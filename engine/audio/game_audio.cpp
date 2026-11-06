#include "audio/game_audio.h"

#include <cmath>

#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"

namespace cc {
namespace audio {

GameAudio::GameAudio(SoundSystem& sys, const Vfs& vfs)
    : w_(sys, vfs), world_(w_), speech_(sys), cc_(sys), oneShots_(sys) {
    trafficAudio_ = std::make_unique<TrafficAudio>(w_);
}

GameAudio::~GameAudio() {
    clearTraffic();
    clearPeds();
    clearPolice();
    clearOpponents();
    for (int k = 0; k < Manager3D::kSlots; ++k)
        if (Source3D* s = w_.mgr.holder(k)) w_.mgr.release(*s);
    player_.reset();
    w_.sys.stopEverything();
}

void GameAudio::setup(const GameAudioConfig& cfg, phys::Car& player, phys::World* world) {
    cfg_ = cfg;
    w_.city = cfg.city;
    w_.london = cfg.city == "london";
    w_.weather = cfg.weather;
    w_.timeOfDay = cfg.timeOfDay;
    w_.world = world;
    w_.chaseCount = 0;
    w_.airLatch = false;
    w_.mgr.enable();
    w_.resetImpactCache();
    // Before any car: the vehicle types and the shared tyre files; the traffic's count files
    // (before the streets are set up).
    w_.loadSharedCarFiles();
    TrafficAudio::readCountFiles(w_);
    // The player's car (the 3D manager exists): the city ambience, then the car audio.
    world_.setup(cfg.ambient);
    player_ = std::make_unique<CarAudio>(w_);
    player_->label = "player " + cfg.playerCar;
    if (!player_->setup(&player, cfg.playerCar, CarAudio::Owner::Player, cfg.playerLightBar)) player_.reset();
    // Speech after the player's car (the race announcer, or the Crash Course instructor); the
    // mode's one-shots.
    if (cfg.commentary) {
        if (cfg.mode == 6) cc_.setup(w_.vfs, cfg.city, cfg.course);
        else speech_.setup(w_.vfs, cfg.city, cfg.mode, cfg.weather, cfg.timeOfDay, cfg.playerCar, cfg.minimalInstall);
    }
    oneShots_.setup(cfg.mode);
}

void GameAudio::setupTraffic(ai::TrafficSystem* ts) {
    clearTraffic();
    if (!ts || !ts->active()) return;
    traffic_ = ts;
    trafficAudio_->setup(*ts);
}

void GameAudio::clearTraffic() {
    if (trafficAudio_) trafficAudio_->clear();
    traffic_ = nullptr;
}

void GameAudio::setupOpponents(ai::OpponentSystem* os) {
    clearOpponents();
    if (!os) return;
    opponents_ = os;
    for (ai::Opponent& o : os->opponents()) {
        auto a = std::make_unique<CarAudio>(w_);
        a->label = "opponent " + std::to_string(o.index) + " " + o.row.car;
        if (o.car && a->setup(o.car.get(), o.row.car, CarAudio::Owner::Ai, false)) opps_.push_back(std::move(a));
    }
}

void GameAudio::clearOpponents() {
    for (auto& a : opps_) a->releaseSlot();
    opps_.clear();
    opponents_ = nullptr;
}

void GameAudio::setupPeds(const ai::PedestrianSystem* ps) {
    clearPeds();
    if (ps && ps->active()) trafficAudio_->setupPeds(*ps);
}

void GameAudio::clearPeds() {
    if (trafficAudio_) trafficAudio_->clearPeds();
}

void GameAudio::pedFrame(const ai::PedestrianSystem& ps) {
    if (ps.active()) trafficAudio_->pedFrame(ps);
}

void GameAudio::raceEvent(const std::string& n, int v, int v2) {
    // Speech (speech.md) and the car (car-sounds.md, "Silencing").
    if (n == "prerace" || n == "ccPrerace") {
        // A game start or restart: the 3D manager on again, the engine sound restored.
        w_.mgr.enable();
        if (player_) player_->setSilenced(false);
        if (n == "prerace") speech_.preRace();
        else cc_.preRace();
    } else if (n == "finalCheckpoint") {
        speech_.finalCheckpoint();
    } else if (n == "damagePenalty") {
        speech_.damagePenalty();
    } else if (n == "resultsPoor") {
        speech_.results(10, 10);  // Blitz out of time: as p = 10, n = 10
    } else if (n == "results") {
        // The results screen: every slot released, the 3D manager off until the next reset.
        w_.mgr.disable();
        speech_.results(v, v2);
    } else if (n == "ccGate") {
        cc_.gatePassed(v);
    } else if (n == "ccPass" || n == "ccFail") {
        cc_.result(n == "ccPass");
    } else if (n == "engineSilence") {
        if (player_) player_->setSilenced(true);
    } else if (n == "TimerwarningStart") {
        timerWarning_ = true;
    } else if (n == "TimerwarningStop") {
        timerWarning_ = false;
        oneShots_.stopTimerWarning();
    } else if (oneShots_.has(n)) {
        oneShots_.play(n);  // the bank's files and the gate samples, by name
    }
}

void GameAudio::pausedFrame(float dt) {
    speech_.update(dt);
    cc_.update(dt);
}

void GameAudio::setupPolice(ai::PoliceSystem* ps) {
    clearPolice();
    if (!ps || !ps->active()) return;
    police_ = ps;
    for (const ai::Cop& c : ps->cops()) {
        if (c.index >= int(cops_.size())) cops_.resize(size_t(c.index) + 1);
        auto a = std::make_unique<CarAudio>(w_);
        a->label = "cop " + std::to_string(c.index) + " " + c.entry.model;
        if (a->setup(c.car.get(), c.entry.model, CarAudio::Owner::Ai, c.lights.any())) cops_[size_t(c.index)] = std::move(a);
    }
    ps->setSoundHooks(this);
}

void GameAudio::clearPolice() {
    if (police_) police_->setSoundHooks(nullptr);
    for (auto& c : cops_)
        if (c) c->releaseSlot();
    cops_.clear();
    police_ = nullptr;
}

void GameAudio::addNetworkCar(int key, phys::Car* car, const std::string& name, bool lightBar) {
    removeNetworkCar(key);
    auto a = std::make_unique<CarAudio>(w_);
    a->label = "network " + std::to_string(key) + " " + name;
    if (!a->setup(car, name, CarAudio::Owner::Network, lightBar)) return;
    network_.emplace_back(key, std::move(a));
}

void GameAudio::removeNetworkCar(int key) {
    for (size_t i = 0; i < network_.size(); ++i)
        if (network_[i].first == key) {
            network_[i].second->releaseSlot();
            network_.erase(network_.begin() + long(i));
            return;
        }
}

void GameAudio::networkHorn(int key, bool on) {
    for (auto& n : network_)
        if (n.first == key) {
            if (on) n.second->hornPress();
            else n.second->hornRelease();
        }
}

void GameAudio::networkSiren(int key, bool on) {
    for (auto& n : network_)
        if (n.first == key) {
            if (on) n.second->sirenStart(false);
            else n.second->sirenStop();
        }
}

CarAudio* GameAudio::copAudio(int index) const {
    return index >= 0 && size_t(index) < cops_.size() ? cops_[size_t(index)].get() : nullptr;
}

void GameAudio::aiFrameStart(float F, float playerSpeed) {
    w_.frameTime = F;
    if (trafficAudio_) trafficAudio_->aiFrameStart(F, playerSpeed);
}

void GameAudio::trafficImpact(int car, const vec3& J, int otherSound) {
    if (trafficAudio_) trafficAudio_->impact(car, J, otherSound);
}

void GameAudio::hornPressed() {
    if (player_) player_->hornPress();
}

void GameAudio::hornReleased() {
    if (player_) player_->hornRelease();
}

void GameAudio::lightBarToggled(bool on) {
    if (player_) player_->lightBarToggled(on);
}

void GameAudio::playerCarReset() {
    if (player_) player_->carReset();
}

void GameAudio::gameReset() {
    w_.mgr.enable();
    speech_.preRace();
}

void GameAudio::viewSet(int view) {
    if (view == view_) return;
    view_ = view;
    // Inside for the bumper (1) and dash (3) views.
    world_.viewSet(view == 1 || view == 3);
}

void GameAudio::sirenStart(const ai::Cop& c, bool localPlayer) {
    if (CarAudio* a = copAudio(c.index)) a->sirenStart(localPlayer);
}

void GameAudio::sirenStop(const ai::Cop& c) {
    if (CarAudio* a = copAudio(c.index)) a->sirenStop();
}

void GameAudio::explosion(const ai::Cop& c) {
    if (CarAudio* a = copAudio(c.index)) a->explosion();
}

void GameAudio::frame(float dt, const vec3& listenerPos, const vec3& listenerRight, bool echo) {
    w_.frameTime = dt;
    // 1. The cars' post-physics updates (position, slot requests).
    if (player_) player_->postPhysics();
    for (auto& a : opps_) a->postPhysics();
    if (police_)
        for (const ai::Cop& c : police_->cops())
            if (CarAudio* a = copAudio(c.index)) a->postPhysics();
    for (auto& n : network_) n.second->postPhysics();
    // 2. The player's update: listener, echo state; rain and thunder.
    w_.mgr.setListener(listenerPos, listenerRight);
    w_.mgr.echo = echo;
    world_.playerFrame(dt);
    // 3. The 3D manager: ambient objects, then every slot holder in slot order.
    if (w_.mgr.enabled()) {
        world_.updateObjects();
        w_.mgr.updateHolders(dt);
    }
    // Timer warning (race clock below 10 s; looped below 3 s).
    if (timerWarning_) oneShots_.timerWarning(raceClock_, dt);
    // 4. Speech.
    speech_.update(dt);
    cc_.update(dt);
    ++frame_;
}

std::string GameAudio::statsJson() const {
    JsonWriter j;
    j.beginObject();
    j.key("slots").beginArray();
    for (int k = 0; k < Manager3D::kSlots; ++k) {
        const Source3D* s = w_.mgr.holder(k);
        if (!s) {
            j.value(std::string());
            continue;
        }
        j.value(s->twoD ? s->label : strf("%s (%.0f m, A %.2f)", s->label.c_str(), double(std::sqrt(s->d2)), double(s->A)));
    }
    j.endArray();
    j.field("chaseCount", w_.chaseCount);
    j.field("echo", w_.mgr.echo);
    j.field("airborne", airborne());
    j.field("ambientObjects", int(world_.cityObjects().size()));
    j.field("rain", world_.rain() != nullptr);
    j.field("speech", speech_.active() ? speech_.voiceName() : cc_.active() ? std::string("instructor") : std::string());
    j.field("trafficSounding", trafficAudio_ ? trafficAudio_->soundingCars() : 0);
    if (trafficAudio_) {
        const TrafficAudio::Counts& c = trafficAudio_->counts();
        j.field("honks", c.honks);
        j.field("stuckHorns", c.stuckHorns);
        j.field("shouts", c.shouts);
        j.field("trafficImpacts", c.impactSounds);
        j.field("pedScreamEvents", c.screamEvents);
        j.field("pedScreams", c.screams);
        j.field("pedVoiceFiles", trafficAudio_->pedVoiceFiles());
    }
    j.field("playerImpacts", player_ ? player_->impactsPlayed() : 0);
    j.field("cops", int(cops_.size()));
    j.endObject();
    return j.str();
}

}  // namespace audio
}  // namespace cc
