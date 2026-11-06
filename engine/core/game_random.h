// MM2's global random generator (spec/engine/controls.md, "Free-roam start"; vehicle-wheels.md):
// state = state × 214013 + 2531011 (mod 2³²), value = (state >> 16) & 0x7FFF. The physics bumps
// and the AI's road choices draw from the same state.
#pragma once

#include <cstdint>

namespace cc {

// A generator of the same kind with its own state (the traffic and police generators).
class Lcg {
public:
    explicit Lcg(uint32_t seed = 1) : x_(seed) {}
    void reset(uint32_t seed) { x_ = seed; }
    int next15() {
        x_ = x_ * 214013u + 2531011u;
        return int((x_ >> 16) & 0x7fff);
    }
    float unit() { return float(next15()) * 3.0517578e-05f; }  // value × 3.0517578e-05
    uint32_t state() const { return x_; }

private:
    uint32_t x_;
};

int gameRandom15();                   // next 15-bit value
float gameRandomUnit();               // value / 32768 (in [0, 1))
void resetGameRandom(uint32_t seed);  // the AI map reseeds to 1 at every reset
uint32_t gameRandomState();

// The effects generator (physics/collision.md, "Random numbers"; formats/particles.md): MM2's
// second state of the same formula, used by every visual effect (texture damage marks, smoke
// point, particles, sparks, chips). It starts at 0 when the program starts and is never restarted
// by the game; the physics never reads it.
int effectsRandom15();          // raw 15-bit value
float effectsRandomUnit();      // value x 3.0517578e-05, in [0, 1)
uint32_t effectsRandomState();
void setEffectsRandomState(uint32_t x);  // tests only

}  // namespace cc
