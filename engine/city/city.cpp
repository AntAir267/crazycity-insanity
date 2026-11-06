#include "city/city.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

#include "ai/road_network.h"
#include "ai/traffic_control.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/pkg.h"
#include "formats/texture_lookup.h"
#include "fx/car_fx.h"
#include "fx/effects_world.h"
#include "model/asset_cache.h"
#include "render/fx_draw.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

struct Frustum {
    vec4 planes[6];
    explicit Frustum(const mat4& m) {
        auto row = [&](int r) { return vec4(m.at(r, 0), m.at(r, 1), m.at(r, 2), m.at(r, 3)); };
        vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto add = [](vec4 a, vec4 b, float s) { return vec4(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s, a.w + b.w * s); };
        planes[0] = add(r3, r0, 1);
        planes[1] = add(r3, r0, -1);
        planes[2] = add(r3, r1, 1);
        planes[3] = add(r3, r1, -1);
        planes[4] = add(r3, r2, 1);
        planes[5] = add(r3, r2, -1);
        for (auto& p : planes) {
            float l = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
            if (l > 0) p = vec4(p.x / l, p.y / l, p.z / l, p.w / l);
        }
    }
    bool sphere(const vec3& c, float r) const {
        for (const auto& p : planes)
            if (p.x * c.x + p.y * c.y + p.z * c.z + p.w < -r) return false;
        return true;
    }
};

vec3 argbToVec(uint32_t c) {
    return vec3(float((c >> 16) & 255) / 255.0f, float((c >> 8) & 255) / 255.0f, float(c & 255) / 255.0f);
}

const City::Detail kDetail[4] = {{200, 150, 70, 20}, {250, 175, 90, 30}, {300, 200, 100, 40}, {300, 200, 130, 70}};

}  // namespace

City::City() = default;

City::~City() {
    freeSurfaces();
    freePedMeshes();
}

void City::freePedMeshes() {
    for (GpuMesh*& g : pedMeshes_) {
        MeshCache::destroy(g);
        g = nullptr;
    }
    pedMeshes_.clear();
    MeshCache::destroy(pedFarMesh_);
    pedFarMesh_ = nullptr;
}

void City::freeSurfaces() {
    for (auto& c : chunks_) MeshCache::destroy(c.gpu);
    chunks_.clear();
    for (auto& c : roomChunks_)
        if (c.gpu) MeshCache::destroy(c.gpu);
    roomChunks_.clear();
}

City::Detail City::detail() const { return kDetail[std::clamp(opt_.objectDetail, 0, 3)]; }

bool City::load(const std::string& name, EngineContext& ctx, std::string& message, const std::string& raceDir,
                const LevelSet& levelSet) {
    if (!data_.load(name, *ctx.vfs, *ctx.assets, message, raceDir, levelSet)) return false;
    const Psdl& psdl = data_.psdl();
    if (const SkyInfo* sky = data_.sky()) {
        skyPkg_ = ctx.assets->package(sky->model, true);
        skyMesh_ = nullptr;
        if (skyPkg_) {
            PartTable t = takeParts(*skyPkg_, {""});
            skyMesh_ = t.parts[0].at(LodH);
        }
    }
    ctx.textures->setCondition(textureCondition());
    fetchSlotTextures(ctx);
    buildSurfaces(ctx);
    extentMin_ = psdl.boundsMin;
    extentMax_ = psdl.boundsMax;
    for (const CityObject& o : data_.objects()) {
        extentMin_ = vmin(extentMin_, o.pos - vec3(o.radius, o.radius, o.radius));
        extentMax_ = vmax(extentMax_, o.pos + vec3(o.radius, o.radius, o.radius));
    }
    // Spanning objects are filed in every room they touch (drawn when any is in the room list).
    spanRooms_.assign(data_.objects().size(), {});
    for (size_t i = 0; i < data_.objects().size(); ++i) {
        const CityObject& o = data_.objects()[i];
        if (o.kind == CityObjectKind::Spanning) spanRooms_[i] = data_.touched(o.room, o.pos, o.modelRadius, 32);
    }
    {
        const auto t0 = std::chrono::steady_clock::now();
        collision_.setupCity(*ctx.vfs, data_);
        logInfo("city %s: %zu collision polygons, %zu collision objects (%.0f ms)", name.c_str(),
                collision_.cityPolygonCount(), collision_.objects().size(),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
    message = summary();
    logInfo("city %s: %s", name.c_str(), message.c_str());
    return true;
}

void City::buildSurfaces(EngineContext& ctx) {
    freeSurfaces();
    const Psdl& psdl = data_.psdl();
    const int idx = conditionIndex(opt_.timeOfDay, opt_.weather);
    PsdlBuildOptions bo;
    bo.roomColors = data_.roomColors();
    if (const CityLights* l = data_.lights(idx)) buildWallLightTable(*l, bo.wallLights);
    // Wall shading (viewer.md, our option): Soft squeezes every channel c into MM2's default-table
    // range, floor(118 + c·118/255) (255 -> 236); MM2 exact keeps the table; Flat makes every
    // entry white.
    for (uint32_t& e : bo.wallLights) {
        if (opt_.wallShading == 2) {
            e = 0xFFFFFFFFu;
        } else if (opt_.wallShading == 0) {
            uint32_t out = 0xFF000000u;
            for (int sh = 0; sh <= 16; sh += 8) {
                const int c = int((e >> sh) & 255u);
                const int squeezed = 118 + c * 118 / 255;  // integer floor of a non-negative value
                out |= uint32_t(squeezed) << sh;
            }
            e = out;
        }
    }
    PsdlGeometry g = buildPsdlGeometry(psdl, bo);
    for (const auto& p : g.problems) logFile(LogLevel::Warn, "city/" + data_.name() + ".psdl", p);
    triangles_ = g.triangles;

    // Group rooms into square chunks; one mesh per chunk, one batch per texture slot.
    const float cell = 300.0f;
    std::map<std::pair<int, int>, std::vector<size_t>> cells;
    for (size_t r = 1; r < g.rooms.size(); ++r) {
        if (g.rooms[r].empty) continue;
        vec3 c = (g.rooms[r].boundsMin + g.rooms[r].boundsMax) * 0.5f;
        cells[{int(std::floor(c.x / cell)), int(std::floor(c.z / cell))}].push_back(r);
    }
    auto buildChunk = [&](const std::vector<size_t>& roomList) {
        Mesh mesh;
        mesh.fvf = kFvfXYZ | kFvfDiffuse | kFvfTex1;
        std::map<int, size_t> slotBatch;
        bool first = true;
        for (size_t r : roomList) {
            const PsdlRoomGeometry& rg = g.rooms[r];
            if (first) {
                mesh.boundsMin = rg.boundsMin;
                mesh.boundsMax = rg.boundsMax;
                first = false;
            } else {
                mesh.boundsMin = vmin(mesh.boundsMin, rg.boundsMin);
                mesh.boundsMax = vmax(mesh.boundsMax, rg.boundsMax);
            }
            for (const PsdlSurface& s : rg.surfaces) {
                auto it = slotBatch.find(s.slot);
                if (it == slotBatch.end() || mesh.batches[it->second].vertices.size() + s.vertices.size() > 65535) {
                    mesh.batches.emplace_back();
                    mesh.batches.back().material = s.slot;
                    slotBatch[s.slot] = mesh.batches.size() - 1;
                    it = slotBatch.find(s.slot);
                }
                Batch& bt = mesh.batches[it->second];
                const uint32_t baseV = uint32_t(bt.vertices.size());
                for (const CityVertex& cv : s.vertices) {
                    Vertex v;
                    v.pos = cv.pos;
                    v.uv = cv.uv;
                    std::copy(cv.color, cv.color + 4, v.color);
                    bt.vertices.push_back(v);
                }
                for (uint32_t i : s.indices) bt.indices.push_back(uint16_t(baseV + i));
            }
        }
        std::sort(mesh.batches.begin(), mesh.batches.end(),
                  [](const Batch& a, const Batch& b) { return a.material < b.material; });
        Chunk ch;
        ch.gpu = MeshCache::upload(mesh);
        ch.centre = (mesh.boundsMin + mesh.boundsMax) * 0.5f;
        ch.radius = length(mesh.boundsMax - mesh.boundsMin) * 0.5f;
        return ch;
    };
    for (auto& kv : cells) chunks_.push_back(buildChunk(kv.second));
    // One mesh per room as well, for the "MM2" draw distance's room list (cpvs.md).
    roomChunks_.assign(g.rooms.size(), Chunk());
    for (size_t r = 1; r < g.rooms.size(); ++r)
        if (!g.rooms[r].empty && !g.rooms[r].surfaces.empty()) roomChunks_[r] = buildChunk({r});

    // Ground lookup: upward-facing triangles.
    groundTris_.clear();
    gridMin_ = psdl.boundsMin;
    vec3 ext = psdl.boundsMax - psdl.boundsMin;
    gridW_ = std::max(1, int(ext.x / cellSize_) + 1);
    gridH_ = std::max(1, int(ext.z / cellSize_) + 1);
    grid_.assign(size_t(gridW_) * size_t(gridH_), {});
    for (size_t r = 1; r < g.rooms.size(); ++r) {
        for (const PsdlSurface& s : g.rooms[r].surfaces) {
            for (size_t i = 0; i + 2 < s.indices.size(); i += 3) {
                vec3 a = s.vertices[s.indices[i]].pos, b = s.vertices[s.indices[i + 1]].pos,
                     c = s.vertices[s.indices[i + 2]].pos;
                vec3 n = cross(b - a, c - a);
                float l = length(n);
                if (l <= 1e-6f || n.y / l < 0.5f) continue;
                uint32_t ti = uint32_t(groundTris_.size() / 3);
                groundTris_.push_back(a);
                groundTris_.push_back(b);
                groundTris_.push_back(c);
                vec3 mn = vmin(a, vmin(b, c)), mx = vmax(a, vmax(b, c));
                int x0 = std::clamp(int((mn.x - gridMin_.x) / cellSize_), 0, gridW_ - 1);
                int x1 = std::clamp(int((mx.x - gridMin_.x) / cellSize_), 0, gridW_ - 1);
                int z0 = std::clamp(int((mn.z - gridMin_.z) / cellSize_), 0, gridH_ - 1);
                int z1 = std::clamp(int((mx.z - gridMin_.z) / cellSize_), 0, gridH_ - 1);
                for (int z = z0; z <= z1; ++z)
                    for (int x = x0; x <= x1; ++x) grid_[size_t(z) * size_t(gridW_) + size_t(x)].push_back(ti);
            }
        }
    }
}

void City::setOptions(const CityOptions& opt, EngineContext& ctx) {
    bool rebuild = opt.timeOfDay != opt_.timeOfDay || opt.weather != opt_.weather || opt.wallShading != opt_.wallShading;
    opt_ = opt;
    opt_.objectDetail = std::clamp(opt_.objectDetail, 0, 3);
    if (rebuild && !data_.psdl().rooms.empty()) buildSurfaces(ctx);  // wall shading follows the lights
}

void City::updateLevelObjects(float F, const std::vector<vec3>& cars) {
    data_.levelObjects().update(F, data_, cars);
    collision_.syncLevelObjects(data_.levelObjects());
}

void City::resetLevelObjects() {
    data_.levelObjects().reset(data_);
    collision_.syncLevelObjects(data_.levelObjects());
}

void City::update(float dt) {
    if (const SkyInfo* sky = data_.sky()) skyAngle_ = std::fmod(skyAngle_ + sky->spin * dt, 2.0f * kPi);
    if (!driving_) updateLevelObjects(dt, {});
    // Traffic lights: once per frame, after the AI vehicles (traffic-control.md, "The cycle"). While
    // a drive runs, its traffic update advances them.
    if (!trafficRunning_)
        if (ai::RoadNetwork* net = data_.network()) net->control().update(dt);
}

void City::resetTraffic() {
    if (ai::RoadNetwork* net = data_.network()) net->control().reset();
}

void City::startTraffic(EngineContext& ctx, float density, const std::vector<ai::TrafficPlayer>& players) {
    trafficRunning_ = data_.network() != nullptr;
    if (!trafficRunning_) return;
    traffic_.create(*ctx.vfs, *ctx.assets, data_, collision_, density);
    traffic_.reset(players);
}

void City::stopTraffic() {
    trafficRunning_ = false;
    traffic_.clear();
}

void City::startPolice(EngineContext& ctx, bool on, const std::string& playerCar, std::shared_ptr<phys::Bound> playerBound) {
    police_.clear();
    if (!on || !data_.network()) return;
    police_.create(*ctx.vfs, *ctx.assets, data_, *data_.network(), collision_, 1.0f, playerCar, playerBound);
    police_.reset();
}

void City::startPoliceDensity(EngineContext& ctx, float density, const std::string& playerCar,
                              std::shared_ptr<phys::Bound> playerBound) {
    police_.clear();
    if (!(density > 0.0f) || !data_.network()) return;
    police_.create(*ctx.vfs, *ctx.assets, data_, *data_.network(), collision_, density, playerCar, playerBound);
    police_.reset();
}

void City::stopPolice() { police_.clear(); }

void City::startPedestrians(EngineContext& ctx, int count, bool rain, const std::vector<ai::PedPlayer>& players) {
    peds_.clear();
    if (count <= 0 || !data_.network()) return;
    peds_.create(*ctx.vfs, *ctx.assets, data_, collision_, count, rain);
    peds_.reset(players);
}

void City::stopPedestrians() {
    peds_.clear();
    freePedMeshes();
}

const City::LightMeshes& City::lightMeshesOf(const Pkg* pkg) {
    for (const LightMeshes& l : lightMeshes_)
        if (l.pkg == pkg) return l;
    LightMeshes l;
    l.pkg = pkg;
    static const char* const kNames[10] = {"REDGLOWDAY_H",   "YELLOWGLOWDAY_H",   "GREENGLOWDAY_H",   "WALK_DAY_H",   "NOWALK_DAY_H",
                                           "REDGLOWNIGHT_H", "YELLOWGLOWNIGHT_H", "GREENGLOWNIGHT_H", "WALK_NIGHT_H", "NOWALK_NIGHT_H"};
    for (int i = 0; i < 10; ++i)
        for (const PkgEntry& e : pkg->entries)
            if (e.mesh >= 0 && iequals(e.name, kNames[i])) {
                l.m[i] = &pkg->meshes[size_t(e.mesh)];
                break;
            }
    lightMeshes_.push_back(l);
    return lightMeshes_.back();
}

void City::drawTrafficLights(EngineContext& ctx, const vec3& eye, const vec3& viewDir, const mat4& viewProj, bool fullMap) {
    // traffic-control.md, "What the lights show": two meshes per standing pole, DAY or NIGHT set.
    const ai::RoadNetwork* net = data_.network();
    if (!net || !opt_.props) return;
    const Frustum fr(viewProj);
    const Detail dt = detail();
    const auto& rooms = data_.rooms();
    const auto& objects = data_.objects();
    const int base = opt_.timeOfDay >= 2 ? 5 : 0;
    for (const ai::LightSet& set : net->control().sets())
        for (const ai::TrafficLight& l : set.lights) {
            if (l.pole < 0 || collision_.cityObjectBroken(l.pole)) continue;
            const CityObject& o = objects[size_t(l.pole)];
            if (o.room <= 0 || size_t(o.room) >= rooms.size()) continue;
            if (!listed(o.room)) continue;
            if (!fullMap) {
                const CityRoom& rr = rooms[size_t(o.room)];
                const float d = o.room == lastCamRoom_ ? -rr.radius : dot(rr.centre - eye, viewDir) - rr.radius;
                if (d > dt.roomLimit) continue;
            }
            if (!fr.sphere(o.pos, o.radius + 1.0f)) continue;
            const LightMeshes& lm = lightMeshesOf(o.pkg.get());
            int a = 0, b = 4;  // mesh indices within the set: 0 red, 1 yellow, 2 green, 3 walk, 4 no walk
            switch (l.state) {
            case ai::LightRed: a = 0; b = 4; break;
            case ai::LightYellow: a = 1; b = 4; break;
            case ai::LightGreen: a = 2; b = 4; break;
            case ai::LightWalk: a = 0; b = 3; break;
            default: a = 0; b = 4; break;
            }
            const Mesh* ma = lm.m[base + a];
            const Mesh* mb = lm.m[base + b];
            if (!ma || !mb) continue;  // either mesh missing: nothing drawn for this pole
            for (const Mesh* m : {ma, mb}) {
                ModelDraw md;
                md.gpu = ctx.meshes->get(*m);
                md.pkg = o.pkg.get();
                md.materialSet = 0;
                md.model = l.placement;
                md.user = o.name.c_str();
                ctx.renderer->drawGlowModel(md, *ctx.textures);
            }
        }
}

void City::buildAiOverlay() {
    overlayStatic_.clear();
    overlayBuilt_ = true;
    const ai::RoadNetwork* net = data_.network();
    if (!net) return;
    auto vert = [](const vec3& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        SimpleVertex v;
        v.pos = p;
        v.color[0] = r;
        v.color[1] = g;
        v.color[2] = b;
        v.color[3] = a;
        return v;
    };
    auto& out = overlayStatic_;
    auto line = [&](const vec3& a, const vec3& b, const uint8_t* c) {
        out.push_back(vert(a, c[0], c[1], c[2], c[3]));
        out.push_back(vert(b, c[0], c[1], c[2], c[3]));
    };
    // A piece sampled every ~1.5 m; heights interpolated between ya and yb.
    auto piece = [&](const ai::Piece& p, float ya, float yb, const uint8_t* c) {
        if (!(p.L > 0.0f)) return;
        const int steps = std::clamp(int(p.L / 1.5f) + 1, 2, 60);
        vec3 prev;
        for (int i = 0; i <= steps; ++i) {
            const float d = p.L * float(i) / float(steps);
            vec3 q = ai::RoadNetwork::directPosition(p, d);
            q.y = ya + (yb - ya) * (float(i) / float(steps)) + 0.3f;
            if (i > 0) line(prev, q, c);
            prev = q;
        }
    };
    const uint8_t kSideB[4] = {70, 200, 255, 255}, kSideA[4] = {255, 160, 50, 255}, kClosed[4] = {150, 150, 150, 160},
                  kShortcut[4] = {230, 80, 230, 220}, kTurn[4] = {220, 240, 90, 200}, kWalk[4] = {200, 200, 200, 90},
                  kCable[4] = {150, 100, 60, 255}, kSubway[4] = {60, 60, 220, 255}, kNode[4] = {255, 255, 255, 255};
    const auto& roads = net->roads();
    for (int ri = 0; ri < int(roads.size()); ++ri) {
        const ai::Road& r = roads[size_t(ri)];
        if (r.n < 2) continue;
        for (int s : {1, -1}) {
            const ai::RoadSide& sd = r.sideOf(s);
            const uint8_t* c = r.shortcut ? kShortcut : !sd.open() ? kClosed : s > 0 ? kSideB : kSideA;
            for (int l = 0; l < sd.lanes; ++l) {
                const ai::Lane& lane = sd.lines[size_t(l)];
                for (int k = 1; k <= r.n - 1; ++k) {
                    const ai::Piece p = net->lanePiece(ri, s, l, k, 0.0f, 0.0f, r.n == 2 ? ai::PieceSetup::Start : ai::PieceSetup::Advance);
                    piece(p, lane.P[size_t(k - 1)].y, lane.P[size_t(k)].y, c);
                }
                // A chevron at the lane's middle point showing the travel direction.
                const int mid = r.n / 2;
                const vec3 at = lane.P[size_t(mid)] + vec3(0, 0.3f, 0);
                const vec3 t = net->travelTangent(ri, s, mid);
                const vec3 side(-t.z, 0, t.x);
                line(at, at - t * 1.6f + side * 0.9f, c);
                line(at, at - t * 1.6f - side * 0.9f, c);
                // Turn paths to every possible next road (f = 2.2 m, o = 0).
                if (sd.open() && r.n >= 3 && !r.shortcut)
                    for (const ai::NextChoice& o : net->nextOptions(ri, s, l)) {
                        const float t0 = net->turnStart(ri, s, l, 2.2f);
                        const ai::Piece tp = net->turnPiece(ri, s, l, 0.0f, t0, o);
                        const ai::Lane& nl = roads[size_t(o.road)].sideOf(o.side).lines[size_t(o.lane)];
                        piece(tp, lane.P[size_t(r.n - 1)].y, nl.P[0].y, kTurn);
                    }
            }
            for (int w = 0; w < sd.sidewalks; ++w) {
                const ai::Lane& lane = sd.lines[size_t(sd.lanes + w)];
                for (int i = 1; i < r.n; ++i)
                    line(lane.P[size_t(i - 1)] + vec3(0, 0.3f, 0), lane.P[size_t(i)] + vec3(0, 0.3f, 0), kWalk);
            }
            if (!sd.cableLine.empty())
                for (int i = 1; i < r.n; ++i)
                    line(sd.cableLine[size_t(i - 1)] + vec3(0, 0.35f, 0), sd.cableLine[size_t(i)] + vec3(0, 0.35f, 0), kCable);
            if (!sd.subwayLine.empty())
                for (int i = 1; i < r.n; ++i)
                    line(sd.subwayLine[size_t(i - 1)] + vec3(0, 0.35f, 0), sd.subwayLine[size_t(i)] + vec3(0, 0.35f, 0), kSubway);
        }
    }
    // Intersections: a cross and a small square; stop approaches: a red octagon at the sign.
    for (const ai::Intersection& x : net->intersections()) {
        const vec3 c = x.centre + vec3(0, 0.4f, 0);
        line(c + vec3(-2, 0, 0), c + vec3(2, 0, 0), kNode);
        line(c + vec3(0, 0, -2), c + vec3(0, 0, 2), kNode);
        const float h = x.lightSet >= 0 ? 3.0f : 1.2f;
        line(c + vec3(-h, 0, -h), c + vec3(h, 0, -h), kNode);
        line(c + vec3(h, 0, -h), c + vec3(h, 0, h), kNode);
        line(c + vec3(h, 0, h), c + vec3(-h, 0, h), kNode);
        line(c + vec3(-h, 0, h), c + vec3(-h, 0, -h), kNode);
    }
    const uint8_t kStop[4] = {255, 40, 40, 255};
    for (int ri = 0; ri < net->mainRoadCount(); ++ri)
        for (const ai::RoadEnd& e : roads[size_t(ri)].end) {
            if (e.control != 0) continue;
            const vec3 c = e.signPos + vec3(0, 0.5f, 0);
            for (int k = 0; k < 8; ++k) {
                const float a0 = float(k) * kPi / 4.0f + kPi / 8.0f, a1 = float(k + 1) * kPi / 4.0f + kPi / 8.0f;
                line(c + vec3(std::cos(a0), 0, std::sin(a0)) * 1.2f, c + vec3(std::cos(a1), 0, std::sin(a1)) * 1.2f, kStop);
            }
            line(c, c + vec3(0, 2.5f, 0), kStop);
        }
}

void City::drawAiOverlay(EngineContext& ctx) {
    const ai::RoadNetwork* net = data_.network();
    if (!net) return;
    if (!overlayBuilt_) buildAiOverlay();
    Renderer& r = *ctx.renderer;
    r.drawLines(overlayStatic_, mat4::identity(), true);
    // Light states: a post and a square at every light's sign position, coloured by its state.
    std::vector<SimpleVertex> lights;
    auto add = [&](const vec3& a, const vec3& b, const uint8_t* c) {
        SimpleVertex v;
        v.pos = a;
        for (int i = 0; i < 4; ++i) v.color[i] = c[i];
        lights.push_back(v);
        v.pos = b;
        lights.push_back(v);
    };
    const uint8_t kRed[4] = {255, 30, 30, 255}, kYellow[4] = {255, 220, 0, 255}, kGreen[4] = {40, 255, 60, 255},
                  kWalk[4] = {255, 255, 255, 255};
    const auto& roads = net->roads();
    for (const ai::LightSet& set : net->control().sets())
        for (const ai::TrafficLight& l : set.lights) {
            const uint8_t* c = l.state == ai::LightGreen ? kGreen : l.state == ai::LightYellow ? kYellow : kRed;
            // A thick stop line across the approach's lanes at their ends, in the light's colour.
            const ai::Road& rd = roads[size_t(l.road)];
            const ai::RoadSide& sd = rd.sideOf(l.arrivingSide);
            if (sd.lanes > 0 && rd.n >= 2) {
                const vec3 a = sd.lines[0].P[size_t(rd.n - 1)], b = sd.lines[size_t(sd.lanes - 1)].P[size_t(rd.n - 1)];
                const vec3 t = net->endDir(l.road, l.arrivingSide);
                const vec3 across = normalize(vec3(-t.z, 0, t.x));
                const float ea = dot(a - b, across) >= 0.0f ? 1.8f : -1.8f;
                const vec3 a2 = a + across * ea, b2 = b - across * ea;
                for (int k = 0; k < 7; ++k) {
                    const vec3 o = t * (-0.25f * float(k)) + vec3(0, 0.35f, 0);
                    add(a2 + o, b2 + o, c);
                }
            }
            // The pole: a short post with the state's colour on top, white tip while pedestrians walk.
            const vec3 p = l.signPos;
            add(p + vec3(0, 3.2f, 0), p + vec3(0, 4.6f, 0), c);
            if (l.state == ai::LightWalk) add(p + vec3(0, 4.6f, 0), p + vec3(0, 5.4f, 0), kWalk);
        }
    r.drawLines(lights, mat4::identity(), true);
}

bool City::groundBelow(float x, float z, float maxY, float& y) const {
    if (grid_.empty()) return false;
    int cx = int((x - gridMin_.x) / cellSize_), cz = int((z - gridMin_.z) / cellSize_);
    if (cx < 0 || cz < 0 || cx >= gridW_ || cz >= gridH_) return false;
    bool found = false;
    for (uint32_t ti : grid_[size_t(cz) * size_t(gridW_) + size_t(cx)]) {
        const vec3& a = groundTris_[ti * 3];
        const vec3& b = groundTris_[ti * 3 + 1];
        const vec3& c = groundTris_[ti * 3 + 2];
        float d = (b.z - c.z) * (a.x - c.x) + (c.x - b.x) * (a.z - c.z);
        if (std::fabs(d) < 1e-9f) continue;
        float l1 = ((b.z - c.z) * (x - c.x) + (c.x - b.x) * (z - c.z)) / d;
        float l2 = ((c.z - a.z) * (x - c.x) + (a.x - c.x) * (z - c.z)) / d;
        float l3 = 1.0f - l1 - l2;
        if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) continue;
        float h = l1 * a.y + l2 * b.y + l3 * c.y;
        if (h > maxY) continue;
        if (!found || h > y) {
            y = h;
            found = true;
        }
    }
    return found;
}

vec3 City::clearColor() const {
    const int idx = conditionIndex(opt_.timeOfDay, opt_.weather);
    if (size_t(idx) < data_.fog().size()) return data_.fog()[size_t(idx)].color;
    return vec3(0.6f, 0.65f, 0.7f);
}

std::string City::summary() const {
    return strf("%zu rooms, %zu triangles, %zu objects, %zu texture slots", data_.psdl().rooms.size() - 1, triangles_,
                data_.objects().size(), data_.psdl().textures.size() - 1);
}

bool City::canBlend(const Pkg* pkg, EngineContext& ctx) {
    // Decided once from material set 0 (pkg.md, "Translucency").
    for (const auto& e : blendCache_)
        if (e.first == pkg) return e.second;
    bool can = false;
    for (int i = 0; i < pkg->materialsPerSet && !can; ++i) {
        const Material* m = pkg->material(0, i);
        if (!m) continue;
        if (m->diffuse.w != 1.0f) can = true;
        else if (!m->texture.empty()) {
            const GpuTexture* t = ctx.textures->get(m->texture);
            if (t && t->hasAlpha) can = true;
        }
    }
    blendCache_.emplace_back(pkg, can);
    return can;
}

void City::placeRain(const mat4& view, const vec3& eye) {
    // Where rain shows (night.md, section 6), from the room holding the camera (no hint).
    effects_->hideRain();
    if (opt_.weather != 3) return;
    const int room = data_.findRoom(eye, 0);
    const auto& rooms = data_.rooms();
    if (room > 0 && size_t(room) < rooms.size()) {
        const CityRoom& R = rooms[size_t(room)];
        if (R.flags & PsdlEnclosed) return;  // tunnels
        if (R.hasLandmark) {
            phys::ProbeHit hit;
            if (collision_.probeStatic(vec3(eye.x, eye.y + 100.0f, eye.z), eye, hit)) return;  // under a roof
        }
    }
    // 10 m above and 10 m in front of the camera: c + 10·Y − 10·Z (Z = the camera's back axis).
    const vec3 Y(view.m[1], view.m[5], view.m[9]), Z(view.m[2], view.m[6], view.m[10]);
    effects_->setRainPoint(vec3(eye.x + 10.0f * Y.x - 10.0f * Z.x, eye.y + 10.0f * Y.y - 10.0f * Z.y,
                                eye.z + 10.0f * Y.z - 10.0f * Z.z));
}

Lod City::carLod(const CityCar& car, const vec3& eye, const vec3& viewDir) const {
    const VehicleModel& m = *car.model;
    // Level: view depth of the car origin minus the body radius (car-model.md).
    float bodyRadius = 0;
    if (const PartLods* body = m.part(m.kind == VehicleKind::Trailer ? "TRAILER" : "BODY"))
        for (int l = 0; l < LodCount; ++l)
            if (body->at(Lod(l))) bodyRadius = std::max(bodyRadius, body->at(Lod(l))->radius);
    const Detail dt = detail();
    float d = dot(car.world.translation() - eye, viewDir) - bodyRadius;
    return d > dt.t3 ? LodVL : d > dt.t2 ? LodL : d > dt.t1 ? LodM : LodH;
}

void City::drawCar(EngineContext& ctx, const CityCar& car, const vec3& eye, const vec3& viewDir) {
    if (!car.model || car.bodyHidden) return;
    const VehicleModel& m = *car.model;
    const Lod lod = carLod(car, eye, viewDir);
    StillCarOptions so;
    so.lights = false;  // light glows are drawn in the glow pass (night.md)
    std::vector<DrawPart> parts;
    const int set = paintSetFor(car.paint, m.setCount());
    if (m.kind == VehicleKind::Trailer) addTrailerParts(m, lod, set, car.braking, parts, car.wheelPose);  // trailer.md 11
    else addCarParts(m, lod, set, vec3(), so, parts, car.wheelPose);
    // Texture damage: the working copies at H and M, the normal textures at L and VL.
    const std::vector<const GpuTexture*>* damaged =
        car.fx && (lod == LodH || lod == LodM) ? damageSlotTextures(car.fx->textures()) : nullptr;
    for (const DrawPart& dp : parts) {
        ModelDraw md;
        md.gpu = ctx.meshes->get(*dp.mesh);
        md.pkg = dp.pkg;
        md.materialSet = dp.materialSet;
        md.flags = dp.flags;
        md.reflections = opt_.reflections;
        md.inlineBlend = true;
        md.slotTextures = dp.pkg == m.pkg.get() ? damaged : nullptr;
        md.user = m.baseName.c_str();
        md.model = dp.hasTransform ? car.world * dp.transform : car.world * mat4::translate(dp.offset);
        ctx.renderer->drawModel(md, *ctx.textures);
    }
}

void City::drawGlows(EngineContext& ctx, const std::vector<CityCar>& cars, const vec3& eye, const vec3& viewDir,
                     const mat4& viewProj, bool fullMap) {
    // The glow pass (night.md, section 4): after everything else; additive, no fog.
    Renderer& r = *ctx.renderer;
    const Frustum fr(viewProj);
    const Detail dt = detail();
    const bool night = opt_.timeOfDay == 3;
    const bool headlights = opt_.timeOfDay >= 2 || opt_.weather == 2;
    const auto& rooms = data_.rooms();
    auto roomDistance = [&](int room) {
        const CityRoom& rr = rooms[size_t(room)];
        return room == lastCamRoom_ ? -rr.radius : dot(rr.centre - eye, viewDir) - rr.radius;
    };
    auto rnd = [this]() {  // visual flicker only (never the physics generator)
        glowRand_ = glowRand_ * 1664525u + 1013904223u;
        return float((glowRand_ >> 8) & 0xFFFFFF) / 16777216.0f;
    };

    // Street lamps and other lit props, at night.
    if (night && opt_.props) {
        std::vector<Billboard> lamps;
        const auto& objects = data_.objects();
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            const CityObject& o = objects[oi];
            if (o.glows.empty() || collision_.cityObjectBroken(int(oi))) continue;
            if (o.room <= 0 || size_t(o.room) >= rooms.size()) continue;
            if (!listed(o.room)) continue;
            if (!fullMap && roomDistance(o.room) > dt.roomLimit) continue;
            for (const vec3& g : o.glows) {
                Billboard b;
                b.centre = o.drawWorld.transformPoint(g);  // GlowOffset in the CG-shifted frame
                b.half = 1.5f * (0.99f + 0.01f * rnd());
                if (!fr.sphere(b.centre, b.half)) continue;
                lamps.push_back(b);
            }
        }
        r.drawBillboards(lamps, ctx.textures->get("s_yel_glow"));
    }

    // Cars: tail, brake and reversing light meshes, then the headlight billboards.
    std::vector<Billboard> heads;
    for (const CityCar& car : cars) {
        if (!car.model || car.bodyHidden) continue;
        const VehicleModel& m = *car.model;
        if (m.kind == VehicleKind::Trailer) continue;  // trailers have no glows (trailer.md 11)
        const int set = paintSetFor(car.paint, m.setCount());
        auto glowPart = [&](const char* name) {
            const PartLods* p = m.part(name);
            if (!p) return;
            const Mesh* mesh = p->at(LodH);  // light meshes always from the H slot (night.md)
            if (!mesh) return;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*mesh);
            md.pkg = m.pkg.get();
            md.materialSet = set;
            md.model = car.world;
            md.user = m.baseName.c_str();
            r.drawGlowModel(md, *ctx.textures);
        };
        // TLIGHT for braking and again for headlights: both draw it twice (night.md, "Cars").
        if (car.braking) glowPart("TLIGHT");
        if (headlights) glowPart("TLIGHT");
        if (car.braking) glowPart("BLIGHT");
        if (car.reverse) glowPart("RLIGHT");
        const bool flash = car.lightBar && car.lightBar->on;
        if (flash) {
            // Light bar glows (police.md, "Drawing" 2): intensity 25/r², exponent 3, any time of day.
            for (const SirenLight& l : car.lightBar->lights) {
                Billboard b;
                b.centre = car.world.transformPoint(l.pos);
                const vec3 d = eye - b.centre;
                const float r2 = dot(d, d);
                const float c = r2 > 0.0f ? dot(d, l.beam) / std::sqrt(r2) : 0.0f;
                if (c < 0.0f || r2 <= 0.0f) continue;
                const float I = (25.0f / r2) * c * c * c;
                b.half = 0.2f * std::sqrt(r2 * I);
                auto byte = [](float v) { return uint8_t(std::clamp(v * 0.95f, 0.0f, 1.0f) * 255.0f + 0.5f); };
                b.color[0] = byte(l.color.x);
                b.color[1] = byte(l.color.y);
                b.color[2] = byte(l.color.z);
                b.color[3] = 255;
                heads.push_back(b);
            }
        }
        if (!headlights && !flash) continue;
        const vec3 forward(-car.world.m[8], -car.world.m[9], -car.world.m[10]);  // the car's −Z
        for (int k = 0; k < 2; ++k) {
            const char* name = k == 0 ? "HEADLIGHT0" : "HEADLIGHT1";
            const Pivot* pv = m.pivotOf(name);
            if (!pv || !pv->found) continue;
            Billboard b;
            b.centre = car.world.transformPoint(pv->origin);
            const vec3 d = eye - b.centre;
            const float len = length(d);
            if (len <= 0.0f) continue;
            // Flashing headlights: the beams spin about Y (police.md, "Drawing" 3).
            const vec3 beam = flash ? car.lightBar->head[k] : forward;
            const float c = dot(d * (1.0f / len), normalize(beam));
            if (c < 0.0f) continue;
            b.half = c * std::sqrt(c);  // 0.2·√(25·c³) = c^1.5
            // Colour: 0.95 × the diffuse of the first batch's material in the current paint set.
            vec4 col(1, 1, 1, 1);
            if (const PartLods* hp = m.part(name)) {
                const Mesh* hm = nullptr;
                for (int l = 0; l < LodCount && !hm; ++l) hm = hp->at(Lod(l));
                if (hm && !hm->batches.empty())
                    if (const Material* mat = m.pkg->material(set, hm->batches[0].material)) col = mat->diffuse;
            }
            auto byte = [](float v) { return uint8_t(std::clamp(v * 0.95f, 0.0f, 1.0f) * 255.0f + 0.5f); };
            b.color[0] = byte(col.x);
            b.color[1] = byte(col.y);
            b.color[2] = byte(col.z);
            b.color[3] = 255;
            heads.push_back(b);
        }
    }
    if (trafficRunning_ && opt_.props) drawTrafficGlows(ctx, eye, viewDir, fullMap, heads);
    r.drawBillboards(heads, ctx.textures->get("lt_glow"));

    // Lens flares of the light bars, after all glows (police.md, "Drawing" 4).
    std::vector<ScreenQuad> flares;
    for (const CityCar& car : cars) {
        if (!car.lightBar || !car.lightBar->on || car.bodyHidden) continue;
        for (const SirenLight& l : car.lightBar->lights) {
            const vec3 pos = car.world.transformPoint(l.pos);
            const vec3 d = eye - pos;
            const float r2 = dot(d, d);
            if (r2 <= 0.0f) continue;
            float c = dot(d, l.beam) / std::sqrt(r2);
            if (c < 0.0f) c = 0.0f;
            const float fi = 25.0f < r2 * 0.05f ? 0.0f : std::max(0.0f, (25.0f / r2) * c * c * c - 0.05f);
            float I = 0.5f * fi;
            if (I < 0.05f) continue;
            const float* m = viewProj.m;
            const float cx = m[0] * pos.x + m[4] * pos.y + m[8] * pos.z + m[12];
            const float cy = m[1] * pos.x + m[5] * pos.y + m[9] * pos.z + m[13];
            const float cz = m[2] * pos.x + m[6] * pos.y + m[10] * pos.z + m[14];
            const float cw = m[3] * pos.x + m[7] * pos.y + m[11] * pos.z + m[15];
            if (cw <= 0.0f) continue;
            const float x = cx / cw, y = cy / cw, z = (cz / cw + 1.0f) * 0.5f;
            if (z > 1.0f) continue;
            const float xp = 1.33f * x;
            const float rho2 = xp * xp + y * y;
            if (rho2 > 4.0f) continue;
            if (rho2 > 0.25f) {
                const float fade = (std::sqrt(rho2) - 2.0f) / (0.5f - 2.0f);
                I = fade * I;
                if (I < 0.05f) continue;
            }
            for (const FlareElement& e : car.lightBar->flare) {
                if (rho2 > e.m * e.m) continue;
                ScreenQuad q;
                const float sx = xp * e.p - e.z, sy = y * e.p - e.z;
                q.x0 = sx / 1.33f;
                q.x1 = (sx + 2.0f * e.z) / 1.33f;
                q.y0 = sy;
                q.y1 = sy + 2.0f * e.z;
                const float k = std::min(I * e.b, 1.0f);
                q.color[0] = uint8_t(std::clamp(255.0f * l.color.x * e.c.x * k, 0.0f, 255.0f));
                q.color[1] = uint8_t(std::clamp(255.0f * l.color.y * e.c.y * k, 0.0f, 255.0f));
                q.color[2] = uint8_t(std::clamp(255.0f * l.color.z * e.c.z * k, 0.0f, 255.0f));
                q.color[3] = 255;
                flares.push_back(q);
            }
        }
    }
    r.drawScreenQuads(flares, ctx.textures->get("lt_flare"));
}

// ---------------------------------------------------------------------------------------------
// Traffic cars (traffic-visuals.md, "Drawing", "Shadow", "Lights")

namespace {
bool trafficRoomNear(const CityData& data, int room, int camRoom, const vec3& eye, const vec3& viewDir, float limit) {
    const auto& rooms = data.rooms();
    if (room <= 0 || size_t(room) >= rooms.size()) return false;
    const CityRoom& rr = rooms[size_t(room)];
    const float d = room == camRoom ? -rr.radius : dot(rr.centre - eye, viewDir) - rr.radius;
    return d <= limit;
}
}  // namespace

void City::drawTraffic(EngineContext& ctx, const vec3& eye, const vec3& viewDir, const mat4& viewProj, bool fullMap) {
    Renderer& r = *ctx.renderer;
    const Frustum fr(viewProj);
    const Detail dt = detail();
    for (const ai::TrafficCar& c : traffic_.cars()) {
        if (!c.onStreets || c.room == 0 || c.type < 0) continue;
        const ai::TrafficType& t = traffic_.typeOf(c);
        if (!t.model) continue;
        const vec3 up = c.pos + c.Y;
        const float own = dot(up - eye, viewDir);
        if (!fullMap && (!trafficRoomNear(data_, c.room, lastCamRoom_, eye, viewDir, dt.roomLimit) || own > dt.roomLimit)) continue;
        if (!listed(c.room)) continue;
        if (!fr.sphere(c.pos, t.bodyRadius + 1.0f)) continue;
        const float d = own - t.bodyRadius;
        const Lod lod = d > dt.t3 ? LodVL : d > dt.t2 ? LodL : d > dt.t1 ? LodM : LodH;
        const VehicleModel& m = *t.model;
        const mat4 world = traffic_.matrixOf(c);
        const int set = m.setCount() > 0 ? std::clamp(c.paint, 0, m.setCount() - 1) : 0;
        auto draw = [&](const Mesh* mesh, const mat4& model, bool reflect) {
            if (!mesh) return;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*mesh);
            md.pkg = m.pkg.get();
            md.materialSet = set;
            md.model = model;
            md.inlineBlend = true;
            md.reflections = reflect && opt_.reflections;
            if (reflect) md.flags = DrawReflect;
            md.user = m.baseName.c_str();
            r.drawModel(md, *ctx.textures);
        };
        const PartLods* body = m.part("BODY");
        if (body) draw(body->at(lod), world, lod == LodH);
        if (lod != LodH) continue;
        for (int w = 0; w < 6; ++w) {
            if (w >= 4 && (!t.wheelMesh[4] || (w == 5 && !t.wheelMesh[5]))) break;
            const PartLods* p = m.part(strf("WHL%d", w));
            if (!p || !t.hasWheel[w]) continue;
            draw(p->at(LodH), world * traffic_.wheelTransform(c, w), false);
        }
    }
}

// ped-anim.md, "Drawing": a city object in its room (sphere of 2 m); closer than 35 m the posed
// model with the paint variant's materials, farther the camera-facing strips of the .rays file.
void City::drawPedestrians(EngineContext& ctx, const mat4& view, const vec3& eye, const vec3& viewDir, const mat4& viewProj,
                           bool fullMap) {
    pedsNear_ = pedsFar_ = 0;
    if (!peds_.active()) return;
    Renderer& r = *ctx.renderer;
    const Frustum fr(viewProj);
    const Detail dt = detail();
    const vec3 right(view.m[0], view.m[4], view.m[8]);  // the camera's right axis in the world
    const float factor = opt_.timeOfDay == 3 ? 0.5f : 1.0f;
    const auto& peds = peds_.peds();
    if (pedMeshes_.size() < peds.size()) pedMeshes_.resize(peds.size(), nullptr);
    std::vector<PedBoneXf> pose;
    Mesh mesh;
    std::vector<PedModel::StripVertex> strips;
    Mesh far;
    for (size_t i = 0; i < peds.size(); ++i) {
        const ai::Pedestrian& p = peds[i];
        if (!p.onStreets || p.lost || p.room <= 0 || size_t(p.room) >= data_.rooms().size()) continue;
        const PedModel* m = peds_.modelOf(p);
        if (!m) continue;
        if (!fullMap && !trafficRoomNear(data_, p.room, lastCamRoom_, eye, viewDir, dt.roomLimit)) continue;
        if (!listed(p.room)) continue;
        if (!fr.sphere(p.p, 2.0f)) continue;
        const vec3 d = p.p - eye;
        const mat4 world = peds_.matrixOf(p);
        m->pose(p.cur, p.frame, pose);
        const bool near = pedDetailDebug_ == 1 ? false : pedDetailDebug_ == 2 ? true : dot(d, d) < 1225.0f;
        if (near) {
            m->buildMesh(pose, mesh);
            MeshCache::stream(pedMeshes_[i], mesh);
            ModelDraw md;
            md.gpu = pedMeshes_[i];
            md.pkg = &m->materials();
            md.materialSet = p.variant;
            md.model = world;
            md.inlineBlend = true;
            md.user = m->name().c_str();
            r.drawModel(md, *ctx.textures);
            ++pedsNear_;
        } else if (m->hasRays()) {
            // Far: the strips, in world space, into one mesh drawn after the loop.
            const vec3 R(dot(right, p.X), dot(right, p.Y), dot(right, p.Z));
            strips.clear();
            m->farStrips(pose, R, p.variant, factor, strips);
            for (size_t k = 0; k + 2 < strips.size(); k += 3) {
                if (far.batches.empty() || far.batches.back().vertices.size() + 3 > 65535) {
                    far.batches.emplace_back();
                    far.batches.back().material = 0;
                }
                Batch& b = far.batches.back();
                const uint16_t base = uint16_t(b.vertices.size());
                for (size_t q = 0; q < 3; ++q) {
                    const PedModel::StripVertex& sv = strips[k + q];
                    Vertex v;
                    v.pos = world.transformPoint(sv.p);
                    v.color[0] = uint8_t((sv.argb >> 16) & 255u);
                    v.color[1] = uint8_t((sv.argb >> 8) & 255u);
                    v.color[2] = uint8_t(sv.argb & 255u);
                    v.color[3] = uint8_t(sv.argb >> 24);
                    b.vertices.push_back(v);
                }
                // Two-sided: each triangle in both windings.
                for (uint16_t q : {0, 1, 2, 0, 2, 1}) b.indices.push_back(uint16_t(base + q));
            }
            ++pedsFar_;
        }
    }
    if (far.batches.empty()) return;
    // Flat, unlit (vertex colours), untextured, fogged, two-sided (ped-anim.md, "Drawing").
    if (pedStripPkg_.materials.empty()) {
        Material white;
        white.diffuse = white.ambient = vec4(1, 1, 1, 1);
        white.emissive = vec4(0, 0, 0, 1);
        pedStripPkg_.materials.push_back(white);
        pedStripPkg_.setCount = 1;
        pedStripPkg_.materialsPerSet = 1;
    }
    far.fvf = kFvfXYZ | kFvfDiffuse;
    MeshCache::stream(pedFarMesh_, far);
    ModelDraw md;
    md.gpu = pedFarMesh_;
    md.pkg = &pedStripPkg_;
    md.model = mat4::identity();
    md.flags = DrawUnlit;
    md.inlineBlend = true;
    md.user = "pedestrian strips";
    r.drawModel(md, *ctx.textures);
}

void City::drawTrafficShadows(EngineContext& ctx, const vec3& eye, const vec3& viewDir, bool fullMap) {
    Renderer& r = *ctx.renderer;
    const Detail dt = detail();
    for (const ai::TrafficCar& c : traffic_.cars()) {
        if (!c.onStreets || c.room == 0 || c.type < 0 || !c.groundSolved) continue;
        const ai::TrafficType& t = traffic_.typeOf(c);
        if (!t.model) continue;
        if (!fullMap && !trafficRoomNear(data_, c.room, lastCamRoom_, eye, viewDir, dt.roomLimit)) continue;
        if (!listed(c.room)) continue;
        const VehicleModel& m = *t.model;
        const PartLods* sh = m.part("SHADOW");
        if (!sh || !sh->at(LodH)) continue;
        ModelDraw md;
        md.gpu = ctx.meshes->get(*sh->at(LodH));
        md.pkg = m.pkg.get();
        md.materialSet = m.setCount() > 0 ? std::clamp(c.paint, 0, m.setCount() - 1) : 0;
        md.user = m.baseName.c_str();
        const mat4 world = traffic_.matrixOf(c);
        const bool projected = c.state == ai::TrafficKnocked || c.Y.y < 0.0f;
        float gy = c.pos.y;
        if (projected && !groundBelow(c.pos.x, c.pos.z, c.pos.y + 1.0f, gy)) continue;
        md.flags = DrawForceBlend | (projected ? DrawShadow : 0u);
        md.model = projected ? mat4::translate(vec3(0, gy + 0.02f, 0)) * mat4::scale(vec3(1, 0, 1)) * world : world;
        r.drawModel(md, *ctx.textures);
    }
}

void City::drawTrafficGlows(EngineContext& ctx, const vec3& eye, const vec3& viewDir, bool fullMap,
                            std::vector<Billboard>& heads) {
    Renderer& r = *ctx.renderer;
    const Detail dt = detail();
    const bool headlights = opt_.timeOfDay >= 2 || opt_.weather == 2;
    for (const ai::TrafficCar& c : traffic_.cars()) {
        if (!c.onStreets || c.room == 0 || c.type < 0) continue;
        const ai::TrafficType& t = traffic_.typeOf(c);
        if (!t.model) continue;
        if (!fullMap && !trafficRoomNear(data_, c.room, lastCamRoom_, eye, viewDir, dt.roomLimit)) continue;
        if (!listed(c.room)) continue;
        const VehicleModel& m = *t.model;
        const mat4 world = traffic_.matrixOf(c);
        const int set = m.setCount() > 0 ? std::clamp(c.paint, 0, m.setCount() - 1) : 0;
        auto glow = [&](const char* name) {
            const PartLods* p = m.part(name);
            if (!p || !p->at(LodH)) return;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*p->at(LodH));
            md.pkg = m.pkg.get();
            md.materialSet = set;
            md.model = world;
            md.user = m.baseName.c_str();
            r.drawGlowModel(md, *ctx.textures);
        };
        if (c.a < 0.0f || c.v == 0.0f) glow("TLIGHT");
        if (headlights) glow("TLIGHT");
        const bool blink = traffic_.blinkOn(c);
        if ((c.indicator & 1) && blink) glow("SLIGHT0");
        if ((c.indicator & 2) && blink) glow("SLIGHT1");
        if (!headlights) continue;
        const Pivot* pv = m.pivotOf("HEADLIGHT0");
        const PartLods* h0 = m.part("HEADLIGHT0");
        if (!pv || !pv->found || !h0 || !h0->any()) continue;
        const PartLods* h1 = m.part("HEADLIGHT1");
        const vec3 forward(-c.Z.x, -c.Z.y, -c.Z.z);
        const vec3 toCam = normalize(eye - c.pos) * 0.2f;
        for (int k = 0; k < 2; ++k) {
            if (k == 1 && !(h1 && h1->any())) break;
            const vec3 local(k == 0 ? pv->origin.x : -pv->origin.x, pv->origin.y, pv->origin.z);
            Billboard b;
            b.centre = world.transformPoint(local) + toCam;
            const vec3 d = eye - b.centre;
            const float len = length(d);
            if (len <= 0.0f) continue;
            const float cs = dot(d * (1.0f / len), normalize(forward));
            if (cs < 0.0f) continue;
            b.half = cs * std::sqrt(cs);
            const uint8_t w = uint8_t(0.95f * 255.0f + 0.5f);
            b.color[0] = b.color[1] = b.color[2] = w;
            b.color[3] = 255;
            heads.push_back(b);
        }
    }
}

TextureCondition City::textureCondition() const {
    TextureCondition c;
    c.night = opt_.timeOfDay == 3;
    c.rain = opt_.weather == 3;
    return c;
}

void City::fetchSlotTextures(EngineContext& ctx) {
    const Psdl& psdl = data_.psdl();
    slotTextures_.assign(psdl.textures.size(), nullptr);
    const std::string user = ctx.vfs->storedName("city/" + data_.name() + ".psdl");
    for (size_t i = 1; i < psdl.textures.size(); ++i)
        if (!psdl.textures[i].empty()) slotTextures_[i] = ctx.textures->get(Psdl::lookupName(psdl.textures[i]), user);
    slotGeneration_ = ctx.textures->generation();
}

float City::fullMapFar(const vec3& eye) const {
    float far2 = 0;
    for (int c = 0; c < 8; ++c) {
        const vec3 corner((c & 1) ? extentMax_.x : extentMin_.x, (c & 2) ? extentMax_.y : extentMin_.y,
                          (c & 4) ? extentMax_.z : extentMin_.z);
        const vec3 d = corner - eye;
        far2 = std::max(far2, dot(d, d));
    }
    return std::sqrt(far2) * 1.02f + 100.0f;
}

// The frame's room list (cpvs.md): the camera point and room, then the .cpvs row of the camera
// room or, without a usable file, the breadth-first room walk; each room in view, at most 512.
void City::makeRoomList(const mat4& view, const mat4& proj, const vec3& eye) {
    roomList_.clear();
    const auto& rooms = data_.rooms();
    roomListed_.assign(rooms.size(), 0);
    // Camera point P = eye + near·(X.z, Y.z, −Z.z) (MM2 takes a column where a row was meant).
    const float near = proj.m[14] / (proj.m[10] - 1.0f);
    const vec3 P(eye.x + near * view.m[8], eye.y + near * view.m[9], eye.z + near * -view.m[10]);
    int cam = data_.findRoom(P, pvsCamRoom_);
    if (cam == 0) cam = pvsCamRoom_;  // outside every room: the previous camera room
    pvsCamRoom_ = cam;
    if (cam <= 0 || size_t(cam) >= rooms.size()) return;  // nothing but the sky
    const Frustum fr(proj * view);
    auto inView = [&](int r) { return fr.sphere(rooms[size_t(r)].centre, rooms[size_t(r)].radius); };
    auto take = [&](int r) {
        if (roomList_.size() >= 512) return;
        roomList_.push_back(r);
        roomListed_[size_t(r)] = 1;
    };
    if (data_.hasPvs()) {
        if (pvsRowRoom_ != cam) {
            data_.pvsRow(cam, pvsRow_);
            pvsRowRoom_ = cam;
        }
        const size_t n = std::min(pvsRow_.size() * 4, rooms.size());
        for (size_t j = 0; j < n; ++j) {
            const int v = (pvsRow_[j / 4] >> (2 * (j % 4))) & 3;
            if (v == 0) continue;  // the camera room too, when its own bits are zero
            if (int(j) == cam || inView(int(j))) take(int(j));
        }
        return;
    }
    // The walk: perimeter neighbours, then the overlap list (newest first) except enclosed rooms.
    const auto& psdlRooms = data_.psdl().rooms;
    std::vector<uint8_t> marked(rooms.size(), 0);
    std::vector<int> queue{cam};
    marked[size_t(cam)] = 1;
    for (size_t head = 0; head < queue.size(); ++head) {
        const int r = queue[head];
        if (r == cam || inView(r)) take(r);
        if (size_t(r) < psdlRooms.size())
            for (uint16_t nb : psdlRooms[size_t(r)].neighbours)
                if (nb != 0 && nb < rooms.size() && !marked[nb]) {
                    marked[nb] = 1;
                    queue.push_back(nb);
                }
        const auto& ov = rooms[size_t(r)].overlaps;
        for (auto it = ov.rbegin(); it != ov.rend(); ++it) {
            const int o = *it;
            if (o <= 0 || size_t(o) >= rooms.size() || marked[size_t(o)]) continue;
            if (size_t(o) < psdlRooms.size() && (psdlRooms[size_t(o)].flags & 0x02)) continue;
            marked[size_t(o)] = 1;
            queue.push_back(o);
        }
    }
}

// Decals (pathset.md, "Decals"): rooms whose distance is below the room limit; white, unlit, fogged,
// depth test without writes, alpha blended, depth range 0.001 … 0.999, back faces culled.
void City::drawDecals(EngineContext& ctx, const vec3& eye, const vec3& viewDir, int camRoom, bool fullMap) {
    const auto& decals = data_.decals();
    if (decals.empty()) return;
    if (decalTris_.size() != decals.size()) {
        decalTris_.assign(decals.size(), {});
        for (size_t i = 0; i < decals.size(); ++i) {
            const auto& st = decals[i].strip;
            for (size_t k = 0; k + 2 < st.size(); ++k) {
                // psdl.md strip order: (0 1 2), (2 1 3), (2 3 4), …
                const size_t a = (k & 1) ? k + 1 : k, b = (k & 1) ? k : k + 1, c = k + 2;
                for (size_t v : {a, b, c}) {
                    DecalVertex dv;
                    dv.pos = st[v].pos;
                    dv.u = st[v].u;
                    dv.v = st[v].v;
                    decalTris_[i].push_back(dv);
                }
            }
        }
    }
    const Detail dt = detail();
    const auto& rooms = data_.rooms();
    std::vector<DecalDraw> list;
    for (size_t i = 0; i < decals.size(); ++i) {
        const int room = decals[i].room;
        if (room <= 0 || size_t(room) >= rooms.size()) continue;
        if (!listed(room)) continue;
        if (!fullMap) {
            const CityRoom& rr = rooms[size_t(room)];
            const float d = room == camRoom ? -rr.radius : dot(rr.centre - eye, viewDir) - rr.radius;
            if (!(d < dt.roomLimit)) continue;
        }
        DecalDraw dd;
        dd.tex = ctx.textures->get(decals[i].texture, "decal");
        dd.verts = &decalTris_[i];
        list.push_back(dd);
    }
    ctx.renderer->drawDecals(list);
}

void City::render(EngineContext& ctx, const mat4& view, const mat4& proj, const vec3& eye, float farPlane,
                  const std::vector<CityCar>& cars, bool fullMap) {
    Renderer& r = *ctx.renderer;
    // Textures follow the session's time of day and weather (night.md, section 1).
    ctx.textures->setCondition(textureCondition());
    if (slotGeneration_ != ctx.textures->generation()) fetchSlotTextures(ctx);
    const int idx = conditionIndex(opt_.timeOfDay, opt_.weather);
    const mat4 viewProj = proj * view;
    const Frustum fr(viewProj);
    const vec3 viewDir(-view.m[2], -view.m[6], -view.m[10]);
    auto depthOf = [&](const vec3& p) { return dot(p - eye, viewDir); };

    // Fog clamped to the far plane (city-files.md).
    Fog fog;
    if (opt_.fog && !fullMap && size_t(idx) < data_.fog().size()) {
        const FogRow& row = data_.fog()[size_t(idx)];
        fog.on = true;
        fog.color = row.color;
        fog.start = std::min(row.start, farPlane - 30.0f);
        fog.end = std::min(row.end, farPlane);
    }
    Lighting light;
    if (const CityLights* l = data_.lights(idx)) {
        light.ambient = argbToVec(l->ambient);
        light.count = 3;
        for (int i = 0; i < 3; ++i) light.lights[i] = {l->dir[i], l->color[i]};
    }
    r.setLighting(light);
    r.setUntexturedFactor(opt_.timeOfDay == 3 ? 0.5f : 1.0f);
    r.setTime(ctx.time);
    r.setEnvironment(opt_.reflections ? ctx.textures->get("refl_dc") : nullptr);

    // Sky: at the camera, follows it up and down by `follow`, turned by its spin angle; no fog.
    const SkyInfo* sky = data_.sky();
    if (opt_.sky && sky && skyMesh_ && skyPkg_) {
        ModelDraw sd;
        sd.gpu = ctx.meshes->get(*skyMesh_);
        sd.pkg = skyPkg_.get();
        sd.materialSet = skyPkg_->setCount > 0 ? idx % skyPkg_->setCount : 0;
        sd.model = mat4::translate(vec3(eye.x, eye.y * sky->follow + sky->heightOffset, eye.z)) * mat4::rotateY(skyAngle_);
        if (fullMap) {
            // Scaled so the dome stays inside the far plane (it is drawn without depth anyway).
            const float dy = std::fabs(eye.y * sky->follow + sky->heightOffset - eye.y);
            const float reach = skyMesh_->radius + dy;
            if (reach > farPlane * 0.95f && reach > 0.0f) sd.model = sd.model * mat4::scale(vec3(1, 1, 1) * (farPlane * 0.95f / reach));
        }
        sd.user = sky->model.c_str();
        r.setFog(Fog());
        r.drawSky(sd, *ctx.textures);
    }
    r.setFog(fog);

    // Ground overlay (night.md, section 3): cloud shadows by day, light pools in the evening and
    // at night; loaded as stored.
    r.setOverlayMap(ctx.textures->getRaw(opt_.timeOfDay >= 2 ? "shadmap_nite" : "shadmap_day"));

    // The "MM2" draw distance draws the frame's room list (cpvs.md); the others every room in range.
    useRoomList_ = ctx.drawDistance == 0 && !fullMap;
    if (useRoomList_) makeRoomList(view, proj, eye);
    // Rooms: opaque textures first, then the alpha ones blended (psdl.md, render state).
    std::vector<const Chunk*> visible;
    if (useRoomList_) {
        for (int rm : roomList_)
            if (size_t(rm) < roomChunks_.size() && roomChunks_[size_t(rm)].gpu) visible.push_back(&roomChunks_[size_t(rm)]);
    } else {
        for (const Chunk& c : chunks_) {
            float d = depthOf(c.centre) - c.radius;
            if (d > farPlane || !fr.sphere(c.centre, c.radius)) continue;
            visible.push_back(&c);
        }
    }
    for (int pass = 0; pass < 2; ++pass)
        for (const Chunk* c : visible) {
            SlotDraw sd;
            sd.gpu = c->gpu;
            sd.slots = &slotTextures_;
            sd.alphaRef = 100;
            r.drawSlots(sd, pass);
        }

    // Placed objects (inst.md, "Drawing").
    const Detail dt = detail();
    const auto& rooms = data_.rooms();
    const int camRoom = useRoomList_ ? pvsCamRoom_ : data_.findRoom(eye, lastCamRoom_);
    lastCamRoom_ = camRoom;
    auto roomDistance = [&](int room) {
        const CityRoom& rr = rooms[size_t(room)];
        return room == camRoom ? -rr.radius : depthOf(rr.centre) - rr.radius;
    };
    if (!opt_.props && effects_) drawSkidMarks(r, *ctx.textures, *effects_);
    if (opt_.props) {
        const auto& objects = data_.objects();
        // Static objects first, then the decals, then the props, then the trees
        // (pathset.md, "Decals"; street-props.md, "Drawing").
        std::vector<size_t> trees;
        for (int objPass = 0; objPass < 2; ++objPass) {
        if (objPass == 1) {
            // Decal pass: skid marks (effects.md), then the path-set decals.
            if (effects_) drawSkidMarks(r, *ctx.textures, *effects_);
            drawDecals(ctx, eye, viewDir, camRoom, fullMap);
        }
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            const CityObject& o = objects[oi];
            if ((o.kind == CityObjectKind::Breakable) != (objPass == 1)) continue;
            if (collision_.cityObjectBroken(int(oi))) continue;  // knocked out of the way
            if (useRoomList_) {
                // Only objects filed in the frame's rooms (cpvs.md).
                bool in = listed(o.room);
                if (!in && oi < spanRooms_.size())
                    for (int sr : spanRooms_[oi])
                        if (listed(sr)) in = true;
                if (!in) continue;
            }
            // Props are drawn at their CG and measured from there (inst.md, "Where breakable props
            // are drawn"); for other objects drawWorld is the placement.
            const vec3 at = o.drawWorld.translation();
            const float own = depthOf(at);
            if (o.kind == CityObjectKind::Breakable) {
                // Breakables only: room limit on the room and on their own depth.
                if (o.room <= 0 || size_t(o.room) >= rooms.size()) continue;
                if (!fullMap && (roomDistance(o.room) > dt.roomLimit || own > dt.roomLimit)) continue;
            }
            const float d = own - o.radius;
            if (!fullMap && (d > 9999.0f || d > farPlane)) continue;
            if (!fr.sphere(at, o.radius + 1.0f)) continue;
            // One-sided test (inst.md, QUESTIONS 32): plain static objects only (landmarks,
            // spanning objects and props are exempt); hidden when
            // (C.x − P.x)·Z.x + (C.z − P.z)·Z.z < 0 with P the placement translation (pivot).
            if (o.kind == CityObjectKind::Static) {
                const vec3 toCam = eye - o.pos;
                if (toCam.x * o.zAxis.x + toCam.z * o.zAxis.z < 0) continue;
            }
            Lod l = d > dt.t3 ? LodVL : d > dt.t2 ? LodL : d > dt.t1 ? LodM : LodH;
            // Full map only (viewer.md): a landmark that carries its block's ground (block room with
            // an empty PSDL element list) is never drawn below L; some VL meshes leave the ground out.
            if (fullMap && l == LodVL && o.kind == CityObjectKind::Landmark && o.room > 0 &&
                size_t(o.room) < data_.psdl().rooms.size()) {
                const PsdlRoom& pr = data_.psdl().rooms[size_t(o.room)];
                if ((pr.flags & 0x04) && pr.words.empty()) l = LodL;
            }
            const Mesh* m = o.lod[l];
            // Full map only (viewer.md): no mesh at the chosen level falls back to the nearest more
            // detailed one instead of disappearing (MM2 does not draw it: inst.md "Missing levels").
            if (fullMap)
                for (int k = int(l) - 1; !m && k >= 0; --k) m = o.lod[k];
            if (!m) continue;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*m);
            md.pkg = o.pkg.get();
            md.materialSet = o.set;
            md.model = o.drawWorld;
            md.alphaRef = 100;
            md.inlineBlend = true;
            md.canBlend = canBlend(o.pkg.get(), ctx);
            md.groundOverlay = o.kind != CityObjectKind::Breakable;  // buildings, not props
            md.user = "city object";
            if (o.kind == CityObjectKind::Breakable) {
                if (o.billFlags & 0x200) {  // a tree: drawn after the props
                    trees.push_back(oi);
                    continue;
                }
                if (o.billFlags & 0x80) {
                    md.flags |= DrawUnlit;
                    md.alphaRef = 140;
                }
            }
            r.drawModel(md, *ctx.textures);
        }
        }
        // Moved props (loose-bodies.md 9): broken props' copies and pieces and thrown car parts,
        // each with its own matrix, model and paint, while it is in a room.
        for (int si = 0; si < phys::CollisionWorld::kSlotCount; ++si) {
            const phys::MovedSlot& S = collision_.movedSlot(si);
            if (!S.held || S.room <= 0 || !S.data || size_t(S.room) >= rooms.size()) continue;
            if (useRoomList_ && !listed(S.room)) continue;
            const vec3 at = S.M.o;
            const float own = depthOf(at);
            if (!fullMap && (roomDistance(S.room) > dt.roomLimit || own > dt.roomLimit)) continue;
            const float d = own - S.data->radius;
            if (!fullMap && (d > 9999.0f || d > farPlane)) continue;
            if (!fr.sphere(at, S.data->radius + 1.0f)) continue;
            const Lod l = d > dt.t3 ? LodVL : d > dt.t2 ? LodL : d > dt.t1 ? LodM : LodH;
            const Mesh* m = S.data->lod[l];
            if (fullMap)
                for (int k = int(l) - 1; !m && k >= 0; --k) m = S.data->lod[k];
            if (!m || !S.data->pkg) continue;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*m);
            md.pkg = S.data->pkg.get();
            md.materialSet = S.set;
            mat4 w = mat4::identity();
            const vec3* rows[3] = {&S.M.X, &S.M.Y, &S.M.Z};
            for (int i = 0; i < 3; ++i) {
                w.m[i * 4 + 0] = rows[i]->x;
                w.m[i * 4 + 1] = rows[i]->y;
                w.m[i * 4 + 2] = rows[i]->z;
            }
            w.m[12] = at.x;
            w.m[13] = at.y;
            w.m[14] = at.z;
            md.model = w;
            md.alphaRef = 100;
            md.inlineBlend = true;
            md.canBlend = canBlend(S.data->pkg.get(), ctx);
            md.user = "moved prop";
            if (S.data->bd.billFlags & 0x80) {
                md.flags |= DrawUnlit;
                md.alphaRef = 140;
            }
            r.drawModel(md, *ctx.textures);
        }
        // Moving level objects (level-objects.md): boats and train cars in the room pass by the
        // rule of breakable props; leaves and ferries by their straight-line distance from the eye
        // (D = 200 m: H, M, L, VL, nothing beyond 800 m), material set 0.
        for (const LevelObject& lo : data_.levelObjects().objects()) {
            if (!lo.pkg) continue;
            const vec3 at = lo.o;
            Lod l;
            if (lo.drawnByDistance()) {
                const vec3 dd = eye - lo.ref;
                const float dist = std::sqrt(dd.x * dd.x + dd.y * dd.y + dd.z * dd.z);
                if (!fullMap && dist > 800.0f) continue;
                l = dist < 200.0f ? LodH : dist < 400.0f ? LodM : dist < 600.0f ? LodL : LodVL;
            } else {
                if (lo.room <= 0 || size_t(lo.room) >= rooms.size()) continue;
                if (useRoomList_ && !listed(lo.room)) continue;
                const float own = depthOf(at);
                if (!fullMap && (roomDistance(lo.room) > dt.roomLimit || own > dt.roomLimit)) continue;
                const float d = own - lo.radius;
                if (!fullMap && (d > 9999.0f || d > farPlane)) continue;
                l = d > dt.t3 ? LodVL : d > dt.t2 ? LodL : d > dt.t1 ? LodM : LodH;
            }
            if (!fr.sphere(at, lo.radius + 1.0f)) continue;
            const Mesh* m = lo.lod[l];
            if (fullMap)
                for (int k = int(l) - 1; !m && k >= 0; --k) m = lo.lod[k];
            if (!m) continue;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*m);
            md.pkg = lo.pkg.get();
            md.materialSet = lo.set;
            mat4 w = mat4::identity();
            const vec3* rows[3] = {&lo.X, &lo.Y, &lo.Z};
            for (int i = 0; i < 3; ++i) {
                w.m[i * 4 + 0] = rows[i]->x;
                w.m[i * 4 + 1] = rows[i]->y;
                w.m[i * 4 + 2] = rows[i]->z;
            }
            w.m[12] = at.x;
            w.m[13] = at.y;
            w.m[14] = at.z;
            md.model = w;
            md.alphaRef = 100;
            md.inlineBlend = true;
            md.canBlend = canBlend(lo.pkg.get(), ctx);
            md.user = "level object";
            if (lo.bd.billFlags & 0x80) {
                md.flags |= DrawUnlit;
                md.alphaRef = 140;
            }
            r.drawModel(md, *ctx.textures);
        }
        // Cable cars (cable-cars.md, "Drawing"): their BODY at the level of the breakable-prop rule,
        // material set 0, with the pose; a knocked one is drawn as its moved copy.
        if (trafficRunning_ && traffic_.cableData())
            for (const ai::CableCar& cc : traffic_.cableCars()) {
                const phys::LooseData& cd = *traffic_.cableData();
                if (cc.prop >= 0 && collision_.object(cc.prop).broken) continue;
                const ai::TrafficCar& c = traffic_.cars()[size_t(cc.car)];
                if (c.room <= 0 || size_t(c.room) >= rooms.size() || !cd.pkg) continue;
                if (useRoomList_ && !listed(c.room)) continue;
                const float own = depthOf(c.pos);
                if (!fullMap && (roomDistance(c.room) > dt.roomLimit || own > dt.roomLimit)) continue;
                const float d = own - cd.radius;
                if (!fullMap && (d > 9999.0f || d > farPlane)) continue;
                if (!fr.sphere(c.pos, cd.radius + 1.0f)) continue;
                const Lod l = d > dt.t3 ? LodVL : d > dt.t2 ? LodL : d > dt.t1 ? LodM : LodH;
                const Mesh* m = cd.lod[l];
                if (!m) continue;
                ModelDraw md;
                md.gpu = ctx.meshes->get(*m);
                md.pkg = cd.pkg.get();
                md.materialSet = 0;
                mat4 w = mat4::identity();
                const vec3* rows[3] = {&c.X, &c.Y, &c.Z};
                for (int i = 0; i < 3; ++i) {
                    w.m[i * 4 + 0] = rows[i]->x;
                    w.m[i * 4 + 1] = rows[i]->y;
                    w.m[i * 4 + 2] = rows[i]->z;
                }
                w.m[12] = c.pos.x;
                w.m[13] = c.pos.y;
                w.m[14] = c.pos.z;
                md.model = w;
                md.alphaRef = 100;
                md.inlineBlend = true;
                md.canBlend = canBlend(cd.pkg.get(), ctx);
                md.user = "cable car";
                r.drawModel(md, *ctx.textures);
            }
        // Trees: always the H mesh, unlit, alpha test "greater than 120".
        for (size_t oi : trees) {
            const CityObject& o = objects[oi];
            const Mesh* m = o.lod[LodH];
            if (!m) continue;
            ModelDraw md;
            md.gpu = ctx.meshes->get(*m);
            md.pkg = o.pkg.get();
            md.materialSet = o.set;
            md.model = o.drawWorld;
            md.flags = DrawUnlit;
            md.alphaRef = 120;
            md.inlineBlend = true;
            md.canBlend = canBlend(o.pkg.get(), ctx);
            md.user = "city object";
            r.drawModel(md, *ctx.textures);
        }
    }

    // Cars, then their shadows in a pass of their own (car-model.md, "Shadow"). With the room list
    // only cars in listed rooms are drawn.
    std::vector<CityCar> shown;
    for (const CityCar& car : cars) {
        const int room = car.room ? car.room : data_.findRoom(car.world.translation());
        if (listed(room)) shown.push_back(car);
    }
    const std::vector<CityCar>& carsIn = useRoomList_ ? shown : cars;
    for (const CityCar& car : carsIn) drawCar(ctx, car, eye, viewDir);
    if (trafficRunning_ && opt_.props) drawTraffic(ctx, eye, viewDir, viewProj, fullMap);
    drawPedestrians(ctx, view, eye, viewDir, viewProj, fullMap);
    for (const CityCar& car : carsIn) {
        if (!car.model || car.shadowHidden) continue;
        const vec3 p = car.world.translation();
        int room = car.room ? car.room : data_.findRoom(p);
        if (room <= 0 || (!fullMap && roomDistance(room) > dt.roomLimit)) continue;
        float gy;
        if (!groundBelow(p.x, p.z, p.y + 1.0f, gy)) continue;
        std::vector<DrawPart> sh;
        addStillShadow(*car.model, paintSetFor(car.paint, car.model->setCount()), vec3(), 0.0f, sh);
        for (const DrawPart& dp : sh) {
            ModelDraw md;
            md.gpu = ctx.meshes->get(*dp.mesh);
            md.pkg = dp.pkg;
            md.materialSet = dp.materialSet;
            md.flags = dp.flags;
            md.user = car.model->baseName.c_str();
            md.model = mat4::translate(vec3(0, gy + 0.02f, 0)) * mat4::scale(vec3(1, 0, 1)) * car.world;
            r.drawModel(md, *ctx.textures);
        }
    }
    if (trafficRunning_ && opt_.props) drawTrafficShadows(ctx, eye, viewDir, fullMap);
    r.flush();
    drawGlows(ctx, carsIn, eye, viewDir, viewProj, fullMap);
    drawTrafficLights(ctx, eye, viewDir, viewProj, fullMap);
    if (effects_) {
        // Rain (night.md, section 6): decided by the main view once per frame, then the effects pass.
        if (effectsMain_) placeRain(view, eye);
        const vec3 camX(view.m[0], view.m[4], view.m[8]), camY(view.m[1], view.m[5], view.m[9]);
        drawEffectsPass(r, *ctx.textures, *effects_, camX, camY);
    }
    if (opt_.aiOverlay) drawAiOverlay(ctx);
    r.setUntexturedFactor(1.0f);
}

}  // namespace cc
