// The physical body of a traffic car that was hit (spec/physics/car-collision.md sections 4 and 6):
// a rigid body at the model origin with a box bound at CG, four sliding spring wheels, the rest
// detector and the release test.
#pragma once

#include <memory>

#include "physics/bound.h"
#include "physics/contacts.h"
#include "physics/rigid_body.h"
#include "physics/world.h"

namespace cc {
namespace phys {

// One traffic type's physics (aiVehicleData, 6.1).
struct TrafficBodyType {
    float mass = 0;
    vec3 size, cg;
    float spring = 0, damping = 0, limit = 0, rubberSpring = 0, rubberDamp = 0;
    vec3 wheel[4];
    float wheelRadius = 0;
    float bodyRadius = 0;            // BODY part radius (broad tests)
    std::shared_ptr<Bound> box;      // size Size, offset CG (3.1)
    Material material;               // friction 1.0, elasticity 0.5 (3.4)
};

// A box bound of size S with centre offset c (car-collision.md 3.1): radius about the origin.
std::shared_ptr<Bound> makeOffsetBox(const vec3& size, const vec3& offset);
void setupTrafficBodyType(TrafficBodyType& t);

// The rest detector (6.4).
struct RestDetector {
    int state = 1;  // 1 awake, 0 asleep, 2 long asleep
    int still = 0, still2 = 0;
    vec3 Wsum, Usum;
    void reset() {
        state = 1;
        still = still2 = 0;
        Wsum = Usum = vec3();
    }
};

// One sample of the rest detector (6.4) with the thresholds thrV and thrW (squared speeds; traffic
// cars 0.01 and 0.01, loose bodies 0.1 and 0.5, loose-bodies.md 2.2).
void restDetect(RestDetector& rest, RigidBody& rb, float invdt, float thrV, float thrW);

struct TrafficPhys {
    RigidBody rb;
    Mtx prev;                    // previous matrix (after the push, 6.3 step 6)
    int prevLargestSource = 0;   // push source of the previous sample
    int largestSource = 0;
    float largestPush2 = 0;
    float comp[4] = {}, dLat[4] = {}, dLong[4] = {};
    ProbeState probe[4];
    RestDetector rest;
    int room = 0;
    int identity = 0;
    bool wrecked = false;        // left wrecked before (6.5)
    // The lane matrix (6.3 step 5): the body frame copied in every sample, frozen or not, before
    // that sample's end-of-sample push; what is drawn and what the traffic logic sees. The release
    // (6.5) keeps it and does not copy the body frame again.
    Mtx lane;

    // 4.2 steps 3-6: the body from the lane matrix and the lane speed.
    void setup(const TrafficBodyType& t, const Mtx& lane, float laneSpeed);
    // 6.3 steps 1-5 (the room update of step 5 is the caller's).
    void update(const TrafficBodyType& t, float dt, float invdt, World& world, float grip = 1.0f);
    // 6.3 step 6.
    void endSample();
    Mtx matrix() const;
    void notePush(const vec3& push, int fromIdentity);
};

// 6.5 step 2 (a)-(b): the release probe; true if the car may go back to its lane (not counting the
// wreck mark).
bool trafficUpright(World& world, const Mtx& body, int room);

}  // namespace phys
}  // namespace cc
