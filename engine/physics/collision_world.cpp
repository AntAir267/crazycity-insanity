#include "physics/collision_world.h"
#include "physics/scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

#include "city/city_data.h"
#include "city/level_objects.h"
#include "core/bytes.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/banger.h"
#include "formats/dat_text.h"
#include "formats/pkg.h"
#include "physics/car.h"
#include "physics/contacts_internal.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

namespace {
constexpr int kCityIdentity = CollisionWorld::kCityIdentity;
constexpr int kFixedIdentity = CollisionWorld::kFixedIdentity;
constexpr int kPropIdentityBase = CollisionWorld::kPropIdentityBase;

Mtx fromMat4(const mat4& m) {
    return mtxFromRows(vec3(m.m[0], m.m[1], m.m[2]), vec3(m.m[4], m.m[5], m.m[6]), vec3(m.m[8], m.m[9], m.m[10]),
                       vec3(m.m[12], m.m[13], m.m[14]));
}

vec3 norm0(const vec3& v) {
    const float l = pdot(v, v);
    return l == 0.0f ? vec3(0, 0, 0) : vscale(v, 1.0f / fsqrt(l));
}

using Banger = BangerData;
bool loadBanger(const Vfs& vfs, const std::string& name, Banger& b) { return loadBangerData(vfs, name, b); }

// One-sided segment test (collision-objects.md) against a bound polygon; limit = best so far.
bool oneSided(const Bound& b, int pi, const vec3& S, const vec3& E, float limit, float& T, float& pen) {
    const BoundPoly& p = b.polys[size_t(pi)];
    const vec3& c0 = b.points[size_t(p.idx[0])];
    const float dS = ci::dotZXY(vsub(S, c0), p.normal);
    if (!(dS >= 0.0f)) return false;  // negative or NaN: no hit
    const float dE = ci::dotZXY(vsub(E, c0), p.normal);
    if (dE >= 0.0f) return false;
    const float t = dS / (dS - dE);
    if (t > limit) return false;
    const vec3 D = vsub(E, S);
    for (int j = 0; j < p.n; ++j) {
        const vec3& cj = b.points[size_t(p.idx[j])];
        const vec3& cn = b.points[size_t(p.idx[(j + 1) % p.n])];
        const vec3 g = pcross(vsub(cn, cj), D);
        const float s = ci::dotZYX(g, vsub(S, cj));
        if (s != 0.0f && ci::dotZYX(g, p.edgeNormal[j]) > 0.0f && s < 0.0f) return false;
    }
    T = t;
    pen = -dE;
    return true;
}
}  // namespace

SurfaceMaterial CollisionWorld::surfaceOf(const Material& m) const {
    SurfaceMaterial s;
    s.friction = m.friction;
    s.drag = m.drag;
    s.height = m.height;
    s.width = m.width;
    s.depth = m.depth;
    s.elasticity = m.elasticity;
    s.ptxIndex[0] = m.ptxIndex[0];
    s.ptxIndex[1] = m.ptxIndex[1];
    s.ptxThreshold[0] = m.ptxThreshold[0];
    s.ptxThreshold[1] = m.ptxThreshold[1];
    s.water = m.name == "water";
    s.sound = m.sound;
    return s;
}

void CollisionWorld::setupFlat(const Vfs& vfs, float h) {
    flat_ = true;
    flatY_ = h;
    city_ = nullptr;
    table_.reset();
    BytesPtr mtl = vfs.open("city/materials.mtl");
    if (mtl) table_.loadMtl(std::string(mtl->begin(), mtl->end()));
    const int cobble = table_.find("cobblestone");
    flatMaterial_ = cobble >= 0 ? table_.at(cobble) : defaultMaterial();
    flatCode_ = cobble >= 0 ? cobble + 1 : 1;  // the default material prints 1
    coll_.clear();
    objs_.clear();
    roomObjects_.clear();
    roomObjectsAtReset_.clear();
    broken_.clear();
    brokenFlags_.clear();
    vfs_ = &vfs;
    firstSlot_ = 0;
    firstGizmo_ = kSlotCount;
    gizObjs_.clear();
    gizmoOf_.clear();
    levelGizmos_ = 0;
    pieces_.clear();
    resetLoose();
}

void CollisionWorld::setupCity(const Vfs& vfs, const CityData& city, bool streetProps, bool levelObjects) {
    flat_ = false;
    city_ = &city;
    vfs_ = &vfs;
    table_.reset();
    BytesPtr mtl = vfs.open("city/materials.mtl");
    if (mtl) table_.loadMtl(std::string(mtl->begin(), mtl->end()));
    coll_.build(city, vfs, table_);
    waterLevel_ = city.waterLevel();
    broken_.clear();
    brokenFlags_.assign(city.objects().size(), 0);
    buildObjects(vfs, city, streetProps, levelObjects);
    firstSlot_ = int(objs_.size());
    firstGizmo_ = firstSlot_ + kSlotCount;
    gizObjs_.clear();
    gizmoOf_.clear();
    levelGizmos_ = 0;
    if (levelObjects) buildGizmos(vfs, city);
    roomObjectsAtReset_ = roomObjects_;
    pieces_.clear();
    resetLoose();
}

void CollisionWorld::resetProps() {
    for (CollisionObject& o : objs_) o.broken = false;
    broken_.clear();
    std::fill(brokenFlags_.begin(), brokenFlags_.end(), uint8_t(0));
    resetLoose();
}

void CollisionWorld::buildObjects(const Vfs& vfs, const CityData& city, bool streetProps, bool levelObjects) {
    objs_.clear();
    roomObjects_.assign(city.rooms().size(), {});
    std::map<std::string, std::shared_ptr<Bound>> geomCache;
    std::map<std::string, std::shared_ptr<Bound>> propBoundCache;
    std::map<std::string, Banger> bangerCache;
    std::map<std::string, std::shared_ptr<LooseData>> looseCache;
    const auto& list = city.objects();
    // Object ids (drive-trace.md): the .inst objects and poles first, then the street props, then
    // the props.pathset props, each in creation order (then the parked cars, never in check runs).
    std::vector<size_t> order;
    for (int pass = 0; pass < 4; ++pass) {
        if ((pass == 1 || pass == 2) && !streetProps) continue;
        if (pass == 3 && !levelObjects) continue;
        std::vector<size_t> part;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].origin == pass) part.push_back(i);
        if (pass > 0)
            std::stable_sort(part.begin(), part.end(), [&](size_t a, size_t b) { return list[a].created < list[b].created; });
        order.insert(order.end(), part.begin(), part.end());
    }
    for (size_t i : order) {
        const CityObject& co = list[i];
        CollisionObject o;
        o.cityObject = int(i);
        o.identity = kFixedIdentity;
        if (co.kind == CityObjectKind::Landmark) {
            o.kind = ObjKind::Landmark;
            o.O = fromMat4(co.world);
            std::shared_ptr<Bound> b;
            auto it = geomCache.find(co.name);
            if (it != geomCache.end()) {
                b = it->second;
            } else {
                b = loadTerrainBound(vfs, co.name, table_);
                if (!b) b = loadGeometryBound(vfs, co.name, table_);
                geomCache[co.name] = b;
            }
            if (!b) continue;
            o.bound = b;
            // Only the box's min and max corners (collision-objects.md, "Landmarks").
            const float lmin = (b->bmin.x * b->bmin.x + b->bmin.y * b->bmin.y) + b->bmin.z * b->bmin.z;
            const float lmax = (b->bmax.x * b->bmax.x + b->bmax.y * b->bmax.y) + b->bmax.z * b->bmax.z;
            const float D = lmin > lmax ? fsqrt(lmin) : fsqrt(lmax);
            o.radius = D > co.modelRadius ? D : co.modelRadius;
            o.position = o.O.o;
            o.ground = !co.noWheels;  // header bit 26: wheels do not probe it (inst.md)
            const int idx = int(objs_.size());
            objs_.push_back(o);
            if (co.room > 0 && size_t(co.room) < roomObjects_.size()) roomObjects_[size_t(co.room)].push_back(idx);
        } else if (co.kind == CityObjectKind::Spanning) {
            o.kind = ObjKind::Spanning;
            Mtx m = fromMat4(co.placement);
            const float lx = pdot(m.X, m.X), ly = pdot(m.Y, m.Y), lz = pdot(m.Z, m.Z);
            std::shared_ptr<Bound> b = loadGeometryBound(vfs, co.name, table_);
            if (!b) continue;
            if (std::fabs(lx - 1.0f) >= 0.01f || std::fabs(ly - 1.0f) >= 0.01f || std::fabs(lz - 1.0f) >= 0.01f) {
                auto sb = std::make_shared<Bound>(*b);
                const float sx = fsqrt(lx), sy = fsqrt(ly), sz = fsqrt(lz);
                for (vec3& p : sb->points) p = vec3(p.x * sx, p.y * sy, p.z * sz);
                sb->edges.clear();
                deriveBound(*sb);
                b = sb;
                // X' = normalise(Y x Z) with L = (z² + y²) + x²; Y', Z' with (x² + y²) + z².
                const vec3 xc = pcross(m.Y, m.Z);
                const float lX = (xc.z * xc.z + xc.y * xc.y) + xc.x * xc.x;
                const vec3 X2 = lX == 0.0f ? vec3(0, 0, 0) : vscale(xc, 1.0f / fsqrt(lX));
                const vec3 Y2 = norm0(pcross(m.Z, X2));
                const vec3 Z2 = norm0(m.Z);
                m = mtxFromRows(X2, Y2, Z2, m.o);
            }
            o.O = m;
            o.bound = b;
            o.radius = co.modelRadius;
            o.position = m.o;
            o.ground = true;
            const int idx = int(objs_.size());
            objs_.push_back(o);
            for (int r : city.touched(co.room, m.o, co.modelRadius, 32))
                if (r > 0 && size_t(r) < roomObjects_.size()) roomObjects_[size_t(r)].push_back(idx);
        } else if (co.kind == CityObjectKind::Breakable) {
            o.kind = ObjKind::Prop;
            auto bit = bangerCache.find(co.name);
            if (bit == bangerCache.end()) {
                Banger bd;
                if (!loadBanger(vfs, co.name, bd)) continue;
                bit = bangerCache.emplace(co.name, bd).first;
            }
            const Banger& bd = bit->second;
            Mtx m = fromMat4(co.world);  // the stored matrix (turn-only for compact and xref props)
            o.placementPos = m.o;
            // Position = placement + R·CG with R the FULL matrix the prop is placed with (an xref
            // prop's combined matrix C before only its turn is kept): (CG.z·Z + CG.y·Y) + CG.x·X.
            const Mtx F = fromMat4(co.placement);
            const vec3& g = bd.cg;
            m.o = vec3(m.o.x + ((g.z * F.Z.x + g.y * F.Y.x) + g.x * F.X.x), m.o.y + ((g.z * F.Z.y + g.y * F.Y.y) + g.x * F.X.y),
                       m.o.z + ((g.z * F.Z.z + g.y * F.Y.z) + g.x * F.X.z));
            o.O = m;
            o.position = m.o;
            o.radius = co.modelRadius;
            o.size = bd.size;
            o.cg = bd.cg;
            o.yRadius = bd.yRadius;
            o.impulseLimit2 = bd.impulseLimit2;
            o.collisionType = bd.collisionType;
            o.ground = (bd.collisionType & 0x20) != 0;
            o.material = Material();
            o.material.elasticity = bd.elasticity;
            o.material.friction = bd.friction;
            o.identity = kPropIdentityBase + int(objs_.size());
            const int prim = bd.collisionPrim;
            if (prim == 2) {
                o.prim = PropPrim::Capsule;
            } else if (prim == 3) {
                o.prim = PropPrim::Sphere;
            } else {
                std::shared_ptr<Bound> b;
                auto pb = propBoundCache.find(co.name);
                if (pb != propBoundCache.end()) {
                    b = pb->second;
                } else {
                    if (prim == 0) {
                        MaterialTable scratch;
                        auto gb = loadGeometryBound(vfs, co.name, scratch);
                        if (gb) {
                            auto mb = std::make_shared<Bound>(*gb);
                            for (vec3& p : mb->points) p = vec3(p.x + (-bd.cg.x), p.y + (-bd.cg.y), p.z + (-bd.cg.z));
                            mb->computeBox();
                            b = mb;
                        }
                    }
                    if (!b) b = std::make_shared<Bound>(makeBoxBound(bd.size));
                    propBoundCache[co.name] = b;
                }
                o.bound = b;
                o.prim = (prim == 0 && !b->isBox) ? PropPrim::Geometry : PropPrim::Box;
            }
            // What its test body and its moved copy are made of (loose-bodies.md 2.2, 3).
            auto ld = looseCache.find(co.name);
            if (ld == looseCache.end()) {
                auto d = std::make_shared<LooseData>();
                d->name = co.name;
                d->bd = bd;
                looseBoundFor(*d, o.prim == PropPrim::Geometry ? o.bound : nullptr);
                d->radius = co.modelRadius;
                d->pkg = co.pkg;
                for (int l = 0; l < 4; ++l) d->lod[l] = co.lod[l];
                ld = looseCache.emplace(co.name, d).first;
            }
            o.loose = ld->second;
            o.room = co.room;
            o.set = co.set;
            const int idx = int(objs_.size());
            objs_.push_back(o);
            if (co.room > 0 && size_t(co.room) < roomObjects_.size()) roomObjects_[size_t(co.room)].push_back(idx);
        }
    }
    // Room lists (collision-objects.md, "Room lists"): every other object (props, light poles) is
    // filed at the very front whenever it is filed into a room, static objects at the front of the
    // back part; so each part runs newest first: poles, then props, each in reverse creation order,
    // then the static objects in reverse load order. A race reset re-files the untouched props and
    // poles in creation order, which gives the same order again.
    for (auto& rl : roomObjects_) {
        std::vector<int> front, back;
        for (int oi : rl) (objs_[size_t(oi)].kind == ObjKind::Prop ? front : back).push_back(oi);
        auto newestFirst = [&](int a, int b) {
            return list[size_t(objs_[size_t(a)].cityObject)].created > list[size_t(objs_[size_t(b)].cityObject)].created;
        };
        std::stable_sort(front.begin(), front.end(), newestFirst);
        std::reverse(back.begin(), back.end());
        rl = front;
        rl.insert(rl.end(), back.begin(), back.end());
    }
    int lm = 0, sp = 0, pr = 0;
    for (const auto& o : objs_) {
        if (o.kind == ObjKind::Landmark) ++lm;
        else if (o.kind == ObjKind::Spanning) ++sp;
        else ++pr;
    }
    logInfo("collision: %zu city polygons, %d landmarks, %d spanning objects, %d props", coll_.polygonCount(), lm, sp,
            pr);
}

bool CollisionWorld::probeObject(const CollisionObject& o, const vec3& S, const vec3& E, float& best, ProbeHit& hit) {
    if (!o.bound) return false;
    // 2. Reject test (collision-objects.md, "Probing an object"), IEEE division without traps.
    const vec3& org = o.O.o;
    float lambda;
    if (S.x == E.x && S.z == E.z) {
        lambda = 1.0f / std::fabs(S.y - E.y);
    } else {
        const vec3 d = vsub(S, E);
        const float L2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
        lambda = L2 == 0.0f ? 0.0f : 1.0f / fsqrt(L2);
    }
    const vec3 mid((S.x + E.x) * 0.5f, (S.y + E.y) * 0.5f, (S.z + E.z) * 0.5f);
    const vec3 dm = vsub(mid, org);
    const float q = 0.5f / lambda + o.radius;
    if (!((dm.z * dm.z + dm.y * dm.y) + dm.x * dm.x <= q * q)) return false;  // a NaN also rejects
    // 3. Into object coordinates.
    auto into = [&](const vec3& P) {
        const vec3 w = vsub(P, org);
        return vec3(ci::dotZYX(w, o.O.X), ci::dotZYX(w, o.O.Y), ci::dotZYX(w, o.O.Z));
    };
    const vec3 Sl = into(S), El = into(E);
    const Bound& b = *o.bound;
    int bestPoly = -1;
    float bestT = best, bestPen = 0;
    auto tryPoly = [&](int pi) {
        float T, pen;
        if (oneSided(b, pi, Sl, El, bestT, T, pen)) {
            bestT = T;
            bestPen = pen;
            bestPoly = pi;
        }
    };
    if (b.terrain) {
        // polygons of the segment's cells (terrain grid), each once
        const TerrainGrid& g = b.grid;
        std::vector<int> cells;
        terrainCellsOfSegment(b, Sl, El, cells);
        std::vector<char> seen(b.polys.size(), 0);
        for (int c : cells)
            for (int r = 0; r < g.count[size_t(c)]; ++r) {
                const size_t ri = size_t(g.first[size_t(c)] + r);
                if (ri >= g.refs.size()) break;
                const int pi = g.refs[ri];
                if (seen[size_t(pi)]) continue;
                seen[size_t(pi)] = 1;
                tryPoly(pi);
            }
    } else {
        for (size_t pi = b.polys.size(); pi-- > 0;) tryPoly(int(pi));
    }
    if (bestPoly < 0) return false;
    best = bestT;
    // 5. Hit point and normal back to world.
    const vec3 pl((El.x - Sl.x) * bestT + Sl.x, (El.y - Sl.y) * bestT + Sl.y, (El.z - Sl.z) * bestT + Sl.z);
    const Mtx& M = o.O;
    auto back = [&](const vec3& v) {
        return vec3((v.x * M.X.x + v.z * M.Z.x) + v.y * M.Y.x, (v.x * M.X.y + v.z * M.Z.y) + v.y * M.Y.y,
                    (v.z * M.Z.z + v.y * M.Y.z) + v.x * M.X.z);
    };
    const vec3 pw = back(pl);
    hit.point = vec3(pw.x + org.x, pw.y + org.y, pw.z + org.z);
    hit.normal = back(b.polys[size_t(bestPoly)].normal);
    hit.t = bestT;
    hit.penetration = bestPen;
    hit.front = true;
    Material m;
    if (o.kind == ObjKind::Prop || o.kind == ObjKind::Gizmo) {
        m = o.material;
        hit.materialCode = 0;  // a prop's own material is not in the table (drive-trace.md)
    } else {
        const int mi = b.polys[size_t(bestPoly)].material;
        const int gi = (mi >= 0 && size_t(mi) < b.materials.size()) ? b.materials[size_t(mi)] : b.firstMaterial();
        m = table_.at(gi);
        hit.materialCode = gi + 1;  // list entry gi has code gi + 1
    }
    hit.material = surfaceOf(m);
    hit.hasMaterial = true;
    return true;
}

bool CollisionWorld::probe(const vec3& S, const vec3& E, ProbeState& st, ProbeHit& hit) {
    if (flat_) {
        // One endless horizontal polygon at y = h (collision.md, "Flat world for check runs";
        // vehicle-wheels.md, "Flat-world query"): a hit iff the sign bits of dS and dE differ.
        const float dS = S.y - flatY_, dE = E.y - flatY_;
        if (ci::sbit(dS) == ci::sbit(dE)) return false;
        const float t = dS / (dS - dE);
        hit.t = t;
        hit.point = vec3(S.x + t * (E.x - S.x), S.y + t * (E.y - S.y), S.z + t * (E.z - S.z));
        hit.normal = vec3(0.0f, 1.0f, 0.0f);
        hit.front = dS > 0.0f;
        hit.penetration = dS <= 0.0f ? -dS : -dE;
        hit.material = surfaceOf(flatMaterial_);
        hit.hasMaterial = true;
        hit.materialCode = flatCode_;
        hit.object = -1;
        return true;
    }
    const CityData& city = *city_;
    st.roomS = city.findRoom(S, st.roomS);
    st.roomE = city.findRoom(E, st.roomE);
    float best = 2.0f;
    bool haveCity = false, fromCache = false;
    PolyView bestPoly;
    float bestDS = 0, bestDE = 0;
    int bestMaterialCode = 0;
    const vec3 D = vsub(E, S);
    auto testPoly = [&](const PolyView& p) -> bool {
        const float dS = ci::dotYXZ(vsub(S, p.c[0]), p.normal);
        const float dE = ci::dotYXZ(vsub(E, p.c[0]), p.normal);
        if (ci::sbit(dS) == ci::sbit(dE)) return false;
        const float T = dS / (dS - dE);
        if (T > best) return false;
        for (int k = 0; k < p.n; ++k) {
            const vec3 g = pcross(vsub(p.c[(k + 1) % p.n], p.c[k]), D);
            const float s = ci::dotZYX(g, vsub(S, p.c[k]));
            if (s != 0.0f && ci::sbit(s) != ci::sbit(ci::dotZYX(g, p.edgeN[k]))) return false;
        }
        best = T;
        bestDS = dS;
        bestDE = dE;
        return true;
    };
    const auto& rooms = city.rooms();
    auto bit80 = [&](int r) { return r > 0 && size_t(r) < rooms.size() && (rooms[size_t(r)].flags & 0x80); };
    if (bit80(st.roomS) || bit80(st.roomE)) st.cacheValid = false;
    if (st.cacheValid) {
        PolyView c;
        c.n = st.cacheN;
        for (int k = 0; k < c.n; ++k) {
            c.c[k] = st.cache[k];
            c.edgeN[k] = st.cacheEdgeN[k];
        }
        c.normal = st.cacheNormal;
        if (testPoly(c)) {
            haveCity = true;
            fromCache = true;
            bestPoly = c;
        } else {
            st.cacheValid = false;
        }
    }
    if (!fromCache) {
        // Culling sphere (collision.md step 3): centre (E − S)·0.5 + S, radius (1/λ)·0.51 with λ
        // the inverse length of "Probing an object".
        const vec3 mid((E.x - S.x) * 0.5f + S.x, (E.y - S.y) * 0.5f + S.y, (E.z - S.z) * 0.5f + S.z);
        float lambda;
        if (S.x == E.x && S.z == E.z) {
            lambda = 1.0f / std::fabs(S.y - E.y);
        } else {
            const vec3 d = vsub(S, E);
            const float L2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
            lambda = L2 == 0.0f ? 0.0f : 1.0f / fsqrt(L2);
        }
        const float rad = (1.0f / lambda) * 0.51f;
        std::vector<int> qrooms;
        auto addRoom = [&](int r) {
            if (r <= 0) return;
            for (int x : qrooms)
                if (x == r) return;
            qrooms.push_back(r);
        };
        addRoom(st.roomS);
        if (st.roomE != st.roomS) addRoom(st.roomE);
        if (st.roomS > 0 && size_t(st.roomS) < city.psdl().rooms.size()) {
            int added = 0;
            for (uint16_t nb : city.psdl().rooms[size_t(st.roomS)].neighbours) {
                if (added >= 10) break;
                if (nb == 0 || nb == st.roomS || nb == st.roomE || !bit80(nb)) continue;
                bool dup = false;
                for (int x : qrooms)
                    if (x == nb) dup = true;
                if (dup) continue;
                qrooms.push_back(nb);
                ++added;
            }
        }
        for (int r : qrooms) {
            scratch_.clear();
            coll_.gather(r, mid, rad, scratch_, nullptr);
            for (const CollPoly* cp : scratch_) {
                PolyView p;
                p.n = cp->n;
                for (int k = 0; k < cp->n; ++k) {
                    p.c[k] = cp->c[k];
                    p.edgeN[k] = cp->edgeN[k];
                }
                p.normal = cp->normal;
                p.material = cp->material;
                if (testPoly(p)) {
                    haveCity = true;
                    bestPoly = p;
                    bestMaterialCode = cp->material;
                }
            }
        }
        if (haveCity) {
            st.cacheValid = true;
            st.cacheN = bestPoly.n;
            for (int k = 0; k < bestPoly.n; ++k) {
                st.cache[k] = bestPoly.c[k];
                st.cacheEdgeN[k] = bestPoly.edgeN[k];
            }
            st.cacheNormal = bestPoly.normal;
        }
    }
    bool have = false;
    if (haveCity) {
        have = true;
        hit.t = best;
        hit.point = vec3((E.x - S.x) * best + S.x, (E.y - S.y) * best + S.y, (E.z - S.z) * best + S.z);
        hit.normal = bestPoly.normal;
        hit.penetration = bestDS <= 0.0f ? -bestDS : -bestDE;
        hit.front = bestDS > 0.0f;
        if (fromCache) {
            hit.material.friction = st.lastFriction;
            hit.material.drag = st.lastDrag;
            hit.material.height = st.lastHeight;
            hit.material.width = st.lastWidth;
            hit.material.depth = st.lastDepth;
            hit.material.elasticity = st.lastElasticity;
            hit.material.ptxIndex[0] = st.lastPtxIndex[0];  // visual only
            hit.material.ptxIndex[1] = st.lastPtxIndex[1];
            hit.material.ptxThreshold[0] = st.lastPtxThreshold[0];
            hit.material.ptxThreshold[1] = st.lastPtxThreshold[1];
            hit.material.water = st.lastWater;
            hit.material.sound = st.lastSound;
            hit.hasMaterial = st.lastHasMaterial;
            hit.materialCode = st.lastCode;
            hit.object = st.lastObjectId;
        } else {
            hit.material = surfaceOf(table_.byCode(bestMaterialCode));
            hit.hasMaterial = true;
            hit.materialCode = bestMaterialCode == 0 ? 1 : bestMaterialCode;  // codes 0 and 1: the default
            hit.object = -1;
            st.lastObject = -1;
        }
    }
    // Placed objects in room S and room E, and the hard-coded extra rooms.
    auto objectsOf = [&](int r) {
        if (r <= 0 || size_t(r) >= roomObjects_.size()) return;
        for (int oi : roomObjects_[size_t(r)]) {
            const CollisionObject& o = object(oi);
            if (!o.ground || o.broken) continue;
            if (probeObject(o, S, E, best, hit)) {
                have = true;
                st.lastObject = oi;
                hit.object = oi;  // position among the collidable objects (drive-trace.md)
            }
        }
    };
    objectsOf(st.roomS);
    if (st.roomE != st.roomS) objectsOf(st.roomE);
    if (st.roomS > 0 && size_t(st.roomS) < rooms.size() && rooms[size_t(st.roomS)].extraRooms) {
        const int extra = st.roomS == 411 ? 102 : st.roomS == 412 ? 122 : st.roomS == 423 ? 96 : st.roomS == 625 ? 1 : 0;
        objectsOf(extra);
    }
    if (have) {
        st.lastFriction = hit.material.friction;
        st.lastDrag = hit.material.drag;
        st.lastHeight = hit.material.height;
        st.lastWidth = hit.material.width;
        st.lastDepth = hit.material.depth;
        st.lastElasticity = hit.material.elasticity;
        st.lastPtxIndex[0] = hit.material.ptxIndex[0];  // visual only
        st.lastPtxIndex[1] = hit.material.ptxIndex[1];
        st.lastPtxThreshold[0] = hit.material.ptxThreshold[0];
        st.lastPtxThreshold[1] = hit.material.ptxThreshold[1];
        st.lastWater = hit.material.water;
        st.lastSound = hit.material.sound;
        st.lastHasMaterial = hit.hasMaterial;
        st.lastCode = hit.materialCode;
        st.lastObjectId = hit.object;
    }
    return have;
}

bool CollisionWorld::probeStatic(const vec3& S, const vec3& E, ProbeHit& hit) {
    ProbeState st;
    return probe(S, E, st, hit);
}

void CollisionWorld::gather(const Car& car, const vec3& ref, float radius, int room, std::vector<int>& out) const {
    (void)car;
    gatherObjects(ref, radius, room, true, true, out);
}

int CollisionWorld::findRoomAt(const vec3& p, int hint) const { return flat_ || !city_ ? 0 : city_->findRoom(p, hint); }

void CollisionWorld::placeCar(Car& car) {
    CarCollisionState& cs = car.collision();
    cs.roomPending = false;
    const RigidBody& body = car.body();
    cs.room = flat_ ? 0 : city_->findRoom(vadd(body.p, body.R.r[1]), 0);
}

void CollisionWorld::afterCarUpdate(Car& car, float dt) {
    if (flat_) return;  // no water, no rooms
    CarCollisionState& cs = car.collision();
    const RigidBody& body = car.body();
    // Water effect (collision.md, "World limits"): switched on when the car's room is deep water
    // and the model origin is below the water level; while on, forces every sample.
    if (!cs.water && cs.room > 0 && size_t(cs.room) < city_->rooms().size() && city_->rooms()[size_t(cs.room)].deepWater &&
        car.bodyMatrix().o.y < waterLevel_) {
        cs.water = true;
        cs.waterLevel = waterLevel_;
    }
    car.waterForces(dt);
    // The car's room from its reference point (centre of mass + body Y axis).
    cs.room = city_->findRoom(vadd(body.p, body.R.r[1]), cs.room);
}

void CollisionWorld::cityImpacts(const Bound& bound, const Mtx& M, const Mtx& M0, const Material& mat, int room, float dt,
                                 std::vector<Impact>& impacts) {
    impacts.clear();
    std::vector<PolyView> polys;
    if (flat_) {
        PolyView p;
        p.n = 0;
        p.c[0] = vec3(0.0f, flatY_, 0.0f);
        p.normal = vec3(0.0f, 1.0f, 0.0f);
        p.material = 0;
        polys.push_back(p);
    } else if (room > 0) {
        std::vector<int> rooms{room};
        for (int r : city_->touched(room, M.o, bound.radius, 8)) rooms.push_back(r);
        int budget = 256;
        scratch_.clear();
        for (int r : rooms) coll_.gather(r, M.o, bound.radius, scratch_, &budget);
        polys.reserve(scratch_.size());
        for (const CollPoly* cp : scratch_) {
            PolyView p;
            p.n = cp->n;
            for (int k = 0; k < cp->n; ++k) {
                p.c[k] = cp->c[k];
                p.edgeN[k] = cp->edgeN[k];
            }
            p.normal = cp->normal;
            p.material = cp->material;
            polys.push_back(p);
        }
    }
    if (polys.empty()) return;
    if (flat_) {
        // The flat world's material is cobblestone, not a city code.
        MaterialTable t;
        cityContacts(bound, M, M0, dt, polys, mat, t, impacts);
        for (Impact& im : impacts) im.matA = flatMaterial_;
    } else {
        cityContacts(bound, M, M0, dt, polys, mat, table_, impacts);
    }
}

void CollisionWorld::gatherObjects(const vec3& ref, float radius, int room, bool statics, bool props,
                                   std::vector<int>& out) const {
    out.clear();
    if (!city_ || room <= 0) return;
    std::vector<int> rooms{room};
    for (int r : city_->touched(room, ref, radius, 8)) rooms.push_back(r);
    size_t count = 0;
    for (int r : rooms) gatherRoom(r, ref, radius, statics, props, out, count);
}

const std::vector<int>& CollisionWorld::roomList(int r) const {
    static const std::vector<int> none;
    if (r <= 0 || size_t(r) >= roomObjects_.size()) return none;
    return roomObjects_[size_t(r)];
}

void CollisionWorld::gatherRoom(int r, const vec3& ref, float radius, bool statics, bool props, std::vector<int>& out,
                                size_t& count) const {
    if (r <= 0 || size_t(r) >= roomObjects_.size()) return;
    for (int oi : roomObjects_[size_t(r)]) {
        if (count >= 32) return;
        bool dup = false;
        for (int x : out)
            if (x == oi) dup = true;
        if (dup) continue;
        if (gatherTest(oi, ref, radius, statics, props)) {
            out.push_back(oi);
            ++count;
        }
    }
}

bool CollisionWorld::gatherTest(int oi, const vec3& ref, float radius, bool statics, bool props) const {
    const CollisionObject& o = object(oi);
    if (o.broken) return false;
    if ((o.kind == ObjKind::Prop || o.kind == ObjKind::Moved) ? !props : !statics) return false;
    vec3 c = o.position;
    float rr = o.radius;
    if (o.kind == ObjKind::Prop && o.yRadius != 0.0f) {
        // Vertical cylinder about p − R·CG with the stored matrix's rows; Y = the mover's.
        rr = o.yRadius;
        const vec3& g = o.cg;
        const Mtx& R = o.O;
        c = vec3(o.position.x - ((g.z * R.Z.x + g.y * R.Y.x) + g.x * R.X.x), ref.y,
                 o.position.z - ((g.z * R.Z.z + g.y * R.Y.z) + g.x * R.X.z));
    }
    const vec3 d = vsub(c, ref);
    const float sum = rr + radius;
    return (d.z * d.z + d.y * d.y) + d.x * d.x <= sum * sum;  // a NaN does not collide
}

bool CollisionWorld::objectImpacts(int oi, const Bound& bound, const Mtx& M, const Mtx& M0, const Material& mat, float dt,
                                   std::vector<Impact>& impacts, const LooseData* capsule) {
    impacts.clear();
    CollisionObject& o = object(oi);
    if (o.broken) return false;
    // loose-bodies.md 6.3, "Capsules in these tests": a capsule against a polygon side uses the
    // capsule test with the polygon side in the car's role. The cases the page leaves open
    // (capsule against capsule, spheres, terrain landmarks) keep the box of 6.2.
    const LooseData* movedData = o.kind == ObjKind::Moved && o.slot >= 0 ? slots_[o.slot].data : nullptr;
    const bool movedCapsule = movedData && movedData->prim == 2;
    const bool movedRound = movedData && movedData->prim != 0;
    const bool objRound = (o.kind == ObjKind::Prop && o.prim != PropPrim::Box && o.prim != PropPrim::Geometry) || movedRound;
    if (capsule && capsule->prim == 2 && !objRound && o.bound && !o.bound->terrain) {
        // The mover is the capsule (H = M, H0 = M0); the object, fixed, takes the car's role.
        const Material objMat = (o.kind == ObjKind::Prop || o.kind == ObjKind::Moved || o.kind == ObjKind::Gizmo)
                                    ? o.material
                                    : table_.at(o.bound->firstMaterial());
        capsuleObjectContacts(*o.bound, o.O, o.O, capsule->bd.yRadius, capsule->bd.size.y, M, objMat, mat, impacts,
                              &M0);
        for (Impact& im : impacts) {  // A = the mover: the normal out of the object into it
            im.n = vec3(-im.n.x, -im.n.y, -im.n.z);
            im.matA = mat;
            im.matB = objMat;
        }
        return true;
    }
    if (movedCapsule && !(capsule && capsule->prim != 0)) {
        // A capsule copy lying still (H0 = H) against the mover's polygons.
        capsuleObjectContacts(bound, M, M0, movedData->bd.yRadius, movedData->bd.size.y, o.O, mat, o.material, impacts);
        return true;
    }
    if (o.kind == ObjKind::Prop && o.prim == PropPrim::Capsule) {
        capsuleObjectContacts(bound, M, M0, o.yRadius, o.size.y, o.O, mat, o.material, impacts);
    } else if (o.kind == ObjKind::Prop && o.prim == PropPrim::Sphere) {
        return false;  // no prop type uses a sphere against a car
    } else if (o.bound && o.bound->terrain) {
        terrainObjectContacts(bound, M, M0, *o.bound, o.O, dt, mat, table_, impacts);
    } else if (o.bound) {
        const Material objMat = (o.kind == ObjKind::Prop || o.kind == ObjKind::Moved || o.kind == ObjKind::Gizmo)
                                    ? o.material
                                    : table_.at(o.bound->firstMaterial());
        polygonalObjectContacts(bound, M, M0, *o.bound, o.O, dt, mat, objMat, impacts);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Moving objects: the level objects (level-objects.md) and dynamic props (cable-cars.md)

void CollisionWorld::fileAtFront(int oi, int room) {
    if (room > 0 && size_t(room) < roomObjects_.size()) {
        auto& rl = roomObjects_[size_t(room)];
        rl.insert(rl.begin(), oi);
        object(oi).filed = fileStamp();
    }
}

void CollisionWorld::unfile(int oi, int room) {
    if (room > 0 && size_t(room) < roomObjects_.size()) {
        auto& rl = roomObjects_[size_t(room)];
        rl.erase(std::remove(rl.begin(), rl.end(), oi), rl.end());
    }
}

void CollisionWorld::buildGizmos(const Vfs& vfs, const CityData& city) {
    const LevelObjects& lo = city.levelObjects();
    std::map<std::string, std::shared_ptr<LooseData>> cache;
    gizmoOf_.assign(lo.objects().size(), -1);
    for (size_t i = 0; i < lo.objects().size(); ++i) {
        const LevelObject& l = lo.objects()[i];
        if (!l.collides() || !l.hasData) continue;
        auto it = cache.find(l.model);
        if (it == cache.end()) {
            auto d = std::make_shared<LooseData>();
            d->name = l.model;
            d->bd = l.bd;
            std::shared_ptr<Bound> geom;
            if (l.bd.collisionPrim == 0) {
                MaterialTable scratch;
                if (auto gb = loadGeometryBound(vfs, l.model, scratch)) {
                    auto mb = std::make_shared<Bound>(*gb);
                    for (vec3& p : mb->points) p = vec3(p.x + (-l.bd.cg.x), p.y + (-l.bd.cg.y), p.z + (-l.bd.cg.z));
                    mb->computeBox();
                    geom = mb;
                }
            }
            looseBoundFor(*d, geom);
            it = cache.emplace(l.model, d).first;
        }
        CollisionObject o;
        o.kind = ObjKind::Gizmo;
        o.bound = it->second->bound;
        o.prim = it->second->box ? PropPrim::Box : PropPrim::Geometry;
        o.material = it->second->material;
        o.radius = l.radius;
        o.ground = true;  // wheels drive on them whatever their CollisionType
        o.identity = kFixedIdentity;
        o.collisionType = l.bd.collisionType;
        o.cg = l.bd.cg;
        o.size = l.bd.size;
        o.yRadius = l.bd.yRadius;              // read by the AI obstacle tests only (not gathering)
        o.impulseLimit2 = l.bd.impulseLimit2;  // ditto (a level object never breaks)
        o.O = mtxFromRows(l.X, l.Y, l.Z, l.o);
        o.position = l.o;
        o.room = l.room;
        gizmoOf_[i] = int(gizObjs_.size());
        gizObjs_.push_back(o);
    }
    levelGizmos_ = gizObjs_.size();
    // Filed behind the props (which a reset files in front of them), newest first.
    for (size_t g = 0; g < gizObjs_.size(); ++g) {
        const int room = gizObjs_[g].room;
        if (room <= 0 || size_t(room) >= roomObjects_.size()) continue;
        auto& rl = roomObjects_[size_t(room)];
        size_t at = 0;
        while (at < rl.size() && object(rl[at]).kind == ObjKind::Prop) ++at;
        rl.insert(rl.begin() + long(at), firstGizmo_ + int(g));
    }
    logInfo("collision: %zu moving level objects", levelGizmos_);
}

void CollisionWorld::syncLevelObjects(const LevelObjects& lo) {
    for (size_t i = 0; i < gizmoOf_.size() && i < lo.objects().size(); ++i) {
        const int g = gizmoOf_[i];
        if (g < 0) continue;
        const LevelObject& l = lo.objects()[i];
        CollisionObject& o = gizObjs_[size_t(g)];
        o.O = mtxFromRows(l.X, l.Y, l.Z, l.o);
        o.position = l.o;
        if (l.room != o.room) {
            unfile(firstGizmo_ + g, o.room);
            fileAtFront(firstGizmo_ + g, l.room);
            o.room = l.room;
        }
    }
}

void CollisionWorld::setPropDataScale(const std::string& name, float k) {
    // The loaded values are kept on the first call, so the scale is always from them.
    for (CollisionObject& o : objs_) {
        if (o.kind != ObjKind::Prop || !o.loose || !iequals(o.loose->name, name)) continue;
        const std::string key = toLower(name);
        auto it = propScaleBase_.find(key);
        if (it == propScaleBase_.end()) it = propScaleBase_.emplace(key, std::make_pair(o.impulseLimit2, o.loose->bd.mass)).first;
        o.impulseLimit2 = it->second.first * k;
        o.loose->bd.mass = it->second.second * k;
    }
}

int CollisionWorld::addDynamicProp(const std::shared_ptr<LooseData>& data, const Mtx& M, int room) {
    CollisionObject o;
    o.kind = ObjKind::Prop;
    o.bound = data->bound;
    o.prim = data->box ? PropPrim::Box : PropPrim::Geometry;
    o.material = data->material;
    o.radius = data->radius;
    o.yRadius = data->bd.yRadius;
    o.cg = data->bd.cg;
    o.size = data->bd.size;
    o.impulseLimit2 = data->bd.impulseLimit2;
    o.collisionType = data->bd.collisionType;
    o.ground = (data->bd.collisionType & 0x20) != 0;
    o.identity = kFixedIdentity;
    o.loose = data;
    o.O = M;
    o.position = M.o;
    o.room = room;
    const int oi = firstGizmo_ + int(gizObjs_.size());
    gizObjs_.push_back(o);
    fileAtFront(oi, room);
    return oi;
}

void CollisionWorld::moveDynamicProp(int oi, const Mtx& M, int room) {
    CollisionObject& o = object(oi);
    o.O = M;
    o.position = M.o;
    if (room != o.room) {
        if (!o.broken) {
            unfile(oi, o.room);
            fileAtFront(oi, room);
        }
        o.room = room;
    }
}

void CollisionWorld::clearDynamicProps() {
    for (size_t g = levelGizmos_; g < gizObjs_.size(); ++g) {
        const int oi = firstGizmo_ + int(g);
        for (int s = 0; s < kSlotCount; ++s)
            if (slots_[s].held && slots_[s].fromObject == oi) clearSlot(s);
        unfile(oi, gizObjs_[g].room);
    }
    gizObjs_.resize(levelGizmos_);
}

void CollisionWorld::restoreProp(int oi) {
    CollisionObject& o = object(oi);
    for (int s = 0; s < kSlotCount; ++s)
        if (slots_[s].held && slots_[s].fromObject == oi) clearSlot(s);
    if (!o.broken) return;
    o.broken = false;
    if (o.cityObject >= 0 && size_t(o.cityObject) < brokenFlags_.size()) brokenFlags_[size_t(o.cityObject)] = 0;
    unfile(oi, o.room);
    fileAtFront(oi, o.room);
}

// After a reset restored the room lists of setup time: the level objects that have changed room
// since go to the front of their room; the dynamic props stand again in their rooms.
void CollisionWorld::refileMoving() {
    for (size_t g = 0; g < gizObjs_.size(); ++g) {
        const int oi = firstGizmo_ + int(g);
        CollisionObject& o = gizObjs_[g];
        if (g < levelGizmos_) {
            bool there = false;
            if (o.room > 0 && size_t(o.room) < roomObjects_.size())
                for (int x : roomObjects_[size_t(o.room)])
                    if (x == oi) there = true;
            if (!there) {
                for (auto& rl : roomObjects_) rl.erase(std::remove(rl.begin(), rl.end(), oi), rl.end());
                fileAtFront(oi, o.room);
            }
        } else {
            o.broken = false;
            unfile(oi, o.room);
            fileAtFront(oi, o.room);
        }
    }
}

// Step 12 of the car's step when the car is the only mover (no frame of movers around it): the
// scene's tests for that one car (the car's own update and end are Car::step's).
void CollisionWorld::collideCar(Car& car, float dt, float invdt) {
    std::vector<SceneMover> movers(1);
    movers[0].kind = SceneMover::CarMover;
    movers[0].car = &car;
    movers[0].player = true;
    movers[0].number = 1;
    movers[0].identity = 10001;
    Scene scene;
    SceneHooks hooks;
    scene.collideOnly(movers, *this, dt, invdt, hooks);
}

}  // namespace phys
}  // namespace cc
