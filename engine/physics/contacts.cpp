#include "physics/contacts.h"

#include <cmath>

#include "core/log.h"
#include "physics/bound.h"
#include "physics/contacts_internal.h"

namespace cc {
namespace phys {

using namespace ci;

vec3 mtxPoint(const Mtx& m, const vec3& p) {
    return vec3(p.x * m.X.x + p.y * m.Y.x + p.z * m.Z.x + m.o.x, p.x * m.X.y + p.y * m.Y.y + p.z * m.Z.y + m.o.y,
                p.x * m.X.z + p.y * m.Y.z + p.z * m.Z.z + m.o.z);
}

vec3 mtxDir(const Mtx& m, const vec3& d) {
    return vec3(d.x * m.X.x + d.y * m.Y.x + d.z * m.Z.x, d.x * m.X.y + d.y * m.Y.y + d.z * m.Z.y,
                d.x * m.X.z + d.y * m.Y.z + d.z * m.Z.z);
}

Mtx mtxFromRows(const vec3& X, const vec3& Y, const vec3& Z, const vec3& o) {
    Mtx m;
    m.X = X;
    m.Y = Y;
    m.Z = Z;
    m.o = o;
    return m;
}

// ---------------------------------------------------------------------------------------------
// Response (collision.md, "Collision response")

namespace {

struct K3 {
    float k[3][3];
};

vec3 pointVelocity(const RigidBody& b, const vec3& P) {
    const vec3 r = vsub(P, b.p);
    const vec3& w = b.w;
    const vec3 c(r.z * w.y - w.z * r.y, w.z * r.x - r.z * w.x, w.x * r.y - w.y * r.x);
    return vadd(c, b.v);
}

// The inverse-mass matrix of a response: a jointed truck's or trailer's is the joint's
// (trailer.md 6.2).
K3 inverseMass(const RigidBody& b, const vec3& P) {
    K3 K;
    b.collisionInverseMass(P, K.k);
    return K;
}

K3 zeroK() {
    K3 K;
    for (auto& r : K.k)
        for (float& v : r) v = 0.0f;
    return K;
}

// Solve x * K = b (collision.md, "Solving x·K = b").
vec3 solveRow(const K3& K, const vec3& b) { return solveRowSystem(K.k, b); }

// n * K with u_j = ((n.z*K[2][j] + n.y*K[1][j]) + K[0][j]*n.x)
vec3 nTimesK(const vec3& n, const K3& K) {
    return vec3((n.z * K.k[2][0] + n.y * K.k[1][0]) + K.k[0][0] * n.x,
                (n.z * K.k[2][1] + n.y * K.k[1][1]) + K.k[0][1] * n.x,
                (n.z * K.k[2][2] + n.y * K.k[1][2]) + K.k[0][2] * n.x);
}

struct Solved {
    vec3 x;          // final impulse (before the weight)
    vec3 sticking;   // the solved sticking impulse
    vec3 pushA, pushB;
};

Solved solveImpact(const Impact& im, RigidBody* a, RigidBody* b) {
    Solved out;
    const vec3 vA = a ? pointVelocity(*a, im.P) : vec3();
    const vec3 vB = b ? pointVelocity(*b, im.P) : vec3();
    const vec3 vrel(vA.x - vB.x, vA.y - vB.y, vA.z - vB.z);
    const K3 KA = a ? inverseMass(*a, im.P) : zeroK();
    const K3 KB = b ? inverseMass(*b, im.P) : zeroK();
    const vec3& n = im.n;
    const float vn = (n.z * vrel.z + n.y * vrel.y) + vrel.x * n.x;
    float mu = im.matB.friction * im.matA.friction;
    float e = im.matB.elasticity * im.matA.elasticity;
    if (e > 1.0f) e = 1.0f;
    vec3 x;
    if (!(vn > 0.01f)) {  // vn <= 0.01, NaN too
        K3 K;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) K.k[i][j] = KA.k[i][j] + KB.k[i][j];
        x = solveRow(K, vec3(-vrel.x, -vrel.y, -vrel.z));
        out.sticking = x;
        const float jn = (x.z * n.z + x.y * n.y) + x.x * n.x;
        const vec3 xt(x.x - jn * n.x, x.y - jn * n.y, x.z - jn * n.z);
        const float L = fsqrt((xt.z * xt.z + xt.y * xt.y) + xt.x * xt.x);
        if (L > jn * mu) {
            const float s = mu / L;
            const vec3 d(xt.x * s + n.x, xt.y * s + n.y, xt.z * s + n.z);
            const vec3 u = nTimesK(n, K);
            const float den = (u.z * d.z + d.y * u.y) + d.x * u.x;
            if (-0.00001f < den && den < 0.00001f) x = vec3(0, 0, 0);
            else x = vscale(d, -(vn / den));
        }
        const float e1 = e + 1.0f;
        x = vscale(x, e1);
    }
    out.x = x;
    if (im.depth > 0.0f) {
        const vec3 uA = nTimesK(n, KA);
        const float a2 = (uA.z * uA.z + uA.y * uA.y) + uA.x * uA.x;
        const vec3 uB((KB.k[2][0] * n.z + KB.k[1][0] * n.y) + KB.k[0][0] * n.x,
                      (KB.k[2][1] * n.z + KB.k[0][1] * n.x) + KB.k[1][1] * n.y,
                      (KB.k[2][2] * n.z + KB.k[0][2] * n.x) + KB.k[1][2] * n.y);
        const float b2 = (uB.y * uB.y + uB.z * uB.z) + uB.x * uB.x;
        float wA, wB;
        if (a2 > b2) {
            wA = 1.0f / (fsqrt(b2 / a2) + 1.0f);
            wB = 1.0f - wA;
        } else if (b2 > a2) {
            wB = 1.0f / (fsqrt(a2 / b2) + 1.0f);
            wA = 1.0f - wB;
        } else {
            wA = wB = 0.5f;
        }
        vec3 pA = vscale(n, wA * im.depth);
        vec3 pB = vscale(n, -(wB * im.depth));
        if (pA.y < 0.0f && b2 > 0.0f) {
            pB.y = pB.y - pA.y;
            pA.y = 0.0f;
        } else if (pB.y < 0.0f && a2 > 0.0f) {
            pA.y = pA.y - pB.y;
            pB.y = 0.0f;
        }
        out.pushA = pA;
        out.pushB = pB;
    }
    return out;
}

void applyImpact(const Impact& im, const Solved& s, float w, RigidBody* a, RigidBody* b,
                 const std::function<void(int, const vec3&)>& onPush) {
    const vec3 J = vscale(s.x, w);
    if (a) {
        a->addImpulseAt(J, im.P);
        a->addPush(s.pushA);
        if (onPush) onPush(0, s.pushA);
    }
    if (b) {
        b->addImpulseAt(vec3(-J.x, -J.y, -J.z), im.P);
        b->addPush(s.pushB);
        if (onPush) onPush(1, s.pushB);
    }
}

}  // namespace

void resolveImpacts(const std::vector<Impact>& impacts, RigidBody* a, RigidBody* b,
                    const std::function<void(int, const vec3&)>& onPush) {
    if (impacts.empty()) return;
    const float w = 1.0f / float(impacts.size());
    for (const Impact& im : impacts) applyImpact(im, solveImpact(im, a, b), w, a, b, onPush);
}

void resolveOneImpact(const Impact& im, RigidBody* a, RigidBody* b, float w, const ImpactApplied& onApplied) {
    const Solved s = solveImpact(im, a, b);
    const vec3 J = vscale(s.x, w);
    if (a) {
        a->addImpulseAt(J, im.P);
        a->addPush(s.pushA);
    }
    if (onApplied) onApplied(0, J, s.pushA, im);
    if (b) {
        b->addImpulseAt(vec3(-J.x, -J.y, -J.z), im.P);
        b->addPush(s.pushB);
    }
    if (onApplied) onApplied(1, vec3(-J.x, -J.y, -J.z), s.pushB, im);
}

void resolveImpactsEx(const std::vector<Impact>& impacts, RigidBody* a, RigidBody* b, const ImpactApplied& onApplied) {
    if (impacts.empty()) return;
    const float w = 1.0f / float(impacts.size());
    for (const Impact& im : impacts) {
        const Solved s = solveImpact(im, a, b);
        const vec3 J = vscale(s.x, w);
        if (a) {
            a->addImpulseAt(J, im.P);
            a->addPush(s.pushA);
        }
        if (onApplied) onApplied(0, J, s.pushA, im);
        if (b) {
            b->addImpulseAt(vec3(-J.x, -J.y, -J.z), im.P);
            b->addPush(s.pushB);
        }
        if (onApplied) onApplied(1, vec3(-J.x, -J.y, -J.z), s.pushB, im);
    }
}

bool resolvePropImpacts(const std::vector<Impact>& impacts, RigidBody* a, RigidBody* prop, float impulseLimit2,
                        const std::function<void(int, const Impact&, const vec3&, const vec3&)>& onApplied) {
    // collision.md, "Breakable props": A = the mover, B = the prop's test body (loose-bodies.md 2).
    if (impacts.empty() || !a || !prop) return false;
    const float w = 1.0f / float(impacts.size());
    bool broke = false;
    for (const Impact& im : impacts) {
        const vec3& n = im.n;
        const vec3 vA = pointVelocity(*a, im.P);
        const vec3 vB = pointVelocity(*prop, im.P);
        const vec3 vrel(vA.x - vB.x, vA.y - vB.y, vA.z - vB.z);
        const K3 KA = inverseMass(*a, im.P);
        K3 KB = zeroK();
        const float vn = (n.z * vrel.z + n.y * vrel.y) + vrel.x * n.x;
        const float mu = im.matB.friction * im.matA.friction;
        float e = im.matB.elasticity * im.matA.elasticity;
        if (e > 1.0f) e = 1.0f;
        vec3 x, xcap;
        if (!(vn > 0.01f)) {  // vn <= 0.01, NaN too
            // 2.1: the sticking impulse against an immovable prop.
            x = solveRow(KA, vec3(-vrel.x, -vrel.y, -vrel.z));
            const float x2 = (x.z * x.z + x.y * x.y) + x.x * x.x;
            K3 K = KA;
            if (x2 > impulseLimit2) {  // a NaN counts as <=: not broken
                // 2.2 broken: the car keeps the capped impulse, the rest is resolved against the
                // prop's own body.
                const float f = fsqrt(impulseLimit2 / x2);
                xcap = vec3(x.x * f, x.y * f, x.z * f);
                const vec3 vr2(((xcap.z * KA.k[2][0] + xcap.y * KA.k[1][0]) + xcap.x * KA.k[0][0]) + vrel.x,
                               ((xcap.z * KA.k[2][1] + xcap.y * KA.k[1][1]) + xcap.x * KA.k[0][1]) + vrel.y,
                               ((xcap.z * KA.k[2][2] + xcap.y * KA.k[1][2]) + xcap.x * KA.k[0][2]) + vrel.z);
                KB = inverseMass(*prop, im.P);
                for (int i = 0; i < 3; ++i)
                    for (int j = 0; j < 3; ++j) K.k[i][j] = KA.k[i][j] + KB.k[i][j];
                x = solveRow(K, vec3(-vr2.x, -vr2.y, -vr2.z));
                broke = true;
            }
            // 2.3: the friction cone on this x with this K, vn from step 1.
            const float jn = (x.z * n.z + x.y * n.y) + x.x * n.x;
            const vec3 xt(x.x - jn * n.x, x.y - jn * n.y, x.z - jn * n.z);
            const float L = fsqrt((xt.y * xt.y + xt.z * xt.z) + xt.x * xt.x);
            if (L > jn * mu) {
                const float sl = mu / L;
                const vec3 d(xt.x * sl + n.x, xt.y * sl + n.y, xt.z * sl + n.z);
                const vec3 u = nTimesK(n, K);
                const float den = (d.y * u.y + d.x * u.x) + u.z * d.z;  // breakable order
                if (-0.00001f < den && den < 0.00001f) x = vec3(0, 0, 0);
                else x = vscale(d, -(vn / den));
            }
            // 2.4.
            const float e1 = e + 1.0f;
            x = vscale(x, e1);
        }
        // 3. Push: shares from the vector lengths; an untouched prop never pushes A downwards.
        vec3 pushA, pushB;
        if (im.depth > 0.0f) {
            const vec3 uA = nTimesK(n, KA);
            const vec3 uB((KB.k[2][0] * n.z + KB.k[1][0] * n.y) + KB.k[0][0] * n.x,
                          (KB.k[2][1] * n.z + KB.k[0][1] * n.x) + KB.k[1][1] * n.y,
                          (KB.k[2][2] * n.z + KB.k[0][2] * n.x) + KB.k[1][2] * n.y);
            float LA = fsqrt((uA.x * uA.x + uA.y * uA.y) + uA.z * uA.z);
            float LB = fsqrt((uB.x * uB.x + uB.y * uB.y) + uB.z * uB.z);
            float S = LA + LB;
            if (S == 0.0f) {
                S = 1.0f;
                LA = LB = 0.5f;
            }
            const float f = im.depth / S;
            pushA = vscale(n, f * LA);
            pushB = vscale(n, -(f * LB));
            if (pushA.y < 0.0f) {
                pushB.y = pushB.y - pushA.y;
                pushA.y = 0.0f;
            } else if (pushB.y < 0.0f) {
                pushA.y = pushA.y - pushB.y;
                pushB.y = 0.0f;
            }
        }
        // 4.
        const vec3 JA((x.x + xcap.x) * w, (x.y + xcap.y) * w, (x.z + xcap.z) * w);
        const vec3 JB((-x.x) * w, (-x.y) * w, (-x.z) * w);
        a->addImpulseAt(JA, im.P);
        a->addPush(pushA);
        if (onApplied) onApplied(0, im, JA, pushA);
        prop->addImpulseAt(JB, im.P);
        prop->addPush(pushB);
        if (onApplied) onApplied(1, im, JB, pushB);
    }
    return broke;
}

// ---------------------------------------------------------------------------------------------
// Shared machinery

namespace ci {

bool lineInside2(const PolyView& p, const vec3& S, const vec3& D) {
    for (int j = 0; j < p.n; ++j) {
        const vec3& cj = p.c[j];
        const vec3& cn = p.c[(j + 1) % p.n];
        const vec3 g = pcross(vsub(cn, cj), D);
        const vec3 w = vsub(S, cj);
        const float f = dotZYX(g, w);
        const float h = dotZYX(g, p.edgeN[j]);
        if (f != 0.0f && sbit(f) != sbit(h)) return false;
    }
    return true;
}

bool lineInside1(const PolyView& p, const vec3& S, const vec3& D) {
    for (int j = 0; j < p.n; ++j) {
        const vec3& cj = p.c[j];
        const vec3& cn = p.c[(j + 1) % p.n];
        const vec3 g = pcross(vsub(cn, cj), D);
        const vec3 w = vsub(S, cj);
        const float f = dotZYX(g, w);
        const float h = dotZYX(g, p.edgeN[j]);
        if (f < 0.0f && h > 0.0f) return false;
    }
    return true;
}

vec3 displacement(const Mtx& M, const Mtx& M0, const vec3& x) {
    const vec3 r = vsub(x, M.o);
    const vec3 l((r.z * M.X.z + r.y * M.X.y) + r.x * M.X.x, (r.z * M.Y.z + r.y * M.Y.y) + r.x * M.Y.x,
                 (r.z * M.Z.z + r.y * M.Z.y) + r.x * M.Z.x);
    const vec3 DX = vsub(M.X, M0.X), DY = vsub(M.Y, M0.Y), DZ = vsub(M.Z, M0.Z), Do = vsub(M.o, M0.o);
    return vec3(((DZ.x * l.z + DY.x * l.y) + DX.x * l.x) + Do.x, ((DZ.y * l.z + DY.y * l.y) + DX.y * l.x) + Do.y,
                ((DZ.z * l.z + DY.z * l.y) + DX.z * l.x) + Do.z);
}

namespace {
bool sameSide(float a, float b) {
    return (a > 0.0f && b > 0.01f * a && a > 0.01f * b) || (a <= 0.0f && b < 0.01f * a && a < 0.01f * b);
}
}  // namespace

bool closestPoints(const vec3& A, const vec3& B, const vec3& S, const vec3& E, vec3& nrm, float& depth, vec3& ptAB,
                   vec3& ptSE) {
    const vec3 pe = vsub(B, A), ce = vsub(E, S);
    const vec3 n0 = pcross(pe, ce);
    const float L = (n0.z * n0.z + n0.y * n0.y) + n0.x * n0.x;
    if (L <= 0.0f) return false;
    const float k = 1.0f / fsqrt(L);
    nrm = vec3(n0.x * k, n0.y * k, n0.z * k);
    const vec3 w = vsub(A, S);
    depth = (nrm.y * w.y + nrm.x * w.x) + nrm.z * w.z;
    if (depth < 0.0f) {
        depth = -depth;
        nrm = vec3(-nrm.x, -nrm.y, -nrm.z);
    }
    const vec3 q = pcross(nrm, ce);
    const float s0 = dotZYX(q, vsub(A, S)), s1 = dotZYX(q, vsub(B, S));
    if (sameSide(s0, s1)) return false;
    if (s1 == s0) ptAB = vscale(vadd(A, B), 0.5f);
    else ptAB = vscale(vsub(vscale(A, s1), vscale(B, s0)), 1.0f / (s1 - s0));
    const vec3 r = pcross(nrm, pe);
    const float t0 = -dotZYX(r, vsub(A, S)), t1 = dotZYX(r, vsub(E, A));
    if (sameSide(t0, t1)) return false;
    if (t1 == t0) ptSE = vscale(vadd(S, E), 0.5f);
    else ptSE = vscale(vsub(vscale(S, t1), vscale(E, t0)), 1.0f / (t1 - t0));
    return true;
}

EdgeEdgeResult edgeEdgeTest(const PolyView& poly, const vec3& p, const vec3& dir, const vec3& vel, float dt,
                            const std::function<vec3(int, int, const vec3&)>& cornerToWorld, const vec3& S,
                            const vec3& E, const vec3& wen, float cosE) {
    EdgeEdgeResult res;
    const vec3& N = poly.normal;
    // 1. Motion across the car edge.
    const float dv = pdot(dir, vel);
    vec3 vp(vel.x - dir.x * dv, vel.y - dir.y * dv, vel.z - dir.z * dv);
    const vec3 c = pcross(dir, N);
    const float cc = (c.x * c.x + c.z * c.z) + c.y * c.y;
    const float dn = std::fabs((dir.z * N.z + dir.y * N.y) + dir.x * N.x);
    if (dn == 0.0f) {
        vp = vec3(0, 0, 0);
    } else if (cc >= 0.000001f) {
        const float k = -(((c.x * vp.x + vp.z * c.z) + vp.y * c.y) / cc);
        const vec3 w(vp.x + c.x * k, vp.y + c.y * k, vp.z + c.z * k);
        const float f = 1.0f / dn - 1.0f;
        vp = vec3(vp.x + w.x * f, vp.y + w.y * f, vp.z + w.z * f);
    }
    // 2. Polygon edges, from corner j-1 to corner j, starting with last -> first.
    const int n = poly.n;
    float best = kHuge, minDist = kHuge;
    int bestJ = -1, minJ = -1;
    for (int j = 0; j < n; ++j) {
        const vec3& cprev = poly.c[(j - 1 + n) % n];
        const vec3& cj = poly.c[j];
        vec3 e = vsub(cprev, cj);
        const float el = pdot(e, e);
        e = el == 0.0f ? vec3(0, 0, 0) : vscale(e, 1.0f / fsqrt(el));
        const vec3 m = pcross(e, N);
        const float dist = ((p.x - cj.x) * m.x + m.z * (p.z - cj.z)) + m.y * (p.y - cj.y);
        const float pen = dist > 0.0f ? dist : 0.0f;
        const float rate = (m.x * vp.x + m.z * vp.z) + m.y * vp.y;
        if (rate > 0.0f && pen < rate * best) {
            best = pen / rate;
            bestJ = j;
        }
        if (dist < minDist) {
            minDist = dist;
            minJ = j;
        }
    }
    int J;
    if (1.5f * dt > best) {
        res.flag = true;
        J = bestJ;
    } else {
        res.flag = false;
        J = minJ;
    }
    if (J < 0) return res;
    const int jPrev = (J - 1 + n) % n;
    const vec3 A = cornerToWorld(jPrev, 0, poly.c[jPrev]);
    const vec3 B = cornerToWorld(J, 1, poly.c[J]);
    res.edgeNumber = poly.edgeNum[jPrev];
    vec3 nrm, pAB, pSE;
    float depth = 0;
    const bool ok = closestPoints(A, B, S, E, nrm, depth, pAB, pSE);
    // depth is computed unless the lines are parallel
    {
        const vec3 n0 = pcross(vsub(B, A), vsub(E, S));
        if ((n0.z * n0.z + n0.y * n0.y) + n0.x * n0.x > 0.0f) {
            res.depthComputed = true;
            res.depth = depth;
        }
    }
    if (!ok) return res;
    res.nrm = nrm;
    if ((wen.z * nrm.z + wen.y * nrm.y) + wen.x * nrm.x < -((cosE - kSin15) - 0.01f)) {
        res.ok = true;
        res.pos = vscale(vadd(pAB, pSE), 0.5f);
    }
    return res;
}

void normaliseList(std::vector<Record>& list) {
    for (size_t i = 0; i < list.size(); ++i) {
        Record& R = list[i];
        R.grouped = R.face = R.pair = R.fast = R.suppressed = R.done = false;
        R.m = kHuge;
        R.partner = -1;
        R.angleOk = false;
        if (R.kind == RecPolyEdge) {
            R.grouped = true;
            continue;
        }
        if (R.kind == RecEdge && !R.front) {
            std::swap(R.from, R.to);
            std::swap(R.start, R.end);
            R.T = 1.0f - R.T;
            R.front = true;
        }
        if (i > 0) {
            Record& Pv = list[i - 1];
            // A polygon-edge record still counts as the previous record (its T is 0).
            if (Pv.number == R.number && Pv.to == R.from) {
                if ((1.03f - Pv.T) < R.T) {
                    Pv.suppressed = Pv.done = true;
                    R.suppressed = R.done = true;
                } else {
                    Pv.pair = true;
                    R.pair = true;
                }
            }
        }
    }
}

void cornerGroups(std::vector<Record>& list, const Mtx& M, const Mtx& M0, float dt, bool pairStarts, float velSign,
                  float normalSign, std::vector<Impact>& out, const Mtx* O, const Mtx* O0) {
    if (list.empty()) return;
    const float fastThr = 1.5f * dt;
    const float invdt = 1.0f / dt;
    for (size_t gi = 0; gi < list.size(); ++gi) {
        if (list[gi].grouped) continue;
        if (!pairStarts && list[gi].pair) continue;
        const int v = list[gi].to;
        const vec3 dv = displacement(M, M0, list[0].end);
        // velSign < 0: the city's −v; velSign == 0: an object test's car list, (+0) − v (a zero
        // component comes out +0); velSign > 0: v. With a moving object (O, O0): car list
        // v_B − v_A, object list v_A − v_B (car-collision.md 3.2).
        const vec3 vd(dv.x * invdt, dv.y * invdt, dv.z * invdt);
        vec3 vb(0.0f, 0.0f, 0.0f);
        if (O && O0) {
            const vec3 db = displacement(*O, *O0, list[0].end);
            vb = vec3(db.x * invdt, db.y * invdt, db.z * invdt);
        }
        const vec3 vel = velSign < 0.0f   ? vec3(-vd.x, -vd.y, -vd.z)
                         : velSign == 0.0f ? vec3(vb.x - vd.x, vb.y - vd.y, vb.z - vd.z)
                                           : (O ? vec3(vd.x - vb.x, vd.y - vb.y, vd.z - vb.z) : vd);
        float best = kHuge, bestDepth = kHuge, second = kHuge, secondDepth = kHuge;
        bool found = false, blocked = false, keep = true;
        int bestIdx = -1, secondIdx = -1;
        vec3 dN, dPos;
        float dDepth = 0;
        int dPoly = -1;
        for (size_t q = 0; q < list.size(); ++q) {
            Record& R = list[q];
            if (R.kind == RecPolyEdge) continue;
            if (R.from == v && !R.done) blocked = true;
            if (R.to != v) continue;
            if (R.pair) blocked = true;
            R.grouped = true;
            float m;
            if (R.kind == RecCorner) {
                m = ((1.0f - R.T) * dt) * 0.9f;
                R.fast = true;
            } else {
                const float rn = (vel.x * R.normal.x + vel.z * R.normal.z) + vel.y * R.normal.y;
                m = rn > 0.0f ? R.depth / rn : kHuge;
                if (R.depth < 0.0f) m = 0.0f;
                R.fast = m < fastThr;
            }
            if (R.kind == RecCorner || (R.fast && m < best) ||
                (best > fastThr && R.depth < bestDepth && R.T > 0.001f && !blocked)) {
                second = best;
                secondDepth = bestDepth;
                secondIdx = bestIdx;
                best = m;
                bestDepth = R.depth;
                found = true;
                bestIdx = int(q);
                dN = R.normal;
                dDepth = R.depth;
                dPoly = R.polygon;
                if (R.kind == RecCorner) {
                    const float w = (1.0f - R.T) * 0.5f;
                    const float u = 1.0f - w;
                    dPos = vec3(u * R.end.x + w * R.start.x, u * R.end.y + w * R.start.y, u * R.end.z + w * R.start.z);
                    keep = false;
                } else if (R.fast && m > 0.00001f) {
                    const float k = m * 0.5f;
                    dPos = vec3(R.end.x + vel.x * k, R.end.y + vel.y * k, R.end.z + vel.z * k);
                    keep = R.done;
                } else {
                    const float k = R.depth * 0.5f;
                    dPos = vec3(R.end.x + R.normal.x * k, R.end.y + R.normal.y * k, R.end.z + R.normal.z * k);
                    keep = !(m > 0.00001f && !R.done);
                }
            } else if (found && R.polygon != dPoly &&
                       ((R.fast && m < second) || (second > fastThr && R.depth < secondDepth && R.T > 0.001f && !blocked))) {
                second = m;
                secondDepth = R.depth;
                secondIdx = int(q);
            }
            R.m = m;
        }
        if (!found) continue;
        if (blocked) {
            if (!keep) list[size_t(bestIdx)].face = true;
            continue;
        }
        const Record& B = list[size_t(bestIdx)];
        const bool bestCorner = B.kind == RecCorner;
        const vec3 N = dN;
        bool needSecond = false;
        for (size_t q = 0; q < list.size(); ++q) {
            Record& R = list[q];
            if (R.kind == RecPolyEdge) continue;
            vec3 d;
            bool test = false;
            if (R.to == v) {
                if (R.polygon == dPoly) {
                    R.suppressed = true;
                } else if (int(q) != bestIdx && (R.kind == RecCorner || bestCorner || R.number != B.number)) {
                    d = vsub(R.end, R.start);
                    test = true;
                }
            } else if (R.from == v && bestCorner) {
                d = vsub(R.start, R.end);
                test = true;
            }
            if (test) {
                const float qd = (d.z * N.z + d.y * N.y) + d.x * N.x;
                const float dd = (d.x * d.x + d.z * d.z) + d.y * d.y;
                if (qd < 0.0f && qd * qd > dd * kSin15) R.suppressed = true;
                else if (qd > 0.0f && qd * qd > dd * kSin15) needSecond = true;
            }
        }
        if (needSecond && secondIdx >= 0) list[size_t(secondIdx)].face = true;
        if (out.size() < kMaxImpacts) {
            Impact im;
            im.n = vec3(normalSign * N.x, normalSign * N.y, normalSign * N.z);
            im.depth = dDepth;
            im.P = dPos;
            out.push_back(im);
        }
    }
}

void pairMerge(std::vector<Record>& list, bool mergeMetric) {
    size_t i = 0;
    while (list.size() > 1 && i < list.size() - 1) {
        if (list[i].number == list[i + 1].number) {
            const bool s = list[i].suppressed || list[i + 1].suppressed;
            list[i].suppressed = list[i + 1].suppressed = s;
            if (mergeMetric) {
                const float m = list[i].m < list[i + 1].m ? list[i].m : list[i + 1].m;
                list[i].m = list[i + 1].m = m;
            }
            i += 2;
        } else {
            i += 1;
        }
    }
}

void faceImpacts(std::vector<Record>& list, float dt, float normalSign, std::vector<Impact>& out) {
    for (Record& R : list) {
        if (!R.face || R.suppressed || R.done || R.kind == RecPolyEdge) continue;
        const vec3 d = vsub(R.end, R.start);
        if (!R.fast && R.T <= 0.75f && R.depth * R.depth >= 0.04f * ((d.z * d.z + d.y * d.y) + d.x * d.x)) continue;
        if (out.size() >= kMaxImpacts) return;
        Impact im;
        im.n = vec3(normalSign * R.normal.x, normalSign * R.normal.y, normalSign * R.normal.z);
        im.depth = R.depth;
        const float k = R.depth * 0.5f;
        im.P = vec3(R.end.x + R.normal.x * k, R.end.y + R.normal.y * k, R.end.z + R.normal.z * k);
        out.push_back(im);
    }
    (void)dt;
}

}  // namespace ci

// ---------------------------------------------------------------------------------------------
// The car against city polygons (collision-contacts.md)

void cityContacts(const Bound& car, const Mtx& M, const Mtx& M0, float dt, const std::vector<PolyView>& polys,
                  const Material& carMaterial, const MaterialTable& table, std::vector<Impact>& out) {
    out.clear();
    const size_t np = car.points.size();
    std::vector<vec3> C(np), P(np);
    for (size_t k = 0; k < np; ++k) {
        const vec3& p = car.points[k];
        C[k] = vec3(((M.Z.x * p.z + M.X.x * p.x) + M.Y.x * p.y) + M.o.x, ((M.X.y * p.x + M.Z.y * p.z) + M.Y.y * p.y) + M.o.y,
                    ((M.X.z * p.x + M.Z.z * p.z) + M.Y.z * p.y) + M.o.z);
        P[k] = vec3(((M0.Y.x * p.y + M0.Z.x * p.z) + M0.X.x * p.x) + M0.o.x,
                    ((M0.Y.y * p.y + M0.Z.y * p.z) + M0.X.y * p.x) + M0.o.y,
                    ((M0.Y.z * p.y + M0.Z.z * p.z) + M0.X.z * p.x) + M0.o.z);
    }
    std::vector<float> bestT(np, 2.0f), cdepth(np, 0.0f);
    std::vector<int> cpoly(np, -1);
    const size_t ne = car.edges.size();
    std::vector<float> tMin(ne, 2.0f), tMax(ne, -1.0f), dMin(ne, 0.0f), dMax(ne, 0.0f);
    std::vector<int> pMin(ne, -1), pMax(ne, -1);
    std::vector<float> d(np);
    for (size_t i = 0; i < polys.size(); ++i) {
        const PolyView& poly = polys[i];
        for (size_t k = 0; k < np; ++k) d[k] = planeDist(poly, C[k]);
        for (size_t k = 0; k < np; ++k) {
            if (!(d[k] < 0.0f)) continue;
            const float d0 = planeDist(poly, P[k]);
            if (!(d0 > 0.0f)) continue;
            const float t = d0 / (d0 - d[k]);
            if (t < bestT[k] && lineInside2(poly, P[k], vsub(C[k], P[k]))) {
                bestT[k] = t;
                cpoly[k] = int(i);
                cdepth[k] = -d[k];
            }
        }
        if (planeDist(poly, M.o) <= car.radius) {
            for (size_t e = ne; e-- > 0;) {
                const int a = car.edges[e].first, b = car.edges[e].second;
                const float da = d[size_t(a)], db = d[size_t(b)];
                if (sbit(da) == sbit(db)) continue;
                const float t = da / (da - db);
                // A NaN (t or tMin) counts as true for < and <=, false for > and >= (step 2.3):
                // "t < tMin" is !(t >= tMin), "t <= tMin" is !(t > tMin); tMax compares as written.
                if (!((t > tMax[e] || !(t >= tMin[e])) && lineInside2(poly, C[size_t(a)], vsub(C[size_t(b)], C[size_t(a)]))))
                    continue;
                if (!(t > tMin[e]) && (da > 0.0f || !(t >= tMin[e]))) {
                    tMin[e] = t;
                    pMin[e] = int(i);
                    dMin[e] = da <= 0.0f ? da : -db;
                }
                if (t >= tMax[e] && (da < 0.0f || t > tMax[e])) {
                    tMax[e] = t;
                    pMax[e] = int(i);
                    dMax[e] = da <= 0.0f ? da : -db;
                }
            }
        }
    }
    std::vector<Record> list;
    for (size_t k = 0; k < np; ++k) {
        if (cpoly[k] < 0) continue;
        Record R;
        R.kind = RecCorner;
        R.start = P[k];
        R.end = C[k];
        R.T = bestT[k];
        R.point = vec3((R.end.x - R.start.x) * R.T + R.start.x, (R.end.y - R.start.y) * R.T + R.start.y,
                       (R.end.z - R.start.z) * R.T + R.start.z);
        R.normal = polys[size_t(cpoly[k])].normal;
        R.polygon = cpoly[k];
        R.material = polys[size_t(cpoly[k])].material;
        R.depth = cdepth[k];
        R.front = true;
        R.number = int(k);
        R.from = -1;
        R.to = int(k);
        list.push_back(R);
    }
    auto edgeRecord = [&](size_t e, float T, float dd, int poly) {
        Record R;
        R.kind = RecEdge;
        const int a = car.edges[e].first, b = car.edges[e].second;
        R.start = C[size_t(a)];
        R.end = C[size_t(b)];
        R.T = T;
        R.point = vec3((R.end.x - R.start.x) * T + R.start.x, (R.end.y - R.start.y) * T + R.start.y,
                       (R.end.z - R.start.z) * T + R.start.z);
        R.front = dd > 0.0f;
        R.depth = R.front ? dd : -dd;  // d if front, else −d (d = +0 gives −0)
        R.normal = polys[size_t(poly)].normal;
        R.polygon = poly;
        R.material = polys[size_t(poly)].material;
        R.number = int(e);
        R.from = a;
        R.to = b;
        const vec3& en = car.edgeNormal[e];
        R.wen = vec3((M.Y.x * en.y + M.Z.x * en.z) + M.X.x * en.x, (M.Y.y * en.y + M.Z.y * en.z) + M.X.y * en.x,
                     (M.Y.z * en.y + M.Z.z * en.z) + M.X.z * en.x);
        R.edgeCos = car.edgeCos[e];
        list.push_back(R);
    };
    for (size_t e = 0; e < ne; ++e) {
        if (pMin[e] < 0) continue;
        edgeRecord(e, tMin[e], dMin[e], pMin[e]);
        if (pMax[e] >= 0 && pMax[e] != pMin[e]) edgeRecord(e, tMax[e], dMax[e], pMax[e]);
    }
    if (list.empty()) return;
    const int firstMaterial = list[0].material;

    // 3a-3c.
    normaliseList(list);
    cornerGroups(list, M, M0, dt, false, -1.0f, -1.0f, out);
    pairMerge(list, false);
    // 3d. Edge against edge.
    bool anyEdge = false;
    for (const Record& R : list)
        if (R.kind == RecEdge) anyEdge = true;
    if (anyEdge) {
        const float invdt = 1.0f / dt;
        float lastDepth = 0.0f;
        auto identity = [](int, int, const vec3& c) { return c; };
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
        size_t i = 0;
        while (i < list.size()) {
            Record& Pr = list[i];
            const bool paired = Pr.kind == RecEdge && i + 1 < list.size() && list[i + 1].number == Pr.number;
            if (Pr.kind == RecEdge && !Pr.suppressed) {
                vec3 dir = vsub(Pr.end, Pr.start);
                const float dl = (dir.z * dir.z + dir.y * dir.y) + dir.x * dir.x;  // collision-contacts.md 3d step 1
                dir = dl == 0.0f ? vec3(0, 0, 0) : vscale(dir, 1.0f / fsqrt(dl));
                const vec3 dsp = displacement(M, M0, Pr.point);
                const vec3 vel(dsp.x * invdt, dsp.y * invdt, dsp.z * invdt);
                const EdgeEdgeResult r1 = edgeEdgeTest(polys[size_t(Pr.polygon)], Pr.point, dir, vel, dt, identity,
                                                       Pr.start, Pr.end, Pr.wen, Pr.edgeCos);
                if (r1.ok) {
                    Pr.done = true;
                    if (out.size() < kMaxImpacts) {
                        Impact im;
                        im.n = vec3(-r1.nrm.x, -r1.nrm.y, -r1.nrm.z);
                        im.depth = r1.depth;
                        im.P = r1.pos;
                        out.push_back(im);
                    }
                }
                clearFaces(r1, Pr, paired ? &list[i + 1] : nullptr);
                if (paired) {
                    Record& Q = list[i + 1];
                    const vec3 dq = displacement(M, M0, Q.point);
                    const vec3 velq(dq.x * invdt, dq.y * invdt, dq.z * invdt);
                    const EdgeEdgeResult r2 = edgeEdgeTest(polys[size_t(Q.polygon)], Q.point, dir, velq, dt, identity,
                                                           Q.start, Q.end, Q.wen, Q.edgeCos);
                    if (r2.ok) {
                        Pr.done = true;
                        if (!r1.ok && out.size() < kMaxImpacts) {
                            Impact im;
                            im.n = vec3(-r2.nrm.x, -r2.nrm.y, -r2.nrm.z);
                            im.depth = r2.depth;
                            im.P = r2.pos;
                            out.push_back(im);
                        }
                    }
                    clearFaces(r2, Pr, &Q);
                }
            }
            i += paired ? 2 : 1;
        }
    }
    // 3e.
    faceImpacts(list, dt, -1.0f, out);
    // 3f. One city material (the first intersection's) for all impacts.
    const Material& cityMat = table.byCode(firstMaterial);
    for (Impact& im : out) {
        im.matA = cityMat;
        im.matB = carMaterial;
    }
}

}  // namespace phys
}  // namespace cc
