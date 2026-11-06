// Ambient traffic: types, pool, population, lane lists, the frame loop and normal driving
// (spec/ai/traffic.md, traffic-population.md, traffic-driving.md, formats/aivehicledata.md).
#include "ai/traffic.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ai/traffic_control.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/dat_text.h"
#include "model/asset_cache.h"
#include "model/vehicle.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace ai {

namespace {

const std::vector<int> kNoCars;

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

bool loadTypeData(const Vfs& vfs, const std::string& name, TrafficType& t) {
    BytesPtr b = vfs.open("tune/vehicle/" + name + ".aiVehicleData");
    if (!b) return false;
    using dat::FieldType;
    dat::Schema s{{{"Mass", FieldType::Float, &t.mass, nullptr},
                   {"Size", FieldType::Vector, &t.size, nullptr},
                   {"CG", FieldType::Vector, &t.cg, nullptr},
                   {"Spring", FieldType::Float, &t.spring, nullptr},
                   {"Damping", FieldType::Float, &t.damping, nullptr},
                   {"Limit", FieldType::Float, &t.limit, nullptr},
                   {"RubberSpring", FieldType::Float, &t.rubberSpring, nullptr},
                   {"RubberDamp", FieldType::Float, &t.rubberDamp, nullptr},
                   {"MaxDamage", FieldType::Float, &t.maxDamage, nullptr},
                   {"Elasticity", FieldType::Float, &t.elasticity, nullptr},
                   {"Friction", FieldType::Float, &t.friction, nullptr},
                   {"PtxThresh", FieldType::Float, &t.ptxThresh, nullptr},
                   {"MaxAng", FieldType::Vector, &t.maxAng, nullptr}}};
    std::string err;
    if (!dat::parseText(textOf(b), s, &err)) {
        logFile(LogLevel::Warn, vfs.storedName("tune/vehicle/" + name + ".aiVehicleData"), err);
        return false;
    }
    return true;
}

void deriveType(TrafficType& t) {
    const vec3 h(t.size.x * 0.5f, t.size.y * 0.5f, t.size.z * 0.5f);
    t.boxMin = vec3(t.cg.x + (-t.size.x * 0.5f), t.cg.y + (-t.size.y * 0.5f), t.cg.z + (-t.size.z * 0.5f));
    t.boxMax = vec3(t.cg.x + t.size.x * 0.5f, t.cg.y + t.size.y * 0.5f, t.cg.z + t.size.z * 0.5f);
    t.boxRadius = std::sqrt((h.z * h.z + h.y * h.y) + h.x * h.x);
    t.front = -t.boxMin.z;
    t.back = t.boxMax.z;
    t.left = -t.boxMin.x;
    t.right = t.boxMax.x;
}

}  // namespace

TrafficSystem::TrafficSystem() = default;
TrafficSystem::~TrafficSystem() = default;

// ---------------------------------------------------------------------------------------------
// Types and pool

void TrafficSystem::create(const Vfs& vfs, AssetCache& assets, CityData& city, phys::CollisionWorld& world, float density,
                           int poolSize) {
    city_ = &city;
    net_ = city.network();
    world_ = &world;
    world.clearDynamicProps();
    cables_.clear();
    cableType_ = -1;
    density_ = std::clamp(density, 0.0f, 1.0f);
    d_ = density_ * 0.2f;
    types_.clear();
    cars_.clear();
    free_.clear();
    bodies_.clear();
    activeRoads_.clear();
    blinkClock_ = 0;
    frame_ = 0;
    stats_ = Stats();
    if (!net_) return;
    laneChanges_ = net_->raceAimap().laneChanges;
    const auto& roads = net_->roads();
    lanes_.assign(roads.size(), {});
    sectionCars_.assign(roads.size(), {});
    for (size_t r = 0; r < roads.size(); ++r) {
        lanes_[r].resize(2);
        sectionCars_[r].resize(2);
        for (int s = 0; s < 2; ++s) {
            lanes_[r][size_t(s)].resize(size_t(std::max(0, roads[r].side[s].lanes)));
            sectionCars_[r][size_t(s)].resize(size_t(roads[r].n));
        }
    }
    ixCars_.assign(net_->intersections().size(), {});
    wanted_.assign(roads.size(), 0);
    playerRoom_.clear();
    if (density_ <= 0.0f) {  // MM2 creates no traffic cars at all
        if (cableSwitch_) createCableCars(vfs, assets);
        return;
    }

    // The mix (traffic-population.md): the race file's list if it has entries.
    const std::vector<AmbientType>& mix = net_->ambientTypes();
    std::map<std::string, int> typeIndex;
    auto typeFor = [&](const std::string& name) -> int {
        auto it = typeIndex.find(name);
        if (it != typeIndex.end()) return it->second;
        TrafficType t;
        t.name = name;
        int idx = -1;
        auto model = std::make_shared<VehicleModel>();
        if (int(types_.size()) < 32 && loadVehicle(assets, name, VehicleKind::Traffic, *model)) {
            bool wheels = true;
            for (int w = 0; w < 4; ++w)
                if (!model->part(strf("WHL%d", w)) || !model->part(strf("WHL%d", w))->any()) wheels = false;
            if (wheels) {
                t.model = model;
                t.dataFound = loadTypeData(vfs, name, t);
                if (!t.dataFound) {
                    // MM2 reuses the data of the first type loaded this session.
                    if (!types_.empty()) {
                        const TrafficType& f = types_[0];
                        t.mass = f.mass, t.size = f.size, t.cg = f.cg, t.spring = f.spring, t.damping = f.damping;
                        t.limit = f.limit, t.rubberSpring = f.rubberSpring, t.rubberDamp = f.rubberDamp;
                        t.maxDamage = f.maxDamage, t.elasticity = f.elasticity, t.friction = f.friction;
                        logFile(LogLevel::Warn, "tune/vehicle/" + name + ".aiVehicleData",
                                "no vehicle data; uses " + f.name + "'s");
                    } else {
                        logFile(LogLevel::Warn, "tune/vehicle/" + name + ".aiVehicleData", "no vehicle data; type skipped");
                        wheels = false;
                    }
                }
            } else {
                logFile(LogLevel::Warn, "geometry/" + name + ".pkg", "traffic car without BODY or WHL0-WHL3; not used");
            }
            if (wheels) {
                deriveType(t);
                for (int w = 0; w < 6; ++w) {
                    const Pivot* pv = model->pivotOf(strf("WHL%d", w));
                    t.hasWheel[w] = pv && pv->found;
                    if (t.hasWheel[w]) t.wheelPos[w] = pv->origin;
                    const PartLods* pl = model->part(strf("WHL%d", w));
                    t.wheelMesh[w] = pl && pl->at(LodH);
                    if (w == 0 && pv && pv->found) t.wheelRadius = std::fabs((pv->boxMax.y - pv->boxMin.y) * 0.5f);
                }
                if (const PartLods* body = model->part("BODY"))
                    for (int l = 0; l < LodCount; ++l)
                        if (body->at(Lod(l))) t.bodyRadius = std::max(t.bodyRadius, body->at(Lod(l))->radius);
                t.setCount = std::max(1, model->setCount());
                // The physics of a knocked car (car-collision.md 6.1): wheels WHL0-WHL3 only.
                t.body.mass = t.mass;
                t.body.size = t.size;
                t.body.cg = t.cg;
                t.body.spring = t.spring;
                t.body.damping = t.damping;
                t.body.limit = t.limit;
                t.body.rubberSpring = t.rubberSpring;
                t.body.rubberDamp = t.rubberDamp;
                for (int w = 0; w < 4; ++w) t.body.wheel[w] = t.wheelPos[w];
                t.body.wheelRadius = t.wheelRadius;
                t.body.bodyRadius = t.bodyRadius;
                phys::setupTrafficBodyType(t.body);
                idx = int(types_.size());
                types_.push_back(std::move(t));
            }
        }
        typeIndex[name] = idx;
        return idx;
    };

    // Pool creation (traffic.md, "Randomness"): the generator restarts, then the draws in order.
    rng_.reset(1);
    cars_.resize(size_t(std::max(0, poolSize)));
    for (size_t i = 0; i < cars_.size(); ++i) {
        TrafficCar& c = cars_[i];
        c.index = int(i);
        const float r1 = rng_.unit();
        c.o = float(std::sin(double(r1 * 6.2831f))) * 0.5f;
        const float r2 = rng_.unit();
        c.T0 = 8 + int(std::trunc(r2 * 17.0f));
        c.blink = int((i * 7) % 16);
    }
    float speedCounter = 0;  // engine: reset whenever the pool is created
    for (size_t i = 0; i < cars_.size(); ++i) {
        TrafficCar& c = cars_[i];
        if (!mix.empty()) {
            const float r = rng_.unit();
            for (const AmbientType& t : mix)
                if (t.threshold > r) {
                    c.type = typeFor(t.model);
                    break;
                }
        }
        const int n = c.type >= 0 ? types_[size_t(c.type)].setCount : 1;
        for (int k = 0; k < 2; ++k) c.paint = int(std::trunc(rng_.unit() * float(n - 1)));
        const float r = rng_.unit();
        c.A = r * 3.0f + 5.0f;
        c.G = r * 2.5f + 0.5f;
        c.E = speedCounter + speedCounter;
        speedCounter = speedCounter - 1.0f;
        if (speedCounter < 0.0f) speedCounter = 4.0f;
    }
    logInfo("traffic: pool of %zu cars, %zu types, density %.2f", cars_.size(), types_.size(), double(density_));
    // The cable cars continue the level objects' generator after MM2's traffic-pool and
    // pedestrian-pool creation draws (cable-cars.md, "Which cable cars exist" step 3): 2 per pool
    // car; with traffic on and a car mix, 1 type draw per car and 3 more per car that got a type;
    // 2 per pedestrian of the pool.
    {
        const int N = int(cars_.size());
        int typed = 0;
        for (const TrafficCar& c : cars_)
            if (c.type >= 0) ++typed;
        cableSkip_ = 2 * N + ((density_ > 0.0f && !mix.empty()) ? N + 3 * typed : 0) + 2 * pedPool_;
    }
    if (cableSwitch_) createCableCars(vfs, assets);
}

void TrafficSystem::clear() {
    if (net_) net_->control().reset();
    if (world_) world_->clearDynamicProps();
    cables_.clear();
    cableType_ = -1;
    net_ = nullptr;
    density_ = d_ = 0;
    types_.clear();
    cars_.clear();
    free_.clear();
    bodies_.clear();
    activeRoads_.clear();
    lanes_.clear();
    sectionCars_.clear();
    ixCars_.clear();
}

// ---------------------------------------------------------------------------------------------
// Lane lists

std::vector<int>& TrafficSystem::laneList(int road, int side, int lane) {
    return lanes_[size_t(road)][size_t(sideSlot(side))][size_t(lane)];
}

const std::vector<int>& TrafficSystem::laneList(int road, int side, int lane) const {
    if (road < 0 || size_t(road) >= lanes_.size()) return kNoCars;
    const auto& s = lanes_[size_t(road)][size_t(sideSlot(side))];
    if (lane < 0 || size_t(lane) >= s.size()) return kNoCars;
    return s[size_t(lane)];
}

void TrafficSystem::sortedInsert(int car, int road, int side, int lane, float x) {
    auto& l = laneList(road, side, lane);
    size_t i = 0;
    while (i < l.size() && !(cars_[size_t(l[i])].x < x)) ++i;
    l.insert(l.begin() + long(i), car);
}

void TrafficSystem::append(int car, int road, int side, int lane) { laneList(road, side, lane).push_back(car); }

void TrafficSystem::removeFrom(int car, int road, int side, int lane) {
    if (road < 0 || size_t(road) >= lanes_.size()) return;
    auto& s = lanes_[size_t(road)][size_t(sideSlot(side))];
    if (lane < 0 || size_t(lane) >= s.size()) return;
    auto& l = s[size_t(lane)];
    auto it = std::find(l.begin(), l.end(), car);
    if (it != l.end()) l.erase(it);
}

int TrafficSystem::carAheadInList(int car, int road, int side, int lane) const {
    const auto& l = laneList(road, side, lane);
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i] == car) return i == 0 ? -1 : l[i - 1];
    return -1;
}

int TrafficSystem::carAhead(int car) const {
    const TrafficCar& c = cars_[size_t(car)];
    return carAheadInList(car, c.road, c.side, c.listLane);
}

float TrafficSystem::laneLen(int road, int side, int lane) const { return net_->laneLength(road, side, lane); }

// ---------------------------------------------------------------------------------------------
// Population (traffic-population.md)

bool TrafficSystem::isActive(int road) const {
    return std::find(activeRoads_.begin(), activeRoads_.end(), road) != activeRoads_.end();
}

void TrafficSystem::reset(const std::vector<TrafficPlayer>& players) {
    if (!net_) return;
    rng_.reset(1);
    events_.clear();
    while (!bodies_.empty()) dropBody(bodies_.back().car);
    rowMovers_.clear();
    for (auto& r : lanes_)
        for (auto& s : r)
            for (auto& l : s) l.clear();
    activeRoads_.clear();
    std::fill(wanted_.begin(), wanted_.end(), 0);
    free_.clear();
    for (auto& r : sectionCars_)
        for (auto& sd : r)
            for (auto& l : sd) l.clear();
    for (auto& l : ixCars_) l.clear();
    fileSeq_ = 0;
    for (TrafficCar& c : cars_) {
        if (c.cable) continue;  // the cable cars have their own reset
        c.regKind = 0;
        c.netRoom = 0;
        c.filedRoom = 0;
        c.filed = 0;
        c.onStreets = false;
        c.room = 0;
        c.v = 0;
        c.a = 0;
        c.state = c.prevState = TrafficDriving;
        c.tick = 0;
        c.body = -1;
        c.road = -1;
        c.next = NextChoice();
        if (c.type >= 0) free_.push_back(c.index);  // pushed in index order: car N − 1 on top
    }
    net_->control().reset();
    resetCableCars();
    playerRoom_.assign(players.size(), 0);
    for (size_t p = 0; p < players.size(); ++p) {
        // Find(q + CoG, 0): the body position the car is placed at (traffic-population.md).
        const int room = city_->findRoom(players[p].bodyPos, 0);
        if (room != 0) adjust(0, room, int(p));
        playerRoom_[p] = room;
    }
}

void TrafficSystem::resetAtRoom(int room) {
    reset({});
    playerRoom_.assign(1, room);
    if (room != 0) adjust(0, room, 0);
}

void TrafficSystem::adjust(int oldRoom, int newRoom, int player) {
    const auto& oldList = net_->ambientRoads(oldRoom);
    const auto& newList = net_->ambientRoads(newRoom);
    std::vector<int> leaving, entering;
    for (uint16_t r : oldList)
        if (std::find(newList.begin(), newList.end(), r) == newList.end()) leaving.push_back(r);
    for (uint16_t r : newList)
        if (std::find(oldList.begin(), oldList.end(), r) == oldList.end()) entering.push_back(r);
    const auto& roads = net_->roads();
    const uint8_t bit = uint8_t(1u << (player & 3));
    for (int r : leaving) {
        if (r < 0 || size_t(r) >= roads.size() || !isActive(r)) continue;
        wanted_[size_t(r)] = uint8_t(wanted_[size_t(r)] & ~bit);
        if (wanted_[size_t(r)] == 0) {
            const int before = int(free_.size());
            emptyRoad(r);
            events_.push_back({r, false, int(free_.size()) - before});
            activeRoads_.erase(std::find(activeRoads_.begin(), activeRoads_.end(), r));
        }
    }
    auto exceptionOf = [&](int r) -> const RoadException* {
        for (const RoadException& e : net_->raceAimap().exceptions)
            if (e.road == r) return &e;
        return nullptr;
    };
    float L = 0;
    for (int r : entering) {
        if (r < 0 || size_t(r) >= roads.size() || exceptionOf(r)) continue;
        const Road& rd = roads[size_t(r)];
        const float cl = rd.CD[size_t(rd.n - 1)] - rd.CD[0];
        if (!(rd.side[1].flags & 1)) L = ((cl - 5.0f) * float(rd.side[1].lanes)) + L;
        if (!(rd.side[0].flags & 1)) L = ((cl - 5.0f) * float(rd.side[0].lanes)) + L;
    }
    stats_.lastLaneLength = L;
    const float t = (L * d_) * -0.125f;
    const int n = -int(t);
    const float s = L / float(n + 1);
    float acc = 0, carry = 0;
    for (int r : entering) {
        if (r < 0 || size_t(r) >= roads.size()) continue;
        if (wanted_[size_t(r)] != 0) continue;  // another player has it populated
        wanted_[size_t(r)] = uint8_t(wanted_[size_t(r)] | bit);
        activeRoads_.insert(activeRoads_.begin(), r);
        const int placedBefore = stats_.placed;
        struct Tail {
            std::vector<FillEvent>& ev;
            int road;
            const int& placed;
            int before;
            ~Tail() { ev.push_back({road, true, placed - before}); }
        } tail{events_, r, stats_.placed, placedBefore};
        const Road& rd = roads[size_t(r)];
        const RoadException* ex = exceptionOf(r);
        for (int side : {-1, 1}) {
            const RoadSide& sd = rd.sideOf(side);
            if (sd.flags & 1) continue;
            if (ex) {
                const float L0 = laneLen(r, 1, 0);
                const int k = int(std::trunc(L0 * ex->density / 8.0f));
                for (int l = 0; l < sd.lanes; ++l) {
                    const float g = laneLen(r, side, l) / float(k + 1);
                    for (int i = 1; i <= k; ++i) {
                        if (free_.empty()) break;
                        const float rr = rng_.unit();
                        const float x = ((float(i) * g) - g * 0.5f) +
                                        ((g - 10.0f) * float(std::sin(double(rr * 6.28f)))) * 0.5f;
                        if (!place(r, side, l, x)) break;
                    }
                }
                continue;
            }
            const float lam = (rd.CD[size_t(rd.n - 1)] - rd.CD[0]) - 5.0f;
            for (int l = 0; l < sd.lanes; ++l) {
                acc = lam + acc;
                float o = 0;
                bool stop = false;
                while (acc > s + 0.001f) {
                    acc = acc - s;
                    o = (s - carry) + o;
                    carry = 0;
                    if (free_.empty()) {
                        stop = true;
                        break;
                    }
                    const TrafficCar& c = cars_[size_t(free_.back())];
                    const float front = types_[size_t(c.type)].front;
                    const float x = std::min(std::max(o, 0.0f), lam - front);
                    if (!place(r, side, l, x)) {
                        stop = true;
                        break;
                    }
                }
                (void)stop;
                carry = acc;
            }
        }
    }
}

bool TrafficSystem::place(int road, int side, int lane, float x) {
    if (free_.empty()) return false;
    const int ci = free_.back();
    TrafficCar& c = cars_[size_t(ci)];
    c.road = road;
    c.side = side;
    c.lane = lane;
    c.listLane = lane;
    c.x = x;
    c.next = net_->chooseNext(road, side, lane, rng_);
    if (c.next.road < 0) return false;  // the car stays in the free list; filling stops for this lane
    // 4. A race opponent closer than 50 m to the route point (height 0) skips this position: the
    // car stays at the front of the free list for the next one.
    if (opponentPos_ && !opponentPos_->empty()) {
        vec3 p = net_->pointAtRouteDistance(road, side, lane, c.o, types_[size_t(c.type)].front, x, c.next);
        p.y = 0.0f;
        for (const vec3& q : *opponentPos_) {
            const float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
            // (d.z·d.z + d.y·d.y) + d.x·d.x < 2500.0, or a NaN, skips (traffic-population.md).
            if (!((dz * dz + dy * dy) + dx * dx >= 2500.0f)) return true;
        }
    }
    free_.pop_back();
    c.onStreets = true;
    c.v = 0;
    c.a = 0;
    c.vt = 0;
    c.state = c.prevState = TrafficDriving;
    c.tick = 0;
    c.react = int16_t(c.T0);
    c.wheelAngle = 0;
    c.attempts = 1;
    c.rejoinPos = vec3(0, 0, 0);
    c.body = -1;
    c.groundSolved = false;
    c.solvedNear.fill(false);
    c.rail = 0;
    c.k = 1;
    c.u = 0;
    c.go = c.queued = false;
    c.netRoom = 0;
    c.placeRoad = road;
    c.placeSide = side;
    c.placeK = 1;
    for (auto& p : c.probes) p = phys::ProbeState();
    sortedInsert(ci, road, side, lane, x);
    ++stats_.placed;
    if (sound_) sound_->placed(c);
    return true;
}

void TrafficSystem::freeCar(int ci) {
    TrafficCar& c = cars_[size_t(ci)];
    if (c.body >= 0) endPhysics(ci);
    unregister(c);  // engine: MM2 forgets the registration without removing it
    net_->control().removeCar(ci);
    c.onStreets = false;
    c.room = 0;
    c.v = 0;
    c.road = -1;
    free_.push_back(ci);
}

void TrafficSystem::emptyRoad(int road) {
    const Road& rd = net_->roads()[size_t(road)];
    // Every section list of the road is emptied first (traffic-population.md).
    for (auto& sd : sectionCars_[size_t(road)])
        for (auto& l : sd) {
            for (int ci : l) {
                TrafficCar& o = cars_[size_t(ci)];
                if (o.regKind == 1 && o.regRoad == road) o.regKind = 0;
            }
            l.clear();
        }
    for (int side : {1, -1})
        for (int l = 0; l < rd.sideOf(side).lanes; ++l) {
            std::vector<int> list = laneList(road, side, l);
            laneList(road, side, l).clear();
            for (int ci : list) {
                TrafficCar& c = cars_[size_t(ci)];
                if (c.road != road || c.side != side || c.listLane != l) continue;
                if (c.rail == 2 && c.lane != l) removeFrom(ci, road, side, c.lane);
                freeCar(ci);
                ++stats_.emptied;
            }
        }
}

int TrafficSystem::carsOnStreets() const {
    int n = 0;
    for (const TrafficCar& c : cars_)
        if (c.onStreets) ++n;
    return n;
}

// ---------------------------------------------------------------------------------------------
// The frame (traffic.md, "Frame order")

const std::vector<int>& TrafficSystem::sectionCars(int road, int side, int section) const {
    if (road < 0 || size_t(road) >= sectionCars_.size()) return kNoCars;
    const auto& s = sectionCars_[size_t(road)][size_t(sideSlot(side))];
    if (section < 0 || size_t(section) >= s.size()) return kNoCars;
    return s[size_t(section)];
}

const std::vector<int>& TrafficSystem::intersectionCars(int i) const {
    if (i < 0 || size_t(i) >= ixCars_.size()) return kNoCars;
    return ixCars_[size_t(i)];
}

void TrafficSystem::update(float F, const std::vector<TrafficPlayer>& players, bool late) {
    updateCars(F, players);
    if (late) lateUpdate(F, players);
}

void TrafficSystem::updateCars(float F, const std::vector<TrafficPlayer>& players) {
    if (!net_) return;
    ++frame_;
    events_.clear();
    // 1.1 Players' rooms.
    if (playerRoom_.size() != players.size()) playerRoom_.resize(players.size(), 0);
    if (active())
        for (size_t p = 0; p < players.size(); ++p) {
            // Find(P_c, previous room) with P_c the body position; stored every frame, room 0
            // included (traffic-population.md, "Rooms and traffic roads").
            const int room = city_->findRoom(players[p].bodyPos, playerRoom_[p]);
            if (room != 0 && room != playerRoom_[p]) adjust(playerRoom_[p], room, int(p));
            playerRoom_[p] = room;
        }
    // 1.2 Traffic cars.
    rowMovers_.clear();
    if (active()) {
        const std::vector<int> roads = activeRoads_;
        for (int r : roads) {
            if (!isActive(r)) continue;
            const Road& rd = net_->roads()[size_t(r)];
            for (int side : {-1, 1})
                for (int l = 0; l < rd.sideOf(side).lanes; ++l) {
                    const std::vector<int> list = laneList(r, side, l);
                    for (int ci : list) {
                        TrafficCar& c = cars_[size_t(ci)];
                        if (!c.onStreets || c.updatedFrame == frame_) continue;
                        if (c.road != r || c.side != side || c.listLane != l) continue;
                        updateCar(ci, F, players);
                        // Row 3 of car-collision.md 1.1: avoiding/rejoining cars and wrecks without a body.
                        const TrafficCar& u = cars_[size_t(ci)];
                        if (u.onStreets && u.body < 0 &&
                            (u.state == TrafficAvoiding || u.state == TrafficRejoining || (u.state == TrafficKnocked && u.wreck)))
                            rowMovers_.push_back(ci);
                    }
                }
        }
        // Cars standing outside every lane list (out after a failed rejoin) are left alone.
    }
}

void TrafficSystem::lateUpdate(float F, const std::vector<TrafficPlayer>& players) {
    if (!net_) return;
    // 5. The cable cars (after the opponents and police).
    updateCableCars(F, players);
    finishFrame(F);
}

void TrafficSystem::finishFrame(float F) {
    if (!net_) return;
    // 6. Body bookkeeping, the blink clock, then the light sets.
    blinkClock_ = blinkClock_ + F;
    // A car entering another room is filed at the front of that room's object list.
    for (TrafficCar& c : cars_)
        if (c.onStreets && c.room != c.filedRoom) {
            c.filedRoom = c.room;
            c.filed = world_ ? world_->fileStamp() : ++fileSeq_;  // the room lists' clock
        }
    net_->control().update(F);
    processWakes();
}

void TrafficSystem::processWakes() {
    auto& w = net_->control().wakes();
    for (const WakeEvent& e : w) {
        if (e.road < 0) {
            // Always green: every road's waiting cars on that side number.
            for (int r : activeRoads_) {
                const Road& rd = net_->roads()[size_t(r)];
                for (int l = 0; l < rd.sideOf(e.side).lanes; ++l) wakeLane(r, e.side, l, -1);
            }
            continue;
        }
        if (e.road >= int(lanes_.size())) continue;
        if (e.car >= 0 && size_t(e.car) < cars_.size()) {
            // A free approach: the car and the cars behind it in its lane list.
            wakeLane(e.road, e.side, cars_[size_t(e.car)].listLane, e.car);
            continue;
        }
        const Road& rd = net_->roads()[size_t(e.road)];
        for (int l = 0; l < rd.sideOf(e.side).lanes; ++l) wakeLane(e.road, e.side, l, -1);
    }
    w.clear();
}

// traffic-control.md, "Waking a road": walk a lane list from its front car (or from `fromCar`) back
// through the list; cars below 0.01 m/s get react = 0, faster ones are passed over. The step from a
// car to the one behind it follows the car's target-lane link (a lane-changing car leads the walk
// into its target lane's list).
void TrafficSystem::wakeLane(int road, int side, int lane, int fromCar) {
    const auto& first = laneList(road, side, lane);
    int ci = fromCar >= 0 ? fromCar : (first.empty() ? -1 : first.front());
    for (int guard = 0; ci >= 0 && guard < int(cars_.size()) + 1; ++guard) {
        TrafficCar& c = cars_[size_t(ci)];
        if (c.v < 0.01f) c.react = 0;
        const auto& l = laneList(road, side, c.lane);
        auto it = std::find(l.begin(), l.end(), ci);
        ci = (it == l.end() || it + 1 == l.end()) ? -1 : *(it + 1);
    }
}

void TrafficSystem::setState(TrafficCar& c, int state) {
    c.prevState = c.state;
    c.state = state;
    c.tick = 0;
}

void TrafficSystem::updateCar(int ci, float F, const std::vector<TrafficPlayer>& players) {
    TrafficCar& c = cars_[size_t(ci)];
    c.updatedFrame = frame_;
    switch (c.state) {
    case TrafficDriving: driving(c, F, players); break;
    case TrafficKnocked: knocked(c); break;
    case TrafficRejoining: rejoining(c, F, players); break;
    case TrafficAvoiding: avoiding(c, F, players); break;
    default: break;
    }
    if (!c.onStreets) return;
    // Route place (traffic-reactions.md; driver-planner.md §18.5).
    if (c.state != TrafficKnocked && c.state != TrafficAvoiding && c.road >= 0) {
        c.placeRoad = c.road;
        c.placeSide = c.side;
        c.placeK = c.rail == 1 ? net_->roads()[size_t(c.road)].n : c.k;
    }
    // 2. Wheel angle; sound position.
    c.wheelAngle = (F * c.v) + c.wheelAngle;
    if (c.wheelAngle > 6.28f) c.wheelAngle = c.wheelAngle - 6.28f;
    if (sound_) sound_->carUpdated(c);
    // 3. Obstacle registration; 4. room.
    registerObstacle(c);
    if (c.v != 0.0f) updateRoom(c);
}

void TrafficSystem::unregister(TrafficCar& c) {
    // Removed from the list it was filed in (a car not found is left alone).
    std::vector<int>* list = nullptr;
    if (c.regKind == 1 && c.regRoad >= 0 && size_t(c.regRoad) < sectionCars_.size()) {
        auto& lists = sectionCars_[size_t(c.regRoad)][size_t(sideSlot(c.regSide))];
        if (c.regSection >= 0 && size_t(c.regSection) < lists.size()) list = &lists[size_t(c.regSection)];
    } else if (c.regKind == 3 && c.regIx >= 0 && size_t(c.regIx) < ixCars_.size()) {
        list = &ixCars_[size_t(c.regIx)];
    }
    if (list) {
        auto it = std::find(list->begin(), list->end(), c.index);
        if (it != list->end()) list->erase(it);
    }
    c.regKind = 0;
}

// traffic-reactions.md, "Being seen by other AI": after every update and at the end of the start on
// the rail, unless the car's room is 0.
void TrafficSystem::registerObstacle(TrafficCar& c) {
    if (c.room == 0) return;
    int room = 0;
    const Component comp = net_->componentAtPreferring(c.pos, c.netRoom, c.road, true, &room);
    c.netRoom = room;
    if (comp.kind == 1) {
        const int R = comp.index;
        const int s2 = R == c.road ? c.side : c.next.side;
        const int n = net_->roads()[size_t(R)].n;
        const int v = std::clamp(net_->sectionOfPoint(R, c.pos, s2), 1, n - 1);
        if (c.regKind == 1 && c.regRoad == R && c.regSection == v && c.regSide == s2) return;
        unregister(c);
        auto& list = sectionCars_[size_t(R)][size_t(sideSlot(s2))][size_t(v)];
        list.insert(list.begin(), c.index);
        c.regKind = 1;
        c.regRoad = R;
        c.regSide = s2;
        c.regSection = v;
        return;
    }
    if (comp.kind == 3) {
        if (c.regKind == 3) return;  // never moved from one intersection list into another
        unregister(c);
        auto& list = ixCars_[size_t(comp.index)];
        list.insert(list.begin(), c.index);
        c.regKind = 3;
        c.regIx = comp.index;
        return;
    }
    unregister(c);
}

void TrafficSystem::updateRoom(TrafficCar& c) { c.room = city_->findRoom(c.pos + c.Y, c.room); }

// ---------------------------------------------------------------------------------------------
// Driving (traffic-driving.md)

void TrafficSystem::setPiece(TrafficCar& c, const Piece& p) {
    c.len = p.L;
    c.curve = RoadNetwork::curveOf(p);
    c.invLen = c.curve.R;
}

void TrafficSystem::chooseNext(TrafficCar& c) {
    const NextChoice n = net_->chooseNext(c.road, c.side, c.lane, rng_);
    // Lane-0 and last-lane rules set "no next road"; the others leave the previous choice.
    if (n.road >= 0) c.next = n;
    else if (n.rule == 4 || n.rule == 5 || n.rule == 6) c.next = NextChoice();
}

void TrafficSystem::startOnRail(TrafficCar& c) {
    const TrafficType& t = typeOf(c);
    c.go = false;
    c.queued = false;
    float lam = laneLen(c.road, c.side, c.lane);
    c.lcAllowed = c.x <= 0.25f * (lam - t.front);
    c.lookAhead = 25;
    float Lt = 0;
    if (c.next.road >= 0) {
        Lt = net_->turnPiece(c.road, c.side, c.lane, c.o, net_->turnStart(c.road, c.side, c.lane, t.front), c.next).L;
        if (c.x > Lt + (lam - t.front)) {
            c.x = c.x - (Lt + (lam - t.front));
            removeFrom(c.index, c.road, c.side, c.listLane);
            append(c.index, c.next.road, c.next.side, c.next.lane);
            c.road = c.next.road;
            c.side = c.next.side;
            c.lane = c.listLane = c.next.lane;
            c.next = NextChoice();
            chooseNext(c);
            lam = laneLen(c.road, c.side, c.lane);
            Lt = c.next.road >= 0
                     ? net_->turnPiece(c.road, c.side, c.lane, c.o, net_->turnStart(c.road, c.side, c.lane, t.front), c.next).L
                     : 0.0f;
        }
    }
    const Road& rd = net_->roads()[size_t(c.road)];
    const Lane& ln = rd.sideOf(c.side).lines[size_t(c.lane)];
    if (c.x >= lam - t.front && c.next.road >= 0) {
        c.u = c.x - (lam - t.front);
        const Piece tp = net_->turnPiece(c.road, c.side, c.lane, c.o, net_->turnStart(c.road, c.side, c.lane, t.front), c.next);
        setPiece(c, tp);
        c.rail = 1;
        c.pos = net_->intersections()[size_t(net_->exitIntersection(c.road, c.side))].centre;
    } else {
        int k;
        float into;
        net_->pieceAt(c.road, c.side, c.lane, c.x, k, into);
        c.k = k;
        c.u = c.x - ln.D[size_t(k - 1)];
        setPiece(c, net_->lanePiece(c.road, c.side, c.lane, k, c.o, t.front, PieceSetup::Start));
        c.rail = 0;
        const vec3& a = ln.P[size_t(k - 1)];
        const vec3& b = ln.P[size_t(k)];
        const float w = c.u / c.len;
        c.pos = vec3(a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w);
    }
    c.react = int16_t(c.T0);
    registerObstacle(c);  // also at the end of the start on the rail
}

float TrafficSystem::targetSpeed(const TrafficCar& c) const {
    const Road& rd = net_->roads()[size_t(c.road)];
    if (rd.flags & 0x04) {
        const int lanes = rd.sideOf(c.side).lanes;
        return rd.speedLimit + ((float(lanes - c.listLane - 1) * 5.0f) + c.E);
    }
    return rd.speedLimit + c.E;
}

void TrafficSystem::raise(TrafficCar& c) const {
    c.vt = targetSpeed(c) + 0.0001f;
    c.a = c.A;
}

float TrafficSystem::delta(const TrafficCar& c, const TrafficCar& o) const {
    if (o.rail == 3) {
        const vec3 d = o.pos - c.pos;
        return std::sqrt((d.x * d.x + d.y * d.y) + d.z * d.z);
    }
    if (o.road != c.road) return (c.len - c.u) + o.x;
    const TrafficType& t = typeOf(c);
    if (c.rail == 0) {
        if (o.rail == 0 || o.rail == 1) return o.x - c.x;
        // o changing lanes: its source lane is o.lane.
        const float lsrc = laneLen(o.road, c.side, o.lane);
        const float s = lsrc - t.front;
        const float q = (s * 0.25f) + o.u;
        if (o.lane == c.lane) return q - c.x;
        const float mine = laneLen(c.road, c.side, c.lane);
        return ((q * mine) / lsrc) - c.x;
    }
    if (c.rail == 1) {
        if (o.rail == 0) return o.x - c.x;
        if (o.rail == 1) return o.u - c.u;
        return 9999.0f;
    }
    // c changing lanes.
    if (o.rail == 0 || o.rail == 1) return o.x - (c.xLC - (c.len - c.u));
    int j;
    float into;
    net_->pieceAt(o.road, o.side, o.lane, o.x, j, into);
    const Lane& mine = net_->roads()[size_t(c.road)].sideOf(c.side).lines[size_t(c.lane)];
    const Lane& theirs = net_->roads()[size_t(o.road)].sideOf(o.side).lines[size_t(o.lane)];
    return (mine.D[size_t(j)] - c.x) - (theirs.D[size_t(j)] - o.x);
}

void TrafficSystem::follow(TrafficCar& c, const TrafficCar& o, float d) const {
    const TrafficType& t = typeOf(c);
    const TrafficType& to = typeOf(o);
    const float target = targetSpeed(c);
    float w = o.v - 2.5f;
    if (w < 0.0f) w = 0.0f;
    if (w > 999.0f) w = 999.0f;
    const float m = to.back + t.front;
    if (d < m) {
        if (o.rail == 2) {
            c.a = (c.v * c.v) * -0.16666667f;
            c.vt = 0;
        } else {
            c.v = 0;
            c.a = 0;
            c.vt = 0;
        }
        return;
    }
    if (d < (to.back + t.front) + c.G) {
        if (w > c.v) {
            c.a = c.A;
        } else {
            c.v = c.v * 0.75f;
            if (o.a <= 0.0f) {
                c.a = 0;
                c.vt = 0;
                return;
            }
            c.a = o.a <= c.A ? o.a : c.A;
        }
        c.vt = w <= target ? w : target;
        return;
    }
    if (w > c.v) {
        c.a = c.A;
    } else {
        const float M = (c.G + to.back) + t.front;
        float h = d - M;
        if ((c.rail == 1 && o.rail == 1) || (c.rail == 2 && o.rail == 2)) h = h - 2.0f;
        c.a = ((w * w) - (c.v * c.v)) / (h + h);
    }
    c.vt = target < w ? target : w;
}

bool TrafficSystem::askIntersection(TrafficCar& c, float dEnd) {
    const RoadEnd& end = net_->arrivalEnd(c.road, c.side);
    const int I = end.intersection;
    if (!net_->control().mayEnter(*net_, c.road, c.side, c.index, true, c.v, dEnd)) {
        processWakes();
        return false;
    }
    if (end.control == 0) c.queued = true;
    processWakes();
    if (c.next.road < 0) return false;
    // 1. Accident.
    bool accident = false;
    for (int ci : intersectionCars(I))
        if (cars_[size_t(ci)].state != TrafficDriving) accident = true;
    if (!accident) {
        const Road& nr = net_->roads()[size_t(c.next.road)];
        for (int s = 0; s < 2 && !accident; ++s)
            for (int sec = 0; sec < nr.n && !accident; ++sec)
                for (int ci : sectionCars(c.next.road, s == 0 ? -1 : 1, sec))
                    if (cars_[size_t(ci)].state != TrafficDriving) accident = true;
    }
    // 2. Room on the exit lane.
    bool room = true;
    if (!accident) {
        const auto& nl = laneList(c.next.road, c.next.side, c.next.lane);
        if (!nl.empty()) {
            const TrafficCar& B = cars_[size_t(nl.back())];
            float acc = typeOf(B).back;
            auto lenOf = [&](const TrafficCar& y) {
                const TrafficType& ty = typeOf(y);
                return (y.G + ty.front) + ty.back;
            };
            int C = carAhead(c.index);
            while (C >= 0) {
                const TrafficCar& cc = cars_[size_t(C)];
                if (cc.road != c.road || !(c.x < cc.x)) break;
                if (cc.next.road == c.next.road) acc = lenOf(cc) + acc;
                C = carAheadInList(C, cc.road, cc.side, cc.listLane);
            }
            room = B.x > lenOf(c) + acc;
        }
    }
    if (accident || !room) {
        chooseNext(c);
        return false;
    }
    // 3. Crossing conflict.
    const Road& rd = net_->roads()[size_t(c.road)];
    const Road& nrd = net_->roads()[size_t(c.next.road)];
    if (!(rd.flags & 0x04) && !(nrd.flags & 0x04)) {
        const Intersection& x = net_->intersections()[size_t(I)];
        auto sgn = [&](const vec3& y) {
            const float v = ((y.z - c.pos.z) * c.X.z + (y.y - c.pos.y) * c.X.y) + (y.x - c.pos.x) * c.X.x;
            return v > 0.0f ? 1 : v < 0.0f ? -1 : 0;
        };
        for (int R : x.roads) {
            if (R == c.road) continue;
            const Road& rr = net_->roads()[size_t(R)];
            for (int l = 0; l < rr.sideOf(c.side).lanes; ++l) {
                const auto& list = laneList(R, c.side, l);
                if (list.empty()) continue;
                const TrafficCar& H = cars_[size_t(list.front())];
                if (!H.go || H.next.road < 0) continue;
                const Lane& hl = rr.sideOf(H.side).lines[size_t(std::min(H.lane, rr.sideOf(H.side).lanes - 1))];
                const vec3 p = hl.P[size_t(rr.n - 1)];
                const Road& hn = net_->roads()[size_t(H.next.road)];
                const RoadSide& hs = hn.sideOf(c.next.side);
                if (hs.lanes <= 0) continue;
                const vec3 q = hs.lines[size_t(std::min(H.next.lane, hs.lanes - 1))].P[0];
                if (sgn(p) != sgn(q)) return false;
            }
        }
    }
    return true;
}

void TrafficSystem::driving(TrafficCar& c, float F, const std::vector<TrafficPlayer>& players) {
    if (c.tick++ == 0) {
        startOnRail(c);
        solve(c, players);
        updateRoom(c);
    }
    const TrafficType& t = typeOf(c);
    const float v0 = c.v;
    float lastDelta = 99999.0f;
    const float target = targetSpeed(c);
    const float lam = laneLen(c.road, c.side, c.listLane);
    float dEnd;
    if (c.rail == 0) dEnd = lam - (c.x + t.front);
    else if (c.rail == 2) dEnd = ((lam - t.front) + (c.len - c.u)) - c.xLC;
    else dEnd = 9999.0f;
    const int ahead = carAhead(c.index);
    if (dEnd >= c.lookAhead || c.rail == 1) {
        // 3. Open road.
        if (ahead >= 0) {
            const TrafficCar& C = cars_[size_t(ahead)];
            const float d = delta(c, C);
            lastDelta = d;
            if (d < 20.0f) {
                if (c.react > c.T0) follow(c, C, d);
            } else if (target > c.vt || c.a < c.A) {
                raise(c);
            }
        } else if (c.rail == 1 && c.next.road >= 0) {
            float d = 99999.0f;
            int cand = -1;
            const auto& nl = laneList(c.next.road, c.next.side, c.next.lane);
            if (!nl.empty() && nl.back() != c.index) {
                d = delta(c, cars_[size_t(nl.back())]);
                cand = nl.back();
            }
            const Road& rd = net_->roads()[size_t(c.road)];
            for (int l = 0; l < rd.sideOf(c.side).lanes; ++l) {
                const auto& ll = laneList(c.road, c.side, l);
                if (ll.empty()) continue;
                const TrafficCar& H = cars_[size_t(ll.front())];
                if (H.rail != 1 || H.index == c.index || H.next.road != c.next.road || H.next.lane != c.next.lane) continue;
                const float dh = delta(c, H);
                if (dh < d) {
                    d = dh;
                    cand = H.index;
                }
            }
            if (d > 0.0f && d < 20.0f && cand >= 0) {
                if (c.react > c.T0) follow(c, cars_[size_t(cand)], d);
            } else if (target > c.vt || c.a < c.A) {
                raise(c);
            }
        } else if (target > c.vt) {
            raise(c);
        }
    } else {
        // 4. Permission.
        if (!c.go && askIntersection(c, dEnd)) {
            c.go = true;
            if (c.v < 0.01f && dEnd < 0.5f) c.react = int16_t(c.T0 - 3);
        }
        // 5. Approach.
        const int C = carAhead(c.index);
        if (!c.go) {
            int C2 = C;
            if (C >= 0 && cars_[size_t(C)].state == TrafficKnocked) C2 = carAhead(C);
            bool handled = false;
            if (C2 >= 0) {
                const TrafficCar& cc2 = cars_[size_t(C2)];
                if (cc2.rail != 1 && cc2.road == c.road) {
                    handled = true;
                    const float d = delta(c, cc2);
                    lastDelta = d;
                    if (d < 20.0f) {
                        if (c.react > c.T0) follow(c, cars_[size_t(C)], d);
                    } else if (target > c.vt || c.a < c.A) {
                        raise(c);
                    }
                }
            }
            if (!handled) {
                if (c.react > c.T0 && v0 <= 2.0f && dEnd > 1.0f) {
                    c.v = 2.0f;
                    c.lookAhead = 5.0f;
                    raise(c);
                } else if (c.vt != 0.0f) {
                    const float h = dEnd - 0.25f;
                    c.a = -((v0 * v0) / (h + h));
                    c.vt = 0;
                } else if (c.v < 0.2f && dEnd < 0.5f) {
                    c.v = 0;
                }
            }
        } else {
            if (C < 0) {
                if (target > c.vt && c.react > c.T0) raise(c);
            } else {
                const TrafficCar& cc = cars_[size_t(C)];
                const float d = delta(c, cc);
                lastDelta = d;
                if (d >= 20.0f && c.react > c.T0) {
                    if (target > c.vt) raise(c);
                    else if (c.vt > 0.0f && c.a == 0.0f) c.a = c.A;
                } else if (d < 20.0f && cc.vt != c.vt && c.react > c.T0) {
                    follow(c, cc, d);
                }
            }
        }
        if (c.next.road >= 0) {
            const int tc = net_->turnClass(c.road, c.side, c.next.road, c.next.side);
            if (tc > 0) c.indicator = 2;
            else if (tc < 0) c.indicator = 1;
        }
    }
    // 6. Integrate.
    c.v = (F * c.a) + c.v;
    if ((c.v < c.vt + 0.05f && c.a < 0.0f) || (c.vt < c.v && c.a > 0.0f)) {
        c.v = c.vt;
        c.a = 0;
    }
    if (ahead >= 0 && cars_[size_t(ahead)].v < 0.01f && c.v < 0.01f && lastDelta < 20.0f) c.react = 0;
    c.u = (F * c.v) + c.u;
    c.x = (F * c.v) + c.x;

    // Lane-change trigger.
    if (laneChanges_ && c.lcAllowed && c.x > 0.25f * laneLen(c.road, c.side, c.lane)) {
        c.lcAllowed = false;
        tryLaneChange(c);
    }
    if (c.v > 0.0f) {
        advance(c);
        if (!c.onStreets) return;
        solve(c, players);
        if (noticePlayer(c, players, true)) return;
    } else if (!c.groundSolved) {
        for (size_t p = 0; p < players.size() && p < c.solvedNear.size(); ++p) {
            const vec3& pm = players[p].modelOrigin;
            const float dz = c.pos.z - pm.z, dx = c.pos.x - pm.x;
            if (dz * dz + dx * dx < 10000.0f && !c.solvedNear[p]) {
                c.solvedNear[p] = true;
                solve(c, players);
            }
        }
    }
    c.react = int16_t(c.react + 1);
}

void TrafficSystem::advance(TrafficCar& c) {
    if (!(c.u > c.len)) return;
    const TrafficType& t = typeOf(c);
    const Road& rd = net_->roads()[size_t(c.road)];
    const int n = rd.n;
    if (c.rail == 0) {
        if (c.next.road < 0 && c.k >= n - 1) {
            // Engine: no next road: stop at the end of the last piece and ask again each frame.
            c.u = c.len;
            c.v = 0;
            c.a = 0;
            chooseNext(c);
            return;
        }
        c.k = c.k + 1;
        c.u = c.u - c.len;
        if (c.k <= n - 1) {
            const Piece p = net_->lanePiece(c.road, c.side, c.lane, c.k, c.o, t.front, PieceSetup::Advance);
            if (c.k == n - 1 && !(p.L > 0.0f) && c.next.road >= 0) {
                setPiece(c, net_->turnPiece(c.road, c.side, c.lane, c.o, net_->turnStart(c.road, c.side, c.lane, t.front), c.next));
                c.rail = 1;
            } else {
                setPiece(c, p);
            }
        } else {
            setPiece(c, net_->turnPiece(c.road, c.side, c.lane, c.o, net_->turnStart(c.road, c.side, c.lane, t.front), c.next));
            c.rail = 1;
        }
        return;
    }
    if (c.rail == 1) {
        arrive(c);
        return;
    }
    if (c.rail == 2) endLaneChange(c);
}

void TrafficSystem::arrive(TrafficCar& c) {
    const RoadEnd& end = net_->arrivalEnd(c.road, c.side);
    c.go = false;
    c.lookAhead = 25;
    c.lcAllowed = true;
    if (end.control == 0) {
        net_->control().leftIntersection(end.intersection, c.index);
        c.queued = false;
    }
    removeFrom(c.index, c.road, c.side, c.listLane);
    if (c.next.road < 0) return;
    if (!isActive(c.next.road)) {
        const Road& rd = net_->roads()[size_t(c.road)];
        const RoadSide& other = rd.sideOf(-c.side);
        if (!other.open() || other.lanes <= 0) {
            // Engine: test the side the car would turn onto.
            ++stats_.leftStreets;
            freeCar(c.index);
            return;
        }
        c.side = -c.side;
        if (c.lane >= other.lanes) c.lane = c.listLane = other.lanes - 1;
        c.listLane = c.lane;
        append(c.index, c.road, c.side, c.lane);
        ++stats_.turnedRound;
    } else {
        append(c.index, c.next.road, c.next.side, c.next.lane);
        c.road = c.next.road;
        c.side = c.next.side;
        c.lane = c.listLane = c.next.lane;
    }
    chooseNext(c);
    const Road& nr = net_->roads()[size_t(c.road)];
    c.k = 1;
    c.x = c.u - c.len;
    c.u = c.x;
    setPiece(c, net_->lanePiece(c.road, c.side, c.lane, 1, c.o, typeOf(c).front,
                                nr.n == 2 ? PieceSetup::Arrive : PieceSetup::Advance));
    c.rail = 0;
    c.indicator = 0;
}

void TrafficSystem::tryLaneChange(TrafficCar& c) {
    const Road& rd = net_->roads()[size_t(c.road)];
    if (!(rd.length() > 60.0f)) return;
    const RoadSide& sd = rd.sideOf(c.side);
    const float lam = laneLen(c.road, c.side, c.lane);
    const float t = 0.25f * lam;
    auto count = [&](int j) {
        int n = 0;
        for (int ci : laneList(c.road, c.side, j)) {
            if (!(cars_[size_t(ci)].x > t)) break;
            ++n;
        }
        return n;
    };
    int best = count(c.lane), target = c.lane;
    if (c.lane > 0 && count(c.lane - 1) < best) {
        target = c.lane - 1;
        best = count(c.lane - 1);
    }
    if (c.lane < sd.lanes - 1 && count(c.lane + 1) < best) target = c.lane + 1;
    if (target == c.lane) return;
    const TrafficType& ty = typeOf(c);
    const Lane& L = sd.lines[size_t(c.lane)];
    const Lane& T = sd.lines[size_t(target)];
    const int n = rd.n;
    // 1.
    int j;
    float into;
    net_->pieceAt(c.road, c.side, c.lane, c.x, j, into);
    const float m = T.D[size_t(j)] - (L.D[size_t(j)] - c.x);
    sortedInsert(c.index, c.road, c.side, target, m);
    c.listLane = target;
    // 2.
    const float tau = c.u * c.invLen;
    const vec3 P0 = RoadNetwork::curvePosition(c.curve, c.u);
    vec3 T0 = RoadNetwork::curveDerivative(c.curve, c.u);
    (void)tau;
    // 3.
    const float e = ((lam - ty.front) * 0.25f) + 30.0f;
    int j2;
    net_->pieceAt(c.road, c.side, c.lane, e, j2, into);
    c.xLC = T.D[size_t(j2)] - (L.D[size_t(j2)] - e);
    // 4.
    int j3;
    net_->pieceAt(c.road, c.side, target, c.xLC, j3, into);
    Piece tp;
    float g;
    if (j3 < n - 1) {
        g = T.D[size_t(j3)] - T.D[size_t(j3 - 1)];
        tp.P0 = T.P[size_t(j3 - 1)];
        tp.P1 = T.P[size_t(j3)];
        tp.T0 = net_->travelTangent(c.road, c.side, j3 - 1) * g;
        tp.T1 = net_->travelTangent(c.road, c.side, j3) * g;
    } else {
        const float sl = T.D[size_t(n - 1)] - T.D[size_t(n - 2)];
        g = sl + ty.back;
        tp.P0 = T.P[size_t(n - 2)];
        const float w = 1.0f - (ty.front / sl);
        const vec3& a = T.P[size_t(n - 2)];
        const vec3& b = T.P[size_t(n - 1)];
        tp.P1 = vec3((b.x - a.x) * w + a.x, (b.y - a.y) * w + a.y, (b.z - a.z) * w + a.z);
        tp.T0 = net_->travelTangent(c.road, c.side, n - 2) * g;
        tp.T1 = net_->endDir(c.road, c.side) * g;
    }
    const vec3 Xa = net_->axisX(c.road, c.side, j3 - 1), Xb = net_->axisX(c.road, c.side, j3);
    tp.P0 = vec3(c.o * Xa.x + tp.P0.x, c.o * Xa.y + tp.P0.y, c.o * Xa.z + tp.P0.z);
    tp.P1 = vec3(c.o * Xb.x + tp.P1.x, c.o * Xb.y + tp.P1.y, c.o * Xb.z + tp.P1.z);
    tp.L = g;
    const PieceCurve tc = RoadNetwork::curveOf(tp);
    // τ = (x_LC′ − D_T[j₃ − 1]) / g with x_LC′ = x_LC clamped to 0 … λ_T, evaluated with the
    // stored coefficients and no clamp of τ (traffic-driving.md, lane change step 4).
    const float lamT = T.D[size_t(n - 1)];
    const float xc = std::clamp(c.xLC, 0.0f, lamT);
    const float at = (xc - T.D[size_t(j3 - 1)]) / g;
    const vec3 P1 = RoadNetwork::curvePositionAt(tc, at);
    vec3 T1 = RoadNetwork::curveDerivativeAt(tc, at);
    // 5-6.
    c.u = 0;
    c.len = c.xLC - c.x;
    auto norm = [&](vec3 v) {
        const float l2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
        const float gg = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
        v = vec3(gg * v.x, gg * v.y, gg * v.z);
        return vec3(c.len * v.x, c.len * v.y, c.len * v.z);
    };
    T0 = norm(T0);
    T1 = norm(T1);
    Piece lc;
    lc.P0 = P0;
    lc.P1 = P1;
    lc.T0 = T0;
    lc.T1 = T1;
    lc.L = c.len;
    setPiece(c, lc);
    c.rail = 2;
    c.indicator = target > c.lane ? 2 : 1;
    ++stats_.laneChanges;
}

void TrafficSystem::endLaneChange(TrafficCar& c) {
    const int target = c.listLane;
    c.x = (c.u - c.len) + c.xLC;
    int k;
    float into;
    net_->pieceAt(c.road, c.side, target, c.x, k, into);
    const Lane& T = net_->roads()[size_t(c.road)].sideOf(c.side).lines[size_t(target)];
    c.k = k;
    c.u = c.x - T.D[size_t(k - 1)];
    setPiece(c, net_->lanePiece(c.road, c.side, target, k, c.o, typeOf(c).front, PieceSetup::Rejoin));
    removeFrom(c.index, c.road, c.side, c.lane);
    c.lane = target;
    chooseNext(c);
    c.rail = 0;
    c.indicator = 0;
}

bool TrafficSystem::blinkOn(const TrafficCar& c) const {
    return ((c.blink + int(std::trunc(16.0f * blinkClock_))) & 8) != 0;
}

mat4 TrafficSystem::matrixOf(const TrafficCar& c) const {
    mat4 m = mat4::identity();
    m.m[0] = c.X.x, m.m[1] = c.X.y, m.m[2] = c.X.z;
    m.m[4] = c.Y.x, m.m[5] = c.Y.y, m.m[6] = c.Y.z;
    m.m[8] = c.Z.x, m.m[9] = c.Z.y, m.m[10] = c.Z.z;
    m.m[12] = c.pos.x, m.m[13] = c.pos.y, m.m[14] = c.pos.z;
    return m;
}

mat4 TrafficSystem::wheelTransform(const TrafficCar& c, int i) const {
    const TrafficType& t = typeOf(c);
    if (c.body >= 0 && i < 4) {
        const phys::TrafficPhys& b = *bodies_[size_t(c.body)].phys;
        const vec3& o = t.wheelPos[i];
        return mat4::translate(vec3(o.x - b.dLat[i] * 0.2f, o.y + b.comp[i], o.z - b.dLong[i] * 0.3f));
    }
    if (c.body >= 0) return mat4::translate(t.wheelPos[i]);
    // Spin rows (1,0,0), (0,cos α,−sin α), (0,sin α,cos α) in MM2's row-vector convention.
    const float ca = std::cos(c.wheelAngle), sa = std::sin(c.wheelAngle);
    mat4 spin = mat4::identity();
    spin.at(1, 1) = ca;
    spin.at(2, 1) = -sa;
    spin.at(1, 2) = sa;
    spin.at(2, 2) = ca;
    return mat4::translate(t.wheelPos[i]) * spin;
}

}  // namespace ai
}  // namespace cc
