#include "physics/trailer.h"

#include <cmath>

#include "physics/collision_world.h"

namespace cc {
namespace phys {

namespace {

constexpr float kG = -19.6f;

// trailer.md 5.1, order table J: P·Q sums P[i][k]·Q[k][j] over k in this order, "a, b, c" =
// (t_a + t_b) + t_c; zero elements are multiplied and added like any other.
const int kJ[3][3][3] = {{{0, 1, 2}, {1, 2, 0}, {1, 2, 0}},
                         {{1, 0, 2}, {2, 1, 0}, {2, 1, 0}},
                         {{0, 2, 1}, {2, 1, 0}, {2, 1, 0}}};

Mat3 mulJ(const Mat3& P, const Mat3& Q) {
    Mat3 R;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            const float t[3] = {P.m[i][0] * Q.m[0][j], P.m[i][1] * Q.m[1][j], P.m[i][2] * Q.m[2][j]};
            const int* o = kJ[i][j];
            R.m[i][j] = (t[o[0]] + t[o[1]]) + t[o[2]];
        }
    return R;
}

Mat3 transposed(const Mat3& M) {
    Mat3 T;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) T.m[i][j] = M.m[j][i];
    return T;
}

Mat3 added(const Mat3& A, const Mat3& B) {
    Mat3 S;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) S.m[i][j] = A.m[i][j] + B.m[i][j];
    return S;
}

// skew(r): rows (0, −r.z, r.y), (r.z, 0, −r.x), (−r.y, r.x, 0).
Mat3 skew(const vec3& r) {
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
    return S;
}

Mat3 kAt(const RigidBody& b, const vec3& P) {
    float K[3][3];
    b.inverseMassMatrix(P, K);
    Mat3 M;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) M.m[i][j] = K[i][j];
    return M;
}

// 5.2 step 3: Iw = (Rᵀ·diag(I))·R with table J.
Mat3 worldInertia(const RigidBody& b) {
    const Mat3 R = toMat3(b.R);
    Mat3 D;
    D.m[0][0] = b.I.x;
    D.m[1][1] = b.I.y;
    D.m[2][2] = b.I.z;
    return mulJ(mulJ(transposed(R), D), R);
}

// 5.1 anchors.
vec3 anchorA(const RigidBody& b, const vec3& o) {
    const vec3 &X = b.R.r[0], &Y = b.R.r[1], &Z = b.R.r[2];
    return vec3(((Y.x * o.y + Z.x * o.z) + o.x * X.x) + b.p.x, ((Y.y * o.y + Z.y * o.z) + X.y * o.x) + b.p.y,
                ((Y.z * o.y + Z.z * o.z) + X.z * o.x) + b.p.z);
}
vec3 anchorB(const RigidBody& b, const vec3& o) {
    const vec3 &X = b.R.r[0], &Y = b.R.r[1], &Z = b.R.r[2];
    return vec3(((Y.x * o.y + Z.x * o.z) + X.x * o.x) + b.p.x, ((X.y * o.x + Y.y * o.y) + Z.y * o.z) + b.p.y,
                ((X.z * o.x + Y.z * o.y) + Z.z * o.z) + b.p.z);
}

// invlen(v): L = (v.x·v.x + v.y·v.y) + v.z·v.z; 0 if L = 0, else 1/√L.
float invlen(float x, float y, float z) {
    const float L = (x * x + y * y) + z * z;
    return L == 0.0f ? 0.0f : 1.0f / fsqrt(L);
}

// acos(x): s = √((1 + x)·(1 − x)) in float, atan2(s, x) in double, rounded once.
float acosF(float x) {
    const float s = fsqrt((1.0f + x) * (1.0f - x));
    return float(std::atan2(double(s), double(x)));
}

// collision.md "Quantities": v + ω × r.
vec3 pointVelocity(const RigidBody& b, const vec3& P) {
    const vec3 r = vsub(P, b.p);
    const vec3& w = b.w;
    const vec3 c(r.z * w.y - w.z * r.y, w.z * r.x - r.z * w.x, w.x * r.y - w.y * r.x);
    return vadd(c, b.v);
}

// 5.2 step 6: the acceleration terms of one body at the joint point.
void accelTerms(const RigidBody& b, const vec3& r, const Mat3& Iw, const Mat3& Ii, const Mat3& S, const vec3& f,
                const vec3& t, bool truckSums, vec3& c, vec3& ln, vec3& s) {
    const vec3& w = b.w;
    const vec3 u(r.z * w.y - r.y * w.z, r.x * w.z - r.z * w.x, r.y * w.x - r.x * w.y);
    c = vec3(u.z * w.y - u.y * w.z, u.x * w.z - u.z * w.x, u.y * w.x - u.x * w.y);
    ln = vec3(f.x * b.invM, f.y * b.invM, f.z * b.invM);
    const float (*I)[3] = Iw.m;
    vec3 L;
    if (truckSums) {
        L = vec3((I[1][0] * w.y + I[0][0] * w.x) + I[2][0] * w.z, (I[0][1] * w.x + I[1][1] * w.y) + I[2][1] * w.z,
                 (I[0][2] * w.x + I[1][2] * w.y) + I[2][2] * w.z);
    } else {
        L = vec3((I[2][0] * w.z + I[0][0] * w.x) + I[1][0] * w.y, (I[2][1] * w.z + I[0][1] * w.x) + I[1][1] * w.y,
                 (I[2][2] * w.z + I[0][2] * w.x) + I[1][2] * w.y);
    }
    const vec3 g(L.z * w.y - L.y * w.z, L.x * w.z - L.z * w.x, L.y * w.x - L.x * w.y);
    const vec3 tau(t.x - g.x, t.y - g.y, t.z - g.z);
    float al[3];
    for (int k = 0; k < 3; ++k) al[k] = (tau.z * Ii.m[2][k] + tau.y * Ii.m[1][k]) + Ii.m[0][k] * tau.x;
    float sv[3];
    for (int k = 0; k < 3; ++k) sv[k] = (al[2] * S.m[2][k] + al[1] * S.m[1][k]) + S.m[0][k] * al[0];
    s = vec3(sv[0], sv[1], sv[2]);
}

}  // namespace

Trailer::~Trailer() { unlink(); }

void Trailer::unlink() {
    if (truck_ && truck_->body().coupling == this) truck_->body().coupling = nullptr;
    if (body_.coupling == this) body_.coupling = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Set-up (2.4)

void Trailer::setup(const TrailerTune& t, Car& truck, CollisionWorld* world, bool computer) {
    tune_ = t;
    truck_ = &truck;
    world_ = world;
    computer_ = computer;
    const bool loaded = t.vehFile && t.jointFile;
    // 3. Offsets.
    offA_ = t.carHitch;
    offB_ = t.trailerHitch;
    off_ = vec3(t.carHitch.x - t.trailerHitch.x, t.carHitch.y - t.trailerHitch.y, t.carHitch.z - t.trailerHitch.z);
    // 4. Body.
    {
        const float kI = 0.083333336f;
        const vec3 b = t.inertiaBox;
        const float m = t.mass;
        const vec3 inertia(((b.z * b.z + b.y * b.y) * m) * kI, ((b.z * b.z + b.x * b.x) * m) * kI,
                           ((b.y * b.y + b.x * b.x) * m) * kI);
        body_.setMass(m, inertia);
        body_.setAngularLimit(5.0f);
        body_.vmax = 500.0f;
    }
    // 5. Wheels: the geometry, then the static load (a quarter of the weight) and the constants.
    // WheelFront was read into wheel 0 and WheelBack into wheel 2; wheels 1 and 3 still have the
    // defaults here.
    auto staticLoad = [&](Wheel& w) {
        const float a = std::fabs(w.pivot.z);
        w.L0 = (((-(t.mass * kG)) * a) * 0.5f) / (a + a);
        wheelLoadConstants(w);
    };
    for (int i = 0; i < 4; ++i) {
        Wheel& w = wheels_[i];
        w = Wheel();
        w.index = i;
        w.pivot = t.wheels[i].pivot;
        w.radius = t.wheels[i].radius;
        w.width = t.wheels[i].width;
        w.tune = i == 0 ? t.wheelFront : i == 2 ? t.wheelBack : WheelTune();
        staticLoad(w);
    }
    // 6. Trains (train i holds wheel i), the copies only when both files were loaded.
    for (int i = 0; i < 4; ++i) {
        trains_[i] = Train();
        trains_[i].wheels = {i};
        trains_[i].tune = i == 0 || loaded ? t.drivetrain : TrainTune();
    }
    if (loaded) {
        for (int src : {0, 2}) {
            WheelTune c = wheels_[src].tune;
            c.handbrakeCoef = WheelTune().handbrakeCoef;
            c.wobbleLimit = WheelTune().wobbleLimit;
            wheels_[src + 1].tune = c;
            staticLoad(wheels_[src + 1]);
        }
    }
    trainKc_ = truck.tune().sim.mass * 0.005f;
    // 7. The joint.
    status_ = t.joint.jointStatus;
    p_ = vec3();
    lean_ = 0.0f;
    detachKey_ = false;
    // 8. The hitch load on the truck: only if wheels 0 and 2 are on the same side of the origin.
    if (signBit(wheels_[0].pivot.z) == signBit(wheels_[2].pivot.z)) {
        const float W = -(kG * t.mass);
        const float zm = (wheels_[2].pivot.z + wheels_[0].pivot.z) * 0.5f;
        const float tz = t.trailerHitch.z;
        const float d = std::fabs(tz - zm);
        const float Lax = (std::fabs(tz) * W) / d;
        const float Lw = ((std::fabs(zm) * W) * 0.25f) / d;
        for (Wheel& w : wheels_) {
            w.L0 = Lw;
            wheelLoadConstants(w);
        }
        const float FLz = truck.wheel(0).pivot.z, BLz = truck.wheel(2).pivot.z;
        const float q = ((t.carHitch.z - FLz) * Lax) / (BLz - FLz);
        truck.addWheelLoad(2, q * 0.5f);
        truck.addWheelLoad(3, q * 0.5f);
        truck.addWheelLoad(0, (Lax - q) * 0.5f);
        truck.addWheelLoad(1, (Lax - q) * 0.5f);
    }
    // 2.7: the bound's material by name in the world's list (else the file's own values).
    material_ = t.boundMaterialFile;
    if (world) {
        const int mi = world->table().find(t.boundMaterialName);
        if (mi >= 0) material_ = world->table().at(mi);
    }
    // The joint couples both bodies' collision responses (6.2).
    body_.coupling = this;
    truck.body().coupling = this;
    room_ = 0;
    prevSource_ = largestSource_ = 0;
    largestPush2_ = 0.0f;
    fresh_ = true;
}

// ---------------------------------------------------------------------------------------------
// Reset (section 3)

void Trailer::resetBehind() {
    const RigidBody& T = truck_->body();
    const vec3 &X = T.R.r[0], &Y = T.R.r[1], &Z = T.R.r[2];
    const vec3& c = T.p;
    const vec3& o = off_;
    body_.reset();
    body_.R = T.R;
    body_.p = vec3(((Y.x * o.y + Z.x * o.z) + o.x * X.x) + c.x, ((Y.y * o.y + X.y * o.x) + Z.y * o.z) + c.y,
                   ((Y.z * o.y + X.z * o.x) + Z.z * o.z) + c.z);
    status_ &= ~1;
    p_ = anchorA(T, offA_);
    for (Wheel& w : wheels_) resetWheelState(w);
    for (Train& tr : trains_) {
        tr.wt = 0.0f;
        tr.diff = 1.0f;
    }
    room_ = world_ ? world_->findRoomAt(body_.p, room_) : 0;
    wheelFrame_ = frame();
    // The previous matrix and push source survive resets; the first placement sets them (check
    // runs: the starting frame and no source).
    if (fresh_) {
        prev_ = frame();
        prevSource_ = 0;
        fresh_ = false;
    }
    largestSource_ = 0;
    largestPush2_ = 0.0f;
}

void Trailer::notePush(const vec3& push, int fromIdentity) {
    const float p2 = pdot(push, push);
    if (p2 > largestPush2_) {
        largestPush2_ = p2;
        largestSource_ = fromIdentity;
    }
}

void Trailer::endSample() {
    body_.applyPush();
    prev_ = frame();
    prevSource_ = largestSource_;
    largestSource_ = 0;
    largestPush2_ = 0.0f;
}

// ---------------------------------------------------------------------------------------------
// The update (section 4)

void Trailer::update(float dt, float invdt) {
    if (computer_ || !world_) return;  // section 10: never moves
    // 1. Inputs, only while attached: the rear wheels brake with the truck and steer against it.
    if (attached()) {
        const CarInputs& in = truck_->inputs();
        const float st = truck_->sss() * in.steer;
        const float f2 = st > 0.0f ? 1.0f - st : 1.0f;
        const float f3 = st < 0.0f ? st + 1.0f : 1.0f;
        wheelSetInputs(wheels_[2], -st, in.brake, f2 * in.handbrake);
        wheelSetInputs(wheels_[3], -st, in.brake, f3 * in.handbrake);
    }
    // 2. Gravity.
    body_.F.y = body_.m * kG + body_.F.y;
    // 3. Room (before the integration).
    if (world_) room_ = world_->findRoomAt(body_.p, room_);
    // 4. Integration (5 rad/s limit).
    body_.integrate(dt);
    // 5. Trains 0-3; the wheels hang from the body frame.
    wheelFrame_ = frame();
    for (Train& tr : trains_) trainUpdate(tr, dt, invdt);
    // 6. The joint.
    jointUpdate(dt, invdt);
}

// vehicle-powertrain.md "Drive trains" for a free train of one wheel (no engine): differential 1,
// the spin is the train speed; the compliance uses the truck's Mass (2.5).
void Trailer::trainUpdate(Train& tr, float dt, float invdt) {
    WheelHost host;
    host.body = &body_;
    host.frame = &wheelFrame_;
    host.weatherScale = truck_->weatherScale();
    host.frictionHandling = false;  // 2.5: the material friction is only scaled by the weather
    World& world = *world_;
    Wheel& W = wheels_[tr.wheels[0]];
    const float Itr = tr.tune.angInertia;
    // 1. Brakes.
    const float bc = tr.wt == 0.0f ? tr.tune.brakeStaticCoef : tr.tune.brakeDynamicCoef;
    float brake = 50.0f;
    brake = W.brakeTorque * bc + brake;
    // 2. Drive: none.
    float te = 0.0f, tau = 0.0f;
    tau = tau + te;
    tau = tau - W.roadTorque;
    // 3. Net torque.
    bool stop = false;
    float net;
    if (tr.wt == 0.0f) {
        if (tau < 0.0f) {
            net = tau + brake;
            if (net > 0.0f) net = 0.0f;
        } else {
            net = tau - brake;
            if (net < 0.0f) net = 0.0f;
        }
    } else {
        stop = std::fabs(tau) <= std::fabs(fsign(tr.wt) * brake);
        net = tau + fsign(tr.wt) * brake;
    }
    // 4. Compliance.
    const float Kc = trainKc_;
    // 5. Contact.
    wheelContactCall(W, host, dt, invdt, world);
    // 6. Differential: one wheel.
    tr.diff = 1.0f;
    // 7. Integrate.
    float wn = ((-net) / (dt * Itr + Kc)) * dt + tr.wt;
    if (stop && ((wn < 0.0f && 0.0f < tr.wt) || (tr.wt < 0.0f && 0.0f < wn))) wn = 0.0f;
    // 8-9.
    tr.wt = wn;
    W.omega = tr.wt;
    // 10. Tyre.
    wheelTyreCall(W, host, dt, invdt);
}

// ---------------------------------------------------------------------------------------------
// The joint update (section 5)

void Trailer::jointUpdate(float dt, float invdt) {
    // 0. Detached, or the Ctrl+B key.
    if (status_ & 1) return;
    if (detachKey_) {
        status_ |= 1;
        return;
    }
    RigidBody& A = truck_->body();
    RigidBody& B = body_;
    const TrailerJointTune& J = tune_.joint;
    const vec3 p = p_;
    // 1. Effective mass of the joint point.
    const Mat3 KA = kAt(A, p), KB = kAt(B, p);
    const Mat3 M = inverseMat3(added(KA, KB));
    // 2.
    const vec3 rA = vsub(p, A.p), rB = vsub(p, B.p);
    const Mat3 SA = skew(rA), SB = skew(rB);
    // 3. World inertias and their inverses.
    const Mat3 IwA = worldInertia(A), IiA = inverseMat3(IwA);
    const Mat3 IwB = worldInertia(B), IiB = inverseMat3(IwB);
    // 4. Lean and roll torques (5.4).
    float lean = 0.0f, leanRate = 0.0f, roll = 0.0f, rollRate = 0.0f;
    vec3 axis(1.0f, 0.0f, 0.0f);
    vec3 bz;
    if (status_ & 2) {
        Mat3 E;
        E.m[0][0] = E.m[1][1] = E.m[2][2] = 1.0f;
        const Mat3 FA = mulJ(E, toMat3(A.R)), FB = mulJ(E, toMat3(B.R));
        const vec3 X(FA.m[0][0], FA.m[0][1], FA.m[0][2]);
        const vec3 Y(FA.m[1][0], FA.m[1][1], FA.m[1][2]);
        const vec3 Z(FA.m[2][0], FA.m[2][1], FA.m[2][2]);
        bz = vec3(FB.m[2][0], FB.m[2][1], FB.m[2][2]);
        const float zx = (X.z * bz.z + X.y * bz.y) + bz.x * X.x;
        const float zy = (Y.z * bz.z + Y.x * bz.x) + Y.y * bz.y;
        const float zz = (Z.z * bz.z + Z.x * bz.x) + Z.y * bz.y;
        vec3 tau;
        vec3 l(1.0f, 0.0f, 0.0f);  // MM2 leaves it unset without the lean block (never reached)
        if (std::fabs(zz) < J.cosFreeLean) {
            if (!(zz < 1.0f)) lean = 0.0f;
            else if (!(zz > -1.0f)) lean = 3.1415927f;
            else lean = acosF(zz);
            if (!(double(lean) > 1e-05)) {
                l = vec3(1.0f, 0.0f, 0.0f);
            } else {
                const float k = invlen(zy, -zx, 0.0f);
                l = vec3(k * zy, (-zx) * k, 0.0f * k);
            }
            axis = vec3((l.z * Z.x + l.y * Y.x) + l.x * X.x, (l.z * Z.y + l.y * Y.y) + l.x * X.y,
                        (l.z * Z.z + l.y * Y.z) + l.x * X.z);
            const vec3 dw = vsub(B.w, A.w);
            leanRate = -((dw.x * axis.x + dw.y * axis.y) + dw.z * axis.z);
            rollRate = -((dw.y * bz.y + dw.z * bz.z) + dw.x * bz.x);
            const vec3 pr(rollRate * bz.x, rollRate * bz.y, rollRate * bz.z);
            const vec3 dp(pr.x + dw.x, dw.y + pr.y, dw.z + pr.z);
            const float lA = fsqrt((offA_.x * offA_.x + offA_.y * offA_.y) + offA_.z * offA_.z);
            const float lB = fsqrt((offB_.x * offB_.x + offB_.y * offB_.y) + offB_.z * offB_.z);
            const float a = lA <= 0.0f ? 1.0f : lA;
            const float b = lB <= 0.0f ? 1.0f : lB;
            const float k = (b * a) / ((A.invM * lB) + (B.invM * lA));
            vec3 d;
            if (!(double((dp.x * dp.x + dp.z * dp.z) + dp.y * dp.y) > 1e-12)) {
                d = vec3();
            } else {
                const float k2 = invlen(dp.x, dp.y, dp.z);
                d = vec3(k2 * dp.x, dp.y * k2, dp.z * k2);
            }
            const float cs = (k * 10.0f) * J.dampConstLean;
            const vec3 coulomb(d.x * cs, d.y * cs, d.z * cs);
            const float ss = (J.restoreForceLean * lean) * (12.732395f * k);
            const vec3 spring(axis.x * ss, axis.y * ss, axis.z * ss);
            tau = vsub(spring, coulomb);
        }
        // Roll: reached only with a negative FreeRoll (the roll angle is still 0 when tested).
        if (J.freeRoll < std::fabs(roll) &&
            (J.restoreForceRoll != 0.0f || J.dampConstRoll != 0.0f || J.dampLinearRoll != 0.0f)) {
            float q[3];
            for (int c = 0; c < 3; ++c) q[c] = (FB.m[c][2] * axis.z + FB.m[c][1] * axis.y) + FB.m[c][0] * axis.x;
            const vec3 cc(l.z * zy - l.y * zz, l.x * zz - l.z * zx, l.y * zx - l.x * zy);
            const double rollD = std::atan2(double((q[0] * cc.x + cc.z * q[2]) + cc.y * q[1]),
                                            double((q[0] * l.x + q[2] * l.z) + q[1] * l.y));
            roll = float(rollD);
            const float iz = 1.0f / (B.invI.z + A.invI.z);
            const float sg = double(rollRate) > 1e-05 ? 1.0f : (double(rollRate) < -1e-05 ? -1.0f : 0.0f);
            const float s = (((J.dampLinearRoll * rollRate) * (iz * 7.1364965f)) + ((sg * J.dampConstRoll) * (iz * 10.0f))) +
                            (float(rollD * double(J.restoreForceRoll)) * (iz * 12.732395f));
            tau = vec3(s * bz.x + tau.x, tau.y + s * bz.y, tau.z + s * bz.z);
        }
        A.T = vec3((-tau.x) + A.T.x, (-tau.y) + A.T.y, (-tau.z) + A.T.z);
        B.T = vec3(tau.x + B.T.x, tau.y + B.T.y, tau.z + B.T.z);
    }
    lean_ = lean;
    // 5. Force and torque sums.
    const vec3 fA(invdt * A.G.x + A.F.x, invdt * A.G.y + A.F.y, invdt * A.G.z + A.F.z);
    const vec3 tA(invdt * A.H.x + A.T.x, invdt * A.H.y + A.T.y, invdt * A.H.z + A.T.z);
    const vec3 fB(invdt * B.G.x + B.F.x, invdt * B.G.y + B.F.y, invdt * B.G.z + B.F.z);
    const vec3 tB(invdt * B.H.x + B.T.x, invdt * B.H.y + B.T.y, invdt * B.H.z + B.T.z);
    // 6. Acceleration terms at the joint point.
    vec3 cA, lnA, sA, cB, lnB, sB;
    accelTerms(B, rB, IwB, IiB, SB, fB, tB, false, cB, lnB, sB);
    accelTerms(A, rA, IwA, IiA, SA, fA, tA, true, cA, lnA, sA);
    // 7. Relative acceleration (B minus A).
    const vec3 a(((((cB.x + lnB.x) + sB.x) - lnA.x) - cA.x) - sA.x, ((((lnB.y + cB.y) + sB.y) - lnA.y) - cA.y) - sA.y,
                 ((((lnB.z + cB.z) + sB.z) - lnA.z) - cA.z) - sA.z);
    // 8. Relative velocity.
    const vec3 vA = pointVelocity(A, p), vB = pointVelocity(B, p);
    const float k3 = invdt * 0.33333334f;
    const vec3 dv(k3 * (vB.x - vA.x), (vB.y - vA.y) * k3, (vB.z - vA.z) * k3);
    const vec3 h(dv.x + a.x, a.y + dv.y, a.z + dv.z);
    // 9. The joint force on the truck (the trailer gets −f).
    vec3 f((h.y * M.m[1][0] + h.z * M.m[2][0]) + M.m[0][0] * h.x, (h.y * M.m[1][1] + h.z * M.m[2][1]) + M.m[0][1] * h.x,
           (h.z * M.m[2][2] + M.m[0][2] * h.x) + h.y * M.m[1][2]);
    // 10. Limits (5.5).
    if (status_ & 2) {
        float eL = 0.0f;
        bool active = false;
        if (!(lean < J.leanLimit)) {
            const float e = ((lean - J.leanLimit) * 3.0f) + leanRate;
            if (e > 0.0f) {
                eL = e;
                active = true;
            }
        }
        float eR = 0.0f;
        bool call = false;
        const float rollMax = 0.3f, rollMin = -0.3f;
        if (roll > rollMax) {
            const float e = ((roll - rollMax) * 3.0f) + rollRate;
            if (e > 0.0f) {
                eR = e;
                call = true;
            }
        } else if (roll < rollMin) {
            const float e = ((roll - rollMin) * 3.0f) + rollRate;
            if (e < 0.0f) {
                eR = e;
                call = true;
            }
        }
        if (call || active) {
            const Mat3 W = added(mulJ(IiA, SA), mulJ(IiB, SB));
            Mat3 Kq = mulJ(mulJ(W, M), W);
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) Kq.m[i][j] = (Kq.m[i][j] + IiA.m[i][j]) + IiB.m[i][j];
            const vec3 jf(dt * f.x, dt * f.y, dt * f.z);
            const vec3 hA(((invdt * A.H.x) + A.T.x) * dt, ((invdt * A.H.y) + A.T.y) * dt, ((invdt * A.H.z) + A.T.z) * dt);
            const vec3 hB(((invdt * B.H.x) + B.T.x) * dt, ((invdt * B.H.y) + B.T.y) * dt, ((invdt * B.H.z) + B.T.z) * dt);
            vec3 LA, LB;
            {
                const vec3& r = rA;
                const vec3 c(jf.z * r.y - jf.y * r.z, r.z * jf.x - jf.z * r.x, jf.y * r.x - r.y * jf.x);
                LA = vec3(c.x + hA.x, hA.y + c.y, hA.z + c.z);
            }
            {
                const vec3& r = rB;
                const vec3 c(r.y * jf.z - r.z * jf.y, r.z * jf.x - jf.z * r.x, jf.y * r.x - r.y * jf.x);
                LB = vsub(hB, c);
            }
            auto delta = [](const vec3& Lv, const Mat3& Ii) {
                return vec3((Lv.x * Ii.m[0][0] + Lv.z * Ii.m[2][0]) + Lv.y * Ii.m[1][0],
                            (Lv.z * Ii.m[2][1] + Lv.y * Ii.m[1][1]) + Lv.x * Ii.m[0][1],
                            (Lv.z * Ii.m[2][2] + Lv.y * Ii.m[1][2]) + Lv.x * Ii.m[0][2]);
            };
            const vec3 D = vsub(delta(LA, IiA), delta(LB, IiB));
            vec3 imp;
            if (eL != 0.0f) {
                const float dd = (D.z * axis.z + D.y * axis.y) + D.x * axis.x;
                if (dd > 0.0f) eL = dd + eL;
                float q[3];
                for (int c = 0; c < 3; ++c) q[c] = (Kq.m[2][c] * axis.z + Kq.m[1][c] * axis.y) + Kq.m[0][c] * axis.x;
                const float den = (q[2] * axis.z + q[1] * axis.y) + q[0] * axis.x;
                const float x = den != 0.0f ? eL / den : 0.0f;
                const float sc = (J.limitElasticityLean + 1.0f) * x;
                imp = vec3(axis.x * sc, axis.y * sc, axis.z * sc);
            }
            if (eR != 0.0f) {
                const float dr = (bz.z * D.z + bz.y * D.y) + bz.x * D.x;
                if ((eR > 0.0f && dr > 0.0f) || (!(eR > 0.0f) && dr < 0.0f)) eR = dr + eR;
                const vec3 q((Kq.m[1][0] * bz.y + Kq.m[2][0] * bz.z) + bz.x * Kq.m[0][0],
                             (Kq.m[0][1] * bz.x + Kq.m[1][1] * bz.y) + Kq.m[2][1] * bz.z,
                             (Kq.m[0][2] * bz.x + Kq.m[1][2] * bz.y) + Kq.m[2][2] * bz.z);
                const float den = (q.z * bz.z + q.y * bz.y) + q.x * bz.x;
                const float x = den != 0.0f ? eR / den : 0.0f;
                const float sc = (J.limitElasticityRoll + 1.0f) * x;
                imp = vec3(imp.x + bz.x * sc, imp.y + bz.y * sc, imp.z + bz.z * sc);
            }
            A.H = vec3((-imp.x) + A.H.x, (-imp.y) + A.H.y, (-imp.z) + A.H.z);
            B.H = vec3(imp.x + B.H.x, imp.y + B.H.y, imp.z + B.H.z);
            vec3 q((W.m[1][0] * imp.y + W.m[2][0] * imp.z) + imp.x * W.m[0][0],
                   (W.m[0][1] * imp.x + W.m[1][1] * imp.y) + W.m[2][1] * imp.z,
                   (W.m[0][2] * imp.x + W.m[1][2] * imp.y) + W.m[2][2] * imp.z);
            q = vec3(invdt * q.x, invdt * q.y, invdt * q.z);
            const vec3 e((M.m[1][0] * q.y + M.m[2][0] * q.z) + q.x * M.m[0][0],
                         (M.m[0][1] * q.x + M.m[1][1] * q.y) + M.m[2][1] * q.z,
                         (M.m[0][2] * q.x + M.m[1][2] * q.y) + M.m[2][2] * q.z);
            f = vec3(e.x + f.x, e.y + f.y, e.z + f.z);
        }
    }
    // 11. Break test (the rest of this update still runs).
    if (J.forceLimit > 0.0f) {
        const float L = J.forceLimit * 10000.0f;
        if (L * L < (f.x * f.x + f.z * f.z) + f.y * f.y) status_ |= 1;
    }
    // 12. Torques of the force.
    {
        const vec3& r = rA;
        A.T = vec3((f.z * r.y - r.z * f.y) + A.T.x, (r.z * f.x - r.x * f.z) + A.T.y, (r.x * f.y - f.x * r.y) + A.T.z);
    }
    {
        const vec3 r = vsub(B.p, p);
        B.T = vec3((f.z * r.y - r.z * f.y) + B.T.x, (r.z * f.x - r.x * f.z) + B.T.y, (r.x * f.y - f.x * r.y) + B.T.z);
    }
    // 13. Turn correction (5.7), with MM2's uncleared matrix: K_B(p)·Rot.
    vec3 fc = f;
    {
        const float hdt = dt * 0.5f;
        const vec3 th((A.w.x + B.w.x) * hdt, (A.w.y + B.w.y) * hdt, (A.w.z + B.w.z) * hdt);
        const float t2 = (th.x * th.x + th.z * th.z) + th.y * th.y;
        if (double(t2) > 1e-05) {
            const float sq = fsqrt(t2);
            const float inv = 1.0f / sq;
            const vec3 ax(th.x * inv, th.y * inv, th.z * inv);
            const float half = sq * 0.5f;
            const Mat3 Mr = mulProducts(KB, rotAboutAxis(ax, half));
            const float dd = (ax.x * f.x + ax.z * f.z) + ax.y * f.y;
            const vec3 fp(ax.x * dd, ax.y * dd, dd * ax.z);
            const vec3 fo = vsub(f, fp);
            float q[3];
            for (int c = 0; c < 3; ++c) q[c] = (Mr.m[0][c] * fo.x + fo.z * Mr.m[2][c]) + fo.y * Mr.m[1][c];
            const vec3 gg(q[0] + fp.x, q[1] + fp.y, fp.z + q[2]);
            const float s = t2 < 1.0f ? 1.0f - t2 * 0.041666668f
                                      : 2.0f * float(std::sin(double(half)) * double(inv));
            fc = vec3(s * gg.x, gg.y * s, gg.z * s);
        }
    }
    // 14. Forces.
    A.F = vec3(fc.x + A.F.x, fc.y + A.F.y, fc.z + A.F.z);
    B.F = vec3((-fc.x) + B.F.x, (-fc.y) + B.F.y, (-fc.z) + B.F.z);
    // 15. The joint point and the free range: both bodies move at once.
    {
        const vec3 an = anchorA(A, offA_), bn = anchorB(B, offB_);
        p_ = vec3((bn.x + an.x) * 0.5f, (bn.y + an.y) * 0.5f, (bn.z + an.z) * 0.5f);
        const vec3 d = vsub(bn, an);
        const float L = (d.x * d.x + d.y * d.y) + d.z * d.z;
        if (L > J.freeRange * J.freeRange) {
            const float len = fsqrt(L);
            const vec3 n(d.x / len, d.y / len, d.z / len);
            const float s = (len - J.freeRange) * 0.5f;
            A.p = vec3((s * n.x) + A.p.x, (s * n.y) + A.p.y, (s * n.z) + A.p.z);
            B.p = vec3(B.p.x - s * n.x, B.p.y - s * n.y, B.p.z - s * n.z);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// 6.2: the inverse mass of a jointed body

void Trailer::collisionInverseMass(const RigidBody& S, const vec3& P, float K[3][3]) const {
    S.inverseMassMatrix(P, K);
    if (!attached() || !truck_) return;
    const RigidBody& A = truck_->body();
    const Mat3 Mj = inverseMat3(added(kAt(A, p_), kAt(body_, p_)));
    const vec3 rj = vsub(p_, S.p), rP = vsub(P, S.p);
    const Mat3 Sj = skew(rj), SP = skew(rP);
    const Mat3 R = toMat3(S.R);
    Mat3 C1 = mulJ(R, SP);
    const float iI[3] = {S.invI.x, S.invI.y, S.invI.z};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) C1.m[i][j] = C1.m[i][j] * iI[i];
    Mat3 C = mulJ(transposed(Sj), mulJ(transposed(R), C1));
    for (int i = 0; i < 3; ++i) C.m[i][i] = S.invM + C.m[i][i];
    const Mat3 E = mulJ(transposed(C), mulJ(Mj, C));
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) K[i][j] = K[i][j] - E.m[i][j];
}

}  // namespace phys
}  // namespace cc
