#include "game/progression.h"

#include <cmath>

namespace cc {
namespace game {

const char* const kRecordCities[2] = {"sf", "london"};

bool keepsRecords(const std::string& raceDir) {
    // MM2 compares with its two built-in names (the RaceDir as written in the .cinfo).
    return raceDir == "sf" || raceDir == "london";
}

int CityRaces::count(int t) const {
    switch (t) {
        case RecCheckpoint: return checkpoint;
        case RecCircuit: return circuit;
        case RecBlitz: return blitz;
        case RecCrash: return kCrashCourseCount;
        default: return 0;
    }
}

const std::vector<std::string>* CityRaces::names(int t) const {
    switch (t) {
        case RecCheckpoint: return &checkpointNames;
        case RecCircuit: return &circuitNames;
        case RecBlitz: return &blitzNames;
        default: return nullptr;
    }
}

namespace {
bool bit(uint32_t m, int r) { return r >= 0 && r < 32 && (m & (1u << r)) != 0; }
bool allBits(uint32_t m, uint32_t want) { return (m & want) == want; }
}  // namespace

uint32_t openCheckpointMask(const RecordFile* rec) {
    if (!rec) return ~0u;
    const uint32_t p = rec->passed[RecCheckpoint];
    uint32_t open = 0x007;
    if (allBits(p, 0x007)) open = 0x03F;
    if (allBits(p, 0x038)) open |= 0x1C0;
    if (allBits(p, 0x1C0)) open |= 0xE00;
    return open;
}

uint32_t openCrashMask(const RecordFile* rec) {
    if (!rec) return ~0u;
    const uint32_t p = rec->passed[RecCrash];
    uint32_t open = 0x777;
    if (allBits(p, 0x007)) open = 0x77F;
    if (allBits(p, 0x070)) open |= 0x080;
    if (allBits(p, 0x700)) open |= 0x800;
    if (allBits(p, 0xFFF)) open = ~0u;
    return open;
}

bool raceOpen(const RecordFile* rec, int t, int race) {
    if (!rec) return true;
    if (t == RecCheckpoint) return race < 32 && bit(openCheckpointMask(rec), race);
    if (t == RecCrash) {
        const uint32_t m = openCrashMask(rec);
        return m == ~0u || bit(m, race);
    }
    return true;  // circuit and blitz races are always open
}

int crashCourseState(const RecordFile* rec, int course) {
    if (!rec) return 0;
    const RaceRecord* r = rec->record(RecCrash, course);
    if (!r || r->time == 0.0f) return 0;
    return r->passed ? 1 : 2;
}

bool raceSettingsLocked(const RecordFile* rec, int t, int race) {
    if (!rec || t < 0 || t > RecBlitz) return false;
    return !bit(rec->passed[t], race);
}

// ---------------------------------------------------------------------------------------------

void Locks::unlock(const RewardLine& l) {
    if (l.variant == 0) {
        cars.erase(l.car);
        carWhy.erase(l.car);
    } else {
        paints.erase({l.car, l.variant});
        paintWhy.erase({l.car, l.variant});
    }
}

namespace {
bool conditionMet(const RewardLine& l, const RecordFile& rec, bool exactHalf) {
    const int t = recordTypeOfMode(l.mode);
    if (t < 0) return false;
    const int n = rec.counts[t];
    const int p = rec.passedCount(t);
    if (l.condition == RewardLine::kHalf) return exactHalf ? p == n / 2 : p >= n / 2;
    if (l.condition == RewardLine::kAll) return p == n;
    return bit(rec.passed[t], l.condition);
}
}  // namespace

bool lockConditionMet(const RewardLine& l, const RecordFile& rec) { return conditionMet(l, rec, false); }
bool rewardConditionNow(const RewardLine& l, const RecordFile& rec) { return conditionMet(l, rec, true); }

Locks computeLocks(const std::vector<CityRewards>& cities) {
    Locks k;
    for (const CityRewards& c : cities) {
        if (!c.rewards || !c.record) continue;
        for (const RewardLine& l : *c.rewards) {
            if (lockConditionMet(l, *c.record)) continue;
            if (l.variant == 0) {
                k.cars.insert(l.car);
                k.carWhy.insert({l.car, LockReason{l, c.city}});
            } else {
                k.paints.insert({l.car, l.variant});
                k.paintWhy.insert({{l.car, l.variant}, LockReason{l, c.city}});
            }
        }
    }
    return k;
}

// ---------------------------------------------------------------------------------------------

int placeScore(float bias, int place, float difficulty, bool blitz) {
    int F = 0;
    if (blitz) F = 50;
    else if (place == 1) F = 50;
    else if (place == 2) F = 25;
    else if (place == 3) F = 10;
    return int(std::trunc(bias)) * F * int(std::trunc(difficulty));
}

bool placePasses(int place, int skill) { return skill == 0 ? (place >= 1 && place <= 3) : place == 1; }

bool resultRecord(const RaceResult& r, int skill, bool alreadyPassed, RaceRecord& out) {
    out = RaceRecord();
    out.car = r.car;
    switch (r.mode) {
        case ModeCheckpoint:
            if (!r.finished || !r.ownSettings) return false;
            out.time = r.time;
            out.passed = (placePasses(r.place, skill) || alreadyPassed) ? 1 : 0;
            out.score = placeScore(r.scoringBias, r.place, r.difficulty, false);
            return true;
        case ModeCircuit: {
            if (!r.finished || !r.ownSettings) return false;
            float best = r.time;
            for (size_t i = 0; i < r.laps.size(); ++i)
                if (i == 0 || r.laps[i] < best) best = r.laps[i];
            out.time = best;
            out.passed = placePasses(r.place, skill) ? 1 : 0;
            out.score = placeScore(r.scoringBias, r.place, r.difficulty, false);
            return true;
        }
        case ModeBlitz:
            if (!r.finished || !r.ownSettings || r.race >= 12) return false;
            out.time = r.time;
            out.passed = 1;
            out.score = placeScore(r.scoringBias, 1, r.difficulty, true);
            return true;
        case ModeCrash:
            if (r.race > 12) return false;
            out.time = 1.0f;
            out.passed = r.crashPassed ? 1 : 0;
            out.score = 1;
            return true;
        default:
            return false;
    }
}

void addToRecordFile(RecordFile& f, int t, int race, const RaceRecord& nw) {
    RaceRecord* old = f.record(t, race);
    if (!old) return;
    if (nw.passed && race >= 0 && race < 32) f.passed[t] |= 1u << race;
    if (old->time == 0.0f) {
        *old = nw;
        old->crcOk = true;
        return;
    }
    RaceRecord m = *old;
    if (nw.time < old->time) {
        m.time = nw.time;
        m.car = nw.car;
    }
    m.score = nw.score > old->score ? nw.score : old->score;
    m.passed = (nw.passed || old->passed) ? 1 : 0;
    m.crcOk = true;
    *old = m;
}

bool addToHallOfFame(HallOfFame& h, int t, int race, const std::string& driver, const std::string& car, float time,
                     int score, bool passed) {
    const int base = h.entryIndex(t, race, 0);
    if (base < 0) return false;
    FameEntry* e = &h.entries[size_t(base)];
    // From the first entry whose check value fails, it and every later one count as empty.
    bool broken = false;
    for (int i = 0; i < 10; ++i) {
        if (!e[i].crcOk) broken = true;
        if (broken) {
            e[i] = FameEntry();
            e[i].crcOk = true;
        }
    }
    FameEntry nw;
    nw.name = driver;
    nw.car = car;
    nw.passed = passed ? 1u : 0u;
    bool changed = false;
    // Time table: entries 0, 2, 4, 6, 8.
    for (int k = 0; k < 5; ++k) {
        const FameEntry& at = e[2 * k];
        if (at.value == 0.0f || at.value > time) {
            for (int j = 4; j > k; --j) e[2 * j] = e[2 * (j - 1)];
            e[2 * k] = nw;
            e[2 * k].value = time;
            changed = true;
            break;
        }
    }
    // Score table: entries 1, 3, 5, 7, 9; a score of 0 never enters.
    if (score != 0) {
        for (int k = 0; k < 5; ++k) {
            const FameEntry& at = e[2 * k + 1];
            if (int(std::trunc(at.value)) < score) {
                for (int j = 4; j > k; --j) e[2 * j + 1] = e[2 * (j - 1) + 1];
                e[2 * k + 1] = nw;
                e[2 * k + 1].value = float(score);
                changed = true;
                break;
            }
        }
    }
    return changed;
}

const RewardLine* findReward(const std::vector<RewardLine>& rewards, int mode, const RecordFile& rec, Locks& locks) {
    for (const RewardLine& l : rewards) {
        if (l.mode != mode) continue;
        if (!rewardConditionNow(l, rec)) continue;
        const bool locked = l.variant == 0 ? locks.carLocked(l.car) : locks.paintLocked(l.car, l.variant);
        if (!locked) continue;
        locks.unlock(l);
        return &l;
    }
    return nullptr;
}

void addTotals(const RecordFile& rec, Totals& t) {
    for (int ty = 0; ty < 3; ++ty)
        for (int r = 0; r < rec.counts[ty]; ++r) {
            const RaceRecord* e = rec.record(ty, r);
            if (!e) continue;
            t.score[ty] += e->score;
            if (e->passed == 1) ++t.passed[ty];
            t.total += e->score;
        }
}

}  // namespace game
}  // namespace cc
