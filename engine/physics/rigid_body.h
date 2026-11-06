// The car's rigid body (spec/physics/vehicle-body.md).
#pragma once

#include "physics/pmath.h"

namespace cc {
namespace phys {

class RigidBody;

// A body joined to another (trailer.md 6.2): while it is set, every collision response takes the
// body's inverse-mass matrix at a contact point from the coupling instead of the body itself.
class JointCoupling {
public:
    virtual ~JointCoupling() = default;
    virtual void collisionInverseMass(const RigidBody& body, const vec3& P, float K[3][3]) const = 0;
};

class RigidBody {
public:
    void reset();
    void setMass(float m, const vec3& inertia);  // inertia = principal moments (Ix, Iy, Iz)
    void setAngularLimit(float wmax) { wmax_ = wmax; }

    // Step 4 of vehicle.md: post forces in, integrate, accumulators cleared.
    void integrate(float h);
    // The same step for a frozen body (rest detector state != 1): only the motion (step 2) is
    // skipped; steps 1 and 3 still run, so lastPush = pushSum and pushSum = 0
    // (loose-bodies.md 6.1 step 3, car-collision.md 6.3 step 3).
    void integrateFrozen();
    // Step 13: apply the push collected during the step.
    void applyPush();

    void addForce(const vec3& f) { F = vadd(F, f); }
    void addTorque(const vec3& t) { T = vadd(T, t); }
    void addForceAt(const vec3& f, const vec3& x);
    void addImpulseAt(const vec3& j, const vec3& x);
    void addImplicitContact(const vec3& x, const vec3& f, const Mat3& M);
    void addPush(const vec3& d);

    vec3 velocityAt(const vec3& x, float invdt) const;
    // Inverse-mass matrix K at world point P (collision.md, "Quantities"; row-vector convention).
    void inverseMassMatrix(const vec3& P, float K[3][3]) const;
    // n·K·n with collision.md's K (vehicle-body.md, "Inverse mass along a direction").
    float invMassAlong(const vec3& x, const vec3& n) const;
    // The K a collision response uses: the coupling's while one is set (trailer.md 6.2), else the
    // body's own. (Wheel bottoming uses the body's own, invMassAlong.)
    void collisionInverseMass(const vec3& P, float K[3][3]) const {
        if (coupling) coupling->collisionInverseMass(*this, P, K);
        else inverseMassMatrix(P, K);
    }
    // Not part of the state (reset() keeps it): the joint this body belongs to, if any.
    const JointCoupling* coupling = nullptr;

    // State (public for the car and the check output).
    float m = 0, invM = 0;
    vec3 I, invI;
    M3 R = M3::identity();
    vec3 p, v, w, P, L;
    float vmax = 500.0f;
    vec3 F, T, Fpost, Tpost, G, H;
    Mat3 A, B, C;
    bool implicit = false;
    vec3 push, pushSum, lastPush, turn;

private:
    float wmax_ = 12.566371f;
};

// vehicle-body.md "inverse" (cofactors; a matrix with det == 0 is left unchanged).
Mat3 inverseMat3(const Mat3& M);
// Matrix product A·B in MM2's term orders (vehicle-body.md, "Products").
Mat3 mulProducts(const Mat3& A, const Mat3& B);
// Rot(axis, a) of vehicle-body.md (c and s in double, s rounded, c not).
Mat3 rotAboutAxis(const vec3& axis, float a);
// Solve x·K = b (collision.md, "Solving x·K = b"); also the implicit integration's solve.
vec3 solveRowSystem(const float K[3][3], const vec3& b);

}  // namespace phys
}  // namespace cc
