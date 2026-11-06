// The per-pixel rules of the UI primitives, shared in spirit by the GL shader (ui/gl_backend.cpp)
// and used directly by the software backend: the signed distance of a rounded box, a soft shadow
// as a Gaussian of that distance, two-stop gradients interpolated with premultiplied alpha.
#pragma once

#include <algorithm>
#include <cmath>

#include "ui/draw.h"

namespace cc {
namespace ui {

// Signed distance from (px, py) (relative to the box centre) to a rounded box of half size
// (hx, hy) with corner radii r[4] (top-left, top-right, bottom-right, bottom-left), y down.
inline float roundedBoxDistance(float px, float py, float hx, float hy, const float r[4]) {
    float rad = px < 0 ? (py < 0 ? r[0] : r[3]) : (py < 0 ? r[1] : r[2]);
    rad = std::min(rad, std::min(hx, hy));
    const float qx = std::fabs(px) - hx + rad, qy = std::fabs(py) - hy + rad;
    const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::min(std::max(qx, qy), 0.0f) + std::sqrt(ox * ox + oy * oy) - rad;
}

// Abramowitz-Stegun erf (|error| < 5e-4).
inline float erfApprox(float x) {
    const float s = x < 0 ? -1.0f : 1.0f;
    x = std::fabs(x);
    const float t = 1.0f / (1.0f + 0.47047f * x);
    const float y = 1.0f - t * (0.3480242f + t * (-0.0958798f + t * 0.7478556f)) * std::exp(-x * x);
    return s * y;
}

// Gradient position 0 … 1 of a pixel (relative to the box's top-left) for a CSS linear gradient
// of `angle` or the radial gradient (ellipse at the centre, farthest corner).
inline float gradientT(const Prim& p, float lx, float ly) {
    const float w = p.box.w, h = p.box.h;
    if (p.grad == GradRadial) {
        const float dx = (lx - w * 0.5f) / (w * 0.5f * 1.41421356f), dy = (ly - h * 0.5f) / (h * 0.5f * 1.41421356f);
        return std::clamp(std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
    }
    const float sx = std::sin(p.gradAngle), sy = -std::cos(p.gradAngle);  // direction, y down
    const float len = std::fabs(w * sx) + std::fabs(h * sy);
    const float t = ((lx - w * 0.5f) * sx + (ly - h * 0.5f) * sy) / (len > 0 ? len : 1.0f) + 0.5f;
    return std::clamp(t, 0.0f, 1.0f);
}

// Premultiplied colour of a gradient at t.
inline void gradientColor(const Prim& p, float t, float out[4]) {
    const Color& a = p.fill;
    const Color& b = p.fill2;
    const float pa[4] = {a.r * a.a, a.g * a.a, a.b * a.a, a.a};
    const float pb[4] = {b.r * b.a, b.g * b.a, b.b * b.a, b.a};
    for (int i = 0; i < 4; ++i) out[i] = pa[i] + (pb[i] - pa[i]) * t;
}

}  // namespace ui
}  // namespace cc
