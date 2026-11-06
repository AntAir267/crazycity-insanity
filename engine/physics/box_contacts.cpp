// Box against box (spec/physics/car-collision-boxes.md, offset case; the zero-offset copy of
// section 8 differs only in sum groupings and is not needed by the shipped data).
#include "physics/box_contacts.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "physics/contacts_internal.h"

namespace cc {
namespace phys {

using namespace ci;

namespace {

inline float C(const vec3& v, int i) { return i == 0 ? v.x : (i == 1 ? v.y : v.z); }
inline void setC(vec3& v, int i, float x) { (i == 0 ? v.x : (i == 1 ? v.y : v.z)) = x; }
inline const vec3& rowOf(const Mtx& m, int k) { return k == 0 ? m.X : (k == 1 ? m.Y : m.Z); }
inline int ax(char c) { return c == 'x' ? 0 : (c == 'y' ? 1 : 2); }

// Dot product a·b with the terms in the given order.
inline float dotO(const vec3& a, const vec3& b, const char* o) {
    float s = C(a, ax(o[0])) * C(b, ax(o[0]));
    s = s + C(a, ax(o[1])) * C(b, ax(o[1]));
    return s + C(a, ax(o[2])) * C(b, ax(o[2]));
}
// w turned by M; output component c sums M.row_k.c·w.k in order oc.
inline float turnC(const Mtx& M, const vec3& w, int c, const char* o) {
    float s = C(rowOf(M, ax(o[0])), c) * C(w, ax(o[0]));
    s = s + C(rowOf(M, ax(o[1])), c) * C(w, ax(o[1]));
    return s + C(rowOf(M, ax(o[2])), c) * C(w, ax(o[2]));
}
inline vec3 turn(const Mtx& M, const vec3& w, const char* ox, const char* oy, const char* oz) {
    return vec3(turnC(M, w, 0, ox), turnC(M, w, 1, oy), turnC(M, w, 2, oz));
}
inline vec3 place(const Mtx& M, const vec3& w, const char* ox, const char* oy, const char* oz) {
    const vec3 t = turn(M, w, ox, oy, oz);
    return vec3(t.x + M.o.x, t.y + M.o.y, t.z + M.o.z);
}
inline vec3 intoM(const Mtx& M, const vec3& w, const char* o) {
    return vec3(dotO(M.X, w, o), dotO(M.Y, w, o), dotO(M.Z, w, o));
}
inline bool signBitF(float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    return (u >> 31) != 0;
}

const int kSigma[8][3] = {{1, 1, 1}, {-1, 1, 1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, -1}, {1, -1, -1}};
const int kEdge[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
const int kFaceCorners[6][4] = {{4, 0, 3, 7}, {2, 1, 5, 6}, {1, 0, 4, 5}, {7, 3, 2, 6}, {3, 0, 1, 2}, {5, 4, 7, 6}};
const int kFaceEdges[6][4] = {{8, 3, 11, 7}, {1, 9, 5, 10}, {0, 8, 4, 9}, {11, 2, 10, 6}, {3, 0, 1, 2}, {4, 7, 6, 5}};
const int kFaceView[6][2] = {{1, 2}, {2, 1}, {2, 0}, {0, 2}, {0, 1}, {1, 0}};
const int kCornerEdges[8][3] = {{0, 3, 8}, {0, 9, 1}, {2, 1, 10}, {3, 2, 11}, {4, 8, 7}, {4, 5, 9}, {5, 6, 10}, {6, 7, 11}};
const int kEdgeFaces[12][2] = {{2, 4}, {1, 4}, {3, 4}, {0, 4}, {2, 5}, {1, 5}, {3, 5}, {0, 5}, {0, 2}, {1, 2}, {1, 3}, {0, 3}};
const int kDeepest[8] = {6, 7, 5, 4, 2, 3, 1, 0};

vec3 faceNormal(int f) {
    vec3 n(0, 0, 0);
    setC(n, f / 2, (f % 2) == 0 ? 1.0f : -1.0f);
    return n;
}

struct Slot {
    int kind = 0, f0 = 0, f1 = 0;
    vec3 P, n;
    float depth = 0;
    bool aFirst = true;
};

struct Test {
    // inputs
    vec3 eA, eB, cA, cB;
    Mtx MA, MA0, MB, MB0;
    float dt = 0;
    // state
    float R[3][3];
    int s[3][3], sg[3][3];  // coarse and exact signs
    unsigned T[12][12];
    int N = 0;
    float bestTime = 0, bestDepth = 0, fast = 0;
    Slot slot[8];
    bool slot0Written = false;
    vec3 K[8];       // current pass corners
    vec3 Kpass2[8];  // B's corners in A's frame (for the edge pass)
    vec3 qA, pA, qB, pB;
};

// ---- 2.2 ----
void signs(Test& t) {
    int z = 0;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float x = t.R[i][j];
            if (x > 0.035f) {
                t.s[i][j] = 1;
                t.sg[i][j] = 1;
            } else if (x > 0.0f) {
                t.s[i][j] = 0;
                t.sg[i][j] = 1;
            } else if (x < -0.035f || std::isnan(x)) {
                t.s[i][j] = -1;
                t.sg[i][j] = -1;
            } else if (x < 0.0f) {
                t.s[i][j] = 0;
                t.sg[i][j] = -1;
            } else {
                t.s[i][j] = 0;
                t.sg[i][j] = 0;
            }
            if (t.s[i][j] == 0) ++z;
        }
    auto promote = [&]() {
        float best = 0.0f;
        int bi = -1, bj = -1;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (t.s[i][j] == 0 && t.sg[i][j] != 0 && std::fabs(t.R[i][j]) > best) {
                    best = std::fabs(t.R[i][j]);
                    bi = i;
                    bj = j;
                }
        if (bi >= 0) t.s[bi][bj] = t.sg[bi][bj];
    };
    auto balanced = [&]() {
        for (int i = 0; i < 3; ++i) {
            int zr = 0, zc = 0;
            for (int j = 0; j < 3; ++j) {
                if (t.s[i][j] == 0) ++zr;
                if (t.s[j][i] == 0) ++zc;
            }
            if (zr < 1 || zr > 2 || zc < 1 || zc > 2) return false;
        }
        return true;
    };
    if (z == 2) promote();
    else if (z == 3) {
        promote();
        promote();
    } else if (z == 4) {
        if (!balanced()) {
            promote();
            promote();
            promote();
        }
    } else if (z == 5) {
        float best = 0.0f;
        int bi = -1, bj = -1;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                if (t.s[i][j] == 0 && t.sg[i][j] != 0 && std::fabs(t.R[i][j]) > best) {
                    t.s[i][j] = 1;
                    if (balanced()) {
                        bi = i;
                        bj = j;
                        best = std::fabs(t.R[i][j]);
                    }
                    t.s[i][j] = 0;
                }
        if (bi >= 0) t.s[bi][bj] = t.sg[bi][bj];
        else t.sg[2][0] = -1;  // MM2 writes outside its table
    }
}

// ---- 3 ----
void corners(const vec3 a[3], const vec3& e, const vec3& tc, vec3 K[8]) {
    const vec3 al(a[0].x * e.x, a[0].y * e.x, a[0].z * e.x);
    const vec3 be(a[1].x * e.y, a[1].y * e.y, a[1].z * e.y);
    const vec3 ga(a[2].x * e.z, a[2].y * e.z, a[2].z * e.z);
    const vec3 u(be.x + al.x, be.y + al.y, be.z + al.z);
    const vec3 w(be.x - al.x, be.y - al.y, be.z - al.z);
    K[0] = vec3(u.x + ga.x, u.y + ga.y, u.z + ga.z);
    K[1] = vec3(w.x + ga.x, w.y + ga.y, w.z + ga.z);
    K[2] = vec3(ga.x - u.x, ga.y - u.y, ga.z - u.z);
    K[3] = vec3(ga.x - w.x, ga.y - w.y, ga.z - w.z);
    K[4] = vec3(-K[2].x, -K[2].y, -K[2].z);
    K[5] = vec3(-K[3].x, -K[3].y, -K[3].z);
    K[6] = vec3(-K[0].x, -K[0].y, -K[0].z);
    K[7] = vec3(-K[1].x, -K[1].y, -K[1].z);
    for (int k = 0; k < 8; ++k) K[k] = vec3(K[k].x + tc.x, K[k].y + tc.y, K[k].z + tc.z);
}

struct Pass {
    int index;     // 1 or 2
    vec3 eV, eF, cF;
    vec3 pV;
    vec3 a[3];     // V's axes in F's coordinates
    const Mtx *MV, *MV0, *MF, *MF0;
    bool aFirst;   // sides of its impacts
};

int entryS(const Test& t, const Pass& p, int i, int k) { return p.index == 1 ? t.s[i][k] : t.s[k][i]; }
int entrySg(const Test& t, const Pass& p, int i, int k) { return p.index == 1 ? t.sg[i][k] : t.sg[k][i]; }

void mark(Test& t, const Pass& p, int vEdge, int fEdge, unsigned m) {
    if (p.index == 1) t.T[vEdge][fEdge] |= m;
    else t.T[fEdge][vEdge] |= m;
}
void markCorner(Test& t, const Pass& p, int u, int f) {
    const unsigned m = p.index == 1 ? 1u : 2u;
    for (int k = 0; k < 3; ++k)
        for (int j = 0; j < 4; ++j) mark(t, p, kCornerEdges[u][k], kFaceEdges[f][j], m);
}
void markEdge(Test& t, const Pass& p, int eps, int f) {
    for (int j = 0; j < 4; ++j) mark(t, p, eps, kFaceEdges[f][j], 4u);
}

// ---- 5 ----
bool offer(Test& t, const vec3& P, const vec3& n, float d, const Mtx& MV, const Mtx& MV0, const Mtx& MF, const Mtx& MF0) {
    const vec3 dV = displacement(MV, MV0, P);
    const vec3 dF = displacement(MF, MF0, P);
    const vec3 df(dF.x - dV.x, dF.y - dV.y, dF.z - dV.z);
    const float rho = (df.z * n.z + df.y * n.y) + df.x * n.x;
    float dd = d;  // skin 0
    float tau;
    if (rho > 0.0f) {
        if (dd > 0.0f) tau = dd / rho;
        else {
            tau = 0.0f;
            dd = 0.0f;
        }
    } else {
        tau = FLT_MAX;
    }
    if (t.N > 0) {
        const bool fastBest = !(t.bestTime >= t.fast);  // best time < fast, NaN: yes
        if ((fastBest && !(t.bestTime > tau)) || !(t.bestDepth > dd)) return false;
    }
    t.bestTime = tau;
    t.bestDepth = dd;
    return true;
}

// ---- 4.3.1 ----
bool clipLine(const vec3& X, const vec3& E, const vec3& n, const vec3& e, float& t0, float& t1, int& k0, int& k1) {
    int u, v;
    if (n.x > 0.0f) u = 1, v = 2;
    else if (n.x < 0.0f || std::isnan(n.x)) u = 2, v = 1;
    else if (n.y > 0.0f) u = 2, v = 0;
    else if (n.y < 0.0f) u = 0, v = 2;
    else if (n.z > 0.0f) u = 0, v = 1;
    else u = 1, v = 0;
    const float xu = C(X, u), xv = C(X, v), du = C(E, u), dv = C(E, v), hu = C(e, u), hv = C(e, v);
    int count = 0;
    auto add = [&](float t, int k) {
        if (count == 0) {
            t0 = t1 = t;
            k0 = k1 = k;
        } else if (!(t >= t0)) {
            t0 = t;
            k0 = k;
        } else if (t > t1) {
            t1 = t;
            k1 = k;
        }
        ++count;
    };
    auto le = [](float a, float b) { return !(a > b); };  // a <= b, NaN: yes
    if (du != 0.0f && !std::isnan(du)) {
        const float i = 1.0f / du;
        float t = (hu - xu) * i;
        if (le(std::fabs(dv * t + xv), hv)) add(t, 0);
        t = ((-hu) - xu) * i;
        if (le(std::fabs(dv * t + xv), hv)) add(t, 2);
    }
    if (dv != 0.0f && !std::isnan(dv) && count < 2) {
        const float i = 1.0f / dv;
        float t = (hv - xv) * i;
        if (le(std::fabs(t * du + xu), hu)) add(t, 1);
        if (count < 2) {
            t = ((-hv) - xv) * i;
            if (le(std::fabs(t * du + xu), hu)) add(t, 3);
        }
    }
    if (count == 0) {
        t0 = t1 = 0.5f;
        return false;
    }
    if (!(t0 > 0.0f) && !(t1 > 0.0f)) return false;
    if (!(t0 >= 1.0f) || !(t1 >= 1.0f)) return true;
    return false;
}

// ---- 4.2.1 the overlap walk ----
struct WalkRecord {
    int type;  // 0 crossing, 1 P corner, 2 Q corner
    int i, j;
    float tP, tQ;
};

struct Walk {
    float P[4][2], Q[4][2];
    std::vector<WalkRecord> rec;
    bool stop = false;
    int i = 0, j = 0;
    bool walkerQ = true;
    float hw = 0, tw = 0, ho = 0, to = 0;
    int a = 4, b = 4, budget = 16;

    static float sideOf(const float W[4][2], int k, const float X[2]) {
        const int k1 = (k + 1) & 3;
        const float Eu = W[k1][0] - W[k][0], Ev = W[k1][1] - W[k][1];
        const float Ck = Eu * W[k][1] - Ev * W[k][0];
        return (X[1] * Eu - Ev * X[0]) - Ck;
    }
    // Side values with the current walker.
    float calcHw() const {
        return walkerQ ? sideOf(P, i, Q[(j + 1) & 3]) : sideOf(Q, j, P[(i + 1) & 3]);
    }
    float calcHo() const {
        return walkerQ ? sideOf(Q, j, P[(i + 1) & 3]) : sideOf(P, i, Q[(j + 1) & 3]);
    }
    float calcTo() const { return walkerQ ? sideOf(Q, j, P[i]) : sideOf(P, i, Q[j]); }

    void advance() {
        if (walkerQ) {
            j = (j + 1) & 3;
            --b;
        } else {
            i = (i + 1) & 3;
            --a;
        }
        if (!rec.empty() && (a < 0 || b < 0 || (a <= 0 && b <= 0))) stop = true;
    }
    void swap() {
        walkerQ = !walkerQ;
        hw = ho;
        tw = to;
    }
    void crossing() {
        if (!rec.empty() && rec[0].type == 0 && rec[0].i == i && rec[0].j == j) {
            stop = true;
            return;
        }
        if (rec.empty()) a = b = 4;
        WalkRecord r;
        r.type = 0;
        r.i = i;
        r.j = j;
        const float tWalker = tw / (tw - hw);
        const float tOther = to / (to - ho);
        if (walkerQ) {
            r.tQ = tWalker;
            r.tP = tOther;
        } else {
            r.tP = tWalker;
            r.tQ = tOther;
        }
        rec.push_back(r);
    }
    void corner(bool ofP, int idx) {
        if (rec.empty()) {
            a = b = 4;
        } else if (rec[0].type == (ofP ? 1 : 2) && (ofP ? rec[0].i : rec[0].j) == idx) {
            stop = true;
            return;
        }
        WalkRecord r;
        r.type = ofP ? 1 : 2;
        r.i = ofP ? idx : 0;
        r.j = ofP ? 0 : idx;
        r.tP = r.tQ = 0;
        rec.push_back(r);
    }
    void walkerCorner() { corner(!walkerQ, walkerQ ? j : i); }
    void otherCorner() { corner(walkerQ, walkerQ ? i : j); }
    void allCorners(bool ofP) {
        for (int k = 0; k < 4; ++k) {
            WalkRecord r;
            r.type = ofP ? 1 : 2;
            r.i = ofP ? k : 0;
            r.j = ofP ? 0 : k;
            r.tP = r.tQ = 0;
            rec.push_back(r);
        }
    }

    void run() {
        i = j = 0;
        walkerQ = true;
        hw = sideOf(P, 0, Q[1]);
        tw = sideOf(P, 0, Q[0]);
        ho = sideOf(Q, 0, P[1]);
        to = sideOf(Q, 0, P[0]);
        int phase = 0;  // 1 S1, 2 S2, 3 T1, 4 T2
        if (hw >= 0.0f) {
            if (!(ho >= 0.0f)) {
                if (!(to > 0.0f) || tw >= 0.0f) phase = 1;
                else if (!(hw != 0.0f)) phase = 4;
                else {
                    crossing();
                    phase = 3;
                }
            } else {
                if (!(hw >= tw)) phase = 1;
                else if (!(ho >= to)) {
                    swap();
                    phase = 1;
                } else {
                    return;
                }
            }
        } else {
            if (!(ho >= 0.0f)) {
                phase = 2;
                if (!(to >= ho)) swap();
            } else {
                if (!(to >= 0.0f) && tw > 0.0f) {
                    if (!(ho != 0.0f)) phase = 4;
                    else {
                        crossing();
                        phase = 3;
                    }
                } else {
                    phase = 1;
                }
                swap();
            }
        }
        if (stop) return;
        while (!stop) {
            if (phase == 1) {
                bool low = !(hw >= tw);
                for (;;) {
                    budget -= 1;
                    if (budget <= 0) {
                        allCorners(walkerQ);  // the other polygon's corners
                        stop = true;
                        return;
                    }
                    advance();
                    if (stop) return;
                    tw = hw;
                    hw = calcHw();
                    if (!(hw >= 0.0f)) {
                        to = calcTo();
                        ho = calcHo();
                        if (!(to >= 0.0f)) {
                            int next;
                            if (!(ho > 0.0f)) {
                                next = (ho == 0.0f || std::isnan(ho)) ? 4 : 2;
                            } else {
                                next = 3;
                                if (!(tw > 0.0f)) walkerCorner();
                                else crossing();
                                if (stop) return;
                            }
                            swap();
                            phase = next;
                            break;
                        }
                        low = false;
                        swap();
                        continue;
                    }
                    if (low) {
                        if (hw >= tw) return;  // no records
                        continue;
                    }
                    low = !(hw >= tw);
                }
            } else if (phase == 2) {
                for (;;) {
                    budget -= 1;
                    if (budget <= 0) {
                        allCorners(!walkerQ);  // the walker's polygon's corners
                        stop = true;
                        return;
                    }
                    advance();
                    if (stop) return;
                    tw = hw;
                    ho = calcHo();
                    if (ho >= 0.0f) {
                        to = calcTo();
                        swap();
                        phase = 1;
                        break;
                    }
                    hw = calcHw();
                    if (!(hw >= 0.0f)) continue;
                    to = calcTo();
                    if (to > 0.0f) {
                        if (!(hw != 0.0f)) phase = 4;
                        else {
                            crossing();
                            phase = 3;
                        }
                        break;
                    }
                    if (to != 0.0f && !std::isnan(to)) {
                        phase = 1;
                        break;
                    }
                    if (hw > 0.0f) {
                        otherCorner();
                        phase = 3;
                        break;
                    }
                }
                if (stop) return;
            } else if (phase == 3) {
                for (;;) {
                    advance();
                    if (stop) return;
                    tw = hw;
                    hw = calcHw();
                    if (hw >= 0.0f) continue;
                    ho = calcHo();
                    int next = 3;
                    if (ho > 0.0f) {
                        if (tw > 0.0f) {
                            to = calcTo();
                            crossing();
                        } else {
                            walkerCorner();
                        }
                        if (stop) return;
                    } else {
                        next = 4;
                    }
                    swap();
                    if (next == 4) {
                        phase = 4;
                        break;
                    }
                }
            } else if (phase == 4) {
                for (;;) {
                    advance();
                    if (stop) return;
                    tw = hw;
                    ho = calcHo();
                    walkerCorner();
                    if (stop) return;
                    if (ho >= 0.0f) {
                        swap();
                        phase = 3;
                        break;
                    }
                    hw = calcHw();
                    if (hw > 0.0f) {
                        if (!(tw >= 0.0f)) {
                            to = calcTo();
                            crossing();
                            if (stop) return;
                        }
                        phase = 3;
                        break;
                    }
                }
            } else {
                return;
            }
        }
    }
};

// ---- 4: the face pass ----
void facePass(Test& t, Pass& p) {
    for (int f = 0; f < 6 && t.N >= 0; ++f) {
        const vec3 n = faceNormal(f);
        const int k = f / 2;
        if (!((n.x * p.pV.x + n.y * p.pV.y) + n.z * p.pV.z > 0.0f)) continue;
        const int g = (f % 2) == 0 ? -1 : 1;
        int s[3], b[3];
        for (int i = 0; i < 3; ++i) {
            s[i] = g * entryS(t, p, i, k);
            b[i] = entrySg(t, p, i, k) >= 0 ? 1 : 0;
            if ((f % 2) == 0) b[i] = 1 - b[i];
        }
        const int c = kDeepest[b[0] + 2 * b[1] + 4 * b[2]];
        const vec3& v = t.K[c];
        const float hk = std::fabs((n.z * p.eF.z + n.y * p.eF.y) + n.x * p.eF.x);
        const float d = hk - ((n.z * v.z + n.y * v.y) + n.x * v.x);
        if (!(d >= 0.0f)) {
            t.N = -1;
            return;
        }
        const int zeros = (s[0] == 0) + (s[1] == 0) + (s[2] == 0);
        const Mtx& MF = *p.MF;
        auto nw = [&]() { return turn(MF, n, "zyx", "zyx", "yzx"); };
        auto placeF = [&](const vec3& l) { return place(MF, l, "xzy", "zyx", "zyx"); };
        if (zeros == 0) {
            // 4.1 Corner against face.
            int pc = (k + 1) % 3, qc = (k + 2) % 3;
            if (pc > qc) std::swap(pc, qc);
            const bool inside = !(std::fabs(C(v, pc)) > C(p.eF, pc)) && !(std::fabs(C(v, qc)) > C(p.eF, qc));
            if (inside) {
                const vec3 n_w = nw();
                const float r = d * 0.5f;
                const vec3 Pl((n.x * r + v.x) + p.cF.x, (n.y * r + v.y) + p.cF.y, (n.z * r + v.z) + p.cF.z);
                const vec3 P = placeF(Pl);
                if (offer(t, P, n_w, d, *p.MV, *p.MV0, MF, *p.MF0)) {
                    Slot& sl = t.slot[0];
                    sl.kind = 1;
                    sl.f0 = c;
                    sl.f1 = f;
                    sl.P = P;
                    sl.n = n_w;
                    sl.depth = d;
                    sl.aFirst = p.aFirst;
                    t.slot0Written = true;
                    t.N = 1;
                } else {
                    const float hk2 = std::fabs((n_w.z * p.eF.z + n_w.y * p.eF.y) + n_w.x * p.eF.x);
                    for (int u = 0; u < 8; ++u) {
                        if (u == c) continue;
                        const vec3& Ku = t.K[u];
                        if (!((n_w.x * Ku.x + n_w.z * Ku.z) + n_w.y * Ku.y >= hk2)) markCorner(t, p, u, f);
                    }
                }
            }
            markCorner(t, p, c, f);
        } else if (zeros == 1) {
            // 4.3 Edge against face.
            static const struct {
                int s0, s1, s2, e;
            } kTab[12] = {{1, 1, 0, 8},  {1, 0, 1, 3},  {1, 0, -1, 7},  {1, -1, 0, 11}, {0, 1, 1, 0},   {0, 1, -1, 4},
                          {0, -1, 1, 2}, {0, -1, -1, 6}, {-1, 1, 0, 9}, {-1, 0, 1, 1},  {-1, 0, -1, 5}, {-1, -1, 0, 10}};
            int eps = -1;
            for (const auto& r : kTab)
                if (r.s0 == s[0] && r.s1 == s[1] && r.s2 == s[2]) eps = r.e;
            if (eps < 0) continue;
            markEdge(t, p, eps, f);
            const int o = kEdge[eps][0] == c ? kEdge[eps][1] : kEdge[eps][0];
            const vec3 E(t.K[o].x - v.x, t.K[o].y - v.y, t.K[o].z - v.z);
            float t0 = 0, t1 = 0;
            int k0 = 0, k1 = 0;
            if (!clipLine(v, E, n, p.eF, t0, t1, k0, k1)) {
                markCorner(t, p, c, f);
                markCorner(t, p, o, f);
                continue;
            }
            if (!(t0 >= 0.0f)) t0 = 0.0f;
            if (t1 > 1.0f) t1 = 1.0f;
            const vec3 X0(E.x * t0 + v.x, E.y * t0 + v.y, E.z * t0 + v.z);
            const vec3 X1(E.x * t1 + v.x, E.y * t1 + v.y, E.z * t1 + v.z);
            const float hk3 = std::fabs((n.z * p.eF.z + n.y * p.eF.y) + n.x * p.eF.x);
            const float d1 = hk3 - ((n.z * X1.z + n.y * X1.y) + X1.x * n.x);
            const float d0 = hk3 - ((n.z * X0.z + n.y * X0.y) + X0.x * n.x);
            const vec3 n_w = nw();
            auto nearPoint = [&](const vec3& X, float dd) {
                const float r = dd * 0.5f;
                return placeF(vec3((n.x * r + X.x) + p.cF.x, (n.y * r + X.y) + p.cF.y, (n.z * r + X.z) + p.cF.z));
            };
            if (d0 >= 0.0f && d1 >= 0.0f) {
                const vec3 P0 = nearPoint(X0, d0);
                const vec3 W = placeF(vec3(X1.x + p.cF.x, X1.y + p.cF.y, X1.z + p.cF.z));
                const float r1 = d1 * 0.5f;
                const vec3 P1(n_w.x * r1 + W.x, n_w.y * r1 + W.y, n_w.z * r1 + W.z);
                const vec3 Y((P1.x + P0.x) * 0.5f, (P1.y + P0.y) * 0.5f, (P1.z + P0.z) * 0.5f);
                if (!offer(t, Y, n_w, (d1 + d0) * 0.5f, *p.MV, *p.MV0, MF, *p.MF0)) continue;
                Slot& s0 = t.slot[0];
                Slot& s1 = t.slot[1];
                s0.P = P0;
                s0.n = n_w;
                s0.depth = d0;
                s1.P = P1;
                s1.n = n_w;
                s1.depth = d1;
                if (t0 == 0.0f || std::isnan(t0)) {
                    s0.kind = 1;
                    s0.f0 = c;
                    s0.f1 = f;
                    markCorner(t, p, c, f);
                } else {
                    s0.kind = 0;
                    s0.f0 = eps;
                    s0.f1 = k0;
                }
                if (!(t1 > t0)) {
                    t.N = 1;  // slot 0's sides keep what the slot held (the pass's if never written)
                    if (!t.slot0Written) s0.aFirst = p.aFirst;
                    t.slot0Written = true;
                    continue;
                }
                if (t1 == 1.0f || std::isnan(t1)) {
                    s1.kind = 1;
                    s1.f0 = o;
                    s1.f1 = f;
                    markCorner(t, p, o, f);
                } else {
                    s1.kind = 0;
                    s1.f0 = eps;
                    s1.f1 = k1;
                }
                s0.aFirst = s1.aFirst = p.aFirst;
                t.slot0Written = true;
                t.N = 2;
            } else if (d0 >= 0.0f) {
                const vec3 P0 = nearPoint(X0, d0);
                int kind, f0, f1;
                if (t0 == 0.0f || std::isnan(t0)) {
                    kind = 1;
                    f0 = c;
                    f1 = f;
                    markCorner(t, p, c, f);
                } else {
                    kind = 0;
                    f0 = eps;
                    f1 = k0;
                }
                if (offer(t, P0, n_w, d0, *p.MV, *p.MV0, MF, *p.MF0)) {
                    Slot& sl = t.slot[0];
                    sl.kind = kind;
                    sl.f0 = f0;
                    sl.f1 = f1;
                    sl.P = P0;
                    sl.n = n_w;
                    sl.depth = d0;
                    sl.aFirst = p.aFirst;
                    t.slot0Written = true;
                    t.N = 1;
                }
            } else {
                if (!(d1 >= 0.0f)) {
                    t.N = -1;
                    return;
                }
                const vec3 P1 = nearPoint(X1, d1);
                int kind, f0, f1;
                if (t1 == 1.0f || std::isnan(t1)) {
                    kind = 1;
                    f0 = o;
                    f1 = f;
                    markCorner(t, p, o, f);
                } else {
                    kind = 0;
                    f0 = eps;
                    f1 = k1;
                }
                if (offer(t, P1, n_w, d1, *p.MV, *p.MV0, MF, *p.MF0)) {
                    Slot& sl = t.slot[0];
                    sl.kind = kind;
                    sl.f0 = f0;
                    sl.f1 = f1;
                    sl.P = P1;
                    sl.n = n_w;
                    sl.depth = d1;
                    sl.aFirst = p.aFirst;
                    t.slot0Written = true;
                    t.N = 1;
                }
            }
        } else {
            markCorner(t, p, c, f);
            if (p.index != 1) continue;
            // 4.2 Face against face.
            vec3 L[8];
            for (int q = 0; q < 4; ++q)
                L[q] = vec3(float(kSigma[q][0]) * p.eF.x, float(kSigma[q][1]) * p.eF.y, float(kSigma[q][2]) * p.eF.z);
            L[4] = vec3(-L[2].x, -L[2].y, -L[2].z);
            L[5] = vec3(-L[3].x, -L[3].y, -L[3].z);
            L[6] = vec3(-L[0].x, -L[0].y, -L[0].z);
            L[7] = vec3(-L[1].x, -L[1].y, -L[1].z);
            int phi;
            vec3 NV;
            if (s[0] == 1) phi = 0, NV = p.a[0];
            else if (s[0] == -1) phi = 1, NV = vec3(-p.a[0].x, -p.a[0].y, -p.a[0].z);
            else if (s[1] == 1) phi = 2, NV = p.a[1];
            else if (s[1] == -1) phi = 3, NV = vec3(-p.a[1].x, -p.a[1].y, -p.a[1].z);
            else if (s[2] == 1) phi = 4, NV = p.a[2];
            else phi = 5, NV = vec3(-p.a[2].x, -p.a[2].y, -p.a[2].z);
            Walk w;
            const int uu = kFaceView[f][0], vv = kFaceView[f][1];
            for (int q = 0; q < 4; ++q) {
                const vec3& Kq = t.K[kFaceCorners[phi][q]];
                w.P[q][0] = C(Kq, uu);
                w.P[q][1] = C(Kq, vv);
                const vec3& Lq = L[kFaceCorners[f][3 - q]];
                w.Q[q][0] = C(Lq, uu);
                w.Q[q][1] = C(Lq, vv);
            }
            for (int q = 0; q < 4; ++q)
                for (int r = 0; r < 4; ++r) t.T[kFaceEdges[phi][q]][kFaceEdges[f][r]] |= 4u;
            w.run();
            if (w.rec.empty()) continue;
            // 4.2.2 Records to points.
            struct Kept {
                vec3 X;
                float depth;
                int type, f0, f1;
            };
            std::vector<Kept> kept;
            const vec3& Fc = L[kFaceCorners[f][0]];
            const vec3& Vc = t.K[kFaceCorners[phi][0]];
            for (const WalkRecord& r : w.rec) {
                vec3 X, D;
                float depth;
                int f0, f1;
                if (r.type == 0) {
                    const vec3& Ka = t.K[kFaceCorners[phi][r.i]];
                    const vec3& Kb = t.K[kFaceCorners[phi][(r.i + 1) & 3]];
                    X = vec3((Kb.x - Ka.x) * r.tP + Ka.x, (Kb.y - Ka.y) * r.tP + Ka.y, (Kb.z - Ka.z) * r.tP + Ka.z);
                    const vec3 dd(Fc.x - X.x, Fc.y - X.y, Fc.z - X.z);
                    depth = (dd.z * n.z + dd.y * n.y) + dd.x * n.x;
                    D = n;
                    f0 = kFaceEdges[phi][r.i];
                    f1 = kFaceEdges[f][((2 - r.j) % 4 + 4) % 4];
                } else if (r.type == 1) {
                    X = t.K[kFaceCorners[phi][r.i]];
                    const vec3 dd(Fc.x - X.x, Fc.y - X.y, Fc.z - X.z);
                    depth = (dd.z * n.z + dd.y * n.y) + dd.x * n.x;
                    D = n;
                    f0 = kFaceCorners[phi][r.i];
                    f1 = f;
                } else {
                    X = L[kFaceCorners[f][3 - r.j]];
                    const vec3 dd(Vc.x - X.x, Vc.y - X.y, Vc.z - X.z);
                    depth = (dd.z * NV.z + dd.y * NV.y) + dd.x * NV.x;
                    D = NV;
                    f0 = kFaceCorners[f][3 - r.j];
                    f1 = phi;
                }
                if (!(depth >= 0.0f)) continue;
                const float rr = depth * 0.5f;
                kept.push_back({vec3(D.x * rr + X.x, D.y * rr + X.y, D.z * rr + X.z), depth, r.type, f0, f1});
                if (kept.size() >= 8) break;
            }
            if (kept.empty()) {
                t.N = -1;
                return;
            }
            float Dsum = kept[0].depth;
            vec3 S = kept[0].X;
            for (size_t q = 1; q < kept.size(); ++q) {
                Dsum = Dsum + kept[q].depth;
                S = vec3(S.x + kept[q].X.x, S.y + kept[q].X.y, S.z + kept[q].X.z);
            }
            const float wq = 1.0f / float(kept.size());
            const vec3 n_w = nw();
            const vec3 Yl(S.x * wq + p.cF.x, S.y * wq + p.cF.y, S.z * wq + p.cF.z);
            const vec3 Y = placeF(Yl);
            if (!offer(t, Y, n_w, wq * Dsum, *p.MV, *p.MV0, MF, *p.MF0)) continue;
            const vec3 Nw = turn(MF, NV, "xzy", "zyx", "zyx");
            for (size_t q = 0; q < kept.size(); ++q) {
                Slot& sl = t.slot[q];
                sl.kind = kept[q].type;
                sl.f0 = kept[q].f0;
                sl.f1 = kept[q].f1;
                sl.depth = kept[q].depth;
                sl.P = place(MF, kept[q].X, "zyx", "xzy", "xzy");
                sl.n = kept[q].type == 2 ? vec3(-Nw.x, -Nw.y, -Nw.z) : n_w;
                sl.aFirst = p.aFirst;
            }
            t.slot0Written = true;
            t.N = int(kept.size());
        }
    }
}

}  // namespace

void boxBoxContacts(const BoxShape& A, const Mtx& MA, const Mtx& MA0, const BoxShape& B, const Mtx& MB, const Mtx& MB0,
                    float dt, std::vector<BoxImpact>& out) {
    out.clear();
    Test t;
    t.MA = MA;
    t.MA0 = MA0;
    t.MB = MB;
    t.MB0 = MB0;
    t.dt = dt;
    t.fast = dt * 1.5f;
    t.cA = A.offset;
    t.cB = B.offset;
    auto halfSize = [](const BoxShape& s) {
        const vec3 h(s.size.x * 0.5f, s.size.y * 0.5f, s.size.z * 0.5f);
        const vec3 m(s.offset.x + h.x, s.offset.y + h.y, s.offset.z + h.z);
        return vec3(m.x - s.offset.x, m.y - s.offset.y, m.z - s.offset.z);
    };
    t.eA = halfSize(A);
    t.eB = halfSize(B);
    std::memset(t.T, 0, sizeof(t.T));
    // 2.1 R.
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const char* o = (i == 0 && j == 0) ? "yxz" : (j == 0 ? "yzx" : "zyx");
            t.R[i][j] = dotO(rowOf(MA, i), rowOf(MB, j), o);
        }
    signs(t);
    const vec3 dir(MB.o.x - MA.o.x, MB.o.y - MA.o.y, MB.o.z - MA.o.z);
    // 2.3 Centres.
    {
        const vec3 w0 = turn(MA, A.offset, "yzx", "xyz", "zyx");
        const vec3 w(w0.x - dir.x, w0.y - dir.y, w0.z - dir.z);
        const vec3 q = intoM(MB, w, "zyx");
        t.qA = vec3(q.x - t.cB.x, q.y - t.cB.y, q.z - t.cB.z);
    }
    {
        const vec3 w0 = place(MA0, A.offset, "zyx", "zyx", "zyx");
        const vec3 w(w0.x - MB0.o.x, w0.y - MB0.o.y, w0.z - MB0.o.z);
        const vec3 q = intoM(MB0, w, "zyx");
        t.pA = vec3(q.x - t.cB.x, q.y - t.cB.y, q.z - t.cB.z);
    }
    {
        const vec3 w0 = turn(MB, B.offset, "zyx", "zyx", "zyx");
        const vec3 w(w0.x + dir.x, w0.y + dir.y, w0.z + dir.z);
        const vec3 q = intoM(MA, w, "zyx");
        t.qB = vec3(q.x - t.cA.x, q.y - t.cA.y, q.z - t.cA.z);
    }
    {
        const vec3 w0 = turn(MB0, B.offset, "xzy", "xzy", "xzy");
        const vec3 w1(w0.x + MB0.o.x, w0.y + MB0.o.y, w0.z + MB0.o.z);
        const vec3 w(w1.x - MA0.o.x, w1.y - MA0.o.y, w1.z - MA0.o.z);
        const vec3 q = intoM(MA0, w, "zyx");
        t.pB = vec3(q.x - t.cA.x, q.y - t.cA.y, q.z - t.cA.z);
    }
    // Pass 1: A against B's faces.
    {
        Pass p;
        p.index = 1;
        p.eV = t.eA;
        p.eF = t.eB;
        p.cF = t.cB;
        p.pV = t.pA;
        for (int i = 0; i < 3; ++i) p.a[i] = vec3(t.R[i][0], t.R[i][1], t.R[i][2]);
        p.MV = &t.MA;
        p.MV0 = &t.MA0;
        p.MF = &t.MB;
        p.MF0 = &t.MB0;
        p.aFirst = true;
        corners(p.a, t.eA, t.qA, t.K);
        facePass(t, p);
        if (t.N < 0) return;
    }
    // Pass 2: B against A's faces.
    {
        Pass p;
        p.index = 2;
        p.eV = t.eB;
        p.eF = t.eA;
        p.cF = t.cA;
        p.pV = t.pB;
        for (int k = 0; k < 3; ++k) p.a[k] = vec3(t.R[0][k], t.R[1][k], t.R[2][k]);
        p.MV = &t.MB;
        p.MV0 = &t.MB0;
        p.MF = &t.MA;
        p.MF0 = &t.MA0;
        p.aFirst = false;
        corners(p.a, t.eB, t.qB, t.K);
        facePass(t, p);
        if (t.N < 0) return;
    }
    // 6. The edge pass (K = B's corners in A's centred frame).
    vec3 NB[6];
    for (int k = 0; k < 3; ++k) {
        NB[2 * k] = vec3(t.R[0][k], t.R[1][k], t.R[2][k]);
        NB[2 * k + 1] = vec3(-t.R[0][k], -t.R[1][k], -t.R[2][k]);
    }
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 12; ++j) {
            if (t.T[i][j] != 3u) continue;
            const int a = kEdge[i][0], b = kEdge[i][1];
            const vec3 DA(float(kSigma[b][0] - kSigma[a][0]) * t.eA.x, float(kSigma[b][1] - kSigma[a][1]) * t.eA.y,
                          float(kSigma[b][2] - kSigma[a][2]) * t.eA.z);
            const int f0 = kEdgeFaces[j][0], f1 = kEdgeFaces[j][1];
            if (signBitF(dotO(DA, NB[f1], "zxy")) == signBitF(dotO(DA, NB[f0], "yxz"))) continue;
            const int a2 = kEdge[j][0], b2 = kEdge[j][1];
            const vec3 DB(t.K[b2].x - t.K[a2].x, t.K[b2].y - t.K[a2].y, t.K[b2].z - t.K[a2].z);
            const int g0 = kEdgeFaces[i][0], g1 = kEdgeFaces[i][1];
            if (signBitF(dotO(DB, faceNormal(g1), "yzx")) == signBitF(dotO(DB, faceNormal(g0), "xzy"))) continue;
            const vec3 nA0 = faceNormal(g0), nA1 = faceNormal(g1);
            const vec3 sA(nA1.x + nA0.x, nA1.y + nA0.y, nA1.z + nA0.z);
            const vec3 sB(NB[f1].x + NB[f0].x, NB[f1].y + NB[f0].y, NB[f1].z + NB[f0].z);
            if (!!(dotO(sA, sB, "xzy") >= 0.0f)) continue;  // unless s_A·s_B < 0 (NaN: yes)
            const vec3 Ap(float(kSigma[a][0]) * t.eA.x, float(kSigma[a][1]) * t.eA.y, float(kSigma[a][2]) * t.eA.z);
            const vec3 Bp(float(kSigma[b][0]) * t.eA.x, float(kSigma[b][1]) * t.eA.y, float(kSigma[b][2]) * t.eA.z);
            vec3 nrm, ptA, Pi;
            float depth = 0;
            if (!closestPoints(Ap, Bp, t.K[a2], t.K[b2], nrm, depth, ptA, Pi)) continue;
            const float e0 = dotO(nrm, NB[f0], "zyx"), e1 = dotO(nrm, NB[f1], "xzy");
            if (e0 > 0.0f && e1 > 0.0f) {
                t.N = -1;
                return;
            }
            if ((e0 > 0.0f) != (e1 > 0.0f)) continue;
            if (!(dotO(nrm, t.qB, "xzy") > 0.0f)) continue;
            const float r = depth * 0.5f;
            const vec3 Pl((nrm.x * r + Pi.x) + t.cB.x, (nrm.y * r + Pi.y) + t.cB.y, (nrm.z * r + Pi.z) + t.cB.z);
            const vec3 Pw = place(t.MA, Pl, "yxz", "zyx", "xzy");
            const vec3 P(Pw.x - t.cA.x, Pw.y - t.cA.y, Pw.z - t.cA.z);
            const vec3 n_w = turn(t.MA, vec3(-nrm.x, -nrm.y, -nrm.z), "xzy", "xzy", "xzy");
            if (offer(t, P, n_w, depth, t.MA, t.MA0, t.MB, t.MB0)) {
                Slot& sl = t.slot[0];
                sl.kind = 0;
                sl.f0 = i;
                sl.f1 = j;
                sl.P = P;
                sl.n = n_w;
                sl.depth = depth;
                sl.aFirst = true;
                t.slot0Written = true;
                t.N = 1;
            }
        }
    if (t.N <= 0) return;
    for (int q = 0; q < t.N; ++q) {
        BoxImpact bi;
        bi.im.P = t.slot[q].P;
        bi.im.n = t.slot[q].n;
        bi.im.depth = t.slot[q].depth;
        bi.aFirst = t.slot[q].aFirst;
        out.push_back(bi);
    }
}

}  // namespace phys
}  // namespace cc
