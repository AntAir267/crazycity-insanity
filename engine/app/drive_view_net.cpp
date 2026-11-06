// Drive mode's network games (net/net_game.h; spec/game/multiplayer.md): the world of a network
// game (no traffic, police, opponents or cable cars), the game hooked into the drive's frame, the
// other players' cars and the Cops and Robbers objects drawn, the HUD fed, and the plain
// scoreboard's data for the page. The real HUD and menus come from the UI builder.
#include <algorithm>
#include <cmath>

#include "app/drive_view.h"
#include "city/city.h"
#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/pkg.h"
#include "game/race.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "net/net_game.h"
#include "net/session.h"
#include "net/texts.h"
#include "physics/trailer.h"
#include "platform/web_bridge.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"

namespace cc {

namespace {

mat4 bodyWorld(const phys::Car& c) {
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

// The HUD map's mark of each remote slot (multiplayer.md 1.5), slots 6 and 7 with MM2's alphas.
const uint8_t kSlotColours[8][4] = {{239, 0, 0, 255},   {255, 255, 0, 255}, {255, 90, 0, 255},    {180, 0, 255, 255},
                                    {0, 255, 255, 255}, {255, 3, 144, 255}, {240, 81, 236, 64}, {100, 117, 104, 109}};

}  // namespace

// What the network game asks of this drive (net::NetWorld).
struct DriveView::NetHostImpl : net::NetWorld {
    DriveView* v = nullptr;
    EngineContext* ctx = nullptr;
    phys::Car& localCar() override { return v->session_->car(); }
    const CityData* city() const override { return v->city_ ? &v->city_->data() : nullptr; }
    phys::CollisionWorld* collision() override { return v->city_ ? &v->city_->collision() : &v->flat_; }
    void placeLocal(const vec3& q, float theta) override {
        v->session_->placeAt(q, theta);
        if (v->audio_) v->audio_->playerCarReset();
    }
    void postRaceCamera() override { v->session_->startPostRaceCamera(); }
    bool localHorn() const override { return v->session_->hornHeld() && !v->playerLights_.any(); }
    bool localSiren() const override { return v->playerLights_.any() && v->playerLights_.on; }
    void remoteCarAdded(net::RemoteCar& rc) override {
        auto view = std::make_unique<NetCarView>();
        view->rc = &rc;
        view->model = std::make_unique<VehicleModel>();
        if (!loadVehicle(*ctx->assets, rc.carName, VehicleKind::Player, *view->model)) {
            logWarn("network: the model of %s cannot be loaded", rc.carName.c_str());
            view->model.reset();
        } else {
            const int set = paintSetFor(rc.paint, view->model->setCount());
            rc.car.setPaintSet(set);
            view->lights.setup(*view->model, set);
            if (v->effects_) {
                view->fx = std::make_unique<fx::CarFx>();
                view->fx->setup(v->effects_.get(), v->effects_->images(), *view->model, set, rc.carName, rc.car);
                rc.car.setEffects(view->fx.get());
            }
            if (!rc.hasTrailer) view->model->trailer.reset();
        }
        if (v->audio_) v->audio_->addNetworkCar(rc.slot, &rc.car, rc.carName, view->lights.any());
        v->netCars_.push_back(std::move(view));
    }
    void remoteCarRemoved(net::RemoteCar& rc) override {
        if (v->audio_) v->audio_->removeNetworkCar(rc.slot);
        rc.car.setEffects(nullptr);
        auto& list = v->netCars_;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const std::unique_ptr<NetCarView>& c) { return c->rc == &rc; }),
                   list.end());
    }
    void event(const char* name) override {
        if (!v->audio_) return;
        const std::string n = name;
        if (v->audio_->oneShots().has(n)) v->audio_->oneShots().play(n);
    }
};

std::string DriveView::startNetGame(const CarEntry& car, int paint, City* city, const CityEntry& entry, net::Session& session,
                                    EngineContext& ctx, float aspect) {
    auto fail = [](const std::string& why) {
        JsonWriter j;
        j.beginObject();
        j.field("ok", false);
        j.field("why", why);
        j.endObject();
        return j.str();
    };
    net::NetGame* game = session.game();
    if (!game) return fail("no game is running");
    if (!city) return fail("the game's city is not loaded");
    const net::Settings& st = game->settings();
    // The world as 1.4 builds it: no traffic, police, opponents or cable cars; the session's
    // pedestrians; trailers only in the race modes.
    netNoAi_ = true;
    pedDensity_ = std::clamp(st.pedDensity, 0.0f, 1.0f);
    CityOptions opt = city->options();
    if (opt.timeOfDay != st.timeOfDay || opt.weather != st.weather) {
        opt.timeOfDay = st.timeOfDay & 3;
        opt.weather = st.weather & 3;
        city->setOptions(opt, ctx);
    }
    if (city->data().network()) {
        ai::RoadNetwork* rn = const_cast<ai::RoadNetwork*>(city->data().network());
        if (rn->raceName() != "roam") rn->setRace(*ctx.vfs, "roam", false);
    }
    trailerAllowedNext_ = net::isRaceMode(st.mode);
    audioMode_ = st.mode == net::ModeCruise ? 5 : st.mode;  // the one-shot bank (speech.md)
    std::string r = start(car, paint, city, RepeatableStart, FreeFlyCamera(), ctx, aspect);
    trailerAllowedNext_ = true;
    audioMode_ = 0;
    if (!session_) {
        netNoAi_ = false;
        return r;
    }
    session_->car().setWeatherScale(st.weather == 3 ? (st.timeOfDay == 3 ? 0.75f : 0.8f) : 1.0f);
    netSession_ = &session;
    net_ = game;
    netHost_ = std::make_shared<NetHostImpl>();
    netHost_->v = this;
    netHost_->ctx = &ctx;
    // The Cops and Robbers objects (5.3).
    const char* objs[5] = {"wpobj_gold", "pt_hideout", "pt_bank", "pt_red", "pt_blue"};
    for (int k = 0; k < 5; ++k) {
        netPkg_[k] = ctx.assets->package(objs[k], false);
        netMesh_[k] = nullptr;
        if (netPkg_[k]) {
            const PartTable t = takeParts(*netPkg_[k], {""});
            netMesh_[k] = t.parts[0].at(LodH);
        }
    }
    // The race gates' models (waypoints.md, "Drawn as").
    const char* gates[2] = {"pt_check", "pt_finish"};
    for (int k = 0; k < 2; ++k) {
        gatePkg_[k] = ctx.assets->package(gates[k], false);
        gateMesh_[k] = nullptr;
        if (gatePkg_[k]) {
            const PartTable t = takeParts(*gatePkg_[k], {""});
            gateMesh_[k] = t.parts[0].at(LodH);
        }
    }
    // The game's update runs where a race's does (before the car's control step), so the drive
    // treats it as a race: the game owns wrecks, water and falling through.
    session_->setRaceHooks({[this](float F, const ControlEvents& ev) {
        (void)ev;
        if (net_) net_->frameStart(F);
    }});
    hud_.reset(st.mode);
    net_->attach(netHost_.get());
    netHudTimer_ = 1.0f;
    netChat_.clear();
    logInfo("network game: mode %d in %s, %d players", st.mode, entry.raceDir.c_str(), session.playerCount());
    JsonWriter j;
    j.beginObject();
    j.field("ok", true);
    j.field("mode", st.mode);
    j.endObject();
    return j.str();
}

void DriveView::endNetGame() {
    if (net_) net_->detach();
    net_ = nullptr;
    netSession_ = nullptr;
    netCars_.clear();
    netHost_.reset();
    for (auto& p : netPkg_) p.reset();
    for (auto& m : netMesh_) m = nullptr;
    netChat_.clear();
}

void DriveView::netNotices(const std::vector<net::Notice>& notices) {
    for (const net::Notice& n : notices) {
        std::string text = n.text ? (text_ ? text_(n.text) : std::string()) : std::string();
        if (n.text && text.empty()) text = net::netText(n.text);
        if (n.kind == net::Notice::Chat) {
            // A text: one line "name text"; a player's own words: "name: line".
            const std::string line = n.text ? net::joinWords(net::joinWords(n.name, text), n.line)
                                            : (n.name.empty() ? n.line : n.name + ": " + n.line);
            netChat_.push_back({line, 15.0f});
            if (netChat_.size() > 5) netChat_.erase(netChat_.begin());
            if (n.sound && audio_ && audio_->oneShots().has("Messagenote")) audio_->oneShots().play("Messagenote");
        } else if (n.twoLines) {
            // The name as the primary message, "text line" as the secondary line.
            hud_.postPrimary(-1, n.name, n.seconds, n.upper);
            hud_.postSecondary(-1, net::joinWords(text, n.line));
        } else {
            hud_.postPrimary(-1, net::joinWords(net::joinWords(n.name, text), n.line), n.seconds, n.upper);
        }
    }
}

// Every frame of a network game: the HUD's clocks, panel, arrow and map items from the game.
void DriveView::netFrameHud(float dt) {
    if (!net_) return;
    for (auto& c : netChat_) c.second -= dt;
    netChat_.erase(std::remove_if(netChat_.begin(), netChat_.end(), [](const std::pair<std::string, float>& c) { return c.second <= 0.0f; }),
                   netChat_.end());
    const int mode = net_->mode();
    if (net::isRaceMode(mode)) {
        hud_.raceClock.value = net_->raceClock();
        hud_.countdown.value = net_->countdown();
        hud_.countdown.running = mode == net::ModeBlitz && net_->raceState() == net::NetGame::Racing;
        const game::WaypointList& L = net_->waypoints();
        const int total = mode == net::ModeCircuit ? L.size() : std::max(0, L.size() - 1);
        int passed = 0;
        for (int i = 0; i < L.size(); ++i)
            if (L.passed(i) && (i != 0 || mode == net::ModeCircuit)) ++passed;
        if (L.done()) passed = total;
        hud_.setCheckpoints(passed, total);
        hud_.setPlace(net_->place(), net_->racers());  // kept once the local car finished
        if (mode == net::ModeCircuit) hud_.setLaps(std::min(L.lap() + 1, L.laps()), L.laps());
    } else if (mode == net::ModeCops) {
        const bool timed = net_->copsTimed();
        hud_.countdown.value = timed ? (netSession_ && netSession_->isHost() ? net_->copsCountdown() : net_->hostClock()) : 0.0f;
        hud_.countdown.running = timed;
    }
    vec3 target;
    hud_.setArrowTarget(net_->arrowTarget(target), target);
    // Name tags (hud.md: each player's name over their car within 300 m of the camera): drawn by
    // the page from netJson. Map markers: below (netMapItems).
    hud_.mapCars.clear();
    hud_.mapDots.clear();
}

void DriveView::netMapItems(std::vector<HudMapView::CarMarker>& cars, std::vector<HudMapView::Dot>& dots) const {
    if (!net_) return;
    const int sub = net_->settings().subMode;
    for (const auto& v : netCars_) {
        if (!v->rc || !v->rc->shown) continue;
        // A race hides a finished player's mark (5.2).
        if (net::isRaceMode(net_->mode()) && net_->playerFinished(v->rc->id)) continue;
        HudMapView::CarMarker m;
        m.pos = v->rc->car.body().p;
        m.forward = v->rc->car.forward();
        for (int k = 0; k < 3; ++k) m.rgb[k] = kSlotColours[v->rc->slot & 7][k];
        m.a = kSlotColours[v->rc->slot & 7][3];
        cars.push_back(m);
    }
    if (net_->mode() == net::ModeCops && net_->basesPlaced()) {
        if (net_->goldShown()) dots.push_back({net_->goldPoint(), 6});
        dots.push_back({net_->bankPoint(), sub == net::RobberTeams ? 1 : 7});
        dots.push_back({net_->hideoutPoint(), sub == net::RobberTeams ? 0 : 8});
    }
    if (net::isRaceMode(net_->mode())) {
        const game::WaypointList& L = net_->waypoints();
        const int n = L.size();
        for (int i = 0; i < n; ++i) {
            const Waypoint& w = L.point(i);
            const vec3 p(w.pos.x, std::max(w.pos.y, 0.0f) + 10.0f, w.pos.z);
            if (net_->mode() == net::ModeCircuit) {
                dots.push_back({p, i == 0 ? 5 : L.passed(i) ? 3 : (i == L.target() ? 4 : 2)});
            } else if (i > 0) {
                if (L.passed(i)) continue;
                if (i == n - 1 && net_->mode() == net::ModeCheckpoint) {
                    if (L.count() == n - 1) dots.push_back({p, 5});
                    continue;
                }
                dots.push_back({p, i == L.target() ? 4 : 2});
            }
        }
    }
}

void DriveView::addNetCars(std::vector<CityCar>& cars) const {
    if (!net_) return;
    for (const auto& v : netCars_) {
        net::RemoteCar* rc = v->rc;
        if (!rc || !rc->shown || !v->model) continue;
        CityCar cc;
        cc.model = v->model.get();
        cc.world = bodyWorld(rc->car);
        cc.paint = rc->paint;
        cc.room = rc->car.collision().room;
        cc.wheelPose = carWheelPose(&rc->car, v->model.get());
        cc.braking = rc->car.inputs().brake > 0.0f;
        cc.reverse = rc->car.gearIndex() == 0;
        cc.lightBar = v->lights.any() ? &v->lights : nullptr;
        cc.fx = v->fx.get();
        if (cc.fx) {
            cc.fx->refresh(rc->car);
            cc.wheelPose = carWheelPose(&rc->car, v->model.get(), cc.fx->wheelTilts());
        }
        cars.push_back(cc);
        const phys::Trailer* t = rc->car.trailer();
        if (t && v->model->trailer) {
            CityCar tc;
            tc.model = v->model->trailer.get();
            const phys::RigidBody& b = t->body();
            tc.world = mat4::identity();
            for (int i = 0; i < 3; ++i) {
                tc.world.m[i * 4 + 0] = b.R.r[i].x;
                tc.world.m[i * 4 + 1] = b.R.r[i].y;
                tc.world.m[i * 4 + 2] = b.R.r[i].z;
            }
            tc.world.m[12] = b.p.x;
            tc.world.m[13] = b.p.y;
            tc.world.m[14] = b.p.z;
            tc.paint = paintSetFor(rc->paint, v->model->setCount());
            tc.room = t->room();
            tc.braking = rc->car.inputs().brake > 0.1f;
            cars.push_back(tc);
        }
    }
}

void DriveView::drawNetObjects(EngineContext& ctx) {
    if (!net_) return;
    Renderer& r = *ctx.renderer;
    if (net::isRaceMode(net_->mode())) {
        // The race's gates as in single player (waypoints.md, "Drawn as"): shown gates only.
        const game::WaypointList& L = net_->waypoints();
        if (L.disabled() || L.size() == 0) return;
        const int n = L.size();
        for (int i = 0; i < n; ++i) {
            if (!L.shown(i)) continue;
            bool finish = false;
            if (net_->mode() == net::ModeCheckpoint) finish = i == n - 1;
            else if (net_->mode() == net::ModeCircuit) finish = i == 0;
            const int k = finish ? 1 : 0;
            if (!gateMesh_[k] || !gatePkg_[k]) continue;
            const Waypoint& w = L.point(i);
            ModelDraw md;
            md.gpu = ctx.meshes->get(*gateMesh_[k]);
            md.pkg = gatePkg_[k].get();
            md.materialSet = 0;
            md.inlineBlend = true;
            md.user = finish ? "pt_finish" : "pt_check";
            md.model = mat4::translate(vec3(w.pos.x, w.pos.y + 3.75f, w.pos.z)) * mat4::rotateY(w.heading * -0.017453292f) *
                       mat4::scale(vec3(w.radius, 7.5f, w.radius));
            r.drawModel(md, *ctx.textures);
        }
        r.flush();
        return;
    }
    if (net_->mode() != net::ModeCops || !net_->basesPlaced()) return;
    const int sub = net_->settings().subMode;
    auto draw = [&](int k, const mat4& model, bool unlit) {
        if (!netMesh_[k] || !netPkg_[k]) return;
        ModelDraw md;
        md.gpu = ctx.meshes->get(*netMesh_[k]);
        md.pkg = netPkg_[k].get();
        md.materialSet = 0;
        md.inlineBlend = true;  // as the gates (waypoints.md, "Drawn as")
        md.flags = unlit ? DrawUnlit : 0;
        md.user = k == 0 ? "wpobj_gold" : "base";
        md.model = model;
        r.drawModel(md, *ctx.textures);
    };
    // The gold (5.3 "Drawing"): at its drawn point (its point + (0, 1.5, 0); a carrier's gold's
    // point follows the carrier + (0, 2, 0)), its own size, lit, turned about +Y by 3 × game time.
    if (net_->goldShown()) {
        const vec3 g = net_->goldPoint();
        const float a = float(std::fmod(ctx.time * 3.0, 6.283185307179586));
        draw(0, mat4::translate(vec3(g.x, g.y + 1.5f, g.z)) * mat4::rotateY(a), false);
    }
    // The bases: camera-facing billboards at their drawn point (point + (0, 3.75, 0)), the camera's
    // own rotation, scaled (12, 7.5, 12) before it, unlit. Team 1: hideout (sub-modes 0, 1) / red
    // (2); team 0: bank / blue.
    const CamPose& cam = session_->cameras().frame().pose;
    auto billboard = [&](const vec3& at) {
        mat4 m = mat4::identity();
        const vec3 X = cam.X * 12.0f, Y = cam.Y * 7.5f, Z = cam.Z * 12.0f;
        m.m[0] = X.x, m.m[1] = X.y, m.m[2] = X.z;
        m.m[4] = Y.x, m.m[5] = Y.y, m.m[6] = Y.z;
        m.m[8] = Z.x, m.m[9] = Z.y, m.m[10] = Z.z;
        m.m[12] = at.x, m.m[13] = at.y, m.m[14] = at.z;
        return m;
    };
    const vec3 h = net_->hideoutPoint(), b = net_->bankPoint();
    draw(sub == net::RobberTeams ? 3 : 1, billboard(vec3(h.x, h.y + 3.75f, h.z)), true);
    draw(sub == net::RobberTeams ? 4 : 2, billboard(vec3(b.x, b.y + 3.75f, b.z)), true);
    r.flush();
}

std::string DriveView::netJson(int width, int height) const {
    JsonWriter j;
    j.beginObject();
    if (!net_ || !session_ || !netSession_) {
        j.field("active", false);
        j.endObject();
        return j.str();
    }
    const net::NetGame& g = *net_;
    const net::Session& s = *netSession_;
    const int mode = g.mode();
    j.field("active", true);
    j.field("gameTime", clock_);  // the sum of frame times (the page's hidden-page catch-up)
    j.field("mode", mode);
    j.field("host", s.isHost());
    j.field("self", (long long)(s.self()));
    j.field("state", g.raceState());
    j.field("waiting", g.waitCount());
    j.field("playing", g.playing());
    j.field("penalty", g.penalty());
    if (net::isRaceMode(mode)) {
        j.field("clock", game::formatTimer(g.raceClock()));
        if (g.countdownShown()) j.field("countdown", game::formatTimer(g.countdown()));
        j.field("place", g.place());
        j.field("racers", g.racers());
        j.field("finished", g.finished());
        if (g.finishWindow() >= 0.0f && !g.resultsShown()) j.field("window", double(g.finishWindow()));
    }
    if (mode == net::ModeCops) {
        j.field("subMode", g.settings().subMode);
        j.field("points", g.points());
        j.field("team", g.localTeam());
        j.field("carrying", g.localCarries());
        j.field("carrier", (long long)(g.goldCarrier()));
        j.field("gameOver", g.gameOver());
        if (g.copsTimed()) j.field("timeLeft", game::formatTimer(s.isHost() ? g.copsCountdown() : g.hostClock()));
        if (g.settings().subMode != net::FreeForAll) {
            j.field("team0", g.teamPoints(0));
            j.field("team1", g.teamPoints(1));
        }
        const vec3 gp = g.goldPoint();
        const vec3 me = session_->car().body().p;
        j.field("goldDist", double(std::sqrt((gp.x - me.x) * (gp.x - me.x) + (gp.z - me.z) * (gp.z - me.z))));
        auto point = [&](const char* k, const vec3& p) {
            j.key(k).beginArray().value(double(p.x)).value(double(p.y)).value(double(p.z)).endArray();
        };
        point("gold", gp);
        point("bank", g.bankPoint());
        point("hideout", g.hideoutPoint());
        j.field("goldShown", g.goldShown());
        int meshes = 0;
        for (const Mesh* m : netMesh_)
            if (m) ++meshes;
        j.field("objectMeshes", meshes);
    }
    j.field("canRestart", g.canRestart());
    j.field("resultsShown", g.resultsShown());
    // The players: the local one, then the remote cars in slot order.
    auto nameOf = [&](net::PlayerId id) { return latin1ToUtf8(s.nameOf(id)); };
    j.key("players").beginArray();
    j.beginObject();
    j.field("id", (long long)(s.self()));
    j.field("name", nameOf(s.self()));
    j.field("local", true);
    j.field("score", mode == net::ModeCops ? g.points() : (net::isRaceMode(mode) ? g.waypoints().count() : 0));
    j.field("team", g.localTeam());
    j.endObject();
    for (const auto& v : netCars_) {
        if (!v->rc) continue;
        j.beginObject();
        j.field("id", (long long)(v->rc->id));
        j.field("name", nameOf(v->rc->id));
        j.field("slot", v->rc->slot);
        j.field("shown", v->rc->shown);
        j.field("score", int(v->rc->score()));
        j.field("team", g.teamOf(v->rc->id));
        j.field("car", v->rc->carName);
        j.endObject();
    }
    j.endArray();
    if (g.resultsShown()) {
        j.key("results").beginArray();
        int place = 0;
        for (const net::NetResult& r : g.resultsList()) {
            j.beginObject();
            if (r.team >= 0) {
                j.field("team", r.team);
                j.field("teamText", r.teamText);
                j.field("name", text_ ? text_(r.teamText) : std::string());
                if (!text_ || text_(r.teamText).empty()) j.field("fallback", net::netText(r.teamText));
                j.field("points", r.points);
            } else {
                j.field("place", ++place);
                j.field("name", nameOf(r.id));
                if (net::isRaceMode(mode)) j.field("time", r.time >= 86400.0f ? std::string("DNF") : game::formatRaceTime(r.time));
                else j.field("points", r.points);
            }
            j.endObject();
        }
        j.endArray();
    }
    // Name tags: each other player's name over their car within 300 m of the camera, as screen
    // fractions (top-left origin), projected as render() draws the 3D view: the whole screen (or
    // the wide view's letterbox rows 0.18 – 0.84 at 70 degrees) in map modes 0 and 1, the top half
    // at 70 degrees in mode 2; none over the full-screen map (mode 3).
    {
        const CameraFrame cf = session_->cameras().frame();
        const CamPose& p = cf.pose;
        const int mapMode = map_.state().mode();
        const float W = float(width > 0 ? width : 1), H = float(height > 0 ? height : 1);
        float vx = 0, vy = 0, vw = W, vh = H;
        float fov = cf.fov;
        if (mapMode == 2) {
            const game::MapRect r = game::viewRect(2, width, height, map_.tune());
            vx = float(r.x), vy = float(r.y), vw = float(r.w), vh = float(r.h);
            fov = 70.0f;
        } else if (session_->wide()) {
            vy = float(int(H * 0.16f));
            vh = float(int(H * 0.66f));
            vy = H - vy - vh;  // from the top
            fov = 70.0f;
        }
        const float aspect = vw / (vh > 0 ? vh : 1.0f);
        const float t = std::tan(fov * kPi / 360.0f);
        j.key("tags").beginArray();
        for (const auto& v : netCars_) {
            if (mapMode == 3) break;
            if (!v->rc || !v->rc->shown) continue;
            if (net::isRaceMode(mode) && g.playerFinished(v->rc->id)) continue;  // its icon is hidden
            const vec3 w = v->rc->car.modelOrigin() + vec3(0, 2.2f, 0);
            const vec3 d = w - p.p;
            const float dd = length(d);
            if (dd > 300.0f) continue;
            const float z = -dot(d, p.Z);
            if (z < 0.5f) continue;
            const float x = dot(d, p.X) / (z * t * aspect), y = dot(d, p.Y) / (z * t);
            if (std::fabs(x) > 1.2f || std::fabs(y) > 1.2f) continue;
            j.beginObject();
            j.field("name", nameOf(v->rc->id));
            j.field("x", double((vx + vw * (0.5f + 0.5f * x)) / W));
            j.field("y", double((vy + vh * (0.5f - 0.5f * y)) / H));
            j.field("d", double(dd));
            if (net::isRaceMode(mode)) j.field("place", g.remotePlace(v->rc->slot));
            // Cops and Robbers sub-modes 1 and 2: the icon is the team's colour (5.3).
            if (mode == net::ModeCops && g.settings().subMode != net::FreeForAll)
                j.field("color", g.teamOf(v->rc->id) == 1 ? std::string("#ef0000") : std::string("#0000ef"));
            j.endObject();
        }
        j.endArray();
    }
    j.key("chat").beginArray();
    for (const auto& c : netChat_) j.value(latin1ToUtf8(c.first));
    j.endArray();
    j.endObject();
    return j.str();
}

}  // namespace cc
