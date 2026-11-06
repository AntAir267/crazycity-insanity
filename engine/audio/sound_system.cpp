#include "audio/sound_system.h"

#include <algorithm>
#include <cmath>

#include "audio/wav.h"
#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {
namespace audio {

std::string soundPath(const std::string& name, const std::string& subPath, const std::string& extension) {
    std::string p = "aud\\" + subPath + "\\" + name;
    if (!(name.size() >= 4 && name[name.size() - 4] == '.')) p += extension;
    if (!iendsWith(p, ".wav") && !iendsWith(p, ".adpcm")) p += ".wav";
    return p;
}

// ---------------------------------------------------------------------------------------------
// Sample

Sample::Sample(SoundSystem& sys, bool pitchControl, int instances, float volume) : sys_(sys), pitch_(pitchControl) {
    inst_.resize(size_t(instances < 1 ? 1 : instances));
    for (Inst& in : inst_) in.volume = volume;
    sys_.registerSample(this);
}

Sample::~Sample() {
    for (Inst& in : inst_) {
        if (sys_.backend_) {
            if (in.voice.id >= 0) sys_.backend_->stopVoice(in.voice.id);
            if (in.echo.voice.id >= 0) sys_.backend_->stopVoice(in.echo.voice.id);
        }
    }
    sys_.unlist(this);
    sys_.unregisterSample(this);
}

void Sample::setFile(int i, const std::string& path) {
    if (i < 0 || i >= int(inst_.size())) return;
    Inst& in = inst_[size_t(i)];
    if (in.playing) stopInst(in, false);
    in.buffer = path.empty() ? -1 : sys_.file(path);
}

void Sample::setFileAll(const std::string& path) {
    for (int i = 0; i < int(inst_.size()); ++i) setFile(i, path);
}

const std::string& Sample::file(int i) const {
    static const std::string none;
    if (i < 0 || i >= int(inst_.size())) return none;
    const int b = inst_[size_t(i)].buffer;
    return b >= 0 ? sys_.files_[size_t(b)].path : none;
}

bool Sample::loaded() const {
    const int b = inst_[size_t(current_)].buffer;
    return b >= 0 && sys_.files_[size_t(b)].state == SoundSystem::File::Ready;
}

bool Sample::pending() const {
    const int b = inst_[size_t(current_)].buffer;
    return b >= 0 && sys_.files_[size_t(b)].state == SoundSystem::File::Pending;
}

bool Sample::missing() const { return !loaded() && !pending(); }

double Sample::duration() const {
    const Inst& in = inst_[size_t(current_)];
    if (in.buffer < 0) return 0;
    const SoundSystem::File& f = sys_.files_[size_t(in.buffer)];
    return f.state == SoundSystem::File::Ready && f.rate > 0 ? double(f.frames) / double(f.rate) : 0.0;
}

double Sample::position() const {
    const Inst& in = inst_[size_t(current_)];
    if (in.buffer < 0) return 0;
    const SoundSystem::File& f = sys_.files_[size_t(in.buffer)];
    return f.rate > 0 ? in.pos / double(f.rate) : 0.0;
}

int Sample::rateHz(const Inst& in) const {
    if (in.buffer < 0) return 0;
    const SoundSystem::File& f = sys_.files_[size_t(in.buffer)];
    if (!pitch_) return f.rate;
    // rate × clamp(f, 0, 2), then whole hertz (truncated) limited to 100 … 100,000.
    float k = in.pitch;
    if (!(k >= 0.0f)) k = 0.0f;
    if (k > 2.0f) k = 2.0f;
    double hz = std::trunc(double(float(f.rate) * k));
    if (hz < 100.0) hz = 100.0;
    if (hz > 100000.0) hz = 100000.0;
    return int(hz);
}

void Sample::queue(std::deque<Echo::Rep>& q, const Echo::Rep& r) {
    // N = trunc(T × 180) repeats of each kind; a command arriving at a full queue overwrites the
    // oldest and the other pending repeats of that kind are forgotten (sound-system.md, "Echo").
    const size_t cap = size_t(std::max(1.0, std::trunc(double(echoParams_.delay) * 180.0)));
    if (q.size() >= cap) q.clear();
    q.push_back(r);
}

void Sample::setVolume(float v) {
    Inst& in = inst_[size_t(current_)];
    in.volume = v;
    if (echo_) {
        Echo::Rep r;
        r.value = sys_.effectsGain() * v * echoParams_.gain;
        queue(in.echo.volumes, r);
    }
}

void Sample::setPitch(float f) {
    Inst& in = inst_[size_t(current_)];
    in.pitch = f;
    if (echo_) {
        Echo::Rep r;
        r.rateHz = rateHz(in);
        queue(in.echo.pitches, r);
    }
}

void Sample::setPan(float p) {
    Inst& in = inst_[size_t(current_)];
    if (p < -1.0f) p = -1.0f;
    if (p > 1.0f) p = 1.0f;
    in.pan = p;
    if (echo_) {
        float e = -0.25f * p;
        in.echo.pan = e < -1.0f ? -1.0f : (e > 1.0f ? 1.0f : e);
    }
}

void Sample::start(bool loop) {
    Inst& in = inst_[size_t(current_)];
    if (!loaded()) return;
    const double posAtCommand = in.playing ? in.pos : 0.0;
    if (in.playing) {
        if (inst_.size() > 1) return;  // several instances: it carries on
        // One instance: stopped and started again from the beginning.
        in.pos = 0;
        in.loop = loop;
        in.restart = true;
    } else {
        if (!sys_.admit(this)) return;
        in.playing = true;
        in.loop = loop;
        in.pos = 0;
        in.restart = true;
        sys_.listPlaying(this);
    }
    if (echo_) {
        Echo::Rep r;
        r.loop = loop;
        r.pos = posAtCommand;
        queue(in.echo.plays, r);
    }
}

void Sample::stopInst(Inst& in, bool repeat) {
    in.playing = false;
    in.pos = 0;
    in.restart = false;
    if (repeat && echo_) queue(in.echo.stops, Echo::Rep());
}

void Sample::stop() {
    Inst& in = inst_[size_t(current_)];
    const bool was = in.playing;
    stopInst(in, true);
    if (was && !anyPlaying()) sys_.unlist(this);
}

void Sample::stopAll() {
    for (Inst& in : inst_) stopInst(in, true);
    sys_.unlist(this);
}

bool Sample::anyPlaying() const {
    for (const Inst& in : inst_)
        if (in.playing) return true;
    return false;
}

void Sample::setEcho(bool on, const EchoParams& e) {
    if (on) {
        if (!echoMade_) {
            echoMade_ = true;
            echoT_ = e.delay;  // fixed with the first switch-on (and the queue size with it)
        }
        echoParams_ = e;
        echoParams_.delay = echoT_;
        echo_ = true;
        for (Inst& in : inst_) {
            Echo& ec = in.echo;
            int fr = 0;
            if (in.buffer >= 0) fr = sys_.files_[size_t(in.buffer)].rate;
            if (!ec.made) {
                // Made once per instance from the main voice: its position, level and rate (the
                // file's rate without pitch control).
                ec.made = true;
                ec.pos = in.pos;
                ec.level = sys_.effectsGain() * in.volume;
                ec.rateHz = pitch_ ? rateHz(in) : fr;
            }
            ec.pan = std::clamp(-0.25f * in.pan, -1.0f, 1.0f);
            if (e.hasPitch) {
                double hz = std::trunc(double(float(fr) * e.pitch));
                ec.rateHz = int(std::clamp(hz, 100.0, 100000.0));
            }
            // A loop already running gets its echo T later, from the echo voice's position.
            if (in.playing && in.loop) {
                Echo::Rep r;
                r.loop = true;
                r.pos = ec.pos;
                queue(ec.plays, r);
            }
        }
    } else {
        echo_ = false;
        for (Inst& in : inst_) {
            Echo& ec = in.echo;
            ec.plays.clear();
            ec.stops.clear();
            ec.volumes.clear();
            ec.pitches.clear();
            ec.playing = false;  // stops where it is (a later switch-on reuses it there)
        }
    }
}

void Sample::echoTick(float dt) {
    if (!echo_) return;
    const float T = echoParams_.delay;
    for (Inst& in : inst_) {
        Echo& ec = in.echo;
        auto due = [&](std::deque<Echo::Rep>& q, Echo::Rep& out) {
            for (Echo::Rep& r : q) r.age = dt + r.age;
            if (q.empty() || !(q.front().age >= T)) return false;
            out = q.front();
            q.pop_front();
            while (!q.empty() && q.front().age >= T) q.pop_front();
            return true;
        };
        Echo::Rep r;
        if (due(ec.plays, r)) {
            if (in.buffer >= 0 && sys_.files_[size_t(in.buffer)].state == SoundSystem::File::Ready) {
                ec.playing = true;
                ec.loop = r.loop;
                ec.pos = r.pos;
                ec.restart = true;
            }
        }
        if (due(ec.stops, r)) {
            ec.playing = false;
            ec.pos = 0;
        }
        if (due(ec.volumes, r)) ec.level = r.value;
        if (due(ec.pitches, r)) ec.rateHz = r.rateHz;
    }
}

bool Sample::echoPlaying() const { return inst_[size_t(current_)].echo.playing; }

size_t Sample::echoPending() const {
    const Echo& e = inst_[size_t(current_)].echo;
    return e.plays.size() + e.stops.size() + e.volumes.size() + e.pitches.size();
}

// ---------------------------------------------------------------------------------------------
// SoundSystem

SoundSystem::SoundSystem() = default;

SoundSystem::~SoundSystem() {
    // Samples outlive nothing: owners delete theirs first.
}

void SoundSystem::setBackend(AudioBackend* b) { backend_ = b; }

float SoundSystem::effectsGain() const {
    if (!opt_.effects) return 0.0f;
    const float s = opt_.sfxVolume;
    if (!(s > 0.0f)) return 0.0f;
    return float(std::log(200.0 * double(s)) / std::log(200.0));
}

void SoundSystem::unregisterSample(Sample* s) {
    auto it = std::find(samples_.begin(), samples_.end(), s);
    if (it != samples_.end()) samples_.erase(it);
}

int SoundSystem::file(const std::string& path) {
    const std::string key = pathKey(path);
    auto it = fileIndex_.find(key);
    if (it != fileIndex_.end()) return it->second;
    const int idx = int(files_.size());
    files_.push_back(File());
    fileIndex_[key] = idx;
    File& f = files_.back();
    f.path = path;
    const Vfs::FileState st = vfs_ ? vfs_->request(path) : Vfs::FileState::Missing;
    if (st == Vfs::FileState::Missing) {
        // No fallback (sound-system.md): the sample stays silent.
        f.state = File::Missing;
        logInfo("sound file not found: %s", path.c_str());
    } else if (st == Vfs::FileState::Ready) {
        decode(f);
    } else {
        f.state = File::Pending;
        pending_.push_back(idx);
    }
    return idx;
}

void SoundSystem::decode(File& f) {
    BytesPtr b = vfs_ ? vfs_->open(f.path) : nullptr;
    if (!b) {
        f.state = File::Failed;
        return;
    }
    WavInfo w;
    std::string err;
    if (!parseWav(b->data(), b->size(), w, &err)) {
        logFile(LogLevel::Warn, f.path, "sound not loaded: " + err);
        f.state = File::Failed;
        vfs_->forget(f.path);
        return;
    }
    std::vector<int16_t> pcm;
    wavToPcm16(b->data(), w, pcm);
    vfs_->forget(f.path);
    f.channels = w.channels;
    f.rate = w.rate;
    f.frames = w.frames();
    f.backendId = backend_ ? backend_->createBuffer(pcm.data(), f.frames, f.channels, f.rate) : -1;
    f.state = File::Ready;
}

void SoundSystem::clearFiles() {
    stopEverything();
    for (Sample* s : samples_)
        for (Sample::Inst& in : s->inst_) in.buffer = -1;
    if (backend_)
        for (File& f : files_)
            if (f.backendId >= 0) backend_->destroyBuffer(f.backendId);
    files_.clear();
    fileIndex_.clear();
    pending_.clear();
}

bool SoundSystem::admit(Sample* s) {
    if (s->listed_) return true;
    if (int(playing_.size()) < kMaxPlaying) return true;
    // The playing sample with the lowest priority among those not above the new one's (the first
    // such in start order) is stopped; if all are higher, the new one does not play.
    Sample* victim = nullptr;
    for (Sample* p : playing_) {
        if (p->priority_ > s->priority_) continue;
        if (!victim || p->priority_ < victim->priority_) victim = p;
    }
    if (!victim) {
        ++refused_;
        return false;
    }
    ++stolen_;
    victim->stopAll();
    return true;
}

void SoundSystem::listPlaying(Sample* s) {
    if (s->listed_) return;
    s->listed_ = true;
    playing_.push_back(s);
}

void SoundSystem::unlist(Sample* s) {
    if (!s->listed_) return;
    s->listed_ = false;
    auto it = std::find(playing_.begin(), playing_.end(), s);
    if (it != playing_.end()) playing_.erase(it);
}

void SoundSystem::stopEverything() {
    for (Sample* s : samples_) {
        for (Sample::Inst& in : s->inst_) {
            in.playing = false;
            in.pos = 0;
            in.echo.playing = false;
        }
        unlist(s);
        flush(*s);
    }
}

void SoundSystem::outputRestarted() {
    for (Sample* s : samples_)
        for (Sample::Inst& in : s->inst_) {
            in.voice = Sample::Voice();
            in.echo.voice = Sample::Voice();
            if (in.playing) in.restart = true;
            if (in.echo.playing) in.echo.restart = true;
        }
}

float SoundSystem::amplitude(float v, bool clamp01) const {
    if (clamp01) {
        if (!(v > 0.0f)) return 0.0f;
        if (v > 1.0f) v = 1.0f;
    }
    // DirectSound attenuation trunc((v − 1) × 10000) in hundredths of a decibel.
    const double att = std::trunc((double(v) - 1.0) * 10000.0);
    if (att <= -10000.0) return 0.0f;
    if (att >= 0.0) return 1.0f;
    return float(std::pow(10.0, att / 2000.0));
}

void SoundSystem::panGains(float pan, float& l, float& r) const {
    l = r = 1.0f;
    if (opt_.stereo) {
        if (pan > 0.0f) l = float(std::pow(10.0, -5.0 * double(pan)));
        else if (pan < 0.0f) r = float(std::pow(10.0, 5.0 * double(pan)));
    }
    // Balance (linear amplitude of the whole output).
    const float b = opt_.balance;
    if (b > 0.0f) l *= (1.0f - b);
    else if (b < 0.0f) r *= (1.0f + b);
}

void SoundSystem::flushVoice(Sample::Voice& v, bool playing, bool restart, int buffer, bool loop, double pos,
                             int rateHz, float gl, float gr) {
    if (!backend_) return;
    if (!playing) {
        if (v.id >= 0) backend_->stopVoice(v.id);
        v = Sample::Voice();
        return;
    }
    const File& f = files_[size_t(buffer)];
    const double rate = f.rate > 0 ? double(rateHz) / double(f.rate) : 1.0;
    if (v.id < 0 || restart) {
        if (v.id >= 0) backend_->stopVoice(v.id);
        const double offset = f.rate > 0 ? pos / double(f.rate) : 0.0;
        v.id = f.backendId >= 0 ? backend_->startVoice(f.backendId, loop, offset, rate, gl, gr) : -1;
        v.rate = rate;
        v.gl = gl;
        v.gr = gr;
        return;
    }
    if (v.rate != rate || v.gl != gl || v.gr != gr) {
        backend_->setVoice(v.id, rate, gl, gr);
        v.rate = rate;
        v.gl = gl;
        v.gr = gr;
    }
}

void SoundSystem::flush(Sample& s) {
    const float g = effectsGain();
    for (Sample::Inst& in : s.inst_) {
        const bool ok = in.buffer >= 0 && files_[size_t(in.buffer)].state == File::Ready;
        float pl, pr;
        panGains(in.pan, pl, pr);
        const float a = amplitude(g * in.volume, true);
        flushVoice(in.voice, ok && in.playing, in.restart, in.buffer, in.loop, in.pos, s.rateHz(in), a * pl, a * pr);
        in.restart = false;
        Sample::Echo& e = in.echo;
        float el, er;
        panGains(e.pan, el, er);
        const float ea = amplitude(e.level, false);
        flushVoice(e.voice, ok && e.playing, e.restart, in.buffer, e.loop, e.pos, e.rateHz, ea * el, ea * er);
        e.restart = false;
    }
}

void SoundSystem::beginFrame(float dt) {
    // Files that arrived.
    if (!pending_.empty() && vfs_) {
        std::vector<int> still;
        for (int idx : pending_) {
            File& f = files_[size_t(idx)];
            const Vfs::FileState st = vfs_->request(f.path);
            if (st == Vfs::FileState::Ready) decode(f);
            else if (st == Vfs::FileState::Missing) f.state = File::Missing;
            else still.push_back(idx);
        }
        pending_.swap(still);
    }
    // Play positions: everything playing was sent out by an earlier frame and has played dt more.
    for (Sample* s : samples_) {
        bool any = false;
        for (Sample::Inst& in : s->inst_) {
            if (in.buffer < 0 || files_[size_t(in.buffer)].state != File::Ready) {
                in.playing = false;
                in.echo.playing = false;
                continue;
            }
            const File& f = files_[size_t(in.buffer)];
            auto advance = [&](bool& playing, bool loop, double& pos, int hz) {
                if (!playing) return;
                pos += double(hz) * double(dt);
                if (pos >= double(f.frames)) {
                    if (loop && f.frames > 0) pos = std::fmod(pos, double(f.frames));
                    else {
                        playing = false;
                        pos = 0;
                    }
                }
            };
            advance(in.playing, in.loop, in.pos, s->rateHz(in));
            advance(in.echo.playing, in.echo.loop, in.echo.pos, in.echo.rateHz);
            if (in.playing) any = true;
        }
        if (!any) unlist(s);
    }
}

void SoundSystem::endFrame() {
    // Pause: the first two paused frames stop everything (nothing is resumed).
    if (paused_) {
        if (pausedFrames_ < 2) {
            for (Sample* s : samples_) {
                for (Sample::Inst& in : s->inst_) {
                    in.playing = false;
                    in.pos = 0;
                    in.echo.playing = false;
                }
                unlist(s);
            }
        }
        ++pausedFrames_;
    } else {
        pausedFrames_ = 0;
    }
    for (Sample* s : samples_) flush(*s);
}

SoundSystem::Stats SoundSystem::stats() const {
    Stats st;
    st.samples = int(samples_.size());
    st.playing = int(playing_.size());
    for (const Sample* s : samples_)
        for (const Sample::Inst& in : s->inst_) {
            if (in.voice.id >= 0) ++st.voices;
            if (in.echo.voice.id >= 0) ++st.voices;
        }
    st.files = int(files_.size());
    for (const File& f : files_) {
        if (f.state == File::Missing || f.state == File::Failed) ++st.missing;
        if (f.state == File::Pending) ++st.pending;
    }
    st.stolen = stolen_;
    st.refused = refused_;
    return st;
}

}  // namespace audio
}  // namespace cc
