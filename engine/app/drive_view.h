// Drive mode in the viewer: a DriveSession drawn in the loaded city (or on flat ground), with the
// HUD sent to the page.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ai/pedestrians.h"
#include "ai/police.h"
#include "ai/traffic.h"
#include "app/car_view.h"
#include "app/cockpit.h"
#include "app/hud_view.h"
#include "app/engine_context.h"
#include "audio/game_audio.h"
#include "drive/drive_session.h"
#include "fx/car_fx.h"
#include "fx/effects_world.h"
#include "game/hud.h"
#include "formats/cinfo.h"
#include "game/race.h"
#include "model/light_bar.h"
#include "physics/collision_world.h"
#include "render/camera.h"

namespace cc {

namespace net {
class Session;
class NetGame;
class RemoteCar;
struct Notice;
}  // namespace net

class City;
struct CityCar;
struct InputState;
struct Pkg;
struct Mesh;

class DriveView {
public:
    enum StartMode { RandomStart = 0, RepeatableStart = 1, UnderCamera = 2 };
    DriveView();
    ~DriveView();

    // Starts driving `car` in `city` (nullptr = flat ground). Returns a JSON result.
    std::string start(const CarEntry& car, int paint, City* city, StartMode mode, const FreeFlyCamera& fly,
                      EngineContext& ctx, float aspect);
    void stop();
    bool active() const { return session_ != nullptr; }
    // The running session (the player's car for the native force feedback), or null.
    const DriveSession* session() const { return session_.get(); }
    City* city() const { return city_; }
    void setOption(const std::string& name, int value);
    // Traffic density 0 … 1 (0 = off); applied at once to a running drive.
    void setTrafficDensity(float d, EngineContext& ctx);
    // Cruise's cop density 0 … 1 (police.md, "How many cops": the first trunc(d × N) cops of the
    // list; MM2 starts with 1.0); applied at once to a running drive. setCops: the viewer's
    // on/off switch, density 1 or 0.
    void setCopDensity(float d, EngineContext& ctx);
    void setCops(bool on, EngineContext& ctx) { setCopDensity(on ? 1.0f : 0.0f, ctx); }
    // Pedestrian density 0 … 1 (pedestrians.md: the Cruise slider, default 0.25; 0 = off); applied
    // at once to a running drive (a new pool and a reset).
    void setPedDensity(float d, EngineContext& ctx);
    // The "pedestrians off" display option (pedestrians.md, "Settings"): no pedestrians in any mode.
    void setPedsOff(bool off, EngineContext& ctx);
    // The pedestrians' states (debugging and the page).
    std::string pedsJson() const;

    void update(const InputState& in, float dt, EngineContext& ctx, float aspect);
    void render(EngineContext& ctx, int width, int height);

    // ---- the game around the drive (front end): HUD, HUD map, pause, the saved view bytes.
    struct GameSetup {
        bool game = false;        // started from the front end: Esc opens the pause menu
        int mode = 0;             // game mode number (profiles.md)
        bool singlePlayer = true;
        uint8_t view[12] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0};  // view bytes (controls.md)
        bool haveView = false;    // apply `view` (else the viewer's own settings)
        float viewDistance = 0;   // the driver's view distance: the far plane in a game (cameras.md)
        // The driver's controls (players/<name>.cfg): the binding table read for driving and the
        // HUD keys; without them the viewer's own control scheme.
        bool haveControls = false;
        ControlBindings controls;
        // The driver's force-feedback option and strengths (controls.md, "Force feedback").
        bool ffOn = false;
        float ffCollision = 1.0f, ffRoad = 1.0f;
    };
    // Set before start(); start() applies it.
    void setGame(const GameSetup& g) { setup_ = g; }
    bool inGame() const { return setup_.game; }
    // The in-race view distance slider (cameras.md, "Projection": replaces the far plane at once).
    void setViewDistance(float m) { setup_.viewDistance = m; }
    // The current view bytes (written back into the player's .cfg at the game end).
    void viewBytes(uint8_t out[12]) const;
    // Pause (Esc menu, F2, the full-screen map): physics, AI and clocks stop.
    void setPaused(bool p, bool menu);
    bool paused() const { return paused_; }
    // The game mode of the drive (MM2 numbers: 0 Cruise, 1 Checkpoint, 3 Circuit, 4 Blitz, 6 Crash Course).
    int raceMode() const;
    bool pauseMenu() const { return pauseMenu_; }
    // F4 / the pause menu's restart (races.md, "Restart"; Cruise: the world reset).
    void restart(EngineContext& ctx);
    game::Hud& hud() { return hud_; }
    // Text by mmlang.dll number for the HUD lines ("" when unknown).
    void setTextSource(std::function<std::string(int)> f) { text_ = std::move(f); }
    std::string hudJson(int width, int height) const;
    // Scripted checks of the front end: put the player's car on the race's target gate (facing
    // through it), so a race can be run to its end without driving it.
    bool debugJumpToTarget();
    bool mirrorOn() const { return mirrorOn_; }
    bool leaveRequested() const { return (session_ && session_->leaveRequested()) || leaveViewer_; }
    // Where the fly camera should go when driving stops.
    vec3 cameraPosition() const;
    float cameraYaw() const;
    std::string readout() const;
    // The cops' states (debugging and the page).
    std::string policeJson() const;

    // ---- Race modes (game/race.h) ----
    // Starts race `race` of `mode` (game::RaceMode numbers) in `city`; `circuitOpponents` −1 =
    // the race's number. Returns a JSON result.
    // `nextRace`: started by the results' "next race" (results.md: Checkpoint and Blitz then keep the
    // session's pedestrian density).
    std::string startRace(const CarEntry& car, int paint, City* city, const CityEntry& entry, int mode, int race, bool pro,
                          int circuitOpponents, bool noDamage, EngineContext& ctx, float aspect, bool nextRace = false);
    void restartRace();
    // The player's own race-menu choices for the next startRace only (menus.md, "Locked settings";
    // −1 = the race's own): time of day and weather (so the rain grip follows them too,
    // vehicle-wheels.md), the Circuit laps, the cop and pedestrian densities.
    struct RaceChoices {
        int timeOfDay = -1, weather = -1, laps = -1;
        float copDensity = -1.0f, pedDensity = -1.0f, trafficDensity = -1.0f;
    };
    void setRaceChoices(const RaceChoices& c) { raceChoices_ = c; }
    // "Next race" from the results (results.md): what to start (startRace with nextRace = true,
    // which keeps a Checkpoint or Blitz session's pedestrian density).
    struct NextRace {
        bool offered = false;
        int mode = 0, race = 0;
        bool pro = false;
        int opponents = -1;
        std::string car;  // a replacement car (Crash Course), empty = keep
    };
    NextRace nextRace() const;
    bool racing() const { return race_ != nullptr; }
    // The game's own clock (the sum of frame times): the page's catch-up while it is hidden.
    double gameClock() const { return clock_; }
    // The plain race overlay's data (countdown, gates, place, clocks, results).
    std::string raceJson() const;
    // The records the race writes to (the UI builder's profiles); null: nothing is recorded.
    void setRaceRecords(game::RaceRecords* r) { records_ = r; }
    // Race sound, speech and music events (the sound builder plays them).
    void setRaceEventSink(std::function<void(const game::RaceEvent&)> f) { raceSink_ = std::move(f); }
    // ---- Network games (net/net_game.h; spec/game/multiplayer.md) ----
    // Starts the session's running game in `city` (loaded with the network level set) with the
    // local player's car: no traffic, police, opponents or cable cars, the session's pedestrians,
    // time of day and weather, no trailer in Cruise and Cops and Robbers, and the game attached.
    std::string startNetGame(const CarEntry& car, int paint, City* city, const CityEntry& entry, net::Session& session,
                             EngineContext& ctx, float aspect);
    bool netActive() const { return net_ != nullptr; }
    // The session's notices: HUD lines go to the HUD, chat lines to the page.
    void netNotices(const std::vector<net::Notice>& notices);
    // The plain scoreboard's data: mode, state, places or points, the players, results, the name
    // tags (screen positions), the gold and bases.
    std::string netJson(int width, int height) const;

    // Car and chase camera state for measuring (heading, yaw rate, inputs, camera distance).
    std::string debugJson() const;
    // The game's sound state (slots, chase count, echo), or "" when not driving.
    std::string soundJson() const { return audio_ ? audio_->statsJson() : std::string(); }
    audio::GameAudio* audio() const { return audio_.get(); }
    // What the music rules read (music.md, "Each frame of a game"): values after the last frame.
    struct MusicCues {
        float speed = 0;
        int chase = 0;
        bool airborne = false;
        bool echo = false;
    };
    MusicCues musicCues() const;
    // The level name (MapName) for the music's ambience table; "" on flat ground.
    std::string mapName() const;

private:
    void handleHudKeys(const InputState& in, EngineContext& ctx);
    ActionPresses hudPresses_;  // the HUD actions of a game's binding table
    // The force feedback's device: the platform's haptic effects.
    class PlatformFf : public FfDevice {
    public:
        Platform* p = nullptr;
        bool playing(Effect e) const override;
        void start(Effect e) override;
        void stop(Effect e) override;
        void setFriction(int c) override;
        void setSpring(int c) override;
        void setRumble(int periodUs, int magnitude) override;
        void jolt(int strength, int direction) override;
    } ffDevice_;
    bool ffWasActive_ = false, ffPopup_ = false;
    // Configures the session's force feedback for this frame (paused: the reset).
    void forceFeedbackFrame(const InputState& in, EngineContext& ctx);
    // The HUD from the race session's state (timers, messages, panel, arrow, markers, map).
    void syncRaceHud();
    std::string raceHudText(int id, const std::string& arg) const;
    void applyMapMode(int oldMode);
    void renderScene(EngineContext& ctx, int width, int height, int vx, int vy, int vw, int vh, bool letterbox,
                     bool wideFov);
    void drawMap(EngineContext& ctx, int width, int height);
    void postWaterText();
    GameSetup setup_;
    HudMapView map_;
    HudArrow arrow_;
    game::Hud hud_;
    bool paused_ = false, pauseMenu_ = false, mapPause_ = false;
    bool leaveViewer_ = false;  // viewer drive: Esc on a paused (full-screen map) drive leaves
    bool iconsOn_ = true;
    bool wideSaved_ = false;     // view byte 2 (the player's wide-view choice)
    bool dashRestore_ = false;   // the dash view was on when the half-screen map took over
    bool waterPrev_ = false;
    bool rhd_ = false;           // right-hand drive (.info Flags bit 64)
    std::string mapCity_;        // MapName (background colour, water text)
    std::function<std::string(int)> text_;
    float lastDt_ = 0.0f;
    int lastW_ = 0, lastH_ = 0;  // the size the 3D view was last drawn at (the HUD's fractions)
    void viewSize(EngineContext& ctx, int& w, int& h) const;
    void drawOverlays(EngineContext& ctx, int width, int height, const CameraFrame& cf, float fov, float aspect,
                      float farPlane);
    Cockpit cockpit_;
    MirrorSettings mirror_;
    bool mirrorOn_ = false;
    std::unique_ptr<DriveSession> session_;
    City* city_ = nullptr;
    phys::CollisionWorld flat_;  // the flat ground (set up on first use)
    bool flatReady_ = false;
    size_t brokenSeen_ = 0;
    float hudTimer_ = 0;
    bool automatic_ = true, autoReverse_ = true;
    float trafficDensity_ = 0.5f;  // Cruise default (traffic-population.md)
    std::vector<ai::TrafficPlayer> trafficPlayers() const;
    std::vector<ai::PolicePlayer> policePlayers() const;
    std::vector<ai::PedPlayer> pedPlayers() const;
    float pedDensity_ = 0.25f;  // the Cruise slider (pedestrians.md: default 0.25)
    bool pedsOff_ = false;      // the display option
    // The session's pedestrian density and mode (Cruise: the slider; a race: its settings row, or
    // kept on Checkpoint/Blitz "next race"; Circuit: no pedestrians whatever the density).
    float sessionPedDensity_ = 0.25f;
    bool sessionCircuit_ = false;
    // A new pool for the session's density (the generator restarted before it), then a reset.
    void startPeds(EngineContext& ctx);
    int pedCount() const;  // the pedestrian pool of this session's settings
    // A reset of the pedestrians around the player's reset position (session and race resets).
    void resetPeds();
    void startTraffic(EngineContext& ctx);
    void startPolice(EngineContext& ctx);
    void addCopCars(std::vector<CityCar>& cars) const;
    bool trailerCar(CityCar& out) const;
    float copDensity_ = 1.0f;  // MM2 starts Cruise with cop density 1
    std::string carName_;
    float lastF_ = 0.0f;
    LightBar playerLights_;  // a player driving a car with SRN parts: the horn toggles it
    bool hornPrev_ = false;
    double clock_ = 0.0;
    phys::Scene scene_;
    std::vector<phys::SceneMover> movers_;
    phys::SceneHooks sceneHooks_;
    // Visual effects (damage-visuals.md, effects.md): the player's car and each cop car; traffic
    // has none. The world is declared first so it outlives the cars' effects.
    std::unique_ptr<fx::EffectsWorld> effects_;
    std::unique_ptr<fx::CarFx> playerFx_;
    std::vector<std::pair<const phys::Car*, std::unique_ptr<fx::CarFx>>> copFx_;
    void attachCopEffects();
    bool fxOff_ = false;  // debugging: effects off
    fx::CarFx* copEffects(const phys::Car* car) const;
    // Sound (spec/audio): made at the start (or once the sound data has arrived).
    void startAudio(EngineContext& ctx);
    std::unique_ptr<audio::GameAudio> audio_;
    bool audioPending_ = false;
    int raceNo_ = 0;  // the race index of a race (Crash Course course for the instructor)
    int echoRoom_ = 0;
    bool echo_ = false;  // the echo state of the last frame (music cue)
    phys::World* world_ = nullptr;
    // Race modes.
    struct RaceHostImpl;
    std::unique_ptr<RaceHostImpl> raceHost_;
    std::unique_ptr<game::RaceSession> race_;
    game::RaceRecords noRecords_;
    game::RaceRecords* records_ = &noRecords_;
    std::function<void(const game::RaceEvent&)> raceSink_;
    std::vector<vec3> oppPos_;
    std::vector<int> propOf_;
    std::vector<std::string> raceEvents_;  // the last events, for the page
    float raceHudTimer_ = 0;
    double wall_ = 0;  // real time (seconds): the opponents' wreck repair clock (driver.md)
    ai::PlayerTracking tracking_;  // the player's tracked road (driver-planner.md §16 step 3)
    int raceOpponents_ = -1;
    RaceChoices raceChoices_;
    std::shared_ptr<const Pkg> gatePkg_[2];
    const Mesh* gateMesh_[2] = {nullptr, nullptr};
    void raceAiFrame(float F);
    // Scripted checks: the player's car driven by the AI driver toward the race's target gate.
    void setAutopilot(bool on);
    std::unique_ptr<ai::Driver> autoDriver_;
    void addOpponentCars(std::vector<CityCar>& cars) const;
    void drawGates(EngineContext& ctx);
    void endRace();
    // Network games.
    struct NetHostImpl;
    std::shared_ptr<NetHostImpl> netHost_;  // shared: its deleter is made where the type is complete
    net::Session* netSession_ = nullptr;
    net::NetGame* net_ = nullptr;
    bool netNoAi_ = false;  // building a network game's world: no traffic, police or cable cars
    bool trailerAllowedNext_ = true;  // start(): the player's trailer (none in network Cruise and Cops and Robbers)
    int audioMode_ = 0;               // start(): the game mode of the sound set-up
    float debugThrottle_ = 0, debugSteer_ = 0;  // setOption("debugthrottle" / "debugsteer")
    struct NetCarView {
        net::RemoteCar* rc = nullptr;
        std::unique_ptr<VehicleModel> model;
        std::unique_ptr<fx::CarFx> fx;
        LightBar lights;
        bool horn = false, siren = false;
    };
    std::vector<std::unique_ptr<NetCarView>> netCars_;
    std::shared_ptr<const Pkg> netPkg_[5];  // wpobj_gold, pt_hideout, pt_bank, pt_red, pt_blue
    const Mesh* netMesh_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    float netHudTimer_ = 0;
    std::vector<std::pair<std::string, float>> netChat_;  // chat lines and the time they have left
    void endNetGame();
    void netFrameHud(float dt);
    void addNetCars(std::vector<CityCar>& cars) const;
    void drawNetObjects(EngineContext& ctx);
    void netMapItems(std::vector<HudMapView::CarMarker>& cars, std::vector<HudMapView::Dot>& dots) const;
};

}  // namespace cc
