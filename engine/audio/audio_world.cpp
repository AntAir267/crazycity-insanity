#include "audio/audio_world.h"

#include "core/log.h"
#include "vfs/vfs.h"

namespace cc {
namespace audio {

void ImpactTable::build(SoundSystem& sys, const ImpactData& d) {
    data_ = d;
    ok_ = d.ok;
    entries_.clear();
    if (!d.ok) return;
    for (const auto& e : d.entries) {
        Entry en;
        en.name = e.first;
        for (const ImpactRowData& r : e.second) {
            Row row;
            row.d = r;
            const bool pitch = r.freq != 1.0f;
            const std::string path = soundPath("impacts\\" + r.name);
            for (auto& s : row.smp) {
                s = std::make_unique<Sample>(sys, pitch, 1, 0.0f);
                s->setFile(0, path);
            }
            en.rows.push_back(std::move(row));
        }
        entries_.push_back(std::move(en));
    }
}

int ImpactTable::play(int slot, float force, int id, bool threeD, float aPrime, float pan) {
    lastStarted_ = 0;
    if (id > 1000) id = 0;
    if (id == -1) return -1;
    if (entries_.empty()) return -1;
    const int e = (id < 0 || id >= int(entries_.size())) ? 0 : id;
    for (Row& r : entries_[size_t(e)].rows) {
        if (!(r.d.minF <= force && force <= r.d.maxF)) continue;
        if (slot < 0 || slot >= Manager3D::kSlots) continue;
        Sample* s = r.smp[size_t(slot)].get();
        if (!s || s->playing()) continue;
        const float v = r.d.slope * force + r.d.minV;
        s->setVolume(threeD ? v * aPrime : v);
        s->setPan(pan);
        s->play();
        r.lastV[slot] = v;
        ++lastStarted_;
    }
    return e;
}

void ImpactTable::refresh(int slot, int entry, float aPrime, float pan) {
    if (entry < 0 || entry >= int(entries_.size()) || slot < 0 || slot >= Manager3D::kSlots) return;
    for (Row& r : entries_[size_t(entry)].rows) {
        Sample* s = r.smp[size_t(slot)].get();
        if (!s || !s->playing()) continue;
        s->setVolume(r.lastV[slot] * aPrime);
        s->setPan(pan);
    }
}

void ImpactTable::slotAssigned(int slot) {
    if (slot < 0 || slot >= Manager3D::kSlots) return;
    for (Entry& e : entries_)
        for (Row& r : e.rows)
            if (r.d.freq != 1.0f && r.smp[size_t(slot)]) r.smp[size_t(slot)]->setPitch(r.d.freq);
}

AudioWorld::AudioWorld(SoundSystem& s, const Vfs& v) : sys(s), vfs(v), cache(s) {}

AudioWorld::~AudioWorld() = default;

void AudioWorld::resetImpactCache() {
    carTables_.clear();
    trafficTable_.reset();
    impactCache_.clear();
}

ImpactTable* AudioWorld::trafficImpactTable() {
    if (!trafficTable_) {
        // Loaded once per game when the first traffic car is set up; recorded in the cache by its
        // name with the traffic's table number, which is always 0.
        trafficTable_ = std::make_unique<ImpactTable>();
        trafficTable_->build(sys, loadImpacts(vfs, "opponent"));
        impactCache_["default_impacts"] = 0;
    }
    return trafficTable_->ok() ? trafficTable_.get() : nullptr;
}

ImpactTable* AudioWorld::carImpactTable(bool playerFolder) {
    auto it = impactCache_.find("default_impacts");
    if (it != impactCache_.end()) {
        // Found (some traffic car was set up earlier in this game): the car takes the recorded
        // number as its car-table number, i.e. the first car table (the player's).
        if (carTables_.empty() || !carTables_[0]->ok()) return nullptr;
        return carTables_[0].get();
    }
    auto t = std::make_unique<ImpactTable>();
    t->build(sys, loadImpacts(vfs, playerFolder ? "player" : "opponent"));
    if (!t->ok())
        logFile(LogLevel::Warn, std::string("aud/cardata/") + (playerFolder ? "player" : "opponent") + "/default_impacts.csv",
                "no impact table (missing, or no ENDOFDATA entry): no impact sounds");
    carTables_.push_back(std::move(t));
    return carTables_.back()->ok() ? carTables_.back().get() : nullptr;
}

void AudioWorld::loadSharedCarFiles() {
    types = loadVehTypes(vfs);
    surface = loadSurfaceFile(vfs, weather == 3);
    surfaceSamples.clear();
    for (const SurfaceBlock& b : surface.blocks) {
        SurfaceSamples s;
        if (b.hasRolling) s.rolling = cache.get("surfaces\\" + b.rolling, true);
        for (const auto& sk : b.skids) s.skids.push_back(sk.hasSample ? cache.get("surfaces\\" + sk.wave, false) : SlotSample());
        surfaceSamples.push_back(s);
    }
    suspension = loadSuspension(vfs);
    if (suspension.found) suspensionSample = cache.get("suspension\\" + suspension.wave, true);
    wobble = loadWobble(vfs);
    if (wobble.found) wobbleSample = cache.get(wobble.wave, true);
}

const SirenData& AudioWorld::sirenData() {
    if (!sirenRead) {
        siren = loadSiren(vfs, london);
        sirenRead = true;
        if (!siren.found) logFile(LogLevel::Warn, london ? "aud/cardata/player/londonpolicesiren.csv" : "aud/cardata/player/sfpolicesiren.csv",
                                  "no siren file: police cars have no siren sound");
    }
    return siren;
}

const SemiData& AudioWorld::semiData() {
    if (!semiRead) {
        semi = loadSemiData(vfs);
        semiRead = true;
    }
    return semi;
}

}  // namespace audio
}  // namespace cc
