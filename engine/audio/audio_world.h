// What every sound owner of one game shares (spec/audio/*.md): the 3D manager and its sample
// cache, the game's city, weather and time of day, the vehicle types, the shared tyre files, the
// impact tables with MM2's table cache, the chase count and the semi air-brake latch.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/car_data.h"
#include "audio/sound3d.h"
#include "audio/sound_system.h"

namespace cc {
class Vfs;
namespace phys {
class World;
}

namespace audio {

// One loaded impact table (tyre-impact-sounds.md, "Impact sounds"), one sample copy per slot.
class ImpactTable {
public:
    bool ok() const { return ok_; }
    void build(SoundSystem& sys, const ImpactData& d);
    size_t entryCount() const { return entries_.size(); }
    // "Playing an impact" for a source in `slot`: returns the entry used (or -1). `threeD`: the
    // volume is v × aPrime and the pan is set; 2D: v and the source's own pan.
    int play(int slot, float force, int id, bool threeD, float aPrime, float pan);
    // Each slot update: the playing samples of the last entry follow the source.
    void refresh(int slot, int entry, float aPrime, float pan);
    // On getting a slot: the pitch of the samples with a frequency other than 1.
    void slotAssigned(int slot);
    // Samples started by the last play().
    int lastStarted() const { return lastStarted_; }
    const ImpactData& data() const { return data_; }

private:
    struct Row {
        ImpactRowData d;
        std::array<std::unique_ptr<Sample>, Manager3D::kSlots> smp;
        float lastV[Manager3D::kSlots] = {};
    };
    struct Entry {
        std::string name;
        std::vector<Row> rows;
    };
    bool ok_ = false;
    int lastStarted_ = 0;
    ImpactData data_;
    std::vector<Entry> entries_;
};

// The samples of one surface block (rolling loop and skids), from the 3D cache.
struct SurfaceSamples {
    SlotSample rolling;
    std::vector<SlotSample> skids;
};

class AudioWorld {
public:
    AudioWorld(SoundSystem& sys, const Vfs& vfs);
    ~AudioWorld();

    SoundSystem& sys;
    const Vfs& vfs;
    Manager3D mgr;
    SampleCache cache;

    std::string city;     // MapName (exact comparisons: "london", "sf")
    bool london = false;  // city name exactly "london"
    int weather = 0;      // 0 clear, 1 cloudy, 2 fog, 3 rain
    int timeOfDay = 1;    // 0 morning, 1 noon, 2 evening, 3 night
    phys::World* world = nullptr;  // the airborne probe
    float frameTime = 0.0f;        // F of the current frame

    VehTypes types;
    SurfaceFile surface;
    std::vector<SurfaceSamples> surfaceSamples;
    SuspensionData suspension;
    SlotSample suspensionSample;
    WobbleData wobble;
    SlotSample wobbleSample;
    SirenData siren;
    bool sirenRead = false;
    SemiData semi;
    bool semiRead = false;

    // Chase count (car-sounds.md, "Chase count"): read by the music.
    int chaseCount = 0;
    // The air-brake latch shared by every semi/bus car.
    bool airLatch = false;

    // Impact tables (tyre-impact-sounds.md, "Which impact table a car uses").
    ImpactTable* carImpactTable(bool playerFolder);
    ImpactTable* trafficImpactTable();
    // Starts each game with an empty table cache.
    void resetImpactCache();

    // Reads the shared car files once per game (vehicle types, surfaces, suspension, wobble).
    void loadSharedCarFiles();
    const SirenData& sirenData();
    const SemiData& semiData();

private:
    std::vector<std::unique_ptr<ImpactTable>> carTables_;
    std::unique_ptr<ImpactTable> trafficTable_;
    std::map<std::string, int> impactCache_;  // file name → table number
};

}  // namespace audio
}  // namespace cc
