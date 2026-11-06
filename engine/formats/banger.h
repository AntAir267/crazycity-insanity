// tune/banger/<prop>.dgBangerData (spec/physics/collision-objects.md, "Reading .dgBangerData";
// the glow offsets are used by the night glows, spec/formats/night.md).
#pragma once

#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

class Vfs;

struct BangerData {
    vec3 size{0.2f, 0.5f, 0.2f}, cg;
    std::vector<vec3> glowOffsets;  // only when the file lists NumGlows
    float mass = 50.0f, elasticity = 0.5f, friction = 0.9f, impulseLimit2 = 0, yRadius = 0;
    int collisionPrim = 0, collisionType = 0x10;
    int numParts = 0;   // pieces it breaks into (loose-bodies.md, 3)
    int billFlags = 0;  // drawing flags (street-props.md, "Drawing")
    int colliderId = 0;  // the prop's impact sound id (tyre-impact-sounds.md); 0 when the key is missing
};

// Values in a fixed order, mostly by position (the key token before each value is skipped
// unchecked); NumGlows and the three trailing keys are read only when they are the next token.
// False if the file does not exist.
bool loadBangerData(const Vfs& vfs, const std::string& name, BangerData& out);

}  // namespace cc
