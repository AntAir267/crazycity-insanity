#include "app/drive_view.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "ai/opponents.h"
#include "ai/road_network.h"
#include "city/city.h"
#include "formats/car_info.h"
#include "formats/pkg.h"
#include "model/parts.h"
#include "vfs/vfs.h"
#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "model/asset_cache.h"
#include "net/net_game.h"
#include "net/remote_car.h"
#include "platform/platform.h"
#include "physics/trailer.h"
#include "platform/web_bridge.h"
#include "render/fx_draw.h"
#include "render/mesh_cache.h"
#include "render/renderer.h"
#include "render/texture_cache.h"

namespace cc {

std::string DriveView::start(const CarEntry& car, int paint, City* city, StartMode mode, const FreeFlyCamera& fly,
                             EngineContext& ctx, float aspect) {
    stop();
    // Cruise reads race/<c>/roam.aimap (a race played before switched the network's race file).
    if (city && city->data().network() && city->data().network()->raceName() != "roam")
        const_cast<ai::RoadNetwork*>(city->data().network())->setRace(*ctx.vfs, "roam", false);
    auto s = std::make_unique<DriveSession>();
    phys::CollisionWorld* cw = nullptr;
    phys::resetBumpRandom();  // a race start restarts the physics random generator
    if (city) {
        cw = &city->collision();
        cw->resetProps();  // every prop stands again
    } else {
        if (!flatReady_) {
            flat_.setupFlat(*ctx.vfs, 0.0f);
            flatReady_ = true;
        }
        cw = &flat_;
    }
    world_ = cw;
    echo_ = false;
    std::string msg;
    s->setTrailerAllowed(trailerAllowedNext_);
    if (!s->start(*ctx.vfs, *ctx.assets, car.baseName, car.kind, paint, car.infoFlags, cw, aspect, msg)) {
        logError("cannot drive %s: %s", car.baseName.c_str(), msg.c_str());
        return "{\"ok\":false,\"why\":\"" + msg + "\"}";
    }
    s->setAutomatic(automatic_);
    s->setAutoReverse(autoReverse_);
    s->setBindings(setup_.game && setup_.haveControls ? &setup_.controls : nullptr);
    // vehicle-wheels.md "Surface": each game sets the rain grip from its own weather (Ant's choice:
    // 1 when it is not raining; 0.8 in rain, 0.75 at night).
    if (city) {
        const CityOptions& co = city->options();
        s->car().setWeatherScale(co.weather == 3 ? (co.timeOfDay == 3 ? 0.75f : 0.8f) : 1.0f);
    }
    std::string where;
    if (!city) {
        s->placeFlat();
        where = "flat ground";
    } else {
        const auto& starts = city->data().starts();
        if (mode == UnderCamera) {
            // 2 m above the first solid surface below the camera (if there is one).
            vec3 p = fly.pos;
            phys::ProbeHit hit;
            if (cw->probeStatic(vec3(p.x, p.y + 0.5f, p.z), vec3(p.x, p.y - 500.0f, p.z), hit) && hit.normal.y > 0.5f)
                p.y = hit.point.y + 2.0f;
            s->placeAt(p, fly.yaw);
            where = "under the camera";
        } else if (!city->data().hasAiNetwork() || starts.empty()) {
            s->placeAt(vec3(0, 20, 0), 0.0f);  // a city without an AI network
            where = "(0, 20, 0)";
        } else {
            // Free-roam start: an eligible intersection, 2 m above it, facing north.
            size_t i = mode == RandomStart ? size_t(std::rand()) % starts.size() : 0;
            s->placeAt(starts[i].pos + vec3(0, 2.0f, 0), 0.0f);
            where = strf("intersection %d", starts[i].intersection);
        }
    }
    s->setAutomatic(automatic_);
    if (!cockpit_.load(ctx, car.baseName)) logInfo("%s has no cockpit package", car.baseName.c_str());
    mirror_ = loadMirrorSettings(ctx, car.baseName);
    session_ = std::move(s);
    city_ = city;
    carName_ = car.baseName;
    // The effects of this drive (the archives may have changed since the last one) and the
    // player's car effects: texture damage, smoke, wobble, particles, sparks, chips, skid marks.
    effects_ = std::make_unique<fx::EffectsWorld>(*ctx.vfs);
    effects_->startSession(city ? city->options().weather : 0);
    effects_->images().setCondition(ctx.textures->condition());
    if (const VehicleModel* m = session_->model()) {
        playerFx_ = std::make_unique<fx::CarFx>();
        playerFx_->setup(effects_.get(), effects_->images(), *m, paintSetFor(session_->paint(), m->setCount()),
                         car.baseName, session_->car());
        session_->car().setEffects(playerFx_.get());
    }
    if (city) {
        // Props knocked loose puff their particles (effects.md, "Props").
        // The prop emitters belong to the loose-body pool (effects.md, "Props"): activated each
        // time an object gets a body, updated for the movers the after-samples step keeps, gone
        // when the body is let go or dropped.
        fx::EffectsWorld* fxw = effects_.get();
        phys::CollisionWorld* col = &city->collision();
        col->onLooseActive = [fxw](int key, const std::string& name, const phys::Mtx& M, bool untouched) {
            fxw->activateProp(key, name, M.X, M.Y, M.Z, M.o, untouched);
        };
        col->onLooseFrame = [fxw](int key, const phys::Mtx& M) { fxw->moveProp(key, M.X, M.Y, M.Z, M.o); };
        col->onLooseInactive = [fxw](int key) { fxw->deactivateProp(key); };
    }
    playerLights_ = LightBar();
    if (const VehicleModel* m = session_->model()) playerLights_.setup(*m, paintSetFor(session_->paint(), m->setCount()));
    hornPrev_ = false;
    if (city_) {
        city_->resetTraffic();  // an AI reset: lights restart, stop-sign queues empty
        startPolice(ctx);
        // Cruise: the slider's density (a race sets its own after this, races.md). Set before the
        // traffic: the cable cars discard the pedestrian pool's creation draws (cable-cars.md).
        sessionPedDensity_ = pedDensity_;
        sessionCircuit_ = false;
        startTraffic(ctx);
        startPeds(ctx);
        // Then the level objects (level-objects.md, "Race reset"); they move with the drive's frames.
        city_->resetLevelObjects();
        city_->setDriving(true);
    }
    startAudio(ctx);
    brokenSeen_ = 0;
    hudTimer_ = 1.0f;
    // HUD, HUD map and the saved view settings (hud.md, hudmap.md, controls.md "View settings").
    rhd_ = (car.infoFlags & 64) != 0;
    mapCity_ = city ? city->data().name() : std::string();
    map_ = HudMapView();
    if (city) map_.load(ctx, mapCity_);
    else map_.state().setAvailable(false);
    arrow_.load(ctx);
    hud_.reset(setup_.mode);
    paused_ = pauseMenu_ = mapPause_ = false;
    leaveViewer_ = false;
    waterPrev_ = false;
    dashRestore_ = false;
    if (setup_.haveView) {
        const uint8_t* v = setup_.view;
        session_->cameras().setViewState(v[0], v[3] != 0);
        session_->cameras().resetAll();
        wideSaved_ = v[2] != 0;
        session_->setWide(wideSaved_);
        session_->setGaugesOn(v[5] != 0);
        iconsOn_ = v[7] != 0;
        map_.state().setHeadingUp(v[10] != 0);
        // Byte 11 is replaced by the city's ZoomIn at every game start.
        if (map_.tune().hasZoomIn) setup_.view[11] = map_.tune().zoomIn != 0 ? 1 : 0;
        map_.state().setZoomedIn(setup_.view[11] != 0);
        map_.state().resetToSaved(v[1]);
    } else {
        wideSaved_ = session_->wide();
        map_.state().resetToSaved(0);
    }
    // A reset into the half-screen map: the dash view goes off (the wide view follows its saved
    // byte, an MM2 quirk kept).
    if (map_.state().mode() == 2 && session_->cameras().active() == CameraRig::Dash) {
        session_->cameras().leaveDash();
        dashRestore_ = true;
    }
    logInfo("driving %s at %s", car.baseName.c_str(), where.c_str());
    JsonWriter j;
    j.beginObject();
    j.field("ok", true);
    j.field("where", where);
    j.field("note", msg);
    j.endObject();
    return j.str();
}

std::vector<ai::TrafficPlayer> DriveView::trafficPlayers() const {
    std::vector<ai::TrafficPlayer> out;
    if (!session_) return out;
    const phys::Car& c = session_->car();
    ai::TrafficPlayer p;
    p.modelOrigin = c.modelOrigin();
    p.bodyPos = c.body().p;
    p.radius = c.modelRadius();
    p.horn = session_->hornHeld();
    p.X = c.rotation().r[0];
    p.Z = c.rotation().r[2];
    const vec3 box = c.tune().sim.inertiaBox;
    p.half = vec3(box.x * 0.5f, box.y * 0.5f, box.z * 0.5f);
    p.siren = playerLights_.any();  // a car with a light bar never counts as honking (its horn runs the siren)
    out.push_back(p);
    return out;
}

std::vector<ai::PolicePlayer> DriveView::policePlayers() const {
    std::vector<ai::PolicePlayer> out;
    if (!session_) return out;
    ai::PolicePlayer p;
    p.car = const_cast<phys::Car*>(&session_->car());
    p.room = session_->car().collision().room;
    if (const phys::Bound* b = session_->car().bound()) {
        p.boxMin = b->bmin;
        p.boxMax = b->bmax;
    }
    out.push_back(p);
    return out;
}

std::vector<ai::PedPlayer> DriveView::pedPlayers() const {
    std::vector<ai::PedPlayer> out;
    if (!session_) return out;
    const phys::Car& c = session_->car();
    ai::PedPlayer p;
    p.bodyPos = c.body().p;
    const phys::M3& R = c.rotation();
    p.X = R.r[0];
    p.Y = R.r[1];
    p.Z = R.r[2];
    p.velocity = c.velocity();
    p.speed = c.speed();
    p.steer = c.inputs().steer;
    p.gear = c.gearIndex();
    p.inertiaBox = c.tune().sim.inertiaBox;
    p.radius = c.modelRadius();
    out.push_back(p);
    return out;
}

int DriveView::pedCount() const {
    if (!city_) return 0;
    const ai::RoadNetwork* net = city_->data().network();
    const int pool = net ? net->cityAimap().pedPool : 100;
    return ai::PedestrianSystem::countFor(pool, sessionPedDensity_, pedsOff_, sessionCircuit_);
}

void DriveView::startPeds(EngineContext& ctx) {
    if (!city_ || !session_) return;
    if (audio_) audio_->clearPeds();
    city_->startPedestrians(ctx, pedCount(), city_->options().weather == 3, pedPlayers());
    if (audio_) audio_->setupPeds(&city_->pedestrians());
}

void DriveView::resetPeds() {
    if (city_ && session_ && city_->pedestrians().active()) city_->pedestrians().reset(pedPlayers());
}

void DriveView::setPedDensity(float d, EngineContext& ctx) {
    pedDensity_ = std::clamp(d, 0.0f, 1.0f);
    // The slider is Cruise's: a running race keeps its own density (races.md).
    if (session_ && city_ && !racing()) {
        sessionPedDensity_ = pedDensity_;
        startPeds(ctx);
    }
}

void DriveView::setPedsOff(bool off, EngineContext& ctx) {
    if (pedsOff_ == off) return;
    pedsOff_ = off;
    if (session_ && city_) startPeds(ctx);
}

std::string DriveView::pedsJson() const {
    JsonWriter j;
    j.beginObject();
    if (city_) {
        const ai::PedestrianSystem& ps = city_->pedestrians();
        const auto& st = ps.stats();
        j.field("count", ps.count());
        j.field("onStreets", ps.onStreets());
        j.field("lost", ps.lostCount());
        j.field("activeRoads", int(ps.activeRoads().size()));
        j.field("drawnNear", city_->pedsDrawnNear());
        j.field("drawnFar", city_->pedsDrawnFar());
        j.field("roadEnds", st.roadEnds);
        j.field("crossings", st.crossings);
        j.field("wary", st.wary);
        j.field("dives", st.dives);
        j.field("backups", st.backups);
        int modes[3] = {}, steps[4] = {};
        float nearest = 1e9f;
        int nearestPed = -1;
        const vec3 car = session_ ? session_->car().body().p : vec3();
        for (const ai::Pedestrian& p : ps.peds()) {
            if (!p.onStreets) continue;
            ++modes[std::clamp(p.mode, 0, 2)];
            ++steps[std::clamp(p.step, 0, 3)];
            const vec3 d = p.p - car;
            const float dd = std::sqrt(d.x * d.x + d.z * d.z);
            if (dd < nearest) {
                nearest = dd;
                nearestPed = p.index;
            }
        }
        // Pedestrians in a crossing step or a reaction: index, step, mode, x, z.
        j.key("busy");
        j.beginArray();
        for (const ai::Pedestrian& p : ps.peds()) {
            if (!p.onStreets || (p.step == 0 && p.mode == 0)) continue;
            j.beginArray();
            j.value(p.index);
            j.value(p.step);
            j.value(p.mode);
            j.value(double(p.p.x));
            j.value(double(p.p.z));
            j.endArray();
        }
        j.endArray();
        j.field("walking", modes[0]);
        j.field("waryNow", modes[1]);
        j.field("dodgingNow", modes[2]);
        j.field("crossingNow", steps[1] + steps[2] + steps[3]);
        if (nearestPed >= 0) {
            const ai::Pedestrian& p = ps.peds()[size_t(nearestPed)];
            const PedModel* m = ps.modelOf(p);
            j.field("nearest", double(nearest));
            j.field("nearestPed", nearestPed);
            j.field("nx", double(p.p.x));
            j.field("ny", double(p.p.y));
            j.field("nz", double(p.p.z));
            j.field("nh", double(p.h));
            j.field("nmode", p.mode);
            j.field("nstep", p.step);
            j.field("nseq", m && p.cur >= 0 ? m->sequences()[size_t(p.cur)].row.name : std::string("-"));
        }
    }
    j.endObject();
    return j.str();
}

void DriveView::startPolice(EngineContext& ctx) {
    if (!city_ || !session_) return;
    copFx_.clear();
    if (audio_) audio_->clearPolice();  // the cop cars are about to go
    city_->startPoliceDensity(ctx, netNoAi_ ? 0.0f : copDensity_, carName_, session_->tune().bound);  // no AI in a network game
    attachCopEffects();
    if (audio_) audio_->setupPolice(&city_->police());
}

void DriveView::attachCopEffects() {
    // Police cars get the same visuals as the player (damage-visuals.md, "Which cars").
    copFx_.clear();
    if (!city_ || !effects_) return;
    for (ai::Cop& c : city_->police().cops()) {
        if (!c.car || !c.model) continue;
        auto f = std::make_unique<fx::CarFx>();
        f->setup(effects_.get(), effects_->images(), *c.model, paintSetFor(c.paint, c.model->setCount()),
                 c.model->baseName, *c.car);
        c.car->setEffects(f.get());
        copFx_.emplace_back(c.car.get(), std::move(f));
    }
}

fx::CarFx* DriveView::copEffects(const phys::Car* car) const {
    for (const auto& p : copFx_)
        if (p.first == car) return p.second.get();
    return nullptr;
}

void DriveView::startAudio(EngineContext& ctx) {
    audio_.reset();
    audioPending_ = false;
    if (!ctx.sound || !ctx.vfs || !session_) return;
    if (!ctx.soundReady) {
        audioPending_ = true;  // set up once the sound data has arrived
        return;
    }
    audio::GameAudioConfig cfg;
    cfg.city = city_ ? city_->data().name() : std::string();
    if (city_) {
        cfg.weather = city_->options().weather;
        cfg.timeOfDay = city_->options().timeOfDay;
    }
    cfg.mode = race_ ? int(race_->mode()) : audioMode_;  // the race's mode, else Cruise unless a network game says otherwise
    cfg.course = race_ ? raceNo_ : 0;
    cfg.playerCar = carName_;
    cfg.playerLightBar = playerLights_.any();
    cfg.ambient = ctx.sound->options().ambient;
    cfg.commentary = ctx.sound->options().commentary;
    cfg.minimalInstall = !ctx.audioExtras;
    audio_ = std::make_unique<audio::GameAudio>(*ctx.sound, *ctx.vfs);
    phys::World* world = city_ ? static_cast<phys::World*>(&city_->collision()) : static_cast<phys::World*>(&flat_);
    audio_->setup(cfg, session_->car(), world);
    // MM2's set-up order: the player's car, the opponents, the traffic, then the police; the
    // pedestrians with the street population.
    if (race_) audio_->setupOpponents(&race_->opponents());
    if (city_) {
        audio_->setupTraffic(&city_->traffic());
        audio_->setupPolice(&city_->police());
        audio_->setupPeds(&city_->pedestrians());
    }
    if (!race_) audio_->gameReset();  // a race's own start emits its pre-race event
    audio_->viewSet(int(session_->cameras().active()));
    echoRoom_ = 0;
}

void DriveView::setCopDensity(float d, EngineContext& ctx) {
    copDensity_ = std::clamp(d, 0.0f, 1.0f);
    if (session_ && city_) {
        startPolice(ctx);
        startTraffic(ctx);  // the AI hooks are set again with the new police
    }
}

namespace {
mat4 carWorld(const phys::Car& c) {
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

void DriveView::addCopCars(std::vector<CityCar>& cars) const {
    if (!city_) return;
    for (const ai::Cop& c : city_->police().cops()) {
        if (!c.model) continue;
        CityCar cc;
        cc.model = c.model.get();
        cc.world = carWorld(*c.car);
        cc.paint = c.paint;
        cc.room = c.car->collision().room;
        cc.wheelPose = carWheelPose(c.car.get(), c.model.get());
        cc.braking = c.car->inputs().brake > 0.0f;
        cc.reverse = c.car->gearIndex() == 0;
        cc.lightBar = &c.lights;
        cc.fx = copEffects(c.car.get());
        if (cc.fx) {
            cc.fx->refresh(*c.car);
            cc.wheelPose = carWheelPose(c.car.get(), c.model.get(), cc.fx->wheelTilts());
        }
        cars.push_back(cc);
        // A computer truck's trailer stands where it was placed (trailer.md 10, 11).
        const phys::Trailer* t = c.car->trailer();
        if (t && c.model->trailer) {
            CityCar tc;
            tc.model = c.model->trailer.get();
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
            tc.paint = paintSetFor(c.paint, c.model->setCount());
            tc.room = t->room();
            tc.braking = c.car->inputs().brake > 0.1f;
            cars.push_back(tc);
        }
    }
}

// The trailer (trailer.md 11): its own frame and room, the car's paint set (the trailer's set is
// that modulo its own count), TLIGHT while the truck's brake input is above 0.1. It stays visible
// in the dash and bumper views and in the mirror.
bool DriveView::trailerCar(CityCar& t) const {
    if (!session_ || !session_->trailerModel()) return false;
    const VehicleModel* m = session_->model();
    t.model = session_->trailerModel();
    t.world = session_->trailerMatrix();
    t.paint = paintSetFor(session_->paint(), m ? m->setCount() : 0);
    t.room = session_->trailer()->room();
    t.wheelPose = session_->trailerWheelPose();
    t.braking = session_->car().inputs().brake > 0.1f;
    return true;
}

void DriveView::startTraffic(EngineContext& ctx) {
    if (!city_ || !session_) return;
    if (audio_) audio_->clearTraffic();
    city_->traffic().setPedestrianPool(pedCount());  // its creation draws precede the cable cars'
    // A network game builds its world without traffic and without cable cars (multiplayer.md 1.4).
    city_->traffic().setCableCarSwitch(!netNoAi_);
    city_->startTraffic(ctx, netNoAi_ ? 0.0f : trafficDensity_, trafficPlayers());
    if (audio_) audio_->setupTraffic(&city_->traffic());
    City* city = city_;
    DriveSession::AiHooks h;
    h.beforePhysics = [this, city](float F) {
        // The voice band timers run at the very start of the AI update (traffic-sounds.md).
        if (audio_) audio_->aiFrameStart(F, session_->car().speed());
        lastF_ = F;
        clock_ += double(F);
        // Step 1.1: the player's AI bookkeeping, the tracked road (driver-planner.md §16 step 3).
        if (race_ && city->data().network())
            tracking_.frame(*city->data().network(), city->data(), session_->car().body().p);
        ai::TrafficSystem& ts = city->traffic();
        const std::vector<ai::TrafficPlayer> tps = trafficPlayers();
        ts.updateCars(F, tps);
        // Pedestrians after the traffic cars (pedestrians.md, "Frame order").
        ai::PedestrianSystem& peds = city->pedestrians();
        if (peds.active()) {
            peds.update(F, pedPlayers(), ts.active() ? &ts : nullptr);
            if (audio_) audio_->pedFrame(peds);  // the pedestrians' voices (traffic-sounds.md)
        }
        // Opponents and police, then the cable cars and the light sets (traffic.md, "Frame order").
        ai::PoliceSystem& ps = city->police();
        if (race_) raceAiFrame(F);
        else if (ps.active()) ps.update(F, policePlayers(), ts.active() ? &ts : nullptr, clock_);
        ts.lateUpdate(F, tps);
        // The movers of this frame (car-collision.md 1.1): the player, traffic cars without a
        // body that avoid or rejoin, police within 250 m, physical traffic.
        movers_.clear();
        session_->appendMovers(movers_);  // the player's car, then its trailer (trailer.md 6.1)
        if (ts.active()) {
            ts.prePhysics();
            ts.appendRowMovers(movers_);
        }
        if (race_) race_->opponents().appendMovers(movers_);  // row 4: race opponents, at any distance
        if (ps.active()) ps.appendMovers(movers_);
        if (ts.active()) ts.appendBodyMovers(movers_);
        // The other players' cars, after the AI's movers, in slot order (multiplayer.md 3.4).
        if (net_) net_->beforePhysics(F, movers_);
        // The level objects move once, after the AI update and before the physics.
        city->updateLevelObjects(F, {session_->car().body().p});
        // Loose bodies last (row 7); those of the first kind outside the player's rooms go.
        std::vector<int> oppRooms;
        if (race_)
            for (const ai::Opponent& o : race_->opponents().opponents())
                if (o.roomWaking) oppRooms.push_back(o.car->collision().room);
        city->collision().declareLoose(movers_, session_->car().collision().room, race_ ? &oppRooms : nullptr);
        for (phys::SceneMover& m : movers_) m.touchedPlayer = false;
    };
    sceneHooks_ = phys::SceneHooks();
    sceneHooks_.laneCars = [city](std::vector<phys::LaneCar>& v) {
        if (city->traffic().active()) city->traffic().laneCars(v);
    };
    sceneHooks_.makePhysical = [city](int id, const phys::Mtx& lane, float speed) {
        return city->traffic().takeBody(id, lane, speed);
    };
    sceneHooks_.findRoom = [city](const vec3& p, int hint) { return city->roomAt(p, hint); };
    sceneHooks_.trafficImpact = [this](int id, const vec3& J, int otherSound) {
        if (audio_) audio_->trafficImpact(id, J, otherSound);
    };
    h.sample = [this, city](float dt, float invdt) {
        movers_.reserve(movers_.size() + 64);
        scene_.runSample(movers_, city->collision(), dt, invdt, sceneHooks_);
    };
    h.afterPhysics = [this, city]() {
        city->collision().afterLooseSamples(movers_);  // loose-bodies.md 5.2
        city->collision().takeLooseEvents();
        if (city->traffic().active()) city->traffic().afterPhysics();
        if (city->police().active()) city->police().afterPhysics(movers_, lastF_);
        if (race_) race_->opponents().afterPhysics(movers_);
        if (net_) net_->frameEnd(lastF_);
        playerLights_.update(lastF_, session_->car().forward());
        for (auto& nc : netCars_)
            if (nc->rc) nc->lights.update(lastF_, nc->rc->car.forward());
    };
    session_->setAiHooks(h);
}

void DriveView::setTrafficDensity(float d, EngineContext& ctx) {
    trafficDensity_ = std::clamp(d, 0.0f, 1.0f);
    if (session_ && city_) startTraffic(ctx);
}

void DriveView::stop() {
    const bool wasNet = net_ != nullptr;
    endRace();
    endNetGame();
    if (wasNet) netNoAi_ = false;  // (start() stops the previous drive first: a new network game keeps it)
    if (city_) city_->stopPedestrians();
    audio_.reset();  // before the cars it listens to go
    audioPending_ = false;
    if (city_) city_->stopTraffic();
    if (city_) city_->stopPolice();
    if (city_) city_->setDriving(false);
    if (city_) {
        city_->collision().onPropBroken = nullptr;
        city_->collision().onLooseActive = nullptr;
        city_->collision().onLooseFrame = nullptr;
        city_->collision().onLooseInactive = nullptr;
        city_->setEffects(nullptr);
    }
    copFx_.clear();
    if (session_) {
        session_->forceFeedback().stopAll();  // engine choice: no effects without a game
        session_->car().setEffects(nullptr);
    }
    ffWasActive_ = ffPopup_ = false;
    playerFx_.reset();
    effects_.reset();
    if (city_) city_->collision().resetProps();  // the viewer shows the city whole again
    session_.reset();
    city_ = nullptr;
    paused_ = pauseMenu_ = mapPause_ = leaveViewer_ = false;
    world_ = nullptr;
    pageEvent("hud", "{\"visible\":false}");
}

void DriveView::setOption(const std::string& name, int v) {
    if (name == "automatic") {
        automatic_ = v != 0;
        if (session_) session_->setAutomatic(automatic_);
    } else if (name == "autoreverse") {
        autoReverse_ = v != 0;
        if (session_) session_->setAutoReverse(autoReverse_);
    } else if (name == "mirror") {
        mirrorOn_ = v != 0;
    } else if (name == "debugfx") {
        // Debugging aid (not on the page): 0 = effects neither simulated nor drawn (timing checks).
        fxOff_ = v == 0;
        if (session_) session_->car().setEffects(fxOff_ ? nullptr : playerFx_.get());
        if (city_)
            for (ai::Cop& c : city_->police().cops())
                if (c.car) c.car->setEffects(fxOff_ ? nullptr : copEffects(c.car.get()));
    } else if (name == "debugdamage" && session_) {
        // Debugging aid (not on the page): the player's damage set to v % of MaxDamage, to look at
        // the smoke levels and wobble without crashing 30 times.
        session_->car().damage().damage = session_->car().damageLimit() * float(v) / 100.0f;
    } else if (name == "peddetail") {
        if (city_) city_->setPedDetailDebug(v);  // debug: 1 far strips only, 2 near figures only
    } else if (name == "autopilot") {
        setAutopilot(v != 0);
    } else if ((name == "debugthrottle" || name == "debugsteer") && session_) {
        // Debugging aid (not on the page): the player's throttle and steering held at v % (both 0
        // gives the controls back), for driving a car in a page without keyboard focus.
        (name == "debugthrottle" ? debugThrottle_ : debugSteer_) = float(v) / 100.0f;
        if (debugThrottle_ == 0.0f && debugSteer_ == 0.0f) {
            session_->setControlOverride(nullptr);
        } else {
            session_->setControlOverride([this](float F, phys::DriverControls& dc) {
                (void)F;
                dc.accelerate = debugThrottle_ > 0.0f ? debugThrottle_ : 0.0f;
                dc.brake = debugThrottle_ < 0.0f ? -debugThrottle_ : 0.0f;
                dc.steer = debugSteer_;
            });
        }
    } else if (name == "netgoto" && net_ && session_) {
        // Debugging aid (not on the page): the local car 12 m behind remote slot v's car, facing
        // its way, announced to everyone as a "car reset"; v = −1: 8 m from the Cops and Robbers
        // gold, −2: 8 m from the own team's base, −3: the race's target gate, v ≥ 100: a free-roam
        // start.
        if (v >= 100 && city_ && !city_->data().starts().empty()) {
            // v ≥ 100: free-roam start v − 100 (an open intersection), facing north.
            const auto& st = city_->data().starts();
            session_->placeAt(st[size_t(v - 100) % st.size()].pos + vec3(0, 2.0f, 0), 0.0f);
            net_->announceReset();
        }
        if (v == -3 && net_->haveWaypoints()) {
            // v = −3: on the race's target gate, turned its way (the gate test sees it next frame).
            const game::WaypointList& L = net_->waypoints();
            const Waypoint& w = L.point(L.target());
            session_->placeAt(w.pos + vec3(0, 0.5f, 0), w.heading * -0.017453292f);
            net_->announceReset();
        }
        if ((v == -1 || v == -2) && net_->basesPlaced()) {
            const vec3 t = v == -1 ? net_->goldPoint() : (net_->localTeam() == 1 ? net_->hideoutPoint() : net_->bankPoint());
            session_->placeAt(t + vec3(0, 1.0f, 8.0f), 0.0f);
            net_->announceReset();
        }
        for (net::RemoteCar* rc : net_->remoteCars()) {
            if (rc->slot != v || !rc->shown) continue;
            const vec3 f = rc->car.forward();
            const float h = std::atan2(-f.x, -f.z);
            const vec3 p = rc->car.body().p - rc->car.tune().sim.centerOfGravity - vec3(f.x, 0.0f, f.z) * 12.0f;
            session_->placeAt(p + vec3(0, 1.0f, 0), h);
            net_->announceReset();
        }
    }
}

void DriveView::setPaused(bool p, bool menu) {
    paused_ = p;
    pauseMenu_ = p && menu;
    if (!p) mapPause_ = false;
}

void DriveView::restart(EngineContext& ctx) {
    if (!session_) return;
    if (race_) {
        // A race restarts by its own sequence (races.md, "Restart"), which resets the world
        // through its host.
        race_->restart();
        brokenSeen_ = 0;
        hud_.reset(setup_.mode);
        waterPrev_ = false;
        dashRestore_ = false;
        map_.state().resetToSaved(map_.state().mode());
        paused_ = pauseMenu_ = mapPause_ = false;
        return;
    }
    // The world reset: props back, AI reset, the player back at its start, damage cleared.
    session_->resetToStart();
    session_->car().damage().reset();
    session_->car().setDrivable(true);
    if (city_) {
        city_->resetTraffic();
        startPolice(ctx);
        startTraffic(ctx);
        resetPeds();  // the generator restarts; the fill around the player's start (pedestrians.md)
        city_->resetLevelObjects();
    }
    brokenSeen_ = 0;
    hud_.reset(setup_.mode);
    waterPrev_ = false;
    if (audio_ && !race_) audio_->gameReset();  // a Cruise restart: the pre-race line again
    dashRestore_ = false;
    map_.state().resetToSaved(map_.state().mode());
    paused_ = pauseMenu_ = mapPause_ = false;
}

void DriveView::viewBytes(uint8_t out[12]) const {
    for (int i = 0; i < 12; ++i) out[i] = setup_.view[i];
    if (!session_) return;
    out[0] = uint8_t(session_->cameras().externalEntry());
    out[1] = uint8_t(map_.state().mode());
    out[2] = wideSaved_ ? 1 : 0;
    out[3] = session_->cameras().active() == CameraRig::Dash ? 1 : 0;
    // Byte 4 (mirror at game start) is not changed by Backspace in MM2; our engine remembers the
    // mirror per browser instead (dash.md, Ant's choice), so the file's byte stays as read.
    out[5] = session_->gaugesOn() ? 1 : 0;
    out[6] = session_->cameras().active() == CameraRig::Orbit ? 1 : 0;
    out[7] = iconsOn_ ? 1 : 0;
    out[10] = map_.state().headingUp() ? 1 : 0;
    out[11] = map_.state().zoomedIn() ? 1 : 0;
}

void DriveView::applyMapMode(int oldMode) {
    const int m = map_.state().mode();
    if (m == oldMode) return;
    setup_.view[1] = uint8_t(m);  // saved every time it changes (also 3)
    if (m == 2) {
        // The half-screen map: the wide view on (not saved as on), the dash view off. Tab entering
        // mode 2 with the dash on sets the one "bring the dash back" mark (hudmap.md).
        session_->setWide(true);
        if (session_->cameras().active() == CameraRig::Dash) {
            session_->cameras().leaveDash();
            dashRestore_ = oldMode == 1;
        }
    } else if (oldMode == 2) {
        session_->setWide(wideSaved_);
        // Tab from mode 2 to 0 reads the mark: the dash comes back only if nothing cleared it.
        if (m == 0 && dashRestore_) session_->cameras().enterDash();
        dashRestore_ = false;
    }
}

void DriveView::handleHudKeys(const InputState& in, EngineContext& ctx) {
    const bool kb = in.hasFocus;
    auto pressed = [&](Key k) { return kb && in.pressed[k]; };
    // A game reads the HUD actions from the driver's binding table (map mode, full map, map zoom,
    // map orientation, opponent icons: controls.md); the viewer from its own keys.
    const bool bound = setup_.game && setup_.haveControls;
    auto action = [&](int a, Key viewerKey) {
        return bound ? hudPresses_.pressed(setup_.controls, in, a) : pressed(viewerKey);
    };
    game::HudMapState& ms = map_.state();
    const int before = ms.mode();
    if (action(ActMapMode, KeyTab)) {
        bool leftFull = false;
        ms.tab(leftFull);  // leaving the full-screen map with Tab keeps a paused game paused
        session_->cameras().noteViewKey();  // clears the orbit view's dash mark (cameras.md)
    }
    if (action(ActFullMap, KeyM)) {
        dashRestore_ = false;  // a view key that is not ignored in mode 2 clears the dash mark
        bool toggled = false;
        ms.fullScreen(toggled);
        session_->cameras().noteViewKey();
        if (toggled && setup_.singlePlayer) {
            if (ms.mode() == 3) {
                paused_ = true;
                mapPause_ = true;
            } else if (mapPause_ || paused_) {
                paused_ = false;
                mapPause_ = false;
                pauseMenu_ = false;
            }
        }
    }
    if (action(ActMapZoom, KeyK)) ms.zoom();  // viewer: N is the next waypoint (MM2's S)
    if (action(ActMapOrientation, KeyO)) ms.orientation();
    if (action(ActIcons, KeyI)) iconsOn_ = !iconsOn_;
    applyMapMode(before);
    if (!setup_.game) {
        if (paused_ && pressed(KeyEscape)) {
            paused_ = mapPause_ = false;
            leaveViewer_ = true;
        }
        return;
    }
    if (pressed(KeyF2) && setup_.singlePlayer && !pauseMenu_) {
        paused_ = !paused_;
        mapPause_ = false;
    }
    if (pressed(KeyF4) && !net_) {  // a network game restarts only from its results (the host)
        restart(ctx);
        pageEvent("pause", "{\"menu\":false,\"restarted\":true}");
    }
    if (paused_ && pressed(KeyEscape)) {
        // Esc on a paused game: opens the pause menu, or closes it again.
        if (pauseMenu_) setPaused(false, false);
        else setPaused(true, true);
        pageEvent("pause", pauseMenu_ ? "{\"menu\":true}" : "{\"menu\":false}");
    }
}

void DriveView::postWaterText() {
    // The city's water text for 3 s, upper line: London 642, San Francisco 643, others 30.
    // MapName compared case-sensitively (hud.md).
    const int id = mapCity_ == "london" ? 642 : mapCity_ == "sf" ? 643 : 30;
    hud_.postPrimary(id, text_ ? text_(id) : std::string(), 3.0f, true);
}

// A race message's text: the player's mmlang.dll string (232 is a format with the target
// speed); "" when there is no file (the page shows its own words).
std::string DriveView::raceHudText(int id, const std::string& arg) const {
    std::string t = text_ ? text_(id) : std::string();
    if (t.empty() || arg.empty()) return t;
    // One printf conversion (mmlang.md): a number printed into the text.
    const size_t p = t.find('%');
    if (p == std::string::npos) return t;
    size_t q = p + 1;
    while (q < t.size() && std::strchr("-+ #0123456789.", t[q])) ++q;
    if (q >= t.size()) return t;
    const std::string spec = t.substr(p, q - p + 1);
    char buf[64];
    if (t[q] == 'f' || t[q] == 'g' || t[q] == 'e') std::snprintf(buf, sizeof buf, spec.c_str(), std::atof(arg.c_str()));
    else if (t[q] == 'd' || t[q] == 'i') std::snprintf(buf, sizeof buf, spec.c_str(), std::atoi(arg.c_str()));
    else return t;
    return t.substr(0, p) + buf + t.substr(q + 1);
}

bool DriveView::debugJumpToTarget() {
    if (!race_ || !session_) return false;
    const game::WaypointList& L = race_->waypoints();
    if (L.size() == 0 || L.done() || L.disabled()) return false;
    const Waypoint& w = L.point(L.target());
    session_->placeAt(w.pos + vec3(0, 1.0f, 0), w.heading * -0.017453292f);
    return true;
}

void DriveView::syncRaceHud() {
    if (!race_ || !session_) return;
    const game::RaceSession& R = *race_;
    const game::WaypointList& L = R.waypoints();
    const int mode = int(R.mode());
    if (hud_.mode() != mode) hud_.reset(mode);
    // What each mode shows (hud.md, "Per mode"); Crash Course lessons by event kind.
    game::HudModeLayout lay = game::hudLayoutFor(mode);
    if (R.mode() == game::RaceMode::CrashCourse) {
        const int k = R.crashKind();
        lay.timer = 2;
        lay.arrow = true;
        lay.checkpoints = true;
        lay.markersForced = false;
        if (k == 9) lay.arrow = false;
        else if (k == 2 || k == 8) {
            lay.arrow = lay.checkpoints = false;
            lay.markersForced = true;
            if (k == 2) lay.timer = 0;
        }
    }
    hud_.setLayout(lay);
    // Timers: the race clock (Checkpoint, Circuit), the countdown while the HUD shows it.
    float timer = -1.0f;
    if (lay.timer == 1) timer = R.raceClock();
    else if (lay.timer == 2 && R.countdownShown()) timer = R.countdown();
    const int warning = R.timerWarning() ? (R.countdown() <= 3.0f ? 2 : 1) : 0;
    hud_.setExternalTimer(timer, warning);
    // Messages: the primary line (a text, or a split time alone), the secondary line.
    const game::HudMessage& m = R.message();
    game::HudLine p, sec;
    if (m.text != 0) {
        p.textId = m.text;
        p.text = raceHudText(m.text, m.arg);
        p.timeLeft = m.time;
        p.upper = m.upper;
        if (m.second) {
            sec.textId = m.second;
            sec.text = raceHudText(m.second, std::string());
        } else if (!m.secondTime.empty()) {
            sec.text = m.secondTime;
        }
    } else if (!m.secondTime.empty()) {
        p.text = m.secondTime;
        p.timeLeft = m.time;
        p.upper = m.upper;
    }
    hud_.setLines(p, sec);
    // Place, checkpoints, laps (hud.md, "Place, checkpoints and laps").
    hud_.setPlace(R.playerPlace(), R.racers());
    const int n = L.size();
    if (R.mode() == game::RaceMode::Circuit) {
        // "i/(count − 1)", i the checkpoint just passed: in the fixed order, the gates passed
        // in this lap.
        int i = 0;
        for (int g = 1; g < n; ++g)
            if (L.passed(g)) ++i;
        hud_.setCheckpoints(i, std::max(0, n - 1));
        const int laps = L.laps();
        hud_.setLaps(std::min(L.lap() + 1, laps), laps);
        std::vector<float> rows = R.lapTimes();
        if (int(rows.size()) < laps && R.state() >= 3 && !R.finished()) rows.push_back(R.lapClock());
        hud_.setLapTimes(rows);
    } else {
        const int total = std::max(0, n - 1);
        int k = std::min(std::max(0, L.count() - 1), total);
        if (L.done()) k = total;
        hud_.setCheckpoints(k, total);
    }
    hud_.setArrowTarget(L.arrowOn(), L.targetPosition());
    // Opponent markers (their place as the digit) and Blitz's cyan markers over the checkpoints;
    // the map's opponents and checkpoint dots (hudmap.md, "Drawing").
    std::vector<game::HudMarker>& mk = hud_.markers();
    mk.clear();
    hud_.mapCars.clear();
    hud_.mapDots.clear();
    const auto& opps = R.opponents().opponents();
    for (size_t i = 0; i < opps.size(); ++i) {
        const ai::Opponent& o = opps[i];
        if (!o.car) continue;
        game::HudMarker hm;
        hm.pos = o.car->body().p;
        hm.digit = R.opponentPlace(int(i));
        mk.push_back(hm);
        game::HudMapCar mc;
        mc.pos = o.car->body().p;
        mc.forward = o.car->forward();
        hud_.mapCars.push_back(mc);
    }
    const bool circuit = R.mode() == game::RaceMode::Circuit;
    const bool blitz = R.mode() == game::RaceMode::Blitz;
    if (blitz)
        for (int i = 0; i < n; ++i)
            if (L.shown(i)) {
                game::HudMarker hm;
                hm.pos = L.point(i).pos;
                hm.rgb[0] = 0;
                hm.rgb[1] = 255;
                hm.rgb[2] = 255;
                mk.push_back(hm);
            }
    if (!L.disabled() && n > 0) {
        bool othersHit = true;
        for (int i = 1; i < n - 1; ++i)
            if (!L.passed(i)) othersHit = false;
        for (int i = 0; i < n; ++i) {
            int set = -1;  // 2 green, 3 grey, 4 yellow, 5 finish
            const bool hit = L.passed(i), cur = i == L.target();
            if (circuit) {
                set = i == 0 ? 5 : (hit ? 3 : (cur ? 4 : 2));
            } else if (i == 0) {
                set = -1;  // the start is never marked
            } else if (i < n - 1 || blitz || R.mode() == game::RaceMode::CrashCourse) {
                if (!hit) set = cur ? 4 : (L.onlyTarget() ? -1 : 2);
            } else if (L.checkpointRace()) {
                set = othersHit && !hit ? 5 : -1;
            }
            if (set < 0) continue;
            game::HudMapDot d;
            d.pos = L.point(i).pos;
            d.set = set;
            hud_.mapDots.push_back(d);
        }
    }
}

std::string DriveView::hudJson(int width, int height) const {
    JsonWriter j;
    j.beginObject();
    if (!session_) {
        j.field("visible", false);
        j.endObject();
        return j.str();
    }
    const HudState h = session_->hud();
    const bool lookHidden = session_->cameras().lookingAround();
    const bool hidden = lookHidden || paused_;
    j.field("visible", true);
    j.field("paused", paused_);
    j.field("pauseMenu", pauseMenu_);
    j.field("game", setup_.game);
    j.field("mode", setup_.mode);
    // Gauges overlay: H (view byte 5); hidden in the dash view, where the cockpit shows them.
    const bool dash = session_->cameras().active() == CameraRig::Dash;
    j.field("gauges", session_->gaugesOn() && !dash && !hidden);
    j.field("dash", dash);
    j.field("hideAll", hidden);  // look keys and pause: everything except the race timer
    j.field("mph", h.mph);
    j.field("kmh", double(h.kmh));
    j.field("rpm", double(h.rpm));
    j.field("maxRpm", double(h.maxRpm));
    j.field("gear", std::string(1, h.gear));
    j.field("automatic", h.automatic);
    j.field("camera", h.camera);
    {
        const phys::Car& c = session_->car();
        const float lim = c.damageLimit();
        float f = lim > 0.0f ? c.damage().damage / lim : 0.0f;
        if (f > 1.0f) f = 1.0f;
        if (f < 0.0f) f = 0.0f;
        j.field("damage", double(f));
    }
    const game::HudModeLayout& L = hud_.layout();
    const float t = hud_.shownTimer();
    // Cops and Robbers shows its clock only in timed games (hud.md, "Per mode").
    const bool untimedCops = net_ && net_->mode() == net::ModeCops && !net_->copsTimed();
    if (t >= 0.0f && !untimedCops) j.field("timer", game::formatTimer(t));
    j.field("warning", hud_.timeWarning());
    if (L.place && hud_.racers() > 0) j.key("place").beginArray().value(hud_.place()).value(hud_.racers()).endArray();
    if (L.checkpoints && hud_.checkpointsTotal() > 0)
        j.key("checkpoints").beginArray().value(hud_.checkpointsDone()).value(hud_.checkpointsTotal()).endArray();
    if (L.laps && hud_.laps() > 0) {
        j.key("lap").beginArray().value(hud_.lap()).value(hud_.laps()).endArray();
        j.key("lapTimes").beginArray();
        for (float x : hud_.lapTimes()) j.value(game::formatTime(x));
        j.endArray();
    }
    auto line = [&](const char* k, const game::HudLine& l) {
        if (l.textId < 0 && l.text.empty()) return;
        j.key(k).beginObject();
        j.field("id", l.textId);
        j.field("text", l.text);
        j.field("upper", l.upper);
        j.endObject();
    };
    line("primary", hud_.primary());
    line("secondary", hud_.secondary());
    // Where the 3D view and the map are (fractions of the screen, top-left origin).
    const int mode = map_.state().mode();
    j.field("mapMode", mode);
    j.field("mapAvailable", map_.available());
    j.field("headingUp", map_.state().headingUp());
    j.field("zoomedIn", map_.state().zoomedIn());
    j.field("icons", iconsOn_);
    const float W = float(width > 0 ? width : 1), H = float(height > 0 ? height : 1);
    auto rect = [&](const char* k, const game::MapRect& r) {
        j.key(k).beginObject();
        j.field("x", double(float(r.x) / W));
        j.field("y", double(float(r.y) / H));
        j.field("w", double(float(r.w) / W));
        j.field("h", double(float(r.h) / H));
        j.endObject();
    };
    if (race_) {
        // The race for the page's results screen and end-of-race menu (results.md, races.md).
        const game::RaceSession& R = *race_;
        j.key("race").beginObject();
        j.field("mode", int(R.mode()));
        j.field("race", R.raceNumber());
        j.field("title", latin1ToUtf8(R.raceTitle()));
        j.field("pro", R.professional());
        j.field("state", R.state());
        j.field("finished", R.finished());
        j.field("lost", R.lost());
        j.field("endMenu", R.endMenuOpen());
        j.field("resultsShown", R.resultsShown());
        j.field("score", R.score());
        j.field("nextRace", R.nextRaceOffered());
        j.field("crashKind", R.crashKind());
        if (R.message().text == 232) j.field("arg", R.message().arg);
        j.key("results").beginArray();
        for (const game::ResultRow& row : R.results()) {
            j.beginObject();
            j.field("place", row.place);
            j.field("racer", row.racer);
            j.field("time", double(row.time));
            j.field("passMark", row.passMark);
            j.endObject();
        }
        j.endArray();
        j.endObject();
    }
    rect("view", mode == 3 ? game::MapRect{0, 0, width, height} : game::viewRect(mode, width, height, map_.tune()));
    if (mode >= 1)
        rect("map", game::mapRect(mode, width, height, map_.tune(),
                                  rhd_ && session_->cameras().active() == CameraRig::Dash));
    j.endObject();
    return j.str();
}

bool DriveView::PlatformFf::playing(Effect e) const { return p && p->hapticPlaying(int(e)); }
void DriveView::PlatformFf::start(Effect e) {
    if (p) p->hapticStart(int(e));
}
void DriveView::PlatformFf::stop(Effect e) {
    if (p) p->hapticStop(int(e));
}
void DriveView::PlatformFf::setFriction(int c) {
    if (p) p->hapticFriction(c);
}
void DriveView::PlatformFf::setSpring(int c) {
    if (p) p->hapticSpring(c);
}
void DriveView::PlatformFf::setRumble(int periodUs, int magnitude) {
    if (p) p->hapticRumble(periodUs, magnitude);
}
void DriveView::PlatformFf::jolt(int strength, int direction) {
    if (p) p->hapticJolt(strength, direction);
}

void DriveView::forceFeedbackFrame(const InputState& in, EngineContext& ctx) {
    // Active (controls.md, "Force feedback"): a game with the driver's force-feedback option on,
    // the joystick or wheel device in use, a controller that reports force feedback.
    ffDevice_.p = ctx.platform;
    bool active = false;
    if (setup_.game && setup_.haveControls && setup_.ffOn && in.pad.connected && in.pad.feedback && ctx.platform &&
        ctx.platform->hapticAvailable()) {
        const int dev = setup_.controls.effectiveDevice(in);
        active = dev == 2 || dev == 4;
    }
    ForceFeedback& ff = session_->forceFeedback();
    ff.configure(&ffDevice_, active, setup_.ffCollision, setup_.ffRoad);
    ff.setClock(ctx.time);
    if (ffWasActive_ && !active) ff.stopAll();
    ffWasActive_ = active;
    // A pop-up opening (the Esc menu, the results, the end menu) stops rumble, spring and friction.
    const bool popup = pauseMenu_ || (race_ && (race_->resultsShown() || race_->endMenuOpen()));
    if (popup && !ffPopup_ && active) ff.popup();
    ffPopup_ = popup;
    // Every paused frame: the force-feedback reset (friction and spring play on with their values).
    if (paused_ && active) ff.reset();
}

void DriveView::update(const InputState& in, float dt, EngineContext& ctx, float aspect) {
    if (!session_) return;
    lastDt_ = dt;
    wall_ = ctx.time;
    handleHudKeys(in, ctx);
    forceFeedbackFrame(in, ctx);
    map_.state().update(dt);  // the zoom keeps animating on the paused full-screen map
    if (paused_) {
        // The water timer and the falling-through check run also while paused (races.md).
        {
            float F = dt;
            if (F < 0.0001f) F = 0.0001f;
            if (F > 0.1f) F = 0.1f;
            if (race_) {
                race_->pausedFrame(F);
                syncRaceHud();
            } else {
                session_->pausedWorldLimits(dt, city_ ? &city_->data() : nullptr);
            }
        }
        if (audio_) audio_->pausedFrame(dt);  // the speech queue runs on while paused
        hudTimer_ += dt;
        if (hudTimer_ >= 0.05f) {
            hudTimer_ = 0;
            int w = 1, h = 1;
            viewSize(ctx, w, h);
            pageEvent("hud", hudJson(w, h));
        }
        return;
    }
    {
        const int m = map_.state().mode();
        session_->setViewKeyLocks(m == 2 || m == 3, m == 2);
    }
    const float waterBefore = session_->car().collision().waterTimer;
    session_->frame(in, dt, aspect, city_ ? &city_->data() : nullptr);
    // Once per frame after the physics: prop particles (following their loose bodies), then the
    // rain (effects.md).
    if (effects_) effects_->frameUpdate(session_->lastFrame(), city_ && city_->options().weather == 3);
    if (session_->viewKeyPressed()) map_.state().reapply();  // every view key snaps the map zoom
    // The dash is switched off by the camera keys, the mirror key and every car reset: the mark
    // goes (hudmap.md, "Interlocks").
    if (session_->cameraKeyPressed() || session_->mirrorKeyPressed() || session_->carResetThisFrame())
        dashRestore_ = false;
    if (map_.state().mode() < 2) wideSaved_ = session_->wide();
    if (setup_.game && session_->leaveRequested()) {
        // In a game Esc opens the pause menu (single player: the game pauses); a lost or finished
        // race locks it out (races.md, "Losing, finishing and the menus").
        session_->clearLeave();
        if (!(race_ && (race_->lost() || race_->finished()))) {
            setPaused(setup_.singlePlayer, true);
            pauseMenu_ = true;
            pageEvent("pause", "{\"menu\":true}");
        }
    }
    if (race_) syncRaceHud();
    else hud_.update(session_->lastFrame());
    if (!race_) {
        // The water text: on the first frame of the water episode, and again every frame while
        // the timer before that frame's step is below 0.1 s (hud.md).
        const float waterAfter = session_->car().collision().waterTimer;
        if (waterAfter > 0.0f && waterBefore < 0.1f) postWaterText();
        waterPrev_ = waterAfter > 0.0f;
    }
    if (city_) {
        city_->update(dt);
        const auto& broken = city_->collision().brokenProps();
        if (broken.size() < brokenSeen_) {
            brokenSeen_ = 0;  // props came back (a reset)
            if (effects_) effects_->resetProps();
        }
        for (; brokenSeen_ < broken.size(); ++brokenSeen_) {
            const auto& objs = city_->data().objects();
            const int oi = broken[brokenSeen_];
            if (oi >= 0 && size_t(oi) < objs.size()) logInfo("knocked over %s", objs[size_t(oi)].name.c_str());
        }
    }
    automatic_ = session_->car().automatic();
    {
        const bool horn = session_->hornHeld();
        if (horn && !hornPrev_ && playerLights_.any()) playerLights_.on = !playerLights_.on;
        if (audio_) {
            if (horn && !hornPrev_) {
                audio_->hornPressed();
                if (playerLights_.any()) audio_->lightBarToggled(playerLights_.on);
            } else if (!horn && hornPrev_) {
                audio_->hornReleased();
            }
        }
        hornPrev_ = horn;
    }
    // Sound: set up once its data has arrived; then its frame (listener = the active camera; echo
    // while the player car's body is in an enclosed room).
    if (audioPending_ && ctx.soundReady) startAudio(ctx);
    bool echo = false;
    if (city_) {
        const CityData& cd = city_->data();
        echoRoom_ = cd.findRoom(session_->car().body().p, echoRoom_);
        if (echoRoom_ > 0 && size_t(echoRoom_) < cd.rooms().size()) echo = (cd.rooms()[size_t(echoRoom_)].flags & PsdlEnclosed) != 0;
    }
    echo_ = echo;
    if (audio_) {
        if (race_) audio_->setRaceClock(race_->countdown());
        audio_->viewSet(int(session_->cameras().active()));
        const CamPose& cam = session_->cameras().frame().pose;
        audio_->frame(dt < 0.1f ? dt : 0.1f, cam.p, cam.X, echo);
    }
    if (session_->takeMirrorToggle()) {
        mirrorOn_ = !mirrorOn_;
        pageEvent("mirror", mirrorOn_ ? "{\"on\":true}" : "{\"on\":false}");
    }
    if (race_) {
        raceHudTimer_ += dt;
        if (raceHudTimer_ >= 0.1f) {
            raceHudTimer_ = 0;
            pageEvent("race", raceJson());
        }
    }
    if (net_) {
        netFrameHud(session_->lastFrame());
        // The other players' horns and sirens (car-sounds.md, "Network cars").
        for (auto& nc : netCars_) {
            if (!nc->rc) continue;
            const bool horn = nc->rc->horn(), siren = nc->rc->siren();
            if (audio_ && horn != nc->horn) audio_->networkHorn(nc->rc->slot, horn);
            if (audio_ && siren != nc->siren) audio_->networkSiren(nc->rc->slot, siren);
            nc->horn = horn;
            nc->siren = siren;
            nc->lights.on = siren;
        }
        netHudTimer_ += dt;
        if (netHudTimer_ >= 0.1f) {
            netHudTimer_ = 0;
            int w = 1, h = 1;
            viewSize(ctx, w, h);
            pageEvent("netgame", netJson(w, h));
        }
    }
    hudTimer_ += dt;
    if (hudTimer_ >= 0.05f) {
        hudTimer_ = 0;
        int w = 1, h = 1;
        viewSize(ctx, w, h);
        pageEvent("hud", hudJson(w, h));
    }
}

void DriveView::viewSize(EngineContext& ctx, int& w, int& h) const {
    if (lastW_ > 0 && lastH_ > 0) {
        w = lastW_;
        h = lastH_;
    } else {
        ctx.platform->drawableSize(w, h);
    }
}

vec3 DriveView::cameraPosition() const { return session_ ? session_->cameras().frame().pose.p : vec3(); }

float DriveView::cameraYaw() const {
    if (!session_) return 0;
    const vec3& Z = session_->cameras().frame().pose.Z;
    return std::atan2(Z.x, Z.z);
}

std::string DriveView::readout() const {
    if (!session_) return std::string();
    const phys::Car& c = session_->car();
    const vec3 o = c.modelOrigin();
    std::string cops;
    if (city_ && city_->police().active()) {
        int n = 0;
        float nearest = 1e9f;
        for (const ai::Cop& cop : city_->police().cops())
            if (cop.state != ai::CopState::Idle && cop.state != ai::CopState::Out) {
                ++n;
                nearest = std::min(nearest, cop.range);
            }
        if (n > 0) cops = strf("  ·  %d cop%s chasing (%.0f m)", n, n == 1 ? "" : "s", double(nearest));
    }
    return strf("x %.1f  y %.1f  z %.1f  ·  %s camera%s  ·  room %d%s", double(o.x), double(o.y), double(o.z),
                session_->cameras().name(), city_ ? "" : "  ·  flat ground", session_->room(), cops.c_str());
}

DriveView::MusicCues DriveView::musicCues() const {
    // What the music reads from the last frame: the sound system's airborne flag (the player car's
    // tyre sound step) and chase count (traffic-sounds.md), and the echo state (sound-system.md).
    MusicCues m;
    if (!session_) return m;
    m.speed = session_->car().speed();
    if (audio_) {
        m.chase = audio_->chaseCount();
        m.airborne = audio_->airborne();
    }
    m.echo = echo_;
    return m;
}

std::string DriveView::mapName() const { return city_ ? city_->name() : std::string(); }

std::string DriveView::debugJson() const {
    JsonWriter j;
    j.beginObject();
    if (session_) {
        const phys::Car& c = session_->car();
        j.field("F", double(session_->lastFrame()));
        j.field("heading", double(session_->heading()));
        j.field("headingDeg", double(session_->heading()) * 57.29578);
        j.field("yawRate", double(session_->yawRate()));
        j.field("speed", double(c.signedSpeed()));
        j.field("gear", c.gearIndex() - 1);
        j.field("throttle", double(c.inputs().throttle));
        j.field("brake", double(c.inputs().brake));
        j.field("steer", double(c.inputs().steer));
        j.field("handbrake", double(c.inputs().handbrake));
        j.field("camera", std::string(session_->cameras().name()));
        // Damage and its visuals (damage-visuals.md).
        j.field("damage", double(c.damage().damage));
        j.field("maxDamage", double(c.damageLimit()));
        if (playerFx_) {
            j.field("markRounds", playerFx_->rounds());
            j.field("lastMarked", playerFx_->lastMarked());
            j.field("smokeLevel", playerFx_->smokeLevel());
            j.field("strength", double(playerFx_->strength()));
            j.field("textureDamage", playerFx_->textures().active());
            // Effects (effects.md).
            j.field("wheelParticles", playerFx_->wheelEmitter().count());
            j.field("smokeParticles", playerFx_->smokeEmitter().count());
            j.field("sparks", playerFx_->sparkCount());
            int skid = 0;
            for (int w = 0; w < 4; ++w) skid += (playerFx_->skids(w).head - playerFx_->skids(w).tail + 64) % 64;
            j.field("skidEntries", skid);
            j.field("slide0", double(c.wheel(0).slide));
            j.field("slide2", double(c.wheel(2).slide));
            j.field("ptx0", c.wheel(2).material.ptxIndex[0]);
            j.field("ptx1", c.wheel(2).material.ptxIndex[1]);
            if (effects_) {
                j.field("rainDrops", effects_->rain().count());
                j.field("rainShown", effects_->rainShown());
                j.field("props", int(effects_->props().size()));
            }
            if (city_) j.field("weather", city_->options().weather);
        }
        if (const phys::Trailer* t = session_->trailer()) {
            const vec3& tp = t->body().p;
            const vec3 tf = t->body().R.r[2];
            j.field("trailerAttached", t->attached());
            j.field("trailerLean", double(t->lean()));
            j.field("trailerX", double(tp.x));
            j.field("trailerY", double(tp.y));
            j.field("trailerZ", double(tp.z));
            j.field("trailerHeadingDeg", std::atan2(double(tf.x), double(tf.z)) * 57.29578);  // forward = −Z
            j.field("trailerRoom", t->room());
        }
        if (const ChaseCamera* ch = session_->cameras().activeChase()) {
            const vec3 d = session_->cameras().frame().pose.p - ch->trackPoint();
            j.field("camDist", double(length(d)));
            j.field("camMaxDist", double(ch->maxDist()));
            j.field("camRate", double(ch->approachRate()));
            j.field("camSwing", double(ch->swing()));
            j.field("camReversing", ch->reversing());
        }
    }
    j.endObject();
    return j.str();
}

std::string DriveView::policeJson() const {
    JsonWriter j;
    j.beginArray();
    if (city_)
        for (const ai::Cop& c : city_->police().cops()) {
            const vec3 p = c.car->body().p;
            j.beginObject();
            j.field("i", c.index);
            j.field("state", int(c.state));
            j.field("tactic", int(c.tactic));
            j.field("mode", int(c.driver.mode()));
            j.field("x", double(p.x));
            j.field("y", double(p.y));
            j.field("z", double(p.z));
            j.field("v", double(c.car->speed()));
            j.field("range", double(c.range));
            j.field("route", int(c.route.size()));
            j.field("w", c.driver.waypoint());
            j.field("S0", c.driver.window(0));
            j.field("thr", double(c.car->inputs().throttle));
            j.field("brk", double(c.car->inputs().brake));
            j.field("steer", double(c.car->inputs().steer));
            j.field("mover", c.mover);
            j.field("lights", c.lights.on);
            j.endObject();
        }
    j.endArray();
    return j.str();
}

void DriveView::render(EngineContext& ctx, int width, int height) {
    if (!session_) return;
    lastW_ = width;
    lastH_ = height;
    Renderer& r = *ctx.renderer;
    const int mode = map_.state().mode();
    if (mode == 3) {
        // Full-screen map, then the 3D view in the corner rectangle (cleared to black first).
        r.beginFrame(width, height, vec3(0, 0, 0));
        drawMap(ctx, width, height);
        const game::MapRect c = game::viewRect(3, width, height, map_.tune());
        r.setSubViewport(c.x, height - c.y - c.h, c.w, c.h, vec3(0, 0, 0));
        renderScene(ctx, width, height, c.x, height - c.y - c.h, c.w, c.h, false, session_->wide());
    } else if (mode == 2) {
        // The 3D view in the top half with the wide view's field of view, the map below.
        r.beginFrame(width, height, vec3(0, 0, 0));
        const game::MapRect v = game::viewRect(2, width, height, map_.tune());
        renderScene(ctx, width, height, 0, height - v.h, v.w, v.h, false, true);
        drawMap(ctx, width, height);
    } else {
        renderScene(ctx, width, height, 0, 0, width, height, session_->wide(), session_->wide());
        if (mode == 1) drawMap(ctx, width, height);
    }
    r.setDepthMode(false, 1000.0f);
}

void DriveView::drawMap(EngineContext& ctx, int width, int height) {
    if (!map_.available() || !session_) return;
    const game::MapRect rect = game::mapRect(map_.state().mode(), width, height, map_.tune(),
                                             rhd_ && session_->cameras().active() == CameraRig::Dash);
    const phys::Car& c = session_->car();
    std::vector<HudMapView::CarMarker> cars;
    // Police cars in red while the officer is doing something (not idle).
    if (city_)
        for (const ai::Cop& cop : city_->police().cops()) {
            if (cop.state == ai::CopState::Idle || cop.state == ai::CopState::Out || !cop.car) continue;
            HudMapView::CarMarker m;
            m.pos = cop.car->body().p;
            m.forward = cop.car->forward();
            m.rgb[0] = 255;
            m.rgb[1] = 0;
            m.rgb[2] = 0;
            cars.push_back(m);
        }
    for (const game::HudMapCar& o : hud_.mapCars) {
        HudMapView::CarMarker m;
        m.pos = o.pos;
        m.forward = o.forward;
        for (int k = 0; k < 3; ++k) m.rgb[k] = o.rgb[k];
        cars.push_back(m);
    }
    HudMapView::CarMarker me;
    me.pos = c.body().p;
    me.forward = c.forward();
    me.player = true;
    cars.push_back(me);
    std::vector<HudMapView::Dot> dots;
    for (const game::HudMapDot& d : hud_.mapDots) dots.push_back({d.pos, d.set});
    netMapItems(cars, dots);
    // P = the body's pose (centre of mass), Z = its backward axis.
    map_.draw(ctx, rect, width, height, c.body().p, c.rotation().r[2], dots, cars, mapCity_);
}

void DriveView::renderScene(EngineContext& ctx, int width, int height, int vx, int vy, int vw, int vh, bool letterbox,
                            bool wideFov) {
    Renderer& r = *ctx.renderer;
    const CameraFrame cf = session_->cameras().frame();
    const vec3 clear = city_ ? city_->clearColor() : vec3(0.55f, 0.62f, 0.7f);
    float fov = wideFov ? 70.0f : cf.fov;
    const bool fullScreen = vx == 0 && vy == 0 && vw == width && vh == height;
    if (letterbox) {
        // Letterbox: rows 0.18H..0.84H, vertical field of view 70 (cameras.md).
        r.beginFrame(width, height, vec3(0, 0, 0));
        vy = int(float(height) * 0.16f);
        vh = int(float(height) * 0.66f);
        r.setSubViewport(0, vy, width, vh, clear);
        fov = 70.0f;
    } else if (fullScreen) {
        r.beginFrame(width, height, clear);
    } else {
        r.setSubViewport(vx, vy, vw, vh, clear);
    }
    const float aspect = float(vw) / float(vh > 0 ? vh : 1);
    // Far plane (viewer.md, "Draw distance"): "MM2" (the default) is what MM2 sets in a game, the
    // driver's view distance from the profile (cameras.md, "Projection"; 600 m with the shipped
    // profile, 600 m outside a game), not the camera files' CameraFar; the city fog is clamped to
    // it. Our own 1 / 2 / 4 km and Full map (a browser setting, never in the .cfg) override the
    // profile's value, in a game too.
    const bool fullMap = ctx.drawDistance < 0;
    float farPlane = setup_.viewDistance > 0.0f ? setup_.viewDistance : 600.0f;
    if (ctx.drawDistance > 0) farPlane = float(ctx.drawDistance);
    else if (fullMap) farPlane = city_ ? city_->fullMapFar(cf.pose.p) : 3000.0f;
    if (ctx.drawDistance != 0) r.setDepthMode(true, farPlane);
    const CamPose& p = cf.pose;
    const mat4 view = mat4::lookAt(p.p, p.p - p.Z, p.Y);
    const mat4 proj = mat4::perspective(fov * kPi / 180.0f, aspect, cf.near, farPlane);
    r.setCamera(view, proj, p.p);

    CityCar car;
    car.model = session_->model();
    car.world = session_->carMatrix();
    car.paint = session_->paint();
    car.room = session_->room();
    car.bodyHidden = session_->cameras().bodyHidden();
    car.shadowHidden = car.bodyHidden;  // dash and bumper hide the whole car, shadow included (dash.md)
    car.wheelPose = session_->wheelPose();
    car.braking = session_->car().inputs().brake > 0.0f;
    car.reverse = session_->car().gearIndex() == 0;
    car.lightBar = &playerLights_;
    if (playerFx_) {
        playerFx_->refresh(session_->car());
        car.fx = playerFx_.get();
        car.wheelPose = carWheelPose(&session_->car(), session_->model(), playerFx_->wheelTilts());
    }
    CityCar trailer;
    const bool hasTrailer = trailerCar(trailer);

    if (city_) {
        std::vector<CityCar> cars{car};
        if (hasTrailer) cars.push_back(trailer);
        addOpponentCars(cars);
        addCopCars(cars);
        addNetCars(cars);
        city_->setEffects(fxOff_ ? nullptr : effects_.get(), true);
        if (fxOff_)
            for (CityCar& cc : cars) cc.fx = nullptr;
        city_->render(ctx, view, proj, p.p, farPlane, cars, fullMap);
        if (effects_) effects_->images().setCondition(ctx.textures->condition());  // the effects follow the city's textures
        drawGates(ctx);
        drawNetObjects(ctx);
        drawOverlays(ctx, width, height, cf, fov, aspect, farPlane);
        return;
    }
    if (effects_) effects_->images().setCondition(TextureCondition());
    // Flat ground: sky gradient, a large grid, the car and its shadow (day textures).
    ctx.textures->setCondition(TextureCondition());
    r.drawGradient(vec3(0.45f, 0.6f, 0.8f), vec3(0.8f, 0.84f, 0.88f));
    {
        std::vector<SimpleVertex> ground;
        const vec3 o = session_->car().modelOrigin();
        const float cx = std::floor(o.x / 100.0f) * 100.0f, cz = std::floor(o.z / 100.0f) * 100.0f;
        auto v = [](float x, float z, uint8_t c) {
            SimpleVertex s;
            s.pos = vec3(x, 0, z);
            s.color[0] = c; s.color[1] = c; s.color[2] = uint8_t(c + 3); s.color[3] = 255;
            return s;
        };
        const float R = 1500.0f;
        ground.push_back(v(cx - R, cz - R, 92));
        ground.push_back(v(cx + R, cz + R, 92));
        ground.push_back(v(cx + R, cz - R, 92));
        ground.push_back(v(cx - R, cz - R, 92));
        ground.push_back(v(cx - R, cz + R, 92));
        ground.push_back(v(cx + R, cz + R, 92));
        r.drawTriangles(ground, mat4::identity());
        std::vector<SimpleVertex> lines;
        for (int i = -30; i <= 30; ++i) {
            const uint8_t a = (i % 10 == 0) ? 160 : 70;
            auto l = [&](float x, float z) {
                SimpleVertex s;
                s.pos = vec3(x, 0.01f, z);
                s.color[0] = 40; s.color[1] = 44; s.color[2] = 50; s.color[3] = a;
                return s;
            };
            lines.push_back(l(cx + float(i) * 10.0f, cz - 300.0f));
            lines.push_back(l(cx + float(i) * 10.0f, cz + 300.0f));
            lines.push_back(l(cx - 300.0f, cz + float(i) * 10.0f));
            lines.push_back(l(cx + 300.0f, cz + float(i) * 10.0f));
        }
        r.drawLines(lines, mat4::identity());
    }
    if (effects_) drawSkidMarks(r, *ctx.textures, *effects_);  // the decal pass
    Lighting light;
    light.ambient = vec3(0.4f, 0.4f, 0.42f);
    light.count = 2;
    light.lights[0] = {normalize(vec3(0.45f, -0.75f, 0.55f)), vec3(0.75f, 0.73f, 0.68f)};
    light.lights[1] = {normalize(vec3(-0.6f, -0.3f, -0.5f)), vec3(0.25f, 0.27f, 0.32f)};
    r.setLighting(light);
    r.setFog(Fog());
    r.setTime(ctx.time);
    r.setEnvironment(ctx.textures->get("refl_dc"));
    auto drawFlat = [&](const CityCar& c, bool isTrailer) {
        std::vector<DrawPart> parts;
        const VehicleModel& m = *c.model;
        const int set = paintSetFor(c.paint, m.setCount());
        StillCarOptions so;
        if (!c.bodyHidden) {
            if (isTrailer) addTrailerParts(m, LodH, set, c.braking, parts, c.wheelPose);
            else addCarParts(m, LodH, set, vec3(), so, parts, c.wheelPose);
        }
        if (!c.shadowHidden) addStillShadow(m, set, vec3(), 0.0f, parts);
        const std::vector<const GpuTexture*>* damaged = c.fx ? damageSlotTextures(c.fx->textures()) : nullptr;
        for (const DrawPart& dp : parts) {
            ModelDraw md;
            md.gpu = ctx.meshes->get(*dp.mesh);
            md.pkg = dp.pkg;
            md.materialSet = dp.materialSet;
            md.flags = dp.flags;
            md.reflections = true;
            if (!(dp.flags & DrawShadow)) md.slotTextures = damaged;
            md.user = m.baseName.c_str();
            if (dp.flags & DrawShadow)
                md.model = mat4::translate(vec3(0, 0.02f, 0)) * mat4::scale(vec3(1, 0, 1)) * c.world;
            else
                md.model = dp.hasTransform ? c.world * dp.transform : c.world * mat4::translate(dp.offset);
            r.drawModel(md, *ctx.textures);
        }
    };
    drawFlat(car, false);
    if (hasTrailer) drawFlat(trailer, true);
    r.flush();
    if (effects_)
        drawEffectsPass(r, *ctx.textures, *effects_, vec3(view.m[0], view.m[4], view.m[8]),
                        vec3(view.m[1], view.m[5], view.m[9]));
    drawOverlays(ctx, width, height, cf, fov, aspect, farPlane);
}

void DriveView::drawOverlays(EngineContext& ctx, int width, int height, const CameraFrame& cf, float fov, float aspect,
                             float farPlane) {
    Renderer& r = *ctx.renderer;
    const phys::Car& c = session_->car();
    // Cockpit (dash.md): dash view only, not while a look key is held.
    if (session_->cameras().active() == CameraRig::Dash && !session_->cameras().lookingAround() && cockpit_.loaded()) {
        CockpitState st;
        st.speedMph = c.speedMph();
        st.rpm = c.rpm();
        st.gear = c.gearIndex();
        st.steer = c.inputs().steer;
        st.damage = c.damage().damage;  // damage.md: the needle shows damage against MaxDamage
        st.damageLimit = c.damageLimit();
        cockpit_.draw(ctx, cf.pose, fov, aspect, farPlane, st);
    }
    // HUD parts in the 3D view (hud.md): opponent markers and the direction arrow, hidden while
    // a look key is held or the game is paused.
    const bool hudHidden = session_->cameras().lookingAround() || paused_ ||
                           (race_ && (race_->endMenuOpen() || race_->resultsShown()));
    if (!hudHidden) {
        const mat4 view = mat4::lookAt(cf.pose.p, cf.pose.p - cf.pose.Z, cf.pose.Y);
        const mat4 proj = mat4::perspective(fov * kPi / 180.0f, aspect, cf.near, farPlane);
        r.setCamera(view, proj, cf.pose.p);
        if (iconsOn_ || hud_.layout().markersForced) {
            std::vector<WorldMarker> wm;
            for (const game::HudMarker& m : hud_.markers()) {
                WorldMarker w;
                w.pos = m.pos;
                for (int k = 0; k < 3; ++k) w.rgb[k] = m.rgb[k];
                wm.push_back(w);
            }
            drawWorldMarkers(ctx, cf.pose, wm);
        }
        if (hud_.arrowOn()) arrow_.draw(ctx, cf.pose, fov, aspect, hud_.arrowTarget());
    }
    // Rear-view mirror (dash.md): in every view while on; cities only (flat ground has nothing
    // behind but the grid). Not over the full-screen map.
    if (!mirrorOn_ || !city_ || map_.state().mode() == 3) return;
    const int w = int(float(width) * mirror_.sizeX), h = int(float(height) * mirror_.sizeY);
    if (w <= 0 || h <= 0) return;
    const int left = width - w - 1, top = 1;
    r.setSubViewport(left, height - top - h, w, h, vec3(0, 0, 0));
    // Camera: the car model's pose as drawn (body rotation, origin = body position + R·CoG) with a
    // frame at Position whose Z is the car's −Z (so the picture is mirrored left-to-right),
    // looking along the car's +Z. The city centres its sky dome on this eye (dash.md).
    const phys::M3& R = c.rotation();
    const vec3 X = R.r[0], Y = R.r[1], Zc = R.r[2];
    const vec3& q = mirror_.position;
    const vec3 eye = c.modelOrigin() + X * q.x + Y * q.y + Zc * q.z;
    const vec3 Z = Zc * -1.0f;
    mat4 view = mat4::identity();
    const vec3* rows[3] = {&X, &Y, &Z};
    for (int i = 0; i < 3; ++i) {
        view.at(i, 0) = rows[i]->x;
        view.at(i, 1) = rows[i]->y;
        view.at(i, 2) = rows[i]->z;
        view.at(i, 3) = -dot(*rows[i], eye);
    }
    const mat4 proj = mat4::perspective(mirror_.fov * kPi / 180.0f, mirror_.aspect, mirror_.nearClip, mirror_.farClip);
    r.setDepthMode(false, mirror_.farClip);
    r.setCamera(view, proj, eye);
    r.setMirrored(true);
    std::vector<CityCar> cops;
    CityCar trailer;
    if (trailerCar(trailer)) cops.push_back(trailer);
    addOpponentCars(cops);
    addCopCars(cops);
    addNetCars(cops);
    city_->setEffects(effects_.get(), false);  // the mirror does not move the rain
    city_->render(ctx, view, proj, eye, mirror_.farClip, cops);  // the player's car is hidden
    city_->setEffects(effects_.get(), true);
    r.setMirrored(false);
}

}  // namespace cc
