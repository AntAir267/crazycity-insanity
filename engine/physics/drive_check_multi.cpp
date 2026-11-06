// Check runs with extra vehicles (spec/engine/drive-check.md, "Extra vehicles";
// spec/engine/drive-trace.md, "Runs with extra vehicles"): police and opponent cars as full cars
// with box bounds, traffic cars on straight lanes that become physical when hit, all in the
// physics scene of car-collision.md.
#include <cmath>
#include <map>
#include <memory>

#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/dat_text.h"
#include "formats/mtx.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "model/vehicle.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/pmath.h"
#include "physics/scene.h"
#include "physics/trailer.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

namespace {

void tf(std::FILE* f, float v) {
    if (std::isnan(v)) std::fputs(" nan", f);
    else if (std::isinf(v)) std::fputs(v > 0 ? " inf" : " -inf", f);
    else std::fprintf(f, " %a", double(v));
}
void tv(std::FILE* f, const vec3& v) {
    tf(f, v.x);
    tf(f, v.y);
    tf(f, v.z);
}

bool loadTrafficType(const Vfs& vfs, AssetCache& assets, const std::string& name, TrafficBodyType& t, std::string& err) {
    BytesPtr b = vfs.open("tune/vehicle/" + name + ".aiVehicleData");
    if (!b) {
        err = "tune/vehicle/" + name + ".aiVehicleData not found";
        return false;
    }
    using dat::FieldType;
    dat::Schema s{{{"Mass", FieldType::Float, &t.mass, nullptr},
                   {"Size", FieldType::Vector, &t.size, nullptr},
                   {"CG", FieldType::Vector, &t.cg, nullptr},
                   {"Spring", FieldType::Float, &t.spring, nullptr},
                   {"Damping", FieldType::Float, &t.damping, nullptr},
                   {"Limit", FieldType::Float, &t.limit, nullptr},
                   {"RubberSpring", FieldType::Float, &t.rubberSpring, nullptr},
                   {"RubberDamp", FieldType::Float, &t.rubberDamp, nullptr}}};
    dat::parseText(std::string(b->begin(), b->end()), s);
    for (int w = 0; w < 4; ++w) {
        BytesPtr m = vfs.open("geometry/" + name + strf("_WHL%d.mtx", w));
        Pivot p;
        std::string e;
        if (m && parseMtx(m->data(), m->size(), p, e)) {
            t.wheel[w] = p.origin;
            if (w == 0) t.wheelRadius = std::fabs((p.boxMax.y - p.boxMin.y) * 0.5f);
        }
    }
    if (auto pkg = assets.package(name, false)) {
        const PartTable pt = takeParts(*pkg, trafficCarParts());
        if (const PartLods* body = pt.find("BODY"))
            for (int l = 0; l < LodCount; ++l)
                if (body->at(Lod(l))) t.bodyRadius = std::max(t.bodyRadius, body->at(Lod(l))->radius);
    }
    setupTrafficBodyType(t);
    return true;
}

struct Extra {
    RunVehicle spec;
    int number = 0;
    // computer cars
    std::unique_ptr<Car> car;
    bool box = false;
    BoxShape shape;
    size_t seg = 0;
    long segFrame = 0;
    bool touched = false;
    // traffic
    TrafficBodyType ttype;
    Mtx lane;
    float laneSpeed = 0, d = 0;
    vec3 lanePos;
    int state = 0;  // 0 lane never hit, 1 physical, 2 released upright, 3 released wrecked
    std::unique_ptr<TrafficPhys> phys;
    bool hasBody = false;
    int room = 0;
};

}  // namespace

// In drive_check.cpp.
bool runMultiCheck(const Vfs& vfs, const RunFile& run, CollisionWorld& world, const CityData* city, bool flat, Car& car,
                   std::FILE* out, std::string& err, bool trace, AssetCache& assets);

bool runMultiCheck(const Vfs& vfs, const RunFile& run, CollisionWorld& world, const CityData* city, bool flat, Car& car,
                   std::FILE* out, std::string& err, bool trace, AssetCache& assets) {
    // One bound per model name, vehicle 1 first (car-collision.md 3.1).
    std::map<std::string, std::shared_ptr<Bound>> bounds;
    bounds[run.car] = car.tune().bound;
    std::vector<Extra> extras(run.extras.size());
    for (size_t i = 0; i < run.extras.size(); ++i) {
        Extra& e = extras[i];
        e.spec = run.extras[i];
        e.number = int(i) + 2;
        if (e.spec.kind != RunVehicle::Traffic) {
            CarTune tune = loadCarTune(vfs, e.spec.model, false);
            if (!tune.simFile) {
                err = "tune/vehicle/" + e.spec.model + ".vehCarSim not found";
                return false;
            }
            auto it = bounds.find(e.spec.model);
            if (it != bounds.end()) {
                tune.bound = it->second;
            } else if (tune.bound) {
                const vec3 size(tune.bound->bmax.x - tune.bound->bmin.x, tune.bound->bmax.y - tune.bound->bmin.y,
                                tune.bound->bmax.z - tune.bound->bmin.z);
                const vec3 off(size.x * 0.5f + tune.bound->bmin.x, size.y * 0.5f + tune.bound->bmin.y,
                               size.z * 0.5f + tune.bound->bmin.z);
                tune.bound = std::make_shared<Bound>(makeBoxBound(size, off));
                bounds[e.spec.model] = tune.bound;
            }
            e.box = tune.bound && tune.bound->isBox;
            if (e.box) {
                const Bound& b = *tune.bound;
                e.shape.size = vec3(b.bmax.x - b.bmin.x, b.bmax.y - b.bmin.y, b.bmax.z - b.bmin.z);
                e.shape.offset = b.centre;
            }
            e.car = std::make_unique<Car>();
            e.car->setup(tune);
            e.car->setEffects(checkEffectsFor(*e.car, e.spec.model, 0));  // tests only (normally none)
            e.car->setAiStuck(e.spec.kind == RunVehicle::Police ? 0.75f : 0.5f);
            e.car->setEngineScale(1.0f);
            if (flat) e.car->resetFlat(e.spec.pos.x, e.spec.pos.y, e.spec.pos.z, e.spec.heading);
            else e.car->placeAtDegrees(e.spec.pos, e.spec.heading);
            // A computer car of a towing model: its trailer is placed but never a mover; the car's
            // collisions use the jointed inverse mass with it (trailer.md 10).
            TrailerTune tt;
            std::string why;
            if (loadTrailerTune(vfs, e.car->tune(), tt, why)) {
                auto t = std::make_unique<Trailer>();
                t->setup(tt, *e.car, &world, true);
                e.car->attachTrailer(std::move(t));
            }
        } else {
            if (!loadTrafficType(vfs, assets, e.spec.model, e.ttype, err)) return false;
            const double h = double(e.spec.heading) * 3.14159265358979323846 / 180.0;
            const M3 R = headingRotation(float(std::cos(h)), float(std::sin(h)));
            e.lane = mtxFromRows(R.r[0], R.r[1], R.r[2], e.spec.pos);
            e.lanePos = e.spec.pos;
            e.laneSpeed = e.spec.speed;
            e.phys = std::make_unique<TrafficPhys>();
            e.phys->identity = 20000 + e.number;
            e.room = city ? city->findRoom(vadd(e.lane.o, e.lane.Y), 0) : 0;
        }
    }
    const int playerIdentity = 10001;
    std::vector<int> pool;  // extras indices of physical traffic cars, in pool-list order

    const float F = kCheckFrame;
    const int n = physicsStepsForFrame(F);
    const float dt = F / float(n);
    const float invdt = 1.0f / dt;
    Scene scene;
    SceneHooks hooks;
    std::vector<std::tuple<int, int, Impact>> carImpacts;
    hooks.laneCars = [&](std::vector<LaneCar>& v) {
        for (size_t i = 0; i < extras.size(); ++i) {
            Extra& e = extras[i];
            if (e.spec.kind != RunVehicle::Traffic || e.hasBody) continue;
            LaneCar lc;
            lc.id = int(i);
            lc.M = e.lane;
            lc.type = &e.ttype;
            lc.speed = e.laneSpeed;
            lc.room = e.room;
            lc.number = e.number;
            v.push_back(lc);
        }
    };
    auto release = [&](size_t poolPos) {
        Extra& e = extras[size_t(pool[poolPos])];
        const Mtx M = e.phys->matrix();
        const bool upright = trafficUpright(world, M, e.room) && !e.phys->wrecked;
        if (!upright) e.phys->wrecked = true;
        e.state = upright ? 2 : 3;
        e.hasBody = false;
        e.lane = e.phys->lane;  // the lane matrix of its last sample (car-collision.md 6.5)
        e.laneSpeed = 0;
        e.lanePos = e.lane.o;
        e.d = 0;
        pool[poolPos] = pool.back();
        pool.pop_back();
    };
    hooks.makePhysical = [&](int id, const Mtx& laneM, float speed) -> TrafficPhys* {
        Extra& e = extras[size_t(id)];
        if (e.hasBody) return e.phys.get();
        if (pool.size() >= 32) release(0);
        const bool wasWrecked = e.phys->wrecked;
        const int identity = e.phys->identity;
        *e.phys = TrafficPhys();
        e.phys->identity = identity;
        e.phys->wrecked = wasWrecked;
        e.phys->setup(e.ttype, laneM, speed);
        e.phys->room = e.room;
        e.laneSpeed = 0;
        e.hasBody = true;
        e.state = 1;
        pool.push_back(id);
        return e.phys.get();
    };
    hooks.onCarImpact = [&](int a, int b, const Impact& im) { carImpacts.emplace_back(a, b, im); };
    if (city) hooks.findRoom = [&](const vec3& p, int hint) { return city->findRoom(p, hint); };

    auto numberOfMover = [&](const SceneMover& m) {
        if (m.kind == SceneMover::CarMover) return m.player ? 1 : m.number;
        return extras[size_t(m.laneId)].number;
    };
    auto writeExtra = [&](const Extra& e) {
        vec3 o, v, f, u;
        float speed = 0;
        int state = 1;
        if (e.car) {
            o = e.car->modelOrigin();
            v = e.car->velocity();
            f = e.car->forward();
            u = e.car->up();
            speed = e.car->signedSpeed();
        } else if (e.hasBody) {
            const RigidBody& rb = e.phys->rb;
            o = rb.p;
            v = rb.v;
            f = vec3(-rb.R.r[2].x, -rb.R.r[2].y, -rb.R.r[2].z);
            u = rb.R.r[1];
            speed = (v.x * f.x + v.y * f.y) + v.z * f.z;
            state = 1;
        } else {
            o = e.lane.o;
            f = vec3(-e.lane.Z.x, -e.lane.Z.y, -e.lane.Z.z);
            u = e.lane.Y;
            v = vec3(e.laneSpeed * f.x, e.laneSpeed * f.y, e.laneSpeed * f.z);
            speed = e.laneSpeed;
            state = e.state;
        }
        if (e.car) state = 1;
        std::fprintf(out, ",%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d", double(o.x), double(o.y),
                     double(o.z), double(v.x), double(v.y), double(v.z), double(f.x), double(f.y), double(f.z), double(u.x),
                     double(u.y), double(u.z), double(speed), state);
    };
    auto writeRowAll = [&](float t) {
        const vec3 o = car.modelOrigin();
        const vec3& v = car.velocity();
        const vec3 f = car.forward();
        const vec3 u = car.up();
        std::fprintf(out, "%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d", double(t),
                     double(o.x), double(o.y), double(o.z), double(v.x), double(v.y), double(v.z), double(f.x),
                     double(f.y), double(f.z), double(u.x), double(u.y), double(u.z), double(car.signedSpeed()),
                     double(car.rpm()), car.gearIndex() - 1);
        for (int i = 0; i < 4; ++i) std::fprintf(out, ",%.6g", double(car.wheel(i).omega));
        for (int i = 0; i < 4; ++i)
            std::fprintf(out, ",%.6g", double(car.wheel(i).comp + car.wheel(i).tune.suspensionExtent));
        for (const Extra& e : extras) writeExtra(e);
        std::fprintf(out, "\n");
    };
    auto traceVehicles = [&](bool end) {
        // vehicle 1
        {
            const RigidBody& b = car.body();
            std::fprintf(out, end ? "vend 1" : "veh 1 1");
            tv(out, b.p);
            tv(out, b.v);
            tv(out, b.w);
            std::fputs("\n", out);
        }
        for (const Extra& e : extras) {
            if (e.car) {
                const RigidBody& b = e.car->body();
                std::fprintf(out, end ? "vend %d" : "veh %d 1", e.number);
                tv(out, b.p);
                tv(out, b.v);
                tv(out, b.w);
            } else if (e.hasBody) {
                const RigidBody& b = e.phys->rb;
                std::fprintf(out, end ? "vend %d" : "veh %d 1", e.number);
                tv(out, b.p);
                tv(out, b.v);
                tv(out, b.w);
            } else {
                if (end) continue;
                std::fprintf(out, "veh %d 0", e.number);
                tv(out, e.lane.o);
                const vec3 f(-e.lane.Z.x, -e.lane.Z.y, -e.lane.Z.z);
                tv(out, vec3(e.laneSpeed * f.x, e.laneSpeed * f.y, e.laneSpeed * f.z));
                tv(out, vec3(0, 0, 0));
            }
            std::fputs("\n", out);
        }
    };

    if (!trace) {
        std::fprintf(out, "t,x,y,z,vx,vy,vz,fx,fy,fz,ux,uy,uz,speed,rpm,gear,w0,w1,w2,w3,c0,c1,c2,c3");
        for (const Extra& e : extras) {
            const int N = e.number;
            std::fprintf(out, ",x%d,y%d,z%d,vx%d,vy%d,vz%d,fx%d,fy%d,fz%d,ux%d,uy%d,uz%d,speed%d,state%d", N, N, N, N, N, N, N,
                         N, N, N, N, N, N, N);
        }
        std::fprintf(out, "\n");
        writeRowAll(0.0f);
    }
    int steps = 0;
    for (const RunSegment& seg : run.segments) {
        const long frames = std::lround(double(seg.duration) / double(F));
        for (long fr = 0; fr < frames; ++fr) {
            DriverControls c = seg.controls;
            if (seg.hasGear) c.setGear = seg.gear + 1;
            car.controlStep(c);
            // The computer cars' inputs (their own timelines).
            for (Extra& e : extras) {
                if (!e.car) continue;
                CarInputs in;
                int gear = -100;
                while (e.seg < e.spec.segments.size() &&
                       e.segFrame >= std::lround(double(e.spec.segments[e.seg].duration) / double(F))) {
                    ++e.seg;
                    e.segFrame = 0;
                }
                if (e.seg < e.spec.segments.size()) {
                    const RunSegment& s = e.spec.segments[e.seg];
                    in.throttle = s.controls.accelerate;
                    in.brake = s.controls.brake;
                    in.steer = s.controls.steer;
                    in.handbrake = s.controls.handbrake;
                    if (s.hasGear) gear = s.gear + 1;
                    ++e.segFrame;
                }
                e.car->setCarFrictionHandling(e.touched ? 2.0f : 1.0f);  // car-collision.md 5.4
                e.car->aiControlStep(in, gear);
            }
            // The traffic lanes.
            for (Extra& e : extras) {
                if (e.car || e.hasBody) continue;
                if (e.state == 0) {
                    e.d = e.laneSpeed * F + e.d;
                    const vec3 f(-e.lane.Z.x, -e.lane.Z.y, -e.lane.Z.z);
                    e.lane.o = vec3(f.x * e.d + e.spec.pos.x, e.spec.pos.y, f.z * e.d + e.spec.pos.z);
                    if (city) e.room = city->findRoom(vadd(e.lane.o, e.lane.Y), e.room);
                }
            }
            // Movers: vehicle 1, computer cars (police within 250 m), physical traffic in pool order.
            std::vector<SceneMover> movers;
            {
                SceneMover m;
                m.kind = SceneMover::CarMover;
                m.car = &car;
                m.player = true;
                m.number = 1;
                m.identity = playerIdentity;
                movers.push_back(m);
            }
            for (Extra& e : extras) {
                if (!e.car) continue;
                if (e.spec.kind == RunVehicle::Police) {
                    const vec3 dd = vsub(e.car->body().p, car.body().p);
                    const float d2 = (dd.z * dd.z + dd.y * dd.y) + dd.x * dd.x;
                    if (!(d2 < 62500.0f)) continue;
                }
                SceneMover m;
                m.kind = SceneMover::CarMover;
                m.car = e.car.get();
                m.number = e.number;
                m.identity = 10000 + e.number;
                m.box = e.box;
                m.shape = e.shape;
                movers.push_back(m);
            }
            for (int id : pool) {
                Extra& e = extras[size_t(id)];
                SceneMover m;
                m.kind = SceneMover::TrafficBodyMover;
                m.tp = e.phys.get();
                m.ttype = &e.ttype;
                m.laneId = id;
                m.number = e.number;
                m.identity = e.phys->identity;
                m.box = true;
                m.shape = BoxShape{e.ttype.size, e.ttype.cg};
                movers.push_back(m);
            }
            // Loose bodies last (row 7), then the player's rooms rule (loose-bodies.md 5.1).
            world.declareLoose(movers, car.collision().room);
            if (trace) writeLooseTrace(out, world, nullptr);
            for (SceneMover& m : movers) m.touchedPlayer = false;
            for (int k = 0; k < n; ++k) {
                if (trace) {
                    std::fprintf(out, "step %d", steps);
                    tf(out, float(steps) * dt);
                    std::fputs("\n", out);
                    traceVehicles(false);
                }
                carImpacts.clear();
                movers.reserve(movers.size() + extras.size() + 4);
                scene.runSample(movers, world, dt, invdt, hooks);
                ++steps;
                if (trace) {
                    for (const auto& ci : carImpacts) {
                        const Impact& im = std::get<2>(ci);
                        std::fprintf(out, "carimpact %d %d", std::get<0>(ci) == 0 ? 1 : std::get<0>(ci),
                                     std::get<1>(ci) == 0 ? 1 : std::get<1>(ci));
                        tv(out, im.P);
                        tv(out, im.n);
                        tf(out, im.depth);
                        std::fputs("\n", out);
                    }
                    traceVehicles(true);
                    writeLooseTrace(out, world, &movers);
                } else {
                    writeRowAll(float(steps) * dt);
                }
            }
            world.afterLooseSamples(movers);
            if (trace) writeLooseTrace(out, world, nullptr);
            else world.takeLooseEvents();
            (void)numberOfMover;
            // The computer cars' touched-player marks (5.4) for the next frame.
            for (const SceneMover& m : movers)
                for (Extra& e : extras)
                    if (e.car && m.car == e.car.get()) e.touched = m.touchedPlayer;
            // Release check (6.5), in pool order.
            for (size_t p = 0; p < pool.size();) {
                Extra& e = extras[size_t(pool[p])];
                const RigidBody& rb = e.phys->rb;
                if (e.phys->rest.state != 0 && -100.0f <= rb.p.y) {
                    if (city) e.room = city->findRoom(vadd(rb.p, rb.R.r[1]), e.room);
                    ++p;
                    continue;
                }
                release(p);
            }
            // World limits of vehicle 1 (cities).
            if (!flat && car.worldLimitReset(F)) {
                resetBumpRandom();
                world.resetPlacedObjects();
                car.placeAtDegrees(run.start, run.heading);
                if (run.manual) car.setAutomatic(false);
            }
        }
    }
    return true;
}

}  // namespace phys
}  // namespace cc
