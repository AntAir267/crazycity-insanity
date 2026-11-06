// The front end's calls from the page (through Module.ccall), kept for scripted checks: the
// engine's own front end (app/front_end.*) calls the same functions directly (app/menu_data.h,
// app/game_api.h). Strings returned stay valid until the next call that returns a string.
#include "platform/keepalive.h"

#include <string>

#include "app/game_api.h"
#include "app/menu_data.h"

using namespace cc;

namespace {
std::string g_ret;
const char* ret(std::string s) {
    g_ret = std::move(s);
    return g_ret.c_str();
}
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE const char* cc_ui_load_mmlang(const uint8_t* data, int size) {
    return ret(menu::loadMmlang(data, size_t(size < 0 ? 0 : size)));
}
EMSCRIPTEN_KEEPALIVE void cc_ui_players_clear() { menu::playersClear(); }
EMSCRIPTEN_KEEPALIVE void cc_ui_set_controller_buttons(int n) { menu::setControllerButtons(n); }
EMSCRIPTEN_KEEPALIVE int cc_ui_players_put(const char* path, const char* b64) { return menu::playersPut(path, b64) ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_start() {
    std::string s = menu::start();
    gameapi::afterStart();
    return ret(s);
}
EMSCRIPTEN_KEEPALIVE const char* cc_ui_state() { return ret(menu::state()); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_select_driver(int i) { return ret(menu::selectDriver(i)); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_new_driver(const char* name, int skill) { return ret(menu::newDriver(name, skill)); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_delete_driver(int i) { return ret(menu::deleteDriver(i)); }
EMSCRIPTEN_KEEPALIVE void cc_ui_import_begin() { menu::importBegin(); }
EMSCRIPTEN_KEEPALIVE void cc_ui_import_file(const char* path, const uint8_t* data, int size) {
    menu::importFile(path, data, size_t(size < 0 ? 0 : size));
}
EMSCRIPTEN_KEEPALIVE const char* cc_ui_import_finish() { return ret(menu::importFinish()); }
EMSCRIPTEN_KEEPALIVE void cc_ui_set_option(const char* name, double v) { menu::setOption(name, v); }
// One entry of the binding table (customise controls): menu::setBinding. 0 when out of range.
EMSCRIPTEN_KEEPALIVE int cc_ui_set_binding(int device, int action, int kind, int inputDevice, int component) {
    return menu::setBinding(device, action, kind, inputDevice, component) ? 1 : 0;
}
EMSCRIPTEN_KEEPALIVE const char* cc_ui_options_done() { return ret(menu::optionsDone()); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_options_cancel() { return ret(menu::optionsCancel()); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_options_defaults(const char* screen) { return ret(menu::optionsDefaults(screen)); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_city_races(int cityIndex) { return ret(menu::cityRaces(cityIndex)); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_cars() { return ret(menu::cars()); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_driver_record() { return ret(menu::driverRecord()); }
EMSCRIPTEN_KEEPALIVE const char* cc_ui_race_stats(int cityIndex, int skill) { return ret(menu::raceStats(cityIndex, skill)); }
EMSCRIPTEN_KEEPALIVE void cc_game_set_environment(int own, int timeOfDay, int weather, double traffic, double cops, int laps,
                                                  double peds) {
    gameapi::setEnvironment(own, timeOfDay, weather, traffic, cops, laps, peds);
}
EMSCRIPTEN_KEEPALIVE const char* cc_game_begin(int cityIndex, int mode, int race, int carIndex, int paint, int opponents) {
    return ret(gameapi::begin(cityIndex, mode, race, carIndex, paint, opponents));
}
EMSCRIPTEN_KEEPALIVE const char* cc_game_next_race(int cityIndex, int carIndex, int paint) {
    return ret(gameapi::nextRace(cityIndex, carIndex, paint));
}
EMSCRIPTEN_KEEPALIVE const char* cc_game_last_outcome() { return ret(menu::lastOutcome()); }
EMSCRIPTEN_KEEPALIVE void cc_game_set_view_distance(double m) { gameapi::setViewDistance(m); }
EMSCRIPTEN_KEEPALIVE void cc_game_pause(int on) { gameapi::pause(on != 0); }
EMSCRIPTEN_KEEPALIVE void cc_game_restart() { gameapi::restart(); }
EMSCRIPTEN_KEEPALIVE const char* cc_game_end() { return ret(gameapi::end()); }
EMSCRIPTEN_KEEPALIVE void cc_ui_net_prepare(int on) { gameapi::netPrepare(on != 0); }
EMSCRIPTEN_KEEPALIVE void cc_ui_net_game_start(const char* netName, const char* car, int paint, const char* raceDir, int mode,
                                               int race) {
    gameapi::netGameStart(netName ? netName : "", car ? car : "", paint, raceDir ? raceDir : "", mode, race);
}
EMSCRIPTEN_KEEPALIVE const char* cc_ui_net_game_end() { return ret(gameapi::netGameEnd()); }
EMSCRIPTEN_KEEPALIVE const char* cc_game_record_result(int mode, int race, int place, int finished, double time,
                                                       const char* laps, int crashPassed, int ownSettings,
                                                       double difficulty) {
    return ret(menu::recordResult(mode, race, place, finished, time, laps ? laps : "", crashPassed, ownSettings, difficulty));
}
EMSCRIPTEN_KEEPALIVE int cc_debug_race_jump() { return gameapi::debugRaceJump(); }
EMSCRIPTEN_KEEPALIVE void cc_hud_demo(int what, double a, double b, double c) { gameapi::hudDemo(what, a, b, c); }

}  // extern "C"
