// MM2's player files (spec/formats/profiles.md): players.dir, <file>.sav, <file>.cfg,
// <city>/<file>.rec, <city>/amateur.dat and pro.dat, and race/<c>/<c>_rewards.csv. Byte layouts,
// the check value (a reflected CRC-32 with polynomial 0xED7282A0) and the reading rules. What the
// game does with them is game/progression.h; the folder and MM2's save moments are
// game/profile_store.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/bytes.h"

namespace cc {
namespace game {

// ---------------------------------------------------------------------------------------------
// Check value (profiles.md, "Check value").

class Mm2Crc {
public:
    void add(const uint8_t* p, size_t n);
    void add(const Bytes& b, size_t from, size_t count) { add(b.data() + from, count); }
    uint32_t value() const { return ~s_; }

private:
    uint32_t s_ = 0xFFFFFFFFu;
};
uint32_t mm2Crc(const uint8_t* p, size_t n);

// Text fields: Windows-1252 bytes in the files, UTF-8 in the engine and on the page.
std::string cp1252ToUtf8(const std::string& s);
std::string utf8ToCp1252(const std::string& s);  // characters without a Windows-1252 byte become '?'

// ---------------------------------------------------------------------------------------------
// Game mode numbers (profiles.md, "Game mode numbers").

enum ModeNumber { ModeCruise = 0, ModeCheckpoint = 1, ModeCopsRobbers = 2, ModeCircuit = 3, ModeBlitz = 4,
                ModeMultiCruise = 5, ModeCrash = 6 };

// The four record types in file order (checkpoint, circuit, blitz, crash course); the hall of
// fame has the first three. −1 for a mode that keeps no records.
enum RecordType { RecCheckpoint = 0, RecCircuit = 1, RecBlitz = 2, RecCrash = 3, RecTypeCount = 4 };
int recordTypeOfMode(int mode);
int modeOfRecordType(int type);

constexpr int kCrashCourseCount = 13;  // always 13 slots, whatever the city
constexpr int kMaxDrivers = 18;

// ---------------------------------------------------------------------------------------------
// players/players.dir

struct DriverEntry {
    std::string name;  // UTF-8
    std::string file;  // "player0", ...
};

struct PlayersDir {
    std::vector<DriverEntry> drivers;  // in creation order
    std::string last;                  // name of the driver used last
};
// False when MM2 treats the file as absent (missing, version not 6, no drivers, too short).
bool readPlayersDir(const Bytes& b, PlayersDir& out);
Bytes writePlayersDir(const PlayersDir& d);
// The file name a new driver gets (profiles.md, "Rules for the list").
std::string newDriverFileName(const PlayersDir& d);

// ---------------------------------------------------------------------------------------------
// players/<file>.sav

struct SaveFile {
    uint32_t version = 3;
    uint32_t storedCrc = 0;  // as read
    float stamp = 0.0f;
    int skill = 0;           // 0 Amateur, 1 Professional
    std::string car;         // BaseName; empty for a driver that never left the menus
    int32_t paint = 0;
    int32_t mode = 0;
    int32_t race = 0;
    std::string name, netName, fileName, city, address;
};
enum class SaveRead { Ok, Missing, BadVersion, BadCrc, TooShort };
SaveRead readSave(const Bytes& b, SaveFile& out);
Bytes writeSave(const SaveFile& s);  // always version 3 with its check value
uint32_t saveCrc(const Bytes& b);    // check value over a version-3 file's covered fields

// ---------------------------------------------------------------------------------------------
// players/<file>.cfg

struct Binding {
    uint32_t kind = 0, device = 0, component = 0;
};

struct ConfigFile {
    // Audio.
    float sfxVolume = 1.0f, musicVolume = 1.0f, balance = 0.0f;
    uint32_t audioBits = 0xC73;
    uint32_t channels = 32;
    std::string device{'\0'};  // the sound device field as stored: n bytes, ending with its zero
    std::string deviceName() const { return device.substr(0, device.find('\0')); }
    // Graphics: 14 four-byte fields kept as stored (accessors below give their meaning).
    uint32_t graphics[14] = {};
    // Controls: 13 four-byte fields kept as stored.
    uint32_t controls[13] = {};
    Binding bindings[170];
    uint8_t view[12] = {};

    ConfigFile();  // program defaults (profiles.md tables)

    // Graphics fields (profiles.md, "Graphics options").
    int textureQuality() const { return int(graphics[0]); }
    int objectDetail() const { return int(graphics[1]); }
    bool reflections() const { return graphics[3] != 0; }
    int cloudShadows() const { return int(graphics[4]); }
    bool texturedSky() const { return graphics[5] != 0; }
    float viewDistance() const { return asFloat(graphics[6]); }
    float lighting() const { return asFloat(graphics[7]); }
    bool pedestriansOff() const { return graphics[9] != 0; }
    void setTextureQuality(int v) { graphics[0] = uint32_t(v); }
    void setObjectDetail(int v) {
        graphics[1] = uint32_t(v);
        graphics[8] = asBits(float(v) / 3.0f + 0.25f);
    }
    void setReflections(bool v) { graphics[3] = v ? 1u : 0u; }
    void setCloudShadows(int v) { graphics[4] = uint32_t(v); }
    void setTexturedSky(bool v) { graphics[5] = v ? 1u : 0u; }
    void setViewDistance(float v) { graphics[6] = asBits(v); }
    void setLighting(float v) { graphics[7] = asBits(v); }
    void setPedestriansOff(bool v) { graphics[9] = v ? 1u : 0u; }

    // Control fields (profiles.md, "Control options").
    int controlDevice() const { return int(controls[0]); }
    bool automatic() const { return controls[1] != 0; }
    float realism() const { return asFloat(controls[2]); }
    bool autoReverse() const { return controls[3] != 0; }
    bool hatSwitch() const { return controls[4] != 0; }
    bool forceFeedback() const { return controls[5] != 0; }
    float ffCollision() const { return asFloat(controls[6]); }
    float ffRoad() const { return asFloat(controls[7]); }
    float deadZone() const { return asFloat(controls[9]); }
    float keyboardTarget() const { return asFloat(controls[11]); }
    float steeringFactor() const { return asFloat(controls[12]); }  // U = 1 / sensitivity
    void setControlDevice(int v) { controls[0] = uint32_t(v); }
    void setAutomatic(bool v) { controls[1] = v ? 1u : 0u; }
    void setRealism(float v) { controls[2] = asBits(v); }
    void setAutoReverse(bool v) { controls[3] = v ? 1u : 0u; }
    void setHatSwitch(bool v) { controls[4] = v ? 1u : 0u; }
    void setForceFeedback(bool v) { controls[5] = v ? 1u : 0u; }
    void setFfCollision(float v) { controls[6] = asBits(v); }
    void setFfRoad(float v) { controls[7] = asBits(v); }
    void setDeadZone(float v) { controls[9] = asBits(v); }
    void setSteeringFactor(float v) { controls[12] = asBits(v); }

    static float asFloat(uint32_t u);
    static uint32_t asBits(float f);
};
// MM2's default binding table of all five devices (controls.md, "Default binding table"):
// `controllerButtons` = buttons of the game controller found at start-up (0 without one); the
// wheel uses buttons 3 – 6 only above 5 buttons and 7, 8 only above 7.
void defaultBindings(Binding (&t)[170], int controllerButtons);

enum class CfgRead { Ok, Missing, OtherVersion, Damaged };
// OtherVersion: only the 8-byte header was valid; `out` is left as it was (the options in effect).
CfgRead readConfig(const Bytes& b, ConfigFile& out);
Bytes writeConfig(const ConfigFile& c);
// The view bytes of a new driver (profiles.md): bytes 10 and 11 are kept from `current`.
void newDriverViewBytes(uint8_t view[12], const uint8_t current[12]);

// ---------------------------------------------------------------------------------------------
// players/<city>/<file>.rec

struct RaceRecord {
    uint32_t storedCrc = 0;
    bool crcOk = true;      // as read
    float time = 0.0f;      // best time (Circuit: best lap); 0 = no result yet
    std::string car;
    int32_t passed = 0;
    int32_t score = 0;
};

struct RecordFile {
    float version = 2.0f;
    uint32_t storedCrc = 0;
    bool headerCrcOk = true;  // as read
    float stamp = 0.0f;
    int32_t counts[4] = {};   // checkpoint, circuit, blitz, crash course
    uint32_t passed[4] = {};
    std::vector<RaceRecord> records;  // sum of the counts, in type order

    int recordIndex(int type, int race) const;  // −1 when out of range
    RaceRecord* record(int type, int race);
    const RaceRecord* record(int type, int race) const;
    int passedCount(int type) const;  // set bits among 0 … counts[type] − 1
};
enum class RecRead { Ok, Missing, BadVersion, BadCrc, TooShort };
// Reads the header and every record (each record's crcOk says whether its check value matched).
RecRead readRecord(const Bytes& b, RecordFile& out);
Bytes writeRecord(const RecordFile& r);
RecordFile freshRecord(float stamp, int checkpoint, int circuit, int blitz);
uint32_t recordEntryCrc(const RaceRecord& r);  // check value of an entry written by writeRecord

// ---------------------------------------------------------------------------------------------
// players/<city>/amateur.dat and pro.dat

struct FameEntry {
    uint32_t storedCrc = 0;
    bool crcOk = true;
    std::string name, car;
    float value = 0.0f;   // time, or score as a float; 0 = empty
    uint32_t passed = 0;
};

struct HallOfFame {
    uint32_t version = 1;
    int32_t counts[3] = {};          // checkpoint, circuit, blitz
    std::vector<FameEntry> entries;  // 10 per race; entry 2k = time rank k, 2k + 1 = score rank k

    int entryIndex(int type, int race, int e) const;
};
enum class FameRead { Ok, Missing, BadVersion, TooShort };
FameRead readHallOfFame(const Bytes& b, HallOfFame& out);
Bytes writeHallOfFame(const HallOfFame& h);
HallOfFame freshHallOfFame(int checkpoint, int circuit, int blitz);
uint32_t fameEntryCrc(const FameEntry& e);

// ---------------------------------------------------------------------------------------------
// race/<c>/<c>_rewards.csv

struct RewardLine {
    int mode = 0;         // game mode number of the race type word
    int condition = 0;    // kHalf, kAll, or a race number ≥ 0
    std::string car;      // BaseName
    int variant = 0;      // 0 = the car itself; v > 0 = paint job v
    std::string message;  // as read (line end removed)
    static constexpr int kHalf = -1, kAll = -2;
};
// `carExists(baseName)` filters lines naming unknown cars; at most 32 lines are used.
template <typename F>
std::vector<RewardLine> parseRewards(const std::string& text, F carExists);
std::vector<RewardLine> parseRewardsAll(const std::string& text);  // without the car filter

template <typename F>
std::vector<RewardLine> parseRewards(const std::string& text, F carExists) {
    std::vector<RewardLine> out;
    for (RewardLine& l : parseRewardsAll(text)) {
        if (!carExists(l.car)) continue;
        if (out.size() >= 32) break;
        out.push_back(std::move(l));
    }
    return out;
}

// Base64 for the page's storage of the player files.
std::string base64Encode(const Bytes& b);
bool base64Decode(const std::string& s, Bytes& out);

}  // namespace game
}  // namespace cc
