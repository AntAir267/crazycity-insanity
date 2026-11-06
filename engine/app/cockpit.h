// The dash view's cockpit model and the rear-view mirror settings (spec/formats/dash.md).
#pragma once

#include <memory>
#include <string>

#include "app/engine_context.h"
#include "core/math.h"

namespace cc {

struct Pkg;
struct Mesh;
struct CamPose;

// tune/<car>.mmMirror with the defaults of dash.md.
struct MirrorSettings {
    vec3 position{0.0f, 1.4f, -1.0f};  // car space
    float sizeX = 0.3f, sizeY = 0.16f;  // fractions of the screen
    float fov = 10.0f;                  // vertical, degrees
    float aspect = 2.0f;
    float nearClip = 1.2f, farClip = 100.0f;
};
MirrorSettings loadMirrorSettings(EngineContext& ctx, const std::string& baseName);

// What the gauges and the wheel show this frame.
struct CockpitState {
    float speedMph = 0;  // >= 0
    float rpm = 0;       // displayed RPM
    float damage = 0, damageLimit = 0;
    int gear = 1;        // gear index (0 reverse, 1 neutral, 2 first, ...)
    float steer = 0;     // the car's steering input, -1 .. +1
};

class Cockpit {
public:
    // Loads geometry/<car>_dash.pkg, its .mtx pivots and tune/<car>_dash.asNode. False if the
    // package is missing (the dash view then shows no cockpit).
    bool load(EngineContext& ctx, const std::string& baseName);
    bool loaded() const { return pkg_ != nullptr; }

    // Draws the cockpit over the scene: fixed to the camera pose, near plane 0.01, depth test and
    // writes off, unlit, alpha blended, no fog. `farPlane` and `fovDeg`/`aspect` as the scene's.
    void draw(EngineContext& ctx, const CamPose& cam, float fovDeg, float aspect, float farPlane,
              const CockpitState& s) const;

private:
    enum Part { Damage, Dash, Gear, Roof, Speed, Tach, Wheel, Extra, PartCount };
    std::shared_ptr<const Pkg> pkg_;
    const Mesh* mesh_[PartCount] = {};
    vec3 centre_[PartCount];  // box centres of the .mtx files (speed, tach, damage, wheel)
    std::string name_;
    struct Tune {
        vec3 dashPos{0.0f, -0.707f, -1.216f}, roofPos{0.0f, 0.189f, 0.0f}, wheelPos;
        vec3 speedOffset, tachOffset, dmgOffset;
        vec3 speedPivotOffset, tachPivotOffset, dmgPivotOffset, wheelPivotOffset, gearPivotOffset;
        float wheelFact = 0.9f;
        float speedRotMin = -0.907f, speedRotMax = 7.0f;
        float rpmRotMin = -0.907f, rpmRotMax = 7.0f;
        float damageRotMin = -0.907f, damageRotMax = 7.0f;
    } t_;
};

}  // namespace cc
