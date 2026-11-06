#include "audio/world_audio.h"

#include <cmath>

#include "audio/sound_random.h"
#include "audio/text_reader.h"
#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {
namespace audio {

namespace {
constexpr float kAmbientDoppler = 0.017631503f;  // 1/56.7166633605957, float32 0x3C906FF1
}

AmbientData loadAmbientFile(const Vfs& vfs, const std::string& name) {
    AmbientData d;
    LineReader r = openCsv(vfs, "aud/ambient/" + name);
    if (!r.valid()) return d;
    d.found = true;
    std::string raw;
    r.read(raw);
    if (r.read(raw)) {
        const auto f = splitFields(cutLine(raw));
        d.minD = fieldF(f, 0);
        d.maxD = fieldF(f, 1);
        d.priority = fieldI(f, 2);
        d.area = fieldI(f, 3);
    }
    r.read(raw);
    bool points = false;
    while (r.read(raw)) {
        const auto f = splitFields(cutLine(raw));
        if (f.empty()) continue;  // a blank line (MM2 would crash): skipped
        if (!points) {
            if (f[0] == "VECTORPOINTS") {
                points = true;
                r.read(raw);  // header
                continue;
            }
            AmbientRow row;
            row.name = f[0];
            row.volume = fieldF(f, 1);
            row.type = fieldI(f, 2);
            row.low = fieldF(f, 3);
            row.high = fieldF(f, 4);
            row.active = fieldI(f, 5);
            row.minSpeed = fieldF(f, 6);
            row.maxSpeed = fieldF(f, 7);
            row.doppler = fieldI(f, 8);
            d.rows.push_back(row);
        } else {
            d.points.push_back(vec3(fieldF(f, 0), fieldF(f, 1), fieldF(f, 2)));
        }
    }
    return d;
}

std::vector<std::string> ambientList(const Vfs& vfs, const std::string& city) {
    std::vector<std::string> out;
    std::string list;
    if (city == "london") list = "aud/ambient/londonambientcontainer";
    else if (city == "sf") list = "aud/ambient/sfambientcontainer";
    else return out;
    LineReader r = openCsv(vfs, list);
    if (!r.valid()) return out;
    std::string raw;
    r.read(raw);
    while (r.read(raw)) {
        const std::string line = cutLine(raw);
        const size_t comma = line.find(',');
        const std::string name = comma == std::string::npos ? line : line.substr(0, comma);
        if (name.empty()) continue;  // an empty line names no file
        out.push_back(name);
    }
    return out;
}

// ---- AmbientObject ----

AmbientObject::AmbientObject(AudioWorld& w, const AmbientData& d) : w_(w), d_(d) {
    attach(w.mgr);
    if (d.found) setDropoffs(d.minD, d.maxD);
    priority = d.priority;
    dopplerK = kAmbientDoppler;
    for (const AmbientRow& r : d.rows) {
        Row row;
        row.d = r;
        row.active = r.active != 0;
        row.smp = w.cache.get("Amb3D\\" + r.name, r.doppler != 0);
        rows_.push_back(row);
    }
}

AmbientObject::~AmbientObject() = default;

void AmbientObject::objectUpdate(float s) {
    speed_ = s;
    const bool echo = w_.mgr.echo;
    if (d_.area == 1 && !echo) return;
    if (d_.area == 2 && echo) return;
    if (!w_.mgr.enabled() || holdsSlot()) return;
    if (!d_.points.empty()) {
        const vec3& L = w_.mgr.listener();
        float best = 0;
        int bi = -1;
        for (size_t i = 0; i < d_.points.size(); ++i) {
            const vec3& P = d_.points[i];
            const float D = float(std::fabs(double(P.x) - double(L.x)) +
                                  (std::fabs(double(P.z) - double(L.z)) + std::fabs(double(P.y) - double(L.y))));
            if (bi < 0 || D < best) {
                best = D;
                bi = int(i);
            }
        }
        point_ = bi;
        position = d_.points[size_t(bi)];
    }
    requestSlot();
}

void AmbientObject::setRow(int row, bool on) {
    if (row < 0 || size_t(row) >= rows_.size()) return;  // missing rows are ignored
    Row& r = rows_[size_t(row)];
    if (on) {
        r.active = true;
        return;
    }
    r.active = false;
    if (r.d.type == 0)
        if (Sample* s = r.smp.at(slot_))
            if (s->playing()) s->stop();
}

void AmbientObject::ownerReset() { reset3D(); }

void AmbientObject::onReleased() {
    if (echoFlag_) {
        for (Row& r : rows_)
            if (Sample* s = r.smp.at(slot_)) s->setEcho(false);
        echoFlag_ = false;
    }
    for (Row& r : rows_)
        if (Sample* s = r.smp.at(slot_))
            if (s->playing()) s->stop();
}

void AmbientObject::apply3D(Row& r) {
    Sample* s = r.smp.at(slot_);
    if (!s) return;
    s->setVolume(r.d.volume * A);
    if (r.d.doppler != 0) s->setPitch(doppler);
    s->setPan(pan);
}

void AmbientObject::fire(Row& r) {
    Sample* s = r.smp.at(slot_);
    float factor, p, pitch;
    if (r.d.type == 1) {
        factor = float(rndS(0.75f, 1.0f));
        p = float(rndS(-1.0f, 1.0f));
        pitch = 1.0f;
    } else {
        factor = A;
        p = pan;
        pitch = doppler;
    }
    if (!s) return;
    s->setVolume(r.d.volume * factor);
    s->setPan(p);
    if (r.d.doppler != 0) s->setPitch(pitch);
    if (!s->playing()) s->play();
}

void AmbientObject::runTimer(Row& r, float dt) {
    if (!(r.d.minSpeed <= speed_ && speed_ <= r.d.maxSpeed)) return;  // frozen outside the window
    if (r.timer <= 0.0f) {
        fire(r);
        r.timer = r.d.low != r.d.high ? float(rndS(r.d.low, r.d.high)) : r.d.high;
    }
    r.timer = r.timer - dt;
}

void AmbientObject::slotUpdate(float dt) {
    const bool echo = w_.mgr.echo;
    // 1. Echo.
    if (echo && !echoFlag_) {
        EchoParams p;
        p.delay = w_.mgr.echoDelay();
        p.gain = 0.96f;
        for (Row& r : rows_)
            if (Sample* s = r.smp.at(slot_)) s->setEcho(true, p);
        echoFlag_ = true;
    } else if (!echo && echoFlag_) {
        for (Row& r : rows_)
            if (Sample* s = r.smp.at(slot_)) s->setEcho(false);
        echoFlag_ = false;
    }
    // 2. Area.
    if ((d_.area == 1 && !echoFlag_) || (d_.area == 2 && echoFlag_)) {
        releaseSlot();
        return;
    }
    // 3.
    if (echoFlag_)
        for (Row& r : rows_)
            if (Sample* s = r.smp.at(slot_)) s->echoTick(dt);
    // 4.
    if (w_.mgr.pastMax(*this)) {
        releaseSlot();
        return;
    }
    // 5.
    bool positioned = false;
    for (const Row& r : rows_)
        if (r.d.type == 0 || r.d.type == 2 || r.d.type == 3) positioned = true;
    if (positioned) w_.mgr.compute(*this, dt);
    // 6.
    for (Row& r : rows_) {
        if (!r.active) continue;
        switch (r.d.type) {
        case 0: {
            apply3D(r);
            Sample* s = r.smp.at(slot_);
            if (!s) break;
            if (speed_ < r.d.minSpeed || speed_ > r.d.maxSpeed) {
                if (s->playing()) s->stop();
            } else if (!s->playing()) {
                s->playLooped();
            }
            break;
        }
        case 1: runTimer(r, dt); break;
        case 2:
            apply3D(r);
            runTimer(r, dt);
            break;
        case 3: apply3D(r); break;
        default: break;
        }
    }
}

// ---- CableCarSound ----

CableCarSound::CableCarSound(AudioWorld& w, float initialSpeed) : w_(w), oldSpeed_(initialSpeed) {
    attach(w.mgr);
    setDropoffs(0.0f, 100.0f);
    priority = 8;
    dopplerK = kAmbientDoppler;
    start_ = w.cache.get("CABLECARSTART", true);
    loop_ = w.cache.get("CABLECAR", true);
    stop_ = w.cache.get("CABLECARSTOP", true);
    bell_ = w.cache.get("CABLECARGOBELL", true);
    cable_ = w.cache.get("STREETCABLE", true);  // loaded, never played
}

void CableCarSound::ownerFrame(const vec3& pos, float speed) {
    position = pos;
    speed_ = speed;
    if (!holdsSlot()) requestSlot();
}

void CableCarSound::onReleased() {
    if (Sample* s = loop_.at(slot_)) s->stop();
    if (Sample* s = start_.at(slot_)) s->stop();
}

void CableCarSound::slotUpdate(float dt) {
    // 1.
    if (w_.mgr.pastMax(*this)) {
        releaseSlot();
        oldSpeed_ = speed_;
        return;
    }
    // 2.
    w_.mgr.compute(*this, dt);
    const float v = 0.98f * A;
    // 3.
    const float n = speed_, o = oldSpeed_;
    Sample* startS = start_.at(slot_);
    if (o < 0.001f && n < 0.001f) state_ = 0;
    else if (o < 0.1f && n >= 0.1f) state_ = 2;
    else if (o > 0.5f && n <= 0.5f) state_ = 1;
    else if (state_ == 2 && !(startS && startS->playing())) state_ = 3;
    // 4.
    Sample* loopS = loop_.at(slot_);
    switch (state_) {
    case 0:
        if (loopS && loopS->playing()) loopS->stop();
        break;
    case 1:
        if (loopS && loopS->playing()) loopS->stop();
        if (Sample* s = stop_.at(slot_)) {
            s->setVolume(v);
            s->setPan(pan);
            s->setPitch(doppler);
            if (!s->playing()) s->play();
        }
        break;
    case 2:
        if (startS) {
            startS->setVolume(v);
            startS->setPan(pan);
            startS->setPitch(doppler);
            if (!startS->playing()) startS->play();
        }
        if (Sample* b = bell_.at(slot_)) {
            b->setVolume(v);
            if (prevState_ != 2 && !b->playing()) b->play();
        }
        break;
    case 3:
        if (loopS) {
            loopS->setVolume(v);
            loopS->setPan(pan);
            loopS->setPitch(doppler);
            if (!loopS->playing()) loopS->playLooped();
        }
        break;
    default: break;
    }
    prevState_ = state_;
    // 5.
    oldSpeed_ = speed_;
}

// ---- RainAudio ----

RainAudio::RainAudio(AudioWorld& w, bool night) : w_(w), night_(night) {
    exterior_ = std::make_unique<Sample>(w.sys, false);
    exterior_->setFile(0, soundPath("Rainexterior"));
    exterior_->setVolume(0.82f);
    interior_ = std::make_unique<Sample>(w.sys, false);
    interior_->setFile(0, soundPath("Raininterior"));
    interior_->setVolume(0.85f);
    if (night) {
        thunder1_ = std::make_unique<Sample>(w.sys, true);
        thunder1_->setFile(0, soundPath("Thunder"));
        thunder1_->setVolume(1.0f);
        thunder1_->setPan(-0.2f);
        thunder2_ = std::make_unique<Sample>(w.sys, true);
        thunder2_->setFile(0, soundPath("Thunder"));
        thunder2_->setVolume(1.0f);
        thunder2_->setPan(0.2f);
        thunder2_->setPitch(0.8f);
    }
    current_ = exterior_.get();
}

void RainAudio::viewSet(bool inside) {
    if (inside == inside_) return;
    inside_ = inside;
    Sample* n = inside ? interior_.get() : exterior_.get();
    Sample* other = inside ? exterior_.get() : interior_.get();
    n->playLooped();
    if (other->playing()) other->stop();
    current_ = n;
}

void RainAudio::frame(float dt) {
    // 1. Results screen.
    if (!w_.mgr.enabled()) {
        if (current_->playing()) current_->stop();
        return;
    }
    // 2. Tunnels.
    const bool echo = w_.mgr.echo;
    if (echo && !muted_) {
        exterior_->setVolume(0.65f);
        interior_->setVolume(0.0f);
        if (thunder1_) {
            thunder1_->setVolume(0.85f);
            thunder2_->setVolume(0.85f);
            thunder1_->setPan(0.0f);
            thunder2_->setPan(0.0f);
        }
        muted_ = true;
    } else if (!echo && muted_) {
        exterior_->setVolume(0.76f);
        interior_->setVolume(0.83f);
        if (thunder1_) {
            thunder1_->setVolume(1.0f);
            thunder2_->setVolume(1.0f);
            thunder1_->setPan(-20.0f);
            thunder2_->setPan(20.0f);
        }
        muted_ = false;
    }
    // 3.
    if (!current_->playing()) current_->playLooped();
    // 4-7. Thunder at night.
    if (!night_) {
        t_ = 0.0f;
        return;
    }
    if (!phase_ && t_ >= 15.0f) {
        if (thunder1_ && !thunder1_->playing()) thunder1_->play();
        phase_ = true;
        t_ = 0.0f;
    }
    if (phase_ && t_ > 1.0f) {
        if (thunder2_ && !thunder2_->playing()) thunder2_->play();
        phase_ = false;
        t_ = 0.0f;
    }
    t_ = t_ + dt;
}

// ---- WorldAudio ----

void WorldAudio::setup(bool ambientOption) {
    city_.clear();
    if (ambientOption) {
        for (const std::string& name : ambientList(w_.vfs, w_.city)) {
            AmbientData d = loadAmbientFile(w_.vfs, name);
            if (!d.found) continue;
            if (d.points.empty()) {
                logFile(LogLevel::Warn, "aud/ambient/" + name + ".csv", "a city ambient object without points (MM2 would crash); not made");
                continue;
            }
            city_.push_back(std::make_unique<AmbientObject>(w_, d));
            city_.back()->label = "ambient " + name;
        }
        if (!city_.empty()) logInfo("city ambience: %zu ambient objects", city_.size());
    }
    rain_.reset();
    if (w_.weather == 3) rain_ = std::make_unique<RainAudio>(w_, w_.timeOfDay == 3);
}

void WorldAudio::updateObjects() {
    for (auto& o : city_) o->objectUpdate(0.0f);
}

void WorldAudio::viewSet(bool inside) {
    if (rain_) rain_->viewSet(inside);
}

void WorldAudio::playerFrame(float dt) {
    if (rain_) rain_->frame(dt);
}

AmbientObject* WorldAudio::makeOwned(const std::string& file) {
    owned_.push_back(std::make_unique<AmbientObject>(w_, loadAmbientFile(w_.vfs, file)));
    owned_.back()->label = file;
    return owned_.back().get();
}

}  // namespace audio
}  // namespace cc
