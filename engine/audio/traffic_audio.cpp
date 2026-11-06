#include "audio/traffic_audio.h"

#include <cmath>

#include "audio/sound_random.h"
#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {
namespace audio {

namespace {
constexpr float kTrafficDoppler = 0.035263006f;  // float32 of 1 / 28.358332

// Shared by every car and pedestrian of the program (traffic-sounds.md, "Reaction state").
float g_gate = 0.0f;
int g_lastVariant = 0;
int g_lastImpact = 0;
// Count file numbers (a missing count file keeps the previous game's number; 0 at start).
int g_driverK = 0, g_femaleK = 0, g_maleK = 0;

std::vector<std::string> fieldsOf(const std::string& raw) { return splitFields(cutLine(raw)); }
}  // namespace

// ---- files ----

float TrafficEngineData::pitch(float s, float sp, bool decay, float P) const {
    if (rows.empty()) return P;
    if (s < sp || decay) {
        const Row& L = rows.back();
        return (s * L.k) + L.minP;
    }
    for (size_t i = 0; i + 1 < rows.size(); ++i) {
        const Row& r = rows[i];
        if (r.minS <= s && s <= r.maxS) return ((s - r.minS) * r.k) + r.minP;
    }
    return P;
}

TrafficEngineData parseTrafficEngine(LineReader& r) {
    TrafficEngineData d;
    if (!r.valid()) return d;
    d.found = true;
    std::string raw;
    r.read(raw);
    if (r.read(raw)) {
        const auto f = fieldsOf(raw);
        d.sample = fieldS(f, 0);
        d.volume = fieldF(f, 1);
    }
    r.read(raw);
    while (r.read(raw)) {
        const auto f = fieldsOf(raw);
        if (f.size() < 4) continue;  // MM2 would crash; ignored
        TrafficEngineData::Row row;
        row.minS = fieldF(f, 0);
        row.maxS = fieldF(f, 1);
        row.minP = fieldF(f, 2);
        row.maxP = fieldF(f, 3);
        const float span = row.maxS - row.minS;
        const float g = span == 0.0f ? 0.0f : 1.0f / span;
        row.k = (row.maxP - row.minP) * g;
        d.rows.push_back(row);
    }
    return d;
}

TrafficHornData parseTrafficHorn(LineReader& r) {
    TrafficHornData d;
    if (!r.valid()) return d;
    d.found = true;
    std::string raw;
    r.read(raw);
    if (r.read(raw)) {
        const auto f = fieldsOf(raw);
        d.sample = fieldS(f, 0);
        d.volume = fieldF(f, 1);
        d.pitch = fieldF(f, 2);
        d.minForce = fieldF(f, 3);
    }
    while (r.read(raw)) {
        const auto f = fieldsOf(raw);
        if (f.empty()) continue;
        if (iequals(f[0], "horn play duration")) {
            d.patterns.emplace_back();
            continue;
        }
        if (d.patterns.empty() || f.size() < 2) continue;  // MM2 would crash; ignored
        d.patterns.back().push_back({fieldF(f, 0), fieldF(f, 1)});
    }
    return d;
}

VoiceData parseVoiceFile(LineReader& r) {
    VoiceData d;
    if (!r.valid()) return d;
    d.found = true;
    std::vector<std::vector<std::string>> lines;
    std::string raw;
    while (r.read(raw)) lines.push_back(fieldsOf(raw));
    auto isHeader = [](const std::vector<std::string>& f, const char* what) { return !f.empty() && iequals(f[0], what); };
    if (lines.empty() || !(isHeader(lines[0], "min speed") || isHeader(lines[0], "min impact force"))) return d;
    size_t i = 0;
    while (i < lines.size()) {
        const bool band = isHeader(lines[i], "min speed");
        const bool imp = isHeader(lines[i], "min impact force");
        if (!band && !imp) {
            ++i;
            continue;
        }
        ++i;
        const std::vector<std::string> values = i < lines.size() ? lines[i] : std::vector<std::string>();
        ++i;
        ++i;  // one skipped line
        std::vector<std::vector<std::string>> rows;
        while (i < lines.size() && !isHeader(lines[i], "min speed") && !isHeader(lines[i], "min impact force")) {
            if (!lines[i].empty()) rows.push_back(lines[i]);
            ++i;
        }
        if (band) {
            VoiceData::Band b;
            b.minS = fieldF(values, 0);
            b.maxS = fieldF(values, 1);
            b.A = fieldF(values, 2);
            b.B = fieldF(values, 3);
            for (const auto& f : rows)
                if (f.size() >= 2) b.samples.push_back({f[0], fieldF(f, 1)});
            d.bands.push_back(b);
        } else {
            // A second impact block replaces the first.
            d.hasImpact = true;
            d.impactForce = fieldF(values, 0);
            d.impact.clear();
            for (const auto& f : rows)
                if (f.size() >= 3) d.impact.push_back({f[0], fieldF(f, 1), fieldF(f, 2)});
        }
    }
    return d;
}

float readCountFile(const Vfs& vfs, const std::string& path) {
    LineReader r = openCsv(vfs, path);
    if (!r.valid()) return -1.0f;
    std::string raw;
    r.read(raw);
    if (!r.read(raw)) return 0.0f;
    return fieldF(fieldsOf(raw), 0);
}

// ---- the source of one traffic car ----

class TrafficAudio::CarSound : public Source3D {
public:
    explicit CarSound(TrafficAudio& o) : o_(o) {
        attach(o.w_.mgr);
        setDropoffs(0.0f, 100.0f);
        priority = 8;
        dopplerK = kTrafficDoppler;
    }
    int engine = -1, horn = -1, voice = -1;
    float speed = 0, prevSpeed = 0;
    int lastImpact = -1;
    bool echoFlag = false;

protected:
    void onAssigned(int slot) override {
        if (o_.impacts_) o_.impacts_->slotAssigned(slot);
    }
    void onReleased() override {
        const int k = slot_;
        if (echoFlag) setEcho(false);
        echoFlag = false;
        if (engine >= 0)
            if (Sample* s = o_.engines_[size_t(engine)]->smp.at(k)) s->stop();
        if (horn >= 0) {
            HornFile& h = *o_.horns_[size_t(horn)];
            if (Sample* s = h.smp.at(k)) s->stop();
            HornCopy& c = h.copy[k];
            if (c.current >= 0 && size_t(c.current) < c.pats.size()) c.pats[size_t(c.current)].state = HornCopy::Idle;
        }
        if (voice >= 0) {
            VoiceFile::Copy& c = o_.voices_[size_t(voice)]->copy[k];
            for (auto& b : c.bands) b.armed = false;
            c.armed = false;
        }
    }
    void setEcho(bool on) {
        const int k = slot_;
        EchoParams p;
        p.delay = o_.w_.mgr.echoDelay();
        p.gain = 0.96f;
        if (engine >= 0)
            if (Sample* s = o_.engines_[size_t(engine)]->smp.at(k)) s->setEcho(on, p);
        if (horn >= 0)
            if (Sample* s = o_.horns_[size_t(horn)]->smp.at(k)) s->setEcho(on, p);
        if (voice >= 0) {
            VoiceFile::Copy& c = o_.voices_[size_t(voice)]->copy[k];
            for (auto& band : c.bandSamples)
                for (auto& s : band) s->setEcho(on, p);
            for (auto& s : c.impactSamples) s->setEcho(on, p);
        }
    }
    void tickEcho(float dt) {
        const int k = slot_;
        if (engine >= 0)
            if (Sample* s = o_.engines_[size_t(engine)]->smp.at(k)) s->echoTick(dt);
        if (horn >= 0)
            if (Sample* s = o_.horns_[size_t(horn)]->smp.at(k)) s->echoTick(dt);
        if (voice >= 0) {
            VoiceFile::Copy& c = o_.voices_[size_t(voice)]->copy[k];
            for (auto& band : c.bandSamples)
                for (auto& s : band) s->echoTick(dt);
            for (auto& s : c.impactSamples) s->echoTick(dt);
        }
    }
    void slotUpdate(float dt) override {
        AudioWorld& w = o_.w_;
        const int k = slot_;
        // 1. Echo.
        if (w.mgr.echo != echoFlag) {
            setEcho(w.mgr.echo);
            echoFlag = w.mgr.echo;
        }
        if (echoFlag) tickEcho(dt);
        // 2.
        if (w.mgr.pastMax(*this)) {
            releaseSlot();
            return;
        }
        // 3.
        w.mgr.compute(*this, dt);
        const float a = A, p = pan, d = doppler, D2 = d2;
        // 4. Deceleration test.
        float s;
        bool decay;
        if ((prevSpeed - speed) > 4.0f) {
            s = prevSpeed * 0.95f;
            prevSpeed = s;
            decay = true;
        } else {
            s = speed;
            decay = false;
        }
        // 5. Horn.
        HornFile* h = horn >= 0 ? o_.horns_[size_t(horn)].get() : nullptr;
        if (h) {
            HornCopy& c = h->copy[k];
            if (c.current >= 0 && size_t(c.current) < c.pats.size() && c.pats[size_t(c.current)].state != HornCopy::Idle)
                if (Sample* hs = h->smp.at(k)) {
                    hs->setVolume(a * h->d.volume);
                    hs->setPitch(d * h->d.pitch);
                    hs->setPan(p);
                }
        }
        // 6-7. Engine (pitch from the previous update of this copy), then the next pitch.
        if (engine >= 0) {
            EngineFile& e = *o_.engines_[size_t(engine)];
            if (Sample* es = e.smp.at(k)) {
                es->setVolume(a * e.d.volume);
                es->setPitch(d * e.P[k]);
                es->setPan(p);
                if (!es->playing()) es->playLooped();
            }
            e.P[k] = e.d.pitch(s, prevSpeed, decay, e.P[k]);
        }
        // 8. Voices.
        if (voice >= 0) {
            VoiceFile& v = *o_.voices_[size_t(voice)];
            VoiceFile::Copy& c = v.copy[k];
            c.a = a;
            c.p = p;
            c.D2 = D2;
            for (size_t b = 0; b < c.bandSamples.size(); ++b)
                for (size_t i = 0; i < c.bandSamples[b].size(); ++i) {
                    Sample* x = c.bandSamples[b][i].get();
                    if (x->playing()) {
                        x->setVolume(v.d.bands[b].samples[i].second * a);
                        x->setPan(p);
                    }
                }
            for (size_t i = 0; i < c.impactSamples.size(); ++i) {
                Sample* x = c.impactSamples[i].get();
                if (x->playing()) {
                    x->setVolume(v.d.impact[i].volume * a);
                    x->setPan(p);
                }
            }
        }
        // 9. Impact sounds.
        if (o_.impacts_ && lastImpact >= 0) o_.impacts_->refresh(k, lastImpact, a, p);
        // 10.
        if (!decay) prevSpeed = speed;
        // 11. Horn pattern.
        if (h) o_.hornUpdate(*h, k, w.frameTime);
    }

private:
    TrafficAudio& o_;
};

// ---- the source of one pedestrian ----

class TrafficAudio::PedSound : public Source3D {
public:
    explicit PedSound(TrafficAudio& o) : o_(o) {
        attach(o.w_.mgr);
        setDropoffs(0.0f, 40.0f);
        priority = 8;
        dopplerK = kTrafficDoppler;  // unused: voices have no pitch
    }
    int file = -1;
    bool echoFlag = false;
    bool anyPlaying() const {
        if (file < 0 || slot_ < 0) return false;
        const VoiceFile::Copy& c = o_.pedVoices_[size_t(file)]->copy[slot_];
        for (const auto& band : c.bandSamples)
            for (const auto& s : band)
                if (s->playing()) return true;
        return false;
    }

protected:
    void setEcho(bool on) {
        if (file < 0) return;
        EchoParams p;
        p.delay = o_.w_.mgr.echoDelay();
        p.gain = 0.96f;
        for (auto& band : o_.pedVoices_[size_t(file)]->copy[slot_].bandSamples)
            for (auto& s : band) s->setEcho(on, p);
    }
    void onReleased() override {
        if (echoFlag) setEcho(false);
        echoFlag = false;
        if (file < 0) return;
        VoiceFile::Copy& c = o_.pedVoices_[size_t(file)]->copy[slot_];
        for (auto& b : c.bands) b.armed = false;
    }
    void slotUpdate(float dt) override {
        AudioWorld& w = o_.w_;
        if (w.mgr.echo != echoFlag) {
            setEcho(w.mgr.echo);
            echoFlag = w.mgr.echo;
        }
        if (echoFlag && file >= 0)
            for (auto& band : o_.pedVoices_[size_t(file)]->copy[slot_].bandSamples)
                for (auto& s : band) s->echoTick(dt);
        if (w.mgr.pastMax(*this)) {
            releaseSlot();
            return;
        }
        w.mgr.compute(*this, dt);
        if (file < 0) return;
        VoiceFile::Copy& c = o_.pedVoices_[size_t(file)]->copy[slot_];
        c.a = A;
        c.p = pan;
        c.D2 = d2;
    }

private:
    TrafficAudio& o_;
};

// ---- TrafficAudio ----

TrafficAudio::TrafficAudio(AudioWorld& w) : w_(w) {}

TrafficAudio::~TrafficAudio() {
    clear();
    clearPeds();
}

float TrafficAudio::gateTime() { return g_gate; }
int TrafficAudio::driverVoiceNumber() { return g_driverK; }

void TrafficAudio::readCountFiles(AudioWorld& w) {
    const std::string sfx = w.london ? "_l" : "_s";
    auto draw = [&](const std::string& path, int& k) {
        const float n = readCountFile(w.vfs, path);
        if (n < 0.0f) return;  // missing: the previous game's number stays
        k = int(std::trunc(rndS(1.0f, n + 0.25f)));
    };
    draw("aud/creaturedata/numambcarvoicefiles" + sfx, g_driverK);
    draw("aud/creaturedata/numfemalepedvoicefiles", g_femaleK);
    draw("aud/creaturedata/nummalepedvoicefiles", g_maleK);
}

bool TrafficAudio::pedHolds(int file, int k) const {
    const Source3D* h = w_.mgr.holder(k);
    for (const auto& p : peds_)
        if (p.get() == h) return p->file == file;
    return false;
}

void TrafficAudio::clear() {
    if (ts_) ts_->setSoundHooks(nullptr);
    ts_ = nullptr;
    for (auto& c : cars_) c->releaseSlot();
    cars_.clear();
    engines_.clear();
    horns_.clear();
    voices_.clear();
    nameCache_.clear();
    impacts_ = nullptr;
}

int TrafficAudio::engineFile(const std::string& type) {
    auto load = [&](const std::string& name) -> int {
        auto it = nameCache_.find(name);
        if (it != nameCache_.end()) return it->second;
        LineReader r = openCsv(w_.vfs, name);
        int idx = -1;
        if (r.valid()) {
            auto e = std::make_unique<EngineFile>();
            e->d = parseTrafficEngine(r);
            if (!e->d.sample.empty()) e->smp = w_.cache.get("engines\\" + e->d.sample, true);
            idx = int(engines_.size());
            engines_.push_back(std::move(e));
        }
        nameCache_[name] = idx;
        return idx;
    };
    int i = load("aud/cardata/ambient/" + type + "_engine");
    if (i < 0) i = load("aud/cardata/ambient/default_engine");
    return i;
}

int TrafficAudio::hornFile(const std::string& type) {
    auto load = [&](const std::string& name) -> int {
        auto it = nameCache_.find(name);
        if (it != nameCache_.end()) return it->second;
        LineReader r = openCsv(w_.vfs, name);
        int idx = -1;
        if (r.valid()) {
            auto h = std::make_unique<HornFile>();
            h->d = parseTrafficHorn(r);
            if (!h->d.sample.empty()) h->smp = w_.cache.get("horns\\" + h->d.sample, true);
            for (HornCopy& c : h->copy) c.pats.assign(h->d.patterns.size(), HornCopy::Pattern());
            idx = int(horns_.size());
            horns_.push_back(std::move(h));
        }
        nameCache_[name] = idx;
        return idx;
    };
    int i = load("aud/cardata/ambient/" + type + "_horn");
    if (i < 0) i = load("aud/cardata/ambient/default_horn");
    return i;
}

int TrafficAudio::loadVoice(std::vector<std::unique_ptr<VoiceFile>>& list, std::map<std::string, int>& cache,
                            const std::string& name) {
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    LineReader r = openCsv(w_.vfs, name);
    int idx = -1;
    if (r.valid()) {
        auto v = std::make_unique<VoiceFile>();
        v->d = parseVoiceFile(r);
        for (int k = 0; k < Manager3D::kSlots; ++k) {
            VoiceFile::Copy& c = v->copy[k];
            c.bands.assign(v->d.bands.size(), VoiceFile::BandState());
            for (const auto& b : v->d.bands) {
                c.bandSamples.emplace_back();
                for (const auto& s : b.samples) {
                    auto smp = std::make_unique<Sample>(w_.sys, false);
                    smp->setFile(0, soundPath("Creature3D\\" + s.first));
                    c.bandSamples.back().push_back(std::move(smp));
                }
            }
            for (const auto& s : v->d.impact) {
                auto smp = std::make_unique<Sample>(w_.sys, false);
                smp->setFile(0, soundPath("Creature3D\\" + s.name));
                c.impactSamples.push_back(std::move(smp));
            }
        }
        idx = int(list.size());
        v->index = idx;
        v->ped = &list == &pedVoices_;
        list.push_back(std::move(v));
    }
    cache[name] = idx;
    return idx;
}

int TrafficAudio::voiceFile(const std::string& type) {
    const std::string sfx = w_.london ? "_l" : "_s";
    int i = loadVoice(voices_, nameCache_, "aud/creaturedata/" + type + "_ambcarvoice" + sfx);
    if (i < 0) i = loadVoice(voices_, nameCache_, "aud/creaturedata/default_ambcarvoice" + sfx + std::to_string(g_driverK));
    return i;
}

void TrafficAudio::setup(ai::TrafficSystem& ts) {
    clear();
    ts_ = &ts;
    const auto& cars = ts.cars();
    for (size_t i = 0; i < cars.size(); ++i) {
        auto cs = std::make_unique<CarSound>(*this);
        const ai::TrafficCar& c = cars[i];
        if (c.type >= 0) {
            const std::string& type = ts.types()[size_t(c.type)].name;
            cs->label = "traffic " + type;
            cs->engine = engineFile(type);
            cs->horn = hornFile(type);
            impacts_ = w_.trafficImpactTable();
            cs->voice = voiceFile(type);
        }
        cars_.push_back(std::move(cs));
    }
    ts.setSoundHooks(this);
}

void TrafficAudio::clearPeds() {
    for (auto& p : peds_) p->releaseSlot();
    peds_.clear();
    pedVoices_.clear();
    pedCache_.clear();
}

void TrafficAudio::setupPeds(const ai::PedestrianSystem& ps) {
    clearPeds();
    for (const ai::Pedestrian& p : ps.peds()) {
        auto s = std::make_unique<PedSound>(*this);
        s->label = std::string(p.female ? "pedestrian (female) " : "pedestrian (male) ") + std::to_string(p.index);
        // traffic-sounds.md "Files": no city suffix, the number drawn from the count file.
        const std::string name = p.female ? "aud/creaturedata/default_fpedvoice" + std::to_string(g_femaleK)
                                          : "aud/creaturedata/default_mpedvoice" + std::to_string(g_maleK);
        s->file = loadVoice(pedVoices_, pedCache_, name);
        peds_.push_back(std::move(s));
    }
}

void TrafficAudio::pedFrame(const ai::PedestrianSystem& ps) {
    const auto& peds = ps.peds();
    // The start of each pedestrian's update: a slot held while none of its voices plays goes back.
    for (size_t i = 0; i < peds_.size() && i < peds.size(); ++i) {
        PedSound& s = *peds_[i];
        s.position = peds[i].p;
        if (s.holdsSlot() && !s.anyPlaying()) s.releaseSlot();
    }
    // Scream events: a slot within 40 m, then every triggering band may arm a variant.
    for (const ai::PedVoiceEvent& e : ps.voiceEvents()) {
        if (e.ped < 0 || size_t(e.ped) >= peds_.size()) continue;
        ++counts_.screamEvents;
        PedSound& s = *peds_[size_t(e.ped)];
        if (s.file < 0) continue;
        s.position = e.pos;
        if (!s.holdsSlot()) s.requestSlot();
        if (s.holdsSlot()) avoidanceReaction(*pedVoices_[size_t(s.file)], s.slot());
    }
}

int TrafficAudio::pedVoiceFiles() const {
    int n = 0;
    for (const auto& v : pedVoices_)
        if (!v->d.bands.empty()) ++n;
    return n;
}

int TrafficAudio::screamingPeds() const {
    int n = 0;
    for (const auto& p : peds_)
        if (p->holdsSlot()) ++n;
    return n;
}

void TrafficAudio::carUpdated(const ai::TrafficCar& c) {
    if (c.index < 0 || size_t(c.index) >= cars_.size()) return;
    CarSound& s = *cars_[size_t(c.index)];
    s.position = c.pos;
    const bool stopped = c.state == ai::TrafficKnocked || c.state == ai::TrafficOut || c.wreck;
    s.speed = stopped ? 0.0f : c.v;
    if (!s.holdsSlot()) s.requestSlot();
}

void TrafficAudio::placed(const ai::TrafficCar& c) {
    if (c.index < 0 || size_t(c.index) >= cars_.size()) return;
    CarSound& s = *cars_[size_t(c.index)];
    s.reset3D();
    s.speed = s.prevSpeed = 0.0f;
}

void TrafficAudio::hornStart(HornFile& h, int k, int pattern) {
    HornCopy& c = h.copy[k];
    if (pattern < 0 || size_t(pattern) >= c.pats.size()) return;
    HornCopy::Pattern& p = c.pats[size_t(pattern)];
    if (p.state != HornCopy::Idle) return;
    if (Sample* s = h.smp.at(k)) s->playLooped();
    p.t = 0.0f;
    p.state = HornCopy::Playing;
}

void TrafficAudio::hornStopPattern(HornFile& h, int k, int pattern) {
    HornCopy& c = h.copy[k];
    if (pattern < 0 || size_t(pattern) >= c.pats.size()) return;
    HornCopy::Pattern& p = c.pats[size_t(pattern)];
    p.seg = 0;
    p.state = HornCopy::Idle;
    if (Sample* s = h.smp.at(k)) s->stop();
}

void TrafficAudio::hornUpdate(HornFile& h, int k, float F) {
    HornCopy& c = h.copy[k];
    if (c.current < 0 || size_t(c.current) >= c.pats.size()) return;
    stepHornPattern(c.pats[size_t(c.current)], h.d.patterns[size_t(c.current)], F, h.smp.at(k));
}

void TrafficAudio::stepHornPattern(HornCopy::Pattern& p, const std::vector<std::pair<float, float>>& segs, float F,
                                   Sample* s) {
    switch (p.state) {
    case HornCopy::Playing: {
        p.t = F + p.t;
        const float play = p.seg >= 0 && size_t(p.seg) < segs.size() ? segs[size_t(p.seg)].first : 0.0f;
        if (p.t >= play) {
            if (s) s->stop();
            p.t = 0.0f;
            p.state = HornCopy::Pausing;
        } else if (s && !s->playing()) {
            s->playLooped();
        }
        break;
    }
    case HornCopy::Pausing: {
        p.t = F + p.t;
        const float pause = p.seg >= 0 && size_t(p.seg) < segs.size() ? segs[size_t(p.seg)].second : 0.0f;
        if (p.t >= pause) {
            if (p.seg + 1 < int(segs.size())) {
                ++p.seg;
                p.state = HornCopy::Playing;
                if (s) s->playLooped();
            } else {
                p.state = HornCopy::Idle;
                p.seg = 0;
                if (s) s->stop();
            }
            p.t = 0.0f;
        } else if (s && s->playing()) {
            s->stop();
        }
        break;
    }
    case HornCopy::Idle:
        if (s && s->playing()) s->stop();
        break;
    }
}

void TrafficAudio::voicePlay(VoiceFile& v, int k, Sample* s, float fileVolume, bool impact, int index) {
    VoiceFile::Copy& c = v.copy[k];
    if (!s || s->playing()) return;  // stays armed
    s->setVolume(c.a * fileVolume);
    s->setPan(c.p);
    s->play();
    if (v.ped) ++counts_.screams;
    else ++counts_.shouts;
    if (impact) {
        c.armed = false;
        g_gate = 0.0f;
        g_lastImpact = index;
    }
}

void TrafficAudio::aiFrameStart(float F, float s) {
    // The driver voice files, then the pedestrian ones; each update adds F to the gate time.
    g_gate = F + g_gate;
    voiceFrame(voices_, F, s);
    g_gate = F + g_gate;
    voiceFrame(pedVoices_, F, s);
}

void TrafficAudio::voiceFrame(std::vector<std::unique_ptr<VoiceFile>>& list, float F, float s) {
    for (auto& vp : list) {
        VoiceFile& v = *vp;
        for (int k = 0; k < Manager3D::kSlots; ++k) {
            VoiceFile::Copy& c = v.copy[k];
            for (size_t b = 0; b < v.d.bands.size(); ++b) {
                const VoiceData::Band& band = v.d.bands[b];
                VoiceFile::BandState& st = c.bands[b];
                if (band.minS <= s && s <= band.maxS) {
                    st.tin = F + st.tin;
                    st.tout = 0.0f;
                } else {
                    st.tout = F + st.tout;
                    st.tin = 0.0f;
                }
                if (st.armed) {
                    st.wait = F + st.wait;
                    if (st.wait >= 5.0f) {
                        st.armed = false;  // ready again: the shout is dropped
                    } else if (v.ped && !pedHolds(v.index, k)) {
                        // a pedestrian voice plays only while its pedestrian holds the slot (stays armed)
                    } else if (c.D2 <= 2500.0f && st.variant >= 0 && size_t(st.variant) < c.bandSamples[b].size()) {
                        Sample* x = c.bandSamples[b][size_t(st.variant)].get();
                        if (!x->playing()) {
                            voicePlay(v, k, x, band.samples[size_t(st.variant)].second, false, st.variant);
                            st.armed = false;
                        }
                    }
                }
            }
            if (c.armed && c.chosen >= 0 && size_t(c.chosen) < v.d.impact.size()) {
                c.timer = F + c.timer;
                if (c.timer >= v.d.impact[size_t(c.chosen)].delay) {
                    voicePlay(v, k, c.impactSamples[size_t(c.chosen)].get(), v.d.impact[size_t(c.chosen)].volume, true,
                              c.chosen);
                    c.timer = 0.0f;
                }
            }
        }
    }
}

void TrafficAudio::avoidanceReaction(VoiceFile& v, int k) {
    VoiceFile::Copy& c = v.copy[k];
    for (size_t b = 0; b < v.d.bands.size(); ++b) {
        const VoiceData::Band& band = v.d.bands[b];
        VoiceFile::BandState& st = c.bands[b];
        if (!(st.tin >= band.A || st.tout <= band.B)) continue;
        if (st.armed) continue;
        const int n = int(band.samples.size());
        int x = int(std::trunc(rndS(float(2 * n))));
        if (x < n) {
            if (x == g_lastVariant) {
                x = x + 1;
                if (x == n) x = 0;
            }
            st.variant = x;
            g_lastVariant = x;
            st.wait = 0.0f;
            st.armed = true;
        }
    }
}

void TrafficAudio::avoidStart(const ai::TrafficCar& tc) {
    if (tc.index < 0 || size_t(tc.index) >= cars_.size()) return;
    CarSound& s = *cars_[size_t(tc.index)];
    if (!s.holdsSlot() || s.horn < 0) return;
    const int k = s.slot();
    HornFile& h = *horns_[size_t(s.horn)];
    HornCopy& hc = h.copy[k];
    const int c = int(h.d.patterns.size()) - 1;
    if (c < 0) return;  // no patterns: never honks
    if (hc.current >= 0 && size_t(hc.current) < hc.pats.size() && hc.pats[size_t(hc.current)].state != HornCopy::Idle) return;
    const int hp = int(std::trunc(rndS(float(2 * c) - 0.01f)));
    if (hp >= c || hp < 0) return;
    hornStart(h, k, hp);
    hc.current = hp;
    ++counts_.honks;
    if (s.voice >= 0) avoidanceReaction(*voices_[size_t(s.voice)], k);
}

void TrafficAudio::impact(int car, const vec3& J, int otherSound) {
    if (car < 0 || size_t(car) >= cars_.size()) return;
    CarSound& s = *cars_[size_t(car)];
    if (!s.holdsSlot()) return;
    const int k = s.slot();
    const float L = (std::fabs(J.z) + std::fabs(J.y)) + std::fabs(J.x);
    // The impact sound.
    if (impacts_) {
        const int e = impacts_->play(k, L, otherSound, true, s.A, s.pan);
        if (e >= 0) {
            s.lastImpact = e;
            counts_.impactSounds += impacts_->lastStarted();
        }
    }
    // The stuck horn (25 %).
    if (s.horn >= 0) {
        HornFile& h = *horns_[size_t(s.horn)];
        const int c = int(h.d.patterns.size()) - 1;
        if (c >= 0 && L >= h.d.minForce && rndS(10.0f) >= 7.5) {
            HornCopy& hc = h.copy[k];
            bool start = true;
            if (hc.current >= 0 && size_t(hc.current) < hc.pats.size() && hc.pats[size_t(hc.current)].state != HornCopy::Idle) {
                if (hc.current == c) start = false;
                else hornStopPattern(h, k, hc.current);
            }
            if (start) {
                hornStart(h, k, c);
                hc.current = c;
                ++counts_.stuckHorns;
            }
        }
    }
    // The impact reaction of the driver.
    if (s.voice >= 0) {
        VoiceFile& v = *voices_[size_t(s.voice)];
        VoiceFile::Copy& vc = v.copy[k];
        if (v.d.hasImpact && !vc.armed && g_gate >= 60.0f && L >= v.d.impactForce && !v.d.impact.empty()) {
            vc.timer = 0.0f;
            const int n = int(v.d.impact.size());
            int i = int(std::trunc(rndS(float(n) - 0.01f)));
            if (i == g_lastImpact) {
                i = i + 1;
                if (i == n) i = 0;
            }
            if (i < 0 || i >= n) i = 0;
            vc.chosen = i;
            g_lastImpact = i;
            vc.armed = true;
        }
    }
}

int TrafficAudio::soundingCars() const {
    int n = 0;
    for (const auto& c : cars_)
        if (c->holdsSlot()) ++n;
    return n;
}

const HornCopy* TrafficAudio::hornCopy(int car) const {
    if (car < 0 || size_t(car) >= cars_.size()) return nullptr;
    const CarSound& s = *cars_[size_t(car)];
    if (s.horn < 0 || !s.holdsSlot()) return nullptr;
    return &horns_[size_t(s.horn)]->copy[s.slot()];
}

}  // namespace audio
}  // namespace cc
