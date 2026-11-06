// Native checks of the visual effects (spec/formats/damage-visuals.md, spec/engine/effects.md):
// damageable slots per car against the survey table, the texture mark round, repair, smoke levels
// and wheel wobble, and that check runs with every effect attached stay byte-identical.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/game_random.h"
#include "core/strutil.h"
#include "formats/pkg.h"
#include "fx/car_fx.h"
#include "fx/damage_textures.h"
#include "fx/effects_world.h"
#include "fx/image_source.h"
#include "fx/particles.h"
#include "model/asset_cache.h"
#include "model/vehicle.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "physics/drive_check.h"
#include "vfs/vfs.h"

using namespace cc;

namespace {

using Expect = std::function<void(bool, const std::string&)>;

// damage-visuals.md, "Checked against": slots per paint set (the exceptions listed), damageable
// body triangles, all body triangles, TextelDamageRadius.
struct SurveyRow {
    const char* car;
    int slots;
    int exceptSlots;  // -1 = every set has `slots`
    std::vector<int> exceptSets;
    int tris, bodyTris;
    float radius;
};

void slotSurvey(const Vfs& vfs, const Expect& expect) {
    const SurveyRow rows[] = {
        {"vp4x4", 5, -1, {}, 194, 326, 20.0f},          {"vpauditt", 7, 6, {4}, 450, 600, 2.0f},
        {"vpbug", 6, -1, {}, 284, 346, 2.7f},           {"vpbullet", 10, -1, {}, 214, 298, 3.0f},
        {"vpbus", 12, -1, {}, 252, 320, 3.3f},          {"vpcab", 8, -1, {}, 266, 309, 4.3f},
        {"vpcaddie", 8, -1, {}, 184, 256, 0.4f},        {"vpcentury", 6, -1, {}, 266, 348, 0.4f},
        {"vpcoop", 8, -1, {}, 236, 300, 2.7f},          {"vpcoop2k", 8, -1, {}, 285, 344, 2.9f},
        {"vpcop", 6, -1, {}, 188, 296, 3.0f},           {"vpdb7", 8, -1, {}, 191, 246, 3.4f},
        {"vpddbus", 5, -1, {}, 185, 351, 10.0f},        {"vpdune", 8, -1, {}, 284, 346, 3.0f},
        {"vpford", 8, -1, {}, 240, 356, 6.3f},          {"vpmustang99", 5, -1, {}, 178, 260, 3.5f},
        {"vppanoz", 10, -1, {}, 312, 370, 4.0f},        {"vppanozgt", 6, 3, {1, 3}, 318, 442, 20.0f},
        {"vpsemi", 5, -1, {}, 193, 299, 6.4f},          {"vpvwcup", 4, -1, {}, 278, 340, 3.7f},
    };
    AssetCache assets(vfs);
    fx::ImageSource images(vfs);
    int good = 0;
    for (const SurveyRow& r : rows) {
        VehicleModel m;
        if (!loadVehicle(assets, r.car, VehicleKind::Player, m)) {
            expect(false, std::string(r.car) + " loads");
            continue;
        }
        bool ok = true;
        std::string line;
        size_t tris0 = 0, body0 = 0;
        for (int set = 0; set < m.setCount(); ++set) {
            fx::DamageTextures t;
            t.setup(m, set, images);
            int want = r.slots;
            for (int e : r.exceptSets)
                if (e == set) want = r.exceptSlots;
            const int got = int(t.slots().size());
            line += strf(" %d", got);
            if (got != want) ok = false;
            if (set == 0) {
                tris0 = t.damageableTriangles();
                body0 = t.triangleCount();
            }
        }
        const fx::DamageVisualTune tune = fx::loadDamageVisualTune(vfs, r.car);
        const bool trisOk = int(tris0) == r.tris && int(body0) == r.bodyTris;
        const bool radiusOk = std::fabs(tune.textelDamageRadius - r.radius) < 0.001f;
        std::printf("  %-12s slots per set:%s  triangles %zu / %zu  radius %.1f\n", r.car, line.c_str(), tris0, body0,
                    double(tune.textelDamageRadius));
        if (ok && trisOk && radiusOk) ++good;
        else expect(false, strf("%s: slots, triangles (%d / %d) and radius as in damage-visuals.md", r.car, r.tris, r.bodyTris));
    }
    expect(good == 20, strf("damageable slots, triangles and radius of all 20 menu cars as in damage-visuals.md (%d match)", good));
}

// The clean image's top level as the working copy would start.
bool workIsClean(const fx::DamageSlot& s) {
    if (s.work.empty()) return true;
    const ImageLevel& L = s.clean->first().levels[0];
    return L.rgba.size() == s.work.size() && std::memcmp(L.rgba.data(), s.work.data(), s.work.size()) == 0;
}

void markChecks(const Vfs& vfs, const Expect& expect) {
    AssetCache assets(vfs);
    fx::ImageSource images(vfs);
    VehicleModel m;
    if (!loadVehicle(assets, "vpbug", VehicleKind::Player, m)) {
        expect(false, "vpbug loads");
        return;
    }
    fx::DamageTextures t;
    expect(t.setup(m, 0, images), "vpbug has texture damage");
    for (fx::DamageSlot& s : t.slots()) t.ensureWork(s);
    // The front-left corner of the body (car space: -X left, -Z front), radius 2.7.
    const vec3 corner(m.boundsMin.x, (m.boundsMin.y + m.boundsMax.y) * 0.5f, m.boundsMin.z);
    const uint32_t before = 0;
    setEffectsRandomState(before);
    const int n1 = t.markRound(corner, 2.7f);
    // Four draws per marked triangle.
    Lcg ref(before);
    for (int i = 0; i < 4 * n1; ++i) ref.next15();
    expect(effectsRandomState() == ref.state(), strf("a round of marks makes four effects draws per marked triangle (%d marked)", n1));
    std::printf("  vpbug front-left corner (%.2f, %.2f, %.2f), radius 2.7: %d of %zu triangles marked\n", double(corner.x),
                double(corner.y), double(corner.z), n1, t.triangleCount());
    expect(n1 > 100 && n1 < 220, "a hit at vpbug's front corner marks about 160 triangles (damage-visuals.md)");
    int changed = 0, cleanSlots = 0;
    for (const fx::DamageSlot& s : t.slots()) {
        if (!workIsClean(s)) ++changed;
        else ++cleanSlots;
    }
    expect(changed > 0, strf("the marks change working copies (%d of %zu slots)", changed, t.slots().size()));
    // Physics random state untouched.
    const uint32_t g0 = gameRandomState();
    setEffectsRandomState(12345);
    t.markRound(vec3(-m.boundsMin.x, 0.5f, m.boundsMax.z), 2.7f);
    expect(gameRandomState() == g0, "marks never draw from the game-wide (physics) generator");
    // Marks only uncover damage-image texels: each changed texel equals the damage image's.
    bool onlyDamage = true;
    for (const fx::DamageSlot& s : t.slots()) {
        if (s.work.empty()) continue;
        const ImageLevel& C = s.clean->first().levels[0];
        const ImageLevel& D = s.damage->first().levels[0];
        for (int y = 0; y < s.h; ++y)
            for (int x = 0; x < s.w; ++x) {
                const size_t i = (size_t(y) * size_t(s.w) + size_t(x)) * 4;
                if (std::memcmp(&s.work[i], &C.rgba[i], 4) == 0) continue;
                if (x >= D.width || y >= D.height ||
                    std::memcmp(&s.work[i], &D.rgba[(size_t(y) * size_t(D.width) + size_t(x)) * 4], 4) != 0)
                    onlyDamage = false;
            }
    }
    expect(onlyDamage, "every changed texel is the damage image's texel at the same place");
    // The same state gives the same marks (copies, not blends: order-independent results).
    fx::DamageTextures t2;
    t2.setup(m, 0, images);
    for (fx::DamageSlot& s : t2.slots()) t2.ensureWork(s);
    setEffectsRandomState(before);
    t2.markRound(corner, 2.7f);
    setEffectsRandomState(12345);
    t2.markRound(vec3(-m.boundsMin.x, 0.5f, m.boundsMax.z), 2.7f);
    bool same = true;
    for (size_t i = 0; i < t.slots().size(); ++i)
        if (t.slots()[i].work != t2.slots()[i].work) same = false;
    expect(same, "the same effects-generator state gives the same marks");
    t.repair();
    bool allClean = true;
    for (const fx::DamageSlot& s : t.slots())
        if (!workIsClean(s)) allClean = false;
    expect(allClean, "a clear damage refills every working copy from its clean image");
    // Shared clean artwork: vpbugyellow_sd and vpbugyellow_sd_dmg slots each get their own copy.
    int sd = 0;
    for (const fx::DamageSlot& s : t.slots())
        if (iequals(s.cleanName, "vpbugyellow_sd")) ++sd;
    expect(sd == 2, "vpbugyellow_sd and vpbugyellow_sd_dmg are two slots with separate copies of vpbugyellow_sd");
    // A radius 20 car (vp4x4) reaches every damageable triangle.
    VehicleModel m4;
    if (loadVehicle(assets, "vp4x4", VehicleKind::Player, m4)) {
        fx::DamageTextures t4;
        t4.setup(m4, 0, images);
        setEffectsRandomState(7);
        const int n4 = t4.markRound(vec3(0, 0.5f, 0), 20.0f);
        expect(n4 == int(t4.damageableTriangles()), strf("vp4x4 (radius 20) marks every damageable triangle (%d)", n4));
    }
}

void carFxChecks(const Vfs& vfs, const Expect& expect) {
    AssetCache assets(vfs);
    fx::ImageSource images(vfs);
    VehicleModel m;
    if (!loadVehicle(assets, "vpbug", VehicleKind::Player, m)) return;
    phys::CarTune tune = phys::loadCarTune(vfs, "vpbug", true);
    phys::Car car;
    car.setup(tune);
    car.resetFlat(0, 0, 0, 0);
    fx::CarFx f;
    f.setup(images, m, 0, "vpbug");
    car.setEffects(&f);
    const float med = tune.damage.medDamage, mx = tune.damage.maxDamage;
    const float dt = 1.0f / 120.0f;
    struct Case {
        float frac;
        int level;
    };
    const Case cases[] = {{-0.5f, 0}, {0.0f, 0}, {0.1f, 1}, {0.25f, 1}, {0.3f, 2}, {0.6f, 3}, {0.8f, 4}, {1.5f, 4}};
    bool levels = true, tilts = true;
    for (const Case& c : cases) {
        car.damage().damage = med + c.frac * (mx - med);
        f.sampleEffects(car, dt);
        if (f.smokeLevel() != c.level) levels = false;
        // Wheels at rest: g = 0, k = strength.
        const float s = f.strength();
        const float* t = f.wheelTilts();
        if (std::fabs(t[0] + 0.15f * s) > 1e-6f || std::fabs(t[1] - 0.35f * s) > 1e-6f ||
            std::fabs(t[2] - 0.35f * s) > 1e-6f || std::fabs(t[3] + 0.15f * s) > 1e-6f)
            tilts = false;
    }
    expect(levels, "engine smoke level = ceil(4 strength): 0 at MedDamage, 4 above 0.75");
    expect(tilts, "wheel tilts -0.15k, 0.35k, 0.35k, -0.15k with k = strength for wheels at rest");
    expect(fx::CarFx::smokeFrame(1) == 1 && fx::CarFx::smokeFrame(2) == 0 && fx::CarFx::smokeFrame(3) == 3 &&
               fx::CarFx::smokeFrame(4) == 2,
           "smoke frames by level: 1, 0, 3, 2 of fxpt8");
    // A pending mark request survives a clear damage (damage-visuals.md, "When marks are made").
    {
        phys::DamageState::Slot sl;
        sl.carContact = vec3(0.5f, 0.5f, -1.5f);
        sl.total = 2000.0f;
        const int r0 = f.rounds();
        f.countedImpact(car, sl, 2000.0f, dt);
        car.clearDamage();
        f.sampleEffects(car, dt);
        expect(f.rounds() == r0 + 1, "a pending mark request survives a clear damage and is carried out at the next update");
    }
    // Without fxpt8 (no effects world here) the whole smoking step is skipped: no point draw.
    const fx::DamageVisualTune& vt = f.tune();
    car.damage().damage = mx;
    const uint32_t s0 = effectsRandomState();
    f.sampleEffects(car, dt);
    expect(effectsRandomState() == s0, "no fxpt8: the smoking step is skipped whole (no smoke-point draw)");
    // vpbug with its smoke texture: SmokeOffset2 set, DoublePivot 0 -> a point between the two
    // offsets at the fraction of the first effects draw.
    {
        fx::EffectsWorld w(vfs);
        w.startSession(0);
        fx::CarFx g;
        g.setup(&w, w.images(), m, 0, "vpbug", car);
        car.setEffects(&g);
        const uint32_t s1 = effectsRandomState();
        g.sampleEffects(car, dt);
        Lcg ref(s1);
        const float fr = float(ref.next15()) * 3.0517578e-05f;
        const vec3& a = vt.smokeOffset;
        const vec3& b = vt.smokeOffset2;
        const vec3 q = g.smokePoint();
        const float want = (b.x - a.x) * fr + a.x;
        expect(g.hasSmoke() && q.x == want && q.y == ((b.y - a.y) * fr + a.y),
               strf("vpbug smoke point between SmokeOffset and SmokeOffset2 at the first draw's fraction (x = %.3f)", double(q.x)));
        car.setEffects(&f);
    }
    // A clear damage repairs and stops the smoke.
    car.damage().reset();
    f.sampleEffects(car, dt);
    expect(f.smokeLevel() == 0 && f.wheelTilts()[1] == 0.0f, "a clear damage stops smoke and wobble");
    car.setEffects(nullptr);
}

// Every check run with effects attached to every car gives the same output as without.
void checkRunsUnchanged(const Vfs& vfs, const Expect& expect) {
    const char* runs[] = {"cc_headon_vpbug", "cc_tbone_vpmustang99", "cc_chain_vpbug", "cc_bus_vpbus",
                          "london_e_vpbug",  "sf_n_vpauditt"};
    AssetCache assets(vfs);
    fx::ImageSource images(vfs);
    std::vector<std::unique_ptr<fx::CarFx>> made;
    std::vector<std::unique_ptr<VehicleModel>> models;
    int rounds = 0, checked = 0;
    for (const char* name : runs) {
        std::FILE* fp = std::fopen((std::string("checks/") + name + ".run").c_str(), "rb");
        if (!fp) {
            std::printf("  (checks/%s.run not found; skipped)\n", name);
            continue;
        }
        std::string text;
        char buf[4096];
        size_t k;
        while ((k = std::fread(buf, 1, sizeof buf, fp)) > 0) text.append(buf, k);
        std::fclose(fp);
        phys::RunFile run;
        std::string err;
        if (!phys::parseRunFile(text, run, err)) continue;
        fx::EffectsWorld world(vfs);
        world.startSession(0);
        auto runOnce = [&](bool fx) {
            std::FILE* out = std::tmpfile();
            phys::setCheckEffects(nullptr);
            if (fx)
                phys::setCheckEffects([&](phys::Car& car, const std::string& model, int paint) -> phys::CarEffectsHook* {
                    auto vm = std::make_unique<VehicleModel>();
                    if (!loadVehicle(assets, model, VehicleKind::Player, *vm)) return nullptr;
                    auto f = std::make_unique<fx::CarFx>();
                    f->setup(&world, images, *vm, paintSetFor(paint, vm->setCount()), model, car);
                    models.push_back(std::move(vm));
                    made.push_back(std::move(f));
                    return made.back().get();
                });
            std::string e;
            phys::runDriveCheck(vfs, run, out, e);
            phys::setCheckEffects(nullptr);
            std::string csv;
            std::rewind(out);
            while ((k = std::fread(buf, 1, sizeof buf, out)) > 0) csv.append(buf, k);
            std::fclose(out);
            return csv;
        };
        const std::string plain = runOnce(false);
        made.clear();
        models.clear();
        const std::string withFx = runOnce(true);
        int r = 0, sparks = 0;
        for (const auto& f : made) {
            r += f->rounds();
            sparks += f->sparkCount();
        }
        rounds += r;
        (void)sparks;
        made.clear();
        models.clear();
        ++checked;
        expect(!plain.empty() && plain == withFx,
               strf("%s: byte-identical with every car's effects (marks, particles, sparks, chips, skid marks) attached (%d mark rounds)",
                    name, r));
    }
    if (checked) expect(rounds > 0, "the crash runs make texture marks");
}

void particleChecks(const Vfs& vfs, const Expect& expect) {
    // The rule files (effects.md, "Checked against").
    struct Want {
        const char* name;
        int blast, flags;
        uint32_t color;
    };
    const Want wants[] = {{"dirt", 100, 0, 0xffffffffu}, {"dust", 64, 0, 0xffffffffu},  {"grass", 64, 0, 0xffffffffu},
                          {"leaf", 64, 0, 0xffffffffu},  {"smoke", 32, 1, 0xf0faf0e6u}, {"snow", 64, 0, 0xffffffffu},
                          {"splash", 64, 1, 0xfffaf0e6u}, {"rock", 32, 0, 0xffffffffu}};
    int good = 0;
    for (const Want& w : wants) {
        fx::BirthRule r;
        const bool ok = fx::loadBirthRule(vfs, std::string("tune/effects/") + w.name + ".asBirthRule", r);
        if (ok && r.initialBlast == w.blast && r.birthFlags == w.flags && uint32_t(r.color) == w.color) ++good;
        else
            std::printf("  %s: blast %d flags %d colour %08x\n", w.name, r.initialBlast, r.birthFlags, unsigned(r.color));
    }
    expect(good == 8, "the eight wheel rules: burst sizes, BirthFlags and colours as in effects.md");
    fx::BirthRule rain;
    expect(fx::loadBirthRule(vfs, "tune/rain.asBirthRule", rain) && rain.birthFlags == 8 && rain.height == 0.0f &&
               rain.spewRate == 200.0f && rain.texFrameStart == 0 && rain.texFrameEnd == 15,
           "tune/rain.asBirthRule: 200 drops a second, BirthFlags 8, Height 0, frames 0-15");

    // The first three births of the smoke rule from effects state 0.
    fx::BirthRule smoke;
    fx::loadBirthRule(vfs, "tune/effects/smoke.asBirthRule", smoke);
    fx::Emitter e;
    e.setup(128, 8, 8);
    setEffectsRandomState(0);
    e.blast(3, &smoke);
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.00051f; };
    const fx::Particle& p0 = e.particle(0);
    const fx::Particle& p1 = e.particle(1);
    const fx::Particle& p2 = e.particle(2);
    const bool b0 = near(p0.v.x, 0.1002f) && near(p0.v.y, 0.4f) && near(p0.v.z, 0.2296f) && near(p0.life, 4.8851f) &&
                    near(p0.im, 10.0f) && p0.frame == 14 && near(p0.radius, 1.5f) && p0.da == -5 && p0.a == 240;
    const bool b1 = near(p1.v.x, 0.1972f) && near(p1.v.y, 0.4f) && near(p1.v.z, 0.2604f) && near(p1.life, 4.9740f) &&
                    p1.da == -5;
    const bool b2 = near(p2.v.x, 0.1149f) && near(p2.v.y, 0.4f) && near(p2.v.z, 0.1051f) && near(p2.life, 5.0080f) &&
                    p2.da == -4;
    std::printf("  smoke births from state 0: (%.4f %.4f %.4f) life %.4f da %d; (%.4f %.4f %.4f) life %.4f da %d; (%.4f %.4f %.4f) life %.4f da %d\n",
                double(p0.v.x), double(p0.v.y), double(p0.v.z), double(p0.life), p0.da, double(p1.v.x), double(p1.v.y),
                double(p1.v.z), double(p1.life), p1.da, double(p2.v.x), double(p2.v.y), double(p2.v.z), double(p2.life), p2.da);
    expect(b0 && b1 && b2, "the first three smoke births from effects state 0 as in effects.md");
    Lcg ref(0);
    for (int i = 0; i < 3 * 15; ++i) ref.next15();
    expect(effectsRandomState() == ref.state(), "a birth with a frame draw takes 15 draws");

    // Alpha steps by whole units per update: -2 at 1/144 s never fades, at 1/60 s exactly -2.
    {
        fx::BirthRule r;
        r.dAlpha = -2;
        r.life = 100;
        r.gravity = 0;
        fx::Emitter a, b;
        a.setup(4, 1, 1);
        b.setup(4, 1, 1);
        a.blast(1, &r);
        b.blast(1, &r);
        for (int i = 0; i < 10; ++i) {
            a.update(1.0f / 144.0f);
            b.update(1.0f / 60.0f);
        }
        expect(a.particle(0).a == 255 && b.particle(0).a == 235, "alpha steps: never fades at 1/144 s, -2 per update at 1/60 s");
    }
    // A particle dies one update after its alpha reached 0; capacity is never exceeded.
    {
        fx::BirthRule r;
        r.dAlpha = -128;
        r.life = 100;
        fx::Emitter a;
        a.setup(8, 1, 1);
        a.blast(20, &r);
        const int full = a.count();
        a.update(1.0f / 60.0f);
        a.update(1.0f / 60.0f);
        const int afterZero = a.count();
        a.update(1.0f / 60.0f);
        expect(full == 8 && afterZero == 8 && a.count() == 0, "pool capacity respected; death one update after alpha 0");
    }

    // A frame outside the sheet (particles.md): born, updated and counted, never drawn.
    {
        fx::BirthRule r;
        r.texFrameStart = 4;
        r.texFrameEnd = 4;
        r.life = 5.0f;
        fx::Emitter e;
        e.setup(16, 2, 2);
        e.blast(6, &r);
        e.update(1.0f / 60.0f);
        std::vector<fx::FxVertex> out;
        fx::particleTriangles(e, vec3(1, 0, 0), vec3(0, 1, 0), out);
        expect(e.count() == 6 && out.empty(), strf("frame 4 of a 2 x 2 sheet: %d particles counted, none drawn", e.count()));
    }

    // Prop data: TexNumber over every prop data file (effects.md, "Checked against").
    {
        std::map<int, int> tex;
        int files = 0, withRule = 0;
        for (const std::string& path : vfs.listUnique("tune/banger/")) {
            if (!endsWith(path, ".dgBangerData")) continue;  // the survey's spelling (3 files end in .dgbangerdata)
            fx::PropParticles pp;
            const std::string name = stem(path);
            if (!fx::loadPropParticles(vfs, name, pp)) continue;
            ++files;
            if (!pp.hasRule) continue;
            ++withRule;
            ++tex[pp.texNumber];
        }
        std::string line;
        for (const auto& kv : tex) line += strf(" %d x %d;", kv.second, kv.first);
        std::printf("  %d prop data files, %d with a BirthRule; TexNumber:%s\n", files, withRule, line.c_str());
        expect(files == 992 && withRule == 992 && tex[0] == 954 && tex[1] == 1 && tex[2] == 3 && tex[3] == 8 && tex[4] == 3 && tex[5] == 6 &&
                   tex[6] == 8 && tex[7] == 8 && tex[16] == 1,
               "992 prop data files read by position, all with a rule block; TexNumbers 954 x 0, 1 x 1, 3 x 2, 8 x 3, 3 x 4, 6 x 5, 8 x 6, 8 x 7, 1 x 16 (38 props emit)");
    }
    // .vehCarDamage rule keys overwrite the shared smoke rule.
    {
        fx::EffectsWorld world(vfs);
        world.startSession(0);
        AssetCache assets(vfs);
        VehicleModel m;
        loadVehicle(assets, "vpbug", VehicleKind::Player, m);
        phys::CarTune tune = phys::loadCarTune(vfs, "vpbug", true);
        phys::Car car;
        car.setup(tune);
        fx::CarFx f;
        f.setup(&world, world.images(), m, 0, "vpbug", car);
        const fx::BirthRule& r = world.smokeRule();
        expect(r.life == 0.8f && r.mass == 0.591f && r.radius == 0.361f && r.dAlpha == -10 && r.gravity == 15.0f &&
                   (uint32_t(r.color) >> 24) == 245,
               "vpbug's .vehCarDamage sets the shared smoke rule (Life 0.8, Mass 0.591, Radius 0.361, DAlpha -10, Gravity 15, alpha 245)");
    }
    // The spark colour table.
    {
        const fx::SparkTable t = fx::loadSparkTable(vfs);
        bool alpha128 = t.fromFile && t.w == 8 && t.h == 8 && t.shift == 5;
        for (uint32_t c : t.rgba)
            if ((c >> 24) != 128) alpha128 = false;
        expect(alpha128 && t.rgba.size() == 64, "texture/spark.tga: 8 x 8, 24-bit, alpha 128 throughout, shift 5");
    }
}

// Sparks, chips, wheel particles, smoke and skid marks on a car driven on the flat world.
void systemChecks(const Vfs& vfs, const Expect& expect) {
    fx::EffectsWorld world(vfs);
    world.startSession(0);
    AssetCache assets(vfs);
    VehicleModel m;
    if (!loadVehicle(assets, "vpbug", VehicleKind::Player, m)) return;
    phys::CollisionWorld flat;
    flat.setupFlat(vfs, 0.0f);
    phys::CarTune tune = phys::loadCarTune(vfs, "vpbug", true);
    phys::Car car;
    car.setup(tune);
    car.resetFlat(0, 0, 0, 0);
    fx::CarFx f;
    f.setup(&world, world.images(), m, 0, "vpbug", car);
    car.setEffects(&f);
    const float F = phys::kCheckFrame;
    const int n = phys::physicsStepsForFrame(F);
    const float dt = F / float(n);
    auto drive = [&](float seconds, const phys::DriverControls& c) {
        for (int fr = 0; fr < int(seconds / F); ++fr) {
            car.controlStep(c);
            for (int k = 0; k < n; ++k) car.step(dt, 1.0f / dt, flat);
        }
    };
    phys::DriverControls go;
    go.accelerate = 1.0f;
    drive(3.0f, go);
    expect(car.speedMph() > 15.0f, strf("vpbug reaches %.0f mph on the flat world", double(car.speedMph())));
    // A counted impact on a slot with total 3000: 64 sparks (7 draws each), 2 chips (9 draws each).
    phys::DamageState::Slot slot;
    slot.used = true;
    slot.total = 3000.0f;
    slot.contact = car.modelOrigin();
    slot.normal = vec3(1, 0, 0);
    const uint32_t s0 = effectsRandomState();
    f.countedImpact(car, slot, 3000.0f, dt);
    Lcg ref(s0);
    for (int i = 0; i < 64 * 7 + 2 * 9; ++i) ref.next15();
    expect(effectsRandomState() == ref.state() && f.sparkCount() == 64,
           "an impact with total 3000 at speed: 64 sparks (the rest dropped, 7 draws each) and 2 chips (9 draws each)");
    int liveChips = 0;
    for (int i = 0; i < 16; ++i)
        if (f.chips()[i].age < 1.8f) ++liveChips;
    expect(liveChips == 2, "two chips started in the ring");
    bool rows = true;
    for (int i = 0; i < 64; ++i) {
        const fx::Spark& sp = f.sparks()[i];
        if (sp.life < 192 || sp.life > 255 || sp.c % world.sparkTable().w != 0) rows = false;
    }
    expect(rows, "spark lives 192-255 and colour rows on row boundaries");
    // Sparks live about 9-12 steps of 1/30 s; chips 1.8 s.
    phys::DriverControls coast;
    drive(0.5f, coast);
    expect(f.sparkCount() == 0, "sparks are gone after 0.5 s");
    drive(1.5f, coast);
    liveChips = 0;
    for (int i = 0; i < 16; ++i)
        if (f.chips()[i].age < 1.8f) ++liveChips;
    expect(liveChips == 0, "chips stop being drawn after 1.8 s");
    // Handbrake slide on cobblestone (ptxindex 4, 7): wheel particles and skid marks.
    phys::DriverControls slide;
    slide.accelerate = 1.0f;
    slide.handbrake = 1.0f;
    slide.steer = 1.0f;
    drive(3.0f, go);
    drive(1.5f, slide);
    int entries = 0;
    for (int w = 0; w < 4; ++w) entries += (f.skids(w).head - f.skids(w).tail + 64) % 64;
    std::printf("  handbrake slide: %d wheel particles live, %d skid entries\n", f.wheelEmitter().count(), entries);
    expect(f.wheelEmitter().count() > 0, "a sliding car on cobblestone makes wheel particles");
    expect(entries > 0, "a sliding car lays skid marks");
    // Smoke at full damage: one particle per sample, frame 2 (black).
    car.damage().damage = tune.damage.maxDamage;
    drive(0.2f, coast);
    expect(f.smokeLevel() == 4 && f.smokeEmitter().count() > 0 && f.smokeEmitter().particle(0).frame == 2,
           "a car past MedDamage smokes (level 4: frame 2 of fxpt8)");
    // A clear damage empties the smoke emitter; a car reset empties the skid rings.
    car.damage().reset();
    car.resetFlat(0, 0, 0, 0);
    drive(0.02f, coast);
    entries = 0;
    for (int w = 0; w < 4; ++w) entries += (f.skids(w).head - f.skids(w).tail + 64) % 64;
    expect(f.smokeLevel() == 0 && entries == 0 && world.smokeRule().texFrameStart == 0,
           "a clear damage stops the smoke (frames 0, 0) and a car reset empties the skid rings");
    car.setEffects(nullptr);

    // Props: sp_hotdogcart_f (TexNumber 3): 16 particles at the hit, then 5 a second (fxpt3).
    {
        world.activateProp(1, "sp_hotdogcart_f", vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(10, 1, 10));
        const bool burst = world.props().size() == 1 && world.props()[0]->emitter.count() == 16 &&
                           world.props()[0]->texture == "fxpt3";
        for (int i = 0; i < 30; ++i) world.frameUpdate(1.0f / 30.0f, false);
        const int afterSecond = world.props().empty() ? -1 : world.props()[0]->emitter.count();
        world.activateProp(2, "sp_mailbox_f", vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 1, 0));
        const bool mailbox = world.props().size() == 2 && world.props()[1]->emitter.count() == 10;
        world.activateProp(3, "vpbug_WHL0", vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), vec3(0, 1, 0));  // TexNumber 0
        std::printf("  props: hot dog cart %s, %d particles after 1 s; mailbox %s\n", burst ? "16 at the hit" : "WRONG",
                    afterSecond, mailbox ? "10 at the hit" : "WRONG");
        expect(burst && mailbox && world.props().size() == 2,
               "prop particles: sp_hotdogcart_f bursts 16 (fxpt3), sp_mailbox_f 10, a TexNumber 0 prop none");
        world.resetProps();
    }

    // The skid ring on its own: a start entry after 0.1 m, straight runs stretch, at most 63 entries.
    fx::SkidRing ring;
    ring.hw = 0.1f;
    ring.iw = 5.0f;
    const vec3 X(1, 0, 0);
    ring.mark(vec3(0, 0, 0), X, true);
    ring.mark(vec3(0, 0, -0.05f), X, true);
    const bool waits = ring.head == 0 && ring.pending;
    ring.mark(vec3(0, 0, -0.2f), X, true);
    const bool started = ring.head == 2 && ring.e[0].v == 0.0f && std::fabs(ring.e[1].v - 1.0f) < 1e-5f;
    ring.mark(vec3(0, 0, -1.0f), X, true);
    const bool stretched = ring.head == 2 && std::fabs(ring.e[1].v - 5.0f) < 1e-4f;
    for (int i = 0; i < 100; ++i) ring.mark(vec3(float(i % 2), 0, -2.0f - float(i)), X, true);  // zig-zag: a push each time
    const int kept = (ring.head - ring.tail + 64) % 64;
    ring.mark(vec3(), X, false);
    expect(waits && started && stretched && kept == 63 && !ring.cur && !ring.pending,
           "skid ring: starts after 0.1 m, straight runs stretch the last pair, 63 entries kept, no texture ends the strip");
}

}  // namespace

void testEffects(Vfs& vfs, const std::function<void(bool, const std::string&)>& expect) {
    std::printf("== Damage visuals (damage-visuals.md)\n");
    slotSurvey(vfs, expect);
    markChecks(vfs, expect);
    carFxChecks(vfs, expect);
    std::printf("== Particles and effects (particles.md, effects.md)\n");
    particleChecks(vfs, expect);
    systemChecks(vfs, expect);
    std::printf("== Effects never change the physics\n");
    checkRunsUnchanged(vfs, expect);
}
