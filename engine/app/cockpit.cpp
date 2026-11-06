#include "app/cockpit.h"

#include <algorithm>
#include <cstdlib>

#include "core/bytes.h"
#include "core/log.h"
#include "core/strutil.h"
#include "drive/camera_rig.h"
#include "formats/dat_text.h"
#include "formats/mtx.h"
#include "formats/pkg.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

std::string textOf(const BytesPtr& b) { return std::string(b->begin(), b->end()); }

// Rows X, Y, Z and origin as a column-major matrix: p -> p.x X + p.y Y + p.z Z + o.
mat4 frameMatrix(const vec3& X, const vec3& Y, const vec3& Z, const vec3& o) {
    mat4 m = mat4::identity();
    const vec3* cols[4] = {&X, &Y, &Z, &o};
    for (int c = 0; c < 4; ++c) {
        m.at(0, c) = cols[c]->x;
        m.at(1, c) = cols[c]->y;
        m.at(2, c) = cols[c]->z;
    }
    return m;
}

// Rz(t) of dash.md (row-vector rows (cos t, sin t, 0), (−sin t, cos t, 0)) as a column-vector matrix.
mat4 rotZ(float t) {
    mat4 m = mat4::identity();
    const float c = std::cos(t), s = std::sin(t);
    m.at(0, 0) = c;
    m.at(0, 1) = -s;
    m.at(1, 0) = s;
    m.at(1, 1) = c;
    return m;
}

// Needle angle (dash.md, "Needle angles"): no smoothing.
float needleAngle(float value, float floorV, float top, float rotMin, float rotMax) {
    if (!(top > 0.0f)) return rotMin;
    const float f = std::max(value, floorV) / top;
    float a = (rotMax - rotMin) * f + rotMin;
    const float lo = std::min(rotMin, rotMax), hi = std::max(rotMin, rotMax);
    return std::clamp(a, lo, hi);
}

}  // namespace

MirrorSettings loadMirrorSettings(EngineContext& ctx, const std::string& baseName) {
    MirrorSettings m;
    BytesPtr b = ctx.vfs->open("tune/" + baseName + ".mmMirror");
    if (!b) return m;
    // Size has two numbers, so the keys are read by hand: key, then its numbers.
    std::vector<std::string> tok;
    for (const std::string& line : split(textOf(b), '\n'))
        for (const std::string& w : split(trim(line), ' '))
            for (const std::string& x : split(w, '\t'))
                if (!trim(x).empty()) tok.push_back(trim(x));
    auto num = [&](size_t i, float& out) {
        if (i < tok.size()) out = std::strtof(tok[i].c_str(), nullptr);
    };
    for (size_t i = 0; i < tok.size(); ++i) {
        const std::string& k = tok[i];
        if (k == "Position") {
            num(i + 1, m.position.x);
            num(i + 2, m.position.y);
            num(i + 3, m.position.z);
        } else if (k == "Size") {
            num(i + 1, m.sizeX);
            num(i + 2, m.sizeY);
        } else if (k == "Fov") {
            num(i + 1, m.fov);
        } else if (k == "Aspect") {
            num(i + 1, m.aspect);
        } else if (k == "NearClip") {
            num(i + 1, m.nearClip);
        } else if (k == "FarClip") {
            num(i + 1, m.farClip);
        }
    }
    return m;
}

bool Cockpit::load(EngineContext& ctx, const std::string& baseName) {
    name_ = baseName + "_dash";
    pkg_ = ctx.assets->package(name_, false);
    if (!pkg_) return false;
    // Sequential lookup in this order, level H only (dash.md, "The cockpit package").
    static const char* const kParts[PartCount] = {"damage_needle", "dash",       "gear_indicator", "roof",
                                                  "speed_needle",  "tach_needle", "wheel",          "dash_extra"};
    const PartTable pt = takeParts(*pkg_, std::vector<std::string>(kParts, kParts + PartCount));
    for (int i = 0; i < PartCount; ++i) mesh_[i] = pt.parts[size_t(i)].at(LodH);
    // Pivots: the box centre of <car>_dash_<part>.mtx.
    for (int i : {int(Speed), int(Tach), int(Damage), int(Wheel)}) {
        centre_[i] = vec3();
        BytesPtr b = ctx.vfs->open("geometry/" + name_ + "_" + kParts[i] + ".mtx");
        Pivot p;
        std::string err;
        if (b && parseMtx(b->data(), b->size(), p, err)) centre_[i] = p.centre;
    }
    BytesPtr nb = ctx.vfs->open("tune/" + name_ + ".asNode");
    if (nb) {
        using dat::FieldType;
        Tune& t = t_;
        dat::Schema s{{{"DashPos", FieldType::Vector, &t.dashPos, nullptr},
                       {"RoofPos", FieldType::Vector, &t.roofPos, nullptr},
                       {"WheelPos", FieldType::Vector, &t.wheelPos, nullptr},
                       {"SpeedOffset", FieldType::Vector, &t.speedOffset, nullptr},
                       {"TachOffset", FieldType::Vector, &t.tachOffset, nullptr},
                       {"DmgOffset", FieldType::Vector, &t.dmgOffset, nullptr},
                       {"SpeedPivotOffset", FieldType::Vector, &t.speedPivotOffset, nullptr},
                       {"TachPivotOffset", FieldType::Vector, &t.tachPivotOffset, nullptr},
                       {"DmgPivotOffset", FieldType::Vector, &t.dmgPivotOffset, nullptr},
                       {"WheelPivotOffset", FieldType::Vector, &t.wheelPivotOffset, nullptr},
                       {"GearPivotOffset", FieldType::Vector, &t.gearPivotOffset, nullptr},
                       {"WheelFact", FieldType::Float, &t.wheelFact, nullptr},
                       {"SpeedRotMin", FieldType::Float, &t.speedRotMin, nullptr},
                       {"SpeedRotMax", FieldType::Float, &t.speedRotMax, nullptr},
                       {"RPMRotMin", FieldType::Float, &t.rpmRotMin, nullptr},
                       {"RPMRotMax", FieldType::Float, &t.rpmRotMax, nullptr},
                       {"DamageRotMin", FieldType::Float, &t.damageRotMin, nullptr},
                       {"DamageRotMax", FieldType::Float, &t.damageRotMax, nullptr}}};
        dat::parseText(textOf(nb), s);
    }
    return true;
}

void Cockpit::draw(EngineContext& ctx, const CamPose& cam, float fovDeg, float aspect, float farPlane,
                   const CockpitState& st) const {
    if (!pkg_) return;
    Renderer& r = *ctx.renderer;
    // Near plane 0.01 m, same field of view, aspect and far plane as the scene.
    const mat4 view = mat4::lookAt(cam.p, cam.p - cam.Z, cam.Y);
    const mat4 proj = mat4::perspective(fovDeg * kPi / 180.0f, aspect, 0.01f, farPlane);
    r.setDepthMode(false, farPlane);
    r.setCamera(view, proj, cam.p);
    const mat4 C = frameMatrix(cam.X, cam.Y, cam.Z, cam.p);
    const mat4 dash = C * mat4::translate(t_.dashPos);
    auto turned = [&](Part part, const vec3& pivotOffset, const vec3& offset, float a) {
        const vec3 P = centre_[part] + pivotOffset;
        return dash * mat4::translate(P + offset) * rotZ(-a) * mat4::translate(P * -1.0f);
    };
    const float aSpeed = needleAngle(st.speedMph, 0.0f, 160.0f, t_.speedRotMin, t_.speedRotMax);
    const float aTach = needleAngle(st.rpm, 800.0f, 8000.0f, t_.rpmRotMin, t_.rpmRotMax);
    const float aDmg = needleAngle(st.damage, 0.0f, st.damageLimit, t_.damageRotMin, t_.damageRotMax);
    const float aWheel = st.steer * t_.wheelFact;
    struct Item {
        Part part;
        mat4 model;
        int set;
    };
    // Drawing order (dash.md): dash, roof, gear, speed, rev, damage, extra, wheel.
    const Item items[] = {
        {Dash, dash, 0},
        {Roof, C * mat4::translate(t_.roofPos), 0},
        {Gear, dash * mat4::translate(t_.gearPivotOffset), std::clamp(st.gear, 0, 8)},
        {Speed, turned(Speed, t_.speedPivotOffset, t_.speedOffset, aSpeed), 0},
        {Tach, turned(Tach, t_.tachPivotOffset, t_.tachOffset, aTach), 0},
        {Damage, turned(Damage, t_.dmgPivotOffset, t_.dmgOffset, aDmg), 0},
        {Extra, dash, 0},
        {Wheel, turned(Wheel, t_.wheelPivotOffset, t_.wheelPos, aWheel), 0},
    };
    for (const Item& it : items) {
        const Mesh* m = mesh_[it.part];
        if (!m) continue;
        ModelDraw md;
        md.gpu = ctx.meshes->get(*m);
        md.pkg = pkg_.get();
        md.materialSet = it.set;
        md.model = it.model;
        md.user = name_.c_str();
        r.drawCockpitPart(md, *ctx.textures);
    }
}

}  // namespace cc
