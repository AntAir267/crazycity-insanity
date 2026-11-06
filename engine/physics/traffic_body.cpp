#include "physics/traffic_body.h"

#include <cmath>

#include "physics/pmath.h"

namespace cc {
namespace phys {

std::shared_ptr<Bound> makeOffsetBox(const vec3& size, const vec3& c) {
    return std::make_shared<Bound>(makeBoxBound(size, c));
}

void setupTrafficBodyType(TrafficBodyType& t) {
    t.box = makeOffsetBox(t.size, t.cg);
    t.material = Material();
    t.material.friction = 1.0f;
    t.material.elasticity = 0.5f;
}

void restDetect(RestDetector& rest, RigidBody& rb, float invdt, float thrV, float thrW) {
    bool moving = false;
    const vec3 w = rb.w;
    const float w2 = (w.x * w.x + w.y * w.y) + w.z * w.z;
    if (w2 > thrW) {
        rest.Wsum = vec3(w.x + rest.Wsum.x, w.y + rest.Wsum.y, w.z + rest.Wsum.z);
        const vec3& S = rest.Wsum;
        if (!(1.8f * thrW >= (S.x * S.x + S.y * S.y) + S.z * S.z)) {
            rest.Wsum = w;
            moving = true;
        }
    }
    rest.Wsum = w;
    const vec3 u(((rb.push.x + rb.pushSum.x) * invdt) + rb.v.x, ((rb.push.y + rb.pushSum.y) * invdt) + rb.v.y,
                 ((rb.push.z + rb.pushSum.z) * invdt) + rb.v.z);
    const float u2 = (u.z * u.z + u.y * u.y) + u.x * u.x;
    if (u2 > thrV) {
        rest.Usum = vec3(u.x + rest.Usum.x, u.y + rest.Usum.y, u.z + rest.Usum.z);
        const vec3& S = rest.Usum;
        if (!(1.8f * thrV >= (S.x * S.x + S.y * S.y) + S.z * S.z)) {
            rest.Usum = u;
            moving = true;
        }
    }
    rest.Usum = u;
    const vec3 g(rb.G.x * rb.invM + rb.v.x, rb.G.y * rb.invM + rb.v.y, rb.G.z * rb.invM + rb.v.z);
    if (((g.x * g.x + g.y * g.y) + g.z * g.z) - ((rb.v.x * rb.v.x + rb.v.y * rb.v.y) + rb.v.z * rb.v.z) > thrV) moving = true;
    auto clearAccumulators = [&]() {
        rb.F = rb.T = rb.Fpost = rb.Tpost = rb.G = rb.H = vec3();
        rb.A = rb.B = rb.C = Mat3();
        rb.implicit = false;
        rb.turn = vec3();
    };
    if (!moving) {
        if (rest.state == 1) {
            rest.still = rest.still + 1;
            if (rest.still >= 15) {
                rest.state = 0;
                rest.still2 = 0;
                rb.P = rb.L = rb.v = rb.w = vec3();
                clearAccumulators();
                rb.lastPush = vec3(rb.push.x + rb.pushSum.x, rb.push.y + rb.pushSum.y, rb.push.z + rb.pushSum.z);
                rb.pushSum = vec3();
                rb.push = vec3();
            }
        } else {
            clearAccumulators();
            rest.still2 = rest.still2 + 1;
            if (rest.still2 >= 120) rest.state = 2;
        }
    } else if (rest.state == 1) {
        rest.still = rest.still2 = 0;
    } else {
        rest.reset();
    }
}

void TrafficPhys::setup(const TrafficBodyType& t, const Mtx& lane, float laneSpeed) {
    rb.reset();
    rb.R.r[0] = lane.X;
    rb.R.r[1] = lane.Y;
    rb.R.r[2] = lane.Z;
    rb.p = lane.o;
    const float m = t.mass, k = 0.083333336f;
    const vec3& b = t.size;
    rb.setMass(m, vec3(((b.z * b.z + b.y * b.y) * m) * k, ((b.z * b.z + b.x * b.x) * m) * k, ((b.y * b.y + b.x * b.x) * m) * k));
    rb.vmax = 500.0f;
    rb.setAngularLimit(5.0f);
    const float s = -laneSpeed;
    rb.v = vec3(s * lane.Z.x, s * lane.Z.y, s * lane.Z.z);
    rb.P = vec3(m * rb.v.x, m * rb.v.y, m * rb.v.z);
    rb.w = rb.L = vec3();
    rest.reset();
    for (int i = 0; i < 4; ++i) {
        comp[i] = dLat[i] = dLong[i] = 0.0f;
        probe[i] = ProbeState();
    }
    prev = lane;
    this->lane = lane;
    prevLargestSource = largestSource = 0;
    largestPush2 = 0;
}

Mtx TrafficPhys::matrix() const { return mtxFromRows(rb.R.r[0], rb.R.r[1], rb.R.r[2], rb.p); }

void TrafficPhys::notePush(const vec3& push, int fromIdentity) {
    const float p2 = (push.x * push.x + push.y * push.y) + push.z * push.z;
    if (p2 > largestPush2) {
        largestPush2 = p2;
        largestSource = fromIdentity;
    }
}

void TrafficPhys::update(const TrafficBodyType& t, float dt, float invdt, World& world, float grip) {
    // 1. Gravity.
    rb.F.y = rb.m * -19.6f + rb.F.y;
    // 2. The rest detector (6.4).
    restDetect(rest, rb, invdt, 0.01f, 0.01f);
    // 3. Integration (explicit; 5 rad/s limit set at setup). Frozen: no motion, but lastPush =
    // pushSum, pushSum = 0; steps 4 and 5 run every sample, frozen or not (6.3 step 3).
    if (rest.state == 1)
        rb.integrate(dt);
    else
        rb.integrateFrozen();
    // 4. The four wheels, FL, FR, BL, BR (6.2).
    const float L = (rb.m * -19.6f) * -0.25f;
    for (int i = 0; i < 4; ++i) {
        const vec3& X = rb.R.r[0];
        const vec3& Y = rb.R.r[1];
        const vec3& Z = rb.R.r[2];
        const vec3& o = t.wheel[i];
        const vec3& p = rb.p;
        const vec3 C(((Z.x * o.z + X.x * o.x) + Y.x * o.y) + p.x, ((Z.y * o.z + X.y * o.x) + Y.y * o.y) + p.y,
                     ((Z.z * o.z + X.z * o.x) + Y.z * o.y) + p.z);
        const float r = t.wheelRadius, l = t.limit;
        const float h = r + l;
        const vec3 S(h * Y.x + C.x, h * Y.y + C.y, h * Y.z + C.z);
        const vec3 E(C.x - h * Y.x, C.y - h * Y.y, C.z - h * Y.z);
        probe[i].cacheValid = false;  // no polygon cache
        ProbeHit hit;
        const bool got = world.probe(S, E, probe[i], hit);
        probe[i].cacheValid = false;
        const vec3& n = hit.normal;
        if (!got || fsqrt((n.x * n.x + n.y * n.y) + n.z * n.z) == 0.0f) {
            comp[i] = -l;
            dLat[i] = dLong[i] = 0.0f;
            continue;
        }
        const vec3 rr(hit.point.x - p.x, hit.point.y - p.y, hit.point.z - p.z);
        const vec3& w = rb.w;
        const vec3 q(rr.z * w.y - rr.y * w.z, rr.x * w.z - rr.z * w.x, rr.y * w.x - rr.x * w.y);
        const vec3 u(q.x + rb.v.x, q.y + rb.v.y, q.z + rb.v.z);
        const float vn = (u.z * n.z + u.y * n.y) + u.x * n.x;
        const vec3 tt(u.x - vn * n.x, u.y - vn * n.y, u.z - vn * n.z);
        const vec3 D(S.x - E.x, S.y - E.y, S.z - E.z);
        const float len = fsqrt((D.z * D.z + D.y * D.y) + D.x * D.x);
        // 5. Suspension.
        const float old = comp[i];
        comp[i] = ((r + r) + l) - len * hit.t;
        float rate = (comp[i] - old) * invdt;
        if (!(rate >= -3.0f)) rate = -3.0f;
        else if (rate > 3.0f) rate = 3.0f;
        float F = (comp[i] * t.spring + rate * t.damping) + L;
        if (!(F >= 0.0f)) F = 0.0f;
        // 6. Normal force.
        const float Fn = ((n.z * Y.z + n.y * Y.y) + n.x * Y.x) * F;
        vec3 a(Fn * n.x + 0.0f, 0.0f + Fn * n.y, 0.0f + Fn * n.z);
        // 7. Slip speeds.
        const float sLat = (tt.x * X.x + X.z * tt.z) + X.y * tt.y;
        const vec3 f(-Z.x, -Z.y, -Z.z);
        const float sLong = (f.z * tt.z + f.y * tt.y) + f.x * tt.x;
        // 8. Deflection limit and targets.
        const float m = ((Fn * grip) * 0.4f) / t.rubberSpring;
        auto sgn = [](float x) { return x > 0.0f ? 1.0f : (x < 0.0f || std::isnan(x)) ? -1.0f : 0.0f; };
        const float tLat = sgn(sLat) * m, tLong = sgn(sLong) * m;
        // 9. Deflections.
        const float dLat0 = dLat[i], dLong0 = dLong[i];
        auto deflect = [&](float& d, float s, float tau) {
            const float step = s * dt;
            float lo, hi;
            if (!(step >= 0.0f)) {
                lo = step + d;
                hi = d;
            } else {
                lo = d;
                hi = step + d;
            }
            if (!(tau >= lo)) d = lo;
            else if (tau <= hi) d = tau;
            else d = hi;
        };
        deflect(dLat[i], sLat, tLat);
        deflect(dLong[i], sLong, tLong);
        // 10. Tyre forces.
        const float K = t.rubberSpring, Dm = t.rubberDamp;
        const float fLat = -(K * dLat[i]) - ((dLat[i] - dLat0) * invdt) * Dm;
        const float fLong = -(K * dLong[i]) - ((dLong[i] - dLong0) * invdt) * Dm;
        const vec3 e(X.x * fLat - Z.x * fLong, X.y * fLat - Z.y * fLong, X.z * fLat - Z.z * fLong);
        // 11.
        a = vec3(e.x + a.x, a.y + e.y, a.z + e.z);
        rb.F = vec3(a.x + rb.F.x, a.y + rb.F.y, a.z + rb.F.z);
        const float x = hit.point.x - p.x, y = hit.point.y - p.y, z = hit.point.z - p.z;
        const vec3 tau(y * a.z - z * a.y, z * a.x - x * a.z, x * a.y - a.x * y);
        rb.T = vec3(tau.x + rb.T.x, tau.y + rb.T.y, tau.z + rb.T.z);
    }
    // 5. The lane matrix becomes the body frame.
    lane = matrix();
}

void TrafficPhys::endSample() {
    rb.applyPush();
    prev = matrix();
    prevLargestSource = largestSource;
    largestSource = 0;
    largestPush2 = 0;
}

bool trafficUpright(World& world, const Mtx& body, int room) {
    const vec3& Y = body.Y;
    const vec3& p = body.o;
    const vec3 S(Y.x * 0.5f + p.x, Y.y * 0.5f + p.y, Y.z * 0.5f + p.z);
    const vec3 E(p.x - Y.x * 3.0f, p.y - Y.y * 3.0f, p.z - Y.z * 3.0f);
    ProbeState st;
    st.roomS = st.roomE = room;
    ProbeHit hit;
    if (!world.probe(S, E, st, hit)) return false;
    const vec3& n = hit.normal;
    return double((n.y * Y.y + n.z * Y.z) + n.x * Y.x) >= 0.9;
}

}  // namespace phys
}  // namespace cc
