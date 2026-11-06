#include "music/dm_files.h"

#include <cstdio>

#include "core/strutil.h"
#include "music/riff.h"

namespace cc {
namespace music {

std::string Guid::str() const {
    char s[40];
    std::snprintf(s, sizeof s, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x", b[3], b[2], b[1],
                  b[0], b[5], b[4], b[7], b[6], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    return s;
}

int Pattern::rhythmBits() const {
    int n = 0;
    for (uint32_t w : rhythm)
        for (; w; w &= w - 1) ++n;
    return n;
}

const Pattern* Style::findMotif(const std::string& n) const {
    for (const Pattern& p : patterns)
        if (p.isMotif() && iequals(p.name, n)) return &p;
    return nullptr;
}

int Style::partIndex(const Guid& g) const {
    for (size_t i = 0; i < parts.size(); ++i)
        if (parts[i].guid == g) return int(i);
    return -1;
}

const Style* Segment::styleAt(int32_t t) const {
    const Style* s = nullptr;
    for (const StyleRefEvent& e : styles) {
        if (!e.style) continue;
        if (!s || e.time <= t) s = e.style.get();
        if (e.time > t) break;
    }
    if (!s && motifStyle) s = motifStyle.get();
    return s;
}

TimeSig Segment::timeSig() const {
    if (const Style* s = styleAt(0)) return s->sig;
    return TimeSig();
}

const DlsInstrument* DlsCollection::find(uint32_t patch) const {
    for (const DlsInstrument& i : instruments)
        if (i.patch() == patch) return &i;
    return nullptr;
}

std::string riffForm(const Bytes& data) {
    if (data.size() < 12 || readU32LE(data.data()) != fourcc("RIFF")) return std::string();
    return std::string(reinterpret_cast<const char*>(data.data() + 8), 4);
}

namespace {

void readGuid(ByteReader& r, Guid& g) { r.bytes(g.b, 16); }

TimeSig readTimeSig(ByteReader& r) {
    TimeSig t;
    t.beats = r.u8();
    t.beatValue = r.u8();
    t.grids = r.u16();
    return t;
}

double readF64(ByteReader& r) {
    uint8_t b[8];
    r.bytes(b, 8);
    double d;
    std::memcpy(&d, b, 8);
    return d;
}

std::string unfoName(const RiffChunk& list) {
    RiffIter it = children(list);
    RiffChunk c;
    while (it.next(c))
        if (c.id == fourcc("UNAM")) return utf16Name(c.data, c.size);
    return std::string();
}

bool readReference(const RiffChunk& list, DmReference& ref) {
    RiffIter it = children(list);
    RiffChunk c;
    while (it.next(c)) {
        ByteReader r = c.reader();
        if (c.id == fourcc("refh")) {
            readGuid(r, ref.classId);
            ref.valid = r.u32();
        } else if (c.id == fourcc("guid")) {
            readGuid(r, ref.guid);
            ref.hasGuid = r.ok();
        } else if (c.id == fourcc("name")) {
            ref.name = utf16Name(c.data, c.size);
        } else if (c.id == fourcc("file")) {
            ref.file = utf16Name(c.data, c.size);
        }
    }
    return true;
}

bool readBand(const RiffChunk& riff, Band& band) {
    RiffIter it = children(riff);
    RiffChunk c;
    while (it.next(c)) {
        if (c.id == fourcc("guid")) {
            ByteReader r = c.reader();
            readGuid(r, band.guid);
        } else if (c.isList() && c.form == fourcc("UNFO")) {
            band.name = unfoName(c);
        } else if (c.isList() && c.form == fourcc("lbil")) {
            RiffIter li = children(c);
            RiffChunk ins;
            while (li.next(ins)) {
                if (!(ins.isList() && ins.form == fourcc("lbin"))) continue;
                BandInstrument bi;
                bool haveBins = false;
                RiffIter ii = children(ins);
                RiffChunk x;
                while (ii.next(x)) {
                    if (x.id == fourcc("bins")) {
                        ByteReader r = x.reader();
                        bi.patch = r.u32();
                        bi.assignPatch = r.u32();
                        for (uint32_t& w : bi.noteRanges) w = r.u32();
                        bi.pchannel = r.u32();
                        bi.flags = r.u32();
                        bi.pan = r.u8();
                        bi.volume = r.u8();
                        bi.transpose = int16_t(r.u16());
                        bi.priority = r.u32();
                        haveBins = r.ok();
                    } else if (x.isList() && x.form == fourcc("DMRF")) {
                        bi.hasRef = readReference(x, bi.ref);
                    }
                }
                if (haveBins) band.instruments.push_back(bi);
            }
        }
    }
    return true;
}

// Item arrays: u32 item size, then items of that size.
template <typename F>
void forItems(const RiffChunk& c, F f) {
    ByteReader r = c.reader();
    uint32_t isz = r.u32();
    if (!r.ok() || isz == 0) return;
    while (r.remaining() >= isz) {
        ByteReader item = r.sub(isz);
        f(item);
    }
}

void readPart(const RiffChunk& list, Part& p) {
    RiffIter it = children(list);
    RiffChunk c;
    while (it.next(c)) {
        if (c.id == fourcc("prth")) {
            ByteReader r = c.reader();
            p.sig = readTimeSig(r);
            for (uint32_t& w : p.choices) w = r.u32();
            readGuid(r, p.guid);
            p.lengthMeasures = r.u16();
            p.playMode = r.u8();
            p.invUpper = r.u8();
            p.invLower = r.u8();
        } else if (c.id == fourcc("note")) {
            forItems(c, [&](ByteReader& r) {
                Note n;
                n.grid = int32_t(r.u32());
                n.variations = r.u32();
                n.duration = int32_t(r.u32());
                n.offset = int16_t(r.u16());
                n.value = r.u16();
                n.velocity = r.u8();
                n.timeRange = r.u8();
                n.durRange = r.u8();
                n.velRange = r.u8();
                n.inversionId = r.u8();
                n.playMode = r.u8();
                if (r.ok()) p.notes.push_back(n);
            });
        } else if (c.id == fourcc("crve")) {
            forItems(c, [&](ByteReader& r) {
                Curve v;
                v.grid = int32_t(r.u32());
                v.variations = r.u32();
                v.duration = int32_t(r.u32());
                v.resetDuration = int32_t(r.u32());
                v.offset = int16_t(r.u16());
                v.startValue = int16_t(r.u16());
                v.endValue = int16_t(r.u16());
                v.resetValue = int16_t(r.u16());
                v.type = r.u8();
                v.shape = r.u8();
                v.cc = r.u8();
                v.flags = r.u8();
                if (r.ok()) p.curves.push_back(v);
            });
        }
    }
}

void readPattern(const RiffChunk& list, Pattern& p) {
    RiffIter it = children(list);
    RiffChunk c;
    while (it.next(c)) {
        ByteReader r = c.reader();
        if (c.id == fourcc("ptnh")) {
            p.sig = readTimeSig(r);
            p.grooveBottom = r.u8();
            p.grooveTop = r.u8();
            p.embellishment = r.u16();
            p.lengthMeasures = r.u16();
        } else if (c.id == fourcc("rhtm")) {
            while (r.remaining() >= 4) p.rhythm.push_back(r.u32());
        } else if (c.id == fourcc("mtfs")) {
            p.motif.repeats = r.u32();
            p.motif.playStart = int32_t(r.u32());
            p.motif.loopStart = int32_t(r.u32());
            p.motif.loopEnd = int32_t(r.u32());
            p.motif.resolution = r.u32();
            p.hasMotif = r.ok();
        } else if (c.isList() && c.form == fourcc("UNFO")) {
            p.name = unfoName(c);
        } else if (c.isList() && c.form == fourcc("pref")) {
            RiffIter pi = children(c);
            RiffChunk x;
            while (pi.next(x)) {
                if (x.id != fourcc("prfc")) continue;
                ByteReader q = x.reader();
                PartRef ref;
                readGuid(q, ref.partGuid);
                ref.pchannel = q.u16();
                ref.lockId = q.u8();
                ref.subchord = q.u8();
                ref.priority = q.u8();
                ref.varMode = q.u8();
                if (q.ok()) p.parts.push_back(ref);
            }
        }
    }
}

bool openForm(const Bytes& data, uint32_t form, RiffChunk& top, std::string& err) {
    RiffIter it(data.data(), data.size());
    if (!it.next(top) || top.id != fourcc("RIFF")) {
        err = "not a RIFF file";
        return false;
    }
    if (top.form != form) {
        err = "unexpected RIFF form";
        return false;
    }
    return true;
}

bool sameSuffix(const Guid& g) {
    // d2ac28xx-b39b-11d1-8704-00600893b1bd
    static const uint8_t tail[15] = {0x28, 0xac, 0xd2, 0x9b, 0xb3, 0xd1, 0x11, 0x87, 0x04, 0x00, 0x60, 0x08, 0x93, 0xb1, 0xbd};
    return std::memcmp(g.b + 1, tail, 15) == 0;
}

}  // namespace

bool parseSegment(const Bytes& data, Segment& seg, std::string& err) {
    RiffChunk top;
    if (!openForm(data, fourcc("DMSG"), top, err)) return false;
    bool haveHeader = false;
    RiffIter it = children(top);
    RiffChunk c;
    while (it.next(c)) {
        if (c.id == fourcc("segh")) {
            ByteReader r = c.reader();
            seg.repeats = r.u32();
            seg.length = int32_t(r.u32());
            seg.playStart = int32_t(r.u32());
            seg.loopStart = int32_t(r.u32());
            seg.loopEnd = int32_t(r.u32());
            seg.resolution = r.u32();
            haveHeader = r.ok();
        } else if (c.id == fourcc("guid")) {
            ByteReader r = c.reader();
            readGuid(r, seg.guid);
        } else if (c.isList() && c.form == fourcc("UNFO")) {
            seg.name = unfoName(c);
        } else if (c.isList() && c.form == fourcc("trkl")) {
            RiffIter ti = children(c);
            RiffChunk trk;
            while (ti.next(trk)) {
                if (!(trk.id == fourcc("RIFF") && trk.form == fourcc("DMTK"))) continue;
                Guid cls;
                RiffIter xi = children(trk);
                RiffChunk x;
                std::vector<RiffChunk> body;
                while (xi.next(x)) {
                    if (x.id == fourcc("trkh")) {
                        ByteReader r = x.reader();
                        readGuid(r, cls);
                    } else {
                        body.push_back(x);
                    }
                }
                const uint8_t kind = sameSuffix(cls) ? cls.b[0] : 0;
                bool known = true;
                for (const RiffChunk& b : body) {
                    if (kind == 0x8d && b.isList() && b.form == fourcc("sttr")) {
                        seg.hasStyleTrack = true;
                        RiffIter si = children(b);
                        RiffChunk s;
                        while (si.next(s)) {
                            if (!(s.isList() && s.form == fourcc("strf"))) continue;
                            StyleRefEvent ev;
                            RiffIter ri = children(s);
                            RiffChunk y;
                            while (ri.next(y)) {
                                if (y.id == fourcc("stmp")) {
                                    ByteReader r = y.reader();
                                    ev.time = int32_t(r.u32());
                                } else if (y.isList() && y.form == fourcc("DMRF")) {
                                    readReference(y, ev.ref);
                                }
                            }
                            seg.styles.push_back(ev);
                        }
                    } else if (kind == 0x94 && b.id == fourcc("RIFF") && b.form == fourcc("DMBT")) {
                        seg.hasBandTrack = true;
                        RiffIter bi = children(b);
                        RiffChunk l;
                        while (bi.next(l)) {
                            if (!(l.isList() && l.form == fourcc("lbdl"))) continue;
                            RiffIter li = children(l);
                            RiffChunk e;
                            while (li.next(e)) {
                                if (!(e.isList() && e.form == fourcc("lbnd"))) continue;
                                BandEvent ev;
                                RiffIter ei = children(e);
                                RiffChunk y;
                                while (ei.next(y)) {
                                    if (y.id == fourcc("bdih")) {
                                        ByteReader r = y.reader();
                                        ev.time = int32_t(r.u32());
                                    } else if (y.id == fourcc("RIFF") && y.form == fourcc("DMBD")) {
                                        ev.band = std::make_shared<Band>();
                                        readBand(y, *ev.band);
                                    }
                                }
                                if (ev.band) seg.bands.push_back(ev);
                            }
                        }
                    } else if (kind == 0x85 && b.id == fourcc("tetr")) {
                        seg.hasTempoTrack = true;
                        forItems(b, [&](ByteReader& r) {
                            TempoEvent t;
                            t.time = int32_t(r.u32());
                            r.skip(4);
                            t.tempo = readF64(r);
                            if (r.ok()) seg.tempos.push_back(t);
                        });
                    } else if (kind == 0x8c && b.id == fourcc("cmnd")) {
                        seg.hasCommandTrack = true;
                        forItems(b, [&](ByteReader& r) {
                            CommandEvent e;
                            e.time = int32_t(r.u32());
                            e.measure = r.u16();
                            e.beat = r.u8();
                            e.command = r.u8();
                            e.groove = r.u8();
                            e.range = r.u8();
                            // the two padding bytes hold garbage in MM2's files: ignored
                            if (r.ok()) seg.commands.push_back(e);
                        });
                    } else {
                        known = false;
                    }
                }
                if (!known || body.empty()) ++seg.otherTracks;
            }
        }
    }
    if (!haveHeader) {
        err = "no segment header";
        return false;
    }
    return true;
}

bool parseStyle(const Bytes& data, Style& st, std::string& err) {
    RiffChunk top;
    if (!openForm(data, fourcc("DMST"), top, err)) return false;
    bool haveHeader = false;
    RiffIter it = children(top);
    RiffChunk c;
    while (it.next(c)) {
        if (c.id == fourcc("styh")) {
            ByteReader r = c.reader();
            st.sig = readTimeSig(r);
            st.tempo = readF64(r);
            haveHeader = r.ok();
        } else if (c.id == fourcc("guid")) {
            ByteReader r = c.reader();
            readGuid(r, st.guid);
        } else if (c.isList() && c.form == fourcc("UNFO")) {
            st.name = unfoName(c);
        } else if (c.isList() && c.form == fourcc("part")) {
            st.parts.emplace_back();
            readPart(c, st.parts.back());
        } else if (c.isList() && c.form == fourcc("pttn")) {
            st.patterns.emplace_back();
            readPattern(c, st.patterns.back());
        } else if (c.id == fourcc("RIFF") && c.form == fourcc("DMBD")) {
            auto b = std::make_shared<Band>();
            readBand(c, *b);
            st.bands.push_back(b);
        }
    }
    if (!haveHeader) {
        err = "no style header";
        return false;
    }
    for (Pattern& p : st.patterns)
        for (PartRef& r : p.parts) r.part = st.partIndex(r.partGuid);
    return true;
}

bool parseBandFile(const Bytes& data, Band& band, std::string& err) {
    RiffChunk top;
    if (!openForm(data, fourcc("DMBD"), top, err)) return false;
    return readBand(top, band);
}

namespace {

void readWsmp(const RiffChunk& c, WaveSample& w) {
    ByteReader r = c.reader();
    uint32_t cb = r.u32();
    w.unityNote = r.u16();
    w.fineTune = int16_t(r.u16());
    w.attenuation = int32_t(r.u32());
    w.options = r.u32();
    uint32_t loops = r.u32();
    w.looped = false;
    if (cb >= 20 && r.seek(cb) && loops > 0) {
        uint32_t lsz = r.u32();
        w.loopType = r.u32();
        w.loopStart = r.u32();
        w.loopLength = r.u32();
        (void)lsz;
        w.looped = r.ok();
    }
}

void readArt(const RiffChunk& lart, Articulation& a, bool& has) {
    RiffIter it = children(lart);
    RiffChunk c;
    while (it.next(c)) {
        if (c.id != fourcc("art1")) continue;
        ByteReader r = c.reader();
        uint32_t cb = r.u32();
        uint32_t n = r.u32();
        if (!r.ok() || !r.seek(cb)) continue;
        has = true;
        for (uint32_t i = 0; i < n && r.remaining() >= 12; ++i) {
            Connection k;
            k.source = r.u16();
            k.control = r.u16();
            k.destination = r.u16();
            k.transform = r.u16();
            k.scale = int32_t(r.u32());
            a.conns.push_back(k);
        }
    }
}

void readRegion(const RiffChunk& list, DlsRegion& g) {
    RiffIter it = children(list);
    RiffChunk c;
    while (it.next(c)) {
        ByteReader r = c.reader();
        if (c.id == fourcc("rgnh")) {
            g.keyLo = r.u16();
            g.keyHi = r.u16();
            g.velLo = r.u16();
            g.velHi = r.u16();
            g.options = r.u16();
            g.keyGroup = r.u16();
        } else if (c.id == fourcc("wsmp")) {
            readWsmp(c, g.wsmp);
            g.hasWsmp = true;
        } else if (c.id == fourcc("wlnk")) {
            g.linkOptions = r.u16();
            g.phaseGroup = r.u16();
            g.channel = r.u32();
            g.waveIndex = r.u32();
        } else if (c.isList() && c.form == fourcc("lart")) {
            readArt(c, g.art, g.hasArt);
        }
    }
}

bool readWave(const RiffChunk& list, DlsWave& w) {
    RiffIter it = children(list);
    RiffChunk c;
    uint16_t tag = 0, channels = 1, bits = 16;
    const uint8_t* pcm = nullptr;
    size_t pcmSize = 0;
    while (it.next(c)) {
        ByteReader r = c.reader();
        if (c.id == fourcc("fmt ")) {
            tag = r.u16();
            channels = r.u16();
            w.rate = r.u32();
            r.u32();
            r.u16();
            bits = r.u16();
        } else if (c.id == fourcc("data")) {
            pcm = c.data;
            pcmSize = c.size;
        } else if (c.id == fourcc("wsmp")) {
            readWsmp(c, w.wsmp);
            w.hasWsmp = true;
        }
    }
    if (tag != 1 || !pcm || channels == 0) return false;
    if (bits == 16) {
        size_t frames = pcmSize / (2u * channels);
        w.samples.resize(frames);
        for (size_t i = 0; i < frames; ++i) {
            const uint8_t* p = pcm + i * 2u * channels;
            w.samples[i] = int16_t(p[0] | (p[1] << 8));
        }
    } else if (bits == 8) {
        size_t frames = pcmSize / channels;
        w.samples.resize(frames);
        for (size_t i = 0; i < frames; ++i) w.samples[i] = int16_t((int(pcm[i * channels]) - 128) << 8);
    } else {
        return false;
    }
    return true;
}

}  // namespace

bool parseDls(const Bytes& data, DlsCollection& col, std::string& err) {
    RiffChunk top;
    if (!openForm(data, fourcc("DLS "), top, err)) return false;
    std::vector<uint32_t> table;
    RiffChunk wvpl;
    bool haveWvpl = false;
    RiffIter it = children(top);
    RiffChunk c;
    while (it.next(c)) {
        ByteReader r = c.reader();
        if (c.id == fourcc("dlid")) {
            readGuid(r, col.guid);
            col.hasGuid = r.ok();
        } else if (c.id == fourcc("ptbl")) {
            uint32_t cb = r.u32();
            uint32_t n = r.u32();
            if (r.seek(cb))
                for (uint32_t i = 0; i < n && r.remaining() >= 4; ++i) table.push_back(r.u32());
        } else if (c.isList() && c.form == fourcc("lins")) {
            RiffIter li = children(c);
            RiffChunk ins;
            while (li.next(ins)) {
                if (!(ins.isList() && ins.form == fourcc("ins "))) continue;
                DlsInstrument in;
                RiffIter ii = children(ins);
                RiffChunk x;
                while (ii.next(x)) {
                    ByteReader q = x.reader();
                    if (x.id == fourcc("insh")) {
                        q.u32();
                        in.bank = q.u32();
                        in.program = q.u32();
                    } else if (x.isList() && x.form == fourcc("lrgn")) {
                        RiffIter gi = children(x);
                        RiffChunk g;
                        while (gi.next(g)) {
                            if (!(g.isList() && (g.form == fourcc("rgn ") || g.form == fourcc("rgn2")))) continue;
                            DlsRegion rg;
                            readRegion(g, rg);
                            in.regions.push_back(rg);
                        }
                    } else if (x.isList() && x.form == fourcc("lart")) {
                        readArt(x, in.art, in.hasArt);
                    } else if (x.isList() && x.form == fourcc("INFO")) {
                        RiffIter ni = children(x);
                        RiffChunk y;
                        while (ni.next(y))
                            if (y.id == fourcc("INAM"))
                                in.name = std::string(reinterpret_cast<const char*>(y.data), strnlen(reinterpret_cast<const char*>(y.data), y.size));
                    }
                }
                col.instruments.push_back(std::move(in));
            }
        } else if (c.isList() && c.form == fourcc("wvpl")) {
            wvpl = c;
            haveWvpl = true;
        } else if (c.isList() && c.form == fourcc("INFO")) {
            RiffIter ni = children(c);
            RiffChunk y;
            while (ni.next(y))
                if (y.id == fourcc("INAM"))
                    col.name = std::string(reinterpret_cast<const char*>(y.data), strnlen(reinterpret_cast<const char*>(y.data), y.size));
        }
    }
    if (haveWvpl) {
        if (table.empty()) {
            // No pool table: the waves in file order.
            RiffIter wi = children(wvpl);
            RiffChunk w;
            while (wi.next(w)) {
                if (!(w.isList() && w.form == fourcc("wave"))) continue;
                DlsWave dw;
                readWave(w, dw);
                col.waves.push_back(std::move(dw));
            }
        } else {
            for (uint32_t off : table) {
                DlsWave dw;
                if (off + 12 <= wvpl.size) {
                    RiffIter wi(wvpl.data + off, wvpl.size - off);
                    RiffChunk w;
                    if (wi.next(w) && w.isList() && w.form == fourcc("wave")) readWave(w, dw);
                }
                col.waves.push_back(std::move(dw));
            }
        }
    }
    return true;
}

}  // namespace music
}  // namespace cc
