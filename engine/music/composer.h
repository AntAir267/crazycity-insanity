// Composing from a style (spec/formats/directmusic.md, "Composing from a style"): the style
// track's pattern choice, part variations, notes with their random ranges, curves, and the music
// value conversion with the chord (always the default chord with MM2's data).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/game_random.h"
#include "music/dm_files.h"

namespace cc {
namespace music {

struct Subchord {
    uint32_t chordPattern = 0x891, scalePattern = 0xAB5AB5, inversionPoints = 0xFFFFFF, levels = 0xFFFFFFFF;
    uint8_t chordRoot = 12, scaleRoot = 0;
};

struct Chord {
    std::string name = "M7";
    uint8_t keyRoot = 12;
    uint32_t keyScale = 0xAB5AB5;
    std::vector<Subchord> subchords{Subchord()};
    const Subchord& forLevel(uint8_t level) const;
};

// The default chord of a segment without a chord track.
const Chord& defaultChord();

// A note's music value to a MIDI key ("Music values"); play mode 0x10 must already be replaced
// by the part's. Returns the key (0 for a value outside 0…127 after the conversion).
int musicValueToKey(uint16_t value, uint8_t playMode, uint8_t invLower, uint8_t invUpper, const Subchord& chord,
                    bool* outside = nullptr);

// The variation masks of the default chord ("Parts and variations", step 1).
std::vector<int> allowedVariations(const Part& part);

// Where the composed notes and controller values go (global ticks).
class ComposerSink {
public:
    virtual ~ComposerSink() = default;
    virtual void note(int64_t on, int64_t off, int pchannel, int key, int velocity) = 0;
    // type 3 pitch bend, 4 controller (cc), 5 channel pressure
    virtual void control(int64_t tick, int type, int pchannel, int cc, int value) = 0;
    // A curve with the reset flag: its reset value is sent if the segment is cut before `until`.
    virtual void curveReset(int64_t until, int type, int pchannel, int cc, int value) = 0;
    virtual void patternChosen(int64_t tick, const Style& style, const Pattern& p, bool silent) {
        (void)tick;
        (void)style;
        (void)p;
        (void)silent;
    }
};

// One style track of a playing segment (or a motif's pattern track).
class StyleComposer {
public:
    // `seg`'s style events and commands; a motif segment plays its fixed pattern.
    void init(const Segment* seg);

    // The start of play or a loop jump to segment time `pos`: repeat the choices silently from
    // time 0 up to `pos` (motif: new variations), then the pattern plays from the middle.
    // `endLimit` is the loop end while loops remain, else the length. `off` maps segment time to
    // global ticks.
    void seek(int32_t pos, int32_t endLimit, Lcg& rng, ComposerSink& sink, int64_t off);
    // Plays segment times [a, b).
    void play(int32_t a, int32_t b, int32_t endLimit, Lcg& rng, ComposerSink& sink, int64_t off);

    const Pattern* pattern() const { return pat_; }
    const Style* style() const { return style_; }

private:
    void choose(int32_t t, int32_t endLimit, Lcg& rng, ComposerSink& sink, int64_t off, bool silent);
    void chooseVariations(Lcg& rng, bool samePattern);
    void playWindow(int32_t a, int32_t b, Lcg& rng, ComposerSink& sink, int64_t off);
    void resendCurves(int32_t entry, ComposerSink& sink, int64_t off);
    void emitCurve(const Curve& c, int64_t start, int64_t from, int pch, ComposerSink& sink);
    int partLength(const Part& p) const;
    TimeSig partSig(const Part& p) const;

    const Segment* seg_ = nullptr;
    const Style* style_ = nullptr;
    const Pattern* pat_ = nullptr;
    const Pattern* lastPat_ = nullptr;
    int32_t patStart_ = 0, patEnd_ = 0;
    std::vector<int> var_;  // per part reference
    bool motif_ = false;
};

}  // namespace music
}  // namespace cc
