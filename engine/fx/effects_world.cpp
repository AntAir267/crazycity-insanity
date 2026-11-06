#include "fx/effects_world.h"

#include <cstdlib>

#include <algorithm>

#include "core/bytes.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/dat_text.h"
#include "vfs/vfs.h"

namespace cc {
namespace fx {

namespace {
// The built-in spark table: 4 rows of 8 colours, shift 5 (effects.md, "Sparks").
const uint8_t kBuiltInSparks[4][8][3] = {
    {{51, 0, 0}, {76, 0, 0}, {102, 0, 0}, {127, 0, 0}, {153, 0, 0}, {178, 0, 0}, {204, 0, 0}, {229, 0, 0}},
    {{51, 0, 25}, {76, 0, 25}, {102, 0, 25}, {127, 25, 51}, {153, 51, 76}, {178, 76, 127}, {204, 102, 178}, {229, 153, 204}},
    {{51, 0, 25}, {76, 0, 25}, {102, 0, 25}, {127, 51, 51}, {153, 76, 102}, {178, 102, 127}, {204, 127, 153}, {229, 153, 178}},
    {{76, 0, 0}, {102, 0, 0}, {127, 0, 0}, {153, 0, 0}, {178, 0, 0}, {204, 76, 76}, {229, 153, 153}, {255, 255, 255}},
};

uint32_t packRgba(int r, int g, int b, int a) {
    return uint32_t(r & 255) | (uint32_t(g & 255) << 8) | (uint32_t(b & 255) << 16) | (uint32_t(a & 255) << 24);
}

int log2Exact(int v) {
    if (v <= 0 || (v & (v - 1)) != 0) return -1;
    int n = 0;
    while ((1 << n) < v) ++n;
    return n;
}

// The wheel rules by ptxindex (effects.md, "Wheel particles").
const char* const kWheelRules[8] = {"dirt", "dust", "grass", "leaf", "smoke", "snow", "splash", "rock"};
}  // namespace

SparkTable loadSparkTable(const Vfs& vfs) {
    SparkTable t;
    BytesPtr b = vfs.open("texture/spark.tga");
    if (b && b->size() >= 18) {
        const uint8_t* d = b->data();
        const int idLen = d[0], cmapType = d[1], type = d[2];
        const int w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16];
        const int lw = log2Exact(w), lh = log2Exact(h);
        const int bytes = bpp / 8;
        if (idLen == 0 && cmapType == 0 && type == 2 && lw >= 0 && lh >= 0 && w * h <= 256 && (bpp == 24 || bpp == 32) &&
            b->size() >= size_t(18 + w * h * bytes)) {
            t.w = w;
            t.h = h;
            t.shift = 8 - lw;
            t.fromFile = true;
            for (int i = 0; i < w * h; ++i) {
                const uint8_t* px = d + 18 + i * bytes;  // B, G, R (, A), rows as stored
                t.rgba.push_back(packRgba(px[2], px[1], px[0], bytes == 4 ? px[3] : 128));
            }
            return t;
        }
        logFile(LogLevel::Info, "texture/spark.tga", "not a small uncompressed true-colour TGA; built-in spark colours used");
    }
    t.w = 8;
    t.h = 4;
    t.shift = 5;
    for (const auto& row : kBuiltInSparks)
        for (const auto& c : row) t.rgba.push_back(packRgba(c[0], c[1], c[2], 255));
    return t;
}

bool loadPropParticles(const Vfs& vfs, const std::string& name, PropParticles& out) {
    // Prop data are read by position (particles.md, "Prop data blocks";
    // collision-objects.md, "Reading .dgBangerData"): every key token, the block's name and its
    // braces are skipped unchecked.
    out = PropParticles();
    BytesPtr b = vfs.open("tune/banger/" + name + ".dgBangerData");
    if (!b) return false;
    std::vector<std::string> t;
    {
        const std::string s(b->begin(), b->end());
        size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
            size_t j = i;
            while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
            if (j > i) t.push_back(s.substr(i, j - i));
            i = j;
        }
    }
    size_t pos = 0;
    if (t.size() >= 2 && t[0] == "type:" && t[1] == "a") pos = 2;
    pos += 2;  // normally "dgBangerData {"
    auto have = [&](size_t n) { return pos + n <= t.size(); };
    auto readF = [&](float& v) {
        if (!have(2)) return false;
        v = std::strtof(t[pos + 1].c_str(), nullptr);
        pos += 2;
        return true;
    };
    auto readI = [&](int& v) {
        if (!have(2)) return false;
        v = std::atoi(t[pos + 1].c_str());
        pos += 2;
        return true;
    };
    auto readV = [&](vec3& v) {
        if (!have(4)) return false;
        v = vec3(std::strtof(t[pos + 1].c_str(), nullptr), std::strtof(t[pos + 2].c_str(), nullptr),
                 std::strtof(t[pos + 3].c_str(), nullptr));
        pos += 4;
        return true;
    };
    int ii = 0;
    float ff = 0;
    vec3 vv;
    if (!readI(ii) || !readV(vv) || !readV(vv)) return true;  // AudioId, Size, CG
    int glows = 1;
    if (have(1) && t[pos] == "NumGlows" && !readI(glows)) return true;
    for (int g = 0; g < glows; ++g)
        if (!readV(vv)) return true;  // GlowOffset
    for (int k = 0; k < 4; ++k)
        if (!readF(ff)) return true;  // Mass, Elasticity, Friction, ImpulseLimit2
    for (int k = 0; k < 3; ++k)
        if (!readI(ii)) return true;  // SpinAxis, Flash, NumParts
    // The rule block: its name and '{', then 24 values in this order, then one more token.
    if (!have(2)) return true;
    pos += 2;
    BirthRule& r = out.rule;
    const bool ok = readV(r.position) && readV(r.positionVar) && readV(r.velocity) && readV(r.velocityVar) &&
                    readF(r.life) && readF(r.mass) && readF(r.massVar) && readF(r.radius) && readF(r.radiusVar) &&
                    readF(r.drag) && readF(r.dragVar) && readF(r.dRadius) && readF(r.dRadiusVar) && readI(r.dAlpha) &&
                    readI(r.dAlphaVar) && readI(r.dRotation) && readI(r.dRotationVar) && readI(r.initialBlast) &&
                    readF(r.spewRate) && readF(r.spewTimeLimit) && readF(r.gravity) && readI(r.texFrameStart) &&
                    readI(r.texFrameEnd) && readI(r.birthFlags);
    if (!ok) return true;
    out.hasRule = true;
    if (!have(1)) return true;
    pos += 1;  // the closing brace
    readI(out.texNumber);
    return true;
}

EffectsWorld::EffectsWorld(const Vfs& vfs) : vfs_(vfs), images_(vfs) {}

bool EffectsWorld::hasTexture(const std::string& name) {
    const std::string k = pathKey(name);
    auto it = textures_.find(k);
    if (it != textures_.end()) return it->second;
    const bool e = vfs_.exists("texture/" + name + ".tex") || vfs_.exists("texture/" + name + ".tga") ||
                   vfs_.exists("texture/" + name + ".bmp");
    textures_.emplace(k, e);
    return e;
}

void EffectsWorld::startSession(int weather) {
    for (int i = 0; i < 8; ++i) {
        wheelRules_[i] = BirthRule();
        wheelLoaded_[i] = loadBirthRule(vfs_, std::string("tune/effects/") + kWheelRules[i] + ".asBirthRule", wheelRules_[i]);
    }
    // Rain: the splash rule is copied over the smoke rule for the session.
    if (weather == 3 && wheelLoaded_[6]) {
        wheelRules_[4] = wheelRules_[6];
        wheelLoaded_[4] = true;
    }
    smokeRule_ = BirthRule();
    sparks_ = loadSparkTable(vfs_);
    rainRule_ = BirthRule();
    rainLoaded_ = loadBirthRule(vfs_, "tune/rain.asBirthRule", rainRule_) && hasTexture("ptx_rain");
    rain_.setup(200, 4, 4);
    rain_.rule = rainLoaded_ ? &rainRule_ : nullptr;
    rainShown_ = false;
    props_.clear();
}

void EffectsWorld::addCar(CarFx* c) {
    if (std::find(cars_.begin(), cars_.end(), c) == cars_.end()) cars_.push_back(c);
}

void EffectsWorld::removeCar(CarFx* c) { cars_.erase(std::remove(cars_.begin(), cars_.end(), c), cars_.end()); }

void EffectsWorld::activateProp(int key, const std::string& propName, const vec3& X, const vec3& Y, const vec3& Z,
                                const vec3& P, bool untouched) {
    auto it = propData_.find(pathKey(propName));
    if (it == propData_.end()) {
        PropParticles pp;
        loadPropParticles(vfs_, propName, pp);
        it = propData_.emplace(pathKey(propName), pp).first;
    }
    PropParticles& pp = it->second;
    deactivateProp(key);
    // Textures fxpt1 ... fxpt20; a missing one (or none) emits nothing.
    if (pp.texNumber <= 0 || pp.texNumber > 20 || !pp.hasRule) return;
    if ((pp.rule.birthFlags & 0x01) && !untouched) return;  // only a prop untouched before this hit
    const std::string tex = strf("fxpt%d", pp.texNumber);
    if (!hasTexture(tex)) return;
    auto f = std::make_unique<PropFx>();
    f->key = key;
    f->texture = tex;
    f->emitter.setup(64, 2, 2);
    f->emitter.reset();
    if (pp.rule.birthFlags & 0x01) {
        pp.rule.position = P;  // births sit at the prop's position and do not follow it
    } else {
        f->emitter.setFollow(X, Y, Z, P);
        f->follows = true;
    }
    f->emitter.rule = &pp.rule;
    f->emitter.blast(pp.rule.initialBlast);
    props_.push_back(std::move(f));
}

void EffectsWorld::deactivateProp(int key) {
    props_.erase(std::remove_if(props_.begin(), props_.end(), [key](const std::unique_ptr<PropFx>& p) { return p->key == key; }),
                 props_.end());
}

void EffectsWorld::moveProp(int key, const vec3& X, const vec3& Y, const vec3& Z, const vec3& P) {
    for (auto& p : props_)
        if (p->key == key) {
            if (p->follows) p->emitter.setFollow(X, Y, Z, P);
            p->registered = true;
        }
}

void EffectsWorld::resetProps() { props_.clear(); }

void EffectsWorld::frameUpdate(float F, bool raining) {
    // The emitter lives with its loose body (loose-bodies.md): updated and drawn in frames whose
    // after-samples step registered it; deactivateProp() ends it.
    for (auto& p : props_) {
        p->shown = p->registered;
        if (p->registered) {
            p->active += F;
            p->emitter.update(F);
        }
        p->registered = false;
    }
    if (raining && rainLoaded_) rain_.update(F);
}

}  // namespace fx
}  // namespace cc
