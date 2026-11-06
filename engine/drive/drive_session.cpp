#include "drive/drive_session.h"

#include <cmath>
#include <cstdio>

#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "model/asset_cache.h"
#include "physics/drive_check.h"
#include "physics/trailer.h"
#include "platform/platform.h"
#include "vfs/vfs.h"

namespace cc {

DriveSession::DriveSession() = default;
DriveSession::~DriveSession() = default;

bool DriveSession::start(const Vfs& vfs, AssetCache& assets, const std::string& baseName, VehicleKind kind,
                         int paint, int infoFlags, phys::CollisionWorld* world, float aspect, std::string& message) {
    world_ = world;
    cworld_ = world;
    paint_ = paint;
    phys::CarTune tune = phys::loadCarTune(vfs, baseName, kind == VehicleKind::Player);
    for (const auto& n : tune.notes) logFile(LogLevel::Warn, baseName, n);
    for (int i = 0; i < 4; ++i)
        if (!tune.wheels[i].found) {
            message = strf("%s has no wheel pivot %d; it cannot be driven", baseName.c_str(), i);
            return false;
        }
    car_.setup(tune);
    ffHook_.ff = &ff_;
    car_.setFeedbackImpactHook(&ffHook_);
    controls_.setTune(loadSteeringTune(vfs, baseName));
    controls_.reset();
    // Indoor chase: .info Flags bits 1, 2 or 16 (cameras.md).
    cams_.setup(loadCameraFiles(vfs, baseName, aspect), (infoFlags & (1 | 2 | 16)) != 0);
    model_ = std::make_unique<VehicleModel>();
    if (!loadVehicle(assets, baseName, kind, *model_)) {
        message = "the car's model cannot be loaded";
        return false;
    }
    // The trailer (trailer.md 1: the player's car always tows it if its model has one).
    if (kind == VehicleKind::Player && model_->trailer && world && trailerAllowed_) {
        phys::TrailerTune tt;
        std::string why;
        if (phys::loadTrailerTune(vfs, car_.tune(), tt, why)) {
            for (const auto& n : tt.notes) logFile(LogLevel::Info, tt.name, n);
            // Placed and attached at the first placement (placeAt / placeFlat), which is then its
            // starting frame (the previous matrix of its first sample, as in check runs).
            pendingTrailer_ = std::make_unique<phys::Trailer>();
            pendingTrailer_->setup(tt, car_, world, false);
            pendingTrailer_->identity = 10501;
        } else {
            logInfo("%s: no trailer (%s)", baseName.c_str(), why.c_str());
        }
    }
    if (!pendingTrailer_) model_->trailer.reset();
    car_.setPaintSet(paintSetFor(paint, model_->setCount()));  // its VARIANT part (damage.md)
    message = tune.simFile ? "" : "no .vehCarSim: default tuning";
    return true;
}

void DriveSession::placeAt(const vec3& q, float heading) {
    car_.placeAt(q, heading);
    if (pendingTrailer_) car_.attachTrailer(std::move(pendingTrailer_));
    controls_.reset();
    cams_.resetAll();
    startPos_ = q;
    startHeading_ = heading;
    flatStart_ = false;
}

void DriveSession::placeFlat() {
    car_.resetFlat(0.0f, 0.0f, 0.0f, 0.0f);
    if (pendingTrailer_) car_.attachTrailer(std::move(pendingTrailer_));
    controls_.reset();
    cams_.resetAll();
    flatStart_ = true;
}

void DriveSession::resetToStart() {
    // A race reset: the physics random generator restarts and every prop comes back.
    phys::resetBumpRandom();
    if (world_) world_->resetPlacedObjects();
    if (flatStart_) placeFlat();
    else placeAt(startPos_, startHeading_);
}

mat4 DriveSession::carMatrix() const {
    const phys::M3& R = car_.rotation();
    const vec3 o = car_.modelOrigin();
    mat4 m = mat4::identity();
    for (int i = 0; i < 3; ++i) {
        m.m[i * 4 + 0] = R.r[i].x;
        m.m[i * 4 + 1] = R.r[i].y;
        m.m[i * 4 + 2] = R.r[i].z;
    }
    m.m[12] = o.x;
    m.m[13] = o.y;
    m.m[14] = o.z;
    return m;
}

WheelPoseFn DriveSession::wheelPose() const { return carWheelPose(&car_, model_.get()); }

mat4 DriveSession::trailerMatrix() const {
    mat4 m = mat4::identity();
    const phys::Trailer* t = car_.trailer();
    if (!t) return m;
    const phys::RigidBody& b = t->body();
    for (int i = 0; i < 3; ++i) {
        m.m[i * 4 + 0] = b.R.r[i].x;
        m.m[i * 4 + 1] = b.R.r[i].y;
        m.m[i * 4 + 2] = b.R.r[i].z;
    }
    m.m[12] = b.p.x;
    m.m[13] = b.p.y;
    m.m[14] = b.p.z;
    return m;
}

WheelPoseFn DriveSession::trailerWheelPose() const {
    const phys::Trailer* t = car_.trailer();
    return [t](const std::string& part, DrawPart& d) -> bool {
        int n = -1;
        if (!t || std::sscanf(part.c_str(), "TWHL%d", &n) != 1 || n < 0 || n > 3) return false;
        // trailer.md 11: the wheel's frame of its last contact call (the steering turn about the
        // inner edge and the pivot), without spin or suspension travel.
        const phys::Wheel& w = t->wheel(n);
        vec3 c = w.pivot;
        float a = 0.0f;
        if (w.tune.steeringLimit != 0.0f) {
            a = w.steerAngle;
            const float o = (phys::fsign(w.pivot.x) * w.width) * 0.5f;
            const float ca = phys::fcos(a), sa = phys::fsin(a);
            c = vec3(w.pivot.x - o, w.pivot.y, w.pivot.z);
            c = vec3(o * ca + c.x, c.y, o * -sa + c.z);
        }
        d.hasTransform = true;
        d.transform = mat4::translate(c) * mat4::rotateY(a);
        return true;
    };
}

void DriveSession::appendMovers(std::vector<phys::SceneMover>& movers) {
    phys::SceneMover p;
    p.kind = phys::SceneMover::CarMover;
    p.car = &car_;
    p.player = true;
    p.number = 1;
    p.identity = 10001;
    movers.push_back(p);
    if (phys::Trailer* t = car_.trailer()) {
        phys::SceneMover m;
        m.kind = phys::SceneMover::TrailerMover;
        m.trailer = t;
        m.number = 1;
        m.identity = t->identity;
        movers.push_back(m);
    }
}

WheelPoseFn carWheelPose(const phys::Car* car, const VehicleModel* model, const float* tilt) {
    return [car, model, tilt](const std::string& part, DrawPart& d) -> bool {
        // A part thrown off is no longer drawn with its car (damage.md, "Parts thrown off").
        if (car->partThrown(part)) {
            d.mesh = nullptr;
            return true;
        }
        auto wheelMatrix = [&](int n) {
            const phys::Wheel& w = car->wheel(n);
            const vec3 c = w.pivot + vec3(0.0f, w.comp - w.visualDisp(), 0.0f);
            mat4 m = mat4::translate(c) * mat4::rotateY(w.steerAngle) * mat4::rotateX(w.rotAngle);
            if (tilt && tilt[n] != 0.0f) {
                // Wobble: about the wheel's own (already spun) Z axis.
                mat4 z = mat4::identity();
                const float cz = std::cos(tilt[n]), sz = std::sin(tilt[n]);
                z.m[0] = cz;
                z.m[1] = sz;
                z.m[4] = -sz;
                z.m[5] = cz;
                m = m * z;
            }
            return m;
        };
        int n = -1;
        if (std::sscanf(part.c_str(), "WHL%d", &n) == 1 || std::sscanf(part.c_str(), "HUB%d", &n) == 1) {
            if (n >= 0 && n < 4) {
                d.hasTransform = true;
                d.transform = wheelMatrix(n);
                return true;
            }
            if (n == 4 || n == 5) {
                // Six-wheelers: the live WHL2/WHL3 transform moved toward the rear by 2.2 radii.
                const int src = n - 2;
                d.hasTransform = true;
                d.transform = mat4::translate(vec3(0, 0, 2.2f * car->wheel(src).radius)) * wheelMatrix(src);
                return true;
            }
        }
        if (std::sscanf(part.c_str(), "FNDR%d", &n) == 1 && n >= 0 && n < 2) {
            // car-model.md, "Fenders": X = the drawn wheel's X axis (after the spin and the wobble
            // turn, damage-visuals.md), Y = the car's Y, Z = X × Y; origin W + o in that frame, W the
            // drawn wheel's position, o FNDR0's offset (mirrored in x for FNDR1).
            vec3 o;
            fenderOffset(*model, o);
            if (n == 1) o.x = -o.x;
            const mat4 wm = wheelMatrix(n);
            const vec3 X(wm.m[0], wm.m[1], wm.m[2]), Y(0.0f, 1.0f, 0.0f), W(wm.m[12], wm.m[13], wm.m[14]);
            const vec3 Z(Y.z * X.y - Y.y * X.z, X.z * Y.x - Y.z * X.x, Y.y * X.x - X.y * Y.x);
            const vec3 p(((W.x + o.x * X.x) + o.y * Y.x) + o.z * Z.x, ((W.y + o.x * X.y) + o.y * Y.y) + o.z * Z.y,
                         ((W.z + o.x * X.z) + o.y * Y.z) + o.z * Z.z);
            mat4 f = mat4::identity();
            f.m[0] = X.x, f.m[1] = X.y, f.m[2] = X.z;
            f.m[4] = Y.x, f.m[5] = Y.y, f.m[6] = Y.z;
            f.m[8] = Z.x, f.m[9] = Z.y, f.m[10] = Z.z;
            f.m[12] = p.x, f.m[13] = p.y, f.m[14] = p.z;
            d.hasTransform = true;
            d.transform = f;
            return true;
        }
        {
            const float t[4] = {car->wheel(0).comp - car->wheel(0).visualDisp(), car->wheel(1).comp - car->wheel(1).visualDisp(),
                                car->wheel(2).comp - car->wheel(2).visualDisp(), car->wheel(3).comp - car->wheel(3).visualDisp()};
            mat4 lm;
            bool isLive = false;
            if (liveSuspensionMatrix(*model, part, t, lm, isLive)) {
                d.hasTransform = true;
                d.transform = lm;
                return true;
            }
            if (isLive) {
                d.mesh = nullptr;  // no .mtx: not drawn
                return true;
            }
        }
        return false;
    };
}

CameraSubject DriveSession::subject(const CityData* city) const {
    CameraSubject s;
    const phys::M3& R = car_.rotation();
    s.X = R.r[0];
    s.Y = R.r[1];
    s.Z = R.r[2];
    s.origin = car_.modelOrigin();
    s.steer = car_.inputs().steer;
    s.throttle = car_.inputs().throttle;
    s.handbrake = car_.inputs().handbrake;
    s.speed = car_.speed();
    s.reverseGear = car_.gearIndex() == 0;
    s.wheelsTouching = car_.groundedWheels();
    for (int i = 0; i < 4; ++i) {
        s.grounded[i] = car_.wheel(i).grounded;
        s.normal[i] = car_.wheel(i).N;
    }
    const vec3& L = car_.angularMomentum();
    s.angMom2 = dot(L, L);
    if (city && room_ > 0 && size_t(room_) < city->rooms().size())
        s.enclosedRoom = (city->rooms()[size_t(room_)].flags & PsdlEnclosed) != 0;
    return s;
}

void DriveSession::frame(const InputState& in, float frameSeconds, float aspect, const CityData* city) {
    // Frame split (vehicle.md, "Time step").
    float F = frameSeconds;
    if (F < 0.0001f) F = 0.0001f;
    if (F > 0.1f) F = 0.1f;
    const int n = phys::physicsStepsForFrame(F);
    const float dt = F / float(n);
    const float invdt = 1.0f / dt;

    // 1-2: devices to controls.
    ControlEvents ev;
    phys::DriverControls dc = controls_.read(in, F, ev);
    lastAspect_ = aspect;
    lastCity_ = city;
    // While a pre-race or post-race camera runs no key changes the camera (cameras.md).
    if (cams_.locked()) ev.camera = ev.orbit = ev.dash = false;
    // 4: press events.
    viewKey_ = ev.camera || ev.orbit || ev.dash || ev.wide || ev.mirror;
    cameraKey_ = ev.camera || ev.orbit;
    mirrorKey_ = ev.mirror;
    carReset_ = ev.reset;
    if (ev.camera) cams_.cycle();
    if (ev.orbit) cams_.toggleOrbit();
    if (ev.dash && !dashLocked_) cams_.toggleDash(wide_);
    // W is ignored in the dash view (and in the half-screen and full-screen map modes).
    if (ev.wide && !wideLocked_ && cams_.active() != CameraRig::Dash) {
        wide_ = !wide_;
        cams_.noteViewKey();
    }
    // H: the gauges overlay only; in the dash view it is hidden and H cannot change it.
    if (ev.hud && cams_.active() != CameraRig::Dash) hudHidden_ = !hudHidden_;
    if (ev.transmission) car_.setAutomatic(!car_.automatic());
    if (ev.reverse) car_.toggleReverse();
    if (ev.leave) leave_ = true;
    if (ev.mirror) {
        mirrorToggle_ = true;
        cams_.noteViewKey();
    }
    horn_ = ev.horn;
    if (ev.reset && raceMode()) {
        // In a race our reset key only rights the car (damage, wrecks and the race's hold stay).
        car_.resetOnWheels();
        controls_.reset();
    } else if (ev.reset) {
        car_.resetOnWheels();
        car_.clearDamage();  // engine choice: our reset key also clears the damage (Q44)
        wreckTimer_ = 0.0f;
        car_.setDrivable(true);
        controls_.reset();
        cams_.resetAll();
    }
    // Race modes: the mode's update, water and falling through, the gates and the clocks.
    if (race_.beforeControls) race_.beforeControls(F, ev);
    if (override_) override_(F, dc);
    // 5-6: the car's control step, then the speed-sensitive steering rates for the next frame.
    car_.controlStep(dc);
    controls_.recompute(car_.speed());
    // A wrecked car in free roam (damage.md): 3.0 s not drivable (brake 1 and neutral at the start
    // of the physics), then the damage is cleared and the car drives on (neutral or reverse → first).
    if (raceMode()) {
        if (!car_.drivable()) car_.holdNotDrivable();
    } else if (car_.drivable() && car_.wrecked()) {
        car_.setDrivable(false);
        wreckTimer_ = 3.0f;
    }
    if (!raceMode() && !car_.drivable()) {
        wreckTimer_ -= F;
        if (!(wreckTimer_ > 0.0f)) {
            car_.clearDamage();
            car_.setDrivable(true);
            if (car_.gearIndex() <= 1) car_.selectGear(2);
        } else {
            car_.holdNotDrivable();
        }
    }
    // The force feedback, at the end of the player's frame update (controls.md, "Each frame";
    // nothing unless it is active).
    ff_.frame(car_);
    // The AI update (traffic, later police) runs once before the physics steps.
    if (ai_.beforePhysics) ai_.beforePhysics(F);
    // 7: physics. Ctrl+B goes to the trailer's joint for this frame (trailer.md 7.2).
    phys::Trailer* trailer = car_.trailer();
    if (trailer) {
        if (ev.detachTrailer && trailer->attached()) logInfo("Ctrl+B: the trailer comes off");
        trailer->setDetachKey(ev.detachTrailer);
    }
    for (int k = 0; k < n; ++k) {
        if (ai_.sample) {
            ai_.sample(dt, invdt);
        } else if (trailer && cworld_) {
            // The car and its trailer as movers 1 and 2 (car-collision.md 1.2).
            movers_.clear();
            appendMovers(movers_);
            phys::SceneHooks hooks;
            scene_.runSample(movers_, *cworld_, dt, invdt, hooks);
        } else {
            car_.step(dt, invdt, *world_);
        }
    }
    if (trailer) trailer->setDetachKey(false);
    if (ai_.afterPhysics) ai_.afterPhysics();

    // World limits (collision.md): below y = -50, or more than 5 s in deep water, is a full reset
    // to the start, as in cruise. The flat ground has no limits.
    const vec3 o = car_.modelOrigin();
    const vec3 to = trailer ? trailer->body().p : vec3();
    if (!std::isfinite(o.x) || !std::isfinite(o.y) || !std::isfinite(o.z) || !std::isfinite(to.x) ||
        !std::isfinite(to.y) || !std::isfinite(to.z)) {
        logWarn("the car's state is not finite; back to the start");
        resetToStart();
        carReset_ = true;
    } else if (city && !raceMode() && car_.worldLimitReset(F)) {
        logInfo(car_.collision().water ? "the car sank: back to the start" : "the car fell through the city: back to the start");
        resetToStart();
        carReset_ = true;
    }

    {
        const vec3 f = car_.forward();
        const float h = float(std::atan2(-double(f.x), -double(f.z)));
        float dh = h - heading_;
        while (dh > 3.14159265f) dh -= 6.2831853f;
        while (dh < -3.14159265f) dh += 6.2831853f;
        yawRate_ = haveHeading_ ? dh / F : 0.0f;
        heading_ = h;
        haveHeading_ = true;
        lastF_ = F;
    }
    if (city) room_ = city->findRoom(car_.modelOrigin(), room_);
    // Indoor chase test: enclosed room, or a landmark room with something 100 m above the camera.
    bool indoor = false;
    phys::World* camWorld = cameraWorld_ ? cameraWorld_ : world_;
    if (city && room_ > 0) {
        const CityRoom& r = city->rooms()[size_t(room_)];
        if (r.flags & PsdlEnclosed) indoor = true;
        else if (r.hasLandmark && camWorld) {
            phys::ProbeState ps;
            phys::ProbeHit hit;
            const vec3 cp = cams_.frame().pose.p;
            indoor = camWorld->probe(cp, cp + vec3(0, 100.0f, 0), ps, hit);
        }
    }
    cams_.update(subject(city), F, camWorld, aspect, ev.look, indoor);
}

void DriveSession::startPreRaceCamera() { cams_.startPreRace(subject(lastCity_)); }

void DriveSession::startPostRaceCamera() {
    phys::World* camWorld = cameraWorld_ ? cameraWorld_ : world_;
    cams_.startPostRace(subject(lastCity_), camWorld, lastAspect_, lastF_ > 0 ? lastF_ : 0.016666668f);
}

void DriveSession::startWaterCamera() { cams_.startWater(); }

void DriveSession::pausedWorldLimits(float frameSeconds, const CityData* city) {
    if (raceMode() || !city) return;
    float F = frameSeconds;
    if (F < 0.0001f) F = 0.0001f;
    if (F > 0.1f) F = 0.1f;
    if (car_.worldLimitReset(F)) resetToStart();
}

HudState DriveSession::hud() const {
    HudState h;
    const float v = std::fabs(car_.signedSpeed());
    h.mph = int(v * 2.236025f);
    h.kmh = v * 3.6f;
    h.rpm = car_.rpm();
    h.maxRpm = car_.maxRpm();
    const int g = car_.gearIndex();
    if (g == 0) h.gear = 'R';
    else if (g == 1) h.gear = 'P';
    else if (car_.automatic()) h.gear = 'D';
    else h.gear = char('0' + (g - 1));
    h.automatic = car_.automatic();
    h.camera = cams_.name();
    h.hidden = hudHidden_ || cams_.lookingAround();
    return h;
}

}  // namespace cc
