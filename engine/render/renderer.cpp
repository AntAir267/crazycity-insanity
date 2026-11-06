#include "render/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

#include "core/log.h"
#include "formats/pkg.h"
#include "formats/texture_lookup.h"
#include "model/vehicle.h"
#include "render/mesh_cache.h"
#include "render/texture_cache.h"

namespace cc {

namespace {

const char* kMainVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aColor;
uniform mat4 uModel;
uniform mat4 uViewProj;
uniform mat3 uNormalMat;
uniform int uMode;
uniform vec4 uDiffuse;
uniform vec3 uEmissive;
uniform vec3 uAmbient;
uniform vec3 uLightDir[3];
uniform vec3 uLightColor[3];
uniform int uLightCount;
uniform bool uVertexColor;
uniform float uOverlayScale;
out vec4 vColor;
out vec2 vUV;
out vec3 vWorld;
#ifdef LOG_DEPTH
out float vClipW;
#endif
void main() {
    vec4 wp = uModel * vec4(aPos, 1.0);
    vWorld = wp.xyz;
    gl_Position = uViewProj * wp;
#ifdef LOG_DEPTH
    vClipW = gl_Position.w;
#endif
    vUV = aUV;
    if (uMode == 0) {
        vec3 n = normalize(uNormalMat * aNormal);
        vec3 c = uEmissive + uDiffuse.rgb * uAmbient;
        for (int i = 0; i < 3; ++i) {
            if (i >= uLightCount) break;
            c += uDiffuse.rgb * uLightColor[i] * max(dot(n, -uLightDir[i]), 0.0);
        }
        vColor = vec4(min(c, vec3(1.0)), uDiffuse.a);
    } else if (uMode == 4) {
        // Ground overlay (night.md, section 3): uv from the position, black, alpha from the map.
        vUV = vec2(aPos.x + aPos.y, aPos.y + aPos.z) * uOverlayScale;
        vColor = vec4(0.0, 0.0, 0.0, 1.0);
    } else if (uMode == 3) {
        // Reflection pass (car-model.md): world-space normal, u = 0.5 + 0.5 n.x, v = 0.5 - 0.5 n.y.
        vec3 n = uNormalMat * aNormal;
        vUV = vec2(0.5 + 0.5 * n.x, 0.5 - 0.5 * n.y);
        vColor = vec4(uDiffuse.rgb, 1.0);
    } else {
        vColor = uVertexColor ? aColor : vec4(1.0);
    }
}
)";

const char* kMainFS = R"(#version 300 es
precision highp float;
in vec4 vColor;
in vec2 vUV;
in vec3 vWorld;
uniform sampler2D uTex;
uniform bool uHasTex;
uniform float uAlphaRef;
uniform int uFogMode;   // 0 off, 1 blend to the fog colour, 2 fade out (additive passes)
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uEye;
uniform bool uOverlay;     // ground overlay pass: alpha = 1 - map alpha
#ifdef LOG_DEPTH
in float vClipW;
uniform float uLogK;       // 1 / log2(far + 1)
uniform float uDepthBias;  // pulls coplanar overlays (shadows) forward
#endif
out vec4 fragColor;
void main() {
    vec4 c = vColor;
    if (uOverlay) c = vec4(0.0, 0.0, 0.0, 1.0 - texture(uTex, vUV).a);
    else if (uHasTex) c *= texture(uTex, vUV);
    if (uAlphaRef >= 0.0 && c.a * 255.0 <= uAlphaRef) discard;
    if (uFogMode != 0) {
        float d = length(vWorld - uEye);
        float f = clamp((uFogEnd - d) / max(uFogEnd - uFogStart, 0.001), 0.0, 1.0);
        c.rgb = uFogMode == 1 ? mix(uFogColor, c.rgb, f) : c.rgb * f;
    }
#ifdef LOG_DEPTH
    // Logarithmic depth: even precision from 0.3 m to the far side of the city.
    gl_FragDepth = log2(max(vClipW, 1e-6) + 1.0) * uLogK - uDepthBias;
#endif
    fragColor = c;
}
)";

const char* kSimpleVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;
uniform mat4 uViewProj;
uniform mat4 uModel;
out vec4 vColor;
#ifdef LOG_DEPTH
out float vClipW;
#endif
void main() {
    vColor = aColor;
    gl_Position = uViewProj * uModel * vec4(aPos, 1.0);
#ifdef LOG_DEPTH
    vClipW = gl_Position.w;
#endif
}
)";

const char* kSimpleFS = R"(#version 300 es
precision highp float;
in vec4 vColor;
#ifdef LOG_DEPTH
in float vClipW;
uniform float uLogK;
#endif
out vec4 fragColor;
void main() {
#ifdef LOG_DEPTH
    gl_FragDepth = log2(max(vClipW, 1e-6) + 1.0) * uLogK;
#endif
    fragColor = vColor;
}
)";

const char* kGradVS = R"(#version 300 es
precision highp float;
out float vT;
void main() {
    vec2 p = vec2(float((gl_VertexID & 1) * 4 - 1), float((gl_VertexID & 2) * 2 - 1));
    vT = p.y * 0.5 + 0.5;
    gl_Position = vec4(p, 0.999, 1.0);
}
)";

const char* kGradFS = R"(#version 300 es
precision highp float;
in float vT;
uniform vec3 uTop;
uniform vec3 uBottom;
out vec4 fragColor;
void main() { fragColor = vec4(mix(uBottom, uTop, clamp(vT, 0.0, 1.0)), 1.0); }
)";

}  // namespace

namespace {
// The same shader with "#define LOG_DEPTH" after its version line.
std::string withLogDepth(const char* src) {
    std::string s(src);
    const size_t nl = s.find('\n');
    return s.substr(0, nl + 1) + "#define LOG_DEPTH\n" + s.substr(nl + 1);
}
}  // namespace

bool Renderer::init() {
    const std::string mainVSLog = withLogDepth(kMainVS), mainFSLog = withLogDepth(kMainFS);
    const std::string simpleVSLog = withLogDepth(kSimpleVS), simpleFSLog = withLogDepth(kSimpleFS);
    gradProg_ = buildProgram("gradient", kGradVS, kGradFS);
    if (!gradProg_) return false;
    for (int v = 0; v < 2; ++v) {
        prog_ = v ? buildProgram("main (log depth)", mainVSLog.c_str(), mainFSLog.c_str()) : buildProgram("main", kMainVS, kMainFS);
        simpleProg_ = v ? buildProgram("simple (log depth)", simpleVSLog.c_str(), simpleFSLog.c_str())
                        : buildProgram("simple", kSimpleVS, kSimpleFS);
        if (!prog_ || !simpleProg_) return false;
        initUniforms();
        variants_[v].prog = prog_;
        variants_[v].simpleProg = simpleProg_;
        variants_[v].u = u_;
        variants_[v].simpleViewProj = simpleViewProj_;
        variants_[v].simpleModel = simpleModel_;
        variants_[v].logK = glGetUniformLocation(prog_, "uLogK");
        variants_[v].depthBias = glGetUniformLocation(prog_, "uDepthBias");
        variants_[v].simpleLogK = glGetUniformLocation(simpleProg_, "uLogK");
    }
    selectVariant(0);
    gradTop_ = glGetUniformLocation(gradProg_, "uTop");
    gradBottom_ = glGetUniformLocation(gradProg_, "uBottom");
    return initBuffers();
}

void Renderer::selectVariant(int v) {
    variant_ = v;
    prog_ = variants_[v].prog;
    simpleProg_ = variants_[v].simpleProg;
    u_ = variants_[v].u;
    simpleViewProj_ = variants_[v].simpleViewProj;
    simpleModel_ = variants_[v].simpleModel;
    current_ = 0;
    dirty_ = true;
    cache_.valid = false;
}

void Renderer::setDepthMode(bool logDepth, float farPlane) {
    const int v = logDepth ? 1 : 0;
    if (v != variant_) selectVariant(v);
    logK_ = 1.0f / std::log2(farPlane + 1.0f);
    dirty_ = true;
}

void Renderer::initUniforms() {
#define U(name) glGetUniformLocation(prog_, name)
    u_.model = U("uModel");
    u_.viewProj = U("uViewProj");
    u_.normalMat = U("uNormalMat");
    u_.mode = U("uMode");
    u_.diffuse = U("uDiffuse");
    u_.emissive = U("uEmissive");
    u_.ambient = U("uAmbient");
    u_.lightDir = U("uLightDir");
    u_.lightColor = U("uLightColor");
    u_.lightCount = U("uLightCount");
    u_.vertexColor = U("uVertexColor");
    u_.tex = U("uTex");
    u_.hasTex = U("uHasTex");
    u_.alphaRef = U("uAlphaRef");
    u_.fogOn = U("uFogMode");
    u_.fogColor = U("uFogColor");
    u_.fogStart = U("uFogStart");
    u_.fogEnd = U("uFogEnd");
    u_.eye = U("uEye");
    u_.overlayScale = U("uOverlayScale");
    u_.overlay = U("uOverlay");
#undef U
    simpleViewProj_ = glGetUniformLocation(simpleProg_, "uViewProj");
    simpleModel_ = glGetUniformLocation(simpleProg_, "uModel");
}

bool Renderer::initBuffers() {
    glGenVertexArrays(1, &simpleVao_);
    glBindVertexArray(simpleVao_);
    glGenBuffers(1, &simpleVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, simpleVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SimpleVertex), reinterpret_cast<void*>(offsetof(SimpleVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(SimpleVertex), reinterpret_cast<void*>(offsetof(SimpleVertex, color)));
    glGenVertexArrays(1, &gradVao_);
    glBindVertexArray(0);

    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    checkGl("renderer init");
    return true;
}

void Renderer::shutdown() {
    for (auto& v : variants_) {
        if (v.prog) glDeleteProgram(v.prog);
        if (v.simpleProg) glDeleteProgram(v.simpleProg);
        v.prog = v.simpleProg = 0;
    }
    if (gradProg_) glDeleteProgram(gradProg_);
    prog_ = simpleProg_ = gradProg_ = 0;
}

void Renderer::beginFrame(int w, int h, const vec3& clear) {
    width_ = w;
    height_ = h;
    stats_ = RenderStats();
    overlay_ = nullptr;  // views that want the ground overlay set it each frame
    if (variant_ != 0) selectVariant(0);  // views that want log depth set it after beginFrame
    blended_.clear();
    order_ = 0;
    dirty_ = true;
    current_ = 0;
    blendMode_ = 0;
    cache_.valid = false;
    glViewport(originX_, originY_, w, h);
    glClearColor(clear.x, clear.y, clear.z, 1.0f);
    glDepthMask(GL_TRUE);
    const bool part = originX_ != 0 || originY_ != 0;
    if (part) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(originX_, originY_, w, h);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (part) glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);  // triangles are counter-clockwise seen from the front
    glDisable(GL_BLEND);
}

void Renderer::setSubViewport(int x, int y, int w, int h, const vec3& c) {
    x += originX_;
    y += originY_;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x, y, w, h);
    glViewport(x, y, w, h);
    glClearColor(c.x, c.y, c.z, 1.0f);
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::setCamera(const mat4& view, const mat4& proj, const vec3& eye) {
    view_ = view;
    proj_ = proj;
    viewProj_ = proj * view;
    eye_ = eye;
    dirty_ = true;
}

void Renderer::useMain() {
    if (current_ != prog_) {
        glUseProgram(prog_);
        current_ = prog_;
    }
    if (!dirty_) return;
    dirty_ = false;
    glUniformMatrix4fv(u_.viewProj, 1, GL_FALSE, viewProj_.m);
    glUniform3f(u_.ambient, lighting_.ambient.x, lighting_.ambient.y, lighting_.ambient.z);
    float dirs[9] = {}, cols[9] = {};
    for (int i = 0; i < lighting_.count && i < 3; ++i) {
        vec3 d = normalize(lighting_.lights[i].dir);
        dirs[i * 3] = d.x; dirs[i * 3 + 1] = d.y; dirs[i * 3 + 2] = d.z;
        cols[i * 3] = lighting_.lights[i].color.x;
        cols[i * 3 + 1] = lighting_.lights[i].color.y;
        cols[i * 3 + 2] = lighting_.lights[i].color.z;
    }
    glUniform3fv(u_.lightDir, 3, dirs);
    glUniform3fv(u_.lightColor, 3, cols);
    glUniform1i(u_.lightCount, lighting_.count < 3 ? lighting_.count : 3);
    glUniform1i(u_.tex, 0);
    glUniform3f(u_.fogColor, fog_.color.x, fog_.color.y, fog_.color.z);
    glUniform1f(u_.fogStart, fog_.start);
    glUniform1f(u_.fogEnd, fog_.end);
    glUniform3f(u_.eye, eye_.x, eye_.y, eye_.z);
    glUniform1i(u_.overlay, 0);
    if (variant_ == 1) {
        glUniform1f(variants_[1].logK, logK_);
        glUniform1f(variants_[1].depthBias, 0.0f);
    }
}

void Renderer::setModel(const mat4& m) {
    glUniformMatrix4fv(u_.model, 1, GL_FALSE, m.m);
    float nm[9];
    normalMatrix(m, nm);
    glUniformMatrix3fv(u_.normalMat, 1, GL_FALSE, nm);
}

void Renderer::applyBatch(const BatchState& s) {
    // WebGL calls are expensive: only send what changed since the last batch.
    BatchCache& c = cache_;
    if (!c.valid || c.mode != s.mode) { glUniform1i(u_.mode, s.mode); c.mode = s.mode; }
    if (!c.valid || c.vertexColor != s.vertexColor) {
        glUniform1i(u_.vertexColor, s.vertexColor ? 1 : 0);
        c.vertexColor = s.vertexColor;
    }
    if (s.mode == 0 || s.mode == 3) {
        if (!c.valid || c.diffuse.x != s.diffuse.x || c.diffuse.y != s.diffuse.y || c.diffuse.z != s.diffuse.z ||
            c.diffuse.w != s.diffuse.w) {
            glUniform4f(u_.diffuse, s.diffuse.x, s.diffuse.y, s.diffuse.z, s.diffuse.w);
            c.diffuse = s.diffuse;
        }
        if (!c.valid || c.emissive.x != s.emissive.x || c.emissive.y != s.emissive.y || c.emissive.z != s.emissive.z) {
            glUniform3f(u_.emissive, s.emissive.x, s.emissive.y, s.emissive.z);
            c.emissive = s.emissive;
        }
    }
    const GLuint tex = s.tex ? s.tex : whiteTex_;
    if (c.uploads != TextureCache::uploadCount()) {
        c.tex = ~0u;  // a texture upload changed the binding on unit 0
        c.uploads = TextureCache::uploadCount();
    }
    if (!c.valid || c.hasTex != (s.tex != 0)) { glUniform1i(u_.hasTex, s.tex ? 1 : 0); c.hasTex = s.tex != 0; }
    if (!c.valid || c.tex != tex) { glBindTexture(GL_TEXTURE_2D, tex); c.tex = tex; }
    if (!c.valid || c.alphaRef != s.alphaRef) { glUniform1f(u_.alphaRef, s.alphaRef); c.alphaRef = s.alphaRef; }
    const int fogMode = (fog_.on && s.fog) ? (s.mode == 3 ? 2 : 1) : 0;
    if (!c.valid || c.fogMode != fogMode) { glUniform1i(u_.fogOn, fogMode); c.fogMode = fogMode; }
    c.valid = true;
}

void Renderer::drawRange(const GpuMesh& g, int range) {
    const GpuMesh::Range& r = g.ranges[size_t(range)];
    if (r.count <= 0) return;
    glDrawElements(GL_TRIANGLES, r.count, GL_UNSIGNED_INT,
                   reinterpret_cast<void*>(uintptr_t(r.firstIndex) * 4u));
    ++stats_.drawCalls;
    stats_.triangles += r.count / 3;
}

void Renderer::setBlend(int mode) {
    if (mode == blendMode_) return;
    blendMode_ = mode;
    if (mode == 0) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    if (mode == 1) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    else glBlendFunc(GL_ONE, GL_ONE);
}

void Renderer::drawReflection(const GpuMesh& g, int range, float strength) {
    // Additive second pass with the environment texture (car-model.md, "Reflection pass").
    BatchState s;
    s.mode = 3;
    s.diffuse = vec4(strength, strength, strength, 1.0f);
    s.tex = env_ && !env_->frames.empty() ? env_->frames[0] : 0;
    s.alphaRef = -1;
    if (!s.tex) return;
    setBlend(2);
    glDepthMask(GL_FALSE);
    applyBatch(s);
    drawRange(g, range);
    glDepthMask(GL_TRUE);
}

void Renderer::drawModel(const ModelDraw& d, TextureCache& textures) {
    if (!d.gpu || !d.pkg) return;
    const GpuMesh& g = *d.gpu;
    useMain();
    setModel(d.model);
    glBindVertexArray(g.vao);
    const bool unlit = (d.flags & DrawUnlit) || !g.hasNormals;
    const vec3 centre = d.model.transformPoint((g.boundsMin + g.boundsMax) * 0.5f);
    const float depth = length(centre - eye_);
    const int order = order_++;
    for (size_t i = 0; i < g.ranges.size(); ++i) {
        const Material* mat = d.pkg->material(d.materialSet, g.ranges[i].material);
        BatchState s;
        s.mode = unlit ? 1 : 0;
        s.vertexColor = g.hasColors;
        const GpuTexture* tex = nullptr;
        float refl = 0;
        if (mat) {
            s.diffuse = mat->diffuse;
            if (mat->texture.empty() && untexturedFactor_ != 1.0f) {
                s.diffuse.x *= untexturedFactor_;
                s.diffuse.y *= untexturedFactor_;
                s.diffuse.z *= untexturedFactor_;
            }
            s.emissive = vec3(mat->emissive.x, mat->emissive.y, mat->emissive.z);
            const int mi = g.ranges[i].material;
            if (d.slotTextures && mi >= 0 && size_t(mi) < d.slotTextures->size() && (*d.slotTextures)[size_t(mi)]) {
                tex = (*d.slotTextures)[size_t(mi)];
            } else if (!mat->texture.empty()) {
                std::string name = mat->texture;
                if (d.flags & DrawDamageSlots)
                    name = undamagedTextureName(name, [&](const std::string& n) { return textures.exists(n); });
                tex = textures.get(name, d.user);
            }
            if ((d.flags & DrawReflect) && d.reflections && env_) {
                // g = trunc(255 * power) / 255
                refl = float(int(mat->power * 255.0f)) / 255.0f;
            }
        }
        s.tex = tex ? tex->frameAt(time_) : 0;
        const bool blend = d.canBlend && ((d.flags & DrawForceBlend) || (mat && mat->diffuse.w != 1.0f) ||
                                          (tex && tex->hasAlpha));
        // MM2 switches blending and the alpha test together (pkg.md, "Translucency").
        s.alphaRef = blend ? d.alphaRef : -1.0f;
        if (blend && !d.inlineBlend) {
            Blended b;
            b.gpu = d.gpu;
            b.range = int(i);
            b.model = d.model;
            b.state = s;
            b.refl = refl;
            b.shadow = (d.flags & DrawShadow) != 0;
            b.depth = depth;
            b.order = order;
            blended_.push_back(b);
            continue;
        }
        setBlend(blend ? 1 : 0);
        applyBatch(s);
        drawRange(g, int(i));
        if (refl > 0) {
            drawReflection(g, int(i), refl);
            setBlend(0);
        }
        if (d.groundOverlay && !blend) {
            // The flag test uses material set 0's texture, whatever set is shown (night.md).
            const Material* m0 = d.pkg->material(0, g.ranges[i].material);
            const GpuTexture* t0 = (m0 && !m0->texture.empty()) ? (m0 == mat ? tex : textures.get(m0->texture, d.user)) : nullptr;
            if (overlayWanted(t0)) {
                drawOverlayRange(g, int(i), 1.0f / 128.0f);  // model coordinates
                setBlend(0);
            }
        }
    }
    setBlend(0);
    glBindVertexArray(0);
}

void Renderer::drawSlots(const SlotDraw& d, int pass) {
    if (!d.gpu || !d.slots) return;
    const GpuMesh& g = *d.gpu;
    useMain();
    setModel(d.model);
    glBindVertexArray(g.vao);
    BatchState s;
    s.mode = 2;
    s.vertexColor = true;
    bool any = false;
    for (size_t i = 0; i < g.ranges.size(); ++i) {
        int slot = g.ranges[i].material;
        const GpuTexture* tex = (slot >= 0 && size_t(slot) < d.slots->size()) ? (*d.slots)[size_t(slot)] : nullptr;
        const bool alpha = tex && tex->hasAlpha;
        // Pass 0: opaque textures, no blend or test. Pass 1: alpha textures, blended and tested.
        if (alpha != (pass == 1)) continue;
        if (!any) {
            setBlend(pass == 1 ? 1 : 0);
            any = true;
        }
        s.tex = tex ? tex->frameAt(time_) : 0;
        s.alphaRef = alpha ? d.alphaRef : -1.0f;
        applyBatch(s);
        drawRange(g, int(i));
        if (pass == 0 && overlayWanted(tex)) {
            // World coordinates: u = (x + y)/256, v = (y + z)/256.
            drawOverlayRange(g, int(i), 1.0f / 256.0f);
            setBlend(0);
        }
    }
    setBlend(0);
    glBindVertexArray(0);
}

void Renderer::drawSky(const ModelDraw& d, TextureCache& textures) {
    if (!d.gpu || !d.pkg) return;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    useMain();
    setModel(d.model);
    glBindVertexArray(d.gpu->vao);
    for (size_t i = 0; i < d.gpu->ranges.size(); ++i) {
        const Material* mat = d.pkg->material(d.materialSet, d.gpu->ranges[i].material);
        BatchState s;
        s.mode = 1;
        s.vertexColor = d.gpu->hasColors;
        s.fog = false;
        s.alphaRef = 0;
        const GpuTexture* tex = mat && !mat->texture.empty() ? textures.get(mat->texture, d.user) : nullptr;
        s.tex = tex ? tex->frameAt(time_) : 0;
        bool blend = tex && tex->hasAlpha;
        s.alphaRef = blend ? 0.0f : -1.0f;
        setBlend(blend ? 1 : 0);
        applyBatch(s);
        drawRange(*d.gpu, int(i));
    }
    setBlend(0);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void Renderer::drawOverlayRange(const GpuMesh& g, int range, float scale) {
    // The ground overlay pass (night.md, section 3): the map turned black with its alpha
    // inverted, alpha-blended over the batch just drawn; fog stays on.
    if (!overlay_ || overlay_->frames.empty()) return;
    BatchState s;
    s.mode = 4;
    s.tex = overlay_->frames[0];
    s.alphaRef = -1.0f;
    s.fog = true;
    setBlend(1);
    glDepthMask(GL_FALSE);
    glUniform1f(u_.overlayScale, scale);
    glUniform1i(u_.overlay, 1);
    applyBatch(s);
    drawRange(g, range);
    glUniform1i(u_.overlay, 0);
    glDepthMask(GL_TRUE);
    setBlend(0);
}

bool Renderer::overlayWanted(const GpuTexture* tex) const {
    return overlay_ && tex && !tex->hasAlpha && (tex->flags & 0x2u) != 0;
}

void Renderer::drawGlowModel(const ModelDraw& d, TextureCache& textures) {
    // Glow pass (night.md, section 4): additive, depth test on, no depth writes, unlit, no fog.
    if (!d.gpu || !d.pkg) return;
    const GpuMesh& g = *d.gpu;
    useMain();
    setModel(d.model);
    glBindVertexArray(g.vao);
    glDepthMask(GL_FALSE);
    setBlend(2);
    for (size_t i = 0; i < g.ranges.size(); ++i) {
        const Material* mat = d.pkg->material(d.materialSet, g.ranges[i].material);
        BatchState s;
        s.mode = 1;
        s.vertexColor = g.hasColors;
        s.fog = false;
        s.alphaRef = -1.0f;
        const GpuTexture* tex = mat && !mat->texture.empty() ? textures.get(mat->texture, d.user) : nullptr;
        s.tex = tex ? tex->frameAt(time_) : 0;
        applyBatch(s);
        drawRange(g, int(i));
    }
    setBlend(0);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void Renderer::drawCockpitPart(const ModelDraw& d, TextureCache& textures) {
    if (!d.gpu || !d.pkg) return;
    const GpuMesh& g = *d.gpu;
    useMain();
    setModel(d.model);
    glBindVertexArray(g.vao);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    setBlend(1);
    for (size_t i = 0; i < g.ranges.size(); ++i) {
        const Material* mat = d.pkg->material(d.materialSet, g.ranges[i].material);
        BatchState s;
        s.mode = 1;              // unlit
        s.vertexColor = false;   // texture × white; material colours ignored
        s.fog = false;
        s.alphaRef = 0.0f;       // discard alpha 0
        const GpuTexture* tex = mat && !mat->texture.empty() ? textures.get(mat->texture, d.user) : nullptr;
        s.tex = tex ? tex->frameAt(time_) : 0;
        applyBatch(s);
        drawRange(g, int(i));
    }
    setBlend(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void Renderer::setMirrored(bool on) { glFrontFace(on ? GL_CW : GL_CCW); }

void Renderer::drawBillboards(const std::vector<Billboard>& quads, const GpuTexture* tex) {
    if (quads.empty()) return;
    // Squares facing the camera: corners p ± h·right ± h·up with the camera's axes.
    const vec3 right(view_.m[0], view_.m[4], view_.m[8]);
    const vec3 up(view_.m[1], view_.m[5], view_.m[9]);
    struct V {
        float pos[3], normal[3], uv[2];
        uint8_t color[4];
    };
    std::vector<V> verts;
    verts.reserve(quads.size() * 6);
    for (const Billboard& q : quads) {
        const vec3 rx = right * q.half, uy = up * q.half;
        const vec3 c[4] = {q.centre - rx - uy, q.centre + rx - uy, q.centre + rx + uy, q.centre - rx + uy};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k : idx) {
            V v;
            v.pos[0] = c[k].x;
            v.pos[1] = c[k].y;
            v.pos[2] = c[k].z;
            v.normal[0] = 0;
            v.normal[1] = 1;
            v.normal[2] = 0;
            v.uv[0] = uv[k][0];
            v.uv[1] = uv[k][1];
            for (int j = 0; j < 4; ++j) v.color[j] = q.color[j];
            verts.push_back(v);
        }
    }
    useMain();
    setModel(mat4::identity());
    if (!billboardVao_) {
        glGenVertexArrays(1, &billboardVao_);
        glBindVertexArray(billboardVao_);
        glGenBuffers(1, &billboardVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, billboardVbo_);
        const GLsizei stride = sizeof(V);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(V, pos)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(V, normal)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(V, uv)));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(V, color)));
    }
    glBindVertexArray(billboardVao_);
    glBindBuffer(GL_ARRAY_BUFFER, billboardVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(V)), verts.data(), GL_STREAM_DRAW);
    BatchState s;
    s.mode = 1;
    s.vertexColor = true;
    s.fog = false;
    s.alphaRef = -1.0f;
    s.tex = tex ? tex->frameAt(time_) : 0;
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    setBlend(2);
    applyBatch(s);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
    ++stats_.drawCalls;
    setBlend(0);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void Renderer::drawDecals(const std::vector<DecalDraw>& list) {
    if (list.empty()) return;
    struct V {
        float pos[3], normal[3], uv[2];
        uint8_t color[4];
    };
    useMain();
    setModel(mat4::identity());
    if (!billboardVao_) drawBillboards({Billboard{vec3(), 0.0f, {0, 0, 0, 0}}}, nullptr);  // creates the buffers
    glBindVertexArray(billboardVao_);
    glBindBuffer(GL_ARRAY_BUFFER, billboardVbo_);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    if (variant_ == 1) glUniform1f(variants_[1].depthBias, logK_ * 0.0003f);
    else glDepthRangef(0.001f, 0.999f);
    setBlend(1);
    std::vector<V> verts;
    for (const DecalDraw& d : list) {
        if (!d.verts || d.verts->empty()) continue;
        verts.clear();
        for (const DecalVertex& dv : *d.verts) {
            V v;
            v.pos[0] = dv.pos.x;
            v.pos[1] = dv.pos.y;
            v.pos[2] = dv.pos.z;
            v.normal[0] = 0;
            v.normal[1] = 1;
            v.normal[2] = 0;
            v.uv[0] = dv.u;
            v.uv[1] = dv.v;
            v.color[0] = v.color[1] = v.color[2] = v.color[3] = 255;
            verts.push_back(v);
        }
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(V)), verts.data(), GL_STREAM_DRAW);
        BatchState s;
        s.mode = 1;
        s.vertexColor = true;
        s.fog = true;
        s.alphaRef = 0.0f;
        s.tex = d.tex ? d.tex->frameAt(time_) : 0;
        applyBatch(s);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
        ++stats_.drawCalls;
        stats_.triangles += int(verts.size() / 3);
    }
    setBlend(0);
    if (variant_ == 1) glUniform1f(variants_[1].depthBias, 0.0f);
    else glDepthRangef(0.0f, 1.0f);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void Renderer::drawScreenQuads(const std::vector<ScreenQuad>& quads, const GpuTexture* tex) {
    if (quads.empty()) return;
    // Built like billboards, with identity camera matrices so positions are device coordinates.
    struct V {
        float pos[3], normal[3], uv[2];
        uint8_t color[4];
    };
    std::vector<V> verts;
    verts.reserve(quads.size() * 6);
    for (const ScreenQuad& q : quads) {
        const float c[4][2] = {{q.x0, q.y0}, {q.x1, q.y0}, {q.x1, q.y1}, {q.x0, q.y1}};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const int idx[6] = {0, 1, 2, 0, 2, 3};
        for (int k : idx) {
            V v;
            v.pos[0] = c[k][0];
            v.pos[1] = c[k][1];
            v.pos[2] = 0.0f;
            v.normal[0] = 0;
            v.normal[1] = 1;
            v.normal[2] = 0;
            v.uv[0] = uv[k][0];
            v.uv[1] = uv[k][1];
            for (int j = 0; j < 4; ++j) v.color[j] = q.color[j];
            verts.push_back(v);
        }
    }
    const mat4 view = view_, proj = proj_;
    const vec3 eye = eye_;
    setCamera(mat4::identity(), mat4::identity(), eye);
    useMain();
    setModel(mat4::identity());
    if (!billboardVao_) drawBillboards({Billboard{vec3(), 0.0f, {0, 0, 0, 0}}}, nullptr);  // creates the buffers
    glBindVertexArray(billboardVao_);
    glBindBuffer(GL_ARRAY_BUFFER, billboardVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(V)), verts.data(), GL_STREAM_DRAW);
    BatchState s;
    s.mode = 1;
    s.vertexColor = true;
    s.fog = false;
    s.alphaRef = -1.0f;
    s.tex = tex ? tex->frameAt(time_) : 0;
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    setBlend(2);
    applyBatch(s);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
    ++stats_.drawCalls;
    setBlend(0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    setCamera(view, proj, eye);
}

void Renderer::flush() {
    if (blended_.empty()) return;
    // Farthest object first; batches of one object keep their order. Shadows go first: they lie
    // on the ground under everything else.
    std::stable_sort(blended_.begin(), blended_.end(), [](const Blended& a, const Blended& b) {
        if (a.shadow != b.shadow) return a.shadow;
        if (a.depth != b.depth) return a.depth > b.depth;
        return a.order < b.order;
    });
    useMain();
    const GpuMesh* bound = nullptr;
    for (const Blended& b : blended_) {
        if (b.shadow) {
            glDepthMask(GL_FALSE);
            if (variant_ == 1) {
                // gl_FragDepth bypasses the polygon offset: a small bias in log-depth units.
                glUniform1f(variants_[1].depthBias, logK_ * 0.0003f);
            } else {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-1.0f, -2.0f);
            }
        }
        if (bound != b.gpu) {
            glBindVertexArray(b.gpu->vao);
            bound = b.gpu;
        }
        setModel(b.model);
        setBlend(1);
        applyBatch(b.state);
        drawRange(*b.gpu, b.range);
        if (b.refl > 0) drawReflection(*b.gpu, b.range, b.refl);
        if (b.shadow) {
            glDepthMask(GL_TRUE);
            if (variant_ == 1) glUniform1f(variants_[1].depthBias, 0.0f);
            else glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }
    glBindVertexArray(0);
    setBlend(0);
    blended_.clear();
}

void Renderer::drawGradient(const vec3& top, const vec3& bottom) {
    glUseProgram(gradProg_);
    current_ = gradProg_;
    glUniform3f(gradTop_, top.x, top.y, top.z);
    glUniform3f(gradBottom_, bottom.x, bottom.y, bottom.z);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(gradVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    ++stats_.drawCalls;
}

void Renderer::drawTriangles(const std::vector<SimpleVertex>& verts, const mat4& model, bool depthWrite,
                             bool depthTest) {
    if (verts.empty()) return;
    glUseProgram(simpleProg_);
    current_ = simpleProg_;
    glUniformMatrix4fv(simpleViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniformMatrix4fv(simpleModel_, 1, GL_FALSE, model.m);
    if (variant_ == 1) glUniform1f(variants_[1].simpleLogK, logK_);
    glBindVertexArray(simpleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, simpleVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(SimpleVertex)), verts.data(), GL_STREAM_DRAW);
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);  // helper geometry is two-sided
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts.size()));
    if (!depthTest) glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    blendMode_ = 0;
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
    ++stats_.drawCalls;
}

void Renderer::drawEffectTriangles(const void* verts, int count, const GpuTexture* tex, bool depthTest, float alphaRef) {
    if (!verts || count <= 0) return;
    useMain();
    setModel(mat4::identity());
    const GLsizei stride = 24;
    if (!fxVao_) {
        glGenVertexArrays(1, &fxVao_);
        glBindVertexArray(fxVao_);
        glGenBuffers(1, &fxVbo_);
        glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(0));
        glDisableVertexAttribArray(1);
        glVertexAttrib3f(1, 0.0f, 1.0f, 0.0f);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(20));
    }
    glBindVertexArray(fxVao_);
    glBindBuffer(GL_ARRAY_BUFFER, fxVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(size_t(count) * size_t(stride)), verts, GL_STREAM_DRAW);
    BatchState s;
    s.mode = 1;
    s.vertexColor = true;
    s.fog = false;
    s.alphaRef = alphaRef;
    s.tex = tex ? tex->frameAt(time_) : 0;
    glDepthMask(GL_FALSE);
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    setBlend(1);
    applyBatch(s);
    glDrawArrays(GL_TRIANGLES, 0, count);
    ++stats_.drawCalls;
    stats_.triangles += count / 3;
    setBlend(0);
    glEnable(GL_CULL_FACE);
    if (!depthTest) glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glBindVertexArray(0);
}

void Renderer::clampTextureU(const GpuTexture* tex) {
    if (!tex) return;
    for (GLuint id : tex->frames) {
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    }
    cache_.valid = false;
    TextureCache::noteBindingChanged();
}

void Renderer::drawLines(const std::vector<SimpleVertex>& verts, const mat4& model, bool depthTest, bool depthWrite) {
    if (verts.empty()) return;
    glUseProgram(simpleProg_);
    current_ = simpleProg_;
    glUniformMatrix4fv(simpleViewProj_, 1, GL_FALSE, viewProj_.m);
    glUniformMatrix4fv(simpleModel_, 1, GL_FALSE, model.m);
    if (variant_ == 1) glUniform1f(variants_[1].simpleLogK, logK_);
    glBindVertexArray(simpleVao_);
    glBindBuffer(GL_ARRAY_BUFFER, simpleVbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(SimpleVertex)), verts.data(), GL_STREAM_DRAW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!depthTest) glDisable(GL_DEPTH_TEST);
    if (!depthWrite) glDepthMask(GL_FALSE);
    glDrawArrays(GL_LINES, 0, GLsizei(verts.size()));
    if (!depthWrite) glDepthMask(GL_TRUE);
    if (!depthTest) glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    blendMode_ = 0;
    glBindVertexArray(0);
    ++stats_.drawCalls;
}

}  // namespace cc
