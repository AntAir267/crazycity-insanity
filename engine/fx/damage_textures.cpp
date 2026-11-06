#include "fx/damage_textures.h"

#include <cmath>
#include <cstring>

#include "core/game_random.h"
#include "core/log.h"
#include "core/strutil.h"
#include "fx/image_source.h"
#include "model/vehicle.h"

namespace cc {
namespace fx {

bool damageSlotNames(const std::string& texture, ImageSource& images, std::string& clean, std::string& damage) {
    if (texture.empty()) return false;
    // A name whose text after the last '_' is "dmg" (any case).
    if (texture.size() > 4 && iendsWith(texture, "_dmg")) {
        damage = texture;
        const std::string stripped = texture.substr(0, texture.size() - 4);
        clean = images.exists(stripped) ? stripped : texture;
        return images.exists(damage);
    }
    if (images.exists(texture + "_dmg") && images.exists(texture)) {
        clean = texture;
        damage = texture + "_dmg";
        return true;
    }
    return false;
}

bool DamageTextures::setup(const VehicleModel& model, int set, ImageSource& images) {
    *this = DamageTextures();
    images_ = &images;
    generation_ = images.generation();
    const PartLods* body = model.part("BODY");
    const Mesh* mesh = body ? body->at(LodH) : nullptr;
    if (!mesh || !model.pkg) return false;
    const Pkg& pkg = *model.pkg;
    // Slots of the paint set: damageable or not, decided once per material index.
    std::vector<int> decided;  // -2 = not looked at yet, -1 = not damageable, else slot index
    auto slotFor = [&](int material) {
        if (material < 0) return -1;
        if (size_t(material) >= decided.size()) decided.resize(size_t(material) + 1, -2);
        int& d = decided[size_t(material)];
        if (d != -2) return d;
        d = -1;
        const Material* mat = pkg.material(set, material);
        std::string clean, damage;
        if (mat && damageSlotNames(mat->texture, images, clean, damage)) {
            DamageSlot s;
            s.material = material;
            s.cleanName = clean;
            s.damageName = damage;
            loadImages(s);
            if (s.w > 0 && s.h > 0) {
                if (s.dw != s.w || s.dh != s.h)
                    logFile(LogLevel::Info, "texture/" + damage,
                            strf("damage image %dx%d differs from %s %dx%d; only the common texels are copied", s.dw,
                                 s.dh, clean.c_str(), s.w, s.h));
                d = int(slots_.size());
                slots_.push_back(std::move(s));
            }
        }
        return d;
    };
    // The batches in order: a working copy for each damageable slot met, then every vertex and
    // triangle of the mesh.
    for (const Batch& b : mesh->batches) {
        const uint32_t base = uint32_t(pos_.size());
        const int slot = slotFor(b.material);
        for (const Vertex& v : b.vertices) {
            pos_.push_back(v.pos);
            uv_.push_back(v.uv);
        }
        for (size_t i = 0; i + 2 < b.indices.size(); i += 3) {
            Tri t;
            t.v[0] = base + b.indices[i];
            t.v[1] = base + b.indices[i + 1];
            t.v[2] = base + b.indices[i + 2];
            t.slot = slot;
            if (t.v[0] >= pos_.size() || t.v[1] >= pos_.size() || t.v[2] >= pos_.size()) continue;
            tris_.push_back(t);
        }
    }
    slotOfMaterial_.assign(decided.size(), -1);
    for (size_t m = 0; m < decided.size(); ++m) slotOfMaterial_[m] = decided[m] >= 0 ? decided[m] : -1;
    if (slots_.empty()) {
        logInfo("%s: no damage textures found", model.baseName.c_str());
        pos_.clear();
        uv_.clear();
        tris_.clear();
    }
    return !slots_.empty();
}

size_t DamageTextures::damageableTriangles() const {
    size_t n = 0;
    for (const Tri& t : tris_)
        if (t.slot >= 0) ++n;
    return n;
}

void DamageTextures::loadImages(DamageSlot& s) {
    s.clean = images_->get(s.cleanName);
    s.damage = images_->get(s.damageName);
    s.w = s.h = s.dw = s.dh = 0;
    if (s.clean && !s.clean->frames.empty() && !s.clean->first().levels.empty()) {
        const Image& img = s.clean->first();
        s.w = img.levels[0].width;
        s.h = img.levels[0].height;
        s.hasAlpha = img.hasAlpha;
        s.clampU = img.clampU;
        s.clampV = img.clampV;
        s.texFlags = img.texFlags;
    }
    if (s.damage && !s.damage->frames.empty() && !s.damage->first().levels.empty()) {
        s.dw = s.damage->first().levels[0].width;
        s.dh = s.damage->first().levels[0].height;
    }
}

void DamageTextures::fillFromMask(DamageSlot& s) {
    const size_t n = size_t(s.w) * size_t(s.h);
    s.work.resize(n * 4);
    if (s.mask.size() != n) s.mask.assign(n, 0);
    const std::vector<uint8_t>* clean = nullptr;
    if (s.clean && !s.clean->frames.empty() && !s.clean->first().levels.empty()) {
        const ImageLevel& L = s.clean->first().levels[0];
        if (L.width == s.w && L.height == s.h && L.rgba.size() == n * 4) clean = &L.rgba;
    }
    if (clean) std::memcpy(s.work.data(), clean->data(), n * 4);
    else std::memset(s.work.data(), 0, n * 4);
    const ImageLevel* dmg = (s.damage && !s.damage->frames.empty() && !s.damage->first().levels.empty())
                                ? &s.damage->first().levels[0]
                                : nullptr;
    if (dmg && marked_)
        for (int y = 0; y < s.h && y < dmg->height; ++y)
            for (int x = 0; x < s.w && x < dmg->width; ++x) {
                const size_t i = size_t(y) * size_t(s.w) + size_t(x);
                if (!s.mask[i]) continue;
                std::memcpy(&s.work[i * 4], &dmg->rgba[(size_t(y) * size_t(dmg->width) + size_t(x)) * 4], 4);
            }
    s.x0 = 0;
    s.y0 = 0;
    s.x1 = s.w - 1;
    s.y1 = s.h - 1;
}

void DamageTextures::ensureWork(DamageSlot& s) {
    if (s.work.empty() && s.w > 0 && s.h > 0) fillFromMask(s);
}

void DamageTextures::sync() {
    if (!images_ || images_->generation() == generation_) return;
    generation_ = images_->generation();
    for (DamageSlot& s : slots_) {
        const int w = s.w, h = s.h;
        loadImages(s);
        if (s.w != w || s.h != h) s.mask.clear();  // a differently sized variant: the marks start over
        if (!s.work.empty()) fillFromMask(s);
    }
    ++version_;
}

namespace {
// Inclusive column ranges per row y - 3 ... y + 3 (damage-visuals.md, "Marking" step 4).
const int kLarge[7][2] = {{-4, 5}, {-8, 7}, {-8, 9}, {-10, 7}, {-8, 11}, {-6, 7}, {-4, 5}};
const int kSmall[7][2] = {{-2, 2}, {-4, 3}, {-4, 4}, {-5, 3}, {-4, 5}, {-3, 3}, {-2, 2}};
}  // namespace

void DamageTextures::stamp(DamageSlot& s, int x, int y, bool large) {
    ensureWork(s);
    if (s.work.empty() || !s.damage || s.damage->frames.empty() || s.damage->first().levels.empty()) return;
    const ImageLevel& dmg = s.damage->first().levels[0];
    const int (*brush)[2] = large ? kLarge : kSmall;
    // Only texels inside both images are copied (mismatched sizes).
    const int W = s.w < dmg.width ? s.w : dmg.width;
    const int H = s.h < dmg.height ? s.h : dmg.height;
    for (int r = 0; r < 7; ++r) {
        const int yy = y + r - 3;
        if (yy < 0 || yy >= s.h || yy >= H) continue;
        int a = x + brush[r][0], b = x + brush[r][1];
        if (a < 0) a = 0;
        if (b > W - 1) b = W - 1;
        if (a > b) continue;
        for (int xx = a; xx <= b; ++xx) {
            const size_t i = size_t(yy) * size_t(s.w) + size_t(xx);
            s.mask[i] = 1;
            std::memcpy(&s.work[i * 4], &dmg.rgba[(size_t(yy) * size_t(dmg.width) + size_t(xx)) * 4], 4);
        }
        if (s.x0 > s.x1) {
            s.x0 = a;
            s.x1 = b;
            s.y0 = s.y1 = yy;
        } else {
            if (a < s.x0) s.x0 = a;
            if (b > s.x1) s.x1 = b;
            if (yy < s.y0) s.y0 = yy;
            if (yy > s.y1) s.y1 = yy;
        }
    }
}

int DamageTextures::markRound(const vec3& p, float r) {
    if (slots_.empty()) return 0;
    const float r2 = r * r;
    int marked = 0;
    for (const Tri& t : tris_) {
        if (t.slot < 0) continue;
        bool in = false;
        for (int k = 0; k < 3 && !in; ++k) {
            const vec3& q = pos_[t.v[k]];
            const float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
            in = (dx * dx + dy * dy) + dz * dz < r2;
        }
        if (!in) continue;
        ++marked;
        // A random point inside the triangle (three draws), then the brush (one raw draw).
        const float a = effectsRandomUnit();
        const float b = effectsRandomUnit();
        const float c = effectsRandomUnit();
        const float n = 1.0f / ((c + b) + a);
        const float wa = n * a, wb = n * b, wc = n * c;
        const vec2& t0 = uv_[t.v[0]];
        const vec2& t1 = uv_[t.v[1]];
        const vec2& t2 = uv_[t.v[2]];
        const float u = (wc * t2.x + wb * t1.x) + wa * t0.x;
        const float v = (wa * t0.y + wb * t1.y) + wc * t2.y;
        const int raw = effectsRandom15();
        DamageSlot& s = slots_[size_t(t.slot)];
        const float fx = float(s.w) * u, fy = float(s.h) * v;
        // trunc; a point far outside the image (or not a number, a + b + c = 0) marks nothing.
        if (!(std::fabs(fx) < 1.0e8f) || !(std::fabs(fy) < 1.0e8f)) continue;
        marked_ = true;
        stamp(s, int(fx), int(fy), (raw & 1) == 0);
    }
    if (marked) ++version_;
    return marked;
}

void DamageTextures::repair() {
    if (!marked_) return;
    marked_ = false;
    for (DamageSlot& s : slots_) {
        if (!s.mask.empty()) std::fill(s.mask.begin(), s.mask.end(), uint8_t(0));
        if (!s.work.empty()) fillFromMask(s);
    }
    ++version_;
}

}  // namespace fx
}  // namespace cc
