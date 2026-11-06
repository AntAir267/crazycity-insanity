// Native checks of the sound layer against the numbers in spec/audio/*.md ("Checked against"
// sections and worked examples): WAV formats of the shipped files, mixer rules, the 3D manager,
// echo copies, the car, tyre, impact, traffic, world and speech files, and a short game.
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "audio/car_audio.h"
#include "audio/car_data.h"
#include "audio/game_audio.h"
#include "audio/sound3d.h"
#include "audio/sound_random.h"
#include "audio/sound_system.h"
#include "audio/speech.h"
#include "audio/traffic_audio.h"
#include "audio/wav.h"
#include "audio/world_audio.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "physics/car.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "vfs/vfs.h"

using namespace cc;
using namespace cc::audio;

namespace {

using Expect = std::function<void(bool, const std::string&)>;

std::string f9(float x) { return strf("%.9g", double(x)); }
bool near(double a, double b, double tol) { return std::fabs(a - b) <= tol; }

// Records what the mixer sends to the output.
struct RecBackend : AudioBackend {
    int buffers = 0, starts = 0, stops = 0, sets = 0, next = 1;
    std::map<int, std::pair<float, float>> live;
    int createBuffer(const int16_t*, uint32_t, int, int) override { return ++buffers; }
    void destroyBuffer(int) override {}
    int startVoice(int, bool, double, double, float gl, float gr) override {
        ++starts;
        live[next] = {gl, gr};
        return next++;
    }
    void setVoice(int v, double, float gl, float gr) override {
        ++sets;
        live[v] = {gl, gr};
    }
    void stopVoice(int v) override {
        ++stops;
        live.erase(v);
    }
};

struct TestSource : Source3D {
    int assigned = 0, released = 0;
    void onAssigned(int) override { ++assigned; }
    void onReleased() override { ++released; }
};

void testWavs(const Vfs& vfs, const Expect& expect) {
    std::map<std::string, int> kinds;
    int total = 0, bad = 0, pcm16 = 0;
    for (const std::string& p : vfs.listUnique("aud/")) {
        if (!iendsWith(p, ".wav")) continue;
        ++total;
        BytesPtr b = vfs.open(p);
        WavInfo w;
        std::string err;
        if (!b || !parseWav(b->data(), b->size(), w, &err)) {
            ++bad;
            std::printf("  refused %s: %s\n", p.c_str(), err.c_str());
            continue;
        }
        if (w.bits == 16) ++pcm16;
        ++kinds[strf("%s %d", w.channels == 1 ? "mono" : "stereo", w.rate)];
    }
    std::string k;
    for (const auto& e : kinds) k += strf("%s%s: %d", k.empty() ? "" : ", ", e.first.c_str(), e.second);
    std::printf("  %d WAV files: %s\n", total, k.c_str());
    expect(total == 2503 && bad == 0 && pcm16 == 2503, "2,503 shipped WAVs, all PCM 16-bit, none refused (sound-system.md)");
    expect(kinds["mono 11025"] == 2112 && kinds["mono 22050"] == 375 && kinds["mono 48000"] == 13 &&
               kinds["mono 44100"] == 1 && kinds["stereo 11025"] == 1 && kinds["stereo 22050"] == 1,
           "WAV formats: mono 11,025 Hz 2,112 / 22,050 Hz 375 / 48 kHz 13 / 44.1 kHz 1, stereo 11,025 and 22,050 one each");
    // The chunk rules.
    const uint8_t foreign[] = {'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E', 'L', 'I', 'S', 'T', 0, 0, 0, 0,
                               'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xAC, 0, 0, 2, 0, 16, 0};
    WavInfo w;
    expect(!parseWav(foreign, sizeof foreign, w, nullptr), "a foreign chunk before fmt and data makes the load fail");
}

void testPathsAndMixer(const Vfs& vfs, const Expect& expect) {
    expect(soundPath("engines\\RACECARIDLE") == "aud\\aud22\\engines\\RACECARIDLE.22k.wav" &&
               vfs.exists(soundPath("engines\\RACECARIDLE")),
           "engines\\RACECARIDLE is aud/aud22/engines/RACECARIDLE.22k.wav (sound-system.md)");
    expect(soundPath("AL1PRE01", "aud11\\AL1", ".11K") == "aud\\aud11\\AL1\\AL1PRE01.11K.wav" && soundPath("x.abc") == "aud\\aud22\\x.abc.wav",
           "speech sub-path and extension; a name with its own extension");
    expect(!vfs.exists(soundPath("horns\\FIRETRUCKSIREN")) && !vfs.exists(soundPath("surfaces\\skidflagstone")) &&
               !vfs.exists(soundPath("impacts\\caroncobble")) && !vfs.exists(soundPath("Amb3D\\NOTHING")),
           "the names the spec lists as missing are missing (no fallback)");

    SoundSystem sys;
    RecBackend rec;
    sys.setVfs(&vfs);
    sys.setBackend(&rec);
    SoundOptions o;
    o.sfxVolume = 0.5f;
    sys.setOptions(o);
    const float g05 = sys.effectsGain();
    o.sfxVolume = 0.1f;
    sys.setOptions(o);
    const float g01 = sys.effectsGain();
    o.sfxVolume = 1.0f;
    sys.setOptions(o);
    std::printf("  effects gain: s 0.5 -> %.4f, s 0.1 -> %.4f\n", double(g05), double(g01));
    expect(near(g05, 0.8692, 5e-5) && near(g01, 0.5654, 5e-5) && sys.effectsGain() == 1.0f, "effects gain log(200 s)/log(200)");

    // Voice limits: 32 samples; a 33rd of equal priority steals the first started; a lower one is refused.
    const std::string file = soundPath("engines\\VWIDLE");
    std::vector<std::unique_ptr<Sample>> v;
    for (int i = 0; i < 34; ++i) {
        v.push_back(std::make_unique<Sample>(sys, true));
        v.back()->setFile(0, file);
        v.back()->setPriority(i < 33 ? 5 : 3);
    }
    for (int i = 0; i < 32; ++i) v[size_t(i)]->playLooped();
    sys.update(1.0f / 30.0f);
    const int voices32 = int(rec.live.size());
    v[32]->playLooped();
    const bool stole = !v[0]->playing() && v[32]->playing();
    v[33]->playLooped();
    const bool refused = !v[33]->playing();
    sys.update(1.0f / 30.0f);
    std::printf("  voice limit: %d voices for 32 samples; 33rd steals the first: %s; lower priority refused: %s\n", voices32,
                stole ? "yes" : "no", refused ? "yes" : "no");
    expect(voices32 == 32 && stole && refused && sys.stats().playing == 32, "32-sample limit with priority stealing (sound-system.md)");
    // Volume in dB: v = 0.9 is -10 dB.
    v[1]->setVolume(0.9f);
    v[1]->setPan(0.0f);
    sys.update(1.0f / 30.0f);
    // The voice of v[1]: the gains of a -10 dB voice are 10^(-0.5).
    bool found = false;
    for (const auto& e : rec.live)
        if (near(e.second.first, 0.316228, 1e-4) && near(e.second.second, 0.316228, 1e-4)) found = true;
    expect(found, "volume 0.9 plays at -10 dB (amplitude 0.3162)");
    v.clear();
    sys.update(0.0f);

    // Echo: the play repeats on the echo voice T seconds later (counted in owner ticks).
    Sample e(sys, true);
    e.setFile(0, file);
    EchoParams ep;
    ep.delay = 0.5f;
    ep.gain = 0.96f;
    e.setEcho(true, ep);
    e.playLooped();
    int tick = 0;
    for (; tick < 40 && !e.echoPlaying(); ++tick) e.echoTick(1.0f / 30.0f);
    std::printf("  echo copy starts after %d ticks of 1/30 s\n", tick);
    expect(tick == 15, "echo play repeat after T = 0.5 s of ticks");
    // A loop already running when the echo goes on gets its echo T later (sound-system.md, step 1).
    Sample run(sys, true);
    run.setFile(0, file);
    run.playLooped();
    run.setEcho(true, ep);
    int tick2 = 0;
    for (; tick2 < 40 && !run.echoPlaying(); ++tick2) run.echoTick(1.0f / 30.0f);
    // A full queue (N = trunc(0.05 × 180) = 9) wraps: the tenth command is the only one left.
    Sample q(sys, true);
    q.setFile(0, file);
    EchoParams hp;
    hp.delay = 0.05f;
    hp.gain = 0.96f;
    q.setEcho(true, hp);
    for (int i = 0; i < 10; ++i) q.setVolume(0.5f + 0.01f * float(i));
    std::printf("  running loop echoes after %d ticks; a 10th volume command leaves %zu pending\n", tick2, q.echoPending());
    expect(tick2 == 15 && q.echoPending() == 1, "echo for a running loop, and a full echo queue wraps (sound-system.md)");
    // Pause: the first paused frames stop everything.
    sys.setPaused(true);
    sys.update(1.0f / 30.0f);
    expect(!e.playing() && !e.echoPlaying(), "pause stops every sample and echo copy");
    sys.setPaused(false);

    // Sound random: a fresh generator per draw; equal within one second.
    pinSoundClock(1792000000);
    const double r1 = soundUnit(), r2 = soundUnit();
    bool range = true;
    for (long long s = 1792000000; s < 1792000000 + 20000; ++s) {
        const double r = soundUnitForSeed(s);
        if (!(r >= 0.0 && r < 1.0)) range = false;
    }
    pinSoundClock(-1);
    expect(r1 == r2 && range, "sound random: same r within a second, 0 <= r < 1");
}

void test3d(const Expect& expect) {
    Manager3D m;
    m.setListener(vec3(0, 0, 0), vec3(1, 0, 0));
    TestSource s[7];
    const float dist[4] = {10, 20, 30, 40};
    for (int i = 0; i < 4; ++i) {
        s[i].attach(m);
        s[i].priority = 8;
        s[i].setDropoffs(0, 100);
        s[i].position = vec3(0, 0, dist[i]);
        s[i].requestSlot();
    }
    s[4].attach(m);
    s[4].priority = 8;
    s[4].setDropoffs(0, 100);
    s[4].position = vec3(0, 0, 25);
    s[4].requestSlot();
    const bool farthest = s[4].slot() == 3 && s[3].released == 1;
    s[5].attach(m);
    s[5].priority = 7;
    s[5].setDropoffs(0, 100);
    s[5].position = vec3(0, 0, 5);
    s[5].requestSlot();
    const bool lower = !s[5].holdsSlot();
    s[6].attach(m);
    s[6].twoD = true;
    s[6].priority = 9;
    s[6].requestSlot();
    const bool twoD = s[6].slot() == 2;
    expect(farthest && lower && twoD, "slot eviction: the farthest equal-priority holder, never a higher one; 2D always wins");
    // Attenuation, pan, doppler.
    TestSource car;
    car.attach(m);
    car.setDropoffs(0, 150);
    car.dopplerK = 0.0176315f;
    car.position = vec3(0, 0, 76.6666667f);
    m.measure(car);
    car.position = vec3(0, 0, 75);
    m.measure(car);
    m.compute(car, 1.0f / 30.0f);
    std::printf("  car at 75 m: A %.4f, doppler %.5f; ", double(car.A), double(car.doppler));
    TestSource side;
    side.attach(m);
    side.setDropoffs(0, 150);
    side.position = vec3(10, 0, 0);
    m.measure(side);
    m.compute(side, 1.0f / 30.0f);
    std::printf("pan right %.3f\n", double(side.pan));
    expect(near(car.A, 0.75, 1e-6) && near(car.doppler, 1.00098, 2e-5) && near(side.pan, 0.2, 1e-6),
           "attenuation 0.75 at half the maximum, doppler about 1.001 at 50 m/s, 3D pan 0.2 (sound-system.md)");
}

void testCarFiles(const Vfs& vfs, const Expect& expect) {
    const VehTypes t = loadVehTypes(vfs);
    expect(t.found && t.semiBus == std::vector<std::string>{"vpcentury", "vpbus", "vpddbus"} &&
               t.police == std::vector<std::string>{"vpcop", "vpsemi", "vpeagle"} && !t.alwaysNitro,
           "vehtypes.csv: semi/bus, police lists, nitro off");
    const CarData bug = loadCarData(vfs, "player", "vpbug");
    std::string eng;
    bool engOk = bug.engine.size() == 4;
    const float want[4][2] = {{0.8172f, 0.9732f}, {0.5880f, 0.7239f}, {0.5714f, 1.0893f}, {0.5500f, 0.6500f}};
    for (size_t i = 0; i < bug.engine.size() && i < 4; ++i) {
        const float V = bug.engine[i].volume(750.0f, false), P = bug.engine[i].pitch(750.0f);
        eng += strf(" %s %.4f/%.4f", bug.engine[i].wave.c_str(), double(V), double(P));
        if (!near(V, want[i][0], 6e-5) || !near(P, want[i][1], 6e-5)) engOk = false;
    }
    std::printf("  vpbug at 750 rpm:%s\n", eng.c_str());
    expect(engOk, "vpbug player engine samples at 750 rpm as in car-sounds.md");
    expect(loadCarData(vfs, "opponent", "default").engine.size() == 1 && loadCarData(vfs, "player", "vpcop").engine.size() == 4,
           "engine rows counted from the file, not the count field");
    // Every car of the list: its samples resolve, except vpsemi's horn.
    int refs = 0;
    std::set<std::string> missing;
    for (const CarInfo& c : buildCarList(vfs)) {
        if (!c.builtIn) continue;
        for (const char* folder : {"player", "opponent"}) {
            const CarData d = loadCarData(vfs, folder, c.baseName);
            for (const EngineRow& r : d.engine) {
                ++refs;
                if (!vfs.exists(soundPath("engines\\" + r.wave))) missing.insert("engines\\" + r.wave);
            }
            if (std::string(folder) == "player") {
                ++refs;
                if (!vfs.exists(soundPath("horns\\" + d.hornWave))) missing.insert("horns\\" + d.hornWave);
                ++refs;
                if (!vfs.exists(soundPath(d.clutchWave))) missing.insert(d.clutchWave);
            }
        }
    }
    std::printf("  %d car sample references, missing:", refs);
    for (const auto& m : missing) std::printf(" %s", m.c_str());
    std::printf("\n");
    expect(missing == std::set<std::string>{"horns\\FIRETRUCKSIREN"}, "car samples all present but vpsemi's horns\\FIRETRUCKSIREN");
    const SirenData sl = loadSiren(vfs, true), ss = loadSiren(vfs, false);
    size_t ssteps = 0;
    for (const auto& s : ss.samples) ssteps += s.steps.size();
    expect(sl.samples.size() == 2 && sl.samples[0].steps.size() == 1 && ss.samples.size() == 4 && ssteps == 16 && ss.explosion == "explosion",
           "sirens: London 2 samples / 2 steps, SF 4 samples / 16 steps, explosion");
    const SemiData sd = loadSemiData(vfs);
    expect(sd.reverse == "freightreverse" && sd.airBlow == "freightairblow" && sd.reverseVolume == 0.87f && sd.airVolume == 0.9f,
           "semidata.csv");

    // Surfaces.
    const SurfaceFile dry = loadSurfaceFile(vfs, false), wet = loadSurfaceFile(vfs, true);
    size_t skids = 0;
    for (const auto& b : dry.blocks) skids += b.skids.size();
    expect(dry.tunnel == 0 && dry.blocks.size() == 5 && skids == 8 && wet.tunnel == 5 && wet.blocks.size() == 6,
           "surface files: dry tunnel 0, 5 blocks, 8 skid rows; wet tunnel 5, 6 blocks");
    if (dry.blocks.size() >= 3) {
        const SurfaceBlock& g = dry.blocks[2];
        const float vol10 = 10.0f * g.volSlope + g.minVol, pitch10 = 10.0f * g.pitchSlope + g.minPitch;
        const SurfaceBlock& b0 = dry.blocks[0];
        const float skid65 = 0.65f * b0.skidRange + b0.minSkid;
        std::printf("  grass: volSlope %s pitchSlope %s skidRange %s; at 10 m/s %s / %s; block 1 skid range %s; asphalt skid at 0.65 %s\n",
                    f9(g.volSlope).c_str(), f9(g.pitchSlope).c_str(), f9(g.skidRange).c_str(), f9(vol10).c_str(),
                    f9(pitch10).c_str(), f9(dry.blocks[1].skidRange).c_str(), f9(skid65).c_str());
        expect(f9(g.volSlope) == "0.0160000008" && f9(g.pitchSlope) == "0.0159999989" && f9(g.skidRange) == "0.219999999" &&
                   f9(vol10) == "0.50999999" && f9(pitch10) == "1.00999999" && f9(dry.blocks[1].skidRange) == "0.349999994" &&
                   f9(skid65) == "0.746999979",
               "surface slopes from the text values (tyre-impact-sounds.md)");
    }
    const SuspensionData su = loadSuspension(vfs);
    expect(su.wave == "Suspension3" && su.minVel == 2.0f && f9(su.minVol) == "0.850000024" && f9(su.maxVol) == "0.899999976" &&
               f9(su.inv) == "0.333333343",
           "suspensionaudio.csv");
    const WobbleData wb = loadWobble(vfs);
    expect(wb.wave == "tirewobble1" && f9(wb.inv) == "0.0666666701" && f9(0.335f * 6.28318f) == "2.10486531",
           "tirewobble.csv; a 0.335 m wheel thumps every 2.10486531 m");
    // Impacts.
    const ImpactData pi = loadImpacts(vfs, "player"), oi = loadImpacts(vfs, "opponent");
    expect(pi.ok && oi.ok && pi.entries.size() == 24 && oi.entries.size() == 24, "impact tables: 24 entries, ENDOFDATA found");
    if (pi.ok && oi.ok && !pi.entries.empty() && pi.entries[0].second.size() == 3 && oi.entries[0].second.size() == 3) {
        const auto& pw = pi.entries[0].second;
        const float soft = pw[0].slope * 8000.0f + pw[0].minV, med = pw[1].slope * 8000.0f + pw[1].minV;
        const auto& ow = oi.entries[0].second;
        const float omed = ow[1].slope * 1000.0f + ow[1].minV;
        std::printf("  WALL: player at 8000 %s / %s, opponent at 1000 %s\n", f9(soft).c_str(), f9(med).c_str(), f9(omed).c_str());
        expect(f9(soft) == "0.932857156" && f9(med) == "0.939999998" && f9(omed) == "0.949999988", "WALL impact volumes");
    }
}

void testTrafficFiles(const Vfs& vfs, const Expect& expect) {
    LineReader r1 = openCsv(vfs, "aud/cardata/ambient/default_engine");
    const TrafficEngineData d = parseTrafficEngine(r1);
    LineReader r2 = openCsv(vfs, "aud/cardata/ambient/va_smallsuv_s_engine");
    const TrafficEngineData suv = parseTrafficEngine(r2);
    const float p0 = d.pitch(0, 0, false, 0), p15 = d.pitch(15, 15, false, 0), p15d = d.pitch(15, 16, false, 0),
                p33 = d.pitch(33, 33, false, 0), p33d = d.pitch(33, 33, true, 0), suv40 = suv.pitch(40, 41, false, 0);
    std::printf("  traffic engine pitch: 0 %.4f, 15 %.4f / %.4f, 33 %.4f / %.4f, small SUV slowing at 40 %.4f\n", double(p0),
                double(p15), double(p15d), double(p33), double(p33d), double(suv40));
    expect(d.sample == "enginesedan1" && d.rows.size() == 3 && near(p0, 0.317, 1e-6) && near(p15, 0.6275, 6e-5) &&
               near(p15d, 0.7268, 6e-5) && near(p33, 1.0, 1e-6) && near(p33d, 1.2186, 6e-5) && near(suv40, 2.2166, 6e-5),
           "traffic engine pitch tables (traffic-sounds.md)");
    // A knocked car from 15 m/s glides for 26 frames before its idle pitch.
    float prev = 15.0f, P = 0;
    int glide = 0;
    float first = 0, last = 0;
    for (int f = 0; f < 60; ++f) {
        const float speed = 0.0f;
        float s;
        bool decay;
        if ((prev - speed) > 4.0f) {
            s = prev * 0.95f;
            prev = s;
            decay = true;
        } else {
            s = speed;
            decay = false;
        }
        P = d.pitch(s, prev, decay, P);
        if (decay) {
            if (glide == 0) first = P;
            last = P;
            ++glide;
        }
        if (!decay) prev = speed;
    }
    std::printf("  knocked from 15 m/s: %d frames %.4f -> %.4f, then %.4f\n", glide, double(first), double(last), double(P));
    expect(glide == 26 && near(first, 0.7063, 6e-5) && near(last, 0.4250, 6e-5) && near(P, 0.317, 1e-6), "knocked car pitch glide");

    LineReader r3 = openCsv(vfs, "aud/cardata/ambient/default_horn");
    const TrafficHornData h = parseTrafficHorn(r3);
    expect(h.sample == "CARHORNAMB1" && h.patterns.size() == 3 && h.minForce == 5500.0f, "default_horn.csv: 3 patterns, force 5500");
    // Frame counts of the patterns at F = 1/60 (sounding + pausing frames per segment).
    std::string got;
    for (const auto& segs : h.patterns) {
        HornCopy::Pattern p;
        p.state = HornCopy::Playing;
        std::vector<int> snd(segs.size()), pau(segs.size());
        for (int f = 0; f < 2000 && p.state != HornCopy::Idle; ++f) {
            const int seg = p.seg;
            if (p.state == HornCopy::Playing) ++snd[size_t(seg)];
            else ++pau[size_t(seg)];
            TrafficAudio::stepHornPattern(p, segs, float(1.0 / 60.0), nullptr);
        }
        for (size_t i = 0; i < segs.size(); ++i) got += strf("%s%d+%d", i ? ", " : (got.empty() ? "" : "; "), snd[i], pau[i]);
    }
    std::printf("  horn patterns at 1/60 s: %s\n", got.c_str());
    expect(got == "30+12, 91+1; 15+6, 9+6, 15+5, 61+1; 1+9, 181+1", "horn pattern timing (traffic-sounds.md)");

    LineReader r4 = openCsv(vfs, "aud/creaturedata/default_ambcarvoice_s1");
    const VoiceData v = parseVoiceFile(r4);
    expect(v.bands.size() == 2 && v.bands[0].maxS == 20.0f && v.bands[0].A == 5.0f && v.bands[1].minS == 50.0f &&
               v.bands[1].B == 5.0f && v.bands[0].samples.size() == 3 && v.hasImpact && v.impactForce == 5500.0f &&
               v.impact.size() == 3 && v.impact[0].delay == 1.0f,
           "driver voice file: two bands and the impact block");
    expect(readCountFile(vfs, "aud/creaturedata/numambcarvoicefiles_s") == 5.0f &&
               readCountFile(vfs, "aud/creaturedata/numambcarvoicefiles_l") == 3.0f &&
               readCountFile(vfs, "aud/creaturedata/numfemalepedvoicefiles") == 3.0f &&
               readCountFile(vfs, "aud/creaturedata/nummalepedvoicefiles") == 1.0f,
           "voice count files: _s 5, _l 3, female 3, male 1");
}

void testWorld(const Vfs& vfs, const Expect& expect) {
    expect(ambientList(vfs, "london") == std::vector<std::string>{"londonriver", "tubevoices"} &&
               ambientList(vfs, "sf") == std::vector<std::string>{"birdies", "buoyseals", "horns_gulls", "trolleycable"} &&
               ambientList(vfs, "London").empty(),
           "city ambience lists (exact city names)");
    std::map<std::string, size_t> pts;
    for (const char* n : {"birdies", "buoyseals", "horns_gulls", "trolleycable", "londonriver", "tubevoices"})
        pts[n] = loadAmbientFile(vfs, n).points.size();
    expect(pts["birdies"] == 23 && pts["buoyseals"] == 93 && pts["horns_gulls"] == 69 && pts["trolleycable"] == 93 &&
               pts["londonriver"] == 24 && pts["tubevoices"] == 10,
           "ambient point counts");
    SoundSystem sys;
    sys.setVfs(&vfs);
    {
        AudioWorld w(sys, vfs);
        w.city = "london";
        WorldAudio wa(w);
        wa.setup(true);
        w.mgr.setListener(vec3(700, 0, -150), vec3(1, 0, 0));
        wa.updateObjects();
        w.mgr.updateHolders(1.0f / 30.0f);
        const AmbientObject* river = wa.cityObjects().empty() ? nullptr : wa.cityObjects()[0].get();
        if (river) {
            std::printf("  London (700, 0, -150): londonriver point %d (%.2f, %.2f, %.2f) %.1f m, A %.4f\n", river->chosenPoint(),
                        double(river->position.x), double(river->position.y), double(river->position.z),
                        std::sqrt(double(river->d2)), double(river->A));
        }
        expect(river && river->chosenPoint() == 6 && near(std::sqrt(river->d2), 58.8, 0.05) && river->A == 1.0f && river->holdsSlot(),
               "London: listener picks londonriver point 6, 58.8 m, A = 1 (world-sounds.md)");
    }
    {
        AudioWorld w(sys, vfs);
        w.city = "sf";
        WorldAudio wa(w);
        wa.setup(true);
        w.mgr.setListener(vec3(-800, 40, 160), vec3(1, 0, 0));
        wa.updateObjects();
        w.mgr.updateHolders(1.0f / 30.0f);
        const auto& objs = wa.cityObjects();
        int held = 0;
        for (const auto& o : objs)
            if (o->holdsSlot()) ++held;
        const AmbientObject* cable = objs.size() == 4 ? objs[3].get() : nullptr;
        if (cable)
            std::printf("  SF (-800, 40, 160): trolleycable point %d, %.1f m, A %.4f, %d of 4 objects heard\n", cable->chosenPoint(),
                        std::sqrt(double(cable->d2)), double(cable->A), held);
        expect(cable && cable->chosenPoint() == 91 && near(std::sqrt(cable->d2), 22.5, 0.05) && near(cable->A, 0.9591, 6e-5) && held == 1,
               "SF: trolleycable point 91, 22.5 m, A 0.9591, the others out of range");
    }
    // Thunder at night: 15, 16, 31, 32, 47, 48 s at 30 frames per second.
    {
        AudioWorld w(sys, vfs);
        w.weather = 3;
        w.timeOfDay = 3;
        RainAudio rain(w, true);
        const float dt = float(1.0 / 30.0);
        std::string times;
        bool was[2] = {false, false};
        int claps = 0;
        for (int f = 0; f < 30 * 50 && claps < 6; ++f) {
            sys.beginFrame(dt);
            rain.frame(dt);
            for (int i = 0; i < 2; ++i) {
                const bool now = rain.thunder(i)->playing();
                if (now && !was[i]) {
                    times += strf("%s%.2f", times.empty() ? "" : ", ", double(f) / 30.0);
                    ++claps;
                }
                was[i] = now;
            }
            sys.endFrame();
        }
        std::printf("  thunder at %s s\n", times.c_str());
        expect(times == "15.00, 16.00, 31.00, 32.00, 47.00, 48.00", "thunder timing (world-sounds.md)");
    }
    // A cable car accelerating at 1.5 m/s² from rest, then braking at 3 m/s².
    {
        AudioWorld w(sys, vfs);
        w.mgr.setListener(vec3(0, 0, 0), vec3(1, 0, 0));
        CableCarSound c(w, 0.0f);
        const float dt = float(1.0 / 30.0);
        float v = 0, startAt = -1, loopAt = -1, stopAt = -1, startV = 0, stopV = 0;
        for (int f = 0; f < 30 * 20; ++f) {
            sys.beginFrame(dt);
            const float t = float(f) * dt;
            if (t < 8.0f) v = v + 1.5f * dt;
            else v = std::max(0.0f, v - 3.0f * dt);
            c.ownerFrame(vec3(0, 0, 5), v);
            const int before = c.state();
            w.mgr.updateHolders(dt);
            const int st = c.state();
            if (st == 2 && before != 2 && startAt < 0) {
                startAt = t;
                startV = v;
            }
            if (st == 3 && before != 3 && loopAt < 0) loopAt = t;
            if (st == 1 && before != 1 && stopAt < 0) {
                stopAt = t;
                stopV = v;
            }
            sys.endFrame();
        }
        std::printf("  cable car: start at %.2f m/s, loop %.2f s later, stopping at %.2f m/s\n", double(startV), double(loopAt - startAt),
                    double(stopV));
        expect(startAt >= 0 && startV >= 0.1f && startV < 0.16f && near(loopAt - startAt, 1.74, 0.05) && stopAt > 0 && stopV <= 0.5f &&
                   stopV > 0.39f,
               "cable car: bell and start clip at 0.1 m/s, loop when the start clip ends, stopping below 0.5 m/s");
    }
}

void testSiren(const Vfs& vfs, const Expect& expect) {
    SoundSystem sys;
    sys.setVfs(&vfs);
    AudioWorld w(sys, vfs);
    w.city = "sf";
    w.loadSharedCarFiles();
    phys::Car car;
    car.setup(phys::loadCarTune(vfs, "vpcop", false));
    car.resetFlat(0, 0, 0, 0);
    w.mgr.setListener(car.body().p + vec3(0, 0, 10), vec3(1, 0, 0));
    CarAudio a(w);
    const bool ok = a.setup(&car, "vpcop", CarAudio::Owner::Ai, true);
    a.postPhysics();
    a.sirenStart(true);
    const int chase = w.chaseCount;
    const float dt = float(1.0 / 30.0);
    std::string times;
    int switches = 0, cur = a.sirenCurrent();
    for (int f = 1; f <= 30 * 12 && switches < 4; ++f) {
        sys.beginFrame(dt);
        w.mgr.updateHolders(dt);
        sys.endFrame();
        if (a.sirenCurrent() != cur) {
            cur = a.sirenCurrent();
            times += strf("%s%.2f", times.empty() ? "" : ", ", double(f) / 30.0);
            ++switches;
        }
    }
    a.sirenStop();
    // A wreck's wind-down, cut by the Stop, runs on at the next start; at its end the sample
    // loops on (car-sounds.md, "Explosion").
    a.explosion();
    a.sirenStop();
    a.sirenStart(false);
    int f2 = 0;
    for (; f2 < 30 * 10 && a.sirenSounding(); ++f2) {
        sys.beginFrame(dt);
        w.mgr.updateHolders(dt);
        sys.endFrame();
    }
    const bool loopsOn = !a.sirenSounding() && !a.windingDown() && a.sirenSamplePlaying();
    a.sirenStart(false);
    sys.beginFrame(dt);
    w.mgr.updateHolders(dt);
    sys.endFrame();
    const bool normal = a.sirenSounding() && !a.windingDown() && a.sirenSamplePlaying();
    std::printf("  wind-down ran %.2f s; then the sample loops on: %s; the next start sounds normally: %s\n", double(f2) / 30.0,
                loopsOn ? "yes" : "no", normal ? "yes" : "no");
    expect(loopsOn && normal, "a wind-down's end leaves the siren sample looping; the next start sounds normally");
    a.sirenStop();
    std::printf("  SF siren switches at %s s; chase count %d, %d after the stop\n", times.c_str(), chase, w.chaseCount);
    expect(ok && a.kind() == CarAudioKind::Police && chase == 1 && w.chaseCount == 0 && times == "4.50, 5.77, 8.43, 10.77",
           "SF siren stepping at 30 frames per second and the chase count (car-sounds.md)");
}

void testSpeech(const Vfs& vfs, const Expect& expect) {
    SoundSystem sys;
    sys.setVfs(&vfs);
    bool all = true;
    std::string voices;
    for (const char* v : {"as1", "as2", "as4", "as5", "al1", "al2", "al3", "al4", "al5", "al6"}) {
        SpeechPlayer p(sys, "aud11\\" + toLower(v));
        SpeechCategories c;
        if (!loadSpeechFile(vfs, std::string("aud/spchdata/") + v + "/checkpoint", v, false, p, c)) {
            all = false;
            continue;
        }
        voices += std::string(voices.empty() ? "" : " ") + v;
        const auto pre = c.range("PRERACE");
        if (!(pre.first == 0 && pre.last == 1 && c.entry("FINALCHECKPOINT") == 2 && c.entry("RESULTSPOOR") == 3 &&
              c.entry("RESULTSMID") == 4 && c.entry("RESULTSWIN") == 5))
            all = false;
    }
    std::printf("  checkpoint files of voices %s\n", voices.c_str());
    expect(all, "Checkpoint speech: PRERACE 0-1, FINALCHECKPOINT 2, RESULTSPOOR 3, MID 4, WIN 5 for every voice (speech.md)");
    SpeechPlayer p(sys, "aud11\\AS1");
    SpeechCategories c;
    loadSpeechFile(vfs, "aud/spchdata/as1/cruise", "AS1", false, p, c);
    expect(c.range("PRERACE").first == -1, "SF voice 1 has no PRERACE range in Cruise");
    // The clip choice never repeats an entry's clip.
    SpeechPlayer q(sys, "aud11\\AL1");
    const int e = q.addEntry("AL1PRE", 11, 0);
    bool noRepeat = true;
    int last = -1;
    for (long long s = 1792000000; s < 1792000100; ++s) {
        pinSoundClock(s);
        const int k = q.chooseClip(e);
        if (k == last || k < 1 || k > 11) noRepeat = false;
        last = k;
    }
    pinSoundClock(-1);
    expect(noRepeat, "speech clip choice 1 … end, never the same twice in a row");
}

void testOneShots(const Vfs& vfs, const Expect& expect) {
    SoundSystem sys;
    sys.setVfs(&vfs);
    RaceOneShots o(sys);
    o.setup(1);  // Checkpoint
    const float dt = float(1.0 / 30.0);
    std::string got;
    std::vector<double> at;
    bool was[2] = {false, false};
    o.countdown(2.5f);
    for (int f = 0; f < 120; ++f) {
        sys.beginFrame(dt);
        if (f > 0) o.update(dt);
        const char* names[2] = {"Startracelow", "Startracehigh"};
        for (int i = 0; i < 2; ++i) {
            const bool now = o.playing(names[i]);
            if (now && !was[i]) {
                got += strf("%s%s %.2f", got.empty() ? "" : ", ", i ? "high" : "low", double(f) / 30.0);
                at.push_back(double(f) / 30.0 + (i ? 100.0 : 0.0));
            }
            was[i] = now;
        }
        sys.endFrame();
    }
    std::printf("  Checkpoint countdown: %s\n", got.c_str());
    expect(at.size() == 3 && at[0] == 0.0 && near(at[1], 1.25, 0.034) && near(at[2], 102.5, 0.034),
           "countdown: low, low again with 1.25 s left, high at 0, to the frame (speech.md)");
    o.setup(3);
    o.damagedOut();
    const bool circuit = o.playing("Messagenote");
    o.setup(6);
    o.damagedOut(5);
    const bool cc5 = o.playing("Youlose");
    o.setup(0);
    expect(circuit && cc5 && o.has("Messagenote") && !o.has("Startracelow"),
           "damaged out: the message chime in Circuit, Youlose in a no-contact Crash Course event; Cruise loads only Messagenote");
}

// The race session's events (game/race.h) into a game's sound: speech, one-shots, engine silence,
// timer warning, the results screen; the Crash Course instructor's gate list.
void testRaceEvents(const Vfs& vfs, const Expect& expect) {
    SoundSystem sys;
    sys.setVfs(&vfs);
    phys::CollisionWorld flat;
    flat.setupFlat(vfs, 0.0f);
    phys::Car car;
    car.setup(phys::loadCarTune(vfs, "vpbug", true));
    car.resetFlat(0, 0, 0, 0);
    const float F = float(1.0 / 30.0);
    auto frames = [&](GameAudio& ga, int n) {
        for (int i = 0; i < n; ++i) {
            sys.beginFrame(F);
            ga.frame(F, car.body().p + vec3(0, 2, 6), vec3(1, 0, 0), false);
            sys.endFrame();
        }
    };
    pinSoundClock(1792000000);
    {
        GameAudio ga(sys, vfs);
        GameAudioConfig cfg;
        cfg.city = "sf";
        cfg.mode = 4;  // Blitz
        cfg.playerCar = "vpbug";
        ga.setup(cfg, car, &flat);
        ga.raceEvent("prerace", 0, 0);
        frames(ga, 60);
        const bool spoke = ga.speech().player() && !ga.speech().player()->lastClipFile().empty();
        ga.raceEvent("Startracelow", 0, 0);
        const bool low = ga.oneShots().playing("Startracelow");
        frames(ga, 10);
        const int before = sys.stats().playing;
        ga.raceEvent("engineSilence", 0, 0);
        frames(ga, 3);
        const int silenced = sys.stats().playing;
        ga.raceEvent("TimerwarningStart", 0, 0);
        ga.setRaceClock(9.5f);
        frames(ga, 1);
        const bool beep = ga.oneShots().playing("Timerwarning");
        ga.setRaceClock(2.5f);
        frames(ga, 40);
        const bool looped = ga.oneShots().playing("Timerwarning");
        ga.raceEvent("TimerwarningStop", 0, 0);
        const bool stopped = !ga.oneShots().playing("Timerwarning");
        ga.raceEvent("results", 1, 3);
        frames(ga, 2);
        const bool released = ga.world().mgr.holder(0) == nullptr && !ga.world().mgr.enabled();
        ga.raceEvent("prerace", 0, 0);
        frames(ga, 2);
        const bool back = ga.world().mgr.holder(0) == ga.playerAudio();
        std::printf("  Blitz events: pre-race clip %s; low %d; samples %d -> %d silenced; warning %d, looped %d, stopped %d; "
                    "results release %d, restart %d\n",
                    spoke ? ga.speech().player()->lastClipFile().c_str() : "-", low, before, silenced, beep, looped, stopped,
                    released, back);
        expect(spoke && low && silenced < before && beep && looped && stopped && released && back,
               "race events: pre-race line, countdown sound, engine silence, timer warning, results screen, restart");
    }
    {
        GameAudio ga(sys, vfs);
        GameAudioConfig cfg;
        cfg.city = "london";
        cfg.mode = 6;  // Crash Course
        cfg.course = 4;
        cfg.playerCar = "vpbug";
        ga.setup(cfg, car, &flat);
        std::string gates;
        for (int g : ga.crashSpeech().gateClips()) gates += strf("%s%d", gates.empty() ? "" : ", ", g);
        ga.raceEvent("ccPrerace", 0, 0);
        frames(ga, 60);
        const bool intro = ga.crashSpeech().player() && !ga.crashSpeech().player()->lastClipFile().empty();
        std::printf("  Crash Course London course 4: gate clips %s; intro %s\n", gates.c_str(),
                    intro ? ga.crashSpeech().player()->lastClipFile().c_str() : "-");
        expect(gates == "-1, 15, 3, 6, 5, 10, 12, 13" && intro, "Crash Course instructor: London course 4 gate list and intro");
    }
    pinSoundClock(-1);
}

// A short game on flat ground: the player's car (2D), SF ambience, rain at night, speech.
void testGame(const Vfs& vfs, const Expect& expect) {
    SoundSystem sys;
    RecBackend rec;
    sys.setVfs(&vfs);
    sys.setBackend(&rec);
    phys::CollisionWorld flat;
    flat.setupFlat(vfs, 0.0f);
    phys::Car car;
    car.setup(phys::loadCarTune(vfs, "vpbug", true));
    car.resetFlat(0, 0, 0, 0);
    GameAudio ga(sys, vfs);
    GameAudioConfig cfg;
    cfg.city = "sf";
    cfg.weather = 3;
    cfg.timeOfDay = 3;
    cfg.playerCar = "vpbug";
    ga.setup(cfg, car, &flat);
    ga.gameReset();
    const float F = float(1.0 / 30.0);
    const int n = phys::physicsStepsForFrame(F);
    int maxPlaying = 0;
    bool spoke = false, echoed = false;
    for (int f = 0; f < 150; ++f) {
        const bool tunnel = f >= 90;  // the last two seconds in the echo state, honking
        sys.beginFrame(F);
        phys::DriverControls dc;
        dc.accelerate = 1.0f;
        car.controlStep(dc);
        for (int k = 0; k < n; ++k) car.step(F / float(n), float(n) / F, flat);
        if (f == 100) ga.hornPressed();
        if (f == 130) ga.hornReleased();
        ga.frame(F, car.body().p + vec3(0, 2, 6), vec3(1, 0, 0), tunnel);
        sys.endFrame();
        if (f < 90) maxPlaying = std::max(maxPlaying, sys.stats().playing);
        if (tunnel && sys.stats().voices > sys.stats().playing) echoed = true;
        if (ga.speech().player() && ga.speech().player()->speaking()) spoke = true;
    }
    const std::string st = ga.statsJson();
    std::printf("  flat-ground drive: up to %d samples playing, %d voices started, %d live, echo copies %s; %s\n", maxPlaying,
                rec.starts, int(rec.live.size()), echoed ? "sounded" : "silent", st.c_str());
    expect(echoed, "in the echo state the horn's echo copy sounds");
    expect(ga.world().mgr.holder(0) == ga.playerAudio() && maxPlaying >= 3 && !rec.live.empty() && spoke &&
               ga.worldSounds().cityObjects().size() == 4 && ga.worldSounds().rain() != nullptr,
           "a short game: the player's car holds slot 0 with engine samples and rain playing, the announcer speaks");
}

}  // namespace

void testAudio(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect) {
    std::printf("== Sound (spec/audio)\n");
    if (!vfs.exists("aud/cardata/shared/vehtypes.csv")) {
        std::printf("  (the audio archives are not loaded: sound checks skipped)\n");
        return;
    }
    testWavs(vfs, expect);
    testPathsAndMixer(vfs, expect);
    test3d(expect);
    testCarFiles(vfs, expect);
    testTrafficFiles(vfs, expect);
    testWorld(vfs, expect);
    testSiren(vfs, expect);
    testSpeech(vfs, expect);
    testOneShots(vfs, expect);
    testRaceEvents(vfs, expect);
    testGame(vfs, expect);
}
