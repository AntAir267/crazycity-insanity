// Our own wire format for the network game (spec/game/multiplayer.md leaves the protocol to us):
// every message is one type byte followed by its fields, little-endian, floats as IEEE singles.
// The car state keeps MM2's quantisation (multiplayer.md 3.1); the other records carry what
// section 4 lists.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {
namespace net {

// Message types (multiplayer.md section 4; record, settings and chat of sections 1.1 – 1.5).
enum class Msg : uint8_t {
    Record = 1,       // player record changed (1.1); also sent to every newcomer
    Settings,         // host → all: the settings of 1.2
    Start,            // host → all: start slot table (10 ids)
    CarState,         // each → each, unguaranteed (3.1)
    Loaded,           // each → all: car check value
    Reloaded,         // each → all (after a restart)
    Joined,           // joiner → all: car check value
    RaceStart,        // host → all
    Finished,         // non-host → host: finish time
    Result,           // host → all: player id, finish time
    RaceOver,         // host → all
    TimeUp,           // host → all
    Restart,          // host → all
    Wrecked,          // each → all
    DamageRepaired,   // each → all
    CarReset,         // each → all: body matrix
    Chat,             // each → all: text
    Cheater,          // host → all: player id
    TimeRequest,      // non-host → host
    TimeReply,        // host → requester: host clock
    HostQuits,        // host → all
    ReturnToLobby,    // host → all
    Kick,             // host → one
    PlayerLeaves,     // player → all
    GoldRequest,      // non-host → host
    GoldCarrier,      // host → all: player id
    GoldDropped,      // dropper → all: gold position
    GoldDelivered,    // deliverer → all
    GoldPlaced,       // host → all: gold, bank, hideout positions
    StateRequest,     // C&R joiner → all (team / state request)
    StateReply,       // host → joiner: carrier (flag, id), gold, bank, hideout
    MinutesLeft,      // host → all: warning number 0 – 4
    GameOver,         // host → all: reason (1 time, 2 points)
    Count
};
const char* msgName(Msg m);

class Writer {
public:
    explicit Writer(Msg type) { u8(uint8_t(type)); }
    void u8(uint8_t v) { b_.push_back(v); }
    void i8(int8_t v) { b_.push_back(uint8_t(v)); }
    void u16(uint16_t v) {
        u8(uint8_t(v));
        u8(uint8_t(v >> 8));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) u8(uint8_t(v >> (8 * i)));
    }
    void f32(float f) {
        uint32_t v;
        std::memcpy(&v, &f, 4);
        u32(v);
    }
    void f64(double d) {
        uint64_t v;
        std::memcpy(&v, &d, 8);
        for (int i = 0; i < 8; ++i) u8(uint8_t(v >> (8 * i)));
    }
    void v3(const vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }
    void str(const std::string& s) {
        const size_t n = s.size() < 65535 ? s.size() : 65535;
        u16(uint16_t(n));
        b_.insert(b_.end(), s.begin(), s.begin() + long(n));
    }
    const std::vector<uint8_t>& bytes() const { return b_; }

private:
    std::vector<uint8_t> b_;
};

class Reader {
public:
    Reader(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    explicit Reader(const std::vector<uint8_t>& b) : p_(b.data()), n_(b.size()) {}
    bool ok() const { return ok_; }
    bool atEnd() const { return i_ >= n_; }
    uint8_t u8() {
        if (i_ + 1 > n_) return fail(), 0;
        return p_[i_++];
    }
    int8_t i8() { return int8_t(u8()); }
    uint16_t u16() {
        const uint16_t a = u8();
        const uint16_t b = u8();
        return uint16_t(a | (b << 8));
    }
    uint32_t u32() {
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= uint32_t(u8()) << (8 * i);
        return v;
    }
    float f32() {
        const uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    double f64() {
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(u8()) << (8 * i);
        double d;
        std::memcpy(&d, &v, 8);
        return d;
    }
    vec3 v3() {
        vec3 v;
        v.x = f32();
        v.y = f32();
        v.z = f32();
        return v;
    }
    std::string str() {
        const size_t n = u16();
        if (i_ + n > n_) return fail(), std::string();
        std::string s(reinterpret_cast<const char*>(p_ + i_), n);
        i_ += n;
        return s;
    }

private:
    void fail() {
        ok_ = false;
        i_ = n_;
    }
    const uint8_t* p_;
    size_t n_;
    size_t i_ = 0;
    bool ok_ = true;
};

}  // namespace net
}  // namespace cc
