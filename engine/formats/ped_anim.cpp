// Readers for the pedestrian files of `anim/` (spec/formats/ped-anim.md).
#include "formats/ped_anim.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "core/bytes.h"
#include "core/strutil.h"

namespace cc {

namespace {

// C atoi / atof on a token (leading blanks skipped, as much as parses).
int toInt(const std::string& s) { return int(std::strtol(s.c_str(), nullptr, 10)); }
double toDouble(const std::string& s) { return std::strtod(s.c_str(), nullptr); }
float toFloat(const std::string& s) { return float(std::strtod(s.c_str(), nullptr)); }

// C strtok over `s` from `pos` with the delimiter set `delims`; false when no token remains.
bool strtokNext(const std::string& s, size_t& pos, const char* delims, std::string& out) {
    while (pos < s.size() && std::strchr(delims, s[pos]) && s[pos] != '\0') ++pos;
    if (pos >= s.size()) return false;
    const size_t start = pos;
    while (pos < s.size() && !std::strchr(delims, s[pos])) ++pos;
    out = s.substr(start, pos - start);
    if (pos < s.size()) ++pos;  // strtok overwrites the delimiter that ended the token
    return true;
}

class Tokens {
public:
    explicit Tokens(std::vector<std::string> t) : t_(std::move(t)) {}
    bool done() const { return i_ >= t_.size(); }
    const std::string& peek() const {
        static const std::string empty;
        return i_ < t_.size() ? t_[i_] : empty;
    }
    std::string next() { return i_ < t_.size() ? t_[i_++] : std::string(); }
    bool expect(const char* word) {
        if (i_ < t_.size() && t_[i_] == word) {
            ++i_;
            return true;
        }
        return false;
    }
    float f() { return toFloat(next()); }
    int i() { return toInt(next()); }
    vec3 v3() {
        const float x = f(), y = f(), z = f();
        return vec3(x, y, z);
    }

private:
    std::vector<std::string> t_;
    size_t i_ = 0;
};

}  // namespace

std::vector<std::string> pedTokens(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n' || text[i] == '\f' ||
                                   text[i] == '\v'))
            ++i;
        if (i >= text.size()) break;
        const size_t s = i;
        while (i < text.size() && !(text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n' ||
                                    text[i] == '\f' || text[i] == '\v'))
            ++i;
        out.push_back(text.substr(s, i - s));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Sequence table

std::vector<PedSeqRow> parsePedSequenceTable(const std::string& text) {
    std::vector<PedSeqRow> rows;
    size_t pos = 0;
    while (pos < text.size()) {
        // fgets with a 128-byte buffer: at most 127 bytes, through the line feed.
        std::string line;
        while (pos < text.size() && line.size() < 127) {
            const char ch = text[pos++];
            line.push_back(ch);
            if (ch == '\n') break;
        }
        if (!line.empty() && line[0] == '#') continue;
        PedSeqRow r;
        size_t p = 0;
        std::string f[9];
        int n = 0;
        for (; n < 8; ++n)
            if (!strtokNext(line, p, ",", f[n])) break;
        if (n == 8 && strtokNext(line, p, ",\r\n", f[8])) n = 9;
        r.fields = n;
        r.name = f[0];
        r.anim = f[1];
        r.first = toInt(f[2]);
        r.last = toInt(f[3]);
        r.fwdOffset = toDouble(f[4]);
        r.fwdDist = toDouble(f[5]);
        r.sideOffset = toDouble(f[6]);
        r.sideDist = toDouble(f[7]);
        r.next = f[8];
        rows.push_back(r);
    }
    return rows;
}

// ---------------------------------------------------------------------------------------------
// Animation

bool parsePedAnimation(const uint8_t* data, size_t size, PedAnimation& out, std::string& err) {
    ByteReader r(data, size);
    const uint32_t a = r.u32();
    uint32_t F = 0, C = 0;
    if (a == 0) {
        F = r.u32();
        C = r.u32();
    } else {
        F = a;
        const uint32_t B = r.u32();
        C = B > 400u ? 100000u : 3u * B + 3u;
    }
    if (!r.ok()) {
        err = "truncated header";
        return false;
    }
    if (F < 1 || F > 10000 || C < 1 || C > 1000) {
        err = strf("frame count %u or channel count %u out of range", F, C);
        return false;
    }
    out.frames = int(F);
    out.channels = int(C);
    out.stride = r.f32();
    out.flag = r.u8();
    out.data.assign(size_t(F) * size_t(C), 0.0f);
    for (float& v : out.data) v = r.f32();
    if (!r.ok()) {
        err = "truncated frames";
        return false;
    }
    out.exact = r.atEnd();
    return true;
}

void pedAnimForwardFixup(PedAnimation& a) {
    if (a.channels < 3 || a.frames < 1) return;
    const float step = -(a.stride / float(a.frames));
    for (int i = 0; i < a.frames; ++i) {
        float& z = a.at(i, 2);
        z = z - ((float(i) * step) + 0.0f);
    }
}

void pedAnimDriftRemoval(PedAnimation& an, int N) {
    if (an.channels < 3 || N < 0 || N >= an.frames) return;
    const float x0 = an.at(0, 0), z0 = an.at(0, 2), xN = an.at(N, 0), zN = an.at(N, 2);
    const float a = -x0, b = -z0;
    float sx = 0.0f, sz = 0.0f;
    if (N != 0) {
        sx = (-xN - a) / float(N);
        sz = (-zN - b) / float(N);
    }
    for (int i = 0; i <= N; ++i) {
        float& x = an.at(i, 0);
        float& z = an.at(i, 2);
        x = x - (-((float(i) * sx) + a));
        z = z - (-((float(i) * sz) + b));
    }
}

// ---------------------------------------------------------------------------------------------
// Skeleton

int PedSkeleton::find(const std::string& name) const {
    for (size_t i = 0; i < bones.size(); ++i)
        if (bones[i].name == name) return int(i);
    return -1;
}

namespace {
bool readBone(Tokens& t, int parent, PedSkeleton& sk, int depth, std::string& err) {
    if (depth > 256) {
        err = "bones nested too deeply";
        return false;
    }
    PedBone b;
    b.name = t.next();
    if (b.name.size() > 63) b.name.resize(63);
    b.parent = parent;
    if (!t.expect("{")) {
        err = "bone " + b.name + ": '{' expected";
        return false;
    }
    const int me = int(sk.bones.size());
    sk.bones.push_back(b);
    while (!t.done()) {
        const std::string w = t.next();
        if (w == "}") return true;
        if (w == "offset") sk.bones[size_t(me)].offset = t.v3();
        else if (w == "rotmin") {
            sk.bones[size_t(me)].rotMin = t.v3();
            sk.bones[size_t(me)].hasRotMin = true;
        } else if (w == "rotmax") {
            sk.bones[size_t(me)].rotMax = t.v3();
            sk.bones[size_t(me)].hasRotMax = true;
        } else if (w == "bone") {
            if (!readBone(t, me, sk, depth + 1, err)) return false;
        }
    }
    err = "bone " + b.name + ": '}' missing";
    return false;
}
}  // namespace

bool parsePedSkeleton(const std::string& text, PedSkeleton& out, std::string& err) {
    Tokens t(pedTokens(text));
    out = PedSkeleton();
    if (!t.expect("NumBones")) {
        err = "no NumBones";
        return false;
    }
    out.declared = t.i();
    if (!t.expect("bone")) {
        err = "not a skeleton (no 'bone' after the count)";
        return false;
    }
    return readBone(t, -1, out, 0, err);
}

// ---------------------------------------------------------------------------------------------
// Model

int PedMod::adjunctTotal() const {
    int n = 0;
    for (const auto& m : mats) n += int(m.adj.size());
    return n;
}

int PedMod::triangleTotal() const {
    int n = 0;
    for (const auto& m : mats) n += int(m.tris.size() / 3);
    return n;
}

bool parsePedMod(const std::string& text, PedMod& m, std::string& err) {
    Tokens t(pedTokens(text));
    m = PedMod();
    // Header: eleven labels with their values.
    for (int k = 0; k < 11; ++k) {
        const std::string label = t.next();
        const std::string value = t.next();
        if (label == "version:") m.version = toFloat(value);
        else if (label == "verts:") m.verts = toInt(value);
        else if (label == "normals:") m.normals = toInt(value);
        else if (label == "colors:") m.colors = toInt(value);
        else if (label == "tex1s:") m.tex1s = toInt(value);
        else if (label == "tex2s:") m.tex2s = toInt(value);
        else if (label == "tangents:") m.tangents = toInt(value);
        else if (label == "materials:") m.materials = toInt(value);
        else if (label == "adjuncts:") m.adjuncts = toInt(value);
        else if (label == "primitives:") m.primitives = toInt(value);
        else if (label == "matrices:") m.matrices = toInt(value);
        else {
            err = "unknown header label " + label;
            return false;
        }
    }
    auto bad = [&](int n) { return n < 0 || n > 1000000; };
    if (bad(m.verts) || bad(m.normals) || bad(m.colors) || bad(m.tex1s) || bad(m.tex2s) || bad(m.tangents) ||
        bad(m.materials) || bad(m.adjuncts) || bad(m.primitives) || m.matrices < 0 || m.matrices > 4096) {
        err = "header counts out of range";
        return false;
    }
    auto lines = [&](const char* tag, int n, int values, auto&& take) -> bool {
        for (int i = 0; i < n; ++i) {
            if (!t.expect(tag)) {
                err = strf("'%s' line %d of %d expected", tag, i + 1, n);
                return false;
            }
            float v[4] = {};
            for (int k = 0; k < values; ++k) v[k] = t.f();
            take(v);
        }
        return true;
    };
    if (!lines("v", m.verts, 3, [&](const float* v) { m.V.push_back(vec3(v[0], v[1], v[2])); })) return false;
    if (!lines("n", m.normals, 3, [&](const float* v) { m.N.push_back(vec3(v[0], v[1], v[2])); })) return false;
    if (m.colors < 2) {
        if (!lines("c", 1, 4, [](const float*) {})) return false;
    } else if (!lines("c", m.colors, 4, [&](const float* v) { m.C.push_back(vec4(v[0], v[1], v[2], v[3])); })) {
        return false;
    }
    if (!lines("t1", m.tex1s, 2, [&](const float* v) { m.T1.push_back(vec2{v[0], v[1]}); })) return false;
    if (!lines("t2", m.tex2s, 2, [](const float*) {})) return false;
    if (!lines("ts", m.tangents, 3, [](const float*) {})) return false;
    if (!lines("tt", m.tangents, 3, [](const float*) {})) return false;
    // Materials.
    for (int i = 0; i < m.materials; ++i) {
        if (!t.expect("mtl")) {
            err = strf("material %d: 'mtl' expected", i);
            return false;
        }
        PedModMaterial mt;
        mt.name = t.next();
        if (!t.expect("{")) {
            err = "material " + mt.name + ": '{' expected";
            return false;
        }
        bool closed = false;
        while (!t.done()) {
            const std::string k = t.next();
            if (k == "}") {
                closed = true;
                break;
            }
            if (k == "packets:") {
                mt.packetForm = true;
                mt.packets = t.i();
            } else if (k == "adjuncts:") {
                mt.adjuncts = t.i();
            } else if (k == "primitives:") {
                mt.primitives = t.i();
            } else if (k == "textures:") {
                mt.textures = t.i();
            } else if (k == "illum:") {
                mt.illum = t.next();
            } else if (k == "ambient:") {
                mt.ambient = t.v3();
            } else if (k == "diffuse:") {
                mt.diffuse = t.v3();
            } else if (k == "specular:") {
                mt.specular = t.v3();
            } else if (k == "texture:") {
                t.i();
                mt.textureNames.push_back(t.next());
            }
        }
        if (!closed) {
            err = "material " + mt.name + ": '}' missing";
            return false;
        }
        m.mats.push_back(std::move(mt));
    }
    m.packetLayout = !m.mats.empty() && m.mats[0].packetForm;
    auto readMtxv = [&]() {
        if (!t.expect("mtxv")) return;
        for (int k = 0; k < m.matrices; ++k) m.mtxv.push_back(t.i());
        for (int b = 0; b < int(m.mtxv.size()); ++b)
            for (int k = 0; k < m.mtxv[size_t(b)] && int(m.vertexBone.size()) < m.verts; ++k) m.vertexBone.push_back(b);
    };
    auto boneOfVertex = [&](int v) { return (v >= 0 && size_t(v) < m.vertexBone.size()) ? m.vertexBone[size_t(v)] : 0; };
    if (m.packetLayout) {
        std::vector<std::vector<int>> packetBones;  // per adjunct of each material: the packet's bone
        for (PedModMaterial& mt : m.mats) {
            for (int p = 0; p < mt.packets; ++p) {
                if (!t.expect("packet")) {
                    err = "material " + mt.name + ": 'packet' expected";
                    return false;
                }
                const int na = t.i(), nt = t.i(), nk = t.i();
                int nr = 0;
                if (!t.expect("{")) {
                    nr = t.i();
                    if (!t.expect("{")) {
                        err = "packet: '{' expected";
                        return false;
                    }
                }
                if (na < 0 || nt < 0 || nk < 0 || nk > 8 || nr < 0) {
                    err = "packet counts out of range";
                    return false;
                }
                std::vector<int> slots;
                const int base = int(mt.adj.size());
                for (int i = 0; i < na; ++i) {
                    if (!t.expect("adj")) {
                        err = "packet: 'adj' expected";
                        return false;
                    }
                    PedModAdjunct a;
                    a.v = t.i();
                    a.n = t.i();
                    a.c = t.i();
                    a.t1 = t.i();
                    a.t2 = t.i();
                    slots.push_back(nk > 0 ? t.i() : 0);
                    mt.adj.push_back(a);
                }
                for (int i = 0; i < nr; ++i) {
                    // Never shipped: skipped up to the next known line.
                    if (!t.expect("reskin")) break;
                    while (!t.done() && t.peek() != "reskin" && t.peek() != "tri" && t.peek() != "mtx" && t.peek() != "}") t.next();
                }
                for (int i = 0; i < nt; ++i) {
                    if (!t.expect("tri")) {
                        err = "packet: 'tri' expected";
                        return false;
                    }
                    for (int k = 0; k < 3; ++k) mt.tris.push_back(base + t.i());
                }
                std::vector<int> mtx;
                if (nk > 0) {
                    if (!t.expect("mtx")) {
                        err = "packet: 'mtx' expected";
                        return false;
                    }
                    for (int i = 0; i < nk; ++i) mtx.push_back(t.i());
                }
                if (!t.expect("}")) {
                    err = "packet: '}' expected";
                    return false;
                }
                for (int i = 0; i < na; ++i) {
                    const int s = slots[size_t(i)];
                    mt.adj[size_t(base + i)].bone = (nk > 0 && s >= 0 && s < nk) ? mtx[size_t(s)] : 0;
                }
                ++m.packetCount;
            }
        }
        readMtxv();  // not used by this layout; kept for the agreement check
        if (!m.vertexBone.empty())
            for (const PedModMaterial& mt : m.mats)
                for (const PedModAdjunct& a : mt.adj)
                    if (a.bone != boneOfVertex(a.v)) ++m.packetBoneMismatches;
    } else {
        std::vector<PedModAdjunct> all;
        std::vector<int> tris;
        for (int i = 0; i < m.adjuncts; ++i) {
            if (!t.expect("adj")) {
                err = strf("adjunct %d: 'adj' expected", i);
                return false;
            }
            PedModAdjunct a;
            a.v = t.i();
            a.n = t.i();
            a.c = t.i();
            a.t1 = t.i();
            a.t2 = t.i();
            all.push_back(a);
        }
        for (int i = 0; i < m.primitives; ++i) {
            if (!t.expect("tri")) {
                err = strf("triangle %d: 'tri' expected", i);
                return false;
            }
            for (int k = 0; k < 3; ++k) tris.push_back(t.i());
        }
        readMtxv();
        for (PedModAdjunct& a : all) a.bone = boneOfVertex(a.v);
        size_t ai = 0, ti = 0;
        for (PedModMaterial& mt : m.mats) {
            const int first = int(ai);
            for (int k = 0; k < mt.adjuncts && ai < all.size(); ++k) mt.adj.push_back(all[ai++]);
            for (int k = 0; k < mt.primitives && ti + 3 <= tris.size(); ++k, ti += 3)
                for (int j = 0; j < 3; ++j) mt.tris.push_back(tris[ti + size_t(j)] - first);
        }
    }
    // Indices must stay inside the model's lists.
    for (const PedModMaterial& mt : m.mats) {
        for (int idx : mt.tris)
            if (idx < 0 || size_t(idx) >= mt.adj.size()) {
                err = "material " + mt.name + ": triangle index out of range";
                return false;
            }
        for (const PedModAdjunct& a : mt.adj)
            if (a.v < 0 || size_t(a.v) >= m.V.size() || a.n < 0 || size_t(a.n) >= m.N.size()) {
                err = "material " + mt.name + ": adjunct vertex or normal out of range";
                return false;
            }
    }
    return true;
}

int pedReorderTranslucent(std::vector<PedModMaterial>& mats, const std::function<int(const PedModMaterial&)>& kind) {
    const int M = int(mats.size());
    if (M == 0) return 0;
    // 1. The last opaque material.
    int j = M - 1;
    while (j > 0 && kind(mats[size_t(j)]) != 1) --j;
    // 2. Translucent materials before it change places with it, one by one.
    int swaps = 0;
    for (int i = 0; i < j; ++i) {
        if (kind(mats[size_t(i)]) == 2) {
            std::swap(mats[size_t(i)], mats[size_t(j)]);
            --j;
            ++swaps;
        }
    }
    return swaps;
}

// ---------------------------------------------------------------------------------------------
// Far detail

bool parsePedRays(const std::string& text, int boneCount, int variants, PedRays& out, std::string& err) {
    Tokens t(pedTokens(text));
    out = PedRays();
    out.count = t.i();
    if (out.count != boneCount) {
        err = strf("%d rays for %d bones", out.count, boneCount);
        return false;
    }
    for (int i = 0; i < out.count; ++i) {
        PedRay r;
        r.a = t.f();
        r.b = t.f();
        r.h = t.f();
        r.origin = t.i();
        r.unused = t.i();
        out.bones.push_back(r);
    }
    for (int v = 0; v < variants; ++v) {
        std::vector<int> row;
        for (int i = 0; i < out.count; ++i) row.push_back(t.i());
        out.rows.push_back(row);
    }
    return true;
}

}  // namespace cc
