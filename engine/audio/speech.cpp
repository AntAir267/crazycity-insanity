#include "audio/speech.h"

#include <cmath>

#include "audio/sound_random.h"
#include "audio/text_reader.h"
#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {
namespace audio {

// ---- SpeechPlayer ----

SpeechPlayer::SpeechPlayer(SoundSystem& sys, const std::string& subPath) : subPath_(subPath) {
    voice_ = std::make_unique<Sample>(sys, false);
    voice_->setVolume(1.0f);
    queue_.emplace_back();  // a set starts with one queue slot
}

int SpeechPlayer::addEntry(const std::string& prefix, float end, float add) {
    Entry e;
    e.prefix = prefix;
    e.end = end;
    e.add = add;
    entries_.push_back(e);
    return int(entries_.size()) - 1;
}

int SpeechPlayer::chooseClip(int i) {
    Entry& e = entries_[size_t(i)];
    int k = int(std::trunc(rndS(e.add + 1.0f, e.end + 0.99f)));
    if (k == e.last && e.end > 1.0f) {
        k = k + 1;
        if (k > int(std::trunc(e.end))) k = 1;
    }
    e.last = k;
    return k;
}

void SpeechPlayer::start(int i, int clip) {
    if (i < 0 || i >= int(entries_.size())) return;
    const int k = clip > 0 ? clip : chooseClip(i);
    const std::string name = entries_[size_t(i)].prefix + strf("%02d", k);
    voice_->stop();
    lastFile_ = soundPath(name, subPath_, ".11K");
    voice_->setFile(0, lastFile_);
    voice_->setVolume(1.0f);
    if (voice_->pending()) {
        // Streamed from a lazily read archive: it starts once its bytes arrive.
        waiting_ = true;
        waited_ = 0;
        return;
    }
    waiting_ = false;
    voice_->play();  // a missing clip file simply plays nothing
}

void SpeechPlayer::play(int entry, float delay, int clip) {
    if (entry < 0 || entry >= int(entries_.size())) return;
    if (delay == 0.0f) {
        stop();
        start(entry, clip);
        return;
    }
    for (Slot& s : queue_)
        if (s.entry < 0) {
            s.entry = entry;
            s.delay = delay;
            s.clip = clip;
            return;
        }
}

void SpeechPlayer::stop() {
    for (Slot& s : queue_) s = Slot();
    voice_->stop();
    waiting_ = false;
}

bool SpeechPlayer::speaking() const { return waiting_ || voice_->playing(); }

void SpeechPlayer::update(float dt) {
    if (waiting_) {
        waited_ += dt;
        if (voice_->loaded()) {
            waiting_ = false;
            voice_->play();
        } else if (!voice_->pending() || waited_ > 3.0f) {
            waiting_ = false;  // missing, or too late to be worth saying
        }
    }
    for (Slot& s : queue_) {
        if (s.entry < 0) continue;
        s.delay = s.delay - dt;
        if (!speaking() && s.delay <= 0.0f) {
            const int e = s.entry, clip = s.clip;
            s = Slot();
            start(e, clip);
            break;  // the rest of the queue waits for the next frame
        }
    }
}

// ---- data files ----

namespace {
const char* const kRaceRanges[] = {"PRERACE", "UNLOCKRACE", "UNLOCKVEHICLE"};
const char* const kRaceSingles[] = {"UNLOCKTEXTURE",      "FINALCHECKPOINT",   "FINALLAP",          "RACEPROGRESS",
                                    "RESULTSPOOR",        "RESULTSMID",        "RESULTSWIN",        "VEHICLERESULTSPOOR",
                                    "VEHICLERESULTSMID",  "VEHICLERESULTSWIN", "VEHICLEPRERACE",    "TIMEOFDAY",
                                    "WEATHER",            "DAMAGEPENALTY"};
const char* const kCrashSingles[] = {"PRERACE", "UNLOCK", "CHECKPOINT", "RESULTSPOOR", "RESULTSWIN"};

bool startsWithName(const std::string& field, const char* name) { return istartsWith(field, name); }
}  // namespace

bool loadSpeechFile(const Vfs& vfs, const std::string& path, const std::string& entryPrefix, bool crashCourse,
                    SpeechPlayer& p, SpeechCategories& cats) {
    LineReader r = openCsv(vfs, path);
    if (!r.valid()) return false;
    std::string raw;
    r.read(raw);  // title
    std::string range, single;
    while (r.read(raw)) {
        const auto f = splitFields(cutLine(raw));
        if (f.empty()) continue;
        const std::string& first = f[0];
        const size_t h = toLower(first).find("header");
        if (h != std::string::npos && h > 0) {
            range.clear();
            single.clear();
            if (!crashCourse) {
                for (const char* n : kRaceRanges)
                    if (startsWithName(first, n)) range = n;
                if (range.empty())
                    for (const char* n : kRaceSingles)
                        if (startsWithName(first, n)) single = n;
                if (!range.empty()) {
                    cats.ranges[range] = SpeechCategories::Range();
                    if (range != "PRERACE") {
                        // Each unlock category line adds a queue slot from the second one on.
                        if (cats.unlockLines >= 1) p.addQueueSlot();
                        ++cats.unlockLines;
                    }
                }
            } else {
                for (const char* n : kCrashSingles)
                    if (startsWithName(first, n)) single = n;
                if (single == "UNLOCK") {
                    if (cats.unlockLines >= 1) p.addQueueSlot();
                    ++cats.unlockLines;
                }
            }
            if (!single.empty()) cats.single.erase(single);
            continue;
        }
        const float end = fieldF(f, 1), add = fieldF(f, 2);
        const int idx = p.addEntry(entryPrefix + first, end, add);
        if (!range.empty()) {
            SpeechCategories::Range& rg = cats.ranges[range];
            if (rg.first < 0) rg.first = idx;
            rg.last = idx;
        }
        if (!single.empty()) {
            cats.single[single] = idx;
            if (crashCourse && single == "PRERACE") {
                const int n = int(std::trunc(end)) - (int(std::trunc(add)) + 1) + 1;
                for (int i = 0; i < n; ++i) p.addQueueSlot();
            }
            single.clear();
        }
    }
    return true;
}

// ---- race speech ----

bool RaceSpeech::setup(const Vfs& vfs, const std::string& cityName, int mode, int weather, int timeOfDay,
                       const std::string& car, bool minimalInstall) {
    player_.reset();
    cats_ = SpeechCategories();
    if (!(mode == 0 || mode == 1 || mode == 3 || mode == 4)) return false;
    const std::string city = toLower(cityName);
    if (city != "sf" && city != "london") return false;  // no speech in any other city
    LineReader r = openCsv(vfs, "aud/spchdata/" + city);
    if (!r.valid()) return false;
    std::string raw;
    r.read(raw);
    r.read(raw);
    const float n = fieldF(splitFields(cutLine(raw)), 0);
    r.read(raw);
    r.read(raw);
    const std::string prefix = fieldS(splitFields(cutLine(raw)), 0);
    int voice = int(std::trunc(rndS(1.0f, n + 0.99f)));
    if (minimalInstall) voice = 1;
    if (city == "sf" && voice == 3) voice = 4;
    voice_ = prefix + std::to_string(voice);
    folder_ = "aud/spchdata/" + toLower(voice_) + "/";
    player_ = std::make_unique<SpeechPlayer>(sys_, "aud11\\" + voice_);
    const char* modeFile = mode == 0 ? "cruise" : mode == 1 ? "checkpoint" : mode == 3 ? "circuit" : "blitz";
    const char* weatherFiles[] = {"weaclr_prerace", "weacldy_prerace", "weafog_prerace", "wearain_prerace"};
    auto load = [&](const std::string& f) { loadSpeechFile(vfs, folder_ + f, voice_, false, *player_, cats_); };
    load(modeFile);
    if (weather >= 0 && weather <= 3) load(weatherFiles[weather]);
    if (timeOfDay == 0) load("timemorn_prerace");
    else if (timeOfDay == 1) load("timenoon_prerace");
    else if (timeOfDay == 2 && weather == 0) load("timeeve_prerace");
    else if (timeOfDay == 3) load("timenight_prerace");
    load(car + "_prerace");
    load(car + "_results");
    load("finallap");
    load(mode == 0 ? "cruisedamage" : "racedamage");
    logInfo("race speech: voice %s, %d lines", voice_.c_str(), player_->entryCount());
    return true;
}

int RaceSpeech::rangeDraw(const SpeechCategories::Range& rg) const {
    return int(std::trunc(rndS(float(rg.first), float(rg.last) + 0.99f)));
}

void RaceSpeech::preRace() {
    if (!player_) return;
    const double x = rndS(11.5f);
    int e = -1;
    if (x <= 7.5) e = rangeDraw(cats_.range("PRERACE"));
    else if (x <= 8.5) e = cats_.entry("TIMEOFDAY");
    else if (x <= 9.5) e = cats_.entry("WEATHER");
    else {
        e = cats_.entry("VEHICLEPRERACE");
        if (e < 0) e = rangeDraw(cats_.range("PRERACE"));
    }
    player_->stop();
    if (e >= 0) player_->play(e, 1.5f);
}

void RaceSpeech::finalCheckpoint() {
    if (!player_) return;
    player_->stop();
    player_->play(cats_.entry("FINALCHECKPOINT"), 0.0f);
}

void RaceSpeech::damagePenalty() {
    if (!player_) return;
    player_->stop();
    player_->play(cats_.entry("DAMAGEPENALTY"), 0.0f);
}

void RaceSpeech::results(int p, int n) {
    if (!player_) return;
    int e = -1;
    if (p == 1) {
        const int v = cats_.entry("VEHICLERESULTSWIN");
        e = (rndS(10.0f) > 5.0 && v >= 0) ? v : cats_.entry("RESULTSWIN");
    } else if (0 < p - 1 && p - 1 < n / 2) {
        const int v = cats_.entry("VEHICLERESULTSMID");
        e = (rndS(10.0f) > 5.0 && v >= 0) ? v : cats_.entry("RESULTSMID");
    } else {
        const int v = cats_.entry("VEHICLERESULTSPOOR");
        e = (rndS(10.0f) > 8.0 && v >= 0) ? v : cats_.entry("RESULTSPOOR");
    }
    player_->stop();
    if (e >= 0) player_->play(e, 0.0f);
}

void RaceSpeech::unlockedCar(const Vfs& vfs, const std::string& car) {
    if (!player_) return;
    loadSpeechFile(vfs, folder_ + car + "_unlock", voice_, false, *player_, cats_);
    const SpeechCategories::Range rg = cats_.range("UNLOCKVEHICLE");
    player_->play(rangeDraw(rg), 0.1f);
}

void RaceSpeech::unlockedPaint(const Vfs& vfs, const std::string& car) {
    if (!player_) return;
    loadSpeechFile(vfs, folder_ + car + "_texture", voice_, false, *player_, cats_);
    player_->stop();
    player_->play(cats_.entry("UNLOCKTEXTURE"), 0.0f);
}

// ---- Crash Course ----

bool CrashCourseSpeech::setup(const Vfs& vfs, const std::string& cityName, int course) {
    player_.reset();
    cats_ = SpeechCategories();
    gates_.clear();
    const std::string city = toLower(cityName);
    if (city != "sf" && city != "london") return false;
    const char l = city == "london" ? 'l' : 's';
    const std::string folder = std::string("aud/spchdata/cc") + l + "/";
    player_ = std::make_unique<SpeechPlayer>(sys_, std::string("aud11\\CC") + char(l - 32));
    if (!loadSpeechFile(vfs, folder + "cc" + l + std::to_string(course), "", true, *player_, cats_)) {
        player_.reset();
        return false;
    }
    if (course == 4) {
        loadSpeechFile(vfs, folder + "cc_cpoint_waveinfo", "", true, *player_, cats_);
        LineReader r = openCsv(vfs, folder + "cc_cpoint_indexinfo");
        if (r.valid()) {
            std::string raw;
            r.read(raw);
            while (r.read(raw)) {
                const auto f = splitFields(cutLine(raw));
                if (!f.empty()) gates_.push_back(atoiC(f[0]));
            }
        }
    }
    logInfo("Crash Course instructor: course %d, %d lines", course, player_->entryCount());
    return true;
}

void CrashCourseSpeech::preRace() {
    if (!player_) return;
    player_->stop();
    player_->play(cats_.entry("PRERACE"), 1.5f);
    if (!gates_.empty() && gates_[0] >= 0) player_->play(cats_.entry("CHECKPOINT"), 1.51f, gates_[0]);
}

void CrashCourseSpeech::gatePassed(int g) {
    if (!player_ || g < 0 || size_t(g) >= gates_.size() || gates_[size_t(g)] < 0) return;
    player_->play(cats_.entry("CHECKPOINT"), 0.01f, gates_[size_t(g)]);
}

void CrashCourseSpeech::result(bool passed) {
    if (!player_) return;
    player_->stop();
    player_->play(cats_.entry(passed ? "RESULTSWIN" : "RESULTSPOOR"), 0.0f);
}

// ---- one-shots ----

void RaceOneShots::setup(int mode) {
    mode_ = mode;
    count_ = countTotal_ = 0;
    warnAcc_ = 1.0f;
    warnLooped_ = false;
    names_.clear();
    struct Inst {
        const char* name;
        float volume;
    };
    std::vector<Inst> list;
    const bool race = mode == 1 || mode == 3 || mode == 4 || mode == 6;
    if (race) {
        list.push_back({"Startracelow", 0.9f});
        list.push_back({"Startracehigh", 0.9f});
        list.push_back({"Endofracetag", 0.925f});
        list.push_back({"Youlose", 0.925f});
        if (mode == 3) {
            list.push_back({"Messagenote", 0.9f});
        } else {
            list.push_back({"Damgelose", 0.925f});
            list.push_back({"Messagenote", 0.9f});
            if (mode == 4 || mode == 6) list.push_back({"Timerwarning", 0.9f});
        }
    } else if (mode == 0) {
        list.push_back({"Messagenote", 1.0f});  // Cruise: only the chime, volume left at full
    } else if (mode == 2 || mode == 5) {
        // Network games (profile modes 2 Cops and Robbers, 5 multiplayer Cruise): the GO sound.
        list.push_back({"Startracehigh", mode == 2 ? 0.98f : 0.9f});
    }
    bank_.reset();
    waypoint_.reset();
    lastWaypoint_.reset();
    if (list.empty()) return;
    bank_ = std::make_unique<Sample>(sys_, false, int(list.size()));
    bank_->setPriority(23);
    for (size_t i = 0; i < list.size(); ++i) {
        bank_->setFile(int(i), soundPath(list[i].name));
        bank_->setCurrent(int(i));
        bank_->setVolume(list[i].volume);
        names_.push_back(list[i].name);
    }
    bank_->setCurrent(0);
    if (race) {
        const float gv = mode == 6 ? 0.91f : 0.95f;
        waypoint_ = std::make_unique<Sample>(sys_, false);
        waypoint_->setFile(0, soundPath("Waypoint"));
        waypoint_->setVolume(gv);
        waypoint_->setPriority(23);
        lastWaypoint_ = std::make_unique<Sample>(sys_, false);
        lastWaypoint_->setFile(0, soundPath("Lastwaypoint"));
        lastWaypoint_->setVolume(gv);
        lastWaypoint_->setPriority(23);
    }
}

void RaceOneShots::countdown(float seconds) {
    countTotal_ = count_ = seconds;
    secondLow_ = false;
    play("Startracelow");
}

bool RaceOneShots::update(float dt) {
    if (!(count_ > 0.0f)) return false;
    count_ = count_ - dt;
    if (!secondLow_ && count_ <= 1.25f) {
        secondLow_ = true;
        if (count_ > 0.0f) play("Startracelow");
    }
    if (count_ <= 0.0f) {
        count_ = 0.0f;
        play("Startracehigh");
        return true;
    }
    return false;
}

void RaceOneShots::finish(bool won) { play(won ? "Endofracetag" : "Youlose"); }

void RaceOneShots::damagedOut(int kind) {
    if (mode_ == 6) {
        if (kind == 0 || kind == 7 || kind == 9) play("Damgelose");
        else if (kind == 1) play("Messagenote");
        else if (kind == 5) play("Youlose");
        return;
    }
    // Instance 4: Damgelose in Checkpoint and Blitz, the message chime in Circuit.
    if (bank_ && bank_->instanceCount() > 4) {
        bank_->setCurrent(4);
        bank_->play();
    }
}

void RaceOneShots::timerWarning(float left, float dt) {
    if (!(left < 10.0f) || !(left > 0.0f)) return;
    if (left > 3.0f) {
        if (warnAcc_ >= 1.0f) {
            warnAcc_ = 0.0f;
            if (!playing("Timerwarning")) play("Timerwarning");
        }
        warnAcc_ += dt;
    } else if (!warnLooped_) {
        warnLooped_ = true;
        stop("Timerwarning");
        playLooped("Timerwarning");
    }
}

void RaceOneShots::stopTimerWarning() {
    stop("Timerwarning");
    warnAcc_ = 1.0f;
    warnLooped_ = false;
}

void RaceOneShots::gatePassed(bool lapCompleted, bool leavesOnlyFinish) {
    if (mode_ == 3 && lapCompleted) play("Lastwaypoint");
    else if (mode_ == 1 && leavesOnlyFinish) return;
    else play("Waypoint");
}

Sample* RaceOneShots::find(const std::string& name, int& inst) const {
    inst = 0;
    if (iequals(name, "Waypoint")) return waypoint_.get();
    if (iequals(name, "Lastwaypoint")) return lastWaypoint_.get();
    for (size_t i = 0; i < names_.size(); ++i)
        if (iequals(names_[i], name)) {
            inst = int(i);
            return bank_.get();
        }
    return nullptr;
}

bool RaceOneShots::has(const std::string& name) const {
    int i;
    return find(name, i) != nullptr;
}

void RaceOneShots::play(const std::string& name) {
    int i;
    if (Sample* s = find(name, i)) {
        s->setCurrent(i);
        s->play();  // an instance that still plays carries on (a sample with several instances)
    }
}

void RaceOneShots::playLooped(const std::string& name) {
    int i;
    if (Sample* s = find(name, i)) {
        s->setCurrent(i);
        if (!s->playing()) s->playLooped();
    }
}

void RaceOneShots::stop(const std::string& name) {
    int i;
    if (Sample* s = find(name, i)) {
        s->setCurrent(i);
        s->stop();
    }
}

bool RaceOneShots::playing(const std::string& name) const {
    int i;
    if (Sample* s = find(name, i)) {
        s->setCurrent(i);
        return s->playing();
    }
    return false;
}

}  // namespace audio
}  // namespace cc
