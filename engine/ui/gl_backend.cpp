#include "ui/gl_backend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "render/texture_cache.h"
#include "ui/text.h"

namespace cc {
namespace ui {

namespace {

const char* kVs = R"(#version 300 es
layout(location = 0) in vec2 a_corner;
layout(location = 1) in vec4 i_box;
layout(location = 2) in vec4 i_radii;
layout(location = 3) in vec4 i_fill;
layout(location = 4) in vec4 i_fill2;
layout(location = 5) in vec4 i_border;
layout(location = 6) in vec4 i_p0;
layout(location = 7) in vec4 i_p1;
layout(location = 8) in vec4 i_uv;
layout(location = 9) in vec4 i_clip;
uniform vec2 u_view;
out vec2 v_pos;
flat out vec4 v_box;
flat out vec4 v_radii;
flat out vec4 v_fill;
flat out vec4 v_fill2;
flat out vec4 v_border;
flat out vec4 v_p0;
flat out vec4 v_p1;
flat out vec4 v_uv;
flat out vec4 v_clip;
void main() {
    vec4 area;
    if (i_p0.x > 0.5 && i_p0.x < 1.5) {
        float reach = i_p1.x * 1.5 + max(0.0, i_p1.y) + 1.0;
        area = vec4(i_box.xy + i_uv.xy - reach, i_box.zw + 2.0 * reach);
    } else {
        area = vec4(i_box.xy - 1.0, i_box.zw + 2.0);
    }
    vec2 a0 = max(area.xy, i_clip.xy);
    vec2 a1 = min(area.xy + area.zw, i_clip.zw);
    a1 = max(a1, a0);
    vec2 p = mix(a0, a1, a_corner);
    v_pos = p;
    vec2 ndc = p / u_view * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    v_box = i_box; v_radii = i_radii; v_fill = i_fill; v_fill2 = i_fill2; v_border = i_border;
    v_p0 = i_p0; v_p1 = i_p1; v_uv = i_uv; v_clip = i_clip;
}
)";

const char* kFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_atlas;
uniform sampler2D u_image;
uniform sampler2D u_capture;
uniform sampler2D u_blur;
uniform vec2 u_view;
in vec2 v_pos;
flat in vec4 v_box;
flat in vec4 v_radii;
flat in vec4 v_fill;
flat in vec4 v_fill2;
flat in vec4 v_border;
flat in vec4 v_p0;
flat in vec4 v_p1;
flat in vec4 v_uv;
flat in vec4 v_clip;
out vec4 o_color;
float sdRound(vec2 p, vec2 h, vec4 r) {
    float rad = p.x < 0.0 ? (p.y < 0.0 ? r.x : r.w) : (p.y < 0.0 ? r.y : r.z);
    rad = min(rad, min(h.x, h.y));
    vec2 q = abs(p) - h + rad;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - rad;
}
float erfA(float x) {
    float s = sign(x);
    x = abs(x);
    float t = 1.0 / (1.0 + 0.47047 * x);
    float y = 1.0 - t * (0.3480242 + t * (-0.0958798 + t * 0.7478556)) * exp(-x * x);
    return s * y;
}
vec4 pm(vec4 c) { return vec4(c.rgb * c.a, c.a); }
void main() {
    if (v_pos.x < v_clip.x || v_pos.y < v_clip.y || v_pos.x >= v_clip.z || v_pos.y >= v_clip.w) discard;
    vec2 h = v_box.zw * 0.5;
    vec2 c = v_box.xy + h;
    vec2 p = v_pos - c;
    int kind = int(v_p0.x + 0.5);
    vec4 col = vec4(0.0);
    if (kind == 0) {
        float d = sdRound(p, h, v_radii);
        float inside = clamp(0.5 - d, 0.0, 1.0);
        if (inside <= 0.0) discard;
        vec4 f = pm(v_fill);
        if (v_p0.z > 0.5) {
            float t;
            vec2 l = v_pos - v_box.xy;
            if (v_p0.z > 1.5) {
                vec2 q = (l - h) / (h * 1.41421356);
                t = clamp(length(q), 0.0, 1.0);
            } else {
                vec2 dir = vec2(sin(v_p0.w), -cos(v_p0.w));
                float len = abs(v_box.z * dir.x) + abs(v_box.w * dir.y);
                t = clamp(dot(l - h, dir) / max(len, 0.0001) + 0.5, 0.0, 1.0);
            }
            f = mix(pm(v_fill), pm(v_fill2), t);
        }
        col = f * inside;
        if (v_p0.y > 0.0) {
            float inner = clamp(0.5 - (d + v_p0.y), 0.0, 1.0);
            vec4 b = pm(v_border) * max(0.0, inside - inner);
            col = b + col * (1.0 - b.a);
        }
    } else if (kind == 1) {
        vec4 r = max(v_radii + v_p1.y, 0.0);
        float d = sdRound(p - v_uv.xy, h + v_p1.y, r);
        float a;
        if (v_p1.x > 0.01) {
            float sigma = v_p1.x * 0.5;
            a = 0.5 * (1.0 - erfA(d / (sigma * 1.41421356)));
        } else {
            a = clamp(0.5 - d, 0.0, 1.0);
        }
        if (v_p1.z > 0.5) a *= clamp(sdRound(p, h, v_radii) + 0.5, 0.0, 1.0);
        if (a <= 0.0) discard;
        col = pm(v_fill) * a;
    } else if (kind == 2) {
        vec2 l = (v_pos - v_box.xy) / v_box.zw;
        if (l.x < 0.0 || l.y < 0.0 || l.x > 1.0 || l.y > 1.0) discard;
        vec2 uv = mix(v_uv.xy, v_uv.zw, l);
        float cov = texture(u_atlas, uv).r;
        col = pm(v_fill) * cov;
    } else if (kind == 3) {
        float d = sdRound(p, h, v_radii);
        float inside = clamp(0.5 - d, 0.0, 1.0);
        if (inside <= 0.0) discard;
        vec2 l = clamp((v_pos - v_box.xy) / v_box.zw, 0.0, 1.0);
        vec4 t = texture(u_image, mix(v_uv.xy, v_uv.zw, l));
        col = vec4(t.rgb * v_fill.rgb, t.a * v_fill.a);
        col = pm(col) * inside;
    } else if (kind == 4) {
        float d = sdRound(p, h, v_radii);
        float inside = clamp(0.5 - d, 0.0, 1.0);
        if (inside <= 0.0) discard;
        vec3 b = texture(u_blur, vec2(v_pos.x / u_view.x, 1.0 - v_pos.y / u_view.y)).rgb;
        col = vec4(b, 1.0) * inside;
    } else {
        vec2 l = (v_pos - v_box.xy) / v_box.zw;
        if (l.x < 0.0 || l.y < 0.0 || l.x > 1.0 || l.y > 1.0) discard;
        vec3 s = texture(u_capture, vec2(v_pos.x / u_view.x, 1.0 - v_pos.y / u_view.y)).rgb * v_p1.x;
        float lum = dot(s, vec3(0.2126, 0.7152, 0.0722));
        s = max(vec3(lum) + (s - vec3(lum)) * v_p1.y, 0.0);
        col = vec4(s, 1.0);
    }
    o_color = col;
}
)";

const char* kQuadVs = R"(#version 300 es
out vec2 v_uv;
void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char* kDownFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_texel;
in vec2 v_uv;
out vec4 o;
void main() {
    o = 0.25 * (texture(u_tex, v_uv + u_texel * vec2(-1.0, -1.0)) + texture(u_tex, v_uv + u_texel * vec2(1.0, -1.0)) +
                texture(u_tex, v_uv + u_texel * vec2(-1.0, 1.0)) + texture(u_tex, v_uv + u_texel * vec2(1.0, 1.0)));
}
)";

const char* kBlurFs = R"(#version 300 es
precision highp float;
uniform sampler2D u_tex;
uniform vec2 u_dir;
uniform float u_sigma;
uniform int u_taps;
in vec2 v_uv;
out vec4 o;
void main() {
    vec4 acc = texture(u_tex, v_uv);
    float wsum = 1.0;
    for (int i = 1; i <= 40; ++i) {
        if (i > u_taps) break;
        float w = exp(-float(i * i) / (2.0 * u_sigma * u_sigma));
        acc += (texture(u_tex, v_uv + u_dir * float(i)) + texture(u_tex, v_uv - u_dir * float(i))) * w;
        wsum += 2.0 * w;
    }
    o = acc / wsum;
}
)";

constexpr int kFloats = 36;

void texParams(GLenum filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

}  // namespace

bool GlBackend::init() {
    prog_ = buildProgram("ui", kVs, kFs);
    blurProg_ = buildProgram("ui blur", kQuadVs, kBlurFs);
    downProg_ = buildProgram("ui downsample", kQuadVs, kDownFs);
    if (!prog_ || !blurProg_ || !downProg_) return false;
    uView_ = glGetUniformLocation(prog_, "u_view");
    uAtlas_ = glGetUniformLocation(prog_, "u_atlas");
    uImage_ = glGetUniformLocation(prog_, "u_image");
    uCapture_ = glGetUniformLocation(prog_, "u_capture");
    uBlur_ = glGetUniformLocation(prog_, "u_blur");
    bTex_ = glGetUniformLocation(blurProg_, "u_tex");
    bDir_ = glGetUniformLocation(blurProg_, "u_dir");
    bSigma_ = glGetUniformLocation(blurProg_, "u_sigma");
    bTaps_ = glGetUniformLocation(blurProg_, "u_taps");
    dTex_ = glGetUniformLocation(downProg_, "u_tex");
    dTexel_ = glGetUniformLocation(downProg_, "u_texel");
    glGenVertexArrays(1, &vao_);
    glGenVertexArrays(1, &emptyVao_);
    glGenBuffers(1, &quadVbo_);
    glGenBuffers(1, &instVbo_);
    glBindVertexArray(vao_);
    const float quad[8] = {0, 0, 1, 0, 0, 1, 1, 1};
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int a = 0; a < 9; ++a) {
        glEnableVertexAttribArray(GLuint(1 + a));
        glVertexAttribPointer(GLuint(1 + a), 4, GL_FLOAT, GL_FALSE, kFloats * 4, reinterpret_cast<void*>(uintptr_t(a * 16)));
        glVertexAttribDivisor(GLuint(1 + a), 1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGenTextures(1, &atlasTex_);
    // The default framebuffer's format (the capture copies it, which needs the same format).
    GLint alpha = 8;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_BACK, GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE, &alpha);
    while (glGetError() != GL_NO_ERROR) {
    }
    capFormat_ = alpha > 0 ? GL_RGBA8 : GL_RGB8;
    return true;
}

void GlBackend::shutdown() {
    if (prog_) glDeleteProgram(prog_);
    if (blurProg_) glDeleteProgram(blurProg_);
    if (downProg_) glDeleteProgram(downProg_);
    prog_ = blurProg_ = downProg_ = 0;
}

void GlBackend::uploadAtlas() {
    Atlas& A = Fonts::get().atlas();
    if (A.pix.empty()) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, atlasTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (atlasW_ != A.w || atlasH_ != A.h || atlasGen_ != A.generation) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, A.w, A.h, 0, GL_RED, GL_UNSIGNED_BYTE, A.pix.data());
        texParams(GL_LINEAR);
        atlasW_ = A.w;
        atlasH_ = A.h;
        atlasGen_ = A.generation;
        A.dirtyY0 = A.dirtyY1 = 0;
        return;
    }
    if (A.dirtyY1 > A.dirtyY0) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, A.dirtyY0, A.w, A.dirtyY1 - A.dirtyY0, GL_RED, GL_UNSIGNED_BYTE,
                        A.pix.data() + size_t(A.dirtyY0) * size_t(A.w));
        A.dirtyY0 = A.dirtyY1 = 0;
    }
}

void GlBackend::ensureTargets(int w, int h) {
    if (w == capW_ && h == capH_ && capTex_) return;
    auto make = [](GLuint& tex, GLuint& fbo, int tw, int th, GLenum fmt) {
        if (!tex) glGenTextures(1, &tex);
        if (!fbo) glGenFramebuffers(1, &fbo);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GLint(fmt), tw, th, 0, fmt == GL_RGB8 ? GL_RGB : GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        texParams(GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    };
    capW_ = w;
    capH_ = h;
    smallW_ = std::max(1, (w + 3) / 4);
    smallH_ = std::max(1, (h + 3) / 4);
    make(capTex_, capFbo_, w, h, capFormat_);
    make(smallTex_[0], smallFbo_[0], smallW_, smallH_, GL_RGBA8);
    make(smallTex_[1], smallFbo_[1], smallW_, smallH_, GL_RGBA8);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

bool GlBackend::capture(int w, int h, float blur) {
    if (capFailed_) return false;
    ensureTargets(w, h);
    while (glGetError() != GL_NO_ERROR) {
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, capFbo_);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (glGetError() != GL_NO_ERROR) {
        capFailed_ = true;
        logWarn("UI: the frame cannot be copied for the glass panels' blur; they are drawn without it");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, smallFbo_[0]);
    glViewport(0, 0, smallW_, smallH_);
    glDisable(GL_BLEND);
    glBindVertexArray(emptyVao_);
    glUseProgram(downProg_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, capTex_);
    glUniform1i(dTex_, 0);
    glUniform2f(dTexel_, 1.0f / float(w), 1.0f / float(h));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    // CSS blur(r): a Gaussian of standard deviation r (device px), at a quarter of the size.
    const float sigma = std::max(0.5f, blur * 0.25f);
    const int taps = std::min(40, int(std::ceil(sigma * 2.5f)));
    glUseProgram(blurProg_);
    glUniform1i(bTex_, 0);
    glUniform1f(bSigma_, sigma);
    glUniform1i(bTaps_, taps);
    glBindFramebuffer(GL_FRAMEBUFFER, smallFbo_[1]);
    glBindTexture(GL_TEXTURE_2D, smallTex_[0]);
    glUniform2f(bDir_, 1.0f / float(smallW_), 0.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, smallFbo_[0]);
    glBindTexture(GL_TEXTURE_2D, smallTex_[1]);
    glUniform2f(bDir_, 0.0f, 1.0f / float(smallH_));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    capOk_ = true;
    return true;
}

void GlBackend::render(const DrawList& dl, int w, int h) {
    if (!prog_ || dl.cmds.empty()) return;
    uploadAtlas();
    // Instance data.
    inst_.resize(dl.prims.size() * kFloats);
    for (size_t i = 0; i < dl.prims.size(); ++i) {
        const Prim& p = dl.prims[i];
        float* f = &inst_[i * kFloats];
        f[0] = p.box.x;
        f[1] = p.box.y;
        f[2] = p.box.w;
        f[3] = p.box.h;
        for (int k = 0; k < 4; ++k) f[4 + k] = p.radius[k];
        const Color* cs[3] = {&p.fill, &p.fill2, &p.border};
        for (int c = 0; c < 3; ++c) {
            f[8 + 4 * c] = cs[c]->r;
            f[9 + 4 * c] = cs[c]->g;
            f[10 + 4 * c] = cs[c]->b;
            f[11 + 4 * c] = cs[c]->a;
        }
        f[20] = float(p.kind);
        f[21] = p.borderW;
        f[22] = float(p.grad);
        f[23] = p.gradAngle;
        f[24] = p.blur;
        f[25] = p.spread;
        f[26] = p.outsideOnly ? 1.0f : 0.0f;
        f[27] = 0;
        for (int k = 0; k < 4; ++k) f[28 + k] = p.uv[k];
        f[32] = p.clip.x;
        f[33] = p.clip.y;
        f[34] = p.clip.right();
        f[35] = p.clip.bottom();
    }
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    const size_t bytes = inst_.size() * sizeof(float);
    if (bytes > instCap_) {
        instCap_ = bytes * 2;
        glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(instCap_), nullptr, GL_DYNAMIC_DRAW);
    }
    if (bytes) glBufferSubData(GL_ARRAY_BUFFER, 0, GLsizeiptr(bytes), inst_.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    auto state = [&]() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, w, h);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_SCISSOR_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(prog_);
        glUniform2f(uView_, float(w), float(h));
        glUniform1i(uAtlas_, 0);
        glUniform1i(uImage_, 1);
        glUniform1i(uCapture_, 2);
        glUniform1i(uBlur_, 3);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlasTex_);
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, capOk_ ? capTex_ : atlasTex_);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, capOk_ ? smallTex_[0] : atlasTex_);
        glBindVertexArray(vao_);
    };
    capOk_ = false;
    state();
    GLuint boundImage = 0;
    for (const DrawCmd& c : dl.cmds) {
        if (c.type == DrawCmd::Capture) {
            capture(w, h, c.blur);
            state();
            boundImage = 0;
            continue;
        }
        int start = c.first;
        const int end = c.first + c.count;
        while (start < end) {
            // A batch: the primitives up to the next image change (backdrops without a capture
            // are left out).
            int stop = start;
            GLuint img = boundImage;
            for (; stop < end; ++stop) {
                const Prim& p = dl.prims[size_t(stop)];
                if (p.kind == PrimImage && p.image >= 0) {
                    const GLuint t = dl.images[size_t(p.image)].gl;
                    if (stop == start) img = t;
                    else if (t != img) break;
                }
                if ((p.kind == PrimBackdrop || p.kind == PrimFilter) && !capOk_) break;
            }
            if (stop == start) {
                ++start;  // a backdrop with no capture: skip it
                continue;
            }
            if (img != boundImage) {
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, img);
                boundImage = img;
            }
            // drawArraysInstanced has no base instance: point the attributes at the batch.
            glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
            for (int a = 0; a < 9; ++a)
                glVertexAttribPointer(GLuint(1 + a), 4, GL_FLOAT, GL_FALSE, kFloats * 4,
                                      reinterpret_cast<void*>(uintptr_t(size_t(start) * kFloats * 4 + size_t(a) * 16)));
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, stop - start);
            start = stop;
        }
    }
    // Back to the renderer's expectations: unit 0 active, nothing bound, depth writes on.
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instVbo_);
    for (int a = 0; a < 9; ++a)
        glVertexAttribPointer(GLuint(1 + a), 4, GL_FLOAT, GL_FALSE, kFloats * 4, reinterpret_cast<void*>(uintptr_t(a * 16)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    TextureCache::noteBindingChanged();
}

}  // namespace ui
}  // namespace cc
