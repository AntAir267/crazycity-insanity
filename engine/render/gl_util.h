// OpenGL ES 3.0 / WebGL 2 includes and small helpers.
#pragma once

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include "render/gl_native.h"  // the same entry points, loaded at start-up (render/gl_native.cpp)
#endif

#include <string>

namespace cc {

// Compiles and links a program; logs and returns 0 on failure.
GLuint buildProgram(const char* name, const char* vertexSrc, const char* fragmentSrc);

bool hasGlExtension(const char* name);

// Logs any pending GL errors with a label (debug aid).
void checkGl(const char* where);

}  // namespace cc
