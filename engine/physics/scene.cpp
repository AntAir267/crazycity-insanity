#include "physics/scene.h"

#include <algorithm>

#include "city/city_data.h"
#include "physics/pmath.h"
#include "physics/trailer.h"

namespace cc {
namespace phys {

namespace {
constexpr int kCity = CollisionWorld::kCityIdentity;
constexpr int kFixed = CollisionWorld::kFixedIdentity;
constexpr int kLaneFirst = CollisionWorld::kLaneFirstIdentity;

bool isLoose(const SceneMover& m) { return m.kind == SceneMover::LooseMover; }

// trailer.md 6.1: the pair truck–trailer is skipped while the joint is attached.
bool jointedPair(const SceneMover& a, const SceneMover& b) {
    auto test = [](const SceneMover& c, const SceneMover& t) {
        return c.kind == SceneMover::CarMover && t.kind == SceneMover::TrailerMover && t.trailer &&
               &t.trailer->truck() == c.car && t.trailer->attached();
    };
    return test(a, b) || test(b, a);
}

void traceImpact(Car* car, int kind, const Impact& im, int obj) {
    if (car && car->collision().tracing) car->collision().traceImpacts.push_back({kind, im, obj});
}
}  // namespace

vec3 Scene::centreOf(const SceneMover& m) const {
    switch (m.kind) {
    case SceneMover::CarMover: {
        const RigidBody& b = m.car->body();
        return vadd(b.p, b.R.r[1]);
    }
    case SceneMover::TrafficBodyMover: return vadd(m.tp->rb.p, m.tp->rb.R.r[1]);
    case SceneMover::LooseMover: return m.loose >= 0 ? world_->looseBody(m.loose).rb.p : vec3();
    case SceneMover::TrailerMover: return m.trailer->body().p;  // its body position, no Y row (6.1)
    default: return vadd(m.laneM.o, m.laneM.Y);
    }
}

float Scene::radiusOf(const SceneMover& m) const {
    if (m.kind == SceneMover::CarMover) return m.car->modelRadius();
    if (m.kind == SceneMover::TrailerMover) return m.trailer->radius();
    if (m.kind == SceneMover::LooseMover) {
        if (m.loose < 0) return 0.0f;
        const LooseData* d = world_->looseBody(m.loose).data;
        return d ? d->radius : 0.0f;
    }
    return m.ttype ? m.ttype->bodyRadius : 0.0f;
}

int Scene::soundIdOf(const Side& s) const {
    if (s.loose < 0 || !world_) return 0;
    const LooseBody& L = world_->looseBody(s.loose);
    return L.data ? L.data->bd.colliderId : 0;
}

int Scene::roomOf(const SceneMover& m) const {
    switch (m.kind) {
    case SceneMover::CarMover: return m.car->collision().room;
    case SceneMover::TrafficBodyMover: return m.tp->room;
    case SceneMover::TrailerMover: return m.trailer->room();
    case SceneMover::LooseMover: {
        if (m.loose < 0) return 0;
        const int s = world_->looseBody(m.loose).slot;
        return s >= 0 ? world_->movedSlot(s).room : 0;
    }
    default: return m.laneRoom;
    }
}

Scene::Side Scene::sideOf(SceneMover& m) const {
    Side s;
    s.number = m.number;
    s.player = m.player;
    switch (m.kind) {
    case SceneMover::CarMover: {
        Car& c = *m.car;
        s.body = &c.body();
        s.bound = c.bound();
        s.M = c.bodyMatrix();
        s.M0 = c.collision().prevMatrix;
        s.prevSource = c.collision().prevLargestSource;
        s.lastPush = c.body().lastPush;
        s.material = c.boundMaterial();
        s.identity = m.identity;
        s.mass = c.body().m;
        s.car = &c;
        s.box = m.box;
        s.shape = m.shape;
        break;
    }
    case SceneMover::TrafficBodyMover: {
        TrafficPhys& t = *m.tp;
        s.body = &t.rb;
        s.bound = m.ttype->box.get();
        s.M = t.matrix();
        s.M0 = t.prev;
        s.prevSource = t.prevLargestSource;
        s.lastPush = t.rb.lastPush;
        s.material = m.ttype->material;
        s.identity = t.identity;
        s.mass = t.rb.m;
        s.tp = &t;
        s.box = true;
        s.shape = BoxShape{m.ttype->size, m.ttype->cg};
        break;
    }
    case SceneMover::TrailerMover: {
        Trailer& t = *m.trailer;
        s.body = &t.body();
        s.bound = t.bound();
        s.M = t.frame();
        s.M0 = t.prevMatrix();
        s.prevSource = t.prevSource();
        s.lastPush = t.body().lastPush;
        s.material = t.material();
        s.identity = m.identity;
        s.mass = t.body().m;
        s.trailer = &t;
        break;
    }
    case SceneMover::LooseMover: {
        if (m.loose < 0) break;
        LooseBody& L = world_->looseBody(m.loose);
        if (!L.data) break;
        s.body = &L.rb;
        s.bound = L.data->bound.get();
        s.M = L.matrix();
        s.M0 = L.prev;
        s.prevSource = L.prevLargestSource;
        s.lastPush = L.rb.lastPush;
        s.material = L.data->material;
        s.identity = L.identity;
        s.mass = L.rb.m;
        s.data = L.data;
        s.box = L.data->box;
        s.shape = BoxShape{L.data->boxSize, L.data->boxOffset};
        s.loose = m.loose;
        s.traceId = L.slot >= 0 ? CollisionWorld::kSlotIdBase + L.slot : -1;
        break;
    }
    default:
        s.bound = m.ttype->box.get();
        s.M = s.M0 = m.laneM;
        s.material = m.ttype->material;
        s.identity = kLaneFirst;
        s.box = true;
        s.shape = BoxShape{m.ttype->size, m.ttype->cg};
        break;
    }
    return s;
}

void Scene::notePush(SceneMover& m, const vec3& push, int identity) {
    if (m.kind == SceneMover::CarMover) m.car->notePush(push, identity);
    else if (m.kind == SceneMover::TrafficBodyMover) m.tp->notePush(push, identity);
    else if (m.kind == SceneMover::TrailerMover) m.trailer->notePush(push, identity);
    else if (m.kind == SceneMover::LooseMover && m.loose >= 0) world_->looseBody(m.loose).notePush(push, identity);
}

// One pair test with A = a (earlier mover) and B = b (later mover), or B = a gathered lane car.
void Scene::pairTest(SceneMover& a, SceneMover& b, float dt, SceneHooks& hooks, std::vector<SceneMover>& movers,
                     CollisionWorld& world, bool bIsLane, const LaneCar* lane) {
    (void)world;
    Side A = sideOf(a);
    Side B;
    SceneMover laneMover;
    if (bIsLane) {
        laneMover.kind = SceneMover::LaneMover;
        laneMover.ttype = lane->type;
        laneMover.laneM = lane->M;
        laneMover.laneId = lane->id;
        laneMover.laneSpeed = lane->speed;
        laneMover.laneRoom = lane->room;
        laneMover.number = lane->number;
        B = sideOf(laneMover);
        B.identity = kFixed;  // shapes tested as the second side without a body share one identity
    } else {
        B = sideOf(b);
    }
    if (!A.bound || !B.bound) return;
    Mtx MA0 = A.M0, MB0 = B.M0;
    if (A.body && A.prevSource == B.identity) MA0.o = vadd(MA0.o, A.lastPush);
    if (B.body && B.prevSource == A.identity) MB0.o = vadd(MB0.o, B.lastPush);
    std::vector<BoxImpact> impacts;
    const int primA = A.data ? A.data->prim : 0, primB = B.data ? B.data->prim : 0;
    if ((primA == 2 && primB == 0) || (primA == 0 && primB == 2)) {
        // loose-bodies.md 6.3, "Capsules in these tests": a capsule copy against a polygon side,
        // the polygon side in the car's role, H = the capsule's matrix, H0 its previous matrix
        // (both previous matrices shifted as above). The normals: out of B into A.
        const bool capA = primA == 2;
        const Side& P = capA ? B : A;
        const Side& C = capA ? A : B;
        const Mtx& P0 = capA ? MB0 : MA0;
        const Mtx& C0 = capA ? MA0 : MB0;
        std::vector<Impact> cap;
        capsuleObjectContacts(*P.bound, P.M, P0, C.data->bd.yRadius, C.data->bd.size.y, C.M, P.material, C.material, cap,
                              &C0);
        for (Impact& im : cap) {
            if (capA) im.n = vec3(-im.n.x, -im.n.y, -im.n.z);
            impacts.push_back(BoxImpact{im, true});
        }
    } else if (A.box && B.box) {
        // (Capsule against capsule and spheres: the boxes of 6.2, an engine choice.)
        boxBoxContacts(A.shape, A.M, MA0, B.shape, B.M, MB0, dt, impacts);
    } else {
        std::vector<Impact> poly;
        polygonalObjectContacts(*A.bound, A.M, MA0, *B.bound, B.M, dt, A.material, B.material, poly,
                                B.body ? &MB0 : nullptr);
        for (const Impact& im : poly) impacts.push_back(BoxImpact{im, true});
    }
    if (impacts.empty()) return;
    // A traffic car of the test takes its body before the response (4.1).
    SceneMover* bm = bIsLane ? nullptr : &b;
    if (bIsLane && hooks.makePhysical) {
        TrafficPhys* tp = hooks.makePhysical(lane->id, lane->M, lane->speed);
        if (tp) {
            SceneMover nm;
            nm.kind = SceneMover::TrafficBodyMover;
            nm.tp = tp;
            nm.ttype = lane->type;
            nm.laneId = lane->id;
            nm.U = nm.C = nm.V = nm.O = false;
            nm.appendedThisSample = true;
            nm.box = true;
            nm.shape = BoxShape{lane->type->size, lane->type->cg};
            nm.number = lane->number;
            nm.identity = tp->identity;
            movers.push_back(nm);
            bm = &movers.back();
            B.body = &tp->rb;
            B.mass = tp->rb.m;
            B.tp = tp;
            B.identity = tp->identity;  // pushes of this test are booked under the new body (3.3)
        }
    }
    SceneMover* am = &a;
    if (a.kind == SceneMover::LaneMover && hooks.makePhysical) {
        TrafficPhys* tp = hooks.makePhysical(a.laneId, a.laneM, a.laneSpeed);
        if (tp) {
            a.kind = SceneMover::TrafficBodyMover;
            a.tp = tp;
            a.appendedThisSample = true;
            a.identity = tp->identity;
            A.body = &tp->rb;
            A.mass = tp->rb.m;
            A.tp = tp;
            A.identity = tp->identity;
        }
    }
    const bool withLoose = A.loose >= 0 || B.loose >= 0;
    const float w = 1.0f / float(impacts.size());
    for (BoxImpact& bi : impacts) {
        Impact im = bi.im;
        Side& first = bi.aFirst ? A : B;
        Side& second = bi.aFirst ? B : A;
        SceneMover* fm = bi.aFirst ? am : bm;
        SceneMover* sm = bi.aFirst ? bm : am;
        im.matA = first.material;
        im.matB = second.material;
        // drive-trace.md: a car's impacts with a loose body (kind loose, the slot's object id).
        if (first.car && second.loose >= 0) traceImpact(first.car, 3, im, second.traceId);
        if (second.car && first.loose >= 0) traceImpact(second.car, 3, im, first.traceId);
        resolveOneImpact(im, first.body, second.body, w, [&](int side, const vec3& J, const vec3& push, const Impact& ri) {
            Side& me = side == 0 ? first : second;
            Side& other = side == 0 ? second : first;
            SceneMover* mm = side == 0 ? fm : sm;
            if (!me.body || !mm) return;
            notePush(*mm, push, other.identity);
            if (me.car)
                me.car->damageImpact(J, other.identity, other.body ? other.mass : 0.0f, ri.P, ri.n,
                                     soundIdOf(other));  // the record's normal (B into A)
            else if (me.tp && hooks.trafficImpact) hooks.trafficImpact(mm->laneId, J, soundIdOf(other));
        });
        if (hooks.onCarImpact && !withLoose) hooks.onCarImpact(first.number, second.number, im);
    }
    if (A.car) A.car->notifyImpact();
    if (B.car) B.car->notifyImpact();
    if (A.player && bm) bm->touchedPlayer = true;
    if (B.player) am->touchedPlayer = true;
}

// A mover's impacts against something without a body (the city, a fixed object): each impact
// resolved with weight 1/N, the mover as side A (`moverIsA`) or B.
void Scene::resolveWithTrace(SceneMover& m, const std::vector<Impact>& impacts, bool moverIsA, int otherIdentity,
                             int traceKind, int traceObj) {
    Side s = sideOf(m);
    for (const Impact& im : impacts) traceImpact(s.car, traceKind, im, traceObj);
    const float w = 1.0f / float(impacts.size());
    for (const Impact& im : impacts)
        resolveOneImpact(im, moverIsA ? s.body : nullptr, moverIsA ? nullptr : s.body, w,
                         [&](int side, const vec3& J, const vec3& push, const Impact& ri) {
                             if (side != (moverIsA ? 0 : 1) || !s.body) return;
                             notePush(m, push, otherIdentity);
                             // The slot keeps the impact record's normal (out of B into A), unturned.
                             if (s.car) s.car->damageImpact(J, otherIdentity, 0.0f, ri.P, ri.n);
                             else if (m.kind == SceneMover::TrafficBodyMover && hooks_ && hooks_->trafficImpact)
                                 hooks_->trafficImpact(m.laneId, J, 0);
                         });
    if (s.car) s.car->notifyImpact();
}

// Mover i against gathered object oi (collision-objects.md, "Contacts"; loose-bodies.md 6.3).
void Scene::objectTest(std::vector<SceneMover>& movers, size_t i, int oi, float dt, SceneHooks& hooks) {
    CollisionWorld& world = *world_;
    CollisionObject& o = world.object(oi);
    if (o.kind == ObjKind::Moved) {
        const MovedSlot& S = world.movedSlot(o.slot);
        if (S.body >= 0) {
            // It got a body earlier in this sample: a moving body now, a pair test.
            const int e = S.body;
            for (size_t q = 0; q < movers.size(); ++q)
                if (isLoose(movers[q]) && movers[q].loose == e) {
                    if (q == i) return;
                    pairTest(movers[i], movers[q], dt, hooks, movers, world, false, nullptr);
                    return;
                }
            SceneMover tmp;
            tmp.kind = SceneMover::LooseMover;
            tmp.loose = e;
            tmp.looseSlot = o.slot;
            tmp.identity = world.looseBody(e).identity;
            pairTest(movers[i], tmp, dt, hooks, movers, world, false, nullptr);
            return;
        }
        if (o.broken) return;
    }
    Side s = sideOf(movers[i]);
    if (!s.bound) return;
    // Tested as the shared fixed-object stand-in (car-collision.md 3.3).
    Mtx M0 = s.M0;
    if (s.body && s.prevSource == kFixed) M0.o = vadd(M0.o, s.lastPush);
    std::vector<Impact> impacts;
    if (!world.objectImpacts(oi, *s.bound, s.M, M0, s.material, dt, impacts, s.data) || impacts.empty()) return;
    SceneMover& m = movers[i];
    if (m.kind == SceneMover::LaneMover && hooks.makePhysical) {
        TrafficPhys* tp = hooks.makePhysical(m.laneId, m.laneM, m.laneSpeed);
        if (tp) {
            m.kind = SceneMover::TrafficBodyMover;
            m.tp = tp;
            m.appendedThisSample = true;
            m.identity = tp->identity;
            s.body = &tp->rb;
            s.identity = tp->identity;
        }
    }
    if (!s.body) return;
    const int myIdentity = s.identity;
    if (o.kind == ObjKind::Prop) {
        // An untouched prop: a test body, then the breakable rule (collision.md).
        for (const Impact& im : impacts) traceImpact(s.car, 2, im, world.traceId(oi));
        const int e = world.propTestBody(oi);
        LooseBody& L = world.looseBody(e);
        const int id = L.identity;
        const float mass = L.rb.m;
        RigidBody* prop = &L.rb;
        const int snd = o.loose ? o.loose->bd.colliderId : 0;
        const int laneId = movers[i].laneId;
        const bool trafficBody = movers[i].kind == SceneMover::TrafficBodyMover && hooks.trafficImpact;
        const bool broke = resolvePropImpacts(impacts, s.body, prop, o.impulseLimit2,
                                              [&](int side, const Impact& im, const vec3& J, const vec3& push) {
                                                  if (side == 0) {
                                                      notePush(movers[i], push, id);
                                                      if (s.car) s.car->damageImpact(J, id, mass, im.P, im.n, snd);
                                                      else if (trafficBody) hooks.trafficImpact(laneId, J, snd);
                                                  } else {
                                                      L.notePush(push, myIdentity);
                                                  }
                                              });
        world.finishPropTest(oi, e, broke, s.body->v);
        if (s.car) s.car->notifyImpact();
    } else if (o.kind == ObjKind::Moved) {
        // A moved prop lying still: a body, a mover, both bodies (loose-bodies.md 6.3).
        for (const Impact& im : impacts) traceImpact(s.car, 3, im, world.traceId(oi));
        const int e = world.slotTestBody(o.slot);
        LooseBody& L = world.looseBody(e);
        const int id = L.identity;
        const int snd = L.data ? L.data->bd.colliderId : 0;
        const int laneId = movers[i].laneId;
        const bool trafficBody = movers[i].kind == SceneMover::TrafficBodyMover && hooks.trafficImpact;
        const float w = 1.0f / float(impacts.size());
        for (const Impact& im : impacts)
            resolveOneImpact(im, s.body, &L.rb, w, [&](int side, const vec3& J, const vec3& push, const Impact& ri) {
                if (side == 0) {
                    notePush(movers[i], push, id);
                    if (s.car) s.car->damageImpact(J, id, L.rb.m, ri.P, ri.n, snd);
                    else if (trafficBody) hooks.trafficImpact(laneId, J, snd);
                } else {
                    L.notePush(push, myIdentity);
                }
            });
        if (s.car) s.car->notifyImpact();
    } else {
        resolveWithTrace(movers[i], impacts, true, kFixed, 1, world.traceId(oi));
    }
}

void Scene::runSample(std::vector<SceneMover>& movers, CollisionWorld& world, float dt, float invdt, SceneHooks& hooks) {
    sample(movers, world, dt, invdt, hooks, false);
}

void Scene::collideOnly(std::vector<SceneMover>& movers, CollisionWorld& world, float dt, float invdt, SceneHooks& hooks) {
    sample(movers, world, dt, invdt, hooks, true);
}

void Scene::sample(std::vector<SceneMover>& movers, CollisionWorld& world, float dt, float invdt, SceneHooks& hooks,
                   bool carsDone) {
    world_ = &world;
    hooks_ = &hooks;
    // New physical traffic cars and loose bodies are appended during the sample: room enough that
    // the list never moves while a test holds a reference into it.
    movers.reserve(movers.size() + 96);
    world.setLooseMovers(&movers);
    const size_t n0 = movers.size();
    for (SceneMover& m : movers) m.appendedThisSample = false;
    // 1. Update.
    for (size_t i = 0; i < n0; ++i) {
        SceneMover& m = movers[i];
        if (!m.U) continue;
        if (m.kind == SceneMover::CarMover) {
            if (!carsDone) m.car->stepUpdate(dt, invdt, world);
        } else if (m.kind == SceneMover::TrafficBodyMover) {
            m.tp->update(*m.ttype, dt, invdt, world);
            if (hooks.findRoom) m.tp->room = hooks.findRoom(centreOf(m), m.tp->room);
        } else if (m.kind == SceneMover::LooseMover && m.loose >= 0) {
            world.looseUpdate(m.loose, dt, invdt);
        } else if (m.kind == SceneMover::TrailerMover) {
            m.trailer->update(dt, invdt);
        }
    }
    // 2. Gather. Per room (the mover's room, then the rooms it touches): the room's one list, the
    // traffic cars without a body and the objects interleaved by filing time, newest first
    // (collision-objects.md, "Room lists"; loose-bodies.md 5.4); at most 32. The flat world: lane
    // cars in run order.
    std::vector<LaneCar> lanes;
    if (hooks.laneCars) hooks.laneCars(lanes);
    const bool flat = world.isFlat();
    struct Gathered {
        bool lane = false;
        int idx = 0;
    };
    std::vector<std::vector<Gathered>> gathered(n0);
    std::vector<size_t> laneOrder(lanes.size());
    for (size_t k = 0; k < lanes.size(); ++k) laneOrder[k] = k;
    if (!flat)
        std::stable_sort(laneOrder.begin(), laneOrder.end(),
                         [&](size_t a, size_t b) { return lanes[a].filed > lanes[b].filed; });
    for (size_t i = 0; i < n0; ++i) {
        SceneMover& m = movers[i];
        if (!(m.C || m.O)) continue;
        const vec3 ref = centreOf(m);
        const float r = radiusOf(m);
        const int room = roomOf(m);
        const bool wantLanes = m.O || m.V;
        auto laneHit = [&](size_t k) {
            const LaneCar& lc = lanes[k];
            if (m.kind != SceneMover::CarMover && m.kind != SceneMover::LooseMover && m.kind != SceneMover::TrailerMover &&
                m.laneId == lc.id)
                return false;
            const vec3 c = vadd(lc.M.o, lc.M.Y);
            const vec3 d = vsub(c, ref);
            const float sum = lc.type->bodyRadius + r;
            return (d.z * d.z + d.y * d.y) + d.x * d.x <= sum * sum;
        };
        size_t count = 0;
        if (flat) {
            if (wantLanes)
                for (size_t k = 0; k < lanes.size() && count < 32; ++k)
                    if (laneHit(k)) {
                        gathered[i].push_back({true, int(k)});
                        ++count;
                    }
            continue;
        }
        if (room <= 0 || !world.city()) continue;
        std::vector<int> rooms{room};
        for (int x : world.city()->touched(room, ref, r, 8)) rooms.push_back(x);
        std::vector<int> objs;
        // car-collision.md 1.2 step 2: landmarks and spanning objects with any of C, O, V; props,
        // moved props lying still and lane cars with O or V.
        const bool statics = m.C || m.O || m.V;
        const bool props = m.O || m.V;
        for (int rr : rooms) {
            // One list per room (loose-bodies.md 5.4): the lane cars and the room's objects in
            // filing order, newest first (a lane car before an object filed at the same time).
            std::vector<size_t> roomLanes;
            if (wantLanes)
                for (size_t k : laneOrder)
                    if (lanes[k].room == rr) roomLanes.push_back(k);
            const std::vector<int>& list = world.roomList(rr);
            size_t a = 0, b = 0;
            while (count < 32 && (a < roomLanes.size() || b < list.size())) {
                const bool lane = a < roomLanes.size() &&
                                  (b >= list.size() || lanes[roomLanes[a]].filed >= world.object(list[b]).filed);
                if (lane) {
                    const size_t k = roomLanes[a++];
                    if (!laneHit(k)) continue;
                    bool dup = false;
                    for (const Gathered& g : gathered[i])
                        if (g.lane && g.idx == int(k)) dup = true;
                    if (dup) continue;
                    gathered[i].push_back({true, int(k)});
                    ++count;
                } else {
                    const int oi = list[b++];
                    bool dup = false;
                    for (int x : objs)
                        if (x == oi) dup = true;
                    if (dup || !world.gatherTest(oi, ref, r, statics, props)) continue;
                    objs.push_back(oi);
                    gathered[i].push_back({false, oi});
                    ++count;
                }
            }
        }
    }
    // 3. Collide.
    for (size_t i = 0; i < n0; ++i) {
        // (movers may grow: index, never hold a reference across a test)
        if (movers[i].C) {
            Side s = sideOf(movers[i]);
            if (s.bound) {
                Mtx M0 = s.M0;
                if (s.body && s.prevSource == kCity) M0.o = vadd(M0.o, s.lastPush);
                std::vector<Impact> impacts;
                world.cityImpacts(*s.bound, s.M, M0, s.material, roomOf(movers[i]), dt, impacts);
                if (!impacts.empty()) {
                    SceneMover& m = movers[i];
                    if (m.kind == SceneMover::LaneMover && hooks.makePhysical) {
                        TrafficPhys* tp = hooks.makePhysical(m.laneId, m.laneM, m.laneSpeed);
                        if (tp) {
                            m.kind = SceneMover::TrafficBodyMover;
                            m.tp = tp;
                            m.appendedThisSample = true;
                            m.identity = tp->identity;
                        }
                    }
                    resolveWithTrace(movers[i], impacts, false, kCity, 0, -1);
                }
            }
        }
        if (movers[i].V)
            for (size_t j = i + 1; j < n0; ++j) {
                if (!movers[j].V || !movers[i].V) continue;
                // A truck and its own trailer only once the joint has come off (trailer.md 6.1).
                if (jointedPair(movers[i], movers[j])) continue;
                const vec3 ca = centreOf(movers[i]), cb = centreOf(movers[j]);
                const float ra = radiusOf(movers[i]), rb = radiusOf(movers[j]);
                const vec3 d = vsub(ca, cb);
                if (!((d.z * d.z + d.y * d.y) + d.x * d.x <= (rb + ra) * (rb + ra))) continue;
                pairTest(movers[i], movers[j], dt, hooks, movers, world, false, nullptr);
            }
        if (movers[i].C || movers[i].O) {
            for (const Gathered& g : gathered[i]) {
                if (!(movers[i].C || movers[i].O)) break;  // let go during the tests
                if (g.lane) {
                    const LaneCar& lc = lanes[size_t(g.idx)];
                    // Made physical earlier in this sample: tested as a body now.
                    int bodyIdx = -1;
                    for (size_t q = 0; q < movers.size(); ++q)
                        if (movers[q].kind == SceneMover::TrafficBodyMover && movers[q].laneId == lc.id) bodyIdx = int(q);
                    if (bodyIdx >= 0) {
                        if (size_t(bodyIdx) == i) continue;
                        pairTest(movers[i], movers[size_t(bodyIdx)], dt, hooks, movers, world, false, nullptr);
                    } else {
                        pairTest(movers[i], movers[i], dt, hooks, movers, world, true, &lc);
                    }
                    continue;
                }
                objectTest(movers, i, g.idx, dt, hooks);
            }
        }
    }
    // 4-5. End of sample (appended movers take part, then get all switches).
    for (SceneMover& m : movers) {
        if (!(m.U || m.appendedThisSample)) continue;
        if (m.kind == SceneMover::CarMover) {
            if (!carsDone) m.car->stepEnd();
        } else if (m.kind == SceneMover::TrafficBodyMover) {
            m.tp->endSample();
        } else if (m.kind == SceneMover::TrailerMover) {
            m.trailer->endSample();
        } else if (m.kind == SceneMover::LooseMover) {
            if (m.loose < 0) continue;
            world.looseEnd(m.loose);
        }
        if (m.appendedThisSample) {
            m.U = m.C = m.V = m.O = true;
            m.appendedThisSample = false;
        }
    }
    world.setLooseMovers(nullptr);
}

}  // namespace phys
}  // namespace cc
