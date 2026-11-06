// Vehicle damage (spec/physics/damage.md): the damage value of a car, its impact slots, and the
// wrecked state. No effect on the physics itself.
#pragma once

#include <string>

#include "core/math.h"

namespace cc {

class Vfs;

namespace phys {

struct DamageTune {
    float maxDamage = 1000000.0f;
    float medDamage = 500000.0f;
    float impactThreshold = 100.0f;
    float regenerateRate = 0.0f;
};

DamageTune loadDamageTune(const Vfs& vfs, const std::string& baseName);

struct DamageState {
    float damage = 0;
    bool enabled = true;
    struct Slot {
        bool used = false;
        int identity = 0;
        float peak = 0, total = 0, timer = 0;
        // Visual only (damage-visuals.md): where and how the impact that opened the slot hit, in
        // world space (point, unit normal pointing away from the other side) and in car space.
        vec3 contact, normal, carContact;
    };
    Slot slots[12];
    // Counts every reset ("clear damage"), so the visuals can see that they must be repaired.
    unsigned clears = 0;

    void reset();
    // Per physics sample, after the car's own update: regeneration and slot timers.
    void sample(float dt, const DamageTune& t);
    // Where an impact hit (visual only).
    struct Contact {
        vec3 world, normal, car;
    };
    // One impact report: J = the impulse applied to this car (weighted), otherMass > 0 when the
    // other side has a rigid body. Returns the slot when the impact is counted (step 5), else null;
    // `strength` receives s. `sound` (if given) is set when the report reaches the count step (a
    // new or a stronger impact) with s > 0.001: an impact sound plays (damage.md step 5).
    const Slot* impact(const DamageTune& t, const vec3& J, int otherIdentity, float otherMass, float selfMass,
                       float speedMph, const Contact& where, float* strength = nullptr, bool* sound = nullptr);
    bool wrecked(const DamageTune& t) const { return damage > t.maxDamage; }
};

}  // namespace phys
}  // namespace cc
