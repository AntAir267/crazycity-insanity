// cctool peds: a pedestrian run (spec/ai/pedestrians.md, "Check runs") with ambient traffic at
// the traffic run's settings (D 0.5, pool 300). Scenarios: "parked" (the player parked at the
// repeatable start for the whole run) and "street" (a scripted player car driving along the longest
// sidewalk-lined street, slowly and then fast, to trigger the wary modes, dives and wall runs).
// One CSV row per pedestrian on the streets per frame, in update order.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "ai/pedestrians.h"
#include "ai/road_network.h"
#include "ai/traffic.h"
#include "city/city_data.h"
#include "core/log.h"
#include "core/strutil.h"
#include "model/asset_cache.h"
#include "physics/car_tune.h"
#include "physics/collision_world.h"
#include "vfs/vfs.h"

using namespace cc;

bool mountArchivesForTool(Vfs& vfs, const std::vector<std::string>& archives);

namespace {

// A scripted player car moving along a polyline at a speed profile (no physics).
struct ScriptedCar {
    std::vector<vec3> path;
    std::vector<float> dist;
    float s = 0;
    vec3 pos, X{1, 0, 0}, Z{0, 0, 1}, vel;
    float speed = 0;
    void setPath(const std::vector<vec3>& p) {
        path = p;
        dist.assign(p.size(), 0.0f);
        for (size_t i = 1; i < p.size(); ++i) dist[i] = dist[i - 1] + length(p[i] - p[i - 1]);
        s = 0;
        place();
    }
    float total() const { return dist.empty() ? 0.0f : dist.back(); }
    void place() {
        if (path.size() < 2) return;
        size_t i = 1;
        while (i + 1 < path.size() && dist[i] < s) ++i;
        const float seg = dist[i] - dist[i - 1];
        const float w = seg > 0 ? std::clamp((s - dist[i - 1]) / seg, 0.0f, 1.0f) : 0.0f;
        pos = path[i - 1] + (path[i] - path[i - 1]) * w;
        vec3 f = path[i] - path[i - 1];
        f.y = 0;
        f = normalize(f);
        Z = f * -1.0f;
        X = vec3(Z.z, 0, -Z.x);  // right = Y × Z (forward −Z)
        vel = f * speed;
    }
    void advance(float F, float v) {
        speed = v;
        s = std::min(s + v * F, total());
        if (s >= total()) speed = 0;
        place();
    }
};

}  // namespace

int cmdPeds(const std::string& cityName, const std::string& scenario, float seconds, float density, const std::string& outPath,
            const std::vector<std::string>& archives) {
    setLogEcho(false);
    Vfs vfs;
    if (!mountArchivesForTool(vfs, archives)) return 1;
    AssetCache assets(vfs);
    CityData city;
    std::string msg;
    if (!city.load(cityName, vfs, assets, msg) || !city.network()) {
        std::fprintf(stderr, "cannot load %s: %s\n", cityName.c_str(), msg.c_str());
        return 1;
    }
    const ai::RoadNetwork& net = *city.network();
    phys::CollisionWorld world;
    world.setupCity(vfs, city);
    ai::TrafficSystem ts;
    ts.create(vfs, assets, city, world, 0.5f);
    ai::PedestrianSystem peds;
    const int count = ai::PedestrianSystem::countFor(net.cityAimap().pedPool, density);
    peds.create(vfs, assets, city, world, count, false);

    phys::CarTune tune = phys::loadCarTune(vfs, "vpbug", true);
    ScriptedCar car;
    const bool street = scenario == "street";
    vec3 start(0, 0, 0);
    if (!city.starts().empty()) start = city.starts()[0].pos;
    if (street) {
        // A main road with sidewalks open to pedestrians on both sides and a lane on side B, 150-600 m
        // long, in the most rooms' pedestrian lists (a busy street).
        std::vector<int> listed(size_t(net.mainRoadCount()), 0);
        for (size_t room = 0; room < city.rooms().size(); ++room)
            for (uint16_t r : net.pedRoads(int(room)))
                if (r < listed.size()) ++listed[r];
        int best = -1;
        float bestLen = 0;
        for (int r = 0; r < net.mainRoadCount(); ++r) {
            const ai::Road& rd = net.roads()[size_t(r)];
            if (rd.side[1].lanes < 1 || (rd.side[0].flags & 2) || (rd.side[1].flags & 2) || rd.n < 3 || (rd.flags & 4)) continue;
            if (rd.length() < 150.0f || rd.length() > 600.0f) continue;
            if (best < 0 || listed[size_t(r)] > listed[size_t(best)]) {
                bestLen = rd.length();
                best = r;
            }
        }
        if (best < 0) return 1;
        const ai::Road& rd = net.roads()[size_t(best)];
        // Along side B's sidewalk line (players drive on the sidewalks), from section 0.
        std::vector<vec3> path = rd.side[1].lines[size_t(rd.side[1].lanes)].P;
        for (vec3& p : path) p.y += 0.6f;
        car.setPath(path);
        std::printf("street run on road %d (%.0f m, %d sections) along side B's sidewalk\n", best, double(bestLen), rd.n);
    } else {
        car.setPath({start + vec3(0, 0.6f, 0), start + vec3(0, 0.6f, -0.01f)});
    }
    auto players = [&]() {
        ai::PedPlayer p;
        p.bodyPos = car.pos;
        p.X = car.X;
        p.Y = vec3(0, 1, 0);
        p.Z = car.Z;
        p.velocity = car.vel;
        p.speed = car.speed;
        p.gear = car.speed > 0 ? 2 : 1;
        p.inertiaBox = tune.sim.inertiaBox;
        p.radius = tune.modelRadius > 0 ? tune.modelRadius : 2.5f;
        return std::vector<ai::PedPlayer>{p};
    };
    auto trafficPlayers = [&]() {
        ai::TrafficPlayer p;
        p.modelOrigin = car.pos - vec3(0, 0.5f, 0);
        p.bodyPos = car.pos;
        p.radius = tune.modelRadius > 0 ? tune.modelRadius : 2.5f;
        return std::vector<ai::TrafficPlayer>{p};
    };
    ts.reset(trafficPlayers());
    peds.reset(players());
    std::FILE* out = std::fopen(outPath.c_str(), "w");
    if (!out) return 1;
    std::fprintf(out,
                 "frame,ped,road,side,dir,proad,pside,pdir,k,x,o,h,px,py,pz,mode,step,wall,choice,seq,next,aframe\n");
    const float F = float(1.0 / 60.0);
    const int frames = int(seconds * 60.0f + 0.5f);
    int nan = 0, modes[3] = {}, maxOn = 0;
    auto seqName = [&](const ai::Pedestrian& p, int i) -> std::string {
        const PedModel* m = peds.modelOf(p);
        if (!m || i < 0 || size_t(i) >= m->sequences().size()) return "-";
        return m->sequences()[size_t(i)].row.name;
    };
    for (int f = 1; f <= frames; ++f) {
        if (street) car.advance(F, f < 60 * 10 ? 4.0f : 12.0f);
        const auto tp = trafficPlayers();
        ts.updateCars(F, tp);
        peds.update(F, players(), &ts);
        ts.finishFrame(F);
        ts.afterPhysics();
        for (int i : peds.updateOrder()) {
            const ai::Pedestrian& p = peds.peds()[size_t(i)];
            if (!std::isfinite(p.p.x + p.p.y + p.p.z + p.h)) ++nan;
            ++modes[std::clamp(p.mode, 0, 2)];
            std::fprintf(out, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%.6g,%.6g,%.6g,%.6g,%.6g,%.6g,%d,%d,%d,%d,%s,%s,%d\n", f, p.index, p.road,
                         p.side, p.dir, p.prevRoad, p.prevSide, p.prevDir, p.k, double(p.x), double(p.o), double(p.h),
                         double(p.p.x), double(p.p.y), double(p.p.z), p.mode, p.step, p.wall, p.choice,
                         seqName(p, p.cur).c_str(), seqName(p, p.next).c_str(), p.frame);
        }
        maxOn = std::max(maxOn, peds.onStreets());
    }
    std::fclose(out);
    const auto& st = peds.stats();
    std::printf("%s %s: %d pedestrians (pool %d × %.2f), %d frames: %d on the streets (max %d), placed %d, emptied %d, road ends "
                "%d, crossings %d, turn-backs %d, step-arounds %d, wary %d, dives %d, back-ups %d, lost %d, NaN %d; "
                "updates walking %d, wary %d, dodging %d\n",
                cityName.c_str(), scenario.c_str(), peds.count(), net.cityAimap().pedPool, double(density), frames,
                peds.onStreets(), maxOn, st.placed, st.emptied, st.roadEnds, st.crossings, st.turnBacks, st.stepArounds, st.wary,
                st.dives, st.backups, peds.lostCount(), nan, modes[0], modes[1], modes[2]);
    return nan ? 1 : 0;
}
