#include "city/city_data.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "ai/road_network.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/banger.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

float maxAxisScale(const mat4& m) {
    float sx = length(vec3(m.m[0], m.m[1], m.m[2]));
    float sy = length(vec3(m.m[4], m.m[5], m.m[6]));
    float sz = length(vec3(m.m[8], m.m[9], m.m[10]));
    return std::max({sx, sy, sz});
}

// Element header walk for the first one or two elements of a room.
struct ElemHead {
    int type = -1;
    int n = 0;
    size_t data = 0;
    int textureValue = -1;
};

bool readElem(const std::vector<uint16_t>& w, size_t& pos, ElemHead& e) {
    if (pos >= w.size()) return false;
    const uint16_t h = w[pos++];
    const int s = h & 7;
    e.type = (h >> 3) & 15;
    if (e.type == 10) {
        if (s == 0) {
            if (pos >= w.size()) return false;
            uint16_t v = w[pos++];
            e.textureValue = v;
            if (v & 0x0700) ++pos;
        } else {
            if (pos >= w.size()) return false;
            e.textureValue = (s << 8) | w[pos++];
        }
        return true;
    }
    int n = s;
    if (n == 0) {
        if (pos >= w.size()) return false;
        n = w[pos++];
    }
    e.n = n;
    e.data = pos;
    return true;
}

}  // namespace

bool CityData::load(const std::string& name, const Vfs& vfs, AssetCache& assets, std::string& message,
                    const std::string& raceDir, const LevelSet& levelSet) {
    name_ = name;
    levelSet_ = levelSet;
    raceDir_ = raceDir.empty() ? name : raceDir;
    const std::string base = "city/" + name;
    BytesPtr b = vfs.open(base + ".psdl");
    std::string err;
    if (!b) {
        message = base + ".psdl not found";
        return false;
    }
    if (!parsePsdl(b->data(), b->size(), psdl_, err)) {
        logFile(LogLevel::Error, vfs.storedName(base + ".psdl"), "city geometry broken: " + err);
        message = "the city's .psdl is broken: " + err;
        return false;
    }
    for (int i = 0; i < 16; ++i) {
        BytesPtr lt = vfs.open(strf("%s.lt%02d", base.c_str(), i));
        haveLights_[i] = lt && parseLightFile(textOf(lt), lights_[i]);
        if (lt && !haveLights_[i]) logFile(LogLevel::Warn, strf("%s.lt%02d", base.c_str(), i), "lighting file incomplete");
    }
    fog_ = parseFogCsv(textOf(vfs.open(base + "_fog.csv")));
    if (fog_.size() < 16) logFile(LogLevel::Warn, base + "_fog.csv", strf("%zu fog rows (16 expected)", fog_.size()));
    haveSky_ = parseSky(textOf(vfs.open(base + ".sky")), sky_);
    if (!haveSky_) logFile(LogLevel::Warn, base + ".sky", "no sky file");
    BytesPtr lm = vfs.open(base + ".lmap");
    if (lm && parseLmap(lm->data(), lm->size(), lmap_) && lmap_.size() != psdl_.rooms.size()) {
        logInfo("%s.lmap has %zu colours for %zu rooms: not used (all rooms white)", base.c_str(), lmap_.size(),
                psdl_.rooms.size());
        lmap_.clear();
    }

    buildRooms(vfs);
    loadPvs(vfs);

    objects_.clear();
    decals_.clear();
    spStats_ = StreetPropStats();
    placeObjects(vfs, assets, base + ".inst");
    placeObjects(vfs, assets, base + "_ai.inst");
    // The street props (created before every .inst object in MM2: their creation keys say so),
    // the props.pathset props and the decals (street-props.md, "Creation order").
    generateStreetProps(vfs, assets);
    placePathsetProps(vfs, assets);
    loadDecals(vfs);
    // The race props (races.md, "Path sets of a race"): race/<c>/<race>.pathset with the session's
    // race name, in every mode (no roam.pathset ships); a missing file is no error.
    if (!levelSet.race.empty()) {
        const std::string rp = "race/" + raceDir_ + "/" + levelSet.race + ".pathset";
        if (vfs.exists(rp)) placePathsetProps(vfs, assets, rp);
    }
    // The moving objects' files: always the plain ones in Cruise (level-objects.md, "Which file").
    const std::string objRace = iequals(levelSet.race, "roam") ? std::string() : levelSet.race;
    // The level objects (level-objects.md, "Creation order"; Cruise files): their draws continue
    // from the street props' final state after the player car's 192 ("Randomness").
    {
        Lcg g(streetRngState_);
        for (int i = 0; i < 192; ++i) g.next15();
        gizmoRngStart_ = g.state();
        levelObjectsCreated_ = (long long)objects_.size();
        levelObjects_.create(vfs, assets, *this, g, objRace, levelSet.ferries, levelSet.openBridges);
        if (levelSet.parkedCars) placeParkedCars(vfs, assets, g, objRace);
        gizmoRngEnd_ = g.state();
    }

    // The AI network exists only if city/<MapName>.bai does; it is read as city/<RaceDir>.bai.
    const bool aiWanted = vfs.exists(base + ".bai");
    BytesPtr bai = aiWanted ? vfs.open("city/" + raceDir_ + ".bai") : nullptr;
    haveBai_ = false;
    if (bai) {
        if (parseBai(bai->data(), bai->size(), bai_, err)) haveBai_ = true;
        else logFile(LogLevel::Warn, vfs.storedName(base + ".bai"), "AI network unreadable: " + err);
    }
    findStarts();
    // The AI network (bai.md) with its traffic-light poles, for Cruise ("roam").
    network_ = aiWanted ? std::make_shared<ai::RoadNetwork>() : nullptr;
    if (network_) {
        std::string nm;
        if (!network_->load(vfs, assets, *this, "roam", nm)) network_.reset();
        else logInfo("city %s: AI network: %s", name.c_str(), nm.c_str());
    }
    // Props are modelled around their centre of gravity: their meshes are drawn at the prop's
    // position, placement + R·CG (collision-objects.md), with the stored rotation.
    {
        std::map<std::string, BangerData> cgs;
        for (size_t i = 0; i < objects_.size(); ++i) {
            CityObject& o = objects_[i];
            o.drawWorld = o.world;
            if (o.kind != CityObjectKind::Breakable) continue;
            auto it = cgs.find(o.name);
            if (it == cgs.end()) {
                BangerData b;
                loadBangerData(vfs, o.name, b);
                it = cgs.emplace(o.name, b).first;
            }
            // BillFlags; 0x200 (tree) is also set for every type whose name contains "_tree".
            o.billFlags = it->second.billFlags | (o.name.find("_tree") != std::string::npos ? 0x200 : 0);
            const vec3 p = propPosition(int(i), it->second.cg);
            o.drawWorld.m[12] = p.x;
            o.drawWorld.m[13] = p.y;
            o.drawWorld.m[14] = p.z;
        }
    }
    message = strf("%zu rooms, %zu objects (%d street props, %d path-set props, %zu decals)", psdl_.rooms.size() - 1,
                   objects_.size(), spStats_.streetProps, spStats_.pathsetProps, decals_.size());
    return true;
}

// cpvs.md, "File layout": PVS0, the table size C, C − 1 row offsets, then the row data. A table
// without a row for every room is treated as missing.
void CityData::loadPvs(const Vfs& vfs) {
    pvsValid_ = false;
    pvsOffsets_.clear();
    pvsData_.clear();
    BytesPtr b = vfs.open("city/" + name_ + ".cpvs");
    if (!b || b->size() < 8 || std::memcmp(b->data(), "PVS0", 4) != 0) return;
    const uint8_t* d = b->data();
    auto u32 = [&](size_t at) { return uint32_t(d[at]) | uint32_t(d[at + 1]) << 8 | uint32_t(d[at + 2]) << 16 | uint32_t(d[at + 3]) << 24; };
    const uint32_t C = u32(4);
    const size_t NR = psdl_.rooms.size();
    if (C < NR + 1 || size_t(C) * 4 + 4 > b->size()) {
        logFile(LogLevel::Warn, vfs.storedName("city/" + name_ + ".cpvs"), "room visibility table does not fit the city; not used");
        return;
    }
    pvsOffsets_.resize(C - 1);
    for (uint32_t i = 0; i + 1 < C; ++i) pvsOffsets_[i] = u32(8 + size_t(i) * 4);
    pvsData_.assign(d + size_t(C) * 4 + 4, d + b->size());
    pvsValid_ = true;
}

void CityData::pvsRow(int room, std::vector<uint8_t>& out) const {
    out.clear();
    const size_t B = (psdl_.rooms.size() + 3) / 4;
    if (pvsValid_ && room >= 1 && size_t(room) < pvsOffsets_.size()) {
        size_t i = pvsOffsets_[size_t(room) - 1];
        const size_t end = pvsOffsets_[size_t(room)];
        while (i < end && i < pvsData_.size() && out.size() < 512) {
            const uint8_t c = pvsData_[i];
            if (c < 0x80) {
                const uint8_t v = i + 1 < pvsData_.size() ? pvsData_[i + 1] : 0;
                out.insert(out.end(), c, v);
                i += 2;
            } else {
                const size_t n = size_t(c) - 127;  // codes are used whole, even past the row's end
                for (size_t k = 1; k <= n; ++k) out.push_back(i + k < pvsData_.size() ? pvsData_[i + k] : 0);
                i += n + 1;
            }
        }
    }
    out.resize(B, 0);
}

void CityData::buildRooms(const Vfs& vfs) {
    rooms_.assign(psdl_.rooms.size(), CityRoom());
    stackedRooms_.clear();

    // Deep water by texture material: the first material in city/materials.mtl (inst.md, psdl.md).
    std::string deepMaterial;
    {
        std::string mtl = textOf(vfs.open("city/materials.mtl"));
        size_t p = mtl.find("mtl");
        while (p != std::string::npos && p > 0 && !std::isspace(static_cast<unsigned char>(mtl[p - 1])))
            p = mtl.find("mtl", p + 3);
        if (p != std::string::npos) {
            size_t a = p + 3;
            while (a < mtl.size() && std::isspace(static_cast<unsigned char>(mtl[a]))) ++a;
            size_t e = a;
            while (e < mtl.size() && !std::isspace(static_cast<unsigned char>(mtl[e])) && mtl[e] != '{') ++e;
            deepMaterial = mtl.substr(a, e - a);
        }
    }
    std::map<std::string, std::string> texMaterial;
    {
        auto lines = split(textOf(vfs.open("city/materials.csv")), '\n');
        for (size_t i = 1; i < lines.size(); ++i) {
            auto f = split(trim(lines[i]), ',');
            if (f.size() >= 2) texMaterial[toLower(trim(f[0]))] = trim(f[1]);
        }
    }
    WaterInfo water;
    parseWater(textOf(vfs.open("city/" + name_ + ".water")), water);

    for (size_t r = 1; r < psdl_.rooms.size(); ++r) {
        const PsdlRoom& src = psdl_.rooms[r];
        CityRoom& room = rooms_[r];
        room.flags = src.flags;
        for (uint16_t nb : src.neighbours)
            if (nb != 0 && std::find(room.neighbours.begin(), room.neighbours.end(), int(nb)) == room.neighbours.end())
                room.neighbours.push_back(int(nb));
        for (uint16_t i : src.perimeter)
            if (i < psdl_.points.size()) {
                room.outline.push_back(psdl_.points[i]);
                room.pointIdx.push_back(i);
            }
        if (room.outline.empty()) continue;
        room.bmin = room.bmax = room.outline[0];
        for (const vec3& p : room.outline) {
            room.bmin = vmin(room.bmin, p);
            room.bmax = vmax(room.bmax, p);
        }
        // The room bounding sphere (psdl.md, "Room bounding sphere"), float32 in MM2's order.
        {
            const std::vector<uint16_t>& q = src.perimeter;
            const std::vector<vec3>& V = psdl_.points;
            const size_t N = q.size();
            bool valid = N > 0;
            for (uint16_t idx : q)
                if (idx >= V.size()) valid = false;
            if (valid) {
                float A = 0.0f, Sx = 0.0f, Sy = 0.0f, Sz = 0.0f;
                int k = 0;
                size_t i = 0;
                uint16_t prev = q[N - 1];
                while (i < N) {
                    if (q[i] == prev) {
                        if (i == N - 1) break;
                        i = i + 1;
                    }
                    k = k + 1;
                    const uint16_t cur = q[i];
                    i = i + 1;
                    const vec3& P = V[cur];
                    const vec3& Q = V[prev];
                    const float w = (P.z * Q.x) - (P.x * Q.z);
                    A = A + w;
                    Sx = Sx + (P.x + Q.x) * w;
                    Sy = Sy + Q.y;
                    Sz = Sz + (P.z + Q.z) * w;
                    prev = cur;
                }
                if (A == 0.0f) {
                    room.centre = V[q[0]];
                } else {
                    const float f = 1.0f / (A * 3.0f);
                    room.centre = vec3(f * Sx, Sy / float(k), f * Sz);
                }
                float r2 = 0.0f;
                for (uint16_t idx : q) {
                    const vec3 d(V[idx].x - room.centre.x, V[idx].y - room.centre.y, V[idx].z - room.centre.z);
                    const float x = (d.z * d.z + d.y * d.y) + d.x * d.x;
                    if (x > r2) r2 = x;
                }
                room.radius = std::sqrt(r2);
            }
        }
        float ymin = 1e30f, ymax = -1e30f;
        for (const vec3& p : room.outline) {
            ymin = std::min(ymin, p.y);
            ymax = std::max(ymax, p.y);
        }
        // Vertical range.
        size_t pos = 0;
        ElemHead first;
        bool haveFirst = readElem(src.words, pos, first);
        if (!(src.flags & PsdlEnclosed)) {
            room.yLo = ymin - 1.0f;
            room.yHi = 1000.0f;
        } else {
            float clearance = 7.0f;
            if (!(src.flags & PsdlBlock) && haveFirst) {
                ElemHead e = first;
                if (e.type == 10) {
                    if (!readElem(src.words, pos, e)) e.type = -1;
                }
                if (e.type == 9) {
                    if (e.n == 10 && e.data + 1 < src.words.size()) clearance = float(src.words[e.data + 1]) / 256.0f;
                    else if (e.data + 2 < src.words.size()) clearance = float(src.words[e.data + 2]) / 256.0f;
                }
            }
            room.yLo = -1000.0f;
            room.yHi = ymax + clearance;
        }
        if (src.flags & PsdlStacked) stackedRooms_.push_back(int(r));
        // Deep water: first element is a texture whose material is the deep-water one.
        if (haveFirst && first.type == 10 && !((src.flags & PsdlEnclosed) && (src.flags & PsdlBlock))) {
            int t = first.textureValue;
            if (t > 0 && size_t(t) < psdl_.textures.size()) {
                auto it = texMaterial.find(toLower(Psdl::lookupName(psdl_.textures[size_t(t)])));
                if (it != texMaterial.end() && !deepMaterial.empty() && it->second == deepMaterial) room.deepWater = true;
            }
        }
    }
    for (int r : water.rooms)
        if (r > 0 && size_t(r) < rooms_.size()) rooms_[size_t(r)].deepWater = true;
    if (!water.rooms.empty() || water.level != 0.0f) waterLevel_ = water.level;
    // Extra rooms bit (city-collision.md): only in cities whose name contains "sf".
    if (name_.find("sf") != std::string::npos)
        for (int r : {411, 412, 423, 625})
            if (size_t(r) < rooms_.size()) rooms_[size_t(r)].extraRooms = true;
    buildRoomGrid();
    buildOverlaps();
}

void CityData::buildRoomGrid() {
    // The game's 64 × 64 bucket grid over the box of all rooms' perimeter points
    // (city-collision.md, "Finding the room of a point", step 3), in single precision.
    grid_.clear();
    bool first = true;
    vec3 mn, mx;
    for (size_t r = 1; r < rooms_.size(); ++r) {
        if (rooms_[r].outline.empty()) continue;
        if (first) {
            mn = rooms_[r].bmin;
            mx = rooms_[r].bmax;
            first = false;
        } else {
            mn = vmin(mn, rooms_[r].bmin);
            mx = vmax(mx, rooms_[r].bmax);
        }
    }
    if (first) return;
    gridMin_ = mn;
    gridW_ = gridH_ = 64;
    gridSx_ = 64.0f / (mx.x - mn.x);
    gridSz_ = 64.0f / (mx.z - mn.z);
    grid_.assign(64 * 64, {});
    auto lo = [](float v) { return std::isfinite(v) && std::fabs(v) < 2.0e9f ? int(std::trunc(v)) : 0; };
    auto hi = [](float v) { return std::isfinite(v) && std::fabs(v) < 2.0e9f ? int(std::ceil(v)) - 1 : 0; };
    for (size_t r = 1; r < rooms_.size(); ++r) {
        const CityRoom& room = rooms_[r];
        if (room.outline.empty()) continue;
        int x0 = lo((room.bmin.x - mn.x) * gridSx_), x1 = hi((room.bmax.x - mn.x) * gridSx_);
        int z0 = lo((room.bmin.z - mn.z) * gridSz_), z1 = hi((room.bmax.z - mn.z) * gridSz_);
        x0 = std::clamp(x0, 0, 63);
        x1 = std::clamp(x1, 0, 63);
        z0 = std::clamp(z0, 0, 63);
        z1 = std::clamp(z1, 0, 63);
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) grid_[size_t(z) * 64 + size_t(x)].push_back(int(r));
    }
}

int CityData::bucketOf(float x, float z) const {
    if (grid_.empty()) return -1;
    const float fx = (x - gridMin_.x) * gridSx_, fz = (z - gridMin_.z) * gridSz_;
    if (!std::isfinite(fx) || !std::isfinite(fz) || std::fabs(fx) >= 2.0e9f || std::fabs(fz) >= 2.0e9f) return -1;
    const int ix = int(std::trunc(fx)), iz = int(std::trunc(fz));
    if (ix < 0 || iz < 0 || ix > 63 || iz > 63) return -1;
    return iz * 64 + ix;
}

void CityData::buildOverlaps() {
    for (auto& r : rooms_) r.overlaps.clear();
    static const float kF[5] = {0.33f, 0.66f, 0.5f, 0.16f, 0.86f};
    auto addTo = [&](int x, int sroom) {
        auto& l = rooms_[size_t(x)].overlaps;
        for (int v : l)
            if (v == sroom) return;
        l.push_back(sroom);
    };
    for (size_t sr = 1; sr < rooms_.size(); ++sr) {
        const CityRoom& S = rooms_[sr];
        if (!(S.flags & PsdlStacked) || S.outline.empty()) continue;
        const size_t n = S.outline.size();
        for (size_t i = 0; i < n; ++i) {
            const vec3& A = S.outline[i];
            const vec3& B = S.outline[(i + 1) % n];
            vec3 pts[7];
            pts[0] = A;
            pts[1] = B;
            for (int k = 0; k < 5; ++k)
                pts[2 + k] = vec3((B.x - A.x) * kF[k] + A.x, 0.0f, (B.z - A.z) * kF[k] + A.z);
            for (const vec3& p : pts) {
                // Every room without bit 0x40 that contains the point (a box test with a margin
                // well above rounding keeps it fast without changing the result).
                for (size_t x = 1; x < rooms_.size(); ++x) {
                    const CityRoom& X = rooms_[x];
                    if ((X.flags & PsdlStacked) || X.outline.empty()) continue;
                    if (p.x < X.bmin.x - 0.5f || p.x > X.bmax.x + 0.5f || p.z < X.bmin.z - 0.5f || p.z > X.bmax.z + 0.5f)
                        continue;
                    if (flatInRoom(int(x), p.x, p.z)) addTo(int(x), int(sr));
                }
            }
        }
    }
    if (name_.find("london") != std::string::npos && rooms_.size() > 1297)
        for (int x : {597, 599, 600, 1297}) addTo(x, 382);
}

bool CityData::flatInRoom(int room, float x, float z) const {
    if (room <= 0 || size_t(room) >= rooms_.size()) return false;
    const CityRoom& r = rooms_[size_t(room)];
    const size_t n = r.outline.size();
    if (n == 0) return false;
    int count = 0;
    for (size_t i = 0; i < n; ++i) {
        const size_t pi = i == 0 ? n - 1 : i - 1;  // previous -> current, starting with last -> first
        if (r.pointIdx[pi] == r.pointIdx[i]) continue;
        const vec3& prev = r.outline[pi];
        const vec3& cur = r.outline[i];
        if ((prev.z <= z && z < cur.z) || (cur.z <= z && z < prev.z)) {
            const float xc = (prev.x - cur.x) * (z - cur.z) / (prev.z - cur.z) + cur.x;
            if (xc > x) ++count;
        }
    }
    return (count & 1) != 0;
}

bool CityData::roomContains(int room, const vec3& p) const {
    if (!flatInRoom(room, p.x, p.z)) return false;
    const CityRoom& r = rooms_[size_t(room)];
    if (r.flags & PsdlStacked) return r.yLo <= p.y && p.y <= r.yHi;
    for (int s : r.overlaps) {
        const CityRoom& o = rooms_[size_t(s)];
        if (o.yLo <= p.y && p.y <= o.yHi && flatInRoom(s, p.x, p.z)) return false;
    }
    return true;
}

int CityData::findRoom(const vec3& p, int hint) const {
    if (hint > 0 && size_t(hint) < rooms_.size()) {
        if (roomContains(hint, p)) return hint;
        int best = 0;
        for (uint16_t nb : psdl_.rooms[size_t(hint)].neighbours)
            if (nb > best && roomContains(nb, p)) best = nb;
        if (best) return best;
    }
    const int bi = bucketOf(p.x, p.z);
    if (bi < 0) return 0;
    const auto& bucket = grid_[size_t(bi)];
    for (size_t i = bucket.size(); i-- > 0;)
        if (roomContains(bucket[i], p)) return bucket[i];
    return 0;
}

std::vector<int> CityData::touched(int room, const vec3& c, float radius, int maxRooms) const {
    std::vector<int> out;
    if (room <= 0 || size_t(room) >= rooms_.size()) return out;
    const PsdlRoom& src = psdl_.rooms[size_t(room)];
    const size_t n = src.perimeter.size();
    for (size_t i = 0; i < n && int(out.size()) < maxRooms; ++i) {
        const int N = src.neighbours[i];
        if (N == 0 || size_t(N) >= rooms_.size()) continue;
        bool dup = false;
        for (int v : out)
            if (v == N) dup = true;
        if (dup) continue;
        const uint16_t ia = src.perimeter[i], ib = src.perimeter[(i + 1) % n];
        if (ia >= psdl_.points.size() || ib >= psdl_.points.size()) continue;
        const vec3& Pa = psdl_.points[ia];
        const vec3& Pb = psdl_.points[ib];
        if (rooms_[size_t(N)].flags & PsdlAlwaysNeighbour) {
            out.push_back(N);
        } else if (ia == ib) {
            const float dx = c.x - Pa.x, dz = c.z - Pa.z;
            if (dx * dx + dz * dz < radius * radius) out.push_back(N);  // compared squared
        } else {
            const float r2 = radius * radius;
            const float ax = Pa.x - c.x, az = Pa.z - c.z, bx = Pb.x - c.x, bz = Pb.z - c.z;
            const float ex = Pb.x - Pa.x, ez = Pb.z - Pa.z;
            const float L2 = ez * ez + ex * ex;
            const float h = L2 + r2;
            const float T = h + h;
            // K from the differences of the already rounded a and b (city-collision.md).
            const float kx = bx - ax, kz = bz - az;
            const float K = kz * kz + kx * kx;
            const float cr = ax * bz - az * bx;
            if (ax * ax + az * az < T && bx * bx + bz * bz < T && K * r2 - cr * cr > 0.0f) out.push_back(N);
        }
    }
    return out;
}

int CityData::landmarkCount() const {
    int n = 0;
    for (const auto& o : objects_)
        if (o.kind == CityObjectKind::Landmark) ++n;
    return n;
}

void CityData::placeObjects(const Vfs& vfs, AssetCache& assets, const std::string& path) {
    BytesPtr b = vfs.open(path);
    if (!b) return;
    std::vector<InstRecord> recs;
    std::string err;
    if (!parseInst(b->data(), b->size(), recs, err))
        logFile(LogLevel::Error, vfs.storedName(path), "placements broken (" + err + "); objects after it are missing");
    int recordIndex = -1;
    for (InstRecord& r : recs) {
        ++recordIndex;
        const mat4 placement = mat4::fromGameMatrix(r.matrix);
        if (r.landmark) flattenLandmarkMatrix(r.matrix);
        if (r.landmark && r.room > 0 && size_t(r.room) < rooms_.size()) rooms_[r.room].hasLandmark = true;
        // Props without tune/banger/<name>.dgBangerData are not created at all.
        if (r.breakable && !hasBangerData(vfs, r.name)) continue;
        auto pkg = assets.package(r.name, true);
        if (!pkg) continue;
        CityObject o;
        o.name = r.name;
        o.pkg = pkg;
        o.placement = placement;
        o.variant = r.variant;
        o.instIndex = recordIndex;
        o.compact = r.compact;
        PartTable t = takeParts(*pkg, {""});
        for (int l = 0; l < LodCount; ++l) o.lod[l] = t.parts[0].at(Lod(l));
        o.world = mat4::fromGameMatrix(r.matrix);
        o.pos = o.world.translation();
        o.zAxis = vec3(o.world.m[8], o.world.m[9], o.world.m[10]);
        float rad = 0;
        for (int l = 0; l < LodCount; ++l)
            if (o.lod[l]) rad = std::max(rad, o.lod[l]->radius);
        o.modelRadius = rad;
        o.radius = rad * maxAxisScale(o.world);
        o.room = r.room;
        o.set = pkg->setCount > 0 ? r.variant % pkg->setCount : 0;
        o.noWheels = r.landmark && r.landmarkDetail;
        o.kind = r.landmark ? CityObjectKind::Landmark
                 : r.breakable ? CityObjectKind::Breakable
                 : r.spansRooms ? CityObjectKind::Spanning
                                : CityObjectKind::Static;
        if (o.kind == CityObjectKind::Breakable) o.glows = glowPoints(vfs, o.name);
        o.created = (long long)objects_.size();
        if (o.lod[LodH] || o.lod[LodVL]) objects_.push_back(o);
        if (!pkg->xrefs.empty()) addXrefs(vfs, assets, o, r.variant);
    }
}

std::vector<vec3> CityData::glowPoints(const Vfs& vfs, const std::string& name) {
    auto it = glowCache_.find(name);
    if (it != glowCache_.end()) return it->second;
    std::vector<vec3> pts;
    BangerData b;
    if (loadBangerData(vfs, name, b))
        for (const vec3& g : b.glowOffsets) pts.push_back(vec3(b.cg.x + g.x, b.cg.y + g.y, b.cg.z + g.z));
    glowCache_.emplace(name, pts);
    return pts;
}

bool CityData::hasBangerData(const Vfs& vfs, const std::string& name) {
    return vfs.exists("tune/banger/" + name + ".dgBangerData");
}

void CityData::addXrefs(const Vfs& vfs, AssetCache& assets, const CityObject& owner, uint8_t ownerVariant) {
    for (const Xref& x : owner.pkg->xrefs) {
        // Combined matrix C = P (the xref matrix) on the owner's placement, in MM2's orders
        // (collision-objects.md, "Props"): C_i.c = sum_k P_i.k * A_k.c.
        const float* P = x.matrix;
        const vec3 A[3] = {vec3(owner.world.m[0], owner.world.m[1], owner.world.m[2]),
                           vec3(owner.world.m[4], owner.world.m[5], owner.world.m[6]),
                           vec3(owner.world.m[8], owner.world.m[9], owner.world.m[10])};
        const vec3 oo(owner.world.m[12], owner.world.m[13], owner.world.m[14]);
        auto comp = [&](int row, int c) {
            const float tx = P[row * 3 + 0] * (&A[0].x)[c], ty = P[row * 3 + 1] * (&A[1].x)[c],
                        tz = P[row * 3 + 2] * (&A[2].x)[c];
            if (row == 3) return c == 0 ? ((tx + ty) + tz) + oo.x : ((ty + tz) + tx) + (&oo.x)[c];
            if (c == 0) return row == 2 ? (tx + tz) + ty : (tx + ty) + tz;
            return (ty + tz) + tx;
        };
        vec3 ax[3];
        for (int i = 0; i < 3; ++i) ax[i] = vec3(comp(i, 0), comp(i, 1), comp(i, 2));
        const vec3 T(comp(3, 0), comp(3, 1), comp(3, 2));
        bool bad = false;
        for (auto& a : ax)
            if (a.x == 0 && a.y == 0 && a.z == 0) bad = true;
        {
            // Signed dot products (a negative one never rejects), in the spec's orders.
            const vec3 &X = ax[0], &Y = ax[1], &Z = ax[2];
            const float xy = (X.x * Y.x + X.z * Y.z) + X.y * Y.y;
            const float yz = (Y.z * Z.z + Y.y * Z.y) + Y.x * Z.x;
            const float zx = (X.x * Z.x + X.z * Z.z) + X.y * Z.y;
            if (xy > 0.01f || yz > 0.01f || zx > 0.01f) bad = true;
        }
        if (bad) {
            logFile(LogLevel::Warn, "geometry/" + owner.name + ".pkg", "attached prop " + x.name + " has a bad placement; skipped");
            continue;
        }
        // Axes normalised when L is outside [0.97, 1.03], as a·(1/√L) per component, with
        // L = (z² + x²) + y² for X and (z² + y²) + x² for Y and Z (collision-objects.md).
        for (int k = 0; k < 3; ++k) {
            vec3& a = ax[k];
            const float l2 = k == 0 ? (a.z * a.z + a.x * a.x) + a.y * a.y : (a.z * a.z + a.y * a.y) + a.x * a.x;
            if (l2 < 0.97f || l2 > 1.03f) {
                const float r = 1.0f / std::sqrt(l2);
                a = vec3(a.x * r, a.y * r, a.z * r);
            }
        }
        if (!hasBangerData(vfs, x.name)) continue;  // not created without its .dgBangerData
        auto pkg = assets.package(x.name, false);
        if (!pkg) {
            logFile(LogLevel::Warn, "geometry/" + x.name + ".pkg", "attached prop of " + owner.name + " not found; skipped");
            continue;
        }
        // Keeps only its position and its turn about the vertical axis.
        float g[12] = {ax[0].x, ax[0].y, ax[0].z, ax[1].x, ax[1].y, ax[1].z, ax[2].x, ax[2].y, ax[2].z, T.x, T.y, T.z};
        const mat4 full = mat4::fromGameMatrix(g);  // C, normalised: the CG offset uses it
        flattenLandmarkMatrix(g);
        CityObject o;
        o.name = x.name;
        o.pkg = pkg;
        PartTable t = takeParts(*pkg, {""});
        for (int l = 0; l < LodCount; ++l) o.lod[l] = t.parts[0].at(Lod(l));
        o.world = mat4::fromGameMatrix(g);
        o.placement = full;
        o.pos = o.world.translation();
        o.zAxis = vec3(o.world.m[8], o.world.m[9], o.world.m[10]);
        float rad = 0;
        for (int l = 0; l < LodCount; ++l)
            if (o.lod[l]) rad = std::max(rad, o.lod[l]->radius);
        o.modelRadius = rad;
        o.radius = rad * maxAxisScale(o.world);
        o.set = pkg->setCount > 0 ? ownerVariant % pkg->setCount : 0;
        o.variant = ownerVariant;
        o.kind = CityObjectKind::Breakable;
        o.fromXref = true;
        o.compact = true;
        o.instIndex = owner.instIndex;
        o.room = findRoom(o.pos, owner.room);  // 0 = never drawn
        o.glows = glowPoints(vfs, o.name);
        o.created = (long long)objects_.size();
        if (o.lod[LodH] || o.lod[LodVL]) objects_.push_back(o);
    }
}

int CityData::addStreetProp(const Vfs& vfs, AssetCache& assets, const std::string& name, const mat4& world) {
    if (!hasBangerData(vfs, name)) return -1;
    auto pkg = assets.package(name, true);
    if (!pkg) return -1;
    BangerData bd;
    loadBangerData(vfs, name, bd);
    CityObject o;
    o.name = name;
    o.pkg = pkg;
    PartTable t = takeParts(*pkg, {""});
    for (int l = 0; l < LodCount; ++l) o.lod[l] = t.parts[0].at(Lod(l));
    if (!o.lod[LodH] && !o.lod[LodVL]) return -1;
    o.world = world;
    o.placement = world;
    o.pos = world.translation();
    o.zAxis = vec3(world.m[8], world.m[9], world.m[10]);
    float rad = 0;
    for (int l = 0; l < LodCount; ++l)
        if (o.lod[l]) rad = std::max(rad, o.lod[l]->radius);
    o.modelRadius = rad;
    o.radius = rad * maxAxisScale(world);
    o.kind = CityObjectKind::Breakable;
    o.compact = true;
    o.streetProp = true;
    o.glows = glowPoints(vfs, name);
    o.created = (long long)objects_.size();
    objects_.push_back(o);
    const int idx = int(objects_.size()) - 1;
    objects_.back().room = findRoom(propPosition(idx, bd.cg), 0);
    return idx;
}

vec3 CityData::propPosition(int object, const vec3& g) const {
    const CityObject& o = objects_[size_t(object)];
    const mat4& F = o.placement;
    const vec3 X(F.m[0], F.m[1], F.m[2]), Y(F.m[4], F.m[5], F.m[6]), Z(F.m[8], F.m[9], F.m[10]);
    const vec3 p = o.world.translation();
    return vec3(p.x + ((g.z * Z.x + g.y * Y.x) + g.x * X.x), p.y + ((g.z * Z.y + g.y * Y.y) + g.x * X.y),
                p.z + ((g.z * Z.z + g.y * Y.z) + g.x * X.z));
}

void CityData::findStarts() {
    starts_.clear();
    if (!haveBai_) return;
    for (const BaiIntersection& x : bai_.intersections) {
        if (x.index < 1) continue;  // intersection 0 is never used
        int room = x.room;
        if (room <= 0 || size_t(room) >= rooms_.size()) continue;
        const CityRoom& r = rooms_[size_t(room)];
        if ((r.flags & PsdlEnclosed) || r.deepWater || r.hasLandmark) continue;
        bool badRoad = false;
        for (uint32_t road : x.roads)
            if (bai_.flagsOfRoad(road) & 0x06) badRoad = true;
        if (badRoad) continue;
        CityStart s;
        s.intersection = x.index;
        s.room = room;
        s.pos = x.pos;
        starts_.push_back(s);
    }
    std::sort(starts_.begin(), starts_.end(),
              [](const CityStart& a, const CityStart& b) { return a.intersection < b.intersection; });
}

}  // namespace cc
