// City object placements ".inst" (spec/formats/inst.md).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {

struct InstRecord {
    uint32_t header = 0;
    uint16_t room = 0;
    uint8_t variant = 0;   // material set
    bool landmark = false;     // bit 24
    bool breakable = false;    // bit 25
    bool landmarkDetail = false;  // bit 26
    bool spansRooms = false;   // bit 29
    bool compact = false;      // compact placement (turn about Y + horizontal scale)
    std::string name;          // model name (part after the last backslash)
    float matrix[12];          // X axis, Y axis, Z axis, position (as stored / expanded)
};

bool parseInst(const uint8_t* data, size_t size, std::vector<InstRecord>& out, std::string& err);

// Landmarks keep only position and the X axis' x and z (inst.md, "Kinds").
void flattenLandmarkMatrix(float m[12]);

}  // namespace cc
