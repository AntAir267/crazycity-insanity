// A race session (spec/game/races.md, modes.md, crash-course.md, results.md): the race's
// settings row, its waypoint list and opponents, the Checkpoint, Blitz, Circuit and Crash Course
// state machines, the three clocks, the countdown, standings, results, score and what is handed
// to the records, the restart sequence, water and falling through.
//
// The session draws nothing and plays nothing: it posts HUD messages (by mmlang text number) and
// named sound/speech/music events for the HUD, sound and music builders, and it asks its host for
// the parts of the world it does not own (props, traffic and police resets, the player's
// placement, the race cameras, the cop-in-sight probe).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai/opponents.h"
#include "formats/cinfo.h"
#include "formats/race_files.h"
#include "game/race_catalogue.h"
#include "game/waypoints.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;

namespace phys {
class Car;
class CollisionWorld;
struct Bound;
}  // namespace phys

namespace ai {
class RoadNetwork;
}

namespace game {

// The session's values (races.md, "The session's settings") the host applies.
struct SessionValues {
    float opponentNumber = 0;
    float copDensity = 1.0f;
    float trafficDensity = 0.5f;
    float pedDensity = 0.25f;
    int timeOfDay = 1, weather = 0;
    int laps = 1;
    float timeLimit = 0;
    float difficulty = 1;
};

// A named event for the sound, speech and music builders (spec/audio/events.md, "Race modes").
// Sounds by their file name: Startracelow, Startracehigh, Endofracetag, Youlose, Damgelose,
// Messagenote, Waypoint, Lastwaypoint, TimerwarningStart / TimerwarningStop. Speech: prerace,
// finalCheckpoint, damagePenalty, resultsPoor, results (value = place, value2 = opponent
// setting), ccPrerace, ccGate (value = gate), ccPass, ccFail. Music: musicStart, musicStop,
// musicFinalCheckpoint, musicFinalLap. Car sound: engineSilence.
struct RaceEvent {
    std::string name;
    int value = 0, value2 = 0;
};

// One HUD message line pair (hud.md, "Race messages"): text numbers of mmlang.dll.
struct HudMessage {
    int text = 0;             // 0: none
    std::string arg;          // a value printed into the text (232: the target speed)
    int second = 0;           // the secondary line's text (0: none)
    std::string secondTime;   // or a time on the secondary line
    float time = 0;           // seconds left
    bool upper = false;       // the countdown position
};

struct ResultRow {
    int place = 0;
    int racer = -1;      // −1 the player, else the opponent index
    float time = 0;      // seconds
    bool passMark = false;  // Crash Course: text 651 instead of a time
};

// What a finished race hands to the records (results.md, progression.md).
struct RaceOutcome {
    RaceMode mode = RaceMode::Checkpoint;
    int race = 0;
    bool professional = false;
    std::string car;
    float time = 0;
    bool passed = false;
    int score = 0;
    int place = 0;
    std::vector<float> lapTimes;  // Circuit: the stored laps 1 … laps
};

// The records and the current driver, as the race asks for them. The UI builder owns profiles;
// the default records nothing (no driver).
class RaceRecords {
public:
    virtual ~RaceRecords() = default;
    virtual bool hasDriver() const { return false; }
    virtual bool cheated() const { return false; }
    virtual bool passedBefore(RaceMode m, int race, bool pro) const {
        (void)m, (void)race, (void)pro;
        return false;
    }
    virtual bool raceOpen(RaceMode m, int race, bool pro) const {
        (void)m, (void)race, (void)pro;
        return true;
    }
    virtual void record(const RaceOutcome& r) { (void)r; }
};

// The parts of the session the race does not own.
class RaceHost {
public:
    virtual ~RaceHost() = default;
    // Restart step 3 (races.md, "Restart"): props back, the AI reset (traffic, police, the game
    // generators). The race has already put its opponents back.
    virtual void worldReset() = 0;
    // The player's car at q with heading θ by the placement rule: damage cleared, controls
    // enabled, camera to its view, water effect off.
    virtual void resetPlayer(const vec3& q, float theta) = 0;
    virtual void preRaceCamera() = 0;
    virtual bool preRaceCameraRunning() const = 0;
    virtual void postRaceCamera() = 0;
    virtual void waterCamera() = 0;
    virtual bool copInSight() const = 0;
    virtual void hudMapOn() {}
};

// A text for a message number: the player's mmlang.dll in the real HUD; our English stand-ins
// for the plain overlay.
std::string fallbackText(int id);
// hud.md, "Times in messages and tables": M:SS:hh, or a dash for t ≤ 0.
std::string formatRaceTime(float t);
// hud.md, "Timer display": MM:SS:hh of max(t, 0).
std::string formatTimer(float t);

class RaceSession {
public:
    struct Setup {
        const Vfs* vfs = nullptr;
        AssetCache* assets = nullptr;  // opponent models (null: none drawn)
        CityData* city = nullptr;
        ai::RoadNetwork* net = nullptr;
        phys::CollisionWorld* world = nullptr;
        CityEntry cityEntry;
        RaceMode mode = RaceMode::Checkpoint;
        int race = 0;
        bool professional = false;
        int circuitOpponents = -1;  // Circuit: the player's choice (−1 = the race's number)
        // The player's own choices from the race menu where the race allows them (menus.md,
        // "Locked settings"; races.md, "The session's settings"): −1 = the race's own value.
        int timeOfDay = -1, weather = -1;
        int laps = -1;              // Circuit: 1 – 8 (other modes keep their own)
        float copDensity = -1.0f;   // 0 … 1 (a race's own Cops count of 1 or more means every cop)
        float trafficDensity = -1.0f;  // 0 … 1 (Checkpoint and Blitz; Circuit keeps its own)
        float pedDensity = -1.0f;   // 0 … 1 (it never matters for the records)
        std::string playerCar;
        std::shared_ptr<phys::Bound> playerBound;
        float scoringBias = 0;      // tune/<car>.info ScoringBias
        bool noDamage = false;      // the player's no-damage option
        RaceRecords* records = nullptr;
        RaceHost* host = nullptr;
        std::function<void(const RaceEvent&)> events;
    };

    RaceSession();
    ~RaceSession();

    // Reads the race's files (races.md, "Setting up a race", steps 2–3): settings row, waypoint
    // list or event file, opponents, the opponents' finish line. False with `err` when the race
    // cannot start (missing file).
    bool setup(const Setup& s, phys::Car& player, std::string& err);
    const SessionValues& values() const { return values_; }
    // "Placing the cars" and the first restart (call after the host set up traffic and police).
    void start();
    // A full reset of the race (races.md, "Restart").
    void restart();

    // Steps 1–3 of one frame (races.md, "One frame"): the mode's update, water and falling
    // through, the S/X keys, the gate test and the clocks. `next`/`prev`: S/X pressed.
    void frame(float F, bool next, bool prev, double now);
    // While the game is paused: only the water timer and the falling-through check run (races.md,
    // "Water and falling through"; the car does not move).
    void pausedFrame(float F);
    // The opponents, for the host's AI frame (update between the yield-mark passes).
    ai::OpponentSystem& opponents() { return opps_; }
    const ai::OpponentSystem& opponents() const { return opps_; }
    std::vector<vec3> opponentPositions() const;

    // State for the HUD and the page.
    RaceMode mode() const { return mode_; }
    int raceNumber() const { return race_; }
    bool professional() const { return pro_; }
    const std::string& raceTitle() const { return title_; }
    int state() const { return state_; }
    const WaypointList& waypoints() const { return list_; }
    float raceClock() const { return clock_.t; }
    float opponentClock() const { return oppClock_.t; }
    float countdown() const { return countdown_.t; }
    bool countdownShown() const { return countdownShown_; }
    bool timerWarning() const { return warning_; }
    float lapClock() const { return clock_.t - lapSum_; }
    const std::vector<float>& lapTimes() const { return lapTimes_; }  // completed laps, in order
    float bestLap() const { return bestLap_; }
    int playerPlace() const { return hudPlace_; }
    int racers() const { return int(opps_.opponents().size()) + 1; }
    int opponentPlace(int i) const { return i >= 0 && size_t(i) < oppPlace_.size() ? oppPlace_[size_t(i)] : 0; }
    int opponentGates(int i) const { return i >= 0 && size_t(i) < oppCount_.size() ? oppCount_[size_t(i)] : 0; }
    bool opponentFinished(int i) const { return i >= 0 && size_t(i) < oppDone_.size() && oppDone_[size_t(i)]; }
    const HudMessage& message() const { return msg_; }
    const std::vector<ResultRow>& results() const { return results_; }
    bool resultsShown() const { return resultsShown_; }
    bool endMenuOpen() const { return endMenu_; }
    bool finished() const { return raceFinished_; }
    bool lost() const { return state_ == 4; }
    int score() const { return score_; }
    bool nextRaceOffered() const { return nextOffered_; }
    // "Next race" (results.md): race number + 1 of the same mode; the Crash Course replaces the car
    // by vpbullet (RaceDir sf) or vpcab (elsewhere) when the driver has not passed the new course
    // (empty: keep the car); Checkpoint and Blitz keep the pedestrian density.
    int nextRaceNumber() const { return race_ + 1; }
    std::string nextRaceCar() const;
    bool nextRaceKeepsPeds() const { return mode_ == RaceMode::Checkpoint || mode_ == RaceMode::Blitz; }
    // Crash Course.
    int crashEvent() const { return event_; }
    int crashEventCount() const { return int(events_.size()); }
    int crashKind() const { return events_.empty() ? -1 : events_[size_t(event_)].kind; }
    bool crashPassed() const { return ccPassed_; }

    const vec3& startPoint() const { return start_; }
    float startHeading() const { return startTheta_; }

private:
    struct Clock {
        float t = 0;
        bool running = false;
    };
    void post(int text, float time, bool upper = false, const std::string& arg = std::string());
    void post2(int text, int second, float time);
    void emit(const char* name, int v = 0, int v2 = 0);
    void stopClocks();
    void resetStopClocks();
    void startResetClocks();
    void stopCountdownToLimit();
    void holdRacers();
    void go(bool withOpponents = true);
    void releaseOpponents(int first, int count);
    void disablePlayer();
    void notDrivable();
    bool wrecked() const;
    bool damageRules() const { return !noDamage_; }
    // Modes.
    void checkpointFrame(float F);
    void blitzFrame(float F);
    void circuitFrame(float F);
    void crashFrame(float F);
    void playerFinished(int k);
    void standings();
    void opponentsFinishing();
    void waterAndFalling(float F);
    void gateEvents(const std::vector<GateEvent>& ev);
    void advanceClocks(float F);
    void recordResult(const RaceOutcome& r);
    void setNextRace();
    int scoreFor(int place) const;
    // Crash Course.
    void setupEvent(int e);
    void crashKindChecks(float F);
    bool goalMet();
    void coursePass();
    void courseFail();
    void timeOut();
    phys::Car* targetCar() const;

    Setup s_;
    phys::Car* player_ = nullptr;
    RaceMode mode_ = RaceMode::Checkpoint;
    int race_ = 0;
    bool pro_ = false;
    std::string title_;
    SessionValues values_;
    RaceDataRow row_;
    bool haveRow_ = false;
    std::vector<Waypoint> points_;
    WaypointList list_;
    ai::OpponentSystem opps_;
    int raceCount_ = 0;
    vec3 start_;
    float startTheta_ = 0;
    bool noDamage_ = false;
    // State.
    int state_ = 0;
    float t_ = 0;
    Clock clock_, oppClock_, countdown_;
    float countdownLimit_ = 0;
    bool countdownShown_ = false;
    bool warning_ = false;
    bool outOfTime_ = false;
    bool raceFinished_ = false;
    bool inWater_ = false;
    bool resultsShown_ = false, endMenu_ = false;
    bool waterEpisode_ = false;   // the player's water timer passed 5 s in this episode
    HudMessage msg_;
    // Standings and finishing.
    int finishedCount_ = 0;
    std::vector<int> oppCount_, oppPlace_;
    std::vector<uint32_t> oppMask_;
    std::vector<char> oppDone_;
    int playerPlace_ = 0, hudPlace_ = 1;
    std::vector<ResultRow> results_;
    int score_ = 0;
    bool nextOffered_ = false;
    // Circuit.
    float lapSum_ = 0;
    std::vector<float> lapTimes_;   // completed laps (HUD)
    std::vector<float> storedLaps_; // slots 1 … laps (index lap − 1), 0 = not stored
    float bestLap_ = 0;
    bool haveBest_ = false;
    int nextStore_ = 1;
    // Crash Course.
    std::vector<CrashEvent> events_;
    int event_ = 0;
    bool ccFinishedMark_ = false;
    bool ccPassed_ = false;
    bool ccReached_ = false;
    float ccUnder_ = 0;
    float ccMinSpeed_ = 50;
    bool firstEvent_ = true;
    double now_ = 0;
};

}  // namespace game
}  // namespace cc
