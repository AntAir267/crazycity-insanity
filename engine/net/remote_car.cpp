#include "net/remote_car.h"

#include <cmath>

namespace cc {
namespace net {

namespace {

struct Quat {
    float w = 1, x = 0, y = 0, z = 0;
};

// The rotation's matrix m[i][j] = row i, component j (any consistent convention works: slerp
// commutes with the transpose, which is the quaternion's conjugate).
Quat quatOf(const phys::M3& R) {
    const float m00 = R.r[0].x, m01 = R.r[0].y, m02 = R.r[0].z;
    const float m10 = R.r[1].x, m11 = R.r[1].y, m12 = R.r[1].z;
    const float m20 = R.r[2].x, m21 = R.r[2].y, m22 = R.r[2].z;
    Quat q;
    const float tr = (m00 + m11) + m22;
    if (tr > 0.0f) {
        const float s = std::sqrt(tr + 1.0f) * 2.0f;
        q.w = 0.25f * s;
        q.x = (m21 - m12) / s;
        q.y = (m02 - m20) / s;
        q.z = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(((1.0f + m00) - m11) - m22) * 2.0f;
        q.w = (m21 - m12) / s;
        q.x = 0.25f * s;
        q.y = (m01 + m10) / s;
        q.z = (m02 + m20) / s;
    } else if (m11 > m22) {
        const float s = std::sqrt(((1.0f + m11) - m00) - m22) * 2.0f;
        q.w = (m02 - m20) / s;
        q.x = (m01 + m10) / s;
        q.y = 0.25f * s;
        q.z = (m12 + m21) / s;
    } else {
        const float s = std::sqrt(((1.0f + m22) - m00) - m11) * 2.0f;
        q.w = (m10 - m01) / s;
        q.x = (m02 + m20) / s;
        q.y = (m12 + m21) / s;
        q.z = 0.25f * s;
    }
    return q;
}

phys::M3 matrixOf(const Quat& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    phys::M3 R;
    R.r[0] = vec3(1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (x * z + y * w));
    R.r[1] = vec3(2.0f * (x * y + z * w), 1.0f - 2.0f * (x * x + z * z), 2.0f * (y * z - x * w));
    R.r[2] = vec3(2.0f * (x * z - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (x * x + y * y));
    return R;
}

vec3 normalised(const vec3& v) {
    const float l2 = (v.x * v.x + v.y * v.y) + v.z * v.z;
    if (!(l2 > 0.0f)) return vec3();
    const float k = 1.0f / std::sqrt(l2);
    return vec3(v.x * k, v.y * k, v.z * k);
}

}  // namespace

vec3 Hermite::at(float s) const {
    const float s2 = s * s, s3 = s2 * s;
    return vec3(((a.x * s3 + b.x * s2) + c.x * s) + d.x, ((a.y * s3 + b.y * s2) + c.y * s) + d.y,
                ((a.z * s3 + b.z * s2) + c.z * s) + d.z);
}

Hermite Hermite::make(const vec3& P0, const vec3& P1, const vec3& T0, const vec3& T1) {
    // Rows (2, −2, 1, 1), (−3, 3, −2, −1), (0, 0, 1, 0), (1, 0, 0, 0).
    Hermite h;
    h.a = vec3(((2.0f * P0.x - 2.0f * P1.x) + T0.x) + T1.x, ((2.0f * P0.y - 2.0f * P1.y) + T0.y) + T1.y,
               ((2.0f * P0.z - 2.0f * P1.z) + T0.z) + T1.z);
    h.b = vec3(((-3.0f * P0.x + 3.0f * P1.x) - 2.0f * T0.x) - T1.x, ((-3.0f * P0.y + 3.0f * P1.y) - 2.0f * T0.y) - T1.y,
               ((-3.0f * P0.z + 3.0f * P1.z) - 2.0f * T0.z) - T1.z);
    h.c = T0;
    h.d = P0;
    h.valid = true;
    return h;
}

phys::M3 slerpRotation(const phys::M3& from, const phys::M3& to, float t) {
    const Quat a = quatOf(from);
    Quat b = quatOf(to);
    float d = ((a.w * b.w + a.x * b.x) + a.y * b.y) + a.z * b.z;
    if (d < 0.0f) {  // the shorter way
        b.w = -b.w;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        d = -d;
    }
    float ka, kb;
    if (d > 0.9995f) {
        ka = 1.0f - t;
        kb = t;
    } else {
        const float th = float(std::acos(double(d)));
        const float s = float(std::sin(double(th)));
        ka = float(std::sin(double((1.0f - t) * th))) / s;
        kb = float(std::sin(double(t * th))) / s;
    }
    Quat q;
    q.w = a.w * ka + b.w * kb;
    q.x = a.x * ka + b.x * kb;
    q.y = a.y * ka + b.y * kb;
    q.z = a.z * ka + b.z * kb;
    const float l2 = ((q.w * q.w + q.x * q.x) + q.y * q.y) + q.z * q.z;
    if (l2 > 0.0f) {
        const float k = 1.0f / std::sqrt(l2);
        q.w *= k;
        q.x *= k;
        q.y *= k;
        q.z *= k;
    }
    return matrixOf(q);
}

phys::M3 rotateRows(const phys::M3& R, const vec3& u, float theta) {
    const float c = phys::fcos(theta), s = phys::fsin(theta), k = 1.0f - c;
    phys::M3 out;
    for (int i = 0; i < 3; ++i) {
        const vec3& v = R.r[i];
        const vec3 x = phys::pcross(u, v);
        const float ud = phys::pdot(u, v);
        out.r[i] = vec3((v.x * c + x.x * s) + u.x * (ud * k), (v.y * c + x.y * s) + u.y * (ud * k),
                        (v.z * c + x.z * s) + u.z * (ud * k));
    }
    return out;
}

float RemoteCar::meanInterval() const {
    float sum = 0.0f;
    for (float x : intervals_) sum += x;
    return sum * 0.1f;
}

bool RemoteCar::receive(const CarState& s, double now, bool& damageCleared) {
    damageCleared = false;
    sirenChanged = hornChanged = false;
    // 1. Order.
    if (haveFirst_ && s.seq < lastSeq_) return false;
    lastSeq_ = s.seq;
    gotState_ = true;
    // 2. Decode.
    const phys::M3 M = matrixFromAngles(s.a, s.b, s.c);
    const vec3 p = s.pos;
    const vec3 A = decodeDirection(s.accelLen, s.accelDir);
    const vec3 V = decodeDirection(s.velLen, s.velDir);
    const vec3 W = decodeDirection(s.angLen, s.angDir);
    phys::RigidBody& b = car.body();
    // 3. The first state ever: the car goes there.
    if (!haveFirst_) {
        b.R = M;
        b.p = p;
        haveFirst_ = true;
    }
    // 4. Velocity and momentum (not the angular velocity).
    b.v = V;
    b.P = vec3(b.m * V.x, b.m * V.y, b.m * V.z);
    // 5. Inputs.
    in_.steer = float(s.steer) / 127.0f;
    in_.brake = float(s.brake) * 0.003921569f;
    in_.throttle = float(s.throttle) * 0.003921569f;
    // 6. Damage.
    const float old = car.damage().damage;
    if (old > 1.0f && s.damage < 0.0001f) {
        car.clearDamage();  // the damage visuals with it
        damageCleared = true;
    }
    car.damage().damage = s.damage;
    // 7. Packet interval.
    const double delta = now - lastClock_;
    lastClock_ = now;
    intervals_[intervalNext_] = float(delta);
    intervalNext_ = (intervalNext_ + 1) % 10;
    const float T = meanInterval();
    // 8. Target.
    const float T2 = T * T;
    const vec3 pStar((p.x + T * V.x) + (0.5f * A.x) * T2, (p.y + T * V.y) + (0.5f * A.y) * T2,
                     (p.z + T * V.z) + (0.5f * A.z) * T2);
    phys::M3 mStar = M;
    const float wl = std::sqrt((W.x * W.x + W.y * W.y) + W.z * W.z);
    if (wl > 1e-6f) mStar = rotateRows(M, vec3(W.x / wl, W.y / wl, W.z / wl), T * wl);
    const bool reset = (s.flags & FlagReset) != 0;
    if (!reset) {
        // 9. Drift test.
        const vec3 e = vec3(b.p.x - p.x, b.p.y - p.y, b.p.z - p.z);
        float d = std::sqrt((e.x * e.x + e.y * e.y) + e.z * e.z);
        if (d < 2.0f) d = 0.0f;
        if (d <= 10.0f) {
            const float radius = car.bound() ? car.bound()->radius : 0.0f;
            offCourse_ = radius < d;
        }
        // 10. The orientation jumps to the sent one; the position stays.
        b.R = M;
        targetP_ = pStar;
        targetR_ = mStar;
        // 12. The curve from here to the target.
        curve_ = Hermite::make(b.p, pStar, normalised(b.R.r[2]), normalised(mStar.r[2]));
    } else {
        // 11. A reset state: snapped, no curve, not off course.
        b.R = M;
        b.p = p;
        targetP_ = p;
        targetR_ = M;
        curve_ = Hermite();
        offCourse_ = false;
    }
    // 13. Score, host clock, flags.
    score_ = s.score;
    hostClock_ = s.hostClock;
    const uint16_t was = flags_;
    flags_ = s.flags;
    hornChanged = ((was ^ flags_) & FlagHorn) != 0;
    sirenChanged = ((was ^ flags_) & FlagSiren) != 0;
    const bool rev = (s.flags & FlagReverse) != 0;
    if (rev && car.gearIndex() != 0) car.selectGear(0);
    else if (!rev && car.gearIndex() == 0) car.selectGear(2);
    in_.handbrake = (s.flags & FlagHandbrake) ? 1.0f : 0.0f;
    if (s.flags & FlagManual) {
        car.setAutomatic(false);
        car.selectGear(s.gear);
    } else {
        car.setAutomatic(true);
    }
    const bool drivable = (s.flags & FlagNotDrivable) == 0;
    // On a change only, the local car's switch: drivable again → first gear from neutral or reverse.
    if (drivable && !car.drivable() && car.gearIndex() <= 1) car.selectGear(2);
    car.setDrivable(drivable);
    // 14. Frame counts.
    fTotal_ += fSince_;
    fSince_ = 0;
    fAvg_ = s.seq > 0 ? int(uint32_t(fTotal_) / s.seq) : 0;
    return true;
}

void RemoteCar::countFrame() {
    if (!gotState_) ++fSince_;
    gotState_ = false;
}

void RemoteCar::beforePhysics(float dt, const std::function<int(const vec3&, int)>& findRoom) {
    // The last received inputs (a car that is not drivable: brake and neutral).
    car.aiControlStep(in_);
    if (!car.drivable()) car.holdNotDrivable();
    // Smoothing.
    float s;
    if (fAvg_ == 0) s = fSince_ == 0 ? 0.0f : 1.0f;
    else s = float(fSince_) / float(fAvg_);
    if (s < 0.0f) s = 0.0f;
    if (s > 1.0f) s = 1.0f;
    phys::RigidBody& b = car.body();
    if (haveFirst_) {
        b.R = slerpRotation(b.R, targetR_, dt);
        if (offCourse_ && curve_.valid) {
            b.p = curve_.at(s);
        } else {
            b.p = vec3(b.p.x + (targetP_.x - b.p.x) * dt, b.p.y + (targetP_.y - b.p.y) * dt,
                       b.p.z + (targetP_.z - b.p.z) * dt);
        }
    }
    // Room 0 (outside the city): searched again.
    if (car.collision().room == 0 && findRoom) car.collision().room = findRoom(b.p, 0);
}

void RemoteCar::resetTo(const phys::M3& R, const vec3& p) {
    car.resetToBodyMatrix(R, p);
    targetR_ = R;
    targetP_ = p;
    curve_ = Hermite();
    offCourse_ = false;
}

void RemoteCar::placed() {
    targetR_ = car.rotation();
    targetP_ = car.body().p;
    curve_ = Hermite();
    offCourse_ = false;
}

}  // namespace net
}  // namespace cc
