#include "render/gl_util.h"

#include <cstring>
#include <vector>

#include "core/log.h"

namespace cc {

namespace {
GLuint compile(const char* name, GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(size_t(len > 1 ? len : 1));
        glGetShaderInfoLog(s, GLsizei(log.size()), nullptr, log.data());
        logError("shader %s (%s) failed to compile: %s", name,
                 type == GL_VERTEX_SHADER ? "vertex" : "fragment", log.data());
        glDeleteShader(s);
        return 0;
    }
    return s;
}
}  // namespace

GLuint buildProgram(const char* name, const char* vs, const char* fs) {
    GLuint v = compile(name, GL_VERTEX_SHADER, vs);
    GLuint f = compile(name, GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(size_t(len > 1 ? len : 1));
        glGetProgramInfoLog(p, GLsizei(log.size()), nullptr, log.data());
        logError("program %s failed to link: %s", name, log.data());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool hasGlExtension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, GLuint(i)));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}

void checkGl(const char* where) {
    for (int i = 0; i < 8; ++i) {
        GLenum e = glGetError();
        if (e == GL_NO_ERROR) return;
        logWarn("GL error 0x%x at %s", e, where);
    }
}

}  // namespace cc
