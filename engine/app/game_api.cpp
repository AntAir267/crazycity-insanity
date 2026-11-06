#include "app/game_api.h"

#include "app/app.h"
#include "app/menu_data.h"
#include "core/json.h"
#include "core/strutil.h"
#include "game/race.h"
#include "game/race_catalogue.h"
#include "net/net_game.h"
#include "net/session.h"
#include "net/settings.h"

namespace cc {
namespace gameapi {

using namespace cc::game;

namespace {

// The environment the player chose in the race menu where the race allows changes (menus.md,
// "Locked settings"); applied after the race set its own values (a result is then not recorded).
struct EnvChoice {
    bool own = true;
    int timeOfDay = -1, weather = -1;
    float traffic = -1.0f, cops = -1.0f;
    int laps = -1;        // Circuit: 1 – 8
    float peds = -1.0f;   // the pedestrian density (never matters for the records)
};
EnvChoice g_env;

struct CityPick {
    std::string raceDir, mapName;
};
bool cityAt(int index, CityPick& out) {
    const auto& list = theApp().cityView().cities();
    if (index < 0 || size_t(index) >= list.size()) return false;
    out.raceDir = list[size_t(index)].raceDir;
    out.mapName = list[size_t(index)].mapName;
    return true;
}

// The driver's controls for driving (controls.md): the control device, the binding table of the
// five devices, the dead zone, U and K.
void fillControls(DriveView::GameSetup& g, const ConfigFile& c) {
    g.haveControls = true;
    ControlBindings& b = g.controls;
    b.device = c.controlDevice();
    for (int k = 0; k < 5 * ActionCount; ++k) b.table[k] = ControlBinding{c.bindings[k].kind, c.bindings[k].device, c.bindings[k].component};
    b.deadZone = c.deadZone();
    b.steerU = c.steeringFactor() > 0.0f ? c.steeringFactor() : 1.0f;
    b.keyTarget = c.keyboardTarget() > 0.0f ? c.keyboardTarget() : 1.0f;
    g.ffOn = c.forceFeedback();
    g.ffCollision = c.ffCollision();
    g.ffRoad = c.ffRoad();
}

std::string beginGame(int cityIndex, int mode, int race, int carIndex, int paint, int opponents, bool nextRace = false) {
    App& app = theApp();
    CityPick c;
    if (!cityAt(cityIndex, c)) return "{\"ok\":false,\"why\":\"no such city\"}";
    if (carIndex < 0 || size_t(carIndex) >= app.cars().size()) return "{\"ok\":false,\"why\":\"no such car\"}";
    if (!app.cityView().city() || !iequals(app.cityView().city()->name(), c.mapName))
        return "{\"ok\":false,\"why\":\"load the city first\"}";
    app.selectCar(carIndex);
    app.setPaint(paint);
    const std::string carName = app.cars()[size_t(carIndex)].baseName;
    ProfileStore& st = menu::store();
    st.writeForPlay(carName, paint, c.raceDir, mode, race);  // leaving the menus to play
    st.gameStart(c.raceDir, mode, race);
    menu::lastCity() = c.raceDir;
    DriveView::GameSetup g;
    g.game = true;
    g.mode = mode;
    g.singlePlayer = true;
    g.haveView = st.current() >= 0;
    for (int i = 0; i < 12; ++i) g.view[i] = st.options().view[i];
    g.viewDistance = st.options().viewDistance();  // the far plane in a game (cameras.md)
    if (st.current() >= 0) fillControls(g, st.options());
    app.driveView().setGame(g);
    app.driveView().setTextSource([](int id) { return menu::text(id); });
    app.driveView().setOption("automatic", st.options().automatic() ? 1 : 0);
    app.driveView().setOption("autoreverse", st.options().autoReverse() ? 1 : 0);
    std::string r;
    if (mode == ModeCruise) {
        app.driveView().setRaceRecords(nullptr);
        r = app.startDrive(0, false);  // Cruise: a random free-roam start
    } else {
        // A race session (game/race.h) with the driver's records.
        menu::Records& recs = menu::records();
        recs.ownSettings = g_env.own;
        recs.haveLast = false;
        app.driveView().setRaceRecords(&recs);
        // The player's choices go into the race's own setup (races.md "The session's settings"):
        // time of day and weather (the city options and the rain grip follow), Circuit laps, cops
        // and traffic only with changed settings (no record then); the pedestrian density whenever
        // the player set one (it does not matter for the records).
        DriveView::RaceChoices rc;
        rc.pedDensity = g_env.peds;
        if (!g_env.own) {
            rc.timeOfDay = g_env.timeOfDay;
            rc.weather = g_env.weather;
            rc.laps = g_env.laps;
            rc.copDensity = g_env.cops;
            rc.trafficDensity = g_env.traffic;
        }
        app.driveView().setRaceChoices(rc);
        r = app.startRace(mode, race, st.save().skill != 0, opponents, nextRace);
    }
    if (!app.driveView().active()) {
        st.gameEnd(st.options().view);
        app.driveView().setGame(DriveView::GameSetup());
        app.driveView().setRaceRecords(nullptr);
    }
    return r;
}

net::Session* session() { return theApp().netSession(); }

}  // namespace

void afterStart() {
    // The network car choice follows the current driver's locks (multiplayer.md 1.3 step 1).
    theApp().setCarLockQuery([](const std::string& car, int paint) {
        ProfileStore& s = menu::store();
        if (s.current() < 0) return 0;
        const Locks l = s.locks();
        return l.carLocked(car) ? 1 : l.paintLocked(car, paint) ? 2 : 0;
    });
}

void setEnvironment(int own, int timeOfDay, int weather, double traffic, double cops, int laps, double peds) {
    g_env.own = own != 0;
    g_env.timeOfDay = timeOfDay;
    g_env.weather = weather;
    g_env.traffic = float(traffic);
    g_env.cops = float(cops);
    g_env.laps = laps;
    g_env.peds = float(peds);
}

std::string begin(int cityIndex, int mode, int race, int carIndex, int paint, int opponents) {
    return beginGame(cityIndex, mode, race, carIndex, paint, opponents);
}

std::string nextRace(int cityIndex, int carIndex, int paint) {
    App& app = theApp();
    const DriveView::NextRace n = app.driveView().nextRace();
    if (!n.offered) return "{\"ok\":false,\"why\":\"no next race\"}";
    int car = carIndex, pt = paint;
    if (!n.car.empty())
        for (size_t i = 0; i < app.cars().size(); ++i)
            if (app.cars()[i].baseName == n.car && app.cars()[i].group == CarEntry::Menu) {
                car = int(i);
                pt = 0;
            }
    uint8_t v[12];
    app.driveView().viewBytes(v);
    menu::store().gameEnd(v);  // the .cfg with the current view bytes; the .sav follows with the new race
    g_env = EnvChoice();
    return beginGame(cityIndex, n.mode, n.race, car, pt, n.opponents, true);
}

void setViewDistance(double m) { theApp().driveView().setViewDistance(float(m)); }
void pause(bool on) { theApp().driveView().setPaused(on, on); }
void restart() { theApp().driveView().restart(theApp().context()); }

std::string end() {
    App& app = theApp();
    uint8_t v[12];
    app.driveView().viewBytes(v);
    menu::store().gameEnd(v);
    app.stopDrive();
    app.driveView().setGame(DriveView::GameSetup());
    app.driveView().setRaceRecords(nullptr);  // the viewer's race card records nothing
    return menu::state();
}

void netPrepare(bool on) {
    App& app = theApp();
    if (!on) {
        app.setNetGameSetup(nullptr);
        return;
    }
    ProfileStore& st = menu::store();
    DriveView::GameSetup g;
    g.game = true;
    g.singlePlayer = false;
    g.haveView = st.current() >= 0;
    for (int i = 0; i < 12; ++i) g.view[i] = st.options().view[i];
    g.viewDistance = st.options().viewDistance();
    if (st.current() >= 0) fillControls(g, st.options());
    app.setNetGameSetup(&g);
    app.driveView().setTextSource([](int id) { return menu::text(id); });
    app.driveView().setOption("automatic", st.options().automatic() ? 1 : 0);
    app.driveView().setOption("autoreverse", st.options().autoReverse() ? 1 : 0);
}

void netGameStart(const std::string& netName, const std::string& car, int paint, const std::string& raceDir, int mode, int race) {
    ProfileStore& st = menu::store();
    st.setNetName(netName);
    st.writeForPlay(car, paint, raceDir, mode, race);
    st.gameStart(raceDir, mode, race);
    menu::lastCity() = raceDir;
}

std::string netGameEnd() {
    menu::store().gameEnd(theApp().netLastView());
    return menu::state();
}

int debugRaceJump() { return theApp().driveView().debugJumpToTarget() ? 1 : 0; }

void hudDemo(int what, double a, double b, double c) {
    game::Hud& h = theApp().driveView().hud();
    switch (what) {
        case 0: h.reset(int(a)); break;
        case 1:
            h.countdown.reset(float(a), false);
            h.countdown.running = b != 0.0;
            break;
        case 2: h.startClocks(); break;
        case 3: h.postPrimary(int(a), menu::text(int(a)), float(b), c != 0.0); break;
        case 4: h.postSecondary(-1, game::formatTime(float(a))); break;
        case 5: h.setPlace(int(a), int(b)); break;
        case 6: h.setCheckpoints(int(a), int(b)); break;
        case 7: h.setLaps(int(a), int(b)); break;
        case 8: h.setLapTime(int(a), float(b)); break;
        case 9: h.setArrowTarget(true, vec3(float(a), float(b), float(c))); break;
        case 10: {
            game::HudMapDot d;
            d.pos = vec3(float(a), float(b), float(c));
            h.mapDots.push_back(d);
            break;
        }
        default: break;
    }
}

// ---- the network session

void netBegin(const std::string& name, const std::string& car, int paint) { theApp().netBegin(name, car, paint); }
void netEnd() { theApp().netEnd(); }

void netRecord(const std::string& name, const std::string& car, int paint, int team, bool slowLink) {
    net::Session* s = session();
    if (!s) return;
    net::PlayerRecord& r = s->localRecord();
    if (!name.empty()) r.name = name;
    if (!car.empty()) r.car = car;
    r.paint = paint < 0 ? 0 : paint;
    // team < 0: the default by the car (the free-for-all rule of 5.3: a police car team 0).
    r.team = team >= 0 ? (team & 1) : theApp().netDefaultTeam(r.car);
    r.slowLink = slowLink;
    s->applyLocks();  // the Cops and Robbers team car where it is forced, then published
}

void netReady(bool on) {
    if (net::Session* s = session()) s->setReady(on);
}

void netSettings(int mode, const std::string& city, int race, int skill, int timeOfDay, int weather, int laps, double timeLimit,
                 double pedDensity, int subMode, int goldWeight, int limitType, int limitChoice) {
    net::Session* s = session();
    if (!s) return;
    net::Settings st;
    st.mode = mode;
    st.city = city.empty() ? std::string("sf") : city;
    st.race = race;
    st.skill = skill;
    st.timeOfDay = timeOfDay;
    st.weather = weather;
    st.laps = laps;
    st.timeLimit = float(timeLimit);
    st.pedDensity = float(pedDensity);
    st.subMode = subMode;
    st.goldWeight = goldWeight;
    st.limitType = limitType;
    st.limitChoice = limitChoice;
    s->setSettings(st);
}

std::string netDefaults(int mode, const std::string& city, int race, int skill) {
    net::Settings current;
    if (net::Session* s = session())
        if (s->haveSettings()) current = s->settings();
    const net::Settings st = net::hostDefaults(theApp().vfs(), mode, city.empty() ? std::string("sf") : city, race, skill, current);
    JsonWriter j;
    j.beginObject();
    j.field("mode", st.mode);
    j.field("city", st.city);
    j.field("race", st.race);
    j.field("skill", st.skill);
    j.field("timeOfDay", st.timeOfDay);
    j.field("weather", st.weather);
    j.field("laps", st.laps);
    j.field("timeLimit", double(st.timeLimit));
    j.field("pedDensity", double(st.pedDensity));
    j.endObject();
    return j.str();
}

void netDescribe(const std::string& name, const std::string& password, int maxPlayers) {
    net::Session* s = session();
    if (!s) return;
    net::SessionDesc d = s->description();
    d.name = name;
    d.password = password;
    d.maxPlayers = maxPlayers < 1 ? 1 : maxPlayers > 8 ? 8 : maxPlayers;
    d.version = "cci-1";
    s->describe(d);
}

bool netStart() {
    net::Session* s = session();
    return s && s->start();
}

bool netJoinRunning() {
    net::Session* s = session();
    return s && s->joinRunningGame();
}

void netKick(double id) {
    if (net::Session* s = session()) s->kick(net::PlayerId(id));
}

void netLobby() {
    if (net::Session* s = session()) s->returnToLobby();
}

void netQuit() {
    if (net::Session* s = session()) s->quit();
}

void netChat(const std::string& text) {
    net::Session* s = session();
    if (s && !text.empty()) s->chat(text);
}

bool netRestart() {
    net::Session* s = session();
    if (!s || !s->game() || !s->game()->canRestart()) return false;
    s->game()->restart();
    return true;
}

std::string netState() { return theApp().netStateJson(); }

}  // namespace gameapi
}  // namespace cc
