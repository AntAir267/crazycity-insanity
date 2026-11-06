// Float32 helpers for the vehicle physics. Every operation is written out so that it rounds
// once, in the order the spec gives (spec/physics/vehicle.md, "Precision"): sums left to right,
// products left to right, no fused multiply-add (build with -ffp-contract=off).
#pragma once

#include <cmath>

#include "core/math.h"

namespace cc {
namespace phys {

inline float fsign(float x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }
inline float fabsf_(float x) { return std::fabs(x); }
inline float fsqrt(float x) { return std::sqrt(x); }
inline float fsin(float x) { return float(std::sin(double(x))); }
inline float fcos(float x) { return float(std::cos(double(x))); }

// (a.x*b.x + a.y*b.y) + a.z*b.z
inline float pdot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 pcross(const vec3& a, const vec3& b) {
    return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
inline vec3 vadd(const vec3& a, const vec3& b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 vsub(const vec3& a, const vec3& b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 vscale(const vec3& a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
// a + b*s per component (b*s + a)
inline vec3 vmadd(const vec3& a, const vec3& b, float s) { return vec3(b.x * s + a.x, b.y * s + a.y, b.z * s + a.z); }

// Rotation stored as rows: the body's X, Y, Z axes in world space.
struct M3 {
    vec3 r[3];
    static M3 identity() {
        M3 m;
        m.r[0] = vec3(1, 0, 0);
        m.r[1] = vec3(0, 1, 0);
        m.r[2] = vec3(0, 0, 1);
        return m;
    }
    float at(int i, int j) const { return (&r[i].x)[j]; }
    float& at(int i, int j) { return (&r[i].x)[j]; }
};

// Generic 3x3 for the implicit integration (row-major).
struct Mat3 {
    float m[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
};

// C[i][j] = (A[i][0]*B[0][j] + A[i][1]*B[1][j]) + A[i][2]*B[2][j]
inline Mat3 mmul(const Mat3& a, const Mat3& b) {
    Mat3 c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return c;
}

inline Mat3 toMat3(const M3& r) {
    Mat3 c;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) c.m[i][j] = r.at(i, j);
    return c;
}

inline M3 fromMat3(const Mat3& c) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.at(i, j) = c.m[i][j];
    return r;
}

// p.x*X + p.y*Y + p.z*Z summed left to right per component.
inline vec3 localToWorld(const vec3& p, const M3& m) {
    return vec3(p.x * m.r[0].x + p.y * m.r[1].x + p.z * m.r[2].x, p.x * m.r[0].y + p.y * m.r[1].y + p.z * m.r[2].y,
                p.x * m.r[0].z + p.y * m.r[1].z + p.z * m.r[2].z);
}

// Turn by `heading` about world +Y: rows (cos, 0, -sin), (0, 1, 0), (sin, 0, cos).
inline M3 headingRotation(float c, float s) {
    M3 m;
    m.r[0] = vec3(c, 0.0f, -s);
    m.r[1] = vec3(0.0f, 1.0f, 0.0f);
    m.r[2] = vec3(s, 0.0f, c);
    return m;
}

}  // namespace phys
}  // namespace cc
