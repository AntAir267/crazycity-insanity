// The sound of one game (one drive): set-up in MM2's order, the events the game hands over, and
// the frame order of spec/audio/sound-system.md ("Order within a frame").
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ai/opponents.h"
#include "ai/police.h"
#include "ai/traffic.h"
#include "audio/audio_world.h"
#include "audio/car_audio.h"
#include "audio/speech.h"
#include "audio/traffic_audio.h"
#include "audio/world_audio.h"

namespace cc {
namespace audio {

struct GameAudioConfig {
    std::string city;  // MapName; "" for the flat ground
    int weather = 0, timeOfDay = 1;
    int mode = 0;    // MM2 game mode number: 0 Cruise, 1 Checkpoint, 3 Circuit, 4 Blitz, 6 Crash Course
    int course = 0;  // the race (Crash Course: the course) index
    std::string playerCar;
    bool playerLightBar = false;
    bool ambient = true;      // option 0x800, read at game set-up only
    bool commentary = true;   // option 0x400
    bool minimalInstall = false;  // mm2audex.ar (voices 2 and up) is not loaded
};

class GameAudio : public ai::PoliceSoundHooks {
public:
    GameAudio(SoundSystem& sys, const Vfs& vfs);
    ~GameAudio() override;

    // The 3D manager, the player's car audio (with the city ambience) and the speech.
    void setup(const GameAudioConfig& cfg, phys::Car& player, phys::World* world);
    // The traffic pool was created (setupTraffic) or is about to go (clearTraffic); the same for
    // the police (their cars are deleted with them).
    void setupTraffic(ai::TrafficSystem* ts);
    void clearTraffic();
    void setupPolice(ai::PoliceSystem* ps);
    void clearPolice();
    // Race opponents (computer-driven cars of the race data): after the player's car, before the
    // traffic (car-sounds.md, "Owner kinds"); clearOpponents() before they go.
    void setupOpponents(ai::OpponentSystem* os);
    void clearOpponents();
    // The pedestrian pool was created (or is about to go); after each pedestrian update of the AI
    // frame, pedFrame() runs the slot release rule and the scream events.
    void setupPeds(const ai::PedestrianSystem* ps);
    void clearPeds();
    void pedFrame(const ai::PedestrianSystem& ps);
    // A race session's named event (game/race.h: sound files, speech lines, engineSilence).
    void raceEvent(const std::string& name, int value, int value2);
    // The race clock the timer warning reads (time left), each frame before frame().
    void setRaceClock(float timeLeft) { raceClock_ = timeLeft; }
    // A paused frame: only the speech runs (speech.md, "Queue slots").
    void pausedFrame(float dt);

    // Network cars (car-sounds.md, owner kind "network"): other players' cars in a network game,
    // by the caller's key; their horn and siren bits start and stop the horn and the siren
    // (uncounted).
    void addNetworkCar(int key, phys::Car* car, const std::string& name, bool lightBar);
    void removeNetworkCar(int key);
    void networkHorn(int key, bool on);
    void networkSiren(int key, bool on);

    // Events.
    void aiFrameStart(float F, float playerSpeed);
    void trafficImpact(int car, const vec3& J, int otherSound);
    void hornPressed();
    void hornReleased();
    void lightBarToggled(bool on);
    void playerCarReset();
    void gameReset();          // game start or restart: the pre-race line
    void viewSet(int view);    // the player's camera view (CameraRig::View numbers)

    // Every frame after the game's frame (the mixer's update follows).
    void frame(float dt, const vec3& listenerPos, const vec3& listenerRight, bool echo);

    int chaseCount() const { return w_.chaseCount; }
    bool airborne() const { return player_ && player_->airborne(); }
    AudioWorld& world() { return w_; }
    CarAudio* playerAudio() const { return player_.get(); }
    RaceOneShots& oneShots() { return oneShots_; }
    RaceSpeech& speech() { return speech_; }
    CrashCourseSpeech& crashSpeech() { return cc_; }
    WorldAudio& worldSounds() { return world_; }
    TrafficAudio* trafficAudio() const { return trafficAudio_.get(); }
    CarAudio* copAudioAt(int index) const { return copAudio(index); }
    std::string statsJson() const;

    // ai::PoliceSoundHooks.
    void sirenStart(const ai::Cop& c, bool localPlayer) override;
    void sirenStop(const ai::Cop& c) override;
    void explosion(const ai::Cop& c) override;

private:
    CarAudio* copAudio(int index) const;
    AudioWorld w_;
    GameAudioConfig cfg_;
    std::unique_ptr<CarAudio> player_;
    ai::TrafficSystem* traffic_ = nullptr;
    std::unique_ptr<TrafficAudio> trafficAudio_;
    ai::PoliceSystem* police_ = nullptr;
    std::vector<std::unique_ptr<CarAudio>> cops_;  // by cop index
    ai::OpponentSystem* opponents_ = nullptr;
    std::vector<std::unique_ptr<CarAudio>> opps_;
    std::vector<std::pair<int, std::unique_ptr<CarAudio>>> network_;  // by key
    WorldAudio world_;
    RaceSpeech speech_;
    CrashCourseSpeech cc_;
    RaceOneShots oneShots_;
    bool timerWarning_ = false;
    float raceClock_ = 0;
    int frame_ = 0;
    int view_ = -1;
};

}  // namespace audio
}  // namespace cc
