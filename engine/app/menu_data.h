// The front end's data (no drawing, no GL): the player's profiles (a players folder kept by the
// host's storage), the mmlang.dll strings, the race and car menus' data, the options, the race
// records a game writes to. Replies are JSON (the shape the web front end read; the engine's own
// front end reads them back with core/json_value.h). Used by the app (app/game_api.cpp) and by
// cctool's UI tests.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "formats/cinfo.h"
#include "formats/mmlang.h"
#include "game/profile_store.h"
#include "game/race.h"

namespace cc {

class Vfs;

namespace menu {

// A car of the viewer's car list (the index the views and the network use).
struct ListCar {
    std::string baseName;
    bool menu = true;   // in the menu's car list (not an unlisted package or traffic car)
    int infoFlags = 0;  // .info Flags (8: a police car)
};

struct Env {
    const Vfs* vfs = nullptr;
    std::function<const std::vector<CityEntry>&()> cities;  // the city list
    std::function<std::vector<ListCar>()> cars;             // the viewer's car list
    std::function<double()> clock;                           // program seconds
};
void setEnv(const Env& e);
const Env& env();

// ---- strings (formats/mmlang.md)
std::string loadMmlang(const uint8_t* data, size_t size);  // {ok, why?, strings?}
const StringTable& strings();
std::string text(int id);

// ---- the players folder
void playersClear();
void setControllerButtons(int n);
bool playersPut(const std::string& path, const std::string& b64);
// Every write of the profile store (data null: deleted).
void setFileSink(std::function<void(const std::string& path, const Bytes* data)> f);
std::string start();  // program start: {state}
std::string state();
std::string selectDriver(int i);
std::string newDriver(const std::string& name, int skill);
std::string deleteDriver(int i);
void importBegin();
void importFile(const std::string& path, const uint8_t* data, size_t size);
std::string importFinish();
bool started();

// ---- options (the current driver's .cfg)
void setOption(const std::string& name, double v);
// One entry of the binding table (customise controls, controls.md): device 0 – 4, action 0 – 33,
// kind (1 held, 2 analog, 3 press; 0 = none), input device (2 mouse, 3 keyboard, 4 controller),
// component. Kept like the other options until "Done" writes the .cfg; false when out of range.
bool setBinding(int device, int action, int kind, int inputDevice, int component);
std::string optionsDone();
std::string optionsCancel();
std::string optionsDefaults(const std::string& screen);

// ---- menus
std::string cityRaces(int cityIndex);
std::string cars();
std::string driverRecord();
std::string raceStats(int cityIndex, int skill);

// ---- a game's records
game::ProfileStore& store();
// The race session's records (the current driver's files).
struct Records : game::RaceRecords {
    bool ownSettings = true;
    game::ProfileStore::Outcome last;
    bool haveLast = false;
    bool hasDriver() const override;
    bool cheated() const override;
    bool passedBefore(game::RaceMode m, int race, bool pro) const override;
    bool raceOpen(game::RaceMode m, int race, bool pro) const override;
    void record(const game::RaceOutcome& r) override;
};
Records& records();
std::string lastOutcome();
// The race-code hook and the page's test path: a final result by hand.
std::string recordResult(int mode, int race, int place, int finished, double time, const std::string& laps, int crashPassed,
                         int ownSettings, double difficulty);
// RaceDir of the running game's city.
std::string& lastCity();

}  // namespace menu
}  // namespace cc
