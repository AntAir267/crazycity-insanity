// The UI's input events (keys, typed text, pointer, touch, wheel, clipboard), filled in by the
// platform layer each frame (platform/platform.h InputState::events).
#pragma once

#include <cstdint>
#include <string>

namespace cc {
namespace ui {

enum class Key : uint16_t {
    None, Escape, Enter, Tab, Backspace, Delete, Left, Right, Up, Down, Home, End, PageUp, PageDown, Space,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    D0, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Other,
};
enum Mod : uint8_t { ModShift = 1, ModCtrl = 2, ModAlt = 4, ModMeta = 8 };
// Gamepad buttons (the Web "standard" layout order).
enum class Pad : uint8_t { A, B, X, Y, LB, RB, LT, RT, Back, Start, LS, RS, Up, Down, Left, Right, Count };

struct InputEvent {
    enum Type : uint8_t { KeyDown, KeyUp, Text, MouseMove, MouseDown, MouseUp, Wheel, TouchDown, TouchMove, TouchUp, PadDown, Paste, Cut, Copy };
    Type type = KeyDown;
    Key key = Key::None;
    uint8_t mods = 0;
    bool repeat = false;
    std::string text;     // Text, Paste
    float x = 0, y = 0;   // device px (mouse, touch)
    int button = 0;       // 0 left, 1 middle, 2 right
    float dx = 0, dy = 0; // Wheel: steps (+dy = away from the user, i.e. scroll up)
    int touch = 0;
    Pad pad = Pad::A;
};

}  // namespace ui
}  // namespace cc
