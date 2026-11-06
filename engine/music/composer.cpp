#include "music/composer.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace cc {
namespace music {

const Subchord& Chord::forLevel(uint8_t level) const {
    for (const Subchord& s : subchords)
        if (level < 32 && (s.levels & (1u << level))) return s;
    return subchords.front();
}

const Chord& defaultChord() {
    static const Chord c;
    return c;
}

namespace {

// The scale as 32 bits, bit 0 = the scale root ("Music values", step 4).
uint32_t scale32(const Subchord& sc) {
    uint32_t p = sc.scalePattern & 0xFFF;
    if (p == 0) p = 0xAB5;
    const int root = sc.scaleRoot % 12;
    uint32_t r = root ? (((p >> root) | (p << (12 - root))) & 0xFFF) : p;
    uint32_t s24 = r | (r << 12);
    return s24 | ((s24 >> 12) & 0xFF) << 24;
}

bool scaleBit(uint32_t s, int i) { return i >= 32 || (s >> i) & 1u; }

// Semitones walked up the scale from bit `start` (counting the start as a tone when it is one)
// until the n-th scale tone (n ≥ 1).
int walk(uint32_t s, int start, int n) {
    int count = 0, p = start;
    for (int guard = 0; guard < 400; ++guard, ++p) {
        if (scaleBit(s, p)) ++count;
        if (count >= n) return p - start;
    }
    return p - start;
}

}  // namespace

int musicValueToKey(uint16_t value, uint8_t playMode, uint8_t invLower, uint8_t invUpper, const Subchord& sc,
                    bool* outside) {
    if (outside) *outside = false;
    int key;
    if (playMode == 0) {
        key = value;
        while (key > 127) key -= 12;
        return key;
    }
    int o = 0;
    uint16_t v = value;
    while (v >= 0xE000) {
        v = uint16_t(v + 0x1000);
        o -= 12;
    }
    const int octave = v >> 12;
    const int c = (v >> 8) & 15;
    int s = (v >> 4) & 15;
    int a = v & 15;
    if (s >= 9) {
        s -= 9;
        o = -12;
    }
    s &= 7;
    if (a >= 9) a -= 16;
    const uint32_t scale = scale32(sc);
    const int R = sc.chordRoot;
    uint32_t chord = sc.chordPattern ? sc.chordPattern : 1;
    int tones[32], nt = 0;
    for (int b = 0; b < 32; ++b)
        if (chord & (1u << b)) tones[nt++] = b;
    int r;
    const int sroot = sc.scaleRoot % 12;
    if (c < nt) {
        const int t = tones[c];
        if (s == 0) {
            r = R + t + a;
        } else {
            const int start = (((t + R) % 12) - sroot + 12) % 12;
            r = R + t + walk(scale, start, s + 1) + a;
        }
    } else {
        const int start = ((R % 12) - sroot + 12) % 12;
        r = R + walk(scale, start, s + 2 * c + 1) + a;
    }
    key = r + o + 12 * octave - 12;
    if (key < 0 || key > 127) {
        if (outside) *outside = true;
        return 0;
    }
    if (invLower <= invUpper && invUpper - invLower >= 11) {
        while (key < invLower) key += 12;
        while (key > invUpper) key -= 12;
    }
    while (key > 127) key -= 12;
    while (key < 0) key += 12;
    return key;
}

std::vector<int> allowedVariations(const Part& part) {
    std::vector<int> out;
    for (int v = 0; v < 32; ++v) {
        uint32_t c = part.choices[v];
        if (c == 0xFFFFFFFFu) c = 0x3FFFFFFFu;
        const uint32_t m = (c & (1u << 29)) ? 0x02200001u : 0x00002101u;
        if ((c & m) == m) out.push_back(v);
    }
    if (out.empty())
        for (int v = 0; v < 32; ++v) {
            uint32_t c = part.choices[v];
            if (c == 0xFFFFFFFFu) c = 0x3FFFFFFFu;
            if (c & 0x5FFFFFFFu) out.push_back(v);
        }
    if (out.empty())
        for (int v = 0; v < 16; ++v) out.push_back(v);
    return out;
}

// ---- the style track

void StyleComposer::init(const Segment* seg) {
    seg_ = seg;
    motif_ = seg && seg->motif;
    style_ = seg ? seg->styleAt(0) : nullptr;
    pat_ = lastPat_ = nullptr;
    patStart_ = patEnd_ = 0;
}

TimeSig StyleComposer::partSig(const Part& p) const { return p.sig.beatValue ? p.sig : style_->sig; }

int StyleComposer::partLength(const Part& p) const {
    return int(p.lengthMeasures ? p.lengthMeasures : 1) * partSig(p).measureTicks();
}

namespace {

// The command in force at segment time t and whether one lies exactly there.
const CommandEvent* commandAt(const Segment& s, int32_t t, bool& exact) {
    const CommandEvent* c = nullptr;
    exact = false;
    for (const CommandEvent& e : s.commands) {
        if (e.time > t) break;
        c = &e;
    }
    if (c && c->time == t) exact = true;
    return c;
}

bool kindMatches(uint16_t emb, int kind) {
    if (emb & 0xFF00) return emb == kind;
    switch (kind) {
        case CmdGroove: return emb == 0;
        case CmdEnd: return (emb & EmbEnd) != 0;
        case CmdFill: return (emb & EmbFill) != 0;
        case CmdIntro: return (emb & EmbIntro) != 0;
        case CmdBreak: return (emb & EmbBreak) != 0;
        default: return false;
    }
}

}  // namespace

void StyleComposer::choose(int32_t t, int32_t endLimit, Lcg& rng, ComposerSink& sink, int64_t off, bool silent) {
    const Style* st = seg_->styleAt(t);
    if (st) style_ = st;
    if (!style_ || style_->patterns.empty()) {
        pat_ = nullptr;
        patStart_ = t;
        patEnd_ = t + 3072;
        return;
    }
    if (motif_) {
        pat_ = seg_->motif;
        patStart_ = 0;
        patEnd_ = seg_->length;
        chooseVariations(rng, pat_ == lastPat_);
        lastPat_ = pat_;
        if (!silent) sink.patternChosen(off + t, *style_, *pat_, silent);
        return;
    }
    const int measure = style_->sig.measureTicks();
    int longest = 1;
    for (const Pattern& p : style_->patterns)
        if (!p.isMotif()) longest = std::max(longest, int(p.lengthMeasures));
    int A = (endLimit - t) / measure;
    A = std::clamp(A, 1, longest);
    int32_t nextCmd = endLimit;
    bool haveNext = false;
    for (const CommandEvent& e : seg_->commands)
        if (e.time > t) {
            nextCmd = e.time;
            haveNext = true;
            break;
        }
    int C = (nextCmd - t) / measure;
    if (C < 0) C = 0;
    std::vector<int> level(static_cast<size_t>(A)), kind(static_cast<size_t>(A));
    for (int i = 0; i < A; ++i) {
        bool exact = false;
        const CommandEvent* c = commandAt(*seg_, t + i * measure, exact);
        level[size_t(i)] = c ? std::clamp(int(c->groove), 1, 100) : 62;
        kind[size_t(i)] = (c && exact) ? int(c->command) : int(CmdGroove);
    }
    auto matches = [&](const Pattern& p, int n) {
        if (p.isMotif()) return false;
        const int m = std::min(n, int(p.lengthMeasures ? p.lengthMeasures : 1));
        for (int i = 0; i < m && i < A; ++i) {
            if (!(i == 0 || kind[size_t(i)] != CmdGroove || level[size_t(i)] != level[0])) continue;
            if (!kindMatches(p.embellishment, kind[size_t(i)])) return false;
            const int lv = level[size_t(i)];
            const int h = 0;  // groove range ÷ 2 (0 in MM2)
            const int lo = lv < h ? 1 : lv - h;
            const int hi = std::min(lv + h, 100);
            if (!(p.grooveBottom <= hi && p.grooveTop >= lo)) return false;
        }
        return true;
    };
    // Groups of equal length, longest first (and at equal length the longest measure first).
    std::vector<int> order;
    for (size_t i = 0; i < style_->patterns.size(); ++i) order.push_back(int(i));
    std::stable_sort(order.begin(), order.end(), [&](int x, int y) {
        const Pattern& a = style_->patterns[size_t(x)];
        const Pattern& b = style_->patterns[size_t(y)];
        if (a.lengthMeasures != b.lengthMeasures) return a.lengthMeasures > b.lengthMeasures;
        return a.sig.measureTicks() > b.sig.measureTicks();
    });
    std::vector<const Pattern*> cand;
    for (int test = 1; test <= 3 && cand.empty(); ++test) {
        size_t g = 0;
        while (g < order.size() && cand.empty()) {
            size_t h = g;
            const Pattern& first = style_->patterns[size_t(order[g])];
            while (h < order.size() && style_->patterns[size_t(order[h])].lengthMeasures == first.lengthMeasures &&
                   style_->patterns[size_t(order[h])].sig.measureTicks() == first.sig.measureTicks())
                ++h;
            // file order inside the group
            std::vector<int> grp(order.begin() + long(g), order.begin() + long(h));
            std::sort(grp.begin(), grp.end());
            for (int pi : grp) {
                const Pattern& p = style_->patterns[size_t(pi)];
                const int len = p.lengthMeasures ? p.lengthMeasures : 1;
                bool ok;
                if (test == 3) ok = matches(p, 1);
                else ok = matches(p, A) && len <= A && (C == 0 || len <= C) && (test == 2 || p.rhythmBits() > 0);
                if (ok) cand.push_back(&p);
            }
            if (test == 1 && !cand.empty()) {
                int fewest = 1 << 30;
                for (const Pattern* p : cand) fewest = std::min(fewest, p->rhythmBits());
                std::vector<const Pattern*> keep;
                for (const Pattern* p : cand)
                    if (p->rhythmBits() == fewest) keep.push_back(p);
                cand.swap(keep);
            }
            g = h;
        }
    }
    if (cand.empty())
        for (const Pattern& p : style_->patterns)
            if (!p.isMotif()) cand.push_back(&p);
    if (cand.empty()) {
        pat_ = nullptr;
        patStart_ = t;
        patEnd_ = t + measure;
        return;
    }
    const int pick = rng.next15() % int(cand.size());
    pat_ = cand[size_t(pick)];
    patStart_ = t;
    const int32_t len = int32_t(pat_->lengthMeasures ? pat_->lengthMeasures : 1) * pat_->sig.measureTicks();
    patEnd_ = t + len;
    const int32_t cap = haveNext ? nextCmd : endLimit;
    if (patEnd_ > cap) patEnd_ = cap;
    if (patEnd_ <= t) patEnd_ = t + measure;
    chooseVariations(rng, pat_ == lastPat_);
    lastPat_ = pat_;
    if (!silent) sink.patternChosen(off + t, *style_, *pat_, silent);
}

void StyleComposer::chooseVariations(Lcg& rng, bool samePattern) {
    const size_t n = pat_->parts.size();
    if (!samePattern || var_.size() != n) var_.assign(n, -1);
    int lock[256];
    bool locked[256] = {};
    for (size_t i = 0; i < n; ++i) {
        const PartRef& ref = pat_->parts[i];
        if (ref.part < 0) {
            var_[i] = 0;
            continue;
        }
        const Part& part = style_->parts[size_t(ref.part)];
        if (ref.lockId != 0 && locked[ref.lockId]) {
            var_[i] = lock[ref.lockId];
            continue;
        }
        const std::vector<int> allowed = allowedVariations(part);
        int v;
        if (ref.varMode == 0) {
            // In order: the next allowed after the current one (cyclic).
            v = allowed.front();
            for (int a : allowed)
                if (a > var_[i]) {
                    v = a;
                    break;
                }
        } else {
            v = allowed[size_t(rng.next15() % int(allowed.size()))];
        }
        var_[i] = v;
        if (ref.lockId != 0) {
            lock[ref.lockId] = v;
            locked[ref.lockId] = true;
        }
    }
}

namespace {

int randomRange(Lcg& rng, int x) {
    int w;
    if (x < 191) w = x;
    else if (x <= 212) w = (x - 152) * 5;
    else if (x <= 232) w = (x - 182) * 10;
    else w = (x - 222) * 50;
    if (w <= 0) return 0;
    return rng.next15() % w - w / 2;
}

int curveTable(int shape, int i) {
    switch (shape) {
        case 0: return 100 * i;  // linear
        case 3: return int(std::trunc(19200.0 * std::log10(1.0 + 9.0 * i / 192.0)));
        case 2: return 19200 - int(std::trunc(19200.0 * std::log10(1.0 + 9.0 * (192 - i) / 192.0)));
        case 4: return int(std::trunc(9600.0 * (1.0 - std::cos(3.14159265358979323846 * i / 192.0))));
        default: return 100 * i;
    }
}

int curveValue(const Curve& c, int64_t e) {
    if (c.duration <= 0) return c.endValue;
    const int64_t i = e * 193 / c.duration;
    if (i >= 192) return c.endValue;
    return c.startValue + int((int64_t(c.endValue - c.startValue) * curveTable(c.shape, int(i))) / 19200);
}

int clampCurve(const Curve& c, int v) {
    if (c.type == 3) return std::clamp(v, 0, 16383);
    if (c.type == 4) {
        if (c.cc == 1 || c.cc == 10 || c.cc == 91 || c.cc == 93) return std::clamp(v, 0, 127);
        if (c.cc == 7 || c.cc == 11) return v & 0xFF;
    }
    return v;
}

}  // namespace

void StyleComposer::emitCurve(const Curve& c, int64_t start, int64_t from, int pch, ComposerSink& sink) {
    auto send = [&](int64_t tick, int v) { sink.control(tick, c.type, pch, c.cc, clampCurve(c, v)); };
    if (c.shape == 1 || c.duration <= 0) {
        send(std::max(start, from), c.endValue);
    } else {
        const int diff = std::abs(int(c.endValue) - int(c.startValue));
        int64_t step = int64_t(c.duration) * (c.type == 3 ? 100 : 3) / (diff ? diff : 1);
        if (step < 16) step = 16;
        for (int64_t e = 0; e < c.duration; e += step)
            if (start + e >= from) send(start + e, curveValue(c, e));
            else if (start + e + step > from && from < start + c.duration) send(from, curveValue(c, from - start));
        send(start + c.duration, c.endValue);
    }
    if (c.flags & 1) sink.curveReset(start + c.duration + c.resetDuration, c.type, pch, c.cc, clampCurve(c, c.resetValue));
}

void StyleComposer::seek(int32_t pos, int32_t endLimit, Lcg& rng, ComposerSink& sink, int64_t off) {
    if (!seg_) return;
    if (motif_) {
        choose(0, endLimit, rng, sink, off, false);
        return;
    }
    int32_t t = 0;
    for (int guard = 0; guard < 100000; ++guard) {
        choose(t, endLimit, rng, sink, off, true);
        if (patEnd_ > pos) break;
        t = patEnd_;
    }
    if (pat_) sink.patternChosen(off + pos, *style_, *pat_, false);
    if (pat_ && pos > patStart_) resendCurves(pos, sink, off);
}

void StyleComposer::resendCurves(int32_t entry, ComposerSink& sink, int64_t off) {
    for (size_t r = 0; r < pat_->parts.size(); ++r) {
        const PartRef& ref = pat_->parts[r];
        if (ref.part < 0 || r >= var_.size()) continue;
        const Part& part = style_->parts[size_t(ref.part)];
        if (part.curves.empty()) continue;
        const TimeSig ps = partSig(part);
        const int plen = partLength(part);
        std::map<int, std::pair<int64_t, const Curve*>> last;  // kind -> (start, curve)
        for (int32_t rep = patStart_; rep < entry && plen > 0; rep += plen)
            for (const Curve& c : part.curves) {
                if (!(c.variations & (1u << var_[r]))) continue;
                const int32_t g = rep + (c.grid / ps.grids) * ps.beatTicks() + (c.grid % ps.grids) * ps.gridTicks();
                const int64_t start = int64_t(g) + c.offset;
                if (g >= patEnd_ || start >= entry) continue;
                const int k = c.type * 256 + (c.type == 4 ? c.cc : 0);
                auto it = last.find(k);
                if (it == last.end() || start >= it->second.first) last[k] = {start, &c};
            }
        for (const auto& kv : last) {
            const Curve& c = *kv.second.second;
            const int64_t start = kv.second.first;
            if (start + c.duration <= entry) {
                sink.control(off + entry, c.type, ref.pchannel, c.cc, clampCurve(c, c.endValue));
            } else {
                emitCurve(c, off + start, off + entry, ref.pchannel, sink);
            }
        }
    }
}

void StyleComposer::playWindow(int32_t a, int32_t b, Lcg& rng, ComposerSink& sink, int64_t off) {
    if (!pat_) return;
    const Subchord& chord = defaultChord().forLevel(0);
    for (size_t r = 0; r < pat_->parts.size(); ++r) {
        const PartRef& ref = pat_->parts[r];
        if (ref.part < 0 || r >= var_.size()) continue;
        const Part& part = style_->parts[size_t(ref.part)];
        const TimeSig ps = partSig(part);
        const int plen = partLength(part);
        if (plen <= 0) continue;
        const uint32_t vbit = 1u << var_[r];
        const Subchord& sc = defaultChord().forLevel(ref.subchord);
        (void)chord;
        for (int32_t rep = patStart_; rep < patEnd_ && rep < b; rep += plen) {
            for (const Note& n : part.notes) {
                if (!(n.variations & vbit)) continue;
                const int32_t g = rep + (n.grid / ps.grids) * ps.beatTicks() + (n.grid % ps.grids) * ps.gridTicks();
                if (g < a || g >= b || g >= patEnd_ || g < patStart_) continue;
                int64_t time = int64_t(g) + n.offset;
                if (n.timeRange) time += randomRange(rng, n.timeRange);
                int dur = n.duration;
                if (n.durRange) dur += randomRange(rng, n.durRange);
                int vel = n.velocity;
                if (n.velRange) {
                    vel = (vel + rng.next15() % n.velRange - n.velRange / 2) % 256;
                    if (vel < 0) vel += 256;
                    if (vel == 0) vel = 1;
                    if (vel > 127) vel = 127;
                }
                const uint8_t pm = n.playMode == 0x10 ? part.playMode : n.playMode;
                const int key = musicValueToKey(n.value, pm, part.invLower, part.invUpper, sc);
                if (key < 0 || key > 127 || dur < 1) continue;
                const int64_t on = off + time;
                const int64_t offT = dur > 1 ? on + dur - 1 : on + 1;
                sink.note(on, offT, ref.pchannel, key, vel);
            }
            for (const Curve& c : part.curves) {
                if (!(c.variations & vbit)) continue;
                const int32_t g = rep + (c.grid / ps.grids) * ps.beatTicks() + (c.grid % ps.grids) * ps.gridTicks();
                if (g < a || g >= b || g >= patEnd_ || g < patStart_) continue;
                const int64_t start = off + int64_t(g) + c.offset;
                emitCurve(c, start, start, ref.pchannel, sink);
            }
        }
    }
}

void StyleComposer::play(int32_t a, int32_t b, int32_t endLimit, Lcg& rng, ComposerSink& sink, int64_t off) {
    if (!seg_) return;
    int32_t pos = a;
    for (int guard = 0; pos < b && guard < 100000; ++guard) {
        if (!pat_ || pos >= patEnd_) {
            if (motif_) {
                if (pos >= patEnd_) return;  // a motif lap ends at the loop jump (seek)
            }
            choose(pat_ ? patEnd_ : pos, endLimit, rng, sink, off, false);
        }
        const int32_t w = std::min(b, patEnd_);
        if (w <= pos) break;
        playWindow(pos, w, rng, sink, off);
        pos = w;
    }
}

}  // namespace music
}  // namespace cc
