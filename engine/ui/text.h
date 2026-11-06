// Text for the engine's UI: the IBM Plex faces compiled into the program (ui/font_data.h), the
// UI's own vector icons for symbols the fonts lack (▶ ★ ⚙ 🔒 …), one glyph atlas shared by
// every size (glyphs are rasterized at the exact device pixel size, with a quarter-pixel
// horizontal position, so text is crisp at any UI scale), UTF-8 shaping with kerning and letter
// spacing, and line layout with wrapping and ellipsis over runs of mixed styles.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ui/font.h"

namespace cc {
namespace ui {

enum Face : uint8_t { SansRegular, SansMedium, SansSemiBold, SansBold, MonoRegular, MonoMedium, FaceCount };
// CSS weights to the faces we ship (400, 500, 600 and 700; heavier is Bold).
Face faceFor(int weight, bool mono = false);

// UTF-8 helpers.
uint32_t decodeUtf8(const std::string& s, size_t& i);  // advances i; U+FFFD on bad bytes
void appendUtf8(std::string& s, uint32_t cp);
std::string toUpperUtf8(const std::string& s);
size_t utf8Length(const std::string& s);  // code points
size_t utf8Prev(const std::string& s, size_t i);  // start of the code point before byte i
size_t utf8Next(const std::string& s, size_t i);

// A glyph's place in the atlas.
struct GlyphSlot {
    int x = 0, y = 0, w = 0, h = 0;  // atlas pixels (w == 0: nothing to draw)
    int left = 0, top = 0;           // bitmap's top-left relative to the pen position (device px)
};

// The single-channel coverage atlas, 2048 × 2048. When it is full, glyphs that do not fit are
// skipped for the frame and the atlas starts again at the next Fonts::beginFrame().
struct Atlas {
    int w = 2048, h = 2048;
    std::vector<uint8_t> pix;
    unsigned generation = 1;  // changes when the atlas is cleared or resized
    // Rows changed since the backend last uploaded them ([dirtyY0, dirtyY1)).
    int dirtyY0 = 0, dirtyY1 = 0;
    // Shelf packing.
    int shelfX = 0, shelfY = 0, shelfH = 0;
    bool place(int gw, int gh, int& x, int& y);
    void reset();
};

class Fonts {
public:
    static Fonts& get();
    // Loads the compiled-in faces (once). False when they are missing.
    bool init();
    bool ready() const { return ready_; }
    const FontFace& face(Face f) const { return faces_[f]; }

    // Metrics in px for a font size in px.
    float ascent(Face f, float px) const;
    float descent(Face f, float px) const;
    float normalLineHeight(Face f, float px) const;

    // The glyph to draw a code point with: a face's glyph, else one of our icons (icon = true).
    struct Resolved {
        Face face = SansRegular;
        int glyph = 0;
        bool icon = false;
    };
    Resolved resolve(Face f, uint32_t cp) const;
    float advance(const Resolved& r, float px) const;
    float kerning(Face f, int left, int right, float px) const;

    // The atlas slot of a glyph at a device pixel size and quarter-pixel offset (0 – 3), `blur`
    // device pixels of Gaussian blur (text shadows); rasterized on first use.
    const GlyphSlot& slot(const Resolved& r, float devicePx, int subpixel, float blur = 0.0f);
    Atlas& atlas() { return atlas_; }
    // Before a frame's text is drawn: starts the atlas again if it overflowed.
    void beginFrame();

private:
    bool ready_ = false, tried_ = false, overflow_ = false;
    FontFace faces_[FaceCount];
    Atlas atlas_;
    std::unordered_map<uint64_t, GlyphSlot> slots_;
    unsigned slotsGeneration_ = 0;
};

// The vector outline of one of the UI's icons (1000 units per em, y up, baseline 0); false
// when `cp` is not an icon. Advance 1000 × `advance`.
bool iconPath(uint32_t cp, Path& out, float& advance);

// ---- text layout

struct TextStyle {
    float size = 14.0f;         // px
    int weight = 400;
    bool mono = false;
    float letterSpacing = 0.0f; // px after every character
    float lineHeight = 0.0f;    // px; 0 = the face's normal line height
    bool upper = false;         // text-transform: uppercase
};

// One run of a rich text: its words and style (and colour, kept for drawing).
struct Span {
    std::string text;
    TextStyle style;
    uint32_t color = 0;  // 0xRRGGBBAA; 0 = the node's colour
    // A boxed span (a key cap): drawn as one unbreakable item with its own padding, background
    // and border (see ui/ui.h), `padX` px on each side.
    bool boxed = false;
    float padX = 0.0f;
    uint32_t boxBg = 0, boxBorder = 0;  // 0xRRGGBBAA
    float boxRadius = 4.0f, boxBorderW = 1.0f, boxBottomW = 1.0f;
    float raise = 0.0f;  // baseline shift in px (positive up)
};

struct ShapedGlyph {
    Fonts::Resolved r;
    float x = 0;          // pen position within the line (px)
    float adv = 0;        // advance including kerning and letter spacing
    uint16_t span = 0;
    uint32_t cp = 0;
    uint32_t byte = 0;    // byte offset of the code point within its span's text
};

struct TextLine {
    int g0 = 0, g1 = 0;   // glyphs [g0, g1)
    float width = 0;      // px
    float top = 0;        // y of the line box's top
    float height = 0;     // line box height
    float baseline = 0;   // y of the baseline
    bool ellipsis = false;  // an ellipsis glyph is the last one
};

struct TextBox {          // a boxed span's place (line-relative x and the line)
    int span = 0, line = 0;
    float x = 0, w = 0;
};

struct TextLayout {
    std::vector<ShapedGlyph> glyphs;
    std::vector<TextLine> lines;
    std::vector<TextBox> boxes;
    float width = 0, height = 0;   // of the laid-out lines
    float naturalWidth = 0;        // widest line without wrapping
    bool truncated = false;
};

struct TextOptions {
    float maxWidth = 1e9f;   // wrap or truncate beyond this
    bool wrap = true;        // false: one line (newlines still break)
    bool ellipsis = false;   // with !wrap: cut with "…" beyond maxWidth
    int maxLines = 0;        // 0 = no limit
    bool breakWords = true;  // a word wider than the line breaks between its characters
};

void layoutText(const std::vector<Span>& spans, const TextOptions& opt, TextLayout& out);

class DrawList;
struct Color;
// Draws a laid-out text: its top-left at (x, y) (px), each line aligned in `alignWidth` (align 0
// left, 1 centre, 2 right), `scale` device pixels per px, `color` for spans without their own.
// A text shadow: blur radius `shadowBlur` (px) of `shadowColor`, offset (sdx, sdy) px.
void drawText(DrawList& dl, const std::vector<Span>& spans, const TextLayout& L, float x, float y, float alignWidth, int align,
              float scale, const Color& color, float shadowBlur = 0.0f, const Color* shadowColor = nullptr, float sdx = 0.0f,
              float sdy = 0.0f);
// Width of one line of plain text.
float measureText(const std::string& text, const TextStyle& st);

}  // namespace ui
}  // namespace cc
