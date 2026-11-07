#include "drive/player_controls.h"

#include <algorithm>
#include <cmath>

#include "platform/platform.h"

namespace cc {

namespace {
float sgn(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }

// Dead zone 0.1, rescaled so its edge reads 0 and full deflection 1.
float deadZone(float r, float dz) {
    if (std::fabs(r) <= dz) return 0.0f;
    return sgn(r) * (std::fabs(r) - dz) / (1.0f - dz);
}

// The hat's directions (1 up, 2 right, 4 down, 8 left): the merged hat (a gamepad's d-pad
// included natively), and in the browser a standard gamepad's d-pad buttons.
int hatOf(const GamepadState& p) {
    if (!p.connected) return 0;
    if (p.kind != GamepadState::Gamepad) return p.hat;
    return p.hat | (p.button[12] > 0.5f ? 1 : 0) | (p.button[15] > 0.5f ? 2 : 0) | (p.button[13] > 0.5f ? 4 : 0) |
           (p.button[14] > 0.5f ? 8 : 0);
}

// The hat as MM2 reads it: −1 when centred, else (36000 − h)/36000 with h its angle in hundredths
// of a degree clockwise from up.
float hatValue(int bits) {
    const bool up = bits & 1, right = bits & 2, down = bits & 4, left = bits & 8;
    int h;
    if (up && right) h = 4500;
    else if (right && down) h = 13500;
    else if (down && left) h = 22500;
    else if (left && up) h = 31500;
    else if (up) h = 0;
    else if (right) h = 9000;
    else if (down) h = 18000;
    else if (left) h = 27000;
    else return -1.0f;
    return float(36000 - h) / 36000.0f;
}

// Look directions (1 left, 2 right, 4 back, 8 forward) from a look-around value 0 … 1 (the hat's
// convention: 1 up = forward, 0.75 right, 0.5 down = back, 0.25 left; 0 = none).
int lookOf(float v) {
    if (!(v > 0.0f)) return 0;
    static const int dirs[8] = {8, 8 | 1, 1, 4 | 1, 4, 4 | 2, 2, 8 | 2};
    const int k = int(std::lround(double(v) * 8.0)) & 7;  // 1 → 0 (forward), 0.875 → 7, …
    return dirs[(8 - k) & 7];
}

constexpr uint32_t kDevMouse = 2, kDevKeyboard = 3, kDevController = 4;

bool isHalf(uint32_t c) { return c >= 17 && c <= 20; }
bool isWholeAxis(uint32_t c) { return c >= 10 && c <= 15; }
bool isButton(uint32_t c) { return c >= 21 && c < 21u + uint32_t(GamepadState::kButtons); }

// The controller's raw axis of a whole-axis component (Z negated, U and V none), for the pedals'
// resting ends; −1 = none.
int pedalAxis(uint32_t c) {
    switch (c) {
        case 10: return 0;
        case 11: return 1;
        case 12: return 2;
        case 14: return 3;
        default: return -1;
    }
}
}  // namespace

int ControlBindings::effectiveDevice(const InputState& in) const {
    const int d = device < 0 || device > 4 ? 1 : device;
    return d >= 2 && !in.pad.connected ? 1 : d;
}

int ControlBindings::companionDevice(const InputState& in) const {
    const int d = effectiveDevice(in);
    if (d >= 2) return 1;
    if (!in.pad.connected) return -1;
    return in.pad.kind == GamepadState::Wheel ? 4 : (in.pad.kind == GamepadState::Joystick ? 2 : 3);
}

float componentValue(const ControlBinding& b, const InputState& in, float dz, bool analogButtons) {
    if (b.kind == 0) return 0.0f;
    if (b.device == kDevKeyboard) return in.hasFocus && b.component < 256 && in.key[b.component] ? 1.0f : 0.0f;
    if (b.device == kDevMouse) {
        // The pointer's place across the window (the middle until it is first seen).
        const float px = in.mouseKnown && in.windowW > 0 ? in.mouseX * (1.0f / in.windowW) : 0.5f;
        const float py = in.mouseKnown && in.windowH > 0 ? in.mouseY * (1.0f / in.windowH) : 0.5f;
        switch (b.component) {
            case 1: return in.buttons[0] ? 1.0f : 0.0f;
            case 2: return in.buttons[2] ? 1.0f : 0.0f;
            case 10: return (px + px) - 1.0f;
            case 11: return (py + py) - 1.0f;
            default: return 0.0f;
        }
    }
    if (b.device != kDevController || !in.pad.connected) return 0.0f;
    const GamepadState& p = in.pad;
    const float X = deadZone(p.axis[0], dz), Y = deadZone(p.axis[1], dz);
    switch (b.component) {
        case 10: return X;
        case 11: return Y;
        case 12: return -p.axis[2];
        case 13: return 0.0f;  // MM2 never reads a U axis
        case 14: return p.axis[3];
        case 15: return 0.0f;  // nor a V axis
        case 16: return hatValue(hatOf(p));
        case 17: return X < 0.0f ? -X : 0.0f;
        case 18: return X > 0.0f ? X : 0.0f;
        case 19: return Y < 0.0f ? -Y : 0.0f;
        case 20: return Y > 0.0f ? Y : 0.0f;
        default: break;
    }
    if (isButton(b.component)) {
        const float v = p.button[b.component - 21];
        return analogButtons ? v : (v > 0.5f ? 1.0f : 0.0f);
    }
    return 0.0f;
}

float ControlBindings::value(const InputState& in, int action) const { return componentValue(binding(in, action), in, deadZone); }

bool ControlBindings::usesKey(const InputState& in, uint32_t dik) const {
    for (int d : {effectiveDevice(in), companionDevice(in)}) {
        if (d < 0) continue;
        for (int a = 0; a < ActionCount; ++a) {
            const ControlBinding& b = table[d * ActionCount + a];
            if (b.kind != 0 && b.device == kDevKeyboard && b.component == dik) return true;
        }
    }
    return false;
}

bool ControlBindings::usesButton(const InputState& in, int button) const {
    for (int d : {effectiveDevice(in), companionDevice(in)}) {
        if (d < 0) continue;
        for (int a = 0; a < ActionCount; ++a) {
            const ControlBinding& b = table[d * ActionCount + a];
            if (b.kind != 0 && b.device == kDevController && b.component == uint32_t(21 + button)) return true;
        }
    }
    return false;
}

bool ActionPresses::pressedOn(const ControlBindings& c, int device, const InputState& in, int action) {
    if (device < 0 || device > 4) return false;
    const ControlBinding& b = c.bindingOn(device, action);
    if (b.kind == 0) return false;
    if (b.device == kDevKeyboard) return in.hasFocus && b.component < 256 && in.keyPressed[b.component];
    if (b.device == kDevMouse) return (b.component == 1 && in.clicked[0]) || (b.component == 2 && in.clicked[2]);
    if (b.device != kDevController || !in.pad.connected) return false;
    if (isButton(b.component)) return in.pad.pressed[b.component - 21];
    if (isHalf(b.component)) {
        // Engine choice: the bound half passing 0.75, once until it falls back.
        const float v = componentValue(b, in, c.deadZone);
        bool& l = latch_[device][action];
        if (v > 0.75f && !l) {
            l = true;
            return true;
        }
        if (v <= 0.75f) l = false;
    }
    return false;
}

void PlayerControls::recompute(float v) {
    const SteeringTune& t = tune_;
    if (t.speedSensitive == 0) {
        deltaIn_ = t.deltaInLo;
        deltaOut_ = t.deltaOutLo;
        filter_ = t.filterLo;
        blend_ = 0.0f;
        return;
    }
    if (t.speedSensitive == 1) {
        deltaIn_ = t.deltaInHi;
        deltaOut_ = t.deltaOutHi;
        filter_ = t.filterHi;
        blend_ = 1.0f;
        return;
    }
    // MM2's blend: t = clamp(v, Low, Hi) / (Hi - Low), not (v - Low) / (Hi - Low).
    float c = v < t.speedBaseLow ? t.speedBaseLow : (v > t.speedBaseHi ? t.speedBaseHi : v);
    const float k = c / (t.speedBaseHi - t.speedBaseLow);
    deltaIn_ = t.deltaInLo + (t.deltaInHi - t.deltaInLo) * k;
    deltaOut_ = t.deltaOutLo + (t.deltaOutHi - t.deltaOutLo) * k;
    filter_ = t.filterLo + (t.filterHi - t.filterLo) * k;
    blend_ = k;
}

phys::DriverControls PlayerControls::read(const InputState& in, float dt, ControlEvents& ev) {
    if (haveBindings_) return readBound(in, dt, ev);
    phys::DriverControls c;
    const bool kb = in.hasFocus;
    auto held = [&](Key k) { return kb && in.down[k]; };
    auto pressed = [&](Key k) { return kb && in.pressed[k]; };
    const GamepadState& pad = in.pad;
    const bool gamepad = pad.connected && pad.kind == GamepadState::Gamepad;
    const bool stick = pad.connected && pad.kind != GamepadState::Gamepad;  // a joystick or wheel
    const bool wheel = stick && pad.kind == GamepadState::Wheel;
    const float dz = pad.deadZone;

    // Pedals: exactly 0 or 1 from keys and pad buttons; triggers are analog (our extension).
    float accel = (held(KeyUp) || held(KeyW)) ? 1.0f : 0.0f;
    float brake = (held(KeyDown) || held(KeyS)) ? 1.0f : 0.0f;
    float hand = held(KeySpace) ? 1.0f : 0.0f;
    float padX = 0.0f;
    if (gamepad) {
        if (pad.button[0] > 0.5f) accel = 1.0f;
        if (pad.button[1] > 0.5f) brake = 1.0f;
        if (pad.button[3] > 0.5f) hand = 1.0f;
        const float rt = deadZone(pad.button[7], 0.05f), lt = deadZone(pad.button[6], 0.05f);
        if (rt > accel) accel = rt;
        if (lt > brake) brake = lt;
        padX = deadZone(pad.axis[0], dz);
    } else if (stick) {
        // Y axis: the forward (up, negative) half throttle, the back half brake, each with the
        // dead zone; button 1 handbrake.
        const float y = pad.axis[1];
        const float up = y < 0.0f ? deadZone(-y, dz) : 0.0f, back = y > 0.0f ? deadZone(y, dz) : 0.0f;
        if (up > accel) accel = up;
        if (back > brake) brake = back;
        if (pad.button[0] > 0.5f) hand = 1.0f;
        padX = deadZone(pad.axis[0], dz);
    }

    // Steering target: keyboard (left wins), else the stick.
    float T = 0.0f;
    const bool left = held(KeyLeft) || held(KeyA);
    const bool right = held(KeyRight) || held(KeyD);
    const float K = 1.0f;
    if (left) T = -K;
    else if (right) T = K;
    else T = padX;

    if (stick && !left && !right) {
        // Joystick / wheel shaping instead of the smoothing (controls.md).
        c.steer = joystickSteer(padX, dt, wheel, pad.steerU);
        s_ = 0.0f;
    } else {
        // Smoothing: out toward full lock with DeltaOut, otherwise DeltaIn (sign(0) quirk kept).
        const float rate = (std::fabs(s_) <= std::fabs(T) && sgn(s_) == sgn(T)) ? deltaOut_ : deltaIn_;
        const float stepLen = rate * dt;
        if (s_ < T) s_ = (s_ + stepLen > T) ? T : s_ + stepLen;
        else if (s_ > T) s_ = (s_ - stepLen < T) ? T : s_ - stepLen;
        c.steer = sgn(s_) * std::pow(std::fabs(s_), filter_);
    }
    c.accelerate = accel;
    c.brake = brake;
    c.handbrake = hand;

    // The joystick and wheel buttons of controls.md's binding table (button n = pressed[n - 1]).
    const bool many = pad.buttonCount > 5, more = pad.buttonCount > 7;
    const bool joyCamera = stick && (!wheel || many) && pad.pressed[2];
    const bool wheelUp = wheel && more && pad.pressed[6], wheelDown = wheel && more && pad.pressed[7];

    // Press events.
    if (pressed(KeyE) || pressed(KeyPageUp) || (gamepad && pad.pressed[5]) || wheelUp) c.shift = 1;
    if (pressed(KeyZ) || pressed(KeyQ) || pressed(KeyPageDown) || (gamepad && pad.pressed[4]) || wheelDown) c.shift = -1;
    ev.camera = pressed(KeyC) || (gamepad && pad.pressed[8]) || joyCamera;
    ev.orbit = pressed(KeyV);
    ev.dash = pressed(KeyF) || (gamepad && pad.pressed[11]);
    ev.wide = pressed(KeyB) && !held(KeyCtrl);
    ev.detachTrailer = pressed(KeyB) && held(KeyCtrl);
    ev.transmission = pressed(KeyT);
    ev.reverse = pressed(KeyR);
    ev.hud = pressed(KeyH) || (wheel && many && pad.pressed[3]);
    ev.reset = pressed(KeyG) || (gamepad && pad.pressed[9]);
    ev.mirror = pressed(KeyBackspace) || (wheel && many && pad.pressed[5]);
    // Race modes (controls.md, S / X): next and previous waypoint; engine keys N and X (S drives).
    ev.nextWaypoint = pressed(KeyN);
    ev.prevWaypoint = pressed(KeyX);
    ev.horn = held(KeyEnter) || (gamepad && pad.button[2] > 0.5f) || (stick && pad.button[1] > 0.5f);
    ev.leave = pressed(KeyEscape);
    // Look around: the numpad keys, the gamepad's d-pad or the joystick's hat.
    const int hat = gamepad ? ((pad.button[12] > 0.5f ? 1 : 0) | (pad.button[15] > 0.5f ? 2 : 0) |
                               (pad.button[13] > 0.5f ? 4 : 0) | (pad.button[14] > 0.5f ? 8 : 0))
                            : (stick ? pad.hat : 0);
    const bool lookL = held(KeyNum4) || (hat & 8), lookR = held(KeyNum6) || (hat & 2);
    const bool lookB = held(KeyNum2) || (hat & 4), lookF = held(KeyNum8) || (hat & 1);
    int look = 0;
    if (lookL) look |= 1;
    else if (lookR) look |= 2;  // left wins over right
    if (lookB) look |= 4;
    else if (lookF) look |= 8;
    ev.look = look;
    return c;
}

}  // namespace cc

namespace cc {

// The joystick and wheel shaping with the approach mode (controls.md, "Joystick and steering
// wheel"): S = U × the sensitivity blend, F, Out and In blended, A and the approach switch as
// read; the hold timer H and the approach value q are shared by both devices.
float PlayerControls::joystickSteer(float x, float dt, bool wheel, float U) {
    const SteeringTune& t = tune_;
    auto blend = [&](float lo, float hi) { return (hi - lo) * blend_ + lo; };
    const float S = U * (wheel ? blend(t.wheelSensitivityLow, t.wheelSensitivityHi) : blend(t.joySensitivityLow, t.joySensitivityHi));
    const float F = wheel ? blend(t.wheelSteerFilterLow, t.wheelSteerFilterHi) : blend(t.joySteerFilterLow, t.joySteerFilterHi);
    const float out = wheel ? blend(t.wheelApproachOutLo, t.wheelApproachOutHi) : blend(t.joyApproachOutLo, t.joyApproachOutHi);
    const float inRate = wheel ? blend(t.wheelApproachInLo, t.wheelApproachInHi) : blend(t.joyApproachInLo, t.joyApproachInHi);
    const float A = wheel ? t.wheelAppApp : t.joyAppApp;
    const bool approach = (wheel ? t.wheelApp : t.joyApp) != 0;
    // 1. The hold timer.
    if (std::fabs(x) > 0.99f) holdH_ = holdH_ + dt;
    else holdH_ = 0.0f;
    // 2. The target.
    float y;
    if (S <= 1.0f || std::fabs(x) <= 0.99f) y = S != 0.0f ? std::clamp(x / S, -1.0f, 1.0f) : 0.0f;
    else y = sgn(x);
    float steer;
    if (!approach || holdH_ == 0.0f) {
        // 3. Normal: ((|y|·S)^F · (1/S)) · sign(y).
        approachQ_ = y;
        const float invS = S != 0.0f ? 1.0f / S : 0.0f;
        const float pw = float(std::pow(double(std::fabs(y) * S), double(F)));
        steer = (pw * invS) * sgn(y);
    } else {
        // 4. Approach: q ramps toward y; steering = |q|^F · sign(q).
        const float rate = (std::fabs(y) < std::fabs(approachQ_) || sgn(approachQ_) != sgn(y)) ? A * holdH_ + inRate : A * holdH_ + out;
        if (approachQ_ < y) {
            approachQ_ = rate * dt + approachQ_;
            if (approachQ_ > y) approachQ_ = y;
        } else if (approachQ_ > y) {
            approachQ_ = approachQ_ - rate * dt;
            if (approachQ_ < y) approachQ_ = y;
        }
        steer = float(std::pow(double(std::fabs(approachQ_)), double(F))) * sgn(approachQ_);
    }
    // 5. Clamp.
    return std::clamp(steer, -1.0f, 1.0f);
}

// The mouse device (controls.md, "Mouse"): x = ((p + p) − 1) / M with M = U × the
// MouseSensitivity blend, steering = clamp(|x|^G · sign(x)); no dead zone and no smoothing.
float PlayerControls::mouseSteer(float p) const {
    const SteeringTune& t = tune_;
    const float M = bindings_.steerU * ((t.mouseSensitivityHi - t.mouseSensitivityLow) * blend_ + t.mouseSensitivityLow);
    const float G = (t.mouseSteerFilterHi - t.mouseSteerFilterLow) * blend_ + t.mouseSteerFilterLow;
    const float x = M != 0.0f ? ((p + p) - 1.0f) / M : 0.0f;
    return std::clamp(float(std::pow(double(std::fabs(x)), double(G))) * sgn(x), -1.0f, 1.0f);
}

// A pedal 0 … 1 from its binding ("Axis-bound driving actions", "Pedals"): keys and mouse buttons
// 0 or 1, controller buttons their analog value (our trigger extension), the throttle max(value, 0)
// and the brake clamp(value, 0, 1) of an axis; an axis bound to the throttle on the gamepad device
// is never read. Engine choice: a whole axis that rests at one end (a wheel's separate pedals, the
// platform's rest of the axis) reads 0 there and 1 at the other end, without the dead zone.
float PlayerControls::pedal(const ControlBinding& b, const InputState& in, bool throttle, int device) const {
    if (b.kind == 0) return 0.0f;
    if (b.device == kDevController && in.pad.connected) {
        const uint32_t c = b.component;
        if (isButton(c)) return std::clamp(componentValue(b, in, bindings_.deadZone, true), 0.0f, 1.0f);
        if (throttle && device == 3) return 0.0f;
        const int ra = isWholeAxis(c) ? pedalAxis(c) : -1;
        if (ra >= 0) {
            const float r = in.pad.rest[ra], a = in.pad.axis[ra];
            if (r > 0.5f) return std::clamp((r - a) / (r + 1.0f), 0.0f, 1.0f);
            if (r < -0.5f) return std::clamp((a - r) / (1.0f - r), 0.0f, 1.0f);
        }
    }
    const float v = componentValue(b, in, bindings_.deadZone);
    return throttle ? std::max(v, 0.0f) : std::clamp(v, 0.0f, 1.0f);
}

float PlayerControls::steerByDevice(int device, const InputState& in, float dt) {
    const ControlBindings& B = bindings_;
    const ControlBinding& ax = B.bindingOn(device, ActSteerAxis);
    if (device == 3) {
        // Gamepad: the steering axis as the target of the keyboard's smoothing.
        const float T = componentValue(ax, in, B.deadZone);
        const float rate = (std::fabs(s_) <= std::fabs(T) && sgn(s_) == sgn(T)) ? deltaOut_ : deltaIn_;
        const float stepLen = rate * dt;
        if (s_ < T) s_ = (s_ + stepLen > T) ? T : s_ + stepLen;
        else if (s_ > T) s_ = (s_ - stepLen < T) ? T : s_ - stepLen;
        return sgn(s_) * std::pow(std::fabs(s_), filter_);
    }
    s_ = 0.0f;
    if (device == 0) {
        // Mouse: the pointer's place (a binding of its X axis), no smoothing.
        const float v = ax.kind != 0 ? componentValue(ax, in, 0.0f) : 0.0f;  // 2p − 1
        return ax.kind != 0 && ax.device == kDevMouse ? mouseSteer((v + 1.0f) * 0.5f) : std::clamp(v, -1.0f, 1.0f);
    }
    // Joystick and wheel: the shaping with the approach mode.
    return joystickSteer(componentValue(ax, in, B.deadZone), dt, device == 4, B.steerU);
}

phys::DriverControls PlayerControls::readBound(const InputState& in, float dt, ControlEvents& ev) {
    phys::DriverControls c;
    const ControlBindings& B = bindings_;
    const GamepadState& pad = in.pad;
    const int dev = B.effectiveDevice(in), comp = B.companionDevice(in);
    const bool kb = in.hasFocus;
    auto valueOn = [&](int d, int a) { return d >= 0 ? componentValue(B.bindingOn(d, a), in, B.deadZone) : 0.0f; };
    auto held = [&](int a) { return valueOn(dev, a) > 0.5f || valueOn(comp, a) > 0.5f; };
    auto pressed = [&](int a) { return presses_.pressed(B, in, a); };
    auto pedalOn = [&](int d, int a, bool throttle) { return d >= 0 ? pedal(B.bindingOn(d, a), in, throttle, d) : 0.0f; };

    // Pedals and handbrake: the device's and the one beside it, whichever gives more.
    c.accelerate = std::max(pedalOn(dev, ActThrottle, true), pedalOn(comp, ActThrottle, true));
    c.brake = std::max(pedalOn(dev, ActBrake, false), pedalOn(comp, ActBrake, false));
    c.handbrake = std::max(std::clamp(valueOn(dev, ActHandbrake), 0.0f, 1.0f), std::clamp(valueOn(comp, ActHandbrake), 0.0f, 1.0f));

    // Steering: a controller beside the keyboard or mouse device while its steering axis is pushed
    // and no steer key is held; else the steer keys while held (the keyboard's, or the mouse
    // device's own); else the device's own steering (the keyboard's smoothing back to the centre).
    const int keys = dev == 0 ? 0 : 1;  // the mouse device's own table has the steer keys too
    const bool left = valueOn(keys, ActSteerLeft) > 0.5f, right = valueOn(keys, ActSteerRight) > 0.5f;
    const bool beside = comp >= 2 && valueOn(comp, ActSteerAxis) != 0.0f;  // a controller beside the keyboard or mouse
    if (beside && !left && !right) {
        c.steer = steerByDevice(comp, in, dt);
    } else if (left || right || dev == 1) {
        const float K = B.keyTarget > 0.0f ? B.keyTarget : 1.0f;
        const float T = left ? -K : (right ? K : 0.0f);
        const float rate = (std::fabs(s_) <= std::fabs(T) && sgn(s_) == sgn(T)) ? deltaOut_ : deltaIn_;
        const float stepLen = rate * dt;
        if (s_ < T) s_ = (s_ + stepLen > T) ? T : s_ + stepLen;
        else if (s_ > T) s_ = (s_ - stepLen < T) ? T : s_ - stepLen;
        c.steer = sgn(s_) * std::pow(std::fabs(s_), filter_);
    } else {
        c.steer = steerByDevice(dev, in, dt);
    }

    // Press events of the tables.
    if (pressed(ActShiftUp)) c.shift = 1;
    if (pressed(ActShiftDown)) c.shift = -1;
    ev.camera = pressed(ActCamera);
    ev.orbit = pressed(ActCrossView);
    ev.dash = pressed(ActDash);
    ev.wide = pressed(ActWide);
    ev.transmission = pressed(ActGearbox);
    ev.reverse = pressed(ActReverse);
    ev.hud = pressed(ActGauges);
    ev.mirror = pressed(ActMirror);
    ev.nextWaypoint = pressed(ActNextWaypoint);
    ev.prevWaypoint = pressed(ActPrevWaypoint);
    ev.horn = held(ActHorn);
    ev.leave = kb && in.pressed[KeyEscape];
    // The engine's own keys beside the tables, unless they use them: G or the gamepad's Start
    // put the car back on its wheels, Ctrl+B detaches a trailer.
    constexpr uint32_t kG = 0x22, kB = 0x30;
    ev.reset = (kb && in.keyPressed[kG] && !B.usesKey(in, kG)) ||
               (pad.connected && pad.kind == GamepadState::Gamepad && pad.pressed[9] && !B.usesButton(in, 9));
    ev.detachTrailer = kb && in.keyPressed[kB] && (in.key[0x1D] || in.key[0x9D]) && !B.usesKey(in, kB);
    // Looking around: the look actions and the look-around axis (clamp(value, 0, 1), the hat's
    // directions).
    const int around = lookOf(std::clamp(valueOn(dev, ActLookAxis), 0.0f, 1.0f)) | lookOf(std::clamp(valueOn(comp, ActLookAxis), 0.0f, 1.0f));
    const bool lookL = held(ActLookLeft) || (around & 1), lookR = held(ActLookRight) || (around & 2);
    const bool lookB = held(ActLookBack) || (around & 4), lookF = held(ActLookForward) || (around & 8);
    int look = 0;
    if (lookL) look |= 1;
    else if (lookR) look |= 2;  // left wins over right
    if (lookB) look |= 4;
    else if (lookF) look |= 8;
    ev.look = look;
    return c;
}

}  // namespace cc
