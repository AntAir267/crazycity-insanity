// The AI driver (spec/ai/driver.md) and its target planner (spec/ai/driver-planner.md): turns
// orders (a route of intersections, a destination, tuning numbers) into a car's steering, throttle
// and brake once per frame. Used by the police (police.h); race opponents later.
#pragma once

#include <array>
#include <vector>

#include "ai/road_network.h"
#include "core/math.h"
#include "physics/car.h"

namespace cc {

class CityData;

namespace phys {
class CollisionWorld;
}

namespace ai {

class TrafficSystem;

// driver-planner.md §17.1 for a vehicle box (also the cable cars' "Something ahead").
float vehicleBlockDistance(const vec3& pos, const vec3& X, const vec3& Z, float Fo, float Bo, float Lo, float Ro,
                           const vec3& A, const vec3& B, float M, float Wd);

enum class DriverMode { None = -1, Drive = 0, TurnBack = 1, Recover = 2, Hold = 3 };

struct DriverOrders {
    std::vector<int> route;  // W[0 … n − 1]
    vec3 D, Dd;
    int laps = 0;
    float s1 = 0, s2 = 0;
    std::array<bool, 6> sw{};  // 1 unused, 2 avoid traffic, 3 props, 4 players, 5 opponents, 6 path preference
    float kthr = 1.0f, kc = 1.0f, kb = 0.69999999f, kp = 75.0f;
};

// What a driver may meet (driver-planner.md §2.3).
struct Obstacle {
    enum Kind { None = 0, TrafficCarObs = 1, PropObs = 2, PlayerObs = 3, OpponentObs = 4 };
    Kind kind = None;
    int index = -1;  // traffic car index, collision-object index, player index, opponent index
    int regRoad = -1, regIx = -1;  // props: the road or intersection whose list held it
    bool operator==(const Obstacle& o) const { return kind == o.kind && index == o.index; }
};

// driver-planner.md §16 step 3, "Player tracking": the road a player is tracked on, kept by the
// AI map. Every frame a road component updates it (an intersection, a shortcut or nothing keeps
// it); every world reset looks at the car's start point.
struct PlayerTracking {
    int road = -1;  // none until the player is first tracked on a road
    int room = 0;   // the room found last frame (the next frame's hint)
    void frame(const RoadNetwork& net, const CityData& city, const vec3& bodyPos);
    // In the AI reset, before the car is put back: `startBody` = its body start point, `Z` and
    // `reversing` = its body Z row and reverse gear now.
    void worldReset(const RoadNetwork& net, const CityData& city, const vec3& startBody, const vec3& Z, bool reversing);
};

// A player car as the planner sees it.
struct DriverPlayer {
    const phys::Car* car = nullptr;
    int road = -1;  // the road the player is tracked on (−1: none)
};

class Driver;

// A race opponent as the planner sees it (driver-planner.md §16 step 4, §17.1, §18.5).
struct DriverOpponent {
    const phys::Car* car = nullptr;
    const Driver* driver = nullptr;
    int number = 0;  // its racer number (its index)
};

// The world a driver reads.
struct DriverEnv {
    RoadNetwork* net = nullptr;
    const CityData* city = nullptr;
    const TrafficSystem* traffic = nullptr;    // null or inactive: no traffic
    const phys::CollisionWorld* world = nullptr;
    const std::vector<int>* propOfCityObject = nullptr;  // CityData object → collision object (−1)
    std::vector<DriverPlayer> players;
    std::vector<DriverOpponent> opponents;  // race opponents (none in Cruise)
    double now = 0;     // real-time clock (wreck repair)
    bool firstPlayerReversing = false;
};

// One point of a planned path (driver-planner.md §3.1).
struct PathRecord {
    vec3 pos;
    float cost = 0, dist = 0;
    int slot = 0, prog = 0, mark = 0, arc = 0, cls = 0, zone = 0;
    Obstacle obj;
};

class Driver {
public:
    // Creation (driver.md, "Creation and reset"): the car, its base name (car kind), extents from
    // its bounding box (min, max).
    void create(phys::Car* car, const std::string& baseName, const vec3& boxMin, const vec3& boxMax, bool regenerate);
    void reset(DriverEnv& env);
    void giveOrders(const DriverOrders& o, DriverEnv& env);
    // One frame (driver.md, "One frame"); F = frame time.
    void frame(float F, DriverEnv& env);
    // Mirroring (police.md, "Holding the block"): the owner sets the outputs directly.
    void mirror(const vec3& target, float steer, float throttle, float brake, bool scaleL);

    // Yield marks (driver.md, "Yield marks").
    void yieldMarks(RoadNetwork& net, bool set) const;

    DriverMode mode() const { return mode_; }
    void setMode(DriverMode m) { mode_ = m; }
    bool wrecked() const { return wrecked_; }
    float throttleOut() const { return throttleC_; }
    const phys::CarInputs& carInputs() const { return in_; }
    phys::CarInputs& carInputs() { return in_; }
    const vec3& target() const { return T_; }
    const DriverOrders& orders() const { return orders_; }
    int window(int k) const { return S_[k]; }
    int windowDir(int k) const { return d_[k]; }
    int waypoint() const { return w_; }
    int lap() const { return lap_; }
    int routeLength() const { return n_; }
    // The racer number the planner's opponent test compares (opponents: their index; cops: their
    // index in the police list).
    void setRacerNumber(int n) { racer_ = n; }
    int racerNumber() const { return racer_; }
    // What others read (driver.md, "What others read from a driver"): the current road id (the
    // window road of the slot recorded in the planner's working point 0) and the section on S0
    // (mirrored for d0 = −1), −1 when S0 is none.
    int currentRoad() const;
    int sectionOnS0(const RoadNetwork& net) const;
    // The touched-the-player mark of the last physics update (car-collision.md 5.4).
    void setTouchedPlayer(bool t) { touchedPlayer_ = t; }
    float front() const { return F_; }
    float back() const { return B_; }
    float left() const { return Lx_; }
    float right() const { return Rx_; }
    // The chosen path (debug drawing): count and points.
    int pathCount() const;
    const PathRecord& pathPoint(int i) const;

private:
    // ---- driver.md ----
    void driveEntry();
    void driveUpdate(float F, DriverEnv& env);
    void turnBackEntry();
    void turnBackUpdate();
    void turnBackFinish();
    void recoverEntry();
    void recoverUpdate(float F, DriverEnv& env);
    void holdUpdate();
    void locate(DriverEnv& env, bool preferred);
    void routeProgress(DriverEnv& env);
    void enterRoad(int R, DriverEnv& env);
    void fillWindow(DriverEnv& env);
    void junctionAngles(DriverEnv& env);
    void speedControl(DriverEnv& env);
    void brakeWith(float b);
    void stuckWatch(float F);
    int roadBetweenW(DriverEnv& env, int a, int b, int& d) const;
    void clearPaths();
    // ---- driver-planner.md ----
    struct Ctx;
    void plan(DriverEnv& env);
    void junctionTurns(DriverEnv& env);
    void refreshBends(DriverEnv& env, int road, int sigma);
    void expand(Ctx& c, int k);
    void cont(Ctx& c, int k);
    int arcTest(Ctx& c, int q);
    int placeArc(Ctx& c, int& k, int src);
    void finish(Ctx& c, int k, bool withAngle);
    void destinationStep(Ctx& c, int k);
    void recordPoint(Ctx& c, int k, int s, int q);
    void finalCorridor(Ctx& c, int k);
    void routeCorridor(Ctx& c, int k);
    Obstacle obstacleScan(Ctx& c, const vec3& A, const vec3& B, int sA, int qA, int sB, int qB, float M, int& cls);
    float blockDistance(Ctx& c, const Obstacle& o, const vec3& A, const vec3& B, float M, float Wd);
    vec3 obstaclePosition(Ctx& c, const Obstacle& o) const;
    void passingPoints(Ctx& c, const Obstacle& o, const vec3& from, const vec3& dir, float clr, vec3& A, vec3& B);
    bool windowPlace(Ctx& c, const Obstacle& o, int& wo, int& ov);
    struct Alt {
        vec3 pt;
        Obstacle o;
        int zone = 1, mark = 0;
    };
    void collectAlternatives(Ctx& c, int k, const Obstacle& O, bool walk, std::vector<Alt>& out);
    void follow(Ctx& c, int k, const vec3& pt, const Obstacle& O, int side, int depth, bool walk, std::vector<Alt>& out);
    bool aheadStd(Ctx& c, int k, const vec3& Y);
    void useAlternative(Ctx& c, int k, const Alt& a, int cls);
    int roadOfSlot(int s) const;
    int sigmaOfSlot(int s) const;
    float aSlot(int s) const;
    vec3 JSlot(int s) const;

    phys::Car* car_ = nullptr;
    phys::CarInputs in_;
    DriverMode mode_ = DriverMode::Drive, prevMode_ = DriverMode::None;
    DriverOrders orders_;
    int n_ = 1, w_ = 1, lap_ = 1;
    bool routeDone_ = false;
    int locKind_ = 3, locIndex_ = 1, room_ = 0;
    int S_[3] = {-1, -1, -1}, d_[3] = {1, 1, 1};
    int f_ = 0;
    int lastS0_ = -1;
    float a_[2] = {}, tau_[2] = {};
    vec3 J_[2], G_[2], Sdir_[2], Edir_[2];
    float h_[2] = {}, rj_[2] = {}, tj_[2] = {};
    vec3 T_;
    float brakeC_ = 0, throttleC_ = 0, steerC_ = 0;
    bool wrecked_ = false;
    double wreckStart_ = 0;
    bool regenerate_ = false;
    int carKind_ = -1;
    int turnBackCounter_ = 0;
    enum class Watch { Idle, Watch, Stuck } watch_ = Watch::Idle;
    float watchTimer_ = 0;
    vec3 watchStart_;
    float F_ = 0, B_ = 0, Lx_ = 0, Rx_ = 0;
    bool touchedPlayer_ = false;
    int racer_ = -1;
    // Destination place.
    int destKind_ = 0, destIndex_ = 0;
    float lambda0_ = 0;
    // Planner state (§3).
    // One array (driver-planner.md §3.1, "One array"): the working path Q0 … Q39, then the 25
    // rows of 40 records; row r's record i is array record 40·(r + 1) + i.
    static constexpr int kRecords = 40 + 25 * 40;
    std::array<PathRecord, kRecords> Q_{};
    static int rowRecord(int r, int i) { return 40 * (r + 1) + i; }
    std::array<int, 25> rowLen_{};
    std::array<bool, 25> rowBlocked_{}, rowZone2_{};
    int rowCount_ = 0, chosen_ = -1;
    vec3 REF_, REF0_;
    int FINAL_ = 0;
};

}  // namespace ai
}  // namespace cc
