// Pedestrian model and animation files in `anim/` (spec/formats/ped-anim.md): sequence tables
// (.csv), animations (.anim), skeletons (.skel), text models (.mod, packet and global layouts) and
// far-detail figures (.rays). The paint variants (.shaders) use the package material reader
// (formats/pkg.h, parseShadersFile).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

// ---- Sequence table (.csv) ----

// One row as read (ped-anim.md, "Sequence table"). `fields` = how many fields the row had.
struct PedSeqRow {
    std::string name, anim, next;
    int first = 0, last = 0;
    double fwdOffset = 0, fwdDist = 0, sideOffset = 0, sideDist = 0;  // fields 5–8 as atof gives them
    int fields = 0;
};

// The reading rules: lines of at most 127 bytes (a longer line is cut and its rest read as the next
// line); a line whose first character is '#' is skipped, every other line is a row; fields split at
// commas with empty fields vanishing (C strtok), the ninth field also ending at CR or LF.
std::vector<PedSeqRow> parsePedSequenceTable(const std::string& text);

// ---- Animation (.anim) ----

struct PedAnimation {
    int frames = 0;    // F
    int channels = 0;  // C
    float stride = 0;  // root distance along z over the whole animation
    uint8_t flag = 0;
    bool exact = false;  // the file ended right after the frames
    std::vector<float> data;  // F × C, channel-major per frame
    float& at(int f, int c) { return data[size_t(f) * size_t(channels) + size_t(c)]; }
    float at(int f, int c) const { return data[size_t(f) * size_t(channels) + size_t(c)]; }
};

// Reads an animation file; false (with err) if the counts are out of range or it is truncated.
bool parsePedAnimation(const uint8_t* data, size_t size, PedAnimation& out, std::string& err);
// The forward fix-up MM2 applies at load to channel 2 (root z) of every frame.
void pedAnimForwardFixup(PedAnimation& a);
// The sequence table's drift removal on frames 0 … N (changes the shared data).
void pedAnimDriftRemoval(PedAnimation& a, int N);

// ---- Skeleton (.skel) ----

struct PedBone {
    std::string name;
    int parent = -1;
    vec3 offset;
    bool hasRotMin = false, hasRotMax = false;
    vec3 rotMin, rotMax;
};

struct PedSkeleton {
    int declared = 0;  // the NumBones count
    std::vector<PedBone> bones;  // depth first, parent before children; bone 0 is the root
    int find(const std::string& name) const;
};

// False if the file is not a skeleton (no `bone` after the count): the model then has no bones.
bool parsePedSkeleton(const std::string& text, PedSkeleton& out, std::string& err);

// ---- Model (.mod) ----

struct PedModAdjunct {
    int v = 0, n = 0, c = 0, t1 = 0, t2 = 0;
    int bone = 0;
};

struct PedModMaterial {
    std::string name;
    bool packetForm = false;  // `packets:` (packet layout) rather than `adjuncts:`
    int packets = 0, adjuncts = 0, primitives = 0, textures = 0;
    std::string illum;
    vec3 ambient, diffuse, specular;
    float diffuseAlpha = 1.0f;  // always 1 in a .mod
    std::vector<std::string> textureNames;
    // Geometry owned by this material: its adjuncts and triangles (indices into `adj`).
    std::vector<PedModAdjunct> adj;
    std::vector<int> tris;  // 3 per triangle
};

struct PedMod {
    float version = 0;
    int verts = 0, normals = 0, colors = 0, tex1s = 0, tex2s = 0, tangents = 0, materials = 0, adjuncts = 0,
        primitives = 0, matrices = 0;
    std::vector<vec3> V, N;
    std::vector<vec4> C;   // only when colors ≥ 2
    std::vector<vec2> T1;
    std::vector<PedModMaterial> mats;
    bool packetLayout = false;
    int packetCount = 0;
    std::vector<int> mtxv;           // vertex counts per bone (both layouts, when present)
    std::vector<int> vertexBone;     // from mtxv
    int packetBoneMismatches = 0;    // packet layout: adjuncts whose packet bone differs from mtxv's
    int adjunctTotal() const;
    int triangleTotal() const;
};

bool parsePedMod(const std::string& text, PedMod& out, std::string& err);

// MM2's move of translucent materials toward the end of the list (ped-anim.md, ".mod model"), with
// their geometry. `kind` classifies a material: 0 neither (untextured), 1 opaque (a texture without
// alpha), 2 translucent (diffuse alpha below 1 or a texture with alpha). Returns the swaps made.
int pedReorderTranslucent(std::vector<PedModMaterial>& mats, const std::function<int(const PedModMaterial&)>& kind);

// ---- Far detail (.rays) ----

struct PedRay {
    float a = 0, b = 0, h = 0;
    int origin = 0, unused = 0;
};

struct PedRays {
    int count = 0;
    std::vector<PedRay> bones;
    std::vector<std::vector<int>> rows;  // per paint variant: one material index per bone
};

// `variants` index rows are read after the bone lines (missing numbers read as 0). False if the
// count does not equal `boneCount` (the model then has no far-detail figure).
bool parsePedRays(const std::string& text, int boneCount, int variants, PedRays& out, std::string& err);

// Whitespace tokens of a text file, for the readers above (exposed for the tests).
std::vector<std::string> pedTokens(const std::string& text);

}  // namespace cc
