// Ambient traffic sounds (spec/audio/traffic-sounds.md): engine loops through speed tables, horn
// patterns (honk on avoiding, the stuck horn), driver voices with their bands and the impact
// shout, and the traffic cars' impact sounds. Every loaded file has one copy per 3D slot; a car
// holding slot j uses copy j.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ai/pedestrians.h"
#include "ai/traffic.h"
#include "audio/audio_world.h"
#include "audio/text_reader.h"

namespace cc {
namespace audio {

struct TrafficEngineData {
    bool found = false;
    std::string sample;
    float volume = 0;
    struct Row {
        float minS = 0, maxS = 0, minP = 0, maxP = 0, k = 0;
    };
    std::vector<Row> rows;  // the last is the deceleration row
    // "Engine pitch": P for speed s, previous speed sp, decay; `P` unchanged when no row fits.
    float pitch(float s, float sp, bool decay, float P) const;
};
TrafficEngineData parseTrafficEngine(LineReader& r);

struct TrafficHornData {
    bool found = false;
    std::string sample;
    float volume = 0, pitch = 1, minForce = 0;
    std::vector<std::vector<std::pair<float, float>>> patterns;  // play, pause per segment
};
TrafficHornData parseTrafficHorn(LineReader& r);

struct VoiceData {
    bool found = false;
    struct Band {
        float minS = 0, maxS = 0, A = 0, B = 0;
        std::vector<std::pair<std::string, float>> samples;
    };
    std::vector<Band> bands;
    bool hasImpact = false;
    float impactForce = 0;
    struct ImpactSample {
        std::string name;
        float volume = 0, delay = 0;
    };
    std::vector<ImpactSample> impact;
};
VoiceData parseVoiceFile(LineReader& r);

// The number of a count file (line 2, first field), or -1 if the file is missing.
float readCountFile(const Vfs& vfs, const std::string& path);

// The run-time state of one horn file copy ("Horn patterns").
struct HornCopy {
    enum State { Idle, Playing, Pausing };
    struct Pattern {
        int seg = 0;
        float t = 0;
        State state = Idle;
    };
    int current = 0;
    std::vector<Pattern> pats;
};

class TrafficAudio : public ai::TrafficSoundHooks {
public:
    explicit TrafficAudio(AudioWorld& w);
    ~TrafficAudio() override;

    // Once per game at set-up, before the streets: the city suffix and the three count files.
    static void readCountFiles(AudioWorld& w);
    // The pool was created: every car's files in pool order.
    void setup(ai::TrafficSystem& ts);
    void clear();

    // ai::TrafficSoundHooks.
    void carUpdated(const ai::TrafficCar& c) override;
    void avoidStart(const ai::TrafficCar& c) override;
    void placed(const ai::TrafficCar& c) override;
    // An impact applied to a physical traffic car.
    void impact(int car, const vec3& J, int otherSound);
    // Pedestrians (traffic-sounds.md, "Pedestrian voices"): one source and voice file per
    // pedestrian of the pool; after the pedestrians' update of each AI frame the release rule and
    // this frame's scream events.
    void setupPeds(const ai::PedestrianSystem& ps);
    void clearPeds();
    void pedFrame(const ai::PedestrianSystem& ps);
    int screamingPeds() const;
    int pedVoiceFiles() const;
    // The very start of the AI update: voice band timers with the first player's car speed.
    void aiFrameStart(float F, float playerSpeed);

    // One update of the current horn pattern ("Horn patterns"); `s` may be null.
    static void stepHornPattern(HornCopy::Pattern& p, const std::vector<std::pair<float, float>>& segs, float F, Sample* s);

    // Shared over the whole program (traffic-sounds.md, "Reaction state").
    static float gateTime();
    static int driverVoiceNumber();

    // Event counts (tests and the page's readout).
    struct Counts {
        int honks = 0, stuckHorns = 0, shouts = 0, impactSounds = 0, screams = 0, screamEvents = 0;
    };
    const Counts& counts() const { return counts_; }
    // Test access.
    int soundingCars() const;
    const HornCopy* hornCopy(int car) const;

private:
    struct EngineFile {
        TrafficEngineData d;
        SlotSample smp;
        float P[Manager3D::kSlots] = {};
    };
    struct HornFile {
        TrafficHornData d;
        SlotSample smp;
        HornCopy copy[Manager3D::kSlots];
    };
    struct VoiceFile {
        VoiceData d;
        bool ped = false;  // a pedestrian voice file
        int index = 0;     // in its list
        struct BandState {
            float tin = 0, tout = 0, wait = 0;
            int variant = 0;
            bool armed = false;
        };
        struct Copy {
            std::vector<BandState> bands;
            std::vector<std::vector<std::unique_ptr<Sample>>> bandSamples;
            std::vector<std::unique_ptr<Sample>> impactSamples;
            int chosen = 0;
            float timer = 0;
            bool armed = false;
            float a = 0, p = 0, D2 = 1e9f;
        };
        Copy copy[Manager3D::kSlots];
    };
    class CarSound;
    friend class CarSound;
    class PedSound;
    friend class PedSound;
    bool pedHolds(int file, int slot) const;
    int loadVoice(std::vector<std::unique_ptr<VoiceFile>>& list, std::map<std::string, int>& cache, const std::string& name);
    void voiceFrame(std::vector<std::unique_ptr<VoiceFile>>& list, float F, float playerSpeed);

    int engineFile(const std::string& type);
    int hornFile(const std::string& type);
    int voiceFile(const std::string& type);
    void hornUpdate(HornFile& h, int k, float F);
    void hornStart(HornFile& h, int k, int pattern);
    void hornStopPattern(HornFile& h, int k, int pattern);
    void voicePlay(VoiceFile& v, int k, Sample* s, float fileVolume, bool impact, int index);
    void avoidanceReaction(VoiceFile& v, int k);

    AudioWorld& w_;
    ai::TrafficSystem* ts_ = nullptr;
    std::vector<std::unique_ptr<CarSound>> cars_;
    std::vector<std::unique_ptr<EngineFile>> engines_;
    std::vector<std::unique_ptr<HornFile>> horns_;
    std::vector<std::unique_ptr<VoiceFile>> voices_;
    std::map<std::string, int> nameCache_;  // file name → index (−1: missing)
    ImpactTable* impacts_ = nullptr;
    std::vector<std::unique_ptr<PedSound>> peds_;
    std::vector<std::unique_ptr<VoiceFile>> pedVoices_;
    std::map<std::string, int> pedCache_;
    Counts counts_;
};

}  // namespace audio
}  // namespace cc
