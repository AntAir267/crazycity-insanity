#include "physics/city_collision.h"

#include <cmath>
#include <map>

#include "city/city_data.h"
#include "core/bytes.h"
#include "core/strutil.h"
#include "physics/pmath.h"
#include "vfs/vfs.h"

namespace cc {
namespace phys {

namespace {

const float kCurb = 0.15f;

vec3 normalize0(const vec3& v) {
    const float L = pdot(v, v);
    if (L == 0.0f) return vec3(0, 0, 0);
    return vscale(v, 1.0f / fsqrt(L));
}

inline vec3 up(const vec3& p, float h) { return vec3(p.x, p.y + h, p.z); }
inline vec3 atY(const vec3& p, float y) { return vec3(p.x, y, p.z); }
inline float flat2(const vec3& a, const vec3& b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return dx * dx + dz * dz;
}

struct Elem {
    int type = 0;
    int s = 0;
    int n = 0;
    size_t data = 0;
    int texValue = 0;
};

class Builder {
public:
    Builder(const Psdl& p, const CityCollision& cc, RoomPolys& out, int room, bool triangleMode)
        : psdl_(p), cc_(cc), out_(out), room_(room), tri_(triangleMode) {}

    void run(const std::vector<uint16_t>& words, const std::vector<uint16_t>& perimeter);

private:
    const Psdl& psdl_;
    const CityCollision& cc_;
    RoomPolys& out_;
    int room_;
    bool tri_;
    const std::vector<uint16_t>* words_ = nullptr;
    const std::vector<uint16_t>* perim_ = nullptr;
    int t_ = 0;
    int curPass_ = -1, curSection_ = 0, curGroup_ = -1;
    int curType_ = -1, curCat_ = 0, wallCat_ = 1;

    uint16_t w(size_t i) const { return i < words_->size() ? (*words_)[i] : 0; }
    vec3 V(uint16_t i) const { return i < psdl_.points.size() ? psdl_.points[i] : vec3(); }
    float F(uint16_t i) const { return i < psdl_.heights.size() ? psdl_.heights[i] : 0.0f; }
    int ms() const { return cc_.slotCode(t_ + 1); }
    int mr() const { return cc_.slotCode(t_); }

    void emit(const vec3* c, int n, int material) {
        CollPoly p;
        p.n = n;
        for (int k = 0; k < n; ++k) p.c[k] = c[k];
        p.material = material;
        if (!finishCollPoly(p)) {
            ++cc_.mutableStats().dropped;
            if (curType_ >= 0 && curType_ < 16) ++cc_.mutableStats().droppedByType[curType_];
            return;
        }
        if (n == 3) {
            const float e1 = flat2(p.c[0], p.c[1]), e2 = flat2(p.c[1], p.c[2]), e3 = flat2(p.c[2], p.c[0]);
            if (e1 <= e2) {
                p.L2 = e2 > e3 ? e2 : e3;
                p.R = p.c[2];
            } else {
                p.L2 = e1 > e3 ? e1 : e3;
                p.R = p.c[0];
            }
        } else {
            p.R = p.c[0];
            p.L2 = flat2(p.c[0], p.c[2]);
        }
        p.pass = curPass_;
        p.section = curSection_;
        p.flatGroup = curGroup_;
        p.elemType = curType_;
        p.category = curCat_;
        p.room = room_;
        p.index = int(out_.polys.size());
        out_.polys.push_back(p);
    }
    void tri(const vec3& a, const vec3& b, const vec3& c, int m) {
        const vec3 v[3] = {a, b, c};
        emit(v, 3, m);
    }
    void quad(const vec3& a, const vec3& b, const vec3& c, const vec3& d, int m) {
        const vec3 v[4] = {a, b, c, d};
        emit(v, 4, m);
    }
    // Strip quad S(p, hp; q, hq; p', hp'; q', hq').
    void strip(uint16_t p, float hp, uint16_t q, float hq, uint16_t p2, float hp2, uint16_t q2, float hq2, int m) {
        if (q == q2) tri(up(V(p), hp), up(V(q), hq), up(V(p2), hp2), m);
        else if (p == p2) tri(up(V(p), hp), up(V(q), hq), up(V(q2), hq2), m);
        else if (p2 == 0 && hp2 == 0.0f) {
            ++cc_.mutableStats().fourthIndex;
            tri(up(V(p), hp), up(V(q), hq), up(V(q2), hq2), m);
        }
        else quad(up(V(p), hp), up(V(q), hq), up(V(q2), hq2), up(V(p2), hp2), m);
    }
    // Wall W(A, B, yA, yB) from positions.
    void wallPos(const vec3& A, const vec3& B, float yA, float yB, int m) {
        curCat_ = wallCat_;
        quad(A, B, atY(B, yB), atY(A, yA), m);
        curCat_ = 0;
    }
    void wall(uint16_t p, uint16_t q, float yp, float yq, int m) { wallPos(V(p), V(q), yp, yq, m); }

    int beginPass(const std::vector<std::pair<uint16_t, uint16_t>>& lines) {
        StripPass sp;
        for (const auto& l : lines) {
            const vec3 P = V(l.first);
            const vec3 D = vsub(V(l.second), P);
            const float len2 = D.x * D.x + D.z * D.z;
            sp.P.push_back(P);
            sp.D.push_back(D);
            sp.w.push_back(len2 > 0.0f ? 1.0f / std::sqrt(len2) : 0.0f);
        }
        out_.passes.push_back(sp);
        curPass_ = int(out_.passes.size() - 1);
        return curPass_;
    }
    void endPass() { curPass_ = -1; curSection_ = 0; }

    void leftSidewalk(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b);
    void rightSidewalk(const std::vector<uint16_t>& c, const std::vector<uint16_t>& d);
    void road(const Elem& e);
    void sidewalk(const Elem& e);
    void groundStrip(const Elem& e);
    void crosswalk(const Elem& e);
    void fan(const Elem& e, bool intersection);
    void wallLight(const Elem& e);
    void dividedRoad(const Elem& e);
    void tunnel(const std::vector<Elem>& elems, size_t i);
    void roof(const Elem& e);
};

void Builder::leftSidewalk(const std::vector<uint16_t>& a, const std::vector<uint16_t>& b) {
    const size_t n = a.size();
    std::vector<std::pair<uint16_t, uint16_t>> lines;
    for (size_t i = 0; i < n; ++i) lines.emplace_back(a[i], b[i]);
    beginPass(lines);
    for (size_t i = 0; i + 1 < n; ++i) {
        curSection_ = int(i);
        if (a[i] == b[i]) continue;
        if (tri_) {
            tri(V(a[i]), up(V(b[i]), kCurb), V(a[i + 1]), ms());
            tri(up(V(b[i]), kCurb), up(V(b[i + 1]), kCurb), V(a[i + 1]), ms());
        } else {
            strip(a[i], 0.0f, b[i], kCurb, a[i + 1], 0.0f, b[i + 1], kCurb, ms());
        }
        wall(b[i], b[i + 1], V(b[i]).y + kCurb, V(b[i + 1]).y + kCurb, ms());
    }
    endPass();
}

void Builder::rightSidewalk(const std::vector<uint16_t>& c, const std::vector<uint16_t>& d) {
    const size_t n = c.size();
    std::vector<std::pair<uint16_t, uint16_t>> lines;
    for (size_t i = 0; i < n; ++i) lines.emplace_back(c[i], d[i]);
    beginPass(lines);
    for (size_t i = 0; i + 1 < n; ++i) {
        curSection_ = int(i);
        if (c[i] == d[i]) continue;
        if (tri_) {
            tri(up(V(c[i]), kCurb), V(d[i]), up(V(c[i + 1]), kCurb), ms());
            tri(V(d[i]), V(d[i + 1]), up(V(c[i + 1]), kCurb), ms());
        } else {
            strip(c[i], kCurb, d[i], 0.0f, c[i + 1], kCurb, d[i + 1], 0.0f, ms());
        }
        wall(c[i + 1], c[i], V(c[i + 1]).y + kCurb, V(c[i]).y + kCurb, ms());
    }
    endPass();
}

void Builder::road(const Elem& e) {
    const int n = e.n;
    if (n < 1) return;
    std::vector<uint16_t> a(n), b(n), c(n), d(n);
    for (int i = 0; i < n; ++i) {
        a[i] = w(e.data + 4 * size_t(i));
        b[i] = w(e.data + 4 * size_t(i) + 1);
        c[i] = w(e.data + 4 * size_t(i) + 2);
        d[i] = w(e.data + 4 * size_t(i) + 3);
    }
    leftSidewalk(a, b);
    {
        std::vector<std::pair<uint16_t, uint16_t>> lines;
        for (int i = 0; i < n; ++i) lines.emplace_back(b[i], c[i]);
        beginPass(lines);
        for (int i = 0; i + 1 < n; ++i) {
            curSection_ = i;
            if (tri_) {
                tri(V(b[i]), V(c[i]), V(b[i + 1]), ms());
                tri(V(c[i]), V(c[i + 1]), V(b[i + 1]), ms());
            } else {
                strip(b[i], 0, c[i], 0, b[i + 1], 0, c[i + 1], 0, mr());
            }
        }
        endPass();
    }
    rightSidewalk(c, d);
}

void Builder::sidewalk(const Elem& e) {
    const int n = e.n;
    if (n < 1) return;
    const uint16_t p0 = w(e.data), q0 = w(e.data + 1);
    if (n == 2 && ((p0 == 0 && q0 == 0) || (p0 == 1 && q0 == 1))) {
        const vec3 A = V(w(e.data + 2)), B = V(w(e.data + 3));
        if (p0 == 0) tri(up(A, kCurb), B, A, ms());
        else tri(A, B, up(A, kCurb), ms());
        return;
    }
    for (int i = 0; i + 1 < n; ++i) {
        const uint16_t p = w(e.data + 2 * size_t(i)), q = w(e.data + 2 * size_t(i) + 1);
        const uint16_t p2 = w(e.data + 2 * size_t(i) + 2), q2 = w(e.data + 2 * size_t(i) + 3);
        strip(p, kCurb, q, 0.0f, p2, kCurb, q2, 0.0f, ms());
        wall(p2, p, V(p2).y + kCurb, V(p).y + kCurb, ms());
    }
}

void Builder::groundStrip(const Elem& e) {
    const int n = e.n;
    if (n < 1) return;
    std::vector<std::pair<uint16_t, uint16_t>> lines;
    for (int i = 0; i < n; ++i) lines.emplace_back(w(e.data + 2 * size_t(i)), w(e.data + 2 * size_t(i) + 1));
    beginPass(lines);
    for (int i = 0; i + 1 < n; ++i) {
        curSection_ = i;
        const uint16_t p = lines[size_t(i)].first, q = lines[size_t(i)].second;
        const uint16_t p2 = lines[size_t(i) + 1].first, q2 = lines[size_t(i) + 1].second;
        if (tri_) {
            tri(V(p), V(q), V(p2), ms());
            tri(V(q), V(q2), V(p2), ms());
        } else {
            strip(p, 0, q, 0, p2, 0, q2, 0, mr());
        }
    }
    endPass();
}

void Builder::crosswalk(const Elem& e) {
    const uint16_t i0 = w(e.data), i1 = w(e.data + 1), i2 = w(e.data + 2), i3 = w(e.data + 3);
    const float y = V(i0).y;
    const int m = cc_.slotCode(t_ + 2);
    if (i3 == 0 && V(i3).y == y) tri(atY(V(i1), y), atY(V(i0), y), atY(V(i2), y), m);
    else quad(atY(V(i1), y), atY(V(i0), y), atY(V(i2), y), atY(V(i3), y), m);
}

void Builder::fan(const Elem& e, bool intersection) {
    if (mr() == 2) return;  // deep water is not solid
    const int cnt = e.n + 2;
    const vec3 p0 = V(w(e.data));
    if (intersection) {
        out_.groupY.push_back(p0.y);
        curGroup_ = int(out_.groupY.size() - 1);
        for (int k = 2; k < cnt; ++k)
            tri(p0, atY(V(w(e.data + size_t(k) - 1)), p0.y), atY(V(w(e.data + size_t(k))), p0.y), mr());
        curGroup_ = -1;
    } else {
        for (int k = 2; k < cnt; ++k) tri(p0, V(w(e.data + size_t(k) - 1)), V(w(e.data + size_t(k))), mr());
    }
}

void Builder::wallLight(const Elem& e) {
    const float h = F(w(e.data + 1));
    wall(w(e.data + 2), w(e.data + 3), h, h, mr());
}

void Builder::dividedRoad(const Elem& e) {
    const uint16_t w0 = w(e.data), w1 = w(e.data + 1);
    const int n = e.n;
    if (n < 1) return;
    const size_t base = e.data + 2;
    std::vector<uint16_t> a(n), b(n), c(n), d(n), ee(n), f(n);
    for (int i = 0; i < n; ++i) {
        a[i] = w(base + 6 * size_t(i));
        b[i] = w(base + 6 * size_t(i) + 1);
        c[i] = w(base + 6 * size_t(i) + 2);
        d[i] = w(base + 6 * size_t(i) + 3);
        ee[i] = w(base + 6 * size_t(i) + 4);
        f[i] = w(base + 6 * size_t(i) + 5);
    }
    leftSidewalk(a, b);
    const int kind = w0 & 0x3F;
    const int mt = w0 >> 8;
    const float h = float(w1) / 256.0f;
    {
        std::vector<std::pair<uint16_t, uint16_t>> lines;
        for (int i = 0; i < n; ++i) lines.emplace_back(b[i], ee[i]);
        beginPass(lines);
        for (int i = 0; i + 1 < n; ++i) {
            curSection_ = i;
            if (tri_) {
                tri(V(b[i]), V(c[i]), V(b[i + 1]), ms());
                tri(V(c[i]), V(c[i + 1]), V(b[i + 1]), ms());
            } else {
                strip(b[i], 0, c[i], 0, b[i + 1], 0, c[i + 1], 0, mr());
            }
            if (kind == 1) {
                if (tri_) {
                    tri(V(c[i]), V(d[i]), V(c[i + 1]), ms());
                    tri(V(d[i]), V(d[i + 1]), V(c[i + 1]), ms());
                } else {
                    strip(c[i], 0, d[i], 0, c[i + 1], 0, d[i + 1], 0, cc_.slotCode(mt + 1));
                }
            } else {
                wallCat_ = 2;  // median side (diagnostics only)
                wall(c[i + 1], c[i], h + V(c[i + 1]).y, h + V(c[i]).y, ms());
                wall(d[i], d[i + 1], h + V(d[i]).y, h + V(d[i + 1]).y, ms());
                wallCat_ = 1;
                if (tri_) {
                    tri(V(c[i]), V(d[i]), V(c[i + 1]), ms());
                    tri(V(d[i]), V(d[i + 1]), V(c[i + 1]), ms());
                } else {
                    strip(c[i], h, d[i], h, c[i + 1], h, d[i + 1], h, cc_.slotCode(mt + 2));
                }
            }
            if (tri_) {
                tri(V(d[i]), V(ee[i]), V(d[i + 1]), ms());
                tri(V(ee[i]), V(ee[i + 1]), V(d[i + 1]), ms());
            } else {
                strip(d[i], 0, ee[i], 0, d[i + 1], 0, ee[i + 1], 0, mr());
            }
        }
        endPass();
    }
    rightSidewalk(ee, f);
}

void Builder::tunnel(const std::vector<Elem>& elems, size_t idx) {
    const Elem& e = elems[idx];
    const uint16_t flags = w(e.data);
    float c = float(w(e.data + 2)) / 256.0f;
    if (c < 3.0f) c = 3.0f;
    const int m = mr();
    if (e.n == 10) {
        const uint32_t walls = uint32_t(w(e.data + 4)) | (uint32_t(w(e.data + 5)) << 16);
        const std::vector<uint16_t>& per = *perim_;
        const int N = int(per.size());
        for (int i = 0; i < N && i < 32; ++i) {
            if (!((walls >> i) & 1u)) continue;
            const uint16_t pi = per[size_t(i)], pp = per[size_t((i - 1 + N) % N)];
            wall(pi, pp, V(pi).y + c, V(pp).y + c, m);
        }
        return;
    }
    if (e.n != 3) return;
    size_t ri = idx + 1;
    if (ri < elems.size() && elems[ri].type == 10) ++ri;
    if (ri >= elems.size()) return;
    const Elem& r = elems[ri];
    int per = 0;
    size_t start = r.data;
    if (r.type == 0) per = 4;
    else if (r.type == 8) { per = 6; start += 2; }
    else if (r.type == 2) per = 2;
    else return;
    const int n = r.n & 0xFF;
    if (n < 1) return;
    const float k = (float(w(e.data + 1)) / 256.0f) * 0.333f;
    std::vector<vec3> L(n), R(n), O(n), Q(n);
    for (int i = 0; i < n; ++i) {
        const vec3 Li = V(w(start + size_t(per * i))), Lp = V(w(start + size_t(per * i + 1)));
        const vec3 Ri = V(w(start + size_t(per * i + per - 1))), Rp = V(w(start + size_t(per * i + per - 2)));
        L[i] = Li;
        R[i] = Ri;
        // A step of k away from B, only X and Z (city-collision.md, road tunnels); no guard.
        auto outward = [k](const vec3& A, const vec3& B) {
            const float dx = A.x - B.x, dz = A.z - B.z;
            const float f = k / std::sqrt(dx * dx + dz * dz);
            return vec3(A.x - (B.x - A.x) * f, A.y, A.z - (B.z - A.z) * f);
        };
        O[i] = outward(Li, Lp);
        Q[i] = outward(Ri, Rp);
    }
    if (!(flags & 0x2000)) {
        for (int j = 0; j + 1 < n; ++j) {
            if (flags & 0x0001) wallPos(L[j], L[j + 1], L[j].y + c, L[j + 1].y + c, m);
            if (flags & 0x0002) wallPos(R[j + 1], R[j], R[j + 1].y + c, R[j].y + c, m);
        }
        if ((flags & 0x0001) && (flags & 0x0004)) {
            if (flags & 0x0010) wallPos(O[0], L[0], O[0].y + c, L[0].y + c, m);
            for (int j = 0; j + 1 < n; ++j) wallPos(O[j + 1], O[j], O[j + 1].y + c, O[j].y + c, m);
            if (flags & 0x0020) wallPos(L[n - 1], O[n - 1], L[n - 1].y + c, O[n - 1].y + c, m);
        }
        if ((flags & 0x0002) && (flags & 0x0004)) {
            if (flags & 0x0040) wallPos(R[0], Q[0], R[0].y + c, Q[0].y + c, m);
            for (int j = 0; j + 1 < n; ++j) wallPos(Q[j], Q[j + 1], Q[j].y + c, Q[j + 1].y + c, m);
            if (flags & 0x0080) wallPos(Q[n - 1], R[n - 1], Q[n - 1].y + c, R[n - 1].y + c, m);
        }
    } else {
        const float q = c / 4.0f;
        if (flags & 0x0001)
            for (int j = 0; j + 1 < n; ++j) {
                const vec3 X = (j == 0 && (flags & 0x0200)) ? L[j] : O[j];
                const vec3 Y = (j == n - 2 && (flags & 0x0400)) ? L[j + 1] : O[j + 1];
                quad(L[j], L[j + 1], up(Y, q), up(X, q), m);
                wallPos(X, Y, X.y + c, Y.y + c, m);
            }
        if (flags & 0x0002)
            for (int j = 0; j + 1 < n; ++j) {
                const vec3 X = (j == 0 && (flags & 0x0800)) ? R[j] : Q[j];
                const vec3 Y = (j == n - 2 && (flags & 0x1000)) ? R[j + 1] : Q[j + 1];
                quad(R[j + 1], R[j], up(X, q), up(Y, q), m);
                wallPos(Y, X, Y.y + c, X.y + c, m);
            }
    }
}

void Builder::roof(const Elem& e) {
    const float y = F(w(e.data));
    const int cnt = e.n + 1;  // p0 .. pn
    out_.groupY.push_back(y);
    curGroup_ = int(out_.groupY.size() - 1);
    const vec3 p0 = atY(V(w(e.data + 1)), y);
    for (int k = 2; k < cnt; ++k)
        tri(p0, atY(V(w(e.data + 1 + size_t(k) - 1)), y), atY(V(w(e.data + 1 + size_t(k))), y), mr());
    curGroup_ = -1;
}

void Builder::run(const std::vector<uint16_t>& words, const std::vector<uint16_t>& perimeter) {
    words_ = &words;
    perim_ = &perimeter;
    std::vector<Elem> elems;
    const size_t W = words.size();
    size_t pos = 0;
    bool walkHidden = false;  // a texture element of value 0 is in force
    while (pos < W) {
        const uint16_t hdr = words[pos++];
        Elem e;
        e.s = hdr & 7;
        e.type = (hdr >> 3) & 15;
        const bool last = (hdr & 0x80) != 0;
        if (e.type == 10) {
            if (e.s == 0) {
                if (pos >= W) break;
                const uint16_t v = words[pos++];
                e.texValue = v;
                if (v & 0x0700) ++pos;
            } else {
                if (pos >= W) break;
                e.texValue = (e.s << 8) | words[pos++];
            }
            walkHidden = e.texValue == 0;
        } else {
            int n = e.s;
            if (n == 0) {
                if (pos >= W) break;
                n = words[pos++];
            }
            e.n = n;
            size_t len = 0;
            switch (e.type) {
                case 0: len = 4 * size_t(n); break;
                case 1: case 2: len = 2 * size_t(n); break;
                case 3: case 4: case 7: len = 4; break;
                case 5: case 6: case 12: len = size_t(n) + 2; break;
                case 8: len = 2 + 6 * size_t(n); break;
                case 9: len = size_t(n); break;
                case 11: len = 6; break;
                default: return;
            }
            e.data = pos;
            // A hidden divided road is stepped over by only 5n + 2 words (city-collision.md).
            pos += (e.type == 8 && walkHidden) ? 5 * size_t(n) + 2 : len;
            if (pos > W) return;
        }
        elems.push_back(e);
        if (last) break;
    }
    bool hidden = false;
    t_ = 0;
    for (size_t i = 0; i < elems.size(); ++i) {
        const Elem& e = elems[i];
        if (e.type == 10) {
            t_ = e.texValue;
            hidden = (t_ == 0);
            continue;
        }
        if (hidden && (e.type == 0 || e.type == 1 || e.type == 2 || e.type == 4 || e.type == 5 || e.type == 6 ||
                       e.type == 8))
            continue;
        curType_ = e.type;
        switch (e.type) {
            case 0: road(e); break;
            case 1: sidewalk(e); break;
            case 2: groundStrip(e); break;
            case 4: crosswalk(e); break;
            case 5: fan(e, true); break;
            case 6: fan(e, false); break;
            case 7: wallLight(e); break;
            case 8: dividedRoad(e); break;
            case 9: tunnel(elems, i); break;
            case 12: roof(e); break;
            default: break;  // 3 and 11: no collision
        }
    }
}

}  // namespace

bool finishCollPoly(CollPoly& p) {
    const vec3& c0 = p.c[0];
    const vec3& c1 = p.c[1];
    const vec3& c2 = p.c[2];
    if (c0.y == c1.y && c1.y == c2.y) {
        p.normal = vec3(0.0f, 1.0f, 0.0f);
    } else {
        const vec3 x = pcross(vsub(c2, c1), vsub(c0, c1));
        const float L = pdot(x, x);
        if (L == 0.0f) return false;
        p.normal = vscale(x, 1.0f / fsqrt(L));
    }
    for (int k = 0; k < p.n; ++k) {
        const vec3 e = vsub(p.c[(k + 1) % p.n], p.c[k]);
        p.edgeN[k] = normalize0(pcross(p.normal, e));
    }
    return true;
}

void CityCollision::build(const CityData& city, const Vfs& vfs, const MaterialTable& table) {
    const Psdl& psdl = city.psdl();
    // Material codes of texture slots (city-collision.md).
    std::map<std::string, int> texCode;
    {
        BytesPtr csv = vfs.open("city/materials.csv");
        if (csv) {
            auto lines = split(std::string(csv->begin(), csv->end()), '\n');
            for (size_t i = 1; i < lines.size(); ++i) {
                auto f = split(trim(lines[i]), ',');
                if (f.size() < 2) continue;
                const std::string tex = trim(f[0]), phys = trim(f[1]);
                if (phys == "none") continue;
                const int idx = table.find(phys);
                if (idx < 0) continue;
                texCode.emplace(tex, idx + 1);  // the first line wins
            }
        }
    }
    slotCodes_.assign(psdl.textures.size(), 0);
    for (size_t s = 1; s < psdl.textures.size(); ++s) {
        auto it = texCode.find(Psdl::lookupName(psdl.textures[s]));
        if (it != texCode.end()) slotCodes_[s] = it->second;
    }
    stats_ = CityCollisionStats();
    rooms_.assign(psdl.rooms.size(), RoomPolys());
    for (size_t r = 1; r < psdl.rooms.size(); ++r) {
        const PsdlRoom& room = psdl.rooms[r];
        Builder b(psdl, *this, rooms_[r], int(r), (room.flags & 0x20) != 0);
        b.run(room.words, room.perimeter);
        if (room.flags & 0x20) {
            ++stats_.triModeRooms;
            stats_.triModePolys += int(rooms_[r].polys.size());
        }
    }
}

size_t CityCollision::polygonCount() const {
    size_t n = 0;
    for (const auto& r : rooms_) n += r.polys.size();
    return n;
}

namespace {
// Section range of one strip pass for the sphere (c, r): sections lo .. hi - 1.
void sectionRange(const StripPass& sp, const vec3& c, float r, int& lo, int& hi) {
    const int n = int(sp.P.size());
    auto d = [&](int i) {
        const vec3& P = sp.P[size_t(i)];
        const vec3& D = sp.D[size_t(i)];
        return ((P.z - c.z) * D.x - (P.x - c.x) * D.z) * sp.w[size_t(i)];
    };
    lo = 0;
    hi = n - 1;
    if (n < 3) return;
    int m = n;
    int k = 0;
    for (;;) {
        k = lo + m / 2;
        const float dk = d(k);
        if (dk > -r) {
            lo = k;
            if (dk < r) break;
            m = (m + 1) / 2;
        } else {
            hi = k;
            m = m / 2 + 1;
        }
        if (m < 3) return;
    }
    while (lo >= 1 && d(lo) < r) --lo;
    int j = k;
    while (j < hi && d(j) > -r) ++j;
    hi = j;
}
}  // namespace

void CityCollision::gather(int room, const vec3& c, float r, std::vector<const CollPoly*>& out, int* budget) const {
    if (room <= 0 || size_t(room) >= rooms_.size()) return;
    const RoomPolys& rp = rooms_[size_t(room)];
    std::vector<int> lo(rp.passes.size(), -1), hi(rp.passes.size(), -1);
    std::vector<signed char> groupOk(rp.groupY.size(), -1);
    const float r2 = r * r;
    for (const CollPoly& p : rp.polys) {
        if (p.pass >= 0) {
            if (lo[size_t(p.pass)] < 0) sectionRange(rp.passes[size_t(p.pass)], c, r, lo[size_t(p.pass)], hi[size_t(p.pass)]);
            if (p.section < lo[size_t(p.pass)] || p.section > hi[size_t(p.pass)] - 1) continue;
        }
        if (p.flatGroup >= 0) {
            signed char& g = groupOk[size_t(p.flatGroup)];
            if (g < 0) {
                const float y = rp.groupY[size_t(p.flatGroup)];
                g = ((c.y + r - y) * (c.y - r - y) < 0.0f) ? 1 : 0;
            }
            if (!g) continue;
        }
        const float dx = c.x - p.R.x, dz = c.z - p.R.z;
        if (!(dx * dx + dz * dz < 2.0f * (p.L2 + r2))) continue;
        if (budget) {
            if (*budget <= 0) return;
            --*budget;
        }
        out.push_back(&p);
    }
}

}  // namespace phys
}  // namespace cc
