// Body contacts and collision response: spec/physics/collision.md ("Collision response"),
// collision-contacts.md (the car against city polygons) and collision-object-contacts.md (the car
// against placed objects). Single precision, operations in the spec's order.
#pragma once

#include <functional>
#include <vector>

#include "core/math.h"
#include "physics/material.h"
#include "physics/rigid_body.h"

namespace cc {
namespace phys {

struct Bound;
struct CollPoly;

// A rigid placement: rows X, Y, Z and origin (row-vector convention, p' = p.x X + p.y Y + p.z Z + o).
struct Mtx {
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1}, o;
};

struct Impact {
    vec3 P;      // contact point
    vec3 n;      // unit normal, out of B into A
    float depth = 0;
    Material matA, matB;
};

// A polygon as the contact code sees it (city polygon, bound polygon in some frame, or the flat
// world's endless plane).
struct PolyView {
    vec3 c[4];
    int n = 0;          // 0 corners = the endless plane of the flat world (every inside test passes)
    vec3 normal;
    vec3 edgeN[4];
    int material = 0;   // city: material code; bound: material index
    int edgeNum[4] = {0, 0, 0, 0};
};

// Response for one impact list (collision.md): `a`/`b` may be null for a side without a body.
// `onPush(side, push)` is told every push given to a body (side 0 = A, 1 = B) for the
// largest-push bookkeeping. Pushes and impulses go to the bodies' accumulators.
void resolveImpacts(const std::vector<Impact>& impacts, RigidBody* a, RigidBody* b,
                    const std::function<void(int, const vec3&)>& onPush);
// The same, telling each side the impulse it received and its push (side 0 = A, 1 = B), in the
// order of car-collision.md 3.4 (A then B for each impact), with the impact itself.
using ImpactApplied = std::function<void(int side, const vec3& J, const vec3& push, const Impact& im)>;
void resolveImpactsEx(const std::vector<Impact>& impacts, RigidBody* a, RigidBody* b, const ImpactApplied& onApplied);
// One impact with weight w (for impact lists whose sides differ per impact, car-collision.md 3.2).
void resolveOneImpact(const Impact& im, RigidBody* a, RigidBody* b, float w, const ImpactApplied& onApplied);

// Breakable props (collision.md, "Breakable props"): A = the mover, B = the prop's test body
// (loose-bodies.md 2). `onApplied(side, impact, J, push)` after each side's impulse and push.
// Returns true if any impact broke the prop.
bool resolvePropImpacts(const std::vector<Impact>& impacts, RigidBody* a, RigidBody* prop, float impulseLimit2,
                        const std::function<void(int, const Impact&, const vec3&, const vec3&)>& onApplied);

// The car's bound against city polygons (collision-contacts.md). `M0` already carries the
// lastPush shift if it applies. `firstMaterial` receives the material code of the first
// intersection. Returns the impacts (A = the city, B = the car).
void cityContacts(const Bound& car, const Mtx& M, const Mtx& M0, float dt, const std::vector<PolyView>& polys,
                  const Material& carMaterial, const MaterialTable& table, std::vector<Impact>& out);

// Placed objects (collision-object-contacts.md). O = the object's collision matrix. A = the car,
// B = the object. `objMaterial` = the object's material (first bound material / prop material).
void polygonalObjectContacts(const Bound& car, const Mtx& M, const Mtx& M0, const Bound& obj, const Mtx& O,
                             float dt, const Material& carMaterial, const Material& objMaterial,
                             std::vector<Impact>& out, const Mtx* O0 = nullptr);
// O0: the object's previous matrix when it moves too (car-collision.md 3.2, polygonal pairs); null
// = a fixed object (bit for bit the same as O0 = O).
void terrainObjectContacts(const Bound& car, const Mtx& M, const Mtx& M0, const Bound& obj, const Mtx& O,
                           float dt, const Material& carMaterial, const MaterialTable& table,
                           std::vector<Impact>& out);
// Capsule props: radius r, cylinder length L, matrix H (centre = prop CG, axis = H.Y). The impacts
// have A = the polygon side `car` (matrix M, previous M0), B = the capsule, normals out of the
// capsule into A. H0: the capsule's previous matrix when it moves (loose-bodies.md 6.3, "Capsules
// in these tests": 5.2 step 5 takes w and l with H0); null = a fixed capsule (H0 = H).
void capsuleObjectContacts(const Bound& car, const Mtx& M, const Mtx& M0, float r, float L, const Mtx& H,
                           const Material& carMaterial, const Material& propMaterial, std::vector<Impact>& out,
                           const Mtx* H0 = nullptr);

// The cells of a segment in a terrain grid (collision-objects.md), non-empty cells in walk order.
void terrainCellsOfSegment(const Bound& b, const vec3& S, const vec3& E, std::vector<int>& cells);

// Helpers shared with the probe code.
vec3 mtxPoint(const Mtx& m, const vec3& p);   // canonical order
vec3 mtxDir(const Mtx& m, const vec3& d);
Mtx mtxFromRows(const vec3& X, const vec3& Y, const vec3& Z, const vec3& o);

}  // namespace phys
}  // namespace cc
