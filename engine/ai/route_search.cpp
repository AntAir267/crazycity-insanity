#include "ai/route_search.h"

#include <algorithm>
#include <cmath>

#include "ai/road_network.h"
#include "city/city_data.h"

namespace cc {
namespace ai {

int keepHeadingRoad(const RoadNetwork& net, int I, const vec3& Z, bool reversing) {
    const auto& ix = net.intersections();
    const auto& roads = net.roads();
    if (I < 0 || size_t(I) >= ix.size()) return -1;
    const vec3 h = reversing ? Z : vec3(-Z.x, -Z.y, -Z.z);
    float best = -999999.0f;
    int pick = -1;
    for (int ri : ix[size_t(I)].roads) {
        const Road& r = roads[size_t(ri)];
        if (r.side[1].outerEdge.empty() || r.shortcut) continue;
        vec3 d;
        if (r.end[1].intersection == I) d = r.C[1] - r.C[0];
        else d = r.C[size_t(r.n - 2)] - r.C[size_t(r.n - 1)];
        const float v = ((d.z * h.z) + (d.y * h.y)) + d.x * h.x;
        if (v > best) {
            best = v;
            pick = ri;
        }
    }
    if (pick < 0 && !ix[size_t(I)].roads.empty()) pick = ix[size_t(I)].roads.front();
    return pick;
}

int otherEnd(const RoadNetwork& net, int r, int I) {
    const Road& road = net.roads()[size_t(r)];
    return road.end[1].intersection == I ? road.end[0].intersection : road.end[1].intersection;
}

int roadBetween(const RoadNetwork& net, int A, int B, int& dirFlag) {
    const auto& ix = net.intersections();
    if (A < 0 || B < 0 || size_t(A) >= ix.size() || size_t(B) >= ix.size()) return -1;
    for (int ra : ix[size_t(A)].roads)
        for (int rb : ix[size_t(B)].roads)
            if (ra == rb) {
                dirFlag = net.roads()[size_t(ra)].end[1].intersection == A ? 1 : 0;
                return ra;
            }
    return -1;
}

namespace {

constexpr float kBig = 9999999.0f;

struct Search {
    const RoadNetwork& net;
    const CityData* city;
    std::vector<float> cost;
    std::vector<int> pred;
    std::vector<int> open;  // front first
    std::vector<int> goals;

    void insert(int v) {
        if (std::find(open.begin(), open.end(), v) != open.end()) return;
        open.insert(open.begin(), v);
    }
    float flatTo(const vec3& S, int I) const {
        const vec3& c = net.intersections()[size_t(I)].centre;
        const float dx = S.x - c.x, dz = S.z - c.z;
        return std::sqrt(dx * dx + dz * dz);
    }
    void seedAround(int A, int B) {
        for (int r : net.intersections()[size_t(A)].roads) {
            const int v = otherEnd(net, r, A);
            if (v == B) continue;
            cost[size_t(v)] = net.roads()[size_t(r)].length();
            insert(v);
        }
        cost[size_t(A)] = 0.0f;
    }
    bool isGoal(int v) const { return std::find(goals.begin(), goals.end(), v) != goals.end(); }
    void addGoal(int v, bool unique) {
        if (goals.size() >= 16) return;  // MM2's list holds 16
        if (unique && isGoal(v)) return;
        goals.push_back(v);
    }
};

const std::vector<int>& neighboursOf(const CityData* city, int room) {
    static const std::vector<int> none;
    if (!city || room <= 0 || size_t(room) >= city->rooms().size()) return none;
    return city->rooms()[size_t(room)].neighbours;
}

}  // namespace

int routeSearch(const RoadNetwork& net, const RouteQuery& q, std::vector<int>& route, float* goalCost) {
    route.clear();
    const auto& ix = net.intersections();
    const auto& roads = net.roads();
    if (ix.empty()) return 0;
    Search s{net, net.city(), std::vector<float>(ix.size(), kBig), std::vector<int>(ix.size(), -1), {}, {}};
    // 1-2. Place the start and the goal.
    const std::vector<Component> Ls = net.allComponentsAt(q.start, q.startRoom, -1);
    const std::vector<Component> Lg = net.allComponentsAt(q.goal, q.goalRoom, -1);
    for (const Component& a : Ls)
        for (const Component& b : Lg)
            if (a == b) return 0;
    const Component s0 = Ls.front(), g0 = Lg.front();
    // 3. Seed.
    int k = 0;
    int startNode = -1;  // −1: virtual
    std::vector<int> list;
    if (s0.kind == 0) {
        bool inserted = false;
        for (int nb : neighboursOf(s.city, s0.index))
            for (const Component& c : net.roomComponents(nb)) {
                if (c.kind == 3) {
                    s.cost[size_t(c.index)] = s.flatTo(q.start, c.index);
                    s.insert(c.index);
                    inserted = true;
                }
                if (c == g0) return 0;
            }
        if (!inserted)
            for (int nb : neighboursOf(s.city, s0.index))
                for (const Component& c : net.roomComponents(nb)) {
                    if (c.kind != 1) continue;
                    const Road& r = roads[size_t(c.index)];
                    for (int e : {0, 1}) {
                        const int I = r.end[e].intersection;
                        s.cost[size_t(I)] = s.flatTo(q.start, I);
                        s.insert(I);
                    }
                }
    } else if (s0.kind == 1 || s0.kind == 2) {
        const Road& r = roads[size_t(s0.index)];
        if (q.mode == RouteStart::Free) {
            const float d = net.distanceAlongCentre(s0.index, q.start);
            s.cost[size_t(r.end[0].intersection)] = r.length() - d;
            s.insert(r.end[0].intersection);
            s.cost[size_t(r.end[1].intersection)] = d;
            s.insert(r.end[1].intersection);
            k = 1;
            list.assign(1, -1);
        } else {
            const bool along = net.facingRoadDirection(s0.index, q.start, q.startZ);
            const int behind = along ? r.end[1].intersection : r.end[0].intersection;
            const int ahead = along ? r.end[0].intersection : r.end[1].intersection;
            list = {behind, ahead};
            k = 2;
            startNode = ahead;
            s.seedAround(ahead, behind);
        }
    } else {
        const int I = s0.index;
        if (q.mode == RouteStart::Free) {
            list = {I};
            k = 1;
            startNode = I;
            s.seedAround(I, -1);
        } else {
            const int pick = keepHeadingRoad(net, I, q.startZ, q.firstPlayerReversing);
            if (pick < 0) return 0;
            const int nb = otherEnd(net, pick, I);
            list = {I, nb};
            k = 2;
            startNode = nb;
            s.seedAround(nb, I);
        }
    }
    // 4. Predecessors.
    std::fill(s.pred.begin(), s.pred.end(), startNode);
    // 5. Goal list.
    for (const Component& c : Lg) {
        if (c.kind == 0) {
            for (int nb : neighboursOf(s.city, c.index))
                for (const Component& e : net.roomComponents(nb)) {
                    if (e.kind == 1) {
                        const Road& r = roads[size_t(e.index)];
                        s.addGoal(r.end[0].intersection, true);
                        s.addGoal(r.end[1].intersection, true);
                    }
                    if (e == s0) return 0;
                }
        } else if (c.kind == 1 || c.kind == 2) {
            const Road& r = roads[size_t(c.index)];
            s.addGoal(r.end[1].intersection, false);
            s.addGoal(r.end[0].intersection, false);
        } else {
            s.addGoal(c.index, false);
        }
    }
    // 6. Already there.
    if (startNode >= 0 && s.isGoal(startNode)) {
        if (k == 1) return 0;
        route = list;
        return k;
    }
    // 7. Expand.
    int found = -1;
    while (!s.open.empty()) {
        float best = kBig;
        size_t at = 0;
        int u = -1;
        for (size_t i = 0; i < s.open.size(); ++i)
            if (s.cost[size_t(s.open[i])] < best) {
                best = s.cost[size_t(s.open[i])];
                u = s.open[i];
                at = i;
            }
        if (u < 0) break;  // every open cost is 9999999 or NaN: nothing can be taken
        if (s.isGoal(u)) {
            found = u;
            break;
        }
        s.open.erase(s.open.begin() + std::ptrdiff_t(at));
        for (int r : ix[size_t(u)].roads) {
            const int v = otherEnd(net, r, u);
            const float c = roads[size_t(r)].length() + s.cost[size_t(u)];
            if (c < s.cost[size_t(v)]) {
                s.cost[size_t(v)] = c;
                s.insert(v);
                s.pred[size_t(v)] = u;
            }
        }
    }
    // 8. No route.
    if (found < 0) return 0;
    if (goalCost) *goalCost = s.cost[size_t(found)];
    // 9. Write the list.
    std::vector<int> chain;
    for (int v = found; v != startNode && v >= 0 && chain.size() <= ix.size(); v = s.pred[size_t(v)]) chain.push_back(v);
    const int h = int(chain.size());
    list.resize(size_t(k + h), -1);
    for (int i = 0; i < h; ++i) list[size_t(k + h - 1 - i)] = chain[size_t(i)];
    k += h;
    // 10. First entry.
    if ((s0.kind == 1 || s0.kind == 2) && q.mode == RouteStart::Free && k >= 2) {
        const Road& r = roads[size_t(s0.index)];
        list[0] = list[1] == r.end[0].intersection ? r.end[1].intersection : r.end[0].intersection;
    } else if (s0.kind == 3) {
        list[0] = s0.index;
    }
    route = list;
    return k;
}

}  // namespace ai
}  // namespace cc
