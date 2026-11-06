// Ambient traffic (spec/ai/traffic.md and its pages traffic-population.md, traffic-driving.md,
// traffic-reactions.md, traffic-visuals.md; spec/formats/aivehicledata.md): the pool of scripted
// cars, filling and emptying roads as players change rooms, driving on the lane rails, lights and
// stop signs, lane changes, avoiding players, rejoining, and the simple physical body of a car that
// was hit.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "ai/road_network.h"
#include "core/game_random.h"
#include "core/math.h"
#include "physics/rigid_body.h"
#include "physics/scene.h"
#include "physics/traffic_body.h"
#include "physics/world.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;
struct VehicleModel;

namespace phys {
class CollisionWorld;
}

namespace ai {

// One traffic car type (aivehicledata.md).
struct TrafficType {
    std::string name;
    std::shared_ptr<VehicleModel> model;
    bool dataFound = false;
    float mass = 0;
    vec3 size, cg;
    float spring = 0, damping = 0, limit = 0, rubberSpring = 0, rubberDamp = 0;
    float maxDamage = 0, elasticity = 0, friction = 0, ptxThresh = 0;
    vec3 maxAng;
    // Derived numbers.
    vec3 boxMin, boxMax;
    float boxRadius = 0;
    float front = 0, back = 0, left = 0, right = 0;
    vec3 wheelPos[6];
    bool hasWheel[6] = {};
    bool wheelMesh[6] = {};  // WHL4/WHL5 drawn only with an _H mesh
    float wheelRadius = 0;
    float bodyRadius = 0;  // largest BODY level radius (drawing)
    int setCount = 1;
    phys::TrafficBodyType body;  // the physics of a knocked car (car-collision.md 6.1)
};

enum TrafficState { TrafficDriving = 0, TrafficKnocked = 2, TrafficRejoining = 3, TrafficAvoiding = 4, TrafficOut = 6 };

// What traffic needs to know about a player (traffic-reactions.md).
struct TrafficPlayer {
    vec3 modelOrigin;  // P_m
    vec3 bodyPos;      // P_c (centre of mass)
    float radius = 1;  // R_p: BODY radius
    bool horn = false;
    bool siren = false;
    // The body's rows and its box half extents (InertiaBox / 2), for the cable cars' block test.
    vec3 X{1, 0, 0}, Z{0, 0, 1};
    vec3 half{1, 1, 2};
};

// A cable car (cable-cars.md): driven on the cable lines, seen by the other AI through its
// TrafficCar entry, a breakable prop in the collision world.
struct CableCar {
    int car = -1;              // its entry in TrafficSystem::cars()
    int startRoad = 0, startSide = 1;
    int partner = -1;
    float ac = 2.5f;           // acceleration a_c
    bool active = true;
    bool turning = false;
    bool permission = false;
    float eps = 0;             // ε: distance along the road
    int prop = -1;             // its collision object
};

// A physical traffic car (car-collision.md 4-6): the car index and its body, in the pool's list.
struct TrafficBodySlot {
    int car = -1;
    std::unique_ptr<phys::TrafficPhys> phys;
};

struct TrafficCar {
    int index = 0;
    int type = -1;  // −1: no type (never placed)
    int paint = 0;
    // Per-car constants (traffic-driving.md).
    float o = 0;     // lateral offset
    int T0 = 8;      // reaction delay, frames
    float A = 5, G = 0.5f, E = 0;
    int blink = 0;   // blink phase 0 … 15
    // Lane state.
    bool onStreets = false;
    int road = -1, side = 1, lane = 0, listLane = 0;
    NextChoice next;  // road −1: none
    float x = 0;
    int rail = 0;     // 0 lane, 1 turn piece, 2 lane change, 3 rejoining curve
    int k = 1;
    float u = 0, len = 1, invLen = 1;
    float xLC = 0;
    PieceCurve curve;
    float v = 0, a = 0, vt = 0;
    float lookAhead = 25;
    bool go = false, queued = false;
    int16_t react = 0;
    bool lcAllowed = true;
    int state = TrafficDriving, prevState = TrafficDriving;
    int tick = 0;  // frames in the current state (0: its start runs)
    int indicator = 0;
    // Pose.
    vec3 pos;
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    bool groundSolved = false;
    phys::ProbeState probes[3];
    std::array<bool, 4> solvedNear{};  // one-off solve per player while stopped
    int room = 0;
    float wheelAngle = 0;
    // Obstacle registration (traffic-reactions.md, "Being seen by other AI"): 0 none, 1 on road
    // regRoad's side regSide section list regSection, 3 in intersection regIx's list; the car's own
    // network-room memory (hint of the component look-up).
    int regKind = 0;
    int regRoad = -1, regSide = 1, regSection = 0, regIx = -1;
    int netRoom = 0;
    // When the car was last filed into a room's object list (collision-objects.md, "Room lists").
    int filedRoom = 0;
    long long filed = 0;
    // Route place for other drivers' window_place (§18.5): rail road, travel side, piece index
    // (n in the turn piece); knocked and avoiding cars keep their last values.
    int placeRoad = -1, placeSide = 1, placeK = 1;
    // Reactions.
    int avoidPlayer = -1;
    float avoidTheta = 0, avoidR = 0, avoidS = 0;
    bool avoidCentred = false;
    float base = 0, mergeL = 30;
    int attempts = 1;
    vec3 rejoinPos;
    bool wreck = false;
    int body = -1;  // index into bodies(), −1 none
    int updatedFrame = -1;
    bool cable = false;  // a cable car's entry (never in the pool or the lane lists)
};

// The sound system's view of traffic (spec/audio/traffic-sounds.md, "Events this page needs"); no
// effect on the traffic itself.
struct TrafficSoundHooks {
    virtual ~TrafficSoundHooks() = default;
    // Step 2 of one car update ("sound position"), in update order.
    virtual void carUpdated(const TrafficCar& c) = 0;
    // A car starts avoiding a player (the honk event).
    virtual void avoidStart(const TrafficCar& c) = 0;
    // A car was placed on the streets (its sound source is reset).
    virtual void placed(const TrafficCar& c) = 0;
};

class TrafficSystem {
public:
    TrafficSystem();
    ~TrafficSystem();

    // Creates the pool for density `density` (0 … 1; 0 = no traffic at all) and pool size `poolSize`.
    void create(const Vfs& vfs, AssetCache& assets, CityData& city, phys::CollisionWorld& world, float density,
                int poolSize = 300);
    void clear();  // no traffic (the drive ended)
    // The size of the pedestrian pool the session creates after this one (cable-cars.md, "Which
    // cable cars exist" step 3: its creation draws, 2 per pedestrian, come before the cable cars'
    // in MM2's generator). Set before create().
    void setPedestrianPool(int count) { pedPool_ = count > 0 ? count : 0; }
    bool active() const { return net_ != nullptr && density_ > 0.0f; }
    float density() const { return density_; }
    // A traffic reset: every car off the streets, the generator restarted, the streets filled
    // around each player (traffic-population.md).
    void reset(const std::vector<TrafficPlayer>& players);

    // The AI update of one frame (traffic.md, "Frame order"): updateCars(), then with `late`
    // lateUpdate() at once. A caller that runs the pedestrians, opponents and police in between
    // calls updateCars(), them, then lateUpdate().
    void update(float F, const std::vector<TrafficPlayer>& players, bool late = true);
    // Steps 1.1-1.2: the players' rooms and the traffic cars.
    void updateCars(float F, const std::vector<TrafficPlayer>& players);
    // Steps 5-6: the cable cars, then finishFrame().
    void lateUpdate(float F, const std::vector<TrafficPlayer>& players);
    // Step 6: the body bookkeeping, the blink clock, the light sets and their wake-ups.
    void finishFrame(float F);
    const std::vector<CableCar>& cableCars() const { return cables_; }
    // The session's cable-car switch (cable-cars.md): off while a network game's world is built
    // (multiplayer.md 1.4), so create() makes none. On by default.
    void setCableCarSwitch(bool on) { cableSwitch_ = on; }
    // Test helper (cable-cars.md, "Checked against"): no stops, no blocking, a_c = 2.5.
    void setCableFreeRun(bool on) {
        cableFreeRun_ = on;
        if (on)
            for (CableCar& cc : cables_) cc.ac = 2.5f;
    }
    const std::shared_ptr<phys::LooseData>& cableData() const { return cableData_; }
    // Before the frame's physics: bodies dropped from the pool (car-collision.md 6.5, last
    // paragraph); then the movers of traffic for the scene (physical cars in pool order, and the
    // row-3 cars without a body) and the lane cars movers gather.
    void prePhysics();
    void appendMovers(std::vector<phys::SceneMover>& movers) {
        appendRowMovers(movers);
        appendBodyMovers(movers);
    }
    // Row 3 of car-collision.md 1.1 (avoiding/rejoining/wrecked cars without a body) and row 6
    // (physical cars in pool order); opponents and police go between them.
    void appendRowMovers(std::vector<phys::SceneMover>& movers);
    void appendBodyMovers(std::vector<phys::SceneMover>& movers);
    void laneCars(std::vector<phys::LaneCar>& out) const;
    // A contact made car `id` physical (4.2): its body set up from the lane matrix and speed.
    phys::TrafficPhys* takeBody(int id, const phys::Mtx& lane, float speed);
    // After the frame's physics: bodies that rest or fell end their physics (6.5).
    void afterPhysics();

    // Race opponents' body positions (traffic-population.md, "Placing" step 4: no car is placed
    // within 50 m of an opponent). Null or empty: none (Cruise). Read while the streets fill.
    void setOpponentPositions(const std::vector<vec3>* p) { opponentPos_ = p; }

    const std::vector<TrafficCar>& cars() const { return cars_; }
    const std::vector<TrafficType>& types() const { return types_; }
    const TrafficType& typeOf(const TrafficCar& c) const { return types_[size_t(c.type)]; }
    float blinkClock() const { return blinkClock_; }
    bool blinkOn(const TrafficCar& c) const;
    // Model frame of a car (rows X, Y, Z, origin) as a column-major matrix.
    mat4 matrixOf(const TrafficCar& c) const;
    // Wheel draw transform (car space) of wheel i, with spin or (knocked) deflections.
    mat4 wheelTransform(const TrafficCar& c, int i) const;
    int carsOnStreets() const;
    int activeRoadCount() const { return int(activeRoads_.size()); }

    const std::vector<TrafficBodySlot>& bodies() const { return bodies_; }
    void setSoundHooks(TrafficSoundHooks* h) { sound_ = h; }

    // Obstacle lists for the AI drivers (driver-planner.md §16): cars registered per road side and
    // section (1 … n − 1; side A counted against the road) and per intersection, newest first.
    const std::vector<int>& sectionCars(int road, int side, int section) const;
    const std::vector<int>& intersectionCars(int intersection) const;

    // Event counters for the HUD / tests.
    struct Stats {
        int placed = 0, emptied = 0, leftStreets = 0, turnedRound = 0, laneChanges = 0;
        float lastLaneLength = 0;  // total entering lane length L of the last adjust
    };
    // Filling and emptying of this frame (traffic check runs).
    struct FillEvent {
        int road = 0;
        bool entering = true;
        int cars = 0;
    };
    const std::vector<FillEvent>& events() const { return events_; }
    // Test helper: a reset with the streets filled as for a player entering `room` from room 0.
    void resetAtRoom(int room);
    const Stats& stats() const { return stats_; }

private:
    // Population.
    void adjust(int oldRoom, int newRoom, int player);
    void emptyRoad(int road);
    bool place(int road, int side, int lane, float x);
    void freeCar(int car);
    bool isActive(int road) const;
    // Lane lists.
    std::vector<int>& laneList(int road, int side, int lane);
    const std::vector<int>& laneList(int road, int side, int lane) const;
    void sortedInsert(int car, int road, int side, int lane, float x);
    void append(int car, int road, int side, int lane);
    void removeFrom(int car, int road, int side, int lane);
    int carAhead(int car) const;   // in its list lane
    int carAheadInList(int car, int road, int side, int lane) const;
    // Driving.
    void updateCar(int car, float F, const std::vector<TrafficPlayer>& players);
    void driving(TrafficCar& c, float F, const std::vector<TrafficPlayer>& players);
    void startOnRail(TrafficCar& c);
    float targetSpeed(const TrafficCar& c) const;
    void raise(TrafficCar& c) const;
    float delta(const TrafficCar& c, const TrafficCar& o) const;
    void follow(TrafficCar& c, const TrafficCar& o, float d) const;
    bool askIntersection(TrafficCar& c, float dEnd);
    void advance(TrafficCar& c);
    void arrive(TrafficCar& c);
    void tryLaneChange(TrafficCar& c);
    void endLaneChange(TrafficCar& c);
    void chooseNext(TrafficCar& c);
    void setPiece(TrafficCar& c, const Piece& p);
    float laneLen(int road, int side, int lane) const;
    void wakeLane(int road, int side, int lane, int fromCar);
    void processWakes();
    // Reactions.
    bool inPath(const TrafficCar& c, const TrafficPlayer& p) const;
    bool screened(const TrafficCar& c, const TrafficPlayer& p) const;
    bool noticePlayer(TrafficCar& c, const std::vector<TrafficPlayer>& players, bool screening);
    void setState(TrafficCar& c, int state);
    void avoiding(TrafficCar& c, float F, const std::vector<TrafficPlayer>& players);
    void rejoining(TrafficCar& c, float F, const std::vector<TrafficPlayer>& players);
    void startRejoin(TrafficCar& c);
    void knocked(TrafficCar& c);
    void endPhysics(int car);
    void projectOnLane(TrafficCar& c, int road, int side, int lane);
    // Visuals.
    void solve(TrafficCar& c, const std::vector<TrafficPlayer>& players);
    void freeHeadingSolve(TrafficCar& c);
    void probeCorners(TrafficCar& c, const vec3 corners[3], float yPrev, int tries, float stepY, bool fromCurrent);
    void updateRoom(TrafficCar& c);
    void registerObstacle(TrafficCar& c);
    void unregister(TrafficCar& c);
    // Bodies.
    void dropBody(int car);
    void release(int car);  // 6.5 steps 1-4
    // Cable cars (cable_cars.cpp).
    void createCableCars(const Vfs& vfs, AssetCache& assets);
    bool cableSwitch_ = true;
    void resetCableCars();
    void resetCableCar(CableCar& cc);  // one car's part of the AI reset (cable-cars.md)
    void updateCableCars(float F, const std::vector<TrafficPlayer>& players);
    NextChoice cableNext(int road, int s) const;
    Piece cablePiece(int road, int s, int k, float f) const;
    Piece cableTurnPiece(int road, int s, const NextChoice& next, float f) const;
    bool cableBlocked(const TrafficCar& c, float& b, const std::vector<TrafficPlayer>& players) const;
    void cablePose(TrafficCar& c);
    void cableRegister(TrafficCar& c);
    void cableSetPiece(TrafficCar& c, const Piece& p);
    phys::Mtx cableMatrix(const TrafficCar& c) const;
    std::vector<CableCar> cables_;
    int cableType_ = -1;
    bool cableFreeRun_ = false;
    std::shared_ptr<phys::LooseData> cableData_;

    const CityData* city_ = nullptr;
    RoadNetwork* net_ = nullptr;
    phys::CollisionWorld* world_ = nullptr;
    float density_ = 0, d_ = 0;
    bool laneChanges_ = true;
    Lcg rng_;
    std::vector<TrafficType> types_;
    std::vector<TrafficCar> cars_;
    std::vector<int> free_;  // back = front of MM2's list
    std::vector<std::vector<std::vector<std::vector<int>>>> lanes_;  // [road][side slot][lane] → cars, front first
    std::vector<int> activeRoads_;  // front first
    std::vector<uint8_t> wanted_;   // per road: bit per player
    std::vector<int> playerRoom_;
    std::vector<TrafficBodySlot> bodies_;
    std::vector<int> rowMovers_;  // row 3 of car-collision.md 1.1, in visiting order
    // Obstacle registrations of the current frame.
    std::vector<std::vector<std::vector<std::vector<int>>>> sectionCars_;  // [road][side slot][section]
    std::vector<std::vector<int>> ixCars_;
    float blinkClock_ = 0;
    int frame_ = 0;
    long long fileSeq_ = 0;
    int pedPool_ = 0;
    int cableSkip_ = 0;  // draws discarded before the cable cars' (cable-cars.md step 3)
    Stats stats_;
    std::vector<FillEvent> events_;
    TrafficSoundHooks* sound_ = nullptr;
    const std::vector<vec3>* opponentPos_ = nullptr;
};

}  // namespace ai
}  // namespace cc
