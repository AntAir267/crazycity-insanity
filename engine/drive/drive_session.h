// One driving session: the car's physics, the player's controls, the cameras, in the frame order
// of spec/engine/controls.md and spec/physics/vehicle.md. No graphics (the app draws it).
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "drive/camera_rig.h"
#include "drive/force_feedback.h"
#include "drive/player_controls.h"
#include "model/vehicle.h"
#include "physics/car.h"
#include "physics/scene.h"
#include "physics/world.h"

namespace cc {

class Vfs;
class AssetCache;
namespace phys {
class Trailer;
}
class CityData;
struct InputState;

struct HudState {
    int mph = 0;
    float kmh = 0;
    float rpm = 0, maxRpm = 1;
    char gear = 'P';
    bool automatic = true;
    const char* camera = "";
    bool hidden = false;  // H key, or looking around in a point-of-view camera
};

// Moving wheels and fenders of a simulated car drawn with `model` (also used for the police cars).
// `tilt` (optional, 4 angles in radians, wheels 0-3): the wobble of a damaged car
// (damage-visuals.md, "Wobbling wheels"), turned about the spun wheel's own Z axis.
WheelPoseFn carWheelPose(const phys::Car* car, const VehicleModel* model, const float* tilt = nullptr);

class DriveSession {
public:
    DriveSession();
    ~DriveSession();
    // Loads the car's physics and cameras, and its trailer if the car tows one (trailer.md 1).
    // `world` must outlive the session.
    bool start(const Vfs& vfs, AssetCache& assets, const std::string& baseName, VehicleKind kind,
               int paint, int infoFlags, phys::CollisionWorld* world, float aspect, std::string& message);
    // Network Cruise and Cops and Robbers: no trailer for the player's car (car-model.md, "Trailers";
    // multiplayer.md 1.4). Set before start(); on by default.
    void setTrailerAllowed(bool a) { trailerAllowed_ = a; }
    // MM2's placement rule at a start point (vehicle.md); flat world: the check-run start.
    void placeAt(const vec3& q, float headingRadians);
    void placeFlat();
    // The full reset of the world limits: back to the last placement.
    void resetToStart();

    // One rendered frame: controls, events, physics steps, cameras.
    void frame(const InputState& in, float frameSeconds, float aspect, const CityData* city);

    const phys::Car& car() const { return car_; }
    phys::Car& car() { return car_; }
    const VehicleModel* model() const { return model_.get(); }
    // The towed trailer (physics and model), or null.
    phys::Trailer* trailer() { return car_.trailer(); }
    const phys::Trailer* trailer() const { return car_.trailer(); }
    const VehicleModel* trailerModel() const { return model_ && car_.trailer() ? model_->trailer.get() : nullptr; }
    mat4 trailerMatrix() const;  // the trailer's frame as a column-major matrix
    WheelPoseFn trailerWheelPose() const;
    // The player's movers of a frame (car-collision.md 1.1): the car, then its trailer.
    void appendMovers(std::vector<phys::SceneMover>& movers);
    int paint() const { return paint_; }
    mat4 carMatrix() const;  // model frame as a column-major matrix
    WheelPoseFn wheelPose() const;
    const phys::CarTune& tune() const { return car_.tune(); }
    const CameraRig& cameras() const { return cams_; }
    CameraRig& cameras() { return cams_; }
    HudState hud() const;
    bool wide() const { return wide_; }
    // View settings of the player file (controls.md, "View settings"): the wide view and the
    // gauges overlay (H), set at a game start and read back at its end.
    void setWide(bool on) { wide_ = on; }
    bool gaugesOn() const { return !hudHidden_; }
    void setGaugesOn(bool on) { hudHidden_ = !on; }
    // HUD map interlocks (hudmap.md): the wide-view key does nothing in map modes 2 and 3, the
    // dash key nothing in mode 2.
    void setViewKeyLocks(bool wideLocked, bool dashLocked) {
        wideLocked_ = wideLocked;
        dashLocked_ = dashLocked;
    }
    // A view key was pressed this frame (camera, cross view, wide, dash, mirror): the map
    // re-applies its mode.
    bool viewKeyPressed() const { return viewKey_; }
    bool cameraKeyPressed() const { return cameraKey_; }  // C or V this frame
    bool mirrorKeyPressed() const { return mirrorKey_; }
    // The car was reset this frame (the reset key, or back to the start after the world limits).
    bool carResetThisFrame() const { return carReset_; }
    bool leaveRequested() const { return leave_; }
    // Backspace this frame (the view owns the mirror state).
    bool takeMirrorToggle() {
        const bool t = mirrorToggle_;
        mirrorToggle_ = false;
        return t;
    }
    void clearLeave() { leave_ = false; }
    int room() const { return room_; }
    void setAutomatic(bool a) { car_.setAutomatic(a); }
    // The player's force feedback (controls.md, "Force feedback"): configured by the app each frame,
    // updated in frame() before the physics and on the car's counted impacts.
    ForceFeedback& forceFeedback() { return ff_; }
    // A game's binding table (null: the viewer's control scheme), player_controls.h.
    void setBindings(const ControlBindings* b) { controls_.setBindings(b); }
    void setAutoReverse(bool a) { car_.setAutoReverse(a); }
    // Static geometry the cameras probe (defaults to the car's world).
    void setCameraWorld(phys::World* w) { cameraWorld_ = w; }
    // The AI update before the frame's physics steps (frame time F), the AI bodies inside each step
    // and the bookkeeping after them (traffic.md, "Frame order").
    struct AiHooks {
        std::function<void(float F)> beforePhysics;
        // When set, one physics sample of every mover (car-collision.md 1.2) in place of the
        // player car's own step.
        std::function<void(float h, float invh)> sample;
        std::function<void()> afterPhysics;
    };
    void setAiHooks(const AiHooks& h) { ai_ = h; }
    // Race modes (game/race.h): the race's steps 1–3 run after the devices are read and before
    // the car's control step; the race owns wrecks, water and falling through (no Cruise rules),
    // and a car that is not drivable gets brake and neutral before the physics.
    struct RaceHooks {
        std::function<void(float F, const ControlEvents& ev)> beforeControls;
    };
    void setRaceHooks(const RaceHooks& h) { race_ = h; }
    // Scripted checks: replaces the player's controls just before the car's control step.
    void setControlOverride(std::function<void(float F, phys::DriverControls&)> f) { override_ = std::move(f); }
    bool raceMode() const { return bool(race_.beforeControls); }
    // The race cameras (races.md, "Cameras").
    void startPreRaceCamera();
    void startPostRaceCamera();
    void startWaterCamera();
    // While the game is paused (collision.md, "World limits"): Cruise's falling-through and water
    // checks still run with the frame time (the car does not move).
    void pausedWorldLimits(float frameSeconds, const CityData* city);
    bool hornHeld() const { return horn_; }
    float wreckTimer() const { return car_.drivable() ? 0.0f : wreckTimer_; }
    // Debugging: the last frame's clamped length, the car's heading (radians, 0 = facing −Z,
    // positive = turned left) and its yaw rate over that frame (rad/s).
    float lastFrame() const { return lastF_; }
    float heading() const { return heading_; }
    float yawRate() const { return yawRate_; }

private:
    float wreckTimer_ = 0.0f;  // free roam: time left not drivable after a wreck
    float lastF_ = 0.0f, heading_ = 0.0f, yawRate_ = 0.0f;
    bool haveHeading_ = false;
    CameraSubject subject(const CityData* city) const;

    phys::Car car_;
    std::unique_ptr<phys::Trailer> pendingTrailer_;  // set up, attached at the first placement
    phys::CollisionWorld* cworld_ = nullptr;
    phys::Scene scene_;  // flat ground with a trailer: car and trailer samples
    std::vector<phys::SceneMover> movers_;
    PlayerControls controls_;
    ForceFeedback ff_;
    struct FfImpactHook : phys::Car::CountedImpactHook {
        ForceFeedback* ff = nullptr;
        void countedImpact(const phys::Car& car, int otherIdentity, float slotTotal) override {
            (void)otherIdentity;
            ff->impact(car, slotTotal);
        }
    } ffHook_;
    CameraRig cams_;
    std::unique_ptr<VehicleModel> model_;
    phys::World* world_ = nullptr;
    phys::World* cameraWorld_ = nullptr;
    int paint_ = 0;
    bool wide_ = false;
    bool hudHidden_ = false;
    bool wideLocked_ = false, dashLocked_ = false;
    bool viewKey_ = false, cameraKey_ = false, mirrorKey_ = false, carReset_ = false;
    bool leave_ = false;
    bool mirrorToggle_ = false;
    bool horn_ = false;
    AiHooks ai_;
    RaceHooks race_;
    std::function<void(float F, phys::DriverControls&)> override_;
    float lastAspect_ = 1.0f;
    const CityData* lastCity_ = nullptr;
    int room_ = 0;
    vec3 startPos_;
    float startHeading_ = 0;
    bool flatStart_ = true;
    bool trailerAllowed_ = true;
};

}  // namespace cc
