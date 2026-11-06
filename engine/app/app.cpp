#include "app/app.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>

#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "game/race_catalogue.h"
#include "model/asset_cache.h"
#include "platform/web_bridge.h"
#include "render/texture_cache.h"
#include "net/net_game.h"
#include "net/session.h"
#include "net/settings.h"
#include "platform/web_transport.h"
#ifndef __EMSCRIPTEN__
#include "platform/host_server.h"
#include "platform/native_transport.h"
#endif
#include "net/net_link.h"
#include "app/front_end.h"
#include "app/menu_data.h"
#include "ui/text.h"

namespace cc {
// app/app_services.cpp: the front end's services over this App.
std::unique_ptr<fe::Services> makeAppServices(App& app);
}  // namespace cc

namespace cc {

App& theApp() {
    static App app;
    return app;
}

App::App() = default;
App::~App() {
    fe_.reset();
    driveView_.stop();
    netSession_.reset();
}

void App::setHost(ui::Host* h) {
    host_ = h ? h : &defaultHost_;
    ui_.setHost(host_);
}

bool App::init() {
    // Log lines go to the viewer's problems list, and warnings to the page's console (natively
    // every line to the program's sink: the terminal decides what it prints).
    setLogSink([this](LogLevel level, const std::string& kind, const std::string& msg) {
        const std::string m = latin1ToUtf8(msg);
        if (fe_) fe_->onLog(int(level), kind, m);
        else logs_.push_back(LogEntry{int(level), kind, m});
#ifdef __EMSCRIPTEN__
        if (int(level) >= 1) pageLog(int(level), kind, m);
#else
        pageLog(int(level), kind, m);
#endif
    });
    // Engine events to the front end, in order, once a frame.
    setEventSink([this](const std::string& name, const std::string& json) { events_.push_back(Event{name, json}); });
    if (!platform_.init("CrazyCity Insanity")) return false;
    if (!renderer_.init()) return false;
    ui_.setHost(host_);
    if (!ui::Fonts::get().init()) logWarn("UI: the fonts are missing (build/gen/ui_fonts.cpp)");
    uiOk_ = uiGl_.init();
    if (!uiOk_) logError("UI: the UI shaders did not build");
    {
        menu::Env env;
        env.vfs = &vfs_;
        env.cities = [this]() -> const std::vector<CityEntry>& { return cityView_.cities(); };
        env.cars = [this]() {
            std::vector<menu::ListCar> out;
            for (const CarEntry& c : cars_) out.push_back(menu::ListCar{c.baseName, c.group == CarEntry::Menu, c.infoFlags});
            return out;
        };
        env.clock = [this]() { return clock(); };
        menu::setEnv(env);
    }
    feServices_ = makeAppServices(*this);
    fe_ = std::make_unique<fe::FrontEnd>(*feServices_, *host_);
    for (const LogEntry& l : logs_) fe_->onLog(l.level, l.kind, l.msg);
    logs_.clear();
    assets_ = std::make_unique<AssetCache>(vfs_);
    textures_ = std::make_unique<TextureCache>(vfs_);
    ctx_.vfs = &vfs_;
    ctx_.assets = assets_.get();
    ctx_.textures = textures_.get();
    ctx_.meshes = &meshes_;
    ctx_.renderer = &renderer_;
    ctx_.platform = &platform_;
    sound_.setVfs(&vfs_);
    sound_.setBackend(&mixer_);
    ctx_.sound = &sound_;
    // The race session's music events (music.md, "Race events").
    driveView_.setRaceEventSink([this](const game::RaceEvent& e) { raceMusicEvent(e.name); });
    startTime_ = lastTime_ = platform_.timeSeconds();
    ready_ = true;
    logInfo("renderer ready (WebGL 2)");
    return true;
}

void App::stepFrames(int n, float dt) {
    // Scripted checks: frames of a fixed length, also while the page is hidden (no animation
    // frames). Keys are not read.
    for (int i = 0; i < n; ++i) {
        fixedDt_ = dt;
        frame();
    }
    fixedDt_ = -1.0f;
}

void App::uiFrame(int w, int h, float dt) {
    ui::FrameInput fi;
    fi.events = input_.events;
    fi.width = w;
    fi.height = h;
    fi.scale = ui::Ui::chooseScale(w, h, platform_.pixelRatio());
    fi.time = ctx_.time;
    fi.dt = dt;
    fi.padConnected = input_.pad.connected;
    for (int b = 0; b < 17; ++b) fi.padHeld[b] = input_.pad.button[b] > 0.5f;
    for (int a = 0; a < 4; ++a) fi.padAxis[a] = input_.pad.axis[a];
    ui_.beginFrame(fi);
    if (fe_) {
        // The engine's events since the last frame (pause, HUD, race, network).
        std::vector<Event> ev;
        ev.swap(events_);
        for (const Event& e : ev) fe_->onEngineEvent(e.name, e.json);
        fe_->frame(ui_);
    }
    ui_.endFrame();
    // Typed text only while a text field has the keyboard.
    if (ui_.wantsKeyboard() != textInputOn_) {
        textInputOn_ = ui_.wantsKeyboard();
        platform_.setTextInput(textInputOn_);
    }
}

void App::frame() {
    if (!ready_) return;
    if (fixedDt_ < 0.0f) platform_.pollEvents(input_);
    else input_.beginFrame();
    for (const ui::InputEvent& e : pendingUiEvents_) input_.events.push_back(e);
    pendingUiEvents_.clear();
    double now = platform_.timeSeconds();
    float dt = float(now - lastTime_);
    lastTime_ = now;
    if (fixedDt_ >= 0.0f) dt = fixedDt_;
    if (dt > 0.1f) dt = 0.1f;
    if (dt < 0) dt = 0;
    ctx_.time = now - startTime_;
    frameMs_ = frameMs_ * 0.95f + dt * 1000.0f * 0.05f;

    sound_.beginFrame(dt);
    // The audio archives' data files (cardata, ambience, speech lists) arrive after mounting.
    if (!ctx_.soundReady && vfs_.prefetched()) {
        ctx_.soundReady = true;
        if (!vfs_.archives().empty()) logInfo("sound and music data ready");
        // Music from aud/dmusic/ (mm2aud.ar), read once its files have arrived.
        music_.setFileReader([this](const std::string& path) { return vfs_.exists(path) ? vfs_.open(path) : nullptr; });
        if (!music_.hasData()) logInfo("no music data (aud/dmusic/ is in mm2aud.ar)");
        startMusicPhase();
    }
    int w = 1, h = 1;
    platform_.drawableSize(w, h);
    const double t0 = platform_.timeSeconds();
    netFrame(dt);
    // Whether the 3D views get this frame's keys is decided before the UI uses them (an Esc that
    // closes a menu must not reach the drive as well).
    const bool viewKeys = !fe_ || fe_->viewsGetKeys(ui_);
    // The UI first (input, the screens, layout); it is drawn over the 3D view below.
    uiFrame(w, h, dt);
    // The 3D view in its part of the window, with the input the UI leaves to it.
    ui::Rect scene{0, 0, float(w), float(h)};
    if (fe_) scene = fe_->sceneRect(w, h, ui_.scale());
    const int sx = int(std::lround(scene.x)), sy = int(std::lround(scene.y));
    const int sw = std::max(1, int(std::lround(scene.w))), sh = std::max(1, int(std::lround(scene.h)));
    renderer_.setOrigin(sx, h - sy - sh);
    InputState vin = input_;
    vin.events.clear();
    if (fe_ && !fe_->viewsGetPointer(ui_) && !vin.pointerLocked) {
        for (bool& b : vin.buttons) b = false;
        for (bool& c : vin.clicked) c = false;
        vin.wheel = 0;
        vin.mouseDX = vin.mouseDY = 0;
    }
    if (!viewKeys || (fe_ && !fe_->viewsGetKeys(ui_))) {
        vin.hasFocus = false;
        for (bool& d : vin.down) d = false;
        for (bool& p : vin.pressed) p = false;
        for (bool& d : vin.key) d = false;
        for (bool& p : vin.keyPressed) p = false;
    }
    if (view_ == 2 && driveView_.active()) {
        // A pause (sound-system.md "Pause"): the sounds stop. The music follows the pause menu
        // below (music.md "Pause").
        if (driveView_.paused() != gamePaused_) {
            gamePaused_ = driveView_.paused();
            sound_.setPaused(gamePaused_);
        }
        // The game mode's music frame, before the cars and physics of this frame (music.md).
        const DriveView::MusicCues mc = driveView_.musicCues();
        music::MusicFrame mf;
        mf.F = std::clamp(dt, 0.0001f, 0.1f);
        mf.speed = mc.speed;
        mf.chaseCount = mc.chase;
        mf.airborne = mc.airborne;
        mf.echo = mc.echo;
        // The pause (or chat) menu cuts to the pause music and back (music.md, "Pause"); while the
        // game is paused nothing of the frame rules runs.
        if (driveView_.pauseMenu() != musicPauseMenu_) {
            musicPauseMenu_ = driveView_.pauseMenu();
            if (musicPauseMenu_) music_.paused();
            else music_.resumed();
        }
        if (!driveView_.paused()) music_.frame(mf);
        driveView_.update(vin, dt, ctx_, float(sw) / float(sh));
        driveView_.render(ctx_, sw, sh);
        if (driveView_.leaveRequested() && !driveView_.netActive()) {
            stopDrive();
            pageEvent("drive", "{\"active\":false}");
        }
    } else if (view_ == 0) {
        carView_.update(vin, dt, sh);
        carView_.render(ctx_, sw, sh);
    } else {
        cityView_.update(vin, dt, ctx_);
        cityView_.render(ctx_, sw, sh);
    }
    renderer_.setOrigin(0, 0);
    if (uiOk_) uiGl_.render(ui_.drawList(), w, h);
    sound_.endFrame();
    platform_.swap();
    cpuMs_ = cpuMs_ * 0.9f + float(platform_.timeSeconds() - t0) * 1000.0f * 0.1f;

    readoutTimer_ += dt;
    if (readoutTimer_ > 0.1) {
        readoutTimer_ = 0;
        readout_ = view_ == 2 ? driveView_.readout() : view_ == 1 ? cityView_.readout() : std::string();
    }
}

void App::clearArchives() { pending_.clear(); }

void App::addArchive(const std::string& name, std::shared_ptr<ByteSource> source) {
    pending_.push_back({name, std::move(source)});
}

std::string App::mount() {
    // Drop everything that points into the old archives.
    music_.setFileReader(nullptr);
    driveView_.stop();
    if (view_ == 2) view_ = 1;
    carView_.clear();
    cityView_.unload(ctx_);
    meshes_.clear();
    textures_->clear();
    assets_->clear();
    sound_.clearFiles();
    vfs_.clear();
    resetLogDedup();
    ctx_.soundReady = false;
    ctx_.audioExtras = false;

    std::vector<std::string> names;
    for (const auto& p : pending_) names.push_back(p.name);
    JsonWriter j;
    j.beginObject();
    j.key("archives").beginArray();
    for (size_t i : Vfs::mountOrder(names)) {
        const PendingArchive& p = pending_[i];
        bool ok = vfs_.mount(p.name, p.source);
        if (ok && iequals(baseName(p.name), "mm2audex.ar")) ctx_.audioExtras = true;
        j.beginObject();
        j.field("name", latin1ToUtf8(p.name));
        j.field("ok", ok);
        j.field("base", Vfs::isBaseArchive(p.name));
        j.field("files", ok ? int(vfs_.archives().back()->entries().size()) : 0);
        j.endObject();
    }
    j.endArray();
    // The audio archives are read lazily in the browser: their small data files are fetched now,
    // the sound files when a sample first needs them.
    vfs_.prefetch([](const std::string& n) {
        return istartsWith(n, "aud/") && !iendsWith(n, ".wav");  // aud/dmusic/ for the music too
    });
    if (!vfs_.exists("tune/vpbug.info") && !vfs_.exists("geometry/vpbug.pkg"))
        logFile(LogLevel::Warn, "mm2core.ar", "the base game data does not seem to be loaded (no tune/vpbug.info)");

    buildCarList();
    cityView_.refreshCityList(vfs_);
    // Music from aud/dmusic/ (mm2aud.ar); the menus' music while not driving.
    j.field("files", int(vfs_.fileCount()));
    j.field("cars", int(cars_.size()));
    j.field("cities", int(cityView_.cities().size()));
    j.endObject();
    selectedCar_ = -1;
    mountSummary_ = j.str();
    if (fe_) fe_->onMounted(mountSummary_);
    return mountSummary_;
}

void App::buildCarList() {
    cars_.clear();
    std::set<std::string> listed;
    for (const CarInfo& c : cc::buildCarList(vfs_)) {
        CarEntry e;
        e.group = CarEntry::Menu;
        e.baseName = c.baseName;
        e.description = c.description;
        e.colors = c.colors;
        e.archive = c.archive;
        e.fromMod = c.fromMod;
        e.builtIn = c.builtIn;
        e.kind = VehicleKind::Player;
        e.infoFlags = std::atoi(c.info.get("Flags").c_str());
        listed.insert(toLower(c.baseName));
        cars_.push_back(std::move(e));
    }
    // Vehicle packages that are not in the menu list, and traffic cars.
    std::vector<CarEntry> unlisted, traffic;
    for (const std::string& path : vfs_.listUnique("geometry/")) {
        if (!iendsWith(path, ".pkg")) continue;
        std::string name = stem(path);
        std::string ln = toLower(name);
        if (iendsWith(ln, "_dash") || iendsWith(ln, "_trailer")) continue;
        bool vp = startsWith(ln, "vp"), va = startsWith(ln, "va_");
        if ((!vp && !va) || listed.count(ln)) continue;
        if (vfs_.fileSize(path) <= 0) continue;
        if (va) {
            // Some city objects share the va_ prefix (va_ug_l); only packages with a body are cars.
            auto pkg = assets_->package(name, false);
            bool body = false;
            if (pkg)
                for (const auto& m : pkg->meshes)
                    if (istartsWith(m.name, "BODY_")) body = true;
            if (!body) continue;
        }
        CarEntry e;
        e.group = va ? CarEntry::Traffic : CarEntry::Unlisted;
        e.kind = va ? VehicleKind::Traffic : VehicleKind::Player;
        e.baseName = name;
        e.description = name;
        e.archive = vfs_.archiveOf(path);
        e.fromMod = !vfs_.fromBaseArchive(path);
        (va ? traffic : unlisted).push_back(std::move(e));
    }
    for (auto& e : unlisted) cars_.push_back(std::move(e));
    for (auto& e : traffic) cars_.push_back(std::move(e));
}

std::string App::carListJson() const {
    JsonWriter j;
    j.beginArray();
    for (size_t i = 0; i < cars_.size(); ++i) {
        const CarEntry& c = cars_[i];
        j.beginObject();
        j.field("index", int(i));
        j.field("name", latin1ToUtf8(c.baseName));
        j.field("description", c.description);
        j.field("group", int(c.group));
        j.field("mod", c.fromMod);
        j.field("archive", latin1ToUtf8(c.archive));
        j.field("colors", int(c.colors.size()));
        j.endObject();
    }
    j.endArray();
    return j.str();
}

std::string App::selectCar(int index) {
    if (index < 0 || index >= int(cars_.size())) return "{\"loaded\":false}";
    if (carView_.setCar(cars_[size_t(index)], ctx_)) selectedCar_ = index;
    return carView_.infoJson();
}

void App::setLod(int l) {
    if (l < 0) l = 0;
    if (l > 3) l = 3;
    carView_.setLod(Lod(l));
}

void App::resetCamera() {
    if (view_ == 0) carView_.resetCamera();
}

std::string App::startDrive(int mode, bool flat) {
    if (selectedCar_ < 0 || selectedCar_ >= int(cars_.size())) return "{\"ok\":false,\"why\":\"pick a car first\"}";
    if (driveView_.netActive()) return "{\"ok\":false,\"why\":\"a network game is running (leave it first)\"}";
    if (!flat) cityForGame(LevelSet());  // Cruise: race props of roam, the plain object files
    City* city = flat ? nullptr : cityView_.city();
    if (!flat && !city) return "{\"ok\":false,\"why\":\"load a city first (or drive on flat ground)\"}";
    int w = 1, h = 1;
    platform_.drawableSize(w, h);
    std::string r = driveView_.start(cars_[size_t(selectedCar_)], carView_.paint(), city,
                                     DriveView::StartMode(mode < 0 || mode > 2 ? 0 : mode), cityView_.camera(), ctx_,
                                     float(w) / float(h > 0 ? h : 1));
    if (driveView_.active()) {
        view_ = 2;
        if (platform_.pointerLocked()) platform_.setPointerLock(false);
        // Drive mode is a Cruise game: the singleroam table (music.md).
        music::GameMusicSetup g;
        g.race = false;
        g.mapName = driveView_.mapName();
        musicPauseMenu_ = false;
        music_.startGame(g);
    }
    return r;
}

std::string App::raceListJson() const {
    JsonWriter j;
    j.beginObject();
    const City* city = const_cast<CityView&>(cityView_).city();
    const CityEntry* entry = nullptr;
    if (city)
        for (const CityEntry& c : cityView_.cities())
            if (iequals(c.mapName, city->name())) entry = &c;
    j.field("ok", entry != nullptr && city->data().network() != nullptr);
    if (entry) {
        j.field("city", latin1ToUtf8(entry->localizedName));
        j.key("modes").beginArray();
        for (game::RaceMode m : {game::RaceMode::Checkpoint, game::RaceMode::Circuit, game::RaceMode::Blitz, game::RaceMode::CrashCourse}) {
            const int n = game::raceCount(*entry, m);
            if (n <= 0) continue;
            j.beginObject();
            j.field("mode", int(m));
            j.field("name", std::string(game::modeTitle(m)));
            j.key("races").beginArray();
            for (int i = 0; i < n; ++i) j.value(latin1ToUtf8(game::raceDisplayName(*entry, m, i)));
            j.endArray();
            j.endObject();
        }
        j.endArray();
    }
    j.endObject();
    return j.str();
}

std::string App::startRace(int mode, int race, bool pro, int circuitOpponents, bool nextRace) {
    if (selectedCar_ < 0 || selectedCar_ >= int(cars_.size())) return "{\"ok\":false,\"why\":\"pick a car first\"}";
    if (driveView_.netActive()) return "{\"ok\":false,\"why\":\"a network game is running (leave it first)\"}";
    {
        // The race's path sets (race props and object variants), single player's object kinds.
        LevelSet ls;
        ls.race = game::raceName(game::RaceMode(mode), race);
        cityForGame(ls);
    }
    City* city = cityView_.city();
    if (!city) return "{\"ok\":false,\"why\":\"load a city first\"}";
    const CityEntry* entry = nullptr;
    for (const CityEntry& c : cityView_.cities())
        if (iequals(c.mapName, city->name())) entry = &c;
    if (!entry) return "{\"ok\":false,\"why\":\"the loaded city is not in the city list\"}";
    int w = 1, h = 1;
    platform_.drawableSize(w, h);
    std::string r = driveView_.startRace(cars_[size_t(selectedCar_)], carView_.paint(), city, *entry, mode, race, pro,
                                         circuitOpponents, false, ctx_, float(w) / float(h > 0 ? h : 1), nextRace);
    if (driveView_.active()) {
        view_ = 2;
        if (platform_.pointerLocked()) platform_.setPointerLock(false);
        startMusicPhase();  // the singlerace table (music.md)
    }
    return r;
}

std::string App::nextRace() {
    const DriveView::NextRace n = driveView_.nextRace();
    if (!n.offered) return "{\"ok\":false,\"why\":\"no next race\"}";
    // The Crash Course's car for a course the driver has not passed (results.md).
    if (!n.car.empty())
        for (size_t i = 0; i < cars_.size(); ++i)
            if (cars_[i].baseName == n.car) {
                selectedCar_ = int(i);
                break;
            }
    return startRace(n.mode, n.race, n.pro, n.opponents, true);
}

void App::stopDrive() {
    if (driveView_.active()) {
        FreeFlyCamera& c = cityView_.camera();
        c.pos = driveView_.cameraPosition();
        c.yaw = driveView_.cameraYaw();
        c.pitch = -0.1f;
    }
    const bool wasDriving = driveView_.active();
    driveView_.stop();
    if (view_ == 2) view_ = 1;
    if (gamePaused_) {
        gamePaused_ = false;
        sound_.setPaused(false);
    }
    musicPauseMenu_ = false;
    if (wasDriving) music_.startMenus();
}

void App::setMusicMode(int mode) {
    music_.setMode(mode == 1 ? music::MusicMode::Music : mode == 2 ? music::MusicMode::Ambient : music::MusicMode::Off);
    // The player file's exclusive pair (sound-system.md, "Options"): music (0x004) or ambient
    // (0x800, also the city's ambient sound objects at the next game set-up), or neither.
    audio::SoundOptions o = sound_.options();
    o.music = mode == 1;
    o.ambient = mode == 2;
    sound_.setOptions(o);
}

void App::raceMusicEvent(const std::string& n) {
    // The race session names its music moments; the stop after a wreck ends with an ending.
    if (n == "musicStart") music_.raceGo();
    else if (n == "musicFinalLap") music_.finalLap();
    else if (n == "musicFinalCheckpoint") {
        if (driveView_.raceMode() == 3) music_.lastGate();
        else music_.lastCheckpoint();
    } else if (n == "musicStop") {
        if (lastRaceEvent_ == "Damgelose" || lastRaceEvent_ == "damagePenalty") music_.wrecked();
        else music_.finished();
    } else if (n == "results") {
        music_.resultsOpened();
    }
    lastRaceEvent_ = n;
}

void App::startMusicPhase() {
    musicPauseMenu_ = false;
    if (driveView_.active()) {
        music::GameMusicSetup g;
        g.race = driveView_.raceMode() != 0;
        g.mapName = driveView_.mapName();
        music_.startGame(g);
    } else {
        music_.startMenus();
    }
}

void App::renderAudio(float* out, int frames) {
    music_.render(out, frames);
    mixer_.mix(out, frames, music::MusicSystem::kRate);
    for (int i = 0; i < 2 * frames; ++i) {
        if (out[i] > 1.0f) out[i] = 1.0f;
        else if (out[i] < -1.0f) out[i] = -1.0f;
    }
}

std::string App::setMusicGmBank(const std::string& name, const Bytes& data, bool bundled) {
    JsonWriter j;
    j.beginObject();
    if (data.empty()) {
        if (bundled) music_.setDefaultGmBank(nullptr);
        else music_.setGmBank(nullptr);
        j.field("ok", true);
        j.field("cleared", true);
        j.endObject();
        return j.str();
    }
    auto col = std::make_shared<music::DlsCollection>();
    std::string err;
    const std::string form = music::riffForm(data);
    bool ok = false;
    if (form == "DLS ") ok = music::parseDls(data, *col, err);
    else if (form == "sfbk") ok = music::parseSf2(data, *col, err);
    else err = "not a DLS or SoundFont 2 file";
    col->fileName = name;
    const bool piano = ok && col->find(0) != nullptr;
    const bool drums = ok && col->find(0x80000000u) != nullptr;
    if (ok) {
        if (bundled) music_.setDefaultGmBank(col);
        else music_.setGmBank(col);
        logInfo("%s General MIDI bank %s: %d instruments%s%s", bundled ? "bundled" : "player's", name.c_str(),
                int(col->instruments.size()), piano ? ", piano" : "", drums ? ", drum kit" : "");
    } else {
        logFile(LogLevel::Warn, name, "General MIDI bank not usable: " + err);
    }
    j.field("ok", ok);
    j.field("why", err);
    j.field("instruments", ok ? int(col->instruments.size()) : 0);
    j.field("piano", piano);
    j.field("drums", drums);
    j.endObject();
    return j.str();
}

void App::musicEvent(const std::string& n) {
    if (n == "pause") music_.paused();
    else if (n == "resume") music_.resumed();
    else if (n == "go") music_.raceGo();
    else if (n == "finallap") music_.finalLap();
    else if (n == "lastcheckpoint") music_.lastCheckpoint();
    else if (n == "finish") music_.finished();
    else if (n == "wreck") music_.wrecked();
    else if (n == "results") music_.resultsOpened();
    else if (n == "cdon") music_.cdPlaying(true);
    else if (n == "cdoff") music_.cdPlaying(false);
}

void App::setView(int v) {
    if (driveView_.active()) stopDrive();
    view_ = v == 1 ? 1 : 0;
    if (view_ == 0 && platform_.pointerLocked()) platform_.setPointerLock(false);
}

std::string App::cityListJson() const {
    JsonWriter j;
    j.beginArray();
    for (const auto& c : cityView_.cities()) {
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

std::string App::loadCity(int index) {
    if (driveView_.active()) stopDrive();
    const auto& list = cityView_.cities();
    if (index >= 0 && index < int(list.size())) cityView_.loadCity(list[size_t(index)], ctx_);
    return cityView_.infoJson();
}

std::string App::dropCar() {
    if (selectedCar_ < 0 || selectedCar_ >= int(cars_.size())) return "{\"ok\":false,\"why\":\"pick a car first\"}";
    bool ok = cityView_.dropCar(cars_[size_t(selectedCar_)], carView_.paint(), ctx_);
    return ok ? "{\"ok\":true}" : "{\"ok\":false,\"why\":\"no street below the camera\"}";
}

void App::setCityCamera(float x, float y, float z, float yaw, float pitch) {
    FreeFlyCamera& c = cityView_.camera();
    c.pos = vec3(x, y, z);
    c.yaw = yaw;
    c.pitch = pitch;
}

void App::setSoundOption(const std::string& name, int v) {
    audio::SoundOptions o = sound_.options();
    if (name == "effects") o.effects = v != 0;
    else if (name == "commentary") o.commentary = v != 0;
    else if (name == "volume") o.sfxVolume = float(std::clamp(v, 0, 100)) / 100.0f;
    else if (name == "balance") {
        o.balance = float(std::clamp(v, -100, 100)) / 100.0f;
        music_.setBalance(o.balance, true);  // the same option pans the music (music.md)
    }
    else if (name == "stereo") o.stereo = v != 0;
    sound_.setOptions(o);
}

std::string App::soundJson() const {
    const audio::SoundSystem::Stats st = sound_.stats();
    JsonWriter j;
    j.beginObject();
    j.field("ready", ctx_.soundReady);
    j.field("samples", st.samples);
    j.field("playing", st.playing);
    j.field("voices", st.voices);
    j.field("sounding", mixer_.sounding());
    j.field("files", st.files);
    j.field("missing", st.missing);
    j.field("pending", st.pending);
    j.field("stolen", st.stolen);
    j.field("gain", double(sound_.effectsGain()));
    j.key("game");
    const std::string g = driveView_.soundJson();
    j.raw(g.empty() ? std::string("null") : g);
    j.endObject();
    return j.str();
}

std::string App::statsJson() const {
    JsonWriter j;
    j.beginObject();
    j.field("drawCalls", renderer_.stats().drawCalls);
    j.field("triangles", renderer_.stats().triangles);
    j.field("textures", int(textures_ ? textures_->count() : 0));
    j.field("textureMB", textures_ ? double(textures_->gpuBytes()) / 1048576.0 : 0.0);
    j.field("meshes", int(meshes_.count()));
    j.field("packages", int(assets_ ? assets_->packageCount() : 0));
    j.field("frameMs", double(frameMs_));
    j.field("cpuMs", double(cpuMs_));
    j.endObject();
    return j.str();
}

}  // namespace cc

namespace cc {

// ---------------------------------------------------------------------------------------------
// Network games

void App::netBegin(const std::string& name, const std::string& car, int paint) {
    if (driveView_.netActive()) {
        driveView_.viewBytes(netView_);
        stopDrive();
    }
    netSession_.reset();
    netTransport_.reset();
    webTransport_ = nullptr;
#ifdef __EMSCRIPTEN__
    auto t = std::make_unique<net::WebTransport>();
    webTransport_ = t.get();
#else
    // The native transport (platform/native_transport.*): the front end hosts or joins through it.
    nativeTransport_ = nullptr;
    auto t = std::make_unique<net::NativeTransport>(netConfig());
    nativeTransport_ = t.get();
#endif
    netTransport_ = std::move(t);
    netSession_ = std::make_unique<net::Session>(netTransport_.get(), &vfs_);
    net::PlayerRecord& r = netSession_->localRecord();
    r.name = name.empty() ? std::string("Player") : name;
    r.car = car.empty() ? std::string("vpbug") : car;
    r.paint = paint;
    r.team = netDefaultTeam(r.car);  // the free-for-all rule's default (1.3 step 1)
    if (const City* c = const_cast<CityView&>(cityView_).city()) r.city = c->data().raceDir();
    // The car list (a remote car of a name not in it is built as vpcoop), the driver's locks, a
    // real clock for the spot seeds (1.3 step 1, 5.3).
    std::vector<std::string> names;
    for (const CarEntry& c : cars_)
        if (c.group == CarEntry::Menu) names.push_back(c.baseName);
    netSession_->setCarNames(names);
    netSession_->setLockQuery([this](const std::string& car, int paint) { return carLockQuery_ ? carLockQuery_(car, paint) : 0; });
    netSession_->setRealClock([this]() { return platform_.timeSeconds(); });
    // The joiner's city test (1.3 step 1): the host's city must be in this machine's city list.
    netSession_->setCityInstalled([this](const std::string& raceDir) {
        for (const CityEntry& c : cityView_.cities())
            if (iequals(c.raceDir, raceDir)) return true;
        return false;
    });
    netClosedSent_ = false;
    netStateTimer_ = 1.0f;
}

void App::netEnd() {
    if (driveView_.netActive()) {
        driveView_.viewBytes(netView_);
        stopDrive();
    }
    if (netSession_ && netSession_->open()) netSession_->quit();
    netSession_.reset();
    netTransport_.reset();
    webTransport_ = nullptr;
#ifndef __EMSCRIPTEN__
    nativeTransport_ = nullptr;
#endif
}

#ifndef __EMSCRIPTEN__
void App::setNetConfig(const net::NativeNetConfig& c) { netConfig_ = std::make_unique<net::NativeNetConfig>(c); }

const net::NativeNetConfig& App::netConfig() const {
    static const net::NativeNetConfig defaults;
    return netConfig_ ? *netConfig_ : defaults;
}

bool App::netStartLocalServer(uint16_t port, bool lan, std::string* error) {
    netStopLocalServer();
    auto s = std::make_unique<net::HostServer>();
    net::HostServerConfig c;
    c.port = port;
    c.lan = lan;
    c.log = [](const std::string& line) { logInfo("network server: %s", line.c_str()); };
    if (!s->start(c, error) || !s->startThread()) return false;
    net::NativeNetConfig cfg = netConfig();
    cfg.server = "ws://127.0.0.1:" + std::to_string(s->port()) + "/net";
    setNetConfig(cfg);
    netServer_ = std::move(s);
    return true;
}

void App::netStopLocalServer() {
    if (netServer_) netServer_->stop();
    netServer_.reset();
}
#endif

int App::netDefaultTeam(const std::string& car) const {
    for (const CarEntry& c : cars_)
        if (c.baseName == car) return (c.infoFlags & 8) ? 0 : 1;
    return 1;
}

int App::netCarIndex(std::string& car, int& paint) const {
    for (size_t i = 0; i < cars_.size(); ++i)
        if (iequals(cars_[i].baseName, car)) return int(i);
    car = "vpbug";
    paint = 0;
    for (size_t i = 0; i < cars_.size(); ++i)
        if (iequals(cars_[i].baseName, car)) return int(i);
    return cars_.empty() ? -1 : 0;
}

void App::cityForGame(const LevelSet& levelSet) {
    City* city = cityView_.city();
    if (!city || city->data().levelSet() == levelSet) return;
    for (const CityEntry& c : cityView_.cities())
        if (iequals(c.mapName, city->name())) {
            logInfo("loading %s again with the path sets of %s", c.mapName.c_str(), levelSet.race.c_str());
            // A drive in the old city (the race before a "next race") lets go of it first.
            driveView_.stop();
            cityView_.loadCity(c, ctx_, levelSet);
            pageEvent("city", "{\"reloaded\":true}");
            return;
        }
}

std::string App::startNetGame() {
    net::NetGame* g = netSession_ ? netSession_->game() : nullptr;
    if (!g) return "{\"ok\":false,\"why\":\"no game\"}";
    const net::Settings st = g->settings();
    const CityEntry* entry = nullptr;
    for (const CityEntry& c : cityView_.cities())
        if (!c.testLayout && iequals(c.raceDir, st.city)) entry = &c;
    if (!entry) return "{\"ok\":false,\"why\":\"the game's city is not installed\"}";
    // The level objects of a network game (1.4): no ferries, parked cars only in the race modes,
    // London's drawbridges open, the path-set variants of the mode's race name.
    LevelSet ls;
    ls.race = net::netRaceName(st.mode, st.race);  // "roam" for Cruise
    ls.ferries = false;
    ls.parkedCars = net::isRaceMode(st.mode);
    ls.openBridges = iequals(entry->raceDir, "london");
    if (driveView_.active()) stopDrive();
    City* city = cityView_.city();
    if (!city || !iequals(city->name(), entry->mapName) || city->data().levelSet() != ls) {
        logInfo("loading %s for the network game", entry->mapName.c_str());
        cityView_.loadCity(*entry, ctx_, ls);
        city = cityView_.city();
        pageEvent("city", "{\"reloaded\":true}");
    }
    if (!city) return "{\"ok\":false,\"why\":\"the city cannot be loaded\"}";
    // The local car (5.3: the cops-and-robbers sub-mode forces the team's car).
    net::PlayerRecord& rec = netSession_->localRecord();
    if (st.mode == net::ModeCops && st.subMode == net::CopsAndRobbers) {
        rec.car = rec.team == 0 ? "vpcop" : "vpmustang99";
        rec.paint = 0;
    }
    const int ci = netCarIndex(rec.car, rec.paint);
    if (ci < 0) return "{\"ok\":false,\"why\":\"no cars\"}";
    rec.city = entry->raceDir;
    netSession_->publishRecord();
    DriveView::GameSetup gs = haveNetSetup_ ? netSetup_ : DriveView::GameSetup();  // the front end's driver
    gs.game = true;
    gs.mode = st.mode;
    gs.singlePlayer = false;  // no pause (1.5)
    driveView_.setGame(gs);
    int w = 1, h = 1;
    platform_.drawableSize(w, h);
    std::string r = driveView_.startNetGame(cars_[size_t(ci)], rec.paint, city, *entry, *netSession_, ctx_,
                                            float(w) / float(h > 0 ? h : 1));
    if (driveView_.active()) {
        view_ = 2;
        if (platform_.pointerLocked()) platform_.setPointerLock(false);
        music::GameMusicSetup m;
        m.race = net::isRaceMode(st.mode);  // singlerace / singleroam (music.md)
        m.mapName = driveView_.mapName();
        music_.startGame(m);
    }
    return r;
}

void App::netFrame(float dt) {
    if (!netSession_) return;
    netSession_->pump(clock());
    if (netSession_->takeGameEnded() && driveView_.netActive()) {
        driveView_.viewBytes(netView_);  // the front end writes them into the driver's .cfg
        stopDrive();
        driveView_.setGame(DriveView::GameSetup());
        pageEvent("drive", "{\"active\":false,\"net\":true}");
    }
    if (netSession_->takeGameStarting()) {
        const std::string r = startNetGame();
        pageEvent("netstart", r);
    }
    std::vector<net::Notice> notices = netSession_->takeNotices();
    if (driveView_.netActive()) {
        driveView_.netNotices(notices);
    } else if (!notices.empty()) {
        JsonWriter j;
        j.beginArray();
        for (const net::Notice& n : notices) {
            j.beginObject();
            j.field("kind", n.kind == net::Notice::Chat ? "chat" : "hud");
            j.field("text", n.text);
            j.field("name", latin1ToUtf8(n.name));
            j.field("line", latin1ToUtf8(n.line));
            j.field("twoLines", n.twoLines);
            j.endObject();
        }
        j.endArray();
        pageEvent("netnotice", j.str());
    }
    netStateTimer_ += dt;
    const bool closedNow = netSession_->closed() && !netClosedSent_;
    if (netStateTimer_ >= 0.25f || closedNow) {
        netStateTimer_ = 0;
        if (closedNow) netClosedSent_ = true;
        pageEvent("net", netStateJson());
    }
}

std::string App::netStateJson() const {
    JsonWriter j;
    j.beginObject();
    const net::Session* s = netSession_.get();
    j.field("session", s != nullptr);
    if (!s) {
        j.endObject();
        return j.str();
    }
    j.field("open", s->open());
    j.field("closed", s->closed());
    j.field("reason", s->closedReason());
    j.field("self", (long long)s->self());
    j.field("host", (long long)s->host());
    j.field("isHost", s->isHost());
    j.field("sealed", s->sealed());
    std::string why;
    j.field("canStart", s->canStart(&why));
    j.field("why", why);
    j.field("canJoinRunning", s->canJoinRunningGame());
    j.field("inGame", s->game() != nullptr);
    j.field("haveSettings", s->haveSettings());
    if (s->haveSettings()) {
        const net::Settings& st = s->settings();
        j.key("settings").beginObject();
        j.field("mode", st.mode);
        j.field("city", st.city);
        j.field("race", st.race);
        j.field("skill", st.skill);
        j.field("timeOfDay", st.timeOfDay);
        j.field("weather", st.weather);
        j.field("laps", st.laps);
        j.field("timeLimit", double(st.timeLimit));
        j.field("pedDensity", double(st.pedDensity));
        j.field("subMode", st.subMode);
        j.field("goldWeight", st.goldWeight);
        j.field("limitType", st.limitType);
        j.field("limitChoice", st.limitChoice);
        j.endObject();
    }
    j.key("players").beginArray();
    for (const net::Player& p : s->players()) {
        j.beginObject();
        j.field("id", (long long)p.id);
        j.field("name", latin1ToUtf8(p.rec.name));
        j.field("car", p.rec.car);
        j.field("paint", p.rec.paint);
        j.field("team", p.rec.team);
        j.field("ready", p.rec.ready);
        j.field("inGame", p.rec.inGame);
        j.field("host", p.id == s->host());
        j.field("local", p.id == s->self());
        j.field("cheater", p.cheater);
        j.field("slowLink", p.rec.slowLink);
        j.endObject();
    }
    j.endArray();
    j.endObject();
    return j.str();
}

}  // namespace cc
