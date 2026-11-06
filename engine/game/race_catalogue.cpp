#include "game/race_catalogue.h"

#include "core/strutil.h"

namespace cc {
namespace game {

const char* modeKey(RaceMode m) {
    switch (m) {
        case RaceMode::Cruise: return "roam";
        case RaceMode::Checkpoint: return "race";
        case RaceMode::Circuit: return "circuit";
        case RaceMode::Blitz: return "blitz";
        case RaceMode::CrashCourse: return "crash";
    }
    return "roam";
}

const char* modeTitle(RaceMode m) {
    switch (m) {
        case RaceMode::Cruise: return "Cruise";
        case RaceMode::Checkpoint: return "Checkpoint";
        case RaceMode::Circuit: return "Circuit";
        case RaceMode::Blitz: return "Blitz";
        case RaceMode::CrashCourse: return "Crash Course";
    }
    return "";
}

std::string raceName(RaceMode m, int n) {
    if (m == RaceMode::Cruise) return "roam";
    return strf("%s%d", modeKey(m), n);
}

int raceCount(const CityEntry& city, RaceMode m) {
    switch (m) {
        case RaceMode::Cruise: return 1;
        case RaceMode::Checkpoint: return city.checkpoint;
        case RaceMode::Circuit: return city.circuit;
        case RaceMode::Blitz: return city.blitz;
        case RaceMode::CrashCourse: return 13;
    }
    return 0;
}

std::string raceDisplayName(const CityEntry& city, RaceMode m, int n) {
    const std::vector<std::string>* names = nullptr;
    if (m == RaceMode::Checkpoint) names = &city.checkpointNames;
    else if (m == RaceMode::Circuit) names = &city.circuitNames;
    else if (m == RaceMode::Blitz) names = &city.blitzNames;
    if (names && n >= 0 && size_t(n) < names->size()) return (*names)[size_t(n)];
    if (m == RaceMode::CrashCourse) return strf("Lesson %d", n + 1);
    if (m == RaceMode::Cruise) return "Cruise";
    return strf("%s %d", modeTitle(m), n + 1);
}

std::string settingsPath(const std::string& raceDir, RaceMode m) {
    const char* f = "mmracedata.csv";
    if (m == RaceMode::Circuit) f = "mmcircuitdata.csv";
    else if (m == RaceMode::Blitz) f = "mmblitzdata.csv";
    else if (m == RaceMode::CrashCourse) f = "mmcrashdata.csv";
    return "race/" + raceDir + "/" + f;
}

std::string waypointPath(const std::string& raceDir, RaceMode m, int n) {
    return "race/" + raceDir + "/" + raceName(m, n) + "waypoints.csv";
}

std::string crashEventPath(const std::string& raceDir, int n, bool pro) {
    return strf("race/%s/crash%ddata%s.csv", raceDir.c_str(), n, pro ? "_p" : "");
}

std::string raceAimapPath(const std::string& raceDir, const std::string& race, bool pro) {
    return "race/" + raceDir + "/" + race + (pro ? ".aimap_p" : ".aimap");
}

}  // namespace game
}  // namespace cc
