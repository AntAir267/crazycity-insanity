#include "formats/psdl.h"

#include <cstring>

#include "core/bytes.h"
#include "core/strutil.h"

namespace cc {

std::string Psdl::lookupName(const std::string& n) {
    if (n.size() >= 5 && n[n.size() - 5] == '-' && n[n.size() - 4] == '0') return n.substr(0, n.size() - 5);
    return n;
}

bool parsePsdl(const uint8_t* data, size_t size, Psdl& p, std::string& err) {
    p = Psdl();
    ByteReader r(data, size);
    char magic[4];
    if (!r.bytes(magic, 4) || std::memcmp(magic, "PSD0", 4) != 0) { err = "not a PSD0 file"; return false; }
    uint32_t version = r.u32();
    if (version != 2) { err = strf("version %u is not supported (only 2)", version); return false; }

    uint32_t nv = r.u32();
    if (!r.ok() || !r.need(size_t(nv) * 12)) { err = "point table truncated"; return false; }
    p.points.resize(nv);
    for (auto& v : p.points) {
        v.x = r.f32();
        v.y = r.f32();
        v.z = r.f32();
    }
    uint32_t nf = r.u32();
    if (!r.ok() || !r.need(size_t(nf) * 4)) { err = "height table truncated"; return false; }
    p.heights.resize(nf);
    for (auto& f : p.heights) f = r.f32();

    uint32_t nt = r.u32();
    if (!r.ok() || nt > 65536) { err = "bad texture count"; return false; }
    p.textures.assign(nt ? nt : 1, std::string());
    for (uint32_t i = 1; i < nt; ++i) {
        uint8_t len = r.u8();
        if (!r.need(len)) { err = "texture names truncated"; return false; }
        const char* s = reinterpret_cast<const char*>(r.here());
        p.textures[i].assign(s, strnlen(s, len));
        r.skip(len);
    }

    uint32_t nr = r.u32();
    p.firstNonBlockRoom = r.u32();
    if (!r.ok() || nr > 65536) { err = "bad room count"; return false; }
    p.rooms.resize(nr ? nr : 1);
    for (uint32_t i = 1; i < nr; ++i) {
        PsdlRoom& room = p.rooms[i];
        uint32_t np = r.u32();
        uint32_t na = r.u32();
        if (!r.ok() || !r.need(size_t(np) * 4 + size_t(na) * 2)) {
            err = strf("room %u truncated", i);
            return false;
        }
        room.perimeter.resize(np);
        room.neighbours.resize(np);
        for (uint32_t k = 0; k < np; ++k) {
            room.perimeter[k] = r.u16();
            room.neighbours[k] = r.u16();
        }
        room.words.resize(na);
        for (auto& w : room.words) w = r.u16();
    }
    if (!r.need(size_t(nr) * 2)) { err = "room flags truncated"; return false; }
    for (uint32_t i = 0; i < nr; ++i) {
        uint8_t f = r.u8();
        if (i < p.rooms.size()) p.rooms[i].flags = f;
    }
    for (uint32_t i = 0; i < nr; ++i) {
        uint8_t f = r.u8();
        if (i < p.rooms.size()) p.rooms[i].propRule = f;
    }
    auto v3 = [&]() {
        vec3 v;
        v.x = r.f32();
        v.y = r.f32();
        v.z = r.f32();
        return v;
    };
    p.boundsMin = v3();
    p.boundsMax = v3();
    p.refPoint = v3();
    p.radius = r.f32();
    if (!r.ok()) { err = "bounds truncated"; return false; }

    uint32_t roads = r.u32();
    if (!r.ok() || roads > 100000) { err = "bad AI road count"; return false; }
    p.aiRoads.reserve(roads);
    for (uint32_t i = 0; i < roads; ++i) {
        PsdlAiRoad road;
        road.flags = r.u32();
        road.lanesA = r.u8();
        road.lanesB = r.u8();
        road.lanes.resize(size_t(road.lanesA) + road.lanesB);
        for (auto& l : road.lanes) l = r.u32();
        r.u16();
        for (int k = 0; k < 4; ++k) r.f32();
        uint8_t n = r.u8();
        road.rooms.resize(n);
        for (auto& rm : road.rooms) rm = int16_t(r.u16());
        if (!r.ok()) { err = strf("AI road %u truncated", i); return false; }
        p.aiRoads.push_back(std::move(road));
    }
    return true;
}

}  // namespace cc
