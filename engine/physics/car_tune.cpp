#include "physics/car_tune.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/bytes.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/dat_text.h"
#include "formats/mtx.h"
#include "formats/banger.h"
#include "formats/pkg.h"
#include "model/parts.h"
#include "model/vehicle.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

using dat::FieldType;
using dat::Schema;

namespace {

Schema wheelSchema(WheelTune& w) {
    return Schema{{{"SuspensionExtent", FieldType::Float, &w.suspensionExtent, nullptr},
                   {"SuspensionLimit", FieldType::Float, &w.suspensionLimit, nullptr},
                   {"SuspensionFactor", FieldType::Float, &w.suspensionFactor, nullptr},
                   {"SuspensionDampCoef", FieldType::Float, &w.suspensionDampCoef, nullptr},
                   {"SteeringLimit", FieldType::Float, &w.steeringLimit, nullptr},
                   {"SteeringOffset", FieldType::Float, &w.steeringOffset, nullptr},
                   {"BrakeCoef", FieldType::Float, &w.brakeCoef, nullptr},
                   {"HandbrakeCoef", FieldType::Float, &w.handbrakeCoef, nullptr},
                   {"CamberLimit", FieldType::Float, &w.camberLimit, nullptr},
                   {"WobbleLimit", FieldType::Float, &w.wobbleLimit, nullptr},
                   {"TireDispLimitLong", FieldType::Float, &w.tireDispLimitLong, nullptr},
                   {"TireDampCoefLong", FieldType::Float, &w.tireDampCoefLong, nullptr},
                   {"TireDragCoefLong", FieldType::Float, &w.tireDragCoefLong, nullptr},
                   {"TireDispLimitLat", FieldType::Float, &w.tireDispLimitLat, nullptr},
                   {"TireDampCoefLat", FieldType::Float, &w.tireDampCoefLat, nullptr},
                   {"TireDragCoefLat", FieldType::Float, &w.tireDragCoefLat, nullptr},
                   {"OptimumSlipPercent", FieldType::Float, &w.optimumSlipPercent, nullptr},
                   {"StaticFric", FieldType::Float, &w.staticFric, nullptr},
                   {"SlidingFric", FieldType::Float, &w.slidingFric, nullptr}}};
}

Schema trainSchema(TrainTune& t) {
    return Schema{{{"AngInertia", FieldType::Float, &t.angInertia, nullptr},
                   {"BrakeDynamicCoef", FieldType::Float, &t.brakeDynamicCoef, nullptr},
                   {"BrakeStaticCoef", FieldType::Float, &t.brakeStaticCoef, nullptr}}};
}

Schema axleSchema(AxleTune& a) {
    return Schema{{{"TorqueCoef", FieldType::Float, &a.torqueCoef, nullptr},
                   {"DampCoef", FieldType::Float, &a.dampCoef, nullptr}}};
}

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

}  // namespace

bool parseCarSim(const std::string& text, CarSimTune& c, std::string* err) {
    Schema aero{{{"AngCDamp", FieldType::Vector, &c.angCDamp, nullptr},
                 {"AngVelDamp", FieldType::Vector, &c.angVelDamp, nullptr},
                 {"AngVel2Damp", FieldType::Vector, &c.angVel2Damp, nullptr},
                 {"Drag", FieldType::Float, &c.drag, nullptr},
                 {"Down", FieldType::Float, &c.down, nullptr}}};
    Schema engine{{{"AngInertia", FieldType::Float, &c.engAngInertia, nullptr},
                   {"MaxHorsePower", FieldType::Float, &c.maxHorsePower, nullptr},
                   {"IdleRPM", FieldType::Float, &c.idleRPM, nullptr},
                   {"OptRPM", FieldType::Float, &c.optRPM, nullptr},
                   {"MaxRPM", FieldType::Float, &c.maxRPM, nullptr},
                   {"GCL", FieldType::Float, &c.gcl, nullptr}}};
    Schema trans{{{"ManualNumGears", FieldType::Int, &c.manualNumGears, nullptr},
                  {"AutoNumGears", FieldType::Int, &c.autoNumGears, nullptr},
                  {"Reverse", FieldType::Float, &c.reverse, nullptr},
                  {"Low", FieldType::Float, &c.low, nullptr},
                  {"High", FieldType::Float, &c.high, nullptr},
                  {"GearBias", FieldType::Float, &c.gearBias, nullptr},
                  {"UpshiftBias", FieldType::Float, &c.upshiftBias, nullptr},
                  {"DownshiftBiasMin", FieldType::Float, &c.downshiftBiasMin, nullptr},
                  {"DownshiftBiasMax", FieldType::Float, &c.downshiftBiasMax, nullptr},
                  {"GearChangeTime", FieldType::Float, &c.gearChangeTime, nullptr}}};
    Schema drive = trainSchema(c.drivetrain), free = trainSchema(c.freetrain);
    Schema wf = wheelSchema(c.wheelFront), wb = wheelSchema(c.wheelBack);
    Schema af = axleSchema(c.axleFront), ab = axleSchema(c.axleBack);
    Schema root{{{"Mass", FieldType::Float, &c.mass, nullptr},
                 {"InertiaBox", FieldType::Vector, &c.inertiaBox, nullptr},
                 {"CenterOfGravity", FieldType::Vector, &c.centerOfGravity, nullptr},
                 {"BoundFriction", FieldType::Float, &c.boundFriction, nullptr},
                 {"BoundElasticity", FieldType::Float, &c.boundElasticity, nullptr},
                 {"DrivetrainType", FieldType::Int, &c.drivetrainType, nullptr},
                 {"SSSValue", FieldType::Float, &c.sssValue, nullptr},
                 {"SSSThreshold", FieldType::Float, &c.sssThreshold, nullptr},
                 {"CarFrictionHandling", FieldType::Float, &c.carFrictionHandling, nullptr},
                 {"Aero", FieldType::Block, nullptr, &aero},
                 {"Engine", FieldType::Block, nullptr, &engine},
                 {"Trans", FieldType::Block, nullptr, &trans},
                 {"Drivetrain", FieldType::Block, nullptr, &drive},
                 {"Freetrain", FieldType::Block, nullptr, &free},
                 {"WheelFront", FieldType::Block, nullptr, &wf},
                 {"WheelBack", FieldType::Block, nullptr, &wb},
                 {"AxleFront", FieldType::Block, nullptr, &af},
                 {"AxleBack", FieldType::Block, nullptr, &ab}}};
    return dat::parseText(text, root, err);
}

bool parseVehTrailer(const std::string& text, TrailerTune& t, std::string* err) {
    Schema wf = wheelSchema(t.wheelFront), wb = wheelSchema(t.wheelBack);
    Schema drive = trainSchema(t.drivetrain);
    Schema root{{{"Mass", FieldType::Float, &t.mass, nullptr},
                 {"InertiaBox", FieldType::Vector, &t.inertiaBox, nullptr},
                 {"CarHitchOffset", FieldType::Vector, &t.carHitch, nullptr},
                 {"TrailerHitchOffset", FieldType::Vector, &t.trailerHitch, nullptr},
                 {"WheelFront", FieldType::Block, nullptr, &wf},
                 {"WheelBack", FieldType::Block, nullptr, &wb},
                 {"Drivetrain", FieldType::Block, nullptr, &drive}}};
    return dat::parseText(text, root, err);
}

bool parseTrailerJoint(const std::string& text, TrailerJointTune& j, std::string* err) {
    Schema root{{{"ForceLimit", FieldType::Float, &j.forceLimit, nullptr},
                 {"JointStatus", FieldType::Int, &j.jointStatus, nullptr},
                 {"RestoreForceLean", FieldType::Float, &j.restoreForceLean, nullptr},
                 {"DampConstLean", FieldType::Float, &j.dampConstLean, nullptr},
                 {"DampLinearLean", FieldType::Float, &j.dampLinearLean, nullptr},
                 {"RestoreForceRoll", FieldType::Float, &j.restoreForceRoll, nullptr},
                 {"DampConstRoll", FieldType::Float, &j.dampConstRoll, nullptr},
                 {"DampLinearRoll", FieldType::Float, &j.dampLinearRoll, nullptr},
                 {"LeanLimit", FieldType::Float, &j.leanLimit, nullptr},
                 {"LimitElasticityLean", FieldType::Float, &j.limitElasticityLean, nullptr},
                 {"LimitElasticityRoll", FieldType::Float, &j.limitElasticityRoll, nullptr},
                 {"FreeRange", FieldType::Float, &j.freeRange, nullptr},
                 {"FreeLean", FieldType::Float, &j.freeLean, nullptr},
                 {"FreeRoll", FieldType::Float, &j.freeRoll, nullptr}}};
    return dat::parseText(text, root, err);
}

bool loadTrailerTune(const Vfs& vfs, const CarTune& car, TrailerTune& t, std::string& why) {
    t = TrailerTune();
    t.car = car.baseName;
    t.name = car.baseName + "_trailer";
    auto readPivot = [&](const std::string& path, Pivot& pv) {
        BytesPtr b = vfs.open(path);
        std::string err;
        return b && parseMtx(b->data(), b->size(), pv, err);
    };
    // The hitch rule (car-model.md, "Trailers"): a TRAILER_HITCH mesh (any level, after the fill
    // rule) and a hitch .mtx origin that is not exactly (0, 0, 0).
    if (!car.pkg) {
        why = "the car has no package";
        return false;
    }
    {
        const PartTable pt = takeParts(*car.pkg, playerCarParts());
        const PartLods* hitch = pt.find("TRAILER_HITCH");
        if (!hitch || !hitch->any()) {
            why = car.baseName + " has no TRAILER_HITCH mesh";
            return false;
        }
    }
    Pivot carHitch;
    if (!readPivot("geometry/" + car.baseName + "_TRAILER_HITCH.mtx", carHitch) ||
        (carHitch.origin.x == 0.0f && carHitch.origin.y == 0.0f && carHitch.origin.z == 0.0f)) {
        why = car.baseName + "_TRAILER_HITCH.mtx is missing or its origin is (0, 0, 0)";
        return false;
    }
    // The trailer's package: TRAILER, TWHL0 and TWHL1 are required.
    BytesPtr pk = vfs.open("geometry/" + t.name + ".pkg");
    Pkg pkg;
    std::string perr;
    if (!pk || !parsePkg(pk->data(), pk->size(), pkg, perr)) {
        why = "geometry/" + t.name + ".pkg is missing or broken";
        return false;
    }
    const PartTable tp = takeParts(pkg, trailerParts());
    for (const char* req : {"TRAILER", "TWHL0", "TWHL1"}) {
        const PartLods* pl = tp.find(req);
        if (!pl || !pl->any()) {
            why = t.name + " has no " + req + " part";
            return false;
        }
    }
    if (const PartLods* body = tp.find("TRAILER"))
        for (int l = 0; l < LodCount; ++l)
            if (body->at(Lod(l)) && body->at(Lod(l))->radius > t.radius) t.radius = body->at(Lod(l))->radius;
    // Hitch points: the .mtx origins, the trailer's only if it has a TRAILER_HITCH mesh.
    t.carHitch = carHitch.origin;
    {
        const PartLods* th = tp.find("TRAILER_HITCH");
        Pivot trHitch;
        if (th && th->any()) {
            if (readPivot("geometry/" + t.name + "_TRAILER_HITCH.mtx", trHitch)) t.trailerHitch = trHitch.origin;
            else t.notes.push_back(t.name + "_TRAILER_HITCH.mtx missing: trailer hitch (0, 0, 0)");
        }
    }
    // The tune files (tune/vehicle/ only; the copies in tune/ itself are never read).
    {
        const std::string path = "tune/vehicle/" + car.baseName + ".vehTrailer";
        BytesPtr b = vfs.open(path);
        if (b) {
            t.vehFile = true;
            std::string err;
            if (!parseVehTrailer(textOf(b), t, &err)) t.notes.push_back(path + ": " + err);
        } else {
            t.notes.push_back(path + " missing: defaults used");
        }
    }
    {
        const std::string path = "tune/vehicle/" + car.baseName + ".dgTrailerJoint";
        BytesPtr b = vfs.open(path);
        if (b) {
            t.jointFile = true;
            std::string err;
            if (!parseTrailerJoint(textOf(b), t.joint, &err)) t.notes.push_back(path + ": " + err);
        } else {
            t.notes.push_back(path + " missing: defaults used");
        }
    }
    t.joint.cosFreeLean = float(std::cos(double(t.joint.freeLean)));
    // Wheels: a missing .mtx leaves the previous wheel's values in place (2.4 step 5).
    WheelGeom last;
    for (int i = 0; i < 4; ++i) {
        const std::string path = strf("geometry/%s_TWHL%d.mtx", t.name.c_str(), i);
        Pivot p;
        if (readPivot(path, p)) {
            last.found = true;
            last.pivot = p.origin;
            last.radius = std::fabs((p.boxMax.y - p.boxMin.y) * 0.5f);
            last.width = p.boxMax.x - p.boxMin.x;
        } else {
            t.notes.push_back(path + " missing: the previous wheel's values stay");
        }
        t.wheels[i] = last;
    }
    // The bound and its first material (looked up by name in the world's list when set up).
    {
        MaterialTable scratch;
        t.bound = loadGeometryBound(vfs, t.name, scratch);
        if (t.bound) {
            t.boundMaterialFile = scratch.at(t.bound->firstMaterial());
            t.boundMaterialName = t.boundMaterialFile.name;
        } else {
            t.notes.push_back("bound/" + t.name + "_BOUND.bnd missing or broken: the trailer has no collision");
        }
    }
    return true;
}

bool parseGyro(const std::string& text, GyroTune& g, std::string* err) {
    Schema root{{{"Drift", FieldType::Float, &g.drift, nullptr},
                 {"Spin180", FieldType::Float, &g.spin180, nullptr},
                 {"Reverse180", FieldType::Float, &g.reverse180, nullptr},
                 {"Pitch", FieldType::Float, &g.pitch, nullptr},
                 {"Roll", FieldType::Float, &g.roll, nullptr}}};
    return dat::parseText(text, root, err);
}

bool parseStuck(const std::string& text, StuckTune& s, std::string* err) {
    Schema root{{{"Turn", FieldType::Float, &s.turn, nullptr},
                 {"Rotation", FieldType::Float, &s.rotation, nullptr},
                 {"Translation", FieldType::Float, &s.translation, nullptr},
                 {"TimeThresh", FieldType::Float, &s.timeThresh, nullptr},
                 {"PosThresh", FieldType::Float, &s.posThresh, nullptr},
                 {"MoveThresh", FieldType::Float, &s.moveThresh, nullptr}}};
    return dat::parseText(text, root, err);
}

CarTune loadCarTune(const Vfs& vfs, const std::string& baseName, bool playerCar) {
    CarTune t;
    t.baseName = baseName;
    auto readSim = [&](const std::string& name) {
        const std::string path = "tune/vehicle/" + name + ".vehCarSim";
        BytesPtr b = vfs.open(path);
        if (!b) {
            t.notes.push_back(path + " missing: defaults used");
            return false;
        }
        std::string err;
        if (!parseCarSim(textOf(b), t.sim, &err)) {
            t.notes.push_back(path + ": " + err);
            return false;
        }
        return true;
    };
    t.simFile = readSim(baseName);
    t.damage = loadDamageTune(vfs, baseName);  // follows the model name (the player's vpcop keeps vpcop's)
    t.waterCoG = t.sim.centerOfGravity;
    t.waterSize = t.sim.inertiaBox;
    std::string wheelsFrom = baseName;
    if (playerCar && baseName == "vpcop") {
        // The player's vpcop is set up a second time from vpmustang99: its .vehCarSim and wheel
        // pivots. The bound file stays vpcop's, but its material is re-applied from the (now
        // Mustang) BoundFriction/BoundElasticity at every reset; the water-effect points keep
        // vpcop's first set-up values (vehicle-tune.md).
        readSim("vpmustang99");
        wheelsFrom = "vpmustang99";
    }
    if (t.sim.drivetrainType < 0 || t.sim.drivetrainType > 2) {
        t.notes.push_back(strf("DrivetrainType %d is not valid (MM2 cannot load this car); 0 used", t.sim.drivetrainType));
        t.sim.drivetrainType = 0;
    }
    auto clampSlots = [&](int& n, const char* what) {
        if (n < 3 || n > 8) {
            t.notes.push_back(strf("%s %d outside 3..8; clamped", what, n));
            n = n < 3 ? 3 : 8;
        }
    };
    clampSlots(t.sim.manualNumGears, "ManualNumGears");
    clampSlots(t.sim.autoNumGears, "AutoNumGears");

    {
        const std::string path = "tune/vehicle/" + baseName + ".vehGyro";
        BytesPtr b = vfs.open(path);
        if (b) t.gyroFile = parseGyro(textOf(b), t.gyro);
        else t.notes.push_back(path + " missing: defaults used");
    }
    {
        const std::string path = "tune/vehicle/" + baseName + ".vehStuck";
        BytesPtr b = vfs.open(path);
        if (b) t.stuckFile = parseStuck(textOf(b), t.stuck);
        else t.notes.push_back(path + " missing: defaults used");
    }
    {
        // The car's bound; its materials are replaced by the car's own (BoundFriction/Elasticity).
        MaterialTable scratch;
        t.bound = loadGeometryBound(vfs, baseName, scratch);
        if (!t.bound) t.notes.push_back("bound/" + baseName + "_BOUND.bnd missing or broken: the body has no collision");
        BytesPtr pk = vfs.open("geometry/" + baseName + ".pkg");
        auto pkg = std::make_shared<Pkg>();
        std::string err;
        // The car's radius is its BODY part's (collision-objects.md, "Gathering" step 1): the
        // largest radius over BODY_H, BODY_M, BODY_L and BODY_VL.
        if (pk && parsePkg(pk->data(), pk->size(), *pkg, err)) {
            for (const auto& m : pkg->meshes) {
                const std::string n = toLower(m.name);
                if (n != "body_h" && n != "body_m" && n != "body_l" && n != "body_vl") continue;
                if (m.radius > t.modelRadius) t.modelRadius = m.radius;
            }
            t.pkg = pkg;
            // The loose parts (damage.md, "Parts thrown off"): each only if it has an H mesh after
            // the fill rule, with its place from its .mtx and its prop data <car>_<PART>.
            const PartTable pt = takeParts(*pkg, playerCarParts());
            std::vector<std::pair<std::string, bool>> names;
            for (const char* n : {"BREAK0", "BREAK1", "BREAK2", "BREAK3", "BREAK01", "BREAK12", "BREAK23", "BREAK03"})
                names.emplace_back(n, false);
            for (int v = 0; v < 10; ++v) names.emplace_back(strf("VARIANT%d", v), false);
            for (const char* n : {"WHL0", "HUB0", "WHL1", "HUB1", "WHL2", "HUB2", "WHL3", "HUB3", "FNDR0", "FNDR1", "ENGINE"})
                names.emplace_back(n, true);
            for (const auto& nm : names) {
                const PartLods* pl = pt.find(nm.first);
                if (!pl || !pl->at(LodH)) continue;
                LoosePartTune lp;
                lp.name = nm.first;
                lp.gear = nm.second;
                if (startsWith(nm.first, "VARIANT")) lp.variant = std::atoi(nm.first.c_str() + 7);
                BytesPtr mb = vfs.open("geometry/" + baseName + "_" + nm.first + ".mtx");
                Pivot pv;
                std::string merr;
                if (mb && parseMtx(mb->data(), mb->size(), pv, merr)) lp.place = pv.origin;
                auto d = std::make_shared<LooseData>();
                d->name = baseName + "_" + nm.first;
                if (!loadBangerData(vfs, d->name, d->bd))
                    t.notes.push_back("tune/banger/" + d->name + ".dgBangerData missing: the part is thrown with the default prop data");
                looseBoundFor(*d, nullptr);  // car parts are boxes; their CG is not used (loose-bodies.md 4)
                d->pkg = pkg;
                for (int l = 0; l < LodCount; ++l) {
                    d->lod[l] = pl->at(Lod(l));
                    if (d->lod[l] && d->lod[l]->radius > d->radius) d->radius = d->lod[l]->radius;
                }
                lp.data = d;
                t.looseParts.push_back(lp);
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        const std::string path = strf("geometry/%s_whl%d.mtx", wheelsFrom.c_str(), i);
        BytesPtr b = vfs.open(path);
        Pivot p;
        std::string err;
        if (!b || !parseMtx(b->data(), b->size(), p, err)) {
            t.notes.push_back(path + (b ? " broken: " + err : std::string(" missing")));
            continue;
        }
        WheelGeom& w = t.wheels[i];
        w.found = true;
        w.pivot = p.origin;
        w.radius = std::fabs((p.boxMax.y - p.boxMin.y) * 0.5f);
        w.width = p.boxMax.x - p.boxMin.x;
    }
    return t;
}

}  // namespace phys
}  // namespace cc
