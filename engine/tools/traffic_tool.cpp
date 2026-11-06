// cctool traffic commands: an ambient-traffic run with a parked player (traffic.md, "Check runs
// with traffic") and the population survey of traffic-population.md, "Checked against".
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ai/driver.h"
#include "ai/police.h"
#include "ai/road_network.h"
#include "ai/route_search.h"
#include "ai/traffic.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "drive/drive_session.h"
#include "platform/platform.h"
#include "model/asset_cache.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "vfs/vfs.h"

using namespace cc;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

int cmdTraffic(const std::string& cityName, float seconds, float density, const std::string& outPath,
               const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    AssetCache assets(vfs);
    CityData city;
    std::string msg;
    if (!city.load(cityName, vfs, assets, msg) || !city.network()) {
        std::fprintf(stderr, "cannot load %s: %s\n", cityName.c_str(), msg.c_str());
        return 1;
    }
    phys::CollisionWorld world;
    world.setupCity(vfs, city);
    ai::TrafficSystem ts;
    ts.create(vfs, assets, city, world, density);
    // The parked player: at the first eligible intersection (the repeatable start), on the street.
    vec3 start(0, 0, 0);
    if (!city.starts().empty()) start = city.starts()[0].pos;
    ai::TrafficPlayer p;
    p.modelOrigin = start + vec3(0, 0.3f, 0);
    p.bodyPos = start + vec3(0, 0.8f, 0);
    p.radius = 2.5f;
    const std::vector<ai::TrafficPlayer> players{p};
    ts.reset(players);
    std::FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) return 1;
    std::fprintf(out, "frame,car,state,road,side,lane,listlane,rail,k,x,u,v,a,vt,go,react,indicator,px,py,pz,zx,zy,zz\n");
    for (const auto& e : ts.events())
        std::fprintf(out, "0,event,%s,%d,%d\n", e.entering ? "enter" : "leave", e.road, e.cars);
    const float F = float(1.0 / 60.0);
    const int frames = int(seconds * 60.0f + 0.5f);
    std::vector<vec3> prev(ts.cars().size());
    std::vector<int> prevRoad(ts.cars().size(), -2), prevSide(ts.cars().size(), 0);
    float worstJump = 0;
    int worstCar = -1, worstFrame = 0, nan = 0, maxOn = 0;
    for (int f = 1; f <= frames; ++f) {
        ts.update(F, players);
        ts.afterPhysics();
        for (const auto& e : ts.events())
            std::fprintf(out, "%d,event,%s,%d,%d\n", f, e.entering ? "enter" : "leave", e.road, e.cars);
        int on = 0;
        for (const ai::TrafficCar& c : ts.cars()) {
            if (!c.onStreets) {
                prevRoad[size_t(c.index)] = -2;
                continue;
            }
            ++on;
            if (!std::isfinite(c.pos.x) || !std::isfinite(c.pos.y) || !std::isfinite(c.pos.z)) ++nan;
            std::fprintf(out, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g\n", f,
                         c.index, c.state, c.road, c.side, c.lane, c.listLane, c.rail, c.k, double(c.x), double(c.u),
                         double(c.v), double(c.a), double(c.vt), int(c.go), int(c.react), c.indicator, double(c.pos.x),
                         double(c.pos.y), double(c.pos.z), double(c.Z.x), double(c.Z.y), double(c.Z.z));
            const bool turnedRound = prevRoad[size_t(c.index)] == c.road && prevSide[size_t(c.index)] != c.side;
            if (prevRoad[size_t(c.index)] != -2 && c.state == ai::TrafficDriving && !turnedRound) {
                const vec3 d = c.pos - prev[size_t(c.index)];
                const float jump = std::sqrt(d.x * d.x + d.z * d.z) - c.v * F;
                if (jump > worstJump) {
                    worstJump = jump;
                    worstCar = c.index;
                    worstFrame = f;
                }
            }
            prev[size_t(c.index)] = c.pos;
            prevRoad[size_t(c.index)] = c.road;
            prevSide[size_t(c.index)] = c.side;
        }
        if (on > maxOn) maxOn = on;
    }
    std::fclose(out);
    const auto& st = ts.stats();
    std::printf("%s: D %.2f, %d frames: %d cars on the streets at the end (max %d), placed %d, emptied %d, left %d, "
                "turned round %d, lane changes %d, NaN %d; largest step beyond v*F: %.3f m (car %d, frame %d)\n",
                cityName.c_str(), double(density), frames, ts.carsOnStreets(), maxOn, st.placed, st.emptied, st.leftStreets,
                st.turnedRound, st.laneChanges, nan, double(worstJump), worstCar, worstFrame);
    return nan ? 1 : 0;
}

int cmdTrafficSurvey(const std::string& cityName, const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    AssetCache assets(vfs);
    CityData city;
    std::string msg;
    if (!city.load(cityName, vfs, assets, msg) || !city.network()) return 1;
    phys::CollisionWorld world;
    world.setupCity(vfs, city);
    for (float D : {0.25f, 0.5f, 1.0f}) {
        ai::TrafficSystem ts;
        ts.create(vfs, assets, city, world, D);
        double sumCars = 0, sumL = 0, maxL = 0;
        int rooms = 0, maxRoads = 0;
        double sumRoads = 0;
        for (size_t r = 1; r < city.rooms().size(); ++r) {
            const auto& list = city.network()->ambientRoads(int(r));
            if (list.empty()) continue;
            ++rooms;
            sumRoads += double(list.size());
            if (int(list.size()) > maxRoads) maxRoads = int(list.size());
            ts.resetAtRoom(int(r));
            sumCars += ts.carsOnStreets();
            sumL += ts.stats().lastLaneLength;
            if (ts.stats().lastLaneLength > maxL) maxL = ts.stats().lastLaneLength;
        }
        std::printf("%s D %.2f: %d rooms with roads (up to %d roads, mean %.1f); entering lane length mean %.1f km (max %.1f "
                    "km); cars placed mean %.0f\n",
                    cityName.c_str(), double(D), rooms, maxRoads, sumRoads / rooms, sumL / rooms / 1000.0, maxL / 1000.0,
                    sumCars / rooms);
    }
    return 0;
}

// cctool chase <city> <cop> <seconds> <traffic density> <out.txt> <archives>: a player car driven by
// the AI driver to a far intersection, starting 30 m in front of cop <cop>, with the city's cops and
// traffic; one line per second (and every state change) of the player and the chasing cops.
int cmdChase(const std::string& cityName, int copIndex, float seconds, float density, const std::string& outPath,
             const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    AssetCache assets(vfs);
    CityData city;
    std::string msg;
    if (!city.load(cityName, vfs, assets, msg) || !city.network()) {
        std::fprintf(stderr, "cannot load %s: %s\n", cityName.c_str(), msg.c_str());
        return 1;
    }
    ai::RoadNetwork& net = *city.network();
    phys::CollisionWorld world;
    world.setupCity(vfs, city);
    ai::PoliceSystem ps;
    ps.create(vfs, assets, city, net, world, 1.0f, "vpbug", nullptr);
    ps.reset();
    if (copIndex < 0 || size_t(copIndex) >= ps.cops().size()) return 1;
    phys::CarTune tune = phys::loadCarTune(vfs, "vpbug", true);
    phys::Car player;
    player.setup(tune);
    const ai::Cop& cop = ps.cops()[size_t(copIndex)];
    const vec3 fwd = cop.car->forward();
    vec3 at = cop.car->body().p + vec3(fwd.x * 30.0f, 0, fwd.z * 30.0f);
    phys::ProbeHit hit;
    if (world.probeStatic(at + vec3(0, 6, 0), at - vec3(0, 20, 0), hit)) at.y = hit.point.y + 1.0f;
    player.placeAt(at, cop.entry.heading);
    ai::TrafficSystem ts;
    ts.create(vfs, assets, city, world, density);
    std::vector<ai::TrafficPlayer> tps(1);
    auto updateTp = [&]() {
        tps[0].modelOrigin = player.modelOrigin();
        tps[0].bodyPos = player.body().p;
        tps[0].radius = player.modelRadius();
    };
    updateTp();
    ts.reset(tps);
    // The player's own AI driver: to the intersection farthest (flat) from the start.
    const auto& ix = net.intersections();
    int far = 0;
    float best = -1;
    for (size_t i = 0; i < ix.size(); ++i) {
        const float dx = ix[i].centre.x - at.x, dz = ix[i].centre.z - at.z;
        const float d = dx * dx + dz * dz;
        if (d > best && d < 900.0f * 900.0f) {
            best = d;
            far = int(i);
        }
    }
    ai::Driver pd;
    pd.create(&player, "vpbug", tune.bound ? tune.bound->bmin : vec3(-1, 0, -2), tune.bound ? tune.bound->bmax : vec3(1, 1, 2), false);
    ai::DriverEnv penv;
    penv.net = &net;
    penv.city = &city;
    penv.traffic = &ts;
    penv.world = &world;
    std::vector<int> propOf(city.objects().size(), -1);
    for (size_t i = 0; i < world.objects().size(); ++i)
        if (world.objects()[i].kind == phys::ObjKind::Prop && world.objects()[i].cityObject >= 0)
            propOf[size_t(world.objects()[i].cityObject)] = int(i);
    penv.propOfCityObject = &propOf;
    pd.reset(penv);
    auto playerOrders = [&]() {
        ai::RouteQuery q;
        q.start = player.body().p;
        q.startZ = player.rotation().r[2];
        q.startRoom = player.collision().room;
        q.goal = ix[size_t(far)].centre;
        std::vector<int> route;
        ai::routeSearch(net, q, route);
        ai::DriverOrders o;
        o.route = route;
        o.D = ix[size_t(far)].centre;
        o.s1 = 0;
        o.s2 = 5;
        o.sw = {false, true, true, false, true, false};
        o.kthr = 0.8f;
        o.kc = 1.0f;
        o.kb = 0.69999999f;
        o.kp = 75.0f;
        pd.giveOrders(o, penv);
        return route.size();
    };
    const size_t routeLen = playerOrders();
    std::FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) return 1;
    std::fprintf(out, "player from (%.1f %.1f %.1f) to intersection %d, route %zu entries\n", double(at.x), double(at.y), double(at.z), far,
                 routeLen);
    std::vector<ai::PolicePlayer> pps(1);
    pps[0].car = &player;
    pps[0].boxMin = tune.bound ? tune.bound->bmin : vec3();
    pps[0].boxMax = tune.bound ? tune.bound->bmax : vec3();
    phys::Scene scene;
    phys::SceneHooks hooks;
    hooks.findRoom = [&](const vec3& p, int h) { return city.findRoom(p, h); };
    hooks.laneCars = [&](std::vector<phys::LaneCar>& v) { if (ts.active()) ts.laneCars(v); };
    hooks.makePhysical = [&](int id, const phys::Mtx& lane, float speed) { return ts.takeBody(id, lane, speed); };
    const float F = 1.0f / 60.0f;
    const int n = phys::physicsStepsForFrame(F);
    const float dt = F / float(n), invdt = 1.0f / dt;
    const int frames = int(seconds * 60.0f);
    std::vector<int> lastState(ps.cops().size(), -9), lastTactic(ps.cops().size(), -9), lastMode(ps.cops().size(), -9);
    int nan = 0, turnBacks = 0, maxChasing = 0;
    float minRange = 1e9f;
    for (int f = 0; f < frames; ++f) {
        const double t = f * double(F);
        // The player's driver and the AI frame: traffic, police.
        pd.frame(F, penv);
        player.aiControlStep(pd.carInputs());
        updateTp();
        ts.update(F, tps);
        pps[0].room = player.collision().room;
        ps.update(F, pps, ts.active() ? &ts : nullptr, t);
        std::vector<phys::SceneMover> movers;
        phys::SceneMover pm;
        pm.kind = phys::SceneMover::CarMover;
        pm.car = &player;
        pm.player = true;
        pm.number = 1;
        pm.identity = 10001;
        movers.push_back(pm);
        if (ts.active()) {
            ts.prePhysics();
            ts.appendRowMovers(movers);
        }
        ps.appendMovers(movers);
        if (ts.active()) ts.appendBodyMovers(movers);
        movers.reserve(movers.size() + 64);
        for (int k = 0; k < n; ++k) scene.runSample(movers, world, dt, invdt, hooks);
        if (ts.active()) ts.afterPhysics();
        ps.afterPhysics(movers, F);
        int chasing = 0;
        bool changed = false;
        for (const ai::Cop& c : ps.cops()) {
            const vec3 p = c.car->body().p;
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) ++nan;
            if (c.state != ai::CopState::Idle && c.state != ai::CopState::Out) {
                ++chasing;
                minRange = std::min(minRange, c.range);
            }
            const size_t i = size_t(c.index);
            if (int(c.state) != lastState[i] || int(c.tactic) != lastTactic[i] || int(c.driver.mode()) != lastMode[i]) {
                if (c.driver.mode() == ai::DriverMode::TurnBack && lastMode[i] != int(ai::DriverMode::TurnBack)) ++turnBacks;
                if (lastState[i] != -9) changed = true;
                lastState[i] = int(c.state);
                lastTactic[i] = int(c.tactic);
                lastMode[i] = int(c.driver.mode());
            }
        }
        maxChasing = std::max(maxChasing, chasing);
        if (f % 60 == 0 || changed) {
            const vec3 pp = player.body().p;
            std::fprintf(out, "t %6.2f player (%7.1f %5.1f %7.1f) v %4.1f mode %d w %d S0 %d |", t, double(pp.x), double(pp.y),
                         double(pp.z), double(player.speed()), int(pd.mode()), pd.waypoint(), pd.window(0));
            for (const ai::Cop& c : ps.cops()) {
                if (c.state == ai::CopState::Idle && c.perp < 0) continue;
                std::fprintf(out, " cop %d st %d ta %d md %d v %4.1f r %5.1f rt %zu w %d S0 %d in %.2f/%.2f/%.2f%s;", c.index, int(c.state),
                             int(c.tactic), int(c.driver.mode()), double(c.car->speed()), double(c.range), c.route.size(),
                             c.driver.waypoint(), c.driver.window(0), double(c.car->inputs().throttle), double(c.car->inputs().brake),
                             double(c.car->inputs().steer), c.mover ? "" : " (frozen)");
            }
            std::fprintf(out, "\n");
        }
    }
    std::fprintf(out, "end: %d chasing at most, closest %.1f m, %d turn-backs, %d non-finite cop positions\n", maxChasing,
                 double(minRange), turnBacks, nan);
    std::fclose(out);
    std::printf("chase %s: %d chasing at most, closest %.1f m, %d turn-backs, %d non-finite\n", cityName.c_str(), maxChasing,
                double(minRange), turnBacks, nan);
    return 0;
}

// cctool drivesim <script> <out.csv> <archives>: the whole drive-mode frame (keyboard smoothing,
// control step, physics, chase camera) on flat ground, driven by held keys. Script lines:
// `car <name>`, `frame F1 F2 …` (cycled), `hold <seconds> [Up] [Down] [Left] [Right] [Space]`.
// One CSV row per frame: time, frame length, heading, yaw rate, speed, gear, inputs, camera.
int cmdDriveSim(const std::string& scriptPath, const std::string& outPath, const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    std::FILE* sf = std::fopen(scriptPath.c_str(), "rb");
    if (!sf) return 1;
    std::string text;
    char buf[4096];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof buf, sf)) > 0) text.append(buf, got);
    std::fclose(sf);
    std::string car = "vpbug";
    std::vector<float> frames{1.0f / 60.0f};
    struct Hold {
        float seconds = 0;
        bool up = false, down = false, left = false, right = false, space = false;
    };
    std::vector<Hold> holds;
    for (const std::string& raw : split(text, '\n')) {
        std::vector<std::string> w;
        for (const std::string& t : split(trim(raw), ' '))
            if (!t.empty()) w.push_back(t);
        if (w.empty() || w[0][0] == '#') continue;
        if (w[0] == "car" && w.size() > 1) car = w[1];
        else if (w[0] == "frame") {
            frames.clear();
            for (size_t i = 1; i < w.size(); ++i) frames.push_back(float(std::atof(w[i].c_str())));
        } else if (w[0] == "hold" && w.size() > 1) {
            Hold h;
            h.seconds = float(std::atof(w[1].c_str()));
            for (size_t i = 2; i < w.size(); ++i) {
                if (w[i] == "Up") h.up = true;
                else if (w[i] == "Down") h.down = true;
                else if (w[i] == "Left") h.left = true;
                else if (w[i] == "Right") h.right = true;
                else if (w[i] == "Space") h.space = true;
            }
            holds.push_back(h);
        }
    }
    if (frames.empty()) frames.push_back(1.0f / 60.0f);
    AssetCache assets(vfs);
    phys::CollisionWorld world;
    world.setupFlat(vfs, 0.0f);
    DriveSession s;
    std::string msg;
    if (!s.start(vfs, assets, car, VehicleKind::Player, 0, 0, &world, 1.33f, msg)) {
        std::fprintf(stderr, "cannot drive %s: %s\n", car.c_str(), msg.c_str());
        return 1;
    }
    s.placeFlat();
    std::FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) return 1;
    std::fprintf(out, "t,F,heading_deg,yaw_rate,speed,gear,throttle,brake,steer,handbrake,cam_dist,cam_max,cam_rate,cam_swing,cam_rev,cam_heading_deg,cam_yaw_rate,car_screen_deg\n");
    float prevCamH = 0;
    bool havePrevCam = false;
    InputState in;
    in.hasFocus = true;
    double t = 0;
    long k = 0;
    float peak = 0;
    double peakT = 0;
    for (const Hold& h : holds) {
        float sum = 0;
        while (sum < h.seconds - 0.5f * frames[size_t(k) % frames.size()]) {
            const float F = frames[size_t(k) % frames.size()];
            ++k;
            const bool wasUp = in.down[KeyUp], wasDown = in.down[KeyDown], wasLeft = in.down[KeyLeft], wasRight = in.down[KeyRight],
                       wasSpace = in.down[KeySpace];
            in.beginFrame();
            in.down[KeyUp] = h.up;
            in.down[KeyDown] = h.down;
            in.down[KeyLeft] = h.left;
            in.down[KeyRight] = h.right;
            in.down[KeySpace] = h.space;
            in.pressed[KeyUp] = h.up && !wasUp;
            in.pressed[KeyDown] = h.down && !wasDown;
            in.pressed[KeyLeft] = h.left && !wasLeft;
            in.pressed[KeyRight] = h.right && !wasRight;
            in.pressed[KeySpace] = h.space && !wasSpace;
            s.frame(in, F, 1.33f, nullptr);
            t += double(s.lastFrame());
            sum += F;
            const phys::Car& c = s.car();
            float camDist = 0, camMax = 0, camRate = 0, camSwing = 0;
            int camRev = 0;
            if (const ChaseCamera* ch = s.cameras().activeChase()) {
                camDist = length(s.cameras().frame().pose.p - ch->trackPoint());
                camMax = ch->maxDist();
                camRate = ch->approachRate();
                camSwing = ch->swing();
                camRev = ch->reversing() ? 1 : 0;
            }
            if (std::fabs(s.yawRate()) > std::fabs(peak)) {
                peak = s.yawRate();
                peakT = t;
            }
            const CamPose& cp = s.cameras().frame().pose;
            const float camH = float(std::atan2(-double(cp.Z.x), -double(cp.Z.z)));
            float dch = camH - prevCamH;
            while (dch > 3.14159265f) dch -= 6.2831853f;
            while (dch < -3.14159265f) dch += 6.2831853f;
            const float camRateYaw = havePrevCam ? dch / s.lastFrame() : 0.0f;
            prevCamH = camH;
            havePrevCam = true;
            float rel = (s.heading() - camH) * 57.29578f;  // the car's heading as seen on screen
            while (rel > 180.0f) rel -= 360.0f;
            while (rel < -180.0f) rel += 360.0f;
            std::fprintf(out, "%.4f,%.4f,%.3f,%.4f,%.4f,%d,%.4f,%.4f,%.4f,%.1f,%.4f,%.2f,%.4f,%.4f,%d,%.3f,%.4f,%.2f\n", t, double(F),
                         double(s.heading()) * 57.29578, double(s.yawRate()), double(c.signedSpeed()), c.gearIndex() - 1,
                         double(c.inputs().throttle), double(c.inputs().brake), double(c.inputs().steer),
                         double(c.inputs().handbrake), double(camDist), double(camMax), double(camRate), double(camSwing), camRev,
                         double(camH) * 57.29578, double(camRateYaw), double(rel));
        }
    }
    std::fclose(out);
    std::printf("drivesim %s: %ld frames, peak yaw %.2f rad/s at %.2f s\n", car.c_str(), k, double(peak), peakT);
    return 0;
}
