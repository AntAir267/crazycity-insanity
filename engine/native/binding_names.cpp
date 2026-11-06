#include "native/binding_names.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace cc {
namespace native {

namespace {

const char* kDevices[5] = {"mouse", "keyboard", "joystick", "gamepad", "wheel"};
const char* kActions[34] = {"map",        "fullmap",     "mapzoom",  "maporientation", "gauges",   "steer",   "left",
                            "right",      "throttle",    "brake",    "handbrake",      "camera",   "crossview", "horn",
                            "lookleft",   "lookright",   "lookback", "lookforward",    "wide",     "dash",    "gearbox",
                            "shiftup",    "shiftdown",   "reverse",  "nextwaypoint",   "prevwaypoint", "cdshow", "cdplay",
                            "cdprevious", "cdnext",      "mirror",   "lookaround",     "icons",    "chat"};

struct KeyName {
    const char* name;
    uint32_t dik;
};
const KeyName kKeys[] = {
    {"escape", 0x01}, {"1", 0x02}, {"2", 0x03}, {"3", 0x04}, {"4", 0x05}, {"5", 0x06}, {"6", 0x07}, {"7", 0x08},
    {"8", 0x09}, {"9", 0x0A}, {"0", 0x0B}, {"minus", 0x0C}, {"equals", 0x0D}, {"backspace", 0x0E}, {"tab", 0x0F},
    {"q", 0x10}, {"w", 0x11}, {"e", 0x12}, {"r", 0x13}, {"t", 0x14}, {"y", 0x15}, {"u", 0x16}, {"i", 0x17},
    {"o", 0x18}, {"p", 0x19}, {"lbracket", 0x1A}, {"rbracket", 0x1B}, {"enter", 0x1C}, {"lctrl", 0x1D}, {"a", 0x1E},
    {"s", 0x1F}, {"d", 0x20}, {"f", 0x21}, {"g", 0x22}, {"h", 0x23}, {"j", 0x24}, {"k", 0x25}, {"l", 0x26},
    {"semicolon", 0x27}, {"apostrophe", 0x28}, {"grave", 0x29}, {"lshift", 0x2A}, {"backslash", 0x2B}, {"z", 0x2C},
    {"x", 0x2D}, {"c", 0x2E}, {"v", 0x2F}, {"b", 0x30}, {"n", 0x31}, {"m", 0x32}, {"comma", 0x33}, {"period", 0x34},
    {"slash", 0x35}, {"rshift", 0x36}, {"numpadstar", 0x37}, {"lalt", 0x38}, {"space", 0x39}, {"capslock", 0x3A},
    {"f1", 0x3B}, {"f2", 0x3C}, {"f3", 0x3D}, {"f4", 0x3E}, {"f5", 0x3F}, {"f6", 0x40}, {"f7", 0x41}, {"f8", 0x42},
    {"f9", 0x43}, {"f10", 0x44}, {"numpad7", 0x47}, {"numpad8", 0x48}, {"numpad9", 0x49}, {"numpadminus", 0x4A},
    {"numpad4", 0x4B}, {"numpad5", 0x4C}, {"numpad6", 0x4D}, {"numpadplus", 0x4E}, {"numpad1", 0x4F},
    {"numpad2", 0x50}, {"numpad3", 0x51}, {"numpad0", 0x52}, {"numpadperiod", 0x53}, {"f11", 0x57}, {"f12", 0x58},
    {"numpadenter", 0x9C}, {"rctrl", 0x9D}, {"numpadslash", 0xB5}, {"ralt", 0xB8}, {"home", 0xC7}, {"up", 0xC8},
    {"pageup", 0xC9}, {"left", 0xCB}, {"right", 0xCD}, {"end", 0xCF}, {"down", 0xD0}, {"pagedown", 0xD1},
    {"insert", 0xD2}, {"delete", 0xD3}};

std::string lower(std::string s) {
    for (char& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}
bool heldAction(int a) { return (a >= 5 && a <= 10) || (a >= 13 && a <= 17) || a == 31; }

}  // namespace

int deviceNumber(const std::string& name) {
    const std::string l = lower(name);
    for (int i = 0; i < 5; ++i)
        if (l == kDevices[i]) return i;
    return -1;
}
const char* deviceName(int device) { return device >= 0 && device < 5 ? kDevices[device] : "?"; }

int actionNumber(const std::string& name) {
    const std::string l = lower(name);
    for (int i = 0; i < 34; ++i)
        if (l == kActions[i]) return i;
    return -1;
}
const char* actionName(int action) { return action >= 0 && action < 34 ? kActions[action] : "?"; }

bool parseBinding(const std::string& text, int action, BindingText& out, std::string& why) {
    out = BindingText();
    const std::string t = lower(text);
    const size_t colon = t.find(':');
    const std::string kind = t.substr(0, colon), arg = colon == std::string::npos ? std::string() : t.substr(colon + 1);
    const uint32_t keyKind = heldAction(action) ? 1u : 3u;
    if (t == "none") return true;
    if (kind == "key") {
        uint32_t dik = 0;
        for (const KeyName& k : kKeys)
            if (arg == k.name) dik = k.dik;
        if (!dik && arg.size() > 2 && arg[0] == '0' && arg[1] == 'x') dik = uint32_t(std::strtoul(arg.c_str(), nullptr, 16));
        if (!dik || dik > 255) {
            why = "no key " + arg;
            return false;
        }
        out = {keyKind, 3, dik};
        return true;
    }
    if (kind == "button") {
        const int n = std::atoi(arg.c_str());
        if (n < 1 || n > 32) {
            why = "buttons are 1 - 32";
            return false;
        }
        out = {keyKind, 4, uint32_t(20 + n)};
        return true;
    }
    if (kind == "axis") {
        static const char* axes[6] = {"x", "y", "z", "u", "r", "v"};
        for (int i = 0; i < 6; ++i)
            if (arg == axes[i]) {
                out = {2, 4, uint32_t(10 + i)};
                return true;
            }
        why = "axes are x, y, z, r, u, v";
        return false;
    }
    if (kind == "half") {
        static const char* halves[4] = {"x-", "x+", "y-", "y+"};
        for (int i = 0; i < 4; ++i)
            if (arg == halves[i]) {
                out = {2, 4, uint32_t(17 + i)};
                return true;
            }
        why = "half axes are x-, x+, y- (up), y+ (down)";
        return false;
    }
    if (t == "hat") {
        out = {2, 4, 16};
        return true;
    }
    if (kind == "mouse") {
        if (arg == "left") out = {keyKind, 2, 1};
        else if (arg == "right") out = {keyKind, 2, 2};
        else if (arg == "x") out = {2, 2, 10};
        else if (arg == "y") out = {2, 2, 11};
        else {
            why = "mouse inputs are left, right, x, y";
            return false;
        }
        return true;
    }
    why = "an input is key:NAME, button:N, axis:A, half:H, hat, mouse:M or none";
    return false;
}

std::string bindingText(uint32_t kind, uint32_t device, uint32_t component) {
    if (kind == 0) return "none";
    char buf[32];
    if (device == 3) {
        for (const KeyName& k : kKeys)
            if (k.dik == component) return std::string("key:") + k.name;
        std::snprintf(buf, sizeof buf, "key:0x%02x", component);
        return buf;
    }
    if (device == 2) {
        switch (component) {
            case 1: return "mouse:left";
            case 2: return "mouse:right";
            case 10: return "mouse:x";
            case 11: return "mouse:y";
            default: break;
        }
    }
    if (device == 4) {
        static const char* axes[6] = {"x", "y", "z", "u", "r", "v"};
        static const char* halves[4] = {"x-", "x+", "y-", "y+"};
        if (component >= 10 && component <= 15) return std::string("axis:") + axes[component - 10];
        if (component == 16) return "hat";
        if (component >= 17 && component <= 20) return std::string("half:") + halves[component - 17];
        if (component >= 21) {
            std::snprintf(buf, sizeof buf, "button:%u", component - 20);
            return buf;
        }
    }
    std::snprintf(buf, sizeof buf, "?%u/%u/%u", kind, device, component);
    return buf;
}

}  // namespace native
}  // namespace cc
