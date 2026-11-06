// Pedestrian models: loading, posing and geometry (spec/formats/ped-anim.md).
#include "model/ped_model.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "core/strutil.h"
#include "formats/texture_lookup.h"
#include "model/asset_cache.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

const char* const kSeqNames[PsNamedCount] = {"STAND",         "STAND2",        "STAND_WALK",    "WALK",          "WALK_STAND",
                                             "STAND_ANTIC",   "ANTIC",         "ANTIC_WALK",    "WALK_ANTIC",    "ANTIC_LDIVE",
                                             "LDIVE_GROUNDL", "GROUND_STANDL", "ANTIC_RDIVE",   "RDIVE_GROUNDR", "GROUND_STANDR",
                                             "WALK_RDIVE",    "WALK_LDIVE",    "RUN",           "BACKUP",        "BACKUP_WALK",
                                             "RUN_WALK",      "LDIVE",         "RDIVE"};

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

// (cos, sin) of an animation angle: (1, 0) when it is exactly 0, else the double results rounded.
void cs(float a, float& c, float& s) {
    if (a == 0.0f) {
        c = 1.0f;
        s = 0.0f;
        return;
    }
    c = float(std::cos(double(a)));
    s = float(std::sin(double(a)));
}

vec3 rowTimes(const vec3& v, const vec3 r[3]) {
    return vec3(v.x * r[0].x + v.y * r[1].x + v.z * r[2].x, v.x * r[0].y + v.y * r[1].y + v.z * r[2].y,
                v.x * r[0].z + v.y * r[1].z + v.z * r[2].z);
}

}  // namespace

const char* pedSeqName(int named) { return named >= 0 && named < PsNamedCount ? kSeqNames[named] : "?"; }

bool pedNameContains(const std::string& s, const char* pattern) {
    const size_t plen = std::strlen(pattern);
    if (plen == 0) return true;
    size_t j = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(s[i])) == std::toupper(static_cast<unsigned char>(pattern[j]))) {
            if (++j == plen) return true;
        } else {
            j = 0;  // restart after the mismatching character, which is not tested again
        }
    }
    return false;
}

bool pedModelIsFemale(const std::string& name) {
    return pedNameContains(name, "FEMALE") || pedNameContains(name, "WOMAN") || pedNameContains(name, "GIRL") ||
           pedNameContains(name, "HOOKER");
}

int PedModel::find(const std::string& n) const {
    for (size_t i = 0; i < seqs_.size(); ++i)
        if (seqs_[i].row.name == n) return int(i);
    return -1;
}

bool PedModel::load(const Vfs& vfs, AssetCache& assets, const std::string& name, std::string& err) {
    name_ = name;
    female_ = pedModelIsFemale(name);
    const std::string base = "anim/" + name;
    BytesPtr csv = vfs.open(base + ".csv"), skel = vfs.open(base + ".skel"), mod = vfs.open(base + ".mod");
    if (!csv || !skel || !mod) {
        err = strf("missing %s%s%s", csv ? "" : ".csv ", skel ? "" : ".skel ", mod ? "" : ".mod");
        return false;
    }
    if (!parsePedSkeleton(textOf(skel), skel_, err)) {
        err = base + ".skel: " + err;
        return false;
    }
    if (!parsePedMod(textOf(mod), mod_, err)) {
        err = base + ".mod: " + err;
        return false;
    }
    // Translucent materials move toward the end of the model's list (the paint variants' lists do
    // not move). A texture that cannot be found counts as no texture (engine choice).
    pedReorderTranslucent(mod_.mats, [&](const PedModMaterial& m) {
        if (m.diffuseAlpha < 1.0f) return 2;
        if (m.textures < 1 || m.textureNames.empty()) return 0;
        std::shared_ptr<TextureAsset> t = loadTextureAsset(vfs, m.textureNames[0]);
        if (!t) return 0;
        return t->hasAlpha() ? 2 : 1;
    });
    // Paint variants.
    materials_ = Pkg();
    hasShaders_ = false;
    if (BytesPtr sh = vfs.open(base + ".shaders")) {
        std::string e;
        if (parseShadersFile(sh->data(), sh->size(), materials_, e)) hasShaders_ = true;
        else logFile(LogLevel::Warn, vfs.storedName(base + ".shaders"), "paint variants not read: " + e);
    }
    if (!hasShaders_) {
        // One variant: the model's own materials (alpha 1, emissive 0, power 0).
        materials_ = Pkg();
        materials_.setCount = 1;
        materials_.materialsPerSet = int(mod_.mats.size());
        for (const PedModMaterial& m : mod_.mats) {
            Material mt;
            if (m.textures >= 1 && !m.textureNames.empty()) mt.texture = m.textureNames[0];
            mt.diffuse = vec4(m.diffuse.x, m.diffuse.y, m.diffuse.z, 1.0f);
            mt.ambient = vec4(m.ambient.x, m.ambient.y, m.ambient.z, 1.0f);
            mt.specular = vec4(m.specular.x, m.specular.y, m.specular.z, 1.0f);
            mt.emissive = vec4(0, 0, 0, 1.0f);
            materials_.materials.push_back(mt);
        }
    }
    // Far detail.
    hasRays_ = false;
    if (BytesPtr r = vfs.open(base + ".rays")) {
        std::string e;
        if (parsePedRays(textOf(r), int(skel_.bones.size()), variantCount(), rays_, e)) hasRays_ = true;
        else logFile(LogLevel::Warn, vfs.storedName(base + ".rays"), "no far-detail figure: " + e);
    }
    // Sequence table (ped-anim.md, "Sequence table", steps 1-7).
    seqs_.clear();
    for (const PedSeqRow& r : parsePedSequenceTable(textOf(csv))) {
        PedSequence s;
        s.row = r;
        int N = r.last - r.first;
        const float t = float(N + 1) * 0.033330001f;
        s.vf = float(r.fwdDist / double(t));
        s.vs = float(r.sideDist / double(t));
        s.anim = r.anim.empty() ? nullptr : assets.pedAnimation(r.anim);
        if (s.anim && s.anim->frames <= N) N = s.anim->frames - 1;
        if (s.anim) pedAnimDriftRemoval(*s.anim, N);
        s.N = N;
        s.frameCount = N + 1;
        s.direction = N > 0 ? 1 : (N == 0 ? 0 : -1);
        seqs_.push_back(std::move(s));
    }
    for (size_t i = 0; i < seqs_.size(); ++i) {
        PedSequence& s = seqs_[i];
        const int j = s.row.next.empty() ? -1 : find(s.row.next);
        s.nextFound = j >= 0;
        s.next = j >= 0 ? j : int(i);  // engine choice: an unknown name links the row to itself
        if (j < 0) logFile(LogLevel::Warn, vfs.storedName(base + ".csv"), "sequence " + s.row.name + ": next '" + s.row.next + "' not found");
    }
    for (int k = 0; k < PsNamedCount; ++k) named_[k] = find(kSeqNames[k]);
    if (seqs_.empty()) {
        err = base + ".csv: no sequences";
        return false;
    }
    // Rest-pose radius (diagnostics).
    std::vector<PedBoneXf> p;
    pose(seq(PsSTAND) >= 0 ? seq(PsSTAND) : 0, 0, p);
    Mesh m;
    buildMesh(p, m);
    radius_ = m.radius;
    return true;
}

int PedModel::unlinkedRows() const {
    int n = 0;
    for (const auto& s : seqs_)
        if (!s.nextFound) ++n;
    return n;
}

int PedModel::rowsWithoutAnimation() const {
    int n = 0;
    for (const auto& s : seqs_)
        if (!s.anim) ++n;
    return n;
}

uint32_t PedModel::rayColour(int variant, int bone, float factor) const {
    if (!hasRays_ || rays_.rows.empty()) return 0xFF000000u;
    const auto& row = rays_.rows[size_t(std::clamp(variant, 0, int(rays_.rows.size()) - 1))];
    if (bone < 0 || size_t(bone) >= row.size()) return 0xFF000000u;
    const Material* m = materials_.material(variant, row[size_t(bone)]);
    if (!m) return 0xFF000000u;
    float r = m->diffuse.x, g = m->diffuse.y, b = m->diffuse.z;
    if (m->texture.empty()) {
        r = r * factor;
        g = g * factor;
        b = b * factor;
    }
    auto byte = [](float c) { return uint32_t(int(std::trunc(c * 178.5f))) & 255u; };
    return 0xFF000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

void PedModel::pose(int seqIndex, int frame, std::vector<PedBoneXf>& out) const {
    const size_t nb = skel_.bones.size();
    out.assign(nb, PedBoneXf());
    const PedAnimation* a = nullptr;
    if (seqIndex >= 0 && size_t(seqIndex) < seqs_.size()) a = seqs_[size_t(seqIndex)].anim.get();
    const int f = a ? std::clamp(frame, 0, a->frames - 1) : 0;
    auto ch = [&](int c) { return (a && c < a->channels) ? a->at(f, c) : 0.0f; };
    for (size_t i = 0; i < nb; ++i) {
        const PedBone& b = skel_.bones[i];
        float ca, sa, cb, sb, cg, sg;
        cs(ch(3 + 3 * int(i)), ca, sa);
        cs(ch(4 + 3 * int(i)), cb, sb);
        cs(ch(5 + 3 * int(i)), cg, sg);
        vec3 r[3];
        r[0] = vec3(cg * cb, sg, -(cg * sb));
        r[1] = vec3(sb * sa - (sg * cb) * ca, cg * ca, (sg * sb) * ca + cb * sa);
        r[2] = vec3((sg * cb) * sa + sb * ca, -(cg * sa), cb * ca - (sg * sb) * sa);
        vec3 t = b.offset;
        if (i == 0 && a) t = vec3(ch(0), ch(1), ch(2));
        PedBoneXf& x = out[i];
        if (b.parent < 0 || size_t(b.parent) >= i) {
            x.r[0] = r[0];
            x.r[1] = r[1];
            x.r[2] = r[2];
            x.t = t;
        } else {
            const PedBoneXf& p = out[size_t(b.parent)];
            for (int k = 0; k < 3; ++k) x.r[k] = rowTimes(r[k], p.r);
            x.t = rowTimes(t, p.r) + p.t;
        }
    }
}

void PedModel::buildMesh(const std::vector<PedBoneXf>& pose, Mesh& out) const {
    out = Mesh();
    out.fvf = kFvfXYZ | kFvfNormal | kFvfTex1 | (mod_.colors >= 2 ? kFvfDiffuse : 0u);
    bool first = true;
    float r2 = 0;
    for (size_t mi = 0; mi < mod_.mats.size(); ++mi) {
        const PedModMaterial& mt = mod_.mats[mi];
        if (mt.tris.empty()) continue;
        Batch b;
        b.material = int(mi);
        b.vertices.reserve(mt.adj.size());
        for (const PedModAdjunct& a : mt.adj) {
            Vertex v;
            const PedBoneXf* x = (a.bone >= 0 && size_t(a.bone) < pose.size()) ? &pose[size_t(a.bone)] : nullptr;
            const vec3 p = mod_.V[size_t(a.v)], n = mod_.N[size_t(a.n)];
            v.pos = x ? rowTimes(p, x->r) + x->t : p;
            v.normal = x ? rowTimes(n, x->r) : n;
            if (a.t1 >= 0 && size_t(a.t1) < mod_.T1.size()) v.uv = mod_.T1[size_t(a.t1)];
            if (mod_.colors >= 2 && a.c >= 0 && size_t(a.c) < mod_.C.size()) {
                const vec4& c = mod_.C[size_t(a.c)];
                v.color[0] = uint8_t(std::clamp(c.x, 0.0f, 1.0f) * 255.0f);
                v.color[1] = uint8_t(std::clamp(c.y, 0.0f, 1.0f) * 255.0f);
                v.color[2] = uint8_t(std::clamp(c.z, 0.0f, 1.0f) * 255.0f);
                v.color[3] = uint8_t(std::clamp(c.w, 0.0f, 1.0f) * 255.0f);
            }
            if (first) {
                out.boundsMin = out.boundsMax = v.pos;
                first = false;
            } else {
                out.boundsMin = vmin(out.boundsMin, v.pos);
                out.boundsMax = vmax(out.boundsMax, v.pos);
            }
            r2 = std::max(r2, dot(v.pos, v.pos));
            b.vertices.push_back(v);
        }
        for (int idx : mt.tris) b.indices.push_back(uint16_t(idx));
        out.batches.push_back(std::move(b));
    }
    out.radius = std::sqrt(r2);
}

void PedModel::farStrips(std::vector<PedBoneXf>& pose, const vec3& R, int variant, float factor,
                         std::vector<StripVertex>& out) const {
    if (!hasRays_) return;
    for (size_t i = 0; i < rays_.bones.size() && i < pose.size(); ++i) {
        const PedRay& ray = rays_.bones[i];
        if (ray.a == 0.0f) continue;
        pose[i].t.y = pose[i].t.y + ray.h;
        const vec3 P = pose[i].t;
        const vec3 O = pose[size_t(std::clamp(ray.origin, 0, int(pose.size()) - 1))].t;
        const uint32_t c = rayColour(variant, int(i), factor);
        const vec3 q[4] = {P - R * ray.a, P + R * ray.a, O - R * ray.b, O + R * ray.b};
        // Strip (0 1 2), (1 2 3) as a triangle list.
        for (int k : {0, 1, 2, 2, 1, 3}) out.push_back({q[k], c});
    }
}

}  // namespace cc
