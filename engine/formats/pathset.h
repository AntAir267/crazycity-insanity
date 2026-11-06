// Path sets (`.pathset`, magic PTH1; spec/formats/pathset.md): named polylines with a placement
// mode and spacing, and how a path turns into object placements.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

struct PathsetPath {
    std::string name;
    std::vector<vec3> points;
    uint8_t mode = 0;
    uint8_t spacing = 0;  // quarter metres; 0 = 5 m
};

// False if the magic is wrong. A file that ends inside a path keeps the complete paths before it.
bool parsePathset(const uint8_t* data, size_t size, std::vector<PathsetPath>& out, std::string& err);

struct PathPlacement {
    vec3 X, Y, Z, P;
    bool fullMatrix = false;  // mode 2 keeps the full matrix; modes 0 and 1 the turn only
};

// The placements of one path (pathset.md, "Placing a path's objects").
std::vector<PathPlacement> pathPlacements(const PathsetPath& p);

}  // namespace cc
