#include "app/car_view.h"

#include <algorithm>
#include <cmath>

#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "model/asset_cache.h"
#include "platform/platform.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"

namespace cc {

bool CarView::setCar(const CarEntry& entry, EngineContext& ctx) {
    auto m = std::make_unique<VehicleModel>();
    if (!loadVehicle(*ctx.assets, entry.baseName, entry.kind, *m, false)) {
        logError("cannot show %s: %s", entry.baseName.c_str(),
                 m->problems.empty() ? "unknown problem" : m->problems.front().c_str());
        return false;
    }
    entry_ = entry;
    model_ = std::move(m);
    paint_ = 0;
    if (lod_ < LodH || lod_ > LodVL) lod_ = LodH;
    resetCamera();
    return true;
}

void CarView::clear() {
    model_.reset();
    entry_ = CarEntry();
}

void CarView::resetCamera() {
    if (!model_) return;
    vec3 mn = model_->boundsMin, mx = model_->boundsMax;
    if (model_->trailer && opt_.trailer) {
        mn = vmin(mn, model_->trailer->boundsMin + model_->trailerOrigin);
        mx = vmax(mx, model_->trailer->boundsMax + model_->trailerOrigin);
    }
    // The turntable turns about the middle of the whole vehicle.
    pivot_ = vec3((mn.x + mx.x) * 0.5f, 0, (mn.z + mx.z) * 0.5f);
    cam_.target = vec3(pivot_.x, model_->groundY + (mx.y - model_->groundY) * 0.4f, pivot_.z);
    fitSize_ = std::max({mx.z - mn.z, mx.x - mn.x, mx.y - mn.y, 1.0f});
    cam_.minDistance = fitSize_ * 0.35f;
    cam_.maxDistance = fitSize_ * 6.0f + 10.0f;
    cam_.yaw = -2.45f;  // front-left three-quarter view
    cam_.pitch = 0.28f;
    needsFit_ = true;
}

void CarView::setOption(const std::string& name, int v) {
    if (name == "autorotate") autoRotate_ = v != 0;
    else if (name == "shadow") opt_.shadow = v != 0;
    else if (name == "lights") opt_.lights = v != 0;
    else if (name == "trailer") { opt_.trailer = v != 0; resetCamera(); }
    else if (name == "reflections") reflections_ = v != 0;
}

void CarView::update(const InputState& in, float dt, int viewportHeight) {
    bool touched = cam_.update(in, viewportHeight);
    if (touched) idle_ = 0;
    else idle_ += dt;
    // The turntable keeps turning unless the camera was just moved.
    if (autoRotate_ && idle_ > 1.5f) spin_ = std::fmod(spin_ + dt * 0.35f, 2.0f * kPi);
    if (in.hasFocus && in.pressed[KeyR]) resetCamera();
}

void CarView::render(EngineContext& ctx, int width, int height) {
    Renderer& r = *ctx.renderer;
    ctx.textures->setCondition(TextureCondition());  // the showroom shows day colours (night.md)
    r.beginFrame(width, height, vec3(0.2f, 0.22f, 0.25f));
    r.drawGradient(vec3(0.33f, 0.37f, 0.43f), vec3(0.09f, 0.10f, 0.12f));
    float aspect = float(width) / float(height > 0 ? height : 1);
    if (needsFit_) {
        // Fit the vehicle's largest dimension in the narrower field of view.
        float fit = fitSize_ * 1.2f / (aspect < 1.4f ? aspect / 1.4f : 1.0f);
        cam_.distance = std::clamp(fit + 1.0f, cam_.minDistance, cam_.maxDistance);
        needsFit_ = false;
    }
    float far = cam_.maxDistance * 4.0f + 50.0f;
    r.setCamera(cam_.view(), mat4::perspective(38.0f * kPi / 180.0f, aspect, 0.05f, far), cam_.eye());
    Lighting light;
    light.ambient = vec3(0.42f, 0.42f, 0.45f);
    light.count = 2;
    light.lights[0] = {normalize(vec3(0.45f, -0.75f, 0.55f)), vec3(0.75f, 0.73f, 0.68f)};
    light.lights[1] = {normalize(vec3(-0.6f, -0.3f, -0.5f)), vec3(0.25f, 0.27f, 0.32f)};
    r.setLighting(light);
    r.setFog(Fog());
    r.setTime(ctx.time);
    // The showroom reflection map (car-model.md: refl_showroom, else refl_dc).
    const GpuTexture* env = ctx.textures->exists("refl_showroom") ? ctx.textures->get("refl_showroom")
                                                                  : ctx.textures->get("refl_dc");
    r.setEnvironment(reflections_ ? env : nullptr);

    if (!model_) return;
    const mat4 world = mat4::translate(pivot_) * mat4::rotateY(spin_) * mat4::translate(-pivot_);

    // Turntable disc just under the wheels.
    {
        float g = model_->groundY - 0.002f;
        float rad = std::max(model_->boundsMax.z - model_->boundsMin.z, 2.0f) * 0.78f;
        if (model_->trailer && opt_.trailer) rad = fitSize_ * 0.62f;
        std::vector<SimpleVertex> disc;
        const int seg = 72;
        auto vtx = [](float x, float y, float z, uint8_t c, uint8_t a) {
            SimpleVertex v;
            v.pos = vec3(x, y, z);
            v.color[0] = c; v.color[1] = c; v.color[2] = uint8_t(c + 4); v.color[3] = a;
            return v;
        };
        for (int i = 0; i < seg; ++i) {
            float a0 = 2 * kPi * float(i) / seg, a1 = 2 * kPi * float(i + 1) / seg;
            // counter-clockwise seen from above
            disc.push_back(vtx(0, g, 0, 92, 255));
            disc.push_back(vtx(std::sin(a1) * rad, g, std::cos(a1) * rad, 70, 255));
            disc.push_back(vtx(std::sin(a0) * rad, g, std::cos(a0) * rad, 70, 255));
            // soft rim
            float r2 = rad * 1.06f;
            disc.push_back(vtx(std::sin(a0) * rad, g, std::cos(a0) * rad, 70, 255));
            disc.push_back(vtx(std::sin(a1) * rad, g, std::cos(a1) * rad, 70, 255));
            disc.push_back(vtx(std::sin(a1) * r2, g, std::cos(a1) * r2, 40, 0));
            disc.push_back(vtx(std::sin(a0) * rad, g, std::cos(a0) * rad, 70, 255));
            disc.push_back(vtx(std::sin(a1) * r2, g, std::cos(a1) * r2, 40, 0));
            disc.push_back(vtx(std::sin(a0) * r2, g, std::cos(a0) * r2, 40, 0));
        }
        r.drawTriangles(disc, mat4::translate(pivot_) * mat4::rotateY(spin_));
    }

    for (const DrawPart& d : buildStillCar(*model_, lod_, paint_, opt_)) {
        ModelDraw md;
        md.gpu = ctx.meshes->get(*d.mesh);
        md.pkg = d.pkg;
        md.materialSet = d.materialSet;
        md.flags = d.flags;
        md.user = entry_.baseName.c_str();
        md.reflections = reflections_;
        if (d.flags & DrawShadow)
            md.model = world * mat4::translate(d.offset) * mat4::scale(vec3(1, 0, 1));
        else if (d.hasTransform)
            md.model = world * d.transform;
        else
            md.model = world * mat4::translate(d.offset);
        r.drawModel(md, *ctx.textures);
    }
    r.flush();
}

std::string CarView::infoJson() const {
    JsonWriter j;
    j.beginObject();
    if (!model_) {
        j.field("loaded", false);
        j.endObject();
        return j.str();
    }
    const VehicleModel& m = *model_;
    j.field("loaded", true);
    j.field("baseName", entry_.baseName);
    j.field("description", entry_.description);
    j.field("archive", entry_.archive);
    j.field("fromMod", entry_.fromMod);
    j.field("kind", m.kind == VehicleKind::Traffic ? "traffic" : "player");
    j.field("sets", m.setCount());
    j.key("paints").beginArray();
    // Colour list as the menu offers it; sets without a name are added so all can be seen.
    int named = int(entry_.colors.size());
    int total = std::max(named, m.setCount());
    for (int c = 0; c < total; ++c) {
        j.beginObject();
        std::string label = c < named ? entry_.colors[size_t(c)] : strf("Set %d", c);
        j.field("label", label);
        j.field("set", paintSetFor(c, m.setCount()));
        j.field("named", c < named);
        j.endObject();
    }
    j.endArray();
    j.key("lods").beginArray();
    const PartLods* body = m.part(m.kind == VehicleKind::Trailer ? "TRAILER" : "BODY");
    for (int l = 0; l < LodCount; ++l) j.value(body && body->at(Lod(l)) != nullptr);
    j.endArray();
    int parts = 0;
    std::string partNames;
    for (const auto& p : m.parts.parts)
        if (p.any()) {
            ++parts;
            partNames += (partNames.empty() ? "" : " ") + p.name;
        }
    j.field("partCount", parts);
    j.field("parts", partNames);
    j.field("ground", double(m.groundY));
    j.field("length", double(m.boundsMax.z - m.boundsMin.z));
    j.field("width", double(m.boundsMax.x - m.boundsMin.x));
    j.field("height", double(m.boundsMax.y - m.groundY));
    j.field("trailer", m.trailer ? m.trailer->baseName : std::string());
    j.key("problems").beginArray();
    for (const auto& p : m.problems) j.value(latin1ToUtf8(p));
    j.endArray();
    j.endObject();
    return j.str();
}

}  // namespace cc
