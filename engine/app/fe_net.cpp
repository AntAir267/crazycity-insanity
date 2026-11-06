// The front end's network screens (spec/game/multiplayer.md) and the session control the page's
// web/mp.js did: host or join (by code, invite link or the server's list), the lobby (players,
// the host's settings, team, car, ready, start, chat), and in the game the scoreboard, name tags,
// chat lines, chat box, results, roster, network pause and quit menus. Words by number where the
// spec names them (mmlang.md, "Network screens").
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "app/fe_theme.h"
#include "app/fe_words.h"
#include "app/front_end.h"
#include "app/menu_data.h"
#include "core/json.h"
#include "core/strutil.h"

namespace cc {
namespace fe {

using namespace ui;

namespace {
JValue parse(const std::string& s) { return JValue::parse(s); }
const int kLimitMinutes[4] = {5, 10, 20, 30};
const int kLimitPoints[4] = {100, 250, 500, 1000};
// Words joined with one space (no shipped text starts or ends with one; multiplayer.md, "Words").
std::string joinWords(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return a + " " + b;
}
// Lobby lines of names: "name text" (a space unless one is there already).
std::string joinName(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    if (a.back() == ' ' || b[0] == ' ' || b[0] == ',' || b[0] == '.' || b[0] == ':') return a + b;
    return a + " " + b;
}
}  // namespace

// ------------------------------------------------------------------------------------- session state

bool FrontEnd::mpOpen() const { return mp_.state["session"].truthy() && mp_.state["open"].truthy(); }

const JValue* FrontEnd::mpLocal(const JValue& st) const {
    for (const JValue& p : st["players"].items())
        if (p["local"].truthy()) return &p;
    return nullptr;
}

std::string FrontEnd::mpName() const {
    if (mp_.nameSet) return mp_.name.empty() ? T(437) : mp_.name;  // an emptied name: 437 (mmlang.md)
    const JValue& d = driver();
    if (d["netName"].truthy()) return d["netName"].str();
    if (!mp_.viewerName.empty()) return mp_.viewerName;
    return T(437);
}

// The car this player brings (multiplayer.md 1.3, "Car choice and locks"): one chosen here, at
// first the driver's last car.
const JValue* FrontEnd::mpCar() {
    const JValue* c = carByIndex(mp_.car);
    if (!c) {
        const JValue& d = driver();
        const JValue* byDriver = d.isNull() ? nullptr : carByName(d["car"].str());
        if (byDriver && (*byDriver)["index"].integer(-1) < 0) byDriver = nullptr;
        c = byDriver ? byDriver : carByIndex(sel_.car);
        if (!c) {
            c = carByName("vpbug");
            if (c && (*c)["index"].integer(-1) < 0) c = nullptr;
        }
        mp_.paint = c && !d.isNull() && d["car"].str() == (*c)["name"].str() ? d["paint"].integer()
                    : c && (*c)["index"].integer() == sel_.car                ? sel_.paint
                                                                               : 0;
        mp_.car = c ? (*c)["index"].integer() : -1;
        mpDriverLocks();
    }
    c = carByIndex(mp_.car);
    if (c && mp_.paint >= int((*c)["paints"].size())) mp_.paint = 0;
    return c;
}

// The driver's locks: a locked car becomes vpbug with paint 0, else a locked paint becomes 0.
void FrontEnd::mpDriverLocks() {
    const JValue* c = carByIndex(mp_.car);
    if (!c) return;
    if ((*c)["locked"].truthy()) {
        const JValue* bug = carByName("vpbug");
        if (bug && (*bug)["index"].integer(-1) >= 0) {
            mp_.car = (*bug)["index"].integer();
            mp_.paint = 0;
        }
    } else if ((*c)["paints"][mp_.paint]["locked"].truthy()) {
        mp_.paint = 0;
    }
}

bool FrontEnd::haveSettings(const JValue& st) const {
    if (!st["open"].truthy()) return false;
    return (st["isHost"].truthy() && mp_.haveSet) || st["haveSettings"].truthy();
}

// The session's settings: the host's own choice here (its echo comes later), else the host's.
FrontEnd::NetSettings FrontEnd::currentSettings(const JValue& st) const {
    if (st["isHost"].truthy() && mp_.haveSet) return mp_.set;
    NetSettings s;
    const JValue& v = st["settings"];
    s.mode = v["mode"].integer();
    s.cityDir = v["city"].str();
    const CityInfo* c = cityByRaceDir(s.cityDir);
    s.city = c ? c->index : -1;
    s.race = v["race"].integer();
    s.skill = v["skill"].integer();
    s.timeOfDay = v["timeOfDay"].integer();
    s.weather = v["weather"].integer();
    s.laps = v["laps"].integer(1);
    s.timeLimit = int(std::lround(v["timeLimit"].num()));
    s.pedDensity = v["pedDensity"].num();
    s.subMode = v["subMode"].integer();
    s.goldWeight = v["goldWeight"].integer();
    s.limitType = v["limitType"].integer();
    s.limitChoice = v["limitChoice"].integer();
    return s;
}

bool FrontEnd::copsTeamsOnly(const JValue& st) const {
    if (!haveSettings(st)) return false;
    const NetSettings s = currentSettings(st);
    return s.mode == 2 && s.subMode == 1;
}

// The locks worked out again (1.3): entering multiplayer or the lobby (not in Cops and Robbers):
// the driver's locks; entering the lobby in Cops and Robbers or settings for it: sub-mode 1 the
// team's car with paint 0, sub-modes 0 and 2 the car kept; settings leaving Cops and Robbers:
// the car kept. The record follows.
void FrontEnd::mpLockRules(const JValue& st, bool entering) {
    mpCar();
    const bool have = haveSettings(st);
    const NetSettings s = have ? currentSettings(st) : NetSettings();
    const bool cops = have && s.mode == 2;
    const JValue* me = mpLocal(st);
    const int team = me ? (*me)["team"].integer() : mpTeam();
    const std::string key = std::string(cops ? "1" : "0") + ":" + std::to_string(cops ? s.subMode : -1) + ":" +
                            std::to_string(copsTeamsOnly(st) ? team : -1);
    const bool changed = key != mp_.lockKey;
    mp_.lockKey = key;
    const int wasCar = mp_.car, wasPaint = mp_.paint;
    if (copsTeamsOnly(st)) {
        if (entering || changed) {
            const JValue* c = carByName(team == 0 ? "vpcop" : "vpmustang99");
            if (c && (*c)["index"].integer(-1) >= 0) {
                mp_.car = (*c)["index"].integer();
                mp_.paint = 0;
            }
        }
    } else if (!cops && entering) {
        mpDriverLocks();
    }
    if (mp_.car != wasCar || mp_.paint != wasPaint) mpSendRecord();
}

std::string FrontEnd::cityRaceDir(int index) const {
    const CityInfo* c = cityByIndex(index);
    return c ? c->raceDir : std::string("sf");
}

const JValue& FrontEnd::netRaces(int city, int mode) {
    static const JValue none;
    if (!isRaceMode(mode)) return none;
    if (mp_.racesCity != city) {
        mp_.racesCity = city;
        mp_.racesList = parse(menu::cityRaces(city));
    }
    return mp_.racesList["modes"][std::to_string(mode)];
}

// The host defaults of the chosen mode and race (multiplayer.md 1.2).
void FrontEnd::mpDefaults() {
    NetSettings& s = mp_.set;
    const JValue d = parse(svc_.netDefaults(s.mode, cityRaceDir(s.city), s.race, s.skill));
    if (d.isNull()) return;
    s.timeOfDay = d["timeOfDay"].integer() & 3;
    s.weather = d["weather"].integer() & 3;
    s.laps = std::max(1, d["laps"].integer(1) ? d["laps"].integer(1) : 1);
    s.timeLimit = int(std::lround(d["timeLimit"].num()));
    s.pedDensity = d["pedDensity"].num();
}

void FrontEnd::mpInitSettings() {
    if (mp_.haveSet) return;
    // A session hosted from the viewer's Network tab keeps the settings it has.
    const JValue& st = mp_.state;
    if (st["open"].truthy() && st["isHost"].truthy() && st["haveSettings"].truthy()) {
        mp_.set = currentSettings(st);
        if (mp_.set.city < 0) mp_.set.city = sel_.city;
        mp_.haveSet = true;
        return;
    }
    NetSettings s;
    s.city = sel_.city;
    s.skill = driver().isNull() ? 0 : driver()["skill"].integer();
    mp_.set = s;
    mp_.haveSet = true;
    mpDefaults();
}

void FrontEnd::mpSendSettings() {
    if (mp_.state["isHost"].truthy() && mpOpen()) sendSettingsNow(true);
}

// The record's team: the one chosen in the lobby, else the free-for-all rule (5.3: a police car
// is team 0, any other car team 1).
int FrontEnd::mpTeam() {
    if (mp_.team == 0 || mp_.team == 1) return mp_.team;
    const JValue* c = mpCar();
    return c && (*c)["police"].truthy() ? 0 : 1;
}

void FrontEnd::mpSendRecord() {
    if (!mp_.state["session"].truthy()) return;
    std::string car;
    int paint = 0;
    if (mp_.owned) {
        const JValue* c = mpCar();
        car = c ? (*c)["name"].str() : std::string("vpbug");
        paint = mp_.paint;
    } else {
        car = v_.selectedName.empty() ? std::string("vpbug") : v_.selectedName;
        paint = v_.paint;
    }
    // No team field (outside the Cops and Robbers team games): −1, the engine's default by the car.
    const JValue& st = mp_.state;
    const bool teams = haveSettings(st) && currentSettings(st).mode == 2 && currentSettings(st).subMode > 0;
    const int team = teams ? (mp_.owned ? mpTeam() : (mp_.team >= 0 ? mp_.team : -1)) : -1;
    svc_.netRecord(mp_.owned ? mpName() : mp_.viewerName, car, paint, team, mp_.slow);
}

// The driver's settings for the coming game (as a single-player start applies them).
void FrontEnd::mpPrepare() {
    svc_.netPrepare(true);
    applyGraphics();
    applyAudio();
    svc_.setDriveOption("pedsoff", options()["pedestriansOff"].truthy() ? 1 : 0);
}

void FrontEnd::mpRelease() {
    mp_.owned = false;
    svc_.netPrepare(false);
}

std::string FrontEnd::lastLine() const { return mp_.lines.empty() ? std::string() : mp_.lines.back(); }

void FrontEnd::line(const std::string& t) {
    mp_.lines.push_back(t);
    while (mp_.lines.size() > 40) mp_.lines.pop_front();
}

// A new session object with the local player's name, car and paint (web/mp.js begin()).
void FrontEnd::netBeginSession() {
    std::string car = "vpbug";
    int paint = 0;
    if (mp_.owned) {
        if (const JValue* c = mpCar()) car = (*c)["name"].str();
        paint = mp_.paint;
    } else if (!v_.selectedName.empty()) {
        car = v_.selectedName;
        paint = v_.paint;
    }
    svc_.netBegin(mp_.owned ? mpName() : mp_.viewerName, car, paint);
}

// The host's settings and description into the session (web/mp.js sendSettings).
void FrontEnd::sendSettingsNow(bool force) {
    const JValue& st = mp_.state;
    if (!st["isHost"].truthy()) return;
    const NetSettings& s = mp_.set;
    const std::string name = !mp_.sessionName.empty() ? mp_.sessionName : (mp_.owned ? mpName() : mp_.viewerName) + "'s game";
    char key[512];
    std::snprintf(key, sizeof key, "%d|%s|%d|%d|%d|%d|%d|%d|%.4f|%d|%d|%d|%d|%s|%s|%d", s.mode, cityRaceDir(s.city).c_str(), s.race, s.skill,
                  s.timeOfDay, s.weather, s.laps, s.timeLimit, s.pedDensity, s.subMode, s.goldWeight, s.limitType, s.limitChoice,
                  name.c_str(), mp_.password.c_str(), mp_.maxPlayers);
    if (!force && key == mp_.lastSent) return;
    mp_.lastSent = key;
    svc_.netSettings(s.mode, cityRaceDir(s.city), s.race, s.skill, s.timeOfDay, s.weather, s.laps, s.timeLimit, s.pedDensity, s.subMode,
                     s.goldWeight, s.limitType, s.limitChoice);
    svc_.netDescribe(name, mp_.password, mp_.maxPlayers);
}

void FrontEnd::mpHost() {
    net::NetLink* link = svc_.netLink();
    if (!link || mp_.busy) return;
    if (cities_.empty()) {
        line("Load the game data first.");
        return;
    }
    mpInitSettings();
    mp_.owned = true;
    mp_.busy = true;
    mp_.hostWait = svc_.clock();
    mp_.note.clear();
    mpPrepare();
    netBeginSession();
    net::SessionDesc d;
    d.name = !mp_.sessionName.empty() ? mp_.sessionName : mpName() + "'s game";
    d.password = mp_.password;
    d.maxPlayers = mp_.maxPlayers;
    d.version = "cci-1";
    if (!mp_.server.empty() && !link->hostHere()) link->rememberServer(mp_.server);
    link->host(mp_.server.empty() ? link->defaultServer() : mp_.server, d);
    mp_.wantDefaults = false;
    mp_.pendingSettings = true;  // the settings chosen here are the first ones sent
    rerender();
}

void FrontEnd::mpJoin(std::string code, const std::string& pass) {
    net::NetLink* link = svc_.netLink();
    if (!link || mp_.busy) return;
    code = toUpperUtf8(trim(code));
    bool ok = code.size() == 4;
    for (char c : code) ok = ok && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    if (!ok) {
        mp_.note = "A game code has four letters.";
        mp_.noteBad = true;
        rerender();
        return;
    }
    if (cities_.empty()) {
        line("Load the game data first.");
        return;
    }
    mp_.code = code;
    mp_.owned = true;
    mp_.busy = true;
    mp_.hostWait = svc_.clock();
    mp_.note.clear();
    mpPrepare();
    netBeginSession();
    if (!mp_.server.empty()) link->rememberServer(mp_.server);
    link->join(mp_.server.empty() ? link->defaultServer() : mp_.server, code, pass, "cci-1");
    rerender();
}

void FrontEnd::mpLeave() {
    svc_.netQuit();
    if (net::NetLink* link = svc_.netLink()) link->leave();
    pendingNetEnd_ = 3;  // the session object goes once the quit messages are out
    if (netGame_) netGameOver();
    mp_.resultsOpen = false;
    mp_.note = "You left the game.";
    mp_.noteBad = false;
    rerender();
}

// The car screen for the network game: "Use this car" brings the player back here.
void FrontEnd::mpChooseCar() {
    if (const JValue* c = mpCar()) {
        sel_.car = (*c)["index"].integer();
        sel_.paint = mp_.paint;
        carPaint_[(*c)["name"].str()] = mp_.paint;
    }
    carLookedAt_ = true;
    go("car");
    carFor_ = "mp";
}

// ------------------------------------------------------------------------------------- events

// Lobby lines of the session's changes (mmlang.md): "name" 68 when a player joins, 70 when this
// machine becomes the host, 72 when the connection is lost (not a leave, kick or host quit).
void FrontEnd::mpLobbyLines(const JValue& st) {
    const bool open = st["session"].truthy() && st["open"].truthy();
    if (open) {
        std::set<double> ids;
        for (const JValue& p : st["players"].items()) ids.insert(p["id"].num());
        if (mp_.haveSeen && mp_.seenSelf == st["self"].num()) {
            if (!st["inGame"].truthy())
                for (const JValue& p : st["players"].items())
                    if (!mp_.seen.count(p["id"].num()) && !p["local"].truthy()) line(joinName(p["name"].str(), T(68)));
            if (st["isHost"].truthy() && !mp_.seenHost) line(T(70));
        }
        mp_.seen = ids;
        mp_.haveSeen = true;
        mp_.seenSelf = st["self"].num();
        mp_.seenHost = st["isHost"].truthy();
        mp_.seenOpen = true;
    } else {
        const std::string reason = st["reason"].str();
        const bool quiet = startsWith(reason, "left") || startsWith(reason, "host quit") || startsWith(reason, "kicked") ||
                           startsWith(reason, "city not installed") || startsWith(reason, "refused");
        if (mp_.seenOpen && st["closed"].truthy() && !quiet) line(T(72));
        mp_.seen.clear();
        mp_.haveSeen = false;
        mp_.seenOpen = false;
    }
}

// The session's state (the engine's "net" event).
void FrontEnd::mpOnState() {
    const JValue& st = mp_.state;
    mpLobbyLines(st);
    if (!st["session"].truthy() || st["closed"].truthy()) {
        if (netGame_) netGameOver();
        mp_.resultsOpen = false;
    }
    if (st["open"].truthy()) mp_.busy = false;
    if (st["closed"].truthy() && !st["reason"].str().empty() && mp_.busy) mp_.busy = false;
    if (!st["session"].truthy() || !st["open"].truthy()) return;
    // A car or paint picked since goes into the record (in the lobby).
    const JValue* mine = mpLocal(st);
    if (mine && !st["inGame"].truthy()) {
        std::string car = "vpbug";
        int paint = 0;
        if (mp_.owned) {
            if (const JValue* c = carByIndex(mp_.car)) car = (*c)["name"].str();
            paint = mp_.paint;
        } else if (!v_.selectedName.empty()) {
            car = v_.selectedName;
            paint = v_.paint;
        }
        if ((*mine)["car"].str() != car || (*mine)["paint"].integer() != paint) mpSendRecord();
    }
    if (st["isHost"].truthy()) {
        if (mp_.pendingSettings) {
            mp_.pendingSettings = false;
            mpInitSettings();
            sendSettingsNow(true);
        } else if (mp_.wantDefaults && !cities_.empty()) {
            mp_.wantDefaults = false;
            mpInitSettings();
            mpDefaults();
            sendSettingsNow(true);
        }
    }
}

// Out of a game the session's notices are lobby lines (mmlang.md): 69 for a player who left, 73
// back to the lobby, and after 84 (the city is not installed) 86 (disconnected).
void FrontEnd::netNotices(const JValue& list) {
    auto textOf = [](int n) {
        const std::string s = n ? T(n) : std::string();
        return !s.empty() ? s : (n ? "[" + std::to_string(n) + "]" : std::string());
    };
    for (const JValue& n0 : list.items()) {
        std::vector<JValue> items = {n0};
        int text = n0["text"].integer();
        for (const JValue& n : items) {
            int t = text == 38 ? 69 : text == 45 ? 73 : text;
            const std::string name = n["name"].str(), ln = n["line"].str();
            if (n["kind"].str() == "chat" && !t) line((name.empty() ? std::string() : name + ": ") + ln);
            else if (n["twoLines"].truthy()) line(name + " — " + joinWords(textOf(t), ln));
            else line(joinWords(joinWords(name, textOf(t)), ln));
        }
        if (text == 84) line(T(86));
    }
}

void FrontEnd::netFrame() {
    if (pendingNetEnd_ > 0 && --pendingNetEnd_ == 0) {
        svc_.netEnd();
        // No session sends no more state events: show the ended state.
        mp_.state = parse(svc_.netState());
        mpOnState();
    }
    net::NetLink* link = svc_.netLink();
    if (!link) return;
    link->service();
    std::vector<net::SessionInfo> list;
    if (link->takeSessions(list)) {
        mp_.sessions = list;
        mp_.haveSessions = true;
        mp_.searching = false;
    }
    std::string err;
    while (link->takeError(err)) {
        line(err);
        if (mp_.busy) {
            mp_.busy = false;
            mp_.note = lastLine().empty() ? std::string("The game could not be hosted.") : lastLine();
            mp_.noteBad = true;
        }
    }
    if (mp_.busy && svc_.clock() - mp_.hostWait > 8.0) mp_.busy = false;
    // An invite link (?join=CODE) joins once the game data is in.
    if (mp_.autoJoin.empty() && !autoJoinChecked_) {
        autoJoinChecked_ = true;
        mp_.autoJoin = toUpperUtf8(host_.param("join"));
    }
    if (!mp_.autoJoin.empty() && ready_ && !cities_.empty()) {
        const std::string code = mp_.autoJoin;
        mp_.autoJoin.clear();
        if (host_.hasParam("play")) {
            if (!play_ && !inGame_) openFrontEnd();
            mp_.code = code;
            go("mp");
            mpJoin(code, "");
        } else {
            v_.tab = "net";
            mp_.code = code;
            netBeginSession();
            link->join(mp_.server.empty() ? link->defaultServer() : mp_.server, code, "", "cci-1");
        }
    }
}

// ------------------------------------------------------------------------------------- the game

// The game has started (the host's start, or this player joined a running game).
void FrontEnd::netGameBegin() {
    mp_.owned = true;
    renderedScreen_.clear();  // back from the game enters the lobby screen again
    netGame_ = true;
    inGame_ = true;
    resultsOpen_ = endMenuOpen_ = false;
    mp_.resultsOpen = false;
    mp_.resultsSig.clear();
    pause_ = Pause::None;
    loading_ = false;
    closeModal();
    dim_ = false;
    const JValue& st = mp_.state;
    const NetSettings s = currentSettings(st);
    const JValue* c = mpCar();
    if (const CityInfo* city = cityByRaceDir(st["settings"]["city"].str())) v_.loadedCity = city->index;  // the game loaded its own city
    svc_.netGameStart(mpName(), c ? (*c)["name"].str() : std::string(), mp_.paint, st["settings"]["city"].str(), s.mode, s.race);
    state_ = parse(menu::state());
    sceneFocus_ = true;
}

// Back to the lobby, or the session left: the view bytes into the driver's .cfg, the lobby again.
void FrontEnd::netGameOver() {
    if (!netGame_) return;
    netGame_ = false;
    inGame_ = false;
    if (v_.driving) setDriving(false);
    mp_.resultsOpen = false;
    pause_ = Pause::None;
    closeNetChat();
    const JValue s = parse(svc_.netGameEnd());
    refreshState(&s);
    hud_ = JValue();
    refreshCars();
    if (play_) {
        showCar();
        screen_ = "mp";
        history_.assign(1, "main");
        rerender();
    }
}

void FrontEnd::openNetChat() {
    mp_.chatOpen = true;
    mp_.chatText.clear();
    if (ui_) ui_->setFocus(ui_->id("hudchatbox"));
}

void FrontEnd::closeNetChat() {
    mp_.chatOpen = false;
    mp_.chatText.clear();
    if (ui_ && ui_->focus() == ui_->id("hudchatbox")) ui_->clearFocus();
}

// The scoreboard, chat lines, name tags and chat box in the HUD's look.
void FrontEnd::buildNetHud(Ui& ui) {
    const JValue& d = mp_.game;
    if (!d["active"].truthy()) return;
    const int sub = d["subMode"].integer();
    const int mode = d["mode"].integer();
    std::vector<const JValue*> players;
    for (const JValue& p : d["players"].items()) players.push_back(&p);
    if (mode != 0)
        std::stable_sort(players.begin(), players.end(), [](const JValue* a, const JValue* b) {
            const double sa = (*a)["score"].num(), sb = (*b)["score"].num();
            if (sa != sb) return sa > sb;
            return (*a)["local"].truthy() && !(*b)["local"].truthy();
        });
    const JValue& st = mp_.state;
    double hostId = -1;
    for (const JValue& p : st["players"].items())
        if (p["host"].truthy()) hostId = p["id"].num();
    // The roster (mmlang.md: 500 its title, 501 the host's mark).
    if (mp_.boardOn && !mp_.resultsOpen) {
        Style b = Style();
        b.absolute = true;
        b.left = 16;
        b.top = 16;
        b.minW = 190;
        b.maxW = 260;
        b.pad = Edges(10, 12);
        b.round(14);
        b.bg = col::hudGlass;
        b.outline(1, rgba(255, 255, 255, 0.08f));
        b.column();
        ui.open("board", b);
        Style t;
        t.row();
        t.gap = 8;
        t.justify = Justify::SpaceBetween;
        ui.open(t);
        Style tt = tBody(10.5f, 800, col::accent);
        tt.text.upper = true;
        tt.text.letterSpacing = 1.47f;
        ui.text(label(500), tt);
        Style ts = tt;
        ts.color = rgba(255, 255, 255, 0.55f);
        ui.text(netModeName(mode) + (mode == 2 ? " · " + subName(sub) : std::string()), ts);
        ui.close();
        auto lineS = [&](const std::string& text, Color c) {
            Style l = tBody(13, 650, c);
            l.mar.t = 5;
            ui.text(text, l);
        };
        if (isRaceMode(mode) && d["state"].integer() == 0 && d["waiting"].integer() > 0) lineS(T(30 + std::min(7, d["waiting"].integer())), col::hudInk);
        if (!d["window"].isNull()) lineS("Finish window " + std::to_string(int(std::ceil(d["window"].num()))) + " s", col::warn);
        if (mode == 2) {
            if (sub > 0) {
                Style tm;
                tm.row();
                tm.gap = 6;
                tm.mar.t = 7;
                ui.open(tm);
                for (int team = 0; team < 2; ++team) {
                    Style s;
                    s.row();
                    s.grow = 1;
                    s.pad = Edges(4, 8);
                    s.round(9);
                    s.gap = 6;
                    s.justify = Justify::SpaceBetween;
                    s.align = Align::Center;
                    s.bg = team == 0 ? rgba(59, 130, 246, 0.28f) : rgba(239, 68, 68, 0.28f);
                    ui.open(s);
                    Style n = tBody(11, 800, col::hudInk);
                    n.text.upper = true;
                    n.text.letterSpacing = 0.66f;
                    ui.text(teamName(sub, team), n);
                    ui.text(std::to_string(d[team == 0 ? "team0" : "team1"].integer()), tBody(15, 800, col::hudInk));
                    ui.close();
                }
                ui.close();
            }
            const JValue* carrier = nullptr;
            for (const JValue* p : players)
                if ((*p)["id"].num() == d["carrier"].num() && d["carrier"].truthy()) carrier = p;
            lineS(d["carrying"].truthy() ? std::string("You have the gold: to your base!")
                  : carrier             ? (*carrier)["name"].str() + " has the gold"
                                        : "Gold " + std::to_string(int(std::lround(d["goldDist"].num()))) + " m away",
                  hex(0xfde68a));
        }
        Style tab;
        tab.grid({Track::fr(1), Track::autoW()});
        tab.mar.t = 6;
        ui.open(tab);
        for (const JValue* p : players) {
            const bool me = (*p)["local"].truthy();
            Style cell;
            cell.row();
            cell.align = Align::Center;
            cell.pad = Edges(3, 2);
            cell.border = Edges(1, 0, 0, 0);
            cell.borderColor = rgba(255, 255, 255, 0.06f);
            cell.minW = 0;
            ui.open(cell);
            if (mode == 2 && sub > 0) {
                Style dot;
                dot.size(8, 8);
                dot.round(4);
                dot.mar.r = 6;
                dot.bg = ((*p)["team"].integer() & 1) ? hex(0xef4444) : hex(0x3b82f6);
                ui.leaf(dot);
            }
            Style n = tBody(13, me ? 750 : 400, me ? col::accent : col::hudInk);
            n.nowrap = true;
            n.ellipsis = true;
            n.shrink = 1;
            n.minW = 0;
            n.maxW = 180;
            ui.text((*p)["name"].str(), n);
            if (hostId == (*p)["id"].num()) {
                Style hm = tBody(9.5f, 800, col::warn);
                hm.text.upper = true;
                hm.text.letterSpacing = 0.76f;
                hm.mar.l = 6;
                hm.pad = Edges(0, 5);
                hm.round(5);
                hm.bg = rgba(251, 191, 36, 0.16f);
                ui.text(label(501), hm);
            }
            ui.close();
            Style num = tBody(13, 800, me ? col::accent : col::hudInk);
            num.textAlign = TextAlign::Right;
            num.pad = Edges(3, 2);
            num.border = Edges(1, 0, 0, 0);
            num.borderColor = rgba(255, 255, 255, 0.06f);
            ui.text(mode == 0 ? std::string() : std::to_string(int((*p)["score"].num())), num);
        }
        ui.close();
        if (mode != 0) {
            Style k = tBody(10, 400, rgba(255, 255, 255, 0.45f));
            k.text.upper = true;
            k.text.letterSpacing = 0.8f;
            k.mar.t = 5;
            ui.text(std::string(mode == 2 ? "points" : "checkpoints") + " · F6 hides", k);
        }
        ui.close();
    }
    // Chat lines (up to five, 15 s each: hud.md).
    if (d["chat"].size()) {
        Style c;
        c.absolute = true;
        c.left = 18;
        c.bottom = 124;
        c.maxW = std::min(ui.viewW() * 0.46f, 520.0f);
        c.column();
        c.gap = 4;
        c.align = Align::Start;
        ui.open("chatlines", c);
        for (const JValue& l : d["chat"].items()) {
            Style s = tBody(14, 600, col::hudInk);
            s.pad = Edges(4, 10);
            s.round(10);
            s.bg = col::hudGlass;
            s.textShadow = Shadow{0, 1, 4, 0, rgba(0, 0, 0, 0.5f)};
            ui.text(l.str(), s);
        }
        ui.close();
    }
    // Name tags over the other players' cars (within 300 m), in the 3D view.
    const JValue& hv = hud_["view"];
    const float vw = hv.isNull() ? 1 : hv["w"].f(1), vh = hv.isNull() ? 1 : hv["h"].f(1);
    (void)vw;
    (void)vh;
    for (const JValue& t : d["tags"].items()) {
        Style s = tBody(12.5f, 750, col::hudInk);
        s.absolute = true;
        s.leftPct = t["x"].f() * 100;
        s.topPct = t["y"].f() * 100;
        s.anchorX = 0.5f;
        s.anchorY = 1.0f;
        s.pad = Edges(2, 9);
        s.round(999);
        s.nowrap = true;
        s.bg = rgba(8, 12, 18, 0.66f);
        s.outline(1, rgba(45, 212, 191, 0.55f));
        s.opacity = std::max(0.35f, 1.0f - t["d"].f() / 400.0f);
        ui.text(t["name"].str(), s);
    }
    // The chat box (446 its title, 398 the hint).
    if (mp_.chatOpen) {
        Style w;
        w.absolute = true;
        w.left = 18;
        w.bottom = 92;
        w.w = std::min(ui.viewW() * 0.46f, 520.0f);
        w.column();
        w.align = Align::Start;
        ui.open("chatwrap", w);
        Style ttl = tBody(10, 800, col::accentInk);
        ttl.text.upper = true;
        ttl.text.letterSpacing = 1.4f;
        ttl.pad = Edges(1, 8);
        ttl.round(6);
        ttl.bg = col::accent;
        ttl.mar.b = 4;
        ui.text(label(446), ttl);
        Style box;
        box.pad = Edges(9, 12);
        box.round(12);
        box.font(14);
        box.bg = rgba(8, 12, 18, 0.88f);
        box.outline(1, col::accent);
        box.self = Align::Stretch;
        InputOpts o;
        o.max = 120;
        o.placeholder = T(398);
        o.style = &box;
        const unsigned f = input(ui, "hudchatbox", mp_.chatText, o);
        ui.close();
        if (f & Ui::EditSubmit) {
            const std::string t = trim(mp_.chatText);
            if (!t.empty()) svc_.netChat(t);
            closeNetChat();
        } else if (f & Ui::EditCancel) {
            closeNetChat();
        } else if (ui.focus() != ui.id("hudchatbox") && mp_.chatText.empty() && chatFocusFrames_ > 2) {
            mp_.chatOpen = false;  // the box closes when it loses the keyboard empty
        }
        if (ui.focus() != ui.id("hudchatbox") && chatFocusFrames_ <= 2) ui.setFocus(ui.id("hudchatbox"));
        ++chatFocusFrames_;
    } else {
        chatFocusFrames_ = 0;
    }
    // Results (network games).
    if (d["resultsShown"].truthy() && !d["results"].isNull()) {
        const std::string sig = d["results"].isNull() ? std::string() : std::to_string(d["results"].size()) + (d["canRestart"].truthy() ? "r" : "") +
                                                                            (st["isHost"].truthy() ? "h" : "");
        if (!mp_.resultsOpen || sig != mp_.resultsSig) {
            mp_.resultsOpen = true;
            mp_.resultsSig = sig;
            mp_.rosterShown = false;
            pause_ = Pause::NetResults;
            ui.focusFirst();
        }
    } else if (mp_.resultsOpen) {
        mp_.resultsOpen = false;
        if (pause_ == Pause::NetResults) pause_ = Pause::None;
    }
}

void FrontEnd::buildNetResults(Ui& ui) {
    const JValue& d = mp_.game;
    const JValue& st = mp_.state;
    const bool host = st["isHost"].truthy();
    const int mode = d["mode"].integer();
    sec(ui, netModeName(mode) + (mode == 2 ? " · " + subName(d["subMode"].integer()) : std::string()), 0);
    ui.text("Results", tH());
    if (haveSettings(st)) sub(ui, raceTitle(currentSettings(st)));
    std::vector<TableCol> cols = {{"", Track::autoW(), false}, {"", Track::fr(1), false}, {"", Track::autoW(), true}};
    beginTable(ui, cols, 12, 12);
    for (const JValue& r : d["results"].items()) {
        if (r.has("team")) {
            std::vector<Color> dots = {(r["team"].integer() & 1) ? hex(0xef4444) : hex(0x3b82f6), Color(), Color()};
            const std::string name = r["name"].truthy() ? r["name"].str() : r["fallback"].str();
            tableRow(ui, cols, {"", name, std::to_string(r["points"].integer())}, false, {}, true, &dots);
        } else {
            std::string v;
            if (r.has("time")) v = r["time"].str() == "DNF" ? T(499) : r["time"].str();
            else v = std::to_string(r["points"].integer());
            tableRow(ui, cols, {std::to_string(r["place"].integer()), r["name"].str(), v}, r["name"].str() == mpName());
        }
    }
    endTable(ui);
    if (mp_.rosterShown) {
        // The roster: every player of the session, their car and the host's mark (500, 501).
        sec(ui, label(500));
        std::vector<TableCol> rc = {{"", Track::fr(1), false}, {"", Track::autoW(), false}, {"", Track::autoW(), true}};
        beginTable(ui, rc);
        for (const JValue& p : st["players"].items())
            tableRow(ui, rc, {p["name"].str(), carDesc(p["car"].str()), p["host"].truthy() ? label(501) : std::string()}, p["local"].truthy(),
                     {false, true, false});
        endTable(ui);
    }
    if (!host) note(ui, "The host restarts or takes everyone back to the lobby.");
    beginRow(ui, Justify::End);
    if (button(ui, "leave", "Leave", Btn::Danger)) mpLeave();
    if (button(ui, "roster", T(494), Btn::Ghost)) mp_.rosterShown = !mp_.rosterShown;
    if (host && button(ui, "lobby", T(479), Btn::Ghost)) svc_.netLobby();
    if (host && d["canRestart"].truthy() && button(ui, "restart", "Restart")) {
        svc_.netRestart();
        pause_ = Pause::None;
        mp_.resultsOpen = false;
        sceneFocus_ = true;
    }
    endRow(ui);
}

// The pause menu of a network game: the game keeps running (multiplayer.md 1.5).
void FrontEnd::buildNetPause(Ui& ui) {
    const JValue& st = mp_.state;
    net::NetLink* link = svc_.netLink();
    sec(ui, "Network game · " + (link ? link->code() : std::string()), 0);
    ui.text(haveSettings(st) ? netModeName(currentSettings(st).mode) : std::string("Network game"), tH());
    sub(ui, "The game keeps running while this menu is open.");
    Style m;
    m.column();
    m.gap = 8;
    ui.open(m);
    if (item(ui, "resume", "▶", "Resume", "Esc", true)) resume();
    if (item(ui, "chat", "✎", "Chat", "Y")) {
        resume();
        openNetChat();
    }
    if (item(ui, "board", "≡", mp_.boardOn ? "Hide the scoreboard" : "Show the scoreboard", "F6")) {
        mp_.boardOn = !mp_.boardOn;
        resume();
    }
    if (item(ui, "options", "⚙", "Options", "Audio, controls, graphics")) {
        pause_ = Pause::Options;
        ui.focusFirst();
    }
    if (item(ui, "quit", "⏏", label(478), "")) {
        pause_ = Pause::NetQuit;
        ui.focusFirst();
    }
    ui.close();
}

// The quit menu (mmlang.md, "In-race": 478 title, 479 quit to the lobby, 641 quit the game (481
// in Cops and Robbers), 482 cancel).
void FrontEnd::buildNetQuit(Ui& ui) {
    const JValue& st = mp_.state;
    const bool host = st["isHost"].truthy();
    ui.text(label(478), tH());
    sub(ui, "The game keeps running while this menu is open.");
    Style m;
    m.column();
    m.gap = 8;
    ui.open(m);
    if (host && item(ui, "lobby", "☰", T(479), "Ends this game for all players")) {
        pause_ = Pause::None;
        svc_.netLobby();
    }
    const bool cops = haveSettings(st) && currentSettings(st).mode == 2;
    if (item(ui, "leave", "⏏", T(cops ? 481 : 641), host ? "You are the host: the game ends for everyone" : "Back to Multiplayer")) {
        pause_ = Pause::None;
        mpLeave();
    }
    if (item(ui, "cancel", "‹", T(482), "Esc")) pause_ = Pause::NetPause;
    ui.close();
}

// ------------------------------------------------------------------------------------- screens

// "City · 3. Race" of a session's settings (the city alone for Cruise and Cops and Robbers).
std::string FrontEnd::raceTitle(const NetSettings& s) {
    const CityInfo* c = s.city >= 0 ? cityByIndex(s.city) : nullptr;
    std::string t = c ? c->name : s.cityDir;
    if (c && isRaceMode(s.mode)) {
        const JValue& r = netRaces(c->index, s.mode)[s.race];
        if (!r.isNull()) t += " · " + std::to_string(s.race + 1) + ". " + r["name"].str();
    }
    return t;
}

void FrontEnd::screenMp(Ui& ui) {
    h1(ui, "Multiplayer");
    if (!svc_.netLink()) {
        note(ui, "Network games are not available in this build.");
        return;
    }
    const JValue& st = mp_.state;
    const bool open = st["session"].truthy() && st["open"].truthy();
    const bool entering = entering_ || mp_.wasOpen != open;
    mp_.wasOpen = open;
    mpLockRules(st, entering);
    if (open) {
        if (!mp_.owned) {
            mp_.owned = true;
            mpPrepare();
        }
        renderLobby(ui, st);
    } else {
        renderMpStart(ui, st);
    }
}

// The player panel (mmlang.md: 428 driver, 429 car, 430 colour) with the car button.
void FrontEnd::playerPanel(Ui& ui, bool carFixed) {
    const JValue* c = mpCar();
    std::string paint;
    if (c && !(*c)["paints"][mp_.paint].isNull()) {
        const JValue& p = (*c)["paints"][mp_.paint];
        paint = p["name"].truthy() ? p["name"].str() : "paint " + std::to_string(mp_.paint + 1);
    }
    Style row;
    row.row();
    row.justify = Justify::SpaceBetween;
    row.align = Align::Center;
    row.gap = 8;
    row.mar = Edges(12, 0, 4, 0);
    row.pad = Edges(10, 12);
    row.round(12);
    row.outline(1, col::line);
    row.bg = rgba(255, 255, 255, 0.025f);
    ui.open("playerpanel", row);
    Style g;
    g.grid({Track::autoW(), Track::fr(1)});
    g.gap = 14;
    g.rowGap = 3;
    g.shrink = 1;
    g.minW = 0;
    ui.open(g);
    const std::pair<std::string, std::string> rows[3] = {
        {label(428), mpName()}, {label(429), c ? (*c)["description"].str() : std::string("—")}, {label(430), paint.empty() ? std::string("—") : paint}};
    for (const auto& r : rows) {
        ui.text(r.first, tBody(13, 400, col::muted));
        ui.text(r.second, tBody(13, 700));
    }
    ui.close();
    if (button(ui, "changecar", "Change car", Btn::Ghost, carFixed)) mpChooseCar();
    ui.close();
}

// The chat box of the lobby: the session's lines and an input.
void FrontEnd::chatBox(Ui& ui, const std::string& key) {
    sec(ui, "Chat");
    Style box;
    box.column();
    box.h = 132;
    box.scrollY = true;
    box.pad = Edges(8, 10);
    box.round(10);
    box.outline(1, col::line);
    box.bg = rgba(0, 0, 0, 0.25f);
    box.hit = true;
    Node& lines = ui.open(key + "lines", box);
    (void)lines;
    const size_t from = mp_.lines.size() > 14 ? mp_.lines.size() - 14 : 0;
    if (mp_.lines.empty()) {
        ui.text("Nothing said yet.", tBody(13, 400, col::muted));
    } else {
        for (size_t i = from; i < mp_.lines.size(); ++i) {
            Style l = tBody(13);
            l.text.lineHeight = 13 * 1.45f;
            ui.text(mp_.lines[i], l);
        }
    }
    ui.close();
    // Keep the newest line in view.
    if (mp_.lines.size() != chatSeen_) {
        chatSeen_ = mp_.lines.size();
        ui.scrollOf(ui.id(key + "lines")) = 1e6f;
    }
    Style row;
    row.row();
    row.gap = 8;
    row.mar.t = 6;
    row.align = Align::Center;
    ui.open(row);
    InputOpts o;
    o.max = 120;
    o.placeholder = T(398);
    o.fontSize = 14;
    const unsigned f = input(ui, key + "say", mp_.say, o);
    const bool send = button(ui, key + "send", "Send", Btn::Ghost) || (f & Ui::EditSubmit);
    ui.close();
    if (send) {
        const std::string t = trim(mp_.say);
        if (!t.empty()) svc_.netChat(t);
        mp_.say.clear();
        ui.setFocus(ui.id(key + "say"));
    }
}

void FrontEnd::renderMpStart(Ui& ui, const JValue& st) {
    sub(ui, "Drive the same city with friends: host a game and send the code, or join one. Everyone needs MM2's data in their own " +
                std::string(host_.isBrowser() ? "browser." : "game folder."));
    if (st["closed"].truthy() && mp_.owned && st["reason"].truthy() && mp_.note.empty()) {
        mp_.note = "The game ended: " + st["reason"].str();
        mp_.noteBad = true;
    }
    if (!mp_.note.empty()) msg(ui, mp_.note, mp_.noteBad ? Msg::Bad : Msg::Ok);
    if (mp_.busy) {
        net::NetLink* lk = svc_.netLink();
        const std::string st = lk ? lk->statusLine() : std::string();
        msg(ui, st.empty() ? std::string("Connecting…") : st, Msg::Ok);
    }
    std::string name = mpName();
    field(ui, "Your name in the game", "", [&]() {
        InputOpts o;
        o.max = 18;
        if (input(ui, "mpname", name, o) & Ui::EditChanged) {
            mp_.name = trim(name);
            mp_.nameSet = true;
        }
    });
    const JValue* c = mpCar();
    playerPanel(ui, false);
    // Host.
    sec(ui, "Host a game");
    beginCard(ui, "hostcard");
    field(ui, "Game name", "", [&]() {
        InputOpts o;
        o.max = 30;
        o.placeholder = mpName() + "'s game";
        input(ui, "sname", mp_.sessionName, o);
    });
    field(ui, "Password", "", [&]() {
        InputOpts o;
        o.max = 20;
        o.placeholder = "none";
        input(ui, "spass", mp_.password, o);
    });
    std::vector<Choice> maxes;
    for (int n = 2; n <= 8; ++n) maxes.push_back(Choice{std::to_string(n), n, false});
    field(ui, "Players", "", [&]() { seg(ui, "max", maxes, mp_.maxPlayers); });
    if (net::NetLink* lk = svc_.netLink(); lk && lk->canHostHere()) {
        // Natively: the game's own session server on this computer, or the server below.
        bool here = lk->hostHere();
        if (check(ui, "here", "Host on this computer", here)) lk->setHostHere(here);
        if (here) {
            bool lan = lk->lan();
            if (check(ui, "lan", "Let players on my network join", lan)) lk->setLan(lan);
            note(ui, lan ? "Friends on your network join with the code and this computer's address (shown once you host). Players "
                           "elsewhere need the server's port forwarded to this computer."
                         : "Only players on this computer can join; switch the network on for friends on your network.");
        } else {
            note(ui, "The game is hosted on the session server under Server.");
        }
    }
    beginRow(ui, Justify::End);
    if (button(ui, "host", "Host a game", Btn::Go, mp_.busy || !c)) mpHost();
    endRow(ui);
    endCard(ui);
    // Join.
    sec(ui, "Join a game");
    beginCard(ui, "joincard");
    {
        Style row;
        row.row();
        row.gap = 8;
        row.align = Align::Center;
        ui.open(row);
        InputOpts oc;
        oc.max = 4;
        oc.placeholder = "CODE";
        oc.upper = true;
        oc.code = true;
        oc.width = 7.5f * 15;
        const unsigned fc = input(ui, "code", mp_.code, oc);
        InputOpts op;
        op.max = 20;
        op.placeholder = "password (if any)";
        op.password = true;
        input(ui, "pass", mp_.pass, op);
        const bool join = button(ui, "join", "Join", Btn::Primary, mp_.busy || !c) || (fc & Ui::EditSubmit);
        ui.close();
        if (join) mpJoin(mp_.code, mp_.pass);
    }
    beginRow(ui, Justify::SpaceBetween, 8, 10);
    Style n = tNote();
    n.mar = Edges(0);
    n.shrink = 1;
    ui.text(T(438), n);
    if (button(ui, "find", mp_.haveSessions ? "Refresh" : "Find games", Btn::Ghost)) {
        mp_.haveSessions = false;
        mp_.searching = true;
        if (net::NetLink* link = svc_.netLink()) link->list(mp_.server.empty() ? link->defaultServer() : mp_.server);
    }
    endRow(ui);
    if (mp_.searching && !mp_.haveSessions) note(ui, T(657));
    if (mp_.haveSessions) {
        Style l;
        l.column();
        l.gap = 4;
        l.mar.t = 8;
        l.maxH = ui.viewH() * 0.38f;
        l.scrollY = true;
        ui.open("sessions", l);
        if (mp_.sessions.empty()) note(ui, "No games on this server yet.");
        for (size_t i = 0; i < mp_.sessions.size(); ++i) {
            const net::SessionInfo& s = mp_.sessions[i];
            // The session text the host sends: 87 the city, 88 the event; a session of another network
            // version shows 83, one in a city this machine lacks 84 and 85 (neither can be joined).
            const CityInfo* city = cityByRaceDir(s.city);
            const bool otherVersion = !s.version.empty() && s.version != "cci-1";
            const bool noCity = !s.city.empty() && !city;
            const std::string sub = otherVersion ? T(83)
                                    : noCity     ? T(84) + " · " + T(85)
                                    : !s.city.empty() ? labelled(87, city->name) + " · " + labelled(88, s.mode == 0 ? T(399) : netModeName(s.mode))
                                                      : std::string();
            const bool disabled = mp_.busy || otherVersion || noCity;
            const std::string meta = std::to_string(s.players) + "/" + std::to_string(s.max) + (s.sealed ? " · racing" : "") + (s.password ? " · 🔒" : "");
            if (listItem(ui, "s" + std::to_string(i), s.code, s.name, false, disabled, [&]() {
                    Style m = tBody(12, 400, col::muted);
                    m.nowrap = true;
                    ui.text(meta, m);
                }, true, sub)) {
                mp_.code = s.code;
                if (!s.server.empty()) mp_.server = s.server;  // a LAN game: its own server
                mpJoin(s.code, s.password ? mp_.pass : std::string());
            }
        }
        ui.close();
    }
    endCard(ui);
    // The server (most players never change it).
    {
        const Id id = ui.id("adv");
        Interact it = ui.interact(id);
        Style s = tBody(13, 400, col::muted);
        s.mar.t = 14;
        s.cursor = Cursor::Hand;
        s.hit = true;
        Node& nn = ui.text(std::string(mp_.advOpen ? "▼ " : "▶ ") + label(435), s, id);
        nn.label = "Server";
        if (it.clicked) mp_.advOpen = !mp_.advOpen;
        if (mp_.advOpen) {
            if (mp_.server.empty() && svc_.netLink()) mp_.server = svc_.netLink()->defaultServer();
            field(ui, "Session server", "", [&]() {
                InputOpts o;
                o.placeholder = "ws://host:8821/net";
                input(ui, "server", mp_.server, o);
            });
            note(ui, "Players connect to each other directly where they can; the server only introduces them and relays when it must.");
        }
    }
    // Side: how it works and the driver.
    sideCards_.push_back([this, &ui]() {
        Style card;
        card.column();
        card.bg = rgba(255, 255, 255, 0.03f);
        card.outline(1, col::line);
        card.round(12);
        card.pad = Edges(12, 14);
        card.w = std::min(360.0f, ui.viewW() * 0.34f);
        card.hit = true;
        ui.open("howcard", card);
        sec(ui, "How it works", 0);
        noteRich(ui, {span("1. One player hosts and picks the mode, city and race.\n2. The others join with the four-letter code or the invite "
                           "link.\n3. Everyone presses Ready; the host starts.\nWhile driving, ",
                           12),
                      kbd("Y"), span(" chats and ", 12), kbd("F6"), span(" shows or hides the scoreboard. The game never pauses.", 12)});
        ui.close();
        driverCard(ui);
    });
}

void FrontEnd::renderLobby(Ui& ui, const JValue& st) {
    net::NetLink* link = svc_.netLink();
    const JValue* me = mpLocal(st);
    const bool have = haveSettings(st);
    const NetSettings s = have ? currentSettings(st) : NetSettings();
    const std::string code = link ? link->code() : std::string();
    beginRow(ui, Justify::SpaceBetween, 8, 0, false);
    {
        Style c;
        c.column();
        ui.open(c);
        sec(ui, std::string(st["isHost"].truthy() ? "Hosting" : "In the lobby") + (st["inGame"].truthy() ? " · game running" : ""), 0);
        Style cs = tBody(34, 850, col::accent);
        cs.text.letterSpacing = 0.22f * 34;
        cs.text.lineHeight = 34 * 1.1f;
        cs.mar.t = 2;
        ui.text(code.empty() ? std::string("????") : code, cs);
        ui.close();
    }
    const bool native = link && link->canHostHere();
    if (button(ui, "copy", native ? "Copy the code and address" : "Copy invite link", Btn::Ghost) && link) {
        const std::string u = link->inviteUrl(code, mp_.server.empty() ? link->defaultServer() : mp_.server);
        host_.setClipboard(u);
        toast(native ? "The code and the server's address are copied." : "Invite link copied: send it to your friends.");
    }
    endRow(ui);
    if (native && link->localServerRunning()) {
        // Hosted on this computer: where friends connect (the Server field of their Multiplayer screen).
        std::string where;
        for (const std::string& u : link->localServerUrls())
            if (u.find("127.0.0.1") == std::string::npos) where += (where.empty() ? "" : ", ") + u;
        note(ui, where.empty() ? "Hosted on this computer for players on this computer only (code " + code + ")."
                               : "Hosted on this computer. Friends type " + where + " under Server and join with the code " + code + ".");
    } else {
        note(ui, "Friends join with the code " + code + " in Multiplayer" + std::string(native ? "" : ", or with the invite link") + ".");
    }
    if (link) {
        const std::string st = link->statusLine();
        if (!st.empty()) msg(ui, st, Msg::Warn);
    }
    // Players and the buttons.
    sec(ui, label(431));
    mpLive(ui, st);
    // The host's settings, or what the host chose.
    if (st["isHost"].truthy() && !st["inGame"].truthy()) hostSettings(ui);
    else if (have) settingsSummary(ui, st);
    // Team (cops and robbers, robber teams).
    const int sub = have ? s.subMode : 0;
    if (have && s.mode == 2 && sub > 0) {
        field(ui, "Your team", "", [&]() {
            int v = me ? (*me)["team"].integer() : mpTeam();
            if (seg(ui, "team", {Choice{teamName(sub, 0), 0, false}, Choice{teamName(sub, 1), 1, false}}, v, st["inGame"].truthy())) {
                mp_.team = v;
                mpSendRecord();
            }
        });
        if (sub == 1)
            note(ui, teamName(1, 0) + " drive the police car and " + teamName(1, 1) + " the Mustang, whatever car you chose.");
    }
    // The player panel (sub-mode 1 locks every car but the team's).
    playerPanel(ui, st["inGame"].truthy() || copsTeamsOnly(st));
    if (check(ui, "slow", "Slow connection (fewer car updates)", mp_.slow)) mpSendRecord();
    std::string name = mpName();
    field(ui, "Your name in the game", "", [&]() {
        InputOpts o;
        o.max = 18;
        const unsigned f = input(ui, "mpname", name, o);
        if (f & Ui::EditChanged) {
            mp_.name = trim(name);
            mp_.nameSet = true;
        }
        if ((f & Ui::EditSubmit) || (lastNameFocus_ && ui.focus() != ui.id("mpname"))) mpSendRecord();
        lastNameFocus_ = ui.focus() == ui.id("mpname");
    });
    chatBox(ui, "lobby");
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
}

// The players list, the buttons and the lines.
void FrontEnd::mpLive(Ui& ui, const JValue& st) {
    const bool have = haveSettings(st);
    const NetSettings s = have ? currentSettings(st) : NetSettings();
    const JValue& ss = st["settings"];
    const bool teams = ss["mode"].integer() == 2 && ss["subMode"].integer() > 0;
    Style l;
    l.column();
    l.gap = 4;
    ui.open("players", l);
    for (const JValue& p : st["players"].items()) {
        const bool local = p["local"].truthy();
        const std::string sub = carDesc(p["car"].str()) + (teams ? " · " + teamName(ss["subMode"].integer(), p["team"].integer()) : std::string());
        const double id = p["id"].num();
        listItem(ui, "pl" + std::to_string((long long)id), p["host"].truthy() ? "★" : "", p["name"].str() + (local ? " (you)" : ""), local, false,
                 [&]() {
                     if (p["cheater"].truthy()) badge(ui, "car differs", Badge::Lock);
                     if (!local) {
                         // Natively: a direct UDP path or the server's relay.
                         const std::string how = svc_.netLink() ? svc_.netLink()->linkTo(id) : std::string();
                         if (!how.empty()) {
                             Style hs = tBody(11.5f, 400, col::muted);
                             hs.nowrap = true;
                             ui.text(how, hs).label = how;
                         }
                     }
                     if (p["inGame"].truthy()) badge(ui, "in game", Badge::Accent);
                     else if (p["host"].truthy()) badge(ui, label(501), Badge::Pro);
                     else if (p["ready"].truthy()) badge(ui, "ready", Badge::Ok);
                     else badge(ui, "not ready", Badge::Muted);
                     if (st["isHost"].truthy() && !local && button(ui, "kick" + std::to_string((long long)id), "Kick", Btn::Small)) {
                         const std::string nm = p["name"].str();
                         confirmBox("Remove " + nm + "?", "They leave the game and see that they were removed.", "Remove",
                                    [this, id]() { svc_.netKick(id); });
                     }
                 },
                 false, sub);
    }
    ui.close();
    const JValue* me = mpLocal(st);
    beginRow(ui, Justify::Start, 8, 10);
    if (!st["isHost"].truthy() && !st["inGame"].truthy() && !st["canJoinRunning"].truthy()) {
        const bool ready = me && (*me)["ready"].truthy();
        if (button(ui, "ready", ready ? "Not ready" : "Ready", ready ? Btn::Ghost : Btn::Primary)) svc_.netReady(!ready);
    }
    if (st["isHost"].truthy() && !st["inGame"].truthy()) {
        if (button(ui, "start", have && isRaceMode(s.mode) ? T(433) : std::string("Start the game"), Btn::Go, !st["canStart"].truthy())) {
            mpPrepare();
            mpSendSettings();
            svc_.netStart();
        }
    }
    if (st["canJoinRunning"].truthy() && button(ui, "joinrun", "Join the running game", Btn::Go)) {
        mpPrepare();
        svc_.netJoinRunning();
    }
    if (st["isHost"].truthy() && st["inGame"].truthy() && button(ui, "lobby", T(479), Btn::Ghost)) svc_.netLobby();
    if (button(ui, "leave", "Leave", Btn::Danger)) {
        if (st["isHost"].truthy()) confirmBox("Leave the game?", "You are the host: the game ends for everyone.", "Leave", [this]() { mpLeave(); });
        else mpLeave();
    }
    endRow(ui);
    const std::string why = st["isHost"].truthy() && !st["inGame"].truthy() && !st["canStart"].truthy() ? st["why"].str()
                            : st["canJoinRunning"].truthy()                                        ? std::string("A game is running: join it now.")
                            : !st["isHost"].truthy() && !st["inGame"].truthy()                     ? T(432)
                                                                                                     : std::string();
    if (!why.empty()) note(ui, why);
}

// The session panel (mmlang.md, "Network screens"): 407 + the city (not installed: 406 and 404 +
// 405), the mode (585 – 589), the event (399 Cruise, the race's name, 400 – 402 the Cops and Robbers
// sub-modes), weather 408 / 624 / 409 / 410, time of day 412 – 415, Circuit 418 laps, Cops and
// Robbers 423 gold weight (420 – 422) and 427 limit; the skill, time limit and pedestrians in our
// own words.
void FrontEnd::settingsSummary(Ui& ui, const JValue& st) {
    const NetSettings s = currentSettings(st);
    const CityInfo* c = s.city >= 0 ? cityByIndex(s.city) : nullptr;
    std::vector<std::pair<std::string, bool>> lines;
    if (c) lines.push_back({labelled(407, c->name), false});
    else {
        lines.push_back({T(406), false});
        lines.push_back({T(404) + " " + T(405), false});
    }
    lines.push_back({netModeName(s.mode), true});
    std::string event = T(399);
    if (isRaceMode(s.mode)) {
        const JValue& r = c ? netRaces(c->index, s.mode)[s.race] : JValue();
        event = !r.isNull() ? r["name"].str() : std::to_string(s.race + 1);
    } else if (s.mode == 2) {
        event = T(400 + std::max(0, std::min(2, s.subMode)));
    }
    lines.push_back({event, true});
    static const int weather[4] = {408, 624, 409, 410};
    lines.push_back({T(weather[s.weather & 3]), false});
    lines.push_back({T(412 + (s.timeOfDay & 3)), false});
    if (s.mode == 3) lines.push_back({T(418) + " " + std::to_string(s.laps), false});
    if (s.mode == 2) {
        lines.push_back({T(423) + " " + T(420 + std::max(0, std::min(2, s.goldWeight))), false});
        lines.push_back({T(427) + " " +
                             (s.limitType == 1   ? std::to_string(kLimitMinutes[s.limitChoice & 3]) + " " + T(425)
                              : s.limitType == 2 ? std::to_string(kLimitPoints[s.limitChoice & 3]) + " " + T(424)
                                                 : T(426)),
                         false});
    }
    std::vector<std::string> own;
    if (isRaceMode(s.mode)) {
        own.push_back(skillName(s.skill));
        if (s.timeLimit > 0) own.push_back("time limit " + fmtSecs(s.timeLimit));
    }
    own.push_back("pedestrians " + pct(s.pedDensity));
    beginCard(ui, "summary", 12);
    sec(ui, "The host's choice", 0);
    for (const auto& l : lines) {
        Style t = tBody(13.5f, l.second ? 700 : 400);
        t.text.lineHeight = 13.5f * 1.55f;
        ui.text(l.first, t);
    }
    std::string o;
    for (size_t i = 0; i < own.size(); ++i) o += (i ? " · " : "") + own[i];
    Style n = tNote();
    n.mar.b = 0;
    ui.text(o, n);
    endCard(ui);
}

// The host's settings (multiplayer.md 1.2): changing the mode, city, race or skill loads the
// race's defaults; every change is sent at once.
void FrontEnd::hostSettings(Ui& ui) {
    mpInitSettings();
    NetSettings& s = mp_.set;
    bool send = false, defaults = false;
    sec(ui, "Game");
    std::vector<Choice> modes;
    for (int m : {0, 1, 3, 4, 2}) modes.push_back(Choice{netModeName(m), m, isRaceMode(m) && netRaces(s.city, m).size() == 0});
    if (tabs(ui, "netmode", modes, s.mode)) {
        s.race = 0;
        send = defaults = true;
    }
    std::vector<Choice> cities;
    for (const CityInfo& c : cities_) cities.push_back(Choice{c.name, c.index, false});
    field(ui, "City", "", [&]() {
        if (seg(ui, "netcity", cities, s.city)) {
            s.race = 0;
            if (isRaceMode(s.mode) && netRaces(s.city, s.mode).size() == 0) s.mode = 0;
            send = defaults = true;
        }
    });
    if (isRaceMode(s.mode)) {
        const JValue& races = netRaces(s.city, s.mode);
        if (s.race >= int(races.size())) s.race = 0;
        beginList(ui, "netraces", ui.viewH() * 0.38f);
        for (size_t i = 0; i < races.size(); ++i)
            if (listItem(ui, "nr" + std::to_string(i), std::to_string(i + 1), races[i]["name"].str(), int(i) == s.race, false)) {
                s.race = int(i);
                send = defaults = true;
            }
        endList(ui);
        field(ui, "Skill", "", [&]() {
            if (seg(ui, "netskill", {Choice{skillName(0), 0, false}, Choice{skillName(1), 1, false}}, s.skill)) send = defaults = true;
        });
    } else if (s.mode == 0) {
        note(ui, "Drive the city together: no goal, no end. Players can join while you drive.");
    }
    if (s.mode == 2) {
        note(ui, "Grab the gold and take it to your base; hits knock it loose. Players can join while the game runs.");
        field(ui, "Game", "", [&]() {
            if (seg(ui, "sub", {Choice{subName(0), 0, false}, Choice{subName(1), 1, false}, Choice{subName(2), 2, false}}, s.subMode)) send = true;
        });
        // The host's Cops and Robbers options: 509 the gold mass with 506 – 508, the limits 510 – 513
        // (minutes) and 514 – 517 (points); the sub-mode and limit type are pictures in MM2 (ours).
        field(ui, label(509), "", [&]() {
            if (seg(ui, "gold", {Choice{T(506), 0, false}, Choice{T(507), 1, false}, Choice{T(508), 2, false}}, s.goldWeight)) send = true;
        });
        field(ui, "Limit", "", [&]() {
            if (seg(ui, "limit", choices({"No limit", "Time", "Points"}), s.limitType)) {
                s.limitChoice = 0;
                send = true;
            }
        });
        if (s.limitType) {
            std::vector<Choice> vals;
            for (int i = 0; i < 4; ++i) vals.push_back(Choice{T((s.limitType == 1 ? 510 : 514) + i), i, false});
            field(ui, s.limitType == 1 ? "Minutes" : "Points", "", [&]() {
                if (seg(ui, "limitv", vals, s.limitChoice)) send = true;
            });
        }
    }
    sec(ui, "Conditions");
    std::vector<Choice> tod, weather;
    for (int i = 0; i < 4; ++i) {
        tod.push_back(Choice{T(629 + i), i, false});
        weather.push_back(Choice{T(625 + i), i, false});
    }
    field(ui, "Time of day", "", [&]() {
        if (seg(ui, "nettod", tod, s.timeOfDay)) send = true;
    });
    field(ui, "Weather", "", [&]() {
        if (seg(ui, "netweather", weather, s.weather)) send = true;
    });
    if (s.mode == 3)
        field(ui, label(418), "", [&]() {
            if (stepper(ui, "laps", s.laps, 1, 15, 1, [](int v) { return std::to_string(v); })) send = true;
        });
    if (isRaceMode(s.mode))
        field(ui, s.mode == 4 ? "Time limit" : "Race clock limit", "", [&]() {
            if (stepper(ui, "timelimit", s.timeLimit, 0, 255, 5, [](int v) { return fmtSecs(v); })) send = true;
        });
    field(ui, "Pedestrians", pct(s.pedDensity), [&]() {
        float v = float(s.pedDensity);
        if (slider(ui, "netpeds", 0, 1, v)) s.pedDensity = v;
        // Sent when the slider is let go.
        if (ui.active() != ui.id("netpeds") && pedsDragging_) send = true;
        pedsDragging_ = ui.active() == ui.id("netpeds");
        if (ui.isFocused(ui.id("netpeds")) && (ui.keyPressed(Key::Left) || ui.keyPressed(Key::Right))) send = true;
    });
    note(ui, "No traffic or police in network games: just the players (and the pedestrians).");
    sec(ui, "Session");
    field(ui, "Game name", "", [&]() {
        InputOpts o;
        o.max = 30;
        o.placeholder = mpName() + "'s game";
        const unsigned f = input(ui, "hsname", mp_.sessionName, o);
        if (f & (Ui::EditSubmit)) send = true;
        if (lastSnameFocus_ && ui.focus() != ui.id("hsname")) send = true;
        lastSnameFocus_ = ui.focus() == ui.id("hsname");
    });
    field(ui, "Password", "", [&]() {
        InputOpts o;
        o.max = 20;
        o.placeholder = "none";
        const unsigned f = input(ui, "hspass", mp_.password, o);
        if (f & (Ui::EditSubmit)) send = true;
        if (lastSpassFocus_ && ui.focus() != ui.id("hspass")) send = true;
        lastSpassFocus_ = ui.focus() == ui.id("hspass");
    });
    std::vector<Choice> maxes;
    for (int n = 2; n <= 8; ++n) maxes.push_back(Choice{std::to_string(n), n, false});
    field(ui, "Players", "", [&]() {
        if (seg(ui, "hmax", maxes, mp_.maxPlayers)) send = true;
    });
    if (defaults) mpDefaults();
    if (send) {
        mpSendSettings();
        rerender();
    }
}

}  // namespace fe
}  // namespace cc
