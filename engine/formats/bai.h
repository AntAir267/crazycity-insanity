// city/<c>.bai ("CAI1"): only what the free-roam start needs, the road flags and the
// intersections (spec/formats/city-files.md, "AI network").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

struct BaiIntersection {
    int index = 0;
    int room = 0;
    vec3 pos;
    std::vector<uint32_t> roads;
};

struct BaiNetwork {
    std::vector<uint16_t> roadFlags;  // by road record order
    std::vector<uint16_t> roadIndex;  // index field of each road record
    std::vector<BaiIntersection> intersections;
    // Flags of the road with this index (searched by its index field), 0 if unknown.
    uint16_t flagsOfRoad(uint32_t road) const;
};

bool parseBai(const uint8_t* data, size_t size, BaiNetwork& out, std::string& err);

// ---- The whole file (spec/formats/bai.md), as stored. ----

struct BaiSide {
    int lanes = 0;         // L
    int sidewalks = 0;     // W
    bool cable = false, subway = false;
    uint16_t flags = 0;    // bit 0: no ambient vehicles, bit 1: no pedestrians
    std::vector<std::vector<float>> distances;  // (L + W) lines × n
    std::vector<float> offsets;                 // L + W (not read by the game)
    float band[10] = {};
    std::vector<std::vector<vec3>> lines;       // (L + W) lines × n points
    std::vector<vec3> cableLine, subwayLine;    // n points when present
    std::vector<vec3> kerb, outerEdge;          // n points each
};

struct BaiEnd {
    uint32_t intersection = 0;
    uint16_t lightSlot = 0;   // junk in the file
    uint16_t control = 3;     // 0 stop, 1 light, 2 yield, 3 none
    uint16_t turnRule = 0;
    uint32_t slot = 0;
    vec3 signPos, facingPoint;
};

struct BaiRoad {
    uint16_t index = 0;
    int n = 0;
    uint16_t flags = 0;
    std::vector<int16_t> rooms;
    float halfWidth = 0, speed = 0;
    BaiSide side[2];  // [0] side A, [1] side B, file order
    std::vector<float> centreDist;
    std::vector<vec3> C, X, Y, Z, T;
    BaiEnd end[2];    // [0] at section n − 1, [1] at section 0
};

struct BaiIntersectionRec {
    uint16_t index = 0;
    uint16_t room = 0;
    vec3 centre;
    std::vector<uint32_t> roads;
};

struct BaiFile {
    bool supplementary = false;
    std::vector<BaiRoad> roads;
    std::vector<BaiIntersectionRec> intersections;          // main file only
    std::vector<std::vector<uint16_t>> ambientTable, pedTable;  // main file only, by room
};

// Reads a main (`supplementary` false) or supplementary file to its last byte. A file whose magic
// is not CAI1 is refused.
bool parseBaiFile(const uint8_t* data, size_t size, bool supplementary, BaiFile& out, std::string& err);

}  // namespace cc
