#include "app/hud_view.h"

#include <cmath>

#include "drive/camera_rig.h"
#include "formats/pkg.h"
#include "model/asset_cache.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

// View matrix from camera axes (rows X, Y, Z; the camera looks along −Z) and position.
mat4 viewFromAxes(const vec3& X, const vec3& Y, const vec3& Z, const vec3& eye) {
    mat4 v = mat4::identity();
    const vec3* rows[3] = {&X, &Y, &Z};
    for (int i = 0; i < 3; ++i) {
        v.at(i, 0) = rows[i]->x;
        v.at(i, 1) = rows[i]->y;
        v.at(i, 2) = rows[i]->z;
        v.at(i, 3) = -dot(*rows[i], eye);
    }
    return v;
}

// Column-major frame: p → p.x X + p.y Y + p.z Z + o.
mat4 frame(const vec3& X, const vec3& Y, const vec3& Z, const vec3& o) {
    mat4 m = mat4::identity();
    const vec3* cols[4] = {&X, &Y, &Z, &o};
    for (int c = 0; c < 4; ++c) {
        m.at(0, c) = cols[c]->x;
        m.at(1, c) = cols[c]->y;
        m.at(2, c) = cols[c]->z;
    }
    return m;
}

SimpleVertex sv(const vec3& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    SimpleVertex v;
    v.pos = p;
    v.color[0] = r;
    v.color[1] = g;
    v.color[2] = b;
    v.color[3] = a;
    return v;
}

const Mesh* levelH(const Pkg& p) {
    for (const Mesh& m : p.meshes)
        if (m.name == "H" || (m.name.size() >= 2 && m.name.compare(m.name.size() - 2, 2, "_H") == 0)) return &m;
    return p.meshes.empty() ? nullptr : &p.meshes[0];
}

}  // namespace

bool HudMapView::load(EngineContext& ctx, const std::string& mapName) {
    name_ = mapName;
    map_ = ctx.assets->package("hudmap_" + mapName, false);
    square_ = ctx.assets->package("hudmap_square", false);
    mapMeshes_.clear();
    squareMesh_ = square_ ? levelH(*square_) : nullptr;
    if (map_)
        if (const Mesh* m = levelH(*map_)) mapMeshes_.push_back(m);
    game::HudMapTune t;
    if (BytesPtr b = ctx.vfs->open("tune/" + mapName + ".mmHudMap"))
        game::parseHudMapTune(std::string(b->begin(), b->end()), t);
    state_.setTune(t);
    state_.setAvailable(map_ != nullptr && !mapMeshes_.empty());
    return state_.available();
}

void HudMapView::draw(EngineContext& ctx, const game::MapRect& rect, int W, int H, const vec3& P, const vec3& Zcar,
                      const std::vector<Dot>& dots, const std::vector<CarMarker>& cars, const std::string& cityName) {
    if (!available() || rect.w <= 0 || rect.h <= 0) return;
    Renderer& r = *ctx.renderer;
    int rgb[3];
    game::mapBackground(cityName, rgb);
    const vec3 sea(float(rgb[0]) / 255.0f, float(rgb[1]) / 255.0f, float(rgb[2]) / 255.0f);
    r.setDepthMode(false, game::kMapFar);
    r.setSubViewport(rect.x, H - rect.y - rect.h, rect.w, rect.h, sea);
    // Map camera (hudmap.md): straight down from (P.x, h, P.z); north-up or heading-up.
    vec3 v(Zcar.x, 0.0f, Zcar.z);
    const float vl = length(v);
    v = vl > 0.0f ? v / vl : vec3(0, 0, 0);
    vec3 up(0, 0, -1);  // screen up = world −Z (north-up)
    if (state_.headingUp() && vl > 0.0f) up = -v;  // the car's forward direction points up
    const vec3 Zc(0, 1, 0);
    const vec3 Xc = cross(up, Zc);
    const vec3 eye(P.x, state_.height(), P.z);
    const mat4 view = viewFromAxes(Xc, up, Zc, eye);
    const int mode = state_.mode();
    const mat4 proj = mat4::perspective(game::kMapFov * kPi / 180.0f, game::mapAspect(mode), game::kMapNear, game::kMapFar);
    r.setCamera(view, proj, eye);
    // The map plane, unlit and blended, in world coordinates.
    for (const Mesh* m : mapMeshes_) {
        ModelDraw md;
        md.gpu = ctx.meshes->get(*m);
        md.pkg = map_.get();
        md.materialSet = 0;
        md.model = mat4::identity();
        md.user = "hudmap";
        r.drawCockpitPart(md, *ctx.textures);
    }
    const float s = state_.markerSize();
    // Checkpoint dots: hudmap_square scaled by s / 7.51 at height max(y, 0) + 10.
    if (squareMesh_)
        for (const Dot& d : dots) {
            ModelDraw md;
            md.gpu = ctx.meshes->get(*squareMesh_);
            md.pkg = square_.get();
            md.materialSet = d.set;
            const float k = s / 7.51f;
            md.model = mat4::translate(vec3(d.pos.x, std::max(d.pos.y, 0.0f) + 10.0f, d.pos.z)) * mat4::scale(vec3(k, k, k));
            md.user = "hudmap_square";
            r.drawCockpitPart(md, *ctx.textures);
        }
    // Car markers: flat triangles 15 m above the car, tip at the front, no depth test; the
    // player last (a black triangle 1.3 times the size, then a yellow one on top).
    std::vector<SimpleVertex> tris;
    auto add = [&](const vec3& pos, const vec3& fwd, float size, uint8_t cr, uint8_t cg, uint8_t cb, uint8_t ca = 255) {
        vec3 f(fwd.x, 0.0f, fwd.z);
        f = length(f) > 0.0f ? normalize(f) : vec3(0, 0, -1);
        const vec3 right = cross(f, vec3(0, 1, 0));
        vec3 c[3];
        game::carMarkerCorners(pos, f, right, size, c);
        for (const vec3& q : c) tris.push_back(sv(q, cr, cg, cb, ca));
    };
    for (const CarMarker& c : cars)
        if (!c.player) add(c.pos, c.forward, s, c.rgb[0], c.rgb[1], c.rgb[2], c.a);
    for (const CarMarker& c : cars)
        if (c.player) {
            add(c.pos + vec3(0, -0.05f, 0), c.forward, s * 1.3f, 0, 0, 0);
            add(c.pos, c.forward, s, 255, 255, 0);
        }
    r.drawTriangles(tris, mat4::identity(), false, false);
}

// ---------------------------------------------------------------------------------------------

bool HudArrow::load(EngineContext& ctx) {
    pkg_ = ctx.assets->package("hudarrow01", false);
    mesh_ = pkg_ ? levelH(*pkg_) : nullptr;
    return mesh_ != nullptr;
}

void HudArrow::draw(EngineContext& ctx, const CamPose& cam, float fovDeg, float aspect, const vec3& target) {
    if (!mesh_) return;
    Renderer& r = *ctx.renderer;
    // The target moved to the camera's height, in camera space.
    const vec3 t(target.x, cam.p.y, target.z);
    const vec3 w = t - cam.p;
    vec3 d(dot(w, cam.X), dot(w, cam.Y), dot(w, cam.Z));
    if (d.z < 0.0f) set_ = 0;       // in front of the camera: green
    else if (d.z > 0.0f) set_ = 1;  // behind: yellow (unchanged at exactly 0)
    d = normalize(d);
    if (length(d) == 0.0f) d = vec3(0, 0, -1);
    // The model's −Z end is the tip; the whole arrow is then turned −20° about the camera's X
    // axis through its position (0, 2.5, −6.1), which tips the point down.
    // Local axes in camera space (hud.md): Z = −d, X = (Z.z, 0, −Z.x), Y = Z × X, not normalised.
    const vec3 Zm = -d;
    const vec3 Xm(Zm.z, 0.0f, -Zm.x);
    const vec3 Ym = cross(Zm, Xm);
    const mat4 local = mat4::translate(vec3(0.0f, 2.5f, -6.1f)) * mat4::rotateX(-20.0f * kPi / 180.0f) *
                       frame(Xm, Ym, Zm, vec3(0, 0, 0));
    const mat4 camToWorld = frame(cam.X, cam.Y, cam.Z, cam.p);
    const mat4 view = mat4::lookAt(cam.p, cam.p - cam.Z, cam.Y);
    const mat4 proj = mat4::perspective(fovDeg * kPi / 180.0f, aspect, 0.1f, 100.0f);
    r.setDepthMode(false, 100.0f);
    r.setCamera(view, proj, cam.p);
    ModelDraw md;
    md.gpu = ctx.meshes->get(*mesh_);
    md.pkg = pkg_.get();
    md.materialSet = set_;
    md.model = camToWorld * local;
    md.user = "hudarrow01";
    r.drawCockpitPart(md, *ctx.textures);
}

void drawWorldMarkers(EngineContext& ctx, const CamPose& cam, const std::vector<WorldMarker>& list) {
    if (list.empty()) return;
    std::vector<SimpleVertex> tris;
    for (const WorldMarker& m : list) {
        // A camera-facing triangle pointing down, 1.6 m wide, its centre `lift` above the object.
        const vec3 c = m.pos + vec3(0, m.lift, 0);
        const vec3 rgt = cam.X * 0.8f, upv = cam.Y * 0.7f;
        tris.push_back(sv(c - upv, m.rgb[0], m.rgb[1], m.rgb[2]));
        tris.push_back(sv(c + upv + rgt, m.rgb[0], m.rgb[1], m.rgb[2]));
        tris.push_back(sv(c + upv - rgt, m.rgb[0], m.rgb[1], m.rgb[2]));
    }
    ctx.renderer->drawTriangles(tris, mat4::identity(), false, false);
}

}  // namespace cc
