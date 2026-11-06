#include "ui/soft_backend.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <zlib.h>

#include "ui/shade.h"
#include "ui/text.h"

namespace cc {
namespace ui {

void SoftCanvas::resize(int w, int h) {
    w_ = std::max(1, w);
    h_ = std::max(1, h);
    px_.assign(size_t(w_) * size_t(h_) * 4, 0.0f);
}

void SoftCanvas::background(const Color& top, const Color& bottom) {
    for (int y = 0; y < h_; ++y) {
        const Color c = mix(top, bottom, h_ > 1 ? float(y) / float(h_ - 1) : 0.0f);
        for (int x = 0; x < w_; ++x) {
            float* p = &px_[(size_t(y) * size_t(w_) + size_t(x)) * 4];
            p[0] = c.r * c.a;
            p[1] = c.g * c.a;
            p[2] = c.b * c.a;
            p[3] = c.a;
        }
    }
}

namespace {

inline void blendOver(float* d, const float s[4]) {
    const float k = 1.0f - s[3];
    d[0] = s[0] + d[0] * k;
    d[1] = s[1] + d[1] * k;
    d[2] = s[2] + d[2] * k;
    d[3] = s[3] + d[3] * k;
}

void gaussian(const std::vector<float>& src, std::vector<float>& dst, int w, int h, float sigma) {
    dst = src;
    if (sigma < 0.3f) return;
    const int r = std::max(1, int(std::ceil(sigma * 3.0f)));
    std::vector<float> k(static_cast<size_t>(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[size_t(i + r)] = std::exp(-float(i * i) / (2.0f * sigma * sigma));
    for (float& v : k) v /= sum;
    std::vector<float> tmp(src.size());
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; ++i) {
                const int xx = std::clamp(x + i, 0, w - 1);
                const float* s = &src[(size_t(y) * size_t(w) + size_t(xx)) * 4];
                const float kk = k[size_t(i + r)];
                for (int c = 0; c < 4; ++c) acc[c] += s[c] * kk;
            }
            std::memcpy(&tmp[(size_t(y) * size_t(w) + size_t(x)) * 4], acc, sizeof acc);
        }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float acc[4] = {0, 0, 0, 0};
            for (int i = -r; i <= r; ++i) {
                const int yy = std::clamp(y + i, 0, h - 1);
                const float* s = &tmp[(size_t(yy) * size_t(w) + size_t(x)) * 4];
                const float kk = k[size_t(i + r)];
                for (int c = 0; c < 4; ++c) acc[c] += s[c] * kk;
            }
            std::memcpy(&dst[(size_t(y) * size_t(w) + size_t(x)) * 4], acc, sizeof acc);
        }
}

}  // namespace

void SoftCanvas::doCapture(float blur) {
    cap_ = px_;
    gaussian(cap_, capBlur_, w_, h_, blur);
}

void SoftCanvas::render(const DrawList& dl) {
    for (const DrawCmd& c : dl.cmds) {
        if (c.type == DrawCmd::Capture) {
            doCapture(c.blur);
            continue;
        }
        for (int i = c.first; i < c.first + c.count; ++i) drawPrim(dl.prims[size_t(i)], dl);
    }
}

void SoftCanvas::drawPrim(const Prim& p, const DrawList& dl) {
    Rect area = p.box;
    if (p.kind == PrimShadow) {
        const float reach = p.blur * 1.5f + std::max(0.0f, p.spread) + 1.0f;
        Rect s{p.box.x + p.uv[0], p.box.y + p.uv[1], p.box.w, p.box.h};
        area = s.expand(reach);
    } else {
        area = area.expand(1.0f);
    }
    area = area.intersect(p.clip).intersect(Rect{0, 0, float(w_), float(h_)});
    if (area.empty()) return;
    const int x0 = int(std::floor(area.x)), y0 = int(std::floor(area.y));
    const int x1 = std::min(w_, int(std::ceil(area.right()))), y1 = std::min(h_, int(std::ceil(area.bottom())));
    const float hx = p.box.w * 0.5f, hy = p.box.h * 0.5f;
    const float cx = p.box.x + hx, cy = p.box.y + hy;
    const Atlas& atlas = Fonts::get().atlas();
    const float pa = p.fill.a;
    const float fillP[4] = {p.fill.r * pa, p.fill.g * pa, p.fill.b * pa, pa};
    const float ba = p.border.a;
    const float borderP[4] = {p.border.r * ba, p.border.g * ba, p.border.b * ba, ba};
    for (int y = y0; y < y1; ++y) {
        const float fy = float(y) + 0.5f;
        if (fy < p.clip.y || fy >= p.clip.bottom()) continue;
        for (int x = x0; x < x1; ++x) {
            const float fx = float(x) + 0.5f;
            if (fx < p.clip.x || fx >= p.clip.right()) continue;
            float out[4] = {0, 0, 0, 0};
            switch (p.kind) {
                case PrimRect: {
                    const float d = roundedBoxDistance(fx - cx, fy - cy, hx, hy, p.radius);
                    const float inside = std::clamp(0.5f - d, 0.0f, 1.0f);
                    if (inside <= 0) continue;
                    float f[4];
                    if (p.grad != GradNone) gradientColor(p, gradientT(p, fx - p.box.x, fy - p.box.y), f);
                    else std::memcpy(f, fillP, sizeof f);
                    for (int k = 0; k < 4; ++k) out[k] = f[k] * inside;
                    if (p.borderW > 0 && ba > 0) {
                        const float inner = std::clamp(0.5f - (d + p.borderW), 0.0f, 1.0f);
                        const float bm = std::max(0.0f, inside - inner);
                        float b[4];
                        for (int k = 0; k < 4; ++k) b[k] = borderP[k] * bm;
                        const float kk = 1.0f - b[3];
                        for (int k = 0; k < 4; ++k) out[k] = b[k] + out[k] * kk;
                    }
                    break;
                }
                case PrimShadow: {
                    float r[4];
                    for (int k = 0; k < 4; ++k) r[k] = std::max(0.0f, p.radius[k] + p.spread);
                    const float d = roundedBoxDistance(fx - cx - p.uv[0], fy - cy - p.uv[1], hx + p.spread, hy + p.spread, r);
                    float a;
                    if (p.blur > 0.01f) {
                        const float sigma = p.blur * 0.5f;
                        a = 0.5f * (1.0f - erfApprox(d / (sigma * 1.41421356f)));
                    } else {
                        a = std::clamp(0.5f - d, 0.0f, 1.0f);
                    }
                    if (p.outsideOnly) {
                        const float d0 = roundedBoxDistance(fx - cx, fy - cy, hx, hy, p.radius);
                        a *= std::clamp(d0 + 0.5f, 0.0f, 1.0f);
                    }
                    for (int k = 0; k < 4; ++k) out[k] = fillP[k] * a;
                    break;
                }
                case PrimGlyph: {
                    if (fx < p.box.x || fy < p.box.y || fx >= p.box.right() || fy >= p.box.bottom()) continue;
                    const float u = p.uv[0] + (fx - p.box.x) / p.box.w * (p.uv[2] - p.uv[0]);
                    const float v = p.uv[1] + (fy - p.box.y) / p.box.h * (p.uv[3] - p.uv[1]);
                    const int ax = std::clamp(int(u * float(atlas.w)), 0, atlas.w - 1);
                    const int ay = std::clamp(int(v * float(atlas.h)), 0, atlas.h - 1);
                    if (atlas.pix.empty()) continue;
                    const float cov = float(atlas.pix[size_t(ay) * size_t(atlas.w) + size_t(ax)]) / 255.0f;
                    for (int k = 0; k < 4; ++k) out[k] = fillP[k] * cov;
                    break;
                }
                case PrimImage: {
                    const float d = roundedBoxDistance(fx - cx, fy - cy, hx, hy, p.radius);
                    const float inside = std::clamp(0.5f - d, 0.0f, 1.0f);
                    if (inside <= 0 || p.image < 0 || size_t(p.image) >= dl.images.size()) continue;
                    const ImageRef& im = dl.images[size_t(p.image)];
                    if (!im.rgba) continue;
                    const float u = p.uv[0] + (fx - p.box.x) / p.box.w * (p.uv[2] - p.uv[0]);
                    const float v = p.uv[1] + (fy - p.box.y) / p.box.h * (p.uv[3] - p.uv[1]);
                    const int ix = std::clamp(int(u * float(im.w)), 0, im.w - 1), iy = std::clamp(int(v * float(im.h)), 0, im.h - 1);
                    const uint8_t* s = im.rgba + (size_t(iy) * size_t(im.w) + size_t(ix)) * 4;
                    const float a = float(s[3]) / 255.0f * pa * inside;
                    out[0] = float(s[0]) / 255.0f * p.fill.r * a;
                    out[1] = float(s[1]) / 255.0f * p.fill.g * a;
                    out[2] = float(s[2]) / 255.0f * p.fill.b * a;
                    out[3] = a;
                    break;
                }
                case PrimBackdrop: {
                    if (capBlur_.empty()) continue;
                    const float d = roundedBoxDistance(fx - cx, fy - cy, hx, hy, p.radius);
                    const float inside = std::clamp(0.5f - d, 0.0f, 1.0f);
                    if (inside <= 0) continue;
                    const float* s = &capBlur_[(size_t(y) * size_t(w_) + size_t(x)) * 4];
                    for (int k = 0; k < 4; ++k) out[k] = s[k] * inside;
                    break;
                }
                case PrimFilter: {
                    if (cap_.empty()) continue;
                    if (fx < p.box.x || fy < p.box.y || fx >= p.box.right() || fy >= p.box.bottom()) continue;
                    const float* s = &cap_[(size_t(y) * size_t(w_) + size_t(x)) * 4];
                    float r = s[0] * p.blur, g = s[1] * p.blur, b = s[2] * p.blur;
                    const float l = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                    r = l + (r - l) * p.spread;
                    g = l + (g - l) * p.spread;
                    b = l + (b - l) * p.spread;
                    out[0] = std::max(0.0f, r);
                    out[1] = std::max(0.0f, g);
                    out[2] = std::max(0.0f, b);
                    out[3] = s[3];
                    float* d = &px_[(size_t(y) * size_t(w_) + size_t(x)) * 4];
                    std::memcpy(d, out, sizeof out);  // replaces what is there
                    continue;
                }
            }
            blendOver(&px_[(size_t(y) * size_t(w_) + size_t(x)) * 4], out);
        }
    }
}

std::vector<uint8_t> SoftCanvas::rgba8() const {
    std::vector<uint8_t> out(size_t(w_) * size_t(h_) * 4);
    for (size_t i = 0; i < size_t(w_) * size_t(h_); ++i) {
        const float* p = &px_[i * 4];
        const float a = p[3];
        for (int k = 0; k < 3; ++k) {
            const float v = a > 0.0001f ? p[k] / a : 0.0f;
            out[i * 4 + size_t(k)] = uint8_t(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
        }
        out[i * 4 + 3] = uint8_t(std::clamp(a * 255.0f + 0.5f, 0.0f, 255.0f));
    }
    return out;
}

bool SoftCanvas::writePng(const std::string& path) const {
    const std::vector<uint8_t> p = rgba8();
    return writePngRgba(path, p.data(), w_, h_);
}

namespace {
void be32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(uint8_t(x >> 24));
    v.push_back(uint8_t(x >> 16));
    v.push_back(uint8_t(x >> 8));
    v.push_back(uint8_t(x));
}
void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    be32(out, uint32_t(data.size()));
    std::vector<uint8_t> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, uint32_t(crc32(0L, td.data(), uInt(td.size()))));
}
}  // namespace

bool writePngRgba(const std::string& path, const uint8_t* rgba, int w, int h) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        raw.insert(raw.end(), rgba + size_t(y) * size_t(w) * 4, rgba + size_t(y + 1) * size_t(w) * 4);
    }
    uLongf zn = compressBound(uLong(raw.size()));
    std::vector<uint8_t> z(zn);
    if (compress2(z.data(), &zn, raw.data(), uLong(raw.size()), 6) != Z_OK) return false;
    z.resize(zn);
    std::vector<uint8_t> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    be32(ihdr, uint32_t(w));
    be32(ihdr, uint32_t(h));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", z);
    chunk(out, "IEND", {});
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

}  // namespace ui
}  // namespace cc
