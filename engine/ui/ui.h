// The engine's UI: a tree of boxes built every frame by the screens (immediate style: a widget
// call returns whether it was clicked), laid out with a small flexbox and grid (ui/layout.cpp),
// painted into a DrawList (ui/paint.cpp), with input handled against the previous frame's layout:
// pointer hit-testing, keyboard and gamepad focus navigation, text fields, sliders, scrolling,
// tooltips, toasts and modal layers.
//
// Units: layout in "px" (CSS pixels); the draw list in device pixels (px × scale). The scale comes
// from the display's pixel density and the window size (Ui::chooseScale).
#pragma once

#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ui/draw.h"
#include "ui/host.h"
#include "ui/input.h"
#include "ui/text.h"

namespace cc {
namespace ui {

using Id = uint64_t;
constexpr float kAuto = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = 1e9f;
inline bool isAuto(float v) { return std::isnan(v); }

// ------------------------------------------------------------------------------------- style

enum class Dir : uint8_t { Column, Row, Grid, Stack };
enum class Align : uint8_t { Start, Center, End, Stretch, Baseline, Auto };
enum class Justify : uint8_t { Start, Center, End, SpaceBetween };
enum class TextAlign : uint8_t { Left, Center, Right };

struct Edges {
    float l = 0, t = 0, r = 0, b = 0;
    Edges() = default;
    Edges(float all) : l(all), t(all), r(all), b(all) {}
    Edges(float v, float h) : l(h), t(v), r(h), b(v) {}
    Edges(float t_, float r_, float b_, float l_) : l(l_), t(t_), r(r_), b(b_) {}
    float h() const { return l + r; }
    float v() const { return t + b; }
};

struct Shadow {
    float dx = 0, dy = 0, blur = 0, spread = 0;
    Color color;
};

// A grid column: fixed px, as wide as its widest cell (auto), or a share of the rest (fr).
struct Track {
    enum Kind : uint8_t { Px, Auto, Fr } kind = Auto;
    float v = 1;
    static Track px(float p) { return Track{Px, p}; }
    static Track autoW() { return Track{Auto, 0}; }
    static Track fr(float f = 1) { return Track{Fr, f}; }
};

struct Style {
    // ---- layout
    Dir dir = Dir::Column;
    Align align = Align::Stretch;   // align-items
    Align self = Align::Auto;       // align-self
    Justify justify = Justify::Start;
    bool wrap = false;              // rows: flex-wrap
    float gap = 0, rowGap = -1;     // rowGap < 0: gap
    Edges pad, mar;
    float w = kAuto, h = kAuto;     // px
    float wPct = kAuto, hPct = kAuto;  // percent of the parent's content box
    float minW = 0, maxW = kInf, minH = 0, maxH = kInf;
    float grow = 0, shrink = 1, basis = kAuto;
    std::vector<Track> cols;        // Dir::Grid
    // Absolute placement inside the parent's padding box (px or percent), moved by `anchor`
    // fractions of the node's own size (CSS translate(-50%)).
    bool absolute = false;
    float left = kAuto, top = kAuto, right = kAuto, bottom = kAuto;
    float leftPct = kAuto, topPct = kAuto;
    float anchorX = 0, anchorY = 0;
    float offsetY = 0;              // drawn this much lower (button press)
    // ---- look
    Color bg, bg2;
    GradientKind grad = GradNone;
    float gradAngle = 3.14159265f;
    Color borderColor;
    Edges border;                   // widths per side (all equal: a rounded border)
    float radius[4] = {0, 0, 0, 0};
    Shadow shadows[3];
    int shadowCount = 0;
    float backdropBlur = 0;         // CSS backdrop-filter: blur(px)
    float opacity = 1;
    bool clip = false;              // overflow: hidden
    bool scrollY = false;           // overflow-y: auto
    // ---- text
    TextStyle text;
    Color color = Color(0.91f, 0.93f, 0.96f, 1.0f);
    TextAlign textAlign = TextAlign::Left;
    bool textVCenter = false;       // the text block centred in a taller box
    float textOffsetX = 0;          // text drawn this far to the left (a scrolled text field)
    bool nowrap = false;
    bool ellipsis = false;
    int maxLines = 0;
    Shadow textShadow;              // color.a == 0: none
    // ---- behaviour
    bool hit = false;               // catches the pointer (panels, cards)
    bool focusable = false;
    bool focusRing = true;          // draw the keyboard focus ring when focused
    Cursor cursor = Cursor::Arrow;

    // Builders.
    Style& row() { dir = Dir::Row; return *this; }
    Style& column() { dir = Dir::Column; return *this; }
    Style& grid(std::vector<Track> c) { dir = Dir::Grid; cols = std::move(c); return *this; }
    Style& stack() { dir = Dir::Stack; return *this; }
    Style& gaps(float g, float rg = -1) { gap = g; rowGap = rg; return *this; }
    Style& padding(const Edges& e) { pad = e; return *this; }
    Style& margin(const Edges& e) { mar = e; return *this; }
    Style& size(float w_, float h_) { w = w_; h = h_; return *this; }
    Style& width(float v) { w = v; return *this; }
    Style& height(float v) { h = v; return *this; }
    Style& fill(const Color& c) { bg = c; return *this; }
    Style& gradient(const Color& a, const Color& b, float angleDeg) {
        bg = a;
        bg2 = b;
        grad = GradLinear;
        gradAngle = angleDeg * 3.14159265f / 180.0f;
        return *this;
    }
    Style& radial(const Color& a, const Color& b) { bg = a; bg2 = b; grad = GradRadial; return *this; }
    Style& outline(float wdt, const Color& c) { border = Edges(wdt); borderColor = c; return *this; }
    Style& round(float r) { for (float& v : radius) v = r; return *this; }
    Style& shadow(float dx, float dy, float blur, float spread, const Color& c) {
        if (shadowCount < 3) shadows[shadowCount++] = Shadow{dx, dy, blur, spread, c};
        return *this;
    }
    Style& font(float sz, int weight = 400) { text.size = sz; text.weight = weight; return *this; }
    Style& ink(const Color& c) { color = c; return *this; }
    Style& alignItems(Align a) { align = a; return *this; }
    Style& justifyContent(Justify j) { justify = j; return *this; }
    Style& flex(float g, float s = 1, float b = kAuto) { grow = g; shrink = s; basis = b; return *this; }
};

// ------------------------------------------------------------------------------------- nodes

struct Node;
class Ui;
// Custom drawing for a node: its border box in device px.
using PaintFn = std::function<void(DrawList&, const Rect& box, float scale)>;

struct Node {
    Id id = 0;
    Style s;
    std::vector<Span> spans;        // text content
    ImageRef image;
    float imageUv[4] = {0, 0, 1, 1};
    Color imageTint = Color(1, 1, 1, 1);
    PaintFn paint;
    std::string tooltip;
    std::string label;              // for tests and focus (the button's words)
    int parent = -1, first = -1, last = -1, next = -1;
    int layer = 0;
    // ---- layout results (px)
    Rect rect;                      // border box, absolute
    float contentH = 0;             // children's extent (scrolling)
    float scroll = 0;
    TextLayout layout;
    float layoutW = -1;             // the width `layout` was made for
    float baseline = -1;            // first line's baseline from the top of the border box
    // measure cache
    float mAvail = -2, mW = 0, mH = 0;
    float maxContent = -1, minContent = -1;
};

enum class Layer : uint8_t { Base = 0, Overlay = 1, Modal = 2, Popup = 3, Toast = 4, Tooltip = 5, Count = 6 };

// The state of an interactive node this frame (from the last frame's layout and this frame's
// input).
struct Interact {
    Id id = 0;
    bool hover = false;     // the pointer is over it (and nothing above it)
    bool active = false;    // held down
    bool focused = false;
    bool focusVisible = false;  // focused by keyboard or gamepad (draw the ring)
    bool clicked = false;   // released over it, or Enter / Space / A while focused
    bool rightClicked = false;
    bool doubleClicked = false;
    Rect rect;              // last frame's rect (px), empty when new
};

// Per-frame input to the UI.
struct FrameInput {
    std::vector<InputEvent> events;
    int width = 1, height = 1;   // device px
    float scale = 1;             // device px per px
    double time = 0;             // seconds
    float dt = 0;
    // The first gamepad (buttons 0 – 16 held, axes) for navigation with repeat.
    bool padConnected = false;
    bool padHeld[17] = {};
    float padAxis[4] = {};
};

class Ui {
public:
    Ui();
    void setHost(Host* h) { host_ = h ? h : &defaultHost_; }
    Host& host() { return *host_; }

    // A scale for a window of w × h device px at `dpr` device px per CSS px: the density, and
    // larger on big windows (more than 1920 × 1080 px of room) so text keeps its size on screen.
    static float chooseScale(int w, int h, float dpr);

    void beginFrame(const FrameInput& in);
    void endFrame();                 // layout, focus bookkeeping, painting
    DrawList& drawList() { return dl_; }
    float scale() const { return scale_; }
    float viewW() const { return viewW_; }  // px
    float viewH() const { return viewH_; }
    double time() const { return time_; }
    float dt() const { return dt_; }

    // ---- tree building
    void layer(Layer l);             // new nodes go to this layer's root
    Layer currentLayer() const { return layer_; }
    // A layer for a while (a popup opened from inside a panel), then back to where the tree was.
    void pushLayer(Layer l);
    void popLayer();
    // A modal layer makes the layers below it inert (no hover, clicks or focus).
    void setModal(Layer l) { modalLayer_ = int(l); }
    Id id(const std::string& key) const;          // in the current scope
    void pushId(const std::string& key);
    void popId();
    Node& open(Id id, const Style& s);
    Node& open(const std::string& key, const Style& s) { return open(id(key), s); }
    Node& open(const Style& s) { return open(autoId(), s); }
    void close();
    Node& leaf(Id id, const Style& s);
    Node& leaf(const Style& s) { return leaf(autoId(), s); }
    Node& text(const std::string& t, const Style& s, Id id = 0);
    Node& rich(std::vector<Span> spans, const Style& s, Id id = 0);
    Node& current() { return nodes_[size_t(stack_.back())]; }
    // Spacer that grows.
    void spacer(float grow = 1);

    // ---- interaction (call before creating the node with the same id)
    // flags: InteractFocusable (keyboard focus), InteractArrows (the widget uses the left/right
    // keys itself), InteractText (a text field: typing goes to it).
    enum : unsigned { InteractFocusable = 1, InteractArrowsLR = 2, InteractArrowsUD = 4, InteractText = 8, InteractDrag = 16 };
    Interact interact(Id id, unsigned flags = InteractFocusable);
    bool isFocused(Id id) const { return focus_ == id; }
    void setFocus(Id id, bool visible = false);
    void clearFocus() { focus_ = 0; }
    Id focus() const { return focus_; }
    bool focusVisible() const { return focusVisible_; }
    // Focus moves to the first focusable node of the next frame (a new screen).
    void focusFirst() {
        focusFirstPending_ = true;
        focusScope_ = 0;
    }
    // The first focusable inside `scope` (a node of this frame) that is not a text field, at
    // the end of the frame; else as focusFirst.
    void focusFirstIn(Id scope) {
        focusFirstPending_ = true;
        focusScope_ = scope;
    }
    // Pointer state (px).
    float pointerX() const { return px_; }
    float pointerY() const { return py_; }
    bool pointerDown() const { return mouseDown_; }
    Id active() const { return active_; }
    // Rect of a node in the last frame (px; empty if unknown).
    Rect lastRect(Id id) const;
    // The UI uses the pointer (over a panel, or dragging) / the keyboard (a text field).
    bool wantsPointer() const { return wantsPointer_; }
    bool wantsKeyboard() const { return focusIsText_; }
    // Keys not used by the UI this frame (screens' shortcuts). `back` is Escape or gamepad B.
    bool keyPressed(Key k, uint8_t mods = 0) const;
    bool backPressed() const { return backPressed_; }
    bool padPressed(Pad p) const { return padPressed_[int(p)]; }
    const std::vector<InputEvent>& events() const { return events_; }
    // The keyboard and gamepad input since the last frame came from a gamepad / keyboard (show
    // gamepad or key hints).
    bool usingPad() const { return usingPad_; }

    // ---- text fields: typing state of the focused field
    struct TextFieldState {
        size_t cursor = 0, anchor = 0;  // byte offsets (selection [min, max))
        float scrollX = 0;
        double blinkStart = 0;
        bool dragging = false;
    };
    TextFieldState& textState(Id id) { return textStates_[id]; }
    // Edits `text` with this frame's typing when field `id` is focused. Returns flags.
    enum : unsigned { EditChanged = 1, EditSubmit = 2, EditCancel = 4 };
    unsigned editText(Id id, std::string& text, size_t maxChars, bool upper);

    // ---- scrolling
    float& scrollOf(Id id) { return scroll_[id]; }
    void scrollIntoView(Id id) { scrollTo_ = id; }

    // ---- toasts and tooltips
    void toast(const std::string& text, float seconds = 4.2f);
    const std::string& toastText() const { return toast_; }
    bool toastShown() const { return toastUntil_ > time_; }

    // ---- tests
    struct Focusable {
        Id id;
        Rect rect;
        int layer;
        unsigned flags;
        std::string label;
    };
    const std::vector<Focusable>& focusables() const { return focusables_; }
    // The node tree of the last frame, for tests (label, rect, layer).
    struct Snapshot {
        Id id;
        std::string label;
        Rect rect;
        Rect visible;  // the part not clipped away (scrolled out: empty)
        int layer;
        bool focusable;
    };
    std::vector<Snapshot> snapshot() const;
    // Theme of the focus ring.
    Color focusRingColor = Color(0.18f, 0.83f, 0.75f, 0.9f);

private:
    friend struct LayoutEngine;
    friend struct Painter;
    Id autoId();
    void handleEvent(const InputEvent& e);
    void moveFocus(int dx, int dy);
    void moveFocusSeq(int dir);
    Id hitTest(float x, float y, bool interactiveOnly) const;
    std::vector<Id> scrollersAt(float x, float y) const;
    void layoutAll();
    void paintAll();
    void finishFocus();

    Host defaultHost_;
    Host* host_ = &defaultHost_;
    DrawList dl_;
    float scale_ = 1, viewW_ = 1, viewH_ = 1;
    double time_ = 0;
    float dt_ = 0;
    // tree
    std::deque<Node> nodes_;
    std::vector<int> stack_;
    std::vector<Id> idStack_;
    int roots_[int(Layer::Count)];
    Layer layer_ = Layer::Base;
    std::vector<std::pair<Layer, std::vector<int>>> layerStack_;
    int modalLayer_ = -1, lastModalLayer_ = -1;
    unsigned autoCounter_ = 0;
    // last frame
    struct LastNode {
        Rect rect;
        int layer = 0;
        bool hit = false, interactive = false;
        Rect clip;
        bool scroll = false;
        float maxScroll = 0;
        int order = 0;
    };
    std::unordered_map<Id, LastNode> last_;
    std::vector<Id> hitOrder_;       // paint order of hit nodes (last frame)
    std::vector<Focusable> focusables_, building_;
    std::unordered_map<Id, unsigned> interactiveNow_;  // nodes made interactive this frame
    std::unordered_map<Id, unsigned> interactiveLast_;
    // input
    std::vector<InputEvent> events_;
    float px_ = -1, py_ = -1;
    bool mouseDown_ = false;
    Id hot_ = 0, active_ = 0, focus_ = 0;
    bool focusVisible_ = false, focusIsText_ = false, focusFirstPending_ = false;
    Id focusScope_ = 0, firstInScope_ = 0;
    unsigned focusFlags_ = 0;
    std::unordered_set<Id> clicked_, rightClicked_, doubleClicked_;
    Id lastClickId_ = 0;
    double lastClickTime_ = 0;
    bool backPressed_ = false;
    bool padPressed_[int(Pad::Count)] = {};
    bool usingPad_ = false;
    std::vector<std::pair<Key, uint8_t>> keysFree_;
    bool wantsPointer_ = false;
    // gamepad repeat
    bool padHeldPrev_[17] = {};
    float padRepeat_[4] = {};  // up, down, left, right
    int padDirPrev_ = -1;
    // touch scrolling
    bool touchActive_ = false, touchScrolling_ = false;
    float touchX0_ = 0, touchY0_ = 0, touchLastY_ = 0;
    Id touchScroller_ = 0;
    // text
    std::unordered_map<Id, TextFieldState> textStates_;
    std::vector<InputEvent> textEvents_;  // typing for the focused field
    std::unordered_map<Id, std::vector<std::pair<uint32_t, float>>> caretStops_;  // character, x (px from caretX0_)
    std::unordered_map<Id, float> caretX0_;
    bool textInputOn_ = false;
    // scrolling
    std::unordered_map<Id, float> scroll_;
    Id scrollTo_ = 0;
    // toast and tooltip
    std::string toast_;
    double toastUntil_ = 0;
    Id tooltipId_ = 0;
    double tooltipSince_ = 0;
    std::string tooltipText_;
    Cursor cursor_ = Cursor::Arrow, cursorSet_ = Cursor::Arrow;
};

}  // namespace ui
}  // namespace cc
