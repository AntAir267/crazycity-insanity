#include "drive/camera_rig.h"

#include <algorithm>
#include <cmath>

#include "physics/world.h"

namespace cc {

namespace {

const vec3 kUp(0, 1, 0);

float sgnf(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }

vec3 safeNormalize(const vec3& v) {
    float l = length(v);
    return l > 0 ? v / l : v;
}

// Look-at pose from E toward Q (cameras.md, "Definitions").
CamPose lookAtPose(const vec3& E, const vec3& Q) {
    CamPose p;
    p.Z = safeNormalize(E - Q);
    p.X = safeNormalize(vec3(p.Z.z, 0.0f, -p.Z.x));
    p.Y = cross(p.Z, p.X);
    p.p = E;
    return p;
}

struct Angles {
    float a = 0, b = 0, c = 0;  // pitch, heading, roll
};

Angles anglesOf(const CamPose& p) {
    Angles r;
    float zy = std::max(-1.0f, std::min(1.0f, -p.Z.y));
    r.a = std::asin(zy);
    r.b = std::atan2(p.Z.x, p.Z.z);
    r.c = std::atan2(p.X.y, p.Y.y);
    return r;
}

void axesFrom(const Angles& g, CamPose& p) {
    const float sa = std::sin(g.a), ca = std::cos(g.a);
    const float sb = std::sin(g.b), cb = std::cos(g.b);
    const float sc = std::sin(g.c), cc = std::cos(g.c);
    p.X = vec3(cc * cb + sc * sa * sb, sc * ca, sc * sa * cb - cc * sb);
    p.Y = vec3(cc * sa * sb - sc * cb, cc * ca, cc * sa * cb + sc * sb);
    p.Z = vec3(ca * sb, -sa, ca * cb);
}

// Rodrigues rotation of v about the unit axis k by angle t (right-handed).
vec3 rotateAbout(const vec3& v, const vec3& k, float t) {
    const float c = std::cos(t), s = std::sin(t);
    return v * c + cross(k, v) * s + k * (dot(k, v) * (1.0f - c));
}

void turnPose(CamPose& p, const vec3& pivot, const vec3& axis, float t) {
    p.X = rotateAbout(p.X, axis, t);
    p.Y = rotateAbout(p.Y, axis, t);
    p.Z = rotateAbout(p.Z, axis, t);
    p.p = pivot + rotateAbout(p.p - pivot, axis, t);
}

// Approach step of u toward g (cameras.md, "Definitions").
void approach(float& u, float g, float k, float softZone, float& w, bool smoothing, float appApp, float dt) {
    const float delta = std::fabs(g - u);
    float sigma = delta;
    if (softZone != 0.0f && delta <= softZone) sigma = delta * delta / softZone;
    if (smoothing) {
        w = (sigma - w) * appApp + w;
        // A smoothed speed of the other sign stops the camera for this frame (cameras.md).
        if ((w < 0.0f && sigma > 0.0f) || (w > 0.0f && sigma < 0.0f)) w = 0.0f;
        sigma = w;
    }
    const float step = sigma * k * dt;
    if (u < g) u = std::min(g, u + step);
    else if (u > g) u = std::max(g, u - step);
}

void nearAngle(float cur, float& tgt) {
    if (cur > kPi / 2 && tgt < -kPi / 2) tgt += 2 * kPi;
    if (cur < -kPi / 2 && tgt > kPi / 2) tgt -= 2 * kPi;
}

// Step 6 of the chase camera, shared with the point-of-view cameras.
template <typename Tune>
void approachPose(CamPose& cur, const CamPose& tgt, const vec3& P0, float aimHeight, float rateXZ,
                  const Tune& t, float minDist, float maxDist, float* wPos, float* wAng, float dt) {
    vec3 pos = cur.p;
    approach(pos.x, tgt.p.x, rateXZ, t.appPosMin, wPos[0], t.appAppOn != 0, t.appApp, dt);
    approach(pos.y, tgt.p.y, t.appYPos, t.appPosMin, wPos[1], t.appAppOn != 0, t.appApp, dt);
    approach(pos.z, tgt.p.z, rateXZ, t.appPosMin, wPos[2], t.appAppOn != 0, t.appApp, dt);
    if (maxDist != 0.0f && minDist <= maxDist) {
        vec3 d = pos - P0;
        float l = length(d);
        if (l > 0.0f) {
            if (l > maxDist) {
                pos = P0 + d * (maxDist / l);
                l = maxDist;
            }
            if (l < minDist) pos = P0 + d * (minDist / l);
        }
    }
    Angles ca = anglesOf(cur), ta = anglesOf(tgt);
    nearAngle(ca.a, ta.a);
    nearAngle(ca.b, ta.b);
    nearAngle(ca.c, ta.c);
    if (t.lookAt != 0.0f) {
        Angles la = anglesOf(lookAtPose(pos, P0 + vec3(0, aimHeight, 0)));
        nearAngle(ca.a, la.a);
        nearAngle(ca.b, la.b);
        nearAngle(ca.c, la.c);
        ta.a = (1.0f - t.lookAt) * ta.a + t.lookAt * la.a;
        ta.b = (1.0f - t.lookAt) * ta.b + t.lookAt * la.b;
        ta.c = (1.0f - t.lookAt) * ta.c + t.lookAt * la.c;
    }
    const float pitchRate = t.appXRot != 0.0f ? t.appXRot : t.appRot;
    approach(ca.c, ta.c, t.appRot, t.appRotMin, wAng[2], t.appAppOn != 0, t.appApp, dt);
    approach(ca.b, ta.b, t.appRot, t.appRotMin, wAng[1], t.appAppOn != 0, t.appApp, dt);
    approach(ca.a, ta.a, pitchRate, t.appRotMin, wAng[0], t.appAppOn != 0, t.appApp, dt);
    axesFrom(ca, cur);
    cur.p = pos;
}

}  // namespace

// ---------------------------------------------------------------------------------------------

void ChaseCamera::reset() {
    cur_ = tgt_;
    fresh_ = true;
    b2_ = false;
    rev_ = false;
    rt_ = 0;
    rdir_ = 1;
}

void ChaseCamera::update(const CameraSubject& s, float dt, phys::World* world, float aspect) {
    const ChaseTune& t = t_;
    // 1.
    if (fresh_) theta_ = 0;
    // 2. Reverse swing and break test.
    const float steer = s.steer;
    if (t.reverseOn == 1) {
        if (s.reverseGear) {
            if (s.handbrake > 0.5f) a_ = 0;
            if (!rev_ && s.throttle >= 0.05f) {
                rt_ += dt;
                if (rt_ >= 2.0f) {
                    rev_ = true;
                    rdir_ = steer <= 0.1f ? 1.0f : -1.0f;
                }
            }
        } else {
            if (rev_) {
                rdir_ = steer <= 0.1f ? -1.0f : 1.0f;
                if (std::fabs(theta_) > 3.1405928f) theta_ = rdir_ * kPi;
                rev_ = false;
            }
            rt_ = 0;
        }
    } else if (t.reverseOn == -1) {
        rev_ = true;
    }
    if ((t.trackBreak == 1 && s.Y.y < 0.35f) || t.trackBreak == 2) {
        b1_ = true;
        g_ = false;
    } else {
        if (s.wheelsTouching < 3) {
            airT_ += dt;
            if (airT_ > 0.1f) {
                g_ = false;
                groundT_ = 0;
            }
        } else {
            groundT_ += dt;
            if (groundT_ > 0.1f) {
                g_ = true;
                airT_ = 0;
            }
        }
        b1_ = s.angMom2 > 2.25e6f && !g_;
    }
    // 3. Slope tilt.
    vec3 front, rear;
    int nf = 0, nr = 0;
    for (int i = 0; i < 2; ++i)
        if (s.grounded[i]) { front += s.normal[i]; ++nf; }
    for (int i = 2; i < 4; ++i)
        if (s.grounded[i]) { rear += s.normal[i]; ++nr; }
    if (nf) front = front / float(nf);
    if (nr) rear = rear / float(nr);
    vec3 n;
    int cnt = 0;
    if (dot(front, front) > 0.01f) { n += front; ++cnt; }
    if (dot(rear, rear) > 0.01f) { n += rear; ++cnt; }
    n = cnt ? n / float(cnt) : kUp;
    N_ = fresh_ ? n : N_ + (n - N_) * t.hillLerp;
    N_ = safeNormalize(N_);
    const float cosUp = dot(kUp, N_);
    const float alpha = cosUp > 0.9999999f ? 0.0f : std::acos(std::max(-1.0f, std::min(1.0f, cosUp)));
    const float wgt = std::max(0.0f, std::min(1.0f, alpha / (kPi / 4)));
    float h = wgt;
    if (alpha >= 0.01f) h = wgt * dot(safeNormalize(cross(kUp, N_)), safeNormalize(s.X));
    const float e = sgnf(h) * (1.0f - std::cos(kPi * std::min(std::fabs(h), 0.5f)));
    const float k = e >= 0.0f ? -t.hillMax * e : t.hillMin * e;

    // 4. Goal.
    const vec3 P = s.toWorld(t.trackTo);
    const vec3 delta = havePold_ ? P - Pold_ : vec3();
    Pold_ = P;
    havePold_ = true;
    vec3 G;
    if (!g_ && (b1_ || b2_)) {
        b1_ = b2_ = true;
        G = Gold_ + delta;
    } else {
        b2_ = false;
        const vec3 b = safeNormalize(vec3(s.Z.x, 0, s.Z.z));
        const vec3 r = cross(kUp, b);
        G = P + b * std::max(t.offset.z, 0.01f) + r * t.offset.x + vec3(0, t.offset.y, 0);
    }
    Gold_ = G;
    const vec3 Pp = P + vec3(0, 0.4f, 0);
    const float A = (t.offset.y - 0.8f) * t.vertOffset;
    tgt_ = lookAtPose(G, Pp);
    if (t.reverseOn == 1) {
        const float target = rev_ ? rdir_ * kPi : 0.0f;
        const float rate = (rev_ ? t.revOnApp : t.revOffApp) * dt;
        if (theta_ < target) theta_ = std::min(target, theta_ + rate);
        else if (theta_ > target) theta_ = std::max(target, theta_ - rate);
    } else if (t.reverseOn == -1) {
        theta_ = rev_ ? kPi : 0.0f;
    }
    const float phi = t.reverseOn == 0 ? k : k * (1.0f - 2.0f * std::min(std::fabs(theta_) / kPi, 1.0f));
    if (theta_ != 0.0f || phi != 0.0f) {
        vec3 axis = safeNormalize(cross(kUp, tgt_.Z));
        if (dot(axis, axis) > 0) turnPose(tgt_, Pp, axis, -phi);
        turnPose(tgt_, Pp, kUp, theta_);
    }
    tgt_ = lookAtPose(tgt_.p, Pp + vec3(0, A, 0));

    // 5. Horizontal approach rate.
    float f;
    if (t.minSpeed == t.maxSpeed || s.speed <= t.minSpeed) f = 0;
    else if (s.speed >= t.maxSpeed) f = 1;
    else f = (s.speed - t.minSpeed) / (t.maxSpeed - t.minSpeed);
    float want = t.minAppXZPos != 0.0f ? t.maxAppXZPos + (t.minAppXZPos - t.maxAppXZPos) * f : t.maxAppXZPos;
    if (t.reverseOn == 0 && s.reverseGear) want = 20.0f;
    if (a_ < want) a_ = std::min(want, a_ + t.appInc * dt);
    else if (a_ > want) a_ = std::max(want, a_ - t.appDec * dt);

    // 6. Approach.
    const float y0 = cur_.p.y;
    const vec3 P0 = s.toWorld(t.trackTo);
    P0_ = P0;
    if (t.approachOn == 0 || fresh_) {
        cur_ = tgt_;
    } else {
        approachPose(cur_, tgt_, P0, A, a_, t, t.minDist, t.maxDist, wPos_, wAng_, dt);
    }

    // 7. Floor and ceiling.
    if (t.minMaxOn && world && !fresh_) {
        phys::ProbeState ps;
        phys::ProbeHit hit;
        const float x = cur_.p.x, z = cur_.p.z;
        bool haveCeil = false, haveFloor = false;
        float ceil = 0, floor = 0;
        if (world->probe(vec3(x, y0, z), vec3(x, y0 + 5.0f, z), ps, hit) && hit.normal.y < 0.7f) {
            haveCeil = true;
            ceil = hit.point.y - 0.5f;
        }
        const float from = haveCeil ? ceil : y0;
        if (world->probe(vec3(x, from, z), vec3(x, y0 - 5.0f, z), ps, hit) && hit.normal.y > 0.7f) {
            haveFloor = true;
            floor = hit.point.y + 0.5f;
        }
        if (!(haveCeil && haveFloor && ceil < floor)) {
            if (haveCeil && cur_.p.y > ceil) cur_.p.y = ceil;
            if (haveFloor && cur_.p.y < floor) cur_.p.y = floor;
        }
    }
    // 8. Walls.
    if (t.collideType == 1 && world) {
        const vec3 off = cur_.p - P0;
        const float dist = length(off);
        if (dist > 0) {
            const vec3 d = off / dist;
            const float near = t.cameraNear;
            const float tn = std::tan(t.cameraFOV * kPi / 360.0f);
            const float hw = near * tn * aspect + 0.33f, hh = near * tn + 0.33f;
            float best = 2.0f;
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sy = -1; sy <= 1; sy += 2) {
                    const vec3 c = cur_.X * (hw * float(sx)) + cur_.Y * (hh * float(sy));
                    phys::ProbeState ps;
                    phys::ProbeHit hit;
                    if (world->probe(P0 + c, P0 + c + d * t.maxDist, ps, hit) && dot(hit.normal, d) < -0.00001f &&
                        hit.t < best)
                        best = hit.t;
                }
            if (best <= 1.0f) {
                const float R = (nearOrFar_ && s.enclosedRoom) ? 1.11f : 0.33f;
                const float L = best * t.maxDist + near - R;
                if (L < dist) cur_.p = P0 + d * std::max(L, 0.0f);
            }
        }
    }
    fresh_ = false;
}

// ---------------------------------------------------------------------------------------------

void PovCamera::update(const CameraSubject& s, float dt, int look) {
    const PovTune& t = t_;
    tgt_.X = s.X;
    tgt_.Y = s.Y;
    tgt_.Z = s.Z;
    tgt_.p = s.toWorld(t.offset);
    if (t.pitch != 0.0f) {
        const vec3 ax = safeNormalize(tgt_.X);
        tgt_.Y = rotateAbout(tgt_.Y, ax, t.pitch);
        tgt_.Z = rotateAbout(tgt_.Z, ax, t.pitch);
    }
    float frac = 0;
    const bool l = look & 1, r = look & 2, b = look & 4, f = look & 8;
    if (l && b) frac = 0.375f;
    else if (r && b) frac = 0.625f;
    else if (l && f) frac = 0.125f;
    else if (r && f) frac = 0.875f;
    else if (l) frac = 0.25f;
    else if (r) frac = 0.75f;
    else if (b) frac = 0.5f;
    const float psi = 2.0f * kPi * frac;
    if (psi != 0.0f) {
        const vec3 ax = safeNormalize(tgt_.Y);
        tgt_.X = rotateAbout(tgt_.X, ax, psi);
        tgt_.Z = rotateAbout(tgt_.Z, ax, psi);
    }
    const vec3 P0 = s.toWorld(t.trackTo);
    if (t.approachOn == 0 || fresh_) {
        cur_ = tgt_;
    } else {
        approachPose(cur_, tgt_, P0, t.lookAbove, t.appXZPos, t, t.minDist, t.maxDist, wPos_, wAng_, dt);
    }
    fresh_ = false;
}

void OrbitCamera2::update(const CameraSubject& s) {
    Angles a;
    a.a = -0.25f;
    a.b = 2.5f;
    a.c = 0;
    axesFrom(a, pose_);
    pose_.p = s.origin + pose_.Z * 10.0f + vec3(0, 2.5f, 0);
}

// ---------------------------------------------------------------------------------------------

void CameraRig::setup(const CameraFiles& files, bool indoorCapable) {
    files_ = files;
    indoorCapable_ = indoorCapable;
    near_ = std::make_unique<ChaseCamera>(files.nearChase, true);
    far_ = std::make_unique<ChaseCamera>(files.farChase, true);
    indoor_ = std::make_unique<ChaseCamera>(files.indoor, false);
    bumper_ = std::make_unique<PovCamera>(files.bumper);
    dash_ = std::make_unique<PovCamera>(files.dash);
    external_ = Near;
    resetAll();
}

// ---- Race cameras ----

void PreRaceCamera::start(const CameraSubject& s) {
    heading_ = float(std::atan2(double(s.Z.x), double(s.Z.z))) + 0.0f;
    update(s);
}

void PreRaceCamera::update(const CameraSubject& s) {
    Angles g;
    g.a = -1.1f;
    g.b = heading_;
    g.c = 0.0f;
    axesFrom(g, pose_);
    pose_.p = s.origin + pose_.Z * 22.0f + vec3(0, 2.0f, 0);
}

void PointCamera::update(const CameraSubject& s) {
    pose_ = lookAtPose(pos_, s.origin);
    const vec3 d = s.origin - pos_;
    const float q = (d.x * d.x + d.y * d.y) + d.z * d.z;
    const float m = (D_ * 0.3f) * (D_ * 0.3f);
    if (q <= m) {
        fov_ = 60.0f;
    } else {
        const float f = q < D_ * D_ ? (q - m) / (D_ * D_ - m) : 1.0f;
        fov_ = 60.0f - f * 35.0f;
    }
}

void CameraRig::startPreRace(const CameraSubject& s) {
    queue_.clear();
    postRace_ = false;
    dashAfter_ = false;
    preCam_.start(s);
    // The view to reach: the current external view (the dash goes off for the fly-in).
    View target = external_;
    if (active_ == Dash) {
        dashAfter_ = true;
        target = Bumper;
    } else if (active_ != Near && active_ != Far && active_ != Bumper) {
        target = external_;
    }
    active_ = PreRace;
    blending_ = false;
    if (target == Bumper || dashAfter_) {
        switchTo(Near, 3.5f, &s, 3);
        if (dashAfter_) {
            queue_.push_back({Bumper, 0.3f, 1});
            queue_.push_back({Dash, 0.3f, 2});
        } else {
            queue_.push_back({Bumper, 0.5f, 3});
        }
    } else {
        switchTo(target, 3.5f, &s, 3);
    }
    preRace_ = true;
}

void CameraRig::startPostRace(const CameraSubject& s, phys::World* world, float aspect, float dt) {
    queue_.clear();
    preRace_ = false;
    dashAfter_ = false;
    far_->update(s, dt, world, aspect);
    pointCam_.place(far_->frame().pose.p + vec3(0, 3.5f, 0), 25.0f);
    postUsed_ = true;
    postRace_ = true;
    if (active_ == Dash) active_ = external_;  // the dash view goes off
    switchTo(Point, 0.8f, &s, 3);
}

void CameraRig::startWater() {
    queue_.clear();
    preRace_ = false;
    dashAfter_ = false;
    pointCam_.place(shown_.pose.p + vec3(0, 9.0f, 0), postUsed_ ? 25.0f : 70.0f);
    if (active_ == Dash) active_ = external_;
    switchTo(Point, 0.8f, nullptr, 3);
}

void CameraRig::endRaceCameras() {
    queue_.clear();
    preRace_ = postRace_ = dashAfter_ = false;
    if (active_ == PreRace || active_ == Point) active_ = external_;
    blending_ = false;
}

void CameraRig::resetAll() {
    // A reset keeps the dash view (the external entry then becomes the bumper camera), never the
    // orbit view (cameras.md, "Switching views").
    if (active_ == PreRace || active_ == Point) active_ = external_;
    queue_.clear();
    preRace_ = postRace_ = false;
    if (active_ == Dash) {
        external_ = Bumper;
    } else {
        active_ = external_;
    }
    orbitFromDash_ = false;
    blending_ = false;
    near_->reset();
    far_->reset();
    indoor_->reset();
    bumper_->reset();
    dash_->reset();
}

const char* CameraRig::name() const {
    switch (active_) {
        case Near: return "near chase";
        case Bumper: return "bumper";
        case Far: return "far chase";
        case Dash: return "dash";
        case Orbit: return "orbit";
        case Indoor: return "indoor chase";
        case PreRace: return "pre-race";
        case Point: return "race point";
    }
    return "";
}

CameraFrame CameraRig::evaluate(View v) const {
    switch (v) {
        case Near: return near_->frame();
        case Far: return far_->frame();
        case Indoor: return indoor_->frame();
        case Bumper: return bumper_->frame();
        case Dash: return dash_->frame();
        case Orbit: return orbit_.frame();
        case PreRace: return preCam_.frame();
        case Point: return pointCam_.frame();
    }
    return near_->frame();
}

void CameraRig::updateView(View v, const CameraSubject& s, float dt, phys::World* world, float aspect, int look) {
    switch (v) {
        case Near: near_->update(s, dt, world, aspect); break;
        case Far: far_->update(s, dt, world, aspect); break;
        case Indoor: indoor_->update(s, dt, world, aspect); break;
        case Bumper: bumper_->update(s, dt, look); break;
        case Dash: dash_->update(s, dt, look); break;
        case Orbit: orbit_.update(s); break;
        case PreRace: preCam_.update(s); break;
        case Point: pointCam_.update(s); break;
    }
}

void CameraRig::switchTo(View v, float duration, const CameraSubject* s, int easing) {
    if (v == active_) return;
    easing_ = easing;
    switch (v) {
        case Near: near_->reset(); break;
        case Far: far_->reset(); break;
        case Indoor: indoor_->reset(); break;
        case Bumper: bumper_->reset(); break;
        case Dash: dash_->reset(); break;
        default: break;
    }
    if (duration > 0.0f) {
        blending_ = true;
        from_ = active_;
        remaining_ = duration;
        duration_ = duration;
    } else {
        blending_ = false;
    }
    active_ = v;
    (void)s;
}

void CameraRig::cycle() {
    if (active_ == Dash || active_ == Orbit) {
        orbitFromDash_ = false;
        switchTo(external_, 0.8f, nullptr);
        return;
    }
    external_ = external_ == Near ? Bumper : external_ == Bumper ? Far : Near;
    switchTo(external_, 0.8f, nullptr);
}

void CameraRig::toggleDash(bool wide) {
    if (active_ == Orbit) return;  // D is ignored while the orbit view is shown
    if (active_ == Dash) switchTo(external_, 0.8f, nullptr);
    else if (!wide) switchTo(Dash, 0.0f, nullptr);  // the wide view keeps the dash view off
}

void CameraRig::toggleOrbit() {
    if (active_ == Orbit) {
        if (orbitFromDash_) switchTo(Dash, 0.0f, nullptr);  // back to the dash view, instantly
        else switchTo(external_, 0.8f, nullptr);
        orbitFromDash_ = false;
    } else {
        orbitFromDash_ = active_ == Dash;
        switchTo(Orbit, 0.8f, nullptr);
    }
}

void CameraRig::update(const CameraSubject& s, float dt, phys::World* world, float aspect, int look,
                       bool indoorWanted) {
    look_ = (active_ == Bumper || active_ == Dash) ? look : 0;
    carOrigin_ = s.origin;
    // Indoor chase (cameras.md, "Indoor chase camera").
    if (indoorCapable_ && !preRace_ && !postRace_) {
        if (indoorWanted && (active_ == Near || active_ == Far)) switchTo(Indoor, 1.0f, &s);
        else if (!indoorWanted && active_ == Indoor) switchTo(external_, 1.0f, &s);
    }
    updateView(active_, s, dt, world, aspect, look_);
    if (blending_) {
        updateView(from_, s, dt, world, aspect, 0);
        remaining_ -= dt;
        if (remaining_ <= 0.0f) blending_ = false;
    }
    if (!blending_) {
        // The race cameras' chains (races.md, "Cameras").
        if (!queue_.empty()) {
            const Step st = queue_.front();
            queue_.erase(queue_.begin());
            switchTo(st.view, st.duration, &s, st.easing);
            if (st.view == Dash) dashAfter_ = false;
        } else if (preRace_) {
            preRace_ = false;
        }
        if (!blending_) {
            shown_ = evaluate(active_);
            return;
        }
    }
    const CameraFrame A = evaluate(from_), B = evaluate(active_);
    const float p = 1.0f - std::max(0.0f, std::min(1.0f, remaining_ / duration_));
    float e;
    if (easing_ == 1) e = 1.0f - std::cos(p * kPi * 0.5f);
    else if (easing_ == 2) e = std::sin(p * kPi * 0.5f);
    else e = (1.0f - std::cos(p * kPi)) * 0.5f;  // easing type 3
    CameraFrame out;
    out.fov = A.fov + (B.fov - A.fov) * p;
    out.near = A.near + (B.near - A.near) * p;
    const vec3 C = s.origin;
    const float dA = length(A.pose.p - C), dB = length(B.pose.p - C);
    const vec3 FA = A.pose.p - A.pose.Z * dA, FB = B.pose.p - B.pose.Z * dB;
    Angles aA = anglesOf(A.pose), aB = anglesOf(B.pose);
    auto within = [](float a, float& b) {
        if (a - b > kPi) b += 2 * kPi;
        if (a - b < -kPi) b -= 2 * kPi;
    };
    within(aA.a, aB.a);
    within(aA.b, aB.b);
    within(aA.c, aB.c);
    Angles m;
    m.a = aA.a + (aB.a - aA.a) * e;
    m.b = aA.b + (aB.b - aA.b) * e;
    m.c = aA.c + (aB.c - aA.c) * e;
    const float d = dA + (dB - dA) * e;
    const vec3 F = FA + (FB - FA) * e;
    axesFrom(m, out.pose);
    out.pose.p = F + out.pose.Z * d;
    shown_ = out;
}

}  // namespace cc
