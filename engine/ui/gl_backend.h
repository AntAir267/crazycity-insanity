// The UI's draw list on WebGL 2 / OpenGL ES 3: every primitive is one instanced quad whose
// fragment shader evaluates the rounded box, gradient, shadow, glyph or image (the rules of
// ui/shade.h); captures copy the frame drawn so far (resolving multisampling) and blur it at a
// quarter of the resolution for the glass panels' backdrops.
#pragma once

#include <vector>

#include "render/gl_util.h"
#include "ui/draw.h"

namespace cc {
namespace ui {

class GlBackend {
public:
    bool init();
    void shutdown();
    // Draws `dl` over the current default framebuffer (w × h device px).
    void render(const DrawList& dl, int w, int h);

private:
    void uploadAtlas();
    bool capture(int w, int h, float blur);
    void ensureTargets(int w, int h);

    GLuint prog_ = 0, blurProg_ = 0, downProg_ = 0;
    GLuint vao_ = 0, quadVbo_ = 0, instVbo_ = 0, emptyVao_ = 0;
    GLint uView_ = -1, uAtlas_ = -1, uImage_ = -1, uCapture_ = -1, uBlur_ = -1;
    GLint bTex_ = -1, bDir_ = -1, bSigma_ = -1, bTaps_ = -1, dTex_ = -1, dTexel_ = -1;
    GLuint atlasTex_ = 0;
    int atlasW_ = 0, atlasH_ = 0;
    unsigned atlasGen_ = 0;
    // Capture targets: the full frame and two small blur buffers.
    GLuint capTex_ = 0, capFbo_ = 0, smallTex_[2] = {0, 0}, smallFbo_[2] = {0, 0};
    int capW_ = 0, capH_ = 0, smallW_ = 0, smallH_ = 0;
    bool capOk_ = false, capFailed_ = false;
    GLenum capFormat_ = 0;
    std::vector<float> inst_;
    size_t instCap_ = 0;
};

}  // namespace ui
}  // namespace cc
