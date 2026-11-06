#include "formats/pathset.h"

#include <cmath>
#include <cstring>

namespace cc {

namespace {
uint32_t rdU32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
float rdF32(const uint8_t* p) {
    const uint32_t u = rdU32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// X × A and Z × X as MM2 groups them (A = (0, 1, 0)).
vec3 crossXA(const vec3& X, const vec3& A) {
    return vec3(A.z * X.y - X.z * A.y, X.z * A.x - A.z * X.x, A.y * X.x - A.x * X.y);
}
vec3 crossZX(const vec3& Z, const vec3& X) {
    return vec3(X.z * Z.y - Z.z * X.y, Z.z * X.x - X.z * Z.x, Z.x * X.y - Z.y * X.x);
}
bool nonZero(const vec3& v) { return v.x != 0.0f || v.y != 0.0f || v.z != 0.0f; }
}  // namespace

bool parsePathset(const uint8_t* data, size_t size, std::vector<PathsetPath>& out, std::string& err) {
    out.clear();
    if (size < 12 || std::memcmp(data, "PTH1", 4) != 0) {
        err = "not a PTH1 path set";
        return false;
    }
    const uint32_t count = rdU32(data + 4);
    if (count > 192) {
        err = "more than 192 paths";
        return false;
    }
    size_t pos = 12;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos + 40 > size) break;
        PathsetPath p;
        const char* nm = reinterpret_cast<const char*>(data + pos);
        p.name.assign(nm, strnlen(nm, 32));
        const uint32_t n = rdU32(data + pos + 32);
        pos += 40;
        if (uint64_t(n) * 16 + 4 > size - pos) break;  // ends inside the path: drop it and the rest
        for (uint32_t k = 0; k < n; ++k) {
            const uint8_t* q = data + pos + size_t(k) * 16;
            p.points.push_back(vec3(rdF32(q + 4), rdF32(q + 8), rdF32(q + 12)));
        }
        pos += size_t(n) * 16;
        p.mode = data[pos];
        p.spacing = data[pos + 1];
        pos += 4;
        out.push_back(std::move(p));
    }
    return true;
}

std::vector<PathPlacement> pathPlacements(const PathsetPath& p) {
    std::vector<PathPlacement> out;
    const auto& pts = p.points;
    const size_t n = pts.size();
    const vec3 A(0.0f, 1.0f, 0.0f);
    if (p.mode == 0) {
        for (const vec3& q : pts) out.push_back({vec3(1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), q, false});
    } else if (p.mode == 1) {
        for (size_t i = 0; i + 1 < n; i += 2) {
            vec3 X(pts[i + 1].x - pts[i].x, 0.0f, pts[i + 1].z - pts[i].z);
            const float L2 = X.z * X.z + X.x * X.x;
            const float k = L2 == 0.0f ? 0.0f : 1.0f / std::sqrt(L2);
            X = vec3(k * X.x, k * X.y, k * X.z);
            const vec3 Z = crossXA(X, A);
            const vec3 Y = crossZX(Z, X);
            out.push_back({X, Y, Z, pts[i], false});
        }
    } else if (p.mode == 2) {
        const float spacing = p.spacing == 0 ? 5.0f : float(p.spacing) * 0.25f;
        for (size_t i = 0; i + 1 < n; ++i) {
            const vec3 d(pts[i].x - pts[i + 1].x, pts[i].y - pts[i + 1].y, pts[i].z - pts[i + 1].z);
            float L = std::sqrt((d.z * d.z + d.x * d.x) + d.y * d.y);
            vec3 X(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y, pts[i + 1].z - pts[i].z);
            const float L2 = (X.z * X.z + X.x * X.x) + X.y * X.y;
            const float k = L2 == 0.0f ? 0.0f : 1.0f / std::sqrt(L2);
            X = vec3(k * X.x, k * X.y, k * X.z);
            const vec3 Z = crossXA(X, A);
            const vec3 Y = crossZX(Z, X);
            const float q = L / spacing;
            const float fq = std::floor(q);
            if (!(fq >= 1.0f)) continue;
            const float step = L / fq;
            if (!(step > 0.0f)) continue;
            vec3 P = pts[i];
            for (int guard = 0; L >= spacing && guard < 100000; ++guard) {
                if (nonZero(X) && nonZero(Y) && nonZero(Z)) out.push_back({X, Y, Z, P, true});
                P = vec3(X.x * step + P.x, step * X.y + P.y, X.z * step + P.z);
                L = L - step;
            }
        }
    }
    return out;
}

}  // namespace cc
