// The police (spec/ai/police.md): parked cops from the race file's police list, detection,
// pursuit with route search and the AI driver, blocking, the roster, the end of a chase, the yield
// marks, the light bar, and the police's own random generator.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/driver.h"
#include "core/game_random.h"
#include "formats/aimap.h"
#include "model/light_bar.h"
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
}

namespace ai {

class RoadNetwork;
class TrafficSystem;

enum class CopState { None = -1, Idle = 0, Apprehend = 1, Follow = 2, Unknown = 3, Out = 4 };
enum class CopTactic { Ram, Block, BlockWait };

// A player car as the police see it.
struct PolicePlayer {
    phys::Car* car = nullptr;
    int room = 0;
    vec3 boxMin, boxMax;  // its bound's box (extents)
};

struct Cop {
    int index = 0;
    PoliceEntry entry;
    std::unique_ptr<phys::Car> car;
    std::shared_ptr<VehicleModel> model;
    int paint = 0;
    bool box = true;
    phys::BoxShape shape;
    vec3 boxMin, boxMax;
    Driver driver;
    CopState state = CopState::Idle, prev = CopState::None;
    CopTactic tactic = CopTactic::Ram;
    int perp = -1;      // target index: players first, then the race opponents
    int lastPerp = -1;  // the car this cop chased last (kept over resets; opponent registration)
    std::vector<char> refused;  // per opponent: this cop decided not to chase it
    float range = 0;
    Component perpPlace;
    std::vector<int> route;
    LightBar lights;
    bool mover = false;        // a physics mover this frame
    bool touchedPlayer = false;
    int identity = 0;
};

// The sound system's view of the police (spec/audio/traffic-sounds.md, "Police"); no effect on the
// police itself.
struct PoliceSoundHooks {
    virtual ~PoliceSoundHooks() = default;
    // A cop enters FOLLOW; `localPlayer`: its perp is the local player's car (the chase count).
    virtual void sirenStart(const Cop& c, bool localPlayer) = 0;
    // The perp escapes (with or without explosion) or the cop is reset.
    virtual void sirenStop(const Cop& c) = 0;
    // The perp escapes with explosion (the cop is wrecked); the siren stop follows at once.
    virtual void explosion(const Cop& c) = 0;
};

class PoliceSystem {
public:
    PoliceSystem();
    ~PoliceSystem();

    // The cops of race/<city>/roam.aimap for cop density `density` (0 = none). `playerCar` and
    // `playerBound`: a player driving the same model shares its bound (police.md, "The cop's car").
    void create(const Vfs& vfs, AssetCache& assets, const CityData& city, RoadNetwork& net, phys::CollisionWorld& world,
                float density, const std::string& playerCar, std::shared_ptr<phys::Bound> playerBound);
    void clear();
    bool active() const { return !cops_.empty(); }
    // A world reset: the generator restarts, the roster empties, every cop is parked.
    void reset();

    // Step 3 of the AI frame (police.md, "One frame of police"); the cars' inputs are applied at
    // the end. `now` is the real-time clock.
    void update(float F, const std::vector<PolicePlayer>& players, const TrafficSystem* traffic, double now);
    // The same in pieces, for race modes, where the yield marks of the opponents and the cops are
    // cleared before the opponents update and set after the cops (police.md, "One frame of
    // police"): clearMarks, (opponents), updateCops, setMarks, applyInputs. `opponents` are the
    // race opponents as targets (detection step 2), `oppView` the planner's view of them.
    void clearMarks() const;
    void updateCops(float F, const std::vector<PolicePlayer>& players, const TrafficSystem* traffic, double now,
                    const std::vector<PolicePlayer>& opponents, const std::vector<DriverOpponent>& oppView);
    void setMarks() const;
    void applyInputs();
    // Crash Course (police.md, "Caught"): is any cop in sight of the player's car?
    bool copInSight(const phys::Car& player, phys::World& world) const;
    // Cops that are movers this frame (U C V O), in list order.
    void appendMovers(std::vector<phys::SceneMover>& movers);
    // After the frame's physics: touched-the-player marks, light bars of the cops that moved.
    void afterPhysics(const std::vector<phys::SceneMover>& movers, float F);

    const std::vector<Cop>& cops() const { return cops_; }
    std::vector<Cop>& cops() { return cops_; }
    // Roster contents for the HUD / tests: chased player and its cops per slot.
    struct Roster {
        int nCars = 0;
        int car[3] = {-1, -1, -1};
        int nCops[3] = {0, 0, 0};
        int cops[3][3] = {{-1, -1, -1}, {-1, -1, -1}, {-1, -1, -1}};
    };
    const Roster& roster() const { return roster_; }
    int chasing() const;  // cops not IDLE/OUT
    void setSoundHooks(PoliceSoundHooks* h) { sound_ = h; }
    uint32_t randomState() const { return prnd_.state(); }

private:
    void resetCop(Cop& c);
    void updateCop(Cop& c, float F, const std::vector<PolicePlayer>& players);
    void detection(Cop& c, const std::vector<PolicePlayer>& players);
    std::vector<PolicePlayer> targets_;  // this frame: the players, then the opponents
    size_t playerCount_ = 0;
    std::vector<DriverOpponent> oppView_;
    void startChasing(Cop& c, int p, const std::vector<PolicePlayer>& players);
    void following(Cop& c, const std::vector<PolicePlayer>& players);
    void apprehend(Cop& c, const std::vector<PolicePlayer>& players);
    void giveOrders(Cop& c, const vec3& D, const vec3& Dd, float s1, float s2, float kc, const PolicePlayer& perp);
    void escapes(Cop& c, bool explosion);
    void mirrorPerp(Cop& c, const PolicePlayer& perp);
    bool registerCop(int c, int p);
    void unregisterCop(int c, int p);
    DriverEnv env(const std::vector<PolicePlayer>& players) const;

    const CityData* city_ = nullptr;
    RoadNetwork* net_ = nullptr;
    phys::CollisionWorld* world_ = nullptr;
    const TrafficSystem* traffic_ = nullptr;
    std::vector<int> propOf_;
    std::vector<Cop> cops_;
    Roster roster_;
    Lcg prnd_;
    float Dg_ = 250.0f;
    double now_ = 0;
    PoliceSoundHooks* sound_ = nullptr;
};

}  // namespace ai
}  // namespace cc
