// The DirectMusic performance as MM2 drives it (spec/formats/directmusic.md, "Playback: time",
// "Playback: a segment", "Starting a primary segment", "Stopping", "Motifs", "Automatic
// transitions", "Playing bands"): a timeline in ticks (768 per quarter note) with a tempo map, one
// primary segment at a time (plus a queue), secondary segments beside it, the tracks (style,
// band, tempo, command) turned into timed events ahead of playback, and the synthesiser that
// renders them.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/game_random.h"
#include "music/composer.h"
#include "music/dm_files.h"
#include "music/synth.h"

namespace cc {
namespace music {

enum class Boundary { Immediate, Grid, Beat, Measure };

class Performance;

// One playing (or queued) instance of a segment.
struct SegState {
    int id = 0;
    SegmentPtr seg;
    bool primary = false;
    int64_t start = 0;  // global tick at which the play start point plays
    int32_t playStart = 0, loopStart = 0, loopEnd = 0, length = 0;
    int64_t repeats = 0;
    bool looping = false;
    int64_t total = 0;  // ticks from start to end
    bool stopped = false;
    int64_t stopTick = 0;
    bool dead = false;  // its stop has been carried out (or it was dropped)
    bool dropped = false;  // taken out of the queue before it started
    int64_t genTo = 0;  // generated up to (global, exclusive)
    bool begun = false;
    int64_t lap = 0;
    std::string pattern;  // the pattern sounding (for the page)
    StyleComposer composer;
    struct Reset {
        int64_t until;
        int type, pch, cc, value;
    };
    std::vector<Reset> resets;

    int64_t end() const { return stopped ? stopTick : start + total; }
    // Segment time and loop count at elapsed e (≥ 0).
    int32_t segTime(int64_t e, int64_t* lapOut = nullptr) const;
};
using StatePtr = std::shared_ptr<SegState>;

struct PerformanceTrace {
    int64_t tick = 0;
    std::string segment, style, pattern;
    int kind = 0;  // 0 pattern start, 1 segment start, 2 segment stop
};

class Performance {
public:
    explicit Performance(DlsSynth& synth);

    void setSeed(uint32_t seed) { rng_.reset(seed); }
    Lcg& rng() { return rng_; }
    DlsSynth& synth() { return synth_; }

    // The clocks (directmusic.md, "Playback: time"): the render point (the next sample to render),
    // the present (80 ms before it) and *now* (the render point or, if later, the latest MIDI
    // event already handed to the synthesiser: events go over once less than 75 ms ahead).
    int64_t now() const;
    int64_t renderTick() const;
    double presentTick() const;
    double tempo() const { return tempo_; }
    int64_t samplePosition() const { return samplePos_; }

    // repeats < 0: the segment's own.
    StatePtr playPrimary(SegmentPtr seg, Boundary b, int64_t repeats = -1);
    StatePtr playSecondary(SegmentPtr seg, Boundary b, int64_t repeats = -1);
    // Stop at once: every state of the segment (queued ones are dropped).
    void stop(const Segment* seg);
    // Automatic transition from the playing primary to `to` (may be null: to silence).
    void transition(SegmentPtr to, uint8_t command, Boundary b);
    bool isPlaying(const Segment* seg) const;
    // The primary playing at the queue time, if any.
    const SegState* primaryNow() const;
    // Everything stopped and forgotten (the music player is thrown away).
    void clear();

    // Renders stereo 16-bit frames, carrying out the events due on the way.
    void render(int16_t* out, int frames);

    // Composing ahead: up to the present + this many milliseconds (1000 in MM2).
    void setPrepareMs(double ms) { prepareMs_ = ms; }
    std::function<void(const PerformanceTrace&)> trace;
    // What sounds now (for the page): segment and pattern names.
    std::string nowPlaying() const;

private:
    friend class StateSink;
    struct Event {
        int64_t tick;
        int cls;  // 0 stop/cut, 1 tempo, 2 note-off, 3 controls/band, 4 note-on, 5 trace
        uint64_t seq;
        int type;
        int owner;
        int a, b, c;
        uint32_t note;
        int64_t offTick;  // note-on: its note-off's time
        double d;
        std::string text;
    };
    struct EventLess {
        bool operator()(const Event& x, const Event& y) const {
            if (x.tick != y.tick) return x.tick < y.tick;
            if (x.cls != y.cls) return x.cls < y.cls;
            return x.seq < y.seq;
        }
    };
    enum EvType { EvNoteOn, EvNoteOff, EvCC, EvProgram, EvBend, EvPressure, EvTempo, EvTranspose, EvPriority, EvStop, EvCutAll, EvTrace };

    StatePtr makeState(SegmentPtr seg, bool primary, int64_t start, int64_t repeats);
    void push(Event e);
    void stopAt(SegState& s, int64_t t);
    void drop(SegState& s);
    const SegState* primaryAt(int64_t t) const;
    int64_t roundUp(const SegState& s, int64_t t, Boundary b) const;
    void generate(int64_t horizon);
    void generateState(SegState& s, int64_t to);
    void sendBand(const Band& band, int64_t tick, int owner);
    void dispatch(const Event& e);
    void eraseEvents(int owner, int64_t from);
    void stopAll(int64_t t);
    static bool isMidi(int type) { return type <= EvPressure; }
    double sampleOfTick(double tick) const { return anchorSample_ + (tick - anchorTick_) * spt_; }
    double tickOfSample(double sample) const { return anchorTick_ + (sample - anchorSample_) / spt_; }
    void setTempo(double t, double atTick);
    SegState* stateById(int id);

    DlsSynth& synth_;
    Lcg rng_{1};
    double tempo_ = 120, spt_ = 0;
    double anchorTick_ = 0, anchorSample_ = 0;
    int64_t samplePos_ = 0;
    double prepareMs_ = 1000;
    std::vector<StatePtr> states_;
    std::set<Event, EventLess> queue_;
    uint64_t seq_ = 0;
    int nextId_ = 1;
    uint32_t nextNote_ = 1;
    struct Sounding {
        int pch, key, owner;
    };
    std::unordered_map<uint32_t, Sounding> sounding_;
    int transpose_[DlsSynth::kChannels] = {};
};

}  // namespace music
}  // namespace cc
