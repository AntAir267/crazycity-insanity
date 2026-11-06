// cctool gmtrim <in.sf2> <out.sf2> [rate]
//
// Makes the bundled General MIDI fallback (directmusic.md, "General MIDI instruments": the
// player's own gm.dls, otherwise a bundled open bank; Ant chose FluidR3 GM, MIT) from the full
// FluidR3_GM.sf2: keeps only the standard drum kit (bank 128, preset 0) and the acoustic grand
// piano (bank 0, preset 0), mixes each linked stereo pair into one mono sample, and resamples to
// the music synthesiser's rate (22,050 Hz; it cannot reproduce anything above 11 kHz anyway) with
// a windowed-sinc filter. Modulators are dropped (the synthesiser is DLS level 1). The result is
// a valid SoundFont 2 file the engine's SF2 reader loads.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

struct Rec {
    std::vector<uint8_t> b;
};

uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

struct Gen {
    uint16_t op;
    uint16_t amount;
};
struct Zone {
    std::vector<Gen> gens;
    int find(uint16_t op) const {
        for (size_t i = 0; i < gens.size(); ++i)
            if (gens[i].op == op) return int(i);
        return -1;
    }
};
struct Shdr {
    char name[20];
    uint32_t start, end, loopStart, loopEnd, rate;
    uint8_t pitch;
    int8_t correction;
    uint16_t link, type;
};

std::vector<Zone> zonesOf(const uint8_t* bag, size_t bagN, const uint8_t* gen, size_t genN, size_t b0, size_t b1) {
    std::vector<Zone> out;
    for (size_t b = b0; b < b1 && b + 1 < bagN; ++b) {
        Zone z;
        const size_t g0 = rd16(bag + b * 4), g1 = rd16(bag + (b + 1) * 4);
        for (size_t g = g0; g < g1 && g < genN; ++g) z.gens.push_back({rd16(gen + g * 4), rd16(gen + g * 4 + 2)});
        out.push_back(z);
    }
    return out;
}

// Windowed-sinc resampling of x (looped over [ls, le) when le > ls) by ratio = out/in.
std::vector<int16_t> resample(const std::vector<float>& x, double ratio, uint32_t ls, uint32_t le, bool looped, size_t outLen) {
    std::vector<int16_t> y(outLen);
    const int half = 32;  // taps each side
    const double fc = std::min(1.0, ratio) * 0.95 * 0.5;  // cutoff (cycles per input sample)
    const long n = long(x.size());
    auto at = [&](long i) -> double {
        if (i < 0) return 0.0;
        if (looped && i >= long(le)) i = long(ls) + (i - long(ls)) % long(le - ls);
        if (i >= n) return 0.0;
        return x[size_t(i)];
    };
    const double kPi = 3.14159265358979323846;
    for (size_t k = 0; k < outLen; ++k) {
        const double t = double(k) / ratio;  // input position
        const long c = long(std::floor(t));
        double acc = 0, wsum = 0;
        for (long i = c - half + 1; i <= c + half; ++i) {
            const double d = t - double(i);
            const double arg = 2.0 * fc * d;
            const double sinc = std::fabs(arg) < 1e-12 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
            const double wpos = (d + half) / (2.0 * half);  // 0..1 across the window
            double w = 0;
            if (wpos > 0 && wpos < 1)
                w = 0.35875 - 0.48829 * std::cos(2 * kPi * wpos) + 0.14128 * std::cos(4 * kPi * wpos) -
                    0.01168 * std::cos(6 * kPi * wpos);
            const double h = 2.0 * fc * sinc * w;
            acc += at(i) * h;
            wsum += h;
        }
        if (wsum != 0) acc /= wsum;  // unity gain at DC
        y[k] = int16_t(std::clamp(std::lrint(acc), -32768L, 32767L));
    }
    return y;
}

void put16(std::vector<uint8_t>& o, uint16_t v) {
    o.push_back(uint8_t(v));
    o.push_back(uint8_t(v >> 8));
}
void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i)));
}
void putName(std::vector<uint8_t>& o, const char* s, size_t n = 20) {
    for (size_t i = 0; i < n; ++i) o.push_back(i < std::strlen(s) ? uint8_t(s[i]) : 0);
}
std::vector<uint8_t> chunk(const char* id, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> o(id, id + 4);
    put32(o, uint32_t(data.size()));
    o.insert(o.end(), data.begin(), data.end());
    if (data.size() & 1) o.push_back(0);
    return o;
}
std::vector<uint8_t> list(const char* form, const std::vector<std::vector<uint8_t>>& parts, const char* id = "LIST") {
    std::vector<uint8_t> body(form, form + 4);
    for (const auto& p : parts) body.insert(body.end(), p.begin(), p.end());
    std::vector<uint8_t> o(id, id + 4);
    put32(o, uint32_t(body.size()));
    o.insert(o.end(), body.begin(), body.end());
    return o;
}
std::vector<uint8_t> zstr(const std::string& s) {
    std::vector<uint8_t> v(s.begin(), s.end());
    v.push_back(0);
    if (v.size() & 1) v.push_back(0);
    return v;
}

}  // namespace

int cmdGmTrim(const std::string& inPath, const std::string& outPath, int rate) {
    std::FILE* f = std::fopen(inPath.c_str(), "rb");
    if (!f) {
        std::fprintf(stderr, "cannot open %s\n", inPath.c_str());
        return 1;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> b(size_t(size > 0 ? size : 0));
    const bool ok = std::fread(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    if (!ok || b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "sfbk", 4)) {
        std::fprintf(stderr, "%s: not a SoundFont 2 file\n", inPath.c_str());
        return 1;
    }
    // Find the chunks.
    const uint8_t* smpl = nullptr;
    size_t smplN = 0;
    std::map<std::string, std::pair<const uint8_t*, size_t>> pd;
    for (size_t off = 12; off + 8 <= b.size();) {
        const std::string id(reinterpret_cast<const char*>(&b[off]), 4);
        const uint32_t sz = rd32(&b[off + 4]);
        if (id == "LIST" && off + 12 <= b.size()) {
            const std::string form(reinterpret_cast<const char*>(&b[off + 8]), 4);
            for (size_t o2 = off + 12; o2 + 8 <= off + 8 + sz && o2 + 8 <= b.size();) {
                const std::string id2(reinterpret_cast<const char*>(&b[o2]), 4);
                const uint32_t s2 = rd32(&b[o2 + 4]);
                if (form == "sdta" && id2 == "smpl") {
                    smpl = &b[o2 + 8];
                    smplN = s2 / 2;
                } else if (form == "pdta") {
                    pd[id2] = {&b[o2 + 8], s2};
                }
                o2 += 8 + s2 + (s2 & 1);
            }
        }
        off += 8 + sz + (sz & 1);
    }
    for (const char* need : {"phdr", "pbag", "pgen", "inst", "ibag", "igen", "shdr"})
        if (!pd.count(need) || !smpl) {
            std::fprintf(stderr, "%s: incomplete SoundFont (%s)\n", inPath.c_str(), need);
            return 1;
        }
    const uint8_t* phdr = pd["phdr"].first;
    const size_t nPh = pd["phdr"].second / 38;
    const uint8_t* pbag = pd["pbag"].first;
    const size_t nPb = pd["pbag"].second / 4;
    const uint8_t* pgen = pd["pgen"].first;
    const size_t nPg = pd["pgen"].second / 4;
    const uint8_t* inst = pd["inst"].first;
    const size_t nIn = pd["inst"].second / 22;
    const uint8_t* ibag = pd["ibag"].first;
    const size_t nIb = pd["ibag"].second / 4;
    const uint8_t* igen = pd["igen"].first;
    const size_t nIg = pd["igen"].second / 4;
    std::vector<Shdr> sh;
    for (size_t i = 0; i < pd["shdr"].second / 46; ++i) {
        const uint8_t* p = pd["shdr"].first + i * 46;
        Shdr s;
        std::memcpy(s.name, p, 20);
        s.name[19] = 0;
        s.start = rd32(p + 20);
        s.end = rd32(p + 24);
        s.loopStart = rd32(p + 28);
        s.loopEnd = rd32(p + 32);
        s.rate = rd32(p + 36);
        s.pitch = p[40];
        s.correction = int8_t(p[41]);
        s.link = rd16(p + 42);
        s.type = rd16(p + 44);
        sh.push_back(s);
    }

    struct OutPreset {
        std::string name;
        uint16_t preset, bank;
        std::vector<Zone> zones;
    };
    struct OutInst {
        std::string name;
        std::vector<Zone> zones;
    };
    std::vector<OutPreset> presets;
    std::vector<OutInst> insts;
    std::map<int, int> instMap;    // old -> new
    std::map<int, int> sampleMap;  // old (left or mono) -> new
    std::vector<int> sampleOrder;  // old indices in new order
    std::map<int, int> partner;    // left sample -> its right sample
    const std::pair<int, int> keep[] = {{0, 0}, {128, 0}};
    for (size_t pi = 0; pi + 1 < nPh; ++pi) {
        const uint8_t* p = phdr + pi * 38;
        const int prog = rd16(p + 20), bank = rd16(p + 22);
        bool want = false;
        for (const auto& k : keep) want = want || (k.first == bank && k.second == prog);
        if (!want) continue;
        OutPreset op;
        char nm[21] = {};
        std::memcpy(nm, p, 20);
        op.name = nm;
        op.preset = uint16_t(prog);
        op.bank = uint16_t(bank);
        op.zones = zonesOf(pbag, nPb, pgen, nPg, rd16(p + 24), rd16(p + 38 + 24));
        for (Zone& z : op.zones) {
            const int gi = z.find(41);
            if (gi < 0) continue;
            const int old = z.gens[size_t(gi)].amount;
            if (!instMap.count(old)) {
                instMap[old] = int(insts.size());
                OutInst oi;
                char in2[21] = {};
                std::memcpy(in2, inst + size_t(old) * 22, 20);
                oi.name = in2;
                const size_t b0 = rd16(inst + size_t(old) * 22 + 20);
                const size_t b1 = size_t(old) + 1 < nIn ? rd16(inst + (size_t(old) + 1) * 22 + 20) : nIb - 1;
                std::vector<Zone> zs = zonesOf(ibag, nIb, igen, nIg, b0, b1);
                // Stereo pairs: FluidR3 leaves the sample links empty, so a right-channel zone is
                // paired with the left-channel zone of the same key and velocity range.
                auto range = [](const Zone& z, uint16_t op) {
                    const int i = z.find(op);
                    return i >= 0 ? int(z.gens[size_t(i)].amount) : -1;
                };
                std::vector<bool> skip(zs.size(), false);
                std::vector<bool> pairedLeft(zs.size(), false);
                for (size_t r = 0; r < zs.size(); ++r) {
                    const int rs = zs[r].find(53);
                    if (rs < 0 || !(sh[zs[r].gens[size_t(rs)].amount].type & 2)) continue;
                    for (size_t l = 0; l < zs.size(); ++l) {
                        const int lsI = zs[l].find(53);
                        if (lsI < 0 || pairedLeft[l] || !(sh[zs[l].gens[size_t(lsI)].amount].type & 4)) continue;
                        if (range(zs[l], 43) != range(zs[r], 43) || range(zs[l], 44) != range(zs[r], 44)) continue;
                        pairedLeft[l] = true;
                        skip[r] = true;
                        partner[zs[l].gens[size_t(lsI)].amount] = zs[r].gens[size_t(rs)].amount;
                        break;
                    }
                }
                for (size_t zi = 0; zi < zs.size(); ++zi) {
                    Zone& iz = zs[zi];
                    const int si = iz.find(53);
                    if (si < 0) {
                        oi.zones.push_back(iz);  // global zone
                        continue;
                    }
                    if (skip[zi]) continue;  // mixed into its left channel
                    const int s = iz.gens[size_t(si)].amount;
                    if (pairedLeft[zi]) {
                        // The pair becomes one centred mono sample: drop the zone's pan.
                        const int pan = iz.find(17);
                        if (pan >= 0) iz.gens.erase(iz.gens.begin() + pan);
                    }
                    if (!sampleMap.count(s)) {
                        sampleMap[s] = int(sampleOrder.size());
                        sampleOrder.push_back(s);
                    }
                    const int si2 = iz.find(53);
                    iz.gens[size_t(si2)].amount = uint16_t(sampleMap[s]);
                    oi.zones.push_back(iz);
                }
                insts.push_back(oi);
            }
            z.gens[size_t(gi)].amount = uint16_t(instMap[old]);
        }
        presets.push_back(op);
    }
    if (presets.size() != 2) {
        std::fprintf(stderr, "%s: drum kit or piano preset not found\n", inPath.c_str());
        return 1;
    }

    // Samples: mix pairs, resample, lay out with 46 zero frames after each.
    std::vector<int16_t> outPcm;
    std::vector<Shdr> outSh;
    size_t inBytes = 0;
    for (int old : sampleOrder) {
        const Shdr& S = sh[size_t(old)];
        const Shdr* R = partner.count(old) ? &sh[size_t(partner[old])] : nullptr;
        const uint32_t len = std::min<uint32_t>(S.end, uint32_t(smplN)) - std::min<uint32_t>(S.start, uint32_t(smplN));
        std::vector<float> x(len);
        for (uint32_t i = 0; i < len; ++i) {
            float v = float(int16_t(rd16(smpl + size_t(S.start + i) * 2)));
            if (R && R->start + i < R->end && R->start + i < smplN) v = 0.5f * (v + float(int16_t(rd16(smpl + size_t(R->start + i) * 2))));
            x[i] = v;
        }
        inBytes += size_t(len) * 2 * (R ? 2 : 1);
        const bool looped = S.loopEnd > S.loopStart && S.loopEnd <= S.end && S.loopStart >= S.start;
        const uint32_t ls = looped ? S.loopStart - S.start : 0, le = looped ? S.loopEnd - S.start : 0;
        const double ratio = S.rate > uint32_t(rate) ? double(rate) / double(S.rate) : 1.0;
        Shdr o = S;
        o.rate = ratio < 1.0 ? uint32_t(rate) : S.rate;
        const size_t outLen = size_t(std::floor(double(len) * ratio));
        std::vector<int16_t> y;
        if (ratio < 1.0) {
            y = resample(x, ratio, ls, le, looped, outLen);
        } else {
            y.resize(len);
            for (uint32_t i = 0; i < len; ++i) y[i] = int16_t(std::clamp(std::lrint(x[i]), -32768L, 32767L));
        }
        o.start = uint32_t(outPcm.size());
        o.end = o.start + uint32_t(y.size());
        if (looped) {
            uint32_t nls = uint32_t(std::lrint(double(ls) * ratio));
            uint32_t nle = uint32_t(std::lrint(double(le) * ratio));
            nle = std::min<uint32_t>(nle, uint32_t(y.size()));
            if (nle < nls + 8) nls = nle > 8 ? nle - 8 : 0;
            o.loopStart = o.start + nls;
            o.loopEnd = o.start + nle;
        } else {
            o.loopStart = o.start;
            o.loopEnd = o.start;
        }
        o.link = 0;
        o.type = 1;
        if (R) {  // "Crsh 1(L)" + "Crsh 1(R)" -> "Crsh 1"
            std::string n(o.name);
            const size_t at = n.rfind("(L)");
            if (at != std::string::npos) n.erase(at, 3);
            while (!n.empty() && n.back() == ' ') n.pop_back();
            std::memset(o.name, 0, sizeof o.name);
            std::memcpy(o.name, n.c_str(), std::min<size_t>(n.size(), 19));
        }
        outPcm.insert(outPcm.end(), y.begin(), y.end());
        outPcm.insert(outPcm.end(), 46, int16_t(0));
        outSh.push_back(o);
    }

    // pdta records.
    std::vector<uint8_t> PH, PB, PM, PG, IN, IB, IM, IG, SH;
    uint16_t bagN = 0, genN = 0;
    for (const OutPreset& p : presets) {
        putName(PH, p.name.c_str());
        put16(PH, p.preset);
        put16(PH, p.bank);
        put16(PH, bagN);
        put32(PH, 0);
        put32(PH, 0);
        put32(PH, 0);
        for (const Zone& z : p.zones) {
            put16(PB, genN);
            put16(PB, 0);
            for (const Gen& g : z.gens) {
                put16(PG, g.op);
                put16(PG, g.amount);
                ++genN;
            }
            ++bagN;
        }
    }
    putName(PH, "EOP");
    put16(PH, 0);
    put16(PH, 0);
    put16(PH, bagN);
    put32(PH, 0);
    put32(PH, 0);
    put32(PH, 0);
    put16(PB, genN);
    put16(PB, 0);
    for (int i = 0; i < 10; ++i) PM.push_back(0);
    put32(PG, 0);
    bagN = 0;
    genN = 0;
    for (const OutInst& in : insts) {
        putName(IN, in.name.c_str());
        put16(IN, bagN);
        for (const Zone& z : in.zones) {
            put16(IB, genN);
            put16(IB, 0);
            for (const Gen& g : z.gens) {
                put16(IG, g.op);
                put16(IG, g.amount);
                ++genN;
            }
            ++bagN;
        }
    }
    putName(IN, "EOI");
    put16(IN, bagN);
    put16(IB, genN);
    put16(IB, 0);
    for (int i = 0; i < 10; ++i) IM.push_back(0);
    put32(IG, 0);
    for (const Shdr& s : outSh) {
        putName(SH, s.name);
        put32(SH, s.start);
        put32(SH, s.end);
        put32(SH, s.loopStart);
        put32(SH, s.loopEnd);
        put32(SH, s.rate);
        SH.push_back(s.pitch);
        SH.push_back(uint8_t(s.correction));
        put16(SH, s.link);
        put16(SH, s.type);
    }
    putName(SH, "EOS");
    for (int i = 0; i < 26; ++i) SH.push_back(0);

    std::vector<uint8_t> ifil;
    put16(ifil, 2);
    put16(ifil, 1);
    std::vector<uint8_t> pcm;
    for (int16_t v : outPcm) put16(pcm, uint16_t(v));
    std::vector<uint8_t> info = list("INFO", {chunk("ifil", ifil), chunk("isng", zstr("EMU8000")),
                                               chunk("INAM", zstr("FluidR3 GM: Standard drum kit and Yamaha Grand Piano")),
                                               chunk("ICOP", zstr("Copyright (c) 2000-2002, 2008 Frank Wen. MIT licence (COPYING-FluidR3.txt)")),
                                               chunk("ICMT", zstr("Trimmed from FluidR3_GM.sf2 for CrazyCity Insanity: bank 128 preset 0 and "
                                                                  "bank 0 preset 0 only, stereo pairs mixed to mono, resampled to " +
                                                                  std::to_string(rate) + " Hz.")),
                                               chunk("ISFT", zstr("cctool gmtrim"))});
    std::vector<uint8_t> sdta = list("sdta", {chunk("smpl", pcm)});
    std::vector<uint8_t> pdta = list("pdta", {chunk("phdr", PH), chunk("pbag", PB), chunk("pmod", PM), chunk("pgen", PG),
                                               chunk("inst", IN), chunk("ibag", IB), chunk("imod", IM), chunk("igen", IG),
                                               chunk("shdr", SH)});
    std::vector<uint8_t> file = list("sfbk", {info, sdta, pdta}, "RIFF");
    std::FILE* o = std::fopen(outPath.c_str(), "wb");
    if (!o || std::fwrite(file.data(), 1, file.size(), o) != file.size()) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        if (o) std::fclose(o);
        return 1;
    }
    std::fclose(o);
    std::printf("gmtrim: %zu presets, %zu instruments, %zu samples (%.1f MB of input sample data) -> %s, %.2f MB\n",
                presets.size(), insts.size(), outSh.size(), double(inBytes) / 1048576.0, outPath.c_str(),
                double(file.size()) / 1048576.0);
    return 0;
}
