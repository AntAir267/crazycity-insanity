// The viewer (web/app.js's side panel, now drawn by the engine): the game data card, the Cars,
// City and Network tabs (car list and paints, the city's time and weather, draw distance, the
// Drive, Sound and Race cards), the music card and the problems list; on the 3D view the position
// readout, the key hints, the frame statistics and the plain race overlay of the viewer's races.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app/fe_theme.h"
#include "app/fe_words.h"
#include "app/front_end.h"
#include "app/menu_data.h"
#include "core/json.h"
#include "core/strutil.h"

namespace cc {
namespace fe {

using namespace ui;

namespace {

JValue parse(const std::string& s) { return JValue::parse(s); }

namespace vc {
constexpr Color panel = hex(0x161a20);
constexpr Color card = hex(0x1c2129);
constexpr Color line = hex(0x2a313b);
constexpr Color text = hex(0xe6e9ee);
constexpr Color muted = hex(0x8d97a5);
constexpr Color accent = hex(0x4f9cf9);
constexpr Color accent2 = hex(0x2f6fc0);
constexpr Color warn = hex(0xe8b04a);
constexpr Color error = hex(0xef6461);
constexpr Color ok = hex(0x5fbf77);
}  // namespace vc

Style vt(float size = 14, int weight = 400, Color c = vc::text) {
    Style s;
    s.font(size, weight);
    s.color = c;
    return s;
}

Span vkbd(const std::string& t) {
    Span s;
    s.text = t;
    s.style.size = 11;
    s.style.mono = true;
    s.boxed = true;
    s.padX = 5;
    s.boxBg = rgbaHex(vc::card);
    s.boxBorder = rgbaHex(vc::line);
    s.boxRadius = 4;
    s.boxBottomW = 2;
    return s;
}

// The help lines: text with key caps written as [key].
std::vector<Span> helpSpans(const std::string& src, float size) {
    std::vector<Span> out;
    std::string cur;
    for (size_t i = 0; i < src.size(); ++i) {
        if (src[i] == '[') {
            const size_t e = src.find(']', i);
            if (e != std::string::npos) {
                if (!cur.empty()) {
                    out.push_back(span(cur, size));
                    cur.clear();
                }
                out.push_back(vkbd(src.substr(i + 1, e - i - 1)));
                i = e;
                continue;
            }
        }
        cur += src[i];
    }
    if (!cur.empty()) out.push_back(span(cur, size));
    for (Span& s : out) s.style.lineHeight = size * 1.7f;
    return out;
}

enum class VBadge { Plain, Ok, Warn, Mod };
void vbadge(Ui& ui, const std::string& t, VBadge k) {
    Style s = vt(11);
    s.pad = Edges(1, 8);
    s.round(999);
    s.nowrap = true;
    s.self = Align::Center;
    switch (k) {
        case VBadge::Ok:
            s.bg = rgba(95, 191, 119, 0.18f);
            s.color = vc::ok;
            break;
        case VBadge::Warn:
            s.bg = rgba(232, 176, 74, 0.18f);
            s.color = vc::warn;
            break;
        case VBadge::Mod:
            s.bg = rgba(79, 156, 249, 0.18f);
            s.color = vc::accent;
            break;
        default: s.bg = vc::line; break;
    }
    ui.text(t, s);
}

void beginVCard(Ui& ui, const std::string& key, const std::string& title, const std::string& badgeText, VBadge bk, bool drive = false) {
    Style c;
    c.column();
    c.bg = vc::card;
    c.outline(1, vc::line);
    c.round(8);
    c.pad = Edges(10);
    if (drive) {
        c.gap = 6;
        c.mar.t = 8;
    }
    c.hit = true;
    ui.open(key, c);
    Style head;
    head.row();
    head.align = Align::Center;
    head.justify = Justify::SpaceBetween;
    head.gap = 8;
    head.mar.b = drive ? 0 : 6;
    ui.open(head);
    Style h = vt(13, 700, vc::muted);
    h.text.upper = true;
    h.text.letterSpacing = 0.78f;
    ui.text(title, h);
    if (!badgeText.empty()) vbadge(ui, badgeText, bk);
    ui.close();
}

bool vbutton(Ui& ui, const std::string& key, const std::string& label, bool secondary, bool disabled = false, bool wide = false,
             const std::string& tip = std::string()) {
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id);
    Style s = vt(13);
    s.pad = Edges(6, 10);
    s.round(6);
    s.textAlign = TextAlign::Center;
    s.nowrap = true;
    s.hit = true;
    s.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
    if (disabled) {
        s.opacity = 0.45f;
        s.outline(1, vc::line);
    } else if (secondary) {
        s.outline(1, it.hover ? vc::accent : vc::line);
        if (it.hover) s.bg = rgba(79, 156, 249, 0.1f);
    } else {
        s.bg = it.hover ? vc::accent : vc::accent2;
        s.outline(1, vc::accent);
    }
    if (wide) {
        s.self = Align::Stretch;
        s.mar.t = 8;
    } else {
        s.self = Align::Start;
    }
    Node& n = ui.text(label, s, id);
    n.label = label;
    n.tooltip = tip;
    return it.clicked && !disabled;
}

struct VChoice {
    std::string label;
    int value;
    std::string tip;
    bool disabled = false;
};

bool vseg(Ui& ui, const std::string& key, const std::vector<VChoice>& cs, int& current) {
    Style box;
    box.row();
    box.outline(1, vc::line);
    box.round(6);
    box.self = Align::Start;
    box.clip = true;
    ui.pushId(key);
    ui.open(box);
    bool changed = false;
    for (size_t i = 0; i < cs.size(); ++i) {
        const VChoice& c = cs[i];
        const Id id = ui.id(std::to_string(c.value));
        Interact it = c.disabled ? Interact{} : ui.interact(id);
        const bool on = c.value == current;
        Style s = vt(12.5f, 400, c.disabled ? hex(0x4a525e) : vc::text);
        s.pad = Edges(4, 12);
        s.bg = on ? vc::accent2 : vc::card;
        if (i + 1 < cs.size()) {
            s.border = Edges(0, 1, 0, 0);
            s.borderColor = vc::line;
        }
        if (i == 0) s.radius[0] = s.radius[3] = 5;
        if (i + 1 == cs.size()) s.radius[1] = s.radius[2] = 5;
        s.nowrap = true;
        s.cursor = c.disabled ? Cursor::Arrow : Cursor::Hand;
        s.hit = true;
        Node& n = ui.text(c.label, s, id);
        n.label = c.label;
        n.tooltip = c.tip;
        if (it.clicked && !c.disabled) {
            changed = changed || current != c.value;
            current = c.value;
            changed = true;
        }
    }
    ui.close();
    ui.popId();
    return changed;
}

bool vcheck(Ui& ui, const std::string& key, const std::string& label, bool& v, const std::string& tip = std::string(), float size = 13) {
    const Id id = ui.id(key);
    Interact it = ui.interact(id);
    Style row;
    row.row();
    row.gap = 5;
    row.align = Align::Center;
    row.cursor = Cursor::Hand;
    row.hit = true;
    Node& n = ui.open(id, row);
    n.label = label;
    n.tooltip = tip;
    Style box;
    box.size(14, 14);
    box.round(3);
    box.shrink = 0;
    if (v) {
        box.bg = vc::accent;
        box.color = hex(0x0f1115);
        box.font(11, 700);
        box.textAlign = TextAlign::Center;
        box.textVCenter = true;
        ui.text("✔", box);
    } else {
        box.bg = vc::card;
        box.outline(1, it.hover ? vc::accent : hex(0x5a6372));
        ui.leaf(box);
    }
    ui.text(label, vt(size));
    ui.close();
    if (it.clicked) {
        v = !v;
        return true;
    }
    return false;
}

// A range input with any step; `released` when the pointer lets it go (the page's "change").
bool vslider(Ui& ui, const std::string& key, int min, int max, int step, int& v, bool disabled, bool* released = nullptr) {
    const Id id = ui.id(key);
    Interact it = disabled ? Interact{} : ui.interact(id, Ui::InteractFocusable | Ui::InteractArrowsLR);
    Style s;
    s.h = 16;
    s.grow = 1;
    s.minW = 60;
    s.hit = true;
    s.cursor = disabled ? Cursor::Arrow : Cursor::Hand;
    s.focusRing = false;
    Node& n = ui.leaf(id, s);
    const float f = max > min ? float(v - min) / float(max - min) : 0.0f;
    const bool focus = it.focusVisible;
    n.paint = [f, disabled, focus](DrawList& dl, const Rect& b, float sc) {
        const float th = 14 * sc, tr = 2 * sc, cy = b.y + b.h * 0.5f;
        const float x0 = b.x + th * 0.5f, x1 = b.right() - th * 0.5f, tx = x0 + (x1 - x0) * std::clamp(f, 0.0f, 1.0f);
        dl.rect(Rect{x0, cy - tr, x1 - x0, 2 * tr}, rgba(255, 255, 255, 0.2f), tr);
        dl.rect(Rect{x0, cy - tr, tx - x0, 2 * tr}, disabled ? rgba(141, 151, 165, 0.6f) : vc::accent, tr);
        if (focus) dl.rect(Rect{tx - th * 0.5f - 3 * sc, cy - th * 0.5f - 3 * sc, th + 6 * sc, th + 6 * sc}, Color(), th, 2 * sc, vc::accent.withAlpha(0.6f));
        dl.rect(Rect{tx - th * 0.5f, cy - th * 0.5f, th, th}, disabled ? rgba(141, 151, 165, 0.8f) : vc::accent, th * 0.5f);
    };
    bool changed = false;
    if (released) *released = false;
    if (!disabled) {
        if ((it.active || ui.active() == id) && ui.pointerDown() && it.rect.w > 14) {
            const float p = std::clamp((ui.pointerX() - it.rect.x - 7) / (it.rect.w - 14), 0.0f, 1.0f);
            int nv = min + int(std::lround(p * float(max - min) / float(step))) * step;
            nv = std::clamp(nv, min, max);
            if (nv != v) {
                v = nv;
                changed = true;
            }
        }
        if (it.clicked && released) *released = true;
        if (it.focused) {
            int d = 0;
            if (ui.keyPressed(Key::Right) || ui.keyPressed(Key::Up)) d = 1;
            if (ui.keyPressed(Key::Left) || ui.keyPressed(Key::Down)) d = -1;
            if (d) {
                v = std::clamp(v + d * step, min, max);
                changed = true;
                if (released) *released = true;
            }
        }
    }
    return changed;
}

void vlabel(Ui& ui, const std::string& t, const std::string& value = std::string(), const std::string& tip = std::string()) {
    Style r;
    r.row();
    r.gap = 4;
    r.align = Align::Center;
    Node& n = ui.open(r);
    n.tooltip = tip;
    Style l = vt(11, 400, vc::muted);
    l.text.upper = true;
    l.text.letterSpacing = 0.66f;
    ui.text(t, l);
    if (!value.empty()) ui.text(value, vt(12, 400, vc::muted));
    ui.close();
}

void vfield(Ui& ui, const std::function<void()>& body) {
    Style f;
    f.column();
    f.gap = 5;
    f.mar.t = 4;
    ui.open(f);
    body();
    ui.close();
}

}  // namespace

// ------------------------------------------------------------------------------------- the panel

void FrontEnd::buildViewer(Ui& ui) {
    const bool narrow = ui.viewW() <= 760;
    Style p;
    p.column();
    p.absolute = true;
    p.left = 0;
    if (narrow) {
        p.right = 0;
        p.bottom = 0;
        p.h = std::round(ui.viewH() * 0.45f);
        p.border = Edges(1, 0, 0, 0);
    } else {
        p.top = 0;
        p.bottom = 0;
        p.w = 340;
        p.border = Edges(0, 1, 0, 0);
    }
    p.borderColor = vc::line;
    p.bg = vc::panel;
    p.gap = 10;
    p.pad = Edges(12);
    p.scrollY = true;
    p.hit = true;
    ui.open("panel", p);
    // Brand and the Play button.
    {
        Style b;
        b.column();
        ui.open(b);
        Style h = vt(18, 700);
        h.text.letterSpacing = 0.36f;
        ui.text("CrazyCity Insanity", h);
        Style s = vt(12, 400, vc::muted);
        s.mar.t = 2;
        ui.text("Viewer for Midtown Madness 2 data and mods", s);
        const Id id = ui.id("play");
        Interact it = ready_ ? ui.interact(id) : Interact{};
        Style pb;
        pb.column();
        pb.align = Align::Center;
        pb.mar.t = 10;
        pb.pad = Edges(9, 12);
        pb.round(10);
        pb.gradient(hex(0x14b8a6), hex(0x0ea5a4), 135);
        pb.outline(1, hex(0x2dd4bf));
        pb.hit = true;
        pb.cursor = ready_ ? Cursor::Hand : Cursor::Arrow;
        if (!ready_) pb.opacity = 0.45f;
        Node& n = ui.open(id, pb);
        n.label = "PLAY";
        Style t = vt(14, 700, hex(0x03211d));
        t.text.letterSpacing = 0.56f;
        ui.text("PLAY", t);
        Style sub = vt(11, 500, hex(0x03211d));
        sub.opacity = 0.8f;
        ui.text("drivers, cruise, races, records", sub);
        ui.close();
        ui.close();
        if (it.clicked && ready_) openFrontEnd();
    }
    viewerData(ui);
    // Tabs.
    {
        Style t;
        t.row();
        t.gap = 4;
        t.border = Edges(0, 0, 1, 0);
        t.borderColor = vc::line;
        ui.open("tabs", t);
        const char* keys[3] = {"cars", "city", "net"};
        const char* labels[3] = {"Cars", "City", "Network"};
        for (int i = 0; i < 3; ++i) {
            const Id id = ui.id(std::string("tab") + keys[i]);
            Interact it = ui.interact(id);
            const bool on = v_.tab == keys[i];
            Style s = vt(14, 500, on ? vc::text : vc::muted);
            s.pad = Edges(8, 14);
            s.border = Edges(0, 0, 2, 0);
            s.borderColor = on ? vc::accent : Color();
            s.mar.b = -1;
            s.cursor = Cursor::Hand;
            s.hit = true;
            Node& n = ui.text(labels[i], s, id);
            n.label = labels[i];
            if (it.clicked && !on) {
                v_.tab = keys[i];
                // The network panel does not change the view (a network game may be running).
                if (v_.tab != "net") {
                    const bool city = v_.tab == "city";
                    v_.view = city ? 1 : 0;
                    svc_.setView(v_.view);
                    if (v_.driving) setDriving(false);
                }
            }
        }
        ui.close();
    }
    Style body;
    body.column();
    body.gap = 8;
    ui.open("tabbody", body);
    if (v_.tab == "cars") viewerCars(ui);
    else if (v_.tab == "city") viewerCity(ui);
    else viewerNet(ui);
    ui.close();
    viewerMusic(ui);
    viewerLog(ui);
    ui.close();
}

void FrontEnd::viewerData(Ui& ui) {
    std::string badge = "not loaded";
    VBadge bk = VBadge::Plain;
    int mods = 0, okBase = 0;
    for (const JValue& a : v_.summary["archives"].items()) {
        if (a["ok"].truthy() && !a["base"].truthy()) ++mods;
        if (a["ok"].truthy() && a["base"].truthy()) ++okBase;
    }
    if (!v_.summary.isNull()) {
        badge = okBase ? (mods ? std::to_string(okBase) + " base + " + std::to_string(mods) + " mod" + (mods > 1 ? "s" : "") : std::string("loaded"))
                       : std::string("no base data");
        bk = okBase ? VBadge::Ok : VBadge::Warn;
    }
    beginVCard(ui, "data", "Game data", badge, bk);
    Style st = vt(12.5f, 400, vc::muted);
    st.minH = 15;
    std::string status = v_.status;
    if (!v_.summary.isNull()) {
        status = std::to_string(v_.summary["files"].integer()) + " files · " + std::to_string(v_.summary["cars"].integer()) + " vehicles · " +
                 std::to_string(v_.summary["cities"].integer()) + " cities" + (source_.empty() ? std::string() : " (from " + source_ + ")");
        if (v_.progress >= 0) status = v_.status;
    }
    ui.text(status, st);
    if (v_.progress >= 0) {
        Style pb;
        pb.h = 4;
        pb.round(2);
        pb.bg = vc::line;
        pb.mar.t = 8;
        Node& n = ui.leaf(pb);
        const float f = v_.progress;
        n.paint = [f](DrawList& dl, const Rect& r, float sc) { dl.rect(Rect{r.x, r.y, r.w * std::clamp(f, 0.0f, 1.0f), r.h}, vc::accent, 2 * sc); };
    }
    Style row;
    row.row();
    row.gap = 6;
    row.mar.t = 8;
    row.wrap = true;
    ui.open(row);
    if (vbutton(ui, "pickfolder", "Open my MM2 folder", false, !host_.canPick(Pick::GameFolder), false, "Choose the folder MM2 is installed in"))
        host_.pick(Pick::GameFolder);
    if (vbutton(ui, "pickmods", "Add mod .ar", true, !host_.canPick(Pick::ModArchives), false, "Add mod archives (.ar) on top of the game data"))
        host_.pick(Pick::ModArchives);
    ui.close();
    Style hint = vt(11.5f, 400, vc::muted);
    hint.mar.t = 6;
    ui.text(host_.isBrowser() ? "Files are read in your browser and never uploaded. You can also drop .ar files on the view."
                              : "Files are read from your disk. You can also drop .ar files on the window.",
            hint);
    {
        const Id id = ui.id("archives");
        Interact it = ui.interact(id);
        Style s = vt(12, 400, vc::muted);
        s.mar.t = 8;
        s.cursor = Cursor::Hand;
        s.hit = true;
        const size_t n = v_.summary["archives"].size();
        Node& nn = ui.text(std::string(v_.archivesOpen ? "▼ " : "▶ ") + "Archives" + (n ? " (" + std::to_string(n) + ")" : std::string()), s, id);
        nn.label = "Archives";
        if (it.clicked) v_.archivesOpen = !v_.archivesOpen;
        if (v_.archivesOpen) {
            Style l;
            l.column();
            l.mar.t = 6;
            ui.open(l);
            for (const JValue& a : v_.summary["archives"].items()) {
                Style r;
                r.row();
                r.justify = Justify::SpaceBetween;
                r.pad = Edges(2, 0);
                r.gap = 8;
                ui.open(r);
                Style an = vt(12, 500);
                an.shrink = 1;
                an.minW = 0;
                an.ellipsis = true;
                an.nowrap = true;
                ui.text(a["name"].str(), an);
                const std::string ln = toLower(a["name"].str());
                const bool audio = ln == "mm2aud.ar" || ln == "mm2audex.ar";
                Style as = vt(12, 400, vc::muted);
                as.nowrap = true;
                ui.text(a["ok"].truthy() ? std::to_string(a["files"].integer()) + " files" + (a["base"].truthy() ? "" : " · mod") +
                                               (audio ? " · read as needed" : "")
                                         : std::string("not readable"),
                        as);
                ui.close();
            }
            ui.close();
        }
    }
    ui.close();
}

void FrontEnd::viewerSelectCar(int index) {
    const JValue info = parse(svc_.selectCar(index));
    if (!info["loaded"].truthy()) {
        v_.carInfo = JValue();
        carLoadFailed_ = true;
        return;
    }
    carLoadFailed_ = false;
    v_.selectedCar = index;
    v_.selectedName.clear();
    for (const JValue& c : v_.carList.items())
        if (c["index"].integer() == index) v_.selectedName = c["name"].str();
    v_.carInfo = info;
    v_.paint = 0;
    svc_.setPaint(0);
    const JValue& lods = info["lods"];
    if (!lods[v_.lod].truthy()) {
        v_.lod = 0;
        for (size_t i = 0; i < lods.size(); ++i)
            if (lods[i].truthy()) {
                v_.lod = int(i);
                break;
            }
    }
    svc_.setLod(v_.lod);
}

void FrontEnd::viewerCars(Ui& ui) {
    InputOpts o;
    o.placeholder = "Filter cars…";
    Style fs = vt(14);
    fs.bg = vc::card;
    fs.outline(1, ui.isFocused(ui.id("carfilter")) ? vc::accent : vc::line);
    fs.round(6);
    fs.pad = Edges(6, 9);
    fs.self = Align::Stretch;
    o.style = &fs;
    input(ui, "carfilter", v_.filter, o);
    // The list.
    Style l;
    l.column();
    l.bg = vc::card;
    l.outline(1, vc::line);
    l.round(8);
    l.maxH = ui.viewH() * 0.34f;
    l.scrollY = true;
    l.pad = Edges(4, 0);
    l.hit = true;
    ui.open("carlist", l);
    static const char* groups[3] = {"Cars", "Other vehicle packages", "Traffic"};
    const std::string q = toLower(trim(v_.filter));
    int shown = 0;
    for (int g = 0; g < 3; ++g) {
        std::vector<const JValue*> items;
        for (const JValue& c : v_.carList.items())
            if (c["group"].integer() == g &&
                (q.empty() || toLower(c["name"].str()).find(q) != std::string::npos || toLower(c["description"].str()).find(q) != std::string::npos))
                items.push_back(&c);
        if (items.empty()) continue;
        Style gh = vt(11, 400, vc::muted);
        gh.text.upper = true;
        gh.text.letterSpacing = 0.66f;
        gh.pad = Edges(8, 10, 3, 10);
        ui.text(std::string(groups[g]) + " (" + std::to_string(items.size()) + ")", gh);
        for (const JValue* c : items) {
            const int index = (*c)["index"].integer();
            const Id id = ui.id("car" + std::to_string(index));
            Interact it = ui.interact(id, Ui::InteractFocusable | Ui::InteractArrowsUD);
            const bool sel = index == v_.selectedCar;
            Style r;
            r.row();
            r.align = Align::Baseline;
            r.justify = Justify::SpaceBetween;
            r.gap = 8;
            r.pad = Edges(4, 10);
            r.bg = sel ? rgba(79, 156, 249, 0.18f) : (it.hover ? rgba(255, 255, 255, 0.04f) : Color());
            r.cursor = Cursor::Hand;
            r.hit = true;
            Node& n = ui.open(id, r);
            n.label = (*c)["description"].str();
            Style nm;
            nm.row();
            nm.gap = 4;
            nm.align = Align::Center;
            nm.shrink = 1;
            nm.minW = 0;
            ui.open(nm);
            Style t = vt(14);
            t.nowrap = true;
            t.ellipsis = true;
            t.shrink = 1;
            t.minW = 0;
            ui.text((*c)["description"].str(), t);
            if ((*c)["mod"].truthy()) {
                Style mb = vt(11, 400, vc::accent);
                mb.bg = rgba(79, 156, 249, 0.18f);
                mb.pad = Edges(1, 8);
                mb.round(999);
                mb.nowrap = true;
                Node& m = ui.text("mod", mb);
                m.tooltip = "from " + (*c)["archive"].str();
            }
            ui.close();
            Style code = vt(11, 400, vc::muted);
            code.text.mono = true;
            code.nowrap = true;
            ui.text((*c)["name"].str(), code);
            ui.close();
            if (it.clicked) viewerSelectCar(index);
            if (it.focused && (ui.keyPressed(Key::Down) || ui.keyPressed(Key::Up))) {
                // Arrow keys move the selection through the shown cars.
                std::vector<int> order;
                for (int gg = 0; gg < 3; ++gg)
                    for (const JValue& cc : v_.carList.items())
                        if (cc["group"].integer() == gg && (q.empty() || toLower(cc["name"].str()).find(q) != std::string::npos ||
                                                            toLower(cc["description"].str()).find(q) != std::string::npos))
                            order.push_back(cc["index"].integer());
                auto pos = std::find(order.begin(), order.end(), v_.selectedCar);
                int p2 = pos == order.end() ? 0 : int(pos - order.begin()) + (ui.keyPressed(Key::Down) ? 1 : -1);
                p2 = std::max(0, std::min(int(order.size()) - 1, p2));
                if (!order.empty()) {
                    viewerSelectCar(order[size_t(p2)]);
                    ui.setFocus(ui.id("car" + std::to_string(order[size_t(p2)])), true);
                    ui.scrollIntoView(ui.id("car" + std::to_string(order[size_t(p2)])));
                }
            }
            ++shown;
        }
    }
    if (!shown) {
        Style e = vt(12.5f, 400, vc::muted);
        e.pad = Edges(8, 10);
        ui.text(v_.carList.size() ? "No car matches the filter." : "Load game data to see the cars.", e);
    }
    ui.close();
    if (carLoadFailed_) {
        ui.text("This vehicle could not be loaded; see Problems.", vt(12, 400, vc::muted));
        return;
    }
    if (v_.carInfo.isNull()) return;
    const JValue& info = v_.carInfo;
    vfield(ui, [&]() {
        vlabel(ui, "Paint");
        Style ch;
        ch.row();
        ch.wrap = true;
        ch.gap = 5;
        ui.open("paints", ch);
        const JValue& paints = info["paints"];
        for (size_t i = 0; i < paints.size(); ++i) {
            const JValue& p = paints[i];
            const Id id = ui.id("vp" + std::to_string(i));
            Interact it = ui.interact(id);
            const bool on = int(i) == v_.paint;
            Style s = vt(12.5f, 400, p["named"].truthy() ? vc::text : vc::muted);
            s.pad = Edges(3, 10);
            s.round(999);
            s.bg = on ? rgba(79, 156, 249, 0.18f) : vc::card;
            s.outline(1, on || it.hover ? vc::accent : vc::line);
            s.nowrap = true;
            s.cursor = Cursor::Hand;
            s.hit = true;
            Node& n = ui.text(p["label"].str(), s, id);
            n.label = p["label"].str();
            n.tooltip = "material set " + std::to_string(p["set"].integer()) + (p["named"].truthy() ? "" : " (not in the Colors= list)");
            if (it.clicked) {
                v_.paint = int(i);
                svc_.setPaint(v_.paint);
            }
        }
        ui.close();
    });
    vfield(ui, [&]() {
        vlabel(ui, "Level of detail");
        std::vector<VChoice> l;
        const char* names[4] = {"H", "M", "L", "VL"};
        for (int i = 0; i < 4; ++i) l.push_back(VChoice{names[i], i, std::string(), !info["lods"][i].truthy()});
        if (vseg(ui, "lod", l, v_.lod)) svc_.setLod(v_.lod);
    });
    {
        Style c;
        c.row();
        c.wrap = true;
        c.gap = 14;
        c.rowGap = 4;
        c.mar.t = 4;
        ui.open(c);
        if (vcheck(ui, "autorotate", "Turntable spin", v_.autorotate)) svc_.setCarOption("autorotate", v_.autorotate);
        if (vcheck(ui, "shadow", "Shadow", v_.shadow)) svc_.setCarOption("shadow", v_.shadow);
        if (vcheck(ui, "reflections", "Reflections", v_.reflections)) svc_.setCarOption("reflections", v_.reflections);
        if (vcheck(ui, "lights", "Lights on", v_.lights)) svc_.setCarOption("lights", v_.lights);
        if (vcheck(ui, "trailer", "Trailer", v_.trailer)) svc_.setCarOption("trailer", v_.trailer);
        ui.close();
    }
    // Details.
    {
        Style d = vt(12, 400, vc::muted);
        d.text.lineHeight = 18;
        d.mar.t = 4;
        auto b = [](const std::string& t) { return span(t, 12, 500, rgbaHex(vc::text)); };
        auto m = [](const std::string& t) { return span(t, 12); };
        auto code = [](const std::string& t) {
            Span s = span(t, 11);
            s.style.mono = true;
            return s;
        };
        char dims[160];
        std::snprintf(dims, sizeof dims, "%.2f m long · %.2f m wide · %.2f m high\n", info["length"].num(), info["width"].num(), info["height"].num());
        std::vector<Span> sp = {b(info["description"].str()), m(" · "), code(info["baseName"].str()), m("\n"), m(dims)};
        const int sets = info["sets"].integer();
        sp.push_back(m(std::to_string(sets) + " paint set" + (sets == 1 ? "" : "s") + " · " + std::to_string(info["partCount"].integer()) + " parts"));
        if (info["trailer"].truthy()) {
            sp.push_back(m(" · trailer "));
            sp.push_back(code(info["trailer"].str()));
        }
        sp.push_back(m("\nfrom "));
        sp.push_back(code(info["archive"].str()));
        if (info["fromMod"].truthy()) sp.push_back(span(" mod", 11, 400, rgbaHex(vc::accent)));
        sp.push_back(m("\n" + info["parts"].str()));
        for (auto& s : sp) s.style.lineHeight = 18;
        ui.rich(sp, d);
        for (const JValue& p : info["problems"].items()) ui.text(p.str(), vt(12, 400, vc::warn));
    }
}

void FrontEnd::viewerLoadCity(int i) {
    const CityInfo* c = nullptr;
    for (const CityInfo& ci : viewerCitiesAll_)
        if (ci.index == i) c = &ci;
    v_.cityStatus = "Loading " + (c ? c->name : std::string()) + "…";
    pendingCityLoad_ = i;
    pendingCityFrames_ = 0;
}

void FrontEnd::afterCityLoaded(int i, const JValue& info, const std::string& secs) {
    v_.cityLoaded = info["loaded"].truthy();
    v_.loadedCity = v_.cityLoaded ? i : -1;
    const CityInfo* c = nullptr;
    for (const CityInfo& ci : viewerCitiesAll_)
        if (ci.index == i) c = &ci;
    v_.cityStatus = v_.cityLoaded ? (info["summary"].truthy() ? info["summary"].str() : (c ? c->name : std::string()) + " loaded") +
                                        (secs.empty() ? std::string() : " · " + secs + " s")
                                  : info["message"].str("The city loader is coming: cities are listed but not drawn yet.");
    refreshRaces();
}

void FrontEnd::setDriving(bool on) {
    v_.driving = on;
    if (!on) hud_ = JValue();
}

void FrontEnd::startDrive(bool flat) {
    const JValue r = parse(svc_.startDrive(v_.driveStart, flat));
    if (!r["ok"].truthy()) {
        v_.cityStatus = "Cannot drive: " + (r.isNull() ? std::string("engine error") : r["why"].str());
        return;
    }
    v_.cityStatus = "Driving from " + r["where"].str() + (r["note"].truthy() ? " (" + r["note"].str() + ")" : std::string());
    setDriving(true);
    sceneFocus_ = true;
}

// Traffic density (traffic-population.md): 0–100 % of MM2's slider, or off; remembered per user.
void FrontEnd::applyTraffic(bool save) {
    svc_.setDriveOption("traffic", v_.trafficOff ? 0 : v_.traffic);
    if (save) host_.storageSet("cci.traffic", v_.trafficOff ? std::string("off") : std::to_string(v_.traffic));
}
// Pedestrian density: "off" is the "pedestrians off" display option, the slider Cruise's density.
void FrontEnd::applyPeds(bool save) {
    svc_.setDriveOption("pedsoff", v_.pedsOff ? 1 : 0);
    svc_.setDriveOption("peds", v_.peds);
    if (save) host_.storageSet("cci.peds", v_.pedsOff ? std::string("off") : std::to_string(v_.peds));
}
// Cops on/off (police.md: cop density 1 or 0).
void FrontEnd::applyCops(bool save) {
    svc_.setDriveOption("cops", v_.cops ? 1 : 0);
    if (save) host_.storageSet("cci.cops", v_.cops ? "1" : "0");
}
void FrontEnd::applySound(bool save) {
    svc_.setSoundOption("effects", v_.sndEffects ? 1 : 0);
    svc_.setSoundOption("commentary", v_.sndCommentary ? 1 : 0);
    svc_.setSoundOption("volume", v_.sndVolume);
    svc_.setSoundOption("balance", v_.sndBalance);
    if (save) {
        JsonWriter j;
        j.beginObject();
        j.field("effects", v_.sndEffects);
        j.field("commentary", v_.sndCommentary);
        j.field("volume", v_.sndVolume);
        j.field("balance", v_.sndBalance);
        j.endObject();
        host_.storageSet("cci.sound", j.str());
    }
}
void FrontEnd::applyMusicVolume(bool save) {
    svc_.setMusicVolume(float(v_.musicVolume) / 100.0f);
    if (save) host_.storageSet("cci.musicVolume", std::to_string(v_.musicVolume));
}
void FrontEnd::setMusicMode(int m, bool save) {
    v_.musicMode = m;
    svc_.setMusicMode(m);
    if (save) host_.storageSet("cci.music", std::to_string(m));
}

// The viewer's remembered settings at start (web/app.js onEngineReady).
void FrontEnd::restoreViewerState() {
    if (!viewerRestored_) {
        viewerRestored_ = true;
        const std::string far = host_.storageGet("cci.drawDistance");
        if (!far.empty()) v_.far = std::atoi(far.c_str());
        const std::string walls = host_.storageGet("cci.wallShading");
        if (!walls.empty()) v_.walls = std::atoi(walls.c_str());
        v_.mirror = host_.storageGet("cci.mirror") == "1";
        const std::string peds = host_.storageGet("cci.peds");
        if (peds == "off") v_.pedsOff = true;
        else if (!peds.empty()) v_.peds = std::atoi(peds.c_str());
        v_.cops = host_.storageGet("cci.cops") != "0";
        const std::string traffic = host_.storageGet("cci.traffic");
        if (traffic == "off") v_.trafficOff = true;
        else if (!traffic.empty()) v_.traffic = std::atoi(traffic.c_str());
        const JValue snd = parse(host_.storageGet("cci.sound"));
        if (snd.isObject()) {
            if (snd["effects"].isBool()) v_.sndEffects = snd["effects"].boolean();
            if (snd["commentary"].isBool()) v_.sndCommentary = snd["commentary"].boolean();
            if (snd["volume"].isNumber()) v_.sndVolume = snd["volume"].integer();
            if (snd["balance"].isNumber()) v_.sndBalance = snd["balance"].integer();
        }
        const std::string mv = host_.storageGet("cci.musicVolume");
        if (!mv.empty()) v_.musicVolume = std::atoi(mv.c_str());
        const std::string mm = host_.storageGet("cci.music");
        v_.musicMode = mm.empty() ? 2 : std::atoi(mm.c_str());
        mp_.viewerName = host_.storageGet("cci.mp.name");
        if (mp_.viewerName.empty()) mp_.viewerName = "Player" + std::to_string(10 + std::rand() % 90);
        svc_.setCityOption("far", v_.far);
        svc_.setCityOption("walls", v_.walls);
        svc_.setDriveOption("mirror", v_.mirror ? 1 : 0);
        applyPeds(false);
        applyCops(false);
        applyTraffic(false);
        applySound(false);
        applyMusicVolume(false);
        setMusicMode(v_.musicMode, false);
        return;
    }
    // Back from the front end: the viewer's tab decides the view again.
    if (v_.tab != "net") {
        v_.view = v_.tab == "city" ? 1 : 0;
        svc_.setView(v_.view);
        if (v_.driving) setDriving(false);
    }
}

// The viewer's own draw settings, put back when the front end closes.
void FrontEnd::restoreViewerOptions() {
    svc_.setCityOption("far", v_.far);
    svc_.setCityOption("detail", v_.detail);
    svc_.setCityOption("props", v_.props);
    svc_.setCityOption("reflections", v_.carRefl);
    svc_.setCityOption("fog", v_.fog);
    svc_.setCityOption("sky", v_.sky);
    svc_.setCityOption("aiOverlay", v_.aiOverlay);
    svc_.setDriveOption("traffic", v_.trafficOff ? 0 : v_.traffic);
    svc_.setDriveOption("cops", v_.cops ? 1 : 0);
}

// The viewer's own sound and music settings back when the front end closes.
void FrontEnd::restoreViewerAudio() {
    svc_.setSoundOption("effects", v_.sndEffects ? 1 : 0);
    svc_.setSoundOption("commentary", v_.sndCommentary ? 1 : 0);
    svc_.setSoundOption("volume", v_.sndVolume);
    svc_.setSoundOption("balance", v_.sndBalance);
    svc_.setMusicMode(v_.musicMode);
    svc_.setMusicVolume(float(v_.musicVolume) / 100.0f);
}

void FrontEnd::refreshRaces() {
    v_.raceList = v_.cityLoaded ? parse(svc_.raceList()) : JValue();
    v_.raceMode = v_.raceList["modes"][0]["mode"].integer();
    v_.raceNumber = 0;
}

void FrontEnd::startViewerRace() {
    const JValue r = parse(svc_.startRace(v_.raceMode, v_.raceNumber, v_.racePro, v_.raceOpp == 0 ? -1 : v_.raceOpp));
    if (!r["ok"].truthy()) {
        v_.cityStatus = "Cannot start the race: " + (r.isNull() ? std::string("engine error") : r["why"].str());
        return;
    }
    v_.cityStatus = "Racing: " + r["title"].str() + " · " + std::to_string(r["opponents"].integer()) + " opponents";
    // The race's time of day and weather (races.md): shown on the city buttons.
    v_.tod = r["timeOfDay"].integer();
    v_.weather = r["weather"].integer();
    v_.racing = true;
    setDriving(true);
    sceneFocus_ = true;
}

void FrontEnd::viewerCity(Ui& ui) {
    // The city list (MM2's cities by their LocalizedName, then unused layouts marked as tests).
    Style l;
    l.column();
    l.bg = vc::card;
    l.outline(1, vc::line);
    l.round(8);
    l.maxH = ui.viewH() * 0.34f;
    l.scrollY = true;
    l.pad = Edges(4, 0);
    l.hit = true;
    ui.open("citylist", l);
    if (viewerCitiesAll_.empty()) {
        Style e = vt(12.5f, 400, vc::muted);
        e.pad = Edges(8, 10);
        ui.text(v_.summary.isNull() ? "Load game data to see the cities." : "No cities (tune/*.cinfo or city/*.psdl) in the loaded data.", e);
    }
    for (const CityInfo& c : viewerCitiesAll_) {
        const Id id = ui.id("city" + std::to_string(c.index));
        Interact it = ui.interact(id);
        const bool sel = c.index == v_.loadedCity || c.index == pendingCityLoad_;
        Style r;
        r.row();
        r.align = Align::Baseline;
        r.justify = Justify::SpaceBetween;
        r.gap = 8;
        r.pad = Edges(4, 10);
        r.bg = sel ? rgba(79, 156, 249, 0.18f) : (it.hover ? rgba(255, 255, 255, 0.04f) : Color());
        r.cursor = Cursor::Hand;
        r.hit = true;
        Node& n = ui.open(id, r);
        n.label = c.name;
        Style t = vt(14, 400, c.test ? vc::muted : vc::text);
        t.nowrap = true;
        t.ellipsis = true;
        t.shrink = 1;
        t.minW = 0;
        ui.text(c.name, t);
        Style code = vt(11, 400, vc::muted);
        code.text.mono = true;
        code.nowrap = true;
        ui.text(c.test ? c.map + " · test layout" : c.map, code);
        ui.close();
        if (it.clicked) viewerLoadCity(c.index);
    }
    ui.close();
    ui.text(v_.cityStatus, vt(12.5f, 400, vc::muted));
    if (v_.cityLoaded) {
        vfield(ui, [&]() {
            vlabel(ui, "Time of day");
            if (vseg(ui, "tod", {{"Morning", 0, ""}, {"Noon", 1, ""}, {"Evening", 2, ""}, {"Night", 3, ""}}, v_.tod)) svc_.setCityOption("tod", v_.tod);
        });
        vfield(ui, [&]() {
            vlabel(ui, "Weather");
            if (vseg(ui, "weather", {{"Clear", 0, ""}, {"Cloudy", 1, ""}, {"Fog", 2, ""}, {"Rain", 3, ""}}, v_.weather))
                svc_.setCityOption("weather", v_.weather);
        });
        vfield(ui, [&]() {
            vlabel(ui, "Wall shading");
            if (vseg(ui, "walls",
                     {{"Soft", 0, "Time-of-day shading squeezed into the range of MM2's default lighting (never pure white or very dark)"},
                      {"MM2 exact", 1, "MM2's wall light table exactly"},
                      {"Flat", 2, "Walls unshaded"}},
                     v_.walls)) {
                svc_.setCityOption("walls", v_.walls);
                host_.storageSet("cci.wallShading", std::to_string(v_.walls));
            }
        });
        vfield(ui, [&]() {
            vlabel(ui, "Object detail");
            if (vseg(ui, "detail", {{"Low", 0, ""}, {"1", 1, ""}, {"2", 2, ""}, {"High", 3, "what MM2 picks on modern machines"}}, v_.detail))
                svc_.setCityOption("detail", v_.detail);
        });
        vfield(ui, [&]() {
            vlabel(ui, "Draw distance (fly camera and driving)");
            if (vseg(ui, "far",
                     {{"MM2", 0, "As MM2 in a game: the profile's view distance (600 m) as the far plane, the city fog clamped to it"},
                      {"1 km", 1000, ""},
                      {"2 km", 2000, ""},
                      {"4 km", 4000, ""},
                      {"Full map", -1, "The whole city from anywhere: no fog, nothing dropped for distance"}},
                     v_.far)) {
                svc_.setCityOption("far", v_.far);
                host_.storageSet("cci.drawDistance", std::to_string(v_.far));
            }
        });
        Style c;
        c.row();
        c.wrap = true;
        c.gap = 14;
        c.rowGap = 4;
        c.mar.t = 4;
        ui.open(c);
        if (vcheck(ui, "props", "Objects", v_.props)) svc_.setCityOption("props", v_.props);
        if (vcheck(ui, "crefl", "Car reflections", v_.carRefl)) svc_.setCityOption("reflections", v_.carRefl);
        if (vcheck(ui, "fog", "Fog", v_.fog)) svc_.setCityOption("fog", v_.fog);
        if (vcheck(ui, "sky", "Sky", v_.sky)) svc_.setCityOption("sky", v_.sky);
        if (vcheck(ui, "ai", "AI road network", v_.aiOverlay,
                   "Debug view of the traffic network: lanes (blue: road direction, orange: against it, grey: no traffic, magenta: shortcuts), "
                   "turn paths (yellow-green), intersections, stop signs (red) and the traffic-light states"))
            svc_.setCityOption("aiOverlay", v_.aiOverlay);
        ui.close();
    }
    if (vbutton(ui, "dropcar", "Drop the selected car below the camera", true, !v_.cityLoaded || v_.selectedCar < 0, true)) {
        const JValue r = parse(svc_.dropCar());
        if (!r["ok"].truthy()) v_.cityStatus = "Cannot drop the car: " + r["why"].str();
        sceneFocus_ = true;
    }
    // Drive card.
    std::string carName = "pick a car";
    for (const JValue& c : v_.carList.items())
        if (c["index"].integer() == v_.selectedCar) carName = c["description"].str();
    beginVCard(ui, "drive", "Drive", v_.selectedCar >= 0 ? carName : std::string("no car"), VBadge::Plain, true);
    vfield(ui, [&]() {
        vlabel(ui, "Start");
        vselect(ui, "start", {"Random intersection (like MM2)", "Repeatable start (first eligible intersection)", "Under the fly camera"},
                v_.driveStart);
    });
    vfield(ui, [&]() {
        vlabel(ui, "Gearbox");
        int a = v_.automatic ? 1 : 0;
        if (vseg(ui, "gearbox", {{"Automatic", 1, ""}, {"Manual", 0, ""}}, a)) {
            v_.automatic = a != 0;
            svc_.setDriveOption("automatic", a);
            if (v_.driving) sceneFocus_ = true;
        }
    });
    {
        Style c;
        c.row();
        c.wrap = true;
        c.gap = 14;
        c.rowGap = 4;
        ui.open(c);
        if (vcheck(ui, "autorev", "Automatic reverse", v_.autoReverse)) svc_.setDriveOption("autoreverse", v_.autoReverse);
        if (vcheck(ui, "mirror", "Rear-view mirror (Backspace)", v_.mirror)) {
            svc_.setDriveOption("mirror", v_.mirror);
            host_.storageSet("cci.mirror", v_.mirror ? "1" : "0");
            if (v_.driving) sceneFocus_ = true;
        }
        ui.close();
    }
    vfield(ui, [&]() {
        vlabel(ui, "Traffic", v_.trafficOff ? std::string("off") : std::to_string(v_.traffic) + "%");
        Style r;
        r.row();
        r.gap = 6;
        r.align = Align::Center;
        ui.open(r);
        bool rel = false;
        vslider(ui, "traffic", 0, 100, 5, v_.traffic, v_.trafficOff, &rel);
        if (rel) applyTraffic(true);
        if (vcheck(ui, "trafficoff", "Off", v_.trafficOff, "", 12)) applyTraffic(true);
        ui.close();
        if (vcheck(ui, "cops", "Cops", v_.cops,
                   "The parked police cars of the city (MM2's Cruise cops): they chase you when you pass within 75 m in front of them"))
            applyCops(true);
    });
    vfield(ui, [&]() {
        vlabel(ui, "Pedestrians", v_.pedsOff ? std::string("off") : std::to_string(v_.peds) + "%",
               "MM2's Cruise pedestrian slider: the share of the city's pool of 100 pedestrians (25 % = 25 people around you)");
        Style r;
        r.row();
        r.gap = 6;
        r.align = Align::Center;
        ui.open(r);
        bool rel = false;
        vslider(ui, "peds", 0, 100, 5, v_.peds, v_.pedsOff, &rel);
        if (rel) applyPeds(true);
        if (vcheck(ui, "pedsoff", "Off", v_.pedsOff, "", 12)) applyPeds(true);
        ui.close();
    });
    {
        Style r;
        r.row();
        r.gap = 6;
        r.mar.t = 8;
        r.wrap = true;
        ui.open(r);
        if (!v_.driving) {
            if (vbutton(ui, "drivecity", "Drive in this city", false, !v_.cityLoaded || v_.selectedCar < 0)) startDrive(false);
            if (vbutton(ui, "driveflat", "Drive on flat ground", true, v_.selectedCar < 0)) startDrive(true);
        }
        ui.close();
        if (v_.driving && vbutton(ui, "drivestop", "Stop driving (Esc)", false, false, true)) {
            svc_.stopDrive();
            setDriving(false);
            v_.racing = false;
            v_.raceState = JValue();
        }
    }
    {
        Style h = vt(12, 400, vc::muted);
        ui.rich(helpSpans("[↑]/[W] accelerate · [↓]/[S] brake, reverse · [←][→]/[A][D] steer · [Space] handbrake · [C] camera · [F] dash · "
                          "[V] orbit · [B] wide · [T] automatic/manual · [E]/[Q] shift up/down · [R] reverse · [Enter] horn (siren on police "
                          "cars) · [H] gauges · [Backspace] mirror · [G] reset car · [Tab] map · [M] full-screen map · [K] map zoom · [O] map "
                          "north/heading up · [Esc] stop. A gamepad works too.",
                          12),
                h);
    }
    ui.close();
    // Sound card.
    {
        const JValue& s = v_.soundState;
        bool haveAudio = false;
        for (const JValue& a : v_.summary["archives"].items()) {
            const std::string n = toLower(a["name"].str());
            if (n == "mm2aud.ar" || n == "mm2audex.ar") haveAudio = true;
        }
        std::string badge = "no sound data";
        VBadge bk = VBadge::Plain;
        if (haveAudio && !s["ready"].truthy()) badge = "loading…";
        else if (haveAudio && !svc_.audioRunning()) {
            badge = "click to start";
            bk = VBadge::Warn;
        } else if (haveAudio && !v_.sndEffects) badge = "muted";
        else if (haveAudio) {
            badge = "on";
            bk = VBadge::Ok;
        }
        beginVCard(ui, "sound", "Sound", badge, bk);
        Style c;
        c.row();
        c.wrap = true;
        c.gap = 14;
        c.rowGap = 4;
        c.mar.t = 4;
        ui.open(c);
        if (vcheck(ui, "effects", "Sound effects", v_.sndEffects, "All sound effects and speech (MM2's sound effects option)")) {
            applySound(true);
            if (v_.driving) sceneFocus_ = true;
        }
        if (vcheck(ui, "commentary", "Commentary", v_.sndCommentary, "The race announcer (from the next drive; San Francisco and London only)")) {
            applySound(true);
            if (v_.driving) sceneFocus_ = true;
        }
        ui.close();
        vfield(ui, [&]() {
            vlabel(ui, "Effects volume", std::to_string(v_.sndVolume) + "%");
            bool rel = false;
            if (vslider(ui, "volume", 0, 100, 1, v_.sndVolume, false, &rel)) applySound(false);
            if (rel) applySound(true);
        });
        vfield(ui, [&]() {
            const int b = v_.sndBalance;
            vlabel(ui, "Balance", b == 0 ? std::string("centre") : b < 0 ? std::to_string(-b) + "% left" : std::to_string(b) + "% right");
            bool rel = false;
            if (vslider(ui, "balance", -100, 100, 5, v_.sndBalance, false, &rel)) applySound(false);
            if (rel) applySound(true);
        });
        std::string stLine;
        if (haveAudio && s["ready"].truthy()) {
            const int playing = s["playing"].integer(), sounding = s["sounding"].integer();
            stLine = std::to_string(playing) + " sample" + (playing == 1 ? "" : "s") + " playing · " + std::to_string(sounding) + " voice" +
                     (sounding == 1 ? "" : "s");
            if (s["pending"].integer()) stLine += " · " + std::to_string(s["pending"].integer()) + " loading";
            const JValue& g = s["game"];
            if (!g.isNull()) {
                std::string held;
                for (const JValue& sl : g["slots"].items())
                    if (sl.truthy()) held += (held.empty() ? "" : ", ") + sl.str();
                if (!held.empty()) stLine += " · slots: " + held;
                if (g["speech"].truthy()) stLine += " · announcer " + g["speech"].str();
                if (g["echo"].truthy()) stLine += " · tunnel echo";
                if (g["chaseCount"].integer()) stLine += " · chase " + std::to_string(g["chaseCount"].integer());
            }
        }
        ui.text(stLine, vt(12, 400, vc::muted));
        ui.close();
    }
    // Race card.
    {
        const JValue& rl = v_.raceList;
        const bool okRaces = rl["ok"].truthy() && rl["modes"].size() > 0;
        beginVCard(ui, "race", "Race", okRaces ? rl["city"].str() : (v_.cityLoaded ? std::string("no races") : std::string("load a city")),
                   VBadge::Plain, true);
        std::vector<std::string> modeNames;
        std::vector<int> modeValues;
        for (const JValue& m : rl["modes"].items()) {
            modeNames.push_back(m["name"].str() + " (" + std::to_string(m["races"].size()) + ")");
            modeValues.push_back(m["mode"].integer());
        }
        int mi = 0;
        for (size_t i = 0; i < modeValues.size(); ++i)
            if (modeValues[i] == v_.raceMode) mi = int(i);
        vfield(ui, [&]() {
            vlabel(ui, "Mode");
            if (vselect(ui, "racemode", modeNames, mi) && mi < int(modeValues.size())) {
                v_.raceMode = modeValues[size_t(mi)];
                v_.raceNumber = 0;
            }
        });
        std::vector<std::string> raceNames;
        for (const JValue& m : rl["modes"].items())
            if (m["mode"].integer() == v_.raceMode) {
                int k = 0;
                for (const JValue& n : m["races"].items()) raceNames.push_back(std::to_string(++k) + ". " + n.str());
            }
        vfield(ui, [&]() {
            vlabel(ui, "Race");
            vselect(ui, "racenumber", raceNames, v_.raceNumber);
        });
        vfield(ui, [&]() {
            vlabel(ui, "Skill");
            int p = v_.racePro ? 1 : 0;
            if (vseg(ui, "skill", {{"Amateur", 0, ""}, {"Professional", 1, ""}}, p)) v_.racePro = p != 0;
        });
        if (v_.raceMode == 3)
            vfield(ui, [&]() {
                vlabel(ui, "Opponents (Circuit)", v_.raceOpp == 0 ? std::string("race's number") : std::to_string(v_.raceOpp));
                Node& n = ui.current();
                (void)n;
                vslider(ui, "raceopp", 0, 8, 1, v_.raceOpp, false);
            });
        Style r;
        r.row();
        r.gap = 6;
        r.mar.t = 8;
        r.wrap = true;
        ui.open(r);
        if (vbutton(ui, "racestart", "Start race", false, !okRaces || v_.selectedCar < 0)) startViewerRace();
        if (vbutton(ui, "racerestart", "Restart", true, !v_.racing)) {
            svc_.restartRace();
            sceneFocus_ = true;
        }
        const bool nextOk = v_.raceState["resultsShown"].truthy() && v_.raceState["nextRace"].truthy();
        if (vbutton(ui, "racenext", "Next race", true, !nextOk, false, "Offered with the results (results.md, Next race)")) {
            const JValue res = parse(svc_.nextRace());
            if (!res["ok"].truthy()) {
                v_.cityStatus = "No next race: " + (res.isNull() ? std::string("engine error") : res["why"].str());
            } else {
                v_.cityStatus = "Racing: " + res["title"].str() + " · " + std::to_string(res["opponents"].integer()) + " opponents";
                v_.tod = res["timeOfDay"].integer();
                v_.weather = res["weather"].integer();
                v_.racing = true;
                setDriving(true);
                sceneFocus_ = true;
            }
        }
        ui.close();
        ui.rich(helpSpans("The race's own time of day, weather, cops and traffic are used. [N]/[X] next / previous checkpoint (MM2's S/X) · [Esc] stops.",
                          12),
                vt(12, 400, vc::muted));
        ui.close();
    }
    ui.rich(helpSpans("Fly: click the view to look around with the mouse (Esc releases it), or drag with the right button · [W][A][S][D] move · "
                      "[Q]/[E] down/up · [Shift] fast · mouse wheel sets the speed",
                      12),
            vt(12, 400, vc::muted));
}

// The viewer's network panel (web/mp.js's plain panel): the same session as the front end's
// Multiplayer screens.
void FrontEnd::viewerNet(Ui& ui) {
    net::NetLink* link = svc_.netLink();
    const JValue& st = mp_.state;
    const bool on = st["session"].truthy() && st["open"].truthy();
    beginVCard(ui, "net", "Network game",
               on ? (st["inGame"].truthy() ? "in game" : st["isHost"].truthy() ? "hosting" : "in lobby") : (st["closed"].truthy() ? "left" : "offline"),
               VBadge::Plain);
    if (!link) {
        ui.text("Network games are not available in this build.", vt(12.5f, 400, vc::muted));
        ui.close();
        return;
    }
    if (mp_.server.empty()) mp_.server = link->defaultServer();
    Style fs = vt(14);
    fs.bg = vc::card;
    fs.outline(1, vc::line);
    fs.round(6);
    fs.pad = Edges(5, 8);
    fs.self = Align::Stretch;
    vfield(ui, [&]() {
        vlabel(ui, "Server");
        InputOpts o;
        o.style = &fs;
        input(ui, "vserver", mp_.server, o);
    });
    vfield(ui, [&]() {
        vlabel(ui, "Your name");
        InputOpts o;
        o.style = &fs;
        o.max = 18;
        const unsigned f = input(ui, "vname", mp_.viewerName, o);
        if (f & Ui::EditChanged) host_.storageSet("cci.mp.name", mp_.viewerName);
        if (f & Ui::EditSubmit) mpSendRecord();
    });
    ui.text("Car: " + (v_.selectedName.empty() ? std::string("vpbug") : v_.selectedName) + " · paint " + std::to_string(v_.paint) +
                " (change it in the Cars tab)",
            vt(12, 400, vc::muted));
    if (!on) {
        Style r;
        r.row();
        r.gap = 6;
        r.mar.t = 8;
        r.wrap = true;
        ui.open(r);
        if (vbutton(ui, "vhost", "Host a game", false)) {
            if (cities_.empty()) line("Load the game data first.");
            else {
                mp_.owned = false;
                netBeginSession();
                net::SessionDesc d;
                d.name = mp_.sessionName.empty() ? mp_.viewerName + "'s game" : mp_.sessionName;
                d.maxPlayers = mp_.maxPlayers;
                d.version = "cci-1";
                link->host(mp_.server, d);
                mp_.haveSet = false;
                mp_.wantDefaults = true;
            }
        }
        if (vbutton(ui, "vlist", "List games", true)) link->list(mp_.server);
        ui.close();
        vfield(ui, [&]() {
            vlabel(ui, "Join by code");
            Style jr;
            jr.row();
            jr.gap = 6;
            jr.align = Align::Center;
            ui.open(jr);
            Style cs = fs;
            cs.self = Align::Auto;
            InputOpts oc;
            oc.style = &cs;
            oc.max = 4;
            oc.upper = true;
            oc.width = 6 * 14;
            oc.placeholder = "ABCD";
            input(ui, "vcode", mp_.code, oc);
            InputOpts op;
            op.style = &cs;
            op.password = true;
            op.placeholder = "password (if any)";
            input(ui, "vpass", mp_.pass, op);
            if (vbutton(ui, "vjoin", "Join", false) && !trim(mp_.code).empty()) {
                if (cities_.empty()) line("Load the game data first.");
                else {
                    mp_.owned = false;
                    netBeginSession();
                    link->join(mp_.server, toUpperUtf8(trim(mp_.code)), mp_.pass, "cci-1");
                }
            }
            ui.close();
        });
        Style sl;
        sl.column();
        sl.gap = 4;
        sl.mar.t = 6;
        ui.open(sl);
        if (mp_.haveSessions && mp_.sessions.empty()) ui.text("No games on this server.", vt(12, 400, vc::muted));
        for (size_t i = 0; i < mp_.sessions.size(); ++i) {
            const net::SessionInfo& s = mp_.sessions[i];
            Style r2;
            r2.row();
            r2.gap = 6;
            r2.align = Align::Center;
            ui.open(r2);
            Style t = vt(12);
            t.shrink = 1;
            t.minW = 0;
            ui.rich({span(s.code, 12, 700), span(" " + s.name + " · " + std::to_string(s.players) + "/" + std::to_string(s.max) +
                                                     (s.sealed ? " · racing" : "") + (s.password ? " · password" : ""),
                                                 12)},
                    t);
            if (vbutton(ui, "vj" + std::to_string(i), "Join", true)) {
                mp_.owned = false;
                netBeginSession();
                link->join(mp_.server, s.code, mp_.pass, "cci-1");
            }
            ui.close();
        }
        ui.close();
    } else {
        const std::string code = link->code();
        {
            Style r;
            r.row();
            r.gap = 6;
            r.align = Align::Center;
            r.mar = Edges(4, 0, 6, 0);
            ui.open(r);
            ui.rich({span("Game ", 14), span(code, 16, 700)}, vt(14));
            if (vbutton(ui, "vinvite", "invite link", true)) {
                host_.setClipboard(link->inviteUrl(code, mp_.server));
                toast("Invite link copied.");
            }
            ui.close();
        }
        const JValue& ss = st["settings"];
        const int sub = ss["subMode"].integer();
        const bool teams = ss["mode"].integer() == 2 && sub > 0;
        for (const JValue& p : st["players"].items()) {
            Style r;
            r.row();
            r.gap = 8;
            r.align = Align::Center;
            r.pad = Edges(4, 6);
            r.round(6);
            r.bg = rgba(127, 127, 127, 0.12f);
            if (p["local"].truthy()) r.outline(1, rgba(120, 170, 255, 0.6f));
            ui.open(r);
            ui.text(p["name"].str() + (p["host"].truthy() ? " ★" : "") + (p["cheater"].truthy() ? " ⚠" : ""), vt(13, 600));
            Style sm = vt(12, 400, vc::muted);
            sm.grow = 1;
            sm.shrink = 1;
            sm.minW = 0;
            ui.text(p["car"].str() + (teams ? " · " + teamName(sub, p["team"].integer()) : std::string()) + (p["inGame"].truthy() ? " · in game" : "") +
                        (!p["host"].truthy() ? (p["ready"].truthy() ? " · ready" : " · not ready") : ""),
                    sm);
            if (st["isHost"].truthy() && !p["local"].truthy() && vbutton(ui, "vkick" + std::to_string((long long)p["id"].num()), "Kick", true))
                svc_.netKick(p["id"].num());
            ui.close();
        }
        if (st["isHost"].truthy() && !st["inGame"].truthy()) {
            // The host's settings in the panel's own short form.
            mpInitSettings();
            NetSettings& s = mp_.set;
            bool send = false, defaults = false;
            std::vector<std::string> modes = {"Cruise", "Checkpoint", "Circuit", "Blitz", "Cops and Robbers"};
            const int modeValues[5] = {0, 1, 3, 4, 2};
            int mi = 0;
            for (int i = 0; i < 5; ++i)
                if (modeValues[i] == s.mode) mi = i;
            vfield(ui, [&]() {
                vlabel(ui, "Mode");
                if (vselect(ui, "vmode", modes, mi)) {
                    s.mode = modeValues[mi];
                    s.race = 0;
                    send = defaults = true;
                }
            });
            std::vector<std::string> cityNames;
            std::vector<int> cityIdx;
            for (const CityInfo& c : cities_) {
                cityNames.push_back(c.name + " (" + c.raceDir + ")");
                cityIdx.push_back(c.index);
            }
            int ci = 0;
            for (size_t i = 0; i < cityIdx.size(); ++i)
                if (cityIdx[i] == s.city) ci = int(i);
            vfield(ui, [&]() {
                vlabel(ui, "City");
                if (vselect(ui, "vcity", cityNames, ci) && ci < int(cityIdx.size())) {
                    s.city = cityIdx[size_t(ci)];
                    send = defaults = true;
                }
            });
            if (isRaceMode(s.mode)) {
                std::vector<std::string> races;
                const JValue& rl = netRaces(s.city, s.mode);
                for (size_t i = 0; i < rl.size(); ++i) races.push_back(std::to_string(i + 1) + ". " + rl[i]["name"].str());
                vfield(ui, [&]() {
                    vlabel(ui, "Race");
                    if (vselect(ui, "vrace", races, s.race)) send = defaults = true;
                });
                vfield(ui, [&]() {
                    vlabel(ui, "Skill");
                    if (vselect(ui, "vskill", {"Amateur", "Professional"}, s.skill)) send = defaults = true;
                });
            }
            Style g;
            g.grid({Track::fr(1), Track::fr(1)});
            g.gap = 10;
            g.rowGap = 6;
            g.mar = Edges(6, 0);
            ui.open(g);
            auto cell = [&](const char* lbl, const std::function<void()>& body) {
                Style c;
                c.column();
                c.gap = 2;
                ui.open(c);
                ui.text(lbl, vt(12));
                body();
                ui.close();
            };
            cell("Time of day", [&]() {
                if (vselect(ui, "vtod", {"Morning", "Noon", "Evening", "Night"}, s.timeOfDay)) send = true;
            });
            cell("Weather", [&]() {
                if (vselect(ui, "vweather", {"Clear", "Cloudy", "Fog", "Rain"}, s.weather)) send = true;
            });
            if (s.mode == 3)
                cell("Laps", [&]() {
                    if (vslider(ui, "vlaps", 1, 15, 1, s.laps, false)) send = true;
                });
            if (isRaceMode(s.mode))
                cell("Time limit (s)", [&]() {
                    if (vslider(ui, "vtime", 0, 255, 5, s.timeLimit, false)) send = true;
                });
            cell("Pedestrians %", [&]() {
                int p = int(std::lround(s.pedDensity * 100));
                if (vslider(ui, "vpeds", 0, 100, 5, p, false)) {
                    s.pedDensity = p / 100.0;
                    send = true;
                }
            });
            cell("Max players", [&]() {
                if (vslider(ui, "vmax", 2, 8, 1, mp_.maxPlayers, false)) send = true;
            });
            if (s.mode == 2) {
                cell("Game", [&]() {
                    if (vselect(ui, "vsub", {"Free-for-all", "Cops and Robbers", "Robber teams"}, s.subMode)) send = true;
                });
                cell("Gold", [&]() {
                    if (vselect(ui, "vgold", {"Weightless", "Quarter ton (100 kg)", "Half ton (200 kg)"}, s.goldWeight)) send = true;
                });
                cell("Limit", [&]() {
                    if (vselect(ui, "vlimit", {"None", "Time", "Points"}, s.limitType)) {
                        s.limitChoice = 0;
                        send = true;
                    }
                });
                cell("Limit value", [&]() {
                    std::vector<std::string> vals = s.limitType == 1 ? std::vector<std::string>{"5 minutes", "10 minutes", "20 minutes", "30 minutes"}
                                                    : s.limitType == 2 ? std::vector<std::string>{"100 points", "250 points", "500 points", "1000 points"}
                                                                       : std::vector<std::string>{"—"};
                    if (vselect(ui, "vlimitv", vals, s.limitChoice)) send = true;
                });
            }
            cell("Session name", [&]() {
                InputOpts o;
                o.style = &fs;
                o.max = 30;
                if (input(ui, "vsname", mp_.sessionName, o) & Ui::EditSubmit) send = true;
            });
            cell("Password", [&]() {
                InputOpts o;
                o.style = &fs;
                o.max = 20;
                o.placeholder = "none";
                if (input(ui, "vspass", mp_.password, o) & Ui::EditSubmit) send = true;
            });
            ui.close();
            if (defaults) mpDefaults();
            if (send) mpSendSettings();
        } else if (haveSettings(st)) {
            const NetSettings s = currentSettings(st);
            std::string t = netModeName(s.mode) + " · " + s.cityDir;
            if (isRaceMode(s.mode))
                t += " race " + std::to_string(s.race + 1) + (s.mode == 3 ? " · " + std::to_string(s.laps) + " laps" : std::string()) + " · " +
                     std::to_string(s.timeLimit) + " s";
            static const char* todN[4] = {"Morning", "Noon", "Evening", "Night"};
            static const char* weN[4] = {"Clear", "Cloudy", "Fog", "Rain"};
            t += std::string(" · ") + todN[s.timeOfDay & 3] + ", " + weN[s.weather & 3];
            ui.text(t, vt(12, 400, vc::muted));
        }
        if (teams) {
            vfield(ui, [&]() {
                vlabel(ui, "Team");
                const JValue* me = mpLocal(st);
                int t = me ? (*me)["team"].integer() : std::max(0, mp_.team);
                if (vselect(ui, "vteam", {teamName(sub, 0), teamName(sub, 1)}, t)) {
                    mp_.team = t;
                    mpSendRecord();
                }
            });
        }
        if (vcheck(ui, "vslow", "Slow link (dial-up: states every 0.1 s)", mp_.slow, "", 12)) mpSendRecord();
        const JValue* me = mpLocal(st);
        Style r;
        r.row();
        r.gap = 6;
        r.mar.t = 8;
        r.wrap = true;
        ui.open(r);
        if (!st["isHost"].truthy() && !st["inGame"].truthy() &&
            vbutton(ui, "vready", me && (*me)["ready"].truthy() ? "Not ready" : "Ready", true))
            svc_.netReady(!(me && (*me)["ready"].truthy()));
        if (st["isHost"].truthy() && !st["inGame"].truthy() && vbutton(ui, "vstart", "Start", false, !st["canStart"].truthy())) {
            sendSettingsNow(true);
            svc_.netStart();
        }
        if (st["canJoinRunning"].truthy() && vbutton(ui, "vjoinrun", "Join the running game", false)) svc_.netJoinRunning();
        if (st["isHost"].truthy() && st["inGame"].truthy() && vbutton(ui, "vlobby", "Back to lobby", true)) svc_.netLobby();
        if (vbutton(ui, "vleave", "Leave", true)) mpLeave();
        ui.close();
        if (st["isHost"].truthy() && !st["canStart"].truthy()) ui.text(st["why"].str(), vt(12, 400, vc::muted));
    }
    // Chat.
    {
        Style lines;
        lines.column();
        lines.maxH = 120;
        lines.scrollY = true;
        lines.mar.t = 10;
        ui.open("vlines", lines);
        for (const std::string& l : mp_.lines) ui.text(l, vt(12, 400, rgba(230, 233, 238, 0.9f)));
        ui.close();
        if (mp_.lines.size() != viewerChatSeen_) {
            viewerChatSeen_ = mp_.lines.size();
            ui.scrollOf(ui.id("vlines")) = 1e6f;
        }
        Style r;
        r.row();
        r.gap = 6;
        r.align = Align::Center;
        r.mar.t = 4;
        ui.open(r);
        InputOpts o;
        Style cs = fs;
        cs.self = Align::Auto;
        o.style = &cs;
        o.max = 120;
        o.placeholder = "chat (Y in the game)";
        const unsigned f = input(ui, "vsay", mp_.say, o);
        if (vbutton(ui, "vsend", "Say", true) || (f & Ui::EditSubmit)) {
            const std::string t = trim(mp_.say);
            if (!t.empty()) svc_.netChat(t);
            mp_.say.clear();
        }
        ui.close();
    }
    ui.rich(helpSpans("Host a game, then send the invite link or the four-letter code. Every player needs the same MM2 data. Chat with [Y] while "
                      "driving. Peers connect directly (WebRTC) where they can; the server relays otherwise.",
                      12),
            vt(12, 400, vc::muted));
    ui.close();
}

void FrontEnd::viewerMusic(Ui& ui) {
    const JValue& st = v_.musicStatus;
    const bool suspended = !svc_.audioRunning();
    std::string badge = "off";
    VBadge bk = VBadge::Plain;
    if (!st.isNull()) {
        const bool off = st["mode"].str() == "off";
        badge = off ? "off" : !st["data"].truthy() ? "no data" : suspended ? "click to start" : st["playing"].truthy() ? "playing" : "on";
        bk = !off && st["data"].truthy() && st["playing"].truthy() && !suspended ? VBadge::Ok : (!off && !st["data"].truthy() ? VBadge::Warn : VBadge::Plain);
    }
    beginVCard(ui, "music", "Music", badge, bk);
    vfield(ui, [&]() {
        std::vector<VChoice> c = {{"Off", 0, ""},
                                  {"Music", 1, "MM2's interactive soundtrack: menu music here, Cruise music while driving (idle, chases, big air)"},
                                  {"City ambience", 2,
                                   "MM2's ambient option instead of music: the city ambience segment and the city's ambient sounds (gulls, buoys, "
                                   "tug horns, the cable under the SF streets, station voices in London; from the next drive)"}};
        // In a game MM2 offers only the volume sliders.
        if (v_.driving)
            for (VChoice& x : c) {
                x.disabled = true;
                x.tip = "Change it when not driving (in a game MM2 offers only the volume sliders)";
            }
        int m = v_.musicMode;
        if (vseg(ui, "musicmode", c, m)) setMusicMode(m, true);
    });
    vfield(ui, [&]() {
        vlabel(ui, "Volume", std::to_string(v_.musicVolume) + "%");
        bool rel = false;
        if (vslider(ui, "musicvol", 0, 100, 1, v_.musicVolume, false, &rel)) applyMusicVolume(false);
        if (rel) applyMusicVolume(true);
    });
    std::string now;
    if (!st.isNull() && st["mode"].str() != "off" && !st["data"].truthy()) now = "The music is in mm2aud.ar: open your whole MM2 folder.";
    else if (!st.isNull() && st["mode"].str() != "off") {
        now = st["playing"].truthy() ? st["playing"].str() : (st["phase"].str() == "game" && !st["started"].truthy() ? "starting…" : "silent");
        if (st["slot"].truthy() && st["table"].truthy())
            now += " · " + st["table"].str() + (st["row"].integer(-1) >= 0 ? " row " + std::to_string(st["row"].integer()) : std::string());
        if (st["chase"].truthy()) now += " · chase";
    }
    ui.text(now, vt(12.5f, 400, vc::muted));
    {
        const Id id = ui.id("gm");
        Interact it = ui.interact(id);
        Style r;
        r.row();
        r.gap = 6;
        r.align = Align::Center;
        r.mar.t = 8;
        r.cursor = Cursor::Hand;
        r.hit = true;
        Node& n = ui.open(id, r);
        n.label = "General MIDI";
        ui.text(std::string(v_.gmOpen ? "▼ " : "▶ ") + "General MIDI drum kit and piano", vt(12, 400, vc::muted));
        const std::string gb = st["gm"].truthy() ? (st["gmName"].truthy() ? st["gmName"].str() : std::string("loaded"))
                                                  : st["gmBundled"].truthy() ? std::string("FluidR3 (bundled)") : std::string("none");
        vbadge(ui, gb, st["gm"].truthy() || st["gmBundled"].truthy() ? VBadge::Ok : VBadge::Plain);
        ui.close();
        if (it.clicked) v_.gmOpen = !v_.gmOpen;
        if (v_.gmOpen) {
            Style h = vt(11.5f, 400, vc::muted);
            h.mar.t = 6;
            ui.text("A few MM2 tracks use the drum kit and piano of Windows' own gm.dls, which MM2 does not ship. Add yours "
                    "(C:\\Windows\\System32\\drivers\\gm.dls, or put it in your MM2 folder) or a General MIDI SoundFont (.sf2). Without one those "
                    "parts stay silent.",
                    h);
            Style rr;
            rr.row();
            rr.gap = 6;
            rr.mar.t = 8;
            ui.open(rr);
            if (vbutton(ui, "pickgm", "Add gm.dls or .sf2", true, !host_.canPick(Pick::GmBank))) host_.pick(Pick::GmBank);
            if (vbutton(ui, "cleargm", "Remove", true, !st["gm"].truthy())) svc_.clearGmBank();
            ui.close();
        }
    }
    ui.close();
}

void FrontEnd::viewerLog(Ui& ui) {
    Style c;
    c.column();
    c.bg = vc::card;
    c.outline(1, vc::line);
    c.round(8);
    c.pad = Edges(10);
    c.minH = 120;
    c.hit = true;
    ui.open("log", c);
    Style head;
    head.row();
    head.align = Align::Center;
    head.justify = Justify::SpaceBetween;
    head.mar.b = 6;
    ui.open(head);
    Style hr;
    hr.row();
    hr.gap = 6;
    hr.align = Align::Center;
    ui.open(hr);
    Style h = vt(13, 700, vc::muted);
    h.text.upper = true;
    h.text.letterSpacing = 0.78f;
    ui.text("Problems", h);
    vbadge(ui, std::to_string(v_.problems), v_.problems ? VBadge::Warn : VBadge::Plain);
    ui.close();
    vcheck(ui, "logall", "all messages", v_.logAll, "", 12);
    ui.close();
    Style l;
    l.column();
    l.maxH = ui.viewH() * 0.3f;
    l.scrollY = true;
    l.hit = true;
    ui.open("loglist", l);
    for (const auto& e : v_.log) {
        if (!v_.logAll && e.level < 1) continue;
        Style li = vt(11.5f, 400, e.level == 0 ? vc::muted : e.level >= 2 ? hex(0xffb3b1) : vc::text);
        li.text.mono = true;
        li.pad = Edges(2, 0, 2, 8);
        li.mar.b = 2;
        li.border = Edges(0, 0, 0, 2);
        li.borderColor = e.level >= 2 ? vc::error : e.level == 1 ? vc::warn : vc::line;
        if (e.count > 1) {
            TextStyle ts = li.text;
            Span a{e.msg, ts};
            Span b{" ×" + std::to_string(e.count), ts};
            b.color = rgbaHex(vc::muted);
            ui.rich({a, b}, li);
        } else {
            ui.text(e.msg, li);
        }
    }
    ui.close();
    if (logScroll_) {
        logScroll_ = false;
        ui.scrollOf(ui.id("loglist")) = 1e6f;
    }
    ui.close();
}

// Once a second: the frame statistics, the sound and the music state.
void FrontEnd::viewerTick() {
    if (!viewerRestored_) restoreViewerState();
    const double now = svc_.clock();
    v_.readout = svc_.readout();
    if (pendingCityLoad_ >= 0 && ++pendingCityFrames_ >= 3) {
        const int i = pendingCityLoad_;
        pendingCityLoad_ = -1;
        const double t0 = svc_.clock();
        const JValue info = parse(svc_.loadCity(i));
        char secs[32];
        std::snprintf(secs, sizeof secs, "%.1f", svc_.clock() - t0);
        afterCityLoaded(i, info, secs);
        if (v_.driving) setDriving(false);
        sceneFocus_ = true;
    }
    if (now - v_.tick < 1.0) return;
    v_.tick = now;
    v_.stats = parse(svc_.stats());
    v_.soundState = parse(svc_.soundState());
    v_.musicStatus = parse(svc_.musicStatus());
}

// The view's own overlays: the readout, key hints and statistics (viewer), the plain race
// overlay of the viewer's races, and the message box over the view.
void FrontEnd::buildViewerOverlays(Ui& ui) {
    if (play_) return;
    const Rect sc = sceneRectPx(ui);
    auto hud = [&](const std::string& key, const std::string& text, bool mono, Color c, float size, bool right, bool bottom) {
        if (text.empty()) return;
        Style s = vt(size, 400, c);
        s.text.mono = mono;
        s.absolute = true;
        if (right) s.right = ui.viewW() - sc.right() + 10;
        else s.left = sc.x + 10;
        if (bottom) s.bottom = ui.viewH() - sc.bottom() + 10;
        else s.top = sc.y + 10;
        s.bg = rgba(10, 12, 16, 0.66f);
        s.round(6);
        s.pad = Edges(4, 9);
        s.maxW = sc.w - 20;
        Node& n = ui.text(text, s);
        n.label = key;
    };
    if (!v_.readout.empty()) hud("readout", v_.readout, true, vc::text, 12, false, false);
    std::string hints = v_.driving ? "Arrows/WASD drive · Space handbrake · C camera · F dash · Backspace mirror · G reset · Esc stop"
                        : v_.view == 1 ? "Click to look around (Esc releases) · WASD move · Q/E down/up · Shift fast · wheel: speed"
                                       : "Drag to orbit · right-drag to pan · wheel to zoom · R resets";
    hud("hints", hints, false, vc::muted, 12, false, true);
    if (!v_.stats.isNull()) {
        const JValue& s = v_.stats;
        char buf[256];
        std::snprintf(buf, sizeof buf, "%d draws · %.1fk tris · %d tex (%.0f MB) · %.1f ms/frame (%.1f busy)", s["drawCalls"].integer(),
                      s["triangles"].num() / 1000.0, s["textures"].integer(), s["textureMB"].num(), s["frameMs"].num(), s["cpuMs"].num());
        hud("stats", buf, true, vc::muted, 11, true, true);
    }
    // The plain race overlay (the viewer's race card's): countdown, gates, place, clocks, results.
    const JValue& d = v_.raceState;
    if (d["active"].truthy() && !inGame_) {
        Style b = vt(13, 400, hex(0xeef1f5));
        b.text.mono = true;
        b.absolute = true;
        b.right = ui.viewW() - sc.right() + 12;
        b.top = sc.y + 12;
        b.minW = 220;
        b.maxW = 360;
        b.pad = Edges(10, 12);
        b.round(8);
        b.bg = rgba(10, 12, 16, 0.72f);
        b.column();
        ui.open("raceoverlay", b);
        auto mono = [&](const std::string& t, Color c = hex(0xeef1f5), int w = 400, float size = 13) {
            Style s = vt(size, w, c);
            s.text.mono = true;
            s.text.lineHeight = size * 1.45f;
            s.nowrap = true;
            ui.text(t, s);
        };
        mono(d["modeName"].str() + " · " + d["title"].str() + (d["pro"].truthy() ? " · Pro" : ""), hex(0xeef1f5), 700);
        if (!d["countdown"].isNull()) mono("Time left  " + d["countdown"].str(), d["warning"].truthy() ? hex(0xff6b6b) : hex(0xeef1f5));
        if (d["mode"].integer() != 6) mono("Time       " + d["clock"].str());
        if (d["lap"].truthy()) mono("Lap        " + d["lap"].str() + "   " + d["lapClock"].str());
        if (d["laps"].size()) {
            std::string l;
            for (const JValue& x : d["laps"].items()) l += (l.empty() ? "" : "  ") + x.str();
            mono("Laps       " + l);
        }
        if (d["event"].truthy()) mono("Event      " + d["event"].str() + " (kind " + std::to_string(d["kind"].integer()) + ")");
        mono("Gates      " + d["gates"].str());
        if (d["mode"].integer() == 1 || d["mode"].integer() == 3)
            mono("Place      " + std::to_string(d["place"].integer()) + "/" + std::to_string(d["racers"].integer()));
        if (!d["targetDist"].isNull()) {
            const double bb = d["targetBearing"].num();
            const char* arrow = std::fabs(bb) < 20 ? "↑" : bb > 0 ? (bb < 110 ? "↗" : "↘") : (bb > -110 ? "↖" : "↙");
            mono(std::string("Next gate  ") + arrow + " " + std::to_string(int(std::lround(d["targetDist"].num()))) + " m");
        }
        if (d["opponents"].size()) {
            std::string o;
            for (const JValue& x : d["opponents"].items()) o += (o.empty() ? "" : " ") + std::string(x["finished"].truthy() ? "✓" : "") + std::to_string(x["place"].integer());
            mono("Opponents  " + o);
        }
        if (d["endMenu"].truthy()) mono("Race over — Restart (panel) or Esc", hex(0xff6b6b));
        if (d["msg"].truthy()) {
            Style m = vt(d["msgUpper"].truthy() ? 26 : 18, 700, d["msgUpper"].truthy() ? hex(0xffffff) : hex(0xffd76a));
            m.text.mono = true;
            m.mar.t = 6;
            ui.text(d["msg"].str(), m);
        }
        if (d["msg2"].truthy()) mono(d["msg2"].str(), hex(0xffd76a));
        if (d["resultsShown"].truthy() && d["results"].size()) {
            Style r;
            r.column();
            r.mar.t = 8;
            r.pad.t = 6;
            r.border = Edges(1, 0, 0, 0);
            r.borderColor = rgba(255, 255, 255, 0.25f);
            ui.open(r);
            mono("Results" + (d["score"].integer() ? " · score " + std::to_string(d["score"].integer()) : std::string()) +
                 (d["nextRace"].truthy() ? " · next race open" : ""), hex(0xeef1f5), 700);
            for (const JValue& x : d["results"].items())
                mono(std::to_string(x["place"].integer()) + ".  " + x["name"].str() + "  " + x["time"].str());
            ui.close();
        }
        ui.close();
    }
    // The message box over the view (loading, no data yet).
    if (v_.overlay) {
        Style ov;
        ov.absolute = true;
        ov.left = sc.x;
        ov.top = sc.y;
        ov.w = sc.w;
        ov.h = sc.h;
        ov.stack();
        ov.align = Align::Center;
        ov.justify = Justify::Center;
        ui.open("overlay", ov);
        Style box;
        box.column();
        box.bg = rgba(15, 17, 21, 0.86f);
        box.outline(1, dropHover_ ? vc::accent : vc::line);
        box.round(10);
        box.pad = Edges(18, 22);
        box.maxW = 460;
        box.align = Align::Center;
        ui.open(box);
        Style t = vt(16, 600);
        t.textAlign = TextAlign::Center;
        ui.text(v_.overlayTitle, t);
        if (!v_.overlayText.empty()) {
            Style x = vt(13, 400, vc::muted);
            x.textAlign = TextAlign::Center;
            x.mar.t = 6;
            x.text.lineHeight = 13 * 1.5f;
            ui.text(v_.overlayText, x);
        }
        ui.close();
        ui.close();
    }
}

// A drop-down list (the page's <select>): the current choice, a list below it when opened; the
// arrow keys change the choice while it has the focus.
bool FrontEnd::vselect(Ui& ui, const std::string& key, const std::vector<std::string>& options, int& current) {
    const Id id = ui.id(key);
    Interact it = ui.interact(id, Ui::InteractFocusable | Ui::InteractArrowsUD);
    bool changed = false;
    Style s = vt(14);
    s.row();
    s.align = Align::Center;
    s.bg = vc::card;
    s.outline(1, it.hover || openSelect_ == id ? vc::accent : vc::line);
    s.round(6);
    s.pad = Edges(5, 8);
    s.self = Align::Stretch;
    s.cursor = Cursor::Hand;
    s.hit = true;
    Node& n = ui.open(id, s);
    n.label = key;
    Style t = vt(14);
    t.grow = 1;
    t.minW = 0;
    t.nowrap = true;
    t.ellipsis = true;
    ui.text(current >= 0 && current < int(options.size()) ? options[size_t(current)] : std::string(), t);
    ui.text("▼", vt(9, 400, vc::muted));
    ui.close();
    if (it.clicked) {
        openSelect_ = openSelect_ == id ? 0 : id;
        selectSince_ = ui.time();
    }
    if (it.focused && openSelect_ != id && !options.empty()) {
        if (ui.keyPressed(Key::Down) && current + 1 < int(options.size())) {
            ++current;
            changed = true;
        }
        if (ui.keyPressed(Key::Up) && current > 0) {
            --current;
            changed = true;
        }
    }
    if (openSelect_ == id) {
        // The list (a popup above everything, under the field).
        const Rect r = it.rect;
        ui.pushLayer(Layer::Popup);
        Style back;
        back.absolute = true;
        back.left = back.top = back.right = back.bottom = 0;
        back.hit = true;
        const Id backId = ui.id(key + "#back");
        Interact bi = ui.interact(backId, 0);
        ui.leaf(backId, back);
        Style list;
        list.absolute = true;
        list.left = r.x;
        const float maxH = std::min(300.0f, std::max(120.0f, ui.viewH() - r.bottom() - 8));
        list.top = r.bottom() + 2;
        list.w = r.w;
        list.maxH = maxH;
        list.scrollY = true;
        list.column();
        list.bg = hex(0x232a34);
        list.outline(1, vc::line);
        list.round(6);
        list.pad = Edges(3, 0);
        list.shadow(0, 8, 24, 0, rgba(0, 0, 0, 0.45f));
        list.hit = true;
        ui.open(key + "#list", list);
        for (size_t i = 0; i < options.size(); ++i) {
            const Id oid = ui.id(key + "#o" + std::to_string(i));
            Interact oi = ui.interact(oid);
            Style o = vt(14);
            o.pad = Edges(5, 10);
            o.bg = int(i) == current ? vc::accent2 : (oi.hover ? rgba(255, 255, 255, 0.06f) : Color());
            o.hit = true;
            o.cursor = Cursor::Hand;
            Node& on = ui.text(options[i], o, oid);
            on.label = options[i];
            if (oi.clicked) {
                current = int(i);
                changed = true;
                openSelect_ = 0;
            }
        }
        ui.close();
        if (bi.clicked || ui.backPressed()) openSelect_ = 0;
        ui.popLayer();
    }
    return changed;
}

}  // namespace fe
}  // namespace cc
