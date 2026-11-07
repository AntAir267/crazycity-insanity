// cctool uiscreens <game folder> <out dir> <archive.ar>...: the engine-drawn front end
// (app/front_end.h) driven without a window or GPU. Every screen and overlay is laid out and
// drawn on the CPU (ui/soft_backend.h) into <out dir>/*.png; the checks walk the menus with keys
// and clicks as a player would, against the player's own profiles (players/), mmlang.dll and
// archives, and verify the menus.md numbers of the sliders (19 steps, the cop control).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "app/fe_services.h"
#include "app/fe_theme.h"
#include "app/fe_words.h"
#include "app/front_end.h"
#include "app/menu_data.h"
#include "core/json.h"
#include "core/json_value.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "formats/cinfo.h"
#include "ui/soft_backend.h"
#include "ui/text.h"
#include "ui/ui.h"
#include "vfs/vfs.h"

using namespace cc;
using namespace cc::ui;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

namespace {

bool readWhole(const std::string& path, Bytes& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n < 0 ? 0 : n));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

// ---------------------------------------------------------------------------------------------
// The program around the front end: replies as the App gives them, no views.

class TestServices : public fe::Services {
public:
    std::vector<CarInfo> cars;
    std::vector<CityEntry> cities;
    bool drivingNow = false;
    int begins = 0, nextRaces = 0, ends = 0, restarts = 0, pauses = 0;
    double now = 0;

    std::string carList() override {
        JsonWriter j;
        j.beginArray();
        for (size_t i = 0; i < cars.size(); ++i) {
            j.beginObject();
            j.field("index", int(i));
            j.field("name", latin1ToUtf8(cars[i].baseName));
            j.field("description", latin1ToUtf8(cars[i].description));
            j.field("group", 0);
            j.field("mod", cars[i].fromMod);
            j.field("archive", latin1ToUtf8(cars[i].archive));
            j.field("colors", int(cars[i].colors.size()));
            j.endObject();
        }
        j.endArray();
        return j.str();
    }
    std::string cityList() override {
        JsonWriter j;
        j.beginArray();
        for (const CityEntry& c : cities) {
            j.beginObject();
            j.field("name", latin1ToUtf8(c.localizedName));
            j.field("map", latin1ToUtf8(c.mapName));
            j.field("raceDir", latin1ToUtf8(c.raceDir));
            j.field("test", c.testLayout);
            j.endObject();
        }
        j.endArray();
        return j.str();
    }
    std::string selectCar(int) override { return "{\"loaded\":true}"; }
    void setPaint(int) override {}
    void setLod(int) override {}
    void setCarOption(const std::string&, int) override {}
    void resetCamera() override {}
    void setView(int) override { drivingNow = false; }  // as the App: a view switch ends a drive
    std::string loadCity(int) override { return "{\"loaded\":true}"; }
    void setCityOption(const std::string&, int) override {}
    std::string dropCar() override { return "{\"ok\":true}"; }
    std::string startDrive(int, bool) override {
        drivingNow = true;
        return "{\"ok\":true}";
    }
    void stopDrive() override { drivingNow = false; }
    void setDriveOption(const std::string&, int) override {}
    void setCopDensity(float) override {}
    void setPointerLock(bool) override {}
    void setSoundOption(const std::string&, int) override {}
    std::string soundState() override { return "{}"; }
    void setMusicMode(int) override {}
    void setMusicVolume(float) override {}
    std::string musicStatus() override { return "{}"; }
    void clearGmBank() override {}
    bool audioRunning() override { return true; }
    std::string raceList() override { return "{}"; }
    std::string startRace(int, int, bool, int) override { return "{\"ok\":false}"; }
    void restartRace() override {}
    std::string nextRace() override { return "{\"ok\":false}"; }
    std::string stats() override { return "{}"; }
    std::string readout() override { return std::string(); }
    bool driving() override { return drivingNow; }
    bool netDriving() override { return false; }
    void afterStart() override {}
    void setEnvironment(int, int, int, double, double, int, double) override {}
    std::string gameBegin(int, int, int, int, int, int) override {
        ++begins;
        drivingNow = true;
        return "{\"ok\":true}";
    }
    std::string gameNextRace(int, int, int) override {
        ++nextRaces;
        return "{\"ok\":true}";
    }
    void gameSetViewDistance(double) override {}
    void gamePause(bool on) override { pauses += on ? 1 : 0; }
    void gameRestart() override { ++restarts; }
    std::string gameEnd() override {
        ++ends;
        drivingNow = false;
        return "{\"ok\":true,\"state\":" + menu::state() + "}";
    }
    void netPrepare(bool) override {}
    void netGameStart(const std::string&, const std::string&, int, const std::string&, int, int) override {}
    std::string netGameEnd() override { return "{}"; }
    net::NetLink* netLink() override { return nullptr; }
    void netBegin(const std::string&, const std::string&, int) override {}
    void netEnd() override {}
    void netRecord(const std::string&, const std::string&, int, int, bool) override {}
    void netReady(bool) override {}
    void netSettings(int, const std::string&, int, int, int, int, int, double, double, int, int, int, int) override {}
    std::string netDefaults(int, const std::string&, int, int) override { return "{}"; }
    void netDescribe(const std::string&, const std::string&, int) override {}
    bool netStart() override { return false; }
    bool netJoinRunning() override { return false; }
    void netKick(double) override {}
    void netLobby() override {}
    void netQuit() override {}
    void netChat(const std::string&) override {}
    bool netRestart() override { return false; }
    std::string netState() override { return "{}"; }
    double clock() override { return now; }
    int gamepadButtons() override { return padButtons; }
    bool controllerFeedback() override { return false; }
    fe::RawInput raw;  // the next frame's raw input (the customise-controls checks)
    int padButtons = 0;
    fe::RawInput rawInput() override {
        fe::RawInput r = raw;
        raw.keys.clear();
        raw.buttons.clear();
        return r;
    }
};

// A program that can close itself, keeps its storage in memory and has a clipboard.
class TestHost : public Host {
public:
    std::map<std::string, std::string> store;
    std::string clip;
    bool quitAsked = false;
    void setClipboard(const std::string& t) override { clip = t; }
    std::string clipboardText() override { return clip; }
    std::string storageGet(const std::string& k) override {
        auto it = store.find(k);
        return it == store.end() ? std::string() : it->second;
    }
    bool storageSet(const std::string& k, const std::string& v) override {
        store[k] = v;
        return true;
    }
    void storageDel(const std::string& k) override { store.erase(k); }
    std::vector<std::string> storageKeys(const std::string& prefix) override {
        std::vector<std::string> out;
        for (const auto& kv : store)
            if (startsWith(kv.first, prefix)) out.push_back(kv.first);
        return out;
    }
    bool canQuit() const override { return true; }
    void quit() override { quitAsked = true; }
};

// ---------------------------------------------------------------------------------------------
// Frames, input and snapshots

struct Rig {
    TestServices svc;
    TestHost host;
    Ui ui;
    std::unique_ptr<fe::FrontEnd> fe;
    int w = 1280, h = 720;
    float dpr = 1.0f;
    double t = 0;
    std::string outDir;
    int problems = 0;
    int pngs = 0;

    void check(bool c, const std::string& what) {
        std::printf("  [%s] %s\n", c ? " ok " : "FAIL", what.c_str());
        if (!c) ++problems;
    }

    void frame(std::vector<InputEvent> ev = {}) {
        FrameInput fi;
        fi.events = std::move(ev);
        fi.width = w;
        fi.height = h;
        fi.scale = Ui::chooseScale(w, h, dpr);
        fi.time = t;
        fi.dt = 1.0f / 60.0f;
        ui.beginFrame(fi);
        fe->frame(ui);
        ui.endFrame();
        t += 1.0 / 60.0;
        svc.now = t;
    }
    void frames(int n) {
        for (int i = 0; i < n; ++i) frame();
    }
    void key(Key k, uint8_t mods = 0) {
        InputEvent d;
        d.type = InputEvent::KeyDown;
        d.key = k;
        d.mods = mods;
        InputEvent u = d;
        u.type = InputEvent::KeyUp;
        frame({d});
        frame({u});
        frame();
    }
    void type(const std::string& s) {
        InputEvent e;
        e.type = InputEvent::Text;
        e.text = s;
        frame({e});
    }
    // The node with this label (the last one drawn: the topmost); `visibleOnly`: not scrolled
    // out of sight.
    const Ui::Snapshot* find(const std::string& label, bool visibleOnly = true, bool focusableOnly = true) {
        static std::vector<Ui::Snapshot> snap;
        snap = ui.snapshot();
        const Ui::Snapshot* best = nullptr;
        for (const Ui::Snapshot& s : snap)
            if (s.label == label && (s.focusable || !focusableOnly) && (!visibleOnly || (s.visible.w > 0 && s.visible.h > 0))) best = &s;
        return best;
    }
    // Some node's words contain `text` (messages, table cells, buttons).
    bool has(const std::string& text) {
        for (const Ui::Snapshot& s : ui.snapshot())
            if (s.label.find(text) != std::string::npos) return true;
        return false;
    }
    // The wheel over its list until a node scrolled out of sight shows (as a player would).
    void reveal(const std::string& label) {
        for (int i = 0; i < 20 && !find(label); ++i) {
            const Ui::Snapshot* s = find(label, false);
            if (!s) return;
            // The pointer over the node's list, mid-height (inside the menus' panel).
            InputEvent m;
            m.type = InputEvent::MouseMove;
            m.x = s->rect.x + 8;
            m.y = float(h) * 0.5f;
            InputEvent wh = m;
            wh.type = InputEvent::Wheel;
            wh.dy = s->rect.y > m.y ? -2.0f : 2.0f;
            frame({m, wh});
            frames(2);
        }
    }
    bool click(const std::string& label) {
        reveal(label);
        const Ui::Snapshot* s = find(label);
        if (!s) s = find(label, true, false);  // a button the keyboard does not reach
        if (!s) {
            check(false, "\"" + label + "\" is on the screen to click");
            return false;
        }
        InputEvent m;
        m.type = InputEvent::MouseMove;
        m.x = s->visible.x + s->visible.w * 0.5f;
        m.y = s->visible.y + s->visible.h * 0.5f;
        InputEvent d = m;
        d.type = InputEvent::MouseDown;
        InputEvent u = m;
        u.type = InputEvent::MouseUp;
        frame({m, d});
        frame({u});
        frames(2);
        return true;
    }
    std::string focused() {
        for (const Ui::Focusable& f : ui.focusables())
            if (f.id == ui.focus()) return f.label;
        return std::string();
    }
    void snap(const std::string& name) {
        SoftCanvas c;
        c.resize(w, h);
        c.background(hex(0x55606e), hex(0x171a1f));  // a stand-in for the 3D view
        c.render(ui.drawList());
        const std::string path = outDir + "/" + name + ".png";
        const bool ok = c.writePng(path);
        if (ok) ++pngs;
        else check(false, "snapshot " + path + " written");
    }
    // Nothing focusable sticks out of the window sideways (no horizontal page scroll).
    bool fitsWidth() {
        for (const Ui::Snapshot& s : ui.snapshot())
            if (s.focusable && s.visible.w > 0 && (s.visible.x < -0.5f || s.visible.x + s.visible.w > float(w) + 0.5f)) return false;
        return true;
    }
};

// One frame of a slider (fe_theme.h) on its own, focused, with these keys pressed.
template <typename F>
void sliderFrames(Ui& ui, double& t, const std::vector<Key>& keys, F build) {
    auto one = [&](std::vector<InputEvent> ev) {
        FrameInput fi;
        fi.events = std::move(ev);
        fi.width = 800;
        fi.height = 200;
        fi.scale = 1;
        fi.time = t;
        fi.dt = 1.0f / 60.0f;
        ui.beginFrame(fi);
        Style s;
        s.column();
        s.w = 400;
        ui.open("root", s);
        build();
        ui.close();
        ui.endFrame();
        t += 1.0 / 60.0;
    };
    one({});
    for (Key k : keys) {
        InputEvent d;
        d.type = InputEvent::KeyDown;
        d.key = k;
        one({d});
        d.type = InputEvent::KeyUp;
        one({d});
    }
}

}  // namespace

int cmdUiScreens(const std::string& gameDir, const std::string& outDir, const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    if (!Fonts::get().init()) {
        std::printf("the UI fonts are missing\n");
        return 1;
    }
    Rig r;
    r.outDir = outDir;
    std::error_code mkErr;
    std::filesystem::create_directories(outDir, mkErr);
    r.ui.setHost(&r.host);
    r.svc.cities = buildCityList(vfs);
    r.svc.cars = buildCarList(vfs);
    {
        menu::Env env;
        env.vfs = &vfs;
        env.cities = [&r]() -> const std::vector<CityEntry>& { return r.svc.cities; };
        env.cars = [&r]() {
            std::vector<menu::ListCar> out;
            for (const CarInfo& c : r.svc.cars) out.push_back(menu::ListCar{c.baseName, true, std::atoi(c.info.get("Flags").c_str())});
            return out;
        };
        env.clock = [&r]() { return r.t; };
        menu::setEnv(env);
    }
    r.fe = std::make_unique<fe::FrontEnd>(r.svc, r.host);
    int firstCity = -1;
    for (size_t i = 0; i < r.svc.cities.size() && firstCity < 0; ++i)
        if (!r.svc.cities[i].testLayout) firstCity = int(i);

    std::printf("== Program start (%d cars, %d cities)\n", int(r.svc.cars.size()), int(r.svc.cities.size()));
    Bytes mmlang;
    const bool haveMmlang = readWhole(gameDir + "/mmlang.dll", mmlang);
    if (haveMmlang) r.fe->loadStrings(mmlang.data(), mmlang.size(), false);
    r.check(haveMmlang, "mmlang.dll read from the game folder");
    {
        // The mount summary as App::mount writes it.
        JsonWriter j;
        j.beginObject();
        j.key("archives").beginArray();
        for (const auto& a : vfs.archives()) {
            j.beginObject();
            j.field("name", a->name());
            j.field("ok", true);
            j.field("base", Vfs::isBaseArchive(a->name()));
            j.field("files", int(a->entries().size()));
            j.endObject();
        }
        j.endArray();
        j.field("files", int(vfs.fileCount()));
        j.field("cars", int(r.svc.cars.size()));
        j.field("cities", int(r.svc.cities.size()));
        j.endObject();
        r.fe->onMounted(j.str());
    }
    r.frames(3);
    r.check(r.fe->screenName() == "viewer", "after mounting: the viewer (" + r.fe->screenName() + ")");
    r.snap("viewer");

    // The player's own profiles, imported as the Drivers screen's import does.
    {
        menu::importBegin();
        int files = 0;
        namespace fs = std::filesystem;
        const fs::path pd = fs::path(gameDir) / "players";
        std::error_code ec;
        for (fs::recursive_directory_iterator it(pd, ec), endIt; !ec && it != endIt; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            Bytes data;
            if (!readWhole(it->path().string(), data)) continue;
            menu::importFile(fs::relative(it->path(), pd).generic_string(), data.data(), data.size());
            ++files;
        }
        r.fe->importFinished();
        r.frames(3);
        r.check(r.find("OK") != nullptr || r.find("Close") != nullptr, "the import's report is shown");
        r.snap("import_report");
        r.key(Key::Enter);
        r.check(files > 0 && !r.host.storageKeys("cci.players/").empty(),
                std::to_string(files) + " files of players/ imported and kept in the host's storage (" +
                    std::to_string(r.host.storageKeys("cci.players/").size()) + " files)");
    }

    std::printf("== Main menu: keys and labels\n");
    r.fe->openFrontEnd();
    r.frames(4);
    r.check(r.fe->screenName() == "main", "PLAY opens the main menu");
    r.check(r.focused() == "Single player", "the first item has the focus (" + r.focused() + ")");
    r.snap("main");
    r.key(Key::Down);
    r.check(r.focused() == "Quick race", "Down: Quick race (" + r.focused() + ")");
    r.key(Key::Down);
    const std::string crashLabel = menu::text(12);
    r.check(haveMmlang && r.focused() == crashLabel && !crashLabel.empty(), "Down: text 12 from mmlang.dll (" + r.focused() + ")");
    r.key(Key::Up);
    r.key(Key::Up);
    r.check(r.focused() == "Single player", "Up, Up: back to Single player");
    r.check(r.find("Quit", false) != nullptr, "Quit offered where the program can close itself");
    r.key(Key::Enter);
    r.check(r.fe->screenName() == "race", "Enter opens Single player (" + r.fe->screenName() + ")");
    r.snap("race");
    r.key(Key::Escape);
    r.check(r.fe->screenName() == "main", "Esc goes back to the main menu");

    std::printf("== Every screen\n");
    struct Visit {
        const char* item;
        const char* screen;
        const char* png;
    };
    const std::vector<Visit> visits = {{"Quick race", "car", "car"},       {nullptr, "crashcity", "crashcity"},
                                       {"Multiplayer", "mp", "multiplayer"}, {"Drivers", "drivers", "drivers"},
                                       {"Driver record", "record", "record"}, {"Race statistics", "stats", "stats"},
                                       {"Options", "options", "options_graphics"}};
    for (const Visit& v : visits) {
        const std::string label = v.item ? v.item : crashLabel;
        if (!r.click(label)) continue;
        r.check(r.fe->screenName() == v.screen, "click " + label + ": screen " + r.fe->screenName());
        r.snap(v.png);
        if (std::string(v.screen) == "options") {
            if (r.click("Audio")) r.snap("options_audio");
            if (r.click("Controls")) r.snap("options_controls");
            if (r.click("Customise controls")) r.snap("options_bindings");
        }
        if (std::string(v.screen) == "crashcity" && firstCity >= 0) {
            const std::string cityName = latin1ToUtf8(r.svc.cities[size_t(firstCity)].localizedName);
            if (r.click(cityName)) {
                r.check(r.fe->screenName() == "crash", "a city's lessons (" + r.fe->screenName() + ")");
                r.snap("crash");
                r.key(Key::Escape);
            }
        }
        r.key(Key::Escape);
        r.check(r.fe->screenName() == "main", std::string("Esc from ") + v.screen + " back to the main menu (" + r.fe->screenName() + ")");
    }

    std::printf("== Drivers: a new driver typed in, the clipboard, deleted again\n");
    if (r.click("Drivers")) {
        const JValue before = JValue::parse(menu::state());
        const size_t n0 = before["drivers"].size();
        if (r.click("New driver")) {
            r.snap("new_driver");
            r.type("Uitest");
            r.key(Key::A, ModCtrl);
            r.key(Key::C, ModCtrl);
            r.check(r.host.clip == "Uitest", "Ctrl+A, Ctrl+C copy the name (\"" + r.host.clip + "\")");
            r.key(Key::End);
            r.key(Key::V, ModCtrl);  // pasted at the end: "UitestUitest"
            r.key(Key::Enter);
            const JValue after = JValue::parse(menu::state());
            r.check(after["drivers"].size() == n0 + 1, "Enter makes the driver (" + std::to_string(after["drivers"].size()) + " drivers)");
            const std::string made = after["driver"]["name"].str();
            r.check(made == "UitestUitest", "the typed and pasted name: " + made);
            if (r.click("Delete driver")) {
                r.snap("delete_confirm");
                r.click("Delete");
                r.check(JValue::parse(menu::state())["drivers"].size() == n0, "Delete driver, Delete: back to " + std::to_string(n0));
            }
        }
        r.key(Key::Escape);
    }

    std::printf("== Customise controls: the driver's binding table (controls.md)\n");
    if (r.click("Options")) {
        auto entry = [](int dev, int a) {
            const JValue s = JValue::parse(menu::state());
            const JValue& e = s["options"]["bindings"][dev * 34 + a];
            return strf("%d/%d/0x%02X", e[0].integer(), e[1].integer(), e[2].integer());
        };
        auto capture = [&](const std::string& action) {
            r.click(action);
            return r.has("No binding");  // the capture dialog
        };
        r.click("Customise controls");
        const std::string padThrottle = entry(3, 8);
        r.click(fe::T(581));  // the keyboard device's table
        r.check(entry(1, 8) == "1/3/0xC8", "keyboard device: throttle on Up, MM2's default (" + entry(1, 8) + ")");
        r.check(capture(fe::T(280)), "choosing " + fe::T(280) + " opens the capture");
        r.snap("bind_capture");
        r.svc.raw.keys = {0x11};
        r.frames(3);
        r.check(entry(1, 8) == "1/3/0x11" && !r.has("No binding"), "W pressed: throttle on W, held key (" + entry(1, 8) + ")");
        capture(fe::T(280));
        r.svc.raw.keys = {0x39};
        r.frames(3);
        const std::string warn = "Space is also used for " + fe::T(307) + ".";
        r.check(entry(1, 8) == "1/3/0x39" && r.fe->bindWarning() == warn && r.has(warn), "a key in use is taken with a warning: " + r.fe->bindWarning());
        r.snap("bind_warning");
        capture(fe::T(281));
        r.svc.raw.keys = {0x01};
        r.key(Key::Escape);  // the same Esc as a key: it must not also leave the screen
        r.check(entry(1, 9) == "1/3/0xD0" && !r.has("No binding") && r.fe->screenName() == "options",
                "Esc keeps the brake on Down and stays on the screen (" + entry(1, 9) + ", " + r.fe->screenName() + ")");
        capture(fe::T(276));
        r.svc.raw.keys = {0x2E};
        r.frames(3);
        r.check(entry(1, 11) == "3/3/0x2E", "a press action takes kind 3 (camera on C: " + entry(1, 11) + ")");
        // The gamepad device: an axis for steering, a button for the throttle, a moved half axis
        // for the brake.
        r.click(fe::T(583));
        r.svc.raw.padConnected = true;
        r.svc.raw.axisCount = 6;
        capture(fe::T(282));
        r.svc.raw.axis[0] = -1.0f;
        r.frames(3);
        r.svc.raw.axis[0] = 0.0f;
        r.check(entry(3, 5) == "2/4/0x0A", "gamepad device: the stick moved gives the steering axis X, kind 2 (" + entry(3, 5) + ")");
        capture(fe::T(280));
        r.svc.raw.buttons = {0};
        r.frames(3);
        r.check(entry(3, 8) == "1/4/0x15", "button 1 pressed: throttle on button 1, component 21 (" + entry(3, 8) + ")");
        capture(fe::T(281));
        r.svc.raw.axis[1] = 1.0f;
        r.frames(3);
        r.svc.raw.axis[1] = 0.0f;
        r.check(entry(3, 9) == "2/4/0x14", "the stick pulled back: brake on the Y axis's down half (" + entry(3, 9) + ")");
        // The mouse device's mouse inputs are chosen in the dialog.
        r.click(fe::T(580));
        capture(fe::T(281));
        r.click(fe::T(311));
        r.check(entry(0, 9) == "1/2/0x02", "mouse device: brake on the right mouse button (" + entry(0, 9) + ")");
        r.svc.raw = fe::RawInput();
        r.click("Cancel");
        r.check(entry(1, 8) == "1/3/0xC8" && entry(3, 8) == padThrottle,
                "Cancel puts the driver's table back from its file (keyboard throttle " + entry(1, 8) + ", gamepad " + entry(3, 8) + ")");
        r.key(Key::Escape);
        // The Keys box lists the driver's bindings for its control device.
        const JValue s = JValue::parse(menu::state());
        const int dev = s["options"]["controlDevice"].integer();
        const std::string throttle = fe::T(280) + " " + fe::bindingName(s["options"]["bindings"][dev * 34 + 8]);
        if (r.click("Keys")) {
            r.check(r.has(throttle), "the Keys box shows the driver's own throttle binding (" + throttle + ")");
            r.snap("keys_help");
            r.click("OK");
        }
        r.frames(2);
        r.check(r.fe->screenName() == "main", "back on the main menu (" + r.fe->screenName() + ")");
    }

    std::printf("== A game: loading, HUD, pause menu, results, Next race\n");
    if (firstCity >= 0) {
        r.fe->testPlay(firstCity, 1, 0, 0, 0);
        r.frame();
        r.snap("loading");
        for (int i = 0; i < 20 && !r.fe->inGame(); ++i) r.frame();
        r.check(r.fe->inGame() && r.svc.begins == 1, "the game began after its loading screen");
        const std::string hud =
            "{\"visible\":true,\"gauges\":true,\"mph\":57,\"gear\":\"3\",\"automatic\":true,\"rpm\":5200,\"maxRpm\":7000,\"damage\":0.25,"
            "\"timer\":\"00:41.20\",\"place\":[2,8],\"checkpoints\":[3,9],\"camera\":\"near chase\","
            "\"map\":{\"x\":0.78,\"y\":0.62,\"w\":0.2,\"h\":0.35},\"mapMode\":1,\"race\":{\"mode\":1}}";
        r.fe->onEngineEvent("hud", hud);
        r.frames(3);
        r.snap("hud");
        r.fe->onEngineEvent("pause", "{\"menu\":true}");
        r.frames(3);
        r.check(r.focused() == "Resume", "the pause menu takes the focus on Resume (" + r.focused() + ")");
        r.snap("pause");
        r.key(Key::Escape);
        r.check(r.find("Resume") == nullptr, "Esc resumes");
        // The race's end (a stand-in for the session's results; races.md).
        const std::string res =
            "{\"visible\":true,\"race\":{\"mode\":1,\"race\":0,\"title\":\"Test race\",\"resultsShown\":true,\"nextRace\":true,"
            "\"results\":[{\"place\":1,\"racer\":-1,\"time\":83.5},{\"place\":2,\"racer\":0,\"time\":85.25},{\"place\":3,\"racer\":1,\"time\":90.0}]}}";
        r.fe->onEngineEvent("hud", res);
        r.frames(3);
        r.check(r.find("Next race") != nullptr, "the results offer Next race");
        r.snap("results");
        if (r.click("Next race")) r.check(r.svc.nextRaces == 1, "Next race asks the engine for the next race");
        r.fe->testQuit("race");
        r.frames(2);
        r.check(!r.fe->inGame() && r.fe->screenName() == "race", "leaving the game: the race menu (" + r.fe->screenName() + ")");
    }

    std::printf("== Keys while driving\n");
    {
        // A drive started outside the menus (the page's cc_start_drive) while they are open.
        r.key(Key::Escape);
        r.fe->openFrontEnd();
        r.frames(3);
        r.check(r.fe->inPlay() && !r.fe->viewsGetKeys(r.ui), "the menus have the keys while no drive runs");
        r.svc.drivingNow = true;
        r.frames(3);
        r.check(!r.fe->inPlay() && r.svc.drivingNow && r.fe->viewsGetKeys(r.ui),
                "a drive started outside the menus: the viewer's drive (still running), with the keys");
        // (A text field taking the keys from a drive, the viewer's network panel, is checked in the page:
        // this program has no network link.)
        r.svc.drivingNow = false;
        r.fe->onEngineEvent("drive", "{\"active\":false}");
        r.frames(2);
        // The UI does not move or press anything with keys the drive has.
        Ui ui;
        double t = 0;
        int clicks = 0;
        for (int f = 0; f < 4; ++f) {
            FrameInput fi;
            fi.width = 400;
            fi.height = 200;
            fi.scale = 1;
            fi.time = t;
            fi.dt = 1.0f / 60.0f;
            fi.sceneKeys = f >= 1;
            if (f >= 1) {
                InputEvent e;
                e.type = InputEvent::KeyDown;
                e.key = f == 1 ? Key::Space : f == 2 ? Key::Enter : Key::Down;
                fi.events.push_back(e);
            }
            ui.beginFrame(fi);
            Style s;
            s.column();
            ui.open("root", s);
            if (fe::button(ui, "a", "First")) ++clicks;
            fe::button(ui, "b", "Second");
            if (f == 0) ui.setFocus(ui.id("a"), true);
            ui.close();
            ui.endFrame();
            t += 1.0 / 60.0;
        }
        r.check(clicks == 0, "Space and Enter for the drive press no focused button");
        r.check(ui.isFocused(ui.id("a")), "and the arrows do not move the focus");
        r.fe->openFrontEnd();  // back to the menus for the rest
        r.frames(3);
    }

    std::printf("== Window sizes\n");
    for (auto wh : std::vector<std::pair<int, int>>{{1920, 1080}, {390, 844}}) {
        r.w = wh.first;
        r.h = wh.second;
        r.key(Key::Escape);
        r.frames(3);
        r.check(r.fitsWidth(), strf("%dx%d: nothing focusable sticks out sideways", r.w, r.h));
        r.snap(strf("main_%dx%d", r.w, r.h));
    }
    r.w = 1280;
    r.h = 720;

    std::printf("== Sliders (menus.md)\n");
    {
        Ui ui;
        double t = 0;
        float v = 100;
        bool first = true;
        auto build = [&]() {
            fe::slider(ui, "s", 100, 1000, v);
            if (first) {
                ui.setFocus(ui.id("s"), true);
                first = false;
            }
        };
        sliderFrames(ui, t, {Key::Right}, build);
        r.check(std::fabs(v - (100.0f + 900.0f / 19.0f)) < 1e-3f, strf("19 equal steps: 100 … 1000, Right gives %.4f", double(v)));
        sliderFrames(ui, t, {Key::End, Key::Left}, build);
        r.check(std::fabs(v - (100.0f + 18.0f * 900.0f / 19.0f)) < 1e-3f, strf("End, Left: step 18 of 19 (%.4f)", double(v)));
        float cops = 3.0f;  // a race row's count
        bool viaKey = false, focusOnce = true;
        auto copBuild = [&]() {
            fe::mm2Slider(ui, "cops", cops, false, viaKey);
            if (focusOnce) {
                ui.setFocus(ui.id("cops"), true);
                focusOnce = false;
            }
        };
        sliderFrames(ui, t, {}, copBuild);
        r.check(cops == 3.0f, "the cop control leaves a count of 3 untouched until moved");
        sliderFrames(ui, t, {Key::Left}, copBuild);
        r.check(cops == 1.0f - 1.0f / 19.0f, strf("a count of 3, one step down: 18/19 (%.8f)", double(cops)));
        cops = 0.0f;
        std::vector<Key> ups(19, Key::Right);
        sliderFrames(ui, t, ups, copBuild);
        r.check(cops < 1.0f && std::fabs(cops - 0.9999998f) < 1e-7f, strf("19 steps up from 0: %.7f (not 1)", double(cops)));
        sliderFrames(ui, t, {Key::Right}, copBuild);
        r.check(cops == 1.0f, "one more step at the top writes exactly 1.0");
    }

    std::printf("== Quit\n");
    r.key(Key::Escape);
    r.frames(2);
    if (r.click("Quit")) r.check(r.host.quitAsked, "Quit asks the program to close");

    std::printf("== %d snapshot(s) in %s\n", r.pngs, outDir.c_str());
    std::printf("== Summary: %d check(s) failed\n", r.problems);
    return r.problems == 0 ? 0 : 1;
}
