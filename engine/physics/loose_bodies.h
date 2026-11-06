// Loose bodies (spec/physics/loose-bodies.md): props knocked loose, their pieces and car parts
// thrown off, as rigid bodies kept in two fixed pools (32 bodies, 40 moved-prop slots). The pools
// live in the CollisionWorld (it owns the objects and the room lists); the Scene runs them as
// movers (car-collision.md 1.1, row 7).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "formats/banger.h"
#include "formats/pkg.h"
#include "physics/bound.h"
#include "physics/contacts.h"
#include "physics/rigid_body.h"
#include "physics/traffic_body.h"

namespace cc {
namespace phys {

class Car;

// What a moved prop or a body is made from (2.2, 3, 4): its prop data, bound, material, model
// radius and model.
struct LooseData {
    std::string name;               // prop data name: <prop>, <prop>_BREAKnn or <car>_<PART>
    BangerData bd;
    std::shared_ptr<Bound> bound;   // in coordinates relative to the body position
    bool box = true;                // a box bound (box method against another box)
    vec3 boxSize, boxOffset;
    Material material;              // Friction, Elasticity of the prop data
    float radius = 0;               // model radius (gathering and the broad test, 5.4)
    int prim = 0;                   // 2 capsule, 3 sphere (`bound` is then the box of 6.2), else 0
    // Drawing (9).
    std::shared_ptr<const Pkg> pkg;
    const Mesh* lod[4] = {nullptr, nullptr, nullptr, nullptr};
};

// The prop data's mass and inertia, the /40 rule and the limits of 2.2 steps 3-4.
void setupLooseMass(RigidBody& rb, const BangerData& bd);
// A box or geometry bound for prop data `bd` (collision-objects.md, "Props"): a geometry bound
// moved by −CG if CollisionPrim is 0 and `geometry` is given, else the box of Size; a capsule or
// sphere becomes the box of its bounding box (6.2).
void looseBoundFor(LooseData& d, const std::shared_ptr<Bound>& geometry);

// One element of the body pool.
struct LooseBody {
    RigidBody rb;
    Mtx prev;                       // body frame at the end of its last sample (after the push)
    int prevLargestSource = 0, largestSource = 0;
    float largestPush2 = 0;
    RestDetector rest;
    int identity = 0;               // belongs to the pool element (2.2)
    bool inUse = false;
    int slot = -1;                  // the moved-prop slot whose object it moves
    int prop = -1;                  // or the untouched prop (object index) it was handed for a test
    const LooseData* data = nullptr;
    Mtx matrix() const { return mtxFromRows(rb.R.r[0], rb.R.r[1], rb.R.r[2], rb.p); }
    void notePush(const vec3& push, int fromIdentity);
};

// One moved-prop slot.
struct MovedSlot {
    enum Kind { Copy, Piece, CarPart };
    bool held = false;              // holds an object since the last reset
    int room = 0;                   // its room (0: in no room's list)
    int body = -1;                  // pool element moving it (-1: none)
    bool frozen = false;            // its body was dropped without letting go (5.1)
    Mtx M;                          // the object's matrix (6.1 step 4)
    const LooseData* data = nullptr;
    std::shared_ptr<const LooseData> hold;  // keeps `data` (and its model) alive
    int set = 0;                    // paint set
    Kind kind = Copy;
    const Car* car = nullptr;       // CarPart: the car (only compared) and its part index
    int part = -1;
    int fromObject = -1;            // Copy, Piece: the broken prop's object index
    long long filed = 0;            // filing stamp of its place in its room's list
    bool collidable() const { return held && body < 0 && !frozen && room > 0; }
};

// Records for the drive trace and the log (drive-trace.md).
struct LooseEvent {
    enum Kind { Eject, Break, Gone, Rest };
    Kind kind = Eject;
    std::string part;               // Eject: the part name
    int id = 0;                     // Eject, Gone, Rest: slot object id; Break: the prop's object id
    vec3 P, H;                      // Eject: momentum and angular impulse
    int n = 0;                      // Break: NumParts
    std::vector<int> ids;           // Break: the slots it became
};

// A glass pane's shard burst (8): where the shards start and the height they fall to.
struct PaneBurst {
    vec3 at;
    float floorY = 0;
    int cityObject = -1;
};

}  // namespace phys
}  // namespace cc
