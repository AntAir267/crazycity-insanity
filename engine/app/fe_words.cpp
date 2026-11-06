#include "app/fe_words.h"

#include <cmath>
#include <cstdio>
#include <map>

#include "app/menu_data.h"
#include "core/strutil.h"

namespace cc {
namespace fe {

namespace {

const std::map<int, std::string>& english() {
    static std::map<int, std::string> en;
    if (!en.empty()) return en;
    auto put = [&](int k, const char* v) { en[k] = v; };
    put(3, "You"); put(5, "Circuit"); put(6, "Checkpoint"); put(7, "Blitz"); put(12, "Crash Course");
    put(30, "In the water!"); put(642, "In the river!"); put(643, "In the bay!");
    put(62, "Final lap"); put(63, "Lap"); put(65, "Driver"); put(66, "Player"); put(77, "Player");
    put(78, "Crash Course"); put(79, "Cops and Robbers"); put(80, "Cruise"); put(81, "Professional"); put(82, "Amateur");
    put(158, "Ready"); put(159, "Set"); put(160, "Go!"); put(161, "Out of time"); put(162, "Wrecked"); put(164, "Blitz complete!");
    put(165, "Ready"); put(166, "Set"); put(167, "Go!"); put(168, "Wrecked: repairs under way"); put(177, "Finished");
    put(179, "Ready"); put(180, "Set"); put(181, "Go!"); put(182, "Wrecked"); put(191, "Finished");
    put(428, "Driver"); put(429, "Car"); put(430, "Paint"); put(499, "Did not finish");
    put(574, "Low"); put(575, "Medium"); put(576, "High"); put(577, "Very high");
    put(580, "Mouse"); put(581, "Keyboard"); put(582, "Joystick"); put(583, "Gamepad"); put(584, "Steering wheel");
    put(606, "Off"); put(607, "On"); put(633, "Manual"); put(634, "Automatic");
    put(635, "Skill"); put(636, "Last race"); put(637, "Last car"); put(638, "Controller"); put(640, "Score");
    put(651, "Passed"); put(660, "None"); put(661, "Low");
    static const char* ord[8] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
    for (int i = 0; i < 8; ++i) {
        en[13 + i] = "Racer " + std::to_string(i + 1);
        en[21 + i] = ord[i];
        en[169 + i] = std::string("Finished ") + ord[i];
        en[183 + i] = std::string("Finished ") + ord[i];
    }
    for (int i = 0; i < 13; ++i) {
        en[532 + i] = "Lesson " + std::to_string(i + 1);
        en[545 + i] = "Lesson " + std::to_string(i + 1);
    }
    static const char* actions[kActions] = {"Map mode", "Full-screen map", "Map zoom", "Map orientation", "Gauges", "Steering axis",
                                            "Steer left", "Steer right", "Accelerate", "Brake / reverse", "Handbrake", "Camera",
                                            "Orbit view", "Horn", "Look left", "Look right", "Look back", "Look forward",
                                            "Wide view", "Cockpit view", "Gearbox mode", "Shift up", "Shift down", "Reverse",
                                            "Next waypoint", "Previous waypoint", "CD player", "CD play / stop", "CD previous",
                                            "CD next", "Mirror", "Look-around axis", "Opponent icons", "Chat"};
    for (int i = 0; i < kActions; ++i) en[kActionText[i]] = actions[i];
    put(585, "Blitz"); put(586, "Circuit"); put(587, "Checkpoint"); put(588, "Cruise");
    put(629, "Morning"); put(630, "Noon"); put(631, "Evening"); put(632, "Night");
    put(625, "Clear"); put(626, "Cloudy"); put(627, "Fog"); put(628, "Rain");
    put(254, "Place"); put(255, "Checkpoints"); put(259, "Place"); put(260, "Checkpoint"); put(261, "Lap");
    put(348, "Race statistics"); put(349, "Score"); put(350, "Time"); put(351, "Race"); put(353, "Driver"); put(355, "Car");
    put(344, "Points"); put(345, "Race"); put(346, "Time"); put(347, "Car");
    put(271, "Undefined"); put(272, "Button"); put(273, "Controller"); put(310, "Left mouse button"); put(311, "Right mouse button");
    put(312, "Mouse X axis"); put(313, "Mouse Y axis"); put(314, "X axis"); put(315, "X axis left"); put(316, "X axis right");
    put(317, "Y axis"); put(318, "Y axis up"); put(319, "Y axis down"); put(320, "Z axis"); put(321, "U axis"); put(322, "R axis");
    put(323, "V axis"); put(324, "Hat");
    // Multiplayer: the mode list, sub-modes, team names, waiting lines and the network screens'
    // texts (mmlang.md, "Network screens"; MM2's buttons there are pictures, ours our own words).
    put(589, "Cops and Robbers"); put(8, "Free-for-all"); put(9, "Cops and Robbers"); put(10, "Robber teams");
    put(264, "Cops"); put(265, "Robbers"); put(266, "Blue"); put(267, "Red");
    for (int n = 1; n <= 7; ++n) en[30 + n] = "Waiting for " + std::to_string(n) + (n > 1 ? " players" : " player");
    put(438, "Pick a game to join"); put(657, "Looking for games…"); put(83, "Another version of the game: it cannot be joined");
    put(437, "noname"); put(68, "has joined"); put(69, "has left"); put(70, "You are now the host");
    put(71, "The game started in a city you do not have"); put(72, "You have been disconnected"); put(73, "Back to the lobby");
    put(84, "The game's city is not installed"); put(85, "You cannot join");
    put(86, "The game's city is not installed: you have been disconnected"); put(38, "has left the game"); put(45, "Back to the lobby");
    put(87, "City:"); put(88, "Event:"); put(398, "Say something to everyone (Enter sends)");
    put(407, "City:"); put(406, "City: not installed"); put(399, "Cruise"); put(400, "Free-for-all"); put(401, "Cops and Robbers");
    put(402, "Robber teams"); put(408, "Weather: clear"); put(624, "Weather: cloudy"); put(409, "Weather: fog"); put(410, "Weather: rain");
    put(412, "Time: morning"); put(413, "Time: noon"); put(414, "Time: evening"); put(415, "Time: night"); put(418, "Laps");
    put(423, "Gold"); put(420, "weightless"); put(421, "100 kg"); put(422, "200 kg"); put(427, "Limit"); put(424, "points");
    put(425, "minutes"); put(426, "none"); put(509, "Gold"); put(506, "Weightless"); put(507, "100 kg"); put(508, "200 kg");
    put(510, "5 min"); put(511, "10 min"); put(512, "20 min"); put(513, "30 min");
    put(514, "100"); put(515, "250"); put(516, "500"); put(517, "1,000");
    put(431, "Players"); put(432, "Waiting for the host to start…"); put(433, "Start the race"); put(435, "Server");
    put(478, "Quit"); put(479, "Everyone back to the lobby"); put(641, "Leave the game"); put(481, "End the session"); put(482, "Cancel");
    put(500, "Roster"); put(501, "Host"); put(494, "Show roster"); put(446, "Chat");
    return en;
}

}  // namespace

const int kActionText[kActions] = {296, 298, 299, 300, 297, 282, 283, 284, 280, 281, 307, 276, 277, 279, 286, 285, 287,
                                   288, 289, 290, 278, 291, 292, 293, 294, 295, 301, 302, 304, 303, 305, 306, 308, 309};
bool heldAction(int a) { return (a >= 5 && a <= 10) || (a >= 13 && a <= 17) || a == 31; }

bool playerWords() { return menu::strings().loaded(); }

std::string T(int id) {
    if (menu::strings().loaded()) {
        const std::string& s = menu::strings().get(id);
        if (!s.empty()) return s;
    }
    const auto& en = english();
    auto it = en.find(id);
    return it == en.end() ? std::string() : it->second;
}

std::string T(int id, const std::string& fallback) {
    if (menu::strings().loaded()) {
        const std::string& s = menu::strings().get(id);
        if (!s.empty()) return s;
    }
    return fallback;
}

std::string label(int id) {
    std::string t = T(id);
    while (!t.empty() && (t.back() == ' ' || t.back() == ':' || t.back() == '\t')) t.pop_back();
    // The full-width colon (U+FF1A).
    while (t.size() >= 3 && t.compare(t.size() - 3, 3, "\xEF\xBC\x9A") == 0) t.erase(t.size() - 3);
    return t;
}

std::string labelled(int id, const std::string& v) {
    std::string t = T(id);
    std::string trimmed = t;
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) trimmed.pop_back();
    const bool colon = (!trimmed.empty() && trimmed.back() == ':') ||
                       (trimmed.size() >= 3 && trimmed.compare(trimmed.size() - 3, 3, "\xEF\xBC\x9A") == 0);
    return colon ? trimmed + " " + v : t + " " + v;
}

std::string modeName(int m) {
    switch (m) {
        case 0: return T(80);
        case 1: return T(6);
        case 3: return T(5);
        case 4: return T(7);
        case 6: return T(12);
        default: return std::string();
    }
}
std::string listName(int m) {
    switch (m) {
        case 4: return T(585);
        case 3: return T(586);
        case 1: return T(587);
        case 0: return T(588);
        case 6: return T(12);
        default: return std::string();
    }
}
std::string skillName(int s) { return T(s ? 81 : 82); }

std::string fmtTime(double t) {
    if (!(t > 0)) return "–:––:––";
    const double tp = t + 0.005;
    const long long whole = (long long)std::floor(tp);
    const int hh = int(std::floor((tp - double(whole)) * 100.0));
    const long long M = whole / 60;
    const long long S = whole - 60 * M;
    char buf[48];
    std::snprintf(buf, sizeof buf, "%lld:%02lld:%02d", M, S, hh);
    return buf;
}

std::string fmtThousands(long long v) {
    const bool neg = v < 0;
    std::string d = std::to_string(neg ? -v : v);
    std::string out;
    int n = 0;
    for (size_t i = d.size(); i-- > 0;) {
        out.insert(out.begin(), d[i]);
        if (++n % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return neg ? "-" + out : out;
}

std::string pct(double v) { return std::to_string((long long)std::llround(v * 100.0)) + "%"; }

std::string fmtSecs(double t) {
    if (!(t > 0)) return "none";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", int(std::floor(t / 60.0)), int(std::llround(std::fmod(t, 60.0))));
    return buf;
}

std::string fmt2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string bindingName(const JValue& b) { return bindingName(b[0].integer(), b[1].integer(), b[2].integer()); }

std::string bindingName(int kind, int dev, int comp) {
    static std::map<int, std::string> scan;
    if (scan.empty()) {
        const std::pair<int, const char*> fixed[] = {
            {0x01, "Esc"},       {0x0E, "Backspace"}, {0x0F, "Tab"},       {0x1C, "Enter"},    {0x1D, "Ctrl"},      {0x2A, "Left Shift"},
            {0x36, "Right Shift"}, {0x38, "Alt"},     {0x39, "Space"},     {0xC8, "↑"},        {0xD0, "↓"},         {0xCB, "←"},
            {0xCD, "→"},         {0xC9, "Page Up"},   {0xD1, "Page Down"}, {0xC7, "Home"},     {0xCF, "End"},       {0xD2, "Insert"},
            {0xD3, "Delete"},    {0x9C, "Num Enter"}, {0x47, "Num 7"},     {0x48, "Num 8"},    {0x49, "Num 9"},     {0x4A, "Num −"},
            {0x4B, "Num 4"},     {0x4C, "Num 5"},     {0x4D, "Num 6"},     {0x4E, "Num +"},    {0x4F, "Num 1"},     {0x50, "Num 2"},
            {0x51, "Num 3"},     {0x52, "Num 0"},     {0x53, "Num ."},     {0x37, "Num *"},    {0xB5, "Num /"},     {0x0C, "-"},
            {0x0D, "="},         {0x1A, "["},         {0x1B, "]"},         {0x27, ";"},        {0x28, "'"},         {0x29, "`"},
            {0x2B, "\\"},        {0x33, ","},         {0x34, "."},         {0x35, "/"},        {0x56, "\\ (ISO)"},   {0x3A, "Caps Lock"},
            {0x57, "F11"},       {0x58, "F12"},       {0x9D, "Right Ctrl"}, {0xB8, "Right Alt"}, {0xDB, "Left Win"}, {0xDC, "Right Win"},
            {0xDD, "Menu"},      {0xC5, "Pause"},     {0xB7, "Print Screen"}, {0x45, "Num Lock"}, {0x46, "Scroll Lock"}};
        for (const auto& p : fixed) scan[p.first] = p.second;
        const char* digits = "1234567890";
        for (int i = 0; i < 10; ++i) scan[0x02 + i] = std::string(1, digits[i]);
        const char* r1 = "QWERTYUIOP";
        for (int i = 0; i < 10; ++i) scan[0x10 + i] = std::string(1, r1[i]);
        const char* r2 = "ASDFGHJKL";
        for (int i = 0; i < 9; ++i) scan[0x1E + i] = std::string(1, r2[i]);
        const char* r3 = "ZXCVBNM";
        for (int i = 0; i < 7; ++i) scan[0x2C + i] = std::string(1, r3[i]);
        for (int i = 0; i < 10; ++i) scan[0x3B + i] = "F" + std::to_string(i + 1);
    }
    if (kind && dev == 3) {
        auto it = scan.find(comp);
        if (it != scan.end()) return it->second;
        char buf[24];
        std::snprintf(buf, sizeof buf, "Key 0x%02X", comp);
        return buf;
    }
    if (kind && dev == 2) {
        const int m = comp == 1 ? 310 : comp == 2 ? 311 : comp == 10 ? 312 : comp == 11 ? 313 : 0;
        if (m) return T(m);
    }
    if (kind && dev == 4) {
        if (comp >= 21) return T(273) + " " + T(272) + " " + std::to_string(comp - 20);
        static const std::map<int, int> axes = {{10, 314}, {11, 317}, {12, 320}, {13, 321}, {14, 322}, {15, 323},
                                                {16, 324}, {17, 315}, {18, 316}, {19, 318}, {20, 319}};
        auto it = axes.find(comp);
        if (it != axes.end()) return T(273) + " " + T(it->second);
    }
    return T(271);
}

std::string netModeName(int m) {
    switch (m) {
        case 0: return T(588);
        case 1: return T(587);
        case 3: return T(586);
        case 4: return T(585);
        case 2: return T(589);
        default: return std::to_string(m);
    }
}
std::string subName(int k) { return T(8 + (k < 0 ? 0 : k)); }
std::string teamName(int sub, int t) { return T((sub == 2 ? 266 : 264) + (t & 1)); }
bool isRaceMode(int m) { return m == 1 || m == 3 || m == 4; }

}  // namespace fe
}  // namespace cc
