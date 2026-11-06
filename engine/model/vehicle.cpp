#include "model/vehicle.h"

#include <cmath>
#include <cstdio>

#include "core/log.h"
#include "core/strutil.h"
#include "model/asset_cache.h"

namespace cc {

const std::vector<std::string>& playerCarParts() {
    static const std::vector<std::string> k = {
        "BODY",    "SHADOW",  "HLIGHT",  "TLIGHT",  "RLIGHT",   "SLIGHT0",  "SLIGHT1",
        "BLIGHT",  "BODYDAMAGE", "SIREN0", "SIREN1", "DECAL",   "DRIVER",   "SHOCK0",
        "SHOCK1",  "SHOCK2",  "SHOCK3",  "ARM0",    "ARM1",     "ARM2",     "ARM3",
        "SHAFT2",  "SHAFT3",  "AXLE0",   "AXLE1",   "ENGINE",   "WHL0",     "WHL1",
        "WHL2",    "WHL3",    "BREAK0",  "BREAK1",  "BREAK2",   "BREAK3",   "BREAK01",
        "BREAK12", "BREAK23", "BREAK03", "HUB0",    "HUB1",     "HUB2",     "HUB3",
        "TRAILER_HITCH", "SRN0", "SRN1", "SRN2",    "SRN3",     "HEADLIGHT0", "HEADLIGHT1",
        "FNDR0",   "FNDR1",   "WHL4",    "WHL5",    "VARIANT0", "VARIANT1", "VARIANT2",
        "VARIANT3", "VARIANT4", "VARIANT5", "VARIANT6", "VARIANT7", "VARIANT8", "VARIANT9"};
    return k;
}

const std::vector<std::string>& trafficCarParts() {
    static const std::vector<std::string> k = {
        "BODY", "SHADOW", "HLIGHT", "TLIGHT", "SLIGHT0", "SLIGHT1", "WHL0", "WHL1", "WHL2",
        "WHL3", "BREAK0", "BREAK1", "BREAK2", "BREAK3", "HEADLIGHT0", "HEADLIGHT1", "WHL4", "WHL5"};
    return k;
}

const std::vector<std::string>& trailerParts() {
    static const std::vector<std::string> k = {"TRAILER", "SHADOW", "TLIGHT", "TWHL0", "TWHL1",
                                               "TWHL2",   "TWHL3",  "TRAILER_HITCH"};
    return k;
}

namespace {

// Parts placed by their own .mtx origin ("live" parts use it for a still view).
bool hasOwnPivot(const std::string& n) {
    static const char* const prefixes[] = {"WHL", "TWHL", "BREAK", "SRN", "HEADLIGHT", "FNDR",
                                           "VARIANT", "SHOCK", "ARM", "SHAFT", "AXLE"};
    if (n == "TRAILER_HITCH" || n == "ENGINE") return true;
    for (const char* p : prefixes)
        if (startsWith(n, p)) return true;
    return false;
}

void problem(VehicleModel& m, const std::string& path, const std::string& what, LogLevel lvl) {
    m.problems.push_back(path + ": " + what);
    logFile(lvl, path, what);
}

}  // namespace

const Pivot* VehicleModel::pivotOf(const std::string& name) const {
    for (size_t i = 0; i < parts.parts.size(); ++i)
        if (iequals(parts.parts[i].name, name)) return &pivots[i];
    return nullptr;
}

int paintSetFor(int colorIndex, int setCount) {
    if (colorIndex < 0 || colorIndex > 255) colorIndex = 0;
    return setCount > 0 ? colorIndex % setCount : 0;
}

int trailerSetFor(int carSet, int trailerSetCount) {
    return trailerSetCount > 0 ? carSet % trailerSetCount : 0;
}

bool loadVehicle(AssetCache& assets, const std::string& baseName, VehicleKind kind,
                 VehicleModel& m, bool strict) {
    m = VehicleModel();
    m.baseName = baseName;
    m.kind = kind;
    const std::string pkgPath = "geometry/" + baseName + ".pkg";
    m.pkg = assets.package(baseName, true);
    if (!m.pkg) {
        m.problems.push_back(pkgPath + ": missing or broken");
        return false;
    }
    const auto& names = kind == VehicleKind::Player    ? playerCarParts()
                        : kind == VehicleKind::Traffic ? trafficCarParts()
                                                       : trailerParts();
    m.parts = takeParts(*m.pkg, names);
    for (const auto& s : m.parts.skippedMeshes)
        problem(m, pkgPath, "mesh '" + s + "' is out of the part order and is not used", LogLevel::Info);

    const std::string bodyName = kind == VehicleKind::Trailer ? "TRAILER" : "BODY";
    const PartLods* body = m.part(bodyName);
    if (!body || !body->any()) {
        problem(m, pkgPath, "has no " + bodyName + " mesh", LogLevel::Error);
        return false;
    }
    const Mesh* bh = body->at(LodH) ? body->at(LodH) : body->at(LodVL);
    if (bh) {
        m.boundsMin = bh->boundsMin;
        m.boundsMax = bh->boundsMax;
    }

    // Pivots.
    m.pivots.resize(m.parts.parts.size());
    for (size_t i = 0; i < m.parts.parts.size(); ++i) {
        const PartLods& p = m.parts.parts[i];
        if (!p.any() || !hasOwnPivot(p.name)) continue;
        m.pivots[i] = assets.pivot(baseName, p.name);
        if (!m.pivots[i].found)
            problem(m, pivotPath(baseName, p.name), "pivot file missing; part drawn at the car origin",
                    LogLevel::Warn);
    }
    // Hubs share their wheel's transform.
    for (int n = 0; n < 4; ++n) {
        const Pivot* w = m.pivotOf(strf("WHL%d", n));
        for (size_t i = 0; i < m.parts.parts.size(); ++i)
            if (m.parts.parts[i].name == strf("HUB%d", n) && w) m.pivots[i] = *w;
    }

    // Required wheels.
    std::vector<std::string> required;
    if (kind == VehicleKind::Player) required = {"WHL0"};
    if (kind == VehicleKind::Traffic) required = {"WHL0", "WHL1", "WHL2", "WHL3"};
    if (kind == VehicleKind::Trailer) required = {"TWHL0", "TWHL1"};
    for (const auto& r : required) {
        const PartLods* p = m.part(r);
        if (!p || !p->any()) {
            // pkg.md rule 3: a required part with no level makes MM2 stop with an error.
            problem(m, pkgPath, "required part " + r + " is missing (MM2 stops with an error)",
                    strict ? LogLevel::Error : LogLevel::Warn);
            if (strict) return false;
        }
    }

    // Ground height from the wheels.
    const char* wheelPrefix = kind == VehicleKind::Trailer ? "TWHL" : "WHL";
    const int wheelCount = kind == VehicleKind::Trailer ? 4 : 6;
    for (int n = 0; n < wheelCount; ++n) {
        std::string wn = strf("%s%d", wheelPrefix, n);
        const PartLods* p = m.part(wn);
        const Pivot* pv = m.pivotOf(wn);
        if (!p || !p->any() || !pv || !pv->found) continue;
        float g = pv->origin.y - pv->wheelRadius();
        if (!m.groundFromWheels || g < m.groundY) m.groundY = g;
        m.groundFromWheels = true;
    }
    if (!m.groundFromWheels) m.groundY = m.boundsMin.y;

    // Trailer.
    if (kind == VehicleKind::Player) {
        // A trailer needs a TRAILER_HITCH mesh (any level) and a hitch pivot origin that is not
        // exactly (0, 0, 0) (car-model.md, "Trailers"); vpcentury has no hitch mesh.
        const PartLods* hitch = m.part("TRAILER_HITCH");
        const std::string trailerName = baseName + "_trailer";
        const Pivot carHitch = assets.pivot(baseName, "TRAILER_HITCH");
        const bool hitchAtOrigin = !carHitch.found || (carHitch.origin.x == 0.0f && carHitch.origin.y == 0.0f &&
                                                       carHitch.origin.z == 0.0f);
        if (hitch && hitch->any() && !hitchAtOrigin && assets.packageExists(trailerName)) {
            auto t = std::make_unique<VehicleModel>();
            if (loadVehicle(assets, trailerName, VehicleKind::Trailer, *t)) {
                const Pivot trHitch = assets.pivot(trailerName, "TRAILER_HITCH");
                vec3 a = carHitch.found ? carHitch.origin : vec3();
                vec3 b = trHitch.found ? trHitch.origin : vec3();
                if (!trHitch.found)
                    problem(m, pivotPath(trailerName, "TRAILER_HITCH"),
                            "trailer hitch pivot missing; trailer placed at the car hitch", LogLevel::Warn);
                m.trailerOrigin = a - b;
                m.trailer = std::move(t);
            } else {
                for (auto& pr : t->problems) m.problems.push_back(pr);
            }
        }
    }
    return true;
}

std::vector<DrawPart> buildStillCar(const VehicleModel& car, Lod lod, int color,
                                    const StillCarOptions& opt) {
    std::vector<DrawPart> out;
    const int set = paintSetFor(color, car.setCount());
    const vec3 origin(0, 0, 0);
    if (opt.shadow) addStillShadow(car, set, origin, car.groundY, out);
    addCarParts(car, lod, set, origin, opt, out);
    if (opt.trailer && car.trailer) {
        const VehicleModel& t = *car.trailer;
        const int tset = trailerSetFor(set, t.setCount());
        if (opt.shadow) addStillShadow(t, tset, car.trailerOrigin, t.groundY + car.trailerOrigin.y, out);
        addCarParts(t, lod, tset, car.trailerOrigin, opt, out);
    }
    return out;
}

void addStillShadow(const VehicleModel& v, int matSet, vec3 base, float groundY, std::vector<DrawPart>& out) {
    // Always SHADOW's H slot (after the upward fill), whatever the car's level.
    const PartLods* p = v.part("SHADOW");
    if (!p || !p->at(LodH)) return;
    DrawPart d;
    d.pkg = v.pkg.get();
    d.mesh = p->at(LodH);
    d.materialSet = matSet;
    d.flags = DrawUnlit | DrawShadow | DrawForceBlend;
    d.role = "SHADOW";
    d.offset = vec3(base.x, groundY + 0.005f, base.z);  // flattened to just above the ground
    out.push_back(d);
}

void addTrailerParts(const VehicleModel& t, Lod lod, int set, bool brakeLight, std::vector<DrawPart>& out,
                     const WheelPoseFn& wheelPose) {
    auto add = [&](const std::string& name, bool usePivot) {
        const PartLods* p = t.part(name);
        if (!p || !p->at(lod)) return;
        DrawPart d;
        d.pkg = t.pkg.get();
        d.mesh = p->at(lod);
        d.materialSet = set;
        d.flags = DrawDamageSlots;
        d.role = p->name.c_str();
        if (usePivot) {
            const Pivot* pv = t.pivotOf(name);
            if (pv && pv->found) d.offset = pv->origin;
        }
        if (wheelPose) wheelPose(name, d);
        if (d.mesh) out.push_back(d);
    };
    add("TRAILER", false);
    if (lod != LodH) return;
    if (brakeLight) add("TLIGHT", false);
    for (int n = 0; n < 4; ++n) add(strf("TWHL%d", n), true);
}

bool fenderOffset(const VehicleModel& m, vec3& o) {
    o = vec3();
    const PartLods* f = m.part("FNDR0");
    const Pivot* fp = m.pivotOf("FNDR0");
    const Pivot* wp = m.pivotOf("WHL0");
    if (!f || !f->at(LodH) || !fp || !fp->found || !wp || !wp->found) return false;
    o = vec3(fp->origin.x - wp->origin.x, fp->origin.y - wp->origin.y, fp->origin.z - wp->origin.z);
    o.y = o.y + 0.025f;
    return true;
}

bool liveSuspensionMatrix(const VehicleModel& m, const std::string& part, const float t[4], mat4& out, bool& isLive) {
    isLive = false;
    int wheel = -1, axle = -1;
    int n = -1;
    if (std::sscanf(part.c_str(), "SHOCK%d", &n) == 1 || std::sscanf(part.c_str(), "ARM%d", &n) == 1) wheel = n;
    else if (part == "SHAFT2") wheel = 2;
    else if (part == "SHAFT3") wheel = 3;
    else if (part == "AXLE0") axle = 0;
    else if (part == "AXLE1") axle = 1;
    if (wheel < 0 && axle < 0) return false;
    if (wheel > 3) return false;
    isLive = true;
    const Pivot* pv = m.pivotOf(part);
    if (!pv || !pv->found) return false;  // never placed without its .mtx: not drawn
    // The 48 bytes as a matrix: rows X, Y, Z and origin P (pkg.md, "Coordinate system").
    vec3 X = pv->boxMin, Y = pv->boxMax, Z = pv->centre;
    const vec3 P = pv->origin;
    auto centre = [&](int w) {
        const Pivot* wp = m.pivotOf(strf("WHL%d", w));
        return wp && wp->found ? wp->origin : vec3();
    };
    auto reach = [&](const vec3& C, const vec3& A) {
        const vec3 D(C.x - P.x, C.y - P.y, C.z - P.z);
        return (D.z * A.z + D.y * A.y) + D.x * A.x;
    };
    if (wheel >= 0) {
        const float k = 1.0f / reach(centre(wheel), Z);
        const bool mode1 = std::fabs(Z.y) >= 0.5f;
        Z.y = mode1 ? t[wheel] * k + 1.0f : t[wheel] * k;
    } else {
        const int a = axle == 0 ? 0 : 2, b = a + 1;
        const vec3 C = centre(a);
        const float kz = 1.0f / reach(C, Z), kx = 1.0f / reach(C, X);
        const float d = t[a] - t[b];
        X.y = (d * kx) * 0.5f;
        Z.y = ((t[b] + t[a]) * kz) * 0.5f;
    }
    out = mat4::identity();
    out.m[0] = X.x, out.m[1] = X.y, out.m[2] = X.z;
    out.m[4] = Y.x, out.m[5] = Y.y, out.m[6] = Y.z;
    out.m[8] = Z.x, out.m[9] = Z.y, out.m[10] = Z.z;
    out.m[12] = P.x, out.m[13] = P.y, out.m[14] = P.z;
    return true;
}

void addCarParts(const VehicleModel& car, Lod lod, int set, vec3 base, const StillCarOptions& opt,
                 std::vector<DrawPart>& out, const WheelPoseFn& wheelPose) {
    auto add = [&](const std::string& name, Lod l, unsigned flags, bool usePivot) {
        const PartLods* p = car.part(name);
        if (!p || !p->at(l)) return;
        DrawPart d;
        d.pkg = car.pkg.get();
        d.mesh = p->at(l);
        d.materialSet = set;
        d.flags = flags | DrawDamageSlots;
        d.role = p->name.c_str();
        d.offset = base;
        if (usePivot) {
            const Pivot* pv = car.pivotOf(name);
            if (pv && pv->found) d.offset += pv->origin;
        }
        if (wheelPose && wheelPose(name, d)) {
            // the caller placed it (moving wheels, fenders, live parts)
        } else {
            // Still view (car-model.md): fenders by the fender rule, live parts with travel 0.
            if (name == "FNDR0" || name == "FNDR1") {
                vec3 o;
                fenderOffset(car, o);
                const Pivot* wp = car.pivotOf(name == "FNDR0" ? "WHL0" : "WHL1");
                const vec3 w = wp && wp->found ? wp->origin : vec3();
                d.offset = base + vec3(w.x + (name == "FNDR0" ? o.x : -o.x), w.y + o.y, w.z + o.z);
            } else {
                const float t0[4] = {0, 0, 0, 0};
                mat4 lm;
                bool isLive = false;
                const bool ok = liveSuspensionMatrix(car, name, t0, lm, isLive);
                if (isLive && !ok) return;
                if (isLive) {
                    d.hasTransform = true;
                    d.transform = mat4::translate(base) * lm;
                    d.offset = base;
                }
            }
        }
        if (!d.mesh) return;  // a live part without its .mtx is not drawn
        out.push_back(d);
    };

    if (car.kind == VehicleKind::Traffic) {
        // Body at the level, breakables at H, wheels only at H (car-model.md LOD table).
        add("BODY", lod, lod == LodH ? unsigned(DrawReflect) : 0u, false);
        for (int n = 0; n < 4; ++n) add(strf("BREAK%d", n), LodH, 0, true);
        if (lod == LodH)
            for (int n = 0; n < 6; ++n) add(strf("WHL%d", n), LodH, 0, true);
        if (opt.lights) add("TLIGHT", lod, DrawUnlit, false);  // HLIGHT is never drawn
        return;
    }
    if (car.kind == VehicleKind::Trailer) {
        add("TRAILER", lod, 0, false);
        if (lod != LodVL)
            for (int n = 0; n < 4; ++n) add(strf("TWHL%d", n), lod, 0, true);
        if (opt.lights) add("TLIGHT", lod, DrawUnlit, false);
        return;
    }

    // Player car. The reflection pass is only on BODY_H.
    add("BODY", lod, lod == LodH ? unsigned(DrawReflect) : 0u, false);
    if (lod != LodVL) {
        add("DECAL", lod, DrawForceBlend, false);
        for (int n = 0; n < 6; ++n) {
            add(strf("WHL%d", n), lod, 0, true);
            if (n < 4) add(strf("HUB%d", n), lod, 0, true);
        }
        static const char* const kBreak[] = {"BREAK0",  "BREAK1",  "BREAK2",  "BREAK3",
                                             "BREAK01", "BREAK12", "BREAK23", "BREAK03"};
        for (const char* b : kBreak) add(b, lod, 0, true);
        static const char* const kLive[] = {"SHOCK0", "SHOCK1", "SHOCK2", "SHOCK3", "ARM0",
                                            "ARM1",   "ARM2",   "ARM3",   "SHAFT2", "SHAFT3",
                                            "AXLE0",  "AXLE1",  "ENGINE"};
        for (const char* l : kLive) add(l, lod, 0, true);
        // VARIANT<s> only if it has an H mesh; then at the current level (never VL).
        if (set < 10) {
            const PartLods* vp = car.part(strf("VARIANT%d", set));
            if (vp && vp->at(LodH)) add(vp->name, lod, 0, true);
        }
        if (lod == LodH) {
            add("FNDR0", LodH, 0, true);
            add("FNDR1", LodH, 0, true);
        }
    }
    if (opt.lights) add("TLIGHT", lod, DrawUnlit, false);  // HLIGHT is never drawn (car-model.md)
}

}  // namespace cc
