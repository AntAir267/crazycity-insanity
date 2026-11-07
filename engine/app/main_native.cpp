// The native program: the engine in a window of its own (SDL3, OpenGL ES 3.0), reading the
// player's Midtown Madness 2 folder straight from disk. What the browser keeps in its storage (the
// players folder, the settings) are real files here: the profiles in MM2's own formats under
// ~/.local/share/crazycity-insanity/players/, the program's settings in
// ~/.config/crazycity-insanity/native.cfg.
//
// The program opens on the engine-drawn front end (app/front_end.h), the same menus as the
// browser's. For testing, the command line can start a Cruise drive or any race straight away
// (city, mode, race, car, paint, skill) and show a small debug overlay (--overlay, F1);
// `--selftest` runs offscreen: the main menu, Cruise and a race through the front end, PNG
// captures, the audio output.
//
// Network games (platform/native_transport.*, platform/host_server.*) are hosted and joined from
// the Multiplayer screens (platform/native_net_link.*). For testing, --host-game hosts one on this
// computer (the engine's own session server on its own thread; --lan opens it to the local
// network), --join CODE joins one, --net names another server (signal.js or another player's
// game); browsers and native players play together. `crazycity --server` runs only the session
// server, without a window.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "app/app.h"
#include "app/front_end.h"
#include "app/native_host.h"
#include "platform/native_net_link.h"
#include "core/json_read.h"
#include "core/log.h"
#include "core/strutil.h"
#include "drive/drive_session.h"
#include "game/player_files.h"
#include "game/race.h"
#include "game/race_catalogue.h"
#include "native/binding_names.h"
#include "native/files.h"
#include "native/overlay.h"
#include "native/png.h"
#include "net/net_game.h"
#include "net/remote_car.h"
#include "net/session.h"
#include "net/settings.h"
#include "platform/host_server.h"
#include "platform/native_transport.h"
#include "platform/platform.h"
#include "platform/web_bridge.h"
#include "render/gl_util.h"
#include "vfs/source.h"

// The profile store's entry points (app/ui_api.cpp), as the page calls them.
extern "C" {
const char* cc_ui_state();
const char* cc_ui_select_driver(int i);
const char* cc_ui_new_driver(const char* name, int skill);
void cc_ui_import_begin();
void cc_ui_import_file(const char* path, const uint8_t* data, int size);
const char* cc_ui_import_finish();
void cc_ui_set_option(const char* name, double v);
int cc_ui_set_binding(int device, int action, int kind, int inputDevice, int component);
const char* cc_ui_options_done();
void cc_game_pause(int on);
}

#ifndef CC_VERSION
#define CC_VERSION "dev"
#endif

using namespace cc;

namespace {

const char* kTitle = "CrazyCity Insanity";
constexpr int kAudioChunk = 1024;   // frames per pull, as the page's script node
constexpr int kAudioTarget = 2048;  // frames kept queued (about 93 ms at 22,050 Hz)

// ---------------------------------------------------------------------------------------------
// Command line

struct Options {
    std::string mm2, configDir, dataDir;
    bool chooseMm2 = false, importPlayers = false;
    std::string city, mode = "cruise", car, driver, skill, list, device;
    std::vector<std::string> binds;
    int race = 1, paint = -1, opponents = -1;
    int tod = -1, weather = -1, traffic = -1, cops = -1, peds = -1;
    bool startGame = false;  // a game option was given: start it instead of showing the menus
    bool fullscreen = false, windowed = false, noVsync = false, noAudio = false, overlay = false, noPad = false;
    int width = 0, height = 0, msaa = -1, volume = 100;
    std::string audioDriver;
    bool verbose = false;
    std::string selftest;
    int frames = 240;
    double quitAfter = 0;
    bool help = false, version = false;
    // Network games.
    bool hostGame = false, lan = false, relay = false, offscreen = false;
    std::string join, net, name, password;
    int startWith = 2;
};

void usage() {
    std::printf(
        "CrazyCity Insanity %s (native)\n"
        "\n"
        "  crazycity [options]\n"
        "\n"
        "Game data\n"
        "  --mm2 DIR             your Midtown Madness 2 folder (the one with mm2core.ar); remembered\n"
        "  --choose-mm2          ask for the folder again\n"
        "  --import-players      import the MM2 folder's players/ again (replaces the drivers kept here)\n"
        "  --list WHAT           print cities, races, cars, drivers or controls and quit\n"
        "\n"
        "Starting a game straight away (skips the menus; for testing)\n"
        "  --city NAME|N         race folder (sf, london), map name or list number; default: the driver's last city\n"
        "  --mode MODE           cruise, checkpoint, circuit, blitz or crash (default cruise)\n"
        "  --race N              race number, from 1 (default 1)\n"
        "  --car NAME|N          car base name (vpbug) or list number; default: the driver's last car\n"
        "  --paint N             paint job, from 0\n"
        "  --driver NAME         play as this driver (made if missing)\n"
        "  --skill amateur|pro   the driver's skill level (a driver of that level is chosen or made)\n"
        "  --opponents N         Circuit opponents\n"
        "  --tod 0-3 --weather 0-3 --traffic 0-100 --cops 0-100 --peds 0-100\n"
        "                        Cruise's settings (in a race they are used where the race allows changes;\n"
        "                        the result is then not recorded)\n"
        "\n"
        "Controls (the driver's players/<name>.cfg, as MM2's controls screen sets them)\n"
        "  --device NAME         mouse, keyboard, joystick, gamepad or wheel\n"
        "  --bind DEVICE.ACTION=INPUT   e.g. wheel.throttle=axis:z  wheel.brake=axis:r  gamepad.horn=button:3\n"
        "                        keyboard.wide=key:b; inputs key:NAME, button:N, axis:x|y|z|r|u|v, half:x-|x+|y-|y+,\n"
        "                        hat, mouse:left|right|x|y, none; actions as --list controls shows them\n"
        "\n"
        "Window, sound, controllers\n"
        "  --fullscreen | --window   start in full screen or in a window (F11 or Alt+Enter switch)\n"
        "  --size WxH            window size\n"
        "  --no-vsync  --msaa N  --no-audio  --volume 0-100  --audio-driver NAME  --no-gamepad\n"
        "  --overlay             the debug overlay (speed, frame rate, problems; F1 switches it)\n"
        "\n"
        "Network games (with browsers and other native players)\n"
        "  --host-game           host a network game of --mode (cruise, checkpoint, circuit, blitz, cops),\n"
        "                        --city and --race on this computer; others join with the code it shows\n"
        "  --lan                 let other computers on the local network join (they find it by a LAN search)\n"
        "  --join CODE           join a network game by its code\n"
        "  --net ADDRESS         another session server (host:port, ws://host:8821/net or wss://...)\n"
        "  --name NAME           your name in network games (default: the driver's)\n"
        "  --password TEXT       the game's password (hosting) or the one to join with\n"
        "  --start-with N        the host starts once N players are ready (default 2; Enter starts at once)\n"
        "  --relay               no direct UDP to other native players (everything through the server)\n"
        "  --server [--port N] [--lan | --host ADDR] [--static DIR] [--quiet]\n"
        "                        only the session server, no window (what server/signal.js does); Ctrl+C stops\n"
        "  native.cfg: net_server, net_name, net_direct, net_udp_port, net_host_port, net_host_lan,\n"
        "              net_ca_file, net_verify_tls\n"
        "\n"
        "Folders and tests\n"
        "  --config-dir DIR      instead of ~/.config/crazycity-insanity/\n"
        "  --data-dir DIR        instead of ~/.local/share/crazycity-insanity/\n"
        "  --selftest PNG        offscreen: the main menu, a Cruise drive and a race, PNG captures, audio check;\n"
        "                        exit 1 on failure\n"
        "  --frames N            frames per part of the self-test (default 240)\n"
        "  --quit-after SECONDS  leave the game after that long (a quick look)\n"
        "  --verbose             every log line on the terminal\n"
        "  --offscreen           no window (a network player for tests; give --mm2)\n"
        "\n"
        "Keys: the driver's binding table (MM2's keyboard defaults: arrows drive, Space handbrake, C camera,\n"
        "D dash, W wide, T gearbox, A / Z shift, H gauges, Tab map; G puts the car back on its wheels),\n"
        "Esc pause menu, F2 pause, F4 restart; the menus take the mouse, the keyboard (arrows, Enter, Esc)\n"
        "and a game controller. F1 shows the debug overlay, Ctrl+Q quits.\n",
        CC_VERSION);
}

bool parseArgs(int argc, char** argv, Options& o, std::string& err) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& v) {
            if (i + 1 >= argc) {
                err = a + " needs a value";
                return false;
            }
            v = argv[++i];
            return true;
        };
        auto nextInt = [&](int& v) {
            std::string s;
            if (!next(s)) return false;
            char* end = nullptr;
            const long n = std::strtol(s.c_str(), &end, 10);
            if (!end || *end) {
                err = a + ": not a number: " + s;
                return false;
            }
            v = int(n);
            return true;
        };
        std::string s;
        if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--version") o.version = true;
        else if (a == "--mm2") { if (!next(o.mm2)) return false; }
        else if (a == "--choose-mm2") o.chooseMm2 = true;
        else if (a == "--import-players") o.importPlayers = true;
        else if (a == "--list") { if (!next(o.list)) return false; }
        else if (a == "--city") { if (!next(o.city)) return false; o.startGame = true; }
        else if (a == "--device") { if (!next(o.device)) return false; }
        else if (a == "--bind") {
            if (!next(s)) return false;
            o.binds.push_back(s);
        }
        else if (a == "--mode") { if (!next(o.mode)) return false; o.startGame = true; }
        else if (a == "--race") { if (!nextInt(o.race)) return false; o.startGame = true; }
        else if (a == "--car") { if (!next(o.car)) return false; o.startGame = true; }
        else if (a == "--paint") { if (!nextInt(o.paint)) return false; o.startGame = true; }
        else if (a == "--driver") { if (!next(o.driver)) return false; }
        else if (a == "--skill") { if (!next(o.skill)) return false; }
        else if (a == "--opponents") { if (!nextInt(o.opponents)) return false; o.startGame = true; }
        else if (a == "--tod") { if (!nextInt(o.tod)) return false; o.startGame = true; }
        else if (a == "--weather") { if (!nextInt(o.weather)) return false; o.startGame = true; }
        else if (a == "--traffic") { if (!nextInt(o.traffic)) return false; o.startGame = true; }
        else if (a == "--cops") { if (!nextInt(o.cops)) return false; o.startGame = true; }
        else if (a == "--peds") { if (!nextInt(o.peds)) return false; o.startGame = true; }
        else if (a == "--fullscreen") o.fullscreen = true;
        else if (a == "--window") o.windowed = true;
        else if (a == "--size") {
            if (!next(s)) return false;
            if (std::sscanf(s.c_str(), "%dx%d", &o.width, &o.height) != 2 || o.width < 320 || o.height < 200) {
                err = "--size wants WxH, at least 320x200";
                return false;
            }
        }
        else if (a == "--no-vsync") o.noVsync = true;
        else if (a == "--msaa") { if (!nextInt(o.msaa)) return false; }
        else if (a == "--no-audio") o.noAudio = true;
        else if (a == "--volume") { if (!nextInt(o.volume)) return false; }
        else if (a == "--audio-driver") { if (!next(o.audioDriver)) return false; }
        else if (a == "--no-gamepad") o.noPad = true;
        else if (a == "--overlay") o.overlay = true;
        else if (a == "--no-overlay") o.overlay = false;
        else if (a == "--config-dir") { if (!next(o.configDir)) return false; }
        else if (a == "--data-dir") { if (!next(o.dataDir)) return false; }
        else if (a == "--selftest") { if (!next(o.selftest)) return false; }
        else if (a == "--frames") { if (!nextInt(o.frames)) return false; }
        else if (a == "--quit-after") {
            if (!next(s)) return false;
            o.quitAfter = std::atof(s.c_str());
        }
        else if (a == "--verbose" || a == "-v") o.verbose = true;
        else if (a == "--host-game") o.hostGame = true;
        else if (a == "--lan") o.lan = true;
        else if (a == "--relay") o.relay = true;
        else if (a == "--offscreen") o.offscreen = true;
        else if (a == "--join") { if (!next(o.join)) return false; }
        else if (a == "--net") { if (!next(o.net)) return false; }
        else if (a == "--name") { if (!next(o.name)) return false; }
        else if (a == "--password") { if (!next(o.password)) return false; }
        else if (a == "--start-with") { if (!nextInt(o.startWith)) return false; }
        else {
            err = "unknown option " + a + " (see --help)";
            return false;
        }
    }
    return true;
}

int modeNumber(const std::string& m) {
    const std::string l = toLower(m);
    if (l == "cruise" || l == "roam" || l == "0") return 0;
    if (l == "checkpoint" || l == "race" || l == "1") return 1;
    if (l == "circuit" || l == "3") return 3;
    if (l == "blitz" || l == "4") return 4;
    if (l == "crash" || l == "crashcourse" || l == "crash-course" || l == "6") return 6;
    if (l == "cops" || l == "copsandrobbers" || l == "cops-and-robbers" || l == "2") return 2;  // network games only
    return -1;
}

// ---------------------------------------------------------------------------------------------
// The program's state

struct Native {
    bool saveOnExit = false;  // the main loop ran: save the window state and settings at the end
    Options opt;
    std::string configDir, dataDir, playersDir, exeDir, mm2;
    native::Settings cfg;
    Platform* platform = nullptr;
    NativeHost host;                  // the engine UI's hooks (files, dialogs, clipboard)
    std::vector<std::string> mods;     // mod archives added on top of the MM2 folder's
    bool pickPending = false;          // a file choice asked for by the menus, shown after the frame
    ui::Pick pick = ui::Pick::GameFolder;
    bool wasDriving = false;
    native::Overlay overlay;
    bool overlayReady = false, overlayOn = false;
    std::deque<std::string> problems;  // the last warnings and errors (overlay)
    std::string loading;                // "Loading …" while a city loads
    // The game the command line (or the self-test) starts.
    int cityIndex = -1, carIndex = -1, paint = 0, mode = 0, race = 0;
    std::string note;  // a line for the overlay (e.g. "not recorded")
    float noteTime = 0;
    // Frame capture (self-test).
    std::string capturePath;
    bool captureDone = false, captureOk = false;
    double captureMean = 0, captureSpread = 0;
    // Timing.
    double fpsTime = 0;
    int fpsFrames = 0;
    float fps = 0;
    // A network game (the session is the App's; this side hosts or joins and readies).
    bool net = false, netHosting = false, netSettingsSent = false, netReady = false, netClosedSaid = false;
    int skill = 0;
    double netReported = 0;
};
Native* g_native = nullptr;

void printLog(int level, const std::string& kind, const std::string& msg) {
    Native* n = g_native;
    const bool verbose = n && n->opt.verbose;
    if (level >= 1 || verbose) std::fprintf(stderr, "%s%s%s\n", level >= 2 ? "error: " : (level == 1 ? "warning: " : ""),
                                            kind.empty() ? "" : (kind + ": ").c_str(), msg.c_str());
    if (n && level >= 1) {
        n->problems.push_back(msg);
        while (n->problems.size() > 4) n->problems.pop_front();
    }
}

fe::FrontEnd* frontEnd() { return theApp().frontEnd(); }

// ---------------------------------------------------------------------------------------------
// Data

bool isMm2Folder(const std::string& dir) { return !dir.empty() && native::isDir(dir) && !native::findNoCase(dir, "mm2core.ar").empty(); }

// The MM2 folder: the command line, the remembered one, else the folder dialog (asked once, then
// remembered).
std::string findMm2Folder(Native& n) {
    if (!n.opt.mm2.empty()) {
        const std::string d = native::absolutePath(n.opt.mm2);
        if (isMm2Folder(d)) return d;
        n.platform->message(kTitle, "There is no mm2core.ar in " + d + ".\nGive the folder Midtown Madness 2 is installed in.", true);
        return std::string();
    }
    const std::string remembered = n.cfg.get("mm2_folder");
    if (!n.opt.chooseMm2 && isMm2Folder(remembered)) return remembered;
    if (n.platform->offscreen()) return std::string();
    std::string start = remembered.empty() ? native::homeDir() : remembered;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const std::string d = n.platform->chooseFolder("Your Midtown Madness 2 folder (the one with mm2core.ar)", start);
        if (d.empty()) return std::string();
        if (isMm2Folder(d)) return native::absolutePath(d);
        n.platform->message(kTitle, "There is no mm2core.ar in " + d + ".\nPick the folder Midtown Madness 2 is installed in.", true);
        start = d;
    }
    return std::string();
}

// Every .ar of the folder (MM2 reads the archives in its own folder), read from disk as needed.
bool mountFolder(Native& n) {
    App& app = theApp();
    app.clearArchives();
    int count = 0;
    for (const std::string& f : native::listDir(n.mm2, true, false)) {
        if (!iendsWith(f, ".ar")) continue;
        std::string err;
        auto src = openFileSource(native::joinPath(n.mm2, f), &err);
        if (!src) {
            logWarn("%s", err.c_str());
            continue;
        }
        app.addArchive(f, src);
        ++count;
    }
    // Mods added from the viewer's data panel, on top (the engine decides the order).
    for (const std::string& path : n.mods) {
        std::string err;
        auto src = openFileSource(path, &err);
        if (!src) {
            logWarn("%s", err.c_str());
            continue;
        }
        const size_t slash = path.find_last_of('/');
        app.addArchive(slash == std::string::npos ? path : path.substr(slash + 1), src);
        ++count;
    }
    const JsonValue s = parseJson(app.mount());
    std::fprintf(stderr, "%s: %d archives, %d files, %d vehicles, %d cities\n", n.mm2.c_str(), count, s["files"].integer(),
                 s["cars"].integer(), s["cities"].integer());
    return s["cars"].integer() > 0 && s["cities"].integer() > 0;
}

void loadGmBanks(Native& n) {
    App& app = theApp();
    // The bundled drum kit and piano (FluidR3, MIT) next to the program.
    for (const std::string& p : {n.exeDir + "gm/FluidR3_GM_drums_piano.sf2", n.exeDir + "../share/crazycity-insanity/gm/FluidR3_GM_drums_piano.sf2"}) {
        native::Bytes b;
        if (native::readFile(p, b)) {
            app.setMusicGmBank("FluidR3_GM_drums_piano.sf2", Bytes(b.begin(), b.end()), true);
            break;
        }
    }
    // The player's own gm.dls in the MM2 folder (music.md, "General MIDI").
    const std::string gm = native::findNoCase(n.mm2, "gm.dls");
    if (!gm.empty()) {
        native::Bytes b;
        if (native::readFile(native::joinPath(n.mm2, gm), b)) app.setMusicGmBank(gm, Bytes(b.begin(), b.end()), false);
    }
}

// The player's mmlang.dll (MM2's words), before the archives are mounted.
void loadStrings(Native& n) {
    const std::string dll = native::findNoCase(n.mm2, "mmlang.dll");
    if (dll.empty() || !frontEnd()) return;
    native::Bytes b;
    if (native::readFile(native::joinPath(n.mm2, dll), b)) frontEnd()->loadStrings(b.data(), b.size(), false);
}

// MM2's players folder in `dir` (an MM2 folder or the players folder itself); "" if none.
std::string playersFolderOf(const std::string& dir) {
    if (dir.empty() || !native::isDir(dir)) return std::string();
    if (!native::findNoCase(dir, "players.dir").empty()) return dir;
    const std::string pd = native::findNoCase(dir, "players");
    if (pd.empty()) return std::string();
    const std::string sub = native::joinPath(dir, pd);
    return native::findNoCase(sub, "players.dir").empty() ? std::string() : sub;
}

// The drivers of MM2's players folder `dir` imported into the profile store (which writes them to
// <data>/players/). `report`: the menus show what was imported (the player asked); else a line on
// the terminal (the first start imports the MM2 folder's drivers without asking).
bool importPlayers(Native& n, const std::string& dir, bool report) {
    cc_ui_import_begin();
    int files = 0;
    if (!dir.empty())
        for (const std::string& rel : native::listFilesRecursive(dir)) {
            native::Bytes b;
            if (!native::readFile(native::joinPath(dir, rel), b) || b.size() > 4u * 1048576u) continue;
            cc_ui_import_file(rel.c_str(), b.data(), int(b.size()));
            ++files;
        }
    if (report && frontEnd()) {
        frontEnd()->importFinished();
        return true;
    }
    const JsonValue r = parseJson(cc_ui_import_finish());
    std::fprintf(stderr, "imported %d drivers from %s (%d files)\n", r["drivers"].integer(), dir.c_str(), files);
    for (const JsonValue& note : r["notes"].items) std::fprintf(stderr, "  %s\n", note.str().c_str());
    if (frontEnd()) frontEnd()->profilesChanged();
    return r["ok"].boolean();
}

int findDriver(const JsonValue& state, const std::string& name) {
    const JsonValue& d = state["drivers"];
    for (size_t i = 0; i < d.size(); ++i)
        if (iequals(d.at(i).str(), name)) return int(i);
    return -1;
}

// --driver and --skill: the driver to play as (selected, or made).
JsonValue chooseDriver(Native& n, JsonValue state) {
    int wantSkill = -1;
    if (!n.opt.skill.empty()) {
        const std::string s = toLower(n.opt.skill);
        wantSkill = (s == "pro" || s == "professional" || s == "1") ? 1 : 0;
    }
    auto select = [&](int i) { state = parseJson(cc_ui_select_driver(i))["state"]; };
    auto make = [&](const std::string& name, int skill) {
        const JsonValue r = parseJson(cc_ui_new_driver(name.c_str(), skill));
        if (!r["ok"].boolean()) logWarn("driver %s not made (%s)", name.c_str(), r["why"].str().c_str());
        else std::fprintf(stderr, "new driver %s (%s)\n", name.c_str(), skill ? "professional" : "amateur");
        state = r["state"];
    };
    if (!n.opt.driver.empty()) {
        const int i = findDriver(state, n.opt.driver);
        if (i >= 0) select(i);
        else make(n.opt.driver, wantSkill < 0 ? 0 : wantSkill);
    }
    if (wantSkill >= 0 && state["driver"]["skill"].integer(-1) != wantSkill) {
        const std::string name = wantSkill ? "Professional" : "Amateur";
        const int i = findDriver(state, name);
        if (i >= 0) select(i);
        if (state["driver"]["skill"].integer(-1) != wantSkill) make(i >= 0 ? name + " 2" : name, wantSkill);
    }
    return state;
}

int findCity(const std::string& want, const std::string& fallbackRaceDir) {
    const auto& list = theApp().cityView().cities();
    auto byName = [&](const std::string& w) {
        for (size_t i = 0; i < list.size(); ++i)
            if (iequals(list[i].raceDir, w) || iequals(list[i].mapName, w) || iequals(list[i].localizedName, w)) return int(i);
        return -1;
    };
    if (!want.empty()) {
        char* end = nullptr;
        const long k = std::strtol(want.c_str(), &end, 10);
        if (end && !*end && k >= 1 && size_t(k) <= list.size()) return int(k - 1);
        return byName(want);
    }
    int i = byName(fallbackRaceDir);
    if (i < 0)
        for (size_t k = 0; k < list.size() && i < 0; ++k)
            if (!list[k].testLayout) i = int(k);
    return i;
}

int findCar(const std::string& want, const std::string& fallback) {
    const auto& cars = theApp().cars();
    auto byName = [&](const std::string& w) {
        for (size_t i = 0; i < cars.size(); ++i)
            if (iequals(cars[i].baseName, w)) return int(i);
        return -1;
    };
    if (!want.empty()) {
        char* end = nullptr;
        const long k = std::strtol(want.c_str(), &end, 10);
        if (end && !*end && k >= 1 && size_t(k) <= cars.size()) return int(k - 1);
        return byName(want);
    }
    int i = byName(fallback);
    if (i < 0) i = byName("vpbug");
    if (i < 0 && !cars.empty()) i = 0;
    return i;
}

// --device and --bind: the current driver's controls, written into its .cfg.
bool applyControlOptions(Native& n, JsonValue& state) {
    if (n.opt.device.empty() && n.opt.binds.empty()) return true;
    if (!n.opt.device.empty()) {
        const int d = native::deviceNumber(n.opt.device);
        if (d < 0) {
            std::fprintf(stderr, "no control device %s (mouse, keyboard, joystick, gamepad, wheel)\n", n.opt.device.c_str());
            return false;
        }
        cc_ui_set_option("controlDevice", d);
    }
    for (const std::string& b : n.opt.binds) {
        const size_t dot = b.find('.'), eq = b.find('=');
        const int d = dot == std::string::npos ? -1 : native::deviceNumber(b.substr(0, dot));
        const int a = dot == std::string::npos || eq == std::string::npos || eq < dot ? -1 : native::actionNumber(b.substr(dot + 1, eq - dot - 1));
        native::BindingText t;
        std::string why = "write DEVICE.ACTION=INPUT";
        if (dot != std::string::npos && d < 0) why = "no device " + b.substr(0, dot);
        else if (dot != std::string::npos && eq != std::string::npos && a < 0) why = "no action " + b.substr(dot + 1, eq - dot - 1);
        if (d < 0 || a < 0 || !native::parseBinding(b.substr(eq + 1), a, t, why)) {
            std::fprintf(stderr, "--bind %s: %s (see --list controls)\n", b.c_str(), why.c_str());
            return false;
        }
        cc_ui_set_binding(d, a, int(t.kind), int(t.device), int(t.component));
    }
    state = parseJson(cc_ui_options_done());
    std::fprintf(stderr, "controls of %s saved\n", state["driver"]["name"].str("the driver").c_str());
    return true;
}

void listThings(const std::string& what, const JsonValue& state) {
    App& app = theApp();
    const std::string w = toLower(what);
    if (w == "cities" || w == "races") {
        const auto& list = app.cityView().cities();
        for (size_t i = 0; i < list.size(); ++i) {
            const CityEntry& c = list[i];
            std::printf("%2d  %-10s %-12s %s%s\n", int(i + 1), c.raceDir.c_str(), c.mapName.c_str(),
                        latin1ToUtf8(c.localizedName).c_str(), c.testLayout ? "  (test layout)" : "");
            if (w != "races" || c.testLayout) continue;
            for (game::RaceMode m : {game::RaceMode::Checkpoint, game::RaceMode::Circuit, game::RaceMode::Blitz, game::RaceMode::CrashCourse}) {
                const int k = game::raceCount(c, m);
                for (int r = 0; r < k; ++r)
                    std::printf("      --mode %-10s --race %-2d %s\n", game::modeKey(m) == std::string("race") ? "checkpoint" : game::modeKey(m),
                                r + 1, latin1ToUtf8(game::raceDisplayName(c, m, r)).c_str());
            }
        }
    } else if (w == "cars") {
        const auto& cars = app.cars();
        for (size_t i = 0; i < cars.size(); ++i) {
            const CarEntry& c = cars[i];
            if (c.group != CarEntry::Menu) continue;
            std::printf("%3d  %-14s %s (%d paints)%s\n", int(i + 1), c.baseName.c_str(), latin1ToUtf8(c.description).c_str(),
                        int(c.colors.size()), c.fromMod ? "  [mod]" : "");
        }
    } else if (w == "controls") {
        const JsonValue& o = state["options"];
        const int dev = o["controlDevice"].integer(1);
        std::printf("%s: control device %s, dead zone %.2f, steering sensitivity %.2f, force feedback %s\n",
                    state["driver"]["name"].str("no driver").c_str(), native::deviceName(dev), o["deadZone"].num(),
                    o["steeringSensitivity"].num(), o["forceFeedback"].boolean() ? "on" : "off");
        const JsonValue& t = o["bindings"];
        for (int d = 0; d < 5; ++d) {
            std::printf("\n%s%s\n", native::deviceName(d), d == dev ? "  (in use)" : "");
            for (int a = 0; a < 34; ++a) {
                const JsonValue& e = t.at(size_t(d * 34 + a));
                std::printf("  %s.%-15s %s\n", native::deviceName(d), native::actionName(a),
                            native::bindingText(uint32_t(e.at(0).integer()), uint32_t(e.at(1).integer()), uint32_t(e.at(2).integer())).c_str());
            }
        }
    } else if (w == "drivers") {
        const JsonValue& d = state["drivers"];
        for (size_t i = 0; i < d.size(); ++i)
            std::printf("%2d  %s%s\n", int(i + 1), d.at(i).str().c_str(), int(i) == state["current"].integer(-1) ? "  (current)" : "");
    } else {
        std::fprintf(stderr, "--list cities, races, cars, drivers or controls\n");
    }
}

// ---------------------------------------------------------------------------------------------
// Drawing: the overlay and the frame capture, just before the frame is shown.

std::string ascii(const std::string& s) {
    // UTF-8 to what the overlay font has: Latin-1 letters lose their accents, the rest is '?'.
    static const char* fold = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYPsaaaaaaaceeeeiiiidnooooo/ouuuuypy";
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x80) {
            o += char(c);
        } else if ((c == 0xC3) && i + 1 < s.size()) {
            const unsigned char d = (unsigned char)s[++i];
            const int k = int(d) - 0x80;
            o += k >= 0 && k < 64 ? fold[k] : '?';
        } else if ((c & 0xE0) == 0xC0) {
            ++i;
            o += '?';
        } else if ((c & 0xF0) == 0xE0) {
            i += 2;
            o += '?';
        } else if ((c & 0xF8) == 0xF0) {
            i += 3;
            o += '?';
        }
    }
    return o;
}

// ---------------------------------------------------------------------------------------------
// A network game

// The overlay's network lines: the connection, the code, the players and how each is reached.
std::vector<std::string> netLines(Native& n) {
    std::vector<std::string> out;
    App& app = theApp();
    net::Session* s = app.netSession();
    net::NativeTransport* t = app.nativeTransport();
    if (!s || !t) return out;
    std::string l = std::string("Network: ") + t->stateName();
    if (!t->code().empty()) l += "  |  code " + t->code();
    if (s->open()) l += strf("  |  %d player%s%s", s->playerCount(), s->playerCount() == 1 ? "" : "s", s->isHost() ? "  |  host" : "");
    if (s->closed()) l += "  |  " + s->closedReason();
    else if (t->state() == net::NativeTransport::State::Failed) l += "  |  " + t->error();
    out.push_back(ascii(l));
    if (net::HostServer* hs = app.netLocalServer()) {
        const std::vector<std::string> urls = hs->urls();
        if (urls.size() > 1) out.push_back("  others join at " + urls.front());
    }
    const std::vector<net::PeerLink> links = t->links();
    for (const net::Player& p : s->players()) {
        std::string r = "  " + ascii(p.rec.name) + "  " + p.rec.car;
        if (p.id == s->host()) r += "  host";
        if (p.id == s->self()) r += "  (you)";
        else if (p.rec.inGame) r += "  in game";
        else if (p.rec.ready) r += "  ready";
        for (const net::PeerLink& k : links)
            if (k.id == p.id)
                r += k.direct ? strf("  direct %.0f ms", k.rtt >= 0 ? k.rtt * 1000.0 : 0.0) : std::string(k.native ? "  relayed" : "  browser, relayed");
        out.push_back(r);
    }
    return out;
}

// Joins or hosts as the command line says: the transport's settings from native.cfg and the
// command line, the engine's own server on this computer when hosting without --net.
bool startNetwork(Native& n, const JsonValue& state, std::string& why) {
    App& app = theApp();
    if (n.mode == 6) {
        why = "Crash Course is not a network game (cruise, checkpoint, circuit, blitz or cops)";
        return false;
    }
    net::NativeNetConfig cfg;
    cfg.server = net::serverUrl(!n.opt.net.empty() ? n.opt.net : n.cfg.get("net_server", "ws://127.0.0.1:8821/net"));
    cfg.direct = !n.opt.relay && n.cfg.getBool("net_direct", true);
    cfg.link.port = uint16_t(std::clamp(n.cfg.getInt("net_udp_port", 0), 0, 65535));
    cfg.ws.caFile = n.cfg.get("net_ca_file");
    cfg.ws.verifyTls = n.cfg.getBool("net_verify_tls", true);
    app.setNetConfig(cfg);
    if (n.opt.hostGame && n.opt.net.empty()) {
        const int port = std::clamp(n.cfg.getInt("net_host_port", int(net::kDefaultNetPort)), 0, 65535);
        const bool lan = n.opt.lan || n.cfg.getBool("net_host_lan", false);
        std::string err;
        if (!app.netStartLocalServer(uint16_t(port), lan, &err)) {
            why = "cannot host on this computer: " + err;
            return false;
        }
        for (const std::string& u : app.netLocalServer()->urls()) std::fprintf(stderr, "network: the session server takes players at %s\n", u.c_str());
    }
    std::string name = n.opt.name;
    if (name.empty()) name = n.cfg.get("net_name");
    if (name.empty()) name = state["driver"]["name"].str("Player");
    const auto& cars = app.cars();
    const std::string car = n.carIndex >= 0 && size_t(n.carIndex) < cars.size() ? cars[size_t(n.carIndex)].baseName : std::string("vpbug");
    n.skill = state["driver"]["skill"].integer(0);
    app.netBegin(name, car, n.paint);
    net::NativeTransport* t = app.nativeTransport();
    if (!t) {
        why = "no network transport";
        return false;
    }
    if (n.opt.hostGame) {
        net::SessionDesc d;
        d.name = name + "'s game";
        d.password = n.opt.password;
        d.version = net::kGameVersion;
        t->host(d);
    } else {
        t->join(n.opt.join, n.opt.password, net::kGameVersion);
    }
    n.net = true;
    n.netHosting = n.opt.hostGame;
    std::fprintf(stderr, "network: %s on %s as %s\n", n.opt.hostGame ? "hosting" : ("joining " + n.opt.join).c_str(),
                 app.netConfig().server.c_str(), name.c_str());
    return true;
}

// After each frame: the host's settings once the session is open, the joiner's ready (or a join of
// a running Cruise or Cops and Robbers game), the host's start once enough players are ready.
void netStep(Native& n, bool startNow) {
    App& app = theApp();
    net::Session* s = app.netSession();
    net::NativeTransport* t = app.nativeTransport();
    if (!s || !t) return;
    if (s->closed() || (!s->open() && t->state() == net::NativeTransport::State::Failed)) {
        if (!n.netClosedSaid) {
            std::fprintf(stderr, "network: out of the game (%s)\n", s->closed() ? s->closedReason().c_str() : t->error().c_str());
            n.netClosedSaid = true;
        }
        return;
    }
    if (!s->open()) return;
    if (n.netHosting && !n.netSettingsSent) {
        const auto& cities = app.cityView().cities();
        const std::string raceDir = n.cityIndex >= 0 && size_t(n.cityIndex) < cities.size() ? cities[size_t(n.cityIndex)].raceDir : "sf";
        s->setSettings(net::hostDefaults(app.vfs(), n.mode, raceDir, n.race, n.skill));
        net::SessionDesc d = s->description();
        d.name = s->localRecord().name + "'s game";
        d.password = n.opt.password;
        d.version = net::kGameVersion;
        s->describe(d);
        n.netSettingsSent = true;
        std::fprintf(stderr, "network: hosting, code %s (a browser joins with ?join=%s)\n", t->code().c_str(), t->code().c_str());
    }
    if (!n.netHosting) {
        if (!s->game() && s->canJoinRunningGame()) s->joinRunningGame();
        if (!n.netReady && s->haveSettings()) {
            s->setReady(true);
            n.netReady = true;
        }
    }
    if (n.netHosting && !s->game() && s->canStart() && (startNow || s->playerCount() >= std::max(2, n.opt.startWith))) s->start();
    // --verbose: what this machine sees of the others, every 5 s.
    const double now = app.clock();
    if (n.opt.verbose && s->game() && s->game()->attached() && now - n.netReported >= 5.0) {
        n.netReported = now;
        for (const net::RemoteCar* rc : s->game()->remoteCars()) {
            const vec3 p = rc->car.body().p;
            std::fprintf(stderr, "network: the car of %s (%s) %s at (%.1f, %.1f, %.1f), %.1f m/s\n", s->nameOf(rc->id).c_str(),
                         rc->carName.c_str(), rc->shown ? "shown" : "not shown", double(p.x), double(p.y), double(p.z), double(rc->car.speed()));
        }
    }
}

void drawOverlay(Native& n, int w, int h) {
    if (!n.overlayReady) return;
    App& app = theApp();
    native::Overlay& ov = n.overlay;
    const int sc = native::Overlay::scaleFor(h);
    ov.begin(w, h);
    const float margin = float(8 * sc);
    if (!n.loading.empty()) {
        std::vector<std::string> l = {ascii(n.loading)};
        const float tw = float(l[0].size() * native::Overlay::kCellW * sc * 2);
        ov.panel(float(w) * 0.5f - tw * 0.5f, float(h) * 0.5f, l, 0xFFFFFFFFu, sc * 2);
        ov.end();
        return;
    }
    if (!n.overlayOn) {
        ov.end();
        return;
    }
    fe::FrontEnd* fe = frontEnd();
    DriveView& dv = app.driveView();
    std::vector<std::string> top;
    const JsonValue hud = dv.active() ? parseJson(dv.hudJson(w, h)) : JsonValue();
    const JsonValue race = dv.racing() ? parseJson(dv.raceJson()) : JsonValue();
    if (dv.active()) {
        std::string l = strf("%3d mph  %s %s", hud["mph"].integer(), hud["gear"].str().c_str(), hud["automatic"].boolean() ? "auto" : "manual");
        l += strf("  damage %d%%", int(hud["damage"].num() * 100.0 + 0.5));
        top.push_back(l);
        std::string r;
        if (hud.has("timer")) r += "time " + hud["timer"].str() + "  ";
        if (hud["place"].size() == 2) r += strf("place %d/%d  ", hud["place"].at(0).integer(), hud["place"].at(1).integer());
        if (hud["checkpoints"].size() == 2) r += strf("checkpoints %d/%d  ", hud["checkpoints"].at(0).integer(), hud["checkpoints"].at(1).integer());
        if (hud["lap"].size() == 2) r += strf("lap %d/%d  ", hud["lap"].at(0).integer(), hud["lap"].at(1).integer());
        if (race.has("countdown")) r += "start in " + race["countdown"].str() + "  ";
        if (!r.empty()) top.push_back(r);
        for (const char* k : {"primary", "secondary"}) {
            const JsonValue& m = hud[k];
            if (m.isNull()) continue;
            std::string t = m["text"].str();
            if (t.empty() && m["id"].integer(-1) >= 0) t = game::fallbackText(m["id"].integer());
            if (!t.empty()) top.push_back(ascii(t));
        }
        if (race.has("msg") && !race["msg"].str().empty()) {
            const std::string m = ascii(race["msg"].str());
            bool shown = false;
            for (const std::string& l : top) shown = shown || l == m || l.rfind(m, 0) == 0;
            if (!shown) top.push_back(m);
        }
    }
    if (!top.empty()) ov.panel(margin, margin, top, 0xFFFFFFFFu, sc);

    // The bottom-left lines: what is running, the keys, problems.
    std::vector<std::string> info;
    {
        std::string what = dv.racing() ? ascii(race["title"].str("Race")) : (dv.active() ? "Cruise" : "Menus: " + (fe ? fe->screenName() : std::string("-")));
        if (app.cityView().city()) what += "  |  " + ascii(app.cityView().city()->name());
        info.push_back(what);
    }
    const std::string pad = n.platform->controllerName();
    info.push_back(strf("%.0f fps  |  %s%s", double(n.fps), pad.empty() ? "keyboard" : ascii(pad).c_str(),
                        n.platform->audioOpen() ? "" : "  |  no sound"));
    if (n.noteTime > 0 && !n.note.empty()) info.push_back(ascii(n.note));
    for (const std::string& p : n.problems) info.push_back("! " + ascii(p));
    if (n.net) {
        for (const std::string& l : netLines(n)) info.push_back(l);
        info.push_back("F1 overlay  F11 full screen  Enter ready / start  Ctrl+Q quit");
    } else {
        info.push_back("F1 overlay  F11 full screen  Esc pause  F4 restart  Ctrl+Q quit");
    }
    const float ih = float(info.size()) * native::Overlay::kCellH * sc + 8 * sc;
    ov.panel(margin, float(h) - ih - margin, info, 0xFFE0E0E0u, sc, 0x80000000u);
    ov.end();
}

void beforeSwap(int w, int h) {
    Native& n = *g_native;
    drawOverlay(n, w, h);
    if (!n.capturePath.empty() && !n.captureDone) {
        std::vector<unsigned char> px(size_t(w) * size_t(h) * 4);
        n.captureDone = true;
        if (n.platform->readPixels(w, h, px.data())) {
            n.captureOk = native::writePng(n.capturePath, w, h, px.data(), true);
            // How much the picture varies (a blank or one-colour frame fails the self-test).
            double sum = 0, sum2 = 0;
            const size_t count = size_t(w) * size_t(h);
            for (size_t i = 0; i < count; ++i) {
                const double l = 0.299 * px[4 * i] + 0.587 * px[4 * i + 1] + 0.114 * px[4 * i + 2];
                sum += l;
                sum2 += l * l;
            }
            n.captureMean = sum / double(count);
            n.captureSpread = std::sqrt(std::max(0.0, sum2 / double(count) - n.captureMean * n.captureMean));
        }
    }
}

// One frame on the screen with only the overlay (the loading line) while the program is busy.
void showBusy(Native& n, const std::string& text) {
    n.loading = text;
    int w = 1, h = 1;
    n.platform->drawableSize(w, h);
    glViewport(0, 0, w, h);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!n.platform->offscreen()) n.platform->swap();
    n.loading.clear();
}

// ---------------------------------------------------------------------------------------------
// A game

// The driver's controller options when a drive starts (the dead zone, the steering sensitivity,
// the force feedback); the front end sets the rest (graphics, sound) as the page does.
void applyControllerOptions(Native& n) {
    const JsonValue state = parseJson(cc_ui_state());
    const JsonValue& o = state["options"];
    if (o.isNull()) return;
    const float sens = float(o["steeringSensitivity"].num(1));
    n.platform->setControllerOptions(float(o["deadZone"].num(0.1)), sens > 0.0f ? 1.0f / sens : 1.0f);
}

// The command line's game, started through the front end as its Go button would (its loading
// screen, then the game with its HUD, pause menu and results). The command line's settings
// replace the race menu's (in a race: not recorded then).
void requestGame(Native& n) {
    fe::FrontEnd* fe = frontEnd();
    if (!fe) return;
    const Options& o = n.opt;
    fe::FrontEnd::PlayEnv env;
    env.tod = o.tod;
    env.weather = o.weather;
    env.traffic = o.traffic >= 0 ? o.traffic / 100.0 : -1;
    env.cops = o.cops >= 0 ? o.cops / 100.0 : -1;
    env.peds = o.peds >= 0 ? o.peds / 100.0 : -1;
    env.opponents = o.opponents;
    if (!fe->inPlay()) fe->openFrontEnd();
    fe->testPlay(n.cityIndex, n.mode, n.race, n.carIndex, n.paint, &env);
}

// The self-test's game: started, then frames until the front end has begun it.
bool startGame(Native& n, std::string& why) {
    App& app = theApp();
    fe::FrontEnd* fe = frontEnd();
    if (!fe) {
        why = "no front end";
        return false;
    }
    requestGame(n);
    // Real frames (the controllers are read in them): the drive learns a pedal's resting end from
    // its first frame.
    const double t0 = n.platform->timeSeconds();
    while (!fe->inGame() && n.platform->timeSeconds() - t0 < 30.0) app.frame();
    if (!fe->inGame()) {
        why = "the game did not start (" + fe->screenName() + ")";
        return false;
    }
    applyControllerOptions(n);
    return true;
}

// Leaves the running game for the menus (the view bytes go back into the driver's .cfg).
void endGame(Native& n) {
    fe::FrontEnd* fe = frontEnd();
    if (fe && fe->inGame() && !theApp().driveView().netActive()) fe->testQuit("race");
}

void saveWindowState(Native& n) {
    if (n.platform->offscreen()) return;
    const Platform::WindowState w = n.platform->windowState();
    n.cfg.setInt("window_width", w.w);
    n.cfg.setInt("window_height", w.h);
    n.cfg.setBool("fullscreen", w.fullscreen);
    n.cfg.setBool("maximized", w.maximized);
}

// The program's own keys, after each frame (the menus take the rest).
bool handleKeys(Native& n) {
    const InputState* in = n.platform->lastInput();
    if (!in) return true;
    if (in->pressed[KeyF1]) n.overlayOn = !n.overlayOn;
    if (in->down[KeyCtrl] && in->pressed[KeyQ]) return false;
    // A command-line network game (--host-game / --join): Enter in its lobby starts (the host) or
    // readies (a guest) while no text field has the keyboard.
    App& app = theApp();
    if (n.net && !app.driveView().netActive() && in->pressed[KeyEnter] && !app.ui().wantsKeyboard()) {
        if (net::Session* s = app.netSession()) {
            if (s->isHost()) netStep(n, true);
            else if (s->open()) s->setReady(!s->localRecord().ready);
        }
    }
    return true;
}

// A file choice the menus asked for (ui/host.h Pick), shown between frames: the system's dialog,
// then the data loaded as the page loads it.
void handlePick(Native& n, ui::Pick what) {
    App& app = theApp();
    fe::FrontEnd* fe = frontEnd();
    switch (what) {
        case ui::Pick::GameFolder: {
            std::string d = n.platform->chooseFolder("Your Midtown Madness 2 folder (the one with mm2core.ar)", n.mm2);
            if (d.empty()) return;
            d = native::absolutePath(d);
            if (!isMm2Folder(d)) {
                n.platform->message(kTitle, "There is no mm2core.ar in " + d + ".\nPick the folder Midtown Madness 2 is installed in.", true);
                return;
            }
            n.mm2 = d;
            n.cfg.set("mm2_folder", d);
            n.cfg.save();
            loadStrings(n);
            if (!mountFolder(n)) n.platform->message(kTitle, "The game data in " + d + " could not be read (no cars or cities).", true);
            loadGmBanks(n);
            // Its players folder: offered for import when the menus open (as in the browser).
            if (fe && !playersFolderOf(d).empty()) fe->setPendingPlayersFolder(true);
            return;
        }
        case ui::Pick::ModArchives: {
            const std::vector<std::string> files = n.platform->chooseFiles("Mod archives to add", n.mm2, "MM2 archives (.ar)", "ar", true);
            if (files.empty()) return;
            for (const std::string& f : files)
                if (std::find(n.mods.begin(), n.mods.end(), f) == n.mods.end()) n.mods.push_back(f);
            mountFolder(n);
            return;
        }
        case ui::Pick::PlayersFolder: {
            const std::string d = n.platform->chooseFolder("Your Midtown Madness 2 folder, or its players folder", n.mm2);
            if (!d.empty()) importPlayers(n, playersFolderOf(native::absolutePath(d)), true);
            return;
        }
        case ui::Pick::HeldPlayers: importPlayers(n, playersFolderOf(n.mm2), true); return;
        case ui::Pick::GmBank: {
            const std::vector<std::string> files =
                n.platform->chooseFiles("A General MIDI bank", n.mm2, "gm.dls or SoundFont 2", "dls;sf2", false);
            if (files.empty()) return;
            native::Bytes b;
            if (!native::readFile(files[0], b)) {
                logWarn("could not read %s", files[0].c_str());
                return;
            }
            const size_t slash = files[0].find_last_of('/');
            app.setMusicGmBank(slash == std::string::npos ? files[0] : files[0].substr(slash + 1), Bytes(b.begin(), b.end()), false);
            return;
        }
        default: return;
    }
}

// ---------------------------------------------------------------------------------------------
// The self-test: offscreen, Cruise and a race, a PNG of each, the audio output.

struct AudioCheck {
    long frames = 0;
    double sum2 = 0;
    float peak = 0;
};

bool selfTestCapture(Native& n, const std::string& path, int frames, AudioCheck& audio, std::string& report) {
    App& app = theApp();
    // Frames of 1/60 s, the audio pulled after each as the real loop pulls it; the last frame is
    // captured.
    for (int i = 0; i < frames; ++i) {
        if (i == frames - 1) {
            n.capturePath = path;
            n.captureDone = n.captureOk = false;
        }
        app.stepFrames(1, 1.0f / 60.0f);
        n.platform->feedAudio(
            [&](float* out, int count) {
                app.renderAudio(out, count);
                for (int k = 0; k < 2 * count; ++k) {
                    audio.sum2 += double(out[k]) * out[k];
                    audio.peak = std::max(audio.peak, std::fabs(out[k]));
                }
                audio.frames += count;
            },
            kAudioChunk, kAudioTarget);
    }
    n.capturePath.clear();
    const bool varied = n.captureOk && n.captureSpread > 8.0;
    report += strf("  %s: %s, mean %.1f, spread %.1f%s\n", path.c_str(), n.captureOk ? "written" : "NOT WRITTEN", n.captureMean,
                   n.captureSpread, varied ? "" : "  (FLAT: nothing drawn?)");
    return varied;
}

// Controls through the driver's binding table (controls.md) and every controller merged into one
// (the shared game goals), with SDL's virtual joystick driver (no real device is touched; force
// feedback recorded, not played) and injected key and mouse events: two gamepads from the start
// (one silent), one unplugged mid-game, the keyboard beside the gamepad device, a force-feedback
// wheel plugged in mid-game with separate pedals, a second force-feedback wheel, the joystick's
// approach mode, the mouse device, the keyboard device. Real-time frames, so the platform polls
// the devices and hears them come and go.
void selfTestControllers(Native& n, const std::function<void(bool, const std::string&)>& check) {
    App& app = theApp();
    Platform& p = *n.platform;
    auto drive = [&](double seconds) {
        const double t0 = p.timeSeconds();
        while (p.timeSeconds() - t0 < seconds) {
            app.frame();
            p.feedAudio([&](float* out, int count) { app.renderAudio(out, count); }, kAudioChunk, kAudioTarget);
        }
        return parseJson(app.driveView().debugJson());
    };
    auto restart = [&]() {
        endGame(n);
        std::string why;
        return startGame(n, why);
    };
    auto tagsWith = [](const std::vector<std::string>& log, const std::string& command) {
        std::vector<std::string> tags;
        for (const std::string& l : log) {
            const size_t sp = l.find(' ');
            if (sp == std::string::npos || l.compare(sp + 1, command.size(), command) != 0) continue;
            const std::string t = l.substr(0, sp);
            if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
        }
        return tags;
    };
    auto firstValue = [](const std::vector<std::string>& log, const std::string& command) {
        for (const std::string& l : log) {
            const size_t at = l.find(" " + command + " ");
            if (at != std::string::npos) return std::atoi(l.c_str() + at + command.size() + 2);
        }
        return -1;
    };
    cc_ui_set_option("forceFeedback", 1);

    // Two gamepads from the start, A silent: B drives (button 1, the stick); a gamepad gets no
    // force feedback (MM2 has none).
    cc_ui_set_option("controlDevice", 3);
    const int padA = p.addTestController(false), padB = p.addTestController(false);
    if (!padA || !padB || !restart()) {
        check(false, "two virtual gamepads could be attached");
        return;
    }
    p.setTestButton(padB, 0, true);  // A: button 1, the throttle
    p.setTestAxis(padB, 0, -1.0f);   // left stick X: the steering axis
    JsonValue d = drive(1.0);
    check(p.controllerCount() == 2 && d["throttle"].num() > 0.9 && d["steer"].num() < -0.3,
          strf("two gamepads merged (%d connected), the second one driving: throttle %.2f, steering %.2f", p.controllerCount(),
               d["throttle"].num(), d["steer"].num()));
    check(p.takeTestHapticLog().empty(), "gamepad device: no force feedback");
    // B unplugged mid-game: A drives on.
    p.removeTestController(padB);
    p.setTestButton(padA, 0, true);
    p.setTestAxis(padA, 0, 1.0f);
    d = drive(1.0);
    check(p.controllerCount() == 1 && d["throttle"].num() > 0.9 && d["steer"].num() > 0.3,
          strf("after unplugging one, the other gamepad drives (%d connected): throttle %.2f, steering %.2f", p.controllerCount(),
               d["throttle"].num(), d["steer"].num()));
    p.setTestButton(padA, 0, false);
    p.setTestAxis(padA, 0, 0.0f);
    drive(0.5);
    // The keyboard beside the gamepad device.
    p.injectKey(82, true);  // SDL_SCANCODE_UP
    p.injectKey(80, true);  // SDL_SCANCODE_LEFT
    d = drive(1.0);
    p.injectKey(82, false);
    p.injectKey(80, false);
    check(d["throttle"].num() > 0.9 && d["steer"].num() < -0.3,
          strf("the keyboard beside the gamepad device: Up and Left give throttle %.2f, steering %.2f", d["throttle"].num(),
               d["steer"].num()));

    // A force-feedback wheel plugged in mid-game, separate pedals (throttle = Z axis, brake = R
    // axis, resting at +1); the wheel device.
    cc_ui_set_option("controlDevice", 4);
    cc_ui_set_binding(4, 8, 2, 4, 12);
    cc_ui_set_binding(4, 9, 2, 4, 14);
    const int wheel = p.addTestController(true, true);
    p.setTestAxis(wheel, 2, 1.0f);
    p.setTestAxis(wheel, 3, 1.0f);
    drive(0.3);
    const int joined = p.controllerCount();
    restart();
    d = drive(0.3);
    const double idle = d["throttle"].num(), idleBrake = d["brake"].num();
    p.setTestAxis(wheel, 2, -1.0f);  // throttle pedal down
    p.setTestAxis(wheel, 0, 1.0f);   // wheel right
    d = drive(1.0);
    check(joined == 2 && idle < 0.05 && idleBrake < 0.05 && d["throttle"].num() > 0.9 && d["brake"].num() < 0.05 &&
              d["steer"].num() > 0.3,
          strf("a wheel plugged in mid-game joins (%d connected), pedals on Z and R: throttle %.2f / brake %.2f at rest, "
               "throttle %.2f pressed, steering %.2f",
               joined, idle, idleBrake, d["throttle"].num(), d["steer"].num()));
    std::vector<std::string> log = p.takeTestHapticLog();
    const int fr = firstValue(log, "friction");
    check(!tagsWith(log, "start friction").empty() && !tagsWith(log, "start spring").empty() && fr > 0 && fr <= 7000,
          strf("wheel force feedback: friction and spring play, friction %d (0.7 x the tyre's surface friction)", fr));
    // A second force-feedback wheel: the effects go to both.
    const int wheel2 = p.addTestController(true, true);
    drive(0.3);
    cc_game_pause(1);  // the Esc menu: a pop-up, then the reset every paused frame
    drive(0.2);
    log = p.takeTestHapticLog();
    const std::vector<std::string> both = tagsWith(log, "start friction");
    check(p.controllerCount() == 3 && both.size() >= 2 && !tagsWith(log, "stop spring").empty(),
          strf("force feedback on every wheel (%d with friction) in the pause menu: stopped, then friction and spring again",
               int(both.size())));
    cc_game_pause(0);
    p.removeTestController(wheel2);
    p.setTestAxis(wheel, 0, 0.0f);
    p.setTestAxis(wheel, 2, 1.0f);

    // The joystick device: the shaping, and the approach mode in the stick's last 1 %.
    cc_ui_set_option("controlDevice", 2);
    restart();
    p.setTestAxis(wheel, 0, 0.5f);
    d = drive(0.5);
    const double half = d["steer"].num();
    p.setTestAxis(wheel, 0, 1.0f);
    JsonValue d1 = drive(0.05);
    d = drive(1.0);
    check(half > 0.0 && half < 0.6 && d["steer"].num() > 0.95,
          strf("joystick device: stick at 0.5 steers %.2f; held at full it ramps from %.2f to %.2f (approach mode)", half,
               d1["steer"].num(), d["steer"].num()));
    p.removeTestController(wheel);
    p.removeTestController(padA);
    drive(0.2);
    p.takeTestHapticLog();

    // The mouse device: the pointer at the right edge, the left button the throttle.
    cc_ui_set_option("controlDevice", 0);
    restart();
    int w = 1, h = 1;
    p.drawableSize(w, h);
    p.injectMouse(float(w), float(h) * 0.5f, 0, true);
    d = drive(1.0);
    p.injectMouse(float(w) * 0.5f, float(h) * 0.5f, 0, false);
    check(d["throttle"].num() > 0.9 && d["steer"].num() > 0.3 && d["steer"].num() < 0.99,
          strf("mouse device: pointer at the right edge steers %.2f (below full lock), left button throttle %.2f", d["steer"].num(),
               d["throttle"].num()));

    // The keyboard device: the arrow keys of MM2's table.
    cc_ui_set_option("controlDevice", 1);
    restart();
    p.injectKey(82, true);
    p.injectKey(80, true);
    d = drive(1.0);
    p.injectKey(82, false);
    p.injectKey(80, false);
    check(d["throttle"].num() > 0.9 && d["steer"].num() < -0.3,
          strf("keyboard device: Up and Left give throttle %.2f, steering %.2f", d["throttle"].num(), d["steer"].num()));
    cc_ui_set_option("controlDevice", 3);
}

int runSelfTest(Native& n) {
    App& app = theApp();
    std::string report;
    bool ok = true;
    auto check = [&](bool c, const std::string& what) {
        report += strf("  [%s] %s\n", c ? " ok " : "FAIL", what.c_str());
        if (!c) ok = false;
    };
    const JsonValue state = parseJson(cc_ui_state());
    check(state["drivers"].size() > 0, strf("profiles: %d drivers, current %s", int(state["drivers"].size()),
                                            state["driver"]["name"].str("none").c_str()));
    check(native::isFile(native::joinPath(n.playersDir, "players.dir")),
          "the players folder written to " + n.playersDir + " (MM2's players.dir and .cfg/.sav files)");
    check(n.platform->audioOpen(), "audio stream open (" + n.platform->audioDriver() + ")");

    const std::string base = n.opt.selftest;
    const std::string stem = iendsWith(base, ".png") ? base.substr(0, base.size() - 4) : base;
    const int frames = std::max(2, n.opt.frames);

    // The engine-drawn main menu.
    fe::FrontEnd* fe = frontEnd();
    check(fe != nullptr, "front end made");
    if (fe) {
        fe->openFrontEnd();
        AudioCheck menuAudio;
        check(selfTestCapture(n, stem + "_menu.png", 30, menuAudio, report) && fe->screenName() == "main",
              "main menu drawn (screen " + fe->screenName() + ")");
    }

    // Cruise.
    n.mode = 0;
    n.race = 0;
    std::string why;
    AudioCheck cruiseAudio;
    const bool cruise = startGame(n, why);
    check(cruise, "Cruise started in " + std::to_string(n.cityIndex + 1) + (cruise ? "" : ": " + why));
    if (cruise) {
        check(selfTestCapture(n, base, frames, cruiseAudio, report), "Cruise frame captured and not blank");
        check(app.driveView().active() && app.driveView().session() && app.driveView().session()->car().time() > 0.5f,
              strf("Cruise physics ran (%.2f s of car time)", app.driveView().session() ? double(app.driveView().session()->car().time()) : 0.0));
        const double rms = cruiseAudio.frames ? std::sqrt(cruiseAudio.sum2 / double(2 * cruiseAudio.frames)) : 0.0;
        check(cruiseAudio.frames > 0 && rms > 1e-4,
              strf("Cruise audio: %ld frames pulled, rms %.4f, peak %.3f", cruiseAudio.frames, rms, double(cruiseAudio.peak)));
        selfTestControllers(n, check);
        endGame(n);
    }

    // A race: the first Checkpoint race of the city (or the first mode it has).
    int mode = -1;
    const auto& cities = app.cityView().cities();
    if (n.cityIndex >= 0 && size_t(n.cityIndex) < cities.size())
        for (game::RaceMode m : {game::RaceMode::Checkpoint, game::RaceMode::Circuit, game::RaceMode::Blitz})
            if (mode < 0 && game::raceCount(cities[size_t(n.cityIndex)], m) > 0) mode = int(m);
    check(mode > 0, "the city has races");
    if (mode > 0) {
        n.mode = mode;
        n.race = 0;
        AudioCheck raceAudio;
        const bool started = startGame(n, why);
        check(started, std::string("race started: ") + game::modeTitle(game::RaceMode(mode)) + " 1" + (started ? "" : ": " + why));
        if (started) {
            check(selfTestCapture(n, stem + "_race.png", frames, raceAudio, report), "race frame captured and not blank");
            const JsonValue race = parseJson(app.driveView().raceJson());
            check(race["active"].boolean(), "race session running: state " + std::to_string(race["state"].integer()) + ", clock " +
                                                race["clock"].str() + ", " + std::to_string(race["racers"].integer()) + " racers");
            const double rms = raceAudio.frames ? std::sqrt(raceAudio.sum2 / double(2 * raceAudio.frames)) : 0.0;
            check(raceAudio.frames > 0 && rms > 1e-4,
                  strf("race audio: %ld frames pulled, rms %.4f, peak %.3f", raceAudio.frames, rms, double(raceAudio.peak)));
            endGame(n);
        }
    }

    // A network game hosted on this computer: the engine's session server on its own thread, the
    // native transport, the session open with a code (no other player: nothing starts).
    {
        std::string err;
        const bool served = app.netStartLocalServer(0, false, &err);
        check(served, "the session server runs on this computer" + (served ? strf(" (port %u)", unsigned(app.netLocalServer()->port())) : ": " + err));
        if (served) {
            app.netBegin("Self-test", "vpbug", 0);
            net::SessionDesc d;
            d.name = "self-test";
            d.version = net::kGameVersion;
            app.nativeTransport()->host(d);
            for (int i = 0; i < 300 && !(app.netSession()->open() && !app.nativeTransport()->code().empty()); ++i) {
                app.stepFrames(1, 1.0f / 60.0f);
                Platform::delay(0.002);
            }
            const bool open = app.netSession()->open() && app.netSession()->isHost() && app.nativeTransport()->code().size() == 4;
            check(open, "a network game hosted: code " + app.nativeTransport()->code() + ", " + app.nativeTransport()->stateName());
            app.netEnd();
            app.netStopLocalServer();
        }
    }
    // The same through the Multiplayer screen: "Host on this computer" (the network link starts the
    // engine's server for the game), the code, then Leave (the server stops after the quit).
    if (fe::FrontEnd* fe = frontEnd(); fe && app.netLink()) {
        net::NetLink* link = app.netLink();
        const bool here = link->hostHere(), lan = link->lan();
        link->setHostHere(true);
        link->setLan(false);
        fe->openScreen("mp");
        app.stepFrames(3, 1.0f / 60.0f);
        fe->testNetHost();
        for (int i = 0; i < 300 && !(app.netSession() && app.netSession()->open() && link->code().size() == 4); ++i) {
            app.stepFrames(1, 1.0f / 60.0f);
            Platform::delay(0.002);
        }
        const bool open = app.netSession() && app.netSession()->open() && link->localServerRunning();
        check(open, "a network game hosted from the Multiplayer screen on this computer: code " + link->code() + " at " +
                        link->localServerUrl());
        if (open) {
            AudioCheck mpAudio;
            selfTestCapture(n, stem + "_mp.png", 20, mpAudio, report);  // the lobby
        }
        fe->testNetLeave();
        for (int i = 0; i < 60 && link->localServerRunning(); ++i) app.stepFrames(1, 1.0f / 60.0f);
        check(!link->localServerRunning() && !(app.netSession() && app.netSession()->open()), "Leave ends it and stops the server");
        link->setHostHere(here);
        link->setLan(lan);
    }
    std::printf("self-test %s\n%s", ok ? "passed" : "FAILED", report.c_str());
    return ok ? 0 : 1;
}

}  // namespace

// The one way out once the engine has started (Ctrl+Q, Q in the pause menu, the window closed,
// --quit-after, the self-test, every error after start-up): the game ends (its view bytes into the
// driver's .cfg), a network session is left, the settings are saved, the app releases its GPU
// objects, then the GL context, the window and SDL go, all before main returns, so no static
// destructor does any GL work.
int finish(Native& n, int rc) {
    App& app = theApp();
    endGame(n);
    if (n.net) {
        app.netEnd();  // leaves the session (a host's "host quits" first)
        for (int i = 0; i < 5; ++i) Platform::delay(0.01);
        app.netStopLocalServer();
        n.net = false;
    }
    if (n.platform && n.saveOnExit) {
        saveWindowState(n);
        if (n.cfg.dirty()) n.cfg.save();
    }
    app.shutdown();
    if (n.platform) n.platform->shutdown();
    Platform::finalQuit();
    return rc;
}

int main(int argc, char** argv) {
    // `crazycity --server`: only the session server (platform/host_server.*), no window.
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--server") == 0) return net::serverMain(std::vector<std::string>(argv + 1, argv + argc));
    Native n;
    g_native = &n;
    std::string err;
    if (!parseArgs(argc, argv, n.opt, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    if (n.opt.help) {
        usage();
        return 0;
    }
    if (n.opt.version) {
        std::printf("CrazyCity Insanity %s (native)\n", CC_VERSION);
        return 0;
    }
    const bool selftest = !n.opt.selftest.empty();
    const bool listing = !n.opt.list.empty();
    n.exeDir = Platform::basePath();
    n.configDir = n.opt.configDir.empty() ? native::defaultConfigDir() : native::absolutePath(n.opt.configDir) + "/";
    n.dataDir = n.opt.dataDir.empty() ? native::defaultDataDir() : native::absolutePath(n.opt.dataDir) + "/";
    n.playersDir = n.dataDir + "players/";
    native::makeDirs(n.configDir);
    native::makeDirs(n.dataDir);
    n.cfg.load(n.configDir + "native.cfg");

    // Logging: the terminal (warnings and errors; everything with --verbose) and the overlay.
    setLogEcho(false);
    NativePageSinks sinks;
    sinks.log = printLog;
    setNativePageSinks(sinks);


    PlatformConfig pc;
    pc.offscreen = selftest || listing || n.opt.offscreen;
    pc.width = n.opt.width > 0 ? n.opt.width : n.cfg.getInt("window_width", 1280);
    pc.height = n.opt.height > 0 ? n.opt.height : n.cfg.getInt("window_height", 720);
    if (pc.offscreen && n.opt.width <= 0) {
        pc.width = 1280;
        pc.height = 720;
    }
    pc.fullscreen = n.opt.fullscreen || (!n.opt.windowed && n.cfg.getBool("fullscreen", false));
    pc.maximized = !n.opt.windowed && !n.opt.fullscreen && n.cfg.getBool("maximized", false);
    pc.vsync = !n.opt.noVsync;
    pc.msaa = n.opt.msaa >= 0 ? n.opt.msaa : 4;
    pc.audio = !n.opt.noAudio && !listing;
    pc.audioDriver = n.opt.audioDriver.empty() && selftest ? "dummy" : n.opt.audioDriver;
    pc.controllers = !n.opt.noPad && !listing;
    pc.testControllersOnly = selftest;  // the self-test's virtual controllers only, never the player's
    Platform::configure(pc);

    // The engine UI's hooks: the drivers as files in <data>/players/, the menus' settings in
    // native.cfg, the system's dialogs (shown between frames) and clipboard.
    native::makeDirs(n.playersDir);
    n.host.settings = &n.cfg;
    n.host.playersDir = n.playersDir;
    n.host.onPick = [&n](ui::Pick what) {
        n.pickPending = true;
        n.pick = what;
        return true;
    };
    n.host.canPickFn = [&n](ui::Pick what) {
        if (!n.platform || n.platform->offscreen()) return false;
        if (what == ui::Pick::HeldPlayers) return !playersFolderOf(n.mm2).empty();
        return what != ui::Pick::DevPlayers;
    };
    theApp().setHost(&n.host);

    App& app = theApp();
    if (!app.init()) {
        std::fprintf(stderr, "the engine could not start (OpenGL ES 3.0 is needed)\n");
        app.shutdown();
        Platform::finalQuit();
        return 1;
    }
    n.platform = app.context().platform;
    n.host.platform = n.platform;
    // The Multiplayer screens' network link: the native transport, hosting on this computer, LAN
    // games; its settings from native.cfg (net_*).
    static net::NativeNetLink netLink(app, &n.cfg);
    netLink.applySettings();
    app.setNetLink(&netLink);
    n.overlayOn = n.opt.overlay;
    n.overlayReady = n.overlay.init();
    n.platform->setBeforeSwap(beforeSwap);
    showBusy(n, "CrazyCity Insanity");

    n.mm2 = findMm2Folder(n);
    if (n.mm2.empty()) {
        if (!selftest && !listing && n.opt.mm2.empty())
            n.platform->message(kTitle, "CrazyCity Insanity needs your Midtown Madness 2 folder (the one with mm2core.ar).\n"
                                        "Start it again and pick that folder, or give it with --mm2 DIR.", true);
        std::fprintf(stderr, "no Midtown Madness 2 folder (give it with --mm2 DIR)\n");
        return finish(n, 1);
    }
    if (!selftest && !listing) {
        n.cfg.set("mm2_folder", n.mm2);
        n.cfg.save();
    }
    showBusy(n, "Reading " + n.mm2);
    // The drivers kept here are read by the front end when the archives are mounted; the first
    // start (or --import-players) imports the MM2 folder's players folder.
    bool haveDrivers = false;
    for (const std::string& rel : native::listFilesRecursive(n.playersDir)) haveDrivers = haveDrivers || !iendsWith(rel, ".tmp");
    loadStrings(n);
    if (!mountFolder(n)) {
        n.platform->message(kTitle, "The game data in " + n.mm2 + " could not be read (no cars or cities).", true);
        return finish(n, 1);
    }
    loadGmBanks(n);
    if (!haveDrivers || n.opt.importPlayers) {
        const std::string pd = playersFolderOf(n.mm2);
        if (!pd.empty()) importPlayers(n, pd, false);
    }
    JsonValue state = parseJson(cc_ui_state());
    state = chooseDriver(n, state);
    if (!applyControlOptions(n, state)) {
        return finish(n, 2);
    }
    if (frontEnd()) frontEnd()->profilesChanged();
    if (listing) {
        listThings(n.opt.list, state);
        return finish(n, 0);
    }

    // What to play: the command line, else the driver's last city, car and paint.
    const JsonValue& d = state["driver"];
    n.cityIndex = findCity(n.opt.city, d["city"].str("sf"));
    if (n.cityIndex < 0) {
        std::fprintf(stderr, "no city %s (see --list cities)\n", n.opt.city.c_str());
        return finish(n, 2);
    }
    n.carIndex = findCar(n.opt.car, d["car"].str());
    if (n.carIndex < 0) {
        std::fprintf(stderr, "no car %s (see --list cars)\n", n.opt.car.c_str());
        return finish(n, 2);
    }
    n.paint = n.opt.paint >= 0 ? n.opt.paint : (n.opt.car.empty() ? d["paint"].integer(0) : 0);
    n.mode = modeNumber(n.opt.mode);
    if (n.mode < 0) {
        std::fprintf(stderr, "no mode %s (cruise, checkpoint, circuit, blitz or crash)\n", n.opt.mode.c_str());
        return finish(n, 2);
    }
    n.race = std::max(0, n.opt.race - 1);
    if (n.platform->openAudio(app.audioRate())) n.platform->setAudioGain(float(std::clamp(n.opt.volume, 0, 100)) / 100.0f);

    if (selftest) {
        const int rc = runSelfTest(n);
        return finish(n, rc);
    }

    // The menus; for testing, the command line's game or network game straight away.
    std::string why;
    n.net = n.opt.hostGame || !n.opt.join.empty();
    if (n.net) {
        if (!startNetwork(n, state, why)) {
            std::fprintf(stderr, "network: %s\n", why.c_str());
            n.platform->message(kTitle, "The network game could not start: " + why, true);
            return finish(n, 1);
        }
        if (frontEnd()) frontEnd()->openScreen("mp");  // the lobby, as the Multiplayer screen shows it
    } else if (n.opt.startGame) {
        requestGame(n);
    } else if (frontEnd()) {
        frontEnd()->openFrontEnd();
    }

    // The main loop: a frame per vertical refresh as the browser's animation frames (the engine's
    // frame takes the real time since the last one, at most 0.1 s); nothing runs while the window
    // is minimised, as a hidden page gets no frames; the audio stream is topped up between frames.
    bool running = true;
    bool wasMinimized = false;
    n.saveOnExit = true;  // the window state and native.cfg are saved when the game ends
    double last = n.platform->timeSeconds();
    const double started = last;
    long frames = 0;
    while (running) {
        if (n.platform->minimized()) {
            if (!wasMinimized) n.platform->pauseAudio(true);
            wasMinimized = true;
            if (!n.platform->waitWhileMinimized(100)) break;
            continue;
        }
        if (wasMinimized) {
            n.platform->pauseAudio(false);
            wasMinimized = false;
        }
        const double t0 = n.platform->timeSeconds();
        app.frame();
        if (n.platform->quitRequested() || n.host.quitAsked) break;
        if (n.net) netStep(n, false);
        running = handleKeys(n);
        if (n.pickPending) {
            n.pickPending = false;
            handlePick(n, n.pick);
            last = n.platform->timeSeconds();  // the dialog's time is not game time
        }
        // A drive started (from the menus): the driver's controller options.
        const bool driving = app.driveView().active();
        if (driving && !n.wasDriving) applyControllerOptions(n);
        n.wasDriving = driving;
        n.platform->feedAudio([&](float* out, int count) { app.renderAudio(out, count); }, kAudioChunk, kAudioTarget);
        const double now = n.platform->timeSeconds();
        const float dt = float(now - last);
        last = now;
        n.noteTime = std::max(0.0f, n.noteTime - dt);
        ++n.fpsFrames;
        ++frames;
        if (now - n.fpsTime >= 0.5) {
            n.fps = float(n.fpsFrames / (now - n.fpsTime));
            n.fpsTime = now;
            n.fpsFrames = 0;
        }
        if (n.platform->windowStateChanged()) saveWindowState(n);
        if (n.opt.quitAfter > 0 && now - started >= n.opt.quitAfter) running = false;
        // Without vsync (or where the swap does not wait), at most a frame per 1/240 s.
        const double spent = n.platform->timeSeconds() - t0;
        if (spent < 1.0 / 240.0) Platform::delay(1.0 / 240.0 - spent);
    }
    if (n.opt.verbose) {
        const double t = n.platform->timeSeconds() - started;
        std::fprintf(stderr, "%ld frames in %.1f s (%.1f per second)\n", frames, t, t > 0 ? double(frames) / t : 0.0);
    }
    return finish(n, 0);
}
