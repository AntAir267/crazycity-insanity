// Several moving bodies in one physics sample (spec/physics/car-collision.md 1-4): the mover list,
// gathering, city / pair / object tests in order, traffic cars becoming physical, the end of the
// sample.
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "physics/box_contacts.h"
#include "physics/car.h"
#include "physics/collision_world.h"
#include "physics/traffic_body.h"

namespace cc {
namespace phys {

class Trailer;

struct SceneMover {
    enum Kind { CarMover, TrafficBodyMover, LaneMover, LooseMover, TrailerMover };
    Kind kind = CarMover;
    Trailer* trailer = nullptr;               // TrailerMover (trailer.md 6.1: right after its truck)
    int loose = -1;                           // LooseMover: its pool element (loose-bodies.md; -1 = let go)
    int looseSlot = -1;                       // LooseMover: the moved-prop slot of its object
    bool firstKind = true;                    // LooseMover: removed outside the player's rooms (5.1)
    Car* car = nullptr;                       // CarMover
    TrafficPhys* tp = nullptr;                // TrafficBodyMover
    const TrafficBodyType* ttype = nullptr;   // traffic (body or lane)
    int laneId = -1;                          // the caller's traffic car index
    Mtx laneM;                                // LaneMover: its lane matrix
    float laneSpeed = 0;
    int laneRoom = 0;
    bool U = true, C = true, V = true, O = true;
    int number = 0;                           // vehicle number (check runs, trace)
    bool player = false;
    bool box = false;                         // the bound is a box (computer car, traffic)
    BoxShape shape;
    int identity = 0;
    bool touchedPlayer = false;               // car-collision.md 5.4
    bool appendedThisSample = false;
};

// A traffic car without a body, gathered like a prop (2.3).
struct LaneCar {
    int id = 0;
    Mtx M;
    const TrafficBodyType* type = nullptr;
    float speed = 0;   // lane speed (m/s)
    int room = 0;
    int number = 0;    // vehicle number (trace)
    long long filed = 0;  // when it was last filed into its room (larger = newer: gathered first)
};

struct SceneHooks {
    // Lane traffic that movers may gather (cars without a body), in gathering order.
    std::function<void(std::vector<LaneCar>&)> laneCars;
    // Lane car `id` takes a body (4.2): set up and return its physics state, or null.
    std::function<TrafficPhys*(int id, const Mtx& lane, float speed)> makePhysical;
    // Every car-to-car impact in resolve order (drive-trace.md): numbers of the impact's sides.
    std::function<void(int a, int b, const Impact&)> onCarImpact;
    // Room of a point (city), or 0.
    std::function<int(const vec3& p, int hint)> findRoom;
    // Every impact applied to a physical traffic car (car-collision.md 3.4, "a traffic body's
    // report"; the impact sound and horn events): its traffic index, the impulse it took and the
    // other side's impact sound id. No physical effect.
    std::function<void(int laneId, const vec3& J, int otherSound)> trafficImpact;
};

class Scene {
public:
    // One physics sample over `movers` (list order = declaration order). New physical traffic cars
    // and new loose bodies are appended to `movers`.
    void runSample(std::vector<SceneMover>& movers, CollisionWorld& world, float dt, float invdt, SceneHooks& hooks);
    // The same without the cars' own update and end steps (they are done by Car::step around it).
    void collideOnly(std::vector<SceneMover>& movers, CollisionWorld& world, float dt, float invdt, SceneHooks& hooks);

private:
    void sample(std::vector<SceneMover>& movers, CollisionWorld& world, float dt, float invdt, SceneHooks& hooks,
                bool carsDone);
    void objectTest(std::vector<SceneMover>& movers, size_t i, int oi, float dt, SceneHooks& hooks);
    void resolveWithTrace(SceneMover& m, const std::vector<Impact>& impacts, bool moverIsA, int otherIdentity,
                          int traceKind, int traceObj);
    struct Side {
        RigidBody* body = nullptr;
        const Bound* bound = nullptr;
        Mtx M, M0;
        Material material;
        int identity = 0;
        int prevSource = 0;
        vec3 lastPush;
        float mass = 0;
        Car* car = nullptr;
        TrafficPhys* tp = nullptr;
        bool box = false;
        BoxShape shape;
        int number = 0;
        bool player = false;
        int loose = -1;   // a loose body's pool element
        const LooseData* data = nullptr;  // ... and its prop data (capsule tests, loose-bodies.md 6.3)
        int traceId = -1; // its slot object id (drive trace)
        Trailer* trailer = nullptr;
    };
    Side sideOf(SceneMover& m) const;
    void notePush(SceneMover& m, const vec3& push, int identity);
    void pairTest(SceneMover& a, SceneMover& b, float dt, SceneHooks& hooks, std::vector<SceneMover>& movers,
                  CollisionWorld& world, bool bIsLane, const LaneCar* lane);
    vec3 centreOf(const SceneMover& m) const;
    float radiusOf(const SceneMover& m) const;
    int roomOf(const SceneMover& m) const;
    // The impact sound id of a side (tyre-impact-sounds.md): a loose body's prop data ColliderId,
    // else 0.
    int soundIdOf(const Side& s) const;
    CollisionWorld* world_ = nullptr;
    SceneHooks* hooks_ = nullptr;  // of the current sample (the traffic impact report)
};

}  // namespace phys
}  // namespace cc
