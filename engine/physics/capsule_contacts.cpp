// The car against capsule props (spec/physics/collision-object-contacts.md section 5).
#include <cmath>

#include "physics/bound.h"
#include "physics/contacts.h"
#include "physics/contacts_internal.h"

namespace cc {
namespace phys {

using namespace ci;

namespace {

inline float cmp(const vec3& v, char a) { return a == 'x' ? v.x : (a == 'y' ? v.y : v.z); }
inline float dotO(const vec3& a, const vec3& b, const char* o) {
    return (cmp(a, o[0]) * cmp(b, o[0]) + cmp(a, o[1]) * cmp(b, o[1])) + cmp(a, o[2]) * cmp(b, o[2]);
}
inline const vec3& rowOf(const Mtx& m, char a) { return a == 'x' ? m.X : (a == 'y' ? m.Y : m.Z); }
// to world: component c = ((row(o0).c*p.o0 + row(o1).c*p.o1) + row(o2).c*p.o2) (+ o.c)
inline float wc(const Mtx& m, const vec3& p, char c, const char* o, bool point) {
    float s = (cmp(rowOf(m, o[0]), c) * cmp(p, o[0]) + cmp(rowOf(m, o[1]), c) * cmp(p, o[1])) +
              cmp(rowOf(m, o[2]), c) * cmp(p, o[2]);
    return point ? s + cmp(m.o, c) : s;
}
inline vec3 tw(const Mtx& m, const vec3& p, const char* ox, const char* oy, const char* oz, bool point) {
    return vec3(wc(m, p, 'x', ox, point), wc(m, p, 'y', oy, point), wc(m, p, 'z', oz, point));
}
inline vec3 intoM(const Mtx& m, const vec3& w, const char* ox, const char* oy, const char* oz) {
    return vec3(dotO(m.X, w, ox), dotO(m.Y, w, oy), dotO(m.Z, w, oz));
}
inline vec3 madd(const vec3& c, const vec3& s, float h) { return vec3(h * s.x + c.x, h * s.y + c.y, h * s.z + c.z); }

enum Kind { KEdge = 0, KFace = 1, KVertex = 2 };
enum Lookup { LEdge, LVertex, LFace };

struct Contact {
    float depth = 0;
    vec3 normal, pos;
    int part = 0, feature = 0, kind = 0;
};

// ---- geometric helpers (5.9) ----

enum SphereResult { SNone, SVertex, SEdge, SFace };

SphereResult spherePolygon(const vec3& P, float r, const vec3* V, int n, const vec3& N, vec3& Q, vec3& nrm, float& depth,
                           int& slot) {
    const float d0 = dotO(vsub(P, V[0]), N, "yzx");
    if (std::fabs(d0) > r) return SNone;  // a NaN goes on
    enum { In, Before, After };
    int mark[4] = {In, In, In, In};
    bool allIn = true, allOut = true;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const vec3 a = vsub(P, V[i]);
        const vec3 e = vsub(V[j], V[i]);
        const vec3 m(e.z * N.y - e.y * N.z, e.x * N.z - e.z * N.x, e.y * N.x - e.x * N.y);
        const float test = dotO(m, a, "zyx");
        if (test >= 0.0f) {
            mark[i] = In;
            allOut = false;
            continue;
        }
        allIn = false;
        if (!(dotO(a, e, "yzx") >= 0.0f)) {  // < 0 or NaN
            mark[i] = Before;
            continue;
        }
        const vec3 b = vsub(P, V[j]);
        if (dotO(b, e, "yzx") > 0.0f) {
            mark[i] = After;
            continue;
        }
        // This edge decides.
        const float la2 = dotO(a, a, "zyx"), lb2 = dotO(b, b, "zyx"), le2 = dotO(e, e, "yzx");
        const float D = la2 - lb2, S = lb2 + la2;
        const float h2 = (((S + S) - le2) - (D * D) / le2) * 0.25f;
        if (!(h2 > r * r)) {  // <= or NaN
            depth = r - fsqrt(h2);
            const float t2 = (la2 - h2) / le2;
            const float t = !(t2 > 0.0f) ? 0.0f : fsqrt(t2);
            Q = vec3(e.x * t + V[i].x, e.y * t + V[i].y, e.z * t + V[i].z);
            vec3 nn = vsub(P, Q);
            const float l2 = dotO(nn, nn, "xyz");
            nn = (l2 == 0.0f || std::isnan(l2)) ? vec3(0, 0, 0) : vscale(nn, 1.0f / fsqrt(l2));
            nrm = nn;
            const float k = depth * 0.5f;
            Q = vec3(Q.x - nn.x * k, Q.y - nn.y * k, Q.z - nn.z * k);
            slot = i;
            return SEdge;
        }
        return SNone;
    }
    if (allIn || allOut) {
        const float d = dotO(vsub(P, V[0]), N, "xzy");
        if (!(std::fabs(d) > r)) {  // <= or NaN
            nrm = N;
            depth = r - d;
            const float k = (d + r) * 0.5f;
            Q = vec3(P.x - N.x * k, P.y - N.y * k, P.z - N.z * k);
            return SFace;
        }
        return SNone;
    }
    int k = -1;
    for (int i = 0; i < n; ++i)
        if (mark[i] == After && mark[(i + 1) % n] == Before) {
            k = (i + 1) % n;
            break;
        }
    if (k < 0) {
        for (int i = 0; i < n; ++i)
            if (mark[i] == After) k = (i + 1) % n;
    }
    if (k < 0) k = 0;
    vec3 nn = vsub(P, V[k]);
    const float l2 = dotO(nn, nn, "xyz");
    if (l2 > r * r) return SNone;
    const float L = fsqrt(l2);
    nn = vec3(nn.x / L, nn.y / L, nn.z / L);
    depth = r - L;
    const float kk = (r - L) * 0.5f;
    Q = vec3(V[k].x - nn.x * kk, V[k].y - nn.y * kk, V[k].z - nn.z * kk);
    nrm = nn;
    slot = k;
    return SVertex;
}

// 5.9.3 Segment parameters of P0 + s D0 and Q0 + t D1. Returns true if interior.
bool segmentParams(const vec3& P0, const vec3& D0, const vec3& Q0, const vec3& D1, float& s, float& t) {
    {
        const vec3 w = vsub(P0, Q0);
        const float i2 = 1.0f / dotO(D1, D1, "xyz");
        const float q = dotO(w, D1, "yzx") * i2;
        const vec3 wp(w.x - D1.x * q, w.y - D1.y * q, w.z - D1.z * q);
        const float c = dotO(D1, D0, "zyx") * i2;
        const vec3 up(D0.x - D1.x * c, D0.y - D1.y * c, D0.z - D1.z * c);
        s = -(dotO(wp, up, "zyx") / dotO(up, up, "zxy"));
    }
    {
        const vec3 w = vsub(Q0, P0);
        const float i1 = 1.0f / dotO(D0, D0, "xyz");
        const float q = dotO(w, D0, "xyz") * i1;
        const vec3 wp(w.x - D0.x * q, w.y - D0.y * q, w.z - D0.z * q);
        const float c = dotO(D1, D0, "zyx") * i1;
        const vec3 up(D1.x - D0.x * c, D1.y - D0.y * c, D1.z - D0.z * c);
        t = -(dotO(wp, up, "zyx") / dotO(up, up, "zxy"));
    }
    if (0.0f < s && s < 1.0f && 0.0f < t && t < 1.0f) return true;
    const vec3 P1 = vadd(P0, D0), Q1 = vadd(Q0, D1);
    auto proj = [](const vec3& X0, const vec3& D, const vec3& Y) {
        const vec3 w = vsub(X0, Y);
        const float k = -dotO(w, D, "zyx");
        if (!(k > 0.0f)) return 0.0f;  // <= 0 or NaN
        const float rem = dotO(D, D, "xyz") - k;
        if (!(rem > 0.0f)) return 1.0f;
        return k / (rem + k);
    };
    auto dist = [](const vec3& X0, const vec3& D, const vec3& Y) {
        const vec3 w = vsub(X0, Y);
        const vec3 u = pcross(w, D);
        vec3 n = pcross(u, D);
        const float l2 = dotO(n, n, "zyx");
        n = (l2 == 0.0f || std::isnan(l2)) ? vec3(0, 0, 0) : vscale(n, 1.0f / fsqrt(l2));
        return -dotO(n, w, "yzx");
    };
    const float a = proj(Q0, D1, P0), b = proj(Q0, D1, P1);
    const float sQ0 = proj(P0, D0, Q0), sQ1 = proj(P0, D0, Q1);
    const float dQ0 = dist(P0, D0, Q0), dQ1 = dist(P0, D0, Q1), dP0 = dist(Q0, D1, P0), dP1 = dist(Q0, D1, P1);
    // A NaN a or b counts as 0; each <= also holds when a side is NaN.
    auto cls = [](float v) { return (v == 0.0f || std::isnan(v)) ? 0 : (v == 1.0f ? 1 : 2); };
    auto le = [](float x, float y) { return !(x > y); };
    const int ca = cls(a), cb = cls(b);
    if (ca == 0 && cb == 0) { s = sQ0; t = 0; }
    else if (ca == 0 && cb == 1) { if (le(dQ1, dQ0)) { s = sQ1; t = 1; } else { s = sQ0; t = 0; } }
    else if (ca == 0 && cb == 2) { if (le(dP1, dQ0)) { s = 1; t = b; } else { s = sQ0; t = 0; } }
    else if (ca == 1 && cb == 0) { if (le(dQ1, dQ0)) { s = sQ1; t = 1; } else { s = sQ0; t = 0; } }
    else if (ca == 1 && cb == 1) { s = sQ1; t = 1; }
    else if (ca == 1 && cb == 2) { if (le(dP1, dQ1)) { s = 1; t = b; } else { s = sQ1; t = 1; } }
    else if (ca == 2 && cb == 0) { if (le(dP0, dQ0)) { s = 0; t = a; } else { s = sQ0; t = 0; } }
    else if (ca == 2 && cb == 1) { if (le(dP0, dQ1)) { s = 0; t = a; } else { s = sQ1; t = 1; } }
    else { if (le(dP1, dP0)) { s = 1; t = b; } else { s = 0; t = a; } }
    return false;
}

enum SegEdge { SENone, SEInterior, SEEndA, SEEndB, SECapEnd };

// 5.9.2 Segment-edge test: axis E0 -> E1 (radius r) against car edge A -> B.
SegEdge segmentEdge(const vec3& E0, const vec3& E1, float r, const vec3& A, const vec3& B, vec3& nrm, float& depth,
                    vec3& X) {
    const vec3 d1 = vsub(B, A), d2 = vsub(E1, E0);
    const vec3 c = pcross(d2, d1);
    const float C2 = dotO(c, c, "zyx");
    const float thr = (dotO(d2, d2, "xzy") * dotO(d1, d1, "xyz")) * 0.0012217f;
    vec3 n;
    float dist;
    if (!(C2 >= thr)) {  // < thr or NaN: nearly parallel
        const vec3 w = vsub(E0, A);
        const vec3 u = pcross(w, d2);
        n = pcross(u, d2);
        const float l2 = dotO(n, n, "zyx");
        n = (l2 == 0.0f || std::isnan(l2)) ? vec3(0, 0, 0) : vscale(n, 1.0f / fsqrt(l2));
        dist = -((n.y * w.y + n.x * w.x) + n.z * w.z);
    } else {
        const float L = fsqrt(C2);
        n = vec3(c.x / L, c.y / L, c.z / L);
        const vec3 w = vsub(A, E0);
        dist = dotO(w, n, "xyz");
        if (!(dist >= 0.0f)) {  // < 0 or NaN
            dist = -dist;
            n = vec3(-n.x, -n.y, -n.z);
        }
    }
    if (dist > r) return SENone;
    float s = 0, t = 0;
    const bool interior = segmentParams(A, d1, E0, d2, s, t);
    if (interior) {
        nrm = vec3(-n.x, -n.y, -n.z);
        depth = r - dist;
        X = vec3(d2.x * t + E0.x, d2.y * t + E0.y, d2.z * t + E0.z);
        return SEInterior;
    }
    if (!(t > 0.0f && t < 1.0f)) return SECapEnd;  // t <= 0, t >= 1 or NaN
    const bool endA = !(s >= 0.5f);                // s < 0.5 or NaN
    const vec3 end = endA ? A : B;
    X = vec3(d2.x * t + E0.x, d2.y * t + E0.y, d2.z * t + E0.z);
    vec3 nn = vsub(end, X);
    const float l2 = dotO(nn, nn, "zyx");
    if (l2 > r * r) return SENone;
    const float L = fsqrt(l2);
    depth = r - L;
    nrm = vec3(nn.x / -L, nn.y / -L, nn.z / -L);
    return endA ? SEEndA : SEEndB;
}

// ---- the capsule probe (5.8) ----

struct CapsuleGeom {
    float r, L, h, r2;
};

// Cap test from start point P with direction d, half g (+1 upper, -1 lower).
int capTest(const CapsuleGeom& cg, const vec3& P, const vec3& d, float g, float& tA, float& tB) {
    const float b = (P.y * d.y + d.z * P.z) + P.x * d.x;
    const float c0 = (P.x * P.x + P.y * P.y) + P.z * P.z;
    const float a = (d.x * d.x + d.y * d.y) + d.z * d.z;
    const float D = ((b * b) / a - (c0 - cg.r2)) / a;
    if (!(D >= 0.0f)) return 0;
    auto right = [&](float t) { return ((t * d.y) + P.y) * g > 0.0f; };
    const bool startIn = !(c0 > cg.r2);
    const vec3 pe = vadd(P, d);
    const bool endIn = !(((pe.z * pe.z + pe.x * pe.x) + pe.y * pe.y) > cg.r2);
    const float sq = fsqrt(D), ba = b / a;
    if (startIn) {
        if (endIn) return 0;
        tA = sq - ba;
        return right(tA) ? 1 : 0;
    }
    const float tn = (-ba) - sq;
    tA = tn;
    if (!(tn >= 0.0f) || tn > 1.0f) return 0;
    const float tf = sq - ba;
    if (right(tn)) {
        tB = tf;
        return (tf > 1.0f || !right(tf)) ? 1 : 2;
    }
    tA = tf;
    if (tf > 1.0f) return 0;
    return right(tf) ? 1 : 0;
}

int rayTest(const CapsuleGeom& cg, const vec3& A, const vec3& d, float& t1, int& part1) {
    float t2 = 0, u1 = 0, u2 = 0;
    int part2 = 0;
    t1 = 0;
    part1 = 0;
    // Cylinder test.
    int n = 0;
    {
        const float c = (A.x * A.x + A.z * A.z) - cg.r2;
        const bool startIn = !(c >= 0.0f);
        const bool endIn = !(((d.z + A.z) * (d.z + A.z) + (d.x + A.x) * (d.x + A.x)) >= cg.r2);
        if (startIn && endIn) {
            n = 0;
        } else {
            const float b = d.x * A.x + d.z * A.z;
            const float a = d.z * d.z + d.x * d.x;
            const float D = ((b * b) / a - c) / a;
            if (!(D >= 0.0f)) {
                n = 0;
            } else {
                const float s = fsqrt(D), bp = b / a;
                auto U = [&](float t) { return ((d.y * t + A.y) / cg.L) + 0.5f; };
                if (startIn) {
                    t1 = s - bp;
                    u1 = U(t1);
                    n = 1;
                } else {
                    t1 = (-bp) - s;
                    if (!(t1 >= 0.0f) || t1 > 1.0f) {
                        n = 0;
                    } else {
                        t2 = s - bp;
                        u1 = U(t1);
                        if (endIn) {
                            n = 1;
                        } else {
                            u2 = U(t2);
                            n = 2;
                        }
                    }
                }
            }
        }
    }
    float scratch = 0;
    const vec3 Atop(A.x, A.y - cg.L, A.z);
    auto Bottom = [&](float& x, float& y) { return capTest(cg, A, d, -1.0f, x, y); };
    auto Top = [&](float& x, float& y) { return capTest(cg, Atop, d, 1.0f, x, y); };
    auto inBand = [](float u) { return 0.0f <= u && u <= 1.0f; };
    auto below = [](float u) { return !(u >= 0.0f); };
    auto sortH = [&]() {
        if (t1 > t2) {
            std::swap(t1, t2);
            std::swap(u1, u2);
            std::swap(part1, part2);
        }
    };
    if (n == 0 || (n == 1 && !inBand(u1))) {
        int k = Bottom(t1, t2);
        if (k == 2) {
            part1 = part2 = 0;
            return 2;
        }
        if (k == 1) {
            part1 = 0;
            if (Top(t2, scratch) > 0) {
                part2 = 1;
                sortH();
                return 2;
            }
            return 1;
        }
        k = Top(t1, t2);
        if (k > 0) part1 = part2 = 1;
        return k;
    }
    if (n == 1) {
        part1 = 2;
        if (Bottom(t2, scratch) > 0) {
            part2 = 0;
            sortH();
            return 2;
        }
        if (Top(t2, scratch) > 0) {
            part2 = 1;
            sortH();
            return 2;
        }
        return 1;
    }
    // n == 2
    if (inBand(u1) && inBand(u2)) {
        part1 = part2 = 2;
        return 2;
    }
    if (below(u1) && below(u2)) {
        const int k = Bottom(t1, t2);
        if (k > 0) part1 = part2 = 0;
        return k;
    }
    if (u1 > 1.0f && u2 > 1.0f) {
        const int k = Top(t1, t2);
        if (k > 0) part1 = part2 = 1;
        return k;
    }
    if ((below(u1) && u2 > 1.0f) || (u1 > 1.0f && below(u2))) {
        if (Bottom(t1, scratch) == 0) {
            Top(t1, scratch);
            part1 = 1;
            return 1;
        }
        part1 = 0;
        if (Top(t2, scratch) > 0) {
            part2 = 1;
            sortH();
            return 2;
        }
        return 1;
    }
    if (inBand(u1)) {
        part1 = 2;
        if (below(u2)) {
            Bottom(t2, scratch);
            part2 = 0;
        } else {
            Top(t2, scratch);
            part2 = 1;
        }
        sortH();
        return 2;
    }
    if (below(u1)) {
        Bottom(t1, scratch);
        part1 = 0;
    } else {
        Top(t1, scratch);
        part1 = 1;
    }
    part2 = 2;
    sortH();
    return 2;
}

bool capsuleProbe(const CapsuleGeom& cg, const vec3& S, const vec3& E, vec3& point, vec3& normal, float& pen,
                  int& part) {
    const float h = cg.h, r = cg.r, hr = h + r;
    const float rho2 = S.x * S.x + S.z * S.z;
    // A NaN S.y, or a NaN right side in either r·r < … test, counts as outside.
    const bool outside = std::isnan(S.y) || hr < S.y || -hr > S.y || rho2 > r * r ||
                         (S.y > h && !(r * r >= (S.y - h) * (S.y - h) + rho2)) ||
                         (-h > S.y && !(r * r >= (h + S.y) * (h + S.y) + rho2));
    if (!outside) return false;
    const vec3 A(S.x, h + S.y, S.z);
    const vec3 d = vsub(E, S);
    float t1 = 0;
    int part1 = 0;
    const int n = rayTest(cg, A, d, t1, part1);
    if (n == 0 || t1 > 2.0f) return false;
    const vec3 P(d.x * t1 + S.x, d.y * t1 + S.y, d.z * t1 + S.z);
    vec3 nn;
    if (part1 == 2) nn = vec3(P.x, 0.0f, P.z);
    else if (part1 == 1) nn = vec3(P.x, P.y - h, P.z);
    else nn = vec3(P.x, h + P.y, P.z);
    const float l2 = dotO(nn, nn, "zxy");
    nn = (l2 == 0.0f || std::isnan(l2)) ? vec3(0, 0, 0) : vscale(nn, 1.0f / fsqrt(l2));
    point = P;
    normal = nn;
    pen = dotO(vsub(P, E), nn, "zyx");
    part = part1;
    return true;
}

// ---- contact list management (5.3) ----

class ContactList {
public:
    explicit ContactList(const Mtx& H) : H_(H) {}
    std::vector<Contact> c;
    int count() const { return int(c.size()); }
    bool full() const { return c.size() >= 200; }

    int duplicate(Lookup type, int P, int F) const {
        for (size_t i = 0; i < c.size(); ++i) {
            const Contact& I = c[i];
            switch (type) {
                case LEdge:
                    if (I.kind == KEdge && I.part == P && I.feature == F) return int(i);
                    break;
                case LVertex:
                    if ((I.kind == KFace && I.part == P && I.feature == F) || (I.kind == KVertex && I.part == F && I.feature == P))
                        return int(i);
                    break;
                case LFace:
                    if ((I.kind == KFace && I.part == F && I.feature == P) || (I.kind == KVertex && I.part == P && I.feature == F))
                        return int(i);
                    break;
            }
        }
        return -1;
    }

    // (a − b)·n < tol; a NaN counts as not in front.
    static bool notInFront(const vec3& a, const vec3& b, const vec3& n, float tol) {
        return !(dotZYX(vsub(a, b), n) >= tol);
    }
    static vec3 centre(const vec3& pos, const vec3& n, float depth) {
        const float k = depth * 0.5f;
        return vec3(n.x * k + pos.x, n.y * k + pos.y, n.z * k + pos.z);
    }

    bool firstMerge(const Contact& N) {
        const vec3 cN = centre(N.pos, N.normal, N.depth);
        size_t i = 0;
        while (i < c.size()) {
            const Contact& I = c[i];
            const vec3 cI = centre(I.pos, I.normal, I.depth);
            if (I.part == N.part && notInFront(cN, cI, I.normal, I.depth * 0.01f) &&
                notInFront(cI, cN, N.normal, N.depth * 0.01f)) {
                if (I.depth >= N.depth) return false;
                c.erase(c.begin() + long(i));
            } else {
                ++i;
            }
        }
        i = 0;
        while (i < c.size()) {
            const Contact& I = c[i];
            const vec3 cI = centre(I.pos, I.normal, I.depth);
            if (I.part == N.part && notInFront(cI, cN, N.normal, N.depth * 0.01f)) c.erase(c.begin() + long(i));
            else ++i;
        }
        return true;
    }

    bool secondMerge(const Contact& N) {
        const vec3 cN = centre(N.pos, N.normal, N.depth);
        const vec3 hc = H_.o, ax = H_.Y;
        const float sN = dotYXZ(vsub(cN, hc), ax);
        auto flatN = [&](const Contact& I) {
            const float t = dotYZX(I.normal, ax);
            if (t != 0.0f && !std::isnan(t)) {
                const vec3 u(I.normal.x - ax.x * t, I.normal.y - ax.y * t, I.normal.z - ax.z * t);
                const float l2 = dotZYX(u, u);
                return (l2 == 0.0f || std::isnan(l2)) ? vec3(0, 0, 0) : vscale(u, 1.0f / fsqrt(l2));
            }
            return I.normal;
        };
        size_t i = 0;
        while (i < c.size()) {
            const Contact& I = c[i];
            const vec3 fl = flatN(I);
            const float side = dotYXZ(vsub(I.pos, hc), ax) * sN;
            const vec3 cI = centre(I.pos, fl, I.depth);
            if (side > 0.0f && notInFront(cN, cI, fl, I.depth * 0.01f)) {
                if (!notInFront(cI, cN, N.normal, N.depth * 0.01f)) return false;
                if (I.depth >= N.depth) return false;
                c.erase(c.begin() + long(i));
            } else {
                ++i;
            }
        }
        i = 0;
        while (i < c.size()) {
            const Contact& I = c[i];
            const vec3 fl = flatN(I);
            const float side = dotYXZ(vsub(I.pos, hc), ax) * sN;
            const vec3 cI = centre(I.pos, fl, I.depth);
            if (side > 0.0f && notInFront(cI, cN, N.normal, N.depth * 0.01f)) c.erase(c.begin() + long(i));
            else ++i;
        }
        return true;
    }

    // Commit: duplicate search, merge rule, then count it. A duplicate drops the new contact.
    bool commit(Contact N, Lookup type, int kind, int feature, bool second) {
        if (!c.empty() && duplicate(type, N.part, feature) >= 0) return false;
        if (!c.empty()) {
            const bool ok = second ? secondMerge(N) : firstMerge(N);
            if (!ok) return false;
        }
        N.kind = kind;
        N.feature = feature;
        c.push_back(N);
        return true;
    }

private:
    Mtx H_;
};

}  // namespace

void capsuleObjectContacts(const Bound& car, const Mtx& M, const Mtx& M0, float r, float L, const Mtx& H,
                           const Material& carMaterial, const Material& propMaterial, std::vector<Impact>& out,
                           const Mtx* H0) {
    out.clear();
    CapsuleGeom cg;
    cg.r = r;
    cg.L = L;
    cg.h = L * 0.5f;
    cg.r2 = r * r;
    const float h = cg.h;
    // 5.2 Setup.
    const vec3 d = vsub(M.o, H.o);
    const vec3 cc(-dotO(M.X, d, "xzy"), -dotO(M.Y, d, "zyx"), -dotO(M.Z, d, "zyx"));
    const vec3 a = intoM(M, H.Y, "zyx", "zyx", "zyx");
    const vec3 s0(-a.x, -a.y, -a.z), s1 = a;
    const vec3 E0 = madd(cc, s0, h), E1 = madd(cc, s1, h);
    // (A moving capsule: w and l with its previous matrix H0, q turned back by H's rows.)
    const Mtx& Hp = H0 ? *H0 : H;
    const vec3 w = vsub(Hp.o, M0.o);
    const vec3 l = intoM(Hp, w, "yzx", "yzx", "yzx");
    const vec3 q = tw(H, l, "zyx", "zxy", "zxy", false);
    const vec3 disp = vadd(d, q);
    const vec3 v = intoM(M, disp, "zyx", "zyx", "zyx");
    const float V2 = (v.z * v.z + v.y * v.y) + v.x * v.x;
    bool F[2] = {false, false};
    ContactList list(H);
    auto writeCar = [&](const vec3& Q, const vec3& n, float depth, int part) {
        Contact C;
        C.pos = tw(M, Q, "zxy", "zyx", "zyx", true);
        C.normal = tw(M, n, "xzy", "xzy", "xzy", false);
        C.depth = depth;
        C.part = part;
        return C;
    };

    // 5.4 Pass 1: car polygons against the end spheres.
    for (size_t pi = 0; pi < car.polys.size() && !list.full(); ++pi) {
        const BoundPoly& bp = car.polys[pi];
        vec3 V[4];
        for (int k = 0; k < bp.n; ++k) V[k] = car.points[size_t(bp.idx[k])];
        const vec3& N = bp.normal;
        for (int j = 0; j < 2 && !list.full(); ++j) {
            const vec3& Pj = j == 0 ? E0 : E1;
            const vec3& sj = j == 0 ? s0 : s1;
            vec3 Q, nrm;
            float depth = 0;
            int slot = 0;
            const SphereResult res = spherePolygon(Pj, r, V, bp.n, N, Q, nrm, depth, slot);
            int kind = KFace, feature = 0;
            Lookup lk = LFace;
            bool keep = false;
            if (res == SVertex) {
                keep = dotO(vsub(Q, Pj), sj, "xzy") >= 0.0f;
                feature = bp.idx[slot];
                lk = LVertex;
                kind = KVertex;
            } else if (res == SEdge) {
                keep = dotO(vsub(Q, Pj), sj, "xzy") >= 0.0f;
                feature = bp.edgeNum[slot];
                lk = LEdge;
                kind = KEdge;
            } else if (res == SFace) {
                keep = !(dotO(N, sj, "xzy") > 0.06f);  // a NaN passes
                feature = int(pi);
                lk = LFace;
                kind = KFace;
            } else {
                if (dotO(N, v, "zyx") > 0.0f && !(dotO(N, sj, "xzy") >= 0.1f)) {  // NaN: fails / passes
                    // Swept rescue: one-sided segment test of Pj + v -> Pj against the polygon.
                    PolyView pv;
                    pv.n = bp.n;
                    for (int k = 0; k < bp.n; ++k) {
                        pv.c[k] = V[k];
                        pv.edgeN[k] = bp.edgeNormal[k];
                    }
                    pv.normal = N;
                    const vec3 S = vadd(Pj, v);
                    const vec3& E = Pj;
                    const float dS = dotZXY(vsub(S, V[0]), N);
                    bool hit = false;
                    if (!(dS < 0.0f)) {
                        const float dE = dotZXY(vsub(E, V[0]), N);
                        if (!(dE >= 0.0f)) {
                            const float T = dS / (dS - dE);
                            if (!(T > 2.0f) && lineInside1(pv, S, vsub(E, S))) hit = true;
                        }
                    }
                    if (hit) {
                        nrm = N;
                        const vec3 Q0(Pj.x - N.x * r, Pj.y - N.y * r, Pj.z - N.z * r);
                        depth = dotO(vsub(V[0], Q0), N, "zyx");
                        const float k = depth * 0.5f;
                        Q = vec3(Q0.x + N.x * k, Q0.y + N.y * k, Q0.z + N.z * k);
                        feature = int(pi);
                        lk = LFace;
                        kind = KFace;
                        keep = true;
                    }
                }
            }
            if (!keep) continue;
            if (list.commit(writeCar(Q, nrm, depth, j), lk, kind, feature, false)) F[j] = true;
        }
    }

    // 5.5 Pass 2: car edges against the axis.
    for (size_t e = 0; e < car.edges.size() && !list.full(); ++e) {
        const int ia = car.edges[e].first, ib = car.edges[e].second;
        const vec3& A = car.points[size_t(ia)];
        const vec3& B = car.points[size_t(ib)];
        vec3 nrm, X;
        float depth = 0;
        const SegEdge res = segmentEdge(E0, E1, r, A, B, nrm, depth, X);
        int feature, kind;
        Lookup lk;
        if (res == SEInterior) {
            feature = int(e);
            kind = KEdge;
            lk = LEdge;
        } else if (res == SEEndA || res == SEEndB) {
            feature = res == SEEndA ? ia : ib;
            kind = KVertex;
            lk = LVertex;
        } else {
            continue;
        }
        const float k = r - depth * 0.5f;
        vec3 Q(X.x - nrm.x * k, X.y - nrm.y * k, X.z - nrm.z * k);
        if (!(dotO(nrm, v, "xzy") >= 0.0f) && V2 > r * r) {  // nrm·v < 0, NaN too
            depth = (r + r) - depth;
            Q = vec3(Q.x + nrm.x * r, Q.y + nrm.y * r, Q.z + nrm.z * r);
            nrm = vec3(-nrm.x, -nrm.y, -nrm.z);
        }
        if (list.commit(writeCar(Q, nrm, depth, 2), lk, kind, feature, true)) {
            const vec3 xe = vsub(X, E0);
            if (dotO(xe, xe, "zyx") <= h * h) F[0] = true;
            else F[1] = true;
        }
    }

    // 5.6 Pass 3: the axis pierces a car polygon.
    bool pass3Made = false;
    if (!list.full()) {
        float tMin = 2.0f, tMax = -1.0f;
        int hits = 0, slot0 = -1, slot1 = -1;
        for (size_t pi = car.polys.size(); pi-- > 0;) {
            const BoundPoly& bp = car.polys[pi];
            PolyView pv;
            pv.n = bp.n;
            for (int k = 0; k < bp.n; ++k) {
                pv.c[k] = car.points[size_t(bp.idx[k])];
                pv.edgeN[k] = bp.edgeNormal[k];
            }
            pv.normal = bp.normal;
            const float dS = dotYXZ(vsub(E0, pv.c[0]), pv.normal);
            const float dE = dotYXZ(vsub(E1, pv.c[0]), pv.normal);
            if (sbit(dS) == sbit(dE)) continue;
            const float t = dS / (dS - dE);
            if (tMin < t && t < tMax) continue;
            if (!lineInside2(pv, E0, vsub(E1, E0))) continue;
            const bool front = !(dS <= 0.0f);
            hits += 1;
            if (t <= tMin && (front || t < tMin)) {
                slot0 = int(pi);
                tMin = t;
            }
            if (t >= tMax && (!front || t > tMax)) {
                slot1 = int(pi);
                tMax = t;
            }
        }
        std::vector<int> kept;
        if (hits >= 1) kept.push_back(slot0);
        if (hits >= 2 && slot1 != slot0) kept.push_back(slot1);
        const vec3 dAx = vsub(E1, E0);
        for (int pi : kept) {
            if (list.full()) break;
            const BoundPoly& bp = car.polys[size_t(pi)];
            float best = kHuge;
            bool found = false, inside = false;
            int chosen = -1;
            vec3 nrm, Q;
            for (int k = 0; k < bp.n; ++k) {
                const int e = bp.edgeNum[k];
                const vec3& Va = car.points[size_t(car.edges[size_t(e)].first)];
                const vec3 De = vsub(car.points[size_t(car.edges[size_t(e)].second)], Va);
                float s = 0, t = 0;
                segmentParams(Va, De, E0, dAx, s, t);
                const vec3 Pe(De.x * s + Va.x, De.y * s + Va.y, De.z * s + Va.z);
                const vec3 Pa(dAx.x * t + E0.x, dAx.y * t + E0.y, dAx.z * t + E0.z);
                const vec3 g = vsub(Pa, Pe);
                const float G2 = dotO(g, g, "zyx");
                bool take = false;
                float m = 0;
                if (G2 <= V2 || std::isnan(G2)) {  // MM2 treats NaN as near
                    m = -dotO(g, v, "zyx");
                    if (!inside || m > best) {
                        take = true;
                        inside = true;
                    }
                } else {
                    const bool Fh = t > 0.5f ? F[1] : F[0];  // a NaN t gives F0
                    if (!Fh && !inside && G2 < best) {
                        take = true;
                        m = G2;
                    }
                }
                if (take) {
                    best = m;
                    chosen = e;
                    nrm = vec3(-g.x, -g.y, -g.z);
                    Q = Pe;
                    found = true;
                }
            }
            if (!found) continue;
            if (list.count() > 0 && list.duplicate(LEdge, 2, chosen) >= 0) continue;
            const float Lr = fsqrt(dotO(nrm, nrm, "xyz"));
            nrm = vec3(nrm.x / Lr, nrm.y / Lr, nrm.z / Lr);
            const float depth = Lr + r;
            const float k = depth * 0.5f;
            Q = vec3(Q.x - nrm.x * k, Q.y - nrm.y * k, Q.z - nrm.z * k);
            Contact C = writeCar(Q, nrm, depth, 2);
            C.kind = KEdge;
            C.feature = chosen;
            list.c.push_back(C);
            pass3Made = true;
        }
    }

    // 5.7 Pass 4: swept car points.
    if (V2 > r * r && !list.full() && !pass3Made) {
        for (size_t k = 0; k < car.points.size(); ++k) {
            const vec3 W = tw(M, car.points[k], "zyx", "zyx", "zyx", true);
            const vec3 S = vsub(W, disp);
            const vec3 wc0 = vsub(W, H.o), ws0 = vsub(S, H.o);
            const vec3 cur = intoM(H, wc0, "xzy", "zyx", "zyx");
            const vec3 prev = intoM(H, ws0, "xzy", "zyx", "zyx");
            vec3 pt, nh;
            float pen = 0;
            int part = 0;
            if (!capsuleProbe(cg, prev, cur, pt, nh, pen, part)) continue;
            if (list.count() > 0) {
                const int di = list.duplicate(LVertex, part, int(k));
                if (di >= 0) list.c.erase(list.c.begin() + di);
            }
            Contact C;
            C.depth = pen;
            const vec3 nw = tw(H, nh, "xzy", "zyx", "zyx", false);
            C.normal = vec3(-nw.x, -nw.y, -nw.z);
            C.pos = tw(H, pt, "xzy", "zyx", "zyx", true);
            C.part = part;
            if (list.count() > 0) {
                const bool ok = part == 2 ? list.secondMerge(C) : list.firstMerge(C);
                if (!ok) continue;
            }
            C.kind = KVertex;
            C.feature = int(k);
            list.c.push_back(C);
        }
    }

    // 5.10 Motion filter and hand-over.
    if (list.count() > 1) {
        const float D2 = dotO(disp, disp, "xyz");
        const float thr = D2 * 0.01f;
        auto dotN = [&](const Contact& C) { return dotO(C.normal, disp, "xzy"); };
        int first = -1;
        for (int i = 0; i < list.count(); ++i) {
            const float dn = dotN(list.c[size_t(i)]);
            if (dn * dn > thr) {
                first = i;
                break;
            }
        }
        if (first >= 0) {
            list.c.erase(list.c.begin(), list.c.begin() + first);
            size_t i = 1;
            while (i < list.c.size()) {
                const float dn = dotN(list.c[i]);
                if (!(dn * dn > thr)) list.c.erase(list.c.begin() + long(i));  // a NaN is deleted
                else ++i;
            }
        }
    }
    for (const Contact& C : list.c) {
        Impact im;
        im.P = C.pos;
        im.n = vec3(-C.normal.x, -C.normal.y, -C.normal.z);
        im.depth = C.depth;
        im.matA = carMaterial;
        im.matB = propMaterial;
        out.push_back(im);
    }
}

}  // namespace phys
}  // namespace cc
