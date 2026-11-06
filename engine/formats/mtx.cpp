#include "formats/mtx.h"

#include "core/bytes.h"
#include "core/strutil.h"

namespace cc {

bool parseMtx(const uint8_t* data, size_t size, Pivot& out, std::string& err) {
    out = Pivot();
    if (size < 48) {
        err = strf("pivot file is %zu bytes, expected 48", size);
        return false;
    }
    ByteReader r(data, size);
    auto v3 = [&]() {
        vec3 v;
        v.x = r.f32();
        v.y = r.f32();
        v.z = r.f32();
        return v;
    };
    out.boxMin = v3();
    out.boxMax = v3();
    out.centre = v3();
    out.origin = v3();
    out.found = true;
    return true;
}

std::string pivotPath(const std::string& package, const std::string& part) {
    return "geometry/" + package + "_" + (part.empty() ? std::string("(null)") : part) + ".mtx";
}

}  // namespace cc
