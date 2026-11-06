// The host's game settings (spec/game/multiplayer.md 1.2) and their packing into three 32-bit
// words plus the city name. A joining peer only ever gets the packed values, so every value is
// limited exactly as the packing limits it; the host keeps the same limited values.
#pragma once

#include <cstdint>
#include <string>

namespace cc {

class Vfs;

namespace net {

// Network game modes (1.2).
enum NetMode { ModeCruise = 0, ModeCheckpoint = 1, ModeCops = 2, ModeCircuit = 3, ModeBlitz = 4 };
bool isRaceMode(int mode);  // Checkpoint, Circuit, Blitz

// Cops and Robbers sub-modes and limits (5.3).
enum CopsSubMode { FreeForAll = 0, CopsAndRobbers = 1, RobberTeams = 2 };
enum CopsLimit { LimitNone = 0, LimitTime = 1, LimitPoints = 2 };

struct Settings {
    int mode = ModeCruise;
    std::string city = "sf";   // RaceDir
    int race = 0;
    int timeOfDay = 1, weather = 0;
    int skill = 0;             // 0 amateur, 1 professional
    bool noDamage = false;
    int laps = 1;              // Circuit laps (Checkpoint 1, Blitz 0)
    float timeLimit = 0;       // seconds (Blitz countdown; the other modes only start a clock)
    float pedDensity = 0.25f;
    // Cops and Robbers byte (mode 2; replaces laps and time limit).
    int subMode = FreeForAll;  // 0 – 2
    int goldWeight = 0;        // 0, 1, 2 → mass 0, 100, 200 kg
    int limitType = LimitNone; // 0 – 2
    int limitChoice = 0;       // 0 – 3: 5/10/20/30 minutes or 100/250/500/1000 points

    float goldMass() const { return goldWeight == 1 ? 100.0f : goldWeight == 2 ? 200.0f : 0.0f; }
    // The throttle cap while carrying (5.3): 0.9 for 100 kg, 0.81 for 200 kg, else 1.
    float goldCap() const { return goldWeight == 1 ? 0.9f : goldWeight == 2 ? 0.81f : 1.0f; }
    int limitMinutes() const;  // 5, 10, 20, 30 (time limit)
    int limitPoints() const;   // 100, 250, 500, 1000 (point limit)
    uint8_t copsByte() const;
    void setCopsByte(uint8_t b);
};

// The packed form (1.2): what goes over the wire.
struct PackedSettings {
    uint32_t w[3] = {0, 0, 0};
    std::string city;
};
PackedSettings pack(const Settings& s);
Settings unpack(const PackedSettings& p);
// Every value limited as the packing limits it (pack then unpack).
Settings limited(const Settings& s);

// Host defaults when a mode and race are picked (1.2): Checkpoint the race row's time of day,
// weather, pedestrian density and time limit with laps 1; Circuit the same plus the row's laps;
// Blitz the same with laps 0; Cruise pedestrians 0.25, noon, clear; Cops and Robbers sets nothing
// (the time of day, weather and pedestrians of `current` stay). The row is the host's skill level
// half of race/<city>/mm<mode>data.csv.
Settings hostDefaults(const Vfs& vfs, int mode, const std::string& city, int race, int skill,
                      const Settings& current = Settings());

// The race name of a network game for the race files and path sets (multiplayer.md 1.4): "roam"
// for Cruise, "multicop", "race<N>", "circuit<N>", "blitz<N>".
std::string netRaceName(int mode, int race);

}  // namespace net
}  // namespace cc
