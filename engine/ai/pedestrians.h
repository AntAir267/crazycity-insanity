// Pedestrians (spec/ai/pedestrians.md, pedestrian-walking.md, pedestrian-reactions.md;
// spec/formats/ped-anim.md): the pool, filling and emptying the pedestrian roads as players change
// rooms, walking along the sidewalk lines, crossing at walk lights, stepping around props and slow
// player cars, and the reactions to the nearest player's car. Pedestrians are never hit: they take
// no part in the physics and nothing reads their state.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/road_network.h"
#include "core/game_random.h"
#include "core/math.h"
#include "model/ped_model.h"
#include "physics/world.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;

namespace phys {
class CollisionWorld;
}

namespace ai {

class TrafficSystem;

// What pedestrians need to know about a player's car (pedestrians.md, "Terms").
struct PedPlayer {
    vec3 bodyPos;      // P_c: every pedestrian test and the room tracking (pedestrians.md)
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};  // body matrix rows
    vec3 velocity;     // body linear velocity (for v̂)
    float speed = 0;   // the car's speed (vehicle.md, "Speed")
    float steer = 0;   // −1 full left … +1 full right
    int gear = 1;      // 0 reverse
    vec3 inertiaBox{2.0f, 1.0f, 3.0f};
    float radius = 1;  // R_p: the BODY radius
};

enum PedMode { PedWalking = 0, PedWary = 1, PedDodging = 2 };
enum PedStep { PedStepWalk = 0, PedStepToKerb = 1, PedStepWait = 2, PedStepCross = 3 };

struct Pedestrian {
    int index = 0;
    int model = -1;    // index into PedestrianSystem::models(); −1: the model did not load (never placed)
    int variant = 0;   // paint variant
    bool female = false;
    bool onStreets = false;
    bool lost = false;  // its heading became NaN (pedestrian-walking.md, "Stepping around"): not drawn,
                        // moved or updated; it only ticks the shared clock
    // Where it walks (pedestrian-walking.md, "State").
    int road = -1, side = 1, dir = 1;
    int prevRoad = -1, prevSide = 1, prevDir = 1;
    int k = 1;
    float x = 0, o = 0, h = 0;
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
    vec3 p;
    int mode = PedWalking, step = PedStepWalk, seenMode = -1, seenStep = -1;
    int wall = 0;
    vec3 wallHit;
    bool wallNear = false;  // this frame's wall probe hit within 1.25 m
    int choice = 0;
    vec3 target;
    int nearest = -1;
    float q = 100000.0f;
    float diveStart = 0;
    bool reverse = false;
    float speed = 0;
    // The curve of its segment (pedestrian-walking.md, "The curve").
    PieceCurve curve;
    // Animation (ped-anim.md, "Playback").
    int cur = -1, next = -1, frame = 0;
    phys::ProbeState probe, wallProbe;
    int room = 0;
    int nextInRoad = -1;  // the pedestrian after it in its road's list
    int updatedFrame = -1;
    int updates = 0;
    bool voiceReset = true;
};

// A 3D sound source event for the sound milestone (pedestrians.md, "Sounds"): the avoidance voice.
struct PedVoiceEvent {
    int ped = 0;
    vec3 pos;
    bool female = false;
    int reason = 0;  // 0 dive start, 1 run to a wall while dodging, 2 leaving the back-to-the-wall pose
};

class PedestrianSystem {
public:
    PedestrianSystem();
    ~PedestrianSystem();

    // count = trunc(float(P) × ρ); 0 when off or in a Circuit race (pedestrians.md, "Settings").
    static int countFor(int poolSize, float density, bool off = false, bool circuit = false);

    // The pool (pedestrians.md, "The pool"): `count` pedestrians; the bad-weather models when
    // `rain`. The generator restarts with x = 1 before the pool (engine choice).
    void create(const Vfs& vfs, AssetCache& assets, const CityData& city, phys::CollisionWorld& world, int count, bool rain);
    void clear();
    bool active() const { return net_ != nullptr && !peds_.empty(); }
    // A reset: the generator restarts, every road emptied, the pool back on the free list, then each
    // player's fill as if it entered its room from room 0.
    void reset(const std::vector<PedPlayer>& players);
    // Test helper: a reset with the fill for a player entering `room` from room 0.
    void resetAtRoom(int room);
    // Test helper: set a pedestrian's heading (a NaN makes it lost at its next update).
    void testSetHeading(int ped, float h) {
        if (ped >= 0 && size_t(ped) < peds_.size()) peds_[size_t(ped)].h = h;
    }
    int playerRoom(int player) const {
        return player >= 0 && size_t(player) < playerRoom_.size() ? playerRoom_[size_t(player)] : 0;
    }
    float clockAccumulator() const { return acc_; }
    // The pedestrians' part of one frame's AI update (pedestrians.md, "Frame order": the players'
    // room changes and adjusts, then every pedestrian of the active roads). `traffic` (may be null)
    // answers the accident tests; light states are read from the network as they are before this
    // frame's light update.
    void update(float F, const std::vector<PedPlayer>& players, const TrafficSystem* traffic);

    const std::vector<Pedestrian>& peds() const { return peds_; }
    const std::vector<std::unique_ptr<PedModel>>& models() const { return models_; }
    const PedModel* modelOf(const Pedestrian& p) const {
        return p.model >= 0 && size_t(p.model) < models_.size() ? models_[size_t(p.model)].get() : nullptr;
    }
    // World matrix of a pedestrian (rows X, Y, Z, position) as a column-major matrix.
    mat4 matrixOf(const Pedestrian& p) const;
    int onStreets() const;
    int lostCount() const;
    int count() const { return int(peds_.size()); }
    const std::vector<int>& activeRoads() const { return activeRoads_; }
    // This frame's update order (pedestrian check runs) and voice events (sound milestone).
    const std::vector<int>& updateOrder() const { return order_; }
    const std::vector<PedVoiceEvent>& voiceEvents() const { return voices_; }

    struct Stats {
        int placed = 0, emptied = 0, roadEnds = 0, crossings = 0, turnBacks = 0, dives = 0, wary = 0, backups = 0, lost = 0,
            stepArounds = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    // Population.
    void adjust(int oldRoom, int newRoom, int player);
    void emptyRoad(int road);
    void place(int ped, int road, int side);
    bool pedSide(int road, int side) const;
    bool isActive(int road) const;
    void pushFront(int ped, int road);
    void removeFromRoad(int ped, int road);
    // Sidewalk geometry.
    const Lane& sidewalk(int road, int side) const;
    float sidewalkLength(const Pedestrian& p, int a, int b) const;
    int segmentOf(const Pedestrian& p, float x) const;
    float heading(const Pedestrian& p, float x, int lambda, int d) const;
    void buildCurve(Pedestrian& p, int k, float offset);
    void targetAt(Pedestrian& p, float e);
    float locate(Pedestrian& p);
    void matrixFromHeading(Pedestrian& p) const;
    void turnToward(Pedestrian& p, const vec3& t) const;
    // One update and its behaviours.
    void updateOne(Pedestrian& p, float F, const std::vector<PedPlayer>& players);
    void chooseMode(Pedestrian& p, const std::vector<PedPlayer>& players);
    void walking(Pedestrian& p, const std::vector<PedPlayer>& players);
    void walkCore(Pedestrian& p, const std::vector<PedPlayer>& players);
    bool playerAhead(const Pedestrian& p, const std::vector<PedPlayer>& players, float& fwd) const;
    bool obstacleAhead(const Pedestrian& p, float& dist, int& object) const;
    void stepAround(Pedestrian& p, const vec3& Q, float r);
    void roadEnd(Pedestrian& p, float x);
    int choose(Pedestrian& p);
    void toKerb(Pedestrian& p, const std::vector<PedPlayer>& players);
    void waiting(Pedestrian& p, const std::vector<PedPlayer>& players);
    void crossing(Pedestrian& p, const std::vector<PedPlayer>& players);
    vec3 kerbPoint(int road, int side, bool endN) const;
    vec3 nearKerb(const Pedestrian& p) const;
    vec3 farKerb(const Pedestrian& p) const;
    void turnBack(Pedestrian& p);
    bool playerInTheWay(const Pedestrian& p, const std::vector<PedPlayer>& players) const;
    int previousIntersection(const Pedestrian& p) const;
    bool accidentAt(int intersection) const;
    void wary(Pedestrian& p, const std::vector<PedPlayer>& players);
    void dodging(Pedestrian& p, const std::vector<PedPlayer>& players, float& latFactor);
    void wallProbe(Pedestrian& p);
    void backUp(Pedestrian& p, bool sideDependent);
    void diveStart(Pedestrian& p, const PedPlayer& pl, bool crossingPiece);
    void diveControl(Pedestrian& p, const PedPlayer& pl, float& latFactor) const;
    // Animation.
    void startSeq(Pedestrian& p, int named);
    void queueSeq(Pedestrian& p, int named);
    bool isSeq(const Pedestrian& p, int named) const;
    void advance(Pedestrian& p, float F);
    const PedSequence* curSeq(const Pedestrian& p) const;
    void voice(Pedestrian& p, int reason);
    // The shared accumulator alone (acc = F × 30 + acc, step = floor(acc), acc − step).
    int tickClock(float F);
    void resetClock() {
        acc_ = 0.0f;
        lastStep_ = 0;
    }

    const CityData* city_ = nullptr;
    const RoadNetwork* net_ = nullptr;
    phys::CollisionWorld* world_ = nullptr;
    const TrafficSystem* traffic_ = nullptr;
    Lcg rng_;
    std::vector<std::unique_ptr<PedModel>> models_;
    std::vector<Pedestrian> peds_;
    std::vector<int> free_;          // back = front of MM2's list
    std::vector<int> head_;          // per road: first pedestrian of its list, −1 none
    std::vector<uint8_t> wanted_;    // per road: one bit per player
    std::vector<int> activeRoads_;   // front first
    std::vector<int> playerRoom_;    // one room memory per player: Find(P_c, memory), stored every frame
    std::vector<int> propOf_;        // city object → collision object (props), −1 none
    // Player bookkeeping of this frame: v̂ and inverse speed.
    std::vector<vec3> vhat_;
    std::vector<float> invSpeed_;
    float acc_ = 0.0f;  // the shared frame accumulator
    int lastStep_ = 0;
    int frame_ = 0;
    std::vector<int> order_;
    std::vector<PedVoiceEvent> voices_;
    Stats stats_;
};

}  // namespace ai
}  // namespace cc
