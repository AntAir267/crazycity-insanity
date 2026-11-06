// A running network game on this machine (spec/game/multiplayer.md sections 2 – 5): the start
// positions, the remote cars, the car states sent and received, the rules every mode shares (no
// pause, the throttle cap, the wreck penalty, water) and the modes: Cruise, the Checkpoint,
// Circuit and Blitz races, and Cops and Robbers with the gold.
//
// The game draws nothing. It asks its world (NetWorld) for the local car, the city and the
// physics, and leaves HUD lines (Session notices), sounds (NetWorld::event) and the tables below to
// the view. Frame order, called by the view (or the native test) around its own frame:
//   frameStart   before the local car's control step: messages, car states sent, the mode;
//   beforePhysics after the AI's movers are declared: the remote cars' inputs, smoothing, movers;
//   frameEnd     after the physics.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/game_random.h"
#include "game/waypoints.h"
#include "net/remote_car.h"
#include "net/settings.h"
#include "net/transport.h"
#include "net/wire.h"
#include "physics/car.h"

namespace cc {
class CityData;
namespace phys {
class CollisionWorld;
struct SceneMover;
}
namespace net {

class Session;

// What the game asks of the machine's world.
class NetWorld {
public:
    virtual ~NetWorld() = default;
    virtual phys::Car& localCar() = 0;
    virtual const CityData* city() const = 0;        // null: flat ground (no rooms, no spots)
    virtual phys::CollisionWorld* collision() = 0;   // ground probes; the remote trailers' world
    // The local car at q with heading θ by the placement rule (cameras and trailer follow).
    virtual void placeLocal(const vec3& q, float theta) = 0;
    virtual void postRaceCamera() {}
    virtual bool localHorn() const { return false; }
    virtual bool localSiren() const { return false; }
    // The view's parts of a remote car (model, effects, sound); gone again.
    virtual void remoteCarAdded(RemoteCar& rc) { (void)rc; }
    virtual void remoteCarRemoved(RemoteCar& rc) { (void)rc; }
    // Sounds and music cues by name (Startracelow, Startracehigh, musicStart, …).
    virtual void event(const char* name) { (void)name; }
};

// A row of the results (5.2 step 6, 5.3 "End").
struct NetResult {
    PlayerId id = 0;
    float time = 0;     // races: seconds, 86400 = did not finish
    int points = 0;     // Cops and Robbers
    int team = -1;      // a team line (Cops and Robbers sub-modes 1, 2): 0 or 1; −1 a player
    int teamText = 0;   // its name's text number
};

class NetGame {
public:
    NetGame(Session& s, const Settings& st, bool lateJoin);
    ~NetGame();

    // The world is built: the mode starts (start positions, records, the first events).
    void attach(NetWorld* w);
    bool attached() const { return world_ != nullptr; }
    void detach();

    // Queued by the session; handled at the next frameStart (in arrival order).
    void queueMessage(PlayerId from, const std::vector<uint8_t>& data);
    void queueLeft(PlayerId id, const std::string& name);
    void queueHostChanged(PlayerId host);

    // ---- the frame
    void frameStart(float F);
    void beforePhysics(float F, std::vector<phys::SceneMover>& movers);
    void frameEnd(float F);

    // The local car was put somewhere else outside the game's own rules (a reset key, a test):
    // "car reset" with its matrix to everyone (1.5).
    void announceReset() {
        if (world_) sendCarReset();
    }

    // ---- from the results (the host)
    bool canRestart() const;
    void restart();

    // ---- state for the view and the tests
    int mode() const { return mode_; }
    const Settings& settings() const { return st_; }
    enum RaceState { Waiting = 0, Countdown = 1, Racing = 2, SlowDown = 3, Stopped = 4, Results = 5 };
    int raceState() const { return raceState_; }
    int waitCount() const { return waitCount_; }
    bool playing() const { return playing_; }
    int localSlot() const { return localSlot_; }  // start slot (race grid), −1 none
    std::vector<RemoteCar*> remoteCars() const;   // slot order
    RemoteCar* remoteOf(PlayerId id) const;
    const game::WaypointList& waypoints() const { return list_; }
    bool haveWaypoints() const { return list_.size() > 0; }
    float raceClock() const { return clock_; }
    float countdown() const { return countdown_; }
    bool countdownShown() const { return countdownShown_; }
    bool finished() const { return finished_; }
    int place() const { return place_; }  // the HUD place (kept once the local car finished)
    int racers() const;
    // A remote player's result has been entered (5.2 "Racing").
    bool playerFinished(PlayerId id) const;
    // A present remote car's place for its icon (0: hidden — absent or its player finished).
    int remotePlace(int slot) const;
    const std::vector<NetResult>& resultTable() const { return results_; }  // races: sorted by time
    bool resultsShown() const { return resultsShown_; }
    std::vector<NetResult> resultsList() const;  // what the results screen lists
    float finishWindow() const { return window_; }
    bool penalty() const { return penaltyTimer_ > 0.0f; }
    float hostClock() const { return hostClock_; }
    // Cops and Robbers.
    int points() const { return points_; }
    int teamOf(PlayerId id) const;
    int localTeam() const;
    int teamPoints(int team) const;
    PlayerId goldCarrier() const { return carrier_; }
    bool goldShown() const { return goldShown_; }
    vec3 goldPoint() const;         // where it lies (or floats)
    const vec3& goldHome() const { return goldHome_; }
    const vec3& bankPoint() const { return bank_; }
    const vec3& hideoutPoint() const { return hideout_; }
    bool basesPlaced() const { return placed_; }
    bool localCarries() const;
    float copsCountdown() const { return copsClock_; }
    bool copsTimed() const { return st_.limitType == LimitTime; }
    bool gameOver() const { return over_ != 0; }
    int scoringEvents() const { return scoringEvents_; }
    const std::vector<vec3>& spots() const { return spots_; }

    // The arrow (hud.md): the gold or the own base; false when off.
    bool arrowTarget(vec3& p) const;
    // Counters for the tests.
    int statesSent = 0, statesReceived = 0, statesDropped = 0;
    // The game asks the session to quit (the host vanished in Cops and Robbers, 1.3 step 6).
    bool quitWanted = false;

    // The network respawn (2.1) and the grid (2.2), for the tests.
    static vec3 gridOffset(int slot, bool wide);
    vec3 gridPosition(int slot, bool wide) const;
    bool networkRespawn(PlayerId id, vec3& pos);
    // A random Cops and Robbers spot (5.3 "Spots").
    vec3 randomSpot();

private:
    struct Incoming {
        enum Kind { Message, Left, HostChanged } kind = Message;
        PlayerId from = 0;
        std::vector<uint8_t> data;
        std::string name;
    };
    void handle(const Incoming& in);
    void onMessage(PlayerId from, Msg type, Reader& r);
    void onCarState(PlayerId from, Reader& r);
    void onLeft(PlayerId id, const std::string& name);
    void onHostChanged(PlayerId host);

    RemoteCar* addRemote(PlayerId id);
    void removeRemote(PlayerId id);
    void syncRemotes();          // a remote car for every other player of the session
    void showRemote(RemoteCar& rc, bool atRandomIntersection);
    bool wideCar(const phys::Car& c, bool trailer) const;
    vec3 dropToGround(const vec3& p, float up, float down) const;
    int roomOf(const vec3& p) const;
    void sendStates(float F);
    void sendStateTo(PlayerId to, uint16_t extraFlags);
    uint16_t localScore() const;
    void localRules(float F);
    void wreckPenalty(float F);
    void waterRule(float F);
    void localReset(const vec3& q, float theta);
    void sendCarReset();
    bool damageOn() const;

    // Cruise.
    void cruiseStart();
    void cruiseFrame(float F);
    // Races.
    void raceSetup();
    void raceToGrid();
    void raceFrame(float F);
    void raceGo();
    void standings();
    void localFinished(float t);
    void insertResult(PlayerId id, float time);
    void stopLocalCar();
    void showResults();
    void blitzTimeUp(bool received);
    void raceRestart(bool sendIt, bool initial);
    // Cops and Robbers.
    void copsStart();
    void copsGo();
    void copsFrame(float F);
    void placeGoldAndBases();
    void takeGold(PlayerId id);
    void dropGold(const vec3& P, bool forced, bool sendIt);
    void goldLeaves(PlayerId carrier);
    void delivered(PlayerId id);
    void copsEnd(int reason);
    void copsRestart();
    void sendStateReply(PlayerId to);
    bool pickupBlocked() const;
    struct ImpactHook;
    std::unique_ptr<ImpactHook> impactHook_;
    void knockedLoose();

    Session& s_;
    Settings st_;
    int mode_ = 0;
    bool lateJoin_ = false;
    NetWorld* world_ = nullptr;
    std::vector<Incoming> inbox_;
    std::vector<std::unique_ptr<RemoteCar>> remotes_;  // by slot (null = free)
    uint32_t seq_ = 0;
    float slowLinkTimer_ = 0;
    int frame_ = 0;
    bool playing_ = false;    // "while racing / playing" (wreck penalty, pickups)
    float penaltyTimer_ = 0;  // the wreck penalty's 5 s
    float hostClock_ = 0;     // the host's mode timer value (rides in its states)
    float modeClock_ = 0;     // Cruise: the session's clock (ends nothing)
    bool modeClockOn_ = false;
    vec3 resetPoint_;
    bool resetFlagPending_ = false;
    bool selfCheaterHold_ = false;

    // Races.
    std::vector<Waypoint> wpts_;
    game::WaypointList list_;
    vec3 S_;
    float theta_ = 0;
    int localSlot_ = -1;
    int raceState_ = Waiting;
    float t_ = 0;
    bool loadedSent_ = false;
    bool restarted_ = false;
    int waitCount_ = 0;
    int countStep_ = 0;
    float clock_ = 0;
    bool clockOn_ = false;
    float countdown_ = 0;
    bool countdownOn_ = false, countdownShown_ = false;
    bool finished_ = false;   // the local car has completed the race (or was stopped)
    bool reported_ = false;
    float slowDown_ = 0;
    bool timeUp_ = false;
    float blitzS_ = 60.0f, blitzR_ = 0.0f;  // Blitz: the slow-down S and the results timer R (5.2)
    bool blitzTimers_ = false;
    float ctlSlow_ = 0.0f;    // this frame's throttle reduction (slow-down time, Blitz's R)
    bool ctlStopped_ = false; // the car stopped (steering 0, brake 1, not drivable)
    int racersShown_ = 1;     // the HUD's racer count (5.2 "Racing" step 1)
    int remotePlace_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    bool resultsShown_ = false;
    std::vector<NetResult> results_;
    float window_ = -1;       // the 60 s finish window (−1: not running)
    bool raceOverSent_ = false;
    int place_ = 1;

    // Cops and Robbers.
    std::vector<vec3> spots_;
    vec3 gold_, goldHome_, bank_, hideout_;
    bool placed_ = false;
    bool goldShown_ = false;
    bool goldHidden_ = false;    // hidden locally after a request
    PlayerId carrier_ = 0;
    bool requestPending_ = false;
    bool stateReplied_ = false;  // non-host: the state reply arrived (the game starts)
    bool copsStarted_ = false;
    int points_ = 0;
    int scoringEvents_ = 0;
    float knockPause_ = 0;
    bool knockPending_ = false;
    float copsClock_ = 0;
    bool copsClockOn_ = false;
    float warnL_ = 0;            // the time warnings' L (5.3 "End")
    double frameMark_ = 0;       // MM2's frame mark (real seconds) for the spot seeds
    int over_ = 0;                // game over reason (1 time, 2 points)
    float overTimer_ = 0;
    std::vector<std::pair<PlayerId, int>> teams_;  // teams learnt from state requests
    Lcg clockRng_;
};

}  // namespace net
}  // namespace cc
