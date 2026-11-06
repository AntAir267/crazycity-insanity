// The waypoint list of a race (spec/game/waypoints.md): the gates, the exact crossing tests for
// the player and the opponents, the five layouts with their reset and per-frame rules, the
// nearest target and the S / X keys.
#pragma once

#include <vector>

#include "core/math.h"
#include "formats/race_files.h"

namespace cc {
namespace game {

// A car's pose as the gate tests read it: body position (centre of mass) and rotation rows.
struct GatePose {
    vec3 P;
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1};
};

// T(p1, p2, q1, q2, t): segment p1–p2 crosses q1–q2 with tolerance t (a line intersection and a
// box check, as MM2 has it).
bool segmentTest(vec2 p1, vec2 p2, vec2 q1, vec2 q2, float t);
// The player's test (InertiaBox halves, the long segment reaching 2 m behind the tail).
bool playerCrossesGate(const GatePose& c, const vec3& inertiaBox, const Waypoint& w);
// An opponent's test (five car-widths wide).
bool opponentCrossesGate(const GatePose& c, const vec3& inertiaBox, const Waypoint& w);
// A sphere gate (layout 4 with the sphere flag): 3D distance of the body position below r.
bool sphereGateHit(const vec3& P, const Waypoint& w);

enum class Layout { Laps = 1, CheckpointsFinish = 2, AnyOrder = 3, AnyOrderFinish = 4, FixedOrder = 5 };

// What a frame of the list did, in order.
struct GateEvent {
    enum Kind {
        Registered,       // waypoint `index` passed (split time and gate sound)
        FinalCheckpoint,  // only the finish is left (music and speech cues)
        LapDone,          // Circuit: the start/finish line completed lap `lap` (done: the last)
    };
    Kind kind = Registered;
    int index = 0;
    int lap = 0;
    bool done = false;
};

class WaypointList {
public:
    // A list of waypoints used with `layout`; `laps`: Circuit's lap count. (A Crash Course event
    // list replaces the previous one; the extra slots MM2's pool keeps stay passed and hidden,
    // which the engine shows by drawing and testing only the current list.)
    void set(const std::vector<Waypoint>& points, Layout layout, int laps, bool onlyTarget);
    // An empty list (missing file) or a list switched off completely (Crash Course events
    // without checkpoints).
    void clear();
    void setDisabled(bool d) { disabled_ = d; }
    bool disabled() const { return disabled_; }

    // waypoints.md, "Reset".
    void reset();
    // waypoints.md, "Every frame": the gate test with the player's pose. Skipped while the list is
    // empty, disabled or off.
    void frame(const GatePose& player, const vec3& inertiaBox, std::vector<GateEvent>& events);
    // Nearest target (Checkpoint and Blitz) from the player's body position.
    void nearestTarget(const vec3& P);
    // S (+1) and X (−1): Checkpoint races only (the caller checks the mode).
    void stepTarget(int dir);

    int size() const { return n_; }
    const Waypoint& point(int i) const { return pts_[size_t(i)]; }
    bool passed(int i) const { return passed_[size_t(i)] != 0; }
    bool shown(int i) const { return shown_[size_t(i)] != 0; }
    void setShown(int i, bool s) { shown_[size_t(i)] = s ? 1 : 0; }
    int count() const { return count_; }
    int target() const { return target_; }
    int lastPassed() const { return last_; }
    int lap() const { return lap_; }
    int laps() const { return laps_; }
    bool done() const { return done_; }
    bool off() const { return off_; }
    void setOff(bool o) { off_ = o; }
    // "Hide the last waypoint" (a lost race).
    void hideLast() {
        if (n_ > 0) shown_[size_t(n_ - 1)] = 0;
    }
    bool arrowOn() const { return arrow_ && !done_ && n_ > 0 && !disabled_; }
    void setArrow(bool on) { arrow_ = on; }
    Layout layout() const { return layout_; }
    bool onlyTarget() const { return onlyTarget_; }
    bool checkpointRace() const { return layout_ == Layout::CheckpointsFinish; }
    // The point the arrow points at.
    vec3 targetPosition() const { return n_ > 0 ? pts_[size_t(target_)].pos : vec3(); }

private:
    void registerGate(int i, std::vector<GateEvent>& events);
    bool crossed(int i, const GatePose& p, const vec3& box) const;

    std::vector<Waypoint> pts_;
    std::vector<char> passed_, shown_;
    int n_ = 0;  // waypoints of the current list (the pool may hold more)
    Layout layout_ = Layout::CheckpointsFinish;
    int laps_ = 1, lap_ = 0;
    int count_ = 1, target_ = 1, last_ = 0;
    bool done_ = false, off_ = false, disabled_ = false;
    bool onlyTarget_ = false;
    bool arrow_ = true;
};

}  // namespace game
}  // namespace cc
