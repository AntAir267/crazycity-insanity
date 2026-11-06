#include "city/psdl_geometry.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/strutil.h"

namespace cc {

void buildWallLightTable(const CityLights& lights, uint32_t out[64]) {
    const float ar = float((lights.ambient >> 16) & 255), ag = float((lights.ambient >> 8) & 255),
                ab = float(lights.ambient & 255);
    for (int k = 0; k < 64; ++k) {
        const float a = float(k) * kPi / 32.0f - kPi / 2.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        float r = ar, g = ag, b = ab;
        for (int l = 0; l < 3; ++l) {
            float d = ca * -lights.dir[l].x + sa * -lights.dir[l].z;
            if (d <= 0) continue;
            r += d * lights.color[l].x * 255.0f;
            g += d * lights.color[l].y * 255.0f;
            b += d * lights.color[l].z * 255.0f;
        }
        auto cap = [](float v) { return uint32_t(v > 255.0f ? 255.0f : (v < 0 ? 0 : v)); };
        out[k] = 0xFF000000u | (cap(r) << 16) | (cap(g) << 8) | cap(b);
    }
}

namespace {

constexpr float kCurb = 0.15f;

struct SV {
    vec3 p;
    float u, v;
};

inline vec3 up(const vec3& p, float h) { return vec3(p.x, p.y + h, p.z); }
inline float flatLength(const vec3& d) { return std::sqrt(d.x * d.x + d.z * d.z); }
inline vec3 flatUnit(const vec3& d) {
    float l = flatLength(d);
    return l > 0 ? vec3(d.x / l, 0, d.z / l) : vec3(0, 0, 0);
}
inline vec3 unit3(const vec3& d) { return normalize(d); }
inline vec3 mirror(const vec3& o) { return vec3(-o.x, o.y, -o.z); }
// out(d, m) = (m / flatlength(d)) * (-dz, 0, dx)
inline vec3 outward(const vec3& d, float m) {
    float l = flatLength(d);
    if (l <= 0) return vec3(0, 0, 0);
    return vec3(-d.z, 0, d.x) * (m / l);
}

uint32_t dimColor(uint32_t c) { return ((c >> 1) & 0x7F7F7Fu) | 0xFF000000u; }

uint32_t litColor(uint32_t c, uint32_t l) {
    if (c == 0xFFFFFFFFu) return l;
    if (l == 0xFFFFFFFFu) return c;
    uint32_t r = (((c >> 16) & 255) * ((l >> 16) & 255)) >> 8;
    uint32_t g = (((c >> 8) & 255) * ((l >> 8) & 255)) >> 8;
    uint32_t b = ((c & 255) * (l & 255)) >> 8;
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

// Strip u (psdl.md, "Texture coordinates along a strip").
std::vector<float> zigzagU(const std::vector<vec3>& q, float repeatLen, bool fromWidths,
                           const std::vector<vec3>* r) {
    const size_t m = q.size();
    std::vector<float> u(m, 0.0f);
    if (m < 2) return u;
    float S = 0, smax = 1;
    for (size_t i = 0; i + 1 < m; ++i) {
        float si = length(q[i + 1] - q[i]);
        S += si;
        smax = std::max(smax, si);
    }
    float L = repeatLen;
    if (fromWidths) {
        float W = 0;
        for (size_t i = 0; i + 1 < m; ++i) W += length(q[i] - (*r)[i]);
        if (W == 0) return u;
        L = W / float(m);
    }
    if (L == 0 || S < 0.1f) return u;
    float R = std::max(1.0f, std::floor(S / L + 0.5f));
    float k = R / S;
    if (k * smax > 128.0f) k = 127.0f / smax;
    float a = 0;
    for (size_t i = 0; i + 1 < m; ++i) {
        float si = length(q[i + 1] - q[i]);
        if (a > 0) a -= si;
        else a += si;
        u[i + 1] = a * k;
    }
    return u;
}

std::vector<float> stripU(const std::vector<vec3>& q, const std::vector<vec3>& r) { return zigzagU(q, 0, true, &r); }
std::vector<float> wallU(const std::vector<vec3>& q, float L) { return zigzagU(q, L, false, nullptr); }

struct Elem {
    int type = 0;
    int s = 0;
    int n = 0;
    size_t data = 0;  // index of the first data word
    size_t len = 0;   // data words
};

class RoomBuilder {
public:
    RoomBuilder(const Psdl& p, PsdlGeometry& g, const PsdlBuildOptions& o) : psdl_(p), geo_(g), opt_(o) {}

    void build(int roomIndex);

private:
    const Psdl& psdl_;
    PsdlGeometry& geo_;
    const PsdlBuildOptions& opt_;
    PsdlRoomGeometry* room_ = nullptr;
    const PsdlRoom* src_ = nullptr;
    int roomIndex_ = 0;
    std::unordered_map<int, size_t> slotIndex_;
    uint32_t color_ = 0xFFFFFFFFu;
    int light_ = 0;
    int badPoints_ = 0;

    uint16_t w(size_t i) const { return i < src_->words.size() ? src_->words[i] : 0; }
    vec3 P(uint16_t i) {
        if (i < psdl_.points.size()) return psdl_.points[i];
        ++badPoints_;
        return vec3(0, 0, 0);
    }
    float F(uint16_t i) {
        if (i < psdl_.heights.size()) return psdl_.heights[i];
        ++badPoints_;
        return 0.0f;
    }

    PsdlSurface& surface(int slot) {
        auto it = slotIndex_.find(slot);
        if (it != slotIndex_.end()) return room_->surfaces[it->second];
        slotIndex_[slot] = room_->surfaces.size();
        room_->surfaces.emplace_back();
        room_->surfaces.back().slot = slot;
        return room_->surfaces.back();
    }

    uint32_t emitVertex(PsdlSurface& s, const SV& v, uint32_t argb) {
        CityVertex cv;
        cv.pos = v.p;
        cv.uv.x = v.u;
        cv.uv.y = v.v;
        cv.color[0] = uint8_t(argb >> 16);
        cv.color[1] = uint8_t(argb >> 8);
        cv.color[2] = uint8_t(argb);
        cv.color[3] = uint8_t(argb >> 24);
        if (room_->empty) {
            room_->boundsMin = room_->boundsMax = v.p;
            room_->empty = false;
        } else {
            room_->boundsMin = vmin(room_->boundsMin, v.p);
            room_->boundsMax = vmax(room_->boundsMax, v.p);
        }
        s.vertices.push_back(cv);
        return uint32_t(s.vertices.size() - 1);
    }

    // Strip: (p0,p1,p2), (p2,p1,p3), (p2,p3,p4), (p4,p3,p5), ...
    void strip(int slot, uint32_t argb, const std::vector<SV>& pts) {
        if (pts.size() < 3) return;
        PsdlSurface& s = surface(slot);
        uint32_t base = uint32_t(s.vertices.size());
        for (const SV& v : pts) emitVertex(s, v, argb);
        for (size_t i = 0; i + 2 < pts.size(); ++i) {
            uint32_t a = base + uint32_t(i), b = a + 1, c = a + 2;
            if (i & 1) std::swap(a, b);
            s.indices.push_back(a);
            s.indices.push_back(b);
            s.indices.push_back(c);
            ++room_->triangles;
        }
    }

    // Fan: (p0,p1,p2), (p0,p2,p3), ...
    void fan(int slot, uint32_t argb, const std::vector<SV>& pts) {
        if (pts.size() < 3) return;
        PsdlSurface& s = surface(slot);
        uint32_t base = uint32_t(s.vertices.size());
        for (const SV& v : pts) emitVertex(s, v, argb);
        for (size_t i = 1; i + 1 < pts.size(); ++i) {
            s.indices.push_back(base);
            s.indices.push_back(base + uint32_t(i));
            s.indices.push_back(base + uint32_t(i + 1));
            ++room_->triangles;
        }
    }

    static SV flatSV(const vec3& p, float tile, const vec3& origin) {
        return SV{p, p.x / tile - std::floor(origin.x / tile), p.z / tile - std::floor(origin.z / tile)};
    }

    void road(const Elem& e, int t);
    void sidewalk(const Elem& e, int t);
    void groundStrip(const Elem& e, int t);
    void lowWall(const Elem& e, int t);
    void crosswalk(const Elem& e, int t);
    void flatFan(const Elem& e, int t);
    void dividedRoad(const Elem& e, int t);
    void tunnel(const std::vector<Elem>& elems, size_t index, int t);
    void junctionTunnel(const Elem& e, int t);
    void buildingWall(const Elem& e, int t);
    void roof(const Elem& e, int t);

    // Sidewalk + curb along the left (outer a, curb b) of a road.
    void leftSidewalk(const std::vector<vec3>& A, const std::vector<vec3>& B, int t);
    void rightSidewalk(const std::vector<vec3>& C, const std::vector<vec3>& D, int t);
};

void RoomBuilder::leftSidewalk(const std::vector<vec3>& A, const std::vector<vec3>& B, int t) {
    auto u = stripU(A, B);
    std::vector<SV> walk, curb;
    for (size_t i = 0; i < A.size(); ++i) {
        walk.push_back({A[i], u[i], 1});
        walk.push_back({up(B[i], kCurb), u[i], 0});
        curb.push_back({up(B[i], kCurb), u[i], 0});
        curb.push_back({B[i], u[i], 0});
    }
    strip(t + 1, color_, walk);
    strip(t + 1, dimColor(color_), curb);
}

void RoomBuilder::rightSidewalk(const std::vector<vec3>& C, const std::vector<vec3>& D, int t) {
    auto u = stripU(C, D);
    std::vector<SV> walk, curb;
    for (size_t i = 0; i < C.size(); ++i) {
        walk.push_back({up(C[i], kCurb), u[i], 0});
        walk.push_back({D[i], u[i], 1});
        curb.push_back({C[i], u[i], 0});
        curb.push_back({up(C[i], kCurb), u[i], 0});
    }
    strip(t + 1, color_, walk);
    strip(t + 1, dimColor(color_), curb);
}

void RoomBuilder::road(const Elem& e, int t) {
    const int n = e.n;
    std::vector<vec3> A(n), B(n), C(n), D(n);
    for (int i = 0; i < n; ++i) {
        A[i] = P(w(e.data + 4 * i));
        B[i] = P(w(e.data + 4 * i + 1));
        C[i] = P(w(e.data + 4 * i + 2));
        D[i] = P(w(e.data + 4 * i + 3));
    }
    if (n < 1) return;
    if (w(e.data) != w(e.data + 1)) leftSidewalk(A, B, t);
    if (w(e.data + 2) != w(e.data + 3)) rightSidewalk(C, D, t);
    auto u = stripU(B, C);
    std::vector<SV> s1, s2;
    for (int i = 0; i < n; ++i) {
        vec3 m = (B[i] + C[i]) * 0.5f;
        s1.push_back({B[i], u[i], 1});
        s1.push_back({m, u[i], 0});
        s2.push_back({m, u[i], 0});
        s2.push_back({C[i], u[i], 1});
    }
    strip(t, color_, s1);
    strip(t, color_, s2);
}

void RoomBuilder::sidewalk(const Elem& e, int t) {
    const int n = e.n;
    if (n < 1) return;
    uint16_t p0 = w(e.data), q0 = w(e.data + 1);
    if (n == 2 && ((p0 == 0 && q0 == 0) || (p0 == 1 && q0 == 1))) {
        // Curb end cap.
        vec3 A = P(w(e.data + 2)), B = P(w(e.data + 3));
        const vec3& o = A;
        if (p0 == 0) fan(t + 1, dimColor(color_), {flatSV(up(A, kCurb), 4, o), flatSV(B, 4, o), flatSV(A, 4, o)});
        else fan(t + 1, dimColor(color_), {flatSV(A, 4, o), flatSV(B, 4, o), flatSV(up(A, kCurb), 4, o)});
        return;
    }
    const vec3 origin = P(p0);
    std::vector<SV> curb, surf;
    for (int i = 0; i < n; ++i) {
        vec3 p = P(w(e.data + 2 * i)), q = P(w(e.data + 2 * i + 1));
        curb.push_back(flatSV(p, 4, origin));
        curb.push_back(flatSV(up(p, kCurb), 4, origin));
        surf.push_back(flatSV(up(p, kCurb), 4, origin));
        surf.push_back(flatSV(q, 4, origin));
    }
    strip(t + 1, dimColor(color_), curb);
    strip(t + 1, color_, surf);
}

void RoomBuilder::groundStrip(const Elem& e, int t) {
    const int n = e.n;
    std::vector<vec3> Pp(n), Q(n);
    for (int i = 0; i < n; ++i) {
        Pp[i] = P(w(e.data + 2 * i));
        Q[i] = P(w(e.data + 2 * i + 1));
    }
    auto u = stripU(Pp, Q);
    std::vector<SV> s;
    for (int i = 0; i < n; ++i) {
        s.push_back({Pp[i], u[i], 0});
        s.push_back({Q[i], u[i], 1});
    }
    strip(t, color_, s);
}

void RoomBuilder::lowWall(const Elem& e, int t) {
    const float top = F(w(e.data));
    const float sigma = F(w(e.data + 1));
    const vec3 A = P(w(e.data + 2)), B = P(w(e.data + 3));
    const float ue = std::floor(flatLength(B - A) * sigma + 0.5f);
    const float vA = (A.y - top) * sigma, vB = (B.y - top) * sigma;
    const vec3 At(A.x, top, A.z), Bt(B.x, top, B.z);
    const uint32_t c = litColor(color_, opt_.wallLights[light_ & 63]);
    // A triangle at an end above the top faces inward and is never visible (psdl.md, "Ends above
    // the top"): the one with corner B′ needs B below the top, the one with corner A′ needs A.
    const bool keepB = B.y < top, keepA = A.y < top;
    if (vA != 0 && vB != 0) {
        if (keepB) fan(t, c, {{A, 0, vA}, {B, ue, vB}, {Bt, ue, 0}});
        if (keepA) fan(t, c, {{A, 0, vA}, {Bt, ue, 0}, {At, 0, 0}});
    } else if (vA == 0 && vB != 0) {
        if (keepB) fan(t, c, {{A, 0, 0}, {B, ue, vB}, {Bt, ue, 0}});
    } else if (vA != 0 && vB == 0) {
        if (keepA) fan(t, c, {{A, 0, vA}, {B, ue, 0}, {At, 0, 0}});
    }
}

void RoomBuilder::crosswalk(const Elem& e, int t) {
    uint16_t i0 = w(e.data), i1 = w(e.data + 1);
    vec3 p0 = P(i0), p1 = P(i1), p2 = P(w(e.data + 2)), p3 = P(w(e.data + 3));
    float wdt = (i0 == i1) ? length(p2 - p3) : length(p0 - p1);
    float l = length(p0 - p2);
    float r = wdt > 0 ? l / wdt : 0;
    strip(t + 2, color_, {{p1, 0, 0}, {p0, 1, 0}, {p3, 0, r}, {p2, 1, r}});
}

void RoomBuilder::flatFan(const Elem& e, int t) {
    const int cnt = e.n + 2;
    std::vector<SV> pts;
    const vec3 origin = P(w(e.data));
    for (int i = 0; i < cnt; ++i) pts.push_back(flatSV(P(w(e.data + i)), 8, origin));
    fan(t, color_, pts);
}

void RoomBuilder::dividedRoad(const Elem& e, int t) {
    const uint16_t w0 = w(e.data), w1 = w(e.data + 1);
    const int n = e.n;
    if (n < 1) return;
    const size_t base = e.data + 2;
    std::vector<vec3> A(n), B(n), C(n), D(n), E(n), Fv(n);
    for (int i = 0; i < n; ++i) {
        A[i] = P(w(base + 6 * i));
        B[i] = P(w(base + 6 * i + 1));
        C[i] = P(w(base + 6 * i + 2));
        D[i] = P(w(base + 6 * i + 3));
        E[i] = P(w(base + 6 * i + 4));
        Fv[i] = P(w(base + 6 * i + 5));
    }
    if (w(base) != w(base + 1)) leftSidewalk(A, B, t);
    if (w(base + 4) != w(base + 5)) rightSidewalk(E, Fv, t);
    {
        auto u = stripU(B, C);
        std::vector<SV> s1, s2;
        for (int i = 0; i < n; ++i) {
            s1.push_back({B[i], u[i], 1});
            s1.push_back({C[i], u[i], 0});
            s2.push_back({D[i], u[i], 0});
            s2.push_back({E[i], u[i], 1});
        }
        strip(t, color_, s1);
        strip(t, color_, s2);
    }
    // Median.
    const int kind = w0 & 0x3F;
    const bool startCap = (w0 & 0x40) != 0, endCap = (w0 & 0x80) != 0;
    const int mt = w0 >> 8;
    const float h = float(w1) / 256.0f;
    auto u = stripU(C, D);
    const uint32_t dim = dimColor(color_);
    if (kind == 1) {
        std::vector<SV> s;
        for (int i = 0; i < n; ++i) {
            s.push_back({C[i], u[i], 0});
            s.push_back({D[i], u[i], h});
        }
        strip(mt + 1, color_, s);
    } else if (kind == 2) {
        std::vector<vec3> I(n), J(n), K(n);
        for (int i = 0; i < n; ++i) {
            vec3 o = unit3(D[i] - C[i]) * h + vec3(0, h, 0);
            I[i] = C[i] + o;
            J[i] = D[i] + mirror(o);
            K[i] = D[i] + unit3(C[i] - D[i]) * h + vec3(0, h, 0);
        }
        std::vector<SV> a1, a2, b1, b2, c1;
        for (int i = 0; i < n; ++i) {
            a1.push_back({C[i], u[i], 0});
            a1.push_back({up(C[i], h), u[i], h});
            a2.push_back({up(D[i], h), u[i], 0});
            a2.push_back({D[i], u[i], 1});
            b1.push_back({up(C[i], h), u[i], 0});
            b1.push_back({I[i], u[i], 1});
            b2.push_back({K[i], u[i], 1});
            b2.push_back({up(D[i], h), u[i], 0});
            c1.push_back({I[i], u[i], 0});
            c1.push_back({J[i], u[i], 1});
        }
        strip(mt, dim, a1);
        strip(mt, dim, a2);
        strip(mt + 1, color_, b1);
        strip(mt + 1, color_, b2);
        strip(mt + 2, color_, c1);
        if (startCap)
            strip(mt + 3, color_, {{C[0], 0, 0}, {D[0], 1, 0}, {up(C[0], h), 1, 1}, {up(D[0], h), 0, 1}});
        if (endCap) {
            const vec3& c = C[n - 1];
            const vec3& d = D[n - 1];
            strip(startCap ? mt + 3 : mt + 2, color_, {{d, 0, 0}, {c, 1, 0}, {up(d, h), 1, 1}, {up(c, h), 0, 1}});
        }
    } else if (kind == 3) {
        std::vector<vec3> L(n), R(n);
        for (int i = 0; i < n; ++i) {
            L[i] = unit3(D[i] - C[i]) * 0.4f + vec3(0, 1, 0);
            R[i] = unit3(C[i] - D[i]) * 0.4f + vec3(0, 1, 0);
        }
        std::vector<SV> s1, s2, s3;
        for (int i = 0; i < n; ++i) {
            s1.push_back({C[i], u[i], 0});
            s1.push_back({C[i] + L[i], u[i], 1});
            s2.push_back({D[i] + R[i], u[i], 1});
            s2.push_back({D[i], u[i], 0});
            s3.push_back({C[i] + L[i], u[i], 0});
            s3.push_back({D[i] + mirror(L[i]), u[i], 1});
        }
        strip(mt + 1, color_, s1);
        strip(mt + 1, color_, s2);
        strip(mt + 2, color_, s3);
        if (startCap)
            strip(mt + 3, color_, {{C[0], 0, 0}, {D[0], 1, 0}, {C[0] + L[0], 1, 1}, {D[0] + mirror(L[0]), 0, 1}});
        if (endCap) {
            const int l = n - 1;
            strip(startCap ? mt + 3 : mt + 2, color_,
                  {{D[l], 0, 0}, {C[l], 1, 0}, {D[l] + R[l], 1, 1}, {C[l] + mirror(R[l]), 0, 1}});
        }
    } else if (kind != 0) {
        geo_.problems.push_back(strf("room %d: unknown median kind %d", roomIndex_, kind));
    }
}

void RoomBuilder::tunnel(const std::vector<Elem>& elems, size_t index, int t) {
    const Elem& e = elems[index];
    if (e.n == 10) {
        junctionTunnel(e, t);
        return;
    }
    if (e.n != 3) {
        geo_.problems.push_back(strf("room %d: tunnel with %d words", roomIndex_, e.n));
        return;
    }
    const uint16_t flags = w(e.data);
    const float h = float(w(e.data + 1)) / 256.0f;
    // The road it follows: the next element, skipping one texture element.
    size_t ri = index + 1;
    if (ri < elems.size() && elems[ri].type == 10) ++ri;
    if (ri >= elems.size()) return;
    const Elem& r = elems[ri];
    int per = 0;
    size_t start = r.data;
    if (r.type == 0) per = 4;
    else if (r.type == 8) { per = 6; start += 2; }
    else if (r.type == 2) per = 2;
    else {
        geo_.problems.push_back(strf("room %d: tunnel followed by element type %d", roomIndex_, r.type));
        return;
    }
    const int n = r.n & 0xFF;
    if (n < 1) return;
    std::vector<vec3> L(n), Lp(n), R(n), Rp(n), O(n), Q(n);
    const float k = 0.333f * h;
    for (int i = 0; i < n; ++i) {
        L[i] = P(w(start + size_t(per * i)));
        Lp[i] = P(w(start + size_t(per * i + 1)));
        R[i] = P(w(start + size_t(per * i + per - 1)));
        Rp[i] = P(w(start + size_t(per * i + per - 2)));
        O[i] = L[i] + flatUnit(L[i] - Lp[i]) * k;
        Q[i] = R[i] + flatUnit(R[i] - Rp[i]) * k;
    }
    auto u = wallU(L, h);
    const uint32_t c = color_;
    const bool curved = (flags & 0x2000) != 0;
    if (!curved) {
        if (flags & 0x0001) {
            std::vector<SV> s, sw;
            for (int i = 0; i < n; ++i) {
                s.push_back({up(L[i], h), u[i], 1});
                s.push_back({L[i], u[i], 0});
                sw.push_back({L[i], u[i], 0});
                sw.push_back({up(L[i], h), u[i], 1});
            }
            strip(t, c, s);
            if (flags & 0x4000) strip(t, c, sw);
        }
        if (flags & 0x0002) {
            std::vector<SV> s, sw;
            for (int i = 0; i < n; ++i) {
                s.push_back({R[i], u[i], 0});
                s.push_back({up(R[i], h), u[i], 1});
                sw.push_back({up(R[i], h), u[i], 1});
                sw.push_back({R[i], u[i], 0});
            }
            strip(t + 1, c, s);
            if (flags & 0x4000) strip(t + 1, c, sw);
        }
    } else {
        for (int j = 0; j + 1 < n; ++j) {
            const bool first = j == 0, last = j + 1 == n - 1;
            if (flags & 0x0001) {
                vec3 x75p = (last && (flags & 0x20)) ? up(L[j + 1], 0.75f * h) : up(O[j + 1], 0.75f * h);
                vec3 x25p = (last && (flags & 0x20)) ? up(L[j + 1], 0.25f * h) : up(O[j + 1], 0.25f * h);
                vec3 x75 = (first && (flags & 0x10)) ? up(L[j], h) : up(O[j], 0.75f * h);
                vec3 x25 = (first && (flags & 0x10)) ? up(L[j], 0.25f * h) : up(O[j], 0.25f * h);
                strip(t, c, {{up(L[j + 1], h), u[j + 1], 1}, {up(L[j], h), u[j], 1}, {x75p, u[j + 1], 0.75f},
                             {x75, u[j], 0.75f}, {x25p, u[j + 1], 0.25f}, {x25, u[j], 0.25f},
                             {L[j + 1], u[j + 1], 0}, {L[j], u[j], 0}});
            }
            if (flags & 0x0002) {
                vec3 y75 = (first && (flags & 0x40)) ? up(R[j], 0.75f * h) : up(Q[j], 0.75f * h);
                vec3 y25 = (first && (flags & 0x40)) ? up(R[j], 0.25f * h) : up(Q[j], 0.25f * h);
                vec3 y75p = (last && (flags & 0x80)) ? up(R[j + 1], 0.75f * h) : up(Q[j + 1], 0.75f * h);
                vec3 y25p = (last && (flags & 0x80)) ? up(R[j + 1], 0.25f * h) : up(Q[j + 1], 0.25f * h);
                strip(t + 1, c, {{up(R[j], h), u[j], 1}, {up(R[j + 1], h), u[j + 1], 1}, {y75, u[j], 0.75f},
                                 {y75p, u[j + 1], 0.75f}, {y25, u[j], 0.25f}, {y25p, u[j + 1], 0.25f},
                                 {R[j], u[j], 0}, {R[j + 1], u[j + 1], 0}});
            }
        }
    }
    if ((flags & 0x0004) && n >= 2) {
        std::vector<vec3> Oc = O, Qc = Q;
        auto corr = [&](const vec3& d) {
            float l = flatLength(d);
            return l > 0 ? vec3(-d.z, 0, d.x) * (1.414f * k / l) : vec3(0, 0, 0);
        };
        Oc[0] = L[0] + flatUnit(L[0] - Lp[0]) * k;
        Oc[n - 1] = L[n - 1] + flatUnit(L[n - 1] - Lp[n - 1]) * k;
        if (flags & 0x0200) Oc[0] += corr(L[0] - Lp[0]);
        if (flags & 0x0400) Oc[n - 1] -= corr(L[n - 1] - Lp[n - 1]);
        Qc[0] = R[0] + flatUnit(R[0] - Rp[0]) * k;
        if (flags & 0x0800) Qc[0] -= corr(R[0] - Rp[0]);
        if (flags & 0x0001) {
            std::vector<SV> top, side;
            for (int i = 0; i < n; ++i) {
                top.push_back({up(Oc[i], h), u[i], 0});
                top.push_back({up(L[i], h), u[i], 1});
            }
            if (flags & 0x0010) {
                side.push_back({L[0], -1, 1});
                side.push_back({up(L[0], h), -1, 0});
            }
            for (int i = 0; i < n; ++i) {
                side.push_back({up(Oc[i], -1), u[i], 1});
                side.push_back({up(Oc[i], h), u[i], 0});
            }
            if (flags & 0x0020) {
                side.push_back({L[n - 1], u[n - 1] + 1, 1});
                side.push_back({up(L[n - 1], h), u[n - 1] + 1, 0});
            }
            strip(t + 3, c, top);
            strip(t + 3, c, side);
        }
        if (flags & 0x0002) {
            std::vector<SV> top, side;
            for (int i = 0; i < n; ++i) {
                top.push_back({up(R[i], h), u[i], 0});
                top.push_back({up(Qc[i], h), u[i], 1});
            }
            if (flags & 0x0040) {
                side.push_back({up(R[0], h), -1, 0});
                side.push_back({R[0], -1, 1});
            }
            for (int i = 0; i < n; ++i) {
                side.push_back({up(Qc[i], h), u[i], 0});
                side.push_back({up(Qc[i], -1), u[i], 1});
            }
            if (flags & 0x0080) {
                side.push_back({up(R[n - 1], h), u[n - 1] + 1, 0});
                side.push_back({R[n - 1], u[n - 1] + 1, 1});
            }
            strip(t + 4, c, top);
            strip(t + 4, c, side);
        }
        if ((flags & 0x0003) == 0x0003) {
            // Underside, with the plain (uncorrected) outward points.
            auto u2 = stripU(L, R);
            std::vector<SV> s;
            for (int i = 0; i < n; ++i) {
                s.push_back({up(Q[i], -1), u2[i], 0});
                s.push_back({up(O[i], -1), u2[i], 1});
            }
            strip(t + 5, c, s);
        }
    }
    if ((flags & 0x0008) && !(flags & 0x0100)) {
        auto u3 = stripU(L, R);
        std::vector<SV> s;
        for (int i = 0; i < n; ++i) {
            s.push_back({up(R[i], h), u3[i], 0});
            s.push_back({up(L[i], h), u3[i], 1});
        }
        strip(t + 2, c, s);
    }
    if (flags & 0x0100) {
        auto u3 = stripU(L, R);
        for (int j = 0; j + 1 < n; ++j) {
            const bool first = j == 0, last = j + 1 == n - 1;
            auto rise = [&](int sec, float f) -> float {
                if (sec == j && first && (flags & 0x10)) return 0.0f;
                if (sec == j + 1 && last && (flags & 0x20)) return 0.0f;
                return f == 0.5f ? 2.0f : 1.5f;
            };
            std::vector<SV> s;
            const float fs[5] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
            for (int q = 0; q < 5; ++q) {
                for (int sec : {j + 1, j}) {
                    vec3 D = L[sec] - R[sec];
                    D.y = 0;
                    vec3 p;
                    if (q == 0) p = up(R[sec], h);
                    else if (q == 4) p = up(L[sec], h);
                    else p = R[sec] + D * fs[q] + vec3(0, h + rise(sec, fs[q]), 0);
                    s.push_back({p, u3[sec], fs[q]});
                }
            }
            strip(t + 2, c, s);
        }
    }
}

void RoomBuilder::junctionTunnel(const Elem& e, int t) {
    const uint16_t flags = w(e.data);
    const float h = float(w(e.data + 1)) / 256.0f;
    if (h == 0) return;
    const int s = w(e.data + 3);
    const uint32_t walls = uint32_t(w(e.data + 4)) | (uint32_t(w(e.data + 5)) << 16);
    const uint32_t bendA = uint32_t(w(e.data + 6)) | (uint32_t(w(e.data + 7)) << 16);
    const uint32_t bendB = uint32_t(w(e.data + 8)) | (uint32_t(w(e.data + 9)) << 16);
    const int N = int(src_->perimeter.size());
    if (N < 2) return;
    std::vector<vec3> Pp(N);
    float ymax = -1e30f;
    for (int i = 0; i < N; ++i) {
        Pp[i] = P(src_->perimeter[size_t(i)]);
        ymax = std::max(ymax, Pp[i].y);
    }
    auto at = [&](int i) { return Pp[size_t(((i % N) + N) % N)]; };
    auto edge = [&](uint32_t mask, int i) { return i >= 0 && i < 32 && ((mask >> i) & 1u); };
    const float k = 0.333f * h;
    const uint32_t c = color_;
    for (int i = 0; i < N && i < 32; ++i) {
        if (!edge(walls, i)) continue;
        vec3 a = at(i), b = at(i - 1);
        float u = std::max(1.0f, length(a - b) / h);
        strip(t, c, {{up(a, h), 0, 1}, {a, 0, 0}, {up(b, h), u, 1}, {b, u, 0}});
        if ((flags & 0x4000) && !(flags & 0x0004))
            strip(t, c, {{a, 0, 0}, {up(a, h), 0, 1}, {b, u, 0}, {up(b, h), u, 1}});
    }
    if (flags & 0x0008) {
        std::vector<SV> f;
        const vec3 origin = at(s);
        for (int i = 0; i < N; ++i) {
            vec3 p = at(s - i);
            p.y = ymax + h;
            f.push_back(flatSV(p, 8, origin));
        }
        fan(t + 2, c, f);
    }
    if (flags & 0x0004) {
        std::vector<SV> f;
        for (int ci = N - 1; ci >= 0; --ci) {
            int p = (ci + 1) % N, q = (ci - 1 + N) % N;
            vec3 off;
            if (edge(walls, p)) off = outward(at(p) - at(ci), k);
            else if (edge(walls, ci)) off = outward(at(ci) - at(q), k);
            else off = outward(at(p) - at(ci), 0.25f);
            vec3 pt = at(ci) + off;
            pt.y = ymax - 1;
            f.push_back({pt, pt.x / 8.0f, pt.z / 8.0f});
        }
        fan(t + 5, c, f);
        for (int i = 0; i < N && i < 32; ++i) {
            if (!edge(walls, i)) continue;
            vec3 o = outward(at(i) - at(i - 1), k);
            vec3 oA = o, oB = o;
            if (edge(bendA, i)) oA = vec3(o.x - 1.414f * o.z, 0, o.z - 1.414f * o.x);
            if (edge(bendB, i)) oB = vec3(o.x - 1.414f * o.z, 0, o.z - 1.414f * o.x);
            vec3 A = at(i), B = at(i - 1);
            A.y = ymax;
            B.y = ymax;
            float u = std::max(1.0f, flatLength(at(i) - at(i - 1)) / h);
            strip(t + 4, c, {{up(A + oA, -1), 0, 0}, {up(A + oA, h), 0, 1}, {up(B + oB, -1), u, 0}, {up(B + oB, h), u, 1}});
            strip(t + 4, c, {{up(A + oA, h), 0, 0}, {up(A, h), 0, 1}, {up(B + oB, h), u, 0}, {up(B, h), u, 1}});
        }
    }
}

void RoomBuilder::buildingWall(const Elem& e, int t) {
    const float bottom = F(w(e.data)), top = F(w(e.data + 1));
    const float U = float(w(e.data + 2)), V = float(w(e.data + 3));
    if (bottom > top) {
        // Inverted walls (bottom above top) face inward and are never seen.
        ++geo_.skippedInvertedWalls;
        return;
    }
    const vec3 A = P(w(e.data + 4)), B = P(w(e.data + 5));
    const uint32_t c = litColor(color_, opt_.wallLights[light_ & 63]);
    fan(t, c, {{vec3(A.x, bottom, A.z), 0, 0}, {vec3(B.x, bottom, B.z), U, 0}, {vec3(B.x, top, B.z), U, V},
               {vec3(A.x, top, A.z), 0, V}});
}

void RoomBuilder::roof(const Elem& e, int t) {
    const float hgt = F(w(e.data));
    const int cnt = e.n + 1;
    std::vector<SV> pts;
    vec3 origin = P(w(e.data + 1));
    for (int i = 0; i < cnt; ++i) {
        vec3 p = P(w(e.data + 1 + size_t(i)));
        p.y = hgt;
        pts.push_back(flatSV(p, 8, origin));
    }
    fan(t, color_, pts);
}

void RoomBuilder::build(int roomIndex) {
    roomIndex_ = roomIndex;
    src_ = &psdl_.rooms[size_t(roomIndex)];
    room_ = &geo_.rooms[size_t(roomIndex)];
    slotIndex_.clear();
    light_ = 0;
    color_ = (opt_.roomColors.size() == psdl_.rooms.size()) ? opt_.roomColors[size_t(roomIndex)] : 0xFFFFFFFFu;

    // Split the words into elements first (a road tunnel needs to look ahead).
    std::vector<Elem> elems;
    const size_t W = src_->words.size();
    size_t pos = 0;
    bool ended = false;
    bool walkHidden = false;  // a texture element of value 0 is in force
    while (pos < W) {
        const uint16_t hdr = src_->words[pos++];
        Elem e;
        e.s = hdr & 7;
        e.type = (hdr >> 3) & 15;
        const bool last = (hdr & 0x80) != 0;
        if (e.type == 10) {
            if (e.s == 0) {
                if (pos >= W) break;
                const uint16_t v = src_->words[pos++];
                e.n = v;
                e.data = pos;
                e.len = 0;
                if (v & 0x0700) ++pos;  // the game skips a word here
                walkHidden = v == 0;
            } else {
                e.n = e.s;
                e.data = pos;
                e.len = 1;
                walkHidden = pos < W && ((e.s << 8) | src_->words[pos]) == 0;
                pos += 1;
            }
        } else {
            int n = e.s;
            if (n == 0) {
                if (pos >= W) break;
                n = src_->words[pos++];
            }
            e.n = n;
            size_t len = 0;
            switch (e.type) {
                case 0: len = size_t(4) * n; break;
                case 1: case 2: len = size_t(2) * n; break;
                case 3: case 4: case 7: len = 4; break;
                case 5: case 6: case 12: len = size_t(n) + 2; break;
                case 8: len = 2 + size_t(6) * n; break;
                case 9: len = size_t(n); break;
                case 11: len = 6; break;
                default:
                    geo_.problems.push_back(strf("room %d: unknown element type %d", roomIndex, e.type));
                    return;
            }
            e.data = pos;
            e.len = len;
            // A hidden divided road is stepped over by only 5n + 2 of its words (psdl.md).
            pos += (e.type == 8 && walkHidden) ? size_t(5) * n + 2 : len;
            if (pos > W) {
                geo_.problems.push_back(strf("room %d: element type %d runs past the room", roomIndex, e.type));
                return;
            }
        }
        if (e.type < 13) ++geo_.elementCounts[e.type];
        elems.push_back(e);
        if (last) {
            ended = true;
            break;
        }
    }
    if (ended && pos < W) ++geo_.spareWordRooms;

    int t = 0;
    bool zeroTexture = false;
    for (size_t i = 0; i < elems.size(); ++i) {
        const Elem& e = elems[i];
        if (e.type == 10) {
            t = e.s == 0 ? e.n : ((e.s << 8) | w(e.data));
            zeroTexture = (t == 0);
            continue;
        }
        if (e.type == 7) {
            light_ = w(e.data) & 63;
            continue;
        }
        if (zeroTexture) {
            // Surfaces draw nothing until the next texture; walls, tunnels and roofs draw
            // untextured (slot 0).
            switch (e.type) {
                case 3: lowWall(e, 0); break;
                case 9: tunnel(elems, i, 0); break;
                case 11: buildingWall(e, 0); break;
                case 12: roof(e, 0); break;
                default: break;
            }
            continue;
        }
        switch (e.type) {
            case 0: road(e, t); break;
            case 1: sidewalk(e, t); break;
            case 2: groundStrip(e, t); break;
            case 3: lowWall(e, t); break;
            case 4: crosswalk(e, t); break;
            case 5: case 6: flatFan(e, t); break;
            case 8: dividedRoad(e, t); break;
            case 9: tunnel(elems, i, t); break;
            case 11: buildingWall(e, t); break;
            case 12: roof(e, t); break;
            default: break;
        }
    }
    if (badPoints_) {
        geo_.problems.push_back(strf("room %d: %d point/height indices out of range", roomIndex, badPoints_));
        badPoints_ = 0;
    }
}

}  // namespace

PsdlGeometry buildPsdlGeometry(const Psdl& psdl, const PsdlBuildOptions& opt) {
    PsdlGeometry g;
    g.rooms.resize(psdl.rooms.size());
    RoomBuilder b(psdl, g, opt);
    for (size_t r = 1; r < psdl.rooms.size(); ++r) {
        b.build(int(r));
        g.triangles += g.rooms[r].triangles;
    }
    return g;
}

}  // namespace cc
