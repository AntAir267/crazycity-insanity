// City collision polygons made from the PSDL (spec/physics/city-collision.md), with the culling
// of "Culling and buffers". Built once per room at load, in the order the game makes them.
#pragma once

#include <string>
#include <vector>

#include "core/math.h"
#include "physics/material.h"

namespace cc {

class Vfs;
class CityData;

namespace phys {

struct CollPoly {
    vec3 c[4];
    int n = 3;
    vec3 normal;
    vec3 edgeN[4];  // inward edge normals
    int material = 0;  // material code (0/1 = default, c >= 1 = table entry c - 1)
    // culling
    vec3 R;          // reference corner
    float L2 = 0;    // flat extent
    int pass = -1;   // strip pass (section range), -1 = none
    int section = 0;
    int flatGroup = -1;  // intersection fan / roof group (straddle test), -1 = none
    int room = 0;
    int index = 0;   // position in the room's list
    int elemType = -1;  // PSDL element type that made it (diagnostics)
    int category = 0;   // 0 surface, 1 wall, 2 median side wall (diagnostics)
};

struct StripPass {
    std::vector<vec3> P, D;  // cross-section line per section: start point, direction
    std::vector<float> w;    // 1 / flat length (0 for a zero-length line)
};

struct RoomPolys {
    std::vector<CollPoly> polys;
    std::vector<StripPass> passes;
    std::vector<float> groupY;
};

// Build diagnostics (native test only).
struct CityCollisionStats {
    int dropped = 0;         // polygons dropped for a zero normal
    int droppedByType[16] = {};
    int fourthIndex = 0;     // strip quads made triangles by the "p' index 0" rule
    int triModeRooms = 0;    // rooms with flag 0x20
    int triModePolys = 0;    // polygons made in those rooms
};

class CityCollision {
public:
    void build(const CityData& city, const Vfs& vfs, const MaterialTable& table);
    void clear() { rooms_.clear(); }
    bool empty() const { return rooms_.empty(); }

    // Appends the polygons of `room` that pass the culling for the sphere (c, r), in order.
    // `budget` < 0 means unlimited; otherwise it is decremented and polygons stop at 0.
    void gather(int room, const vec3& c, float r, std::vector<const CollPoly*>& out, int* budget) const;

    int slotCode(int slot) const { return (slot >= 0 && size_t(slot) < slotCodes_.size()) ? slotCodes_[size_t(slot)] : 0; }
    size_t polygonCount() const;
    const RoomPolys& room(int r) const { return rooms_[size_t(r)]; }
    int roomCount() const { return int(rooms_.size()); }
    const CityCollisionStats& stats() const { return stats_; }
    CityCollisionStats& mutableStats() const { return stats_; }

private:
    std::vector<RoomPolys> rooms_;
    std::vector<int> slotCodes_;
    mutable CityCollisionStats stats_;
};

// Polygon normal and inward edge normals (city-collision.md, "Collision polygons"). False if the
// polygon must be dropped.
bool finishCollPoly(CollPoly& p);

}  // namespace phys
}  // namespace cc
