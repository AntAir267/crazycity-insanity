// Thin platform layer: window + GL context, input, time. One implementation (platform_sdl.cpp,
// SDL3) serves both builds: the browser build through Emscripten's SDL3 port, the native build
// with SDL3 linked statically. Natively it also owns the audio output, game controllers with force
// feedback, full-screen switching, the folder dialog and an offscreen mode for tests.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/input.h"

namespace cc {

enum Key {
    KeyW, KeyA, KeyS, KeyD, KeyQ, KeyE, KeyShift, KeyCtrl, KeySpace, KeyEscape,
    KeyUp, KeyDown, KeyLeft, KeyRight, KeyR, KeyF, KeyPageUp, KeyPageDown,
    KeyC, KeyV, KeyB, KeyT, KeyZ, KeyH, KeyBackspace, KeyEnter, KeyG,
    KeyNum2, KeyNum4, KeyNum6, KeyNum8,
    // HUD keys (hudmap.md, hud.md; our choices where the driving keys took MM2's letters):
    // Tab map mode, M full-screen map (MM2: Q), K map zoom (MM2: E), O map orientation
    // (MM2: F), I opponent icons, F2 pause, F4 restart; N / X next / previous waypoint (MM2: S / X).
    KeyTab, KeyM, KeyN, KeyO, KeyI, KeyF2, KeyF4, KeyX, KeyK,
    KeyF1,  // native: the debug overlay on/off (MM2: the customise-controls screen, menus.md)
    KeyCount
};

// The game controller: the browser's first connected gamepad, natively the first SDL gamepad, or
// failing that the first joystick or wheel SDL finds.
// Gamepads use the Web "standard" layout (buttons 0 A, 1 B, 2 X, 3 Y, 4 LB, 5 RB, 6 LT, 7 RT,
// 8 View/Back, 9 Start, 10/11 stick clicks, 12-15 d-pad up/down/left/right, 16 guide; axes 0/1
// left stick, 2/3 right stick, 4/5 the left / right trigger as an axis, -1 released … +1 pressed).
// A joystick or wheel (kind Joystick / Wheel) keeps its own numbering: axes 0 … 7 as the device
// gives them, button n = DirectInput's button n + 1 (controls.md's "button 1" is button[0]), the
// first hat in `hat`.
struct GamepadState {
    enum Kind { Gamepad = 0, Joystick = 1, Wheel = 2 };
    static constexpr int kAxes = 8, kButtons = 32;
    bool connected = false;
    int kind = Gamepad;
    float axis[kAxes] = {};
    float button[kButtons] = {};  // analog value 0..1
    bool pressed[kButtons] = {};  // went down this frame
    int hat = 0;                  // the hat's directions: 1 up, 2 right, 4 down, 8 left
    int axisCount = 0;            // axes and buttons the controller has
    int buttonCount = 0;
    bool feedback = false;        // the controller reports force feedback
    // The driver's control options (controls.md, players/<name>.cfg): the controller dead zone and
    // U = 1 / "Steering Sensitivity". Set by the game, never by the platform.
    float deadZone = 0.1f;
    float steerU = 1.0f;
};

struct InputState {
    bool down[KeyCount] = {};
    bool pressed[KeyCount] = {};  // went down this frame
    // Every key by its DirectInput scan code (the binding table's keyboard components,
    // controls.md): held, and went down this frame.
    bool key[256] = {};
    bool keyPressed[256] = {};
    float mouseDX = 0, mouseDY = 0;  // pixels moved this frame
    float wheel = 0;                 // wheel steps this frame (+ = away from the user)
    float mouseX = 0, mouseY = 0;
    bool mouseKnown = false;         // the pointer has been over the window (a move or a button)
    float windowW = 0, windowH = 0;  // the window (canvas) size in the mouse's units
    bool buttons[3] = {};            // left, middle, right held
    bool clicked[3] = {};            // pressed this frame
    bool pointerLocked = false;
    bool hasFocus = false;           // keyboard goes to the canvas
    GamepadState pad;
    // Everything that happened this frame, in order, for the engine's UI (ui/input.h): keys with
    // their modifiers and repeats, typed text (while text input is on), pointer and touch in
    // device pixels, wheel steps. Touches arrive as Touch* events, not as mouse events.
    std::vector<ui::InputEvent> events;

    void beginFrame() {
        for (bool& p : pressed) p = false;
        for (bool& p : keyPressed) p = false;
        for (bool& c : clicked) c = false;
        mouseDX = mouseDY = wheel = 0;
        events.clear();
    }
};

// Native start-up choices, given to Platform::configure() before the app initialises the platform.
// The browser build ignores them.
struct PlatformConfig {
    bool offscreen = false;  // nothing on the screen: SDL's offscreen video driver (an EGL pbuffer)
    int width = 1280, height = 720;  // window size in screen units (the offscreen frame in pixels)
    int x = -1, y = -1;              // window position; -1 = the system's choice
    bool fullscreen = false;         // borderless full screen on the window's display
    bool maximized = false;
    bool vsync = true;
    int msaa = 4;                    // multisample count (0 = none), as the page's WebGL canvas
    bool audio = true;               // open the audio output
    std::string audioDriver;         // SDL audio driver ("" = SDL's choice; "dummy" for tests)
    bool controllers = true;         // game controllers and force feedback
};

class Platform {
public:
    static void configure(const PlatformConfig& c);

    bool init(const char* title);
    // Natively: stops the sound and lets go of the controllers. The window, the GL context and
    // SDL stay until finalQuit(), which the native program registers with atexit() before the
    // app (a static) exists, so the app's GL objects go while their context is still there.
    void shutdown();
    static void finalQuit();
    // Processes pending events into `input`. Returns false when the app should quit.
    bool pollEvents(InputState& input);
    // Drawable size in pixels (follows the canvas' CSS size on the web).
    void drawableSize(int& w, int& h);
    void swap();
    double timeSeconds() const;
    void setPointerLock(bool on);
    bool pointerLocked() const;
    // Device pixels per UI pixel: the browser's devicePixelRatio; natively the window's display
    // scale (pixel density times the desktop's scale setting).
    float pixelRatio() const;
    // Typed text events on or off (on while a UI text field has the keyboard).
    void setTextInput(bool on);
    // The system clipboard (natively; the browser's goes through the page's clipboard events).
    void setClipboard(const std::string& text);
    std::string clipboard() const;

    // ---- native only (no-ops or defaults in the browser)
    // The window was closed (or the system asked the program to quit).
    bool quitRequested() const { return quit_; }
    // The input of the last pollEvents (the native program's own keys after a frame).
    const InputState* lastInput() const { return lastInput_; }
    // The driver's controller options, copied into the input's GamepadState at every poll.
    void setControllerOptions(float deadZone, float steerU) {
        deadZone_ = deadZone;
        steerU_ = steerU;
    }
    // Called just before each frame is presented, with the drawable size: the native overlay and
    // the frame capture draw or read there.
    void setBeforeSwap(std::function<void(int w, int h)> f) { beforeSwap_ = std::move(f); }
    bool offscreen() const;
    bool minimized() const;
    // Waits up to `ms` for the window to come back (events are handled; false = quit).
    bool waitWhileMinimized(int ms);
    void toggleFullscreen();
    struct WindowState {
        int w = 1280, h = 720, x = -1, y = -1;
        bool fullscreen = false, maximized = false;
    };
    // The window's size and place to remember (the size before full screen / maximising).
    WindowState windowState() const;
    bool windowStateChanged();  // since the last call
    // Reads the drawable's pixels (RGBA, bottom row first) before the swap; false if unreadable.
    bool readPixels(int w, int h, unsigned char* rgba) const;

    // The audio output: one stream of interleaved stereo float frames at `rate` Hz.
    bool openAudio(int rate);
    // Tops the stream up from `pull` in chunks of `chunk` frames until `target` frames are queued
    // (the browser's script node pulls 1024 frames at a time on the page's thread; this does the
    // same between frames). Returns the frames pulled.
    int feedAudio(const std::function<void(float*, int)>& pull, int chunk, int target);
    void pauseAudio(bool paused);
    void setAudioGain(float gain);  // the output's master gain (1 = as the engine mixes it)
    int audioQueued() const;  // frames waiting in the stream
    bool audioOpen() const;
    std::string audioDriver() const;

    // Game controllers: the buttons of the controller found at start-up (0 = none; the binding
    // table's defaults depend on it, controls.md), its name, and the force feedback.
    int startupControllerButtons() const { return startupButtons_; }
    std::string controllerName() const;
    // Force feedback (controls.md, "Force feedback"; the engine's FfDevice): MM2's four effects
    // 0 friction, 1 spring, 2 rumble, 3 jolt on a controller that reports force feedback; values in
    // MM2's units (0 … 10000, µs, hundredths of a degree). Stopped all when the window loses focus.
    bool hapticAvailable() const;
    bool hapticPlaying(int effect) const;
    void hapticStart(int effect);
    void hapticStop(int effect);
    void hapticFriction(int coefficient);
    void hapticSpring(int coefficient);
    void hapticRumble(int periodUs, int magnitude);
    void hapticJolt(int strength, int direction);
    void hapticStopAll();

    // Tests: a virtual gamepad (axes and buttons in SDL's gamepad order) or wheel (raw axes,
    // buttons and a hat) from SDL's virtual joystick driver, opened as the controller (`haptic`:
    // reporting force feedback, its commands recorded instead of played); its axes (-1 … 1) and
    // buttons; the recorded force-feedback commands since the last call.
    bool openTestController(bool wheel, bool haptic = false);
    void setTestAxis(int axis, float v);
    void setTestButton(int button, bool down);
    std::vector<std::string> takeTestHapticLog();
    void closeTestController();
    // Tests: a key event (SDL scancode) as if the player had pressed or released the key; the
    // pointer moved to (x, y) in window units, and the left button (0) or right (2) pressed or
    // released there (button -1: only the move).
    void injectKey(int scancode, bool down);
    void injectMouse(float x, float y, int button, bool down);

    // A folder chosen with the system's folder dialog ("" = cancelled). Blocks until the player
    // has answered, handling events meanwhile.
    std::string chooseFolder(const std::string& title, const std::string& startDir);
    // Files chosen with the system's file dialog (empty = cancelled); `patterns` like "ar" or
    // "dls;sf2" for a filter called `filterName`; `many`: several files.
    std::vector<std::string> chooseFiles(const std::string& title, const std::string& startDir, const std::string& filterName,
                                         const std::string& patterns, bool many);
    // A message box (natively; falls back to the log).
    void message(const std::string& title, const std::string& text, bool error);
    // Where the program is (natively: the folder of the executable, with a trailing slash).
    static std::string basePath();
    static void delay(double seconds);

private:
    void* window_ = nullptr;
    void* context_ = nullptr;
    std::function<void(int, int)> beforeSwap_;
    int startupButtons_ = 0;
    bool quit_ = false;
    const InputState* lastInput_ = nullptr;
    float deadZone_ = 0.1f, steerU_ = 1.0f;
};

}  // namespace cc
