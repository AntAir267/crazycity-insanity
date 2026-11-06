#include "formats/inst.h"

#include <cstring>

#include "core/bytes.h"
#include "core/strutil.h"

namespace cc {

bool parseInst(const uint8_t* data, size_t size, std::vector<InstRecord>& out, std::string& err) {
    out.clear();
    ByteReader r(data, size);
    while (!r.atEnd()) {
        InstRecord rec;
        rec.header = r.u32();
        uint8_t lb = r.u8();
        size_t len = lb & 0x7F;
        rec.compact = (lb & 0x80) != 0;
        if (!r.ok() || !r.need(len)) {
            err = strf("record %zu truncated", out.size());
            return false;
        }
        const char* s = reinterpret_cast<const char*>(r.here());
        std::string name(s, strnlen(s, len));
        r.skip(len);
        size_t bs = name.find_last_of('\\');
        rec.name = bs == std::string::npos ? name : name.substr(bs + 1);
        rec.room = uint16_t(rec.header & 0xFFFF);
        rec.variant = uint8_t((rec.header >> 16) & 0xFF);
        rec.landmark = (rec.header >> 24) & 1;
        rec.breakable = (rec.header >> 25) & 1;
        rec.landmarkDetail = (rec.header >> 26) & 1;
        rec.spansRooms = (rec.header >> 29) & 1;
        if (rec.compact) {
            float c = r.f32(), sn = r.f32();
            float px = r.f32(), py = r.f32(), pz = r.f32();
            const float m[12] = {c, 0, sn, 0, 1, 0, -sn, 0, c, px, py, pz};
            std::memcpy(rec.matrix, m, sizeof m);
        } else {
            for (float& f : rec.matrix) f = r.f32();
        }
        if (!r.ok()) {
            err = strf("record %zu placement truncated", out.size());
            return false;
        }
        out.push_back(std::move(rec));
    }
    return true;
}

void flattenLandmarkMatrix(float m[12]) {
    const float x = m[0], z = m[2];
    const float f[9] = {x, 0, z, 0, 1, 0, -z, 0, x};
    std::memcpy(m, f, sizeof f);
}

}  // namespace cc
