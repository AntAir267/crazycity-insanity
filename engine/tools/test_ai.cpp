// Native checks of the AI road network, aimaps and traffic control against the counts in
// spec/formats/bai.md, aimap.md, spec/ai/road-network.md and traffic-control.md.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

#include "ai/police.h"
#include "ai/road_network.h"
#include "ai/route_search.h"
#include "ai/traffic.h"
#include "ai/traffic_control.h"
#include "physics/collision_world.h"
#include "city/city_data.h"
#include "core/strutil.h"
#include "formats/aimap.h"
#include "formats/bai.h"
#include "formats/cinfo.h"
#include "formats/inst.h"
#include "formats/pkg.h"
#include "model/asset_cache.h"
#include "physics/car.h"
#include "physics/car_tune.h"
#include "physics/drive_check.h"
#include "physics/scene.h"
#include "vfs/vfs.h"

using namespace cc;

namespace {

using Expect = std::function<void(bool, const std::string&)>;

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

float len3(const vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

std::string hist(const std::map<std::string, int>& h) {
    std::string s;
    for (const auto& kv : h) s += strf("%s%s %d", s.empty() ? "" : ", ", kv.first.c_str(), kv.second);
    return s;
}

void rawFileChecks(const Vfs& vfs, const Expect& expect) {
    struct Want {
        const char* file;
        bool sup;
        int ni, nr, rooms;
    };
    const Want wants[] = {{"city/london.bai", false, 328, 540, 1342}, {"city/london_sup.bai", true, 0, 70, 0},
                          {"city/sf.bai", false, 214, 379, 1172},     {"city/sf_sup.bai", true, 0, 49, 0},
                          {"city/london_bak.bai", false, 328, 540, 1342}, {"city/sf_bak.bai", false, 214, 379, 1172},
                          {"city/sfai.bai", false, 215, 474, 1329}};
    for (const Want& w : wants) {
        BytesPtr b = vfs.open(w.file);
        BaiFile f;
        std::string err;
        const bool ok = b && parseBaiFile(b->data(), b->size(), w.sup, f, err);
        std::printf("  %-20s %s: %zu intersections, %zu roads, %zu table rooms%s\n", w.file, ok ? "read" : "FAILED",
                    f.intersections.size(), f.roads.size(), f.ambientTable.size(), err.empty() ? "" : (" (" + err + ")").c_str());
        expect(ok && err.empty() && int(f.intersections.size()) == w.ni && int(f.roads.size()) == w.nr &&
                   int(f.ambientTable.size()) == w.rooms && int(f.pedTable.size()) == w.rooms,
               strf("%s read to its last byte: %d / %d / %d", w.file, w.ni, w.nr, w.rooms));
    }
    int bai1 = 0, refused = 0;
    for (const auto& path : vfs.listUnique("city/")) {
        if (!(iendsWith(path, ".bai.tmp") || iendsWith(path, ".bai.bak"))) continue;
        BytesPtr b = vfs.open(path);
        if (!b || b->size() < 4 || std::string(b->begin(), b->begin() + 4) != "BAI1") continue;
        ++bai1;
        BaiFile f;
        std::string err;
        if (!parseBaiFile(b->data(), b->size(), true, f, err)) ++refused;
    }
    expect(bai1 == 2 && refused == 2, strf("the two BAI1 files are refused (%d of %d)", refused, bai1));

    for (const char* city : {"london", "sf"}) {
        const bool L = std::string(city) == "london";
        BytesPtr b = vfs.open(strf("city/%s.bai", city));
        BaiFile f;
        std::string err;
        if (!b || !parseBaiFile(b->data(), b->size(), false, f, err)) continue;
        // Line and centre distances, axes, point order.
        int badDist = 0, zSections = 0, zBad = 0, z0 = 0, xBad = 0, aLanes = 0, aRev = 0, aCable = 0, aCableRev = 0,
            loopRoads = 0;
        for (const BaiRoad& r : f.roads) {
            const int n = r.n;
            for (const BaiSide& s : r.side)
                for (size_t l = 0; l < s.lines.size(); ++l) {
                    float acc = 0;
                    for (int i = 1; i < n; ++i) {
                        acc += len3(s.lines[l][size_t(i)] - s.lines[l][size_t(i - 1)]);
                        if (std::fabs(acc - s.distances[l][size_t(i)]) > 0.05f) {
                            ++badDist;
                            break;
                        }
                    }
                }
            float acc = 0;
            for (int i = 1; i < n; ++i) {
                acc += len3(r.C[size_t(i)] - r.C[size_t(i - 1)]);
                if (std::fabs(acc - r.centreDist[size_t(i)]) > 0.05f) {
                    ++badDist;
                    break;
                }
            }
            for (int i = 1; i < n; ++i) {
                ++zSections;
                vec3 d = r.C[size_t(i)] - r.C[size_t(i - 1)];
                d = d * (1.0f / len3(d));
                if (len3(r.Z[size_t(i)] + d) > 0.001f) ++zBad;
            }
            if (n >= 2 && len3(r.Z[0] - r.Z[1]) < 1e-5f) ++z0;
            for (int i = 0; i < n; ++i) {
                const vec3& Y = r.Y[size_t(i)];
                const vec3 mz = r.Z[size_t(i)] * -1.0f;
                const vec3 c = cross(Y, mz);
                if (len3(c - r.X[size_t(i)]) > 0.02f) ++xBad;
            }
            const BaiSide& A = r.side[0];
            for (int l = 0; l < A.lanes; ++l) {
                ++aLanes;
                if (len3(A.lines[size_t(l)][0] - r.C[size_t(n - 1)]) < len3(A.lines[size_t(l)][0] - r.C[0])) ++aRev;
            }
            if (A.cable) {
                ++aCable;
                if (len3(A.cableLine[0] - r.C[size_t(n - 1)]) < len3(A.cableLine[0] - r.C[0])) ++aCableRev;
            }
            if (r.end[0].intersection == r.end[1].intersection) ++loopRoads;
        }
        expect(badDist == 0, strf("%s: line and centre distances are the cumulative point distances (%d off)", city, badDist));
        std::printf("  %s main roads: %d sections i >= 1 (%d with Z off), Z[0] = Z[1] on %d, X off on %d\n", city,
                    zSections, zBad, z0, xBad);
        expect(aRev == aLanes, strf("%s: side A lanes in travel order (%d of %d)", city, aRev, aLanes));
        expect(aCable == aCableRev, strf("%s: side A cable lines in travel order (%d of %d)", city, aCableRev, aCable));
        expect(loopRoads == (L ? 3 : 1), strf("%s: %d loop roads", city, loopRoads));
        // List order around the stored centres and slots.
        int unsorted = 0, ends = 0, slotOk = 0;
        for (size_t I = 0; I < f.intersections.size(); ++I) {
            const BaiIntersectionRec& x = f.intersections[I];
            float prev = -1e9f;
            for (size_t k = 0; k < x.roads.size(); ++k) {
                const BaiRoad& r = f.roads[x.roads[k]];
                const vec3& p = r.end[0].intersection == I ? r.C[size_t(r.n - 1)] : r.C[0];
                const float key = std::atan2(p.x - x.centre.x, p.z - x.centre.z);
                if (key < prev) ++unsorted;
                prev = key;
            }
        }
        int loopEnds = 0;
        for (size_t ri = 0; ri < f.roads.size(); ++ri) {
            const BaiRoad& r = f.roads[ri];
            const bool loop = r.end[0].intersection == r.end[1].intersection;
            for (const BaiEnd& e : r.end) {
                const auto& l = f.intersections[e.intersection].roads;
                const bool ok = e.slot < l.size() && l[e.slot] == ri;
                if (loop) {
                    if (ok) ++loopEnds;
                    continue;
                }
                ++ends;
                if (ok) ++slotOk;
            }
        }
        expect(unsorted == 0, strf("%s: every intersection's road list ascends in atan2 order (%d out of order)", city, unsorted));
        expect(ends == slotOk, strf("%s: all %d non-loop end slots match the list (%d)", city, ends, slotOk));
        expect(slotOk + loopEnds == (L ? 1077 : 757),
               strf("%s: %d road ends have the slot of their list position (spec: %d)", city, slotOk + loopEnds, L ? 1077 : 757));
        if (L) {
            const BaiRoad& r = f.roads[88];
            const BaiSide& B = r.side[1];
            auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
            const bool shape = r.n == 4 && r.flags == 0x08 && near(r.halfWidth, 11.0f) && r.side[0].lanes == 2 && B.lanes == 2;
            const bool offs = B.offsets.size() == 3 && near(B.offsets[0], 2.0f) && near(B.offsets[1], 6.0f) && near(B.offsets[2], 9.5f);
            const bool band = near(B.band[0], -11.0f) && near(B.band[1], 4.0f) && near(B.band[2], 4.0f) && near(B.band[3], 7.75f) &&
                              near(B.band[4], 7.75f) && near(B.band[5], 11.0f);
            const float kerb = len3(B.kerb[1] - r.C[1]), edge = len3(B.outerEdge[1] - r.C[1]);
            expect(shape && offs && band && std::fabs(kerb - 8.0f) < 0.05f && std::fabs(edge - 11.0f) < 0.05f,
                   strf("London road 88: n 4, flags 0x08, half width 11, lanes (2,2), offsets (2,6,9.5), band edges, kerb "
                        "%.2f m, edge %.2f m",
                        double(kerb), double(edge)));
            const BaiIntersectionRec& x = f.intersections[10];
            expect(x.room == 1023 && std::fabs(x.centre.x + 230.0f) < 0.05f && std::fabs(x.centre.y + 0.1f) < 0.05f &&
                       std::fabs(x.centre.z - 333.5f) < 0.05f,
                   "London intersection 10: room 1023, centre (-230.0, -0.1, 333.5)");
        }
    }
}

void aimapChecks(const Vfs& vfs, const Expect& expect) {
    const CityAimap L = parseCityAimap(textOf(vfs.open("city/london.aimap")));
    std::string types;
    for (const auto& t : L.ambient) types += strf(" %s<%.2f", t.model.c_str(), double(t.threshold));
    std::printf("  london.aimap: limit %.1f, left %d, %zu types:%s\n", double(L.speedLimit), int(L.driveOnLeft),
                L.ambient.size(), types.c_str());
    expect(L.speedLimit == 15.0f && L.driveOnLeft && L.ambient.size() == 13 && L.ambient[0].model == "va_compact_s" &&
               std::fabs(L.ambient[0].threshold - 0.07f) < 1e-6f && L.ambient[1].model == "va_diesels_s" &&
               L.ambient[12].model == "va_ddbus_l" && L.ambient[12].threshold == 1.0f && L.ambient[11].model == "va_compact_s" &&
               std::fabs(L.ambient[11].threshold - 0.98f) < 1e-6f && L.peds.size() == 2 &&
               L.lightSingle == "sp_traflitsingle_ped_l" && L.lightDual == "sp_traflitsingle_ped_l" && !L.haveSubway,
           "london.aimap: 15, drive on the left, 13 types, 2 pedestrian pairs, sp_traflitsingle_ped_l lights, no subway");
    const CityAimap S = parseCityAimap(textOf(vfs.open("city/sf.aimap")));
    expect(S.speedLimit == 15.0f && !S.driveOnLeft && S.ambient.size() == 11 && S.ambient[0].model == "va_bus_f" &&
               std::fabs(S.ambient[0].threshold - 0.01f) < 1e-6f && S.ambient.back().model == "va_smallsuv_s" &&
               S.ambient.back().threshold == 1.0f && S.peds.size() == 2 && S.lightSingle == "sp_traflitsingle_f" &&
               S.lightDual == "sp_traflitdual_f",
           "sf.aimap: 15, drive on the right, 11 types, 2 pedestrian pairs, default lights");

    int files = 0, withExceptions = 0;
    std::set<std::string> withAmbient;
    std::set<std::string> models;
    for (const auto& t : L.ambient) models.insert(t.model);
    for (const auto& t : S.ambient) models.insert(t.model);
    for (const auto& p : L.peds) models.insert(p.good), models.insert(p.bad);
    for (const auto& p : S.peds) models.insert(p.good), models.insert(p.bad);
    for (const char* m : {"sp_traflitsingle_ped_l", "sp_traflitsingle_f", "sp_traflitdual_f"}) models.insert(m);
    for (const char* c : {"london", "sf"})
        for (const auto& path : vfs.listUnique(strf("race/%s/", c))) {
            if (!(iendsWith(path, ".aimap") || iendsWith(path, ".aimap_p"))) continue;
            const std::string base = toLower(baseName(path));
            ++files;
            const RaceAimap r = parseRaceAimap(textOf(vfs.open(path)));
            if (!r.exceptions.empty()) ++withExceptions;
            if (!r.ambient.empty()) withAmbient.insert(path);
            for (const auto& t : r.ambient) models.insert(t.model);
            for (const auto& p : r.police) models.insert(p.model);
            for (const auto& p : r.peds) models.insert(p.good), models.insert(p.bad);
        }
    expect(files == 207 && withExceptions == 88, strf("%d race aimaps, %d with exceptions (207, 88)", files, withExceptions));
    const RaceAimap r2 = parseRaceAimap(textOf(vfs.open("race/sf/race2.aimap")));
    bool zero = r2.exceptions.size() == 12;
    for (const auto& e : r2.exceptions) zero = zero && e.density == 0.0f && e.speedLimit == 0.0f;
    expect(zero, strf("race/sf/race2.aimap: 12 exceptions with density 0 and limit 0 (%zu)", r2.exceptions.size()));
    const RaceAimap lr = parseRaceAimap(textOf(vfs.open("race/london/roam.aimap")));
    bool noCab = true;
    for (const auto& t : lr.ambient) noCab = noCab && t.model != "va_cab_l";
    expect(withAmbient.size() == 3 && lr.ambient.size() == 12 && noCab && lr.ambient[0].model == "va_compact_s" &&
               lr.ambient.back().model == "va_ddbus_l",
           strf("only London's roam.aimap/_p/roambak have ambient types: 12 rows without va_cab_l (%zu files)", withAmbient.size()));
    const RaceAimap sr = parseRaceAimap(textOf(vfs.open("race/sf/roam.aimap")));
    const RaceAimap srp = parseRaceAimap(textOf(vfs.open("race/sf/roam.aimap_p")));
    expect(lr.police.size() == 20 && sr.police.size() == 19 && srp.police.size() == 20 && lr.laneChanges && sr.laneChanges &&
               lr.copChaseDistance == 250.0f && sr.copChaseDistance == 250.0f,
           strf("Cruise police: London %zu, SF %zu (_p %zu); lane changes on, chase distance 250", lr.police.size(),
                sr.police.size(), srp.police.size()));
    int missing = 0;
    for (const auto& m : models)
        if (!vfs.exists("geometry/" + m + ".pkg")) {
            ++missing;
            std::printf("  no geometry/%s.pkg\n", m.c_str());
        }
    expect(missing == 0, strf("every ambient, police, pedestrian and light model has a package (%zu models)", models.size()));
}

void networkChecks(const Vfs& vfs, const Expect& expect) {
    for (const char* city : {"london", "sf"}) {
        const bool L = std::string(city) == "london";
        CityData cd;
        AssetCache assets(vfs);
        std::string msg;
        if (!cd.load(city, vfs, assets, msg) || !cd.network()) {
            expect(false, strf("%s loads with its AI network", city));
            continue;
        }
        const ai::RoadNetwork& net = *cd.network();
        const auto& roads = net.roads();
        const int NR = net.mainRoadCount();
        std::printf("  %s: %d roads + %zu shortcuts, %zu intersections, %zu light sets\n", city, NR, roads.size() - size_t(NR),
                    net.intersections().size(), net.control().sets().size());
        std::map<std::string, int> lanes, flags;
        int noLaneOpen = 0, supBad = 0, cable = 0, cableBoth = 0, subwayB = 0, subwayA = 0;
        int ctl[4] = {}, supCtlBad = 0;
        std::map<float, int> limits;
        for (int ri = 0; ri < int(roads.size()); ++ri) {
            const ai::Road& r = roads[size_t(ri)];
            for (const ai::RoadSide& s : r.side)
                if (s.lanes == 0 && !(s.flags & 1)) ++noLaneOpen;
            ++limits[r.speedLimit];
            if (r.shortcut) {
                if (!(r.side[0].lanes == 0 && r.side[0].sidewalks == 0 && r.side[1].lanes == 1 && r.side[1].sidewalks == 0 &&
                      r.flags == 0 && r.halfWidth == 2.5f))
                    ++supBad;
                for (const ai::RoadEnd& e : r.end)
                    if (e.control != 3) ++supCtlBad;
                continue;
            }
            ++lanes[strf("(%d,%d)", r.side[0].lanes, r.side[1].lanes)];
            ++flags[strf("0x%02X", r.flags)];
            for (const ai::RoadEnd& e : r.end) ++ctl[std::clamp(e.control, 0, 3)];
            if (r.side[0].cable || r.side[1].cable) ++cable;
            if (r.side[0].cable && r.side[1].cable) ++cableBoth;
            if (r.side[1].subway) ++subwayB;
            if (r.side[0].subway) ++subwayA;
        }
        std::printf("  %s lanes (A,B): %s\n  %s flags: %s\n", city, hist(lanes).c_str(), city, hist(flags).c_str());
        if (L) {
            expect(lanes["(0,1)"] == 129 && lanes["(0,2)"] == 64 && lanes["(0,3)"] == 36 && lanes["(0,4)"] == 23 &&
                       lanes["(1,1)"] == 142 && lanes["(2,2)"] == 118 && lanes["(3,3)"] == 28 && lanes.size() == 7,
                   "London lanes (A,B) on main roads match");
            expect(flags["0x08"] == 400 && flags["0x0A"] == 69 && flags["0x00"] == 61 && flags["0x02"] == 10 && flags.size() == 4,
                   "London road flags match");
            expect(limits.size() == 1 && limits[15.0f] == 610, "London: speed limit 15 on all 610 roads");
            expect(ctl[0] == 45 && ctl[1] == 519 && ctl[3] == 516 && ctl[2] == 0, "London end controls: stop 45, light 519, none 516");
            expect(subwayB == 28 && subwayA == 0, strf("London: subway lines on side B of 28 roads (%d, A %d)", subwayB, subwayA));
        } else {
            expect(lanes["(0,1)"] == 16 && lanes["(0,2)"] == 23 && lanes["(0,4)"] == 69 && lanes["(1,0)"] == 9 &&
                       lanes["(1,1)"] == 110 && lanes["(2,2)"] == 122 && lanes["(3,3)"] == 30 && lanes.size() == 7,
                   "SF lanes (A,B) on main roads match");
            expect(flags["0x00"] == 286 && flags["0x08"] == 63 && flags["0x02"] == 17 && flags["0x01"] == 5 &&
                       flags["0x05"] == 4 && flags["0x0A"] == 3 && flags["0x09"] == 1 && flags.size() == 7,
                   "SF road flags match");
            expect(limits.size() == 2 && limits[27.5f] == 4 && limits[15.0f] == 424, "SF: limit 15, and 27.5 on the 4 freeway roads");
            expect(ctl[0] == 36 && ctl[1] == 533 && ctl[3] == 189 && ctl[2] == 0, "SF end controls: stop 36, light 533, none 189");
            expect(cable == 21 && cableBoth == 21, strf("SF: cable-car lines on both sides of 21 roads (%d)", cable));
        }
        expect(noLaneOpen == 0, strf("%s: every side without lanes has side-flag bit 0 (%d not)", city, noLaneOpen));
        expect(supBad == 0 && supCtlBad == 0 && int(roads.size()) - NR == (L ? 70 : 49),
               strf("%s: shortcuts are one B lane, flags 0, half width 2.5, ends control 3", city));
        expect(net.stats().listMismatches == 0, strf("%s: slots match the lists after the shortcut merge (%d off)", city,
                                                     net.stats().listMismatches));
        int z0all = 0, zSec = 0, zOff = 0, xOff = 0;
        for (const ai::Road& r : roads) {
            if (r.n >= 2 && len3(r.Z[0] - r.Z[1]) < 1e-5f) ++z0all;
            for (int i = 1; i < r.n; ++i) {
                ++zSec;
                vec3 d = r.C[size_t(i)] - r.C[size_t(i - 1)];
                d = d * (1.0f / len3(d));
                if (len3(r.Z[size_t(i)] + d) > 0.001f) ++zOff;
            }
            for (int i = 0; i < r.n; ++i)
                if (len3(cross(r.Y[size_t(i)], r.Z[size_t(i)] * -1.0f) - r.X[size_t(i)]) > 0.02f) ++xOff;
        }
        std::printf("  %s: Z[0] = Z[1] (to 1e-5) on %d of %zu roads (spec: %d); X = Y x (-Z) off by > 0.02 on %d sections "
                    "(spec: %d)\n",
                    city, z0all, roads.size(), L ? 562 : 389, xOff, L ? 20 : 44);
        expect(zOff == 0 && zSec == (L ? 2644 : 2066),
               strf("%s: Z[i] = -dir(C[i-1] -> C[i]) on all %d sections i >= 1 (%d off)", city, zSec, zOff));

        // Mirroring (London): side B lane 0 is the kerb lane.
        if (L) {
            const ai::Road& r = roads[88];
            const vec3 d0 = r.side[1].lines[0].P[1] - r.C[1], d1 = r.side[1].lines[1].P[1] - r.C[1];
            const float x0 = dot(d0, r.X[1]), x1 = dot(d1, r.X[1]);
            expect(x0 > x1 && x1 > 0.0f, strf("London road 88 after mirroring: side B on +X, lane 0 outside lane 1 (%.2f, %.2f)",
                                              double(x0), double(x1)));
        }

        // Light sets.
        BaiFile raw;
        {
            BytesPtr b = vfs.open(strf("city/%s.bai", city));
            std::string err;
            if (b) parseBaiFile(b->data(), b->size(), false, raw, err);
        }
        const ai::TrafficControl& tc = net.control();
        std::map<std::string, int> skl, sizes, models;
        int facingOff = 0, below = 0, rule3 = 0;
        for (const ai::LightSet& s : tc.sets()) {
            ++skl[strf("(%d,%zu,%d)", s.sources, s.lights.size(), s.kind)];
            ++sizes[strf("%zu", s.lights.size())];
            const vec3 centre = size_t(s.intersection) < raw.intersections.size() ? raw.intersections[size_t(s.intersection)].centre
                                                                                  : vec3();
            for (const ai::TrafficLight& l : s.lights) {
                ++models[l.model];
                const ai::RoadEnd& e = roads[size_t(l.road)].end[l.end];
                if (std::fabs(len3(e.facingPoint - e.signPos) - 1.0f) > 0.001f) ++facingOff;
                if (std::fabs((centre.y - e.signPos.y) - 0.1f) < 0.05f) ++below;
            }
        }
        for (const ai::Road& r : roads)
            for (const ai::RoadEnd& e : r.end)
                if (e.turnRule == 3) ++rule3;
        std::printf("  %s light sets (sources,lights,kind): %s\n  %s sizes: %s; models: %s\n", city, hist(skl).c_str(), city,
                    hist(sizes).c_str(), hist(models).c_str());
        const auto& st = tc.stats();
        if (L) {
            expect(tc.sets().size() == 218 && st.kinds[1] == 128 && st.kinds[0] == 90 && st.kinds[2] == 0 && st.lights == 519 &&
                       models["sp_traflitsingle_ped_l"] == 519,
                   "London: 218 sets (128 kind 1, 90 kind 0), 519 lights, all sp_traflitsingle_ped_l");
            expect(skl["(1,1,1)"] == 16 && skl["(2,2,1)"] == 52 && skl["(3,3,1)"] == 41 && skl["(4,4,1)"] == 19 &&
                       skl["(2,1,0)"] == 14 && skl["(3,2,0)"] == 44 && skl["(4,3,0)"] == 17,
                   "London (sources, lights, kind) counts match");
            expect(sizes["1"] == 30 && sizes["2"] == 102 && sizes["3"] == 60 && sizes["4"] == 25 && sizes["5"] == 1,
                   "London light sizes 30/102/60/25/1");
            expect(below == 508, strf("London: %d of 519 poles stand 0.1 m below the intersection centre (508)", below));
        } else {
            expect(tc.sets().size() == 182 && st.kinds[1] == 116 && st.kinds[0] == 66 && st.kinds[2] == 0 && st.lights == 533 &&
                       models["sp_traflitdual_f"] == 356 && models["sp_traflitsingle_f"] == 177,
                   "SF: 182 sets (116, 66), 533 lights: 356 dual, 177 single");
            expect(sizes["1"] == 9 && sizes["2"] == 43 && sizes["3"] == 83 && sizes["4"] == 46 && sizes["5"] == 1,
                   "SF light sizes 9/43/83/46/1");
        }
        expect(facingOff == 0, strf("%s: every facing point is 1.0 m from its sign position (%d not)", city, facingOff));
        expect(rule3 == 0, strf("%s: no turn rule 3 (%d)", city, rule3));
        expect(st.poles == st.lights, strf("%s: a pole for every light (%d of %d)", city, st.poles, st.lights));
        // Every pole is a breakable street prop in a room.
        int poleRooms = 0;
        for (const ai::LightSet& s : tc.sets())
            for (const ai::TrafficLight& l : s.lights)
                if (l.pole >= 0 && cd.objects()[size_t(l.pole)].room > 0) ++poleRooms;
        expect(poleRooms == st.lights, strf("%s: every pole lies in a room (%d)", city, poleRooms));
        // traffic-control.md: lights whose number is not their road's list slot (227 London, 200 SF
        // with the room centres of psdl.md, "Room bounding sphere").
        int slotDiffers = 0;
        for (const ai::LightSet& s : tc.sets()) {
            const ai::Intersection& x = net.intersections()[size_t(s.intersection)];
            for (size_t c = 0; c < s.lights.size(); ++c)
                if (c >= x.roads.size() || x.roads[c] != s.lights[c].road) ++slotDiffers;
        }
        expect(slotDiffers == (L ? 227 : 200), strf("%s: %d lights differ from their road slot (%d)", city, slotDiffers, L ? 227 : 200));

        // Stop signs sit on sp_stop_f props of <c>_ai.inst.
        std::vector<vec3> stops;
        if (BytesPtr b = vfs.open(strf("city/%s_ai.inst", city))) {
            std::vector<InstRecord> recs;
            std::string err;
            parseInst(b->data(), b->size(), recs, err);
            for (const auto& r : recs)
                if (iequals(r.name, "sp_stop_f")) stops.push_back(vec3(r.matrix[9], r.matrix[10], r.matrix[11]));
        }
        int stopEnds = 0, matched = 0;
        for (int ri = 0; ri < NR; ++ri)
            for (const ai::RoadEnd& e : roads[size_t(ri)].end) {
                if (e.control != 0) continue;
                ++stopEnds;
                for (const vec3& p : stops)
                    if (std::fabs(p.x - e.signPos.x) < 0.01f && std::fabs(p.z - e.signPos.z) < 0.01f) {
                        ++matched;
                        break;
                    }
            }
        expect(stopEnds == matched && int(stops.size()) == (L ? 67 : 40),
               strf("%s: all %d stop approaches stand (in x and z) on one of the %zu sp_stop_f signs (%d)", city, stopEnds, stops.size(), matched));

        // The ten glow meshes of the light models.
        for (const char* m : {"sp_traflitsingle_ped_l", "sp_traflitsingle_f", "sp_traflitdual_f"}) {
            auto pkg = assets.package(m, false);
            int found = 0;
            for (const char* g : {"REDGLOWDAY_H", "YELLOWGLOWDAY_H", "GREENGLOWDAY_H", "WALK_DAY_H", "NOWALK_DAY_H",
                                  "REDGLOWNIGHT_H", "YELLOWGLOWNIGHT_H", "GREENGLOWNIGHT_H", "WALK_NIGHT_H", "NOWALK_NIGHT_H"})
                if (pkg)
                    for (const auto& e : pkg->entries)
                        if (iequals(e.name, g) && e.mesh >= 0) {
                            ++found;
                            break;
                        }
            if (L) expect(found == 10, strf("%s has the ten glow meshes (%d)", m, found));
        }

        // Next-road choices of every open lane end.
        std::map<int, int> choices;
        int none = 0;
        for (int ri = 0; ri < int(roads.size()); ++ri)
            for (int s : {1, -1}) {
                const ai::RoadSide& sd = roads[size_t(ri)].sideOf(s);
                if (!sd.open()) continue;
                for (int l = 0; l < sd.lanes; ++l) {
                    const auto opts = net.nextOptions(ri, s, l);
                    if (opts.empty() || opts[0].road < 0) ++none;
                    else ++choices[int(opts.size())];
                }
            }
        std::printf("  %s next-road options per open lane end: 1: %d, 2: %d, 3: %d, 4: %d; none %d\n", city, choices[1],
                    choices[2], choices[3], choices[4], none);
        if (L)
            expect(none == 0 && choices[1] == 826 && choices[2] == 281 && choices[3] == 34 && choices[4] == 0,
                   "London lane ends: 826 with one choice, 281 with two, 34 with three");
        else
            expect(none == 0 && choices[1] == 598 && choices[2] == 524 && choices[3] == 89 && choices[4] == 1,
                   "SF lane ends: 598, 524, 89 and one with four");
        if (!L) {
            const auto opts = net.nextOptions(115, -1, 0);
            bool to127 = false;
            for (const auto& o : opts)
                if (o.road == 127 && o.side == -1 && o.lane == 0 && o.rule == 5) to127 = true;
            expect(to127 && opts.size() == 1, "SF road 115 side -1 lane 0 continues to road 127 side -1 lane 0 (lane-0 rule)");
        }

        // Turn pieces join each lane end smoothly to the chosen lane (f = 2.2, o = 0).
        int gaps = 0, pieces = 0;
        for (int ri = 0; ri < int(roads.size()); ++ri)
            for (int s : {1, -1}) {
                const ai::RoadSide& sd = roads[size_t(ri)].sideOf(s);
                if (!sd.open() || roads[size_t(ri)].n < 3) continue;
                for (int l = 0; l < sd.lanes; ++l)
                    for (const auto& o : net.nextOptions(ri, s, l)) {
                        const float t = net.turnStart(ri, s, l, 2.2f);
                        const ai::Piece tp = net.turnPiece(ri, s, l, 0.0f, t, o);
                        const ai::PieceCurve c = ai::RoadNetwork::curveOf(tp);
                        const vec3 a = ai::RoadNetwork::curvePosition(c, tp.L);
                        const ai::Piece first = net.lanePiece(o.road, o.side, o.lane, 1, 0.0f, 2.2f);
                        ++pieces;
                        if (std::fabs(a.x - first.P0.x) + std::fabs(a.z - first.P0.z) > 0.01f) ++gaps;
                    }
            }
        expect(gaps == 0, strf("%s: %d turn pieces end where the next lane starts (%d gaps)", city, pieces, gaps));

        // Room lists and look-ups.
        const ai::Intersection& x0 = net.intersections()[L ? 10 : 2];
        const ai::Component c = net.componentAt(x0.centre + vec3(0, 0.5f, 0), 0);
        expect(c.kind == 3 && c.index == (L ? 10 : 2), strf("%s: the component at intersection %d's centre is it (%d, %d)", city,
                                                            L ? 10 : 2, c.index, c.kind));
        int roadHits = 0, roadTests = 0;
        for (int ri = 0; ri < NR; ri += 7) {
            const ai::Road& r = roads[size_t(ri)];
            if (r.n < 3 || r.side[1].lanes == 0) continue;  // LB = 0: never 1 (road-network.md)
            const vec3 p = r.C[1] + vec3(0, 0.5f, 0);
            ++roadTests;
            if (net.onRoad(ri, p, 0.0f) == 1) ++roadHits;
        }
        expect(roadHits == roadTests, strf("%s: section centres classify as on the roadway (%d of %d)", city, roadHits, roadTests));
        size_t edges = 0;
        for (const auto& l : net.graph()) edges += l.size();
        size_t listEntries = 0;
        for (const auto& x : net.intersections()) listEntries += x.roads.size();
        expect(edges == listEntries, strf("%s: routing graph has an edge per list entry (%zu)", city, edges));

        // Traffic population (traffic-population.md, "Checked against"): fresh starts in every
        // room with traffic roads at D = 0.5, pool 300.
        {
            phys::CollisionWorld world;
            world.setupCity(vfs, cd);
            ai::TrafficSystem ts;
            ts.create(vfs, assets, cd, world, 0.5f);
            double sumCars = 0, sumL = 0, maxL = 0;
            int rooms = 0;
            for (size_t r = 1; r < cd.rooms().size(); ++r) {
                if (net.ambientRoads(int(r)).empty()) continue;
                ++rooms;
                ts.resetAtRoom(int(r));
                sumCars += ts.carsOnStreets();
                sumL += ts.stats().lastLaneLength;
                maxL = std::max(maxL, double(ts.stats().lastLaneLength));
            }
            const double meanCars = sumCars / rooms, meanL = sumL / rooms / 1000.0;
            std::printf("  %s traffic: %d rooms with roads, entering lane length mean %.2f km (max %.2f), cars placed mean "
                        "%.1f at D 0.5\n",
                        city, rooms, meanL, maxL / 1000.0, meanCars);
            expect(rooms == (L ? 1187 : 1087) && std::lround(meanCars) == (L ? 59 : 131) &&
                       std::fabs(meanL - (L ? 4.7 : 10.6)) < 0.05 && std::fabs(maxL / 1000.0 - (L ? 16.7 : 35.8)) < 0.05,
                   strf("%s traffic population: rooms, lane lengths and cars placed as in traffic-population.md", city));
            // A minute with a parked player at the first start: no car leaves its rail by more than
            // 5 cm a frame beyond v·F (turning round excepted).
            ai::TrafficPlayer pl;
            pl.modelOrigin = cd.starts().empty() ? vec3() : cd.starts()[0].pos + vec3(0, 0.3f, 0);
            pl.bodyPos = pl.modelOrigin + vec3(0, 0.5f, 0);
            pl.radius = 2.5f;
            const std::vector<ai::TrafficPlayer> players{pl};
            ts.reset(players);
            std::vector<vec3> prev(ts.cars().size());
            std::vector<int> prevRoad(ts.cars().size(), -2), prevSide(ts.cars().size(), 0);
            float worst = 0;
            int bad = 0;
            const float F = float(1.0 / 60.0);
            for (int f = 0; f < 3600; ++f) {
                ts.update(F, players);
                ts.afterPhysics();
                for (const ai::TrafficCar& c : ts.cars()) {
                    if (!c.onStreets) {
                        prevRoad[size_t(c.index)] = -2;
                        continue;
                    }
                    if (!std::isfinite(c.pos.x + c.pos.y + c.pos.z)) ++bad;
                    if (prevRoad[size_t(c.index)] != -2 && !(prevRoad[size_t(c.index)] == c.road && prevSide[size_t(c.index)] != c.side) &&
                        c.state == ai::TrafficDriving) {
                        const vec3 d = c.pos - prev[size_t(c.index)];
                        worst = std::max(worst, std::sqrt(d.x * d.x + d.z * d.z) - c.v * F);
                    }
                    prev[size_t(c.index)] = c.pos;
                    prevRoad[size_t(c.index)] = c.road;
                    prevSide[size_t(c.index)] = c.side;
                }
            }
            expect(bad == 0 && worst < 0.05f, strf("%s traffic: 60 s with %d cars, largest step beyond v*F %.3f m", city,
                                                   ts.carsOnStreets(), double(worst)));
        }

        // The light cycle (frame time 1/64 s): every phase lasts until t > 6 s, i.e. 385 frames;
        // a kind 1 set with k lights repeats after k + 1 phases, a kind 0 set after k.
        {
            ai::RoadNetwork& mnet = *cd.network();
            ai::TrafficControl& mtc = mnet.control();
            mtc.reset();
            int k1 = -1, k0 = -1;
            for (size_t i = 0; i < mtc.sets().size(); ++i) {
                if (k1 < 0 && mtc.sets()[i].kind == 1 && mtc.sets()[i].lights.size() == 3) k1 = int(i);
                if (k0 < 0 && mtc.sets()[i].kind == 0 && mtc.sets()[i].lights.size() == 2) k0 = int(i);
            }
            std::vector<std::vector<int>> trace1, trace0;
            const float dt = 1.0f / 64.0f;
            for (int f = 0; f < 385 * 10; ++f) {
                mtc.update(dt);
                std::vector<int> a, b;
                if (k1 >= 0)
                    for (const auto& l : mtc.sets()[size_t(k1)].lights) a.push_back(l.state);
                if (k0 >= 0)
                    for (const auto& l : mtc.sets()[size_t(k0)].lights) b.push_back(l.state);
                trace1.push_back(a);
                trace0.push_back(b);
            }
            bool per1 = k1 >= 0, per0 = k0 >= 0;
            for (size_t i = 0; i + 4 * 385 < trace1.size(); ++i) per1 = per1 && trace1[i] == trace1[i + 4 * 385];
            for (size_t i = 0; i + 2 * 385 < trace0.size(); ++i) per0 = per0 && trace0[i] == trace0[i + 2 * 385];
            // Frame f is after f + 1 updates (t = (f + 1)/64 within the phase).
            const std::vector<int> g0{3, 1, 1}, y0{2, 1, 1}, g1{1, 3, 1}, w{4, 4, 4}, we{5, 5, 5};
            const bool start = k1 >= 0 && trace1[191] == g0 && trace1[192] == y0 && trace1[383] == y0 && trace1[384] == g1 &&
                               trace1[3 * 385 - 1] == w && trace1[3 * 385 + 191] == w && trace1[3 * 385 + 192] == we && trace1[4 * 385 - 1] == g0;
            expect(per1 && per0 && start, strf("%s: light cycle (3 s green, 3 s yellow per light, walk 3 s + 3 s, then again)",
                                               city));
            mtc.reset();
        }
    }
}

// The police specs' "Checked against" (police.md, route-search.md, driver-planner.md) and a short
// chase on the real city data.
void policeChecks(const Vfs& vfs, const Expect& expect) {
    for (const char* city : {"london", "sf"}) {
        const bool L = std::string(city) == "london";
        CityData cd;
        AssetCache assets(vfs);
        std::string msg;
        if (!cd.load(city, vfs, assets, msg) || !cd.network()) {
            expect(false, strf("%s loads with its AI network", city));
            continue;
        }
        ai::RoadNetwork& net = *cd.network();
        // Bends (driver-planner.md §4.1): SF 9, London 55, all two-section.
        int bends = 0, single = 0;
        for (int ri = 0; ri < net.mainRoadCount(); ++ri)
            for (const ai::Bend& b : net.roads()[size_t(ri)].bends) {
                ++bends;
                if (b.K.x == 0.0f && b.K.z == 0.0f) ++single;
            }
        expect(bends == (L ? 55 : 9), strf("%s: %d bends on the main roads (%d)", city, bends, L ? 55 : 9));
        // The police list.
        const auto& pol = net.raceAimap().police;
        bool fields = true;
        for (const PoliceEntry& e : pol)
            if (e.model != "vpcop" || e.i0 != 0 || e.i1 != 15 || e.f0 != 0.5f || e.f1 != 50.0f) fields = false;
        expect(pol.size() == size_t(L ? 20 : 19) && fields, strf("%s: %zu Cruise cops, all vpcop 0 15 0.5 50", city, pol.size()));
        if (L && pol.size() > 13)
            expect(std::fabs(pol[13].heading - -9.3327780f) < 1e-5f, strf("London spot 13 heading %.7f", double(pol[13].heading)));
        if (!L) {
            std::string got;
            for (int k = 0; k <= 10; ++k) got += strf("%d ", int(float(k) * 0.1f * float(pol.size())));
            expect(got == "0 1 3 5 7 9 11 13 15 17 19 ", "SF cops at density 0 … 1: " + got);
        }
        // Route searches.
        auto search = [&](const vec3& S, const vec3& G, float& cost) {
            ai::RouteQuery q;
            q.start = S;
            q.goal = G;
            q.startZ = vec3(0, 0, 1);
            std::vector<int> route;
            cost = 0;
            ai::routeSearch(net, q, route, &cost);
            std::string out;
            for (int i : route) out += strf("%d ", i);
            return out;
        };
        float cost = 0;
        if (!L && pol.size() >= 3) {
            const std::string a = search(pol[0].pos, pol[1].pos, cost);
            expect(a == "40 50 " && std::fabs(cost - 230.397f) < 0.01f, strf("SF cop 0 -> cop 1: %s(cost %.3f)", a.c_str(), double(cost)));
            const std::string b = search(pol[2].pos, pol[0].pos, cost);
            expect(b == "183 168 174 41 40 ", strf("SF cop 2 -> cop 0: %s", b.c_str()));
        }
        if (L && pol.size() >= 2) {
            const std::string a = search(pol[0].pos, pol[1].pos, cost);
            expect(a == "29 249 280 138 140 ", strf("London cop 0 -> cop 1: %s", a.c_str()));
            const auto& ix = net.intersections();
            const std::string b = search(ix[198].centre, ix[199].centre, cost);
            expect(b == "198 199 " && std::fabs(cost - 31.081f) < 0.01f,
                   strf("London 198 -> 199: %s(cost %.3f, through the last of three parallel roads)", b.c_str(), double(cost)));
            const ai::Road& r5 = net.roads()[5];
            const std::string c = search(ix[7].centre, r5.C[size_t(r5.n / 2)], cost);
            expect(c.empty() && (r5.end[0].intersection == 7 || r5.end[1].intersection == 7),
                   strf("London 7 -> a point on road 5: no route (%s)", c.c_str()));
        }
        // Parking: 13 of SF's 19 and 13 of London's 20 spots are off the network (RECOVER).
        phys::CollisionWorld world;
        world.setupCity(vfs, cd);
        ai::PoliceSystem ps;
        ps.create(vfs, assets, cd, net, world, 1.0f, "vpbug", nullptr);
        ps.reset();
        expect(!ps.cops().empty() && ps.cops()[0].paint == (L ? 1 : 0),
               strf("%s: vpcop livery %d (cable cars: SF paint 0, London paint 1)", city, ps.cops().empty() ? -1 : ps.cops()[0].paint));
        int recover = 0, drive = 0;
        for (const ai::Cop& c : ps.cops()) {
            if (c.driver.mode() == ai::DriverMode::Recover) ++recover;
            if (c.driver.mode() == ai::DriverMode::Drive) ++drive;
        }
        // The basic look-up gives RECOVER exactly where the spot's room has no road or
        // intersection entry and no shortcut it is on (police.md, "Parking"; Q50).
        int offNet = 0;
        for (const ai::Cop& c : ps.cops()) {
            const int room = cd.findRoom(c.car->body().p, 0);
            bool on = false;
            for (const auto& cc : net.roomComponents(room))
                if (cc.kind == 1 || cc.kind == 3 || (cc.kind == 2 && net.onRoad(cc.index, c.car->body().p, 0.0f) < 3)) on = true;
            if (!on) ++offNet;
        }
        expect(int(ps.cops().size()) == int(pol.size()) && recover == offNet && recover == (L ? 15 : 11) && drive == (L ? 5 : 8),
               strf("%s: parked cops %zu: %d in RECOVER (off the network), %d in DRIVE (police.md: London 15/5, SF 11/8)", city,
                    ps.cops().size(), recover, drive));
        // A chase: a parked player car 30 m in front of the first cop that parks on a road.
        int ci = -1;
        for (const ai::Cop& c : ps.cops())
            if (c.driver.mode() == ai::DriverMode::Drive) {
                ci = c.index;
                break;
            }
        if (ci < 0) continue;
        phys::CarTune tune = phys::loadCarTune(vfs, "vpbug", true);
        phys::Car player;
        player.setup(tune);
        const ai::Cop& cop0 = ps.cops()[size_t(ci)];
        const vec3 fwd = cop0.car->forward();
        vec3 at = cop0.car->body().p + vec3(fwd.x * 30.0f, 0, fwd.z * 30.0f);
        phys::ProbeHit hit;
        if (world.probeStatic(at + vec3(0, 6, 0), at - vec3(0, 20, 0), hit)) at.y = hit.point.y + 1.0f;
        player.placeAt(at, cop0.entry.heading);
        std::vector<ai::PolicePlayer> players(1);
        players[0].car = &player;
        players[0].boxMin = tune.bound ? tune.bound->bmin : vec3(-1, 0, -2);
        players[0].boxMax = tune.bound ? tune.bound->bmax : vec3(1, 1.5f, 2);
        phys::Scene scene;
        phys::SceneHooks hooks;
        hooks.findRoom = [&](const vec3& p, int h) { return cd.findRoom(p, h); };
        const float F = 1.0f / 60.0f;
        const int n = phys::physicsStepsForFrame(F);
        const float dt = F / float(n), invdt = 1.0f / dt;
        float d0 = 0, dmin = 1e9f;
        bool chased = false, lights = false;
        auto distance = [&]() {
            const vec3 a = ps.cops()[size_t(ci)].car->body().p, b = player.body().p;
            return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
        };
        d0 = distance();
        phys::CarInputs hold;
        hold.brake = 1.0f;
        auto frame = [&](double t) {
            players[0].room = player.collision().room;
            player.aiControlStep(hold);
            ps.update(F, players, nullptr, t);
            std::vector<phys::SceneMover> movers;
            phys::SceneMover pm;
            pm.kind = phys::SceneMover::CarMover;
            pm.car = &player;
            pm.player = true;
            pm.number = 1;
            pm.identity = 10001;
            movers.push_back(pm);
            ps.appendMovers(movers);
            movers.reserve(movers.size() + 8);
            for (int k = 0; k < n; ++k) scene.runSample(movers, world, dt, invdt, hooks);
            ps.afterPhysics(movers, F);
        };
        for (int f = 0; f < 60 * 20; ++f) {
            frame(f * F);
            const ai::Cop& c = ps.cops()[size_t(ci)];
            if (c.state == ai::CopState::Follow || c.state == ai::CopState::Apprehend) chased = true;
            if (c.lights.on) lights = true;
            dmin = std::min(dmin, distance());
        }
        const ai::Cop& c = ps.cops()[size_t(ci)];
        std::printf("  %s chase: cop %d started %.1f m away, closest %.1f m, now %.1f m, state %d, route %zu entries\n", city, ci,
                    double(d0), double(dmin), double(distance()), int(c.state), c.route.size());
        expect(chased && lights && dmin < 12.0f, strf("%s: a parked player 30 m ahead is chased (light bar on) and reached", city));
        // Give up beyond 250 m.
        const vec3 far = player.body().p + vec3(0, 50.0f, 0) + vec3(fwd.x * 400.0f, 0, fwd.z * 400.0f);
        player.placeAt(far, 0.0f);
        for (int f = 0; f < 3; ++f) frame((60 * 20 + f) * F);
        expect(ps.cops()[size_t(ci)].state == ai::CopState::Idle && ps.cops()[size_t(ci)].driver.mode() == ai::DriverMode::Hold &&
                   ps.roster().nCars == 0,
               strf("%s: the cop gives up beyond 250 m (IDLE, HOLD, roster empty)", city));
        (void)single;
    }
}

// Street props, path-set props and decals (street-props.md, pathset.md, "Checked against").
void streetPropChecks(const Vfs& vfs, const Expect& expect) {
    for (const char* city : {"london", "sf"}) {
        const bool L = std::string(city) == "london";
        CityData cd;
        AssetCache assets(vfs);
        std::string msg;
        if (!cd.load(city, vfs, assets, msg)) {
            expect(false, strf("%s loads", city));
            continue;
        }
        const StreetPropStats& st = cd.streetPropStats();
        std::set<int> rooms;
        for (const CityObject& o : cd.objects())
            if (o.origin == 1) rooms.insert(o.room);
        std::string types;
        for (const auto& kv : st.byType) types += strf(" %s %d", kv.first.c_str(), kv.second);
        std::printf("  %s street props:%s\n", city, types.c_str());
        expect(st.roads == (L ? 540 : 379) && st.roadsWithRule == (L ? 404 : 331) && st.roadsWithElement == (L ? 374 : 331),
               strf("%s: %d AI roads, %d with a rule byte on the first room, %d with a road element there", city, st.roads,
                    st.roadsWithRule, st.roadsWithElement));
        expect(st.streetProps == (L ? 4835 : 5349) && int(rooms.size()) == (L ? 379 : 409),
               strf("%s: %d street props in %zu rooms (spec: %d in %d)", city, st.streetProps, rooms.size(), L ? 4835 : 5349,
                    L ? 379 : 409));
        auto bt = [&](const char* t) {
            auto it = st.byType.find(t);
            return it == st.byType.end() ? 0 : it->second;
        };
        if (L)
            expect(bt("meter") == 1057 && bt("streetree") == 591 && bt("streetlight") == 505 && bt("streetlightb") == 458 &&
                       bt("trashcan") == 372 && bt("phone") == 249 && bt("bench") == 135,
                   "London street props by type (meter 1057, streetree 591, streetlight 505, …)");
        else
            expect(bt("meter") == 1295 && bt("streetlight") == 1159 && bt("streetlightb") == 1069 && bt("trashcan") == 319 &&
                       bt("streetree") == 295 && bt("busstop") == 222 && bt("newsboxs") == 116,
                   "SF street props by type (meter 1295, streetlight 1159, streetlightb 1069, …)");
        expect(st.pathsetPlaced == (L ? 1033 : 894) && st.pathsetProps == (L ? 1033 : 778) && st.pathsetFull == (L ? 383 : 305),
               strf("%s: props.pathset %d placed, %d created (%d full matrix)", city, st.pathsetPlaced, st.pathsetProps,
                    st.pathsetFull));
        expect(st.decals == (L ? 79 : 48), strf("%s: %d decals", city, st.decals));
        int room0 = 0;
        for (const CityObject& o : cd.objects())
            if (o.origin == 2 && o.room == 0) ++room0;
        for (const CityDecal& d : cd.decals())
            if (d.room == 0) ++room0;
        expect(room0 == 0, strf("%s: every path-set prop and decal lies in a room (%d in room 0)", city, room0));
        if (L) {
            // Road 12's street light, and path 0's first tree.
            bool lamp = false, tree = false, near = false;
            for (const CityObject& o : cd.objects()) {
                const vec3 P = o.world.translation();
                if (o.origin == 1 && o.name == "sp_lightstreet_l" && P.x == -217.161041f && P.y == 0.0149000138f &&
                    P.z == 328.375488f && o.placement.m[0] == -0.174760833f && o.placement.m[1] == 0.0499336943f &&
                    o.placement.m[2] == -0.983343959f)
                    lamp = true;
                if (o.origin == 2 && o.name == "sp_tree1_s" && std::fabs(P.x - 346.2885f) < 0.001f && std::fabs(P.y - 4.8504f) < 0.001f &&
                    std::fabs(P.z + 930.6403f) < 0.001f && o.room == 886)
                    tree = true;
                const vec3 g = o.drawWorld.translation();
                if (o.origin == 1 && o.name == "sp_tree1_s" && std::fabs(g.x + 237.477f) < 0.001f && std::fabs(g.y - 3.515f) < 0.001f &&
                    std::fabs(g.z - 334.889f) < 0.001f)
                    near = true;
            }
            expect(lamp, "London road 12: the street light at (-217.161041, 0.0149000138, 328.375488), X (-0.174760833, ...)");
            expect(tree, "London props.pathset path 0: first tree at (346.2885, 4.8504, -930.6403) in room 886");
            expect(near, "London: sp_tree1_s of road 80 at (-237.477, 3.515, 334.889) near the free-roam start");
        }
    }
}

// The city list (city-files.md, "City list"; viewer.md test layouts).
void cityListChecks(const Vfs& vfs, const Expect& expect) {
    const std::vector<CityEntry> list = buildCityList(vfs);
    std::string got;
    int tests = 0;
    for (const CityEntry& e : list) {
        got += strf("%s%s(%s)", got.empty() ? "" : ", ", e.localizedName.c_str(), e.mapName.c_str());
        if (e.testLayout) ++tests;
    }
    std::printf("  cities: %s\n", got.c_str());
    expect(list.size() >= 2 && list[0].localizedName == "San Francisco" && list[0].mapName == "sf" && list[0].raceDir == "sf" &&
               list[1].localizedName == "London" && list[1].mapName == "london" && !list[0].testLayout && !list[1].testLayout &&
               list[0].blitz == 10 && list[0].circuit == 10 && list[0].checkpoint == 12,
           "city list: San Francisco, London from tune/*.cinfo (10/10/12 races), then test layouts");
    expect(tests == int(list.size()) - 2, strf("city list: %d unused city/*.psdl layouts listed as tests", tests));
}

// Loose bodies (loose-bodies.md): the car's loose parts, the slot ring and the body pool, a prop
// breaking into pieces that fall and come to rest.
void looseChecks(const Vfs& vfs, const Expect& expect) {
    using namespace phys;
    const CarTune cab = loadCarTune(vfs, "vpcab", true);
    std::string names;
    for (const LoosePartTune& lp : cab.looseParts) names += (names.empty() ? "" : " ") + lp.name + (lp.gear ? "*" : "");
    std::printf("  vpcab loose parts: %s\n", names.c_str());
    expect(names == "BREAK2 BREAK01 BREAK23 WHL0* WHL1* WHL2* WHL3*",
           "vpcab: body parts BREAK2, BREAK01, BREAK23, then the wheels (parts with an H mesh)");
    for (const LoosePartTune& lp : cab.looseParts)
        if (lp.name == "BREAK01")
            expect(std::fabs(lp.data->bd.mass - 47.21487f) < 1e-4f && std::fabs(lp.data->bd.size.x - 1.59981f) < 1e-5f &&
                       lp.data->radius > 0.5f && lp.data->box,
                   strf("vpcab BREAK01: prop data vpcab_BREAK01 (47.2 kg, 1.60 x 0.10 x 0.37 box), radius %.3f", lp.data->radius));
    {
        RigidBody rb;
        BangerData bd;
        bd.mass = 12.637488f;
        bd.size = vec3(0.180607f, 3.228001f, 0.180607f);
        setupLooseMass(rb, bd);
        expect(rb.I.y == rb.I.x / 40.0f && rb.I.x == rb.I.z, "a pole's smallest moment is raised to the largest / 40");
    }
    // The ring of 40 slots and the pool of 32 bodies (flat world: room 0, the parts never move).
    {
        CollisionWorld flat;
        flat.setupFlat(vfs, 0.0f);
        Car car;
        car.setup(cab);
        car.resetFlat(0.0f, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < 43; ++i) flat.throwCarPart(car, 0, vec3(0, 1, 0), vec3());
        const auto ev = flat.takeLooseEvents();
        std::vector<int> ids;
        for (const LooseEvent& e : ev) ids.push_back(e.id - CollisionWorld::kSlotIdBase);
        bool ring = ids.size() == 43;
        for (int i = 0; i < 40 && ring; ++i) ring = ids[size_t(i)] == i;
        ring = ring && ids[40] == 0 && ids[41] == 0 && ids[42] == 1;
        expect(ring, "moved-prop slots: 0 … 39, then 0 twice, then 1 (loose-bodies.md 1)");
        expect(flat.bodiesInUse() == 32, strf("body pool: %d of 32 in use after 43 throws", flat.bodiesInUse()));
        std::vector<SceneMover> mv;
        flat.declareLoose(mv, 0);
        expect(mv.empty() && flat.bodiesInUse() == 0, "flat world: thrown parts lie in room 0 and are dropped (5.1)");
    }
    // A London prop with pieces broken by a push along +x: its pieces fall and come to rest.
    {
        CityData cd;
        AssetCache assets(vfs);
        std::string msg;
        if (!cd.load("london", vfs, assets, msg)) {
            expect(false, "london loads");
            return;
        }
        CollisionWorld cw;
        cw.setupCity(vfs, cd, false, false);
        int oi = -1;
        for (size_t i = 0; i < cw.objects().size(); ++i) {
            const CollisionObject& o = cw.objects()[i];
            if (o.kind == ObjKind::Prop && o.loose && o.loose->bd.numParts >= 2 && o.cityObject >= 0 &&
                cd.objects()[size_t(o.cityObject)].room > 0) {
                oi = int(i);
                break;
            }
        }
        if (oi < 0) {
            expect(false, "London has a prop with pieces");
            return;
        }
        const CityObject& co = cd.objects()[size_t(cw.objects()[size_t(oi)].cityObject)];
        const int N = cw.objects()[size_t(oi)].loose->bd.numParts;
        std::vector<SceneMover> mv;
        cw.setLooseMovers(&mv);
        // The prop emitters (effects.md, "Props"): activated at every body attach, ended when the
        // body is let go.
        std::vector<int> act, inact;
        cw.onLooseActive = [&](int key, const std::string&, const Mtx&, bool) { act.push_back(key); };
        cw.onLooseInactive = [&](int key) { inact.push_back(key); };
        const int e = cw.propTestBody(oi);
        RigidBody& rb = cw.looseBody(e).rb;
        const float m = rb.m;
        rb.G = vec3(3.0f * m, 1.0f * m, 0.0f);  // a hit worth 3 m/s sideways and 1 m/s up
        cw.finishPropTest(oi, e, true, vec3(1, 0, 0));
        cw.setLooseMovers(nullptr);
        expect(int(act.size()) == 1 + N && act[0] == e && inact.size() == 1 && inact[0] == e,
               strf("prop bursts: the test body and each piece activate (%zu), the prop's body ends (%zu)", act.size(),
                    inact.size()));
        cw.onLooseActive = nullptr;
        cw.onLooseInactive = nullptr;
        const auto ev = cw.takeLooseEvents();
        expect(ev.size() == 1 && ev[0].kind == LooseEvent::Break && ev[0].n == N && int(ev[0].ids.size()) == N &&
                   int(mv.size()) == N && cw.object(oi).broken,
               strf("%s (room %d) breaks into its %d pieces, each a mover", co.name.c_str(), co.room, N));
        bool share = true;
        for (const SceneMover& sm : mv) {
            const RigidBody& p = cw.looseBody(sm.loose).rb;
            share = share && std::fabs(p.G.x - 3.0f * p.m) < 1e-3f * p.m && std::fabs(p.G.y - p.m) < 1e-3f * p.m;
        }
        expect(share, "each piece gets the velocity the hit gave the whole prop (G = Δv·m_i)");
        // Frames of two samples until every piece is let go.
        Scene scene;
        SceneHooks hooks;
        int frames = 0, rest = 0;
        float lowest = 1e9f, highest = -1e9f;
        const float groundY = co.drawWorld.translation().y;
        for (; frames < 1200 && rest < N; ++frames) {
            std::vector<SceneMover> movers;
            cw.declareLoose(movers, co.room);
            for (int k = 0; k < 2; ++k) scene.runSample(movers, cw, 0.008333334f, 120.0f, hooks);
            cw.afterLooseSamples(movers);
            for (const LooseEvent& x : cw.takeLooseEvents())
                if (x.kind == LooseEvent::Rest || x.kind == LooseEvent::Gone) {
                    ++rest;
                    const MovedSlot& S = cw.movedSlot(x.id - CollisionWorld::kSlotIdBase);
                    lowest = std::min(lowest, S.M.o.y);
                    highest = std::max(highest, S.M.o.y);
                }
        }
        std::printf("  %s: %d pieces at rest after %d frames, heights %.3f … %.3f (prop CG at %.3f)\n", co.name.c_str(), rest,
                    frames, double(lowest), double(highest), double(groundY));
        expect(rest == N && frames < 1200 && lowest > groundY - 5.0f, "the pieces fall, settle on the city and are let go");
        cw.resetProps();
        expect(!cw.object(oi).broken && cw.bodiesInUse() == 0 && !cw.movedSlot(0).held, "a reset brings the prop back");
    }
}

// Moving level objects (level-objects.md, "Checked against").
void levelObjectChecks(const Vfs& vfs, const Expect& expect) {
    for (const char* city : {"london", "sf"}) {
        const bool L = std::string(city) == "london";
        CityData cd;
        AssetCache assets(vfs);
        std::string msg;
        if (!cd.load(city, vfs, assets, msg)) {
            expect(false, strf("%s loads", city));
            continue;
        }
        const LevelObjects& lo = cd.levelObjects();
        const auto& st = lo.stats();
        std::printf("  %s: %d boats, %d leaves, %d trains, %d ferries; generator 0x%08x at the first draw\n", city, st.boats,
                    st.leaves, st.trains, st.ferries, cd.gizmoRandomStart());
        bool seen[4] = {};
        for (const LevelObject& o : lo.objects()) {
            if (seen[o.kind]) continue;
            seen[o.kind] = true;
            static const char* const kinds[] = {"boat", "leaf", "train car", "ferry"};
            std::printf("    first %s: %s at (%.1f, %.1f, %.1f) room %d\n", kinds[o.kind], o.model.c_str(), double(o.o.x),
                        double(o.o.y), double(o.o.z), o.room);
        }
        expect(cd.gizmoRandomStart() == (L ? 0x3a2d7dc1u : 0xef3c16d1u),
               strf("%s: generator state at the first level-object draw (street props, then 192 draws)", city));
        int draws = 0;
        for (Lcg g(cd.gizmoRandomStart()); g.state() != cd.gizmoRandomEnd() && draws < 100000; g.next15()) ++draws;
        expect(draws == (L ? 1264 : 281), strf("%s: %d level-object draws (%d)", city, draws, L ? 1264 : 281));
        expect(st.boats == 16 && st.leaves == (L ? 7 : 2) && st.trains == (L ? 8 : 0) && st.ferries == (L ? 6 : 1),
               strf("%s: 16 boats, %d leaves, %d trains, %d ferries", city, L ? 7 : 2, L ? 8 : 0, L ? 6 : 1));
        const auto& pk = cd.parkedStats();
        expect(L ? (pk.slots == 730 && pk.empty == 234 && pk.pcar01 == 258 && pk.pcar02 == 238)
                 : (pk.slots == 138 && pk.empty == 28 && pk.pcar01 == 62 && pk.pcar02 == 48),
               strf("%s parked cars: %d slots, %d empty, %d giz_pcar01_l, %d giz_pcar02_l", city, pk.slots, pk.empty, pk.pcar01,
                    pk.pcar02));
        // The first parked car.
        for (size_t i = 0; i < cd.objects().size(); ++i) {
            const CityObject& o = cd.objects()[i];
            if (o.origin != 3) continue;
            const vec3 P = o.placement.translation(), S = o.drawWorld.translation();
            auto near = [](float a, float b) { return std::fabs(a - b) < 0.0015f; };
            if (L)
                expect(o.name == "giz_pcar02_l" && o.set == 8 && near(P.x, -1359.612f) && near(P.y, 10.040f) && near(P.z, -612.549f) &&
                           near(o.placement.m[0], -0.72682f) && near(o.placement.m[1], 0.02803f) && near(o.placement.m[2], 0.68626f) &&
                           near(o.placement.m[8], -0.68626f) && near(o.placement.m[10], -0.72682f) && near(S.x, -1359.597f) &&
                           near(S.y, 10.794f) && near(S.z, -612.564f) && o.room == 427,
                       strf("London first parked car: %s paint %d at (%.3f, %.3f, %.3f), stored (%.3f, %.3f, %.3f), room %d",
                            o.name.c_str(), o.set, P.x, P.y, P.z, S.x, S.y, S.z, o.room));
            else
                expect(o.name == "giz_pcar01_l" && o.set == 5 && near(P.x, -137.185f) && near(P.y, 13.044f) && near(P.z, 450.446f) &&
                           o.room == 95,
                       strf("SF first parked car: %s paint %d at (%.3f, %.3f, %.3f), room %d", o.name.c_str(), o.set, P.x, P.y, P.z,
                            o.room));
            break;
        }
        // Drawbridge rooms and the leaves.
        std::vector<int> br = lo.bridgeRooms();
        std::sort(br.begin(), br.end());
        std::string rs;
        for (int r : br) rs += strf(" %d", r);
        expect(L ? br == std::vector<int>({333, 857, 939, 970, 971}) : br == std::vector<int>({34, 35}),
               strf("%s: drawbridge rooms%s", city, rs.c_str()));
        // AI obstacles (level-objects.md, "AI obstacles"): the moving level objects registered by
        // the road-network load.
        if (const ai::RoadNetwork* net = cd.network()) {
            std::set<int> objsSeen;
            int onRoads = 0, atIx = 0, leaves = 0, ferries = 0;
            std::string where;
            auto note = [&](int id) {
                if (id < ai::kLevelObstacleBase) return false;
                const LevelObject& l = lo.objects()[size_t(id - ai::kLevelObstacleBase)];
                objsSeen.insert(id);
                if (l.kind == LevelObject::Leaf) ++leaves;
                if (l.kind == LevelObject::Ferry) ++ferries;
                return true;
            };
            for (const ai::Road& r : net->roads())
                for (int sd = 0; sd < 2; ++sd)
                    for (size_t k = 0; k < r.obstacles[sd].size(); ++k)
                        for (int id : r.obstacles[sd][k])
                            if (note(id)) {
                                ++onRoads;
                                if (!L) where += strf(" road %d section %zu side %s", r.index, k, sd ? "+1" : "-1");
                            }
            std::string ixs;
            for (const ai::Intersection& x : net->intersections())
                for (int id : x.obstacles)
                    if (note(id)) {
                        ++atIx;
                        ixs += strf(" %d", x.index);
                    }
            const int n = int(objsSeen.size());
            if (L)
                expect(n == 17 && onRoads == 21 && atIx == 2 && ixs == " 59 59" && leaves == 0 && ferries == 0,
                       strf("London AI obstacles: %d train cars, %d registrations on road sections, %d at intersections%s; "
                            "%d leaves, %d ferries",
                            n, onRoads, atIx, ixs.c_str(), leaves, ferries));
            else
                expect(n == 2 && onRoads == 2 && atIx == 0 && leaves == 2 &&
                           where == " road 33 section 1 side -1 road 33 section 1 side +1",
                       strf("SF AI obstacles: %d objects (%d leaves), %d registrations:%s; %d at intersections", n, leaves,
                            onRoads, where.c_str(), atIx));
        }
        if (!L) continue;
        {
            const LevelObject& l0 = lo.leafObject(0);
            auto near = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
            const float h = l0.bd.size.z * 0.5f;
            const float freeY = l0.o.y - h * l0.Z.y;
            expect(near(l0.o.x, 1242.95f) && near(l0.o.y, 7.70f) && near(l0.o.z, -134.11f) && near(freeY, 7.70f),
                   strf("Tower Bridge leaf 0: centre (%.2f, %.2f, %.2f), free end %.2f m", l0.o.x, l0.o.y, l0.o.z, freeY));
        }
        // The bridge cycle and the trains at F = 1/60.
        {
            CityData& m = cd;
            LevelObjects& lv = m.levelObjects();
            lv.reset(m);
            const float F = 1.0f / 60.0f;
            int prev = lv.leafState(0);
            std::vector<int> changes;
            float openFree = 0;
            const int T = lv.trainCount() > 0 ? 0 : -1;
            std::vector<std::string> stops;
            int tPrev = T >= 0 ? lv.trainState(T) : 0;
            for (int f = 1; f <= 6000; ++f) {
                lv.update(F, m, {});
                const int now = lv.leafState(0);
                if (now != prev) changes.push_back(f);
                prev = now;
                if (now == LevelObjects::Open) {
                    const LevelObject& l0 = lv.leafObject(0);
                    openFree = l0.o.y - l0.bd.size.z * 0.5f * l0.Z.y;
                }
                if (T >= 0) {
                    const int ts = lv.trainState(T);
                    if (ts == LevelObjects::Waiting && tPrev == LevelObjects::Braking && stops.size() < 3)
                        stops.push_back(strf("%.2f s seg %d", double(float(f - 1) * F), lv.trainCarSegment(T, lv.trainForward(T) ? 2 : 0)));
                    tPrev = ts;
                }
            }
            std::string cs;
            for (int c : changes) cs += strf(" %d", c);
            std::printf("  London bridge leaf 0 changes on frames%s; open free end %.2f m\n", cs.c_str(), double(openFree));
            expect(changes.size() >= 5 && changes[0] == 600 && changes[1] == 1166 && changes[2] == 1766 && changes[3] == 2332 &&
                       changes[4] == 2932 && std::fabs(openFree - 21.32f) < 0.01f,
                   "London bridges: open on frame 600, open 1166, close 1766, closed 2332, again 2932; free end 21.32 m open");
            std::string ss;
            for (const std::string& x : stops) ss += " " + x;
            expect(ss == " 27.37 s seg 6 52.70 s seg 1 78.02 s seg 6",
                   strf("London train 0 (%d points) stops:%s (27.37 s in segment 6, 52.70 s back in 1, 78.02 s)",
                        T >= 0 ? lv.trainPoints(T) : 0, ss.c_str()));
        }
    }
}

// SF's cable cars (cable-cars.md, "Checked against").
void cableCarChecks(const Vfs& vfs, const Expect& expect) {
    CityData cd;
    AssetCache assets(vfs);
    std::string msg;
    if (!cd.load("sf", vfs, assets, msg) || !cd.network()) {
        expect(false, "sf loads with its network");
        return;
    }
    phys::CollisionWorld world;
    world.setupCity(vfs, cd);
    ai::TrafficSystem ts;
    ts.create(vfs, assets, cd, world, 0.0f);
    ts.reset({});
    const auto& cars = ts.cableCars();
    std::string got;
    for (const ai::CableCar& cc : cars) {
        const ai::TrafficCar& c = ts.cars()[size_t(cc.car)];
        got += strf(" (%.1f, %.2f, %.1f) road %d %c partner %d;", double(c.pos.x), double(c.pos.y), double(c.pos.z), cc.startRoad,
                    cc.startSide > 0 ? 'B' : 'A', cc.partner);
    }
    std::printf("  SF cable cars:%s\n", got.c_str());
    bool starts = cars.size() == 4;
    const int roads[4] = {238, 134, 161, 178};
    const int sides[4] = {1, -1, -1, -1};
    for (size_t i = 0; i < cars.size() && i < 4; ++i)
        starts = starts && cars[i].startRoad == roads[i] && cars[i].startSide == sides[i] && cars[i].partner == int(i);
    expect(starts, "SF: 4 cable cars on roads 238 B, 134 A, 161 A, 178 A, each its own partner");
    if (!starts) return;
    {
        const ai::TrafficType& t = ts.typeOf(ts.cars()[size_t(cars[0].car)]);
        const auto& C0 = cd.network()->roads()[238].side[1].cableLine[0];
        expect(std::fabs(t.front - 4.3551f) < 1e-3f && std::fabs(t.back - 3.9492f) < 1e-3f && std::fabs(t.left - 1.1470f) < 1e-3f &&
                   std::fabs(t.right - 1.1274f) < 1e-3f && std::fabs(ts.cableData()->radius - 5.597f) < 1e-3f &&
                   std::fabs(C0.x + 996.4f) < 0.05f && std::fabs(C0.y - 21.85f) < 0.01f && std::fabs(C0.z + 503.7f) < 0.05f,
               strf("va_cablecar_f extents %.4f/%.4f/%.4f/%.4f, BODY radius %.3f; car 0 starts at (-996.4, 21.85, -503.7)", t.front,
                    t.back, t.left, t.right, ts.cableData()->radius));
    }
    // Running free (no stops, a_c = 2.5, F = 1/60): the route and the time back on the start road.
    ts.setCableFreeRun(true);
    const float F = 1.0f / 60.0f;
    std::vector<std::string> route0;
    float back[4] = {-1, -1, -1, -1};
    int lastRoad[4], lastSide[4];
    bool left[4] = {};
    for (size_t i = 0; i < 4; ++i) {
        lastRoad[i] = cars[i].startRoad;
        lastSide[i] = cars[i].startSide;
    }
    route0.push_back("238 B");
    for (int f = 1; f <= 60 * 240; ++f) {
        ts.update(F, {});
        for (size_t i = 0; i < 4; ++i) {
            const ai::TrafficCar& c = ts.cars()[size_t(cars[i].car)];
            if (c.road != lastRoad[i] || c.side != lastSide[i]) {
                lastRoad[i] = c.road;
                lastSide[i] = c.side;
                if (i == 0 && route0.size() < 7) route0.push_back(strf("%d %c", c.road, c.side > 0 ? 'B' : 'A'));
                if (c.road == cars[i].startRoad && c.side == cars[i].startSide && left[i] && back[i] < 0) back[i] = float(f) * F;
                left[i] = true;
            }
        }
    }
    std::string r0;
    for (const std::string& x : route0) r0 += (r0.empty() ? "" : ", ") + x;
    std::printf("  cable car 0 route: %s; back on the start road after %.1f, %.1f, %.1f, %.1f s\n", r0.c_str(), double(back[0]),
                double(back[1]), double(back[2]), double(back[3]));
    expect(r0 == "238 B, 266 B, 106 A, 107 B, 201 B, 202 B, 378 A", "cable car 0 runs 238 B, 266 B, 106 A, 107 B, 201 B, 202 B, 378 A");
    expect(std::fabs(back[0] - 208.5f) < 0.2f && std::fabs(back[1] - 208.5f) < 0.2f && std::fabs(back[2] - 129.8f) < 0.2f &&
               std::fabs(back[3] - 129.8f) < 0.2f,
           "running free, cars 0 and 1 are back on their start road after 208.5 s, cars 2 and 3 after 129.8 s");
}

}  // namespace

void testAiNetwork(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect) {
    std::printf("== AI road network (city/*.bai, *.aimap)\n");
    rawFileChecks(vfs, expect);
    aimapChecks(vfs, expect);
    networkChecks(vfs, expect);
    std::printf("== City list\n");
    cityListChecks(vfs, expect);
    std::printf("== Street props, path sets, decals\n");
    streetPropChecks(vfs, expect);
    std::printf("== Police, route search, AI driver\n");
    policeChecks(vfs, expect);
    std::printf("== Loose bodies\n");
    looseChecks(vfs, expect);
    std::printf("== Moving level objects\n");
    levelObjectChecks(vfs, expect);
    std::printf("== Cable cars\n");
    cableCarChecks(vfs, expect);
}
