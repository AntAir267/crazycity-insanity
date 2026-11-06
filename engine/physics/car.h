// MM2 car physics (spec/physics/vehicle*.md): rigid body, engine, gearbox, drive trains, wheels
// with suspension and tyres, aerodynamics, anti-roll bars, stability helpers, unstuck helper,
// and the per-frame player control step. All float32, in the spec's order of operations.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/rigid_body.h"
#include "physics/world.h"

namespace cc {
namespace phys {

// The car's inputs (after the control step).
struct CarInputs {
    float throttle = 0, brake = 0, steer = 0, handbrake = 0;
};

// The player's controls before the control step (vehicle-helpers.md, "Player controls"): the
// pedals and steering as the keyboard/gamepad smoothing left them.
struct DriverControls {
    float accelerate = 0, brake = 0, steer = 0, handbrake = 0;
    int shift = 0;       // manual gearbox: +1 up, -1 down this frame
    int setGear = -100;  // check runs: set this gear index directly (-100 = none)
};

struct Wheel {
    int index = 0;
    WheelTune tune;
    vec3 pivot;
    float radius = 0, width = 0;
    // constants (vehicle-wheels.md, "Static load and derived constants")
    float L0 = 0, kk = 0, k = 0, prog = 0, damp = 0, mL = 0;
    float Klong = 0, Dlong = 0, Klat = 0, Dlat = 0;
    float brakeMax = 0, handbrakeMax = 0, invOpt2 = 0;
    // state
    float comp = 0, rate = 0, Fsusp = 0, Fz = 0, Ks = 0;
    bool bottomed = false;
    float steerAngle = 0, brakeTorque = 0;
    bool grounded = false;
    vec3 x, N, Lat, Back;
    float vLat = 0, vFwd = 0, vN = 0;
    float matFric = 1, matDrag = 0, depth = 0, bump = 0, phi = 0;
    float dL = 0, dT = 0, FL = 0, FT = 0, roadTorque = 0;
    float latSlip = 0, longSlip = 0, slide = 0;
    bool slipping = false;
    float omega = 0, rotAngle = 0;
    // Visual only (engine/effects.md): the longitudinal slip speed sV of the last tyre step and
    // the wheel's world X axis (axle direction with steering) of the last contact step.
    float sV = 0;
    vec3 axle;
    ProbeState probe;
    SurfaceMaterial material;  // the last hit's material
    // Drive trace (drive-trace.md): the probe result and state after the probe and suspension.
    struct Trace {
        bool hit = false;
        float T = 0;
        vec3 n, p;
        int mat = -1, obj = -1;
        float comp = 0, spin = 0;
    } trace;
    float visualDisp() const;
};

struct Train {
    std::vector<int> wheels;
    TrainTune tune;
    float wt = 0;
    float diff = 1;
    bool drive = false;
};

// Receives the impact reports of a car that reach the damage count step (the impact sound event,
// tyre-impact-sounds.md "When a car plays one"): the impulse applied to the car and the other
// side's impact sound id. No effect on the physics.
struct ImpactSink {
    virtual ~ImpactSink() = default;
    virtual void carImpact(const vec3& J, int soundId) = 0;
};
// What a wheel hangs from and pushes on (vehicle-wheels.md): the car's model frame of step 5 and
// its body, or a trailer's body frame and body (trailer.md 2.5).
struct WheelHost {
    RigidBody* body = nullptr;
    const Mtx* frame = nullptr;
    float weatherScale = 1.0f;
    bool frictionHandling = true;      // "Surface": the CarFrictionHandling step (not for trailers)
    float carFrictionHandling = 1.0f;
    bool trace = false;                // fill Wheel::trace (drive-trace.md)
};
// vehicle-wheels.md, "Static load and derived constants": everything that follows from w.L0
// (the SuspensionFactor floor included, stored back into the tune value).
void wheelLoadConstants(Wheel& w);
// vehicle-wheels.md, "Steering and brake inputs": set(s, b, h).
void wheelSetInputs(Wheel& w, float s, float b, float h);
// vehicle-wheels.md, "Wheel state": every moving value back to rest (pivot, tune and constants kept).
void resetWheelState(Wheel& w);
// The wheel's two calls (vehicle-wheels.md, "Contact" and "Tyre").
void wheelContactCall(Wheel& w, const WheelHost& host, float dt, float invdt, World& world);
void wheelTyreCall(Wheel& w, const WheelHost& host, float dt, float invdt);

// Restarts the shared bump random sequence (check runs call it before each run).
void resetBumpRandom(uint32_t seed = 1);

class Car;

// Visual effects attached to a car (formats/damage-visuals.md, engine/effects.md): told about
// every physics sample and every counted impact. Drawing only: an implementation must never change
// the car or draw from the physics random generator. Check runs attach none.
class CarEffectsHook {
public:
    virtual ~CarEffectsHook() = default;
    // Each physics sample, at the end of the car's own update (after the damage regeneration and
    // slot timers): skid marks, wheel particles and the visual part of the damage step.
    virtual void sampleEffects(const Car& car, float dt) = 0;
    // A counted impact (damage.md, "Per impact report" step 5) with strength s on `slot` (its
    // stored contact point and normal, its current total). dt = this sample's length.
    virtual void countedImpact(const Car& car, const DamageState::Slot& slot, float s, float dt) = 0;
};
class Trailer;

class Car {
public:
    Car();
    ~Car();
    Car(const Car&) = delete;
    Car& operator=(const Car&) = delete;
    bool setup(const CarTune& tune);
    // Drive trace (spec/engine/drive-trace.md): records of each following step go to `f` (null
    // = off); traceStep() gives the number and time of the next step.
    void setTrace(std::FILE* f) { trace_ = f; }
    void traceStep(int k, float t) {
        traceK_ = k;
        traceT_ = t;
    }
    const CarTune& tune() const { return tune_; }
    // Race modes change damage limits (crash-course.md: no contact, destroy the car).
    DamageTune& damageTune() { return tune_.damage; }
    const DamageTune& damageTune() const { return tune_.damage; }
    // "Go" (races.md): the automatic-reverse state is cleared.
    void clearAutoReverse() { reverseMode_ = false; }

    // Check-run starting state on the flat world (vehicle.md, "Starting state").
    void resetFlat(float x, float groundY, float z, float headingDegrees);
    // MM2's placement at a start point (vehicle.md, "Placing the car in a city").
    void placeAt(const vec3& q, float headingRadians);
    // The same with cos/sin of heading·π/180 in double precision rounded to float (check runs).
    void placeAtDegrees(const vec3& q, float headingDegrees);
    // The reset key / automatic righting (vehicle-helpers.md, "Resetting the car").
    void resetOnWheels();
    // A network car's "vehicle reset" at a full body matrix (multiplayer.md 3.4, "Reset"): damage
    // cleared, the moving state back to rest, then the body rotation rows and position (centre of
    // mass) set as given.
    void resetToBodyMatrix(const M3& R, const vec3& bodyPos);

    // Once per frame before the physics (vehicle-helpers.md, "Player controls").
    void controlStep(const DriverControls& c);
    // One physics step (vehicle.md, "One physics step"): stepUpdate (steps 1-11 and the damage
    // bookkeeping), the world collision, stepEnd (step 13).
    void step(float dt, float invdt, World& world);
    void stepUpdate(float dt, float invdt, World& world);
    void stepEnd();
    // Computer cars (car-collision.md 5.4): the inputs set directly, gears through "set gear".
    void aiControlStep(const CarInputs& in, int setGear = -100);
    // Computer cars' unstuck values (car-collision.md 5.3): Rotation 0, TimeThresh, PosThresh 1.
    void setAiStuck(float timeThresh);
    void setEngineScale(float s) { engineScale_ = s; }
    void setCarFrictionHandling(float v) { tune_.sim.carFrictionHandling = v; }
    // The AI driver's access to the unstuck helper and gearbox (driver.md).
    enum UnstuckState { UnstuckInactive = 0, UnstuckWatch = 1, UnstuckLift = 2, UnstuckRoll = 3, UnstuckTurn = 4 };
    int unstuckState() const { return int(stuck_); }
    void deactivateUnstuck() {
        stuck_ = Stuck::Inactive;
        stuckTimer_ = 0.0f;
    }
    void setUnstuckInactiveOnly() { stuck_ = Stuck::Inactive; }
    void selectGear(int g) { setGear(g); }          // "set gear" with its bookkeeping
    void writeGearDirect(int g) { gear_ = g; }       // a direct write (no timer, no changed flag)
    // Not drivable (damage.md, free roam): brake 1 and neutral each frame, the unstuck helper and
    // the water effect paused.
    void setDrivable(bool d) { drivable_ = d; }
    // vehicle-wheels.md "Surface": 0.8 in rain (0.75 at night), else 1; set by each game.
    void setWeatherScale(float s) { weatherScale_ = s; }
    bool drivable() const { return drivable_; }
    // Player override (controls.md, "Player overrides" 1): a disabled player (after a race ends)
    // gets throttle 0, brake 1, steering −1 in every control step.
    void setPlayerDisabled(bool d) { playerDisabled_ = d; }
    bool playerDisabled() const { return playerDisabled_; }
    void holdNotDrivable() {
        in_.brake = 1.0f;
        gear_ = 1;
    }

    // Damage (damage.md). `P` = the impact's world contact point, `n` the impact record's unit
    // normal (out of side B into side A, whichever side the car is; visual only); `otherSound` the
    // other side's impact sound id (0 for the city, fixed objects and cars; a prop's ColliderId).
    void damageImpact(const vec3& J, int otherIdentity, float otherMass, const vec3& P, const vec3& n,
                      int otherSound = 0);
    void setImpactSink(ImpactSink* s) { impactSink_ = s; }
    // A reset or "clear damage": damage 0, every slot empty, the thrown parts attached again (and
    // taken back from the world), the wreck throw re-armed.
    void clearDamage();
    // Parts thrown off (damage.md): the car's paint set picks its VARIANT part.
    void setPaintSet(int set);
    int paintSet() const { return paintSet_; }
    bool partThrown(const std::string& name) const;
    const std::vector<bool>& thrownParts() const { return thrown_; }
    // Visual effects (null = none); the hook must outlive its use by the car.
    void setEffects(CarEffectsHook* h) { effects_ = h; }
    CarEffectsHook* effects() const { return effects_; }
    // Counts the car resets (placement, reset key, righting): skid marks and wheel particle
    // counters restart at a car reset (effects.md).
    unsigned resetCount() const { return resetCount_; }
    const DamageState& damage() const { return damage_; }
    DamageState& damage() { return damage_; }
    bool wrecked() const { return damage_.enabled && damage_.wrecked(tune_.damage); }
    float damageLimit() const { return tune_.damage.maxDamage; }
    float mass() const { return body_.m; }

    // The collision side reports an impact (activates the unstuck helper).
    void notifyImpact();

    // ---- Network games (game/multiplayer.md). All off outside a network game.
    // 1.5 "Throttle cap": in a forward gear the quantised throttle t becomes min(t, cap) for
    // t >= 0 and 0 for t < 0. `slowDown` > 0 (5.2 "slow-down": seconds left, Blitz's results
    // timer) first takes slowDown x 0.2 off the throttle and sets the brake to 1. `stopped` (5.2:
    // the car stopped after the finish) also gives steering 0 and brake 1.
    void setNetworkControls(bool on, float cap = 1.0f, float slowDown = 0.0f, bool stopped = false) {
        netControls_ = on;
        netCap_ = cap;
        netSlowDown_ = slowDown;
        netStopped_ = stopped;
    }
    // 5.3 "Carrying": the body's mass raised by `extra` kg (the gold): m := m + (extra − the
    // extra before) and 1/m recomputed; the inertia, momenta and velocities stay as they are.
    void setExtraMass(float extra);
    float extraMass() const { return extraMass_; }
    // Told about every counted impact (damage.md "Per impact report" step 5) with the other
    // side's identity and the slot's total strength (5.3 "Knocked loose"). Null = none.
    struct CountedImpactHook {
        virtual ~CountedImpactHook() = default;
        virtual void countedImpact(const Car& car, int otherIdentity, float slotTotal) = 0;
    };
    void setCountedImpactHook(CountedImpactHook* h) { countedHook_ = h; }
    // A second listener for the player's force-feedback impact jolts (controls.md, "Impact
    // jolts"); told after the first. No effect on the physics.
    void setFeedbackImpactHook(CountedImpactHook* h) { feedbackHook_ = h; }

    // Gearbox control (controls.md keys).
    void setAutomatic(bool a);
    bool automatic() const { return automatic_; }
    void toggleReverse();   // R key
    bool reverseMode() const { return reverseMode_; }
    void setAutoReverse(bool on) { autoReverse_ = on; }

    // State for output, drawing and cameras.
    vec3 modelOrigin() const;             // body position + R*CoG
    const M3& rotation() const { return body_.R; }
    vec3 forward() const { return vscale(body_.R.r[2], -1.0f); }
    vec3 up() const { return body_.R.r[1]; }
    const vec3& velocity() const { return body_.v; }
    const vec3& angularMomentum() const { return body_.L; }
    float signedSpeed() const { return pdot(body_.v, forward()); }
    float speed() const { return speed_; }        // |Z . v| from the last step
    float speedMph() const { return speedMph_; }
    float rpm() const { return eng_.rpm; }
    int gearIndex() const { return gear_; }      // 0 R, 1 N, 2 = 1st, ...
    int gearCount() const { return automatic_ ? tune_.sim.autoNumGears : tune_.sim.manualNumGears; }
    const CarInputs& inputs() const { return in_; }
    const Wheel& wheel(int i) const { return wheels_[i]; }
    int groundedWheels() const;
    float idleRpm() const { return tune_.sim.idleRPM; }
    float maxRpm() const { return tune_.sim.maxRPM; }
    float driveTrainSpeed() const { return trains_.empty() ? 0.0f : trains_.back().wt; }
    float time() const { return time_; }
    int steps() const { return steps_; }
    const RigidBody& body() const { return body_; }
    RigidBody& body() { return body_; }

    // Collision side (collision.md): the body matrix of this sample (rows R, model origin), the
    // bound and its material, the model radius, and the state kept between samples.
    Mtx bodyMatrix() const;
    const Bound* bound() const { return tune_.bound.get(); }
    const Material& boundMaterial() const { return boundMaterial_; }
    float modelRadius() const { return tune_.modelRadius; }
    CarCollisionState& collision() { return col_; }
    const CarCollisionState& collision() const { return col_; }
    // A push received from a world side with the given identity (largest-push bookkeeping).
    void notePush(const vec3& push, int identity);
    // Water effect (collision.md, "World limits"), applied after the car's own update.
    void waterForces(float dt);
    // Per rendered frame: true when the game would reset the run (fell below -50, or 5 s in water).
    bool worldLimitReset(float frameSeconds);

    // The trailer this car tows (trailer.md), or null. Attaching places it behind the car
    // (section 3); every later reset of the car (placement, world-limit reset, reset key) puts it
    // back and attaches it again.
    void attachTrailer(std::unique_ptr<Trailer> t);
    Trailer* trailer() { return trailer_.get(); }
    const Trailer* trailer() const { return trailer_.get(); }
    // The speed-sensitive steering factor of this step's wheel inputs (vehicle-wheels.md).
    float sss() const { return sss_; }
    float weatherScale() const { return weatherScale_; }
    // trailer.md 2.6: "add x" to wheel i's static load (at least 1), its constants recomputed.
    void addWheelLoad(int i, float x);

private:
    void traceStart() const;
    void traceAfterWheels() const;
    void traceEnd() const;
    std::FILE* trace_ = nullptr;
    int traceK_ = 0;
    float traceT_ = 0;

    struct Engine {
        float wMax = 0, wOpt = 0, wIdle = 0, gap = 0, invGap = 0, K = 0;
        float we = 0, rpm = 0, torque = 0, throttle = 0, gclTimer = 0, shiftRpm = 0, power = 0;
        bool waiting = true;
        bool coupled = false;
    };

    void resetMoving();
    void setGear(int g);
    float ratio() const;
    float Tfull(float w) const;
    float Tzero(float we) const;
    void computeGearbox();
    void computeWheelConstants(Wheel& w);
    void wheelInputs();
    void engineUpdate(float dt);
    void gearboxUpdate(float dt);
    void aero(float dt, float invdt);
    void trainUpdate(Train& tr, float dt, float invdt, World& world);
    void wheelContact(Wheel& w, float dt, float invdt, World& world);
    void wheelTyre(Wheel& w, float dt, float invdt);
    WheelHost wheelHost();
    void afterReset();
    void antiRoll();
    void gyro();
    void unstuck(float dt);
    vec3 cogOffset(const M3& R) const;
    void throwPart(int i, float A, float B);
    int partIndex(const std::string& name) const;
    void wreckThrow();

    CarTune tune_;
    RigidBody body_;
    Wheel wheels_[4];
    std::vector<Train> trains_;  // update order; the drive train is last
    Engine eng_;
    int gear_ = 2;
    bool changed_ = true;
    float gearTimer_ = 0;
    bool automatic_ = true;
    float autoRatio_[8] = {}, manualRatio_[8] = {};
    float up_[8] = {}, downMin_[8] = {}, downMax_[8] = {};
    float arbK_[2] = {}, arbD_[2] = {};
    CarInputs in_;
    float speed_ = 0, speedMph_ = 0, sss_ = 1;
    // model frame of this step
    vec3 origin_;
    Mtx frame_;  // the model frame of step 5 (body rows, model origin)
    // control state
    bool reverseMode_ = false;
    bool autoReverse_ = true;
    // unstuck helper
    enum class Stuck { Inactive, Watch, Lift, Roll, Turn } stuck_ = Stuck::Inactive;
    vec3 stuckStart_;
    float stuckTimer_ = 0;
    float lastDt_ = 0;
    DamageState damage_;
    ImpactSink* impactSink_ = nullptr;
    // Parts thrown off (damage.md): per tune part, and the wreck throw done since the last reset.
    std::vector<bool> thrown_;
    bool wreckThrown_ = false;
    int paintSet_ = 0;
    World* world_ = nullptr;  // the world of the last step (thrown parts go there)
    CarEffectsHook* effects_ = nullptr;
    unsigned resetCount_ = 0;
    float engineScale_ = 1.0f;
    bool drivable_ = true;
    bool playerDisabled_ = false;
    bool netControls_ = false;
    float netCap_ = 1.0f, netSlowDown_ = 0.0f;
    bool netStopped_ = false;
    float extraMass_ = 0.0f;
    CountedImpactHook* countedHook_ = nullptr;
    CountedImpactHook* feedbackHook_ = nullptr;
    float weatherScale_ = 1.0f;

    float time_ = 0;
    int steps_ = 0;
    Material boundMaterial_;
    CarCollisionState col_;
    vec3 waterPoints_[64];
    std::unique_ptr<Trailer> trailer_;
};

}  // namespace phys
}  // namespace cc
