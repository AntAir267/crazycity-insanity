// The game's whole visible interface, drawn by the engine (ui/): the viewer (cars, cities, the
// drive and race cards, sound, music, network, problems), the front end (main menu, single
// player race menu, car screen, Crash Course, drivers, driver record, race statistics, options,
// multiplayer start and lobby), loading screens, the driving HUD with the HUD map's frame, the
// pause, quit and results menus, and the network game's scoreboard, name tags and chat.
//
// It is the port of the page's web/ui.js, web/mp.js and the side panel of web/app.js, with the
// same behaviour; game rules and player files stay in the engine (app/menu_data.h,
// app/game_api.h, net/). The App runs one per program (App::frame calls frame()).
#pragma once

#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/fe_services.h"
#include "core/json_value.h"
#include "net/net_link.h"
#include "ui/ui.h"

namespace cc {
namespace fe {

class FrontEnd {
public:
    FrontEnd(Services& s, ui::Host& host);
    ~FrontEnd();

    // Every frame: builds what is visible.
    void frame(ui::Ui& ui);
    // Where the 3D view goes (device px; the whole window unless the viewer's panel takes a side).
    ui::Rect sceneRect(int w, int h, float scale) const;
    // The 3D views get the keyboard (the scene has the focus and no menu or text field takes it).
    bool viewsGetKeys(const ui::Ui& ui) const;
    // The 3D views get the pointer (it is over the scene, or a drag began there).
    bool viewsGetPointer(const ui::Ui& ui) const;
    bool sceneDim() const { return dim_; }

    // ---- from the program
    void onEngineEvent(const std::string& name, const std::string& json);
    void onLog(int level, const std::string& kind, const std::string& msg);
    // The archives were mounted (cc_mount's summary).
    void onMounted(const std::string& summaryJson);
    // The page's loading state before the data is in (status line, progress 0 … 1 or < 0).
    void setLoadStatus(const std::string& text, float progress);
    void setOverlay(const std::string& title, const std::string& text);
    void setDataSource(const std::string& s) { source_ = s; }
    void setDropHover(bool on) { dropHover_ = on; }
    // The player's mmlang.dll (from the chosen folder or the dev server); `keep`: remember it.
    void loadStrings(const uint8_t* data, size_t size, bool keep);
    // The chosen game folder has a players folder (its files are held by the host until the
    // front end offers the import).
    void setPendingPlayersFolder(bool on) { pendingFolder_ = on; }
    // A players folder import ran (the files went into menu::importFile): its report.
    void importFinished();
    // The dev server's players folder can be imported (`desktop`: the desktop app's, which is
    // the player's own MM2 folder).
    void setDevPlayers(bool on, bool desktop = false) {
        devPlayers_ = on;
        devIsDesktop_ = desktop;
    }
    // The page asks to open the front end (?play) once ready.
    void openFrontEnd();
    // The front end open on one screen ("mp": the Multiplayer screen, e.g. a command-line game).
    void openScreen(const std::string& screen);
    bool inPlay() const { return play_; }
    // Scripted checks (the page's test hooks).
    std::string screenName() const { return play_ ? screen_ : std::string("viewer"); }
    // A game started as if from the menus (tests, the native program's command line); `env`
    // replaces the race menu's settings where it is not −1 (as the player changing them would).
    struct PlayEnv {
        int tod = -1, weather = -1, opponents = -1;
        double traffic = -1, cops = -1, peds = -1;
    };
    void testPlay(int city, int mode, int race, int car, int paint, const PlayEnv* env = nullptr);
    // The drivers changed outside the menus (an import or a driver chosen by the native program).
    void profilesChanged();
    bool inGame() const { return inGame_; }
    const std::string& bindWarning() const { return bindWarn_; }  // (tests)
    // (tests) The Multiplayer screen's Host and Leave buttons.
    void testNetHost() { mpHost(); }
    void testNetLeave() { mpLeave(); }
    void testQuit(const std::string& screen) { endGame(screen.empty() ? "race" : screen); }

    // (public for the screen files)
    struct Env {
        int tod = 1, weather = 0;
        double traffic = 0.5, peds = 0.25;
        float cops = 1.0f;
        int opponents = 0, laps = 1, maxOpponents = 0;
        double timeLimit = 0, difficulty = 1;
    };
    struct Sel {
        int city = 0, mode = 0, race = 0, car = -1, paint = 0;
        Env env, defaults;
    };
    struct CityInfo {
        int index = 0;
        std::string name, map, raceDir;
        bool test = false;
    };

private:
    friend struct Screens;
    Services& svc_;
    ui::Host& host_;
    ui::Ui* ui_ = nullptr;

    // ---- shared helpers
    void refreshState(const JValue* reply = nullptr);
    void refreshAll();
    void refreshCars();
    void syncSelFromDriver();
    void loadRaces();
    const JValue& racesOf(int mode) const;
    bool modeAvailable(int mode) const;
    void resetEnv();
    bool editable(const std::string& key) const;
    bool ownSettings() const;
    const JValue& driver() const { return state_["driver"]; }
    const JValue& options() const { return state_["options"]; }
    const JValue* carByIndex(int index) const;
    const JValue* carByName(const std::string& name) const;
    std::string carDesc(const std::string& name) const;
    const CityInfo* cityByIndex(int index) const;
    const CityInfo* cityByRaceDir(const std::string& rd) const;
    std::string unlockText(const JValue& why, bool isCar) const;
    void showCar();
    void toast(const std::string& t);

    std::string screenTitle() const;
    const JValue& cached(const std::string& key, const std::function<std::string()>& fn);
    std::map<std::string, std::pair<unsigned, JValue>> caches_;
    ui::Rect sceneRectPx(const ui::Ui& ui) const;  // the scene in px

    // ---- navigation
    void closeFrontEnd();
    void go(const std::string& screen, bool root = false);
    void back();
    void rerender() { ++serial_; }

    // ---- frame parts
    void buildFrontEnd(ui::Ui& ui);
    void buildTopBar(ui::Ui& ui);
    void buildFoot(ui::Ui& ui);
    void driverCard(ui::Ui& ui);
    void buildModal(ui::Ui& ui);
    void buildLoading(ui::Ui& ui);
    void buildPause(ui::Ui& ui);
    void buildHud(ui::Ui& ui);
    void buildMapFrame(ui::Ui& ui);
    void buildNetHud(ui::Ui& ui);
    void buildViewer(ui::Ui& ui);
    void buildViewerOverlays(ui::Ui& ui);

    // ---- screens (fe_screens.cpp)
    void screenMain(ui::Ui& ui);
    void screenDrivers(ui::Ui& ui);
    void screenRace(ui::Ui& ui);
    void envControls(ui::Ui& ui);
    void screenCar(ui::Ui& ui);
    void screenCrashCity(ui::Ui& ui);
    void screenCrash(ui::Ui& ui);
    void screenRecord(ui::Ui& ui);
    void screenStats(ui::Ui& ui);
    void optionsPanel(ui::Ui& ui, bool inRace);

    // ---- dialogs
    enum class Modal { None, NewDriver, Confirm, Report, Keys, Bind };
    void confirmBox(const std::string& title, const std::string& text, const std::string& ok, std::function<void()> onOk);
    void reportBox(const std::string& title, const std::vector<std::string>& notes);
    void closeModal() { modal_ = Modal::None; }
    void confirmImport(std::function<void()> run);
    // ---- customise controls (controls.md's binding table)
    void startBind(int device, int action);
    void bindFrame(ui::Ui& ui);
    void assignBinding(int inputDevice, int component);
    void keysHelp(ui::Ui& ui);

    // ---- a game
    struct Spec {
        int city = 0, mode = 0, race = 0, car = -1, paint = 0;
        int skill = 0;
        Env settings;
        bool ownSettings = true;
        bool haveSettings = false;
        std::string fixedCar;
    };
    void startGame();
    void startRaceSpec(Spec spec);
    void doStart();
    void applyGraphics();
    void applyAudio();
    void restoreViewerOptions();
    void restoreViewerAudio();
    void endGame(const std::string& toScreen);
    void showPauseMenu(bool raceOver = false);
    void resume();
    void raceMenus(const JValue& r);
    void nextRace();

    // ---- multiplayer (fe_net.cpp)
    void screenMp(ui::Ui& ui);
    void renderMpStart(ui::Ui& ui, const JValue& st);
    void renderLobby(ui::Ui& ui, const JValue& st);
    void hostSettings(ui::Ui& ui);
    void settingsSummary(ui::Ui& ui, const JValue& s);
    void playerPanel(ui::Ui& ui, bool carFixed);
    void chatBox(ui::Ui& ui, const std::string& key);
    void mpLive(ui::Ui& ui, const JValue& st);
    void netFrame();
    void mpOnState();
    void mpLobbyLines(const JValue& st);
    void netNotices(const JValue& list);
    void netGameBegin();
    void netGameOver();
    void buildNetResults(ui::Ui& ui);
    void buildNetPause(ui::Ui& ui);
    void buildNetQuit(ui::Ui& ui);
    void openNetChat();
    void closeNetChat();
    bool mpOpen() const;
    const JValue* mpLocal(const JValue& st) const;
    std::string mpName() const;
    const JValue* mpCar();
    void mpDriverLocks();
    void mpLockRules(const JValue& st, bool entering);
    struct NetSettings {
        int mode = 0, city = 0, race = 0, skill = 0, timeOfDay = 1, weather = 0, laps = 1;
        std::string cityDir;   // the session's city (RaceDir); city = −1 when not installed here
        int timeLimit = 0;
        double pedDensity = 0.25;
        int subMode = 0, goldWeight = 0, limitType = 0, limitChoice = 0;
    };
    NetSettings currentSettings(const JValue& st) const;  // the host's own choice, else the session's
    bool haveSettings(const JValue& st) const;
    bool copsTeamsOnly(const JValue& st) const;
    std::string cityRaceDir(int index) const;
    const JValue& netRaces(int city, int mode);
    void mpDefaults();
    void mpInitSettings();
    void mpSendSettings();
    int mpTeam();
    void mpSendRecord();
    void mpPrepare();
    void mpRelease();
    std::string lastLine() const;
    void mpHost();
    void mpJoin(std::string code, const std::string& pass);
    void mpLeave();
    void mpChooseCar();
    void line(const std::string& t);
    // the session panel's own part (web/mp.js)
    void netBeginSession();
    int pendingNetEnd_ = 0;
    void sendSettingsNow(bool force);
    std::string raceTitle(const NetSettings& s);

    // ---- viewer (fe_viewer.cpp)
    void viewerData(ui::Ui& ui);
    void viewerCars(ui::Ui& ui);
    void viewerCity(ui::Ui& ui);
    void viewerNet(ui::Ui& ui);
    void viewerMusic(ui::Ui& ui);
    void viewerLog(ui::Ui& ui);
    void viewerSelectCar(int index);
    void viewerLoadCity(int i);
    void afterCityLoaded(int i, const JValue& info, const std::string& secs);
    void setDriving(bool on);
    void startDrive(bool flat);
    void applyTraffic(bool save);
    void applyPeds(bool save);
    void applyCops(bool save);
    void applySound(bool save);
    void applyMusicVolume(bool save);
    void setMusicMode(int m, bool save);
    void refreshRaces();
    void startViewerRace();
    void restoreViewerState();
    void viewerTick();

    // ---- state: the front end (web/ui.js FE)
    bool ready_ = false;
    bool play_ = false;           // the front end or a game is showing (the viewer's panel hidden)
    JValue state_;                // menu::state()
    JValue cars_;                 // menu::cars()
    std::vector<CityInfo> cities_;
    JValue races_;                // menu::cityRaces(sel_.city)
    std::string screen_ = "main";
    std::vector<std::string> history_;
    Sel sel_;
    std::map<std::string, int> carPaint_;
    int lastUnlockedCar_ = -1;
    bool inGame_ = false;
    bool devPlayers_ = false, devIsDesktop_ = false;
    bool pendingFolder_ = false;
    std::string renderedScreen_;
    bool entering_ = false;
    std::string carFor_;
    bool carLookedAt_ = false;
    int crashCity_ = 0;
    std::string recCity_;
    int recType_ = 2;
    int statTable_ = 0, statType_ = 2, statCity_ = -1;
    std::string optTab_ = "graphics";
    int keysDev_ = -1;
    int bindDev_ = 0, bindAction_ = -1, bindFrames_ = 0, bindHat_ = 0;  // the binding being captured
    float bindRest_[8] = {};                                            // the controller's axes at its start
    std::string bindWarn_;                                              // "also bound to …"
    Spec game_;
    bool resultsOpen_ = false, endMenuOpen_ = false;
    bool netGame_ = false;
    unsigned serial_ = 1;
    bool dim_ = false;
    std::string source_;
    // per-screen caches (refreshed when serial_ changes)
    unsigned cacheSerial_ = 0;
    JValue cacheRecord_, cacheStats_, cacheCrash_, cacheState_, cacheMusic_;
    std::string cacheKey_;
    // dialogs
    Modal modal_ = Modal::None;
    std::string modalTitle_, modalText_, modalOk_;
    std::function<void()> modalOnOk_;
    std::vector<std::string> modalNotes_;
    std::string newName_;
    int newSkill_ = 0;
    std::string newMsg_;
    bool newFocus_ = false;
    // loading
    bool loading_ = false;
    std::string loadTitle_, loadSub_;
    int loadFrames_ = 0;          // frames the loading screen has been drawn (the start waits for one)
    bool pendingStart_ = false;
    Spec pendingSpec_;
    // pause overlay
    enum class Pause { None, Menu, Options, Results, NetPause, NetQuit, NetResults };
    Pause pause_ = Pause::None;
    bool pauseRaceOver_ = false;
    struct ResultsView {
        int mode = 0;
        std::string title, message;
        std::vector<std::vector<std::string>> rows;  // place, name, value
        std::vector<bool> me;
        bool nextRace = false;
    } results_;
    // HUD
    JValue hud_, race_;
    bool hudVisible_ = false;
    // ---- multiplayer (web/ui.js MP, web/mp.js S)
    struct Mp {
        bool owned = false;
        std::string name;
        bool nameSet = false;
        std::string server;
        bool haveSet = false;
        NetSettings set;
        std::string sessionName, password;
        int maxPlayers = 8;
        std::string code, pass;
        bool haveSessions = false, searching = false;
        std::vector<net::SessionInfo> sessions;
        std::string note;
        bool noteBad = false, busy = false;
        int team = -1;
        bool slow = false;
        int car = -1, paint = 0;
        std::string lockKey;
        bool wasOpen = false;
        bool resultsOpen = false;
        std::string resultsSig;
        bool boardOn = true;
        bool rosterShown = false;
        int racesCity = -1;
        JValue racesList;
        // lobby lines
        std::set<double> seen;
        bool haveSeen = false;
        double seenSelf = 0;
        bool seenHost = false, seenOpen = false;
        // the panel (web/mp.js)
        JValue state;             // the session's state (net event)
        JValue game;              // the running game (netgame event)
        std::deque<std::string> lines;
        bool wantDefaults = false;
        bool pendingSettings = false;
        std::string lastSent;
        std::string autoJoin;
        double hostWait = 0;      // the host request's time (busy until Opened or an error)
        bool chatOpen = false;
        std::string chatText;
        std::string say;
        bool viewerOwned = false; // the viewer's Network tab opened the session
        std::string viewerName;
        bool advOpen = false;
    } mp_;
    bool autoJoinChecked_ = false;
    int chatFocusFrames_ = 0;
    size_t chatSeen_ = 0;
    bool lastNameFocus_ = false, pedsDragging_ = false, lastSnameFocus_ = false, lastSpassFocus_ = false;
    float netStateTimer_ = 0;
    // ---- viewer (web/app.js)
    struct Viewer {
        JValue summary;           // the last mount
        std::string status = "Starting the engine…";
        float progress = -1;
        std::string overlayTitle = "Starting…", overlayText;
        bool overlay = true;
        std::string tab = "cars";
        JValue carList;
        int selectedCar = -1;
        std::string selectedName;
        JValue carInfo;
        int paint = 0, lod = 0;
        std::string filter;
        bool autorotate = true, shadow = true, reflections = true, lights = false, trailer = true;
        bool cityLoaded = false;
        int loadedCity = -1;
        std::string cityStatus;
        int tod = 1, weather = 0, walls = 0, detail = 3, far = 0;
        bool props = true, carRefl = true, fog = true, sky = true, aiOverlay = false;
        int driveStart = 0;
        bool automatic = true, autoReverse = true, mirror = false;
        int traffic = 50;
        bool trafficOff = false, cops = true;
        int peds = 25;
        bool pedsOff = false;
        bool sndEffects = true, sndCommentary = true;
        int sndVolume = 100, sndBalance = 0;
        JValue raceList;
        int raceMode = 0, raceNumber = 0;
        bool racePro = false;
        int raceOpp = 0;
        bool racing = false;
        JValue raceState;
        int musicMode = 2, musicVolume = 100;
        JValue musicStatus, soundState, stats;
        double tick = 0;
        bool archivesOpen = false, gmOpen = false;
        bool logAll = false;
        struct LogLine {
            int level;
            std::string msg;
            int count;
        };
        std::vector<LogLine> log;
        int problems = 0;
        std::string readout;
        bool driving = false;
        int view = 0;
        bool sceneFocus = false;
        bool sceneDrag = false;
        bool advOpen = false;
    } v_;
    bool sceneFocus_ = false, sceneDrag_ = false;
    std::vector<CityInfo> viewerCitiesAll_;      // the viewer's list with the test layouts
    bool logScroll_ = false;
    int resetCameraIn_ = 0;
    std::vector<std::function<void()>> sideCards_;  // the front end's right-hand column
    std::string lockCard_;
    bool carLoadFailed_ = false;
    int pendingCityLoad_ = -1, pendingCityFrames_ = 0;
    bool viewerRestored_ = false;
    size_t viewerChatSeen_ = 0;
    bool dropHover_ = false;
    ui::Id openSelect_ = 0;
    double selectSince_ = 0;
    bool vselect(ui::Ui& ui, const std::string& key, const std::vector<std::string>& options, int& current);
};

}  // namespace fe
}  // namespace cc
