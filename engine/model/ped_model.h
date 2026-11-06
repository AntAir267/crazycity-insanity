// A pedestrian model (spec/formats/ped-anim.md): its sequence table with the derived speeds and
// links, skeleton, text model, paint variants and far-detail figure; posing from an animation
// frame and the geometry to draw it near (posed triangles) and far (camera-facing strips). No GL.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/math.h"
#include "formats/ped_anim.h"
#include "formats/pkg.h"

namespace cc {

class Vfs;
class AssetCache;

// The sequences the behaviour code asks for by name (ai/pedestrian-walking.md and
// pedestrian-reactions.md). `LDIVE` and `RDIVE` are asked for but no shipped table has them.
enum PedSeqName : int {
    PsSTAND,
    PsSTAND2,
    PsSTAND_WALK,
    PsWALK,
    PsWALK_STAND,
    PsSTAND_ANTIC,
    PsANTIC,
    PsANTIC_WALK,
    PsWALK_ANTIC,
    PsANTIC_LDIVE,
    PsLDIVE_GROUNDL,
    PsGROUND_STANDL,
    PsANTIC_RDIVE,
    PsRDIVE_GROUNDR,
    PsGROUND_STANDR,
    PsWALK_RDIVE,
    PsWALK_LDIVE,
    PsRUN,
    PsBACKUP,
    PsBACKUP_WALK,
    PsRUN_WALK,
    PsLDIVE,
    PsRDIVE,
    PsNamedCount
};
const char* pedSeqName(int named);

// One row of the sequence table after MM2's derivation (ped-anim.md, "Sequence table").
struct PedSequence {
    PedSeqRow row;
    int N = 0;           // frames played: 0 … N of the animation
    int frameCount = 1;  // N + 1
    int direction = 1;   // +1 (N > 0), 0 (N = 0), −1 (N < 0)
    float vf = 0, vs = 0;  // forward and sideways speed, m/s
    std::shared_ptr<PedAnimation> anim;  // null: the row's animation could not be loaded
    int next = 0;          // row index of the next sequence
    bool nextFound = true; // false: the name was absent or not found (engine: linked to itself)
};

// A bone's posed frame in model space: rows (row vectors, as everywhere in MM2) and position.
struct PedBoneXf {
    vec3 r[3];
    vec3 t;
};

class PedModel {
public:
    // Reads anim/<name>.csv, .skel, .mod (required), .shaders and .rays (optional). Animations come
    // from the game-wide cache of `assets`; each row's drift removal runs on the shared data.
    bool load(const Vfs& vfs, AssetCache& assets, const std::string& name, std::string& err);

    const std::string& name() const { return name_; }
    const std::vector<PedSequence>& sequences() const { return seqs_; }
    // Row index of a named sequence, −1 if the table has none.
    int seq(int named) const { return named >= 0 && named < PsNamedCount ? named_[named] : -1; }
    int find(const std::string& name) const;
    const PedSkeleton& skeleton() const { return skel_; }
    const PedMod& mod() const { return mod_; }
    // Paint variants: the .shaders sets, or one set of the model's own materials.
    int variantCount() const { return materials_.setCount > 0 ? materials_.setCount : 1; }
    const Pkg& materials() const { return materials_; }
    bool hasShaders() const { return hasShaders_; }
    bool hasRays() const { return hasRays_; }
    const PedRays& rays() const { return rays_; }
    // Ray colour (A, R, G, B packed as 0xAARRGGBB) of bone `bone` in variant `variant`, with the
    // untextured-colour factor `factor` (pkg.md: 0.5 for a night city).
    uint32_t rayColour(int variant, int bone, float factor) const;
    // MM2's gender test on the model name (traffic-sounds.md, "Pedestrian voices").
    bool female() const { return female_; }
    float radius() const { return radius_; }  // largest distance of a rest-pose vertex from the origin

    // Pose from frame `frame` of row `seq` (the rest pose without an animation).
    void pose(int seq, int frame, std::vector<PedBoneXf>& out) const;
    // The posed model in model space: one batch per material, the batch's material = the model's
    // material index (drawn with the same index of the paint variant).
    void buildMesh(const std::vector<PedBoneXf>& pose, Mesh& out) const;
    // The far-detail strips (two triangles per bone with a ≠ 0), model space; `right` = the
    // camera's right axis in the model frame. `pose` is changed (the bones' raised positions).
    struct StripVertex {
        vec3 p;
        uint32_t argb = 0;
    };
    void farStrips(std::vector<PedBoneXf>& pose, const vec3& right, int variant, float factor,
                   std::vector<StripVertex>& out) const;

    // Diagnostics: rows whose next name was not found, rows without an animation.
    int unlinkedRows() const;
    int rowsWithoutAnimation() const;

private:
    std::string name_;
    std::vector<PedSequence> seqs_;
    int named_[PsNamedCount] = {};
    PedSkeleton skel_;
    PedMod mod_;
    Pkg materials_;
    bool hasShaders_ = false, hasRays_ = false;
    PedRays rays_;
    bool female_ = false;
    float radius_ = 0;
};

// MM2's case-insensitive substring test, which restarts at the character after a mismatch
// without re-testing it (so "ffemale" does not contain "female").
bool pedNameContains(const std::string& s, const char* pattern);
bool pedModelIsFemale(const std::string& name);

}  // namespace cc
