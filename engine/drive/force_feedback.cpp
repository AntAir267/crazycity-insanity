#include "drive/force_feedback.h"

#include <algorithm>
#include <cmath>

#include "core/game_random.h"
#include "physics/car.h"

namespace cc {

namespace {
int clampCoef(int c) { return c < 0 ? 0 : (c > 10000 ? 10000 : c); }
}  // namespace

void ForceFeedback::configure(FfDevice* device, bool active, float collision, float road) {
    dev_ = device;
    active_ = active;
    C_ = collision;  // × the car's collision gain, always 1
    R_ = road;       // × the car's road gain, always 1
}

// Friction(v): clamp(trunc(v × 10000), 0, 10000), not scaled by R.
void ForceFeedback::sendFriction(float v) {
    const float p = v * 10000.0f;
    dev_->setFriction(clampCoef(int(p)));
}

// Spring(k): clamp(trunc(R) × trunc(k × 10000), 0, 10000); with R = 0 neither set nor started.
void ForceFeedback::sendSpring(float k) {
    if (R_ == 0.0f) return;
    const float p = k * 10000.0f;
    dev_->setSpring(clampCoef(int(std::trunc(R_)) * int(p)));
}

void ForceFeedback::stopRumble() {
    if (dev_->playing(FfDevice::Rumble)) dev_->stop(FfDevice::Rumble);
}

// Rumble request(f, m): below 1 Hz the rumble stops; else it plays with P = 1 / clamp(f, 1, 20)
// and M = clamp(m, 0, 1), sent when the pair changes: period trunc(P × 1 000 000) µs, magnitude
// trunc(R × trunc(M × 10000)). With R = 0 neither set nor started.
void ForceFeedback::rumbleRequest(float f, float m) {
    if (f < 1.0f) {
        stopRumble();
        return;
    }
    if (R_ == 0.0f) return;
    if (!dev_->playing(FfDevice::Rumble)) dev_->start(FfDevice::Rumble);
    const float P = 1.0f / std::clamp(f, 1.0f, 20.0f);
    const float M = std::clamp(m, 0.0f, 1.0f);
    if (P != sentP_ || M != sentM_) {
        const float pu = P * 1000000.0f;
        const float mm = M * 10000.0f;
        const float mag = R_ * float(int(mm));
        dev_->setRumble(int(pu), int(mag));
        sentP_ = P;
        sentM_ = M;
    }
}

// Jolt(m, angle): strength trunc(C × trunc(m × 10000)), direction clamp(trunc(angle), 0, 360) × 100;
// with C = 0 nothing happens.
void ForceFeedback::sendJolt(float m, float angle) {
    if (C_ == 0.0f) return;
    const float mm = m * 10000.0f;
    const float s = C_ * float(int(mm));
    const int dir = std::clamp(int(angle), 0, 360) * 100;
    dev_->jolt(int(s), dir);
    ++jolts_;
}

void ForceFeedback::frame(const phys::Car& car) {
    if (!active()) return;
    if (car.resetCount() != lastReset_) {
        lastReset_ = car.resetCount();
        reset();
    }
    const phys::Wheel& fl = car.wheel(0);  // the front-left wheel
    const float v = car.speed();
    // 2. Friction and spring play.
    if (!dev_->playing(FfDevice::Friction)) dev_->start(FfDevice::Friction);
    if (!dev_->playing(FfDevice::Spring)) dev_->start(FfDevice::Spring);
    // 3. Friction from the front-left tyre's surface.
    if (fl.matFric != Lf_) {
        sendFriction(0.7f * fl.matFric);
        Lf_ = fl.matFric;
    }
    // 4. Rumble, by damage.
    const phys::DamageTune& dt = car.damageTune();
    float d = dt.maxDamage != dt.medDamage ? (car.damage().damage - dt.medDamage) / (dt.maxDamage - dt.medDamage) : 0.0f;
    d = std::clamp(d, 0.0f, 1.0f);
    if (d > 0.0f) {
        const int grounded = car.groundedWheels();
        if (grounded > 0 && now_ - J_ > 0.5) {
            const float r10 = fl.radius * 10.0f;
            rumbleRequest(v / r10, d * 0.4f);
        } else if (grounded == 0) {
            stopRumble();
        }
    } else {
        const float bh = fl.material.height, bw = fl.material.width;
        if (!(bh == Lh_ && bw == Lw_ && v == 0.0f)) {
            Lh_ = bh;
            Lw_ = bw;
            if (bh != 0.0f && bw >= 0.01f && v >= 1.0f) rumbleRequest(v / bw, bh);
            else stopRumble();
        }
    }
    // 5. Spring: none while the front-left tyre slips, else rising from 10 to 80 m/s.
    float k = 0.0f;
    if (std::fabs(fl.latSlip) < 0.05f && std::fabs(fl.longSlip) < 0.05f) {
        if (v > 80.0f) k = 1.0f;
        else if (v > 10.0f) k = (v - 10.0f) / 70.0f;
    }
    if (std::fabs(Lk_ - k) > 0.01f) {
        sendSpring(k);
        Lk_ = k;
    }
    // 6. Wheel jolt: a wheel compressing faster than 1.3 m/s (front right, front left, back
    // right, back left), above 5 mph, 0.5 s after an impact jolt; J stays.
    const bool hard = car.wheel(1).rate > 1.3f || car.wheel(0).rate > 1.3f || car.wheel(3).rate > 1.3f || car.wheel(2).rate > 1.3f;
    if (hard && car.speedMph() > 5.0f && now_ - J_ > 0.5) {
        const float r = gameRandomUnit();
        sendJolt(20.0f * 0.01f, r * 360.0f);
    }
}

void ForceFeedback::impact(const phys::Car& car, float total) {
    if (!active()) return;
    if (car.speedMph() > 5.0f && now_ - J_ > 0.5) {
        const float r = gameRandomUnit();
        J_ = now_;
        sendJolt(std::clamp(total * 0.01f, 0.1f, 1.0f) * 1.0f, r * 360.0f);
    }
}

void ForceFeedback::reset() {
    Lf_ = Lh_ = Lw_ = Lk_ = 0.0f;
    J_ = 0.0;
    if (!active()) return;
    stopRumble();
    dev_->start(FfDevice::Friction);
    dev_->start(FfDevice::Spring);
}

void ForceFeedback::popup() {
    if (!dev_) return;
    dev_->stop(FfDevice::Rumble);
    dev_->stop(FfDevice::Spring);
    dev_->stop(FfDevice::Friction);
}

void ForceFeedback::stopAll() {
    if (!dev_) return;
    for (int e = 0; e < 4; ++e)
        if (dev_->playing(FfDevice::Effect(e))) dev_->stop(FfDevice::Effect(e));
}

}  // namespace cc
