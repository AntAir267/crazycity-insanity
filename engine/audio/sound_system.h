// The sound layer every MM2 sound goes through (spec/audio/sound-system.md): sound files found by
// MM2's path rule and decoded once, samples with instances, the decibel-linear volume, pitch and
// pan of the mixer, the 32-sample voice limit with priority stealing, echo copies and the pause
// rule. Voices are played by an AudioBackend (Web Audio in the browser, a recorder in tests).
//
// "Is playing" follows the play position the mixer keeps itself (the file's frames at the
// playback rate, advanced by the frame time), so the rules behave the same with any backend.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace cc {
class Vfs;

namespace audio {

// The output (one voice per playing instance and per sounding echo copy).
class AudioBackend {
public:
    virtual ~AudioBackend() = default;
    // Interleaved signed 16-bit frames at the file's own rate. Returns a buffer id.
    virtual int createBuffer(const int16_t* pcm, uint32_t frames, int channels, int rate) = 0;
    virtual void destroyBuffer(int id) = 0;
    // playbackRate is relative to the buffer's own rate; gains are linear amplitudes per channel.
    virtual int startVoice(int buffer, bool loop, double offsetSeconds, double playbackRate, float gainL,
                           float gainR) = 0;
    virtual void setVoice(int voice, double playbackRate, float gainL, float gainR) = 0;
    virtual void stopVoice(int voice) = 0;
};

// The audio block of the player options (sound-system.md, "Options in the player file").
struct SoundOptions {
    bool effects = true;     // 0x001
    bool music = false;      // 0x004
    bool stereo = true;      // 0x040
    bool commentary = true;  // 0x400
    bool ambient = true;     // 0x800
    float sfxVolume = 1.0f;  // s, 0 … 1
    float musicVolume = 1.0f;
    float balance = 0.0f;    // b, −1 … 1
    int channels = 32;       // stored, no effect
};

// sound-system.md "Finding a sound file": aud\<sub-path>\<name>[<extension>][.wav].
std::string soundPath(const std::string& name, const std::string& subPath = "aud22", const std::string& extension = ".22k");

// Echo copy settings (sound-system.md, "Echo"): delay T, gain G, initial echo pitch F.
struct EchoParams {
    float delay = 0.5f;
    float gain = 0.98f;
    bool hasPitch = false;
    float pitch = 1.0f;
};

class SoundSystem;

class Sample {
public:
    // A sample with `instances` instances (1 unless a page says otherwise). Volume starts at
    // `volume` (1 = full; the 3D cache makes its copies at 0).
    Sample(SoundSystem& sys, bool pitchControl, int instances = 1, float volume = 1.0f);
    ~Sample();
    Sample(const Sample&) = delete;
    Sample& operator=(const Sample&) = delete;

    // The file of instance i (or of every instance): an archive path (soundPath()).
    void setFile(int instance, const std::string& path);
    void setFileAll(const std::string& path);
    const std::string& file(int instance = 0) const;

    int instanceCount() const { return int(inst_.size()); }
    void setCurrent(int i) {
        if (i >= 0 && i < int(inst_.size())) current_ = i;
    }
    int current() const { return current_; }
    bool pitchControl() const { return pitch_; }
    // The current instance's file is decoded and can play.
    bool loaded() const;
    // The current instance's file is still being fetched (lazy archives).
    bool pending() const;
    // The current instance's file is missing or failed to decode (every action does nothing).
    bool missing() const;

    // On the current instance.
    void setVolume(float v);
    float volume() const { return inst_[size_t(current_)].volume; }
    void setPitch(float f);
    float pitch() const { return inst_[size_t(current_)].pitch; }
    void setPan(float p);
    void play() { start(false); }
    void playLooped() { start(true); }
    void stop();
    bool playing() const { return inst_[size_t(current_)].playing; }
    bool anyPlaying() const;
    void stopAll();
    // Seconds of the current instance's file (0 if not loaded).
    double duration() const;
    // The current instance's play position in seconds.
    double position() const;

    void setPriority(int p) { priority_ = p; }
    int priority() const { return priority_; }

    // Echo copies of every instance (sound-system.md, "Echo (tunnels)").
    void setEcho(bool on, const EchoParams& e = EchoParams());
    bool echoOn() const { return echo_; }
    // Carries out the due echo repeats of every instance (once per owner update).
    void echoTick(float dt);
    // Test access: the echo copy of the current instance is sounding; its pending repeats.
    bool echoPlaying() const;
    size_t echoPending() const;

private:
    friend class SoundSystem;
    struct Voice {
        int id = -1;
        double rate = -1;
        float gl = -1, gr = -1;
    };
    struct Echo {
        bool made = false;  // the echo voice exists (made at the first switch-on)
        bool playing = false, loop = false, restart = false;
        double pos = 0;
        float level = 0;  // g × v × G (mapped without the 0…1 clamp)
        int rateHz = 0;
        float pan = 0;
        Voice voice;
        struct Rep {
            float age = 0;
            bool loop = false;
            double pos = 0;
            float value = 0;
            int rateHz = 0;
        };
        std::deque<Rep> plays, stops, volumes, pitches;
    };
    struct Inst {
        int buffer = -1;
        float volume = 1, pitch = 1, pan = 0;
        bool playing = false, loop = false, restart = false;
        double pos = 0;  // frames
        Voice voice;
        Echo echo;
    };
    void start(bool loop);
    void stopInst(Inst& in, bool repeat);
    int rateHz(const Inst& in) const;
    void queue(std::deque<Echo::Rep>& q, const Echo::Rep& r);

    SoundSystem& sys_;
    bool pitch_ = false;
    int priority_ = 0;
    int current_ = 0;
    std::vector<Inst> inst_;
    bool echo_ = false;
    bool echoMade_ = false;
    float echoT_ = 0.5f;
    EchoParams echoParams_;
    bool listed_ = false;  // in the playing list (voice limits)
};

class SoundSystem {
public:
    SoundSystem();
    ~SoundSystem();

    void setVfs(const Vfs* vfs) { vfs_ = vfs; }
    const Vfs* vfs() const { return vfs_; }
    void setBackend(AudioBackend* b);
    AudioBackend* backend() const { return backend_; }

    void setOptions(const SoundOptions& o) { opt_ = o; }
    const SoundOptions& options() const { return opt_; }
    // g of "Volume": log(200 s) / log(200), 0 when s = 0 or the effects are off.
    float effectsGain() const;

    // Once per rendered frame: beginFrame(dt) before the game's frame advances the play positions
    // by the time since the last frame (so owners see a sound end in the first frame after it
    // ended); endFrame() after every owner's commands runs the pause rule and sends the changes
    // to the backend.
    void beginFrame(float dt);
    void endFrame();
    void update(float dt) {
        beginFrame(dt);
        endFrame();
    }
    // Pause: the first two paused frames stop every playing sample and echo copy.
    void setPaused(bool p) { paused_ = p; }
    bool paused() const { return paused_; }
    // The backend's output (re)started (the browser's first gesture): voices that should be
    // playing are started again where they are.
    void outputRestarted();
    // Every sample stops (owners going away).
    void stopEverything();
    // The files are gone (archives remounted): every decoded file is dropped.
    void clearFiles();

    // The decoded file of an archive path: its index (loaded now, or fetched and decoded later).
    int file(const std::string& path);
    // Archive files still being fetched.
    int pendingFiles() const { return int(pending_.size()); }

    struct Stats {
        int samples = 0, playing = 0, voices = 0, files = 0, missing = 0, pending = 0, stolen = 0, refused = 0;
    };
    Stats stats() const;
    static constexpr int kMaxPlaying = 32;

private:
    friend class Sample;
    struct File {
        std::string path;
        enum State { Missing, Pending, Ready, Failed } state = Missing;
        int channels = 1, rate = 22050;
        uint32_t frames = 0;
        int backendId = -1;
    };
    void decode(File& f);
    bool admit(Sample* s);  // voice limits for a sample about to start
    void listPlaying(Sample* s);
    void unlist(Sample* s);
    void flush(Sample& s);
    void flushVoice(Sample::Voice& v, bool playing, bool restart, int buffer, bool loop, double pos, int rateHz,
                    float gl, float gr);
    float amplitude(float v, bool clamp01) const;
    void panGains(float pan, float& l, float& r) const;
    void registerSample(Sample* s) { samples_.push_back(s); }
    void unregisterSample(Sample* s);

    const Vfs* vfs_ = nullptr;
    AudioBackend* backend_ = nullptr;
    SoundOptions opt_;
    std::vector<File> files_;
    std::unordered_map<std::string, int> fileIndex_;
    std::vector<int> pending_;
    std::vector<Sample*> samples_;
    std::vector<Sample*> playing_;  // in the order the samples started
    bool paused_ = false;
    int pausedFrames_ = 0;
    int stolen_ = 0, refused_ = 0;
};

}  // namespace audio
}  // namespace cc
