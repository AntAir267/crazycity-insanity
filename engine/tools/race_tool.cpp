// cctool race <city> <mode> <race> <amateur|pro> <seconds> <out.txt> <archives…>
//
// A whole race session without graphics: the race's settings, waypoints, opponents, traffic and
// police, the player's car driven by an autopilot (the AI driver with a route search to the
// current target waypoint), in the AI frame order of the race modes. Writes the race's events,
// a status line per second and the results; for checking the state machines natively.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ai/opponents.h"
#include "ai/police.h"
#include "ai/road_network.h"
#include "ai/route_search.h"
#include "ai/traffic.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "formats/cinfo.h"
#include "game/race.h"
#include "model/asset_cache.h"
#include "physics/car.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "vfs/vfs.h"

using namespace cc;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

namespace {

struct ToolHost : game::RaceHost {
    phys::Car* car = nullptr;
    phys::CollisionWorld* world = nullptr;
    ai::TrafficSystem* ts = nullptr;
    ai::PoliceSystem* ps = nullptr;
    std::vector<ai::TrafficPlayer>* tps = nullptr;
    std::FILE* out = nullptr;
    float preRace = 0;  // the pre-race camera's blend time left
    ai::PlayerTracking* tracking = nullptr;
    const ai::RoadNetwork* net = nullptr;
    const CityData* city = nullptr;
    const game::RaceSession* race = nullptr;
    void worldReset() override {
        phys::resetBumpRandom();
        world->resetPlacedObjects();
        if (tracking && race)
            tracking->worldReset(*net, *city, race->startPoint() + car->tune().sim.centerOfGravity, car->rotation().r[2],
                                 car->gearIndex() == 0);
        if (ts) ts->reset(*tps);  // also the cable cars (with no traffic too)
        if (ps && ps->active()) ps->reset();
    }
    void resetPlayer(const vec3& q, float theta) override { car->placeAt(q, theta); }
    void preRaceCamera() override { preRace = 3.5f; }
    bool preRaceCameraRunning() const override { return preRace > 0.0f; }
    void postRaceCamera() override {}
    void waterCamera() override {}
    bool copInSight() const override { return ps && ps->copInSight(*car, *world); }
};

}  // namespace

int cmdRace(const std::string& cityName, const std::string& modeName, int raceNo, bool pro, float seconds,
            const std::string& outPath, const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    CityEntry entry;
    for (const CityEntry& c : buildCityList(vfs))
        if (iequals(c.raceDir, cityName) || iequals(c.mapName, cityName)) entry = c;
    if (entry.mapName.empty()) {
        std::fprintf(stderr, "no city %s\n", cityName.c_str());
        return 1;
    }
    game::RaceMode mode = game::RaceMode::Checkpoint;
    if (modeName == "circuit") mode = game::RaceMode::Circuit;
    else if (modeName == "blitz") mode = game::RaceMode::Blitz;
    else if (modeName == "crash") mode = game::RaceMode::CrashCourse;
    AssetCache assets(vfs);
    CityData city;
    std::string msg;
    if (!city.load(entry.mapName, vfs, assets, msg, entry.raceDir) || !city.network()) {
        std::fprintf(stderr, "cannot load %s: %s\n", cityName.c_str(), msg.c_str());
        return 1;
    }
    ai::RoadNetwork& net = *city.network();
    net.setRace(vfs, game::raceName(mode, raceNo), pro);
    phys::CollisionWorld world;
    world.setupCity(vfs, city);
    std::FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) return 1;

    const std::string carName = mode == game::RaceMode::CrashCourse ? (iequals(entry.raceDir, "sf") ? "vpbullet" : "vpcab") : "vpmustang99";
    phys::CarTune tune = phys::loadCarTune(vfs, carName, true);
    phys::Car car;
    car.setup(tune);
    BytesPtr infoBytes = vfs.open("tune/" + carName + ".info");
    const InfoFile info = infoBytes ? parseInfoFile(std::string(infoBytes->begin(), infoBytes->end())) : InfoFile();

    ai::TrafficSystem ts;
    ai::PoliceSystem ps;
    std::vector<ai::TrafficPlayer> tps(1);
    ToolHost host;
    host.car = &car;
    host.world = &world;
    host.ts = &ts;
    host.ps = &ps;
    host.tps = &tps;
    host.out = out;
    ai::PlayerTracking tracking;
    host.tracking = &tracking;
    host.net = &net;
    host.city = &city;

    game::RaceSession race;
    game::RaceSession::Setup st;
    st.vfs = &vfs;
    st.city = &city;
    st.net = &net;
    st.world = &world;
    st.cityEntry = entry;
    st.mode = mode;
    st.race = raceNo;
    st.professional = pro;
    st.playerCar = carName;
    st.playerBound = tune.bound;
    st.scoringBias = float(std::atof(info.get("ScoringBias").c_str()));
    st.host = &host;
    double clock = 0;
    st.events = [&](const game::RaceEvent& e) {
        std::fprintf(out, "t %7.3f event %s %d %d\n", clock, e.name.c_str(), e.value, e.value2);
    };
    host.race = &race;
    std::string err;
    if (!race.setup(st, car, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    const game::SessionValues& v = race.values();
    std::fprintf(out, "%s %s %d (%s) '%s': opponents %d (number %.0f), cops %.0f, traffic %.2f, laps %d, time limit %.0f\n",
                 entry.raceDir.c_str(), game::modeTitle(mode), raceNo, pro ? "pro" : "amateur", race.raceTitle().c_str(),
                 int(race.opponents().opponents().size()), double(v.opponentNumber), double(v.copDensity), double(v.trafficDensity),
                 v.laps, double(v.timeLimit));
    // Traffic and police with the race's values (the race aimap is the network's now).
    auto updateTp = [&]() {
        tps[0].modelOrigin = car.modelOrigin();
        tps[0].bodyPos = car.body().p;
        tps[0].radius = car.modelRadius();
    };
    std::vector<vec3> oppPos;
    ts.create(vfs, assets, city, world, v.trafficDensity);
    ts.setOpponentPositions(&oppPos);
    ps.create(vfs, assets, city, net, world, v.copDensity, carName, tune.bound);
    race.start();
    updateTp();

    // The autopilot: the AI driver to the current target waypoint.
    ai::Driver pd;
    pd.create(&car, carName, tune.bound->bmin, tune.bound->bmax, false);
    std::vector<int> propOf(city.objects().size(), -1);
    for (size_t i = 0; i < world.objects().size(); ++i)
        if (world.objects()[i].kind == phys::ObjKind::Prop && world.objects()[i].cityObject >= 0)
            propOf[size_t(world.objects()[i].cityObject)] = int(i);
    ai::DriverEnv penv;
    penv.net = &net;
    penv.city = &city;
    penv.traffic = &ts;
    penv.world = &world;
    penv.propOfCityObject = &propOf;
    pd.reset(penv);

    phys::Scene scene;
    phys::SceneHooks hooks;
    hooks.findRoom = [&](const vec3& p, int h) { return city.findRoom(p, h); };
    hooks.laneCars = [&](std::vector<phys::LaneCar>& lv) {
        if (ts.active()) ts.laneCars(lv);
    };
    hooks.makePhysical = [&](int id, const phys::Mtx& lane, float speed) { return ts.takeBody(id, lane, speed); };
    const float F = 1.0f / 60.0f;
    const int n = phys::physicsStepsForFrame(F);
    const float dt = F / float(n), invdt = 1.0f / dt;
    const int frames = int(seconds * 60.0f);
    int lastState = -1;
    for (int fr = 0; fr < frames; ++fr) {
        // Steps 1–3: the race.
        race.frame(F, false, false, clock);
        if (race.state() != lastState) {
            std::fprintf(out, "t %7.3f state %d\n", clock, race.state());
            lastState = race.state();
        }
        // The player's controls: the autopilot, then the player overrides.
        if (pd.mode() == ai::DriverMode::Drive || pd.mode() == ai::DriverMode::Recover) {
            const game::WaypointList& L = race.waypoints();
            if (L.size() > 0) {
                ai::RouteQuery q;
                q.start = car.body().p;
                q.startZ = car.rotation().r[2];
                q.startRoom = car.collision().room;
                q.goal = L.targetPosition();
                q.goalRoom = city.findRoom(q.goal, 0);
                std::vector<int> route;
                ai::routeSearch(net, q, route);
                ai::DriverOrders o;
                o.route = route;
                o.D = q.goal;
                o.s1 = 25.0f;
                o.s2 = 0.0f;
                o.sw = {false, true, true, false, false, false};
                o.kthr = 1.0f;
                o.kc = 1.3f;
                o.kp = 75.0f;
                pd.giveOrders(o, penv);
            }
        }
        penv.now = clock;
        pd.frame(F, penv);
        car.aiControlStep(pd.carInputs());
        if (car.playerDisabled()) {
            phys::CarInputs in = car.inputs();
            in.throttle = 0;
            in.brake = 1;
            in.steer = -1;
            car.aiControlStep(in);
        }
        if (!car.drivable()) car.holdNotDrivable();
        if (host.preRace > 0) host.preRace -= F;
        // The AI frame: traffic, then the opponents and cops between the yield-mark passes.
        updateTp();
        tracking.frame(net, city, car.body().p);
        oppPos = race.opponentPositions();
        ts.update(F, tps);
        std::vector<ai::PolicePlayer> pps(1);
        pps[0].car = &car;
        pps[0].room = car.collision().room;
        pps[0].boxMin = tune.bound->bmin;
        pps[0].boxMax = tune.bound->bmax;
        std::vector<ai::PolicePlayer> opTargets;
        for (const ai::Opponent& o : race.opponents().opponents()) {
            ai::PolicePlayer t;
            t.car = o.car.get();
            t.room = o.car->collision().room;
            t.boxMin = o.boxMin;
            t.boxMax = o.boxMax;
            opTargets.push_back(t);
        }
        race.opponents().yieldMarks(false);
        ps.clearMarks();
        ai::DriverEnv oenv = penv;
        oenv.players = {ai::DriverPlayer{&car, tracking.road}};
        oenv.opponents = race.opponents().driverView();
        race.opponents().update(F, oenv, {&car});
        ps.updateCops(F, pps, ts.active() ? &ts : nullptr, clock, opTargets, oenv.opponents);
        race.opponents().yieldMarks(true);
        ps.setMarks();
        race.opponents().applyInputs();
        ps.applyInputs();
        // Physics.
        std::vector<phys::SceneMover> movers;
        phys::SceneMover pm;
        pm.kind = phys::SceneMover::CarMover;
        pm.car = &car;
        pm.player = true;
        pm.number = 1;
        pm.identity = 10001;
        movers.push_back(pm);
        if (ts.active()) {
            ts.prePhysics();
            ts.appendRowMovers(movers);
        }
        race.opponents().appendMovers(movers);
        ps.appendMovers(movers);
        if (ts.active()) ts.appendBodyMovers(movers);
        std::vector<int> oppRooms;
        for (const ai::Opponent& o : race.opponents().opponents())
            if (o.roomWaking) oppRooms.push_back(o.car->collision().room);
        world.declareLoose(movers, car.collision().room, &oppRooms);
        movers.reserve(movers.size() + 64);
        for (int k = 0; k < n; ++k) scene.runSample(movers, world, dt, invdt, hooks);
        world.afterLooseSamples(movers);
        world.takeLooseEvents();
        if (ts.active()) ts.afterPhysics();
        ps.afterPhysics(movers, F);
        race.opponents().afterPhysics(movers);
        clock += double(F);
        if (fr % 60 == 0) {
            const vec3 p = car.body().p;
            std::fprintf(out, "t %7.3f st %d clock %.2f cd %.2f gates %d/%d target %d lap %d place %d/%d  car (%.1f %.1f %.1f) v %.1f msg %d",
                         clock, race.state(), double(race.raceClock()), double(race.countdown()), race.waypoints().count(),
                         race.waypoints().size(), race.waypoints().target(), race.waypoints().lap(), race.playerPlace(), race.racers(),
                         double(p.x), double(p.y), double(p.z), double(car.speed()), race.message().text);
            int chasingPlayer = 0, chasingOpp = 0;
            for (const ai::Cop& c : ps.cops())
                if (c.state != ai::CopState::Idle && c.state != ai::CopState::Out) (c.perp == 0 ? chasingPlayer : chasingOpp)++;
            std::fprintf(out, " cops %d/%d chasing player %d opponents %d", int(ps.cops().size()), ps.chasing(), chasingPlayer, chasingOpp);
            for (size_t i = 0; i < race.opponents().opponents().size(); ++i) {
                const ai::Opponent& o = race.opponents().opponents()[i];
                std::fprintf(out, " | o%zu g%d p%d w%d/%zu md%d%s", i, race.opponentGates(int(i)), race.opponentPlace(int(i)),
                             o.driver.waypoint(), o.route.route.size(), int(o.driver.mode()), race.opponentFinished(int(i)) ? " F" : "");
            }
            std::fprintf(out, "\n");
        }
        if (race.resultsShown() || race.endMenuOpen()) break;
    }
    std::fprintf(out, "end at %.2f s: state %d, results %s, end menu %s, score %d, next race %s\n", clock, race.state(),
                 race.resultsShown() ? "shown" : "no", race.endMenuOpen() ? "open" : "no", race.score(),
                 race.nextRaceOffered() ? "offered" : "no");
    for (const game::ResultRow& r : race.results())
        std::fprintf(out, "  %d. %s %s\n", r.place, r.racer < 0 ? "player" : strf("opponent %d", r.racer).c_str(),
                     r.passMark ? "passed" : game::formatRaceTime(r.time).c_str());
    std::fclose(out);
    std::printf("race %s %s %d: state %d after %.1f s, %zu result rows%s\n", entry.raceDir.c_str(), modeName.c_str(), raceNo, race.state(),
                clock, race.results().size(), race.resultsShown() ? ", results shown" : "");
    return 0;
}
