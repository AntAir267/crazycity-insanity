#include "model/parts.h"

#include "core/strutil.h"

namespace cc {

const char* lodSuffix(Lod l) {
    static const char* const k[] = {"H", "M", "L", "VL"};
    return k[l];
}

const PartLods* PartTable::find(const std::string& name) const {
    for (const auto& p : parts)
        if (iequals(p.name, name)) return &p;
    return nullptr;
}

PartTable takeParts(const Pkg& pkg, const std::vector<std::string>& partNames) {
    PartTable t;
    t.parts.resize(partNames.size());
    size_t cursor = 0;
    const auto& entries = pkg.entries;
    for (size_t pi = 0; pi < partNames.size(); ++pi) {
        PartLods& part = t.parts[pi];
        part.name = partNames[pi];
        for (int l = 0; l < LodCount; ++l) {
            if (cursor >= entries.size()) break;
            std::string want = part.name.empty() ? std::string(lodSuffix(Lod(l)))
                                                 : part.name + "_" + lodSuffix(Lod(l));
            const PkgEntry& e = entries[cursor];
            if (iequals(e.name, want)) {
                if (e.mesh >= 0) part.lod[l] = &pkg.meshes[size_t(e.mesh)];
                ++cursor;
            }
        }
        // Fill missing higher levels from the next lower one, in this order.
        if (!part.lod[LodL]) part.lod[LodL] = part.lod[LodVL];
        if (!part.lod[LodM]) part.lod[LodM] = part.lod[LodL];
        if (!part.lod[LodH]) part.lod[LodH] = part.lod[LodM];
    }
    // Everything from the cursor up to the material table that is a mesh was not taken.
    for (size_t i = cursor; i < entries.size(); ++i)
        if (entries[i].mesh >= 0) t.skippedMeshes.push_back(entries[i].name);
    return t;
}

}  // namespace cc
