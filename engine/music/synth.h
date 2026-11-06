// MM2's music synthesiser (spec/formats/directmusic.md, "Synthesiser"): a DLS level 1 software
// synthesiser with 2 channel groups (PChannels 0-31), 41 voices plus a reserve of 10, linear
// interpolation, the DLS envelopes and LFO, the small-room reverb, at the sound system's mixing
// rate (22,050 Hz), stereo 16-bit.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "music/dm_files.h"

namespace cc {
namespace music {

class DlsSynth {
public:
    static constexpr int kChannels = 32;
    static constexpr int kVoices = 41;
    static constexpr int kReserve = 10;

    explicit DlsSynth(int sampleRate = 22050);

    int sampleRate() const { return fs_; }
    // Channel state back to the start values and every voice cut.
    void reset();
    // Channel state back to the start values (a new music player); sounding voices go on.
    void resetChannels();

    // Instruments. `gm` marks one from the General MIDI set (found after collection instruments
    // of the same patch). Downloading the same instrument twice keeps its first place.
    void download(const DlsInstrument* inst, const DlsCollection* owner, bool gm);
    // Forgets the downloaded instruments (voices playing them go on: their data must stay alive).
    void clearDownloads();
    int downloadCount() const { return int(downloads_.size()); }
    bool hasPatch(uint32_t patch) const;

    // Events on PChannel 0-31.
    void noteOn(int pch, int key, int velocity);
    void noteOff(int pch, int key);
    void controller(int pch, int cc, int value);
    void programChange(int pch, int program);
    void pitchBend(int pch, int value);  // 0..16383, 8192 centre
    void channelPressure(int pch, int value) { (void)pch; (void)value; }  // no effect
    void setChannelPriority(int pch, uint32_t priority);
    void allSoundOff();

    // The CD player's deactivation: silent, voices dropped, note-ons ignored.
    void setActive(bool on);
    bool active() const { return active_; }
    void setReverb(bool on) { reverbOn_ = on; }

    // Renders `frames` stereo frames (interleaved, 16-bit, after the reverb and the final doubling).
    void render(int16_t* out, int frames);

    int liveVoices() const;
    // Testing: every note-on with the instrument it found (null: none).
    std::function<void(int pch, int key, int vel, const DlsInstrument* inst)> onNote;
    uint64_t notesStarted() const { return notesStarted_; }
    uint64_t notesDropped() const { return notesDropped_; }  // no instrument or no voice
    // Notes that found no instrument, by (PChannel << 32 | patch looked for).
    const std::map<uint64_t, int>& missing() const { return missing_; }

    // Reverb coefficients (Q15) for checks: dry, wet, a, g0, g1, b0[4], b1[4], delays.
    struct ReverbInfo {
        int dry, wet, a, g0, g1, b0[4], b1[4], n[4], c0, c1;
    };
    ReverbInfo reverbInfo() const;

private:
    struct Env {
        int attack = 1, decay = 0, sustain = 1000, release = 1;  // samples; sustain 0..1000
        bool released = false;
        int64_t relU = 0;
        int L0 = 0;
        int relLen = 1;
        int level(int64_t u, bool volume) const;
        void release_(int64_t u, int levelNow, int maxLen, bool volume);
    };
    struct Channel {
        int program = 0, bankMsb = 0, bankLsb = 0;
        uint32_t patch = 0;
        int cc7 = 100, cc10 = 64, cc11 = 127, cc1 = 0;
        int bend = 8192, bendRange = 200;  // cents
        bool sustain = false;
        int rpnMsb = 127, rpnLsb = 127;
        int fineTune = 8192, coarseTune = 64;  // RPN 1 (14-bit), RPN 2 (semitones + 64)
        int dataMsb = 0, dataLsb = 0;
        uint32_t priority = 0;
    };
    struct Voice {
        bool used = false;
        int pch = 0, key = 0, vel = 0;
        uint64_t note = 0;   // note-on serial
        uint64_t age = 0;    // start order
        uint32_t priority = 0;
        const DlsRegion* region = nullptr;
        const int16_t* data = nullptr;
        uint32_t len = 0;
        bool looped = false;
        uint32_t loopStart = 0, loopEnd = 0;
        double rateRatio = 1;
        uint64_t pos = 0;  // 32.32
        Env vol, pitch;
        int baseGain = 0;   // V[velocity] + attenuation (hundredths of a dB)
        int baseCents = 0;  // key, unity note, fine tune, channel tuning
        int artPan = 0;
        int pitchEnvDepth = 0;  // cents
        int lfoPitch = 0, lfoVol = 0, modPitch = 0, modVol = 0;  // cents / hundredths of a dB
        int64_t lfoDelay = 0;
        double lfoStep = 0;  // table steps per sample
        int64_t u = 0;       // samples since note-on
        bool offReceived = false, sustained = false, quick = false;
        float ampL = 0, ampR = 0;
        int lastGain = -9600;
    };

    void startVoice(Voice& v, int pch, int key, int vel, const DlsInstrument& inst, const DlsRegion& rg, uint64_t note);
    Voice* allocate(uint32_t priority);
    void quickStop(Voice& v);
    void releaseVoice(Voice& v);
    void maintainReserve();
    void computeTargets(Voice& v, int64_t u, float& l, float& r, double& step, int& gain) const;
    void renderVoice(Voice& v, int32_t* mixL, int32_t* mixR, int n);
    void reverb(int16_t* buf, int frames);
    void setupReverb();
    const DlsInstrument* findInstrument(uint32_t patch, int key, int vel) const;

    int fs_;
    bool active_ = true;
    bool reverbOn_ = true;
    Channel ch_[kChannels];
    std::vector<Voice> voices_;
    struct Download {
        const DlsInstrument* inst;
        const DlsCollection* owner;
        uint32_t patch;
        bool gm;
    };
    std::vector<Download> downloads_;  // newest last
    uint64_t noteSerial_ = 0, ageSerial_ = 0;
    uint64_t notesStarted_ = 0, notesDropped_ = 0;
    std::map<uint64_t, int> missing_;

    // Tables (directmusic.md "Gain", "Envelopes").
    int V_[128], P_[128], A_[201], L_[256];

    // Reverb state.
    int rvDry_ = 0, rvWet_ = 0, rvA_ = 0, rvG_[2] = {}, rvB0_[4] = {}, rvB1_[4] = {};
    int rvN_[4] = {}, rvC_[2] = {};
    std::vector<int32_t> line_[4];
    size_t linePos_[4] = {};
    std::vector<int32_t> pair_[2];  // interleaved (u0, u1) per frame
    size_t pairPos_[2] = {};
    int32_t yPrev_[4] = {};
    std::vector<int32_t> mixL_, mixR_;
};

}  // namespace music
}  // namespace cc
