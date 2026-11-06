// Ambient traffic: reactions to players, rejoining, the knocked body, and placing the car on the
// ground (spec/ai/traffic-reactions.md, traffic-visuals.md).
#include <algorithm>
#include <cmath>

#include "ai/traffic.h"
#include "ai/traffic_control.h"
#include "city/city_data.h"
#include "core/log.h"
#include "physics/collision_world.h"

namespace cc {
namespace ai {

namespace {

float len2(const vec3& v) { return (v.x * v.x + v.y * v.y) + v.z * v.z; }

vec3 normalised(const vec3& v) {
    const float l2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    const float g = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    return vec3(v.x * g, v.y * g, v.z * g);
}

float fatan2(float y, float x) { return float(std::atan2(double(y), double(x))); }

}  // namespace

// ---------------------------------------------------------------------------------------------
// Noticing a player

bool TrafficSystem::inPath(const TrafficCar& c, const TrafficPlayer& p) const {
    const TrafficType& t = typeOf(c);
    vec3 A = c.pos;
    for (int i = 1; i <= 3; ++i) {
        const vec3 B = net_->pointAtRouteDistance(c.road, c.side, c.lane, c.o, t.front, (float(i) * 10.0f) + c.x, c.next);
        const float dx = B.x - A.x, dz = B.z - A.z;
        const float s = dz * dz + dx * dx;
        const float g = s == 0.0f ? 0.0f : 1.0f / std::sqrt(s);
        const float ux = dx * g, uz = g * dz;
        const float rx = p.bodyPos.x - A.x, rz = p.bodyPos.z - A.z;
        const float fwd = rz * uz + rx * ux;
        const float lat = ux * rz + rx * (-uz);
        if (fwd < 11.0f && fwd > t.front && lat > -t.left && lat < t.right) return true;
        A = B;
    }
    return false;
}

bool TrafficSystem::screened(const TrafficCar& c, const TrafficPlayer& p) const {
    const int ahead = carAhead(c.index);
    if (ahead < 0) return false;
    const vec3& C = cars_[size_t(ahead)].pos;
    const vec3& V = c.pos;
    const vec3& Z = c.Z;
    const float fC = (Z.z * (V.z - C.z) + Z.y * (V.y - C.y)) + Z.x * (V.x - C.x);
    const vec3& P = p.bodyPos;
    const float fP = (Z.z * (V.z - P.z) + Z.y * (V.y - P.y)) + Z.x * (V.x - P.x);
    return fC < fP;
}

bool TrafficSystem::noticePlayer(TrafficCar& c, const std::vector<TrafficPlayer>& players, bool screening) {
    if (!(c.v > 0.0f) && screening) return false;
    for (size_t i = 0; i < players.size(); ++i) {
        const TrafficPlayer& p = players[i];
        if (screening) {
            const float dx = c.pos.x - p.modelOrigin.x, dz = c.pos.z - p.modelOrigin.z;
            if (!(dx * dx + dz * dz < 625.0f)) continue;
        }
        if (!inPath(c, p)) continue;
        if (screening && screened(c, p)) continue;
        c.avoidPlayer = int(i);
        setState(c, TrafficAvoiding);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Avoiding a player (state 4)

void TrafficSystem::avoiding(TrafficCar& c, float F, const std::vector<TrafficPlayer>& players) {
    if (c.avoidPlayer < 0 || size_t(c.avoidPlayer) >= players.size()) {
        setState(c, TrafficRejoining);
        return;
    }
    const TrafficPlayer& P = players[size_t(c.avoidPlayer)];
    const TrafficType& t = typeOf(c);
    const Road& rd = net_->roads()[size_t(c.road)];
    const float vmax = rd.speedLimit + c.E;
    if (c.tick++ == 0) {
        // Start: the honk event is the sound system's.
        if (sound_) sound_->avoidStart(c);
        if (vmax <= c.v) {
            c.a = c.A;
        } else {
            const vec3 d = c.pos - P.bodyPos;
            const float D = std::sqrt((d.z * d.z + d.y * d.y) + d.x * d.x);
            c.avoidS = (P.radius + c.G) + t.front;
            c.a = -((c.v * c.v) / ((D - c.avoidS) + (D - c.avoidS)));
        }
        c.avoidS = (P.radius + c.G) + t.front;
        c.avoidR = (P.radius + t.right) + 2.5f;
        c.avoidTheta = fatan2(c.Z.x, c.Z.z);
        const vec3 d = P.modelOrigin - c.pos;
        const float x0 = (d.z * c.X.z + d.y * c.X.y) + d.x * c.X.x;
        c.avoidCentred = -0.4f <= x0 && x0 <= 0.4f;
    }
    // 1. Steer and move.
    {
        const vec3 d = P.modelOrigin - c.pos;
        const float x = (d.z * c.X.z + d.y * c.X.y) + d.x * c.X.x;
        const float z = ((-c.Z.z) * d.z + (-c.Z.y) * d.y) + (-c.Z.x) * d.x;
        const float r = rng_.unit();
        const bool H = P.horn && !P.siren;
        const float R = c.avoidR;
        float phi;
        if ((rd.flags & 0x01) && c.lane == 0) {
            if (r < 0.5f) {
                c.v = c.v - 0.5f;
                phi = 0.4f;
            } else {
                phi = fatan2(x + R, z);
            }
        } else if (c.avoidCentred) {
            c.a = c.a - 10.0f;
            phi = x < 0.0f ? fatan2(x + R, z) : fatan2(x - R, z);
        } else if (x < -0.4f) {
            phi = (H || r < 0.7f) ? fatan2(x + R, z) : fatan2(x - R, z);
        } else if (x <= 0.4f) {
            phi = 0.4f;
        } else {
            phi = (H || r < 0.7f) ? fatan2(x - R, z) : fatan2(x + R, z);
        }
        float m = c.v / vmax;
        if (c.avoidCentred && c.v > 5.0f) m = 1.0f;
        phi = std::clamp(phi, -0.02f, 0.02f);
        c.avoidTheta = ((phi * 1.5f) * m) + c.avoidTheta;
        const float th = c.avoidTheta;
        const float ct = float(std::cos(double(th))), st = float(std::sin(double(th)));
        c.X = vec3(ct, 0.0f, -st);
        c.Y = vec3(0.0f, 1.0f, 0.0f);
        c.Z = vec3(st, 0.0f, ct);
        c.v = (F * c.a) + c.v;
        if (c.v < 0.25f) c.v = 0.0f;
        if (vmax < c.v) c.v = vmax;
        const float step = F * c.v;
        c.x = step + c.x;
        c.pos = vec3(c.pos.x - step * c.Z.x, c.pos.y - step * c.Z.y, c.pos.z - step * c.Z.z);
        freeHeadingSolve(c);
    }
    // 2. Braking.
    if (c.v < vmax) {
        const vec3 d = c.pos - P.modelOrigin;
        const float D = std::sqrt((d.z * d.z + d.y * d.y) + d.x * d.x);
        c.a = -((c.v * c.v) / ((D - c.avoidS) + (D - c.avoidS)));
    }
    // 3. Distance along the lane, re-derived from the position.
    c.room = city_->findRoom(c.pos, c.room);
    const Component comp = net_->componentOfRoom(c.room);
    if (comp.kind == 3) {
        const float lam = laneLen(c.road, c.side, c.lane);
        const int n = rd.n;
        float over;
        if (c.side > 0)
            over = (-rd.Z[size_t(n - 1)].x) * (c.pos.x - rd.C[size_t(n - 1)].x) +
                   (-rd.Z[size_t(n - 1)].z) * (c.pos.z - rd.C[size_t(n - 1)].z);
        else
            over = (c.pos.x - rd.C[0].x) * rd.Z[0].x + (c.pos.z - rd.C[0].z) * rd.Z[0].z;
        c.x = lam + over;
    } else {
        const int r = comp.kind == 1 ? comp.index : c.road;
        // Lateral offset from lane 0, then the lane whose band holds it.
        projectOnLane(c, r, c.side, -1);
    }
    // 4. Exit test.
    const vec3 Q = net_->pointAtRouteDistance(c.road, c.side, c.lane, c.o, t.front, 25.0f + c.x, c.next);
    const vec3& V = c.pos;
    const float zQ = (c.Z.z * (V.z - Q.z) + c.Z.y * (V.y - Q.y)) + c.Z.x * (V.x - Q.x);
    const float lQ = ((-c.X.z) * (V.z - Q.z) + (-c.X.y) * (V.y - Q.y)) + (-c.X.x) * (V.x - Q.x);
    const float lo = std::min(lQ, -3.0f), hi = std::max(lQ, 3.0f);
    const vec3& Pc = P.bodyPos;
    const float zP = (c.Z.z * (V.z - Pc.z) + c.Z.y * (V.y - Pc.y)) + c.Z.x * (V.x - Pc.x);
    const float lP = ((-c.X.z) * (V.z - Pc.z) + (-c.X.y) * (V.y - Pc.y)) + (-c.X.x) * (V.x - Pc.x);
    const bool corridor = zP < zQ && zP > t.front && lo < lP && lP < hi;
    const vec3 dv = V - Pc;
    const bool ahead = dot(c.Z, dv) > t.front && len2(dv) < 625.0f;
    if (!corridor && !ahead) setState(c, TrafficRejoining);
}

// Project V on road r, side s, lane j (j < 0: find the lane from lane 0's lateral offset).
void TrafficSystem::projectOnLane(TrafficCar& c, int r, int s, int j) {
    const Road& rd = net_->roads()[size_t(r)];
    const RoadSide& sd = rd.sideOf(s);
    if (sd.lanes <= 0) return;
    auto project = [&](int lane, float& lat, bool& found) {
        lane = std::min(lane, sd.lanes - 1);
        const Lane& L = sd.lines[size_t(lane)];
        const vec3& V = c.pos;
        for (int i = 1; i <= rd.n - 1; ++i) {
            const vec3& P = L.P[size_t(i)];
            const float fwd = ((V.x - P.x) * rd.Z[size_t(i)].x + (V.y - P.y) * rd.Z[size_t(i)].y) + (V.z - P.z) * rd.Z[size_t(i)].z;
            if (fwd > 0.0f) {
                lat = -(((V.x - rd.C[size_t(i)].x) * rd.X[size_t(i)].x + (V.y - rd.C[size_t(i)].y) * rd.X[size_t(i)].y) +
                        (V.z - rd.C[size_t(i)].z) * rd.X[size_t(i)].z);
                if (std::fabs(lat) < rd.halfWidth + 5.0f) {
                    c.x = L.D[size_t(i)] - fwd;
                    found = true;
                    return;
                }
            }
        }
    };
    float lat = 0;
    bool found = false;
    if (j < 0) {
        project(0, lat, found);
        const Road& cur = net_->roads()[size_t(c.road)];
        const float* e = cur.sideOf(s).band;
        const int lines = sd.lanes + sd.sidewalks;
        j = sd.lanes - 1;
        for (int k = 0; k < lines && 2 * k + 1 < 10; ++k)
            if (e[2 * k] < lat && lat < e[2 * k + 1]) {
                j = k;
                break;
            }
        j = std::min(j, sd.lanes - 1);
        found = false;
    }
    project(j, lat, found);
}

// ---------------------------------------------------------------------------------------------
// Rejoining (state 3)

void TrafficSystem::startRejoin(TrafficCar& c) {
    const TrafficType& t = typeOf(c);
    const vec3 dv = c.pos - c.rejoinPos;
    if (len2(dv) <= 1.0f) {
        c.attempts = c.attempts + 1;
    } else {
        c.attempts = 1;
        c.rejoinPos = c.pos;
    }
    const int room = city_->findRoom(c.pos, c.room);
    c.room = room;
    const auto& comps = net_->roomComponents(room);
    Component found{room, 0};
    for (const Component& k : comps)
        if (k.kind == 1 && k.index == c.road) found = k;
    if (found.kind == 0)
        for (const Component& k : comps)
            if (k.kind == 3) {
                found = k;
                break;
            }
    if (found.kind == 0)
        for (const Component& k : comps)
            if ((k.kind == 1 || k.kind == 2) && net_->onRoad(k.index, c.pos, 0.0f) < 3) {
                found = k;
                break;
            }
    c.mergeL = 30.0f;
    const int prevNextSide = c.next.side;
    if (found.kind == 1 || found.kind == 2) {
        // On a road.
        const Road& cur = net_->roads()[size_t(c.road)];
        for (int l = 0; l < cur.sideOf(c.side).lanes; ++l) removeFrom(c.index, c.road, c.side, l);
        const Road& rd = net_->roads()[size_t(found.index)];
        int side = c.side, lane = 0;
        float base = c.base;
        bool ok = false;
        for (int i = 0; i < rd.n; ++i) {
            const vec3 d = c.pos - rd.C[size_t(i)];
            const float fwd = d.x * rd.Z[size_t(i)].x + d.z * rd.Z[size_t(i)].z;
            const float lat = d.z * rd.X[size_t(i)].z + d.x * rd.X[size_t(i)].x;
            if (fwd >= 0.0f && -rd.halfWidth < lat && lat < rd.halfWidth) {
                side = (c.Z.x * rd.Z[size_t(i)].x + c.Z.z * rd.Z[size_t(i)].z) < 0.0f ? -1 : 1;
                const RoadSide& sd = rd.sideOf(side);
                lane = 0;
                for (int j = 0; j < sd.lanes; ++j)
                    if (2 * j + 1 < 10 && sd.band[2 * j] < lat && lat < sd.band[2 * j + 1]) {
                        lane = j;
                        break;
                    }
                if (sd.lanes > 0) {
                    const Lane& L = sd.lines[size_t(lane)];
                    base = side > 0 ? L.D[size_t(i)] - fwd : L.D[size_t(rd.n - i - 1)] + fwd;
                }
                ok = true;
                break;
            }
        }
        (void)ok;
        c.road = found.index;
        c.side = side;
        c.lane = lane;
        c.base = base;
        const RoadSide& sd = rd.sideOf(side);
        if ((!(rd.flags & 0x04) || side == prevNextSide) && sd.open() && sd.lanes > 0) {
            chooseNext(c);
            c.listLane = lane;
            c.x = base + 30.0f;
            sortedInsert(c.index, c.road, c.side, lane, base + 30.0f);
        } else {
            setState(c, TrafficOut);
            return;
        }
    } else if (found.kind == 3) {
        // In an intersection: the exit best aligned with the car's heading.
        const Intersection& x = net_->intersections()[size_t(found.index)];
        const Road& cur = net_->roads()[size_t(c.road)];
        const bool fw = (cur.flags & 0x04) || (cur.sideOf(c.side).lanes == 1 && c.next.road >= 0 &&
                                                (net_->roads()[size_t(c.next.road)].flags & 0x04));
        const vec3 f = c.Z * -1.0f;
        float best = -1e30f;
        int bestRoad = -1, bestSide = 1;
        for (int q : x.roads) {
            const int os = net_->outgoingSide(q, found.index);
            const Road& rq = net_->roads()[size_t(q)];
            if (!rq.sideOf(os).open()) continue;
            if (fw && !(rq.flags & 0x04)) continue;
            const vec3 d = os > 0 ? rq.C[1] - rq.C[0] : rq.C[size_t(rq.n - 2)] - rq.C[size_t(rq.n - 1)];
            const float s = (d.y * f.y + d.z * f.z) + f.x * d.x;
            if (s > best) {
                best = s;
                bestRoad = q;
                bestSide = os;
            }
        }
        if (bestRoad >= 0) {
            c.next.road = bestRoad;
            c.next.side = bestSide;
            const RoadSide& ns = net_->roads()[size_t(bestRoad)].sideOf(bestSide);
            if (c.next.lane >= ns.lanes) c.next.lane = std::max(0, ns.lanes - 1);
        }
        if (c.next.road >= 0) {
            const Road& nr = net_->roads()[size_t(c.next.road)];
            const RoadSide& ns = nr.sideOf(c.next.side);
            const vec3 Ps = ns.lanes > 0 ? ns.lines[0].P[0] : nr.C[0];
            const Piece tp = net_->turnPiece(c.road, c.side, c.listLane, c.o, net_->turnStart(c.road, c.side, c.listLane, t.front),
                                             c.next);
            const vec3 Pe = tp.P0;
            const float cc = std::fabs(Pe.x - Ps.x) + std::fabs(Pe.z - Ps.z);
            const vec3 dirN = c.next.side > 0 ? nr.Z[0] : nr.Z[size_t(nr.n - 1)] * -1.0f;
            const float lam = laneLen(c.road, c.side, c.listLane);
            const vec3 dp = c.pos - Ps;
            c.base = ((lam - t.front) + cc) - ((dirN.x * dp.x + dirN.y * dp.y) + dirN.z * dp.z);
            const float total = (laneLen(c.next.road, c.next.side, std::min(c.next.lane, std::max(0, ns.lanes - 1))) + lam) + cc;
            c.mergeL = c.base + 30.0f <= total ? 30.0f : total - c.base;
            if (!((!(cur.flags & 0x04) || c.side == c.next.side) && ns.open())) setState(c, TrafficOut);
        } else {
            setState(c, TrafficOut);
        }
    } else {
        setState(c, TrafficOut);
    }
    // 6. Merge curve.
    c.u = 0;
    vec3 tq;
    const Road& rd = net_->roads()[size_t(c.road)];
    vec3 Q = c.pos;
    if (rd.sideOf(c.side).lanes > 0)
        Q = net_->pointAtRouteDistanceRejoin(c.road, c.side, std::min(c.lane, rd.sideOf(c.side).lanes - 1), c.o, t.front,
                                             c.base + c.mergeL, c.next, tq);
    tq = normalised(tq) * c.mergeL;
    Piece p;
    p.P0 = c.pos;
    p.T0 = c.Z * (-c.mergeL);
    p.P1 = Q;
    p.T1 = tq;
    p.L = c.mergeL;
    setPiece(c, p);
    c.rail = 3;
    c.go = false;
    c.vt = rd.speedLimit + c.E;
}

void TrafficSystem::rejoining(TrafficCar& c, float F, const std::vector<TrafficPlayer>& players) {
    if (c.tick++ == 0) {
        startRejoin(c);
        if (c.state != TrafficRejoining) return;
    }
    if (c.state == TrafficOut) return;
    // 2. In path of a player (no distance test, no screening).
    if (noticePlayer(c, players, false)) return;
    if (c.v > c.vt) c.v = c.vt;
    else if (c.v < c.vt) c.v = (F * c.A) + c.v;
    c.u = (c.v * F) + c.u;
    c.x = c.u + c.base;
    solve(c, players);
    if (c.u > c.mergeL) {
        setState(c, TrafficDriving);
        c.indicator = 0;
    }
    updateRoom(c);
}

// ---------------------------------------------------------------------------------------------
// Knocked (state 2) and the body

void TrafficSystem::knocked(TrafficCar& c) {
    if (c.body >= 0) {
        // The lane matrix of the last sample (car-collision.md 6.3 step 5).
        const phys::TrafficPhys& b = *bodies_[size_t(c.body)].phys;
        c.X = b.lane.X;
        c.Y = b.lane.Y;
        c.Z = b.lane.Z;
        c.pos = b.lane.o;
    }
    // (A wreck without a body is a mover that only looks for objects: car-collision.md 1.1 row 3.)
}

phys::TrafficPhys* TrafficSystem::takeBody(int ci, const phys::Mtx& lane, float speed) {
    TrafficCar& c = cars_[size_t(ci)];
    if (c.body >= 0) return bodies_[size_t(c.body)].phys.get();
    if (bodies_.size() >= 32) release(bodies_.front().car);
    TrafficBodySlot slot;
    slot.car = ci;
    slot.phys = std::make_unique<phys::TrafficPhys>();
    phys::TrafficPhys& p = *slot.phys;
    p.identity = 20000 + ci;
    p.wrecked = c.wreck;
    p.setup(typeOf(c).body, lane, speed);
    p.room = c.room;
    bodies_.push_back(std::move(slot));
    c.body = int(bodies_.size()) - 1;
    // The lane logic: speed 0, the knocked state, hazard lights (traffic-reactions.md).
    c.v = 0;
    c.vt = net_->roads()[size_t(c.road)].speedLimit + c.E + 0.0001f;
    c.tick = 0;
    setState(c, TrafficKnocked);
    c.indicator = 3;
    return &p;
}

void TrafficSystem::prePhysics() {
    for (size_t i = 0; i < bodies_.size();) {
        const phys::TrafficPhys& p = *bodies_[i].phys;
        TrafficCar& c = cars_[size_t(bodies_[i].car)];
        if (p.rest.state == 0 || p.rb.p.y < -100.0f || c.room == 0) {
            dropBody(bodies_[i].car);  // no release steps; the car stays frozen
            continue;
        }
        ++i;
    }
}

void TrafficSystem::appendRowMovers(std::vector<phys::SceneMover>& movers) {
    for (int ci : rowMovers_) {
        TrafficCar& c = cars_[size_t(ci)];
        if (c.body >= 0 || !c.onStreets) continue;
        phys::SceneMover m;
        m.kind = phys::SceneMover::LaneMover;
        m.ttype = &typeOf(c).body;
        m.laneId = ci;
        m.laneM = phys::mtxFromRows(c.X, c.Y, c.Z, c.pos);
        m.laneSpeed = c.v;
        m.laneRoom = c.room;
        m.U = false;
        m.V = false;
        m.C = c.state != TrafficKnocked;  // a wreck only looks for objects
        m.O = true;
        m.box = true;
        m.shape = phys::BoxShape{typeOf(c).body.size, typeOf(c).body.cg};
        m.number = 1000 + ci;
        movers.push_back(m);
    }
}

void TrafficSystem::appendBodyMovers(std::vector<phys::SceneMover>& movers) {
    for (TrafficBodySlot& b : bodies_) {
        const TrafficCar& c = cars_[size_t(b.car)];
        phys::SceneMover m;
        m.kind = phys::SceneMover::TrafficBodyMover;
        m.tp = b.phys.get();
        m.ttype = &typeOf(c).body;
        m.laneId = b.car;
        m.identity = b.phys->identity;
        m.box = true;
        m.shape = phys::BoxShape{typeOf(c).body.size, typeOf(c).body.cg};
        m.number = 1000 + b.car;
        movers.push_back(m);
    }
}

void TrafficSystem::laneCars(std::vector<phys::LaneCar>& out) const {
    for (const TrafficCar& c : cars_) {
        if (!c.onStreets || c.body >= 0 || c.type < 0 || c.room == 0) continue;
        phys::LaneCar lc;
        lc.id = c.index;
        lc.M = phys::mtxFromRows(c.X, c.Y, c.Z, c.pos);
        lc.type = &typeOf(c).body;
        lc.speed = c.v;
        lc.room = city_->findRoom(c.pos + c.Y, c.room);
        lc.number = 1000 + c.index;
        lc.filed = c.filed;
        out.push_back(lc);
    }
}

void TrafficSystem::afterPhysics() {
    for (size_t i = 0; i < bodies_.size();) {
        const int ci = bodies_[i].car;
        const phys::TrafficPhys& p = *bodies_[i].phys;
        if (p.rest.state != 0 && -100.0f <= p.rb.p.y) {
            TrafficCar& c = cars_[size_t(ci)];
            knocked(c);
            c.room = city_->findRoom(vec3(p.rb.p.x + p.rb.R.r[1].x, p.rb.p.y + p.rb.R.r[1].y, p.rb.p.z + p.rb.R.r[1].z),
                                     c.room);
            ++i;
            continue;
        }
        release(ci);  // the last slot takes this place
    }
}

void TrafficSystem::dropBody(int ci) {
    TrafficCar& c = cars_[size_t(ci)];
    if (c.body < 0) return;
    const int bi = c.body;
    c.body = -1;
    if (size_t(bi) + 1 != bodies_.size()) {
        bodies_[size_t(bi)] = std::move(bodies_.back());
        cars_[size_t(bodies_[size_t(bi)].car)].body = bi;
    }
    bodies_.pop_back();
}

void TrafficSystem::release(int ci) {
    TrafficCar& c = cars_[size_t(ci)];
    if (c.body < 0) return;
    const phys::TrafficPhys& p = *bodies_[size_t(c.body)].phys;
    // The car keeps the lane matrix of its last sample; the probe uses the body frame (6.5).
    const phys::Mtx M = p.matrix();
    c.X = p.lane.X;
    c.Y = p.lane.Y;
    c.Z = p.lane.Z;
    c.pos = p.lane.o;
    const bool upright = phys::trafficUpright(*world_, M, c.room) && !c.wreck;
    dropBody(ci);
    if (upright) {
        setState(c, c.attempts >= 3 ? TrafficOut : TrafficRejoining);
        c.v = 0;
        c.vt = net_->roads()[size_t(c.road)].speedLimit + c.E + 0.0001f;
    } else {
        c.wreck = true;  // stays knocked where it lies (the mark lasts the session)
    }
}

void TrafficSystem::endPhysics(int ci) { release(ci); }

// ---------------------------------------------------------------------------------------------
// Position and orientation (traffic-visuals.md)

void TrafficSystem::probeCorners(TrafficCar& c, const vec3 corners[3], float yPrev, int tries, float stepY, bool fromCurrent) {
    const TrafficType& t = typeOf(c);
    vec3 P[3];
    for (int k = 0; k < 3; ++k) {
        P[k] = corners[k];
        bool hitAny = false;
        for (int i = 1; i <= tries && !hitAny; ++i) {
            const float base = fromCurrent ? corners[k].y : yPrev;
            const float up = fromCurrent ? stepY : float(i) * stepY;
            const vec3 S(corners[k].x, fromCurrent ? base + up : up + base, corners[k].z);
            const vec3 E(corners[k].x, base - up, corners[k].z);
            phys::ProbeHit hit;
            if (world_->probe(S, E, c.probes[k], hit)) {
                P[k].y = hit.point.y;
                hitAny = true;
            }
        }
        if (!hitAny) P[k].y = fromCurrent ? c.pos.y : yPrev;
    }
    vec3 X = P[1] - P[0], Z = P[2] - P[0];
    const float wF = t.front / (t.back + t.front), wL = t.left / (t.right + t.left);
    c.pos = vec3((wL * X.x + P[0].x) + wF * Z.x, (wL * X.y + P[0].y) + wF * Z.y, (wL * X.z + P[0].z) + wF * Z.z);
    X = normalised(X);
    Z = normalised(Z);
    c.X = X;
    c.Z = Z;
    c.Y = vec3(Z.y * X.z - Z.z * X.y, X.x * Z.z - Z.x * X.z, Z.x * X.y - X.x * Z.y);
    c.groundSolved = true;
}

void TrafficSystem::solve(TrafficCar& c, const std::vector<TrafficPlayer>& players) {
    const TrafficType& t = typeOf(c);
    const float tau = c.u * c.invLen;
    const PieceCurve& k = c.curve;
    const float Px = ((tau * k.ax + k.bx) * tau + k.cx) * tau + k.dx;
    const float Pz = ((tau * k.az + k.bz) * tau + k.cz) * tau + k.dz;
    const float Dx = (((tau * k.ax) * 3.0f) + (k.bx + k.bx)) * tau + k.cx;
    const float Dz = (((tau * k.az) * 3.0f) + (k.bz + k.bz)) * tau + k.cz;
    const float l2 = (0.0f * 0.0f + Dz * Dz) + Dx * Dx;
    const float g = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    const vec3 d(g * Dx, 0.0f, g * Dz);
    const Road& rd = net_->roads()[size_t(c.road)];
    const bool nextFlat = c.next.road >= 0 && (net_->roads()[size_t(c.next.road)].flags & 8);
    if ((rd.flags & 8) && (c.next.road < 0 || (c.state == TrafficDriving && nextFlat))) {
        c.X = vec3(-d.z, 0.0f, d.x);
        c.Y = vec3(0.0f, 1.0f, 0.0f);
        c.Z = vec3(-d.x, -0.0f, -d.z);
        c.pos = vec3(Px, rd.C[0].y, Pz);
        c.groundSolved = false;
        return;
    }
    bool near = false;
    for (const TrafficPlayer& p : players) {
        const float dx = p.bodyPos.x - c.pos.x, dz = p.bodyPos.z - c.pos.z;
        if (dx * dx + dz * dz < 10000.0f) near = true;
    }
    if (!near && c.state != TrafficRejoining && c.rail != 2 && c.regKind == 1 && c.k < rd.n - 1) {
        const int n = rd.n;
        if (c.side > 0) {
            c.X = rd.X[size_t(c.k)] * -1.0f;
            c.Y = rd.Y[size_t(c.k)];
            c.Z = rd.Z[size_t(c.k)];
        } else {
            c.X = rd.X[size_t(n - c.k)];
            c.Y = rd.Y[size_t(n - c.k)];
            c.Z = rd.Z[size_t(n - c.k)] * -1.0f;
        }
        const Lane& L = rd.sideOf(c.side).lines[size_t(c.lane)];
        const float h = ((L.P[size_t(c.k)].y - L.P[size_t(c.k - 1)].y) * tau) + L.P[size_t(c.k - 1)].y;
        c.pos = vec3(Px, h, Pz);
        c.groundSolved = false;
        return;
    }
    const float Xx = -d.z, Xz = d.x;
    const vec3 corners[3] = {
        vec3(((Px - t.left * Xx) + t.front * d.x), 0.0f, ((Pz - t.left * Xz) + t.front * d.z)),
        vec3(((t.right * Xx + Px) + t.front * d.x), 0.0f, ((t.right * Xz + Pz) + t.front * d.z)),
        vec3(((Px - t.left * Xx) - t.back * d.x), 0.0f, ((Pz - t.left * Xz) - t.back * d.z)),
    };
    probeCorners(c, corners, c.pos.y, 3, 2.5f, false);
}

void TrafficSystem::freeHeadingSolve(TrafficCar& c) {
    const TrafficType& t = typeOf(c);
    const vec3 d = c.Z * -1.0f;
    const vec3& X = c.X;
    const vec3& P = c.pos;
    const vec3 corners[3] = {
        (P - X * t.left) + d * t.front,
        (X * t.right + P) + d * t.front,
        (P - X * t.left) - d * t.back,
    };
    probeCorners(c, corners, c.pos.y, 1, 5.0f, true);
}

}  // namespace ai
}  // namespace cc
