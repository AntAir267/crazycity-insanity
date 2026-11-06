#include "fx/car_fx.h"

#include <cmath>

#include "core/bytes.h"
#include "core/game_random.h"
#include "formats/dat_text.h"
#include "formats/mtx.h"
#include "fx/effects_world.h"
#include "fx/image_source.h"
#include "model/vehicle.h"
#include "physics/pmath.h"
#include "vfs/vfs.h"

namespace cc {
namespace fx {

DamageVisualTune loadDamageVisualTune(const Vfs& vfs, const std::string& baseName) {
    DamageVisualTune t;
    BytesPtr b = vfs.open("tune/vehicle/" + baseName + ".vehCarDamage");
    if (!b) return t;
    using dat::FieldType;
    dat::Schema s{{{"SmokeOffset", FieldType::Vector, &t.smokeOffset, nullptr},
                   {"TextelDamageRadius", FieldType::Float, &t.textelDamageRadius, nullptr},
                   {"SmokeOffset2", FieldType::Vector, &t.smokeOffset2, nullptr},
                   {"DoublePivot", FieldType::Int, &t.doublePivot, nullptr}}};
    t.found = dat::parseText(std::string(b->begin(), b->end()), s);
    return t;
}

// ---------------------------------------------------------------------------------------------
// Skid marks (effects.md, "Skid marks")

void SkidRing::push(const Entry& x) {
    const int nx = (head + 1) % 64;
    if (nx == tail) {
        tail = (tail + 1) % 64;
        if (e[(tail + 1) % 64].v == 0.0f) tail = (tail + 1) % 64;  // a strip cut down to one entry is dropped
    }
    e[head] = x;
    head = nx;
}

void SkidRing::mark(const vec3& p, const vec3& X, bool tex) {
    if (!tex) {
        pending = false;
        cur = false;
        return;
    }
    const vec3 L((X.x * -hw) + p.x, (X.y * -hw) + p.y, (X.z * -hw) + p.z);
    const vec3 R((X.x * hw) + p.x, (X.y * hw) + p.y, (X.z * hw) + p.z);
    if (!cur && !pending) {
        pending = true;
        pendL = L;
        pendR = R;
        A = p;
        return;
    }
    if (pending) {
        const vec3 d(p.x - A.x, p.y - A.y, p.z - A.z);
        const float l2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
        if (l2 < 0.010000001f) return;  // moved less than 0.1 m
        push({pendL, pendR, tex, 0.0f});
        pending = false;
        const float m = 1.0f / std::sqrt(l2);
        D = vec3(d.x * m, d.y * m, d.z * m);
        cur = tex;
        push({L, R, tex, std::sqrt(l2) * iw});
        return;
    }
    vec3 d(p.x - A.x, p.y - A.y, p.z - A.z);
    const float len = std::sqrt((d.z * d.z + d.y * d.y) + d.x * d.x);
    const float inv = 1.0f / len;
    d = vec3(d.x * inv, d.y * inv, d.z * inv);
    const float v = len * iw;
    if (tex == cur && (d.x * D.x + d.y * D.y) + d.z * D.z >= 0.99f && len <= 10.0f) {
        Entry& n = e[(head + 63) % 64];  // a straight run stretches its last vertex pair
        n.L = L;
        n.R = R;
        n.v = v;
    } else {
        push({L, R, tex, v});
        cur = tex;
        D = d;
        A = p;
    }
}

// ---------------------------------------------------------------------------------------------
// Set-up and bookkeeping

CarFx::~CarFx() {
    if (world_) world_->removeCar(this);
}

void CarFx::setupDamageOnly(ImageSource& images, const VehicleModel& model, int paintSet, const std::string& tuneName) {
    tune_ = loadDamageVisualTune(images.vfs(), tuneName);
    hasTextures_ = textures_.setup(model, paintSet, images);
    model_ = &model;
    paintSet_ = paintSet;
    known_ = false;
    markPending_ = false;
    strength_ = 0.0f;
    smokeLevel_ = 0;
    pivotToggle_ = false;
    for (float& t : tilt_) t = 0.0f;
}

void CarFx::setup(EffectsWorld* world, ImageSource& images, const VehicleModel& model, int paintSet,
                  const std::string& tuneName, const phys::Car& car) {
    setupDamageOnly(images, model, paintSet, tuneName);
    if (world_ && world_ != world) world_->removeCar(this);
    world_ = world;
    if (!world) return;
    const Vfs& vfs = world->vfs();
    // Wheel particles: pool 128, ptx_wheel in 8 x 8 frames.
    wheelOn_ = world->hasTexture("ptx_wheel");
    wheelEm_.setup(128, 8, 8);
    carry_[0] = carry_[1] = 0.0f;
    // Engine smoke: the one shared rule gets the set-up values, then this car's .vehCarDamage keys
    // (so the car set up last decides every car's smoke).
    {
        BirthRule& r = world->smokeRule();
        r.velocity = vec3(0, 1, 0);
        r.velocityVar = vec3(1, 2, 1);
        r.life = 0.8f;
        r.lifeVar = 0.4f;
        r.mass = 0.2f;
        r.drag = 1.0f;
        r.radius = 0.3f;
        r.radiusVar = 0.1f;
        r.dAlpha = -15;
        r.dRadius = 0.03f;
        r.gravity = 3.0f;
        if (BytesPtr b = vfs.open("tune/vehicle/" + tuneName + ".vehCarDamage")) {
            dat::Schema s;
            addBirthRuleFields(r, s);
            dat::parseText(std::string(b->begin(), b->end()), s);
        }
    }
    smokeOn_ = world->hasTexture("fxpt8");
    smokeEm_.setup(64, 2, 2);
    acc_ = 0.0f;
    // Exhaust points (shipped: only vpcentury).
    exhaustCount_ = 0;
    for (int i = 0; i < 2; ++i) {
        BytesPtr b = vfs.open("geometry/" + model.baseName + "_exhaust" + std::to_string(i) + ".mtx");
        Pivot pv;
        std::string err;
        if (b && parseMtx(b->data(), b->size(), pv, err)) exhaust_[exhaustCount_++] = pv.origin;
    }
    sparkCount_ = 0;
    sparkAcc_ = 0.0f;
    for (Chip& c : chips_) c = Chip();
    chipNext_ = 0;
    materialCount_ = model.pkg ? model.pkg->materialsPerSet : 0;
    skidOn_ = world->hasTexture("tire_track");
    for (int w = 0; w < 4; ++w) {
        skids_[w].clear();
        const float width = car.wheel(w).width;
        skids_[w].hw = width * 0.5f;
        skids_[w].iw = 1.0f / width;
    }
    world->addCar(this);
}

int CarFx::smokeFrame(int level) {
    // fxpt8 frames by level (damage-visuals.md): white, light grey, grey, black.
    static const int kFrame[5] = {0, 1, 0, 3, 2};
    return level >= 0 && level <= 4 ? kFrame[level] : 0;
}

int CarFx::chipMaterial(int i) const {
    // j goes up by one per ring slot and returns to 0 at 16 div S (never when that is 0).
    if (materialCount_ <= 0) return 0;
    const int q = 16 / materialCount_;
    return q == 0 ? i : i % q;
}

void CarFx::refresh(const phys::Car& car) {
    const unsigned c = car.damage().clears, r = car.resetCount();
    if (known_ && r != resets_) carReset();
    if (known_ && c != clears_) clearDamage();
    clears_ = c;
    resets_ = r;
    known_ = true;
    if (hasTextures_) textures_.sync();
}

void CarFx::carReset() {
    // A car reset empties the skid rings and the wheel particle counters (effects.md).
    for (SkidRing& s : skids_) s.clear();
    carry_[0] = carry_[1] = 0.0f;
    acc_ = 0.0f;
}

void CarFx::clearDamage() {
    // Every "clear damage" refills the working copies (damage-visuals.md, "Repair"); the smoke
    // emitter empties and the shared rule's frames go back to 0 (effects.md).
    textures_.repair();
    // A pending mark request survives (damage-visuals.md, "When marks are made"): only the car's
    // set-up starts without one.
    strength_ = 0.0f;
    smokeLevel_ = 0;
    for (float& t : tilt_) t = 0.0f;
    if (world_) {
        smokeEm_.reset();
        acc_ = 0.0f;
        world_->smokeRule().texFrameStart = 0;
        world_->smokeRule().texFrameEnd = 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Each physics sample

void CarFx::sampleEffects(const phys::Car& car, float dt) {
    refresh(car);
    if (world_) {
        skidStep(car);
        wheelParticles(car, dt);
    }
    damageStep(car, dt);
}

void CarFx::skidStep(const phys::Car& car) {
    if (!skidOn_) return;
    for (int w = 0; w < 4; ++w) {
        const phys::Wheel& wh = car.wheel(w);
        // tire_track while majorly slipping, not on water (drawbridge rooms: later milestone).
        const bool tex = wh.slipping && !wh.material.water;
        skids_[w].mark(wh.x, wh.axle, tex);
    }
}

void CarFx::wheelParticles(const phys::Car& car, float dt) {
    if (!wheelOn_) return;
    for (int w = 0; w < 4; ++w) {
        const phys::Wheel& wh = car.wheel(w);
        for (int slot = 0; slot < 2; ++slot) {
            BirthRule* R = world_->wheelRule(wh.material.ptxIndex[slot]);
            if (!R) continue;
            if (!(wh.slide > wh.material.ptxThreshold[slot])) continue;
            float f;
            if (wh.Fsusp < wh.L0) {
                f = ((wh.Fsusp / wh.L0) + 1.0f) * 0.25f;
            } else {
                f = (((wh.Fsusp - wh.L0) / (wh.k * wh.tune.suspensionLimit)) + 1.0f) * 0.5f;
                if (f > 1.0f) f = 1.0f;
            }
            const float rho = ((f * wh.width) * R->radius) * 0.5f;
            const float sigma = -wh.omega > 0.0f ? 1.0f : (-wh.omega < 0.0f ? -1.0f : 0.0f);
            const float c = sigma * wh.radius;
            vec3 P((c * wh.Back.x) + wh.x.x, (c * wh.Back.y) + wh.x.y, (c * wh.Back.z) + wh.x.z);
            P.y = P.y + rho;
            const float a = wh.vLat * R->velocity.x;
            const float b = (std::fabs(wh.omega) * wh.radius) * R->velocity.y;
            const float e = (-wh.sV) * R->velocity.z;
            const vec3 V((e * wh.Back.x + b * wh.N.x) + a * wh.Lat.x, (e * wh.Back.y + b * wh.N.y) + a * wh.Lat.y,
                         (e * wh.Back.z + b * wh.N.z) + a * wh.Lat.z);
            const vec3 keepV = R->velocity;
            const float keepR = R->radius;
            R->position = P;
            R->velocity = V;
            R->radius = rho;
            const float q = ((float(R->initialBlast) * dt) * f) + carry_[slot];
            const int n = q < 1.0e6f ? int(q) : 1000000;
            carry_[slot] = q - float(n);
            if (n >= 1) wheelEm_.blast(n, R);
            R->velocity = keepV;
            R->radius = keepR;
        }
    }
    wheelEm_.update(dt);
}

void CarFx::puff(const phys::Car& car, const vec3& q, float w) {
    acc_ = w + acc_;
    const int n = acc_ < 1.0e6f ? int(acc_) : 1000000;
    if (n == 0) return;
    BirthRule& R = world_->smokeRule();
    const phys::M3& B = car.rotation();
    const vec3& X = B.r[0];
    const vec3& Y = B.r[1];
    const vec3& Z = B.r[2];
    const vec3 v = R.velocity;
    const vec3& p = car.body().p;  // the centre of mass, not the model origin
    R.velocity = vec3((v.z * Z.x + v.y * Y.x) + v.x * X.x, (v.z * Z.y + v.y * Y.y) + v.x * X.y,
                      (v.z * Z.z + v.y * Y.z) + v.x * X.z);
    R.position = vec3(((q.x * X.x + q.y * Y.x) + q.z * Z.x) + p.x, ((q.x * X.y + q.y * Y.y) + q.z * Z.y) + p.y,
                      ((q.x * X.z + q.y * Y.z) + q.z * Z.z) + p.z);
    acc_ = acc_ - float(n);
    smokeEm_.blast(n, &R);
    R.velocity = v;
}

void CarFx::damageStep(const phys::Car& car, float dt) {
    // What the damage amount drives (damage-visuals.md), each damage update.
    const phys::DamageTune& t = car.tune().damage;
    float s = (car.damage().damage - t.medDamage) / (t.maxDamage - t.medDamage);
    if (!(s > 0.0f)) s = 0.0f;  // NaN too
    if (s > 1.0f) s = 1.0f;
    strength_ = s;
    smokeLevel_ = int(std::ceil(4.0f * s));
    // The smoking step (frames, the point's draw or alternation, the puff) only when the car has
    // its smoke texture fxpt8 (effects.md, "Engine smoke and exhaust").
    if (smokeLevel_ > 0 && smokeOn_) {
        if (world_) {
            world_->smokeRule().texFrameStart = smokeFrame(smokeLevel_);
            world_->smokeRule().texFrameEnd = smokeFrame(smokeLevel_);
        }
        // The emission point (car space, from the body position).
        const vec3& a = tune_.smokeOffset;
        const vec3& b = tune_.smokeOffset2;
        if (b.x == 0.0f && b.y == 0.0f && b.z == 0.0f) {
            smokePoint_ = a;
        } else if (tune_.doublePivot == 0) {
            const float f = effectsRandomUnit();
            smokePoint_ = vec3((b.x - a.x) * f + a.x, (b.y - a.y) * f + a.y, (b.z - a.z) * f + a.z);
        } else {
            smokePoint_ = pivotToggle_ ? b : a;
            pivotToggle_ = !pivotToggle_;
        }
        if (world_) puff(car, smokePoint_, 1.0f);  // one particle per sample
    }
    if (world_ && smokeOn_) {
        smokeEm_.update(dt);
        // Exhaust puffs (vpcentury): rate from the engine speed, the frame the shared rule holds.
        if (exhaustCount_ > 0) {
            float w = (car.rpm() - 2000.0f) / (car.maxRpm() - 2000.0f);
            if (!(w > 0.0f)) w = 0.0f;
            if (w > 1.0f) w = 1.0f;
            for (int i = 0; i < exhaustCount_; ++i) puff(car, exhaust_[i], w);
        }
    }
    if (world_) {
        sparkStep(dt);
        chipUpdate(dt);
    }
    // Wobbling wheels: tilt from the strength, fading as the front-left wheel spins faster.
    float g = ((std::fabs(car.wheel(0).omega) * dt) * 2.0f) * 0.31830987f;
    if (!(g > 0.0f)) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    const float k = (1.0f - g) * s;
    tilt_[0] = -0.15f * k;
    tilt_[1] = 0.35f * k;
    tilt_[2] = 0.35f * k;
    tilt_[3] = -0.15f * k;
    // (The wreck throw of the running gear goes here: parts thrown off are a later milestone.)
    // The pending mark request: one round, then cleared (also without texture damage).
    if (markPending_) {
        markPending_ = false;
        ++rounds_;
        lastMarked_ = hasTextures_ ? textures_.markRound(markPoint_, tune_.textelDamageRadius) : 0;
    }
}

// ---------------------------------------------------------------------------------------------
// Counted impacts: mark request, sparks, chips

void CarFx::countedImpact(const phys::Car& car, const phys::DamageState::Slot& slot, float s, float dt) {
    (void)s;
    // A mark request with the slot's car-space contact point, unless one is pending.
    if (!markPending_) {
        markPending_ = true;
        markPoint_ = slot.carContact;
    }
    if (!world_) return;
    // (A strong impact's body part throw goes here: parts thrown off are a later milestone.)
    if (car.speedMph() > 15.0f) {
        const float q = (16.0f * slot.total) * dt;
        const int n = q < 1000.0f ? int(q) : 1000;
        if (n > 0) sparkBurst(n, slot.contact, slot.normal);
    }
    if (slot.total > 500.0f && car.speed() > 5.0f) {
        int n = int(slot.total / 300.0f > 2.0f ? 2.0f : slot.total / 300.0f);
        if (n >= 1) chipBurst(car, n, slot.contact);
    }
}

void CarFx::sparkBurst(int n, const vec3& P, const vec3& N) {
    const SparkTable& tab = world_->sparkTable();
    const vec3 H = std::fabs(N.y) >= 0.95f ? vec3(1, 0, 0) : vec3(0, 1, 0);
    const vec3 T1 = phys::pcross(N, H);
    const vec3 T2 = phys::pcross(T1, N);
    for (int i = 0; i < n && sparkCount_ < 64; ++i) {
        Spark& sp = sparks_[sparkCount_];
        const float r1 = effectsRandomUnit();
        const float r2 = effectsRandomUnit();
        const float r3 = effectsRandomUnit();
        vec3 w((r1 - 0.5f) * 0.1f, r3 * 0.1f, (r2 - 0.5f) * 0.1f);
        sp.p = sp.t = vec3(w.x + P.x, w.y + P.y, w.z + P.z);
        const float l2 = (w.z * w.z + w.y * w.y) + w.x * w.x;
        const float m = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
        w = vec3(w.x * m, w.y * m, w.z * m);
        const float r4 = effectsRandomUnit();
        const float s1 = ((7.0f - 6.0f) * r4) + 6.0f;
        w.x = s1 * w.x;
        const float r5 = effectsRandomUnit();
        const float s2 = ((5.0f - 4.0f) * r5) + 4.0f;
        w.y = s2 * w.y;
        w.z = s1 * w.z;
        sp.v = vec3((N.x * w.y + T2.x * w.z) + T1.x * w.x, (N.y * w.y + T2.y * w.z) + T1.y * w.x,
                    (N.z * w.y + T2.z * w.z) + T1.z * w.x);
        sp.c = effectsRandom15() & ((tab.h - 1) * tab.w);
        sp.life = int(uint8_t((effectsRandom15() & 63) - 64));
        sp.color = tab.at((sp.life >> tab.shift) + sp.c);
        ++sparkCount_;
    }
}

void CarFx::sparkStep(float dt) {
    sparkAcc_ = sparkAcc_ + dt;
    if (!(sparkAcc_ >= 0.033333335f)) return;
    const float T = sparkAcc_;
    sparkAcc_ = 0.0f;
    const SparkTable& tab = world_->sparkTable();
    const float gT = T * -20.0f, k = T * -0.036f;
    const int dl = int(T * -650.0f);
    int i = 0;
    while (i < sparkCount_) {
        Spark& sp = sparks_[i];
        const int l = sp.life + dl;
        if (l < 0) {
            --sparkCount_;
            sparks_[i] = sparks_[sparkCount_];
            continue;
        }
        sp.life = l;
        sp.color = tab.at((l >> tab.shift) + sp.c);
        sp.t = vec3((k * sp.v.x) + sp.p.x, (k * sp.v.y) + sp.p.y, (k * sp.v.z) + sp.p.z);
        sp.v.y = gT + sp.v.y;
        sp.p = vec3((T * sp.v.x) + sp.p.x, (T * sp.v.y) + sp.p.y, (T * sp.v.z) + sp.p.z);
        if (sp.p.y < 0.0f && sp.v.y < 0.0f) sp.v.y = sp.v.y * -0.8f;  // the floor is world height 0
        ++i;
    }
}

void CarFx::chipBurst(const phys::Car& car, int n, const vec3& P) {
    const float v = car.speed();
    const phys::M3& B = car.rotation();
    const vec3& X = B.r[0];
    const vec3& Y = B.r[1];
    const vec3& Z = B.r[2];
    for (int i = 0; i < n; ++i) {
        const float r1 = effectsRandomUnit();
        const float a = (((r1 + r1) - 1.0f) * 0.3f) * v;
        const float r2 = effectsRandomUnit();
        const float b = (((r2 + 1.0f) * 0.5f) * 0.3f) * v;
        const float r3 = effectsRandomUnit();
        const float e = ((r3 * 0.9f + 0.1f) * 0.2f) * v;
        const float r4 = effectsRandomUnit(), r5 = effectsRandomUnit(), r6 = effectsRandomUnit();
        const vec3 w(r4 - 0.5f, r5, r6 - 0.5f);
        const float l2 = (w.x * w.x + w.y * w.y) + w.z * w.z;
        const float m = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
        const vec3 axis(m * w.z, m * w.y, m * w.x);
        const float r7 = effectsRandomUnit();
        const float spin = ((10.8f * 5.0f - 10.8f) * r7) + 10.8f;
        chipNext_ = (chipNext_ + 1) % 16;
        Chip& c = chips_[chipNext_];
        c.V = vec3((a * X.x + b * Y.x) + e * Z.x, (a * X.y + b * Y.y) + e * Z.y, (a * X.z + b * Y.z) + e * Z.z);
        c.axis = axis;
        c.spin = spin;
        c.age = 0.0f;
        c.P = P;  // the orientation is kept from the slot's last chip
        c.u0 = effectsRandomUnit();
        c.v0 = effectsRandomUnit();
    }
}

namespace {
// Rot(axis, a) of vehicle-body.md: rows of the rotation; each row r becomes r · Rot.
void turnRows(vec3* rows, const vec3& ax, float angle) {
    const double cd = std::cos(double(angle));
    const float s = float(std::sin(double(angle)));
    const float t = float(1.0 - cd);
    const float x = ax.x, y = ax.y, z = ax.z;
    const float m[3][3] = {{float(double((x * x) * t) + cd), s * z + (y * x) * t, (z * x) * t - s * y},
                           {(y * x) * t - s * z, float(double((y * y) * t) + cd), s * x + (z * y) * t},
                           {s * y + (z * x) * t, (z * y) * t - s * x, float(double((z * z) * t) + cd)}};
    for (int i = 0; i < 3; ++i) {
        const vec3 r = rows[i];
        rows[i] = vec3((r.x * m[0][0] + r.y * m[1][0]) + r.z * m[2][0], (r.x * m[0][1] + r.y * m[1][1]) + r.z * m[2][1],
                       (r.x * m[0][2] + r.y * m[1][2]) + r.z * m[2][2]);
    }
}
}  // namespace

void CarFx::chipUpdate(float dt) {
    for (Chip& c : chips_) {
        if (!(c.age < 1.8f)) continue;
        c.V.y = c.V.y - dt * 20.0f;
        vec3 rows[3] = {c.X, c.Y, c.Z};
        turnRows(rows, c.axis, dt * c.spin);
        c.X = rows[0];
        c.Y = rows[1];
        c.Z = rows[2];
        c.P = vec3((dt * c.V.x) + c.P.x, (dt * c.V.y) + c.P.y, (dt * c.V.z) + c.P.z);
        c.age = c.age + dt;
    }
}

}  // namespace fx
}  // namespace cc
