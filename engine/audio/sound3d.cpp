#include "audio/sound3d.h"

#include <cmath>

#include "core/strutil.h"

namespace cc {
namespace audio {

Source3D::~Source3D() {
    if (mgr_) mgr_->forget(*this);
}

void Source3D::reset3D() {
    if (mgr_ && slot_ >= 0) mgr_->release(*this);
    d2 = 1000000.0f;
    haveD_ = false;
    D = dD = 0;
}

void Source3D::requestSlot() {
    if (mgr_) mgr_->request(*this);
}

void Source3D::releaseSlot() {
    if (mgr_ && slot_ >= 0) mgr_->release(*this);
}

void Manager3D::disable() {
    enabled_ = false;
    for (int k = 0; k < kSlots; ++k)
        if (slots_[k]) release(*slots_[k]);
}

void Manager3D::measure(Source3D& s) {
    if (s.twoD) {
        s.d2 = 0.01f;
        return;
    }
    const float dx = s.position.x - L_.x, dy = s.position.y - L_.y, dz = s.position.z - L_.z;
    s.d2 = (dx * dx + dy * dy) + dz * dz;
    s.D = (std::fabs(dx) + std::fabs(dy)) + std::fabs(dz);
    s.dD = s.haveD_ ? s.Dprev_ - s.D : 0.0f;
    s.Dprev_ = s.D;
    s.haveD_ = true;
}

void Manager3D::assign(Source3D& s, int k) {
    slots_[k] = &s;
    s.slot_ = k;
    s.onAssigned(k);
}

void Manager3D::request(Source3D& s) {
    if (!enabled_ || s.slot_ >= 0) return;
    if (!s.twoD) {
        measure(s);
        if (!s.maxValid_ || !(s.d2 < s.max2_)) return;
    }
    for (int k = 0; k < kSlots; ++k)
        if (!slots_[k]) {
            assign(s, k);
            return;
        }
    // All taken: the candidate starts at slot 0's holder and moves to a farther holder whose slot
    // priority is not higher.
    int c = 0;
    for (int k = 1; k < kSlots; ++k) {
        const Source3D& h = *slots_[k];
        const Source3D& cand = *slots_[c];
        if (dist2(h) > dist2(cand) && slotPriority(h) <= slotPriority(cand)) c = k;
    }
    Source3D& cand = *slots_[c];
    const int ps = slotPriority(s), pc = slotPriority(cand);
    if (ps > pc || (ps == pc && dist2(cand) > dist2(s))) {
        release(cand);
        assign(s, c);
    }
}

void Manager3D::release(Source3D& s) {
    if (s.slot_ < 0) return;
    // The source is told while it still knows its slot (it lets go of that slot's samples).
    const int k = s.slot_;
    s.onReleased();
    slots_[k] = nullptr;
    s.slot_ = -1;
}

void Manager3D::forget(Source3D& s) {
    if (s.slot_ >= 0 && s.slot_ < kSlots && slots_[s.slot_] == &s) slots_[s.slot_] = nullptr;
    s.slot_ = -1;
}

bool Manager3D::pastMax(Source3D& s) {
    if (s.twoD) return false;
    measure(s);
    if (s.keepsSlot) return false;
    return !s.maxValid_ || !(s.d2 < s.max2_);
}

void Manager3D::compute(Source3D& s, float dt) {
    if (s.twoD) {
        s.A = 1.0f;
        s.pan = s.pan2D;
        s.doppler = 1.0f;
        return;
    }
    const bool inside = s.maxValid_ && s.d2 < s.max2_;
    if (inside) {
        float a = 0.0f;
        if (!(s.d2 <= s.min2_)) a = (s.d2 - s.min2_) * (1.0f / (s.max2_ - s.min2_));
        s.A = 1.0f - a;
    }
    if (s.d2 < s.min2_ || !(s.D > 0.0f)) {
        s.pan = 0.0f;
    } else {
        const vec3 d(s.position.x - L_.x, s.position.y - L_.y, s.position.z - L_.z);
        const float x = (d.x * R_.x + d.y * R_.y) + d.z * R_.z;
        s.pan = 0.2f * x / s.D;
    }
    s.doppler = ((s.dD * s.dopplerK) * dt) + 1.0f;
}

void Manager3D::updateHolders(float dt) {
    for (int k = 0; k < kSlots; ++k)
        if (slots_[k]) slots_[k]->slotUpdate(dt);
}

SlotSample SampleCache::get(const std::string& name, bool pitchControl) {
    SlotSample out;
    if (name.empty()) return out;
    const std::string key = pathKey(name) + (pitchControl ? "|p" : "|n");
    auto it = map_.find(key);
    if (it == map_.end()) {
        std::array<std::unique_ptr<Sample>, Manager3D::kSlots> a;
        const std::string path = soundPath(name);
        for (auto& s : a) {
            s = std::make_unique<Sample>(sys_, pitchControl, 1, 0.0f);
            s->setFile(0, path);
        }
        it = map_.emplace(key, std::move(a)).first;
    }
    for (int k = 0; k < Manager3D::kSlots; ++k) out.copy[size_t(k)] = it->second[size_t(k)].get();
    return out;
}

}  // namespace audio
}  // namespace cc
