// Collision bounds (spec/formats/bnd.md) and terrain grids (spec/physics/collision-objects.md,
// "Terrain bounds"), plus the box bound of props (collision-object-contacts.md 4.1).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/math.h"
#include "physics/material.h"

namespace cc {

class Vfs;

namespace phys {

struct BoundPoly {
    int idx[4] = {0, 0, 0, 0};
    int n = 3;            // 3 or 4 corners
    int material = 0;     // index into the bound's material list (low 8 bits)
    vec3 normal;
    vec3 edgeNormal[4];   // inward edge normals per corner
    int edgeNum[4] = {0, 0, 0, 0};  // edge number of the edge from corner k to k+1
};

struct TerrainGrid {
    int W = 0, H = 1, D = 0;
    vec3 size, factor, gmin, gmax;
    std::vector<uint16_t> first, count, refs;
    bool hotEdges = false;
};

struct Bound {
    std::vector<vec3> points;
    std::vector<BoundPoly> polys;
    std::vector<std::pair<int, int>> edges;
    std::vector<vec3> edgeNormal;
    std::vector<float> edgeCos;
    std::vector<int> materials;  // global material table indices (one per bound material)
    vec3 bmin, bmax, centre;
    float radius = 0;
    bool terrain = false;
    TerrainGrid grid;
    bool isBox = false;

    int firstMaterial() const { return materials.empty() ? 0 : materials[0]; }
    void computeBox();
};

// Text .bnd. Materials are shared by name through `table` (the file's values ignored for known
// names). False + err on a malformed file.
bool parseBndText(const std::string& text, Bound& out, MaterialTable& table, std::string& err);
// Binary .bbnd.
bool parseBbnd(const uint8_t* data, size_t size, Bound& out, MaterialTable& table, std::string& err);
// Derived data of bnd.md "After loading" (edges, edge numbers, normals, edge normals and cosines,
// box). `keepEdges` keeps a file edge list already in `edges`.
void deriveBound(Bound& b);
// .ter on top of a loaded geometry bound.
bool parseTer(const uint8_t* data, size_t size, Bound& b, std::string& err);

// Box bound of size `size` centred at 0 (collision-object-contacts.md 4.1).
Bound makeBoxBound(const vec3& size);
// The same with every point moved by `offset` (car-collision.md 3.1: point k = t_k·S + c; normals
// from these points; min/max c ± h; radius about the bound's origin, not about c).
Bound makeBoxBound(const vec3& size, const vec3& offset);

// Loads bound/<name>_BOUND.bnd (text) or the terrain form. nullptr if missing/broken (logged).
std::shared_ptr<Bound> loadGeometryBound(const Vfs& vfs, const std::string& name, MaterialTable& table);
std::shared_ptr<Bound> loadTerrainBound(const Vfs& vfs, const std::string& name, MaterialTable& table);

}  // namespace phys
}  // namespace cc
