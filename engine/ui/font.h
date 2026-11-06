// TrueType fonts for the engine's own UI: the tables a text renderer needs (cmap, hmtx, loca,
// glyf with composite glyphs, hhea metrics, GPOS pair kerning) and an anti-aliased coverage
// rasterizer for outlines (exact area coverage, no hinting). The same rasterizer draws the UI's
// vector icons, which use the same path type.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace cc {
namespace ui {

// An outline in its own units (fonts: font units, y up). Contours are closed.
struct Path {
    enum Op : uint8_t { Move, Line, Quad };
    struct Cmd {
        Op op;
        float x, y;    // end point
        float cx, cy;  // control point (Quad)
    };
    std::vector<Cmd> cmds;
    void moveTo(float x, float y) { cmds.push_back({Move, x, y, 0, 0}); }
    void lineTo(float x, float y) { cmds.push_back({Line, x, y, 0, 0}); }
    void quadTo(float cx, float cy, float x, float y) { cmds.push_back({Quad, x, y, cx, cy}); }
    bool empty() const { return cmds.empty(); }
    // Bounds of the end and control points.
    void bounds(float& x0, float& y0, float& x1, float& y1) const;
};

// A coverage bitmap (one byte per pixel, 0 – 255).
struct Bitmap {
    int w = 0, h = 0;
    std::vector<uint8_t> pix;
};

// Rasterizes `p` scaled by (sx, sy) and moved by (ox, oy) (pixel = point * s + o; y grows
// downwards in the bitmap, so a font's y-up outline uses a negative sy) into a w × h bitmap,
// non-zero winding, exact area coverage.
void rasterize(const Path& p, float sx, float sy, float ox, float oy, Bitmap& out, int w, int h);

class FontFace {
public:
    bool load(const uint8_t* data, size_t size, std::string& err);
    bool loaded() const { return data_ != nullptr; }

    int glyphIndex(uint32_t codepoint) const;
    int unitsPerEm() const { return unitsPerEm_; }
    // hhea metrics (font units; descent positive downwards).
    int ascent() const { return ascent_; }
    int descent() const { return descent_; }
    int lineGap() const { return lineGap_; }
    int advance(int glyph) const;
    // Horizontal kerning between two glyphs (GPOS "kern" pair adjustment), font units.
    int kerning(int left, int right) const;
    // The glyph's outline in font units (y up). False for an empty glyph (space).
    bool outline(int glyph, Path& out) const;

private:
    bool outlineAt(int glyph, Path& out, const float m[6], int depth) const;
    uint32_t glyphOffset(int glyph, uint32_t& len) const;
    int kerningUncached(int left, int right) const;
    int coverageIndex(uint32_t table, int glyph) const;
    int classOf(uint32_t table, int glyph) const;

    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    uint32_t cmap_ = 0, cmapFormat_ = 0, hmtx_ = 0, loca_ = 0, glyf_ = 0, gpos_ = 0;
    int numGlyphs_ = 0, numHMetrics_ = 0, indexToLoc_ = 0;
    int unitsPerEm_ = 1000, ascent_ = 0, descent_ = 0, lineGap_ = 0;
    std::vector<std::vector<uint32_t>> kernLookups_;  // per "kern" lookup: its PairPos subtables
    mutable std::unordered_map<uint32_t, int16_t> kernCache_;
    mutable std::unordered_map<uint32_t, int> glyphCache_;
};

}  // namespace ui
}  // namespace cc
