// Draws meshes with MM2's material rules (spec/formats/pkg.md, tex.md): per-vertex lighting
// (emissive + diffuse x ambient + diffuse x lights) times the texture, unlit vertex-colour
// surfaces, alpha test + blending, linear fog. Opaque batches are drawn immediately; blended
// batches are queued and drawn back to front by flush().
#pragma once

#include <string>
#include <vector>

#include "core/math.h"
#include "render/gl_util.h"

namespace cc {

struct GpuMesh;
struct GpuTexture;
struct Pkg;
class TextureCache;

struct DirLight {
    vec3 dir;    // direction the light travels
    vec3 color;  // 0..1
};

struct Lighting {
    vec3 ambient{0.35f, 0.35f, 0.35f};
    DirLight lights[3];
    int count = 0;
};

struct Fog {
    bool on = false;
    vec3 color;
    float start = 0, end = 1;
};

// One mesh drawn with a package's materials.
struct ModelDraw {
    const GpuMesh* gpu = nullptr;
    const Pkg* pkg = nullptr;
    int materialSet = 0;
    mat4 model = mat4::identity();
    unsigned flags = 0;      // DrawFlags (model/vehicle.h)
    float alphaRef = 0;      // blended batches keep texels whose alpha (0..255) is above this
    bool reflections = false;  // draw the reflection pass on DrawReflect parts
    bool inlineBlend = false;  // blended batches drawn in place, in batch order (MM2's way)
    bool canBlend = true;      // false: never blended or alpha-tested (city objects, pkg.md)
    bool groundOverlay = false;  // placed city model: ground overlay pass (night.md, section 3)
    // Per material index: a texture drawn in place of the material's own (a car's texture damage
    // working copies, damage-visuals.md); null entries and indices past the end draw normally.
    const std::vector<const GpuTexture*>* slotTextures = nullptr;
    const char* user = "";   // for "texture not found" messages
};

// A mesh whose batches pick textures from a slot table (city surfaces): unlit, vertex colour x
// texture.
struct SlotDraw {
    const GpuMesh* gpu = nullptr;
    const std::vector<const GpuTexture*>* slots = nullptr;  // indexed by batch material
    mat4 model = mat4::identity();
    float alphaRef = 100;
};

struct SimpleVertex {
    vec3 pos;
    uint8_t color[4];
};

// A camera-facing square for the glow pass (night.md): centre, half-size, colour (RGBA).
struct Billboard {
    vec3 centre;
    float half = 1.0f;
    uint8_t color[4] = {255, 255, 255, 255};
};

// A decal vertex (pathset.md, "Decals") and one decal to draw.
struct DecalVertex {
    vec3 pos;
    float u = 0, v = 0;
};
struct DecalDraw {
    const GpuTexture* tex = nullptr;
    const std::vector<DecalVertex>* verts = nullptr;  // triangles
};

// A screen-space square for lens flares: corners in normalised device coordinates.
struct ScreenQuad {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    uint8_t color[4] = {255, 255, 255, 255};
};

struct RenderStats {
    int drawCalls = 0;
    int triangles = 0;
};

class Renderer {
public:
    bool init();
    void shutdown();

    void beginFrame(int width, int height, const vec3& clearColor);
    // Where the 3D view's (0, 0) is in the window (device px from the bottom-left; the UI may
    // take a side of the window). beginFrame and setSubViewport draw relative to it.
    void setOrigin(int x, int y) {
        originX_ = x;
        originY_ = y;
    }
    // Restricts drawing to a part of the screen (letterbox), cleared to `clearColor`.
    void setSubViewport(int x, int y, int w, int h, const vec3& clearColor);
    void setCamera(const mat4& view, const mat4& proj, const vec3& eye);
    // Logarithmic depth (gl_FragDepth = log2(w + 1) / log2(far + 1)) for long draw distances;
    // beginFrame() returns to the normal depth buffer.
    void setDepthMode(bool logDepth, float farPlane);
    void setLighting(const Lighting& l) { lighting_ = l; dirty_ = true; }
    void setFog(const Fog& f) { fog_ = f; dirty_ = true; }
    void setTime(double seconds) { time_ = seconds; }
    void setEnvironment(const GpuTexture* env) { env_ = env; dirty_ = true; }
    // Diffuse multiplier for untextured materials (0.5 in night cities, pkg.md).
    void setUntexturedFactor(float f) { untexturedFactor_ = f; }

    void drawModel(const ModelDraw& d, TextureCache& textures);
    // pass 0: batches with opaque textures; pass 1: alpha textures (blended, tested at alphaRef).
    void drawSlots(const SlotDraw& d, int pass);
    // Sky: no depth test or writes, unlit, no fog.
    void drawSky(const ModelDraw& d, TextureCache& textures);
    // The ground overlay map for this frame (night.md, section 3); null = no overlay.
    void setOverlayMap(const GpuTexture* map) { overlay_ = map; }
    // Glow pass (night.md, section 4): additive, depth test without writes, unlit, no fog.
    void drawGlowModel(const ModelDraw& d, TextureCache& textures);
    void drawBillboards(const std::vector<Billboard>& quads, const GpuTexture* tex);
    // Decals: white, unlit, fogged, depth test without writes, alpha blended (alpha test 0),
    // depth range 0.001 … 0.999, back faces culled.
    void drawDecals(const std::vector<DecalDraw>& list);
    // Additive textured squares in screen space (no depth test, no fog): lens flares.
    void drawScreenQuads(const std::vector<ScreenQuad>& quads, const GpuTexture* tex);
    // Cockpit part (dash.md, "Drawing"): depth test and writes off, unlit (texture × white),
    // alpha blended (discarding alpha 0), no fog.
    void drawCockpitPart(const ModelDraw& d, TextureCache& textures);
    // Mirror pass: the camera frame is mirrored, so the front-face winding is swapped.
    void setMirrored(bool on);
    // Draws the queued blended batches, farthest first.
    void flush();

    // Effects geometry (engine/effects.md, "Drawing order"): world matrix identity, unlit, vertex
    // colour × texture, alpha blended, alpha test (texels with alpha <= alphaRef dropped), no fog,
    // no depth writes, no culling; `depthTest` off for skid marks. `verts` holds `count` vertices
    // of 24 bytes: float x, y, z, u, v, then R, G, B, A bytes (fx::FxVertex).
    void drawEffectTriangles(const void* verts, int count, const GpuTexture* tex, bool depthTest, float alphaRef = 0.0f);
    // Clamps a texture's u (skid marks: clamped across, repeating along).
    void clampTextureU(const GpuTexture* tex);

    // Untextured helpers (background, turntable, grids).
    void drawGradient(const vec3& top, const vec3& bottom);
    void drawTriangles(const std::vector<SimpleVertex>& verts, const mat4& model, bool depthWrite = true,
                       bool depthTest = true);
    void drawLines(const std::vector<SimpleVertex>& verts, const mat4& model, bool depthTest = true,
                   bool depthWrite = true);

    const RenderStats& stats() const { return stats_; }
    const mat4& viewProj() const { return viewProj_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    struct BatchState {
        int mode = 0;  // 0 lit, 1 unlit (white or vertex colour), 2 slots, 3 reflection pass
        bool vertexColor = false;
        vec4 diffuse{1, 1, 1, 1};
        vec3 emissive;
        GLuint tex = 0;
        float alphaRef = 0;
        bool fog = true;
    };

    struct Blended {
        const GpuMesh* gpu;
        int range;
        mat4 model;
        BatchState state;
        float refl;
        bool shadow;
        float depth;
        int order;
    };

    void setBlend(int mode);  // 0 off, 1 alpha, 2 additive
    void drawReflection(const GpuMesh& g, int range, float strength);

    void useMain();
    void drawOverlayRange(const GpuMesh& g, int range, float scale);
    bool overlayWanted(const GpuTexture* tex) const;
    void initUniforms();
    bool initBuffers();
    void selectVariant(int v);
    void setModel(const mat4& m);
    void applyBatch(const BatchState& s);
    void drawRange(const GpuMesh& g, int range);

    GLuint prog_ = 0, simpleProg_ = 0, gradProg_ = 0;
    GLuint simpleVao_ = 0, simpleVbo_ = 0, gradVao_ = 0;
    GLuint whiteTex_ = 0;
    struct {
        GLint model, viewProj, normalMat, mode, diffuse, emissive, ambient, lightDir, lightColor,
            lightCount, vertexColor, tex, hasTex, alphaRef, fogOn, fogColor, fogStart, fogEnd, eye, overlayScale, overlay;
    } u_{};
    GLint simpleViewProj_ = -1, simpleModel_ = -1;
    struct Variant {
        GLuint prog = 0, simpleProg = 0;
        decltype(u_) u{};
        GLint simpleViewProj = -1, simpleModel = -1, logK = -1, depthBias = -1, simpleLogK = -1;
    } variants_[2];
    int variant_ = 0;
    const GpuTexture* overlay_ = nullptr;
    GLuint billboardVao_ = 0, billboardVbo_ = 0;
    GLuint fxVao_ = 0, fxVbo_ = 0;
    float logK_ = 0.1f;
    GLint gradTop_ = -1, gradBottom_ = -1;

    mat4 view_ = mat4::identity(), proj_ = mat4::identity(), viewProj_ = mat4::identity();
    vec3 eye_;
    Lighting lighting_;
    Fog fog_;
    double time_ = 0;
    const GpuTexture* env_ = nullptr;
    float untexturedFactor_ = 1.0f;
    int width_ = 1, height_ = 1;
    int originX_ = 0, originY_ = 0;
    std::vector<Blended> blended_;
    RenderStats stats_;
    int order_ = 0;
    struct BatchCache {
        bool valid = false;
        int mode = -1;
        bool vertexColor = false;
        vec4 diffuse;
        vec3 emissive;
        bool hasTex = false;
        GLuint tex = 0;
        float alphaRef = -2;
        int fogMode = -1;
        unsigned uploads = 0;
    } cache_;
    GLuint current_ = 0;   // program in use
    int blendMode_ = 0;
    bool dirty_ = true;    // frame-wide uniforms need uploading
};

}  // namespace cc
