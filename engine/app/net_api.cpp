// The network game's calls from the page (through Module.ccall): web/net.js pushes the transport's
// events in (cc_net_push); the rest are kept for scripted checks (the engine's front end calls
// app/game_api.h directly). Strings returned stay valid until the next call that returns a string.
#include "platform/keepalive.h"

#include <string>

#include "app/app.h"
#include "app/game_api.h"
#include "platform/web_transport.h"

using cc::theApp;
namespace gameapi = cc::gameapi;

namespace {
std::string g_net;
const char* ret(std::string s) {
    g_net = std::move(s);
    return g_net.c_str();
}
std::string str(const char* s) { return s ? std::string(s) : std::string(); }
}  // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE void cc_net_begin(const char* name, const char* car, int paint) { gameapi::netBegin(str(name), str(car), paint); }
EMSCRIPTEN_KEEPALIVE void cc_net_end() { gameapi::netEnd(); }

// A transport event from web/net.js: kind 0 opened, 1 joined, 2 left, 3 host, 4 message, 5 closed.
EMSCRIPTEN_KEEPALIVE void cc_net_push(int kind, double peer, int reliable, const uint8_t* data, int size, const char* json) {
    if (cc::net::WebTransport* t = cc::net::WebTransport::current())
        t->push(kind, cc::net::PlayerId(peer), reliable != 0, data, size_t(size < 0 ? 0 : size), json ? json : "");
}

EMSCRIPTEN_KEEPALIVE void cc_net_record(const char* name, const char* car, int paint, int team, int slowLink) {
    gameapi::netRecord(str(name), str(car), paint, team, slowLink != 0);
}
EMSCRIPTEN_KEEPALIVE void cc_net_ready(int on) { gameapi::netReady(on != 0); }
EMSCRIPTEN_KEEPALIVE void cc_net_settings(int mode, const char* city, int race, int skill, int timeOfDay, int weather, int laps,
                                          double timeLimit, double pedDensity, int subMode, int goldWeight, int limitType,
                                          int limitChoice) {
    gameapi::netSettings(mode, str(city), race, skill, timeOfDay, weather, laps, timeLimit, pedDensity, subMode, goldWeight,
                         limitType, limitChoice);
}
EMSCRIPTEN_KEEPALIVE const char* cc_net_defaults(int mode, const char* city, int race, int skill) {
    return ret(gameapi::netDefaults(mode, str(city), race, skill));
}
EMSCRIPTEN_KEEPALIVE void cc_net_describe(const char* name, const char* password, int maxPlayers) {
    gameapi::netDescribe(str(name), str(password), maxPlayers);
}
EMSCRIPTEN_KEEPALIVE int cc_net_start() { return gameapi::netStart() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int cc_net_join_running() { return gameapi::netJoinRunning() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void cc_net_kick(double id) { gameapi::netKick(id); }
EMSCRIPTEN_KEEPALIVE void cc_net_lobby() { gameapi::netLobby(); }
EMSCRIPTEN_KEEPALIVE void cc_net_quit() { gameapi::netQuit(); }
EMSCRIPTEN_KEEPALIVE void cc_net_chat(const char* text) { gameapi::netChat(str(text)); }
EMSCRIPTEN_KEEPALIVE int cc_net_restart() { return gameapi::netRestart() ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE const char* cc_net_state() { return ret(gameapi::netState()); }

}  // extern "C"
