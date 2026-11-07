#include "render/gl_native.h"

namespace cc {

#define CC_GL_DEFINE(type, name) type name = nullptr;
CC_GL_FUNCTIONS(CC_GL_DEFINE)
#undef CC_GL_DEFINE

namespace {
// A do-nothing stand-in for an entry point of type F.
template <typename F>
struct GlStub;
template <typename R, typename... A>
struct GlStub<R(GL_APIENTRYP)(A...)> {
    static R GL_APIENTRY call(A...) { return R(); }
};
template <typename... A>
struct GlStub<void(GL_APIENTRYP)(A...)> {
    static void GL_APIENTRY call(A...) {}
};
}  // namespace

void unloadGlFunctions() {
#define CC_GL_UNLOAD(type, name) name = &GlStub<type>::call;
    CC_GL_FUNCTIONS(CC_GL_UNLOAD)
#undef CC_GL_UNLOAD
}

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
