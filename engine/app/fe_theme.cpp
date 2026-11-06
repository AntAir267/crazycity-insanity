#include "app/fe_theme.h"

#include <algorithm>
#include <cmath>

namespace cc {
namespace fe {

using namespace ui;

uint32_t rgbaHex(const Color& c) {
    auto b = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return (b(c.r) << 24) | (b(c.g) << 16) | (b(c.b) << 8) | b(c.a);
}

namespace {
Color brighter(const Color& c, float k) { return Color(std::min(1.0f, c.r * k), std::min(1.0f, c.g * k), std::min(1.0f, c.b * k), c.a); }
}  // namespace

Style tH() {
    Style s;
    s.font(22, 750);
    s.text.letterSpacing = -0.22f;
    s.color = col::text;
    s.mar = Edges(0, 0, 4, 0);
    return s;
}
Style tSub() {
    Style s;
    s.font(13);
    s.text.lineHeight = 13 * 1.45f;
    s.color = col::muted;
    s.mar = Edges(0, 0, 14, 0);
    return s;
}
Style tSec() {
    Style s;
    s.font(11);
    s.text.upper = true;
    s.text.letterSpacing = 1.1f;
    s.color = col::muted;
    s.mar = Edges(16, 0, 8, 0);
    return s;
}
Style tNote(bool warn) {
    Style s;
    s.font(12);
    s.text.lineHeight = 18;
    s.color = warn ? col::warn : col::muted;
    s.mar = Edges(8, 0, 8, 0);
    return s;
}
Style tBody(float size, int weight, Color c) {
    Style s;
    s.font(size, weight);
    s.color = c;
    return s;
}
Span span(const std::string& t, float size, int weight, uint32_t color) {
    Span s;
    s.text = t;
    s.style.size = size;
    s.style.weight = weight;
    s.color = color;
    return s;
}
Span kbd(const std::string& t, float size) {
    Span s;
    s.text = t;
    s.style.size = size;
    s.boxed = true;
    s.padX = 5;
    s.boxBg = rgbaHex(rgba(255, 255, 255, 0.06f));
    s.boxBorder = rgbaHex(col::line);
    s.boxRadius = 5;
    return s;
}

void h1(Ui& ui, const std::string& t) { ui.text(t, tH()); }
void sub(Ui& ui, const std::string& t) { ui.text(t, tSub()); }
void sec(Ui& ui, const std::string& t, float marginTop) {
    Style s = tSec();
    s.mar.t = marginTop;
    ui.text(t, s);
}
void note(Ui& ui, const std::string& t, bool warn) { ui.text(t, tNote(warn)); }
void noteRich(Ui& ui, std::vector<Span> spans) {
    Style s = tNote();
    for (Span& sp : spans) {
        if (sp.style.size == 14.0f && !sp.boxed) sp.style.size = 12;
        sp.style.lineHeight = 18;
    }
    ui.rich(std::move(spans), s);
}

void msg(Ui& ui, const std::string& t, Msg kind) {
    Style s;
    s.pad = Edges(10, 12);
    s.round(10);
    s.mar = Edges(10, 0, 10, 0);
    s.font(13);
    if (kind == Msg::Bad) {
        s.bg = rgba(248, 113, 113, 0.1f);
        s.outline(1, rgba(248, 113, 113, 0.35f));
        s.color = col::bad;
    } else if (kind == Msg::Ok) {
        s.bg = rgba(74, 222, 128, 0.08f);
        s.outline(1, rgba(74, 222, 128, 0.3f));
        s.color = col::good;
    } else {
        s.bg = rgba(251, 191, 36, 0.1f);
        s.outline(1, rgba(251, 191, 36, 0.35f));
        s.color = col::warn;
    }
    ui.text(t, s).label = t;  // (tests and the page's test hooks read messages by their words)
}

void badge(Ui& ui, const std::string& t, Badge kind) {
    Style s;
    s.pad = Edges(2, 9);
    s.round(999);
    s.font(11, 700);
    s.text.letterSpacing = 0.44f;
    s.nowrap = true;
    s.self = Align::Center;
    switch (kind) {
        case Badge::Pro:
            s.bg = rgba(251, 191, 36, 0.14f);
            s.color = col::warn;
            break;
        case Badge::Lock:
            s.bg = rgba(248, 113, 113, 0.14f);
            s.color = col::bad;
            break;
        case Badge::Ok:
            s.bg = rgba(74, 222, 128, 0.14f);
            s.color = col::good;
            break;
        case Badge::Muted:
            s.bg = rgba(148, 163, 184, 0.14f);
            s.color = col::muted;
            break;
        default:
            s.bg = rgba(45, 212, 191, 0.14f);
            s.color = col::accent;
            break;
    }
    ui.text(t, s);
}

bool item(Ui& ui, const std::string& key, const std::string& icon, const std::string& title, const std::string& desc, bool big,
          bool disabled) {
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id);
    const bool lit = !disabled && (it.hover || it.focusVisible);
    Style s;
    s.row();
    s.gap = 12;
    s.align = Align::Center;
    s.pad = big ? Edges(15, 16) : Edges(12, 14);
    s.round(12);
    s.bg = lit ? rgba(45, 212, 191, 0.08f) : rgba(255, 255, 255, 0.03f);
    s.outline(1, lit ? col::accent : col::line);
    s.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
    s.focusRing = false;
    s.hit = true;
    if (disabled) s.opacity = 0.45f;
    if (it.active) s.offsetY = 1;
    Node& n = ui.open(id, s);
    n.label = title;
    Style ic;
    ic.size(34, 34);
    ic.round(10);
    ic.bg = rgba(45, 212, 191, 0.12f);
    ic.color = col::accent;
    ic.font(15, 800);
    ic.textAlign = TextAlign::Center;
    ic.textVCenter = true;
    ic.shrink = 0;
    ui.text(icon, ic);
    Style colS;
    colS.column();
    colS.shrink = 1;
    colS.grow = 1;
    colS.minW = 0;
    ui.open(colS);
    ui.text(title, tBody(big ? 17 : 15, 650));
    if (!desc.empty()) {
        Style d = tBody(12, 400, col::muted);
        d.mar.t = 1;
        ui.text(desc, d);
    }
    ui.close();
    ui.close();
    return it.clicked && !disabled;
}

bool button(Ui& ui, const std::string& key, const std::string& label, Btn kind, bool disabled, bool focusable) {
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id, focusable ? unsigned(Ui::InteractFocusable) : 0u);
    Style s;
    s.pad = kind == Btn::Go ? Edges(12, 26) : kind == Btn::Small ? Edges(4, 10) : Edges(9, 16);
    s.round(10);
    s.font(kind == Btn::Go ? 16 : kind == Btn::Small ? 12 : 14, 650);
    s.textAlign = TextAlign::Center;
    s.nowrap = true;
    s.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
    s.self = Align::Center;
    s.shrink = 0;
    s.hit = true;
    const bool hover = it.hover && !disabled;
    switch (kind) {
        case Btn::Primary:
        case Btn::Go:
            s.bg = hover ? brighter(col::accent, 1.08f) : col::accent;
            s.outline(1, hover ? brighter(col::accent, 1.08f) : col::accent);
            s.color = col::accentInk;
            break;
        case Btn::Danger:
            s.bg = Color();
            s.outline(1, rgba(248, 113, 113, 0.5f));
            s.color = col::bad;
            if (hover) s.bg = rgba(248, 113, 113, 0.06f);
            break;
        default:
            s.bg = Color();
            s.outline(1, hover ? col::accent : col::lineStrong);
            s.color = col::text;
            break;
    }
    if (disabled) s.opacity = 0.4f;
    Node& n = ui.text(label, s, id);
    n.label = label;
    return it.clicked && !disabled;
}

bool navButton(Ui& ui, const std::string& key, const std::string& label) {
    const Id id = ui.id(key);
    Interact it = ui.interact(id);
    Style s;
    s.pad = Edges(6, 12);
    s.round(10);
    s.outline(1, it.hover ? col::accent : col::line);
    s.font(13);
    s.color = col::text;
    s.nowrap = true;
    s.cursor = Cursor::Hand;
    s.hit = true;
    Node& n = ui.text(label, s, id);
    n.label = label;
    return it.clicked;
}

std::vector<Choice> choices(const std::vector<std::string>& labels) {
    std::vector<Choice> v;
    for (size_t i = 0; i < labels.size(); ++i) v.push_back(Choice{labels[i], int(i), false});
    return v;
}

bool tabs(Ui& ui, const std::string& key, const std::vector<Choice>& choices, int& current) {
    Style box;
    box.row();
    box.gap = 4;
    box.pad = Edges(4);
    box.bg = rgba(0, 0, 0, 0.25f);
    box.round(12);
    box.outline(1, col::line);
    box.mar = Edges(0, 0, 12, 0);
    ui.pushId(key);
    ui.open(box);
    bool changed = false;
    for (const Choice& c : choices) {
        const Id id = ui.id(std::to_string(c.value));
        Interact it = c.disabled ? Interact{} : ui.interact(id);
        const bool on = c.value == current;
        Style s;
        s.flex(1, 1);
        s.minW = 0;
        s.pad = Edges(8, 6);
        s.round(9);
        s.font(12.5f, 650);
        s.nowrap = true;
        s.ellipsis = true;
        s.textAlign = TextAlign::Center;
        s.bg = on ? col::accent : Color();
        s.color = on ? col::accentInk : col::muted;
        s.cursor = c.disabled ? Cursor::Arrow : Cursor::Hand;
        s.hit = true;
        if (c.disabled) s.opacity = 0.35f;
        Node& n = ui.text(c.label, s, id);
        n.label = c.label;
        if (it.clicked && !c.disabled && !on) {
            current = c.value;
            changed = true;
        }
    }
    ui.close();
    ui.popId();
    return changed;
}

bool seg(Ui& ui, const std::string& key, const std::vector<Choice>& choices, int& current, bool fixed) {
    Style box;
    box.row();
    box.outline(1, col::lineStrong);
    box.round(10);
    box.clip = true;
    ui.pushId(key);
    ui.open(box);
    bool changed = false;
    for (size_t i = 0; i < choices.size(); ++i) {
        const Choice& c = choices[i];
        const bool disabled = fixed || c.disabled;
        const Id id = ui.id(std::to_string(c.value));
        Interact it = disabled ? Interact{} : ui.interact(id);
        const bool on = c.value == current;
        Style s;
        s.flex(1, 1, 0);
        s.minW = 0;
        s.pad = Edges(7, 8);
        s.font(13, on && !fixed ? 700 : 400);
        s.textAlign = TextAlign::Center;
        s.nowrap = true;
        s.ellipsis = true;
        if (i + 1 < choices.size()) {
            s.border = Edges(0, 1, 0, 0);
            s.borderColor = col::line;
        }
        const float r = 9;
        if (i == 0) s.radius[0] = s.radius[3] = r;
        if (i + 1 == choices.size()) s.radius[1] = s.radius[2] = r;
        if (on) {
            s.bg = fixed ? rgba(148, 163, 184, 0.18f) : rgba(45, 212, 191, 0.2f);
            s.color = fixed ? col::text : col::accent;
        } else {
            s.color = disabled ? rgba(148, 163, 184, 0.45f) : col::text;
            if (it.hover) s.bg = rgba(255, 255, 255, 0.04f);
        }
        s.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
        s.hit = true;
        Node& n = ui.text(c.label, s, id);
        n.label = c.label;
        if (it.clicked && !disabled) {
            if (current != c.value) changed = true;
            current = c.value;
        }
    }
    ui.close();
    ui.popId();
    return changed;
}

void field(Ui& ui, const std::string& label, const std::string& value, const std::function<void()>& body) {
    Style f;
    f.column();
    f.mar = Edges(10, 0, 10, 0);
    ui.open(f);
    Style row;
    row.row();
    row.justify = Justify::SpaceBetween;
    row.mar = Edges(0, 0, 6, 0);
    ui.open(row);
    Style l = tBody(12, 400, col::muted);
    l.text.letterSpacing = 0.24f;
    l.shrink = 1;
    ui.text(label, l);
    if (!value.empty()) {
        Style v = tBody(12, 400, col::text);
        v.nowrap = true;
        ui.text(value, v);
    }
    ui.close();
    if (body) body();
    ui.close();
}

namespace {
// A slider's look and pointer handling: returns the fraction set by the pointer (−1: none).
float sliderBody(Ui& ui, Id id, const Interact& it, float frac, bool disabled) {
    Style s;
    s.h = 18;
    s.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
    s.hit = true;
    s.focusRing = false;
    Node& n = ui.leaf(id, s);
    const bool focused = it.focusVisible;
    const float f = std::clamp(frac, 0.0f, 1.0f);
    n.paint = [f, disabled, focused, hover = it.hover || it.active](DrawList& dl, const Rect& b, float sc) {
        const float th = 16 * sc, tr = 2 * sc;
        const float cy = b.y + b.h * 0.5f;
        const float x0 = b.x + th * 0.5f, x1 = b.right() - th * 0.5f;
        const float tx = x0 + (x1 - x0) * f;
        const Color fillC = disabled ? rgba(148, 163, 184, 0.5f) : col::accent;
        dl.rect(Rect{x0, cy - tr, x1 - x0, 2 * tr}, rgba(255, 255, 255, disabled ? 0.12f : 0.22f), tr);
        dl.rect(Rect{x0, cy - tr, tx - x0, 2 * tr}, fillC, tr);
        if (focused) dl.rect(Rect{tx - th * 0.5f - 3 * sc, cy - th * 0.5f - 3 * sc, th + 6 * sc, th + 6 * sc}, Color(), th, 2 * sc, col::accent.withAlpha(0.6f));
        dl.rect(Rect{tx - th * 0.5f, cy - th * 0.5f, th, th}, hover && !disabled ? brighter(fillC, 1.1f) : fillC, th * 0.5f);
    };
    if (disabled) return -1;
    if (it.active || (ui.active() == id && ui.pointerDown())) {
        const Rect r = it.rect;
        if (r.w > 16) return std::clamp((ui.pointerX() - r.x - 8) / (r.w - 16), 0.0f, 1.0f);
    }
    return -1;
}
}  // namespace

bool slider(Ui& ui, const std::string& key, float min, float max, float& v, bool disabled) {
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id, Ui::InteractFocusable | Ui::InteractArrowsLR);
    const float step = (max - min) / 19.0f;
    const float frac = max > min ? (v - min) / (max - min) : 0.0f;
    float p = sliderBody(ui, id, it, frac, disabled);
    bool changed = false;
    if (p >= 0) {
        float nv = min + std::round(p * 19.0f) * step;
        if (std::fabs(nv - v) > 1e-6f) {
            v = nv;
            changed = true;
        }
    }
    if (it.focused && !disabled) {
        int d = 0;
        if (ui.keyPressed(Key::Right) || ui.keyPressed(Key::Up)) d = 1;
        if (ui.keyPressed(Key::Left) || ui.keyPressed(Key::Down)) d = -1;
        if (d) {
            const int k = int(std::round((v - min) / step)) + d;
            v = min + float(std::clamp(k, 0, 19)) * step;
            changed = true;
        }
        if (ui.keyPressed(Key::Home)) {
            v = min;
            changed = true;
        }
        if (ui.keyPressed(Key::End)) {
            v = max;
            changed = true;
        }
    }
    return changed;
}

bool mm2Slider(Ui& ui, const std::string& key, float& v, bool disabled, bool& viaKey) {
    viaKey = false;
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id, Ui::InteractFocusable | Ui::InteractArrowsLR);
    const float shown = std::max(0.0f, std::min(1.0f, v));
    const float p = sliderBody(ui, id, it, shown, disabled);
    if (p >= 0) {
        const float nv = float(p);  // single precision, like the page's Math.fround
        if (nv != v) {
            v = nv;
            return true;
        }
        return false;
    }
    if (it.focused && !disabled) {
        int d = 0;
        if (ui.keyPressed(Key::Right) || ui.keyPressed(Key::Up)) d = 1;
        if (ui.keyPressed(Key::Left) || ui.keyPressed(Key::Down)) d = -1;
        if (d) {
            const float stepF = 1.0f / 19.0f;
            float nv = shown + float(d) * stepF;
            if (nv > 1.0f) nv = 1.0f;
            if (nv < 0.0f) nv = 0.0f;
            v = nv;
            viaKey = true;
            return true;
        }
    }
    return false;
}

bool check(Ui& ui, const std::string& key, const std::string& label, bool& v, bool disabled) {
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id);
    Style row;
    row.row();
    row.gap = 10;
    row.align = Align::Center;
    row.pad = Edges(8, 0);
    row.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
    row.hit = true;
    if (disabled) row.opacity = 0.5f;
    Node& n = ui.open(id, row);
    n.label = label;
    Style box;
    box.size(18, 18);
    box.round(4);
    box.shrink = 0;
    if (v) {
        box.bg = col::accent;
        box.color = col::accentInk;
        box.font(13, 700);
        box.textAlign = TextAlign::Center;
        box.textVCenter = true;
        ui.text("✔", box);
    } else {
        box.bg = rgba(0, 0, 0, 0.25f);
        box.outline(1.5f, it.hover ? col::accent : rgba(148, 163, 184, 0.6f));
        ui.leaf(box);
    }
    ui.text(label, tBody(14));
    ui.close();
    if (it.clicked && !disabled) {
        v = !v;
        return true;
    }
    return false;
}

unsigned input(Ui& ui, const std::string& key, std::string& v, const InputOpts& o) {
    const Id id = ui.id(key);
    Interact it = ui.interact(id, Ui::InteractFocusable | Ui::InteractText);
    const unsigned flags = ui.editText(id, v, o.max, o.upper);
    Ui::TextFieldState& ts = ui.textState(id);
    Style s;
    if (o.style) {
        s = *o.style;
    } else {
        s.pad = Edges(10, 12);
        s.round(10);
        s.outline(1, it.focused ? col::accent : col::lineStrong);
        s.bg = rgba(0, 0, 0, 0.3f);
        s.font(o.fontSize, o.code ? 800 : 400);
        if (o.code) {
            s.text.letterSpacing = 0.25f * o.fontSize;
            s.textAlign = TextAlign::Center;
        }
    }
    s.color = col::text;
    s.nowrap = true;
    s.clip = true;
    s.cursor = Cursor::Text;
    s.focusRing = false;
    s.hit = true;
    if (!std::isnan(o.width)) {
        s.w = o.width;
        s.shrink = 0;
    } else {
        s.grow = o.grow;
        s.minW = 0;
    }
    // The shown text and where the caret is (characters).
    std::string shown;
    if (o.password) {
        for (size_t i = 0, n = utf8Length(v); i < n; ++i) shown += "•";
    } else {
        shown = v;
    }
    auto charIndex = [&](size_t byte) {
        size_t n = 0;
        for (size_t i = 0; i < byte && i < v.size();) {
            decodeUtf8(v, i);
            ++n;
        }
        return n;
    };
    const size_t ci = charIndex(ts.cursor), ca = charIndex(ts.anchor);
    // Keep the caret in view (last frame's widths).
    const Rect last = it.rect;
    const float contentW = last.w > 0 ? last.w - s.pad.h() - s.border.h() : 0.0f;
    float caretX = 0;
    {
        TextStyle tst = s.text;
        size_t k = 0;
        std::string pre;
        for (size_t i = 0; i < shown.size() && k < ci;) {
            const size_t a = i;
            decodeUtf8(shown, i);
            pre += shown.substr(a, i - a);
            ++k;
        }
        caretX = shown.empty() ? 0.0f : measureText(pre, tst);
    }
    if (s.textAlign != TextAlign::Center && contentW > 0) {
        if (caretX - ts.scrollX > contentW - 2) ts.scrollX = caretX - contentW + 2;
        if (caretX - ts.scrollX < 0) ts.scrollX = caretX;
        if (ts.scrollX < 0) ts.scrollX = 0;
    } else {
        ts.scrollX = 0;
    }
    s.textOffsetX = ts.scrollX;
    Node& n = ui.open(id, s);
    n.label = key;
    if (shown.empty()) {
        Span ph{o.placeholder, s.text};
        ph.color = rgbaHex(rgba(147, 161, 181, 0.62f));
        n.spans.push_back(ph);
    } else {
        n.spans.push_back(Span{shown, s.text});
    }
    const bool focused = it.focused;
    const bool empty = shown.empty();
    const double blink = ui.time() - ts.blinkStart;
    Node* np = &n;
    n.paint = [np, focused, empty, ci, ca, blink, align = s.textAlign](DrawList& dl, const Rect& b, float sc) {
        if (!focused) return;
        const Node& nn = *np;
        const Rect cb{(nn.rect.x + nn.s.border.l + nn.s.pad.l) * sc, (nn.rect.y + nn.s.border.t + nn.s.pad.t) * sc,
                      (nn.rect.w - nn.s.border.h() - nn.s.pad.h()) * sc, (nn.rect.h - nn.s.border.v() - nn.s.pad.v()) * sc};
        auto xAt = [&](size_t k) {
            if (empty || nn.layout.lines.empty()) return 0.0f;
            const TextLine& L = nn.layout.lines[0];
            float off = 0;
            if (align == TextAlign::Center) off = (cb.w / sc - L.width) * 0.5f;
            if (k == 0) return off;
            const int g = std::min(int(k) - 1, L.g1 - 1);
            if (g < L.g0) return off;
            return off + nn.layout.glyphs[size_t(g)].x + nn.layout.glyphs[size_t(g)].adv;
        };
        const float ox = cb.x - nn.s.textOffsetX * sc;
        const float lh = nn.layout.lines.empty() ? cb.h / sc : nn.layout.lines[0].height;
        const float top = cb.y + (cb.h - lh * sc) * 0.5f;
        if (ci != ca && !empty) {
            const float a = xAt(std::min(ci, ca)) * sc, z = xAt(std::max(ci, ca)) * sc;
            dl.pushClip(cb);
            dl.rect(Rect{ox + a, top, z - a, lh * sc}, rgba(45, 212, 191, 0.32f));
            dl.popClip();
        }
        if (std::fmod(blink, 1.06) < 0.6) {
            const float x = std::round(ox + xAt(ci) * sc);
            dl.rect(Rect{x, top + 2 * sc, std::max(1.0f, std::round(1.4f * sc)), lh * sc - 4 * sc}, col::text);
        }
    };
    ui.close();
    return flags;
}

bool stepper(Ui& ui, const std::string& key, int& value, int min, int max, int step, const std::function<std::string(int)>& fmt,
             bool disabled) {
    Style box;
    box.row();
    box.outline(1, col::lineStrong);
    box.round(10);
    box.clip = true;
    box.self = Align::Start;
    ui.pushId(key);
    ui.open(box);
    bool changed = false;
    auto btn = [&](const char* k, const char* label, bool off, int dir) {
        const Id id = ui.id(k);
        Interact it = off ? Interact{} : ui.interact(id);
        Style s;
        s.w = 36;
        s.pad = Edges(6, 0);
        s.font(16);
        s.textAlign = TextAlign::Center;
        s.color = off ? rgba(148, 163, 184, 0.4f) : (it.hover ? col::accent : col::text);
        if (it.hover && !off) s.bg = rgba(45, 212, 191, 0.12f);
        if (dir < 0) s.radius[0] = s.radius[3] = 9;
        else s.radius[1] = s.radius[2] = 9;
        s.cursor = off ? Cursor::Arrow : Cursor::Hand;
        s.hit = true;
        Node& n = ui.text(label, s, id);
        n.label = std::string(key) + label;
        if (it.clicked && !off) {
            value = dir < 0 ? std::max(min, value - step) : std::min(max, value + step);
            changed = true;
        }
    };
    btn("minus", "−", disabled || value <= min, -1);
    Style v;
    v.minW = 64;
    v.pad = Edges(6, 8);
    v.font(14, 650);
    v.textAlign = TextAlign::Center;
    v.border = Edges(0, 1, 0, 1);
    v.borderColor = col::line;
    v.color = col::text;
    ui.text(fmt(value), v);
    btn("plus", "+", disabled || value >= max, 1);
    ui.close();
    ui.popId();
    return changed;
}

void beginCard(Ui& ui, const std::string& key, float marginTop, float width) {
    Style s;
    s.column();
    s.bg = rgba(255, 255, 255, 0.03f);
    s.outline(1, col::line);
    s.round(12);
    s.pad = Edges(12, 14);
    s.mar.t = marginTop;
    s.w = width;
    s.hit = true;
    if (key.empty()) ui.open(s);
    else ui.open(key, s);
}
void endCard(Ui& ui) { ui.close(); }

void beginRow(Ui& ui, Justify j, float gap, float marginTop, bool wrap) {
    Style s;
    s.row();
    s.gap = gap;
    s.align = Align::Center;
    s.justify = j;
    s.wrap = wrap;
    s.mar.t = marginTop;
    ui.open(s);
}
void endRow(Ui& ui) { ui.close(); }

void beginList(Ui& ui, const std::string& key, float maxH) {
    Style s;
    s.column();
    s.gap = 4;
    s.maxH = maxH;
    s.scrollY = true;
    s.pad = Edges(0, 4, 0, 0);
    s.hit = true;
    ui.open(key, s);
}
void endList(Ui& ui) { ui.close(); }

bool listItem(Ui& ui, const std::string& key, const std::string& num, const std::string& text, bool on, bool locked,
              const std::function<void()>& extra, bool interactive, const std::string& sub) {
    const Id id = ui.id(key);
    Interact it = (interactive && !locked) ? ui.interact(id) : Interact{};
    Style s;
    s.row();
    s.gap = 10;
    s.align = Align::Center;
    s.pad = Edges(8, 10);
    s.round(9);
    s.outline(1, on ? col::accent : (it.hover ? col::lineStrong : Color()));
    s.bg = on ? rgba(45, 212, 191, 0.1f) : rgba(255, 255, 255, 0.025f);
    s.cursor = interactive && !locked ? Cursor::Hand : Cursor::Arrow;
    if (locked) s.opacity = 0.5f;
    s.hit = true;
    s.shrink = 0;
    Node& n = ui.open(id, s);
    n.label = text;
    if (!num.empty() || interactive) {
        Style ns = tBody(12, 400, col::muted);
        ns.w = 22;
        ns.shrink = 0;
        ns.nowrap = true;
        if (num.size() > 2 && num != "★") {
            ns.w = kAuto;
            ns.minW = 44;
        }
        ui.text(num, ns);
    }
    if (sub.empty()) {
        Style g = tBody(14);
        g.grow = 1;
        g.minW = 0;
        g.nowrap = true;
        g.ellipsis = true;
        ui.text(text, g);
    } else {
        Style g;
        g.column();
        g.grow = 1;
        g.minW = 0;
        ui.open(g);
        Style t = tBody(14, 700);
        t.nowrap = true;
        t.ellipsis = true;
        ui.text(text, t);
        Style ss = tBody(12, 400, col::muted);
        ss.nowrap = true;
        ss.ellipsis = true;
        ui.text(sub, ss);
        ui.close();
    }
    if (extra) extra();
    ui.close();
    return it.clicked;
}

void beginTable(Ui& ui, const std::vector<TableCol>& cols, float marginTop, float marginBottom) {
    Style s;
    std::vector<Track> tracks;
    for (const TableCol& c : cols) tracks.push_back(c.track);
    s.grid(tracks);
    s.mar = Edges(marginTop, 0, marginBottom, 0);
    ui.open(s);
    bool anyHead = false;
    for (const TableCol& c : cols) anyHead = anyHead || !c.head.empty();
    if (!anyHead) return;
    for (const TableCol& c : cols) {
        Style h = tBody(11, 600, col::muted);
        h.text.upper = true;
        h.text.letterSpacing = 0.88f;
        h.pad = Edges(6, 8);
        h.border = Edges(0, 0, 1, 0);
        h.borderColor = col::line;
        h.textAlign = c.num ? TextAlign::Right : TextAlign::Left;
        ui.text(c.head, h);
    }
}

void tableRow(Ui& ui, const std::vector<TableCol>& cols, const std::vector<std::string>& cells, bool me, const std::vector<bool>& dim,
              bool team, const std::vector<Color>* dots) {
    for (size_t i = 0; i < cols.size(); ++i) {
        const bool d = i < dim.size() && dim[i];
        Style c = tBody(13, me ? 700 : team ? 750 : 400, me ? col::accent : team ? col::warn : d ? col::muted : col::text);
        c.pad = Edges(7, 8);
        c.border = Edges(0, 0, 1, 0);
        c.borderColor = rgba(148, 163, 184, 0.08f);
        c.textAlign = cols[i].num ? TextAlign::Right : TextAlign::Left;
        const std::string& t = i < cells.size() ? cells[i] : std::string();
        if (dots && i < dots->size() && (*dots)[i].visible()) {
            // A team dot before the text.
            Style rowS = c;
            rowS.row();
            rowS.gap = 6;
            rowS.align = Align::Center;
            ui.open(rowS);
            Style dot;
            dot.size(10, 10);
            dot.round(5);
            dot.bg = (*dots)[i];
            ui.leaf(dot);
            Style tt = tBody(13, c.text.weight, c.color);
            ui.text(t, tt).label = t;
            ui.close();
        } else {
            ui.text(t, c).label = t;
        }
    }
}

void endTable(Ui& ui) { ui.close(); }

void kv(Ui& ui, const std::vector<std::pair<std::string, std::string>>& rows, float rowGap, float fontSize) {
    Style s;
    s.grid({Track::autoW(), Track::fr(1)});
    s.gap = 14;
    s.rowGap = rowGap;
    ui.open(s);
    for (const auto& r : rows) {
        ui.text(r.first, tBody(fontSize, 400, col::muted));
        ui.text(r.second, tBody(fontSize));
    }
    ui.close();
}

}  // namespace fe
}  // namespace cc
