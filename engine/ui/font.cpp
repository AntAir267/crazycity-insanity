#include "ui/font.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cc {
namespace ui {

namespace {

inline uint16_t u16(const uint8_t* p) { return uint16_t((p[0] << 8) | p[1]); }
inline int16_t s16(const uint8_t* p) { return int16_t(u16(p)); }
inline uint32_t u32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

int popcount16(unsigned v) {
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}

// One line of the accumulation rasterizer (exact signed area per cell; after font-rs).
void drawLine(float* a, int w, int h, int stride, float x0, float y0, float x1, float y1) {
    if (y0 == y1) return;
    float dir = 1.0f;
    if (y0 > y1) {
        dir = -1.0f;
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const float dxdy = (x1 - x0) / (y1 - y0);
    float x = x0;
    int ys = int(std::floor(y0));
    if (y0 < 0.0f) {
        x -= y0 * dxdy;
        ys = 0;
    }
    const int ye = std::min(h, int(std::ceil(y1)));
    const float fw = float(w);
    for (int y = ys; y < ye; ++y) {
        const float top = std::max(float(y), y0);
        const float dy = std::min(float(y) + 1.0f, y1) - top;
        const float xnext = x + dxdy * dy;
        const float d = dy * dir;
        const float xa = std::clamp(std::min(x, xnext), 0.0f, fw);
        const float xb = std::clamp(std::max(x, xnext), 0.0f, fw);
        float* row = a + size_t(y) * size_t(stride);
        const float x0floor = std::floor(xa);
        const int x0i = int(x0floor);
        const float x1ceil = std::ceil(xb);
        const int x1i = int(x1ceil);
        if (x1i <= x0i + 1) {
            const float xmf = 0.5f * (xa + xb) - x0floor;
            row[x0i] += d - d * xmf;
            row[x0i + 1] += d * xmf;
        } else {
            const float s = 1.0f / (xb - xa);
            const float x0f = xa - x0floor;
            const float a0 = 0.5f * s * (1.0f - x0f) * (1.0f - x0f);
            const float x1f = xb - x1ceil + 1.0f;
            const float am = 0.5f * s * x1f * x1f;
            row[x0i] += d * a0;
            if (x1i == x0i + 2) {
                row[x0i + 1] += d * (1.0f - a0 - am);
            } else {
                const float a1 = s * (1.5f - x0f);
                row[x0i + 1] += d * (a1 - a0);
                for (int xi = x0i + 2; xi < x1i - 1; ++xi) row[xi] += d * s;
                const float a2 = a1 + float(x1i - x0i - 3) * s;
                row[x1i - 1] += d * (1.0f - a2 - am);
            }
            row[x1i] += d * am;
        }
        x = xnext;
    }
}

}  // namespace

void Path::bounds(float& x0, float& y0, float& x1, float& y1) const {
    x0 = y0 = 1e30f;
    x1 = y1 = -1e30f;
    for (const Cmd& c : cmds) {
        x0 = std::min(x0, c.x);
        x1 = std::max(x1, c.x);
        y0 = std::min(y0, c.y);
        y1 = std::max(y1, c.y);
        if (c.op == Quad) {
            x0 = std::min(x0, c.cx);
            x1 = std::max(x1, c.cx);
            y0 = std::min(y0, c.cy);
            y1 = std::max(y1, c.cy);
        }
    }
    if (cmds.empty()) x0 = y0 = x1 = y1 = 0.0f;
}

void rasterize(const Path& p, float sx, float sy, float ox, float oy, Bitmap& out, int w, int h) {
    out.w = std::max(0, w);
    out.h = std::max(0, h);
    out.pix.assign(size_t(out.w) * size_t(out.h), 0);
    if (out.w == 0 || out.h == 0 || p.cmds.empty()) return;
    const int stride = out.w + 2;
    std::vector<float> acc(size_t(stride) * size_t(out.h) + 2, 0.0f);
    float cx = 0, cy = 0, startX = 0, startY = 0;
    bool open = false;
    auto close = [&]() {
        if (open && (cx != startX || cy != startY)) drawLine(acc.data(), out.w, out.h, stride, cx, cy, startX, startY);
        open = false;
    };
    for (const Path::Cmd& c : p.cmds) {
        const float x = c.x * sx + ox, y = c.y * sy + oy;
        if (c.op == Path::Move) {
            close();
            cx = startX = x;
            cy = startY = y;
            open = true;
        } else if (c.op == Path::Line) {
            drawLine(acc.data(), out.w, out.h, stride, cx, cy, x, y);
            cx = x;
            cy = y;
        } else {
            const float qx = c.cx * sx + ox, qy = c.cy * sy + oy;
            // Segments by the curve's deviation from its chord (about 0.1 px).
            const float ddx = cx - 2.0f * qx + x, ddy = cy - 2.0f * qy + y;
            const float dev = std::sqrt(ddx * ddx + ddy * ddy);
            int n = 1 + int(std::sqrt(dev * 2.5f));
            if (n > 40) n = 40;
            float px = cx, py = cy;
            for (int i = 1; i <= n; ++i) {
                const float t = float(i) / float(n), u = 1.0f - t;
                const float nx = u * u * cx + 2.0f * u * t * qx + t * t * x;
                const float ny = u * u * cy + 2.0f * u * t * qy + t * t * y;
                drawLine(acc.data(), out.w, out.h, stride, px, py, nx, ny);
                px = nx;
                py = ny;
            }
            cx = x;
            cy = y;
        }
    }
    close();
    for (int y = 0; y < out.h; ++y) {
        float s = 0.0f;
        const float* row = acc.data() + size_t(y) * size_t(stride);
        uint8_t* o = out.pix.data() + size_t(y) * size_t(out.w);
        for (int x = 0; x < out.w; ++x) {
            s += row[x];
            float c = std::fabs(s);
            if (c > 1.0f) c = 1.0f;
            o[x] = uint8_t(c * 255.0f + 0.5f);
        }
    }
}

bool FontFace::load(const uint8_t* data, size_t size, std::string& err) {
    data_ = nullptr;
    if (size < 12) {
        err = "too small";
        return false;
    }
    const int numTables = u16(data + 4);
    if (size < size_t(12 + 16 * numTables)) {
        err = "bad table directory";
        return false;
    }
    uint32_t head = 0, hhea = 0, maxp = 0;
    for (int i = 0; i < numTables; ++i) {
        const uint8_t* r = data + 12 + 16 * i;
        const uint32_t off = u32(r + 8), len = u32(r + 12);
        if (size_t(off) + len > size) continue;
        if (!std::memcmp(r, "head", 4)) head = off;
        else if (!std::memcmp(r, "hhea", 4)) hhea = off;
        else if (!std::memcmp(r, "maxp", 4)) maxp = off;
        else if (!std::memcmp(r, "cmap", 4)) cmap_ = off;
        else if (!std::memcmp(r, "hmtx", 4)) hmtx_ = off;
        else if (!std::memcmp(r, "loca", 4)) loca_ = off;
        else if (!std::memcmp(r, "glyf", 4)) glyf_ = off;
        else if (!std::memcmp(r, "GPOS", 4)) gpos_ = off;
    }
    if (!head || !hhea || !maxp || !cmap_ || !hmtx_ || !loca_ || !glyf_) {
        err = "missing tables (a TrueType outline font is needed)";
        return false;
    }
    unitsPerEm_ = u16(data + head + 18);
    indexToLoc_ = s16(data + head + 50);
    ascent_ = s16(data + hhea + 4);
    descent_ = -s16(data + hhea + 6);
    lineGap_ = s16(data + hhea + 8);
    numHMetrics_ = u16(data + hhea + 34);
    numGlyphs_ = u16(data + maxp + 4);
    // cmap: a Unicode subtable, format 12 preferred over 4.
    const int nt = u16(data + cmap_ + 2);
    uint32_t best = 0, bestFormat = 0;
    for (int i = 0; i < nt; ++i) {
        const uint8_t* r = data + cmap_ + 4 + 8 * i;
        const int pid = u16(r), eid = u16(r + 2);
        const uint32_t sub = cmap_ + u32(r + 4);
        if (sub + 2 > size) continue;
        const int fmt = u16(data + sub);
        const bool unicode = pid == 0 || (pid == 3 && (eid == 1 || eid == 10));
        if (!unicode) continue;
        if (fmt == 12 && bestFormat != 12) {
            best = sub;
            bestFormat = 12;
        } else if (fmt == 4 && bestFormat == 0) {
            best = sub;
            bestFormat = 4;
        }
    }
    if (!best) {
        err = "no Unicode character map";
        return false;
    }
    cmap_ = best;
    cmapFormat_ = bestFormat;
    data_ = data;
    size_ = size;
    // GPOS: the PairPos subtables of every lookup of the "kern" features.
    kernLookups_.clear();
    if (gpos_ && gpos_ + 10 <= size) {
        const uint32_t featureList = gpos_ + u16(data + gpos_ + 6);
        const uint32_t lookupList = gpos_ + u16(data + gpos_ + 8);
        std::vector<int> lookups;
        const int nf = u16(data + featureList);
        for (int i = 0; i < nf; ++i) {
            const uint8_t* fr = data + featureList + 2 + 6 * i;
            if (std::memcmp(fr, "kern", 4)) continue;
            const uint32_t ft = featureList + u16(fr + 4);
            const int n = u16(data + ft + 2);
            for (int k = 0; k < n; ++k) {
                const int li = u16(data + ft + 4 + 2 * k);
                if (std::find(lookups.begin(), lookups.end(), li) == lookups.end()) lookups.push_back(li);
            }
        }
        std::sort(lookups.begin(), lookups.end());
        const int nl = u16(data + lookupList);
        for (int li : lookups) {
            if (li >= nl) continue;
            const uint32_t lk = lookupList + u16(data + lookupList + 2 + 2 * li);
            int type = u16(data + lk);
            const int ns = u16(data + lk + 4);
            std::vector<uint32_t> subs;
            for (int s = 0; s < ns; ++s) {
                uint32_t st = lk + u16(data + lk + 6 + 2 * s);
                int t = type;
                if (t == 9) {  // extension
                    t = u16(data + st + 2);
                    st += u32(data + st + 4);
                }
                if (t == 2) subs.push_back(st);
            }
            if (!subs.empty()) kernLookups_.push_back(std::move(subs));
        }
    }
    kernCache_.clear();
    glyphCache_.clear();
    return true;
}

int FontFace::glyphIndex(uint32_t cp) const {
    if (!data_) return 0;
    auto it = glyphCache_.find(cp);
    if (it != glyphCache_.end()) return it->second;
    int g = 0;
    const uint8_t* t = data_ + cmap_;
    if (cmapFormat_ == 12) {
        const uint32_t n = u32(t + 12);
        uint32_t lo = 0, hi = n;
        while (lo < hi) {
            const uint32_t mid = (lo + hi) / 2;
            const uint8_t* r = t + 16 + 12 * mid;
            if (cp < u32(r)) hi = mid;
            else if (cp > u32(r + 4)) lo = mid + 1;
            else {
                g = int(u32(r + 8) + (cp - u32(r)));
                break;
            }
        }
    } else if (cp <= 0xFFFF) {
        const int segX2 = u16(t + 6), seg = segX2 / 2;
        const uint8_t* ends = t + 14;
        const uint8_t* starts = ends + segX2 + 2;
        const uint8_t* deltas = starts + segX2;
        const uint8_t* ranges = deltas + segX2;
        int lo = 0, hi = seg;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (cp > u16(ends + 2 * mid)) lo = mid + 1;
            else hi = mid;
        }
        if (lo < seg && cp >= u16(starts + 2 * lo)) {
            const int ro = u16(ranges + 2 * lo);
            const int delta = s16(deltas + 2 * lo);
            if (ro == 0) {
                g = (int(cp) + delta) & 0xFFFF;
            } else {
                const uint8_t* gp = ranges + 2 * lo + ro + 2 * (cp - u16(starts + 2 * lo));
                if (gp + 2 <= data_ + size_) {
                    const int v = u16(gp);
                    g = v ? (v + delta) & 0xFFFF : 0;
                }
            }
        }
    }
    if (g >= numGlyphs_) g = 0;
    glyphCache_[cp] = g;
    return g;
}

int FontFace::advance(int glyph) const {
    if (!data_ || numHMetrics_ <= 0) return 0;
    const int i = glyph < numHMetrics_ ? glyph : numHMetrics_ - 1;
    return u16(data_ + hmtx_ + 4 * i);
}

uint32_t FontFace::glyphOffset(int glyph, uint32_t& len) const {
    len = 0;
    if (glyph < 0 || glyph >= numGlyphs_) return 0;
    uint32_t a, b;
    if (indexToLoc_ == 0) {
        a = uint32_t(u16(data_ + loca_ + 2 * glyph)) * 2;
        b = uint32_t(u16(data_ + loca_ + 2 * glyph + 2)) * 2;
    } else {
        a = u32(data_ + loca_ + 4 * glyph);
        b = u32(data_ + loca_ + 4 * glyph + 4);
    }
    if (b <= a || size_t(glyf_) + b > size_) return 0;
    len = b - a;
    return glyf_ + a;
}

bool FontFace::outline(int glyph, Path& out) const {
    out.cmds.clear();
    if (!data_) return false;
    const float m[6] = {1, 0, 0, 1, 0, 0};
    outlineAt(glyph, out, m, 0);
    return !out.cmds.empty();
}

bool FontFace::outlineAt(int glyph, Path& out, const float m[6], int depth) const {
    uint32_t len = 0;
    const uint32_t off = glyphOffset(glyph, len);
    if (!off || len < 10 || depth > 8) return false;
    const uint8_t* g = data_ + off;
    const uint8_t* end = g + len;
    const int nc = s16(g);
    auto xf = [&](float x, float y, float& ox, float& oy) {
        ox = m[0] * x + m[2] * y + m[4];
        oy = m[1] * x + m[3] * y + m[5];
    };
    if (nc >= 0) {
        const uint8_t* p = g + 10;
        if (p + 2 * nc + 2 > end) return false;
        std::vector<int> ends(static_cast<size_t>(nc));
        for (int i = 0; i < nc; ++i) ends[size_t(i)] = u16(p + 2 * i);
        const int npts = nc ? ends.back() + 1 : 0;
        p += 2 * nc;
        const int ilen = u16(p);
        p += 2 + ilen;
        std::vector<uint8_t> flags(static_cast<size_t>(npts));
        for (int i = 0; i < npts && p < end;) {
            const uint8_t f = *p++;
            flags[size_t(i++)] = f;
            if (f & 8) {
                if (p >= end) return false;
                int r = *p++;
                while (r-- > 0 && i < npts) flags[size_t(i++)] = f;
            }
        }
        std::vector<float> xs(static_cast<size_t>(npts)), ys(static_cast<size_t>(npts));
        int v = 0;
        for (int i = 0; i < npts; ++i) {
            const uint8_t f = flags[size_t(i)];
            if (f & 2) {
                if (p >= end) return false;
                const int d = *p++;
                v += (f & 16) ? d : -d;
            } else if (!(f & 16)) {
                if (p + 2 > end) return false;
                v += s16(p);
                p += 2;
            }
            xs[size_t(i)] = float(v);
        }
        v = 0;
        for (int i = 0; i < npts; ++i) {
            const uint8_t f = flags[size_t(i)];
            if (f & 4) {
                if (p >= end) return false;
                const int d = *p++;
                v += (f & 32) ? d : -d;
            } else if (!(f & 32)) {
                if (p + 2 > end) return false;
                v += s16(p);
                p += 2;
            }
            ys[size_t(i)] = float(v);
        }
        int first = 0;
        struct P {
            float x, y;
            bool on;
        };
        std::vector<P> c;
        for (int ci = 0; ci < nc; ++ci) {
            const int last = ends[size_t(ci)];
            c.clear();
            for (int i = first; i <= last && i < npts; ++i) {
                float tx, ty;
                xf(xs[size_t(i)], ys[size_t(i)], tx, ty);
                c.push_back({tx, ty, (flags[size_t(i)] & 1) != 0});
            }
            first = last + 1;
            if (c.size() < 2) continue;
            // Start on an on-curve point (the implied middle when there is none).
            size_t s = 0;
            while (s < c.size() && !c[s].on) ++s;
            if (s == c.size()) {
                P mid{(c.back().x + c[0].x) * 0.5f, (c.back().y + c[0].y) * 0.5f, true};
                c.insert(c.begin(), mid);
                s = 0;
            }
            std::rotate(c.begin(), c.begin() + long(s), c.end());
            out.moveTo(c[0].x, c[0].y);
            bool haveCtrl = false;
            float qx = 0, qy = 0;
            for (size_t i = 1; i <= c.size(); ++i) {
                const P& q = c[i % c.size()];
                if (q.on) {
                    if (haveCtrl) out.quadTo(qx, qy, q.x, q.y);
                    else out.lineTo(q.x, q.y);
                    haveCtrl = false;
                } else {
                    if (haveCtrl) out.quadTo(qx, qy, (qx + q.x) * 0.5f, (qy + q.y) * 0.5f);
                    qx = q.x;
                    qy = q.y;
                    haveCtrl = true;
                }
            }
        }
        return true;
    }
    // Composite glyph.
    const uint8_t* p = g + 10;
    for (;;) {
        if (p + 4 > end) return false;
        const int flags = u16(p);
        const int gi = u16(p + 2);
        p += 4;
        float dx = 0, dy = 0;
        if (flags & 1) {
            if (p + 4 > end) return false;
            dx = float(s16(p));
            dy = float(s16(p + 2));
            p += 4;
        } else {
            if (p + 2 > end) return false;
            dx = float(int8_t(p[0]));
            dy = float(int8_t(p[1]));
            p += 2;
        }
        if (!(flags & 2)) dx = dy = 0;  // point matching: not used by the fonts we ship
        float a = 1, b = 0, c = 0, d = 1;
        auto f2dot14 = [](const uint8_t* q) { return float(s16(q)) / 16384.0f; };
        if (flags & 8) {
            a = d = f2dot14(p);
            p += 2;
        } else if (flags & 0x40) {
            a = f2dot14(p);
            d = f2dot14(p + 2);
            p += 4;
        } else if (flags & 0x80) {
            a = f2dot14(p);
            b = f2dot14(p + 2);
            c = f2dot14(p + 4);
            d = f2dot14(p + 6);
            p += 8;
        }
        // child point -> (a x + c y + dx, b x + d y + dy), then the parent's matrix.
        const float cm[6] = {m[0] * a + m[2] * b, m[1] * a + m[3] * b, m[0] * c + m[2] * d, m[1] * c + m[3] * d,
                             m[0] * dx + m[2] * dy + m[4], m[1] * dx + m[3] * dy + m[5]};
        outlineAt(gi, out, cm, depth + 1);
        if (!(flags & 0x20)) break;
    }
    return true;
}

int FontFace::coverageIndex(uint32_t t, int glyph) const {
    const uint8_t* c = data_ + t;
    const int fmt = u16(c);
    if (fmt == 1) {
        const int n = u16(c + 2);
        int lo = 0, hi = n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            const int v = u16(c + 4 + 2 * mid);
            if (v == glyph) return mid;
            if (v < glyph) lo = mid + 1;
            else hi = mid;
        }
    } else if (fmt == 2) {
        const int n = u16(c + 2);
        int lo = 0, hi = n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            const uint8_t* r = c + 4 + 6 * mid;
            if (glyph < u16(r)) hi = mid;
            else if (glyph > u16(r + 2)) lo = mid + 1;
            else return u16(r + 4) + glyph - u16(r);
        }
    }
    return -1;
}

int FontFace::classOf(uint32_t t, int glyph) const {
    const uint8_t* c = data_ + t;
    const int fmt = u16(c);
    if (fmt == 1) {
        const int start = u16(c + 2), n = u16(c + 4);
        if (glyph >= start && glyph < start + n) return u16(c + 6 + 2 * (glyph - start));
    } else if (fmt == 2) {
        const int n = u16(c + 2);
        int lo = 0, hi = n;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            const uint8_t* r = c + 4 + 6 * mid;
            if (glyph < u16(r)) hi = mid;
            else if (glyph > u16(r + 2)) lo = mid + 1;
            else return u16(r + 4);
        }
    }
    return 0;
}

int FontFace::kerning(int left, int right) const {
    if (!data_ || kernLookups_.empty()) return 0;
    const uint32_t key = (uint32_t(left) << 16) | uint32_t(right & 0xFFFF);
    auto it = kernCache_.find(key);
    if (it != kernCache_.end()) return it->second;
    const int v = kerningUncached(left, right);
    kernCache_[key] = int16_t(v);
    return v;
}

int FontFace::kerningUncached(int left, int right) const {
    int total = 0;
    for (const std::vector<uint32_t>& lookup : kernLookups_)
    for (uint32_t st : lookup) {
        const uint8_t* s = data_ + st;
        const int fmt = u16(s);
        const int ci = coverageIndex(st + u16(s + 2), left);
        if (ci < 0) continue;
        const int vf1 = u16(s + 4), vf2 = u16(s + 6);
        const int size1 = 2 * popcount16(unsigned(vf1)), size2 = 2 * popcount16(unsigned(vf2));
        // XAdvance's place in the first value record (after XPlacement and YPlacement if present).
        if (!(vf1 & 4)) continue;
        const int xadv = 2 * popcount16(unsigned(vf1 & 3));
        if (fmt == 1) {
            const int n = u16(s + 8);
            if (ci >= n) continue;
            const uint8_t* ps = s + u16(s + 10 + 2 * ci);
            const int count = u16(ps);
            const int rec = 2 + size1 + size2;
            int lo = 0, hi = count;
            bool found = false;
            while (lo < hi) {
                const int mid = (lo + hi) / 2;
                const uint8_t* r = ps + 2 + rec * mid;
                const int g2 = u16(r);
                if (g2 == right) {
                    total += s16(r + 2 + xadv);
                    found = true;
                    break;
                }
                if (g2 < right) lo = mid + 1;
                else hi = mid;
            }
            if (found) break;  // a pair found in a subtable ends the lookup's search
        } else if (fmt == 2) {
            const int c1 = classOf(st + u16(s + 8), left);
            const int c2 = classOf(st + u16(s + 10), right);
            const int n1 = u16(s + 12), n2 = u16(s + 14);
            if (c1 >= n1 || c2 >= n2) continue;
            const uint8_t* r = s + 16 + (size1 + size2) * (c1 * n2 + c2);
            total += s16(r + xadv);
            break;
        }
    }
    return total;
}

}  // namespace ui
}  // namespace cc
