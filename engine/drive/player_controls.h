// Keyboard, mouse and game controllers to the car's controls (spec/engine/controls.md): steering
// smoothing with the speed-sensitive rates of tune/<car>.asNode, the joystick shaping, the dead
// zone, and the press events.
//
// Two ways of reading the devices:
// - A game (a driver's players/<name>.cfg): the driver's binding table (controls.md, "Default
//   binding table"; setBindings) for the control device of the .cfg, which falls back to the
//   keyboard's table while no game controller is connected. Each of the 34 actions has one binding:
//   a key (DirectInput scan code), a mouse button or axis, or a controller button, axis, half axis
//   or hat, read as "The game controller" says. Steering: the keyboard device by the keys
//   (target ±K, smoothing); the gamepad device by its axis through the same smoothing; the
//   joystick and wheel devices by their axis through the shaping with the approach mode ("Joystick
//   and steering wheel"); the mouse device by the pointer's place ("Mouse"). The steer keys only
//   steer the keyboard device. Engine choices kept beside the table: Esc, G or the gamepad's
//   Start (when they have no binding) put the car back on its wheels, Ctrl+B detaches a trailer;
//   a pedal bound to a button takes the button's analog value (triggers); a pedal bound to a whole
//   axis (a wheel's separate pedals) reads 0 at the end it rests on (the platform's rest of that
//   axis) and 1 at the other end, without the dead zone (controls.md, "Pedals", engine choice).
//   Beside the device in use the keyboard (or, beside the keyboard and mouse, the connected
//   controller) works too (companionDevice; the shared game goals): its pedals and buttons add
//   to the device's, and its steer keys steer while held.
// - The viewer's Drive mode (no driver): the engine's own scheme below.
//
// Engine choices of the viewer scheme (controls.md leaves them to us): WASD drive like the arrow keys, so MM2's W
// (wide view), A (shift up), S (waypoint) and D (dash) move to B (wide), E or Page Up (shift
// up; Z, Q or Page Down shift down), F (dash). Backspace resets the car onto its wheels. Gamepad
// (Web "standard" layout): left stick steers, A accelerate, B brake, X horn, Y handbrake, right/
// left triggers analog throttle/brake, LB/RB shift down/up, View/Back = camera, right stick
// click = dash, Start = reset; the d-pad looks around (the binding table's look-around axis, the
// hat on every device). The controller dead zone is the driver's option (default 0.1).
// A joystick or steering wheel (the native build: SDL joysticks without a gamepad mapping) uses
// MM2's joystick / wheel device defaults: X axis steering through the joystick shaping with the
// Joy (or Wheel) values and U, Y axis halves throttle and brake, button 1 handbrake, button 2
// horn, button 3 camera (a wheel: only with more than 5 buttons, which also bring button 4 gauges
// and button 6 mirror; buttons 7 / 8 shift up / down with more than 7), the hat looks around.
#pragma once

#include <cstdint>

#include "drive/drive_tune.h"
#include "physics/car.h"

namespace cc {

struct InputState;

// One binding of the table (controls.md): kind 1 held, 2 analog, 3 press; input device 2 mouse,
// 3 keyboard, 4 game controller; the component (a DirectInput key scan code; mouse 1 left button,
// 2 right button, 10 X axis, 11 Y axis; controller 10 X, 11 Y, 12 Z, 13 U, 14 R, 15 V axes, 16 hat,
// 17 / 18 X left / right half, 19 / 20 Y up / down half, 21 + n button n + 1).
struct ControlBinding {
    uint32_t kind = 0, device = 0, component = 0;
};

// The actions of the binding table (controls.md), in its order.
enum ControlAction {
    ActMapMode, ActFullMap, ActMapZoom, ActMapOrientation, ActGauges, ActSteerAxis, ActSteerLeft, ActSteerRight,
    ActThrottle, ActBrake, ActHandbrake, ActCamera, ActCrossView, ActHorn, ActLookLeft, ActLookRight, ActLookBack,
    ActLookForward, ActWide, ActDash, ActGearbox, ActShiftUp, ActShiftDown, ActReverse, ActNextWaypoint,
    ActPrevWaypoint, ActCdShow, ActCdPlay, ActCdPrevious, ActCdNext, ActMirror, ActLookAxis, ActIcons, ActChat,
    ActionCount
};

// The driver's controls (players/<name>.cfg): the control device (0 mouse, 1 keyboard,
// 2 joystick, 3 gamepad, 4 wheel), the table of all five devices, the dead zone, U and K.
struct ControlBindings {
    int device = 1;
    ControlBinding table[5 * ActionCount];
    float deadZone = 0.1f;
    float steerU = 1.0f;     // 1 / "Steering Sensitivity"
    float keyTarget = 1.0f;  // K
    // The device whose table is read now: the controller devices fall back to the keyboard while
    // no game controller is connected (controls.md).
    int effectiveDevice(const InputState& in) const;
    // The device read beside it (engine choice: the keyboard keeps working while a controller is
    // the device in use, and a connected controller while the keyboard or mouse is: the shared
    // game goals): the keyboard beside the joystick, gamepad and wheel devices; beside the keyboard
    // and mouse devices the connected controller's device by its kind (gamepad, joystick, wheel);
    // -1 = none.
    int companionDevice(const InputState& in) const;
    const ControlBinding& binding(const InputState& in, int action) const {
        return table[effectiveDevice(in) * ActionCount + action];
    }
    const ControlBinding& bindingOn(int device, int action) const { return table[device * ActionCount + action]; }
    // An action's component value this frame ("The game controller", "What is read"; keys and
    // mouse buttons 0 or 1, the mouse axes by the pointer's place) and whether it is held
    // (value above 0.5).
    float value(const InputState& in, int action) const;
    bool held(const InputState& in, int action) const { return value(in, action) > 0.5f; }
    // Whether some action of the table in use takes this key or controller button (the engine's
    // own extra keys yield to them).
    bool usesKey(const InputState& in, uint32_t dik) const;
    bool usesButton(const InputState& in, int button) const;
};

// A component's value ("What is read"): controller X and Y with the dead zone, Z negated, U and V
// 0, R the fourth axis, the hat −1 when centred else 1 up, 0.75 right, 0.5 down, 0.25 left, the
// halves of X and Y, buttons 0 or 1 (`analogButtons`: the button's analog value, our trigger
// extension for pedals); keys 0 or 1; the mouse buttons, and its axes as 2p − 1 of the pointer's
// place p across the window (the middle until the pointer is first seen: engine choice).
float componentValue(const ControlBinding& b, const InputState& in, float deadZone, bool analogButtons = false);

// The press actions of a table: a key or button fires when it goes down; a half axis when it
// passes 0.75, once until it falls back (engine choice, controls.md).
class ActionPresses {
public:
    // The action's binding on `device` (0 … 4) went down this frame.
    bool pressedOn(const ControlBindings& c, int device, const InputState& in, int action);
    // On the device in use or on the one beside it.
    bool pressed(const ControlBindings& c, const InputState& in, int action) {
        const int d = c.effectiveDevice(in), k = c.companionDevice(in);
        const bool a = pressedOn(c, d, in, action);
        const bool b = k >= 0 && pressedOn(c, k, in, action);
        return a || b;
    }
    void clear() {
        for (auto& row : latch_)
            for (bool& l : row) l = false;
    }

private:
    bool latch_[5][ActionCount] = {};
};

struct ControlEvents {
    bool camera = false;     // C: next chase/bumper camera
    bool orbit = false;      // V
    bool dash = false;       // F (MM2: D)
    bool wide = false;       // B (MM2: W), not with Ctrl held
    bool detachTrailer = false;  // Ctrl+B: the hidden trailer detach key (trailer.md 7.2)
    bool transmission = false;  // T
    bool reverse = false;    // R
    bool hud = false;        // H
    bool reset = false;      // G (our own key; MM2 has none, controls.md)
    bool mirror = false;     // Backspace: rear-view mirror on/off (dash.md)
    bool nextWaypoint = false, prevWaypoint = false;  // N / X (MM2: S / X), race modes
    bool leave = false;      // Esc
    int look = 0;            // look direction bits: 1 left, 2 right, 4 back, 8 forward
    bool horn = false;       // Enter or pad X held (horn; siren on police cars)
};

class PlayerControls {
public:
    // Before the first recomputation the rates are DeltaIn 1, DeltaOut 1, Filter 2 (controls.md).
    void setTune(const SteeringTune& t) {
        tune_ = t;
        deltaIn_ = 1.0f;
        deltaOut_ = 1.0f;
        filter_ = 2.0f;
        blend_ = 0.0f;
        holdH_ = approachQ_ = 0.0f;
    }
    void reset() {
        s_ = 0.0f;
        holdH_ = approachQ_ = 0.0f;
    }
    // A game's binding table (null: the viewer's scheme). The pedal axes' resting ends are found
    // again when the controller is next read.
    void setBindings(const ControlBindings* b) {
        haveBindings_ = b != nullptr;
        if (b) bindings_ = *b;
        presses_.clear();
    }
    bool hasBindings() const { return haveBindings_; }
    // Steps 1-2 of controls.md: devices to the player's controls (before the car's control step).
    phys::DriverControls read(const InputState& in, float dt, ControlEvents& ev);
    // Step 6: the speed-sensitive rates for the next frame, from the car speed v.
    void recompute(float v);

    float deltaIn() const { return deltaIn_; }
    float deltaOut() const { return deltaOut_; }
    float filter() const { return filter_; }
    float steeringValue() const { return s_; }

private:
    SteeringTune tune_;
    float s_ = 0.0f;
    float deltaIn_ = 1.0f, deltaOut_ = 1.0f, filter_ = 2.0f;
    float blend_ = 0.0f;  // the Lo/Hi blend of the last recomputation (joystick and wheel values)
    bool haveBindings_ = false;
    ControlBindings bindings_;
    ActionPresses presses_;
    // The joystick / wheel shaping's hold timer H and approach value q (shared by both devices).
    float holdH_ = 0.0f, approachQ_ = 0.0f;
    phys::DriverControls readBound(const InputState& in, float dt, ControlEvents& ev);
    float pedal(const ControlBinding& b, const InputState& in, bool throttle, int device) const;
    // A device's own steering (gamepad: the smoothing toward its axis; joystick, wheel: the shaping;
    // mouse: the pointer).
    float steerByDevice(int device, const InputState& in, float dt);
    // The joystick (wheel = false) or wheel shaping with the approach mode, from the axis value x.
    float joystickSteer(float x, float dt, bool wheel, float U);
    // The mouse device's steering from the pointer's place p (0 … 1 across the window).
    float mouseSteer(float p) const;
};

}  // namespace cc
