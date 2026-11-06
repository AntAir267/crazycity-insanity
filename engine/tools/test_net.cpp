// cctool testnet <out.txt> <archives…>
//
// Native checks of the network game (spec/game/multiplayer.md): the settings packing, the car state
// quantisation, the receiving and smoothing rules of a remote car, the send timers, the car check
// values, the start positions, and then whole sessions of two engine instances in one process (each
// its own city, physics world, car, session and game) exchanging packets through the loopback hub:
// the lobby, Cruise, a Checkpoint race and a Blitz, Cops and Robbers with the gold, leaving and the
// host vanishing. Every session is run twice and its log compared, so the result is repeatable.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "city/city_data.h"
#include "core/game_random.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/banger.h"
#include "formats/car_info.h"
#include "formats/cinfo.h"
#include "game/player_files.h"
#include "model/asset_cache.h"
#include "net/car_state.h"
#include "net/check_value.h"
#include "net/loopback.h"
#include "net/net_game.h"
#include "net/remote_car.h"
#include "net/session.h"
#include "net/settings.h"
#include "net/texts.h"
#include "net/wire.h"
#include "physics/car.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "tools/test_net_rig.h"
#include "vfs/vfs.h"

using namespace cc;

namespace {

using Expect = std::function<void(bool, const std::string&)>;

bool near(float a, float b, float eps) { return std::fabs(a - b) <= eps; }
float dist(const vec3& a, const vec3& b) {
    const vec3 d = a - b;
    return std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
}

// ---------------------------------------------------------------------------------------------
// One engine instance: its own city data, physics world, car, session and game.

struct Peer : net::NetWorld {
    std::string label;
    const Vfs* vfs = nullptr;
    AssetCache* assets = nullptr;
    net::Transport* t = nullptr;
    std::unique_ptr<net::Session> s;
    net::RespawnCounter counter;
    std::unique_ptr<CityData> cityData;
    phys::CollisionWorld world;
    phys::Car car;
    bool carReady = false;
    phys::Scene scene;
    phys::SceneHooks hooks;
    std::vector<phys::SceneMover> movers;
    double now = 0;
    phys::DriverControls dc;
    bool horn = false;
    std::vector<std::string>* log = nullptr;
    std::vector<net::Notice> notices;
    std::vector<std::string> events;
    int frames = 0;
    long long realTicks = 0;

    phys::Car& localCar() override { return car; }
    const CityData* city() const override { return cityData.get(); }
    phys::CollisionWorld* collision() override { return &world; }
    void placeLocal(const vec3& q, float theta) override { car.placeAt(q, theta); }
    bool localHorn() const override { return horn; }
    void event(const char* name) override { events.push_back(name); }

    bool loadCity(const CityEntry& e, const net::Settings& st) {
        cityData = std::make_unique<CityData>();
        LevelSet ls;
        ls.race = net::netRaceName(st.mode, st.race);  // "roam" for Cruise
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

    net::NetGame* game() { return s ? s->game() : nullptr; }

    void say(const std::string& line) {
        if (log) log->push_back(strf("%7.3f %s: %s", now, label.c_str(), line.c_str()));
    }

    void frame(float F) {
        s->pump(now);
        if (s->takeGameEnded()) {
            carReady = false;
            say("game ended");
        }
        if (s->takeGameStarting()) {
            net::NetGame* g = s->game();
            net::PlayerRecord& rec = s->localRecord();
            if (g->mode() == net::ModeCops && g->settings().subMode == net::CopsAndRobbers)
                rec.car = rec.team == 0 ? "vpcop" : "vpmustang99";
            car.setup(phys::loadCarTune(*vfs, rec.car, true));
            car.damage().enabled = true;
            carReady = true;
            s->publishRecord();
            g->attach(this);
            say(strf("game %d attached (slot %d)", g->mode(), g->localSlot()));
        }
        net::NetGame* g = game();
        if (g && g->attached()) g->frameStart(F);
        for (net::Notice& n : s->takeNotices()) {
            const std::string text = n.text ? net::netText(n.text) : std::string();
            std::string shown;
            if (n.kind == net::Notice::Chat && !n.text) shown = n.name.empty() ? n.line : n.name + ": " + n.line;
            else if (n.twoLines) shown = n.name + " / " + net::joinWords(text, n.line);
            else shown = net::joinWords(net::joinWords(n.name, text), n.line);
            say(strf("%s [%d] %s", n.kind == net::Notice::Chat ? "chat" : "hud", n.text, shown.c_str()));
            notices.push_back(n);
        }
        if (carReady) {
            car.controlStep(dc);
            if (!car.drivable()) car.holdNotDrivable();
            movers.clear();
            phys::SceneMover m;
            m.kind = phys::SceneMover::CarMover;
            m.car = &car;
            m.player = true;
            m.number = 1;
            m.identity = 10001;
            movers.push_back(m);
            if (g && g->attached()) g->beforePhysics(F, movers);
            const int n = phys::physicsStepsForFrame(F);
            const float dt = F / float(n);
            for (int k = 0; k < n; ++k) scene.runSample(movers, world, dt, 1.0f / dt, hooks);
            if (g && g->attached()) g->frameEnd(F);
        }
        now += double(F);
        ++frames;
    }

    bool sawNotice(int text) const {
        for (const net::Notice& n : notices)
            if (n.text == text) return true;
        return false;
    }
};

struct Rig {
    net::LoopbackHub hub;
    std::unique_ptr<RigNet> net;  // real sockets instead of the hub (tools/test_net_sockets.cpp)
    std::vector<std::unique_ptr<Peer>> peers;
    std::vector<std::string> log;
    const Vfs* vfs = nullptr;
    AssetCache* assets = nullptr;
    double t = 0;

    Peer& add(const std::string& label, const std::string& car, net::SessionDesc d = net::SessionDesc()) {
        auto p = std::make_unique<Peer>();
        p->label = label;
        p->vfs = vfs;
        p->assets = assets;
        p->log = &log;
        p->t = net ? net->connect(d) : hub.connect(d);
        p->s = std::make_unique<net::Session>(p->t, vfs);
        p->s->setRespawnCounter(&p->counter);
        // A real clock that moves a little at every reading (the spot seeds, 5.3), repeatably.
        Peer* raw = p.get();
        p->s->setRealClock([raw]() {
            raw->realTicks += 1;
            return raw->now + double(raw->realTicks) * 0.000137;
        });
        p->s->localRecord().name = label;
        p->s->localRecord().car = car;
        p->now = t;
        peers.push_back(std::move(p));
        return *peers.back();
    }
    void step(float F, int n = 1) {
        for (int i = 0; i < n; ++i) {
            hub.setTime(t);
            if (net) {
                net->setTime(t);
                net->beforeStep();
            }
            for (auto& p : peers) {
                p->frame(F);
                if (net) net->afterFrame(p->t);
            }
            t += double(F);
        }
    }
    void vanish(net::PlayerId id) {
        if (net) net->vanish(id);
        else hub.vanish(id);
    }
    bool serverSealed() { return net ? net->sealed() : hub.desc().sealed; }
    // Steps until `cond` holds (at most `seconds`); true if it did.
    bool until(float F, float seconds, const std::function<bool()>& cond) {
        const int n = int(seconds / F);
        for (int i = 0; i < n; ++i) {
            if (cond()) return true;
            step(F);
        }
        return cond();
    }
};

CityEntry cityEntry(const Vfs& vfs, const std::string& raceDir) {
    for (const CityEntry& c : buildCityList(vfs))
        if (iequals(c.raceDir, raceDir) && !c.testLayout) return c;
    return CityEntry();
}

// A heading (radians, 0 = north) with no wall within 70 m ahead of the peer's car, 1 m above the
// street (the first of 16 directions that is free; the longest otherwise).
float freeHeading(Peer& p) {
    const vec3 o = p.car.body().p + vec3(0, 0.5f, 0);
    float best = 0, bestLen = -1;
    for (int k = 0; k < 16; ++k) {
        const float th = float(k) * 0.39269908f;
        const vec3 f(-std::sin(th), 0.0f, -std::cos(th));
        float len = 70.0f;
        for (float d = 2.0f; d <= 70.0f; d += 2.0f) {
            phys::ProbeState ps;
            phys::ProbeHit hit;
            const vec3 a = o + f * (d - 2.0f), b = o + f * d;
            if (p.world.probeStatic(a, b, hit)) {
                len = d;
                break;
            }
            // Ground still there below (no falling off a bridge).
            if (!p.world.probeStatic(b, b - vec3(0, 6.0f, 0), hit)) {
                len = d;
                break;
            }
        }
        if (len > bestLen) {
            bestLen = len;
            best = th;
        }
        if (len >= 70.0f) break;
    }
    return best;
}

// The lobby: host with settings, guest joins, both ready, start, both cities loaded.
bool lobby(Rig& rig, Peer& host, Peer& guest, const net::Settings& st, const CityEntry& city) {
    host.s->setSettings(st);
    rig.step(1.0f / 60.0f, 3);
    guest.s->setReady(true);
    rig.step(1.0f / 60.0f, 3);
    if (!host.s->canStart()) return false;
    if (!host.loadCity(city, host.s->settings()) || !guest.loadCity(city, guest.s->settings())) return false;
    return host.s->start();
}

// ---------------------------------------------------------------------------------------------
// Unit checks

void settingsChecks(const Expect& expect) {
    net::Settings s;
    s.mode = net::ModeCheckpoint;
    s.city = "london";
    s.race = 17;
    s.timeOfDay = 2;
    s.weather = 3;
    s.skill = 1;
    s.laps = 18;
    s.timeLimit = 300.7f;
    s.pedDensity = 0.25f;
    s.noDamage = true;
    const net::Settings u = net::limited(s);
    expect(u.race == 1 && u.laps == 2 && u.timeLimit == 44.0f && u.timeOfDay == 2 && u.weather == 3 && u.skill == 1 &&
               u.noDamage && u.city == "london",
           "settings packing: race 17 → 1, laps 18 → 2, time limit 300.7 → 44 (trunc, mod 256), the rest kept");
    expect(u.pedDensity == float(63) * 0.003921569f, strf("pedestrian density 0.25 → byte 63 → %.8f", double(u.pedDensity)));
    net::Settings c;
    c.mode = net::ModeCops;
    c.noDamage = true;
    bool all = true;
    int n = 0;
    for (int sub = 0; sub < 3; ++sub)
        for (int w = 0; w < 3; ++w)
            for (int lt = 0; lt < 3; ++lt)
                for (int lc = 0; lc < 4; ++lc) {
                    c.subMode = sub;
                    c.goldWeight = w;
                    c.limitType = lt;
                    c.limitChoice = lc;
                    const net::Settings v = net::limited(c);
                    all = all && v.subMode == sub && v.goldWeight == w && v.limitType == lt && v.limitChoice == lc && !v.noDamage;
                    ++n;
                }
    expect(all && n == 108, "all 108 Cops and Robbers option bytes round-trip; Cops and Robbers clears no-damage");
    net::Settings g;
    g.goldWeight = 1;
    const float m1 = g.goldMass(), c1 = g.goldCap();
    g.goldWeight = 2;
    expect(m1 == 100.0f && c1 == 0.9f && g.goldMass() == 200.0f && g.goldCap() == 0.81f,
           "gold masses 100 / 200 kg, throttle caps 0.9 / 0.81");
    net::Settings x;
    x.mode = 6;
    expect(net::limited(x).mode == net::ModeCruise, "a Crash Course choice becomes Cruise");
    g.limitChoice = 2;
    expect(g.limitMinutes() == 20 && g.limitPoints() == 500, "limit choice 2: 20 minutes / 500 points");
}

void carStateChecks(const Vfs& vfs, const Expect& expect) {
    float len;
    int8_t d[3];
    net::encodeDirection(vec3(3, 4, 0), len, d);
    const vec3 back = net::decodeDirection(len, d);
    expect(len == 5.0f && d[0] == 76 && d[1] == 101 && d[2] == 0,
           strf("velocity (3, 4, 0): |v| 5, bytes %d %d %d (trunc of 0.6·127, 0.8·127)", d[0], d[1], d[2]));
    expect(near(back.x, 5.0f * 76.0f / 127.0f, 1e-6f) && near(back.y, 5.0f * 101.0f / 127.0f, 1e-6f),
           strf("decoded (%.4f, %.4f, %.4f): L × byte / 127", double(back.x), double(back.y), double(back.z)));
    net::encodeDirection(vec3(), len, d);
    expect(len == 0.0f && d[0] == 0 && d[1] == 0 && d[2] == 0, "a zero velocity sends 0");
    // Angles round trip.
    phys::M3 R = net::matrixFromAngles(0.3f, -0.2f, 2.5f);
    float a, b, c;
    net::anglesOf(R, a, b, c);
    const phys::M3 R2 = net::matrixFromAngles(a, b, c);
    float err = 0;
    for (int i = 0; i < 3; ++i) err = std::max(err, dist(R.r[i], R2.r[i]));
    expect(near(a, 0.3f, 1e-5f) && near(b, -0.2f, 1e-5f) && near(c, 2.5f, 1e-5f) && err < 1e-5f,
           strf("orientation angles round trip (a %.5f b %.5f c %.5f, matrix error %.2g)", double(a), double(b), double(c),
                double(err)));
    // Wire.
    net::CarState s;
    s.hostClock = 12.5f;
    s.steer = -127;
    s.throttle = 255;
    s.brake = 3;
    s.gear = 4;
    s.pos = vec3(1, 2, 3);
    s.velLen = 7;
    s.velDir[1] = -128;
    s.score = 1234;
    s.flags = net::FlagHorn | net::FlagReset;
    s.seq = 99;
    net::Writer w(net::Msg::CarState);
    net::writeCarState(w, s);
    net::Reader r(w.bytes());
    r.u8();
    net::CarState t;
    const bool ok = net::readCarState(r, t);
    expect(ok && r.atEnd() && t.hostClock == 12.5f && t.steer == -127 && t.throttle == 255 && t.gear == 4 && t.velDir[1] == -128 &&
               t.score == 1234 && t.flags == (net::FlagHorn | net::FlagReset) && t.seq == 99 && w.bytes().size() == 66,
           strf("car state on the wire: %zu bytes, every field back", w.bytes().size()));
    // Capture from a real car.
    phys::Car car;
    car.setup(phys::loadCarTune(vfs, "vpbug", true));
    car.placeAt(vec3(10, 0, 20), 0.5f);
    car.body().v = vec3(0, 0, -10);
    phys::DriverControls dc;
    dc.accelerate = 1.0f;
    dc.steer = 0.5f;
    car.controlStep(dc);
    const net::CarState cs = net::captureCarState(car);
    expect(cs.throttle == 255 && cs.steer == 63 && cs.velLen == 10.0f && cs.velDir[2] == -127 && cs.accelLen == 10.0f,
           strf("captured: throttle %d, steering %d (trunc 0.4960·127), |v| %.1f in both slots", cs.throttle, cs.steer,
                double(cs.velLen)));
}

void checkValueChecks(const Vfs& vfs, const Expect& expect) {
    const uint32_t bug = net::carCheckValue(vfs, "vpbug"), mus = net::carCheckValue(vfs, "vpmustang99"),
                   cop = net::carCheckValue(vfs, "vpcop");
    expect(bug == 0x9CE8B9A2u, strf("vpbug check value %08X (9CE8B9A2)", bug));
    expect(mus == 0x66D6DD02u, strf("vpmustang99 check value %08X (66D6DD02)", mus));
    expect(cop == mus, "vpcop sends vpmustang99's check value");
    std::set<uint32_t> seen;
    int cars = 0;
    for (const CarInfo& ci : buildCarList(vfs)) {
        if (!ci.builtIn) continue;
        BytesPtr b = vfs.open("tune/vehicle/" + ci.baseName + ".vehcarsim");  // each car's own file (vpcop's too)
        if (b) seen.insert(net::checkValueOfText(b->data(), b->size()));
        ++cars;
    }
    expect(cars == 20 && int(seen.size()) == 20, strf("the %d shipped cars have %zu distinct check values", cars, seen.size()));
    const uint8_t text[] = {'a', 'b', 0, 'c', '\n', 'd'};
    const uint8_t cut[] = {'a', 'b', 'd'};
    expect(net::checkValueOfText(text, 6) == game::mm2Crc(cut, 3), "a line is cut at a NUL byte (the rest of it skipped)");
}

void remoteCarChecks(const Vfs& vfs, const Expect& expect) {
    phys::CollisionWorld flat;
    flat.setupFlat(vfs, 0.0f);
    net::RemoteCar rc;
    rc.car.setup(phys::loadCarTune(vfs, "vpbug", true));
    rc.car.placeAt(vec3(0, 0, 0), 0.0f);
    rc.placed();
    const float radius = rc.car.bound()->radius;
    net::CarState s;
    s.pos = vec3(100, 1, 50);
    s.a = 0.0f;
    s.b = 0.0f;
    s.c = 0.0f;
    net::encodeDirection(vec3(0, 0, -10), s.velLen, s.velDir);
    s.accelLen = s.velLen;
    for (int i = 0; i < 3; ++i) s.accelDir[i] = s.velDir[i];
    s.seq = 5;
    s.damage = 0;
    bool cleared = false;
    rc.receive(s, 1.0, cleared);
    expect(rc.car.body().p.x == 100.0f && rc.car.body().p.z == 50.0f, "the first state puts the car there");
    expect(near(rc.meanInterval(), 9.1f, 1e-5f), strf("T after the first state: (9 × 10 + 1.0) / 10 = %.4f", double(rc.meanInterval())));
    const float T = rc.meanInterval();
    const float vz = (10.0f * -127.0f) / 127.0f;
    expect(near(rc.targetPos().z, (50.0f + T * vz) + (0.5f * vz) * (T * T), 1e-3f),
           strf("target z %.3f = p + T·V + 0.5·A·T² (the velocity in the acceleration slot)", double(rc.targetPos().z)));
    expect(rc.car.body().v.z == vz, "velocity set from the state");
    // An older state is dropped.
    net::CarState old = s;
    old.seq = 4;
    expect(!rc.receive(old, 1.05, cleared), "a smaller sequence number is dropped");
    // Drift test: 5 m off (more than the bound radius) → off course; 1.5 m → d = 0 → not off; 15 m keeps.
    net::CarState s2 = s;
    s2.seq = 6;
    s2.pos = vec3(105, 1, 50);
    rc.car.body().p = vec3(100, 1, 50);
    rc.receive(s2, 1.1, cleared);
    const bool off5 = rc.offCourse();
    s2.seq = 7;
    s2.pos = vec3(101.5f, 1, 50);
    rc.car.body().p = vec3(100, 1, 50);
    rc.receive(s2, 1.15, cleared);
    const bool off15 = rc.offCourse();
    s2.seq = 8;
    s2.pos = vec3(105, 1, 50);
    rc.car.body().p = vec3(100, 1, 50);
    rc.receive(s2, 1.2, cleared);
    s2.seq = 9;
    s2.pos = vec3(130, 1, 50);
    rc.car.body().p = vec3(100, 1, 50);
    rc.receive(s2, 1.25, cleared);
    const bool keep = rc.offCourse();
    expect(off5 && !off15 && keep && radius < 5.0f,
           strf("drift test (bound radius %.2f): 5 m off course, 1.5 m not (below 2 m counts as 0), 30 m keeps the mark", double(radius)));
    expect(rc.car.body().p.x == 100.0f, "the position stays at a state (only the orientation snaps)");
    // Frame counts: F_avg = F_total ÷ sequence number (whole numbers).
    net::RemoteCar fc;
    fc.car.setup(phys::loadCarTune(vfs, "vpbug", true));
    net::CarState f = s;
    for (int k = 1; k <= 3; ++k) {
        fc.countFrame();
        fc.countFrame();
        fc.countFrame();
        f.seq = uint32_t(k * 2);  // the sender numbers all its states (also those to others)
        fc.receive(f, double(k) * 0.05, cleared);
        fc.countFrame();
    }
    expect(fc.framesTotal() == 9 && fc.framesAvg() == 9 / 6, strf("frame counts: total %d, average %d (9 ÷ 6, whole numbers)",
                                                                  fc.framesTotal(), fc.framesAvg()));
    // Reset state.
    net::CarState rs = s;
    rs.seq = 20;
    rs.flags = net::FlagReset;
    rs.pos = vec3(-40, 2, 7);
    rc.receive(rs, 2.0, cleared);
    expect(rc.car.body().p.x == -40.0f && !rc.offCourse() && !rc.curve().valid && rc.targetPos().x == -40.0f,
           "a reset state snaps the car, its target, no curve, not off course");
    // Damage visuals cleared when the damage goes from above 1 to below 0.0001.
    rc.car.damage().damage = 500.0f;
    rs.seq = 21;
    rs.flags = 0;
    rs.damage = 0.0f;
    rc.receive(rs, 2.05, cleared);
    expect(cleared, "damage 500 → 0: the damage visuals cleared");
    // Smoothing: a blend by the fraction dt toward the target.
    net::RemoteCar sm;
    sm.car.setup(phys::loadCarTune(vfs, "vpbug", true));
    net::CarState a0 = s;
    a0.velLen = 0;
    a0.accelLen = 0;
    for (int i = 0; i < 3; ++i) a0.velDir[i] = a0.accelDir[i] = 0;
    a0.pos = vec3(0, 1, 0);
    a0.seq = 1;
    sm.receive(a0, 0.5, cleared);
    a0.pos = vec3(3, 1, 0);  // 3 m: within the bound radius? no: radius 2.3 < 3 → off course
    a0.seq = 2;
    sm.receive(a0, 0.55, cleared);
    const bool offNow = sm.offCourse();
    sm.countFrame();
    sm.beforePhysics(1.0f / 60.0f, nullptr);
    const float hx = sm.car.body().p.x;
    expect(offNow && near(hx, sm.curve().at(0.0f).x, 1e-5f),
           strf("off course: the position follows the curve q(s), s = F_since / F_avg (x %.4f)", double(hx)));
    // Slerp.
    const phys::M3 I = phys::M3::identity();
    const phys::M3 Q = net::matrixFromAngles(0.0f, 0.0f, 1.0f);
    const phys::M3 H = net::slerpRotation(I, Q, 0.5f);
    float ha, hb, hc;
    net::anglesOf(H, ha, hb, hc);
    expect(near(hc, 0.5f, 1e-5f), strf("rotation blend: half of 1 rad about Z is %.5f", double(hc)));
    const phys::M3 Qn = net::matrixFromAngles(0.0f, 0.0f, 3.0f), Qm = net::matrixFromAngles(0.0f, 0.0f, -3.0f);
    const phys::M3 Hn = net::slerpRotation(Qn, Qm, 0.5f);
    net::anglesOf(Hn, ha, hb, hc);
    expect(near(std::fabs(hc), 3.14159265f, 1e-4f), "the shorter way: 3 rad to −3 rad passes through π");
    // M* = M rotated about W/|W| by T·|W|.
    const phys::M3 Rz = net::rotateRows(I, vec3(0, 0, 1), 0.25f);
    net::anglesOf(Rz, ha, hb, hc);
    expect(near(hc, 0.25f, 1e-6f), "rotating the rows about +Z by 0.25 rad (right-handed)");
}

void sendTimerChecks(const Expect& expect) {
    auto period = [](float F, float I) {
        float tau = 0;
        int sends = 0, firstGap = -1, last = -1;
        for (int f = 0; f < 120; ++f) {
            if (tau > I) {
                if (last >= 0 && firstGap < 0) firstGap = f - last;
                last = f;
                tau = 0;
                ++sends;
            } else {
                tau = tau + F;
            }
        }
        return firstGap;
    };
    const int p60 = period(1.0f / 60.0f, 0.05f), p30 = period(1.0f / 30.0f, 0.05f), slow = period(1.0f / 60.0f, 0.1f);
    expect(p60 == 4 && p30 == 3, strf("car states every %d frames at 60 fps, every %d at 30 fps (0.05 s)", p60, p30));
    expect(slow == 8, strf("a slow link (0.1 s): every %d frames at 60 fps (τ passes 0.1 after 7 frame steps)", slow));
}

void positionChecks(const Vfs& vfs, AssetCache& assets, const Expect& expect) {
    expect(net::NetGame::gridOffset(0, false).x == 2.25f && net::NetGame::gridOffset(0, false).z == 6.0f &&
               net::NetGame::gridOffset(7, false).x == -4.5f && net::NetGame::gridOffset(4, true).z == 34.0f &&
               net::NetGame::gridOffset(9, true).z == 0.0f,
           "grid offsets as tabled (normal and wide)");
    // Network respawn with two players' ids and the counter.
    const CityEntry e = cityEntry(vfs, "london");
    Rig rig;
    rig.vfs = &vfs;
    rig.assets = &assets;
    Peer& a = rig.add("A", "vpbug");
    net::Settings st;
    st.city = "london";
    a.loadCity(e, st);
    rig.step(1.0f / 60.0f, 2);
    a.s->setSettings(st);
    net::Session& s = *a.s;
    net::NetGame g(s, st, false);
    g.attach(&a);
    const int k0 = a.counter.k;
    vec3 p1, p2, p3;
    g.networkRespawn(1001, p1);
    g.networkRespawn(1002, p2);
    a.counter.k = k0;
    g.networkRespawn(1001, p3);
    expect(dist(p1, p3) == 0.0f && dist(p1, p2) > 1.0f && a.counter.k == (k0 + 1) % 100,
           strf("network respawn: id 1001 at (%.1f, %.1f, %.1f), id 1002 elsewhere; same id and counter → same place", double(p1.x),
                double(p1.y), double(p1.z)));
    bool eligible = false;
    for (const CityStart& cs : a.cityData->starts())
        if (dist(cs.pos + vec3(0, 2, 0), p1) == 0.0f) eligible = true;
    expect(eligible, "the respawn is an eligible free-roam intersection + 2 m");
    g.detach();
}

// ---------------------------------------------------------------------------------------------
// Sessions

struct Scenario {
    std::string name;
    std::function<void(Rig&, const Expect&)> run;
};

void lobbyScenario(Rig& rig, const Expect& expect) {
    net::SessionDesc d;
    d.maxPlayers = 2;
    d.password = "gold";
    Peer& h = rig.add("host", "vpbug", d);
    rig.step(1.0f / 60.0f, 2);
    h.s->describe(d);
    net::SessionDesc j;
    j.password = "gold";
    Peer& g = rig.add("guest", "vpmustang99", j);
    rig.step(1.0f / 60.0f, 3);
    expect(h.s->playerCount() == 2 && g.s->playerCount() == 2 && g.s->find(h.s->self()) && g.s->find(h.s->self())->rec.host,
           "host and guest see each other; the host's record says host");
    net::Settings st;
    st.mode = net::ModeCircuit;
    st.city = "sf";
    st.race = 3;
    st.laps = 3;
    st.timeLimit = 90;
    h.s->setSettings(st);
    rig.step(1.0f / 60.0f, 2);
    expect(g.s->haveSettings() && g.s->settings().mode == net::ModeCircuit && g.s->settings().race == 3 && g.s->settings().laps == 3,
           "the guest reads the host's settings");
    expect(!h.s->canStart(), "the host cannot start before the guest is ready");
    g.s->setReady(true);
    rig.step(1.0f / 60.0f, 2);
    expect(h.s->canStart(), "everyone ready: the host can start");
    // A third machine: the session is full (2 players).
    net::SessionDesc x;
    x.password = "gold";
    Peer& third = rig.add("third", "vpbug", x);
    rig.step(1.0f / 60.0f, 2);
    expect(third.s->closed() && third.s->closedReason() == "refused: full", "a third player is refused: the session is full");
    // Chat.
    g.s->chat("hello");
    g.s->chat("/wav horn.wav");
    g.s->chat("/rc secret");
    rig.step(1.0f / 60.0f, 2);
    int lines = 0;
    for (const net::Notice& n : h.notices)
        if (n.kind == net::Notice::Chat && n.line == "hello" && n.name == "guest") ++lines;
    bool wavShown = false;
    for (const net::Notice& n : h.notices)
        if (n.line.find("/wav") != std::string::npos || n.line.find("/rc") != std::string::npos) wavShown = true;
    expect(lines == 1 && !wavShown, "chat: 'guest: hello' shown; /wav sent but not shown; /rc not sent");
    // Start: the slot table in session order.
    const net::Settings ss = h.s->settings();
    expect(h.s->start(), "the host starts");
    rig.step(1.0f / 60.0f, 2);
    const auto& t1 = h.s->slotTable();
    const auto& t2 = g.s->slotTable();
    expect(t1.size() == 10 && t1 == t2 && t1[0] == h.s->self() && t1[1] == g.s->self() && t1[2] == 0,
           "start slots: host 0, guest 1, the rest empty; both have the same table");
    (void)ss;
    // Kick.
    h.s->kick(g.s->self());
    rig.step(1.0f / 60.0f, 3);
    expect(g.s->closed() && g.sawNotice(44) && h.s->playerCount() == 1, "a kicked player sees text 44 and is out");
}

void cruiseScenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "london");
    Peer& h = rig.add("host", "vpbug");
    rig.step(1.0f / 60.0f, 2);
    Peer& g = rig.add("guest", "vpcab");
    rig.step(1.0f / 60.0f, 3);
    net::Settings st;
    st.mode = net::ModeCruise;
    st.city = "london";
    st.timeLimit = 0;
    expect(lobby(rig, h, g, st, city), "Cruise: lobby to start");
    rig.step(1.0f / 60.0f, 3);
    net::NetGame* hg = h.game();
    net::NetGame* gg = g.game();
    expect(hg && gg && hg->attached() && gg->attached(), "both games attached");
    expect(dist(h.car.body().p, g.car.body().p) > 5.0f, "the two players start at different intersections");
    expect(h.sawNotice(154) && g.sawNotice(154) && h.car.drivable(), "'Go' (text 154) on the second frame; drivable");
    // Drive the host for 6 s in a direction with free road ahead; the guest's copy follows.
    const float heading = freeHeading(h);
    h.car.placeAt(h.car.body().p - h.car.tune().sim.centerOfGravity, heading);
    rig.step(1.0f / 60.0f, 30);
    h.dc.accelerate = 1.0f;
    float worst = 0, fastest = 0;
    for (int i = 0; i < 360; ++i) {
        rig.step(1.0f / 60.0f);
        fastest = std::max(fastest, h.car.speed());
        if (i > 120) {
            net::RemoteCar* copy = gg->remoteOf(h.s->self());
            if (copy) worst = std::max(worst, dist(copy->car.body().p, h.car.body().p));
        }
    }
    net::RemoteCar* copy = gg->remoteOf(h.s->self());
    expect(copy && copy->shown && copy->haveFirst(), "the guest shows the host's car");
    expect(copy && worst < 8.0f && fastest > 10.0f,
           strf("the guest's copy stays within %.2f m of the host's car (up to %.1f m/s)", double(worst), double(fastest)));
    const int frames = 360 + 3 + 30;
    expect(hg->statesSent > 0 && near(float(hg->statesSent) / float(frames), 0.25f, 0.03f),
           strf("the host sent %d states in %d frames (every 4th frame at 60 fps)", hg->statesSent, frames));
    expect(copy && copy->framesAvg() >= 3 && copy->framesAvg() <= 4, strf("F_avg %d frames per state", copy ? copy->framesAvg() : -1));
    // A remote car is a full car: it collides with the local car. The guest parks 30 m ahead of
    // the host's car on free road; the host drives into it.
    g.dc = phys::DriverControls();
    h.dc = phys::DriverControls();
    h.dc.brake = 1.0f;
    rig.step(1.0f / 60.0f, 120);
    h.dc.brake = 0.0f;
    const float hh = freeHeading(h);
    const vec3 hp = h.car.body().p - h.car.tune().sim.centerOfGravity;
    h.car.placeAt(hp, hh);
    const vec3 f(-std::sin(hh), 0.0f, -std::cos(hh));
    g.car.placeAt(vec3(hp.x + f.x * 30.0f, hp.y + 0.5f, hp.z + f.z * 30.0f), hh);
    rig.step(1.0f / 60.0f, 60);
    h.dc.accelerate = 1.0f;
    bool hit = false;
    float total = 0;
    int identity = 0;
    for (int i = 0; i < 360 && !hit; ++i) {
        rig.step(1.0f / 60.0f);
        for (const auto& sl : g.car.damage().slots)
            if (sl.used && net::RemoteCar::isPlayerIdentity(sl.identity)) {
                hit = true;
                total = sl.total;
                identity = sl.identity;
            }
    }
    expect(hit && total > 0.0f, strf("the host's car rammed the guest's: the guest's own car counted an impact from a player's car "
                                     "(identity %d, strength %.0f; host at %.1f m/s)",
                                     identity, double(total), double(h.car.speed())));
    h.dc = phys::DriverControls();
    rig.step(1.0f / 60.0f, 60);
    // Water: the guest's reset point and a "car reset".
    net::RemoteCar* gcopy = hg->remoteOf(g.s->self());
    const vec3 before = gcopy ? gcopy->car.body().p : vec3();
    g.car.body().p.y = -60.0f;  // fell through the city
    rig.step(1.0f / 60.0f, 3);
    const vec3 after = gcopy ? gcopy->car.body().p : vec3();
    expect(gcopy && g.car.body().p.y > -10.0f && dist(after, g.car.body().p) < 3.0f && dist(before, after) > 3.0f,
           "falling through the city: the guest back at its reset point; the host's copy snapped by 'car reset'");
    // A late joiner sees both and appears to them.
    Peer& late = rig.add("late", "vpbus");
    rig.step(1.0f / 60.0f, 4);
    expect(late.s->canJoinRunningGame(), "a late joiner may join the running Cruise game");
    late.loadCity(city, late.s->settings());
    late.s->joinRunningGame();
    rig.step(1.0f / 60.0f, 30);
    expect(late.game() && late.game()->remoteCars().size() == 2 && h.sawNotice(42),
           "the late joiner's game has both remote cars; the host shows text 42 (joined)");
    expect(late.s->timeRequests >= 1 && std::fabs(late.s->clock() - h.s->clock()) < 0.2,
           strf("the late joiner asked the host for the time: clocks %.3f / %.3f", late.s->clock(), h.s->clock()));
    // Leaving.
    late.s->quit();
    rig.step(1.0f / 60.0f, 3);
    expect(h.game()->remoteCars().size() == 1 && h.sawNotice(38), "a player who quits: its car freed, text 38 on the others");
    // The host vanishes: in Cruise the guest becomes host and the game goes on.
    rig.vanish(h.s->self());
    rig.step(1.0f / 60.0f, 5);
    expect(g.s->isHost() && g.game() && g.s->localRecord().host, "the host vanished: the guest is host, the Cruise game continues");
}

// Drives the local car through the gates of its list by placing it on the target gate, turned its
// way, until the list is done (at most `upTo` gates; −1 all).
void driveGates(Rig& rig, Peer& p, int upTo = -1) {
    net::NetGame* g = p.game();
    const game::WaypointList& L = g->waypoints();
    for (int k = 0; k < 200 && !L.done() && (upTo < 0 || k < upTo); ++k) {
        const Waypoint& w = L.point(L.target());
        p.car.placeAt(w.pos + vec3(0, 0.5f, 0), w.heading * -0.017453292f);
        rig.step(1.0f / 60.0f, 2);
    }
}

void checkpointScenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "london");
    Peer& h = rig.add("host", "vpbug");
    rig.step(1.0f / 60.0f, 2);
    Peer& g = rig.add("guest", "vpmustang99");
    rig.step(1.0f / 60.0f, 3);
    net::Settings st = net::hostDefaults(*rig.vfs, net::ModeCheckpoint, "london", 0, 0);
    expect(st.laps == 1 && st.timeLimit > 0.0f, strf("Checkpoint race 0 defaults: laps 1, time limit %.0f s", double(st.timeLimit)));
    expect(lobby(rig, h, g, st, city), "Checkpoint: lobby to start");
    rig.step(1.0f / 60.0f, 2);
    net::NetGame* hg = h.game();
    net::NetGame* gg = g.game();
    expect(h.s->sealed() && rig.serverSealed(), "the host sealed the session at the start of the race");
    const vec3 slot0 = hg->gridPosition(0, false), slot1 = hg->gridPosition(1, false);
    expect(dist(h.car.body().p - h.car.tune().sim.centerOfGravity, slot0) < 0.6f && dist(g.car.body().p - g.car.tune().sim.centerOfGravity, slot1) < 0.6f,
           strf("both cars on their grid slots (%.1f m apart)", double(dist(h.car.body().p, g.car.body().p))));
    expect(!h.car.drivable() && hg->raceState() == net::NetGame::Waiting && hg->waitCount() == 1,
           "waiting: not drivable, waiting for 1 player");
    // 5 s, then "loaded", "race start", the countdown.
    rig.until(1.0f / 60.0f, 8.0f, [&]() { return hg->raceState() == net::NetGame::Racing && gg->raceState() == net::NetGame::Racing; });
    expect(hg->raceState() == net::NetGame::Racing && gg->raceState() == net::NetGame::Racing && h.car.drivable() && g.car.drivable(),
           strf("after 5 s loaded, the host's race start and 2.5 s of countdown both race (t %.2f)", rig.t));
    expect(h.sawNotice(144) && h.sawNotice(145) && h.sawNotice(146) && g.sawNotice(146), "ready, set, go: texts 144, 145, 146");
    // The host drives through its gates first, then the guest.
    driveGates(rig, h);
    expect(hg->finished() && h.sawNotice(148), "the host finished: text 148 'finished in'");
    rig.step(1.0f / 60.0f, 3);
    expect(gg->resultTable().size() == 1 && gg->resultTable()[0].id == h.s->self() && g.sawNotice(150),
           "the guest got the host's result (text 150)");
    expect(hg->finishWindow() > 59.0f, "the host's 60 s finish window runs");
    // Standings: the guest is behind.
    expect(gg->place() == 2, strf("the guest's place: %d of %d", gg->place(), gg->racers()));
    rig.step(1.0f / 60.0f, 200);
    expect(hg->raceState() == net::NetGame::Stopped, "3 s of slow-down, then the host's car is stopped");
    driveGates(rig, g);
    rig.step(1.0f / 60.0f, 5);
    expect(gg->finished() && h.sawNotice(152) && g.sawNotice(149), "the guest finished: the host shows 152, the guest its own 149");
    rig.until(1.0f / 60.0f, 2.0f, [&]() { return hg->resultsShown() && gg->resultsShown(); });
    expect(hg->resultsShown() && gg->resultsShown() && hg->resultTable().size() == 2 && hg->resultTable()[0].id == h.s->self() &&
               gg->resultTable()[1].id == g.s->self(),
           "both in the table: 'race over', the results on both (host first)");
    // Restart from the results: waiting again with "reloaded"; the cars snap back.
    const int seqBefore = int(gg->statesReceived);
    hg->restart();
    rig.step(1.0f / 60.0f, 3);
    expect(hg->raceState() == net::NetGame::Waiting && gg->raceState() == net::NetGame::Waiting && hg->resultTable().empty() &&
               gg->resultTable().empty(),
           "restart: both waiting again, the results cleared");
    net::RemoteCar* copy = gg->remoteOf(h.s->self());
    expect(copy && dist(copy->car.body().p, h.car.body().p) < 0.5f && gg->statesReceived > seqBefore,
           "the reset-flag state snapped the host's car on the guest");
    rig.until(1.0f / 60.0f, 9.0f, [&]() { return hg->raceState() == net::NetGame::Racing; });
    expect(hg->raceState() == net::NetGame::Racing, "after 'reloaded' the race starts again");
    // The finish window: only the guest finishes; 60 s later the host's own time is 86400.
    driveGates(rig, g);
    rig.until(1.0f / 60.0f, 63.0f, [&]() { return hg->resultsShown(); });
    bool dnf = false;
    for (const net::NetResult& r : hg->resultTable())
        if (r.id == h.s->self() && r.time == 86400.0f) dnf = true;
    expect(dnf && hg->resultsShown() && h.sawNotice(143), "60 s after the first finish: time up, the host 'did not finish' (86400)");
}

void circuitScenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "london");
    Peer& h = rig.add("host", "vpsemi");
    rig.step(1.0f / 60.0f, 2);
    Peer& g = rig.add("guest", "vpbug");
    rig.step(1.0f / 60.0f, 3);
    net::Settings st = net::hostDefaults(*rig.vfs, net::ModeCircuit, "london", 0, 0);
    expect(st.laps >= 2, strf("Circuit 0 defaults: %d laps, limit %.0f s", st.laps, double(st.timeLimit)));
    expect(lobby(rig, h, g, st, city), "Circuit: lobby to start");
    rig.step(1.0f / 60.0f, 2);
    net::NetGame* hg = h.game();
    net::NetGame* gg = g.game();
    // The race path set's concrete barricades: Mass and ImpulseLimit2 × 26 (1.4).
    int barricades = 0;
    bool scaled = true;
    for (const phys::CollisionObject& o : h.world.objects()) {
        if (o.kind != phys::ObjKind::Prop || !o.loose) continue;
        if (!iequals(o.loose->name, "sp_barricadeconcl_f") && !iequals(o.loose->name, "sp_barricadeconcr_f")) continue;
        BangerData bd;
        loadBangerData(*rig.vfs, o.loose->name, bd);
        ++barricades;
        scaled = scaled && near(o.impulseLimit2, bd.impulseLimit2 * 26.0f, 1e-3f * std::fabs(bd.impulseLimit2 * 26.0f)) &&
                 near(o.loose->bd.mass, bd.mass * 26.0f, 1e-3f * bd.mass * 26.0f);
    }
    expect(barricades > 0 && scaled, strf("the race path set's %d concrete barricades: Mass and ImpulseLimit2 × 26", barricades));
    // The semi tows its trailer in a race: the guest's copy of it has the trailer too (trailer.md 1).
    net::RemoteCar* semi = gg->remoteOf(h.s->self());
    expect(semi && semi->hasTrailer && semi->car.trailer(), "the guest's copy of the semi has its trailer");
    rig.until(1.0f / 60.0f, 9.0f, [&]() { return hg->raceState() == net::NetGame::Racing && gg->raceState() == net::NetGame::Racing; });
    expect(h.sawNotice(101) && h.sawNotice(103), "Circuit ready / go texts 101, 103");
    driveGates(rig, g);
    expect(gg->finished() && gg->waypoints().lap() == st.laps, strf("the guest drove %d laps and finished", gg->waypoints().lap()));
    driveGates(rig, h);
    rig.until(1.0f / 60.0f, 3.0f, [&]() { return hg->resultsShown() && gg->resultsShown(); });
    expect(hg->resultsShown() && hg->resultTable().size() == 2 && hg->resultTable()[0].id == g.s->self() && g.sawNotice(106) &&
               h.sawNotice(105),
           "results: the guest first (106 on the guest, 105 for the host's own finish)");
}

void blitzScenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "sf");
    Peer& h = rig.add("host", "vpbug");
    rig.step(1.0f / 60.0f, 2);
    Peer& g = rig.add("guest", "vpcab");
    rig.step(1.0f / 60.0f, 3);
    net::Settings st = net::hostDefaults(*rig.vfs, net::ModeBlitz, "sf", 0, 0);
    expect(st.laps == 0 && st.timeLimit >= 18.0f, strf("Blitz 0 defaults: laps 0, limit %.0f s", double(st.timeLimit)));
    expect(lobby(rig, h, g, st, city), "Blitz: lobby to start");
    rig.until(1.0f / 60.0f, 9.0f, [&]() { return h.game()->raceState() == net::NetGame::Racing; });
    net::NetGame* hg = h.game();
    net::NetGame* gg = g.game();
    expect(hg->countdown() > st.timeLimit - 0.5f && hg->countdownShown(), "the countdown starts at the time limit");
    driveGates(rig, g);
    expect(gg->finished(), "the guest finished the Blitz");
    rig.until(1.0f / 60.0f, st.timeLimit + 8.0f, [&]() { return hg->resultsShown() && gg->resultsShown(); });
    bool hostDnf = false, guestTime = false;
    for (const net::NetResult& r : hg->resultTable()) {
        if (r.id == h.s->self() && r.time == 86400.0f) hostDnf = true;
        if (r.id == g.s->self() && r.time > 0.0f && r.time < 86400.0f) guestTime = true;
    }
    expect(hostDnf && guestTime && h.sawNotice(94), "the host's countdown ran out: 'time's up' (94), 86400; the guest has its time");
    expect(gg->resultsShown() && g.sawNotice(95), "the guest saw its own result (95) and the results when its time ran out");
}

void copsScenario(Rig& rig, const Expect& expect, int subMode) {
    const CityEntry city = cityEntry(*rig.vfs, "london");
    Peer& h = rig.add("host", subMode == net::FreeForAll ? "vpcop" : "vpbug");
    rig.step(1.0f / 60.0f, 2);
    Peer& g = rig.add("guest", "vpbug");
    rig.step(1.0f / 60.0f, 3);
    if (subMode != net::FreeForAll) {
        h.s->localRecord().team = 0;
        h.s->publishRecord();
        g.s->localRecord().team = 1;
        g.s->publishRecord();
    }
    net::Settings st;
    st.mode = net::ModeCops;
    st.city = "london";
    st.subMode = subMode;
    st.goldWeight = 1;
    st.limitType = net::LimitPoints;
    st.limitChoice = 0;  // 100 points
    expect(lobby(rig, h, g, st, city), strf("Cops and Robbers (sub-mode %d): lobby to start", subMode));
    rig.step(1.0f / 60.0f, 4);
    net::NetGame* hg = h.game();
    net::NetGame* gg = g.game();
    expect(hg->spots().size() == 45 || hg->spots().size() == 46, strf("London multicopwaypoints: %zu spots kept", hg->spots().size()));
    expect(hg->basesPlaced() && gg->basesPlaced() && dist(hg->goldPoint(), gg->goldPoint()) < 0.01f &&
               dist(hg->bankPoint(), gg->bankPoint()) < 0.01f && dist(hg->hideoutPoint(), gg->hideoutPoint()) < 0.01f,
           "the host placed gold, bank and hideout; the guest took them from the state reply");
    expect(h.sawNotice(113) && g.sawNotice(113) && g.car.drivable(), "'go' (text 113) on both after the state reply");
    if (subMode == net::FreeForAll)
        expect(hg->localTeam() == 0 && gg->localTeam() == 1 && hg->teamOf(g.s->self()) == 1 && gg->teamOf(h.s->self()) == 0,
               "free-for-all: the vpcop driver is team 0 (bank), the others team 1 (hideout)");
    if (subMode == net::CopsAndRobbers)
        expect(h.car.tune().baseName == "vpcop" || iequals(h.s->localRecord().car, "vpcop"),
               "cops and robbers: the cop drives vpcop, the robber vpmustang99");
    // The guest drives onto the gold.
    const vec3 gold = gg->goldPoint();
    g.car.placeAt(gold + vec3(1.0f, 0.5f, 0), 0.0f);
    rig.step(1.0f / 60.0f, 20);
    expect(gg->localCarries() && hg->goldCarrier() == g.s->self() && h.sawNotice(133) && g.sawNotice(134),
           "the guest drove onto the gold: request, the host names it the carrier (133 / 134)");
    expect(gg->points() == 25 && hg->scoringEvents() == 1, strf("+25 points (guest %d); the host counted 1 scoring event", gg->points()));
    expect(near(g.car.mass(), g.car.tune().sim.mass + 100.0f, 0.01f) && near(gg->remoteOf(h.s->self()) ? 1.0f : 1.0f, 1.0f, 0.0f) &&
               hg->remoteOf(g.s->self()) && near(hg->remoteOf(g.s->self())->car.mass(), g.car.tune().sim.mass + 100.0f, 0.01f),
           strf("the carrier's mass + 100 kg on both machines (%.0f kg)", double(g.car.mass())));
    g.dc.accelerate = 1.0f;
    rig.step(1.0f / 60.0f, 30);
    expect(g.car.gearIndex() >= 2 && near(g.car.inputs().throttle, 0.9f, 1e-6f),
           strf("the carrier's throttle capped at 0.9 (%.4f)", double(g.car.inputs().throttle)));
    g.dc = phys::DriverControls();
    // Knocked loose: the host rams the carrier at speed.
    rig.step(1.0f / 60.0f, 30);
    // The carrier at the first free-roam start (an open intersection); the host 25 m away on free
    // road, facing it.
    const vec3 open = g.cityData->starts().front().pos + vec3(0, 0.6f, 0);
    g.car.placeAt(open, 0.0f);
    gg->announceReset();
    rig.step(1.0f / 60.0f, 60);
    const float fh = freeHeading(g);
    const vec3 gp = g.car.body().p - g.car.tune().sim.centerOfGravity;
    const vec3 fd(-std::sin(fh), 0.0f, -std::cos(fh));
    h.car.placeAt(vec3(gp.x + fd.x * 25.0f, gp.y + 0.5f, gp.z + fd.z * 25.0f), fh + 3.14159265f);
    hg->announceReset();
    rig.step(1.0f / 60.0f, 40);
    h.dc.accelerate = 1.0f;
    bool loose = false;
    for (int i = 0; i < 300 && !loose; ++i) {
        rig.step(1.0f / 60.0f);
        loose = !gg->localCarries();
        if (loose) {
            // The host drives off (it would take the dropped gold itself otherwise).
            h.car.placeAt(h.car.body().p - h.car.tune().sim.centerOfGravity + vec3(0, 0.5f, 0) + fd * 60.0f, fh);
            hg->announceReset();
        }
    }
    h.dc = phys::DriverControls();
    rig.step(1.0f / 60.0f, 5);
    expect(loose && g.sawNotice(112) && hg->goldCarrier() == 0 && h.sawNotice(136) && near(g.car.mass(), g.car.tune().sim.mass, 0.01f),
           "the host's car knocked the gold loose (112); dropped (136 on the host); the mass restored");
    // The guest picks it up again after the 2 s pause and delivers it to its base.
    rig.step(1.0f / 60.0f, 130);
    g.car.placeAt(gg->goldPoint() + vec3(1.0f, 0.5f, 0), 0.0f);
    rig.step(1.0f / 60.0f, 20);
    expect(gg->localCarries() && gg->points() == 50, strf("picked up again: %d points", gg->points()));
    const vec3 base = gg->localTeam() == 1 ? gg->hideoutPoint() : gg->bankPoint();
    const vec3 oldGold = hg->goldHome();
    g.car.placeAt(base + vec3(2.0f, 0.5f, 0), 0.0f);
    rig.step(1.0f / 60.0f, 20);
    expect(!gg->localCarries() && gg->points() >= 150 && (g.sawNotice(116) || g.sawNotice(117)) && h.sawNotice(137),
           strf("delivered to its base: +100 (%d points), text 116/117, the host 137", gg->points()));
    expect(dist(hg->goldHome(), gg->goldHome()) < 0.01f && dist(oldGold, hg->goldHome()) > 0.01f,
           "the host placed new gold and bases; the guest has the same");
    // The point limit (100): the guest has 150 → game over.
    rig.step(1.0f / 60.0f, 5);
    expect(hg->gameOver() && gg->gameOver() && (h.sawNotice(120) || h.sawNotice(121)) && g.sawNotice(131),
           "point limit 100 reached: the host 120/121, the guest 131");
    rig.step(1.0f / 60.0f, 200);
    expect(hg->resultsShown() && gg->resultsShown(), "3 s later the results");
    const auto list = hg->resultsList();
    if (subMode == net::FreeForAll) {
        expect(list.size() == 2 && list[0].id == g.s->self() && list[0].points == gg->points(), "results: the guest first by points");
    } else {
        expect(list.size() == 4 && list[0].team == 1 && list[1].team == 0 && list[2].id == g.s->self(),
               strf("results: the winning team (team 1, text %d) first, then the players", list.empty() ? 0 : list[0].teamText));
    }
    // Restart: points 0, new respawn, the gold and bases stay.
    const vec3 home = hg->goldHome();
    hg->restart();
    rig.step(1.0f / 60.0f, 5);
    expect(hg->points() == 0 && gg->points() == 0 && !hg->gameOver() && !gg->gameOver() && dist(home, gg->goldHome()) < 0.01f &&
               !h.s->sealed(),
           "restart: points 0, unsealed, gold and bases where they were");
    if (subMode == net::FreeForAll) {
        // The host vanishes in Cops and Robbers: everyone quits with text 40.
        rig.vanish(h.s->self());
        rig.step(1.0f / 60.0f, 6);
        expect(g.s->closed() && g.sawNotice(40), "the host vanished: the guest shows text 40 and quits");
    }
}

void copsTimeScenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "sf");
    Peer& h = rig.add("host", "vpbug");
    rig.step(1.0f / 30.0f, 2);
    Peer& g = rig.add("guest", "vpbug");
    rig.step(1.0f / 30.0f, 3);
    net::Settings st;
    st.mode = net::ModeCops;
    st.city = "sf";
    st.subMode = net::FreeForAll;
    st.limitType = net::LimitTime;
    st.limitChoice = 0;  // 5 minutes
    expect(lobby(rig, h, g, st, city), "timed Cops and Robbers: lobby to start");
    rig.step(1.0f / 30.0f, 4);
    expect(h.game()->copsCountdown() > 299.0f && !h.sawNotice(141), "the host's countdown at 5 minutes; no '5 minutes left' (141: not below the limit)");
    rig.until(1.0f / 30.0f, 305.0f, [&]() { return g.game() && g.game()->gameOver(); });
    int w142 = 0, wOther = 0;
    for (const net::Notice& n : g.notices) {
        if (n.text == 142) ++w142;
        if (n.text >= 138 && n.text <= 141) ++wOther;
    }
    expect(w142 == 1 && wOther == 0 && h.sawNotice(142), "5 minutes: only '1 minute left' (142), once, on both");
    expect(h.sawNotice(118) && g.sawNotice(130) && g.game()->gameOver(), "time up: the host 118, the guest 130");
    expect(std::fabs(g.game()->hostClock() - h.game()->copsCountdown()) < 1.0f, "the host's clock rode in its car states");
}

void copsTime10Scenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "sf");
    Peer& h = rig.add("host", "vpbug");
    rig.step(1.0f / 30.0f, 2);
    Peer& g = rig.add("guest", "vpbug");
    rig.step(1.0f / 30.0f, 3);
    net::Settings st;
    st.mode = net::ModeCops;
    st.city = "sf";
    st.limitType = net::LimitTime;
    st.limitChoice = 1;  // 10 minutes
    expect(lobby(rig, h, g, st, city), "10-minute Cops and Robbers: lobby to start");
    rig.until(1.0f / 30.0f, 605.0f, [&]() { return g.game() && g.game()->gameOver(); });
    std::vector<int> seen;
    for (const net::Notice& n : g.notices)
        if (n.text >= 138 && n.text <= 142) seen.push_back(n.text);
    expect(seen == std::vector<int>{141, 142} && g.sawNotice(130), "10 minutes: warnings 141, 142 (5 and 1 minutes left), then 130");
}

// The driver's locks (1.3 step 1) and a car name the receiving machine does not know.
void locksScenario(Rig& rig, const Expect& expect) {
    const CityEntry city = cityEntry(*rig.vfs, "london");
    Peer& h = rig.add("host", "vpbug");
    std::vector<std::string> names;
    for (const CarInfo& ci : buildCarList(*rig.vfs)) names.push_back(ci.baseName);
    h.s->setCarNames(names);
    rig.step(1.0f / 60.0f, 2);
    Peer& g = rig.add("guest", "vpmustang99");
    // The guest's driver has vpmustang99 locked and vpbug's paint 2.
    g.s->setLockQuery([](const std::string& car, int paint) { return car == "vpmustang99" ? 1 : (car == "vpbug" && paint == 2) ? 2 : 0; });
    g.s->setCarNames(names);
    rig.step(1.0f / 60.0f, 3);
    expect(g.s->localRecord().car == "vpbug" && g.s->localRecord().paint == 0 && h.s->find(g.s->self())->rec.car == "vpbug",
           "entering multiplayer: a locked car becomes vpbug with paint 0 (and is published)");
    g.s->localRecord().paint = 2;
    g.s->applyLocks(false);
    const bool keptOutsideEntering = g.s->localRecord().paint == 2;
    g.s->applyLocks(true);  // entering the lobby screen again
    rig.step(1.0f / 60.0f, 2);
    expect(keptOutsideEntering && g.s->localRecord().car == "vpbug" && g.s->localRecord().paint == 0,
           "a locked paint becomes paint 0 on entering the lobby (not at other moments)");
    net::Settings st;
    st.mode = net::ModeCops;
    st.city = "london";
    st.subMode = net::CopsAndRobbers;
    g.s->localRecord().team = 1;
    g.s->publishRecord();
    h.s->setSettings(st);
    rig.step(1.0f / 60.0f, 3);
    expect(g.s->localRecord().car == "vpmustang99" && g.s->localRecord().paint == 0 && h.s->localRecord().car == "vpmustang99",
           "settings for cops and robbers: the team's car (robbers vpmustang99, also over the driver's lock), paint 0");
    st.mode = net::ModeCruise;
    h.s->setSettings(st);
    rig.step(1.0f / 60.0f, 3);
    expect(g.s->localRecord().car == "vpmustang99", "settings leaving Cops and Robbers keep the car (the driver's locks apply again later)");
    // A third player with a car the others do not have: built as vpcoop, marked a cheater.
    Peer& x = rig.add("odd", "vpNoSuchCar");
    rig.step(1.0f / 60.0f, 4);
    x.s->setReady(true);
    g.s->setReady(true);
    rig.step(1.0f / 60.0f, 3);
    for (Peer* p : {&h, &g, &x}) p->loadCity(city, h.s->settings());
    expect(h.s->start(), "Cruise started with the three");
    rig.step(1.0f / 60.0f, 30);
    net::RemoteCar* oddCopy = h.game() ? h.game()->remoteOf(x.s->self()) : nullptr;
    const net::Player* xp = h.s->find(x.s->self());
    expect(oddCopy && oddCopy->carName == "vpcoop" && oddCopy->sentCar == "vpNoSuchCar",
           "an unknown car name is built as vpcoop with the sent paint");
    expect(xp && xp->cheater && h.sawNotice(43), "the host compares vpcoop's check value: the player is marked (43)");
}

}  // namespace

void testNet(const Vfs& vfs, const std::string& outPath, const Expect& expect) {
    setLogEcho(false);
    AssetCache assets(vfs);
    std::printf("network: settings\n");
    settingsChecks(expect);
    std::printf("network: car states\n");
    carStateChecks(vfs, expect);
    std::printf("network: check values\n");
    checkValueChecks(vfs, expect);
    std::printf("network: remote cars\n");
    remoteCarChecks(vfs, expect);
    std::printf("network: send timers\n");
    sendTimerChecks(expect);
    std::printf("network: start positions\n");
    positionChecks(vfs, assets, expect);

    const std::vector<Scenario> scenarios = {
        {"lobby", lobbyScenario},
        {"cruise", cruiseScenario},
        {"checkpoint", checkpointScenario},
        {"circuit", circuitScenario},
        {"blitz", blitzScenario},
        {"cops free-for-all", [](Rig& r, const Expect& e) { copsScenario(r, e, net::FreeForAll); }},
        {"cops and robbers", [](Rig& r, const Expect& e) { copsScenario(r, e, net::CopsAndRobbers); }},
        {"robber teams", [](Rig& r, const Expect& e) { copsScenario(r, e, net::RobberTeams); }},
        {"cops timed", copsTimeScenario},
        {"cops timed 10 minutes", copsTime10Scenario},
        {"locks", locksScenario},
    };
    std::FILE* out = outPath.empty() ? nullptr : std::fopen(outPath.c_str(), "w");
    std::map<std::string, std::vector<std::string>> loopbackLogs;
    for (const Scenario& sc : scenarios) {
        std::printf("network session: %s\n", sc.name.c_str());
        std::vector<std::string> logs[2];
        for (int run = 0; run < 2; ++run) {
            resetGameRandom(1);
            phys::resetBumpRandom();
            Rig rig;
            rig.vfs = &vfs;
            rig.assets = &assets;
            int fails = 0;
            const Expect quiet = [&](bool c, const std::string& w) {
                if (!c) ++fails;
                if (run == 0) expect(c, w);
                rig.log.push_back(strf("check %s: %s", c ? "ok" : "FAIL", w.c_str()));
            };
            sc.run(rig, quiet);
            logs[run] = rig.log;
        }
        expect(logs[0] == logs[1], strf("%s: two runs give the same log (%zu lines)", sc.name.c_str(), logs[0].size()));
        if (out) {
            std::fprintf(out, "== %s\n", sc.name.c_str());
            for (const std::string& l : logs[0]) std::fprintf(out, "%s\n", l.c_str());
        }
        loopbackLogs[sc.name] = logs[0];
    }
    if (out) std::fclose(out);

    // ---- Over real local sockets (tools/test_net_sockets.cpp): the socket-level checks, then the
    // same sessions with native transports through the engine's own session server (direct UDP
    // paths, or the relay only), through Node's server/signal.js, and with car states lost and
    // late on the direct path. Their logs go to <out>_sockets.txt.
    std::string buildDir = "build";
    const size_t slash = outPath.rfind('/');
    if (slash != std::string::npos) buildDir = outPath.substr(0, slash);
    testNetSocketUnits(expect, buildDir);
    struct Backend {
        std::string name;
        SocketNetOptions o;
        bool sameAsLoopback = true;
        std::set<std::string> only;
    };
    std::vector<Backend> backends;
    {
        Backend b;
        b.name = "engine server, direct UDP";
        backends.push_back(b);
        b.name = "engine server, relay only";
        b.o.direct = false;
        backends.push_back(b);
    }
    if (haveNode()) {
        Backend b;
        b.name = "server/signal.js, direct UDP";
        b.o.server = SocketNetOptions::Node;
        b.o.logPath = buildDir + "/net_node_server.log";
        backends.push_back(b);
    } else {
        expect(true, "Node not found: the sessions through server/signal.js skipped");
    }
    {
        Backend b;
        b.name = "engine server, direct UDP losing 25 % of the car states, up to 60 ms late";
        b.o.faults.unrelLoss = 0.25f;
        b.o.faults.jitter = 0.06;
        b.sameAsLoopback = false;
        b.only = {"cruise", "cops free-for-all"};
        backends.push_back(b);
    }
    std::string sockPath = outPath.empty() ? std::string() : outPath;
    if (endsWith(sockPath, ".txt")) sockPath = sockPath.substr(0, sockPath.size() - 4);
    if (!sockPath.empty()) sockPath += "_sockets.txt";
    std::FILE* so = sockPath.empty() ? nullptr : std::fopen(sockPath.c_str(), "w");
    // For development: CCI_NETTEST_ONLY="text" runs only the backends and scenarios whose names
    // contain it.
    const char* onlyEnv = std::getenv("CCI_NETTEST_ONLY");
    const std::string only = onlyEnv ? onlyEnv : "";
    for (const Backend& b : backends) {
        std::printf("network over sockets: %s\n", b.name.c_str());
        if (so) std::fprintf(so, "#### %s\n", b.name.c_str());
        for (const Scenario& sc : scenarios) {
            if (!b.only.empty() && !b.only.count(sc.name)) continue;
            if (!only.empty() && b.name.find(only) == std::string::npos && sc.name.find(only) == std::string::npos) continue;
            std::vector<std::string> logs[2];
            std::string sums[2];
            int fails = 0, checks = 0, stalls = 0;
            bool started = true;
            for (int run = 0; run < 2 && started; ++run) {
                resetGameRandom(1);
                phys::resetBumpRandom();
                Rig rig;
                rig.vfs = &vfs;
                rig.assets = &assets;
                std::string err;
                rig.net = makeSocketNet(b.o, &err);
                if (!rig.net) {
                    expect(false, b.name + ": the network starts (" + err + ")");
                    started = false;
                    break;
                }
                const Expect quiet = [&](bool c, const std::string& w) {
                    if (run == 0) {
                        ++checks;
                        if (!c) {
                            ++fails;
                            expect(false, b.name + ": " + sc.name + ": " + w);
                        }
                    }
                    rig.log.push_back(strf("check %s: %s", c ? "ok" : "FAIL", w.c_str()));
                };
                sc.run(rig, quiet);
                logs[run] = rig.log;
                sums[run] = rig.net->summary();
                stalls += rig.net->stalls();
                rig.peers.clear();
            }
            if (!started) break;
            // The game logs must match run for run. The transport's counters too when the server
            // runs in this process; with signal.js in its own process the first messages can take
            // the relay or the just-found direct path a frame apart (the game log is the same).
            const bool sameLog = logs[0] == logs[1];
            const bool sameSums = sums[0] == sums[1];
            const bool sumsMatter = b.o.server == SocketNetOptions::Engine;
            const bool asLoopback = logs[0] == loopbackLogs[sc.name];
            expect(fails == 0 && sameLog && (sameSums || !sumsMatter) && stalls == 0 && (!b.sameAsLoopback || asLoopback),
                   strf("%s: %s: %d checks pass; two runs give %s%s%s (%s)", b.name.c_str(), sc.name.c_str(), checks,
                        !sameLog ? "DIFFERENT logs" : sameSums ? "the same log and counters" : sumsMatter ? "the same log, DIFFERENT counters" : "the same log",
                        b.sameAsLoopback ? (asLoopback ? ", the same log as over the loopback hub" : ", NOT the loopback hub's log") : "",
                        stalls ? strf(", %d barrier stalls", stalls).c_str() : "", sums[0].c_str()));
            if (so) {
                std::fprintf(so, "== %s\n", sc.name.c_str());
                for (const std::string& l : logs[0]) std::fprintf(so, "%s\n", l.c_str());
                std::fprintf(so, "-- %s\n", sums[0].c_str());
            }
        }
    }
    if (so) std::fclose(so);
}
