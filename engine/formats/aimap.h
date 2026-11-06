// City and race AI settings, `.aimap` text files (spec/formats/aimap.md).
#pragma once

#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

struct AmbientType {
    std::string model;
    float threshold = 0;  // cumulative probability: the first row with threshold > r is picked
    int value = 0;        // third column (no reader known)
};

struct PedPair {
    std::string good, bad;
};

struct CityAimap {
    int pedPool = 100;
    float speedLimit = 15.0f;
    bool haveSubway = false;
    std::string subwayModel;
    int subwayCars = 1;
    bool driveOnLeft = false;
    std::vector<AmbientType> ambient;
    std::vector<PedPair> peds;
    std::string lightSingle = "sp_traflitsingle_f", lightDual = "sp_traflitdual_f";
};

struct RoadException {
    int road = 0;
    float density = 0, speedLimit = 0;
};

struct PoliceEntry {
    std::string model;
    vec3 pos;
    float heading = 0;  // radians as MM2 stores it: degrees × −0.017444445
    int i0 = 0, i1 = 15;
    float f0 = 0.5f, f1 = 50.0f;
};

// One `[Opponent]` row (aimap.md, "Opponent rows"; ../ai/opponents.md).
struct OpponentRow {
    std::string car, file;        // car base name, route file name (as written)
    float throttle = 1.0f;        // field 3, k_thr
    float pad = 50.0f;            // field 5, k_p
    float brake = 0.69999999f;    // field 6, k_b
    float corner = 1.0f;          // field 12, k_c
    bool sw[6] = {false, true, true, true, true, false};  // switches 1 … 6
    int fields = 0;               // fields the row gave (12 in the shipped rows)
};

struct RaceAimap {
    bool found = false;
    bool laneChanges = true;
    float copChaseDistance = 250.0f;
    std::vector<AmbientType> ambient;  // non-empty: replaces the city's
    std::vector<PedPair> peds;
    std::vector<RoadException> exceptions;
    std::vector<PoliceEntry> police;
    int opponentRows = 0, hookmenRows = 0;
    int opponentCount = 0;               // the [Opponent] count line
    std::vector<OpponentRow> opponents;  // the rows read (a row without car and file is skipped)
};

// The reading rules of aimap.md (127-byte lines, prefix headers, value and list sections).
CityAimap parseCityAimap(const std::string& text);
RaceAimap parseRaceAimap(const std::string& text);

}  // namespace cc
