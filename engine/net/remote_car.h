// Another player's car as this machine shows and simulates it (spec/game/multiplayer.md 3): a
// full car with the last received inputs, moved toward the sender's predicted position between
// its states (the receiving rules of 3.3 and the per-frame smoothing of 3.4).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "net/car_state.h"
#include "net/transport.h"
#include "physics/car.h"

namespace cc {
namespace net {

// A cubic Hermite curve q(s) = a·s³ + b·s² + c·s + d (3.3 step 12).
struct Hermite {
    vec3 a, b, c, d;
    bool valid = false;
    vec3 at(float s) const;
    static Hermite make(const vec3& P0, const vec3& P1, const vec3& T0, const vec3& T1);
};

// q1 → q2 by the fraction t as unit quaternions, the shorter way (3.4 step 3).
phys::M3 slerpRotation(const phys::M3& from, const phys::M3& to, float t);
// R rotated about the unit axis u by the angle θ (right-handed): every row turned.
phys::M3 rotateRows(const phys::M3& R, const vec3& u, float theta);

class RemoteCar {
public:
    static constexpr int kIdentityBase = 30000;  // collision identity: base + slot ("a player's car")
    static bool isPlayerIdentity(int identity) { return identity >= kIdentityBase && identity < kIdentityBase + 16; }

    PlayerId id = 0;
    int slot = 0;            // 0 … 7
    std::string carName;     // the car it is built as
    std::string sentCar;     // the record's car name (vpcoop is built for an unknown one)
    int paint = 0;
    bool shown = false;      // drawn, simulated and collided with
    phys::Car car;
    bool hasTrailer = false;

    int identity() const { return kIdentityBase + slot; }

    // 3.3: a state from this car's player, received at network clock `now`. Returns false when it
    // was dropped (older than the last one). `damageCleared` is set when its damage visuals must
    // be cleared (step 6).
    bool receive(const CarState& s, double now, bool& damageCleared);
    // 3.4 step 1: once per frame, before the smoothing.
    void countFrame();
    // 3.4 steps 3 and 4 before the physics: the inputs, the smoothing, the room. `findRoom` gives
    // the room of a point (0 outside the city).
    void beforePhysics(float dt, const std::function<int(const vec3&, int)>& findRoom);
    // "car reset" (1.5, 3.4 "Reset event"): a vehicle reset at the sent matrix.
    void resetTo(const phys::M3& R, const vec3& p);
    // A race start or a restart: the car placed and its smoothing state cleared.
    void placed();

    // State (for the tests and the HUD).
    bool haveFirst() const { return haveFirst_; }
    uint32_t lastSeq() const { return lastSeq_; }
    float meanInterval() const;  // T
    const vec3& targetPos() const { return targetP_; }
    const phys::M3& targetRot() const { return targetR_; }
    bool offCourse() const { return offCourse_; }
    const Hermite& curve() const { return curve_; }
    int framesSince() const { return fSince_; }
    int framesTotal() const { return fTotal_; }
    int framesAvg() const { return fAvg_; }
    bool stateThisFrame() const { return gotState_; }
    uint16_t score() const { return score_; }
    float hostClock() const { return hostClock_; }
    uint16_t flags() const { return flags_; }
    bool horn() const { return (flags_ & FlagHorn) != 0; }
    bool siren() const { return (flags_ & FlagSiren) != 0; }
    const phys::CarInputs& inputs() const { return in_; }
    // Siren starts / stops seen by the last receive (the sound builder plays them).
    bool sirenChanged = false;
    bool hornChanged = false;

private:
    bool haveFirst_ = false;
    uint32_t lastSeq_ = 0;
    double lastClock_ = 0;
    float intervals_[10] = {10, 10, 10, 10, 10, 10, 10, 10, 10, 10};
    int intervalNext_ = 0;
    vec3 targetP_;
    phys::M3 targetR_ = phys::M3::identity();
    bool offCourse_ = false;
    Hermite curve_;
    int fSince_ = 0, fTotal_ = 0, fAvg_ = 0;
    bool gotState_ = false;
    uint16_t score_ = 0, flags_ = 0;
    float hostClock_ = 0;
    phys::CarInputs in_;
};

}  // namespace net
}  // namespace cc
