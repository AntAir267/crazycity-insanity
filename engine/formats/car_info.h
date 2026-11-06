// Player car list and tune/<car>.info files (spec/formats/car-model.md, "Which player cars exist").
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cc {

class Vfs;

// "Key=Value" text file (one per line). Keys keep their spelling; lookups ignore case.
struct InfoFile {
    std::vector<std::pair<std::string, std::string>> values;
    std::string get(const std::string& key, const std::string& fallback = "") const;
};

InfoFile parseInfoFile(const std::string& text);

// MM2's strict reading of a car's .info (car-model.md, "Reading a .info file"): twelve lines in a
// fixed order with fixed patterns (case-sensitive keys), then UIDist lines. False (with `why`)
// when MM2 rejects the car. `out` gets the twelve values under their keys plus "UIDist".
bool readCarInfoStrict(const std::string& bytes, InfoFile& out, std::string& why);

struct CarInfo {
    std::string infoPath;     // stored path of the .info file
    std::string baseName;     // BaseName= (file names of the model)
    std::string description;  // Description= (display name)
    std::vector<std::string> colors;  // Colors= split on '|'
    bool builtIn = false;     // one of the 20 built-in cars
    std::string archive;      // archive that supplied the .info
    bool fromMod = false;     // the .info comes from a non-base archive
    InfoFile info;
    float uiDist = 6.0f;      // UIDist (menu value)
};

// The 20 built-in car names in menu order.
const std::vector<std::string>& builtInCarNames();

// Builds MM2's car list: the 20 built-ins (tune/<name>.info) in order, then every other
// tune/*.info / *.INFO file. Problems are logged as file problems.
std::vector<CarInfo> buildCarList(const Vfs& vfs);

}  // namespace cc
