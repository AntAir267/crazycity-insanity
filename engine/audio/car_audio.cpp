#include "audio/car_audio.h"

#include <cmath>

#include "core/log.h"
#include "physics/world.h"

namespace cc {
namespace audio {

namespace {
constexpr float kCarDoppler = 0.0176315f;  // single 0x3C906FF1
}

CarAudio::CarAudio(AudioWorld& w) : w_(w) { attach(w.mgr); }

CarAudio::~CarAudio() {
    if (car_) car_->setImpactSink(nullptr);
}

bool CarAudio::setup(phys::Car* car, const std::string& name, Owner owner, bool lightBar) {
    car_ = car;
    name_ = name;
    owner_ = owner;
    lightBar_ = lightBar;
    // 1. The audio kind (exact names).
    kind_ = carAudioKind(w_.types, name);
    // 2. Police: the siren file; every police set-up resets the chase count. Semi/bus: semidata.
    if (kind_ == CarAudioKind::Police) {
        const SirenData& sd = w_.sirenData();
        for (const auto& s : sd.samples) sirenSamples_.push_back(w_.cache.get("sirens\\" + s.wave, true));
        if (!sd.explosion.empty()) explosion_ = w_.cache.get("sirens\\" + sd.explosion, true);
        siren_.stepIndex.assign(sd.samples.size(), 0);
        w_.chaseCount = 0;
    } else if (kind_ == CarAudioKind::SemiBus) {
        const SemiData& sd = w_.semiData();
        if (!sd.reverse.empty()) beeper_ = w_.cache.get(sd.reverse, true);
        if (!sd.airBlow.empty()) airBlow_ = w_.cache.get(sd.airBlow, true);
        beeperVol_ = sd.reverseVolume;
        airVol_ = sd.airVolume;
    }
    // 3. The cardata file (default.csv of the same folder when missing or unusable).
    const std::string folder = owner == Owner::Player ? "player" : "opponent";
    data_ = loadCarData(w_.vfs, folder, name);
    if (!data_.ok) {
        logFile(LogLevel::Warn, "aud/cardata/" + folder + "/" + name + ".csv", "no usable car sound file (nor default.csv): the car makes no sound");
        return false;
    }
    for (const EngineRow& r : data_.engine) engine_.push_back(w_.cache.get("engines\\" + r.wave, true));
    if ((owner == Owner::Player || owner == Owner::Network) && !(owner == Owner::Network && kind_ == CarAudioKind::Police) &&
        !data_.hornWave.empty())
        horn_ = w_.cache.get("horns\\" + data_.hornWave, true);
    if (owner == Owner::Player && !data_.clutchWave.empty()) clutch_ = w_.cache.get(data_.clutchWave, true);
    // 4. Tyre, suspension, wobble and impact files.
    impacts_ = w_.carImpactTable(owner == Owner::Player);
    // 5. Source settings.
    twoD = owner == Owner::Player;
    setDropoffs(0.0f, 150.0f);
    priority = kind_ == CarAudioKind::Police ? 7 : 9;
    dopplerK = kCarDoppler;
    pan2D = 0.0f;
    if (car_) {
        position = car_->body().p;
        car_->setImpactSink(this);
    }
    active_ = true;
    if (twoD) requestSlot();
    return true;
}

// ---- slots ----

void CarAudio::onAssigned(int slot) {
    if (twoD && !levelsSet_) {
        // Player: horn and gear click levels, set once right after the first slot at set-up.
        if (Sample* h = horn_.at(slot)) {
            h->setVolume(data_.hornVolume);
            h->setPitch(1.0f);
            h->setPan(0.0f);
        }
        if (Sample* c = clutch_.at(slot)) {
            c->setVolume(data_.clutchVolume);
            c->setPitch(1.0f);
            c->setPan(0.0f);
        }
        levelsSet_ = true;
    }
    // Siren samples get their own volumes back.
    const SirenData& sd = w_.siren;
    for (size_t i = 0; i < sirenSamples_.size() && i < sd.samples.size(); ++i)
        if (Sample* s = sirenSamples_[i].at(slot)) {
            s->setVolume(sd.samples[i].volume);
            if (twoD) s->setPitch(1.0f);
        }
    if (twoD && kind_ == CarAudioKind::SemiBus) {
        // 2D: both samples get the reverse volume (MM2 uses it twice), pitch 1.
        if (Sample* b = beeper_.at(slot)) {
            b->setVolume(beeperVol_);
            b->setPitch(1.0f);
        }
        if (Sample* a = airBlow_.at(slot)) {
            a->setVolume(beeperVol_);
            a->setPitch(1.0f);
        }
    }
    if (impacts_) impacts_->slotAssigned(slot);
    if (car_) circ_ = car_->wheel(2).radius * 6.28318f;  // the float constant 6.28318, not 2π
}

void CarAudio::onReleased() {
    // The echo copies go off; the loops stop; one-shots are let go.
    if (echoFlag_) setEchoAll(false);
    echoFlag_ = false;
    stopEngine();
    if (Sample* h = at(horn_)) h->stop();
    if (Sample* c = at(clutch_)) c->stop();
    if (siren_.current >= 0 && size_t(siren_.current) < sirenSamples_.size())
        if (Sample* s = at(sirenSamples_[size_t(siren_.current)])) s->stop();
    if (Sample* b = at(beeper_)) b->stop();
    if (Sample* a = at(airBlow_)) a->stop();
    stopTyreLoops();
}

void CarAudio::postPhysics() {
    if (!active_) return;
    if (car_) position = car_->body().p;
    requestSlot();
}

void CarAudio::carReset() {
    if (!active_) return;
    if (!twoD) reset3D();
}

// ---- echo ----

void CarAudio::setEchoAll(bool on) {
    EchoParams p;
    p.delay = w_.mgr.echoDelay();
    p.gain = 0.96f;
    auto set = [&](const SlotSample& s, const EchoParams& e) {
        if (Sample* x = at(s)) x->setEcho(on, e);
    };
    for (const SlotSample& s : engine_) set(s, p);
    EchoParams hp;
    hp.delay = 0.05f;
    hp.gain = 0.96f;
    hp.hasPitch = true;
    hp.pitch = 0.997f;
    set(horn_, hp);
    set(clutch_, p);
    for (const SlotSample& s : sirenSamples_) set(s, p);
    set(beeper_, p);
    set(airBlow_, p);
    // Every block's rolling and skid samples.
    for (const SurfaceSamples& b : w_.surfaceSamples) {
        set(b.rolling, p);
        for (const SlotSample& s : b.skids) set(s, p);
    }
}

void CarAudio::tickEcho(float dt) {
    auto tick = [&](const SlotSample& s) {
        if (Sample* x = at(s)) x->echoTick(dt);
    };
    tick(horn_);
    tick(clutch_);
    for (const SlotSample& s : sirenSamples_) tick(s);
    tick(beeper_);
    tick(airBlow_);
    // The rolling and skid samples of block cur only.
    if (cur_ >= 0 && size_t(cur_) < w_.surfaceSamples.size()) {
        const SurfaceSamples& b = w_.surfaceSamples[size_t(cur_)];
        tick(b.rolling);
        for (const SlotSample& s : b.skids) tick(s);
    }
}

// ---- per update ----

void CarAudio::slotUpdate(float dt) {
    if (!active_ || !car_) return;
    // 1. Echo: switch when the state differs from the car's flag, then tick (plain audio ticks
    // in the switching update; the other kinds from the next one, and once more when it goes off).
    const bool was = echoFlag_;
    if (w_.mgr.echo != echoFlag_) {
        setEchoAll(w_.mgr.echo);
        echoFlag_ = w_.mgr.echo;
    }
    if (kind_ == CarAudioKind::Plain ? echoFlag_ : was) tickEcho(dt);

    if (twoD) {
        // 3. Player (2D): A = 1, its own pan, doppler 1.
        w_.mgr.compute(*this, dt);
        if (kind_ == CarAudioKind::Police) {
            gearClick();
            tyreStep(dt);
            engineStep(dt);
            if (siren_.mode != SirenMode::Off) sirenUpdate(dt);
            return;
        }
        gearClick();
        engineStep(dt);
        tyreStep(dt);
        impactStep();
        if (kind_ == CarAudioKind::SemiBus) semiStep();
        return;
    }
    // 2. 3D car.
    if (kind_ == CarAudioKind::Police) {
        const bool past = w_.mgr.pastMax(*this);
        if ((past || siren_.explosionFlag) && siren_.mode != SirenMode::Counted && !explosionPlaying()) {
            releaseSlot();
            return;
        }
        w_.mgr.compute(*this, dt);
        gearClick();
        tyreStep(dt);
        if (siren_.mode == SirenMode::Off) engineStep(dt);
        else sirenUpdate(dt);
        return;
    }
    if (w_.mgr.pastMax(*this)) {
        releaseSlot();
        return;
    }
    w_.mgr.compute(*this, dt);
    if (owner_ == Owner::Network) {
        if (Sample* h = at(horn_)) {
            h->setVolume(A * data_.hornVolume);
            h->setPitch(doppler);
            h->setPan(pan);
        }
    }
    gearClick();
    engineStep(dt);
    tyreStep(dt);
    impactStep();
    if (kind_ == CarAudioKind::SemiBus) semiStep();
}

void CarAudio::gearClick() {
    const int g = car_->gearIndex();
    if (Sample* c = at(clutch_)) {
        if ((prevGear_ == 0 && g != 0) || (prevGear_ != 0 && g == 0)) c->play();
    }
    prevGear_ = g;
}

void CarAudio::stopEngine() {
    for (const SlotSample& s : engine_)
        if (Sample* x = at(s))
            if (x->playing()) x->stop();
}

void CarAudio::engineStep(float dt) {
    const float rpm = car_->rpm();
    for (size_t i = 0; i < engine_.size(); ++i) {
        Sample* s = at(engine_[i]);
        if (!s) continue;
        const EngineRow& row = data_.engine[i];
        const float V = row.volume(rpm, silenced_);
        if (V < 0.25f) {
            if (s->playing()) s->stop();
        } else {
            const float P = row.pitch(rpm);
            if (twoD) {
                s->setVolume(V);
                s->setPitch(P);
            } else {
                s->setVolume(V * A);
                s->setPitch(P * doppler);
                s->setPan(pan);
            }
            if (!s->playing()) s->playLooped();
        }
        if (s->echoOn()) s->echoTick(dt);
    }
}

// ---- tyres ----

void CarAudio::stopTyreLoops() {
    if (cur_ < 0 || size_t(cur_) >= w_.surfaceSamples.size()) return;
    const SurfaceSamples& b = w_.surfaceSamples[size_t(cur_)];
    if (Sample* r = at(b.rolling)) r->stop();
    for (const SlotSample& s : b.skids)
        if (Sample* x = at(s)) x->stop();
}

void CarAudio::airborneTest() {
    if (airborne_ || !w_.world) return;
    const vec3 p = car_->body().p;
    float y = p.y - 3.0f;
    for (int k = 1; k <= 10; ++k) {
        const float y2 = y - 3.0f;
        phys::ProbeState ps;
        ps.roomS = ps.roomE = car_->collision().room;
        phys::ProbeHit hit;
        if (w_.world->probe(vec3(p.x, y, p.z), vec3(p.x, y2, p.z), ps, hit)) {
            airborne_ = true;
            return;
        }
        y = y2;
    }
}

void CarAudio::tyreStep(float dt) {
    const auto& blocks = w_.surface.blocks;
    const int nb = int(blocks.size());
    auto soundOf = [&](int wi) {
        const int s = car_->wheel(wi).material.sound;
        return s < 0 ? 0 : s;
    };
    const bool echo = w_.mgr.echo;
    int g = 0;
    for (int i = 0; i < 4; ++i)
        if (car_->wheel(i).grounded) ++g;
    const float speed = car_->speed();
    const float fA = twoD ? 1.0f : A;

    // Suspension thump (before step 1).
    if (Sample* su = at(w_.suspensionSample)) {
        const float mean = (((car_->wheel(0).rate + car_->wheel(1).rate) + car_->wheel(2).rate) + car_->wheel(3).rate) * 0.25f;
        if (mean >= std::fabs(w_.suspension.minVel) && g >= 2 && !su->playing()) {
            float v = mean * w_.suspension.inv;
            if (v < w_.suspension.minVol) v = w_.suspension.minVol;
            else if (v > w_.suspension.maxVol) v = w_.suspension.maxVol;
            su->setVolume(v * fA);
            if (!twoD) su->setPan(pan);
            su->play();
        }
    }

    auto rollingOf = [&](int b) -> Sample* {
        if (b < 0 || b >= nb) return nullptr;
        return at(w_.surfaceSamples[size_t(b)].rolling);
    };
    auto stopRolling = [&](int b) {
        if (Sample* r = rollingOf(b)) r->stop();
    };
    auto stopSkids = [&](int b) {
        if (b < 0 || b >= nb) return;
        for (const SlotSample& s : w_.surfaceSamples[size_t(b)].skids)
            if (Sample* x = at(s)) x->stop();
    };
    auto anySkidPlaying = [&](int b) {
        if (b < 0 || b >= nb) return false;
        for (const SlotSample& s : w_.surfaceSamples[size_t(b)].skids)
            if (Sample* x = at(s))
                if (x->playing()) return true;
        return false;
    };
    // Step 1: rolling loop.
    [&]() {
        if (g < 2) {
            stopRolling(cur_);
            if (owner_ == Owner::Player) {
                if (g == 0) airborneTest();
                else airborne_ = false;
            }
            return;
        }
        if (owner_ == Owner::Player) airborne_ = false;
        if (!(speed > 2.0f)) {
            stopRolling(cur_);
            return;
        }
        if (anySkidPlaying(cur_)) {
            stopRolling(cur_);
            return;
        }
        if (nb == 0) return;
        if (echo) {
            cur_ = w_.surface.tunnel;
            if (cur_ < 0 || cur_ >= nb) cur_ = 0;
        } else if (soundOf(0) != cur_ && soundOf(1) != cur_) {
            stopRolling(cur_);
            cur_ = soundOf(0);
            if (cur_ >= nb) cur_ = 0;
        }
        const SurfaceBlock& b = blocks[size_t(cur_)];
        if (!b.hasRolling) return;
        float pitch, vol;
        if (speed < b.maxSpeed) {
            pitch = speed * b.pitchSlope + b.minPitch;
            vol = speed * b.volSlope + b.minVol;
        } else {
            pitch = b.maxPitch;
            vol = b.maxVol;
        }
        if (Sample* r = rollingOf(cur_)) {
            r->setPitch(pitch);
            r->setVolume(vol * fA);
            if (!twoD) r->setPan(pan);
            if (!r->playing()) r->playLooped();
        }
        prev_ = cur_;
    }();
    // Step 2: skid loops.
    if (nb > 0) {
        float s = 0.0f;
        for (int i = 0; i < 4; ++i)
            if (car_->wheel(i).slide > s) s = car_->wheel(i).slide;
        if (echo) {
            cur_ = w_.surface.tunnel;
            if (cur_ < 0 || cur_ >= nb) cur_ = 0;
        } else if (soundOf(0) != cur_ && soundOf(1) != cur_) {
            stopSkids(cur_);
            cur_ = soundOf(0);
            if (cur_ >= nb) cur_ = 0;
        }
        const SurfaceBlock& b = blocks[size_t(cur_)];
        if (s > 0.0f) {
            const float vol = s * b.skidRange + b.minSkid;
            for (size_t i = 0; i < b.skids.size(); ++i) {
                Sample* x = i < w_.surfaceSamples[size_t(cur_)].skids.size() ? at(w_.surfaceSamples[size_t(cur_)].skids[i]) : nullptr;
                if (!x) continue;
                if (s < b.skids[i].minSlide || s > b.skids[i].maxSlide) {
                    if (x->playing()) x->stop();
                } else {
                    x->setVolume(vol * fA);
                    if (!twoD) x->setPan(pan);
                    if (!x->playing()) x->playLooped();
                }
            }
            skidding_ = true;
            prev_ = cur_;
        } else {
            if (skidding_) {
                stopSkids(cur_);
                skidding_ = false;
            }
            prev_ = cur_;
        }
    }
    // Tyre wobble (after step 2).
    if (Sample* wb = at(w_.wobbleSample)) {
        const phys::DamageTune& t = car_->tune().damage;
        float frac = (car_->damage().damage - t.medDamage) / (t.maxDamage - t.medDamage);
        if (!(frac >= 0.0f)) return;
        if (frac > 1.0f) frac = 1.0f;
        else if (frac <= 0.05f) return;
        const float d = speed * dt + wobbleAcc_;
        if (d < circ_) {
            wobbleAcc_ = d;
            return;
        }
        const WobbleData& wd = w_.wobble;
        float vol = frac;
        if (vol < wd.minV) vol = wd.minV;
        else if (vol > wd.maxV) vol = wd.maxV;
        float pitch = speed * wd.inv;
        if (pitch < wd.minP) pitch = wd.minP;
        else if (pitch > wd.maxP) pitch = wd.maxP;
        if (wb->playing()) return;  // the distance waits until the last thump has finished
        wb->setVolume(vol * fA);
        if (!twoD) wb->setPan(pan);
        if (owner_ == Owner::Player) wb->setPitch(pitch);
        wb->play();
        wobbleAcc_ = 0.0f;
    }
}

// ---- impacts ----

void CarAudio::carImpact(const vec3& J, int soundId) {
    if (!active_ || !impacts_ || slot_ < 0) return;
    const float F = (std::fabs(J.z) + std::fabs(J.y)) + std::fabs(J.x);
    const int e = impacts_->play(slot_, F, soundId, !twoD, twoD ? 1.0f : A, twoD ? pan2D : pan);
    if (e >= 0) {
        lastImpact_ = e;
        impactsPlayed_ += impacts_->lastStarted();
    }
}

void CarAudio::impactStep() {
    if (!impacts_ || lastImpact_ < 0) return;
    impacts_->refresh(slot_, lastImpact_, twoD ? 1.0f : A, twoD ? pan2D : pan);
}

// ---- semi / bus ----

void CarAudio::semiStep() {
    Sample* b = at(beeper_);
    Sample* a = at(airBlow_);
    if (!twoD) {
        if (b) {
            b->setVolume(A * beeperVol_);
            b->setPitch(doppler);
            b->setPan(pan);
        }
        if (a) {
            a->setVolume(A * airVol_);
            a->setPitch(doppler);
            a->setPan(pan);
        }
    }
    if (b) {
        if (car_->gearIndex() == 0) {
            if (!b->playing()) b->playLooped();
        } else if (b->playing()) {
            b->stop();
        }
    }
    const bool braking = car_->wheel(0).tune.brakeCoef > 0.5f && car_->wheel(1).tune.brakeCoef > 0.5f;
    const float v = car_->speed();
    if (v > 0.04f) {
        if (!braking) w_.airLatch = false;
    } else {
        if (braking && !w_.airLatch) {
            if (a) a->play();
            w_.airLatch = true;
        }
        if (!braking) w_.airLatch = false;
    }
}

// ---- horn and siren ----

void CarAudio::hornPress() {
    hornHeld_ = true;
    if (!active_ || lightBar_) return;  // a light-bar car's horn runs the siren (lightBarToggled)
    if (kind_ == CarAudioKind::Police) return;  // police audio has no horn playback
    if (Sample* h = at(horn_))
        if (!h->playing()) h->playLooped();
}

void CarAudio::hornRelease() {
    hornHeld_ = false;
    if (!active_ || lightBar_ || kind_ == CarAudioKind::Police) return;
    if (Sample* h = at(horn_)) h->stop();
}

void CarAudio::lightBarToggled(bool on) {
    if (!active_ || kind_ != CarAudioKind::Police) return;  // MM2 would crash; ours: no sound
    if (on) sirenStart(false);
    else sirenStop();
}

float CarAudio::sirenVolume(int i) const {
    const SirenData& sd = w_.siren;
    return i >= 0 && size_t(i) < sd.samples.size() ? sd.samples[size_t(i)].volume : 0.0f;
}

bool CarAudio::sirenSamplePlaying() const {
    if (siren_.current < 0 || size_t(siren_.current) >= sirenSamples_.size()) return false;
    const Sample* s = at(sirenSamples_[size_t(siren_.current)]);
    return s && s->playing();
}

bool CarAudio::explosionPlaying() const {
    if (Sample* e = at(explosion_)) return e->playing();
    return false;
}

void CarAudio::sirenStart(bool counted) {
    if (!active_ || kind_ != CarAudioKind::Police) return;
    if (siren_.mode != SirenMode::Off) return;
    priority = 10;
    keepsSlot = true;
    siren_.mode = counted ? SirenMode::Counted : SirenMode::Uncounted;
    siren_.level = sirenVolume(siren_.current);
    if (counted) ++w_.chaseCount;
    if (slot_ >= 0 && siren_.current >= 0 && size_t(siren_.current) < sirenSamples_.size()) {
        Sample* s = at(sirenSamples_[size_t(siren_.current)]);
        if (s && !s->playing()) {
            if (twoD) {
                s->setVolume(sirenVolume(siren_.current));
                s->setPitch(1.0f);
            } else {
                s->setVolume(A * sirenVolume(siren_.current));
                s->setPitch(doppler);
            }
            s->playLooped();
        }
    }
    if (!twoD && slot_ >= 0) stopEngine();
}

void CarAudio::sirenStop() {
    if (!active_ || kind_ != CarAudioKind::Police) return;
    if (w_.chaseCount > 0 && siren_.mode == SirenMode::Counted) --w_.chaseCount;
    siren_.mode = SirenMode::Off;
    keepsSlot = false;
    priority = 7;
    siren_.explosionFlag = false;
    if (slot_ >= 0 && siren_.current >= 0 && size_t(siren_.current) < sirenSamples_.size())
        if (Sample* s = at(sirenSamples_[size_t(siren_.current)])) s->stop();
}

void CarAudio::explosion() {
    if (!active_ || kind_ != CarAudioKind::Police) return;
    if (slot_ >= 0) {
        if (Sample* e = at(explosion_))
            if (!e->playing()) {
                e->setVolume((twoD ? 1.0f : A) * 1.0f);
                e->play();
            }
    }
    siren_.explosionFlag = true;
    siren_.level = sirenVolume(siren_.current);
    if (w_.chaseCount > 0 && siren_.mode == SirenMode::Counted) --w_.chaseCount;
    if (slot_ >= 0) {
        siren_.windDown = true;
        siren_.t = 0.01f;
        windDownStep(w_.frameTime);
    } else {
        siren_.mode = SirenMode::Off;
    }
}

void CarAudio::windDownStep(float dt) {
    if (siren_.current < 0 || size_t(siren_.current) >= sirenSamples_.size()) return;
    Sample* s = at(sirenSamples_[size_t(siren_.current)]);
    const float a = twoD ? 1.0f : A;
    const float v = sirenVolume(siren_.current);
    if (siren_.t <= 1.0f) {
        if (s) {
            s->setVolume(a * v);
            s->setPitch(doppler * 0.5f);
        }
        siren_.t = dt + siren_.t;
    } else if (siren_.t <= 1.75f) {
        if (s) {
            s->setVolume(a * v);
            s->setPitch(doppler * 0.45f);
        }
        siren_.t = dt + siren_.t;
    } else if (siren_.level > 0.2f) {
        siren_.level = siren_.level * 0.5f;
        if (s) s->setVolume(a * siren_.level);
    } else {
        if (s) s->stop();
        siren_.windDown = false;
        siren_.mode = SirenMode::Off;  // chase count and keeps slot untouched
    }
}

void CarAudio::sirenUpdate(float dt) {
    const SirenData& sd = w_.siren;
    // Stepping (only with a number of siren samples other than 1).
    if (sd.samples.size() != 1 && siren_.current >= 0 && size_t(siren_.current) < sd.samples.size()) {
        const auto& steps = sd.samples[size_t(siren_.current)].steps;
        int& si = siren_.stepIndex[size_t(siren_.current)];
        if (!steps.empty()) {
            if (si < 0 || size_t(si) >= steps.size()) si = 0;
            const auto step = steps[size_t(si)];
            if (siren_.timer >= step.first) {
                if (step.second >= 0 && size_t(step.second) < sd.samples.size()) {
                    si = (si + 1) % int(steps.size());
                    if (Sample* s = at(sirenSamples_[size_t(siren_.current)]))
                        if (s->playing()) s->stop();
                    siren_.current = step.second;
                    if (Sample* n = at(sirenSamples_[size_t(siren_.current)])) n->playLooped();
                    siren_.timer = 0.0f;
                }
            }
        }
        siren_.timer = dt + siren_.timer;
    }
    if (siren_.current < 0 || size_t(siren_.current) >= sirenSamples_.size()) return;
    Sample* s = at(sirenSamples_[size_t(siren_.current)]);
    if (twoD) {
        if (siren_.windDown) windDownStep(dt);
        // Whatever the mode: a wind-down that has just ended leaves its sample looping on.
        if (s && !s->playing()) s->playLooped();
        return;
    }
    if (!siren_.windDown) {
        if (s) {
            const float v = sirenVolume(siren_.current) * A;
            s->setVolume(v > 0.75f ? v : 0.75f);
            s->setPitch(doppler);
        }
    } else {
        windDownStep(dt);
    }
    // Whatever the mode: when the wind-down step has just stopped the sample and turned the mode
    // off, it starts again at the wind-down's last volume and pitch (car-sounds.md, "Explosion").
    if (s) {
        if (!s->playing()) s->playLooped();
        s->setPan(pan);
    }
    if (Sample* e = at(explosion_))
        if (e->playing()) {
            const float v = A * 1.0f;
            e->setVolume(v > 0.85f ? v : 0.85f);
            e->setPan(pan);
        }
}

}  // namespace audio
}  // namespace cc
