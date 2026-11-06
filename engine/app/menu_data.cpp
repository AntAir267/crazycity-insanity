#include "app/menu_data.h"

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/car_info.h"
#include "formats/race_files.h"
#include "game/race_catalogue.h"
#include "vfs/vfs.h"

namespace cc {
namespace menu {

using namespace cc::game;

namespace {

struct State {
    Env env;
    StringTable strings;
    ProfileStore store;
    std::vector<CarInfo> carInfo;  // the menu car list with its .info values
    ProfileStore::Files import;
    Records records;
    bool started = false;
    std::string lastCity;  // RaceDir of the city of the running game
    std::function<void(const std::string&, const Bytes*)> sink;
};
State& st() {
    static State s;
    return s;
}

// The race settings rows of a city's mode (races.md, "The race settings file"), read with the
// race code's reader (formats/race_files.h), for the race menu's defaults.
struct RaceRow {
    bool present = false;
    int timeOfDay = 1, weather = 0, opponents = 0, cops = 0, laps = 1;
    float ambient = 0.5f, peds = 0.25f, timeLimit = 0.0f, difficulty = 1.0f;
};

std::vector<RaceRow> raceRows(const std::string& raceDir, game::RaceMode m, int skill) {
    std::vector<RaceRow> out;
    if (!st().env.vfs) return out;
    BytesPtr b = st().env.vfs->open(game::settingsPath(raceDir, m));
    if (!b) return out;
    for (const RaceDataRow& d : parseRaceData(std::string(b->begin(), b->end()))) {
        const RaceSettings& v = d.of(skill != 0);
        RaceRow r;
        r.present = true;
        r.timeOfDay = v.timeOfDay;
        r.weather = v.weather;
        r.opponents = v.opponents;
        r.cops = v.cops;
        r.ambient = v.ambient;
        r.peds = v.peds;
        r.laps = v.numLaps;
        r.timeLimit = v.timeLimit;
        r.difficulty = v.difficulty;
        out.push_back(r);
    }
    return out;
}

std::vector<CityRaces> cityList() {
    std::vector<CityRaces> out;
    if (!st().env.cities) return out;
    for (const CityEntry& e : st().env.cities()) {
        CityRaces c;
        c.localizedName = cp1252ToUtf8(e.localizedName);
        c.mapName = e.mapName;
        c.raceDir = e.raceDir;
        c.checkpoint = e.checkpoint;
        c.circuit = e.circuit;
        c.blitz = e.blitz;
        for (const auto& n : e.checkpointNames) c.checkpointNames.push_back(cp1252ToUtf8(n));
        for (const auto& n : e.circuitNames) c.circuitNames.push_back(cp1252ToUtf8(n));
        for (const auto& n : e.blitzNames) c.blitzNames.push_back(cp1252ToUtf8(n));
        c.testLayout = e.testLayout;
        out.push_back(c);
    }
    return out;
}

void setupEnv() {
    State& u = st();
    u.carInfo = u.env.vfs ? buildCarList(*u.env.vfs) : std::vector<CarInfo>();
    ProfileStore::Env env;
    env.cities = cityList();
    for (const CarInfo& c : u.carInfo) {
        CarRef r;
        r.baseName = c.baseName;
        r.colors = int(c.colors.size());
        r.scoringBias = float(std::atof(c.info.get("ScoringBias").c_str()));
        r.description = c.description;
        env.cars.push_back(r);
    }
    env.rewardsText = [](const std::string& c) {
        BytesPtr b = st().env.vfs ? st().env.vfs->open("race/" + c + "/" + c + "_rewards.csv") : nullptr;
        return b ? std::string(b->begin(), b->end()) : std::string();
    };
    env.text = [](int id) { return text(id); };
    u.store.setEnv(env);
    u.store.onWrite = [](const std::string& path, const Bytes* data) {
        if (st().sink) st().sink(path, data);
    };
}

void optionsJson(JsonWriter& j, const ConfigFile& c) {
    j.key("options").beginObject();
    j.field("sfxVolume", double(c.sfxVolume));
    j.field("musicVolume", double(c.musicVolume));
    j.field("balance", double(c.balance));
    j.field("audioBits", int(c.audioBits));
    j.field("channels", int(c.channels));
    j.field("device", cp1252ToUtf8(c.deviceName()));
    j.field("textureQuality", c.textureQuality());
    j.field("objectDetail", c.objectDetail());
    j.field("reflections", c.reflections());
    j.field("cloudShadows", c.cloudShadows());
    j.field("texturedSky", c.texturedSky());
    j.field("viewDistance", double(c.viewDistance()));
    j.field("lighting", double(c.lighting()));
    j.field("pedestriansOff", c.pedestriansOff());
    j.field("controlDevice", c.controlDevice());
    j.field("automatic", c.automatic());
    j.field("autoReverse", c.autoReverse());
    j.field("hatSwitch", c.hatSwitch());
    j.field("forceFeedback", c.forceFeedback());
    j.field("ffCollision", double(c.ffCollision()));
    j.field("ffRoad", double(c.ffRoad()));
    j.field("deadZone", double(c.deadZone()));
    j.field("steeringSensitivity", double(c.steeringFactor() > 0.0f ? 1.0f / c.steeringFactor() : 1.0f));
    j.key("view").beginArray();
    for (int i = 0; i < 12; ++i) j.value(int(c.view[i]));
    j.endArray();
    j.key("bindings").beginArray();
    for (int k = 0; k < 170; ++k)
        j.beginArray().value(int(c.bindings[k].kind)).value(int(c.bindings[k].device)).value(int(c.bindings[k].component)).endArray();
    j.endArray();
    j.endObject();
}

std::string stateJson() {
    State& u = st();
    ProfileStore& s = u.store;
    JsonWriter j;
    j.beginObject();
    j.field("started", u.started);
    j.field("strings", u.strings.loaded());
    j.key("drivers").beginArray();
    for (const DriverEntry& d : s.drivers().drivers) j.value(d.name);
    j.endArray();
    j.field("current", s.current());
    if (s.current() >= 0) {
        const SaveFile& sv = s.save();
        j.key("driver").beginObject();
        j.field("name", sv.name);
        j.field("netName", sv.netName);
        j.field("file", sv.fileName);
        j.field("skill", sv.skill);
        j.field("car", sv.car);
        j.field("paint", int(sv.paint));
        j.field("mode", int(sv.mode));
        j.field("race", int(sv.race));
        j.field("city", sv.city);
        const CarRef* c = s.car(sv.car);
        j.field("carDescription", c ? c->description : sv.car);
        // The last race's name: Cruise (text 80), Cops and Robbers (79), a crash course (78),
        // else the race's name from the city's lists.
        std::string raceName;
        const CityRaces* city = s.city(sv.city);
        const int t = recordTypeOfMode(sv.mode);
        if (city && t >= 0 && t < 3) {
            const auto* names = city->names(t);
            if (names && sv.race >= 0 && size_t(sv.race) < names->size()) raceName = (*names)[size_t(sv.race)];
        }
        j.field("raceName", raceName);
        j.field("cityName", city ? city->localizedName : sv.city);
        const Totals tot = s.totals();
        j.field("totalScore", tot.total);
        j.endObject();
    }
    optionsJson(j, s.options());
    j.endObject();
    return j.str();
}

const CityRaces* cityAt(int index, std::vector<CityRaces>& keep) {
    keep = cityList();
    if (index < 0 || size_t(index) >= keep.size()) return nullptr;
    return &keep[size_t(index)];
}

void rowJson(JsonWriter& j, const RaceRow& r) {
    j.beginObject();
    j.field("present", r.present);
    j.field("timeOfDay", r.timeOfDay);
    j.field("weather", r.weather);
    j.field("opponents", r.opponents);
    j.field("cops", r.cops);
    j.field("ambient", double(r.ambient));
    j.field("peds", double(r.peds));
    j.field("laps", r.laps);
    j.field("timeLimit", double(r.timeLimit));
    j.field("difficulty", double(r.difficulty));
    j.endObject();
}

std::string withState(bool ok, const std::string& why) {
    JsonWriter j;
    j.beginObject();
    j.field("ok", ok);
    j.field("why", why);
    j.endObject();
    std::string r = j.str();
    r.pop_back();
    return r + ",\"state\":" + stateJson() + "}";
}

}  // namespace

void setEnv(const Env& e) { st().env = e; }
const Env& env() { return st().env; }

// ---- strings

std::string loadMmlang(const uint8_t* data, size_t size) {
    std::string why;
    State& u = st();
    const bool ok = u.strings.load(data, size, why);
    JsonWriter j;
    j.beginObject();
    j.field("ok", ok);
    if (!ok) {
        j.field("why", why);
        logWarn("mmlang.dll: %s; the engine's own English labels are used", why.c_str());
    } else {
        logInfo("mmlang.dll: %d strings", int(u.strings.all().size()));
        j.key("strings").beginObject();
        for (const auto& kv : u.strings.all()) j.field(std::to_string(kv.first), kv.second);
        j.endObject();
    }
    j.endObject();
    return j.str();
}

const StringTable& strings() { return st().strings; }
std::string text(int id) { return st().strings.get(id); }

// ---- the players folder

void playersClear() { st().store.clearQuiet(); }
void setControllerButtons(int n) { st().store.setControllerButtons(n); }
bool playersPut(const std::string& path, const std::string& b64) {
    Bytes b;
    if (!base64Decode(b64, b)) return false;
    st().store.putFileQuiet(path, b);
    return true;
}
void setFileSink(std::function<void(const std::string&, const Bytes*)> f) { st().sink = std::move(f); }
bool started() { return st().started; }

std::string start() {
    State& u = st();
    setupEnv();
    u.store.setClock(float(u.env.clock ? u.env.clock() : 0.0));
    std::vector<CityRaces> cities = cityList();
    const std::string first = cities.empty() ? std::string("sf") : cities[0].raceDir;
    u.store.programStart(u.store.save().city.empty() ? first : u.store.save().city);
    u.started = true;
    return stateJson();
}

std::string state() { return stateJson(); }

std::string selectDriver(int i) {
    std::string why;
    const bool ok = st().store.selectDriver(i, why);
    return withState(ok, why);
}

std::string newDriver(const std::string& name, int skill) {
    std::string why;
    st().store.setClock(float(st().env.clock ? st().env.clock() : 0.0));
    const bool ok = st().store.newDriver(name, skill, why);
    return withState(ok, why);
}

std::string deleteDriver(int i) {
    std::string why;
    const bool ok = st().store.deleteDriver(i, why);
    return withState(ok, why);
}

void importBegin() { st().import.clear(); }
void importFile(const std::string& path, const uint8_t* data, size_t size) { st().import[path] = Bytes(data, data + size); }
std::string importFinish() {
    State& u = st();
    if (!u.started) setupEnv();
    const ProfileStore::ImportReport rep = u.store.importFolder(u.import);
    u.import.clear();
    u.started = true;
    JsonWriter j;
    j.beginObject();
    j.field("ok", rep.ok);
    j.field("drivers", rep.drivers);
    j.key("notes").beginArray();
    for (const std::string& n : rep.notes) j.value(cp1252ToUtf8(n));
    j.endArray();
    j.endObject();
    std::string r = j.str();
    r.pop_back();
    return r + ",\"state\":" + stateJson() + "}";
}

// ---- options

void setOption(const std::string& n, double v) {
    ConfigFile& c = st().store.options();
    const float f = float(v);
    const bool b = v != 0.0;
    if (n == "sfxVolume") c.sfxVolume = f;
    else if (n == "musicVolume") c.musicVolume = f;
    else if (n == "balance") c.balance = f;
    else if (n == "audioBits") c.audioBits = uint32_t(v);
    else if (n == "channels") c.channels = uint32_t(v);
    else if (n == "textureQuality") c.setTextureQuality(int(v));
    else if (n == "objectDetail") c.setObjectDetail(int(v));
    else if (n == "reflections") c.setReflections(b);
    else if (n == "cloudShadows") c.setCloudShadows(int(v));
    else if (n == "texturedSky") c.setTexturedSky(b);
    else if (n == "viewDistance") c.setViewDistance(f);
    else if (n == "lighting") c.setLighting(std::round(f));
    else if (n == "pedestriansOff") c.setPedestriansOff(b);
    else if (n == "controlDevice") c.setControlDevice(int(v));
    else if (n == "automatic") c.setAutomatic(b);
    else if (n == "autoReverse") c.setAutoReverse(b);
    else if (n == "hatSwitch") c.setHatSwitch(b);
    else if (n == "forceFeedback") c.setForceFeedback(b);
    else if (n == "ffCollision") c.setFfCollision(f);
    else if (n == "ffRoad") c.setFfRoad(f);
    else if (n == "deadZone") c.setDeadZone(f);
    else if (n == "steeringSensitivity") c.setSteeringFactor(f > 0.0f ? 1.0f / f : 1.0f);
    else if (n.compare(0, 4, "view") == 0) {
        const int k = std::atoi(n.c_str() + 4);
        if (k >= 0 && k < 12) c.view[k] = uint8_t(v);
    }
}

bool setBinding(int device, int action, int kind, int inputDevice, int component) {
    if (device < 0 || device > 4 || action < 0 || action >= 34 || kind < 0 || kind > 3 || component < 0) return false;
    if (kind != 0 && (inputDevice < 2 || inputDevice > 4)) return false;
    Binding& b = st().store.options().bindings[device * 34 + action];
    b = kind == 0 ? Binding() : Binding{uint32_t(kind), uint32_t(inputDevice), uint32_t(component)};
    return true;
}

std::string optionsDone() {
    st().store.writeOptions();
    return stateJson();
}

std::string optionsCancel() {
    ProfileStore& s = st().store;
    if (s.current() >= 0) {
        const Bytes* b = s.file(s.drivers().drivers[size_t(s.current())].file + ".cfg");
        if (b) readConfig(*b, s.options());
    }
    return stateJson();
}

std::string optionsDefaults(const std::string& screen) {
    ConfigFile& c = st().store.options();
    const ConfigFile d;
    if (screen == "audio") {
        c.sfxVolume = d.sfxVolume;
        c.musicVolume = d.musicVolume;
        c.balance = d.balance;
        c.audioBits = d.audioBits;
        c.channels = d.channels;
    } else if (screen == "graphics") {
        for (int k : {0, 1, 3, 4, 5, 6, 7, 8, 9}) c.graphics[k] = d.graphics[k];
    } else if (screen == "controls") {
        for (int k : {0, 1, 3, 4, 5, 6, 7, 9, 11, 12}) c.controls[k] = d.controls[k];
        defaultBindings(c.bindings, st().store.controllerButtons());
    } else if (screen == "keys") {
        // The customise-controls screen's Defaults: the binding table only (controls.md).
        defaultBindings(c.bindings, st().store.controllerButtons());
    }
    return stateJson();
}

// ---- menus

std::string cityRaces(int cityIndex) {
    std::vector<CityRaces> keep;
    const CityRaces* c = cityAt(cityIndex, keep);
    JsonWriter j;
    j.beginObject();
    if (!c) {
        j.field("ok", false);
        j.endObject();
        return j.str();
    }
    ProfileStore& s = st().store;
    const bool recs = keepsRecords(c->raceDir) && s.current() >= 0;
    const RecordFile* rec = recs ? s.record(c->raceDir) : nullptr;
    const int skill = s.current() >= 0 ? s.save().skill : 0;
    j.field("ok", true);
    j.field("name", c->localizedName);
    j.field("raceDir", c->raceDir);
    j.field("mapName", c->mapName);
    j.field("records", rec != nullptr);
    struct M {
        int mode, type;
    };
    const M modes[] = {{ModeCheckpoint, RecCheckpoint}, {ModeCircuit, RecCircuit}, {ModeBlitz, RecBlitz}};
    j.key("modes").beginObject();
    for (const M& m : modes) {
        const std::vector<RaceRow> rows = raceRows(c->raceDir, game::RaceMode(m.mode), skill);
        j.key(std::to_string(m.mode)).beginArray();
        const auto* names = c->names(m.type);
        for (int r = 0; r < c->count(m.type); ++r) {
            j.beginObject();
            j.field("name", names && size_t(r) < names->size() ? (*names)[size_t(r)] : strf("%d", r + 1));
            j.field("open", raceOpen(rec, m.type, r));
            j.field("locked", raceSettingsLocked(rec, m.type, r));
            const RaceRecord* e = rec ? rec->record(m.type, r) : nullptr;
            j.field("passed", e ? e->passed != 0 : false);
            j.field("best", e && e->time > 0.0f ? double(e->time) : 0.0);
            j.field("score", e ? e->score : 0);
            j.key("row");
            rowJson(j, size_t(r) < rows.size() ? rows[size_t(r)] : RaceRow());
            j.endObject();
        }
        j.endArray();
    }
    j.endObject();
    const std::vector<RaceRow> crash = raceRows(c->raceDir, game::RaceMode::CrashCourse, skill);
    j.key("crash").beginArray();
    for (int r = 0; r < kCrashCourseCount; ++r) {
        j.beginObject();
        j.field("open", raceOpen(rec, RecCrash, r));
        j.field("state", crashCourseState(rec, r));
        j.key("row");
        rowJson(j, size_t(r) < crash.size() ? crash[size_t(r)] : RaceRow());
        j.endObject();
    }
    j.endArray();
    j.endObject();
    return j.str();
}

std::string cars() {
    State& u = st();
    if (u.carInfo.empty() && u.env.vfs) u.carInfo = buildCarList(*u.env.vfs);
    const Locks locks = u.store.current() >= 0 ? u.store.locks() : Locks();
    std::vector<CityRaces> cities = cityList();
    // Why a car or paint job is locked: the rewards line not yet met (progression.md).
    auto reason = [&](JsonWriter& j, const LockReason& r) {
        j.beginObject();
        const CityRaces* c = nullptr;
        for (const CityRaces& x : cities)
            if (x.raceDir == r.city && !x.testLayout) c = &x;
        j.field("raceDir", r.city);
        j.field("city", c ? c->localizedName : r.city);
        j.field("mode", r.line.mode);
        j.field("condition", r.line.condition);
        std::string raceName;
        const int t = recordTypeOfMode(r.line.mode);
        if (c && r.line.condition >= 0 && t >= 0 && t < 3) {
            const auto* names = c->names(t);
            if (names && size_t(r.line.condition) < names->size()) raceName = (*names)[size_t(r.line.condition)];
        }
        j.field("raceName", raceName);
        j.endObject();
    };
    const std::vector<ListCar> appCars = u.env.cars ? u.env.cars() : std::vector<ListCar>();
    JsonWriter j;
    j.beginArray();
    for (const CarInfo& c : u.carInfo) {
        int index = -1;
        for (size_t i = 0; i < appCars.size(); ++i)
            if (appCars[i].baseName == c.baseName && appCars[i].menu) index = int(i);
        j.beginObject();
        j.field("index", index);
        j.field("name", c.baseName);
        j.field("description", c.description);
        j.field("mod", c.fromMod);
        // A police car (.info Flags, as the network game reads it): the free-for-all team 0
        // (multiplayer.md 5.3), the lobby's default team.
        j.field("police", index >= 0 && (appCars[size_t(index)].infoFlags & 8) != 0);
        j.field("locked", locks.carLocked(c.baseName));
        {
            auto it = locks.carWhy.find(c.baseName);
            if (it != locks.carWhy.end()) {
                j.key("why");
                reason(j, it->second);
            }
        }
        j.key("paints").beginArray();
        for (size_t p = 0; p < c.colors.size(); ++p) {
            j.beginObject();
            j.field("name", cp1252ToUtf8(c.colors[p]));
            j.field("locked", locks.paintLocked(c.baseName, int(p)));
            auto it = locks.paintWhy.find({c.baseName, int(p)});
            if (it != locks.paintWhy.end()) {
                j.key("why");
                reason(j, it->second);
            }
            j.endObject();
        }
        j.endArray();
        j.field("horsepower", std::atof(c.info.get("Horsepower").c_str()));
        j.field("topSpeed", std::atof(c.info.get("Top Speed").c_str()));
        j.field("durability", std::atof(c.info.get("Durability").c_str()));
        j.field("mass", std::atof(c.info.get("Mass").c_str()));
        j.field("scoringBias", std::atof(c.info.get("ScoringBias").c_str()));
        j.field("rhd", (std::atoi(c.info.get("Flags").c_str()) & 64) != 0);
        j.endObject();
    }
    j.endArray();
    return j.str();
}

std::string driverRecord() {
    ProfileStore& s = st().store;
    s.driverRecordsShown();
    JsonWriter j;
    j.beginObject();
    const Totals t = s.totals();
    j.key("score").beginArray().value(t.score[0]).value(t.score[1]).value(t.score[2]).endArray();
    j.key("passed").beginArray().value(t.passed[0]).value(t.passed[1]).value(t.passed[2]).endArray();
    j.field("total", t.total);
    j.key("cities").beginArray();
    for (const char* cn : kRecordCities) {
        const CityRaces* c = s.city(cn);
        const RecordFile* r = c ? s.record(cn) : nullptr;
        if (!c) continue;
        j.beginObject();
        j.field("raceDir", c->raceDir);
        j.field("name", c->localizedName);
        j.field("valid", r != nullptr);
        if (r) {
            j.key("counts").beginArray();
            for (int k = 0; k < 4; ++k) j.value(int(r->counts[k]));
            j.endArray();
            j.key("passedCount").beginArray();
            for (int k = 0; k < 4; ++k) j.value(r->passedCount(k));
            j.endArray();
            j.key("types").beginArray();
            for (int ty = 0; ty < 3; ++ty) {
                j.beginObject();
                j.field("mode", modeOfRecordType(ty));
                j.key("races").beginArray();
                const auto* names = c->names(ty);
                for (int k = 0; k < r->counts[ty]; ++k) {
                    const RaceRecord* e = r->record(ty, k);
                    j.beginObject();
                    j.field("name", names && size_t(k) < names->size() ? (*names)[size_t(k)] : strf("%d", k + 1));
                    j.field("time", e ? double(e->time) : 0.0);
                    j.field("car", e ? e->car : std::string());
                    j.field("score", e ? e->score : 0);
                    j.field("passed", e ? e->passed != 0 : false);
                    j.endObject();
                }
                j.endArray();
                j.endObject();
            }
            j.endArray();
        }
        j.endObject();
    }
    j.endArray();
    j.endObject();
    return j.str();
}

std::string raceStats(int cityIndex, int skill) {
    std::vector<CityRaces> keep;
    const CityRaces* c = cityAt(cityIndex, keep);
    JsonWriter j;
    j.beginObject();
    const HallOfFame* h = c ? st().store.hallOfFame(c->raceDir, skill, true) : nullptr;
    j.field("ok", h != nullptr);
    if (h && c) {
        j.key("types").beginArray();
        for (int t = 0; t < 3; ++t) {
            j.beginObject();
            j.field("mode", modeOfRecordType(t));
            j.key("races").beginArray();
            const auto* names = c->names(t);
            for (int r = 0; r < h->counts[t]; ++r) {
                j.beginObject();
                j.field("name", names && size_t(r) < names->size() ? (*names)[size_t(r)] : strf("%d", r + 1));
                for (int tab = 0; tab < 2; ++tab) {
                    j.key(tab == 0 ? "times" : "scores").beginArray();
                    for (int k = 0; k < 5; ++k) {
                        const int i = h->entryIndex(t, r, 2 * k + tab);
                        const FameEntry* e = i >= 0 ? &h->entries[size_t(i)] : nullptr;
                        if (!e || !e->crcOk || e->value == 0.0f) {
                            j.null();
                            continue;
                        }
                        j.beginObject();
                        j.field("name", e->name);
                        j.field("car", e->car);
                        j.field("value", double(e->value));
                        j.field("passed", e->passed != 0);
                        j.endObject();
                    }
                    j.endArray();
                }
                j.endObject();
            }
            j.endArray();
            j.endObject();
        }
        j.endArray();
    }
    j.endObject();
    return j.str();
}

// ---- records

ProfileStore& store() { return st().store; }
Records& records() { return st().records; }
std::string& lastCity() { return st().lastCity; }

bool Records::hasDriver() const { return st().store.current() >= 0; }
bool Records::cheated() const { return st().store.cheated(); }
bool Records::passedBefore(game::RaceMode m, int race, bool) const { return st().store.passedBefore(int(m), race); }
bool Records::raceOpen(game::RaceMode m, int race, bool) const { return st().store.raceOpenNow(int(m), race); }
void Records::record(const game::RaceOutcome& r) {
    ProfileStore::FinalResult f;
    f.mode = int(r.mode);
    f.race = r.race;
    f.time = r.time;
    f.passed = r.passed;
    f.score = r.score;
    f.car = r.car;
    f.laps = r.lapTimes;
    f.ownSettings = ownSettings;
    last = st().store.recordFinal(f);
    haveLast = true;
    logInfo("result: %s (score %d%s%s)", last.recorded ? "recorded" : (last.why.empty() ? "kept in the hall of fame" : last.why.c_str()),
            last.score, last.fameChanged ? ", hall of fame" : "", last.reward ? ", reward" : "");
}

std::string lastOutcome() {
    const Records& r = st().records;
    JsonWriter j;
    j.beginObject();
    j.field("have", r.haveLast);
    if (r.haveLast) {
        j.field("recorded", r.last.recorded);
        j.field("fameChanged", r.last.fameChanged);
        j.field("score", r.last.score);
        j.field("passed", r.last.passed);
        j.field("why", r.last.why);
        j.field("reward", r.last.reward);
        if (r.last.reward) {
            j.field("rewardCar", r.last.rewardLine.car);
            j.field("rewardPaint", r.last.rewardLine.variant);
            j.field("rewardMessage", r.last.rewardLine.message);
        }
    }
    j.endObject();
    return j.str();
}

std::string recordResult(int mode, int race, int place, int finished, double time, const std::string& laps, int crashPassed,
                         int ownSettings, double difficulty) {
    State& u = st();
    RaceResult r;
    r.mode = mode;
    r.race = race;
    r.place = place;
    r.finished = finished != 0;
    r.time = float(time);
    for (const std::string& x : split(laps, ','))
        if (!trim(x).empty()) r.laps.push_back(float(std::atof(x.c_str())));
    r.crashPassed = crashPassed != 0;
    r.ownSettings = ownSettings != 0;
    r.difficulty = float(difficulty);
    r.car = u.store.save().car;
    if (const CarRef* c = u.store.car(r.car)) r.scoringBias = c->scoringBias;
    if (!u.store.game().active) u.store.gameStart(u.lastCity.empty() ? "sf" : u.lastCity, mode, race);
    const ProfileStore::Outcome o = u.store.recordResult(r);
    JsonWriter j;
    j.beginObject();
    j.field("recorded", o.recorded);
    j.field("fameChanged", o.fameChanged);
    j.field("score", o.score);
    j.field("passed", o.passed);
    j.field("why", o.why);
    j.field("reward", o.reward);
    if (o.reward) {
        j.field("rewardCar", o.rewardLine.car);
        j.field("rewardPaint", o.rewardLine.variant);
        j.field("rewardMessage", o.rewardLine.message);
    }
    j.endObject();
    return j.str();
}

}  // namespace menu
}  // namespace cc
