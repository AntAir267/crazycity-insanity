// DirectMusic music files as MM2 uses them (spec/formats/directmusic.md): segments (.sgt),
// styles (.sty), bands (.bnd and inside segments and styles) and DLS level 1 collections (.dls).
// Plain data; parsing in dm_files.cpp, resolving references in dm_loader.h.
#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/bytes.h"

namespace cc {
namespace music {

struct Guid {
    uint8_t b[16] = {};
    bool operator==(const Guid& o) const { return std::memcmp(b, o.b, 16) == 0; }
    bool operator!=(const Guid& o) const { return !(*this == o); }
    bool isZero() const {
        for (uint8_t x : b)
            if (x) return false;
        return true;
    }
    std::string str() const;  // 8-4-4-4-12 hex
};

// LIST DMRF: a reference to another file.
struct DmReference {
    Guid classId;
    uint32_t valid = 0;
    Guid guid;
    bool hasGuid = false;
    std::string name, file;
};

struct TimeSig {
    uint8_t beats = 4;      // per measure
    uint8_t beatValue = 4;  // 4 = quarter
    uint16_t grids = 2;     // per beat
    int beatTicks() const { return beatValue ? 3072 / beatValue : 768; }
    int measureTicks() const { return int(beats ? beats : 1) * beatTicks(); }
    int gridTicks() const { return grids ? beatTicks() / grids : beatTicks(); }
};

// ---- bands

struct BandInstrument {
    uint32_t patch = 0, assignPatch = 0;
    uint32_t noteRanges[4] = {};
    uint32_t pchannel = 0, flags = 0;
    uint8_t pan = 64, volume = 100;
    int16_t transpose = 0;
    uint32_t priority = 0;
    bool hasRef = false;
    DmReference ref;  // DLS collection
};

enum BandFlags : uint32_t {
    BandProgram = 0x1,
    BandBank = 0x2,
    BandAssign = 0x8,
    BandNoteRanges = 0x10,
    BandPan = 0x20,
    BandVolume = 0x40,
    BandTranspose = 0x80,
    BandGM = 0x100,
    BandGS = 0x200,
    BandXG = 0x400,
    BandPriority = 0x800,
};

struct Band {
    Guid guid;
    std::string name;
    std::vector<BandInstrument> instruments;  // file order
};
using BandPtr = std::shared_ptr<Band>;

// ---- styles

struct Note {
    int32_t grid = 0;
    uint32_t variations = 0;
    int32_t duration = 0;
    int16_t offset = 0;
    uint16_t value = 0;  // music value
    uint8_t velocity = 0, timeRange = 0, durRange = 0, velRange = 0, inversionId = 0, playMode = 0;
};

struct Curve {
    int32_t grid = 0;
    uint32_t variations = 0;
    int32_t duration = 0, resetDuration = 0;
    int16_t offset = 0, startValue = 0, endValue = 0, resetValue = 0;
    uint8_t type = 0, shape = 0, cc = 0, flags = 0;
};

struct Part {
    TimeSig sig;
    uint32_t choices[32] = {};
    Guid guid;
    uint16_t lengthMeasures = 1;
    uint8_t playMode = 0, invUpper = 127, invLower = 0;
    std::vector<Note> notes;
    std::vector<Curve> curves;
};

struct PartRef {
    Guid partGuid;
    int part = -1;  // index into Style::parts
    uint16_t pchannel = 0;
    uint8_t lockId = 0, subchord = 0, priority = 0, varMode = 0;
};

struct MotifSettings {
    uint32_t repeats = 0;
    int32_t playStart = 0, loopStart = 0, loopEnd = 0;
    uint32_t resolution = 0;
};

enum Embellishment : uint16_t {
    EmbNormal = 0,
    EmbFill = 1,
    EmbBreak = 2,
    EmbIntro = 4,
    EmbEnd = 8,
    EmbMotif = 0x10,
};

struct Pattern {
    TimeSig sig;
    uint8_t grooveBottom = 1, grooveTop = 100;
    uint16_t embellishment = 0;
    uint16_t lengthMeasures = 1;
    std::vector<uint32_t> rhythm;
    bool hasMotif = false;
    MotifSettings motif;
    std::string name;
    std::vector<PartRef> parts;
    bool isMotif() const { return (embellishment & EmbMotif) != 0; }
    int rhythmBits() const;
};

struct Style {
    Guid guid;
    std::string name, fileName;
    TimeSig sig;
    double tempo = 120;
    std::vector<Part> parts;
    std::vector<Pattern> patterns;
    std::vector<BandPtr> bands;  // the first is the default band
    const Pattern* findMotif(const std::string& name) const;  // case-insensitive
    int partIndex(const Guid& g) const;
};
using StylePtr = std::shared_ptr<Style>;

// ---- segments

struct TempoEvent {
    int32_t time = 0;
    double tempo = 120;
};

enum CommandKind : uint8_t { CmdGroove = 0, CmdFill = 1, CmdIntro = 2, CmdBreak = 3, CmdEnd = 4 };

struct CommandEvent {
    int32_t time = 0;
    uint16_t measure = 0;
    uint8_t beat = 0, command = 0, groove = 0, range = 0;
};

struct StyleRefEvent {
    int32_t time = 0;
    DmReference ref;
    StylePtr style;  // resolved by the loader
};

struct BandEvent {
    int32_t time = 0;
    BandPtr band;
};

struct Segment {
    std::string name, fileName;
    Guid guid;
    uint32_t repeats = 0;
    int32_t length = 0, playStart = 0, loopStart = 0, loopEnd = 0;
    uint32_t resolution = 0;
    bool hasStyleTrack = false, hasBandTrack = false, hasTempoTrack = false, hasCommandTrack = false;
    int otherTracks = 0;  // chord, sequence, time signature, unknown (not played)
    std::vector<StyleRefEvent> styles;
    std::vector<BandEvent> bands;
    std::vector<TempoEvent> tempos;
    std::vector<CommandEvent> commands;
    // A motif (directmusic.md, "Motifs") plays this pattern of `motifStyle` instead of a style
    // track's choices.
    StylePtr motifStyle;
    const Pattern* motif = nullptr;
    const Style* styleAt(int32_t t) const;  // the first style also applies before its own time
    TimeSig timeSig() const;
};
using SegmentPtr = std::shared_ptr<Segment>;

// ---- DLS

struct WaveSample {
    uint16_t unityNote = 60;
    int16_t fineTune = 0;
    int32_t attenuation = 0;  // 1/655,360 dB
    uint32_t options = 0;
    bool looped = false;
    uint32_t loopType = 0, loopStart = 0, loopLength = 0;  // frames
};

struct Connection {
    uint16_t source = 0, control = 0, destination = 0, transform = 0;
    int32_t scale = 0;
};

struct Articulation {
    std::vector<Connection> conns;
    bool find(uint16_t src, uint16_t ctl, uint16_t dst, int32_t& scale) const {
        for (const Connection& c : conns)
            if (c.source == src && c.control == ctl && c.destination == dst) {
                scale = c.scale;
                return true;
            }
        return false;
    }
};

struct DlsRegion {
    uint16_t keyLo = 0, keyHi = 127, velLo = 0, velHi = 127, options = 0, keyGroup = 0;
    bool hasWsmp = false;
    WaveSample wsmp;
    uint16_t linkOptions = 0, phaseGroup = 0;
    uint32_t channel = 1, waveIndex = 0;
    bool hasArt = false;
    Articulation art;
};

struct DlsWave {
    uint32_t rate = 22050;
    std::vector<int16_t> samples;  // mono
    bool hasWsmp = false;
    WaveSample wsmp;
};

struct DlsInstrument {
    uint32_t bank = 0, program = 0;
    std::string name;
    std::vector<DlsRegion> regions;
    bool hasArt = false;
    Articulation art;
    // Patch as bands name it: program | LSB << 8 | MSB << 16 | drum bit 31.
    uint32_t patch() const {
        return (program & 0x7F) | ((bank & 0x7F) << 8) | (((bank >> 8) & 0x7F) << 16) | (bank & 0x80000000u);
    }
};

struct DlsCollection {
    Guid guid;
    bool hasGuid = false;
    std::string name, fileName;
    std::vector<DlsInstrument> instruments;
    std::vector<DlsWave> waves;  // in pool-table order
    const DlsInstrument* find(uint32_t patch) const;
};
using DlsPtr = std::shared_ptr<DlsCollection>;

// ---- parsing (false + message on a broken file)

bool parseSegment(const Bytes& data, Segment& out, std::string& err);
bool parseStyle(const Bytes& data, Style& out, std::string& err);
bool parseBandFile(const Bytes& data, Band& out, std::string& err);
bool parseDls(const Bytes& data, DlsCollection& out, std::string& err);
// SoundFont 2 (an alternative General MIDI bank, our choice): presets flattened into DLS-style
// instruments and regions with the closest DLS level 1 articulation.
bool parseSf2(const Bytes& data, DlsCollection& out, std::string& err);

// The kind of a RIFF file from its form type: "DMSG", "DMST", "DMBD", "DLS ", "sfbk" or "".
std::string riffForm(const Bytes& data);

}  // namespace music
}  // namespace cc
