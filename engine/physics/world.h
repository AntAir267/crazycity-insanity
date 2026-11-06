// The physical world as the vehicle sees it. For now: the ground probe of
// spec/physics/collision.md ("Ground queries", Probe interface) and its flat-world form
// (vehicle-wheels.md, "Flat-world query"). Body-versus-world collision comes with the collision
// spec.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include "core/math.h"

namespace cc {
namespace phys {

struct SurfaceMaterial {
    float friction = 1.0f;
    float drag = 0.0f;
    float height = 0.0f;  // bump height
    float width = 1.0f;   // bump width
    float depth = 0.0f;
    float elasticity = 0.9f;
    // Visual only (engine/effects.md): the material's particle pair and whether it is "water".
    int ptxIndex[2] = {-1, -1};
    float ptxThreshold[2] = {0.25f, 0.5f};
    bool water = false;
    int sound = 0;  // the material's sound number (tyre-impact-sounds.md surface block); no physics use
};

// Per-caller state that persists between probes (one per wheel): rooms of the segment ends, the
// polygon cache, and the material/object the previous query left (collision.md, "The query").
struct ProbeState {
    int roomS = 0, roomE = 0;
    bool cacheValid = false;
    vec3 cache[4];
    int cacheN = 0;
    vec3 cacheNormal;
    vec3 cacheEdgeN[4];
    float lastFriction = 1.0f, lastDrag = 0.0f, lastHeight = 0.0f, lastWidth = 1.0f, lastDepth = 0.0f,
          lastElasticity = 0.5f;
    int lastSound = 0;
    bool lastHasMaterial = false;
    int lastObject = -1;
    int lastCode = -1, lastObjectId = -1;  // for the drive trace (drive-trace.md)
    // Visual only (engine/effects.md): the particle pair and water flag of the cached material.
    int lastPtxIndex[2] = {-1, -1};
    float lastPtxThreshold[2] = {0.25f, 0.5f};
    bool lastWater = false;
};

struct ProbeHit {
    vec3 point;
    vec3 normal;
    float t = 0;           // 0 at S, 1 at E
    float penetration = 0;
    bool front = false;
    SurfaceMaterial material;
    bool hasMaterial = false;  // false: the default material applies
    // For the drive trace (drive-trace.md, "Exact meanings"): material code = 1 + table position
    // (a prop's own material 0) and object id = position among the collidable placed objects in
    // load order (-1 = city polygon or flat ground).
    int materialCode = -1;
    int object = -1;
};

class Car;

class World {
public:
    virtual ~World() = default;
    // Probe(S, E, state, ignore = the car, mask = ground).
    virtual bool probe(const vec3& S, const vec3& E, ProbeState& state, ProbeHit& hit) = 0;
    virtual const char* name() const = 0;
    // Step 12 of the car's physics step: body against the world (nothing by default).
    // A race reset or world-limit reset: every placed object comes back untouched.
    virtual void resetPlacedObjects() {}
    // Start of a car's first step after a placement: the car's room from its reference point with
    // no hint (vehicle.md, "City check runs").
    virtual void placeCar(Car& car) { (void)car; }
    // End of step 11: the water effect, then the room update (vehicle.md, step 11).
    virtual void afterCarUpdate(Car& car, float dt) {
        (void)car;
        (void)dt;
    }
    virtual void collideCar(Car& car, float dt, float invdt) {
        (void)car;
        (void)dt;
        (void)invdt;
    }
    // damage.md, "Parts thrown off": part `part` of the car's list leaves with momentum P and
    // angular impulse H (loose-bodies.md 4); a world without loose bodies ignores it.
    virtual void throwCarPart(Car& car, int part, const vec3& P, const vec3& H) {
        (void)car;
        (void)part;
        (void)P;
        (void)H;
    }
    // A car's reset or "clear damage": its thrown parts are taken back (loose-bodies.md 7).
    virtual void takeBackCarParts(const Car& car) { (void)car; }
};

inline bool signBit(float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    return (u >> 31) != 0;
}

}  // namespace phys
}  // namespace cc
