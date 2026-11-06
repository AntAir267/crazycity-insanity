// Headless check runs (spec/engine/drive-check.md): run files in, one CSV row per physics step out.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "core/math.h"
#include "formats/aimap.h"
#include "physics/car.h"

namespace cc {

class Vfs;

namespace phys {

struct RunSegment {
    float duration = 0;
    DriverControls controls;
    bool hasGear = false;
    int gear = 0;  // -1 reverse, 0 neutral, 1..n
    bool detach = false;  // `detach=1`: Ctrl+B goes down on the segment's first frame (trailer runs)
};

// An extra vehicle of a car-to-car run (drive-check.md, "Extra vehicles").
struct RunVehicle {
    enum Kind { Police, Ai, Traffic };
    Kind kind = Police;
    std::string model;
    vec3 pos;
    float heading = 0;
    float speed = 0;                   // traffic: lane speed
    std::vector<RunSegment> segments;  // computer cars: seg<N>
};

struct RunFile {
    std::string car;
    std::string world = "flat";
    vec3 start;
    float heading = 0;
    int paint = 0;
    std::vector<RunSegment> segments;
    bool manual = false;  // any segment sets a gear
    std::vector<RunVehicle> extras;  // vehicles 2, 3, … in file order
    // `streetprops on`: the city run includes the street props and props.pathset props
    // (drive-check.md, "Street props in city runs"); off by default for now.
    bool streetProps = false;
    // `frame F` / `frame F1 F2 …` (drive-check.md): frame lengths cycled per frame; empty = the
    // standard 0.016666668 with the round(d / F) segment rule.
    std::vector<float> frames;
    // `trailer on` (drive-check.md, "Trailer runs"): the car tows its MM2 trailer.
    bool trailer = false;
    // `opponent <file> <throttle> <sw1> <pad> <brake> <sw2> … <sw6> <corner>` (drive-check.md,
    // "Opponent runs"): vehicle 1 driven by the computer driver as race opponent 0.
    bool opponent = false;
    OpponentRow opponentRow;  // car = the run's car, file = the route file
    std::string runDir;       // the run file's folder (route files are looked up there first)
};

bool parseRunFile(const std::string& text, RunFile& out, std::string& err);

// The frame length of check runs (vehicle.md, "Time step") and the frame split.
constexpr float kCheckFrame = 0.016666668f;
int physicsStepsForFrame(float frame);

// Runs one check and writes the CSV. False + err if the car or world cannot be set up. City
// worlds are loaded through `assets` (shared between runs by the caller).
// With `trace`, writes the drive trace of spec/engine/drive-trace.md instead of the CSV.
bool runDriveCheck(const Vfs& vfs, const RunFile& run, std::FILE* out, std::string& err, bool trace = false);

// The CSV's columns t … c3 of one row (no line end), for runs that add columns.
void writeCheckRowColumns(std::FILE* out, const Car& car, double t);

// drive-trace.md, loose bodies: the events since the last call (`eject`, `break`, `gone`, `rest`),
// then with `movers` one `loose` record per loose mover that has switch U.
struct SceneMover;
void writeLooseTrace(std::FILE* f, CollisionWorld& world, const std::vector<SceneMover>* movers);
// Tests only: when set, every car of a check run (`model`, paint index) gets the visual effects
// hook this returns (null = none). Effects never change the physics, so the output must stay the
// same byte for byte (damage-visuals.md).
using CheckEffectsFactory = std::function<CarEffectsHook*(Car& car, const std::string& model, int paint)>;
void setCheckEffects(CheckEffectsFactory f);
CarEffectsHook* checkEffectsFor(Car& car, const std::string& model, int paint);

}  // namespace phys
}  // namespace cc
