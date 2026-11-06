// Taking an object's parts out of a package (spec/formats/pkg.md, "Part names and levels of
// detail").
#pragma once

#include <string>
#include <vector>

#include "formats/pkg.h"

namespace cc {

enum Lod { LodH = 0, LodM = 1, LodL = 2, LodVL = 3, LodCount = 4 };

const char* lodSuffix(Lod l);  // "H", "M", "L", "VL"

struct PartLods {
    std::string name;
    const Mesh* lod[LodCount] = {nullptr, nullptr, nullptr, nullptr};
    bool any() const { return lod[0] || lod[1] || lod[2] || lod[3]; }
    const Mesh* at(Lod l) const { return lod[l]; }
};

struct PartTable {
    std::vector<PartLods> parts;           // same order as the requested names
    std::vector<std::string> skippedMeshes;  // mesh entries the owner never took
    const PartLods* find(const std::string& name) const;
};

// Asks for each part in `partNames` as <PART>_H, _M, _L, _VL (or H, M, L, VL for "") the way
// MM2 does: the package is read front to back and an entry is taken only if it is the next one
// asked for. Then missing higher levels are filled from lower ones (no L -> VL, no M -> L,
// no H -> M).
PartTable takeParts(const Pkg& pkg, const std::vector<std::string>& partNames);

}  // namespace cc
