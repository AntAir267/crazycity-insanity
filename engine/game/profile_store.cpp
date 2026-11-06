#include "game/profile_store.h"

#include <algorithm>

namespace cc {
namespace game {

namespace {
std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
}  // namespace

std::string ProfileStore::key(const std::string& p) {
    std::string k = lower(p);
    for (char& c : k)
        if (c == '\\') c = '/';
    while (!k.empty() && k[0] == '/') k.erase(0, 1);
    return k;
}

void ProfileStore::setEnv(Env e) { env_ = std::move(e); }

const Bytes* ProfileStore::file(const std::string& path) const {
    auto it = files_.find(key(path));
    return it == files_.end() ? nullptr : &it->second;
}

void ProfileStore::write(const std::string& path, const Bytes& b) {
    const std::string k = key(path);
    files_[k] = b;
    recCache_.clear();
    if (onWrite) onWrite(k, &files_[k]);
}

void ProfileStore::remove(const std::string& path) {
    const std::string k = key(path);
    if (!files_.erase(k)) return;
    recCache_.clear();
    if (onWrite) onWrite(k, nullptr);
}

std::string ProfileStore::textOr(int id, const char* fallback) const {
    std::string t = env_.text ? env_.text(id) : std::string();
    return t.empty() ? std::string(fallback) : t;
}

const CityRaces* ProfileStore::city(const std::string& raceDir) const {
    for (const CityRaces& c : env_.cities)
        if (lower(c.raceDir) == lower(raceDir) && !c.testLayout) return &c;
    return nullptr;
}

const CarRef* ProfileStore::car(const std::string& baseName) const {
    for (const CarRef& c : env_.cars)
        if (c.baseName == baseName) return &c;
    return nullptr;
}

std::vector<RewardLine> ProfileStore::rewards(const std::string& c) const {
    const std::string text = env_.rewardsText ? env_.rewardsText(c) : std::string();
    if (text.empty()) return {};
    return parseRewards(text, [this](const std::string& b) { return car(b) != nullptr; });
}

void ProfileStore::writeDir() { write("players.dir", writePlayersDir(dir_)); }

void ProfileStore::writeSave() {
    if (current_ < 0) return;
    write(dir_.drivers[size_t(current_)].file + ".sav", game::writeSave(save_));
}

void ProfileStore::writeCfg() {
    if (current_ < 0) return;
    write(dir_.drivers[size_t(current_)].file + ".cfg", writeConfig(options_));
}

// ---------------------------------------------------------------------------------------------

bool ProfileStore::loadRecord(const std::string& raceDir, RecordFile& out) const {
    if (current_ < 0) return false;
    const Bytes* b = file(raceDir + "/" + dir_.drivers[size_t(current_)].file + ".rec");
    if (!b) return false;
    if (readRecord(*b, out) != RecRead::Ok) return false;
    return out.stamp == save_.stamp;
}

const RecordFile* ProfileStore::record(const std::string& raceDir) {
    RecordFile r;
    if (!loadRecord(raceDir, r)) return nullptr;
    // A record whose check value fails is replaced by an empty one and its passed bit cleared,
    // both written back at once.
    bool repaired = false;
    for (int t = 0; t < 4; ++t)
        for (int i = 0; i < r.counts[t]; ++i) {
            RaceRecord* e = r.record(t, i);
            if (e && !e->crcOk) {
                *e = RaceRecord();
                if (i < 32) r.passed[t] &= ~(1u << i);
                repaired = true;
            }
        }
    if (repaired) write(lower(raceDir) + "/" + dir_.drivers[size_t(current_)].file + ".rec", writeRecord(r));
    const std::string k = lower(raceDir);
    recCache_[k] = r;
    return &recCache_[k];
}

void ProfileStore::ensureRecords() {
    // "Driver selected": for sf and london, a fresh empty record file where the file is not valid.
    if (current_ < 0) return;
    for (const char* c : kRecordCities) {
        const CityRaces* cr = city(c);
        if (!cr) continue;
        RecordFile r;
        if (loadRecord(c, r)) continue;
        write(std::string(c) + "/" + dir_.drivers[size_t(current_)].file + ".rec",
              writeRecord(freshRecord(save_.stamp, cr->checkpoint, cr->circuit, cr->blitz)));
    }
}

void ProfileStore::ensureHallsOfFame() {
    // london then sf (if the city exists): created empty if missing or unreadable.
    for (const char* c : {"london", "sf"}) {
        const CityRaces* cr = city(c);
        if (!cr) continue;
        for (const char* f : {"amateur.dat", "pro.dat"}) {
            const std::string p = std::string(c) + "/" + f;
            HallOfFame h;
            const Bytes* b = file(p);
            if (b && readHallOfFame(*b, h) == FameRead::Ok) continue;
            write(p, writeHallOfFame(freshHallOfFame(cr->checkpoint, cr->circuit, cr->blitz)));
        }
    }
}

const HallOfFame* ProfileStore::hallOfFame(const std::string& raceDir, int skill, bool create) {
    static HallOfFame shown;
    const CityRaces* cr = city(raceDir);
    const std::string p = lower(raceDir) + "/" + (skill ? "pro.dat" : "amateur.dat");
    const Bytes* b = file(p);
    HallOfFame h;
    const bool ok = b && readHallOfFame(*b, h) == FameRead::Ok;
    if (!ok) {
        if (!create || !cr) return nullptr;
        h = freshHallOfFame(cr->checkpoint, cr->circuit, cr->blitz);
        write(p, writeHallOfFame(h));
    } else if (create && cr &&
               (h.counts[0] != cr->checkpoint || h.counts[1] != cr->circuit || h.counts[2] != cr->blitz)) {
        h = freshHallOfFame(cr->checkpoint, cr->circuit, cr->blitz);
        write(p, writeHallOfFame(h));
    }
    shown = h;
    return &shown;
}

void ProfileStore::driverRecordsShown() {
    if (current_ < 0) return;
    for (const CityRaces& c : env_.cities) {
        if (c.testLayout) continue;
        RecordFile r;
        if (!loadRecord(c.raceDir, r)) continue;
        if (r.counts[0] != c.checkpoint || r.counts[1] != c.circuit || r.counts[2] != c.blitz)
            write(lower(c.raceDir) + "/" + dir_.drivers[size_t(current_)].file + ".rec",
                  writeRecord(freshRecord(save_.stamp, c.checkpoint, c.circuit, c.blitz)));
    }
}

Locks ProfileStore::locks() {
    std::vector<std::vector<RewardLine>> lines(2);
    std::vector<RecordFile> recs(2);
    std::vector<CityRewards> in;
    for (int i = 0; i < 2; ++i) {
        const char* c = kRecordCities[i];
        if (!city(c)) continue;
        lines[size_t(i)] = rewards(c);
        const RecordFile* r = record(c);
        if (!r || lines[size_t(i)].empty()) continue;
        recs[size_t(i)] = *r;
        in.push_back({&lines[size_t(i)], &recs[size_t(i)], c});
    }
    return computeLocks(in);
}

Totals ProfileStore::totals() {
    Totals t;
    for (const char* c : kRecordCities)
        if (const RecordFile* r = record(c)) addTotals(*r, t);
    return t;
}

// ---------------------------------------------------------------------------------------------

void ProfileStore::createDefaultDriver(const std::string& currentCity) {
    dir_ = PlayersDir();
    DriverEntry d;
    d.name = textOr(65, "Driver");
    d.file = newDriverFileName(dir_);
    dir_.drivers.push_back(d);
    dir_.last = d.name;
    current_ = 0;
    save_ = SaveFile();
    save_.stamp = clock_;
    save_.skill = 0;
    save_.name = d.name;
    save_.netName = textOr(66, "Player");
    save_.fileName = d.file;
    save_.city = currentCity;
    uint8_t v[12];
    newDriverViewBytes(v, options_.view);
    std::copy(v, v + 12, options_.view);
    writeCfg();
    writeSave();
    writeDir();
    ensureRecords();
}

void ProfileStore::programStart(const std::string& currentCity) {
    current_ = -1;
    game_ = GameStart();
    // MM2 builds its default binding table when it starts, before any driver's file is read.
    defaultBindings(options_.bindings, controllerButtons_);
    const Bytes* b = file("players.dir");
    if (!b || !readPlayersDir(*b, dir_)) createDefaultDriver(currentCity);
    ensureHallsOfFame();
    int sel = -1;
    for (size_t i = 0; i < dir_.drivers.size(); ++i)
        if (dir_.drivers[i].name == dir_.last) sel = int(i);
    if (sel < 0) sel = int(dir_.drivers.size()) - 1;
    std::string why;
    if (sel >= 0) selectDriver(sel, why);
}

bool ProfileStore::selectDriver(int index, std::string& why) {
    if (index < 0 || index >= int(dir_.drivers.size())) {
        why = "no such driver";
        return false;
    }
    const DriverEntry d = dir_.drivers[size_t(index)];
    SaveFile s;
    const Bytes* sb = file(d.file + ".sav");
    const SaveRead sr = sb ? readSave(*sb, s) : SaveRead::Missing;
    if (sr != SaveRead::Ok) {
        current_ = -1;
        // The last driver of the list is never deleted: it stays with its files, and there is no
        // current driver (profiles.md).
        if (dir_.drivers.size() <= 1) {
            why = "the driver's .sav file cannot be read";
            return false;
        }
        // Otherwise MM2 throws the driver away: removed from players.dir, its .cfg and .rec files
        // deleted (the .sav survives, still held open).
        why = "the driver's .sav file cannot be read; the driver was removed";
        dir_.drivers.erase(dir_.drivers.begin() + index);
        remove(d.file + ".cfg");
        for (const CityRaces& c : env_.cities) remove(lower(c.raceDir) + "/" + d.file + ".rec");
        writeDir();
        // After a check-value failure (the file still carried the driver's name) MM2 loads the
        // first driver instead, current if its .sav reads; else no driver is current.
        if (sr == SaveRead::BadCrc) {
            SaveFile first;
            const Bytes* fb = file(dir_.drivers[0].file + ".sav");
            if (fb && readSave(*fb, first) == SaveRead::Ok) {
                std::string w2;
                selectDriver(0, w2);
            }
        }
        return false;
    }
    current_ = index;
    save_ = s;
    // An empty or unknown car is replaced by vpbug with paint 0 when the driver is applied.
    if (save_.car.empty() || (!env_.cars.empty() && !car(save_.car))) {
        save_.car = "vpbug";
        save_.paint = 0;
    }
    const Bytes* cb = file(d.file + ".cfg");
    ConfigFile c = options_;
    const CfgRead cr = cb ? readConfig(*cb, c) : CfgRead::Missing;
    if (cr == CfgRead::Ok) options_ = c;
    else if (cr == CfgRead::Missing || cr == CfgRead::Damaged) writeCfg();
    // (another version: the options in effect stay; the file is rewritten with them later)
    dir_.last = d.name;
    writeDir();
    ensureRecords();
    return true;
}

bool ProfileStore::newDriver(const std::string& nameIn, int skill, std::string& why) {
    std::string name = nameIn;
    // At most 18 characters (code points) in the new-driver box.
    {
        size_t cps = 0, i = 0;
        for (; i < name.size(); ++i)
            if ((static_cast<unsigned char>(name[i]) & 0xC0) != 0x80 && ++cps > 18) break;
        name.resize(i);
    }
    why.clear();
    if (name.empty()) return false;
    for (const DriverEntry& d : dir_.drivers)
        if (d.name == name) {
            why = "duplicate";
            return false;
        }
    if (dir_.drivers.size() >= size_t(kMaxDrivers)) {
        why = "full";
        return false;
    }
    DriverEntry d;
    d.name = name;
    d.file = newDriverFileName(dir_);
    const std::string city = save_.city;
    dir_.drivers.push_back(d);
    dir_.last = name;
    current_ = int(dir_.drivers.size()) - 1;
    writeDir();
    save_ = SaveFile();
    save_.stamp = clock_;
    save_.skill = skill ? 1 : 0;
    save_.name = name;
    save_.netName = textOr(77, "Player");
    save_.fileName = d.file;
    save_.city = city;
    writeSave();
    options_.setRealism(0.75f);
    uint8_t v[12];
    newDriverViewBytes(v, options_.view);
    std::copy(v, v + 12, options_.view);
    writeCfg();
    ensureRecords();
    save_.car = "vpbug";  // applied: an empty car drives vpbug with paint 0
    save_.paint = 0;
    return true;
}

void ProfileStore::deleteDriverFiles(const DriverEntry& d) {
    remove(d.file + ".sav");
    remove(d.file + ".cfg");
    for (const CityRaces& c : env_.cities) remove(lower(c.raceDir) + "/" + d.file + ".rec");
}

bool ProfileStore::deleteDriver(int index, std::string& why) {
    if (index < 0 || index >= int(dir_.drivers.size())) {
        why = "no such driver";
        return false;
    }
    if (dir_.drivers.size() <= 1) {
        why = "last";
        return false;
    }
    const DriverEntry d = dir_.drivers[size_t(index)];
    const bool wasCurrent = index == current_;
    dir_.drivers.erase(dir_.drivers.begin() + index);
    if (current_ > index) --current_;
    if (dir_.last == d.name && !wasCurrent && current_ >= 0) dir_.last = dir_.drivers[size_t(current_)].name;
    writeDir();
    deleteDriverFiles(d);
    if (wasCurrent) {
        current_ = -1;
        std::string w;
        selectDriver(0, w);
    }
    return true;
}

void ProfileStore::writeOptions() { writeCfg(); }

void ProfileStore::writeForPlay(const std::string& carName, int paint, const std::string& cityRaceDir, int mode,
                                int race) {
    if (current_ < 0) return;
    save_.car = carName;
    save_.paint = paint;
    save_.city = cityRaceDir;
    save_.mode = mode;
    save_.race = race;
    writeSave();
    writeCfg();
}

// ---------------------------------------------------------------------------------------------

void ProfileStore::gameStart(const std::string& raceDir, int mode, int race) {
    game_ = GameStart();
    game_.active = true;
    game_.raceDir = raceDir;
    const CityRaces* cr = city(raceDir);
    game_.mapName = cr ? cr->mapName : raceDir;
    game_.mode = mode;
    game_.race = race;
    game_.skill = save_.skill;
    cfgReadAtStart_ = false;
    {
        // The hall of fame of the skill level (this does not depend on the driver or the records).
        const Bytes* fb = file(lower(raceDir) + "/" + (save_.skill ? "pro.dat" : "amateur.dat"));
        HallOfFame h;
        game_.fameOn = fb && readHallOfFame(*fb, h) == FameRead::Ok;
    }
    if (current_ >= 0) {
        ConfigFile c = options_;
        const Bytes* cb = file(dir_.drivers[size_t(current_)].file + ".cfg");
        if (cb && readConfig(*cb, c) == CfgRead::Ok) {
            options_ = c;  // read and applied (its view bytes are the ones the last game wrote)
            cfgReadAtStart_ = true;
        }
        RecordFile r;
        game_.recordsOn = keepsRecords(raceDir) && loadRecord(raceDir, r);
        if (game_.recordsOn) {
            std::vector<RewardLine> lines = rewards(game_.mapName);
            game_.locks = computeLocks({{&lines, &r, raceDir}});
        }
    }
}

void ProfileStore::gameEnd(const uint8_t view[12]) {
    if (!game_.active) return;
    std::copy(view, view + 12, options_.view);
    if (cfgReadAtStart_) writeCfg();
    game_.active = false;
}

ProfileStore::Outcome ProfileStore::recordResult(const RaceResult& res) {
    Outcome o;
    if (cheated_) {
        o.why = "a cheat code was used in this session";
        return o;
    }
    // Checkpoint and Blitz (and the Crash Course) skip the recording step without a current
    // driver; Circuit runs it anyway (only its hall of fame can then take the result, with an
    // empty driver name).
    if (current_ < 0 && res.mode != ModeCircuit) {
        o.why = "no driver";
        return o;
    }
    const int t = recordTypeOfMode(res.mode);
    if (t < 0) {
        o.why = "this mode keeps no records";
        return o;
    }
    RecordFile rf;
    const bool recValid = current_ >= 0 && game_.recordsOn && loadRecord(game_.raceDir, rf);
    const bool alreadyPassed = recValid && res.race >= 0 && res.race < 32 && (rf.passed[t] & (1u << res.race));
    RaceRecord nw;
    if (!resultRecord(res, game_.skill, alreadyPassed, nw)) {
        o.why = res.ownSettings ? "this result is not recorded" : "the race was played with changed settings";
        return o;
    }
    std::vector<float> laps;
    if (t == RecCircuit) laps = res.laps;
    return applyRecord(res.mode, res.race, nw, laps);
}

ProfileStore::Outcome ProfileStore::recordFinal(const FinalResult& f) {
    Outcome o;
    if (cheated_) {
        o.why = "a cheat code was used in this session";
        return o;
    }
    if (!f.ownSettings) {
        o.why = "the race was played with changed settings";
        return o;
    }
    if (current_ < 0 && f.mode != ModeCircuit) {
        o.why = "no driver";
        return o;
    }
    if (recordTypeOfMode(f.mode) < 0) {
        o.why = "this mode keeps no records";
        return o;
    }
    RaceRecord nw;
    nw.time = f.time;
    nw.car = f.car;
    nw.passed = f.passed ? 1 : 0;
    nw.score = f.score;
    // Circuit: the stored laps (a slot never stored stays out of the hall of fame).
    std::vector<float> laps;
    for (float x : f.laps)
        if (x > 0.0f) laps.push_back(x);
    return applyRecord(f.mode, f.race, nw, laps);
}

ProfileStore::Outcome ProfileStore::applyRecord(int mode, int race, const RaceRecord& nw, const std::vector<float>& laps) {
    Outcome o;
    const int t = recordTypeOfMode(mode);
    const std::string recPath =
        current_ >= 0 ? lower(game_.raceDir) + "/" + dir_.drivers[size_t(current_)].file + ".rec" : std::string();
    const std::string driverName = current_ >= 0 ? save_.name : std::string();
    RecordFile rf;
    const bool recValid = current_ >= 0 && game_.recordsOn && loadRecord(game_.raceDir, rf);
    o.score = nw.score;
    o.passed = nw.passed != 0;
    if (recValid && rf.record(t, race)) {
        addToRecordFile(rf, t, race, nw);
        write(recPath, writeRecord(rf));
        o.recorded = true;
    }
    if (game_.fameOn && t != RecCrash) {
        const std::string fp = lower(game_.raceDir) + "/" + (game_.skill ? "pro.dat" : "amateur.dat");
        HallOfFame h;
        const Bytes* fb = file(fp);
        if (fb && readHallOfFame(*fb, h) == FameRead::Ok) {
            bool changed = false;
            if (t == RecCircuit && !laps.empty()) {
                // Every lap's time in lap order to the time table; the score once (first lap).
                for (size_t i = 0; i < laps.size(); ++i)
                    changed |= addToHallOfFame(h, t, race, driverName, nw.car, laps[i], i == 0 ? nw.score : 0,
                                               nw.passed != 0);
            } else {
                changed = addToHallOfFame(h, t, race, driverName, nw.car, nw.time, nw.score, nw.passed != 0);
            }
            if (changed) write(fp, writeHallOfFame(h));
            o.fameChanged = changed;
        }
    }
    // Rewards: after the record was written (checkpoint, circuit, blitz every time; a crash course
    // only when passed), from the city's rewards (by MapName) and the record file as it is now.
    if (o.recorded && (t != RecCrash || nw.passed)) {
        RecordFile now;
        if (loadRecord(game_.raceDir, now)) {
            const std::vector<RewardLine> lines = rewards(game_.mapName);
            if (const RewardLine* l = findReward(lines, mode, now, game_.locks)) {
                o.reward = true;
                o.rewardLine = *l;
            }
        }
    }
    if (current_ >= 0) writeSave();
    return o;
}

bool ProfileStore::passedBefore(int mode, int race) const {
    const int t = recordTypeOfMode(mode);
    RecordFile r;
    if (t < 0 || race < 0 || race >= 32 || !loadRecord(game_.raceDir, r)) return false;
    return (r.passed[t] >> race) & 1u;
}

bool ProfileStore::raceOpenNow(int mode, int race) const {
    const int t = recordTypeOfMode(mode);
    RecordFile r;
    const bool valid = current_ >= 0 && keepsRecords(game_.raceDir) && loadRecord(game_.raceDir, r);
    return t < 0 || raceOpen(valid ? &r : nullptr, t, race);
}

// ---------------------------------------------------------------------------------------------

ProfileStore::ImportReport ProfileStore::importFolder(const Files& srcIn) {
    ImportReport rep;
    Files src;
    for (const auto& kv : srcIn) {
        std::string k = key(kv.first);
        if (k.compare(0, 8, "players/") == 0) k = k.substr(8);
        src[k] = kv.second;
    }
    auto get = [&](const std::string& p) -> const Bytes* {
        auto it = src.find(key(p));
        return it == src.end() ? nullptr : &it->second;
    };
    PlayersDir in;
    const Bytes* db = get("players.dir");
    if (!db || !readPlayersDir(*db, in)) {
        rep.notes.push_back("players.dir is missing or cannot be read: nothing to import");
        return rep;
    }
    Files out;
    PlayersDir kept;
    std::vector<float> stamps;
    for (const DriverEntry& d : in.drivers) {
        const Bytes* sb = get(d.file + ".sav");
        SaveFile s;
        const SaveRead r = sb ? readSave(*sb, s) : SaveRead::Missing;
        if (r != SaveRead::Ok) {
            rep.notes.push_back("skipped driver " + d.name + ": its .sav file " +
                                (r == SaveRead::Missing ? "is missing" : r == SaveRead::BadCrc ? "fails its check value"
                                                                                                : "cannot be read"));
            continue;
        }
        kept.drivers.push_back(d);
        stamps.push_back(s.stamp);
        out[key(d.file + ".sav")] = *sb;
        if (!s.car.empty() && !env_.cars.empty() && !car(s.car))
            rep.notes.push_back(d.name + ": car " + s.car + " is not in the car list (vpbug is used)");
        ConfigFile c;
        const Bytes* cb = get(d.file + ".cfg");
        const CfgRead cr = cb ? readConfig(*cb, c) : CfgRead::Missing;
        if (cr == CfgRead::Ok) {
            out[key(d.file + ".cfg")] = *cb;
        } else {
            ConfigFile def;
            def.setRealism(0.75f);
            uint8_t v[12];
            newDriverViewBytes(v, def.view);
            std::copy(v, v + 12, def.view);
            out[key(d.file + ".cfg")] = writeConfig(def);
            rep.notes.push_back(d.name + ": " + (cr == CfgRead::Missing ? "no .cfg" : "an older or damaged .cfg") +
                                ", default options");
        }
        for (const CityRaces& city : env_.cities) {
            if (city.testLayout) continue;
            const std::string rp = lower(city.raceDir) + "/" + d.file + ".rec";
            const Bytes* rb = get(rp);
            if (!rb) continue;
            RecordFile rec;
            if (readRecord(*rb, rec) != RecRead::Ok || rec.stamp != s.stamp) {
                rep.notes.push_back(d.name + ": the " + city.raceDir + " records are not valid (a fresh file is made)");
                continue;
            }
            if (rec.counts[0] != city.checkpoint || rec.counts[1] != city.circuit || rec.counts[2] != city.blitz) {
                rep.notes.push_back(d.name + ": the " + city.raceDir + " race list changed; its records start empty");
                out[rp] = writeRecord(freshRecord(s.stamp, city.checkpoint, city.circuit, city.blitz));
                continue;
            }
            int bad = 0;
            for (int t = 0; t < 4; ++t)
                for (int i = 0; i < rec.counts[t]; ++i) {
                    RaceRecord* e = rec.record(t, i);
                    if (e && !e->crcOk) {
                        *e = RaceRecord();
                        if (i < 32) rec.passed[t] &= ~(1u << i);
                        ++bad;
                    }
                }
            if (bad) rep.notes.push_back(d.name + ": " + std::to_string(bad) + " damaged " + city.raceDir +
                                         " record(s) emptied");
            out[rp] = bad ? writeRecord(rec) : *rb;
        }
    }
    if (kept.drivers.empty()) {
        rep.notes.push_back("no driver could be imported");
        return rep;
    }
    kept.last = in.last;
    bool lastFound = false;
    for (const DriverEntry& d : kept.drivers) lastFound |= d.name == kept.last;
    if (!lastFound) kept.last = kept.drivers.back().name;
    out["players.dir"] = writePlayersDir(kept);
    for (const CityRaces& city : env_.cities) {
        if (city.testLayout) continue;
        for (const char* f : {"amateur.dat", "pro.dat"}) {
            const std::string p = lower(city.raceDir) + "/" + f;
            const Bytes* hb = get(p);
            if (!hb) continue;
            HallOfFame h;
            if (readHallOfFame(*hb, h) != FameRead::Ok) {
                rep.notes.push_back(p + " cannot be read (a fresh one is made)");
                continue;
            }
            if (h.counts[0] != city.checkpoint || h.counts[1] != city.circuit || h.counts[2] != city.blitz) {
                out[p] = writeHallOfFame(freshHallOfFame(city.checkpoint, city.circuit, city.blitz));
                rep.notes.push_back(p + ": the race list changed; rebuilt empty");
                continue;
            }
            int bad = 0;
            for (FameEntry& e : h.entries)
                if (!e.crcOk) {
                    e = FameEntry();
                    ++bad;
                }
            out[p] = bad ? writeHallOfFame(h) : *hb;
            if (bad) rep.notes.push_back(p + ": " + std::to_string(bad) + " damaged entries emptied");
        }
    }
    // Replace the folder.
    std::vector<std::string> gone;
    for (const auto& kv : files_)
        if (!out.count(kv.first)) gone.push_back(kv.first);
    for (const std::string& g : gone) remove(g);
    for (const auto& kv : out) write(kv.first, kv.second);
    rep.ok = true;
    rep.drivers = int(kept.drivers.size());
    programStart(save_.city);
    return rep;
}

}  // namespace game
}  // namespace cc
