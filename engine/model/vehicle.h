// Vehicle assembly (spec/formats/car-model.md): parts, pivots, trailer, paint set, and the list
// of meshes to draw for a still car at a chosen level of detail.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/math.h"
#include "formats/mtx.h"
#include "model/parts.h"

namespace cc {

class AssetCache;

enum class VehicleKind { Player, Traffic, Trailer };

// Part names in the order MM2 asks for them.
const std::vector<std::string>& playerCarParts();
const std::vector<std::string>& trafficCarParts();
const std::vector<std::string>& trailerParts();

struct VehicleModel {
    std::string baseName;
    VehicleKind kind = VehicleKind::Player;
    std::shared_ptr<const Pkg> pkg;
    PartTable parts;
    std::vector<Pivot> pivots;  // one per part (found == false when the part has none)
    float groundY = 0;          // g = min over wheels (origin.y - radius)
    bool groundFromWheels = false;
    vec3 boundsMin, boundsMax;  // body (H) bounds

    std::unique_ptr<VehicleModel> trailer;
    vec3 trailerOrigin;  // trailer's own origin in car space

    std::vector<std::string> problems;

    int setCount() const { return pkg ? pkg->setCount : 0; }
    const PartLods* part(const std::string& name) const { return parts.find(name); }
    const Pivot* pivotOf(const std::string& name) const;
};

// Loads a vehicle and (for player cars) its trailer. False if the package is missing/broken
// or has no body. Problems are logged and also kept in model.problems.
// `strict`: a required part with no level fails the load as in MM2 (pkg.md rule 3); the viewers
// pass false to still show such a model (with the problem logged).
bool loadVehicle(AssetCache& assets, const std::string& baseName, VehicleKind kind,
                 VehicleModel& model, bool strict = true);

// Paint set used for colour number c (position in the .info Colors= list).
int paintSetFor(int colorIndex, int setCount);
// The trailer's set for the car's set index.
int trailerSetFor(int carSet, int trailerSetCount);

enum DrawFlags : unsigned {
    DrawUnlit = 1u << 0,       // ignore lighting (shadow, glows)
    DrawShadow = 1u << 1,      // flatten to the ground, blended, no depth writes
    DrawForceBlend = 1u << 2,  // always alpha-blend (decal, shadow)
    DrawReflect = 1u << 3,     // environment reflection pass (body at H)
    DrawDamageSlots = 1u << 4, // apply the "_dmg" undamaged-texture rule
};

struct DrawPart {
    const Pkg* pkg = nullptr;
    const Mesh* mesh = nullptr;
    vec3 offset;      // translation in car space
    bool hasTransform = false;  // use `transform` (car space) instead of `offset`
    mat4 transform = mat4::identity();
    int materialSet = 0;
    unsigned flags = 0;
    const char* role = "";  // part name, for debugging/UI
};

struct StillCarOptions {
    bool shadow = true;
    bool lights = false;  // draw the head/tail light glows
    bool trailer = true;
};

// Meshes to draw for an undamaged car standing still, at level `lod`, colour number `color`.
std::vector<DrawPart> buildStillCar(const VehicleModel& car, Lod lod, int color,
                                    const StillCarOptions& opt);

// Lets a caller move parts (wheels of a driving car): called for every part added; it may change
// the DrawPart (e.g. give it a full transform) and returns true if it did.
using WheelPoseFn = std::function<bool(const std::string& part, DrawPart& d)>;

// car-model.md, "Fenders": FNDR0's offset from the WHL0 centre (+0.025 in y); FNDR1 uses
// (−o.x, o.y, o.z). False if FNDR0 has no H mesh or no pivot (the offset is then (0, 0, 0)).
bool fenderOffset(const VehicleModel& m, vec3& o);
// car-model.md, "Live suspension parts": the car-space matrix of SHOCKn, ARMn, SHAFT2/3, AXLE0/1
// for the wheels' drawn lifts t[0..3] (comp − visualDisp; 0 for a still view). False if the part
// is not one of them or has no .mtx (then it is not drawn).
bool liveSuspensionMatrix(const VehicleModel& m, const std::string& part, const float t[4], mat4& out, bool& isLive);

// The parts of one vehicle (no shadow, no trailer) at level `lod` with material set `set`.
void addCarParts(const VehicleModel& car, Lod lod, int set, vec3 base, const StillCarOptions& opt,
                 std::vector<DrawPart>& out, const WheelPoseFn& wheelPose = nullptr);
// trailer.md 11, a towed trailer while driving: at H the TRAILER mesh, TLIGHT while the truck
// brakes (an ordinary mesh, not a glow) and the wheels TWHL0-TWHL3 (placed by `wheelPose`); at M,
// L and VL the TRAILER mesh of that level only.
void addTrailerParts(const VehicleModel& trailer, Lod lod, int set, bool brakeLight, std::vector<DrawPart>& out,
                     const WheelPoseFn& wheelPose);
// The flattened SHADOW (H slot) at the given ground height.
void addStillShadow(const VehicleModel& v, int matSet, vec3 base, float groundY, std::vector<DrawPart>& out);

}  // namespace cc
