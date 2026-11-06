#include "music/performance.h"

#include <algorithm>
#include <cmath>

namespace cc {
namespace music {

int32_t SegState::segTime(int64_t e, int64_t* lapOut) const {
    if (e < 0) e = 0;
    int64_t lap = 0;
    int64_t t;
    if (!looping) {
        t = playStart + e;
    } else {
        const int64_t first = int64_t(loopEnd) - playStart;
        if (e < first) {
            t = playStart + e;
        } else {
            const int64_t e2 = e - first;
            const int64_t L = int64_t(loopEnd) - loopStart;
            const int64_t k = e2 / L;
            if (k < repeats) {
                lap = k + 1;
                t = loopStart + e2 % L;
            } else {
                lap = repeats;
                t = loopEnd + (e2 - repeats * L);
            }
        }
    }
    if (lapOut) *lapOut = lap;
    return int32_t(std::min<int64_t>(t, INT32_MAX));
}

// Composer output for one state.
class StateSink : public ComposerSink {
public:
    StateSink(Performance& p, SegState& s) : p_(p), s_(s) {}
    void note(int64_t on, int64_t off, int pch, int key, int vel) override {
        // Each event at its own time (or the render point if that has passed): a note pulled
        // early by its time offset may sound before its pattern or segment begins. The note-off
        // goes out at least 1 ms after the note-on.
        const int64_t ms1 = int64_t(std::ceil(p_.tempo() * 768.0 / 60000.0));
        if (off < on + ms1) off = on + ms1;
        Performance::Event e{};
        e.tick = on;
        e.cls = 4;
        e.type = Performance::EvNoteOn;
        e.owner = s_.id;
        e.a = pch;
        e.b = key;
        e.c = vel;
        e.note = p_.nextNote_++;
        e.offTick = off;
        p_.push(e);
        Performance::Event f{};
        f.tick = off;
        f.cls = 2;
        f.type = Performance::EvNoteOff;
        f.owner = s_.id;
        f.note = e.note;
        p_.push(f);
    }
    void control(int64_t tick, int type, int pch, int cc, int value) override {
        Performance::Event e{};
        e.tick = tick;
        e.cls = 3;
        e.type = type == 3 ? Performance::EvBend : type == 5 ? Performance::EvPressure : Performance::EvCC;
        e.owner = s_.id;
        e.a = pch;
        e.b = cc;
        e.c = value;
        p_.push(e);
    }
    void curveReset(int64_t until, int type, int pch, int cc, int value) override {
        s_.resets.push_back({until, type, pch, cc, value});
    }
    void patternChosen(int64_t tick, const Style& style, const Pattern& pat, bool silent) override {
        if (silent) return;
        Performance::Event e{};
        e.tick = std::max(tick, s_.start);
        e.cls = 5;
        e.type = Performance::EvTrace;
        e.owner = s_.id;
        e.a = 0;
        e.text = style.name + "|" + pat.name;
        p_.push(e);
    }

private:
    Performance& p_;
    SegState& s_;
};

Performance::Performance(DlsSynth& synth) : synth_(synth) { setTempo(120, 0); }

void Performance::setTempo(double t, double atTick) {
    if (!(t > 0)) t = 120;
    if (spt_ > 0) anchorSample_ = sampleOfTick(atTick);
    anchorTick_ = atTick;
    tempo_ = t;
    spt_ = double(synth_.sampleRate()) * 60.0 / (t * 768.0);
}

int64_t Performance::renderTick() const { return int64_t(std::ceil(tickOfSample(double(samplePos_)) - 1e-7)); }

double Performance::presentTick() const { return tickOfSample(double(samplePos_) - 0.080 * synth_.sampleRate()); }

int64_t Performance::now() const {
    // The render point, or the latest MIDI event already handed to the synthesiser (each goes over
    // once its time is less than 75 ms after the render point), whichever is later.
    const int64_t r = renderTick();
    const double handOver = tickOfSample(double(samplePos_) + 0.075 * synth_.sampleRate());
    Event probe{};
    probe.tick = int64_t(std::ceil(handOver));
    probe.cls = -1;
    for (auto it = queue_.lower_bound(probe); it != queue_.begin();) {
        --it;
        if (it->tick < r) break;
        if (!(double(it->tick) < handOver) || !isMidi(it->type)) continue;
        if (it->owner) {
            const SegState* o = nullptr;
            for (const StatePtr& st : states_)
                if (st->id == it->owner) o = st.get();
            if (o && (o->dead || (o->stopped && it->tick >= o->stopTick))) continue;
        }
        return std::max(r, it->tick);
    }
    return r;
}

void Performance::push(Event e) {
    e.seq = ++seq_;
    queue_.insert(std::move(e));
}

void Performance::eraseEvents(int owner, int64_t from) {
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->owner == owner && it->tick >= from && it->type != EvStop) it = queue_.erase(it);
        else ++it;
    }
}

SegState* Performance::stateById(int id) {
    for (const StatePtr& s : states_)
        if (s->id == id) return s.get();
    return nullptr;
}

StatePtr Performance::makeState(SegmentPtr seg, bool primary, int64_t start, int64_t repeats) {
    auto s = std::make_shared<SegState>();
    s->id = nextId_++;
    s->seg = seg;
    s->primary = primary;
    s->start = start;
    s->playStart = seg->playStart;
    s->loopStart = seg->loopStart;
    s->loopEnd = seg->loopEnd;
    s->length = seg->length;
    int64_t rep = repeats >= 0 ? repeats : int64_t(seg->repeats);
    // "Playback: a segment", the start rules.
    if (rep != 0 && s->loopStart == 0 && s->loopEnd == 0) {
        s->loopStart = 0;
        s->loopEnd = s->length;
    }
    if (s->loopEnd == 0) s->loopEnd = s->length;
    if (s->loopStart < s->loopEnd) {
        rep = std::min<int64_t>(rep, (1073741823LL - s->length) / (int64_t(s->loopEnd) - s->loopStart));
    } else if (s->loopStart == s->loopEnd && s->loopStart != 0) {
        rep = 0;
    }
    if (s->loopEnd <= s->playStart) rep = 0;
    if (rep < 0) rep = 0;
    s->repeats = rep;
    s->looping = rep > 0 && s->loopStart < s->loopEnd;
    s->total = int64_t(s->length) - s->playStart + (s->looping ? rep * (int64_t(s->loopEnd) - s->loopStart) : 0);
    if (s->total < 1) s->total = 1;
    s->genTo = start;
    states_.push_back(s);
    if (trace) {
        PerformanceTrace t;
        t.tick = start;
        t.segment = seg->name;
        t.kind = 1;
        trace(t);
    }
    return s;
}

const SegState* Performance::primaryAt(int64_t t) const {
    const SegState* best = nullptr;
    for (const StatePtr& s : states_) {
        if (!s->primary || s->dropped) continue;
        if (s->start <= t && t <= s->end())
            if (!best || s->start >= best->start) best = s.get();
    }
    return best;
}

const SegState* Performance::primaryNow() const { return primaryAt(now()); }

int64_t Performance::roundUp(const SegState& s, int64_t t, Boundary b) const {
    if (b == Boundary::Immediate) return t;
    const TimeSig sig = s.seg->timeSig();
    const int unit = b == Boundary::Measure ? sig.measureTicks() : b == Boundary::Beat ? sig.beatTicks() : sig.gridTicks();
    if (unit <= 0) return t;
    const int64_t st = s.segTime(t - s.start);
    const int64_t r = ((st + unit - 1) / unit) * unit;
    return t + (r - st);
}

void Performance::drop(SegState& s) {
    s.stopped = true;
    s.stopTick = s.start;
    s.dead = true;
    s.dropped = true;
    s.genTo = std::max(s.genTo, s.start);
    eraseEvents(s.id, INT64_MIN);
}

void Performance::stopAt(SegState& s, int64_t t) {
    t = std::max(t, renderTick());
    if (s.stopped && s.stopTick <= t) return;
    s.stopped = true;
    s.stopTick = t;
    eraseEvents(s.id, t);  // a stop removes the events composed for its time on
    Event e{};
    e.tick = t;
    e.cls = 0;
    e.type = EvStop;
    e.owner = s.id;
    push(e);
    if (trace) {
        PerformanceTrace tr;
        tr.tick = t;
        tr.segment = s.seg->name;
        tr.kind = 2;
        trace(tr);
    }
}

StatePtr Performance::playPrimary(SegmentPtr seg, Boundary b, int64_t repeats) {
    if (!seg) return nullptr;
    const int64_t t = now();
    const SegState* P = primaryAt(t);
    const int64_t S = P ? roundUp(*P, t, b) : t;
    for (const StatePtr& q : states_) {
        if (!q->primary || q->dead) continue;
        if (q->start > t) drop(*q);  // waiting in the queue
        else if (q->end() > S - 1) stopAt(*q, S - 1);
    }
    StatePtr st = makeState(seg, true, S, repeats);
    Event cut{};
    cut.tick = S;
    cut.cls = 0;
    cut.type = EvCutAll;
    cut.owner = st->id;  // every note sounding at S is cut, except the new segment's own early notes
    push(cut);
    return st;
}

StatePtr Performance::playSecondary(SegmentPtr seg, Boundary b, int64_t repeats) {
    if (!seg) return nullptr;
    const int64_t t = renderTick();  // counted from the render point, not now
    const SegState* P = primaryAt(t);
    const int64_t S = P ? roundUp(*P, t, b) : t;
    return makeState(seg, false, S, repeats);
}

void Performance::stop(const Segment* seg) {
    const int64_t t = now();
    for (const StatePtr& s : states_) {
        if (s->seg.get() != seg || s->dead) continue;
        if (s->start > t) drop(*s);
        else stopAt(*s, t);
    }
}

bool Performance::isPlaying(const Segment* seg) const {
    // Playing = a start the present has reached, and no end or stop before the present
    // (audio/music.md, "Start slot s"): queued segments are not playing yet; one stopped at now
    // still is until the present reaches now.
    const double p = presentTick();
    for (const StatePtr& s : states_) {
        if (s->seg.get() != seg || s->dropped) continue;
        if (double(s->start) <= p && p < double(s->end())) return true;
    }
    return false;
}

void Performance::stopAll(int64_t t) {
    for (const StatePtr& s : states_) {
        if (s->dead) continue;
        if (s->start > t) drop(*s);
        else stopAt(*s, t);
    }
}

void Performance::transition(SegmentPtr to, uint8_t command, Boundary b) {
    const int64_t t = now();
    const SegState* P = primaryAt(t);
    if (!P) {
        // No transition: the new segment starts at once; without one every segment, secondaries
        // included, stops at that time.
        if (to) playPrimary(to, b);
        else stopAll(t);
        return;
    }
    const int64_t S = roundUp(*P, t, b);
    const int64_t t1 = S == t ? S : S - 1;
    const SegState* F = primaryAt(t1);
    if (!F || !F->seg->hasStyleTrack || F->seg->styles.empty()) {
        if (to) playPrimary(to, b);
        else stopAll(S);
        return;
    }
    // Read the from segment at its elapsed play time (the play start and loops not added back).
    const int64_t e = t1 - F->start;
    const Segment& fs = *F->seg;
    StylePtr style;
    for (const StyleRefEvent& ev : fs.styles) {
        if (!ev.style) continue;
        if (!style || ev.time <= e) style = ev.style;
        if (ev.time > e) break;
    }
    if (!style) {
        if (to) playPrimary(to, b);
        else stopAll(S);
        return;
    }
    double tempo = -1;
    for (const TempoEvent& te : fs.tempos)
        if (te.time <= e) tempo = te.tempo;
    if (tempo < 0) {
        tempo = 120;
        if (to)
            for (const TempoEvent& te : to->tempos)
                if (te.time <= 0) tempo = te.tempo;
    }
    int G = 62;
    for (const CommandEvent& c : fs.commands)
        if (c.time <= e) G = c.groove;
    int measures = 1;
    if (command == CmdEnd) {
        int longest = 0;
        for (const Pattern& p : style->patterns)
            if (p.embellishment == EmbEnd) longest = std::max(longest, int(p.lengthMeasures));
        if (longest > 0) measures = longest;
    }
    auto T = std::make_shared<Segment>();
    T->name = std::string(command == CmdEnd ? "ending" : "groove") + " (" + style->name + ")";
    T->length = measures * style->sig.measureTicks();
    T->repeats = 0;
    T->hasStyleTrack = T->hasCommandTrack = T->hasTempoTrack = true;
    StyleRefEvent se;
    se.time = 0;
    se.style = style;
    T->styles.push_back(se);
    CommandEvent ce;
    ce.command = command;
    ce.groove = uint8_t(std::clamp(G, 0, 255));
    T->commands.push_back(ce);
    TempoEvent tev;
    tev.tempo = tempo;
    T->tempos.push_back(tev);
    if (!style->bands.empty()) {
        T->hasBandTrack = true;
        BandEvent be;
        be.time = -64;
        be.band = style->bands.front();
        T->bands.push_back(be);
    }
    StatePtr ts = playPrimary(T, b, 0);
    if (to && ts) makeState(to, true, ts->start + ts->total, -1);
}

void Performance::clear() {
    stopAll(renderTick());
    // carry out the stops at once
    std::vector<Event> stops;
    for (const Event& e : queue_)
        if (e.type == EvStop || e.type == EvCutAll) stops.push_back(e);
    queue_.clear();
    for (const Event& e : stops) dispatch(e);
    for (auto& kv : sounding_) synth_.noteOff(kv.second.pch, kv.second.key);
    sounding_.clear();
    states_.clear();
    for (int& x : transpose_) x = 0;
}

void Performance::sendBand(const Band& band, int64_t tick, int owner) {
    auto ev = [&](int type, int a, int b, int c) {
        Event e{};
        e.tick = tick;
        e.cls = 3;
        e.type = type;
        e.owner = owner;
        e.a = a;
        e.b = b;
        e.c = c;
        push(e);
    };
    for (size_t i = band.instruments.size(); i-- > 0;) {
        const BandInstrument& bi = band.instruments[i];
        const int pch = int(bi.pchannel);
        if (pch < 0 || pch >= DlsSynth::kChannels) continue;
        if (bi.flags & BandProgram) {
            const int msb = (bi.flags & BandBank) ? int((bi.patch >> 16) & 0x7F) : 0;
            const int lsb = (bi.flags & BandBank) ? int((bi.patch >> 8) & 0x7F) : 0;
            ev(EvCC, pch, 0, msb);
            ev(EvCC, pch, 32, lsb);
            ev(EvProgram, pch, int(bi.patch & 0x7F), 0);
        }
        if (bi.flags & BandTranspose) ev(EvTranspose, pch, bi.transpose, 0);
        if (bi.flags & BandVolume) ev(EvCC, pch, 7, bi.volume);
        if (bi.flags & BandPan) ev(EvCC, pch, 10, bi.pan);
        if (bi.flags & BandPriority) ev(EvPriority, pch, int(bi.priority), 0);
    }
}

void Performance::generateState(SegState& s, int64_t to) {
    to = std::min(to, s.end());
    const Segment& seg = *s.seg;
    StateSink sink(*this, s);
    const bool composes = (seg.hasStyleTrack && !seg.styles.empty()) || seg.motif;
    while (s.genTo < to) {
        const int64_t e = s.genTo - s.start;
        int64_t lap = 0;
        const int32_t st = s.segTime(e, &lap);
        int64_t wEnd = to;
        const bool loopsRemain = s.looping && lap < s.repeats;
        if (loopsRemain) {
            const int64_t jump = s.start + (int64_t(s.loopEnd) - s.playStart) + lap * (int64_t(s.loopEnd) - s.loopStart);
            wEnd = std::min(wEnd, jump);
        }
        const int32_t endLimit = loopsRemain ? s.loopEnd : s.length;
        const int64_t off = s.genTo - st;
        if (!s.begun) {
            s.begun = true;
            s.lap = lap;
            // Band track: every band before the play start, in order, at the start.
            for (const BandEvent& b : seg.bands)
                if (b.time < st && b.band) sendBand(*b.band, s.start, s.id);
            // Tempo in force at the start.
            const TempoEvent* tin = nullptr;
            for (const TempoEvent& te : seg.tempos)
                if (te.time < st) tin = &te;
            if (tin) {
                Event ev{};
                ev.tick = s.start;
                ev.cls = 1;
                ev.type = EvTempo;
                ev.owner = s.id;
                ev.d = tin->tempo;
                push(ev);
            }
            if (composes) {
                s.composer.init(&seg);
                s.composer.seek(st, endLimit, rng_, sink, off);
            }
        } else if (lap != s.lap) {
            s.lap = lap;
            const TempoEvent* tin = nullptr;
            for (const TempoEvent& te : seg.tempos)
                if (te.time < st) tin = &te;
            if (tin) {
                Event ev{};
                ev.tick = s.genTo;
                ev.cls = 1;
                ev.type = EvTempo;
                ev.owner = s.id;
                ev.d = tin->tempo;
                push(ev);
            }
            if (composes) s.composer.seek(st, endLimit, rng_, sink, off);
        }
        const int32_t stB = int32_t(st + (wEnd - s.genTo));
        for (const BandEvent& b : seg.bands)
            if (b.band && b.time >= st && b.time < stB) sendBand(*b.band, off + b.time, s.id);
        for (const TempoEvent& te : seg.tempos)
            if (te.time >= st && te.time < stB) {
                Event ev{};
                ev.tick = off + te.time;
                ev.cls = 1;
                ev.type = EvTempo;
                ev.owner = s.id;
                ev.d = te.tempo;
                push(ev);
            }
        if (composes) s.composer.play(st, stB, endLimit, rng_, sink, off);
        s.genTo = wEnd;
    }
}

void Performance::generate(int64_t horizon) {
    // Interleave the states by time so the random draws follow the playback order.
    for (int guard = 0; guard < 10000; ++guard) {
        SegState* next = nullptr;
        for (const StatePtr& s : states_) {
            if (s->dead) continue;
            const int64_t lim = std::min(horizon, s->end());
            if (s->genTo >= lim) continue;
            if (!next || s->genTo < next->genTo) next = s.get();
        }
        if (!next) break;
        const int64_t lim = std::min(horizon, next->end());
        generateState(*next, std::min(lim, next->genTo + 768));
    }
    // Forget states long finished.
    const int64_t t = now();
    states_.erase(std::remove_if(states_.begin(), states_.end(),
                                 [&](const StatePtr& s) { return s->end() + 400000 < t && s->genTo >= s->end(); }),
                  states_.end());
}

void Performance::dispatch(const Event& e) {
    SegState* o = e.owner ? stateById(e.owner) : nullptr;
    if (o && e.type != EvStop && e.type != EvNoteOff && e.type != EvCutAll) {
        if (o->dead || (o->stopped && e.tick >= o->stopTick)) return;
    }
    switch (e.type) {
        case EvNoteOn: {
            // A note composed so late that its note-off has passed too makes no sound.
            if (double(e.offTick) < tickOfSample(double(samplePos_))) return;
            const int key = e.b + transpose_[e.a & 31];
            if (key < 0 || key > 127) return;
            synth_.noteOn(e.a, key, e.c);
            sounding_[e.note] = {e.a, key, e.owner};
            break;
        }
        case EvNoteOff: {
            auto it = sounding_.find(e.note);
            if (it == sounding_.end()) return;
            synth_.noteOff(it->second.pch, it->second.key);
            sounding_.erase(it);
            break;
        }
        case EvCC: synth_.controller(e.a, e.b, e.c); break;
        case EvProgram: synth_.programChange(e.a, e.b); break;
        case EvBend: synth_.pitchBend(e.a, e.c); break;
        case EvPressure: synth_.channelPressure(e.a, e.c); break;
        case EvTempo: setTempo(e.d, double(e.tick)); break;
        case EvTranspose: transpose_[e.a & 31] = e.b; break;
        case EvPriority: synth_.setChannelPriority(e.a, uint32_t(e.b)); break;
        case EvStop: {
            if (!o || o->dead) return;
            o->dead = true;
            for (auto it = sounding_.begin(); it != sounding_.end();) {
                if (it->second.owner == o->id) {
                    synth_.noteOff(it->second.pch, it->second.key);
                    it = sounding_.erase(it);
                } else {
                    ++it;
                }
            }
            for (const SegState::Reset& r : o->resets)
                if (e.tick < r.until) {
                    if (r.type == 3) synth_.pitchBend(r.pch, r.value);
                    else if (r.type == 4) synth_.controller(r.pch, r.cc, r.value);
                }
            o->resets.clear();
            break;
        }
        case EvCutAll:
            for (auto it = sounding_.begin(); it != sounding_.end();) {
                if (e.owner && it->second.owner == e.owner) {
                    ++it;  // the new primary's own notes that started early
                    continue;
                }
                synth_.noteOff(it->second.pch, it->second.key);
                it = sounding_.erase(it);
            }
            break;
        case EvTrace: {
            const size_t bar = e.text.find('|');
            const std::string pat = bar == std::string::npos ? e.text : e.text.substr(bar + 1);
            if (o) o->pattern = pat;
            if (trace) {
                PerformanceTrace t;
                t.tick = e.tick;
                t.segment = o ? o->seg->name : std::string();
                t.style = bar == std::string::npos ? std::string() : e.text.substr(0, bar);
                t.pattern = pat;
                trace(t);
            }
            break;
        }
        default: break;
    }
}

void Performance::render(int16_t* out, int frames) {
    int done = 0;
    auto dispatchDue = [&]() {
        while (!queue_.empty()) {
            const double s = std::ceil(sampleOfTick(double(queue_.begin()->tick)) - 1e-6);
            if (s > double(samplePos_)) break;
            Event e = *queue_.begin();
            queue_.erase(queue_.begin());
            dispatch(e);
        }
    };
    while (done < frames) {
        // Compose up to the present + the prepare time.
        const double prepareTicks = prepareMs_ * tempo_ * 768.0 / 60000.0;
        generate(int64_t(std::ceil(presentTick() + prepareTicks)));
        dispatchDue();
        int64_t next = samplePos_ + (frames - done);
        if (!queue_.empty()) {
            const int64_t s = int64_t(std::ceil(sampleOfTick(double(queue_.begin()->tick)) - 1e-6));
            next = std::min(next, std::max(samplePos_ + 1, s));
        }
        next = std::min(next, samplePos_ + 441);  // keep composing ahead in steps of 20 ms
        const int n = int(next - samplePos_);
        synth_.render(out + size_t(done) * 2, n);
        samplePos_ += n;
        done += n;
    }
    dispatchDue();
}

std::string Performance::nowPlaying() const {
    const SegState* p = primaryNow();
    if (!p || p->dead) return std::string();
    return p->seg->name + (p->pattern.empty() ? "" : " · " + p->pattern);
}

}  // namespace music
}  // namespace cc
