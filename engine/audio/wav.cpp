#include "audio/wav.h"

#include <algorithm>
#include <cstring>

#include "core/strutil.h"

namespace cc {
namespace audio {

namespace {
inline uint32_t u32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline uint16_t u16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
}  // namespace

bool parseWav(const uint8_t* p, size_t n, WavInfo& w, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    w = WavInfo();
    if (n < 12 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0)
        return fail("not a RIFF WAVE file");
    bool haveFmt = false, haveData = false;
    size_t pos = 12;
    while (!(haveFmt && haveData)) {
        if (pos + 8 > n) return fail(haveFmt ? "no data chunk" : "no fmt chunk");
        const uint8_t* id = p + pos;
        const uint32_t size = u32(p + pos + 4);
        const size_t body = pos + 8;
        if (std::memcmp(id, "fmt ", 4) == 0) {
            if (size < 14) return fail("fmt chunk shorter than 14 bytes");
            // The 18-byte wave format (bytes past the chunk or the file read as 0).
            uint8_t f[18] = {};
            for (size_t i = 0; i < 18 && body + i < n; ++i) f[i] = p[body + i];
            w.formatTag = u16(f);
            w.channels = u16(f + 2);
            w.rate = int(u32(f + 4));
            w.byteRate = int(u32(f + 8));
            w.blockAlign = u16(f + 12);
            w.bits = size >= 16 ? u16(f + 14) : 0;
            w.extraSize = size >= 18 ? u16(f + 16) : 0;
            haveFmt = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            w.dataOffset = body;
            w.dataSize = body <= n ? std::min<size_t>(size, n - body) : 0;
            haveData = true;
        } else if (std::memcmp(id, "fact", 4) == 0) {
            // accepted, not used
        } else {
            return fail(strf("chunk '%.4s' before the fmt and data chunks", reinterpret_cast<const char*>(id)));
        }
        pos = body + size_t(size) + (size & 1u);
    }
    if (w.formatTag == 0x6969) return fail("compressed format 0x6969 is not supported");
    if (w.formatTag != 1) return fail(strf("format tag %d is not PCM", w.formatTag));
    if (w.bits != 8 && w.bits != 16) return fail(strf("%d bits per sample", w.bits));
    if (w.channels != 1 && w.channels != 2) return fail(strf("%d channels", w.channels));
    if (w.rate <= 0) return fail("no sample rate");
    return true;
}

void wavToPcm16(const uint8_t* p, const WavInfo& w, std::vector<int16_t>& out) {
    const size_t samples = size_t(w.frames()) * size_t(w.channels);
    out.resize(samples);
    const uint8_t* d = p + w.dataOffset;
    if (w.bits == 16) {
        for (size_t i = 0; i < samples; ++i) out[i] = int16_t(u16(d + 2 * i));
    } else {
        for (size_t i = 0; i < samples; ++i) out[i] = int16_t((int(d[i]) - 128) << 8);
    }
}

}  // namespace audio
}  // namespace cc
