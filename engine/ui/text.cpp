#include "ui/text.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "ui/draw.h"
#include "ui/font_data.h"

namespace cc {
namespace ui {

// ------------------------------------------------------------------------------------- UTF-8

uint32_t decodeUtf8(const std::string& s, size_t& i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        ++i;
        return c;
    }
    int n = 0;
    uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
        n = 1;
        cp = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        n = 2;
        cp = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        n = 3;
        cp = c & 0x07;
    } else {
        ++i;
        return 0xFFFD;
    }
    if (i + size_t(n) >= s.size()) {
        i = s.size();
        return 0xFFFD;
    }
    for (int k = 1; k <= n; ++k) {
        const unsigned char d = static_cast<unsigned char>(s[i + size_t(k)]);
        if ((d & 0xC0) != 0x80) {
            i += size_t(k);
            return 0xFFFD;
        }
        cp = (cp << 6) | (d & 0x3F);
    }
    i += size_t(n) + 1;
    return cp;
}

void appendUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
        s += char(cp);
    } else if (cp < 0x800) {
        s += char(0xC0 | (cp >> 6));
        s += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    } else {
        s += char(0xF0 | (cp >> 18));
        s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
}

namespace {
uint32_t upperOf(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x17F && c != 0x130 && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        const bool oddLower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
        if (oddLower) return (c & 1) ? c : c - 1;
        return (c & 1) ? c - 1 : c;
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;
    if (c >= 0x430 && c <= 0x44F) return c - 32;
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}
}  // namespace

std::string toUpperUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) appendUtf8(out, upperOf(decodeUtf8(s, i)));
    return out;
}

size_t utf8Length(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        decodeUtf8(s, i);
        ++n;
    }
    return n;
}

size_t utf8Prev(const std::string& s, size_t i) {
    if (i == 0) return 0;
    --i;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

size_t utf8Next(const std::string& s, size_t i) {
    if (i >= s.size()) return s.size();
    decodeUtf8(s, i);
    return i;
}

Face faceFor(int weight, bool mono) {
    if (mono) return weight >= 550 ? MonoMedium : MonoRegular;
    if (weight < 450) return SansRegular;
    if (weight < 550) return SansMedium;
    if (weight < 675) return SansSemiBold;
    return SansBold;
}

// ------------------------------------------------------------------------------------- atlas

bool Atlas::place(int gw, int gh, int& x, int& y) {
    if (pix.empty()) pix.assign(size_t(w) * size_t(h), 0);
    if (gw > w || gh > h) return false;
    if (shelfX + gw > w) {
        shelfY += shelfH + 1;
        shelfX = 0;
        shelfH = 0;
    }
    if (shelfY + gh > h) return false;
    x = shelfX;
    y = shelfY;
    shelfX += gw + 1;
    shelfH = std::max(shelfH, gh);
    return true;
}

void Atlas::reset() {
    pix.assign(size_t(w) * size_t(h), 0);
    shelfX = shelfY = shelfH = 0;
    ++generation;
    dirtyY0 = 0;
    dirtyY1 = h;
}

// ------------------------------------------------------------------------------------- icons

namespace {

struct PathBuilder {
    Path& p;
    void poly(std::initializer_list<float> xy) {
        const float* v = xy.begin();
        const size_t n = xy.size() / 2;
        for (size_t i = 0; i < n; ++i) {
            if (i == 0) p.moveTo(v[0], v[1]);
            else p.lineTo(v[2 * i], v[2 * i + 1]);
        }
    }
    void polyv(const std::vector<float>& v) {
        for (size_t i = 0; i + 1 < v.size(); i += 2) {
            if (i == 0) p.moveTo(v[0], v[1]);
            else p.lineTo(v[i], v[i + 1]);
        }
    }
    // A circle of 8 quadratic arcs; `hole` runs the other way round (cuts a hole).
    void circle(float cx, float cy, float r, bool hole = false) {
        const float k = r / std::cos(3.14159265f / 8.0f);
        p.moveTo(cx + r, cy);
        for (int i = 1; i <= 8; ++i) {
            const float s = hole ? -1.0f : 1.0f;
            const float a = s * float(i) * 3.14159265f / 4.0f, am = s * (float(i) - 0.5f) * 3.14159265f / 4.0f;
            p.quadTo(cx + k * std::cos(am), cy + k * std::sin(am), cx + r * std::cos(a), cy + r * std::sin(a));
        }
    }
    void rect(float x0, float y0, float x1, float y1, bool hole = false) {
        if (!hole) poly({x0, y0, x1, y0, x1, y1, x0, y1});
        else poly({x0, y0, x0, y1, x1, y1, x1, y0});
    }
    // A rectangle of width `w` along the segment a → b.
    void bar(float ax, float ay, float bx, float by, float w) {
        const float dx = bx - ax, dy = by - ay;
        const float l = std::sqrt(dx * dx + dy * dy);
        const float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
        poly({ax + nx, ay + ny, ax - nx, ay - ny, bx - nx, by - ny, bx + nx, by + ny});
    }
};

std::vector<float> arc(float cx, float cy, float r, float a0, float a1, int n) {
    std::vector<float> v;
    for (int i = 0; i <= n; ++i) {
        const float a = (a0 + (a1 - a0) * float(i) / float(n)) * 3.14159265f / 180.0f;
        v.push_back(cx + r * std::cos(a));
        v.push_back(cy + r * std::sin(a));
    }
    return v;
}

}  // namespace

bool iconPath(uint32_t cp, Path& out, float& advance) {
    out.cmds.clear();
    PathBuilder b{out};
    advance = 0.95f;
    switch (cp) {
        case 0x25B6:  // ▶
            b.poly({150, -20, 830, 350, 150, 720});
            break;
        case 0x25C0:  // ◀
            b.poly({800, -20, 800, 720, 120, 350});
            break;
        case 0x25B2:  // ▲
            advance = 0.85f;
            b.poly({80, 0, 770, 0, 425, 640});
            break;
        case 0x25BC:  // ▼
            advance = 0.85f;
            b.poly({80, 640, 425, 0, 770, 640});
            break;
        case 0x26A1:  // ⚡
            advance = 0.9f;
            b.poly({380, 780, 220, 330, 440, 330, 330, -100, 740, 470, 520, 470, 700, 780});
            break;
        case 0x271A:  // ✚
            b.rect(130, 255, 870, 445);
            b.rect(405, -20, 595, 720);
            break;
        case 0x263A: {  // ☺
            b.circle(500, 330, 390);
            b.circle(500, 330, 325, true);
            b.circle(370, 430, 52);
            b.circle(630, 430, 52);
            std::vector<float> s = arc(500, 330, 230, 200, 340, 12);
            std::vector<float> in = arc(500, 330, 172, 340, 200, 12);
            s.insert(s.end(), in.begin(), in.end());
            b.polyv(s);
            break;
        }
        case 0x2605: {  // ★
            std::vector<float> v;
            for (int i = 0; i < 10; ++i) {
                const float a = (90.0f + 36.0f * float(i)) * 3.14159265f / 180.0f;
                const float r = (i % 2) ? 165.0f : 410.0f;
                v.push_back(500 + r * std::cos(a));
                v.push_back(320 + r * std::sin(a));
            }
            b.polyv(v);
            break;
        }
        case 0x2261:  // ≡
            advance = 0.9f;
            b.rect(120, 105, 780, 185);
            b.rect(120, 305, 780, 385);
            b.rect(120, 505, 780, 585);
            break;
        case 0x2630:  // ☰
            b.rect(120, 70, 880, 165);
            b.rect(120, 300, 880, 395);
            b.rect(120, 530, 880, 625);
            break;
        case 0x2699: {  // ⚙
            b.circle(500, 330, 270);
            for (int i = 0; i < 8; ++i) {
                const float a = float(i) * 3.14159265f / 4.0f;
                b.bar(500 + 220 * std::cos(a), 330 + 220 * std::sin(a), 500 + 395 * std::cos(a), 330 + 395 * std::sin(a), 135);
            }
            b.circle(500, 330, 112, true);
            break;
        }
        case 0x2302:  // ⌂
            b.poly({150, -30, 400, -30, 400, 210, 600, 210, 600, -30, 850, -30, 850, 390, 500, 720, 150, 390});
            break;
        case 0x270E:  // ✎
            b.poly({170, -40, 322, 6, 843, 527, 737, 633, 216, 112});
            break;
        case 0x23CF:  // ⏏
            b.poly({130, 250, 870, 250, 500, 700});
            b.rect(130, 20, 870, 150);
            break;
        case 0x1F512: {  // 🔒
            advance = 0.9f;
            b.poly({170, -50, 830, -50, 830, 400, 170, 400});
            std::vector<float> v{265, 380, 265, 470};
            std::vector<float> o = arc(500, 470, 235, 180, 0, 14);
            v.insert(v.end(), o.begin(), o.end());
            v.insert(v.end(), {735, 380, 650, 380, 650, 470});
            std::vector<float> in = arc(500, 470, 150, 0, 180, 14);
            v.insert(v.end(), in.begin(), in.end());
            v.insert(v.end(), {350, 380});
            b.polyv(v);
            b.circle(500, 200, 62, true);
            b.rect(470, 50, 530, 200, true);
            break;
        }
        case 0x26A0:  // ⚠
            b.poly({60, -40, 940, -40, 500, 760});
            b.rect(455, 190, 545, 520, true);
            b.rect(455, 40, 545, 130, true);
            break;
        case 0x25CF:  // ●
            advance = 0.8f;
            b.circle(400, 330, 300);
            break;
        case 0x25CB:  // ○
            advance = 0.8f;
            b.circle(400, 330, 300);
            b.circle(400, 330, 230, true);
            break;
        case 0x25A0:  // ■
            advance = 0.8f;
            b.rect(100, -20, 700, 580);
            break;
        case 0x2714:  // ✔
            b.poly({90, 330, 230, 470, 400, 300, 800, 720, 940, 580, 400, 40});
            break;
        case 0x2715:  // ✕
            b.bar(170, 20, 830, 680, 140);
            b.bar(830, 20, 170, 680, 140);
            break;
        default:
            return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------- fonts

Fonts& Fonts::get() {
    static Fonts f;
    return f;
}

bool Fonts::init() {
    if (tried_) return ready_;
    tried_ = true;
    int n = 0;
    const EmbeddedFont* list = embeddedFonts(n);
    static const char* names[FaceCount] = {"IBMPlexSans-Regular.ttf",  "IBMPlexSans-Medium.ttf", "IBMPlexSans-SemiBold.ttf",
                                           "IBMPlexSans-Bold.ttf",     "IBMPlexMono-Regular.ttf", "IBMPlexMono-Medium.ttf"};
    for (int f = 0; f < FaceCount; ++f)
        for (int i = 0; i < n; ++i)
            if (!std::strcmp(list[i].name, names[f])) {
                std::string err;
                if (!faces_[f].load(list[i].data, list[i].size, err)) logWarn("UI font %s: %s", names[f], err.c_str());
            }
    // A missing face falls back to the regular one.
    for (int f = 1; f < FaceCount; ++f)
        if (!faces_[f].loaded()) faces_[f] = faces_[f >= MonoRegular && faces_[MonoRegular].loaded() ? MonoRegular : SansRegular];
    ready_ = faces_[SansRegular].loaded();
    atlas_.pix.assign(size_t(atlas_.w) * size_t(atlas_.h), 0);
    return ready_;
}

float Fonts::ascent(Face f, float px) const {
    const FontFace& F = faces_[f];
    return F.loaded() ? float(F.ascent()) * px / float(F.unitsPerEm()) : px * 0.8f;
}
float Fonts::descent(Face f, float px) const {
    const FontFace& F = faces_[f];
    return F.loaded() ? float(F.descent()) * px / float(F.unitsPerEm()) : px * 0.2f;
}
float Fonts::normalLineHeight(Face f, float px) const {
    const FontFace& F = faces_[f];
    return F.loaded() ? float(F.ascent() + F.descent() + F.lineGap()) * px / float(F.unitsPerEm()) : px * 1.2f;
}

Fonts::Resolved Fonts::resolve(Face f, uint32_t cp) const {
    Resolved r;
    r.face = f;
    if (cp == 0x00A0) cp = ' ';
    int g = faces_[f].glyphIndex(cp);
    if (g == 0 && (f == MonoRegular || f == MonoMedium)) {
        const Face s = f == MonoMedium ? SansMedium : SansRegular;
        g = faces_[s].glyphIndex(cp);
        if (g) r.face = s;
    }
    if (g == 0) {
        Path p;
        float adv;
        if (iconPath(cp, p, adv)) {
            r.icon = true;
            r.glyph = int(cp);
            return r;
        }
    }
    r.glyph = g;
    return r;
}

float Fonts::advance(const Resolved& r, float px) const {
    if (r.icon) {
        Path p;
        float adv = 1.0f;
        iconPath(uint32_t(r.glyph), p, adv);
        return adv * px;
    }
    const FontFace& F = faces_[r.face];
    return float(F.advance(r.glyph)) * px / float(F.unitsPerEm());
}

float Fonts::kerning(Face f, int left, int right, float px) const {
    const FontFace& F = faces_[f];
    return float(F.kerning(left, right)) * px / float(F.unitsPerEm());
}

namespace {
// Coverage to alpha: a little heavier than linear, as light text on the dark glass reads thin
// otherwise (browsers do the same with their text contrast).
const uint8_t* gammaTable() {
    static uint8_t t[256];
    static bool made = false;
    if (!made) {
        for (int i = 0; i < 256; ++i) t[i] = uint8_t(std::lround(255.0 * std::pow(i / 255.0, 0.82)));
        made = true;
    }
    return t;
}

void blurBitmap(Bitmap& b, float sigma) {
    if (sigma <= 0.05f || b.w == 0) return;
    const int r = std::max(1, int(std::ceil(sigma * 3.0f)));
    std::vector<float> k(static_cast<size_t>(2 * r + 1));
    float sum = 0;
    for (int i = -r; i <= r; ++i) sum += k[size_t(i + r)] = std::exp(-float(i * i) / (2.0f * sigma * sigma));
    for (float& v : k) v /= sum;
    std::vector<float> a(b.pix.size()), t(b.pix.size());
    for (size_t i = 0; i < a.size(); ++i) a[i] = float(b.pix[i]);
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) {
                const int xx = x + i;
                if (xx >= 0 && xx < b.w) s += a[size_t(y * b.w + xx)] * k[size_t(i + r)];
            }
            t[size_t(y * b.w + x)] = s;
        }
    for (int y = 0; y < b.h; ++y)
        for (int x = 0; x < b.w; ++x) {
            float s = 0;
            for (int i = -r; i <= r; ++i) {
                const int yy = y + i;
                if (yy >= 0 && yy < b.h) s += t[size_t(yy * b.w + x)] * k[size_t(i + r)];
            }
            b.pix[size_t(y * b.w + x)] = uint8_t(std::clamp(s + 0.5f, 0.0f, 255.0f));
        }
}
}  // namespace

void Fonts::beginFrame() {
    if (!overflow_) return;
    overflow_ = false;
    atlas_.reset();
    slots_.clear();
    slotsGeneration_ = atlas_.generation;
}

const GlyphSlot& Fonts::slot(const Resolved& r, float devicePx, int subpixel, float blur) {
    static const GlyphSlot none;
    if (slotsGeneration_ != atlas_.generation) {
        slots_.clear();
        slotsGeneration_ = atlas_.generation;
    }
    const uint64_t sizeQ = uint64_t(std::clamp(int(std::lround(devicePx * 8.0f)), 1, 16383));
    const uint64_t blurQ = uint64_t(std::clamp(int(std::lround(blur * 2.0f)), 0, 127));
    const uint64_t key = (uint64_t(r.glyph) & 0x1FFFFF) | (uint64_t(r.face & 7) << 21) | (uint64_t(r.icon ? 1 : 0) << 24) |
                         (sizeQ << 25) | (uint64_t(subpixel & 3) << 39) | (blurQ << 41);
    auto it = slots_.find(key);
    if (it != slots_.end()) return it->second;
    const float px = float(sizeQ) / 8.0f;
    Path path;
    float scale = 1.0f;
    if (r.icon) {
        float adv;
        iconPath(uint32_t(r.glyph), path, adv);
        scale = px / 1000.0f;
    } else {
        const FontFace& F = faces_[r.face];
        F.outline(r.glyph, path);
        scale = px / float(F.unitsPerEm());
    }
    GlyphSlot s;
    if (!path.empty()) {
        float x0, y0, x1, y1;
        path.bounds(x0, y0, x1, y1);
        const float fx = float(subpixel) * 0.25f;
        const float b = float(blurQ) / 2.0f;
        const int pad = 1 + int(std::ceil(b * 1.5f));
        const int left = int(std::floor(x0 * scale + fx)) - pad;
        const int right = int(std::ceil(x1 * scale + fx)) + pad;
        const int top = int(std::floor(-y1 * scale)) - pad;
        const int bottom = int(std::ceil(-y0 * scale)) + pad;
        Bitmap bm;
        rasterize(path, scale, -scale, fx - float(left), -float(top), bm, right - left, bottom - top);
        if (b > 0.0f) blurBitmap(bm, b * 0.5f);
        else {
            const uint8_t* g = gammaTable();
            for (uint8_t& v : bm.pix) v = g[v];
        }
        int ax = 0, ay = 0;
        if (!atlas_.place(bm.w, bm.h, ax, ay)) {
            overflow_ = true;  // started again before the next frame
            return none;
        }
        for (int y = 0; y < bm.h; ++y)
            std::memcpy(&atlas_.pix[size_t(ay + y) * size_t(atlas_.w) + size_t(ax)], &bm.pix[size_t(y) * size_t(bm.w)], size_t(bm.w));
        if (atlas_.dirtyY1 <= atlas_.dirtyY0) {
            atlas_.dirtyY0 = ay;
            atlas_.dirtyY1 = ay + bm.h;
        } else {
            atlas_.dirtyY0 = std::min(atlas_.dirtyY0, ay);
            atlas_.dirtyY1 = std::max(atlas_.dirtyY1, ay + bm.h);
        }
        s.x = ax;
        s.y = ay;
        s.w = bm.w;
        s.h = bm.h;
        s.left = left;
        s.top = top;
    }
    return slots_[key] = s;
}

// ------------------------------------------------------------------------------------- layout

namespace {

struct Item {
    int g0, g1;       // glyphs
    float width;      // without trailing spaces
    float trailing;   // width of the trailing spaces
    bool hardBreak;   // a newline follows
};

}  // namespace

void layoutText(const std::vector<Span>& spans, const TextOptions& opt, TextLayout& out) {
    Fonts& F = Fonts::get();
    out.glyphs.clear();
    out.lines.clear();
    out.boxes.clear();
    out.width = out.height = out.naturalWidth = 0;
    out.truncated = false;
    if (!F.ready()) F.init();
    // 1. Shape every span into glyphs and items (break opportunities after spaces).
    std::vector<Item> items;
    std::vector<std::string> texts(spans.size());
    for (size_t si = 0; si < spans.size(); ++si) {
        const Span& sp = spans[si];
        const TextStyle& st = sp.style;
        const Face face = faceFor(st.weight, st.mono);
        const std::string& t = texts[si] = st.upper ? toUpperUtf8(sp.text) : sp.text;
        if (sp.boxed) {
            Item it{int(out.glyphs.size()), 0, 0, 0, false};
            float x = sp.padX;
            int prev = -1;
            for (size_t i = 0; i < t.size();) {
                const size_t at = i;
                const uint32_t cp = decodeUtf8(t, i);
                ShapedGlyph g;
                g.r = F.resolve(face, cp);
                g.cp = cp;
                g.byte = uint32_t(at);
                g.span = uint16_t(si);
                if (prev >= 0 && !g.r.icon) x += F.kerning(face, prev, g.r.glyph, st.size);
                g.x = x;
                g.adv = F.advance(g.r, st.size) + st.letterSpacing;
                x += g.adv;
                prev = g.r.icon ? -1 : g.r.glyph;
                out.glyphs.push_back(g);
            }
            it.g1 = int(out.glyphs.size());
            it.width = x + sp.padX;
            // A zero-width marker glyph keeps the box in the line even when its text is empty.
            items.push_back(it);
            continue;
        }
        int prev = -1;
        Item cur{int(out.glyphs.size()), int(out.glyphs.size()), 0, 0, false};
        float x = 0;
        bool inSpaces = false;
        for (size_t i = 0; i < t.size();) {
            const size_t at = i;
            uint32_t cp = decodeUtf8(t, i);
            if (cp == '\r') continue;
            if (cp == '\n') {
                cur.g1 = int(out.glyphs.size());
                cur.hardBreak = true;
                items.push_back(cur);
                cur = Item{int(out.glyphs.size()), int(out.glyphs.size()), 0, 0, false};
                x = 0;
                prev = -1;
                inSpaces = false;
                continue;
            }
            if (cp == '\t') cp = ' ';
            const bool space = cp == ' ';
            if (!space && inSpaces) {
                // A word starts after spaces: the previous item ends here.
                cur.g1 = int(out.glyphs.size());
                items.push_back(cur);
                cur = Item{int(out.glyphs.size()), int(out.glyphs.size()), 0, 0, false};
                x = 0;
                inSpaces = false;
            }
            ShapedGlyph g;
            g.r = F.resolve(face, cp);
            g.cp = cp;
            g.byte = uint32_t(at);
            g.span = uint16_t(si);
            if (prev >= 0 && !g.r.icon && g.r.face == face) {
                const float k = F.kerning(face, prev, g.r.glyph, st.size);
                if (!out.glyphs.empty()) out.glyphs.back().adv += k;
                x += k;
            }
            g.x = x;
            g.adv = F.advance(g.r, st.size) + st.letterSpacing;
            x += g.adv;
            prev = g.r.icon ? -1 : g.r.glyph;
            out.glyphs.push_back(g);
            if (space) {
                inSpaces = true;
                cur.trailing += g.adv;
            } else {
                cur.width = x;
                cur.trailing = 0;
            }
            // "/" and "-" between letters are break opportunities too.
            if ((cp == '/' || cp == '-') && i < t.size() && t[i] != ' ') {
                cur.g1 = int(out.glyphs.size());
                items.push_back(cur);
                cur = Item{int(out.glyphs.size()), int(out.glyphs.size()), 0, 0, false};
                x = 0;
                prev = -1;
            }
        }
        cur.g1 = int(out.glyphs.size());
        if (cur.g1 > cur.g0) items.push_back(cur);
    }
    // Item widths: a word continued in the next span (no space between) stays one word for
    // breaking; keep it simple: items break only where they were cut.
    // 2. Lines.
    struct LineAcc {
        std::vector<int> items;
        float width = 0;
    };
    std::vector<LineAcc> lines(1);
    // The widest line without wrapping.
    float natural = 0;
    {
        float run = 0;
        for (const Item& it : items) {
            run += it.width + it.trailing;
            if (it.hardBreak) {
                natural = std::max(natural, run - it.trailing);
                run = 0;
            }
        }
        if (!items.empty()) natural = std::max(natural, run - items.back().trailing);
    }
    const bool wrap = opt.wrap;
    const size_t nItems = items.size();
    for (size_t ii = 0; ii < nItems; ++ii) {
        const Item it = items[ii];
        const float w = it.width;
        if (wrap && !lines.back().items.empty() && lines.back().width + w > opt.maxWidth + 0.01f) lines.emplace_back();
        // A single word wider than the line breaks between its characters.
        if (wrap && opt.breakWords && lines.back().items.empty() && w > opt.maxWidth + 0.01f && it.g1 - it.g0 > 1 &&
            !spans[out.glyphs[size_t(it.g0)].span].boxed) {
            float x0 = out.glyphs[size_t(it.g0)].x;
            int g = it.g0;
            while (g < it.g1) {
                int e = g + 1;
                while (e < it.g1 && out.glyphs[size_t(e)].x + out.glyphs[size_t(e)].adv - x0 <= opt.maxWidth) ++e;
                Item part{g, e, out.glyphs[size_t(e - 1)].x + out.glyphs[size_t(e - 1)].adv - x0, 0, false};
                if (e == it.g1) {
                    part.width = it.width - (x0 - out.glyphs[size_t(it.g0)].x);
                    part.trailing = it.trailing;
                    part.hardBreak = it.hardBreak;
                }
                items.push_back(part);
                lines.back().items.push_back(int(items.size()) - 1);
                lines.back().width += part.width + part.trailing;
                if (e < it.g1) lines.emplace_back();
                g = e;
                if (g < it.g1) x0 = out.glyphs[size_t(g)].x;
            }
            if (it.hardBreak) lines.emplace_back();
            continue;
        }
        lines.back().items.push_back(int(ii));
        lines.back().width += w + it.trailing;
        if (it.hardBreak) lines.emplace_back();
    }
    if (lines.size() > 1 && lines.back().items.empty() && nItems > 0 && !items[nItems - 1].hardBreak) lines.pop_back();
    // 3. Place glyphs line by line.
    std::vector<ShapedGlyph> placed;
    placed.reserve(out.glyphs.size() + 2);
    const TextStyle base = spans.empty() ? TextStyle() : spans[0].style;
    float y = 0;
    for (size_t li = 0; li < lines.size(); ++li) {
        if (opt.maxLines > 0 && int(li) >= opt.maxLines) {
            out.truncated = true;
            break;
        }
        TextLine tl;
        tl.g0 = int(placed.size());
        float x = 0, lastVisible = 0;
        // Line metrics: the strut (first span) and every span with glyphs on the line.
        auto metrics = [&](const TextStyle& st, float& asc, float& desc, float& lh) {
            const Face f = faceFor(st.weight, st.mono);
            asc = F.ascent(f, st.size);
            desc = F.descent(f, st.size);
            lh = st.lineHeight > 0 ? st.lineHeight : F.normalLineHeight(f, st.size);
        };
        float asc, desc, lh;
        metrics(base, asc, desc, lh);
        float above = (lh - (asc + desc)) * 0.5f + asc, below = lh - above;
        for (int ii : lines[li].items) {
            const Item& it = items[size_t(ii)];
            if (it.g1 > it.g0) {
                const Span& sp = spans[out.glyphs[size_t(it.g0)].span];
                metrics(sp.style, asc, desc, lh);
                const float a = (lh - (asc + desc)) * 0.5f + asc + sp.raise;
                above = std::max(above, a);
                below = std::max(below, lh - a + 2.0f * sp.raise);
            }
            const float itemX0 = it.g1 > it.g0 ? out.glyphs[size_t(it.g0)].x : 0.0f;
            const bool boxed = it.g1 > it.g0 && spans[out.glyphs[size_t(it.g0)].span].boxed;
            if (boxed) {
                TextBox bx;
                bx.span = out.glyphs[size_t(it.g0)].span;
                bx.line = int(li);
                bx.x = x;
                bx.w = it.width;
                out.boxes.push_back(bx);
                for (int g = it.g0; g < it.g1; ++g) {
                    ShapedGlyph sg = out.glyphs[size_t(g)];
                    sg.x = x + sg.x;
                    placed.push_back(sg);
                }
                x += it.width;
                lastVisible = x;
                continue;
            }
            for (int g = it.g0; g < it.g1; ++g) {
                ShapedGlyph sg = out.glyphs[size_t(g)];
                sg.x = x + (sg.x - itemX0);
                placed.push_back(sg);
            }
            x += it.width + it.trailing;
            lastVisible = x - it.trailing;
        }
        tl.g1 = int(placed.size());
        tl.width = lastVisible;
        tl.top = y;
        tl.height = above + below;
        tl.baseline = y + above;
        y += tl.height;
        out.lines.push_back(tl);
    }
    out.glyphs.swap(placed);
    // 4. Ellipsis: one line cut to the width, or the last allowed line of a wrapped text.
    auto ellipsize = [&](TextLine& L, float maxW) {
        if (L.g1 <= L.g0) return;
        const uint16_t si = out.glyphs[size_t(L.g1 - 1)].span;
        const TextStyle& st = spans[si].style;
        const Face face = faceFor(st.weight, st.mono);
        ShapedGlyph e;
        e.r = F.resolve(face, 0x2026);
        e.cp = 0x2026;
        e.span = si;
        e.adv = F.advance(e.r, st.size) + st.letterSpacing;
        int g = L.g1;
        while (g > L.g0 && out.glyphs[size_t(g - 1)].x + out.glyphs[size_t(g - 1)].adv + e.adv > maxW) --g;
        // Drop the spaces before the ellipsis.
        while (g > L.g0 && out.glyphs[size_t(g - 1)].cp == ' ') --g;
        e.x = g > L.g0 ? out.glyphs[size_t(g - 1)].x + out.glyphs[size_t(g - 1)].adv : 0.0f;
        e.byte = g < L.g1 ? out.glyphs[size_t(g)].byte : 0;
        out.glyphs.erase(out.glyphs.begin() + g, out.glyphs.begin() + L.g1);
        out.glyphs.insert(out.glyphs.begin() + g, e);
        const int removed = L.g1 - g - 1;
        L.g1 = g + 1;
        L.width = e.x + e.adv;
        L.ellipsis = true;
        out.truncated = true;
        for (size_t k = 0; k < out.lines.size(); ++k)
            if (&out.lines[k] != &L && out.lines[k].g0 >= L.g1 + removed) {
                out.lines[k].g0 -= removed;
                out.lines[k].g1 -= removed;
            }
    };
    if (opt.ellipsis) {
        if (!opt.wrap) {
            for (TextLine& L : out.lines)
                if (L.width > opt.maxWidth + 0.01f) ellipsize(L, opt.maxWidth);
        } else if (out.truncated && !out.lines.empty()) {
            ellipsize(out.lines.back(), opt.maxWidth);
        }
    }
    out.naturalWidth = natural;
    for (const TextLine& L : out.lines) out.width = std::max(out.width, L.width);
    out.height = y;
    if (out.lines.empty()) {
        float asc, desc, lh;
        const Face f = faceFor(base.weight, base.mono);
        asc = F.ascent(f, base.size);
        desc = F.descent(f, base.size);
        lh = base.lineHeight > 0 ? base.lineHeight : F.normalLineHeight(f, base.size);
        (void)asc;
        (void)desc;
        out.height = lh;
    }
}

void drawText(DrawList& dl, const std::vector<Span>& spans, const TextLayout& L, float x, float y, float alignWidth, int align,
              float scale, const Color& color, float shadowBlur, const Color* shadowColor, float sdx, float sdy) {
    Fonts& F = Fonts::get();
    const Atlas& A = F.atlas();
    const float iw = 1.0f / float(A.w), ih = 1.0f / float(A.h);
    for (int pass = shadowColor ? 0 : 1; pass < 2; ++pass) {
        const bool sh = pass == 0;
        for (const TextLine& line : L.lines) {
            float off = 0;
            if (align == 1) off = (alignWidth - line.width) * 0.5f;
            else if (align == 2) off = alignWidth - line.width;
            const float base = std::round((y + line.baseline + (sh ? sdy : 0.0f)) * scale);
            for (int gi = line.g0; gi < line.g1; ++gi) {
                const ShapedGlyph& g = L.glyphs[size_t(gi)];
                if (g.cp == ' ' || g.cp == 0x00A0) continue;
                const Span& sp = spans[g.span];
                Color c = color;
                if (sp.color) c = Color(float((sp.color >> 24) & 255) / 255.0f, float((sp.color >> 16) & 255) / 255.0f,
                                        float((sp.color >> 8) & 255) / 255.0f, float(sp.color & 255) / 255.0f * color.a);
                if (sh) c = Color(shadowColor->r, shadowColor->g, shadowColor->b, shadowColor->a * c.a);
                const float pen = (x + off + g.x + (sh ? sdx : 0.0f)) * scale;
                float ix = std::floor(pen);
                int sub = int(std::floor((pen - ix) * 4.0f + 0.5f));
                if (sub >= 4) {
                    sub = 0;
                    ix += 1.0f;
                }
                const float raise = std::round(sp.raise * scale);
                const GlyphSlot& s = F.slot(g.r, sp.style.size * scale, sub, sh ? shadowBlur * scale : 0.0f);
                if (s.w == 0) continue;
                Rect r{ix + float(s.left), base - raise + float(s.top), float(s.w), float(s.h)};
                const float uv[4] = {float(s.x) * iw, float(s.y) * ih, float(s.x + s.w) * iw, float(s.y + s.h) * ih};
                dl.glyph(r, uv, c);
            }
        }
    }
}

float measureText(const std::string& text, const TextStyle& st) {
    TextLayout l;
    TextOptions o;
    o.wrap = false;
    layoutText({Span{text, st}}, o, l);
    return l.width;
}

}  // namespace ui
}  // namespace cc
