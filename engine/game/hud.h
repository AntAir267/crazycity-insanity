// The information of the driving HUD (spec/game/hud.md): timers and their formatting, the race
// messages (primary/secondary lines with their times), the place / checkpoint / lap panel, the
// direction arrow's target and the opponent markers. The look is the page's; this is the state.
// The race modes (milestone 4) drive it through the calls marked "race hook".
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {
namespace game {

// Timer display "MM:SS:hh" (hud.md, "Timers").
std::string formatTimer(float t);
// Times in messages and tables "M:SS:hh"; "" for t ≤ 0 (the page shows its "no time" dash).
std::string formatTime(float t);

struct RaceTimer {
    float value = 0.0f;
    float startValue = 0.0f;
    bool countsUp = true;
    bool running = false;
    void reset(float start, bool up) {
        startValue = start;
        value = start;
        countsUp = up;
        running = false;
    }
    void advance(float dt) {
        if (!running) return;
        if (countsUp) value += dt;
        else {
            value -= dt;
            if (value <= 0.0f) {  // a countdown that reaches 0 stays there and stops
                value = 0.0f;
                running = false;
            }
        }
    }
};

// What each mode shows (hud.md, "Per mode").
struct HudModeLayout {
    int timer = 0;           // 0 none, 1 race clock (counting up), 2 countdown
    bool place = false, checkpoints = false, laps = false;
    bool arrow = false;
    bool markersForced = false;
};
HudModeLayout hudLayoutFor(int mode);

struct HudLine {
    int textId = -1;      // mmlang.dll number (−1: `text` only)
    std::string text;     // filled-in text or a formatted time
    float timeLeft = 0.0f;
    bool upper = false;   // countdown position (high) or the normal one (low)
};

struct HudMarker {
    vec3 pos;             // world position of the object (the marker floats above it)
    uint8_t rgb[3] = {180, 0, 255};
    int digit = 0;        // 1 – 8: shown above the marker
};

// Race hooks for the HUD map: opponents and checkpoints (hudmap.md, "Drawing").
struct HudMapCar {
    vec3 pos, forward;
    uint8_t rgb[3] = {180, 0, 255};
};
struct HudMapDot {
    vec3 pos;
    int set = 2;  // hudmap_square material set (0 red … 5 finish, 6 gold, 7 bank, 8 hideout)
};

class Hud {
public:
    void reset(int mode);  // a new game or restart of `mode`
    int mode() const { return mode_; }
    const HudModeLayout& layout() const { return layout_; }

    // Per frame (frame time dt; not called while paused).
    void update(float dt);

    // ---- race hooks
    RaceTimer raceClock, opponentClock, countdown;
    void startClocks() {
        raceClock.reset(0.0f, true);
        opponentClock.reset(0.0f, true);
        raceClock.running = opponentClock.running = true;
    }
    // A new primary line replaces the old one and clears the secondary.
    void postPrimary(int textId, const std::string& text, float seconds, bool upper);
    void postSecondary(int textId, const std::string& text);
    void setPlace(int p, int n) { place_ = p; racers_ = n; }
    void setCheckpoints(int k, int n) { cpDone_ = k; cpTotal_ = n; }
    void setLaps(int lap, int laps) { lap_ = lap; laps_ = laps; }
    void setLapTime(int i, float t);  // lap row i (0-based) shows t
    void setArrowTarget(bool on, const vec3& p) { arrowOn_ = on; arrowTarget_ = p; }
    std::vector<HudMarker>& markers() { return markers_; }
    std::vector<HudMapCar> mapCars;  // drawn on the HUD map (opponents, network players)
    std::vector<HudMapDot> mapDots;  // checkpoint dots on the HUD map
    bool outOfTime = false;  // Blitz overtime: the countdown has become an overtime clock

    // ---- a race session drives the HUD (its own clocks and messages, game/race.h)
    void setLayout(const HudModeLayout& l) { layout_ = l; }
    // The shown timer (t < 0: none) and the time warning (0 none, 1 beeps, 2 continuous).
    void setExternalTimer(float t, int warning) {
        external_ = true;
        extTimer_ = t;
        extWarning_ = warning;
    }
    void setLines(const HudLine& p, const HudLine& s) {
        primary_ = p;
        secondary_ = s;
    }
    void setLapTimes(const std::vector<float>& v) { lapTimes_ = v; }

    // ---- state for the page
    const HudLine& primary() const { return primary_; }
    const HudLine& secondary() const { return secondary_; }
    int place() const { return place_; }
    int racers() const { return racers_; }
    int checkpointsDone() const { return cpDone_; }
    int checkpointsTotal() const { return cpTotal_; }
    int lap() const { return lap_; }
    int laps() const { return laps_; }
    const std::vector<float>& lapTimes() const { return lapTimes_; }
    bool arrowOn() const { return arrowOn_ && layout_.arrow; }
    const vec3& arrowTarget() const { return arrowTarget_; }
    const std::vector<HudMarker>& markers() const { return markers_; }
    // The shown timer (−1: none).
    float shownTimer() const;
    // Time warning of Blitz and Crash Course countdowns: 0 none, 1 a beep about once a second,
    // 2 continuous (hud.md, "Time warning").
    int timeWarning() const;

private:
    int mode_ = 0;
    HudModeLayout layout_;
    bool external_ = false;
    float extTimer_ = -1.0f;
    int extWarning_ = 0;
    HudLine primary_, secondary_;
    int place_ = 0, racers_ = 0, cpDone_ = 0, cpTotal_ = 0, lap_ = 0, laps_ = 0;
    std::vector<float> lapTimes_;
    bool arrowOn_ = false;
    vec3 arrowTarget_;
    std::vector<HudMarker> markers_;
};

}  // namespace game
}  // namespace cc
