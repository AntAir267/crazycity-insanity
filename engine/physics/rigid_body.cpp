#include "physics/rigid_body.h"

#include <cfloat>
#include <cmath>

#include "core/log.h"

namespace cc {
namespace phys {

namespace {

const float kBig = 3.4028235e38f;

Mat3 inverse3(const Mat3& M) {
    const float a = M.m[0][0], b = M.m[0][1], c = M.m[0][2];
    const float d = M.m[1][0], e = M.m[1][1], f = M.m[1][2];
    const float g = M.m[2][0], h = M.m[2][1], i = M.m[2][2];
    const float c00 = e * i - h * f;
    const float c01 = d * i - g * f;
    const float c02 = h * d - g * e;
    const float det = c02 * c + (c00 * a - c01 * b);
    if (det == 0.0f) return M;
    const float r = 1.0f / det;
    Mat3 o;
    o.m[0][0] = r * c00;
    o.m[0][1] = -((b * i - h * c) * r);
    o.m[0][2] = (b * f - c * e) * r;
    o.m[1][0] = -(r * c01);
    o.m[1][1] = (a * i - g * c) * r;
    o.m[1][2] = -((a * f - d * c) * r);
    o.m[2][0] = r * c02;
    o.m[2][1] = -((h * a - g * b) * r);
    o.m[2][2] = (e * a - d * b) * r;
    return o;
}

// Products in MM2's per-element term orders (vehicle-body.md, "Products"): order a, b, c means
// (t[a] + t[b]) + t[c].
const int kMulOrder[3][3][3] = {{{1, 0, 2}, {1, 2, 0}, {1, 2, 0}},
                                {{1, 2, 0}, {1, 2, 0}, {1, 0, 2}},
                                {{1, 0, 2}, {1, 0, 2}, {2, 1, 0}}};
const int kMulTOrder[3][3][3] = {{{1, 0, 2}, {2, 0, 1}, {1, 2, 0}},
                                 {{1, 2, 0}, {0, 2, 1}, {2, 1, 0}},
                                 {{0, 1, 2}, {0, 2, 1}, {2, 1, 0}}};

inline float sum3(const float t[3], const int o[3]) { return (t[o[0]] + t[o[1]]) + t[o[2]]; }

}  // namespace

Mat3 mulProducts(const Mat3& A, const Mat3& B) {
    Mat3 P;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float t[3] = {A.m[i][0] * B.m[0][j], A.m[i][1] * B.m[1][j], A.m[i][2] * B.m[2][j]};
            P.m[i][j] = sum3(t, kMulOrder[i][j]);
        }
    return P;
}

namespace {

// A * B^T
Mat3 mulTransposed(const Mat3& A, const Mat3& B) {
    Mat3 Q;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float t[3] = {A.m[i][0] * B.m[j][0], A.m[i][1] * B.m[j][1], A.m[i][2] * B.m[j][2]};
            Q.m[i][j] = sum3(t, kMulTOrder[i][j]);
        }
    return Q;
}

// x * M (row vector times matrix): y.x order 2,1,0; y.y and y.z order 2,0,1.
vec3 rowTimes(const vec3& x, const Mat3& M) {
    return vec3((x.z * M.m[2][0] + x.y * M.m[1][0]) + x.x * M.m[0][0],
                (x.z * M.m[2][1] + x.x * M.m[0][1]) + x.y * M.m[1][1],
                (x.z * M.m[2][2] + x.x * M.m[0][2]) + x.y * M.m[1][2]);
}

// M x (matrix times column): y.x order 2,1,0; y.y and y.z order 2,0,1.
vec3 timesCol(const Mat3& M, const vec3& x) {
    return vec3((M.m[0][2] * x.z + M.m[0][1] * x.y) + M.m[0][0] * x.x,
                (M.m[1][2] * x.z + M.m[1][0] * x.x) + M.m[1][1] * x.y,
                (M.m[2][2] * x.z + M.m[2][0] * x.x) + M.m[2][1] * x.y);
}

// L = w * Iw: L.x order 1,2,0; L.y and L.z order 0,1,2.
vec3 angularMomentum(const vec3& w, const Mat3& Iw) {
    return vec3((w.y * Iw.m[1][0] + w.z * Iw.m[2][0]) + w.x * Iw.m[0][0],
                (w.x * Iw.m[0][1] + w.y * Iw.m[1][1]) + w.z * Iw.m[2][1],
                (w.x * Iw.m[0][2] + w.y * Iw.m[1][2]) + w.z * Iw.m[2][2]);
}

// Rot(axis, a): c and s in double precision, s rounded to float, c not; t = float(1 - c); the
// diagonal float((x*x)*t + c) with the unrounded c.
Mat3 rotAxis(const vec3& axis, float a) {
    const double cd = std::cos(double(a));
    const float s = float(std::sin(double(a)));
    const float t = float(1.0 - cd);
    const float x = axis.x, y = axis.y, z = axis.z;
    Mat3 r;
    r.m[0][0] = float(double((x * x) * t) + cd);
    r.m[0][1] = s * z + (y * x) * t;
    r.m[0][2] = (z * x) * t - s * y;
    r.m[1][0] = (y * x) * t - s * z;
    r.m[1][1] = float(double((y * y) * t) + cd);
    r.m[1][2] = s * x + (z * y) * t;
    r.m[2][0] = s * y + (z * x) * t;
    r.m[2][1] = (z * y) * t - s * x;
    r.m[2][2] = float(double((z * z) * t) + cd);
    return r;
}

}  // namespace

Mat3 rotAboutAxis(const vec3& axis, float a) { return rotAxis(axis, a); }

Mat3 inverseMat3(const Mat3& M) { return inverse3(M); }


void RigidBody::reset() {
    R = M3::identity();
    p = v = w = P = L = vec3();
    F = T = Fpost = Tpost = G = H = vec3();
    A = B = C = Mat3();
    implicit = false;
    push = pushSum = lastPush = turn = vec3();
}

void RigidBody::setMass(float mass, const vec3& inertia) {
    m = mass;
    invM = mass <= 0.0f ? kBig : 1.0f / mass;
    I = inertia;
    invI.x = inertia.x <= 0.0f ? kBig : 1.0f / inertia.x;
    invI.y = inertia.y <= 0.0f ? kBig : 1.0f / inertia.y;
    invI.z = inertia.z <= 0.0f ? kBig : 1.0f / inertia.z;
}

void RigidBody::integrate(float h) {
    F = vadd(F, Fpost);
    T = vadd(T, Tpost);

    if (!implicit) {
        P = vadd(P, G);
        P = vmadd(P, F, h);
        v = vscale(P, invM);
        L = vadd(L, H);
        L = vmadd(L, T, h);
        float wb[3];
        const float Ii[3] = {I.x, I.y, I.z};
        const float iI[3] = {invI.x, invI.y, invI.z};
        bool clamped = false;
        for (int i = 0; i < 3; ++i) {
            wb[i] = ((R.r[i].z * L.z + R.r[i].y * L.y) + R.r[i].x * L.x) * iI[i];
            if (wb[i] < -wmax_) {
                wb[i] = -wmax_;
                clamped = true;
            } else if (wb[i] > wmax_) {
                wb[i] = wmax_;
                clamped = true;
            }
        }
        if (clamped) {
            L = vscale(R.r[0], wb[0] * Ii[0]);
            L = vmadd(L, R.r[1], wb[1] * Ii[1]);
            L = vmadd(L, R.r[2], wb[2] * Ii[2]);
        }
        w = vscale(R.r[0], wb[0]);
        w = vmadd(w, R.r[1], wb[1]);
        w = vmadd(w, R.r[2], wb[2]);
    } else {
        const float q = h * invM;
        Mat3 M1;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) M1.m[i][j] = A.m[i][j] * q;
        for (int i = 0; i < 3; ++i) M1.m[i][i] = M1.m[i][i] + 1.0f;
        const Mat3 T1 = inverse3(M1);
        const Mat3 BT1 = mulProducts(B, T1);
        Mat3 Jm = mulTransposed(BT1, B);
        const float sc = -(q * h);
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) Jm.m[i][j] = Jm.m[i][j] * sc;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) Jm.m[i][j] = Jm.m[i][j] + h * C.m[i][j];
        // Iw = (R^T diag(I)) R
        Mat3 Tm;
        const float Ii[3] = {I.x, I.y, I.z};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) Tm.m[i][j] = Ii[j] * R.at(j, i);
        const Mat3 Iw = mulProducts(Tm, toMat3(R));
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) Jm.m[i][j] = Jm.m[i][j] + Iw.m[i][j];
        const vec3 Pe = vmadd(G, F, h);
        vec3 r = vscale(Pe, -q);
        r = rowTimes(r, T1);
        r = timesCol(B, r);
        r = vadd(r, H);
        r = vmadd(r, T, h);
        const vec3 dw = solveRowSystem(Jm.m, r);  // collision.md, "Solving x·K = b"
        vec3 u = vscale(dw, h);
        u = rowTimes(u, B);
        u = vadd(u, Pe);
        u = vscale(u, invM);
        u = rowTimes(u, T1);
        v = vadd(v, u);
        P = vscale(v, m);
        w = vadd(w, dw);
        L = angularMomentum(w, Iw);
    }

    const float vv = pdot(v, v);
    if (vv > vmax * vmax) {
        const float k = (1.0f / fsqrt(vv)) * vmax;
        v = vscale(v, k);
        P = vscale(P, k);
    }
    p = vadd(p, push);
    p = vmadd(p, v, h);
    turn = vmadd(turn, w, h);
    const float tt = pdot(turn, turn);
    if (tt > 1e-15f) {
        const float a = fsqrt(tt);
        const vec3 axis = vscale(turn, 1.0f / a);
        R = fromMat3(mulProducts(toMat3(R), rotAxis(axis, a)));
    }
    F = T = G = H = vec3();
    pushSum = vadd(pushSum, push);
    push = vec3();
    turn = vec3();
    implicit = false;
    A = B = C = Mat3();

    Fpost = Tpost = vec3();
    lastPush = pushSum;
    pushSum = vec3();
}

void RigidBody::integrateFrozen() {
    F = vadd(F, Fpost);
    T = vadd(T, Tpost);
    Fpost = Tpost = vec3();
    lastPush = pushSum;
    pushSum = vec3();
}

void RigidBody::applyPush() {
    p = vadd(p, push);
    pushSum = vadd(pushSum, push);
    push = vec3();
}

void RigidBody::addForceAt(const vec3& f, const vec3& x) {
    F = vadd(F, f);
    T = vadd(T, pcross(vsub(x, p), f));
}

void RigidBody::addImpulseAt(const vec3& j, const vec3& x) {
    G = vadd(G, j);
    H = vadd(H, pcross(vsub(x, p), j));
}

void RigidBody::addImplicitContact(const vec3& x, const vec3& f, const Mat3& M) {
    implicit = true;
    Fpost = vadd(Fpost, f);
    const vec3 r = vsub(x, p);
    Tpost = vadd(Tpost, pcross(r, f));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) A.m[i][j] = A.m[i][j] + M.m[i][j];
    Mat3 S;
    S.m[0][0] = 0.0f;
    S.m[0][1] = -r.z;
    S.m[0][2] = r.y;
    S.m[1][0] = r.z;
    S.m[1][1] = 0.0f;
    S.m[1][2] = -r.x;
    S.m[2][0] = -r.y;
    S.m[2][1] = r.x;
    S.m[2][2] = 0.0f;
    const Mat3 SM = mulProducts(S, M);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) B.m[i][j] = B.m[i][j] + SM.m[i][j];
    for (int i = 0; i < 3; ++i) {
        const vec3 row(SM.m[i][0], SM.m[i][1], SM.m[i][2]);
        const vec3 c = pcross(r, row);
        C.m[i][0] = C.m[i][0] + c.x;
        C.m[i][1] = C.m[i][1] + c.y;
        C.m[i][2] = C.m[i][2] + c.z;
    }
}

void RigidBody::addPush(const vec3& d) {
    const float s = (push.y * d.y + push.z * d.z) + push.x * d.x;
    if (s < 0.0f) {
        push = vec3(d.x + push.x, d.y + push.y, d.z + push.z);
    } else {
        const float e2 = (d.x * d.x + d.y * d.y) + d.z * d.z;
        if (e2 > 0.0f) {
            const float k = s / e2;
            const vec3 e(k * d.x - d.x, k * d.y - d.y, k * d.z - d.z);
            if ((e.z * d.z + e.y * d.y) + e.x * d.x < 0.0f) push = vsub(push, e);
        }
    }
}

vec3 RigidBody::velocityAt(const vec3& x, float invdt) const {
    const vec3 r = vsub(x, p);
    vec3 vel(r.z * w.y - w.z * r.y, w.z * r.x - r.z * w.x, w.x * r.y - w.y * r.x);
    vel = vadd(vel, v);
    const vec3& l = lastPush;
    const float lp = (l.z * l.z + l.y * l.y) + l.x * l.x;
    if (lp > 0.0001f) {
        const float d = ((l.z * vel.z + l.y * vel.y) + l.x * vel.x) * invdt;
        const float q = (invdt * invdt) * lp;
        float s = invdt;
        if (-d <= q) s = -(d / q) * invdt;
        vel = vmadd(vel, lastPush, s);
    }
    return vel;
}

void RigidBody::inverseMassMatrix(const vec3& P, float K[3][3]) const {
    const vec3 r = vsub(P, p);
    vec3 a[3], w3[3];
    const float iI[3] = {invI.x, invI.y, invI.z};
    for (int k = 0; k < 3; ++k) {
        const vec3& Rk = R.r[k];
        a[k] = vec3(Rk.y * r.z - r.y * Rk.z, r.x * Rk.z - Rk.x * r.z, r.y * Rk.x - Rk.y * r.x);
        w3[k] = vec3(a[k].x * iI[k], a[k].y * iI[k], a[k].z * iI[k]);
    }
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float t0 = (&a[0].x)[i] * (&w3[0].x)[j];
            const float t1 = (&a[1].x)[i] * (&w3[1].x)[j];
            const float t2 = (&a[2].x)[i] * (&w3[2].x)[j];
            float s;
            if (j == 0 && (i == 0 || i == 1)) s = (t0 + t1) + t2;
            else if (j == 0 && i == 2) s = (t0 + t2) + t1;
            else s = (t1 + t2) + t0;
            K[i][j] = s;
        }
    for (int i = 0; i < 3; ++i) K[i][i] = K[i][i] + invM;
}

float RigidBody::invMassAlong(const vec3& x, const vec3& n) const {
    float K[3][3];
    inverseMassMatrix(x, K);
    float u[3];
    for (int j = 0; j < 3; ++j) u[j] = (K[0][j] * n.x + K[2][j] * n.z) + K[1][j] * n.y;
    return (u[0] * n.x + u[2] * n.z) + u[1] * n.y;
}

vec3 solveRowSystem(const float K[3][3], const vec3& b) {
    const float m0 = K[0][0], m1 = K[0][1], m2 = K[0][2];
    const float m3 = K[1][0], m4 = K[1][1], m5 = K[1][2];
    const float m6 = K[2][0], m7 = K[2][1], m8 = K[2][2];
    const float ms[9] = {m0, m1, m2, m3, m4, m5, m6, m7, m8};
    float maxAbs = 0;
    int row = 1;
    for (int i = 0; i < 9; ++i)
        if (std::fabs(ms[i]) > maxAbs) {
            maxAbs = std::fabs(ms[i]);
            row = i / 3 + 1;
        }
    if (maxAbs == 0.0f) return vec3(0, 0, 0);
    const float eps = maxAbs * 0.0001f;
    const float C00 = m8 * m4 - m7 * m5, C01 = -(m8 * m3 - m6 * m5), C02 = m7 * m3 - m6 * m4;
    const float C10 = -(m8 * m1 - m2 * m7), C11 = m8 * m0 - m6 * m2, C12 = -(m7 * m0 - m6 * m1);
    const float C20 = m5 * m1 - m4 * m2, C21 = -(m5 * m0 - m2 * m3), C22 = m4 * m0 - m3 * m1;
    auto mx3 = [](float a, float b2, float c) {
        float r = std::fabs(a);
        if (std::fabs(b2) > r) r = std::fabs(b2);
        if (std::fabs(c) > r) r = std::fabs(c);
        return r;
    };
    const float R0 = mx3(C00, C01, C02), R1 = mx3(C10, C11, C12), R2 = mx3(C20, C21, C22);
    float Rmax = R0;
    if (R1 > Rmax) Rmax = R1;
    if (R2 > Rmax) Rmax = R2;
    const float det = (C02 * m2 + C01 * m1) + C00 * m0;
    const bool rowsOk = row == 1 ? (R1 > eps && R2 > eps) : (R0 > eps && R2 > eps);
    if (std::fabs(det) > eps * eps && std::fabs(det) > Rmax * eps && rowsOk) {
        const float inv = 1.0f / det;
        return vec3(((C02 * b.z + C01 * b.y) + C00 * b.x) * inv, ((C12 * b.z + C11 * b.y) + C10 * b.x) * inv,
                    ((C22 * b.z + C21 * b.y) + C20 * b.x) * inv);
    }
    static bool warned = false;
    if (!warned) {
        warned = true;
        logWarn("physics: singular 3x3 solve (the spec says this never happens for a car)");
    }
    return vec3(0, 0, 0);
}

}  // namespace phys
}  // namespace cc
