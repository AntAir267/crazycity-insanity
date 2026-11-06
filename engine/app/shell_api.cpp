// What the page's thin shell (web/app.js) hands the engine's front end: the loading state before
// the data is in, the player's mmlang.dll, a held players folder, the end of an import, the
// clipboard events, files dragged over the view; and the scripted checks' hooks (the UI's
// interactive nodes with their labels and places, the screen, starting and ending a game).
#include <emscripten.h>

#include <string>

#include "app/app.h"
#include "app/front_end.h"
#include "core/json.h"

using cc::theApp;

namespace {
std::string g_ret;
const char* ret(std::string s) {
    g_ret = std::move(s);
    return g_ret.c_str();
}
cc::fe::FrontEnd* fe() { return theApp().frontEnd(); }
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void cc_fe_status(const char* text, double progress) {
    if (fe()) fe()->setLoadStatus(text ? text : "", float(progress));
}
EMSCRIPTEN_KEEPALIVE void cc_fe_overlay(const char* title, const char* text) {
    if (fe()) fe()->setOverlay(title ? title : "", text ? text : "");
}
EMSCRIPTEN_KEEPALIVE void cc_fe_source(const char* text) {
    if (fe()) fe()->setDataSource(text ? text : "");
}
EMSCRIPTEN_KEEPALIVE void cc_fe_drop(int on) {
    if (fe()) fe()->setDropHover(on != 0);
}
// The player's mmlang.dll (bytes copied into wasm memory by the page); keep: remember it.
EMSCRIPTEN_KEEPALIVE void cc_fe_load_strings(const uint8_t* data, int size, int keep) {
    if (fe()) fe()->loadStrings(data, size_t(size < 0 ? 0 : size), keep != 0);
}
EMSCRIPTEN_KEEPALIVE void cc_fe_pending_players(int on) {
    if (fe()) fe()->setPendingPlayersFolder(on != 0);
}
EMSCRIPTEN_KEEPALIVE void cc_fe_dev_players(int on) {
    if (fe()) fe()->setDevPlayers(on != 0, on == 2);
}
// A players folder went into cc_ui_import_begin / cc_ui_import_file: show the report.
EMSCRIPTEN_KEEPALIVE void cc_fe_import_done() {
    if (fe()) fe()->importFinished();
}
EMSCRIPTEN_KEEPALIVE void cc_fe_toast(const char* text) {
    if (text) theApp().ui().toast(text);
}
EMSCRIPTEN_KEEPALIVE void cc_fe_open() {
    if (fe()) fe()->openFrontEnd();
}
// The page's clipboard events: 0 paste (the pasted text), 1 copy, 2 cut.
EMSCRIPTEN_KEEPALIVE void cc_ui_clipboard(int type, const char* text) {
    cc::ui::InputEvent e;
    e.type = type == 0 ? cc::ui::InputEvent::Paste : type == 1 ? cc::ui::InputEvent::Copy : cc::ui::InputEvent::Cut;
    if (text) e.text = text;
    theApp().pushUiEvent(e);
}
// A key or text the page sends itself (tests, or a mobile keyboard's hidden field).
EMSCRIPTEN_KEEPALIVE void cc_ui_text(const char* text) {
    cc::ui::InputEvent e;
    e.type = cc::ui::InputEvent::Text;
    if (text) e.text = text;
    theApp().pushUiEvent(e);
}

// ---- scripted checks

// The interactive and labelled nodes of the last frame: label, focusable, layer and the rect in
// CSS px (the canvas' own coordinates).
EMSCRIPTEN_KEEPALIVE const char* cc_ui_tree() {
    cc::ui::Ui& ui = theApp().ui();
    // Layout px times the UI scale are device px; the page works in CSS px.
    const float dpr = theApp().platform().pixelRatio();
    const float toCss = ui.scale() / (dpr > 0 ? dpr : 1.0f);
    cc::JsonWriter j;
    j.beginObject();
    j.field("screen", fe() ? fe()->screenName() : std::string());
    j.field("scale", double(ui.scale()));
    j.field("focus", (double)(ui.focus() & 0xFFFFFFFFFFFFull));
    j.key("nodes").beginArray();
    for (const auto& n : ui.snapshot()) {
        j.beginObject();
        j.field("label", n.label);
        j.field("focusable", n.focusable);
        j.field("focused", n.id == ui.focus());
        j.field("layer", n.layer);
        j.field("x", double(n.rect.x * toCss));
        j.field("y", double(n.rect.y * toCss));
        j.field("w", double(n.rect.w * toCss));
        j.field("h", double(n.rect.h * toCss));
        j.field("vx", double(n.visible.x * toCss));
        j.field("vy", double(n.visible.y * toCss));
        j.field("vw", double(n.visible.w * toCss));
        j.field("vh", double(n.visible.h * toCss));
        j.endObject();
    }
    j.endArray();
    j.field("toast", ui.toastShown() ? ui.toastText() : std::string());
    j.endObject();
    return ret(j.str());
}
// One frame now, with the input that waits (scripted checks in a page without animation frames).
EMSCRIPTEN_KEEPALIVE void cc_frame_now() { theApp().frame(); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_screen() { return ret(fe() ? fe()->screenName() : std::string()); }
// Starts a game as the menus would (city, mode, race, car index, paint).
EMSCRIPTEN_KEEPALIVE void cc_fe_play(int city, int mode, int race, int car, int paint) {
    if (fe()) fe()->testPlay(city, mode, race, car, paint);
}
EMSCRIPTEN_KEEPALIVE void cc_fe_quit(const char* screen) {
    if (fe()) fe()->testQuit(screen ? screen : "");
}
// The running network game's clock (−1: none), for the page's catch-up while it is hidden.
EMSCRIPTEN_KEEPALIVE double cc_net_game_time() {
    return theApp().driveView().netActive() ? theApp().driveView().gameClock() : -1.0;
}

}  // extern "C"
