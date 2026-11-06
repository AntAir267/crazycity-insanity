// The AI driver's planner (spec/ai/driver-planner.md): bends and junction turns, the path tree,
// the corridor steps, obstacles and detours. Section numbers below are the spec's.
#include <algorithm>
#include <climits>
#include <cmath>

#include "ai/driver.h"
#include "ai/traffic.h"
#include "city/city_data.h"
#include "physics/collision_world.h"

namespace cc {
namespace ai {

struct Driver::Ctx {
    DriverEnv& env;
    RoadNetwork& net;
    vec3 p, rowX, rowZ;
    bool walk = true;
};

namespace {

float hdist(const vec3& a, const vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

vec3 normalise(const vec3& v) {
    const float q = (v.x * v.x + v.y * v.y) + v.z * v.z;
    const float inv = q == 0.0f ? 0.0f : 1.0f / std::sqrt(q);
    return vec3(inv * v.x, inv * v.y, inv * v.z);
}

vec3 flatNormalise(vec3 v) {
    v.y = 0.0f;
    return normalise(v);
}

vec3 unitv(const vec3& v) {
    const float q = (v.x * v.x + v.y * v.y) + v.z * v.z;
    const float s = q == 0.0f ? 0.0f : 1.0f / std::sqrt(q);
    return vec3(v.x * s, v.y * s, v.z * s);
}

float fdot(const vec3& a, const vec3& b) { return a.x * b.x + a.z * b.z; }  // horizontal dot

vec3 sc(const vec3& v, float k) { return vec3(v.x * k, v.y * k, v.z * k); }   // v·k
vec3 scl(float k, const vec3& v) { return vec3(k * v.x, k * v.y, k * v.z); }  // k·v

// angle(a, b) (§1): double; NaN for a zero-length vector.
double angleOf(const vec3& a, const vec3& b) {
    const float Bq = (b.x * b.x + b.y * b.y) + b.z * b.z;
    const float Aq = (a.x * a.x + a.y * a.y) + a.z * a.z;
    const float inv = 1.0f / float(std::sqrt(double(Bq * Aq)));
    if (inv == 0.0f) return 0.0;
    const float c = ((b.x * a.x + b.z * a.z) + b.y * a.y) * inv;
    if (c > 0.99999988f) return 0.0;
    if (c < -1.0f) return 3.1415927;
    return std::acos(double(c));
}

float tiny(float r, float q) {
    if (double(r) > -0.01 && r < 0.0099999998f) return q;
    return r;
}

float pclamp(float v, float h) {  // clamp(v, −h, h) of §1
    if (v < -h) return -h;
    if (v > h) return h;
    return v;
}

float lanezone(const Road& r) {
    const int LB = r.side[1].lanes;
    return LB >= 1 ? r.side[1].band[2 * LB - 1] : 0.0f;
}

float curbw(const Road& r) {
    const int j = r.side[1].sidewalks + r.side[1].lanes - 1;
    if (j > 0) return 2 * j < 10 ? r.side[1].band[2 * j] : 0.0f;
    return 2 * j + 1 >= 0 && 2 * j + 1 < 10 ? r.side[1].band[2 * j + 1] : 0.0f;
}

const vec3& at(const std::vector<vec3>& v, int i) {
    static const vec3 zero;
    if (v.empty()) return zero;
    return v[size_t(std::clamp(i, 0, int(v.size()) - 1))];
}
const vec3& Ea(const Road& r, int i) { return at(r.side[0].kerb, i); }
const vec3& Eb(const Road& r, int i) { return at(r.side[1].kerb, i); }
const vec3& Cc(const Road& r, int i) { return at(r.C, i); }
const vec3& Xc(const Road& r, int i) { return at(r.X, i); }
const vec3& Zc(const Road& r, int i) { return at(r.Z, i); }

bool divided(const Road& r) { return (r.flags & 0x01) != 0; }

const Bend& travelBend(const Road& r, int sigma, int t) {
    const int cnt = int(r.bends.size());
    return sigma ? r.bends[size_t(t)] : r.bends[size_t(cnt - 1 - t)];
}

}  // namespace

int Driver::roadOfSlot(int s) const {
    if (s >= 0 && s < 3) return S_[s];
    if (s == 3) return lastS0_;
    return -1;
}

int Driver::sigmaOfSlot(int s) const {
    if (s >= 0 && s < 3) return d_[s] > 0 ? 1 : 0;
    return 0;
}

// §14 "Junction values of slot 2": MM2 reads the values stored after slot 1's, so a₂ is τ₀ as a
// number and J₂ is G₀.
float Driver::aSlot(int s) const {
    if (s == 0 || s == 1) return a_[s];
    if (s == 2) return tau_[0];
    return 0.0f;
}

vec3 Driver::JSlot(int s) const {
    if (s == 0 || s == 1) return J_[s];
    if (s == 2) return G_[0];
    return vec3();
}

// ---- §4.2 Refreshing the bends of a road ----

void Driver::refreshBends(DriverEnv& env, int road, int sigma) {
    if (road < 0) return;
    Road& R = env.net->roads()[size_t(road)];
    const int N = R.n;
    for (Bend& b : R.bends) {
        const int v = b.v;
        const float beta = (3.14f - std::fabs(b.A)) * 0.5f;
        const float a = REF_.x - b.K.x, c = REF_.z - b.K.z;
        float lat;
        if (sigma) {
            lat = (a * Xc(R, v).x) + (c * Xc(R, v).z);
        } else {
            int m = 1;
            if (v + 2 < N && size_t(v) < R.theta.size() && R.theta[size_t(v)] != b.A) m = 2;
            lat = ((-Xc(R, v + m).x) * a) + ((-Xc(R, v + m).z) * c);
        }
        lat = lat * float(b.s);
        const float wmax = (lanezone(R) + lanezone(R)) - 1.5f;
        const float h = lat < 3.0f ? 3.0f : (lat > wmax ? wmax : lat);
        b.rho = h / float(1.0 - std::sin(double(beta)));
        b.dt = float(std::cos(double(beta)) * double(b.rho));
        const float q = (b.rho - h) * float(b.s);
        const vec3& X = Xc(R, v);
        const vec3& Z = Zc(R, v);
        b.G = (scl(b.dt, Z) + b.K) - scl(q, X);
        b.Sd = flatNormalise((sc(X, b.rho * float(b.s)) + b.G) - b.G);
        const float th = b.A * float(b.s);
        const float sr = float(std::sin(double(th)) * double(b.rho));
        const float cr = float(std::cos(double(th)) * double(b.rho)) * float(b.s);
        const vec3 Qe((b.G.x - Z.x * sr) + cr * X.x, (b.G.y - Z.y * sr) + X.y * cr, (b.G.z - Z.z * sr) + cr * X.z);
        b.Ed = flatNormalise(Qe - b.G);
    }
}

// ---- §5 Junction turns ----

vec3 Driver::obstaclePosition(Ctx& c, const Obstacle& o) const {
    if (o.kind == Obstacle::TrafficCarObs && c.env.traffic) return c.env.traffic->cars()[size_t(o.index)].pos;
    if (o.kind == Obstacle::PropObs && c.env.world) {
        const phys::CollisionObject& ob = c.env.world->object(o.index);
        const phys::Mtx& M = ob.O;
        return ((M.o - sc(M.X, ob.cg.x)) - sc(M.Y, ob.cg.y)) - sc(M.Z, ob.cg.z);
    }
    if (o.kind == Obstacle::PlayerObs && size_t(o.index) < c.env.players.size() && c.env.players[size_t(o.index)].car)
        return c.env.players[size_t(o.index)].car->body().p;
    if (o.kind == Obstacle::OpponentObs && size_t(o.index) < c.env.opponents.size() && c.env.opponents[size_t(o.index)].car)
        return c.env.opponents[size_t(o.index)].car->body().p;
    return vec3();
}

void Driver::junctionTurns(DriverEnv& env) {
    Ctx c{env, *env.net, car_->body().p, car_->rotation().r[0], car_->rotation().r[2], carKind_ != 3};
    const RoadNetwork& net = *env.net;
    const bool left = net.cityAimap().driveOnLeft;
    const TrafficSystem* ts = env.traffic && env.traffic->active() ? env.traffic : nullptr;
    for (int u = 0; u < 2; ++u) {
        if (tau_[u] == 0.0f || S_[u] < 0 || S_[u + 1] < 0) continue;
        const Road& A = net.roads()[size_t(S_[u])];
        const Road& B = net.roads()[size_t(S_[u + 1])];
        const int sg = d_[u] > 0 ? 1 : 0, sg1 = d_[u + 1] > 0 ? 1 : 0;
        const int e0 = sg ? A.n - 1 : 0;
        const int n1 = sg1 ? 0 : B.n - 1;
        auto rt = [](const Road& r, int sigma, int j) { return sigma ? -Xc(r, j) : Xc(r, j); };
        auto rightkerb = [](const Road& r, int sigma, int j) { return sigma ? Eb(r, j) : Ea(r, j); };
        auto leftkerb = [](const Road& r, int sigma, int j) { return sigma ? Ea(r, j) : Eb(r, j); };
        static const std::vector<int> none;
        const std::vector<int>& own = ts ? ts->sectionCars(S_[u], sg ? 1 : -1, A.n - 1) : none;
        const std::vector<int>& onc = ts ? ts->sectionCars(S_[u + 1], sg1 ? -1 : 1, B.n - 1) : none;
        auto queue = [&](const std::vector<int>& list, const vec3& P0, const vec3& d) {
            float m = 0.0f;
            for (int ci : list) {
                const vec3& q = ts->cars()[size_t(ci)].pos;
                const float val = ((q.x - P0.x) * d.x + d.z * (q.z - P0.z)) + 1.25f;
                if (val > m) m = val;
            }
            return m;
        };
        const bool rightTurn = tau_[u] > 0.0f;
        float ein, min, eout, mout;
        if (!left) {
            if (rightTurn) {
                ein = queue(own, rightkerb(A, sg, e0), -rt(A, sg, e0)) + Rx_;
                min = 0.0f;
                eout = Rx_;
                mout = queue(onc, leftkerb(B, sg1, n1), rt(B, sg1, n1));
            } else {
                ein = Lx_;
                min = queue(own, rightkerb(A, sg, e0), -rt(A, sg, e0));
                eout = queue(onc, leftkerb(B, sg1, n1), rt(B, sg1, n1)) + Lx_;
                mout = 0.0f;
            }
        } else {
            if (rightTurn) {
                ein = Rx_;
                min = queue(own, leftkerb(A, sg, e0), rt(A, sg, e0));
                eout = queue(onc, rightkerb(B, sg1, n1), -rt(B, sg1, n1)) + Rx_;
                mout = 0.0f;
            } else {
                ein = queue(own, leftkerb(A, sg, e0), rt(A, sg, e0)) + Lx_;
                min = 0.0f;
                eout = Lx_;
                mout = queue(onc, rightkerb(B, sg1, n1), -rt(B, sg1, n1));
            }
        }
        // §5.2 corner and offset.
        vec3 P1, P2, K2;
        if (rightTurn) {
            P1 = rightkerb(A, sg, e0) + sc(-rt(A, sg, e0), ein);
            K2 = rightkerb(B, sg1, n1);
            P2 = K2 + sc(-rt(B, sg1, n1), eout);
        } else {
            P1 = leftkerb(A, sg, e0) + sc(rt(A, sg, e0), ein);
            K2 = leftkerb(B, sg1, n1);
            P2 = K2 + sc(rt(B, sg1, n1), eout);
        }
        const vec3& d1 = Zc(A, e0);
        const vec3& d2 = Zc(B, n1);
        const float t = ((((P2.z * d1.x) - (P1.z * d1.x)) - (P2.x * d1.z)) + (P1.x * d1.z)) / ((d2.x * d1.z) - (d2.z * d1.x));
        J_[u] = vec3(d2.x * t + P2.x, K2.y, d2.z * t + P2.z);
        const float dx = REF_.x - J_[u].x, dz = REF_.z - J_[u].z;
        const float f = ((tj_[u] + 12.0f) * (tj_[u] + 12.0f) < dx * dx + dz * dz) ? 1.0f : 0.0f;
        const vec3 L = sg ? Xc(A, A.n - 1) : -Xc(A, 0);
        float g = ((dz * L.z + dx * L.x) * tau_[u]) + f;
        const float lo = Lx_ + Rx_;
        auto limit = [&](const Road& r, float e, float m) {
            const float lim = (((curbw(r) + curbw(r)) - Rx_) - e) - m;
            const float hi = lim < lo ? lo : std::min(lim, 9999.0f);
            if (g < lo) g = lo;
            else if (g > hi) g = hi;
        };
        limit(A, ein, min);
        limit(B, eout, mout);
        h_[u] = g;
        // §5.3 radius, centre, start and end.
        const float h = h_[u];
        const float beta = (3.14f - std::fabs(a_[u])) * 0.5f;
        rj_[u] = h / float(1.0 - std::sin(double(beta)));
        tj_[u] = float(std::cos(double(beta)) * double(rj_[u]));
        const vec3 Xp = sg ? Xc(A, A.n - 1) : -Xc(A, 0);
        const vec3 Zp = sg ? Zc(A, A.n - 1) : -Zc(A, 0);
        G_[u] = (scl(tj_[u], Zp) + J_[u]) - sc(Xp, (rj_[u] - h) * tau_[u]);
        Sdir_[u] = flatNormalise((sc(Xp, rj_[u] * tau_[u]) + G_[u]) - G_[u]);
        const float th = a_[u] * tau_[u];
        const float sr = float(std::sin(double(th)) * double(rj_[u]));
        const float cr = float(std::cos(double(th)) * double(rj_[u])) * tau_[u];
        const vec3& G = G_[u];
        const vec3 Qe((G.x - Zp.x * sr) + cr * Xp.x, (G.y - Zp.y * sr) + Xp.y * cr, (G.z - Zp.z * sr) + Xp.z * cr);
        Edir_[u] = flatNormalise(Qe - G);
    }
    (void)c;
}

// ---- §6 One planning pass ----

void Driver::plan(DriverEnv& env) {
    Ctx c{env, *env.net, car_->body().p, car_->rotation().r[0], car_->rotation().r[2], carKind_ != 3};
    FINAL_ = routeDone_ ? 1 : 0;
    const vec3 p = c.p;
    // 1. Record 0.
    PathRecord& r0 = Q_[0];
    r0.pos = vec3(p.x, p.y + 1.0f, p.z);
    r0.cost = 0.0f;
    r0.dist = 0.0f;
    r0.arc = 0;
    if (S_[0] < 0) {
        chosen_ = -1;
        rowCount_ = 0;
        return;
    }
    {
        const int N0 = c.net.roads()[size_t(S_[0])].n;
        const int k = c.net.sectionOfPoint(S_[0], p, d_[0]);
        if (k < N0) {
            r0.slot = 0;
            r0.prog = k;
        } else if (S_[1] < 0) {
            r0.slot = 0;
            r0.prog = N0 - 1;
        } else {
            r0.slot = 1;
            const int N1 = c.net.roads()[size_t(S_[1])].n;
            const int k1 = c.net.sectionOfPoint(S_[1], p, d_[1]);
            if (k1 < N1) {
                r0.prog = k1;
            } else if (S_[2] < 0) {
                r0.prog = N1 - 1;
            } else {
                r0.slot = 2;
                r0.prog = c.net.sectionOfPoint(S_[2], p, d_[2]);
            }
        }
    }
    // 2.
    rowCount_ = 0;
    rowBlocked_.fill(false);
    rowZone2_.fill(false);
    // 3.
    if (w_ < n_) {
        const int r = arcTest(c, 0);
        if (r == 0) {
            r0.mark = 0;
            REF0_ = p;
        } else {
            r0.mark = 1;
        }
    } else {
        r0.mark = 1;
    }
    // 4-5.
    REF_ = REF0_;
    junctionTurns(env);
    // 6.
    expand(c, 1);
    // 7. §9 Choosing the path.
    chosen_ = -1;
    float best = 99999.0f;
    auto pass = [&](auto take) {
        best = 99999.0f;
        for (int i = 0; i < rowCount_; ++i) {
            if (!take(i) || rowLen_[size_t(i)] <= 0) continue;
            const int last = rowRecord(i, rowLen_[size_t(i)] - 1);
            if (last >= kRecords) continue;
            const float cost = Q_[size_t(last)].cost;
            if (cost < best) {
                best = cost;
                chosen_ = i;
            }
        }
    };
    if (orders_.sw[5]) pass([&](int i) { return rowZone2_[size_t(i)]; });
    if (chosen_ < 0) pass([&](int i) { return !rowBlocked_[size_t(i)]; });
    if (chosen_ < 0) pass([&](int) { return true; });
}

// ---- §7 EXPAND ----

void Driver::expand(Ctx& c, int k) {
    if (k > 39) return;
    const int s = Q_[size_t(k - 1)].slot;
    const int Ss = roadOfSlot(s);
    if (FINAL_ == 0 && Ss >= 0) {
        refreshBends(c.env, Ss, sigmaOfSlot(s));
        int r = arcTest(c, k - 1);
        if (r != 0) {
            Q_[size_t(k - 1)].arc = r - 1;
            placeArc(c, k, k - 1);
        } else {
            REF_ = Q_[size_t(k - 1)].pos;
            routeCorridor(c, k);
            r = arcTest(c, k);
            if (r != 0) {
                Q_[size_t(k)].arc = r - 1;
                placeArc(c, k, k);
            }
        }
    } else {
        if (Ss < 0) {
            destinationStep(c, k);
        } else {
            refreshBends(c.env, Ss, sigmaOfSlot(s));
            int r = arcTest(c, k - 1);
            if (r != 0) {
                Q_[size_t(k - 1)].arc = r - 1;
                const int t = placeArc(c, k, k - 1);
                if (t != 0) {
                    Q_[size_t(k)].arc += 1;
                    REF_ = Q_[size_t(k)].pos;
                    const int sk = roadOfSlot(Q_[size_t(k)].slot);
                    if (sk >= 0 && c.net.onRoad(sk, Q_[size_t(k)].pos, 2.0f) > 1) finalCorridor(c, k);
                }
            } else {
                REF_ = Q_[size_t(k - 1)].pos;
                finalCorridor(c, k);
                r = arcTest(c, k);
                if (r != 0) {
                    Q_[size_t(k)].arc = r - 1;
                    placeArc(c, k, k);
                }
            }
        }
    }
    if (k >= kRecords) return;  // engine: never reached (an arc would have to run past the table)
    // 5. Obstacle check of P(k − 1) → P(k) (a junction-turn arc may have advanced k past Q39
    // into row 0's records; that record is checked and continued as usual).
    PathRecord& rk = Q_[size_t(k)];
    const PathRecord& rp = Q_[size_t(k - 1)];
    rk.zone = 1;
    int cls = -1;
    const Obstacle O = obstacleScan(c, rp.pos, rk.pos, rp.slot, rp.prog, rk.slot, rk.prog + 3, (B_ + F_) + (B_ + F_), cls);
    if (O.kind == Obstacle::None) {
        cont(c, k);
        return;
    }
    if (cls == 2 && (w_ <= 2 || rp.mark == 1)) {
        cont(c, k);
        return;
    }
    const vec3 Qp = obstaclePosition(c, O);
    const float dz = rp.pos.z - Qp.z, dx = rp.pos.x - Qp.x;
    if (orders_.kp * orders_.kp <= dx * dx + dz * dz) {
        cont(c, k);
        return;
    }
    std::vector<Alt> alts;
    collectAlternatives(c, k, O, c.walk, alts);
    for (const Alt& a : alts) useAlternative(c, k, a, cls);
}

// ---- §8 CONTINUE ----

void Driver::cont(Ctx& c, int k) {
    const PathRecord& r = Q_[size_t(k)];
    const int Sk = roadOfSlot(r.slot);
    if (r.dist < orders_.kp && Sk >= 0 && rowCount_ < 10 &&
        (r.slot == f_ || (r.prog >= c.net.roads()[size_t(Sk)].n - 1 && r.slot + 1 <= 2 && roadOfSlot(r.slot + 1) < 0))) {
        FINAL_ = 1;
        expand(c, k + 1);
        return;
    }
    if (r.dist < orders_.kp && S_[2] >= 0 && !(r.slot == 2 && r.prog == c.net.roads()[size_t(S_[2])].n) && rowCount_ < 10) {
        expand(c, k + 1);
        return;
    }
    // Commit: records 0 … k copied one at a time, in increasing order, to array records
    // 40·(R + 1) + 0 … k (a longer row runs on into the next row's records; a record copied late
    // can read a slot this commit already overwrote). Engine: past the end of the table only the
    // records that fit are kept.
    if (rowCount_ >= 25) return;
    const int R = rowCount_;
    bool z2 = false, bl = false;
    int kept = 0;
    for (int i = 0; i <= k; ++i) {
        const int dst = rowRecord(R, i);
        if (dst >= kRecords || i >= kRecords) break;
        Q_[size_t(dst)] = Q_[size_t(i)];
        if (Q_[size_t(dst)].zone == 2) z2 = true;
        if (Q_[size_t(dst)].mark == 3) bl = true;
        ++kept;
    }
    rowZone2_[size_t(R)] = z2;
    rowBlocked_[size_t(R)] = bl;
    rowLen_[size_t(R)] = kept;
    ++rowCount_;
}

// ---- §10 arc_test ----

int Driver::arcTest(Ctx& c, int q) {
    PathRecord& rec = Q_[size_t(q)];
    const vec3& P = rec.pos;
    const int s0 = rec.slot;
    const int R = roadOfSlot(s0);
    int t = -1;
    if (R >= 0) {
        const Road& r = c.net.roads()[size_t(R)];
        const int sg = sigmaOfSlot(s0);
        const int cnt = int(r.bends.size());
        if (rec.prog < r.n) {
            int bestStart = INT_MAX;
            for (int tt = 0; tt < cnt; ++tt) {
                const Bend& b = travelBend(r, sg, tt);
                const int start = sg ? b.v : r.n - b.v - 1;
                if (start >= rec.prog && start < bestStart) {
                    bestStart = start;
                    t = tt;
                }
            }
        }
    }
    for (int u = s0; u <= 2; ++u) {
        if (u < 0) continue;
        const int Su = S_[u];
        if (Su >= 0 && t > -1) {
            const Road& r = c.net.roads()[size_t(Su)];
            const int sg = d_[u] > 0 ? 1 : 0;
            const int cnt = int(r.bends.size());
            for (int tp = t; tp < cnt; ++tp) {
                const Bend& b = travelBend(r, sg, tp);
                const float lim = (b.rho + 15.0f) * (b.rho + 15.0f);
                const float dx = P.x - b.G.x, dz = P.z - b.G.z;
                if (dx * dx + dz * dz < lim) {
                    const float s = float(b.s);
                    float T1, T2;
                    if (sg) {
                        T1 = ((s * b.Sd.x) * dz) + ((s * (-b.Sd.z)) * dx);
                        T2 = ((s * b.Ed.z) * dx) + ((s * (-b.Ed.x)) * dz);
                    } else {
                        T1 = ((s * b.Ed.z) * dx) + ((s * (-b.Ed.x)) * dz);
                        T2 = ((s * (-b.Sd.z)) * dx) + ((s * b.Sd.x) * dz);
                    }
                    if (double(T1) >= -0.01 && T2 >= 0.0099999998f) {
                        rec.slot = u;
                        return tp + 3;
                    }
                }
            }
            t = 0;
        }
        if (u < 2 && (u == 0 || (rec.arc == 1 || (-0.2f < a_[0] && a_[0] < 0.2f))) && tau_[u] != 0.0f) {
            const float dx = P.x - G_[u].x, dz = P.z - G_[u].z;
            if (dx * dx + dz * dz < (rj_[u] + 15.0f) * (rj_[u] + 15.0f)) {
                const float tu = tau_[u];
                const float A1 = ((-Sdir_[u].z) * tu) * dx + (Sdir_[u].x * tu) * dz;
                const float A2 = (tu * Edir_[u].z) * dx + ((-Edir_[u].x) * tu) * dz;
                if (double(A1) >= -0.01 && A2 >= 0.0099999998f) return u + 1;
            }
        }
    }
    return 0;
}

// ---- §11 place_arc ----

void Driver::finish(Ctx& c, int k, bool withAngle) {
    PathRecord& r = Q_[size_t(k)];
    const PathRecord& pr = Q_[size_t(k - 1)];
    r.mark = 1;
    r.pos.y = r.pos.y + 1.0f;
    const float dz = pr.pos.z - r.pos.z, dx = pr.pos.x - r.pos.x;
    r.dist = std::sqrt(dz * dz + dx * dx) + pr.dist;
    if (withAngle) {
        if (k == 1) r.cost = float(angleOf(-c.rowZ, r.pos - c.p));
        else r.cost = float(angleOf(pr.pos - Q_[size_t(k - 2)].pos, r.pos - pr.pos) + double(pr.cost));
    }
}

int Driver::placeArc(Ctx& c, int& k, int src) {
    if (k > 39) return 0;
    const int arcv = Q_[size_t(src)].arc;
    if (arcv >= 2) {
        // §11.1 Bend arc.
        const int t = arcv - 2;
        const int sl = Q_[size_t(src)].slot;
        const int R = roadOfSlot(sl);
        if (R < 0) return 0;
        const int sg = sigmaOfSlot(sl);
        const Road& r = c.net.roads()[size_t(R)];
        if (t >= int(r.bends.size())) return 0;
        const Bend& b = travelBend(r, sg, t);
        Q_[size_t(k)].arc = sl;
        const float s = float(b.s);
        const int n = int(((b.A * b.rho) * s) * 0.1f);
        int M = n + (n % 2);
        if (M < 2) M = 2;
        const float Aa = b.A * s;
        const float step = Aa / float(M);
        const vec3& Xv = Xc(r, b.v);
        const vec3& Zv = Zc(r, b.v);
        auto arcpt = [&](float th) {
            const float sr = float(std::sin(double(th)) * double(b.rho));
            const float cr = float(std::cos(double(th)) * double(b.rho)) * s;
            return vec3((b.G.x - Zv.x * sr) + cr * Xv.x, (b.G.y - 0.0f) + 0.0f, (b.G.z - Zv.z * sr) + Xv.z * cr);
        };
        const vec3 Es = sg ? vec3((Xv.x * (b.rho * s)) + b.G.x, 0.0f + b.G.y, (Xv.z * (b.rho * s)) + b.G.z)
                           : arcpt(float(M) * step);
        const vec3 u = normalise(vec3(Es.x - b.G.x, 0.0f, Es.z - b.G.z));
        const vec3 w = Q_[size_t(k - 1)].pos - b.G;
        float phi = float(std::atan2(double((w.x * (-u.z)) + (u.x * w.z)), double((w.x * u.x) + (w.z * u.z))));
        if ((b.s < 0 && sg == 1) || (b.s > 0 && sg == 0)) phi = phi * -1.0f;
        if (phi < -0.1f) {
            Q_[size_t(k)].pos = Es;
            Q_[size_t(k)].prog = c.net.sectionOfPoint(R, Es, sg ? 1 : -1);
            Q_[size_t(k)].slot = sl;
            finish(c, k, true);
            return 0;
        }
        const float half = step * 0.5f;
        for (int i = 1; i <= M; ++i) {
            if (float(i) * step - half > phi) {
                const int j = sg ? i : M - i;
                PathRecord& rk = Q_[size_t(k)];
                rk.pos = arcpt(float(j) * step);
                rk.slot = sl;
                rk.prog = c.net.sectionOfPoint(R, rk.pos, sg ? 1 : -1);
                if (rk.prog >= r.n) {
                    rk.slot += 1;
                    rk.prog = 0;
                }
                finish(c, k, false);
                return i > 1 - int(float(M) * -0.5f) ? 1 : 0;
            }
        }
        const vec3 Qe = arcpt(Aa);
        Q_[size_t(k - 1)].prog = c.net.carSection(R, Qe);
        return 1;
    }
    // §11.2 Junction turn arc.
    const int u = arcv;
    if (u < 0 || u > 1 || S_[u] < 0 || S_[u + 1] < 0) return 0;
    const Road& r = c.net.roads()[size_t(S_[u])];
    const int sg = d_[u] > 0 ? 1 : 0;
    const vec3 Xp = sg ? Xc(r, r.n - 1) : -Xc(r, 0);
    const vec3 Zp = sg ? Zc(r, r.n - 1) : -Zc(r, 0);
    const vec3& G = G_[u];
    const float tu = tau_[u];
    const float q = rj_[u] * tu;
    const vec3 A0(Xp.x * q + G.x, Xp.y * q + G.y, q * Xp.z + G.z);
    Q_[size_t(k)].arc = u;
    const vec3 w = Q_[size_t(k - 1)].pos - G;
    float phi = float(std::atan2(double((Sdir_[u].x * w.z) + ((-Sdir_[u].z) * w.x)), double((w.z * Sdir_[u].z) + (w.x * Sdir_[u].x))));
    if (tu < 0.0f) phi = -phi;
    if (phi < -0.1f) {
        Q_[size_t(k)].pos = A0;
        finish(c, k, true);
        Q_[size_t(k)].slot = u;
        Q_[size_t(k)].prog = r.n - 1;
        Q_[size_t(k)].mark = 0;
        return 0;
    }
    const int n = int(((rj_[u] * tu) * a_[u]) * 0.1f);
    int M = n + (n % 2);
    if (M < 2) M = 2;
    const float step = (tu * a_[u]) / float(M);
    const float half = step * 0.5f;
    const Road& r1 = c.net.roads()[size_t(S_[u + 1])];
    for (int i = 1; i <= M; ++i) {
        const float th = float(i) * step;
        if (th - half > phi) {
            const float sr = float(std::sin(double(th)) * double(rj_[u]));
            const float cr = float(std::cos(double(th)) * double(rj_[u])) * tu;
            PathRecord& rk = Q_[size_t(k)];
            rk.pos = vec3((G.x - Zp.x * sr) + cr * Xp.x, (G.y - Zp.y * sr) + Xp.y * cr, (G.z - sr * Zp.z) + Xp.z * cr);
            finish(c, k, false);
            if ((tu * a_[u]) * 0.5f <= th) {
                rk.slot = u + 1;
                rk.prog = c.net.sectionOfPoint(S_[u + 1], rk.pos, d_[u + 1]);
                if (rk.prog >= r1.n) {
                    rk.slot += 1;
                    rk.prog = 0;
                }
                if (u + 1 >= f_) FINAL_ = 1;
            } else {
                rk.slot = u;
                rk.prog = r.n - 1;
            }
            rk.arc = u;
            if (2 * i > M && c.net.onRoad(S_[u + 1], rk.pos, 0.0f) < 3) {
                rk.arc = u + 1;
                REF_ = rk.pos;
                return 1;
            }
            if (i < M) {
                if (k + 1 >= kRecords) return 1;  // engine: the array ends (never seen)
                ++k;
            }
        }
    }
    return 1;
}

// ---- §12 Destination step, recording a point ----

void Driver::destinationStep(Ctx& c, int k) {
    PathRecord& rk = Q_[size_t(k)];
    const PathRecord& pr = Q_[size_t(k - 1)];
    const vec3& D = orders_.D;
    if (orders_.s2 == 0.0f) {
        rk.pos = D;
    } else {
        const float d = hdist(pr.pos, D);
        if (d < orders_.s2) {
            rk.pos = pr.pos;
        } else {
            const float g = (d - orders_.s2) / d;
            rk.pos = sc(D - pr.pos, g) + pr.pos;
        }
    }
    rk.pos.y = rk.pos.y + 1.0f;
    rk.mark = pr.mark;
    rk.prog = pr.prog;
    rk.slot = pr.slot;
    rk.arc = pr.arc;
    rk.dist = 9999.0f;
    if (k == 1) rk.cost = float(angleOf(-c.rowZ, rk.pos - c.p));
    else rk.cost = float(angleOf(pr.pos - Q_[size_t(k - 2)].pos, rk.pos - pr.pos) + double(pr.cost));
}

void Driver::recordPoint(Ctx& c, int k, int s, int q) {
    PathRecord& rk = Q_[size_t(k)];
    const PathRecord& pr = Q_[size_t(k - 1)];
    rk.mark = 0;
    rk.prog = q;
    rk.slot = s;
    rk.arc = s;
    rk.pos.y = rk.pos.y + 1.0f;
    rk.dist = hdist(pr.pos, rk.pos) + pr.dist;
    if (k == 1) rk.cost = float(angleOf(-c.rowZ, rk.pos - c.p));
    else rk.cost = float(angleOf(pr.pos - Q_[size_t(k - 2)].pos, rk.pos - pr.pos) + double(pr.cost));
}

// ---- §13 Final-road corridor step ----

void Driver::finalCorridor(Ctx& c, int k) {
    const PathRecord pr = Q_[size_t(k - 1)];
    PathRecord& rk = Q_[size_t(k)];
    const RoadNetwork& net = c.net;
    // 1. Start.
    int s = pr.slot, q = pr.prog;
    if (roadOfSlot(s) < 0) {
        destinationStep(c, k);
        return;
    }
    int N = net.roads()[size_t(roadOfSlot(s))].n;
    if (k > 1 && Q_[size_t(k - 2)].mark != 1) {
        ++q;
        if (q == N) {
            if (roadOfSlot(s + 1) >= 0) {
                ++s;
                q = 0;
                N = net.roads()[size_t(roadOfSlot(s))].n;
            } else {
                --q;
            }
        }
    }
    int phi = N - q;
    if (phi == N) phi = N - 1;
    // 2. Destination kind.
    if (destKind_ == 0 || destKind_ == 3) {
        destinationStep(c, k);
        return;
    }
    if (destKind_ < 0 || destKind_ > 3) return;
    // 3.
    vec3 P = pr.pos;
    const Road& Rs = net.roads()[size_t(roadOfSlot(s))];
    const int sgs = sigmaOfSlot(s);
    const float tl = Lx_ + 1.0f, tr = Rx_ + 1.0f;
    auto bearing = [](float r, float f) { return std::atan2(double(r), double(f)); };
    auto rightOf = [](const Road& r, int sigma, int j, const vec3& v) {
        return sigma ? -fdot(Xc(r, j), v) : fdot(Xc(r, j), v);
    };
    auto aheadOf = [](const Road& r, int sigma, int j, const vec3& v) {
        return sigma ? -fdot(Zc(r, j), v) : fdot(Zc(r, j), v);
    };
    // 4. Initial window.
    float lamL, lamR;
    if (sgs) {
        const int j = q;
        const vec3 L = divided(Rs) ? Cc(Rs, j) - scl(tl, Xc(Rs, j)) : Ea(Rs, j) - scl(Lx_, Xc(Rs, j));
        vec3 v = L - pr.pos;
        float r = tiny(rightOf(Rs, 1, j, v), -1.0f), f = aheadOf(Rs, 1, j, v);
        lamL = float(bearing(r, f));
        const vec3 R = scl(Rx_, Xc(Rs, j)) + Eb(Rs, j);
        v = R - pr.pos;
        r = tiny(rightOf(Rs, 1, j, v), 1.0f);
        f = aheadOf(Rs, 1, j, v);
        lamR = float(bearing(r, f));
    } else {
        const int j = N - 1 - q;
        const vec3 u = unitv(Cc(Rs, j) - Eb(Rs, j));
        const vec3 L = scl(tl, u) + Eb(Rs, j);
        vec3 v = L - pr.pos;
        float r = fdot(Xc(Rs, phi), v), f = fdot(Zc(Rs, phi), v);
        if (k == 1 && r > 0.0f) {
            P = pr.pos + scl(r, Xc(Rs, phi));
            r = 0.0f;
        }
        if (k == 1 && f < 1.0f) f = 1.0f;
        lamL = float(bearing(r, f));
        const vec3 R = Ea(Rs, j) - scl(tr, u);
        v = R - pr.pos;
        r = fdot(Xc(Rs, phi), v);
        f = fdot(Zc(Rs, phi), v);
        if (k == 1 && r < 0.0f) {
            P = pr.pos - scl(r, Xc(Rs, phi));
            r = 0.0f;
        }
        if (k == 1 && f < 1.0f) f = 1.0f;
        lamR = float(bearing(r, f));
    }
    int sL = s, qL = q, sR = s, qR = q;
    // 5. First scanned section.
    ++q;
    if (q == N) {
        if (roadOfSlot(s + 1) >= 0) {
            ++s;
            q = 0;
        } else {
            --q;
        }
    }
    const int scanRoad = roadOfSlot(s);
    const Road& Sc = net.roads()[size_t(scanRoad)];
    const int sgScan = sigmaOfSlot(s);
    const int Nsc = Sc.n;
    // Previous point's frame.
    const int prevRoad = roadOfSlot(pr.slot);
    const Road& Rp = net.roads()[size_t(prevRoad >= 0 ? prevRoad : scanRoad)];
    const int sgp = sigmaOfSlot(pr.slot);
    int jp;
    if (sgp) {
        jp = pr.prog;
    } else {
        jp = Rp.n - pr.prog - 1;
        if (jp == Rp.n) jp = Rp.n - pr.prog - 2;
    }
    jp = std::clamp(jp, 0, Rp.n - 1);
    int stop = 0;  // 1 A1, 2 A2, 3 A3, 4 A4
    if (q < Nsc) {
        for (; q < Nsc && !stop; ++q) {
            if (sgScan) {
                const int j = q;
                const vec3 L = (divided(Sc) ? Cc(Sc, j) : Ea(Sc, j)) - scl(tl, Xc(Sc, j));
                vec3 v = L - P;
                float r = tiny(rightOf(Rp, sgp, jp, v), -1.0f), f = aheadOf(Rp, sgp, jp, v);
                double th = bearing(r, f);
                if (th > double(lamR)) {
                    stop = 1;
                    break;
                }
                if (th > double(lamL - 0.001f)) {
                    lamL = float(th);
                    sL = s;
                    qL = q;
                }
                const vec3 R = scl(tr, Xc(Sc, j)) + Eb(Sc, j);
                v = R - P;
                r = tiny(rightOf(Rp, sgp, jp, v), 1.0f);
                f = aheadOf(Rp, sgp, jp, v);
                th = bearing(r, f);
                if (th < double(lamL)) {
                    stop = 2;
                    break;
                }
                if (th < double(lamR + 0.001f)) {
                    lamR = float(th);
                    sR = s;
                    qR = q;
                }
            } else {
                const int j = Nsc - 1 - q;
                const vec3 u = unitv(Cc(Sc, j) - Eb(Sc, j));
                const vec3 L = scl(tl, u) + Eb(Sc, j);
                vec3 v = L - P;
                float r = tiny(rightOf(Rp, sgp, jp, v), -1.0f), f = aheadOf(Rp, sgp, jp, v);
                if (f < 1.0f) f = 1.0f;
                double th = bearing(r, f);
                if (th > double(lamR)) {
                    stop = 3;
                    break;
                }
                if (th > double(lamL - 0.001f)) {
                    lamL = float(th);
                    sL = s;
                    qL = q;
                }
                const vec3 R = Ea(Sc, j) - scl(tr, Xc(Sc, std::clamp(phi, 0, Nsc - 1)));
                v = R - P;
                r = tiny(rightOf(Rp, sgp, jp, v), 1.0f);
                f = aheadOf(Rp, sgp, jp, v);
                if (f < 1.0f) f = 1.0f;
                th = bearing(r, f);
                if (th < double(lamL)) {
                    stop = 4;
                    break;
                }
                if (th < double(lamR + 0.001f)) {
                    lamR = float(th);
                    sR = s;
                    qR = q;
                }
            }
        }
    }
    int outS = s, outQ = 0;
    auto roadAt = [&](int slot) -> const Road& {
        const int rr = roadOfSlot(slot);
        return net.roads()[size_t(rr >= 0 ? rr : scanRoad)];
    };
    if (stop) {
        if (stop == 1 || stop == 2) {
            int& qa = stop == 1 ? qR : qL;
            const int sa = stop == 1 ? sR : sL;
            const Road& Ra = roadAt(sa);
            if (k > 1 && sa == pr.slot && qa == pr.prog && qa + 1 <= Ra.n - 1) ++qa;
            if (stop == 1) rk.pos = scl(tr, Xc(Ra, qa)) + (divided(Ra) ? Cc(Ra, qa) : Eb(Ra, qa));
            else rk.pos = (divided(Ra) ? Cc(Ra, qa) : Ea(Ra, qa)) - scl(tl, Xc(Ra, qa));
            outS = sa;
        } else if (stop == 3) {
            const Road& Ra = roadAt(sR);
            if (sigmaOfSlot(sR)) {
                const vec3 u = unitv(Cc(Ra, qR) - Eb(Ra, qR));
                rk.pos = scl(tr, u) + Eb(Ra, qR);
            } else {
                const int j = Ra.n - qR - 1;
                const vec3 u = unitv(Cc(Ra, j) - Ea(Ra, j));
                rk.pos = scl(tr, u) + Ea(Ra, j);
            }
            outS = sR;
        } else {
            const Road& Ra = roadAt(sL);
            if (sigmaOfSlot(sL)) {
                const vec3 u = unitv(Cc(Ra, qL) - Ea(Ra, qL));
                rk.pos = scl(tl, u) + (divided(Ra) ? Cc(Ra, qL) : Ea(Ra, qL));
            } else {
                const int j = Ra.n - qL - 1;
                const vec3 u = unitv(Cc(Ra, j) - Eb(Ra, j));
                rk.pos = scl(tl, u) + Eb(Ra, j);
            }
            outS = sL;
        }
        outQ = std::min(qL, qR);
    } else {
        // 7. No stop.
        const Road& RR = roadAt(sR);
        const Road& RL = roadAt(sL);
        const int NR = RR.n, NL = RL.n;
        if (sigmaOfSlot(sR) == 0 && qR < NR - 1) {
            const float h = (RR.halfWidth - Rx_) - 1.0f;
            const float cc = pclamp(-fdot(Xc(RR, NR - 1), Cc(RR, NR - 1) - pr.pos), h);
            const float w = h - (cc / h) * h;
            const int j = NR - qR - 1;
            rk.pos = (Ea(RR, j) - scl(tr, Xc(RR, j))) - scl(w, Xc(RR, j));
            outS = sR;
            outQ = qR;
        } else if (sigmaOfSlot(sL) == 0 && qL < NL - 1) {
            const float h = (RL.halfWidth - Rx_) - 1.0f;
            const float cc = pclamp(-fdot(Xc(RL, NL - 1), Cc(RL, NL - 1) - pr.pos), h);
            const float h2 = (RL.halfWidth - Lx_) - 1.0f;
            const float g = (cc / h + 1.0f) * h2;
            const int j = NL - qL - 1;
            rk.pos = (scl(tr, Xc(RL, j)) + Eb(RL, j)) + scl(g, Xc(RL, j));
            outS = sL;
            outQ = qL;
        } else if (sigmaOfSlot(sR) == 1 && qR < NR - 1) {
            const float h = (RR.halfWidth - Rx_) - 1.0f;
            const float cc = pclamp(fdot(Xc(RR, 0), Cc(RR, 0) - pr.pos), h);
            const float w = h - (cc / h) * h;
            rk.pos = (scl(tr, Xc(RR, qR)) + Eb(RR, qR)) + scl(w, Xc(RR, qR));
            outS = sR;
            outQ = qR;
        } else if (sigmaOfSlot(sL) == 1 && qL < NL - 1) {
            const float h = (RL.halfWidth - Lx_) - 1.0f;
            const float cc = pclamp(fdot(Xc(RL, 0), Cc(RL, 0) - pr.pos), h);
            if (divided(RL)) {
                rk.pos = Cc(RL, qL) - scl(tl, Xc(RL, qL));
            } else {
                const float w = h - (cc / h) * h;
                rk.pos = (Ea(RL, qL) - scl(tl, Xc(RL, qL))) - scl(w, Xc(RL, qL));
            }
            outS = sL;
            outQ = qL;
        } else {
            outS = s;
            outQ = std::min(qL, qR);
        }
    }
    // 8. Destination check.
    const int outRoad = roadOfSlot(outS);
    const int qD = outRoad >= 0 ? net.sectionOfPoint(outRoad, orders_.D, sigmaOfSlot(outS) ? 1 : -1) : 0;
    if (outQ >= qD) destinationStep(c, k);
    else recordPoint(c, k, outS, outQ);
}

// ---- §14 Route corridor step ----

void Driver::routeCorridor(Ctx& c, int k) {
    const PathRecord pr = Q_[size_t(k - 1)];
    PathRecord& rk = Q_[size_t(k)];
    const RoadNetwork& net = c.net;
    int s = pr.slot, q = pr.prog;
    if (roadOfSlot(s) < 0) {
        destinationStep(c, k);
        return;
    }
    int N = net.roads()[size_t(roadOfSlot(s))].n;
    // 1. Start.
    if (k > 1 && Q_[size_t(k - 2)].mark != 1) {
        if (q + 1 >= N) {
            if (roadOfSlot(s + 1) >= 0) {
                ++s;
                q = 0;
                N = net.roads()[size_t(roadOfSlot(s))].n;
            }
        } else {
            ++q;
        }
    }
    int phi = N - q;
    if (phi == N) phi = N - 1;
    const vec3 P = pr.pos;
    const float tl = Lx_ + 1.0f, tr = Rx_ + 1.0f;
    auto bearing = [](float r, float f) { return std::atan2(double(r), double(f)); };
    auto rightOf = [](const Road& r, int sigma, int j, const vec3& v) {
        return sigma ? -fdot(Xc(r, j), v) : fdot(Xc(r, j), v);
    };
    auto aheadOf = [](const Road& r, int sigma, int j, const vec3& v) {
        return sigma ? -fdot(Zc(r, j), v) : fdot(Zc(r, j), v);
    };
    // 2. Initial window.
    const Road& R0 = net.roads()[size_t(roadOfSlot(s))];
    const int sg0 = sigmaOfSlot(s);
    const bool div0 = divided(R0);
    float m, lamL, lamR;
    {
        const int j = sg0 ? q : N - 1 - q;
        const vec3 u = unitv(Cc(R0, j) - Eb(R0, j));
        if (!sg0) {
            m = fdot(pr.pos - Cc(R0, j), Xc(R0, j));
            const vec3 L = scl(tl, u) + ((div0 && m >= 0.0f) ? Cc(R0, j) : Eb(R0, j));
            vec3 v = L - P;
            float r = fdot(Xc(R0, phi), v), f = fdot(Zc(R0, phi), v);
            if (k == 1 && f < 1.0f) f = 1.0f;
            lamL = float(bearing(r, f));
            const vec3 R = ((div0 && m < 0.0f) ? Cc(R0, j) : Ea(R0, j)) - scl(tr, u);
            v = R - P;
            r = fdot(Xc(R0, phi), v);
            f = fdot(Zc(R0, phi), v);
            if (k == 1 && f < 1.0f) f = 1.0f;
            lamR = float(bearing(r, f));
        } else {
            m = -fdot(Xc(R0, j), pr.pos - Cc(R0, j));
            const vec3 L = ((div0 && m >= 0.0f) ? Cc(R0, j) : Ea(R0, j)) - scl(tl, u);
            vec3 v = L - P;
            float r = rightOf(R0, 1, j, v), f = aheadOf(R0, 1, j, v);
            if (k == 1 && f < 1.0f) f = 1.0f;
            lamL = float(bearing(r, f));
            const vec3 R = scl(tr, u) + ((div0 && m < 0.0f) ? Cc(R0, j) : Eb(R0, j));
            v = R - P;
            r = rightOf(R0, 1, j, v);
            f = aheadOf(R0, 1, j, v);
            if (k == 1 && f < 1.0f) f = 1.0f;
            lamR = float(bearing(r, f));
        }
    }
    int sL = s, qL = q, sR = s, qR = q;
    // 3. First scanned section.
    ++q;
    if (q == N) {
        if (roadOfSlot(s + 1) >= 0) {
            const float as = aSlot(s);
            if (as < -0.7f || as > 0.7f) {
                --q;
                rk.pos = JSlot(s);
                recordPoint(c, k, s, q);
                return;
            }
            if (s + 1 == f_) --q;
            else {
                ++s;
                q = 0;
            }
        } else {
            --q;
        }
    }
    // 4. Default target on S_s.
    auto defaultTarget = [&](int slot, int qq, bool first) {
        const Road& Rd = net.roads()[size_t(roadOfSlot(slot))];
        const int Nd = Rd.n;
        const float h = (Rd.halfWidth - Rx_) - 1.0f;
        float cc;
        if (sigmaOfSlot(slot) == 0) {
            if (first && k == 1 && qq < 1) {
                cc = lambda0_;
            } else {
                cc = pclamp(-fdot(Xc(Rd, std::clamp(phi, 0, Nd - 1)), Cc(Rd, Nd - 1 - qq) - P), h);
                if (first && k == 1) lambda0_ = cc;
            }
            const float g = cc / h;
            const vec3 E = Ea(Rd, 0) - scl(tr, Xc(Rd, 0));
            rk.pos = sc(E - Cc(Rd, 0), g) + Cc(Rd, 0);
        } else {
            if (first && k == 1 && qq < 1) {
                cc = lambda0_;
            } else {
                cc = pclamp(fdot(Xc(Rd, qq), Cc(Rd, qq) - P), h);
                if (first && k == 1) lambda0_ = cc;
            }
            const float g = cc / h;
            const vec3 E = scl(tr, Xc(Rd, qq)) + Eb(Rd, Nd - 1);
            rk.pos = sc(E - Cc(Rd, Nd - 1), g) + Cc(Rd, Nd - 1);
        }
    };
    defaultTarget(s, q, true);
    // Previous point's frame (§14: N′ − prev.prog, or N′ − 1 if equal to N′).
    const int prevRoad = roadOfSlot(pr.slot);
    const Road& Rp = net.roads()[size_t(prevRoad >= 0 ? prevRoad : roadOfSlot(s))];
    const int sgp = sigmaOfSlot(pr.slot);
    int jp = sgp ? pr.prog : Rp.n - pr.prog;
    if (!sgp && jp == Rp.n) jp = Rp.n - 1;
    jp = std::clamp(jp, 0, Rp.n - 1);
    // 5. Scan.
    int stop = 0;  // 1 B1, 2 B2, 3 window open (step 6)
    while (roadOfSlot(s) >= 0 && q < net.roads()[size_t(roadOfSlot(s))].n) {
        const Road& Sc = net.roads()[size_t(roadOfSlot(s))];
        const int sg = sigmaOfSlot(s);
        const bool dv = divided(Sc);
        const int Ns = Sc.n;
        const int j = sg ? q : Ns - 1 - q;
        const vec3 u = unitv(Cc(Sc, j) - Eb(Sc, j));
        const vec3 L = sg ? ((dv && m >= 0.0f) ? Cc(Sc, j) : Ea(Sc, j)) - scl(tl, u)
                          : scl(tl, u) + ((dv && m >= 0.0f) ? Cc(Sc, j) : Eb(Sc, j));
        vec3 v = L - P;
        float r = tiny(rightOf(Rp, sgp, jp, v), -1.0f), f = aheadOf(Rp, sgp, jp, v);
        if (f < 1.0f) f = 1.0f;
        double th = bearing(r, f);
        if (th > double(lamR)) {
            stop = 1;
            break;
        }
        if (th > double(lamL - 0.001f)) {
            lamL = float(th);
            sL = s;
            qL = q;
        }
        const vec3 R = sg ? scl(tr, u) + ((dv && m < 0.0f) ? Cc(Sc, j) : Eb(Sc, j))
                          : ((dv && m < 0.0f) ? Cc(Sc, j) : Ea(Sc, j)) - scl(tr, u);
        v = R - P;
        r = tiny(rightOf(Rp, sgp, jp, v), 0.0f);
        f = aheadOf(Rp, sgp, jp, v);
        if (f < 1.0f) f = 1.0f;
        th = bearing(r, f);
        if (th < double(lamL)) {
            stop = 2;
            break;
        }
        if (th < double(lamR + 0.001f)) {
            lamR = float(th);
            sR = s;
            qR = q;
        }
        // 3. End of road.
        if (q == Ns - 1) {
            if (s < 2) {
                const bool same = qL == q && qR == q && sL == s && sR == s;
                if ((a_[s] < -0.7f && same) || (a_[s] > 0.7f && same)) {
                    rk.pos = J_[s];
                    recordPoint(c, k, s, q);
                    return;
                }
            }
            if (hdist(P, rk.pos) + pr.dist >= orders_.kp) {
                stop = 3;
                break;
            }
            if (s >= 2) {
                stop = 3;
                break;
            }
            if (s + 1 >= f_) {
                if (destKind_ == 3) {
                    destinationStep(c, k);
                    return;
                }
                FINAL_ = 1;
                stop = 3;
                break;
            }
            if (roadOfSlot(s + 1) < 0) {
                stop = 3;
                break;
            }
            ++s;
            q = -1;
            const Road& Rn = net.roads()[size_t(roadOfSlot(s))];
            const int Nn = Rn.n;
            const float hp = (Rn.halfWidth - Rx_) - 1.0f;
            if (sigmaOfSlot(s) == 0) {
                const float cc = pclamp(-fdot(Xc(Rn, Nn - 1), Cc(Rn, Nn - 1) - rk.pos), hp);
                const float g = cc / hp;
                const vec3 E = Ea(Rn, 0) - scl(tr, Xc(Rn, 0));
                rk.pos = sc(E - Cc(Rn, 0), g) + Cc(Rn, 0);
            } else {
                const float cc = pclamp(fdot(Xc(Rn, 0), Cc(Rn, 0) - rk.pos), hp);
                const float g = cc / hp;
                const vec3 E = scl(tr, Xc(Rn, Nn - 1)) + Eb(Rn, Nn - 1);
                rk.pos = sc(E - Cc(Rn, Nn - 1), g) + Cc(Rn, Nn - 1);
            }
        }
        ++q;
    }
    if (stop == 0) stop = 3;  // the scan ran out
    const int scanSigma = sigmaOfSlot(s);
    auto roadAt = [&](int slot) -> const Road& {
        const int rr = roadOfSlot(slot);
        return net.roads()[size_t(rr >= 0 ? rr : roadOfSlot(s) >= 0 ? roadOfSlot(s) : prevRoad)];
    };
    if (stop == 1 || stop == 2) {
        int outS, outQ;
        if (stop == 1) {
            const Road& Ra = roadAt(sR);
            const bool dv = divided(Ra);
            if (sigmaOfSlot(sR)) {
                const vec3 u = unitv(Cc(Ra, qR) - Eb(Ra, qR));
                rk.pos = scl(tr, u) + ((dv && m < 0.0f) ? Cc(Ra, qR) : Eb(Ra, qR));
                if (scanSigma == 1 && qR == Ra.n - 1 && sR <= 2 && aSlot(sR) > 0.7f) rk.pos = JSlot(sR);
            } else {
                const int j = Ra.n - qR - 1;
                const vec3 u = unitv(Cc(Ra, j) - Ea(Ra, j));
                rk.pos = scl(tr, u) + ((dv && m < 0.0f) ? Cc(Ra, j) : Ea(Ra, j));
            }
            outS = sR;
            outQ = qR;
        } else {
            const Road& Ra = roadAt(sL);
            const bool dv = divided(Ra);
            if (sigmaOfSlot(sL)) {
                const vec3 u = unitv(Cc(Ra, qL) - Ea(Ra, qL));
                rk.pos = scl(tl, u) + ((dv && m >= 0.0f) ? Cc(Ra, qL) : Ea(Ra, qL));
                if (scanSigma == 1 && qL == Ra.n - 1 && sL <= 2 && aSlot(sL) < -0.7f) rk.pos = JSlot(sL);
            } else {
                const int j = Ra.n - qL - 1;
                const vec3 u = unitv(Cc(Ra, j) - Eb(Ra, j));
                rk.pos = scl(tl, u) + ((dv && m >= 0.0f) ? Cc(Ra, j) : Eb(Ra, j));
            }
            outS = sL;
            outQ = qL;
        }
        recordPoint(c, k, outS, outQ);
        return;
    }
    // 6. Window still open.
    const vec3 prev = pr.pos;
    const int pv = pr.prog;
    const Road& RR = roadAt(sR);
    const Road& RL = roadAt(sL);
    auto qualifies = [&](int sa, int qa, const Road& Ra) { return sa < s || qa < Ra.n - 1; };
    int outS = s, outQ = q;
    const int Np = Rp.n;
    if (sigmaOfSlot(sR) == 0 && qualifies(sR, qR, RR)) {
        const int N2 = RR.n;
        if (divided(RR) && m < 0.0f) {
            const int j = N2 - qR - 1;
            rk.pos = Cc(RR, j) - scl(tr, unitv(Cc(RR, j) - Eb(RR, j)));
        } else {
            const int pv1 = std::max(pv, 1);
            if (sgp) {
                const float w = fdot(prev - Eb(Rp, pv1), Xc(Rp, pv1));
                rk.pos = scl(w, Xc(Rp, pv1)) + Ea(RR, N2 - qR - 1);
            } else {
                const int jj = Np - pv1;
                const float w = -fdot(prev - Ea(Rp, jj - 1), Xc(Rp, jj));
                rk.pos = Ea(RR, N2 - qR - 1) - scl(w, Xc(Rp, jj));
            }
        }
        FINAL_ = 0;
        outS = sR;
        outQ = qR;
    } else if (sigmaOfSlot(sL) == 0 && qualifies(sL, qL, RL)) {
        const int N2 = RL.n;
        if (divided(RL) && m > 0.0f) {
            const int j = N2 - qL - 1;
            rk.pos = scl(tr, unitv(Cc(RL, j) - Eb(RL, j))) + Cc(RL, j);
        } else {
            const int pv1 = std::max(pv, 1);
            if (sgp) {
                const float w = fdot(prev - Eb(Rp, pv1), Xc(Rp, pv1));
                rk.pos = scl(w, Xc(Rp, pv1)) + Ea(RL, N2 - qL - 1);
            } else {
                const int jj = Np - pv1;
                const float w = -fdot(prev - Ea(Rp, jj - 1), Xc(Rp, jj));
                rk.pos = Ea(RL, N2 - qL - 1) - scl(w, Xc(Rp, jj));
            }
        }
        FINAL_ = 0;
        outS = sL;
        outQ = qL;
    } else if (sigmaOfSlot(sR) == 1 && qualifies(sR, qR, RR)) {
        if (divided(RR) && m >= 0.0f) {
            rk.pos = scl(tr, unitv(Cc(RR, qR) - Eb(RR, qR))) + Cc(RR, qR);
        } else {
            float w;
            if (sgp) {
                w = fdot(prev - Eb(Rp, pv), Xc(Rp, pv));
            } else {
                const int jj = Np - pv - 1;
                w = -fdot(prev - Ea(Rp, jj), Xc(Rp, jj));
            }
            if (w < Rx_) w = Rx_;
            rk.pos = scl(w, Xc(RR, qR)) + Eb(RR, qR);
        }
        FINAL_ = 0;
        outS = sR;
        outQ = qR;
    } else if (sigmaOfSlot(sL) == 1 && qualifies(sL, qL, RL)) {
        if (divided(RL) && m < 0.0f) {
            rk.pos = Cc(RL, qL) - scl(tl, Xc(RL, qL));
        } else {
            float w;
            if (sgp) {
                w = -fdot(prev - Ea(Rp, pv), Xc(Rp, pv));
            } else {
                const int jj = Np - pv - 1;
                w = fdot(prev - Eb(Rp, jj), Xc(Rp, jj));
            }
            if (w < Lx_) w = Lx_;
            rk.pos = Ea(RL, qL) - scl(w, Xc(RL, qL));
        }
        FINAL_ = 0;
        outS = sL;
        outQ = qL;
    }
    recordPoint(c, k, outS, outQ);
}

// ---- §16-17 Obstacles ----

Obstacle Driver::obstacleScan(Ctx& c, const vec3& A, const vec3& B, int sA, int qA, int sB, int qB, float M, int& cls) {
    const RoadNetwork& net = c.net;
    const float Wd = Lx_ + Rx_;
    float best = 99999.0f;
    Obstacle out;
    cls = -1;
    const TrafficSystem* ts = c.env.traffic && c.env.traffic->active() && orders_.sw[1] ? c.env.traffic : nullptr;
    const bool props = orders_.sw[2] && c.env.world && c.env.propOfCityObject;
    auto test = [&](const Obstacle& o, int k) {
        const float bd = blockDistance(c, o, A, B, M, Wd);
        if (bd > -1.0f && bd < best) {
            best = bd;
            out = o;
            cls = k;
        }
    };
    auto testCars = [&](const std::vector<int>& list) {
        for (int ci : list) test(Obstacle{Obstacle::TrafficCarObs, ci}, 1);
    };
    auto testProps = [&](const std::vector<int>& list, bool heavyOnly, int road, int ix) {
        for (int oi : list) {
            const int co = obstacleObject(oi, *c.env.propOfCityObject, *c.env.world);  // level objects too
            if (co < 0) continue;
            const phys::CollisionObject& ob = c.env.world->object(co);
            if (ob.broken) continue;  // engine: a knocked prop is gone
            if (heavyOnly && !(ob.impulseLimit2 > 250000.0f)) continue;
            Obstacle o{Obstacle::PropObs, co};
            o.regRoad = road;
            o.regIx = ix;
            test(o, 5);
        }
    };
    // 1.
    if (roadOfSlot(sB) >= 0 && qB > net.roads()[size_t(roadOfSlot(sB))].n) {
        ++sB;
        qB = 1;
    }
    // 2.
    bool firstRoad = true;
    bool done = false;
    for (int s = sA; s <= sB && !done; ++s) {
        const int R = roadOfSlot(s);
        if (R < 0) continue;
        const Road& r = net.roads()[size_t(R)];
        const int N = r.n;
        const int sg = sigmaOfSlot(s);
        const int q0 = firstRoad ? qA : 0;
        firstRoad = false;
        for (int q = std::max(q0, 0); q <= N - 1; ++q) {
            if (q == 0) {
                const int I = sg ? r.end[1].intersection : r.end[0].intersection;
                if (ts) testCars(ts->intersectionCars(I));
                if (props && I >= 0 && size_t(I) < net.intersections().size())
                    testProps(net.intersections()[size_t(I)].obstacles, true, -1, I);
            } else {
                const int secB = sg ? q : N - q;
                const int secA = sg ? N - q : q;
                const int secP = sg ? q : N - q;
                if (ts) testCars(ts->sectionCars(R, 1, secB));
                if (props && size_t(secP) < r.obstacles[1].size()) testProps(r.obstacles[1][size_t(secP)], true, R, -1);
                if (ts) testCars(ts->sectionCars(R, -1, secA));
                if (props && size_t(secP) < r.obstacles[0].size()) testProps(r.obstacles[0][size_t(secP)], false, R, -1);
            }
            if ((q == qB && s == sB) || out.kind != Obstacle::None) {
                done = true;
                break;
            }
        }
    }
    // 3. Players (cops: off).
    if (orders_.sw[3]) {
        for (size_t i = 0; i < c.env.players.size(); ++i) {
            const DriverPlayer& pl = c.env.players[i];
            if (!pl.car || pl.car == car_ || pl.road < 0) continue;
            if (pl.road != S_[0] && pl.road != S_[1] && pl.road != S_[2]) continue;
            test(Obstacle{Obstacle::PlayerObs, int(i)}, 0);
        }
    }
    // 4. Opponents (race modes), only once w > 2: each whose current road is in the window and
    // whose racer number differs from this driver's.
    if (orders_.sw[4] && w_ > 2) {
        for (size_t i = 0; i < c.env.opponents.size(); ++i) {
            const DriverOpponent& op = c.env.opponents[i];
            if (!op.car || !op.driver || op.car == car_ || op.number == racer_) continue;
            const int road = op.driver->window(0);  // its S0 (driver.md, "What others read")
            if (road < 0 || (road != S_[0] && road != S_[1] && road != S_[2])) continue;
            test(Obstacle{Obstacle::OpponentObs, int(i)}, 2);
        }
    }
    return out;
}

// §17.1 for a vehicle with box extents Fo, Bo, Lo, Ro at pos (rows X, Z): the forward distance of
// the first corner inside the cone from A toward B, or −1.
float vehicleBlockDistance(const vec3& pos, const vec3& X, const vec3& Z, float Fo, float Bo, float Lo, float Ro,
                           const vec3& A, const vec3& B, float M, float Wd) {
    const float len = hdist(A, B);
    vec3 d = B - A;
    d.y = 0.0f;
    const vec3 u = unitv(d);
    const vec3 base0 = pos - scl(Fo, Z);
    const vec3 base1 = scl(Bo, Z) + pos;
    const vec3 corners[4] = {base0 - scl(Lo, X), scl(Ro, X) + base0, base1 - scl(Lo, X), scl(Ro, X) + base1};
    const float half = Wd * 0.5f + 1.0f;
    for (const vec3& cn : corners) {
        const vec3 v = cn - A;
        const float lat = u.x * v.z + v.x * (-u.z);
        const float fwd = v.z * u.z + v.x * u.x;
        const float a = float(std::atan2(double(lat), double(fwd)));
        if (-half < lat && lat < half && fwd > 0.0f && fwd < len + M && -0.69999999f < a && a < 0.69999999f) return fwd;
    }
    return -1.0f;
}

float Driver::blockDistance(Ctx& c, const Obstacle& o, const vec3& A, const vec3& B, float M, float Wd) {
    const float len = hdist(A, B);
    if (o.kind == Obstacle::TrafficCarObs || o.kind == Obstacle::PlayerObs || o.kind == Obstacle::OpponentObs) {
        vec3 pos, X, Z;
        float Fo, Bo, Lo, Ro;
        if (o.kind == Obstacle::OpponentObs) {
            const DriverOpponent& op = c.env.opponents[size_t(o.index)];
            pos = op.car->body().p;
            X = op.car->rotation().r[0];
            Z = op.car->rotation().r[2];
            Fo = op.driver->front();
            Bo = op.driver->back();
            Lo = op.driver->left();
            Ro = op.driver->right();
        } else if (o.kind == Obstacle::TrafficCarObs) {
            const TrafficCar& tc = c.env.traffic->cars()[size_t(o.index)];
            const TrafficType& tt = c.env.traffic->typeOf(tc);
            pos = tc.pos;
            X = tc.X;
            Z = tc.Z;
            Fo = tt.front;
            Bo = tt.back;
            Lo = tt.left;
            Ro = tt.right;
        } else {
            const phys::Car* pc = c.env.players[size_t(o.index)].car;
            pos = pc->body().p;
            X = pc->rotation().r[0];
            Z = pc->rotation().r[2];
            const vec3 size = pc->tune().sim.inertiaBox;
            Fo = Bo = size.z * 0.5f;
            Lo = Ro = size.x * 0.5f;
        }
        return vehicleBlockDistance(pos, X, Z, Fo, Bo, Lo, Ro, A, B, M, Wd);
    }
    if (o.kind == Obstacle::PropObs) {
        const phys::CollisionObject& ob = c.env.world->object(o.index);
        const vec3 d = B - A;
        const float q = (d.z * d.z + d.y * d.y) + d.x * d.x;
        const float inv = q == 0.0f ? 0.0f : 1.0f / std::sqrt(q);
        const vec3 u = sc(d, inv);
        const vec3 op = obstaclePosition(c, o);
        const float radius = ob.yRadius > 2.0f ? 2.0f : ob.yRadius;
        const float half = (radius + Wd * 0.5f) + 1.0f;
        const float lat = u.x * (op.z - A.z) + (op.x - A.x) * (-u.z);
        const float fwd = (op.z - A.z) * u.z + (op.x - A.x) * u.x;
        const float a = float(std::atan2(double(lat), double(fwd)));
        if (-half < lat && lat < half && fwd > 0.0f && fwd < len + M && -0.69999999f < a && a < 0.69999999f) return fwd;
        return -1.0f;
    }
    return -1.0f;
}

// ---- §18 Detours ----

void Driver::passingPoints(Ctx& c, const Obstacle& o, const vec3& from, const vec3& dir, float clr, vec3& A, vec3& B) {
    if (o.kind == Obstacle::PropObs) {
        const phys::CollisionObject& ob = c.env.world->object(o.index);
        const vec3 op = obstaclePosition(c, o);
        const vec3 uq = op - from;
        const float sum = (uq.z * uq.z + uq.y * uq.y) + uq.x * uq.x;
        const float inv = sum == 0.0f ? 0.0f : 1.0f / std::sqrt(sum);
        const float ex = inv * uq.x, nz = -(inv * uq.z);
        const float r = std::min(ob.radius, 2.0f) + clr;
        B = vec3(nz * r + op.x, op.y + 0.0f, op.z + r * ex);
        A = vec3(op.x - r * nz, op.y - 0.0f, op.z - r * ex);
        return;
    }
    vec3 pos, X, Z;
    float Fo = 0, Bo = 0, Lo = 0, Ro = 0;
    if (o.kind == Obstacle::TrafficCarObs) {
        const TrafficCar& tc = c.env.traffic->cars()[size_t(o.index)];
        const TrafficType& tt = c.env.traffic->typeOf(tc);
        pos = tc.pos;
        X = tc.X;
        Z = tc.Z;
        Fo = tt.front;
        Bo = tt.back;
        Lo = tt.left;
        Ro = tt.right;
    } else if (o.kind == Obstacle::PlayerObs) {
        const phys::Car* pc = c.env.players[size_t(o.index)].car;
        pos = pc->body().p;
        X = pc->rotation().r[0];
        Z = pc->rotation().r[2];
        const vec3 size = pc->tune().sim.inertiaBox;
        Fo = Bo = size.z * 0.5f;
        Lo = Ro = size.x * 0.5f;
    } else if (o.kind == Obstacle::OpponentObs) {
        const DriverOpponent& op = c.env.opponents[size_t(o.index)];
        pos = op.car->body().p;
        X = op.car->rotation().r[0];
        Z = op.car->rotation().r[2];
        Fo = op.driver->front();
        Bo = op.driver->back();
        Lo = op.driver->left();
        Ro = op.driver->right();
    }
    const vec3 base0 = pos - scl(Fo, Z);
    const vec3 base1 = scl(Bo, Z) + pos;
    const vec3 corners[4] = {base0 - scl(Lo, X), scl(Ro, X) + base0, base1 - scl(Lo, X), scl(Ro, X) + base1};
    const vec3 h = normalise(dir);
    float bestA = 99999.0f, bestB = -99999.0f;
    A = B = pos;
    for (const vec3& cn : corners) {
        const vec3 e = normalise(cn - from);
        const vec3 nv((-e.z) * clr, e.y * clr, e.x * clr);
        const vec3 cands[2] = {nv + cn, cn - nv};
        for (const vec3& cd : cands) {
            const vec3 w = cd - from;
            const double a = std::atan2(double(h.x * w.z + w.x * (-h.z)), double(w.z * h.z + w.x * h.x));
            if (a < double(bestA)) {
                bestA = float(a);
                A = cd;
            }
            if (a > double(bestB)) {
                bestB = float(a);
                B = cd;
            }
        }
    }
}

bool Driver::windowPlace(Ctx& c, const Obstacle& o, int& wo, int& ov) {
    const RoadNetwork& net = c.net;
    auto farEnd = [&](int slot) {
        const Road& r = net.roads()[size_t(S_[slot])];
        return d_[slot] > 0 ? r.end[0].intersection : r.end[1].intersection;
    };
    if (o.kind == Obstacle::TrafficCarObs) {
        const TrafficCar& tc = c.env.traffic->cars()[size_t(o.index)];
        // The car's route place (traffic-reactions.md): rail road, piece index, travel direction.
        const int road = tc.placeRoad;
        const int sx = tc.placeK;
        const int dir = tc.placeSide;
        for (int k = 0; k < 3; ++k) {
            if (S_[k] < 0 || S_[k] != road) continue;
            const int N = net.roads()[size_t(road)].n;
            const int sg = d_[k] > 0 ? 1 : 0;
            if (sx != N) {
                wo = k;
                ov = ((sg == 0 && dir == 1) || (sg == 1 && dir != 1)) ? N - sx : sx;
            } else if (sg == 0) {
                wo = k;
                ov = 0;
            } else if (k + 1 > 2 || S_[k + 1] < 0) {
                wo = k;
                ov = N - 1;
            } else {
                wo = k + 1;
                ov = 0;
            }
            return true;
        }
        if (road >= 0) {
            const int toward = net.exitIntersection(road, dir);
            for (int k = 0; k < 3; ++k)
                if (S_[k] >= 0 && farEnd(k) == toward) {
                    wo = k + 1;
                    ov = 0;
                    return true;
                }
        }
        wo = 0;
        ov = 0;
        return true;
    }
    if (o.kind == Obstacle::OpponentObs) {
        // An AI-driven car: its S0 and its section on it (converted with its own side); the first
        // window slot holding that road, else −1.
        const DriverOpponent& op = c.env.opponents[size_t(o.index)];
        const int road = op.driver->window(0);
        for (int k = 0; k < 3; ++k)
            if (road >= 0 && S_[k] == road) {
                wo = k;
                ov = op.driver->sectionOnS0(net);
                return true;
            }
        wo = -1;
        return false;
    }
    if (o.kind == Obstacle::PropObs) {
        const vec3 op = obstaclePosition(c, o);
        if (o.regRoad >= 0) {
            for (int k = 0; k < 3; ++k)
                if (S_[k] == o.regRoad) {
                    wo = k;
                    ov = net.sectionOfPoint(S_[k], op, d_[k]);
                    return true;
                }
            wo = -1;
            return false;
        }
        if (o.regIx >= 0) {
            for (int k = 0; k < 2; ++k)
                if (S_[k] >= 0 && farEnd(k) == o.regIx) {
                    wo = k + 1;
                    ov = 1;
                    return true;
                }
            if (S_[0] >= 0) {
                const Road& r = net.roads()[size_t(S_[0])];
                const int nearEnd = d_[0] > 0 ? r.end[1].intersection : r.end[0].intersection;
                if (nearEnd == o.regIx) {
                    wo = 0;
                    ov = 1;
                    return true;
                }
            }
        }
        wo = -1;
        return false;
    }
    wo = -1;
    return false;
}

bool Driver::aheadStd(Ctx& c, int k, const vec3& Y) {
    const PathRecord& pr = Q_[size_t(k - 1)];
    const float ux = Y.x - pr.pos.x, uz = Y.z - pr.pos.z;
    const int R = roadOfSlot(pr.slot);
    if (R < 0) return false;
    const Road& r = c.net.roads()[size_t(R)];
    const int N = r.n;
    float yv, xv;
    if (sigmaOfSlot(pr.slot)) {
        const int i = std::clamp(pr.prog, 0, N - 1);
        yv = (-Xc(r, i).z) * uz + (-Xc(r, i).x) * ux;
        xv = (-Zc(r, i).z) * uz + (-Zc(r, i).x) * ux;
    } else {
        const int i = std::clamp(N - pr.prog - 1, 0, N - 1);
        yv = uz * Xc(r, i).z + ux * Xc(r, i).x;
        xv = uz * Zc(r, i).z + ux * Zc(r, i).x;
    }
    const double a = std::atan2(double(yv), double(xv));
    return -double(1.57f) < a && a < double(1.57f);
}

void Driver::collectAlternatives(Ctx& c, int k, const Obstacle& O, bool walk, std::vector<Alt>& out) {
    const PathRecord& pr = Q_[size_t(k - 1)];
    // 1.
    if (pr.mark == 1) {
        out.push_back(Alt{Q_[size_t(k)].pos, O, 1, 1});
        return;
    }
    // 2.
    vec3 from, dir;
    if (k == 1) {
        from = Q_[0].pos;
        dir = -c.rowZ;
    } else {
        from = pr.pos;
        dir = pr.pos - Q_[size_t(k - 2)].pos;
    }
    vec3 A, B;
    passingPoints(c, O, from, dir, Rx_ + 2.0f, A, B);
    // 3. Ahead tests (k = 1: the car's axes; else the road frame, swapped for B).
    auto ahead = [&](const vec3& Y, bool swapped) {
        const float ux = Y.x - pr.pos.x, uz = Y.z - pr.pos.z;
        float yv, xv;
        if (k == 1) {
            yv = uz * c.rowX.z + ux * c.rowX.x;
            xv = (-c.rowZ.z) * uz + (-c.rowZ.x) * ux;
        } else {
            const int R = roadOfSlot(pr.slot);
            if (R < 0) return false;
            const Road& r = c.net.roads()[size_t(R)];
            const int N = r.n;
            const bool sg = sigmaOfSlot(pr.slot) != 0;
            const bool negated = sg;
            int i = sg ? pr.prog : N - pr.prog - 1;
            if (swapped) i = sg ? N - pr.prog - 1 : pr.prog;
            i = std::clamp(i, 0, N - 1);
            if (negated) {
                yv = (-Xc(r, i).z) * uz + (-Xc(r, i).x) * ux;
                xv = (-Zc(r, i).z) * uz + (-Zc(r, i).x) * ux;
            } else {
                yv = uz * Xc(r, i).z + ux * Xc(r, i).x;
                xv = uz * Zc(r, i).z + ux * Zc(r, i).x;
            }
        }
        const double a = std::atan2(double(yv), double(xv));
        return -double(1.57f) < a && a < double(1.57f);
    };
    // 4.
    if (ahead(A, false)) follow(c, k, A, O, -1, 0, walk, out);
    if (ahead(B, true)) follow(c, k, B, O, 1, 0, walk, out);
    // 5.
    if (out.empty()) out.push_back(Alt{Q_[size_t(k)].pos, O, 1, 3});
}

void Driver::follow(Ctx& c, int k, const vec3& pt, const Obstacle& O, int side, int depth, bool walk, std::vector<Alt>& out) {
    ++depth;
    if (depth == 10) return;
    int wo = -1, ov = 0;
    if (!windowPlace(c, O, wo, ov) || wo < 0 || roadOfSlot(wo) < 0) return;
    const int cl = c.net.onRoad(roadOfSlot(wo), pt, Rx_);
    if (cl != 1 && !(walk && cl == 2)) return;
    const PathRecord& pr = Q_[size_t(k - 1)];
    int c2 = -1;
    const Obstacle O2 = obstacleScan(c, pr.pos, pt, pr.slot, pr.prog, wo, ov + 2, (B_ + F_) + (B_ + F_), c2);
    if (O2.kind == Obstacle::None || c2 == 2) {
        if (out.size() < 10) out.push_back(Alt{pt, O, cl, 2});
        return;
    }
    vec3 from, dir;
    if (k == 1) {
        from = Q_[0].pos;
        dir = -c.rowZ;
    } else {
        from = pr.pos;
        dir = pr.pos - Q_[size_t(k - 2)].pos;
    }
    vec3 A2, B2;
    passingPoints(c, O2, from, dir, Rx_ + 2.0f, A2, B2);
    const vec3 pO = obstaclePosition(c, O), pO2 = obstaclePosition(c, O2);
    const float ddx = pO.x - pO2.x, ddz = pO.z - pO2.z;
    const float n2 = (pO2.x - c.p.x) * (pO2.x - c.p.x) + (pO2.z - c.p.z) * (pO2.z - c.p.z);
    const float n1 = (pO.x - c.p.x) * (pO.x - c.p.x) + (pO.z - c.p.z) * (pO.z - c.p.z);
    if (ddx * ddx + ddz * ddz > 225.0f && n2 < n1) {
        const vec3& cand = side == -1 ? B2 : A2;
        const float ex = pr.pos.x - pO2.x, ez = pr.pos.z - pO2.z;
        if (aheadStd(c, k, cand) && ex * ex + ez * ez < orders_.kp * orders_.kp && c2 == 1 && out.size() < 10)
            out.push_back(Alt{cand, O2, cl, 2});
    }
    if (side == -1) {
        if (aheadStd(c, k, A2)) follow(c, k, A2, O2, -1, depth, walk, out);
    } else {
        if (aheadStd(c, k, B2)) follow(c, k, B2, O2, 1, depth, walk, out);
    }
}

void Driver::useAlternative(Ctx& c, int k, const Alt& a, int cls) {
    int wo = -1, ov = 0;
    windowPlace(c, a.o, wo, ov);
    if (wo < 0) return;
    if (roadOfSlot(wo) < 0) {
        --wo;
        if (wo < 0 || roadOfSlot(wo) < 0) return;
        ov = c.net.roads()[size_t(roadOfSlot(wo))].n - 1;
    }
    ov = std::clamp(ov, 0, c.net.roads()[size_t(roadOfSlot(wo))].n - 1);
    PathRecord& rk = Q_[size_t(k)];
    const PathRecord& pr = Q_[size_t(k - 1)];
    rk.mark = a.mark;
    rk.cls = cls;
    rk.obj = a.o;
    rk.zone = a.zone;
    rk.pos = a.pt;
    rk.pos.y = rk.pos.y + 1.0f;
    const float dz = pr.pos.z - rk.pos.z, dx = pr.pos.x - rk.pos.x;
    rk.dist = std::sqrt(dz * dz + dx * dx) + pr.dist;
    if (k == 1) rk.cost = float(angleOf(-c.rowZ, rk.pos - c.p));
    else rk.cost = float(angleOf(pr.pos - Q_[size_t(k - 2)].pos, rk.pos - pr.pos) + double(pr.cost));
    rk.prog = ov;
    rk.slot = wo;
    cont(c, k);
}

}  // namespace ai
}  // namespace cc
