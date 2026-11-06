#include "formats/image.h"

#include <algorithm>
#include <cstring>

#include "core/bytes.h"
#include "core/strutil.h"

namespace cc {

size_t Image::byteSize() const {
    size_t n = 0;
    for (const auto& l : levels) n += l.rgba.size();
    return n;
}

namespace {

bool isPow2(unsigned v) { return v != 0 && (v & (v - 1)) == 0; }

void flipRows(ImageLevel& l) {
    const size_t row = size_t(l.width) * 4;
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < l.height / 2; ++y) {
        uint8_t* a = l.rgba.data() + size_t(y) * row;
        uint8_t* b = l.rgba.data() + size_t(l.height - 1 - y) * row;
        std::memcpy(tmp.data(), a, row);
        std::memcpy(a, b, row);
        std::memcpy(b, tmp.data(), row);
    }
}

inline uint8_t expand5(unsigned v) { return uint8_t((v << 3) | (v >> 2)); }

}  // namespace

bool decodeTex(const uint8_t* data, size_t size, Image& img, std::string& err) {
    img = Image();
    img.format = "tex";
    if (size < 14) { err = "too small for a .tex header"; return false; }
    ByteReader r(data, size);
    const unsigned w = r.u16();
    const unsigned h = r.u16();
    const unsigned type = r.u16();
    const unsigned storedMips = r.u16();
    r.u16();  // always 1
    const uint32_t flags = r.u32();
    if (!isPow2(w) || !isPow2(h)) {
        err = strf("size %ux%u is not a power of two", w, h);
        return false;
    }
    int paletteEntries = 0;
    int bits = 0;
    switch (type) {
        case 1: case 14: paletteEntries = 256; bits = 8; break;
        case 2: paletteEntries = 256; bits = 16; break;
        case 15: case 16: paletteEntries = 16; bits = 4; break;
        case 17: bits = 24; break;
        case 18: bits = 32; break;
        case 6: bits = 16; break;
        default:
            err = strf("unknown pixel type %u", type);
            return false;
    }
    img.texType = int(type);
    img.texFlags = flags;
    img.clampU = (flags & 0x00001) != 0;
    img.clampV = (flags & 0x10000) != 0;
    img.hasAlpha = (type == 2 || type == 6 || type == 14 || type == 16 || type == 18);

    // Palette entries are stored B, G, R, A.
    uint8_t pal[256][4] = {};
    for (int i = 0; i < paletteEntries; ++i) {
        uint8_t b = r.u8(), g = r.u8(), rr = r.u8(), a = r.u8();
        pal[i][0] = rr;
        pal[i][1] = g;
        pal[i][2] = b;
        pal[i][3] = a;
    }
    if (!r.ok()) { err = "truncated palette"; return false; }
    const bool paletteAlpha = (type == 14 || type == 16);

    int possible = 1;
    for (unsigned lw = w, lh = h; lw > 1 && lh > 1; lw >>= 1, lh >>= 1) ++possible;
    int levels = storedMips == 0 ? possible : std::min<int>(int(storedMips), possible);

    unsigned lw = w, lh = h;
    for (int lev = 0; lev < levels; ++lev, lw >>= 1, lh >>= 1) {
        const size_t pixels = size_t(lw) * lh;
        const size_t bytes = (pixels * size_t(bits) + 7) / 8;
        if (!r.need(bytes)) {
            if (lev == 0) {
                err = strf("pixel data truncated (%zu bytes needed, %zu present)", bytes, r.remaining());
                return false;
            }
            break;  // keep the levels that are complete
        }
        ImageLevel L;
        L.width = int(lw);
        L.height = int(lh);
        L.rgba.resize(pixels * 4);
        const uint8_t* p = r.here();
        uint8_t* o = L.rgba.data();
        for (size_t i = 0; i < pixels; ++i, o += 4) {
            switch (type) {
                case 1: case 14: {
                    const uint8_t* c = pal[p[i]];
                    o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
                    o[3] = paletteAlpha ? c[3] : 255;
                    break;
                }
                case 2: {
                    const uint8_t* c = pal[p[i * 2]];
                    o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
                    o[3] = p[i * 2 + 1];
                    break;
                }
                case 15: case 16: {
                    uint8_t byte = p[i / 2];
                    uint8_t idx = (i & 1) ? uint8_t(byte >> 4) : uint8_t(byte & 0x0F);  // low nibble = left pixel
                    const uint8_t* c = pal[idx];
                    o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
                    o[3] = paletteAlpha ? c[3] : 255;
                    break;
                }
                case 17:
                    o[0] = p[i * 3]; o[1] = p[i * 3 + 1]; o[2] = p[i * 3 + 2]; o[3] = 255;
                    break;
                case 18:
                    o[0] = p[i * 4]; o[1] = p[i * 4 + 1]; o[2] = p[i * 4 + 2]; o[3] = p[i * 4 + 3];
                    break;
                case 6: {
                    unsigned v = unsigned(p[i * 2]) | (unsigned(p[i * 2 + 1]) << 8);
                    o[0] = expand5((v >> 10) & 31);
                    o[1] = expand5((v >> 5) & 31);
                    o[2] = expand5(v & 31);
                    o[3] = (v & 0x8000) ? 255 : 0;
                    break;
                }
            }
        }
        r.skip(bytes);
        img.levels.push_back(std::move(L));
    }
    return true;
}

bool decodeTga(const uint8_t* data, size_t size, Image& img, std::string& err) {
    img = Image();
    img.format = "tga";
    if (size < 18) { err = "too small for a .tga header"; return false; }
    const unsigned idLen = data[0];
    const unsigned cmapType = data[1];
    const unsigned imgType = data[2];
    const unsigned w = unsigned(data[12] | (data[13] << 8));
    const unsigned h = unsigned(data[14] | (data[15] << 8));
    const unsigned bpp = data[16];
    const unsigned desc = data[17];
    if (cmapType != 0) { err = "colour-mapped TGA is not supported"; return false; }
    if (imgType != 2) { err = strf("TGA image type %u is not supported (only 2)", imgType); return false; }
    if (bpp != 24 && bpp != 32) { err = strf("TGA with %u bits per pixel is not supported", bpp); return false; }
    if (w == 0 || h == 0) { err = "TGA has zero size"; return false; }
    const size_t start = 18 + idLen;
    const size_t bytesPP = bpp / 8;
    const size_t need = size_t(w) * h * bytesPP;
    if (start > size || size - start < need) { err = "TGA pixel data truncated"; return false; }
    ImageLevel L;
    L.width = int(w);
    L.height = int(h);
    L.rgba.resize(size_t(w) * h * 4);
    const uint8_t* p = data + start;
    uint8_t* o = L.rgba.data();
    for (size_t i = 0; i < size_t(w) * h; ++i, p += bytesPP, o += 4) {
        o[0] = p[2];
        o[1] = p[1];
        o[2] = p[0];
        o[3] = bytesPP == 4 ? p[3] : 255;
    }
    // Bottom-up files are flipped so that the top row of the picture is row 0.
    if (!(desc & 0x20)) flipRows(L);
    img.levels.push_back(std::move(L));
    img.hasAlpha = (bpp == 32);
    img.generateMips = (w == h);
    return true;
}

bool decodeBmp(const uint8_t* data, size_t size, Image& img, std::string& err) {
    img = Image();
    img.format = "bmp";
    if (size < 26 || data[0] != 'B' || data[1] != 'M') { err = "not a BMP file"; return false; }
    const uint32_t offBits = readU32LE(data + 10);
    const uint32_t hdrSize = readU32LE(data + 14);
    int w = 0, h = 0, bpp = 0;
    size_t palOff = 14 + hdrSize;
    int palEntrySize = 4;
    if (hdrSize == 12) {
        w = data[18] | (data[19] << 8);
        h = data[20] | (data[21] << 8);
        bpp = data[24] | (data[25] << 8);
        palEntrySize = 3;
    } else if (hdrSize >= 40 && size >= 14 + 40) {
        w = int(readU32LE(data + 18));
        h = int(readU32LE(data + 22));
        bpp = data[28] | (data[29] << 8);
        uint32_t compression = readU32LE(data + 30);
        if (compression != 0) { err = "compressed BMP is not supported"; return false; }
    } else {
        err = strf("BMP header size %u is not supported", hdrSize);
        return false;
    }
    bool topDown = false;
    if (h < 0) { h = -h; topDown = true; }
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384) { err = "BMP size is not valid"; return false; }
    if (bpp != 8 && bpp != 24) { err = strf("BMP with %d bits per pixel is not supported", bpp); return false; }

    // The 8-bit palette is always 256 entries read from offset 54 (tex.md); pixels always start
    // at bfOffBits, even when it is 0.
    uint8_t pal[256][3] = {};
    (void)palOff;
    if (bpp == 8) {
        const size_t palAt = 54;
        if (palAt + size_t(256) * palEntrySize > size) { err = "BMP palette truncated"; return false; }
        for (int i = 0; i < 256; ++i) {
            const uint8_t* e = data + palAt + size_t(i) * palEntrySize;
            pal[i][0] = e[2];
            pal[i][1] = e[1];
            pal[i][2] = e[0];
        }
    }
    const size_t dataOff = offBits;
    // Rows are taken as exactly the width rounded up to even, without 4-byte padding.
    const size_t bytesPP = size_t(bpp / 8);
    const size_t stride = size_t((w + 1) & ~1) * bytesPP;
    if (dataOff > size || size - dataOff < stride * size_t(h)) { err = "BMP pixel data truncated"; return false; }
    ImageLevel L;
    L.width = w;
    L.height = h;
    L.rgba.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = data + dataOff + stride * size_t(y);
        uint8_t* o = L.rgba.data() + size_t(y) * size_t(w) * 4;
        for (int x = 0; x < w; ++x, o += 4) {
            if (bpp == 8) {
                const uint8_t* c = pal[row[x]];
                o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
            } else {
                o[0] = row[x * 3 + 2]; o[1] = row[x * 3 + 1]; o[2] = row[x * 3];
            }
            o[3] = 255;
        }
    }
    // Stored bottom-up; loaded so that the top row of the picture is row 0.
    if (!topDown) flipRows(L);
    img.levels.push_back(std::move(L));
    return true;
}

bool decodeImage(const std::string& path, const uint8_t* data, size_t size, Image& out,
                 std::string& err) {
    if (iendsWith(path, ".tex")) return decodeTex(data, size, out, err);
    if (iendsWith(path, ".tga")) return decodeTga(data, size, out, err);
    if (iendsWith(path, ".bmp")) return decodeBmp(data, size, out, err);
    err = "unknown image extension";
    return false;
}

}  // namespace cc
