// The car audio of the player's car, opponents, cops and network cars (spec/audio/car-sounds.md):
// engine samples, gear click, horn, siren, semi/bus extras; with the tyre and impact sounds of
// tyre-impact-sounds.md (surface loops, skids, suspension thump, tyre wobble, impacts) and the
// airborne flag the music reads.
#pragma once

#include <string>
#include <vector>

#include "audio/audio_world.h"
#include "physics/car.h"

namespace cc {
namespace audio {

class CarAudio : public Source3D, public phys::ImpactSink {
public:
    enum class Owner { Player, Ai, Network };
    explicit CarAudio(AudioWorld& w);
    ~CarAudio() override;

    // "Setting up a car's audio". False: the car makes no sound at all. The player's car takes its
    // slot at once (its horn and click levels are set then).
    bool setup(phys::Car* car, const std::string& name, Owner owner, bool lightBar);
    bool active() const { return active_; }
    Owner owner() const { return owner_; }
    CarAudioKind kind() const { return kind_; }
    const std::string& name() const { return name_; }
    phys::Car* car() const { return car_; }

    // Every frame after the car's physics: the position follows the body and a slot is asked for.
    void postPhysics();
    // Player: the horn control (on a light-bar car each press toggles the siren instead).
    void hornPress();
    void hornRelease();
    bool hornHeld() const { return hornHeld_; }
    // The light bar was toggled by the horn (player): siren on or off, uncounted.
    void lightBarToggled(bool on);
    // Police-listed cars (car-sounds.md, "Siren").
    void sirenStart(bool counted);
    void sirenStop();
    void explosion();
    bool sirenSounding() const { return siren_.mode != SirenMode::Off; }
    // Car reset (respawn, world reset).
    void carReset();
    // Checkpoint / Blitz: the player's engine silenced while damaged out.
    void setSilenced(bool s) { silenced_ = s; }
    bool airborne() const { return airborne_; }

    // phys::ImpactSink: an impact that reached the damage count step.
    void carImpact(const vec3& J, int soundId) override;

    // Test access.
    int currentBlock() const { return cur_; }
    int sirenCurrent() const { return siren_.current; }
    bool sirenSamplePlaying() const;
    bool windingDown() const { return siren_.windDown; }
    int lastImpactEntry() const { return lastImpact_; }
    int impactsPlayed() const { return impactsPlayed_; }

protected:
    void onAssigned(int slot) override;
    void onReleased() override;
    void slotUpdate(float dt) override;

private:
    enum class SirenMode { Off, Counted, Uncounted };
    struct SirenState {
        SirenMode mode = SirenMode::Off;
        int current = 0;
        std::vector<int> stepIndex;
        float timer = 0;
        float level = 0;
        bool windDown = false;
        float t = 0;
        bool explosionFlag = false;
    };
    void setEchoAll(bool on);
    void tickEcho(float dt);
    void gearClick();
    void engineStep(float dt);
    void tyreStep(float dt);
    void impactStep();
    void semiStep();
    void sirenUpdate(float dt);
    void windDownStep(float dt);
    void stopEngine();
    void stopTyreLoops();
    void airborneTest();
    Sample* at(const SlotSample& s) const { return s.at(slot_); }
    float sirenVolume(int i) const;
    bool explosionPlaying() const;

    AudioWorld& w_;
    phys::Car* car_ = nullptr;
    std::string name_;
    Owner owner_ = Owner::Ai;
    CarAudioKind kind_ = CarAudioKind::Plain;
    bool active_ = false;
    bool lightBar_ = false;
    CarData data_;
    std::vector<SlotSample> engine_;
    SlotSample horn_, clutch_;
    // Police.
    std::vector<SlotSample> sirenSamples_;
    SlotSample explosion_;
    SirenState siren_;
    // Semi/bus.
    SlotSample beeper_, airBlow_;
    float beeperVol_ = 0.87f, airVol_ = 0.9f;
    // Tyres and impacts.
    ImpactTable* impacts_ = nullptr;
    int lastImpact_ = -1;
    int impactsPlayed_ = 0;
    int cur_ = 0, prev_ = -1;
    bool skidding_ = false;
    bool airborne_ = false;
    float wobbleAcc_ = 0, circ_ = 0;
    // State.
    int prevGear_ = -1;
    bool hornHeld_ = false;
    bool silenced_ = false;
    bool echoFlag_ = false;
    bool levelsSet_ = false;
};

}  // namespace audio
}  // namespace cc
