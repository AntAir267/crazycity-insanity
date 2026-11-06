#include "ai/traffic_control.h"

#include <algorithm>
#include <cmath>

#include "ai/road_network.h"
#include "city/city_data.h"
#include "core/log.h"

namespace cc {
namespace ai {

void TrafficControl::setup(RoadNetwork& net, CityData& city, const Vfs& vfs, AssetCache& assets, const std::string& single,
                           const std::string& dual) {
    net_ = &net;
    sets_.clear();
    stats_ = Stats();
    auto& roads = net.roads();
    const auto& ixs = static_cast<const RoadNetwork&>(net).intersections();
    queues_.assign(ixs.size(), {});
    for (size_t I = 0; I < ixs.size(); ++I) {
        const Intersection& x = ixs[I];
        // The approaches with a light, in list order; the end that names this intersection.
        struct Approach {
            int road, end, slot;
        };
        std::vector<Approach> lit;
        int sources = 0, blocked = 0;
        for (size_t k = 0; k < x.roads.size(); ++k) {
            const int q = x.roads[k];
            const Road& r = roads[size_t(q)];
            const int end = r.end[0].intersection == int(I) ? 0 : 1;
            if (r.end[end].control == 1) lit.push_back({q, end, int(k)});
            if (r.end[0].intersection == int(I) || r.side[0].lanes > 0) ++sources;
            if (r.end[1].intersection == int(I)) {
                if (r.side[1].flags & 1) ++blocked;
            } else if (r.side[0].lanes > 0 && (r.side[0].flags & 1)) {
                ++blocked;
            }
        }
        if (lit.empty()) continue;
        LightSet set;
        set.intersection = int(I);
        set.sources = sources;
        set.blockedExits = blocked;
        if (x.roads.size() == 4 && sources == 4 && blocked == 4) set.kind = 2;
        else if (int(lit.size()) == sources) set.kind = 1;
        else set.kind = 0;
        for (size_t li = 0; li < lit.size(); ++li) {
            const Approach& a = lit[li];
            Road& r = roads[size_t(a.road)];
            RoadEnd& e = r.end[a.end];
            e.lightSlot = int(li);
            TrafficLight L;
            L.road = a.road;
            L.end = a.end;
            L.arrivingSide = a.end == 0 ? 1 : -1;
            L.model = r.sideOf(L.arrivingSide).lanes <= 1 ? single : dual;
            L.signPos = e.signPos;
            const float dx = e.facingPoint.x - e.signPos.x, dz = e.facingPoint.z - e.signPos.z;
            const float l2 = dx * dx + dz * dz;
            const float k = l2 == 0.0f ? 0.0f : 1.0f / std::sqrt(l2);  // dx·(1/√l2), not dx/√l2
            const vec3 X(dx * k, 0.0f, dz * k);
            const vec3 Y(0, 1, 0);
            const vec3 Z(-X.z, 0.0f, X.x);
            mat4 m = mat4::identity();
            m.m[0] = X.x, m.m[1] = X.y, m.m[2] = X.z;
            m.m[4] = Y.x, m.m[5] = Y.y, m.m[6] = Y.z;
            m.m[8] = Z.x, m.m[9] = Z.y, m.m[10] = Z.z;
            m.m[12] = e.signPos.x, m.m[13] = e.signPos.y, m.m[14] = e.signPos.z;
            L.placement = m;
            L.pole = city.addStreetProp(vfs, assets, L.model, m);
            if (L.pole >= 0) ++stats_.poles;
            L.state = LightYellow;
            set.lights.push_back(L);
        }
        if (set.kind == 2) {
            int acc = 0;
            for (size_t i = 0; i < set.lights.size() && i < x.roads.size(); ++i) {
                Road& r = roads[size_t(x.roads[i])];
                RoadEnd& e = r.end[r.end[0].intersection == int(I) ? 0 : 1];
                acc = 3 | acc | e.turnRule;
                e.turnRule = acc;
            }
        }
        ++stats_.kinds[set.kind];
        stats_.lights += int(set.lights.size());
        net.intersections()[I].lightSet = int(sets_.size());
        sets_.push_back(std::move(set));
    }
}

void TrafficControl::setLight(LightSet& set, int i, int state) {
    if (i >= 0 && size_t(i) < set.lights.size()) set.lights[size_t(i)].state = state;
}

void TrafficControl::wake(LightSet& set, const RoadNetwork& net, int slot, int sideSlot) {
    const Intersection& x = net.intersections()[size_t(set.intersection)];
    if (slot < 0 || size_t(slot) >= x.roads.size()) return;
    if (sideSlot < 0 || size_t(sideSlot) >= x.roads.size()) return;
    const int q = x.roads[size_t(slot)];
    // The side is the arriving side of slot `sideSlot`'s road at this intersection: side B if the
    // intersection is not that road's end 1, else side A (traffic-control.md, "Waking a road"; for
    // kind 2's second road the side chosen for slot c's road).
    const Road& rs = net.roads()[size_t(x.roads[size_t(sideSlot)])];
    WakeEvent w;
    w.intersection = set.intersection;
    w.road = q;
    w.side = rs.end[1].intersection != set.intersection ? 1 : -1;
    wakes_.push_back(w);
}

void TrafficControl::reset() {
    for (LightSet& set : sets_) {
        set.current = 0;
        set.walk = false;
        set.timer = 0;
        for (size_t i = 0; i < set.lights.size(); ++i) set.lights[i].state = i == 0 ? LightGreen : LightRed;
        if (set.kind == 2) setLight(set, 2, LightGreen);
    }
    for (StopQueue& q : queues_) {
        q.waiting.clear();
        q.go.clear();
    }
    if (net_)
        for (Road& r : net_->roads()) r.held = false;  // AI resets clear the held marks
    wakes_.clear();
}

void TrafficControl::update(float dt) {
    for (LightSet& set : sets_) {
        const int count = int(set.lights.size());
        set.timer = set.timer + dt;
        if (!set.walk) {
            if (set.timer <= kCycle) {
                if (set.timer > kCycle - 3.0f) {
                    setLight(set, set.current, LightYellow);
                    if (set.kind == 2) setLight(set, (set.current + 2) % 4, LightYellow);
                }
            } else {
                setLight(set, set.current, LightRed);
                if (set.kind == 2) setLight(set, (set.current + 2) % 4, LightRed);
                set.current = set.current + 1;
                set.timer = 0;
                if (set.current == count) {
                    if (set.kind == 1 || set.kind == 2) {
                        set.walk = true;
                        for (int i = 0; i < count; ++i) setLight(set, i, LightWalk);
                    } else {
                        set.current = 0;
                    }
                }
                if (!set.walk) {
                    setLight(set, set.current, LightGreen);
                    if (set.kind == 2) setLight(set, (set.current + 2) % 4, LightGreen);
                    if (net_) {
                        wake(set, *net_, set.current, set.current);
                        if (set.kind == 2) wake(set, *net_, (set.current + 2) % 4, set.current);
                    }
                }
            }
        } else {
            if (set.timer > kCycle) {
                // Reset of this set (the walk phase ends).
                set.current = 0;
                set.walk = false;
                set.timer = 0;
                for (int i = 0; i < count; ++i) setLight(set, i, i == 0 ? LightGreen : LightRed);
                if (set.kind == 2) setLight(set, 2, LightGreen);
            } else if (set.timer > kCycle - 3.0f) {
                for (int i = 0; i < count; ++i) setLight(set, i, LightWalkEnding);
            }
        }
    }
}

int TrafficControl::approachState(const RoadNetwork& net, int road, int s) const {
    const RoadEnd& e = net.arrivalEnd(road, s);
    if (e.control != 1 || e.lightSlot < 0) return 0;
    const Intersection& x = net.intersections()[size_t(e.intersection)];
    if (x.lightSet < 0) return 0;
    const LightSet& set = sets_[size_t(x.lightSet)];
    if (size_t(e.lightSlot) >= set.lights.size()) return 0;
    return set.lights[size_t(e.lightSlot)].state;
}

bool TrafficControl::mayEnter(RoadNetwork& net, int road, int s, int car, bool ambient, float speed, float remaining) {
    Road& r = net.roads()[size_t(road)];
    const RoadEnd& e = net.arrivalEnd(road, s);
    if (r.alwaysGreen) {
        WakeEvent w;
        w.intersection = e.intersection;
        w.road = -1;  // every road's waiting cars on this side number
        w.side = s;
        wakes_.push_back(w);
        return true;
    }
    if (r.held) return false;
    switch (e.control) {
    case 0: {
        if (!(speed < 0.5f && remaining < 1.5f)) return false;
        StopQueue& q = queues_[size_t(e.intersection)];
        auto in = [&](const std::vector<QueuedCar>& v) {
            return std::any_of(v.begin(), v.end(), [&](const QueuedCar& c) { return c.car == car; });
        };
        if (!in(q.waiting) && !in(q.go)) q.waiting.push_back({car, road, ambient});
        if (q.go.empty() && !q.waiting.empty()) {
            const QueuedCar first = q.waiting.front();
            q.waiting.erase(q.waiting.begin());
            q.go.push_back(first);
            if (first.ambient) {
                for (size_t i = 0; i < q.waiting.size();) {
                    if (q.waiting[i].road == first.road) {
                        q.go.push_back(q.waiting[i]);
                        q.waiting.erase(q.waiting.begin() + long(i));
                    } else {
                        ++i;
                    }
                }
            }
        }
        return in(q.go);
    }
    case 1:
        return approachState(net, road, s) == LightGreen;
    case 2:
        logWarn("traffic: car %d at a yield approach (road %d) is never let through", car, road);
        return false;
    default: {
        WakeEvent w;
        w.intersection = e.intersection;
        w.road = road;
        w.side = s;
        w.car = ambient ? car : -1;
        wakes_.push_back(w);
        return true;
    }
    }
}

void TrafficControl::leftIntersection(int intersection, int car) {
    if (intersection < 0 || size_t(intersection) >= queues_.size()) return;
    auto& go = queues_[size_t(intersection)].go;
    auto it = std::find_if(go.begin(), go.end(), [&](const QueuedCar& c) { return c.car == car; });
    if (it == go.end()) {
        logWarn("traffic: car %d left intersection %d but was not in its go group", car, intersection);
        return;
    }
    go.erase(it);
}

void TrafficControl::removeCar(int car) {
    for (StopQueue& q : queues_) {
        auto drop = [&](std::vector<QueuedCar>& v) {
            v.erase(std::remove_if(v.begin(), v.end(), [&](const QueuedCar& c) { return c.car == car; }), v.end());
        };
        drop(q.waiting);
        drop(q.go);
    }
}

}  // namespace ai
}  // namespace cc
