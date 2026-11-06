// Small city text/binary files (spec/formats/city-files.md, psdl.md): sky, fog, lighting, room
// colours, water.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "core/math.h"

namespace cc {

// city/<c>.sky: "<model> <height offset> <follow> <spin>"
struct SkyInfo {
    std::string model;
    float heightOffset = 0;
    float follow = 0;  // 0 = fixed height, 1 = moves up and down with the camera
    float spin = 0;    // radians per second about +Y
};
bool parseSky(const std::string& text, SkyInfo& out);

// city/<c>_fog.csv: header, then rows r,g,b,start,end,description (index = time of day x 4 + weather).
struct FogRow {
    vec3 color;  // 0..1
    float start = 0, end = 0;
};
std::vector<FogRow> parseFogCsv(const std::string& text);

// city/<c>.ltNN: key + two fill lights and the ambient colour.
struct CityLights {
    vec3 dir[3];    // direction each light travels (key, fill 1, fill 2)
    vec3 color[3];  // 0..1
    uint32_t ambient = 0xFF000000;  // ARGB
    int found = 0;  // number of keys read
};
bool parseLightFile(const std::string& text, CityLights& out);

// city/<c>.lmap ("LMP0"): one ARGB colour per room.
bool parseLmap(const uint8_t* data, size_t size, std::vector<uint32_t>& out);

// city/<c>.water: water level, then deep-water room numbers.
struct WaterInfo {
    float level = 0;
    std::vector<int> rooms;
};
bool parseWater(const std::string& text, WaterInfo& out);

// Time-of-day/weather index used by the sky material set, fog rows and .ltNN files.
inline int conditionIndex(int timeOfDay, int weather) { return timeOfDay * 4 + weather; }
const char* conditionName(int index);  // "clear-morning", ...

}  // namespace cc
