#include "game/hud_map.h"

#include <cstdlib>
#include <vector>

namespace cc {
namespace game {

namespace {

struct Tok {
    std::string t;
    int line = 0;
    bool quoted = false;
};

// Whitespace-separated tokens; a token starting with a double quote runs to the next quote and
// is one token without the quotes.
std::vector<Tok> tokens(const std::string& s) {
    std::vector<Tok> out;
    int line = 1;
    size_t i = 0;
    auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
    while (i < s.size()) {
        if (s[i] == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (ws(s[i])) {
            ++i;
            continue;
        }
        Tok t;
        t.line = line;
        if (s[i] == '"') {
            size_t j = s.find('"', i + 1);
            if (j == std::string::npos) j = s.size();
            t.t = s.substr(i + 1, j - i - 1);
            t.quoted = true;
            for (size_t k = i; k < j; ++k)
                if (s[k] == '\n') ++line;
            i = j + 1;
        } else {
            size_t j = i;
            while (j < s.size() && !ws(s[j])) ++j;
            t.t = s.substr(i, j - i);
            i = j;
        }
        out.push_back(t);
    }
    return out;
}

float num(const std::string& t) { return float(std::strtod(t.c_str(), nullptr)); }

int inum(const std::string& t) {
    size_t i = 0;
    bool neg = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) neg = t[i++] == '-';
    long v = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
        v = v * 10 + (t[i++] - '0');
        if (v > 0x7fffffff) v = 0x7fffffff;
    }
    return int(neg ? -v : v);
}

}  // namespace

bool parseHudMapTune(const std::string& text, HudMapTune& o) {
    const std::vector<Tok> tk = tokens(text);
    if (tk.size() < 2 || tk[0].t != "type:" || tk[1].t != "a") return false;
    size_t p = 3;  // root name not checked
    if (p < tk.size() && tk[p].t == "{") ++p;
    auto next = [&](float& f) {
        if (p < tk.size()) f = num(tk[p++].t);
    };
    struct F {
        const char* key;
        float* v;
    };
    const F floats[] = {{"ZoomInDist", &o.zoomInDist},         {"ZoomOutDist", &o.zoomOutDist},
                        {"ZoomInDistFS", &o.zoomInDistFS},     {"ZoomOutDistFS", &o.zoomOutDistFS},
                        {"IconScaleMin", &o.iconScaleMin},     {"IconScaleMax", &o.iconScaleMax},
                        {"IconScaleMinFS", &o.iconScaleMinFS}, {"IconScaleMaxFS", &o.iconScaleMaxFS},
                        {"Approach Rate", &o.approachRate}};
    while (p < tk.size()) {
        const Tok k = tk[p++];
        if (!k.quoted && k.t == "}") break;
        if (!k.quoted && k.t == "{") continue;
        bool known = true;
        if (k.t == "Size") {
            next(o.sizeX);
            next(o.sizeY);
        } else if (k.t == "Pos") {
            next(o.posX);
            next(o.posY);
        } else if (k.t == "ZoomIn") {
            if (p < tk.size()) {
                o.zoomIn = inum(tk[p++].t);
                o.hasZoomIn = true;
            }
        } else if (k.t == "Ocean Color") {
            next(o.ocean.x);
            next(o.ocean.y);
            next(o.ocean.z);
            o.hasOcean = true;
        } else {
            known = false;
            for (const F& f : floats)
                if (k.t == f.key) {
                    next(*f.v);
                    known = true;
                }
        }
        if (known) continue;
        // Unknown key: skip its block, or the rest of its line.
        if (p < tk.size() && tk[p].t == "{") {
            int depth = 1;
            ++p;
            while (p < tk.size() && depth > 0) {
                if (tk[p].t == "{") ++depth;
                else if (tk[p].t == "}") --depth;
                ++p;
            }
        } else {
            while (p < tk.size() && tk[p].line == k.line) ++p;
        }
    }
    return true;
}

MapRect mapRect(int mode, int W, int H, const HudMapTune& t, bool rhdDash) {
    MapRect r;
    const int x = int(float(W) * t.posX), y = int(float(H) * t.posY);
    const int w = int(float(W) * t.sizeX), h = int(float(H) * t.sizeY);
    if (mode == 1) {
        r = {rhdDash ? 0 : x, y, w - 10, h - 10};
    } else if (mode == 2) {
        r = {0, int(float(H) * 0.5f), W, int(float(H) * 0.5f)};
    } else if (mode == 3) {
        r = {0, 0, W, H};
    }
    return r;
}

MapRect viewRect(int mode, int W, int H, const HudMapTune& t) {
    if (mode == 2) return {0, 0, W, int(float(H) * 0.5f)};
    if (mode == 3)
        return {int(float(W) * t.posX), int(float(H) * t.posY), int(float(W) * t.sizeX), int(float(H) * t.sizeY)};
    return {0, 0, W, H};
}

float mapAspect(int mode) { return mode == 2 ? 2.5f : 1.25f; }

void mapBackground(const std::string& city, int rgb[3]) {
    std::string l;
    for (char c : city) l += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
    const float c[3] = {0.92f, 0.84f, 0.778f}, d[3] = {0.084f, 0.68f, 0.92f};
    for (int i = 0; i < 3; ++i) rgb[i] = int((l == "london" ? c[i] : d[i]) * 255.0f);
}

// ---------------------------------------------------------------------------------------------

void HudMapState::resetToSaved(int saved) {
    if (!available_) saved = 0;
    if (saved < 0 || saved > 3) saved = 0;
    before_ = saved == 3 ? 0 : saved;
    setMode(saved);
}

void HudMapState::setMode(int m) {
    if (!available_) m = 0;
    mode_ = m;
    h_ = targetHeight();
    s_ = targetSize();
}

bool HudMapState::tab(bool& leftFullScreen) {
    leftFullScreen = false;
    if (!available_) return false;
    if (mode_ == 3) {
        leftFullScreen = true;
        setMode(before_);
    } else {
        setMode(mode_ == 0 ? 1 : mode_ == 1 ? 2 : 0);
    }
    return true;
}

bool HudMapState::fullScreen(bool& toggled) {
    toggled = false;
    if (!available_) return false;
    if (mode_ == 3) setMode(before_);
    else {
        before_ = mode_;
        setMode(3);
    }
    toggled = true;
    return true;
}

float HudMapState::targetHeight() const {
    if (mode_ == 3) return zoomedIn_ ? tune_.zoomInDistFS : tune_.zoomOutDistFS;
    return zoomedIn_ ? tune_.zoomInDist : tune_.zoomOutDist;
}

float HudMapState::targetSize() const {
    if (mode_ == 3) return zoomedIn_ ? tune_.iconScaleMinFS : tune_.iconScaleMaxFS;
    return zoomedIn_ ? tune_.iconScaleMin : tune_.iconScaleMax;
}

void HudMapState::update(float dt) {
    const bool fs = mode_ == 3;
    const float hr = ((fs ? tune_.zoomOutDistFS - tune_.zoomInDistFS : tune_.zoomOutDist - tune_.zoomInDist)) *
                     tune_.approachRate;
    const float sr = ((fs ? tune_.iconScaleMaxFS - tune_.iconScaleMinFS : tune_.iconScaleMax - tune_.iconScaleMin)) *
                     tune_.approachRate;
    auto toward = [dt](float& v, float target, float rate) {
        const float step = (rate < 0 ? -rate : rate) * dt;
        if (v < target) v = (v + step > target) ? target : v + step;
        else if (v > target) v = (v - step < target) ? target : v - step;
    };
    toward(h_, targetHeight(), hr);
    toward(s_, targetSize(), sr);
}

void carMarkerCorners(const vec3& pos, const vec3& f, const vec3& r, float s, vec3 out[3]) {
    const vec3 c = pos + vec3(0, 15.0f, 0);
    out[0] = c + f * s;                       // tip (car space z = −1)
    out[1] = c - f * s + r * (0.7f * s);      // (+0.7, +1)
    out[2] = c - f * s - r * (0.7f * s);      // (−0.7, +1)
}

}  // namespace game
}  // namespace cc
