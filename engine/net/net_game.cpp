#include "net/net_game.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "formats/psdl.h"
#include "formats/race_files.h"
#include "game/race.h"
#include "game/race_catalogue.h"
#include "net/session.h"
#include "net/texts.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/scene.h"
#include "physics/trailer.h"
#include "vfs/vfs.h"

namespace cc {
namespace net {

namespace {

constexpr float kDnf = 86400.0f;  // "did not finish" (5.2)

float dist2(const vec3& a, const vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return (dx * dx + dy * dy) + dz * dz;
}

game::RaceMode gameModeOf(int mode) {
    switch (mode) {
        case ModeCheckpoint: return game::RaceMode::Checkpoint;
        case ModeCircuit: return game::RaceMode::Circuit;
        case ModeBlitz: return game::RaceMode::Blitz;
        default: return game::RaceMode::Cruise;
    }
}

int infoFlags(const Vfs* vfs, const std::string& car) {
    if (!vfs) return 0;
    BytesPtr b = vfs->open("tune/" + car + ".info");
    if (!b) return 0;
    return std::atoi(parseInfoFile(std::string(b->begin(), b->end())).get("Flags").c_str());
}

// Mode texts of 5.2 by mode: ready, set, go, penalty, own finish (host), own finish (result),
// another's finish (result), another's report (host), race over (own), race over (time up).
struct RaceTexts {
    int ready, set, go, penalty, ownHost, ownResult, other, report, over, timeUp;
};
RaceTexts raceTexts(int mode) {
    switch (mode) {
        case ModeCircuit: return {101, 102, 103, 104, 105, 106, 107, 110, 100, 111};
        case ModeBlitz: return {89, 90, 91, 92, 93, 95, 96, 99, 94, 97};
        default: return {144, 145, 146, 147, 148, 149, 150, 152, 143, 153};
    }
}

}  // namespace

// The local car's counted impacts (5.3 "Knocked loose").
struct NetGame::ImpactHook : phys::Car::CountedImpactHook {
    NetGame* g = nullptr;
    void countedImpact(const phys::Car& car, int otherIdentity, float slotTotal) override {
        (void)car;
        if (g->mode_ != ModeCops || !g->localCarries()) return;
        if (RemoteCar::isPlayerIdentity(otherIdentity) && slotTotal >= 250.0f) g->knockPending_ = true;
    }
};

NetGame::NetGame(Session& s, const Settings& st, bool lateJoin) : s_(s), st_(st), mode_(st.mode), lateJoin_(lateJoin) {
    remotes_.resize(8);
    impactHook_ = std::make_unique<ImpactHook>();
    impactHook_->g = this;
}

NetGame::~NetGame() { detach(); }

void NetGame::detach() {
    if (!world_) return;
    for (auto& rc : remotes_)
        if (rc) world_->remoteCarRemoved(*rc);
    phys::Car& c = world_->localCar();
    c.setCountedImpactHook(nullptr);
    c.setNetworkControls(false);
    c.setExtraMass(0.0f);
    world_ = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Inbox

void NetGame::queueMessage(PlayerId from, const std::vector<uint8_t>& data) {
    Incoming in;
    in.kind = Incoming::Message;
    in.from = from;
    in.data = data;
    inbox_.push_back(std::move(in));
}

void NetGame::queueLeft(PlayerId id, const std::string& name) {
    Incoming in;
    in.kind = Incoming::Left;
    in.from = id;
    in.name = name;
    inbox_.push_back(std::move(in));
}

void NetGame::queueHostChanged(PlayerId host) {
    Incoming in;
    in.kind = Incoming::HostChanged;
    in.from = host;
    inbox_.push_back(std::move(in));
}

void NetGame::handle(const Incoming& in) {
    switch (in.kind) {
        case Incoming::Left: onLeft(in.from, in.name); return;
        case Incoming::HostChanged: onHostChanged(in.from); return;
        case Incoming::Message: {
            Reader r(in.data);
            const Msg type = Msg(r.u8());
            if (type == Msg::CarState) onCarState(in.from, r);
            else onMessage(in.from, type, r);
            return;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Remote cars

std::vector<RemoteCar*> NetGame::remoteCars() const {
    std::vector<RemoteCar*> v;
    for (const auto& rc : remotes_)
        if (rc) v.push_back(rc.get());
    return v;
}

RemoteCar* NetGame::remoteOf(PlayerId id) const {
    for (const auto& rc : remotes_)
        if (rc && rc->id == id) return rc.get();
    return nullptr;
}

bool NetGame::wideCar(const phys::Car& c, bool trailer) const {
    // 2.2: a bound radius above 6.0 or a trailer.
    return trailer || (c.bound() && c.bound()->radius > 6.0f);
}

RemoteCar* NetGame::addRemote(PlayerId id) {
    const Player* p = s_.find(id);
    if (!p || !p->haveRecord || !s_.vfs()) return nullptr;
    int slot = -1;
    for (int i = 0; i < 8; ++i)
        if (!remotes_[size_t(i)]) {
            slot = i;
            break;
        }
    if (slot < 0) return nullptr;  // at most 8 remote cars
    auto rc = std::make_unique<RemoteCar>();
    rc->id = id;
    rc->slot = slot;
    rc->sentCar = p->rec.car;
    // A name this machine's car list does not have is built as vpcoop with the sent paint (1.3).
    rc->carName = s_.knownCar(p->rec.car) ? p->rec.car : std::string("vpcoop");
    rc->paint = p->rec.paint;
    // A remote vpcop takes its handling from vpmustang99 like the player's own (1.4).
    phys::CarTune tune = phys::loadCarTune(*s_.vfs(), rc->carName, true);
    for (int i = 0; i < 4; ++i)
        if (!tune.wheels[i].found) {
            logWarn("network: %s cannot be driven (no wheel pivot %d); player %u has no car", rc->carName.c_str(), i,
                    unsigned(id));
            return nullptr;
        }
    rc->car.setup(tune);
    // Trailers: none in Cruise and Cops and Robbers; full trailers in the race modes (trailer.md 1).
    if (isRaceMode(mode_) && world_ && world_->collision()) {
        phys::TrailerTune tt;
        std::string why;
        if (phys::loadTrailerTune(*s_.vfs(), rc->car.tune(), tt, why)) {
            auto t = std::make_unique<phys::Trailer>();
            t->setup(tt, rc->car, world_->collision(), false);
            t->identity = RemoteCar::kIdentityBase + 500 + slot;
            rc->car.attachTrailer(std::move(t));
            rc->hasTrailer = true;
        }
    }
    rc->car.damage().enabled = !(isRaceMode(mode_) || st_.noDamage);
    // Placeholders until the first state (2.2, last paragraph).
    if (mode_ == ModeCops) {
        rc->car.placeAt(vec3(0, 50, 0), 0.0f);
    } else if (isRaceMode(mode_)) {
        int startSlot = -1;
        const auto& table = s_.slotTable();
        for (size_t i = 0; i < table.size(); ++i)
            if (table[i] == id) startSlot = int(i);
        const vec3 q = gridPosition(startSlot < 0 ? 8 : startSlot, wideCar(rc->car, rc->hasTrailer));
        rc->car.placeAt(q, theta_);
    } else {
        const vec3 o = gridOffset(slot, false);
        rc->car.placeAt(vec3(o.x, 0.0f, o.z), 0.0f);
    }
    rc->placed();
    RemoteCar* raw = rc.get();
    remotes_[size_t(slot)] = std::move(rc);
    if (world_) world_->remoteCarAdded(*raw);
    return raw;
}

void NetGame::removeRemote(PlayerId id) {
    for (auto& rc : remotes_)
        if (rc && rc->id == id) {
            if (world_) world_->remoteCarRemoved(*rc);
            rc.reset();
        }
}

void NetGame::syncRemotes() {
    // A remote car for every other player of the session (a changed car is made again).
    for (auto& rc : remotes_) {
        if (!rc) continue;
        const Player* p = s_.find(rc->id);
        if (!p) {
            removeRemote(rc->id);
            continue;
        }
        if (p->haveRecord && p->rec.car != rc->sentCar) {
            const bool shown = rc->shown;
            const PlayerId id = rc->id;
            removeRemote(id);
            if (RemoteCar* n = addRemote(id)) {
                if (shown) showRemote(*n, false);
            }
        }
    }
    for (const Player& p : s_.players()) {
        if (p.id == s_.self() || !p.haveRecord) continue;
        RemoteCar* rc = remoteOf(p.id);
        if (!rc) rc = addRemote(p.id);
        if (!rc) continue;
        // Cruise and Cops and Robbers: players in the game are shown (5.1 step 1; the host, who
        // sends no "joined" in Cops and Robbers, by its record).
        if ((mode_ == ModeCruise || mode_ == ModeCops) && p.rec.inGame && !rc->shown) showRemote(*rc, false);
    }
}

void NetGame::showRemote(RemoteCar& rc, bool atRandomIntersection) {
    if (rc.shown) return;
    rc.shown = true;
    if (atRandomIntersection && !rc.haveFirst() && world_ && world_->city()) {
        // 5.1 step 5: a random intersection of the receiver's choosing until its first state.
        const auto& starts = world_->city()->starts();
        if (!starts.empty()) {
            const size_t i = size_t(clockRng_.next15()) % starts.size();
            rc.car.placeAt(starts[i].pos + vec3(0, 2.0f, 0), 0.0f);
            rc.placed();
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Positions

vec3 NetGame::gridOffset(int slot, bool wide) {
    static const float normal[10][2] = {{2.25f, 6}, {-2.25f, 6}, {4.5f, 0}, {0, 0}, {-4.5f, 0},
                                        {4.5f, -6}, {0, -6}, {-4.5f, -6}, {0, 0}, {0, 0}};
    static const float wideT[10][2] = {{2.75f, 16}, {-2.75f, 16}, {5.5f, 16}, {0, 16}, {2.75f, 34},
                                       {-2.75f, 34}, {0, 34}, {5.5f, 34}, {0, 0}, {0, 0}};
    if (slot < 0 || slot > 9) slot = 8;
    const float* o = wide ? wideT[slot] : normal[slot];
    return vec3(o[0], 0.0f, o[1]);
}

vec3 NetGame::gridPosition(int slot, bool wide) const {
    const vec3 o = gridOffset(slot, wide);
    const float c = phys::fcos(theta_), s = phys::fsin(theta_);
    const vec3 X(c, 0.0f, -s), Z(s, 0.0f, c);
    vec3 q(S_.x + o.x * X.x + o.z * Z.x, S_.y, S_.z + o.x * X.z + o.z * Z.z);
    return dropToGround(q, 7.5f, 15.0f);
}

vec3 NetGame::dropToGround(const vec3& p, float up, float down) const {
    if (!world_ || !world_->collision()) return p;
    phys::ProbeState ps;
    phys::ProbeHit hit;
    if (world_->collision()->probe(vec3(p.x, p.y + up, p.z), vec3(p.x, p.y - down, p.z), ps, hit)) return hit.point;
    return p;
}

int NetGame::roomOf(const vec3& p) const {
    const CityData* c = world_ ? world_->city() : nullptr;
    return c ? c->findRoom(p, 0) : 0;
}

bool NetGame::networkRespawn(PlayerId id, vec3& pos) {
    const CityData* c = world_ ? world_->city() : nullptr;
    pos = vec3(0, 20, 0);
    if (!c || !c->hasAiNetwork() || c->intersections().size() < 2 || c->starts().empty()) return false;
    RespawnCounter& counter = s_.respawnCounter();
    const int k = counter.k;
    Lcg g(id);  // the game-wide generator is set aside; a second one seeded with the player's id
    const auto& xs = c->intersections();
    const int NI = int(xs.size());
    const auto& starts = c->starts();
    bool found = false;
    for (int attempt = 0; attempt < 100000 && !found; ++attempt) {
        int r = 0;
        for (int n = 0; n <= k; ++n) r = g.next15();
        const int i = r % (NI - 1) + 1;
        const int index = xs[size_t(i)].index;
        auto it = std::lower_bound(starts.begin(), starts.end(), index,
                                   [](const CityStart& s, int v) { return s.intersection < v; });
        if (it != starts.end() && it->intersection == index) {
            pos = it->pos + vec3(0, 2.0f, 0);
            found = true;
        }
    }
    counter.k = (k + 1) % 100;
    if (found && pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) {
        // (MM2's fallback: grid slot positions around (0, 0, 0).)
        pos = gridOffset(localSlot_ < 0 ? 3 : localSlot_, false);
    }
    return found;
}

vec3 NetGame::randomSpot() {
    const CityData* c = world_ ? world_->city() : nullptr;
    auto intersectionSpot = [&]() -> vec3 {
        // The free-roam rule without the enclosed-room and road-flag rejections (only deep water
        // and landmark rooms), with the game-wide generator: each attempt draws k + 1 numbers and
        // uses the last (k = the respawn counter, not advanced). The spot is the intersection.
        if (!c || c->intersections().size() < 2) return vec3();
        const auto& xs = c->intersections();
        const int NI = int(xs.size());
        const int k = s_.respawnCounter().k;
        for (int attempt = 0; attempt < 100000; ++attempt) {
            int r = 0;
            for (int n = 0; n <= k; ++n) r = gameRandom15();
            const BaiIntersection& x = xs[size_t(r % (NI - 1) + 1)];
            const int room = x.room;
            if (room <= 0 || size_t(room) >= c->rooms().size()) continue;
            const CityRoom& rm = c->rooms()[size_t(room)];
            if (rm.deepWater || rm.hasLandmark) continue;
            return x.pos;
        }
        return xs[1].pos;
    };
    if (spots_.empty()) return intersectionSpot();
    // One number from a temporary generator seeded with trunc(s × the host's id), s = the real
    // seconds since the frame mark; the game-wide generator is not touched.
    auto temporaryDraw = [&]() {
        const double secs = s_.realSeconds() - frameMark_;
        const double seed = std::trunc(secs * double(s_.host()));
        Lcg g{uint32_t(int64_t(seed))};
        return g.next15();
    };
    if (temporaryDraw() & 1) return intersectionSpot();  // odd: heads
    const int n = int(spots_.size());
    const int r = temporaryDraw() % (n - 1);  // the last row is never chosen
    return spots_[size_t(r)];
}

// ---------------------------------------------------------------------------------------------
// Attach and the frame

void NetGame::attach(NetWorld* w) {
    world_ = w;
    phys::Car& c = world_->localCar();
    c.setCountedImpactHook(impactHook_.get());
    c.setNetworkControls(true, 1.0f, 0.0f);
    c.setExtraMass(0.0f);
    clockRng_.reset(uint32_t(s_.self()) ^ 0x5eed1234u);
    frame_ = 0;
    frameMark_ = s_.realSeconds();
    syncRemotes();
    switch (mode_) {
        case ModeCheckpoint:
        case ModeCircuit:
        case ModeBlitz: raceSetup(); break;
        case ModeCops: copsStart(); break;
        default: cruiseStart(); break;
    }
}

void NetGame::frameStart(float F) {
    if (!world_) return;
    ++frame_;
    // 1. Incoming messages (in arrival order).
    std::vector<Incoming> in;
    in.swap(inbox_);
    for (const Incoming& m : in) handle(m);
    if (!world_) return;
    syncRemotes();
    for (auto& rc : remotes_)
        if (rc) rc->countFrame();
    // 2. Car states (3.2), after the incoming messages.
    sendStates(F);
    // 3. The mode.
    switch (mode_) {
        case ModeCheckpoint:
        case ModeCircuit:
        case ModeBlitz: raceFrame(F); break;
        case ModeCops: copsFrame(F); break;
        default: cruiseFrame(F); break;
    }
    localRules(F);
    // 4. The controls of this frame: the throttle cap (1.5) and the slow-down (5.2).
    phys::Car& c = world_->localCar();
    const float cap = mode_ == ModeCops && localCarries() ? st_.goldCap() : 1.0f;
    const bool race = isRaceMode(mode_);
    c.setNetworkControls(true, cap, race ? ctlSlow_ : 0.0f, race && ctlStopped_);
    if (race && ctlStopped_) c.setDrivable(false);
    frameMark_ = s_.realSeconds();  // MM2's frame mark: the end of the frame's game update
    if (selfCheaterHold_ || s_.selfIsCheater()) {
        selfCheaterHold_ = true;
        c.setDrivable(false);  // a marked player's car stays undrivable for the rest of the game (3.7)
    }
}

void NetGame::beforePhysics(float F, std::vector<phys::SceneMover>& movers) {
    if (!world_) return;
    const CityData* city = world_->city();
    std::function<int(const vec3&, int)> find;
    if (city) find = [city](const vec3& p, int hint) { return city->findRoom(p, hint); };
    // 3.4: after the AI's movers, in slot order, all four switches; a trailer right after its car.
    for (auto& rc : remotes_) {
        if (!rc || !rc->shown) continue;
        rc->beforePhysics(F, find);
        phys::SceneMover m;
        m.kind = phys::SceneMover::CarMover;
        m.car = &rc->car;
        m.number = 40 + rc->slot;
        m.identity = rc->identity();
        movers.push_back(m);
        if (phys::Trailer* t = rc->car.trailer()) {
            phys::SceneMover tm;
            tm.kind = phys::SceneMover::TrailerMover;
            tm.trailer = t;
            tm.number = 40 + rc->slot;
            tm.identity = t->identity;
            movers.push_back(tm);
        }
    }
}

void NetGame::frameEnd(float F) {
    (void)F;
    if (!world_) return;
    if (knockPending_) {
        knockPending_ = false;
        if (localCarries()) knockedLoose();
    }
}

// ---------------------------------------------------------------------------------------------
// Car states

uint16_t NetGame::localScore() const {
    if (mode_ == ModeCops) return uint16_t(points_ < 0 ? 0 : points_ > 65535 ? 65535 : points_);
    if (isRaceMode(mode_)) return uint16_t(list_.count());
    return 0;
}

void NetGame::sendStateTo(PlayerId to, uint16_t extraFlags) {
    const phys::Car& c = world_->localCar();
    CarState cs = captureCarState(c);
    cs.hostClock = s_.isHost() ? hostClock_ : 0.0f;
    cs.score = localScore();
    if (world_->localHorn()) cs.flags |= FlagHorn;
    if (world_->localSiren()) cs.flags |= FlagSiren;
    cs.flags |= extraFlags;
    cs.seq = ++seq_;  // one counter for all recipients
    Writer w(Msg::CarState);
    writeCarState(w, cs);
    const bool ok = s_.send(to, w, false);
    ++statesSent;
    if (!ok && to != 0)
        if (Player* p = s_.find(to)) p->unreachable = true;  // no more states go to it
}

void NetGame::sendStates(float F) {
    if (!s_.open()) return;
    const Player* me = s_.find(s_.self());
    if (me && me->rec.slowLink) {
        // One copy to everyone, I = 0.1 s.
        if (slowLinkTimer_ > 0.1f) {
            sendStateTo(0, 0);
            slowLinkTimer_ = 0.0f;
        } else {
            slowLinkTimer_ = slowLinkTimer_ + F;
        }
        return;
    }
    for (const Player& pc : s_.players()) {
        if (pc.id == s_.self()) continue;
        Player* p = s_.find(pc.id);
        if (!p || p->unreachable) continue;
        const float I = p->rec.slowLink ? 0.1f : 0.05f;
        if (p->sendTimer > I) {
            sendStateTo(p->id, 0);
            p->sendTimer = 0.0f;
        } else {
            p->sendTimer = p->sendTimer + F;
        }
    }
}

void NetGame::onCarState(PlayerId from, Reader& r) {
    CarState cs;
    if (!readCarState(r, cs)) return;
    RemoteCar* rc = remoteOf(from);
    if (!rc || from == s_.self()) return;  // unknown players and oneself are ignored
    bool cleared = false;
    if (!rc->receive(cs, s_.clock(), cleared)) {
        ++statesDropped;
        return;
    }
    ++statesReceived;
    if (!s_.isHost() && from == s_.host() && cs.hostClock > 0.0f) hostClock_ = cs.hostClock;
}

// ---------------------------------------------------------------------------------------------
// Rules of every mode (1.5)

bool NetGame::damageOn() const { return mode_ == ModeCops || !st_.noDamage; }

void NetGame::localRules(float F) {
    wreckPenalty(F);
    waterRule(F);
}

void NetGame::wreckPenalty(float F) {
    phys::Car& c = world_->localCar();
    if (penaltyTimer_ > 0.0f) {
        penaltyTimer_ = penaltyTimer_ - F;
        if (penaltyTimer_ <= 0.0f) {
            penaltyTimer_ = 0.0f;
            c.clearDamage();
            c.setDrivable(true);
            if (c.gearIndex() <= 1) c.selectGear(2);
            Writer w(Msg::DamageRepaired);
            s_.sendAll(w);
        }
        return;
    }
    const bool racing = isRaceMode(mode_) ? raceState_ == Racing : playing_;
    if (!racing || !damageOn() || !c.wrecked()) return;
    int text = 155;
    if (isRaceMode(mode_)) text = raceTexts(mode_).penalty;
    else if (mode_ == ModeCops) text = 114;
    s_.notice(text, 5.0f);
    world_->event("Damgelose");
    c.setDrivable(false);
    Writer w(Msg::Wrecked);
    s_.sendAll(w);
    penaltyTimer_ = 5.0f;
    if (mode_ == ModeCops && localCarries()) dropGold(c.body().p, false, true);
}

void NetGame::waterRule(float F) {
    phys::Car& c = world_->localCar();
    if (!world_->city()) return;
    if (!c.worldLimitReset(F)) return;
    c.collision().waterTimer = 0.0f;
    if (isRaceMode(mode_)) {
        // At the gate the car hit last (gate 0 before any), heading −(its angle) × π/180.
        if (list_.size() > 0) {
            const Waypoint& w = list_.point(list_.lastPassed());
            localReset(w.pos, w.heading * -0.017453292f);
        } else {
            localReset(S_, theta_);
        }
    } else if (mode_ == ModeCops) {
        if (localCarries()) dropGold(goldHome_, true, true);  // the gold returns home (forced)
        localReset(resetPoint_, 0.0f);
    } else {
        localReset(resetPoint_, 0.0f);
    }
}

void NetGame::localReset(const vec3& q, float theta) {
    world_->placeLocal(q, theta);
    phys::Car& c = world_->localCar();
    c.collision().waterTimer = 0.0f;
    if (mode_ == ModeCops && localCarries()) c.setExtraMass(st_.goldMass());
    sendCarReset();
}

void NetGame::sendCarReset() {
    const phys::Car& c = world_->localCar();
    Writer w(Msg::CarReset);
    for (int i = 0; i < 3; ++i) w.v3(c.rotation().r[i]);
    w.v3(c.body().p);
    s_.sendAll(w);
}

// ---------------------------------------------------------------------------------------------
// Messages

void NetGame::onLeft(PlayerId id, const std::string& name) {
    RemoteCar* rc = remoteOf(id);
    if (mode_ == ModeCops && carrier_ == id && s_.isHost()) {
        // 5.3 "Leaving": the host drops the gold where it floats.
        const vec3 P = rc ? rc->car.body().p + vec3(0, 2.0f, 0) : gold_;
        dropGold(P, false, true);
    }
    if (isRaceMode(mode_)) {
        if (raceState_ == Waiting) {
            if (waitCount_ > 0) --waitCount_;
            if (waitCount_ > 0 && loadedSent_) s_.notice(30 + std::min(waitCount_, 7), 60.0f);
        } else {
            s_.noticeTwoLines(38, name);
        }
    } else if (mode_ == ModeCruise) {
        s_.noticeTwoLines(38, name);
    }
    removeRemote(id);
    if (carrier_ == id) carrier_ = 0;
}

void NetGame::onHostChanged(PlayerId host) {
    (void)host;
    if (mode_ == ModeCops) {
        // Every peer shows text 40, sends "host quits" and quits.
        s_.chatLine(40);
        Writer w(Msg::HostQuits);
        s_.sendAll(w);
        quitWanted = true;
    }
    // Other modes: the new host marks itself host (the session did) and the game continues.
}

void NetGame::onMessage(PlayerId from, Msg type, Reader& r) {
    const bool fromHost = from == s_.host();
    switch (type) {
        case Msg::Loaded:
        case Msg::Reloaded: {
            const uint32_t check = type == Msg::Loaded ? r.u32() : 0;
            if (type == Msg::Loaded) s_.cheatCheck(from, check, false);
            if (!isRaceMode(mode_)) return;
            if (waitCount_ > 0) {
                --waitCount_;
                if (waitCount_ > 0 && loadedSent_) s_.notice(30 + std::min(waitCount_, 7), 60.0f);
            }
            // The sender's car appears: dropped to the ground where it is, reset and shown.
            if (RemoteCar* rc = remoteOf(from)) {
                const vec3 p = dropToGround(rc->car.body().p, 7.5f, 15.0f);
                rc->resetTo(rc->car.rotation(), p);
                rc->car.damage().enabled = false;
                rc->shown = true;
            }
            return;
        }
        case Msg::Joined: {
            const uint32_t check = r.u32();
            s_.cheatCheck(from, check, true);
            if (RemoteCar* rc = remoteOf(from)) {
                if (!rc->shown) s_.noticeTwoLines(42, s_.nameOf(from));
                showRemote(*rc, true);
            }
            return;
        }
        case Msg::RaceStart:
            if (fromHost && isRaceMode(mode_) && raceState_ == Waiting) {
                raceState_ = Countdown;
                t_ = 2.5f;
                countStep_ = 0;
            }
            return;
        case Msg::Finished: {
            const float t = r.f32();
            if (!s_.isHost() || !isRaceMode(mode_)) return;
            const Player* p = s_.find(from);
            if (!p || p->cheater) return;  // a marked cheater's reports are ignored
            if (t < kDnf) s_.noticeTwoLines(raceTexts(mode_).report, p->rec.name, game::formatRaceTime(t));
            insertResult(from, t);
            Writer w(Msg::Result);
            w.u32(from);
            w.f32(t);
            s_.sendAll(w);
            return;
        }
        case Msg::Result: {
            const PlayerId id = r.u32();
            const float t = r.f32();
            if (!fromHost || !isRaceMode(mode_) || s_.hostIsCheater()) return;
            insertResult(id, t);
            const RaceTexts tx = raceTexts(mode_);
            if (id == s_.self()) {
                if (t < kDnf) s_.noticeTwoLines(tx.ownResult, s_.localRecord().name, game::formatRaceTime(t));
                if (raceState_ != Results) {
                    if (mode_ == ModeBlitz) {
                        blitzR_ = 3.0f;  // its results 3 s after its own "result"; S not restarted
                        blitzTimers_ = true;
                    } else {
                        raceState_ = SlowDown;  // restarts the 3 s slow-down (also after the first)
                        slowDown_ = 3.0f;
                    }
                }
            } else if (t < kDnf) {
                s_.noticeTwoLines(tx.other, s_.nameOf(id), game::formatRaceTime(t));
            }
            return;
        }
        case Msg::RaceOver:
            if (fromHost && isRaceMode(mode_) && finished_) showResults();
            return;
        case Msg::TimeUp: {
            if (!fromHost || !isRaceMode(mode_)) return;
            if (finished_ || timeUp_) return;
            if (mode_ == ModeBlitz) {
                blitzTimeUp(true);
                return;
            }
            timeUp_ = true;
            s_.notice(raceTexts(mode_).timeUp, 5.0f);
            stopLocalCar();
            finished_ = true;
            Writer w(Msg::Finished);
            w.f32(kDnf);
            s_.send(s_.host(), w);
            return;
        }
        case Msg::Restart:
            if (!fromHost) return;
            if (isRaceMode(mode_)) raceRestart(false, false);
            else if (mode_ == ModeCops) copsRestart();
            return;
        case Msg::Wrecked:
            return;  // receivers do nothing (the undrivable state comes with the car state)
        case Msg::DamageRepaired:
            if (RemoteCar* rc = remoteOf(from)) rc->car.clearDamage();  // its damage visuals cleared
            return;
        case Msg::CarReset: {
            phys::M3 R;
            for (int i = 0; i < 3; ++i) R.r[i] = r.v3();
            const vec3 p = r.v3();
            if (!r.ok()) return;
            if (RemoteCar* rc = remoteOf(from)) {
                const float extra = rc->car.extraMass();
                rc->resetTo(R, p);
                rc->car.setExtraMass(extra);
            }
            return;
        }
        // ---- Cops and Robbers
        case Msg::StateRequest: {
            const int team = r.u8() & 1;
            if (!r.ok()) return;
            bool known = false;
            for (auto& t : teams_)
                if (t.first == from) {
                    t.second = team;
                    known = true;
                }
            if (!known) teams_.push_back({from, team});
            if (s_.isHost() && mode_ == ModeCops) sendStateReply(from);
            return;
        }
        case Msg::StateReply: {
            if (!fromHost || mode_ != ModeCops) return;
            const bool carried = r.u8() != 0;
            const PlayerId id = r.u32();
            const vec3 g = r.v3(), b = r.v3(), h = r.v3();
            if (!r.ok() || stateReplied_) return;
            stateReplied_ = true;
            gold_ = g;
            goldHome_ = g;
            bank_ = b;
            hideout_ = h;
            placed_ = true;
            carrier_ = 0;
            goldShown_ = true;
            if (carried && id != 0) {
                carrier_ = id;
                goldShown_ = id != s_.self();
                if (RemoteCar* rc = remoteOf(id)) rc->car.setExtraMass(st_.goldMass());
            }
            Writer w(Msg::Joined);
            w.u32(s_.localRecord().check);
            s_.sendAll(w);
            copsGo();
            return;
        }
        case Msg::GoldRequest: {
            if (!s_.isHost() || mode_ != ModeCops) return;
            if (carrier_ != 0 || over_) return;  // the first request the host sees wins
            Writer w(Msg::GoldCarrier);
            w.u32(from);
            s_.sendAll(w);
            s_.notice(133, 5.0f, s_.nameOf(from));
            takeGold(from);
            ++scoringEvents_;
            return;
        }
        case Msg::GoldCarrier: {
            const PlayerId id = r.u32();
            if (!fromHost || mode_ != ModeCops || !r.ok()) return;
            if (id == s_.self()) s_.notice(134, 3.0f);
            else s_.notice(135, 5.0f, s_.nameOf(id));
            takeGold(id);
            return;
        }
        case Msg::GoldDropped: {
            const vec3 P = r.v3();
            if (mode_ != ModeCops || !r.ok()) return;
            s_.notice(136, 5.0f, s_.nameOf(carrier_ != 0 ? carrier_ : from));
            if (RemoteCar* rc = remoteOf(carrier_)) rc->car.setExtraMass(0.0f);
            carrier_ = 0;
            gold_ = P;
            goldShown_ = true;
            goldHidden_ = false;
            requestPending_ = false;
            return;
        }
        case Msg::GoldDelivered: {
            if (mode_ != ModeCops) return;
            s_.notice(137, 5.0f, s_.nameOf(from));
            if (RemoteCar* rc = remoteOf(from)) {
                rc->car.clearDamage();
                rc->car.setExtraMass(0.0f);
            }
            carrier_ = 0;
            if (s_.isHost()) {
                placeGoldAndBases();
                Writer w(Msg::GoldPlaced);
                w.v3(goldHome_);
                w.v3(bank_);
                w.v3(hideout_);
                s_.sendAll(w);
            }
            return;
        }
        case Msg::GoldPlaced: {
            const vec3 g = r.v3(), b = r.v3(), h = r.v3();
            if (!fromHost || mode_ != ModeCops || !r.ok()) return;
            gold_ = goldHome_ = g;
            bank_ = b;
            hideout_ = h;
            placed_ = true;
            if (RemoteCar* rc = remoteOf(carrier_)) rc->car.setExtraMass(0.0f);
            carrier_ = 0;
            goldShown_ = true;
            goldHidden_ = false;
            requestPending_ = false;
            return;
        }
        case Msg::MinutesLeft: {
            const int n = r.u8();
            if (fromHost && n >= 0 && n <= 4) s_.notice(138 + n, 2.0f);
            return;
        }
        case Msg::GameOver: {
            const int reason = r.u8();
            if (!fromHost || mode_ != ModeCops || over_) return;
            s_.notice(reason == 1 ? 130 : 131, 3.0f);
            over_ = reason == 1 ? 1 : 2;
            overTimer_ = 3.0f;
            return;
        }
        default:
            return;
    }
}

// ---------------------------------------------------------------------------------------------
// Cruise (5.1)

void NetGame::cruiseStart() {
    phys::Car& c = world_->localCar();
    vec3 p;
    networkRespawn(s_.self(), p);
    world_->placeLocal(p, 0.0f);
    resetPoint_ = p;
    c.setDrivable(false);
    c.damage().enabled = false;
    s_.localRecord().inGame = true;
    s_.publishRecord();
    Writer w(Msg::Joined);
    w.u32(s_.localRecord().check);
    s_.sendAll(w);
    playing_ = false;
}

void NetGame::cruiseFrame(float F) {
    if (frame_ == 1) {
        // The first frame: a clock if the session has a time limit (it ends nothing).
        if (st_.timeLimit > 0.0f) {
            modeClockOn_ = true;
            modeClock_ = 0.0f;
        }
    } else if (frame_ == 2) {
        world_->event("Startracehigh");
        s_.notice(154, 1.25f, std::string(), true);
        phys::Car& c = world_->localCar();
        c.setDrivable(true);
        if (c.gearIndex() <= 1) c.selectGear(2);
        c.damage().enabled = !st_.noDamage;
        playing_ = true;
    }
    if (modeClockOn_) modeClock_ = modeClock_ + F;
    if (s_.isHost()) hostClock_ = modeClockOn_ ? modeClock_ : 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Races (5.2)

void NetGame::raceSetup() {
    const game::RaceMode gm = gameModeOf(mode_);
    if (s_.vfs()) {
        BytesPtr b = s_.vfs()->open(game::waypointPath(st_.city, gm, st_.race));
        if (b) {
            const WaypointFile wf = parseWaypoints(std::string(b->begin(), b->end()), 50, mode_ == ModeCircuit);
            wpts_ = wf.points;
        } else {
            logWarn("network race: %s is missing", game::waypointPath(st_.city, gm, st_.race).c_str());
        }
    }
    if (!wpts_.empty()) {
        const game::Layout layout = mode_ == ModeCircuit ? game::Layout::Laps
                                    : mode_ == ModeBlitz ? game::Layout::AnyOrder
                                                         : game::Layout::CheckpointsFinish;
        list_.set(wpts_, layout, mode_ == ModeCircuit ? std::max(1, st_.laps) : 1, false);
        list_.reset();
        S_ = wpts_[0].pos;
        theta_ = wpts_[0].heading * -0.017453292f;
    }
    // Circuit makes the concrete barricades sturdier (1.4).
    if (mode_ == ModeCircuit && world_->collision()) {
        world_->collision()->setPropDataScale("sp_barricadeconcl_f", 26.0f);
        world_->collision()->setPropDataScale("sp_barricadeconcr_f", 26.0f);
    }
    // Sealed at the start of a race mode (1.3 step 4).
    if (s_.isHost()) s_.setSealed(true);
    localSlot_ = -1;
    const auto& table = s_.slotTable();
    for (size_t i = 0; i < table.size(); ++i)
        if (table[i] == s_.self()) localSlot_ = int(i);
    raceRestart(false, true);
}

void NetGame::raceToGrid() {
    phys::Car& c = world_->localCar();
    const vec3 q = gridPosition(localSlot_ < 0 ? 8 : localSlot_, wideCar(c, c.trailer() != nullptr));
    world_->placeLocal(q, theta_);
    c.setDrivable(false);
    c.damage().enabled = false;
    for (auto& rc : remotes_) {
        if (!rc) continue;
        int startSlot = 8;
        const auto& table = s_.slotTable();
        for (size_t i = 0; i < table.size(); ++i)
            if (table[i] == rc->id) startSlot = int(i);
        rc->car.placeAt(gridPosition(startSlot, wideCar(rc->car, rc->hasTrailer)), theta_);
        rc->car.damage().enabled = false;
        rc->placed();
    }
}

void NetGame::raceRestart(bool sendIt, bool initial) {
    phys::Car& c = world_->localCar();
    results_.clear();
    finished_ = reported_ = resultsShown_ = timeUp_ = raceOverSent_ = false;
    window_ = -1.0f;
    clockOn_ = countdownOn_ = false;
    clock_ = 0.0f;
    countdown_ = st_.timeLimit;
    countdownShown_ = mode_ == ModeBlitz;
    slowDown_ = 0.0f;
    penaltyTimer_ = 0.0f;
    c.setDrivable(false);
    c.damage().enabled = false;
    c.clearDamage();
    list_.reset();
    raceToGrid();
    if (sendIt) {
        Writer w(Msg::Restart);
        s_.sendAll(w);
    }
    // One car state with the reset flag to everyone (receivers snap the car, 3.3 step 11).
    if (!initial)
        for (const Player& p : s_.players())
            if (p.id != s_.self()) sendStateTo(p.id, FlagReset);
    raceState_ = Waiting;
    t_ = 0.0f;
    loadedSent_ = false;
    restarted_ = !initial;
    waitCount_ = std::max(0, s_.playerCount() - 1);
    place_ = 1;
    racersShown_ = 1 + int(remoteCars().size());
    for (int& p : remotePlace_) p = 0;
    blitzS_ = 60.0f;  // 5.2 "Finishing, Blitz" 3: 60 s before any finish
    blitzR_ = 0.0f;
    blitzTimers_ = false;
    ctlSlow_ = 0.0f;
    ctlStopped_ = false;
    playing_ = false;
    frameMark_ = s_.realSeconds();
}

void NetGame::raceGo() {
    // 5.2 "Countdown": go.
    phys::Car& c = world_->localCar();
    raceState_ = Racing;
    clockOn_ = true;  // the race clock starts whatever the time limit (5.2 "Countdown")
    clock_ = 0.0f;
    if (mode_ == ModeBlitz) {
        countdown_ = st_.timeLimit;
        countdownOn_ = true;
    }
    c.setDrivable(true);
    if (c.gearIndex() <= 1) c.selectGear(2);
    if (c.automatic()) c.clearAutoReverse();
    c.damage().enabled = !st_.noDamage;
    for (auto& rc : remotes_)
        if (rc) rc->car.damage().enabled = !st_.noDamage;
    playing_ = true;
    world_->event("musicStart");
}

int NetGame::racers() const {
    if (isRaceMode(mode_)) return racersShown_;
    return 1 + int(remoteCars().size());
}

bool NetGame::playerFinished(PlayerId id) const {
    for (const NetResult& r : results_)
        if (r.id == id) return true;
    return false;
}

int NetGame::remotePlace(int slot) const { return slot >= 0 && slot < 8 ? remotePlace_[slot] : 0; }

// 5.2 "Racing": the remote cars as the opponents of results.md's standings.
void NetGame::standings() {
    const int n = list_.size();
    if (n == 0 || resultsShown_) return;
    const phys::Car& c = world_->localCar();
    const vec3& tgt = list_.point(list_.target()).pos;
    const float pd = dist2(c.body().p, tgt);
    const int pc = list_.count();
    // Players who finished whose car is not present still count ahead and in the total.
    int absentFinished = 0;
    for (const NetResult& r : results_) {
        if (r.id == s_.self()) continue;
        const RemoteCar* rc = remoteOf(r.id);
        if (!rc || !rc->shown) ++absentFinished;
    }
    if (!finished_) {
        // 1. The local place (the display keeps its last values once the local car finished).
        int place = 1 + absentFinished, total = 1 + absentFinished;
        for (const auto& rc : remotes_) {
            if (!rc || !rc->shown) continue;
            ++total;
            const int k = rc->score();
            if (playerFinished(rc->id) || k > pc || (k == pc && dist2(rc->car.body().p, tgt) < pd)) ++place;
        }
        place_ = place;
        racersShown_ = total;
    }
    // 2. Each present remote car's place (its icon); a finished player's icon is hidden (0).
    for (const auto& rc : remotes_) {
        if (!rc) continue;
        int& out = remotePlace_[rc->slot];
        if (!rc->shown || playerFinished(rc->id)) {
            out = 0;
            continue;
        }
        const int ki = rc->score();
        const vec3& T = list_.point(ki % n).pos;
        const float e = dist2(rc->car.body().p, T);
        int p = 1 + absentFinished;
        for (const auto& oj : remotes_) {
            if (!oj || oj.get() == rc.get() || !oj->shown) continue;
            const int kj = oj->score();
            if (playerFinished(oj->id) || kj > ki || (kj == ki && dist2(oj->car.body().p, T) < e)) ++p;
        }
        if (pc > ki || finished_ || (pc == ki && pd < dist2(rc->car.body().p, tgt))) ++p;
        out = p;
    }
}

void NetGame::insertResult(PlayerId id, float time) {
    for (const NetResult& r : results_)
        if (r.id == id) return;  // a player already in the table is not added again
    NetResult n;
    n.id = id;
    n.time = time;
    auto at = std::upper_bound(results_.begin(), results_.end(), n,
                               [](const NetResult& a, const NetResult& b) { return a.time < b.time; });
    results_.insert(at, n);
    // The finish window starts with the first result (the host's counts; clients run a copy).
    if (results_.size() == 1 && mode_ != ModeBlitz && window_ < 0.0f) window_ = 60.0f;
}

void NetGame::stopLocalCar() {
    phys::Car& c = world_->localCar();
    if (raceState_ != Results) raceState_ = Stopped;
    slowDown_ = 0.0f;
    clockOn_ = false;
    c.setDrivable(false);  // steering 0, brake 1, not drivable (5.2)
    ctlSlow_ = 0.0f;
    ctlStopped_ = true;
}

void NetGame::localFinished(float t) {
    finished_ = true;
    clockOn_ = false;
    world_->postRaceCamera();
    raceState_ = SlowDown;
    slowDown_ = 3.0f;
    if (mode_ == ModeBlitz) {
        // 5.2 "Finishing, Blitz" 1: the slow-down S and the results timer R.
        countdownOn_ = false;
        blitzS_ = 3.0f;
        blitzR_ = countdown_;
        blitzTimers_ = true;
    }
    world_->event("Endofracetag");
    const RaceTexts tx = raceTexts(mode_);
    if (s_.isHost()) {
        s_.noticeTwoLines(tx.ownHost, s_.localRecord().name, game::formatRaceTime(t));
        insertResult(s_.self(), t);
        Writer w(Msg::Result);
        w.u32(s_.self());
        w.f32(t);
        s_.sendAll(w);
    } else {
        Writer w(Msg::Finished);
        w.f32(t);
        s_.send(s_.host(), w);
    }
    reported_ = true;
}

void NetGame::showResults() {
    if (resultsShown_) return;
    resultsShown_ = true;
    raceState_ = Results;
    slowDown_ = 0.0f;
    ctlStopped_ = true;
    world_->localCar().setDrivable(false);
    world_->event("results");
}

void NetGame::blitzTimeUp(bool received) {
    // 5.2 "Finishing, Blitz" 3: the finish line off, the clocks stopped, 86400 reported.
    const RaceTexts tx = raceTexts(mode_);
    timeUp_ = true;
    finished_ = true;
    list_.setOff(true);
    clockOn_ = countdownOn_ = false;
    raceState_ = SlowDown;
    if (received) {
        s_.notice(97, 5.0f);
        blitzR_ = 5.0f;  // S keeps its value
    } else {
        s_.notice(tx.over, 5.0f);
        blitzR_ = blitzS_ = 5.0f;
    }
    blitzTimers_ = true;
    if (s_.isHost() && !received) {
        Writer w(Msg::TimeUp);
        s_.sendAll(w);
        Writer r(Msg::Result);
        r.u32(s_.self());
        r.f32(kDnf);
        s_.sendAll(r);
        insertResult(s_.self(), kDnf);
    } else {
        Writer w(Msg::Finished);
        w.f32(kDnf);
        s_.send(s_.host(), w);
    }
}

void NetGame::raceFrame(float F) {
    phys::Car& c = world_->localCar();
    const RaceTexts tx = raceTexts(mode_);
    ctlSlow_ = 0.0f;
    ctlStopped_ = raceState_ == Stopped || raceState_ == Results;
    switch (raceState_) {
        case Waiting: {
            // 5.2 "Waiting": until its own "loaded" has gone out a machine only runs its 5 s timer;
            // in the first frame it is above, it sends "loaded" and does nothing else.
            if (!loadedSent_) {
                if (t_ <= 5.0f) {
                    t_ = t_ + F;
                } else {
                    loadedSent_ = true;
                    if (restarted_) {
                        Writer w(Msg::Reloaded);
                        s_.sendAll(w);
                    } else {
                        Writer w(Msg::Loaded);
                        w.u32(s_.localRecord().check);
                        s_.sendAll(w);
                    }
                    if (waitCount_ > 0) s_.notice(30 + std::min(waitCount_, 7), 60.0f);
                }
                break;
            }
            if (waitCount_ <= 0 && s_.isHost()) {
                Writer w(Msg::RaceStart);
                s_.sendAll(w);
                raceState_ = Countdown;
                t_ = 2.5f;
                countStep_ = 0;
            }
            break;
        }
        case Countdown: {
            if (countStep_ == 0) {
                countStep_ = 1;
                world_->event("Startracelow");
                s_.notice(tx.ready, 1.25f, std::string(), true);
            }
            t_ = t_ - F;
            if (countStep_ == 1 && t_ <= 1.25f) {
                countStep_ = 2;
                world_->event("Startracelow");
                s_.notice(tx.set, 1.25f, std::string(), true);
            }
            if (t_ <= 0.0f) {
                world_->event("Startracehigh");
                s_.notice(tx.go, 1.25f, std::string(), true);
                raceGo();
            }
            break;
        }
        case Racing: {
            // The gates with the local car's pose.
            std::vector<game::GateEvent> ev;
            game::GatePose pose;
            pose.P = c.body().p;
            pose.X = c.rotation().r[0];
            pose.Y = c.rotation().r[1];
            pose.Z = c.rotation().r[2];
            list_.frame(pose, c.tune().sim.inertiaBox, ev);
            for (const game::GateEvent& e : ev) {
                if (e.kind == game::GateEvent::Registered) world_->event("Waypoint");
                else if (e.kind == game::GateEvent::FinalCheckpoint) world_->event("musicFinalCheckpoint");
            }
            if (list_.done()) localFinished(clock_);
            else if (mode_ == ModeBlitz && countdown_ <= 0.0f && !timeUp_) blitzTimeUp(false);
            break;
        }
        case SlowDown: {
            if (mode_ == ModeBlitz) break;  // the timers S and R below
            slowDown_ = slowDown_ - F;
            if (slowDown_ <= 0.0f) stopLocalCar();
            else ctlSlow_ = slowDown_;
            break;
        }
        default:
            break;
    }
    if (raceState_ == SlowDown && mode_ != ModeBlitz && slowDown_ > 0.0f) ctlSlow_ = slowDown_;
    // Blitz after the finish or time up: if S ≤ 0 the car is stopped; then if R ≤ 0 the results
    // and the car stopped; otherwise throttle − R × 0.2, brake 1, both timers down.
    if (mode_ == ModeBlitz && blitzTimers_ && !resultsShown_) {
        if (blitzS_ <= 0.0f) stopLocalCar();
        if (blitzR_ <= 0.0f) {
            stopLocalCar();
            showResults();
        } else {
            ctlSlow_ = blitzR_;
            blitzS_ = blitzS_ - F;
            blitzR_ = blitzR_ - F;
        }
    }
    // Clocks (they follow the real clock in network games, hud.md: the frame time here).
    if (clockOn_) clock_ = clock_ + F;
    if (countdownOn_) {
        countdown_ = countdown_ - F;
        if (countdown_ <= 0.0f) {
            countdown_ = 0.0f;
            countdownOn_ = false;
        }
    }
    if (s_.isHost()) hostClock_ = mode_ == ModeBlitz ? countdown_ : clock_;
    if (raceState_ == Racing || raceState_ == SlowDown || raceState_ == Stopped) standings();
    if (mode_ == ModeBlitz) return;
    // The finish window (wait for all on).
    if (window_ >= 0.0f) {
        window_ = window_ - F;
        if (window_ <= 0.0f) {
            window_ = -1.0f;
            if (s_.isHost()) {
                Writer w(Msg::TimeUp);
                s_.sendAll(w);
                if (!finished_) {
                    finished_ = true;
                    stopLocalCar();
                    s_.notice(tx.over, 5.0f);
                    insertResult(s_.self(), kDnf);
                    Writer r(Msg::Result);
                    r.u32(s_.self());
                    r.f32(kDnf);
                    s_.sendAll(r);
                }
            }
        }
    }
    // Every player in the table: race over.
    if (s_.isHost() && !raceOverSent_ && !results_.empty() && int(results_.size()) >= s_.playerCount()) {
        raceOverSent_ = true;
        Writer w(Msg::RaceOver);
        s_.sendAll(w);
        stopLocalCar();
        showResults();
    }
    // Wait-for-all off: each machine shows the results when its own slow-down ends.
    const Player* h = s_.find(s_.host());
    if (h && !h->rec.waitForAll && finished_ && raceState_ == Stopped) showResults();
}

// ---------------------------------------------------------------------------------------------
// Cops and Robbers (5.3)

int NetGame::teamOf(PlayerId id) const {
    if (id == s_.self()) return s_.localRecord().team;
    for (const auto& t : teams_)
        if (t.first == id) return t.second;
    const Player* p = s_.find(id);
    return p && p->haveRecord ? p->rec.team : 1;
}

int NetGame::localTeam() const { return s_.localRecord().team; }

int NetGame::teamPoints(int team) const {
    int sum = localTeam() == team ? points_ : 0;
    for (const auto& rc : remotes_)
        if (rc && teamOf(rc->id) == team) sum += rc->score();
    return sum;
}

bool NetGame::localCarries() const { return carrier_ != 0 && carrier_ == s_.self(); }

vec3 NetGame::goldPoint() const {
    if (carrier_ != 0) {
        if (carrier_ == s_.self() && world_) return world_->localCar().body().p + vec3(0, 2.0f, 0);
        if (RemoteCar* rc = remoteOf(carrier_)) return rc->car.body().p + vec3(0, 2.0f, 0);
    }
    return gold_;
}

bool NetGame::arrowTarget(vec3& p) const {
    if (mode_ == ModeCops) {
        if (!placed_) return false;
        if (localCarries()) p = localTeam() == 1 ? hideout_ : bank_;
        else p = goldPoint();
        return true;
    }
    if (isRaceMode(mode_) && list_.arrowOn() && mode_ != ModeCircuit) {
        p = list_.targetPosition();
        return true;
    }
    return false;
}

bool NetGame::pickupBlocked() const { return penaltyTimer_ > 0.0f || knockPause_ > 0.0f; }

void NetGame::copsStart() {
    phys::Car& c = world_->localCar();
    if (s_.vfs()) {
        BytesPtr b = s_.vfs()->open("race/" + st_.city + "/multicopwaypoints.csv");
        if (b) {
            const WaypointFile wf = parseWaypoints(std::string(b->begin(), b->end()), 51, false);
            for (const Waypoint& w : wf.points)
                if (!w.appended && spots_.size() < 50) spots_.push_back(w.pos);  // the angle is not used
            if (spots_.size() < 3) spots_.clear();
        }
    }
    vec3 p;
    networkRespawn(s_.self(), p);
    world_->placeLocal(p, 0.0f);
    resetPoint_ = p;
    c.setDrivable(false);
    c.damage().enabled = false;
    PlayerRecord& rec = s_.localRecord();
    rec.inGame = true;
    if (st_.subMode == FreeForAll) rec.team = (infoFlags(s_.vfs(), rec.car) & 8) ? 0 : 1;
    s_.publishRecord();
    Writer w(Msg::StateRequest);
    w.u8(uint8_t(rec.team));
    s_.sendAll(w);
    if (s_.isHost()) {
        placeGoldAndBases();
        copsGo();
    }
}

void NetGame::copsGo() {
    phys::Car& c = world_->localCar();
    world_->event("Startracehigh");
    s_.notice(113, 1.25f, std::string(), true);
    c.setDrivable(true);
    if (c.gearIndex() <= 1) c.selectGear(2);
    c.damage().enabled = true;  // Cops and Robbers always clears "no damage"
    playing_ = true;
    copsStarted_ = true;
    if (s_.isHost() && st_.limitType == LimitTime) {
        copsClock_ = float(st_.limitMinutes()) * 60.0f;
        copsClockOn_ = true;
        warnL_ = copsClock_;  // L := limit × 60 at the start and at every restart
    }
}

void NetGame::sendStateReply(PlayerId to) {
    Writer w(Msg::StateReply);
    w.u8(carrier_ != 0 ? 1 : 0);
    w.u32(carrier_);
    w.v3(goldPoint());
    w.v3(bank_);
    w.v3(hideout_);
    s_.send(to, w);
}

void NetGame::placeGoldAndBases() {
    const vec3 G = randomSpot();
    vec3 H = randomSpot();
    for (int i = 0; i < 1000 && H.x == G.x && H.y == G.y && H.z == G.z; ++i) H = randomSpot();
    vec3 B = randomSpot();
    for (int i = 0; i < 1000 && ((B.x == H.x && B.y == H.y && B.z == H.z) || (B.x == G.x && B.y == G.y && B.z == G.z)); ++i)
        B = randomSpot();
    goldHome_ = gold_ = G;
    hideout_ = H;
    bank_ = B;
    placed_ = true;
    carrier_ = 0;
    goldShown_ = true;
    goldHidden_ = false;
    requestPending_ = false;
}

void NetGame::takeGold(PlayerId id) {
    carrier_ = id;
    goldHidden_ = false;
    requestPending_ = false;
    if (id == s_.self()) {
        goldShown_ = false;  // hidden on this machine; the arrow to the own base
        world_->localCar().setExtraMass(st_.goldMass());
        points_ += 25;
        if (s_.isHost()) ++scoringEvents_;
    } else {
        goldShown_ = true;  // floats over the carrier
        if (RemoteCar* rc = remoteOf(id)) rc->car.setExtraMass(st_.goldMass());
    }
}

void NetGame::dropGold(const vec3& P, bool forced, bool sendIt) {
    const PlayerId prev = carrier_;
    carrier_ = 0;
    if (prev == s_.self()) world_->localCar().setExtraMass(0.0f);
    else if (RemoteCar* rc = remoteOf(prev)) rc->car.setExtraMass(0.0f);
    vec3 at = P;
    const CityData* c = world_->city();
    if (forced) {
        at = goldHome_;
    } else if (c) {
        const int room = c->findRoom(P, 0);
        if (room > 0 && size_t(room) < c->rooms().size() && c->rooms()[size_t(room)].deepWater) at = goldHome_;
    }
    gold_ = dropToGround(at, 2.0f, 10.0f);
    goldShown_ = true;
    goldHidden_ = false;
    requestPending_ = false;
    if (sendIt) {
        Writer w(Msg::GoldDropped);
        w.v3(gold_);
        s_.sendAll(w);
    }
}

void NetGame::knockedLoose() {
    s_.notice(112, 3.0f);
    dropGold(world_->localCar().body().p, false, true);
    knockPause_ = 2.0f;
}

void NetGame::delivered(PlayerId id) {
    (void)id;
    phys::Car& c = world_->localCar();
    s_.notice(localTeam() == 1 ? 116 : 117, 3.0f);
    c.clearDamage();  // fully repaired
    c.setExtraMass(0.0f);
    carrier_ = 0;
    points_ += 100;
    Writer w(Msg::GoldDelivered);
    s_.sendAll(w);
    if (s_.isHost()) {
        ++scoringEvents_;
        placeGoldAndBases();
        Writer p(Msg::GoldPlaced);
        p.v3(goldHome_);
        p.v3(bank_);
        p.v3(hideout_);
        s_.sendAll(p);
    }
}

void NetGame::copsEnd(int reason) {
    s_.setSealed(true);
    Writer w(Msg::GameOver);
    w.u8(uint8_t(reason));
    s_.sendAll(w);
    over_ = reason;
    overTimer_ = 3.0f;
}

void NetGame::copsFrame(float F) {
    if (knockPause_ > 0.0f) knockPause_ = std::max(0.0f, knockPause_ - F);
    if (over_) {
        if (overTimer_ > 0.0f) {
            overTimer_ = overTimer_ - F;
            if (overTimer_ <= 0.0f) resultsShown_ = true;
        }
        if (s_.isHost()) hostClock_ = copsClock_;
        return;
    }
    if (!copsStarted_) return;  // waiting for the state reply
    phys::Car& c = world_->localCar();
    const vec3 P = c.body().p;
    const CityData* city = world_->city();
    // Picking up.
    if (carrier_ == 0 && placed_ && goldShown_ && !goldHidden_ && !pickupBlocked()) {
        const int goldRoom = roomOf(gold_ + vec3(0, 1.5f, 0));
        const int carRoom = city ? c.collision().room : 0;
        if (goldRoom == carRoom && dist2(P, gold_) < 25.0f) {
            if (s_.isHost()) {
                bool other = false;
                for (const auto& rc : remotes_)
                    if (rc && rc->shown) other = true;
                if (other) {
                    s_.notice(115, 3.0f);
                    takeGold(s_.self());
                    Writer w(Msg::GoldCarrier);
                    w.u32(s_.self());
                    s_.sendAll(w);
                }
            } else {
                goldHidden_ = true;
                requestPending_ = true;
                Writer w(Msg::GoldRequest);
                s_.send(s_.host(), w);
            }
        }
    }
    // Delivering to the own team's base: team 1 the hideout, team 0 the bank.
    if (localCarries()) {
        const vec3& base = localTeam() == 1 ? hideout_ : bank_;
        const int baseRoom = roomOf(base + vec3(0, 3.75f, 0));
        bool enclosed = false;
        if (city && baseRoom > 0 && size_t(baseRoom) < city->rooms().size())
            enclosed = (city->rooms()[size_t(baseRoom)].flags & PsdlEnclosed) != 0;
        const int carRoom = city ? c.collision().room : 0;
        if ((enclosed || baseRoom == carRoom) && dist2(P, base) < 144.0f) delivered(s_.self());
    }
    // Regeneration.
    if (!localCarries()) {
        const vec3& v = c.velocity();
        const float sp2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
        phys::DamageState& d = c.damage();
        if (sp2 > 25.0f && d.damage >= 0.01f) {
            d.damage = d.damage - 0.0005f * c.damageLimit();
            if (d.damage <= 0.0f) c.clearDamage();
        }
    }
    // The end (host).
    if (!s_.isHost()) return;
    if (copsClockOn_) {
        copsClock_ = copsClock_ - F;
        // Time warnings: the first threshold with t < threshold < L fires, then L := t (at most
        // one per frame).
        static const float marks[5] = {1200, 900, 600, 300, 60};
        for (int w = 0; w < 5; ++w) {
            if (!(copsClock_ < marks[w] && marks[w] < warnL_)) continue;
            warnL_ = copsClock_;
            s_.notice(138 + w, 2.0f);
            Writer m(Msg::MinutesLeft);
            m.u8(uint8_t(w));
            s_.sendAll(m);
            break;
        }
        if (copsClock_ < 0.1f) {
            copsClockOn_ = false;
            s_.notice(118, 3.0f);
            copsEnd(1);
        }
    }
    hostClock_ = copsClock_;
    if (st_.limitType == LimitPoints && !over_) {
        const int limit = st_.limitPoints();
        if (st_.subMode == FreeForAll) {
            bool remote = false;
            for (const auto& rc : remotes_)
                if (rc && rc->score() >= limit) remote = true;
            if (points_ >= limit) {
                s_.notice(119, 3.0f);
                copsEnd(2);
            } else if (remote && scoringEvents_ > 0) {
                s_.notice(120, 3.0f);
                copsEnd(2);
            }
        } else if ((teamPoints(0) >= limit || teamPoints(1) >= limit) && scoringEvents_ > 0) {
            s_.notice(121, 3.0f);
            copsEnd(2);
        }
    }
}

void NetGame::copsRestart() {
    if (s_.isHost()) s_.setSealed(false);
    points_ = 0;
    // Engine: the scoring events start again too, so remote points from before the restart (in
    // car states not yet replaced) cannot end the new game (QUESTIONS.md).
    scoringEvents_ = 0;
    over_ = 0;
    overTimer_ = 0.0f;
    resultsShown_ = false;
    // A carrier at the restart loses the gold (home).
    if (carrier_ != 0) {
        if (carrier_ == s_.self()) world_->localCar().setExtraMass(0.0f);
        else if (RemoteCar* rc = remoteOf(carrier_)) rc->car.setExtraMass(0.0f);
        carrier_ = 0;
        gold_ = goldHome_;
        goldShown_ = true;
    }
    goldHidden_ = requestPending_ = false;
    penaltyTimer_ = knockPause_ = 0.0f;
    phys::Car& c = world_->localCar();
    c.clearDamage();
    vec3 p;
    networkRespawn(s_.self(), p);
    world_->placeLocal(p, 0.0f);
    resetPoint_ = p;
    sendCarReset();
    copsGo();
}

// ---------------------------------------------------------------------------------------------
// Restart and results

bool NetGame::canRestart() const {
    if (!s_.isHost()) return false;
    if (isRaceMode(mode_)) return resultsShown_;
    if (mode_ == ModeCops) return resultsShown_;
    return false;
}

void NetGame::restart() {
    if (!canRestart()) return;
    if (isRaceMode(mode_)) {
        raceRestart(true, false);
    } else if (mode_ == ModeCops) {
        Writer w(Msg::Restart);
        s_.sendAll(w);
        copsRestart();
    }
}

std::vector<NetResult> NetGame::resultsList() const {
    std::vector<NetResult> out;
    if (isRaceMode(mode_)) return results_;
    if (mode_ != ModeCops) return out;
    if (st_.subMode != FreeForAll) {
        const int t0 = teamPoints(0), t1 = teamPoints(1);
        const int first = t1 > t0 ? 1 : 0;  // a tie: team 0 first
        // Names: cops and robbers 122/123 (cops won), 126/127 (robbers won); robber teams
        // 124/125 (blue won), 128/129 (red won).
        int a, b;
        if (st_.subMode == CopsAndRobbers) {
            a = first == 0 ? 122 : 126;
            b = first == 0 ? 123 : 127;
        } else {
            a = first == 0 ? 124 : 128;
            b = first == 0 ? 125 : 129;
        }
        NetResult w;
        w.team = first;
        w.points = first == 0 ? t0 : t1;
        w.teamText = a;
        out.push_back(w);
        NetResult l;
        l.team = 1 - first;
        l.points = first == 0 ? t1 : t0;
        l.teamText = b;
        out.push_back(l);
    }
    // The players by points, highest first; the local player above remote players with equal
    // points; remote players with equal points by slot order.
    std::vector<NetResult> players;
    NetResult me;
    me.id = s_.self();
    me.points = points_;
    players.push_back(me);
    for (const auto& rc : remotes_) {
        if (!rc) continue;
        NetResult r;
        r.id = rc->id;
        r.points = rc->score();
        players.push_back(r);
    }
    std::stable_sort(players.begin(), players.end(), [](const NetResult& x, const NetResult& y) { return x.points > y.points; });
    out.insert(out.end(), players.begin(), players.end());
    return out;
}

}  // namespace net
}  // namespace cc
