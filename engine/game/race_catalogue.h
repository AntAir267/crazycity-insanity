// The races a city offers and the files of each race (spec/game/races.md, "Modes" and "Files of
// a race"; spec/formats/aimap.md, "Which race file").
#pragma once

#include <string>
#include <vector>

#include "formats/cinfo.h"

namespace cc {

class Vfs;

namespace game {

// Game mode numbers (races.md, "Modes"; 2 and 5 are multiplayer).
enum class RaceMode { Cruise = 0, Checkpoint = 1, Circuit = 3, Blitz = 4, CrashCourse = 6 };

const char* modeKey(RaceMode m);    // "roam", "race", "circuit", "blitz", "crash"
const char* modeTitle(RaceMode m);  // our own English words (the menus' text comes from mmlang)

// Race name in the files: "roam", "race<N>", "circuit<N>", "blitz<N>", "crash<N>".
std::string raceName(RaceMode m, int n);
// Races per city: the .cinfo counts; always 13 crash courses; one Cruise.
int raceCount(const CityEntry& city, RaceMode m);
// Display name: the N-th name of the .cinfo names line; crash courses: a fallback name (MM2 takes
// them from mmlang.dll, texts 532 – 544 London and 545 – 557 San Francisco).
std::string raceDisplayName(const CityEntry& city, RaceMode m, int n);

// race/<RaceDir>/mm<mode>data.csv
std::string settingsPath(const std::string& raceDir, RaceMode m);
// race/<RaceDir>/<race>waypoints.csv (not for the Crash Course).
std::string waypointPath(const std::string& raceDir, RaceMode m, int n);
// race/<RaceDir>/crash<N>data.csv or crash<N>data_p.csv.
std::string crashEventPath(const std::string& raceDir, int n, bool pro);
// race/<RaceDir>/<race>.aimap or .aimap_p.
std::string raceAimapPath(const std::string& raceDir, const std::string& race, bool pro);

}  // namespace game
}  // namespace cc
