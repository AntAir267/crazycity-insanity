// Driving cameras (spec/engine/cameras.md): near/far/indoor chase, bumper and dash point of view,
// orbit "cross view", switching and blending.
#pragma once

#include <memory>
#include <vector>

#include "core/math.h"
#include "drive/drive_tune.h"

namespace cc {

namespace phys {
class World;
}

// A camera pose: axes X (right), Y (up), Z (backward; the camera looks along -Z), position.
struct CamPose {
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1}, p;
};

// What the cameras read from the car each frame.
struct CameraSubject {
    vec3 X, Y, Z;       // car axes (Z backward)
    vec3 origin;        // car model origin
    float steer = 0, throttle = 0, handbrake = 0;
    float speed = 0;    // unsigned car speed
    bool reverseGear = false;
    int wheelsTouching = 4;
    bool grounded[4] = {true, true, true, true};
    vec3 normal[4];     // wheels' contact normals
    float angMom2 = 0;  // |angular momentum|^2
    bool enclosedRoom = false;

    vec3 toWorld(const vec3& q) const { return X * q.x + Y * q.y + Z * q.z + origin; }
};

struct CameraFrame {
    CamPose pose;
    float fov = 60;   // vertical, degrees
    float near = 0.5f;
};

class ChaseCamera {
public:
    explicit ChaseCamera(const ChaseTune& t, bool nearOrFar) : t_(t), nearOrFar_(nearOrFar) { a_ = t.appXZPos; }
    void reset();
    void update(const CameraSubject& s, float dt, phys::World* world, float aspect);
    CameraFrame frame() const { return {cur_, t_.cameraFOV, t_.cameraNear}; }
    float blendGoal() const { return t_.blendGoal; }
    // Debugging: this frame's track point, approach rate, swing angle and reverse state.
    const vec3& trackPoint() const { return P0_; }
    float approachRate() const { return a_; }
    float swing() const { return theta_; }
    bool reversing() const { return rev_; }
    float maxDist() const { return t_.maxDist; }

private:
    vec3 P0_;
    ChaseTune t_;
    bool nearOrFar_;
    CamPose cur_, tgt_;
    vec3 N_{0, 1, 0};
    float theta_ = 0;
    bool rev_ = false;
    float rdir_ = 1, rt_ = 0;
    bool g_ = true;
    float airT_ = 0, groundT_ = 0;
    bool b1_ = false, b2_ = false;
    vec3 Pold_, Gold_;
    bool havePold_ = false;
    float a_ = 0;
    float wPos_[3] = {}, wAng_[3] = {};
    bool fresh_ = true;
};

class PovCamera {
public:
    explicit PovCamera(const PovTune& t) : t_(t) {}
    void reset() { fresh_ = true; }
    void update(const CameraSubject& s, float dt, int look);
    CameraFrame frame() const { return {cur_, t_.cameraFOV, t_.cameraNear}; }
    float blendGoal() const { return t_.blendGoal; }

private:
    PovTune t_;
    CamPose cur_, tgt_;
    float wPos_[3] = {}, wAng_[3] = {};
    bool fresh_ = true;
};

class OrbitCamera2 {
public:
    void update(const CameraSubject& s);
    CameraFrame frame() const { return {pose_, 50.0f, 0.1f}; }

private:
    CamPose pose_;
};

// The race modes' cameras (races.md, "Cameras"): the pre-race camera hovering behind and above
// the car, and the fixed point camera of the post-race and water views.
class PreRaceCamera {
public:
    void start(const CameraSubject& s);
    void update(const CameraSubject& s);
    CameraFrame frame() const { return {pose_, 50.0f, 3.0f}; }

private:
    float heading_ = 0;
    CamPose pose_;
};

class PointCamera {
public:
    void place(const vec3& p, float D) {
        pos_ = p;
        D_ = D;
    }
    void update(const CameraSubject& s);
    CameraFrame frame() const { return {pose_, fov_, 0.5f}; }

private:
    vec3 pos_;
    float D_ = 25.0f;
    float fov_ = 60.0f;
    CamPose pose_;
};

// Owns the cameras of one car and does the switching (C, F/D, V) and blending.
class CameraRig {
public:
    enum View { Near = 0, Bumper = 1, Far = 2, Dash = 3, Orbit = 4, Indoor = 5, PreRace = 6, Point = 7 };

    void setup(const CameraFiles& files, bool indoorCapable);
    void resetAll();  // new game / car reset: remembered external entry, no blend
    // View settings of the player file at a game start (controls.md): byte 0 the external entry
    // (0 near, 1 bumper, 2 far), byte 3 the dash view. Takes effect at the next resetAll().
    void setViewState(int externalEntry, bool dash) {
        external_ = externalEntry == 1 ? Bumper : externalEntry == 2 ? Far : Near;
        active_ = dash ? Dash : external_;
    }
    int externalEntry() const { return external_ == Bumper ? 1 : external_ == Far ? 2 : 0; }
    // Turns the dash view off (the half-screen map), back to the external entry, no blend.
    void leaveDash() {
        if (active_ == Dash) switchTo(external_, 0.0f, nullptr);
    }
    void enterDash() {
        if (active_ != Dash && active_ != Orbit) switchTo(Dash, 0.0f, nullptr);
    }
    void cycle();     // C
    void toggleDash(bool wide);  // D (ignored in the orbit view; cannot turn the dash on in the wide view)
    void toggleOrbit();
    // A view key other than C, D and V that was not ignored (W, Backspace, Tab, the full-screen map
    // key): at its end the "bring the dash back" mark is the dash view's own state again, so in the
    // orbit view it is cleared and V then returns to the external entry (cameras.md, "Switching
    // views").
    void noteViewKey() { orbitFromDash_ = active_ == Dash; }
    // Indoor chase switch test inputs: car room enclosed / landmark room with a ceiling above.
    void update(const CameraSubject& s, float dt, phys::World* world, float aspect, int look, bool indoorWanted);

    CameraFrame frame() const { return shown_; }
    View active() const { return active_; }
    // The active chase camera (near, far or indoor), or null.
    const ChaseCamera* activeChase() const {
        if (active_ == Near) return near_.get();
        if (active_ == Far) return far_.get();
        if (active_ == Indoor) return indoor_.get();
        return nullptr;
    }
    bool bodyHidden() const { return active_ == Bumper || active_ == Dash; }
    bool lookingAround() const { return look_ != 0; }
    float farPlane() const { return files_.farPlane; }
    const char* name() const;

    // ---- Race cameras (races.md, "Cameras") ----
    // At every race restart: the pre-race camera, blended to the player's view in 3.5 s (type 3);
    // the bumper and dash views are reached through the near chase camera.
    void startPreRace(const CameraSubject& s);
    // Running until the view shown is the intended one.
    bool preRaceRunning() const { return preRace_; }
    // The post-race point camera (the far chase camera's position + 3.5 m up, 0.8 s blend).
    void startPostRace(const CameraSubject& s, phys::World* world, float aspect, float dt);
    // The water camera (the shown camera's position + 9 m up, 0.8 s blend).
    void startWater();
    // No key changes the camera while a pre-race or post-race camera runs.
    bool locked() const { return preRace_ || postRace_; }
    // Back to the remembered view without a blend (a restart ends the post-race camera).
    void endRaceCameras();
    // The dash view should come back after the fly-in (the caller's dash display).
    bool dashWanted() const { return dashAfter_; }

private:
    void switchTo(View v, float duration, const CameraSubject* s, int easing = 3);
    CameraFrame evaluate(View v) const;
    void updateView(View v, const CameraSubject& s, float dt, phys::World* world, float aspect, int look);

    CameraFiles files_;
    std::unique_ptr<ChaseCamera> near_, far_, indoor_;
    std::unique_ptr<PovCamera> bumper_, dash_;
    OrbitCamera2 orbit_;
    View external_ = Near;  // current entry of the C list
    bool orbitFromDash_ = false;  // V from the dash view: V again returns to it
    View active_ = Near;
    bool indoorCapable_ = false;
    // blend
    bool blending_ = false;
    View from_ = Near;
    float remaining_ = 0, duration_ = 0.8f;
    int easing_ = 3;
    // race cameras
    PreRaceCamera preCam_;
    PointCamera pointCam_;
    bool preRace_ = false, postRace_ = false, postUsed_ = false, dashAfter_ = false;
    struct Step {
        View view;
        float duration;
        int easing;
    };
    std::vector<Step> queue_;  // blends that follow the current one (pre-race into bumper or dash)
    CameraFrame shown_;
    vec3 carOrigin_;
    int look_ = 0;
};

}  // namespace cc
