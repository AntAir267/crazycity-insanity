// Race opponents (spec/ai/opponents.md, spec/formats/opp.md): which opponents exist, their cars,
// start points and ground snap, the orders they drive with (the AI driver of driver.h, never a
// route search), their per-frame update, the finish-line test and their physics status.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/driver.h"
#include "formats/aimap.h"
#include "formats/race_files.h"
#include "physics/car.h"
#include "physics/scene.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;
struct VehicleModel;

namespace phys {
class CollisionWorld;
class World;
struct Bound;
}  // namespace phys

namespace ai {

class RoadNetwork;

enum class OpponentState { None = -1, Racing = 0, Disabled = 1 };

// An opponent's route file resolved against the road network (opp.md, "Meaning of the points").
struct OpponentRoute {
    vec3 start;
    float heading = 0;        // θ = float(h × 0.017444445), radians
    std::vector<int> route;   // W[0 … n − 1]: intersection numbers
    vec3 destination;         // D
    int points = 0;           // N
    int pointsWithoutIntersection = 0;  // route points left out (engine choice)
};
OpponentRoute resolveOpponentRoute(const OppFile& f, const CityData& city, const RoadNetwork& net);

// The finish line of opponents.md, "The finish test": one value for the whole program run, set
// by the Checkpoint and Circuit set-ups (waypoints.md, last section).
struct FinishLine {
    vec3 F, U;
};
FinishLine& opponentFinishLine();
// F = the waypoint's position, U = (float(sin a), 0, float(cos a)), a = float(heading × −0.017453292).
void setOpponentFinishLine(const vec3& pos, float headingDegrees);

struct Opponent {
    int index = 0;
    OpponentRow row;
    std::unique_ptr<phys::Car> car;
    std::shared_ptr<VehicleModel> model;
    int paint = 0;
    bool box = true;
    phys::BoxShape shape;
    vec3 boxMin, boxMax;
    Driver driver;
    OpponentRoute route;
    vec3 q;            // reset point
    float theta = 0;   // reset heading
    OpponentState state = OpponentState::Racing, prev = OpponentState::None;
    float side = 0.0f;  // the finish test's side value S (never reset)
    bool roomWaking = false;  // physics status: within 200 m of a player
    bool touchedPlayer = false;
    int identity = 0;
};

class OpponentSystem {
public:
    OpponentSystem();
    ~OpponentSystem();

    struct Setup {
        std::vector<OpponentRow> rows;  // the race aimap's rows read
        int countLine = 0;              // c: the [Opponent] count line
        float opponentNumber = 0;       // s: the session's opponent number
        int laps = 1;                   // the orders' laps (Circuit: the session's lap count)
        bool regenerate = false;        // Circuit
        std::string raceDir;            // race/<RaceDir>/<route file>
        std::string playerCar;          // a model the player loaded first keeps the player's bound
        std::shared_ptr<phys::Bound> playerBound;
        bool loadModels = true;         // drawing (the native tools leave it off)
        bool cableCars = false;         // vpcop paint rule
    };
    // Creates the opponents (opponents.md, "Which opponents exist"). Logs problems.
    void create(const Vfs& vfs, AssetCache* assets, const CityData& city, RoadNetwork& net, phys::CollisionWorld& world,
                const Setup& s);
    void clear();
    bool active() const { return !opps_.empty(); }
    static int countFor(int countLine, float opponentNumber);

    // The world reset: state RACING, previous none, the car placed at q with θ, the driver's reset.
    void reset(double now);
    // The ground snap of every opponent (once, after the first reset).
    void groundSnap(phys::World& world);

    // The update of every opponent in index order (opponents.md, "One frame"), between the
    // yield-mark passes of the AI frame. `env` carries the network, traffic, players and opponents.
    void update(float F, DriverEnv& env, const std::vector<const phys::Car*>& players);
    void yieldMarks(bool set) const;
    // The cars' inputs before the physics (not drivable: brake and neutral).
    void applyInputs();
    void appendMovers(std::vector<phys::SceneMover>& movers);
    void afterPhysics(const std::vector<phys::SceneMover>& movers);
    // The planner's view of the opponents (DriverEnv::opponents).
    std::vector<DriverOpponent> driverView() const;

    // The finish test of opponent i (it updates the side value).
    bool finishTest(int i, const FinishLine& line);
    static bool finishTest(Opponent& o, const FinishLine& line);

    // One opponent's pieces, shared with the opponent check run (drive-check.md).
    static void giveOrders(Opponent& o, DriverEnv& env, int laps, RoadNetwork* net);
    static void updateOne(Opponent& o, float F, DriverEnv& env, const std::vector<const phys::Car*>& players, int laps,
                          RoadNetwork* net);
    // The ground snap of one car at reset point q with heading θ (q may change).
    static void groundSnap(phys::Car& car, vec3& q, float theta, phys::World& world);

    const std::vector<Opponent>& opponents() const { return opps_; }
    std::vector<Opponent>& opponents() { return opps_; }

private:
    RoadNetwork* net_ = nullptr;
    const CityData* city_ = nullptr;
    int laps_ = 1;
    std::vector<Opponent> opps_;
};

}  // namespace ai
}  // namespace cc
