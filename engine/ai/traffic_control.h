// Traffic lights, stop signs and the "may a car enter the intersection" rule
// (spec/ai/traffic-control.md).
#pragma once

#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;

namespace ai {

class RoadNetwork;

// Light states (traffic-control.md, "Light states").
enum LightState { LightRed = 1, LightYellow = 2, LightGreen = 3, LightWalk = 4, LightWalkEnding = 5 };

struct TrafficLight {
    int road = 0;
    int end = 0;           // the approach: end 0 (side B arrives) or end 1 (side A arrives)
    int arrivingSide = 1;  // +1 or −1
    int pole = -1;         // CityData object index of its pole, −1 if it could not be made
    std::string model;
    mat4 placement = mat4::identity();  // model origin at the sign position
    vec3 signPos;
    int state = LightYellow;
};

struct LightSet {
    int intersection = 0;
    int kind = 0;  // 0 cycle only, 1 all approaches (walk phase), 2 paired
    int sources = 0, blockedExits = 0;
    std::vector<TrafficLight> lights;
    int current = 0;
    bool walk = false;
    float timer = 0;
};

// "Cars waiting on (road, side) are woken" — collected for the traffic AI.
struct WakeEvent {
    int intersection = 0;
    int road = 0;
    int side = 0;
    int car = -1;  // a free approach: the walk starts at this car
};

class TrafficControl {
public:
    static constexpr float kCycle = 6.0f;

    // Load step 5 (bai.md): one set per intersection with a light approach, poles placed as street
    // props of `city`, turn rules of kind 2 sets. `single`/`dual` are the aimap's models.
    void setup(RoadNetwork& net, CityData& city, const Vfs& vfs, AssetCache& assets, const std::string& single,
               const std::string& dual);

    // Session start and every AI reset: the cycle restarts and the stop-sign queues empty.
    void reset();
    // Once per frame after all AI vehicles (dt = the frame time).
    void update(float dt);

    const std::vector<LightSet>& sets() const { return sets_; }
    // State of the light controlling traffic on side s of `road` at its exit intersection; 0 if
    // the approach has no light.
    int approachState(const RoadNetwork& net, int road, int s) const;

    // The signal-and-sign answer (traffic-control.md, "May a car enter the intersection?") for a
    // car on side s of `road` arriving at its exit intersection. `remaining` = distance to its stop
    // point. Ambient cars and cable cars ask; `ambient` false for a cable car.
    bool mayEnter(RoadNetwork& net, int road, int s, int car, bool ambient, float speed, float remaining);
    // The car finished its path through `intersection` from a stop approach.
    void leftIntersection(int intersection, int car);
    // A car removed from the world.
    void removeCar(int car);

    std::vector<WakeEvent>& wakes() { return wakes_; }

    struct QueuedCar {
        int car = 0;
        int road = 0;
        bool ambient = true;
    };
    struct StopQueue {
        std::vector<QueuedCar> waiting, go;
    };
    const StopQueue& stopQueue(int intersection) const { return queues_[size_t(intersection)]; }

    // Counts for the native test.
    struct Stats {
        int kinds[3] = {};
        int lights = 0;
        int poles = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    void setLight(LightSet& set, int i, int state);
    void wake(LightSet& set, const RoadNetwork& net, int slot, int sideSlot);

    std::vector<LightSet> sets_;
    std::vector<StopQueue> queues_;
    std::vector<WakeEvent> wakes_;
    RoadNetwork* net_ = nullptr;
    Stats stats_;
};

}  // namespace ai
}  // namespace cc
