// The sound random numbers (spec/audio/sound-system.md, "Sound random numbers"): every draw seeds
// a fresh subtractive generator (Knuth / Numerical Recipes "ran3") from the wall clock in whole
// seconds and takes its first number, so all draws within one second are equal. Sounds never use
// the game-wide generators.
#pragma once

namespace cc {
namespace audio {

// Check runs and tests may pin the clock (seconds since 1970); a negative value restores the real
// clock (the C time() value).
void pinSoundClock(long long seconds);
long long soundClock();

// r of a fresh generator seeded with `seed` (0 <= r < 1 for every seed the clock gives).
double soundUnitForSeed(long long seed);
// r for the current clock.
double soundUnit();

// rnd_s(a) = ((a × 0.01) × r) × 100 and rnd_s(lo, hi) = ((hi × 0.01 − lo′) × r + lo′) × 100 with
// lo′ = lo × 0.01 rounded to single precision; 0.01 is the single-precision constant. The result
// keeps double precision (the caller rounds when it stores).
double rndS(float a);
double rndS(float lo, float hi);

}  // namespace audio
}  // namespace cc
