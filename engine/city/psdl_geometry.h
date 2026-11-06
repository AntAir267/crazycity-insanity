// Turns a PSDL's element words into triangles (spec/formats/psdl.md, "Element geometry", level 3
// everywhere), with texture coordinates, vertex colours and texture slots. CPU only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"
#include "formats/city_files.h"
#include "formats/psdl.h"

namespace cc {

struct CityVertex {
    vec3 pos;
    vec2 uv;
    uint8_t color[4];  // R, G, B, A
};

// Triangles of one texture slot in one room.
struct PsdlSurface {
    int slot = 0;  // texture slot (0 = untextured)
    std::vector<CityVertex> vertices;
    std::vector<uint32_t> indices;  // triangle list
};

struct PsdlRoomGeometry {
    std::vector<PsdlSurface> surfaces;
    vec3 boundsMin, boundsMax;
    bool empty = true;
    size_t triangles = 0;
};

struct PsdlGeometry {
    std::vector<PsdlRoomGeometry> rooms;  // indexed by room number (0 unused)
    size_t triangles = 0;
    size_t elementCounts[13] = {};
    size_t skippedInvertedWalls = 0;
    size_t spareWordRooms = 0;
    std::vector<std::string> problems;
};

struct PsdlBuildOptions {
    // 64-entry wall light table (ARGB); all white before any lighting is set.
    uint32_t wallLights[64];
    // Per-room colours (ARGB) from the .lmap, used only if the count matches the room count.
    std::vector<uint32_t> roomColors;
    PsdlBuildOptions() {
        for (uint32_t& w : wallLights) w = 0xFFFFFFFFu;
    }
};

// Wall light table from a lighting file (psdl.md, "Wall light table").
void buildWallLightTable(const CityLights& lights, uint32_t out[64]);

PsdlGeometry buildPsdlGeometry(const Psdl& psdl, const PsdlBuildOptions& opt);

}  // namespace cc
