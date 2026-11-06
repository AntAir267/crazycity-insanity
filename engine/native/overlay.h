// The native build's debug text overlay (until the engine-drawn front end lands): lines of text
// in our own 5 × 7 pixel font over dark boxes, drawn after the frame and before it is shown.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cc {
namespace native {

class Overlay {
public:
    ~Overlay();
    bool init();
    // Pixel size of one font pixel for a frame `h` pixels high.
    static int scaleFor(int h) { return h >= 1400 ? 3 : (h >= 700 ? 2 : 1); }
    static constexpr int kCellW = 6, kCellH = 9;  // character cell in font pixels

    void begin(int w, int h);
    void box(float x, float y, float w, float h, uint32_t rgba);
    // Text at (x, y) (top-left, pixels) in `scale`-sized font pixels; '\n' starts a new line.
    // Bytes outside printable ASCII show as '?'.
    void text(float x, float y, const std::string& s, uint32_t rgba, int scale);
    // A block of lines with a background box; returns its height in pixels.
    float panel(float x, float y, const std::vector<std::string>& lines, uint32_t rgba, int scale, uint32_t bg = 0xA0000000u);
    void end();

private:
    unsigned prog_ = 0, vao_ = 0, vbo_ = 0, tex_ = 0;
    int screenLoc_ = -1, fontLoc_ = -1;
    int w_ = 1, h_ = 1;
    std::vector<float> v_;
    void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t rgba);
};

}  // namespace native
}  // namespace cc
