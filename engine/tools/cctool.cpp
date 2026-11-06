// cctool: native command-line tooling for CrazyCity Insanity (no SDL/GL).
//
//   cctool test <archive.ar>...          load every package, pivot, texture and car; report
//   cctool list <archive.ar>             list an archive's entries
//   cctool cat <path> <archive.ar>...    write one file from the mounted archives to stdout
//   cctool drive [--trace] <file.run> <archive.ar>...  one check run, CSV (or the drive trace) to stdout
//   cctool drives <dir> <outdir> <archive.ar>...     every *.run in dir -> outdir/*.csv
//   cctool uitest <game folder> <archive.ar>...       player files, progression, mmlang, HUD checks
//   cctool uiscreens <game folder> <out dir> <archive.ar>...  the engine-drawn menus: keys, clicks, PNGs
//   cctool trailer <car> <archive.ar>...    a car's trailer set-up (trailer.md 2), or why it has none
//   cctool room <city> <room> <archive.ar>...         dump one PSDL room's elements
//   cctool inst <path> <name filter> <archive.ar>...   list placements
//   cctool pkg <name> <archive.ar>...    describe a package (entries, materials)
//   cctool testpeds <archive.ar>...      only the pedestrian checks
//   cctool peds <city> <parked|street> <seconds> <density> <out.csv> <archive.ar>...
//                                        a pedestrian run (pedestrians.md, "Check runs"), CSV
//   cctool pack [--deflate] <out.ar> <dir>
//                                        pack a folder into an archive (paths relative to dir)
//   cctool mkcarmod [--deflate] <out.ar> <src car> <new car> <description> <archive.ar>...
//                                        make a mod archive with a renamed copy of a stock car
#include <sys/stat.h>
#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/strutil.h"
#include "city/city_data.h"
#include "city/psdl_geometry.h"
#include "formats/car_info.h"
#include "formats/city_files.h"
#include "formats/inst.h"
#include "formats/psdl.h"
#include "formats/image.h"
#include "formats/mtx.h"
#include "formats/pkg.h"
#include "formats/texture_lookup.h"
#include "model/asset_cache.h"
#include "model/vehicle.h"
#include "physics/drive_check.h"
#include "physics/bound.h"
#include "physics/collision_world.h"
#include "physics/trailer.h"
#include "vfs/ar_writer.h"
#include "vfs/vfs.h"

using namespace cc;

void testAiNetwork(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect);
void testEffects(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect);
void testPedestrians(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect);
int cmdPeds(const std::string& cityName, const std::string& scenario, float seconds, float density, const std::string& outPath,
            const std::vector<std::string>& archives);
void testAudio(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect);
int cmdSoundRun(const std::string& cityName, int copIndex, float seconds, float density, const std::string& outPath,
                const std::vector<std::string>& archives);
void testRaces(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect);
void testNet(const Vfs& vfs, const std::string& outPath, const std::function<void(bool, const std::string&)>& expect);
int cmdNetServer(const std::vector<std::string>& args);
int cmdNetClient(const std::vector<std::string>& args);
int cmdTraffic(const std::string& cityName, float seconds, float density, const std::string& outPath,
               const std::vector<std::string>& archives);
int cmdChase(const std::string& cityName, int copIndex, float seconds, float density, const std::string& outPath,
             const std::vector<std::string>& archives);
int cmdDriveSim(const std::string& scriptPath, const std::string& outPath, const std::vector<std::string>& archives);
int cmdTrafficSurvey(const std::string& cityName, const std::vector<std::string>& archives);
bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);
int cmdGmTrim(const std::string& inPath, const std::string& outPath, int rate);  // tools/gm_trim.cpp
int cmdUiTest(const std::string& gameDir, const std::vector<std::string>& archives);
int cmdUiScreens(const std::string& gameDir, const std::string& outDir, const std::vector<std::string>& archives);
int cmdMusic(const std::vector<std::string>& args);  // tools/music_tool.cpp
int cmdRace(const std::string& cityName, const std::string& modeName, int raceNo, bool pro, float seconds,
            const std::string& outPath, const std::vector<std::string>& archives);

namespace {

bool writeFile(const std::string& path, const Bytes& data) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return std::fclose(f) == 0 && ok;
}

bool readFile(const std::string& path, Bytes& out) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(size_t(n < 0 ? 0 : n));
    bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool mountAll(Vfs& vfs, const std::vector<std::string>& paths) {
    std::vector<std::string> names;
    for (const auto& p : paths) names.push_back(baseName(p));
    bool ok = true;
    for (size_t i : Vfs::mountOrder(names)) {
        std::string err;
        auto src = openFileSource(paths[i], &err);
        if (!src) {
            std::fprintf(stderr, "%s\n", err.c_str());
            ok = false;
            continue;
        }
        if (!vfs.mount(names[i], src)) ok = false;
    }
    return ok;
}

// ---------------------------------------------------------------------------------------------
// test

struct Tally {
    int ok = 0;
    int fail = 0;
    std::vector<std::string> failures;
    void good() { ++ok; }
    void bad(const std::string& what) {
        ++fail;
        failures.push_back(what);
    }
};

int cmdTest(const std::vector<std::string>& archives) {
    setLogEcho(false);
    int problems = 0;
    auto expect = [&](bool cond, const std::string& what) {
        std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
        if (!cond) ++problems;
    };

    Vfs vfs;
    std::printf("== Archives\n");
    if (!mountAll(vfs, archives)) {
        std::printf("cannot mount all archives\n");
        return 1;
    }
    for (const auto& a : vfs.archives())
        std::printf("  %-28s %6zu files  %8.1f MB\n", a->name().c_str(), a->entries().size(),
                    double(a->byteSize()) / 1048576.0);
    std::printf("  %zu distinct paths\n", vfs.fileCount());

    // Every archive entry must unpack to its stated size.
    {
        int entries = 0, bad = 0, packed = 0;
        for (const auto& a : vfs.archives()) {
            for (const auto& e : a->entries()) {
                if (Vfs::isAudioArchive(a->name()) || !e.usable()) continue;
                ++entries;
                if (e.packed != e.size) ++packed;
                Bytes b;
                std::string err;
                if (!a->read(e, b, &err)) {
                    ++bad;
                    std::printf("  unreadable: %s (%s)\n", e.name.c_str(), err.c_str());
                }
            }
        }
        std::printf("  %d entries read (%d deflated), %d unreadable\n", entries, packed, bad);
        expect(bad == 0, "every archive entry unpacks to its stated size");
    }

    // Packages.
    std::printf("== Packages (geometry/*.pkg)\n");
    std::map<std::string, std::shared_ptr<Pkg>> pkgs;
    Tally pk;
    int pkg2 = 0, meshes = 0, batches = 0, xrefPkgs = 0, compact = 0;
    size_t verts = 0, tris = 0;
    std::set<std::string> texNames;  // lower-case
    std::map<std::string, std::string> texSpelling;
    for (const auto& path : vfs.listUnique("geometry/")) {
        if (!iendsWith(path, ".pkg")) continue;
        BytesPtr b = vfs.open(path);
        auto pkg = std::make_shared<Pkg>();
        std::string err;
        if (!b || !parsePkg(b->data(), b->size(), *pkg, err)) {
            pk.bad(path + ": " + (b ? err : "unreadable"));
            continue;
        }
        pk.good();
        if (pkg->version == 2) ++pkg2;
        if (!pkg->xrefs.empty()) ++xrefPkgs;
        if (pkg->compactMaterials) ++compact;
        for (const auto& w : pkg->warnings) std::printf("  warning %s: %s\n", path.c_str(), w.c_str());
        for (const auto& m : pkg->meshes) {
            ++meshes;
            batches += int(m.batches.size());
            verts += m.vertexCount();
            tris += m.triangleCount();
        }
        for (const auto& mat : pkg->materials)
            if (!mat.texture.empty()) {
                std::string k = toLower(mat.texture);
                texNames.insert(k);
                texSpelling.emplace(k, mat.texture);
            }
        pkgs[toLower(stem(path))] = pkg;
    }
    std::printf("  %d parsed (%d PKG2, %d with compact materials, %d with xrefs), %d failed\n", pk.ok,
                pkg2, compact, xrefPkgs, pk.fail);
    std::printf("  %d meshes, %d batches, %zu vertices, %zu triangles\n", meshes, batches, verts, tris);
    for (const auto& f : pk.failures) std::printf("  failed: %s\n", f.c_str());
    {
        // Known to be broken in the shipped archive (spec/formats/pkg.md).
        const std::set<std::string> known = {"geometry/wf_hse_baywin03_stone_wht_5s_12_l.pkg",
                                             "geometry/ww_bldg1_stone_tan_t6_20_l.pkg",
                                             "geometry/thing.pkg", "geometry/vpvw_dune.pkg"};
        int unexpected = 0;
        for (const auto& f : pk.failures)
            if (!known.count(toLower(f.substr(0, f.find(':'))))) ++unexpected;
        expect(unexpected == 0, strf("no package failures beyond the 4 known broken ones (%d unexpected)", unexpected));
    }

    // Pivots.
    std::printf("== Pivots (geometry/*.mtx)\n");
    Tally mx;
    for (const auto& path : vfs.listUnique("geometry/")) {
        if (!iendsWith(path, ".mtx")) continue;
        BytesPtr b = vfs.open(path);
        Pivot p;
        std::string err;
        if (!b || !parseMtx(b->data(), b->size(), p, err)) {
            mx.bad(path + ": " + err);
            continue;
        }
        if (b->size() != 48) std::printf("  note: %s is %zu bytes\n", path.c_str(), b->size());
        mx.good();
    }
    std::printf("  %d parsed, %d failed\n", mx.ok, mx.fail);
    for (const auto& f : mx.failures) std::printf("  failed: %s\n", f.c_str());
    expect(mx.fail == 0, "every pivot file parses");

    // Texture files.
    std::printf("== Texture files (texture/)\n");
    Tally tx;
    std::map<int, int> texTypes;
    int tga = 0, tex = 0, bmp = 0, tgaAlpha = 0;
    for (const auto& path : vfs.listUnique("texture/")) {
        bool isTex = iendsWith(path, ".tex"), isTga = iendsWith(path, ".tga"), isBmp = iendsWith(path, ".bmp");
        if (!isTex && !isTga && !isBmp) continue;
        BytesPtr b = vfs.open(path);
        Image img;
        std::string err;
        if (!b || !decodeImage(path, b->data(), b->size(), img, err)) {
            tx.bad(path + ": " + err);
            continue;
        }
        tx.good();
        if (isTex) { ++tex; ++texTypes[img.texType]; }
        if (isTga) { ++tga; if (img.hasAlpha) ++tgaAlpha; }
        if (isBmp) ++bmp;
    }
    std::printf("  %d decoded (%d .tex, %d .tga [%d with alpha], %d .bmp), %d refused\n", tx.ok, tex,
                tga, tgaAlpha, bmp, tx.fail);
    std::string types;
    for (auto& kv : texTypes) types += strf(" type %d = %d;", kv.first, kv.second);
    std::printf("  .tex types:%s\n", types.c_str());
    for (const auto& f : tx.failures) std::printf("  refused: %s\n", f.c_str());
    {
        const std::set<std::string> known = {"texture/nl01_coit_top.tex",
                                             "texture/sf_wall_brick03_blkfence_2_l.tex"};
        int unexpected = 0;
        for (const auto& f : tx.failures)
            if (!known.count(toLower(f.substr(0, f.find(':'))))) ++unexpected;
        expect(unexpected == 0, strf("no texture refusals beyond the 2 known non-power-of-two ones (%d unexpected)", unexpected));
    }

    // Texture names used by materials.
    std::printf("== Material texture names\n");
    TextureLibrary lib(vfs);
    int foundTex = 0, foundTga = 0, foundBmp = 0, animated = 0;
    std::vector<std::string> missing;
    for (const auto& k : texNames) {
        auto a = lib.get(texSpelling[k]);
        if (!a) { missing.push_back(texSpelling[k]); continue; }
        if (a->animated) { ++animated; continue; }
        std::string f = a->first().format;
        if (f == "tex") ++foundTex;
        else if (f == "tga") ++foundTga;
        else ++foundBmp;
    }
    std::printf("  %zu distinct names: %d .tex, %d .tga, %d .bmp, %d animated, %zu not found\n",
                texNames.size(), foundTex, foundTga, foundBmp, animated, missing.size());
    for (const auto& k : texNames) {
        auto a = lib.get(texSpelling[k]);
        if (a && a->animated)
            std::printf("  animated: %s (%zu frames, %.0f fps)\n", texSpelling[k].c_str(), a->frames.size(),
                        double(a->rate));
    }
    std::string miss;
    for (const auto& m : missing) miss += " " + m;
    std::printf("  not found:%s\n", miss.c_str());
    lib.clear();

    // Cars.
    std::printf("== Car list\n");
    std::vector<CarInfo> cars = buildCarList(vfs);
    AssetCache assets(vfs);
    int carsOk = 0, carsBad = 0, builtIns = 0, modCars = 0, totalSets = 0, missingCarTex = 0;
    std::set<std::string> listed;
    for (const auto& car : cars) {
        listed.insert(toLower(car.baseName));
        if (car.builtIn) ++builtIns;
        if (car.fromMod) ++modCars;
        VehicleModel v;
        bool ok = loadVehicle(assets, car.baseName, VehicleKind::Player, v);
        if (!ok) {
            ++carsBad;
            std::printf("  %-14s FAILED: %s\n", car.baseName.c_str(),
                        v.problems.empty() ? "?" : v.problems.front().c_str());
            continue;
        }
        ++carsOk;
        totalSets += v.setCount();
        // Every texture of every paint set (undamaged rule), all levels of detail.
        std::set<std::string> carMissing;
        int texCount = 0;
        for (int c = 0; c < v.setCount(); ++c) {
            for (int l = 0; l < LodCount; ++l) {
                StillCarOptions so;
                so.lights = true;
                for (const DrawPart& d : buildStillCar(v, Lod(l), c, so)) {
                    for (const auto& b : d.mesh->batches) {
                        const Material* mat = d.pkg->material(d.materialSet, b.material);
                        if (!mat || mat->texture.empty()) continue;
                        std::string name = mat->texture;
                        if (d.flags & DrawDamageSlots)
                            name = undamagedTextureName(name, [&](const std::string& n) { return lib.exists(n); });
                        ++texCount;
                        if (!lib.exists(name)) carMissing.insert(name);
                    }
                }
            }
        }
        missingCarTex += int(carMissing.size());
        int wheels = 0;
        for (int n = 0; n < 6; ++n) {
            const PartLods* p = v.part(strf("WHL%d", n));
            if (p && p->any()) ++wheels;
        }
        std::string colors;
        for (size_t i = 0; i < car.colors.size(); ++i) colors += (i ? "|" : "") + car.colors[i];
        std::printf("  %-14s %-26s sets %2d  colours %zu  wheels %d  ground %+.3f%s%s%s\n",
                    car.baseName.c_str(), car.description.c_str(), v.setCount(), car.colors.size(), wheels,
                    double(v.groundY), v.trailer ? strf("  trailer at (%.3f, %.3f, %.3f)", double(v.trailerOrigin.x),
                                                        double(v.trailerOrigin.y), double(v.trailerOrigin.z)).c_str() : "",
                    car.fromMod ? ("  [mod: " + car.archive + "]").c_str() : "",
                    carMissing.empty() ? "" : strf("  %zu textures missing", carMissing.size()).c_str());
        for (const auto& m : carMissing) std::printf("      missing texture: %s\n", m.c_str());
        for (const auto& p : v.problems) std::printf("      note: %s\n", p.c_str());
        (void)texCount;
    }
    std::printf("  %zu cars listed (%d built-in, %d from mods): %d load, %d fail; %d paint sets\n",
                cars.size(), builtIns, modCars, carsOk, carsBad, totalSets);
    expect(builtIns == 20, "all 20 built-in cars are listed");
    expect(carsBad == 0, "every listed car loads with a body");

    // Other vehicle packages.
    std::printf("== Other vehicle packages\n");
    int otherOk = 0, otherBad = 0, traffic = 0;
    for (const auto& kv : pkgs) {
        const std::string& n = kv.first;
        bool isVp = startsWith(n, "vp") && !iendsWith(n, "_dash") && !iendsWith(n, "_trailer");
        bool isVa = startsWith(n, "va_");
        if ((!isVp && !isVa) || listed.count(n)) continue;
        if (isVa && !kv.second->findMesh("BODY_H") && !kv.second->findMesh("BODY_M") &&
            !kv.second->findMesh("BODY_L") && !kv.second->findMesh("BODY_VL")) {
            std::printf("  %-22s not a vehicle (no BODY; a city object with a va_ name)\n", n.c_str());
            continue;
        }
        VehicleModel v;
        bool ok = loadVehicle(assets, n, isVa ? VehicleKind::Traffic : VehicleKind::Player, v, false);
        if (isVa) ++traffic;
        if (ok) ++otherOk; else ++otherBad;
        if (!isVa || !ok)
            std::printf("  %-22s %s sets %d ground %+.3f%s\n", n.c_str(), ok ? "ok  " : "FAIL", v.setCount(),
                        double(v.groundY), v.problems.empty() ? "" : ("  (" + v.problems.front() + ")").c_str());
    }
    std::printf("  %d load (%d traffic va_*), %d fail\n", otherOk, traffic, otherBad);

    // Cities.
    std::printf("== Cities (city/*.psdl, *.inst)\n");
    {
        // Wall light table (psdl.md, "Wall light table", example sf.lt04).
        {
            BytesPtr lt = vfs.open("city/sf.lt04");
            CityLights L;
            if (lt && parseLightFile(std::string(lt->begin(), lt->end()), L)) {
                uint32_t t[64];
                buildWallLightTable(L, t);
                auto rgb = [](uint32_t c) { return strf("(%u, %u, %u)", (c >> 16) & 255u, (c >> 8) & 255u, c & 255u); };
                int dk = 0;
                auto sum = [](uint32_t c) { return ((c >> 16) & 255u) + ((c >> 8) & 255u) + (c & 255u); };
                for (int k = 1; k < 64; ++k)
                    if (sum(t[k]) < sum(t[dk])) dk = k;
                std::printf("  sf.lt04 wall lights: k0 %s k6 %s k22 %s k38 %s k54 %s darkest %s ambient %s\n", rgb(t[0]).c_str(),
                            rgb(t[6]).c_str(), rgb(t[22]).c_str(), rgb(t[38]).c_str(), rgb(t[54]).c_str(), rgb(t[dk]).c_str(),
                            rgb(L.ambient).c_str());
                expect(t[0] == 0xFF606081u && t[6] == 0xFF64646Eu && t[22] == 0xFFFFFFFFu && t[38] == 0xFF777388u &&
                           t[54] == 0xFF9D9DD6u && t[dk] == 0xFF49495Du && (L.ambient & 0xFFFFFFu) == 0x282832u,
                       "sf.lt04 wall light table as in psdl.md (k0, k6, k22, k38, k54, darkest, ambient)");
            } else {
                expect(false, "city/sf.lt04 reads");
            }
        }
        const std::map<std::string, size_t> expectTris = {
            {"london", 97628}, {"sf", 93117}, {"sfai", 63970}, {"variant", 25618}, {"city", 449}};
        for (const auto& path : vfs.listUnique("city/")) {
            if (!iendsWith(path, ".psdl") || path.find('/', 5) != std::string::npos) continue;
            std::string name = stem(path);
            BytesPtr b = vfs.open(path);
            Psdl psdl;
            std::string err;
            if (!b || !parsePsdl(b->data(), b->size(), psdl, err)) {
                expect(false, path + " parses (" + err + ")");
                continue;
            }
            PsdlBuildOptions bo;
            CityLights lights;
            BytesPtr lt = vfs.open("city/" + name + ".lt04");
            if (lt && parseLightFile(std::string(lt->begin(), lt->end()), lights)) buildWallLightTable(lights, bo.wallLights);
            PsdlGeometry g = buildPsdlGeometry(psdl, bo);
            size_t blocks = 0;
            for (size_t r = 1; r < psdl.rooms.size(); ++r)
                if (psdl.rooms[r].flags & PsdlBlock) ++blocks;
            std::printf("  %-10s %6zu points %5zu heights %4zu slots %5zu rooms (%zu blocks) %4zu AI roads -> %zu triangles"
                        " (%zu inverted walls skipped)\n",
                        name.c_str(), psdl.points.size(), psdl.heights.size(), psdl.textures.size(), psdl.rooms.size(),
                        blocks, psdl.aiRoads.size(), g.triangles, g.skippedInvertedWalls);
            std::string counts;
            for (int k = 0; k < 13; ++k) counts += strf(" %zu", g.elementCounts[k]);
            std::printf("             elements by type:%s\n", counts.c_str());
            for (size_t k = 0; k < g.problems.size() && k < 5; ++k) std::printf("             problem: %s\n", g.problems[k].c_str());
            auto it = expectTris.find(toLower(name));
            if (it != expectTris.end())
                std::printf("             spec says %zu triangles (difference %+lld)\n", it->second,
                            (long long)g.triangles - (long long)it->second);
            expect(g.problems.empty(), name + ".psdl builds without problems");
            // Texture slots resolve.
            int missingSlots = 0;
            for (size_t sl = 1; sl < psdl.textures.size(); ++sl)
                if (!psdl.textures[sl].empty() && !lib.exists(Psdl::lookupName(psdl.textures[sl]))) ++missingSlots;
            std::printf("             %d texture slots not found\n", missingSlots);
        }
        for (const char* city : {"london", "sf"}) {
            for (const std::string& suffix : {std::string(".inst"), std::string("_ai.inst")}) {
                std::string path = std::string("city/") + city + suffix;
                BytesPtr b = vfs.open(path);
                std::vector<InstRecord> recs;
                std::string err;
                if (!b || !parseInst(b->data(), b->size(), recs, err)) {
                    expect(false, path + " parses (" + err + ")");
                    continue;
                }
                size_t landmarks = 0, breakables = 0, spans = 0, compactN = 0, missingPkg = 0;
                std::set<std::string> names;
                for (const auto& r : recs) {
                    landmarks += r.landmark;
                    breakables += r.breakable;
                    spans += r.spansRooms;
                    compactN += r.compact;
                    names.insert(toLower(r.name));
                }
                for (const auto& n : names)
                    if (!vfs.exists("geometry/" + n + ".pkg")) ++missingPkg;
                std::printf("  %-18s %5zu records: %zu landmarks, %zu breakable, %zu span rooms, %zu compact; %zu names, %zu without a package\n",
                            path.c_str(), recs.size(), landmarks, breakables, spans, compactN, names.size(), missingPkg);
                expect(missingPkg == 0, path + ": every object has a package");
            }
        }
    }

    // Archive priority (ar.md "Several archives"): sorted by upper-cased name, the first wins.
    std::printf("== Archive priority\n");
    {
        std::string text = "BaseName=vpbug\r\nDescription=Override Test\r\nColors=Only\r\nFlags=0\r\nOrder=-1\r\n"
                           "ScoringBias=20\r\nUnlockScore=0\r\nUnlockFlags=0\r\nHorsepower=1\r\nTop Speed=1\r\n"
                           "Durability=1\r\nMass=1\r\n";
        std::vector<ArWriteFile> files;
        files.push_back({"TUNE/VPBUG.INFO", Bytes(text.begin(), text.end())});
        Bytes ar = buildArchive(files, ArWriteOptions{true, true, true});
        auto descOf = [&](Vfs& v) {
            for (const auto& c : buildCarList(v))
                if (c.baseName == "vpbug") return c.description;
            return std::string();
        };
        {
            // A name that sorts before MM2CORE replaces the base file.
            Vfs v;
            std::vector<std::string> paths = archives;
            std::vector<std::string> names;
            for (const auto& p : paths) names.push_back(baseName(p));
            names.push_back("AAA_override.ar");
            for (size_t i : Vfs::mountOrder(names)) {
                if (i == names.size() - 1) v.mount(names[i], std::make_shared<MemorySource>(ar));
                else v.mount(names[i], openFileSource(paths[i], nullptr));
            }
            expect(v.archives().front()->name() == "AAA_override.ar", "AAA_override.ar sorts first");
            expect(descOf(v) == "Override Test", "AAA_override.ar's TUNE/VPBUG.INFO replaces tune/vpbug.info");
            expect(v.exists("Geometry/VPBUG.PKG"), "lookups ignore case");
            expect(buildCarList(v).size() == cars.size(), "the overridden car is listed once (same BaseName)");
        }
        {
            // A name that sorts after MM2TEX loses to the base archives.
            Vfs v;
            mountAll(v, archives);
            v.mount("zz_override.ar", std::make_shared<MemorySource>(ar));
            expect(descOf(v) == "VW New Beetle", "zz_override.ar does not replace the base tune/vpbug.info");
        }
        {
            // A directory out of MM2's order hides entries from the binary search.
            std::vector<ArWriteFile> f2;
            f2.push_back({"b.txt", Bytes{'b'}});
            f2.push_back({"a.txt", Bytes{'a'}});
            Bytes bad = buildArchive(f2, ArWriteOptions{false, false, false});
            Bytes good = buildArchive(f2, ArWriteOptions{false, false, true});
            Vfs v1, v2;
            v1.mount("unsorted.ar", std::make_shared<MemorySource>(bad));
            v2.mount("sorted.ar", std::make_shared<MemorySource>(good));
            expect(!v1.exists("a.txt") && v1.exists("b.txt"), "an unsorted archive hides entries (as in MM2)");
            expect(v2.exists("a.txt") && v2.exists("B.TXT"), "the packer writes MM2's order");
        }
    }

    // Free-roam start points (controls.md, "Free-roam start").
    std::printf("== Free-roam start points\n");
    for (const char* city : {"london", "sf"}) {
        CityData cd;
        std::string msg;
        AssetCache a2(vfs);
        if (!cd.load(city, vfs, a2, msg)) {
            expect(false, std::string(city) + " city data loads (" + msg + ")");
            continue;
        }
        const auto& st = cd.starts();
        std::printf("  %-7s %d intersections, %zu eligible", city, cd.intersectionCount(), st.size());
        if (!st.empty())
            std::printf("; lowest: %d at (%.1f, %.1f, %.1f) room %d", st[0].intersection, double(st[0].pos.x),
                        double(st[0].pos.y), double(st[0].pos.z), st[0].room);
        std::printf("\n");
        if (std::string(city) == "london") {
            expect(cd.intersectionCount() == 328 && st.size() == 262, "London: 328 intersections, 262 eligible");
            expect(!st.empty() && st[0].intersection == 10 && st[0].room == 1023, "London: first eligible is 10 in room 1023");
        } else {
            expect(cd.intersectionCount() == 214 && st.size() == 178, "SF: 214 intersections, 178 eligible");
            expect(!st.empty() && st[0].intersection == 2 && st[0].room == 963, "SF: first eligible is 2 in room 963");
        }
    }

    testAiNetwork(vfs, expect);
    testEffects(vfs, expect);
    testPedestrians(vfs, expect);
    testRaces(vfs, expect);

    // Collision (bnd.md, city-collision.md, collision-objects.md).
    std::printf("== Collision bounds and city collision\n");
    {
        phys::MaterialTable table;
        int bndOk = 0, bndBad = 0, bbOk = 0, bbBad = 0, terOk = 0, terBad = 0, polys = 0;
        for (const auto& path : vfs.listUnique("bound/")) {
            const bool isBnd = iendsWith(path, ".bnd"), isBb = iendsWith(path, "_BOUND.bbnd"),
                       isTer = iendsWith(path, "_BOUND.ter");
            if (!isBnd && !isBb && !isTer) continue;
            BytesPtr b = vfs.open(path);
            phys::Bound bound;
            std::string err;
            if (isBnd) {
                if (b && phys::parseBndText(std::string(b->begin(), b->end()), bound, table, err)) {
                    ++bndOk;
                    polys += int(bound.polys.size());
                } else {
                    ++bndBad;
                    std::printf("  bad %s: %s\n", path.c_str(), err.c_str());
                }
            } else if (isBb) {
                if (b && phys::parseBbnd(b->data(), b->size(), bound, table, err)) ++bbOk;
                else {
                    ++bbBad;
                    std::printf("  bad %s: %s\n", path.c_str(), err.c_str());
                }
            } else {
                std::string name = baseName(path);
                name = name.substr(0, name.size() - std::string("_BOUND.ter").size());
                if (phys::loadTerrainBound(vfs, name, table)) ++terOk;
                else ++terBad;
            }
        }
        std::printf("  .bnd %d (%d polygons), .bbnd %d, .ter %d parsed; %d/%d/%d failed\n", bndOk, polys, bbOk, terOk,
                    bndBad, bbBad, terBad);
        expect(bndBad == 0 && bbBad == 0 && terBad == 0, "every bound file parses");
        expect(bndOk == 525 && bbOk == 324 && terOk == 184 && polys == 13217,
               "525 .bnd (13,217 polygons), 324 .bbnd, 184 .ter as in bnd.md");
        auto bug = phys::loadGeometryBound(vfs, "vpbug", table);
        if (bug) {
            int quads = 0;
            for (const auto& p : bug->polys) quads += p.n == 4;
            std::printf("  vpbug_BOUND: %zu points, %d quads, %zu edges, box (%.3f %.3f %.3f)..(%.3f %.3f %.3f), radius %.3f\n",
                        bug->points.size(), quads, bug->edges.size(), double(bug->bmin.x), double(bug->bmin.y),
                        double(bug->bmin.z), double(bug->bmax.x), double(bug->bmax.y), double(bug->bmax.z),
                        double(bug->radius));
            expect(bug->points.size() == 16 && quads == 14 && bug->edges.size() == 28, "vpbug bound: 16 points, 14 quads, 28 edges");
        }
        for (const char* city : {"london", "sf"}) {
            AssetCache a3(vfs);
            CityData cd;
            std::string msg;
            if (!cd.load(city, vfs, a3, msg)) continue;
            phys::CollisionWorld cw;
            cw.setupCity(vfs, cd);
            int lmG = 0, lmT = 0, span = 0, xref = 0, caps = 0, props = 0, poles = 0, generated = 0;
            for (const auto& o : cw.objects()) {
                if (o.kind == phys::ObjKind::Landmark) (o.bound->terrain ? lmT : lmG)++;
                else if (o.kind == phys::ObjKind::Spanning) ++span;
                else {
                    const CityObject& co = cd.objects()[size_t(o.cityObject)];
                    if (co.origin == 3) continue;  // parked cars (level-objects.md)
                    if (co.origin != 0) {
                        ++generated;  // street props and props.pathset props
                        continue;
                    }
                    ++props;
                    if (co.fromXref) ++xref;
                    if (co.streetProp) ++poles;
                    else if (o.prim == phys::PropPrim::Capsule && !co.fromXref) ++caps;
                }
            }
            const bool isLondon = std::string(city) == "london";
            {
                std::map<std::string, int> cat;
                int perType[16] = {};
                const auto& ccol = cw.cityCollision();
                for (int r = 1; r < ccol.roomCount(); ++r)
                    for (const auto& p : ccol.room(r).polys) {
                        cat[strf("type %2d %s", p.elemType, p.category == 2 ? "median wall" : p.category ? "wall" : "surface")]++;
                        if (p.elemType >= 0 && p.elemType < 16) ++perType[p.elemType];
                    }
                std::string line;
                for (auto& kv : cat) line += strf("  [%s] %d", kv.first.c_str(), kv.second);
                std::printf("  %s:%s\n", city, line.c_str());
                const auto& st = ccol.stats();
                std::printf("  %s: %d dropped (zero normal), %d strip quads made triangles by the index-0 rule, %d triangle-mode rooms with %d polygons\n",
                            city, st.dropped, st.fourthIndex, st.triModeRooms, st.triModePolys);
                // city-collision.md, "Checked against": polygons per element type, drops, triangle mode.
                const int types[10] = {0, 1, 2, 4, 5, 6, 7, 8, 9, 12};
                const int wantL[10] = {2863, 10135, 212, 697, 3879, 3877, 9618, 330, 239, 9619};
                const int wantS[10] = {4356, 9931, 68, 648, 3217, 5115, 12537, 1190, 754, 11699};
                bool typesOk = true;
                for (int k = 0; k < 10; ++k)
                    if (perType[types[k]] != (isLondon ? wantL[k] : wantS[k])) typesOk = false;
                expect(typesOk, std::string(city) + " collision polygons per element type as in city-collision.md");
                expect(st.dropped == (isLondon ? 61 : 326) && st.triModePolys == (isLondon ? 197 : 2125),
                       std::string(city) + " zero-normal drops and triangle-mode polygons as in city-collision.md");
            }
            std::printf("  %-6s %zu collision polygons; landmarks %d geometry + %d terrain, %d spanning, %d props (%d xref, %d .inst capsules, %d light poles)\n",
                        city, cw.cityPolygonCount(), lmG, lmT, span, props, xref, caps, poles);
            expect(cw.cityPolygonCount() == (isLondon ? 41469u : 49515u),
                   std::string(city) + (isLondon ? " 41,469" : " 49,515") + " collision polygons (city-collision.md)");
            expect(generated == (isLondon ? 4835 + 1033 : 5349 + 778),
                   strf("%s: %d street and path-set props collide (street-props.md, pathset.md)", city, generated));
            if (isLondon) {
                expect(lmG == 118 && lmT == 58 && span == 13 && xref == 65 && caps == 67 && poles == 519,
                       "London objects: 118 + 58 landmarks, 13 spanning, 65 xref props, 67 stop signs, 519 light poles");
            } else {
                expect(lmG == 3 && lmT == 85 && span == 2 && xref == 368 && caps == 40 && poles == 533,
                       "SF objects: 3 + 85 landmarks, 2 spanning, 368 xref props, 40 capsule props, 533 light poles");
            }
        }
    }

    // Mod archives.
    int mods = 0;
    for (const auto& a : vfs.archives())
        if (!Vfs::isBaseArchive(a->name())) ++mods;
    if (mods) {
        std::printf("== Mod archives\n");
        for (const auto& a : vfs.archives()) {
            if (Vfs::isBaseArchive(a->name())) continue;
            int cnt = 0;
            for (const auto& car : cars)
                if (car.archive == a->name()) {
                    ++cnt;
                    std::printf("  %s adds car %s (%s)\n", a->name().c_str(), car.baseName.c_str(),
                                car.description.c_str());
                }
            expect(cnt > 0, a->name() + " adds at least one car to the list");
        }
    }

    testAudio(vfs, expect);

    std::printf("== Summary: %d file problems logged, %d check(s) failed\n", fileProblemCount(), problems);
    std::printf(problems ? "TEST FAILED\n" : "TEST PASSED\n");
    return problems ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------

int cmdList(const std::string& path) {
    std::string err;
    auto src = openFileSource(path, &err);
    Archive a;
    if (!src || !a.open(src, baseName(path), &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    for (const auto& e : a.entries())
        if (e.usable()) std::printf("%10u %10u %s\n", e.size, e.packed, e.name.c_str());
    return 0;
}

int cmdCat(const std::string& path, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    mountAll(vfs, archives);
    BytesPtr b = vfs.open(path);
    if (!b) {
        std::fprintf(stderr, "not found: %s\n", path.c_str());
        return 1;
    }
    std::fwrite(b->data(), 1, b->size(), stdout);
    return 0;
}

void collectFiles(const std::string& root, const std::string& rel, std::vector<ArWriteFile>& out) {
    std::string dirPath = rel.empty() ? root : root + "/" + rel;
    DIR* d = opendir(dirPath.c_str());
    if (!d) return;
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        names.push_back(n);
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
        std::string r = rel.empty() ? n : rel + "/" + n;
        std::string full = root + "/" + r;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            collectFiles(root, r, out);
        } else if (S_ISREG(st.st_mode)) {
            ArWriteFile f;
            f.name = r;
            if (readFile(full, f.data)) out.push_back(std::move(f));
        }
    }
}

int cmdPack(bool deflate, const std::string& out, const std::string& dir) {
    std::vector<ArWriteFile> files;
    collectFiles(dir, "", files);
    if (files.empty()) {
        std::fprintf(stderr, "no files under %s\n", dir.c_str());
        return 1;
    }
    size_t n = files.size();
    Bytes ar = buildArchive(std::move(files), ArWriteOptions{deflate, true});
    if (!writeFile(out, ar)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("packed %zu files into %s (%zu bytes)\n", n, out.c_str(), ar.size());
    return 0;
}

int cmdMkCarMod(bool deflate, const std::string& out, const std::string& src, const std::string& dst,
                const std::string& desc, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    if (!mountAll(vfs, archives)) return 1;
    std::vector<ArWriteFile> files;
    // Geometry: <src>.pkg and every <src>_*.pkg / <src>_*.mtx, renamed.
    const std::string lsrc = toLower(src);
    for (const auto& path : vfs.listUnique("geometry/")) {
        std::string file = baseName(path);
        std::string lf = toLower(file);
        bool match = lf == lsrc + ".pkg" ||
                     (startsWith(lf, lsrc + "_") && (endsWith(lf, ".pkg") || endsWith(lf, ".mtx")));
        if (!match) continue;
        BytesPtr b = vfs.open(path);
        if (!b) continue;
        files.push_back({"geometry/" + dst + file.substr(src.size()), *b});
    }
    // tune/<src>.info with BaseName and Description replaced.
    BytesPtr info = vfs.open("tune/" + src + ".info");
    if (!info) {
        std::fprintf(stderr, "tune/%s.info not found\n", src.c_str());
        return 1;
    }
    std::string text(info->begin(), info->end()), outText;
    for (std::string line : split(text, '\n')) {
        std::string t = trim(line);
        if (istartsWith(t, "BaseName=")) line = "BaseName=" + dst + "\r";
        else if (istartsWith(t, "Description=")) line = "Description=" + desc + "\r";
        outText += line + "\n";
    }
    while (endsWith(outText, "\n\n")) outText.pop_back();
    files.push_back({"tune/" + dst + ".info", Bytes(outText.begin(), outText.end())});
    for (const auto& f : files) std::printf("  %-40s %8zu bytes\n", f.name.c_str(), f.data.size());
    size_t n = files.size();
    Bytes ar = buildArchive(std::move(files), ArWriteOptions{deflate, true});
    if (!writeFile(out, ar)) {
        std::fprintf(stderr, "cannot write %s\n", out.c_str());
        return 1;
    }
    std::printf("wrote %s: %zu files, %zu bytes\n", out.c_str(), n, ar.size());
    return 0;
}

int cmdPkg(const std::string& name, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    mountAll(vfs, archives);
    // "<pkg>:<MESH>" prints that mesh's vertex positions.
    std::string base = name, only;
    if (const size_t colon = name.find(':'); colon != std::string::npos) {
        base = name.substr(0, colon);
        only = name.substr(colon + 1);
    }
    std::string path = iendsWith(base, ".pkg") ? base : "geometry/" + base + ".pkg";
    BytesPtr b = vfs.open(path);
    if (!b) { std::fprintf(stderr, "not found: %s\n", path.c_str()); return 1; }
    Pkg pkg;
    std::string err;
    if (!parsePkg(b->data(), b->size(), pkg, err)) { std::fprintf(stderr, "broken: %s\n", err.c_str()); return 1; }
    if (!only.empty()) {
        for (const auto& e : pkg.entries) {
            if (e.mesh < 0 || e.name != only) continue;
            const Mesh& m = pkg.meshes[size_t(e.mesh)];
            for (const auto& bt : m.batches) {
                std::printf("batch material %d\n", bt.material);
                for (const auto& v : bt.vertices) std::printf("  %.3f %.3f %.3f\n", double(v.pos.x), double(v.pos.y), double(v.pos.z));
            }
        }
        return 0;
    }
    std::printf("%s: PKG%d, %zu entries, %d sets x %d materials (%s)\n", path.c_str(), pkg.version,
                pkg.entries.size(), pkg.setCount, pkg.materialsPerSet, pkg.compactMaterials ? "compact" : "full");
    for (const auto& e : pkg.entries) {
        if (e.mesh < 0) { std::printf("  %s\n", e.name.c_str()); continue; }
        const Mesh& m = pkg.meshes[size_t(e.mesh)];
        std::string mats;
        for (const auto& bt : m.batches) mats += strf(" %d", bt.material);
        std::printf("  %-16s fvf 0x%03x %2zu batches %5zu verts  box (%.2f %.2f %.2f)-(%.2f %.2f %.2f) mats:%s\n",
                    e.name.c_str(), m.fvf, m.batches.size(), m.vertexCount(), double(m.boundsMin.x),
                    double(m.boundsMin.y), double(m.boundsMin.z), double(m.boundsMax.x), double(m.boundsMax.y),
                    double(m.boundsMax.z), mats.c_str());
    }
    for (int s = 0; s < pkg.setCount; ++s) {
        std::printf(" set %d\n", s);
        for (int i = 0; i < pkg.materialsPerSet; ++i) {
            const Material* m = pkg.material(s, i);
            std::printf("  %2d %-28s diff %.2f %.2f %.2f %.2f  emis %.2f %.2f %.2f  power %.2f\n", i,
                        m->texture.empty() ? "-" : m->texture.c_str(), double(m->diffuse.x), double(m->diffuse.y),
                        double(m->diffuse.z), double(m->diffuse.w), double(m->emissive.x), double(m->emissive.y),
                        double(m->emissive.z), double(m->power));
        }
        if (s >= 1 && pkg.setCount > 4) { std::printf("  ...\n"); break; }
    }
    for (const auto& x : pkg.xrefs) std::printf("  xref %s at (%.2f %.2f %.2f)\n", x.name.c_str(),
                                                double(x.matrix[9]), double(x.matrix[10]), double(x.matrix[11]));
    return 0;
}

int cmdInst(const std::string& path, const std::string& filter, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    mountAll(vfs, archives);
    BytesPtr b = vfs.open(path);
    std::vector<InstRecord> recs;
    std::string err;
    if (!b || !parseInst(b->data(), b->size(), recs, err)) { std::fprintf(stderr, "cannot read %s %s\n", path.c_str(), err.c_str()); return 1; }
    for (const auto& r : recs) {
        if (!filter.empty() && toLower(r.name).find(toLower(filter)) == std::string::npos) continue;
        std::printf("%-28s room %4u var %2u %s%s%s pos (%.1f, %.1f, %.1f) X (%.2f %.2f %.2f) Z (%.2f %.2f %.2f)\n", r.name.c_str(), r.room,
                    r.variant, r.landmark ? "L" : "-", r.breakable ? "B" : "-", r.spansRooms ? "S" : "-", double(r.matrix[9]),
                    double(r.matrix[10]), double(r.matrix[11]), double(r.matrix[0]), double(r.matrix[1]), double(r.matrix[2]),
                    double(r.matrix[6]), double(r.matrix[7]), double(r.matrix[8]));
    }
    return 0;
}

int cmdRoom(const std::string& city, int room, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    mountAll(vfs, archives);
    BytesPtr b = vfs.open("city/" + city + ".psdl");
    Psdl p;
    std::string err;
    if (!b || !parsePsdl(b->data(), b->size(), p, err)) { std::fprintf(stderr, "cannot read: %s\n", err.c_str()); return 1; }
    if (room == 0) {
        // Room 0: list the enclosed rooms (PSDL flag 0x02, tunnels) with their outline centre.
        for (size_t i = 1; i < p.rooms.size(); ++i) {
            const PsdlRoom& q = p.rooms[i];
            if (!(q.flags & 0x02) || q.perimeter.empty()) continue;
            vec3 c;
            for (uint16_t pi : q.perimeter) c += p.points[pi];
            c = c / float(q.perimeter.size());
            std::printf("room %zu flags 0x%02x centre (%.1f %.1f %.1f)\n", i, q.flags, double(c.x), double(c.y), double(c.z));
        }
        return 0;
    }
    if (room <= 0 || size_t(room) >= p.rooms.size()) { std::fprintf(stderr, "no room %d\n", room); return 1; }
    const PsdlRoom& r = p.rooms[size_t(room)];
    std::printf("room %d flags 0x%02x prop %u, %zu perimeter points, %zu words\n", room, r.flags, r.propRule, r.perimeter.size(), r.words.size());
    for (size_t i = 0; i < r.perimeter.size(); ++i) {
        const vec3& v = p.points[r.perimeter[i]];
        std::printf("  perimeter %zu: point %u (%.1f %.1f %.1f) -> room %u\n", i, r.perimeter[i], double(v.x), double(v.y), double(v.z), r.neighbours[i]);
    }
    size_t pos = 0;
    while (pos < r.words.size()) {
        uint16_t h = r.words[pos++];
        int s = h & 7, type = (h >> 3) & 15;
        std::string line = strf("  type %2d s %d", type, s);
        if (type == 10) {
            int t = s ? ((s << 8) | r.words[pos++]) : r.words[pos++];
            line += strf(" texture slot %d = %s (+1 %s, +2 %s)", t, t < int(p.textures.size()) ? p.textures[size_t(t)].c_str() : "?",
                         t + 1 < int(p.textures.size()) ? p.textures[size_t(t) + 1].c_str() : "?",
                         t + 2 < int(p.textures.size()) ? p.textures[size_t(t) + 2].c_str() : "?");
        } else {
            int n = s ? s : r.words[pos++];
            size_t len = 0;
            switch (type) {
                case 0: len = 4 * size_t(n); break;
                case 1: case 2: len = 2 * size_t(n); break;
                case 3: case 4: case 7: len = 4; break;
                case 5: case 6: case 12: len = size_t(n) + 2; break;
                case 8: len = 2 + 6 * size_t(n); break;
                case 9: len = size_t(n); break;
                case 11: len = 6; break;
            }
            line += strf(" n %d:", n);
            for (size_t k = 0; k < len && pos + k < r.words.size() && k < 24; ++k) line += strf(" %u", r.words[pos + k]);
            pos += len;
        }
        std::printf("%s%s\n", line.c_str(), (h & 0x80) ? "  [last]" : "");
        if (h & 0x80) break;
    }
    return 0;
}

int cmdDrive(std::string runPath, std::vector<std::string> archives) {
    // cctool drive [--trace] <file.run> <archives...>
    bool trace = false;
    if (runPath == "--trace" && !archives.empty()) {
        trace = true;
        runPath = archives.front();
        archives.erase(archives.begin());
    }
    Vfs vfs;
    setLogEcho(false);
    if (!mountAll(vfs, archives)) return 1;
    Bytes text;
    if (!readFile(runPath, text)) {
        std::fprintf(stderr, "cannot read %s\n", runPath.c_str());
        return 1;
    }
    phys::RunFile run;
    std::string err;
    if (!phys::parseRunFile(std::string(text.begin(), text.end()), run, err)) {
        std::fprintf(stderr, "%s: %s\n", runPath.c_str(), err.c_str());
        return 1;
    }
    {
        const size_t slash = runPath.find_last_of('/');
        run.runDir = slash == std::string::npos ? std::string(".") : runPath.substr(0, slash);
    }
    if (!phys::runDriveCheck(vfs, run, stdout, err, trace)) {
        std::fprintf(stderr, "%s: %s\n", runPath.c_str(), err.c_str());
        return 1;
    }
    return 0;
}

// Runs every *.run in a folder into <out>/<name>.csv.
int cmdDrives(const std::string& dir, const std::string& outDir, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    if (!mountAll(vfs, archives)) return 1;
    std::vector<ArWriteFile> runs;
    collectFiles(dir, "", runs);
    int ok = 0, bad = 0;
    for (const auto& f : runs) {
        if (!endsWith(f.name, ".run")) continue;
        phys::RunFile run;
        std::string err;
        std::string out = outDir + "/" + f.name.substr(0, f.name.size() - 4) + ".csv";
        if (!phys::parseRunFile(std::string(f.data.begin(), f.data.end()), run, err)) {
            std::fprintf(stderr, "%s: %s\n", f.name.c_str(), err.c_str());
            ++bad;
            continue;
        }
        {
            const size_t slash = f.name.find_last_of('/');
            run.runDir = slash == std::string::npos ? dir : dir + "/" + f.name.substr(0, slash);
        }
        std::FILE* fp = std::fopen(out.c_str(), "w");
        if (!fp) {
            std::fprintf(stderr, "cannot write %s\n", out.c_str());
            ++bad;
            continue;
        }
        bool done = phys::runDriveCheck(vfs, run, fp, err);
        std::fclose(fp);
        if (!done) {
            std::fprintf(stderr, "%s: %s\n", f.name.c_str(), err.c_str());
            ++bad;
            continue;
        }
        ++ok;
    }
    std::printf("%d runs written to %s, %d failed\n", ok, outDir.c_str(), bad);
    return bad ? 1 : 0;
}

// trailer.md 2: the set-up of a car's trailer, as the physics makes it (player's car, flat world).
int cmdTrailer(const std::string& car, const std::vector<std::string>& archives) {
    Vfs vfs;
    setLogEcho(false);
    if (!mountAll(vfs, archives)) return 1;
    phys::CarTune tune = phys::loadCarTune(vfs, car, true);
    phys::TrailerTune tt;
    std::string why;
    if (!phys::loadTrailerTune(vfs, tune, tt, why)) {
        std::printf("%s: no trailer (%s)\n", car.c_str(), why.c_str());
        return 0;
    }
    phys::CollisionWorld world;
    world.setupFlat(vfs, 0.0f);
    phys::Car truck;
    truck.setup(tune);
    float before[4];
    for (int i = 0; i < 4; ++i) before[i] = truck.wheel(i).L0;
    auto t = std::make_unique<phys::Trailer>();
    t->setup(tt, truck, &world, false);
    phys::Trailer& tr = *t;
    truck.attachTrailer(std::move(t));
    const phys::RigidBody& b = tr.body();
    std::printf("%s: tune files %s / %s (%s)\n", tt.name.c_str(), tt.vehFile ? "vehTrailer" : "-",
                tt.jointFile ? "dgTrailerJoint" : "-", tt.vehFile && tt.jointFile ? "loaded" : "not loaded");
    std::printf("  mass %g  box (%g, %g, %g)  I (%g, %g, %g)\n", double(b.m), double(tt.inertiaBox.x),
                double(tt.inertiaBox.y), double(tt.inertiaBox.z), double(b.I.x), double(b.I.y), double(b.I.z));
    std::printf("  CarHitchOffset (%g, %g, %g)  TrailerHitchOffset (%g, %g, %g)\n", double(tt.carHitch.x),
                double(tt.carHitch.y), double(tt.carHitch.z), double(tt.trailerHitch.x), double(tt.trailerHitch.y),
                double(tt.trailerHitch.z));
    const vec3 o = phys::vsub(b.p, truck.body().p);
    std::printf("  trailer origin - truck body position (%g, %g, %g)\n", double(o.x), double(o.y), double(o.z));
    for (int i = 0; i < 4; ++i) {
        const phys::Wheel& w = tr.wheel(i);
        std::printf("  TWHL%d pivot (%g, %g, %g) radius %g width %g  L0 %g k %g  steer %g\n", i, double(w.pivot.x),
                    double(w.pivot.y), double(w.pivot.z), double(w.radius), double(w.width), double(w.L0), double(w.k),
                    double(w.tune.steeringLimit));
    }
    const phys::TrailerJointTune& j = tt.joint;
    std::printf("  joint ForceLimit %g JointStatus %d lean %g/%g/%g roll %g/%g/%g LeanLimit %g elasticity %g/%g FreeRange %g "
                "FreeLean %g (cos %.9g) FreeRoll %g\n",
                double(j.forceLimit), j.jointStatus, double(j.restoreForceLean), double(j.dampConstLean),
                double(j.dampLinearLean), double(j.restoreForceRoll), double(j.dampConstRoll), double(j.dampLinearRoll),
                double(j.leanLimit), double(j.limitElasticityLean), double(j.limitElasticityRoll), double(j.freeRange),
                double(j.freeLean), double(j.cosFreeLean), double(j.freeRoll));
    if (const phys::Bound* bd = tr.bound())
        std::printf("  bound %zu points %zu polygons radius %g material %s (%g / %g)  TRAILER radius %g\n",
                    bd->points.size(), bd->polys.size(), double(bd->radius), tr.material().name.c_str(),
                    double(tr.material().friction), double(tr.material().elasticity), double(tr.radius()));
    for (int i = 0; i < 4; ++i)
        std::printf("  truck wheel %d L0 %g -> %g\n", i, double(before[i]), double(truck.wheel(i).L0));
    std::printf("  truck Mass %g (train compliance %g)\n", double(tune.sim.mass), double(tune.sim.mass * 0.005f));
    for (const auto& n : tt.notes) std::printf("  note: %s\n", n.c_str());
    return 0;
}

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  cctool test <archive.ar>...\n"
                 "  cctool list <archive.ar>\n"
                 "  cctool cat <path> <archive.ar>...\n"
                 "  cctool pack [--deflate] <out.ar> <dir>\n"
                 "  cctool mkcarmod [--deflate] <out.ar> <src car> <new car> <description> <archive.ar>...\n"
                 "  cctool drive [--trace] <file.run> <archive.ar>...\n"
                 "  cctool drives <dir> <outdir> <archive.ar>...\n"
                 "  cctool testpeds <archive.ar>...\n"
                 "  cctool peds <city> <parked|street> <seconds> <density> <out.csv> <archive.ar>...\n"
                 "  cctool testaudio <archive.ar>...\n"
                 "  cctool soundrun <city> <cop> <seconds> <traffic density> <out.txt> <archive.ar>...\n"
                 "  cctool music survey|trace|render|scenario|all ... (tools/music_tool.cpp)\n"
                 "  cctool trailer <car> <archive.ar>...\n"
                 "  cctool gmtrim <FluidR3_GM.sf2> <out.sf2> [rate]   (the bundled General MIDI drum kit and piano)\n"
                 "  cctool testnet <out.txt> <archive.ar>...   (network game checks: loopback and real sockets)\n"
                 "  cctool netserver [--port 8821] [--host 127.0.0.1 | --lan] [--static dir] [--name text] [--quiet]\n"
                 "  cctool netclient <ws://host:port/net> (--host | --join CODE) [--name N] [--car vpbug] [--city london]\n"
                 "                   [--seconds 60] [--start-with 2] [--relay] <archive.ar>...   (a headless native player)\n");
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        usage();
        return 2;
    }
    std::string cmd = args[0];
    args.erase(args.begin());
    bool deflate = false;
    if (!args.empty() && args[0] == "--deflate") {
        deflate = true;
        args.erase(args.begin());
    }
    if (cmd == "test" && !args.empty()) return cmdTest(args);
    if (cmd == "music") return cmdMusic(args);
    if (cmd == "gmtrim" && args.size() >= 2) return cmdGmTrim(args[0], args[1], args.size() >= 3 ? std::atoi(args[2].c_str()) : 22050);
    if (cmd == "testai" && !args.empty()) {
        // Only the AI checks (road network, traffic, police).
        setLogEcho(false);
        Vfs vfs;
        if (!mountAll(vfs, args)) return 1;
        int problems = 0;
        testAiNetwork(vfs, [&](bool cond, const std::string& what) {
            std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
            if (!cond) ++problems;
        });
        std::printf("%d problem(s)\n", problems);
        return problems == 0 ? 0 : 1;
    }
    if (cmd == "testfx" && !args.empty()) {
        // Only the effects checks (damage visuals, particles).
        setLogEcho(false);
        Vfs vfs;
        if (!mountAll(vfs, args)) return 1;
        int problems = 0;
        testEffects(vfs, [&](bool cond, const std::string& what) {
            std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
            if (!cond) ++problems;
        });
        std::printf("%d problem(s), %d file notes logged\n", problems, fileProblemCount());
        return problems == 0 ? 0 : 1;
    }
    if (cmd == "testpeds" && !args.empty()) {
        // Only the pedestrian checks (anim/ files, population, walking).
        setLogEcho(false);
        Vfs vfs;
        if (!mountAll(vfs, args)) return 1;
        int problems = 0;
        testPedestrians(vfs, [&](bool cond, const std::string& what) {
            std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
            if (!cond) ++problems;
        });
        std::printf("%d problem(s)\n", problems);
        return problems == 0 ? 0 : 1;
    }
    if (cmd == "testaudio" && !args.empty()) {
        // Only the sound checks (spec/audio).
        setLogEcho(false);
        Vfs vfs;
        if (!mountAll(vfs, args)) return 1;
        int problems = 0;
        testAudio(vfs, [&](bool cond, const std::string& what) {
            std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
            if (!cond) ++problems;
        });
        std::printf("%d problem(s)\n", problems);
        return problems == 0 ? 0 : 1;
    }
    if (cmd == "peds" && args.size() >= 6)
        return cmdPeds(args[0], args[1], float(std::atof(args[2].c_str())), float(std::atof(args[3].c_str())), args[4],
                       std::vector<std::string>(args.begin() + 5, args.end()));
    if (cmd == "uitest" && args.size() >= 2)
        return cmdUiTest(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "uiscreens" && args.size() >= 3)
        return cmdUiScreens(args[0], args[1], std::vector<std::string>(args.begin() + 2, args.end()));
    if (cmd == "testraces" && !args.empty()) {
        // Only the race-mode checks.
        setLogEcho(false);
        Vfs vfs;
        if (!mountAll(vfs, args)) return 1;
        int problems = 0;
        testRaces(vfs, [&](bool cond, const std::string& what) {
            std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
            if (!cond) ++problems;
        });
        std::printf("%d problem(s)\n", problems);
        return problems == 0 ? 0 : 1;
    }
    if (cmd == "testnet" && args.size() >= 2) {
        // The network game (game/multiplayer.md): unit checks and two-peer loopback sessions.
        setLogEcho(false);
        Vfs vfs;
        if (!mountAll(vfs, std::vector<std::string>(args.begin() + 1, args.end()))) return 1;
        int problems = 0;
        testNet(vfs, args[0], [&](bool cond, const std::string& what) {
            std::printf("  [%s] %s\n", cond ? " ok " : "FAIL", what.c_str());
            if (!cond) ++problems;
        });
        std::printf("%d problem(s)\n", problems);
        return problems == 0 ? 0 : 1;
    }
    if (cmd == "netserver") return cmdNetServer(args);
    if (cmd == "netclient") return cmdNetClient(args);
    if (cmd == "race" && args.size() >= 7)
        return cmdRace(args[0], args[1], std::atoi(args[2].c_str()), args[3] == "pro", float(std::atof(args[4].c_str())), args[5],
                       std::vector<std::string>(args.begin() + 6, args.end()));
    if (cmd == "list" && args.size() == 1) return cmdList(args[0]);
    if (cmd == "cat" && args.size() >= 2)
        return cmdCat(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "drive" && args.size() >= 2)
        return cmdDrive(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "drives" && args.size() >= 3)
        return cmdDrives(args[0], args[1], std::vector<std::string>(args.begin() + 2, args.end()));
    if (cmd == "traffic" && args.size() >= 5)
        return cmdTraffic(args[0], float(std::atof(args[1].c_str())), float(std::atof(args[2].c_str())), args[3],
                          std::vector<std::string>(args.begin() + 4, args.end()));
    if (cmd == "drivesim" && args.size() >= 3)
        return cmdDriveSim(args[0], args[1], std::vector<std::string>(args.begin() + 2, args.end()));
    if (cmd == "chase" && args.size() >= 6)
        return cmdChase(args[0], std::atoi(args[1].c_str()), float(std::atof(args[2].c_str())), float(std::atof(args[3].c_str())), args[4],
                        std::vector<std::string>(args.begin() + 5, args.end()));
    if (cmd == "soundrun" && args.size() >= 6)
        return cmdSoundRun(args[0], std::atoi(args[1].c_str()), float(std::atof(args[2].c_str())), float(std::atof(args[3].c_str())), args[4],
                           std::vector<std::string>(args.begin() + 5, args.end()));
    if (cmd == "trafficsurvey" && args.size() >= 2)
        return cmdTrafficSurvey(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "room" && args.size() >= 3)
        return cmdRoom(args[0], std::atoi(args[1].c_str()), std::vector<std::string>(args.begin() + 2, args.end()));
    if (cmd == "inst" && args.size() >= 3)
        return cmdInst(args[0], args[1], std::vector<std::string>(args.begin() + 2, args.end()));
    if (cmd == "pkg" && args.size() >= 2)
        return cmdPkg(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "trailer" && args.size() >= 2)
        return cmdTrailer(args[0], std::vector<std::string>(args.begin() + 1, args.end()));
    if (cmd == "pack" && args.size() == 2) return cmdPack(deflate, args[0], args[1]);
    if (cmd == "mkcarmod" && args.size() >= 5)
        return cmdMkCarMod(deflate, args[0], args[1], args[2], args[3],
                           std::vector<std::string>(args.begin() + 4, args.end()));
    usage();
    return 2;
}

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives) { return mountAll(vfs, archives); }
