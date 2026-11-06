#include "music/synth.h"

#include <algorithm>
#include <climits>
#include <cmath>

namespace cc {
namespace music {

namespace {

constexpr double kPi = 3.14159265358979323846;

// DLS connection ids (directmusic.md, "Articulation").
enum : uint16_t {
    SrcNone = 0,
    SrcLfo = 1,
    SrcVelocity = 2,
    SrcKey = 3,
    SrcPitchEnv = 5,
    CtlNone = 0,
    CtlModWheel = 0x81,
    DstAttenuation = 0x001,
    DstPitch = 0x003,
    DstPan = 0x004,
    DstLfoFreq = 0x104,
    DstLfoDelay = 0x105,
    DstVolAttack = 0x206,
    DstVolDecay = 0x207,
    DstVolRelease = 0x209,
    DstVolSustain = 0x20A,
    DstPitchAttack = 0x30A,
    DstPitchDecay = 0x30B,
    DstPitchRelease = 0x30D,
    DstPitchSustain = 0x30E,
};

int q15(double x) {
    double v = x * 32768.0 + 0.5;
    if (v < -32768.0) v = -32768.0;
    if (v > 32767.0) v = 32767.0;
    return int(std::trunc(v));
}

inline int16_t sat16(int32_t v) { return int16_t(v < -32768 ? -32768 : (v > 32767 ? 32767 : v)); }

float g_ampTable[1001];
bool g_ampReady = false;

// A channel's gain in amplitude: 4095 × 10^(k/200) / 4096, k = trunc(gain/10) in −1000…0.
inline float ampOf(int gainHundredths) {
    int k = gainHundredths / 10;  // trunc toward zero
    if (k < -1000) k = -1000;
    if (k > 0) k = 0;
    return g_ampTable[k + 1000];
}

}  // namespace

// ---- envelopes

int DlsSynth::Env::level(int64_t u, bool volume) const {
    (void)volume;
    if (released) {
        int64_t e = u - relU;
        if (e < 0) e = 0;
        if (e >= relLen) return 0;
        return int(L0 - int64_t(L0) * e / relLen);
    }
    if (u < attack) {
        int x = int(u * 1000 / attack);
        if (x > 1000) x = 1000;
        return x;  // the volume envelope maps it through A[] (caller)
    }
    int64_t d = u - attack;
    if (d < decay) return int(1000 - int64_t(1000 - sustain) * d / decay);
    return sustain;
}

void DlsSynth::Env::release_(int64_t u, int levelNow, int maxLen, bool volume) {
    (void)volume;
    L0 = levelNow;
    int64_t len = int64_t(release) * levelNow / 1000;
    if (maxLen > 0 && len > maxLen) len = maxLen;
    if (len < 1) len = 1;
    relLen = int(len);
    relU = u;
    released = true;
}

// ---- set-up

DlsSynth::DlsSynth(int sampleRate) : fs_(sampleRate) {
    if (!g_ampReady) {
        for (int k = -1000; k <= 0; ++k) g_ampTable[k + 1000] = float(4095.0 * std::pow(10.0, double(k) / 200.0) / 4096.0);
        g_ampReady = true;
    }
    V_[0] = -9600;
    for (int v = 1; v < 128; ++v) V_[v] = int(std::trunc(4000.0 * std::log10(double(v) / 127.0)));
    P_[0] = -2500;
    for (int i = 1; i < 128; ++i) P_[i] = int(std::trunc(1000.0 * std::log10(double(i) / 127.0)));
    A_[0] = 0;
    for (int i = 1; i <= 200; ++i) {
        double r = double(i) / 200.0;
        A_[i] = int(std::trunc(1000.0 + (10000.0 / 96.0) * std::log10(r * r)));
    }
    for (int i = 0; i < 256; ++i) L_[i] = int(std::trunc(100.0 * std::sin(2.0 * kPi * double(i) / 256.0)));
    voices_.resize(kVoices + kReserve);
    setupReverb();
    reset();
}

void DlsSynth::resetChannels() {
    for (int p = 0; p < kChannels; ++p) {
        ch_[p] = Channel();
        const int m = p % 16;
        ch_[p].priority = m == 9 ? 0x80000010u : (m < 9 ? 0x8000000Fu - uint32_t(m) : 0x8000000Fu - uint32_t(m - 1));
    }
}

void DlsSynth::reset() {
    resetChannels();
    for (Voice& v : voices_) v.used = false;
    for (int i = 0; i < 4; ++i) {
        std::fill(line_[i].begin(), line_[i].end(), 0);
        linePos_[i] = 0;
        yPrev_[i] = 0;
    }
    for (int i = 0; i < 2; ++i) {
        std::fill(pair_[i].begin(), pair_[i].end(), 0);
        pairPos_[i] = 0;
    }
}

void DlsSynth::download(const DlsInstrument* inst, const DlsCollection* owner, bool gm) {
    if (!inst) return;
    for (const Download& d : downloads_)
        if (d.inst == inst) return;
    downloads_.push_back({inst, owner, inst->patch(), gm});
}

void DlsSynth::clearDownloads() { downloads_.clear(); }

bool DlsSynth::hasPatch(uint32_t patch) const {
    for (const Download& d : downloads_)
        if (d.patch == patch) return true;
    return false;
}

const DlsInstrument* DlsSynth::findInstrument(uint32_t patch, int key, int vel) const {
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = downloads_.size(); i-- > 0;) {
            const Download& d = downloads_[i];
            if (d.gm != (pass == 1) || d.patch != patch) continue;
            for (const DlsRegion& rg : d.inst->regions) {
                const int vlo = (rg.velLo == 0 && rg.velHi == 0) ? 0 : rg.velLo;
                const int vhi = (rg.velLo == 0 && rg.velHi == 0) ? 127 : rg.velHi;
                if (key >= rg.keyLo && key <= rg.keyHi && vel >= vlo && vel <= vhi) return d.inst;
            }
        }
    }
    return nullptr;
}

void DlsSynth::setChannelPriority(int pch, uint32_t priority) {
    if (pch >= 0 && pch < kChannels) ch_[pch].priority = priority;
}

void DlsSynth::setActive(bool on) {
    if (on == active_) return;
    active_ = on;
    if (!on)
        for (Voice& v : voices_) v.used = false;
}

// ---- notes

DlsSynth::Voice* DlsSynth::allocate(uint32_t priority) {
    for (Voice& v : voices_)
        if (!v.used) return &v;
    // Steal one of equal or lower priority: released and quieter voices first, else the oldest
    // held one; it is cut at once.
    Voice* best = nullptr;
    for (Voice& v : voices_) {
        if (v.priority > priority) continue;
        const bool rel = v.vol.released;
        if (!best) {
            best = &v;
            continue;
        }
        const bool brel = best->vol.released;
        if (rel != brel) {
            if (rel) best = &v;
            continue;
        }
        if (rel ? v.lastGain < best->lastGain : v.age < best->age) best = &v;
    }
    if (best) best->used = false;
    return best;
}

void DlsSynth::noteOn(int pch, int key, int vel) {
    if (pch < 0 || pch >= kChannels || key < 0 || key > 127) return;
    if (vel <= 0) {
        noteOff(pch, key);
        return;
    }
    if (!active_) return;
    if (vel > 127) vel = 127;
    const Channel& c = ch_[pch];
    const bool drums = (pch % 16) == 9;
    uint32_t patch = c.patch | (drums ? 0x80000000u : 0u);
    const DlsInstrument* inst = findInstrument(patch, key, vel);
    if (!inst && drums) inst = findInstrument(0x80000000u, key, vel);
    if (onNote) onNote(pch, key, vel, inst);
    if (!inst) {
        ++notesDropped_;
        if (missing_.size() < 64) ++missing_[(uint64_t(pch) << 32) | patch];
        return;
    }
    const uint64_t note = ++noteSerial_;
    bool any = false;
    for (const DlsRegion& rg : inst->regions) {
        const int vlo = (rg.velLo == 0 && rg.velHi == 0) ? 0 : rg.velLo;
        const int vhi = (rg.velLo == 0 && rg.velHi == 0) ? 127 : rg.velHi;
        if (key < rg.keyLo || key > rg.keyHi || vel < vlo || vel > vhi) continue;
        if (!(rg.options & 1)) {
            for (Voice& v : voices_)
                if (v.used && v.pch == pch && v.key == key && v.region == &rg) quickStop(v);
        }
        Voice* v = allocate(c.priority);
        if (!v) continue;
        startVoice(*v, pch, key, vel, *inst, rg, note);
        any = true;
    }
    if (any) ++notesStarted_;
    else ++notesDropped_;
}

void DlsSynth::startVoice(Voice& v, int pch, int key, int vel, const DlsInstrument& inst, const DlsRegion& rg, uint64_t note) {
    const DlsCollection* owner = nullptr;
    for (const Download& d : downloads_)
        if (d.inst == &inst) owner = d.owner;
    if (!owner || rg.waveIndex >= owner->waves.size()) return;
    const DlsWave& w = owner->waves[rg.waveIndex];
    if (w.samples.empty()) return;
    const WaveSample& ws = rg.hasWsmp ? rg.wsmp : w.wsmp;
    const Channel& c = ch_[pch];
    v = Voice();
    v.used = true;
    v.pch = pch;
    v.key = key;
    v.vel = vel;
    v.note = note;
    v.age = ++ageSerial_;
    v.priority = c.priority;
    v.region = &rg;
    v.data = w.samples.data();
    v.len = uint32_t(w.samples.size());
    v.looped = ws.looped && ws.loopLength >= 6 && ws.loopStart < v.len;
    if (v.looped) {
        v.loopStart = ws.loopStart;
        v.loopEnd = std::min<uint32_t>(ws.loopStart + ws.loopLength, v.len);
        if (v.loopEnd - v.loopStart < 6) v.looped = false;
    }
    v.rateRatio = double(w.rate) / double(fs_);

    const Articulation* art = rg.hasArt ? &rg.art : (inst.hasArt ? &inst.art : nullptr);
    auto get = [&](uint16_t s, uint16_t ctl, uint16_t d, int32_t& out) -> bool {
        return art && art->find(s, ctl, d, out);
    };
    auto samples = [&](uint16_t d) -> int64_t {
        int32_t sc = 0;
        if (!get(SrcNone, CtlNone, d, sc) || sc == INT32_MIN) return 0;
        return int64_t(std::trunc(double(fs_) * std::pow(2.0, double(sc) / (1200.0 * 65536.0))));
    };
    auto sustainOf = [&](uint16_t d) -> int {
        int32_t sc = 0;
        if (!get(SrcNone, CtlNone, d, sc)) return 1000;
        int s = sc >> 16;
        return std::clamp(s, 0, 1000);
    };
    auto tcOf = [&](uint16_t s, uint16_t d) -> double {
        int32_t sc = 0;
        if (!get(s, CtlNone, d, sc)) return 0.0;
        return double(sc) / 65536.0;
    };
    const int minT = std::max(1, fs_ / 1000);
    // Volume envelope.
    {
        // The fs/1000 minimum first, then the velocity scaling (a scaled attack can end below it).
        double atk = double(std::max<int64_t>(minT, samples(DstVolAttack)));
        atk *= std::pow(2.0, std::trunc(double(vel) * tcOf(SrcVelocity, DstVolAttack) / 127.0) / 1200.0);
        v.vol.attack = std::max(1, int(atk));
        v.vol.sustain = sustainOf(DstVolSustain);
        double dec = double(samples(DstVolDecay));
        dec *= std::pow(2.0, std::trunc(double(key) * tcOf(SrcKey, DstVolDecay) / 127.0) / 1200.0);
        dec = dec * double(1000 - v.vol.sustain) / 1000.0;
        v.vol.decay = int(dec);
        v.vol.release = std::max(minT, int(samples(DstVolRelease)));
    }
    // Pitch envelope.
    {
        v.pitch.attack = std::max(1, int(samples(DstPitchAttack)));
        v.pitch.sustain = sustainOf(DstPitchSustain);
        double dec = double(samples(DstPitchDecay));
        dec *= std::pow(2.0, std::trunc(double(key) * tcOf(SrcKey, DstPitchDecay) / 127.0) / 1200.0);
        dec = dec * double(1000 - v.pitch.sustain) / 1000.0;
        v.pitch.decay = int(dec);
        v.pitch.release = std::max(1, int(samples(DstPitchRelease)));
        int32_t sc = 0;
        v.pitchEnvDepth = get(SrcPitchEnv, CtlNone, DstPitch, sc) ? (sc >> 16) : 0;
        if (v.pitchEnvDepth == 0) v.pitch.attack = 1;
    }
    // LFO.
    {
        int32_t sc = 0;
        double f = 5.0;
        if (get(SrcNone, CtlNone, DstLfoFreq, sc)) f = 440.0 * std::pow(2.0, (double(sc) / 65536.0 - 6900.0) / 1200.0);
        f = std::clamp(f, 0.084, 9.99);
        v.lfoStep = 256.0 * f / double(fs_);
        v.lfoDelay = samples(DstLfoDelay);
        v.lfoPitch = get(SrcLfo, CtlNone, DstPitch, sc) ? (sc >> 16) : 0;
        v.lfoVol = get(SrcLfo, CtlNone, DstAttenuation, sc) ? int((int64_t(sc) * 10) >> 16) : 0;
        v.modPitch = get(SrcLfo, CtlModWheel, DstPitch, sc) ? (sc >> 16) : 0;
        v.modVol = get(SrcLfo, CtlModWheel, DstAttenuation, sc) ? int((int64_t(sc) * 10) >> 16) : 0;
    }
    {
        int32_t sc = 0;
        // trunc((scale >> 12) / 125): ±64 for ±500 (0.1 % units)
        v.artPan = get(SrcNone, CtlNone, DstPan, sc) ? std::clamp((sc >> 12) / 125, -64, 64) : 0;
    }
    v.baseGain = V_[vel] + int((int64_t(ws.attenuation) * 10) >> 16);
    const int fine = (c.fineTune - 8192) * 100 / 8192;
    const int coarse = (c.coarseTune - 64) * 100;
    v.baseCents = (key - int(ws.unityNote)) * 100 + ws.fineTune + fine + coarse;
    v.pos = 0;
    v.u = 0;
    float l, r;
    double step;
    int gain;
    computeTargets(v, 0, l, r, step, gain);
    v.ampL = l;
    v.ampR = r;
    v.lastGain = gain;
}

void DlsSynth::releaseVoice(Voice& v) {
    if (v.vol.released) return;
    const int lv = v.vol.level(v.u, true);
    const int volLevel = (v.u < v.vol.attack) ? A_[std::min(lv, 1000) / 5] : lv;
    v.vol.release_(v.u, volLevel, 0, true);
    v.pitch.release_(v.u, v.pitch.level(v.u, false), 0, false);
}

void DlsSynth::quickStop(Voice& v) {
    if (!v.used) return;
    const int maxLen = std::max(1, fs_ / 70);
    int volLevel;
    if (v.vol.released) {
        volLevel = v.vol.level(v.u, true);
    } else {
        const int lv = v.vol.level(v.u, true);
        volLevel = (v.u < v.vol.attack) ? A_[std::min(lv, 1000) / 5] : lv;
    }
    v.vol.released = false;
    v.vol.release_(v.u, volLevel, maxLen, true);
    if (!v.pitch.released) v.pitch.release_(v.u, v.pitch.level(v.u, false), maxLen, false);
    v.quick = true;
}

void DlsSynth::noteOff(int pch, int key) {
    if (pch < 0 || pch >= kChannels) return;
    // Every voice of the oldest held note-on of that channel and key.
    uint64_t oldest = 0;
    for (const Voice& v : voices_)
        if (v.used && v.pch == pch && v.key == key && !v.offReceived && !v.vol.released && (oldest == 0 || v.note < oldest))
            oldest = v.note;
    if (oldest == 0) return;
    const bool hold = ch_[pch].sustain;
    for (Voice& v : voices_) {
        if (!v.used || v.note != oldest) continue;
        v.offReceived = true;
        if (hold) v.sustained = true;
        else releaseVoice(v);
    }
}

void DlsSynth::allSoundOff() {
    for (Voice& v : voices_)
        if (v.used) quickStop(v);
}

void DlsSynth::programChange(int pch, int program) {
    if (pch < 0 || pch >= kChannels) return;
    Channel& c = ch_[pch];
    c.program = program & 0x7F;
    c.patch = uint32_t(c.program) | (uint32_t(c.bankLsb & 0x7F) << 8) | (uint32_t(c.bankMsb & 0x7F) << 16);
}

void DlsSynth::pitchBend(int pch, int value) {
    if (pch < 0 || pch >= kChannels) return;
    ch_[pch].bend = std::clamp(value, 0, 16383);
}

void DlsSynth::controller(int pch, int cc, int value) {
    if (pch < 0 || pch >= kChannels) return;
    Channel& c = ch_[pch];
    value &= 0x7F;
    switch (cc) {
        case 0: c.bankMsb = value; break;
        case 32: c.bankLsb = value; break;
        case 1: c.cc1 = value; break;
        case 7: c.cc7 = value; break;
        case 10: c.cc10 = value; break;
        case 11: c.cc11 = value; break;
        case 64: {
            const bool on = value != 0;
            if (c.sustain && !on)
                for (Voice& v : voices_)
                    if (v.used && v.pch == pch && v.sustained) {
                        v.sustained = false;
                        releaseVoice(v);
                    }
            c.sustain = on;
            break;
        }
        case 101: c.rpnMsb = value; break;
        case 100: c.rpnLsb = value; break;
        case 6:
            c.dataMsb = value;
            if (c.rpnMsb == 0 && c.rpnLsb == 0) c.bendRange = value * 100;
            else if (c.rpnMsb == 0 && c.rpnLsb == 1) c.fineTune = (value << 7) | c.dataLsb;
            else if (c.rpnMsb == 0 && c.rpnLsb == 2) c.coarseTune = value;
            break;
        case 38:
            c.dataLsb = value;
            if (c.rpnMsb == 0 && c.rpnLsb == 1) c.fineTune = (c.dataMsb << 7) | value;
            break;
        case 120:
        case 126:
        case 127:
            for (Voice& v : voices_)
                if (v.used && v.pch == pch) quickStop(v);
            break;
        case 121:
            c.cc11 = 127;
            c.bend = 8192;
            c.cc1 = 0;
            if (c.sustain) controller(pch, 64, 0);
            if (value != 0) {
                c.cc7 = 100;
                c.cc10 = 64;
            }
            break;
        case 123:
            for (Voice& v : voices_)
                if (v.used && v.pch == pch && !v.offReceived) {
                    v.offReceived = true;
                    if (c.sustain) v.sustained = true;
                    else releaseVoice(v);
                }
            break;
        default: break;
    }
}

// ---- rendering

void DlsSynth::computeTargets(Voice& v, int64_t u, float& l, float& r, double& step, int& gain) const {
    const Channel& c = ch_[v.pch];
    int lv = v.vol.level(u, true);
    if (!v.vol.released && u < v.vol.attack) lv = A_[std::min(lv, 1000) / 5];
    const int pl = v.pitch.level(u, false);
    int lfo = 0;
    if (u >= v.lfoDelay) lfo = L_[int64_t(double(u - v.lfoDelay) * v.lfoStep) & 255];
    const int pitchLfo = (c.cc1 * v.modPitch / 127 + v.lfoPitch) * lfo / 100;
    const int volLfo = (c.cc1 * v.modVol / 127 + v.lfoVol) * lfo / 100;
    int cents = v.baseCents + (((c.bend - 8192) * c.bendRange) >> 13) + pitchLfo + pl * v.pitchEnvDepth / 1000;
    cents = std::clamp(cents, -4800, 4800);
    step = std::pow(2.0, double(cents) / 1200.0) * v.rateRatio;
    gain = v.baseGain + (lv * 96 / 10 - 9600) + volLfo + V_[c.cc11] + V_[c.cc7] + 600;
    const int pan = std::clamp(c.cc10 + v.artPan, 0, 127);
    l = ampOf(gain + P_[127 - pan]);
    r = ampOf(gain + P_[pan]);
    v.lastGain = gain;
}

void DlsSynth::renderVoice(Voice& v, int32_t* mixL, int32_t* mixR, int n) {
    float l1, r1;
    double step;
    int gain;
    computeTargets(v, v.u + n, l1, r1, step, gain);
    const float dl = (l1 - v.ampL) / float(n), dr = (r1 - v.ampR) / float(n);
    float al = v.ampL, ar = v.ampR;
    const uint64_t inc = uint64_t(step * 4294967296.0);
    const uint64_t loopLen = uint64_t(v.loopEnd - v.loopStart) << 32;
    const uint64_t loopEndFx = uint64_t(v.loopEnd) << 32;
    uint64_t pos = v.pos;
    const int16_t* d = v.data;
    bool ended = false;
    for (int i = 0; i < n; ++i) {
        if (v.looped) {
            while (pos >= loopEndFx) pos -= loopLen;
        } else if ((pos >> 32) >= v.len) {
            ended = true;
            break;
        }
        const uint32_t idx = uint32_t(pos >> 32);
        const float s0 = float(d[idx]);
        float s1;
        if (v.looped) s1 = float(idx + 1 >= v.loopEnd ? d[v.loopStart] : d[idx + 1]);
        else s1 = idx + 1 < v.len ? float(d[idx + 1]) : 0.0f;
        const float frac = float(uint32_t(pos)) * (1.0f / 4294967296.0f);
        const float s = s0 + (s1 - s0) * frac;
        al += dl;
        ar += dr;
        // Summed at half scale with 16-bit saturation: every add clamps.
        mixL[i] = std::clamp<int32_t>(mixL[i] + int32_t(std::lrint(s * al * 0.5f)), -32768, 32767);
        mixR[i] = std::clamp<int32_t>(mixR[i] + int32_t(std::lrint(s * ar * 0.5f)), -32768, 32767);
        pos += inc;
    }
    v.pos = pos;
    v.u += n;
    v.ampL = l1;
    v.ampR = r1;
    if (ended) {
        v.used = false;
        return;
    }
    if (v.vol.released) {
        // The voice ends at the first block after its volume falls below −80 dB.
        const int lv = v.vol.level(v.u, true);
        if (lv * 96 / 10 - 9600 < -8000) v.used = false;
    }
}

void DlsSynth::maintainReserve() {
    int live = 0;
    for (const Voice& v : voices_)
        if (v.used && !v.quick) ++live;
    while (live > kVoices) {
        Voice* best = nullptr;
        for (Voice& v : voices_) {
            if (!v.used || v.quick) continue;
            if (!best) {
                best = &v;
                continue;
            }
            if (v.priority != best->priority) {
                if (v.priority < best->priority) best = &v;
                continue;
            }
            const bool rel = v.vol.released, brel = best->vol.released;
            if (rel != brel) {
                if (rel) best = &v;
                continue;
            }
            if (rel ? v.lastGain < best->lastGain : v.age < best->age) best = &v;
        }
        if (!best) break;
        quickStop(*best);
        --live;
    }
}

int DlsSynth::liveVoices() const {
    int n = 0;
    for (const Voice& v : voices_)
        if (v.used) ++n;
    return n;
}

void DlsSynth::render(int16_t* out, int frames) {
    constexpr int kBlock = 32;
    if (int(mixL_.size()) < kBlock) {
        mixL_.resize(kBlock);
        mixR_.resize(kBlock);
    }
    int done = 0;
    while (done < frames) {
        const int n = std::min(kBlock, frames - done);
        maintainReserve();
        std::fill(mixL_.begin(), mixL_.begin() + n, 0);
        std::fill(mixR_.begin(), mixR_.begin() + n, 0);
        if (active_)
            for (Voice& v : voices_)
                if (v.used) renderVoice(v, mixL_.data(), mixR_.data(), n);
        int16_t* o = out + size_t(done) * 2;
        for (int i = 0; i < n; ++i) {
            o[2 * i] = int16_t(mixL_[size_t(i)]);
            o[2 * i + 1] = int16_t(mixR_[size_t(i)]);
        }
        if (reverbOn_) reverb(o, n);
        for (int i = 0; i < 2 * n; ++i) o[i] = sat16(int32_t(o[i]) * 2);
        done += n;
    }
}

// ---- reverb (directmusic.md, "Mix and reverb")

void DlsSynth::setupReverb() {
    const double fs = double(fs_);
    const double timeMs = 1000.0, hfRatio = 0.001, inGainDb = 0.0, mixDb = -10.0;
    auto rnd = [](double x) { return int(std::floor(x + 0.5)); };
    const int d0 = rnd(0.045 * fs);
    const int d2 = rnd(d0 * std::pow(2.0, 0.25));
    const int d1 = rnd(d2 * std::pow(2.0, 0.25));
    const int d3 = rnd(d1 * std::pow(2.0, 0.25));
    rvC_[0] = rnd(0.230933 * (d0 + d1) / 2.0);
    rvC_[1] = rnd(0.230933 * (d2 + d3) / 2.0);
    rvN_[0] = d0 - rvC_[0];
    rvN_[1] = d1 - rvC_[0];
    rvN_[2] = d2 - rvC_[1];
    rvN_[3] = d3 - rvC_[1];
    const double k = -3000.0 / (timeMs * fs);
    double G[4], g[2], b0[4], b1[4];
    for (int i = 0; i < 4; ++i) G[i] = std::pow(10.0, k * rvN_[i]);
    for (int i = 0; i < 2; ++i) g[i] = std::pow(10.0, k * rvC_[i]);
    const double hf = std::clamp(hfRatio, 0.001, 0.999);
    const double f = 1.0 / hf - 1.0;
    for (int i = 0; i < 4; ++i) {
        const int c = rvC_[i / 2];
        const double h = std::pow(10.0, k * (c + rvN_[i]) * f);
        const double e = (1.0 - h) / 2.0;
        b0[i] = (1.0 - e) * G[i];
        b1[i] = e * G[i];
    }
    double norm = 0;
    for (int i = 0; i < 4; ++i) {
        const double t = G[i] * g[i / 2];
        const double s = 1 + t + t * t + t * t * t + t * t * t * t;
        norm += s * s;
    }
    norm = std::sqrt(norm);
    const double I = std::pow(10.0, inGainDb / 20.0);
    const double M = std::pow(10.0, mixDb / 10.0);
    rvDry_ = q15(std::sqrt(1.0 - M) * I);
    rvWet_ = q15(std::sqrt(M) * I * 4.0 / norm);
    rvA_ = q15(0.618034);
    for (int i = 0; i < 2; ++i) rvG_[i] = q15(g[i]);
    for (int i = 0; i < 4; ++i) {
        rvB0_[i] = q15(b0[i]);
        rvB1_[i] = q15(b1[i]);
        line_[i].assign(size_t(std::max(1, rvN_[i])), 0);
        linePos_[i] = 0;
    }
    for (int i = 0; i < 2; ++i) {
        pair_[i].assign(size_t(std::max(1, rvC_[i])) * 2, 0);
        pairPos_[i] = 0;
    }
}

DlsSynth::ReverbInfo DlsSynth::reverbInfo() const {
    ReverbInfo r;
    r.dry = rvDry_;
    r.wet = rvWet_;
    r.a = rvA_;
    r.g0 = rvG_[0];
    r.g1 = rvG_[1];
    for (int i = 0; i < 4; ++i) {
        r.b0[i] = rvB0_[i];
        r.b1[i] = rvB1_[i];
        r.n[i] = rvN_[i];
    }
    r.c0 = rvC_[0];
    r.c1 = rvC_[1];
    return r;
}

void DlsSynth::reverb(int16_t* buf, int frames) {
    const int64_t a = rvA_;
    for (int f = 0; f < frames; ++f) {
        const int64_t L = buf[2 * f], R = buf[2 * f + 1];
        const int64_t iL = (L * rvWet_) >> 15, iR = (R * rvWet_) >> 15;
        int64_t x[4];
        for (int i = 0; i < 4; ++i) x[i] = line_[i][linePos_[i]];  // written n_i frames ago
        int64_t y[4];
        for (int p = 0; p < 2; ++p) {
            std::vector<int32_t>& pb = pair_[p];
            const size_t at = pairPos_[p] * 2;
            const int64_t p0 = pb[at], p1 = pb[at + 1];  // written c frames ago
            const int64_t g = rvG_[p];
            const int64_t x0 = x[2 * p], x1 = x[2 * p + 1];
            const int64_t y0 = (g * p0 + a * x1) >> 15;
            const int64_t y1 = (g * p1 - a * x0) >> 15;
            pb[at] = int32_t(((a * y1) >> 15) + x0);
            pb[at + 1] = int32_t(x1 - ((a * y0) >> 15));
            pairPos_[p] = (pairPos_[p] + 1) % (pb.size() / 2);
            y[2 * p] = y0;
            y[2 * p + 1] = y1;
        }
        buf[2 * f] = sat16(int32_t(std::clamp<int64_t>(((L * rvDry_) >> 15) + y[0] + y[2], -32768, 32767)));
        buf[2 * f + 1] = sat16(int32_t(std::clamp<int64_t>(((R * rvDry_) >> 15) + y[1] + y[3], -32768, 32767)));
        int64_t fb[4];
        for (int i = 0; i < 4; ++i) fb[i] = (y[i] * rvB0_[i] + int64_t(yPrev_[i]) * rvB1_[i]) >> 15;
        const int64_t in[4] = {iL + fb[0], iR + fb[1], fb[2] - iR, iL + fb[3]};
        for (int i = 0; i < 4; ++i) {
            line_[i][linePos_[i]] = int32_t(in[i]);
            linePos_[i] = (linePos_[i] + 1) % line_[i].size();
            yPrev_[i] = int32_t(y[i]);
        }
    }
}

}  // namespace music
}  // namespace cc
