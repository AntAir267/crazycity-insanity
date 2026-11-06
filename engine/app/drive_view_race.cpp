// Drive mode's race modes (game/race.h): starting a race in the loaded city, the AI frame with
// the opponents between the yield-mark passes, the opponents and gates drawn, and the plain
// overlay's data for the page. The real HUD and menus come from the UI builder.
#include <algorithm>
#include <cmath>

#include "ai/opponents.h"
#include "ai/road_network.h"
#include "ai/route_search.h"
#include "app/drive_view.h"
#include "city/city.h"
#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "formats/pkg.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "platform/web_bridge.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"
#include "vfs/vfs.h"

namespace cc {

// The parts of a race session the drive view owns (game::RaceHost).
struct DriveView::RaceHostImpl : game::RaceHost {
    DriveView* v = nullptr;
    void worldReset() override {
        City* city = v->city_;
        phys::resetBumpRandom();  // the AI reset restarts the game generator
        if (!city) return;
        city->collision().resetProps();
        city->resetTraffic();  // the light cycle restarts, stop-sign queues empty
        // The player's tracked road, before the car is put back (driver-planner.md §16 step 3).
        if (v->race_ && v->session_ && city->data().network()) {
            const phys::Car& c = v->session_->car();
            const vec3 startBody = v->race_->startPoint() + c.tune().sim.centerOfGravity;
            v->tracking_.worldReset(*city->data().network(), city->data(), startBody, c.rotation().r[2], c.gearIndex() == 0);
        }
        v->oppPos_ = v->race_ ? v->race_->opponentPositions() : std::vector<vec3>();
        city->traffic().reset(v->trafficPlayers());  // also the cable cars (with no traffic too)
        // (The pedestrians reset in resetPlayer, once the car is back at its start.)
        if (city->police().active()) city->police().reset();
        city->resetLevelObjects();
    }
    void resetPlayer(const vec3& q, float theta) override {
        if (v->session_) v->session_->placeAt(q, theta);
        // The pedestrians' part of the AI reset (pedestrians.md, "The pool"): their generator
        // restarts and they fill the roads around the player car's reset position, so it runs once
        // the car is back at its start (nothing else of the reset depends on them).
        v->resetPeds();
    }
    void preRaceCamera() override {
        if (v->session_) v->session_->startPreRaceCamera();
    }
    bool preRaceCameraRunning() const override { return v->session_ && v->session_->cameras().preRaceRunning(); }
    void postRaceCamera() override {
        if (v->session_) v->session_->startPostRaceCamera();
    }
    void waterCamera() override {
        if (v->session_) v->session_->startWaterCamera();
    }
    bool copInSight() const override {
        return v->city_ && v->session_ && v->city_->police().copInSight(v->session_->car(), v->city_->collision());
    }
};

DriveView::DriveView() = default;
DriveView::~DriveView() = default;

std::string DriveView::startRace(const CarEntry& car, int paint, City* city, const CityEntry& entry, int modeNo, int raceNo,
                                 bool pro, int circuitOpponents, bool noDamage, EngineContext& ctx, float aspect, bool nextRace) {
    const float keptPedDensity = sessionPedDensity_;  // "next race" in Checkpoint and Blitz keeps it
    const RaceChoices choices = raceChoices_;  // for this start only
    raceChoices_ = RaceChoices();
    auto fail = [](const std::string& why) {
        JsonWriter j;
        j.beginObject();
        j.field("ok", false);
        j.field("why", why);
        j.endObject();
        return j.str();
    };
    if (!city || !city->data().network()) return fail("the city has no AI road network (no races)");
    const game::RaceMode mode = game::RaceMode(modeNo);
    if (mode != game::RaceMode::Checkpoint && mode != game::RaceMode::Circuit && mode != game::RaceMode::Blitz &&
        mode != game::RaceMode::CrashCourse)
        return fail("unknown race mode");
    if (raceNo < 0 || raceNo >= game::raceCount(entry, mode)) return fail("this city has no such race");
    // A Cruise drive (start() also stops any race) with the race's own files.
    ai::RoadNetwork* net = const_cast<ai::RoadNetwork*>(city->data().network());
    std::string r = start(car, paint, city, RepeatableStart, FreeFlyCamera(), ctx, aspect);
    if (!session_) return r;
    wall_ = ctx.time;
    net->setRace(*ctx.vfs, game::raceName(mode, raceNo), pro);
    tracking_ = ai::PlayerTracking();  // none when the player is created
    raceHost_ = std::make_unique<RaceHostImpl>();
    raceHost_->v = this;
    race_ = std::make_unique<game::RaceSession>();
    game::RaceSession::Setup st;
    st.vfs = ctx.vfs;
    st.assets = ctx.assets;
    st.city = const_cast<CityData*>(&city->data());
    st.net = net;
    st.world = &city->collision();
    st.cityEntry = entry;
    st.mode = mode;
    st.race = raceNo;
    st.professional = pro;
    st.circuitOpponents = circuitOpponents;
    st.timeOfDay = choices.timeOfDay;
    st.weather = choices.weather;
    st.laps = choices.laps;
    st.copDensity = choices.copDensity;
    st.pedDensity = choices.pedDensity;
    st.trafficDensity = choices.trafficDensity;
    st.playerCar = car.baseName;
    st.playerBound = session_->tune().bound;
    st.noDamage = noDamage;
    if (BytesPtr b = ctx.vfs->open("tune/" + car.baseName + ".info")) {
        const InfoFile info = parseInfoFile(std::string(b->begin(), b->end()));
        st.scoringBias = float(std::atof(info.get("ScoringBias").c_str()));
    }
    st.records = records_;
    st.host = raceHost_.get();
    st.events = [this](const game::RaceEvent& e) {
        raceEvents_.push_back(e.value || e.value2 ? strf("%s %d %d", e.name.c_str(), e.value, e.value2) : e.name);
        if (raceEvents_.size() > 12) raceEvents_.erase(raceEvents_.begin());
        if (audio_) audio_->raceEvent(e.name, e.value, e.value2);
        if (raceSink_) raceSink_(e);
    };
    std::string err;
    if (!race_->setup(st, session_->car(), err)) {
        logError("race: %s", err.c_str());
        race_.reset();
        raceHost_.reset();
        stop();
        return fail(err);
    }
    // The session's values: time of day and weather, cops, traffic (races.md).
    const game::SessionValues& v = race_->values();
    CityOptions opt = city->options();
    if (opt.timeOfDay != v.timeOfDay || opt.weather != v.weather) {
        opt.timeOfDay = v.timeOfDay;
        opt.weather = v.weather;
        city->setOptions(opt, ctx);
    }
    session_->car().setWeatherScale(v.weather == 3 ? (v.timeOfDay == 3 ? 0.75f : 0.8f) : 1.0f);
    audio_.reset();  // the Cruise drive's sound goes with its cops; the race's is made below
    city->startPoliceDensity(ctx, std::clamp(v.copDensity, 0.0f, 1.0f), carName_, session_->tune().bound);
    raceOpponents_ = circuitOpponents;
    trafficDensity_ = std::clamp(v.trafficDensity, 0.0f, 1.0f);
    city->traffic().setOpponentPositions(&oppPos_);
    // The pedestrian pool after the traffic pool (pedestrians.md, "The pool"): the race's Peds
    // (its settings row; the Crash Course's mmcrashdata row), kept on Checkpoint/Blitz "next race",
    // none in a Circuit race; the race aimap's model list if it has one. The race's first restart
    // (race_->start below) resets them around the start point. Set before the traffic: the cable
    // cars discard the pedestrian pool's creation draws (cable-cars.md).
    sessionPedDensity_ = (nextRace && (mode == game::RaceMode::Checkpoint || mode == game::RaceMode::Blitz))
                             ? keptPedDensity
                             : std::clamp(v.pedDensity, 0.0f, 1.0f);
    sessionCircuit_ = mode == game::RaceMode::Circuit;
    startTraffic(ctx);
    startPeds(ctx);
    raceNo_ = raceNo;
    startAudio(ctx);  // the race's sound (its mode's speech and one-shots, the opponents' cars)
    // Props for the opponents' planner.
    propOf_.assign(city->data().objects().size(), -1);
    for (size_t i = 0; i < city->collision().objects().size(); ++i) {
        const phys::CollisionObject& o = city->collision().objects()[i];
        if (o.kind == phys::ObjKind::Prop && o.cityObject >= 0 && size_t(o.cityObject) < propOf_.size())
            propOf_[size_t(o.cityObject)] = int(i);
    }
    // The gates' models (waypoints.md, "Drawn as").
    const char* gates[2] = {"pt_check", "pt_finish"};
    for (int k = 0; k < 2; ++k) {
        gatePkg_[k] = ctx.assets->package(gates[k], false);
        gateMesh_[k] = nullptr;
        if (gatePkg_[k]) {
            const PartTable t = takeParts(*gatePkg_[k], {""});
            gateMesh_[k] = t.parts[0].at(LodH);
        }
    }
    session_->setRaceHooks({[this](float F, const ControlEvents& ev) {
        if (race_) race_->frame(F, ev.nextWaypoint, ev.prevWaypoint, wall_);
    }});
    race_->start();
    raceHudTimer_ = 1.0f;
    logInfo("race: %s %s %d (%s) '%s', %d opponents, cops %.0f, traffic %.2f, pedestrians %.2f (%d)", entry.raceDir.c_str(),
            game::modeTitle(mode), raceNo, pro ? "professional" : "amateur", race_->raceTitle().c_str(),
            int(race_->opponents().opponents().size()), double(v.copDensity), double(v.trafficDensity),
            double(sessionPedDensity_), city->pedestrians().count());
    JsonWriter j;
    j.beginObject();
    j.field("ok", true);
    j.field("title", latin1ToUtf8(race_->raceTitle()));
    j.field("opponents", int(race_->opponents().opponents().size()));
    j.field("timeOfDay", v.timeOfDay);
    j.field("weather", v.weather);
    j.field("pedestrians", city->pedestrians().count());
    j.endObject();
    return j.str();
}

void DriveView::setAutopilot(bool on) {
    if (!session_ || !city_) return;
    if (!on) {
        session_->setControlOverride(nullptr);
        autoDriver_.reset();
        return;
    }
    phys::Car& car = session_->car();
    const phys::Bound* b = car.bound();
    autoDriver_ = std::make_unique<ai::Driver>();
    autoDriver_->create(&car, carName_, b ? b->bmin : vec3(-1, 0, -2), b ? b->bmax : vec3(1, 1, 2), false);
    ai::DriverEnv env;
    env.net = const_cast<ai::RoadNetwork*>(city_->data().network());
    env.city = &city_->data();
    autoDriver_->reset(env);
    session_->setControlOverride([this](float F, phys::DriverControls& dc) {
        if (!autoDriver_ || !race_ || !city_) return;
        ai::RoadNetwork* net = const_cast<ai::RoadNetwork*>(city_->data().network());
        phys::Car& c = session_->car();
        ai::DriverEnv env;
        env.net = net;
        env.city = &city_->data();
        env.traffic = city_->traffic().active() ? &city_->traffic() : nullptr;
        env.world = &city_->collision();
        env.propOfCityObject = &propOf_;
        env.now = clock_;
        const game::WaypointList& L = race_->waypoints();
        const ai::DriverMode m = autoDriver_->mode();
        if (L.size() > 0 && (m == ai::DriverMode::Drive || m == ai::DriverMode::Recover)) {
            ai::RouteQuery q;
            q.start = c.body().p;
            q.startZ = c.rotation().r[2];
            q.startRoom = c.collision().room;
            q.goal = L.targetPosition();
            q.goalRoom = city_->data().findRoom(q.goal, 0);
            std::vector<int> route;
            ai::routeSearch(*net, q, route);
            ai::DriverOrders o;
            o.route = route;
            o.D = q.goal;
            o.s1 = 25.0f;
            o.sw = {false, true, true, false, false, false};
            o.kc = 1.3f;
            o.kp = 75.0f;
            autoDriver_->giveOrders(o, env);
        }
        autoDriver_->frame(F, env);
        const phys::CarInputs& in = autoDriver_->carInputs();
        dc.accelerate = in.throttle;
        dc.brake = in.brake;
        dc.steer = in.steer;
        dc.handbrake = in.handbrake;
    });
}

int DriveView::raceMode() const { return race_ ? int(race_->mode()) : 0; }

void DriveView::restartRace() {
    if (race_) race_->restart();
}

DriveView::NextRace DriveView::nextRace() const {
    NextRace n;
    if (!race_ || !race_->nextRaceOffered()) return n;
    n.offered = true;
    n.mode = int(race_->mode());
    n.race = race_->nextRaceNumber();
    n.pro = race_->professional();
    n.opponents = raceOpponents_;
    n.car = race_->nextRaceCar();
    return n;
}

void DriveView::endRace() {
    if (session_) session_->setControlOverride(nullptr);
    autoDriver_.reset();
    if (!race_) return;
    if (session_) session_->setRaceHooks({});
    if (city_) city_->traffic().setOpponentPositions(nullptr);
    race_.reset();
    raceHost_.reset();
    oppPos_.clear();
    raceEvents_.clear();
    pageEvent("race", "{\"active\":false}");
}

// The AI frame of a race (traffic.md "Frame order" 1.4; police.md "One frame of police"): the
// yield marks of every AI-driven car cleared, the opponents in index order, every cop, the marks
// set again; then the cars' inputs.
void DriveView::raceAiFrame(float F) {
    if (!race_ || !city_ || !session_) return;
    ai::TrafficSystem& ts = city_->traffic();
    ai::PoliceSystem& ps = city_->police();
    ai::OpponentSystem& os = race_->opponents();
    ai::RoadNetwork* net = const_cast<ai::RoadNetwork*>(city_->data().network());
    const phys::Car& pc = session_->car();
    ai::DriverEnv env;
    env.net = net;
    env.city = &city_->data();
    env.traffic = ts.active() ? &ts : nullptr;
    env.world = &city_->collision();
    env.propOfCityObject = &propOf_;
    // The player as the planner sees it: the road it is tracked on.
    env.players.push_back(ai::DriverPlayer{&pc, tracking_.road});
    env.opponents = os.driverView();
    env.now = wall_;  // real time (driver.md, the regenerate clock)
    env.firstPlayerReversing = pc.gearIndex() == 0;
    std::vector<ai::PolicePlayer> targets;
    for (const ai::Opponent& o : os.opponents()) {
        ai::PolicePlayer t;
        t.car = o.car.get();
        t.room = o.car->collision().room;
        t.boxMin = o.boxMin;
        t.boxMax = o.boxMax;
        targets.push_back(t);
    }
    os.yieldMarks(false);
    if (ps.active()) ps.clearMarks();
    os.update(F, env, {&pc});
    if (ps.active()) ps.updateCops(F, policePlayers(), env.traffic, clock_, targets, env.opponents);
    os.yieldMarks(true);
    if (ps.active()) ps.setMarks();
    os.applyInputs();
    if (ps.active()) ps.applyInputs();
    oppPos_ = race_->opponentPositions();
}

namespace {
mat4 carWorldOf(const phys::Car& c) {
    const phys::M3& R = c.rotation();
    const vec3 o = c.modelOrigin();
    mat4 m = mat4::identity();
    for (int i = 0; i < 3; ++i) {
        m.m[i * 4 + 0] = R.r[i].x;
        m.m[i * 4 + 1] = R.r[i].y;
        m.m[i * 4 + 2] = R.r[i].z;
    }
    m.m[12] = o.x;
    m.m[13] = o.y;
    m.m[14] = o.z;
    return m;
}
}  // namespace

void DriveView::addOpponentCars(std::vector<CityCar>& cars) const {
    if (!race_) return;
    for (const ai::Opponent& o : race_->opponents().opponents()) {
        if (!o.model) continue;
        CityCar cc;
        cc.model = o.model.get();
        cc.world = carWorldOf(*o.car);
        cc.paint = o.paint;
        cc.room = o.car->collision().room;
        cc.wheelPose = carWheelPose(o.car.get(), o.model.get());
        cc.braking = o.car->inputs().brake > 0.0f;
        cc.reverse = o.car->gearIndex() == 0;
        cars.push_back(cc);
    }
}

void DriveView::drawGates(EngineContext& ctx) {
    if (!race_) return;
    const game::WaypointList& L = race_->waypoints();
    if (L.disabled() || L.size() == 0) return;
    Renderer& r = *ctx.renderer;
    const bool crash = race_->mode() == game::RaceMode::CrashCourse;
    const int n = L.size();
    for (int i = 0; i < n; ++i) {
        if (!L.shown(i)) continue;
        // pt_finish: a Checkpoint race's last waypoint, a Circuit's waypoint 0; else pt_check.
        bool finish = false;
        if (race_->mode() == game::RaceMode::Checkpoint) finish = i == n - 1;
        else if (race_->mode() == game::RaceMode::Circuit) finish = i == 0;
        const int k = finish ? 1 : 0;
        if (!gateMesh_[k] || !gatePkg_[k]) continue;
        const Waypoint& w = L.point(i);
        const float theta = w.heading * -0.017453292f;
        ModelDraw md;
        md.gpu = ctx.meshes->get(*gateMesh_[k]);
        md.pkg = gatePkg_[k].get();
        md.materialSet = gatePkg_[k]->setCount > 1 && crash ? 1 : 0;
        md.inlineBlend = true;
        md.user = finish ? "pt_finish" : "pt_check";
        md.model = mat4::translate(vec3(w.pos.x, w.pos.y + 3.75f, w.pos.z)) * mat4::rotateY(theta) *
                   mat4::scale(vec3(w.radius, 7.5f, w.radius));
        r.drawModel(md, *ctx.textures);
    }
    r.flush();
}

std::string DriveView::raceJson() const {
    JsonWriter j;
    j.beginObject();
    if (!race_ || !session_) {
        j.field("active", false);
        j.endObject();
        return j.str();
    }
    const game::RaceSession& R = *race_;
    const game::WaypointList& L = R.waypoints();
    j.field("active", true);
    j.field("mode", int(R.mode()));
    j.field("modeName", std::string(game::modeTitle(R.mode())));
    j.field("race", R.raceNumber());
    j.field("title", latin1ToUtf8(R.raceTitle()));
    j.field("pro", R.professional());
    j.field("state", R.state());
    j.field("clock", game::formatTimer(R.raceClock()));
    j.field("clockS", double(R.raceClock()));
    if (R.countdownShown()) j.field("countdown", game::formatTimer(R.countdown()));
    j.field("warning", R.timerWarning());
    {
        // Gates passed of the list's gates (Circuit: in the current lap; the finish counts when done).
        const int total = R.mode() == game::RaceMode::Circuit ? L.size() : std::max(0, L.size() - 1);
        int passed = 0;
        for (int i = 0; i < L.size(); ++i)
            if (L.passed(i) && (i != 0 || R.mode() == game::RaceMode::Circuit)) ++passed;
        if (L.done()) passed = total;
        j.field("gates", strf("%d/%d", passed, total));
    }
    j.field("racers", R.racers());
    j.field("place", R.playerPlace());
    if (R.mode() == game::RaceMode::Circuit) {
        j.field("lap", strf("%d/%d", std::min(L.lap() + 1, L.laps()), L.laps()));
        j.field("lapClock", game::formatTimer(R.lapClock()));
        j.key("laps").beginArray();
        for (float t : R.lapTimes()) j.value(game::formatRaceTime(t));
        j.endArray();
    }
    if (R.mode() == game::RaceMode::CrashCourse) {
        j.field("event", strf("%d/%d", R.crashEvent() + 1, R.crashEventCount()));
        j.field("kind", R.crashKind());
    }
    // The arrow: distance and bearing to the target (degrees, 0 = straight ahead, + right).
    if (L.arrowOn()) {
        const phys::Car& c = session_->car();
        const vec3 d = L.targetPosition() - c.body().p;
        const vec3 f = c.forward();
        const vec3 X = c.rotation().r[0];
        j.field("target", L.target());
        j.field("targetDist", double(std::sqrt(d.x * d.x + d.z * d.z)));
        j.field("targetBearing", double(std::atan2(d.x * X.x + d.z * X.z, d.x * f.x + d.z * f.z) * 57.29578));
    }
    const game::HudMessage& m = R.message();
    if (m.text != 0 || !m.secondTime.empty()) {
        std::string text = m.text ? game::fallbackText(m.text) : std::string();
        if (m.text == 232) text = strf("Keep above %s mph", m.arg.c_str());
        j.field("msgId", m.text);
        j.field("msg", text);
        j.field("msgUpper", m.upper);
        if (m.second) j.field("msg2", game::fallbackText(m.second));
        else if (!m.secondTime.empty()) j.field("msg2", m.secondTime);
    }
    j.key("opponents").beginArray();
    for (size_t i = 0; i < R.opponents().opponents().size(); ++i) {
        const ai::Opponent& o = R.opponents().opponents()[i];
        j.beginObject();
        j.field("i", int(i));
        j.field("car", o.row.car);
        j.field("place", R.opponentPlace(int(i)));
        j.field("gates", R.opponentGates(int(i)) - 1);
        j.field("finished", R.opponentFinished(int(i)));
        j.field("state", int(o.state));
        j.field("mode", int(o.driver.mode()));
        j.field("w", o.driver.waypoint());
        j.field("n", int(o.route.route.size()));
        j.field("lap", o.driver.lap());
        j.field("v", double(o.car->speed()));
        j.endObject();
    }
    j.endArray();
    j.field("endMenu", R.endMenuOpen());
    j.field("lost", R.lost());
    j.field("resultsShown", R.resultsShown());
    if (R.resultsShown()) {
        j.field("score", R.score());
        j.field("nextRace", R.nextRaceOffered());
        if (R.nextRaceOffered() && !R.nextRaceCar().empty()) j.field("nextCar", R.nextRaceCar());
        j.key("results").beginArray();
        for (const game::ResultRow& row : R.results()) {
            j.beginObject();
            j.field("place", row.place);
            j.field("name", row.racer < 0 ? game::fallbackText(3) : game::fallbackText(13 + row.racer));
            j.field("time", row.passMark ? game::fallbackText(651) : game::formatRaceTime(row.time));
            j.endObject();
        }
        j.endArray();
    }
    j.key("events").beginArray();
    for (const std::string& e : raceEvents_) j.value(e);
    j.endArray();
    j.endObject();
    return j.str();
}

}  // namespace cc
