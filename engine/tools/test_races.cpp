// Native checks of the race modes' data against the numbers in the "Checked against" sections of
// spec/game/races.md, waypoints.md, crash-course.md, results.md, spec/ai/opponents.md and
// spec/formats/opp.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "ai/road_network.h"
#include "city/city_data.h"
#include "core/strutil.h"
#include "ai/opponents.h"
#include "ai/police.h"
#include "formats/aimap.h"
#include "formats/car_info.h"
#include "formats/cinfo.h"
#include "formats/race_files.h"
#include "game/race.h"
#include "game/race_catalogue.h"
#include "game/waypoints.h"
#include "model/asset_cache.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

using namespace cc;
using game::RaceMode;

namespace {

using Expect = std::function<void(bool, const std::string&)>;

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

const RaceMode kRaceModes[3] = {RaceMode::Checkpoint, RaceMode::Circuit, RaceMode::Blitz};

// SF then London, as races.py walks them.
std::vector<CityEntry> shippedCities(const Vfs& vfs) {
    std::vector<CityEntry> out;
    for (const CityEntry& c : buildCityList(vfs))
        if (!c.testLayout && (iequals(c.raceDir, "sf") || iequals(c.raceDir, "london"))) out.push_back(c);
    return out;
}

void catalogueChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    std::string order;
    bool counts = true, files = true;
    for (const CityEntry& c : cities) {
        order += (order.empty() ? "" : ", ") + c.raceDir;
        counts = counts && c.checkpoint == 12 && c.circuit == 10 && c.blitz == 10 && c.checkpointNames.size() == 12 &&
                 c.circuitNames.size() == 10 && c.blitzNames.size() == 10;
        for (RaceMode m : kRaceModes)
            for (int n = 0; n < game::raceCount(c, m); ++n) {
                const std::string race = game::raceName(m, n);
                if (!vfs.exists(game::waypointPath(c.raceDir, m, n)) || !vfs.exists(game::raceAimapPath(c.raceDir, race, false)) ||
                    !vfs.exists(game::raceAimapPath(c.raceDir, race, true))) {
                    files = false;
                    std::printf("  missing a file of %s %s\n", c.raceDir.c_str(), race.c_str());
                }
            }
        for (int n = 0; n < 13; ++n)
            if (!vfs.exists(game::crashEventPath(c.raceDir, n, false)) || !vfs.exists(game::crashEventPath(c.raceDir, n, true)))
                files = false;
    }
    std::printf("  cities %s; SF race 0 '%s', London circuit 9 '%s'\n", order.c_str(),
                cities.empty() ? "" : game::raceDisplayName(cities[0], RaceMode::Checkpoint, 0).c_str(),
                cities.size() < 2 ? "" : game::raceDisplayName(cities[1], RaceMode::Circuit, 9).c_str());
    expect(cities.size() == 2 && iequals(cities[0].raceDir, "sf") && iequals(cities[1].raceDir, "london"),
           "race cities SF then London (races.md)");
    expect(counts, "each city 12 Checkpoint, 10 Circuit, 10 Blitz races with their names (races.md)");
    expect(files, "every race has a waypoint file, an .aimap and an .aimap_p; 13 crash courses with both event files");
}

void settingsChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    bool rows = true, fields = true, difficulty = true, opponents = true, laps = true, blitzTime = true;
    int maxCops = 0;
    std::string maxCopsAt;
    float tMin = 1e9f, tMax = -1e9f;
    std::set<int> unusedLaps;
    for (const CityEntry& c : cities) {
        for (RaceMode m : {RaceMode::Checkpoint, RaceMode::Circuit, RaceMode::Blitz, RaceMode::CrashCourse}) {
            const auto r = parseRaceData(textOf(vfs.open(game::settingsPath(c.raceDir, m))));
            const size_t want = m == RaceMode::Checkpoint ? 12 : m == RaceMode::CrashCourse ? 13 : 10;
            if (r.size() != want) rows = false;
            for (size_t i = 0; i < r.size(); ++i) {
                const RaceDataRow& row = r[i];
                if (row.fields != 21) fields = false;
                for (bool pro : {false, true}) {
                    const RaceSettings& s = row.of(pro);
                    if (s.difficulty != 1.0f || s.carType != 0) difficulty = false;
                    if (m == RaceMode::Blitz || m == RaceMode::CrashCourse) {
                        if (s.opponents != 0) opponents = false;
                    } else if (s.opponents < 4 || s.opponents > 7) {
                        opponents = false;
                    }
                    if (m == RaceMode::Circuit && (s.numLaps < 2 || s.numLaps > 4)) laps = false;
                    if (m == RaceMode::Checkpoint || m == RaceMode::Blitz) unusedLaps.insert(s.numLaps);
                    if (m == RaceMode::Blitz) {
                        tMin = std::min(tMin, s.timeLimit);
                        tMax = std::max(tMax, s.timeLimit);
                    }
                    if (s.cops > maxCops) {
                        maxCops = s.cops;
                        maxCopsAt = strf("%s %s%s", c.raceDir.c_str(), game::raceName(m, int(i)).c_str(), pro ? " pro" : "");
                    }
                }
            }
            if (m == RaceMode::CrashCourse && r.size() == 13 &&
                !(iequals(r[0].description, "lesson1") && iequals(r[12].description, "final13")))
                rows = false;
        }
    }
    if (!(tMin == 18.0f && tMax == 120.0f)) blitzTime = false;
    std::string ul;
    for (int v : unusedLaps) ul += strf(" %d", v);
    std::printf("  settings: cops up to %d (%s); blitz time limits %.0f – %.0f s; unused laps:%s\n", maxCops, maxCopsAt.c_str(),
                double(tMin), double(tMax), ul.c_str());
    expect(rows, "mmracedata 12 rows, mmcircuitdata and mmblitzdata 10, mmcrashdata 13 (lesson1 … final13) in both cities");
    expect(fields, "every settings row has the 21 fields");
    expect(difficulty, "Difficulty 1 and CarType 0 in every row");
    expect(opponents, "Opponents 0 in every Blitz and Crash Course row, 4 – 7 in Checkpoint and Circuit rows");
    expect(maxCops == 8 && maxCopsAt == "sf race8 pro", "Cops up to 8 (SF race8 professional)");
    expect(laps && unusedLaps == std::set<int>{3, 4}, "NumLaps 2 – 4 in Circuit rows, 3 / 4 in the unused columns");
    expect(blitzTime, "Blitz time limits 18 – 120 s");
}

// One position file, by the mode that reads it.
struct ListInfo {
    std::string city, name;
    bool event = false;
    RaceMode mode = RaceMode::Checkpoint;
    WaypointFile file;
};

std::vector<ListInfo> allLists(const Vfs& vfs, const std::vector<CityEntry>& cities) {
    std::vector<ListInfo> out;
    for (const CityEntry& c : cities) {
        for (RaceMode m : kRaceModes)
            for (int n = 0; n < game::raceCount(c, m); ++n) {
                ListInfo li;
                li.city = c.raceDir;
                li.name = game::raceName(m, n);
                li.mode = m;
                li.file = parseWaypoints(textOf(vfs.open(game::waypointPath(c.raceDir, m, n))), 50, m == RaceMode::Circuit);
                li.file.found = vfs.exists(game::waypointPath(c.raceDir, m, n));
                out.push_back(li);
            }
        // Every event of the 26 event files of the city (an event list named twice counts twice).
        for (int n = 0; n < 13; ++n)
            for (bool pro : {false, true})
                for (const CrashEvent& e : parseCrashEvents(textOf(vfs.open(game::crashEventPath(c.raceDir, n, pro))))) {
                    if (e.checkpoints == 0) continue;
                    ListInfo li;
                    li.city = c.raceDir;
                    li.name = e.filename;
                    li.event = true;
                    li.mode = RaceMode::CrashCourse;
                    const std::string path = "race/" + c.raceDir + "/" + e.filename + ".csv";
                    li.file = parseWaypoints(textOf(vfs.open(path)), 32, false);
                    li.file.found = vfs.exists(path);
                    out.push_back(li);
                }
    }
    return out;
}

void waypointChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    const std::vector<ListInfo> lists = allLists(vfs, cities);
    int points = 0, lines = 0, full = 0, msgs = 0, filled = 0, zero = 0, sphere = 0, missing = 0, appended = 0;
    int longestRace = 0, longestEvent = 0;
    float rMin = 1e9f, rMax = -1e9f;
    for (const ListInfo& l : lists) {
        if (!l.file.found) ++missing;
        points += int(l.file.points.size());
        lines += l.file.lines;
        full += l.file.fullLines;
        msgs += l.file.messages;
        filled += l.file.filled;
        (l.event ? longestEvent : longestRace) = std::max(l.event ? longestEvent : longestRace, int(l.file.points.size()));
        for (size_t wi = 0; wi < l.file.points.size(); ++wi) {
            const Waypoint& w = l.file.points[wi];
            if (w.appended) ++appended;
            if (w.heading == 0.0f) {
                ++zero;
                if (wi != 0 && wi + 1 != l.file.points.size())
                    std::printf("  zero heading inside a list: %s %s waypoint %zu\n", l.city.c_str(), l.name.c_str(), wi);
            }
            if (w.sphere) ++sphere;
            rMin = std::min(rMin, w.radius);
            rMax = std::max(rMax, w.radius);
        }
    }
    std::printf("  %zu lists, %d waypoints (%d lines, %d with eight fields, %d messages); longest race list %d, event list %d; "
                "radii %.0f … %.0f; %d filled headings, %d sphere flags\n",
                lists.size(), points, lines, full, msgs, longestRace, longestEvent, double(rMin), double(rMax), filled, sphere);
    expect(lists.size() == 122 && points == 1114, strf("122 lists, 1,114 waypoints (waypoints.md) (%zu, %d)", lists.size(), points));
    expect(missing == 0, "every waypoint file a menu can reach exists");
    expect(full == lines && lines == points && msgs == 0, "every line has the eight numeric fields and an empty message");
    expect(appended == 0 && longestRace == 23 && longestEvent == 24, "no list reaches its slot limit (longest 23 race, 24 event)");
    // A zero heading occurs only where the fill-in never looks (waypoint 0, the last waypoint of a
    // non-Circuit list): QUESTIONS.md.
    expect(zero == 10 && filled == 0 && sphere == 0 && rMin == 3.0f && rMax == 36.0f,
           "the fill-in never fires (10 zero headings, all at waypoint 0 or a list's last waypoint); no radius 0 or sphere flag; radii 3 … 36");

    // Gate direction (waypoints.md "Checked against"): in the ordered lists, the facing of a car
    // put down at a gate against the direction from the previous waypoint, and a vpbug-sized box
    // (2, 2, 3) driven straight from waypoint to waypoint.
    int gates = 0, within90 = 0, centre = 0, off6 = 0, off6turned = 0;
    std::vector<float> cosines;
    const vec3 box(2, 2, 3);
    auto drive = [&](const Waypoint& from, const Waypoint& to, float side, bool turned) {
        Waypoint g = to;
        if (turned) {
            g.heading = g.heading + 90.0f;
            computeGate(g);
        }
        vec3 d(to.pos.x - from.pos.x, 0, to.pos.z - from.pos.z);
        const float L = std::sqrt(d.x * d.x + d.z * d.z);
        if (!(L > 0)) return false;
        d = vec3(d.x / L, 0, d.z / L);
        game::GatePose p;
        p.Z = vec3(-d.x, 0, -d.z);
        p.Y = vec3(0, 1, 0);
        p.X = vec3(-d.z, 0, d.x);  // right of the travel direction
        // The straight path moved sideways by `side` × r.
        const vec3 off = p.X * (side * to.radius);
        for (float s = 0; s <= L + 10.0f; s += 0.25f) {
            p.P = from.pos + d * s + off;
            if (game::playerCrossesGate(p, box, g)) return true;
        }
        return false;
    };
    // The ordered lists: every list but Blitz; a Circuit adds the closing gate back to waypoint 0.
    for (const ListInfo& l : lists) {
        if (l.mode == RaceMode::Blitz) continue;
        const auto& P = l.file.points;
        std::vector<std::pair<size_t, size_t>> legs;
        for (size_t i = 1; i < P.size(); ++i) legs.push_back({i - 1, i});
        if (l.mode == RaceMode::Circuit && P.size() >= 2) legs.push_back({P.size() - 1, 0});
        for (const auto& leg : legs) {
            const Waypoint& a = P[leg.first];
            const Waypoint& b = P[leg.second];
            ++gates;
            const float th = b.heading * -0.017453292f;
            const vec3 face(-std::sin(th), 0, -std::cos(th));
            vec3 d(b.pos.x - a.pos.x, 0, b.pos.z - a.pos.z);
            const float L = std::sqrt(d.x * d.x + d.z * d.z);
            const float c = L > 0 ? (face.x * d.x + face.z * d.z) / L : 0;
            if (c > 0) ++within90;
            cosines.push_back(std::fabs(c));
            if (drive(a, b, 0.0f, false)) ++centre;
            if (drive(a, b, 0.6f, false)) ++off6;
            if (drive(a, b, 0.6f, true)) ++off6turned;
        }
    }
    std::sort(cosines.begin(), cosines.end());
    const float median = cosines.empty() ? 0 : cosines[cosines.size() / 2];
    std::printf("  ordered lists: %d gates, %d face within 90° (median |cos| %.3f); box through the centre %d, 0.6 r off %d, "
                "turned 90° %d\n",
                gates, within90, double(median), centre, off6, off6turned);
    expect(gates == 846 && within90 == 646 && std::fabs(median - 0.937f) < 0.0005f,
           "a car put down at a gate faces within 90° of the way from the previous waypoint at 646 of the 846 ordered gates (median |cos| 0.937)");
    expect(gates == 846 && centre == 846, strf("a vpbug box driven waypoint to waypoint crosses all 846 gates of the ordered lists (%d of %d)",
                                               centre, gates));
}

void crashChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    int files = 0;
    bool fields = true, field8 = true, filesExist = true;
    std::map<std::string, std::set<int>> kinds;
    std::map<std::string, std::string> twoEvent;
    std::vector<std::string> onlyTarget;
    std::map<std::string, std::set<float>> minSpeeds;
    float tMin = 1e9f, tMax = -1e9f, aMin = 1e9f, aMax = -1e9f;
    std::string tMinAt;
    int wMin = 99, wMax = 0;
    for (const CityEntry& c : cities)
        for (int n = 0; n < 13; ++n)
            for (bool pro : {false, true}) {
                const BytesPtr b = vfs.open(game::crashEventPath(c.raceDir, n, pro));
                if (!b) continue;
                ++files;
                const auto ev = parseCrashEvents(textOf(b));
                if (ev.size() == 2) {
                    twoEvent[strf("%s crash%d", c.raceDir.c_str(), n)] = strf("(%d, %d)", ev[0].kind, ev[1].kind);
                }
                for (const CrashEvent& e : ev) {
                    if (e.fields < 8) fields = false;
                    kinds[c.raceDir].insert(e.kind);
                    const bool target = e.kind == 2 || e.kind == 8;
                    if (e.opponents != (target ? 1 : 0)) field8 = false;
                    if (e.onlyTarget)
                        onlyTarget.push_back(strf("%s crash%d %s %d", c.raceDir.c_str(), n, e.filename.c_str(), e.kind));
                    if (e.kind == 4) minSpeeds[c.raceDir].insert(e.misc < 1.0f ? 50.0f : e.misc);
                    if (e.timeLimit < tMin) {
                        tMin = e.timeLimit;
                        tMinAt = strf("%s crash%d", c.raceDir.c_str(), n);
                    }
                    tMax = std::max(tMax, e.timeLimit);
                    aMin = std::min(aMin, e.ambDensity);
                    aMax = std::max(aMax, e.ambDensity);
                    if (e.checkpoints) {
                        const BytesPtr wb = vfs.open("race/" + c.raceDir + "/" + e.filename + ".csv");
                        if (!wb) filesExist = false;
                        const auto wf = parseWaypoints(textOf(wb), 32, false);
                        wMin = std::min(wMin, int(wf.points.size()));
                        wMax = std::max(wMax, int(wf.points.size()));
                    }
                }
            }
    auto setStr = [](const std::set<int>& s) {
        std::string o;
        for (int v : s) o += strf("%s%d", o.empty() ? "" : " ", v);
        return o;
    };
    std::string two;
    for (const auto& kv : twoEvent) two += strf(" %s %s", kv.first.c_str(), kv.second.c_str());
    std::string ot;
    for (const auto& s : onlyTarget) ot += " [" + s + "]";
    std::printf("  %d event files; kinds SF %s, London %s; two-event:%s; only-target:%s; time limits %.0f (%s) … %.0f; "
                "traffic %.1f … %.1f; event lists %d … %d waypoints\n",
                files, setStr(kinds["sf"]).c_str(), setStr(kinds["london"]).c_str(), two.c_str(), ot.c_str(), double(tMin),
                tMinAt.c_str(), double(tMax), double(aMin), double(aMax), wMin, wMax);
    expect(files == 52 && fields, "52 event files, every row with the eight fields (crash-course.md)");
    expect(setStr(kinds["sf"]) == "0 2 3 4 5 7 8" && setStr(kinds["london"]) == "0 2 3 4 5 7 9",
           "event kinds SF 0 2 3 4 5 7 8, London 0 2 3 4 5 7 9");
    expect(twoEvent.size() == 3 && twoEvent["london crash7"] == "(7, 2)" && twoEvent["london crash12"] == "(2, 7)" &&
               twoEvent["sf crash7"] == "(8, 3)",
           "two-event courses London crash7 (7, 2), crash12 (2, 7), SF crash7 (8, 3)");
    expect(field8, "field 8 is 1 for every follow and destroy event, 0 otherwise");
    bool otOk = !onlyTarget.empty();
    for (const auto& s : onlyTarget)
        otOk = otOk && (s == "sf crash4 oneeighty 7" || s == "london crash4 map 9");
    expect(otOk, "only-target only in SF crash4 (oneeighty, kind 7) and London crash4 (map, kind 9)");
    expect(minSpeeds["sf"] == std::set<float>{50.0f} && minSpeeds["london"] == std::set<float>{40.0f},
           "minimum speeds 50 mph (SF) and 40 mph (London)");
    expect(tMin == 0.0f && tMinAt == "sf crash2" && tMax == 200.0f && aMin == 0.0f && std::fabs(aMax - 0.7f) < 1e-6f,
           "time limits 0 (SF crash2) to 200 s, traffic densities 0 – 0.7");
    expect(filesExist && wMin == 2 && wMax == 24, "every named waypoint file exists, 2 – 24 waypoints each");
}

void opponentRowChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    int rows = 0, twelve = 0, countMismatch = 0, missingFiles = 0, thr1 = 0, pad50 = 0, corner1 = 0;
    float tMin = 9, tMax = -9, pMin = 1e9f, pMax = -1e9f, bMin = 9, bMax = -9, cMin = 9, cMax = -9;
    std::map<std::string, int> cars, patterns;
    for (const CityEntry& c : cities)
        for (const std::string& path : vfs.listUnique("race/" + c.raceDir + "/")) {
            if (!(iendsWith(path, ".aimap") || iendsWith(path, ".aimap_p"))) continue;
            const std::string b = toLower(baseName(path));
            if (!(startsWith(b, "race") || startsWith(b, "circuit") || startsWith(b, "blitz") || startsWith(b, "crash"))) continue;
            const RaceAimap a = parseRaceAimap(textOf(vfs.open(path)));
            if (a.opponentCount != int(a.opponents.size())) ++countMismatch;
            for (const OpponentRow& r : a.opponents) {
                ++rows;
                if (r.fields == 12) ++twelve;
                if (!vfs.exists("race/" + c.raceDir + "/" + r.file)) ++missingFiles;
                ++cars[r.car];
                tMin = std::min(tMin, r.throttle), tMax = std::max(tMax, r.throttle);
                pMin = std::min(pMin, r.pad), pMax = std::max(pMax, r.pad);
                bMin = std::min(bMin, r.brake), bMax = std::max(bMax, r.brake);
                cMin = std::min(cMin, r.corner), cMax = std::max(cMax, r.corner);
                if (r.throttle == 1.0f) ++thr1;
                if (r.pad == 50.0f) ++pad50;
                if (r.corner == 1.0f) ++corner1;
                std::string p;
                for (int k = 0; k < 6; ++k) p += strf("%s%d", k ? " " : "", int(r.sw[k]));
                ++patterns[p];
            }
        }
    std::string cs;
    for (const auto& kv : cars) cs += strf(" %s %d", kv.first.c_str(), kv.second);
    std::printf("  %d opponent rows (%d with twelve fields); cars:%s\n", rows, twelve, cs.c_str());
    std::printf("  throttle %.2f … %.2f (%d at 1.00), pad %.0f (%d) … %.0f, brake %.2f … %.2f, corner %.2f … %.2f (%d at 1.0)\n",
                double(tMin), double(tMax), thr1, double(pMin), pad50, double(pMax), double(bMin), double(bMax), double(cMin),
                double(cMax), corner1);
    expect(rows == 536 && twelve == 536 && countMismatch == 0 && missingFiles == 0,
           "536 opponent rows, all with twelve fields, count lines equal to their rows, every route file present");
    expect(cars["vppanoz"] == 95 && cars["vpbullet"] == 68 && cars["vpdb7"] == 57 && cars["vpauditt"] == 48 &&
               cars["vpbug"] == 40 && cars["vpvwcup"] == 38 && cars["vpcoop2k"] == 36 && cars["vpcab"] == 35 &&
               cars["vppanozgt"] == 35 && cars["vpcoop"] == 28 && cars["vpcaddie"] == 23 && cars["vpmustang99"] == 16 &&
               cars["vpford"] == 15 && cars["vp4x4"] == 2 && cars.size() == 14,
           "opponent cars as in opponents.md (vppanoz 95 … vp4x4 2)");
    expect(std::fabs(tMin - 0.57f) < 1e-6f && tMax == 1.0f && thr1 == 335 && pMin == 50.0f && pad50 == 407 && pMax == 150.0f &&
               std::fabs(bMin - 0.07f) < 1e-6f && bMax == 1.0f && std::fabs(cMin - 0.89f) < 1e-6f &&
               std::fabs(cMax - 2.29f) < 1e-6f && corner1 == 297,
           "throttle 0.57 … 1.00 (335 at 1.00), pad 50 (407) … 150, brake 0.07 … 1.0, corner 0.89 … 2.29 (297 at 1.0)");
    const std::map<std::string, int> want = {{"0 0 0 0 0 0", 226}, {"0 1 1 1 1 0", 128}, {"0 0 1 0 0 0", 52}, {"0 1 0 0 1 0", 45},
                                             {"0 1 0 0 0 0", 29},  {"0 1 1 0 1 0", 20},  {"0 0 0 0 1 0", 14}, {"0 0 1 0 1 0", 11},
                                             {"0 1 1 0 0 0", 6},   {"1 1 0 1 1 1", 3},   {"0 0 1 1 1 0", 1},  {"0 1 0 1 0 0", 1}};
    expect(patterns == want, "the twelve switch patterns of opponents.md");
}

void oppFileChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    std::map<std::string, int> files, points;
    int badHeader = 0, notNine = 0, blank = 0, longL = 0, fewTokens = 0;
    for (const CityEntry& c : cities)
        for (const std::string& path : vfs.listUnique("race/" + c.raceDir + "/")) {
            if (!iendsWith(path, ".opp")) continue;
            const OppFile f = parseOpp(textOf(vfs.open(path)));
            ++files[c.raceDir];
            points[c.raceDir] += int(f.points.size());
            if (f.header != "x,y,z,brake,forward offset,side offset,target speed,speed start,side start") ++badHeader;
            blank += f.blankLines;
            longL += f.longLines;
            for (const OppPoint& p : f.points) {
                if (p.tokens != 9) ++notNine;
                if (p.tokens < 4) ++fewTokens;
            }
        }
    std::printf("  .opp files: London %d (%d points), SF %d (%d points)\n", files["london"], points["london"], files["sf"],
                points["sf"]);
    expect(files["london"] == 324 && files["sf"] == 288, "612 route files (London 324, SF 288) (opp.md)");
    expect(badHeader == 0 && notNine == 0 && blank == 0 && longL == 0 && fewTokens == 0,
           "every route file has the shipped header, 9 columns per data line, no blank or long lines");
    expect(points["london"] == 8510 && points["sf"] == 5377, "8,510 (London) and 5,377 (SF) route points");
}

// Route resolution, the start snap and the finish ring, per city with its network and world.
void opponentCityChecks(const Vfs& vfs, const std::vector<CityEntry>& cities, const Expect& expect) {
    for (const CityEntry& ce : cities) {
        const bool L = iequals(ce.raceDir, "london");
        AssetCache assets(vfs);
        CityData city;
        std::string msg;
        if (!city.load(ce.mapName, vfs, assets, msg, ce.raceDir) || !city.network()) {
            expect(false, ce.raceDir + " loads with its AI network");
            continue;
        }
        ai::RoadNetwork& net = *city.network();
        // Every route file: route lengths and route points without an intersection.
        int minLen = 1 << 30, maxLen = 0, files = 0, outside = 0, roomZero = 0;
        long sumLen = 0;
        for (const std::string& path : vfs.listUnique("race/" + ce.raceDir + "/")) {
            if (!iendsWith(path, ".opp")) continue;
            const OppFile f = parseOpp(textOf(vfs.open(path)));
            const ai::OpponentRoute r = ai::resolveOpponentRoute(f, city, net);
            for (const OppPoint& p : f.points)
                if (city.findRoom(p.pos, 0) == 0) ++roomZero;
            ++files;
            const int n = int(r.route.size()) + r.pointsWithoutIntersection;
            minLen = std::min(minLen, n);
            maxLen = std::max(maxLen, n);
            sumLen += n;
            outside += r.pointsWithoutIntersection;
        }
        const double mean = files ? double(sumLen) / files : 0;
        std::printf("  %s: routes %d … %d intersections (mean %.1f); %d route points without an intersection; %d points in room 0\n",
                    ce.raceDir.c_str(), minLen, maxLen, mean, outside, roomZero);
        expect(roomZero == 0, ce.raceDir + ": every route point lies inside a room");
        if (L)
            expect(minLen == 4 && maxLen == 58 && std::fabs(mean - 24.3) < 0.05 && outside == 16,
                   "London routes of 4 … 58 intersections (mean 24.3), 16 route points without an intersection");
        else
            expect(minLen == 5 && maxLen == 42 && std::fabs(mean - 16.7) < 0.05 && outside == 30,
                   "SF routes of 5 … 42 intersections (mean 16.7), 30 route points without an intersection");
        if (!L) {
            const OppFile f = parseOpp(textOf(vfs.open("race/sf/race0-a-0.opp")));
            const ai::OpponentRoute r = ai::resolveOpponentRoute(f, city, net);
            const std::vector<int> want = {61, 60, 37, 186, 43, 195, 118};
            std::printf("  race0-a-0.opp: %d points, heading %.8f, destination (%.4f, %.6f, %.5f)\n", r.points, double(r.heading),
                        double(r.destination.x), double(r.destination.y), double(r.destination.z));
            expect(r.points == 9 && r.route == want && r.heading == 1.5705597f && r.start.x == -1152.0404f &&
                       r.destination.x == -1777.4153f && r.destination.z == -821.31543f,
                   "SF race0-a-0.opp: 9 points, θ 1.5705597, route 61 60 37 186 43 195 118, destination (−1777.4153, …, −821.31543)");
        }
        // Opponents created with the race data's numbers, the finish ring and the start snap.
        phys::CollisionWorld world;
        world.setupCity(vfs, city);
        int created[2][2] = {};  // [checkpoint/circuit][pro]
        int ringOk = 0, total = 0, destNear = 0, destFar = 0;
        for (RaceMode m : {RaceMode::Checkpoint, RaceMode::Circuit}) {
            const auto rows = parseRaceData(textOf(vfs.open(game::settingsPath(ce.raceDir, m))));
            for (int n = 0; n < game::raceCount(ce, m); ++n) {
                const auto wp = parseWaypoints(textOf(vfs.open(game::waypointPath(ce.raceDir, m, n))), 50, m == RaceMode::Circuit);
                if (wp.points.empty()) continue;
                const Waypoint& fin = m == RaceMode::Checkpoint ? wp.points.back() : wp.points.front();
                for (bool pro : {false, true}) {
                    const RaceAimap a = parseRaceAimap(textOf(vfs.open(game::raceAimapPath(ce.raceDir, game::raceName(m, n), pro))));
                    const float s = size_t(n) < rows.size() ? float(rows[size_t(n)].of(pro).opponents) : 0.0f;
                    int k = ai::OpponentSystem::countFor(a.opponentCount, s);
                    k = std::min(k, int(a.opponents.size()));
                    created[m == RaceMode::Circuit][pro] += k;
                    for (int i = 0; i < k; ++i) {
                        const OppFile f = parseOpp(textOf(vfs.open("race/" + ce.raceDir + "/" + a.opponents[size_t(i)].file)));
                        const ai::OpponentRoute r = ai::resolveOpponentRoute(f, city, net);
                        ++total;
                        if (r.route.size() >= 2) {
                            const vec3& c2 = net.intersections()[size_t(r.route[r.route.size() - 2])].centre;
                            const float dx = c2.x - fin.pos.x, dz = c2.z - fin.pos.z;
                            if (dx * dx + dz * dz >= 900.0f) ++ringOk;
                        }
                        const float ex = r.destination.x - fin.pos.x, ez = r.destination.z - fin.pos.z;
                        const float e2 = ex * ex + ez * ez;
                        if (e2 < 400.0f) ++destNear;
                        if (e2 > 900.0f) ++destFar;
                    }
                }
            }
        }
        std::printf("  %s opponents created: Checkpoint %d / %d, Circuit %d / %d; finish ring armed for %d of %d; destinations within 20 m %d, "
                    "beyond 30 m %d\n",
                    ce.raceDir.c_str(), created[0][0], created[0][1], created[1][0], created[1][1], ringOk, total, destNear, destFar);
        if (L) {
            expect(created[0][0] == 76 && created[0][1] == 78 && created[1][0] == 58 && created[1][1] == 59,
                   "London opponents created: Checkpoint 76 / 78, Circuit 58 / 59 (opponents.md)");
            expect(total == 271 && ringOk == 271 && destNear == 236 && destFar == 4,
                   "London: every second-to-last route intersection ≥ 30 m from the finish; 236 destinations within 20 m, 4 beyond 30 m");
        } else {
            expect(created[0][0] == 67 && created[0][1] == 69 && created[1][0] == 55 && created[1][1] == 55,
                   "SF opponents created: Checkpoint 67 / 69, Circuit 55 / 55 (opponents.md)");
            expect(total == 246 && ringOk == 246 && destNear == 191 && destFar == 36,
                   "SF: every second-to-last route intersection ≥ 30 m from the finish; 191 destinations within 20 m, 36 beyond 30 m");
            // Start snap (opponents.md "Checked against"): SF race0 opponents 0 and 4.
            ai::OpponentSystem os;
            ai::OpponentSystem::Setup st;
            const RaceAimap a = parseRaceAimap(textOf(vfs.open("race/sf/race0.aimap")));
            st.rows = a.opponents;
            st.countLine = a.opponentCount;
            st.opponentNumber = 99.0f;
            st.raceDir = "sf";
            st.loadModels = false;
            os.create(vfs, nullptr, city, net, world, st);
            os.reset(0.0);
            const float y4before = os.opponents().size() > 4 ? os.opponents()[4].q.y : 0;
            const float y0model = os.opponents().empty() ? 0 : os.opponents()[0].car->modelOrigin().y;
            os.groundSnap(world);
            const vec3 q0 = os.opponents().empty() ? vec3() : os.opponents()[0].q;
            const float y4 = os.opponents().size() > 4 ? os.opponents()[4].q.y : 0;
            std::printf("  SF race0 snap: opponent 0 model origin y %.4f -> reset point (%.4f, %.4f, %.5f); opponent 4 y %.3f -> %.3f\n",
                        double(y0model), double(q0.x), double(q0.y), double(q0.z), double(y4before), double(y4));
            // Police in races (police.md, "Detection" step 2): an idle cop with the player behind it
            // (within its opponent range) and an opponent 30 m in front draws once and chases it.
            {
                net.setRace(vfs, "race8", true);
                ai::PoliceSystem ps;
                ps.create(vfs, assets, city, net, world, 1.0f, "vpbug", nullptr);
                ps.reset();
                phys::Car player;
                player.setup(phys::loadCarTune(vfs, "vpbug", true));
                bool chased = false;
                int perp = -9;
                uint32_t rnd = 0;
                if (!ps.cops().empty() && !os.opponents().empty()) {
                    ai::Cop& cop = ps.cops()[0];
                    const vec3 cp = cop.car->body().p;
                    const vec3 f = cop.car->forward();
                    player.placeAt(vec3(cp.x - f.x * 20.0f, cp.y, cp.z - f.z * 20.0f), cop.entry.heading);
                    phys::Car& oc = *os.opponents()[0].car;
                    oc.placeAt(vec3(cp.x + f.x * 30.0f, cp.y, cp.z + f.z * 30.0f), cop.entry.heading);
                    std::vector<ai::PolicePlayer> pp(1), targets(1);
                    pp[0].car = &player;
                    targets[0].car = &oc;
                    // The shipped chance is 0: one draw, refused.
                    ps.updateCops(1.0f / 60.0f, pp, nullptr, 0.0, targets, os.driverView());
                    const bool refused = cop.state == ai::CopState::Idle && !cop.refused.empty() && cop.refused[0];
                    const uint32_t r1 = ps.randomState();
                    ps.updateCops(1.0f / 60.0f, pp, nullptr, 0.0, targets, os.driverView());
                    const bool noSecondDraw = ps.randomState() == r1;
                    // With chance 1 it chases (the roster gets the opponent as a "cop": rank UNKNOWN).
                    ps.reset();
                    cop.entry.f0 = 1.0f;
                    ps.updateCops(1.0f / 60.0f, pp, nullptr, 0.0, targets, os.driverView());
                    chased = refused && noSecondDraw && cop.state != ai::CopState::Idle;
                    perp = cop.perp;
                    rnd = ps.randomState();
                }
                std::printf("  SF race8 pro: %zu cops; cop 0 with an opponent in front: refused at chance 0, then chasing %d, perp %d\n",
                            ps.cops().size(), chased ? 1 : 0, perp);
                expect(ps.cops().size() == 8 && chased && perp == 1 && rnd != 1u,
                       "a cop draws once for an opponent in front of it: refused at the shipped chance 0, chased at chance 1");
                net.setRace(vfs, "roam", false);
            }
            // The player's race-menu choices reach the session (races.md, "The session's settings";
            // menus.md, "Locked settings"): Circuit laps 1 - 8, time of day, weather, cops and
            // pedestrians.
            {
                phys::Car player;
                player.setup(phys::loadCarTune(vfs, "vpmustang99", true));
                auto session = [&](RaceMode mode, int laps, int tod, int weather, float cops, float peds, float traffic,
                                   game::SessionValues& v, int& listLaps) {
                    game::RaceSession rs;
                    game::RaceSession::Setup su;
                    su.vfs = &vfs;
                    su.city = &city;
                    su.net = &net;
                    su.world = &world;
                    su.cityEntry = ce;
                    su.mode = mode;
                    su.race = 0;
                    su.playerCar = "vpmustang99";
                    su.laps = laps;
                    su.timeOfDay = tod;
                    su.weather = weather;
                    su.copDensity = cops;
                    su.pedDensity = peds;
                    su.trafficDensity = traffic;
                    std::string err;
                    const bool ok = rs.setup(su, player, err);
                    v = rs.values();
                    listLaps = rs.waypoints().laps();
                    return ok;
                };
                game::SessionValues own, chosen;
                int ownLaps = 0, chosenLaps = 0;
                const bool ok = session(RaceMode::Circuit, -1, -1, -1, -1.0f, -1.0f, -1.0f, own, ownLaps) &&
                                session(RaceMode::Circuit, 5, 3, 3, 1.0f, 0.75f, 0.9f, chosen, chosenLaps);
                // Traffic: Checkpoint and Blitz take the player's; Circuit keeps its own (menus.md).
                game::SessionValues cpOwn, cpChosen;
                int cpLaps = 0;
                const bool okCp = session(RaceMode::Checkpoint, -1, -1, -1, -1.0f, -1.0f, -1.0f, cpOwn, cpLaps) &&
                                  session(RaceMode::Checkpoint, 5, -1, -1, -1.0f, -1.0f, 0.9f, cpChosen, cpLaps);
                std::printf("  SF checkpoint 0: traffic %.2f, chosen 0.90 -> %.2f (laps %d); circuit 0 traffic %.2f with 0.90 chosen\n",
                            double(cpOwn.trafficDensity), double(cpChosen.trafficDensity), cpChosen.laps, double(chosen.trafficDensity));
                expect(okCp && cpChosen.trafficDensity == 0.9f && cpChosen.laps == 1 && chosen.trafficDensity == own.trafficDensity,
                       "SF checkpoint 0 runs the chosen traffic 0.9 (laps stay 1); circuit 0 keeps its own traffic");
                const auto rows = parseRaceData(textOf(vfs.open(game::settingsPath("sf", RaceMode::Circuit))));
                const int numLaps = rows.empty() ? -1 : rows[0].of(false).numLaps;
                const float rowCops = rows.empty() ? -1.0f : float(rows[0].of(false).cops);
                const float rowPeds = rows.empty() ? -1.0f : rows[0].of(false).peds;
                std::printf("  SF circuit 0: own laps %d (NumLaps %d), weather %d, time %d, cops %.2f, pedestrians %.2f; chosen laps %d "
                            "(list %d), weather %d, time %d, cops %.2f, pedestrians %.2f\n",
                            own.laps, numLaps, own.weather, own.timeOfDay, double(own.copDensity), double(own.pedDensity), chosen.laps,
                            chosenLaps, chosen.weather, chosen.timeOfDay, double(chosen.copDensity), double(chosen.pedDensity));
                expect(ok && own.laps == numLaps && ownLaps == numLaps && own.copDensity == rowCops && own.pedDensity == rowPeds &&
                           chosen.laps == 5 && chosenLaps == 5 && chosen.weather == 3 && chosen.timeOfDay == 3 &&
                           chosen.copDensity == 1.0f && chosen.pedDensity == 0.75f,
                       "SF circuit 0: the race's own values, or the player's 5 laps, rain, night, cops 1.0 and pedestrians 0.75 when chosen");
            }
            expect(std::fabs(y0model - 64.65f) < 1e-3f && q0.x == -1152.0404f && std::fabs(q0.y - 65.75f) < 1e-4f &&
                       q0.z == -395.00256f && std::fabs(y4before - 63.367f) < 1e-3f && std::fabs(y4 - 64.285f) < 1e-3f,
                   "SF race0 start snap: opponent 0 to (−1152.0404, 65.75, −395.00256), opponent 4 y 63.367 → 64.285");
        }
    }
}

void scoringChecks(const Vfs& vfs, const Expect& expect) {
    float lo = 1e9f, hi = -1e9f;
    int cars = 0;
    for (const std::string& name : builtInCarNames()) {
        const InfoFile f = parseInfoFile(textOf(vfs.open("tune/" + name + ".info")));
        const std::string v = f.get("ScoringBias");
        if (v.empty()) continue;
        const float b = float(cAtof(v.c_str()));
        lo = std::min(lo, b);
        hi = std::max(hi, b);
        ++cars;
    }
    std::printf("  ScoringBias %.0f … %.0f over %d cars\n", double(lo), double(hi), cars);
    expect(cars == 20 && lo == 4.0f && hi == 100.0f, "ScoringBias 4 – 100 over the 20 cars (results.md)");
}

// The waypoint list's rules on a made-up list (waypoints.md): reset, S / X, crossing, layouts.
void waypointListChecks(const Expect& expect) {
    auto wp = [](float x, float z) {
        Waypoint w;
        w.pos = vec3(x, 0, z);
        w.heading = 0;  // the gate line runs along x; a car going −z crosses it
        w.radius = 10;
        computeGate(w);
        return w;
    };
    std::vector<Waypoint> pts = {wp(0, 0), wp(0, -50), wp(0, -100), wp(0, -150), wp(0, -200)};
    const vec3 box(2, 2, 4);
    auto at = [](const Waypoint& w) {
        game::GatePose p;
        p.P = w.pos;
        return p;
    };
    game::WaypointList L;
    L.set(pts, game::Layout::CheckpointsFinish, 1, false);
    bool ok = !L.shown(0) && L.shown(1) && L.shown(3) && !L.shown(4) && L.passed(0) && L.count() == 1 && L.target() == 1;
    L.stepTarget(1);
    ok = ok && L.target() == 2;
    L.stepTarget(-1);
    L.stepTarget(-1);  // 1 − 1 = 0 lands on n − 2
    ok = ok && L.target() == 3;
    std::vector<game::GateEvent> ev;
    L.frame(at(pts[4]), box, ev);  // the finish too early: ignored
    ok = ok && ev.empty() && !L.passed(4);
    L.frame(at(pts[2]), box, ev);
    ok = ok && ev.size() == 1 && ev[0].index == 2 && L.passed(2) && L.count() == 2;
    ev.clear();
    L.frame(at(pts[1]), box, ev);
    L.frame(at(pts[3]), box, ev);
    ok = ok && L.count() == 4 && L.shown(4) && L.target() == 4 && !ev.empty() && ev.back().kind == game::GateEvent::FinalCheckpoint;
    L.frame(at(pts[4]), box, ev);
    ok = ok && L.done() && !L.arrowOn();
    expect(ok, "waypoint list, Checkpoint layout: reset, S/X stepping, early finish ignored, final checkpoint, done");

    game::WaypointList F;
    F.set(pts, game::Layout::FixedOrder, 1, true);
    ev.clear();
    F.frame(at(pts[2]), box, ev);  // not the target
    bool ok5 = ev.empty() && F.target() == 1 && F.shown(1) && !F.shown(2);
    for (int i = 1; i <= 4; ++i) F.frame(at(pts[size_t(i)]), box, ev);
    ok5 = ok5 && F.done() && F.count() == 5;
    expect(ok5, "waypoint list, fixed order with only-target: only the target counts and is shown; done after the last");

    std::vector<Waypoint> loop = {wp(0, 0), wp(0, -50), wp(0, -100)};
    game::WaypointList C;
    C.set(loop, game::Layout::Laps, 2, false);
    ev.clear();
    for (int lap = 0; lap < 2; ++lap)
        for (int i : {1, 2, 0}) C.frame(at(loop[size_t(i)]), box, ev);
    int laps = 0;
    bool lastDone = false;
    for (const auto& e : ev)
        if (e.kind == game::GateEvent::LapDone) {
            ++laps;
            lastDone = e.done;
        }
    expect(laps == 2 && lastDone && C.done() && C.lap() == 2 && C.count() == 7,
           "waypoint list, laps: 1, 2, 0 completes a lap; two laps end the race; the count goes on over laps");

    // The segment test: crossing, parallel and the box rule.
    const bool cross = game::segmentTest(vec2{0, -1}, vec2{0, 1}, vec2{-1, 0}, vec2{1, 0}, 0.0f);
    const bool parallel = game::segmentTest(vec2{0, 0}, vec2{1, 0}, vec2{0, 1}, vec2{1, 1}, 0.0f);
    const bool apart = game::segmentTest(vec2{5, -1}, vec2{5, 1}, vec2{-1, 0}, vec2{1, 0}, 0.0f);
    expect(cross && !parallel && !apart, "segment test: crossing segments cross, parallel ones and distant ones do not");
}

}  // namespace

void testRaces(Vfs& vfs, const Expect& expect) {
    std::printf("== Race modes (spec/game/races.md, waypoints.md, crash-course.md)\n");
    const std::vector<CityEntry> cities = shippedCities(vfs);
    waypointListChecks(expect);
    catalogueChecks(vfs, cities, expect);
    settingsChecks(vfs, cities, expect);
    waypointChecks(vfs, cities, expect);
    crashChecks(vfs, cities, expect);
    std::printf("== Race opponents (spec/ai/opponents.md, spec/formats/opp.md)\n");
    opponentRowChecks(vfs, cities, expect);
    oppFileChecks(vfs, cities, expect);
    opponentCityChecks(vfs, cities, expect);
    scoringChecks(vfs, expect);
}
