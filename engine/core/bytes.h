// Bounds-checked little-endian reader. Reading past the end never touches memory outside the
// buffer: it returns zeros and clears ok(), so parsers can check once at the end of a block.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace cc {

using Bytes = std::vector<uint8_t>;
using BytesPtr = std::shared_ptr<const Bytes>;

class ByteReader {
public:
    ByteReader() = default;
    ByteReader(const uint8_t* data, size_t size) : p_(data), n_(size) {}
    explicit ByteReader(const Bytes& b) : p_(b.data()), n_(b.size()) {}

    bool ok() const { return ok_; }
    size_t pos() const { return pos_; }
    size_t size() const { return n_; }
    size_t remaining() const { return n_ - pos_; }
    bool atEnd() const { return pos_ >= n_; }
    const uint8_t* data() const { return p_; }
    const uint8_t* here() const { return p_ + pos_; }
    void fail() { ok_ = false; }

    // True if k more bytes can be read; otherwise marks the reader failed.
    bool need(size_t k) {
        if (!ok_ || k > n_ - pos_) { ok_ = false; return false; }
        return true;
    }
    bool skip(size_t k) {
        if (!need(k)) return false;
        pos_ += k;
        return true;
    }
    bool seek(size_t p) {
        if (p > n_) { ok_ = false; return false; }
        pos_ = p;
        return true;
    }
    bool bytes(void* dst, size_t k) {
        if (!need(k)) { std::memset(dst, 0, k); return false; }
        std::memcpy(dst, p_ + pos_, k);
        pos_ += k;
        return true;
    }
    uint8_t u8() {
        if (!need(1)) return 0;
        return p_[pos_++];
    }
    uint16_t u16() {
        if (!need(2)) return 0;
        uint16_t v = uint16_t(p_[pos_] | (p_[pos_ + 1] << 8));
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        if (!need(4)) return 0;
        uint32_t v = uint32_t(p_[pos_]) | (uint32_t(p_[pos_ + 1]) << 8) |
                     (uint32_t(p_[pos_ + 2]) << 16) | (uint32_t(p_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }
    float f32() {
        uint32_t u = u32();
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }

    // A reader over the next k bytes (advances this one past them).
    ByteReader sub(size_t k) {
        if (!need(k)) return ByteReader(p_ + n_, 0);
        ByteReader r(p_ + pos_, k);
        pos_ += k;
        return r;
    }

private:
    const uint8_t* p_ = nullptr;
    size_t n_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
};

inline uint32_t readU32LE(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline void writeU32LE(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

}  // namespace cc
