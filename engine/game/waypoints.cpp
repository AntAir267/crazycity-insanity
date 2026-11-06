#include "game/waypoints.h"

#include <cmath>

namespace cc {
namespace game {

namespace {

// min(u, v) = u if u < v or unordered, else v; max(u, v) = u if u > v, else v.
float wmin(float u, float v) { return (u < v || std::isnan(u) || std::isnan(v)) ? u : v; }
float wmax(float u, float v) { return u > v ? u : v; }

bool zeroOrNan(float d) { return d == 0.0f || std::isnan(d); }

}  // namespace

bool segmentTest(vec2 p1, vec2 p2, vec2 q1, vec2 q2, float t) {
    const float loPx = wmin(p1.x, p2.x) - t, hiPx = wmax(p1.x, p2.x) + t;
    const float loPz = wmin(p1.y, p2.y) - t, hiPz = wmax(p1.y, p2.y) + t;
    const float loQx = wmin(q1.x, q2.x) - t, hiQx = wmax(q1.x, q2.x) + t;
    const float loQz = wmin(q1.y, q2.y) - t, hiQz = wmax(q1.y, q2.y) + t;
    const float dp = p1.x - p2.x, dq = q1.x - q2.x;
    const float mp = zeroOrNan(dp) ? 0.0f : (p1.y - p2.y) / dp;
    const float mq = zeroOrNan(dq) ? 0.0f : (q1.y - q2.y) / dq;
    const float bp = p1.y - mp * p1.x;
    const float bq = q1.y - mq * q1.x;
    float X, Z;
    if (zeroOrNan(dp)) {
        X = p1.x;
        Z = mq * p1.x + bq;
    } else if (zeroOrNan(dq)) {
        X = q1.x;
        Z = mp * q1.x + bp;
    } else {
        X = (bq - bp) / (mp - mq);
        Z = X * mp + bp;
    }
    return loPx <= X && !(X > hiPx) && loPz <= Z && !(Z > hiPz) && loQx <= X && !(X > hiQx) && loQz <= Z &&
           !(Z > hiQz);
}

namespace {
// (P.x + k × V.x, P.z + k × V.z)
vec2 along(const vec3& P, float k, const vec3& V) { return vec2{P.x + k * V.x, P.z + k * V.z}; }
}  // namespace

bool playerCrossesGate(const GatePose& c, const vec3& box, const Waypoint& w) {
    const float hx = box.x * 0.5f, hy = box.y, hz = box.z * 0.5f;
    const vec2 F = along(c.P, -hz, c.Z);
    const float back = hz + 2.0f;
    const vec2 R = along(c.P, back, c.Z);
    if (segmentTest(R, F, w.A, w.B, hx)) return true;
    if (segmentTest(along(c.P, -hy, c.Y), along(c.P, hy, c.Y), w.A, w.B, hx)) return true;
    return segmentTest(along(c.P, -hx, c.X), along(c.P, hx, c.X), w.A, w.B, 0.0f);
}

bool opponentCrossesGate(const GatePose& c, const vec3& box, const Waypoint& w) {
    const float sx = box.x, sy = box.y, sz = box.z;
    const float tol = sx * 5.0f;
    if (segmentTest(along(c.P, -sz, c.Z), along(c.P, sz, c.Z), w.A, w.B, tol)) return true;
    const float uy = sy * 5.0f;
    if (segmentTest(along(c.P, -uy, c.Y), along(c.P, uy, c.Y), w.A, w.B, tol)) return true;
    const float ux = sx * 5.0f;
    return segmentTest(along(c.P, -ux, c.X), along(c.P, ux, c.X), w.A, w.B, 0.0f);
}

bool sphereGateHit(const vec3& P, const Waypoint& w) {
    const float dx = P.x - w.pos.x, dy = P.y - w.pos.y, dz = P.z - w.pos.z;
    return ((dz * dz + dy * dy) + dx * dx) < w.radius * w.radius;
}

// ---- The list ----

void WaypointList::set(const std::vector<Waypoint>& points, Layout layout, int laps, bool onlyTarget) {
    pts_ = points;
    n_ = int(pts_.size());
    passed_.assign(pts_.size(), 0);
    shown_.assign(pts_.size(), 0);
    layout_ = layout;
    laps_ = laps;
    onlyTarget_ = onlyTarget;
    disabled_ = false;
    reset();
}

void WaypointList::clear() {
    pts_.clear();
    passed_.clear();
    shown_.clear();
    n_ = 0;
    done_ = off_ = false;
    count_ = 1;
    target_ = 1;
    last_ = 0;
}

void WaypointList::reset() {
    off_ = done_ = false;
    target_ = 1;
    last_ = 0;
    count_ = 1;
    for (int i = 0; i < n_; ++i) passed_[size_t(i)] = 0;
    for (int i = 0; i < n_; ++i) shown_[size_t(i)] = onlyTarget_ ? 0 : 1;
    if (n_ > 0) {
        if (layout_ == Layout::CheckpointsFinish || layout_ == Layout::AnyOrderFinish || layout_ == Layout::FixedOrder) {
            shown_[0] = 0;
            if (layout_ == Layout::CheckpointsFinish && n_ >= 3) shown_[size_t(n_ - 1)] = 0;
        } else if (layout_ == Layout::AnyOrder) {
            shown_[0] = 0;
        } else {
            for (int i = 0; i < n_; ++i) shown_[size_t(i)] = 1;
            lap_ = 0;
        }
        if (layout_ != Layout::Laps) passed_[0] = 1;
        if (onlyTarget_) {
            for (int i = 0; i < n_; ++i) shown_[size_t(i)] = 0;
            if (target_ < n_) shown_[size_t(target_)] = 1;
        }
    }
    if (layout_ != Layout::Laps) lap_ = 0;
    // 6: the arrow on, except in Circuit (the Crash Course switches it per event kind).
    arrow_ = layout_ != Layout::Laps;
}

void WaypointList::registerGate(int i, std::vector<GateEvent>& events) {
    last_ = i;
    passed_[size_t(i)] = 1;
    shown_[size_t(i)] = 0;
    ++count_;
    GateEvent e;
    e.kind = GateEvent::Registered;
    e.index = i;
    events.push_back(e);
}

bool WaypointList::crossed(int i, const GatePose& p, const vec3& box) const {
    const Waypoint& w = pts_[size_t(i)];
    if (layout_ == Layout::AnyOrderFinish && w.sphere != 0) return sphereGateHit(p.P, w);
    return playerCrossesGate(p, box, w);
}

void WaypointList::frame(const GatePose& player, const vec3& box, std::vector<GateEvent>& events) {
    if (n_ == 0 || off_ || disabled_) return;
    const int n = n_;
    switch (layout_) {
        case Layout::AnyOrder: {
            int found = -1;
            for (int i = 0; i < n && found < 0; ++i)
                if (!passed_[size_t(i)] && crossed(i, player, box)) found = i;
            if (found >= 0 && !done_) {
                registerGate(found, events);
                bool left = false;
                for (int i = 0; i < n; ++i)
                    if (!passed_[size_t(i)]) left = true;
                if (left) nearestTarget(player.P);
                else done_ = true;
            }
            break;
        }
        case Layout::CheckpointsFinish:
        case Layout::AnyOrderFinish: {
            int found = -1;
            for (int i = 0; i < n && found < 0; ++i)
                if (!passed_[size_t(i)] && crossed(i, player, box)) found = i;
            if (found < 0 || done_) break;
            if (found == n - 1) {
                if (count_ == n - 1) done_ = true;  // the finish crossed too early is ignored
            } else if (found > 0) {
                const int wasTarget = target_;
                registerGate(found, events);
                if (layout_ == Layout::CheckpointsFinish) {
                    if (count_ == n - 1) shown_[size_t(n - 1)] = 1;
                    nearestTarget(player.P);
                } else if (found == wasTarget) {
                    stepTarget(1);
                }
                if (onlyTarget_ && !done_) shown_[size_t(target_)] = 1;
                if (count_ == n - 1) {
                    shown_[size_t(target_)] = 1;
                    GateEvent e;
                    e.kind = GateEvent::FinalCheckpoint;
                    e.index = target_;
                    events.push_back(e);
                }
            }
            break;
        }
        case Layout::Laps: {
            const int t = target_;
            if (t < 0 || t >= n || done_ || !crossed(t, player, box)) break;
            shown_[size_t(t)] = 0;
            registerGate(t, events);
            if (t == 0) {
                ++lap_;
                if (lap_ == laps_) done_ = true;
                GateEvent e;
                e.kind = GateEvent::LapDone;
                e.index = 0;
                e.lap = lap_;
                e.done = done_;
                events.push_back(e);
                if (!done_) {
                    for (int i = 0; i < n; ++i) {
                        passed_[size_t(i)] = 0;
                        shown_[size_t(i)] = 1;
                    }
                }
            } else if (t == n - 1 && lap_ == laps_ - 1) {
                GateEvent e;
                e.kind = GateEvent::FinalCheckpoint;
                e.index = t;
                events.push_back(e);
            }
            target_ = t + 1 >= n ? 0 : t + 1;
            break;
        }
        case Layout::FixedOrder: {
            const int t = target_;
            if (t < 0 || t >= n || done_ || !crossed(t, player, box)) break;
            shown_[size_t(t)] = 0;
            target_ = t + 1;
            registerGate(t, events);
            if (count_ == n) done_ = true;
            if (onlyTarget_ && !done_ && target_ < n) shown_[size_t(target_)] = 1;
            if (target_ >= n) target_ = n - 1;  // engine: the arrow keeps the last gate
            break;
        }
    }
}

void WaypointList::nearestTarget(const vec3& P) {
    float best = 1.0e9f;
    int pick = -1;
    for (int i = 1; i < n_; ++i) {
        if (passed_[size_t(i)] || !shown_[size_t(i)]) continue;
        const vec3& q = pts_[size_t(i)].pos;
        const float dx = q.x - P.x, dy = q.y - P.y, dz = q.z - P.z;
        const float d2 = (dz * dz + dy * dy) + dx * dx;
        if (d2 < best || std::isnan(d2)) {
            best = d2;
            pick = i;
        }
    }
    if (pick >= 0 && target_ != 0) target_ = pick;
}

void WaypointList::stepTarget(int dir) {
    const int n = n_;
    if (n == 0) return;
    if (count_ == n - 1) {
        target_ = n - 1;
        return;
    }
    const int start = target_;
    int t = target_;
    for (int guard = 0; guard <= n + 1; ++guard) {
        t = ((t + dir) % n + n) % n;
        if (t == 0 || t == n - 1) t = dir > 0 ? 1 : n - 2;
        if (t < 0 || t >= n) return;
        if (!passed_[size_t(t)]) {
            target_ = t;
            return;
        }
        if (t == start) return;
    }
}

}  // namespace game
}  // namespace cc
