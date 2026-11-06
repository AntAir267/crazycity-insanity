#include "ai/opponents.h"

#include <cmath>
#include <map>

#include "ai/road_network.h"
#include "city/city_data.h"
#include "core/log.h"
#include "model/asset_cache.h"
#include "model/vehicle.h"
#include "physics/bound.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace ai {

OpponentRoute resolveOpponentRoute(const OppFile& f, const CityData& city, const RoadNetwork& net) {
    OpponentRoute r;
    const int N = int(f.points.size());
    r.points = N;
    if (N == 0) return r;  // engine: start (0, 0, 0), heading 0, no route, destination (0, 0, 0)
    r.start = f.points[0].pos;
    r.heading = float(f.points[0].heading * double(0.017444445f));
    if (N == 1) {
        r.destination = r.start;
        return r;
    }
    for (int k = 1; k <= N - 2; ++k) {
        const int room = city.findRoom(f.points[size_t(k)].pos, 0);
        int entry = -1;
        for (const Component& c : net.roomComponents(room))
            if (c.kind == 3) {
                entry = c.index;
                break;
            }
        if (entry < 0) {
            ++r.pointsWithoutIntersection;  // engine: the point is left out (MM2 leaves it undefined)
            continue;
        }
        r.route.push_back(entry);
    }
    r.destination = f.points[size_t(N - 1)].pos;
    return r;
}

FinishLine& opponentFinishLine() {
    static FinishLine line;
    return line;
}

void setOpponentFinishLine(const vec3& pos, float headingDegrees) {
    const float a = headingDegrees * -0.017453292f;
    FinishLine& l = opponentFinishLine();
    l.F = pos;
    l.U = vec3(float(std::sin(double(a))), 0.0f, float(std::cos(double(a))));
}

OpponentSystem::OpponentSystem() = default;
OpponentSystem::~OpponentSystem() = default;

void OpponentSystem::clear() {
    opps_.clear();
    net_ = nullptr;
    city_ = nullptr;
}

int OpponentSystem::countFor(int countLine, float s) {
    const float c = float(countLine);
    const float v = (std::isnan(s) || c < s) ? c : s;
    const float t = std::trunc(v);
    return t > 0.0f ? int(t) : 0;
}

void OpponentSystem::create(const Vfs& vfs, AssetCache* assets, const CityData& city, RoadNetwork& net,
                            phys::CollisionWorld& world, const Setup& s) {
    (void)world;
    clear();
    net_ = &net;
    city_ = &city;
    laps_ = s.laps;
    int m = countFor(s.countLine, s.opponentNumber);
    if (m > int(s.rows.size())) m = int(s.rows.size());  // engine: only the rows read
    // vpcop paint (police.md, "The cop's car"): 0 when the session has cable cars.
    bool cable = s.cableCars;
    if (!cable)
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
    if (s.playerBound) bounds[s.playerCar] = s.playerBound;
    std::map<std::string, std::shared_ptr<VehicleModel>> models;
    opps_.reserve(size_t(m));
    for (int i = 0; i < m; ++i) {
        const OpponentRow& row = s.rows[size_t(i)];
        Opponent o;
        o.index = i;
        o.row = row;
        phys::CarTune tune = phys::loadCarTune(vfs, row.car, false);
        if (!tune.simFile) {
            logWarn("opponent %d: %s has no .vehCarSim; skipped", i, row.car.c_str());
            continue;
        }
        auto it = bounds.find(row.car);
        if (it != bounds.end()) {
            tune.bound = it->second;
        } else if (tune.bound) {
            const phys::Bound& b = *tune.bound;
            const vec3 size(b.bmax.x - b.bmin.x, b.bmax.y - b.bmin.y, b.bmax.z - b.bmin.z);
            const vec3 off(size.x * 0.5f + b.bmin.x, size.y * 0.5f + b.bmin.y, size.z * 0.5f + b.bmin.z);
            tune.bound = std::make_shared<phys::Bound>(phys::makeBoxBound(size, off));
            bounds[row.car] = tune.bound;
        }
        if (!tune.bound) {
            logWarn("opponent %d: %s has no bound; skipped", i, row.car.c_str());
            continue;
        }
        const phys::Bound& b = *tune.bound;
        o.box = b.isBox;
        o.boxMin = b.bmin;
        o.boxMax = b.bmax;
        if (o.box) o.shape = phys::BoxShape{vec3(b.bmax.x - b.bmin.x, b.bmax.y - b.bmin.y, b.bmax.z - b.bmin.z), b.centre};
        o.car = std::make_unique<phys::Car>();
        o.car->setup(tune);
        o.driver.create(o.car.get(), row.car, b.bmin, b.bmax, s.regenerate);
        o.driver.setRacerNumber(i);
        DriverEnv e;
        e.net = &net;
        e.city = &city;
        o.driver.reset(e);
        // The route file (opp.md): race/<RaceDir>/<name> as written.
        const std::string path = "race/" + s.raceDir + "/" + row.file;
        BytesPtr bytes = vfs.open(path);
        if (!bytes) logFile(LogLevel::Warn, path, "opponent route file missing; the opponent has no route");
        const OppFile f = bytes ? parseOpp(std::string(bytes->begin(), bytes->end())) : OppFile();
        o.route = resolveOpponentRoute(f, city, net);
        o.q = o.route.start;
        o.theta = o.route.heading;
        if (s.loadModels && assets) {
            auto mi = models.find(row.car);
            if (mi == models.end()) {
                auto mdl = std::make_shared<VehicleModel>();
                if (!loadVehicle(*assets, row.car, VehicleKind::Player, *mdl)) mdl.reset();
                else mdl->trailer.reset();
                mi = models.emplace(row.car, mdl).first;
            }
            o.model = mi->second;
        }
        o.paint = row.car == "vpcop" ? (cable ? 0 : 1) : (i & 3);
        if (o.model) o.car->setPaintSet(paintSetFor(o.paint, o.model->setCount()));
        else if (tune.pkg) o.car->setPaintSet(paintSetFor(o.paint, tune.pkg->setCount));
        o.identity = 10000 + 200 + i;
        opps_.push_back(std::move(o));
    }
    logInfo("opponents: %d of %d rows (count line %d, opponent number %.1f)", int(opps_.size()), int(s.rows.size()), s.countLine,
            double(s.opponentNumber));
}

void OpponentSystem::reset(double now) {
    for (Opponent& o : opps_) {
        o.state = OpponentState::Racing;
        o.prev = OpponentState::None;
        o.car->placeAt(o.q, o.theta);
        DriverEnv e;
        e.net = net_;
        e.city = city_;
        e.now = now;
        o.driver.reset(e);
        o.touchedPlayer = false;
        o.driver.setTouchedPlayer(false);
    }
}

void OpponentSystem::groundSnap(phys::Car& car, vec3& q, float theta, phys::World& world) {
    const vec3 M = car.modelOrigin();
    phys::ProbeState ps;
    phys::ProbeHit hit;
    if (world.probe(vec3(M.x, M.y + 2.0f, M.z), vec3(M.x, M.y - 10.0f, M.z), ps, hit)) {
        q = vec3(hit.point.x, hit.point.y + 0.89999998f, hit.point.z);
        car.placeAt(q, theta);
    }
}

void OpponentSystem::groundSnap(phys::World& world) {
    for (Opponent& o : opps_) groundSnap(*o.car, o.q, o.theta, world);
}

void OpponentSystem::giveOrders(Opponent& o, DriverEnv& env, int laps, RoadNetwork* net) {
    DriverOrders d;
    d.route = o.route.route;
    d.D = o.route.destination;
    d.Dd = vec3(0, 0, 0);
    d.laps = laps;
    d.s1 = 0.0f;
    d.s2 = 0.0f;
    for (int k = 0; k < 6; ++k) d.sw[size_t(k)] = o.row.sw[k];
    d.kthr = o.row.throttle;
    d.kc = o.row.corner;
    d.kb = o.row.brake;
    d.kp = o.row.pad;
    o.driver.giveOrders(d, env);
    // The one-off AI-ahead mark on W[0] (engine: skipped for an empty route).
    if (!d.route.empty() && net) {
        const int I = d.route[0];
        if (I >= 0 && size_t(I) < net->intersections().size())
            for (int qi : net->intersections()[size_t(I)].roads) {
                Road& Q = net->roads()[size_t(qi)];
                const RoadEnd& e = Q.end[0].intersection == I ? Q.end[0] : Q.end[1];
                if (e.control == 0 || e.control == 1) Q.held = true;
            }
    }
}

void OpponentSystem::updateOne(Opponent& o, float F, DriverEnv& env, const std::vector<const phys::Car*>& players, int laps,
                               RoadNetwork* net) {
    // Steps 1 and 2 are one choice on the state the update starts with.
    if (o.state == OpponentState::Racing) {
        if (o.prev != OpponentState::Racing) {
            giveOrders(o, env, laps, net);
            o.prev = OpponentState::Racing;
        }
        o.driver.frame(F, env);
        if (!(o.car->body().p.y >= -200.0f)) o.state = OpponentState::Disabled;  // fell through the geometry
    } else if (o.state == OpponentState::Disabled) {
        if (o.prev != OpponentState::Disabled) {
            o.driver.setMode(DriverMode::Hold);
            o.prev = OpponentState::Disabled;
        }
    }
    // Physics status: always a mover; within 200 m of a player of the room-waking kind.
    float d2 = 9999999.0f;
    bool nanSeen = false;
    for (const phys::Car* p : players) {
        if (!p) continue;
        const vec3 d = o.car->body().p - p->body().p;
        const float v = (d.z * d.z + d.y * d.y) + d.x * d.x;
        if (nanSeen || v < d2 || std::isnan(v)) {
            d2 = v;
            if (std::isnan(v)) nanSeen = true;
        }
    }
    o.roomWaking = !players.empty() && (d2 < 40000.0f || std::isnan(d2));
}

void OpponentSystem::update(float F, DriverEnv& env, const std::vector<const phys::Car*>& players) {
    for (Opponent& o : opps_) updateOne(o, F, env, players, laps_, net_);
}

void OpponentSystem::yieldMarks(bool set) const {
    if (!net_) return;
    for (const Opponent& o : opps_) o.driver.yieldMarks(*net_, set);
}

void OpponentSystem::applyInputs() {
    for (Opponent& o : opps_) {
        o.car->aiControlStep(o.driver.carInputs());
        if (!o.car->drivable()) o.car->holdNotDrivable();
    }
}

void OpponentSystem::appendMovers(std::vector<phys::SceneMover>& movers) {
    for (Opponent& o : opps_) {
        phys::SceneMover m;
        m.kind = phys::SceneMover::CarMover;
        m.car = o.car.get();
        m.number = 100 + o.index;
        m.identity = o.identity;
        m.box = o.box;
        m.shape = o.shape;
        movers.push_back(m);
    }
}

void OpponentSystem::afterPhysics(const std::vector<phys::SceneMover>& movers) {
    for (Opponent& o : opps_) {
        o.touchedPlayer = false;
        for (const phys::SceneMover& m : movers)
            if (m.kind == phys::SceneMover::CarMover && m.car == o.car.get()) o.touchedPlayer = m.touchedPlayer;
        o.driver.setTouchedPlayer(o.touchedPlayer);
    }
}

std::vector<DriverOpponent> OpponentSystem::driverView() const {
    std::vector<DriverOpponent> v;
    v.reserve(opps_.size());
    for (const Opponent& o : opps_) v.push_back(DriverOpponent{o.car.get(), &o.driver, o.index});
    return v;
}

namespace {
float signValue(float x) {
    if (x > 0.0f) return 1.0f;
    if (x < 0.0f || std::isnan(x)) return -1.0f;
    return 0.0f;
}
}  // namespace

bool OpponentSystem::finishTest(int i, const FinishLine& line) {
    if (i < 0 || size_t(i) >= opps_.size()) return false;
    return finishTest(opps_[size_t(i)], line);
}

bool OpponentSystem::finishTest(Opponent& o, const FinishLine& line) {
    const vec3 p = o.car->body().p;
    const float dz = p.z - line.F.z, dx = p.x - line.F.x;
    const float q = dx * dx + dz * dz;
    if (!(q < 900.0f)) return false;
    if (o.driver.lap() != o.driver.orders().laps) return false;
    if (o.driver.waypoint() < o.driver.routeLength() - 1) return false;
    const float a = line.U.z * dz + dx * line.U.x;
    if (!(q < 400.0f)) {
        o.side = a;
        return false;
    }
    const float sS = signValue(o.side);
    const float v = a - ((o.driver.front() + 1.0f) * sS);
    return signValue(v) != sS;
}

}  // namespace ai
}  // namespace cc
