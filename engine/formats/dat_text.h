// Angel "datParser" text files ("type: a", then nested `Key value` blocks): tune/vehicle/*,
// tune/*.asNode, tune/camera/*. Reading follows spec/physics/vehicle-tune.md, "Syntax": keys are
// matched case-sensitively against a schema; unknown keys skip their block or the rest of the line.
#pragma once

#include <string>
#include <vector>

#include "core/math.h"

namespace cc {
namespace dat {

enum class FieldType { Int, Float, Vector, Block };

struct Schema;
struct Field {
    const char* key;
    FieldType type;
    void* ptr;             // int*, float* or vec3*
    const Schema* block;   // for FieldType::Block
};

struct Schema {
    std::vector<Field> fields;
    const Field* find(const std::string& k) const;
};

// Parses a whole file. False (+ err) if it does not start with "type: a".
bool parseText(const std::string& text, const Schema& root, std::string* err = nullptr);

}  // namespace dat
}  // namespace cc
