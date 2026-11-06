#include "net/settings.h"

#include <cmath>

#include "formats/race_files.h"
#include "game/race_catalogue.h"
#include "vfs/vfs.h"

namespace cc {
namespace net {

bool isRaceMode(int mode) { return mode == ModeCheckpoint || mode == ModeCircuit || mode == ModeBlitz; }

int Settings::limitMinutes() const {
    static const int m[4] = {5, 10, 20, 30};
    return m[limitChoice & 3];
}

int Settings::limitPoints() const {
    static const int p[4] = {100, 250, 500, 1000};
    return p[limitChoice & 3];
}

uint8_t Settings::copsByte() const {
    return uint8_t(((subMode & 3) << 6) | ((goldWeight & 3) << 4) | ((limitType & 3) << 2) | (limitChoice & 3));
}

void Settings::setCopsByte(uint8_t b) {
    subMode = (b >> 6) & 3;
    goldWeight = (b >> 4) & 3;
    limitType = (b >> 2) & 3;
    limitChoice = b & 3;
}

// Our layout of the three words (the bit widths are MM2's):
//   w0: mode 4 | race 4 | time of day 4 | weather 4 | skill 2 | no damage 1
//   w1: laps 4 | time limit 8 (bits 4 – 11), or the Cops and Robbers byte (bits 0 – 7) in mode 2
//   w2: pedestrian density byte
PackedSettings pack(const Settings& s) {
    PackedSettings p;
    int mode = s.mode;
    if (mode < 0 || mode > 4) mode = ModeCruise;  // 5 and 6 are never used; a Crash Course becomes Cruise
    const bool noDamage = mode == ModeCops ? false : s.noDamage;
    p.w[0] = uint32_t(mode & 15) | uint32_t(s.race & 15) << 4 | uint32_t(s.timeOfDay & 15) << 8 |
             uint32_t(s.weather & 15) << 12 | uint32_t(s.skill & 3) << 16 | uint32_t(noDamage ? 1 : 0) << 18;
    if (mode == ModeCops) {
        p.w[1] = s.copsByte();
    } else {
        // Truncated to a whole number, then 8 bits (larger values arrive mod 256).
        const long long t = (long long)std::trunc(double(s.timeLimit));
        p.w[1] = uint32_t(s.laps & 15) | uint32_t(uint64_t(t) & 0xFFu) << 4;
    }
    float d = s.pedDensity;
    int b = int(std::trunc(d * 255.0f));
    p.w[2] = uint32_t(b) & 0xFFu;
    p.city = s.city;
    return p;
}

Settings unpack(const PackedSettings& p) {
    Settings s;
    s.mode = int(p.w[0] & 15);
    if (s.mode > 4) s.mode = ModeCruise;
    s.race = int((p.w[0] >> 4) & 15);
    s.timeOfDay = int((p.w[0] >> 8) & 15);
    s.weather = int((p.w[0] >> 12) & 15);
    s.skill = int((p.w[0] >> 16) & 3);
    s.noDamage = ((p.w[0] >> 18) & 1) != 0;
    if (s.mode == ModeCops) {
        s.setCopsByte(uint8_t(p.w[1] & 0xFF));
        s.laps = 0;
        s.timeLimit = 0;
        s.noDamage = false;
    } else {
        s.laps = int(p.w[1] & 15);
        s.timeLimit = float((p.w[1] >> 4) & 0xFF);
    }
    s.pedDensity = float(p.w[2] & 0xFF) * 0.003921569f;
    s.city = p.city;
    return s;
}

Settings limited(const Settings& s) { return unpack(pack(s)); }

std::string netRaceName(int mode, int race) {
    switch (mode) {
        case ModeCheckpoint: return game::raceName(game::RaceMode::Checkpoint, race);
        case ModeCircuit: return game::raceName(game::RaceMode::Circuit, race);
        case ModeBlitz: return game::raceName(game::RaceMode::Blitz, race);
        case ModeCops: return "multicop";
        default: return "roam";
    }
}

Settings hostDefaults(const Vfs& vfs, int mode, const std::string& city, int race, int skill, const Settings& current) {
    Settings s = mode == ModeCops ? current : Settings();
    if (mode == ModeCops) {
        s.mode = mode;
        s.city = city;
        s.skill = skill;
        s.race = 0;
        return limited(s);
    }
    s.mode = mode;
    s.city = city;
    s.skill = skill;
    s.race = isRaceMode(mode) ? race : 0;
    s.pedDensity = 0.25f;
    s.timeOfDay = 1;
    s.weather = 0;
    s.laps = 1;
    s.timeLimit = 0;
    if (isRaceMode(mode)) {
        const game::RaceMode gm = mode == ModeCheckpoint ? game::RaceMode::Checkpoint
                                  : mode == ModeCircuit  ? game::RaceMode::Circuit
                                                         : game::RaceMode::Blitz;
        BytesPtr b = vfs.open(game::settingsPath(city, gm));
        if (b) {
            const auto rows = parseRaceData(std::string(b->begin(), b->end()));
            if (race >= 0 && size_t(race) < rows.size()) {
                const RaceSettings& r = rows[size_t(race)].of(skill != 0);
                s.timeOfDay = r.timeOfDay;
                s.weather = r.weather;
                s.pedDensity = r.peds;
                s.timeLimit = r.timeLimit;
                s.laps = mode == ModeCircuit ? r.numLaps : mode == ModeBlitz ? 0 : 1;
            }
        }
        if (mode == ModeBlitz) s.laps = 0;
        else if (mode == ModeCheckpoint) s.laps = 1;
    }
    return limited(s);
}

}  // namespace net
}  // namespace cc
