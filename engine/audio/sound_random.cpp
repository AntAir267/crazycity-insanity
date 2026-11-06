#include "audio/sound_random.h"

#include <cstdint>
#include <ctime>

namespace cc {
namespace audio {

namespace {
long long g_pinned = -1;

// 32-bit signed wrap-around, as the original's `long` arithmetic.
inline int32_t wrap(int64_t v) { return int32_t(uint32_t(uint64_t(v))); }
}  // namespace

void pinSoundClock(long long seconds) { g_pinned = seconds; }

long long soundClock() {
    if (g_pinned >= 0) return g_pinned;
    return static_cast<long long>(std::time(nullptr));
}

double soundUnitForSeed(long long seed) {
    const int32_t MBIG = 1000000000, MSEED = 161803398;
    int32_t ma[56] = {};
    const int32_t s32 = wrap(seed);
    const int32_t absSeed = s32 < 0 ? wrap(-int64_t(s32)) : s32;
    // mj = (MSEED − |seed|) as a 32-bit value, then the C remainder (sign kept, no absolute value).
    int32_t mj = wrap(int64_t(MSEED) - int64_t(absSeed));
    mj = mj % MBIG;
    ma[55] = mj;
    int32_t mk = 1;
    for (int i = 1; i <= 54; ++i) {
        const int ii = (21 * i) % 55;
        ma[ii] = mk;
        mk = wrap(int64_t(mj) - int64_t(mk));
        if (mk < 0) mk = wrap(int64_t(mk) + MBIG);
        mj = ma[ii];
    }
    for (int k = 1; k <= 4; ++k)
        for (int i = 1; i <= 55; ++i) {
            ma[i] = wrap(int64_t(ma[i]) - int64_t(ma[1 + (i + 30) % 55]));
            if (ma[i] < 0) ma[i] = wrap(int64_t(ma[i]) + MBIG);
        }
    int inext = 0, inextp = 31;
    // The first draw.
    if (++inext == 56) inext = 1;
    if (++inextp == 56) inextp = 1;
    mj = wrap(int64_t(ma[inext]) - int64_t(ma[inextp]));
    if (mj < 0) mj = wrap(int64_t(mj) + MBIG);
    ma[inext] = mj;
    return double(mj) * 1e-9;
}

double soundUnit() { return soundUnitForSeed(soundClock()); }

double rndS(float a) {
    const double r = soundUnit();
    return ((double(a) * double(0.01f)) * r) * 100.0;
}

double rndS(float lo, float hi) {
    const double r = soundUnit();
    const double lo1 = double(float(double(lo) * double(0.01f)));
    return ((double(hi) * double(0.01f) - lo1) * r + lo1) * 100.0;
}

}  // namespace audio
}  // namespace cc
