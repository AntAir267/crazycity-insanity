#include "render/camera.h"

#include <algorithm>
#include <cmath>

#include "platform/platform.h"

namespace cc {

vec3 OrbitCamera::eye() const {
    float cp = std::cos(pitch);
    return target + vec3(std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp) * distance;
}

mat4 OrbitCamera::view() const { return mat4::lookAt(eye(), target, vec3(0, 1, 0)); }

bool OrbitCamera::update(const InputState& in, int viewportHeight) {
    bool moved = false;
    const float h = float(viewportHeight > 1 ? viewportHeight : 1);
    if (in.buttons[0] && (in.mouseDX != 0 || in.mouseDY != 0)) {
        yaw -= in.mouseDX / h * 3.5f;
        pitch += in.mouseDY / h * 3.0f;
        pitch = std::clamp(pitch, -0.2f, 1.5f);
        moved = true;
    }
    if ((in.buttons[1] || in.buttons[2]) && (in.mouseDX != 0 || in.mouseDY != 0)) {
        vec3 fwd = normalize(target - eye());
        vec3 right = normalize(cross(fwd, vec3(0, 1, 0)));
        vec3 up = cross(right, fwd);
        float s = distance / h * 1.2f;
        target += right * (-in.mouseDX * s) + up * (in.mouseDY * s);
        moved = true;
    }
    if (in.wheel != 0) {
        distance *= std::pow(0.88f, in.wheel);
        distance = std::clamp(distance, minDistance, maxDistance);
        moved = true;
    }
    return moved;
}

vec3 FreeFlyCamera::forward() const {
    float cp = std::cos(pitch);
    return vec3(-std::sin(yaw) * cp, std::sin(pitch), -std::cos(yaw) * cp);
}

vec3 FreeFlyCamera::right() const { return vec3(std::cos(yaw), 0, -std::sin(yaw)); }

mat4 FreeFlyCamera::view() const { return mat4::lookAt(pos, pos + forward(), vec3(0, 1, 0)); }

bool FreeFlyCamera::update(const InputState& in, float dt) {
    bool moved = false;
    if (in.pointerLocked || in.buttons[2]) {
        if (in.mouseDX != 0 || in.mouseDY != 0) {
            yaw -= in.mouseDX * mouseSensitivity;
            pitch -= in.mouseDY * mouseSensitivity;
            pitch = std::clamp(pitch, -1.55f, 1.55f);
            yaw = std::remainder(yaw, 2.0f * kPi);
            moved = true;
        }
    }
    vec3 move;
    if (in.down[KeyW] || in.down[KeyUp]) move += forward();
    if (in.down[KeyS] || in.down[KeyDown]) move -= forward();
    if (in.down[KeyD] || in.down[KeyRight]) move += right();
    if (in.down[KeyA] || in.down[KeyLeft]) move -= right();
    if (in.down[KeyE] || in.down[KeyPageUp] || in.down[KeySpace]) move += vec3(0, 1, 0);
    if (in.down[KeyQ] || in.down[KeyPageDown]) move -= vec3(0, 1, 0);
    if (in.wheel != 0) speed = std::clamp(speed * std::pow(1.15f, in.wheel), 1.0f, 400.0f);
    if (dot(move, move) > 0) {
        float s = speed * (in.down[KeyShift] ? fastFactor : 1.0f);
        pos += normalize(move) * (s * dt);
        moved = true;
    }
    return moved;
}

}  // namespace cc
