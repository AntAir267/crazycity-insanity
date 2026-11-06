// Route search (spec/ai/route-search.md): which intersections an AI car drives through to reach a
// point. MM2's exact search: seeding by assignment, front insertion into the open list, strict
// smallest-cost scan, no closed set.
#pragma once

#include <vector>

#include "core/math.h"

namespace cc {
namespace ai {

class RoadNetwork;

enum class RouteStart { Free, KeepHeading };

struct RouteQuery {
    vec3 start;          // S: the car's body position
    vec3 startZ;         // Zs: its Z row (keep-heading mode)
    int startRoom = 0;   // room hint
    vec3 goal;           // G
    int goalRoom = 0;    // room hint
    RouteStart mode = RouteStart::Free;
    bool firstPlayerReversing = false;  // keep-heading at an intersection reads the first player's gear
};

// Fills `route` (cleared first) with the intersection list; returns its count (0 = no route).
// `goalCost` (optional) gets the search cost of the goal intersection reached.
int routeSearch(const RoadNetwork& net, const RouteQuery& q, std::vector<int>& route, float* goalCost = nullptr);

// "Intersection I, keep heading" (route-search.md, step 3): the road of I that best continues
// heading Z (h = Z when reversing, else −Z), roads without a side-B sidewalk line skipped; the
// first road of I when none qualifies; −1 when I has no roads.
int keepHeadingRoad(const RoadNetwork& net, int I, const vec3& Z, bool reversing);

// The other end of road `r` seen from intersection `I` (route-search.md, "The graph").
int otherEnd(const RoadNetwork& net, int r, int I);

// The road between intersections A and B and its direction flag (1: A is the road's end-1
// intersection, so the car travels from section 0 toward n − 1). −1 when there is none (then
// `dirFlag` is left unchanged).
int roadBetween(const RoadNetwork& net, int A, int B, int& dirFlag);

}  // namespace ai
}  // namespace cc
