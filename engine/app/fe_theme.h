// The front end's look (our own design, as web/ui.css had it: dark slate glass, a teal accent,
// IBM Plex type) as widgets of the engine's UI: menu items, buttons, tabs, segmented controls,
// MM2's 19-step sliders, check boxes, text fields, list rows, badges, cards, tables, steppers.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "ui/ui.h"

namespace cc {
namespace fe {

using ui::Color;
using ui::Style;
using ui::Ui;

namespace col {
constexpr Color text = ui::hex(0xe8edf4);
constexpr Color muted = ui::hex(0x93a1b5);
constexpr Color accent = ui::hex(0x2dd4bf);
constexpr Color accent2 = ui::hex(0x14b8a6);
constexpr Color accentInk = ui::hex(0x04201c);
constexpr Color warn = ui::hex(0xfbbf24);
constexpr Color bad = ui::hex(0xf87171);
constexpr Color good = ui::hex(0x4ade80);
constexpr Color line = ui::rgba(148, 163, 184, 0.18f);
constexpr Color lineStrong = ui::rgba(148, 163, 184, 0.34f);
constexpr Color panel = ui::rgba(17, 24, 35, 0.82f);
constexpr Color panelSolid = ui::hex(0x111823);
constexpr Color hudInk = ui::hex(0xf8fafc);
constexpr Color hudGlass = ui::rgba(8, 12, 18, 0.62f);
}  // namespace col

// Text styles of the design.
Style tH();          // .fe-h  22px 750
Style tSub();        // .fe-sub 13px muted
Style tSec();        // .fe-sec 11px uppercase muted
Style tNote(bool warn = false);  // .fe-note 12px
Style tBody(float size = 13, int weight = 400, Color c = col::text);
ui::Span span(const std::string& t, float size, int weight = 400, uint32_t color = 0);
ui::Span kbd(const std::string& t, float size = 11);  // a key cap (.fe-foot kbd)
uint32_t rgbaHex(const Color& c);

void h1(Ui& ui, const std::string& t);
void sub(Ui& ui, const std::string& t);
void sec(Ui& ui, const std::string& t, float marginTop = 16);
void note(Ui& ui, const std::string& t, bool warn = false);
void noteRich(Ui& ui, std::vector<ui::Span> spans);
enum class Msg { Warn, Bad, Ok };
void msg(Ui& ui, const std::string& t, Msg kind);
enum class Badge { Accent, Pro, Lock, Ok, Muted };
void badge(Ui& ui, const std::string& t, Badge kind);

// .fe-item: an icon, a title and a line under it.
bool item(Ui& ui, const std::string& key, const std::string& icon, const std::string& title, const std::string& desc, bool big = false,
          bool disabled = false);
enum class Btn { Primary, Ghost, Danger, Go, GoGhost, Small };
// `focusable`: false for buttons the keyboard must not reach (while a binding is captured).
bool button(Ui& ui, const std::string& key, const std::string& label, Btn kind = Btn::Primary, bool disabled = false,
            bool focusable = true);
// .fe-navbtn (the top bar).
bool navButton(Ui& ui, const std::string& key, const std::string& label);
// .fe-tabs: returns true when another tab was picked (`current` changes).
struct Choice {
    std::string label;
    int value = 0;
    bool disabled = false;
};
bool tabs(Ui& ui, const std::string& key, const std::vector<Choice>& choices, int& current);
// .fe-seg; `fixed`: shown but not editable.
bool seg(Ui& ui, const std::string& key, const std::vector<Choice>& choices, int& current, bool fixed = false);
std::vector<Choice> choices(const std::vector<std::string>& labels);
// A labelled field: the label row (label left, value right), then the control built by `body`.
void field(Ui& ui, const std::string& label, const std::string& value, const std::function<void()>& body);
// MM2's sliders: 19 equal steps between min and max (menus.md); true while it moves.
bool slider(Ui& ui, const std::string& key, float min, float max, float& v, bool disabled = false);
// The cop control's slider (menus.md, "The cop control"): shows min(v, 1); an arrow key steps by
// 1/19 from the shown position in single precision (a step up at the top writes exactly 1.0);
// a click or drag sets the fraction. `viaKey` tells which.
bool mm2Slider(Ui& ui, const std::string& key, float& v, bool disabled, bool& viaKey);
bool check(Ui& ui, const std::string& key, const std::string& label, bool& v, bool disabled = false);
// .fe-input. Returns ui::Ui::Edit* flags.
struct InputOpts {
    size_t max = 0;
    std::string placeholder;
    bool upper = false, password = false;
    float width = ui::kAuto;   // px; auto = fill
    float grow = 1;
    float fontSize = 15;
    bool code = false;         // the join code's look (bold, wide letter spacing, centred)
    Style* style = nullptr;    // a different look (the HUD chat box)
};
unsigned input(Ui& ui, const std::string& key, std::string& v, const InputOpts& o);
// .fe-step: − value +.
bool stepper(Ui& ui, const std::string& key, int& value, int min, int max, int step, const std::function<std::string(int)>& fmt,
             bool disabled = false);

// Containers.
void beginCard(Ui& ui, const std::string& key = std::string(), float marginTop = 0, float width = ui::kAuto);
void endCard(Ui& ui);
void beginRow(Ui& ui, ui::Justify j = ui::Justify::Start, float gap = 8, float marginTop = 0, bool wrap = true);
void endRow(Ui& ui);
// A scrolling list (.fe-list), at most `maxH` px tall.
void beginList(Ui& ui, const std::string& key, float maxH);
void endList(Ui& ui);
// .fe-li: number, text, right-hand meta and badges (built by `extra`), on / locked.
bool listItem(Ui& ui, const std::string& key, const std::string& num, const std::string& text, bool on, bool locked,
              const std::function<void()>& extra = nullptr, bool interactive = true, const std::string& sub = std::string());

// Tables (.fe-table): column tracks, a header row, then cells.
struct TableCol {
    std::string head;
    ui::Track track = ui::Track::autoW();
    bool num = false;   // right-aligned
};
void beginTable(Ui& ui, const std::vector<TableCol>& cols, float marginTop = 0, float marginBottom = 0);
// One row's cells; `me` colours it in the accent (the player's row), `team` in the warning colour.
void tableRow(Ui& ui, const std::vector<TableCol>& cols, const std::vector<std::string>& cells, bool me = false,
              const std::vector<bool>& dim = {}, bool team = false, const std::vector<Color>* dots = nullptr);
void endTable(Ui& ui);
// .fe-kv: muted keys and values in two columns.
void kv(Ui& ui, const std::vector<std::pair<std::string, std::string>>& rows, float rowGap = 6, float fontSize = 13);

}  // namespace fe
}  // namespace cc
