// 3D sources and the 3D manager (spec/audio/sound-system.md, "3D sources"): the listener, the
// 4 slots every positioned source competes for, the slot request and eviction order, the
// attenuation / pan / doppler formulas, and the shared sample cache with one copy per slot.
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>

#include "audio/sound_system.h"
#include "core/math.h"

namespace cc {
namespace audio {

class Manager3D;

class Source3D {
public:
    Source3D() = default;
    virtual ~Source3D();
    Source3D(const Source3D&) = delete;
    Source3D& operator=(const Source3D&) = delete;

    void attach(Manager3D& m) { mgr_ = &m; }
    Manager3D* manager() const { return mgr_; }

    // Owner settings.
    std::string label;  // for the page's sound readout
    vec3 position;
    bool twoD = false;
    int priority = 0;
    float pan2D = 0.0f;  // a 2D source's own pan
    float dopplerK = 0.0f;
    bool keepsSlot = false;  // keeps its slot past the maximum drop-off (a sounding siren)
    // Drop-offs in metres (stored squared). A new source has an invalid maximum: never heard.
    void setDropoffs(float minM, float maxM) {
        min2_ = minM * minM;
        max2_ = maxM * maxM;
        maxValid_ = true;
    }
    float min2() const { return min2_; }
    float max2() const { return max2_; }
    bool maxValid() const { return maxValid_; }

    // Values of the last measurement and 3D update.
    float d2 = 1000000.0f;  // a reset source counts as 1,000,000 until measured
    float D = 0, dD = 0;
    float A = 0, pan = 0, doppler = 1;

    int slot() const { return slot_; }
    bool holdsSlot() const { return slot_ >= 0; }
    // Reset of the 3D values: the slot is given back, the distance history restarts.
    void reset3D();
    // Asks for a slot when holding none ("Getting and losing a slot").
    void requestSlot();
    void releaseSlot();

protected:
    friend class Manager3D;
    virtual void onAssigned(int slot) { (void)slot; }
    virtual void onReleased() {}
    virtual void slotUpdate(float dt) { (void)dt; }

    Manager3D* mgr_ = nullptr;
    int slot_ = -1;
    float min2_ = 0, max2_ = 0;
    bool maxValid_ = false;
    bool haveD_ = false;
    float Dprev_ = 0;
};

class Manager3D {
public:
    static constexpr int kSlots = 4;
    static constexpr float kEchoDelay = 0.5f;

    // The listener: the active camera's position and right-hand axis (its matrix's first row).
    void setListener(const vec3& pos, const vec3& right) {
        L_ = pos;
        R_ = right;
    }
    const vec3& listener() const { return L_; }
    bool enabled() const { return enabled_; }
    // Results screen: disabled and every slot released; a game reset enables it again.
    void disable();
    void enable() { enabled_ = true; }
    // The echo state of the frame (player car in an enclosed room).
    bool echo = false;
    float echoDelay() const { return kEchoDelay; }

    void request(Source3D& s);
    void release(Source3D& s);
    // Measures d², D and ΔD of a source.
    void measure(Source3D& s);
    // A 3D holder's distance step: measures, and returns true if it is past its maximum drop-off
    // (and does not keep its slot), i.e. must give the slot back.
    bool pastMax(Source3D& s);
    // A, pan and doppler of a holder (2D: 1, its own pan, 1). A source keeping its slot past the
    // maximum keeps its last attenuation.
    void compute(Source3D& s, float dt);
    // Every slot holder in slot order.
    void updateHolders(float dt);
    void forget(Source3D& s);
    Source3D* holder(int k) const { return k >= 0 && k < kSlots ? slots_[k] : nullptr; }
    int slotPriority(const Source3D& s) const { return s.priority + (s.twoD ? 1000000 : 0); }

private:
    float dist2(const Source3D& s) const { return s.twoD ? 0.01f : s.d2; }
    void assign(Source3D& s, int k);
    Source3D* slots_[kSlots] = {};
    vec3 L_, R_{1, 0, 0};
    bool enabled_ = true;
};

// The 3D sample cache: each distinct name (with its pitch-control choice) is loaded once per slot;
// a source holding slot k uses copy k.
struct SlotSample {
    std::array<Sample*, Manager3D::kSlots> copy{};
    Sample* at(int slot) const { return slot >= 0 && slot < Manager3D::kSlots ? copy[size_t(slot)] : nullptr; }
    bool valid() const { return copy[0] != nullptr; }
};

class SampleCache {
public:
    explicit SampleCache(SoundSystem& sys) : sys_(sys) {}
    // `name` as the owner builds it (folder prefix included); the file is soundPath(name).
    SlotSample get(const std::string& name, bool pitchControl);
    void clear() { map_.clear(); }
    size_t size() const { return map_.size(); }

private:
    SoundSystem& sys_;
    std::map<std::string, std::array<std::unique_ptr<Sample>, Manager3D::kSlots>> map_;
};

}  // namespace audio
}  // namespace cc
