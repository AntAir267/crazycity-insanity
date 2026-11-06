// Loose bodies (spec/physics/loose-bodies.md): the two pools, bodies for objects, breaking props
// into copies and pieces, thrown car parts, the per-frame declaration and letting go.
#include "physics/loose_bodies.h"

#include <algorithm>
#include <cmath>

#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "model/parts.h"
#include "physics/car.h"
#include "physics/collision_world.h"
#include "physics/pmath.h"
#include "physics/scene.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

void LooseBody::notePush(const vec3& push, int fromIdentity) {
    const float p2 = (push.x * push.x + push.y * push.y) + push.z * push.z;
    if (p2 > largestPush2) {
        largestPush2 = p2;
        largestSource = fromIdentity;
    }
}

void setupLooseMass(RigidBody& rb, const BangerData& bd) {
    // vehicle-body.md, "Mass and inertia", then no moment below the largest / 40 (2.2 step 3).
    const float m = bd.mass, k = 0.083333336f;
    const vec3& b = bd.size;
    float Ix = ((b.z * b.z + b.y * b.y) * m) * k;
    float Iy = ((b.z * b.z + b.x * b.x) * m) * k;
    float Iz = ((b.y * b.y + b.x * b.x) * m) * k;
    const float M = ((Ix <= Iy && Iy < Iz) || (Ix > Iy && Ix < Iz)) ? Iz : (Ix <= Iy ? Iy : Ix);
    const float f = M / 40.0f;
    if (Ix < f) Ix = f;
    if (Iy < f) Iy = f;
    if (Iz < f) Iz = f;
    rb.setMass(m, vec3(Ix, Iy, Iz));
    // 2.2 step 4.
    rb.vmax = 500.0f;
    rb.setAngularLimit(5.0f);
}

void looseBoundFor(LooseData& d, const std::shared_ptr<Bound>& geometry) {
    const BangerData& bd = d.bd;
    d.material = Material();
    d.material.elasticity = bd.elasticity;
    d.material.friction = bd.friction;
    const int prim = bd.collisionPrim;
    if (prim == 2 || prim == 3) {
        // A capsule (radius YRadius, cylinder Size.y along Y) or a sphere: the box of its bounding
        // box (6.2: size max − min, offset (max − min)·0.5 + min).
        const float r = bd.yRadius;
        const float h = prim == 2 ? bd.size.y * 0.5f + r : r;
        const vec3 mn(-r, -h, -r), mx(r, h, r);
        const vec3 size(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
        const vec3 off(size.x * 0.5f + mn.x, size.y * 0.5f + mn.y, size.z * 0.5f + mn.z);
        d.bound = std::make_shared<Bound>(makeBoxBound(size, off));
        d.box = true;
        d.boxSize = size;
        d.boxOffset = off;
        d.prim = prim;  // the box is only for the city; capsule tests elsewhere (6.3)
    } else if (geometry && !geometry->isBox) {
        d.bound = geometry;
        d.box = false;
    } else {
        d.bound = std::make_shared<Bound>(makeBoxBound(bd.size));
        d.box = true;
        d.boxSize = bd.size;
        d.boxOffset = vec3();
    }
    if (prim != 2 && prim != 3) d.prim = 0;
}

// ---------------------------------------------------------------------------------------------
// The pools (1)

void CollisionWorld::resetLoose() {
    if (onLooseInactive)
        for (int e = 0; e < kBodyCount; ++e)
            if (bodies_[e].inUse) onLooseInactive(e);
    for (int e = 0; e < kBodyCount; ++e) {
        bodies_[e] = LooseBody();
        bodies_[e].identity = kLooseIdentityBase + e;
        order_[e] = e;
    }
    inUse_ = 0;
    for (int s = 0; s < kSlotCount; ++s) {
        slots_[s] = MovedSlot();
        syncSlotObject(s);
    }
    ring_ = 0;
    roomObjects_ = roomObjectsAtReset_;
    // The lists are those of setup time again: nothing in them was filed while driving.
    for (CollisionObject& o : objs_) o.filed = 0;
    for (CollisionObject& o : gizObjs_) o.filed = 0;
    refileMoving();
    events_.clear();
    panes_.clear();
    paneNext_ = 0;
}

void CollisionWorld::switchOffMover(int e) {
    if (!movers_) return;
    for (SceneMover& m : *movers_)
        if (m.kind == SceneMover::LooseMover && m.loose == e) {
            m.U = m.C = m.V = m.O = false;
            m.appendedThisSample = false;
            m.loose = -1;
        }
}

// The body leaves its object (it stops being a mover; the object is collidable again).
void CollisionWorld::detachBody(int e) {
    LooseBody& L = bodies_[e];
    switchOffMover(e);
    if (L.slot >= 0) {
        slots_[L.slot].body = -1;
        syncSlotObject(L.slot);
    }
    if (onLooseInactive && (L.slot >= 0 || L.prop >= 0)) onLooseInactive(e);
    L.slot = L.prop = -1;
}

int CollisionWorld::takeBody() {
    int e;
    if (inUse_ < kBodyCount) {
        e = order_[inUse_];
        ++inUse_;
    } else {
        // All in use: the first is let go and taken again; it keeps its place at the front.
        e = order_[0];
        detachBody(e);
    }
    LooseBody& L = bodies_[e];
    L.inUse = true;
    L.slot = L.prop = -1;
    L.data = nullptr;
    return e;
}

void CollisionWorld::letGo(int e) {
    if (e < 0 || e >= kBodyCount) return;
    LooseBody& L = bodies_[e];
    if (!L.inUse) return;
    detachBody(e);
    L.inUse = false;
    for (int k = 0; k < inUse_; ++k)
        if (order_[k] == e) {
            std::swap(order_[k], order_[inUse_ - 1]);
            break;
        }
    --inUse_;
}

int CollisionWorld::takeSlot() {
    // The ring of 40: 0 … 39, then 0 twice, then 1 … 39 (10).
    int s;
    if (ring_ >= kSlotCount) {
        ring_ = 0;
        s = 0;
    } else {
        s = ring_++;
    }
    clearSlot(s);
    return s;
}

void CollisionWorld::clearSlot(int s) {
    MovedSlot& S = slots_[s];
    if (S.body >= 0) letGo(S.body);
    slotLeaveRoom(s);
    S = MovedSlot();
    syncSlotObject(s);
}

void CollisionWorld::slotLeaveRoom(int s) {
    MovedSlot& S = slots_[s];
    if (S.room > 0 && size_t(S.room) < roomObjects_.size()) {
        auto& rl = roomObjects_[size_t(S.room)];
        rl.erase(std::remove(rl.begin(), rl.end(), firstSlot_ + s), rl.end());
    }
    S.room = 0;
    syncSlotObject(s);
}

void CollisionWorld::slotToRoomFront(int s, int room) {
    slotLeaveRoom(s);
    if (room > 0 && size_t(room) < roomObjects_.size()) {
        auto& rl = roomObjects_[size_t(room)];
        rl.insert(rl.begin(), firstSlot_ + s);
        slots_[s].room = room;
        slots_[s].filed = fileStamp();
    }
    syncSlotObject(s);
}

void CollisionWorld::syncSlotObject(int s) {
    const MovedSlot& S = slots_[s];
    CollisionObject& o = slotObjs_[size_t(s)];
    o.kind = ObjKind::Moved;
    o.slot = s;
    o.identity = kFixedIdentity;
    o.ground = false;  // wheels never probe moved props (5.4)
    o.yRadius = 0.0f;  // no cylinder test
    if (S.data) {
        o.bound = S.data->bound;
        o.prim = S.data->box ? PropPrim::Box : PropPrim::Geometry;
        o.material = S.data->material;
        o.radius = S.data->radius;
        o.collisionType = S.data->bd.collisionType;
    } else {
        o.bound.reset();
    }
    o.O = S.M;
    o.position = S.M.o;
    o.broken = !S.collidable();
    o.filed = S.filed;
}

void CollisionWorld::setupBody(int e, const LooseData& d, const Mtx& M) {
    // 2.2.
    LooseBody& L = bodies_[e];
    L.rb.reset();
    L.rb.R.r[0] = M.X;
    L.rb.R.r[1] = M.Y;
    L.rb.R.r[2] = M.Z;
    L.rb.p = M.o;
    setupLooseMass(L.rb, d.bd);
    L.rest.reset();
    L.prev = M;
    L.prevLargestSource = L.largestSource = 0;
    L.largestPush2 = 0.0f;
    L.data = &d;
}

// ---------------------------------------------------------------------------------------------
// Movers (5)

bool CollisionWorld::joinMovers(int e, Join kind) {
    if (!movers_ || e < 0) return false;
    std::vector<SceneMover>& mv = *movers_;
    const LooseBody& L = bodies_[e];
    const int s = L.slot;
    const int room = s >= 0 ? slots_[s].room : 0;
    if (kind == Join::LyingStill)
        for (SceneMover& m : mv)
            if (m.kind == SceneMover::LooseMover && m.looseSlot == s && s >= 0) {
                m.loose = e;
                m.U = m.C = m.V = m.O = false;
                m.appendedThisSample = true;
                m.identity = L.identity;
                return true;
            }
    if (mv.size() >= 32 || room == 0) return false;
    if (kind == Join::Simple)
        for (const SceneMover& m : mv)
            if (m.kind == SceneMover::LooseMover && m.loose >= 0 && m.looseSlot == s) return false;
    SceneMover m;
    m.kind = SceneMover::LooseMover;
    m.loose = e;
    m.looseSlot = s;
    m.U = m.C = m.V = m.O = false;
    m.appendedThisSample = true;
    m.identity = L.identity;
    m.firstKind = !(L.data && (L.data->bd.collisionType & 0x40));
    mv.push_back(m);
    return true;
}

void CollisionWorld::declareLoose(std::vector<SceneMover>& movers, int playerRoom, const std::vector<int>* extraRooms) {
    movers_ = &movers;
    // The pool drops still, fallen and room-0 bodies without letting them go.
    for (int k = 0; k < inUse_;) {
        const int e = order_[k];
        LooseBody& L = bodies_[e];
        const int room = L.slot >= 0 ? slots_[L.slot].room : 0;
        if (L.rest.state == 0 || L.rb.p.y < -100.0f || room == 0) {
            if (L.slot >= 0) {
                slots_[L.slot].body = -1;
                slots_[L.slot].frozen = true;
                syncSlotObject(L.slot);
            }
            if (onLooseInactive) onLooseInactive(e);
            L.inUse = false;
            L.slot = L.prop = -1;
            std::swap(order_[k], order_[inUse_ - 1]);
            --inUse_;
            continue;
        }
        ++k;
    }
    // Declared by CollisionType, in list order.
    for (int k = 0; k < inUse_; ++k) {
        const int e = order_[k];
        const LooseBody& L = bodies_[e];
        const int t = L.data ? L.data->bd.collisionType : 0;
        SceneMover m;
        m.kind = SceneMover::LooseMover;
        m.loose = e;
        m.looseSlot = L.slot;
        m.identity = L.identity;
        if (t & 0x40) {
            m.firstKind = false;
        } else if (t & 0x10) {
            m.firstKind = true;
        } else if (t & 4) {
            m.firstKind = true;
            m.V = m.O = false;
        } else {
            continue;  // stays frozen where it is
        }
        if (movers.size() >= 32) break;
        movers.push_back(m);
    }
    // The player's rooms: its room, then that room's neighbours (at most 20).
    std::vector<int> rooms;
    // A room-waking opponent (opponents.md, "One frame" step 3): its room, then its neighbours in
    // perimeter order; one "already listed" flag, set by the first entry found in the list, stops
    // the adding for all of its later entries; an entry is added only while fewer than 20 are listed.
    auto addRoom = [&](int room) {
        if (room <= 0) return;
        std::vector<int> entries{room};
        if (city_ && size_t(room) < city_->rooms().size())
            for (int nb : city_->rooms()[size_t(room)].neighbours) entries.push_back(nb);
        for (int e : entries) {
            if (std::find(rooms.begin(), rooms.end(), e) != rooms.end()) return;
            if (rooms.size() < 20) rooms.push_back(e);
        }
    };
    if (playerRoom > 0) {
        rooms.push_back(playerRoom);
        if (city_ && size_t(playerRoom) < city_->rooms().size())
            for (int nb : city_->rooms()[size_t(playerRoom)].neighbours) {
                if (rooms.size() >= 20) break;
                if (std::find(rooms.begin(), rooms.end(), nb) != rooms.end()) break;
                rooms.push_back(nb);
            }
    }
    if (extraRooms)
        for (int r : *extraRooms) addRoom(r);
    for (size_t i = 0; i < movers.size();) {
        const SceneMover& m = movers[i];
        if (m.kind == SceneMover::LooseMover && m.firstKind && m.loose >= 0) {
            const int e = m.loose;
            const int s = bodies_[e].slot;
            const int room = s >= 0 ? slots_[s].room : 0;
            if (std::find(rooms.begin(), rooms.end(), room) == rooms.end()) {
                movers.erase(movers.begin() + long(i));
                letGo(e);
                if (s >= 0) slotLeaveRoom(s);
                LooseEvent ev;
                ev.kind = LooseEvent::Gone;
                ev.id = kSlotIdBase + s;
                events_.push_back(ev);
                continue;
            }
        }
        ++i;
    }
    movers_ = nullptr;
}

void CollisionWorld::afterLooseSamples(std::vector<SceneMover>& movers) {
    movers_ = &movers;
    for (size_t i = 0; i < movers.size(); ++i) {
        const SceneMover& m = movers[i];
        if (m.kind != SceneMover::LooseMover || m.loose < 0 || !m.U) continue;
        const LooseBody& L = bodies_[m.loose];
        if (L.rest.state == 0 || !(L.rb.p.y >= -100.0f)) {
            const int s = L.slot;
            letGo(m.loose);
            LooseEvent ev;
            ev.kind = LooseEvent::Rest;
            ev.id = kSlotIdBase + s;
            events_.push_back(ev);
        } else if (onLooseFrame) {
            onLooseFrame(m.loose, L.slot >= 0 ? slots_[L.slot].M : L.matrix());
        }
    }
    movers_ = nullptr;
}

void CollisionWorld::looseUpdate(int e, float dt, float invdt) {
    // 6.1.
    LooseBody& L = bodies_[e];
    RigidBody& rb = L.rb;
    rb.F.y = rb.m * -19.6f + rb.F.y;
    restDetect(L.rest, rb, invdt, 0.1f, 0.5f);
    if (L.rest.state == 1)
        rb.integrate(dt);
    else
        rb.integrateFrozen();  // no motion; lastPush = pushSum, pushSum = 0 (6.1 step 3)
    if (L.slot >= 0) {
        MovedSlot& S = slots_[L.slot];
        S.M = L.matrix();
        const int room = city_ ? city_->findRoom(rb.p, S.room) : 0;
        if (room != S.room) slotToRoomFront(L.slot, room);
        syncSlotObject(L.slot);
    }
}

void CollisionWorld::looseEnd(int e) {
    // 6.4.
    LooseBody& L = bodies_[e];
    L.rb.applyPush();
    L.prev = L.matrix();
    L.prevLargestSource = L.largestSource;
    L.largestSource = 0;
    L.largestPush2 = 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Bodies for objects (2), breaking (3), glass (8)

int CollisionWorld::propTestBody(int oi) {
    const int e = takeBody();
    CollisionObject& o = object(oi);
    setupBody(e, *o.loose, o.O);
    bodies_[e].prop = oi;
    if (onLooseActive) onLooseActive(e, o.loose->name, o.O, true);
    return e;
}

int CollisionWorld::slotTestBody(int s) {
    const int e = takeBody();
    MovedSlot& S = slots_[s];
    setupBody(e, *S.data, S.M);
    bodies_[e].slot = s;
    S.body = e;
    syncSlotObject(s);
    joinMovers(e, Join::LyingStill);
    if (onLooseActive && S.data) onLooseActive(e, S.data->name, S.M, false);
    return e;
}

const LooseData* CollisionWorld::pieceData(int oi, int i) {
    const CollisionObject& o = object(oi);
    if (!o.loose || !vfs_) return nullptr;
    const std::string name = strf("%s_BREAK%02d", o.loose->name.c_str(), i);
    auto it = pieces_.find(name);
    if (it != pieces_.end()) return it->second.get();
    std::shared_ptr<LooseData> d = std::make_shared<LooseData>();
    d->name = name;
    if (!loadBangerData(*vfs_, name, d->bd)) {
        logWarn("tune/banger/%s.dgBangerData missing: piece %d of %s is not made", name.c_str(), i, o.loose->name.c_str());
        pieces_[name] = nullptr;
        return nullptr;
    }
    std::shared_ptr<Bound> geom;
    if (d->bd.collisionPrim == 0) {
        MaterialTable scratch;
        if (auto gb = loadGeometryBound(*vfs_, name, scratch)) {
            auto mb = std::make_shared<Bound>(*gb);
            for (vec3& p : mb->points) p = vec3(p.x + (-d->bd.cg.x), p.y + (-d->bd.cg.y), p.z + (-d->bd.cg.z));
            mb->computeBox();
            geom = mb;
        }
    }
    looseBoundFor(*d, geom);
    // The piece's model: part BREAK<ii> of the prop's package (pkg.md: the object's own levels,
    // then BREAK01 up to NumParts).
    if (o.loose->pkg) {
        const int n = o.loose->bd.numParts;
        std::vector<std::string> names{""};
        for (int k = 1; k <= std::max(n, i); ++k) names.push_back(strf("BREAK%02d", k));
        const PartTable pt = takeParts(*o.loose->pkg, names);
        if (size_t(i) < pt.parts.size()) {
            const PartLods& pl = pt.parts[size_t(i)];
            for (int l = 0; l < LodCount; ++l) {
                d->lod[l] = pl.at(Lod(l));
                if (d->lod[l] && d->lod[l]->radius > d->radius) d->radius = d->lod[l]->radius;
            }
        }
        d->pkg = o.loose->pkg;
    }
    pieces_[name] = d;
    return d.get();
}

void CollisionWorld::finishPropTest(int oi, int e, bool broke, const vec3& v) {
    LooseBody& L = bodies_[e];
    const bool mine = L.inUse && L.prop == oi;
    if (!broke) {
        if (mine) letGo(e);
        return;
    }
    CollisionObject& o = object(oi);
    const BangerData& bd = o.loose->bd;
    const int room = o.room;
    auto markBroken = [&]() {
        o.broken = true;
        if (o.cityObject >= 0) broken_.push_back(o.cityObject);
        if (o.cityObject >= 0 && size_t(o.cityObject) < brokenFlags_.size()) brokenFlags_[size_t(o.cityObject)] = 1;
    };
    auto leaveRoom = [&]() {
        if (room > 0 && size_t(room) < roomObjects_.size()) {
            auto& rl = roomObjects_[size_t(room)];
            rl.erase(std::remove(rl.begin(), rl.end(), oi), rl.end());
        }
    };
    LooseEvent ev;
    ev.kind = LooseEvent::Break;
    ev.id = oi;
    if (onPropBroken) onPropBroken(oi);
    if (bd.billFlags & 0x100) {
        // 8: a glass pane breaks in place: no loose body, it stays in its room; the shard burst.
        if (mine) letGo(e);
        markBroken();
        const vec3& Z = o.O.Z;
        const vec3& p = o.O.o;
        const float side = (Z.x * v.x + Z.y * v.y) + Z.z * v.z;
        PaneBurst b;
        b.at = side > 0.0f ? vec3(p.x - Z.x, p.y - Z.y, p.z - Z.z) : vec3(p.x + Z.x, p.y + Z.y, p.z + Z.z);
        ProbeState st;
        ProbeHit hit;
        b.floorY = probe(b.at, vec3(b.at.x, b.at.y - 100.0f, b.at.z), st, hit) ? hit.point.y : b.at.y - 100.0f;
        b.cityObject = o.cityObject;
        if (panes_.size() < 2) {
            panes_.push_back(b);
        } else {
            panes_[size_t(paneNext_)] = b;
            paneNext_ = 1 - paneNext_;
        }
        ev.n = bd.numParts;
        events_.push_back(ev);
        return;
    }
    const int N = bd.numParts;
    leaveRoom();
    markBroken();
    ev.n = N;
    if (N <= 0) {
        // The prop moves as one piece: a slot takes it over with its body as it is.
        const int s = takeSlot();
        MovedSlot& S = slots_[s];
        S.held = true;
        S.data = o.loose.get();
        S.hold = o.loose;
        S.M = o.O;
        S.set = o.set;
        S.kind = MovedSlot::Copy;
        S.fromObject = oi;
        slotToRoomFront(s, room);
        if (bodies_[e].inUse && bodies_[e].prop == oi) {
            bodies_[e].prop = -1;
            bodies_[e].slot = s;
            S.body = e;
            syncSlotObject(s);
            joinMovers(e, Join::Replacing);  // the copy keeps the body (and burst) of the test
        }
        ev.ids.push_back(kSlotIdBase + s);
        events_.push_back(ev);
        return;
    }
    // N pieces.
    const vec3& X = o.O.X;
    const vec3& Y = o.O.Y;
    const vec3& Z = o.O.Z;
    const vec3& q = o.O.o;
    const vec3& c = bd.cg;
    vec3 org(q.x - ((Z.x * c.z + Y.x * c.y) + X.x * c.x), q.y - ((Z.y * c.z + Y.y * c.y) + X.y * c.x),
             q.z - ((Z.z * c.z + Y.z * c.y) + X.z * c.x));
    const RigidBody B = bodies_[e].rb;  // its impulses: what the hit gave the whole prop
    const vec3 dv(B.G.x * B.invM, B.G.y * B.invM, B.G.z * B.invM);
    // World inverse inertia W = Rᵀ·S (S rows X/Ix, Y/Iy, Z/Iz), sums in the orders of 3.
    float W[3][3];
    {
        const float iI[3] = {B.invI.x, B.invI.y, B.invI.z};
        float Sm[3][3], a[3][3];
        for (int k = 0; k < 3; ++k) {
            const vec3& Rk = B.R.r[k];
            Sm[k][0] = Rk.x * iI[k];
            Sm[k][1] = Rk.y * iI[k];
            Sm[k][2] = Rk.z * iI[k];
        }
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < 3; ++k) a[i][k] = B.R.at(k, i);
        static const int ord[3][3][3] = {{{0, 1, 2}, {1, 2, 0}, {1, 2, 0}},
                                         {{1, 0, 2}, {2, 1, 0}, {2, 1, 0}},
                                         {{0, 2, 1}, {2, 1, 0}, {2, 1, 0}}};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                const int* o3 = ord[i][j];
                const float t[3] = {a[i][0] * Sm[0][j], a[i][1] * Sm[1][j], a[i][2] * Sm[2][j]};
                W[i][j] = (t[o3[0]] + t[o3[1]]) + t[o3[2]];
            }
    }
    const vec3& H0 = B.H;
    const vec3 dw((H0.z * W[2][0] + H0.y * W[1][0]) + H0.x * W[0][0], (H0.z * W[2][1] + H0.y * W[1][1]) + H0.x * W[0][1],
                  (H0.z * W[2][2] + H0.y * W[1][2]) + H0.x * W[0][2]);
    const vec3 pB = B.p;
    for (int i = 1; i <= N; ++i) {
        const LooseData* pd = pieceData(oi, i);
        if (!pd) continue;
        const int s = takeSlot();
        MovedSlot& S = slots_[s];
        S.held = true;
        S.data = pd;
        S.hold = pieces_[pd->name];
        S.set = o.set;
        S.kind = MovedSlot::Piece;
        S.fromObject = oi;
        slotToRoomFront(s, room);
        const vec3& ec = pd->bd.cg;
        const vec3 d((Z.x * ec.z + Y.x * ec.y) + X.x * ec.x, (X.y * ec.x + Z.y * ec.z) + Y.y * ec.y,
                     (X.z * ec.x + Z.z * ec.z) + Y.z * ec.y);
        const vec3 pos(org.x + d.x, org.y + d.y, org.z + d.z);
        org = vec3(pos.x - d.x, pos.y - d.y, pos.z - d.z);
        S.M = mtxFromRows(X, Y, Z, pos);
        const int e2 = takeBody();
        setupBody(e2, *pd, S.M);
        bodies_[e2].slot = s;
        S.body = e2;
        syncSlotObject(s);
        if (onLooseActive) onLooseActive(e2, pd->name, S.M, true);
        // Its share of the hit.
        RigidBody& rb = bodies_[e2].rb;
        const vec3 r(pB.x - rb.p.x, pB.y - rb.p.y, pB.z - rb.p.z);
        const vec3 cr(r.y * dw.z - r.z * dw.y, r.z * dw.x - r.x * dw.z, r.x * dw.y - r.y * dw.x);
        const float mi = rb.m;
        rb.G = vec3(((cr.x + dv.x) * mi) + rb.G.x, ((cr.y + dv.y) * mi) + rb.G.y, ((cr.z + dv.z) * mi) + rb.G.z);
        Mat3 T;
        const float Ii[3] = {rb.I.x, rb.I.y, rb.I.z};
        for (int a = 0; a < 3; ++a)
            for (int b = 0; b < 3; ++b) T.m[a][b] = Ii[b] * rb.R.at(b, a);
        const Mat3 Iw = mulProducts(T, toMat3(rb.R));
        vec3 Lw;
        for (int k = 0; k < 3; ++k)
            (&Lw.x)[k] = (Iw.m[2][k] * dw.z + Iw.m[1][k] * dw.y) + Iw.m[0][k] * dw.x;
        rb.H = vec3(Lw.x + rb.H.x, Lw.y + rb.H.y, Lw.z + rb.H.z);
        joinMovers(e2, Join::Replacing);
        ev.ids.push_back(kSlotIdBase + s);
    }
    if (bodies_[e].inUse && bodies_[e].prop == oi) letGo(e);  // its pushes are lost
    events_.push_back(ev);
}

// ---------------------------------------------------------------------------------------------
// Thrown car parts (4) and taking them back (7)

void CollisionWorld::throwCarPart(Car& car, int part, const vec3& P, const vec3& H) {
    if (part < 0 || size_t(part) >= car.tune().looseParts.size()) return;
    const LoosePartTune& lp = car.tune().looseParts[size_t(part)];
    // 1-2: a slot at the front of the car's room, with the part's data, model and the car's paint.
    const int s = takeSlot();
    MovedSlot& S = slots_[s];
    S.held = true;
    S.data = lp.data.get();
    S.hold = lp.data;
    S.set = car.paintSet();
    S.kind = MovedSlot::CarPart;
    S.car = &car;
    S.part = part;
    const int room = flat_ ? 0 : car.collision().room;
    if (room > 0) slotToRoomFront(s, room);
    // 3: the part's own matrix (identity, position p) times the car's world matrix.
    const Mtx b = car.bodyMatrix();
    Mat3 A, Bm;
    for (int i = 0; i < 3; ++i) A.m[i][i] = 1.0f;
    const vec3* br[3] = {&b.X, &b.Y, &b.Z};
    for (int i = 0; i < 3; ++i) {
        Bm.m[i][0] = br[i]->x;
        Bm.m[i][1] = br[i]->y;
        Bm.m[i][2] = br[i]->z;
    }
    const Mat3 Rm = mulProducts(A, Bm);
    const vec3& p = lp.place;
    const float t0x = p.x * b.X.x, t1x = p.y * b.Y.x, t2x = p.z * b.Z.x;
    const float t0y = p.x * b.X.y, t1y = p.y * b.Y.y, t2y = p.z * b.Z.y;
    const float t0z = p.x * b.X.z, t1z = p.y * b.Y.z, t2z = p.z * b.Z.z;
    const vec3 pos(((t1x + t0x) + t2x) + b.o.x, ((t1y + t0y) + t2y) + b.o.y, ((t2z + t1z) + t0z) + b.o.z);
    S.M = mtxFromRows(vec3(Rm.m[0][0], Rm.m[0][1], Rm.m[0][2]), vec3(Rm.m[1][0], Rm.m[1][1], Rm.m[1][2]),
                      vec3(Rm.m[2][0], Rm.m[2][1], Rm.m[2][2]), pos);
    // 4: a body at rest; 5: momentum set, angular impulse added.
    const int e = takeBody();
    setupBody(e, *lp.data, S.M);
    bodies_[e].slot = s;
    S.body = e;
    RigidBody& rb = bodies_[e].rb;
    rb.P = P;
    rb.H = vec3(rb.H.x + H.x, rb.H.y + H.y, rb.H.z + H.z);
    rb.R.r[0] = S.M.X;
    rb.R.r[1] = S.M.Y;
    rb.R.r[2] = S.M.Z;
    rb.p = S.M.o;
    syncSlotObject(s);
    if (onLooseActive) onLooseActive(e, lp.data->name, S.M, true);
    // 6: a mover only if the car's room is not 0.
    if (room != 0) joinMovers(e, Join::Simple);
    LooseEvent ev;
    ev.kind = LooseEvent::Eject;
    ev.part = lp.name;
    ev.id = kSlotIdBase + s;
    ev.P = rb.P;
    ev.H = rb.H;
    events_.push_back(ev);
}

void CollisionWorld::takeBackCarParts(const Car& car) {
    for (int s = 0; s < kSlotCount; ++s) {
        const MovedSlot& S = slots_[s];
        if (S.held && S.kind == MovedSlot::CarPart && S.car == &car) clearSlot(s);
    }
}

}  // namespace phys
}  // namespace cc
