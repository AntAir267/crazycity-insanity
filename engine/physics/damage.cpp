#include "physics/damage.h"

#include <cmath>

#include "core/bytes.h"
#include "formats/dat_text.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

DamageTune loadDamageTune(const Vfs& vfs, const std::string& baseName) {
    DamageTune t;
    BytesPtr b = vfs.open("tune/vehicle/" + baseName + ".vehCarDamage");
    if (!b) return t;
    using dat::FieldType;
    dat::Schema s{{{"MaxDamage", FieldType::Float, &t.maxDamage, nullptr},
                   {"MedDamage", FieldType::Float, &t.medDamage, nullptr},
                   {"ImpactThreshold", FieldType::Float, &t.impactThreshold, nullptr},
                   {"RegenerateRate", FieldType::Float, &t.regenerateRate, nullptr}}};
    dat::parseText(std::string(b->begin(), b->end()), s);
    return t;
}

void DamageState::reset() {
    damage = 0;
    for (Slot& s : slots) s = Slot();
    ++clears;
}

void DamageState::sample(float dt, const DamageTune& t) {
    damage = damage - dt * t.regenerateRate;
    if (damage < 0.0f) damage = 0.0f;
    for (Slot& s : slots) {
        if (!s.used) continue;
        s.timer = s.timer - dt;
        if (s.timer < 0.0f) s = Slot();
    }
}

const DamageState::Slot* DamageState::impact(const DamageTune& t, const vec3& J, int other, float otherMass,
                                             float selfMass, float speedMph, const Contact& where, float* strength,
                                             bool* sound) {
    if (sound) *sound = false;
    if (!enabled) return nullptr;
    const bool body = otherMass > 0.0f;
    float q = 1.0f;
    if (body) q = otherMass / (selfMass + otherMass);
    const float J2 = (J.x * J.x + J.y * J.y) + J.z * J.z;
    const float s = (std::sqrt(J2) * 1.0f) * q;
    if (strength) *strength = s;
    auto count = [&](const Slot* sl) -> const Slot* {
        if (t.impactThreshold < s && (10.0f <= speedMph || body)) {
            damage = s + damage;
            if (damage < 0.0f) damage = 0.0f;
            return sl;
        }
        return nullptr;
    };
    Slot* slot = nullptr;
    for (Slot& x : slots)
        if (x.used && x.identity == other) {
            slot = &x;
            break;
        }
    if (!slot) {
        for (Slot& x : slots)
            if (!x.used) {
                slot = &x;
                break;
            }
        if (!slot) return nullptr;  // no free slot: ignored
        slot->used = true;
        slot->identity = other;
        slot->peak = s;
        slot->total = s;
        slot->timer = 0.2f;
        slot->contact = where.world;
        slot->normal = where.normal;
        slot->carContact = where.car;
        if (sound) *sound = s > 0.001f;
        return count(slot);
    }
    if (1.25f * slot->peak < s) {
        slot->total = s + slot->total;
        slot->peak = s;
        slot->timer = 0.2f;
        if (sound) *sound = s > 0.001f;
        return count(slot);
    }
    if (s <= t.impactThreshold) return nullptr;
    if (speedMph < 10.0f && !body) return nullptr;
    slot->total = s + slot->total;
    damage = s + damage;
    if (damage < 0.0f) damage = 0.0f;
    return nullptr;
}

}  // namespace phys
}  // namespace cc
