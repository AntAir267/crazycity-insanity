#include "game/hud.h"

#include <cstdio>

#include "game/player_files.h"

namespace cc {
namespace game {

std::string formatTimer(float t) {
    if (!(t > 0.0f)) t = 0.0f;
    const float tp = t + 0.005f;
    const int whole = int(tp);
    const int hh = int((tp - float(whole)) * 100.0f);
    const int ss = whole % 60;
    const int mm = int(tp * (1.0f / 60.0f)) % 100;
    char b[16];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d", mm, ss, hh);
    return b;
}

std::string formatTime(float t) {
    if (!(t > 0.0f)) return std::string();
    const float tp = t + 0.005f;
    const int whole = int(tp);
    const int hh = int((tp - float(whole)) * 100.0f);
    const int M = whole / 60;
    const int S = whole - 60 * M;
    char b[24];
    std::snprintf(b, sizeof b, "%d:%02d:%02d", M, S, hh);
    return b;
}

HudModeLayout hudLayoutFor(int mode) {
    HudModeLayout l;
    switch (mode) {
        case ModeCheckpoint:
            l.timer = 1;
            l.place = l.checkpoints = true;
            l.arrow = true;
            break;
        case ModeCircuit:
            l.timer = 1;
            l.place = l.checkpoints = l.laps = true;
            break;
        case ModeBlitz:
            l.timer = 2;
            l.checkpoints = true;
            l.arrow = true;
            l.markersForced = true;
            break;
        case ModeCrash:
            l.timer = 2;  // most lessons; the race code changes it per event type
            l.checkpoints = true;
            l.arrow = true;
            break;
        case ModeCopsRobbers:
            l.timer = 2;
            l.arrow = true;
            break;
        default:  // Cruise: no timer, no panel, no arrow
            break;
    }
    return l;
}

void Hud::reset(int mode) {
    mode_ = mode;
    layout_ = hudLayoutFor(mode);
    raceClock = RaceTimer();
    opponentClock = RaceTimer();
    countdown = RaceTimer();
    countdown.countsUp = false;
    primary_ = HudLine();
    secondary_ = HudLine();
    place_ = racers_ = cpDone_ = cpTotal_ = lap_ = laps_ = 0;
    lapTimes_.clear();
    arrowOn_ = false;
    markers_.clear();
    mapCars.clear();
    mapDots.clear();
    outOfTime = false;
    external_ = false;
    extTimer_ = -1.0f;
    extWarning_ = 0;
}

void Hud::update(float dt) {
    raceClock.advance(dt);
    opponentClock.advance(dt);
    countdown.advance(dt);
    if (primary_.timeLeft > 0.0f) {
        primary_.timeLeft -= dt;
        if (primary_.timeLeft <= 0.0f) {  // both lines are cleared when the primary's time runs out
            primary_ = HudLine();
            secondary_ = HudLine();
        }
    }
}

void Hud::postPrimary(int textId, const std::string& text, float seconds, bool upper) {
    // On-screen lines take at most 255 bytes; a longer message is ignored (mmlang.md).
    if (text.size() > 255) return;
    primary_.textId = textId;
    primary_.text = text;
    primary_.timeLeft = seconds;
    primary_.upper = upper;
    secondary_ = HudLine();
}

void Hud::postSecondary(int textId, const std::string& text) {
    if (text.size() > 255) return;
    secondary_.textId = textId;
    secondary_.text = text;
    secondary_.upper = primary_.upper;
}

void Hud::setLapTime(int i, float t) {
    if (i < 0) return;
    if (size_t(i) >= lapTimes_.size()) lapTimes_.resize(size_t(i) + 1, 0.0f);
    lapTimes_[size_t(i)] = t;
}

float Hud::shownTimer() const {
    if (external_) return extTimer_;
    if (layout_.timer == 1) return raceClock.value;
    if (layout_.timer == 2) return countdown.value;
    return -1.0f;
}

int Hud::timeWarning() const {
    if (external_) return extWarning_;
    if (layout_.timer != 2 || outOfTime || !countdown.running) return 0;
    if (countdown.value <= 3.0f) return 2;
    if (countdown.value < 10.0f) return 1;
    return 0;
}

}  // namespace game
}  // namespace cc
