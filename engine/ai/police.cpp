#include "ai/police.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ai/road_network.h"
#include "ai/route_search.h"
#include "ai/traffic.h"
#include "city/city_data.h"
#include "core/log.h"
#include "physics/trailer.h"
#include "model/asset_cache.h"
#include "model/vehicle.h"
#include "physics/bound.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace ai {

namespace {
// Roster entries for opponents MM2 lists as chasing cops (police.md, "Detection" step 2).
constexpr int kOpponentAsCop = 100000;
}  // namespace

PoliceSystem::PoliceSystem() = default;
PoliceSystem::~PoliceSystem() = default;

void PoliceSystem::clear() {
    cops_.clear();
    roster_ = Roster();
    net_ = nullptr;
    city_ = nullptr;
    world_ = nullptr;
    traffic_ = nullptr;
}

void PoliceSystem::create(const Vfs& vfs, AssetCache& assets, const CityData& city, RoadNetwork& net,
                          phys::CollisionWorld& world, float density, const std::string& playerCar,
                          std::shared_ptr<phys::Bound> playerBound) {
    clear();
    city_ = &city;
    net_ = &net;
    world_ = &world;
    const RaceAimap& race = net.raceAimap();
    Dg_ = race.copChaseDistance;
    const float d = std::clamp(density, 0.0f, 1.0f);
    const int N = int(race.police.size());
    const int n = int(d * float(N));
    // City object → collision object (props), for the planner's obstacle lists.
    propOf_.assign(city.objects().size(), -1);
    for (size_t i = 0; i < world.objects().size(); ++i) {
        const phys::CollisionObject& o = world.objects()[i];
        if (o.kind == phys::ObjKind::Prop && o.cityObject >= 0 && size_t(o.cityObject) < propOf_.size())
            propOf_[size_t(o.cityObject)] = int(i);
    }
    // Paint for vpcop: 0 when the session runs cable cars, else 1 (police.md, "The cop's car"): one
    // cable car per intersection at which exactly one of its roads has a cable-car line on the side
    // leaving it (side B if the intersection is the road's end 1, side A if its end 0).
    bool cable = false;
    for (size_t I = 0; I < net.intersections().size() && !cable; ++I) {
        int n = 0;
        for (int ri : net.intersections()[I].roads) {
            const Road& r = net.roads()[size_t(ri)];
            if (r.end[1].intersection == int(I) && r.side[1].cable) ++n;
            else if (r.end[0].intersection == int(I) && r.side[0].cable) ++n;
        }
        if (n == 1) cable = true;
    }
    std::map<std::string, std::shared_ptr<phys::Bound>> bounds;
    if (playerBound) bounds[playerCar] = playerBound;
    std::map<std::string, std::shared_ptr<VehicleModel>> models;
    cops_.reserve(size_t(n));
    for (int i = 0; i < n; ++i) {
        const PoliceEntry& e = race.police[size_t(i)];
        Cop c;
        c.index = i;
        c.entry = e;
        phys::CarTune tune = phys::loadCarTune(vfs, e.model, false);
        if (!tune.simFile) {
            logWarn("police car %s has no .vehCarSim; skipped", e.model.c_str());
            continue;
        }
        auto it = bounds.find(e.model);
        if (it != bounds.end()) {
            tune.bound = it->second;
        } else if (tune.bound) {
            const phys::Bound& b = *tune.bound;
            const vec3 size(b.bmax.x - b.bmin.x, b.bmax.y - b.bmin.y, b.bmax.z - b.bmin.z);
            const vec3 off(size.x * 0.5f + b.bmin.x, size.y * 0.5f + b.bmin.y, size.z * 0.5f + b.bmin.z);
            tune.bound = std::make_shared<phys::Bound>(phys::makeBoxBound(size, off));
            bounds[e.model] = tune.bound;
        }
        if (!tune.bound) continue;
        const phys::Bound& b = *tune.bound;
        c.box = b.isBox;
        c.boxMin = b.bmin;
        c.boxMax = b.bmax;
        if (c.box) c.shape = phys::BoxShape{vec3(b.bmax.x - b.bmin.x, b.bmax.y - b.bmin.y, b.bmax.z - b.bmin.z), b.centre};
        c.car = std::make_unique<phys::Car>();
        c.car->setup(tune);
        c.driver.create(c.car.get(), e.model, b.bmin, b.bmax, false);
        c.driver.setRacerNumber(i);
        c.car->setAiStuck(0.75f);
        // A towing model gets its trailer, placed at every reset but never moved (trailer.md 10).
        {
            phys::TrailerTune tt;
            std::string why;
            if (phys::loadTrailerTune(vfs, c.car->tune(), tt, why)) {
                auto t = std::make_unique<phys::Trailer>();
                t->setup(tt, *c.car, &world, true);
                c.car->attachTrailer(std::move(t));
            }
        }
        auto mi = models.find(e.model);
        if (mi == models.end()) {
            auto m = std::make_shared<VehicleModel>();
            if (!loadVehicle(assets, e.model, VehicleKind::Player, *m)) m.reset();
            else if (!c.car->trailer()) m->trailer.reset();
            mi = models.emplace(e.model, m).first;
        }
        c.model = mi->second;
        c.paint = e.model == "vpcop" ? (cable ? 0 : 1) : i % 4;
        if (c.model) c.lights.setup(*c.model, paintSetFor(c.paint, c.model->setCount()));
        if (c.model) c.car->setPaintSet(paintSetFor(c.paint, c.model->setCount()));
        c.identity = 10000 + 2 + i;
        cops_.push_back(std::move(c));
    }
    logInfo("police: %d of %d cops (density %.2f)", int(cops_.size()), N, double(d));
}

DriverEnv PoliceSystem::env(const std::vector<PolicePlayer>& players) const {
    DriverEnv e;
    e.net = net_;
    e.city = city_;
    e.traffic = traffic_;
    e.world = world_;
    e.propOfCityObject = &propOf_;
    // The players only (the targets list holds the race opponents after them).
    const size_t np = &players == &targets_ ? playerCount_ : players.size();
    for (size_t i = 0; i < np; ++i) e.players.push_back(DriverPlayer{players[i].car, -1});
    e.opponents = oppView_;
    e.now = now_;
    e.firstPlayerReversing = !players.empty() && players[0].car && players[0].car->gearIndex() == 0;
    return e;
}

namespace {
DriverOrders copOrders(const vec3& D, const vec3& Dd, float s1, float s2, float kc) {
    DriverOrders o;
    o.D = D;
    o.Dd = Dd;
    o.laps = 0;
    o.s1 = s1;
    o.s2 = s2;
    o.sw = {false, true, true, false, true, false};
    o.kthr = 1.0f;
    o.kc = kc;
    o.kb = 0.69999999f;
    o.kp = 75.0f;
    return o;
}
}  // namespace

void PoliceSystem::reset() {
    prnd_.reset(1);
    roster_ = Roster();
    for (Cop& c : cops_) resetCop(c);
}

void PoliceSystem::resetCop(Cop& c) {
    // 1. Placement.
    c.car->placeAt(c.entry.pos, c.entry.heading);
    c.car->body().P = vec3();
    c.car->body().L = vec3();
    // 2. State.
    c.state = CopState::Idle;
    c.prev = CopState::None;
    c.tactic = CopTactic::Ram;
    c.perp = -1;
    c.lights.on = false;
    c.route.clear();
    if (sound_) sound_->sirenStop(c);
    std::fill(c.refused.begin(), c.refused.end(), 0);
    // 3. Driver reset, HOLD, then parking orders.
    DriverEnv e = env({});
    c.driver.reset(e);
    c.driver.setMode(DriverMode::Hold);
    c.driver.giveOrders(copOrders(c.entry.pos, vec3(), 0.0f, 5.0f, 2.0f), e);
}

// ---- Roster ----

bool PoliceSystem::registerCop(int c, int p) {
    Roster& r = roster_;
    for (int i = 0; i < r.nCars; ++i)
        if (r.car[i] == p) {
            if (r.nCops[i] >= 3) return false;
            r.cops[i][r.nCops[i]++] = c;
            return true;
        }
    if (r.nCars >= 3) return false;
    const int s = r.nCars;
    r.car[s] = p;
    r.nCops[s] = 1;
    r.cops[s][0] = c;
    r.cops[s][1] = r.cops[s][2] = -1;
    ++r.nCars;
    return true;
}

void PoliceSystem::unregisterCop(int c, int p) {
    Roster& r = roster_;
    for (int i = 0; i < r.nCars; ++i) {
        if (r.car[i] != p) continue;
        for (int j = 0; j < r.nCops[i]; ++j) {
            if (r.cops[i][j] != c) continue;
            for (int k = j; k + 1 < r.nCops[i]; ++k) r.cops[i][k] = r.cops[i][k + 1];
            --r.nCops[i];
            r.cops[i][r.nCops[i]] = -1;
            if (r.nCops[i] == 0) {
                r.car[i] = -1;  // the slot is emptied, not compacted
                --r.nCars;
            }
            return;
        }
        return;
    }
}

int PoliceSystem::chasing() const {
    int n = 0;
    for (const Cop& c : cops_)
        if (c.state != CopState::Idle && c.state != CopState::Out) ++n;
    return n;
}

// ---- One frame ----

void PoliceSystem::update(float F, const std::vector<PolicePlayer>& players, const TrafficSystem* traffic, double now) {
    if (cops_.empty() || !net_) return;
    // Yield marks cleared, every cop completely, yield marks set; the inputs go to the cars before
    // the physics.
    clearMarks();
    updateCops(F, players, traffic, now, {}, {});
    setMarks();
    applyInputs();
}

void PoliceSystem::clearMarks() const {
    if (!net_) return;
    for (const Cop& c : cops_) c.driver.yieldMarks(*net_, false);
}

void PoliceSystem::setMarks() const {
    if (!net_) return;
    for (const Cop& c : cops_) c.driver.yieldMarks(*net_, true);
}

void PoliceSystem::applyInputs() {
    for (Cop& c : cops_) c.car->aiControlStep(c.driver.carInputs());
}

void PoliceSystem::updateCops(float F, const std::vector<PolicePlayer>& players, const TrafficSystem* traffic, double now,
                              const std::vector<PolicePlayer>& opponents, const std::vector<DriverOpponent>& oppView) {
    if (cops_.empty() || !net_) return;
    traffic_ = traffic;
    now_ = now;
    targets_ = players;
    playerCount_ = players.size();
    targets_.insert(targets_.end(), opponents.begin(), opponents.end());
    oppView_ = oppView;
    for (Cop& c : cops_) {
        if (c.refused.size() < opponents.size()) c.refused.resize(opponents.size(), 0);
        updateCop(c, F, targets_);
    }
}

void PoliceSystem::updateCop(Cop& c, float F, const std::vector<PolicePlayer>& players) {
    DriverEnv e = env(players);
    const vec3 cp = c.car->body().p;
    c.mover = false;
    bool skipToDrive = c.state == CopState::Out;
    bool ended = false;
    if (!skipToDrive) {
        if (c.state == CopState::Idle) {
            detection(c, players);
        } else if (c.perp >= 0 && size_t(c.perp) < players.size()) {
            const PolicePlayer& perp = players[size_t(c.perp)];
            const vec3 pp = perp.car->body().p;
            // 1. The perp's place.
            c.perpPlace = net_->componentAt(pp, perp.room);
            // 2. Range.
            const float dx = cp.x - pp.x, dz = cp.z - pp.z;
            c.range = std::sqrt(dx * dx + dz * dz);
            // 3. Rank.
            {
                const Roster& r = roster_;
                int slot = -1;
                for (int i = 0; i < r.nCars; ++i)
                    if (r.car[i] == c.perp) slot = i;
                if (slot < 0) {
                    c.state = CopState::Unknown;
                } else {
                    float best = 1e9f;
                    int nearest = -1;
                    for (int j = 0; j < r.nCops[slot]; ++j) {
                        const int ci = r.cops[slot][j];
                        vec3 q;
                        if (ci >= kOpponentAsCop) {
                            // An opponent MM2 listed as a chasing cop: its car's position.
                            const size_t ti = playerCount_ + size_t(ci - kOpponentAsCop);
                            if (ti >= targets_.size() || !targets_[ti].car) continue;
                            q = targets_[ti].car->body().p;
                        } else {
                            if (ci < 0 || size_t(ci) >= cops_.size()) continue;
                            q = cops_[size_t(ci)].car->body().p;
                        }
                        const float ex = pp.x - q.x, ey = pp.y - q.y, ez = pp.z - q.z;
                        const float d2 = (ez * ez + ey * ey) + ex * ex;
                        if (d2 < best) {
                            best = d2;
                            nearest = ci;
                        }
                    }
                    c.state = (nearest == c.index && c.range <= 25.0f) ? CopState::Apprehend : CopState::Follow;
                }
            }
            // 4. Forced FOLLOW.
            const bool firstReversing = !players.empty() && players[0].car && players[0].car->gearIndex() == 0;
            if (firstReversing || c.driver.mode() == DriverMode::TurnBack || (c.entry.i1 & 15) == 0 ||
                perp.car->speed() < 10.0f)
                c.state = CopState::Follow;
            // 5.
            if (c.state == CopState::Follow) following(c, players);
            else apprehend(c, players);
            // 6-7.
            if (Dg_ < c.range) {
                escapes(c, false);
                ended = true;
            } else if (c.driver.wrecked()) {
                escapes(c, true);
                ended = true;
            }
        }
        if (ended) return;
        // 4. Every non-OUT cop.
        if (c.driver.throttleOut() == 1.0f && c.car->speed() < 50.0f) {
            phys::RigidBody& b = c.car->body();
            b.P = vec3(b.P.x * 1.03f, b.P.y * 1.03f, b.P.z * 1.03f);
        }
        const int room = c.car->collision().room;
        if (room > 0 && size_t(room) < city_->rooms().size() && city_->rooms()[size_t(room)].deepWater) {
            escapes(c, false);
            c.state = CopState::Out;
        }
    }
    // 5. Drive.
    if (c.state == CopState::Apprehend && c.tactic == CopTactic::BlockWait && c.perp >= 0 &&
        size_t(c.perp) < players.size()) {
        mirrorPerp(c, players[size_t(c.perp)]);
    } else {
        c.driver.frame(F, e);
    }
    // 6. A wrecked cop: the perp escapes with explosion, state = OUT; every frame, OUT included (the
    // police part changes nothing more, the sound part plays the explosion again when it has ended).
    if (c.driver.wrecked()) {
        escapes(c, true);
        c.state = CopState::Out;
    }
    // 7. Physics activity.
    float m = 9999999.0f;
    for (const PolicePlayer& p : players) {
        if (!p.car) continue;
        const vec3 d = c.car->body().p - p.car->body().p;
        const float d2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
        if (d2 < m) m = d2;
    }
    c.mover = m < 62500.0f;
    // 8. Fell through.
    if (c.car->body().p.y < -200.0f) resetCop(c);
}

void PoliceSystem::detection(Cop& c, const std::vector<PolicePlayer>& players) {
    const vec3 cp = c.car->body().p;
    const phys::M3& R = c.car->rotation();
    const vec3& X = R.r[0];
    const vec3& Z = R.r[2];
    // 1. The players (the targets list holds the race opponents after them).
    for (size_t i = 0; i < playerCount_ && i < players.size(); ++i) {
        if (!players[i].car) continue;
        const vec3 pp = players[i].car->body().p;
        const vec3 e = cp - pp;
        const float d2 = (e.z * e.z + e.y * e.y) + e.x * e.x;
        if (!(d2 < 5625.0f)) continue;
        const vec3 d = pp - cp;
        const float side = (d.z * X.z + d.y * X.y) + d.x * X.x;
        const float fwd = ((-Z.z) * d.z + (-Z.y) * d.y) + (-Z.x) * d.x;
        const double a = std::atan2(double(side), double(fwd));
        if (!(-double(1.57f) < a && a < double(1.57f))) continue;
        if (!registerCop(c.index, int(i))) continue;
        startChasing(c, int(i), players);
        return;
    }
    // 2. Opponents (race modes): within 75 m, in front, and the first player within the cop's
    // opponent range (horizontal); one draw against the chase chance.
    if (playerCount_ == 0 || !players[0].car) return;
    const vec3 p0 = players[0].car->body().p;
    for (size_t i = playerCount_; i < players.size(); ++i) {
        const size_t k = i - playerCount_;
        if (k < c.refused.size() && c.refused[k]) continue;
        if (!players[i].car) continue;
        const vec3 op = players[i].car->body().p;
        const vec3 e = cp - op;
        const float d2 = (e.z * e.z + e.y * e.y) + e.x * e.x;
        if (!(d2 < 5625.0f)) continue;
        const vec3 d = op - cp;
        const float side = (d.z * X.z + d.y * X.y) + d.x * X.x;
        const float fwd = ((-Z.z) * d.z + (-Z.y) * d.y) + (-Z.x) * d.x;
        const double a = std::atan2(double(side), double(fwd));
        if (!(-double(1.57f) < a && a < double(1.57f))) continue;
        const float hx = cp.x - p0.x, hz = cp.z - p0.z;
        if (!(hx * hx + hz * hz < c.entry.f1 * c.entry.f1)) continue;
        const float r = prnd_.unit();
        if (r <= c.entry.f0) {
            // MM2 registers the opponent the wrong way round: as a chasing "cop" of the car this
            // cop chased last.
            if (registerCop(kOpponentAsCop + int(k), c.lastPerp)) {
                startChasing(c, int(i), players);
                return;
            }
        } else if (k < c.refused.size()) {
            c.refused[k] = 1;
        }
    }
}

void PoliceSystem::startChasing(Cop& c, int p, const std::vector<PolicePlayer>& players) {
    const PolicePlayer& perp = players[size_t(p)];
    c.perp = p;
    c.lastPerp = p;
    c.driver.setMode(DriverMode::Drive);
    c.state = CopState::Follow;
    const vec3 cp = c.car->body().p, pp = perp.car->body().p;
    const float dx = cp.x - pp.x, dz = cp.z - pp.z;
    c.range = std::sqrt(dx * dx + dz * dz);
    c.perpPlace = net_->componentAt(pp, 0);
    following(c, players);
}

void PoliceSystem::giveOrders(Cop& c, const vec3& D, const vec3& Dd, float s1, float s2, float kc,
                              const PolicePlayer& perp) {
    RouteQuery q;
    q.start = c.car->body().p;
    q.startZ = c.car->rotation().r[2];
    q.startRoom = c.car->collision().room;
    q.goal = D;
    q.goalRoom = perp.room;
    q.mode = RouteStart::Free;
    routeSearch(*net_, q, c.route);
    DriverOrders o = copOrders(D, Dd, s1, s2, kc);
    o.route = c.route;
    DriverEnv e = env({});
    c.driver.giveOrders(o, e);
}

void PoliceSystem::following(Cop& c, const std::vector<PolicePlayer>& players) {
    if (c.state != c.prev) {
        c.prev = c.state;
        if (c.lights.any()) c.lights.on = true;  // and the siren sound starts (sound spec)
        if (sound_) sound_->sirenStart(c, c.perp == 0);
    }
    const DriverMode m = c.driver.mode();
    if (m != DriverMode::Drive && m != DriverMode::Recover) return;
    const PolicePlayer& perp = players[size_t(c.perp)];
    const phys::Car& pc = *perp.car;
    const vec3 Z = pc.rotation().r[2];
    giveOrders(c, pc.body().p, vec3(-Z.x, -Z.y, -Z.z), (pc.speed() + c.range) - 12.5f, 5.0f, 2.0f, perp);
}

void PoliceSystem::apprehend(Cop& c, const std::vector<PolicePlayer>& players) {
    if (c.state != c.prev) {
        c.prev = c.state;
        prnd_.unit();  // drawn and discarded
        c.tactic = CopTactic::Block;
    }
    const DriverMode m = c.driver.mode();
    if (m != DriverMode::Drive && m != DriverMode::Recover) return;
    const PolicePlayer& perp = players[size_t(c.perp)];
    const phys::Car& pc = *perp.car;
    const vec3 P = pc.body().p;
    const vec3& X = pc.rotation().r[0];
    const vec3& Z = pc.rotation().r[2];
    const vec3 cp = c.car->body().p;
    const vec3 v = cp - P;
    const float ahead = (((-Z.x) * v.x + (-Z.z) * v.z) + (-Z.y) * v.y);
    if (c.tactic == CopTactic::BlockWait) {
        if (ahead < 0.0f) c.tactic = CopTactic::Block;
        return;
    }
    // BLOCK.
    const float back = perp.boxMax.z, left = -perp.boxMin.x;
    const float right = c.driver.right();
    vec3 T;
    float s1;
    if (!(ahead < -back)) {
        T = vec3(P.x - (12.0f * Z.x), P.y - (12.0f * Z.y), P.z - (12.0f * Z.z));
        s1 = pc.speed();
    } else {
        const float side = (v.z * X.z + v.y * X.y) + v.x * X.x;
        const float w = (right + left) + 1.0f;
        const vec3 B(back * Z.x + P.x, back * Z.y + P.y, back * Z.z + P.z);
        const vec3 Rp(B.x + w * X.x, B.y + w * X.y, B.z + w * X.z);
        const vec3 Lp(B.x - w * X.x, B.y - w * X.y, B.z - w * X.z);
        bool useL = side <= 0.0f;
        // MM2's swapped place fields: "index 1" and road number = the kind (0 … 3).
        if (ahead <= -20.0f && c.perpPlace.index == 1) {
            const int road = c.perpPlace.kind;
            if (road >= 0 && size_t(road) < net_->roads().size()) {
                const int tR = net_->onRoad(road, Rp, c.driver.left());
                const int tL = net_->onRoad(road, Lp, c.driver.right());
                if (tR == tL) useL = side <= 0.0f;
                else useL = tR > tL;
            }
        }
        T = useL ? Lp : Rp;
        s1 = pc.speed() + 25.0f;
    }
    giveOrders(c, T, vec3(-Z.x, -Z.y, -Z.z), s1, 0.0f, 1.0f, perp);
    const float dx = T.x - cp.x, dz = T.z - cp.z;
    if (dx * dx + dz * dz < 9.0f) c.tactic = CopTactic::BlockWait;
}

void PoliceSystem::mirrorPerp(Cop& c, const PolicePlayer& perp) {
    const phys::Car& pc = *perp.car;
    const float u = pc.speed(), cs = c.car->speed();
    const vec3 Fv = -pc.rotation().r[2];
    const vec3& X = c.car->rotation().r[0];
    const vec3& Z = c.car->rotation().r[2];
    const float s = u - 3.0f;
    float throttle = 0.0f, brake = 0.0f;
    bool scaleL = false;
    if (s < cs) {
        const float b = (cs - s) / (1.2f * 19.8f);
        if (b > 0.3f) {
            brake = b < 0.0f ? 0.0f : (b > 1.0f ? 1.0f : b);
            scaleL = true;
        }
    } else {
        throttle = 0.5f;
    }
    const double a = std::atan2(double(Fv.z * X.z + Fv.x * X.x), double(-(Fv.z * Z.z + Fv.x * Z.x)));
    float steer = float(double(float(a * double(1.33f))) * double(1.428f));
    if (!(steer >= -1.0f)) steer = -1.0f;
    else if (steer > 1.0f) steer = 1.0f;
    c.driver.mirror(pc.body().p, steer, throttle, brake, scaleL);
}

void PoliceSystem::escapes(Cop& c, bool explosion) {
    if (sound_) {
        if (explosion) sound_->explosion(c);
        sound_->sirenStop(c);
    }
    if (c.perp >= 0) unregisterCop(c.index, c.perp);
    c.state = CopState::Idle;
    c.driver.setMode(DriverMode::Hold);
}

bool PoliceSystem::copInSight(const phys::Car& player, phys::World& world) const {
    const vec3 pb = player.body().p;
    for (const Cop& c : cops_) {
        if (c.state == CopState::Idle) continue;
        const vec3 qb = c.car->body().p;
        const vec3 P(pb.x, pb.y + 3.5f, pb.z), Q(qb.x, qb.y + 3.5f, qb.z);
        phys::ProbeState ps;
        phys::ProbeHit hit;
        if (world.probe(P, Q, ps, hit)) continue;
        const float dx = P.x - Q.x, dy = P.y - Q.y, dz = P.z - Q.z;
        const float d = std::sqrt(((dy * dy) + (dz * dz)) + dx * dx);
        if (d < 200.0f || std::isnan(d)) return true;
    }
    return false;
}

// ---- Physics ----

void PoliceSystem::appendMovers(std::vector<phys::SceneMover>& movers) {
    for (Cop& c : cops_) {
        if (!c.mover) continue;
        phys::SceneMover m;
        m.kind = phys::SceneMover::CarMover;
        m.car = c.car.get();
        m.number = 2 + c.index;
        m.identity = c.identity;
        m.box = c.box;
        m.shape = c.shape;
        movers.push_back(m);
    }
}

void PoliceSystem::afterPhysics(const std::vector<phys::SceneMover>& movers, float F) {
    for (Cop& c : cops_) {
        c.touchedPlayer = false;
        for (const phys::SceneMover& m : movers)
            if (m.kind == phys::SceneMover::CarMover && m.car == c.car.get()) c.touchedPlayer = m.touchedPlayer;
        c.driver.setTouchedPlayer(c.touchedPlayer);
        if (c.mover) c.lights.update(F, c.car->forward());
    }
}

}  // namespace ai
}  // namespace cc
