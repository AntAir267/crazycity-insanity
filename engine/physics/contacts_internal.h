// Shared pieces of the contact generation (collision-contacts.md step 3 and its object variant).
#pragma once

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

#include "physics/contacts.h"
#include "physics/pmath.h"

namespace cc {
namespace phys {
namespace ci {

constexpr float kHuge = FLT_MAX;
constexpr float kSin15 = 0.258819043636f;

inline bool sbit(float x) {
    uint32_t u;
    std::memcpy(&u, &x, 4);
    return (u >> 31) != 0;
}

enum RecKind { RecCorner = 0, RecEdge = 1, RecPolyEdge = 2 };

struct Record {
    int kind = RecCorner;
    vec3 start, end, point, normal;
    float T = 0, depth = 0;
    bool front = true;
    int number = 0, from = -1, to = 0;
    int polygon = -1;
    int material = 0;
    vec3 wen;          // world edge normal
    float edgeCos = -1;
    // flags
    bool grouped = false, face = false, pair = false, fast = false, suppressed = false, done = false;
    float m = kHuge;
    // two-list partner state
    int partner = -1;
    vec3 pNormal, pPos;
    float pDepth = 0, pOffset = 0;
    bool angleOk = false;
};

// Order helpers. "zyx" = ((a.z*b.z + a.y*b.y) + a.x*b.x) etc.
inline float dotZYX(const vec3& a, const vec3& b) { return (a.z * b.z + a.y * b.y) + a.x * b.x; }
inline float dotYZX(const vec3& a, const vec3& b) { return (a.y * b.y + a.z * b.z) + a.x * b.x; }
inline float dotZXY(const vec3& a, const vec3& b) { return (a.z * b.z + a.x * b.x) + a.y * b.y; }
inline float dotYXZ(const vec3& a, const vec3& b) { return (a.y * b.y + a.x * b.x) + a.z * b.z; }
inline float dotXZY(const vec3& a, const vec3& b) { return (a.x * b.x + a.z * b.z) + a.y * b.y; }
inline float dotXYZ(const vec3& a, const vec3& b) { return (a.x * b.x + a.y * b.y) + a.z * b.z; }

// Plane distance as collision-contacts.md writes it.
inline float planeDist(const PolyView& p, const vec3& x) {
    return ((x.z - p.c[0].z) * p.normal.z + (x.y - p.c[0].y) * p.normal.y) + (x.x - p.c[0].x) * p.normal.x;
}

// Line-inside tests (two-sided / one-sided) for the line through S with direction D.
bool lineInside2(const PolyView& p, const vec3& S, const vec3& D);
bool lineInside1(const PolyView& p, const vec3& S, const vec3& D);

// Displacement of a body point x between M0 and M (collision-contacts.md, "Terms").
vec3 displacement(const Mtx& M, const Mtx& M0, const vec3& x);

// Closest points of the polygon-edge line A->B and the car-edge line S->E (edge-edge step 5).
// `depth` is written as soon as it is computed (even when the result is not ok).
bool closestPoints(const vec3& A, const vec3& B, const vec3& S, const vec3& E, vec3& nrm, float& depth,
                   vec3& ptAB, vec3& ptSE);

struct EdgeEdgeResult {
    bool ok = false;
    bool flag = false;
    bool depthComputed = false;
    vec3 nrm, pos;
    float depth = 0;
    int edgeNumber = -1;
};

// Edge-edge test (collision-contacts.md 3d) with the polygon in frame F; `cornerToWorld(j, c)`
// takes polygon corner j (index, value in F) to world; S, E, wen, cos are the record's world data.
EdgeEdgeResult edgeEdgeTest(const PolyView& poly, const vec3& pointF, const vec3& dirF, const vec3& velF, float dt,
                            const std::function<vec3(int, int, const vec3&)>& cornerToWorld, const vec3& S,
                            const vec3& E, const vec3& wen, float cosE);

// Step 3a for one list. `peAllowed`: polygon-edge records are only marked grouped.
void normaliseList(std::vector<Record>& list);

// Step 3b for one list. `pairStarts`: a *pair* record may still start a group (the car list).
// `velSign` -1 (city, car list) or +1 (object list). `normalSign` +1/-1 for the impact normal.
void cornerGroups(std::vector<Record>& list, const Mtx& M, const Mtx& M0, float dt, bool pairStarts, float velSign,
                  float normalSign, std::vector<Impact>& out, const Mtx* O = nullptr, const Mtx* O0 = nullptr);

// Step 3c. `mergeMetric`: both records of a merged pair get the smaller metric (objects).
void pairMerge(std::vector<Record>& list, bool mergeMetric);

// Step 3e for one list.
void faceImpacts(std::vector<Record>& list, float dt, float normalSign, std::vector<Impact>& out);

constexpr size_t kMaxImpacts = 200;

}  // namespace ci
}  // namespace phys
}  // namespace cc
