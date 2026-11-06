#include "formats/bai.h"

#include <cstring>

#include "core/bytes.h"
#include "core/strutil.h"

namespace cc {

uint16_t BaiNetwork::flagsOfRoad(uint32_t road) const {
    if (road < roadIndex.size() && roadIndex[road] == road) return roadFlags[road];
    for (size_t i = 0; i < roadIndex.size(); ++i)
        if (roadIndex[i] == road) return roadFlags[i];
    return 0;
}

namespace {
// One side's lane data: 5 x u16 (L1, a, b, L2, unused), then the blocks the table lists.
bool skipSide(ByteReader& r, size_t n) {
    uint16_t l1 = r.u16(), a = r.u16(), b = r.u16(), l2 = r.u16();
    r.u16();
    size_t lanes = size_t(l1) + l2;
    r.skip(lanes * n * 4);
    r.skip(lanes * 4);
    r.skip(40);
    r.skip(lanes * n * 12);
    if (a != 0) r.skip(n * 12);
    if (b != 0) r.skip(n * 12);
    r.skip(n * 24);
    return r.ok();
}
}  // namespace

bool parseBai(const uint8_t* data, size_t size, BaiNetwork& out, std::string& err) {
    out = BaiNetwork();
    ByteReader r(data, size);
    char magic[4];
    if (!r.bytes(magic, 4) || std::memcmp(magic, "CAI1", 4) != 0) {
        err = "not a CAI1 file";
        return false;
    }
    const uint16_t ni = r.u16();
    const uint16_t nr = r.u16();
    for (uint16_t i = 0; i < nr; ++i) {
        uint16_t index = r.u16();
        size_t n = r.u16();
        uint16_t flags = r.u16();
        uint16_t c = r.u16();
        r.skip(size_t(c) * 2);
        r.skip(8);
        if (!skipSide(r, n) || !skipSide(r, n)) {
            err = strf("road record %u truncated", i);
            return false;
        }
        r.skip(n * 4);
        r.skip(5 * n * 12);
        r.skip(2 * 38);
        if (!r.ok()) {
            err = strf("road record %u truncated", i);
            return false;
        }
        out.roadIndex.push_back(index);
        out.roadFlags.push_back(flags);
    }
    for (uint16_t i = 0; i < ni; ++i) {
        BaiIntersection x;
        x.index = r.u16();
        x.room = r.u16();
        x.pos.x = r.f32();
        x.pos.y = r.f32();
        x.pos.z = r.f32();
        uint16_t m = r.u16();
        if (!r.need(size_t(m) * 4)) {
            err = strf("intersection %u truncated", i);
            return false;
        }
        for (uint16_t k = 0; k < m; ++k) x.roads.push_back(r.u32());
        out.intersections.push_back(std::move(x));
    }
    if (!r.ok()) {
        err = "intersections truncated";
        return false;
    }
    return true;
}

namespace {

vec3 readVec(ByteReader& r) {
    vec3 v;
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
    return v;
}

bool readSide(ByteReader& r, int n, BaiSide& s) {
    s.lanes = r.u16();
    s.cable = r.u16() != 0;
    s.subway = r.u16() != 0;
    s.sidewalks = r.u16();
    s.flags = r.u16();
    const int lines = s.lanes + s.sidewalks;
    if (!r.need(size_t(lines) * size_t(n) * 4)) return false;
    s.distances.assign(size_t(lines), std::vector<float>(size_t(n)));
    for (auto& d : s.distances)
        for (float& v : d) v = r.f32();
    s.offsets.resize(size_t(lines));
    for (float& v : s.offsets) v = r.f32();
    for (float& v : s.band) v = r.f32();
    if (!r.need(size_t(lines) * size_t(n) * 12)) return false;
    s.lines.assign(size_t(lines), std::vector<vec3>(size_t(n)));
    for (auto& l : s.lines)
        for (vec3& p : l) p = readVec(r);
    if (s.cable) {
        s.cableLine.resize(size_t(n));
        for (vec3& p : s.cableLine) p = readVec(r);
    }
    if (s.subway) {
        s.subwayLine.resize(size_t(n));
        for (vec3& p : s.subwayLine) p = readVec(r);
    }
    if (!r.need(size_t(n) * 24)) return false;
    s.kerb.resize(size_t(n));
    for (vec3& p : s.kerb) p = readVec(r);
    s.outerEdge.resize(size_t(n));
    for (vec3& p : s.outerEdge) p = readVec(r);
    return r.ok();
}

void readEnd(ByteReader& r, BaiEnd& e) {
    e.intersection = r.u32();
    e.lightSlot = r.u16();
    e.control = r.u16();
    e.turnRule = r.u16();
    e.slot = r.u32();
    e.signPos = readVec(r);
    e.facingPoint = readVec(r);
}

bool readRoad(ByteReader& r, BaiRoad& road) {
    road.index = r.u16();
    road.n = r.u16();
    road.flags = r.u16();
    const uint16_t c = r.u16();
    if (!r.need(size_t(c) * 2)) return false;
    road.rooms.resize(c);
    for (int16_t& v : road.rooms) v = int16_t(r.u16());
    road.halfWidth = r.f32();
    road.speed = r.f32();
    const int n = road.n;
    if (n < 1 || n > 10000) return false;
    if (!readSide(r, n, road.side[0]) || !readSide(r, n, road.side[1])) return false;
    if (!r.need(size_t(n) * 4 + size_t(n) * 60 + 76)) return false;
    road.centreDist.resize(size_t(n));
    for (float& v : road.centreDist) v = r.f32();
    for (auto* a : {&road.C, &road.X, &road.Y, &road.Z, &road.T}) {
        a->resize(size_t(n));
        for (vec3& p : *a) p = readVec(r);
    }
    readEnd(r, road.end[0]);
    readEnd(r, road.end[1]);
    return r.ok();
}

}  // namespace

bool parseBaiFile(const uint8_t* data, size_t size, bool supplementary, BaiFile& out, std::string& err) {
    out = BaiFile();
    out.supplementary = supplementary;
    ByteReader r(data, size);
    char magic[4];
    if (!r.bytes(magic, 4) || std::memcmp(magic, "CAI1", 4) != 0) {
        err = "not a CAI1 file";
        return false;
    }
    uint16_t ni = 0;
    if (!supplementary) ni = r.u16();
    const uint16_t nr = r.u16();
    out.roads.resize(nr);
    for (uint16_t i = 0; i < nr; ++i) {
        if (!readRoad(r, out.roads[i])) {
            err = strf("road record %u truncated or broken", i);
            return false;
        }
    }
    if (supplementary) {
        if (!r.atEnd()) err = strf("%zu bytes after the last road", r.remaining());
        return true;
    }
    out.intersections.resize(ni);
    for (uint16_t i = 0; i < ni; ++i) {
        BaiIntersectionRec& x = out.intersections[i];
        x.index = r.u16();
        x.room = r.u16();
        x.centre = readVec(r);
        const uint16_t m = r.u16();
        if (!r.need(size_t(m) * 4)) {
            err = strf("intersection %u truncated", i);
            return false;
        }
        x.roads.resize(m);
        for (uint32_t& v : x.roads) v = r.u32();
    }
    const uint32_t nc = r.u32();
    if (!r.ok() || nc > 100000) {
        err = "activation tables missing";
        return false;
    }
    for (auto* table : {&out.ambientTable, &out.pedTable}) {
        table->resize(nc);
        for (uint32_t k = 0; k < nc; ++k) {
            const uint16_t cnt = r.u16();
            if (!r.need(size_t(cnt) * 2)) {
                err = strf("activation table truncated at room %u", k);
                return false;
            }
            auto& l = (*table)[k];
            l.resize(cnt);
            for (uint16_t& v : l) v = r.u16();
        }
    }
    if (!r.ok()) {
        err = "activation tables truncated";
        return false;
    }
    if (!r.atEnd()) err = strf("%zu bytes after the activation tables", r.remaining());
    return true;
}

}  // namespace cc
