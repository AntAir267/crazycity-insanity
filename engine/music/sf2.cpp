// SoundFont 2 banks as an alternative source of the General MIDI instruments MM2 takes from the
// system (directmusic.md, "General MIDI instruments"; our choice, Ant 2026-10-15). Each preset is
// flattened into one DLS-style instrument whose regions are the preset zone × instrument zone
// pairs, with the generators turned into the nearest DLS level 1 connections. SoundFont features
// DLS level 1 has no place for (filters, modulators, hold stages, the second LFO's own rate) are
// dropped.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "music/dm_files.h"
#include "music/riff.h"

namespace cc {
namespace music {

namespace {

enum Gen {
    gStartAddr = 0, gEndAddr = 1, gStartLoop = 2, gEndLoop = 3, gStartAddrCoarse = 4, gModLfoToPitch = 5,
    gVibLfoToPitch = 6, gModEnvToPitch = 7, gEndAddrCoarse = 12, gModLfoToVolume = 13, gPan = 17,
    gDelayModLfo = 21, gFreqModLfo = 22, gDelayVibLfo = 23, gFreqVibLfo = 24, gDelayModEnv = 25,
    gAttackModEnv = 26, gHoldModEnv = 27, gDecayModEnv = 28, gSustainModEnv = 29, gReleaseModEnv = 30,
    gKeyToModEnvDecay = 32, gDelayVolEnv = 33, gAttackVolEnv = 34, gHoldVolEnv = 35, gDecayVolEnv = 36,
    gSustainVolEnv = 37, gReleaseVolEnv = 38, gKeyToVolEnvDecay = 40, gInstrument = 41, gKeyRange = 43,
    gVelRange = 44, gStartLoopCoarse = 45, gAttenuation = 48, gEndLoopCoarse = 50, gCoarseTune = 51,
    gFineTune = 52, gSampleId = 53, gSampleModes = 54, gExclusiveClass = 57, gRootKey = 58, gCount = 61
};

struct Zone {
    int16_t v[gCount];
    bool set[gCount];
    Zone() {
        std::memset(v, 0, sizeof v);
        std::memset(set, 0, sizeof set);
    }
};

int16_t defaultOf(int g) {
    switch (g) {
        case gDelayModLfo: case gDelayVibLfo: case gDelayModEnv: case gAttackModEnv: case gHoldModEnv:
        case gDecayModEnv: case gReleaseModEnv: case gDelayVolEnv: case gAttackVolEnv: case gHoldVolEnv:
        case gDecayVolEnv: case gReleaseVolEnv: return -12000;
        case gKeyRange: case gVelRange: return int16_t(0x7F00);  // lo 0, hi 127
        case gRootKey: return -1;
        default: return 0;
    }
}

struct Shdr {
    uint32_t start, end, loopStart, loopEnd, rate;
    uint8_t pitch;
    int8_t correction;
    uint16_t type;
};

std::vector<Zone> readZones(const uint8_t* bag, size_t bagN, const uint8_t* gen, size_t genN, size_t first, size_t last) {
    std::vector<Zone> out;
    for (size_t b = first; b < last && b + 1 < bagN; ++b) {
        const size_t g0 = uint16_t(bag[b * 4] | (bag[b * 4 + 1] << 8));
        const size_t g1 = uint16_t(bag[(b + 1) * 4] | (bag[(b + 1) * 4 + 1] << 8));
        Zone z;
        for (size_t g = g0; g < g1 && g < genN; ++g) {
            const uint16_t op = uint16_t(gen[g * 4] | (gen[g * 4 + 1] << 8));
            const int16_t amt = int16_t(gen[g * 4 + 2] | (gen[g * 4 + 3] << 8));
            if (op < gCount) {
                z.v[op] = amt;
                z.set[op] = true;
            }
        }
        out.push_back(z);
    }
    return out;
}

int rangeLo(int16_t v) { return uint16_t(v) & 0xFF; }
int rangeHi(int16_t v) { return (uint16_t(v) >> 8) & 0xFF; }

}  // namespace

bool parseSf2(const Bytes& data, DlsCollection& col, std::string& err) {
    RiffIter top(data.data(), data.size());
    RiffChunk root;
    if (!top.next(root) || root.id != fourcc("RIFF") || root.form != fourcc("sfbk")) {
        err = "not a SoundFont 2 file";
        return false;
    }
    const uint8_t* smpl = nullptr;
    size_t smplN = 0;
    RiffChunk ph{}, pb{}, pg{}, in{}, ib{}, ig{}, sh{};
    RiffIter it = children(root);
    RiffChunk c;
    while (it.next(c)) {
        if (!c.isList()) continue;
        RiffIter li = children(c);
        RiffChunk x;
        while (li.next(x)) {
            if (c.form == fourcc("INFO") && x.id == fourcc("INAM"))
                col.name = std::string(reinterpret_cast<const char*>(x.data), strnlen(reinterpret_cast<const char*>(x.data), x.size));
            else if (c.form == fourcc("sdta") && x.id == fourcc("smpl")) {
                smpl = x.data;
                smplN = x.size / 2;
            } else if (c.form == fourcc("pdta")) {
                if (x.id == fourcc("phdr")) ph = x;
                else if (x.id == fourcc("pbag")) pb = x;
                else if (x.id == fourcc("pgen")) pg = x;
                else if (x.id == fourcc("inst")) in = x;
                else if (x.id == fourcc("ibag")) ib = x;
                else if (x.id == fourcc("igen")) ig = x;
                else if (x.id == fourcc("shdr")) sh = x;
            }
        }
    }
    if (!smpl || !ph.data || !pb.data || !pg.data || !in.data || !ib.data || !ig.data || !sh.data) {
        err = "incomplete SoundFont";
        return false;
    }
    std::vector<Shdr> shdrs;
    for (size_t i = 0; i + 46 <= sh.size; i += 46) {
        const uint8_t* p = sh.data + i;
        Shdr s;
        s.start = readU32LE(p + 20);
        s.end = readU32LE(p + 24);
        s.loopStart = readU32LE(p + 28);
        s.loopEnd = readU32LE(p + 32);
        s.rate = readU32LE(p + 36);
        s.pitch = p[40];
        s.correction = int8_t(p[41]);
        s.type = uint16_t(p[44] | (p[45] << 8));
        shdrs.push_back(s);
    }
    const size_t pbagN = pb.size / 4, pgenN = pg.size / 4, ibagN = ib.size / 4, igenN = ig.size / 4;
    const size_t nInst = in.size / 22;
    std::map<int, uint32_t> waveOf;  // shdr index -> wave
    auto wave = [&](int si) -> int {
        auto f = waveOf.find(si);
        if (f != waveOf.end()) return int(f->second);
        const Shdr& s = shdrs[size_t(si)];
        DlsWave w;
        w.rate = s.rate ? s.rate : 22050;
        const uint32_t a = std::min<uint32_t>(s.start, uint32_t(smplN)), b = std::min<uint32_t>(s.end, uint32_t(smplN));
        for (uint32_t k = a; k < b; ++k) w.samples.push_back(int16_t(smpl[k * 2] | (smpl[k * 2 + 1] << 8)));
        col.waves.push_back(std::move(w));
        waveOf[si] = uint32_t(col.waves.size() - 1);
        return int(col.waves.size() - 1);
    };
    const size_t nPresets = ph.size / 38;
    for (size_t pi = 0; pi + 1 < nPresets; ++pi) {
        const uint8_t* p = ph.data + pi * 38;
        const uint8_t* q = p + 38;
        DlsInstrument out;
        out.name = std::string(reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), 20));
        const uint16_t program = uint16_t(p[20] | (p[21] << 8)), bank = uint16_t(p[22] | (p[23] << 8));
        out.program = program & 0x7F;
        out.bank = bank >= 128 ? 0x80000000u : (uint32_t(bank & 0x7F) << 8);
        const size_t b0 = uint16_t(p[24] | (p[25] << 8)), b1 = uint16_t(q[24] | (q[25] << 8));
        std::vector<Zone> pz = readZones(pb.data, pbagN, pg.data, pgenN, b0, b1);
        Zone pglobal;
        size_t pstart = 0;
        if (!pz.empty() && !pz[0].set[gInstrument]) {
            pglobal = pz[0];
            pstart = 1;
        }
        for (size_t z = pstart; z < pz.size(); ++z) {
            Zone P = pglobal;
            for (int g = 0; g < gCount; ++g)
                if (pz[z].set[g]) {
                    P.v[g] = pz[z].v[g];
                    P.set[g] = true;
                }
            if (!P.set[gInstrument]) continue;
            const size_t ii = uint16_t(P.v[gInstrument]);
            if (ii + 1 >= nInst + 1 || ii + 1 > nInst) continue;
            const uint8_t* ip = in.data + ii * 22;
            const size_t ib0 = uint16_t(ip[20] | (ip[21] << 8));
            const size_t ib1 = ii + 1 < nInst ? uint16_t(ip[42] | (ip[43] << 8)) : ibagN;
            std::vector<Zone> iz = readZones(ib.data, ibagN, ig.data, igenN, ib0, ib1);
            Zone iglobal;
            size_t istart = 0;
            if (!iz.empty() && !iz[0].set[gSampleId]) {
                iglobal = iz[0];
                istart = 1;
            }
            for (size_t k = istart; k < iz.size(); ++k) {
                Zone I = iglobal;
                for (int g = 0; g < gCount; ++g)
                    if (iz[k].set[g]) {
                        I.v[g] = iz[k].v[g];
                        I.set[g] = true;
                    }
                if (!I.set[gSampleId] || size_t(uint16_t(I.v[gSampleId])) >= shdrs.size()) continue;
                auto val = [&](int g) -> int {
                    int v = I.set[g] ? I.v[g] : defaultOf(g);
                    if (P.set[g] && g != gKeyRange && g != gVelRange && g != gSampleId && g != gSampleModes &&
                        g != gExclusiveClass && g != gRootKey && g > gEndAddrCoarse - 9)
                        v += P.v[g];
                    return v;
                };
                const int ikr = I.set[gKeyRange] ? I.v[gKeyRange] : defaultOf(gKeyRange);
                const int pkr = P.set[gKeyRange] ? P.v[gKeyRange] : defaultOf(gKeyRange);
                const int ivr = I.set[gVelRange] ? I.v[gVelRange] : defaultOf(gVelRange);
                const int pvr = P.set[gVelRange] ? P.v[gVelRange] : defaultOf(gVelRange);
                DlsRegion rg;
                rg.keyLo = uint16_t(std::max(rangeLo(int16_t(ikr)), rangeLo(int16_t(pkr))));
                rg.keyHi = uint16_t(std::min(rangeHi(int16_t(ikr)), rangeHi(int16_t(pkr))));
                rg.velLo = uint16_t(std::max(rangeLo(int16_t(ivr)), rangeLo(int16_t(pvr))));
                rg.velHi = uint16_t(std::min(rangeHi(int16_t(ivr)), rangeHi(int16_t(pvr))));
                if (rg.keyLo > rg.keyHi || rg.velLo > rg.velHi) continue;
                if (rg.velLo == 0 && rg.velHi == 0) rg.velHi = 127;
                rg.options = 1;
                rg.keyGroup = uint16_t(val(gExclusiveClass));
                const int si = uint16_t(I.v[gSampleId]);
                const Shdr& s = shdrs[size_t(si)];
                rg.waveIndex = uint32_t(wave(si));
                rg.hasWsmp = true;
                const int root = val(gRootKey) >= 0 && val(gRootKey) <= 127 ? val(gRootKey) : s.pitch;
                rg.wsmp.unityNote = uint16_t(root > 127 ? 60 : root);
                rg.wsmp.fineTune = int16_t(val(gCoarseTune) * 100 + val(gFineTune) + s.correction);
                // Initial attenuation in centibels, × 0.4 as SoundFont players (FluidSynth and the
                // E-mu hardware the format comes from) apply it.
                rg.wsmp.attenuation = -int32_t(std::lround(0.4 * std::max(0, val(gAttenuation)) * 65536.0));
                const int modes = val(gSampleModes) & 3;
                const int64_t ls = int64_t(s.loopStart) + val(gStartLoop) + 32768LL * val(gStartLoopCoarse) - s.start;
                const int64_t le = int64_t(s.loopEnd) + val(gEndLoop) + 32768LL * val(gEndLoopCoarse) - s.start;
                const int64_t n = int64_t(col.waves[rg.waveIndex].samples.size());
                if ((modes == 1 || modes == 3) && ls >= 0 && le > ls && le <= n) {
                    rg.wsmp.looped = true;
                    rg.wsmp.loopStart = uint32_t(ls);
                    rg.wsmp.loopLength = uint32_t(le - ls);
                }
                rg.hasArt = true;
                auto add = [&](uint16_t src, uint16_t ctl, uint16_t dst, int32_t scale) {
                    Connection k;
                    k.source = src;
                    k.control = ctl;
                    k.destination = dst;
                    k.scale = scale;
                    rg.art.conns.push_back(k);
                };
                const int kd = val(gKeyToVolEnvDecay);
                add(0, 0, 0x206, val(gAttackVolEnv) * 65536);
                // DLS level 1 has no hold stage: the hold time is added to the decay (drum kits hold
                // at full level for seconds, then fall to a silent sustain).
                int decayTc = val(gDecayVolEnv);
                if (val(gHoldVolEnv) > -12000) {
                    const double secs = std::pow(2.0, val(gHoldVolEnv) / 1200.0) + std::pow(2.0, decayTc / 1200.0);
                    decayTc = int(std::lround(1200.0 * std::log2(secs)));
                }
                add(0, 0, 0x207, (decayTc + 60 * kd) * 65536);
                if (kd) add(3, 0, 0x207, -kd * 127 * 65536);
                {
                    const int sus = std::clamp(1000 - val(gSustainVolEnv) * 1000 / 960, 0, 1000);
                    add(0, 0, 0x20A, sus << 16);
                }
                add(0, 0, 0x209, val(gReleaseVolEnv) * 65536);
                add(0, 0, 0x004, std::clamp(val(gPan), -500, 500) << 16);
                if (val(gModEnvToPitch)) {
                    add(0, 0, 0x30A, val(gAttackModEnv) * 65536);
                    add(0, 0, 0x30B, val(gDecayModEnv) * 65536);
                    add(0, 0, 0x30E, std::clamp(1000 - val(gSustainModEnv), 0, 1000) << 16);
                    add(0, 0, 0x30D, val(gReleaseModEnv) * 65536);
                    add(5, 0, 0x003, val(gModEnvToPitch) << 16);
                }
                const bool vib = val(gVibLfoToPitch) != 0;
                const bool mod = val(gModLfoToPitch) != 0 || val(gModLfoToVolume) != 0;
                if (vib || mod) {
                    add(0, 0, 0x104, (vib ? val(gFreqVibLfo) : val(gFreqModLfo)) * 65536);
                    add(0, 0, 0x105, (vib ? val(gDelayVibLfo) : val(gDelayModLfo)) * 65536);
                    add(1, 0, 0x003, (vib ? val(gVibLfoToPitch) : val(gModLfoToPitch)) << 16);
                    if (val(gModLfoToVolume)) add(1, 0, 0x001, -val(gModLfoToVolume) * 65536);
                }
                out.regions.push_back(rg);
            }
        }
        if (!out.regions.empty()) col.instruments.push_back(std::move(out));
    }
    return !col.instruments.empty() || (err = "no instruments", false);
}

}  // namespace music
}  // namespace cc
