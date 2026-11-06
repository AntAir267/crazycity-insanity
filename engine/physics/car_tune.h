// Vehicle tune files (spec/physics/vehicle-tune.md): .vehCarSim, .vehGyro, .vehStuck, plus the
// wheel pivots from geometry/<car>_whl<i>.mtx. Values are float32 as read.
#pragma once

#include <string>
#include <vector>

#include "core/math.h"
#include "physics/bound.h"
#include "physics/damage.h"
#include "physics/loose_bodies.h"

#include <memory>

namespace cc {

class Vfs;

namespace phys {

struct WheelTune {
    float suspensionExtent = 0.2f;
    float suspensionLimit = 0.1f;
    float suspensionFactor = 1.0f;
    float suspensionDampCoef = 0.1f;
    float steeringLimit = 0.39f;
    float steeringOffset = 0.0f;
    float brakeCoef = 1.0f;
    float handbrakeCoef = 1.0f;
    float camberLimit = -1.0f;
    float wobbleLimit = 0.0f;
    float tireDispLimitLong = 0.075f;
    float tireDampCoefLong = 0.25f;
    float tireDragCoefLong = 0.02f;
    float tireDispLimitLat = 0.075f;
    float tireDampCoefLat = 0.25f;
    float tireDragCoefLat = 0.05f;
    float optimumSlipPercent = 0.14f;
    float staticFric = 2.0f;
    float slidingFric = 1.9f;
};

struct TrainTune {
    float angInertia = 5000.0f;
    float brakeDynamicCoef = 1.0f;
    float brakeStaticCoef = 1.2f;
};

struct AxleTune {
    float torqueCoef = 0.0f;
    float dampCoef = 0.0f;
};

struct CarSimTune {
    float mass = 2000.0f;
    vec3 inertiaBox{2.0f, 1.0f, 3.0f};
    vec3 centerOfGravity{0.0f, 0.0f, 0.0f};
    float boundFriction = 0.3f;
    float boundElasticity = 0.2f;
    int drivetrainType = 0;
    float sssValue = 1.0f;
    float sssThreshold = 0.0f;
    float carFrictionHandling = 1.0f;
    // Aero
    vec3 angCDamp, angVelDamp, angVel2Damp;
    float drag = 0.0f;
    float down = 0.0f;
    // Engine
    float engAngInertia = 1.0f;
    float maxHorsePower = 200.0f;
    float idleRPM = 750.0f;
    float optRPM = 5000.0f;
    float maxRPM = 8000.0f;
    float gcl = 0.25f;
    // Trans
    int manualNumGears = 7;
    int autoNumGears = 6;
    float reverse = 20.0f;
    float low = 20.0f;
    float high = 75.0f;
    float gearBias = 0.5f;
    float upshiftBias = 0.05f;
    float downshiftBiasMin = 0.05f;
    float downshiftBiasMax = 0.3f;
    float gearChangeTime = 0.8f;
    TrainTune drivetrain, freetrain;
    WheelTune wheelFront, wheelBack;
    AxleTune axleFront, axleBack;
};

struct GyroTune {
    float drift = 0, spin180 = 0, reverse180 = 0, pitch = 0, roll = 0;
};

struct StuckTune {
    float turn = 1.57f;
    float rotation = 0.39f;
    float translation = 0.1f;
    float timeThresh = 0.3f;
    float posThresh = 1.25f;
    float moveThresh = 1.75f;
};

struct WheelGeom {
    bool found = false;
    vec3 pivot;          // .mtx origin: wheel centre at rest, model space
    float radius = 0;    // |(max.y - min.y) * 0.5|
    float width = 0;     // max.x - min.x
};

// A part that can be thrown off (damage.md, "The car's loose parts"; loose-bodies.md 4).
struct LoosePartTune {
    std::string name;                // BREAK0 … BREAK03, VARIANT0 … VARIANT9, WHL0 … ENGINE
    bool gear = false;               // running gear (else a body part)
    int variant = -1;                // VARIANT<n>: n
    vec3 place;                      // p: the origin of <car>_<PART>.mtx (car space; 0 if missing)
    std::shared_ptr<LooseData> data; // prop data <car>_<PART>, its box, the part's meshes and radius
};

struct CarTune {
    std::string baseName;
    CarSimTune sim;
    GyroTune gyro;
    StuckTune stuck;
    WheelGeom wheels[4];
    std::shared_ptr<Bound> bound;    // bound/<car>_BOUND.bnd (collision.md, "The car's bound")
    float modelRadius = 0;           // BODY part radius of geometry/<car>.pkg (largest over its levels)
    // Water-effect inputs fixed at the first set-up (vehicle-tune.md): CenterOfGravity, InertiaBox.
    vec3 waterCoG, waterSize;
    DamageTune damage;               // tune/vehicle/<car>.vehCarDamage (damage.md)
    // The parts with an H mesh, body parts (BREAK…, VARIANT…) first, then the running gear, each
    // list in MM2's order; the package that holds their meshes.
    std::vector<LoosePartTune> looseParts;
    std::shared_ptr<const Pkg> pkg;
    std::vector<std::string> notes;  // missing files, bad values
    bool simFile = false, gyroFile = false, stuckFile = false;
};

// The joint of a trailer: tune/vehicle/<car>.dgTrailerJoint (trailer.md 2.3).
struct TrailerJointTune {
    float forceLimit = 0.0f;
    int jointStatus = 2;
    float restoreForceLean = 2.0f, dampConstLean = 2.0f, dampLinearLean = 0.9f;
    float restoreForceRoll = 2.0f, dampConstRoll = 0.1f, dampLinearRoll = 2.0f;
    float leanLimit = 3.1415927f;
    float limitElasticityLean = 1.0f, limitElasticityRoll = 0.0f;
    float freeRange = 0.15f, freeLean = 0.1f, freeRoll = 0.1f;
    float cosFreeLean = 0.0f;  // derived: cos(FreeLean) in double, rounded to float
};

// Everything a car's trailer is set up from (trailer.md 2.1-2.3, 2.7).
struct TrailerTune {
    std::string car;                 // the towing car's base name
    std::string name;                // <car>_trailer
    // tune/vehicle/<car>.vehTrailer (defaults of 2.2; the hitch points default to the .mtx origins)
    float mass = 3000.0f;
    vec3 inertiaBox{3.0f, 4.0f, 9.0f};
    vec3 carHitch, trailerHitch;     // CarHitchOffset (truck, from its centre of mass), TrailerHitchOffset
    WheelTune wheelFront, wheelBack;
    TrainTune drivetrain;
    TrailerJointTune joint;
    bool vehFile = false, jointFile = false;  // "loaded" = both exist (2.4 step 2)
    // geometry/<car>_trailer_TWHL<i>.mtx after the "previous wheel stays" rule (2.4 step 5)
    WheelGeom wheels[4];
    std::shared_ptr<Bound> bound;    // bound/<car>_trailer_BOUND.bnd (2.7)
    std::string boundMaterialName = "default";
    Material boundMaterialFile;      // its first material as the file (or the built-in default) has it
    float radius = 0;                // largest radius of the TRAILER meshes (H, M, L, VL)
    std::vector<std::string> notes;
};

bool parseVehTrailer(const std::string& text, TrailerTune& out, std::string* err = nullptr);
bool parseTrailerJoint(const std::string& text, TrailerJointTune& out, std::string* err = nullptr);
// trailer.md 1 (car-model.md, "Trailers"): true if the car of `car` tows a trailer (a TRAILER_HITCH
// mesh, a hitch .mtx origin that is not (0, 0, 0), and a trailer package with TRAILER, TWHL0 and
// TWHL1); then `out` holds its set-up data. Else false with the reason in `why`.
bool loadTrailerTune(const Vfs& vfs, const CarTune& car, TrailerTune& out, std::string& why);

// Parses a tune text file into the given structure (keys case-sensitive, rules of
// vehicle-tune.md "Syntax"). Returns false if the header is not "type: a".
bool parseCarSim(const std::string& text, CarSimTune& out, std::string* err = nullptr);
bool parseGyro(const std::string& text, GyroTune& out, std::string* err = nullptr);
bool parseStuck(const std::string& text, StuckTune& out, std::string* err = nullptr);

// Loads everything for a car. `playerCar` applies the player vpcop rule (set up a second time
// from vpmustang99). Missing files keep the defaults and are noted.
CarTune loadCarTune(const Vfs& vfs, const std::string& baseName, bool playerCar);

}  // namespace phys
}  // namespace cc
