// MM2's particle system (spec/formats/particles.md): birth rules (.asBirthRule and the same keys
// in prop data and .vehCarDamage), emitters with a fixed pool, birth, burst, update, and the
// camera-facing squares to draw. Visual only; every random number is the effects generator's.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/math.h"

namespace cc {

class Vfs;

namespace dat {
struct Schema;
}

namespace fx {

struct BirthRule {
    vec3 position, positionVar, velocity, velocityVar;
    float life = 1, lifeVar = 0, mass = 1, massVar = 0, radius = 1, radiusVar = 0;
    float drag = 0, dragVar = 0, damp = 1, dampVar = 0, dRadius = 0, dRadiusVar = 0;
    int dAlpha = 0, dAlphaVar = 0, dRotation = 0, dRotationVar = 0, initialBlast = 0;
    float spewRate = 0, spewTimeLimit = 0, gravity = -9.8f;
    int texFrameStart = 0, texFrameEnd = 0, birthFlags = 0;
    float height = 0, intensity = 1;
    int color = -1;  // A·2^24 + B·2^16 + G·2^8 + R as an unsigned number
};

// The rule's keys as dat-text fields writing into `r` (to embed in another file's schema).
void addBirthRuleFields(BirthRule& r, dat::Schema& s);
// Reads a whole .asBirthRule file; false if it does not exist.
bool loadBirthRule(const Vfs& vfs, const std::string& path, BirthRule& out);

struct Particle {
    vec3 v, p;
    float life = 0, im = 0, drag = 0, damp = 0, g = 0, dr = 0, radius = 0, h = 0;
    int8_t da = 0, drot = 0, frame = 0;
    uint8_t f0 = 0, f1 = 0, rot = 0, flags = 0;
    uint8_t r0 = 255, g0 = 255, b0 = 255;
    uint8_t a = 255, r = 255, g_ = 255, b = 255;  // current colour
};

class Emitter {
public:
    void setup(int capacity, int cols, int rows);
    // count = 0, follow none, t = 0 (carry kept).
    void reset();
    // Blast(n, rule): the rule passed in, else the emitter's spew rule.
    void blast(int n, const BirthRule* r = nullptr);
    void update(float dt);
    // Births placed with this matrix (rows X, Y, Z and position), or none.
    void setFollow(const vec3& X, const vec3& Y, const vec3& Z, const vec3& P);
    void clearFollow() { follow_ = false; }

    const BirthRule* rule = nullptr;  // spew rule
    float bright = 1.0f;
    int count() const { return count_; }
    int capacity() const { return int(pool_.size()); }
    const Particle& particle(int i) const { return pool_[size_t(i)]; }
    int cols() const { return cols_; }
    int rows() const { return rows_; }
    float time() const { return t_; }

private:
    void birth(const BirthRule& r);
    std::vector<Particle> pool_;
    int count_ = 0;
    int cols_ = 1, rows_ = 1;
    bool follow_ = false;
    vec3 fx_, fy_, fz_, fp_;
    float t_ = 0, carry_ = 0;
    vec3 wind_;
};

// One textured, coloured vertex of the effects pass.
struct FxVertex {
    vec3 pos;
    float u = 0, v = 0;
    uint8_t color[4] = {255, 255, 255, 255};  // R, G, B, A
};

// The emitter's live particles as triangles (two per square, slot order), facing the camera
// (right axis X, up axis Y); the flag-0x10 ground shadows go first (particles.md, "Drawing").
void particleTriangles(const Emitter& e, const vec3& camX, const vec3& camY, std::vector<FxVertex>& out);

}  // namespace fx
}  // namespace cc
