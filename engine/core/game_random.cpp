#include "core/game_random.h"

namespace cc {

namespace {
uint32_t g_state = 1;
uint32_t g_effects = 0;
}

int gameRandom15() {
    g_state = g_state * 214013u + 2531011u;
    return int((g_state >> 16) & 0x7fff);
}

float gameRandomUnit() { return float(gameRandom15()) / 32768.0f; }

void resetGameRandom(uint32_t seed) { g_state = seed; }

uint32_t gameRandomState() { return g_state; }

int effectsRandom15() {
    g_effects = g_effects * 214013u + 2531011u;
    return int((g_effects >> 16) & 0x7fff);
}

float effectsRandomUnit() { return float(effectsRandom15()) * 3.0517578e-05f; }

uint32_t effectsRandomState() { return g_effects; }

void setEffectsRandomState(uint32_t x) { g_effects = x; }

}  // namespace cc
