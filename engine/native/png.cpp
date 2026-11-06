#include "native/png.h"

#include <zlib.h>

#include <cstdint>
#include <cstdio>
#include <vector>

namespace cc {
namespace native {

namespace {
void put32(std::vector<unsigned char>& o, uint32_t v) {
    o.push_back((unsigned char)(v >> 24));
    o.push_back((unsigned char)(v >> 16));
    o.push_back((unsigned char)(v >> 8));
    o.push_back((unsigned char)v);
}
void chunk(std::vector<unsigned char>& o, const char* type, const std::vector<unsigned char>& data) {
    put32(o, uint32_t(data.size()));
    const size_t start = o.size();
    o.insert(o.end(), type, type + 4);
    o.insert(o.end(), data.begin(), data.end());
    const uLong crc = crc32(0L, o.data() + start, uInt(o.size() - start));
    put32(o, uint32_t(crc));
}
}  // namespace

bool writePng(const std::string& path, int w, int h, const unsigned char* rgba, bool bottomUp) {
    if (w <= 0 || h <= 0 || !rgba) return false;
    // Filter byte 0 on every row, then the pixels (alpha kept opaque: the frame has no meaning in it).
    std::vector<unsigned char> raw;
    raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
    for (int y = 0; y < h; ++y) {
        const int sy = bottomUp ? h - 1 - y : y;
        raw.push_back(0);
        const unsigned char* row = rgba + size_t(sy) * size_t(w) * 4;
        for (int x = 0; x < w; ++x) {
            raw.push_back(row[4 * x]);
            raw.push_back(row[4 * x + 1]);
            raw.push_back(row[4 * x + 2]);
            raw.push_back(255);
        }
    }
    uLongf zlen = compressBound(uLong(raw.size()));
    std::vector<unsigned char> z(zlen);
    if (compress2(z.data(), &zlen, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
    z.resize(zlen);
    std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<unsigned char> ihdr;
    put32(ihdr, uint32_t(w));
    put32(ihdr, uint32_t(h));
    ihdr.push_back(8);  // bit depth
    ihdr.push_back(6);  // RGBA
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    return std::fclose(f) == 0 && ok;
}

}  // namespace native
}  // namespace cc
