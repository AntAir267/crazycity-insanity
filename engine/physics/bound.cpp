#include "physics/bound.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

#include "core/bytes.h"
#include "core/log.h"
#include "core/strutil.h"
#include "physics/pmath.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

namespace {

float tf(const std::string& t) { return float(std::strtod(t.c_str(), nullptr)); }
long tl(const std::string& t) { return std::strtol(t.c_str(), nullptr, 10); }

// Polygon normal as the box rule writes it: a = c2 - c1, b = c0 - c1, n = a x b,
// L = (n.y^2 + n.x^2) + n.z^2, n * (1/sqrt(L)) (0 if L = 0).
vec3 polyNormal(const vec3& c0, const vec3& c1, const vec3& c2) {
    const vec3 a = vsub(c2, c1), b = vsub(c0, c1);
    const vec3 n(a.y * b.z - b.y * a.z, a.z * b.x - b.z * a.x, a.x * b.y - a.y * b.x);
    const float L = (n.y * n.y + n.x * n.x) + n.z * n.z;
    if (L == 0.0f) return vec3(0, 0, 0);
    return vscale(n, 1.0f / fsqrt(L));
}

vec3 normalizeOrZero(const vec3& v) {
    const float L = pdot(v, v);
    if (L == 0.0f) return vec3(0, 0, 0);
    return vscale(v, 1.0f / fsqrt(L));
}

void polyGeometry(Bound& b) {
    for (BoundPoly& p : b.polys) {
        const vec3& v0 = b.points[size_t(p.idx[0])];
        const vec3& v1 = b.points[size_t(p.idx[1])];
        const vec3& v2 = b.points[size_t(p.idx[2])];
        p.normal = polyNormal(v0, v1, v2);
        for (int k = 0; k < p.n; ++k) {
            const vec3& a = b.points[size_t(p.idx[k])];
            const vec3& c = b.points[size_t(p.idx[(k + 1) % p.n])];
            p.edgeNormal[k] = normalizeOrZero(pcross(p.normal, vsub(c, a)));
        }
    }
}

bool validIndices(const Bound& b) {
    for (const BoundPoly& p : b.polys)
        for (int k = 0; k < p.n; ++k)
            if (p.idx[k] < 0 || size_t(p.idx[k]) >= b.points.size()) return false;
    return true;
}

}  // namespace

void Bound::computeBox() {
    if (points.empty()) return;
    bmin = bmax = points[0];
    for (const vec3& p : points) {
        bmin = vec3(p.x < bmin.x ? p.x : bmin.x, p.y < bmin.y ? p.y : bmin.y, p.z < bmin.z ? p.z : bmin.z);
        bmax = vec3(p.x > bmax.x ? p.x : bmax.x, p.y > bmax.y ? p.y : bmax.y, p.z > bmax.z ? p.z : bmax.z);
    }
    // bnd.md item 6: centre = (max − min)·0.5 + min; radius from d = centre − max, summed z, y, x.
    centre = vec3((bmax.x - bmin.x) * 0.5f + bmin.x, (bmax.y - bmin.y) * 0.5f + bmin.y, (bmax.z - bmin.z) * 0.5f + bmin.z);
    const vec3 d = vsub(centre, bmax);
    radius = fsqrt((d.z * d.z + d.y * d.y) + d.x * d.x);
}

void deriveBound(Bound& b) {
    // 1-2. Edge list (file edges first) and edge numbers per polygon.
    std::map<std::pair<int, int>, int> index;
    for (size_t i = 0; i < b.edges.size(); ++i) {
        auto e = b.edges[i];
        index.emplace(std::make_pair(std::min(e.first, e.second), std::max(e.first, e.second)), int(i));
    }
    for (const BoundPoly& p : b.polys) {
        for (int s = 0; s < p.n; ++s) {
            // the edge from the last corner to the first, then 0->1, 1->2, ...
            int k = s == 0 ? p.n - 1 : s - 1;
            int from = p.idx[k], to = p.idx[(k + 1) % p.n];
            auto key = std::make_pair(std::min(from, to), std::max(from, to));
            if (index.count(key)) continue;
            index.emplace(key, int(b.edges.size()));
            b.edges.emplace_back(from, to);
        }
    }
    for (BoundPoly& p : b.polys)
        for (int k = 0; k < p.n; ++k) {
            int from = p.idx[k], to = p.idx[(k + 1) % p.n];
            p.edgeNum[k] = index[std::make_pair(std::min(from, to), std::max(from, to))];
        }
    // 3-4.
    polyGeometry(b);
    // 5. Edge normal and cosine.
    b.edgeNormal.assign(b.edges.size(), vec3());
    b.edgeCos.assign(b.edges.size(), -1.0f);
    for (size_t e = 0; e < b.edges.size(); ++e) {
        const int a = b.edges[e].first, c = b.edges[e].second;
        bool haveF = false, haveR = false;
        vec3 nF, nR;
        for (const BoundPoly& p : b.polys) {
            for (int k = 0; k < p.n; ++k) {
                int u = p.idx[k], w = p.idx[(k + 1) % p.n];
                if (u == a && w == c) {
                    nF = p.normal;
                    haveF = true;
                } else if (u == c && w == a) {
                    nR = p.normal;
                    haveR = true;
                }
            }
            if (haveF && haveR) break;
        }
        if (!haveF && !haveR) {
            b.edgeNormal[e] = vec3(1, 0, 0);
            b.edgeCos[e] = -1.0f;
            continue;
        }
        if (!haveF) nF = vscale(nR, -1.0f);
        if (!haveR) nR = vscale(nF, -1.0f);
        const vec3 ev = vsub(b.points[size_t(c)], b.points[size_t(a)]);
        vec3 s = vadd(nR, nF);
        if (pdot(s, s) < 0.000001f) {
            s = pcross(nR, ev);
            if (pdot(s, s) < 0.000001f) s = vec3(0, 1, 0);
        }
        const vec3 en = vscale(s, 1.0f / fsqrt(pdot(s, s)));
        b.edgeNormal[e] = en;
        auto dzyx = [](const vec3& a, const vec3& u) { return (a.z * u.z + a.y * u.y) + a.x * u.x; };
        if (dzyx(nR, pcross(en, ev)) <= 0.0f) b.edgeCos[e] = dzyx(nR, en);
        else b.edgeCos[e] = 2.0f;
    }
    // 6.
    b.computeBox();
}

bool parseBndText(const std::string& text, Bound& b, MaterialTable& table, std::string& err) {
    b = Bound();
    auto t = tokenizeWhitespace(text);
    size_t pos = 0;
    auto expect = [&](const char* kw) {
        if (pos >= t.size() || t[pos] != kw) {
            err = strf("expected '%s'", kw);
            return false;
        }
        ++pos;
        return true;
    };
    auto next = [&]() -> std::string { return pos < t.size() ? t[pos++] : std::string(); };
    if (!expect("version:")) return false;
    if (tf(next()) != 1.01f) {
        err = "version is not 1.01";
        return false;
    }
    if (!expect("verts:")) return false;
    long nv = tl(next());
    if (!expect("materials:")) return false;
    long nm = tl(next());
    if (!expect("edges:")) return false;
    long ne = tl(next());
    if (!expect("polys:")) return false;
    long np = tl(next());
    if (nv <= 0 || np <= 0 || nm < 0 || ne < 0 || nv > 1000000 || np > 1000000) {
        err = "no points or no polygons";
        return false;
    }
    for (long i = 0; i < nv; ++i) {
        if (!expect("v")) return false;
        vec3 p;
        p.x = tf(next());
        p.y = tf(next());
        p.z = tf(next());
        b.points.push_back(p);
    }
    for (long i = 0; i < nm; ++i) {
        Material m;
        if (!parseMtlBlock(t, pos, m)) {
            err = "bad material block";
            return false;
        }
        b.materials.push_back(table.shareOrAdd(m));
    }
    for (long i = 0; i < ne; ++i) {
        if (!expect("edge")) return false;
        int a = int(tl(next())), c = int(tl(next()));
        b.edges.emplace_back(a, c);
    }
    for (long i = 0; i < np; ++i) {
        std::string kw = next();
        BoundPoly p;
        if (kw == "tri") {
            p.idx[0] = int(tl(next()));
            p.idx[1] = int(tl(next()));
            p.idx[2] = int(tl(next()));
            p.idx[3] = 0;
            p.material = int(tl(next()) & 0xFF);
            p.n = 3;
        } else if (kw == "quad") {
            int a = int(tl(next())), c = int(tl(next())), d = int(tl(next())), e = int(tl(next()));
            p.material = int(tl(next()) & 0xFF);
            if (e == 0) {
                p.idx[0] = c;
                p.idx[1] = d;
                p.idx[2] = 0;
                p.idx[3] = a;
            } else {
                p.idx[0] = a;
                p.idx[1] = c;
                p.idx[2] = d;
                p.idx[3] = e;
            }
            p.n = p.idx[3] == 0 ? 3 : 4;  // a fourth index of 0 means a triangle
        } else {
            err = "expected 'tri' or 'quad'";
            return false;
        }
        b.polys.push_back(p);
    }
    if (!validIndices(b)) {
        err = "point index out of range";
        return false;
    }
    if (b.materials.empty()) b.materials.push_back(0);
    deriveBound(b);
    return true;
}

bool parseBbnd(const uint8_t* data, size_t size, Bound& b, MaterialTable& table, std::string& err) {
    b = Bound();
    ByteReader r(data, size);
    r.u8();
    uint32_t nv = r.u32(), nm = r.u32(), np = r.u32();
    if (!r.ok() || nv == 0 || np == 0 || !r.need(size_t(nv) * 12 + size_t(nm) * 104 + size_t(np) * 10)) {
        err = "truncated binary bound";
        return false;
    }
    for (uint32_t i = 0; i < nv; ++i) {
        vec3 p;
        p.x = r.f32();
        p.y = r.f32();
        p.z = r.f32();
        b.points.push_back(p);
    }
    for (uint32_t i = 0; i < nm; ++i) {
        char name[33] = {};
        r.bytes(name, 32);
        Material m;
        m.name = name;
        m.elasticity = r.f32();
        m.friction = r.f32();
        r.skip(64);
        b.materials.push_back(table.shareOrAdd(m));
    }
    for (uint32_t i = 0; i < np; ++i) {
        BoundPoly p;
        for (int k = 0; k < 4; ++k) p.idx[k] = r.u16();
        p.material = r.u16() & 0xFF;
        p.n = p.idx[3] == 0 ? 3 : 4;
        b.polys.push_back(p);
    }
    if (!validIndices(b)) {
        err = "point index out of range";
        return false;
    }
    if (b.materials.empty()) b.materials.push_back(0);
    deriveBound(b);
    return true;
}

bool parseTer(const uint8_t* data, size_t size, Bound& b, std::string& err) {
    ByteReader r(data, size);
    const float version = r.f32();
    if (version != 1.1f) {
        err = "terrain version is not 1.1";
        return false;
    }
    const int32_t polyCount = int32_t(r.u32());
    if (polyCount != int32_t(b.polys.size())) {
        err = "terrain polygon count differs from the bound's";
        return false;
    }
    const int32_t E = int32_t(r.u32());
    TerrainGrid& g = b.grid;
    g.hotEdges = r.u8() != 0;
    g.size.x = r.f32();
    g.size.y = r.f32();
    g.size.z = r.f32();
    g.W = int32_t(r.u32());
    g.H = int32_t(r.u32());
    g.D = int32_t(r.u32());
    const int32_t cells = int32_t(r.u32());
    const int32_t R = int32_t(r.u32());
    g.factor.x = r.f32();
    g.factor.y = r.f32();
    g.factor.z = r.f32();
    g.gmin.x = r.f32();
    g.gmin.y = r.f32();
    g.gmin.z = r.f32();
    g.gmax.x = r.f32();
    g.gmax.y = r.f32();
    g.gmax.z = r.f32();
    if (!r.ok() || E < 0 || cells < 0 || R < 0 || g.W <= 0 || g.D <= 0 || cells != g.W * g.H * g.D ||
        !r.need(size_t(cells) * 4 + size_t(R) * 2 + size_t(E) * 4 + size_t(polyCount) * 16 + size_t(E) * 16)) {
        err = "terrain grid truncated or inconsistent";
        return false;
    }
    g.first.resize(size_t(cells));
    g.count.resize(size_t(cells));
    g.refs.resize(size_t(R));
    for (auto& v : g.first) v = r.u16();
    for (auto& v : g.count) v = r.u16();
    for (auto& v : g.refs) v = r.u16();
    b.edges.resize(size_t(E));
    for (auto& e : b.edges) {
        e.first = r.u16();
        e.second = r.u16();
    }
    for (BoundPoly& p : b.polys)
        for (int k = 0; k < 4; ++k) {
            uint32_t v = r.u32();
            if (k < 4) p.edgeNum[k] = int(v & 0xFFFF);
        }
    b.edgeNormal.resize(size_t(E));
    for (auto& n : b.edgeNormal) {
        n.x = r.f32();
        n.y = r.f32();
        n.z = r.f32();
    }
    b.edgeCos.resize(size_t(E));
    for (auto& c : b.edgeCos) c = r.f32();
    if (!r.ok()) {
        err = "terrain data truncated";
        return false;
    }
    for (const auto& ref : g.refs)
        if (ref >= b.polys.size()) {
            err = "terrain cell references a missing polygon";
            return false;
        }
    b.computeBox();
    b.terrain = true;
    return true;
}

namespace {
Bound boxBound(const vec3& size, const vec3* offset);
}

Bound makeBoxBound(const vec3& size) { return boxBound(size, nullptr); }

Bound makeBoxBound(const vec3& size, const vec3& offset) {
    Bound b = boxBound(size, &offset);
    const vec3 h = vscale(size, 0.5f);
    const vec3& c = offset;
    b.bmin = vec3(c.x + (-h.x), c.y + (-h.y), c.z + (-h.z));
    b.bmax = vec3(c.x + h.x, c.y + h.y, c.z + h.z);
    b.centre = c;
    return b;
}

namespace {
Bound boxBound(const vec3& size, const vec3* offset) {
    Bound b;
    b.isBox = true;
    const vec3 h = vscale(size, 0.5f);
    const float sx[8] = {+1, -1, -1, +1, +1, -1, -1, +1};
    const float sy[8] = {+1, +1, -1, -1, +1, +1, -1, -1};
    const float sz[8] = {+1, +1, +1, +1, -1, -1, -1, -1};
    for (int i = 0; i < 8; ++i) {
        if (offset)
            b.points.push_back(vec3(sx[i] * 0.5f * size.x + offset->x, sy[i] * 0.5f * size.y + offset->y,
                                    sz[i] * 0.5f * size.z + offset->z));
        else
            b.points.push_back(vec3(sx[i] * h.x, sy[i] * h.y, sz[i] * h.z));
    }
    const int E[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& e : E) b.edges.emplace_back(e[0], e[1]);
    const int Q[6][4] = {{4, 0, 3, 7}, {2, 1, 5, 6}, {1, 0, 4, 5}, {7, 3, 2, 6}, {3, 0, 1, 2}, {5, 4, 7, 6}};
    const int N[6][4] = {{8, 3, 11, 7}, {1, 9, 5, 10}, {0, 8, 4, 9}, {11, 2, 10, 6}, {3, 0, 1, 2}, {4, 7, 6, 5}};
    for (int q = 0; q < 6; ++q) {
        BoundPoly p;
        p.n = 4;
        for (int k = 0; k < 4; ++k) {
            p.idx[k] = Q[q][k];
            p.edgeNum[k] = N[q][k];
        }
        p.material = 0;
        b.polys.push_back(p);
    }
    polyGeometry(b);
    const float s = 0.70710677f;
    const vec3 en[12] = {vec3(0, s, s),  vec3(-s, 0, s), vec3(0, -s, s),  vec3(s, 0, s),
                         vec3(0, s, -s), vec3(-s, 0, -s), vec3(0, -s, -s), vec3(s, 0, -s),
                         vec3(s, s, 0),  vec3(-s, s, 0), vec3(-s, -s, 0), vec3(s, -s, 0)};
    b.edgeNormal.assign(en, en + 12);
    b.edgeCos.assign(12, 0.785398185f);
    b.bmin = vec3(-h.x, -h.y, -h.z);
    b.bmax = h;
    b.centre = vec3(0, 0, 0);
    b.radius = fsqrt((h.x * h.x + h.y * h.y) + h.z * h.z);
    b.materials.push_back(0);
    return b;
}
}  // namespace

std::shared_ptr<Bound> loadGeometryBound(const Vfs& vfs, const std::string& name, MaterialTable& table) {
    const std::string path = "bound/" + name + "_BOUND.bnd";
    BytesPtr bytes = vfs.open(path);
    if (!bytes) return nullptr;
    auto b = std::make_shared<Bound>();
    std::string err;
    if (!parseBndText(std::string(bytes->begin(), bytes->end()), *b, table, err)) {
        logFile(LogLevel::Warn, vfs.storedName(path), "bound broken: " + err);
        return nullptr;
    }
    return b;
}

std::shared_ptr<Bound> loadTerrainBound(const Vfs& vfs, const std::string& name, MaterialTable& table) {
    const std::string ter = "bound/" + name + "_BOUND.ter";
    BytesPtr tb = vfs.open(ter);
    if (!tb) return nullptr;
    auto b = std::make_shared<Bound>();
    std::string err;
    BytesPtr bb = vfs.open("bound/" + name + "_BOUND.bbnd");
    bool ok = false;
    if (bb) {
        ok = parseBbnd(bb->data(), bb->size(), *b, table, err);
    } else {
        BytesPtr tx = vfs.open("bound/" + name + "_BOUND.bnd");
        if (tx) ok = parseBndText(std::string(tx->begin(), tx->end()), *b, table, err);
        else err = "no geometry for the terrain bound";
    }
    if (ok) ok = parseTer(tb->data(), tb->size(), *b, err);
    if (!ok) {
        logFile(LogLevel::Warn, vfs.storedName(ter), "terrain bound broken: " + err);
        return nullptr;
    }
    return b;
}

}  // namespace phys
}  // namespace cc
