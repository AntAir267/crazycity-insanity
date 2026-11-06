// The visual effects of one car with a car simulation (player, opponents, police), told about the
// car's physics samples and counted impacts through phys::CarEffectsHook:
//   - texture damage marks, smoke level and point, wobbling wheels (formats/damage-visuals.md);
//   - wheel dust/smoke, engine smoke and exhaust puffs, sparks, body chips, skid marks
//     (engine/effects.md).
// Drawing only: nothing here changes the car, and every random number comes from the effects
// generator, in the order effects.md gives.
#pragma once

#include <memory>
#include <string>

#include "core/math.h"
#include "fx/damage_textures.h"
#include "fx/particles.h"
#include "physics/car.h"

namespace cc {

class Vfs;
struct VehicleModel;

namespace fx {

class EffectsWorld;
class ImageSource;

// The visual keys of tune/vehicle/<car>.vehCarDamage (damage-visuals.md).
struct DamageVisualTune {
    float textelDamageRadius = 0.4f;
    vec3 smokeOffset, smokeOffset2;
    int doublePivot = 0;
    bool found = false;
};
DamageVisualTune loadDamageVisualTune(const Vfs& vfs, const std::string& baseName);

// Sparks (effects.md, "Sparks"): line segments, 64 per car.
struct Spark {
    vec3 p, t, v;
    int c = 0;      // colour row offset
    int life = 0;   // 0 … 255
    uint32_t color = 0xffffffffu;
};

// Body chips (effects.md, "Body chips"): a ring of 16 small textured triangles.
struct Chip {
    float age = 3.4028235e38f;  // the largest float: not drawn
    vec3 V, axis{0, 1, 0}, P;
    float spin = 0;
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};  // orientation rows
    float u0 = 0.5f, v0 = 0.5f;
};

// One wheel's skid marks (effects.md, "Skid marks"): a ring of 64 entries.
struct SkidRing {
    struct Entry {
        vec3 L, R;
        bool tex = false;
        float v = 0;
    };
    Entry e[64];
    int head = 0, tail = 0;
    bool cur = false, pending = false;
    vec3 A, D, pendL, pendR;
    float hw = 0, iw = 0;
    void clear() {
        head = tail = 0;
        cur = pending = false;
    }
    void push(const Entry& x);
    void mark(const vec3& p, const vec3& axle, bool tex);
};

class CarFx : public phys::CarEffectsHook {
public:
    CarFx() = default;
    CarFx(const CarFx&) = delete;
    CarFx& operator=(const CarFx&) = delete;
    ~CarFx() override;
    // `tuneName` = the car whose .vehCarDamage is used (its model name). With a world, the car
    // also gets particles, sparks, chips and skid marks; without one, only the damage visuals.
    void setup(EffectsWorld* world, ImageSource& images, const VehicleModel& model, int paintSet,
               const std::string& tuneName, const phys::Car& car);
    void setup(ImageSource& images, const VehicleModel& model, int paintSet, const std::string& tuneName) {
        setupDamageOnly(images, model, paintSet, tuneName);
    }

    void sampleEffects(const phys::Car& car, float dt) override;
    void countedImpact(const phys::Car& car, const phys::DamageState::Slot& slot, float s, float dt) override;
    // Picks up a "clear damage" or car reset that happened outside the physics samples.
    void refresh(const phys::Car& car);

    // Drawing.
    DamageTextures& textures() { return textures_; }
    const DamageTextures& textures() const { return textures_; }
    const float* wheelTilts() const { return tilt_; }  // radians, wheels 0-3
    // Engine smoke: level 0-4, its fxpt8 frame, and the car-space emission point of the last update
    // (relative to the body position, effects.md).
    int smokeLevel() const { return smokeLevel_; }
    static int smokeFrame(int level);
    const vec3& smokePoint() const { return smokePoint_; }
    float strength() const { return strength_; }
    const DamageVisualTune& tune() const { return tune_; }
    const Emitter& wheelEmitter() const { return wheelEm_; }
    const Emitter& smokeEmitter() const { return smokeEm_; }
    bool hasWheelParticles() const { return wheelOn_; }
    bool hasSmoke() const { return smokeOn_; }
    const Spark* sparks() const { return sparks_; }
    int sparkCount() const { return sparkCount_; }
    const Chip* chips() const { return chips_; }
    // The material index chip slot i is textured with.
    int chipMaterial(int i) const;
    int paintSet() const { return paintSet_; }
    const VehicleModel* model() const { return model_; }
    const SkidRing& skids(int w) const { return skids_[w]; }
    bool hasSkids() const { return skidOn_; }
    // Triangles marked by the last round (tests and the debug readout).
    int lastMarked() const { return lastMarked_; }
    int rounds() const { return rounds_; }

private:
    void setupDamageOnly(ImageSource& images, const VehicleModel& model, int paintSet, const std::string& tuneName);
    void clearDamage();
    void carReset();
    void skidStep(const phys::Car& car);
    void wheelParticles(const phys::Car& car, float dt);
    void damageStep(const phys::Car& car, float dt);
    void puff(const phys::Car& car, const vec3& q, float w);
    void sparkBurst(int n, const vec3& P, const vec3& N);
    void sparkStep(float dt);
    void chipBurst(const phys::Car& car, int n, const vec3& P);
    void chipUpdate(float dt);

    EffectsWorld* world_ = nullptr;
    const VehicleModel* model_ = nullptr;
    int paintSet_ = 0;
    DamageVisualTune tune_;
    DamageTextures textures_;
    bool hasTextures_ = false;
    unsigned clears_ = 0, resets_ = 0;
    bool known_ = false;
    // The pending mark request (one round at the next damage update).
    bool markPending_ = false;
    vec3 markPoint_;
    float strength_ = 0.0f;
    int smokeLevel_ = 0;
    vec3 smokePoint_;
    bool pivotToggle_ = false;
    float tilt_[4] = {0, 0, 0, 0};
    int lastMarked_ = 0, rounds_ = 0;
    // Wheel particles.
    bool wheelOn_ = false;
    Emitter wheelEm_;
    float carry_[2] = {0, 0};
    // Engine smoke and exhaust.
    bool smokeOn_ = false;
    Emitter smokeEm_;
    float acc_ = 0;
    int exhaustCount_ = 0;
    vec3 exhaust_[2];
    // Sparks.
    Spark sparks_[64];
    int sparkCount_ = 0;
    float sparkAcc_ = 0;
    // Chips.
    Chip chips_[16];
    int chipNext_ = 0;
    int materialCount_ = 0;
    // Skid marks.
    bool skidOn_ = false;
    SkidRing skids_[4];
};

}  // namespace fx
}  // namespace cc
