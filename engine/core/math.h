// 32-bit float vector and matrix math. Matrices are column-major (OpenGL layout):
// m[col * 4 + row]. World space follows the game: right-handed, +Y up, metres.
#pragma once

#include <cmath>
#include <cstring>

namespace cc {

constexpr float kPi = 3.14159265358979323846f;

struct vec2 {
    float x = 0, y = 0;
};

struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    constexpr vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    vec3 operator+(const vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    vec3 operator-(const vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    vec3 operator-() const { return {-x, -y, -z}; }
    vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    vec3& operator+=(const vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    vec3& operator-=(const vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline float dot(const vec3& a, const vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(const vec3& a, const vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const vec3& v) { return std::sqrt(dot(v, v)); }
inline vec3 normalize(const vec3& v) {
    float l = length(v);
    return l > 0 ? v / l : vec3(0, 0, 0);
}
inline vec3 vmin(const vec3& a, const vec3& b) {
    return {a.x < b.x ? a.x : b.x, a.y < b.y ? a.y : b.y, a.z < b.z ? a.z : b.z};
}
inline vec3 vmax(const vec3& a, const vec3& b) {
    return {a.x > b.x ? a.x : b.x, a.y > b.y ? a.y : b.y, a.z > b.z ? a.z : b.z};
}

struct vec4 {
    float x = 0, y = 0, z = 0, w = 0;
    vec4() = default;
    constexpr vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct mat4 {
    float m[16];

    static mat4 identity() {
        mat4 r;
        std::memset(r.m, 0, sizeof r.m);
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }

    mat4 operator*(const mat4& b) const {
        mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float s = 0;
                for (int k = 0; k < 4; ++k) s += at(rr, k) * b.at(k, c);
                r.at(rr, c) = s;
            }
        return r;
    }

    vec3 transformPoint(const vec3& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
                m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    vec3 transformDir(const vec3& d) const {
        return {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z,
                m[2] * d.x + m[6] * d.y + m[10] * d.z};
    }
    vec3 translation() const { return {m[12], m[13], m[14]}; }

    static mat4 translate(const vec3& t) {
        mat4 r = identity();
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    static mat4 scale(const vec3& s) {
        mat4 r = identity();
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }
    static mat4 rotateY(float a) {
        mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[0] = c;
        r.m[2] = -s;
        r.m[8] = s;
        r.m[10] = c;
        return r;
    }
    static mat4 rotateX(float a) {
        mat4 r = identity();
        float c = std::cos(a), s = std::sin(a);
        r.m[5] = c;
        r.m[6] = s;
        r.m[9] = -s;
        r.m[10] = c;
        return r;
    }

    // A placement stored in game files: X axis, Y axis, Z axis, position (12 floats).
    static mat4 fromGameMatrix(const float f[12]) {
        mat4 r = identity();
        for (int i = 0; i < 3; ++i) {
            r.m[0 + i] = f[0 + i];
            r.m[4 + i] = f[3 + i];
            r.m[8 + i] = f[6 + i];
            r.m[12 + i] = f[9 + i];
        }
        return r;
    }

    static mat4 perspective(float fovyRadians, float aspect, float zNear, float zFar) {
        mat4 r;
        std::memset(r.m, 0, sizeof r.m);
        float f = 1.0f / std::tan(fovyRadians * 0.5f);
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1.0f;
        r.m[14] = 2.0f * zFar * zNear / (zNear - zFar);
        return r;
    }

    static mat4 lookAt(const vec3& eye, const vec3& target, const vec3& up) {
        vec3 f = normalize(target - eye);
        vec3 s = normalize(cross(f, up));
        vec3 u = cross(s, f);
        mat4 r = identity();
        r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
        r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
        r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
        r.m[12] = -dot(s, eye);
        r.m[13] = -dot(u, eye);
        r.m[14] = dot(f, eye);
        return r;
    }
};

// Upper-left 3x3 inverse-transpose (for normals), column-major 9 floats.
inline void normalMatrix(const mat4& a, float out[9]) {
    float m00 = a.at(0, 0), m01 = a.at(0, 1), m02 = a.at(0, 2);
    float m10 = a.at(1, 0), m11 = a.at(1, 1), m12 = a.at(1, 2);
    float m20 = a.at(2, 0), m21 = a.at(2, 1), m22 = a.at(2, 2);
    float c00 = m11 * m22 - m12 * m21, c01 = m12 * m20 - m10 * m22, c02 = m10 * m21 - m11 * m20;
    float det = m00 * c00 + m01 * c01 + m02 * c02;
    if (std::fabs(det) < 1e-12f) {
        // Degenerate (e.g. a flattened shadow): normals are unused there; return identity.
        float id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        std::memcpy(out, id, sizeof id);
        return;
    }
    float inv = 1.0f / det;
    // inverse-transpose = cofactor matrix / det
    float c10 = m02 * m21 - m01 * m22, c11 = m00 * m22 - m02 * m20, c12 = m01 * m20 - m00 * m21;
    float c20 = m01 * m12 - m02 * m11, c21 = m02 * m10 - m00 * m12, c22 = m00 * m11 - m01 * m10;
    // column-major: out[col*3+row] = cofactor(row, col) / det
    out[0] = c00 * inv; out[1] = c10 * inv; out[2] = c20 * inv;
    out[3] = c01 * inv; out[4] = c11 * inv; out[5] = c21 * inv;
    out[6] = c02 * inv; out[7] = c12 * inv; out[8] = c22 * inv;
}

}  // namespace cc
