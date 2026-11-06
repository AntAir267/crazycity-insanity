#include "physics/car.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "core/game_random.h"
#include "core/log.h"
#include "core/strutil.h"
#include "physics/trailer.h"

namespace cc {
namespace phys {

namespace {
constexpr float kGravity = -19.6f;
constexpr float kRpmToRad = 0.10471976f;
constexpr float kRadToRpm = 9.549296f;
constexpr float kMph = 2.236025f;
constexpr float kPhi1 = 1.618034f;
constexpr float kPhi2 = 0.618034f;

inline float clampf(float x, float lo, float hi) {
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

float q255(float x) { return float(int(x * 255.0f) & 255) * 0.003921569f; }
float q127(float x) { return float(int8_t(int(x * 127.0f))) * 0.007874016f; }

SurfaceMaterial defaultSurface() {
    SurfaceMaterial m;
    m.friction = 1.0f;
    m.drag = 0.0f;
    m.height = 0.0f;
    m.width = 1.0f;
    m.depth = 0.0f;
    return m;
}

// The bump random source (vehicle-wheels.md "Bumps": MM2's is not reproducible). One shared
// generator like the C library's rand() of the game's compiler (x = x·214013 + 2531011, value =
// bits 16..30), so check runs that cross bumpy ground are repeatable (resetBumpRandom per run).
float bumpRandom() { return float(gameRandom15()) / 32768.0f; }
}  // namespace

void resetBumpRandom(uint32_t seed) { resetGameRandom(seed); }

float Wheel::visualDisp() const {
    float x = ((radius * 0.05f) * Fsusp) / L0;
    if (x < 0.0f) return 0.0f;
    float lim = radius * 0.3f;
    return x <= lim ? x : lim;
}

// ---------------------------------------------------------------------------------------------
// Set-up (vehicle-tune.md, "Order of set-up")

void Car::computeWheelConstants(Wheel& w) {
    const CarSimTune& s = tune_.sim;
    const float cz = w.pivot.z;
    w.L0 = (-(kGravity * s.mass) * (std::fabs(cz - s.centerOfGravity.z) / (std::fabs(cz) + std::fabs(cz)))) * 0.5f;
    wheelLoadConstants(w);
}

void Car::addWheelLoad(int i, float x) {
    if (i < 0 || i > 3) return;
    Wheel& w = wheels_[i];
    float L = x + w.L0;
    if (L < 1.0f) L = 1.0f;
    w.L0 = L;
    wheelLoadConstants(w);
}

void wheelLoadConstants(Wheel& w) {
    WheelTune& t = w.tune;
    if (t.suspensionFactor < 0.75f) t.suspensionFactor = 0.75f;  // stored back into the tune value
    const float e = t.suspensionExtent, l = t.suspensionLimit, f = t.suspensionFactor;
    w.kk = 1.0f / ((l + e) * e);
    w.k = (f * e + l) * w.kk * w.L0;
    w.prog = ((f - 1.0f) * w.kk * w.L0) / w.k;
    {
        const float tt = fsqrt(w.k * w.L0) * t.suspensionDampCoef;
        w.damp = tt + tt;
    }
    w.mL = w.L0 / kGravity;
    w.Klong = (w.L0 + w.L0) / t.tireDispLimitLong;
    {
        const float tt = fsqrt(w.Klong * (-w.mL)) * t.tireDampCoefLong;
        w.Dlong = tt + tt;
    }
    w.Klat = (w.L0 + w.L0) / t.tireDispLimitLat;
    {
        const float tt = fsqrt(w.Klat * (-w.mL)) * t.tireDampCoefLat;
        w.Dlat = tt + tt;
    }
    const float tb = t.staticFric * w.radius * w.L0;
    w.brakeMax = tb * t.brakeCoef;
    w.handbrakeMax = tb * t.handbrakeCoef;
    w.invOpt2 = 1.0f / (t.optimumSlipPercent * t.optimumSlipPercent);
}

float Car::Tfull(float w) const {
    const Engine& E = eng_;
    if (w <= E.wOpt) return (kPhi1 * E.wOpt - w) * (kPhi2 * E.wOpt + w) * E.K;
    if (w <= E.wMax) {
        // ((((D·A)·B)·C)·K)·invGap (vehicle-powertrain.md): the association matters.
        const float D = (w + E.wMax) - (E.wOpt + E.wOpt);
        const float A = kPhi2 * E.wOpt + w;
        const float B = kPhi1 * E.wOpt - w;
        const float C = E.wMax - w;
        return ((((D * A) * B) * C) * E.K) * E.invGap;
    }
    return 0.0f;
}

float Car::Tzero(float we) const {
    const CarSimTune& s = tune_.sim;
    const Engine& E = eng_;
    return ((E.wIdle - we) * ((1.0f * s.maxHorsePower * 746.0f) / E.wOpt) * 0.75f) / (E.wOpt - E.wIdle);
}

void Car::computeGearbox() {
    const CarSimTune& s = tune_.sim;
    const int drive0 = s.drivetrainType == 0 ? 2 : 0;
    const float radius0 = wheels_[drive0].radius;
    // C = 26.822401 (float 0x41D69447: MM2's single-precision 1609.344 × 0.016666668).
    const float kMphToMpm = 0x1.ad288ep+4f;
    auto ratioFromMph = [&](float v) { return s.optRPM / ((kMphToMpm * v) / (radius0 * 6.2831855f)); };
    auto build = [&](float* ratio, int N) {
        for (int i = 0; i < 8; ++i) ratio[i] = 0.0f;
        ratio[0] = -ratioFromMph(s.reverse);
        ratio[1] = 0.0f;
        ratio[2] = ratioFromMph(s.low);
        ratio[N - 1] = ratioFromMph(s.high);
        if (N - 2 > 2) {
            const int n = N - 3;
            const float inv = 1.0f / float(n);
            const float step = float(std::pow(double(ratio[N - 1] / ratio[2]), double(inv)));
            for (int i = 1; i <= n - 1; ++i) {
                const float e = float(i) - (float((i - (N - 2) + 1) * i) * s.gearBias) / float(n);
                ratio[2 + i] = float(std::pow(double(step), double(e)) * double(ratio[2]));
            }
        }
    };
    build(autoRatio_, s.autoNumGears);
    build(manualRatio_, s.manualNumGears);

    for (int i = 0; i < 8; ++i) up_[i] = downMin_[i] = downMax_[i] = 0.0f;
    const int A = s.autoNumGears;
    for (int g = 2; g <= A - 2; ++g) {
        const float q = autoRatio_[g + 1] / autoRatio_[g];
        float hi = s.maxRPM;
        if (q * s.optRPM < hi) hi = s.optRPM / q;
        float lo = s.optRPM;
        int guard = 0;
        while (1.0f < hi - lo && guard++ < 200) {
            const float mid = (hi + lo) * 0.5f;
            const float a1 = mid * kRpmToRad;
            const float P1 = Tfull(a1) * a1;
            const float a2 = mid * kRpmToRad * q;
            const float P2 = Tfull(a2) * a2;
            if (P1 < P2) hi = mid;
            else lo = mid;
        }
        const float shift = (hi + lo) * 0.5f;
        up_[g] = (s.upshiftBias + 1.0f) * shift;
        downMin_[g + 1] = (1.0f - s.downshiftBiasMin) * shift * q;
        downMax_[g + 1] = (1.0f - s.downshiftBiasMax) * shift * q;
    }
    up_[A - 1] = s.maxRPM;
    downMin_[2] = downMax_[2] = 0.0f;
}

Car::Car() = default;

Car::~Car() {
    if (trailer_) trailer_->unlink();
}

void Car::attachTrailer(std::unique_ptr<Trailer> t) {
    if (trailer_) trailer_->unlink();
    trailer_ = std::move(t);
    if (trailer_) trailer_->resetBehind();
}

// trailer.md 3: every reset of the car puts its trailer back behind it, attached.
void Car::afterReset() {
    if (trailer_) trailer_->resetBehind();
}

bool Car::setup(const CarTune& tune) {
    if (trailer_) trailer_->unlink();
    trailer_.reset();
    tune_ = tune;
    thrown_.assign(tune_.looseParts.size(), false);
    wreckThrown_ = false;
    const CarSimTune& s = tune_.sim;
    for (int i = 0; i < 4; ++i) {
        wheels_[i] = Wheel();
        wheels_[i].index = i;
        wheels_[i].pivot = tune_.wheels[i].pivot;
        wheels_[i].radius = tune_.wheels[i].radius;
        wheels_[i].width = tune_.wheels[i].width;
    }
    // WheelFront -> wheel 0, WheelBack -> wheel 2; the right wheels copy every key except
    // HandbrakeCoef and WobbleLimit (which keep their defaults 1 and 0).
    wheels_[0].tune = s.wheelFront;
    wheels_[2].tune = s.wheelBack;
    if (wheels_[0].tune.suspensionFactor < 0.75f) wheels_[0].tune.suspensionFactor = 0.75f;
    if (wheels_[2].tune.suspensionFactor < 0.75f) wheels_[2].tune.suspensionFactor = 0.75f;
    for (int src : {0, 2}) {
        WheelTune t = wheels_[src].tune;
        t.handbrakeCoef = WheelTune().handbrakeCoef;
        t.wobbleLimit = WheelTune().wobbleLimit;
        wheels_[src + 1].tune = t;
    }

    // Body.
    const float kI = 0.083333336f;
    const vec3 b = s.inertiaBox;
    const vec3 inertia(((b.z * b.z + b.y * b.y) * s.mass) * kI, ((b.z * b.z + b.x * b.x) * s.mass) * kI,
                       ((b.y * b.y + b.x * b.x) * s.mass) * kI);
    body_.setMass(s.mass, inertia);
    body_.setAngularLimit(12.566371f);
    extraMass_ = 0.0f;

    // Trains.
    trains_.clear();
    Train left, right, drive;
    left.tune = right.tune = s.freetrain;
    drive.tune = s.drivetrain;
    drive.drive = true;
    switch (s.drivetrainType) {
        case 1:
            left.wheels = {2};
            right.wheels = {3};
            drive.wheels = {0, 1};
            trains_ = {left, right, drive};
            break;
        case 2:
            drive.wheels = {0, 1, 2, 3};
            trains_ = {drive};
            break;
        default:
            left.wheels = {0};
            right.wheels = {1};
            drive.wheels = {2, 3};
            trains_ = {left, right, drive};
            break;
    }

    // Engine constants.
    Engine& E = eng_;
    E.wMax = s.maxRPM * kRpmToRad;
    E.wOpt = s.optRPM * kRpmToRad;
    E.wIdle = s.idleRPM * kRpmToRad;
    E.gap = s.maxRPM * kRpmToRad - E.wOpt;
    E.invGap = 1.0f / (E.gap * E.gap);
    E.K = (1.0f * s.maxHorsePower * 746.0f) / (E.wOpt * E.wOpt * E.wOpt);

    computeGearbox();

    // Anti-roll bars.
    const AxleTune* ax[2] = {&s.axleFront, &s.axleBack};
    for (int i = 0; i < 2; ++i) {
        arbK_[i] = ax[i]->torqueCoef * body_.I.z;
        const float t = fsqrt(arbK_[i] * body_.I.z) * ax[i]->dampCoef;
        arbD_[i] = t + t;
    }

    for (int i : {1, 3, 0, 2}) computeWheelConstants(wheels_[i]);

    // The car's bound material: the built-in default with the car's friction and elasticity.
    boundMaterial_ = Material();
    boundMaterial_.friction = s.boundFriction;
    boundMaterial_.elasticity = s.boundElasticity;
    // Water sample points in the rigid-body frame (collision.md, "Water effect").
    {
        const vec3 c = tune_.waterCoG, S = tune_.waterSize;
        const vec3 q(c.x - S.x * 0.5f, c.y - S.y * 0.5f, c.z - S.z * 0.5f);
        const vec3 Q(S.x * 0.5f + c.x, S.y * 0.5f + c.y, S.z * 0.5f + c.z);
        int n = 0;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k)
                    waterPoints_[n++] = vec3(((Q.x - q.x) * float(i)) * 0.33333334f + q.x,
                                             ((Q.y - q.y) * float(j)) * 0.33333334f + q.y,
                                             ((Q.z - q.z) * float(k)) * 0.33333334f + q.z);
    }
    resetFlat(0.0f, 0.0f, 0.0f, 0.0f);
    return true;
}

Mtx Car::bodyMatrix() const { return frame_; }

void Car::notePush(const vec3& push, int identity) {
    const float p2 = pdot(push, push);
    if (p2 > col_.largestPush2) {
        col_.largestPush2 = p2;
        col_.largestSource = identity;
    }
}

void Car::waterForces(float dt) {
    if (!col_.water || !drivable_) return;
    const float L = col_.waterLevel;
    const float b = col_.buoyancy, k = 0.08f, m = body_.m;
    const float mb = m * b;  // formed once per sample
    const vec3& x = body_.p;
    const float xl2 = (x.z * x.z + x.y * x.y) + x.x * x.x;
    const vec3 d = xl2 == 0.0f ? vec3(0, 0, 0) : vscale(x, 1.0f / fsqrt(xl2));
    const vec3& X = body_.R.r[0];
    const vec3& Y = body_.R.r[1];
    const vec3& Z = body_.R.r[2];
    for (const vec3& p : waterPoints_) {
        const vec3 w(((p.z * Z.x + p.y * Y.x) + p.x * X.x) + x.x, ((p.z * Z.y + p.x * X.y) + p.y * Y.y) + x.y,
                     ((p.z * Z.z + p.x * X.z) + p.y * Y.z) + x.z);
        if (w.y > L) continue;  // w.y <= L, NaN too
        const vec3 r = vsub(w, x);
        const vec3 u = vadd(body_.v, pcross(body_.w, r));
        const vec3 F(-(((u.x + d.x) * k) * m), mb - ((u.y + d.y) * k) * m, -(((u.z + d.z) * k) * m));
        body_.addForce(F);
        body_.addTorque(pcross(r, F));
    }
    float nb = col_.buoyancy;
    if (nb > 0.4f) {
        nb = nb - 0.03f * dt;
        if (nb < 0.4f) nb = 0.4f;
        else if (nb > 0.7f) nb = 0.7f;
    }
    col_.buoyancy = nb;
}

bool Car::worldLimitReset(float frameSeconds) {
    if (body_.p.y < -50.0f) return true;
    if (col_.water) {
        col_.waterTimer += frameSeconds;
        if (col_.waterTimer > 5.0f) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Placement and reset

vec3 Car::cogOffset(const M3& R) const {
    const vec3& c = tune_.sim.centerOfGravity;
    const vec3& X = R.r[0];
    const vec3& Y = R.r[1];
    const vec3& Z = R.r[2];
    return vec3((Y.x * c.y + Z.x * c.z) + c.x * X.x, (Y.y * c.y + X.y * c.x) + Z.y * c.z,
                (Y.z * c.y + X.z * c.x) + Z.z * c.z);
}

vec3 Car::modelOrigin() const { return vadd(body_.p, cogOffset(body_.R)); }

void resetWheelState(Wheel& w) {
    Wheel fresh = w;
    fresh.comp = fresh.rate = fresh.Fsusp = fresh.Fz = fresh.Ks = 0.0f;
    fresh.bottomed = false;
    fresh.steerAngle = fresh.brakeTorque = 0.0f;
    fresh.grounded = false;
    fresh.x = fresh.N = fresh.Lat = fresh.Back = vec3();
    fresh.vLat = fresh.vFwd = fresh.vN = 0.0f;
    fresh.matFric = 1.0f;
    fresh.matDrag = fresh.depth = fresh.bump = fresh.phi = 0.0f;
    fresh.dL = fresh.dT = fresh.FL = fresh.FT = fresh.roadTorque = 0.0f;
    fresh.latSlip = fresh.longSlip = fresh.slide = 0.0f;
    fresh.slipping = false;
    fresh.omega = fresh.rotAngle = 0.0f;
    fresh.probe = ProbeState();
    fresh.material = defaultSurface();
    w = fresh;
}

void Car::resetMoving() {
    ++resetCount_;
    const M3 R = body_.R;
    const vec3 p = body_.p;
    body_.reset();
    body_.R = R;
    body_.p = p;
    for (Wheel& w : wheels_) resetWheelState(w);
    for (Train& t : trains_) {
        t.wt = 0.0f;
        t.diff = 1.0f;
    }
    Engine& E = eng_;
    E.we = 0.0f;
    E.rpm = tune_.sim.idleRPM;
    E.torque = 0.0f;
    E.throttle = 0.0f;
    E.waiting = true;
    E.gclTimer = tune_.sim.gcl;
    E.shiftRpm = tune_.sim.idleRPM;
    E.power = 0.0f;
    E.coupled = false;
    gear_ = 2;
    changed_ = true;
    gearTimer_ = 0.0f;
    in_ = CarInputs();
    reverseMode_ = false;
    speed_ = speedMph_ = 0.0f;
    sss_ = 1.0f;
    stuck_ = Stuck::Inactive;
    stuckTimer_ = 0.0f;

    origin_ = modelOrigin();
    frame_ = mtxFromRows(body_.R.r[0], body_.R.r[1], body_.R.r[2], origin_);
    col_.prevMatrix = frame_;
    col_.room = 0;
    col_.roomPending = true;
    col_.prevLargestSource = col_.largestSource = 0;
    col_.largestPush2 = 0.0f;
    col_.water = false;  // b (buoyancy) keeps its value: 0.7 only when the car is created
    col_.waterTimer = 0.0f;
    for (Wheel& w : wheels_) w.probe = ProbeState();
}

void Car::resetFlat(float x, float groundY, float z, float headingDegrees) {
    clearDamage();
    const double h = double(headingDegrees) * 3.14159265358979323846 / 180.0;
    body_.R = headingRotation(float(std::cos(h)), float(std::sin(h)));
    const float d0 = wheels_[0].radius - wheels_[0].pivot.y;
    const float d1 = wheels_[1].radius - wheels_[1].pivot.y;
    const float d2 = wheels_[2].radius - wheels_[2].pivot.y;
    const float d3 = wheels_[3].radius - wheels_[3].pivot.y;
    const float h0 = (d0 + d1 + d2 + d3) * 0.25f;
    const vec3 origin(x, groundY + h0, z);
    body_.p = vsub(origin, cogOffset(body_.R));
    automatic_ = true;
    resetMoving();
    time_ = 0.0f;
    steps_ = 0;
    afterReset();
}

void Car::placeAt(const vec3& q, float headingRadians) {
    clearDamage();
    body_.R = headingRotation(fcos(headingRadians), fsin(headingRadians));
    body_.p = vadd(q, tune_.sim.centerOfGravity);
    resetMoving();
    afterReset();
}

void Car::placeAtDegrees(const vec3& q, float headingDegrees) {
    clearDamage();
    const double h = double(headingDegrees) * 3.14159265358979323846 / 180.0;
    body_.R = headingRotation(float(std::cos(h)), float(std::sin(h)));
    body_.p = vadd(q, tune_.sim.centerOfGravity);
    resetMoving();
    afterReset();
}

void Car::resetToBodyMatrix(const M3& R, const vec3& bodyPos) {
    clearDamage();
    body_.R = R;
    body_.p = bodyPos;
    resetMoving();
    afterReset();
}

void Car::setExtraMass(float extra) {
    if (extra == extraMass_) return;
    // m := m + gold (or m − gold when it leaves), 1/m recomputed (vehicle-body.md, "State").
    if (extraMass_ != 0.0f) body_.m = body_.m - extraMass_;
    if (extra != 0.0f) body_.m = body_.m + extra;
    body_.invM = body_.m <= 0.0f ? 3.4028235e38f : 1.0f / body_.m;
    extraMass_ = extra;
}

void Car::resetOnWheels() {
    const M3& R = body_.R;
    const float theta = float(std::atan2(double(R.r[2].x), double(R.r[2].z)));
    const vec3 pos = vadd(vec3(body_.p.x, body_.p.y + 0.5f, body_.p.z), tune_.sim.centerOfGravity);
    body_.R = headingRotation(fcos(theta), fsin(theta));
    body_.p = pos;
    resetMoving();
    afterReset();
}

// ---------------------------------------------------------------------------------------------
// Controls

void Car::setGear(int g) {
    if (g < 0 || g > 7) return;
    if (g != gear_ && (!automatic_ || g < tune_.sim.autoNumGears)) {
        gearTimer_ = 0.0f;
        changed_ = true;
        gear_ = g;
    }
}

void Car::setAutomatic(bool a) {
    if (a == automatic_) return;
    // controls.md: toggling clears the pedal swap (first selecting first gear).
    if (reverseMode_) {
        setGear(2);
        reverseMode_ = false;
    }
    automatic_ = a;
    if (automatic_ && gear_ >= tune_.sim.autoNumGears) setGear(tune_.sim.autoNumGears - 1);
}

void Car::toggleReverse() {
    if (gear_ != 0) setGear(0);
    else setGear(2);
    reverseMode_ = false;
}

void Car::controlStep(const DriverControls& c) {
    const float a = q255(c.accelerate);
    const float b = q255(c.brake);
    const float h = q255(c.handbrake);
    const float s = q127(c.steer);
    if (reverseMode_) {
        in_.throttle = b;
        in_.brake = a;
    } else {
        in_.throttle = a;
        in_.brake = b;
    }
    in_.steer = s;
    in_.handbrake = h;
    if (netControls_) {
        // multiplayer.md 5.2 (the slow-down after the finish), then 1.5 (the throttle cap): where
        // the throttle input is applied, before the automatic reverse and the player overrides.
        if (netSlowDown_ > 0.0f) {
            in_.throttle = in_.throttle - netSlowDown_ * 0.2f;
            in_.brake = 1.0f;
        }
        if (gear_ >= 2) in_.throttle = in_.throttle >= 0.0f ? (in_.throttle < netCap_ ? in_.throttle : netCap_) : 0.0f;
        if (netStopped_) {
            in_.steer = 0.0f;
            in_.brake = 1.0f;
        }
    }
    if (automatic_ && autoReverse_) {
        if (gear_ >= 2 && speed_ < 5.0f && in_.brake > 0.8f && double(in_.throttle) < 0.1) {  // 0.1 as a double
            reverseMode_ = true;
            setGear(0);
        } else if (gear_ == 0 && in_.throttle < 0.8f) {
            reverseMode_ = false;
            setGear(2);
        }
    }
    // Player overrides (controls.md), after the automatic-reverse check: a disabled player, then
    // a wrecked car (damage.md: no throttle, brake or steering), then the parking brake.
    if (playerDisabled_) {
        in_.throttle = 0.0f;
        in_.brake = 1.0f;
        in_.steer = -1.0f;
    }
    if (wrecked()) {
        in_.throttle = 0.0f;
        in_.brake = 0.0f;
        in_.steer = 0.0f;
    }
    if (speedMph_ < 4.0f && in_.throttle == 0.0f) in_.handbrake = 1.0f;
    if (!automatic_) {
        if (c.shift > 0 && gear_ < tune_.sim.manualNumGears - 1) setGear(gear_ + 1);
        if (c.shift < 0 && gear_ > 0) setGear(gear_ - 1);
    }
    if (c.setGear != -100) setGear(c.setGear);
}

// ---------------------------------------------------------------------------------------------
// The physics step

int Car::groundedWheels() const {
    int n = 0;
    for (const Wheel& w : wheels_)
        if (w.grounded) ++n;
    return n;
}

float Car::ratio() const {
    const float* t = automatic_ ? autoRatio_ : manualRatio_;
    return (gear_ >= 0 && gear_ < 8) ? t[gear_] : 0.0f;
}

void wheelSetInputs(Wheel& w, float sIn, float bIn, float hIn) {
    const float a = -(sIn * w.tune.steeringLimit);
    w.steerAngle = (1.0f - a * w.tune.steeringOffset * fsign(w.pivot.x)) * a;
    w.brakeTorque = hIn * w.handbrakeMax + bIn * w.brakeMax;
}

void Car::wheelInputs() {
    const CarSimTune& s = tune_.sim;
    if (s.sssThreshold == 0.0f) sss_ = 1.0f;
    else if (speed_ < s.sssThreshold) sss_ = (speed_ / s.sssThreshold) * (s.sssValue - 1.0f) + 1.0f;
    else sss_ = s.sssValue;
    const float steer = in_.steer, brake = in_.brake, hand = in_.handbrake, throttle = in_.throttle;
    const float st = sss_ * steer;
    const float hbp = hand > 0.0f ? hand : 0.0f;
    const float brakeR = (brake > 0.95f && throttle > 0.95f && speed_ < 1.0f) ? 0.0f : brake;
    auto set = wheelSetInputs;
    const float hFront = hand < 0.0f ? -hand : 0.0f;
    set(wheels_[0], st, brake, hFront);
    set(wheels_[1], st, brake, hFront);
    set(wheels_[2], -st, brakeR, steer > 0.0f ? hbp * (1.0f - steer) : hbp);
    set(wheels_[3], -st, brakeR, steer < 0.0f ? hbp * (steer + 1.0f) : hbp);
}

void Car::engineUpdate(float dt) {
    Engine& E = eng_;
    const CarSimTune& s = tune_.sim;
    E.throttle = in_.throttle;
    if (changed_ && !E.waiting) {
        E.shiftRpm = E.rpm;
        E.gclTimer = s.gcl;
        E.waiting = true;
    }
    E.torque = Tfull(E.we) * E.throttle + (1.0f - E.throttle) * Tzero(E.we);
    const float r = ratio();
    if (r == 0.0f || E.we < E.wIdle) E.coupled = false;
    else if (E.we > E.wIdle + E.wIdle && !E.coupled) E.coupled = true;
    if (!E.coupled) E.we = clampf((E.torque / s.engAngInertia) * dt + E.we, 0.0f, E.wMax);
    E.rpm = E.we * kRadToRpm;
    if (E.waiting) {
        if (E.gclTimer <= 0.0f) {
            E.waiting = false;
            changed_ = false;
            E.we = E.rpm * kRpmToRad;
        } else {
            E.torque = 0.0f;
            E.rpm = (E.shiftRpm * E.gclTimer + (s.gcl - E.gclTimer) * E.rpm) / s.gcl;
            E.gclTimer = E.gclTimer - dt;
        }
    }
    E.power = E.torque * E.we * 0.0013404826f;
    if (gear_ == 1) {
        const float q = (E.torque / Tfull(E.wOpt)) * s.engAngInertia;
        vec3 local = s.drivetrainType == 1 ? vec3(q * s.engAngInertia, 0.0f, 0.0f)
                                           : vec3(0.0f, 0.0f, -(q * s.engAngInertia));
        local = vec3(local.x * body_.I.x, local.y * body_.I.y, local.z * body_.I.z);
        const vec3 &X = body_.R.r[0], &Y = body_.R.r[1], &Z = body_.R.r[2];
        body_.addTorque(vec3((local.z * Z.x + local.y * Y.x) + local.x * X.x,
                             (local.z * Z.y + local.y * Y.y) + local.x * X.y,
                             (local.z * Z.z + local.y * Y.z) + local.x * X.z));
    }
}

void Car::gearboxUpdate(float dt) {
    if (groundedWheels() == 0) return;
    const CarSimTune& s = tune_.sim;
    if (automatic_ && gear_ > 1 && gearTimer_ > s.gearChangeTime && !changed_) {
        if (gear_ >= s.autoNumGears - 1 || eng_.rpm <= up_[gear_]) {
            const float th = in_.throttle;
            if (gear_ >= 3 && eng_.rpm < th * downMin_[gear_] + (1.0f - th) * downMax_[gear_]) setGear(gear_ - 1);
        } else {
            setGear(gear_ + 1);
        }
    }
    gearTimer_ = dt + gearTimer_;
}

void Car::aero(float dt, float invdt) {
    const CarSimTune& s = tune_.sim;
    const vec3& w = body_.w;
    const float cd[3] = {s.angCDamp.x, s.angCDamp.y, s.angCDamp.z};
    const float vd[3] = {s.angVelDamp.x, s.angVelDamp.y, s.angVelDamp.z};
    const float v2d[3] = {s.angVel2Damp.x, s.angVel2Damp.y, s.angVel2Damp.z};
    const float wc[3] = {w.x, w.y, w.z};
    const float Ii[3] = {body_.I.x, body_.I.y, body_.I.z};
    float t[3];
    const M3& R = body_.R;
    // Body-axis angular velocities in MM2's sum orders (vehicle-helpers.md).
    const float wbs[3] = {(R.r[0].z * w.z + R.r[0].y * w.y) + R.r[0].x * w.x,
                          (R.r[1].y * w.y + R.r[1].z * w.z) + R.r[1].x * w.x,
                          (R.r[2].y * w.y + R.r[2].z * w.z) + R.r[2].x * w.x};
    for (int i = 0; i < 3; ++i) {
        const float wb = wbs[i];
        float ti = -(fsign(wb) * cd[i]);
        ti = (ti - wb * vd[i]) - std::fabs(wb) * wb * v2d[i];
        if (std::fabs(wb) < std::fabs(ti) * dt) ti = -(invdt * wb);
        if (std::fabs(wc[i]) < 1.0f) ti = std::fabs(wc[i]) * ti;
        t[i] = ti * Ii[i];
    }
    const vec3 &X = R.r[0], &Y = R.r[1], &Z = R.r[2];
    body_.addTorque(vec3((t[0] * X.x + t[1] * Y.x) + t[2] * Z.x, (t[2] * Z.y + t[1] * Y.y) + t[0] * X.y,
                         (t[2] * Z.z + t[1] * Y.z) + t[0] * X.z));
    // Then drag, then downforce.
    body_.F = vmadd(body_.F, body_.v, -(speed_ * s.drag));
    body_.F = vmadd(body_.F, body_.R.r[1], -(speed_ * speed_ * s.down));
}

namespace {
void wheelSuspension(Wheel& w, RigidBody& body_, bool grounded, float newComp, float cosA, float dt, float invdt) {
    const float e = w.tune.suspensionExtent, l = w.tune.suspensionLimit;
    const float old = w.comp;
    w.bottomed = false;
    bool ext;
    if (!grounded) {
        w.comp = -e;
        ext = true;
    } else {
        w.comp = newComp;
        ext = false;
        if (newComp < -e) {
            w.comp = -e;
            ext = true;
        }
    }
    w.rate = clampf((w.comp - old) * invdt, -10.0f, 10.0f);
    float s = w.prog * w.comp + 1.0f;
    w.Fsusp = (w.comp * w.k + w.rate * w.damp) * s + w.L0;
    if (w.Fsusp < 0.0f) {
        w.Fsusp = 0.0f;
        const float a = dt * w.k;
        w.comp = (old * w.damp - a * e) / (a + w.damp);
        w.rate = (w.comp - old) * invdt;
        w.Ks = 0.0f;
        return;
    }
    if (w.comp > l) {
        float j = 0.0f;
        if (w.vN < 0.0f) j = -(w.vN / body_.invMassAlong(w.x, w.N));
        const float pushF = invdt * j * 0.25f;
        const float KsB = -(pushF / w.vN);
        body_.addPush(vscale(w.N, (w.comp - l) * cosA));
        w.bottomed = true;
        w.comp = l;
        s = w.prog * w.comp + 1.0f;
        w.rate = clampf((w.comp - old) * invdt, -10.0f, 10.0f);
        w.Fsusp = (w.comp * w.k + w.rate * w.damp) * s + w.L0;
        w.Fsusp = pushF + w.Fsusp;
        w.Ks = KsB;
        return;
    }
    if (!ext) w.Ks = (dt * w.k) / cosA + s * w.damp;
    else w.Ks = 0.0f;
}
}  // namespace

WheelHost Car::wheelHost() {
    WheelHost h;
    h.body = &body_;
    h.frame = &frame_;
    h.weatherScale = weatherScale_;
    h.frictionHandling = true;
    h.carFrictionHandling = tune_.sim.carFrictionHandling;
    h.trace = trace_ != nullptr;
    return h;
}

void Car::wheelContact(Wheel& w, float dt, float invdt, World& world) { wheelContactCall(w, wheelHost(), dt, invdt, world); }

void Car::wheelTyre(Wheel& w, float dt, float invdt) { wheelTyreCall(w, wheelHost(), dt, invdt); }

void wheelContactCall(Wheel& w, const WheelHost& host, float dt, float invdt, World& world) {
    // Wheel frame (vehicle-wheels.md): (Ww, wc) = (Wl, c)·M, the frame product of
    // vehicle-body.md "Products" with M the model frame of step 5 (always evaluated).
    RigidBody& body_ = *host.body;
    const Mtx& M = *host.frame;
    Mat3 Wl;
    Wl.m[0][0] = Wl.m[1][1] = Wl.m[2][2] = 1.0f;
    vec3 c = w.pivot;
    if (w.tune.steeringLimit != 0.0f) {
        const float o = (fsign(w.pivot.x) * w.width) * 0.5f;
        const float ca = fcos(w.steerAngle), sa = fsin(w.steerAngle);
        Wl.m[0][0] = ca;
        Wl.m[0][2] = -sa;
        Wl.m[2][0] = sa;
        Wl.m[2][2] = ca;
        c = vec3(w.pivot.x - o, w.pivot.y, w.pivot.z);
        c = vec3(o * Wl.m[0][0] + c.x, o * Wl.m[0][1] + c.y, o * Wl.m[0][2] + c.z);
    }
    Mat3 R;
    const vec3* rows[3] = {&M.X, &M.Y, &M.Z};
    for (int i = 0; i < 3; ++i) {
        R.m[i][0] = rows[i]->x;
        R.m[i][1] = rows[i]->y;
        R.m[i][2] = rows[i]->z;
    }
    const Mat3 Ww = mulProducts(Wl, R);
    const vec3 WwX(Ww.m[0][0], Ww.m[0][1], Ww.m[0][2]);
    const vec3 WwY(Ww.m[1][0], Ww.m[1][1], Ww.m[1][2]);
    w.axle = WwX;  // visual only (skid marks)
    const vec3& X = M.X;
    const vec3& Y = M.Y;
    const vec3& Z = M.Z;
    const vec3 wc(((c.y * Y.x + c.x * X.x) + c.z * Z.x) + M.o.x, ((c.y * Y.y + c.x * X.y) + c.z * Z.y) + M.o.y,
                  ((c.z * Z.z + c.y * Y.z) + c.x * X.z) + M.o.z);

    const float e = w.tune.suspensionExtent;
    const float cTop = w.tune.suspensionLimit + 0.3f;
    const vec3 top = vmadd(wc, WwY, cTop);
    const float d = e + w.radius;
    const vec3 bottom(wc.x - d * WwY.x, wc.y - d * WwY.y, wc.z - d * WwY.z);
    const float len = e + w.radius + cTop;

    ProbeHit hit;
    bool grounded = false;
    float cosA = 0.0f;
    vec3 back;
    float bb = 0.0f;
    const bool probed = world.probe(top, bottom, w.probe, hit);
    if (host.trace) {
        w.trace = Wheel::Trace();
        if (probed) {
            w.trace.hit = true;
            w.trace.T = hit.t;
            w.trace.n = hit.normal;
            w.trace.p = hit.point;
            w.trace.mat = hit.materialCode;
            w.trace.obj = hit.object;
        }
    }
    if (probed) {
        const vec3& n = hit.normal;
        cosA = (n.z * WwY.z + n.y * WwY.y) + n.x * WwY.x;  // collision.md: n·u summed z, y, x
        back = pcross(WwX, n);
        bb = pdot(back, back);
        grounded = cosA >= 0.02f && bb >= 0.02f;
    }
    if (!grounded) {
        wheelSuspension(w, body_, false, 0.0f, cosA, dt, invdt);
        w.Fz = w.Fsusp;
        w.grounded = false;
        w.trace.comp = w.comp;
        w.trace.spin = w.omega;
        return;
    }
    const vec3 n = hit.normal;
    w.Back = vscale(back, 1.0f / fsqrt(bb));
    w.Lat = pcross(n, w.Back);
    w.N = n;
    w.x = hit.point;
    w.grounded = true;

    const vec3 vel = body_.velocityAt(w.x, invdt);
    w.vLat = (vel.z * w.Lat.z + vel.y * w.Lat.y) + vel.x * w.Lat.x;
    w.vFwd = ((-w.Back.z) * vel.z + (-w.Back.y) * vel.y) + (-w.Back.x) * vel.x;
    w.vN = (vel.z * w.N.z + vel.y * w.N.y) + vel.x * w.N.x;

    w.material = hit.hasMaterial ? hit.material : defaultSurface();
    const SurfaceMaterial& m = w.material;
    if (m.height == 0.0f) {
        w.bump = 0.0f;
    } else {
        const float s = fsqrt(w.vFwd * w.vFwd + w.vLat * w.vLat);
        w.phi = (((bumpRandom() + 0.618f) * dt) * s) + w.phi;
        w.phi = std::fmod(w.phi, m.width);
        // The sine is not rounded to float before the product with the height.
        w.bump = float(std::sin(double((w.phi * 6.2831855f) / m.width)) * double(m.height));
        if (s < 1.0f) w.bump = w.bump * s;
    }
    w.matDrag = m.drag;
    w.matFric = m.friction;
    const float target = w.slipping ? m.depth : 0.0f;
    const float rr = std::fabs(w.omega) * w.radius * 0.1f;
    if (target > w.depth) {
        w.depth = rr * dt + w.depth;
        if (target < w.depth) w.depth = target;
    } else if (target < w.depth) {
        w.depth = w.depth - rr * dt;
        if (w.depth < target) w.depth = target;
    }
    if (std::fabs(n.y) < 0.001f) w.matFric = 0.0f;
    const float weatherScale = host.weatherScale;
    w.matFric = weatherScale * w.matFric;
    if (host.frictionHandling && w.matFric < 1.0f) {
        const float cfh = host.carFrictionHandling;
        if (cfh >= 1.0f) w.matFric = (1.0f / cfh) * w.matFric;
        else w.matFric = (1.0f - cfh) * (1.0f - w.matFric) + w.matFric;
    }
    const float newComp = (cTop + w.radius) - ((len * hit.t - w.bump) + w.depth);
    wheelSuspension(w, body_, true, newComp, cosA, dt, invdt);
    w.Fz = w.Fsusp;
    w.trace.comp = w.comp;
    w.trace.spin = w.omega;
}

void wheelTyreCall(Wheel& w, const WheelHost& host, float dt, float invdt) {
    RigidBody& body_ = *host.body;
    if (!w.grounded) {
        w.slipping = false;
        w.slide = 0.0f;
        w.dL = w.dT = 0.0f;
        w.FL = w.FT = 0.0f;
        w.roadTorque = 0.0f;
        w.rotAngle = dt * w.omega + w.rotAngle;
        return;
    }
    const WheelTune& t = w.tune;
    if (w.Ks > 0.0f) {
        const float fk = (w.k * w.rate) * dt;
        const vec3 f = w.comp < t.suspensionLimit ? vec3(fk * w.N.x, fk * w.N.y, fk * w.N.z) : vec3();
        Mat3 M;
        const vec3 kn = vscale(w.N, w.Ks);
        const float kv[3] = {kn.x, kn.y, kn.z};
        const float nv[3] = {w.N.x, w.N.y, w.N.z};
        // Exactly symmetric: the factor a = Ks·n of the smaller index, n of the larger one.
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) M.m[i][j] = i <= j ? kv[i] * nv[j] : kv[j] * nv[i];
        body_.addImplicitContact(w.x, f, M);
    }

    // Slips.
    const float roll = w.omega * w.radius;
    const float vLat = w.vLat, vFwd = w.vFwd;
    if (vLat == 0.0f) w.latSlip = 0.0f;
    else if (std::fabs(vFwd) < std::fabs(vLat)) w.latSlip = fsign(vLat);
    else w.latSlip = vLat / std::fabs(vFwd);
    const float sV = roll + vFwd;
    w.sV = sV;  // visual only (wheel particles)
    if (sV == 0.0f) w.longSlip = 0.0f;
    else if (std::fabs(vFwd) < std::fabs(sV)) w.longSlip = fsign(sV);
    else w.longSlip = sV / std::fabs(vFwd);
    const float gL = (fsign(sV) * w.Fz) / w.Klong;
    const float gT = (fsign(vLat) * w.Fz) / w.Klat;
    const float stepT = dt * vLat;
    const float stepL = dt * sV;
    const float muS = t.staticFric * w.matFric;
    const float muK = t.slidingFric * w.matFric;
    const float opt = t.optimumSlipPercent;

    auto curve = [&](float slip, float& mu, float& vis) {
        const float s = std::fabs(slip);
        const float fpar = (opt + opt - s) * w.invOpt2 * muS * s;
        if (s <= opt) {
            mu = fpar;
            vis = (s * 0.5f) / opt;
            return;
        }
        if (muK < fpar) {
            mu = fpar;
            vis = ((muS - fpar) + (fpar - muK) * 0.5f) / (muS - muK);
            return;
        }
        mu = muK;
        vis = 1.0f;
    };

    float muL = muS, tL = muS * gL, visL = 0.0f;
    if (std::fabs(w.longSlip) <= opt || (stepL <= 0.0f && (w.dL <= tL || stepL < tL - w.dL)) ||
        (stepL > 0.0f && (tL <= w.dL || tL - w.dL < stepL))) {
        curve(w.longSlip, muL, visL);
        tL = muL * gL;
    }
    float muT = muS, tT = muS * gT, visT = 0.0f;
    if (std::fabs(w.latSlip) <= opt || (stepT <= 0.0f && (w.dT <= tT || stepT < tT - w.dT)) ||
        (stepT > 0.0f && (tT <= w.dT || tT - w.dT < stepT))) {
        curve(w.latSlip, muT, visT);
        tT = muT * gT;
    }
    float mx = std::fabs(w.longSlip);
    bool longDom = true;
    if (w.latSlip > mx) {
        mx = w.latSlip;
        longDom = false;
    } else if (mx < -w.latSlip) {
        mx = -w.latSlip;
        longDom = false;
    }
    float muMax = muS;
    if (opt <= mx) {
        if (longDom) {
            muMax = muL;
            if (muL < muT) tT = muL * gT;
        } else {
            muMax = muT;
            if (muT < muL) tL = muT * gL;
        }
    }

    // Deflections.
    const float relax = std::fabs(roll) * dt * 0.1f;
    auto deflect = [&](float step, float tgt, float& dd, float& eff, bool& stick) {
        const float nd = step + dd;
        eff = step;
        if (step < 0.0f) {
            if (tgt > nd) {
                const float cand = relax + dd;
                dd = tgt < cand ? tgt : cand;
                stick = false;
                eff = 0.0f;
            } else {
                dd = nd;
                stick = true;
            }
        } else if (tgt < nd) {
            const float cand = dd - relax;
            dd = cand < tgt ? tgt : cand;
            stick = false;
            eff = 0.0f;
        } else {
            dd = nd;
            stick = true;
        }
    };
    float effL = 0.0f, effT = 0.0f;
    bool stickL = false, stickT = false;
    deflect(stepL, tL, w.dL, effL, stickL);
    deflect(stepT, tT, w.dT, effT, stickT);

    // Forces.
    float FT = -(w.dT * w.Klat) - effT * invdt * w.Dlat;
    float FL = -(w.Klong * w.dL) - effL * invdt * w.Dlong;
    const float f2 = FT * FT + FL * FL;
    const float mf = muMax * w.Fz;
    const float cap = mf * mf;
    if (cap < f2) {
        if ((!stickL && opt < std::fabs(w.longSlip)) || (!stickT && opt < std::fabs(w.latSlip))) {
            const float q = -fsqrt(cap / (vLat * vLat + sV * sV));
            FT = q * vLat;
            FL = q * sV;
        } else {
            const float q = fsqrt(cap / f2);
            FT = q * FT;
            FL = q * FL;
        }
    }
    w.FT = FT;
    w.FL = FL;
    if (stickL) w.slide = stickT ? 0.0f : visT;
    else w.slide = stickT ? visL : (visT <= visL ? visL : visT);
    w.slipping = w.slide > 0.5f;
    w.roadTorque = FL * w.radius;
    const float dragT = -(std::fabs(vLat) * t.tireDragCoefLat * w.Fz * vLat * w.matDrag);
    const float dragL = -((w.depth + 1.0f) * -(std::fabs(vFwd) * t.tireDragCoefLong * w.Fz * w.matDrag * vFwd));
    const float mFL = -FL;
    const vec3& Bk = w.Back;
    const vec3& La = w.Lat;
    const vec3& Nn = w.N;
    // MM2's sum order (vehicle-wheels.md): starting with the suspension force.
    const vec3 force((((w.Fsusp * Nn.x + FT * La.x) + mFL * Bk.x) + dragT * La.x) + dragL * Bk.x,
                     (((w.Fsusp * Nn.y + FT * La.y) + mFL * Bk.y) + dragT * La.y) + dragL * Bk.y,
                     (((w.Fsusp * Nn.z + FT * La.z) + mFL * Bk.z) + dragT * La.z) + dragL * Bk.z);
    body_.addForceAt(force, w.x);
    w.rotAngle = dt * w.omega + w.rotAngle;
}

void Car::trainUpdate(Train& tr, float dt, float invdt, World& world) {
    const CarSimTune& s = tune_.sim;
    Engine& E = eng_;
    const int nw = int(tr.wheels.size());
    const bool coupled = tr.drive && E.coupled;
    const float r = ratio();
    const float Itr = tr.tune.angInertia;

    // 1. Brakes.
    const float bc = tr.wt == 0.0f ? tr.tune.brakeStaticCoef : tr.tune.brakeDynamicCoef;
    float brake = 50.0f;
    for (int i : tr.wheels) brake = wheels_[i].brakeTorque * bc + brake;
    // 2. Drive.
    float te = 0.0f, tau = 0.0f;
    if (coupled) {
        te = r * E.torque;
        tau = (r * tr.wt + E.we) * s.engAngInertia * invdt * r;
    }
    tau = tau + te;
    for (int i : tr.wheels) tau = tau - wheels_[i].roadTorque;
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
    const float Kc = coupled ? (r * s.engAngInertia * r + 0.02f) : (s.mass * 0.005f);
    // 5. Contact.
    for (int i : tr.wheels) wheelContact(wheels_[i], dt, invdt, world);
    // 6. Differential.
    {
        const float aw = std::fabs(tr.wt);
        const float dmax = aw < 50.0f ? (1.03f * aw + (50.0f - aw) * 1.25f) * 0.02f : 1.03f;
        const float dmin = 1.0f / dmax;
        if (nw < 2 || (tr.wt < 0.001f && -0.001f < tr.wt)) {
            tr.diff = 1.0f;
        } else {
            float imb = 0.0f;
            for (int k = 0; k < nw / 2; ++k)
                imb = (wheels_[tr.wheels[2 * k]].roadTorque - wheels_[tr.wheels[2 * k + 1]].roadTorque) + imb;
            const float target = imb / ((0.0f + Itr) * tr.wt) + tr.diff;
            float d = dmax;
            if (target <= dmax) d = target;
            if (target < dmin) d = dmin;
            tr.diff = (tr.diff * 9.0f + d) * 0.1f;
        }
    }
    // 7. Integrate.
    float wn = ((-net) / (dt * Itr + Kc)) * dt + tr.wt;
    if (stop && ((wn < 0.0f && 0.0f < tr.wt) || (tr.wt < 0.0f && 0.0f < wn))) wn = 0.0f;
    // 8. Engine.
    if (coupled) {
        const float lim = E.wMax / std::fabs(r);
        const float a = std::fabs(wn);
        wn = (a <= lim ? a : lim) * fsign(wn);
    }
    tr.wt = wn;
    if (coupled) {
        float ewant = -(r * wn);
        if (ewant >= 0.0f) {
            if (E.wMax < ewant) {
                ewant = E.wMax;
                tr.wt = -(ewant / r);
            }
            E.we = ewant;
        } else {
            tr.wt = 0.0f;
        }
    }
    // 9. Wheel spins.
    for (int k = 0; k < nw - 1; k += 2) {
        wheels_[tr.wheels[k]].omega = tr.diff * tr.wt;
        wheels_[tr.wheels[k + 1]].omega = (1.0f / tr.diff) * tr.wt;
    }
    if (nw % 2 == 1) wheels_[tr.wheels[nw - 1]].omega = tr.wt;
    // 10. Tyres.
    for (int i : tr.wheels) wheelTyre(wheels_[i], dt, invdt);
}

void Car::antiRoll() {
    for (int axle = 0; axle < 2; ++axle) {
        const float k = arbK_[axle];
        if (k == 0.0f) continue;
        const Wheel& a = wheels_[axle * 2];
        const Wheel& b = wheels_[axle * 2 + 1];
        const float diff = (a.comp - a.visualDisp()) - (b.comp - b.visualDisp());
        const float tq = -(diff * k + (a.rate - b.rate) * arbD_[axle]);
        body_.addTorque(vscale(body_.R.r[2], tq));
    }
}

void Car::gyro() {
    const GyroTune& g = tune_.gyro;
    const float allDown = float(groundedWheels() / 4);
    const float wt = driveTrainSpeed();
    const vec3& I = body_.I;
    const M3& R = body_.R;
    if (g.drift > 0.0f) {
        const float st = sss_ * in_.steer;
        const float tq = -(((allDown * (std::fabs(st) * st)) * (-wt)) * (I.y * g.drift));
        body_.addTorque(vscale(R.r[1], tq));
    }
    if (std::fabs(in_.handbrake) > 0.01f && (g.spin180 > 0.0f || g.reverse180 > 0.0f)) {
        const float c = wt < 0.0f ? g.spin180 : g.reverse180;
        const float tq = -(allDown * in_.steer * -wt * I.y * c);
        body_.addTorque(vscale(R.r[1], tq));
    }
    if (std::fabs(in_.brake) > 0.01f && (g.pitch > 0.0f || g.roll > 0.0f)) {
        const float f = in_.brake * in_.brake * (1.0f - allDown);
        const float kp = (I.x * g.pitch) * f;
        const float py = R.r[2].y;
        body_.addTorque(vec3(kp * (py * R.r[0].x), kp * (py * R.r[0].y), kp * (py * R.r[0].z)));
        const float kr = -((g.roll * I.z) * f);
        const float ry = R.r[0].y;
        body_.addTorque(vec3(kr * (ry * R.r[2].x), kr * (ry * R.r[2].y), kr * (ry * R.r[2].z)));
    }
}

// An impact report (vehicle-body.md, "Contact interface"): wakes the unstuck helper with the body
// position of this moment (before the step's pushes); the timer is not touched.
void Car::damageImpact(const vec3& J, int otherIdentity, float otherMass, const vec3& P, const vec3& n,
                       int otherSound) {
    // Car space with the model frame of this sample (rows X, Y, Z, origin o), damage.md "One body
    // part per strong impact": the inverse's translation t.k = -((R.x*o.x + R.y*o.y) + R.z*o.z),
    // then point.k = ((R.z*P.z + R.y*P.y) + R.x*P.x) + t.k for R = X, Y, Z.
    DamageState::Contact c;
    c.world = P;
    c.normal = n;
    const vec3& o = frame_.o;
    auto toCar = [&](const vec3& R) {
        const float t = -((R.x * o.x + R.y * o.y) + R.z * o.z);
        return ((R.z * P.z + R.y * P.y) + R.x * P.x) + t;
    };
    c.car = vec3(toCar(frame_.X), toCar(frame_.Y), toCar(frame_.Z));
    float s = 0.0f;
    bool sound = false;
    const DamageState::Slot* counted =
        damage_.impact(tune_.damage, J, otherIdentity, otherMass, body_.m, speedMph_, c, &s, &sound);
    if (sound && impactSink_) impactSink_->carImpact(J, otherSound);  // the impact sound event
    if (!counted) return;
    if (countedHook_) countedHook_->countedImpact(*this, otherIdentity, counted->total);
    if (feedbackHook_) feedbackHook_->countedImpact(*this, otherIdentity, counted->total);
    // The count step: the visual effects start, and from s = 10000 on one body part is thrown off.
    if (effects_) effects_->countedImpact(*this, *counted, s, lastDt_);
    if (!(s >= 10000.0f)) return;
    // The attached body part nearest the slot's contact point (strictly nearer than 100000).
    const vec3& contact = counted->carContact;
    float best = 100000.0f;
    int pick = -1;
    for (size_t i = 0; i < tune_.looseParts.size(); ++i) {
        const LoosePartTune& lp = tune_.looseParts[i];
        if (lp.gear || thrown_[i] || (lp.variant >= 0 && lp.variant != paintSet_)) continue;
        const vec3 e(lp.place.x - contact.x, lp.place.y - contact.y, lp.place.z - contact.z);
        const float d2 = (e.z * e.z + e.y * e.y) + e.x * e.x;
        if (d2 < best) {
            best = d2;
            pick = int(i);
        }
    }
    if (pick >= 0) throwPart(pick, 4.0f, 1.0f);
}

void Car::clearDamage() {
    damage_.reset();
    wreckThrown_ = false;
    bool any = false;
    for (size_t i = 0; i < thrown_.size(); ++i) {
        if (thrown_[i]) any = true;
        thrown_[i] = false;
    }
    if (any && world_) world_->takeBackCarParts(*this);
}

void Car::setPaintSet(int set) { paintSet_ = set; }

bool Car::partThrown(const std::string& name) const {
    for (size_t i = 0; i < tune_.looseParts.size(); ++i)
        if (thrown_[i] && tune_.looseParts[i].name == name) return true;
    return false;
}

int Car::partIndex(const std::string& name) const {
    for (size_t i = 0; i < tune_.looseParts.size(); ++i)
        if (tune_.looseParts[i].name == name) return int(i);
    return -1;
}

// damage.md, "Throwing a part": the eight launch draws from the game-wide generator; the world
// makes the part a loose body (loose-bodies.md 4).
void Car::throwPart(int i, float A, float B) {
    if (i < 0 || size_t(i) >= thrown_.size() || thrown_[size_t(i)]) return;
    thrown_[size_t(i)] = true;
    float r[8];
    for (float& x : r) x = gameRandomUnit();
    vec3 dir(2.0f * r[1] - 1.0f, r[0], 2.0f * r[2] - 1.0f);
    float L2 = (dir.z * dir.z + dir.x * dir.x) + dir.y * dir.y;
    float n = L2 == 0.0f ? 0.0f : 1.0f / fsqrt(L2);
    dir = vec3(dir.x * n, dir.y * n, dir.z * n);
    const float hi = A + B, lo = A - B;
    const float sp = ((hi - lo) * r[3]) + lo;
    const vec3 P(dir.x * sp, dir.y * sp, dir.z * sp);
    vec3 axis(2.0f * r[5] - 1.0f, r[4], 2.0f * r[6] - 1.0f);
    L2 = (axis.z * axis.z + axis.y * axis.y) + axis.x * axis.x;
    n = L2 == 0.0f ? 0.0f : 1.0f / fsqrt(L2);
    axis = vec3(axis.x * n, axis.y * n, axis.z * n);
    const float C = 2.0f, D = 1.0f;
    const float hi2 = C + D, lo2 = C - D;
    const float k = ((hi2 - lo2) * r[7]) + lo2;
    const vec3 H(axis.x * k, axis.y * k, axis.z * k);
    if (world_) world_->throwCarPart(*this, i, P, H);
}

// damage.md, "Wrecked": once per reset, by the car's speed of this sample.
void Car::wreckThrow() {
    const float A = 1.3f * speed_;
    auto named = [&](const std::string& n) {
        const int i = partIndex(n);
        if (i >= 0 && !thrown_[size_t(i)]) throwPart(i, A, 1.0f);
    };
    auto pair = [&]() {
        const int k = gameRandom15() & 3;
        named(strf("WHL%d", k));
        named(strf("HUB%d", k));
    };
    if (speedMph_ > 100.0f) {
        for (size_t i = 0; i < tune_.looseParts.size(); ++i)
            if (tune_.looseParts[i].gear && !thrown_[i]) throwPart(int(i), A, 1.0f);
    } else if (speedMph_ > 75.0f) {
        pair();
        pair();
        const int f = gameRandom15() & 1;
        named(strf("FNDR%d", f));
    } else if (speedMph_ > 50.0f) {
        pair();
    }
}

void Car::aiControlStep(const CarInputs& in, int setGear) {
    in_ = in;
    if (wrecked()) {
        in_.throttle = 0.0f;
        in_.brake = 0.0f;
        in_.steer = 0.0f;
    }
    if (setGear != -100) this->setGear(setGear);
}

void Car::setAiStuck(float timeThresh) {
    tune_.stuck.rotation = 0.0f;
    tune_.stuck.timeThresh = timeThresh;
    tune_.stuck.posThresh = 1.0f;
}

void Car::notifyImpact() {
    if (stuck_ == Stuck::Inactive) {
        stuck_ = Stuck::Watch;
        stuckStart_ = body_.p;
    }
}

// The unstuck helper (vehicle-helpers.md), step 11. Comparisons are written so that a NaN goes
// the way MM2's does.
void Car::unstuck(float dt) {
    const StuckTune& st = tune_.stuck;
    if (stuck_ == Stuck::Inactive) return;
    auto deactivate = [&]() {
        stuck_ = Stuck::Inactive;
        stuckTimer_ = 0.0f;
    };
    const vec3& p = body_.p;
    const vec3& s0 = stuckStart_;
    const float dx = s0.x - p.x, dy = s0.y - p.y, dz = s0.z - p.z;
    const float d3 = (dz * dz + dy * dy) + dx * dx;
    const float dxz = dx * dx + dz * dz;
    const float P2 = st.posThresh * st.posThresh;
    const float M2 = st.moveThresh * st.moveThresh;
    const bool turnWanted = (0.75f * engineScale_) < in_.throttle && std::fabs(in_.steer) > 0.5f;
    if (d3 > M2) {
        deactivate();
        return;
    }
    switch (stuck_) {
        case Stuck::Watch:
            stuckTimer_ = dt + stuckTimer_;
            if (!(dxz > P2) && groundedWheels() == 0 && stuckTimer_ >= st.timeThresh &&
                (st.translation > 0.0f || st.rotation > 0.0f)) {
                stuck_ = st.rotation > 0.0f ? Stuck::Roll : Stuck::Lift;
                return;
            }
            if (!(dxz > P2) && stuckTimer_ >= st.timeThresh && turnWanted) {
                stuck_ = Stuck::Turn;
                return;
            }
            if (!(stuckTimer_ > st.timeThresh)) return;
            deactivate();
            return;
        case Stuck::Lift: {
            const vec3& Z = body_.R.r[2];
            vec3 X(Z.z - 0.0f, 0.0f, 0.0f - Z.x);
            const float l2 = (X.z * X.z + 0.0f * 0.0f) + X.x * X.x;
            const float k = l2 == 0.0f ? 0.0f : 1.0f / fsqrt(l2);
            X = vec3(k * X.x, k * X.y, k * X.z);
            const vec3 Y(0.0f, 1.0f, 0.0f);
            const vec3 Zn(X.y * Y.z - X.z * Y.y, X.z * Y.x - Y.z * X.x, Y.y * X.x - X.y * Y.x);
            body_.R.r[0] = X;
            body_.R.r[1] = Y;
            body_.R.r[2] = Zn;
            body_.p.y = body_.p.y + st.translation;
            deactivate();
            return;
        }
        case Stuck::Roll: {
            if (body_.R.r[1].y > 0.7f) {
                deactivate();
                return;
            }
            body_.G.y = ((std::fabs(in_.steer) * body_.m) * st.translation) + body_.G.y;
            const float q = (in_.steer * body_.m) * st.rotation;
            const vec3& Z = body_.R.r[2];
            body_.H = vec3(q * Z.x + body_.H.x, q * Z.y + body_.H.y, q * Z.z + body_.H.z);
            in_.throttle = 0.0f;
            in_.brake = 1.0f;
            return;
        }
        case Stuck::Turn: {
            if (!turnWanted) {
                deactivate();
                return;
            }
            float a = (std::fabs(in_.steer) * st.turn) * in_.steer;
            if (gear_ == 0) a = -a;
            const float ang = -(dt * a);
            // RotY with cosine and sine each rounded to float; matrix product of "Products".
            const float c = fcos(ang), sn = fsin(ang);
            Mat3 r;
            r.m[0][0] = c;
            r.m[0][2] = -sn;
            r.m[1][1] = 1.0f;
            r.m[2][0] = sn;
            r.m[2][2] = c;
            body_.R = fromMat3(mulProducts(toMat3(body_.R), r));
            return;
        }
        default:
            return;
    }
}

namespace {
// drive-trace.md: exact float bits as C's %a of the float converted to double.
void traceF(std::FILE* f, float v) {
    if (std::isnan(v)) std::fputs(" nan", f);
    else if (std::isinf(v)) std::fputs(v > 0 ? " inf" : " -inf", f);
    else std::fprintf(f, " %a", double(v));
}
void traceV(std::FILE* f, const vec3& v) {
    traceF(f, v.x);
    traceF(f, v.y);
    traceF(f, v.z);
}
}  // namespace

void Car::traceStart() const {
    std::FILE* f = trace_;
    std::fprintf(f, "step %d", traceK_);
    traceF(f, traceT_);
    std::fputs("\nctl", f);
    traceF(f, in_.throttle);
    traceF(f, in_.brake);
    traceF(f, in_.steer);
    traceF(f, in_.handbrake);
    std::fprintf(f, " %d\nbody", gearIndex() - 1);
    traceV(f, body_.p);
    traceV(f, body_.v);
    traceV(f, body_.w);
    std::fputs("\nrot", f);
    for (int i = 0; i < 3; ++i) traceV(f, body_.R.r[i]);
    std::fputs("\n", f);
}

void Car::traceAfterWheels() const {
    std::FILE* f = trace_;
    for (int i = 0; i < 4; ++i) {
        const Wheel::Trace& t = wheels_[i].trace;
        std::fprintf(f, "wheel %d %d", i, t.hit ? 1 : 0);
        traceF(f, t.T);
        traceV(f, t.n);
        traceV(f, t.p);
        std::fprintf(f, " %d %d", t.mat, t.obj);
        traceF(f, t.comp);
        traceF(f, t.spin);
        std::fputs("\n", f);
    }
}

void Car::traceEnd() const {
    std::FILE* f = trace_;
    std::fprintf(f, "contact %zu\n", col_.traceImpacts.size());
    static const char* const kinds[4] = {"city", "obj", "prop", "loose"};
    for (size_t j = 0; j < col_.traceImpacts.size(); ++j) {
        const auto& ti = col_.traceImpacts[j];
        std::fprintf(f, "impact %zu %s", j, kinds[ti.kind]);
        traceV(f, ti.im.P);
        traceV(f, ti.im.n);
        traceF(f, ti.im.depth);
        std::fprintf(f, " %d\n", ti.obj);
    }
    std::fputs("push", f);
    traceV(f, body_.push);
    std::fputs("\n", f);
}

void Car::step(float dt, float invdt, World& world) {
    stepUpdate(dt, invdt, world);
    // 12. World collision of the body (collision.md).
    world.collideCar(*this, dt, invdt);
    stepEnd();
}

void Car::stepUpdate(float dt, float invdt, World& world) {
    world_ = &world;
    if (col_.roomPending) {
        col_.roomPending = false;
        world.placeCar(*this);
    }
    if (trace_) {
        traceStart();
        col_.tracing = true;
        col_.traceImpacts.clear();
        for (Wheel& w : wheels_) w.trace = Wheel::Trace();
    }
    // 1. Gravity.
    body_.addForce(vec3(0.0f, body_.m * kGravity, 0.0f));
    // 2. Speed.
    // Z = the Z row of the previous step's model frame (vehicle.md step 2).
    speed_ = std::fabs((frame_.Z.z * body_.v.z + frame_.Z.y * body_.v.y) + frame_.Z.x * body_.v.x);
    speedMph_ = speed_ * kMph;
    // 3. Wheel inputs.
    wheelInputs();
    // 4. Body integration.
    body_.integrate(dt);
    // 5. Model frame.
    origin_ = modelOrigin();
    frame_ = mtxFromRows(body_.R.r[0], body_.R.r[1], body_.R.r[2], origin_);
    // 6-8.
    engineUpdate(dt);
    gearboxUpdate(dt);
    aero(dt, invdt);
    // 9. Wheel groups.
    for (Train& t : trains_) trainUpdate(t, dt, invdt, world);
    if (trace_) traceAfterWheels();
    // 10-11.
    antiRoll();
    gyro();
    if (drivable_) unstuck(dt);  // a not-drivable car's unstuck helper is not updated
    // ... then the water effect and the room update.
    world.afterCarUpdate(*this, dt);
    if (trace_) {
        std::fputs("force", trace_);
        traceV(trace_, body_.F);
        traceV(trace_, body_.T);
        std::fputs("\n", trace_);
    }
    // Damage: regeneration and slot timers, then the wreck throw (damage.md, before the collisions).
    damage_.sample(dt, tune_.damage);
    if (!wreckThrown_ && damage_.damage >= tune_.damage.maxDamage) {
        wreckThrown_ = true;
        wreckThrow();
    }
    lastDt_ = dt;
    if (effects_) effects_->sampleEffects(*this, dt);  // drawing only
}

void Car::stepEnd() {
    const float dt = lastDt_;
    // 13. End of step: the model frame of step 5 becomes the previous matrix, the push source
    // the previous source; then the pushes.
    col_.prevMatrix = frame_;
    col_.prevLargestSource = col_.largestSource;
    col_.largestSource = 0;
    col_.largestPush2 = 0.0f;
    if (trace_) traceEnd();
    body_.applyPush();
    ++steps_;
    time_ = float(steps_) * dt;
    if (trace_) {
        std::fputs("end", trace_);
        traceV(trace_, body_.p);
        traceV(trace_, body_.v);
        traceV(trace_, body_.w);
        std::fputs("\n", trace_);
        col_.tracing = false;
    }
}

}  // namespace phys
}  // namespace cc
