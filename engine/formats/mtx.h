// .mtx pivot files (spec/formats/pkg.md): 12 floats = box min, box max, box centre, origin.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "core/math.h"

namespace cc {

struct Pivot {
    bool found = false;
    vec3 boxMin, boxMax, centre;
    vec3 origin;  // where the part's own (0,0,0) goes in the parent's space

    float wheelRadius() const { return (boxMax.y - boxMin.y) * 0.5f; }
    float wheelWidth() const { return boxMax.x - boxMin.x; }
};

bool parseMtx(const uint8_t* data, size_t size, Pivot& out, std::string& err);

// Pivot file name for a part: "<package>_<PART>.mtx", or "<package>_(null).mtx" for an object
// without part names (part == "").
std::string pivotPath(const std::string& package, const std::string& part);

}  // namespace cc
