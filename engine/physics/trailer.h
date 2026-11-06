// Trailers (spec/physics/trailer.md): the second rigid body some trucks tow, with its four wheels
// on single-wheel free trains, held to the truck by the hitch joint of section 5 (exact
// arithmetic, MM2's turn-correction bug included), detaching (force limit and the hidden Ctrl+B
// key), the jointed inverse-mass matrix of collision responses (6.2) and the trailer's state as
// the mover right after its truck (6.1). Set-up data: TrailerTune (car_tune.h).
#pragma once

#include <memory>

#include "physics/car.h"

namespace cc {
namespace phys {

class CollisionWorld;

class Trailer : public JointCoupling {
public:
    ~Trailer() override;
    // trailer.md 2.4 steps 3-8 (the truck is set up already). `world`: where it lives (rooms and
    // the bound material by name, 2.7); null = no rooms, the file's material. `computer`: an
    // opponent's or police truck's trailer (section 10): placed at every reset but never moved.
    // The truck's Car::attachTrailer then places it (section 3).
    void setup(const TrailerTune& tune, Car& truck, CollisionWorld* world, bool computer);
    // Section 3, from the truck's body (the car calls it after each of its resets).
    void resetBehind();
    // Section 4: one physics step (mover 2, right after the truck's update).
    void update(float dt, float invdt);
    // 6.1, end of the step: the push, then the previous matrix and the push source.
    void endSample();
    // 7.2: the B key went down this frame with Ctrl held (set before a frame's physics, cleared
    // after it); the first joint update of the frame takes it.
    void setDetachKey(bool down) { detachKey_ = down; }

    bool attached() const { return (status_ & 1) == 0; }
    bool computer() const { return computer_; }
    float lean() const { return lean_; }           // of the last joint update that ran
    const vec3& jointPoint() const { return p_; }

    // The collision side (car-collision.md 3, trailer.md 6).
    const RigidBody& body() const { return body_; }
    RigidBody& body() { return body_; }
    Mtx frame() const { return mtxFromRows(body_.R.r[0], body_.R.r[1], body_.R.r[2], body_.p); }
    const Bound* bound() const { return tune_.bound.get(); }
    const Material& material() const { return material_; }
    float radius() const { return tune_.radius; }
    int room() const { return room_; }
    const Mtx& prevMatrix() const { return prev_; }
    int prevSource() const { return prevSource_; }
    void notePush(const vec3& push, int fromIdentity);
    int identity = 0;

    const Wheel& wheel(int i) const { return wheels_[i]; }
    const TrailerTune& tune() const { return tune_; }
    const Car& truck() const { return *truck_; }
    Car& truck() { return *truck_; }

    // 6.2: the inverse-mass matrix of the truck or the trailer at a contact point P.
    void collisionInverseMass(const RigidBody& body, const vec3& P, float K[3][3]) const override;
    // Takes the coupling off both bodies (the trailer goes away).
    void unlink();

private:
    void trainUpdate(Train& tr, float dt, float invdt);
    void jointUpdate(float dt, float invdt);

    TrailerTune tune_;
    Car* truck_ = nullptr;
    CollisionWorld* world_ = nullptr;
    bool computer_ = false;
    RigidBody body_;
    Wheel wheels_[4];
    Train trains_[4];
    float trainKc_ = 0;     // the truck's Mass·0.005 (2.5)
    Mtx wheelFrame_;        // the frame the wheels hang from in this step
    Material material_;
    // Joint (5.1).
    vec3 offA_, offB_, off_;  // CarHitchOffset, TrailerHitchOffset, their difference o (2.4 step 3)
    vec3 p_;
    int status_ = 2;
    float lean_ = 0;
    bool detachKey_ = false;
    // Collision state.
    int room_ = 0;
    Mtx prev_;
    int prevSource_ = 0, largestSource_ = 0;
    float largestPush2_ = 0;
    bool fresh_ = true;     // not placed yet: the first reset sets the previous matrix
};

}  // namespace phys
}  // namespace cc
