#include "ai/road_network.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "ai/traffic_control.h"
#include "city/city_data.h"
#include "core/game_random.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/banger.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace ai {

namespace {

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

const std::vector<Component> kNoComponents;
const std::vector<uint16_t> kNoRoads;

// v + o·X per component.
vec3 offsetBy(const vec3& v, float o, const vec3& X) { return vec3(v.x + o * X.x, v.y + o * X.y, v.z + o * X.z); }
vec3 scaled(float L, const vec3& v) { return vec3(L * v.x, L * v.y, L * v.z); }
vec3 neg(const vec3& v) { return vec3(-v.x, -v.y, -v.z); }

Road roadFromFile(const BaiRoad& b) {
    Road r;
    r.index = b.index;
    r.n = b.n;
    r.flags = b.flags;
    for (int16_t v : b.rooms) r.rooms.push_back(v);
    r.halfWidth = b.halfWidth;
    for (int k = 0; k < 2; ++k) {
        const BaiSide& s = b.side[k];
        RoadSide& d = r.side[k];
        d.lanes = s.lanes;
        d.sidewalks = s.sidewalks;
        d.cable = s.cable;
        d.subway = s.subway;
        d.flags = s.flags;
        for (int i = 0; i < 10; ++i) d.band[i] = s.band[i];
        d.lines.resize(s.lines.size());
        for (size_t l = 0; l < s.lines.size(); ++l) {
            d.lines[l].P = s.lines[l];
            d.lines[l].D = s.distances[l];
        }
        d.cableLine = s.cableLine;
        d.subwayLine = s.subwayLine;
        d.kerb = s.kerb;
        d.outerEdge = s.outerEdge;
    }
    r.CD = b.centreDist;
    r.C = b.C;
    r.X = b.X;
    r.Y = b.Y;
    r.Z = b.Z;
    r.T = b.T;
    for (int e = 0; e < 2; ++e) {
        const BaiEnd& s = b.end[e];
        RoadEnd& d = r.end[e];
        d.intersection = int(s.intersection);
        d.lightSlot = -1;  // the file holds junk; set by the light setup
        d.control = s.control;
        d.turnRule = s.turnRule;
        d.slot = int(s.slot);
        d.signPos = s.signPos;
        d.facingPoint = s.facingPoint;
    }
    return r;
}

// Distances recomputed from the points: entry i = entry i−1 + √((dz·dz + dy·dy) + dx·dx).
void recomputeDistances(Lane& l) {
    l.D.assign(l.P.size(), 0.0f);
    for (size_t i = 1; i < l.P.size(); ++i) {
        const float dx = l.P[i].x - l.P[i - 1].x, dy = l.P[i].y - l.P[i - 1].y, dz = l.P[i].z - l.P[i - 1].z;
        l.D[i] = l.D[i - 1] + std::sqrt((dz * dz + dy * dy) + dx * dx);
    }
}

Lane reversed(const Lane& l) {
    Lane r;
    r.P.assign(l.P.rbegin(), l.P.rend());
    return r;
}

}  // namespace

RoadNetwork::RoadNetwork() : control_(std::make_unique<TrafficControl>()) {}
RoadNetwork::~RoadNetwork() = default;

const std::vector<AmbientType>& RoadNetwork::ambientTypes() const {
    return raceAimap_.ambient.empty() ? cityAimap_.ambient : raceAimap_.ambient;
}

void RoadNetwork::addComponent(int room, Component c) {
    if (room <= 0 || size_t(room) >= rooms_.size()) return;
    auto& l = rooms_[size_t(room)];
    for (const Component& x : l)
        if (x == c) return;
    l.push_back(c);
}

void RoadNetwork::registerRoadRooms(int ri, int kind) {
    const Road& r = roads_[size_t(ri)];
    for (int i = 1; i <= r.n - 2; ++i) addComponent(city_->findRoom(r.C[size_t(i)], 0), {ri, kind});
}

bool RoadNetwork::load(const Vfs& vfs, AssetCache& assets, CityData& city, const std::string& raceName,
                       std::string& message) {
    loaded_ = false;
    roads_.clear();
    ix_.clear();
    graph_.clear();
    stats_ = Stats();
    city_ = &city;
    const std::string base = "city/" + city.raceDir();  // AI files by RaceDir (city-files.md)
    // The aimaps are read before the network.
    cityAimap_ = parseCityAimap(textOf(vfs.open(base + ".aimap")));
    raceAimap_ = RaceAimap();
    raceName_ = raceName;
    if (BytesPtr rb = vfs.open("race/" + city.raceDir() + "/" + raceName + ".aimap")) raceAimap_ = parseRaceAimap(textOf(rb));

    BytesPtr mb = vfs.open(base + ".bai");
    if (!mb) {
        message = "no AI network";
        return false;
    }
    BaiFile main, sup;
    std::string err;
    if (!parseBaiFile(mb->data(), mb->size(), false, main, err)) {
        logFile(LogLevel::Warn, vfs.storedName(base + ".bai"), "AI network unreadable: " + err);
        message = "AI network unreadable";
        return false;
    }
    if (!err.empty()) logFile(LogLevel::Warn, vfs.storedName(base + ".bai"), err);
    bool haveSup = false;
    if (BytesPtr sb = vfs.open(base + "_sup.bai")) {
        std::string serr;
        if (parseBaiFile(sb->data(), sb->size(), true, sup, serr)) {
            haveSup = true;
            if (!serr.empty()) logFile(LogLevel::Warn, vfs.storedName(base + "_sup.bai"), serr);
        } else {
            logFile(LogLevel::Warn, vfs.storedName(base + "_sup.bai"), "shortcuts unreadable (" + serr + "); not used");
        }
    }
    const int NR = int(main.roads.size());
    const int NS = haveSup ? int(sup.roads.size()) : 0;
    mainRoads_ = NR;
    rooms_.assign(city.rooms().size(), {});

    // 1-2. Main roads, registered in the room lists.
    roads_.reserve(size_t(NR + NS));
    for (int i = 0; i < NR; ++i) {
        roads_.push_back(roadFromFile(main.roads[size_t(i)]));
        roads_.back().index = i;
    }
    for (int i = 0; i < NR; ++i) registerRoadRooms(i, 1);

    // 3. Intersections.
    const int NI = int(main.intersections.size());
    ix_.resize(size_t(NI));
    for (int i = 0; i < NI; ++i) {
        const BaiIntersectionRec& rec = main.intersections[size_t(i)];
        Intersection& x = ix_[size_t(i)];
        x.index = rec.index;
        x.room = rec.room;
        x.centre = rec.centre;
        for (uint32_t r : rec.roads) x.roads.push_back(int(r));
        addComponent(x.room, {i, 3});
    }
    bool badRefs = false;
    for (const Road& r : roads_)
        for (const RoadEnd& e : r.end)
            if (e.intersection < 0 || e.intersection >= NI) badRefs = true;
    for (const Intersection& x : ix_)
        for (int r : x.roads)
            if (r < 0 || r >= NR) badRefs = true;
    if (badRefs) {
        logFile(LogLevel::Error, vfs.storedName(base + ".bai"), "AI network refers to missing roads or intersections; not used");
        message = "AI network broken";
        return false;
    }

    // Heavy props of each intersection's room (registered with the intersection in step 3).
    {
        // Every prop object of the room (the colliding level objects too), in its list order.
        const auto roomProps = obstacleCandidates(city, vfs);
        for (Intersection& x : ix_) {
            if (x.room <= 0 || size_t(x.room) >= roomProps.size()) continue;
            for (const ObstacleCandidate& c : roomProps[size_t(x.room)])
                if (c.hasData && c.impulseLimit2 > 7.5e7f) x.obstacles.push_back(c.id);
        }
    }

    // 4. Shortcuts.
    for (int i = 0; i < NS; ++i) {
        Road r = roadFromFile(sup.roads[size_t(i)]);
        r.index = NR + i;
        r.shortcut = true;
        r.side[0].flags = 3;
        r.side[1].flags = 3;
        if (r.end[0].intersection < 0 || r.end[0].intersection >= NI || r.end[1].intersection < 0 ||
            r.end[1].intersection >= NI) {
            logFile(LogLevel::Warn, vfs.storedName(base + "_sup.bai"), strf("shortcut %d names a missing intersection; skipped", i));
            continue;
        }
        roads_.push_back(r);
        const int ri = int(roads_.size()) - 1;
        mergeShortcut(ri, r.end[1].intersection, city);
        mergeShortcut(ri, r.end[0].intersection, city);
        registerRoadRooms(ri, 2);
        // Kerb lines: side B then side A, sampled about once a metre per segment.
        const Road& rr = roads_[size_t(ri)];
        const int roomA = ix_[size_t(rr.end[0].intersection)].room, roomB = ix_[size_t(rr.end[1].intersection)].room;
        for (int sideK : {1, 0}) {
            const std::vector<vec3>& E = rr.side[sideK].kerb;
            if (int(E.size()) < rr.n) continue;
            for (int k = 0; k + 1 < rr.n; ++k) {
                const int c = int(std::trunc(rr.CD[size_t(k + 1)] - rr.CD[size_t(k)]));
                for (int j = 0; j < c; ++j) {
                    const float w = float(j) / float(c);
                    const vec3 p((E[size_t(k + 1)].x - E[size_t(k)].x) * w + E[size_t(k)].x,
                                 (E[size_t(k + 1)].y - E[size_t(k)].y) * w + E[size_t(k)].y,
                                 (E[size_t(k + 1)].z - E[size_t(k)].z) * w + E[size_t(k)].z);
                    const int room = city.findRoom(p, 0);
                    if (room == roomA || room == roomB) continue;
                    addComponent(room, {ri, 2});
                }
            }
        }
    }

    // 5. Light sets (poles become street props of the city).
    control_->setup(*this, city, vfs, assets, cityAimap_.lightSingle, cityAimap_.lightDual);

    // 6. Street props as obstacles on the main roads.
    registerObstacles(city, vfs);

    // 7. Activation tables.
    ambientTable_ = main.ambientTable;
    pedTable_ = main.pedTable;

    // 8-9.
    buildBends();
    if (cityAimap_.driveOnLeft) mirrorForLeft();
    applySpeedLimits();
    buildGraph();

    stats_.mainRoads = NR;
    stats_.supRoads = int(roads_.size()) - NR;
    stats_.intersections = NI;
    stats_.tableRooms = int(ambientTable_.size());
    for (size_t ri = 0; ri < roads_.size(); ++ri) {
        const Road& r = roads_[ri];
        if (r.end[0].intersection == r.end[1].intersection) continue;
        for (int e = 0; e < 2; ++e) {
            const Intersection& x = ix_[size_t(r.end[e].intersection)];
            const int slot = r.end[e].slot;
            if (slot < 0 || size_t(slot) >= x.roads.size() || x.roads[size_t(slot)] != int(ri)) ++stats_.listMismatches;
        }
    }
    control_->reset();
    loaded_ = true;
    message = strf("%d roads (+%d shortcuts), %d intersections, %zu light sets", NR, stats_.supRoads, NI,
                   control_->sets().size());
    return true;
}

void RoadNetwork::mergeShortcut(int r, int I, const CityData& city) {
    Intersection& x = ix_[size_t(I)];
    x.roads.push_back(r);
    if (x.room > 0 && size_t(x.room) < city.rooms().size()) x.centre = city.rooms()[size_t(x.room)].centre;
    const vec3 c = x.centre;
    struct Keyed {
        float key;
        int road;
    };
    std::vector<Keyed> keyed;
    for (int q : x.roads) {
        const Road& road = roads_[size_t(q)];
        const vec3& p = road.end[0].intersection == I ? road.C[size_t(road.n - 1)] : road.C[0];
        const float dx = p.x - c.x, dz = p.z - c.z;
        keyed.push_back({float(std::atan2(double(dx), double(dz))), q});
    }
    std::stable_sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) { return a.key < b.key; });
    for (size_t k = 0; k < keyed.size(); ++k) {
        x.roads[k] = keyed[k].road;
        Road& road = roads_[size_t(keyed[k].road)];
        if (road.end[0].intersection == I) road.end[0].slot = int(k);
        else road.end[1].slot = int(k);
    }
}

int obstacleObject(int entry, const std::vector<int>& propOf, const phys::CollisionWorld& world) {
    if (entry >= kLevelObstacleBase) return world.levelObjectId(entry - kLevelObstacleBase);
    if (entry < 0 || size_t(entry) >= propOf.size()) return -1;
    return propOf[size_t(entry)];
}

std::vector<std::vector<RoadNetwork::ObstacleCandidate>> RoadNetwork::obstacleCandidates(const CityData& city,
                                                                                        const Vfs& vfs) {
    struct Entry {
        ObstacleCandidate c;
        long long major = 0;  // creation order (larger = newer = nearer the front of the list)
        int minor = 0;
    };
    std::vector<std::vector<Entry>> rooms(city.rooms().size());
    std::map<std::string, BangerData> banger;
    std::map<std::string, bool> found;
    const auto& objs = city.objects();
    for (size_t oi = 0; oi < objs.size(); ++oi) {
        const CityObject& o = objs[oi];
        if (o.kind != CityObjectKind::Breakable || o.room <= 0 || size_t(o.room) >= rooms.size()) continue;
        auto it = banger.find(o.name);
        if (it == banger.end()) {
            BangerData b;
            found[o.name] = loadBangerData(vfs, o.name, b);
            it = banger.emplace(o.name, b).first;
        }
        Entry e;
        e.c.id = int(oi);
        e.c.pos = city.propPosition(int(oi), it->second.cg);
        e.c.collisionType = it->second.collisionType;
        e.c.impulseLimit2 = it->second.impulseLimit2;
        e.c.hasData = found[o.name];
        e.major = 2 * o.created;
        rooms[size_t(o.room)].push_back(e);
    }
    // The level objects that collide through prop data (not the boats), created after the
    // city's objects and before the parked cars and poles, each filed at the front of its room.
    const auto& lo = city.levelObjects().objects();
    const long long B = city.levelObjectsCreated();
    for (size_t i = 0; i < lo.size(); ++i) {
        const LevelObject& l = lo[i];
        if (!l.collides() || !l.hasData || l.room <= 0 || size_t(l.room) >= rooms.size()) continue;
        Entry e;
        e.c.id = kLevelObstacleBase + int(i);
        e.c.pos = l.o;
        e.c.collisionType = l.bd.collisionType;
        e.c.impulseLimit2 = l.bd.impulseLimit2;
        e.c.hasData = true;
        e.major = 2 * B - 1;
        e.minor = int(i);
        rooms[size_t(l.room)].push_back(e);
    }
    std::vector<std::vector<ObstacleCandidate>> out(rooms.size());
    for (size_t r = 0; r < rooms.size(); ++r) {
        auto& v = rooms[r];
        // A room's list order: newest first (collision-objects.md, "Room lists").
        std::stable_sort(v.begin(), v.end(), [](const Entry& a, const Entry& b) {
            return a.major != b.major ? a.major > b.major : a.minor > b.minor;
        });
        for (const Entry& e : v) out[r].push_back(e.c);
    }
    return out;
}

void RoadNetwork::registerObstacles(const CityData& city, const Vfs& vfs) {
    const auto roomProps = obstacleCandidates(city, vfs);
    for (int ri = 0; ri < mainRoads_; ++ri) {
        Road& r = roads_[size_t(ri)];
        for (auto& o : r.obstacles) o.assign(size_t(r.n), {});
        for (int k = 1; k <= r.n - 1; ++k) {
            const vec3& Ck = r.C[size_t(k)];
            const vec3& Zk = r.Z[size_t(k)];
            const vec3& Xk = r.X[size_t(k)];
            const float seg = r.CD[size_t(k)] - r.CD[size_t(k - 1)];
            for (int room : r.rooms) {
                if (room <= 0 || size_t(room) >= roomProps.size()) continue;
                for (const ObstacleCandidate& c : roomProps[size_t(room)]) {
                    if (c.collisionType & 0x20) continue;
                    const vec3& pos = c.pos;
                    const float dx = pos.x - Ck.x, dz = pos.z - Ck.z;
                    const float a = dx * Zk.x + dz * Zk.z;
                    if (!(0.0f < a && a < seg)) continue;
                    const int side = ((-Xk.z) * dz + (-Xk.x) * dx) < 0.0f ? 0 : 1;
                    r.obstacles[side][size_t(k)].push_back(c.id);
                }
            }
        }
    }
}

// driver-planner.md §4.1: heading changes and the bend list of every road, before mirroring.
void RoadNetwork::buildBends() {
    for (Road& r : roads_) {
        const int N = r.n;
        r.theta.assign(size_t(std::max(N - 1, 0)), 0.0f);
        r.bends.clear();
        for (int i = 0; i + 1 < N; ++i) {
            const float dx = r.C[size_t(i + 1)].x - r.C[size_t(i)].x;
            const float dz = r.C[size_t(i + 1)].z - r.C[size_t(i)].z;
            const vec3& X = r.X[size_t(i)];
            const vec3& Z = r.Z[size_t(i)];
            const float y = (-X.x) * dx + (-X.z) * dz;
            const float x = (-Z.x) * dx + (-Z.z) * dz;
            r.theta[size_t(i)] = float(std::atan2(double(y), double(x)));
        }
        const auto& Ea = r.side[0].kerb;
        const auto& Eb = r.side[1].kerb;
        auto len = [&](int a, int b) { return r.CD[size_t(b)] - r.CD[size_t(a)]; };
        auto at = [](const std::vector<vec3>& v, int i) { return size_t(i) < v.size() ? v[size_t(i)] : vec3(); };
        auto twoSection = [&](int a, float th) {
            Bend b;
            b.v = a;
            b.A = th;
            b.s = th >= 0.0f ? 1 : -1;
            const vec3 d1 = -r.Z[size_t(a)];
            const vec3 d2 = r.Z[size_t(a + 2)];
            vec3 P1, P2;
            if (b.s > 0) {
                P1 = vec3(r.X[size_t(a)].x * 1.5f + at(Eb, a).x, 0, r.X[size_t(a)].z * 1.5f + at(Eb, a).z);
                P2 = vec3(r.X[size_t(a + 2)].x * 1.5f + at(Eb, a + 2).x, 0, r.X[size_t(a + 2)].z * 1.5f + at(Eb, a + 2).z);
            } else {
                P1 = vec3(at(Ea, a).x - r.X[size_t(a)].x * 1.5f, 0, at(Ea, a).z - r.X[size_t(a)].z * 1.5f);
                P2 = vec3(at(Ea, a + 2).x - r.X[size_t(a + 2)].x * 1.5f, 0, at(Ea, a + 2).z - r.X[size_t(a + 2)].z * 1.5f);
            }
            const float t = ((((P2.z * d1.x) - (P1.z * d1.x)) - (d1.z * P2.x)) + (d1.z * P1.x)) / ((d1.z * d2.x) - (d1.x * d2.z));
            b.K = vec3(d2.x * t + P2.x, (b.s > 0 ? at(Eb, a + 1) : at(Ea, a + 1)).y, d2.z * t + P2.z);
            r.bends.push_back(b);
        };
        for (int i = 1; i < N - 1; ++i) {
            const float ti = r.theta[size_t(i)];
            if (-0.7f <= ti && ti <= 0.7f) {
                if (len(i, i + 1) < 10.0f && i < N - 2) {
                    const float th = r.theta[size_t(i + 1)] + ti;
                    ++i;
                    if (th < -0.7f || th > 0.7f) twoSection(i - 1, th);
                }
            } else {
                if (len(i, i + 1) >= 10.0f || i >= N - 2) {
                    Bend b;
                    b.v = i;
                    b.A = ti;
                    b.s = ti >= 0.0f ? 1 : -1;
                    const vec3& X = r.X[size_t(i)];
                    b.K = b.s > 0 ? X + at(Eb, i) : at(Ea, i) - X;
                    r.bends.push_back(b);
                } else {
                    const float th = r.theta[size_t(i + 1)] + ti;
                    ++i;
                    if (th < -0.7f || th > 0.7f) twoSection(i - 1, th);
                }
            }
        }
    }
}

void RoadNetwork::mirrorForLeft() {
    for (Road& r : roads_) {
        const int LA = r.side[0].lanes, LB = r.side[1].lanes;
        if (LA == 0) continue;
        const std::vector<Lane> oldA = r.side[0].lines, oldB = r.side[1].lines;
        std::vector<Lane>& A = r.side[0].lines;
        std::vector<Lane>& B = r.side[1].lines;
        for (int j = 0; j < LA; ++j) {
            const int dst = LA - 1 - j;
            if (dst >= 0 && size_t(dst) < B.size() && size_t(j) < oldA.size()) B[size_t(dst)] = reversed(oldA[size_t(j)]);
        }
        if (size_t(LA) < B.size() && size_t(LA) < oldB.size()) B[size_t(LA)] = oldB[size_t(LA)];
        for (int j = 0; j < LB; ++j) {
            const int dst = LB - 1 - j;
            if (dst >= 0 && size_t(dst) < A.size() && size_t(j) < oldB.size()) A[size_t(dst)] = reversed(oldB[size_t(j)]);
        }
        if (size_t(LB) < A.size() && size_t(LB) < oldA.size()) A[size_t(LB)] = oldA[size_t(LB)];
        for (auto& side : r.side)
            for (Lane& l : side.lines) recomputeDistances(l);
    }
}

bool RoadNetwork::setRace(const Vfs& vfs, const std::string& raceName, bool professional) {
    raceAimap_ = RaceAimap();
    raceName_ = raceName;
    bool found = false;
    if (city_) {
        const std::string path = "race/" + city_->raceDir() + "/" + raceName + (professional ? ".aimap_p" : ".aimap");
        if (BytesPtr rb = vfs.open(path)) {
            raceAimap_ = parseRaceAimap(textOf(rb));
            found = true;
        }
    }
    applySpeedLimits();
    return found;
}

void RoadNetwork::applySpeedLimits() {
    for (Road& r : roads_) {
        bool found = false;
        for (const RoadException& e : raceAimap_.exceptions)
            if (e.road == r.index) {
                r.speedLimit = e.speedLimit;
                found = true;
                break;
            }
        if (found) continue;
        r.speedLimit = cityAimap_.speedLimit;
        if (r.flags & 0x04) r.speedLimit = r.speedLimit + 12.5f;
    }
}

void RoadNetwork::buildGraph() {
    graph_.assign(ix_.size(), {});
    for (size_t I = 0; I < ix_.size(); ++I)
        for (int q : ix_[I].roads) {
            const Road& r = roads_[size_t(q)];
            const int other = r.end[0].intersection == int(I) ? r.end[1].intersection : r.end[0].intersection;
            graph_[I].push_back({other, q, r.CD[size_t(r.n - 1)] - r.CD[0]});
        }
}

// ---- Terms ----

int RoadNetwork::exitIntersection(int road, int s) const { return roads_[size_t(road)].end[s > 0 ? 0 : 1].intersection; }
int RoadNetwork::entryIntersection(int road, int s) const { return roads_[size_t(road)].end[s > 0 ? 1 : 0].intersection; }
int RoadNetwork::outgoingSide(int q, int I) const { return roads_[size_t(q)].end[1].intersection == I ? 1 : -1; }

vec3 RoadNetwork::startDir(int road, int s) const {
    const Road& r = roads_[size_t(road)];
    return s > 0 ? neg(r.Z[0]) : r.Z[size_t(r.n - 1)];
}

vec3 RoadNetwork::endDir(int road, int s) const {
    const Road& r = roads_[size_t(road)];
    return s > 0 ? neg(r.Z[size_t(r.n - 1)]) : r.Z[0];
}

vec3 RoadNetwork::travelTangent(int road, int s, int i) const {
    const Road& r = roads_[size_t(road)];
    return s > 0 ? r.T[size_t(i)] : neg(r.T[size_t(r.n - 1 - i)]);
}

vec3 RoadNetwork::axisX(int road, int s, int i) const {
    const Road& r = roads_[size_t(road)];
    return r.X[size_t(sectionOf(r.n, s, i))];
}

// ---- Lane paths ----

vec3 RoadNetwork::pointBeforeEnd(int road, int s, int lane, float t) const {
    const Road& r = roads_[size_t(road)];
    const RoadSide& sd = r.sideOf(s);
    const Lane& l = sd.lines[size_t(lane)];
    const Lane& l0 = sd.lines[0];
    const int n = r.n;
    const float L0 = l0.D[size_t(n - 1)] - l0.D[size_t(n - 2)];
    const float w = (L0 - t) / L0;
    const vec3& a = l.P[size_t(n - 2)];
    const vec3& b = l.P[size_t(n - 1)];
    return vec3((b.x - a.x) * w + a.x, (b.y - a.y) * w + a.y, (b.z - a.z) * w + a.z);
}

Piece RoadNetwork::lanePiece(int road, int s, int lane, int k, float o, float f, PieceSetup setup) const {
    const Road& r = roads_[size_t(road)];
    const Lane& l = r.sideOf(s).lines[size_t(lane)];
    const int n = r.n;
    auto seglen = [&](int j) { return l.D[size_t(j)] - l.D[size_t(j - 1)]; };
    Piece p;
    const bool last = k == n - 1;
    const bool first = k == 1;
    if (last && setup != PieceSetup::Advance) {
        // Started, rejoined or arrived: ends at the own lane's stop point with the exit section's axis.
        const float sl = seglen(n - 1);
        p.L = sl - f;
        const float w = 1.0f - (f / sl);
        const vec3& a = l.P[size_t(n - 2)];
        const vec3& b = l.P[size_t(n - 1)];
        const vec3 q((b.x - a.x) * w + a.x, (b.y - a.y) * w + a.y, (b.z - a.z) * w + a.z);
        p.P0 = offsetBy(a, o, axisX(road, s, n - 2));
        p.P1 = offsetBy(q, o, axisX(road, s, n - 1));
        const vec3 t0 = first ? (setup == PieceSetup::Arrive ? startDir(road, s) : travelTangent(road, s, 0))
                              : travelTangent(road, s, n - 2);
        p.T0 = scaled(p.L, t0);
        p.T1 = scaled(p.L, endDir(road, s));
        return p;
    }
    if (last) {
        p.L = seglen(n - 1) - f;
        const vec3 X2 = axisX(road, s, n - 2);
        p.P0 = offsetBy(l.P[size_t(n - 2)], o, X2);
        p.P1 = offsetBy(pointBeforeEnd(road, s, lane, f), o, X2);
        p.T0 = scaled(p.L, travelTangent(road, s, n - 2));
        p.T1 = scaled(p.L, endDir(road, s));
        return p;
    }
    p.L = seglen(k);
    p.P0 = offsetBy(l.P[size_t(k - 1)], o, axisX(road, s, k - 1));
    p.P1 = offsetBy(l.P[size_t(k)], o, axisX(road, s, k));
    if (first) p.T0 = scaled(p.L, setup == PieceSetup::Rejoin ? travelTangent(road, s, 0) : startDir(road, s));
    else p.T0 = scaled(p.L, travelTangent(road, s, k - 1));
    p.T1 = scaled(p.L, travelTangent(road, s, k));
    return p;
}

float RoadNetwork::turnStart(int road, int s, int lane, float f) const {
    const Road& r = roads_[size_t(road)];
    const Lane& l = r.sideOf(s).lines[size_t(lane)];
    const float sl = l.D[size_t(r.n - 1)] - l.D[size_t(r.n - 2)];
    return (sl - f) <= 0.0f ? sl : f;
}

Piece RoadNetwork::turnPiece(int road, int s, int lane, float o, float t, const NextChoice& next) const {
    const Road& r = roads_[size_t(road)];
    Piece p;
    p.P0 = offsetBy(pointBeforeEnd(road, s, lane, t), o, axisX(road, s, r.n - 1));
    const Road& rn = roads_[size_t(next.road)];
    const Lane& nl = rn.sideOf(next.side).lines[size_t(next.lane)];
    p.P1 = offsetBy(nl.P[0], o, axisX(next.road, next.side, 0));
    p.L = std::fabs(p.P0.x - p.P1.x) + std::fabs(p.P0.z - p.P1.z);
    p.T0 = scaled(p.L, endDir(road, s));
    p.T1 = scaled(p.L, startDir(next.road, next.side));
    return p;
}

PieceCurve RoadNetwork::curveOf(const Piece& p) {
    PieceCurve c;
    auto coeffs = [](float P0, float P1, float T0, float T1, float& a, float& b, float& cc, float& d) {
        a = ((P1 * -2.0f + T0) + T1) + P0 * 2.0f;
        b = ((T1 * -1.0f + T0 * -2.0f) + P1 * 3.0f) + P0 * -3.0f;
        cc = T0;
        d = P0;
    };
    coeffs(p.P0.x, p.P1.x, p.T0.x, p.T1.x, c.ax, c.bx, c.cx, c.dx);
    coeffs(p.P0.z, p.P1.z, p.T0.z, p.T1.z, c.az, c.bz, c.cz, c.dz);
    c.R = 1.0f / p.L;
    return c;
}

vec3 RoadNetwork::curvePosition(const PieceCurve& c, float e) { return curvePositionAt(c, e * c.R); }

vec3 RoadNetwork::curveDerivative(const PieceCurve& c, float e) { return curveDerivativeAt(c, e * c.R); }

vec3 RoadNetwork::curvePositionAt(const PieceCurve& c, float u) {
    return vec3(((u * c.ax + c.bx) * u + c.cx) * u + c.dx, 0.0f, ((u * c.az + c.bz) * u + c.cz) * u + c.dz);
}

vec3 RoadNetwork::curveDerivativeAt(const PieceCurve& c, float u) {
    return vec3(((u * c.ax) * 3.0f + (c.bx + c.bx)) * u + c.cx, 0.0f, ((u * c.az) * 3.0f + (c.bz + c.bz)) * u + c.cz);
}

vec3 RoadNetwork::directPosition(const Piece& p, float dist, vec3* derivative) {
    float u = dist / p.L;
    if (u < 0.0f) u = 0.0f;
    auto one = [&](float P0, float P1, float T0, float T1, float& pos, float& der) {
        const float a = ((P1 * -2.0f + T0) + T1) + P0 * 2.0f;
        const float b = ((P1 * 3.0f + T0 * -2.0f) + T1 * -1.0f) + P0 * -3.0f;
        pos = ((u * a + b) * u + T0) * u + P0;
        der = ((u * a) * 3.0f + (b + b)) * u + T0;
    };
    vec3 pos, der;
    one(p.P0.x, p.P1.x, p.T0.x, p.T1.x, pos.x, der.x);
    one(p.P0.z, p.P1.z, p.T0.z, p.T1.z, pos.z, der.z);
    if (derivative) *derivative = der;
    return pos;
}

void RoadNetwork::pieceAt(int road, int s, int lane, float dist, int& k, float& into) const {
    const Road& r = roads_[size_t(road)];
    const Lane& l = r.sideOf(s).lines[size_t(lane)];
    const int n = r.n;
    dist = std::clamp(dist, 0.0f, l.D[size_t(n - 1)]);
    k = n - 1;
    for (int j = 1; j <= n - 1; ++j)
        if (dist <= l.D[size_t(j)] + 0.00001f) {
            k = j;
            break;
        }
    into = dist - l.D[size_t(k - 1)];
}

float RoadNetwork::laneLength(int road, int s, int lane) const {
    const Road& r = roads_[size_t(road)];
    return r.sideOf(s).lines[size_t(lane)].D[size_t(r.n - 1)];
}

vec3 RoadNetwork::pointAtRouteDistance(int road, int s, int lane, float o, float f, float dist, const NextChoice& next) const {
    const Road& r = roads_[size_t(road)];
    const Lane& l = r.sideOf(s).lines[size_t(lane)];
    const int n = r.n;
    const float stop = l.D[size_t(n - 1)] - f;
    if (dist < stop) {
        int k;
        float into;
        pieceAt(road, s, lane, dist, k, into);
        Piece p = lanePiece(road, s, lane, k, o, f, PieceSetup::Advance);
        return directPosition(p, dist - l.D[size_t(k - 1)]);
    }
    if (next.road < 0) return directPosition(lanePiece(road, s, lane, n - 1, o, f), dist - l.D[size_t(n - 2)]);
    const float s1 = dist - stop;
    Piece t = turnPiece(road, s, lane, o, f, next);
    if (s1 < t.L) {
        t.T1 = scaled(t.L, startDir(next.road, s));  // MM2: the current side instead of the next one
        return directPosition(t, s1);
    }
    const float s2 = s1 - t.L;
    const Road& rn = roads_[size_t(next.road)];
    const Lane& nl = rn.sideOf(next.side).lines[size_t(next.lane)];
    int k;
    float into;
    pieceAt(next.road, next.side, next.lane, s2, k, into);
    Piece p;
    if (k == 1) {
        p = lanePiece(next.road, next.side, next.lane, 1, o, 0.0f, PieceSetup::Advance);
    } else {
        // A middle piece, also for the last segment (untrimmed).
        p.L = nl.D[size_t(k)] - nl.D[size_t(k - 1)];
        p.P0 = offsetBy(nl.P[size_t(k - 1)], o, axisX(next.road, next.side, k - 1));
        p.P1 = offsetBy(nl.P[size_t(k)], o, axisX(next.road, next.side, k));
        p.T0 = scaled(p.L, travelTangent(next.road, next.side, k - 1));
        p.T1 = scaled(p.L, travelTangent(next.road, next.side, k));
    }
    return directPosition(p, s2 - nl.D[size_t(k - 1)]);
}

vec3 RoadNetwork::pointAtRouteDistanceRejoin(int road, int s, int lane, float o, float f, float dist, const NextChoice& nextIn,
                                             vec3& derivative) const {
    const Road& r = roads_[size_t(road)];
    const Lane& l = r.sideOf(s).lines[size_t(lane)];
    const int n = r.n;
    const float stop = l.D[size_t(n - 1)] - f;
    if (dist < stop) {
        int k;
        float into;
        pieceAt(road, s, lane, dist, k, into);
        Piece p = lanePiece(road, s, lane, k, o, f, PieceSetup::Advance);
        if (k == 1) {
            // MM2: the curve from P[1] + 2o·X(1) to P[1].
            const vec3 X1 = axisX(road, s, 1);
            p.P0 = offsetBy(l.P[1], 2.0f * o, X1);
            p.P1 = l.P[1];
        }
        return directPosition(p, dist - l.D[size_t(k - 1)], &derivative);
    }
    NextChoice next = nextIn;
    if (next.road < 0) return directPosition(lanePiece(road, s, lane, n - 1, o, f), dist - l.D[size_t(n - 2)], &derivative);
    const int nlanes = roads_[size_t(next.road)].sideOf(next.side).lanes;
    if (next.lane > nlanes - 1) next.lane = std::max(0, nlanes - 1);
    const float s1 = dist - stop;
    Piece t = turnPiece(road, s, lane, o, f, next);
    if (s1 < t.L) return directPosition(t, s1, &derivative);
    const float s2 = s1 - t.L;
    const Road& rn = roads_[size_t(next.road)];
    const Lane& nl = rn.sideOf(next.side).lines[size_t(next.lane)];
    int k;
    float into;
    pieceAt(next.road, next.side, next.lane, s2, k, into);
    if (k == rn.n - 1 && rn.n >= 3) {
        const int j = rn.n - 3;
        const float sl = nl.D[size_t(rn.n - 1)] - nl.D[size_t(rn.n - 2)];
        derivative = scaled(sl, travelTangent(next.road, next.side, j));
        const vec3 q = offsetBy(nl.P[size_t(j)], o, axisX(next.road, next.side, j));
        return vec3(q.x, 0.0f, q.z);
    }
    Piece p;
    if (k == 1) {
        p = lanePiece(next.road, next.side, next.lane, 1, o, 0.0f, PieceSetup::Advance);
    } else {
        p.L = nl.D[size_t(k)] - nl.D[size_t(k - 1)];
        p.P0 = offsetBy(nl.P[size_t(k - 1)], o, axisX(next.road, next.side, k - 1));
        p.P1 = offsetBy(nl.P[size_t(k)], o, axisX(next.road, next.side, k));
        p.T0 = scaled(p.L, travelTangent(next.road, next.side, k - 1));
        p.T1 = scaled(p.L, travelTangent(next.road, next.side, k));
    }
    return directPosition(p, s2 - nl.D[size_t(k - 1)], &derivative);
}

// ---- Choosing the next road ----

namespace {

// The rules of road-network.md, "Choosing the next road". With `options` non-null every possible
// result is collected instead of drawing random numbers.
NextChoice decide(const RoadNetwork& net, int road, int s, int lane, std::vector<NextChoice>* options, Lcg* rng);

}  // namespace

NextChoice RoadNetwork::chooseNext(int road, int s, int lane, Lcg& rng) const { return decide(*this, road, s, lane, nullptr, &rng); }

std::vector<NextChoice> RoadNetwork::nextOptions(int road, int s, int lane) const {
    std::vector<NextChoice> out;
    decide(*this, road, s, lane, &out, nullptr);
    return out;
}

int RoadNetwork::turnClass(int road, int s, int q, int os) const {
    const Road& rq = roads_[size_t(q)];
    vec3 d;
    if (os > 0) d = rq.C[1] - rq.C[0];
    else d = rq.C[size_t(rq.n - 2)] - rq.C[size_t(rq.n - 1)];
    const Road& r = roads_[size_t(road)];
    vec3 right, ahead;
    if (s > 0) {
        right = vec3(-r.X[size_t(r.n - 1)].x, -r.X[size_t(r.n - 1)].y, -r.X[size_t(r.n - 1)].z);
        ahead = vec3(-r.Z[size_t(r.n - 1)].x, -r.Z[size_t(r.n - 1)].y, -r.Z[size_t(r.n - 1)].z);
    } else {
        right = r.X[0];
        ahead = r.Z[0];
    }
    const float a = float(std::atan2(double(dot(d, right)), double(dot(d, ahead))));
    return a > 0.5f ? 1 : a < -0.5f ? -1 : 0;
}

namespace {

struct Decider {
    const RoadNetwork& net;
    int road, s, lane, L, I, m, i;
    std::vector<NextChoice>* options;
    Lcg* rng;

    const Road& R(int q) const { return net.roads()[size_t(q)]; }
    const Intersection& X() const { return net.intersections()[size_t(I)]; }
    int slotRoad(int k) const { return X().roads[size_t(((i + k) % m + m) % m)]; }
    bool openOut(int q) const { return R(q).sideOf(net.outgoingSide(q, I)).open(); }
    int lanesOut(int q) const { return R(q).sideOf(net.outgoingSide(q, I)).lanes; }
    NextChoice make(int q, int ln, int rule) const {
        NextChoice c;
        c.road = q;
        c.side = net.outgoingSide(q, I);
        c.lane = ln;
        c.rule = rule;
        return c;
    }
    float rnd() const { return rng ? rng->unit() : 0.0f; }
    int draw(int count) const {
        const float r = rnd();
        return int(std::trunc(r * float(count)));
    }
    // Scan right (dir +1) or left (−1) from slot i, returning the scan step of the first open road.
    int scan(int dir, int startStep, int* stepOut, bool freewayOnly = false) const {
        for (int k = startStep; k <= m - 1; ++k) {
            const int q = slotRoad(dir * k);
            if (!openOut(q)) continue;
            if (freewayOnly && !(R(q).flags & 0x04)) continue;
            if (stepOut) *stepOut = k;
            return q;
        }
        return -1;
    }

    static NextChoice failed(int rule) {
        NextChoice n;
        n.rule = rule;
        return n;
    }

    NextChoice rule2() const {
        const int q = scan(+1, 1, nullptr);
        if (q < 0) return failed(2);
        NextChoice c = make(q, lanesOut(q) - 1, 2);
        if (options) options->push_back(c);
        return c;
    }

    // Rules 5 (dir −1, lane 0) and 6 (dir +1, last lane).
    NextChoice laneRule(int dir) const {
        int k1 = 0;
        const int c1 = scan(dir, 1, &k1);
        if (c1 < 0) return failed(dir < 0 ? 5 : 6);
        int cand[2] = {c1, -1};
        int count = 1;
        int k2 = 0;
        const int c2 = scan(dir, k1 + 1, &k2);
        if (c2 >= 0) {
            const float lat = net_lateral(c2);
            if (dir < 0 ? lat > -2.0f : lat < 2.0f) {
                cand[1] = c2;
                count = 2;
            }
        }
        const int rule = dir < 0 ? 5 : 6;
        auto laneFor = [&](int q) { return dir < 0 ? 0 : lanesOut(q) - 1; };
        if (options) {
            for (int j = 0; j < count; ++j) options->push_back(make(cand[j], laneFor(cand[j]), rule));
            return make(cand[0], laneFor(cand[0]), rule);
        }
        const int pick = draw(count);
        const int q = cand[std::clamp(pick, 0, count - 1)];
        return make(q, laneFor(q), rule);
    }

    float net_lateral(int q) const;

    NextChoice rule4() const {
        std::vector<int> got;
        for (int k = 1; k <= m - 1; ++k) {
            const int q = slotRoad(k);
            if (openOut(q)) got.push_back(q);
        }
        for (int q : got)
            if (R(q).flags & 0x04) {
                NextChoice c = make(q, lanesOut(q) - 1, 4);
                if (options) options->push_back(c);
                return c;
            }
        if (got.empty()) return failed(4);
        auto withClass = [&](int q) {
            const bool rightTurn = net.turnClass(road, s, q, net.outgoingSide(q, I)) > 0;
            return make(q, rightTurn ? lanesOut(q) - 1 : 0, 4);
        };
        if (options) {
            for (int q : got) options->push_back(withClass(q));
            return withClass(got[0]);
        }
        const int pick = draw(int(got.size()));
        return withClass(got[size_t(std::clamp(pick, 0, int(got.size()) - 1))]);
    }

    NextChoice rule7() const {
        int k = 0;
        for (int q : X().roads) {
            if (q == road) continue;
            const int os = net.outgoingSide(q, I);
            if (os < 0 && R(q).side[0].lanes == 0) continue;
            if (R(q).sideOf(os).open()) ++k;
        }
        if (k < 3) {
            std::vector<int> got;
            for (int j = 1; j <= m - 1; ++j) {
                const int q = slotRoad(j);
                if (openOut(q) && lanesOut(q) > 2) got.push_back(q);
            }
            if (got.empty()) return rule2();
            if (got.size() == 1) {
                NextChoice c = make(got[0], lane, 7);
                if (options) options->push_back(c);
                return c;
            }
            const Road& r = R(road);
            const vec3& e = r.sideOf(s).lines[size_t(lane)].P[size_t(r.n - 1)];
            const vec3 A = s > 0 ? r.X[size_t(r.n - 1)] : vec3(-r.X[0].x, -r.X[0].y, -r.X[0].z);
            int best = got[0];
            float bestV = 1e30f;
            for (int q : got) {
                const Road& rq = R(q);
                const RoadSide& sd = rq.sideOf(net.outgoingSide(q, I));
                const vec3& p = sd.lines[size_t(sd.lanes - 1)].P[size_t(rq.n - 1)];
                const float v = std::fabs((p.x - e.x) * A.x + A.z * (p.z - e.z));
                if (v < bestV) {
                    bestV = v;
                    best = q;
                }
            }
            NextChoice c = make(best, lane, 7);
            if (options) options->push_back(c);
            return c;
        }
        int k1 = 0;
        if (scan(+1, 1, &k1) < 0) return NextChoice();
        const int q = scan(+1, k1 + 1, nullptr);
        if (q < 0) return NextChoice();
        NextChoice c = make(q, std::min(lane, lanesOut(q) - 1), 7);
        if (options) options->push_back(c);
        return c;
    }

    NextChoice freewayOnward() const {
        const int q = scan(+1, 1, nullptr, true);
        if (q >= 0) {
            NextChoice c = make(q, lane, 3);
            if (options) options->push_back(c);
            return c;
        }
        if (lane == 0) return laneRule(-1);
        if (lane == L - 1) return laneRule(+1);
        return rule7();
    }

    NextChoice run() const {
        const RoadEnd& end = net.arrivalEnd(road, s);
        if (end.turnRule == 3) {
            const int q = slotRoad(2);
            NextChoice c = make(q, std::min(lane, lanesOut(q) - 1), 1);
            if (options) options->push_back(c);
            return c;
        }
        if (end.turnRule == 1) return rule2();
        if (R(road).flags & 0x04) {
            if (lane == L - 1) {
                if (options) {
                    // Only the lane rule's result counts; its options are what matters.
                    return lane == 0 ? laneRule(-1) : laneRule(+1);
                }
                const float r = rnd();
                if (int(std::trunc(2.0f * r)) == 1) freewayOnward();
                else rule2();
                return lane == 0 ? laneRule(-1) : laneRule(+1);
            }
            return freewayOnward();
        }
        if (L == 1) return rule4();
        if (lane == 0) return laneRule(-1);
        if (lane == L - 1) return laneRule(+1);
        return rule7();
    }
};

float Decider::net_lateral(int q) const {
    // v = that road's lane l'' point 0 minus our lane's end point (x and z).
    const Road& r = R(road);
    const Lane& l = r.sideOf(s).lines[size_t(lane)];
    const Road& rq = R(q);
    const int os = net.outgoingSide(q, I);
    const RoadSide& sd = rq.sideOf(os);
    int l2 = lane;
    if (sd.lanes < lane) l2 = sd.lanes - 1;
    if (l2 < 0 || size_t(l2) >= sd.lines.size()) l2 = 0;
    const vec3& a = sd.lines[size_t(l2)].P[0];
    const vec3& e = l.P[size_t(r.n - 1)];
    const float vx = a.x - e.x, vz = a.z - e.z;
    if (s > 0) {
        const vec3& X = r.X[size_t(r.n - 1)];
        return vx * X.x + vz * X.z;
    }
    const vec3& X = r.X[0];
    return (-X.x) * vx + (-X.z) * vz;
}

NextChoice decide(const RoadNetwork& net, int road, int s, int lane, std::vector<NextChoice>* options, Lcg* rng) {
    const Road& r = net.roads()[size_t(road)];
    Decider d{net, road, s, lane, r.sideOf(s).lanes, net.exitIntersection(road, s), 0, 0, options, rng};
    const Intersection& x = net.intersections()[size_t(d.I)];
    d.m = int(x.roads.size());
    d.i = net.arrivalEnd(road, s).slot;
    if (d.m == 0) return NextChoice();
    return d.run();
}

}  // namespace

// ---- Room lists ----

const std::vector<Component>& RoadNetwork::roomComponents(int room) const {
    if (room <= 0 || size_t(room) >= rooms_.size()) return kNoComponents;
    return rooms_[size_t(room)];
}

Component RoadNetwork::componentOfRoom(int room) const {
    for (const Component& c : roomComponents(room))
        if (c.kind == 3 || c.kind == 1) return c;
    return {room, 0};
}

Component RoadNetwork::componentAt(const vec3& p, int hint) const {
    const int room = city_->findRoom(p, hint);
    const auto& l = roomComponents(room);
    for (const Component& c : l)
        if (c.kind == 1 || c.kind == 3) return c;
    for (const Component& c : l)
        if (c.kind == 2) {
            const int k = onRoad(c.index, p, 0.0f);
            if (k == 1 || k == 2) return c;
        }
    return {room, 0};
}

Component RoadNetwork::componentAtPreferring(const vec3& p, int hint, int q, bool roadsOnly, int* roomOut) const {
    const int room = city_->findRoom(p, hint);
    if (roomOut) *roomOut = room;
    const auto& l = roomComponents(room);
    if (q >= 0) {
        for (const Component& c : l)
            if (c.index == q && c.kind == 1) return c;
        if (!roadsOnly)
            for (const Component& c : l)
                if (c.index == q && c.kind == 2) return c;
    }
    for (const Component& c : l)
        if (c.kind == 3) return c;
    for (const Component& c : l) {
        if (c.kind != 1 && (roadsOnly || c.kind != 2)) continue;
        const int k = onRoad(c.index, p, 0.0f);
        if (k == 1 || k == 2) return c;
    }
    return {room, 0};
}

std::vector<Component> RoadNetwork::allComponentsAt(const vec3& p, int hint, int q) const {
    const int room = city_->findRoom(p, hint);
    const auto& l = roomComponents(room);
    if (q >= 0)
        for (const Component& c : l)
            if (c.index == q && (c.kind == 1 || c.kind == 2)) return {c};
    if (l.empty()) return {{room, 0}};
    if (l.size() == 1 && l[0].kind != 2) return {l[0]};
    for (const Component& c : l)
        if (c.kind == 3) return {c};
    std::vector<Component> out;
    for (const Component& c : l) {
        if (c.kind != 1 && c.kind != 2) continue;
        const Road& r = roads_[size_t(c.index)];
        bool hit = false;
        for (int i = 1; i <= r.n - 1 && !hit; ++i) {
            const float dx = p.x - r.C[size_t(i)].x, dz = p.z - r.C[size_t(i)].z;
            const float a = dx * r.Z[size_t(i)].x + dz * r.Z[size_t(i)].z;
            const float lat = dx * r.X[size_t(i)].x + dz * r.X[size_t(i)].z;
            if (a > -0.1f && std::fabs(lat) < 2.0f * r.halfWidth) hit = true;
        }
        if (hit) out.push_back(c);
        if (out.size() >= 5) break;
    }
    if (out.empty()) out.push_back({room, 0});
    return out;
}

// ---- Road queries ----

int RoadNetwork::onRoad(int road, const vec3& p, float margin, float* lateral) const {
    const Road& r = roads_[size_t(road)];
    const RoadSide& B = r.side[1];
    const int LB = B.lanes;
    const float e1 = (LB >= 1 ? B.band[2 * LB - 1] : 0.0f) - margin;
    const int i2 = 2 * LB + 1;
    const float e2 = (i2 < 10 ? B.band[i2] : 0.0f) - margin;
    // road-network.md, "Is a point on the road", in its exact order.
    const vec3& Y0 = r.Y[0];
    int i = 0;
    for (; i < r.n; ++i) {
        const vec3 e = B.outerEdge.size() > size_t(i) ? B.outerEdge[size_t(i)] - r.C[size_t(i)] : vec3();
        const float L = (e.z * e.z + e.y * e.y) + e.x * e.x;
        const float s = L == 0.0f ? 0.0f : 1.0f / std::sqrt(L);
        const vec3 v(e.x * s, e.y * s, e.z * s);
        const float a = v.y * Y0.z - v.z * Y0.y, b = v.x * Y0.y - v.y * Y0.x;
        const vec3 d = p - r.C[size_t(i)];
        if ((d.z * b) + (d.x * a) > 0.0f) break;
    }
    if (i >= r.n) i = r.n - 1;
    const vec3 d = p - r.C[size_t(i)];
    const float lam = (d.z * r.X[size_t(i)].z + d.x * r.X[size_t(i)].x) * -1.0f;
    if (lateral) *lateral = lam;
    if (-e1 < lam && lam < e1) return 1;
    if (lam >= e2 || lam <= -e2) return 3;
    return 2;
}

int RoadNetwork::sectionOfPoint(int road, const vec3& p, int s) const {
    const Road& r = roads_[size_t(road)];
    const int n = r.n;
    float best = 9999.0f;
    int result = n;
    float prev = 0.0f;
    for (int i = 0; i < n; ++i) {
        const float dx = p.x - r.C[size_t(i)].x, dz = p.z - r.C[size_t(i)].z;
        const float a = dz * r.Z[size_t(i)].z + dx * r.Z[size_t(i)].x;
        bool stop = false;
        if (a > -0.1f && (((r.CD[size_t(i)] - r.CD[0]) - prev + 2.0f > a) || i == 0)) {
            const float lam = std::fabs(dz * r.X[size_t(i)].z + dx * r.X[size_t(i)].x);
            if (lam < best) {
                best = lam;
                result = i;
                if (lam < r.halfWidth + 4.0f) stop = true;
            }
        }
        if (stop) break;
        if (i > 0) prev = r.CD[size_t(i - 1)] - r.CD[0];
    }
    if (best > r.halfWidth + 4.0f) {
        const vec3& Cn = r.C[size_t(n - 1)];
        const float dx = p.x - Cn.x, dz = p.z - Cn.z;
        if (dz * r.Z[size_t(n - 1)].z + dx * r.Z[size_t(n - 1)].x < 0.0f &&
            std::fabs(dz * r.X[size_t(n - 1)].z + dx * r.X[size_t(n - 1)].x) < best)
            result = n;
    }
    return s > 0 ? result : n - result;
}

vec3 RoadNetwork::centrePointAt(int road, float dist) const {
    const Road& r = roads_[size_t(road)];
    dist = std::clamp(dist, 0.0f, r.CD[size_t(r.n - 1)] - r.CD[0]);
    int k = r.n - 1;
    for (int j = 1; j < r.n; ++j)
        if (dist <= r.CD[size_t(j)] - r.CD[0]) {
            k = j;
            break;
        }
    const float w = (dist - (r.CD[size_t(k - 1)] - r.CD[0])) / (r.CD[size_t(k)] - r.CD[size_t(k - 1)]);
    const vec3& a = r.C[size_t(k - 1)];
    const vec3& b = r.C[size_t(k)];
    return vec3(a.x + (b.x - a.x) * w, a.y + (b.y - a.y) * w, a.z + (b.z - a.z) * w);
}

float RoadNetwork::distanceAlongCentre(int road, const vec3& p) const {
    const Road& r = roads_[size_t(road)];
    for (int k = 1; k <= r.n - 1; ++k) {
        const vec3 d = p - r.C[size_t(k)];
        const vec3& Z = r.Z[size_t(k)];
        const float a = (d.z * Z.z + d.y * Z.y) + d.x * Z.x;
        if (a >= 0.0f) return (r.CD[size_t(k)] - r.CD[0]) - a;
    }
    return r.CD[size_t(r.n - 1)] - r.CD[0];
}

int RoadNetwork::carSection(int road, const vec3& p) const {
    const Road& r = roads_[size_t(road)];
    for (int k = 1; k <= r.n - 1; ++k) {
        const vec3 d = p - r.C[size_t(k)];
        if (dot(d, r.Z[size_t(k)]) > 0.0f) return std::fabs(dot(d, r.X[size_t(k)])) < r.halfWidth ? k : r.n - 1;
    }
    return r.n - 1;
}

bool RoadNetwork::facingRoadDirection(int road, const vec3& p, const vec3& carZ) const {
    const Road& r = roads_[size_t(road)];
    int j = -1;
    for (int k = 1; k < r.n; ++k) {
        const float dx = p.x - r.C[size_t(k)].x, dz = p.z - r.C[size_t(k)].z;
        if (dx * r.Z[size_t(k)].x + dz * r.Z[size_t(k)].z > 4.0f) {
            j = k;
            break;
        }
    }
    if (j < 0) j = std::clamp(sectionOfPoint(road, p, 1), 0, r.n - 1);
    return carZ.x * r.Z[size_t(j)].x + carZ.z * r.Z[size_t(j)].z >= 0.0f;
}

// ---- Activation tables ----

const std::vector<uint16_t>& RoadNetwork::ambientRoads(int room) const {
    return room >= 0 && size_t(room) < ambientTable_.size() ? ambientTable_[size_t(room)] : kNoRoads;
}

const std::vector<uint16_t>& RoadNetwork::pedRoads(int room) const {
    return room >= 0 && size_t(room) < pedTable_.size() ? pedTable_[size_t(room)] : kNoRoads;
}

void RoadNetwork::activationChange(int a, int b, std::vector<int>& populate, std::vector<int>& empty) const {
    populate.clear();
    empty.clear();
    const auto& la = ambientRoads(a);
    const auto& lb = ambientRoads(b);
    for (uint16_t r : lb)
        if (std::find(la.begin(), la.end(), r) == la.end()) populate.push_back(r);
    for (uint16_t r : la)
        if (std::find(lb.begin(), lb.end(), r) == lb.end()) empty.push_back(r);
}

}  // namespace ai
}  // namespace cc
