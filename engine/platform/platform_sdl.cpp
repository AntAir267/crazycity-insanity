#include "platform/platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#else
#include "render/gl_native.h"
#endif

#include "core/log.h"

namespace cc {

namespace {

PlatformConfig g_cfg;

int mapKey(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_W: return KeyW;
        case SDL_SCANCODE_A: return KeyA;
        case SDL_SCANCODE_S: return KeyS;
        case SDL_SCANCODE_D: return KeyD;
        case SDL_SCANCODE_Q: return KeyQ;
        case SDL_SCANCODE_E: return KeyE;
        case SDL_SCANCODE_R: return KeyR;
        case SDL_SCANCODE_F: return KeyF;
        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: return KeyShift;
        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL: return KeyCtrl;
        case SDL_SCANCODE_SPACE: return KeySpace;
        case SDL_SCANCODE_ESCAPE: return KeyEscape;
        case SDL_SCANCODE_UP: return KeyUp;
        case SDL_SCANCODE_DOWN: return KeyDown;
        case SDL_SCANCODE_LEFT: return KeyLeft;
        case SDL_SCANCODE_RIGHT: return KeyRight;
        case SDL_SCANCODE_PAGEUP: return KeyPageUp;
        case SDL_SCANCODE_PAGEDOWN: return KeyPageDown;
        case SDL_SCANCODE_C: return KeyC;
        case SDL_SCANCODE_V: return KeyV;
        case SDL_SCANCODE_B: return KeyB;
        case SDL_SCANCODE_T: return KeyT;
        case SDL_SCANCODE_Z: return KeyZ;
        case SDL_SCANCODE_H: return KeyH;
        case SDL_SCANCODE_G: return KeyG;
        case SDL_SCANCODE_X: return KeyX;
        case SDL_SCANCODE_BACKSPACE: return KeyBackspace;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return KeyEnter;
        case SDL_SCANCODE_KP_2: return KeyNum2;
        case SDL_SCANCODE_KP_4: return KeyNum4;
        case SDL_SCANCODE_KP_6: return KeyNum6;
        case SDL_SCANCODE_KP_8: return KeyNum8;
        case SDL_SCANCODE_TAB: return KeyTab;
        case SDL_SCANCODE_M: return KeyM;
        case SDL_SCANCODE_N: return KeyN;
        case SDL_SCANCODE_K: return KeyK;
        case SDL_SCANCODE_O: return KeyO;
        case SDL_SCANCODE_I: return KeyI;
        case SDL_SCANCODE_F2: return KeyF2;
        case SDL_SCANCODE_F4: return KeyF4;
        case SDL_SCANCODE_F1: return KeyF1;
        default: return -1;
    }
}

ui::Key uiKey(SDL_Keycode c, SDL_Scancode sc) {
    if (c >= 'a' && c <= 'z') return ui::Key(int(ui::Key::A) + int(c - 'a'));
    if (c >= '0' && c <= '9') return ui::Key(int(ui::Key::D0) + int(c - '0'));
    switch (sc) {
        case SDL_SCANCODE_ESCAPE: return ui::Key::Escape;
        case SDL_SCANCODE_RETURN:
        case SDL_SCANCODE_KP_ENTER: return ui::Key::Enter;
        case SDL_SCANCODE_TAB: return ui::Key::Tab;
        case SDL_SCANCODE_BACKSPACE: return ui::Key::Backspace;
        case SDL_SCANCODE_DELETE: return ui::Key::Delete;
        case SDL_SCANCODE_LEFT: return ui::Key::Left;
        case SDL_SCANCODE_RIGHT: return ui::Key::Right;
        case SDL_SCANCODE_UP: return ui::Key::Up;
        case SDL_SCANCODE_DOWN: return ui::Key::Down;
        case SDL_SCANCODE_HOME: return ui::Key::Home;
        case SDL_SCANCODE_END: return ui::Key::End;
        case SDL_SCANCODE_PAGEUP: return ui::Key::PageUp;
        case SDL_SCANCODE_PAGEDOWN: return ui::Key::PageDown;
        case SDL_SCANCODE_SPACE: return ui::Key::Space;
        default: break;
    }
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return ui::Key(int(ui::Key::F1) + int(sc - SDL_SCANCODE_F1));
    return ui::Key::Other;
}

uint8_t uiMods(SDL_Keymod m) {
    uint8_t r = 0;
    if (m & SDL_KMOD_SHIFT) r |= ui::ModShift;
    if (m & SDL_KMOD_CTRL) r |= ui::ModCtrl;
    if (m & SDL_KMOD_ALT) r |= ui::ModAlt;
    if (m & SDL_KMOD_GUI) r |= ui::ModMeta;
    return r;
}

// The DirectInput scan code of a key (the binding table's keyboard components), 0 = none.
int dikOf(SDL_Scancode sc) {
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) {
        static const unsigned char letters[26] = {0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                                  0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C};
        return letters[sc - SDL_SCANCODE_A];
    }
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return 0x02 + (sc - SDL_SCANCODE_1);  // 1 … 9, 0
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F10) return 0x3B + (sc - SDL_SCANCODE_F1);
    switch (sc) {
        case SDL_SCANCODE_ESCAPE: return 0x01;
        case SDL_SCANCODE_MINUS: return 0x0C;
        case SDL_SCANCODE_EQUALS: return 0x0D;
        case SDL_SCANCODE_BACKSPACE: return 0x0E;
        case SDL_SCANCODE_TAB: return 0x0F;
        case SDL_SCANCODE_LEFTBRACKET: return 0x1A;
        case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;
        case SDL_SCANCODE_RETURN: return 0x1C;
        case SDL_SCANCODE_LCTRL: return 0x1D;
        case SDL_SCANCODE_SEMICOLON: return 0x27;
        case SDL_SCANCODE_APOSTROPHE: return 0x28;
        case SDL_SCANCODE_GRAVE: return 0x29;
        case SDL_SCANCODE_LSHIFT: return 0x2A;
        case SDL_SCANCODE_BACKSLASH: return 0x2B;
        case SDL_SCANCODE_COMMA: return 0x33;
        case SDL_SCANCODE_PERIOD: return 0x34;
        case SDL_SCANCODE_SLASH: return 0x35;
        case SDL_SCANCODE_RSHIFT: return 0x36;
        case SDL_SCANCODE_KP_MULTIPLY: return 0x37;
        case SDL_SCANCODE_LALT: return 0x38;
        case SDL_SCANCODE_SPACE: return 0x39;
        case SDL_SCANCODE_CAPSLOCK: return 0x3A;
        case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;
        case SDL_SCANCODE_SCROLLLOCK: return 0x46;
        case SDL_SCANCODE_KP_7: return 0x47;
        case SDL_SCANCODE_KP_8: return 0x48;
        case SDL_SCANCODE_KP_9: return 0x49;
        case SDL_SCANCODE_KP_MINUS: return 0x4A;
        case SDL_SCANCODE_KP_4: return 0x4B;
        case SDL_SCANCODE_KP_5: return 0x4C;
        case SDL_SCANCODE_KP_6: return 0x4D;
        case SDL_SCANCODE_KP_PLUS: return 0x4E;
        case SDL_SCANCODE_KP_1: return 0x4F;
        case SDL_SCANCODE_KP_2: return 0x50;
        case SDL_SCANCODE_KP_3: return 0x51;
        case SDL_SCANCODE_KP_0: return 0x52;
        case SDL_SCANCODE_KP_PERIOD: return 0x53;
        case SDL_SCANCODE_NONUSBACKSLASH: return 0x56;
        case SDL_SCANCODE_F11: return 0x57;
        case SDL_SCANCODE_F12: return 0x58;
        case SDL_SCANCODE_KP_ENTER: return 0x9C;
        case SDL_SCANCODE_RCTRL: return 0x9D;
        case SDL_SCANCODE_KP_DIVIDE: return 0xB5;
        case SDL_SCANCODE_PRINTSCREEN: return 0xB7;
        case SDL_SCANCODE_RALT: return 0xB8;
        case SDL_SCANCODE_PAUSE: return 0xC5;
        case SDL_SCANCODE_HOME: return 0xC7;
        case SDL_SCANCODE_UP: return 0xC8;
        case SDL_SCANCODE_PAGEUP: return 0xC9;
        case SDL_SCANCODE_LEFT: return 0xCB;
        case SDL_SCANCODE_RIGHT: return 0xCD;
        case SDL_SCANCODE_END: return 0xCF;
        case SDL_SCANCODE_DOWN: return 0xD0;
        case SDL_SCANCODE_PAGEDOWN: return 0xD1;
        case SDL_SCANCODE_INSERT: return 0xD2;
        case SDL_SCANCODE_DELETE: return 0xD3;
        case SDL_SCANCODE_LGUI: return 0xDB;
        case SDL_SCANCODE_RGUI: return 0xDC;
        case SDL_SCANCODE_APPLICATION: return 0xDD;
        default: return 0;
    }
}

int mapButton(Uint8 b) {
    switch (b) {
        case SDL_BUTTON_LEFT: return 0;
        case SDL_BUTTON_MIDDLE: return 1;
        case SDL_BUTTON_RIGHT: return 2;
        default: return -1;
    }
}

#ifndef __EMSCRIPTEN__
// ---------------------------------------------------------------------------------------------
// Native game controllers. Every controller works, whenever it is plugged in (the shared game
// goals): every joystick SDL reports is opened, at start-up and when it arrives later, and closed
// when it goes; SDL gamepads are read in the standard layout, joysticks and wheels as they are.
// The game reads them merged into one controller (MM2 has a single one, controls.md "The game
// controller"; a handheld can show several devices for one pad, some of them silent): a button
// is held when it is held on any, each axis comes from the controller pushing it furthest from
// where it rests, the hat from any. Force feedback (controls.md, "Force feedback") goes to every
// controller SDL reports as haptic: each gets MM2's four effects (friction, spring, rumble, jolt)
// when it is opened, with its automatic centring off.

struct Controller {
    SDL_Gamepad* pad = nullptr;
    SDL_Joystick* joy = nullptr;  // the gamepad's joystick, or the plain joystick / wheel
    SDL_JoystickID id = 0;
    int kind = GamepadState::Gamepad;
    std::string name;
    int tag = 0;  // the order it was opened in (log lines)
    SDL_Haptic* haptic = nullptr;
    SDL_HapticEffectID effect[4] = {-1, -1, -1, -1};  // friction, spring, rumble, jolt
    bool fakeHaptic = false;  // tests: force-feedback commands recorded, no device
    // This frame's reading, and where each axis rests (SDL's initial state; a gamepad's sticks 0,
    // its triggers -1 as axes 4 / 5).
    int axes = 0, buttons = 0;
    float axis[GamepadState::kAxes] = {};
    float rest[GamepadState::kAxes] = {};
    bool restKnown[GamepadState::kAxes] = {};
    float button[GamepadState::kButtons] = {};
    int hat = 0;
    Uint64 active = 0;  // ms of its last input beyond rest (the merged controller's kind)
};
std::vector<std::unique_ptr<Controller>> g_ctls;
int g_ctlSerial = 0;
// The self-test's virtual controllers (`haptic`: force feedback recorded instead of played).
struct TestDevice {
    SDL_JoystickID id = 0;
    SDL_Joystick* joy = nullptr;
    bool haptic = false;
};
std::vector<TestDevice> g_testDevices;
bool g_ffPlaying[4] = {false, false, false, false};
Uint64 g_joltEnds = 0;                 // ms: the 0.1 s jolt ends by itself
std::vector<std::string> g_hapticLog;  // tests

Controller* findController(SDL_JoystickID id) {
    for (auto& c : g_ctls)
        if (c->id == id) return c.get();
    return nullptr;
}

void releaseController(Controller& c) {
    if (c.haptic) {
        SDL_StopHapticEffects(c.haptic);
        for (SDL_HapticEffectID id : c.effect)
            if (id >= 0) SDL_DestroyHapticEffect(c.haptic, id);
        SDL_CloseHaptic(c.haptic);
        c.haptic = nullptr;
    }
    if (c.pad) SDL_CloseGamepad(c.pad);
    else if (c.joy) SDL_CloseJoystick(c.joy);
    c.pad = nullptr;
    c.joy = nullptr;
}

void closeController(SDL_JoystickID id) {
    for (size_t i = 0; i < g_ctls.size(); ++i)
        if (g_ctls[i]->id == id) {
            logInfo("game controller removed: %s", g_ctls[i]->name.c_str());
            releaseController(*g_ctls[i]);
            g_ctls.erase(g_ctls.begin() + long(i));
            return;
        }
}

void closeAllControllers() {
    for (auto& c : g_ctls) releaseController(*c);
    g_ctls.clear();
}

// The effect templates (controls.md, "Driving SDL's haptic interface").
SDL_HapticDirection steeringAxis() {
    SDL_HapticDirection d;
    SDL_zero(d);
    d.type = SDL_HAPTIC_STEERING_AXIS;
    return d;
}

SDL_HapticEffect conditionEffect(Uint16 type, Sint16 coefficient) {
    SDL_HapticEffect e;
    SDL_zero(e);
    e.type = type;
    e.condition.direction = steeringAxis();
    e.condition.length = SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
    e.condition.right_coeff[0] = e.condition.left_coeff[0] = coefficient;
    return e;
}

Uint16 periodicType(SDL_Haptic* h) {
    const unsigned f = h ? SDL_GetHapticFeatures(h) : 0;
    return (f & SDL_HAPTIC_SQUARE) ? SDL_HAPTIC_SQUARE : SDL_HAPTIC_SINE;
}

SDL_HapticEffect rumbleEffect(SDL_Haptic* h, Uint16 periodMs, Sint16 magnitude) {
    SDL_HapticEffect e;
    SDL_zero(e);
    e.type = periodicType(h);
    e.periodic.direction = steeringAxis();
    e.periodic.length = SDL_HAPTIC_INFINITY;
    e.periodic.period = periodMs;
    e.periodic.magnitude = magnitude;
    return e;
}

SDL_HapticEffect joltEffect(SDL_Haptic* h, Sint16 magnitude, Sint32 direction) {
    SDL_HapticEffect e;
    SDL_zero(e);
    e.type = periodicType(h);
    e.periodic.direction.type = SDL_HAPTIC_POLAR;
    e.periodic.direction.dir[0] = direction;
    e.periodic.length = 100;
    e.periodic.period = 2000;
    e.periodic.magnitude = magnitude;
    return e;
}

Sint16 force(int c) { return Sint16(std::clamp(c, 0, 10000) * 32767 / 10000); }

void openHaptic(Controller& c) {
    c.haptic = SDL_OpenHapticFromJoystick(c.joy);
    if (!c.haptic) return;
    SDL_Haptic* h = c.haptic;
    const unsigned f = SDL_GetHapticFeatures(h);
    if (f & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(h, 0);  // as MM2, whatever the option says
    SDL_HapticEffect e;
    if (f & SDL_HAPTIC_FRICTION) {
        e = conditionEffect(SDL_HAPTIC_FRICTION, 0);
        c.effect[0] = SDL_CreateHapticEffect(h, &e);
    }
    if (f & SDL_HAPTIC_SPRING) {
        e = conditionEffect(SDL_HAPTIC_SPRING, 0);
        c.effect[1] = SDL_CreateHapticEffect(h, &e);
    }
    if (f & (SDL_HAPTIC_SQUARE | SDL_HAPTIC_SINE)) {
        e = rumbleEffect(h, 5000, force(2000));
        c.effect[2] = SDL_CreateHapticEffect(h, &e);
        e = joltEffect(h, force(10000), 0);
        c.effect[3] = SDL_CreateHapticEffect(h, &e);
    }
    // A controller that arrives while effects play joins them.
    for (int k = 0; k < 3; ++k)
        if (g_ffPlaying[k] && c.effect[k] >= 0) SDL_RunHapticEffect(h, c.effect[k], 1);
}

float axisValue(Sint16 v) {
    float f = float(v) / 32767.0f;
    return f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
}

bool openController(SDL_JoystickID id) {
    if (findController(id)) return true;
    if (g_cfg.testControllersOnly) {
        bool test = false;
        for (const TestDevice& t : g_testDevices) test = test || t.id == id;
        if (!test) return false;  // a real controller is left alone in tests
    }
    auto c = std::make_unique<Controller>();
    if (SDL_IsGamepad(id)) {
        c->pad = SDL_OpenGamepad(id);
        if (!c->pad) return false;
        c->joy = SDL_GetGamepadJoystick(c->pad);
        c->kind = GamepadState::Gamepad;
        const char* n = SDL_GetGamepadName(c->pad);
        c->name = n ? n : "gamepad";
        c->axes = 6;
        c->buttons = 17;
        c->rest[4] = c->rest[5] = -1.0f;  // the triggers as axes rest at -1
        for (bool& k : c->restKnown) k = true;
    } else {
        c->joy = SDL_OpenJoystick(id);
        if (!c->joy) return false;
        c->kind = SDL_GetJoystickType(c->joy) == SDL_JOYSTICK_TYPE_WHEEL ? GamepadState::Wheel : GamepadState::Joystick;
        const char* n = SDL_GetJoystickName(c->joy);
        c->name = n ? n : "joystick";
        c->axes = std::min(SDL_GetNumJoystickAxes(c->joy), int(GamepadState::kAxes));
        c->buttons = std::min(SDL_GetNumJoystickButtons(c->joy), int(GamepadState::kButtons));
        for (int a = 0; a < c->axes; ++a) {
            Sint16 v = 0;
            c->restKnown[a] = SDL_GetJoystickAxisInitialState(c->joy, a, &v);
            c->rest[a] = axisValue(c->restKnown[a] ? v : SDL_GetJoystickAxis(c->joy, a));
        }
    }
    c->id = id;
    c->tag = ++g_ctlSerial;
    for (const TestDevice& t : g_testDevices)
        if (t.id == id) c->fakeHaptic = t.haptic;
    if (SDL_IsJoystickHaptic(c->joy)) openHaptic(*c);
    const char* kinds[] = {"gamepad", "joystick", "wheel"};
    logInfo("game controller %d: %s (%s, %d buttons, %d axes%s)", c->tag, c->name.c_str(), kinds[c->kind],
            SDL_GetNumJoystickButtons(c->joy), SDL_GetNumJoystickAxes(c->joy), c->haptic ? ", force feedback" : "");
    g_ctls.push_back(std::move(c));
    return true;
}

// Every controller SDL lists now.
void openAllControllers() {
    int n = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&n);
    for (int i = 0; ids && i < n; ++i) openController(ids[i]);
    SDL_free(ids);
}

void readController(Controller& c) {
    for (float& v : c.axis) v = 0.0f;
    for (float& v : c.button) v = 0.0f;
    c.hat = 0;
    if (!c.joy || !SDL_JoystickConnected(c.joy)) return;
    if (c.pad) {
        SDL_Gamepad* p = c.pad;
        c.axis[0] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX));
        c.axis[1] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY));
        c.axis[2] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX));
        c.axis[3] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY));
        static const SDL_GamepadButton order[17] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
            SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            SDL_GAMEPAD_BUTTON_GUIDE};
        for (int b = 0; b < 17; ++b)
            c.button[b] = order[b] == SDL_GAMEPAD_BUTTON_INVALID ? 0.0f : (SDL_GetGamepadButton(p, order[b]) ? 1.0f : 0.0f);
        // The triggers are analog (0 … 32767): buttons 6 / 7, and axes 4 / 5 from -1 to +1.
        c.button[6] = std::max(0.0f, axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
        c.button[7] = std::max(0.0f, axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
        c.axis[4] = c.button[6] * 2.0f - 1.0f;
        c.axis[5] = c.button[7] * 2.0f - 1.0f;
        c.hat = (c.button[12] > 0.5f ? 1 : 0) | (c.button[15] > 0.5f ? 2 : 0) | (c.button[13] > 0.5f ? 4 : 0) |
                (c.button[14] > 0.5f ? 8 : 0);
    } else {
        SDL_Joystick* j = c.joy;
        for (int a = 0; a < c.axes; ++a) {
            c.axis[a] = axisValue(SDL_GetJoystickAxis(j, a));
            // Where it rests: SDL's initial state of the axis once the device has reported it.
            Sint16 v = 0;
            if (!c.restKnown[a] && SDL_GetJoystickAxisInitialState(j, a, &v)) {
                c.rest[a] = axisValue(v);
                c.restKnown[a] = true;
            }
        }
        for (int b = 0; b < c.buttons; ++b) c.button[b] = SDL_GetJoystickButton(j, b) ? 1.0f : 0.0f;
        if (SDL_GetNumJoystickHats(j) > 0) {
            const Uint8 h = SDL_GetJoystickHat(j, 0);
            c.hat = (h & SDL_HAT_UP ? 1 : 0) | (h & SDL_HAT_RIGHT ? 2 : 0) | (h & SDL_HAT_DOWN ? 4 : 0) | (h & SDL_HAT_LEFT ? 8 : 0);
        }
    }
    bool moved = c.hat != 0;
    for (int b = 0; b < c.buttons && !moved; ++b) moved = c.button[b] > 0.5f;
    for (int a = 0; a < c.axes && !moved; ++a) moved = std::fabs(c.axis[a] - c.rest[a]) > 0.5f;
    if (moved) c.active = SDL_GetTicks() + 1;
}

// The merged controller: buttons held on any, each axis from the controller pushing it furthest
// from its rest (a tie: the one resting furthest from the middle, a pedal), the hat from any.
void pollControllers(GamepadState& g) {
    constexpr int NA = GamepadState::kAxes, NB = GamepadState::kButtons;
    for (float& b : g.button) b = 0.0f;
    for (float& a : g.axis) a = 0.0f;
    for (float& r : g.rest) r = 0.0f;
    g.hat = 0;
    g.buttonCount = g.axisCount = 0;
    g.feedback = false;
    g.controllers = 0;
    const Controller* kindFrom = nullptr;
    for (auto& cp : g_ctls) {
        Controller& c = *cp;
        readController(c);
        if (!c.joy || !SDL_JoystickConnected(c.joy)) continue;
        ++g.controllers;
        if (!kindFrom || c.active > kindFrom->active) kindFrom = &c;
        for (int b = 0; b < NB; ++b) g.button[b] = std::max(g.button[b], c.button[b]);
        g.hat |= c.hat;
        g.buttonCount = std::max(g.buttonCount, c.buttons);
        g.axisCount = std::max(g.axisCount, c.axes);
        g.feedback = g.feedback || c.haptic != nullptr || c.fakeHaptic;
    }
    g.connected = g.controllers > 0;
    if (!g.connected) return;
    g.kind = kindFrom ? kindFrom->kind : GamepadState::Gamepad;
    for (int a = 0; a < NA; ++a) {
        const Controller* best = nullptr;
        float bestPush = -1.0f;
        for (auto& cp : g_ctls) {
            const Controller& c = *cp;
            if (a >= c.axes || !c.joy || !SDL_JoystickConnected(c.joy)) continue;
            const float push = std::fabs(c.axis[a] - c.rest[a]);
            if (!best || push > bestPush + 1e-4f || (std::fabs(push - bestPush) <= 1e-4f && std::fabs(c.rest[a]) > std::fabs(best->rest[a]))) {
                best = &c;
                bestPush = push;
            }
        }
        if (!best) continue;
        g.axis[a] = best->axis[a];
        // Where the axis rests: the controller resting furthest from the middle (a pedal).
        float r = 0.0f;
        for (auto& cp : g_ctls)
            if (a < cp->axes && std::fabs(cp->rest[a]) > std::fabs(r)) r = cp->rest[a];
        g.rest[a] = r;
    }
}

// ---------------------------------------------------------------------------------------------
// Native audio output and window bookkeeping.

SDL_AudioStream* g_audio = nullptr;
std::vector<float> g_audioBuf;
SDL_Window* g_window = nullptr;
SDL_GLContext g_context = nullptr;



Platform::WindowState g_win;
bool g_winChanged = false;
#endif  // !__EMSCRIPTEN__

void pollGamepad(GamepadState& g) {
    constexpr int NB = GamepadState::kButtons;
    bool before[NB];
    for (int i = 0; i < NB; ++i) before[i] = g.button[i] > 0.5f;
    for (bool& p : g.pressed) p = false;
#ifdef __EMSCRIPTEN__
    // Every connected gamepad, merged as natively: buttons held on any, each axis from the pad
    // pushing it furthest from its rest (standard layout: sticks 0, the triggers' axes -1).
    constexpr int NA = GamepadState::kAxes;
    for (float& b : g.button) b = 0.0f;
    for (float& a : g.axis) a = 0.0f;
    for (float& r : g.rest) r = 0.0f;
    g.connected = false;
    g.kind = GamepadState::Gamepad;
    g.hat = 0;
    g.buttonCount = g.axisCount = 0;
    g.controllers = 0;
    float push[NA];
    for (float& p : push) p = -1.0f;
    if (emscripten_sample_gamepad_data() == EMSCRIPTEN_RESULT_SUCCESS) {
        const int n = emscripten_get_num_gamepads();
        for (int i = 0; i < n; ++i) {
            EmscriptenGamepadEvent st;
            if (emscripten_get_gamepad_status(i, &st) != EMSCRIPTEN_RESULT_SUCCESS || !st.connected) continue;
            g.connected = true;
            ++g.controllers;
            float ax[NA] = {}, rest[NA] = {};
            for (int a = 0; a < NA && a < st.numAxes; ++a) ax[a] = float(st.axis[a]);
            for (int b = 0; b < NB && b < st.numButtons; ++b) g.button[b] = std::max(g.button[b], float(st.analogButton[b]));
            const int axes = st.numAxes <= 4 ? 6 : std::min(st.numAxes, NA);
            if (st.numAxes <= 4) {
                ax[4] = float(st.analogButton[6]) * 2.0f - 1.0f;
                ax[5] = float(st.analogButton[7]) * 2.0f - 1.0f;
                rest[4] = rest[5] = -1.0f;
            }
            for (int a = 0; a < axes; ++a) {
                const float p = std::fabs(ax[a] - rest[a]);
                if (p > push[a] + 1e-4f) {
                    push[a] = p;
                    g.axis[a] = ax[a];
                }
                if (std::fabs(rest[a]) > std::fabs(g.rest[a])) g.rest[a] = rest[a];
            }
            g.buttonCount = std::max(g.buttonCount, int(st.numButtons));
            g.axisCount = std::max(g.axisCount, axes);
        }
    }
#else
    pollControllers(g);
#endif
    for (int i = 0; i < NB; ++i) g.pressed[i] = !before[i] && g.button[i] > 0.5f;
}

}  // namespace

void Platform::configure(const PlatformConfig& c) { g_cfg = c; }

bool Platform::init(const char* title) {
#ifdef __EMSCRIPTEN__
    // Keyboard events only while the canvas has focus, so the side panel's inputs keep working.
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
    const SDL_InitFlags flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
#else
    if (g_cfg.offscreen) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");  // no window has the focus
        SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");  // Ctrl+C and kill end a test at once
    }
    SDL_SetHint(SDL_HINT_APP_ID, "crazycity-insanity");
    SDL_SetHint(SDL_HINT_APP_NAME, title);
    SDL_InitFlags flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
    if (g_cfg.controllers) flags |= SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC;
#endif
    if (!SDL_Init(flags)) {
        logError("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    int msaa = 4;
#ifndef __EMSCRIPTEN__
    msaa = g_cfg.msaa;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
#endif
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, msaa > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, msaa > 0 ? msaa : 0);

    SDL_WindowFlags wflags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    int w = 1280, h = 720;
#ifndef __EMSCRIPTEN__
    w = std::max(320, g_cfg.width);
    h = std::max(200, g_cfg.height);
    wflags |= SDL_WINDOW_HIDDEN;  // shown once placed (or never, offscreen)
#endif
    SDL_Window* win = SDL_CreateWindow(title, w, h, wflags);
    if (!win) {
        logError("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    if (!ctx && msaa > 0) {
        // Retry without multisampling (a new window: the pixel format is chosen with it).
        SDL_DestroyWindow(win);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        win = SDL_CreateWindow(title, w, h, wflags);
        ctx = win ? SDL_GL_CreateContext(win) : nullptr;
    }
    if (!ctx) {
        logError("cannot create a WebGL 2 / OpenGL ES 3 context: %s", SDL_GetError());
        if (win) SDL_DestroyWindow(win);
        return false;
    }
    SDL_GL_MakeCurrent(win, ctx);
    window_ = win;
    context_ = ctx;
#ifdef __EMSCRIPTEN__
    SDL_GL_SetSwapInterval(1);
#else
    g_window = win;
    g_context = ctx;
    std::string missing;
    if (!loadGlFunctions(reinterpret_cast<void* (*)(const char*)>(SDL_GL_GetProcAddress), &missing)) {
        logError("the OpenGL ES 3.0 driver lacks: %s", missing.c_str());
        return false;
    }
    const char* glv = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    logInfo("video %s, %s on %s", SDL_GetCurrentVideoDriver(), glv ? glv : "?", glr ? glr : "?");
    if (g_cfg.offscreen || !g_cfg.vsync) SDL_GL_SetSwapInterval(0);
    else if (!SDL_GL_SetSwapInterval(-1)) SDL_GL_SetSwapInterval(1);  // adaptive vsync if offered
    g_win.w = w;
    g_win.h = h;
    if (!g_cfg.offscreen) {
        if (g_cfg.x >= 0 && g_cfg.y >= 0) SDL_SetWindowPosition(win, g_cfg.x, g_cfg.y);
        SDL_ShowWindow(win);
        if (g_cfg.maximized) SDL_MaximizeWindow(win);
        if (g_cfg.fullscreen) SDL_SetWindowFullscreen(win, true);
        SDL_GetWindowPosition(win, &g_win.x, &g_win.y);
        g_win.fullscreen = g_cfg.fullscreen;
        g_win.maximized = g_cfg.maximized;
    }
    if (g_cfg.controllers) {
        // Every controller connected now; the binding defaults' button count is the most any
        // of them has (engine choice for MM2's "controller found at start-up", controls.md).
        openAllControllers();
        for (auto& c : g_ctls) startupButtons_ = std::max(startupButtons_, SDL_GetNumJoystickButtons(c->joy));
    }
#endif
    return true;
}

void Platform::shutdown() {
#ifndef __EMSCRIPTEN__
    if (g_audio) {
        SDL_DestroyAudioStream(g_audio);
        g_audio = nullptr;
    }
    closeAllControllers();
    if (window_) SDL_HideWindow(static_cast<SDL_Window*>(window_));
#endif
}

void Platform::finalQuit() {
#ifndef __EMSCRIPTEN__
    static bool done = false;
    if (done) return;
    done = true;
    if (g_audio) {
        SDL_DestroyAudioStream(g_audio);
        g_audio = nullptr;
    }
    closeAllControllers();
    if (g_context) {
        SDL_GL_MakeCurrent(g_window, g_context);
        glFinish();
        unloadGlFunctions();  // nothing reaches the driver after this
        SDL_GL_MakeCurrent(g_window, nullptr);
        SDL_GL_DestroyContext(g_context);
    }
    if (g_window) SDL_DestroyWindow(g_window);
    g_context = nullptr;
    g_window = nullptr;
    SDL_Quit();
#endif
}

bool Platform::pollEvents(InputState& in) {
    in.beginFrame();
    // SDL's window coordinates to device pixels of the drawable (natively points times the
    // pixel density; in the browser SDL's idea of the window against the canvas' pixels).
    int winW = 1, winH = 1, dw = 1, dh = 1;
    SDL_GetWindowSize(static_cast<SDL_Window*>(window_), &winW, &winH);
    drawableSize(dw, dh);
    const float kx = float(dw) / float(winW > 0 ? winW : 1), ky = float(dh) / float(winH > 0 ? winH : 1);
    SDL_Event e;
    bool keepGoing = true;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_EVENT_QUIT:
                keepGoing = false;
                quit_ = true;
                break;
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
#ifndef __EMSCRIPTEN__
                // F11 and Alt+Enter switch full screen (the native window only).
                if (e.type == SDL_EVENT_KEY_DOWN &&
                    (e.key.scancode == SDL_SCANCODE_F11 ||
                     ((e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) && (e.key.mod & SDL_KMOD_ALT)))) {
                    if (!e.key.repeat) toggleFullscreen();
                    break;
                }
#endif
                {
                    ui::InputEvent ue;
                    ue.type = e.type == SDL_EVENT_KEY_DOWN ? ui::InputEvent::KeyDown : ui::InputEvent::KeyUp;
                    ue.key = uiKey(e.key.key, e.key.scancode);
                    ue.mods = uiMods(e.key.mod);
                    ue.repeat = e.key.repeat;
                    in.events.push_back(ue);
                }
                const bool d = e.type == SDL_EVENT_KEY_DOWN;
                const int dk = dikOf(e.key.scancode);
                if (dk > 0) {
                    if (d && !in.key[dk]) in.keyPressed[dk] = true;
                    in.key[dk] = d;
                }
                int k = mapKey(e.key.scancode);
                if (k < 0) break;
                if (d && !in.down[k]) in.pressed[k] = true;
                in.down[k] = d;
                break;
            }
            case SDL_EVENT_TEXT_INPUT: {
                ui::InputEvent ue;
                ue.type = ui::InputEvent::Text;
                ue.text = e.text.text ? e.text.text : "";
                in.events.push_back(ue);
                break;
            }
            case SDL_EVENT_MOUSE_MOTION: {
                in.mouseDX += e.motion.xrel;
                in.mouseDY += e.motion.yrel;
                in.mouseX = e.motion.x;
                in.mouseY = e.motion.y;
                in.mouseKnown = true;
                ui::InputEvent ue;
                ue.type = e.motion.which == SDL_TOUCH_MOUSEID ? ui::InputEvent::TouchMove : ui::InputEvent::MouseMove;
                ue.x = e.motion.x * kx;
                ue.y = e.motion.y * ky;
                if (!in.pointerLocked) in.events.push_back(ue);
                break;
            }
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const bool d = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                {
                    ui::InputEvent ue;
                    const bool touch = e.button.which == SDL_TOUCH_MOUSEID;
                    ue.type = touch ? (d ? ui::InputEvent::TouchDown : ui::InputEvent::TouchUp)
                                    : (d ? ui::InputEvent::MouseDown : ui::InputEvent::MouseUp);
                    ue.button = e.button.button == SDL_BUTTON_RIGHT ? 2 : e.button.button == SDL_BUTTON_MIDDLE ? 1 : 0;
                    ue.x = e.button.x * kx;
                    ue.y = e.button.y * ky;
                    if (!in.pointerLocked) in.events.push_back(ue);
                }
                int b = mapButton(e.button.button);
                if (b < 0) break;
                in.mouseX = e.button.x;
                in.mouseY = e.button.y;
                in.mouseKnown = true;
                if (d) in.clicked[b] = true;
                in.buttons[b] = d;
                break;
            }
            case SDL_EVENT_MOUSE_WHEEL: {
                in.wheel += e.wheel.y;
                ui::InputEvent ue;
                ue.type = ui::InputEvent::Wheel;
                ue.dy = e.wheel.y;
                ue.dx = e.wheel.x;
                in.events.push_back(ue);
                break;
            }
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                for (bool& d : in.down) d = false;
                for (bool& d : in.key) d = false;
                hapticStopAll();  // engine choice (controls.md, "Reset, pause and pop-ups")
                for (bool& b : in.buttons) b = false;
                break;
#ifndef __EMSCRIPTEN__
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: {
                SDL_Window* win = static_cast<SDL_Window*>(window_);
                const SDL_WindowFlags f = SDL_GetWindowFlags(win);
                const bool fs = (f & SDL_WINDOW_FULLSCREEN) != 0, mx = (f & SDL_WINDOW_MAXIMIZED) != 0;
                if (!fs && !mx && !(f & SDL_WINDOW_MINIMIZED)) {
                    // The size and place of the normal window (not of full screen or maximised).
                    if (e.type == SDL_EVENT_WINDOW_RESIZED) {
                        g_win.w = e.window.data1;
                        g_win.h = e.window.data2;
                    } else if (e.type == SDL_EVENT_WINDOW_MOVED) {
                        g_win.x = e.window.data1;
                        g_win.y = e.window.data2;
                    }
                }
                g_win.fullscreen = fs;
                g_win.maximized = mx;
                g_winChanged = true;
                break;
            }
            case SDL_EVENT_GAMEPAD_ADDED:
            case SDL_EVENT_JOYSTICK_ADDED:
                // A controller plugged in (or present at start): it joins the merged one at once.
                if (g_cfg.controllers) openController(e.type == SDL_EVENT_GAMEPAD_ADDED ? e.gdevice.which : e.jdevice.which);
                break;
            case SDL_EVENT_JOYSTICK_REMOVED:
                closeController(e.jdevice.which);
                break;
#endif
            default: break;
        }
    }
    in.pointerLocked = pointerLocked();
#ifdef __EMSCRIPTEN__
    // The page may lock the pointer itself (from its click handler); keep SDL's relative mode in
    // step so motion events carry movement deltas while locked.
    SDL_Window* win = static_cast<SDL_Window*>(window_);
    if (in.pointerLocked && !SDL_GetWindowRelativeMouseMode(win)) SDL_SetWindowRelativeMouseMode(win, true);
    else if (!in.pointerLocked && SDL_GetWindowRelativeMouseMode(win)) SDL_SetWindowRelativeMouseMode(win, false);
    in.hasFocus = EM_ASM_INT({
        var c = Module['canvas'];
        return (c && document.activeElement === c) ? 1 : 0;
    }) != 0;
    if (!in.hasFocus) {
        // Keys released while the focus was elsewhere never reach us.
        for (bool& d : in.down) d = false;
        for (bool& d : in.key) d = false;
    }
#else
    in.hasFocus = true;
#endif
    {
        int ww = 0, wh = 0;
        SDL_GetWindowSize(static_cast<SDL_Window*>(window_), &ww, &wh);
        in.windowW = float(ww);
        in.windowH = float(wh);
    }
    in.pad.deadZone = deadZone_;
    in.pad.steerU = steerU_;
    pollGamepad(in.pad);
    lastInput_ = &in;
    return keepGoing;
}

void Platform::drawableSize(int& w, int& h) {
#ifdef __EMSCRIPTEN__
    double cssW = 0, cssH = 0;
    emscripten_get_element_css_size("#canvas", &cssW, &cssH);
    double dpr = emscripten_get_device_pixel_ratio();
    int want_w = int(cssW * dpr + 0.5), want_h = int(cssH * dpr + 0.5);
    if (want_w < 1) want_w = 1;
    if (want_h < 1) want_h = 1;
    int cur_w = 0, cur_h = 0;
    emscripten_get_canvas_element_size("#canvas", &cur_w, &cur_h);
    if (cur_w != want_w || cur_h != want_h) emscripten_set_canvas_element_size("#canvas", want_w, want_h);
    w = want_w;
    h = want_h;
#else
    SDL_GetWindowSizeInPixels(static_cast<SDL_Window*>(window_), &w, &h);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
#endif
}

float Platform::pixelRatio() const {
#ifdef __EMSCRIPTEN__
    return float(emscripten_get_device_pixel_ratio());
#else
    const float s = window_ ? SDL_GetWindowDisplayScale(static_cast<SDL_Window*>(window_)) : 1.0f;
    return s > 0.0f ? s : 1.0f;
#endif
}

void Platform::setTextInput(bool on) {
    SDL_Window* win = static_cast<SDL_Window*>(window_);
    if (!win) return;
    if (on) SDL_StartTextInput(win);
    else SDL_StopTextInput(win);
}

void Platform::setClipboard(const std::string& text) { SDL_SetClipboardText(text.c_str()); }

std::string Platform::clipboard() const {
    char* t = SDL_GetClipboardText();
    std::string s = t ? t : "";
    if (t) SDL_free(t);
    return s;
}

void Platform::swap() {
    if (beforeSwap_) {
        int w = 1, h = 1;
        drawableSize(w, h);
        beforeSwap_(w, h);
    }
    SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
}

double Platform::timeSeconds() const { return double(SDL_GetPerformanceCounter()) / double(SDL_GetPerformanceFrequency()); }

void Platform::setPointerLock(bool on) {
    SDL_Window* win = static_cast<SDL_Window*>(window_);
#ifdef __EMSCRIPTEN__
    if (on) {
        // SDL ignores the request if it still thinks the pointer is locked (after Esc).
        SDL_SetWindowRelativeMouseMode(win, false);
        SDL_SetWindowRelativeMouseMode(win, true);
    } else {
        SDL_SetWindowRelativeMouseMode(win, false);
        emscripten_exit_pointerlock();
    }
#else
    if (g_cfg.offscreen) return;
    SDL_SetWindowRelativeMouseMode(win, on);
#endif
}

bool Platform::pointerLocked() const {
#ifdef __EMSCRIPTEN__
    EmscriptenPointerlockChangeEvent st;
    if (emscripten_get_pointerlock_status(&st) == EMSCRIPTEN_RESULT_SUCCESS) return st.isActive != 0;
    return false;
#else
    return window_ && SDL_GetWindowRelativeMouseMode(static_cast<SDL_Window*>(window_));
#endif
}

// ---------------------------------------------------------------------------------------------
// Native only.

bool Platform::offscreen() const {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return g_cfg.offscreen;
#endif
}

bool Platform::minimized() const {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return window_ && !g_cfg.offscreen && (SDL_GetWindowFlags(static_cast<SDL_Window*>(window_)) & SDL_WINDOW_MINIMIZED) != 0;
#endif
}

bool Platform::waitWhileMinimized(int ms) {
#ifdef __EMSCRIPTEN__
    (void)ms;
    return true;
#else
    SDL_Event e;
    if (SDL_WaitEventTimeout(&e, ms)) {
        if (e.type == SDL_EVENT_QUIT) {
            quit_ = true;
            return false;
        }
        if (e.type == SDL_EVENT_WINDOW_RESTORED || e.type == SDL_EVENT_WINDOW_SHOWN) g_winChanged = true;
    }
    return true;
#endif
}

void Platform::toggleFullscreen() {
#ifndef __EMSCRIPTEN__
    if (!window_ || g_cfg.offscreen) return;
    SDL_Window* win = static_cast<SDL_Window*>(window_);
    const bool fs = (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) != 0;
    SDL_SetWindowFullscreenMode(win, nullptr);  // borderless, at the desktop's resolution
    SDL_SetWindowFullscreen(win, !fs);
#endif
}

Platform::WindowState Platform::windowState() const {
#ifdef __EMSCRIPTEN__
    return WindowState();
#else
    return g_win;
#endif
}

bool Platform::windowStateChanged() {
#ifdef __EMSCRIPTEN__
    return false;
#else
    const bool c = g_winChanged;
    g_winChanged = false;
    return c;
#endif
}

bool Platform::readPixels(int w, int h, unsigned char* rgba) const {
#ifdef __EMSCRIPTEN__
    (void)w; (void)h; (void)rgba;
    return false;
#else
    while (glGetError() != GL_NO_ERROR) {
    }
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    return glGetError() == GL_NO_ERROR;
#endif
}

bool Platform::openAudio(int rate) {
#ifdef __EMSCRIPTEN__
    (void)rate;
    return false;  // the page's audio.js pulls cc_audio_render
#else
    if (g_audio) return true;
    if (!g_cfg.audio) return false;
    if (!g_cfg.audioDriver.empty()) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, g_cfg.audioDriver.c_str());
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        logWarn("no audio: %s", SDL_GetError());
        return false;
    }
    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = rate;
    g_audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!g_audio) {
        logWarn("no audio output: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(g_audio);
    SDL_AudioSpec dev;
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(g_audio), &dev, &frames))
        logInfo("audio %s: %d Hz stereo from the engine, device %d Hz, %d channels, %d-frame buffer", SDL_GetCurrentAudioDriver(),
                rate, dev.freq, dev.channels, frames);
    return true;
#endif
}

int Platform::feedAudio(const std::function<void(float*, int)>& pull, int chunk, int target) {
#ifdef __EMSCRIPTEN__
    (void)pull; (void)chunk; (void)target;
    return 0;
#else
    if (!g_audio || chunk <= 0) return 0;
    int queued = SDL_GetAudioStreamQueued(g_audio) / int(2 * sizeof(float));
    int pulled = 0;
    if (g_audioBuf.size() < size_t(chunk) * 2) g_audioBuf.resize(size_t(chunk) * 2);
    while (queued < target) {
        pull(g_audioBuf.data(), chunk);
        SDL_PutAudioStreamData(g_audio, g_audioBuf.data(), int(chunk * 2 * sizeof(float)));
        queued += chunk;
        pulled += chunk;
    }
    return pulled;
#endif
}

void Platform::pauseAudio(bool paused) {
#ifndef __EMSCRIPTEN__
    if (!g_audio) return;
    if (paused) SDL_PauseAudioStreamDevice(g_audio);
    else SDL_ResumeAudioStreamDevice(g_audio);
#else
    (void)paused;
#endif
}

void Platform::setAudioGain(float gain) {
#ifndef __EMSCRIPTEN__
    if (g_audio) SDL_SetAudioStreamGain(g_audio, gain < 0.0f ? 0.0f : gain);
#else
    (void)gain;
#endif
}

int Platform::audioQueued() const {
#ifdef __EMSCRIPTEN__
    return 0;
#else
    return g_audio ? SDL_GetAudioStreamQueued(g_audio) / int(2 * sizeof(float)) : 0;
#endif
}

bool Platform::audioOpen() const {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return g_audio != nullptr;
#endif
}

std::string Platform::audioDriver() const {
#ifdef __EMSCRIPTEN__
    return std::string();
#else
    const char* d = g_audio ? SDL_GetCurrentAudioDriver() : nullptr;
    return d ? d : "";
#endif
}

std::string Platform::controllerName() const {
#ifdef __EMSCRIPTEN__
    return std::string();
#else
    std::string names;
    for (auto& c : g_ctls) names += (names.empty() ? "" : " + ") + c->name;
    return names;
#endif
}

int Platform::controllerCount() const {
#ifdef __EMSCRIPTEN__
    return lastInput_ ? lastInput_->pad.controllers : 0;
#else
    return int(g_ctls.size());
#endif
}

// ---- force feedback (the engine's FfDevice through drive_view.cpp), on every controller that
// reports it

#ifndef __EMSCRIPTEN__
namespace {
const char* kEffectNames[4] = {"friction", "spring", "rumble", "jolt"};
void hapticLog(const Controller& c, const std::string& line) {
    if (g_hapticLog.size() < 8192) g_hapticLog.push_back("c" + std::to_string(c.tag) + " " + line);
}
// Calls `f` for each controller with force feedback (a recorded test device too).
template <typename F>
void eachHaptic(F f) {
    for (auto& c : g_ctls)
        if (c->haptic || c->fakeHaptic) f(*c);
}
}  // namespace
#endif

bool Platform::hapticAvailable() const {
#ifdef __EMSCRIPTEN__
    return false;
#else
    for (auto& c : g_ctls)
        if (c->haptic || c->fakeHaptic) return true;
    return false;
#endif
}

bool Platform::hapticPlaying(int e) const {
#ifdef __EMSCRIPTEN__
    (void)e;
    return false;
#else
    if (e < 0 || e > 3) return false;
    if (e == 3 && g_ffPlaying[3] && SDL_GetTicks() >= g_joltEnds) g_ffPlaying[3] = false;
    return g_ffPlaying[e];
#endif
}

void Platform::hapticStart(int e) {
#ifndef __EMSCRIPTEN__
    if (e < 0 || e > 3) return;
    eachHaptic([&](Controller& c) {
        if (c.fakeHaptic) hapticLog(c, std::string("start ") + kEffectNames[e]);
        else if (c.effect[e] >= 0) SDL_RunHapticEffect(c.haptic, c.effect[e], 1);
    });
    g_ffPlaying[e] = true;
    if (e == 3) g_joltEnds = SDL_GetTicks() + 100;
#else
    (void)e;
#endif
}

void Platform::hapticStop(int e) {
#ifndef __EMSCRIPTEN__
    if (e < 0 || e > 3) return;
    eachHaptic([&](Controller& c) {
        if (c.fakeHaptic) hapticLog(c, std::string("stop ") + kEffectNames[e]);
        else if (c.effect[e] >= 0) SDL_StopHapticEffect(c.haptic, c.effect[e]);
    });
    g_ffPlaying[e] = false;
#else
    (void)e;
#endif
}

void Platform::hapticFriction(int v) {
#ifndef __EMSCRIPTEN__
    eachHaptic([&](Controller& c) {
        if (c.fakeHaptic) hapticLog(c, "friction " + std::to_string(v));
        else if (c.effect[0] >= 0) {
            SDL_HapticEffect e = conditionEffect(SDL_HAPTIC_FRICTION, force(v));
            SDL_UpdateHapticEffect(c.haptic, c.effect[0], &e);
        }
    });
#else
    (void)v;
#endif
}

void Platform::hapticSpring(int v) {
#ifndef __EMSCRIPTEN__
    eachHaptic([&](Controller& c) {
        if (c.fakeHaptic) hapticLog(c, "spring " + std::to_string(v));
        else if (c.effect[1] >= 0) {
            SDL_HapticEffect e = conditionEffect(SDL_HAPTIC_SPRING, force(v));
            SDL_UpdateHapticEffect(c.haptic, c.effect[1], &e);
        }
    });
#else
    (void)v;
#endif
}

void Platform::hapticRumble(int periodUs, int magnitude) {
#ifndef __EMSCRIPTEN__
    eachHaptic([&](Controller& c) {
        if (c.fakeHaptic) hapticLog(c, "rumble " + std::to_string(periodUs) + " " + std::to_string(magnitude));
        else if (c.effect[2] >= 0) {
            SDL_HapticEffect e = rumbleEffect(c.haptic, Uint16(std::clamp(periodUs / 1000, 1, 65535)), force(magnitude));
            SDL_UpdateHapticEffect(c.haptic, c.effect[2], &e);
        }
    });
#else
    (void)periodUs; (void)magnitude;
#endif
}

void Platform::hapticJolt(int strength, int direction) {
#ifndef __EMSCRIPTEN__
    eachHaptic([&](Controller& c) {
        if (c.fakeHaptic) hapticLog(c, "jolt " + std::to_string(strength) + " " + std::to_string(direction));
        else if (c.effect[3] >= 0) {
            SDL_HapticEffect e = joltEffect(c.haptic, force(strength), direction);
            SDL_UpdateHapticEffect(c.haptic, c.effect[3], &e);
        }
    });
    hapticStart(3);
#else
    (void)strength; (void)direction;
#endif
}

void Platform::hapticStopAll() {
#ifndef __EMSCRIPTEN__
    for (int e = 0; e < 4; ++e)
        if (g_ffPlaying[e]) hapticStop(e);
#endif
}

std::vector<std::string> Platform::takeTestHapticLog() {
#ifdef __EMSCRIPTEN__
    return {};
#else
    std::vector<std::string> out;
    out.swap(g_hapticLog);
    return out;
#endif
}

// ---- test controllers (SDL's virtual joystick driver): plugged in by attaching them, so the
// platform finds them through the same events as a real controller plugged in.

int Platform::addTestController(bool wheel, bool haptic) {
#ifdef __EMSCRIPTEN__
    (void)wheel;
    (void)haptic;
    return 0;
#else
    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) return 0;
    SDL_VirtualJoystickDesc d;
    SDL_INIT_INTERFACE(&d);
    if (wheel) {
        d.type = SDL_JOYSTICK_TYPE_WHEEL;
        d.naxes = 6;  // wheel, a combined Y, and four more (separate pedals)
        d.nbuttons = 10;
        d.nhats = 1;
        d.name = "CrazyCity test wheel";
    } else {
        d.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        d.naxes = SDL_GAMEPAD_AXIS_COUNT;
        d.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        d.name = "CrazyCity test gamepad";
    }
    const SDL_JoystickID id = SDL_AttachVirtualJoystick(&d);
    if (!id) return 0;
    TestDevice t;
    t.id = id;
    t.joy = SDL_OpenJoystick(id);  // the test's own handle, for setting its values
    t.haptic = haptic;
    g_testDevices.push_back(t);
    return int(id);
#endif
}

void Platform::setTestAxis(int handle, int axis, float v) {
#ifndef __EMSCRIPTEN__
    for (const TestDevice& t : g_testDevices)
        if (int(t.id) == handle && t.joy)
            SDL_SetJoystickVirtualAxis(t.joy, axis, Sint16(std::clamp(v, -1.0f, 1.0f) * (v < 0 ? 32768.0f : 32767.0f)));
#else
    (void)handle; (void)axis; (void)v;
#endif
}

void Platform::setTestButton(int handle, int button, bool down) {
#ifndef __EMSCRIPTEN__
    for (const TestDevice& t : g_testDevices)
        if (int(t.id) == handle && t.joy) SDL_SetJoystickVirtualButton(t.joy, button, down);
#else
    (void)handle; (void)button; (void)down;
#endif
}

void Platform::removeTestController(int handle) {
#ifndef __EMSCRIPTEN__
    for (size_t i = 0; i < g_testDevices.size(); ++i)
        if (int(g_testDevices[i].id) == handle) {
            if (g_testDevices[i].joy) SDL_CloseJoystick(g_testDevices[i].joy);
            SDL_DetachVirtualJoystick(g_testDevices[i].id);  // unplugged: the platform hears of it
            g_testDevices.erase(g_testDevices.begin() + long(i));
            return;
        }
#else
    (void)handle;
#endif
}

void Platform::injectKey(int scancode, bool down) {
    SDL_Event e;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = SDL_Scancode(scancode);
    e.key.down = down;
    e.key.windowID = window_ ? SDL_GetWindowID(static_cast<SDL_Window*>(window_)) : 0;
    SDL_PushEvent(&e);
}

void Platform::injectMouse(float x, float y, int button, bool down) {
    const SDL_WindowID wid = window_ ? SDL_GetWindowID(static_cast<SDL_Window*>(window_)) : 0;
    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_EVENT_MOUSE_MOTION;
    e.motion.windowID = wid;
    e.motion.x = x;
    e.motion.y = y;
    SDL_PushEvent(&e);
    if (button < 0) return;
    SDL_zero(e);
    e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    e.button.windowID = wid;
    e.button.button = Uint8(button == 2 ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT);
    e.button.down = down;
    e.button.x = x;
    e.button.y = y;
    SDL_PushEvent(&e);
}

std::string Platform::chooseFolder(const std::string& title, const std::string& startDir) {
#ifdef __EMSCRIPTEN__
    (void)title; (void)startDir;
    return std::string();
#else
    struct State {
        bool done = false;
        std::string path;
    } st;
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
    if (!startDir.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, startDir.c_str());
    if (window_ && !g_cfg.offscreen) SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window_);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    SDL_ShowFileDialogWithProperties(
        SDL_FILEDIALOG_OPENFOLDER,
        [](void* user, const char* const* files, int) {
            State* s = static_cast<State*>(user);
            if (!files) logWarn("folder dialog: %s", SDL_GetError());
            else if (files[0]) s->path = files[0];
            s->done = true;
        },
        &st, props);
    SDL_DestroyProperties(props);
    while (!st.done) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 50) && e.type == SDL_EVENT_QUIT) {
            quit_ = true;
            break;
        }
    }
    return st.path;
#endif
}

std::vector<std::string> Platform::chooseFiles(const std::string& title, const std::string& startDir, const std::string& filterName,
                                               const std::string& patterns, bool many) {
#ifdef __EMSCRIPTEN__
    (void)title; (void)startDir; (void)filterName; (void)patterns; (void)many;
    return {};
#else
    struct State {
        bool done = false;
        std::vector<std::string> paths;
    } st;
    SDL_DialogFileFilter filter{filterName.c_str(), patterns.c_str()};
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title.c_str());
    if (!startDir.empty()) SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, startDir.c_str());
    if (window_ && !g_cfg.offscreen) SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, window_);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, many);
    if (!patterns.empty()) {
        SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, &filter);
        SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, 1);
    }
    SDL_ShowFileDialogWithProperties(
        SDL_FILEDIALOG_OPENFILE,
        [](void* user, const char* const* files, int) {
            State* s = static_cast<State*>(user);
            if (!files) logWarn("file dialog: %s", SDL_GetError());
            else
                for (int i = 0; files[i]; ++i) s->paths.push_back(files[i]);
            s->done = true;
        },
        &st, props);
    SDL_DestroyProperties(props);
    while (!st.done) {
        SDL_Event e;
        if (SDL_WaitEventTimeout(&e, 50) && e.type == SDL_EVENT_QUIT) {
            quit_ = true;
            break;
        }
    }
    return st.paths;
#endif
}

void Platform::message(const std::string& title, const std::string& text, bool error) {
    if (error) logError("%s: %s", title.c_str(), text.c_str());
    else logInfo("%s: %s", title.c_str(), text.c_str());
#ifndef __EMSCRIPTEN__
    if (g_cfg.offscreen) return;
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, title.c_str(), text.c_str(),
                             static_cast<SDL_Window*>(window_));
#endif
}

void Platform::delay(double seconds) {
    if (seconds > 0) SDL_DelayPrecise(Uint64(seconds * 1e9));
}

std::string Platform::basePath() {
#ifdef __EMSCRIPTEN__
    return std::string();
#else
    const char* p = SDL_GetBasePath();
    return p ? p : "./";
#endif
}

}  // namespace cc
