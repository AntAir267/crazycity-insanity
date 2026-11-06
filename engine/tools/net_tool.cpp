// cctool netserver: the engine's own session server, headless (what `crazycity --server` runs).
// cctool netclient: a headless native player over the real network (platform/native_transport.*):
// it hosts or joins a session, readies, plays the network game with a scripted driver (no window,
// no sound) and prints what it sees of the other players' cars once a second. For checking
// cross-play with browsers and the servers.
//
//   cctool netserver [--port 8821] [--host 127.0.0.1 | --lan] [--static dir] [--name text] [--quiet]
//   cctool netclient <ws://…/net> (--host | --join CODE) [--name N] [--car vpbug] [--city london]
//                    [--seconds 60] [--start-with 2] [--relay] <archive.ar>...
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "formats/cinfo.h"
#include "model/asset_cache.h"
#include "net/net_game.h"
#include "net/remote_car.h"
#include "net/session.h"
#include "net/settings.h"
#include "physics/car.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "platform/host_server.h"
#include "platform/native_transport.h"
#include "vfs/vfs.h"

using namespace cc;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

int cmdNetServer(const std::vector<std::string>& args) { return net::serverMain(args); }

namespace {

double realNow() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

struct Player : net::NetWorld {
    const Vfs* vfs = nullptr;
    AssetCache* assets = nullptr;
    std::unique_ptr<CityData> cityData;
    phys::CollisionWorld world;
    phys::Car car;
    bool carReady = false;
    phys::Scene scene;
    phys::SceneHooks hooks;
    std::vector<phys::SceneMover> movers;
    phys::DriverControls dc;

    phys::Car& localCar() override { return car; }
    const CityData* city() const override { return cityData.get(); }
    phys::CollisionWorld* collision() override { return &world; }
    void placeLocal(const vec3& q, float theta) override { car.placeAt(q, theta); }

    bool loadCity(const std::string& raceDir, const net::Settings& st) {
        CityEntry e;
        for (const CityEntry& c : buildCityList(*vfs))
            if (iequals(c.raceDir, raceDir) && !c.testLayout) e = c;
        if (e.mapName.empty()) return false;
        cityData = std::make_unique<CityData>();
        LevelSet ls;
        ls.race = net::netRaceName(st.mode, st.race);
        ls.ferries = false;
        ls.parkedCars = net::isRaceMode(st.mode);
        ls.openBridges = iequals(e.raceDir, "london");
        std::string msg;
        if (!cityData->load(e.mapName, *vfs, *assets, msg, e.raceDir, ls)) return false;
        world.setupCity(*vfs, *cityData);
        const CityData* cd = cityData.get();
        hooks.findRoom = [cd](const vec3& p, int hint) { return cd->findRoom(p, hint); };
        return true;
    }

    // A heading with free road ahead (the first of 16 directions without a wall within 60 m).
    float freeHeading() {
        const vec3 o = car.body().p + vec3(0, 0.5f, 0);
        float best = 0, bestLen = -1;
        for (int k = 0; k < 16; ++k) {
            const float th = float(k) * 0.39269908f;
            const vec3 f(-std::sin(th), 0.0f, -std::cos(th));
            float len = 60.0f;
            for (float d = 2.0f; d <= 60.0f; d += 2.0f) {
                phys::ProbeHit hit;
                if (world.probeStatic(o + f * (d - 2.0f), o + f * d, hit) || !world.probeStatic(o + f * d, o + f * d - vec3(0, 6.0f, 0), hit)) {
                    len = d;
                    break;
                }
            }
            if (len > bestLen) {
                bestLen = len;
                best = th;
            }
        }
        return best;
    }
};

}  // namespace

int cmdNetClient(const std::vector<std::string>& argsIn) {
    std::vector<std::string> args = argsIn;
    if (args.empty()) {
        std::fprintf(stderr, "usage: cctool netclient <ws://…/net> (--host | --join CODE) [options] <archive.ar>...\n");
        return 2;
    }
    const std::string url = args[0];
    std::string code, name = "Native", car = "vpbug", city = "london", password;
    bool hosting = false, relay = false;
    double seconds = 60;
    int startWith = 2;
    std::vector<std::string> archives;
    for (size_t i = 1; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto val = [&]() { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--host") hosting = true;
        else if (a == "--join") code = val();
        else if (a == "--password") password = val();
        else if (a == "--name") name = val();
        else if (a == "--car") car = val();
        else if (a == "--city") city = val();
        else if (a == "--seconds") seconds = std::atof(val().c_str());
        else if (a == "--start-with") startWith = std::atoi(val().c_str());
        else if (a == "--relay") relay = true;
        else archives.push_back(a);
    }
    if (!hosting && code.empty()) {
        std::fprintf(stderr, "netclient: --host or --join CODE\n");
        return 2;
    }
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    AssetCache assets(vfs);
    Player pl;
    pl.vfs = &vfs;
    pl.assets = &assets;

    net::NativeNetConfig cfg;
    cfg.server = url;
    cfg.direct = !relay;
    net::NativeTransport t(cfg);
    net::Session s(&t, &vfs);
    net::RespawnCounter counter;
    s.setRespawnCounter(&counter);
    s.setRealClock(realNow);
    std::vector<std::string> names;
    for (const CarInfo& ci : buildCarList(vfs)) names.push_back(ci.baseName);
    s.setCarNames(names);
    s.setCityInstalled([&vfs](const std::string& raceDir) {
        for (const CityEntry& c : buildCityList(vfs))
            if (iequals(c.raceDir, raceDir)) return true;
        return false;
    });
    s.localRecord().name = name;
    s.localRecord().car = car;
    s.localRecord().city = city;

    net::SessionDesc desc;
    desc.name = name + "'s game";
    desc.version = net::kGameVersion;
    if (hosting) t.host(desc);
    else t.join(code, password, net::kGameVersion);

    const double t0 = realNow();
    double now = 0, lastPrint = -1, driveStart = -1;
    bool settingsSent = false, readied = false;
    int seenRemote = 0;
    float bestMove = 0;
    std::map<net::PlayerId, vec3> firstSeen;
    const float F = 1.0f / 60.0f;
    std::printf("netclient: %s %s as %s (%s)\n", hosting ? "hosting on" : ("joining " + code + " on").c_str(), url.c_str(), name.c_str(),
                relay ? "relay only" : "direct UDP to native players");
    std::fflush(stdout);
    while (now < seconds) {
        const double frameStart = realNow();
        now = frameStart - t0;
        s.pump(now);
        if (s.closed()) {
            std::printf("netclient: session closed (%s)\n", s.closedReason().c_str());
            break;
        }
        if (!s.open() && t.state() == net::NativeTransport::State::Failed) {
            std::printf("netclient: %s\n", t.error().c_str());
            break;
        }
        if (s.open() && hosting && !settingsSent) {
            net::Settings st;
            st.mode = net::ModeCruise;
            st.city = city;
            st.timeLimit = 0;
            s.setSettings(st);
            s.describe(desc);
            settingsSent = true;
            std::printf("netclient: hosting code %s (join with ?join=%s)\n", t.code().c_str(), t.code().c_str());
            std::fflush(stdout);
        }
        if (s.open() && !hosting && !readied && s.haveSettings()) {
            s.setReady(true);
            readied = true;
            if (s.canJoinRunningGame()) {
                pl.loadCity(s.settings().city, s.settings());
                s.joinRunningGame();
            }
        }
        if (hosting && !s.game() && s.playerCount() >= startWith && s.canStart()) {
            if (pl.loadCity(s.settings().city, s.settings()) && s.start()) std::printf("netclient: started the game with %d players\n", s.playerCount());
        }
        if (s.takeGameEnded()) pl.carReady = false;
        if (s.takeGameStarting()) {
            net::NetGame* g = s.game();
            if (!pl.cityData) pl.loadCity(g->settings().city, g->settings());
            pl.car.setup(phys::loadCarTune(vfs, s.localRecord().car, true));
            pl.car.damage().enabled = true;
            pl.carReady = true;
            s.publishRecord();
            g->attach(&pl);
            driveStart = now;
            std::printf("netclient: game attached (mode %d, slot %d)\n", g->mode(), g->localSlot());
            std::fflush(stdout);
        }
        for (const net::Notice& n : s.takeNotices()) {
            if (n.kind == net::Notice::Chat && !n.text) std::printf("netclient: chat %s: %s\n", n.name.c_str(), n.line.c_str());
            else if (n.text) std::printf("netclient: notice text %d %s %s\n", n.text, n.name.c_str(), n.line.c_str());
        }
        net::NetGame* g = s.game();
        if (g && g->attached()) g->frameStart(F);
        if (pl.carReady && g && g->attached()) {
            // The scripted driver: free road ahead, half throttle, a turn now and then.
            const double dt = now - driveStart;
            if (dt > 1.0 && dt < 1.05) {
                pl.car.placeAt(pl.car.body().p - pl.car.tune().sim.centerOfGravity, pl.freeHeading());
                g->announceReset();
            }
            pl.dc = phys::DriverControls();
            if (dt > 1.5) {
                const double c = std::fmod(dt, 10.0);
                pl.dc.accelerate = c < 6.0 ? 0.6f : 0.0f;
                pl.dc.brake = c >= 6.0 && c < 8.0 ? 1.0f : 0.0f;
                if (c >= 8.0 && c < 8.05) {
                    pl.car.placeAt(pl.car.body().p - pl.car.tune().sim.centerOfGravity, pl.freeHeading());
                    g->announceReset();
                }
            }
            pl.car.controlStep(pl.dc);
            if (!pl.car.drivable()) pl.car.holdNotDrivable();
            pl.movers.clear();
            phys::SceneMover m;
            m.kind = phys::SceneMover::CarMover;
            m.car = &pl.car;
            m.player = true;
            m.number = 1;
            m.identity = 10001;
            pl.movers.push_back(m);
            g->beforePhysics(F, pl.movers);
            const int n = phys::physicsStepsForFrame(F);
            for (int k = 0; k < n; ++k) pl.scene.runSample(pl.movers, pl.world, F / float(n), float(n) / F, pl.hooks);
            g->frameEnd(F);
            for (net::RemoteCar* rc : g->remoteCars()) {
                if (!rc->shown || !rc->haveFirst()) continue;
                if (!firstSeen.count(rc->id)) {
                    firstSeen[rc->id] = rc->car.body().p;
                    ++seenRemote;
                }
                const vec3 d = rc->car.body().p - firstSeen[rc->id];
                bestMove = std::max(bestMove, std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z));
            }
        }
        if (now - lastPrint >= 1.0) {
            lastPrint = now;
            std::string line = strf("netclient: t %5.1f  %s", now, t.stateName());
            if (s.open()) line += strf("  code %s  me %u%s  players %d", t.code().c_str(), unsigned(s.self()), s.isHost() ? " (host)" : "", s.playerCount());
            if (pl.carReady) {
                const vec3 p = pl.car.body().p;
                line += strf("  car (%.1f, %.1f, %.1f) %.1f m/s", double(p.x), double(p.y), double(p.z), double(pl.car.speed()));
            }
            std::printf("%s\n", line.c_str());
            for (const net::PeerLink& l : t.links())
                std::printf("netclient:     peer %u: %s, %s%s  sent %d direct / %d relayed, got %d / %d\n", unsigned(l.id),
                            l.native ? "native" : "browser", l.direct ? "direct " : "relay", l.direct ? l.path.c_str() : "", l.sentDirect,
                            l.sentRelay, l.recvDirect, l.recvRelay);
            if (g && g->attached())
                for (net::RemoteCar* rc : g->remoteCars()) {
                    const vec3 p = rc->car.body().p;
                    const net::Player* rp = s.find(rc->id);
                    std::printf("netclient:     remote car of %u (%s): %s %s at (%.1f, %.1f, %.1f) %.1f m/s\n", unsigned(rc->id),
                                rp ? rp->rec.name.c_str() : "?", rc->carName.c_str(), rc->shown ? "shown" : "hidden", double(p.x),
                                double(p.y), double(p.z), double(rc->car.speed()));
                }
            std::fflush(stdout);
        }
        const double spent = realNow() - frameStart;
        if (spent < double(F)) std::this_thread::sleep_for(std::chrono::duration<double>(double(F) - spent));
    }
    if (s.open()) s.quit();
    for (int i = 0; i < 10; ++i) {
        t.service();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::printf("netclient: done: saw %d remote car(s), the farthest moved %.1f m\n", seenRemote, double(bestMove));
    return seenRemote > 0 && bestMove > 5.0f ? 0 : 1;
}
