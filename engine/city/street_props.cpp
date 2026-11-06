// Street props along the PSDL's AI roads (spec/formats/street-props.md), the props.pathset props
// and the decals (spec/formats/pathset.md).
#include <algorithm>
#include <cmath>

#include "city/city_data.h"
#include "core/game_random.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/banger.h"
#include "formats/csv_table.h"
#include "formats/pathset.h"
#include "city/level_objects.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

std::string textOf(const BytesPtr& b) { return b ? std::string(b->begin(), b->end()) : std::string(); }

float maxAxis(const mat4& m) {
    float s = 0;
    for (int c = 0; c < 3; ++c) {
        const float l = std::sqrt(m.m[c * 4] * m.m[c * 4] + m.m[c * 4 + 1] * m.m[c * 4 + 1] + m.m[c * 4 + 2] * m.m[c * 4 + 2]);
        s = std::max(s, l);
    }
    return s;
}

mat4 rowsMatrix(const vec3& X, const vec3& Y, const vec3& Z, const vec3& P) {
    mat4 m = mat4::identity();
    m.m[0] = X.x, m.m[1] = X.y, m.m[2] = X.z;
    m.m[4] = Y.x, m.m[5] = Y.y, m.m[6] = Y.z;
    m.m[8] = Z.x, m.m[9] = Z.y, m.m[10] = Z.z;
    m.m[12] = P.x, m.m[13] = P.y, m.m[14] = P.z;
    return m;
}

// One element met by street-props.md's own walk of a room's element words.
struct Element {
    int type = 0, count = 0;
    size_t data = 0;  // first data word
};

// The walk of street-props.md, "Walking a room's elements". False on a low wall (MM2: load error)
// or a malformed list.
bool walkElements(const std::vector<uint16_t>& w, std::vector<Element>& out) {
    out.clear();
    size_t i = 0;
    while (i < w.size()) {
        const uint16_t h = w[i++];
        Element e;
        e.type = (h >> 3) & 15;
        int c = h & 7;
        if (c == 0) {
            if (i >= w.size()) return false;
            c = w[i++] & 0xff;
        }
        e.count = c;
        e.data = i;
        out.push_back(e);
        size_t skip = 0;
        switch (e.type) {
        case 0: skip = size_t(4 * c); break;
        case 1:
        case 2: skip = size_t(2 * c); break;
        case 4:
        case 7:
        case 9:
        case 11: skip = size_t(c); break;
        case 5:
        case 6: skip = size_t(c + 2); break;
        case 8: skip = size_t(2 + 6 * c); break;
        case 10: skip = 0; break;
        case 12: skip = size_t(c + 1); break;
        default: return false;  // 3 low wall: MM2 stops with a load error; 13-15 undefined
        }
        i += skip;
        if (h & 0x80) break;
    }
    return true;
}

struct Slot {
    const std::vector<uint16_t>* words = nullptr;
    size_t start = 0;  // first point word
    int stride = 4;
    int n = 0;
    int room = 0;
};

}  // namespace

int CityData::billFlagsOf(const Vfs& vfs, const std::string& name) {
    auto it = billFlagCache_.find(name);
    if (it != billFlagCache_.end()) return it->second;
    BangerData b;
    loadBangerData(vfs, name, b);
    const int f = b.billFlags | (name.find("_tree") != std::string::npos ? 0x200 : 0);
    billFlagCache_.emplace(name, f);
    return f;
}

bool CityData::addGeneratedProp(const Vfs& vfs, AssetCache& assets, const std::string& model, const vec3& X, const vec3& Y,
                                const vec3& Z, const vec3& P, bool turnOnly, int room, int origin, long long created) {
    if (!hasBangerData(vfs, model)) return false;
    auto pkg = assets.package(model, true);
    if (!pkg) return false;
    CityObject o;
    o.name = model;
    o.pkg = pkg;
    PartTable t = takeParts(*pkg, {""});
    for (int l = 0; l < LodCount; ++l) o.lod[l] = t.parts[0].at(Lod(l));
    if (!o.lod[LodH] && !o.lod[LodVL]) return false;
    o.placement = rowsMatrix(X, Y, Z, P);
    // Turn only: (X.x, 0, X.z), (0, 1, 0), (−X.z, 0, X.x), not re-normalised.
    o.world = turnOnly ? rowsMatrix(vec3(X.x, 0.0f, X.z), vec3(0.0f, 1.0f, 0.0f), vec3(-X.z, 0.0f, X.x), P) : o.placement;
    o.pos = P;
    o.zAxis = vec3(o.world.m[8], o.world.m[9], o.world.m[10]);
    float rad = 0;
    for (int l = 0; l < LodCount; ++l)
        if (o.lod[l]) rad = std::max(rad, o.lod[l]->radius);
    o.modelRadius = rad;
    o.radius = rad * maxAxis(o.world);
    o.room = room;
    o.set = 0;
    o.kind = CityObjectKind::Breakable;
    o.compact = turnOnly;
    o.glows = glowPoints(vfs, model);
    o.origin = origin;
    o.created = created;
    o.billFlags = billFlagsOf(vfs, model);
    objects_.push_back(o);
    return true;
}

// ---- Street props ----

void CityData::generateStreetProps(const Vfs& vfs, AssetCache& assets) {
    streetRngState_ = 1;  // the game-wide generator restarts when a city loads
    const std::string dir = "city/" + name_ + "/";
    const BytesPtr defsBytes = vfs.open(dir + "propdefs.csv");
    const BytesPtr rulesBytes = vfs.open(dir + "proprules.csv");
    spStats_.roads = int(psdl_.aiRoads.size());
    if (!defsBytes || !rulesBytes) return;  // the city gets no street props
    CsvTable defs, rules;
    parseCsvTable(textOf(defsBytes), defs);
    parseCsvTable(textOf(rulesBytes), rules);
    const int cName = defs.column("name"), cStart = defs.column("start"), cDist = defs.column("distance"),
              cMax = defs.column("maxUse"), cMinL = defs.column("minLerp"), cMaxL = defs.column("maxLerp"),
              cFile = defs.column("file1");
    const int cRule = rules.column("rulename"), cProp1 = rules.column("prop1");
    if (cName < 0 || cStart < 0 || cDist < 0 || cMax < 0 || cMinL < 0 || cMaxL < 0 || cFile < 0 || cRule < 0 || cProp1 < 0) {
        logFile(LogLevel::Error, dir + "propdefs.csv", "street prop tables lack a column; no street props");
        return;
    }
    long long seq = 0;
    const long long base = -100000000LL;  // street props come before every other object
    std::vector<Element> elems;
    for (size_t ri = 0; ri < psdl_.aiRoads.size(); ++ri) {
        const PsdlAiRoad& road = psdl_.aiRoads[ri];
        const size_t m = road.rooms.size();
        if (m == 0) continue;
        auto roomOf = [&](size_t j) { return std::abs(int(road.rooms[j])); };
        // 1. Slots.
        std::vector<Slot> slots;
        bool bad = false;
        for (size_t j = 0; j < m && slots.size() < m && !bad; ++j) {
            const int R = roomOf(j);
            if (R <= 0 || size_t(R) >= psdl_.rooms.size()) {
                bad = true;
                break;
            }
            const auto& w = psdl_.rooms[size_t(R)].words;
            if (!walkElements(w, elems)) {
                bad = true;
                break;
            }
            for (const Element& e : elems) {
                if (slots.size() >= m) break;
                if (e.type != 0 && e.type != 2 && e.type != 8) continue;
                Slot s;
                s.words = &w;
                s.start = e.data + (e.type == 8 ? 2 : 0);
                s.stride = e.type == 0 ? 4 : e.type == 2 ? 2 : 6;
                s.n = e.count;
                s.room = R;
                slots.push_back(s);
            }
        }
        if (bad || slots.size() < m || slots.size() > 100) continue;  // no sidewalks: no draws either
        // 2. The generator restarts for every road (its state after the last road is where the level
        // objects' draws continue, level-objects.md, "Randomness").
        Lcg rng(1);
        struct KeepState {
            Lcg& r;
            uint32_t& out;
            ~KeepState() { out = r.state(); }
        } keepState{rng, streetRngState_};
        // 3. The rule byte of the first room.
        const int R0 = roomOf(0);
        const int b = psdl_.rooms[size_t(R0)].propRule;
        if (b == 0) continue;
        ++spStats_.roadsWithRule;
        const CsvTable::Row* leftRule = rules.row(cRule, strf("n%02dleft", b));
        const CsvTable::Row* rightRule = rules.row(cRule, strf("n%02dright", b));
        if (!leftRule || !rightRule) {
            logFile(LogLevel::Warn, dir + "proprules.csv", strf("no rule n%02dleft/right; road %zu gets no props", b, ri));
            continue;
        }
        // Elements of type 0 or 8 in the first room trigger the walks.
        if (!walkElements(psdl_.rooms[size_t(R0)].words, elems)) continue;
        int triggers = 0;
        for (const Element& e : elems)
            if (e.type == 0 || e.type == 8) ++triggers;
        if (triggers > 0) ++spStats_.roadsWithElement;
        // Sections and their points.
        int T = slots[0].n;
        for (size_t j = 1; j < slots.size(); ++j) T += slots[j].n - 1;
        if (T < 1) continue;
        auto sectionSlot = [&](int k, int& local) -> const Slot& {
            if (k < slots[0].n) {
                local = k;
                return slots[0];
            }
            int kk = k - slots[0].n;
            for (size_t j = 1; j < slots.size(); ++j) {
                if (kk < slots[j].n - 1) {
                    local = kk + 1;
                    return slots[j];
                }
                kk -= slots[j].n - 1;
            }
            local = slots.back().n - 1;
            return slots.back();
        };
        const bool f40 = (road.flags & 0x40) != 0;
        const bool centre = (road.flags & 0x400000) != 0 && (road.flags & 0x4) == 0;
        bool pointBad = false;
        auto pointAt = [&](const Slot& s, size_t idx) {
            if (idx >= s.words->size()) {
                pointBad = true;
                return vec3();
            }
            const uint16_t pw = (*s.words)[idx];
            if (pw >= psdl_.points.size()) {
                pointBad = true;
                return vec3();
            }
            return psdl_.points[pw];
        };
        // line: 0 left outer edge, 1 left kerb, 2 right kerb, 3 right outer edge.
        auto sectionPoint = [&](int line, int k) {
            int local = 0;
            const Slot& s = sectionSlot(k, local);
            const int e = s.stride == 6 ? 2 : 0;
            const size_t base = s.start + size_t(s.stride) * size_t(local);
            static const int on[4] = {0, 1, 2, 3}, off[4] = {0, 0, 1, 1};
            const int o = f40 ? (line >= 2 ? on[line] + e : on[line]) : off[line];
            vec3 E = pointAt(s, base + size_t(o));
            if (centre && (line == 1 || line == 2)) {
                const size_t bp = s.stride == 2 ? base : base + 1;
                const vec3 A = pointAt(s, bp), B = pointAt(s, bp + 1 + size_t(e));
                const vec3 C((B.x - A.x) * 0.5f + A.x, (B.y - A.y) * 0.5f + A.y, (B.z - A.z) * 0.5f + A.z);
                E = vec3(E.x + (C.x - E.x) * 0.95f, E.y + (C.y - E.y) * 0.95f, E.z + (C.z - E.z) * 0.95f);
            }
            return E;
        };
        auto sectionRoom = [&](int k) {
            int local = 0;
            return sectionSlot(k, local).room;
        };
        // Side polylines.
        struct Poly {
            std::vector<vec3> V;
            std::vector<int> room;
        };
        auto polyline = [&](int line) {
            Poly p;
            const int N = 2 * T;
            p.V.resize(size_t(N));
            p.room.resize(size_t(N));
            std::vector<vec3> S(static_cast<size_t>(T));
            for (int k = 0; k < T; ++k) S[size_t(k)] = sectionPoint(line, k);
            p.V[0] = S[0];
            p.V[size_t(N - 1)] = S[size_t(T - 1)];
            for (int v = 1; v < N - 1; ++v) {
                const int k = v / 2;
                const vec3& A = S[size_t(k)];
                const vec3& B = (v & 1) ? S[size_t(k + 1)] : S[size_t(k - 1)];
                const vec3 d(B.x - A.x, B.y - A.y, B.z - A.z);
                const float len = std::sqrt((d.y * d.y + d.z * d.z) + d.x * d.x);
                const float L2 = (d.z * d.z + d.y * d.y) + d.x * d.x;
                const float s = L2 == 0.0f ? 0.0f : 1.0f / std::sqrt(L2);
                const vec3 u(s * d.x, s * d.y, s * d.z);
                float mm = len * 0.333f;
                const float lam = (v == 1 || v == N - 2) ? 15.0f : 0.1f;
                if (lam < mm) mm = lam;
                p.V[size_t(v)] = vec3(A.x + u.x * mm, A.y + u.y * mm, A.z + u.z * mm);
            }
            for (int v = 0; v < N; ++v) {
                const int k = v / 2;
                p.room[size_t(v)] = ((v & 1) && v > 0 && v < N - 1) ? sectionRoom(k + 1) : sectionRoom(k);
            }
            return p;
        };
        const Poly lines[4] = {polyline(0), polyline(1), polyline(2), polyline(3)};
        if (pointBad) continue;
        auto pointAlong = [](const Poly& p, float s, vec3& out, int& room) {
            const size_t N = p.V.size();
            for (size_t i = 0; i + 1 < N; ++i) {
                const vec3& a = p.V[i];
                const vec3& q = p.V[i + 1];
                const vec3 d(a.x - q.x, a.y - q.y, a.z - q.z);
                const float len = std::sqrt((d.z * d.z + d.y * d.y) + d.x * d.x);
                if (!(s > len)) {
                    const float t = s / len;
                    out = vec3((q.x - a.x) * t + a.x, (q.y - a.y) * t + a.y, (q.z - a.z) * t + a.z);
                    room = p.room[i];
                    return true;
                }
                s = s - len;
            }
            return false;
        };
        // 4. The entries of the left rule, then of the right rule.
        for (int ruleSide = 0; ruleSide < 2; ++ruleSide) {
            const CsvTable::Row& rule = ruleSide == 0 ? *leftRule : *rightRule;
            for (int col = cProp1; rule.present(col); ++col) {
                const std::string& entry = rule.cell(col);
                if (entry.empty()) continue;
                const CsvTable::Row* d = defs.row(cName, entry);
                if (!d) {
                    logFile(LogLevel::Warn, dir + "propdefs.csv", "no prop type " + entry);
                    continue;
                }
                const float s0 = csvFloat(d->cell(cStart)), delta = csvFloat(d->cell(cDist));
                int M = csvInt(d->cell(cMax));
                const float a = csvFloat(d->cell(cMinL)), bb = csvFloat(d->cell(cMaxL));
                int count = 1;
                if (d->present(cFile + 3)) count = 4;
                else if (d->present(cFile + 2)) count = 3;
                else if (d->present(cFile + 1)) count = 2;
                for (int tr = 0; tr < triggers; ++tr)
                    for (int side = 0; side < 2; ++side) {
                        const Poly& kerb = lines[side == 0 ? 1 : 2];
                        const Poly& outer = lines[side == 0 ? 0 : 3];
                        float s = s0;
                        for (int guard = 0; guard < 1000000; ++guard) {
                            vec3 K, O;
                            int kr = 0, rho = 0;
                            if (!pointAlong(kerb, s, K, kr)) break;
                            if (!pointAlong(outer, s, O, rho)) break;
                            const float r1 = rng.unit();
                            const float t = (bb - a) * r1 + a;
                            const vec3 pos((O.x - K.x) * t + K.x, (O.y - K.y) * t + K.y, (O.z - K.z) * t + K.z);
                            const vec3 P(pos.x, pos.y + 0.15f, pos.z);
                            vec3 X(O.x - K.x, O.y - K.y, O.z - K.z);
                            const float L2 = (X.z * X.z + X.y * X.y) + X.x * X.x;
                            const float k = L2 == 0.0f ? 0.0f : 1.0f / std::sqrt(L2);
                            X = vec3(k * X.x, k * X.y, k * X.z);
                            const vec3 Y(0.0f, 1.0f, 0.0f);
                            const vec3 Z(Y.z * X.y - Y.y * X.z, X.z * Y.x - Y.z * X.x, Y.y * X.x - X.y * Y.x);
                            auto zero = [](const vec3& v) {
                                auto z = [](float f) { return f == 0.0f || std::isnan(f); };
                                return z(v.x) && z(v.y) && z(v.z);
                            };
                            if (!zero(X) && !zero(Z) && side == ruleSide && M != 0) {
                                M = M - 1;
                                const int x = rng.next15();
                                const int cellCol = cFile + x % count;
                                if (d->present(cellCol) && !d->cell(cellCol).empty()) {
                                    if (addGeneratedProp(vfs, assets, d->cell(cellCol), X, Y, Z, P, true, rho, 1, base + seq)) {
                                        ++seq;
                                        ++spStats_.streetProps;
                                        ++spStats_.byType[entry];
                                    }
                                }
                            }
                            const float r2 = rng.unit();
                            s = ((delta - delta) * r2 + delta) + s;
                            if (!(delta > 0.0f)) break;  // a zero spacing would never end (MM2 hangs)
                        }
                    }
            }
        }
    }
}

// ---- props.pathset ----

void CityData::placePathsetProps(const Vfs& vfs, AssetCache& assets, const std::string& file) {
    const std::string path = file.empty() ? "city/" + name_ + "/props.pathset" : file;
    BytesPtr b = vfs.open(path);
    if (!b) return;
    std::vector<PathsetPath> paths;
    std::string err;
    if (!parsePathset(b->data(), b->size(), paths, err)) {
        logFile(LogLevel::Warn, vfs.storedName(path), err);
        return;
    }
    for (const PathsetPath& p : paths)
        for (const PathPlacement& pl : pathPlacements(p)) {
            ++spStats_.pathsetPlaced;
            const int room = findRoom(pl.P, 0);
            if (addGeneratedProp(vfs, assets, p.name, pl.X, pl.Y, pl.Z, pl.P, !pl.fullMatrix, room, 2,
                                 (long long)objects_.size())) {
                ++spStats_.pathsetProps;
                if (pl.fullMatrix) ++spStats_.pathsetFull;
            }
        }
}

// ---- Parked cars (level-objects.md, "Parked cars") ----

void CityData::placeParkedCars(const Vfs& vfs, AssetCache& assets, Lcg& rng, const std::string& race) {
    parked_ = ParkedStats();
    const std::string path = LevelObjects::pathsetFor(vfs, raceDir_, "parkedcar", race);
    if (path.empty()) return;
    BytesPtr b = vfs.open(path);
    if (!b) return;
    std::vector<PathsetPath> paths;
    std::string err;
    if (!parsePathset(b->data(), b->size(), paths, err)) {
        logFile(LogLevel::Warn, vfs.storedName(path), err);
        return;
    }
    const float c = float(std::cos(1.5707964)), s = float(std::sin(1.5707964));
    auto turn = [&](const vec3& v) { return vec3(v.x * c + v.z * s, v.y, v.x * -s + v.z * c); };
    for (const PathsetPath& p0 : paths) {
        PathsetPath p = p0;
        const float sigma = p.spacing == 0 ? 5.0f : float(p.spacing) * 0.25f;
        if (sigma < 5.0f) p.spacing = 20;  // 5 m
        for (const PathPlacement& pl : pathPlacements(p)) {
            ++parked_.slots;
            const int x = rng.next15();
            const int m = x % 3;
            if (m == 0) {
                ++parked_.empty;
                continue;
            }
            const std::string model = m == 1 ? "giz_pcar01_l" : "giz_pcar02_l";
            if (!hasBangerData(vfs, model)) continue;
            const int room = findRoom(pl.P, 0);
            const int paint = rng.next15();
            if (addGeneratedProp(vfs, assets, model, turn(pl.X), turn(pl.Y), turn(pl.Z), pl.P, false, room, 3,
                                 (long long)objects_.size())) {
                CityObject& o = objects_.back();
                o.set = o.pkg && o.pkg->setCount > 0 ? paint % o.pkg->setCount : 0;
                ++(m == 1 ? parked_.pcar01 : parked_.pcar02);
            }
        }
    }
}

// ---- decals.pathset ----

void CityData::loadDecals(const Vfs& vfs) {
    const std::string path = "city/" + name_ + "/decals.pathset";
    BytesPtr b = vfs.open(path);
    if (!b) return;
    std::vector<PathsetPath> paths;
    std::string err;
    if (!parsePathset(b->data(), b->size(), paths, err)) {
        logFile(LogLevel::Warn, vfs.storedName(path), err);
        return;
    }
    for (const PathsetPath& p : paths) {
        const auto& q = p.points;
        if (q.size() < 3) continue;
        CityDecal d;
        d.texture = p.name;
        const vec3 e(q[0].x - q[1].x, q[0].y - q[1].y, q[0].z - q[1].z);
        const float w = std::sqrt((e.z * e.z + e.y * e.y) + e.x * e.x);
        float lastV = 0;
        for (size_t i = 0; i < q.size(); ++i) {
            CityDecal::Vertex v;
            v.pos = vec3(q[i].x, q[i].y + 0.01f, q[i].z);
            v.u = (i & 1) ? 1.0f : 0.0f;
            const size_t j = i / 2;
            if (j < q.size() / 2) {
                const vec3& pj = q[2 * j];
                const vec3 dj(pj.x - q[0].x, pj.y - q[0].y, pj.z - q[0].z);
                const float len = std::sqrt((dj.z * dj.z + dj.x * dj.x) + dj.y * dj.y);
                lastV = std::floor(len / w + 0.5f);
            }
            v.v = lastV;  // an odd count's last vertex keeps the previous pair's v
            d.strip.push_back(v);
        }
        const vec3 c((q[2].x - q[0].x) * 0.5f + q[0].x, (q[2].y - q[0].y) * 0.5f + q[0].y, (q[2].z - q[0].z) * 0.5f + q[0].z);
        d.room = findRoom(c, 0);
        decals_.push_back(std::move(d));
    }
    spStats_.decals = int(decals_.size());
}

}  // namespace cc
