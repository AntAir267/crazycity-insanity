#include "fx/particles.h"

#include <cmath>

#include "core/bytes.h"
#include "core/game_random.h"
#include "formats/dat_text.h"
#include "vfs/vfs.h"

namespace cc {
namespace fx {

void addBirthRuleFields(BirthRule& r, dat::Schema& s) {
    using dat::FieldType;
    const dat::Field fields[] = {
        {"Position", FieldType::Vector, &r.position, nullptr},
        {"PositionVar", FieldType::Vector, &r.positionVar, nullptr},
        {"Velocity", FieldType::Vector, &r.velocity, nullptr},
        {"VelocityVar", FieldType::Vector, &r.velocityVar, nullptr},
        {"Life", FieldType::Float, &r.life, nullptr},
        {"LifeVar", FieldType::Float, &r.lifeVar, nullptr},
        {"Mass", FieldType::Float, &r.mass, nullptr},
        {"MassVar", FieldType::Float, &r.massVar, nullptr},
        {"Radius", FieldType::Float, &r.radius, nullptr},
        {"RadiusVar", FieldType::Float, &r.radiusVar, nullptr},
        {"Drag", FieldType::Float, &r.drag, nullptr},
        {"DragVar", FieldType::Float, &r.dragVar, nullptr},
        {"Damp", FieldType::Float, &r.damp, nullptr},
        {"DampVar", FieldType::Float, &r.dampVar, nullptr},
        {"DRadius", FieldType::Float, &r.dRadius, nullptr},
        {"DRadiusVar", FieldType::Float, &r.dRadiusVar, nullptr},
        {"DAlpha", FieldType::Int, &r.dAlpha, nullptr},
        {"DAlphaVar", FieldType::Int, &r.dAlphaVar, nullptr},
        {"DRotation", FieldType::Int, &r.dRotation, nullptr},
        {"DRotationVar", FieldType::Int, &r.dRotationVar, nullptr},
        {"InitialBlast", FieldType::Int, &r.initialBlast, nullptr},
        {"SpewRate", FieldType::Float, &r.spewRate, nullptr},
        {"SpewTimeLimit", FieldType::Float, &r.spewTimeLimit, nullptr},
        {"Gravity", FieldType::Float, &r.gravity, nullptr},
        {"TexFrameStart", FieldType::Int, &r.texFrameStart, nullptr},
        {"TexFrameEnd", FieldType::Int, &r.texFrameEnd, nullptr},
        {"BirthFlags", FieldType::Int, &r.birthFlags, nullptr},
        {"Height", FieldType::Float, &r.height, nullptr},
        {"Intensity", FieldType::Float, &r.intensity, nullptr},
        {"Color", FieldType::Int, &r.color, nullptr},
    };
    for (const dat::Field& f : fields) s.fields.push_back(f);
}

bool loadBirthRule(const Vfs& vfs, const std::string& path, BirthRule& out) {
    BytesPtr b = vfs.open(path);
    if (!b) return false;
    dat::Schema s;
    addBirthRuleFields(out, s);
    dat::parseText(std::string(b->begin(), b->end()), s);
    return true;
}

namespace {
// vary(base, var) = ((r − 0.5)·var) + base
inline float vary(float base, float var) { return ((effectsRandomUnit() - 0.5f) * var) + base; }
}  // namespace

void Emitter::setup(int capacity, int cols, int rows) {
    pool_.assign(size_t(capacity > 0 ? capacity : 0), Particle());
    cols_ = cols > 0 ? cols : 1;
    rows_ = rows > 0 ? rows : 1;
    count_ = 0;
    follow_ = false;
    t_ = 0;
    carry_ = 0;
}

void Emitter::reset() {
    count_ = 0;
    follow_ = false;
    t_ = 0;
}

void Emitter::setFollow(const vec3& X, const vec3& Y, const vec3& Z, const vec3& P) {
    follow_ = true;
    fx_ = X;
    fy_ = Y;
    fz_ = Z;
    fp_ = P;
}

void Emitter::birth(const BirthRule& R) {
    Particle& q = pool_[size_t(count_)];
    q.v.x = vary(R.velocity.x, R.velocityVar.x);
    q.v.y = vary(R.velocity.y, R.velocityVar.y);
    q.v.z = vary(R.velocity.z, R.velocityVar.z);
    q.im = 1.0f / vary(R.mass, R.massVar);
    q.life = vary(R.life, R.lifeVar);
    q.drag = vary(R.drag, R.dragVar);
    q.damp = vary(R.damp, R.dampVar);
    q.f0 = uint8_t(R.texFrameStart);
    q.f1 = uint8_t(R.texFrameEnd);
    if (R.birthFlags & 0x04) {
        q.frame = int8_t(q.f0);
    } else {
        const int raw = effectsRandom15();
        const int span = R.texFrameEnd - R.texFrameStart + 1;
        q.frame = int8_t(uint8_t(int(q.f0) + (span != 0 ? raw % span : 0)));
    }
    q.radius = vary(R.radius, R.radiusVar);
    q.p.x = vary(R.position.x, R.positionVar.x);
    q.p.y = vary(R.position.y, R.positionVar.y);
    q.p.z = vary(R.position.z, R.positionVar.z);
    const uint32_t c = uint32_t(R.color);
    q.r0 = uint8_t(c & 0xff);
    q.g0 = uint8_t((c >> 8) & 0xff);
    q.b0 = uint8_t((c >> 16) & 0xff);
    q.a = uint8_t(c >> 24);
    q.r = q.r0;
    q.g_ = q.g0;
    q.b = q.b0;
    q.g = R.gravity;
    q.dr = vary(R.dRadius, R.dRadiusVar);
    q.da = int8_t(int(float(R.dAlpha) + ((effectsRandomUnit() - 0.5f) * float(R.dAlphaVar))));
    q.rot = 0;
    q.drot = int8_t(int(float(R.dRotation) + ((effectsRandomUnit() - 0.5f) * float(R.dRotationVar))));
    q.h = R.height;
    q.flags = uint8_t(R.birthFlags & 0xff);
}

void Emitter::blast(int n, const BirthRule* r) {
    const BirthRule* R = r ? r : rule;
    if (!R) return;
    if (n > capacity() - count_) n = capacity() - count_;
    for (int i = 0; i < n; ++i) {
        birth(*R);
        if (follow_) {
            Particle& q = pool_[size_t(count_)];
            const vec3 p = q.p;
            q.p = vec3(((p.x * fx_.x + p.y * fy_.x) + p.z * fz_.x) + fp_.x, ((p.x * fx_.y + p.y * fy_.y) + p.z * fz_.y) + fp_.y,
                       ((p.x * fx_.z + p.y * fy_.z) + p.z * fz_.z) + fp_.z);
        }
        ++count_;
    }
}

void Emitter::update(float dt) {
    const float s = dt * 60.0f;
    // 1. Spew.
    t_ = t_ + dt;
    if (rule && (t_ < rule->spewTimeLimit || rule->spewTimeLimit == 0.0f)) {
        const float a = (dt * rule->spewRate) + carry_;
        const int n = int(a);
        carry_ = a - float(n);
        if (a >= 1.0f) blast(n);
    }
    // 2. Each particle.
    int i = 0;
    while (i < count_) {
        Particle& q = pool_[size_t(i)];
        q.life = q.life - dt;
        if (q.life <= 0.0f || q.p.y < -50.0f || q.a == 0) {
            --count_;
            q = pool_[size_t(count_)];
            continue;
        }
        const vec3 u(q.v.x + wind_.x, q.v.y + wind_.y, q.v.z + wind_.z);
        const float k = -(std::sqrt((u.y * u.y + u.z * u.z) + u.x * u.x) * q.drag);
        q.v.x = (((u.x * k) * q.im) * dt) + q.v.x;
        q.v.y = ((((u.y * k) * q.im) + q.g) * dt) + q.v.y;
        q.v.z = (((u.z * k) * q.im) * dt) + q.v.z;
        q.p.x = (dt * q.v.x) + q.p.x;
        q.p.y = (dt * q.v.y) + q.p.y;
        q.p.z = (dt * q.v.z) + q.p.z;
        q.r = uint8_t(int(float(q.r0) * bright));
        q.g_ = uint8_t(int(float(q.g0) * bright));
        q.b = uint8_t(int(float(q.b0) * bright));
        if (q.da != 0 && q.a > 0) {
            const int d = int(float(q.da) * s);
            if (int(q.a) + d < 0) q.a = 0;
            else q.a = uint8_t(int(q.a) + d);
        }
        q.rot = uint8_t(int(q.rot) + int(float(q.drot) * s));
        q.radius = (s * q.dr) + q.radius;
        if ((q.flags & 0x08) && q.p.y < q.h) {
            q.p.y = q.h;
            q.v = vec3();
            q.g = 0.0f;
            q.im = 0.0f;
            q.life = 0.0f;
        } else if ((q.flags & 0x02) && q.p.y < q.radius + q.h) {
            q.p.y = q.h;
            if (q.v.y < 0.0f) {
                q.v.x = q.damp * q.v.x;
                q.v.y = -(q.damp * q.v.y);
                q.v.z = q.damp * q.v.z;
            }
        }
        if ((q.flags & 0x04) && q.f0 < q.f1) {
            const int f0 = q.f0, f1 = q.f1;
            q.frame = int8_t(((int(q.frame) - f0 + 1) % (f1 - f0)) + f0);
        }
        ++i;
    }
}

namespace {
struct RotTable {
    float c[32], s[32];
    RotTable() {
        float th = 0.0f;
        for (int k = 0; k < 32; ++k) {
            c[k] = std::cos(th);
            s[k] = std::sin(th);
            th = th + 0.19634955f;
        }
    }
};
const RotTable& rotTable() {
    static const RotTable t;
    return t;
}
}  // namespace

void particleTriangles(const Emitter& e, const vec3& X, const vec3& Y, std::vector<FxVertex>& out) {
    const RotTable& T = rotTable();
    static const float kCorner[4][4] = {{-1, -1, 0, 0}, {1, -1, 1, 0}, {1, 1, 1, 1}, {-1, 1, 0, 1}};
    static const int kFan[6] = {0, 1, 2, 0, 2, 3};
    const int cols = e.cols(), rows = e.rows();
    auto frameUV = [&](const Particle& q, int k, float& u, float& v) {
        const int f = int(uint8_t(q.frame));
        const int col = f % cols, row = (f / cols) % rows;
        u = (float(col) + kCorner[k][2]) / float(cols);
        v = (float(row) + kCorner[k][3]) / float(rows);
    };
    // A frame outside the sheet (particles.md, "Frames outside the sheet"): neither the particle
    // nor its shadow is drawn.
    auto inSheet = [&](const Particle& q) { return int(uint8_t(q.frame)) < cols * rows; };
    // Shadow pass first (flag 0x10, above the plane).
    for (int i = 0; i < e.count(); ++i) {
        const Particle& q = e.particle(i);
        if (!(q.flags & 0x10) || !(q.p.y - q.h > 0.01f) || !inSheet(q)) continue;
        const int rs = q.rot % 32;
        FxVertex c[4];
        for (int k = 0; k < 4; ++k) {
            const float x = kCorner[k][0], y = kCorner[k][1];
            const float cx = T.c[rs] * x - T.s[rs] * y, cy = T.c[rs] * y + T.s[rs] * x;
            c[k].pos = vec3(q.p.x + q.radius * cx, q.h, q.p.z + q.radius * cy);
            frameUV(q, k, c[k].u, c[k].v);
            c[k].color[0] = uint8_t(q.b >> 1);
            c[k].color[1] = uint8_t(q.g_ >> 1);
            c[k].color[2] = uint8_t(q.r >> 1);
            c[k].color[3] = q.a;
        }
        for (int k : kFan) out.push_back(c[k]);
    }
    for (int i = 0; i < e.count(); ++i) {
        const Particle& q = e.particle(i);
        if (!inSheet(q)) continue;
        const int rs = q.rot % 32;
        FxVertex c[4];
        for (int k = 0; k < 4; ++k) {
            const float x = kCorner[k][0], y = kCorner[k][1];
            const float cx = T.c[rs] * x - T.s[rs] * y, cy = T.c[rs] * y + T.s[rs] * x;
            const float a = q.radius * cx, b = q.radius * cy;
            c[k].pos = vec3((X.x * a + Y.x * b) + q.p.x, (X.y * a + Y.y * b) + q.p.y, (X.z * a + Y.z * b) + q.p.z);
            frameUV(q, k, c[k].u, c[k].v);
            c[k].color[0] = q.r;
            c[k].color[1] = q.g_;
            c[k].color[2] = q.b;
            c[k].color[3] = q.a;
        }
        for (int k : kFan) out.push_back(c[k]);
    }
}

}  // namespace fx
}  // namespace cc
