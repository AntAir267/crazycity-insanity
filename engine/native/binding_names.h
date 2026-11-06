// Readable names for the binding table (controls.md) on the native command line: --bind,
// --device and --list controls, until the engine draws the customise-controls screen.
#pragma once

#include <cstdint>
#include <string>

namespace cc {
namespace native {

// "mouse", "keyboard", "joystick", "gamepad", "wheel" <-> 0 … 4 (-1 unknown).
int deviceNumber(const std::string& name);
const char* deviceName(int device);
// "throttle", "steer", "shiftup", … <-> action 0 … 33 (-1 unknown).
int actionNumber(const std::string& name);
const char* actionName(int action);

struct BindingText {
    uint32_t kind = 0, device = 0, component = 0;  // kind 0: no binding
};
// "key:up", "key:0x1e", "button:3", "axis:z", "half:y-", "hat", "mouse:left", "mouse:x", "none",
// for `action` (its kind follows the action and the input: held, press or analog). False with a
// reason when the text is not one of them.
bool parseBinding(const std::string& text, int action, BindingText& out, std::string& why);
std::string bindingText(uint32_t kind, uint32_t device, uint32_t component);

}  // namespace native
}  // namespace cc
