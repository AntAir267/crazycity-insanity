#include "platform/platform.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
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
// Native game controllers: the first SDL gamepad (read in the standard layout), else the first
// joystick or wheel (read as it is). Force feedback (controls.md, "Force feedback"): a controller
// SDL reports as haptic gets MM2's four effects (friction, spring, rumble, jolt) when it is opened,
// with its automatic centring off; the engine's force feedback drives them (drive/force_feedback.h).

struct Controller {
    SDL_Gamepad* pad = nullptr;
    SDL_Joystick* joy = nullptr;  // the gamepad's joystick, or the plain joystick / wheel
    SDL_JoystickID id = 0;
    int kind = GamepadState::Gamepad;
    std::string name;
    SDL_Haptic* haptic = nullptr;
    SDL_HapticEffectID effect[4] = {-1, -1, -1, -1};  // friction, spring, rumble, jolt
    bool playing[4] = {false, false, false, false};
    Uint64 joltEnds = 0;      // ms: the 0.1 s jolt ends by itself
    bool fakeHaptic = false;  // tests: force-feedback commands recorded, no device
};
Controller g_ctl;
std::vector<std::string> g_hapticLog;  // tests

void closeController() {
    if (g_ctl.haptic) {
        SDL_StopHapticEffects(g_ctl.haptic);
        for (SDL_HapticEffectID id : g_ctl.effect)
            if (id >= 0) SDL_DestroyHapticEffect(g_ctl.haptic, id);
        SDL_CloseHaptic(g_ctl.haptic);
    }
    if (g_ctl.pad) SDL_CloseGamepad(g_ctl.pad);
    else if (g_ctl.joy) SDL_CloseJoystick(g_ctl.joy);
    g_ctl = Controller();
}

// The effect templates (controls.md, "Driving SDL's haptic interface").
SDL_HapticDirection steeringAxis(unsigned features) {
    SDL_HapticDirection d;
    SDL_zero(d);
    (void)features;
    d.type = SDL_HAPTIC_STEERING_AXIS;
    return d;
}

SDL_HapticEffect conditionEffect(Uint16 type, Sint16 coefficient) {
    SDL_HapticEffect e;
    SDL_zero(e);
    e.type = type;
    e.condition.direction = steeringAxis(0);
    e.condition.length = SDL_HAPTIC_INFINITY;
    e.condition.right_sat[0] = e.condition.left_sat[0] = 0xFFFF;
    e.condition.right_coeff[0] = e.condition.left_coeff[0] = coefficient;
    return e;
}

Uint16 periodicType() {
    const unsigned f = g_ctl.haptic ? SDL_GetHapticFeatures(g_ctl.haptic) : 0;
    return (f & SDL_HAPTIC_SQUARE) ? SDL_HAPTIC_SQUARE : SDL_HAPTIC_SINE;
}

SDL_HapticEffect rumbleEffect(Uint16 periodMs, Sint16 magnitude) {
    SDL_HapticEffect e;
    SDL_zero(e);
    e.type = periodicType();
    e.periodic.direction = steeringAxis(0);
    e.periodic.length = SDL_HAPTIC_INFINITY;
    e.periodic.period = periodMs;
    e.periodic.magnitude = magnitude;
    return e;
}

SDL_HapticEffect joltEffect(Sint16 magnitude, Sint32 direction) {
    SDL_HapticEffect e;
    SDL_zero(e);
    e.type = periodicType();
    e.periodic.direction.type = SDL_HAPTIC_POLAR;
    e.periodic.direction.dir[0] = direction;
    e.periodic.length = 100;
    e.periodic.period = 2000;
    e.periodic.magnitude = magnitude;
    return e;
}

Sint16 force(int c) { return Sint16(std::clamp(c, 0, 10000) * 32767 / 10000); }

void openHaptic() {
    g_ctl.haptic = SDL_OpenHapticFromJoystick(g_ctl.joy);
    if (!g_ctl.haptic) return;
    const unsigned f = SDL_GetHapticFeatures(g_ctl.haptic);
    if (f & SDL_HAPTIC_AUTOCENTER) SDL_SetHapticAutocenter(g_ctl.haptic, 0);  // as MM2, whatever the option says
    SDL_HapticEffect e;
    if (f & SDL_HAPTIC_FRICTION) {
        e = conditionEffect(SDL_HAPTIC_FRICTION, 0);
        g_ctl.effect[0] = SDL_CreateHapticEffect(g_ctl.haptic, &e);
    }
    if (f & SDL_HAPTIC_SPRING) {
        e = conditionEffect(SDL_HAPTIC_SPRING, 0);
        g_ctl.effect[1] = SDL_CreateHapticEffect(g_ctl.haptic, &e);
    }
    if (f & (SDL_HAPTIC_SQUARE | SDL_HAPTIC_SINE)) {
        e = rumbleEffect(5000, force(2000));
        g_ctl.effect[2] = SDL_CreateHapticEffect(g_ctl.haptic, &e);
        e = joltEffect(force(10000), 0);
        g_ctl.effect[3] = SDL_CreateHapticEffect(g_ctl.haptic, &e);
    }
}

bool openController(SDL_JoystickID id) {
    closeController();
    if (SDL_IsGamepad(id)) {
        g_ctl.pad = SDL_OpenGamepad(id);
        if (!g_ctl.pad) return false;
        g_ctl.joy = SDL_GetGamepadJoystick(g_ctl.pad);
        g_ctl.kind = GamepadState::Gamepad;
        const char* n = SDL_GetGamepadName(g_ctl.pad);
        g_ctl.name = n ? n : "gamepad";
    } else {
        g_ctl.joy = SDL_OpenJoystick(id);
        if (!g_ctl.joy) return false;
        g_ctl.kind = SDL_GetJoystickType(g_ctl.joy) == SDL_JOYSTICK_TYPE_WHEEL ? GamepadState::Wheel : GamepadState::Joystick;
        const char* n = SDL_GetJoystickName(g_ctl.joy);
        g_ctl.name = n ? n : "joystick";
    }
    g_ctl.id = id;
    if (SDL_IsJoystickHaptic(g_ctl.joy)) openHaptic();
    const char* kinds[] = {"gamepad", "joystick", "wheel"};
    logInfo("game controller: %s (%s, %d buttons, %d axes%s)", g_ctl.name.c_str(), kinds[g_ctl.kind],
            SDL_GetNumJoystickButtons(g_ctl.joy), SDL_GetNumJoystickAxes(g_ctl.joy), g_ctl.haptic ? ", force feedback" : "");
    return true;
}

// The first controller SDL lists: a gamepad before any other joystick.
bool openFirstController() {
    int n = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&n);
    bool ok = false;
    if (ids && n > 0) ok = openController(ids[0]);
    SDL_free(ids);
    if (ok) return true;
    ids = SDL_GetJoysticks(&n);
    if (ids && n > 0) ok = openController(ids[0]);
    SDL_free(ids);
    return ok;
}

float axisValue(Sint16 v) {
    float f = float(v) / 32767.0f;
    return f < -1.0f ? -1.0f : (f > 1.0f ? 1.0f : f);
}

void pollController(GamepadState& g) {
    constexpr int NA = GamepadState::kAxes, NB = GamepadState::kButtons;
    g.connected = g_ctl.joy != nullptr && SDL_JoystickConnected(g_ctl.joy);
    g.kind = g_ctl.kind;
    g.feedback = g.connected && (g_ctl.haptic != nullptr || g_ctl.fakeHaptic);
    g.hat = 0;
    for (float& b : g.button) b = 0.0f;
    for (float& a : g.axis) a = 0.0f;
    if (!g.connected) {
        g.buttonCount = g.axisCount = 0;
        return;
    }
    if (g_ctl.pad) {
        SDL_Gamepad* p = g_ctl.pad;
        g.axis[0] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX));
        g.axis[1] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY));
        g.axis[2] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX));
        g.axis[3] = axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY));
        static const SDL_GamepadButton order[17] = {
            SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_INVALID,
            SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_START,
            SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK, SDL_GAMEPAD_BUTTON_DPAD_UP,
            SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            SDL_GAMEPAD_BUTTON_GUIDE};
        for (int b = 0; b < 17; ++b)
            g.button[b] = order[b] == SDL_GAMEPAD_BUTTON_INVALID ? 0.0f : (SDL_GetGamepadButton(p, order[b]) ? 1.0f : 0.0f);
        // The triggers are analog (0 … 32767): buttons 6 / 7, and axes 4 / 5 from -1 to +1.
        g.button[6] = std::max(0.0f, axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER)));
        g.button[7] = std::max(0.0f, axisValue(SDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)));
        g.axis[4] = g.button[6] * 2.0f - 1.0f;
        g.axis[5] = g.button[7] * 2.0f - 1.0f;
        g.buttonCount = 17;
        g.axisCount = 6;
        return;
    }
    SDL_Joystick* j = g_ctl.joy;
    const int na = SDL_GetNumJoystickAxes(j), nb = SDL_GetNumJoystickButtons(j);
    for (int a = 0; a < NA && a < na; ++a) g.axis[a] = axisValue(SDL_GetJoystickAxis(j, a));
    for (int b = 0; b < NB && b < nb; ++b) g.button[b] = SDL_GetJoystickButton(j, b) ? 1.0f : 0.0f;
    if (SDL_GetNumJoystickHats(j) > 0) {
        const Uint8 h = SDL_GetJoystickHat(j, 0);
        g.hat = (h & SDL_HAT_UP ? 1 : 0) | (h & SDL_HAT_RIGHT ? 2 : 0) | (h & SDL_HAT_DOWN ? 4 : 0) | (h & SDL_HAT_LEFT ? 8 : 0);
    }
    g.buttonCount = nb;
    g.axisCount = na;
}

// ---------------------------------------------------------------------------------------------
// Native audio output and window bookkeeping.

SDL_AudioStream* g_audio = nullptr;
std::vector<float> g_audioBuf;
SDL_Window* g_window = nullptr;
SDL_GLContext g_context = nullptr;

// The self-test's virtual controller.
SDL_JoystickID g_testPad = 0;

Platform::WindowState g_win;
bool g_winChanged = false;
#endif  // !__EMSCRIPTEN__

void pollGamepad(GamepadState& g) {
    constexpr int NB = GamepadState::kButtons;
    bool before[NB];
    for (int i = 0; i < NB; ++i) before[i] = g.button[i] > 0.5f;
    for (bool& p : g.pressed) p = false;
#ifdef __EMSCRIPTEN__
    g.connected = false;
    g.kind = GamepadState::Gamepad;
    g.hat = 0;
    if (emscripten_sample_gamepad_data() != EMSCRIPTEN_RESULT_SUCCESS) return;
    int n = emscripten_get_num_gamepads();
    for (int i = 0; i < n; ++i) {
        EmscriptenGamepadEvent st;
        if (emscripten_get_gamepad_status(i, &st) != EMSCRIPTEN_RESULT_SUCCESS || !st.connected) continue;
        g.connected = true;
        for (int a = 0; a < GamepadState::kAxes; ++a) g.axis[a] = a < st.numAxes ? float(st.axis[a]) : 0.0f;
        for (int b = 0; b < NB; ++b) g.button[b] = b < st.numButtons ? float(st.analogButton[b]) : 0.0f;
        // The standard layout's triggers as axes 4 / 5 (-1 released … +1 pressed), as natively.
        if (st.numAxes <= 4) {
            g.axis[4] = g.button[6] * 2.0f - 1.0f;
            g.axis[5] = g.button[7] * 2.0f - 1.0f;
        }
        g.buttonCount = st.numButtons;
        g.axisCount = st.numAxes <= 4 ? 6 : st.numAxes;
        break;
    }
    if (!g.connected) {
        for (float& b : g.button) b = 0.0f;
        for (float& a : g.axis) a = 0.0f;
        g.buttonCount = g.axisCount = 0;
    }
#else
    pollController(g);
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
        if (openFirstController()) startupButtons_ = SDL_GetNumJoystickButtons(g_ctl.joy);
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
    closeController();
    if (window_) SDL_HideWindow(static_cast<SDL_Window*>(window_));
#endif
}

void Platform::finalQuit() {
#ifndef __EMSCRIPTEN__
    if (g_context) SDL_GL_DestroyContext(g_context);
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
                if (g_cfg.controllers && !g_ctl.joy) {
                    // A gamepad also arrives as a joystick: wait for its gamepad event.
                    const SDL_JoystickID id = e.type == SDL_EVENT_GAMEPAD_ADDED ? e.gdevice.which : e.jdevice.which;
                    if (e.type == SDL_EVENT_GAMEPAD_ADDED || !SDL_IsGamepad(id)) openController(id);
                }
                break;
            case SDL_EVENT_JOYSTICK_REMOVED:
                if (g_ctl.joy && e.jdevice.which == g_ctl.id) {
                    logInfo("game controller removed: %s", g_ctl.name.c_str());
                    closeController();
                    openFirstController();
                }
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
    return g_ctl.name;
#endif
}

// ---- force feedback (the engine's FfDevice through drive_view.cpp)

#ifndef __EMSCRIPTEN__
namespace {
const char* kEffectNames[4] = {"friction", "spring", "rumble", "jolt"};
void hapticLog(const std::string& line) {
    if (g_hapticLog.size() < 4096) g_hapticLog.push_back(line);
}
}  // namespace
#endif

bool Platform::hapticAvailable() const {
#ifdef __EMSCRIPTEN__
    return false;
#else
    return g_ctl.joy && (g_ctl.haptic || g_ctl.fakeHaptic);
#endif
}

bool Platform::hapticPlaying(int e) const {
#ifdef __EMSCRIPTEN__
    (void)e;
    return false;
#else
    if (e < 0 || e > 3) return false;
    if (e == 3 && g_ctl.playing[3] && SDL_GetTicks() >= g_ctl.joltEnds) g_ctl.playing[3] = false;
    return g_ctl.playing[e];
#endif
}

void Platform::hapticStart(int e) {
#ifndef __EMSCRIPTEN__
    if (e < 0 || e > 3) return;
    if (g_ctl.fakeHaptic) hapticLog(std::string("start ") + kEffectNames[e]);
    else if (g_ctl.haptic && g_ctl.effect[e] >= 0) SDL_RunHapticEffect(g_ctl.haptic, g_ctl.effect[e], 1);
    g_ctl.playing[e] = true;
    if (e == 3) g_ctl.joltEnds = SDL_GetTicks() + 100;
#else
    (void)e;
#endif
}

void Platform::hapticStop(int e) {
#ifndef __EMSCRIPTEN__
    if (e < 0 || e > 3) return;
    if (g_ctl.fakeHaptic) hapticLog(std::string("stop ") + kEffectNames[e]);
    else if (g_ctl.haptic && g_ctl.effect[e] >= 0) SDL_StopHapticEffect(g_ctl.haptic, g_ctl.effect[e]);
    g_ctl.playing[e] = false;
#else
    (void)e;
#endif
}

void Platform::hapticFriction(int c) {
#ifndef __EMSCRIPTEN__
    if (g_ctl.fakeHaptic) hapticLog("friction " + std::to_string(c));
    else if (g_ctl.haptic && g_ctl.effect[0] >= 0) {
        SDL_HapticEffect e = conditionEffect(SDL_HAPTIC_FRICTION, force(c));
        SDL_UpdateHapticEffect(g_ctl.haptic, g_ctl.effect[0], &e);
    }
#else
    (void)c;
#endif
}

void Platform::hapticSpring(int c) {
#ifndef __EMSCRIPTEN__
    if (g_ctl.fakeHaptic) hapticLog("spring " + std::to_string(c));
    else if (g_ctl.haptic && g_ctl.effect[1] >= 0) {
        SDL_HapticEffect e = conditionEffect(SDL_HAPTIC_SPRING, force(c));
        SDL_UpdateHapticEffect(g_ctl.haptic, g_ctl.effect[1], &e);
    }
#else
    (void)c;
#endif
}

void Platform::hapticRumble(int periodUs, int magnitude) {
#ifndef __EMSCRIPTEN__
    if (g_ctl.fakeHaptic) hapticLog("rumble " + std::to_string(periodUs) + " " + std::to_string(magnitude));
    else if (g_ctl.haptic && g_ctl.effect[2] >= 0) {
        SDL_HapticEffect e = rumbleEffect(Uint16(std::clamp(periodUs / 1000, 1, 65535)), force(magnitude));
        SDL_UpdateHapticEffect(g_ctl.haptic, g_ctl.effect[2], &e);
    }
#else
    (void)periodUs; (void)magnitude;
#endif
}

void Platform::hapticJolt(int strength, int direction) {
#ifndef __EMSCRIPTEN__
    if (g_ctl.fakeHaptic) hapticLog("jolt " + std::to_string(strength) + " " + std::to_string(direction));
    else if (g_ctl.haptic && g_ctl.effect[3] >= 0) {
        SDL_HapticEffect e = joltEffect(force(strength), direction);
        SDL_UpdateHapticEffect(g_ctl.haptic, g_ctl.effect[3], &e);
    }
    hapticStart(3);
#else
    (void)strength; (void)direction;
#endif
}

void Platform::hapticStopAll() {
#ifndef __EMSCRIPTEN__
    for (int e = 0; e < 4; ++e)
        if (g_ctl.playing[e]) hapticStop(e);
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

bool Platform::openTestController(bool wheel, bool haptic) {
#ifdef __EMSCRIPTEN__
    (void)wheel;
    (void)haptic;
    return false;
#else
    closeTestController();
    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC)) return false;
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
    g_testPad = SDL_AttachVirtualJoystick(&d);
    if (!g_testPad) return false;
    SDL_UpdateJoysticks();
    if (!openController(g_testPad)) return false;
    g_ctl.fakeHaptic = haptic;
    g_hapticLog.clear();
    return true;
#endif
}

void Platform::setTestAxis(int axis, float v) {
#ifndef __EMSCRIPTEN__
    if (g_ctl.joy && g_ctl.id == g_testPad)
        SDL_SetJoystickVirtualAxis(g_ctl.joy, axis, Sint16(std::clamp(v, -1.0f, 1.0f) * (v < 0 ? 32768.0f : 32767.0f)));
#else
    (void)axis; (void)v;
#endif
}

void Platform::setTestButton(int button, bool down) {
#ifndef __EMSCRIPTEN__
    if (g_ctl.joy && g_ctl.id == g_testPad) SDL_SetJoystickVirtualButton(g_ctl.joy, button, down);
#else
    (void)button; (void)down;
#endif
}

void Platform::closeTestController() {
#ifndef __EMSCRIPTEN__
    if (!g_testPad) return;
    if (g_ctl.id == g_testPad) closeController();
    SDL_DetachVirtualJoystick(g_testPad);
    g_testPad = 0;
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
