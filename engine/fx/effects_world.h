// What the effects of one drive share (spec/engine/effects.md): the decoded images, the eight wheel
// rules, the one engine-smoke rule every car writes, the spark colour table, the cars' effects in
// registration order, the prop emitters and the rain. Visual only.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/math.h"
#include "fx/image_source.h"
#include "fx/particles.h"

namespace cc {

class Vfs;

namespace fx {

class CarFx;

// The spark colour table (effects.md, "Sparks"): H rows of W colours (R, G, B, A).
struct SparkTable {
    int w = 8, h = 4, shift = 5;
    bool fromFile = false;
    std::vector<uint32_t> rgba;  // R | G << 8 | B << 16 | A << 24, row by row
    uint32_t at(int i) const { return i >= 0 && size_t(i) < rgba.size() ? rgba[size_t(i)] : 0xffffffffu; }
};
SparkTable loadSparkTable(const Vfs& vfs);

// The prop data's particle keys (tune/banger/<prop>.dgBangerData: TexNumber and BirthRule).
struct PropParticles {
    int texNumber = 0;
    bool hasRule = false;
    BirthRule rule;
};
bool loadPropParticles(const Vfs& vfs, const std::string& name, PropParticles& out);

class EffectsWorld {
public:
    explicit EffectsWorld(const Vfs& vfs);
    // A new drive: rules, colour table and texture checks; `weather` 3 = rain (night.md).
    void startSession(int weather);

    ImageSource& images() { return images_; }
    const Vfs& vfs() const { return vfs_; }
    // Whether a texture exists (cached; an effect whose texture is missing is off).
    bool hasTexture(const std::string& name);

    // Wheel rules by ptxindex 0-7 (null = none loaded).
    BirthRule* wheelRule(int i) { return i >= 0 && i < 8 && wheelLoaded_[i] ? &wheelRules_[i] : nullptr; }
    BirthRule& smokeRule() { return smokeRule_; }
    const SparkTable& sparkTable() const { return sparks_; }

    // Cars with effects, in the order they register their draw items (effects.md, "Drawing").
    void addCar(CarFx* c);
    void removeCar(CarFx* c);
    const std::vector<CarFx*>& cars() const { return cars_; }

    // Props (effects.md, "Props"). Hook for the prop system: a prop knocked loose at this moment,
    // with its current matrix (rows X, Y, Z, centre of gravity P). `key` identifies the prop.
    // `untouched`: the prop stood untouched before this hit (a rule with BirthFlags 0x01 emits only
    // then).
    void activateProp(int key, const std::string& propName, const vec3& X, const vec3& Y, const vec3& Z,
                      const vec3& P, bool untouched = true);
    // The prop system's after-samples step registers each emitter whose body stays a mover, with
    // the prop's current matrix (births follow it): only registered emitters are updated and drawn
    // in that frame.
    void moveProp(int key, const vec3& X, const vec3& Y, const vec3& Z, const vec3& P);
    // The prop system's hook for when a prop stops being active (its particles go with it).
    void deactivateProp(int key);
    // A world reset: every prop stands again.
    void resetProps();
    struct PropFx {
        int key = 0;
        Emitter emitter;
        std::string texture;
        float active = 0;  // seconds since activation
        bool alive = true;
        bool follows = false;
        bool registered = false;  // registered by the prop system for this frame
        bool shown = false;       // updated this frame: drawn in the effects pass
    };
    const std::vector<std::unique_ptr<PropFx>>& props() const { return props_; }

    // Once per frame after the physics: prop emitters, then the rain (before the city is drawn).
    void frameUpdate(float F, bool raining);
    // Rain (effects.md, "Rain"; night.md section 6): the emitter point for the next updates, or
    // hidden this frame.
    void setRainPoint(const vec3& p) { rainRule_.position = p; rainShown_ = true; }
    void hideRain() { rainShown_ = false; }
    bool rainShown() const { return rainShown_ && rainLoaded_; }
    const Emitter& rain() const { return rain_; }

private:
    const Vfs& vfs_;
    ImageSource images_;
    std::unordered_map<std::string, bool> textures_;
    BirthRule wheelRules_[8];
    bool wheelLoaded_[8] = {};
    BirthRule smokeRule_;
    SparkTable sparks_;
    std::vector<CarFx*> cars_;
    std::vector<std::unique_ptr<PropFx>> props_;
    std::unordered_map<std::string, PropParticles> propData_;  // element addresses stay valid
    BirthRule rainRule_;
    bool rainLoaded_ = false, rainShown_ = false;
    Emitter rain_;
};

}  // namespace fx
}  // namespace cc
