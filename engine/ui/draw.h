// The UI's draw list: what the UI draws in a frame, in device pixels, as a sequence of
// primitives — rounded rectangles with borders and two-stop gradients, soft shadows, glyphs from
// the text atlas, images, and the blurred backdrop of glass panels — with a clip rectangle each.
// Backends: ui/gl_backend.* (WebGL 2 / OpenGL ES 3, instanced quads) and ui/soft_backend.* (the
// same pixels on the CPU, for the native UI tests' snapshots).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {
namespace ui {

struct Color {
    float r = 0, g = 0, b = 0, a = 0;  // straight alpha, 0 … 1
    constexpr Color() = default;
    constexpr Color(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}
    bool visible() const { return a > 0.001f; }
    Color withAlpha(float x) const { return Color(r, g, b, a * x); }
};
// rgba(r, g, b, a) with 0 – 255 channels.
constexpr Color rgba(int r, int g, int b, float a = 1.0f) { return Color(r / 255.0f, g / 255.0f, b / 255.0f, a); }
// #rrggbb
constexpr Color hex(uint32_t v, float a = 1.0f) {
    return Color(float((v >> 16) & 255) / 255.0f, float((v >> 8) & 255) / 255.0f, float(v & 255) / 255.0f, a);
}
inline Color mix(const Color& a, const Color& b, float t) {
    return Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect intersect(const Rect& o) const;
    Rect inset(float l, float t, float r, float b) const { return Rect{x + l, y + t, w - l - r, h - t - b}; }
    Rect expand(float d) const { return Rect{x - d, y - d, w + 2 * d, h + 2 * d}; }
    bool empty() const { return w <= 0 || h <= 0; }
};

// An image the UI can draw: a GL texture (gl backend) and/or CPU pixels (soft backend).
struct ImageRef {
    uint32_t gl = 0;               // GL texture name (0: none)
    const uint8_t* rgba = nullptr; // CPU pixels, w × h × 4
    int w = 0, h = 0;
    bool valid() const { return gl != 0 || rgba != nullptr; }
};

enum PrimKind : uint8_t {
    PrimRect = 0,     // rounded box: fill (solid or gradient) and border
    PrimShadow = 1,   // blurred rounded box (box-shadow); flag OutsideOnly
    PrimGlyph = 2,    // atlas coverage × fill
    PrimImage = 3,    // image × fill (tint), rounded corners
    PrimBackdrop = 4, // the last capture, blurred, through the rounded box
    PrimFilter = 5,   // the last capture (sharp) through a colour filter: brightness, saturation
};

enum GradientKind : uint8_t { GradNone = 0, GradLinear = 1, GradRadial = 2 };

struct Prim {
    PrimKind kind = PrimRect;
    Rect box;                      // device px
    float radius[4] = {0, 0, 0, 0}; // top-left, top-right, bottom-right, bottom-left
    Color fill, fill2, border;
    float borderW = 0;
    GradientKind grad = GradNone;
    float gradAngle = 3.14159265f;  // CSS angle (radians): 0 to the top, π/2 to the right
    float blur = 0;                 // shadow: blur radius (device px; σ = blur / 2); filter: brightness
    float spread = 0;               // shadow spread; filter: saturation
    bool outsideOnly = false;       // shadow drawn only outside `box` (CSS box-shadow)
    float uv[4] = {0, 0, 0, 0};     // glyph or image: u0 v0 u1 v1; shadow: the offset in uv[0], uv[1]
    Rect clip;                      // device px
    int image = -1;                 // PrimImage: index into DrawList::images
};

struct DrawCmd {
    enum Type { Prims, Capture } type = Prims;
    int first = 0, count = 0;  // Prims: primitives [first, first + count)
    float blur = 0;            // Capture: blur radius of the backdrop (device px)
};

class DrawList {
public:
    void clear();
    void setViewport(int w, int h) {
        width = w;
        height = h;
    }
    int width = 1, height = 1;
    std::vector<Prim> prims;
    std::vector<DrawCmd> cmds;
    std::vector<ImageRef> images;

    // ---- clip stack
    void pushClip(const Rect& r);  // intersected with the current one
    void popClip();
    const Rect& clip() const { return clips_.empty() ? full_ : clips_.back(); }

    // ---- primitives (device px)
    void rect(const Rect& r, const Color& fill, float radius = 0, float borderW = 0, const Color& border = Color());
    void rectRadii(const Rect& r, const Color& fill, const float radius[4], float borderW, const Color& border);
    void gradient(const Rect& r, const Color& a, const Color& b, float angle, const float radius[4], GradientKind kind = GradLinear);
    // A box-shadow of the element box `r`: offset (dx, dy), blur radius, spread.
    void shadow(const Rect& r, float dx, float dy, const Color& c, float blur, float spread, const float radius[4], bool outsideOnly);
    void glyph(const Rect& r, const float uv[4], const Color& c);
    void image(const Rect& r, const ImageRef& img, const float uv[4], const Color& tint, const float radius[4]);
    void backdrop(const Rect& r, const float radius[4]);
    void filter(const Rect& r, float brightness, float saturation);
    // Captures what is drawn so far (the 3D view and the UI below) for the backdrops that follow.
    void capture(float blur);
    bool hasCapture() const;

    void add(const Prim& p);

private:
    std::vector<Rect> clips_;
    Rect full_{0, 0, 1e9f, 1e9f};
};

}  // namespace ui
}  // namespace cc
