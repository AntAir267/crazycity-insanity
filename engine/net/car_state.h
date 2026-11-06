// The car state each machine sends about its own car (spec/game/multiplayer.md 3.1), in MM2's
// quantisation: the receiver's behaviour depends on the rounding, so it is kept.
#pragma once

#include <cstdint>

#include "core/math.h"
#include "physics/pmath.h"

namespace cc {
namespace phys {
class Car;
}
namespace net {

class Writer;
class Reader;

// Flags (3.1; the bit values are MM2's).
enum CarStateFlags : uint16_t {
    FlagHorn = 0x02,         // horn sounding
    FlagReverse = 0x04,      // gear is reverse
    FlagSiren = 0x08,        // siren on (police car)
    FlagHandbrake = 0x20,    // handbrake input > 0.8
    FlagManual = 0x40,       // manual gearbox (the gear field is valid)
    FlagNotDrivable = 0x80,  // car not drivable
    FlagReset = 0x100,       // the first state after a race restart
};

struct CarState {
    float hostClock = 0;        // the host's mode timer value; 0 from non-hosts
    int8_t steer = 0;           // trunc(s × 127)
    uint8_t throttle = 0;       // trunc(t × 255)
    uint8_t brake = 0;          // trunc(b × 255)
    int8_t gear = 0;            // gear index (0 reverse, 1 neutral, 2 first, …)
    float a = 0, b = 0, c = 0;  // orientation angles of the body rows
    vec3 pos;                   // body position (centre of mass)
    float accelLen = 0;         // the "acceleration slot": MM2 puts the velocity here
    int8_t accelDir[3] = {0, 0, 0};
    float velLen = 0;
    int8_t velDir[3] = {0, 0, 0};
    float angLen = 0;
    int8_t angDir[3] = {0, 0, 0};
    float damage = 0;
    uint16_t score = 0;
    uint16_t flags = 0;
    uint32_t seq = 0;
};

// The sender's side: everything but host clock, score, flags and sequence is read from the car.
CarState captureCarState(const phys::Car& car);
// |v| and trunc(v / |v| × 127) per axis (0 and 0 for a zero vector).
void encodeDirection(const vec3& v, float& len, int8_t dir[3]);
// L × byte / 127 per axis.
vec3 decodeDirection(float len, const int8_t dir[3]);
// The three angles of a rotation (rows X, Y, Z) and the matrix back from them (3.3 step 2).
void anglesOf(const phys::M3& R, float& a, float& b, float& c);
phys::M3 matrixFromAngles(float a, float b, float c);

void writeCarState(Writer& w, const CarState& s);
bool readCarState(Reader& r, CarState& s);

}  // namespace net
}  // namespace cc
