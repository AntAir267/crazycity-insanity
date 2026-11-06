// Opponent check runs (spec/engine/drive-check.md, "Opponent runs"): vehicle 1 driven by the
// computer driver as race opponent 0 along its route file, in a city, with the CSV's extra
// columns and the drive trace's `opp` record.
#include <cmath>
#include <cstdio>
#include <string>

#include "ai/opponents.h"
#include "ai/road_network.h"
#include "city/city_data.h"
#include "core/strutil.h"
#include "formats/race_files.h"
#include "model/vehicle.h"
#include "physics/bound.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

namespace {

void tfl(std::FILE* f, float v) {
    if (std::isnan(v)) std::fputs(" nan", f);
    else if (std::isinf(v)) std::fputs(v > 0 ? " inf" : " -inf", f);
    else std::fprintf(f, " %a", double(v));
}

std::string readLocal(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return std::string();
    std::string s;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    std::fclose(f);
    return s;
}

bool fileExists(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fclose(f);
    return true;
}

}  // namespace

bool runOpponentCheck(const Vfs& vfs, const RunFile& run, CollisionWorld& world, CityData& city, std::FILE* out,
                      std::string& err, bool trace) {
    ai::RoadNetwork* net = city.network();
    if (!net) {
        err = "the city has no AI network";
        return false;
    }
    // The car as an opponent: full simulation, the box of its bound, the AI unstuck values.
    CarTune tune = loadCarTune(vfs, run.car, false);
    if (!tune.simFile) {
        err = "tune/vehicle/" + run.car + ".vehCarSim not found";
        return false;
    }
    if (!tune.bound) {
        err = "no bound for " + run.car;
        return false;
    }
    {
        const Bound& b = *tune.bound;
        const vec3 size(b.bmax.x - b.bmin.x, b.bmax.y - b.bmin.y, b.bmax.z - b.bmin.z);
        const vec3 off(size.x * 0.5f + b.bmin.x, size.y * 0.5f + b.bmin.y, size.z * 0.5f + b.bmin.z);
        tune.bound = std::make_shared<Bound>(makeBoxBound(size, off));
    }
    ai::Opponent o;
    o.index = 0;
    o.row = run.opponentRow;
    o.car = std::make_unique<Car>();
    Car& car = *o.car;
    car.setup(tune);
    car.setPaintSet(paintSetFor(run.paint, tune.pkg ? tune.pkg->setCount : 0));
    const Bound& bb = *tune.bound;
    o.box = true;
    o.shape = BoxShape{vec3(bb.bmax.x - bb.bmin.x, bb.bmax.y - bb.bmin.y, bb.bmax.z - bb.bmin.z), bb.centre};
    o.driver.create(&car, run.car, bb.bmin, bb.bmax, false);
    o.driver.setRacerNumber(0);
    // The route file: the run file's folder first, then race/<world>/<file>.
    std::string text;
    const std::string local = run.runDir.empty() ? run.opponentRow.file : run.runDir + "/" + run.opponentRow.file;
    if (fileExists(local)) {
        text = readLocal(local);
    } else if (BytesPtr b = vfs.open("race/" + city.raceDir() + "/" + run.opponentRow.file)) {
        text = std::string(b->begin(), b->end());
    } else {
        err = "route file " + run.opponentRow.file + " not found";
        return false;
    }
    o.route = ai::resolveOpponentRoute(parseOpp(text), city, *net);
    o.q = o.route.start;
    o.theta = o.route.heading;
    // Placed at the start point, snapped to the ground and placed again; then the generators restart.
    car.placeAt(o.q, o.theta);
    ai::OpponentSystem::groundSnap(car, o.q, o.theta, world);
    resetBumpRandom();
    ai::DriverEnv env;
    env.net = net;
    env.city = &city;
    env.world = &world;
    env.now = 0.0;
    o.driver.reset(env);
    o.state = ai::OpponentState::Racing;
    o.prev = ai::OpponentState::None;

    const float F = kCheckFrame;
    const int n = physicsStepsForFrame(F);
    const float dt = F / float(n);
    const float invdt = 1.0f / dt;
    double clock = 0.0;
    const std::vector<const Car*> noPlayers;

    auto extra = [&](std::FILE* f) {
        const vec3& T = o.driver.target();
        const CarInputs& in = car.inputs();
        std::fprintf(f, ",%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g", int(o.state), int(o.driver.mode()),
                     o.driver.waypoint(), o.driver.lap(), double(T.x), double(T.y), double(T.z), double(in.steer),
                     double(in.throttle), double(in.brake), double(in.handbrake));
    };
    if (trace) car.setTrace(out);
    else
        std::fprintf(out, "t,x,y,z,vx,vy,vz,fx,fy,fz,ux,uy,uz,speed,rpm,gear,w0,w1,w2,w3,c0,c1,c2,c3,"
                          "state,mode,w,lap,tx,ty,tz,steer,throttle,brake,hbrake\n");
    Scene scene;
    SceneHooks hooks;
    hooks.findRoom = [&city](const vec3& p, int hint) { return city.findRoom(p, hint); };
    std::vector<SceneMover> movers;
    int steps = 0;
    bool first = true;
    for (const RunSegment& seg : run.segments) {
        const long frames = std::lround(double(seg.duration) / double(F));
        for (long fr = 0; fr < frames; ++fr) {
            // The opponent's update (orders on the first frame, the driver's frame, the
            // fall-through test), then its inputs before the physics.
            env.now = clock;
            ai::OpponentSystem::updateOne(o, F, env, noPlayers, 1, net);
            car.aiControlStep(o.driver.carInputs());
            if (trace) {
                std::fprintf(out, "opp %d %d %d %d %d %d %d", int(o.state), int(o.driver.mode()), o.driver.waypoint(),
                             o.driver.lap(), o.driver.window(0), o.driver.window(1), o.driver.window(2));
                tfl(out, o.driver.target().x);
                tfl(out, o.driver.target().y);
                tfl(out, o.driver.target().z);
                std::fputs("\n", out);
            } else if (first) {
                writeCheckRowColumns(out, car, 0.0);
                extra(out);
                std::fputs("\n", out);
            }
            first = false;
            movers.clear();
            {
                SceneMover m;
                m.kind = SceneMover::CarMover;
                m.car = &car;
                m.player = true;  // declared as the player's car is (loose bodies' rooms)
                m.number = 1;
                m.identity = 10001;
                m.box = true;
                m.shape = o.shape;
                movers.push_back(m);
            }
            world.declareLoose(movers, car.collision().room);
            if (trace) writeLooseTrace(out, world, nullptr);
            for (int k = 0; k < n; ++k) {
                if (trace) car.traceStep(steps, float(steps) * dt);
                scene.runSample(movers, world, dt, invdt, hooks);
                ++steps;
                if (trace) {
                    writeLooseTrace(out, world, &movers);
                } else {
                    writeCheckRowColumns(out, car, double(float(steps) * dt));
                    extra(out);
                    std::fputs("\n", out);
                }
            }
            world.afterLooseSamples(movers);
            if (trace) writeLooseTrace(out, world, nullptr);
            else world.takeLooseEvents();
            o.driver.setTouchedPlayer(false);
            clock += double(F);
        }
    }
    return true;
}

}  // namespace phys
}  // namespace cc
