// Native checks of the pedestrian files and the pedestrian AI against the counts in
// spec/formats/ped-anim.md and spec/ai/pedestrians.md ("Checked against").
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <string>

#include "ai/pedestrians.h"
#include "ai/road_network.h"
#include "ai/traffic.h"
#include "ai/traffic_control.h"
#include "city/city_data.h"
#include "core/strutil.h"
#include "formats/aimap.h"
#include "formats/race_files.h"
#include "game/race_catalogue.h"
#include "formats/ped_anim.h"
#include "formats/pkg.h"
#include "model/asset_cache.h"
#include "model/ped_model.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

using namespace cc;

namespace {

using Expect = std::function<void(bool, const std::string&)>;

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

struct SeqWant {
    const char* name;
    int manFrames;
    float manVf, manVs;
    int womFrames;
    float womVf, womVs;
    const char* next;
};

// ped-anim.md, "Shipped values".
const SeqWant kSeqWants[] = {
    {"STAND", 30, 0, 0, 29, 0, 0, "STAND"},
    {"STAND2", 29, 0, 0, 29, 0, 0, "STAND2"},
    {"STAND_WALK", 3, 2.1077f, 0, 3, 0.4575f, 0, "WALK"},
    {"WALK", 20, 2.1137f, 0, 20, 1.6307f, 0, "WALK"},
    {"WALK_STAND", 10, 0.3690f, 0, 9, 1.3021f, 0, "STAND"},
    {"STAND_ANTIC", 4, 0.1620f, 0, 7, 0.3638f, 0, "ANTIC"},
    {"ANTIC", 19, 0, 0, 20, 0, 0, "ANTIC"},
    {"ANTIC2", 19, 0, 0, 20, 0, 0, "ANTIC2"},
    {"ANTIC_WALK", 3, 1.4551f, 0, 3, 0.1425f, 0, "WALK"},
    {"WALK_ANTIC", 9, 0.4400f, 0, 9, 0.8851f, 0, "ANTIC"},
    {"ANTIC_LDIVE", 10, 0, 6.6007f, 10, 0, 6.2706f, "LDIVE_GROUNDL"},
    {"LDIVE_GROUNDL", 10, 0, 6.5407f, 10, 0, 8.7009f, "GROUND_STANDL"},
    {"GROUND_STANDL", 44, 0, 0, 44, 0, 0, "STAND"},
    {"GROUND_STANDL2", 44, 0, 0, 44, 0, 0, "STAND"},
    {"GROUND_STANDR", 44, 0, 0, 44, 0, 0, "STAND"},
    {"GROUND_STANDR2", 44, 0, 0, 44, 0, 0, "STAND"},
    {"ANTIC_RDIVE", 10, 0, -6.6007f, 10, 0, -6.2706f, "RDIVE_GROUNDR"},
    {"RDIVE_GROUNDR", 10, 0, -6.5407f, 10, 0, -8.7009f, "GROUND_STANDR"},
    {"WALK_RDIVE", 24, 0, -2.6259f, 24, 0, -2.1146f, "RDIVE_GROUNDR"},
    {"WALK_LDIVE", 24, 0, 2.7803f, 24, 0, 2.1146f, "LDIVE_GROUNDL"},
    {"RUN", 12, 7.1357f, 0, 12, 5.6481f, 0, "RUN"},
    {"BACKUP", 1, 0, 0, 1, 0, 0, "BACKUP"},
    {"BACKUP_WALK", 3, 1.2601f, 0, 3, 0.4725f, 0, "WALK"},
    {"RUN_WALK", 9, 3.7684f, 0, 7, 2.2240f, 0, "WALK"},
};

void fileChecks(const Vfs& vfs, const Expect& expect) {
    // Every .anim file: 67 shipped, all but pedanim_manantrnch parse to their last byte.
    int files = 0, valid = 0, exact = 0, sixty = 0;
    std::string refused;
    std::set<std::string> animNames;
    for (const auto& path : vfs.listUnique("anim/")) {
        if (!iendsWith(path, ".anim")) continue;
        ++files;
        BytesPtr b = vfs.open(path);
        PedAnimation a;
        std::string err;
        if (b && parsePedAnimation(b->data(), b->size(), a, err)) {
            ++valid;
            if (a.exact) ++exact;
            if (a.channels == 60) ++sixty;
            std::string n = path.substr(5, path.size() - 10);
            animNames.insert(toLower(n));
        } else {
            refused += path + " ";
        }
    }
    std::printf("  anim/: %d animation files, %d valid (%d to the last byte, %d with 60 channels); refused: %s\n", files,
                valid, exact, sixty, refused.c_str());
    expect(files == 67 && valid == 66 && exact == 66 && sixty == 66 && refused.find("manantrnch") != std::string::npos,
           "67 .anim files, 66 valid with 60 channels to their last byte, pedanim_manantrnch refused");
    {
        BytesPtr b = vfs.open("anim/pedanim_manwalk.anim");
        PedAnimation a;
        std::string err;
        const bool ok = b && parsePedAnimation(b->data(), b->size(), a, err);
        BytesPtr r = vfs.open("anim/pedanim_manrun.anim");
        PedAnimation ar;
        const bool okr = r && parsePedAnimation(r->data(), r->size(), ar, err);
        expect(ok && okr && a.frames == 20 && std::fabs(a.stride - 1.5518f) < 0.0001f && ar.frames == 12 &&
                   std::fabs(ar.stride - 2.9696f) < 0.0001f && a.flag == 1,
               strf("pedanim_manwalk 20 frames stride %.4f, pedanim_manrun 12 frames stride %.4f", double(a.stride),
                    double(ar.stride)));
    }

    // The four sequence tables.
    std::set<std::string> named;
    const char* models[4] = {"pedmodel_man", "pedmodel_manw", "pedmodel_woman", "pedmodel_womanw"};
    AssetCache assets(vfs);
    for (const char* mn : models) {
        const bool woman = std::string(mn).find("woman") != std::string::npos;
        const std::vector<PedSeqRow> rows = parsePedSequenceTable(textOf(vfs.open(strf("anim/%s.csv", mn))));
        for (const PedSeqRow& r : rows) named.insert(toLower(r.anim));
        PedModel m;
        std::string err;
        const bool ok = m.load(vfs, assets, mn, err);
        int bad = 0;
        std::string badList;
        for (const SeqWant& w : kSeqWants) {
            const int i = m.find(w.name);
            if (i < 0) {
                ++bad;
                badList += strf(" %s(missing)", w.name);
                continue;
            }
            const PedSequence& s = m.sequences()[size_t(i)];
            const int fr = woman ? w.womFrames : w.manFrames;
            const float vf = woman ? w.womVf : w.manVf, vs = woman ? w.womVs : w.manVs;
            const std::string& next = m.sequences()[size_t(s.next)].row.name;
            if (s.frameCount != fr || std::fabs(s.vf - vf) > 0.00006f || std::fabs(s.vs - vs) > 0.00006f || next != w.next ||
                !s.nextFound) {
                ++bad;
                badList += strf(" %s(%d %.4f %.4f %s)", w.name, s.frameCount, double(s.vf), double(s.vs), next.c_str());
            }
        }
        const int backup = m.seq(PsBACKUP);
        const bool backupDir = backup >= 0 && m.sequences()[size_t(backup)].direction == 0;
        expect(ok && rows.size() == 24 && m.sequences().size() == 24 && bad == 0 && backupDir && m.seq(PsLDIVE) < 0 &&
                   m.seq(PsRDIVE) < 0 && m.unlinkedRows() == 0 && m.rowsWithoutAnimation() == 0,
               strf("%s: 24 rows with the shipped frame counts, speeds and links (BACKUP direction 0, no LDIVE/RDIVE)%s%s", mn,
                    badList.c_str(), ok ? "" : (" " + err).c_str()));
        // After the fix-up and the drift removal the root x and z of the first and last played frame
        // are below 2.1e-8.
        float worst = 0;
        for (const PedSequence& s : m.sequences()) {
            if (!s.anim) continue;
            for (int f : {0, s.N}) {
                worst = std::max(worst, std::fabs(s.anim->at(f, 0)));
                worst = std::max(worst, std::fabs(s.anim->at(f, 2)));
            }
        }
        expect(worst < 2.1e-8f, strf("%s: root x and z of the first and last played frames below 2.1e-8 (%.3g)", mn, double(worst)));
        // Model, skeleton, variants, rays.
        const PedMod& md = m.mod();
        int wantV = 0, wantA = 0, wantT = 0, wantM = 0, wantP = 0;
        const std::string s(mn);
        if (s == "pedmodel_man") wantV = 117, wantA = 248, wantT = 230, wantM = 18, wantP = 18;
        if (s == "pedmodel_manw") wantV = 128, wantA = 252, wantT = 260, wantM = 17, wantP = 20;
        if (s == "pedmodel_woman") wantV = 123, wantA = 696, wantT = 232, wantM = 17, wantP = 0;
        if (s == "pedmodel_womanw") wantV = 127, wantA = 723, wantT = 241, wantM = 16, wantP = 0;
        const bool rain = s.back() == 'w';
        std::printf("  %s: %s layout, %zu vertices, %d adjuncts (%d in the packets), %d triangles, %zu materials, %d packets, "
                    "%zu bones, %d variants, %zu ray rows, radius %.2f m\n",
                    mn, md.packetLayout ? "packet" : "global", md.V.size(), md.adjuncts, md.adjunctTotal(), md.triangleTotal(), md.mats.size(),
                    md.packetCount, m.skeleton().bones.size(), m.variantCount(), m.rays().rows.size(), double(m.radius()));
        expect(int(md.V.size()) == wantV && md.adjuncts == wantA && md.triangleTotal() == wantT &&
                   int(md.mats.size()) == wantM && md.packetCount == wantP && md.packetLayout == (wantP > 0),
               strf("%s: %d vertices, %d adjuncts, %d triangles, %d materials%s", mn, wantV, wantA, wantT, wantM,
                    wantP ? strf(" in %d packets", wantP).c_str() : " (global layout)"));
        expect(m.skeleton().bones.size() == 19 && m.skeleton().declared == 19 && md.matrices == 19 && md.packetBoneMismatches == 0 &&
                   m.hasShaders() && m.variantCount() == (rain ? 24 : 48) && m.materials().materialsPerSet == int(md.mats.size()) &&
                   m.hasRays() && int(m.rays().rows.size()) == m.variantCount() && m.rays().count == 19,
               strf("%s: 19 bones, matrices 19, packet bones agree with mtxv (%d mismatches), %d paint variants of %d materials, "
                    ".rays with one row per variant",
                    mn, md.packetBoneMismatches, rain ? 24 : 48, int(md.mats.size())));
        if (s == "pedmodel_man") {
            const PedBone& root = m.skeleton().bones[0];
            const char* names[19] = {"root",       "spine",   "neck",    "head",       "clavicle_r", "shoulder_r", "elbow_r",
                                     "wrist_r",    "clavicle_l", "shoulder_l", "elbow_l", "wrist_l", "pelvis",   "hip_l",
                                     "knee_l",     "ankle_l", "hip_r",   "knee_r",     "ankle_r"};
            bool order = true;
            for (int i = 0; i < 19; ++i)
                if (m.skeleton().bones[size_t(i)].name != names[i]) order = false;
            expect(order && std::fabs(root.offset.x - 0.00057f) < 1e-6f && std::fabs(root.offset.y - 1.14721f) < 1e-6f,
                   "pedmodel_man: the 19 bones in order, root offset (0.00057, 1.14721, 0)");
            // A posed STAND figure stands: head above 1.5 m, ankles below 0.2 m, on the spot.
            std::vector<PedBoneXf> pose;
            m.pose(m.seq(PsSTAND), 0, pose);
            const int head = m.skeleton().find("head"), al = m.skeleton().find("ankle_l"), ar = m.skeleton().find("ankle_r");
            expect(head >= 0 && al >= 0 && ar >= 0 && pose[size_t(head)].t.y > 1.5f && pose[size_t(al)].t.y < 0.2f &&
                       pose[size_t(ar)].t.y < 0.2f && std::fabs(pose[0].t.x) < 0.05f && std::fabs(pose[0].t.z) < 0.05f,
                   strf("pedmodel_man STAND frame 0: head at %.2f m, ankles at %.2f / %.2f m", double(pose[size_t(head)].t.y),
                        double(pose[size_t(al)].t.y), double(pose[size_t(ar)].t.y)));
            // Triangle winding: counter-clockwise from the front (like packages) for most triangles.
            Mesh mesh;
            m.buildMesh(pose, mesh);
            int ccw = 0, cw = 0;
            for (const Batch& b : mesh.batches)
                for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
                    const Vertex& a = b.vertices[b.indices[i]];
                    const Vertex& c1 = b.vertices[b.indices[i + 1]];
                    const Vertex& c2 = b.vertices[b.indices[i + 2]];
                    const vec3 n = cross(c1.pos - a.pos, c2.pos - a.pos);
                    const vec3 vn = a.normal + c1.normal + c2.normal;
                    if (dot(n, vn) > 0) ++ccw;
                    else ++cw;
                }
            std::printf("  pedmodel_man winding: %d triangles counter-clockwise to their normals, %d clockwise\n", ccw, cw);
            expect(ccw > cw * 4, "pedmodel_man triangles are counter-clockwise seen from the front");
            // Ray colours: (255, trunc(r × 178.5), …).
            const Material* mat = m.materials().material(0, m.rays().rows[0][0]);
            const uint32_t c = m.rayColour(0, 0, 1.0f);
            expect(mat && ((c >> 16) & 255u) == (uint32_t(int(std::trunc(mat->diffuse.x * 178.5f))) & 255u) && (c >> 24) == 255u,
                   strf("pedmodel_man ray colour of bone 0, variant 0: %08X", c));
        }
    }
    // 48 animation files are named by the tables, 19 are not.
    int namedValid = 0;
    for (const std::string& n : named)
        if (animNames.count(n)) ++namedValid;
    expect(named.size() == 48 && namedValid == 48 && animNames.size() - size_t(namedValid) == 18,
           strf("%zu animation files named by the tables (all valid), %zu valid ones not named (+ pedanim_manantrnch)", named.size(),
                animNames.size() - size_t(namedValid)));
    // The translucent-material move (ped-anim.md, ".mod model"): kinds 0 untextured, 1 opaque,
    // 2 translucent. [T0, U1, O2, T3, U4, O5, U6] -> j = 5; i = 0 swaps with 5 (j = 4), i = 3 swaps
    // with 4 (j = 3): [O5, U1, O2, U4, T3, T0, U6].
    {
        const int kinds[7] = {2, 0, 1, 2, 0, 1, 0};
        std::vector<PedModMaterial> mats(7);
        for (int i = 0; i < 7; ++i) mats[size_t(i)].name = std::to_string(i);
        const int swaps = pedReorderTranslucent(mats, [&](const PedModMaterial& m) { return kinds[std::stoi(m.name)]; });
        std::string order;
        for (const auto& m : mats) order += m.name;
        expect(swaps == 2 && order == "5124306", "translucent .mod materials move toward the end: 0123456 -> " + order);
        std::vector<PedModMaterial> none(3);
        expect(pedReorderTranslucent(none, [](const PedModMaterial&) { return 0; }) == 0,
               "untextured materials (all shipped models) never move");
    }
    // The gender test (traffic-sounds.md).
    expect(!pedModelIsFemale("pedmodel_man") && !pedModelIsFemale("pedmodel_manw") && pedModelIsFemale("pedmodel_woman") &&
               pedModelIsFemale("pedmodel_womanw") && !pedModelIsFemale("ffemale") && !pedModelIsFemale("wwoman") &&
               pedModelIsFemale("xGiRl") && pedModelIsFemale("Hooker2"),
           "gender: woman models female, man models male; 'ffemale' and 'wwoman' do not match (MM2's substring test)");
}

// pedestrians.md, "Checked against", and short runs of the pedestrian AI on the real cities.
void cityChecks(const Vfs& vfs, const Expect& expect) {
    // No race aimap has a pedestrian list.
    int raceFiles = 0, withPeds = 0;
    for (const auto& path : vfs.listUnique("race/")) {
        if (!iendsWith(path, ".aimap")) continue;
        ++raceFiles;
        if (!parseRaceAimap(textOf(vfs.open(path))).peds.empty()) ++withPeds;
    }
    expect(raceFiles > 0 && withPeds == 0, strf("%d race aimaps, none with a pedestrian list", raceFiles));
    for (const char* city : {"sf", "london"}) {
        const bool L = std::string(city) == "london";
        CityData cd;
        AssetCache assets(vfs);
        std::string msg;
        if (!cd.load(city, vfs, assets, msg) || !cd.network()) {
            expect(false, strf("%s loads with its AI network", city));
            continue;
        }
        const ai::RoadNetwork& net = *cd.network();
        const CityAimap& am = net.cityAimap();
        expect(am.pedPool == 100 && am.peds.size() == 2 && am.peds[0].good == "pedmodel_man" && am.peds[0].bad == "pedmodel_manw" &&
                   am.peds[1].good == "pedmodel_woman" && am.peds[1].bad == "pedmodel_womanw",
               strf("%s aimap: pool %d, pairs pedmodel_man/manw and pedmodel_woman/womanw", city, am.pedPool));
        // Activation tables.
        const int rooms = int(cd.rooms().size());
        int withRoads = 0, maxRoads = 0, subsets = 0;
        double sum = 0;
        for (int r = 0; r < rooms; ++r) {
            const auto& pl = net.pedRoads(r);
            if (pl.empty()) {
                ++subsets;  // an empty list is a subset too
                continue;
            }
            ++withRoads;
            sum += double(pl.size());
            maxRoads = std::max(maxRoads, int(pl.size()));
            const auto& tl = net.ambientRoads(r);
            bool sub = true;
            for (uint16_t x : pl)
                if (std::find(tl.begin(), tl.end(), x) == tl.end()) sub = false;
            if (sub) ++subsets;
        }
        const double mean = withRoads ? sum / withRoads : 0;
        std::printf("  %s pedestrian tables: %d rooms, %d with roads (mean %.1f, at most %d), %d subsets of the traffic list\n",
                    city, rooms, withRoads, mean, maxRoads, subsets);
        expect(rooms == (L ? 1342 : 1172) && withRoads == (L ? 1115 : 1066) && std::lround(mean * 10) == (L ? 70 : 66) &&
                   maxRoads == (L ? 22 : 20) && subsets == (L ? 1024 : rooms),
               strf("%s: pedestrian activation tables as in pedestrians.md", city));
        // Main-road sides.
        int allowed = 0, closed = 0, oneSidewalk = 0, sides = 0;
        double km = 0;
        for (int r = 0; r < net.mainRoadCount(); ++r)
            for (int s = 0; s < 2; ++s) {
                const ai::RoadSide& sd = net.roads()[size_t(r)].side[s];
                ++sides;
                if (sd.sidewalks == 1) ++oneSidewalk;
                if (sd.flags & 2) {
                    ++closed;
                    continue;
                }
                ++allowed;
                const ai::Lane& w = sd.lines[size_t(sd.lanes)];
                km += double(w.D.back()) / 1000.0;
            }
        std::printf("  %s main-road sides: %d (%d with one sidewalk), pedestrians on %d (%d closed), %.1f km of sidewalk\n", city,
                    sides, oneSidewalk, allowed, closed, km);
        expect(oneSidewalk == sides && allowed == (L ? 868 : 690) && closed == (L ? 212 : 68) &&
                   std::lround(km * 10) == (L ? 779 : 755),
               strf("%s: pedestrians allowed on %d sides (%d closed), %.1f km of walkable sidewalk", city, L ? 868 : 690, L ? 212 : 68,
                    L ? 77.9 : 75.5));
        int kind1 = 0;
        for (const ai::LightSet& set : net.control().sets())
            if (set.kind == 1) ++kind1;
        expect(kind1 == (L ? 128 : 116) && net.control().sets().size() == size_t(L ? 218 : 182),
               strf("%s: %d of %zu light sets have a walk phase", city, kind1, net.control().sets().size()));

        // Fresh-start fill with the Cruise pool of 25 in every room.
        phys::CollisionWorld world;
        world.setupCity(vfs, cd);
        ai::PedestrianSystem peds;
        const int count = ai::PedestrianSystem::countFor(am.pedPool, 0.25f);
        peds.create(vfs, assets, cd, world, count, false);
        expect(count == 25 && peds.count() == 25 && ai::PedestrianSystem::countFor(100, 0.25f, true) == 0 &&
                   ai::PedestrianSystem::countFor(100, 0.5f, false, true) == 0 && ai::PedestrianSystem::countFor(100, 0.1f) == 10,
               "pool count: 100 × 0.25 = 25; 0 when off or in a Circuit race; 100 × 0.1 = 10");
        {
            int models[2] = {}, variantsOk = 0;
            for (const ai::Pedestrian& p : peds.peds()) {
                const PedModel* m = peds.modelOf(p);
                if (!m) continue;
                ++models[m->female() ? 1 : 0];
                if (p.variant >= 0 && p.variant < m->variantCount() - 1) ++variantsOk;
            }
            expect(models[0] + models[1] == 25 && models[0] > 0 && models[1] > 0 && variantsOk == 25 && peds.models().size() == 2,
                   strf("%s pool: %d men, %d women, every paint variant below the last", city, models[0], models[1]));
        }
        int all = 0, two = 0, none = 0, other = 0, roomsTested = 0;
        for (int r = 1; r < rooms; ++r) {
            ++roomsTested;
            peds.resetAtRoom(r);
            const int n = peds.onStreets();
            if (n == 25) ++all;
            else if (n == 2) ++two;
            else if (n == 0) ++none;
            else ++other;
        }
        std::printf("  %s fresh starts (pool 25): all placed in %d of %d rooms, 2 in %d, none in %d, other %d\n", city, all,
                    roomsTested, two, none, other);
        expect(all == (L ? 1113 : 1054) && two == (L ? 2 : 12) && none == (L ? 226 : 105) && other == 0,
               strf("%s: fresh-start fill as in pedestrians.md (25 / 2 / none)", city));

        // A minute with a parked player at the repeatable start, with traffic: everyone stays on the
        // streets, finite, near its road; corners turned and streets crossed in the walk phases.
        ai::TrafficSystem ts;
        ts.create(vfs, assets, cd, world, 0.5f);
        ai::PedPlayer pl;
        const vec3 start = cd.starts().empty() ? vec3() : cd.starts()[0].pos;
        pl.bodyPos = start + vec3(0, 0.8f, 0);
        pl.radius = 2.5f;
        ai::TrafficPlayer tp;
        tp.modelOrigin = start + vec3(0, 0.3f, 0);
        tp.bodyPos = pl.bodyPos;
        tp.radius = pl.radius;
        const std::vector<ai::PedPlayer> players{pl};
        const std::vector<ai::TrafficPlayer> tplayers{tp};
        ts.reset(tplayers);
        peds.reset(players);
        const int startOn = peds.onStreets();
        const float F = float(1.0 / 60.0);
        int nan = 0, twice = 0, far = 0, crossingSteps = 0;
        float worstStep = 0;
        std::vector<vec3> prev(size_t(peds.count()));
        std::vector<int> prevUpdate(size_t(peds.count()), -2);
        for (int f = 0; f < 3600; ++f) {
            ts.updateCars(F, tplayers);
            peds.update(F, players, &ts);
            ts.finishFrame(F);
            ts.afterPhysics();
            std::set<int> seen;
            for (int i : peds.updateOrder()) {
                if (!seen.insert(i).second) ++twice;
                const ai::Pedestrian& p = peds.peds()[size_t(i)];
                if (!std::isfinite(p.p.x + p.p.y + p.p.z + p.h)) ++nan;
                if (p.step != 0) ++crossingSteps;
                // Within 30 m of a centre point of its road (crossings and corners included).
                const ai::Road& rd = net.roads()[size_t(p.road)];
                float best = 1e9f;
                for (const vec3& c : rd.C) best = std::min(best, (c.x - p.p.x) * (c.x - p.p.x) + (c.z - p.p.z) * (c.z - p.p.z));
                if (best > 900.0f + rd.length() * rd.length() * 0.25f) ++far;
                if (prevUpdate[size_t(i)] == f - 1) {
                    const vec3 d = p.p - prev[size_t(i)];
                    const float st = std::sqrt(d.x * d.x + d.z * d.z);
                    if (st > 0.3f && std::getenv("CC_PEDDEBUG"))
                        std::printf("    jump f %d ped %d %.2f m: road %d side %d dir %d k %d mode %d step %d (%.1f %.1f) <- (%.1f %.1f)\n", f, i,
                                    double(st), p.road, p.side, p.dir, p.k, p.mode, p.step, double(p.p.x), double(p.p.z),
                                    double(prev[size_t(i)].x), double(prev[size_t(i)].z));
                    worstStep = std::max(worstStep, st);
                }
                prev[size_t(i)] = p.p;
                prevUpdate[size_t(i)] = f;
            }
        }
        const auto& st = peds.stats();
        std::printf("  %s parked minute: %d on the streets, %d road ends, %d crossings started, %d turn-backs, %d step-arounds, "
                    "%d lost, largest step %.3f m\n",
                    city, peds.onStreets(), st.roadEnds, st.crossings, st.turnBacks, st.stepArounds, peds.lostCount(),
                    double(worstStep));
        // Room memory (pedestrians.md, "Rooms and pedestrian roads"): Find(P_c, memory), stored every
        // frame, 0 included. A frame outside every room, then a room far away: the second adjust runs
        // from room 0, so nothing is emptied and nobody is free to place on the new roads.
        {
            ai::PedestrianSystem rm;
            rm.create(vfs, assets, cd, world, 25, false);
            rm.reset(players);
            const int roomA = rm.playerRoom(0);
            const int emptied0 = rm.stats().emptied;
            const int active0 = int(rm.activeRoads().size());
            ai::PedPlayer out = pl;
            out.bodyPos = vec3(1.0e5f, 0.0f, 1.0e5f);
            rm.update(F, {out}, nullptr);
            const int roomOut = rm.playerRoom(0);
            ai::PedPlayer farAway = pl;
            const CityStart& fs = cd.starts()[cd.starts().size() / 2];
            farAway.bodyPos = fs.pos + vec3(0, 0.8f, 0);
            rm.update(F, {farAway}, nullptr);
            const int roomB = rm.playerRoom(0);
            std::printf("  %s room memory: room %d, then %d outside, then %d: %d roads active (was %d), %d emptied, %d on the "
                        "streets\n",
                        city, roomA, roomOut, roomB, int(rm.activeRoads().size()), active0, rm.stats().emptied - emptied0,
                        rm.onStreets());
            expect(roomA != 0 && roomOut == 0 && roomB != 0 && roomB != roomA && rm.stats().emptied == emptied0 &&
                       rm.onStreets() == 25 && int(rm.activeRoads().size()) >= active0,
                   strf("%s: after a frame in room 0 the next adjust starts from room 0 and empties nothing", city));
            // A NaN heading makes a pedestrian lost: it stays in the update order and ticks the shared
            // clock, but is no longer moved; a new placement makes it normal.
            rm.reset(players);
            int lostIdx = -1;
            for (const ai::Pedestrian& p : rm.peds())
                if (p.onStreets) {
                    lostIdx = p.index;
                    break;
                }
            rm.testSetHeading(lostIdx, std::numeric_limits<float>::quiet_NaN());
            const vec3 before = rm.peds()[size_t(lostIdx)].p;
            int inOrder = 0, orderSize = 0;
            for (int f = 0; f < 120; ++f) {
                rm.update(F, players, nullptr);
                orderSize = int(rm.updateOrder().size());
                inOrder = 0;
                for (int i : rm.updateOrder())
                    if (i == lostIdx) ++inOrder;
            }
            const ai::Pedestrian& lp = rm.peds()[size_t(lostIdx)];
            const bool still = lp.p.x == before.x && lp.p.z == before.z;
            const bool wasLost = lp.lost && rm.lostCount() == 1;
            rm.reset(players);
            expect(wasLost && still && inOrder == 1 && orderSize == 25 && rm.lostCount() == 0,
                   strf("%s: a NaN heading makes a pedestrian lost (kept in the update order, not moved); a reset makes it "
                        "normal",
                        city));
        }
        // Reactions: a scripted car (no physics) along the sidewalk of a busy street, slowly and then
        // at 12 m/s, with the pool at density 0.5: pedestrians stare or run, dive or back up.
        {
            std::vector<int> listed(size_t(net.mainRoadCount()), 0);
            for (int room = 0; room < rooms; ++room)
                for (uint16_t r : net.pedRoads(room))
                    if (r < listed.size()) ++listed[r];
            int best = -1;
            for (int r = 0; r < net.mainRoadCount(); ++r) {
                const ai::Road& rd = net.roads()[size_t(r)];
                if (rd.side[1].lanes < 1 || (rd.side[0].flags & 2) || (rd.side[1].flags & 2) || rd.n < 3 || (rd.flags & 4)) continue;
                if (rd.length() < 150.0f || rd.length() > 600.0f) continue;
                if (best < 0 || listed[size_t(r)] > listed[size_t(best)]) best = r;
            }
            ai::PedestrianSystem rp;
            rp.create(vfs, assets, cd, world, ai::PedestrianSystem::countFor(am.pedPool, 0.5f), false);
            const ai::Road& rd = net.roads()[size_t(std::max(best, 0))];
            const std::vector<vec3>& path = rd.side[1].lines[size_t(rd.side[1].lanes)].P;
            std::vector<float> dist(path.size(), 0.0f);
            for (size_t i = 1; i < path.size(); ++i) dist[i] = dist[i - 1] + length(path[i] - path[i - 1]);
            float sAlong = 0;
            auto player = [&](float v) {
                size_t i = 1;
                while (i + 1 < path.size() && dist[i] < sAlong) ++i;
                const float seg = dist[i] - dist[i - 1];
                const float w = seg > 0 ? std::clamp((sAlong - dist[i - 1]) / seg, 0.0f, 1.0f) : 0.0f;
                vec3 f = path[i] - path[i - 1];
                f.y = 0;
                f = normalize(f);
                ai::PedPlayer pp;
                pp.bodyPos = path[i - 1] + (path[i] - path[i - 1]) * w + vec3(0, 0.6f, 0);
                pp.Z = f * -1.0f;
                pp.X = vec3(pp.Z.z, 0, -pp.Z.x);
                pp.velocity = f * v;
                pp.speed = v;
                pp.gear = 2;
                pp.inertiaBox = vec3(1.6f, 1.4f, 3.6f);
                pp.radius = 2.2f;
                return std::vector<ai::PedPlayer>{pp};
            };
            rp.reset(player(0.0f));
            int rnan = 0;
            for (int f = 0; f < 60 * 30 && best >= 0; ++f) {
                const float v = sAlong >= dist.back() ? 0.0f : (f < 600 ? 4.0f : 12.0f);
                sAlong = std::min(sAlong + v * F, dist.back());
                rp.update(F, player(v), nullptr);
                for (int i : rp.updateOrder()) {
                    const ai::Pedestrian& p = rp.peds()[size_t(i)];
                    if (!std::isfinite(p.p.x + p.p.y + p.p.z + p.h)) ++rnan;
                }
            }
            const auto& rs = rp.stats();
            std::printf("  %s sidewalk drive on road %d: wary %d, dives %d, back-ups %d, %zu voice events in the last frame, "
                        "lost %d\n",
                        city, best, rs.wary, rs.dives, rs.backups, rp.voiceEvents().size(), rp.lostCount());
            expect(best >= 0 && rnan == 0 && rp.lostCount() == 0 && rs.wary > 0 && rs.dives + rs.backups > 0,
                   strf("%s: a car driving along a sidewalk makes pedestrians wary (%d) and dive or back up (%d, %d)", city,
                        rs.wary, rs.dives, rs.backups));
        }
        expect(startOn == 25 && peds.onStreets() == 25 && nan == 0 && twice == 0 && far == 0 && peds.lostCount() == 0 &&
                   st.roadEnds > 0 && st.crossings > 0 && crossingSteps > 0 && worstStep < 0.25f,
               strf("%s: 60 s with a parked player: 25 pedestrians walk (finite, near their roads, no double updates), turn "
                    "corners and cross streets",
                    city));
    }
}

// Race densities (pedestrians.md, "Settings" and "Checked against"; races.md, "The session's
// settings"): the Peds column of each race's row; no pedestrians in a Circuit race.
void raceChecks(const Vfs& vfs, const Expect& expect) {
    const game::RaceMode modes[4] = {game::RaceMode::Checkpoint, game::RaceMode::Circuit, game::RaceMode::Blitz,
                                     game::RaceMode::CrashCourse};
    for (const char* dir : {"sf", "london"}) {
        const bool L = std::string(dir) == "london";
        std::string line;
        int circuitPeople = 0, crashNonZero = 0, cpNonZero = 0, rows = 0;
        float cpMax = 0, blitzMin = 1e9f, blitzMax = 0;
        for (game::RaceMode m : modes) {
            const auto data = parseRaceData(textOf(vfs.open(game::settingsPath(dir, m))));
            std::string d;
            for (const RaceDataRow& r : data)
                for (bool pro : {false, true}) {
                    ++rows;
                    const float rho = r.of(pro).peds;
                    const int count = ai::PedestrianSystem::countFor(100, rho, false, m == game::RaceMode::Circuit);
                    d += strf(" %g", double(rho));
                    if (m == game::RaceMode::Circuit) circuitPeople += count;
                    if (m == game::RaceMode::CrashCourse && rho != 0.0f) ++crashNonZero;
                    if (m == game::RaceMode::Checkpoint) {
                        cpMax = std::max(cpMax, rho);
                        if (rho != 0.0f) ++cpNonZero;
                    }
                    if (m == game::RaceMode::Blitz) {
                        blitzMin = std::min(blitzMin, rho);
                        blitzMax = std::max(blitzMax, rho);
                    }
                }
            line += strf("  %s %s:%s\n", dir, game::modeKey(m), d.c_str());
        }
        std::printf("%s", line.c_str());
        // As read (spec/QUESTIONS.md 102: SF's Checkpoint rows go up to 0.3, not 0.1).
        const bool cp = L ? cpNonZero == 2 && cpMax == 0.1f : cpMax == 0.3f;
        const bool blitz = L ? blitzMin == 0.1f && blitzMax == 0.2f : blitzMin == 0.1f && blitzMax == 0.5f;
        expect(rows > 0 && circuitPeople == 0 && crashNonZero == 0 && cp && blitz,
               strf("%s race pedestrians: none in Circuit (count 0) and Crash Course rows; Checkpoint %s, Blitz %s", dir,
                    L ? "0 except two rows at 0.1" : "0 - 0.3", L ? "0.1 - 0.2" : "0.1 - 0.5"));
    }
}

}  // namespace

void testPedestrians(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect) {
    std::printf("== Pedestrians: anim/ files\n");
    fileChecks(vfs, expect);
    std::printf("== Pedestrians: population and walking\n");
    cityChecks(vfs, expect);
    std::printf("== Pedestrians: race densities\n");
    raceChecks(vfs, expect);
}
