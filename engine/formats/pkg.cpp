#include "formats/pkg.h"

#include <cmath>
#include <cstring>

#include "core/strutil.h"

namespace cc {

size_t Mesh::vertexCount() const {
    size_t n = 0;
    for (const auto& b : batches) n += b.vertices.size();
    return n;
}

size_t Mesh::triangleCount() const {
    size_t n = 0;
    for (const auto& b : batches) n += b.indices.size() / 3;
    return n;
}

const Mesh* Pkg::findMesh(const std::string& name) const {
    for (const auto& m : meshes)
        if (iequals(m.name, name)) return &m;
    return nullptr;
}

const Material* Pkg::material(int set, int index) const {
    if (setCount <= 0 || index < 0 || index >= materialsPerSet) return nullptr;
    return &materials[size_t(effectiveSet(set)) * size_t(materialsPerSet) + size_t(index)];
}

size_t Pkg::byteEstimate() const {
    size_t n = sizeof(Pkg) + materials.size() * sizeof(Material);
    for (const auto& m : meshes)
        for (const auto& b : m.batches)
            n += b.vertices.size() * sizeof(Vertex) + b.indices.size() * 2 + sizeof(Batch);
    return n;
}

namespace {

// Size in bytes of one vertex for a format word, or 0 if the format is not supported.
size_t vertexSize(uint32_t fvf) {
    if ((fvf & kFvfPositionMask) != kFvfXYZ) return 0;
    size_t s = 12;
    if (fvf & kFvfNormal) s += 12;
    if (fvf & kFvfReserved) s += 4;
    if (fvf & kFvfDiffuse) s += 4;
    if (fvf & kFvfSpecular) s += 4;
    if (fvf & kFvfTex1) s += 8;
    if (fvf & kFvfTex2) s += 8;
    return s;
}

bool parseMesh(ByteReader& r, Mesh& mesh, std::string& err) {
    uint32_t batchCount = r.u32();
    if (!r.ok()) { err = "truncated mesh header"; return false; }
    if (batchCount & 0x80) {
        err = "header-only mesh form (bit 0x80) is not supported";
        return false;
    }
    if (batchCount >= 128) {
        err = strf("batch count %u is not valid", batchCount);
        return false;
    }
    uint32_t totalV = r.u32();
    uint32_t totalI = r.u32();
    r.u32();  // batch count again
    mesh.fvf = r.u32();
    if (!r.ok()) { err = "truncated mesh header"; return false; }
    const size_t vsize = vertexSize(mesh.fvf);
    if (vsize == 0) {
        err = strf("unsupported vertex format 0x%x", mesh.fvf);
        return false;
    }
    (void)totalV;
    (void)totalI;

    mesh.batches.resize(batchCount);
    bool first = true;
    for (uint32_t b = 0; b < batchCount; ++b) {
        Batch& batch = mesh.batches[b];
        uint32_t strips = r.u32();
        batch.material = int(r.u32() & 0xFF);
        if (!r.ok()) { err = "truncated batch"; return false; }
        if (strips > 0xFFFF) { err = strf("strip count %u is not valid", strips); return false; }
        for (uint32_t s = 0; s < strips; ++s) {
            r.u32();  // primitive code, ignored (always a triangle list)
            uint32_t vcount = r.u32();
            if (!r.ok() || vcount > 65535 || !r.need(size_t(vcount) * vsize)) {
                err = strf("strip vertex data is truncated or too large (%u vertices)", vcount);
                return false;
            }
            // A batch keeps only its last strip.
            batch.vertices.assign(vcount, Vertex());
            for (uint32_t v = 0; v < vcount; ++v) {
                Vertex& vx = batch.vertices[v];
                vx.pos.x = r.f32();
                vx.pos.y = r.f32();
                vx.pos.z = r.f32();
                if (mesh.fvf & kFvfNormal) {
                    vx.normal.x = r.f32();
                    vx.normal.y = r.f32();
                    vx.normal.z = r.f32();
                }
                if (mesh.fvf & kFvfReserved) r.u32();
                if (mesh.fvf & kFvfDiffuse) r.bytes(vx.color, 4);
                if (mesh.fvf & kFvfSpecular) r.u32();
                if (mesh.fvf & kFvfTex1) {
                    vx.uv.x = r.f32();
                    vx.uv.y = r.f32();
                }
                if (mesh.fvf & kFvfTex2) r.skip(8);
            }
            uint32_t icount = r.u32();
            if (!r.ok() || icount > 65535 || !r.need(size_t(icount) * 2)) {
                err = strf("strip index data is truncated or too large (%u indices)", icount);
                return false;
            }
            batch.indices.resize(icount);
            for (uint32_t i = 0; i < icount; ++i) batch.indices[i] = r.u16();
        }
        // Drop incomplete triangles and out-of-range indices so nothing bad reaches the GPU.
        if (batch.indices.size() % 3) batch.indices.resize(batch.indices.size() / 3 * 3);
        for (uint16_t& ix : batch.indices)
            if (ix >= batch.vertices.size()) {
                err = strf("index %u out of range (%zu vertices)", ix, batch.vertices.size());
                return false;
            }
        for (const Vertex& vx : batch.vertices) {
            if (first) {
                mesh.boundsMin = mesh.boundsMax = vx.pos;
                first = false;
            } else {
                mesh.boundsMin = vmin(mesh.boundsMin, vx.pos);
                mesh.boundsMax = vmax(mesh.boundsMax, vx.pos);
            }
            float d = length(vx.pos);
            if (d > mesh.radius) mesh.radius = d;
        }
    }
    return true;
}

inline float snap32(float v) {
    if (v < 0.05f) return 0.0f;
    if (v > 0.95f) return 1.0f;
    return std::floor(v * 32.0f) / 32.0f;
}

inline vec4 snap32(const vec4& c) { return {snap32(c.x), snap32(c.y), snap32(c.z), snap32(c.w)}; }

bool parseShaders(ByteReader& r, Pkg& pkg, std::string& err, const PkgLoadOptions& opt) {
    uint32_t head = r.u32();
    uint32_t perSet = r.u32();
    if (!r.ok()) { err = "truncated material header"; return false; }
    const int sets = int(head & 0x7F);
    const bool compact = (head & 0x80) != 0;
    if (perSet > 4096) { err = strf("material count %u is not valid", perSet); return false; }
    pkg.compactMaterials = compact;
    pkg.materialsPerSet = int(perSet);
    std::vector<Material> mats;
    mats.reserve(size_t(sets) * perSet);
    auto readColor = [&](void) -> vec4 {
        if (compact) {
            uint8_t b[4];
            r.bytes(b, 4);
            return {b[0] / 255.0f, b[1] / 255.0f, b[2] / 255.0f, b[3] / 255.0f};
        }
        vec4 c;
        c.x = r.f32();
        c.y = r.f32();
        c.z = r.f32();
        c.w = r.f32();
        return c;
    };
    for (int s = 0; s < sets; ++s) {
        for (uint32_t i = 0; i < perSet; ++i) {
            Material m;
            uint8_t n = r.u8();
            if (n) {
                if (!r.need(n)) break;
                const char* p = reinterpret_cast<const char*>(r.here());
                m.texture.assign(p, strnlen(p, n));
                r.skip(n);
            }
            m.diffuse = readColor();
            if (!compact) readColor();  // ambient: discarded
            m.specular = readColor();
            m.emissive = readColor();
            m.power = r.f32();
            if (!r.ok()) break;
            if (!compact) {
                m.diffuse = snap32(m.diffuse);
                m.specular = snap32(m.specular);
                m.emissive = snap32(m.emissive);
            }
            if (m.texture.empty()) {
                m.diffuse.x *= opt.untexturedFactor;
                m.diffuse.y *= opt.untexturedFactor;
                m.diffuse.z *= opt.untexturedFactor;
            }
            m.ambient = m.diffuse;
            mats.push_back(std::move(m));
        }
        if (!r.ok()) break;
    }
    if (!r.ok()) { err = "truncated material records"; return false; }
    int keep = sets;
    if (opt.maxSets > 0 && opt.maxSets < keep) keep = opt.maxSets;
    mats.resize(size_t(keep) * perSet);
    pkg.setCount = keep;
    pkg.materials = std::move(mats);
    return true;
}

bool parseXrefs(ByteReader& r, Pkg& pkg, std::string& err) {
    uint32_t n = r.u32();
    if (!r.ok() || !r.need(size_t(n) * 80)) { err = "truncated xrefs"; return false; }
    pkg.xrefs.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        Xref& x = pkg.xrefs[i];
        for (float& f : x.matrix) f = r.f32();
        char name[33] = {};
        r.bytes(name, 32);
        x.name = name;
    }
    return true;
}

enum class BodyKind { Mesh, Shaders, Xrefs, Offset };

BodyKind kindOf(const std::string& name) {
    if (iequals(name, "shaders")) return BodyKind::Shaders;
    if (iequals(name, "xrefs")) return BodyKind::Xrefs;
    if (iequals(name, "offset")) return BodyKind::Offset;
    return BodyKind::Mesh;
}

// Parses one entry body from r. For PKG3, r covers exactly the body.
bool parseBody(ByteReader& r, const std::string& name, Pkg& pkg, std::string& err,
               const PkgLoadOptions& opt) {
    PkgEntry entry;
    entry.name = name;
    switch (kindOf(name)) {
        case BodyKind::Shaders:
            if (!parseShaders(r, pkg, err, opt)) return false;
            pkg.hasShaders = true;
            break;
        case BodyKind::Xrefs:
            if (!parseXrefs(r, pkg, err)) return false;
            break;
        case BodyKind::Offset:
            pkg.offset.x = r.f32();
            pkg.offset.y = r.f32();
            pkg.offset.z = r.f32();
            if (!r.ok()) { err = "truncated offset"; return false; }
            pkg.hasOffset = true;
            break;
        case BodyKind::Mesh: {
            Mesh mesh;
            mesh.name = name;
            if (!parseMesh(r, mesh, err)) {
                err = name + ": " + err;
                return false;
            }
            entry.mesh = int(pkg.meshes.size());
            pkg.meshes.push_back(std::move(mesh));
            break;
        }
    }
    pkg.entries.push_back(std::move(entry));
    return true;
}

}  // namespace

bool parsePkg(const uint8_t* data, size_t size, Pkg& pkg, std::string& err,
              const PkgLoadOptions& opt) {
    pkg = Pkg();
    ByteReader r(data, size);
    if (size < 4) { err = size == 0 ? "empty file" : "too small"; return false; }
    if (std::memcmp(data, "PKG3", 4) == 0) {
        pkg.version = 3;
    } else if (std::memcmp(data, "PKG2", 4) == 0) {
        pkg.version = 2;
    } else {
        err = "not a PKG2/PKG3 package";
        return false;
    }
    r.skip(4);
    while (!r.atEnd()) {
        char tag[4];
        if (!r.bytes(tag, 4) || std::memcmp(tag, "FILE", 4) != 0) {
            err = strf("expected FILE at byte %zu", r.pos() - 4);
            return false;
        }
        uint8_t nlen = r.u8();
        if (!r.need(nlen)) { err = "truncated entry name"; return false; }
        const char* np = reinterpret_cast<const char*>(r.here());
        std::string name(np, strnlen(np, nlen));
        r.skip(nlen);
        if (pkg.version == 3) {
            uint32_t bodySize = r.u32();
            if (!r.ok() || !r.need(bodySize)) {
                err = strf("entry '%s' is truncated (body %u bytes, %zu left)", name.c_str(), bodySize,
                           r.ok() ? r.remaining() : size_t(0));
                return false;
            }
            ByteReader body = r.sub(bodySize);
            std::string e;
            if (!parseBody(body, name, pkg, e, opt)) {
                // The body size lets us step over a broken entry.
                pkg.warnings.push_back("skipped entry " + e);
                pkg.entries.push_back(PkgEntry{name, -1});
                continue;
            }
            if (body.remaining() != 0)
                pkg.warnings.push_back(strf("entry '%s' has %zu unread bytes", name.c_str(), body.remaining()));
        } else {
            if (!parseBody(r, name, pkg, err, opt)) return false;
        }
    }
    if (!pkg.hasShaders) {
        err = "package has no usable 'shaders' entry";
        return false;
    }
    // pkg.md rule 4: `xrefs` is read only if it is the entry right after `shaders`.
    if (!pkg.xrefs.empty()) {
        int sh = -1, xr = -1;
        for (size_t i = 0; i < pkg.entries.size(); ++i) {
            if (sh < 0 && iequals(pkg.entries[i].name, "shaders")) sh = int(i);
            if (xr < 0 && iequals(pkg.entries[i].name, "xrefs")) xr = int(i);
        }
        if (xr != sh + 1) {
            pkg.warnings.push_back("'xrefs' is not right after 'shaders': MM2 does not read it");
            pkg.xrefs.clear();
        }
    }
    return true;
}

bool parseShadersFile(const uint8_t* data, size_t size, Pkg& out, std::string& err, const PkgLoadOptions& opt) {
    ByteReader r(data, size);
    out = Pkg();
    if (!parseShaders(r, out, err, opt)) return false;
    out.hasShaders = true;
    return true;
}

}  // namespace cc
