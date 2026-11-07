// The viewer application: owns the file system, caches, renderer and the two views, and answers
// the side panel's calls (app/web_api.cpp).
#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/car_view.h"
#include "app/city_view.h"
#include "app/drive_view.h"
#include "app/engine_context.h"
#include "audio/soft_mixer.h"
#include "audio/sound_system.h"
#include "music/music_system.h"
#include "platform/platform.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "ui/gl_backend.h"
#include "ui/host.h"
#include "ui/ui.h"
#include "vfs/vfs.h"

namespace cc {

class AssetCache;
class TextureCache;
namespace net {
class Session;
class Transport;
class NetLink;
class WebTransport;
class NativeTransport;
class HostServer;
struct NativeNetConfig;
}  // namespace net
namespace fe {
class FrontEnd;
class Services;
}  // namespace fe

class App {
public:
    App();
    ~App();

    bool init();
    // Natively, before the program ends: every GPU object the app holds is released while the GL
    // context still exists (drive, city, car view, caches, the UI's and the renderer's programs);
    // the app's destructor then does no GL work. Network sessions end first (netEnd). Idempotent.
    void shutdown();
    void frame();
    // `n` frames of `dt` seconds at once (scripted checks; works while the page is hidden).
    void stepFrames(int n, float dt);

    // Archives registered by the page, mounted together by mount().
    void clearArchives();
    void addArchive(const std::string& name, std::shared_ptr<ByteSource> source);
    std::string mount();  // JSON summary

    std::string carListJson() const;
    std::string selectCar(int index);  // JSON car info
    std::string carInfoJson() const { return carView_.infoJson(); }
    void setPaint(int c) { carView_.setPaint(c); }
    void setLod(int l);
    void setCarOption(const std::string& name, int v) { carView_.setOption(name, v); }
    void resetCamera();

    void setView(int v);  // 0 car, 1 city (2 = driving, set by startDrive)
    std::string cityListJson() const;
    std::string loadCity(int index);  // JSON city info
    void setCityOption(const std::string& name, int v) {
        if (name == "far") ctx_.drawDistance = v;  // fly camera and Drive mode share it
        else cityView_.setOption(name, v, ctx_);
    }
    std::string dropCar();
    void setCityCamera(float x, float y, float z, float yaw, float pitch);
    // Drive mode: mode 0 random free-roam start, 1 repeatable start, 2 under the camera;
    // flat = drive on flat ground instead of the loaded city.
    std::string startDrive(int mode, bool flat);
    void stopDrive();
    void setDriveOption(const std::string& name, int v) {
        if (name == "traffic") driveView_.setTrafficDensity(float(v) / 100.0f, ctx_);  // percent; 0 = off
        else if (name == "cops") driveView_.setCops(v != 0, ctx_);  // the viewer's switch: density 1 or 0
        else if (name == "peds") driveView_.setPedDensity(float(v) / 100.0f, ctx_);  // percent
        else if (name == "pedsoff") driveView_.setPedsOff(v != 0, ctx_);
        else driveView_.setOption(name, v);
    }
    // Cruise's cop density 0 … 1 (the front end's slider, police.md "How many cops").
    void setCopDensity(float d) { driveView_.setCopDensity(d, ctx_); }
    std::string statsJson() const;
    // Sound options from the page (effects, ambient, commentary on/off; volume 0-100; balance
    // -100-100) and the sound readout.
    void setSoundOption(const std::string& name, int v);
    std::string soundJson() const;
    // Race modes in the loaded city: its races per mode, start / restart, the overlay state.
    std::string raceListJson() const;
    // `nextRace`: started by the results' "next race" (Checkpoint and Blitz keep the pedestrians).
    std::string startRace(int mode, int race, bool pro, int circuitOpponents, bool nextRace = false);
    void restartRace() { driveView_.restartRace(); }
    // "Next race" from the results (results.md).
    std::string nextRace();
    std::string raceJson() const { return driveView_.raceJson(); }
    std::string policeJson() const { return driveView_.policeJson(); }
    std::string pedsJson() const { return driveView_.pedsJson(); }
    std::string driveDebugJson() const { return driveView_.debugJson(); }

    // ---- Network games (spec/game/multiplayer.md; app/net_api.cpp): the session of the page's
    // panel. A new session object (the page then connects it through web/net.js; natively the
    // front end hosts or joins through nativeTransport()), its end, and the panel's state.
    void netBegin(const std::string& name, const std::string& car, int paint);
    void netEnd();
    net::Session* netSession() { return netSession_.get(); }
    net::Transport* netTransport() { return netTransport_.get(); }
    net::WebTransport* webTransport() { return webTransport_; }  // the browser's (null natively)
    // The front end's network layer (web: platform/web_net_link.h; native:
    // platform/native_net_link.h). Without one the Multiplayer screens say there are no network
    // games in this build.
    void setNetLink(net::NetLink* l) { netLink_ = l; }
    net::NetLink* netLink() { return netLink_; }
#ifndef __EMSCRIPTEN__
    // Native builds (platform/native_transport.*, platform/host_server.*): the transport's settings
    // for the next netBegin (the server, direct UDP, TLS), the session's transport (host, join,
    // the game list, the links), and "host on this computer": the engine's own session server on
    // its own thread, on 127.0.0.1 or (LAN mode) every interface; the next transports then use
    // it (ws://127.0.0.1:<port>/net). False and `error` when the port is taken.
    void setNetConfig(const net::NativeNetConfig& c);
    const net::NativeNetConfig& netConfig() const;
    net::NativeTransport* nativeTransport() { return nativeTransport_; }
    bool netStartLocalServer(uint16_t port, bool lan, std::string* error = nullptr);
    void netStopLocalServer();
    net::HostServer* netLocalServer() { return netServer_.get(); }
#endif
    std::string netStateJson() const;
    // The local player's car for a game: the record's car if it is in the car list (Cops and
    // Robbers' cops-and-robbers sub-mode forces vpcop / vpmustang99, 5.3), else vpbug paint 0.
    int netCarIndex(std::string& car, int& paint) const;
    // The default Cops and Robbers team of a car (5.3, the free-for-all rule): 0 for a police car
    // (.info Flags bit 8), else 1.
    int netDefaultTeam(const std::string& car) const;
    // The current driver's locks for the network car choice (1.3 step 1): 0 free, 1 the car is
    // locked, 2 the paint is locked. Set by the front end (app/ui_api.cpp); none: nothing locked.
    void setCarLockQuery(std::function<int(const std::string&, int)> f) { carLockQuery_ = std::move(f); }
    // The front end's driver settings for the next network game (view bytes, view distance:
    // app/ui_api.cpp); nullptr = the viewer's own (the plain panel's games).
    void setNetGameSetup(const DriveView::GameSetup* g) {
        haveNetSetup_ = g != nullptr;
        netSetup_ = g ? *g : DriveView::GameSetup();
    }
    // The view bytes of the last network game, kept when it stopped (the game end, progression.md).
    const uint8_t* netLastView() const { return netView_; }

    // For the front end (app/ui_api.cpp): the loaded data and the views.
    Vfs& vfs() { return vfs_; }
    EngineContext& context() { return ctx_; }
    const std::vector<CarEntry>& cars() const { return cars_; }
    int selectedCar() const { return selectedCar_; }
    CityView& cityView() { return cityView_; }
    DriveView& driveView() { return driveView_; }
    double clock() const { return platform_.timeSeconds() - startTime_; }
    // Music (spec/audio/music.md): the option (0 off, 1 music, 2 city ambience), the volume, the
    // player's own General MIDI bank (DLS or SF2 bytes), race/pause events for testing, status.
    void setMusicMode(int mode);
    void setMusicVolume(float v) { music_.setVolume(v); }
    // `bundled`: the fallback bank shipped with the page, used when the player has none.
    std::string setMusicGmBank(const std::string& name, const Bytes& data, bool bundled = false);
    std::string musicStatusJson() const { return music_.statusJson(); }
    void musicEvent(const std::string& name);
    // The one audio output: interleaved stereo at audioRate() (the music's 22,050 Hz), the music
    // with the sound system's voices mixed in.
    // system will mix it into its own output).
    int audioRate() const { return music::MusicSystem::kRate; }
    void renderAudio(float* out, int frames);

    // ---- The engine's own UI (ui/, app/front_end.h): everything visible besides the 3D view.
    // The platform's hooks (clipboard, storage, file choices, address parameters); set before
    // init() or right after it.
    void setHost(ui::Host* h);
    ui::Host& host() { return *host_; }
    fe::FrontEnd* frontEnd() { return fe_.get(); }
    ui::Ui& ui() { return ui_; }
    Platform& platform() { return platform_; }
    // UI events from outside the platform's event loop (the page's paste and copy events).
    void pushUiEvent(const ui::InputEvent& e) { pendingUiEvents_.push_back(e); }
    // The position readout of the active view (fly camera, or the car).
    const std::string& readout() const { return readout_; }
    // The last mount's summary (JSON).
    const std::string& mountSummary() const { return mountSummary_; }
    // The first game controller's buttons (0 = none), for the profiles' default bindings.
    // The buttons of the game controller (the one found at start-up natively; the page's first
    // gamepad in a browser); 0 without one.
    const InputState& input() const { return input_; }
    // Engine choice for merged controllers: the most buttons of any controller connected at
    // start-up or since.
    int gamepadButtons() const { return std::max(padButtons_, platform_.startupControllerButtons()); }

private:
    void buildCarList();
    void netFrame(float dt);
    std::string startNetGame();
    // The loaded city again with the game's path sets and moving objects when it was loaded with
    // others (races.md, "Path sets of a race"; multiplayer.md 1.4).
    void cityForGame(const LevelSet& levelSet);
    std::function<int(const std::string&, int)> carLockQuery_;
    net::NetLink* netLink_ = nullptr;
    std::unique_ptr<net::Transport> netTransport_;
    net::WebTransport* webTransport_ = nullptr;
#ifndef __EMSCRIPTEN__
    net::NativeTransport* nativeTransport_ = nullptr;
    std::unique_ptr<net::NativeNetConfig> netConfig_;
    std::unique_ptr<net::HostServer> netServer_;
#endif
    std::unique_ptr<net::Session> netSession_;
    float netStateTimer_ = 0;
    bool netClosedSent_ = false;
    DriveView::GameSetup netSetup_;
    bool haveNetSetup_ = false;
    uint8_t netView_[12] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0};

    Platform platform_;
    InputState input_;
    audio::SoundSystem sound_;
    audio::SoftMixer mixer_;  // the sound system's voices, mixed into renderAudio()
    void startMusicPhase();   // menus, or the game being driven
    void raceMusicEvent(const std::string& name);
    std::string lastRaceEvent_;
    bool gamePaused_ = false;
    Renderer renderer_;
    Vfs vfs_;
    std::unique_ptr<AssetCache> assets_;
    std::unique_ptr<TextureCache> textures_;
    MeshCache meshes_;
    EngineContext ctx_;

    struct PendingArchive {
        std::string name;
        std::shared_ptr<ByteSource> source;
    };
    std::vector<PendingArchive> pending_;

    std::vector<CarEntry> cars_;
    int selectedCar_ = -1;
    CarView carView_;
    CityView cityView_;
    DriveView driveView_;
    music::MusicSystem music_;
    bool musicPauseMenu_ = false;  // the pause menu's state the music last saw
    int view_ = 0;
    bool ready_ = false;
    double startTime_ = 0, lastTime_ = 0;
    double readoutTimer_ = 0;
    float frameMs_ = 0;
    float cpuMs_ = 0;  // time spent building a frame
    float fixedDt_ = -1.0f;
    // The UI.
    ui::Host defaultHost_;
    ui::Host* host_ = &defaultHost_;
    ui::Ui ui_;
    ui::GlBackend uiGl_;
    int padButtons_ = 0;  // the most buttons any connected controller has had
    bool uiOk_ = false;
    bool uiSceneKeys_ = false;  // this frame's keys and controller go to the views (ui::FrameInput)
    std::unique_ptr<fe::Services> feServices_;
    std::unique_ptr<fe::FrontEnd> fe_;
    std::vector<ui::InputEvent> pendingUiEvents_;
    struct Event {
        std::string name, json;
    };
    std::vector<Event> events_;
    struct LogEntry {
        int level;
        std::string kind, msg;
    };
    std::vector<LogEntry> logs_;
    std::string readout_;
    std::string mountSummary_;
    bool textInputOn_ = false;
    void uiFrame(int w, int h, float dt);
};

App& theApp();

}  // namespace cc
