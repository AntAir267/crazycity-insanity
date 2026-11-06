// Native music checks (milestone "Music"): file survey against the spec's "Checked against"
// numbers, pattern-choice traces, WAV renders, and scripted game-rule scenarios.
//
//   cctool music survey <archive.ar>...
//   cctool music trace <segment> <seconds> <archive.ar>...
//   cctool music render <segment> <seconds> <out.wav> [--gm <bank.dls|.sf2>] <archive.ar>...
//   cctool music scenario <archive.ar>...
//   cctool music all <outdir> <archive.ar>...     (survey + scenario + the two renders)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/strutil.h"
#include "music/composer.h"
#include "music/dm_files.h"
#include "music/dm_loader.h"
#include "music/music_system.h"
#include "music/performance.h"
#include "music/synth.h"
#include "vfs/vfs.h"

bool mountArchivesForTool(cc::Vfs& vfs, const std::vector<std::string>& archives);

namespace cc {
namespace {

using namespace music;

struct Checker {
    int problems = 0;
    void expect(bool cond, const std::string& what) {
        std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
        if (!cond) ++problems;
    }
};

FileReader vfsReader(const Vfs& vfs) {
    return [&vfs](const std::string& path) { return vfs.open(path); };
}

bool writeWav(const std::string& path, const std::vector<int16_t>& stereo, int rate) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t dataBytes = uint32_t(stereo.size() * 2);
    auto u32 = [&](uint32_t v) {
        uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        std::fwrite(b, 1, 4, f);
    };
    auto u16 = [&](uint16_t v) {
        uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
        std::fwrite(b, 1, 2, f);
    };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1);
    u16(2);
    u32(uint32_t(rate));
    u32(uint32_t(rate) * 4);
    u16(4);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (int16_t s : stereo) u16(uint16_t(s));
    return std::fclose(f) == 0;
}

DlsPtr loadBankFile(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return nullptr;
    Bytes b;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    b.resize(size_t(n > 0 ? n : 0));
    bool ok = std::fread(b.data(), 1, b.size(), f) == b.size();
    std::fclose(f);
    if (!ok) return nullptr;
    auto col = std::make_shared<DlsCollection>();
    std::string err;
    const std::string form = riffForm(b);
    if (form == "sfbk" ? !parseSf2(b, *col, err) : !parseDls(b, *col, err)) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), err.c_str());
        return nullptr;
    }
    col->fileName = baseName(path);
    return col;
}

// ---- survey

int cmdSurvey(const Vfs& vfs) {
    Checker c;
    std::printf("music survey\n");
    std::map<std::string, int> kinds;
    int files = 0, broken = 0;
    std::vector<Segment> segs;
    std::vector<Style> styles;
    std::vector<DlsCollection> dls;
    std::vector<Band> bandFiles;
    std::vector<std::string> segNames;
    for (const std::string& path : vfs.listUnique("aud/dmusic/")) {
        const std::string rest = path.substr(std::string("aud/dmusic/").size());
        if (rest.find('/') != std::string::npos || rest.find('\\') != std::string::npos) continue;
        BytesPtr b = vfs.open(path);
        if (!b) continue;
        ++files;
        std::string err;
        const std::string ext = toLower(rest.substr(rest.size() >= 4 ? rest.size() - 4 : 0));
        bool ok = false;
        if (ext == ".sgt") {
            Segment s;
            ok = parseSegment(*b, s, err);
            s.fileName = rest;
            segs.push_back(std::move(s));
        } else if (ext == ".sty") {
            Style s;
            ok = parseStyle(*b, s, err);
            s.fileName = rest;
            styles.push_back(std::move(s));
        } else if (ext == ".dls") {
            DlsCollection d;
            ok = parseDls(*b, d, err);
            d.fileName = rest;
            dls.push_back(std::move(d));
        } else if (ext == ".bnd") {
            Band d;
            ok = parseBandFile(*b, d, err);
            bandFiles.push_back(std::move(d));
        }
        ++kinds[ext];
        if (!ok) {
            ++broken;
            std::printf("    %s: %s\n", rest.c_str(), err.c_str());
        }
    }
    c.expect(files == 95, strf("95 music files (%d)", files));
    c.expect(kinds[".sgt"] == 62, strf("62 segments (%d)", kinds[".sgt"]));
    c.expect(kinds[".sty"] == 17, strf("17 styles (%d)", kinds[".sty"]));
    c.expect(kinds[".dls"] == 14, strf("14 DLS collections (%d)", kinds[".dls"]));
    c.expect(kinds[".bnd"] == 2, strf("2 bands (%d)", kinds[".bnd"]));
    c.expect(broken == 0, strf("no file fails to parse (%d)", broken));

    // Tracks.
    int withFour = 0, cmdTracks = 0, segBands = 0;
    for (const Segment& s : segs) {
        if (s.hasStyleTrack && s.hasBandTrack && s.hasTempoTrack) ++withFour;
        if (s.hasCommandTrack) ++cmdTracks;
        segBands += int(s.bands.size());
    }
    c.expect(withFour == 62, strf("every segment has style, band and tempo tracks (%d)", withFour));
    c.expect(cmdTracks == 61, strf("61 segments with a command track (%d)", cmdTracks));
    c.expect(segBands == 83, strf("83 bands in the segments (%d)", segBands));

    // Styles.
    int parts = 0, patterns = 0, styleBands = 0, notes = 0, curves = 0, single = 0, several = 0, none = 0;
    int w3f = 0, w20 = 0, motifs = 0, endings = 0, normal = 0;
    for (const Style& s : styles) {
        parts += int(s.parts.size());
        patterns += int(s.patterns.size());
        styleBands += int(s.bands.size());
        for (const Part& p : s.parts) {
            notes += int(p.notes.size());
            curves += int(p.curves.size());
            for (uint32_t w : p.choices) {
                if (w == 0x3FFFFFFF) ++w3f;
                if (w == 0x20000000) ++w20;
            }
            const size_t n = allowedVariations(p).size();
            int strict = 0;
            for (int v = 0; v < 32; ++v) {
                uint32_t cw = p.choices[v] == 0xFFFFFFFFu ? 0x3FFFFFFFu : p.choices[v];
                const uint32_t m = (cw & (1u << 29)) ? 0x02200001u : 0x00002101u;
                if ((cw & m) == m) ++strict;
            }
            if (strict == 0) ++none;
            else if (n == 1) ++single;
            else ++several;
        }
        for (const Pattern& p : s.patterns) {
            if (p.embellishment == EmbMotif) ++motifs;
            else if (p.embellishment == EmbEnd) ++endings;
            else if (p.embellishment == 0) ++normal;
        }
    }
    c.expect(parts == 1786, strf("1,786 parts (%d)", parts));
    c.expect(patterns == 183, strf("183 patterns (%d)", patterns));
    c.expect(normal == 158 && endings == 14 && motifs == 11,
             strf("158 normal, 14 ending, 11 motif patterns (%d, %d, %d)", normal, endings, motifs));
    c.expect(styleBands == 31, strf("31 style bands (%d)", styleBands));
    c.expect(notes == 155817, strf("155,817 notes (%d)", notes));
    c.expect(curves == 3720, strf("3,720 curves (%d)", curves));
    c.expect(w3f == 3497 && w20 == 53655, strf("variation words 0x3FFFFFFF x3,497 and 0x20000000 x53,655 (%d, %d)", w3f, w20));
    c.expect(single == 1557 && several == 229 && none == 0,
             strf("1,557 parts with one allowed variation, 229 with several, none with none (%d, %d, %d)", single, several, none));

    // DLS.
    int instruments = 0, regions = 0, waves = 0, drums = 0;
    for (const DlsCollection& d : dls) {
        instruments += int(d.instruments.size());
        waves += int(d.waves.size());
        for (const DlsInstrument& i : d.instruments) {
            regions += int(i.regions.size());
            if (i.bank & 0x80000000u) ++drums;
        }
    }
    c.expect(instruments == 204, strf("204 DLS instruments (%d)", instruments));
    c.expect(regions == 381, strf("381 regions (%d)", regions));
    c.expect(waves == 368, strf("368 waves (%d)", waves));
    c.expect(drums == 0, "no drum instruments in MM2's collections");

    // References and band instruments.
    auto findDls = [&](const DmReference& r) -> const DlsCollection* {
        for (const DlsCollection& d : dls)
            if (iequals(d.fileName, r.file)) return &d;
        return nullptr;
    };
    auto findStyle = [&](const DmReference& r) -> const Style* {
        for (const Style& s : styles)
            if (iequals(s.fileName, r.file)) return &s;
        return nullptr;
    };
    int refs = 0, refsOk = 0, bandInst = 0, patchOk = 0, patchRefs = 0;
    auto bandRefs = [&](const Band& b) {
        for (const BandInstrument& bi : b.instruments) {
            ++bandInst;
            if (!bi.hasRef) continue;
            ++refs;
            const DlsCollection* d = findDls(bi.ref);
            if (d && d->hasGuid && d->guid == bi.ref.guid) ++refsOk;
            ++patchRefs;
            if (d && d->find(bi.patch)) ++patchOk;
        }
    };
    for (const Segment& s : segs) {
        for (const StyleRefEvent& e : s.styles) {
            ++refs;
            const Style* st = findStyle(e.ref);
            if (st && st->guid == e.ref.guid) ++refsOk;
        }
        for (const BandEvent& be : s.bands) bandRefs(*be.band);
    }
    for (const Style& s : styles)
        for (const BandPtr& b : s.bands) bandRefs(*b);
    for (const Band& b : bandFiles) bandRefs(b);
    // The spec's survey counts 3,216 (62 + 3,154); the files hold 1,865 LIST DMRF (QUESTIONS.md).
    c.expect(refs == 1865 && refsOk == refs, strf("1,865 references (62 style, 1,803 DLS), all found by file name with matching GUIDs (%d, %d)", refs, refsOk));
    c.expect(bandInst == 2073, strf("2,073 band instruments (%d)", bandInst));
    c.expect(patchOk == patchRefs, strf("every band patch with a collection reference exists in it (%d of %d)", patchOk, patchRefs));

    // Specific facts.
    for (const Segment& s : segs)
        if (iequals(s.fileName, "TacoLocoRestart.sgt"))
            c.expect(s.playStart == 239616 && s.length == 313344 && s.loopStart == 30720 && s.loopEnd == 313344 && s.repeats == 99,
                     strf("TacoLocoRestart starts at 239616 of 313344, loops 30720..313344, 99 repeats (%d of %d, %d..%d, %u)",
                          s.playStart, s.length, s.loopStart, s.loopEnd, s.repeats));
    for (const Style& s : styles)
        if (iequals(s.fileName, "GrooverStyle.sty")) {
            const Pattern* m = s.findMotif("BigAir");
            bool ok = m && m->lengthMeasures == 1 && m->parts.size() == 2;
            if (ok) {
                std::set<int> pch;
                for (const PartRef& r : m->parts) {
                    pch.insert(r.pchannel);
                    if (r.part < 0 || s.parts[size_t(r.part)].notes.size() != 1) ok = false;
                }
                ok = ok && pch == std::set<int>{30, 31};
            }
            c.expect(ok, "GrooverStyle's BigAir motif: 1 bar, PChannels 30 and 31, one note each");
            if (m) c.expect(m->hasMotif && m->motif.resolution == 0x1000, strf("BigAir starts on a beat (resolution 0x%x)", m ? m->motif.resolution : 0));
        }
    for (const Band& b : bandFiles)
        if (iequals(b.name, "BigAir")) {
            bool ok = b.instruments.size() == 2;
            std::set<int> pans, pch;
            for (const BandInstrument& bi : b.instruments) {
                pch.insert(int(bi.pchannel));
                pans.insert(bi.pan);
                if (bi.patch != ((1u << 8) | 24u) || !iequals(bi.ref.file, "DLS Collection1.dls")) ok = false;
            }
            c.expect(ok && pch == std::set<int>{30, 31} && pans == std::set<int>{0, 127},
                     "BigAir.bnd: PChannels 30 and 31, bank 1 program 24 of DLS Collection1, pans 0 and 127");
        }

    // Tables (music.md).
    {
        MusicSystem ms;
        ms.setFileReader(vfsReader(vfs));
        auto table = [&](const char* name, size_t rows, size_t cols) {
            BytesPtr b = vfs.open(std::string("aud/dmusic/csv_files/") + name + ".csv");
            std::vector<std::string> lines;
            std::string cur;
            if (b)
                for (uint8_t ch : *b) {
                    if (ch == '\n') {
                        lines.push_back(cur);
                        cur.clear();
                    } else if (ch != '\r') {
                        cur += char(ch);
                    }
                }
            if (!cur.empty()) lines.push_back(cur);
            bool ok = lines.size() == rows + 1;
            for (size_t i = 1; i < lines.size(); ++i) ok = ok && split(lines[i], ',').size() == cols;
            c.expect(ok, strf("%s: %zu rows x %zu columns", name, rows, cols));
        };
        table("singlerace", 5, 10);
        table("singleroam", 4, 9);
        table("ui", 1, 1);
        table("sfambience", 1, 1);
        table("londonambience", 1, 1);
    }

    // Music values (directmusic.md, "Music values").
    {
        const Subchord& sc = defaultChord().subchords[0];
        const std::pair<uint16_t, int> ex[] = {{0x3000, 36}, {0x3110, 41}, {0x210F, 27}, {0x4300, 59},
                                               {0x510F, 63}, {0x3400, 50}, {0x30F0, 35}};
        bool ok = true;
        std::string got;
        for (const auto& e : ex) {
            const int k = musicValueToKey(e.first, 0x0E, 0, 127, sc);
            got += strf(" %x->%d", e.first, k);
            if (k != e.second) ok = false;
        }
        c.expect(ok, "music value examples:" + got);
        std::set<std::tuple<int, int, int, int>> combos;
        bool inside = true;
        for (const Style& s : styles)
            for (const Part& p : s.parts)
                for (const Note& n : p.notes) {
                    const int pm = n.playMode == 0x10 ? p.playMode : n.playMode;
                    combos.insert({n.value, pm, p.invLower, p.invUpper});
                    bool outside = false;
                    musicValueToKey(n.value, uint8_t(pm), p.invLower, p.invUpper, sc, &outside);
                    if (outside) inside = false;
                }
        c.expect(combos.size() == 85 && inside, strf("85 distinct (value, play mode, inversion) combinations, all inside 0..127 (%zu)", combos.size()));
    }

    // Synthesiser constants.
    {
        DlsSynth syn(22050);
        const DlsSynth::ReverbInfo r = syn.reverbInfo();
        c.expect(r.n[0] == 715 && r.n[1] == 1126 && r.n[2] == 851 && r.n[3] == 1339 && r.c0 == 277 && r.c1 == 329,
                 strf("reverb delays 715 1126 851 1339, allpass 277 329 (%d %d %d %d, %d %d)", r.n[0], r.n[1], r.n[2], r.n[3], r.c0, r.c1));
        c.expect(r.dry == 31086 && r.wet == 7893 && r.a == 20252 && r.g0 == 30044 && r.g1 == 29559,
                 strf("reverb dry 31086 wet 7893 a 20252 g 30044/29559 (%d %d %d %d/%d)", r.dry, r.wet, r.a, r.g0, r.g1));
        bool taps = true;
        const int want[4] = {13096, 11514, 12550, 10771};
        for (int i = 0; i < 4; ++i) taps = taps && r.b0[i] == want[i] && r.b1[i] == want[i];
        c.expect(taps, strf("reverb taps 13096/11514/12550/10771 (%d/%d/%d/%d, b1 %d/%d/%d/%d)", r.b0[0], r.b0[1], r.b0[2], r.b0[3],
                            r.b1[0], r.b1[1], r.b1[2], r.b1[3]));
        auto V = [](int v) { return int(std::trunc(4000.0 * std::log10(double(v) / 127.0))); };
        auto P = [](int i) { return int(std::trunc(1000.0 * std::log10(double(i) / 127.0))); };
        c.expect(V(64) == -1190 && V(100) == -415 && P(64) == -297, "gain tables V[64] -1190, V[100] -415, P[64] -297");
        auto level = [](float m) {
            const float x = 200.0f * m;
            const long double g = std::log((long double)x) / std::log((long double)200.0);
            return double(std::trunc((g - 1.0L) * 10000.0L)) / 100.0;
        };
        // music.md's own example says 0.1 -> -56.54 dB; its formula gives -43.46 dB (QUESTIONS.md).
        c.expect(level(1.0f) == 0.0 && std::fabs(level(0.5f) + 13.08) < 0.006 && std::fabs(level(0.1f) + 43.46) < 0.011,
                 strf("music volume curve: 1 -> 0 dB, 0.5 -> %.2f dB, 0.1 -> %.2f dB", level(0.5f), level(0.1f)));
    }
    std::printf("%d problem(s)\n", c.problems);
    return c.problems;
}

// ---- traces and renders

struct Rig {
    MusicLibrary lib;
    DlsSynth synth{22050};
    Performance perf{synth};
    explicit Rig(const Vfs& vfs) : lib(vfsReader(vfs)) { perf.setSeed(1); }
};

int cmdTrace(const Vfs& vfs, const std::string& name, double seconds, std::vector<std::string>* out = nullptr) {
    Rig r(vfs);
    SegmentPtr seg = r.lib.loadSegment(name + ".sgt");
    if (!seg) {
        std::fprintf(stderr, "no segment %s\n", name.c_str());
        return 1;
    }
    r.lib.downloadSegment(*seg, r.synth);
    const int measure = seg->timeSig().measureTicks();
    r.perf.trace = [&](const PerformanceTrace& t) {
        if (t.kind != 0) return;
        const std::string line = strf("%8.2f m  %s", double(t.tick) / measure, t.pattern.c_str());
        if (out) out->push_back(t.pattern);
        else std::printf("  %s\n", line.c_str());
    };
    r.perf.playPrimary(seg, Boundary::Immediate);
    std::vector<int16_t> buf(4096);
    const int64_t frames = int64_t(seconds * 22050);
    for (int64_t done = 0; done < frames; done += 2048) r.perf.render(buf.data(), 2048);
    return 0;
}

int cmdRender(const Vfs& vfs, const std::string& name, double seconds, const std::string& outPath, DlsPtr gm) {
    Rig r(vfs);
    r.lib.setGmBank(gm);
    SegmentPtr seg = r.lib.loadSegment(name + ".sgt");
    if (!seg) {
        std::fprintf(stderr, "no segment %s\n", name.c_str());
        return 1;
    }
    r.lib.downloadSegment(*seg, r.synth);
    std::vector<std::string> pats;
    r.perf.trace = [&](const PerformanceTrace& t) {
        if (t.kind == 0) pats.push_back(t.pattern);
    };
    r.perf.playPrimary(seg, Boundary::Immediate);
    const int64_t frames = int64_t(seconds * 22050);
    std::vector<int16_t> pcm(size_t(frames) * 2);
    int peak = 0;
    for (int64_t done = 0; done < frames;) {
        const int n = int(std::min<int64_t>(1024, frames - done));
        r.perf.render(pcm.data() + size_t(done) * 2, n);
        done += n;
    }
    double sum = 0;
    for (int16_t s : pcm) {
        peak = std::max(peak, std::abs(int(s)));
        sum += double(s) * s;
    }
    if (!writeWav(outPath, pcm, 22050)) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 1;
    }
    std::string list;
    for (const std::string& p : pats) list += " " + p;
    std::printf("  %s: %.0f s -> %s (peak %d, rms %.0f, %llu notes, %llu without instrument or voice; GM %s: %d of %d "
                "instruments found)\n   patterns:%s\n",
                name.c_str(), seconds, outPath.c_str(), peak, std::sqrt(sum / double(pcm.size() ? pcm.size() : 1)),
                (unsigned long long)r.synth.notesStarted(), (unsigned long long)r.synth.notesDropped(),
                gm ? gm->fileName.c_str() : "none", r.lib.gmRequests() - r.lib.gmMissing(), r.lib.gmRequests(), list.c_str());
    for (const auto& kv : r.synth.missing())
        std::printf("   no instrument: PChannel %d patch 0x%x (%d notes)\n", int(kv.first >> 32), unsigned(kv.first & 0xFFFFFFFFu), kv.second);
    return 0;
}

// ---- game-rule scenarios

struct Scenario {
    MusicSystem ms;
    std::vector<std::string> log;
    std::vector<int16_t> buf = std::vector<int16_t>(8192);
    explicit Scenario(const Vfs& vfs, const std::string& hide = std::string()) {
        if (hide.empty()) ms.setFileReader(vfsReader(vfs));
        else
            ms.setFileReader([&vfs, hide](const std::string& path) -> BytesPtr {
                return iequals(path, hide) ? nullptr : vfs.open(path);
            });
        ms.setSeed(1);
        ms.setSoundRandom([] { return 0.0; });  // row 0
        ms.log = [this](const std::string& s) { log.push_back(s); };
    }
    // Runs game frames of 1/30 s for `secs`, rendering the audio in step.
    void run(double secs, float speed, int chase = 0, bool air = false) {
        const int frames = int(std::lround(secs * 30));
        for (int i = 0; i < frames; ++i) {
            MusicFrame f;
            f.F = 1.0f / 30.0f;
            f.speed = speed;
            f.chaseCount = chase;
            f.airborne = air;
            ms.frame(f);
            ms.render16(buf.data(), 735);
        }
    }
    void audio(double secs) {
        const int frames = int(secs * 22050);
        for (int d = 0; d < frames; d += 735) ms.render16(buf.data(), 735);
    }
    std::string playing() const { return ms.nowPlaying(); }
    bool logged(const std::string& s) const {
        for (const std::string& l : log)
            if (l == s) return true;
        return false;
    }
};

int cmdScenario(const Vfs& vfs) {
    Checker c;
    std::printf("music scenarios\n");
    {
        Scenario s(vfs);
        s.ms.setMode(MusicMode::Music);
        s.ms.startMenus();
        s.audio(1.0);
        c.expect(s.ms.currentSlotName() == "UI" && s.playing().find("UI") == 0, "menus play the ui table's segment at once: " + s.playing());
    }
    {
        Scenario s(vfs);
        s.ms.setMode(MusicMode::Music);
        GameMusicSetup g;
        g.race = false;
        g.mapName = "london";
        s.ms.startGame(g);
        c.expect(s.logged("game: singleroam row 0 (SunroofStart)"), "Cruise: singleroam row 0 (sound random 0)");
        s.run(1.2, 0.0f);
        c.expect(s.playing().empty() || s.playing().find("Sunroof") == std::string::npos, "nothing plays before 1.25 s");
        s.run(0.1, 0.0f);
        c.expect(s.logged("start SunroofStart"), "the start slot starts after 1.25 s");
        c.expect(s.logged("groove to SunRoofIdle"), "standing still at the start: groove transition to idle at once (timer 10000)");
        s.run(8.0, 0.0f);
        c.expect(s.ms.currentSlotName() == "SunRoofIdle" && s.playing().find("SunRoofIdle") == 0,
                 "idle music plays after the one-measure groove: " + s.playing());
        s.run(2.0, 20.0f);
        c.expect(s.ms.currentSlotName() == "SunRoofReturn", "driving off: groove transition to return");
        s.run(3.0, 20.0f);
        c.expect(s.playing().find("SunRoofReturn") == 0, "return music plays: " + s.playing());
        s.run(0.1, 20.0f, 1);
        c.expect(s.logged("cut to SunRoofCops"), "chase count 0 -> 1 cuts to the chase slot");
        s.run(2.0, 20.0f, 1);
        c.expect(s.playing().find("SunRoofCops") == 0, "chase music plays: " + s.playing());
        s.run(2.0, 2.0f, 1);
        c.expect(s.ms.currentSlotName() == "SunRoofCops", "Cruise has no idle-cop slot: a slow car keeps the chase music");
        s.run(0.5, 20.0f, 2);
        s.run(0.5, 20.0f, 0);
        c.expect(s.ms.currentSlotName() == "SunRoofCops", "2 -> 0 is not a 1 -> 0 change: the chase music stays");
        s.run(0.1, 20.0f, 1);
        s.run(0.1, 20.0f, 0);
        c.expect(s.ms.currentSlotName() == "SunRoofReturn", "1 -> 0 cuts to return");
        const size_t before = s.log.size();
        s.run(0.5, 20.0f, 0, true);
        s.run(0.2, 20.0f, 0, false);
        s.run(0.2, 20.0f, 0, true);
        int air = 0;
        for (size_t i = before; i < s.log.size(); ++i) air += s.log[i] == "big air";
        c.expect(air == 2, strf("big air plays the motif once per jump (%d for 2 jumps)", air));
        s.ms.paused();
        s.audio(1.0);
        c.expect(s.ms.currentSlotName() == "Pause" && s.playing().find("Pause") == 0, "pause cuts to the pause slot: " + s.playing());
        s.ms.resumed();
        s.audio(1.0);
        c.expect(s.ms.currentSlotName() == "SunRoofReturn" && s.playing().find("SunRoofReturn") == 0, "resume cuts back: " + s.playing());
    }
    {
        Scenario s(vfs);
        s.ms.setMode(MusicMode::Music);
        GameMusicSetup g;
        g.race = true;
        s.ms.startGame(g);
        c.expect(s.logged("game: singlerace row 0 (EnemyStart)"), "race: singlerace row 0");
        s.run(2.0, 0.0f);
        c.expect(s.ms.currentSlotName() == "EnemyStart", "race: idle blocked until GO");
        s.ms.raceGo();
        s.run(1.0, 0.0f);
        c.expect(s.ms.currentSlotName() == "EnemyStart", "after GO the blocked branch cleared the timer: no idle yet");
        s.run(5.5, 0.0f);
        c.expect(s.ms.currentSlotName() == "EnemyIdle", "5 s of slow driving after GO: idle");
        s.run(0.1, 0.0f, 1);
        c.expect(s.logged("cut to EnemyCops"), "chase count 0 -> 1 cuts to chase from idle");
        s.run(6.0, 2.0f, 1);
        c.expect(s.ms.currentSlotName() == "EnemyIdleCops", "slow during a race chase: idle-cop");
        s.run(0.5, 20.0f, 1);
        c.expect(s.ms.currentSlotName() == "EnemyCops", "faster again: back to chase");
        s.run(0.1, 20.0f, 0);
        s.ms.finalLap();
        c.expect(s.logged("ending to EnemyCops"), "final lap: ending transition to the chase slot");
        s.run(3.0, 20.0f);
        s.ms.finished();
        s.audio(0.5);
        s.ms.resultsOpened();
        s.audio(2.0);
        c.expect(s.ms.currentSlotName() == "Results1" && s.playing().find("Results1") == 0, "results: " + s.playing());
    }
    {
        Scenario s(vfs);
        s.ms.setMode(MusicMode::Ambient);
        GameMusicSetup g;
        g.mapName = "sf";
        s.ms.startGame(g);
        c.expect(s.logged("ambience: SFAMbience"), "ambient option in SF: SFAMbience");
        s.run(2.0, 0.0f);
        c.expect(s.playing().find("SFAmbience") == 0 || s.playing().find("SFAMbience") == 0, "ambience plays: " + s.playing());
        MusicFrame f;
        f.F = 1.0f / 30.0f;
        f.echo = true;
        s.ms.frame(f);
        s.audio(1.0);
        c.expect(s.playing().empty() || s.ms.performance().primaryNow() == nullptr, "ambience stops in a tunnel");
        f.echo = false;
        s.ms.frame(f);
        s.audio(1.0);
        c.expect(s.ms.performance().primaryNow() != nullptr, "and starts again outside");
    }
    {
        // Roam slots load in column order (music.md, step 2).
        Scenario s(vfs);
        s.ms.setMode(MusicMode::Music);
        GameMusicSetup g;
        s.ms.startGame(g);
        std::vector<std::string> loads;
        for (const std::string& l : s.log)
            if (l.rfind("load ", 0) == 0) loads.push_back(l.substr(5));
        const std::vector<std::string> want = {"SunroofStart", "SunRoofReturn", "SunRoofIdle", "SunRoofCops", "SunRoofIdle", "Pause"};
        std::string got;
        for (const std::string& l : loads) got += " " + l;
        c.expect(loads == want, "roam slots load in column order:" + got);
        // A cut back within 0.1 s starts nothing: the slot's segment is still playing at the present.
        s.run(1.3, 20.0f);
        s.run(3.0, 20.0f);
        c.expect(s.playing().find("SunroofStart") == 0 || s.playing().find("SunRoofStart") == 0, "start music plays: " + s.playing());
        s.run(1.0, 20.0f, 1);  // a chase, then back to the return music
        s.run(3.0, 20.0f, 0);
        c.expect(s.playing().find("SunRoofReturn") == 0, "return music plays: " + s.playing());
        s.run(1.0 / 30.0, 20.0f, 1);
        s.run(1.0 / 30.0, 20.0f, 0);
        c.expect(s.ms.currentSlotName() == "SunRoofReturn", "chase count 1 for one frame: cut to chase, cut back to return");
        s.run(2.0, 20.0f, 0);
        c.expect(s.playing().empty(), "a cut back to a slot within 0.1 s leaves silence (its segment was still playing): \"" +
                                          s.playing() + "\"");
    }
    {
        // A transition to an empty slot is a transition to nothing: one groove measure, then silence.
        Scenario s(vfs, "aud/dmusic/SunRoofIdle.sgt");
        s.ms.setMode(MusicMode::Music);
        GameMusicSetup g;
        s.ms.startGame(g);
        c.expect(s.logged("missing SunRoofIdle"), "the idle slot is empty (file hidden)");
        s.run(1.3, 0.0f);
        c.expect(s.logged("groove to SunRoofIdle"), "standing still: groove transition to the empty idle slot");
        s.run(8.0, 0.0f);
        c.expect(s.ms.currentSlotName() == "SunRoofIdle" && s.playing().empty(),
                 "after the groove measure the music is silent: \"" + s.playing() + "\"");
        s.run(0.1, 0.0f, 1);
        c.expect(!s.logged("cut to SunRoofCops"), "a cut with an empty current slot does nothing");
    }
    {
        // Stop with an ending when nothing plays stops every segment, the motif included.
        Scenario s(vfs);
        s.ms.setMode(MusicMode::Music);
        GameMusicSetup g;
        g.race = true;
        s.ms.startGame(g);
        s.run(1.0, 0.0f, 0, true);  // big air before the music starts: no motif (rules run only after the start)
        s.ms.wrecked();
        s.audio(0.5);
        c.expect(s.ms.performance().primaryNow() == nullptr, "a wreck before the music started leaves silence");
    }
    std::printf("%d problem(s)\n", c.problems);
    return c.problems;
}

}  // namespace

}  // namespace cc

int cmdMusic(const std::vector<std::string>& argsIn) {
    using namespace cc;
    using namespace cc::music;
    std::vector<std::string> args = argsIn;
    if (args.empty()) return 2;
    const std::string sub = args[0];
    args.erase(args.begin());
    DlsPtr gm;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--gm") {
            gm = loadBankFile(args[i + 1]);
            args.erase(args.begin() + long(i), args.begin() + long(i) + 2);
            break;
        }
    setLogEcho(false);
    Vfs vfs;
    auto mount = [&](size_t first) {
        return mountArchivesForTool(vfs, std::vector<std::string>(args.begin() + long(first), args.end()));
    };
    if (sub == "survey" && !args.empty()) {
        if (!mount(0)) return 1;
        return cmdSurvey(vfs) == 0 ? 0 : 1;
    }
    if (sub == "trace" && args.size() >= 3) {
        if (!mount(2)) return 1;
        return cmdTrace(vfs, args[0], std::atof(args[1].c_str()));
    }
    if (sub == "render" && args.size() >= 4) {
        if (!mount(3)) return 1;
        return cmdRender(vfs, args[0], std::atof(args[1].c_str()), args[2], gm);
    }
    if (sub == "scenario" && !args.empty()) {
        if (!mount(0)) return 1;
        return cmdScenario(vfs) == 0 ? 0 : 1;
    }
    if (sub == "all" && args.size() >= 2) {
        const std::string outDir = args[0];
        if (!mount(1)) return 1;
        int problems = cmdSurvey(vfs);
        Checker c;
        std::printf("pattern choices\n");
        std::vector<std::string> p;
        cmdTrace(vfs, "EnemyStart", 60, &p);
        auto has = [&](const std::string& n) { return std::find(p.begin(), p.end(), n) != p.end(); };
        c.expect(p.size() >= 3 && p[0] == "Enenmy_intro" && (p[1] == "EnemyPause_3" || p[1] == "EnemyRace1.1"),
                 "EnemyStart: Enenmy_intro, then EnemyPause_3 or EnemyRace1.1");
        c.expect(has("EnemyRace1.3") && has("EnemyRace1.2"), "EnemyStart goes on with EnemyRace1.3 and EnemyRace1.2");
        p.clear();
        cmdTrace(vfs, "DuckStart", 120, &p);
        const char* duck[] = {"DuckIntro", "DuckPart1", "DuckPart2", "DuckPart3", "DuckPart5", "DuckPart6", "DuckPart7"};
        bool order = p.size() >= 7;
        for (size_t i = 0; i < 7 && order; ++i) order = p[i] == duck[i];
        std::string got;
        for (size_t i = 0; i < std::min<size_t>(p.size(), 8); ++i) got += " " + p[i];
        c.expect(order, "DuckStart: DuckIntro, DuckPart1-3, DuckPart5-7:" + got);
        p.clear();
        cmdTrace(vfs, "SunRoofStart", 40, &p);
        c.expect(p.size() >= 2 && p[0] == "SunRoofStart" && p[1] == "SunRoofTheme", "SunRoofStart: SunRoofStart, then SunRoofTheme");
        problems += c.problems;
        problems += cmdScenario(vfs);
        std::printf("renders\n");
        problems += cmdRender(vfs, "EnemyStart", 25, outDir + "/music_EnemyStart.wav", gm);
        problems += cmdRender(vfs, "SunRoofStart", 20, outDir + "/music_SunRoofStart.wav", gm);
        std::printf("music: %d problem(s)\n", problems);
        return problems == 0 ? 0 : 1;
    }
    std::fprintf(stderr,
                 "usage:\n  cctool music survey <archive.ar>...\n  cctool music trace <segment> <seconds> <archive.ar>...\n"
                 "  cctool music render <segment> <seconds> <out.wav> [--gm bank] <archive.ar>...\n"
                 "  cctool music scenario <archive.ar>...\n  cctool music all <outdir> [--gm bank] <archive.ar>...\n");
    return 2;
}
