// cctool soundrun: a city drive with traffic and police (the chase set-up of cctool chase: the
// player's car, driven by an AI driver, starts 30 m in front of a cop) with the game's sound
// mixed by the software mixer of the browser build; prints the 3D slot holders, the chase count
// and the sound events once per second, and writes what is heard to <out>.wav (22,050 Hz
// stereo; a listener 6 m behind the car).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "ai/driver.h"
#include "ai/pedestrians.h"
#include "ai/police.h"
#include "ai/road_network.h"
#include "ai/route_search.h"
#include "ai/traffic.h"
#include "audio/game_audio.h"
#include "audio/soft_mixer.h"
#include "audio/sound_random.h"
#include "audio/sound_system.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/psdl.h"
#include "model/asset_cache.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "vfs/vfs.h"

using namespace cc;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

namespace {
void writeWav(const std::string& path, const std::vector<int16_t>& pcm, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return;
    auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
    const uint32_t bytes = uint32_t(pcm.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + bytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(bytes);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
}
}  // namespace

int cmdSoundRun(const std::string& cityName, int copIndex, float seconds, float density, const std::string& outPath,
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
    // Pedestrians (the city's pool at full density).
    ai::PedestrianSystem peds;
    peds.create(vfs, assets, city, world, ai::PedestrianSystem::countFor(net.cityAimap().pedPool, 1.0f), false);
    auto pedPlayers = [&]() {
        std::vector<ai::PedPlayer> out(1);
        ai::PedPlayer& p = out[0];
        p.bodyPos = player.body().p;
        const phys::M3& R = player.rotation();
        p.X = R.r[0];
        p.Y = R.r[1];
        p.Z = R.r[2];
        p.velocity = player.velocity();
        p.speed = player.speed();
        p.steer = player.inputs().steer;
        p.gear = player.gearIndex();
        p.inertiaBox = player.tune().sim.inertiaBox;
        p.radius = player.modelRadius();
        return out;
    };
    peds.reset(pedPlayers());
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
    {
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
    }
    // The game's sound.
    audio::SoundSystem sys;
    audio::SoftMixer out;
    sys.setVfs(&vfs);
    sys.setBackend(&out);
    const int rate = 22050;
    std::vector<int16_t> wav;
    std::vector<float> block;
    double owed = 0;
    audio::GameAudio ga(sys, vfs);
    audio::GameAudioConfig cfg;
    cfg.city = cityName;
    cfg.playerCar = "vpbug";
    audio::pinSoundClock(1792000000LL);
    ga.setup(cfg, player, &world);
    ga.setupTraffic(&ts);
    ga.setupPolice(&ps);
    ga.setupPeds(&peds);
    ga.gameReset();

    std::FILE* f = std::fopen(outPath.c_str(), "w");
    if (!f) return 1;
    std::vector<ai::PolicePlayer> pps(1);
    pps[0].car = &player;
    pps[0].boxMin = tune.bound ? tune.bound->bmin : vec3();
    pps[0].boxMax = tune.bound ? tune.bound->bmax : vec3();
    phys::Scene scene;
    phys::SceneHooks hooks;
    hooks.findRoom = [&](const vec3& p, int h) { return city.findRoom(p, h); };
    hooks.laneCars = [&](std::vector<phys::LaneCar>& v) { if (ts.active()) ts.laneCars(v); };
    hooks.makePhysical = [&](int id, const phys::Mtx& lane, float speed) { return ts.takeBody(id, lane, speed); };
    hooks.trafficImpact = [&](int id, const vec3& J, int snd) { ga.trafficImpact(id, J, snd); };
    const float F = 1.0f / 30.0f;
    const int n = phys::physicsStepsForFrame(F);
    const float dt = F / float(n), invdt = 1.0f / dt;
    const int frames = int(seconds * 30.0f);
    int echoRoom = 0, maxChase = 0, maxPlaying = 0, framesTraffic = 0, framesCop = 0, framesAmbient = 0;
    for (int fr = 0; fr < frames; ++fr) {
        const double t = fr * double(F);
        // The sound random clock follows the simulated time (a run is faster than real time).
        audio::pinSoundClock(1792000000LL + (long long)(t));
        sys.beginFrame(F);
        pd.frame(F, penv);
        player.aiControlStep(pd.carInputs());
        updateTp();
        ga.aiFrameStart(F, player.speed());
        ts.update(F, tps);
        if (peds.active()) {
            peds.update(F, pedPlayers(), &ts);
            ga.pedFrame(peds);
        }
        pps[0].room = player.collision().room;
        ps.update(F, pps, &ts, t);
        std::vector<phys::SceneMover> movers;
        phys::SceneMover pm;
        pm.kind = phys::SceneMover::CarMover;
        pm.car = &player;
        pm.player = true;
        pm.number = 1;
        pm.identity = 10001;
        movers.push_back(pm);
        ts.prePhysics();
        ts.appendRowMovers(movers);
        ps.appendMovers(movers);
        ts.appendBodyMovers(movers);
        movers.reserve(movers.size() + 64);
        for (int k = 0; k < n; ++k) scene.runSample(movers, world, dt, invdt, hooks);
        ts.afterPhysics();
        ps.afterPhysics(movers, F);
        // Listener: a chase camera 6 m behind and 2 m above the car; echo in enclosed rooms.
        const phys::M3& R = player.rotation();
        const vec3 cam = player.modelOrigin() + R.r[2] * 6.0f + vec3(0, 2, 0);
        echoRoom = city.findRoom(player.body().p, echoRoom);
        const bool echo = echoRoom > 0 && size_t(echoRoom) < city.rooms().size() && (city.rooms()[size_t(echoRoom)].flags & PsdlEnclosed);
        ga.frame(F, cam, R.r[0], echo);
        sys.endFrame();
        // What the output plays during this frame.
        owed += double(F) * rate;
        const int n = int(owed);
        owed -= n;
        block.assign(size_t(n) * 2, 0.0f);
        out.mix(block.data(), n, rate);
        for (float x : block) wav.push_back(int16_t(std::lround(std::clamp(x, -1.0f, 1.0f) * 32767.0f)));
        maxChase = std::max(maxChase, ga.chaseCount());
        maxPlaying = std::max(maxPlaying, sys.stats().playing);
        bool tr = false, cp = false, am = false;
        for (int k = 0; k < audio::Manager3D::kSlots; ++k)
            if (const audio::Source3D* s = ga.world().mgr.holder(k)) {
                if (startsWith(s->label, "traffic")) tr = true;
                if (startsWith(s->label, "cop")) cp = true;
                if (startsWith(s->label, "ambient")) am = true;
            }
        framesTraffic += tr;
        framesCop += cp;
        framesAmbient += am;
        if (fr % 30 == 0) {
            const vec3 p = player.body().p;
            std::fprintf(f, "t %6.2f (%7.1f %5.1f %7.1f) v %4.1f: %d playing, %d voices; %s\n", t, double(p.x), double(p.y), double(p.z),
                         double(player.speed()), sys.stats().playing, out.sounding(), ga.statsJson().c_str());
        }
    }
    audio::pinSoundClock(-1);
    const std::string st = ga.statsJson();
    std::fprintf(f, "end: chase count max %d, up to %d samples playing, %d voices started; frames with traffic %d, cop %d, ambient %d of %d\n",
                 maxChase, maxPlaying, out.started(), framesTraffic, framesCop, framesAmbient, frames);
    std::fclose(f);
    writeWav(outPath + ".wav", wav, rate);
    std::printf("soundrun %s: chase count max %d, up to %d playing, frames with traffic %d, cop %d, ambient %d of %d; %s\n",
                cityName.c_str(), maxChase, maxPlaying, framesTraffic, framesCop, framesAmbient, frames, st.c_str());
    return 0;
}
