// Speech and one-shots (spec/audio/speech.md): the speech player (entries, clip choice, one
// streamed voice, the delay queue), the race announcer and the Crash Course instructor, and the
// race-mode one-shot bank. Everything here is 2D.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/sound_system.h"

namespace cc {
class Vfs;

namespace audio {

class SpeechPlayer {
public:
    SpeechPlayer(SoundSystem& sys, const std::string& subPath);
    struct Entry {
        std::string prefix;
        float end = 0, add = 0;
        int last = 0;  // the clip this entry played last
    };
    int addEntry(const std::string& prefix, float end, float add);
    int entryCount() const { return int(entries_.size()); }
    const Entry& entry(int i) const { return entries_[size_t(i)]; }
    void addQueueSlot() { queue_.emplace_back(); }
    int queueSlots() const { return int(queue_.size()); }
    // Delay 0: stop and play at once; > 0: into the first free queue slot (dropped if none).
    // `clip` > 0 plays that clip number (no random choice: the Crash Course gate lines).
    void play(int entry, float delay, int clip = 0);
    void stop();
    // Every frame, also while paused.
    void update(float dt);
    bool speaking() const;
    // The clip number an entry would pick now (and remember).
    int chooseClip(int entry);
    const std::string& lastClipFile() const { return lastFile_; }

private:
    void start(int entry, int clip = 0);
    std::string subPath_;
    std::vector<Entry> entries_;
    struct Slot {
        int entry = -1;
        float delay = 0;
        int clip = 0;
    };
    std::vector<Slot> queue_;
    std::unique_ptr<Sample> voice_;
    bool waiting_ = false;   // a clip file still being fetched
    float waited_ = 0;
    std::string lastFile_;
};

// Categories of race and Crash Course speech.
struct SpeechCategories {
    struct Range {
        int first = -1, last = -1;
    };
    std::map<std::string, Range> ranges;  // PRERACE, UNLOCKRACE, UNLOCKVEHICLE
    std::map<std::string, int> single;    // the one-entry categories
    int unlockLines = 0;                  // unlock category lines seen (queue slots)
    int entry(const std::string& name) const {
        auto it = single.find(name);
        return it == single.end() ? -1 : it->second;
    }
    Range range(const std::string& name) const {
        auto it = ranges.find(name);
        return it == ranges.end() ? Range() : it->second;
    }
};

// Loads one speech data file into the player (every row becomes an entry). False if missing.
// `crashCourse` uses the Crash Course categories; `entryPrefix` goes before every row prefix.
bool loadSpeechFile(const Vfs& vfs, const std::string& path, const std::string& entryPrefix, bool crashCourse,
                    SpeechPlayer& p, SpeechCategories& cats);

class RaceSpeech {
public:
    explicit RaceSpeech(SoundSystem& sys) : sys_(sys) {}
    // mode: 0 Cruise, 1 Checkpoint, 3 Circuit, 4 Blitz. False: no race speech in this game.
    bool setup(const Vfs& vfs, const std::string& city, int mode, int weather, int timeOfDay, const std::string& car,
               bool minimalInstall);
    bool active() const { return player_ != nullptr; }
    // Game start or restart.
    void preRace();
    void finalCheckpoint();
    void damagePenalty();  // Blitz only
    // Results for place p (1 = first) among n (the opponent-count setting).
    void results(int place, int n);
    void unlockedCar(const Vfs& vfs, const std::string& car);
    void unlockedPaint(const Vfs& vfs, const std::string& car);
    void update(float dt) {
        if (player_) player_->update(dt);
    }
    const std::string& voiceName() const { return voice_; }
    SpeechPlayer* player() const { return player_.get(); }
    const SpeechCategories& categories() const { return cats_; }

private:
    int rangeDraw(const SpeechCategories::Range& r) const;
    SoundSystem& sys_;
    std::unique_ptr<SpeechPlayer> player_;
    SpeechCategories cats_;
    std::string voice_, folder_;
};

// The Crash Course instructor (speech.md, "Crash Course").
class CrashCourseSpeech {
public:
    explicit CrashCourseSpeech(SoundSystem& sys) : sys_(sys) {}
    // `course`: the 0-based race index. False: no instructor (other cities, missing file).
    bool setup(const Vfs& vfs, const std::string& city, int course);
    bool active() const { return player_ != nullptr; }
    void preRace();
    void gatePassed(int gate);
    void result(bool passed);
    void update(float dt) {
        if (player_) player_->update(dt);
    }
    SpeechPlayer* player() const { return player_.get(); }
    const std::vector<int>& gateClips() const { return gates_; }

private:
    SoundSystem& sys_;
    std::unique_ptr<SpeechPlayer> player_;
    SpeechCategories cats_;
    std::vector<int> gates_;  // course 4: the clip per gate (−1 none); empty: no gate list
};

// The race-mode one-shot bank and the gate samples (speech.md, "Race-mode one-shots"): the
// events of that section (countdown, go, finish, damaged out, timer warning, gate passed, HUD
// message) for the race modes to call, and plain playback by name (`Startracelow`, `Waypoint` …).
class RaceOneShots {
public:
    explicit RaceOneShots(SoundSystem& sys) : sys_(sys) {}
    // The bank of a game mode (0 Cruise, 1 Checkpoint, 3 Circuit, 4 Blitz, 6 Crash Course).
    void setup(int mode);
    int mode() const { return mode_; }

    // Countdown of `seconds` (2.5 Checkpoint, Circuit and the Crash Course checkpoint events; 5.0
    // Blitz and the other counted Crash Course events): `Startracelow` now and again with 1.25 s
    // left, `Startracehigh` at 0 (GO). update() runs it; true from GO on.
    void countdown(float seconds);
    bool update(float dt);
    bool counting() const { return count_ > 0.0f; }
    // GO without a count (later Crash Course events).
    void go() { play("Startracehigh"); }
    // Finish: first place (Blitz: in time) or not.
    void finish(bool won);
    // The player's car damaged out; `crashKind` is the Crash Course event kind (-1 otherwise).
    void damagedOut(int crashKind = -1);
    // Crash Course: the car landed in water.
    void crashCourseWater() { play("Damgelose"); }
    // Timer warning (Blitz; Crash Course kinds 1 and 5): every frame while the race runs with the
    // clock's time left; stopTimerWarning() when the race ends or the time runs out.
    void timerWarning(float timeLeft, float dt);
    void stopTimerWarning();
    // A gate passed: Circuit plays `Lastwaypoint` for a completed lap; Checkpoint plays nothing for
    // the gate that leaves only the finish; otherwise `Waypoint`.
    void gatePassed(bool lapCompleted, bool leavesOnlyFinish);
    // The HUD message chime.
    void message() { play("Messagenote"); }

    void play(const std::string& name);
    void playLooped(const std::string& name);
    void stop(const std::string& name);
    bool playing(const std::string& name) const;
    bool has(const std::string& name) const;

private:
    Sample* find(const std::string& name, int& inst) const;
    SoundSystem& sys_;
    int mode_ = 0;
    std::unique_ptr<Sample> bank_;
    std::vector<std::string> names_;
    std::unique_ptr<Sample> waypoint_, lastWaypoint_;
    float count_ = 0, countTotal_ = 0;
    bool secondLow_ = false;
    float warnAcc_ = 1.0f;
    bool warnLooped_ = false;
};

}  // namespace audio
}  // namespace cc
