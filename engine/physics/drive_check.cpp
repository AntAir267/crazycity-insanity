#include "physics/drive_check.h"

#include <cmath>
#include <cstdlib>

#include "core/strutil.h"
#include "city/city_data.h"
#include "model/asset_cache.h"
#include "model/vehicle.h"
#include "physics/collision_world.h"
#include "physics/scene.h"
#include "physics/trailer.h"
#include "vfs/vfs.h"

#include <map>
#include <memory>

namespace cc {
namespace phys {

namespace {
std::vector<std::string> words(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool toF(const std::string& s, float& v) {
    char* end = nullptr;
    double d = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return false;
    v = float(d);
    return true;
}
}  // namespace

bool parseRunFile(const std::string& text, RunFile& r, std::string& err) {
    r = RunFile();
    int lineNo = 0;
    for (std::string line : split(text, '\n')) {
        ++lineNo;
        size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        auto w = words(line);
        if (w.empty()) continue;
        const std::string& k = w[0];
        if (k == "car" && w.size() >= 2) {
            r.car = w[1];
        } else if (k == "world" && w.size() >= 2) {
            r.world = w[1];
        } else if (k == "start" && w.size() >= 5) {
            if (!toF(w[1], r.start.x) || !toF(w[2], r.start.y) || !toF(w[3], r.start.z) || !toF(w[4], r.heading)) {
                err = strf("line %d: bad start", lineNo);
                return false;
            }
        } else if (k == "frame" && w.size() >= 2) {
            r.frames.clear();
            for (size_t i = 1; i < w.size(); ++i) {
                float f = 0;
                if (!toF(w[i], f) || !(f > 0.0f)) {
                    err = strf("line %d: bad frame length", lineNo);
                    return false;
                }
                r.frames.push_back(f);
            }
        } else if (k == "trailer" && w.size() >= 2) {
            r.trailer = w[1] == "on" || w[1] == "1" || w[1] == "yes";
        } else if (k == "opponent") {
            if (w.size() < 12) {
                err = strf("line %d: opponent needs the route file and ten values", lineNo);
                return false;
            }
            OpponentRow& o = r.opponentRow;
            o.file = w[1];
            float v[10];
            for (int i = 0; i < 10; ++i)
                if (!toF(w[size_t(2 + i)], v[i])) {
                    err = strf("line %d: bad opponent value", lineNo);
                    return false;
                }
            o.throttle = v[0];
            o.sw[0] = int(v[1]) != 0;
            o.pad = v[2];
            o.brake = v[3];
            for (int k2 = 0; k2 < 5; ++k2) o.sw[1 + k2] = int(v[4 + k2]) != 0;
            o.corner = v[9];
            o.fields = 12;
            r.opponent = true;
        } else if (k == "streetprops" && w.size() >= 2) {
            r.streetProps = w[1] == "on" || w[1] == "1" || w[1] == "yes";
        } else if (k == "paint" && w.size() >= 2) {
            r.paint = std::atoi(w[1].c_str());
        } else if ((k == "police" || k == "ai") && w.size() >= 6) {
            RunVehicle v;
            v.kind = k == "police" ? RunVehicle::Police : RunVehicle::Ai;
            v.model = w[1];
            if (!toF(w[2], v.pos.x) || !toF(w[3], v.pos.y) || !toF(w[4], v.pos.z) || !toF(w[5], v.heading)) {
                err = strf("line %d: bad %s line", lineNo, k.c_str());
                return false;
            }
            r.extras.push_back(v);
        } else if (k == "traffic" && w.size() >= 6) {
            RunVehicle v;
            v.kind = RunVehicle::Traffic;
            v.model = w[1];
            if (!toF(w[2], v.pos.x) || !toF(w[3], v.pos.y) || !toF(w[4], v.pos.z) || !toF(w[5], v.heading)) {
                err = strf("line %d: bad traffic line", lineNo);
                return false;
            }
            for (size_t i = 6; i < w.size(); ++i)
                if (w[i].rfind("speed=", 0) == 0 && !toF(w[i].substr(6), v.speed)) {
                    err = strf("line %d: bad speed", lineNo);
                    return false;
                }
            r.extras.push_back(v);
        } else if (k.size() > 3 && k.compare(0, 3, "seg") == 0 && w.size() >= 2) {
            const int num = std::atoi(k.c_str() + 3);
            if (num < 2 || size_t(num - 2) >= r.extras.size()) {
                err = strf("line %d: %s names no vehicle", lineNo, k.c_str());
                return false;
            }
            RunSegment s;
            if (!toF(w[1], s.duration) || s.duration < 0) {
                err = strf("line %d: bad segment duration", lineNo);
                return false;
            }
            for (size_t i = 2; i < w.size(); ++i) {
                size_t eq = w[i].find('=');
                if (eq == std::string::npos) {
                    err = strf("line %d: expected name=value", lineNo);
                    return false;
                }
                const std::string name = w[i].substr(0, eq);
                float v = 0;
                if (!toF(w[i].substr(eq + 1), v)) {
                    err = strf("line %d: bad value for %s", lineNo, name.c_str());
                    return false;
                }
                if (name == "throttle") s.controls.accelerate = v;
                else if (name == "brake") s.controls.brake = v;
                else if (name == "steer") s.controls.steer = v;
                else if (name == "handbrake") s.controls.handbrake = v;
                else if (name == "gear") {
                    s.hasGear = true;
                    s.gear = int(std::lround(double(v)));
                } else {
                    err = strf("line %d: unknown control '%s'", lineNo, name.c_str());
                    return false;
                }
            }
            r.extras[size_t(num - 2)].segments.push_back(s);
        } else if (k == "seg" && w.size() >= 2) {
            RunSegment s;
            if (!toF(w[1], s.duration) || s.duration < 0) {
                err = strf("line %d: bad segment duration", lineNo);
                return false;
            }
            for (size_t i = 2; i < w.size(); ++i) {
                size_t eq = w[i].find('=');
                if (eq == std::string::npos) {
                    err = strf("line %d: expected name=value, got '%s'", lineNo, w[i].c_str());
                    return false;
                }
                std::string name = w[i].substr(0, eq);
                float v = 0;
                if (!toF(w[i].substr(eq + 1), v)) {
                    err = strf("line %d: bad value for %s", lineNo, name.c_str());
                    return false;
                }
                if (name == "throttle") s.controls.accelerate = v;
                else if (name == "brake") s.controls.brake = v;
                else if (name == "steer") s.controls.steer = v;
                else if (name == "handbrake") s.controls.handbrake = v;
                else if (name == "detach") s.detach = v != 0.0f;
                else if (name == "gear") {
                    s.hasGear = true;
                    s.gear = int(std::lround(double(v)));
                    r.manual = true;
                } else {
                    err = strf("line %d: unknown control '%s'", lineNo, name.c_str());
                    return false;
                }
            }
            r.segments.push_back(s);
        } else {
            err = strf("line %d: unknown setting '%s'", lineNo, k.c_str());
            return false;
        }
    }
    if (r.car.empty()) {
        err = "no 'car' line";
        return false;
    }
    r.opponentRow.car = r.car;
    if (r.opponent && (r.world == "flat" || !r.extras.empty() || !r.frames.empty())) {
        err = "an opponent run needs a city world, no extra vehicles and no frame line";
        return false;
    }
    return true;
}

int physicsStepsForFrame(float F) {
    if (F < 0.0001f) F = 0.0001f;
    if (F > 0.1f) F = 0.1f;
    const float q = (F - 0.001f) / 0.028571429f;
    int n = int(std::ceil(q));
    if (n < 1) n = 1;
    if (n > 3) n = 3;
    return n;
}

namespace {
void traceFloat(std::FILE* f, float v) {
    if (std::isnan(v)) std::fputs(" nan", f);
    else if (std::isinf(v)) std::fputs(v > 0 ? " inf" : " -inf", f);
    else std::fprintf(f, " %a", double(v));
}
void traceVec(std::FILE* f, const vec3& v) {
    traceFloat(f, v.x);
    traceFloat(f, v.y);
    traceFloat(f, v.z);
}
}  // namespace

void writeLooseTrace(std::FILE* f, CollisionWorld& world, const std::vector<SceneMover>* movers) {
    for (const LooseEvent& e : world.takeLooseEvents()) {
        switch (e.kind) {
        case LooseEvent::Eject:
            std::fprintf(f, "eject %s %d", e.part.c_str(), e.id);
            traceVec(f, e.P);
            traceVec(f, e.H);
            std::fputs("\n", f);
            break;
        case LooseEvent::Break:
            std::fprintf(f, "break %d %d", e.id, e.n);
            for (int id : e.ids) std::fprintf(f, " %d", id);
            std::fputs("\n", f);
            break;
        case LooseEvent::Gone: std::fprintf(f, "gone %d\n", e.id); break;
        case LooseEvent::Rest: std::fprintf(f, "rest %d\n", e.id); break;
        }
    }
    if (!movers) return;
    for (const SceneMover& m : *movers) {
        if (m.kind != SceneMover::LooseMover || m.loose < 0 || !m.U) continue;
        const LooseBody& L = world.looseBody(m.loose);
        std::fprintf(f, "loose %d", CollisionWorld::kSlotIdBase + L.slot);
        traceVec(f, L.rb.p);
        traceVec(f, L.rb.v);
        traceVec(f, L.rb.w);
        std::fputs("\n", f);
    }
}

void writeCheckRowColumns(std::FILE* out, const Car& car, double t) {
    const vec3 o = car.modelOrigin();
    const vec3& v = car.velocity();
    const vec3 f = car.forward();
    const vec3 u = car.up();
    std::fprintf(out, "%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d", t,
                 double(o.x), double(o.y), double(o.z), double(v.x), double(v.y), double(v.z), double(f.x),
                 double(f.y), double(f.z), double(u.x), double(u.y), double(u.z), double(car.signedSpeed()),
                 double(car.rpm()), car.gearIndex() - 1);
    for (int i = 0; i < 4; ++i) std::fprintf(out, ",%.6g", double(car.wheel(i).omega));
    for (int i = 0; i < 4; ++i)
        std::fprintf(out, ",%.6g", double(car.wheel(i).comp + car.wheel(i).tune.suspensionExtent));
}

namespace {
void writeTrailerColumns(std::FILE* out, const Trailer& t);

void writeRow(std::FILE* out, const Car& car, double t, const Trailer* trailer = nullptr) {
    writeCheckRowColumns(out, car, t);
    if (trailer) writeTrailerColumns(out, *trailer);
    std::fprintf(out, "\n");
}

// drive-check.md, "Trailer runs": the trailer's columns after vehicle 1's.
const char* const kTrailerHeader =
    ",tx,ty,tz,tvx,tvy,tvz,tfx,tfy,tfz,tux,tuy,tuz,tspeed,tw0,tw1,tw2,tw3,tc0,tc1,tc2,tc3,tlean,tbroken";

void writeTrailerColumns(std::FILE* out, const Trailer& t) {
    const RigidBody& b = t.body();
    const vec3 f = vscale(b.R.r[2], -1.0f);
    const vec3& u = b.R.r[1];
    std::fprintf(out, ",%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", double(b.p.x), double(b.p.y),
                 double(b.p.z), double(b.v.x), double(b.v.y), double(b.v.z), double(f.x), double(f.y), double(f.z),
                 double(u.x), double(u.y), double(u.z), double(pdot(b.v, f)));
    for (int i = 0; i < 4; ++i) std::fprintf(out, ",%.6g", double(t.wheel(i).omega));
    for (int i = 0; i < 4; ++i)
        std::fprintf(out, ",%.6g", double(t.wheel(i).comp + t.wheel(i).tune.suspensionExtent));
    std::fprintf(out, ",%.6g,%d", double(t.lean()), t.attached() ? 0 : 1);
}
}  // namespace

bool runMultiCheck(const Vfs& vfs, const RunFile& run, CollisionWorld& world, const CityData* city, bool flat, Car& car,
                   std::FILE* out, std::string& err, bool trace, AssetCache& assets);
// In ai/opponent_check.cpp.
bool runOpponentCheck(const Vfs& vfs, const RunFile& run, CollisionWorld& world, CityData& city, std::FILE* out,
                      std::string& err, bool trace);

namespace {
// Cities loaded once per process (the check runner runs many files).
struct LoadedCity {
    std::unique_ptr<AssetCache> assets;
    std::unique_ptr<CityData> data;
    std::unique_ptr<CollisionWorld> world;
};
std::map<std::string, LoadedCity>& cityCache() {
    static std::map<std::string, LoadedCity> c;
    return c;
}
}  // namespace

namespace {
CheckEffectsFactory& checkEffects() {
    static CheckEffectsFactory f;
    return f;
}
}  // namespace

void setCheckEffects(CheckEffectsFactory f) { checkEffects() = std::move(f); }

CarEffectsHook* checkEffectsFor(Car& car, const std::string& model, int paint) {
    return checkEffects() ? checkEffects()(car, model, paint) : nullptr;
}

bool runDriveCheck(const Vfs& vfs, const RunFile& run, std::FILE* out, std::string& err, bool trace) {
    if (run.opponent) {
        LoadedCity& lc = cityCache()[run.world];
        if (!lc.world) {
            lc.assets = std::make_unique<AssetCache>(vfs);
            lc.data = std::make_unique<CityData>();
            std::string msg;
            if (!lc.data->load(run.world, vfs, *lc.assets, msg)) {
                err = "city '" + run.world + "' cannot be loaded: " + msg;
                cityCache().erase(run.world);
                return false;
            }
            lc.world = std::make_unique<CollisionWorld>();
        }
        lc.world->setupCity(vfs, *lc.data, run.streetProps, false);  // no level objects (drive-check.md)
        return runOpponentCheck(vfs, run, *lc.world, *lc.data, out, err, trace);
    }
    CarTune tune = loadCarTune(vfs, run.car, true);
    if (!tune.simFile) {
        err = "tune/vehicle/" + run.car + ".vehCarSim not found";
        return false;
    }
    for (int i = 0; i < 4; ++i)
        if (!tune.wheels[i].found) {
            err = strf("wheel pivot %d of %s not found", i, run.car.c_str());
            return false;
        }
    const bool flat = run.world == "flat";
    CollisionWorld flatWorld;
    CollisionWorld* world = &flatWorld;
    if (flat) {
        flatWorld.setupFlat(vfs, run.start.y);
    } else {
        LoadedCity& lc = cityCache()[run.world];
        if (!lc.world) {
            lc.assets = std::make_unique<AssetCache>(vfs);
            lc.data = std::make_unique<CityData>();
            std::string msg;
            if (!lc.data->load(run.world, vfs, *lc.assets, msg)) {
                err = "city '" + run.world + "' cannot be loaded: " + msg;
                cityCache().erase(run.world);
                return false;
            }
            lc.world = std::make_unique<CollisionWorld>();
        }
        world = lc.world.get();
        // A fresh city state for every run (props stand again).
        lc.world->setupCity(vfs, *lc.data, run.streetProps, false);  // no level objects (drive-check.md)
    }
    resetBumpRandom();
    Car car;
    car.setup(tune);
    car.setPaintSet(paintSetFor(run.paint, tune.pkg ? tune.pkg->setCount : 0));
    car.setEffects(checkEffectsFor(car, run.car, run.paint));  // tests only (normally none)
    auto place = [&]() {
        if (flat) car.resetFlat(run.start.x, run.start.y, run.start.z, run.heading);
        else car.placeAtDegrees(run.start, run.heading);
    };
    place();
    if (run.manual) car.setAutomatic(false);
    // drive-check.md, "Trailer runs": the car's trailer, placed from its starting body.
    Trailer* trailer = nullptr;
    if (run.trailer) {
        if (!run.extras.empty() || !run.frames.empty()) {
            err = "a trailer run cannot have extra vehicles or a frame line";
            return false;
        }
        TrailerTune tt;
        std::string why;
        if (!loadTrailerTune(vfs, car.tune(), tt, why)) {
            err = run.car + " has no trailer: " + why;
            return false;
        }
        auto t = std::make_unique<Trailer>();
        t->setup(tt, car, world, false);
        t->identity = 10501;
        trailer = t.get();
        car.attachTrailer(std::move(t));
    }

    if (!run.extras.empty()) {
        if (!run.frames.empty()) {
            err = "a frame line is not supported in runs with extra vehicles yet";
            return false;
        }
        AssetCache local(vfs);
        AssetCache& assets = flat ? local : *cityCache()[run.world].assets;
        return runMultiCheck(vfs, run, *world, flat ? nullptr : cityCache()[run.world].data.get(), flat, car, out, err, trace,
                             assets);
    }

    const bool frameLine = !run.frames.empty();
    const float F0 = kCheckFrame;
    const int n0 = physicsStepsForFrame(F0);
    const float dt0 = F0 / float(n0);

    if (trace) {
        car.setTrace(out);
    } else {
        std::fprintf(out, "t,x,y,z,vx,vy,vz,fx,fy,fz,ux,uy,uz,speed,rpm,gear,w0,w1,w2,w3,c0,c1,c2,c3%s\n",
                     trailer ? kTrailerHeader : "");
        writeRow(out, car, 0.0f, trailer);
    }
    // The movers of each frame: the player's car, then the loose bodies (car-collision.md 1.1).
    Scene scene;
    SceneHooks hooks;
    std::vector<SceneMover> movers;
    int steps = 0;
    long frameNo = 0;
    double tSum = 0.0;  // runs with a frame line: the summed step times (double)
    bool wreckedSeen = false;
    for (const RunSegment& seg : run.segments) {
        const long frames = frameLine ? 0 : std::lround(double(seg.duration) / double(F0));
        float segSum = 0.0f;
        for (long f = 0;; ++f) {
            float F = F0;
            if (frameLine) {
                F = run.frames[size_t(frameNo % long(run.frames.size()))];
                if (!(segSum < seg.duration - 0.5f * F)) break;
            } else if (f >= frames) {
                break;
            }
            ++frameNo;
            const int n = frameLine ? physicsStepsForFrame(F) : n0;
            const float dt = frameLine ? F / float(n) : dt0;
            const float invdt = 1.0f / dt;
            DriverControls c = seg.controls;
            if (seg.hasGear) c.setGear = seg.gear + 1;
            car.controlStep(c);
            movers.clear();
            {
                SceneMover m;
                m.kind = SceneMover::CarMover;
                m.car = &car;
                m.player = true;
                m.number = 1;
                m.identity = 10001;
                movers.push_back(m);
            }
            if (trailer) {
                // Mover 2 (car-collision.md 1.1); `detach=1` presses Ctrl+B on the segment's first frame.
                SceneMover m;
                m.kind = SceneMover::TrailerMover;
                m.trailer = trailer;
                m.number = 1;
                m.identity = trailer->identity;
                movers.push_back(m);
                trailer->setDetachKey(seg.detach && f == 0);
            }
            world->declareLoose(movers, car.collision().room);
            if (trace) writeLooseTrace(out, *world, nullptr);
            for (int k = 0; k < n; ++k) {
                // time at the start of the step
                if (trace) car.traceStep(steps, frameLine ? float(tSum) : float(steps) * dt);
                scene.runSample(movers, *world, dt, invdt, hooks);
                ++steps;
                tSum = tSum + double(dt);
                if (trace) writeLooseTrace(out, *world, &movers);
                else writeRow(out, car, frameLine ? tSum : double(float(steps) * dt), trailer);
            }
            if (trailer) trailer->setDetachKey(false);
            world->afterLooseSamples(movers);
            if (trace) writeLooseTrace(out, *world, nullptr);
            else world->takeLooseEvents();
            segSum = segSum + F;
            if (car.wrecked() && !wreckedSeen) {
                wreckedSeen = true;
                std::fprintf(stderr, "%s: wrecked at step %d (damage %.0f > %.0f)\n", run.car.c_str(), steps,
                             double(car.damage().damage), double(car.damageLimit()));
            }
            // World limits, once per rendered frame (not on the flat world).
            if (!flat && car.worldLimitReset(F)) {
                // collision.md, "Resets in check runs": the generator restarts, every prop comes
                // back, the car goes back to its start (water effect off).
                resetBumpRandom();
                world->resetPlacedObjects();
                place();
                if (run.manual) car.setAutomatic(false);
            }
        }
    }
    return true;
}

}  // namespace phys
}  // namespace cc
