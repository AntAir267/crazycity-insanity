#include "formats/city_files.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "core/strutil.h"

namespace cc {

namespace {
std::vector<std::string> tokens(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : text) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool toFloat(const std::string& s, float& v) {
    char* end = nullptr;
    v = std::strtof(s.c_str(), &end);
    return end && end != s.c_str();
}
}  // namespace

bool parseSky(const std::string& text, SkyInfo& out) {
    auto t = tokens(text);
    if (t.empty()) return false;
    out = SkyInfo();
    out.model = t[0];
    if (t.size() > 1) toFloat(t[1], out.heightOffset);
    if (t.size() > 2) toFloat(t[2], out.follow);
    if (t.size() > 3) toFloat(t[3], out.spin);
    return true;
}

std::vector<FogRow> parseFogCsv(const std::string& text) {
    std::vector<FogRow> rows;
    auto lines = split(text, '\n');
    for (size_t i = 1; i < lines.size(); ++i) {
        std::string line = trim(lines[i]);
        if (line.empty()) break;
        auto f = split(line, ',');
        if (f.size() < 5) break;
        char* end = nullptr;
        long v[5];
        bool ok = true;
        for (int k = 0; k < 5; ++k) {
            std::string s = trim(f[size_t(k)]);
            v[k] = std::strtol(s.c_str(), &end, 10);
            if (end == s.c_str()) ok = false;
        }
        if (!ok) break;  // stops at the first unreadable line
        FogRow r;
        r.color = vec3(float(v[0]) / 255.0f, float(v[1]) / 255.0f, float(v[2]) / 255.0f);
        r.start = float(v[3]);
        r.end = float(v[4]);
        rows.push_back(r);
    }
    return rows;
}

bool parseLightFile(const std::string& text, CityLights& out) {
    out = CityLights();
    auto t = tokens(text);
    float heading[3] = {0, 0, 0}, pitch[3] = {0, 0, 0};
    static const char* const prefix[3] = {"Key", "Fill1", "Fill2"};
    for (size_t i = 0; i < t.size(); ++i) {
        for (int l = 0; l < 3; ++l) {
            std::string p = prefix[l];
            if (t[i] == p + "Heading" && i + 1 < t.size()) { toFloat(t[i + 1], heading[l]); ++out.found; }
            else if (t[i] == p + "Pitch" && i + 1 < t.size()) { toFloat(t[i + 1], pitch[l]); ++out.found; }
            else if (t[i] == p + "Color" && i + 3 < t.size()) {
                toFloat(t[i + 1], out.color[l].x);
                toFloat(t[i + 2], out.color[l].y);
                toFloat(t[i + 3], out.color[l].z);
                ++out.found;
            }
        }
        if (t[i] == "Ambient" && i + 1 < t.size()) {
            long long v = std::strtoll(t[i + 1].c_str(), nullptr, 10);
            out.ambient = uint32_t(int32_t(v));
            ++out.found;
        }
    }
    for (int l = 0; l < 3; ++l) {
        float h = heading[l], p = pitch[l];
        out.dir[l] = vec3(std::cos(h) * std::cos(p), std::sin(p), std::sin(h) * std::cos(p));
    }
    return out.found >= 10;
}

bool parseLmap(const uint8_t* data, size_t size, std::vector<uint32_t>& out) {
    out.clear();
    if (size < 8 || std::memcmp(data, "LMP0", 4) != 0) return false;
    uint32_t n = readU32LE(data + 4);
    if (size - 8 < size_t(n) * 4) return false;
    out.resize(n);
    for (uint32_t i = 0; i < n; ++i) out[i] = readU32LE(data + 8 + size_t(i) * 4);
    return true;
}

bool parseWater(const std::string& text, WaterInfo& out) {
    out = WaterInfo();
    auto t = tokens(text);
    if (t.empty() || !toFloat(t[0], out.level)) return false;
    for (size_t i = 1; i < t.size(); ++i) out.rooms.push_back(std::atoi(t[i].c_str()));
    return true;
}

const char* conditionName(int index) {
    static const char* const tod[4] = {"morning", "noon", "evening", "night"};
    static const char* const wx[4] = {"clear", "cloudy", "foggy", "rainy"};
    static std::string buf;
    if (index < 0 || index >= 16) return "?";
    buf = std::string(wx[index % 4]) + "-" + tod[index / 4];
    return buf.c_str();
}

}  // namespace cc
