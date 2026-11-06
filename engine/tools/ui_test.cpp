// cctool uitest <game folder> <archive.ar>...: native checks of the player files, progression,
// mmlang.dll string table, HUD formatting and HUD map rules against the player's own MM2 folder
// (players/*, mmlang.dll) and archives. Every value named in the specs' "Checked against"
// sections of formats/profiles.md, game/progression.md, formats/mmlang.md, game/hud.md and
// game/hudmap.md is verified.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/strutil.h"
#include "drive/camera_rig.h"
#include "formats/car_info.h"
#include "formats/cinfo.h"
#include "formats/mmlang.h"
#include "formats/pkg.h"
#include "game/hud.h"
#include "game/hud_map.h"
#include "game/player_files.h"
#include "game/profile_store.h"
#include "game/progression.h"
#include "vfs/vfs.h"

using namespace cc;
using namespace cc::game;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

namespace {

bool readWhole(const std::string& path, Bytes& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n < 0 ? 0 : n));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

std::string hex(uint32_t v) { return strf("0x%08X", v); }

// Our city list entries with their race names (the .cinfo names lines).
std::vector<CityRaces> cityRaces(const Vfs& vfs) {
    std::vector<CityRaces> out;
    for (const CityEntry& e : buildCityList(vfs)) {
        CityRaces c;
        c.localizedName = e.localizedName;
        c.mapName = e.mapName;
        c.raceDir = e.raceDir;
        c.checkpoint = e.checkpoint;
        c.circuit = e.circuit;
        c.blitz = e.blitz;
        c.checkpointNames = e.checkpointNames;
        c.circuitNames = e.circuitNames;
        c.blitzNames = e.blitzNames;
        c.testLayout = e.testLayout;
        out.push_back(c);
    }
    return out;
}

std::vector<CarRef> carRefs(const Vfs& vfs) {
    std::vector<CarRef> out;
    for (const CarInfo& c : buildCarList(vfs)) {
        CarRef r;
        r.baseName = c.baseName;
        r.colors = int(c.colors.size());
        r.scoringBias = float(std::atof(c.info.get("ScoringBias").c_str()));
        r.description = c.description;
        out.push_back(r);
    }
    return out;
}

}  // namespace

int cmdUiTest(const std::string& gameDir, const std::vector<std::string>& archives) {
    setLogEcho(false);
    int problems = 0;
    auto expect = [&](bool cond, const std::string& what) {
        std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
        if (!cond) ++problems;
    };
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) {
        std::printf("cannot mount the archives\n");
        return 1;
    }
    const std::string pl = gameDir + "/players/";

    std::printf("== Check value (profiles.md)\n");
    {
        const char* s = "123456789";
        expect(mm2Crc(reinterpret_cast<const uint8_t*>(s), 9) == 0xC57B26A2u, "\"123456789\" gives 0xC57B26A2");
        std::vector<uint8_t> z(128, 0);
        expect(mm2Crc(z.data(), 92) == 0x695A3C42u, "92 zero bytes give 0x695A3C42");
        expect(mm2Crc(z.data(), 128) == 0x3DE0A897u, "128 zero bytes give 0x3DE0A897");
    }

    std::printf("== players.dir\n");
    Bytes dirB;
    PlayersDir dir;
    {
        const bool have = readWhole(pl + "players.dir", dirB);
        expect(have && dirB.size() == 168, strf("168 bytes (%zu)", dirB.size()));
        expect(readPlayersDir(dirB, dir), "version 6 reads");
        expect(dir.drivers.size() == 1 && dir.drivers[0].name == "DriverX" && dir.drivers[0].file == "player0",
               "one driver DriverX / player0");
        expect(dir.last == "DriverX", "last driver DriverX");
        PlayersDir back;
        expect(readPlayersDir(writePlayersDir(dir), back) && back.drivers.size() == 1 &&
                   back.drivers[0].name == "DriverX" && back.last == "DriverX",
               "written back, it reads the same");
        PlayersDir t;
        t.drivers = {{"a", "player1"}, {"b", "player0"}};
        expect(newDriverFileName(t) == "player2", "player0 and player1 taken (either order): the new file is player2");
        t.drivers = {{"a", "player0"}, {"b", "player2"}};
        expect(newDriverFileName(t) == "player1", "player0 and player2 taken: player1");
    }

    std::printf("== player0.sav\n");
    SaveFile sav;
    {
        Bytes b;
        readWhole(pl + "player0.sav", b);
        const SaveRead r = readSave(b, sav);
        expect(r == SaveRead::Ok, "reads (version 3, check value valid)");
        expect(sav.version == 3 && sav.storedCrc == 0xEE044F88u && saveCrc(b) == 0xEE044F88u,
               "check value 0xEE044F88, computed equal (" + hex(saveCrc(b)) + ")");
        expect(sav.stamp == 0.0f && sav.skill == 0, "stamp 0.0, Amateur");
        expect(sav.car == "vppanoz" && sav.paint == 2, "car vppanoz, paint 2 (" + sav.car + ")");
        expect(sav.mode == 4 && sav.race == 3 && sav.city == "sf", "mode 4 (Blitz), race 3, city sf");
        expect(sav.name == "DriverX" && sav.fileName == "player0", "name DriverX, file player0");
        SaveFile back;
        const Bytes w = writeSave(sav);
        expect(w.size() == 345 && readSave(w, back) == SaveRead::Ok && back.car == sav.car && back.city == sav.city &&
                   back.paint == 2 && back.mode == 4 && back.race == 3,
               "written back (zero-filled), it reads the same with a valid check value");
        Bytes bad = b;
        bad[20] ^= 1;
        SaveFile x;
        expect(readSave(bad, x) == SaveRead::BadCrc, "a changed byte fails the check value");
    }

    std::printf("== player0.cfg\n");
    {
        Bytes b;
        readWhole(pl + "player0.cfg", b);
        ConfigFile c;
        expect(b.size() == 2229 && readConfig(b, c) == CfgRead::Ok, strf("2,229 bytes, version 31 (%zu)", b.size()));
        expect(c.device.size() == 37, strf("n = 37 (%zu)", c.device.size()));
        expect(c.sfxVolume == 1.0f && c.musicVolume == 1.0f && c.balance == 0.0f, "volumes 1.0 / 1.0, balance 0");
        expect(c.audioBits == 0xCF3 && c.channels == 32, "audio bits 0xCF3, 32 channels");
        expect(c.textureQuality() == 2 && c.objectDetail() == 0 && c.reflections() && c.cloudShadows() == 1 &&
                   c.texturedSky() && c.viewDistance() == 600.0f && c.lighting() == 2.0f &&
                   ConfigFile::asFloat(c.graphics[8]) == 0.25f && !c.pedestriansOff() && c.graphics[13] == 1,
               "graphics: textures 2, detail 0, reflections, clouds 1, sky, 600 m, lighting 2.0, 0.25, peds on, portals 1");
        expect(c.controlDevice() == 3 && c.automatic() && c.autoReverse() && c.deadZone() == 0.1f &&
                   c.keyboardTarget() == 1.0f && c.steeringFactor() == 1.0f && c.realism() == 0.75f,
               "controls: gamepad, automatic, auto reverse, dead zone 0.1, K 1, U 1, realism 0.75");
        const uint8_t want[12] = {0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 1, 0};
        expect(std::memcmp(c.view, want, 12) == 0, "view bytes 00 00 00 00 00 01 00 01 00 00 01 00");
        expect(writeConfig(c) == b, "written back, byte-identical");
        // MM2's default binding table (controls.md, "Default binding table") against the shipped
        // file: all 170 entries, the wheel in its keyboard-key variant (no controller, or 5 or
        // fewer buttons).
        ConfigFile def;
        static const char* devs[5] = {"mouse", "keyboard", "joystick", "gamepad", "wheel"};
        int allSame = 0;
        std::string diff;
        for (int d = 0; d < 5; ++d)
            for (int a = 0; a < 34; ++a) {
                const Binding& x = def.bindings[d * 34 + a];
                const Binding& y = c.bindings[d * 34 + a];
                if (x.kind == y.kind && x.device == y.device && x.component == y.component) ++allSame;
                else if (diff.size() < 200)
                    diff += strf(" %s a%d(%u,%u,%u)", devs[d], a, y.kind, y.device, y.component);
            }
        expect(allSame == 170, strf("the default binding table equals the shipped one: %d/170%s", allSame, diff.c_str()));
        Binding wheel[170];
        defaultBindings(wheel, 8);
        auto bt = [&](int a) { return wheel[4 * 34 + a]; };
        expect(bt(0).component == 25 && bt(0).kind == 3 && bt(4).component == 24 && bt(11).component == 23 &&
                   bt(30).component == 26 && bt(21).component == 27 && bt(22).component == 28 && bt(10).kind == 1 &&
                   bt(8).kind == 2 && bt(8).component == 19,
               "an 8-button wheel takes buttons 5, 4, 3, 6 for map, gauges, camera, mirror and 7, 8 for the shifts");
        defaultBindings(wheel, 6);
        expect(bt(0).component == 25 && bt(21).device == 3 && bt(22).device == 3,
               "a 6-button wheel keeps A and Z for the shifts");
    }

    std::vector<CityRaces> cities = cityRaces(vfs);
    std::vector<CarRef> cars = carRefs(vfs);
    expect(cities.size() >= 2 && cities[0].raceDir == "sf" && cities[1].raceDir == "london",
           "city list: San Francisco, London");
    for (const CityRaces& c : cities) {
        if (c.testLayout) continue;
        expect(int(c.checkpointNames.size()) == c.checkpoint && int(c.circuitNames.size()) == c.circuit &&
                   int(c.blitzNames.size()) == c.blitz,
               c.raceDir + ": race names " + strf("%d/%d/%d", c.checkpoint, c.circuit, c.blitz));
    }

    std::printf("== Record files\n");
    RecordFile recSf, recLondon;
    for (const char* city : {"sf", "london"}) {
        Bytes b;
        readWhole(pl + city + "/player0.rec", b);
        RecordFile& r = std::string(city) == "sf" ? recSf : recLondon;
        expect(b.size() == 4364 && readRecord(b, r) == RecRead::Ok, std::string(city) + ": 4,364 bytes, version 2.0, header check value valid");
        expect(r.counts[0] == 12 && r.counts[1] == 10 && r.counts[2] == 10 && r.counts[3] == 13, "counts 12/10/10/13");
        int okc = 0;
        bool passedAgree = true;
        for (int t = 0; t < 4; ++t)
            for (int i = 0; i < r.counts[t]; ++i) {
                const RaceRecord* e = r.record(t, i);
                if (e && e->crcOk) ++okc;
                if (e && (e->passed != 0) != ((r.passed[t] >> i) & 1u)) passedAgree = false;
            }
        expect(okc == 45, strf("all 45 record check values valid (%d)", okc));
        expect(passedAgree, "passed bits equal the records' passed fields");
        expect(r.stamp == sav.stamp, "stamp equals the .sav stamp");
        RecordFile back;
        const Bytes w = writeRecord(r);
        bool same = readRecord(w, back) == RecRead::Ok;
        for (size_t i = 0; same && i < r.records.size(); ++i)
            same = back.records[i].crcOk && back.records[i].time == r.records[i].time &&
                   back.records[i].car == r.records[i].car && back.records[i].score == r.records[i].score;
        expect(same, "written back, it reads the same with valid check values");
    }
    expect(recSf.passed[RecBlitz] == 0x3 && recSf.passed[RecCheckpoint] == 0 && recSf.passed[RecCrash] == 0,
           "SF: blitz bits 0x3");
    {
        const RaceRecord* b0 = recSf.record(RecBlitz, 0);
        const RaceRecord* b1 = recSf.record(RecBlitz, 1);
        expect(b0 && b1 && b0->car == "vpford" && b1->car == "vpford" && b0->score == 750 && b1->score == 750 &&
                   std::fabs(b0->time - 26.145267f) < 1e-4f && std::fabs(b1->time - 49.547646f) < 1e-4f,
               "SF blitz 0 and 1: 26.15 s and 49.55 s in vpford, score 750 each");
        const RaceRecord* c0 = recLondon.record(RecCrash, 0);
        const RaceRecord* c1 = recLondon.record(RecCrash, 1);
        expect(recLondon.passed[RecCrash] == 0x1 && c0 && c1 && c0->passed == 1 && c1->passed == 0 &&
                   c0->time == 1.0f && c1->time == 1.0f && c0->score == 1 && c1->score == 1 && c0->car == "vpcab",
               "London: crash course 0 passed, 1 tried (vpcab), time 1.0 and score 1");
        expect(crashCourseState(&recLondon, 0) == 1 && crashCourseState(&recLondon, 1) == 2 &&
                   crashCourseState(&recLondon, 2) == 0,
               "crash-course states: passed, tried, not tried");
    }

    std::printf("== Halls of fame\n");
    {
        int okEntries = 0, files = 0;
        for (const char* city : {"sf", "london"})
            for (const char* f : {"amateur.dat", "pro.dat"}) {
                Bytes b;
                readWhole(pl + city + "/" + f, b);
                HallOfFame h;
                const bool ok = b.size() == 42256 && readHallOfFame(b, h) == FameRead::Ok && h.counts[0] == 12 &&
                                h.counts[1] == 10 && h.counts[2] == 10;
                expect(ok, std::string(city) + "/" + f + ": 42,256 bytes, counts 12/10/10");
                if (!ok) continue;
                ++files;
                int filled = 0;
                for (const FameEntry& e : h.entries) {
                    if (e.crcOk) ++okEntries;
                    if (e.value != 0.0f) ++filled;
                }
                const bool sfAm = std::string(city) == "sf" && std::string(f) == "amateur.dat";
                if (!sfAm) {
                    expect(filled == 0, "no entries");
                } else {
                    const int i0 = h.entryIndex(RecBlitz, 0, 0), i1 = h.entryIndex(RecBlitz, 1, 0);
                    const FameEntry& t0 = h.entries[size_t(i0)];
                    const FameEntry& s0 = h.entries[size_t(i0 + 1)];
                    const FameEntry& t1 = h.entries[size_t(i1)];
                    const FameEntry& s1 = h.entries[size_t(i1 + 1)];
                    expect(filled == 4 && t0.name == "DriverX" && t0.car == "vpford" &&
                               std::fabs(t0.value - 26.145f) < 1e-3f && s0.value == 750.0f &&
                               std::fabs(t1.value - 49.548f) < 1e-3f && s1.value == 750.0f && t0.passed == 1,
                           "blitz 0 and 1 rank 0 time and score: DriverX in vpford, 26.145 s / 49.548 s, 750");
                }
                expect(writeHallOfFame(h) == b, "written back, byte-identical");
            }
        expect(files == 4 && okEntries == 1280, strf("all 1,280 entry check values valid (%d)", okEntries));
    }

    std::printf("== Rewards\n");
    std::vector<RewardLine> rewSf, rewLondon;
    for (const char* city : {"sf", "london"}) {
        BytesPtr b = vfs.open(std::string("race/") + city + "/" + city + "_rewards.csv");
        expect(b != nullptr, std::string("race/") + city + "/" + city + "_rewards.csv exists");
        if (!b) continue;
        const std::string text(b->begin(), b->end());
        std::vector<RewardLine> all = parseRewardsAll(text);
        bool known = true, paintsOk = true;
        for (const RewardLine& l : all) {
            const CarRef* c = nullptr;
            for (const CarRef& x : cars)
                if (x.baseName == l.car) c = &x;
            if (!c) known = false;
            else if (l.variant >= c->colors) paintsOk = false;
        }
        expect(all.size() == 10, strf("10 rows (%zu)", all.size()));
        expect(known, "every car known");
        expect(paintsOk, "every paint number below the car's number of Colors");
        (std::string(city) == "sf" ? rewSf : rewLondon) = all;
    }
    {
        std::string many = "header\n# comment\n";
        for (int k = 0; k < 40; ++k) many += strf("blitz,%d,vpbug,%d,msg\n", k, k % 4);
        const auto lim = parseRewards(many, [](const std::string&) { return true; });
        expect(lim.size() == 32 && lim.back().condition == 31, "at most 32 accepted rows are taken");
    }

    std::printf("== Progression of the shipped DriverX\n");
    {
        expect(openCheckpointMask(&recSf) == 0x007 && openCheckpointMask(&recLondon) == 0x007,
               "checkpoint races open 0x007 (SF and London)");
        expect(openCrashMask(&recSf) == 0x777 && openCrashMask(&recLondon) == 0x777, "crash courses open 0x777");
        expect(openCheckpointMask(nullptr) == ~0u, "without records everything is open");
        Locks k = computeLocks({{&rewSf, &recSf, "sf"}, {&rewLondon, &recLondon, "london"}});
        const std::set<std::string> wantCars = {"vp4x4", "vpauditt", "vpcoop2k", "vpdb7",
                                                "vpdune", "vppanozgt", "vpsemi", "vpvwcup"};
        const std::set<std::pair<std::string, int>> wantPaints = {
            {"vpauditt", 4}, {"vpbullet", 4}, {"vpcab", 1},  {"vpcoop", 4},  {"vpcoop2k", 4}, {"vpddbus", 3},
            {"vpdune", 4},   {"vppanozgt", 4}, {"vpsemi", 2}, {"vpvwcup", 4}, {"vpvwcup", 5},  {"vpvwcup", 6}};
        std::string got;
        for (const auto& c : k.cars) got += " " + c;
        expect(k.cars == wantCars, "locked cars vp4x4 vpauditt vpcoop2k vpdb7 vpdune vppanozgt vpsemi vpvwcup (" + got + ")");
        got.clear();
        for (const auto& p : k.paints) got += strf(" %s %d", p.first.c_str(), p.second);
        expect(k.paints == wantPaints, "locked paint jobs as listed (" + got + ")");
        RecordFile freshSf = freshRecord(0.0f, 12, 10, 10), freshLo = freshRecord(0.0f, 12, 10, 10);
        Locks kn = computeLocks({{&rewSf, &freshSf, "sf"}, {&rewLondon, &freshLo, "london"}});
        expect(kn.cars == wantCars && kn.paints == wantPaints, "a new driver has the same locks");
        Totals t;
        addTotals(recSf, t);
        addTotals(recLondon, t);
        expect(t.total == 1500 && t.score[RecBlitz] == 1500 && t.passed[RecBlitz] == 2,
               strf("total score 1,500 (%d): two SF blitz records of 750", t.total));
    }

    std::printf("== Recording rules\n");
    {
        expect(placeScore(15.0f, 1, 1.0f, false) == 750 && placeScore(15.0f, 2, 1.0f, false) == 375 &&
                   placeScore(15.0f, 3, 1.0f, false) == 150 && placeScore(15.0f, 4, 1.0f, false) == 0 &&
                   placeScore(15.9f, 7, 1.9f, true) == 750,
               "score = trunc(bias) × 50/25/10/0 × trunc(difficulty); Blitz always 50");
        expect(placePasses(3, 0) && !placePasses(4, 0) && placePasses(1, 1) && !placePasses(2, 1),
               "passed: place 1–3 at Amateur, 1 at Professional");
        // The shipped records follow: SF blitz 0 in vpford at difficulty 1.
        RecordFile r = freshRecord(0.0f, 12, 10, 10);
        HallOfFame h = freshHallOfFame(12, 10, 10);
        RaceResult res;
        res.mode = ModeBlitz;
        res.race = 0;
        res.time = 26.145267f;
        res.car = "vpford";
        res.scoringBias = 15.0f;
        res.difficulty = 1.0f;
        RaceRecord nw;
        expect(resultRecord(res, 0, false, nw) && nw.score == 750 && nw.passed == 1 && nw.time == res.time,
               "blitz 0 in vpford: passed, score 15 × 50 × 1 = 750");
        addToRecordFile(r, RecBlitz, 0, nw);
        addToHallOfFame(h, RecBlitz, 0, "DriverX", "vpford", nw.time, nw.score, true);
        const Bytes rw = writeRecord(r);
        RecordFile rr;
        readRecord(rw, rr);
        const RaceRecord* sh = recSf.record(RecBlitz, 0);
        expect(rr.passed[RecBlitz] == 1 && rr.record(RecBlitz, 0)->crcOk &&
                   rr.record(RecBlitz, 0)->storedCrc == sh->storedCrc,
               "the record written is the shipped one (same check value " + hex(sh->storedCrc) + ")");
        res.race = 12;
        expect(!resultRecord(res, 0, false, nw), "blitz race 12 is not recorded");
        res.race = 0;
        res.ownSettings = false;
        expect(!resultRecord(res, 0, false, nw), "changed settings record nothing");
        // Slower, then faster results.
        res.ownSettings = true;
        res.time = 30.0f;
        res.car = "vpbug";
        resultRecord(res, 0, false, nw);
        addToRecordFile(r, RecBlitz, 0, nw);
        expect(r.record(RecBlitz, 0)->time == 26.145267f && r.record(RecBlitz, 0)->car == "vpford",
               "a slower time keeps the old time and car");
        res.time = 20.0f;
        resultRecord(res, 0, false, nw);
        addToRecordFile(r, RecBlitz, 0, nw);
        expect(r.record(RecBlitz, 0)->time == 20.0f && r.record(RecBlitz, 0)->car == "vpbug", "a faster time replaces both");
        // Hall of fame order: equal times go below, rank 4 falls off.
        HallOfFame f = freshHallOfFame(12, 10, 10);
        const float times[7] = {30, 20, 25, 20, 40, 10, 50};
        for (int i = 0; i < 7; ++i) addToHallOfFame(f, RecCheckpoint, 3, strf("d%d", i), "vpbug", times[i], 100 * i, true);
        const int base = f.entryIndex(RecCheckpoint, 3, 0);
        std::string order, scores;
        for (int k = 0; k < 5; ++k) {
            order += " " + f.entries[size_t(base + 2 * k)].name;
            scores += strf(" %d", int(f.entries[size_t(base + 2 * k + 1)].value));
        }
        expect(order == " d5 d1 d3 d2 d0", "time table: d5 d1 d3 d2 d0 (" + order + ")");
        expect(scores == " 600 500 400 300 200", "score table: 600 500 400 300 200 (" + scores + ")");
        f.entries[size_t(base + 4)].crcOk = false;  // a damaged entry: it and all after count as empty
        addToHallOfFame(f, RecCheckpoint, 3, "late", "vpbug", 99.0f, 1, true);
        expect(f.entries[size_t(base + 4)].name == "late" && f.entries[size_t(base + 6)].value == 0.0f,
               "from a damaged entry on, the race's entries count as empty");
        // Crash courses keep the car of the first attempt.
        RecordFile c = freshRecord(0.0f, 12, 10, 10);
        RaceResult cr;
        cr.mode = ModeCrash;
        cr.race = 1;
        cr.crashPassed = false;
        cr.car = "vpcab";
        resultRecord(cr, 0, false, nw);
        addToRecordFile(c, RecCrash, 1, nw);
        cr.crashPassed = true;
        cr.car = "vpbug";
        resultRecord(cr, 0, false, nw);
        addToRecordFile(c, RecCrash, 1, nw);
        expect(c.record(RecCrash, 1)->car == "vpcab" && c.record(RecCrash, 1)->passed == 1 && (c.passed[RecCrash] & 2),
               "a crash course keeps its first car; passing later sets the bit");
        // Open checkpoint races step by step.
        RecordFile o = freshRecord(0.0f, 12, 10, 10);
        o.passed[RecCheckpoint] = 0x007;
        bool steps = openCheckpointMask(&o) == 0x03F;
        o.passed[RecCheckpoint] = 0x03F;
        steps = steps && openCheckpointMask(&o) == 0x1FF;
        o.passed[RecCheckpoint] = 0x1C7;  // 0–2 and 6–8, not 3–5
        steps = steps && openCheckpointMask(&o) == 0xE3F;
        o.passed[RecCrash] = 0x777;
        steps = steps && openCrashMask(&o) == 0xFFF;
        o.passed[RecCrash] = 0xFFF;
        steps = steps && openCrashMask(&o) == ~0u;
        expect(steps, "open races: 0x03F after 0–2, 0x1C0 after 3–5, 0xE00 after 6–8; crash 3, 7, 11, then all");
    }

    std::printf("== Profile store (MM2's save moments)\n");
    {
        ProfileStore st;
        ProfileStore::Env env;
        env.cities = cities;
        env.cars = cars;
        env.rewardsText = [&vfs](const std::string& c) {
            BytesPtr b = vfs.open("race/" + c + "/" + c + "_rewards.csv");
            return b ? std::string(b->begin(), b->end()) : std::string();
        };
        st.setEnv(env);
        ProfileStore::Files src;
        for (const char* f : {"players.dir", "player0.sav", "player0.cfg", "sf/player0.rec", "london/player0.rec",
                              "sf/amateur.dat", "sf/pro.dat", "london/amateur.dat", "london/pro.dat"}) {
            Bytes b;
            if (readWhole(pl + f, b)) src[std::string("players/") + f] = b;
        }
        int writes = 0;
        st.onWrite = [&](const std::string&, const Bytes*) { ++writes; };
        ProfileStore::ImportReport rep = st.importFolder(src);
        expect(rep.ok && rep.drivers == 1 && st.current() == 0 && st.save().name == "DriverX",
               strf("import: 1 driver, DriverX current (%zu notes)", rep.notes.size()));
        for (const std::string& n : rep.notes) std::printf("         note: %s\n", n.c_str());
        PlayersDir again;
        expect(st.file("players.dir") && readPlayersDir(*st.file("players.dir"), again) &&
                   again.drivers.size() == 1 && again.last == "DriverX",
               "players.dir rewritten (zero-filled) on selecting the driver, as MM2 does");
        const RecordFile* rs = st.record("sf");
        expect(rs && rs->passed[RecBlitz] == 3, "SF records valid after import");
        expect(st.totals().total == 1500, "total score 1,500");
        std::string why;
        expect(!st.newDriver("DriverX", 0, why) && why == "duplicate", "a duplicate name is refused");
        expect(!st.newDriver("", 0, why) && why.empty(), "an empty name is ignored");
        expect(st.newDriver("Racer", 1, why) && st.current() == 1 && st.drivers().drivers[1].file == "player1" &&
                   st.save().skill == 1,
               "a new Professional driver gets player1 and becomes current");
        expect(st.file("player1.sav") && st.file("player1.cfg") && st.file("sf/player1.rec") && st.file("london/player1.rec"),
               "its .sav, .cfg and both record files are written");
        ConfigFile nc;
        readConfig(*st.file("player1.cfg"), nc);
        expect(nc.view[5] == 0 && nc.view[7] == 1 && nc.view[10] == 1 && nc.realism() == 0.75f,
               "its .cfg has the new-driver view bytes (gauges off, icons on, heading-up kept)");
        uint8_t cur[12] = {2, 1, 1, 1, 1, 1, 1, 0, 0, 1, 0, 1}, nv[12];
        newDriverViewBytes(nv, cur);
        expect(nv[0] == 0 && nv[1] == 0 && nv[5] == 0 && nv[6] == 1 && nv[7] == 1 && nv[8] == 0 && nv[9] == 1 &&
                   nv[10] == 0 && nv[11] == 1,
               "new-driver view bytes: 0 – 5 and 8 cleared, 7 on, 6, 9, 10, 11 kept");
        // Play a few SF blitz races with the new driver.
        st.writeForPlay("vpford", 0, "sf", ModeBlitz, 0);
        st.gameStart("sf", ModeBlitz, 0);
        expect(st.game().recordsOn && st.game().fameOn, "game start: records on, Professional hall of fame readable");
        RaceResult res;
        res.mode = ModeBlitz;
        res.car = "vpford";
        res.scoringBias = 15.0f;
        bool rewardSeen = false;
        std::string rewardWhat;
        for (int race = 0; race < 5; ++race) {
            res.race = race;
            res.time = 40.0f + float(race);
            ProfileStore::Outcome o = st.recordResult(res);
            if (o.reward) {
                rewardSeen = true;
                rewardWhat = strf("%s %d after %d passed", o.rewardLine.car.c_str(), o.rewardLine.variant, race + 1);
                expect(race == 4 && o.rewardLine.variant == 0, "the blitz 'half' reward comes with the 5th pass (" +
                                                                    rewardWhat + ")");
            }
        }
        expect(rewardSeen, "a reward was found");
        const HallOfFame* pro = st.hallOfFame("sf", 1, false);
        expect(pro && pro->entries[size_t(pro->entryIndex(RecBlitz, 0, 0))].name == "Racer",
               "the Professional hall of fame has the new driver at rank 0");
        Locks lk = st.locks();
        expect(lk.cars.size() == 7, strf("one car fewer is locked now (%zu)", lk.cars.size()));
        st.markCheat();
        res.race = 5;
        expect(!st.recordResult(res).recorded, "after a cheat code nothing is recorded");
        expect(!st.deleteDriver(5, why), "deleting a missing driver is refused");
        expect(st.deleteDriver(1, why) && st.current() == 0 && !st.file("player1.sav") && !st.file("sf/player1.rec"),
               "deleting the current driver removes its files; the first driver becomes current");
        expect(!st.deleteDriver(0, why) && why == "last", "the last driver cannot be deleted");
        // A damaged record is repaired when read.
        Bytes rec = *st.file("sf/player0.rec");
        rec[44 + 96 * (12 + 10) + 10] ^= 0xFF;  // inside blitz record 0
        st.putFileQuiet("sf/player0.rec", rec);
        const RecordFile* rp = st.record("sf");
        expect(rp && rp->record(RecBlitz, 0)->time == 0.0f && rp->passed[RecBlitz] == 2,
               "a record failing its check value is emptied and its passed bit cleared");
        // The last driver is never thrown away: a broken .sav leaves it listed, with no current
        // driver; another broken driver is removed, and after a check-value failure the first
        // driver is loaded instead (profiles.md).
        {
            ProfileStore b;
            b.setEnv(env);
            b.importFolder(src);
            std::string w;
            b.newDriver("Second", 0, w);
            Bytes broken = *b.file("player1.sav");
            broken[20] ^= 1;  // a check-value failure
            b.putFileQuiet("player1.sav", broken);
            const bool sel = b.selectDriver(1, w);
            expect(!sel && b.drivers().drivers.size() == 1 && b.current() == 0 && !b.file("player1.cfg") &&
                       b.file("player1.sav"),
                   "a broken driver is removed (its .sav kept) and the first driver is loaded instead");
            Bytes broken0 = *b.file("player0.sav");
            broken0[20] ^= 1;
            b.putFileQuiet("player0.sav", broken0);
            const bool sel0 = b.selectDriver(0, w);
            expect(!sel0 && b.drivers().drivers.size() == 1 && b.current() == -1 && b.file("player0.cfg"),
                   "the last driver stays listed with its files when its .sav breaks; no driver is current");
            ProfileStore::Files bad = src;
            bad["players/player0.sav"][20] ^= 1;
            ProfileStore c2;
            c2.setEnv(env);
            const ProfileStore::ImportReport r2 = c2.importFolder(bad);
            expect(!r2.ok && c2.files().empty(), "an import whose every driver fails imports nothing");
        }
        // A fresh browser: the default driver.
        ProfileStore fresh;
        ProfileStore::Env e2 = env;
        e2.text = [](int id) { return id == 65 ? std::string("TestDefault") : std::string(); };
        fresh.setEnv(e2);
        fresh.programStart("sf");
        expect(fresh.drivers().drivers.size() == 1 && fresh.save().name == "TestDefault" && fresh.file("sf/amateur.dat") &&
                   fresh.file("london/pro.dat") && fresh.file("sf/player0.rec"),
               "first start: the default driver (text 65), halls of fame and record files are created");
        (void)writes;
    }

    std::printf("== mmlang.dll\n");
    {
        Bytes b;
        StringTable st;
        std::string why;
        const bool have = readWhole(gameDir + "/mmlang.dll", b);
        expect(have && b.size() == 53248, strf("53,248 bytes (%zu)", b.size()));
        expect(st.load(b.data(), b.size(), why), "string table reads " + why);
        expect(st.all().size() == 667, strf("667 strings (%zu)", st.all().size()));
        bool all = true;
        for (int i = 1; i <= 668; ++i)
            if ((i == 447) == st.has(i)) all = false;
        expect(all && !st.has(0), "strings 1 – 668 present except 447");
        expect(st.blocks == 42 && st.languages == 1 && st.firstLanguage == 0x0409, "42 blocks, one language 0x0409");
        expect(st.longest == 358, strf("longest string 358 characters (%zu)", st.longest));
        expect(st.ascii, "all plain ASCII");
        expect(st.number(612) == 320 && st.number(613) == 200, "612 / 613 read 320 and 200");
        expect(st.number(664) == 15 && st.number(665) == 10 && st.number(666) == 10 && st.number(667) == 15 &&
                   st.number(668) == 15,
               "664 – 668 read 15, 10, 10, 15, 15");
        expect(st.number(275) == 0 && st.number(531) == 0 && st.number(605) == 0, "275, 531, 605 read 0");
        auto formats = [&](int id) {
            int n = 0;
            const std::string& s = st.get(id);
            for (size_t i = 0; i + 1 < s.size(); ++i)
                if (s[i] == '%') {
                    if (s[i + 1] == '%') ++i;
                    else ++n;
                }
            return n;
        };
        expect(formats(4) == 1 && formats(232) == 1, "strings 4 and 232 hold one format each");
        bool noBar = true;
        for (const auto& kv : st.all())
            if (kv.second.find('|') != std::string::npos) noBar = false;
        expect(noBar, "no string contains '|'");
        // The numbers our menus and HUD use.
        const int used[] = {3,   5,   6,   7,   12,  13,  20,  21,  28,  30,  62,  63,  65,  66,  77,  78,  79,
                            80,  81,  82,  276, 309, 428, 429, 430, 499, 532, 557, 574, 577, 580, 584, 606,
                            607, 633, 634, 635, 636, 637, 638, 640, 642, 643, 651, 660, 661};
        std::string missing;
        for (int id : used)
            if (!st.has(id)) missing += strf(" %d", id);
        expect(missing.empty(), "every text number the engine's UI uses exists" + missing);
    }

    std::printf("== HUD formatting (hud.md)\n");
    {
        expect(formatTimer(26.145267f) == "00:26:15" && formatTime(26.145267f) == "0:26:15",
               "26.145267 s: 00:26:15 (message 0:26:15) [" + formatTimer(26.145267f) + "]");
        expect(formatTimer(49.547646f) == "00:49:55", "49.547646 s: 00:49:55 [" + formatTimer(49.547646f) + "]");
        expect(formatTimer(59.996f) == "01:00:00", "59.996 s rounds up to 01:00:00 [" + formatTimer(59.996f) + "]");
        expect(formatTime(0.0f).empty() && formatTimer(-3.0f) == "00:00:00", "0 shows the placeholder in messages");
        expect(formatTime(125.5f) == "2:05:50", "125.5 s: 2:05:50 [" + formatTime(125.5f) + "]");
        Hud h;
        h.reset(ModeBlitz);
        h.countdown.reset(12.0f, false);
        h.countdown.running = true;
        h.update(2.5f);
        const int w1 = h.timeWarning();
        h.update(7.0f);
        const int w2 = h.timeWarning();
        h.update(5.0f);
        expect(w1 == 1 && w2 == 2 && h.countdown.value == 0.0f && !h.countdown.running && h.timeWarning() == 0,
               "countdown: warning below 10 s, continuous at 3 s, stops at 0");
        h.postPrimary(161, "", 5.0f, false);
        h.postSecondary(-1, "x");
        h.update(4.0f);
        const bool still = h.primary().textId == 161 && h.secondary().text == "x";
        h.update(1.5f);
        expect(still && h.primary().textId == -1 && h.secondary().text.empty(), "message lines clear together when the primary's time runs out");
    }

    std::printf("== HUD map (hudmap.md)\n");
    {
        for (const char* c : {"sf", "london"}) {
            BytesPtr b = vfs.open(std::string("tune/") + c + ".mmHudMap");
            HudMapTune t;
            expect(b && parseHudMapTune(std::string(b->begin(), b->end()), t), std::string("tune/") + c + ".mmHudMap reads");
            const bool london = std::string(c) == "london";
            expect(t.sizeX == 0.21f && t.sizeY == 0.25f && t.posX == 0.78f && t.posY == 0.75f && t.hasZoomIn &&
                       t.zoomIn == 0 && t.zoomInDist == 577.0f && t.zoomOutDist == 1195.0f,
                   "Size 0.21 0.25, Pos 0.78 0.75, ZoomIn 0, 577 / 1195");
            if (london)
                expect(t.iconScaleMin == 34.0f && t.iconScaleMax == 52.5f && t.zoomInDistFS == 786.0f &&
                           t.zoomOutDistFS == 1580.0f && t.iconScaleMinFS == 15.0f && t.iconScaleMaxFS == 18.0f,
                       "icon scales 34 / 52.5, full screen 786 / 1580, 15 / 18");
            else
                expect(t.iconScaleMin == 34.090019f && t.iconScaleMax == 52.399994f && t.zoomInDistFS == 786.0f &&
                           t.zoomOutDistFS == 1581.0f && t.iconScaleMinFS == 15.07f && t.iconScaleMaxFS == 18.0f,
                       "icon scales 34.090019 / 52.399994, full screen 786 / 1581, 15.07 / 18");
            expect(t.approachRate == 1.2f && !t.hasOcean, "the two-word keys are skipped: approach rate 1.2, no ocean colour");
            int rgb[3];
            mapBackground(london ? "London" : "sf", rgb);
            expect(london ? (rgb[0] == 234 && rgb[1] == 214 && rgb[2] == 198) : (rgb[0] == 21 && rgb[1] == 173 && rgb[2] == 234),
                   strf("background (%d, %d, %d)", rgb[0], rgb[1], rgb[2]));
            BytesPtr pk = vfs.open(std::string("geometry/hudmap_") + c + ".pkg");
            Pkg p;
            std::string err;
            bool flat = pk && parsePkg(pk->data(), pk->size(), p, err);
            float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
            for (const Mesh& m : p.meshes)
                for (const Batch& bt : m.batches)
                    for (const Vertex& v : bt.vertices) {
                        if (v.pos.y != 0.0f) flat = false;
                        minX = std::min(minX, v.pos.x);
                        maxX = std::max(maxX, v.pos.x);
                        minZ = std::min(minZ, v.pos.z);
                        maxZ = std::max(maxZ, v.pos.z);
                    }
            expect(flat, strf("geometry/hudmap_%s.pkg is flat: x %.1f … %.1f, z %.1f … %.1f", c, double(minX),
                              double(maxX), double(minZ), double(maxZ)));
        }
        HudMapTune d;
        MapRect r1 = mapRect(1, 640, 480, d, false), r2 = mapRect(2, 640, 480, d, false), v3 = viewRect(3, 640, 480, d);
        expect(r1.x == 499 && r1.y == 360 && r1.w == 124 && r1.h == 110, strf("640 × 480 small map (499, 360, 124, 110): (%d, %d, %d, %d)", r1.x, r1.y, r1.w, r1.h));
        expect(r2.x == 0 && r2.y == 240 && r2.w == 640 && r2.h == 240, "half map (0, 240, 640, 240)");
        expect(v3.x == 499 && v3.y == 360 && v3.w == 134 && v3.h == 120, "full-screen corner (499, 360, 134, 120)");
        MapRect rr = mapRect(1, 640, 480, d, true);
        expect(rr.x == 0, "right-hand drive in the dash view: the small map moves to the left edge");
        MapRect big = mapRect(1, 1024, 768, d, false);
        std::printf("  [info] 1024 × 768 small map (%d, %d, %d, %d)\n", big.x, big.y, big.w, big.h);
        BytesPtr sq = vfs.open("geometry/hudmap_square.pkg");
        Pkg p;
        std::string err;
        const bool sqOk = sq && parsePkg(sq->data(), sq->size(), p, err);
        float ext = 0.0f;
        for (const Mesh& me : p.meshes)
            for (const Batch& bt : me.batches)
                for (const Vertex& v : bt.vertices) ext = std::max(ext, std::max(std::fabs(v.pos.x), std::fabs(v.pos.z)));
        expect(sqOk && p.setCount == 9 && std::fabs(ext - 6.977f) < 0.01f,
               strf("hudmap_square: 9 material sets (%d), ±6.977 (%.3f)", p.setCount, double(ext)));
        HudMapState m;
        m.setTune(d);
        m.resetToSaved(0);
        bool lf = false, tg = false;
        m.tab(lf);
        const int a = m.mode();
        m.tab(lf);
        const int b2 = m.mode();
        m.fullScreen(tg);
        const int c3 = m.mode();
        m.tab(lf);
        const bool backTo2 = m.mode() == 2 && lf;
        m.tab(lf);
        expect(a == 1 && b2 == 2 && c3 == 3 && backTo2 && m.mode() == 0, "Tab 0 → 1 → 2, Q → 3, Tab back to 2, Tab → 0");
        m.setMode(1);
        m.zoom();
        float tt = 0.0f;
        while (m.height() != 577.0f && tt < 2.0f) {
            m.update(1.0f / 60.0f);
            tt += 1.0f / 60.0f;
        }
        expect(std::fabs(tt - 1.0f / 1.2f) < 0.02f, strf("a full zoom change takes 1/1.2 s (%.3f s)", double(tt)));
        expect(std::fabs(2.0 * 577.0 * std::tan(30.0 * 3.14159265358979 / 180.0) - 666.0) < 1.0,
               "zoomed in, the small map shows 666 m of ground");
    }

    std::printf("== Views (cameras.md, \"Switching views\")\n");
    {
        CameraRig rig;
        rig.setup(CameraFiles(), false);
        rig.toggleDash(false);
        rig.toggleOrbit();
        rig.toggleOrbit();
        expect(rig.active() == CameraRig::Dash, "dash, V, V: back to the dash view");
        rig.toggleOrbit();
        rig.noteViewKey();  // e.g. W in the orbit view
        rig.toggleOrbit();
        expect(rig.active() == CameraRig::Near, "dash, V, W, V: the external entry (W clears the mark)");
        rig.toggleDash(false);
        rig.toggleOrbit();
        rig.toggleDash(false);  // D is ignored in the orbit view and keeps the mark
        rig.toggleOrbit();
        expect(rig.active() == CameraRig::Dash, "dash, V, D, V: the dash view (D keeps the mark)");
    }

    std::printf("== Summary: %d check(s) failed\n", problems);
    return problems == 0 ? 0 : 1;
}
