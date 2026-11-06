// The player's `players` folder and MM2's moments of reading and writing it
// (spec/game/progression.md, "Saving"; spec/formats/profiles.md, "Importing an MM2 profile").
// The folder is a map of relative paths (lower case: "players.dir", "player0.sav",
// "sf/player0.rec", "sf/amateur.dat") to file bytes; every write is reported through onWrite so
// the page can keep it (browser storage).
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "game/player_files.h"
#include "game/progression.h"

namespace cc {
namespace game {

class ProfileStore {
public:
    // What the store needs to know about the loaded game data.
    struct Env {
        std::vector<CityRaces> cities;  // the city list
        std::vector<CarRef> cars;       // the car list
        // Text of race/<c>/<c>_rewards.csv ("" when it cannot be read).
        std::function<std::string(const std::string& city)> rewardsText;
        // mmlang.dll text by number ("" when missing or no file loaded).
        std::function<std::string(int id)> text;
    };
    void setEnv(Env e);
    const Env& env() const { return env_; }
    // The game clock in seconds (stamps of new drivers).
    void setClock(float seconds) { clock_ = seconds; }
    // Buttons of the game controller found at start-up (the wheel's default bindings).
    void setControllerButtons(int n) { controllerButtons_ = n; }
    int controllerButtons() const { return controllerButtons_; }

    // ---- the folder
    using Files = std::map<std::string, Bytes>;
    const Files& files() const { return files_; }
    void putFileQuiet(const std::string& path, const Bytes& b) { files_[key(path)] = b; }  // loading, no event
    void clearQuiet() { files_.clear(); }
    std::function<void(const std::string& path, const Bytes* data)> onWrite;  // data null: deleted
    const Bytes* file(const std::string& path) const;

    // ---- MM2's moments
    // Program start: the default driver if players.dir is unreadable, the halls of fame of london
    // and sf, then the last-used driver.
    void programStart(const std::string& currentCityRaceDir);
    const PlayersDir& drivers() const { return dir_; }
    int current() const { return current_; }
    const SaveFile& save() const { return save_; }
    SaveFile& save() { return save_; }
    // The options in effect (the current driver's .cfg once applied).
    const ConfigFile& options() const { return options_; }
    ConfigFile& options() { return options_; }

    bool selectDriver(int index, std::string& why);
    // Refusals give `why` ("" for an empty name, which MM2 ignores silently).
    bool newDriver(const std::string& name, int skill, std::string& why);
    bool deleteDriver(int index, std::string& why);
    // Options screen closed with "done", and the game end (with the current view bytes).
    void writeOptions();
    // Leaving the menus to play (and "next race"): the .sav (car, paint, city, mode, race) and .cfg.
    void writeForPlay(const std::string& car, int paint, const std::string& cityRaceDir, int mode, int race);
    // The current driver's network name (multiplayer.md 1.1), written with the next writeForPlay.
    void setNetName(const std::string& name) {
        if (current_ >= 0 && !name.empty()) save_.netName = name;
    }

    // ---- records
    // The current driver's valid record file of a city (null: none or not valid). Reading repairs
    // bad records (written back), as MM2 does.
    const RecordFile* record(const std::string& raceDir);
    // "Driver records shown": empties record files whose counts differ from the city's.
    void driverRecordsShown();
    // "Race statistics shown for a city": the city's halls of fame, created or rebuilt as needed.
    // skill 0 amateur.dat, 1 pro.dat; null when unreadable.
    const HallOfFame* hallOfFame(const std::string& raceDir, int skill, bool create);
    Locks locks();  // sf then london (progression.md, "Locked cars and paint jobs")
    Totals totals();

    // ---- a game
    struct GameStart {
        bool active = false;
        std::string raceDir, mapName;
        int mode = 0, race = 0, skill = 0;
        bool recordsOn = false;  // the record file was valid at game start
        bool fameOn = false;     // the hall of fame of the skill level could be read
        Locks locks;             // current city only (what the reward check sees)
    };
    void gameStart(const std::string& raceDir, int mode, int race);
    const GameStart& game() const { return game_; }
    void gameEnd(const uint8_t view[12]);
    // A chat cheat code was typed: nothing is recorded until the program ends.
    void markCheat() { cheated_ = true; }
    bool cheated() const { return cheated_; }

    struct Outcome {
        bool recorded = false;       // into the record file
        bool fameChanged = false;
        int score = 0;
        bool passed = false;
        bool reward = false;
        RewardLine rewardLine;
        std::string why;             // when nothing was recorded
    };
    // The race code calls this the moment the player's result is final.
    Outcome recordResult(const RaceResult& r);
    // The same with the record the race session worked out itself (results.md: time, passed,
    // score; Circuit: the stored lap slots 1 … laps, 0 = not stored). `ownSettings`: the race was
    // started with its own settings (else nothing is recorded).
    struct FinalResult {
        int mode = ModeCheckpoint;
        int race = 0;
        float time = 0.0f;
        bool passed = false;
        int score = 0;
        std::string car;
        std::vector<float> laps;
        bool ownSettings = true;
    };
    Outcome recordFinal(const FinalResult& f);
    // Whether the current driver has passed a race of the running game's city (RaceRecords).
    bool passedBefore(int mode, int race) const;
    bool raceOpenNow(int mode, int race) const;

    // ---- import (profiles.md, "Importing an MM2 profile")
    struct ImportReport {
        bool ok = false;
        int drivers = 0;
        std::vector<std::string> notes;
    };
    // `src`: the player's players folder (relative paths, any case). Replaces this folder.
    ImportReport importFolder(const Files& src);

    // City of the list by RaceDir (null when absent).
    const CityRaces* city(const std::string& raceDir) const;
    const CarRef* car(const std::string& baseName) const;

private:
    static std::string key(const std::string& p);
    void write(const std::string& path, const Bytes& b);
    void remove(const std::string& path);
    void writeDir();
    void writeSave();
    void writeCfg();
    std::string textOr(int id, const char* fallback) const;
    std::vector<RewardLine> rewards(const std::string& city) const;
    Outcome applyRecord(int mode, int race, const RaceRecord& nw, const std::vector<float>& laps);
    bool loadRecord(const std::string& raceDir, RecordFile& out) const;  // header valid, stamp equal
    void ensureRecords();  // "driver selected": fresh record files where not valid
    void ensureHallsOfFame();
    void createDefaultDriver(const std::string& currentCity);
    void deleteDriverFiles(const DriverEntry& d);

    Env env_;
    float clock_ = 0.0f;
    int controllerButtons_ = 0;
    Files files_;
    PlayersDir dir_;
    int current_ = -1;
    SaveFile save_;
    ConfigFile options_;
    bool cfgReadAtStart_ = false;
    GameStart game_;
    bool cheated_ = false;
    std::map<std::string, RecordFile> recCache_;
};

}  // namespace game
}  // namespace cc
