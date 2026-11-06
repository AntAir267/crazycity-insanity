// World sounds (spec/audio/world-sounds.md): the city ambience objects, the ambient objects owned
// by moving level objects (ferries, drawbridge leaves, Underground trains; hooks for milestone 5),
// cable cars, and the rain and thunder of a rainy game.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/audio_world.h"

namespace cc {
namespace audio {

// An ambient file (aud/ambient/<name>.csv).
struct AmbientRow {
    std::string name;
    float volume = 0;
    int type = 0;  // 0 loop, 1 2D one-shot, 2 3D one-shot, 3 3D parameters only
    float low = 0, high = 0;
    int active = 0;
    float minSpeed = 0, maxSpeed = 0;
    int doppler = 0;
};
struct AmbientData {
    bool found = false;
    float minD = 0, maxD = 0;
    int priority = 0;
    int area = 0;  // 0 everywhere, 1 only in the echo state, 2 only outside it
    std::vector<AmbientRow> rows;
    std::vector<vec3> points;
};
AmbientData loadAmbientFile(const Vfs& vfs, const std::string& name);
// The city's list: aud/ambient/<london|sf>ambientcontainer.csv (exact, case-sensitive names).
std::vector<std::string> ambientList(const Vfs& vfs, const std::string& city);

class AmbientObject : public Source3D {
public:
    AmbientObject(AudioWorld& w, const AmbientData& d);
    ~AmbientObject() override;
    // "Object update" with the owner's speed (0 for every owner in MM2).
    void objectUpdate(float speed);
    // Owners: rows on and off (0-based), and the owner's reset.
    void setRow(int row, bool on);
    void ownerReset();
    const AmbientData& data() const { return d_; }
    int chosenPoint() const { return point_; }
    // Test access: the timer of row i.
    float rowTimer(int i) const { return i >= 0 && size_t(i) < rows_.size() ? rows_[size_t(i)].timer : 0.0f; }

protected:
    void onReleased() override;
    void slotUpdate(float dt) override;

private:
    struct Row {
        AmbientRow d;
        SlotSample smp;
        float timer = 0;
        bool active = false;
    };
    void apply3D(Row& r);
    void runTimer(Row& r, float dt);
    void fire(Row& r);
    AudioWorld& w_;
    AmbientData d_;
    std::vector<Row> rows_;
    bool echoFlag_ = false;
    float speed_ = 0;
    int point_ = -1;
};

// A cable car's sound (world-sounds.md, "Cable cars"); its owner is milestone 5.
class CableCarSound : public Source3D {
public:
    CableCarSound(AudioWorld& w, float initialSpeed);
    // In the cable car's own update, before it moves.
    void ownerFrame(const vec3& pos, float speed);
    int state() const { return state_; }

protected:
    void onReleased() override;
    void slotUpdate(float dt) override;

private:
    AudioWorld& w_;
    SlotSample start_, loop_, stop_, bell_, cable_;
    int state_ = 0, prevState_ = 0;
    float oldSpeed_ = 0, speed_ = 0;
};

// Rain and thunder (plain 2D samples, no slot).
class RainAudio {
public:
    RainAudio(AudioWorld& w, bool night);
    // The player's view was set: inside for the bumper and dash views.
    void viewSet(bool inside);
    // Every frame in the player's update (not paused).
    void frame(float dt);
    float thunderTimer() const { return t_; }
    const Sample* thunder(int i) const { return i == 0 ? thunder1_.get() : thunder2_.get(); }

private:
    AudioWorld& w_;
    bool night_ = false;
    std::unique_ptr<Sample> exterior_, interior_, thunder1_, thunder2_;
    Sample* current_ = nullptr;
    bool inside_ = false, muted_ = false, phase_ = false;
    float t_ = 0;
};

// Everything world-sounds.md makes for one game.
class WorldAudio {
public:
    explicit WorldAudio(AudioWorld& w) : w_(w) {}
    // Game set-up: the city's ambient objects (if `ambientOption`), rain and thunder.
    void setup(bool ambientOption);
    // The 3D manager's update: every city object's object update (before the slot holders).
    void updateObjects();
    void viewSet(bool inside);
    void playerFrame(float dt);
    // Owned objects (ferries, bridge leaves, trains): made by their owners.
    AmbientObject* makeOwned(const std::string& file);
    const std::vector<std::unique_ptr<AmbientObject>>& cityObjects() const { return city_; }
    RainAudio* rain() const { return rain_.get(); }

private:
    AudioWorld& w_;
    std::vector<std::unique_ptr<AmbientObject>> city_, owned_;
    std::unique_ptr<RainAudio> rain_;
};

}  // namespace audio
}  // namespace cc
