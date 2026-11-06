// The UI's draw list on the CPU: the native UI tests render screens offscreen with it and save
// PNG snapshots (no window, no GPU). It follows the GL backend's per-pixel rules.
#pragma once

#include <string>
#include <vector>

#include "ui/draw.h"

namespace cc {
namespace ui {

class SoftCanvas {
public:
    void resize(int w, int h);
    // Fills the canvas with a vertical gradient (a stand-in for the 3D view behind the UI).
    void background(const Color& top, const Color& bottom);
    void render(const DrawList& dl);
    // RGBA8 pixels (straight alpha, opaque where the background was).
    std::vector<uint8_t> rgba8() const;
    bool writePng(const std::string& path) const;
    int width() const { return w_; }
    int height() const { return h_; }
    // A pixel (premultiplied, 0 … 1).
    const float* at(int x, int y) const { return &px_[(size_t(y) * size_t(w_) + size_t(x)) * 4]; }

private:
    void drawPrim(const Prim& p, const DrawList& dl);
    void doCapture(float blur);
    int w_ = 0, h_ = 0;
    std::vector<float> px_, cap_, capBlur_;
};

// PNG (8-bit RGBA, no interlace) of w × h pixels.
bool writePngRgba(const std::string& path, const uint8_t* rgba, int w, int h);

}  // namespace ui
}  // namespace cc
