// The city list (spec/formats/city-files.md, "City list"): tune/*.cinfo files read MM2's strict way.
#pragma once

#include <string>
#include <vector>

namespace cc {

class Vfs;

struct CityEntry {
    std::string localizedName;  // shown in the menus
    std::string mapName;        // level files city/<MapName>…
    std::string raceDir;        // AI and race data
    int blitz = 0, circuit = 0, checkpoint = 0;
    // The race names of the names lines (split at '|'), one per race; empty for a count of 0
    // (races.md, "Modes": race N has the N-th name).
    std::vector<std::string> blitzNames, circuitNames, checkpointNames;
    std::string cinfoPath;      // "" for a test layout
    bool testLayout = false;    // a city/*.psdl no listed city uses (Ant's choice, viewer.md)
};

// The nine lines of a .cinfo. False (with `why`) when MM2 rejects it.
bool readCinfo(const std::string& bytes, CityEntry& out, std::string& why);

// MM2's list (sf.cinfo first, then every tune/*.cinfo in folder order, duplicates by RaceDir
// dropped), then every city/*.psdl not used as a MapName, as a test layout.
std::vector<CityEntry> buildCityList(const Vfs& vfs);

}  // namespace cc
