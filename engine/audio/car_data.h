// The car audio data files (spec/audio/car-sounds.md and tyre-impact-sounds.md): vehicle types,
// the cardata file with its engine rows, the siren and semi files, the surface, suspension,
// tyre-wobble and impact files. Numbers are kept exactly as MM2 computes them.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cc {
class Vfs;

namespace audio {

// aud/cardata/shared/vehtypes.csv.
struct VehTypes {
    bool found = false;
    std::vector<std::string> semiBus, police;
    bool alwaysNitro = false;
};
VehTypes loadVehTypes(const Vfs& vfs);

enum class CarAudioKind { Plain, SemiBus, Police };
// Exact (case-sensitive) name comparison, semi/bus first (nitro counts as plain).
CarAudioKind carAudioKind(const VehTypes& t, const std::string& car);

// One engine sample row (window or divisor format).
struct EngineRow {
    std::string wave;
    bool divisor = false;
    float v0 = 0, v1 = 0;
    float a = 0, b = 0, c = 0, d = 0;  // fade in start/end, fade out start/end
    float p0 = 0, p1 = 0, s = 0, e = 0;
    float kIn = 0, kOut = 0, kP = 0;
    float D = 0, R = 0;  // divisor format
    // V(r) and P(r) of "Volume and pitch of one engine sample" (silenced: v₀ = v₁ = 0).
    float volume(float rpm, bool silenced) const;
    float pitch(float rpm) const;
};

// aud/cardata/<player|opponent>/<car>.csv (or default.csv of the same folder).
struct CarData {
    bool ok = false;
    std::string file;  // the file that was used
    std::string hornWave;
    float hornVolume = 0;
    int flags = 0, engineCount = 0;  // read, ignored
    std::string clutchWave;
    float clutchVolume = 0;
    std::vector<EngineRow> engine;
};
CarData loadCarData(const Vfs& vfs, const std::string& folder, const std::string& car);
// One engine row from its fields (window or divisor format).
EngineRow parseEngineRow(const std::vector<std::string>& f, bool divisor);

// aud/cardata/player/<london|sf>policesiren.csv.
struct SirenData {
    bool found = false;
    std::string explosion;
    struct Sample {
        std::string wave;
        float volume = 0;
        std::vector<std::pair<float, int>> steps;  // play time, next index
    };
    std::vector<Sample> samples;
};
SirenData loadSiren(const Vfs& vfs, bool london);

// aud/cardata/shared/semidata.csv.
struct SemiData {
    bool found = false;
    std::string reverse, airBlow;
    float reverseVolume = 0.87f, airVolume = 0.9f;
};
SemiData loadSemiData(const Vfs& vfs);

// aud/cardata/player/default_surface<dry|wet>.csv.
struct SurfaceBlock {
    bool hasRolling = false;
    std::string rolling;
    float maxSpeed = 0, minVol = 0, maxVol = 0, minPitch = 0, maxPitch = 0, minSkid = 0, maxSkid = 0;
    float k = 0, volSlope = 0, pitchSlope = 0, skidRange = 0;
    struct Skid {
        bool hasSample = false;
        std::string wave;
        float minSlide = 0, maxSlide = 0;
    };
    std::vector<Skid> skids;
};
struct SurfaceFile {
    bool found = false;
    int tunnel = 0;
    std::vector<SurfaceBlock> blocks;
};
SurfaceFile loadSurfaceFile(const Vfs& vfs, bool wet);

// aud/cardata/player/suspensionaudio.csv.
struct SuspensionData {
    bool found = false;
    std::string wave;
    float minVel = 0, maxVel = 0, minVol = 0, maxVol = 0, inv = 0;
};
SuspensionData loadSuspension(const Vfs& vfs);

// aud/cardata/player/tirewobble.csv.
struct WobbleData {
    bool found = false;
    std::string wave;
    float minV = 0, maxV = 0, minP = 0, maxP = 0, inv = 0;
};
WobbleData loadWobble(const Vfs& vfs);

// aud/cardata/<player|opponent>/default_impacts.csv.
struct ImpactRowData {
    std::string name;
    float minV = 0, maxV = 0, minF = 0, maxF = 0, freq = 1, slope = 0;
};
struct ImpactData {
    bool ok = false;  // opened and ENDOFDATA reached
    std::vector<std::pair<std::string, std::vector<ImpactRowData>>> entries;
};
ImpactData loadImpacts(const Vfs& vfs, const std::string& folder);

}  // namespace audio
}  // namespace cc
