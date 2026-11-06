// The AI driver (spec/ai/driver.md): modes, orders, locating the car, route progress, speed
// control, steering, the stuck watch and the yield marks. The planner is in driver_planner.cpp.
#include "ai/driver.h"

#include <algorithm>
#include <cmath>

#include "ai/route_search.h"
#include "ai/traffic.h"
#include "city/city_data.h"
#include "physics/rigid_body.h"

namespace cc {
namespace ai {

namespace {

// clamp(x, lo, hi): lo if not (x ≥ lo); hi if x > hi; else x (a NaN gives lo).
float dclamp(float x, float lo, float hi) {
    if (!(x >= lo)) return lo;
    if (x > hi) return hi;
    return x;
}

const char* const kKinds[] = {"vppanoz", "vpford", "vpmustang99", "vppanozgt", "vpsemi",
                              "vpcaddie", "vpbug", "vppolice", "vpbullet", "vpbus"};

// Steering angle α = atan2(g.z·X.z + g.x·X.x, −(g.z·Z.z + g.x·Z.x)) (double, not rounded).
double steerAngle(const vec3& g, const phys::M3& R) {
    const vec3& X = R.r[0];
    const vec3& Z = R.r[2];
    const float y = g.z * X.z + g.x * X.x;
    const float x = -(g.z * Z.z + g.x * Z.x);
    return std::atan2(double(y), double(x));
}

float f133(double a) { return float(a * double(1.33f)); }

}  // namespace

void Driver::create(phys::Car* car, const std::string& baseName, const vec3& boxMin, const vec3& boxMax, bool regenerate) {
    car_ = car;
    F_ = -boxMin.z;
    B_ = boxMax.z;
    Lx_ = -boxMin.x;
    Rx_ = boxMax.x;
    regenerate_ = regenerate;
    carKind_ = -1;
    for (int i = 0; i < 10; ++i)
        if (baseName == kKinds[i]) carKind_ = i;
    // Unstuck helper overrides: Rotation 0, TimeThresh 0.5, P2 = 1 (the owner may override).
    car_->setAiStuck(0.5f);
}

void PlayerTracking::frame(const RoadNetwork& net, const CityData& city, const vec3& p) {
    room = city.findRoom(p, room);
    const Component c = net.componentAt(p, room);
    if (c.kind == 1) road = c.index;
}

void PlayerTracking::worldReset(const RoadNetwork& net, const CityData& city, const vec3& startBody, const vec3& Z,
                                bool reversing) {
    room = city.findRoom(startBody, 0);
    const Component c = net.componentAt(startBody, room);
    if (c.kind == 1) road = c.index;
    else if (c.kind == 3) road = keepHeadingRoad(net, c.index, Z, reversing);
}

int Driver::currentRoad() const {
    const int j = Q_[0].slot;
    return (j >= 0 && j < 3) ? S_[j] : -1;
}

int Driver::sectionOnS0(const RoadNetwork& net) const {
    if (S_[0] < 0 || !car_) return -1;
    const int idx = net.carSection(S_[0], car_->body().p);
    return d_[0] > 0 ? idx : net.roads()[size_t(S_[0])].n - 1 - idx;
}

void Driver::reset(DriverEnv& env) {
    prevMode_ = DriverMode::None;
    mode_ = DriverMode::Drive;
    wrecked_ = false;
    routeDone_ = false;
    w_ = 1;
    lap_ = 1;
    n_ = 1;
    orders_.route.assign(1, -1);
    locKind_ = 3;
    locIndex_ = 1;
    chosen_ = -1;
    brakeC_ = throttleC_ = steerC_ = 0.0f;
    S_[0] = S_[1] = S_[2] = -1;
    for (PathRecord& r : Q_) r = PathRecord();
    rowCount_ = 0;
    rowLen_.fill(0);
    watch_ = Watch::Idle;
    watchTimer_ = 0.0f;
    wreckStart_ = env.now;
    in_ = phys::CarInputs();
}

int Driver::roadBetweenW(DriverEnv& env, int a, int b, int& d) const {
    auto entry = [&](int i) { return i >= 0 && i < n_ ? orders_.route[size_t(i)] : -1; };
    const int A = entry(a), B = entry(b);
    int flag = d > 0 ? 1 : 0;
    const int r = roadBetween(*env.net, A, B, flag);
    if (r >= 0) d = flag ? 1 : -1;
    return r;
}

void Driver::giveOrders(const DriverOrders& o, DriverEnv& env) {
    const RoadNetwork& net = *env.net;
    // 1.
    orders_ = o;
    n_ = int(o.route.size());
    lap_ = 1;
    // 2.
    const vec3 p = car_->body().p;
    T_ = p;
    REF_ = REF0_ = p;
    chosen_ = -1;
    Q_[0].slot = 0;
    Q_[0].prog = 0;
    S_[0] = S_[1] = S_[2] = -1;
    lastS0_ = -1;
    a_[0] = a_[1] = 0.0f;
    // 3. Destination place.
    {
        const int room = env.city->findRoom(o.D, 0);
        const auto& l = net.roomComponents(room);
        destKind_ = 0;
        destIndex_ = room;
        bool found = false;
        for (const Component& c : l)
            if (c.kind == 3) {
                destKind_ = 3;
                destIndex_ = c.index;
                found = true;
                break;
            }
        if (!found)
            for (const Component& c : l) {
                if (c.kind != 1 && c.kind != 2) continue;
                const int k = net.onRoad(c.index, o.D, 0.0f);
                if (k != 1 && k != 2) continue;
                const Road& r = net.roads()[size_t(c.index)];
                if (n_ == 0 || r.end[0].intersection == o.route[size_t(n_ - 1)] ||
                    r.end[1].intersection == o.route[size_t(n_ - 1)]) {
                    destKind_ = c.kind;
                    destIndex_ = c.index;
                    break;
                }
            }
    }
    // 4. Locate (basic, hint 0).
    room_ = 0;
    locate(env, false);
    // 5.
    auto routeRoads = [&](bool keepW) {
        if (n_ >= 2) {
            S_[0] = roadBetweenW(env, 0, 1, d_[0]);
            f_ = 1;
            if (!keepW) w_ = 1;
            if (n_ >= 3) {
                S_[1] = roadBetweenW(env, 1, 2, d_[1]);
                f_ = 2;
            }
            if (n_ >= 4) {
                S_[2] = roadBetweenW(env, 2, 3, d_[2]);
                f_ = 3;
            }
        }
    };
    if (locKind_ == 1 || locKind_ == 2) {
        w_ = 0;
        S_[0] = locIndex_;
        d_[0] = 1;
        f_ = 0;
        routeRoads(false);
        mode_ = DriverMode::Drive;
    } else if (locKind_ == 3) {
        w_ = 0;
        f_ = 0;
        const Intersection& I = net.intersections()[size_t(locIndex_)];
        if (!I.roads.empty()) {
            S_[0] = I.roads.front();
            d_[0] = net.roads()[size_t(S_[0])].end[0].intersection == locIndex_ ? 1 : -1;
        }
        routeRoads(false);
        mode_ = DriverMode::Drive;
    } else {
        w_ = 0;
        f_ = 0;
        routeRoads(true);
        if (mode_ != DriverMode::TurnBack) mode_ = DriverMode::Recover;
    }
    // 6. Start lateral offset.
    lambda0_ = 0.0f;
    if (S_[0] >= 0) {
        const Road& r = net.roads()[size_t(S_[0])];
        int v;
        if (locKind_ == 1 || locKind_ == 2) {
            const int k = std::clamp(net.sectionOfPoint(S_[0], p, d_[0]), 0, r.n - 1);
            v = d_[0] > 0 ? k : r.n - 1 - k;
        } else {
            v = d_[0] > 0 ? 0 : r.n - 1;
        }
        const vec3& V = r.C[size_t(v)];
        const vec3& X = r.X[size_t(v)];
        if (d_[0] > 0) lambda0_ = ((V.z - p.z) * X.z) + ((V.x - p.x) * X.x);
        else lambda0_ = ((-X.x) * (V.x - p.x)) + ((-X.z) * (V.z - p.z));
    }
}

void Driver::locate(DriverEnv& env, bool preferred) {
    const RoadNetwork& net = *env.net;
    const vec3 p = car_->body().p;
    room_ = env.city->findRoom(p, room_);
    const auto& l = net.roomComponents(room_);
    if (preferred) {
        int pref = -1;
        if (w_ < n_) {
            int d = 1;
            pref = roadBetweenW(env, w_ == 0 ? n_ - 1 : w_ - 1, w_, d);
        } else if (destKind_ == 1 || destKind_ == 2) {
            pref = destIndex_;
        }
        if (pref >= 0) {
            for (const Component& c : l)
                if (c.kind == 1 && c.index == pref) {
                    locKind_ = 1;
                    locIndex_ = pref;
                    return;
                }
            for (const Component& c : l)
                if (c.kind == 2 && c.index == pref) {
                    locKind_ = 2;
                    locIndex_ = pref;
                    return;
                }
        }
        for (const Component& c : l)
            if (c.kind == 3) {
                locKind_ = 3;
                locIndex_ = c.index;
                return;
            }
        for (const Component& c : l)
            if ((c.kind == 1 || c.kind == 2) && net.onRoad(c.index, p, 0.0f) < 3) {
                locKind_ = c.kind;
                locIndex_ = c.index;
                return;
            }
        locKind_ = 0;  // the index is kept
        return;
    }
    for (const Component& c : l)
        if (c.kind == 1 || c.kind == 3) {
            locKind_ = c.kind;
            locIndex_ = c.index;
            return;
        }
    for (const Component& c : l)
        if (c.kind == 2 && net.onRoad(c.index, p, 0.0f) < 3) {
            locKind_ = 2;
            locIndex_ = c.index;
            return;
        }
    locKind_ = 0;
    locIndex_ = room_;
}

void Driver::clearPaths() {
    chosen_ = -1;
    rowCount_ = 0;
    rowLen_.fill(0);
    rowBlocked_.fill(false);
    rowZone2_.fill(false);
}

void Driver::frame(float F, DriverEnv& env) {
    car_->setEngineScale(orders_.kthr);
    // 2. Wrecked.
    if (car_->wrecked()) {
        if (regenerate_ && wrecked_ && (env.now - wreckStart_) > 5.0) {
            wrecked_ = false;
            car_->clearDamage();
            return;
        }
        if (!wrecked_) {
            wreckStart_ = env.now;
            wrecked_ = true;
        }
        in_.steer = in_.throttle = in_.brake = 0.0f;
        phys::RigidBody& b = car_->body();
        b.P = vec3(b.P.x * 0.94999999f, b.P.y * 0.94999999f, b.P.z * 0.94999999f);
        return;
    }
    // 3. Mode.
    switch (mode_) {
        case DriverMode::Drive:
            if (prevMode_ != mode_) {
                driveEntry();
                prevMode_ = mode_;
            }
            driveUpdate(F, env);
            break;
        case DriverMode::TurnBack:
            if (prevMode_ != mode_) {
                turnBackEntry();
                prevMode_ = mode_;
            }
            turnBackUpdate();
            break;
        case DriverMode::Recover:
            if (prevMode_ != mode_) {
                recoverEntry();
                prevMode_ = mode_;
            }
            recoverUpdate(F, env);
            break;
        case DriverMode::Hold: holdUpdate(); break;
        default: break;
    }
}

void Driver::mirror(const vec3& target, float steer, float throttle, float brake, bool scaleL) {
    T_ = target;
    steerC_ = steer;
    throttleC_ = throttle;
    brakeC_ = brake;
    in_.steer = steer;
    in_.throttle = throttle;
    in_.brake = brake;
    if (scaleL) {
        phys::RigidBody& b = car_->body();
        b.L = vec3(b.L.x * 0.85000002f, b.L.y * 0.85000002f, b.L.z * 0.85000002f);
    }
}

// ---- DRIVE ----

void Driver::driveEntry() {
    steerC_ = 0.0f;
    in_.steer = in_.throttle = in_.brake = 0.0f;
    watch_ = Watch::Idle;
    car_->deactivateUnstuck();
    T_ = car_->body().p;
    clearPaths();
    if (car_->gearIndex() == 0) car_->writeGearDirect(2);
}

void Driver::driveUpdate(float F, DriverEnv& env) {
    const vec3 p = car_->body().p;
    // 1. Drivable: always on for the police.
    // 2.
    stuckWatch(F);
    // 3.
    if (car_->unstuckState() == phys::Car::UnstuckTurn) {
        routeProgress(env);  // with the last lookup
        mode_ = DriverMode::TurnBack;
        car_->body().P = vec3();
        car_->body().L = vec3();
        return;
    }
    // 4.
    if (watch_ == Watch::Stuck) {
        in_.steer = 1.0f;
        in_.brake = 0.0f;
        in_.throttle = 1.0f;
        car_->setUnstuckInactiveOnly();
        return;
    }
    // 5. Locate with a preferred road.
    locate(env, true);
    // 6. Route step.
    routeDone_ = w_ >= n_;
    routeProgress(env);
    if (S_[0] != lastS0_) {
        REF_ = REF0_ = p;
        lastS0_ = S_[0];
        junctionAngles(env);
        junctionTurns(env);
    }
    plan(env);
    const int c = pathCount();
    const PathRecord& P1 = pathPoint(1);
    T_ = P1.pos;
    (void)c;
    speedControl(env);
    // 7. Destination behind.
    const phys::M3& R = car_->rotation();
    const vec3& Z = R.r[2];
    const vec3& D = orders_.D;
    const vec3& Dd = orders_.Dd;
    vec3 g;
    const float ddx = p.x - D.x, ddz = p.z - D.z;
    if (P1.dist == 9999.0f && ddx * ddx + ddz * ddz < 625.0f && (-Z.z) * Dd.z + (-Z.x) * Dd.x > 0.0f &&
        (D.z - p.z) * (-Z.z) + (D.x - p.x) * (-Z.x) < 0.0f) {
        brakeC_ = 1.0f;
        throttleC_ = 0.0f;
        g = vec3(-(T_.x - p.x), -(T_.y - p.y), -(T_.z - p.z));
    } else {
        g = T_ - p;
    }
    // 8. Steering.
    const double alpha = steerAngle(g, R);
    const float s = float(double(f133(alpha)) * double(1.428f));
    steerC_ = dclamp(s, -1.0f, 1.0f);
    // 9-10.
    const float hb = (car_->speed() > 30.0f && (s < -1.0f || s > 1.0f)) ? 1.0f : 0.0f;
    in_.steer = steerC_;
    in_.brake = brakeC_;
    in_.throttle = throttleC_;
    in_.handbrake = hb;
    // 11.
    car_->setCarFrictionHandling(touchedPlayer_ ? 2.0f : 1.0f);
}

// ---- Route progress ----

void Driver::routeProgress(DriverEnv& env) {
    const RoadNetwork& net = *env.net;
    if (w_ < n_ && locKind_ == 3 && locIndex_ == orders_.route[size_t(w_)]) {
        if (w_ > 0) {
            int d = 1;
            const int R = roadBetweenW(env, w_ - 1, w_, d);
            if (R != S_[0] && R >= 0) enterRoad(R, env);
        }
        ++w_;
        if (w_ == n_ && (lap_ < orders_.laps || orders_.laps == -1)) {
            w_ = 1;
            ++lap_;
        }
    }
    if (locKind_ == 1 || locKind_ == 2) {
        enterRoad(locIndex_, env);
    } else if (locKind_ == 3 && f_ < 3 && destKind_ == 1) {
        const Intersection& I = net.intersections()[size_t(locIndex_)];
        for (int q : I.roads)
            if (q == destIndex_) {
                S_[f_] = q;
                d_[f_] = net.roads()[size_t(q)].end[1].intersection == locIndex_ ? 1 : -1;
                break;
            }
    }
}

void Driver::enterRoad(int R, DriverEnv& env) {
    const RoadNetwork& net = *env.net;
    const Road& r = net.roads()[size_t(R)];
    const vec3 p = car_->body().p;
    // 1. Route finished.
    if (w_ >= n_) {
        S_[0] = R;
        S_[1] = S_[2] = -1;
        const int i = std::clamp(net.sectionOfPoint(R, p, 1), 0, r.n - 1);
        const vec3& Z = car_->rotation().r[2];
        d_[0] = (r.Z[size_t(i)].x * Z.x + Z.z * r.Z[size_t(i)].z >= 0.0f) ? 1 : -1;
        return;
    }
    auto W = [&](int i) { return i >= 0 && i < n_ ? orders_.route[size_t(i)] : -1; };
    // 2.
    if (r.end[0].intersection == W(w_ + 1) || r.end[1].intersection == W(w_ + 1) ||
        (w_ == n_ - 1 && destIndex_ == locIndex_))
        ++w_;
    // 3.
    const int I = W(w_);
    if (r.end[0].intersection == I) {
        S_[0] = R;
        d_[0] = 1;
        f_ = 0;
        fillWindow(env);
    } else if (r.end[1].intersection == I) {
        S_[0] = R;
        d_[0] = -1;
        f_ = 0;
        fillWindow(env);
    } else {
        bool matched = false;
        for (int e = 0; e < 2 && !matched; ++e) {
            const Intersection& X = net.intersections()[size_t(r.end[e].intersection)];
            for (int q : X.roads) {
                const Road& Q = net.roads()[size_t(q)];
                if (Q.end[0].intersection == I || Q.end[1].intersection == I) {
                    S_[0] = R;
                    d_[0] = e == 0 ? 1 : -1;
                    S_[1] = q;
                    d_[1] = Q.end[0].intersection == I ? 1 : -1;
                    matched = true;
                    break;
                }
            }
        }
        if (matched) {
            if (w_ + 1 >= n_) S_[2] = -1;
            else S_[2] = roadBetweenW(env, w_, w_ + 1, d_[2]);
        }
    }
}

void Driver::fillWindow(DriverEnv& env) {
    const RoadNetwork& net = *env.net;
    const int k = w_;
    const bool finalLap = lap_ >= orders_.laps && orders_.laps != -1;
    int A, B, C;
    if (k == n_ - 1 && !finalLap) {
        A = k, B = 1, C = 2;
        f_ = 3;
    } else if (k == n_ - 1) {
        A = k, B = -1, C = -1;
        f_ = 1;
    } else if (k == n_ - 2 && finalLap) {
        A = k, B = k + 1, C = -1;
        f_ = 2;
    } else if (k == n_ - 2) {
        A = k, B = k + 1, C = 1;
        f_ = 3;
    } else {
        A = k, B = k + 1, C = k + 2;
        f_ = 3;
    }
    S_[1] = roadBetweenW(env, A, B, d_[1]);
    S_[2] = roadBetweenW(env, B, C, d_[2]);
    if ((destKind_ == 1 || destKind_ == 2) && f_ >= 1 && f_ <= 2 && n_ >= 1) {
        const int last = orders_.route[size_t(n_ - 1)];
        if (last >= 0 && size_t(last) < net.intersections().size())
            for (int q : net.intersections()[size_t(last)].roads)
                if (q == destIndex_) {
                    S_[f_] = q;
                    d_[f_] = net.roads()[size_t(q)].end[1].intersection == last ? 1 : -1;
                    break;
                }
    }
}

// ---- Junction angles ----

void Driver::junctionAngles(DriverEnv& env) {
    const RoadNetwork& net = *env.net;
    for (int k = 0; k < 2; ++k) {
        const int A = S_[k], B = S_[k + 1];
        if (A < 0 || B < 0) {
            a_[k] = 0.0f;
            tau_[k] = 0.0f;
            continue;
        }
        const Road& ra = net.roads()[size_t(A)];
        const Road& rb = net.roads()[size_t(B)];
        vec3 u;
        if (d_[k + 1] > 0) u = vec3(rb.C[1].x - rb.C[0].x, 0, rb.C[1].z - rb.C[0].z);
        else
            u = vec3(rb.C[size_t(rb.n - 2)].x - rb.C[size_t(rb.n - 1)].x, 0,
                     rb.C[size_t(rb.n - 2)].z - rb.C[size_t(rb.n - 1)].z);
        float y, x;
        if (d_[k] > 0) {
            const int e = ra.n - 1;
            y = u.z * (-ra.X[size_t(e)].z) + u.x * (-ra.X[size_t(e)].x);
            x = (-ra.Z[size_t(e)].z) * u.z + (-ra.Z[size_t(e)].x) * u.x;
        } else {
            y = u.z * ra.X[0].z + u.x * ra.X[0].x;
            x = u.z * ra.Z[0].z + u.x * ra.Z[0].x;
        }
        const double at = std::atan2(double(y), double(x));
        a_[k] = float(at);
        if (at >= double(0.7f)) tau_[k] = 1.0f;
        else if (at <= double(-0.7f) || std::isnan(at)) tau_[k] = -1.0f;
        else tau_[k] = 0.0f;
    }
}

// ---- Speed control ----

void Driver::brakeWith(float b) {
    brakeC_ = dclamp(b, 0.0f, 1.0f);
    throttleC_ = 0.0f;
    phys::RigidBody& body = car_->body();
    body.L = vec3(body.L.x * 0.85000002f, body.L.y * 0.85000002f, body.L.z * 0.85000002f);
}

namespace {
double vectorAngle(const vec3& a, const vec3& b) {
    const float A = (a.x * a.x + a.y * a.y) + a.z * a.z;
    const float B = (b.x * b.x + b.y * b.y) + b.z * b.z;
    const float k = 1.0f / float(std::sqrt(double(B * A)));
    if (k == 0.0f) return 0.0;
    const float c = float(((b.x * a.x + b.z * a.z) + b.y * a.y) * k);
    if (c > 0.99999988f) return 0.0;
    if (c < -1.0f) return 3.1415927;
    return std::acos(double(c));
}
}  // namespace

void Driver::speedControl(DriverEnv& env) {
    const RoadNetwork& net = *env.net;
    const float v = car_->speed();
    const vec3 p = car_->body().p;
    auto need = [&](float vmax, float e) { return (v - vmax) / (((e / v) * 1.2f) * 19.8f); };
    const int c = pathCount();
    const PathRecord& P0 = pathPoint(0);
    const PathRecord& P1 = pathPoint(1);
    const PathRecord& P2 = pathPoint(2);
    const float kc = orders_.kc, kb = orders_.kb;
    // 1. Corner of the path.
    if (c > 2) {
        const double th = vectorAngle(P1.pos - P0.pos, P2.pos - P1.pos);
        if (th > double(0.7f)) {
            const float hh = float(double(3.14f) - std::fabs(th));
            const float half = hh * 0.5f;
            const float vmax = float(std::sqrt(double(float(float(float(std::tan(double(half)) * 10.0) * 1.2f) * 19.8f)))) * kc;
            const float b = v > vmax ? need(vmax, P1.dist) : 0.0f;
            if (b > kb) {
                brakeWith(b);
                return;
            }
        }
    }
    // 2.
    brakeC_ = 0.0f;
    throttleC_ = orders_.kthr;
    const float dx = p.x - orders_.D.x, dz = p.z - orders_.D.z;
    const float q = dx * dx + dz * dz;
    if ((q < 5000.0f || std::isnan(q)) && w_ >= n_ && lap_ >= orders_.laps) {
        // 3. Destination approach.
        const float e = float(std::sqrt(double(q))) - orders_.s2;
        const float b = v > orders_.s1 ? need(orders_.s1, e) : 0.0f;
        if (float(double(e) * 0.014) < b) brakeWith(b);
        if (e < 2.5f && orders_.s1 <= 0.0f) {
            brakeC_ = 1.0f;
            throttleC_ = 0.0f;
        }
        return;
    }
    // 4. Sharp turns.
    const int k0 = P0.arc;
    bool braked = false;
    if (k0 < 3) {
        for (int k = std::max(k0, 0); k <= 2 && !braked; ++k) {
            if (S_[k] < 0) continue;
            const Road& R = net.roads()[size_t(S_[k])];
            const int m = int(R.bends.size());
            for (int i = 0; i < m; ++i) {
                const int s = R.bends[size_t(i)].v;
                const Bend& o = d_[k] > 0 ? R.bends[size_t(i)] : R.bends[size_t(m - 1 - i)];
                const float vmax = float(std::sqrt(double(float(float(o.rho * 1.2f) * 19.8f)))) * kc;
                const float cx = P0.pos.x - o.K.x, cz = P0.pos.z - o.K.z;
                float e;
                if (d_[k] > 0) {
                    e = float(cz * R.Z[size_t(s)].z + cx * R.Z[size_t(s)].x) - o.dt;
                } else {
                    int j = 1;
                    if (s + 2 < R.n) {
                        const float gx = R.C[size_t(s + 1)].x - R.C[size_t(s)].x;
                        const float gz = R.C[size_t(s + 1)].z - R.C[size_t(s)].z;
                        const float yy = (-R.X[size_t(s)].x) * gx + (-R.X[size_t(s)].z) * gz;
                        const float xx = (-R.Z[size_t(s)].x) * gx + (-R.Z[size_t(s)].z) * gz;
                        if (float(std::atan2(double(yy), double(xx))) != o.A) j = 2;
                    }
                    const int sj = std::min(s + j, R.n - 1);
                    const vec3& Zp = R.Z[size_t(sj)];
                    e = float((-Zp.x) * cx + (-Zp.z) * cz) - o.dt;
                }
                const float b = v > vmax ? need(vmax, e) : 0.0f;
                if (b > kb) {
                    brakeWith(b);
                    braked = true;
                    break;
                }
            }
        }
    }
    // 5. Junctions.
    auto alley = [&](int r) { return r >= 0 && (net.roads()[size_t(r)].flags & 0x02) != 0; };
    const float jx0 = p.x - J_[0].x, jz0 = p.z - J_[0].z;
    const float pad0 = tj_[0] + orders_.kp;
    const bool near0 = S_[0] >= 0 && !(-0.7f <= a_[0] && a_[0] <= 0.7f) && (jx0 * jx0 + jz0 * jz0) < pad0 * pad0;
    if (near0) {
        float vmax = float(std::sqrt(double(float(float(1.2f * rj_[0]) * 19.8f)))) * kc;
        if (S_[1] >= 0 && alley(S_[1])) vmax = vmax * 0.5f;
        float b = 0.0f;
        if (v > vmax) {
            const Road& R0 = net.roads()[size_t(S_[0])];
            const float cx = P0.pos.x - J_[0].x, cz = P0.pos.z - J_[0].z;
            float e;
            if (d_[0] > 0) {
                const vec3& Zn = R0.Z[size_t(R0.n - 1)];
                e = float(cz * Zn.z + cx * Zn.x) - tj_[0];
            } else {
                e = float((-R0.Z[0].z) * cz + (-R0.Z[0].x) * cx) - tj_[0];
            }
            b = need(vmax, e);
        }
        if (b > kb && b > brakeC_) brakeWith(b);
        return;
    }
    const float jx1 = p.x - J_[1].x, jz1 = p.z - J_[1].z;
    const float pad1 = tj_[1] + orders_.kp;
    const bool near1 = S_[1] >= 0 && !(-0.7f <= a_[1] && a_[1] <= 0.7f) && (jx1 * jx1 + jz1 * jz1) < pad1 * pad1;
    if (near1) {
        float vmax = float(std::sqrt(double(float(float(1.2f * rj_[1]) * 19.8f)))) * kc;
        if (S_[2] >= 0 && alley(S_[2])) vmax = vmax * 0.5f;
        float b = 0.0f;
        if (v > vmax) {
            const float ex = P0.pos.x - J_[1].x, ez = P0.pos.z - J_[1].z;
            const float e = float(std::sqrt(double(ex * ex + ez * ez))) - tj_[1];
            b = need(vmax, e);
        }
        if (b > kb && b > brakeC_) brakeWith(b);
    }
}

// ---- TURN_BACK ----

void Driver::turnBackEntry() {
    turnBackCounter_ = 0;
    T_ = pathPoint(1).pos;
    if (car_->gearIndex() != 0) car_->selectGear(0);
}

void Driver::turnBackUpdate() {
    const vec3 p = car_->body().p;
    const phys::M3& R = car_->rotation();
    const vec3& X = R.r[0];
    const vec3& Y = R.r[1];
    const vec3& Z = R.r[2];
    const vec3 g = T_ - p;
    const float y = ((g.z * X.z) + (g.y * X.y)) + (g.x * X.x);
    const float x = (((-Z.z) * g.z) + ((-Z.y) * g.y)) + ((-Z.x) * g.x);
    const float a = float(std::atan2(double(y), double(x)));
    if (-0.1f <= a && a <= 0.1f) {
        in_.steer = 0.0f;
        // Turn the body by α about its own Y row: R := R·M(Y, α).
        if (a != 0.0f) {
            phys::Mat3 M;
            const int zeros = (Y.x == 0.0f) + (Y.y == 0.0f) + (Y.z == 0.0f);
            if (zeros >= 2) {
                // A coordinate rotation by ±α (an all-zero axis counts as the Z case).
                int ax = 2;
                float comp = Y.z;
                if (Y.x != 0.0f) ax = 0, comp = Y.x;
                else if (Y.y != 0.0f) ax = 1, comp = Y.y;
                const float ang = comp > 0.0f ? a : -a;
                const float c = phys::fcos(ang), s = phys::fsin(ang);
                M = phys::Mat3();
                if (ax == 0) {
                    M.m[0][0] = 1.0f;
                    M.m[1][1] = c;
                    M.m[1][2] = s;
                    M.m[2][1] = -s;
                    M.m[2][2] = c;
                } else if (ax == 1) {
                    M.m[0][0] = c;
                    M.m[0][2] = -s;
                    M.m[1][1] = 1.0f;
                    M.m[2][0] = s;
                    M.m[2][2] = c;
                } else {
                    M.m[0][0] = c;
                    M.m[0][1] = s;
                    M.m[1][0] = -s;
                    M.m[1][1] = c;
                    M.m[2][2] = 1.0f;
                }
            } else {
                const float l2 = (Y.z * Y.z + Y.y * Y.y) + Y.x * Y.x;
                const float k = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
                M = phys::rotAboutAxis(vec3(Y.x * k, Y.y * k, Y.z * k), a);
            }
            car_->body().R = phys::fromMat3(phys::mulProducts(phys::toMat3(car_->body().R), M));
        }
        turnBackFinish();
        return;
    }
    in_.steer = dclamp(a * -2.85714293f, -1.0f, 1.0f);
    if (turnBackCounter_ > 65) {
        turnBackFinish();
        return;
    }
    in_.throttle = 0.85000002f;
    in_.brake = 0.0f;
    ++turnBackCounter_;
}

void Driver::turnBackFinish() {
    mode_ = S_[0] >= 0 ? DriverMode::Drive : DriverMode::Recover;
    car_->deactivateUnstuck();
    phys::RigidBody& b = car_->body();
    b.P = vec3(b.P.x * 0.25f, b.P.y * 0.25f, b.P.z * 0.25f);
    b.L = vec3(b.L.x * 0.25f, b.L.y * 0.25f, b.L.z * 0.25f);
    in_.throttle = 0.0f;
    in_.brake = 1.0f;
}

// ---- RECOVER ----

void Driver::recoverEntry() {
    clearPaths();
    watch_ = Watch::Idle;
    car_->deactivateUnstuck();
    if (car_->gearIndex() == 0) car_->writeGearDirect(2);
}

void Driver::recoverUpdate(float F, DriverEnv& env) {
    const vec3 p = car_->body().p;
    // 2.
    stuckWatch(F);
    if (car_->unstuckState() == phys::Car::UnstuckTurn) {
        routeProgress(env);
        mode_ = DriverMode::TurnBack;
        car_->body().P = vec3();
        car_->body().L = vec3();
        return;
    }
    if (watch_ == Watch::Stuck) {
        in_.steer = 1.0f;
        in_.brake = 0.0f;
        in_.throttle = 1.0f;
        car_->setUnstuckInactiveOnly();
        return;
    }
    // 3.
    locate(env, false);
    // 4. Target.
    if (w_ < n_) {
        const int Wi = orders_.route[size_t(w_)];
        if (Wi >= 0 && size_t(Wi) < env.net->intersections().size()) T_ = env.net->intersections()[size_t(Wi)].centre;
        if (locKind_ == 3 && locIndex_ == Wi) {
            S_[0] = roadBetweenW(env, w_, w_ + 1, d_[0]);
            S_[1] = roadBetweenW(env, w_ + 1, w_ + 2, d_[1]);
            S_[2] = roadBetweenW(env, w_ + 2, w_ + 3, d_[2]);
            ++w_;
            junctionAngles(env);
            mode_ = DriverMode::Drive;
            if (w_ == n_ && (lap_ < orders_.laps || orders_.laps == -1)) {
                w_ = 0;
                ++lap_;
            }
        }
    } else {
        const vec3& D = orders_.D;
        if (orders_.s2 == 0.0f) {
            T_ = D;
        } else {
            const float e = std::sqrt((p.z - D.z) * (p.z - D.z) + (p.x - D.x) * (p.x - D.x));
            if (e >= orders_.s2) {
                const float h = (e - orders_.s2) / e;
                T_ = vec3((D.x - p.x) * h + p.x, (D.y - p.y) * h + p.y, (D.z - p.z) * h + p.z);
            } else {
                T_ = p;
            }
        }
    }
    chosen_ = 0;
    rowLen_[0] = 0;
    Q_[size_t(rowRecord(0, 0))].arc = 0;
    Q_[size_t(rowRecord(0, 0))].slot = 0;
    T_.y = T_.y + 1.0f;
    speedControl(env);
    // 5. Steering.
    const double alpha = steerAngle(T_ - p, car_->rotation());
    steerC_ = dclamp(f133(alpha), -0.75f, 0.75f);
    in_.steer = steerC_;
    in_.brake = brakeC_;
    in_.throttle = throttleC_;
}

// ---- HOLD ----

void Driver::holdUpdate() {
    const vec3 p = car_->body().p;
    T_ = orders_.D;
    const double alpha = steerAngle(orders_.D - p, car_->rotation());
    steerC_ = dclamp(f133(alpha), -1.0f, 1.0f);
    throttleC_ = 0.0f;
    brakeC_ = 1.0f;
    in_.steer = steerC_;
    in_.throttle = 0.0f;
    in_.brake = 1.0f;
    rowCount_ = 0;
    rowLen_[0] = 0;
}

// ---- Stuck watch ----

void Driver::stuckWatch(float F) {
    const vec3 p = car_->body().p;
    auto e3 = [&]() {
        const vec3 e = watchStart_ - p;
        return ((e.z * e.z) + (e.y * e.y)) + (e.x * e.x);
    };
    const float scale = orders_.kthr;
    const bool throttleWants = (0.75f * scale) < in_.throttle;
    const bool wants = throttleWants && std::fabs(in_.steer) < 0.5f;
    // 1.
    if (e3() > 0.36000001f) watch_ = Watch::Idle;
    // 2.
    if (car_->unstuckState() == phys::Car::UnstuckWatch && watch_ == Watch::Idle) {
        watchTimer_ = 0.0f;
        watchStart_ = p;
        watch_ = Watch::Watch;
    }
    if (watch_ == Watch::Watch) {
        watchTimer_ = F + watchTimer_;
        const vec3 e = watchStart_ - p;
        if (e.x * e.x + e.z * e.z < 0.36000001f && watchTimer_ >= 0.30000001f && wants) {
            watch_ = Watch::Stuck;
            return;
        }
        if (e3() > 1.0f) watch_ = Watch::Idle;
    } else if (watch_ == Watch::Stuck) {
        if (e3() > 1.0f) {
            watch_ = Watch::Idle;
        } else if (throttleWants) {
            const float sg = in_.steer > 0.0f ? 1.0f : (in_.steer < 0.0f || std::isnan(in_.steer) ? -1.0f : 0.0f);
            const float ang = -float(float(sg * 1.0f) * F);
            const float c = phys::fcos(ang), sn = phys::fsin(ang);
            phys::Mat3 r;
            r.m[0][0] = c;
            r.m[0][2] = -sn;
            r.m[1][1] = 1.0f;
            r.m[2][0] = sn;
            r.m[2][2] = c;
            car_->body().R = phys::fromMat3(phys::mulProducts(phys::toMat3(car_->body().R), r));
        } else {
            watch_ = Watch::Idle;
        }
    }
}

// ---- Yield marks ----

void Driver::yieldMarks(RoadNetwork& net, bool set) const {
    for (int k = 0; k < 3; ++k) {
        if (S_[k] < 0) continue;
        const Road& r = net.roads()[size_t(S_[k])];
        const int I = d_[k] > 0 ? r.end[0].intersection : r.end[1].intersection;
        if (I < 0 || size_t(I) >= net.intersections().size()) continue;
        for (int q : net.intersections()[size_t(I)].roads) {
            Road& Q = net.roads()[size_t(q)];
            const RoadEnd& e = Q.end[0].intersection == I ? Q.end[0] : Q.end[1];
            if (e.control == 0 || e.control == 1) Q.held = set;
        }
    }
}

// ---- Planner outputs ----

int Driver::pathCount() const { return chosen_ < 0 ? 0 : rowLen_[size_t(chosen_)]; }

const PathRecord& Driver::pathPoint(int i) const {
    const int k = chosen_ < 0 ? i : rowRecord(chosen_, i);
    return Q_[size_t(std::clamp(k, 0, kRecords - 1))];
}

}  // namespace ai
}  // namespace cc
