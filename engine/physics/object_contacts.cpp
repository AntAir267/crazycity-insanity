// The car against placed objects: polygonal bounds and terrain landmarks
// (spec/physics/collision-object-contacts.md sections 1-3).
#include <climits>
#include <cmath>

#include "physics/bound.h"
#include "physics/contacts.h"
#include "physics/contacts_internal.h"

namespace cc {
namespace phys {

using namespace ci;

namespace {

// Component access by axis letter.
inline float comp(const vec3& v, char a) { return a == 'x' ? v.x : (a == 'y' ? v.y : v.z); }
inline const vec3& row(const Mtx& m, char a) { return a == 'x' ? m.X : (a == 'y' ? m.Y : m.Z); }

// "to world" in a given order for each output component: (( t1 + t2 ) + t3) (+ o).
inline float worldComp(const Mtx& m, const vec3& p, char c, const char* ord, bool point) {
    float s = comp(row(m, ord[0]), c) * comp(p, ord[0]);
    s = s + comp(row(m, ord[1]), c) * comp(p, ord[1]);
    s = s + comp(row(m, ord[2]), c) * comp(p, ord[2]);
    return point ? s + comp(m.o, c) : s;
}
inline vec3 toWorld(const Mtx& m, const vec3& p, const char* ox, const char* oy, const char* oz, bool point = true) {
    return vec3(worldComp(m, p, 'x', ox, point), worldComp(m, p, 'y', oy, point), worldComp(m, p, 'z', oz, point));
}
// "into" (dot with row c) in a given order.
inline float intoComp(const vec3& A, const vec3& w, const char* ord) {
    float s = comp(A, ord[0]) * comp(w, ord[0]);
    s = s + comp(A, ord[1]) * comp(w, ord[1]);
    return s + comp(A, ord[2]) * comp(w, ord[2]);
}
inline vec3 into(const Mtx& m, const vec3& w, const char* ox, const char* oy, const char* oz) {
    return vec3(intoComp(m.X, w, ox), intoComp(m.Y, w, oy), intoComp(m.Z, w, oz));
}

// Transpose inverse (rows orthonormal by assumption).
Mtx tinv(const Mtx& a) {
    Mtx r;
    r.X = vec3(a.X.x, a.Y.x, a.Z.x);
    r.Y = vec3(a.X.y, a.Y.y, a.Z.y);
    r.Z = vec3(a.X.z, a.Y.z, a.Z.z);
    r.o = vec3(-((a.X.x * a.o.x + a.X.y * a.o.y) + a.X.z * a.o.z), -((a.Y.x * a.o.x + a.Y.y * a.o.y) + a.Y.z * a.o.z),
               -((a.Z.x * a.o.x + a.Z.y * a.o.y) + a.Z.z * a.o.z));
    return r;
}

// Matrix product A*B with the term orders of "Evaluation orders".
Mtx mprod(const Mtx& A, const Mtx& B) {
    const vec3* Br[3] = {&B.X, &B.Y, &B.Z};
    auto el = [&](const vec3& a, int col, const int* k) {
        float s = (&a.x)[k[0]] * (&Br[k[0]]->x)[col];
        s = s + (&a.x)[k[1]] * (&Br[k[1]]->x)[col];
        return s + (&a.x)[k[2]] * (&Br[k[2]]->x)[col];
    };
    static const int X0[3] = {0, 1, 2}, X1[3] = {1, 2, 0}, Y0[3] = {1, 0, 2}, Y1[3] = {2, 1, 0}, Z0[3] = {0, 2, 1};
    Mtx r;
    r.X = vec3(el(A.X, 0, X0), el(A.X, 1, X1), el(A.X, 2, X1));
    r.Y = vec3(el(A.Y, 0, Y0), el(A.Y, 1, Y1), el(A.Y, 2, Y1));
    r.Z = vec3(el(A.Z, 0, Z0), el(A.Z, 1, Y1), el(A.Z, 2, Y1));
    r.o = vec3(el(A.o, 0, Y0) + B.o.x, el(A.o, 1, Y1) + B.o.y, el(A.o, 2, Y1) + B.o.z);
    return r;
}

// Sphere test of segment P0 -> P1 against the sphere of radius r about the origin.
bool sphereTest(const vec3& P0, const vec3& P1, float r) {
    const float r2 = r * r;
    const vec3 d = vsub(P1, P0);
    const float s = (d.z * P0.z + d.y * P0.y) + d.x * P0.x;
    vec3 q;
    if (s < 0.0f) {
        const float L = (d.z * d.z + d.x * d.x) + d.y * d.y;
        if (-s < L) {
            const float p2 = dotZYX(P0, P0);
            return !(L * r2 < p2 * L - s * s);
        }
        q = P1;
    } else {
        q = P0;
    }
    return !(r2 < dotZYX(q, q));
}

PolyView polyOf(const Bound& b, int i) {
    PolyView v;
    const BoundPoly& p = b.polys[size_t(i)];
    v.n = p.n;
    for (int k = 0; k < p.n; ++k) {
        v.c[k] = b.points[size_t(p.idx[k])];
        v.edgeN[k] = p.edgeNormal[k];
        v.edgeNum[k] = p.edgeNum[k];
    }
    v.normal = p.normal;
    v.material = p.material;
    return v;
}

struct PassInput {
    const Bound* T;   // points and edges
    const Bound* Q;   // polygons
    Mtx cur, last;
    vec3 lT;
    float thr;
    Mtx Tworld;       // T's own world matrix (for world edge normals)
};

// 1.3 + 2.1 for one pass; records in Q's model coordinates.
void runPass(const PassInput& in, std::vector<Record>& out) {
    const Bound& T = *in.T;
    const Bound& Q = *in.Q;
    const size_t np = T.points.size(), ne = T.edges.size();
    std::vector<char> culled(np, 0), needPlane(np, 0);
    for (size_t k = 0; k < np; ++k) culled[k] = dotZYX(T.points[k], in.lT) < in.thr;
    std::vector<vec3> C(np), S(np);
    for (size_t k = 0; k < np; ++k) C[k] = toWorld(in.cur, T.points[k], "zxy", "xzy", "xzy");
    const float r = Q.radius;
    struct PC {
        int k;
        float bestT = 2.0f, depth = 0;
        int poly = -1;
    };
    std::vector<PC> pts;
    for (size_t k = np; k-- > 0;) {
        if (culled[k]) continue;
        S[k] = toWorld(in.last, T.points[k], "yzx", "xyz", "xyz");
        if (sphereTest(S[k], C[k], r)) pts.push_back(PC{int(k)});
    }
    struct EC {
        int e;
        float tMin = 2.0f, tMax = -1.0f, dMin = 0, dMax = 0;
        int pMin = -1, pMax = -1;
    };
    std::vector<EC> eds;
    for (size_t e = ne; e-- > 0;) {
        const int a = T.edges[e].first, b = T.edges[e].second;
        if (culled[size_t(a)] && culled[size_t(b)]) continue;
        if (!sphereTest(C[size_t(a)], C[size_t(b)], r)) continue;
        eds.push_back(EC{int(e)});
        if (culled[size_t(a)]) needPlane[size_t(a)] = 1;
        if (culled[size_t(b)]) needPlane[size_t(b)] = 1;
    }
    if (pts.empty() && eds.empty()) return;
    std::vector<float> d(np, 0.0f);
    for (size_t i = Q.polys.size(); i-- > 0;) {
        const PolyView poly = polyOf(Q, int(i));
        for (size_t k = 0; k < np; ++k)
            if (!culled[k] || needPlane[k]) d[k] = planeDist(poly, C[k]);
        for (PC& pc : pts) {
            const size_t k = size_t(pc.k);
            if (!(d[k] < 0.0f)) continue;
            const float d0 = planeDist(poly, S[k]);
            if (!(d0 > 0.0f)) continue;
            const float t = d0 / (d0 - d[k]);
            if (t < pc.bestT && lineInside1(poly, S[k], vsub(C[k], S[k]))) {
                pc.bestT = t;
                pc.poly = int(i);
                pc.depth = -d[k];
            }
        }
        for (EC& ec : eds) {
            const int a = T.edges[size_t(ec.e)].first, b = T.edges[size_t(ec.e)].second;
            const float da = d[size_t(a)], db = d[size_t(b)];
            if (sbit(da) == sbit(db)) continue;
            const float t = da / (da - db);
            // A NaN (t or tMin) compares true for < and <=, false for > and >= (2.1 step 3).
            if (!((t > ec.tMax || !(t >= ec.tMin)) && lineInside2(poly, C[size_t(a)], vsub(C[size_t(b)], C[size_t(a)]))))
                continue;
            if (!(t > ec.tMin) && (da > 0.0f || !(t >= ec.tMin))) {
                ec.tMin = t;
                ec.pMin = int(i);
                ec.dMin = da <= 0.0f ? da : -db;
            }
            if (t >= ec.tMax && (da < 0.0f || t > ec.tMax)) {
                ec.tMax = t;
                ec.pMax = int(i);
                ec.dMax = da <= 0.0f ? da : -db;
            }
        }
    }
    for (const PC& pc : pts) {
        if (pc.poly < 0) continue;
        const size_t k = size_t(pc.k);
        Record R;
        R.kind = RecCorner;
        R.start = S[k];
        R.end = C[k];
        R.T = pc.bestT;
        R.point = vec3((R.end.x - R.start.x) * R.T + R.start.x, (R.end.y - R.start.y) * R.T + R.start.y,
                       (R.end.z - R.start.z) * R.T + R.start.z);
        R.normal = Q.polys[size_t(pc.poly)].normal;
        R.polygon = pc.poly;
        R.material = Q.polys[size_t(pc.poly)].material;
        R.depth = pc.depth;
        R.front = true;
        R.number = pc.k;
        R.from = -1;
        R.to = pc.k;
        out.push_back(R);
    }
    auto edgeRec = [&](const EC& ec, float t, float dd, int poly) {
        Record R;
        R.kind = RecEdge;
        const int a = T.edges[size_t(ec.e)].first, b = T.edges[size_t(ec.e)].second;
        R.start = C[size_t(a)];
        R.end = C[size_t(b)];
        R.T = t;
        R.point = vec3((R.end.x - R.start.x) * t + R.start.x, (R.end.y - R.start.y) * t + R.start.y,
                       (R.end.z - R.start.z) * t + R.start.z);
        R.front = dd > 0.0f;
        R.depth = R.front ? dd : -dd;  // d if front, else −d (d = +0 gives −0)
        R.normal = Q.polys[size_t(poly)].normal;
        R.polygon = poly;
        R.material = Q.polys[size_t(poly)].material;
        R.number = ec.e;
        R.from = a;
        R.to = b;
        R.wen = toWorld(in.Tworld, T.edgeNormal[size_t(ec.e)], "zyx", "zyx", "zyx", false);
        R.edgeCos = T.edgeCos[size_t(ec.e)];
        out.push_back(R);
    };
    for (const EC& ec : eds) {
        if (ec.pMin < 0) continue;
        edgeRec(ec, ec.tMin, ec.dMin, ec.pMin);
        if (ec.pMax >= 0 && ec.pMax != ec.pMin) edgeRec(ec, ec.tMax, ec.dMax, ec.pMax);
    }
}

// 2.3: polygon-edge records of X's polygons hit by `looked` records, evidence from `evidence`.
void polygonEdges(const Bound& X, const std::vector<Record>& looked, size_t lookedCount,
                  const std::vector<Record>& evidence, std::vector<Record>& target) {
    std::vector<int> seen;
    for (size_t li = 0; li < lookedCount; ++li) {
        const Record& L = looked[li];
        if (L.polygon < 0) continue;
        bool dup = false;
        for (int s : seen)
            if (s == L.polygon) dup = true;
        if (dup) continue;
        seen.push_back(L.polygon);
        const BoundPoly& poly = X.polys[size_t(L.polygon)];
        const int n = poly.n;
        bool f[4] = {false, false, false, false};
        for (int k = 0; k < n; ++k) {
            const int v = poly.idx[k];
            bool inside = false, outside = false;
            for (const Record& R : evidence) {
                const int inEnd = R.front ? R.to : R.from;
                const int outEnd = R.front ? R.from : R.to;
                if (inEnd == v) inside = true;
                if (outEnd == v) outside = true;
            }
            f[k] = inside && !outside;
        }
        int ei = -1, ej = -1;
        if (f[0] && f[1]) {
            ei = 0;
            ej = 1;
        } else if (f[0] && !f[1]) {
            if (f[n - 1]) {
                ei = n - 1;
                ej = 0;
            }
        } else if (!f[0] && f[1]) {
            if (f[2] && (n == 3 || !f[3])) {
                ei = 1;
                ej = 2;
            }
        } else {
            if (n == 4 && f[2] && f[3]) {
                ei = 2;
                ej = 3;
            }
        }
        if (ei < 0 || target.size() >= 100) continue;
        Record R;
        R.kind = RecPolyEdge;
        R.number = poly.edgeNum[ei];
        R.from = poly.idx[ei];
        R.to = poly.idx[ej];
        R.start = X.points[size_t(R.from)];
        R.end = X.points[size_t(R.to)];
        R.wen = X.edgeNormal[size_t(R.number)];  // model coordinates until 2.4
        R.edgeCos = X.edgeCos[size_t(R.number)];
        R.T = 0;
        R.point = vec3();
        R.normal = vec3(1, 0, 0);
        R.polygon = -1;
        target.push_back(R);
    }
}

void recordsToWorld(std::vector<Record>& list, const Mtx& polyOwner, const Mtx& peOwner) {
    for (Record& R : list) {
        if (R.kind == RecPolyEdge) {
            R.start = toWorld(peOwner, R.start, "xzy", "zyx", "zyx");
            R.end = toWorld(peOwner, R.end, "xzy", "zyx", "zyx");
            R.wen = toWorld(peOwner, R.wen, "xzy", "zyx", "yzx", false);
            continue;
        }
        R.start = toWorld(polyOwner, R.start, "xzy", "zyx", "zyx");
        R.end = toWorld(polyOwner, R.end, "xzy", "zyx", "zyx");
        R.point = toWorld(polyOwner, R.point, "xzy", "zyx", "zyx");
        R.normal = toWorld(polyOwner, R.normal, "zxy", "zyx", "yxz", false);
    }
}

// Single-list edge pass (2.5 step 6). `carList`: the list's records are the car's features and
// the owner of the polygons is the object; otherwise the reverse.
void singleListPass(std::vector<Record>& list, bool carList, const Bound& owner, const Mtx& ownerMtx, const Mtx& M,
                    const Mtx& M0, float dt, std::vector<Impact>& out, const Mtx* O = nullptr, const Mtx* O0 = nullptr) {
    const float invdt = 1.0f / dt;
    const Mtx inv = tinv(ownerMtx);
    const float sign = carList ? 1.0f : -1.0f;
    float lastDepth = 0.0f;
    auto clearFaces = [&](const EdgeEdgeResult& r, Record& A, Record* B) {
        if (r.depthComputed) lastDepth = r.depth;
        if (r.flag) {
            A.face = false;
            if (B) B->face = false;
        } else {
            if (A.depth > lastDepth / 2.0f) A.face = false;
            if (B && B->depth > lastDepth / 2.0f) B->face = false;
        }
    };
    auto cornerToWorld = [&](int, int which, const vec3& c) {
        return which == 0 ? toWorld(ownerMtx, c, "zyx", "zyx", "xzy") : toWorld(ownerMtx, c, "zyx", "yzx", "xyz");
    };
    auto test = [&](const Record& R, const vec3& dirW) {
        const vec3 dc = displacement(M, M0, R.point);
        const vec3 db = (O && O0) ? displacement(*O, *O0, R.point) : vec3(0.0f, 0.0f, 0.0f);
        vec3 vel;
        if (carList) vel = vec3((dc.x - db.x) * invdt, (dc.y - db.y) * invdt, (dc.z - db.z) * invdt);
        else vel = vec3((db.x - dc.x) * invdt, (db.y - dc.y) * invdt, (db.z - dc.z) * invdt);
        const vec3 velF = into(ownerMtx, vel, "yzx", "zyx", "zyx");
        vec3 pF = into(ownerMtx, R.point, "yzx", "zyx", "zyx");
        pF = vec3(pF.x + inv.o.x, pF.y + inv.o.y, pF.z + inv.o.z);
        const vec3 dirF = into(ownerMtx, dirW, "yxz", "xzy", "zyx");
        return edgeEdgeTest(polyOf(owner, R.polygon), pF, dirF, velF, dt, cornerToWorld, R.start, R.end, R.wen,
                            R.edgeCos);
    };
    size_t i = 0;
    while (i < list.size()) {
        Record& P = list[i];
        const bool paired = P.kind == RecEdge && i + 1 < list.size() && list[i + 1].kind == RecEdge &&
                            list[i + 1].number == P.number;
        if (P.kind == RecEdge && !P.suppressed) {
            vec3 dir = vsub(P.end, P.start);
            const float dl = (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x;  // collision-contacts.md 3d step 1
            dir = dl == 0.0f ? vec3(0, 0, 0) : vscale(dir, 1.0f / fsqrt(dl));
            const EdgeEdgeResult r1 = test(P, dir);
            if (r1.ok) {
                P.done = true;
                if (out.size() < kMaxImpacts) {
                    Impact im;
                    im.n = vec3(sign * r1.nrm.x, sign * r1.nrm.y, sign * r1.nrm.z);
                    im.depth = r1.depth;
                    im.P = r1.pos;
                    out.push_back(im);
                }
            }
            clearFaces(r1, P, paired ? &list[i + 1] : nullptr);
            if (paired) {
                Record& Q = list[i + 1];
                const EdgeEdgeResult r2 = test(Q, dir);
                if (r2.ok) {
                    P.done = true;
                    const bool write = !r1.ok || r2.edgeNumber != r1.edgeNumber;
                    if (write && out.size() < kMaxImpacts) {
                        Impact im;
                        im.n = vec3(sign * r2.nrm.x, sign * r2.nrm.y, sign * r2.nrm.z);
                        im.depth = r2.depth;
                        im.P = r2.pos;
                        out.push_back(im);
                    }
                }
                clearFaces(r2, P, &Q);
            }
        }
        i += paired ? 2 : 1;
    }
}

// Two-list edge pass (2.5 step 7).
void twoListPass(std::vector<Record>& car, std::vector<Record>& obj, const Mtx& M, const Mtx& M0, float dt,
                 std::vector<Impact>& out, const Mtx* O = nullptr, const Mtx* O0 = nullptr) {
    const float fastThr = 1.5f * dt;
    const float invdt = 1.0f / dt;
    struct Cand {
        bool fast, parallel, angleOk;
        vec3 nrm, pos;
        float depth, d2, ttc;
    };
    auto offer = [&](Record& R, int partner, const vec3& ptO, const Cand& c) {
        if (R.suppressed || R.kind == RecPolyEdge) return;
        const float s = dotZYX(vsub(ptO, R.point), R.normal);
        bool accept;
        if (c.fast) {
            accept = R.partner < 0 || s > R.pOffset || R.m > fastThr || !R.angleOk;
        } else if (c.parallel || R.depth * 1.2f < c.depth || R.m <= fastThr) {
            accept = false;
        } else if (R.partner < 0) {
            accept = s * s + c.d2 < R.depth * R.depth;
        } else {
            accept = s * s + c.d2 * 4.0f < (R.pDepth * R.pDepth * 4.0f + R.pOffset * R.pOffset);
        }
        if (!accept) return;
        R.m = c.ttc;
        R.pNormal = c.nrm;
        R.angleOk = c.angleOk;
        R.pDepth = c.depth;
        R.pPos = c.pos;
        R.pOffset = s;
        R.partner = partner;
    };
    auto clearIf = [&](Record& R, bool fast, float depth) {
        if (fast) R.face = false;
        else if (R.depth > depth * 0.5f) R.face = false;
    };
    size_t j = 0;
    while (j < obj.size()) {
        Record& Q = obj[j];
        if (Q.kind == RecCorner) {
            j += 1;
            continue;
        }
        const bool qPaired = j + 1 < obj.size() && obj[j + 1].number == Q.number;
        if (Q.kind != RecPolyEdge) {
            const vec3 dQ = vsub(Q.end, Q.start);
            const float LQ = dotZYX(dQ, dQ);
            size_t i = 0;
            while (i < car.size()) {
                Record& P = car[i];
                if (P.kind == RecCorner) {
                    i += 1;
                    continue;
                }
                const bool pPaired = i + 1 < car.size() && car[i + 1].number == P.number;
                if (!(Q.suppressed && P.suppressed)) {
                    const vec3 dP = vsub(P.end, P.start);
                    vec3 nrm, ptP, ptQ;
                    float depth = 0;
                    if (closestPoints(P.start, P.end, Q.start, Q.end, nrm, depth, ptP, ptQ)) {
                        Cand c;
                        c.nrm = nrm;
                        c.depth = depth;
                        if (depth == 0.0f) c.angleOk = true;
                        else
                            // Q's edge must face against nrm, P's along it (nrm: Q's line -> P's).
                            c.angleOk = dotZYX(nrm, Q.wen) <= -((Q.edgeCos - kSin15) - 0.01f) &&
                                        ((P.edgeCos - kSin15) - 0.01f) <= dotZYX(nrm, P.wen);
                        c.d2 = depth * depth;
                        const vec3 cr = pcross(dQ, dP);
                        const float cc = dotZYX(cr, cr);
                        c.parallel = (c.d2 * LQ > cc) || (dotZYX(dP, dP) * c.d2 > cc);
                        if (depth == 0.0f) {
                            c.ttc = 0.0f;
                        } else {
                            const vec3 dc = displacement(M, M0, ptP);
                            const vec3 db = (O && O0) ? displacement(*O, *O0, ptQ) : vec3(0.0f, 0.0f, 0.0f);
                            const vec3 rv((dc.x - db.x) * invdt, (dc.y - db.y) * invdt, (dc.z - db.z) * invdt);
                            const float rn = dotZYX(nrm, rv);
                            c.ttc = rn <= 0.0f ? kHuge : depth / rn;
                        }
                        c.pos = vscale(vadd(ptP, ptQ), 0.5f);
                        c.fast = c.ttc < fastThr;
                        clearIf(P, c.fast, depth);
                        if (pPaired) clearIf(car[i + 1], c.fast, depth);
                        clearIf(Q, c.fast, depth);
                        if (qPaired) clearIf(obj[j + 1], c.fast, depth);
                        offer(P, int(j), ptQ, c);
                        if (pPaired) offer(car[i + 1], int(j), ptQ, c);
                        offer(Q, int(i), ptP, c);
                        if (qPaired) offer(obj[j + 1], int(i), ptP, c);
                    }
                }
                i += pPaired ? 2 : 1;
            }
        }
        j += qPaired ? 2 : 1;
    }
    // Emission.
    for (size_t i = 0; i < car.size(); ++i) {
        Record& P = car[i];
        const int k = P.partner;
        if (k < 0 || !P.angleOk || obj[size_t(k)].suppressed) continue;
        if (i > 0 && car[i - 1].number == P.number && car[i - 1].partner == k) continue;
        P.done = true;
        if (out.size() < kMaxImpacts) {
            Impact im;
            im.n = vec3(-P.pNormal.x, -P.pNormal.y, -P.pNormal.z);
            im.depth = P.pDepth;
            im.P = P.pPos;
            out.push_back(im);
        }
    }
    for (size_t jj0 = 0; jj0 < obj.size(); ++jj0) {
        Record& Q = obj[jj0];
        const int k = Q.partner;
        if (k < 0 || !Q.angleOk || car[size_t(k)].suppressed) continue;
        if (jj0 > 0 && obj[jj0 - 1].number == Q.number && obj[jj0 - 1].partner == k) continue;
        Q.done = true;
        const int jj = (jj0 > 0 && obj[jj0 - 1].number == Q.number) ? int(jj0) - 1 : int(jj0);
        bool write = car[size_t(k)].partner != jj;
        if (size_t(k) + 1 < car.size() && car[size_t(k) + 1].number == car[size_t(k)].number &&
            car[size_t(k) + 1].partner == jj)
            write = false;
        if (write && out.size() < kMaxImpacts) {
            Impact im;
            im.n = vec3(-Q.pNormal.x, -Q.pNormal.y, -Q.pNormal.z);
            im.depth = Q.pDepth;
            im.P = Q.pPos;
            out.push_back(im);
        }
    }
}

}  // namespace

void polygonalObjectContacts(const Bound& car, const Mtx& M, const Mtx& M0, const Bound& obj, const Mtx& O,
                             float dt, const Material& carMaterial, const Material& objMaterial,
                             std::vector<Impact>& out, const Mtx* O0p) {
    out.clear();
    const Mtx& O0 = O0p ? *O0p : O;
    // 1.1 Separating test.
    const vec3 dir = vsub(O.o, M.o);
    const vec3 lA = into(M, dir, "yzx", "yzx", "yzx");
    float maxV = -kHuge;
    for (const vec3& p : car.points) {
        const float v = dotZYX(p, lA);
        if (v > maxV) maxV = v;
    }
    const float Mdir = dotYZX(M.o, dir);
    const float maxA = Mdir + maxV;
    const vec3 nd(-dir.x, -dir.y, -dir.z);
    const vec3 lB = into(O, nd, "yzx", "yzx", "yzx");
    float maxW = -kHuge;
    for (const vec3& q : obj.points) {
        const float v = dotZYX(q, lB);
        if (v > maxW) maxW = v;
    }
    const float minB = -(dotYZX(O.o, nd) + maxW);
    if (maxA < minB) return;
    const float thrA = minB - Mdir;
    const float thrB = dotYZX(O.o, dir) + (-maxA);

    // 1.2-2.1 The two passes.
    const Mtx invO = tinv(O), invO0 = tinv(O0), invM = tinv(M), invM0 = tinv(M0);
    std::vector<Record> carList, objList;
    {
        PassInput in;
        in.T = &car;
        in.Q = &obj;
        in.cur = mprod(M, invO);
        in.last = mprod(M0, O0p ? invO0 : invO);
        in.lT = lA;
        in.thr = thrA;
        in.Tworld = M;
        runPass(in, carList);
    }
    {
        PassInput in;
        in.T = &obj;
        in.Q = &car;
        in.cur = mprod(O, invM);
        in.last = mprod(O0p ? O0 : O, invM0);
        in.lT = lB;
        in.thr = thrB;
        in.Tworld = O;
        runPass(in, objList);
    }
    if (carList.empty() && objList.empty()) return;
    // 2.3 Polygon-edge records.
    {
        const std::vector<Record> carOrig = carList;  // evidence and looked-at records as written by 2.1
        const std::vector<Record> objOrig = objList;
        polygonEdges(car, objOrig, objOrig.size(), carOrig, carList);
        polygonEdges(obj, carOrig, carOrig.size(), objOrig, objList);
    }
    // 2.4 To world.
    recordsToWorld(carList, O, M);
    recordsToWorld(objList, M, O);
    // 2.5 Impacts.
    normaliseList(carList);
    normaliseList(objList);
    const Mtx* Om = O0p ? &O : nullptr;
    const Mtx* O0m = O0p;
    cornerGroups(carList, M, M0, dt, true, 0.0f, 1.0f, out, Om, O0m);  // vel = v_B − v_A
    cornerGroups(objList, M, M0, dt, false, 1.0f, -1.0f, out, Om, O0m);
    pairMerge(carList, true);
    pairMerge(objList, true);
    int e1 = 0, e2 = 0;
    for (const Record& R : carList)
        if (R.kind == RecEdge) ++e1;
    for (const Record& R : objList)
        if (R.kind != RecCorner) ++e2;
    if (e1 > 0 && e2 > 0) twoListPass(carList, objList, M, M0, dt, out, Om, O0m);
    else if (e1 > 0) singleListPass(carList, true, obj, O, M, M0, dt, out, Om, O0m);
    else if (e2 > 0) singleListPass(objList, false, car, M, M, M0, dt, out, Om, O0m);
    faceImpacts(carList, dt, 1.0f, out);
    faceImpacts(objList, dt, -1.0f, out);
    for (Impact& im : out) {
        im.matA = carMaterial;
        im.matB = objMaterial;
    }
}

// ---------------------------------------------------------------------------------------------
// Terrain landmarks (section 3)

namespace {

// trunc() of collision-objects.md / 3.4: towards zero to a 64-bit integer, low 32 bits as a
// signed integer; a non-finite value or one of magnitude 2^63 or more gives 0.
int truncOr(float v) {
    if (!std::isfinite(v) || v >= 9223372036854775808.0f || v <= -9223372036854775808.0f) return 0;
    const long long t = static_cast<long long>(v);
    return static_cast<int>(static_cast<unsigned int>(static_cast<unsigned long long>(t)));
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int cellOfPoint(const TerrainGrid& g, float x, float z) {
    int ix = truncOr((x - g.gmin.x) * g.factor.x);
    int iz = truncOr((z - g.gmin.z) * g.factor.z);
    ix = clampi(ix, 0, g.W - 1);
    iz = clampi(iz, 0, g.D - 1);
    return iz * g.W + ix;
}

// collision-objects.md "Cells of a segment" + collision-object-contacts.md 3.4.
void cellsOfSegment(const Bound& b, const vec3& S, const vec3& E, std::vector<int>& cells) {
    cells.clear();
    const TerrainGrid& g = b.grid;
    const int N = g.W * g.D;
    auto add = [&](int c) {
        if (c >= 0 && c < int(g.count.size()) && g.count[size_t(c)] != 0) cells.push_back(c);
    };
    const vec3& mn = b.bmin;
    const vec3& mx = b.bmax;
    if (S.x == E.x && S.z == E.z) {
        if (S.x >= mn.x && S.x <= mx.x && S.z >= mn.z && S.z <= mx.z) add(cellOfPoint(g, S.x, S.z));
        return;
    }
    if ((S.x < mn.x && E.x < mn.x) || (S.y < mn.y && E.y < mn.y) || (S.z < mn.z && E.z < mn.z)) return;
    if ((S.x > mx.x && E.x > mx.x) || (S.y > mx.y && E.y > mx.y) || (S.z > mx.z && E.z > mx.z)) return;
    const int cs = cellOfPoint(g, S.x, S.z), ce = cellOfPoint(g, E.x, E.z);
    if (cs == ce) {
        add(cs);
        return;
    }
    const float x0 = S.x, z0 = S.z, x1 = E.x, z1 = E.z;
    const float dx = x1 - x0, dz = z1 - z0;
    float t0 = 0.0f, t1 = 1.0f;
    if (dx == 0.0f) {
        if (x0 > mx.x || x0 < mn.x) return;
    } else {
        const float inv = 1.0f / dx;
        const float ta = (mx.x - x0) * inv, tb = (mn.x - x0) * inv;
        if (x0 < x1) {
            if (ta < 0.0f) return;
            if (ta <= 1.0f) t1 = ta;
            if (tb > 1.0f) return;
            if (tb >= 0.0f) t0 = tb;
        } else {
            if (ta > 1.0f) return;
            if (ta >= 0.0f) t0 = ta;
            if (tb < 0.0f) return;
            if (tb <= 1.0f) t1 = tb;
        }
        if (t0 > t1) return;
    }
    if (dz == 0.0f) {
        if (z0 > mx.z || z0 < mn.z) return;
    } else {
        const float inv = 1.0f / dz;
        const float ta = (mx.z - z0) * inv, tb = (mn.z - z0) * inv;
        if (z0 < z1) {
            if (ta < 0.0f) return;
            if (ta <= 1.0f && ta < t1) t1 = ta;
            if (tb > 1.0f) return;
            if (tb >= 0.0f && tb > t0) t0 = tb;
        } else {
            if (ta > 1.0f) return;
            if (ta >= 0.0f && ta > t0) t0 = ta;
            if (tb < 0.0f) return;
            if (tb <= 1.0f && tb < t1) t1 = tb;
        }
        if (t0 > t1) return;
    }
    if (t0 > 0.0f) t0 = t0 + 0.0001f;
    if (t1 < 1.0f) t1 = t1 - 0.0001f;
    const float fx = g.factor.x, fz = g.factor.z;
    const float gx0 = ((dx * t0 + x0) - mn.x) * fx, gx1 = ((dx * t1 + x0) - mn.x) * fx;
    const float gz0 = ((dz * t0 + z0) - mn.z) * fz, gz1 = ((dz * t1 + z0) - mn.z) * fz;
    const float gdx = gx1 - gx0, gdz = gz1 - gz0;
    const int tx0 = truncOr(gx0), tx1 = truncOr(gx1), tz0 = truncOr(gz0), tz1 = truncOr(gz1);
    int ix = clampi(tx0, 0, g.W - 1);
    int iz = clampi(tz0, 0, g.D - 1);
    const bool xRises = x1 > x0, zRises = z1 > z0;
    const int W = g.W;
    int guard = 0;
    if (tx0 == tx1) {
        int c = W * iz + ix;
        if (c >= N) return;
        for (;;) {
            if (c < 0 || ++guard > 100000) return;
            add(c);
            if (zRises) {
                iz += 1;
                c += W;
                if (!(float(iz) < gz1)) return;
            } else {
                iz -= 1;
                c -= W;
                if (!(gz1 - 1.0f < float(iz))) return;
            }
            if (c >= N) return;
        }
    } else if (tz0 == tz1) {
        int c = W * iz + ix;
        if (c >= N) return;
        for (;;) {
            if (c < 0 || ++guard > 100000) return;
            add(c);
            if (xRises) {
                ix += 1;
                c += 1;
                if (!(float(ix) < gx1)) return;
            } else {
                ix -= 1;
                c -= 1;
                if (!(gx1 - 1.0f < float(ix))) return;
            }
            if (c >= N) return;
        }
    } else {
        int c = W * iz + ix;
        if (c >= N) return;
        int nx = xRises ? ix + 1 : ix;
        int nz = zRises ? iz + 1 : iz;
        for (;;) {
            if (c < 0 || ++guard > 100000) return;
            add(c);
            const float tx = (float(nx) - gx0) / gdx, tz = (float(nz) - gz0) / gdz;
            if (tx > 1.0f && tz > 1.0f) return;
            if (tx < tz) {
                ix += xRises ? 1 : -1;
                nx += xRises ? 1 : -1;
            } else {
                iz += zRises ? 1 : -1;
                nz += zRises ? 1 : -1;
            }
            c = W * iz + ix;
            if (c >= N) return;
        }
    }
}

}  // namespace

void terrainCellsOfSegment(const Bound& b, const vec3& S, const vec3& E, std::vector<int>& cells) {
    cellsOfSegment(b, S, E, cells);
}

void terrainObjectContacts(const Bound& car, const Mtx& M, const Mtx& M0, const Bound& obj, const Mtx& O,
                           float dt, const Material& carMaterial, const MaterialTable& table,
                           std::vector<Impact>& out) {
    out.clear();
    const Mtx invO = tinv(O);
    const Mtx cur = mprod(M, invO), last = mprod(M0, invO);
    const size_t np = car.points.size();
    std::vector<vec3> C(np);
    for (size_t k = 0; k < np; ++k) C[k] = toWorld(cur, car.points[k], "zyx", "yzx", "yxz");
    std::vector<Record> list;
    std::vector<int> cells;
    std::vector<uint32_t> stamp(obj.polys.size(), 0);
    uint32_t query = 0;
    const TerrainGrid& g = obj.grid;
    auto forPolys = [&](const vec3& S, const vec3& E, const std::function<void(int)>& fn) {
        cellsOfSegment(obj, S, E, cells);
        ++query;
        for (int c : cells) {
            const int first = g.first[size_t(c)], cnt = g.count[size_t(c)];
            for (int r = 0; r < cnt; ++r) {
                const size_t ri = size_t(first + r);
                if (ri >= g.refs.size()) break;
                const int pi = g.refs[ri];
                if (stamp[size_t(pi)] == query) continue;
                stamp[size_t(pi)] = query;
                fn(pi);
            }
        }
    };
    // 3.1-3.2 Corner segments.
    for (size_t k = 0; k < np; ++k) {
        if (list.size() >= 100) break;
        vec3 start = toWorld(last, car.points[k], "yzx", "yxz", "yxz");
        const vec3 end = C[k];
        if (start.x != end.x || start.y != end.y || start.z != end.z)
            start = vec3((start.x - end.x) + end.x, (start.y - end.y) + end.y, (start.z - end.z) + end.z);
        float best = 2.0f, bestDE = 0;
        int bestPoly = -1;
        forPolys(start, end, [&](int pi) {
            const PolyView poly = polyOf(obj, pi);
            const vec3 wS = vsub(start, poly.c[0]);
            const float dS = dotZXY(wS, poly.normal);
            if (dS < 0.0f) return;
            const vec3 wE = vsub(end, poly.c[0]);
            const float dE = dotZXY(wE, poly.normal);
            if (dE >= 0.0f) return;
            const float T = dS / (dS - dE);
            if (T > best) return;
            if (!lineInside1(poly, start, vsub(end, start))) return;
            best = T;
            bestDE = dE;
            bestPoly = pi;
        });
        if (bestPoly < 0) continue;
        Record R;
        R.kind = RecCorner;
        R.start = start;
        R.end = end;
        R.T = best;
        R.point = vec3((end.x - start.x) * best + start.x, (end.y - start.y) * best + start.y,
                       (end.z - start.z) * best + start.z);
        R.normal = obj.polys[size_t(bestPoly)].normal;
        R.polygon = bestPoly;
        R.material = obj.polys[size_t(bestPoly)].material;
        R.depth = -bestDE;
        R.front = true;
        R.number = int(k);
        R.from = -1;
        R.to = int(k);
        list.push_back(R);
    }
    // 3.3 Edge segments.
    for (size_t e = 0; e < car.edges.size(); ++e) {
        if (list.size() + 2 > 100) break;
        const int a = car.edges[e].first, b = car.edges[e].second;
        const vec3 S = C[size_t(a)], E = C[size_t(b)];
        float tMin = 2.0f, tMax = -1.0f;
        int hits = 0;
        struct Hit {
            float T, depth;
            vec3 point, normal;
            int poly, material;
            bool front;
        } slot[2];
        forPolys(S, E, [&](int pi) {
            const PolyView poly = polyOf(obj, pi);
            const float dS = dotYXZ(vsub(S, poly.c[0]), poly.normal);
            const float dE = dotYXZ(vsub(E, poly.c[0]), poly.normal);
            if (sbit(dS) == sbit(dE)) return;
            const float t = dS / (dS - dE);
            const bool tn = std::isnan(t);  // a NaN t is tested and can only fill slot 0 (3.3)
            if (!tn && tMin < t && t < tMax) return;
            if (!lineInside2(poly, S, vsub(E, S))) return;
            Hit h;
            h.T = t;
            h.point = vec3((E.x - S.x) * t + S.x, (E.y - S.y) * t + S.y, (E.z - S.z) * t + S.z);
            h.normal = poly.normal;
            h.poly = pi;
            h.material = poly.material;
            if (dS <= 0.0f) {
                h.depth = -dS;
                h.front = false;
            } else {
                h.depth = -dE;
                h.front = true;
            }
            hits += 1;
            if ((tn || t <= tMin) && (h.front || tn || t < tMin)) {
                slot[0] = h;
                tMin = t;
            }
            if (t >= tMax && (!h.front || t > tMax)) {
                slot[1] = h;
                tMax = t;
            }
        });
        const int nrec = hits < 2 ? hits : 2;
        for (int s = 0; s < nrec; ++s) {
            const Hit& h = slot[s];
            Record R;
            R.kind = RecEdge;
            R.start = S;
            R.end = E;
            R.T = h.T;
            R.point = h.point;
            R.normal = h.normal;
            R.polygon = h.poly;
            R.material = h.material;
            R.depth = h.depth;
            R.front = h.front;
            R.number = int(e);
            R.from = a;
            R.to = b;
            R.wen = toWorld(M, car.edgeNormal[e], "zyx", "zyx", "zyx", false);
            R.edgeCos = car.edgeCos[e];
            list.push_back(R);
        }
    }
    if (list.empty()) return;
    const int firstMat = list[0].material;
    recordsToWorld(list, O, O);
    normaliseList(list);
    cornerGroups(list, M, M0, dt, true, 0.0f, 1.0f, out);  // vel = (+0) − v
    pairMerge(list, true);
    int e1 = 0;
    for (const Record& R : list)
        if (R.kind == RecEdge) ++e1;
    if (e1 > 0) singleListPass(list, true, obj, O, M, M0, dt, out);
    faceImpacts(list, dt, 1.0f, out);
    const int gi = (firstMat >= 0 && size_t(firstMat) < obj.materials.size()) ? obj.materials[size_t(firstMat)]
                                                                             : obj.firstMaterial();
    const Material& lm = table.at(gi);
    for (Impact& im : out) {
        im.matA = carMaterial;
        im.matB = lm;
    }
}

}  // namespace phys
}  // namespace cc
