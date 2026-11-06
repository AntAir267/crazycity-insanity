// Pedestrians: pool, population, walking, crossing, reactions (spec/ai/pedestrians.md,
// pedestrian-walking.md, pedestrian-reactions.md). Arithmetic is float32 evaluated as the spec
// writes it (sums and products in the order given).
#include "ai/pedestrians.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

#include "ai/traffic.h"
#include "ai/traffic_control.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "model/asset_cache.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace ai {

namespace {

const Lane kNoLane;
constexpr float k015 = 0.15000001f;  // float32 0.15 (at most 8.6° a frame)
constexpr float k314 = 3.14f;
constexpr float k2pi = 6.2831001f;   // float32 6.2831

float fatan2(float y, float x) { return float(std::atan2(double(y), double(x))); }
float fsin(float a) { return float(std::sin(double(a))); }
float fcos(float a) { return float(std::cos(double(a))); }
int wrap(int i, int m) { return ((i % m) + m) % m; }

// Flat squared distance as the crossing steps write it: (t.z − p.z)² + (t.x − p.x)².
float flat2(const vec3& t, const vec3& p) {
    const float dz = t.z - p.z, dx = t.x - p.x;
    return dz * dz + dx * dx;
}

}  // namespace

PedestrianSystem::PedestrianSystem() = default;
PedestrianSystem::~PedestrianSystem() = default;

int PedestrianSystem::countFor(int poolSize, float density, bool off, bool circuit) {
    if (off || circuit) return 0;
    const float c = float(poolSize) * density;
    return c > 0.0f ? int(std::trunc(c)) : 0;
}

// ---------------------------------------------------------------------------------------------
// The pool (pedestrians.md, "The pool")

void PedestrianSystem::create(const Vfs& vfs, AssetCache& assets, const CityData& city, phys::CollisionWorld& world, int count,
                              bool rain) {
    clear();
    city_ = &city;
    net_ = city.network();
    world_ = &world;
    if (!net_ || count <= 0) return;
    const auto& roads = net_->roads();
    head_.assign(roads.size(), -1);
    wanted_.assign(roads.size(), 0);
    propOf_.assign(city.objects().size(), -1);
    for (size_t i = 0; i < world.objects().size(); ++i) {
        const phys::CollisionObject& o = world.objects()[i];
        if (o.kind == phys::ObjKind::Prop && o.cityObject >= 0 && size_t(o.cityObject) < propOf_.size())
            propOf_[size_t(o.cityObject)] = int(i);
    }
    const std::vector<PedPair>& list = !net_->raceAimap().peds.empty() ? net_->raceAimap().peds : net_->cityAimap().peds;
    if (list.empty()) {
        logWarn("pedestrians: the aimap names no pedestrian models; none created");
        net_ = nullptr;
        return;
    }
    std::map<std::string, int> loaded;  // model name → index (−1: failed), loaded on first use
    rng_.reset(1);
    peds_.resize(size_t(count));
    for (int i = 0; i < count; ++i) {
        Pedestrian& p = peds_[size_t(i)];
        p.index = i;
        // 1. Model (one draw); 2. loaded on first use.
        const PedPair& pair = list[size_t(std::min(int(std::trunc(rng_.unit() * float(list.size()))), int(list.size()) - 1))];
        const std::string& name = rain ? pair.bad : pair.good;
        auto it = loaded.find(name);
        if (it == loaded.end()) {
            auto m = std::make_unique<PedModel>();
            std::string err;
            int idx = -1;
            if (m->load(vfs, assets, name, err)) {
                idx = int(models_.size());
                models_.push_back(std::move(m));
            } else {
                logFile(LogLevel::Error, "anim/" + name, "pedestrian model not loaded: " + err);
            }
            it = loaded.emplace(name, idx).first;
        }
        p.model = it->second;
        // 3. Paint variant (one draw; never the last variant).
        const int variants = p.model >= 0 ? models_[size_t(p.model)]->variantCount() : 1;
        p.variant = int(std::trunc(rng_.unit() * float(variants - 1)));
        // 4. Gender (only the voice depends on it).
        p.female = pedModelIsFemale(name);
        // 5. Animation state reset.
        p.cur = p.next = -1;
        p.frame = 0;
    }
    resetClock();
    int usable = 0;
    for (const Pedestrian& p : peds_)
        if (p.model >= 0) ++usable;
    logInfo("pedestrians: pool of %d (%d with a model), %zu models", count, usable, models_.size());
}

void PedestrianSystem::clear() {
    city_ = nullptr;
    net_ = nullptr;
    world_ = nullptr;
    traffic_ = nullptr;
    models_.clear();
    peds_.clear();
    free_.clear();
    head_.clear();
    wanted_.clear();
    activeRoads_.clear();
    playerRoom_.clear();
    order_.clear();
    voices_.clear();
    stats_ = Stats();
    frame_ = 0;
    resetClock();
}

int PedestrianSystem::onStreets() const {
    int n = 0;
    for (const Pedestrian& p : peds_)
        if (p.onStreets) ++n;
    return n;
}

int PedestrianSystem::lostCount() const {
    int n = 0;
    for (const Pedestrian& p : peds_)
        if (p.onStreets && p.lost) ++n;
    return n;
}

mat4 PedestrianSystem::matrixOf(const Pedestrian& p) const {
    mat4 m = mat4::identity();
    const vec3* rows[3] = {&p.X, &p.Y, &p.Z};
    for (int i = 0; i < 3; ++i) {
        m.m[i * 4 + 0] = rows[i]->x;
        m.m[i * 4 + 1] = rows[i]->y;
        m.m[i * 4 + 2] = rows[i]->z;
    }
    m.m[12] = p.p.x;
    m.m[13] = p.p.y;
    m.m[14] = p.p.z;
    return m;
}

// ---------------------------------------------------------------------------------------------
// Road lists

bool PedestrianSystem::isActive(int road) const {
    return std::find(activeRoads_.begin(), activeRoads_.end(), road) != activeRoads_.end();
}

bool PedestrianSystem::pedSide(int road, int side) const {
    if (road < 0 || road >= net_->mainRoadCount()) return false;
    const RoadSide& sd = net_->roads()[size_t(road)].sideOf(side);
    // Engine: a side without a sidewalk line cannot be walked whatever its flags (none ships).
    return (sd.flags & 2) == 0 && sd.lines.size() > size_t(std::max(sd.lanes, 0));
}

void PedestrianSystem::pushFront(int ped, int road) {
    peds_[size_t(ped)].nextInRoad = head_[size_t(road)];
    head_[size_t(road)] = ped;
}

void PedestrianSystem::removeFromRoad(int ped, int road) {
    if (road < 0 || size_t(road) >= head_.size()) return;
    int* link = &head_[size_t(road)];
    for (int guard = 0; *link >= 0 && guard <= int(peds_.size()); ++guard) {
        if (*link == ped) {
            *link = peds_[size_t(ped)].nextInRoad;
            peds_[size_t(ped)].nextInRoad = -1;
            return;
        }
        link = &peds_[size_t(*link)].nextInRoad;
    }
}

// ---------------------------------------------------------------------------------------------
// Resets and rooms (pedestrians.md, "Rooms and pedestrian roads")

void PedestrianSystem::reset(const std::vector<PedPlayer>& players) {
    if (!active()) return;
    rng_.reset(1);
    std::fill(head_.begin(), head_.end(), -1);
    std::fill(wanted_.begin(), wanted_.end(), 0);
    activeRoads_.clear();
    free_.clear();
    for (Pedestrian& p : peds_) {
        p.onStreets = false;
        p.lost = false;
        p.room = 0;
        p.nextInRoad = -1;
        p.road = -1;
        if (p.model >= 0) free_.push_back(p.index);  // in index order: the last one is taken first
    }
    playerRoom_.assign(players.size(), 0);
    for (size_t i = 0; i < players.size(); ++i) {
        const int room = city_->findRoom(players[i].bodyPos, 0);
        if (room != 0) adjust(0, room, int(i));
        playerRoom_[i] = room;
    }
}

void PedestrianSystem::resetAtRoom(int room) {
    reset({});
    playerRoom_.assign(1, room);
    if (room != 0) adjust(0, room, 0);
}

void PedestrianSystem::emptyRoad(int road) {
    int pi = head_[size_t(road)];
    for (int guard = 0; pi >= 0 && guard <= int(peds_.size()); ++guard) {
        Pedestrian& p = peds_[size_t(pi)];
        const int next = p.nextInRoad;
        p.nextInRoad = -1;
        p.onStreets = false;
        p.room = 0;
        free_.push_back(pi);
        ++stats_.emptied;
        pi = next;
    }
    head_[size_t(road)] = -1;
    activeRoads_.erase(std::remove(activeRoads_.begin(), activeRoads_.end(), road), activeRoads_.end());
}

void PedestrianSystem::adjust(int oldRoom, int newRoom, int player) {
    const auto& oldList = net_->pedRoads(oldRoom);
    const auto& newList = net_->pedRoads(newRoom);
    const int nr = int(net_->roads().size());
    const uint8_t bit = uint8_t(1u << (player & 7));
    // 1–2. Leaving roads.
    for (uint16_t r : oldList) {
        if (r >= nr || std::find(newList.begin(), newList.end(), r) != newList.end()) continue;
        if (!isActive(r)) continue;
        wanted_[r] = uint8_t(wanted_[r] & ~bit);
        if (wanted_[r] == 0) emptyRoad(r);
    }
    // 3. Entering roads.
    std::vector<int> E;
    for (uint16_t r : newList) {
        if (r >= nr || std::find(oldList.begin(), oldList.end(), r) != oldList.end()) continue;
        const bool before = wanted_[r] != 0;
        wanted_[r] = uint8_t(wanted_[r] | bit);
        if (before) continue;
        activeRoads_.insert(activeRoads_.begin(), r);
        E.push_back(r);
    }
    // 4. Fill, round-robin over both sidewalks.
    if (free_.empty() || E.empty()) return;
    size_t i = 0, last = 0;
    while (true) {
        const int road = E[i];
        if (pedSide(road, -1)) {
            const int pi = free_.back();
            free_.pop_back();
            place(pi, road, -1);
            last = i;
        }
        if (!free_.empty() && pedSide(road, 1)) {
            const int pi = free_.back();
            free_.pop_back();
            place(pi, road, 1);
            last = i;
        }
        i = (i + 1) % E.size();
        if (i == last || free_.empty()) break;
    }
}

// ---------------------------------------------------------------------------------------------
// Sidewalk geometry (pedestrian-walking.md)

const Lane& PedestrianSystem::sidewalk(int road, int side) const {
    if (road < 0 || size_t(road) >= net_->roads().size()) return kNoLane;
    const RoadSide& sd = net_->roads()[size_t(road)].sideOf(side);
    const size_t L = size_t(std::max(sd.lanes, 0));
    if (L < sd.lines.size()) return sd.lines[L];
    return sd.lines.empty() ? kNoLane : sd.lines.back();
}

float PedestrianSystem::sidewalkLength(const Pedestrian& p, int a, int b) const {
    const Lane& W = sidewalk(p.road, p.side);
    a = std::max(a, 0);
    b = std::max(b, 0);
    if (W.D.empty()) return 0.0f;
    a = std::min(a, int(W.D.size()) - 1);
    b = std::min(b, int(W.D.size()) - 1);
    return W.D[size_t(b)] - W.D[size_t(a)];
}

int PedestrianSystem::segmentOf(const Pedestrian& p, float x) const {
    const Lane& W = sidewalk(p.road, p.side);
    const int n = int(W.D.size());
    if (n < 2) return 1;
    x = std::clamp(x, 0.0f, W.D[size_t(n - 1)]);
    for (int k = 1; k <= n - 1; ++k)
        if (x <= W.D[size_t(k)] + 0.00001f) return k;
    return n - 1;
}

float PedestrianSystem::heading(const Pedestrian& p, float x, int lambda, int d) const {
    const Road& rd = net_->roads()[size_t(p.road)];
    const RoadSide& sd = rd.side[d == 1 ? 1 : 0];
    if (sd.lines.empty()) return 0.0f;
    // Engine: a line number past the side's lines reads its last line (MM2 reads past the array,
    // e.g. λ = the other side's lane count on a one-way road; spec/QUESTIONS.md).
    const Lane& D = sd.lines[size_t(std::clamp(lambda, 0, int(sd.lines.size()) - 1))];
    const Lane& Q = sd.lines.back();
    const int n = std::min(int(D.D.size()), int(Q.P.size()));
    if (n < 2) return 0.0f;
    x = std::clamp(x, 0.0f, D.D[size_t(n - 1)]);
    int k = -1;
    for (int i = 1; i <= n - 1; ++i)
        if (x <= D.D[size_t(i)]) {
            k = i;
            break;
        }
    if (k < 0) return 0.0f;
    const vec3& a = Q.P[size_t(k - 1)];
    const vec3& b = Q.P[size_t(k)];
    if (d == 1) return fatan2(b.x - a.x, b.z - a.z);
    return fatan2(a.x - b.x, a.z - b.z);
}

void PedestrianSystem::buildCurve(Pedestrian& p, int k, float offset) {
    const float o = std::clamp(offset, -1.5f, 1.5f);
    p.o = o;
    const float ob = -o;
    const Road& rd = net_->roads()[size_t(p.road)];
    const int n = rd.n;
    const Lane& W = sidewalk(p.road, p.side);
    auto off = [&](int i) {
        const vec3& X = rd.X[size_t(i)];
        const vec3& w = W.P[size_t(i)];
        return vec3((ob * X.x) + w.x, (ob * X.y) + w.y, (ob * X.z) + w.z);
    };
    auto scl = [](float l, const vec3& v) { return vec3(l * v.x, l * v.y, l * v.z); };
    Piece pc;
    float L = 1.0f;
    if (k >= 1 && k <= n - 1) {
        L = sidewalkLength(p, k - 1, k);
        if (k == n - 1) {
            pc.P0 = off(k - 1);
            pc.P1 = off(n - 1);
            pc.T0 = scl(L, rd.T[size_t(k - 1)]);
            pc.T1 = scl(L, -rd.Z[size_t(n - 1)]);
        } else if (k == 1) {
            pc.P0 = off(0);
            pc.P1 = off(1);
            pc.T0 = scl(L, -rd.Z[0]);
            pc.T1 = scl(L, rd.T[1]);
        } else {
            pc.P0 = off(k - 1);
            pc.P1 = off(k);
            pc.T0 = scl(L, rd.T[size_t(k - 1)]);
            pc.T1 = scl(L, rd.T[size_t(k)]);
        }
    } else {
        // The crossing piece between the previous road's end and this road.
        const Lane& Wp = sidewalk(p.prevRoad, p.prevSide);
        const int np = int(Wp.P.size());
        const vec3 A = np == 0 ? W.P[0] : (p.prevDir == 1 ? Wp.P[size_t(np - 1)] : Wp.P[0]);
        if (p.dir == 1) {
            pc.P0 = A;
            pc.P1 = W.P[0];
        } else {
            pc.P0 = W.P[size_t(n - 1)];
            pc.P1 = A;
        }
        const vec3 D(pc.P1.x - pc.P0.x, pc.P1.y - pc.P0.y, pc.P1.z - pc.P0.z);
        pc.T0 = pc.T1 = D;
        L = std::sqrt((D.z * D.z + D.y * D.y) + D.x * D.x);
        if (p.prevRoad == p.road) L = 1.0f;
    }
    // A zero-length piece between two roads gives R = +∞ and NaN targets: the pedestrian is lost
    // (pedestrian-walking.md, "The curve"; plain IEEE arithmetic as in MM2).
    pc.L = L;
    p.curve = RoadNetwork::curveOf(pc);
}

void PedestrianSystem::targetAt(Pedestrian& p, float e) {
    const Lane& W = sidewalk(p.road, p.side);
    const int n = net_->roads()[size_t(p.road)].n;
    float u;
    if (p.k == 0) u = e * p.curve.R;
    else if (p.k >= n) u = (e - W.D[size_t(n - 1)]) * p.curve.R;
    else u = (e - W.D[size_t(p.k - 1)]) * p.curve.R;
    const vec3 c = RoadNetwork::curvePositionAt(p.curve, u);
    p.target = vec3(c.x, p.p.y, c.z);
}

float PedestrianSystem::locate(Pedestrian& p) {
    const Road& rd = net_->roads()[size_t(p.road)];
    const int n = rd.n;
    const Lane& W = sidewalk(p.road, p.side);
    const Lane& Wp = sidewalk(p.prevRoad, p.prevSide);
    const int np = int(Wp.P.size());
    auto offsetAt = [&](int i) {
        const vec3 e(W.P[size_t(i)].x - p.p.x, W.P[size_t(i)].y - p.p.y, W.P[size_t(i)].z - p.p.z);
        const vec3& X = rd.X[size_t(i)];
        return (e.z * X.z + e.y * X.y) + e.x * X.x;
    };
    if (p.dir == 1) {
        if (p.k >= n) p.k = n - 1;  // engine guard (not reached by the rules)
        const vec3& P = W.P[size_t(p.k)];
        const float dx = p.p.x - P.x, dz = p.p.z - P.z;
        vec3 A, v;
        float a;
        if (p.k == 0) {
            A = np == 0 ? P : (p.prevDir == 1 ? Wp.P[size_t(np - 1)] : Wp.P[0]);
            v = vec3(A.x - P.x, A.y - P.y, A.z - P.z);
            const float m = (v.y * v.y + v.z * v.z) + v.x * v.x;
            const float i = m == 0.0f ? 0.0f : 1.0f / std::sqrt(m);
            v = vec3(v.x * i, v.y * i, v.z * i);
            a = dz * v.z + dx * v.x;
        } else {
            a = dz * rd.Z[size_t(p.k)].z + dx * rd.Z[size_t(p.k)].x;
        }
        if (a <= 0.0f) {
            p.k = p.k + 1;
            if (p.k < n) {
                buildCurve(p, p.k, offsetAt(p.k - 1));
                return W.D[size_t(p.k - 1)] - a;
            }
            p.k = n - 1;
            buildCurve(p, p.k, offsetAt(n - 1));
            return W.D[size_t(n - 1)] - a;
        }
        if (p.k != 0) return W.D[size_t(p.k)] - a;
        return ((A.z - W.P[0].z) * v.z + (A.x - W.P[0].x) * v.x) - a;
    }
    // Direction −1.
    if (p.k >= n) {
        p.k = n;
        const vec3& P = W.P[size_t(n - 1)];
        const vec3 A = np == 0 ? P : (p.prevDir == 1 ? Wp.P[0] : Wp.P[size_t(np - 1)]);
        vec3 v(A.x - P.x, A.y - P.y, A.z - P.z);
        const float m = (v.z * v.z + v.x * v.x) + v.y * v.y;
        const float i = m == 0.0f ? 0.0f : 1.0f / std::sqrt(m);
        v.x = v.x * i;
        v.z = v.z * i;
        const float dx = p.p.x - P.x, dz = p.p.z - P.z;
        const float a = dz * v.z + dx * v.x;
        if (a < 0.0f) {
            p.k = p.k - 1;
            buildCurve(p, p.k, p.o);
        }
        return W.D[size_t(p.k - 1)] + a;  // one segment short on the frame it leaves the piece (MM2)
    }
    // k = 0 with d = −1 (only after "return −a" with a exactly 0): W[−1] is the point stored just
    // before W[0], the last point of line L − 1, read with Z[0]; Dw[−1] counts as 0. A side
    // without lanes has nothing there: the engine uses W[0] and Z[1] (pedestrian-walking.md).
    vec3 P;
    vec3 Zk;
    float dPrev;
    if (p.k >= 1) {
        P = W.P[size_t(p.k - 1)];
        Zk = rd.Z[size_t(p.k)];
        dPrev = W.D[size_t(p.k - 1)];
    } else {
        p.k = 0;
        const RoadSide& sd = rd.sideOf(p.side);
        const int L = std::max(sd.lanes, 0);
        if (L >= 1 && size_t(L - 1) < sd.lines.size() && !sd.lines[size_t(L - 1)].P.empty()) {
            P = sd.lines[size_t(L - 1)].P.back();
            Zk = rd.Z[0];
        } else {
            P = W.P[0];
            Zk = rd.Z[1];
        }
        dPrev = 0.0f;
    }
    const float dx = p.p.x - P.x, dz = p.p.z - P.z;
    const float a = dz * Zk.z + dx * Zk.x;
    if (a < 0.0f) return dPrev - a;
    p.k = p.k - 1;
    if (p.k <= 0) {
        p.k = 0;
        return -a;
    }
    buildCurve(p, p.k, offsetAt(p.k));
    return W.D[size_t(p.k)] - a;
}

void PedestrianSystem::matrixFromHeading(Pedestrian& p) const {
    const float c = fcos(p.h), s = fsin(p.h);
    p.X = vec3(-c, 0.0f, s);
    p.Y = vec3(0.0f, 1.0f, 0.0f);
    p.Z = vec3(-s, 0.0f, -c);
}

void PedestrianSystem::turnToward(Pedestrian& p, const vec3& t) const {
    const float dx = t.x - p.p.x, dz = t.z - p.p.z;
    const float a = -fatan2(dx * p.X.x + dz * p.X.z, -(dx * p.Z.x + dz * p.Z.z));
    if (a > k015) p.h = k015 + p.h;
    else if (a < -k015) p.h = p.h - k015;
    else p.h = a + p.h;
}

// ---------------------------------------------------------------------------------------------
// Placing (pedestrians.md, "Placing a pedestrian")

void PedestrianSystem::place(int pi, int road, int side) {
    Pedestrian& p = peds_[size_t(pi)];
    const Road& rd = net_->roads()[size_t(road)];
    const RoadSide& sd = rd.sideOf(side);
    const int L = std::max(sd.lanes, 0);
    const int n = rd.n;
    p.onStreets = true;
    p.lost = false;
    // 1.
    p.road = p.prevRoad = road;
    p.side = p.prevSide = side;
    p.wall = 0;
    p.wallNear = false;
    const Lane& W = sidewalk(road, side);
    // 2. Distance.
    p.x = rng_.unit() * W.D[size_t(n - 1)];
    // 3. Lateral offset.
    const int i1 = std::min(2 * L + 1, 9), i0 = std::min(2 * L, 9);
    const float raw = fsin(rng_.unit() * k2pi) * (((sd.band[i1] - sd.band[i0]) * 0.5f) - 0.5f);
    // 4.
    p.X = vec3(1, 0, 0);
    p.Y = vec3(0, 1, 0);
    p.Z = vec3(0, 0, 1);
    p.k = segmentOf(p, p.x);
    // 5. Direction.
    p.dir = rng_.unit() < 0.5f ? 1 : -1;
    p.prevDir = p.dir;
    // 6. Heading.
    p.h = heading(p, p.x, L, p.dir);
    // 7. Position on the sidewalk line.
    {
        const vec3& a = W.P[size_t(p.k - 1)];
        const vec3& b = W.P[size_t(p.k)];
        const float den = W.D[size_t(p.k)] - W.D[size_t(p.k - 1)];
        const float w = den != 0.0f ? (p.x - W.D[size_t(p.k - 1)]) / den : 0.0f;
        p.p = vec3((b.x - a.x) * w + a.x, (b.y - a.y) * w + a.y, (b.z - a.z) * w + a.z);
    }
    // 8. The curve with the offset; x and z from it.
    buildCurve(p, p.k, raw);
    {
        const float u = (p.x - W.D[size_t(p.k - 1)]) * p.curve.R;
        const vec3 c = RoadNetwork::curvePositionAt(p.curve, u);
        p.p.x = c.x;
        p.p.z = c.z;
    }
    // 9. Ground.
    p.probe = phys::ProbeState();
    p.wallProbe = phys::ProbeState();
    {
        phys::ProbeState st;
        st.roomS = st.roomE = rd.rooms.empty() ? 0 : rd.rooms[0];
        phys::ProbeHit hit;
        if (world_->probe(vec3(p.p.x, p.p.y + 5.0f, p.p.z), vec3(p.p.x, p.p.y - 5.0f, p.p.z), st, hit)) p.p.y = hit.point.y;
    }
    // 10.
    p.mode = PedWalking;
    p.step = PedStepWalk;
    p.seenMode = p.seenStep = -1;
    p.choice = 0;
    // 11.
    pushFront(pi, road);
    // 12.
    matrixFromHeading(p);
    // 13. Animation (the shared accumulator too), WALK at a random frame.
    p.cur = p.next = -1;
    p.frame = 0;
    resetClock();
    startSeq(p, PsWALK);
    {
        const PedSequence* s = curSeq(p);
        const float r = rng_.unit();
        p.frame = s ? int(std::trunc(r * float(s->frameCount))) : 0;
    }
    // 14.
    p.room = rd.rooms.empty() ? 0 : rd.rooms[0];
    // 15.
    targetAt(p, (float(p.dir) * 6.0f) + p.x);
    // 16.
    p.q = 100000.0f;
    p.nearest = -1;
    p.speed = 0;
    p.diveStart = 0;
    p.reverse = false;
    p.voiceReset = true;
    ++stats_.placed;
}

// ---------------------------------------------------------------------------------------------
// The frame (pedestrians.md, "Frame order")

void PedestrianSystem::update(float F, const std::vector<PedPlayer>& players, const TrafficSystem* traffic) {
    if (!active()) return;
    traffic_ = traffic;
    ++frame_;
    order_.clear();
    voices_.clear();
    // 1. Players: room tracking and the pedestrian adjust; the bookkeeping pedestrians use.
    if (playerRoom_.size() != players.size()) playerRoom_.resize(players.size(), 0);
    vhat_.assign(players.size(), vec3());
    invSpeed_.assign(players.size(), 0.0f);
    for (size_t i = 0; i < players.size(); ++i) {
        // Room = Find(P_c, memory); the memory takes the room found every frame, 0 included
        // (pedestrians.md, "Rooms and pedestrian roads").
        const int room = city_->findRoom(players[i].bodyPos, playerRoom_[i]);
        if (room != 0 && room != playerRoom_[i]) adjust(playerRoom_[i], room, int(i));
        playerRoom_[i] = room;
        const vec3& v = players[i].velocity;
        const float m = (v.x * v.x + v.y * v.y) + v.z * v.z;
        if (m > 0.0f) {
            const float s = 1.0f / std::sqrt(m);
            vhat_[i] = vec3(v.x * s, v.y * s, v.z * s);
        }
        invSpeed_[i] = players[i].speed > 0.001f ? 1.0f / players[i].speed : 0.0f;
    }
    // 3. Every road on the active list from the front; each list walked as it is now. Engine: no
    // pedestrian is updated twice in a frame.
    const std::vector<int> roads = activeRoads_;
    const int guardMax = 4 * int(peds_.size()) + 8;
    for (int r : roads) {
        const int first = head_[size_t(r)];
        int pi = first;
        for (int guard = 0; pi >= 0 && guard < guardMax; ++guard) {
            Pedestrian& p = peds_[size_t(pi)];
            if (p.onStreets && p.updatedFrame != frame_) {
                if (!p.lost) {
                    updateOne(p, F, players);
                } else {
                    // A lost pedestrian keeps its place in the update order and ticks the clock.
                    p.updatedFrame = frame_;
                    order_.push_back(p.index);
                    tickClock(F);
                }
            }
            pi = p.nextInRoad;
            if (pi == first) break;
        }
    }
}

const PedSequence* PedestrianSystem::curSeq(const Pedestrian& p) const {
    const PedModel* m = modelOf(p);
    if (!m || p.cur < 0 || size_t(p.cur) >= m->sequences().size()) return nullptr;
    return &m->sequences()[size_t(p.cur)];
}

void PedestrianSystem::startSeq(Pedestrian& p, int named) {
    const PedModel* m = modelOf(p);
    const int i = m ? m->seq(named) : -1;
    if (i < 0) return;
    p.cur = i;
    p.frame = 0;
    p.next = m->sequences()[size_t(i)].next;
}

void PedestrianSystem::queueSeq(Pedestrian& p, int named) {
    const PedModel* m = modelOf(p);
    const int i = m ? m->seq(named) : -1;
    if (i >= 0) p.next = i;
}

bool PedestrianSystem::isSeq(const Pedestrian& p, int named) const {
    const PedModel* m = modelOf(p);
    const int i = m ? m->seq(named) : -1;
    return i >= 0 && p.cur == i;
}

int PedestrianSystem::tickClock(float F) {
    acc_ = F * 30.0f + acc_;
    const int step = int(std::floor(acc_));
    acc_ = acc_ - float(step);
    lastStep_ = step;
    return step;
}

void PedestrianSystem::advance(Pedestrian& p, float F) {
    const int step = tickClock(F);
    const PedSequence* s = curSeq(p);
    if (!s) return;
    const PedModel* m = modelOf(p);
    auto restart = [&](int idx) {
        if (idx < 0 || size_t(idx) >= m->sequences().size()) idx = p.cur;
        p.cur = idx;
        p.frame = 0;
        p.next = m->sequences()[size_t(idx)].next;
    };
    if (s->direction > 0) {
        p.frame = p.frame + step;
        if (p.frame >= s->frameCount) restart(p.next);
    } else if (s->direction == 0) {
        if (step != 0) restart(p.next);
    } else {
        p.frame = p.frame - step;
        if (p.frame <= s->frameCount) restart(p.next);
    }
}

void PedestrianSystem::voice(Pedestrian& p, int reason) {
    PedVoiceEvent e;
    e.ped = p.index;
    e.pos = p.p;
    e.female = p.female;
    e.reason = reason;
    voices_.push_back(e);
    p.voiceReset = false;
}

void PedestrianSystem::updateOne(Pedestrian& p, float F, const std::vector<PedPlayer>& players) {
    // 1.
    p.updatedFrame = frame_;
    ++p.updates;
    order_.push_back(p.index);
    float latFactor = 1.0f;
    // 2. Nearest player.
    p.q = 1.0e9f;
    p.nearest = -1;
    for (size_t i = 0; i < players.size(); ++i) {
        const float dx = p.p.x - players[i].bodyPos.x, dz = p.p.z - players[i].bodyPos.z;
        const float e = dx * dx + dz * dz;
        if (e < p.q) {
            p.q = e;
            p.nearest = int(i);
        }
    }
    // 3. Reaction choice.
    chooseMode(p, players);
    // 4. Behaviour.
    switch (p.mode) {
    case PedWalking:
        switch (p.step) {
        case PedStepWalk: walking(p, players); break;
        case PedStepToKerb: toKerb(p, players); break;
        case PedStepWait: waiting(p, players); break;
        default: crossing(p, players); break;
        }
        break;
    case PedWary: wary(p, players); break;
    default: dodging(p, players, latFactor); break;
    }
    if (std::isnan(p.h)) {
        // Its heading became NaN (a NaN clearance or target): lost. MM2 keeps updating it with NaN;
        // the engine stops it here, and it only ticks the shared clock from now on.
        p.lost = true;
        ++stats_.lost;
        tickClock(F);
        return;
    }
    // 5. Matrix.
    matrixFromHeading(p);
    // 6. Move: f forward, l to its left.
    const PedSequence* s = curSeq(p);
    const float vf = s ? s->vf : 0.0f, vs = s ? s->vs : 0.0f;
    p.speed = vf;
    const float f = vf * F;
    const float l = (latFactor * vs) * F;
    p.p.x = p.p.x - ((f * p.Z.x) + (l * p.X.x));
    p.p.y = p.p.y - ((f * p.Z.y) + (l * p.X.y));
    p.p.z = p.p.z - ((f * p.Z.z) + (l * p.X.z));
    // 7. Height.
    const Road& rd = net_->roads()[size_t(p.road)];
    if ((rd.flags & 0x08) && p.q > 2500.0f && !rd.side[1].kerb.empty()) {
        p.p.y = rd.side[1].kerb[0].y;
    } else {
        p.probe.roomS = p.probe.roomE = p.room;
        phys::ProbeHit hit;
        if (world_->probe(vec3(p.p.x, p.p.y + 2.0f, p.p.z), vec3(p.p.x, p.p.y - 2.0f, p.p.z), p.probe, hit) &&
            std::fabs(hit.point.y - p.p.y) < 0.5f)
            p.p.y = hit.point.y;
    }
    // 8. Room.
    p.room = city_->findRoom(p.p, p.room);
    // 9.
    p.target.y = p.p.y;
    // 10. Animation on the shared clock.
    advance(p, F);
}

// ---------------------------------------------------------------------------------------------
// Choosing the mode (pedestrian-reactions.md)

void PedestrianSystem::chooseMode(Pedestrian& p, const std::vector<PedPlayer>& players) {
    const PedPlayer* pl = p.nearest >= 0 ? &players[size_t(p.nearest)] : nullptr;
    // 1. Backed against a wall.
    if (isSeq(p, PsBACKUP)) {
        bool away = true, far = true;
        if (pl) {
            const vec3 e = p.p - pl->bodyPos;
            const vec3& v = vhat_[size_t(p.nearest)];
            away = (e.z * v.z + e.y * v.y) + e.x * v.x < 0.0f;
            const float rr = pl->radius + pl->radius;
            far = p.q > rr * rr;
        }
        if (away && far) {
            p.mode = PedWalking;
            const float o = fsin(rng_.unit() * k2pi) * 1.5f;
            buildCurve(p, p.k, o);
        }
        return;
    }
    // 2. Diving, on the ground, getting up, or running as the wall flag says.
    static const int keep[] = {PsANTIC_LDIVE, PsANTIC_RDIVE, PsWALK_LDIVE,    PsWALK_RDIVE, PsLDIVE_GROUNDL,
                               PsRDIVE_GROUNDR, PsGROUND_STANDL, PsGROUND_STANDR, PsLDIVE,      PsRDIVE};
    for (int k : keep)
        if (isSeq(p, k)) return;
    if (isSeq(p, PsRUN) && p.mode == p.wall) return;
    // 3–4.
    int mode = PedWalking;
    if (pl && p.q < 1225.0f) {
        const bool rev = pl->gear == 0;
        const vec3 Fa = rev ? pl->Z : -pl->Z;
        const vec3 Sa = rev ? -pl->X : pl->X;
        const vec3 e = p.p - pl->bodyPos;
        const float ahead = (Fa.z * e.z + Fa.y * e.y) + Fa.x * e.x;
        const float lat = (Sa.z * e.z + Sa.y * e.y) + Sa.x * e.x;
        const vec3& ib = pl->inertiaBox;
        const float tau = (ahead - 2.0f) * invSpeed_[size_t(p.nearest)];
        if (pl->speed > 0.0f && ib.z * 0.25f < ahead && ahead < 20.0f && std::fabs(lat) < (ib.x * 0.5f) + 2.0f) {
            mode = pl->speed < 1.0f ? PedWalking : tau < 0.75f ? PedDodging : tau < 2.3f ? PedWary : PedWalking;
        } else {
            const float latp = ((-pl->X.z) * e.z + (-pl->X.y) * e.y) + (-pl->X.x) * e.x;
            if (pl->speed > 0.0f && ib.z * 0.25f < ahead && ahead < 35.0f && std::fabs(latp) < (ib.x * 0.5f) + 4.0f)
                mode = pl->speed < 1.0f ? PedWalking : tau < 2.3f ? PedWary : PedWalking;
        }
    }
    p.mode = mode;
}

// ---------------------------------------------------------------------------------------------
// Walking (pedestrian-walking.md, "Walking")

void PedestrianSystem::walking(Pedestrian& p, const std::vector<PedPlayer>& players) {
    if (p.mode != p.seenMode || p.step != p.seenStep) {
        if (isSeq(p, PsANTIC) || isSeq(p, PsWALK_ANTIC)) queueSeq(p, PsANTIC_WALK);
        else if (isSeq(p, PsRUN)) queueSeq(p, PsRUN_WALK);
        else if (isSeq(p, PsBACKUP)) {
            voice(p, 2);
            queueSeq(p, PsBACKUP_WALK);
        }
        p.wall = 0;
        p.seenMode = p.mode;
        p.seenStep = p.step;
    }
    walkCore(p, players);
}

void PedestrianSystem::walkCore(Pedestrian& p, const std::vector<PedPlayer>& players) {
    p.x = locate(p);
    float pf = 9999.0f, of = 9999.0f;
    int obstacle = -1;
    if (p.q < 36.0f) {
        float fwd;
        if (playerAhead(p, players, fwd)) pf = fwd;
    }
    {
        float d;
        int ob;
        if (obstacleAhead(p, d, ob)) {
            of = d;
            obstacle = ob;
        }
    }
    if (of < 9999.0f && of < pf) {
        const phys::CollisionObject& ob = world_->object(obstacle);
        stepAround(p, ob.O.o, ob.yRadius + 1.0f);
        buildCurve(p, p.k, p.o);
        return;
    }
    if (pf < of && pf < 9999.0f) {
        const PedPlayer& pl = players[size_t(p.nearest)];
        stepAround(p, pl.bodyPos, pl.radius + 1.0f);
        buildCurve(p, p.k, p.o);
        return;
    }
    if (isSeq(p, PsSTAND)) queueSeq(p, PsSTAND_WALK);
    targetAt(p, (float(p.dir) * 6.0f) + p.x);
    turnToward(p, p.target);
    roadEnd(p, p.x);
}

bool PedestrianSystem::playerAhead(const Pedestrian& p, const std::vector<PedPlayer>& players, float& fwd) const {
    if (p.nearest < 0) return false;
    const PedPlayer& pl = players[size_t(p.nearest)];
    const float dx = pl.bodyPos.x - p.p.x, dz = pl.bodyPos.z - p.p.z;
    fwd = (-p.Z.z) * dz + (-p.Z.x) * dx;
    const float lat = dz * p.X.z + dx * p.X.x;
    const float r = pl.radius;
    return -r < fwd && fwd < 6.0f && -r < lat && lat < r;
}

bool PedestrianSystem::obstacleAhead(const Pedestrian& p, float& dist, int& object) const {
    const Road& rd = net_->roads()[size_t(p.road)];
    const int n = rd.n;
    const int I = p.dir == 1 ? rd.end[0].intersection : rd.end[1].intersection;
    const auto& ixs = net_->intersections();
    const std::vector<int>* ixList = (I >= 0 && size_t(I) < ixs.size()) ? &ixs[size_t(I)].obstacles : nullptr;
    const auto& sideLists = rd.obstacles[sideSlot(p.side)];
    auto roadList = [&](int k) -> const std::vector<int>* {
        return (k >= 1 && k < n && size_t(k) < sideLists.size()) ? &sideLists[size_t(k)] : nullptr;
    };
    // The way from p to the current target.
    const vec3 tp(p.target.x - p.p.x, p.target.y - p.p.y, p.target.z - p.p.z);
    const float m = (tp.z * tp.z + tp.y * tp.y) + tp.x * tp.x;
    const float im = m == 0.0f ? 0.0f : 1.0f / std::sqrt(m);
    const vec3 u(tp.x * im, tp.y * im, tp.z * im);
    const float dzt = p.p.z - p.target.z, dxt = p.p.x - p.target.x;
    const float L = std::sqrt(dzt * dzt + dxt * dxt);
    auto scan = [&](const std::vector<int>* list) -> bool {
        if (!list) return false;
        for (int oi : *list) {
            const int co = obstacleObject(oi, propOf_, *world_);  // level objects too
            if (co < 0) continue;
            const phys::CollisionObject& ob = world_->object(co);
            if (ob.broken) continue;  // engine: a knocked prop is gone (MM2 tests it where it lies)
            const phys::Mtx& M = ob.O;
            const vec3 c(((M.o.x - M.X.x * ob.cg.x) - M.Y.x * ob.cg.y) - M.Z.x * ob.cg.z,
                         ((M.o.y - M.X.y * ob.cg.x) - M.Y.y * ob.cg.y) - M.Z.y * ob.cg.z,
                         ((M.o.z - M.X.z * ob.cg.x) - M.Y.z * ob.cg.y) - M.Z.z * ob.cg.z);
            const float lat = (c.x - p.p.x) * (-u.z) + u.x * (c.z - p.p.z);
            const float fwd = (c.x - p.p.x) * u.x + (c.z - p.p.z) * u.z;
            const float R = (0.25f + std::min(ob.yRadius, 2.0f)) + 1.0f;
            const float ang = fatan2(lat, fwd);
            if (-R < lat && lat < R && 0.0f < fwd && fwd < L && -0.7f < ang && ang < 0.7f) {
                dist = fwd;
                object = co;
                return true;
            }
        }
        return false;
    };
    // (1) where it is, (2) the next segment (or the intersection ahead).
    if (scan((p.k == 0 || p.k == n) ? ixList : roadList(p.k))) return true;
    const int k2 = p.k + p.dir;
    if (k2 == 0 || k2 == n) return scan(ixList);
    if (k2 >= 1 && k2 < n) return scan(roadList(k2));
    return false;
}

void PedestrianSystem::stepAround(Pedestrian& p, const vec3& Q, float r) {
    ++stats_.stepArounds;
    auto axis = [&](const Pedestrian& q, int& j) {
        const Road& rd = net_->roads()[size_t(q.road)];
        j = q.k >= rd.n ? rd.n - 1 : std::max(q.k, 0);
        const vec3& X = rd.X[size_t(j)];
        return q.dir == 1 ? -X : X;
    };
    int j = 0;
    vec3 A = axis(p, j);
    {
        const Lane& W = sidewalk(p.road, p.side);
        const vec3& w = W.P[size_t(std::min(j, int(W.P.size()) - 1))];
        const float pl = (p.p.x - w.x) * A.x + (p.p.z - w.z) * A.z;
        const float ql = (Q.x - w.x) * A.x + (Q.z - w.z) * A.z;
        const int n = net_->roads()[size_t(p.road)].n;
        if (ql + r <= 1.5f || -1.5f <= ql - r) {
            // Pass on its own side of the obstacle if there is room there, else on the other side.
            if (pl <= ql) {
                if (-1.5f <= ql - r) r = -r;
            } else {
                if (!(ql + r <= 1.5f)) r = -r;
            }
        } else if (p.k > 0 && p.k < n) {
            // The obstacle fills the sidewalk: turn round.
            p.dir = -p.dir;
            p.h = heading(p, p.x, 0, p.dir);
        } else {
            // On a crossing piece: back to the previous road.
            const int oldPrevSide = p.prevSide;
            const int oldRoad = p.road, oldSide = p.side, oldDir = p.dir;
            removeFromRoad(p.index, oldRoad);
            p.road = p.prevRoad;
            p.prevRoad = oldRoad;
            p.side = p.prevSide;
            p.prevSide = oldSide;
            p.dir = -p.prevDir;
            p.prevDir = oldDir;
            pushFront(p.index, p.road);
            p.h = p.h + k314;
            p.k = p.dir == 1 ? 0 : net_->roads()[size_t(p.road)].n;
            A = axis(p, j);
            p.x = locate(p);
            buildCurve(p, p.k, p.o);
            // MM2 reads the old previous side's integer as the clearance: +1 → 1.4e−45 (the engine
            // uses 0), −1 → NaN, which makes the target and the heading NaN: the pedestrian is lost.
            r = oldPrevSide == 1 ? 0.0f : std::numeric_limits<float>::quiet_NaN();
        }
    }
    p.target = vec3((r * A.x) + Q.x, (r * A.y) + Q.y, (r * A.z) + Q.z);
    // The turn variant: a small angle turns the wrong way (MM2 quirk, kept).
    const vec3 D(p.target.x - p.p.x, p.target.y - p.p.y, p.target.z - p.p.z);
    const float b = fatan2((D.z * p.X.z + D.y * p.X.y) + D.x * p.X.x, -((D.z * p.Z.z + D.y * p.Z.y) + D.x * p.Z.x));
    if (b > k015) p.h = p.h - k015;
    else if (b < -k015) p.h = k015 + p.h;
    else p.h = b + p.h;
    if (isSeq(p, PsSTAND)) queueSeq(p, PsSTAND_WALK);
}

// ---------------------------------------------------------------------------------------------
// Road ends (pedestrian-walking.md, "Road end")

void PedestrianSystem::roadEnd(Pedestrian& p, float x) {
    const int n = net_->roads()[size_t(p.road)].n;
    const bool past = p.dir == 1 ? (p.k >= 1 && x > sidewalkLength(p, 0, n - 1)) : (p.k < n && x < 0.0f);
    if (past) {
        p.prevSide = p.side;
        p.prevDir = p.dir;
        const int R = choose(p);
        removeFromRoad(p.index, p.road);
        p.prevRoad = p.road;
        p.road = R;
        pushFront(p.index, R);
        p.k = p.dir == 1 ? 0 : net_->roads()[size_t(R)].n;
        ++stats_.roadEnds;
    }
    p.x = locate(p);
    buildCurve(p, p.k, p.o);
}

bool PedestrianSystem::accidentAt(int I) const {
    if (!traffic_ || !traffic_->active()) return false;
    for (int ci : traffic_->intersectionCars(I)) {
        const auto& cars = traffic_->cars();
        if (ci >= 0 && size_t(ci) < cars.size() && cars[size_t(ci)].state != TrafficDriving) return true;
    }
    return false;
}

int PedestrianSystem::choose(Pedestrian& p) {
    const Road& rd = net_->roads()[size_t(p.road)];
    const int d = p.dir, s = p.side;
    const int I = d == 1 ? rd.end[0].intersection : rd.end[1].intersection;
    const Intersection& ix = net_->intersections()[size_t(I)];
    // 1.
    int c = 0;
    if (ix.lightSet >= 0) {
        const LightSet& set = net_->control().sets()[size_t(ix.lightSet)];
        if (set.kind != 0) c = rng_.next15() % 3;
    }
    // 2.
    if (accidentAt(I)) c = 0;
    // 3. The corner road.
    const int m = int(ix.roads.size());
    int K = p.road;
    if (m > 0) {
        const int sigma = d == s ? 1 : -1;
        int i = wrap(rd.end[d == 1 ? 0 : 1].slot + sigma, m);
        int tried = 1;
        while (ix.roads[size_t(i)] >= net_->mainRoadCount() && tried < m) {
            i = wrap(i + sigma, m);
            ++tried;
        }
        K = ix.roads[size_t(i)];
    }
    const bool kEnd0 = net_->roads()[size_t(K)].end[0].intersection == I;
    // 4.
    int nd = d, ns = s, R = K;
    if (c == 0) {
        if (d == s) nd = ns = kEnd0 ? -1 : 1;
        else {
            nd = kEnd0 ? -1 : 1;
            ns = -nd;
        }
    } else if (c == 1) {
        if (d == s) {
            nd = kEnd0 ? -1 : 1;
            ns = -nd;
        } else {
            nd = ns = kEnd0 ? -1 : 1;
        }
        p.step = PedStepToKerb;
    } else {
        nd = -d;
        ns = -s;
        R = p.road;
        p.step = PedStepToKerb;
    }
    // 5. Only sides open to pedestrians, on the populated roads; otherwise turn round.
    if (!pedSide(R, ns) || !isActive(R)) {
        R = p.road;
        nd = -d;
        ns = s;
        p.step = PedStepWalk;
        p.h = p.h + k314;
    } else if (p.step == PedStepToKerb) {
        ++stats_.crossings;
    }
    p.dir = nd;
    p.side = ns;
    p.choice = c;
    return R;
}

// ---------------------------------------------------------------------------------------------
// Crossing (pedestrian-walking.md, "Crossing a street")

vec3 PedestrianSystem::kerbPoint(int road, int side, bool endN) const {
    const Road& rd = net_->roads()[size_t(road)];
    const auto& E = rd.sideOf(side).kerb;
    const int n = rd.n;
    if (int(E.size()) < n || n < 1) return rd.C.empty() ? vec3() : rd.C[0];
    if (endN) {
        const vec3& e = E[size_t(n - 1)];
        const vec3& Z = rd.Z[size_t(n - 1)];
        return vec3(e.x - (Z.x * 2.5f), e.y - (Z.y * 2.5f), e.z - (Z.z * 2.5f));
    }
    const vec3& e = E[0];
    const vec3& Z = rd.Z[0];
    return vec3((Z.x * 2.5f) + e.x, (Z.y * 2.5f) + e.y, (Z.z * 2.5f) + e.z);
}

int PedestrianSystem::previousIntersection(const Pedestrian& p) const {
    const Road& rp = net_->roads()[size_t(p.prevRoad)];
    return p.prevDir == 1 ? rp.end[0].intersection : rp.end[1].intersection;
}

vec3 PedestrianSystem::nearKerb(const Pedestrian& p) const {
    if (p.choice == 2) return kerbPoint(p.prevRoad, p.prevSide, p.prevDir == 1);
    const bool endN = net_->roads()[size_t(p.road)].end[0].intersection == previousIntersection(p);
    if (p.prevDir == p.prevSide) return endN ? kerbPoint(p.road, -1, true) : kerbPoint(p.road, 1, false);
    return endN ? kerbPoint(p.road, 1, true) : kerbPoint(p.road, -1, false);
}

vec3 PedestrianSystem::farKerb(const Pedestrian& p) const {
    if (p.choice == 2) return kerbPoint(p.prevRoad, -p.prevSide, p.prevDir == 1);
    const bool endN = net_->roads()[size_t(p.road)].end[0].intersection == previousIntersection(p);
    if (p.prevDir == p.prevSide) return endN ? kerbPoint(p.road, 1, true) : kerbPoint(p.road, -1, false);
    return endN ? kerbPoint(p.road, -1, true) : kerbPoint(p.road, 1, false);
}

void PedestrianSystem::turnBack(Pedestrian& p) {
    p.dir = -p.prevDir;
    p.h = p.h + k314;
    p.side = p.prevSide;
    p.step = PedStepWalk;
    removeFromRoad(p.index, p.road);
    p.road = p.prevRoad;
    pushFront(p.index, p.road);
    p.k = p.dir == 1 ? 0 : net_->roads()[size_t(p.road)].n;
    queueSeq(p, PsWALK);
    ++stats_.turnBacks;
}

bool PedestrianSystem::playerInTheWay(const Pedestrian& p, const std::vector<PedPlayer>& players) const {
    if (p.nearest < 0 || !(players[size_t(p.nearest)].speed < 1.0f)) return false;
    float fwd;
    return playerAhead(p, players, fwd);
}

void PedestrianSystem::toKerb(Pedestrian& p, const std::vector<PedPlayer>& players) {
    if (p.mode != p.seenMode || p.step != p.seenStep) {
        p.target = nearKerb(p);
        queueSeq(p, PsWALK);
        p.seenMode = p.mode;
        p.seenStep = p.step;
    }
    turnToward(p, p.target);
    if (playerInTheWay(p, players)) {
        turnBack(p);
        return;
    }
    if (flat2(p.target, p.p) < 1.0f) p.step = PedStepWait;
}

void PedestrianSystem::waiting(Pedestrian& p, const std::vector<PedPlayer>& players) {
    if (p.mode != p.seenMode || p.step != p.seenStep) {
        p.target = farKerb(p);
        if (rng_.unit() >= 0.5f) queueSeq(p, PsSTAND2);
        else queueSeq(p, PsSTAND);
        p.seenMode = p.mode;
        p.seenStep = p.step;
    }
    turnToward(p, p.target);
    const int I = previousIntersection(p);
    bool accident = accidentAt(I);
    if (!accident && traffic_ && traffic_->active()) {
        const int n = net_->roads()[size_t(p.road)].n;
        const int seg = p.dir == 1 ? 1 : n - 1;
        for (int side : {-1, 1})
            for (int ci : traffic_->sectionCars(p.road, side, seg))
                if (ci >= 0 && size_t(ci) < traffic_->cars().size() && traffic_->cars()[size_t(ci)].state != TrafficDriving)
                    accident = true;
    }
    if (accident || playerInTheWay(p, players)) {
        turnBack(p);
        return;
    }
    if (flat2(p.target, p.p) < 1.0f) p.step = PedStepToKerb;
    const Intersection& ix = net_->intersections()[size_t(I)];
    if (ix.lightSet >= 0) {
        const LightSet& set = net_->control().sets()[size_t(ix.lightSet)];
        if (set.walk && !set.lights.empty() && set.lights[0].state == LightWalk) p.step = PedStepCross;
    }
}

void PedestrianSystem::crossing(Pedestrian& p, const std::vector<PedPlayer>& players) {
    if (p.mode != p.seenMode || p.step != p.seenStep) {
        queueSeq(p, PsWALK);
        p.seenMode = p.mode;
        p.seenStep = p.step;
    }
    turnToward(p, p.target);
    const Intersection& ix = net_->intersections()[size_t(previousIntersection(p))];
    if (ix.lightSet >= 0) {
        const LightSet& set = net_->control().sets()[size_t(ix.lightSet)];
        if (!set.lights.empty() && set.lights[0].state == LightWalkEnding) queueSeq(p, PsRUN);
    }
    if (playerInTheWay(p, players)) {
        turnBack(p);
        return;
    }
    if (flat2(p.target, p.p) < 1.5f) p.step = PedStepWalk;
}

// ---------------------------------------------------------------------------------------------
// Wary and dodging (pedestrian-reactions.md)

void PedestrianSystem::wallProbe(Pedestrian& p) {
    const Road& rd = net_->roads()[size_t(p.road)];
    const vec3& X = rd.X[size_t(p.k)];
    vec3 S, E;
    if (p.side == 1) {
        S = vec3((X.x + X.x) + p.p.x, (X.y + X.y) + p.p.y, (X.z + X.z) + p.p.z);
        E = vec3(p.p.x - X.x * 10.0f, p.p.y - X.y * 10.0f, p.p.z - X.z * 10.0f);
    } else {
        S = vec3(p.p.x - (X.x + X.x), p.p.y - (X.y + X.y), p.p.z - (X.z + X.z));
        E = vec3((X.x * 10.0f) + p.p.x, (X.y * 10.0f) + p.p.y, (X.z * 10.0f) + p.p.z);
    }
    S.y = S.y + 1.0f;
    E.y = E.y + 1.0f;
    p.wallProbe.roomS = p.wallProbe.roomE = p.room;
    phys::ProbeHit hit;
    if (world_->probe(S, E, p.wallProbe, hit)) {
        p.wall = 1;
        p.wallHit = hit.point;
        const vec3 d = p.p - hit.point;
        p.wallNear = std::sqrt((d.z * d.z + d.y * d.y) + d.x * d.x) < 1.25f;
    } else {
        p.wall = 0;
        p.wallNear = false;
    }
}

void PedestrianSystem::backUp(Pedestrian& p, bool sideDependent) {
    const vec3& X = net_->roads()[size_t(p.road)].X[size_t(p.k)];
    const vec3& H = p.wallHit;
    if (!sideDependent || p.side == 1) {
        p.p.x = (X.x * 0.2f) + H.x;
        p.p.z = (X.z * 0.2f) + H.z;
        p.h = fatan2(X.x, X.z);
    } else {
        p.p.x = H.x - X.x * 0.2f;
        p.p.z = H.z - X.z * 0.2f;
        p.h = fatan2(-X.x, -X.z);
    }
    startSeq(p, PsBACKUP);
    ++stats_.backups;
}

void PedestrianSystem::wary(Pedestrian& p, const std::vector<PedPlayer>& players) {
    const PedPlayer& pl = players[size_t(p.nearest)];
    const Road& rd = net_->roads()[size_t(p.road)];
    const int n = rd.n;
    if (p.k == 0 || p.k == n) {
        if (p.mode != p.seenMode) {
            ++stats_.wary;
            if (isSeq(p, PsWALK)) queueSeq(p, PsWALK_ANTIC);
            else if (isSeq(p, PsSTAND)) queueSeq(p, PsSTAND_ANTIC);
            else if (isSeq(p, PsSTAND_WALK)) queueSeq(p, PsWALK_ANTIC);
            else if (isSeq(p, PsWALK_STAND)) queueSeq(p, PsSTAND_ANTIC);
            p.seenMode = p.mode;
        }
        p.h = fatan2(pl.bodyPos.x - p.p.x, pl.bodyPos.z - p.p.z);
        return;
    }
    wallProbe(p);
    const vec3 X = rd.X[size_t(p.k)];
    if (p.mode != p.seenMode) {
        ++stats_.wary;
        if (p.wall == 0) {
            if (isSeq(p, PsWALK)) {
                if (rng_.unit() < 0.5f) {
                    startSeq(p, PsWALK_ANTIC);
                } else {
                    startSeq(p, PsRUN);
                    const vec3 Fa = pl.gear == 0 ? pl.Z : -pl.Z;
                    const vec3& c1 = rd.C[size_t(p.k)];
                    const vec3& c0 = rd.C[size_t(p.k - 1)];
                    const vec3 a(c1.x - c0.x, c1.y - c0.y, c1.z - c0.z);
                    const int dn = ((Fa.z * a.z + Fa.y * a.y) + Fa.x * a.x) > 0.0f ? 1 : -1;
                    if (dn != p.dir) {
                        p.dir = dn;
                        p.x = locate(p);
                        p.h = heading(p, p.x, 0, p.dir);
                        if (p.dir == 1) p.h = p.h > 0.0f ? p.h - k314 : p.h + k314;
                    }
                }
            } else if (isSeq(p, PsSTAND)) {
                queueSeq(p, PsSTAND_ANTIC);
            } else if (isSeq(p, PsSTAND_WALK)) {
                queueSeq(p, PsWALK_ANTIC);
            } else if (isSeq(p, PsWALK_STAND)) {
                queueSeq(p, PsSTAND_ANTIC);
            }
        } else if (!p.wallNear) {
            p.h = p.side == 1 ? fatan2(-X.x, -X.z) : fatan2(X.x, X.z);
            startSeq(p, PsRUN);
        } else {
            backUp(p, true);
        }
        p.seenMode = p.mode;
    }
    if (p.wall == 1) {
        if (isSeq(p, PsRUN) && p.wallNear) backUp(p, true);
    } else if (isSeq(p, PsRUN)) {
        walkCore(p, players);
    } else {
        p.h = fatan2(pl.bodyPos.x - p.p.x, pl.bodyPos.z - p.p.z);
    }
}

void PedestrianSystem::diveStart(Pedestrian& p, const PedPlayer& pl, bool crossingPiece) {
    const vec3 e = p.p - pl.bodyPos;
    p.reverse = pl.gear == 0;
    const vec3 G = p.reverse ? -pl.Z : pl.Z;
    const vec3 S = p.reverse ? -pl.X : pl.X;
    p.h = fatan2(G.x, G.z);
    const float d = crossingPiece ? (S.x * e.x + S.z * e.z) + S.y * e.y : (S.z * e.z + S.y * e.y) + S.x * e.x;
    p.diveStart = d;
    bool left;
    if (pl.steer > 0.85f) left = false;
    else if (pl.steer < -0.85f) left = true;
    else left = d > 0.0f;
    if (isSeq(p, PsWALK)) startSeq(p, left ? PsWALK_LDIVE : PsWALK_RDIVE);
    else startSeq(p, left ? PsANTIC_LDIVE : PsANTIC_RDIVE);
    voice(p, 0);
    ++stats_.dives;
}

void PedestrianSystem::diveControl(Pedestrian& p, const PedPlayer& pl, float& latFactor) const {
    const vec3 G = p.reverse ? -pl.Z : pl.Z;
    const vec3 S = p.reverse ? -pl.X : pl.X;
    const vec3 e = p.p - pl.bodyPos;
    const float lat = (S.x * e.x + S.z * e.z) + S.y * e.y;
    const PedSequence* s = curSeq(p);
    const float v = (s ? s->vs : 0.0f) * 0.033330001f;
    const float f = float(p.frame);
    if (isSeq(p, PsLDIVE_GROUNDL) || isSeq(p, PsRDIVE_GROUNDR)) {
        if (std::fabs(lat) < std::fabs(((f * v) + p.diveStart) + 2.54f)) latFactor = 5.0f;
    } else if (p.frame > 12 && std::fabs(lat) < std::fabs((f * v) + p.diveStart)) {
        latFactor = 3.0f;
    }
    p.h = fatan2(G.x, G.z);
}

void PedestrianSystem::dodging(Pedestrian& p, const std::vector<PedPlayer>& players, float& latFactor) {
    const PedPlayer& pl = players[size_t(p.nearest)];
    const Road& rd = net_->roads()[size_t(p.road)];
    if (p.k == 0 || p.k == rd.n) {
        if (p.mode != p.seenMode) {
            diveStart(p, pl, true);
            p.seenMode = p.mode;
        }
        diveControl(p, pl, latFactor);
        return;
    }
    wallProbe(p);
    const vec3 X = rd.X[size_t(p.k)];
    if (p.mode != p.seenMode) {
        if (p.wall == 0) {
            diveStart(p, pl, false);
        } else if (!p.wallNear) {
            p.h = fatan2(-X.x, -X.z);  // for both sides (MM2)
            voice(p, 1);
            startSeq(p, PsRUN);
        } else {
            backUp(p, false);
        }
        p.seenMode = p.mode;
    }
    if (isSeq(p, PsRUN)) {
        if (p.wall == 1 && p.wallNear) backUp(p, false);  // a missed probe counts as not near (engine)
        return;
    }
    if (isSeq(p, PsBACKUP)) return;
    diveControl(p, pl, latFactor);
}

}  // namespace ai
}  // namespace cc
