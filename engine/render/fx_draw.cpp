#include "render/fx_draw.h"

#include <memory>

#include "formats/pkg.h"
#include "fx/car_fx.h"
#include "fx/damage_textures.h"
#include "fx/effects_world.h"
#include "model/vehicle.h"
#include "render/gl_util.h"
#include "render/renderer.h"
#include "render/texture_cache.h"

namespace cc {

namespace {

struct DamageGpu {
    std::vector<GpuTexture> tex;  // one per slot
    std::vector<const GpuTexture*> byMaterial;
    ~DamageGpu() {
        for (GpuTexture& t : tex)
            if (!t.frames.empty()) glDeleteTextures(GLsizei(t.frames.size()), t.frames.data());
    }
};

void upload(fx::DamageSlot& s, GpuTexture& g) {
    if (s.work.empty() || s.w <= 0 || s.h <= 0) return;
    const bool fresh = g.frames.empty() || g.width != s.w || g.height != s.h;
    if (!fresh && s.x0 > s.x1) return;  // nothing changed
    TextureCache::noteBindingChanged();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (fresh) {
        if (g.frames.empty()) {
            GLuint id = 0;
            glGenTextures(1, &id);
            g.frames.push_back(id);
        }
        glBindTexture(GL_TEXTURE_2D, g.frames[0]);
        // The top level only (damage-visuals.md, "Set-up"): no mip chain.
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s.w, s.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s.work.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s.clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, s.clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        g.width = s.w;
        g.height = s.h;
        g.hasAlpha = s.hasAlpha;
        g.flags = s.texFlags;
        g.path = s.cleanName + " (damage copy)";
    } else {
        // Only the changed rectangle (WebGL 2 texSubImage2D with a row length).
        glBindTexture(GL_TEXTURE_2D, g.frames[0]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, s.w);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, s.x0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, s.y0);
        glTexSubImage2D(GL_TEXTURE_2D, 0, s.x0, s.y0, s.x1 - s.x0 + 1, s.y1 - s.y0 + 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        s.work.data());
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    }
    s.x0 = 1;
    s.x1 = 0;
}

}  // namespace

const std::vector<const GpuTexture*>* damageSlotTextures(fx::DamageTextures& t) {
    if (!t.active()) return nullptr;
    DamageGpu* g = static_cast<DamageGpu*>(t.gpu.get());
    std::vector<fx::DamageSlot>& slots = t.slots();
    if (!g || g->tex.size() != slots.size()) {
        auto made = std::make_shared<DamageGpu>();
        made->tex.resize(slots.size());
        t.gpu = made;
        g = made.get();
        for (fx::DamageSlot& s : slots) s.x0 = 1, s.x1 = 0;  // the first upload sends everything
        int maxMaterial = -1;
        for (const fx::DamageSlot& s : slots)
            if (s.material > maxMaterial) maxMaterial = s.material;
        g->byMaterial.assign(size_t(maxMaterial + 1), nullptr);
    }
    for (size_t i = 0; i < slots.size(); ++i) {
        t.ensureWork(slots[i]);
        upload(slots[i], g->tex[i]);
        g->byMaterial[size_t(slots[i].material)] = g->tex[i].frames.empty() ? nullptr : &g->tex[i];
    }
    return &g->byMaterial;
}

}  // namespace cc
namespace cc {

// ---------------------------------------------------------------------------------------------
// Skid marks, particles, sparks, chips (engine/effects.md)

namespace {
static_assert(sizeof(fx::FxVertex) == 24, "FxVertex is the renderer's 24-byte effect vertex");

fx::FxVertex fxv(const vec3& p, float u, float v, uint32_t rgba) {
    fx::FxVertex x;
    x.pos = p;
    x.u = u;
    x.v = v;
    x.color[0] = uint8_t(rgba & 0xff);
    x.color[1] = uint8_t((rgba >> 8) & 0xff);
    x.color[2] = uint8_t((rgba >> 16) & 0xff);
    x.color[3] = uint8_t(rgba >> 24);
    return x;
}
}  // namespace

void drawSkidMarks(Renderer& r, TextureCache& textures, const fx::EffectsWorld& world) {
    const GpuTexture* tex = textures.get("tire_track");
    if (!tex) return;
    static const GpuTexture* clamped = nullptr;
    if (clamped != tex) {
        r.clampTextureU(tex);
        clamped = tex;
    }
    std::vector<fx::FxVertex> verts;
    int items = 0;
    for (const fx::CarFx* c : world.cars()) {
        if (!c->hasSkids() || items >= 63) continue;
        ++items;
        for (int w = 0; w < 4; ++w) {
            const fx::SkidRing& ring = c->skids(w);
            // Strips from tail to head: a strip goes on while the next entry's v is not 0.
            int i = ring.tail;
            while (i != ring.head) {
                int j = (i + 1) % 64;
                while (j != ring.head && ring.e[j].v != 0.0f) {
                    const fx::SkidRing::Entry& a = ring.e[(j + 63) % 64];
                    const fx::SkidRing::Entry& b = ring.e[j];
                    if (a.tex && b.tex) {
                        const fx::FxVertex aL = fxv(a.L, 0, a.v, 0xffffffffu), aR = fxv(a.R, 1, a.v, 0xffffffffu);
                        const fx::FxVertex bL = fxv(b.L, 0, b.v, 0xffffffffu), bR = fxv(b.R, 1, b.v, 0xffffffffu);
                        verts.push_back(aL);
                        verts.push_back(aR);
                        verts.push_back(bL);
                        verts.push_back(aR);
                        verts.push_back(bR);
                        verts.push_back(bL);
                    }
                    j = (j + 1) % 64;
                }
                i = j;
            }
        }
    }
    r.drawEffectTriangles(verts.data(), int(verts.size()), tex, false);
}

void drawEffectsPass(Renderer& r, TextureCache& textures, const fx::EffectsWorld& world, const vec3& camX,
                     const vec3& camY) {
    int items = 0;
    std::vector<fx::FxVertex> verts;
    auto emitter = [&](const fx::Emitter& e, const char* texture) {
        if (e.count() == 0 || items >= 63) return;
        ++items;
        verts.clear();
        fx::particleTriangles(e, camX, camY, verts);
        r.drawEffectTriangles(verts.data(), int(verts.size()), textures.get(texture), true);
    };
    for (const fx::CarFx* c : world.cars()) {
        if (c->hasWheelParticles()) emitter(c->wheelEmitter(), "ptx_wheel");
        if (c->hasSmoke()) emitter(c->smokeEmitter(), "fxpt8");
        // Sparks: one untextured line per spark from head to tail, in its colour.
        if (c->sparkCount() > 0 && items < 63) {
            ++items;
            std::vector<SimpleVertex> lines;
            for (int i = 0; i < c->sparkCount(); ++i) {
                const fx::Spark& s = c->sparks()[i];
                SimpleVertex a, b;
                a.pos = s.p;
                b.pos = s.t;
                for (int k = 0; k < 4; ++k) a.color[k] = b.color[k] = uint8_t((s.color >> (8 * k)) & 0xff);
                lines.push_back(a);
                lines.push_back(b);
            }
            r.drawLines(lines, mat4::identity(), true, false);
        }
        // Chips: a small triangle each, textured with a window of one of the car's materials.
        if (items < 63) {
            ++items;
            const VehicleModel* m = c->model();
            for (int i = 0; i < 16; ++i) {
                const fx::Chip& ch = c->chips()[i];
                if (!(ch.age < 1.8f)) continue;
                auto place = [&](float x, float y, float z) {
                    return vec3(((x * ch.X.x + y * ch.Y.x) + z * ch.Z.x) + ch.P.x, ((x * ch.X.y + y * ch.Y.y) + z * ch.Z.y) + ch.P.y,
                                ((x * ch.X.z + y * ch.Y.z) + z * ch.Z.z) + ch.P.z);
                };
                const fx::FxVertex tri[3] = {fxv(place(0, 0, 0.1f), ch.u0, ch.v0 + 0.3f, 0xffffffffu),
                                             fxv(place(0, 0, 0), ch.u0, ch.v0, 0xffffffffu),
                                             fxv(place(0.1f, 0, 0), ch.u0 + 0.3f, ch.v0, 0xffffffffu)};
                const GpuTexture* tex = nullptr;
                if (m && m->pkg) {
                    const Material* mat = m->pkg->material(c->paintSet(), c->chipMaterial(i));
                    if (mat && !mat->texture.empty()) tex = textures.get(mat->texture, m->baseName);
                }
                r.drawEffectTriangles(tri, 3, tex, true);
            }
        }
    }
    for (const auto& p : world.props())
        if (p->shown) emitter(p->emitter, p->texture.c_str());
    // Rain, with the same state.
    if (world.rainShown()) {
        verts.clear();
        fx::particleTriangles(world.rain(), camX, camY, verts);
        r.drawEffectTriangles(verts.data(), int(verts.size()), textures.get("ptx_rain"), true);
    }
}

}  // namespace cc
