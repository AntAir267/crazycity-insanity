#include "ui/draw.h"

#include <algorithm>

namespace cc {
namespace ui {

Rect Rect::intersect(const Rect& o) const {
    const float x0 = std::max(x, o.x), y0 = std::max(y, o.y);
    const float x1 = std::min(right(), o.right()), y1 = std::min(bottom(), o.bottom());
    return Rect{x0, y0, std::max(0.0f, x1 - x0), std::max(0.0f, y1 - y0)};
}

void DrawList::clear() {
    prims.clear();
    cmds.clear();
    images.clear();
    clips_.clear();
    full_ = Rect{0, 0, float(width), float(height)};
}

void DrawList::pushClip(const Rect& r) { clips_.push_back(clip().intersect(r)); }

void DrawList::popClip() {
    if (!clips_.empty()) clips_.pop_back();
}

void DrawList::add(const Prim& p0) {
    Prim p = p0;
    p.clip = clip();
    if (p.clip.empty()) return;
    // Nothing of it inside the clip (shadows reach past their box).
    if (p.kind == PrimShadow) {
        const float reach = p.blur * 1.5f + std::max(0.0f, p.spread) + 1.0f;
        const Rect s{p.box.x + p.uv[0], p.box.y + p.uv[1], p.box.w, p.box.h};
        if (s.expand(reach).intersect(p.clip).empty()) return;
    } else if (p.box.expand(1.0f).intersect(p.clip).empty()) {
        return;
    }
    if (cmds.empty() || cmds.back().type != DrawCmd::Prims) cmds.push_back(DrawCmd{DrawCmd::Prims, int(prims.size()), 0, 0});
    prims.push_back(p);
    cmds.back().count++;
}

void DrawList::rect(const Rect& r, const Color& fill, float radius, float borderW, const Color& border) {
    if (!fill.visible() && !(borderW > 0 && border.visible())) return;
    Prim p;
    p.kind = PrimRect;
    p.box = r;
    for (float& v : p.radius) v = radius;
    p.fill = fill;
    p.borderW = borderW;
    p.border = border;
    add(p);
}

void DrawList::rectRadii(const Rect& r, const Color& fill, const float radius[4], float borderW, const Color& border) {
    if (!fill.visible() && !(borderW > 0 && border.visible())) return;
    Prim p;
    p.kind = PrimRect;
    p.box = r;
    for (int i = 0; i < 4; ++i) p.radius[i] = radius[i];
    p.fill = fill;
    p.borderW = borderW;
    p.border = border;
    add(p);
}

void DrawList::gradient(const Rect& r, const Color& a, const Color& b, float angle, const float radius[4], GradientKind kind) {
    Prim p;
    p.kind = PrimRect;
    p.box = r;
    for (int i = 0; i < 4; ++i) p.radius[i] = radius[i];
    p.fill = a;
    p.fill2 = b;
    p.grad = kind;
    p.gradAngle = angle;
    add(p);
}

void DrawList::shadow(const Rect& r, float dx, float dy, const Color& c, float blur, float spread, const float radius[4],
                      bool outsideOnly) {
    if (!c.visible()) return;
    Prim p;
    p.kind = PrimShadow;
    p.box = r;
    p.uv[0] = dx;
    p.uv[1] = dy;
    for (int i = 0; i < 4; ++i) p.radius[i] = radius[i];
    p.fill = c;
    p.blur = blur;
    p.spread = spread;
    p.outsideOnly = outsideOnly;
    add(p);
}

void DrawList::glyph(const Rect& r, const float uv[4], const Color& c) {
    Prim p;
    p.kind = PrimGlyph;
    p.box = r;
    for (int i = 0; i < 4; ++i) p.uv[i] = uv[i];
    p.fill = c;
    add(p);
}

void DrawList::image(const Rect& r, const ImageRef& img, const float uv[4], const Color& tint, const float radius[4]) {
    if (!img.valid()) return;
    Prim p;
    p.kind = PrimImage;
    p.box = r;
    for (int i = 0; i < 4; ++i) {
        p.uv[i] = uv[i];
        p.radius[i] = radius[i];
    }
    p.fill = tint;
    p.image = int(images.size());
    images.push_back(img);
    add(p);
}

void DrawList::backdrop(const Rect& r, const float radius[4]) {
    Prim p;
    p.kind = PrimBackdrop;
    p.box = r;
    for (int i = 0; i < 4; ++i) p.radius[i] = radius[i];
    p.fill = Color(1, 1, 1, 1);
    add(p);
}

void DrawList::filter(const Rect& r, float brightness, float saturation) {
    Prim p;
    p.kind = PrimFilter;
    p.box = r;
    p.blur = brightness;
    p.spread = saturation;
    p.fill = Color(1, 1, 1, 1);
    add(p);
}

void DrawList::capture(float blur) {
    DrawCmd c;
    c.type = DrawCmd::Capture;
    c.blur = blur;
    cmds.push_back(c);
}

bool DrawList::hasCapture() const {
    for (const DrawCmd& c : cmds)
        if (c.type == DrawCmd::Capture) return true;
    return false;
}

}  // namespace ui
}  // namespace cc
