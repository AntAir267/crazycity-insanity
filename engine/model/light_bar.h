// A car's light bar (spec/ai/police.md, "Light bar and siren"): one light per SRN0…SRN3 part, the
// spinning beams, the flashing headlights and the lens flare elements of the car.
#pragma once

#include <array>
#include <vector>

#include "core/math.h"

namespace cc {

struct VehicleModel;

struct SirenLight {
    vec3 pos;    // model space (the part's pivot origin)
    vec3 color;  // diffuse RGB of the part's first H batch material
    vec3 beam;   // world-space direction (never turned with the car)
};

struct FlareElement {
    float p = 0, z = 0, b = 0, m = 0;
    vec3 c;
};

struct LightBar {
    std::vector<SirenLight> lights;
    bool on = false;
    vec3 head[2] = {vec3(0, 0, -1), vec3(0, 0, -1)};  // HEADLIGHT0/1 beam directions
    std::array<FlareElement, 20> flare{};

    // Lights from the SRN parts in the given paint set; flare elements from a generator x = 1.
    void setup(const VehicleModel& m, int materialSet);
    bool any() const { return !lights.empty(); }
    // Once per frame: the beams turn by (F × 2.5) × π about Y while on; the headlight beams spin
    // by ±F × 42.411503 while on, else they follow the car's forward direction.
    void update(float F, const vec3& carForward);
};

}  // namespace cc
