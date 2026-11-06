// Physical surface materials (spec/physics/collision.md, "Materials"): the built-in default, the
// city list from city/materials.mtl, and bound materials shared by name.
#pragma once

#include <string>
#include <vector>

namespace cc {
namespace phys {

struct Material {
    std::string name = "default";
    float elasticity = 0.5f;
    float friction = 1.0f;
    std::string effect = "none";
    int sound = 0;
    float drag = 0.0f;
    float width = 1.0f;
    float height = 0.0f;
    float depth = 0.0f;
    int ptxIndex[2] = {-1, -1};
    float ptxThreshold[2] = {0.25f, 0.5f};
};

// The built-in default material (entry 0 of the city's list).
Material defaultMaterial();

// One `mtl <name> { ... }` block as written (fields left out keep the built-in defaults).
struct MtlBlock {
    Material m;
};

// Parses consecutive mtl blocks from a token stream position (shared by .mtl and .bnd text).
// `pos` is advanced past the blocks. Stops at the first token that is not "mtl".
bool parseMtlBlock(const std::vector<std::string>& tokens, size_t& pos, Material& out);
std::vector<std::string> tokenizeWhitespace(const std::string& text);

class MaterialTable {
public:
    MaterialTable() { reset(); }
    void reset();  // just the built-in default
    // Adds the blocks of city/materials.mtl in file order (names already present are skipped).
    void loadMtl(const std::string& text);
    // Index of a name (case-sensitive), -1 if absent.
    int find(const std::string& name) const;
    // Bound materials: an existing name is reused (the given values are ignored), else added.
    int shareOrAdd(const Material& m);
    const Material& at(int index) const { return list_[size_t(index < 0 || index >= int(list_.size()) ? 0 : index)]; }
    // City polygon material code: 0 and 1 = the default, c >= 1 = entry c - 1.
    const Material& byCode(int code) const { return at(code <= 0 ? 0 : code - 1); }
    size_t size() const { return list_.size(); }

private:
    std::vector<Material> list_;
};

}  // namespace phys
}  // namespace cc
