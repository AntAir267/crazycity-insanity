#include "app/front_end.h"

#include <algorithm>
#include <cmath>

#include "app/fe_theme.h"
#include "app/fe_words.h"
#include "app/menu_data.h"
#include "core/strutil.h"
#include "game/player_files.h"

namespace cc {
namespace fe {

using namespace ui;

namespace {
const JValue& nullJ() {
    static const JValue v;
    return v;
}
JValue parse(const std::string& s) { return JValue::parse(s); }
}  // namespace

FrontEnd::FrontEnd(Services& s, Host& host) : svc_(s), host_(host) {
    // The profile store's writes go to the host's storage (the browser keeps the players folder).
    menu::setFileSink([this](const std::string& path, const Bytes* data) {
        static bool warned = false;
        if (!data) {
            host_.storageDel("cci.players/" + path);
        } else if (!host_.storageSet("cci.players/" + path, game::base64Encode(*data)) && !warned) {
            warned = true;
            toast(host_.isBrowser() ? "Your browser does not keep data for this page: drivers and records last until it is closed."
                                    : "Your drivers and records cannot be saved: they last until the program closes.");
        }
    });
}

FrontEnd::~FrontEnd() { menu::setFileSink(nullptr); }

void FrontEnd::toast(const std::string& t) {
    if (ui_) ui_->toast(t);
}

// ------------------------------------------------------------------------------------- state

void FrontEnd::refreshState(const JValue* reply) {
    if (reply) {
        if (reply->has("state")) state_ = (*reply)["state"];
        else if (reply->isObject()) state_ = *reply;
    }
}

void FrontEnd::refreshAll() {
    if (!ready_) return;
    state_ = parse(menu::state());
    cars_ = parse(menu::cars());
    rerender();
}

void FrontEnd::refreshCars() { cars_ = parse(menu::cars()); }

const JValue* FrontEnd::carByIndex(int index) const {
    for (const JValue& c : cars_.items())
        if (c["index"].integer(-2) == index && index >= 0) return &c;
    return nullptr;
}
const JValue* FrontEnd::carByName(const std::string& name) const {
    for (const JValue& c : cars_.items())
        if (c["name"].str() == name) return &c;
    return nullptr;
}
std::string FrontEnd::carDesc(const std::string& name) const {
    const JValue* c = carByName(name);
    return c ? (*c)["description"].str() : name;
}
const FrontEnd::CityInfo* FrontEnd::cityByIndex(int index) const {
    for (const CityInfo& c : cities_)
        if (c.index == index) return &c;
    return nullptr;
}
const FrontEnd::CityInfo* FrontEnd::cityByRaceDir(const std::string& rd) const {
    for (const CityInfo& c : cities_)
        if (toLower(c.raceDir) == toLower(rd)) return &c;
    return nullptr;
}

// The race menu and car screen start from the current driver's last city, car and paint.
void FrontEnd::syncSelFromDriver() {
    const JValue& d = driver();
    if (d.isNull()) return;
    if (const CityInfo* c = cityByRaceDir(d["city"].str())) sel_.city = c->index;
    if (const JValue* car = carByName(d["car"].str())) {
        sel_.car = (*car)["index"].integer();
        sel_.paint = d["paint"].integer();
        carPaint_[(*car)["name"].str()] = sel_.paint;
    }
    loadRaces();
    // The last mode and race (the .sav), when that race is still offered and open.
    const int m = d["mode"].integer();
    sel_.mode = (m == 0 || m == 1 || m == 3 || m == 4) && modeAvailable(m) ? m : 0;
    const JValue& race = sel_.mode ? racesOf(sel_.mode)[d["race"].integer()] : nullJ();
    sel_.race = race["open"].truthy() ? d["race"].integer() : 0;
    resetEnv();
    if (play_ && !inGame_) showCar();
}

void FrontEnd::loadRaces() { races_ = parse(menu::cityRaces(sel_.city)); }

const JValue& FrontEnd::racesOf(int mode) const { return races_["modes"][std::to_string(mode)]; }

bool FrontEnd::modeAvailable(int mode) const { return mode == 0 || racesOf(mode).size() > 0; }

// Defaults when a mode or race is chosen (menus.md, races.md "The session's settings").
void FrontEnd::resetEnv() {
    const int m = sel_.mode;
    Env env;
    if (m != 0) {
        const JValue& r = racesOf(m)[sel_.race]["row"];
        if (r["present"].truthy()) {
            env.tod = r["timeOfDay"].integer() & 3;
            env.weather = r["weather"].integer() & 3;
            env.traffic = r["ambient"].num();
            env.peds = r["peds"].num();
            env.cops = r["cops"].f();
            env.opponents = m == 4 ? 0 : r["opponents"].integer();
            env.maxOpponents = r["opponents"].integer();
            env.laps = m == 3 ? std::max(1, std::min(8, r["laps"].integer())) : 1;
            env.timeLimit = r["timeLimit"].num();
            env.difficulty = r["difficulty"].num();
        }
    }
    sel_.env = env;
    sel_.defaults = env;
}

// Which environment settings can be changed (menus.md, "Locked settings").
bool FrontEnd::editable(const std::string& key) const {
    const int m = sel_.mode;
    if (m == 0) return key != "opponents" && key != "laps";
    const JValue& race = racesOf(m)[sel_.race];
    if (race["locked"].truthy()) return false;
    if (key == "laps") return m == 3;  // Circuit: 1 – 8 once the race is passed (menus.md)
    if (key == "opponents") return m == 3;
    if (key == "traffic" && m == 3) return false;
    return true;
}

bool FrontEnd::ownSettings() const {
    const Env& e = sel_.env;
    const Env& d = sel_.defaults;
    bool same = e.tod == d.tod && e.weather == d.weather && e.traffic == d.traffic && (e.cops > 0) == (d.cops > 0) && e.cops == d.cops;
    if (sel_.mode == 3) same = same && e.laps == d.laps && e.opponents == d.opponents;
    return same;
}

// How to unlock a car or paint job: its rewards line (progression.md, "Locked cars and paint jobs").
std::string FrontEnd::unlockText(const JValue& w, bool isCar) const {
    const std::string what = isCar ? "this car" : "this paint job";
    if (w.isNull()) return "Win more races to unlock " + what + ".";
    const std::string where = w["city"].truthy() ? " in " + w["city"].str() : std::string();
    const int mode = w["mode"].integer();
    const std::string m = mode == 6 ? T(12) : listName(mode);
    const int cond = w["condition"].integer();
    if (cond == -1) return "Pass half of the " + m + " races" + where + " to unlock " + what + ".";
    if (cond == -2) return "Pass every " + m + " race" + where + " to unlock " + what + ".";
    if (mode == 6) {
        const int base = w["raceDir"].str() == "sf" ? 545 : 532;
        return "Pass " + m + " lesson " + std::to_string(cond + 1) + " (" + T(base + cond) + ")" + where + " to unlock " + what + ".";
    }
    return "Pass " + m + " race " + std::to_string(cond + 1) + (w["raceName"].truthy() ? " (" + w["raceName"].str() + ")" : std::string()) +
           where + " to unlock " + what + ".";
}

// The 3D car behind the menus.
void FrontEnd::showCar() {
    if (sel_.car < 0) return;
    svc_.setView(0);
    svc_.selectCar(sel_.car);
    svc_.setPaint(sel_.paint);
    svc_.setCarOption("autorotate", 1);
}

// ------------------------------------------------------------------------------------- program

void FrontEnd::loadStrings(const uint8_t* data, size_t size, bool keep) {
    const JValue r = parse(menu::loadMmlang(data, size));
    if (!r["ok"].truthy()) return;
    if (keep) host_.storageSet("cci.mmlang", game::base64Encode(Bytes(data, data + size)));
    if (keep) refreshAll();
}

void FrontEnd::onMounted(const std::string& summaryJson) {
    v_.summary = parse(summaryJson);
    // The viewer's lists (web/app.js mountAll).
    v_.overlay = false;
    v_.carList = parse(svc_.carList());
    {
        int idx = -1;
        for (const JValue& c : v_.carList.items())
            if (c["name"].str() == v_.selectedName) idx = c["index"].integer();
        if (idx < 0 && v_.carList.size()) idx = 0;
        if (idx >= 0) viewerSelectCar(idx);
        else if (!v_.carList.size()) {
            v_.overlay = true;
            v_.overlayTitle = "No cars found";
            v_.overlayText = "The loaded archives have no tune/*.info files.";
        }
    }
    {
        const JValue cl = parse(svc_.cityList());
        cities_.clear();
        int i = 0;
        for (const JValue& c : cl.items()) {
            CityInfo ci;
            ci.index = i++;
            ci.name = c["name"].str();
            ci.map = c["map"].str();
            ci.raceDir = c["raceDir"].str();
            ci.test = c["test"].truthy();
            cities_.push_back(ci);
        }
        v_.cityLoaded = false;
        v_.loadedCity = -1;
        v_.cityStatus = cities_.empty() ? std::string() : "Pick a city to load it.";
        refreshRaces();
    }
    // The front end (web/ui.js onMounted).
    ready_ = false;
    if (!v_.summary["cars"].truthy()) return;
    const std::string saved = host_.storageGet("cci.mmlang");
    if (!saved.empty()) {
        Bytes b;
        if (game::base64Decode(saved, b)) {
            const JValue r = parse(menu::loadMmlang(b.data(), b.size()));
            (void)r;
        }
    }
    // The players folder this browser keeps.
    menu::playersClear();
    for (const std::string& k : host_.storageKeys("cci.players/")) {
        const std::string v = host_.storageGet(k);
        if (!v.empty()) menu::playersPut(k.substr(12), v);
    }
    // The game controller found at start-up decides the wheel's default bindings (controls.md).
    menu::setControllerButtons(svc_.gamepadButtons());
    state_ = parse(menu::start());
    svc_.afterStart();
    // The front end lists the game's cities (not the test layouts).
    std::vector<CityInfo> all = cities_;
    cities_.clear();
    for (const CityInfo& c : all)
        if (!c.test) cities_.push_back(c);
    viewerCitiesAll_ = all;
    cars_ = parse(menu::cars());
    ready_ = state_.isObject();
    rerender();
    if (ready_ && host_.hasParam("play")) openFrontEnd();
}

void FrontEnd::importFinished() {
    const JValue r = parse(menu::importFinish());
    if (r.isNull()) return;
    refreshState(&r);
    refreshCars();
    syncSelFromDriver();
    std::vector<std::string> notes;
    for (const JValue& n : r["notes"].items()) notes.push_back(n.str());
    const int n = r["drivers"].integer();
    reportBox(r["ok"].truthy() ? "Imported " + std::to_string(n) + " driver" + (n == 1 ? "" : "s") : "Nothing imported", notes);
    rerender();
}

void FrontEnd::setLoadStatus(const std::string& text, float progress) {
    v_.status = text;
    v_.progress = progress;
}

void FrontEnd::setOverlay(const std::string& title, const std::string& text) {
    v_.overlay = !title.empty();
    v_.overlayTitle = title;
    v_.overlayText = text;
}

void FrontEnd::onLog(int level, const std::string& kind, const std::string& msg) {
    (void)kind;
    for (auto& l : v_.log)
        if (l.level == level && l.msg == msg) {
            ++l.count;
            return;
        }
    v_.log.push_back(Viewer::LogLine{level, msg, 1});
    if (v_.log.size() > 3000) v_.log.erase(v_.log.begin());
    if (level >= 1) ++v_.problems;
    logScroll_ = true;
}

// ------------------------------------------------------------------------------------- navigation

void FrontEnd::openFrontEnd() {
    if (!ready_) return;
    play_ = true;
    history_.clear();
    refreshAll();
    sel_.mode = 0;
    syncSelFromDriver();
    if (sel_.car < 0 && cars_.size()) sel_.car = cars_[0]["index"].integer();
    if (!cityByIndex(sel_.city) && !cities_.empty()) sel_.city = cities_[0].index;
    sel_.mode = 0;
    loadRaces();
    resetEnv();
    showCar();
    resetCameraIn_ = 8;  // the 3D view grew when the panel went away: frame the car again
    go("main", true);
    if (pendingFolder_) {
        pendingFolder_ = false;
        confirmBox("Import MM2 profiles?",
                   std::string("The folder you opened has a players folder. Import its drivers, records and halls of fame ") +
                       (host_.isBrowser() ? "into this browser" : "here") + "? The drivers kept here now are replaced.",
                   "Import", [this]() { host_.pick(Pick::HeldPlayers); });
    }
}

void FrontEnd::openScreen(const std::string& screen) {
    openFrontEnd();
    if (play_ && screen != "main") go(screen);
}

void FrontEnd::closeFrontEnd() {
    mpRelease();  // a network session left open is the viewer's Network tab's from here
    renderedScreen_.clear();
    play_ = false;
    dim_ = false;
    // Back to the viewer: its own view and draw settings.
    restoreViewerState();
    restoreViewerOptions();
    restoreViewerAudio();
}

void FrontEnd::go(const std::string& screen, bool root) {
    if (!root) history_.push_back(screen_);
    else history_.clear();
    screen_ = screen;
    rerender();
}

void FrontEnd::back() {
    if (modal_ != Modal::None) {
        closeModal();
        return;
    }
    if (!history_.empty()) {
        screen_ = history_.back();
        history_.pop_back();
        rerender();
    } else if (screen_ != "main") {
        go("main", true);
    }
}

// ------------------------------------------------------------------------------------- dialogs

void FrontEnd::confirmBox(const std::string& title, const std::string& text, const std::string& ok, std::function<void()> onOk) {
    modal_ = Modal::Confirm;
    modalTitle_ = title;
    modalText_ = text;
    modalOk_ = ok;
    modalOnOk_ = std::move(onOk);
    if (ui_) ui_->focusFirst();
}

void FrontEnd::reportBox(const std::string& title, const std::vector<std::string>& notes) {
    modal_ = Modal::Report;
    modalTitle_ = title;
    modalNotes_ = notes;
    if (ui_) ui_->focusFirst();
}

void FrontEnd::confirmImport(std::function<void()> run) {
    const size_t n = state_["drivers"].size();
    if (!n) {
        run();
        return;
    }
    confirmBox("Replace the drivers kept here?",
               "Importing replaces the " + std::to_string(n) + " driver" + (n == 1 ? "" : "s") + " kept " +
                   (host_.isBrowser() ? "in this browser" : "on this computer") +
                   ", with their records and halls of fame, by your MM2 profiles.",
               "Import", std::move(run));
}

// ------------------------------------------------------------------------------------- controls

// The Keys box: in the menus and games the driver's own binding table for its control device
// (controls.md; a game drives by it), in the viewer the Drive mode's keys.
void FrontEnd::keysHelp(Ui& ui) {
    ui.text("Keys", tH());
    const JValue& o = options();
    const bool table = play_ && !o.isNull() && o["bindings"].size() >= size_t(5 * kActions);
    std::vector<TableCol> cols = {{"", Track::autoW(), false}, {"", Track::fr(1), false}};
    std::vector<std::pair<std::string, std::string>> rows;
    if (table) {
        const int dev = std::max(0, std::min(4, o["controlDevice"].integer(1)));
        sub(ui, driver()["name"].str() + " drives with the " + T(580 + dev) + " table; Options › Customise controls changes it.");
        auto join = [&](std::initializer_list<int> acts) {
            std::string out;
            for (int a : acts) {
                const JValue& e = o["bindings"][dev * kActions + a];
                if (!e[0].integer()) continue;
                if (!out.empty()) out += " · ";
                out += T(kActionText[a]) + " " + bindingName(e);
            }
            return out;
        };
        rows = {{"Drive", dev == 1 ? join({8, 9, 6, 7, 10}) : join({8, 9, 5, 6, 7, 10})},
                {"Gearbox", join({20, 21, 22, 23})},
                {"Views", join({11, 12, 18, 19, 30})},
                {"Look", join({14, 15, 16, 17, 31})},
                {"Map and HUD", join({0, 1, 2, 3, 4, 32})},
                {"Races", join({24, 25, 13})},
                {"Network games", join({33}) + " · F6 scoreboard (the game never pauses)"},
                {"Game", "Esc pause menu, F2 pause, F4 restart, G puts the car back on its wheels"}};
    } else {
        sub(ui, "The viewer's Drive mode. A game drives with the driver's own controls (PLAY › Options › Customise controls).");
        rows = {{"Drive", "↑ ↓ ← → or W A S D, Space handbrake"},
                {"Gearbox", "T automatic / manual, E / Q shift, R reverse"},
                {"Views", "C camera, F cockpit, V orbit, B wide view, Backspace mirror"},
                {"HUD", "H gauges, I opponent icons"},
                {"Map", "Tab map size, M full-screen map (pauses), K zoom, O north / heading up"},
                {"Races", "N / X next / previous checkpoint (Checkpoint races)"},
                {"Game", "Esc pause menu, F2 pause, F4 restart, G put the car back on its wheels"}};
    }
    beginTable(ui, cols);
    for (auto& r : rows)
        if (!r.second.empty()) tableRow(ui, cols, {r.first, r.second}, false, {true, false});
    endTable(ui);
    if (!table) note(ui, "A gamepad works too: left stick steers, A accelerates, B brakes, triggers are analog pedals.");
    beginRow(ui, Justify::End);
    if (button(ui, "ok", "OK")) closeModal();
    endRow(ui);
}

void FrontEnd::startBind(int device, int action) {
    bindDev_ = device;
    bindAction_ = action;
    bindFrames_ = 0;
    bindHat_ = 0;
    bindWarn_.clear();
    modal_ = Modal::Bind;
}

// Capturing one binding: the next key, controller button or moved axis (the hat for the
// look-around axis); the mouse device's mouse inputs by choice. Esc keeps the old binding.
void FrontEnd::bindFrame(Ui& ui) {
    const int a = bindAction_, dev = bindDev_;
    const bool held = heldAction(a);
    ui.clearFocus();  // the keys go to the capture, not to the dialog's buttons
    ui.text(T(kActionText[a]), tH());
    const JValue& o = cached("state", []() { return menu::state(); })["options"];
    const JValue& cur = o["bindings"][dev * kActions + a];
    sub(ui, T(580 + dev) + " · now " + (cur[0].integer() ? bindingName(cur) : std::string("no binding")));
    std::string what = "Press a key";
    if (dev >= 2) what += held ? std::string(", a button or move an axis") + (a == 31 ? " or the hat" : "") : std::string(" or a button");
    note(ui, what + ". Esc keeps the binding.");
    int mouse = 0;  // the mouse device's mouse inputs, chosen here (a click would also be a click)
    if (dev == 0) {
        beginRow(ui, Justify::Start, 8, 10);
        if (button(ui, "ml", T(310), Btn::Ghost, false, false)) mouse = 1;
        if (button(ui, "mr", T(311), Btn::Ghost, false, false)) mouse = 2;
        if (held && button(ui, "mx", T(312), Btn::Ghost, false, false)) mouse = 10;
        if (held && button(ui, "my", T(313), Btn::Ghost, false, false)) mouse = 11;
        endRow(ui);
    }
    beginRow(ui, Justify::End, 8, 14);
    if (button(ui, "none", "No binding", Btn::Ghost, false, false)) {
        menu::setBinding(dev, a, 0, 0, 0);
        closeModal();
        rerender();
    }
    if (button(ui, "cancel", "Cancel", Btn::Ghost, false, false)) closeModal();
    endRow(ui);
    if (mouse) assignBinding(2, mouse);
    if (modal_ != Modal::Bind) return;
    // This frame's raw input (the first frame only notes where the axes and the hat rest: the key
    // or click that opened the capture is not taken).
    const RawInput in = svc_.rawInput();
    if (bindFrames_++ == 0) {
        for (int k = 0; k < 8; ++k) bindRest_[k] = in.axis[k];
        bindHat_ = in.hat;
        return;
    }
    for (int k : in.keys) {
        if (k == 0x01) closeModal();
        else assignBinding(3, k);
        return;
    }
    if (dev < 2 || !in.padConnected) return;
    if (!in.buttons.empty()) {
        assignBinding(4, 21 + in.buttons[0]);
        return;
    }
    if (!held) return;
    if (a == 31 && in.hat && !bindHat_) {
        assignBinding(4, 16);
        return;
    }
    if (!in.hat) bindHat_ = 0;
    // Axes 0 … 5 as controls.md names them (X, Y, Z, R, U, V); the steering and look-around axes
    // take a whole axis, the other held actions the moved half of X or Y, or a whole other axis (a
    // wheel's separate pedals).
    static const int whole[6] = {10, 11, 12, 14, 13, 15};
    const int n = std::min(6, std::max(in.axisCount, 4));
    for (int k = 0; k < n; ++k) {
        const float d = in.axis[k] - bindRest_[k];
        if (std::fabs(d) < 0.6f) continue;
        int comp = whole[k];
        if (a != 5 && a != 31 && k < 2) comp = k == 0 ? (d < 0 ? 17 : 18) : (d < 0 ? 19 : 20);
        assignBinding(4, comp);
        return;
    }
}

void FrontEnd::assignBinding(int inputDevice, int component) {
    const int a = bindAction_, dev = bindDev_;
    const bool axis = (inputDevice == 4 && component >= 10 && component <= 20) || (inputDevice == 2 && component >= 10);
    const int kind = heldAction(a) ? (axis ? 2 : 1) : 3;
    menu::setBinding(dev, a, kind, inputDevice, component);
    closeModal();
    rerender();
    // A warning when the input is used by another action of the device too (menus.md).
    const JValue st = parse(menu::state());
    std::string others;
    for (int b = 0; b < kActions; ++b) {
        const JValue& e = st["options"]["bindings"][dev * kActions + b];
        if (b != a && e[0].integer() && e[1].integer() == inputDevice && e[2].integer() == component)
            others += (others.empty() ? "" : ", ") + T(kActionText[b]);
    }
    bindWarn_ = others.empty() ? std::string() : bindingName(kind, inputDevice, component) + " is also used for " + others + ".";
}

void FrontEnd::buildModal(Ui& ui) {
    if (modal_ == Modal::None) return;
    ui.layer(Layer::Popup);
    ui.setModal(Layer::Popup);
    Style ov;
    ov.absolute = true;
    ov.left = ov.top = ov.right = ov.bottom = 0;
    ov.bg = rgba(2, 4, 8, 0.6f);
    ov.backdropBlur = 4;
    ov.align = Align::Center;
    ov.justify = Justify::Center;
    ov.hit = true;
    ov.stack();
    ui.open("modal", ov);
    Style d;
    d.column();
    d.w = std::min(520.0f, ui.viewW() - 32);
    d.maxH = ui.viewH() - 60;
    d.scrollY = true;
    d.bg = col::panelSolid;
    d.outline(1, col::lineStrong);
    d.round(18);
    d.pad = Edges(22);
    d.shadow(0, 30, 80, 0, rgba(0, 0, 0, 0.5f));
    d.hit = true;
    ui.open("dialog", d);
    auto closeRow = [&]() { beginRow(ui, Justify::End); };
    switch (modal_) {
        case Modal::Confirm: {
            Style h = tH();
            ui.text(modalTitle_, h);
            sub(ui, modalText_);
            closeRow();
            if (button(ui, "cancel", "Cancel", Btn::Ghost)) closeModal();
            if (button(ui, "ok", modalOk_)) {
                closeModal();
                if (modalOnOk_) {
                    auto f = modalOnOk_;
                    f();
                }
            }
            endRow(ui);
            break;
        }
        case Modal::Report: {
            ui.text(modalTitle_, tH());
            if (!modalNotes_.empty()) {
                for (const std::string& n : modalNotes_) {
                    Style r = tNote();
                    r.mar = Edges(2, 0, 2, 0);
                    ui.rich({span("•  ", 12, 400), span(n, 12, 400)}, r);
                }
            } else {
                note(ui, "Every file passed MM2's checks.");
            }
            closeRow();
            if (button(ui, "ok", "OK")) closeModal();
            endRow(ui);
            break;
        }
        case Modal::Keys: keysHelp(ui); break;
        case Modal::Bind: bindFrame(ui); break;
        case Modal::NewDriver: {
            ui.text("New driver", tH());
            const std::string count = std::to_string(utf8Length(newName_)) + " / 18";
            unsigned ed = 0;
            field(ui, "Name", count, [&]() {
                InputOpts o;
                o.max = 18;
                ed = input(ui, "name", newName_, o);
            });
            if (newFocus_) {
                ui.setFocus(ui.id("name"));
                newFocus_ = false;
            }
            field(ui, "Skill level", "", [&]() { seg(ui, "skill", choices({T(82), T(81)}), newSkill_); });
            note(ui, "The skill level cannot be changed later. Professional races are harder to pass and have their own hall of fame.");
            if (!newMsg_.empty()) msg(ui, newMsg_, Msg::Bad);
            closeRow();
            if (button(ui, "cancel", "Cancel", Btn::Ghost)) closeModal();
            const bool create = button(ui, "create", "Create") || (ed & Ui::EditSubmit);
            endRow(ui);
            if (create) {
                const std::string n = trim(newName_);
                if (!n.empty()) {  // an empty name: nothing happens
                    const JValue r = parse(menu::newDriver(n, newSkill_));
                    if (!r["ok"].truthy()) {
                        const std::string why = r["why"].str();
                        newMsg_ = why == "duplicate" ? "A driver with that name exists already."
                                  : why == "full"    ? "There are 18 drivers already; delete one first."
                                                     : "The driver could not be created.";
                    } else {
                        refreshState(&r);
                        refreshCars();
                        syncSelFromDriver();
                        closeModal();
                        rerender();
                    }
                }
            }
            break;
        }
        default: break;
    }
    ui.close();
    ui.close();
    ui.layer(Layer::Base);
}

// ------------------------------------------------------------------------------------- a game

void FrontEnd::startGame() {
    Spec s;
    s.city = sel_.city;
    s.mode = sel_.mode;
    s.race = sel_.race;
    startRaceSpec(s);
}

// The interface the race menus use: {city, mode, race, skill, car, paint, settings,
// ownSettings} starts Cruise or a race session (game/race.h) in the engine.
void FrontEnd::startRaceSpec(Spec spec) {
    if (mpOpen()) {
        toast("You are in a network game: leave it first (Multiplayer).");
        return;
    }
    Spec full = spec;
    full.skill = driver().isNull() ? 0 : driver()["skill"].integer();
    if (full.car < 0) {
        full.car = sel_.car;
        full.paint = sel_.paint;
    }
    if (!full.haveSettings) {
        full.settings = sel_.env;
        full.ownSettings = sel_.mode == 0 ? true : ownSettings();
        if (spec.mode == 6) full.ownSettings = true;  // Crash Course: the lesson's own settings
    }
    if (!spec.fixedCar.empty()) {
        if (const JValue* fc = carByName(spec.fixedCar)) {
            full.car = (*fc)["index"].integer();
            full.paint = 0;
        }
    }
    const CityInfo* city = cityByIndex(full.city);
    loading_ = true;
    loadTitle_ = city ? city->name : "Loading";
    loadSub_ = listName(full.mode);
    loadFrames_ = 0;
    pendingStart_ = true;
    pendingSpec_ = full;
}

void FrontEnd::doStart() {
    pendingStart_ = false;
    Spec full = pendingSpec_;
    // The city (loaded as the viewer would, which keeps the viewer's panel in step).
    if (v_.loadedCity != full.city) {
        const JValue info = parse(svc_.loadCity(full.city));
        afterCityLoaded(full.city, info, std::string());
        if (!info["loaded"].truthy()) {
            loading_ = false;
            toast("The city could not be loaded: " + info["message"].str("see the viewer's problems list"));
            return;
        }
    }
    applyGraphics();
    applyAudio();
    const Env& st = full.settings;
    // Pedestrian density (0 … 1 → percent) and the driver's "pedestrians off" option.
    svc_.setDriveOption("peds", int(std::lround(st.peds * 100.0)));
    svc_.setDriveOption("pedsoff", options()["pedestriansOff"].truthy() ? 1 : 0);
    if (full.mode == 0) {
        svc_.setCityOption("tod", st.tod);
        svc_.setCityOption("weather", st.weather);
        svc_.setDriveOption("traffic", int(std::lround(st.traffic * 100.0)));
        svc_.setCopDensity(st.cops);  // the slider's value (police.md, "How many cops")
        svc_.setEnvironment(1, -1, -1, -1, -1, -1, -1);
    } else {
        // The race's own settings, or the player's where the race allows changes (no record then);
        // the pedestrian density is the player's either way.
        if (full.ownSettings) svc_.setEnvironment(1, -1, -1, -1, -1, -1, st.peds);
        else svc_.setEnvironment(0, st.tod, st.weather, st.traffic, st.cops, full.mode == 3 ? st.laps : -1, st.peds);
    }
    const int opp = full.mode == 3 ? (st.opponents ? st.opponents : -1) : -1;
    const JValue r = parse(svc_.gameBegin(full.city, full.mode, full.race, full.car, full.paint, opp));
    loading_ = false;
    if (!r["ok"].truthy()) {
        toast("Cannot start: " + (r.isNull() ? std::string("engine error") : r["why"].str()));
        return;
    }
    game_ = full;
    inGame_ = true;
    resultsOpen_ = endMenuOpen_ = false;
    v_.driving = true;
    dim_ = false;
    state_ = parse(menu::state());
    sceneFocus_ = true;
    hud_ = JValue();
}

// The driver's graphics options in the engine.
void FrontEnd::applyGraphics() {
    const JValue& o = options();
    if (o.isNull()) return;
    svc_.setCityOption("detail", o["objectDetail"].integer());
    svc_.setCityOption("sky", o["texturedSky"].truthy() ? 1 : 0);
    svc_.setCityOption("reflections", o["reflections"].truthy() ? 1 : 0);
}

// The driver's audio options in the engine at game start (menus.md: the switches only in the
// menus; volumes and balance also live in the game).
void FrontEnd::applyAudio() {
    const JValue& o = options();
    if (o.isNull()) return;
    const int b = o["audioBits"].integer();
    svc_.setSoundOption("effects", b & 0x001 ? 1 : 0);
    svc_.setSoundOption("commentary", b & 0x400 ? 1 : 0);
    svc_.setSoundOption("stereo", b & 0x040 ? 1 : 0);
    svc_.setSoundOption("volume", int(std::lround(o["sfxVolume"].num() * 100)));
    svc_.setSoundOption("balance", int(std::lround(o["balance"].num() * 100)));
    svc_.setMusicMode(b & 0x004 ? 1 : (b & 0x800) ? 2 : 0);
    svc_.setMusicVolume(o["musicVolume"].f());
}

void FrontEnd::testPlay(int city, int mode, int race, int car, int paint, const PlayEnv* env) {
    Spec s;
    s.city = city;
    s.mode = mode;
    s.race = race;
    s.car = car;
    s.paint = paint;
    if (env) {
        // The race menu set to this race, then its settings changed where asked.
        sel_.city = city;
        loadRaces();
        sel_.mode = mode;
        sel_.race = race;
        resetEnv();
        Env& e = sel_.env;
        if (env->tod >= 0) e.tod = env->tod & 3;
        if (env->weather >= 0) e.weather = env->weather & 3;
        if (env->traffic >= 0) e.traffic = std::min(1.0, env->traffic);
        if (env->cops >= 0) e.cops = float(std::min(1.0, env->cops));
        if (env->peds >= 0) e.peds = std::min(1.0, env->peds);
        if (env->opponents >= 0 && mode == 3) e.opponents = std::min(env->opponents, std::max(e.maxOpponents, 0));
        s.settings = e;
        s.haveSettings = true;
        s.ownSettings = mode == 0 || mode == 6 || ownSettings();
    }
    startRaceSpec(s);
}

void FrontEnd::profilesChanged() {
    if (!ready_) return;
    refreshAll();
    syncSelFromDriver();
}

void FrontEnd::endGame(const std::string& toScreen) {
    pause_ = Pause::None;
    const JValue s = parse(svc_.gameEnd());
    inGame_ = false;
    v_.driving = false;
    refreshState(&s);
    refreshCars();
    loadRaces();
    // Entering the menus after a game loads the settings of the driver's last mode and race again
    // (menus.md, "Defaults when a mode or race is chosen"): a changed setting lasts one game only.
    if (!driver().isNull()) syncSelFromDriver();
    else resetEnv();
    hud_ = JValue();
    showCar();
    screen_ = toScreen.empty() ? std::string("race") : toScreen;
    history_.assign(1, "main");
    rerender();
    sceneFocus_ = false;
}

// ------------------------------------------------------------------------------------- pause menu

void FrontEnd::showPauseMenu(bool raceOver) {
    if (netGame_) {
        pause_ = Pause::NetPause;
        if (ui_) ui_->focusFirst();
        return;
    }
    pause_ = Pause::Menu;
    pauseRaceOver_ = raceOver;
    if (ui_) ui_->focusFirst();
}

void FrontEnd::resume() {
    pause_ = Pause::None;
    svc_.gamePause(false);
    sceneFocus_ = true;
}

void FrontEnd::buildPause(Ui& ui) {
    if (pause_ == Pause::None) return;
    ui.layer(Layer::Overlay);
    ui.setModal(Layer::Overlay);
    Style ov;
    ov.absolute = true;
    ov.left = ov.top = ov.right = ov.bottom = 0;
    ov.bg = rgba(3, 6, 10, 0.55f);
    ov.backdropBlur = 3;
    ov.stack();
    ov.align = Align::Center;
    ov.justify = Justify::Center;
    ov.hit = true;
    ui.open("pause", ov);
    float width = 420;
    if (pause_ == Pause::Options) width = 520;
    if (pause_ == Pause::Results || pause_ == Pause::NetResults) width = 560;
    Style box;
    box.column();
    box.w = std::min(width, ui.viewW() - 32);
    box.maxH = ui.viewH() - 40;
    box.scrollY = true;
    box.bg = col::panelSolid;
    box.outline(1, col::lineStrong);
    box.round(18);
    box.pad = Edges(22);
    box.hit = true;
    ui.open("pausebox", box);
    const bool esc = ui.backPressed();
    switch (pause_) {
        case Pause::Menu: {
            ui.text(pauseRaceOver_ ? "Race over" : "Paused", tH());
            sub(ui, driver()["name"].str());
            Style m;
            m.column();
            m.gap = 8;
            ui.open(m);
            if (!pauseRaceOver_ && item(ui, "resume", "▶", "Resume", "Esc", true)) resume();
            if (item(ui, "restart", "↺", "Restart", "F4", pauseRaceOver_)) {
                svc_.gameRestart();
                resume();
            }
            if (item(ui, "options", "⚙", "Options", "Audio, controls, graphics")) {
                pause_ = Pause::Options;
                ui.focusFirst();
            }
            if (item(ui, "quit", "☰", "Quit to the race menu", "")) endGame("race");
            if (item(ui, "viewer", "⌂", "Back to the viewer", "")) {
                endGame("main");
                closeFrontEnd();
            }
            ui.close();
            break;
        }
        case Pause::Options: optionsPanel(ui, true); break;
        case Pause::Results: {
            sec(ui, modeName(results_.mode), 0);
            ui.text(results_.title, tH());
            std::vector<TableCol> cols = {{"", Track::autoW(), false}, {"", Track::fr(1), false}, {"", Track::autoW(), true}};
            beginTable(ui, cols, 12, 12);
            for (size_t i = 0; i < results_.rows.size(); ++i) tableRow(ui, cols, results_.rows[i], results_.me[i]);
            endTable(ui);
            if (!results_.message.empty()) msg(ui, results_.message, Msg::Ok);
            beginRow(ui, Justify::End);
            if (button(ui, "restart", "Restart", Btn::Ghost)) {
                pause_ = Pause::None;
                svc_.gameRestart();
                sceneFocus_ = true;
            }
            if (button(ui, "menu", "Race menu", Btn::Ghost)) endGame("race");
            if (results_.nextRace && button(ui, "next", "Next race")) {
                pause_ = Pause::None;
                nextRace();
            }
            endRow(ui);
            break;
        }
        case Pause::NetPause: buildNetPause(ui); break;
        case Pause::NetQuit: buildNetQuit(ui); break;
        case Pause::NetResults: buildNetResults(ui); break;
        default: break;
    }
    ui.close();
    ui.close();
    ui.layer(Layer::Base);
    // Esc on the pause menu resumes (the network menus go back a step).
    if (esc && modal_ == Modal::None) {
        if (pause_ == Pause::Menu || pause_ == Pause::Options) resume();
        else if (pause_ == Pause::NetPause) resume();
        else if (pause_ == Pause::NetQuit) pause_ = Pause::NetPause;
    }
}

// ------------------------------------------------------------------------------------- results

// The race session's results and end-of-race menu (races.md, "Losing, finishing and the menus").
void FrontEnd::raceMenus(const JValue& r) {
    if (r.isNull()) return;
    if (!r["resultsShown"].truthy()) resultsOpen_ = false;
    if (!r["endMenu"].truthy()) endMenuOpen_ = false;
    if (r["resultsShown"].truthy() && !resultsOpen_) {
        resultsOpen_ = true;
        const std::string me = driver().isNull() ? T(3) : driver()["name"].str();
        results_ = ResultsView();
        results_.mode = r["mode"].integer();
        for (const JValue& x : r["results"].items()) {
            const bool mine = x["racer"].integer() < 0;
            const bool passMark = x["passMark"].truthy();
            results_.rows.push_back({std::to_string(x["place"].integer()), mine ? me : T(13 + x["racer"].integer()),
                                     passMark ? T(651) : fmtTime(x["time"].num())});
            results_.me.push_back(mine);
        }
        const JValue out = parse(menu::lastOutcome());
        std::string message = out["reward"].truthy() ? out["rewardMessage"].str() : std::string();
        if (message.empty() && out["have"].truthy() && out["recorded"].truthy()) {
            const int score = out["score"].integer();
            message = r["mode"].integer() == 6 ? (out["passed"].truthy() ? "Lesson passed and recorded." : "Recorded as tried.")
                                               : "Recorded: " + fmtThousands(score) + (score == 1 ? " point." : " points.");
        } else if (message.empty() && out["have"].truthy() && out["why"].truthy()) {
            message = "Not recorded: " + out["why"].str() + ".";
        }
        results_.message = message;
        // A lesson's name comes from the text file (532 – 544 London, 545 – 557 San Francisco).
        const CityInfo* city = cityByIndex(game_.city);
        std::string title = r["title"].str();
        if (results_.mode == 6 && city && (city->raceDir == "sf" || city->raceDir == "london"))
            title = T((city->raceDir == "sf" ? 545 : 532) + r["race"].integer());
        results_.title = modeName(results_.mode) + " · " + title;
        results_.nextRace = r["nextRace"].truthy();
        pause_ = Pause::Results;
        if (ui_) ui_->focusFirst();
    } else if (r["endMenu"].truthy() && !endMenuOpen_) {
        endMenuOpen_ = true;
        showPauseMenu(true);
    }
}

void FrontEnd::nextRace() {
    const JValue res = parse(svc_.gameNextRace(game_.city, game_.car, game_.paint));
    if (!res["ok"].truthy()) {
        toast("Cannot start the next race: " + (res.isNull() ? std::string("engine error") : res["why"].str()));
        endGame("race");
        return;
    }
    resultsOpen_ = endMenuOpen_ = false;
    state_ = parse(menu::state());
    sceneFocus_ = true;
}

// ------------------------------------------------------------------------------------- events

void FrontEnd::onEngineEvent(const std::string& name, const std::string& json) {
    if (name == "hud") {
        hud_ = parse(json);
        hudVisible_ = hud_["visible"].truthy();
        if (hud_["visible"].truthy()) {
            v_.automatic = hud_["automatic"].truthy();
            if (inGame_) raceMenus(hud_["race"]);
        }
    } else if (name == "race") {
        v_.raceState = parse(json);
        if (!v_.raceState["active"].truthy()) v_.racing = false;
        else if (inGame_) v_.racing = true;
    } else if (name == "pause") {
        const JValue d = parse(json);
        if (!inGame_) return;
        if (d["menu"].truthy()) showPauseMenu();
        else if (!d["restarted"].truthy()) pause_ = Pause::None;
    } else if (name == "mirror") {
        const JValue d = parse(json);
        v_.mirror = d["on"].truthy();
        host_.storageSet("cci.mirror", v_.mirror ? "1" : "0");
    } else if (name == "drive") {
        const JValue d = parse(json);
        if (d["net"].truthy()) mp_.game = JValue();
        if (!d["active"].truthy()) {
            setDriving(false);
            // A drive stopped from elsewhere: leave the game cleanly.
            if (netGame_) netGameOver();
            else if (inGame_) {
                inGame_ = false;
                hud_ = JValue();
            }
            v_.racing = false;
            v_.raceState = JValue();
        }
    } else if (name == "net") {
        mp_.state = parse(json);
        mpOnState();
    } else if (name == "netgame") {
        mp_.game = parse(json);
        if (mp_.game["active"].truthy() && !netGame_ && play_ && ready_) netGameBegin();
    } else if (name == "netnotice") {
        netNotices(parse(json));
    } else if (name == "netstart") {
        const JValue d = parse(json);
        if (d["ok"].truthy()) {
            setDriving(true);
            sceneFocus_ = true;
        } else {
            // A start that fails for the city: 71 (the game started in a city this machine lacks).
            const std::string why = d["why"].str();
            if (toLower(why).find("city") != std::string::npos) line(T(71));
            else line("Cannot start the game: " + why);
        }
    }
}

// ------------------------------------------------------------------------------------- the frame

void FrontEnd::frame(Ui& ui) {
    ui_ = &ui;
    dim_ = false;
    if (resetCameraIn_ > 0 && --resetCameraIn_ == 0) svc_.resetCamera();
    viewerTick();
    netFrame();
    if (pendingStart_ && ++loadFrames_ >= 3) doStart();
    if (play_) {
        if (!inGame_) buildFrontEnd(ui);
    } else {
        buildViewer(ui);
    }
    buildHud(ui);
    buildViewerOverlays(ui);
    buildPause(ui);
    const bool capturing = modal_ == Modal::Bind;
    buildModal(ui);
    buildLoading(ui);
    // Keys (a binding capture that ended this frame took this frame's Esc or controller button).
    if (!capturing && ui.backPressed()) {
        if (modal_ == Modal::None && pause_ == Pause::None && play_ && !inGame_ && !loading_ && !mp_.chatOpen) back();
        else if (modal_ != Modal::None) closeModal();
    }
    if (netGame_ && pause_ == Pause::None && !mp_.chatOpen && !ui.wantsKeyboard()) {
        if (ui.keyPressed(Key::F6)) mp_.boardOn = !mp_.boardOn;
        else if (ui.keyPressed(Key::Y)) openNetChat();
    }
}

void FrontEnd::buildLoading(Ui& ui) {
    if (!loading_) return;
    ui.layer(Layer::Modal);
    ui.setModal(Layer::Modal);
    Style ov;
    ov.absolute = true;
    ov.left = ov.top = ov.right = ov.bottom = 0;
    ov.radial(rgba(13, 31, 36, 0.96f), rgba(4, 6, 10, 0.98f));
    ov.stack();
    ov.align = Align::Center;
    ov.justify = Justify::Center;
    ov.hit = true;
    ui.open("loading", ov);
    Style box;
    box.column();
    box.w = std::min(460.0f, ui.viewW() * 0.8f);
    ui.open(box);
    Style t = tBody(26, 800);
    t.textAlign = TextAlign::Center;
    ui.text(loadTitle_, t);
    Style s = tBody(14, 400, col::muted);
    s.textAlign = TextAlign::Center;
    s.mar = Edges(6, 0, 18, 0);
    ui.text(loadSub_.empty() ? " " : loadSub_, s);
    Style bar;
    bar.h = 6;
    bar.round(3);
    bar.bg = rgba(255, 255, 255, 0.08f);
    bar.clip = true;
    Node& b = ui.leaf(bar);
    const double t0 = std::fmod(ui.time(), 1.1) / 1.1;
    const float e = float(t0 < 0.5 ? 2 * t0 * t0 : 1 - std::pow(-2 * t0 + 2, 2) / 2);  // ease-in-out
    b.paint = [e](DrawList& dl, const Rect& r, float sc) {
        const float w = r.w * 0.3f;
        const float x = r.x - w + (r.w * 0.3f * 4.4f) * e;
        dl.pushClip(r);
        dl.rect(Rect{x, r.y, w, r.h}, col::accent, 3 * sc);
        dl.popClip();
    };
    ui.close();
    ui.close();
    ui.layer(Layer::Base);
}

// ------------------------------------------------------------------------------------- the front end's frame

ui::Rect FrontEnd::sceneRect(int w, int h, float scale) const {
    if (play_) return Rect{0, 0, float(w), float(h)};
    const float cssW = float(w) / scale;
    if (cssW <= 760) {
        // The panel below the view (45 % of the height).
        const float ph = std::round(float(h) * 0.45f);
        return Rect{0, 0, float(w), float(h) - ph};
    }
    const float pw = std::round(340.0f * scale);
    return Rect{pw, 0, float(w) - pw, float(h)};
}

bool FrontEnd::viewsGetKeys(const Ui& ui) const {
    if (ui.wantsKeyboard() || modal_ != Modal::None || loading_) return false;
    if (pause_ != Pause::None) return false;
    if (mp_.chatOpen) return false;
    if (play_ && !inGame_) return false;  // the front end's menus have the keyboard
    return sceneFocus_;
}

bool FrontEnd::viewsGetPointer(const Ui& ui) const {
    if (modal_ != Modal::None || loading_ || pause_ != Pause::None) return false;
    if (play_ && !inGame_) return !ui.wantsPointer();
    return sceneDrag_ || !ui.wantsPointer();
}

void FrontEnd::buildTopBar(Ui& ui) {
    Style top;
    top.row();
    top.align = Align::Center;
    top.gap = 14;
    top.pad = Edges(14, 20);
    top.gradient(rgba(5, 8, 13, 0.9f), rgba(5, 8, 13, 0), 180);
    ui.open("top", top);
    Style logo = tBody(19, 800);
    logo.text.letterSpacing = 0.38f;
    TextStyle ts = logo.text;
    Span a{"CrazyCity ", ts};
    Span b{"Insanity", ts};
    b.color = rgbaHex(col::accent);
    ui.rich({a, b}, logo);
    const std::string title = screenTitle();
    ui.text(title.empty() ? std::string() : "· " + title, tBody(13, 400, col::muted));
    ui.spacer();
    {
        const Id id = ui.id("driverchip");
        Interact it = ui.interact(id);
        Style chip;
        chip.row();
        chip.gap = 8;
        chip.align = Align::Center;
        chip.pad = Edges(6, 12);
        chip.round(999);
        chip.bg = col::panel;
        chip.outline(1, it.hover ? col::accent : col::line);
        chip.cursor = Cursor::Hand;
        chip.hit = true;
        Node& n = ui.open(id, chip);
        n.label = "Drivers";
        n.tooltip = "Drivers";
        Style dot;
        dot.size(8, 8);
        dot.round(4);
        dot.bg = driver()["skill"].truthy() ? col::warn : col::accent;
        ui.leaf(dot);
        ui.text(driver().isNull() ? std::string("no driver") : driver()["name"].str(), tBody(13));
        ui.close();
        if (it.clicked) go("drivers");
    }
    if (navButton(ui, "options", "Options")) go("options");
    if (navButton(ui, "help", "Keys")) {
        modal_ = Modal::Keys;
        ui.focusFirst();
    }
    if (navButton(ui, "back", "Back")) back();
    if (navButton(ui, "quit", "Viewer")) closeFrontEnd();
    ui.close();
}

void FrontEnd::buildFoot(Ui& ui) {
    Style f;
    f.row();
    f.gap = 16;
    f.pad = Edges(10, 20, 16, 20);
    f.align = Align::Center;
    ui.open("foot", f);
    Style t = tBody(12, 400, col::muted);
    ui.rich({kbd("Esc"), span(" back", 12, 400)}, t);
    ui.text(host_.isBrowser() ? "Your files stay in this browser." : "Your files stay on this computer.", t);
    ui.text(playerWords() ? "Words from your mmlang.dll"
                          : (host_.isBrowser() ? "English labels (open your MM2 folder in the viewer for your game's language)"
                                               : "English labels (open your MM2 folder in the viewer for your game's language)"),
            t);
    ui.close();
}

std::string FrontEnd::screenTitle() const {
    if (screen_ == "drivers") return "Drivers";
    if (screen_ == "race") return "Single player";
    if (screen_ == "car") return "Car";
    if (screen_ == "crashcity" || screen_ == "crash") return T(12);
    if (screen_ == "record") return "Driver record";
    if (screen_ == "stats") return T(348);
    if (screen_ == "options") return "Options";
    if (screen_ == "mp") return "Multiplayer";
    return std::string();
}

void FrontEnd::driverCard(Ui& ui) {
    const JValue& d = driver();
    Style card;
    card.column();
    card.bg = rgba(255, 255, 255, 0.03f);
    card.outline(1, col::line);
    card.round(12);
    card.pad = Edges(12, 14);
    card.w = std::min(360.0f, ui.viewW() * 0.34f);
    card.hit = true;
    ui.open("drivercard", card);
    if (d.isNull()) {
        ui.text("No driver", tBody(20, 750));
        note(ui, "Create a driver to keep records.");
    } else {
        const int dev = options().isNull() ? 1 : options()["controlDevice"].integer();
        const std::string ctl = T(580 + dev);
        const int mode = d["mode"].integer();
        std::string last = mode == 0 ? T(80) : mode == 2 ? T(79) : mode == 6 ? T(78) : (d["raceName"].truthy() ? d["raceName"].str() : modeName(mode));
        beginRow(ui, Justify::SpaceBetween);
        ui.text(d["name"].str(), tBody(20, 750));
        badge(ui, skillName(d["skill"].integer()), d["skill"].truthy() ? Badge::Pro : Badge::Accent);
        endRow(ui);
        std::vector<std::pair<std::string, std::string>> rows = {
            {T(636), last + (d["cityName"].truthy() ? " · " + d["cityName"].str() : std::string())},
            {T(637), d["carDescription"].str()},
            {T(638), ctl}};
        if (d["skill"].truthy()) rows.push_back({T(640), fmtThousands((long long)d["totalScore"].num())});
        Style gapS;
        gapS.h = 10;
        ui.leaf(gapS);
        kv(ui, rows);
    }
    ui.close();
}

void FrontEnd::buildFrontEnd(Ui& ui) {
    if (screen_ != "car") carFor_.clear();  // the car screen chooses for a network game only from there
    entering_ = renderedScreen_ != screen_;
    renderedScreen_ = screen_;
    Style root;
    root.column();
    root.absolute = true;
    root.left = root.top = root.right = root.bottom = 0;
    ui.open("fe", root);
    buildTopBar(ui);
    const bool narrow = ui.viewW() <= 760;
    Style mid;
    mid.row();
    mid.grow = 1;
    mid.minH = 0;
    mid.align = Align::Start;
    ui.open("mid", mid);
    Style main;
    main.column();
    main.mar = narrow ? Edges(0, 12) : Edges(0, 0, 0, 20);
    main.w = narrow ? ui.viewW() - 24 : std::max(280.0f, std::min(450.0f, ui.viewW() * 0.5f - 20));
    main.pad = Edges(18);
    main.bg = col::panel;
    main.outline(1, col::line);
    main.round(14);
    main.backdropBlur = 10;
    main.maxH = ui.viewH() - 150;
    main.scrollY = true;
    main.shrink = 0;
    main.hit = true;
    // A new screen starts at its top, and the keys and a game controller at its first control
    // (not the top bar's).
    const Id mainId = ui.id("main");
    if (entering_) ui.scrollOf(mainId) = 0;
    ui.open(mainId, main);
    if (entering_ && modal_ == Modal::None) ui.focusFirstIn(mainId);
    sideCards_.clear();
    if (screen_ == "drivers") screenDrivers(ui);
    else if (screen_ == "race") screenRace(ui);
    else if (screen_ == "car") screenCar(ui);
    else if (screen_ == "crashcity") screenCrashCity(ui);
    else if (screen_ == "crash") screenCrash(ui);
    else if (screen_ == "record") screenRecord(ui);
    else if (screen_ == "stats") screenStats(ui);
    else if (screen_ == "options") optionsPanel(ui, false);
    else if (screen_ == "mp") screenMp(ui);
    else screenMain(ui);
    ui.close();  // main
    if (!narrow) {
        Style side;
        side.column();
        side.grow = 1;
        side.pad = Edges(0, 20);
        side.align = Align::End;
        side.gap = 12;
        ui.open("side", side);
        for (auto& f : sideCards_) f();
        ui.close();
    }
    ui.close();  // mid
    buildFoot(ui);
    ui.close();  // fe
    // A locked car or paint: the view darkened and the lock card (car screen).
    if (lockCard_.size()) {
        Style lc;
        lc.absolute = true;
        lc.right = 28;
        lc.bottom = 90;
        lc.pad = Edges(14, 18);
        lc.round(14);
        lc.bg = rgba(20, 8, 10, 0.75f);
        lc.outline(1, rgba(248, 113, 113, 0.45f));
        lc.maxW = 340;
        lc.column();
        lc.hit = true;
        ui.open("lockcard", lc);
        Style b = tBody(15, 700, col::bad);
        b.mar.b = 4;
        ui.text("🔒 Locked", b);
        Style t = tBody(13.5f, 400, hex(0xfecaca));
        t.text.lineHeight = 13.5f * 1.5f;
        ui.text(lockCard_, t);
        ui.close();
        lockCard_.clear();
    }
}

// ------------------------------------------------------------------------------------- HUD

namespace {
Color damageColour(float f) {
    // Green through yellow to red (hud.md).
    const Color g = rgba(74, 222, 128), y = rgba(250, 204, 21), r = rgba(239, 68, 68);
    return f < 0.5f ? mix(g, y, f / 0.5f) : mix(y, r, (f - 0.5f) / 0.5f);
}
std::string lineText(const JValue& l, const std::string& arg = std::string()) {
    if (l.isNull()) return std::string();
    if (l["text"].truthy()) return l["text"].str();
    const int id = l["id"].integer(-1);
    if (id == 232) return "Keep above " + arg + " mph";
    return id >= 0 ? T(id) : std::string();
}
Style glass(float radius) {
    Style s;
    s.bg = col::hudGlass;
    s.outline(1, rgba(255, 255, 255, 0.08f));
    s.round(radius);
    return s;
}
}  // namespace

void FrontEnd::buildHud(Ui& ui) {
    const JValue& d = hud_;
    const bool showing = d["visible"].truthy() && (v_.driving || inGame_);
    if (!showing) return;
    // The HUD covers the 3D view (the top half with the half-screen map; the whole screen with the
    // full-screen map), in the scene's own rectangle.
    const Rect scene = sceneRectPx(ui);
    const JValue& vv = d["view"];
    const float vx = vv.isNull() ? 0 : vv["x"].f(), vy = vv.isNull() ? 0 : vv["y"].f();
    const float vw = vv.isNull() ? 1 : vv["w"].f(1), vh = vv.isNull() ? 1 : vv["h"].f(1);
    Style root;
    root.absolute = true;
    root.left = scene.x + vx * scene.w;
    root.top = scene.y + vy * scene.h;
    root.w = vw * scene.w;
    root.h = vh * scene.h;
    root.stack();
    ui.open("hud", root);
    // Results and in-race menus hide the HUD like the pause menu (hud.md, "When the HUD is hidden").
    const JValue& race = d["race"];
    const bool hideAll = d["hideAll"].truthy() || race["resultsShown"].truthy() || race["endMenu"].truthy() || (netGame_ && mp_.resultsOpen);
    const bool narrow = ui.viewW() <= 760;
    // Gauges.
    if (d["gauges"].truthy() && !hideAll) {
        Style g = glass(16);
        g.absolute = true;
        g.left = 18;
        g.bottom = 18;
        g.row();
        g.gap = 12;
        g.align = Align::Center;
        g.pad = Edges(12, 16, 12, 14);
        ui.open("gauges", g);
        Style sp;
        sp.row();
        sp.align = Align::Baseline;
        sp.minW = 104;
        sp.justify = Justify::End;
        ui.open(sp);
        Style v = tBody(narrow ? 40 : 54, 800, col::hudInk);
        v.text.letterSpacing = -0.03f * (narrow ? 40 : 54);
        v.text.lineHeight = (narrow ? 40 : 54) * 0.9f;
        v.textShadow = Shadow{0, 2, 10, 0, rgba(0, 0, 0, 0.5f)};
        ui.text(std::to_string(d["mph"].integer()), v);
        Style u = tBody(12, 700, rgba(255, 255, 255, 0.6f));
        u.text.letterSpacing = 1.68f;
        u.mar.l = 4;
        ui.text("MPH", u);
        ui.close();
        Style gear;
        gear.size(40, 40);
        gear.round(11);
        gear.bg = hex(0x0b1118);
        gear.outline(2, col::accent);
        gear.color = col::accent;
        gear.font(22, 800);
        gear.textAlign = TextAlign::Center;
        gear.textVCenter = true;
        gear.shrink = 0;
        ui.text(d["gear"].str(), gear);
        Style colS;
        colS.column();
        colS.gap = 7;
        colS.w = narrow ? 120 : 168;
        ui.open(colS);
        auto meter = [&](const char* lbl, std::function<void()> body) {
            Style m;
            m.row();
            m.gap = 8;
            m.align = Align::Center;
            ui.open(m);
            Style l = tBody(10, 700, rgba(255, 255, 255, 0.55f));
            l.text.letterSpacing = 1.4f;
            l.w = 30;
            l.shrink = 0;
            ui.text(lbl, l);
            body();
            ui.close();
        };
        const float maxRpm = d["maxRpm"].f(), rpm = d["rpm"].f();
        const int units = maxRpm > 0 ? int(std::max(0.0f, std::min(1.0f, rpm / maxRpm)) * 20.0f) : 0;
        meter("RPM", [&]() {
            Style segs;
            segs.grow = 1;
            segs.h = 10;
            Node& n = ui.leaf(segs);
            n.paint = [units](DrawList& dl, const Rect& r, float sc) {
                const float gap = 2 * sc;
                const float w = (r.w - gap * 19) / 20;
                for (int i = 0; i < 20; ++i) {
                    Color c = i < units ? (i >= 16 ? hex(0xfb7185) : col::accent) : rgba(255, 255, 255, 0.1f);
                    dl.rect(Rect{std::round(r.x + i * (w + gap)), r.y, std::max(1.0f, std::round(w)), r.h}, c, 2 * sc);
                }
            };
        });
        const float dmg = d["damage"].f();
        meter("DMG", [&]() {
            Style bar;
            bar.grow = 1;
            bar.h = 7;
            bar.round(4);
            bar.bg = rgba(255, 255, 255, 0.1f);
            bar.clip = true;
            Node& n = ui.leaf(bar);
            n.paint = [dmg](DrawList& dl, const Rect& r, float sc) {
                dl.pushClip(r);
                dl.rect(Rect{r.x, r.y, r.w * std::max(0.0f, std::min(1.0f, dmg)), r.h}, damageColour(dmg), 4 * sc);
                dl.popClip();
            };
        });
        std::string box = toLower(d["automatic"].truthy() ? T(634) : T(633));
        Style cam = tBody(10.5f, 400, rgba(255, 255, 255, 0.5f));
        cam.text.letterSpacing = 0.21f;
        ui.text(d["camera"].str() + " · " + box, cam);
        ui.close();
        ui.close();
    }
    // Race timer (shown even while a look key is held).
    if (d["timer"].truthy()) {
        const int warning = d["warning"].integer();
        Style t = glass(12);
        t.absolute = true;
        t.leftPct = 50;
        t.anchorX = 0.5f;
        t.top = 16;
        t.pad = Edges(6, 16);
        t.font(28, 800);
        t.text.letterSpacing = 0.56f;
        t.nowrap = true;
        t.color = col::hudInk;
        if (warning > 0) {
            t.color = hex(0xfca5a5);
            t.borderColor = rgba(248, 113, 113, 0.5f);
        }
        if (warning > 1) {
            const float k = float(0.5 + 0.5 * std::sin(ui.time() * 3.14159265 * 2.0));
            t.color = mix(hex(0xfca5a5), hex(0xef4444), k);
        }
        ui.text(d["timer"].str(), t);
    }
    // Panel: place, checkpoints, laps.
    if (!hideAll) {
        Style p;
        p.absolute = true;
        p.right = 16;
        p.top = 16;
        p.column();
        p.gap = 6;
        p.align = Align::End;
        ui.open("panel", p);
        const bool circuit = !d["lap"].isNull();
        auto pill = [&](const std::string& k, int a, int b) {
            Style s = glass(10);
            s.row();
            s.gap = 10;
            s.align = Align::Baseline;
            s.pad = Edges(6, 12);
            ui.open(s);
            Style ks = tBody(10, 700, rgba(255, 255, 255, 0.55f));
            ks.text.upper = true;
            ks.text.letterSpacing = 1.4f;
            ui.text(k, ks);
            Style vs = tBody(22, 800, col::hudInk);
            TextStyle big = vs.text;
            TextStyle small;
            small.size = 14;
            small.weight = 700;
            Span sb{"/" + std::to_string(b), small};
            sb.color = rgbaHex(rgba(255, 255, 255, 0.6f));
            ui.rich({Span{std::to_string(a), big}, sb}, vs);
            ui.close();
        };
        if (!d["place"].isNull()) pill(T(circuit ? 259 : 254), d["place"][0].integer(), d["place"][1].integer());
        if (!d["checkpoints"].isNull()) pill(T(circuit ? 260 : 255), d["checkpoints"][0].integer(), d["checkpoints"][1].integer());
        if (circuit) {
            pill(T(261), d["lap"][0].integer(), d["lap"][1].integer());
            const JValue& lt = d["lapTimes"];
            if (lt.size()) {
                Style l;
                l.column();
                l.bg = col::hudGlass;
                l.round(10);
                l.pad = Edges(6, 12);
                ui.open(l);
                for (size_t i = 0; i < lt.size(); ++i) {
                    Style r;
                    r.row();
                    r.gap = 12;
                    r.justify = Justify::SpaceBetween;
                    ui.open(r);
                    ui.text(std::to_string(i + 1) + ".", tBody(13, 400, col::hudInk));
                    ui.text(lt[i].str().empty() ? std::string("–") : lt[i].str(), tBody(13, 400, col::hudInk));
                    ui.close();
                }
                ui.close();
            }
        }
        ui.close();
    }
    // Messages: the primary line (upper or lower position) and its secondary line.
    const JValue& pr = d["primary"];
    std::string prText = !hideAll && !pr.isNull() ? lineText(pr) : std::string();
    if (!prText.empty()) {
        if (pr["id"].integer(-1) == 232 && !pr["text"].truthy()) prText = lineText(pr, race["arg"].str());
        const bool upper = pr["upper"].truthy();
        Style m;
        m.absolute = true;
        m.left = 0;
        m.right = 0;
        if (upper) m.topPct = 26;
        else m.bottom = vh * scene.h / ui.scale() * 0.24f;
        m.column();
        m.align = Align::Center;
        ui.open(m);
        Style p = tBody(upper ? 38 : 26, 850, col::hudInk);
        p.text.letterSpacing = -0.01f * (upper ? 38 : 26);
        p.pad = Edges(4, 18);
        p.textAlign = TextAlign::Center;
        p.textShadow = Shadow{0, 3, 16, 0, rgba(0, 0, 0, 0.65f)};
        ui.text(prText, p);
        const std::string s2 = lineText(d["secondary"]);
        if (!s2.empty()) {
            Style s = tBody(20, 700, col::accent);
            s.textAlign = TextAlign::Center;
            s.mar.t = 2;
            s.textShadow = Shadow{0, 2, 10, 0, rgba(0, 0, 0, 0.6f)};
            ui.text(s2, s);
        }
        ui.close();
    }
    if (d["paused"].truthy() && !d["pauseMenu"].truthy() && d["mapMode"].integer() != 3 && pause_ == Pause::None) {
        Style p = glass(14);
        p.borderColor = Color();
        p.absolute = true;
        p.leftPct = 50;
        p.topPct = 45;
        p.anchorX = 0.5f;
        p.anchorY = 0.5f;
        p.font(30, 800);
        p.text.letterSpacing = 9;
        p.pad = Edges(10, 24);
        p.color = col::hudInk;
        p.nowrap = true;
        ui.text("PAUSED", p);
    }
    if (netGame_) buildNetHud(ui);
    ui.close();
    buildMapFrame(ui);
}

void FrontEnd::buildMapFrame(Ui& ui) {
    const JValue& d = hud_;
    const int mode = d["mapMode"].integer();
    const JValue& m = d["map"];
    if (mode < 1 || m.isNull()) return;
    const Rect scene = sceneRectPx(ui);
    Style f;
    f.absolute = true;
    f.left = scene.x + m["x"].f() * scene.w;
    f.top = scene.y + m["y"].f() * scene.h;
    f.w = m["w"].f() * scene.w;
    f.h = m["h"].f() * scene.h;
    f.round(6);
    const bool full = mode == 3;
    if (!full) {
        f.shadow(0, 8, 24, 0, rgba(0, 0, 0, 0.35f));
        f.shadow(0, 0, 0, 3, rgba(45, 212, 191, 0.55f));
        f.shadow(0, 0, 0, 2, rgba(8, 12, 18, 0.85f));
    }
    ui.open("mapframe", f);
    std::string tag = std::string(d["headingUp"].truthy() ? "▲ HEADING" : "N ▲") + (d["zoomedIn"].truthy() ? " · NEAR" : "") +
                      (full && d["paused"].truthy() ? " · PAUSED (M)" : "");
    Style t = tBody(full ? 12 : 10, 800, hex(0x0b1118));
    t.text.letterSpacing = full ? 1.44f : 1.2f;
    t.absolute = true;
    t.left = full ? 16 : 6;
    t.top = full ? 14 : 5;
    t.bg = rgba(255, 255, 255, 0.75f);
    t.round(5);
    t.pad = Edges(1, 5);
    t.nowrap = true;
    ui.text(tag, t);
    ui.close();
}

ui::Rect FrontEnd::sceneRectPx(const Ui& ui) const {
    const Rect r = sceneRect(int(std::round(ui.viewW() * ui.scale())), int(std::round(ui.viewH() * ui.scale())), ui.scale());
    return Rect{r.x / ui.scale(), r.y / ui.scale(), r.w / ui.scale(), r.h / ui.scale()};
}

}  // namespace fe
}  // namespace cc
