#include "city/level_objects.h"

#include <algorithm>
#include <cmath>

#include "city/city_data.h"
#include "core/bytes.h"
#include "core/log.h"
#include "core/strutil.h"
#include "formats/pathset.h"
#include "model/asset_cache.h"
#include "model/parts.h"
#include "physics/rigid_body.h"
#include "vfs/vfs.h"

namespace cc {

namespace {

const float kOmega = 0.4712389f;  // open angle (27°)

// Approach(v, target, step) of "Movement".
float approach(float v, float target, float step) {
    if (v >= target) {
        if (v > target) v = v - step;
        if (v < target) v = target;
    } else {
        v = step + v;
        if (v > target) v = target;
    }
    return v;
}

float len3(const vec3& v) { return std::sqrt((v.z * v.z + v.y * v.y) + v.x * v.x); }

bool readPaths(const Vfs& vfs, const std::string& path, std::vector<PathsetPath>& out) {
    out.clear();
    if (path.empty()) return false;
    BytesPtr b = vfs.open(path);
    if (!b) return false;
    std::string err;
    if (!parsePathset(b->data(), b->size(), out, err)) {
        logFile(LogLevel::Warn, vfs.storedName(path), err);
        out.clear();
        return false;
    }
    return true;
}

bool packageExists(const Vfs& vfs, const std::string& name) { return !name.empty() && vfs.exists("geometry/" + name + ".pkg"); }

// The model, its levels, radius and prop data.
bool loadModel(const Vfs& vfs, AssetCache& assets, const std::string& name, LevelObject& o) {
    o.model = name;
    o.pkg = assets.package(name, true);
    if (!o.pkg) return false;
    const PartTable t = takeParts(*o.pkg, {""});
    for (int l = 0; l < LodCount; ++l) {
        o.lod[l] = t.parts[0].at(Lod(l));
        if (o.lod[l] && o.lod[l]->radius > o.radius) o.radius = o.lod[l]->radius;
    }
    o.hasData = loadBangerData(vfs, name, o.bd);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Following a path

void PathFollower::setup(int seg) {
    const int N = n();
    i = seg;
    const int h = i == 0 ? N - 1 : i - 1;
    const int j = i + 1 == N ? 0 : i + 1;
    const int k = j == N - 1 ? 0 : j + 1;
    const vec3 P0 = p[size_t(i)], P1 = p[size_t(j)];
    const vec3& Ph = p[size_t(h)];
    const vec3& Pk = p[size_t(k)];
    const vec3 T0((P1.x - Ph.x) * 0.5f, (P1.y - Ph.y) * 0.5f, (P1.z - Ph.z) * 0.5f);
    const vec3 T1((Pk.x - P0.x) * 0.5f, (Pk.y - P0.y) * 0.5f, (Pk.z - P0.z) * 0.5f);
    a = vec3(((P1.x * -2.0f + T0.x) + T1.x) + P0.x * 2.0f, ((P1.y * -2.0f + T0.y) + T1.y) + P0.y * 2.0f,
             ((T1.z + T0.z) + P1.z * -2.0f) + P0.z * 2.0f);
    b = vec3(((-T1.x + T0.x * -2.0f) + P1.x * 3.0f) + P0.x * -3.0f, ((-T1.y + T0.y * -2.0f) + P1.y * 3.0f) + P0.y * -3.0f,
             ((-T1.z + T0.z * -2.0f) + P1.z * 3.0f) + P0.z * -3.0f);
    c = T0;
    d = P0;
    const vec3 M = pointAt(0.5f);
    L = len3(vec3(M.x - P1.x, M.y - P1.y, M.z - P1.z)) + len3(vec3(P0.x - M.x, P0.y - M.y, P0.z - M.z));
}

void PathFollower::restart() {
    t = 0.0f;
    setup(0);
}

vec3 PathFollower::pointAt(float u) const {
    return vec3(((u * a.x + b.x) * u + c.x) * u + d.x, ((u * a.y + b.y) * u + c.y) * u + d.y,
                ((u * a.z + b.z) * u + c.z) * u + d.z);
}

vec3 PathFollower::directionAt(float u) const {
    const float uu3 = (u * u) * 3.0f;
    return vec3(((u + u) * b.x + uu3 * a.x) + c.x, ((u + u) * b.y + uu3 * a.y) + c.y, ((u + u) * b.z + uu3 * a.z) + c.z);
}

void PathFollower::update(float tau, vec3& point, vec3& dir) {
    t = tau + t;
    float u = (s * t) / L;
    if (n() == 2) {
        point = p[0];
        dir = vec3(p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z);
        return;
    }
    if (u > 1.0f) {
        setup(i + 1 == n() ? 0 : i + 1);
        u = u - 1.0f;
        t = (u * L) / s;
    }
    if (u < 0.0f) {
        setup(i == 0 ? n() - 1 : i - 1);
        u = u + 1.0f;
        t = (u * L) / s;
    }
    point = pointAt(u);
    dir = directionAt(u);
}

void matrixFromDirection(const vec3& D, vec3& X, vec3& Y, vec3& Z) {
    const vec3 Y0(0.0f, 1.0f, 0.0f);
    const vec3& Zd = D;
    // X′ = normalise(Y × Z), squared length (z² + y²) + x².
    vec3 x(Zd.z * Y0.y - Zd.y * Y0.z, Zd.x * Y0.z - Zd.z * Y0.x, Zd.y * Y0.x - Y0.y * Zd.x);
    float l2 = (x.z * x.z + x.y * x.y) + x.x * x.x;
    float k = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    X = vec3(x.x * k, x.y * k, x.z * k);
    // Y′ = normalise(Z × X′), (x² + y²) + z².
    vec3 y(X.z * Zd.y - X.y * Zd.z, X.x * Zd.z - X.z * Zd.x, X.y * Zd.x - X.x * Zd.y);
    l2 = (y.x * y.x + y.y * y.y) + y.z * y.z;
    k = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    Y = vec3(y.x * k, y.y * k, y.z * k);
    l2 = (Zd.x * Zd.x + Zd.y * Zd.y) + Zd.z * Zd.z;
    k = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
    Z = vec3(Zd.x * k, Zd.y * k, Zd.z * k);
}

// ---------------------------------------------------------------------------------------------
// Files

std::string LevelObjects::pathsetFor(const Vfs& vfs, const std::string& raceDir, const std::string& word,
                                     const std::string& race) {
    const std::string dir = "race/" + raceDir + "/";
    if (!race.empty()) {
        const std::string p = dir + raceDir + "_" + word + "_" + race + ".pathset";
        if (vfs.exists(p)) return p;
    }
    const std::string p = dir + raceDir + "_" + word + ".pathset";
    return vfs.exists(p) ? p : std::string();
}

bool LevelObjects::isBridgeRoom(int room) const {
    return room > 0 && std::find(bridgeRooms_.begin(), bridgeRooms_.end(), room) != bridgeRooms_.end();
}

void LevelObjects::clear() {
    objs_.clear();
    boats_.clear();
    leaves_.clear();
    trains_.clear();
    ferries_.clear();
    bridgeRooms_.clear();
    stats_ = Stats();
}

void LevelObjects::placeFollowing(LevelObject& o, const vec3& point, const vec3& dir, bool turnOnly) {
    vec3 X, Y, Z;
    matrixFromDirection(dir, X, Y, Z);
    // The lift: the prop data's CG.y above the curve.
    o.o = vec3(point.x, point.y + o.bd.cg.y, point.z);
    if (turnOnly) {
        o.X = vec3(X.x, 0.0f, X.z);
        o.Y = vec3(0.0f, 1.0f, 0.0f);
        o.Z = vec3(-X.z, 0.0f, X.x);
    } else {
        o.X = X;
        o.Y = Y;
        o.Z = Z;
    }
    o.ref = o.o;
}

void LevelObjects::create(const Vfs& vfs, AssetCache& assets, const CityData& city, Lcg& rng, const std::string& race,
                          bool ferries, bool openBridges) {
    clear();
    const std::string& rd = city.raceDir();
    std::vector<PathsetPath> paths;
    // Boats.
    if (readPaths(vfs, pathsetFor(vfs, rd, "sailboat", race), paths))
        for (const PathsetPath& p : paths) {
            if (p.points.size() < 2) continue;
            LevelObject o;
            o.kind = LevelObject::Boat;
            if (!loadModel(vfs, assets, packageExists(vfs, p.name) ? p.name : "giz_sailboat01_f", o)) continue;
            const int x = rng.next15();
            const int sets = o.pkg->setCount;
            o.set = sets > 0 ? x % sets : 0;
            Boat b;
            b.obj = int(objs_.size());
            b.f.p = p.points;
            b.f.s = 4.0f;
            b.f.restart();
            vec3 P, D;
            b.f.update(0.0f, P, D);
            placeFollowing(o, P, D, false);
            o.room = city.findRoom(o.o, 0);
            const float sigma = p.spacing == 0 ? 5.0f : float(p.spacing) * 0.25f;
            const float r = rng.unit();
            b.f.s = ((sigma + 1.0f) - (sigma - 1.0f)) * r + (sigma - 1.0f);
            objs_.push_back(o);
            boats_.push_back(std::move(b));
        }
    // Drawbridge leaves.
    if (packageExists(vfs, "giz_bridge01_l") && readPaths(vfs, pathsetFor(vfs, rd, "bridge", race), paths))
        for (const PathsetPath& p : paths) {
            if (p.points.size() < 2) continue;
            const size_t colon = p.name.find(':');
            const std::string head = colon == std::string::npos ? p.name : p.name.substr(0, colon);
            std::string model = "giz_bridge01_l";
            if (colon != std::string::npos && packageExists(vfs, p.name.substr(colon + 1))) model = p.name.substr(colon + 1);
            else if (packageExists(vfs, head)) model = head;
            const std::string w4 = toLower(head.substr(0, std::min<size_t>(4, head.size())));
            const int mode = openBridges ? 3 : w4 == "inac" ? 0 : w4 == "time" ? 1 : w4 == "prox" ? 2 : w4 == "open" ? 3 : 1;
            auto makeLeaf = [&](const vec3& P, const vec3& Q) {
                LevelObject o;
                o.kind = LevelObject::Leaf;
                if (!loadModel(vfs, assets, model, o)) return -1;
                Leaf l;
                l.obj = int(objs_.size());
                l.mode = mode;
                l.H = P;
                vec3 Z(P.x - Q.x, P.y - Q.y, P.z - Q.z);
                const float l2 = (Z.x * Z.x + Z.y * Z.y) + Z.z * Z.z;
                const float k = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);
                Z = vec3(Z.x * k, Z.y * k, Z.z * k);
                const vec3 U(0.0f, 1.0f, 0.0f);
                const vec3 X(U.y * Z.z - U.z * Z.y, U.z * Z.x - U.x * Z.z, U.x * Z.y - U.y * Z.x);
                const vec3 Y(Z.y * X.z - X.y * Z.z, X.x * Z.z - Z.x * X.z, X.y * Z.x - X.x * Z.y);
                l.PX = X;
                l.PY = Y;
                l.PZ = Z;
                l.h = o.bd.size.z * 0.5f;
                o.ref = P;
                objs_.push_back(o);
                // Reset before the mode is known: closed, angle 0.
                l.state = Closed;
                l.timer = 0.0f;
                l.angle = 0.0f;
                leafMatrix(l);
                objs_[size_t(l.obj)].room = city.findRoom(objs_[size_t(l.obj)].o, 0);
                for (const vec3& m : {P, vec3(P.x, P.y + 5.0f, P.z)}) {
                    const int room = city.findRoom(m, 0);
                    if (room > 0 && !isBridgeRoom(room)) bridgeRooms_.push_back(room);
                }
                leaves_.push_back(l);
                return int(leaves_.size()) - 1;
            };
            if (p.points.size() == 2) {
                makeLeaf(p.points[0], p.points[1]);
            } else {
                const int a = makeLeaf(p.points[0], p.points[2]);
                const int b = makeLeaf(p.points[2], p.points[0]);
                if (a >= 0 && b >= 0) {
                    leaves_[size_t(a)].partner = b;
                    leaves_[size_t(b)].partner = a;
                }
            }
        }
    // Underground trains.
    if (readPaths(vfs, pathsetFor(vfs, rd, "train", race), paths))
        for (const PathsetPath& p : paths) {
            if (p.points.size() < 2) continue;
            const std::string model = packageExists(vfs, p.name) ? p.name : "va_ug_l";
            Train t;
            bool ok = true;
            for (int k = 0; k < 3 && ok; ++k) {
                LevelObject o;
                o.kind = LevelObject::TrainCar;
                if (!loadModel(vfs, assets, model, o)) {
                    ok = false;
                    break;
                }
                t.obj[k] = int(objs_.size());  // (pushed just below)
                t.f[k].p = p.points;
                t.f[k].s = 40.0f;
                objs_.push_back(o);
            }
            if (!ok) continue;
            trains_.push_back(std::move(t));
        }
    // Ferries.
    if (ferries && readPaths(vfs, pathsetFor(vfs, rd, "ferry", race), paths)) {
        for (const PathsetPath& p : paths) {
            if (p.points.size() < 2) continue;
            LevelObject o;
            o.kind = LevelObject::Ferry;
            if (!loadModel(vfs, assets, packageExists(vfs, p.name) ? p.name : "giz_carferry01_f", o)) continue;
            Ferry f;
            f.obj = int(objs_.size());
            f.f.p = p.points;
            f.f.s = 1.0f;
            f.f.restart();
            vec3 P, D;
            f.f.update(0.0f, P, D);
            vec3 X, Y, Z;
            matrixFromDirection(D, X, Y, Z);
            // Created at the point plus R·CG, like a prop (until its first frame).
            const vec3& g = o.bd.cg;
            o.o = vec3(P.x + ((g.z * Z.x + g.y * Y.x) + g.x * X.x), P.y + ((g.z * Z.y + g.y * Y.y) + g.x * X.y),
                       P.z + ((g.z * Z.z + g.y * Y.z) + g.x * X.z));
            o.X = vec3(X.x, 0.0f, X.z);
            o.Y = vec3(0.0f, 1.0f, 0.0f);
            o.Z = vec3(-X.z, 0.0f, X.x);
            o.ref = o.o;
            o.room = city.findRoom(o.o, 0);
            objs_.push_back(o);
            ferries_.push_back(std::move(f));
        }
        for (Ferry& f : ferries_) {
            const float r = rng.unit();
            f.f.s = ((0.75f + 0.0f) - (0.75f - 0.0f)) * r + (0.75f - 0.0f);
        }
    }
    stats_.boats = int(boats_.size());
    stats_.leaves = int(leaves_.size());
    stats_.trains = int(trains_.size());
    stats_.ferries = int(ferries_.size());
    // The reset every game makes before its first frame (open leaves rise, trains line up).
    reset(city);
}

void LevelObjects::leafMatrix(Leaf& l) {
    // B·Placement (a frame product, vehicle-body.md "Products"), origin lowered by 0.3.
    phys::Mat3 B, P;
    vec3 Bo;
    B.m[0][0] = 1.0f;
    if (l.angle == 0.0f) {
        B.m[1][1] = 1.0f;
        B.m[2][2] = 1.0f;
        Bo = vec3(0.0f, 0.0f, 0.0f - l.h);
    } else {
        const float c = float(std::cos(double(l.angle))), s = float(std::sin(double(l.angle)));
        B.m[1][1] = c;
        B.m[1][2] = s;
        B.m[2][1] = -s;
        B.m[2][2] = c;
        Bo = vec3(0.0f, l.h * s, -(l.h * c));
    }
    const vec3* rows[3] = {&l.PX, &l.PY, &l.PZ};
    for (int i = 0; i < 3; ++i) {
        P.m[i][0] = rows[i]->x;
        P.m[i][1] = rows[i]->y;
        P.m[i][2] = rows[i]->z;
    }
    const phys::Mat3 R = phys::mulProducts(B, P);
    LevelObject& o = objs_[size_t(l.obj)];
    o.X = vec3(R.m[0][0], R.m[0][1], R.m[0][2]);
    o.Y = vec3(R.m[1][0], R.m[1][1], R.m[1][2]);
    o.Z = vec3(R.m[2][0], R.m[2][1], R.m[2][2]);
    const float t0x = Bo.x * P.m[0][0], t1x = Bo.y * P.m[1][0], t2x = Bo.z * P.m[2][0];
    const float t0y = Bo.x * P.m[0][1], t1y = Bo.y * P.m[1][1], t2y = Bo.z * P.m[2][1];
    const float t0z = Bo.x * P.m[0][2], t1z = Bo.y * P.m[1][2], t2z = Bo.z * P.m[2][2];
    o.o = vec3(((t1x + t0x) + t2x) + l.H.x, ((t1y + t0y) + t2y) + l.H.y, ((t2z + t1z) + t0z) + l.H.z);
    o.o.y = -0.3f + o.o.y;
}

void LevelObjects::moveTrainCar(Train& t, int k, float tau, const CityData& city) {
    PathFollower& f = t.f[k];
    vec3 P, D;
    f.update(tau, P, D);
    const int N = f.n();
    const int i = f.i, j = i + 1 == N ? 0 : i + 1;
    const float y0 = f.p[size_t(i)].y;
    const float dy = f.p[size_t(j)].y - y0;
    const float uh = (f.s * f.t) / f.L;
    const float y = uh * dy + y0;
    LevelObject& o = objs_[size_t(t.obj[k])];
    vec3 X, Y, Z;
    matrixFromDirection(vec3(D.x, dy, D.z), X, Y, Z);
    o.X = X;
    o.Y = Y;
    o.Z = Z;
    o.o = vec3(P.x, y + o.bd.cg.y, P.z);
    o.ref = o.o;
    o.room = city.findRoom(o.o, o.room);
}

void LevelObjects::reset(const CityData& city) {
    for (Boat& b : boats_) b.f.restart();
    for (Leaf& l : leaves_) {
        l.state = Closed;
        l.timer = 0.0f;
        l.angle = l.mode == 3 ? kOmega : 0.0f;
        leafMatrix(l);
    }
    for (Train& t : trains_) {
        t.state = Waiting;
        t.timer = 0.0f;
        t.factor = 1.0f;
        t.forward = true;
        for (int k = 0; k < 3; ++k) {
            t.f[k].restart();
            moveTrainCar(t, k, float(k) * 0.44f, city);
        }
    }
    for (Ferry& f : ferries_) {
        f.f.restart();
        LevelObject& o = objs_[size_t(f.obj)];
        o.room = city.findRoom(o.o, o.room);
    }
}

void LevelObjects::update(float F, const CityData& city, const std::vector<vec3>& cars) {
    // Boats.
    for (Boat& b : boats_) {
        vec3 P, D;
        b.f.update(F, P, D);
        LevelObject& o = objs_[size_t(b.obj)];
        placeFollowing(o, P, D, false);
        o.room = city.findRoom(o.o, o.room);
    }
    // Leaves.
    for (size_t li = 0; li < leaves_.size(); ++li) {
        Leaf& l = leaves_[li];
        LevelObject& o = objs_[size_t(l.obj)];
        if (l.mode == 2 && l.state == Closed)
            for (const vec3& b : cars) {
                const vec3& q = o.o;
                const float d2 = ((b.z - q.z) * (b.z - q.z) + (b.y - q.y) * (b.y - q.y)) + (b.x - q.x) * (b.x - q.x);
                if (d2 < 10000.0f) {
                    l.state = Opening;
                    l.timer = 0.0f;
                    if (l.partner >= 0 && leaves_[size_t(l.partner)].state == Closed) {
                        leaves_[size_t(l.partner)].state = Opening;
                        leaves_[size_t(l.partner)].timer = 0.0f;
                    }
                    break;
                }
            }
        o.room = city.findRoom(o.o, o.room);
        switch (l.state) {
        case Closed:
            if (l.mode == 1) {
                l.timer = F + l.timer;
                if (l.timer > 10.0f) {
                    l.timer = 0.0f;
                    l.state = Opening;
                }
            }
            break;
        case Open:
            l.timer = F + l.timer;
            if (l.timer > 10.0f) {
                l.timer = 0.0f;
                l.state = Closing;
            }
            break;
        case Closing:
            l.angle = approach(l.angle, 0.0f, 0.05f * F);
            if (l.angle == 0.0f) {
                l.timer = 0.0f;
                l.state = Closed;
            }
            leafMatrix(l);
            break;
        case Opening:
            l.angle = approach(l.angle, kOmega, F * 0.05f);
            if (l.angle == kOmega) {
                l.timer = 0.0f;
                l.state = Open;
            }
            leafMatrix(l);
            break;
        }
    }
    // Trains.
    for (Train& t : trains_) {
        const float sigma = t.forward ? F : F * -1.0f;
        const int N = t.f[0].n();
        auto inStation = [&]() { return t.forward ? t.f[0].i >= N - 3 : t.f[2].i < 2; };
        switch (t.state) {
        case Waiting:
            t.timer = F + t.timer;
            if (t.timer > 10.0f) {
                t.timer = 0.0f;
                t.state = Accelerating;
            }
            break;
        case Running:
            for (int k = 0; k < 3; ++k) moveTrainCar(t, k, sigma, city);
            if (inStation()) t.state = Braking;
            break;
        case Braking:
            t.factor = approach(t.factor, 0.0f, 0.51f * F);
            if (t.factor == 0.0f) {
                t.state = Waiting;
                t.forward = !t.forward;
            } else {
                for (int k = 0; k < 3; ++k) moveTrainCar(t, k, sigma * t.factor, city);
            }
            break;
        case Accelerating:
            t.factor = approach(t.factor, 1.0f, 0.51f * F);
            if (t.factor == 1.0f) {
                t.state = Running;
            } else {
                for (int k = 0; k < 3; ++k) moveTrainCar(t, k, sigma * t.factor, city);
            }
            break;
        }
    }
    // Ferries.
    for (Ferry& f : ferries_) {
        vec3 P, D;
        f.f.update(F, P, D);
        LevelObject& o = objs_[size_t(f.obj)];
        placeFollowing(o, P, D, true);
        o.room = city.findRoom(o.o, o.room);
    }
}

}  // namespace cc
