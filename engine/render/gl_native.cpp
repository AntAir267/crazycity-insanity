#include "render/gl_native.h"

namespace cc {

#define CC_GL_DEFINE(type, name) type name = nullptr;
CC_GL_FUNCTIONS(CC_GL_DEFINE)
#undef CC_GL_DEFINE

bool loadGlFunctions(void* (*getProc)(const char*), std::string* missing) {
    bool ok = true;
#define CC_GL_LOAD(type, name)                                   \
    name = reinterpret_cast<type>(getProc(#name));               \
    if (!name) {                                                 \
        ok = false;                                              \
        if (missing) *missing += (missing->empty() ? "" : " ") + std::string(#name); \
    }
    CC_GL_FUNCTIONS(CC_GL_LOAD)
#undef CC_GL_LOAD
    return ok;
}

}  // namespace cc
