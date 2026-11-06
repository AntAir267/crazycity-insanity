// The front end's menu screens (web/ui.js SCREENS): main, drivers, the single player race menu,
// the car screen, Crash Course, the driver record, race statistics and options.
#include <algorithm>
#include <cmath>

#include "app/fe_theme.h"
#include "app/fe_words.h"
#include "app/front_end.h"
#include "app/menu_data.h"
#include "core/strutil.h"

namespace cc {
namespace fe {

using namespace ui;

namespace {
JValue parse(const std::string& s) { return JValue::parse(s); }

// One action of the binding table: its name and its input (a row that opens the capture).
bool bindRow(Ui& ui, const std::string& key, const std::string& action, const std::string& input, bool none, bool twice) {
    const Id id = ui.id(key);
    Interact it = ui.interact(id);
    Style s;
    s.row();
    s.gap = 10;
    s.align = Align::Center;
    s.pad = Edges(7, 10);
    s.round(8);
    s.bg = it.hover || it.focusVisible ? rgba(45, 212, 191, 0.08f) : Color();
    s.outline(1, it.focusVisible ? col::accent : Color());
    s.cursor = Cursor::Hand;
    s.hit = true;
    s.shrink = 0;
    Node& n = ui.open(id, s);
    n.label = action;
    Style a = tBody(13);
    a.grow = 1;
    a.minW = 0;
    a.nowrap = true;
    a.ellipsis = true;
    ui.text(action, a);
    Style k = tBody(12.5f, 650, none ? col::muted : twice ? col::warn : col::text);
    k.pad = Edges(3, 9);
    k.round(6);
    k.bg = rgba(255, 255, 255, 0.05f);
    k.outline(1, twice ? rgba(251, 191, 36, 0.45f) : col::line);
    k.nowrap = true;
    k.shrink = 0;
    ui.text(input, k);
    ui.close();
    return it.clicked;
}
}  // namespace

const JValue& FrontEnd::cached(const std::string& key, const std::function<std::string()>& fn) {
    auto& c = caches_[key];
    if (c.first != serial_) {
        c.second = parse(fn());
        c.first = serial_;
    }
    return c.second;
}

// ------------------------------------------------------------------------------------- main

void FrontEnd::screenMain(Ui& ui) {
    h1(ui, "Ready to drive?");
    sub(ui, "Midtown Madness 2's cities, cars and rules, in our own engine.");
    Style m;
    m.column();
    m.gap = 8;
    ui.open(m);
    if (item(ui, "single", "▶", "Single player", "Cruise the city or pick a race", true)) go("race");
    if (item(ui, "quick", "⚡", "Quick race", "Straight to the car with the last settings")) go("car");
    if (item(ui, "crash", "✚", T(12), "13 lessons per city")) go("crashcity");
    if (item(ui, "mp", "⇄", "Multiplayer", mpOpen() ? "Back to your network game" : "Host or join a game with friends")) go("mp");
    if (item(ui, "drivers", "☺", "Drivers", "Choose, create, delete or import drivers")) go("drivers");
    if (item(ui, "record", "★", "Driver record", "Scores and passed races")) go("record");
    if (item(ui, "stats", "≡", "Race statistics", "The five best times and scores of every race")) go("stats");
    if (item(ui, "options", "⚙", "Options", "Audio, graphics, controls")) go("options");
    if (item(ui, "viewer", "⌂", "Back to the viewer", "Cars, cities and free driving")) closeFrontEnd();
    if (host_.canQuit() && item(ui, "quit", "✕", "Quit", "Close CrazyCity Insanity")) host_.quit();
    ui.close();
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
}

// ------------------------------------------------------------------------------------- drivers

void FrontEnd::screenDrivers(Ui& ui) {
    h1(ui, "Drivers");
    sub(ui, "Each driver keeps its own options, records and unlocked cars, in MM2's own file formats.");
    const JValue& names = state_["drivers"];
    const int current = state_["current"].integer(-1);
    beginList(ui, "list", ui.viewH() * 0.38f);
    for (size_t i = 0; i < names.size(); ++i) {
        const bool cur = int(i) == current;
        if (listItem(ui, "d" + std::to_string(i), "", names[i].str(), cur, false, [&]() {
                if (cur) badge(ui, "current", Badge::Accent);
            })) {
            const JValue r = parse(menu::selectDriver(int(i)));
            if (!r["ok"].truthy() && r["why"].truthy()) toast(r["why"].str());
            refreshState(&r);
            refreshCars();
            syncSelFromDriver();
            rerender();
        }
    }
    endList(ui);
    beginRow(ui, Justify::Start, 8, 14);
    if (button(ui, "new", "New driver")) {
        modal_ = Modal::NewDriver;
        newName_.clear();
        newSkill_ = 0;
        newMsg_.clear();
        newFocus_ = true;
    }
    if (button(ui, "delete", "Delete driver", Btn::Danger, current < 0)) {
        const int i = current;
        const std::string name = names[i].str();
        confirmBox("Delete " + name + "?", "Its options, records and its place in the halls of fame list stay out of reach once deleted.",
                   "Delete", [this, i]() {
                       const JValue r = parse(menu::deleteDriver(i));
                       if (!r["ok"].truthy()) {
                           const std::string why = r["why"].str();
                           toast(why == "last" ? "The last driver cannot be deleted." : (why.empty() ? "Not deleted." : why));
                       }
                       refreshState(&r);
                       refreshCars();
                       loadRaces();
                       rerender();
                   });
    }
    endRow(ui);
    sec(ui, "Bring your MM2 progress");
    beginRow(ui, Justify::Start, 8);
    if (button(ui, "import", "Import my MM2 profiles…", Btn::Ghost, !host_.canPick(Pick::PlayersFolder)))
        confirmImport([this]() { host_.pick(Pick::PlayersFolder); });
    if (devPlayers_ && button(ui, "importdev", devIsDesktop_ ? "Import from my MM2 folder" : "Import from this server's game folder", Btn::Ghost))
        confirmImport([this]() { host_.pick(Pick::DevPlayers); });
    endRow(ui);
    noteRich(ui, {span("Pick your Midtown Madness 2 folder (or its ", 12), span("players", 12, 700, rgbaHex(col::text)),
                  span(host_.isBrowser() ? " folder). The files are read in your browser and kept there; nothing is uploaded. Drivers whose "
                                           "files fail MM2's checks are skipped, as MM2 would drop them."
                                         : " folder). The files are read and kept on this computer. Drivers whose files fail MM2's "
                                           "checks are skipped, as MM2 would drop them.",
                       12)});
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
}

// ------------------------------------------------------------------------------------- race menu

void FrontEnd::screenRace(Ui& ui) {
    Sel& s = sel_;
    if (!modeAvailable(s.mode)) {
        s.mode = 0;
        s.race = 0;
        resetEnv();
    }
    h1(ui, "Single player");
    // City.
    std::vector<Choice> cities;
    for (const CityInfo& c : cities_) cities.push_back(Choice{c.name, c.index, false});
    field(ui, "City", "", [&]() {
        int v = s.city;
        if (seg(ui, "city", cities, v)) {
            s.city = v;
            s.race = 0;
            loadRaces();
            if (!modeAvailable(s.mode)) s.mode = 0;
            resetEnv();
            rerender();
        }
    });
    // Mode.
    std::vector<Choice> modes;
    for (int m : {4, 3, 1, 0}) modes.push_back(Choice{listName(m), m, !modeAvailable(m)});
    {
        int m = s.mode;
        if (tabs(ui, "mode", modes, m)) {
            s.mode = m;
            s.race = 0;
            resetEnv();
            rerender();
        }
    }
    if (s.mode != 0) {
        const JValue& races = racesOf(s.mode);
        if (!races[s.race]["open"].truthy()) {
            int first = 0;
            for (size_t i = 0; i < races.size(); ++i)
                if (races[i]["open"].truthy()) {
                    first = int(i);
                    break;
                }
            s.race = first;
        }
        beginList(ui, "races", ui.viewH() * 0.38f);
        for (size_t i = 0; i < races.size(); ++i) {
            const JValue& r = races[i];
            const bool open = r["open"].truthy();
            if (listItem(ui, "r" + std::to_string(i), std::to_string(i + 1), r["name"].str(), int(i) == s.race, !open, [&]() {
                    if (r["best"].num() > 0) {
                        Style meta = tBody(12, 400, col::muted);
                        meta.nowrap = true;
                        ui.text(fmtTime(r["best"].num()), meta);
                    }
                    if (r["passed"].truthy()) badge(ui, "passed", Badge::Ok);
                    else if (!open) badge(ui, "locked", Badge::Lock);
                })) {
                s.race = int(i);
                resetEnv();
                rerender();
            }
        }
        endList(ui);
        if (!races_["records"].truthy())
            note(ui, std::string(state_["current"].integer(-1) < 0 ? "Without a driver" : "In this city") +
                         " every race is open and nothing is recorded.");
    } else {
        note(ui, "Free roam: start anywhere in the city, with traffic and cops. Pick the conditions below.");
    }
    envControls(ui);
    beginRow(ui, Justify::End, 8, 14);
    if (button(ui, "go", "Choose car →", Btn::Go)) go("car");
    endRow(ui);
    // Side: what is chosen.
    sideCards_.push_back([this, &ui]() {
        const Sel& s = sel_;
        const JValue& race = s.mode != 0 ? racesOf(s.mode)[s.race] : JValue::parse("null");
        Style card;
        card.column();
        card.bg = rgba(255, 255, 255, 0.03f);
        card.outline(1, col::line);
        card.round(12);
        card.pad = Edges(12, 14);
        card.w = std::min(360.0f, ui.viewW() * 0.34f);
        card.hit = true;
        ui.open("racecard", card);
        sec(ui, listName(s.mode), 0);
        ui.text(!race.isNull() ? race["name"].str() : races_["name"].str(), tBody(20, 750));
        if (!race.isNull() && race["score"].integer()) note(ui, "Best score " + std::to_string(race["score"].integer()));
        if (s.mode == 4 && s.env.timeLimit) note(ui, "Time limit " + fmtTime(s.env.timeLimit));
        if (s.mode != 0 && race["locked"].truthy() && races_["records"].truthy()) note(ui, "Pass this race once to change its settings.");
        if (s.mode != 0 && !ownSettings()) note(ui, "Changed settings: the result will not be recorded.", true);
        ui.close();
        driverCard(ui);
    });
}

void FrontEnd::envControls(Ui& ui) {
    Sel& s = sel_;
    Env& e = s.env;
    sec(ui, "Conditions");
    std::vector<Choice> tod, weather;
    for (int i = 0; i < 4; ++i) {
        tod.push_back(Choice{T(629 + i), i, false});
        weather.push_back(Choice{T(625 + i), i, false});
    }
    field(ui, "Time of day", "", [&]() {
        if (seg(ui, "tod", tod, e.tod, !editable("tod"))) rerender();
    });
    field(ui, "Weather", "", [&]() {
        if (seg(ui, "weather", weather, e.weather, !editable("weather"))) rerender();
    });
    field(ui, "Traffic", pct(e.traffic), [&]() {
        float v = float(e.traffic);
        if (slider(ui, "traffic", 0, 1, v, !editable("traffic"))) {
            e.traffic = v;
            rerender();
        }
    });
    field(ui, "Pedestrians", pct(e.peds), [&]() {
        float v = float(e.peds);
        if (slider(ui, "peds", 0, 1, v, !editable("peds"))) e.peds = v;
    });
    // The cop control (menus.md, "The cop control"): a 0 … 1 slider in every mode. A race starts at
    // its row's Cops count (1 or more shown full) and keeps it until the slider is moved; any move
    // writes the slider's value, and the record compares the stored value with the count exactly.
    field(ui, "Cops", pct(std::min(1.0f, e.cops)), [&]() {
        bool viaKey = false;
        if (mm2Slider(ui, "cops", e.cops, !editable("cops"), viaKey) && s.mode != 0) rerender();
    });
    if (s.mode == 3) {
        std::vector<Choice> laps;
        for (int n = 1; n <= 8; ++n) laps.push_back(Choice{std::to_string(n), n, false});
        field(ui, "Laps", "", [&]() {
            if (seg(ui, "laps", laps, e.laps, !editable("laps"))) rerender();
        });
        const int max = std::max(1, std::min(10, e.maxOpponents ? e.maxOpponents : 1));
        std::vector<Choice> opp;
        for (int n = 1; n <= max; ++n) opp.push_back(Choice{std::to_string(n), n, false});
        field(ui, "Opponents", "", [&]() {
            if (seg(ui, "opponents", opp, e.opponents, !editable("opponents"))) rerender();
        });
    }
}

// ------------------------------------------------------------------------------------- car screen

void FrontEnd::screenCar(Ui& ui) {
    Sel& s = sel_;
    std::vector<const JValue*> cars;
    for (const JValue& c : cars_.items())
        if (c["index"].integer(-1) >= 0) cars.push_back(&c);
    if (cars.empty()) {
        note(ui, "No cars loaded.");
        return;
    }
    int ci = -1;
    for (size_t i = 0; i < cars.size(); ++i)
        if ((*cars[i])["index"].integer() == s.car) ci = int(i);
    // Coming back while the chosen car is locked: the last unlocked car looked at, or the first.
    if (entering_ && (ci < 0 || ((*cars[size_t(ci)])["locked"].truthy() && !carLookedAt_))) {
        int alt = -1;
        for (size_t i = 0; i < cars.size(); ++i)
            if ((*cars[i])["index"].integer() == lastUnlockedCar_ && !(*cars[i])["locked"].truthy()) alt = int(i);
        if (alt < 0)
            for (size_t i = 0; i < cars.size(); ++i)
                if (!(*cars[i])["locked"].truthy()) {
                    alt = int(i);
                    break;
                }
        if (alt >= 0) ci = alt;
        if (ci < 0) ci = 0;
    }
    if (ci < 0) ci = 0;
    if (entering_) carLookedAt_ = false;
    const JValue& car = *cars[size_t(ci)];
    if (s.car != car["index"].integer()) {
        s.car = car["index"].integer();
        auto it = carPaint_.find(car["name"].str());
        s.paint = it == carPaint_.end() ? 0 : it->second;
        showCar();
    }
    if (!car["locked"].truthy()) lastUnlockedCar_ = car["index"].integer();
    const JValue& paints = car["paints"];
    if (s.paint >= int(paints.size())) s.paint = 0;
    if (entering_) showCar();
    auto step = [&](int d) {
        const int n = (ci + d + int(cars.size())) % int(cars.size());  // wraps around
        s.car = (*cars[size_t(n)])["index"].integer();
        auto it = carPaint_.find((*cars[size_t(n)])["name"].str());
        s.paint = it == carPaint_.end() ? 0 : it->second;
        carLookedAt_ = true;
        showCar();
        rerender();
    };
    const bool forNet = carFor_ == "mp";
    beginRow(ui, Justify::SpaceBetween, 8, 0, false);
    {
        Style c;
        c.column();
        c.shrink = 1;
        c.minW = 0;
        ui.open(c);
        sec(ui, forNet ? std::string("Multiplayer") : listName(s.mode) + " · " + races_["name"].str(), 0);
        Style nameRow;
        nameRow.row();
        nameRow.gap = 10;
        nameRow.align = Align::Center;
        nameRow.wrap = true;
        ui.open(nameRow);
        Style cn = tBody(26, 800);
        cn.text.letterSpacing = -0.26f;
        ui.text(car["description"].str(), cn);
        if (car["mod"].truthy()) badge(ui, "mod", Badge::Muted);
        if (car["locked"].truthy()) badge(ui, "locked", Badge::Lock);
        ui.close();
        ui.close();
    }
    {
        Style nav;
        nav.row();
        nav.gap = 8;
        nav.align = Align::Center;
        nav.shrink = 0;
        ui.open(nav);
        for (int d : {-1, 1}) {
            const Id id = ui.id(d < 0 ? "prev" : "next");
            Interact it = ui.interact(id);
            Style a;
            a.size(40, 40);
            a.round(12);
            a.outline(1, it.hover ? col::accent : col::lineStrong);
            a.bg = rgba(255, 255, 255, 0.03f);
            a.font(18);
            a.textAlign = TextAlign::Center;
            a.textVCenter = true;
            a.cursor = Cursor::Hand;
            a.hit = true;
            Node& n = ui.text(d < 0 ? "‹" : "›", a, id);
            n.label = d < 0 ? "previous car" : "next car";
            if (it.clicked) step(d);
        }
        ui.close();
    }
    endRow(ui);
    note(ui, std::to_string(ci + 1) + " of " + std::to_string(cars.size()) + " · " + car["name"].str());
    // Paint.
    sec(ui, T(430));
    {
        Style ps;
        ps.row();
        ps.wrap = true;
        ps.gap = 6;
        ui.open("paints", ps);
        for (size_t i = 0; i < paints.size(); ++i) {
            const JValue& p = paints[i];
            const bool on = int(i) == s.paint;
            const bool locked = p["locked"].truthy();
            const Id id = ui.id("p" + std::to_string(i));
            Interact it = ui.interact(id);
            Style b;
            b.pad = Edges(6, 12);
            b.round(999);
            b.outline(1, on ? col::accent : (it.hover ? col::accent : col::lineStrong));
            b.bg = on ? rgba(45, 212, 191, 0.14f) : Color();
            b.color = on ? col::accent : col::text;
            b.font(13);
            b.nowrap = true;
            b.cursor = Cursor::Hand;
            b.hit = true;
            if (locked) b.opacity = 0.55f;
            const std::string name = p["name"].truthy() ? p["name"].str() : "Paint " + std::to_string(i + 1);
            Node& n = ui.text((locked ? "🔒 " : "") + name, b, id);
            n.label = name;
            if (locked) n.tooltip = unlockText(p["why"], false);
            if (it.clicked) {
                s.paint = int(i);
                carPaint_[car["name"].str()] = int(i);
                carLookedAt_ = true;
                svc_.setPaint(s.paint);
                rerender();
            }
        }
        ui.close();
    }
    // Gearbox.
    const bool autoBox = options().isNull() ? true : options()["automatic"].truthy();
    field(ui, "Gearbox", "", [&]() {
        int v = autoBox ? 1 : 0;
        if (seg(ui, "gearbox", {Choice{T(634), 1, false}, Choice{T(633), 0, false}}, v)) {
            menu::setOption("automatic", v);
            state_ = parse(menu::state());
            carLookedAt_ = true;
            rerender();
        }
    });
    // Facts: bars between 0.5 × the smallest and 1.1 × the largest value of all cars.
    sec(ui, "Facts");
    {
        Style g;
        g.grid({Track::px(100), Track::fr(1), Track::px(70)});
        g.gap = 10;
        g.rowGap = 8;
        g.align = Align::Center;
        ui.open(g);
        const char* keys[4] = {"horsepower", "topSpeed", "durability", "mass"};
        const char* labels[4] = {"Horsepower", "Top speed", "Durability", "Mass"};
        for (int k = 0; k < 4; ++k) {
            double lo = 1e30, hi = -1e30;
            for (const JValue* c : cars) {
                lo = std::min(lo, (*c)[keys[k]].num());
                hi = std::max(hi, (*c)[keys[k]].num());
            }
            lo *= 0.5;
            hi *= 1.1;
            const double v = car[keys[k]].num();
            const float f = hi > lo ? float(std::max(0.0, std::min(1.0, (v - lo) / (hi - lo)))) : 0.0f;
            ui.text(labels[k], tBody(12.5f));
            Style bar;
            bar.h = 8;
            bar.round(4);
            bar.bg = rgba(255, 255, 255, 0.07f);
            Node& b = ui.leaf(bar);
            b.paint = [f](DrawList& dl, const Rect& r, float sc) {
                const float rr[4] = {4 * sc, 4 * sc, 4 * sc, 4 * sc};
                if (f > 0) dl.gradient(Rect{r.x, r.y, r.w * f, r.h}, col::accent2, col::accent, 3.14159265f / 2, rr);
            };
            char buf[64];
            if (k == 0) std::snprintf(buf, sizeof buf, "%.0f hp", std::round(v));
            else if (k == 1) std::snprintf(buf, sizeof buf, "%.0f mph", std::round(v));
            else if (k == 2) std::snprintf(buf, sizeof buf, "%.0fk", v / 1000.0);
            else std::snprintf(buf, sizeof buf, "%.0f kg", std::round(v));
            Style vs = tBody(12.5f, 400, col::muted);
            vs.textAlign = TextAlign::Right;
            ui.text(buf, vs);
        }
        ui.close();
    }
    const bool paintLocked = paints[s.paint]["locked"].truthy();
    beginRow(ui, Justify::End, 8, 16);
    if (button(ui, "go", forNet ? "Use this car" : "Go!", Btn::Go)) {
        if (car["locked"].truthy() || paintLocked) {
            toast(car["locked"].truthy() ? "This car is still locked: win races to unlock it." : "This paint job is still locked.");
        } else {
            carPaint_[car["name"].str()] = s.paint;
            if (forNet) {
                // The network game's car (multiplayer.md 1.1: the record's car and paint).
                mp_.car = car["index"].integer();
                mp_.paint = s.paint;
                mpSendRecord();
                back();
            } else {
                startGame();
            }
        }
    }
    endRow(ui);
    if (car["locked"].truthy() || paintLocked) {
        dim_ = true;
        lockCard_ = unlockText(car["locked"].truthy() ? car["why"] : paints[s.paint]["why"], car["locked"].truthy());
    }
}

// ------------------------------------------------------------------------------------- crash course

void FrontEnd::screenCrashCity(Ui& ui) {
    h1(ui, T(12));
    sub(ui, "Driving lessons in London and San Francisco.");
    Style m;
    m.column();
    m.gap = 8;
    ui.open(m);
    for (const char* rd : {"london", "sf"}) {
        const CityInfo* c = cityByRaceDir(rd);
        if (!c) continue;
        if (item(ui, std::string("city") + rd, std::string(rd) == "sf" ? "SF" : "L", c->name, "13 lessons", true)) {
            crashCity_ = c->index;
            go("crash");
        }
    }
    ui.close();
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
}

void FrontEnd::screenCrash(Ui& ui) {
    const int cityIdx = crashCity_;
    const CityInfo* c = cityByIndex(cityIdx);
    const JValue& races = cached("crash:" + std::to_string(cityIdx), [cityIdx]() { return menu::cityRaces(cityIdx); });
    h1(ui, T(12) + " · " + (c ? c->name : std::string()));
    const int base = c && c->raceDir == "sf" ? 545 : 532;
    beginList(ui, "lessons", ui.viewH() * 0.46f);
    const JValue& list = races["crash"];
    for (size_t i = 0; i < list.size(); ++i) {
        const JValue& r = list[i];
        const bool open = r["open"].truthy();
        const int st = r["state"].integer();
        if (listItem(ui, "l" + std::to_string(i), std::to_string(i + 1), T(base + int(i)), false, !open, [&]() {
                if (st == 1) badge(ui, "passed", Badge::Ok);
                else if (st == 2) badge(ui, "tried", Badge::Muted);
                if (!open) badge(ui, "locked", Badge::Lock);
            })) {
            // A lesson not yet passed is driven in its fixed car (vpbullet in SF, vpcab in London, paint 0).
            Spec spec;
            spec.city = cityIdx;
            spec.mode = 6;
            spec.race = int(i);
            if (st != 1 && c) spec.fixedCar = c->raceDir == "sf" ? "vpbullet" : "vpcab";
            startRaceSpec(spec);
        }
    }
    endList(ui);
    note(ui, "The lessons themselves arrive with the race modes; their progress is already read from your records.");
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
}

// ------------------------------------------------------------------------------------- driver record

void FrontEnd::screenRecord(Ui& ui) {
    const JValue& r = cached("record", []() { return menu::driverRecord(); });
    const JValue& d = driver();
    const bool pro = d["skill"].truthy();
    h1(ui, "Driver record");
    sub(ui, (d.isNull() ? std::string("No driver") : d["name"].str()) + " · San Francisco and London together");
    std::vector<TableCol> cols = {{"", Track::fr(1), false}, {"Passed", Track::autoW(), true}};
    if (pro) cols.push_back({T(344), Track::autoW(), true});
    beginTable(ui, cols);
    const int pairs[3][2] = {{4, 2}, {3, 1}, {1, 0}};
    for (auto& p : pairs) {
        std::vector<std::string> cells = {listName(p[0]), std::to_string(r["passed"][p[1]].integer())};
        if (pro) cells.push_back(fmtThousands((long long)r["score"][p[1]].num()));
        tableRow(ui, cols, cells);
    }
    endTable(ui);
    if (pro) {
        beginCard(ui, "", 12);
        beginRow(ui, Justify::SpaceBetween);
        ui.text(T(640), tBody(13));
        ui.text(fmtThousands((long long)r["total"].num()), tBody(20, 700));
        endRow(ui);
        endCard(ui);
    }
    // The races of one city and type: 345 race, 346 time, 347 car, and 344 points for a
    // Professional driver (mmlang.md).
    std::vector<const JValue*> cities;
    for (const JValue& c : r["cities"].items())
        if (c["valid"].truthy() && c["types"].truthy()) cities.push_back(&c);
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
    if (cities.empty()) return;
    bool found = false;
    for (const JValue* c : cities)
        if ((*c)["raceDir"].str() == recCity_) found = true;
    if (!found) recCity_ = (*cities[0])["raceDir"].str();
    sec(ui, "Best results");
    std::vector<Choice> cityChoices;
    for (size_t i = 0; i < cities.size(); ++i) cityChoices.push_back(Choice{(*cities[i])["name"].str(), int(i), false});
    int ci = 0;
    for (size_t i = 0; i < cities.size(); ++i)
        if ((*cities[i])["raceDir"].str() == recCity_) ci = int(i);
    field(ui, "City", "", [&]() {
        if (seg(ui, "city", cityChoices, ci)) {
            recCity_ = (*cities[size_t(ci)])["raceDir"].str();
            rerender();
        }
    });
    const JValue& city = *cities[size_t(ci)];
    std::vector<Choice> types;
    for (int k : {2, 1, 0}) types.push_back(Choice{listName(city["types"][k]["mode"].integer()), k, false});
    tabs(ui, "type", types, recType_);
    std::vector<TableCol> rc = {{T(345), Track::fr(1), false}, {T(346), Track::autoW(), true}, {T(347), Track::autoW(), false}};
    if (pro) rc.push_back({T(344), Track::autoW(), true});
    Style wrap;
    wrap.column();
    wrap.maxH = ui.viewH() * 0.34f;
    wrap.scrollY = true;
    wrap.hit = true;
    ui.open("recwrap", wrap);
    beginTable(ui, rc);
    for (const JValue& race : city["types"][recType_]["races"].items()) {
        std::vector<std::string> cells = {race["name"].str(), race["time"].num() > 0 ? fmtTime(race["time"].num()) : std::string(),
                                          race["car"].truthy() ? carDesc(race["car"].str()) : std::string()};
        if (pro) cells.push_back(race["score"].integer() ? fmtThousands(race["score"].integer()) : std::string());
        tableRow(ui, rc, cells, race["passed"].truthy(), {false, false, true, false});
    }
    endTable(ui);
    ui.close();
    note(ui, city["name"].str() + ": " + std::to_string(city["passedCount"][3].integer()) + " of " +
                 std::to_string(city["counts"][3].integer()) + " " + T(12) + " lessons passed");
}

// ------------------------------------------------------------------------------------- race statistics

void FrontEnd::screenStats(Ui& ui) {
    if (statCity_ < 0) statCity_ = sel_.city;
    h1(ui, T(348));
    std::vector<Choice> cities;
    for (const CityInfo& c : cities_) cities.push_back(Choice{c.name, c.index, false});
    field(ui, "City", "", [&]() {
        if (seg(ui, "city", cities, statCity_)) rerender();
    });
    const std::vector<std::string> tables = {T(82) + " · " + T(350), T(81) + " · " + T(350), T(81) + " · " + T(349)};
    field(ui, "Table", "", [&]() {
        if (seg(ui, "table", choices(tables), statTable_)) rerender();
    });
    const int skill = statTable_ == 0 ? 0 : 1;
    const bool scores = statTable_ == 2;
    const int city = statCity_;
    const JValue& r = cached("stats:" + std::to_string(city) + ":" + std::to_string(skill), [city, skill]() { return menu::raceStats(city, skill); });
    sideCards_.push_back([this, &ui]() { driverCard(ui); });
    if (!r["ok"].truthy()) {
        note(ui, "No hall of fame for this city.");
        return;
    }
    std::vector<Choice> types;
    for (int i : {2, 1, 0}) {
        const JValue& ty = r["types"][i];
        types.push_back(Choice{listName(ty["mode"].integer()), i, ty["races"].size() == 0});
    }
    tabs(ui, "type", types, statType_);
    const JValue& ty = r["types"][statType_].isNull() ? r["types"][0] : r["types"][statType_];
    Style wrap;
    wrap.column();
    wrap.maxH = ui.viewH() * 0.4f;
    wrap.scrollY = true;
    wrap.hit = true;
    ui.open("statwrap", wrap);
    const std::string me = driver().isNull() ? std::string() : driver()["name"].str();
    // Columns 351 race, 353 driver, 350 time or 349 score, 355 car.
    for (size_t i = 0; i < ty["races"].size(); ++i) {
        const JValue& race = ty["races"][i];
        std::vector<TableCol> cols = {{T(351) + " " + std::to_string(i + 1) + " · " + race["name"].str(), Track::autoW(), false},
                                      {T(353), Track::fr(1), false},
                                      {T(scores ? 349 : 350), Track::autoW(), true},
                                      {T(355), Track::autoW(), false}};
        beginTable(ui, cols, 0, 12);
        const JValue& list = scores ? race["scores"] : race["times"];
        for (int k = 0; k < 5; ++k) {
            const JValue& a = list[k];
            std::vector<std::string> cells = {std::to_string(k + 1), a.isNull() ? std::string("—") : a["name"].str(),
                                              a.isNull() ? std::string()
                                                         : (scores ? fmtThousands((long long)std::trunc(a["value"].num())) : fmtTime(a["value"].num())),
                                              a.isNull() ? std::string() : carDesc(a["car"].str())};
            tableRow(ui, cols, cells, !a.isNull() && a["name"].str() == me && !me.empty(), {true, false, false, true});
        }
        endTable(ui);
    }
    ui.close();
}

// ------------------------------------------------------------------------------------- options

void FrontEnd::optionsPanel(Ui& ui, bool inRace) {
    const JValue& st = cached("state", []() { return menu::state(); });
    const JValue& o = st["options"].isNull() ? options() : st["options"];
    auto setOpt = [this](const char* name, double v) {
        menu::setOption(name, v);
        rerender();
    };
    h1(ui, "Options");
    std::vector<Choice> tabsList = {{"Audio", 0, false}, {"Graphics", 1, false}, {"Controls", 2, false}};
    if (!inRace) tabsList.push_back({"Customise controls", 3, false});
    int tab = optTab_ == "audio" ? 0 : optTab_ == "controls" ? 2 : optTab_ == "keys" ? 3 : 1;
    if (tab == 3 && inRace) tab = 1;
    if (tabs(ui, "tabs", tabsList, tab)) {
        optTab_ = tab == 0 ? "audio" : tab == 2 ? "controls" : tab == 3 ? "keys" : "graphics";
        rerender();
    }
    Style box;
    box.column();
    ui.open("optbox", box);
    if (tab == 0) {
        const int bits = o["audioBits"].integer();
        auto bit = [&](int b) { return (bits & b) != 0; };
        auto chk = [&](const char* key, const char* label, int b, int clear) {
            bool v = bit(b);
            if (check(ui, key, label, v)) {
                int nv = v ? (bits | b) : (bits & ~b);
                if (v && clear) nv &= ~clear;
                setOpt("audioBits", nv);
            }
        };
        // The switches only in the menus (applied when a game starts); effects volume, music volume
        // and balance in the menus and in the game, applied at once (menus.md, "In-race menus").
        if (!inRace) {
            chk("sfx", "Sound effects", 0x001, 0);
            chk("commentary", "Race commentary", 0x400, 0);
            chk("music", "Music", 0x004, 0x800);
            chk("ambient", "City ambience instead of music", 0x800, 0x004);
        }
        field(ui, "Effects volume", pct(o["sfxVolume"].num()), [&]() {
            float v = o["sfxVolume"].f();
            if (slider(ui, "sfxv", 0, 1, v)) {
                setOpt("sfxVolume", v);
                svc_.setSoundOption("volume", int(std::lround(v * 100)));
            }
        });
        field(ui, "Music volume", pct(o["musicVolume"].num()), [&]() {
            float v = o["musicVolume"].f();
            if (slider(ui, "musv", 0, 1, v)) {
                setOpt("musicVolume", v);
                svc_.setMusicVolume(v);
            }
        });
        field(ui, "Balance", fmt2(o["balance"].num()), [&]() {
            float v = o["balance"].f();
            if (slider(ui, "bal", -1, 1, v)) {
                setOpt("balance", v);
                svc_.setSoundOption("balance", int(std::lround(v * 100)));
            }
        });
        if (!inRace) {
            const int q = (bits & 0x30) == 0x30 ? 2 : (bits & 0x10) ? 1 : 0;
            field(ui, "Sound quality", "", [&]() {
                int v = q;
                if (seg(ui, "quality", choices({"Low (8)", "Medium (16)", "High (32)"}), v)) {
                    int nb = bits & ~0x30;
                    if (v >= 1) nb |= 0x10;
                    if (v == 2) nb |= 0x20;
                    menu::setOption("audioBits", nb);
                    setOpt("channels", v == 0 ? 8 : v == 1 ? 16 : 32);
                }
            });
            field(ui, "Channels", "", [&]() {
                int v = bit(0x040) ? 1 : 0;
                if (seg(ui, "channels", choices({"Mono", "Stereo"}), v)) setOpt("audioBits", v ? (bits | 0x040) : (bits & ~0x040));
            });
            // The music's state and the General MIDI bank (the engine's music system).
            const JValue& ms = cached("music", [this]() { return svc_.musicStatus(); });
            if (!ms.isNull()) {
                const std::string state = ms["mode"].str() == "off" ? std::string("off")
                                          : !ms["data"].truthy()    ? std::string("no music data (open your whole MM2 folder in the viewer)")
                                          : ms["playing"].truthy()  ? "playing " + ms["playing"].str()
                                                                    : std::string("ready");
                beginCard(ui, "", 10);
                Style g;
                g.grid({Track::autoW(), Track::fr(1)});
                g.gap = 14;
                g.rowGap = 6;
                g.align = Align::Center;
                ui.open(g);
                ui.text("Music", tBody(13, 400, col::muted));
                ui.text(state, tBody(13));
                ui.text("General MIDI bank", tBody(13, 400, col::muted));
                Style bw;
                bw.row();
                ui.open(bw);
                if (ms["gm"].truthy()) badge(ui, ms["gmName"].truthy() ? ms["gmName"].str() : std::string("loaded"), Badge::Ok);
                else badge(ui, "built-in", Badge::Muted);
                ui.close();
                ui.close();
                endCard(ui);
            }
        }
    } else if (tab == 1) {
        std::vector<std::string> det;
        for (int id : {574, 575, 576, 577}) det.push_back(T(id));
        field(ui, "Object detail", "", [&]() {
            int v = o["objectDetail"].integer();
            if (seg(ui, "detail", choices(det), v)) {
                setOpt("objectDetail", v);
                svc_.setCityOption("detail", v);
            }
        });
        // Our own longer draw distances (viewer.md, "Draw distance"): kept per user only (the same
        // setting as the viewer's Draw distance buttons), never in the .cfg, so the profile stays
        // MM2's; when one is picked it overrides the view distance.
        const int far = std::atoi(host_.storageGet("cci.drawDistance").c_str());
        field(ui, "View distance", std::to_string(int(std::lround(o["viewDistance"].num()))) + " m", [&]() {
            float v = o["viewDistance"].f();
            if (slider(ui, "viewdist", 100, 1000, v, far != 0)) {
                setOpt("viewDistance", v);
                // In a game the view distance replaces the far plane at once (cameras.md).
                if (inGame_) svc_.gameSetViewDistance(v);
            }
        });
        const std::string farLabel = far == 0 ? "the view distance" : far < 0 ? "the whole city" : std::to_string(far / 1000) + " km";
        field(ui, "Draw distance", farLabel, [&]() {
            int v = far;
            if (seg(ui, "far", {{"MM2", 0, false}, {"1 km", 1000, false}, {"2 km", 2000, false}, {"4 km", 4000, false}, {"Full map", -1, false}}, v)) {
                host_.storageSet("cci.drawDistance", std::to_string(v));
                svc_.setCityOption("far", v);
                v_.far = v;
                rerender();
            }
        });
        field(ui, "Lighting quality", "", [&]() {
            int v = int(std::lround(o["lighting"].num()));
            std::vector<Choice> c = choices({"0", "1", "2", "3"});
            if (inRace) c.erase(c.begin());  // in a race: 1 – 3
            if (seg(ui, "lighting", c, v)) setOpt("lighting", v);
        });
        field(ui, "Cloud shadows", "", [&]() {
            int v = o["cloudShadows"].integer();
            if (seg(ui, "clouds", choices({T(660), T(661), T(576)}), v)) setOpt("cloudShadows", v);
        });
        auto chk = [&](const char* key, const char* label, const char* name, bool val, const char* engineOpt) {
            bool v = val;
            if (check(ui, key, label, v)) {
                setOpt(name, v ? 1 : 0);
                if (engineOpt) svc_.setCityOption(engineOpt, v ? 1 : 0);
            }
        };
        chk("refl", "Car reflections", "reflections", o["reflections"].truthy(), "reflections");
        chk("sky", "Textured sky", "texturedSky", o["texturedSky"].truthy(), "sky");
        if (!inRace) {
            bool peds = !o["pedestriansOff"].truthy();
            if (check(ui, "peds", "Pedestrians", peds)) {
                setOpt("pedestriansOff", peds ? 0 : 1);
                svc_.setDriveOption("pedsoff", peds ? 0 : 1);
            }
            field(ui, "Texture quality", "", [&]() {
                int v = o["textureQuality"].integer();
                if (seg(ui, "texq", choices({"0", "1", "2", "3"}), v)) setOpt("textureQuality", v);
            });
            note(ui, "Object detail, view distance, reflections, the sky and the Pedestrians switch apply to the engine; lighting, cloud "
                     "shadows and texture quality are kept for MM2.");
        }
    } else if (tab == 2) {
        const bool pads = svc_.gamepadButtons() > 0;
        std::vector<Choice> devs;
        for (int d = 0; d < 5; ++d) devs.push_back(Choice{T(580 + d), d, d >= 2 && !pads});
        field(ui, "Control device", "", [&]() {
            int v = o["controlDevice"].integer();
            if (seg(ui, "device", devs, v)) setOpt("controlDevice", v);
        });
        {
            bool v = o["autoReverse"].truthy();
            if (check(ui, "autorev", "Automatic reverse", v)) {
                setOpt("autoReverse", v ? 1 : 0);
                svc_.setDriveOption("autoreverse", v ? 1 : 0);
            }
        }
        const int dev = o["controlDevice"].integer();
        if (dev == 0 || dev >= 2)
            field(ui, "Steering sensitivity", fmt2(o["steeringSensitivity"].num()), [&]() {
                float v = o["steeringSensitivity"].f();
                if (slider(ui, "steer", 0.5f, 2.0f, v)) setOpt("steeringSensitivity", v);
            });
        if (dev >= 2)
            field(ui, "Controller dead zone", fmt2(o["deadZone"].num()), [&]() {
                float v = o["deadZone"].f();
                if (slider(ui, "dead", 0, 0.33f, v)) setOpt("deadZone", v);
            });
        // Force feedback: only with force-feedback hardware (menus.md); the browser has none.
        if (dev >= 2 && svc_.controllerFeedback()) {
            bool ff = o["forceFeedback"].truthy();
            if (check(ui, "ff", "Force feedback", ff)) setOpt("forceFeedback", ff ? 1 : 0);
            if (ff) {
                field(ui, "Collision strength", fmt2(o["ffCollision"].num()), [&]() {
                    float v = o["ffCollision"].f();
                    if (slider(ui, "ffcol", 0, 2, v)) setOpt("ffCollision", v);
                });
                field(ui, "Road strength", fmt2(o["ffRoad"].num()), [&]() {
                    float v = o["ffRoad"].f();
                    if (slider(ui, "ffroad", 0, 2, v)) setOpt("ffRoad", v);
                });
            }
        }
        note(ui, std::string(host_.isBrowser() ? "The browser has no force feedback. " : "") +
                     "A game drives with this driver's controls for the device chosen here: Customise controls shows and changes "
                     "them, and Keys lists them.");
    } else {
        if (keysDev_ < 0) keysDev_ = o["controlDevice"].integer();
        std::vector<std::string> devs;
        for (int d = 0; d < 5; ++d) devs.push_back(T(580 + d));
        field(ui, "Device", "", [&]() {
            if (seg(ui, "kdev", choices(devs), keysDev_)) {
                bindWarn_.clear();
                rerender();
            }
        });
        const JValue& tb = o["bindings"];
        auto same = [&](int a, int b) {
            const JValue &x = tb[keysDev_ * kActions + a], &y = tb[keysDev_ * kActions + b];
            return x[0].integer() != 0 && y[0].integer() != 0 && x[1].integer() == y[1].integer() && x[2].integer() == y[2].integer();
        };
        Style wrap;
        wrap.column();
        wrap.gap = 2;
        wrap.maxH = ui.viewH() * 0.42f;
        wrap.scrollY = true;
        wrap.hit = true;
        ui.open("keyswrap", wrap);
        for (int a = 0; a < kActions; ++a) {
            const JValue& e = tb[keysDev_ * kActions + a];
            bool twice = false;
            for (int b = 0; b < kActions && !twice; ++b) twice = b != a && same(a, b);
            if (bindRow(ui, "b" + std::to_string(a), T(kActionText[a]), bindingName(e), e[0].integer() == 0, twice))
                startBind(keysDev_, a);
        }
        ui.close();
        if (!bindWarn_.empty()) msg(ui, bindWarn_, Msg::Warn);
        const int inUse = o["controlDevice"].integer();
        note(ui, "Choose an action, then press its key or button" + std::string(keysDev_ >= 2 ? " or move the axis" : "") +
                     " (Esc keeps it). A game drives with the table of the control device chosen under Controls" +
                     (inUse >= 0 && inUse < 5 ? " (now " + T(580 + inUse) + ")" : std::string()) +
                     "; the changes are written to the driver's file with Done.");
    }
    ui.close();
    beginRow(ui, Justify::End, 8, 14);
    if (!inRace && button(ui, "defaults", "Defaults", Btn::Ghost)) {
        const std::string t = optTab_;
        confirmBox("Defaults?",
                   t == "keys" ? "Put MM2's default bindings back for all five devices." : "Reset the " + t + " options to the defaults.",
                   "Reset", [this, t]() {
                       menu::optionsDefaults(t);
                       bindWarn_.clear();
                       rerender();
                   });
    }
    if (button(ui, "cancel", "Cancel", Btn::Ghost)) {
        bindWarn_.clear();
        const JValue r = parse(menu::optionsCancel());
        refreshState(&r);
        applyGraphics();
        rerender();
        if (inRace) showPauseMenu();
        else back();
    }
    if (button(ui, "done", "Done")) {
        bindWarn_.clear();
        const JValue r = parse(menu::optionsDone());
        refreshState(&r);
        rerender();
        if (inRace) showPauseMenu();
        else back();
    }
    endRow(ui);
    if (!inRace) sideCards_.push_back([this, &ui]() { driverCard(ui); });
}

}  // namespace fe
}  // namespace cc
