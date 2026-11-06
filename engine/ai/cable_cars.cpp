// San Francisco's cable cars (spec/ai/cable-cars.md): created at the network load from the
// intersections where exactly one cable road leaves, driven along the cable lines, seen by the
// other AI through their TrafficCar entries, breakable props of the collision world.
#include <algorithm>
#include <cmath>

#include "ai/driver.h"
#include "ai/traffic.h"
#include "ai/traffic_control.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/banger.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace ai {

namespace {
vec3 scaled(float L, const vec3& v) { return vec3(L * v.x, L * v.y, L * v.z); }
}  // namespace

// ---- Which cable cars exist ----

void TrafficSystem::createCableCars(const Vfs& vfs, AssetCache& assets) {
    cables_.clear();
    cableType_ = -1;
    if (!net_ || !city_) return;
    const auto& roads = net_->roads();
    const auto& ix = net_->intersections();
    std::vector<std::pair<int, int>> starts;  // road, side
    for (const Intersection& x : ix) {
        int count = 0, road = -1, side = 0;
        for (int q : x.roads) {
            const int s = net_->outgoingSide(q, x.index);
            if (roads[size_t(q)].sideOf(s).cable) {
                ++count;
                road = q;
                side = s;
            }
        }
        if (count == 1) starts.emplace_back(road, side);
    }
    if (starts.empty()) return;
    // The model and prop data: extents from the box of the bound (points − CG), a breakable prop.
    const std::string name = "va_cablecar_f";
    auto data = std::make_shared<phys::LooseData>();
    data->name = name;
    if (!loadBangerData(vfs, name, data->bd)) {
        logWarn("tune/banger/%s.dgBangerData missing: no cable cars", name.c_str());
        return;
    }
    std::shared_ptr<phys::Bound> geom;
    if (data->bd.collisionPrim == 0) {
        phys::MaterialTable scratch;
        if (auto gb = phys::loadGeometryBound(vfs, name, scratch)) {
            auto mb = std::make_shared<phys::Bound>(*gb);
            for (vec3& p : mb->points) p = vec3(p.x + (-data->bd.cg.x), p.y + (-data->bd.cg.y), p.z + (-data->bd.cg.z));
            mb->computeBox();
            geom = mb;
        }
    }
    phys::looseBoundFor(*data, geom);
    data->pkg = assets.package(name, true);
    if (data->pkg) {
        const PartTable pt = takeParts(*data->pkg, {"BODY", "SHADOW", "HLIGHT"});
        if (const PartLods* body = pt.find("BODY"))
            for (int l = 0; l < LodCount; ++l) {
                data->lod[l] = body->at(Lod(l));
                if (data->lod[l] && data->lod[l]->radius > data->radius) data->radius = data->lod[l]->radius;
            }
    }
    cableData_ = data;
    TrafficType t;
    t.name = name;
    t.dataFound = true;
    t.mass = data->bd.mass;
    t.size = data->bd.size;
    t.cg = data->bd.cg;
    const phys::Bound& b = *data->bound;
    t.front = -b.bmin.z;
    t.back = b.bmax.z;
    t.left = -b.bmin.x;
    t.right = b.bmax.x;
    t.boxMin = b.bmin;
    t.boxMax = b.bmax;
    t.bodyRadius = data->radius;
    t.setCount = 1;
    cableType_ = int(types_.size());
    types_.push_back(std::move(t));
    // Random draws from the game-wide generator (here continuing from the level objects' draws,
    // after discarding the traffic and pedestrian pools' creation draws): a lane offset per car
    // while they are constructed, then each car's acceleration.
    Lcg g(city_->gizmoRandomEnd());
    for (int i = 0; i < cableSkip_; ++i) g.unit();
    for (size_t i = 0; i < starts.size(); ++i) {
        CableCar cc;
        cc.startRoad = starts[i].first;
        cc.startSide = starts[i].second;
        TrafficCar c;
        c.index = int(cars_.size());
        c.type = cableType_;
        c.cable = true;
        c.onStreets = false;
        c.state = c.prevState = TrafficDriving;
        cc.car = c.index;
        cars_.push_back(c);
        g.unit();  // the lane offset cable cars never use
        cables_.push_back(cc);
    }
    for (CableCar& cc : cables_) {
        const float r = g.unit();
        cc.ac = (r + r) + 1.5f;
    }
    // Partners: the car of the line end each is heading to.
    for (CableCar& cc : cables_) {
        int R = cc.startRoad, s = cc.startSide;
        for (int guard = 0; guard < 99999; ++guard) {
            const int X = net_->arrivalEnd(R, s).intersection;
            int found = 0, remembered = -1;
            for (int q : ix[size_t(X)].roads) {
                const int os = net_->outgoingSide(q, X);
                if (roads[size_t(q)].sideOf(os).cable) {
                    ++found;
                    remembered = q;
                    s = os;
                }
            }
            if (found == 1) {
                for (size_t k = 0; k < cables_.size(); ++k)
                    if (cables_[k].startRoad == remembered) cc.partner = int(k);
                break;
            }
            const NextChoice nx = cableNext(R, s);
            if (nx.road >= 0) R = nx.road;
        }
    }
    // Each a breakable prop of its room.
    for (CableCar& cc : cables_) {
        TrafficCar& c = cars_[size_t(cc.car)];
        const Road& r = roads[size_t(cc.startRoad)];
        const auto& C = r.sideOf(cc.startSide).cableLine;
        c.pos = C.empty() ? vec3() : C[0];
        c.room = city_->findRoom(c.pos, 0);
        if (world_) cc.prop = world_->addDynamicProp(cableData_, cableMatrix(c), c.room);
    }
    logInfo("traffic: %zu cable cars", cables_.size());
}

// ---- Next road ----

NextChoice TrafficSystem::cableNext(int road, int s) const {
    NextChoice out;
    const auto& roads = net_->roads();
    const RoadEnd& e = net_->arrivalEnd(road, s);
    const int X = e.intersection;
    const Intersection& x = net_->intersections()[size_t(X)];
    int count = 0;
    for (int q : x.roads)
        if (roads[size_t(q)].side[1].cable) ++count;  // MM2 tests side B only
    int pick = -1;
    if (count == 1) {
        out.road = road;
        out.side = -s;
        return out;
    }
    if (count == 2 || count == 4) {
        const int n = int(x.roads.size());
        const int slot = e.slot;
        int seen = 0;
        for (int i = 1; i < n && pick < 0; ++i) {
            const int q = x.roads[size_t((slot + i) % n)];
            if (q == road || !roads[size_t(q)].side[1].cable) continue;
            ++seen;
            if (count == 2 || seen == 2) pick = q;
        }
    }
    if (pick < 0) return out;  // none: the car's next road stays as it was
    out.road = pick;
    out.side = roads[size_t(pick)].end[1].intersection == X ? 1 : -1;
    return out;
}

// ---- Pieces ----

Piece TrafficSystem::cablePiece(int road, int s, int k, float f) const {
    const Road& r = net_->roads()[size_t(road)];
    const int n = r.n;
    const auto& C = r.sideOf(s).cableLine;
    auto CL = [&](int a, int b) { return r.CD[size_t(b)] - r.CD[size_t(a)]; };
    Piece p;
    if (k <= 1) {
        p.L = s > 0 ? CL(0, 1) : CL(n - 2, n - 1);
        p.P0 = C[0];
        p.P1 = C[1];
        p.T0 = scaled(p.L, net_->startDir(road, s));
        p.T1 = scaled(p.L, net_->travelTangent(road, s, 1));
    } else if (k <= n - 2) {
        const int i = s > 0 ? k : n - k;
        p.L = CL(i - 1, i);
        p.P0 = C[size_t(k - 1)];
        p.P1 = C[size_t(k)];
        p.T0 = scaled(p.L, net_->travelTangent(road, s, k - 1));
        p.T1 = scaled(p.L, net_->travelTangent(road, s, k));
    } else {
        k = n - 1;
        p.L = (s > 0 ? CL(k - 1, k) : CL(0, 1)) - f;
        const float w = (CL(n - 2, n - 1) - f) / CL(n - 2, n - 1);
        const vec3& A = C[size_t(n - 2)];
        const vec3& B = C[size_t(n - 1)];
        p.P0 = C[size_t(k - 1)];
        p.P1 = vec3((B.x - A.x) * w + A.x, (B.y - A.y) * w + A.y, (B.z - A.z) * w + A.z);
        p.T0 = scaled(p.L, net_->travelTangent(road, s, k - 1));
        p.T1 = scaled(p.L, net_->endDir(road, s));
    }
    return p;
}

Piece TrafficSystem::cableTurnPiece(int road, int s, const NextChoice& next, float f) const {
    const Road& r = net_->roads()[size_t(road)];
    const int n = r.n;
    const auto& C = r.sideOf(s).cableLine;
    auto CL = [&](int a, int b) { return r.CD[size_t(b)] - r.CD[size_t(a)]; };
    const float w = (CL(n - 2, n - 1) - f) / CL(n - 2, n - 1);
    const vec3& A = C[size_t(n - 2)];
    const vec3& B = C[size_t(n - 1)];
    const vec3 Q((B.x - A.x) * w + A.x, (B.y - A.y) * w + A.y, (B.z - A.z) * w + A.z);
    const auto& C2 = net_->roads()[size_t(next.road)].sideOf(next.side).cableLine;
    Piece p;
    p.L = std::fabs(Q.x - C2[0].x) + std::fabs(Q.z - C2[0].z);
    p.P0 = Q;
    p.P1 = C2[0];
    p.T0 = scaled(p.L, net_->endDir(road, s));
    p.T1 = scaled(p.L, net_->startDir(next.road, next.side));
    return p;
}

void TrafficSystem::cableSetPiece(TrafficCar& c, const Piece& p) {
    c.len = p.L;
    c.curve = RoadNetwork::curveOf(p);  // R = 1 / L
    c.invLen = c.curve.R;
}

phys::Mtx TrafficSystem::cableMatrix(const TrafficCar& c) const { return phys::mtxFromRows(c.X, c.Y, c.Z, c.pos); }

// ---- Reset ----

void TrafficSystem::resetCableCars() {
    for (CableCar& cc : cables_) resetCableCar(cc);
}

void TrafficSystem::resetCableCar(CableCar& cc) {
    TrafficCar& c = cars_[size_t(cc.car)];
    // 1. The prop goes back untouched into its room.
    if (world_ && cc.prop >= 0) world_->restoreProp(cc.prop);
    // 2.
    unregister(c);
    c.regKind = 0;
    cc.active = true;
    c.queued = false;
    cc.permission = false;
    c.v = c.a = c.vt = 0.0f;
    c.lookAhead = 25.0f;
    // 3.
    c.road = cc.startRoad;
    c.side = cc.startSide;
    c.next = cableNext(c.road, c.side);
    // 4.
    const Road& r = net_->roads()[size_t(c.road)];
    cc.turning = false;
    c.k = 1;
    c.u = 0.0f;
    cc.eps = 0.0f;
    cableSetPiece(c, cablePiece(c.road, c.side, 1, typeOf(c).front));
    c.netRoom = r.rooms.empty() ? 0 : r.rooms[0];
    // 5.
    c.pos = r.sideOf(c.side).cableLine[0];
    cablePose(c);
    c.room = city_->findRoom(c.pos, c.room);
    c.placeRoad = c.road;
    c.placeSide = c.side;
    c.placeK = c.k;
    if (world_ && cc.prop >= 0) world_->moveDynamicProp(cc.prop, cableMatrix(c), c.room);
}

// ---- Each frame ----

bool TrafficSystem::cableBlocked(const TrafficCar& c, float& b, const std::vector<TrafficPlayer>& players) const {
    const Road& r = net_->roads()[size_t(c.road)];
    const int n = r.n;
    if (c.k >= n - 1) return false;
    const auto& C = r.sideOf(c.side).cableLine;
    const vec3& o = c.pos;
    auto blocks = [&](float d) { return -1.0f < d && d < 30.0f; };
    // 1. Players within 30 m (flat), target the end of the current piece.
    for (const TrafficPlayer& p : players) {
        const vec3& q = p.bodyPos;
        if (!((o.z - q.z) * (o.z - q.z) + (o.x - q.x) * (o.x - q.x) < 900.0f)) continue;
        const float d = vehicleBlockDistance(q, p.X, p.Z, p.half.z, p.half.z, p.half.x, p.half.x, o, C[size_t(c.k)], 30.0f, 2.0f);
        if (blocks(d)) {
            b = d;
            return true;
        }
    }
    // 2-3. The vehicles registered on this side at entry k (and k + 1 when the piece ends within 30 m).
    auto entry = [&](int k, const vec3& target) {
        const int sec = c.side > 0 ? k : n - k;
        for (int ci : sectionCars(c.road, c.side, sec)) {
            if (ci == c.index) continue;
            const TrafficCar& v = cars_[size_t(ci)];
            const TrafficType& t = typeOf(v);
            const float d = vehicleBlockDistance(v.pos, v.X, v.Z, t.front, t.back, t.left, t.right, o, target, 30.0f, 2.0f);
            if (blocks(d)) {
                b = d;
                return true;
            }
        }
        return false;
    };
    if (entry(c.k, C[size_t(c.k)])) return true;
    if (c.len - c.u < 30.0f && c.k + 1 <= n - 1 && entry(c.k + 1, C[size_t(c.k + 1)])) return true;
    return false;
}

void TrafficSystem::cablePose(TrafficCar& c) {
    const float u = c.curve.R * c.u;
    const PieceCurve& k = c.curve;
    const float Px = ((u * k.ax + k.bx) * u + k.cx) * u + k.dx;
    const float Pz = ((u * k.az + k.bz) * u + k.cz) * u + k.dz;
    const float Dx = (((u * k.ax) * 3.0f) + (k.bx + k.bx)) * u + k.cx;
    const float Dz = (((u * k.az) * 3.0f) + (k.bz + k.bz)) * u + k.cz;
    const float l2 = (Dx * Dx + Dz * Dz) + 0.0f * 0.0f;
    const float g = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    const vec3 d(Dx * g, 0.0f, Dz * g);
    const Road& rd = net_->roads()[size_t(c.road)];
    const bool nextFlat = c.next.road >= 0 && (net_->roads()[size_t(c.next.road)].flags & 8);
    if ((rd.flags & 8) && nextFlat) {
        c.X = vec3(-d.z, 0.0f, d.x);
        c.Y = vec3(0.0f, 1.0f, 0.0f);
        c.Z = vec3(-d.x, -d.y, -d.z);
        c.pos = vec3(Px, rd.sideOf(c.side).cableLine[0].y, Pz);
        return;
    }
    // The probe path (traffic-visuals.md, "The solve", steps 7-11), each corner probed once from
    // 5 m above to 5 m below the car's current height.
    const TrafficType& t = typeOf(c);
    const float Xx = -d.z, Xz = d.x;
    const vec3 corners[3] = {
        vec3(((Px - t.left * Xx) + t.front * d.x), 0.0f, ((Pz - t.left * Xz) + t.front * d.z)),
        vec3(((t.right * Xx + Px) + t.front * d.x), 0.0f, ((t.right * Xz + Pz) + t.front * d.z)),
        vec3(((Px - t.left * Xx) - t.back * d.x), 0.0f, ((Pz - t.left * Xz) - t.back * d.z)),
    };
    probeCorners(c, corners, c.pos.y, 1, 5.0f, false);
}

void TrafficSystem::cableRegister(TrafficCar& c) {
    if (c.room == 0) return;
    const int room = city_->findRoom(c.pos, c.netRoom);
    c.netRoom = room;
    const Component comp = net_->componentAt(c.pos, room);
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
        if (c.regKind == 3) return;
        unregister(c);
        auto& list = ixCars_[size_t(comp.index)];
        list.insert(list.begin(), c.index);
        c.regKind = 3;
        c.regIx = comp.index;
        return;
    }
    if (comp.kind == 2) return;  // a shortcut: not handled (MM2 prints an error)
    unregister(c);
}

void TrafficSystem::updateCableCars(float F, const std::vector<TrafficPlayer>& players) {
    const auto& roads = net_->roads();
    for (size_t ci = 0; ci < cables_.size(); ++ci) {
        CableCar& cc = cables_[ci];
        TrafficCar& c = cars_[size_t(cc.car)];
        // A car that no AI reset has placed yet (created without a traffic reset since): placed at
        // its start now, as a reset would (its road would otherwise be −1).
        if (c.road < 0 || size_t(c.road) >= roads.size()) resetCableCar(cc);
        // 1. Knocked loose (Hit hard).
        if (world_ && cc.prop >= 0 && world_->object(cc.prop).broken) {
            CableCar* partner = cc.partner >= 0 ? &cables_[size_t(cc.partner)] : nullptr;
            if (partner) partner->active = false;
            if (!isActive(c.road)) {
                if (partner) partner->active = true;
                world_->restoreProp(cc.prop);
            }
            continue;
        }
        if (!cc.active) continue;
        // 2. Speed.
        const Road& r = roads[size_t(c.road)];
        const int n = r.n;
        const float front = typeOf(c).front;
        const float dE = cc.turning ? 9999.0f : (r.CD[size_t(n - 1)] - r.CD[0]) - (cc.eps + front);
        float b = 0;
        const bool blocked = !cc.turning && !cableFreeRun_ && cableBlocked(c, b, players);
        auto cruise = [&]() {
            if (c.vt < 15.0f) {
                c.a = cc.ac;
                c.vt = 15.0f + 0.0001f;
            }
        };
        if (blocked) {
            if (c.vt != 0.0f) {
                c.vt = 0.0f;
                c.a = -((c.v * c.v) / ((b - 2.5f) + (b - 2.5f)));
            }
        } else if (dE >= c.lookAhead || dE <= 0.0f || cc.turning) {
            cruise();
        } else {
            if (!cc.permission) {
                cc.permission = cableFreeRun_ || net_->control().mayEnter(*net_, c.road, c.side, c.index, false, c.v, dE);
                processWakes();
            }
            if (!cc.permission) {
                if (c.vt == 0.0f) {
                    if (c.v < 0.2f && dE < 0.5f) c.v = 0.0f;
                } else {
                    c.vt = 0.0f;
                    c.a = -((c.v * c.v) / ((dE - 0.25f) + (dE - 0.25f)));
                }
            } else {
                cruise();
            }
        }
        c.v = F * c.a + c.v;
        if ((c.v < c.vt + 0.05f && c.a < 0.0f) || (c.vt < c.v && c.a > 0.0f)) {
            c.a = 0.0f;
            c.v = c.vt;
        }
        const float step = F * c.v;
        c.u = step + c.u;
        cc.eps = step + cc.eps;
        if (c.v == 0.0f) continue;  // a car standing still is not re-posed
        // Advancing (one piece change per frame).
        if (c.u > c.len) {
            if (!cc.turning) {
                c.k = c.k + 1;
                c.u = c.u - c.len;
                if (c.next.road < 0 || c.k <= n - 1) {
                    cableSetPiece(c, cablePiece(c.road, c.side, std::min(c.k, n - 1), front));
                } else {
                    cableSetPiece(c, cableTurnPiece(c.road, c.side, c.next, front));
                    cc.turning = true;
                }
            } else {
                c.queued = false;
                cc.permission = false;
                c.lookAhead = 25.0f;
                const RoadEnd& e0 = r.end[0];
                if (e0.control == 0) net_->control().leftIntersection(e0.intersection, c.index);
                c.road = c.next.road;
                c.side = c.next.side;
                c.next = cableNext(c.road, c.side);
                c.u = c.u - c.len;
                cc.eps = c.u;
                c.k = 1;
                cableSetPiece(c, cablePiece(c.road, c.side, 1, front));
                cc.turning = false;
            }
        }
        cablePose(c);
        c.placeRoad = c.road;
        c.placeSide = c.side;
        c.placeK = cc.turning ? roads[size_t(c.road)].n : c.k;
        cableRegister(c);
        c.room = city_->findRoom(c.pos, c.room);
        if (world_ && cc.prop >= 0) world_->moveDynamicProp(cc.prop, cableMatrix(c), c.room);
    }
}

}  // namespace ai
}  // namespace cc
