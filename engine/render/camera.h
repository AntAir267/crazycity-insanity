// Cameras: an orbit camera for the car turntable and a free-fly camera for cities.
#pragma once

#include "core/math.h"

namespace cc {

struct InputState;

struct OrbitCamera {
    vec3 target;
    float yaw = 0.0f;     // radians about +Y; 0 looks along -Z (from +Z towards the target)
    float pitch = 0.25f;  // radians above the horizontal
    float distance = 7.0f;
    float minDistance = 1.0f, maxDistance = 60.0f;

    vec3 eye() const;
    mat4 view() const;
    // Mouse drag (left = orbit, right/middle = pan) and wheel zoom. Returns true if it moved.
    bool update(const InputState& in, int viewportHeight);
};

struct FreeFlyCamera {
    vec3 pos{0, 20, 0};
    float yaw = 0.0f;    // radians; 0 looks along -Z (north in MM2 cities)
    float pitch = 0.0f;  // radians, + looks up
    float speed = 20.0f;      // metres per second
    float fastFactor = 6.0f;  // with Shift
    float mouseSensitivity = 0.0025f;

    vec3 forward() const;
    vec3 right() const;
    mat4 view() const;
    // WASD move, Q/E down/up, Shift fast, mouse look while the pointer is locked (or the right
    // button is held). Returns true if the camera moved.
    bool update(const InputState& in, float dt);
};

}  // namespace cc
