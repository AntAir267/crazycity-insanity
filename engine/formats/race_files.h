// The text files of MM2's race modes: the race settings rows (`race/<c>/mm<mode>data.csv`,
// spec/game/races.md), the waypoint lists (`<race>waypoints.csv`, spec/game/waypoints.md), the
// Crash Course event files (`crash<N>data.csv`, spec/game/crash-course.md) and the opponent route
// files (`.opp`, spec/formats/opp.md). The opponent rows of a race `.aimap` are read with the
// other aimap sections (formats/aimap.h).
#pragma once

#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

// C's atoi and atof as MM2's runtime has them: leading white space skipped, the longest prefix
// that is a number, 0 if there is none (no "inf", "nan" or hexadecimal forms).
int cAtoi(const char* s);
double cAtof(const char* s);

// The pieces of a line split the way C's strtok(",") does it: runs of commas count as one
// separator, leading commas are skipped, empty tokens never appear.
std::vector<std::string> strtokCommas(const std::string& line);

// ---- Race settings (races.md, "The race settings file") ----

struct RaceSettings {
    int carType = 0, timeOfDay = 0, weather = 0, opponents = 0, cops = 0;
    float ambient = 0, peds = 0;
    int numLaps = 0;
    float timeLimit = 0, difficulty = 0;
};

struct RaceDataRow {
    std::string description;
    RaceSettings amateur, professional;
    int fields = 0;  // fields found on the line (21 in every shipped row)
    const RaceSettings& of(bool pro) const { return pro ? professional : amateur; }
};

// Header skipped; lines of at most 255 bytes; blank lines skipped (MM2 crashes); missing fields
// are 0; only the first 15 rows are kept (MM2's array).
std::vector<RaceDataRow> parseRaceData(const std::string& text);

// ---- Waypoints (waypoints.md) ----

struct Waypoint {
    vec3 pos;
    float heading = 0;  // degrees (a; the fill-in rule may have replaced a file 0)
    float radius = 15;  // the file's integer radius as a float (0 -> 15)
    int sphere = 0;     // field 6 ("frame rate"): a sphere gate in an unordered Crash Course list
    // Gate line ends (x, z), from heading and radius.
    vec2 A, B;
    bool appended = false;  // the extra (0, 0, 0) waypoint of a full list
};

struct WaypointFile {
    bool found = false;
    std::vector<Waypoint> points;
    // For the data checks: lines read after the header, lines with all eight numeric fields,
    // lines whose message text is not empty, headings filled in.
    int lines = 0, fullLines = 0, messages = 0, filled = 0;
};

// `slots`: 50 for races, 32 for Crash Course events. `circuit`: the Circuit fill-in of the last
// waypoint's zero heading.
WaypointFile parseWaypoints(const std::string& text, int slots, bool circuit);
// Recomputes A and B of a waypoint (waypoints.md, "The gate of a waypoint").
void computeGate(Waypoint& w);

// ---- Crash Course events (crash-course.md, "Event file") ----

struct CrashEvent {
    std::string filename;  // the waypoint list <filename>.csv (at most 31 characters)
    int kind = 0;          // 0 … 9
    int checkpoints = 0;   // 0: no waypoint list at all
    float timeLimit = 0;
    float ambDensity = 0;
    float misc = 0;        // minimum-speed kind: the speed in mph
    int onlyTarget = 0;    // field 7
    int opponents = 0;     // field 8
    int fields = 0;        // fields found on the line
};

// Header skipped, lines of at most 255 bytes, blank lines skipped, missing fields 0, the first 10
// events kept.
std::vector<CrashEvent> parseCrashEvents(const std::string& text);

// ---- Opponent route files (opp.md) ----

struct OppPoint {
    vec3 pos;           // each rounded to float
    double heading = 0; // degrees, kept in double precision
    int tokens = 0;     // tokens on the line (9 in the shipped files)
};

struct OppFile {
    bool found = false;
    std::vector<OppPoint> points;
    std::string header;
    int blankLines = 0, longLines = 0;  // data checks
};

OppFile parseOpp(const std::string& text);

}  // namespace cc
