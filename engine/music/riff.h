// RIFF chunk walking for the DirectMusic and DLS files (spec/formats/directmusic.md, "Common
// rules"): a chunk is a four-character id, a u32 size and that many bytes padded to an even
// length; RIFF and LIST chunks start with a form type and hold chunks.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "core/bytes.h"

namespace cc {
namespace music {

constexpr uint32_t fourcc(const char (&s)[5]) {
    return uint32_t(uint8_t(s[0])) | (uint32_t(uint8_t(s[1])) << 8) | (uint32_t(uint8_t(s[2])) << 16) |
           (uint32_t(uint8_t(s[3])) << 24);
}

struct RiffChunk {
    uint32_t id = 0;
    uint32_t form = 0;  // RIFF/LIST only
    const uint8_t* data = nullptr;  // after the form type for RIFF/LIST
    size_t size = 0;                // bytes at data
    bool isList() const { return id == fourcc("RIFF") || id == fourcc("LIST"); }
    ByteReader reader() const { return ByteReader(data, size); }
};

// Iterates the chunks of a container's body. Lenient: a chunk whose size runs past the end is
// cut at the end (some editor chunks in the DLS files have odd sizes).
class RiffIter {
public:
    RiffIter(const uint8_t* p, size_t n) : p_(p), n_(n) {}
    bool next(RiffChunk& c) {
        if (pos_ + 8 > n_) return false;
        c.id = readU32LE(p_ + pos_);
        uint32_t size = readU32LE(p_ + pos_ + 4);
        size_t start = pos_ + 8;
        size_t avail = n_ - start;
        size_t len = size <= avail ? size : avail;
        c.form = 0;
        if (c.id == fourcc("RIFF") || c.id == fourcc("LIST")) {
            if (len < 4) {
                pos_ = n_;
                return false;
            }
            c.form = readU32LE(p_ + start);
            c.data = p_ + start + 4;
            c.size = len - 4;
        } else {
            c.data = p_ + start;
            c.size = len;
        }
        pos_ = start + len + (len & 1);
        return true;
    }

private:
    const uint8_t* p_;
    size_t n_;
    size_t pos_ = 0;
};

inline RiffIter children(const RiffChunk& c) { return RiffIter(c.data, c.size); }

// A NUL-terminated UTF-16 name as Latin-1 (characters above 255 become '?').
inline std::string utf16Name(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint16_t ch = uint16_t(p[i] | (p[i + 1] << 8));
        if (ch == 0) break;
        s += ch < 256 ? char(ch) : '?';
    }
    return s;
}

}  // namespace music
}  // namespace cc
