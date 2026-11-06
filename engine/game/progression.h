// What MM2 does with its player files (spec/game/progression.md): open races, locked cars and
// paint jobs, what a finished race records, the hall of fame, rewards and totals. Pure rules over
// the structures of game/player_files.h; the folder and the save moments are game/profile_store.h.
#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "game/player_files.h"

namespace cc {
namespace game {

// One city of the city list as the menus and records see it (city-files.md, "Reading a .cinfo").
struct CityRaces {
    std::string localizedName, mapName, raceDir;
    int checkpoint = 0, circuit = 0, blitz = 0;
    std::vector<std::string> checkpointNames, circuitNames, blitzNames;
    bool testLayout = false;
    // Race count of a record type (crash courses: always 13).
    int count(int recType) const;
    const std::vector<std::string>* names(int recType) const;
};

// MM2 names the two shipped cities in its code: only they keep records (progression.md,
// "Which cities keep records").
bool keepsRecords(const std::string& raceDir);
extern const char* const kRecordCities[2];  // "sf", "london" (the order locks are worked out in)

// ---------------------------------------------------------------------------------------------
// Open races. `rec` = the current driver's valid record file of the city, or null (no driver, no
// valid file, or a city without records): then everything is open.

uint32_t openCheckpointMask(const RecordFile* rec);
uint32_t openCrashMask(const RecordFile* rec);
bool raceOpen(const RecordFile* rec, int recType, int race);

// Crash-course state for the menus: 0 not tried, 1 passed, 2 tried but not passed.
int crashCourseState(const RecordFile* rec, int course);

// Locked environment settings of the race menu (menus.md, "Locked settings"): true while the
// driver has not yet passed that race (always false without records).
bool raceSettingsLocked(const RecordFile* rec, int recType, int race);

// ---------------------------------------------------------------------------------------------
// Locked cars and paint jobs.

struct CarRef {
    std::string baseName;
    int colors = 1;            // number of paint jobs (Colors=)
    float scoringBias = 0.0f;  // ScoringBias= of tune/<car>.info
    std::string description;
};

// Why something is locked: the first rewards line not yet met, and its city (RaceDir).
struct LockReason {
    RewardLine line;
    std::string city;
};

struct Locks {
    std::set<std::string> cars;                    // BaseNames
    std::set<std::pair<std::string, int>> paints;  // (BaseName, paint job)
    std::map<std::string, LockReason> carWhy;
    std::map<std::pair<std::string, int>, LockReason> paintWhy;
    bool carLocked(const std::string& car) const { return cars.count(car) != 0; }
    bool paintLocked(const std::string& car, int paint) const { return paints.count({car, paint}) != 0; }
    void unlock(const RewardLine& l);
};

// Condition of a rewards line, as the locks read it (half: passed ≥ ⌊N/2⌋).
bool lockConditionMet(const RewardLine& l, const RecordFile& rec);
// As the reward check reads it after a race (half: passed = ⌊N/2⌋ exactly).
bool rewardConditionNow(const RewardLine& l, const RecordFile& rec);

// One city's input to the locks: its rewards lines and the driver's valid record file (either
// null skips the city: its rewards stay unlocked).
struct CityRewards {
    const std::vector<RewardLine>* rewards = nullptr;
    const RecordFile* record = nullptr;
    std::string city;  // RaceDir (for the reasons)
};
Locks computeLocks(const std::vector<CityRewards>& cities);

// ---------------------------------------------------------------------------------------------
// After a race (progression.md, "After a race: what is recorded").

// What the race code reports the moment the player's result is final.
struct RaceResult {
    int mode = ModeCheckpoint;  // 1 checkpoint, 3 circuit, 4 blitz, 6 crash course
    int race = 0;               // race number within the mode, from 0
    int place = 1;              // finishing place from 1 (checkpoint, circuit)
    bool finished = true;       // false: lost (out of time, wrecked, drowned, disqualified)
    float time = 0.0f;          // the race timer (checkpoint, blitz)
    std::vector<float> laps;    // circuit: every lap's time in lap order
    bool crashPassed = false;   // crash course: passed (true) or failed (false)
    // The race was played with the race's own settings for this race and skill level: cops,
    // traffic density, time of day and weather (Circuit also laps and opponents) exactly equal.
    bool ownSettings = true;
    std::string car;            // BaseName of the player's car
    float scoringBias = 0.0f;   // the car's ScoringBias
    float difficulty = 1.0f;    // the race's Difficulty setting
};

// Score = trunc(bias) × F × trunc(D): F = 50, 25, 10, 0 for place 1, 2, 3, other (Blitz always 50).
int placeScore(float scoringBias, int place, float difficulty, bool blitz);
// Checkpoint and circuit: place 1 – 3 at Amateur, 1 at Professional.
bool placePasses(int place, int skill);

// The record a result makes (table of progression.md). False when nothing is recorded (lost race
// other than a failed crash course, changed settings, blitz race 12 or above, crash course above 12).
// `alreadyPassed`: the race's passed bit before this result (checkpoint races stay passed).
bool resultRecord(const RaceResult& r, int skill, bool alreadyPassed, RaceRecord& out);

// Steps 1 – 3 of "Adding a result to the record file" (the record's check value is computed when
// the file is written).
void addToRecordFile(RecordFile& f, int recType, int race, const RaceRecord& nw);

// "Adding a result to the hall of fame" for one value pair; true when either table changed.
bool addToHallOfFame(HallOfFame& h, int recType, int race, const std::string& driver, const std::string& car,
                     float time, int score, bool passed);

// "Rewards": the first line of the race type just played whose condition holds now and whose car
// or paint job is still locked in `locks` (which it then unlocks). Null when there is none.
const RewardLine* findReward(const std::vector<RewardLine>& rewards, int mode, const RecordFile& rec, Locks& locks);

// ---------------------------------------------------------------------------------------------
// Totals shown in the menus (from the driver's valid record files of sf and london).

struct Totals {
    int score[3] = {};   // checkpoint, circuit, blitz: sum of the records' scores
    int passed[3] = {};  // records with passed = 1
    int total = 0;       // checkpoint + circuit + blitz scores of both cities
};
void addTotals(const RecordFile& rec, Totals& t);

}  // namespace game
}  // namespace cc
