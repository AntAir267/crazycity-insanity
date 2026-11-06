#include "game/race.h"

#include <algorithm>
#include <cmath>

#include "ai/road_network.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/aimap.h"
#include "physics/car.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

namespace cc {
namespace game {

namespace {
std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

float dist2(const vec3& a, const vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return (dx * dx + dy * dy) + dz * dz;  // results.md: (dx·dx + dy·dy) + dz·dz
}

GatePose poseOf(const phys::Car& c) {
    GatePose p;
    p.P = c.body().p;
    p.X = c.rotation().r[0];
    p.Y = c.rotation().r[1];
    p.Z = c.rotation().r[2];
    return p;
}
}  // namespace

std::string formatRaceTime(float t) {
    if (!(t > 0.0f)) return "-:--:--";
    const float tp = t + 0.005f;
    const int whole = int(std::trunc(tp));
    const int hund = int(std::trunc((tp - std::trunc(tp)) * 100.0f));
    const int M = whole / 60;
    const int S = whole - 60 * M;
    return strf("%d:%02d:%02d", M, S, hund);
}

// formatTimer is in game/hud.cpp (the same mm:ss:hh rule, game/hud.md).

std::string fallbackText(int id) {
    // Our own English stand-ins for the mmlang.dll texts the race modes name (the HUD builder
    // shows the player's own mmlang.dll; formats/mmlang.md).
    if (id >= 13 && id <= 20) return strf("Opponent %d", id - 12);
    if (id >= 21 && id <= 28) {
        static const char* p[] = {"1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th"};
        return p[id - 21];
    }
    if (id >= 183 && id <= 190) return strf("Finished %s!", fallbackText(21 + id - 183).c_str());
    if (id >= 169 && id <= 176) return strf("Finished %s!", fallbackText(21 + id - 169).c_str());
    if (id >= 532 && id <= 544) return strf("London lesson %d", id - 531);
    if (id >= 545 && id <= 557) return strf("San Francisco lesson %d", id - 544);
    switch (id) {
        case 3: return "You";
        case 5: return "Circuit";
        case 6: return "Checkpoint";
        case 7: return "Blitz";
        case 12: return "Crash Course";
        case 30: case 642: case 643: return "In the water!";
        case 62: return "Final lap";
        case 63: return "Lap time";
        case 158: case 165: case 179: return "Ready";
        case 159: case 166: case 180: return "Set";
        case 160: case 167: case 181: return "Go!";
        case 161: return "Out of time";
        case 162: case 168: case 182: return "Wrecked!";
        case 164: return "You beat the clock!";
        case 177: case 191: return "Finished";
        case 193: case 200: case 206: case 211: case 218: case 225: case 241: case 617: return "Get ready";
        case 194: case 201: case 207: case 212: case 219: case 226: case 242: case 618: return "Set";
        case 195: return "Lose the cops!";
        case 652: return "Lose the cops!";
        case 196: return "You got away!";
        case 197: return "The cops saw you";
        case 198: case 203: case 214: case 228: case 244: case 614: return "Out of time";
        case 199: return "Accelerate and stop";
        case 202: case 208: case 213: case 220: case 227: case 233: case 243: case 619: return "Go!";
        case 204: case 209: case 215: case 221: case 229: case 234: case 609: case 620: return "Lesson passed";
        case 210: return "Clean driving";
        case 216: return "Damage penalty";
        case 222: return "You lost the car";
        case 223: return "Stay closer";
        case 230: return "You touched something";
        case 232: return "Keep above %d mph";
        case 235: return "Too slow";
        case 236: return "Reach the speed before the first gate";
        case 237: return "Now hold that speed";
        case 238: return "Too slow";
        case 239: return "Keep the speed";
        case 240: case 245: case 615: return "Wrecked";
        case 621: return "The car got away";
        case 622: return "Catch it sooner";
        case 651: return "Passed";
        default: break;
    }
    return strf("[%d]", id);
}

RaceSession::RaceSession() = default;
RaceSession::~RaceSession() = default;

void RaceSession::emit(const char* name, int v, int v2) {
    if (s_.events) s_.events(RaceEvent{name, v, v2});
}

void RaceSession::post(int text, float time, bool upper, const std::string& arg) {
    msg_ = HudMessage();
    msg_.text = text;
    msg_.time = time;
    msg_.upper = upper;
    msg_.arg = arg;
}

void RaceSession::post2(int text, int second, float time) {
    post(text, time, false);
    msg_.second = second;
}

bool RaceSession::setup(const Setup& s, phys::Car& player, std::string& err) {
    s_ = s;
    player_ = &player;
    mode_ = s.mode;
    race_ = s.race;
    pro_ = s.professional;
    noDamage_ = s.noDamage;
    const std::string& dir = s.cityEntry.raceDir;
    raceCount_ = raceCount(s.cityEntry, mode_);
    title_ = raceDisplayName(s.cityEntry, mode_, race_);
    // The settings row (races.md, "The session's settings").
    const auto rows = parseRaceData(textOf(s.vfs->open(settingsPath(dir, mode_))));
    haveRow_ = race_ >= 0 && size_t(race_) < rows.size();
    if (haveRow_) row_ = rows[size_t(race_)];
    const RaceSettings& r = row_.of(pro_);
    values_ = SessionValues();
    if (haveRow_) {
        values_.copDensity = float(r.cops);
        values_.trafficDensity = r.ambient;
        values_.pedDensity = r.peds;
        values_.timeOfDay = r.timeOfDay;
        values_.weather = r.weather;
        values_.timeLimit = r.timeLimit;
        values_.difficulty = r.difficulty;
        values_.opponentNumber = float(r.opponents);
    }
    // The player's time of day, weather, cops and pedestrians (menus.md, "Locked settings").
    if (s.timeOfDay >= 0) values_.timeOfDay = s.timeOfDay;
    if (s.weather >= 0) values_.weather = s.weather;
    if (s.copDensity >= 0.0f) values_.copDensity = s.copDensity;
    if (s.trafficDensity >= 0.0f && mode_ != RaceMode::Circuit) values_.trafficDensity = s.trafficDensity;
    if (s.pedDensity >= 0.0f) values_.pedDensity = s.pedDensity;
    values_.laps = 1;
    if (mode_ == RaceMode::Circuit && haveRow_) {
        values_.laps = s.laps > 0 ? std::min(s.laps, 8) : r.numLaps;  // the menu's 1 – 8, or NumLaps
        int choice = s.circuitOpponents < 0 ? r.opponents : s.circuitOpponents;
        if (choice > r.opponents) choice = r.opponents;
        if (choice < 1 && r.opponents >= 1) choice = 1;
        values_.opponentNumber = float(choice);
    }
    if (mode_ == RaceMode::Blitz) countdownLimit_ = values_.timeLimit;
    if (mode_ == RaceMode::CrashCourse) {
        values_.opponentNumber = 8.0f;
        values_.copDensity = 1.0f;
        const std::string path = crashEventPath(dir, race_, pro_);
        BytesPtr b = s.vfs->open(path);
        if (!b) {
            err = "the course's event file " + path + " is missing";
            return false;
        }
        events_ = parseCrashEvents(textOf(b));
        if (events_.empty()) {
            err = "the course's event file " + path + " has no events";
            return false;
        }
        values_.trafficDensity = events_.back().ambDensity;  // the last row wins
        for (const CrashEvent& e : events_)
            if (e.checkpoints && !s.vfs->exists("race/" + dir + "/" + e.filename + ".csv")) {
                err = "the event list race/" + dir + "/" + e.filename + ".csv is missing";
                return false;
            }
    } else {
        const std::string path = waypointPath(dir, mode_, race_);
        BytesPtr b = s.vfs->open(path);
        if (!b) {
            err = "the race's waypoint file " + path + " is missing";
            return false;
        }
        const WaypointFile wf = parseWaypoints(textOf(b), 50, mode_ == RaceMode::Circuit);
        if (wf.points.empty()) {
            err = "the race's waypoint file " + path + " has no waypoints";
            return false;
        }
        points_ = wf.points;
        const Layout layout = mode_ == RaceMode::Circuit ? Layout::Laps
                              : mode_ == RaceMode::Blitz ? Layout::AnyOrder
                                                         : Layout::CheckpointsFinish;
        list_.set(points_, layout, values_.laps, false);
        // The opponents' finish line (waypoints.md, last section).
        if (mode_ == RaceMode::Checkpoint) ai::setOpponentFinishLine(points_.back().pos, points_.back().heading);
        else if (mode_ == RaceMode::Circuit) ai::setOpponentFinishLine(points_.front().pos, points_.front().heading);
    }
    // Opponents (opponents.md, "Which opponents exist").
    const RaceAimap a = parseRaceAimap(textOf(s.vfs->open(raceAimapPath(dir, raceName(mode_, race_), pro_))));
    ai::OpponentSystem::Setup os;
    os.rows = a.opponents;
    os.countLine = a.opponentCount;
    os.opponentNumber = values_.opponentNumber;
    os.laps = mode_ == RaceMode::Circuit ? values_.laps : 1;
    os.regenerate = mode_ == RaceMode::Circuit;
    os.raceDir = dir;
    os.playerCar = s.playerCar;
    os.playerBound = s.playerBound;
    os.loadModels = s.assets != nullptr;
    opps_.create(*s.vfs, s.assets, *s.city, *s.net, *s.world, os);
    const size_t m = opps_.opponents().size();
    oppCount_.assign(m, 1);
    oppPlace_.assign(m, 0);
    oppMask_.assign(m, 1u);
    oppDone_.assign(m, 0);
    return true;
}

std::vector<vec3> RaceSession::opponentPositions() const {
    std::vector<vec3> v;
    for (const ai::Opponent& o : opps_.opponents()) v.push_back(o.car->body().p);
    return v;
}

void RaceSession::start() {
    // Placing the cars (races.md): the player at waypoint 0 with θ = a × −0.017453292.
    Waypoint w0;
    if (mode_ == RaceMode::CrashCourse) {
        for (const CrashEvent& e : events_) {
            if (!e.checkpoints) continue;
            const auto wf = parseWaypoints(textOf(s_.vfs->open("race/" + s_.cityEntry.raceDir + "/" + e.filename + ".csv")), 32, false);
            if (!wf.points.empty()) w0 = wf.points[0];
            break;
        }
    } else {
        w0 = points_[0];
    }
    start_ = w0.pos;
    startTheta_ = w0.heading * -0.017453292f;
    s_.host->resetPlayer(start_, startTheta_);
    // 2. The ground snap, once, from the body position.
    {
        const vec3 P = player_->body().p;
        phys::ProbeState ps;
        phys::ProbeHit hit;
        if (s_.world->probe(vec3(P.x, P.y + 2.0f, P.z), vec3(P.x, P.y - 10.0f, P.z), ps, hit)) {
            start_ = vec3(hit.point.x, hit.point.y + 0.89999998f, hit.point.z);
            s_.host->resetPlayer(start_, startTheta_);
        }
    }
    // 3. The opponents: the first world reset places them, then the snap from the model origin.
    opps_.reset(now_);
    opps_.groundSnap(*s_.world);
    restart();
}

// ---- Racers ----

void RaceSession::holdRacers() {
    player_->setDrivable(false);
    player_->damage().enabled = false;
    for (ai::Opponent& o : opps_.opponents()) {
        o.car->setDrivable(false);
        o.car->damage().enabled = false;
    }
}

void RaceSession::releaseOpponents(int first, int count) {
    auto& v = opps_.opponents();
    for (int i = first; i < first + count && i < int(v.size()); ++i) {
        if (i < 0) continue;
        phys::Car& c = *v[size_t(i)].car;
        c.setDrivable(true);
        if (c.gearIndex() <= 1) c.selectGear(2);
        c.damage().enabled = true;
    }
}

void RaceSession::go(bool withOpponents) {
    phys::Car& p = *player_;
    p.setDrivable(true);
    if (p.gearIndex() <= 1) p.selectGear(2);
    p.damage().enabled = !noDamage_;
    if (p.automatic()) p.clearAutoReverse();
    if (!withOpponents) return;
    if (mode_ == RaceMode::CrashCourse) {
        int S = 0;
        for (int j = 0; j < event_ && j < int(events_.size()); ++j) S += events_[size_t(j)].opponents;
        releaseOpponents(S, events_[size_t(event_)].opponents);
    } else {
        releaseOpponents(0, int(opps_.opponents().size()));
    }
}

void RaceSession::disablePlayer() { player_->setPlayerDisabled(true); }

void RaceSession::notDrivable() { player_->setDrivable(false); }

bool RaceSession::wrecked() const { return player_->damage().wrecked(player_->tune().damage); }

// ---- Clocks ----

// "Stop the clocks" (races.md, "Clocks"): the race clock and the opponents' clock; the countdown
// only where a page names it.
void RaceSession::stopClocks() { clock_.running = oppClock_.running = false; }

void RaceSession::resetStopClocks() {
    clock_ = Clock();
    oppClock_ = Clock();
}

void RaceSession::startResetClocks() {
    clock_ = Clock{0.0f, true};
    oppClock_ = Clock{0.0f, true};
}

void RaceSession::stopCountdownToLimit() {
    countdown_.running = false;
    countdown_.t = countdownLimit_;
}

void RaceSession::advanceClocks(float F) {
    if (clock_.running) clock_.t = clock_.t + F;
    if (oppClock_.running) oppClock_.t = oppClock_.t + F;
    if (countdown_.running) {
        if (countdown_.t <= 0.0f) {
            countdown_.t = 0.0f;
            countdown_.running = false;
        } else {
            countdown_.t = countdown_.t - F;
        }
    }
}

// ---- Restart ----

void RaceSession::restart() {
    // 1. Hold every racer (Crash Course: back to the first event first).
    if (mode_ == RaceMode::CrashCourse) {
        event_ = 0;
        firstEvent_ = true;
        setupEvent(0);
    }
    holdRacers();
    // 2. The race's own state.
    t_ = 0.0f;
    const size_t m = opps_.opponents().size();
    oppCount_.assign(m, 1);
    oppPlace_.assign(m, 0);
    oppMask_.assign(m, 1u);
    oppDone_.assign(m, 0);
    finishedCount_ = 0;
    results_.clear();
    raceFinished_ = false;
    bestLap_ = 0;
    haveBest_ = false;
    lapSum_ = 0;
    lapTimes_.clear();
    storedLaps_.assign(size_t(std::max(values_.laps, 1)), 0.0f);
    nextStore_ = 1;
    outOfTime_ = false;
    inWater_ = false;
    resultsShown_ = endMenu_ = false;
    waterEpisode_ = false;
    msg_ = HudMessage();
    if (warning_) emit("TimerwarningStop");
    warning_ = false;
    score_ = 0;
    nextOffered_ = false;
    playerPlace_ = 0;
    hudPlace_ = 1;
    ccFinishedMark_ = false;
    ccPassed_ = false;
    ccReached_ = false;
    ccUnder_ = 0;
    // 3. The world reset: props, the pre-race speech, the AI reset (opponents, traffic, police,
    // generators), the player, the waypoint list, the HUD.
    emit(mode_ == RaceMode::CrashCourse ? "ccPrerace" : "prerace");
    opps_.reset(now_);
    s_.host->worldReset();
    s_.host->resetPlayer(start_, startTheta_);
    player_->setPlayerDisabled(false);
    list_.reset();
    if (mode_ == RaceMode::CrashCourse) setupEvent(0);
    // 4. The mode's state 0, the pre-race camera, the clocks stopped.
    state_ = 0;
    t_ = 0.0f;
    resetStopClocks();
    countdown_.running = false;
    countdown_.t = countdownLimit_;
    s_.host->preRaceCamera();
}

// ---- One frame ----

void RaceSession::frame(float F, bool next, bool prev, double now) {
    now_ = now;
    // 1. The mode's update.
    switch (mode_) {
        case RaceMode::Checkpoint: checkpointFrame(F); break;
        case RaceMode::Blitz: blitzFrame(F); break;
        case RaceMode::Circuit: circuitFrame(F); break;
        case RaceMode::CrashCourse: crashFrame(F); break;
        default: break;
    }
    // 2. Water and falling through.
    waterAndFalling(F);
    // 3. The player's keys (S / X: Checkpoint races only), the waypoint list with the player's
    // current pose, the HUD's clocks and messages.
    if (mode_ == RaceMode::Checkpoint && list_.size() > 0) {
        if (next) list_.stepTarget(1);
        if (prev) list_.stepTarget(-1);
    }
    std::vector<GateEvent> ev;
    list_.frame(poseOf(*player_), player_->tune().sim.inertiaBox, ev);
    gateEvents(ev);
    advanceClocks(F);
    if (msg_.text != 0 || !msg_.secondTime.empty() || !msg_.arg.empty()) {
        msg_.time = msg_.time - F;
        if (!(msg_.time > 0.0f)) msg_ = HudMessage();
    }
}

void RaceSession::gateEvents(const std::vector<GateEvent>& ev) {
    for (const GateEvent& e : ev) {
        if (e.kind == GateEvent::Registered) {
            if (mode_ != RaceMode::CrashCourse) {
                // The split: the race clock (Circuit: the lap time so far), 1 s, lower line.
                const float t = mode_ == RaceMode::Circuit ? clock_.t - lapSum_ : clock_.t;
                post(0, 1.0f, false);
                msg_.secondTime = formatRaceTime(t);
            }
            const int n = list_.size();
            if (mode_ == RaceMode::Checkpoint) {
                if (list_.count() != n - 1) emit("Waypoint", e.index);
            } else if (mode_ == RaceMode::Circuit) {
                emit(e.index == 0 ? "Lastwaypoint" : "Waypoint", e.index);
            } else {
                emit("Waypoint", e.index);
                if (mode_ == RaceMode::CrashCourse) emit("ccGate", e.index);
            }
        } else if (e.kind == GateEvent::FinalCheckpoint) {
            emit("finalCheckpoint");
            emit("musicFinalCheckpoint");
        } else if (e.kind == GateEvent::LapDone) {
            // results.md, "Circuit lap times".
            const float lap = clock_.t - lapSum_;
            lapSum_ = lapSum_ + lap;
            lapTimes_.push_back(lap);
            if (e.done) {
                clock_.running = false;
                clock_.t = lapSum_ + 0.005f;
            } else if (e.lap == list_.laps() - 1) {
                post(62, 1.0f);
                msg_.secondTime = formatRaceTime(lap);
                emit("musicFinalLap");
            } else {
                post(63, 1.0f);
                msg_.secondTime = formatRaceTime(lap);
            }
        }
    }
}

// ---- Water and falling through (races.md) ----

void RaceSession::pausedFrame(float F) { waterAndFalling(F); }

void RaceSession::waterAndFalling(float F) {
    phys::Car& c = *player_;
    if (c.body().p.y < -50.0f) {
        restart();
        return;
    }
    // The car's own water timer (collision.md, "World limits"; 0 again at every placement).
    float& waterTimer_ = c.collision().waterTimer;
    if (!c.collision().water) {
        waterTimer_ = 0.0f;
        waterEpisode_ = false;
        return;
    }
    const bool firstFrame = waterTimer_ == 0.0f;
    const float before = waterTimer_;
    waterTimer_ = F + waterTimer_;
    if (firstFrame) s_.host->waterCamera();
    // The water text (hud.md): on the first frame of the episode and again every frame while the
    // timer before this frame's step is below 0.1 s; MapName compared case-sensitively.
    if (before < 0.1f) {
        const std::string& d = s_.cityEntry.mapName;
        post(d == "london" ? 642 : d == "sf" ? 643 : 30, 3.0f, true);
    }
    if (waterEpisode_ || !(waterTimer_ > 5.0f)) return;
    waterEpisode_ = true;
    switch (mode_) {
        case RaceMode::Checkpoint:
        case RaceMode::Blitz:
            // Engine: once finished, the results are shown (MM2's quirks would lose the race).
            if (state_ == 5) return;
            emit("Damgelose");
            stopClocks();
            if (mode_ == RaceMode::Blitz) countdown_.running = false;
            if (mode_ == RaceMode::Checkpoint) emit("engineSilence");
            if (warning_) {
                warning_ = false;
                emit("TimerwarningStop");
            }
            t_ = 0.5f;
            inWater_ = true;
            state_ = 4;
            break;
        case RaceMode::Circuit: {
            // Back at the last gate passed (waypoint 0 before the first gate) with its heading;
            // the gate count and the start point stay.
            const Waypoint& w = list_.point(list_.lastPassed());
            s_.host->resetPlayer(w.pos, w.heading * -0.017453292f);
            waterEpisode_ = false;
            break;
        }
        case RaceMode::CrashCourse:
            if (ccFinishedMark_) return;
            emit("Damgelose");
            stopClocks();
            countdown_.running = false;
            t_ = 0.5f;
            inWater_ = true;
            state_ = 4;
            break;
        default: break;
    }
}

// ---- Standings and finishing (results.md) ----

void RaceSession::standings() {
    const int n = list_.size();
    if (n == 0) return;
    const auto& v = opps_.opponents();
    const int m = int(v.size());
    const int pc = list_.count();
    const vec3 pp = player_->body().p;
    const vec3& tgt = list_.point(list_.target()).pos;
    const float pd = dist2(pp, tgt);
    int place = 1;
    for (int i = 0; i < m; ++i) {
        const int c = oppCount_[size_t(i)];
        if (oppDone_[size_t(i)] || c > pc || (c == pc && dist2(v[size_t(i)].car->body().p, tgt) < pd)) ++place;
    }
    for (int i = 0; i < m; ++i) {
        if (oppDone_[size_t(i)]) continue;  // its finishing place stays
        const int ci = oppCount_[size_t(i)];
        const vec3& T = list_.point(ci % n).pos;
        const float e = dist2(v[size_t(i)].car->body().p, T);
        int p = 1;
        for (int j = 0; j < m; ++j) {
            if (j == i) continue;
            const int cj = oppCount_[size_t(j)];
            if (oppDone_[size_t(j)] || cj > ci || (cj == ci && dist2(v[size_t(j)].car->body().p, T) < e)) ++p;
        }
        if (pc > ci || (pc == ci && dist2(pp, T) < e)) ++p;
        oppPlace_[size_t(i)] = p;
    }
    const bool show = mode_ == RaceMode::Checkpoint ? state_ <= 3 : (state_ >= 3 && state_ <= 6);
    if (show) hudPlace_ = place;
}

void RaceSession::opponentsFinishing() {
    const int n = list_.size();
    auto& v = opps_.opponents();
    const ai::FinishLine& line = ai::opponentFinishLine();
    for (size_t i = 0; i < v.size(); ++i) {
        ai::Opponent& o = v[i];
        const GatePose pose = poseOf(*o.car);
        const vec3& box = o.car->tune().sim.inertiaBox;
        if (n > 0) {
            if (mode_ == RaceMode::Checkpoint) {
                for (int g = 1; g < n; ++g) {
                    const uint32_t bit = 1u << (unsigned(g) & 31u);
                    if (oppMask_[i] & bit) continue;
                    const float d = dist2(pose.P, list_.point(g).pos);
                    if (d > 2500.0f) continue;  // 50 m (a NaN passes)
                    if (opponentCrossesGate(pose, box, list_.point(g))) {
                        oppMask_[i] |= bit;
                        ++oppCount_[i];
                        break;
                    }
                }
            } else if (mode_ == RaceMode::Circuit) {
                if (opponentCrossesGate(pose, box, list_.point(oppCount_[i] % n))) ++oppCount_[i];
            }
        }
        if (oppDone_[i] || !ai::OpponentSystem::finishTest(o, line)) continue;
        oppDone_[i] = 1;
        ++finishedCount_;
        oppPlace_[i] = finishedCount_;
        results_.push_back(ResultRow{finishedCount_, int(i), oppClock_.t, false});
        if (state_ == 3) {
            post2(13 + int(i), 20 + finishedCount_, 5.0f);
            emit("Messagenote");
        }
    }
}

int RaceSession::scoreFor(int place) const {
    const int f = place == 1 ? 50 : place == 2 ? 25 : place == 3 ? 10 : 0;
    return int(std::trunc(s_.scoringBias)) * f * int(std::trunc(values_.difficulty));
}

void RaceSession::recordResult(const RaceOutcome& r) {
    if (s_.records && !s_.records->cheated()) s_.records->record(r);
}

std::string RaceSession::nextRaceCar() const {
    if (mode_ != RaceMode::CrashCourse) return std::string();
    if (s_.records && s_.records->passedBefore(mode_, race_ + 1, pro_)) return std::string();
    return iequals(s_.cityEntry.raceDir, "sf") ? "vpbullet" : "vpcab";
}

void RaceSession::setNextRace() {
    const bool notLast = race_ + 1 < raceCount_;
    switch (mode_) {
        case RaceMode::Checkpoint:
            nextOffered_ = notLast && s_.records && s_.records->hasDriver() && s_.records->raceOpen(mode_, race_ + 1, pro_);
            break;
        case RaceMode::Circuit:
        case RaceMode::Blitz: nextOffered_ = notLast; break;
        case RaceMode::CrashCourse:
            nextOffered_ = !(race_ == 2 || race_ == 6 || race_ == 10 || race_ == 11 || race_ == 12);
            break;
        default: nextOffered_ = false;
    }
}

// The player has finished a Checkpoint race (modes.md, state 3 a) or a Circuit.
void RaceSession::playerFinished(int k) {
    const int place = k + 1;
    playerPlace_ = place;
    hudPlace_ = place;
    finishedCount_ = k + 1;
    raceFinished_ = true;
    emit("musicStop");
    emit(k == 0 ? "Endofracetag" : "Youlose");
    results_.push_back(ResultRow{place, -1, clock_.t, false});
    score_ = scoreFor(place);
    RaceOutcome r;
    r.mode = mode_;
    r.race = race_;
    r.professional = pro_;
    r.car = s_.playerCar;
    r.place = place;
    r.score = score_;
    if (mode_ == RaceMode::Checkpoint) {
        r.time = clock_.t;
        r.passed = (s_.records && s_.records->passedBefore(mode_, race_, pro_)) || (pro_ ? place == 1 : place <= 3);
        if (s_.records && s_.records->hasDriver()) recordResult(r);
    } else {
        r.time = bestLap_;
        r.passed = pro_ ? place == 1 : place <= 3;
        r.lapTimes = storedLaps_;
        recordResult(r);  // Circuit records without a driver too (progression.md)
    }
    setNextRace();
    t_ = 5.0f;
}

// ---- Checkpoint race (modes.md, mode 1) ----

void RaceSession::checkpointFrame(float F) {
    switch (state_) {
        case 0:
            if (!s_.host->preRaceCameraRunning()) {
                resetStopClocks();
                t_ = 2.5f;
                state_ = 1;
                emit("Startracelow");
            }
            break;
        case 1:
            t_ = t_ - F;
            if (t_ <= 1.25f) {
                state_ = 2;
                t_ = 1.25f;
                emit("Startracelow");
            } else {
                post(179, 1.25f, true);
            }
            break;
        case 2:
            t_ = t_ - F;
            if (t_ <= 0.0f) {
                state_ = 3;
                emit("Startracehigh");
                emit("musicStart");
                post(181, 1.25f, true);
                go();
                startResetClocks();
            } else {
                post(180, 1.25f, true);
            }
            break;
        case 3:
            if (list_.done()) {
                state_ = 5;
                clock_.running = false;
                s_.host->postRaceCamera();
                disablePlayer();
                const int k = finishedCount_;
                if (k > 7) post(191, 5.0f, false);
                else post(183 + k, 5.0f, true);
                playerFinished(k);
            } else if (damageRules() && wrecked()) {
                list_.setOff(true);
                emit("Damgelose");
                emit("musicStop");
                emit("engineSilence");
                notDrivable();
                post(182, 5.0f, false);
                stopClocks();
                s_.host->postRaceCamera();
                t_ = 5.0f;
                state_ = 4;
            }
            break;
        case 4:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f && (wrecked() || inWater_) && !raceFinished_) endMenu_ = true;
            break;
        case 5:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f && !endMenu_ && !resultsShown_) {
                resultsShown_ = true;
                emit("results", playerPlace_, int(values_.opponentNumber));
            }
            break;
        default: break;
    }
    standings();
    if (state_ >= 3) opponentsFinishing();
}

// ---- Blitz (modes.md, mode 4) ----

void RaceSession::blitzFrame(float F) {
    switch (state_) {
        case 0:
            resetStopClocks();
            outOfTime_ = false;
            countdownShown_ = true;
            countdownLimit_ = values_.timeLimit;
            stopCountdownToLimit();
            t_ = 5.0f;
            state_ = 1;
            emit("Startracelow");
            break;
        case 1:
            t_ = t_ - F;
            if (t_ > 1.25f) {
                post(158, 1.25f, true);
            } else {
                state_ = 2;
                t_ = 1.25f;
                emit("Startracelow");
            }
            break;
        case 2:
            t_ = t_ - F;
            if (t_ > 0.0f) {
                post(159, 1.25f, true);
            } else {
                state_ = 3;
                emit("Startracehigh");
                post(160, 1.25f, true);
                go();
                startResetClocks();
                countdown_.running = true;
                emit("musicStart");
            }
            break;
        case 3: {
            if (countdown_.t < 10.0f && !outOfTime_ && !warning_) {
                warning_ = true;
                emit("TimerwarningStart");
            }
            auto stopWarning = [&]() {
                if (warning_) emit("TimerwarningStop");
                warning_ = false;
            };
            if (list_.done()) {
                if (outOfTime_) {
                    stopWarning();
                    list_.setOff(true);
                    list_.hideLast();
                    emit("resultsPoor");
                    emit("Youlose");
                    post(161, 5.0f, false);
                    stopClocks();
                    countdown_.running = false;
                    disablePlayer();
                    t_ = 5.0f;
                    state_ = 4;
                } else {
                    state_ = 5;
                    s_.host->postRaceCamera();
                    disablePlayer();
                    clock_.running = false;
                    countdown_.running = false;
                    stopWarning();
                    post(164, 5.0f, true);
                    emit("musicStop");
                    emit("Endofracetag");
                    playerPlace_ = 1;
                    hudPlace_ = 1;
                    raceFinished_ = true;
                    results_.push_back(ResultRow{1, -1, clock_.t, false});
                    score_ = scoreFor(1);
                    if (s_.records && s_.records->hasDriver() && race_ < 12) {
                        RaceOutcome r;
                        r.mode = mode_;
                        r.race = race_;
                        r.professional = pro_;
                        r.car = s_.playerCar;
                        r.time = clock_.t;
                        r.passed = true;
                        r.score = score_;
                        r.place = 1;
                        recordResult(r);
                    }
                    setNextRace();
                    t_ = 5.0f;
                    break;  // nothing more this frame
                }
            }
            if (countdown_.t <= 0.0f) {
                stopWarning();
                if (!outOfTime_) {
                    outOfTime_ = true;
                    emit("Youlose");
                    post(161, 5.0f, false);
                    countdownShown_ = false;
                }
            }
            if (damageRules() && wrecked()) {
                stopWarning();
                list_.setOff(true);
                list_.hideLast();
                emit("Damgelose");
                emit("damagePenalty");
                emit("musicStop");
                emit("engineSilence");
                notDrivable();
                post(162, 5.0f, false);
                s_.host->postRaceCamera();
                stopClocks();
                countdown_.running = false;
                t_ = 5.0f;
                state_ = 4;
            }
            break;
        }
        case 4:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f) endMenu_ = true;
            break;
        case 5:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f && !endMenu_ && !resultsShown_) {
                resultsShown_ = true;
                emit("results", 1, int(values_.opponentNumber));
            }
            break;
        default: break;
    }
}

// ---- Circuit (modes.md, mode 3) ----

void RaceSession::circuitFrame(float F) {
    auto storeLap = [&](int slot) {
        if (slot < 1 || size_t(slot) > lapTimes_.size() || size_t(slot) > storedLaps_.size()) return;
        const float t = lapTimes_[size_t(slot - 1)];
        storedLaps_[size_t(slot - 1)] = t;
        if (!haveBest_ || t < bestLap_) {
            bestLap_ = t;
            haveBest_ = true;
        }
    };
    switch (state_) {
        case 0:
            if (!s_.host->preRaceCameraRunning()) {
                resetStopClocks();
                t_ = 2.5f;
                state_ = 1;
                emit("Startracelow");
            }
            break;
        case 1:
            t_ = t_ - F;
            if (t_ <= 1.25f) {
                state_ = 2;
                t_ = 1.25f;
                emit("Startracelow");
            } else {
                post(165, 1.25f, true);
            }
            break;
        case 2:
            t_ = t_ - F;
            if (t_ <= 0.0f) {
                state_ = 3;
                emit("Startracehigh");
                emit("musicStart");
                post(167, 1.25f, true);
                go();
                startResetClocks();
            } else {
                post(166, 1.25f, true);
            }
            break;
        case 3:
            if (damageRules() && wrecked()) {
                notDrivable();
                emit("Messagenote");  // sound instance 4 is Messagenote in Circuit
                post(168, 5.0f, false);
                t_ = 5.0f;
                state_ = 6;
            }
            if (s_.records && s_.records->hasDriver() && list_.lap() == nextStore_) {
                storeLap(nextStore_);
                ++nextStore_;
            }
            break;
        case 5:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f && !endMenu_ && !resultsShown_) {
                resultsShown_ = true;
                emit("results", playerPlace_, int(values_.opponentNumber));
            }
            break;
        case 6:
            t_ = t_ - F;
            if (t_ <= 0.0f) {
                player_->clearDamage();
                player_->setDrivable(true);
                if (player_->gearIndex() <= 1) player_->selectGear(2);
                state_ = 3;
            }
            break;
        default: break;
    }
    if ((state_ == 3 || state_ == 6) && list_.done()) {
        const int k = finishedCount_;
        state_ = 5;
        hudPlace_ = k + 1;
        s_.host->postRaceCamera();
        disablePlayer();
        clock_.running = false;
        player_->damage().enabled = false;
        if (k > 7) post(177, 5.0f, false);
        else post(169 + k, 5.0f, true);
        storeLap(values_.laps);
        playerFinished(k);
    }
    standings();
    if (state_ >= 3) opponentsFinishing();
}

// ---- Crash Course (crash-course.md) ----

phys::Car* RaceSession::targetCar() const {
    int S = 0;
    for (int j = 0; j < event_ && j < int(events_.size()); ++j) S += events_[size_t(j)].opponents;
    const auto& v = opps_.opponents();
    if (S < 0 || size_t(S) >= v.size()) return nullptr;
    return v[size_t(S)].car.get();
}

void RaceSession::setupEvent(int e) {
    if (e < 0 || size_t(e) >= events_.size()) return;
    event_ = e;
    const CrashEvent& ev = events_[size_t(e)];
    if (ev.checkpoints == 0) {
        list_.clear();
        list_.setDisabled(true);
    } else {
        const auto wf = parseWaypoints(textOf(s_.vfs->open("race/" + s_.cityEntry.raceDir + "/" + ev.filename + ".csv")), 32, false);
        list_.set(wf.points, ev.kind == 0 ? Layout::AnyOrderFinish : Layout::FixedOrder, 1, ev.onlyTarget != 0);
    }
    countdownLimit_ = ev.timeLimit;
    switch (ev.kind) {
        case 0: case 3: case 5: case 6: case 7:
            countdownShown_ = true;
            list_.setArrow(true);
            break;
        case 9:
            countdownShown_ = true;
            list_.setArrow(false);
            break;
        case 2:
            countdownShown_ = false;
            list_.setArrow(false);
            break;
        case 8:
            countdownShown_ = true;
            list_.setArrow(false);
            break;
        case 4:
            countdownShown_ = ev.timeLimit != 0.0f;
            list_.setArrow(true);
            break;
        default: break;  // 1: unchanged
    }
    ccMinSpeed_ = ev.misc < 1.0f ? 50.0f : ev.misc;
    state_ = 0;
    t_ = 0.0f;
}

void RaceSession::timeOut() {
    if (warning_) emit("TimerwarningStop");
    warning_ = false;
    stopClocks();
    post(244, 5.0f, false);
}

void RaceSession::coursePass() {
    ccPassed_ = true;
    emit("ccPass");
    results_.clear();
    results_.push_back(ResultRow{1, -1, 0.0f, true});
    score_ = 1;
    if (race_ <= 12) {
        RaceOutcome r;
        r.mode = mode_;
        r.race = race_;
        r.professional = pro_;
        r.car = s_.playerCar;
        r.time = 1.0f;
        r.passed = true;
        r.score = 1;
        r.place = 1;
        recordResult(r);
    }
    setNextRace();
}

void RaceSession::courseFail() {
    emit("ccFail");
    if (race_ <= 12) {
        RaceOutcome r;
        r.mode = mode_;
        r.race = race_;
        r.professional = pro_;
        r.car = s_.playerCar;
        r.time = 1.0f;
        r.passed = false;
        r.score = 1;
        r.place = 1;
        recordResult(r);
    }
}

// Goal met: pass the course at its last event, else hand over to the next one. True when the
// event changed (nothing more this frame).
bool RaceSession::goalMet() {
    if (event_ + 1 >= int(events_.size())) {
        coursePass();
        return false;
    }
    setupEvent(event_ + 1);
    firstEvent_ = false;
    return true;
}

void RaceSession::crashFrame(float F) {
    if (events_.empty()) return;
    const CrashEvent& ev = events_[size_t(event_)];
    const int kind = ev.kind;
    auto readyText = [&]() {
        switch (kind) {
            case 0: return 206;
            case 1: return 211;
            case 2: return 218;
            case 3: return 193;
            case 4: return 232;
            case 5: return 225;
            case 6: return 200;
            case 8: return 617;
            default: return 241;
        }
    };
    auto setText = [&]() {
        switch (kind) {
            case 0: return 207;
            case 1: return 212;
            case 2: return 219;
            case 3: return 194;
            case 4: return 0;
            case 5: return 226;
            case 6: return 201;
            case 8: return 618;
            default: return 242;
        }
    };
    // Kinds whose go starts the countdown (reset to the event's limit).
    auto countdownAtGo = [&]() {
        if (kind == 2) return false;
        if (kind == 4) return ev.timeLimit != 0.0f;
        return true;
    };
    switch (state_) {
        case 0:
            if (kind == 1) {
                post(210, 1.25f, true);
                resetStopClocks();
                stopCountdownToLimit();
                t_ = 5.0f;
                state_ = 1;
                emit("Startracelow");
                break;
            }
            if (kind == 6) {
                if (t_ <= 1.25f) {
                    post(199, 1.25f, true);
                    t_ = t_ + F;
                } else {
                    t_ = 5.0f;
                    state_ = 1;
                    emit("Startracelow");
                }
                break;
            }
            if (kind == 2) countdownShown_ = false;  // hidden each frame of state 0
            if (firstEvent_) {
                if (t_ <= 1.25f) {
                    // The lesson name: 545 + N in sf, 532 + N in london, nothing elsewhere.
                    const std::string& d = s_.cityEntry.raceDir;
                    if (iequals(d, "sf")) post(545 + race_, 1.25f, true);
                    else if (iequals(d, "london")) post(532 + race_, 1.25f, true);
                    t_ = t_ + F;
                } else {
                    // The kind's preparations.
                    if (kind == 0 || kind == 4 || kind == 5 || kind == 7 || kind == 9) stopCountdownToLimit();
                    t_ = (kind == 7 || kind == 9) ? 2.5f : 5.0f;
                    state_ = 1;
                    emit("Startracelow");
                }
            } else {
                resetStopClocks();
                if (kind == 7 || kind == 9 || kind == 8) stopCountdownToLimit();
                t_ = 0.0f;
                go();
                state_ = 2;
            }
            break;
        case 1:
            if (kind == 3) s_.host->hudMapOn();
            t_ = t_ - F;
            if (t_ <= 1.25f) {
                state_ = 2;
                t_ = 1.25f;
                emit("Startracelow");
            } else if (kind == 4) {
                post(232, 2.5f, true, strf("%d", int(ccMinSpeed_)));
            } else {
                post(readyText(), 1.25f, true);
            }
            break;
        case 2: {
            t_ = t_ - F;
            const bool goNow = kind == 4 ? t_ < 0.0f : t_ <= 0.0f;
            if (goNow) {
                state_ = 3;
                emit("Startracehigh");
                int goText = 243;
                switch (kind) {
                    case 0: goText = 208; break;
                    case 1: goText = 213; break;
                    case 2: goText = 220; break;
                    case 3: goText = firstEvent_ ? 195 : 652; break;
                    case 4: goText = 233; break;
                    case 5: goText = 227; break;
                    case 6: goText = 202; break;
                    case 8: goText = 619; break;
                    default: goText = 243; break;
                }
                post(goText, kind == 3 && !firstEvent_ ? 3.0f : 1.25f, true);
                // Release the racers: a first-event jump releases only the player, damage off.
                if (kind == 0 && firstEvent_) {
                    player_->setDrivable(true);
                    if (player_->gearIndex() <= 1) player_->selectGear(2);
                    if (player_->automatic()) player_->clearAutoReverse();
                } else {
                    go();
                }
                if (!(kind == 4 && ev.timeLimit == 0.0f)) startResetClocks();
                if (countdownAtGo()) {
                    countdown_.t = countdownLimit_;
                    countdown_.running = true;
                }
                if (kind == 8 && !opps_.opponents().empty()) {
                    if (phys::Car* tc = targetCar()) {
                        tc->damageTune().maxDamage = 150000.0f;
                        tc->damageTune().medDamage = 75000.0f;
                    }
                }
            } else {
                if (setText()) post(setText(), 1.25f, true);
                if (kind == 5) {
                    // Any impact wrecks the car (the limits stay for the session).
                    player_->damageTune().maxDamage = 10.0f;
                    player_->damageTune().medDamage = 5.0f;
                    player_->damageTune().impactThreshold = 0.0f;
                }
            }
            break;
        }
        case 3: crashKindChecks(F); break;
        case 4:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f) endMenu_ = true;
            break;
        case 5:
            t_ = t_ - F;
            if (t_ <= 0.0f) {
                player_->clearDamage();
                state_ = 3;
            }
            break;
        case 6:
            t_ = t_ - F;
            if (-1.0f < t_ && t_ <= 0.0f && !endMenu_ && !resultsShown_) resultsShown_ = true;
            break;
        case 69:
            t_ = t_ - F;
            if (t_ < 0.0f) {
                ccFinishedMark_ = true;
                state_ = 6;
                s_.host->postRaceCamera();
                disablePlayer();
                clock_.running = false;
                countdown_.running = false;
                post(204, 5.0f, false);
                if (goalMet()) return;
                emit("Endofracetag");
                t_ = 5.0f;
            }
            break;
        default: break;
    }
}

void RaceSession::crashKindChecks(float F) {
    const CrashEvent& ev = events_[size_t(event_)];
    const int kind = ev.kind;
    auto stopWarning = [&]() {
        if (warning_) emit("TimerwarningStop");
        warning_ = false;
    };
    auto startWarning = [&]() {
        if (!warning_) emit("TimerwarningStart");
        warning_ = true;
    };
    auto finishBits = [&]() {  // post-race camera, disable the player, stop the race clock and the countdown
        s_.host->postRaceCamera();
        disablePlayer();
        clock_.running = false;
        countdown_.running = false;
    };
    const float speedMph = player_->speedMph();
    switch (kind) {
        case 0:
            if (list_.done()) {
                ccFinishedMark_ = true;
                state_ = 6;
                finishBits();
                if (goalMet()) return;
                emit("Endofracetag");
                post(209, 5.0f, false);
                t_ = 5.0f;
            } else if (countdown_.t <= 0.0f) {
                stopWarning();
                list_.setOff(true);
                list_.hideLast();
                courseFail();
                emit("Youlose");
                post(614, 5.0f, false);
                stopClocks();
                disablePlayer();
                t_ = 5.0f;
                state_ = 4;
            } else if (damageRules() && wrecked()) {
                stopWarning();
                list_.setOff(true);
                list_.hideLast();
                emit("Damgelose");
                notDrivable();
                post(615, 5.0f, false);
                courseFail();
                s_.host->postRaceCamera();
                stopClocks();
                countdown_.running = false;
                t_ = 5.0f;
                state_ = 4;
            }
            break;
        case 1:
            if (countdown_.t < 10.0f && !ccFinishedMark_) startWarning();
            if (countdown_.t <= 0.0f && !ccFinishedMark_) {
                ccFinishedMark_ = true;
                state_ = 4;
                finishBits();
                post(214, 1.25f, false);
                stopWarning();
                t_ = 5.0f;
            }
            if (list_.done()) {
                post(215, 1.25f, false);
                ccFinishedMark_ = true;
                state_ = 6;
                finishBits();
                t_ = 3.0f;
                stopWarning();
            }
            if (damageRules() && wrecked()) {
                emit("Messagenote");
                post(216, 5.0f, false);
                t_ = 5.0f;
                state_ = 5;
            }
            break;
        case 2: {
            phys::Car* tc = targetCar();
            if (wrecked()) {
                t_ = 3.0f;
                state_ = 5;
            } else if (tc) {
                const float d = dist2(player_->body().p, tc->body().p);
                int S = 0;
                for (int j = 0; j < event_; ++j) S += events_[size_t(j)].opponents;
                const bool fin = ai::OpponentSystem::finishTest(opps_.opponents()[size_t(S)], ai::opponentFinishLine());
                if ((fin || list_.done()) && d < 100.0f) {
                    t_ = 5.0f;
                    if (goalMet()) return;
                    ccFinishedMark_ = true;
                    post(221, 5.0f, false);
                    state_ = 6;
                    clock_.running = false;
                    disablePlayer();
                    s_.host->postRaceCamera();
                }
            }
            if (tc && dist2(player_->body().p, tc->body().p) > 10000.0f) {
                t_ = 5.0f;
                post2(222, 223, 5.0f);
                disablePlayer();
                courseFail();
                state_ = 4;
            }
            break;
        }
        case 3:
            if (wrecked()) {
                t_ = 3.0f;
                state_ = 5;
            }
            if (list_.done()) {
                ccFinishedMark_ = true;
                state_ = 6;
                finishBits();
                if (!s_.host->copInSight()) {
                    post(196, 5.0f, false);
                    if (goalMet()) return;
                    emit("Endofracetag");
                } else {
                    post(197, 5.0f, false);
                    courseFail();
                    emit("Youlose");
                    list_.setOff(true);
                    list_.hideLast();
                    stopClocks();
                    state_ = 4;
                }
                t_ = 5.0f;
            } else if (countdown_.t <= 0.0f) {
                list_.setOff(true);
                list_.hideLast();
                emit("Youlose");
                post(198, 5.0f, false);
                courseFail();
                stopClocks();
                disablePlayer();
                t_ = 5.0f;
                state_ = 4;
            }
            break;
        case 4: {
            const float S = ccMinSpeed_, T = 1.0f;
            if (list_.done()) {
                ccFinishedMark_ = true;
                s_.host->postRaceCamera();
                disablePlayer();
                clock_.running = false;
                oppClock_.running = false;
                t_ = 5.0f;
                if (s_.host->copInSight()) {
                    post(197, 5.0f, false);
                    courseFail();
                    emit("Youlose");
                    state_ = 4;
                    return;
                }
                state_ = 6;
                if (goalMet()) return;
                emit("Endofracetag");
                clock_.running = false;
                countdown_.running = false;
                post(234, 5.0f, false);
                return;
            }
            if (ev.timeLimit != 0.0f && countdown_.t <= 0.0f) {
                timeOut();
                list_.setOff(true);
                list_.hideLast();
                courseFail();
                emit("Youlose");
                stopClocks();
                disablePlayer();
                t_ = 5.0f;
                state_ = 4;
                return;
            }
            if (!ccReached_) {
                if (list_.count() >= 2) {
                    post2(235, 236, 3.0f);
                    disablePlayer();
                    clock_.running = false;
                    countdown_.running = false;
                    t_ = 5.0f;
                    courseFail();
                    state_ = 4;
                }
                if (!ccReached_ && speedMph > S) {
                    ccReached_ = true;
                    post(237, 3.0f, false);
                }
            }
            if (ccUnder_ > 0.0f && speedMph > S) ccUnder_ = 0.0f;
            if (ccReached_ && !(speedMph >= S)) {
                ccUnder_ = F + ccUnder_;
                if (ccUnder_ > T) {
                    ccReached_ = false;
                    post2(238, 239, 5.0f);
                    disablePlayer();
                    t_ = 5.0f;
                    courseFail();
                    clock_.running = false;
                    countdown_.running = false;
                    state_ = 4;
                    return;
                }
            }
            if (wrecked()) {
                t_ = 5.0f;
                post(240, 5.0f, false);
                courseFail();
                state_ = 4;
            }
            break;
        }
        case 5:
            if (countdown_.t < 10.0f && !ccFinishedMark_) startWarning();
            if (countdown_.t <= 0.0f && !ccFinishedMark_) {
                ccFinishedMark_ = true;
                state_ = 4;
                finishBits();
                post(228, 1.25f, false);
                courseFail();
                stopWarning();
                t_ = 5.0f;
                return;
            }
            if (list_.done()) {
                ccFinishedMark_ = true;
                state_ = 6;
                finishBits();
                if (goalMet()) return;
                post(229, 1.25f, false);
                t_ = 3.0f;
                stopWarning();
            }
            if (damageRules() && wrecked()) {
                post(230, 5.0f, false);
                emit("Youlose");
                courseFail();
                t_ = 5.0f;
                state_ = 4;
            }
            break;
        case 6:
            if (wrecked()) {
                t_ = 3.0f;
                state_ = 5;
            }
            if (list_.done()) {
                state_ = 69;
                t_ = 2.0f;
                return;
            }
            if (countdown_.t <= 0.0f) {
                list_.setOff(true);
                list_.hideLast();
                emit("Youlose");
                courseFail();
                post(203, 5.0f, false);
                stopClocks();
                disablePlayer();
                t_ = 5.0f;
                state_ = 4;
            }
            break;
        case 8: {
            phys::Car* tc = targetCar();
            const auto& v = opps_.opponents();
            int S = 0;
            for (int j = 0; j < event_; ++j) S += events_[size_t(j)].opponents;
            if (tc && !v.empty() && v[0].car->tune().damage.maxDamage <= tc->damage().damage) {
                tc->setDrivable(false);
                ccFinishedMark_ = true;
                t_ = 5.0f;
                if (goalMet()) return;
                post(620, 5.0f, false);
                state_ = 6;
                stopClocks();
                countdown_.running = false;
                disablePlayer();
                s_.host->postRaceCamera();
            } else if (tc && ai::OpponentSystem::finishTest(opps_.opponents()[size_t(S)], ai::opponentFinishLine())) {
                t_ = 5.0f;
                post2(621, 622, 5.0f);
                stopClocks();
                countdown_.running = false;
                disablePlayer();
                courseFail();
                state_ = 4;
            } else if (countdown_.t <= 0.0f) {
                timeOut();
                courseFail();
                emit("Youlose");
                stopClocks();
                countdown_.running = false;
                disablePlayer();
                t_ = 5.0f;
                state_ = 4;
            } else if (damageRules() && wrecked()) {
                t_ = 3.0f;
                state_ = 5;
            }
            break;
        }
        default:  // 7 and 9: checkpoints
            if (list_.done()) {
                stopWarning();
                if (goalMet()) return;
                emit("Endofracetag");
                ccFinishedMark_ = true;
                state_ = 6;
                finishBits();
                post(609, 5.0f, false);
                t_ = 5.0f;
            } else if (countdown_.t <= 0.0f) {
                timeOut();
                ccFinishedMark_ = false;
                list_.setOff(true);
                list_.hideLast();
                courseFail();
                emit("Youlose");
                stopClocks();
                disablePlayer();
                t_ = 5.0f;
                state_ = 4;
            } else if (damageRules() && wrecked()) {
                stopWarning();
                list_.setOff(true);
                list_.hideLast();
                emit("Damgelose");
                notDrivable();
                post(245, 5.0f, false);
                courseFail();
                s_.host->postRaceCamera();
                stopClocks();
                countdown_.running = false;
                t_ = 5.0f;
                state_ = 4;
            }
            break;
    }
}

}  // namespace game
}  // namespace cc
