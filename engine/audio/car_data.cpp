#include "audio/car_data.h"

#include <cmath>

#include "audio/text_reader.h"
#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {
namespace audio {

namespace {
inline float inv(float x) { return x == 0.0f ? 0.0f : 1.0f / x; }

std::vector<std::string> nextFields(LineReader& r, bool* got = nullptr) {
    std::string raw;
    const bool ok = r.read(raw);
    if (got) *got = ok;
    return splitFields(cutLine(raw));
}

// A list line: names up to a field exactly ENDOFDATA (ours: the end of the line ends it too).
std::vector<std::string> nameList(const std::vector<std::string>& f) {
    std::vector<std::string> out;
    for (const std::string& s : f) {
        if (s == "ENDOFDATA") break;
        out.push_back(s);
    }
    return out;
}
}  // namespace

VehTypes loadVehTypes(const Vfs& vfs) {
    VehTypes t;
    LineReader r = openCsv(vfs, "aud/cardata/shared/vehtypes");
    if (!r.valid()) return t;
    t.found = true;
    nextFields(r);
    t.semiBus = nameList(nextFields(r));
    nextFields(r);
    t.police = nameList(nextFields(r));
    nextFields(r);
    const auto f = nextFields(r);
    t.alwaysNitro = !f.empty() && iequals(f[0], "TRUE");
    return t;
}

CarAudioKind carAudioKind(const VehTypes& t, const std::string& car) {
    for (const auto& n : t.semiBus)
        if (n == car) return CarAudioKind::SemiBus;
    for (const auto& n : t.police)
        if (n == car) return CarAudioKind::Police;
    return CarAudioKind::Plain;  // nitro audio is plain audio (ours)
}

float EngineRow::volume(float r, bool silenced) const {
    const float lo = silenced ? 0.0f : v0, hi = silenced ? 0.0f : v1;
    if (divisor) {
        float v = r < R ? r / D : D / r;
        if (v < lo) v = lo;
        else if (v > hi) v = hi;
        return v;
    }
    if (r <= a) return lo;
    if (!(r < d)) return lo;
    if (r < b) return (r - a) * (silenced ? 0.0f : kIn) + lo;
    if (r <= c) return hi;
    return (d - r) * (silenced ? 0.0f : kOut) + lo;
}

float EngineRow::pitch(float r) const {
    if (divisor) return r * kP + p0;  // ours: the unset bounds count as none
    if (r <= s) return p0;
    if (!(r < e)) return p1;
    return r * kP + p0;
}

EngineRow parseEngineRow(const std::vector<std::string>& f, bool divisor) {
    EngineRow w;
    w.wave = fieldS(f, 0);
    w.divisor = divisor;
    if (divisor) {
        w.v0 = fieldF(f, 1);
        w.v1 = fieldF(f, 2);
        w.D = fieldF(f, 3);
        w.p0 = fieldF(f, 4);
        w.p1 = fieldF(f, 5);
        w.kP = inv(fieldF(f, 6));
        w.R = fieldF(f, 7);
        return w;
    }
    w.v0 = fieldF(f, 1);
    w.v1 = fieldF(f, 2);
    w.a = fieldF(f, 3);
    w.b = fieldF(f, 4);
    w.c = fieldF(f, 5);
    const double dRead = fieldD(f, 6);
    w.d = float(dRead);
    w.p0 = fieldF(f, 7);
    w.p1 = fieldF(f, 8);
    w.s = fieldF(f, 9);
    const double eRead = fieldD(f, 10);
    w.e = float(eRead);
    w.kIn = (w.v1 - w.v0) * inv(w.b - w.a);
    w.kOut = (w.v1 - w.v0) * inv(float(dRead - double(w.c)));
    w.kP = (w.p1 - w.p0) * inv(float(eRead - double(w.s)));
    return w;
}

namespace {
bool loadCarFile(const Vfs& vfs, const std::string& path, CarData& c) {
    LineReader r = openCsv(vfs, path);
    if (!r.valid()) return false;
    std::string raw;
    std::vector<std::string> lines;
    while (r.read(raw)) lines.push_back(raw);
    if (lines.size() >= 2) {
        // Line 2 is read first: its samples count even if the file then proves unusable.
        const auto f = splitFields(cutLine(lines[1]));
        c.hornWave = fieldS(f, 0);
        c.hornVolume = fieldF(f, 1);
        c.flags = fieldI(f, 2);
        c.engineCount = fieldI(f, 3);
        c.clutchWave = fieldS(f, 4);
        c.clutchVolume = fieldF(f, 5);
    }
    if (lines.size() < 3) return false;
    const auto h = splitFields(cutLine(lines[2]));
    const bool divisor = iequals(fieldS(h, 3), "Volume Divisor");
    c.engine.clear();
    for (size_t i = 3; i < lines.size(); ++i) {
        const auto f = splitFields(cutLine(lines[i]));
        if (f.empty()) {
            logFile(LogLevel::Warn, path + ".csv", strf("line %zu is empty (MM2 would crash); skipped", i + 1));
            continue;
        }
        if (f.size() < (divisor ? 8u : 11u))
            logFile(LogLevel::Warn, path + ".csv", strf("line %zu has %zu fields (MM2 would crash); missing numbers read 0", i + 1, f.size()));
        c.engine.push_back(parseEngineRow(f, divisor));
    }
    c.ok = true;
    c.file = path;
    return true;
}
}  // namespace

CarData loadCarData(const Vfs& vfs, const std::string& folder, const std::string& car) {
    CarData c;
    if (loadCarFile(vfs, "aud/cardata/" + folder + "/" + car, c)) return c;
    CarData d;
    if (loadCarFile(vfs, "aud/cardata/" + folder + "/default", d)) return d;
    return CarData();
}

SirenData loadSiren(const Vfs& vfs, bool london) {
    SirenData s;
    LineReader r = openCsv(vfs, london ? "aud/cardata/player/londonpolicesiren" : "aud/cardata/player/sfpolicesiren");
    if (!r.valid()) return s;
    s.found = true;
    nextFields(r);
    s.explosion = fieldS(nextFields(r), 0);
    enum { None, SampleMode, StepMode } mode = None;
    std::string raw;
    while (r.read(raw)) {
        const auto f = splitFields(cutLine(raw));
        const std::string& first = fieldS(f, 0);
        if (first == "Sample name") {
            mode = SampleMode;
        } else if (first == "play time") {
            mode = StepMode;
        } else if (mode == SampleMode) {
            SirenData::Sample x;
            x.wave = first;
            x.volume = fieldF(f, 1);
            s.samples.push_back(x);
        } else if (mode == StepMode) {
            if (s.samples.empty()) continue;
            s.samples.back().steps.push_back({fieldF(f, 0), fieldI(f, 1)});
        }
    }
    return s;
}

SemiData loadSemiData(const Vfs& vfs) {
    SemiData d;
    LineReader r = openCsv(vfs, "aud/cardata/shared/semidata");
    if (!r.valid()) return d;
    d.found = true;
    nextFields(r);
    const auto f = nextFields(r);
    d.reverse = fieldS(f, 0);
    d.airBlow = fieldS(f, 1);
    d.reverseVolume = fieldF(f, 2);
    d.airVolume = fieldF(f, 3);
    return d;
}

SurfaceFile loadSurfaceFile(const Vfs& vfs, bool wet) {
    SurfaceFile sf;
    LineReader r = openCsv(vfs, wet ? "aud/cardata/player/default_surfacewet" : "aud/cardata/player/default_surfacedry");
    if (!r.valid()) return sf;
    sf.found = true;
    std::string raw;
    r.read(raw);  // header (not checked)
    if (r.read(raw)) sf.tunnel = atoiC(fieldS(splitFields(cutLine(raw, true)), 0));
    for (;;) {
        if (!r.read(raw)) break;  // b1: end of file here ends the file normally
        sf.blocks.emplace_back();
        SurfaceBlock& b = sf.blocks.back();
        if (!r.read(raw)) break;
        const auto f = splitFields(cutLine(raw, true));
        const std::string& name = fieldS(f, 0);
        b.hasRolling = !name.empty() && name != "NOSOUND";
        b.rolling = name;
        const double maxSpeed = fieldD(f, 1), minVol = fieldD(f, 2), maxVol = fieldD(f, 3), minPitch = fieldD(f, 4),
                     maxPitch = fieldD(f, 5), minSkid = fieldD(f, 6), maxSkid = fieldD(f, 7);
        b.maxSpeed = float(maxSpeed);
        b.minVol = float(minVol);
        b.maxVol = float(maxVol);
        b.minPitch = float(minPitch);
        b.maxPitch = float(maxPitch);
        b.minSkid = float(minSkid);
        b.maxSkid = float(maxSkid);
        b.k = b.maxSpeed == 0.0f ? 0.0f : 1.0f / b.maxSpeed;
        b.volSlope = float(maxVol - double(b.minVol)) * b.k;
        b.pitchSlope = float(maxPitch - double(b.minPitch)) * b.k;
        b.skidRange = float(maxSkid - double(b.minSkid));
        const int n = fieldI(f, 8);
        if (!r.read(raw)) break;  // b3
        bool eof = false;
        for (int i = 0; i < n; ++i) {
            if (!r.read(raw)) {
                eof = true;
                break;
            }
            const auto g = splitFields(raw);  // not cut
            SurfaceBlock::Skid sk;
            sk.wave = fieldS(g, 0);
            sk.hasSample = !sk.wave.empty() && sk.wave != "NOSOUND";
            sk.minSlide = fieldF(g, 1);
            sk.maxSlide = fieldF(g, 2);
            b.skids.push_back(sk);
        }
        if (eof) break;
    }
    return sf;
}

SuspensionData loadSuspension(const Vfs& vfs) {
    SuspensionData s;
    LineReader r = openCsv(vfs, "aud/cardata/player/suspensionaudio");
    if (!r.valid()) return s;
    std::string raw;
    if (!r.read(raw) || !r.read(raw)) return s;
    const auto f = splitFields(cutLine(raw, true));
    s.found = true;
    s.wave = fieldS(f, 0);
    s.minVel = fieldF(f, 1);
    s.maxVel = fieldF(f, 2);
    s.minVol = fieldF(f, 3);
    s.maxVol = fieldF(f, 4);
    const double div = fieldD(f, 5);
    s.inv = div == 0.0 ? 0.0f : float(1.0 / div);
    return s;
}

WobbleData loadWobble(const Vfs& vfs) {
    WobbleData w;
    LineReader r = openCsv(vfs, "aud/cardata/player/tirewobble");
    if (!r.valid()) return w;
    std::string raw;
    if (!r.read(raw) || !r.read(raw)) return w;
    const auto f = splitFields(cutLine(raw, true));
    w.found = true;
    w.wave = fieldS(f, 0);
    w.minV = fieldF(f, 1);
    w.maxV = fieldF(f, 2);
    w.minP = fieldF(f, 3);
    w.maxP = fieldF(f, 4);
    const double div = fieldD(f, 5);
    w.inv = div == 0.0 ? 0.0f : float(1.0 / div);
    return w;
}

ImpactData loadImpacts(const Vfs& vfs, const std::string& folder) {
    ImpactData d;
    LineReader r = openCsv(vfs, "aud/cardata/" + folder + "/default_impacts");
    if (!r.valid()) return d;
    std::string raw;
    for (;;) {
        if (!r.read(raw)) return ImpactData();  // e1: end of file before ENDOFDATA: no table
        if (!r.read(raw)) return ImpactData();  // e2
        if (!r.read(raw)) return ImpactData();  // e3
        const auto f = splitFields(cutLine(raw, true));
        const std::string& name = fieldS(f, 0);
        if (name == "ENDOFDATA") break;
        const int n = fieldI(f, 1);
        std::vector<ImpactRowData> rows;
        if (n > 0) {
            if (!r.read(raw)) return ImpactData();  // e4
            for (int i = 0; i < n; ++i) {
                if (!r.read(raw)) return ImpactData();
                const auto g = splitFields(cutLine(raw, true));
                ImpactRowData x;
                x.name = fieldS(g, 0);
                x.minV = fieldF(g, 1);
                x.maxV = fieldF(g, 2);
                x.minF = fieldF(g, 3);
                x.maxF = fieldF(g, 4);
                x.freq = fieldF(g, 5);
                const float span = x.maxF - x.minF;
                x.slope = span == 0.0f ? 0.0f : (x.maxV - x.minV) * (1.0f / span);
                rows.push_back(x);
            }
        }
        d.entries.push_back({name, rows});
    }
    d.ok = true;
    return d;
}

}  // namespace audio
}  // namespace cc
