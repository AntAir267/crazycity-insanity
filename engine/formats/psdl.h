// City geometry files ".psdl" ("PSD0", spec/formats/psdl.md): point and height tables, texture
// slots, rooms with their element words, room flags and the AI road list. Turning elements into
// triangles is city/psdl_geometry.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

enum PsdlRoomFlag : uint8_t {
    PsdlEnclosed = 0x02,
    PsdlBlock = 0x04,
    PsdlRoad = 0x08,
    PsdlIntersection = 0x10,
    PsdlStacked = 0x40,
    PsdlAlwaysNeighbour = 0x80,
};

struct PsdlRoom {
    std::vector<uint16_t> perimeter;   // point indices, counter-clockwise from above
    std::vector<uint16_t> neighbours;  // room across the edge from perimeter[i] to [i+1]
    std::vector<uint16_t> words;       // element words
    uint8_t flags = 0;
    uint8_t propRule = 0;
};

struct PsdlAiRoad {
    uint32_t flags = 0;
    std::vector<uint32_t> lanes;  // a + b values
    uint8_t lanesA = 0, lanesB = 0;
    std::vector<int16_t> rooms;
};

struct Psdl {
    std::vector<vec3> points;
    std::vector<float> heights;
    std::vector<std::string> textures;  // slot 0 = "" (implicit, empty); names as stored
    std::vector<PsdlRoom> rooms;        // room 0 = implicit, empty
    uint32_t firstNonBlockRoom = 0;
    vec3 boundsMin, boundsMax, refPoint;
    float radius = 0;
    std::vector<PsdlAiRoad> aiRoads;

    // Texture name for lookup: "-0nnn" frame suffixes are dropped (animated textures).
    static std::string lookupName(const std::string& slotName);
};

bool parsePsdl(const uint8_t* data, size_t size, Psdl& out, std::string& err);

}  // namespace cc
