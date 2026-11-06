// CPU side of a loaded city (no graphics): PSDL rooms with their spheres, vertical ranges and
// containment test, deep water, landmark rooms, placed objects, and the free-roam start points.
// Used by the renderer (city/city.cpp), the drive mode and the native tools.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "city/level_objects.h"
#include "core/math.h"
#include "formats/bai.h"
#include "formats/city_files.h"
#include "formats/inst.h"
#include "formats/psdl.h"

namespace cc {

class Vfs;
class AssetCache;
struct Pkg;
struct Mesh;
namespace ai {
class RoadNetwork;
}

struct CityRoom {
    vec3 centre;        // sphere (psdl.md, "Levels of detail")
    float radius = 0;
    float yLo = -1000, yHi = 1000;  // vertical range (psdl.md, "Room flags")
    uint8_t flags = 0;
    bool deepWater = false;
    bool hasLandmark = false;
    bool extraRooms = false;     // city-collision.md: SF rooms 411, 412, 423, 625
    std::vector<vec3> outline;   // perimeter points
    std::vector<uint16_t> pointIdx;  // perimeter point indices
    std::vector<int> neighbours;     // route-search.md: distinct non-zero rooms across the edges, perimeter order
    std::vector<int> overlaps;   // stacked rooms overlapping this (non-stacked) room
    vec3 bmin, bmax;             // flat bounding box of the outline
};

enum class CityObjectKind { Static, Spanning, Landmark, Breakable };

struct CityObject {
    std::string name;
    std::shared_ptr<const Pkg> pkg;
    const Mesh* lod[4] = {};  // H, M, L, VL
    mat4 world;
    vec3 pos;
    vec3 zAxis;
    float radius = 0;
    int room = 0;
    int set = 0;
    CityObjectKind kind = CityObjectKind::Static;
    bool fromXref = false;
    uint8_t variant = 0;        // .inst variant byte as stored
    int instIndex = -1;         // record number in its .inst file (load order)
    bool compact = false;       // compact placement
    bool streetProp = false;    // made by the game (traffic-light poles), not by an .inst file
    float modelRadius = 0;      // largest level radius, unscaled
    mat4 placement;             // .inst placement as stored / xref combined matrix C (before any flattening)
    std::vector<vec3> glows;    // props: model-space glow points CG + GlowOffset (night.md)
    mat4 drawWorld;             // what the model is drawn with: props at their CG (placement + R·CG)
    // Where it came from: 0 the .inst files and the game's poles, 1 a street prop
    // (street-props.md), 2 a props.pathset prop (pathset.md).
    int origin = 0;
    bool noWheels = false;      // landmark with .inst header bit 26: wheels do not probe it
    long long created = 0;      // creation order (street-props.md, "Creation order"); room lists run newest first
    int billFlags = 0;          // prop data BillFlags (0x200 tree, also set for names containing "_tree")
};

// A flat road decal (pathset.md, "Decals"): a textured triangle strip, filed in a room as a static.
struct CityDecal {
    std::string texture;
    struct Vertex {
        vec3 pos;
        float u = 0, v = 0;
    };
    std::vector<Vertex> strip;  // psdl.md strip order: (0 1 2), (2 1 3), (2 3 4), …
    int room = 0;
};

// Counts of the generated objects (tests and the log).
struct StreetPropStats {
    int roads = 0, roadsWithRule = 0, roadsWithElement = 0;
    int streetProps = 0, pathsetPlaced = 0, pathsetProps = 0, pathsetFull = 0, decals = 0;
    std::map<std::string, int> byType;  // street props per prop type name
};

// The path sets a game loads with the city and which moving level objects it creates
// (level-objects.md, "The five kinds and their files"; races.md, "Path sets of a race";
// multiplayer.md 1.4). The default is the viewer's and single player's Cruise set.
struct LevelSet {
    // The session's race name (races.md, "Path sets of a race"): race<N>, circuit<N>, blitz<N>,
    // crash<N>, multicop; Cruise "roam". It picks the race props race/<c>/<race>.pathset and,
    // outside Cruise, the moving objects' <c>_<word>_<race>.pathset variants.
    std::string race = "roam";
    bool ferries = true;       // single-player games only
    bool parkedCars = true;    // single player; network games only in Checkpoint, Circuit and Blitz
    bool openBridges = false;  // network games in London: every leaf in the "open" mode
    bool operator==(const LevelSet& o) const {
        return race == o.race && ferries == o.ferries && parkedCars == o.parkedCars && openBridges == o.openBridges;
    }
    bool operator!=(const LevelSet& o) const { return !(*this == o); }
};

struct CityStart {
    int intersection = 0;
    int room = 0;
    vec3 pos;  // intersection position (the car goes 2 m above it)
};

class CityData {
public:
    // `name` is the level (MapName); `raceDir` names the AI and race data (city-files.md, "City
    // list"); empty = the same as `name`.
    bool load(const std::string& name, const Vfs& vfs, AssetCache& assets, std::string& message,
              const std::string& raceDir = "", const LevelSet& levelSet = LevelSet());
    const LevelSet& levelSet() const { return levelSet_; }
    const std::string& raceDir() const { return raceDir_; }

    const std::string& name() const { return name_; }
    const Psdl& psdl() const { return psdl_; }
    const std::vector<CityRoom>& rooms() const { return rooms_; }
    const std::vector<CityObject>& objects() const { return objects_; }
    const std::vector<FogRow>& fog() const { return fog_; }
    const CityLights* lights(int index) const { return (index >= 0 && index < 16 && haveLights_[index]) ? &lights_[index] : nullptr; }
    const SkyInfo* sky() const { return haveSky_ ? &sky_ : nullptr; }
    const std::vector<uint32_t>& roomColors() const { return lmap_; }

    // Point in room (city-collision.md, "Point in room"): the crossing rule, the vertical range
    // of stacked rooms, and the overlap lists of ordinary rooms.
    bool roomContains(int room, const vec3& p) const;
    bool flatInRoom(int room, float x, float z) const;
    // Find(point, hint) of city-collision.md.
    int findRoom(const vec3& p, int hint = 0) const;
    // Touched(room, centre, radius, max) of city-collision.md.
    std::vector<int> touched(int room, const vec3& centre, float radius, int maxRooms) const;
    float waterLevel() const { return waterLevel_; }

    // Free-roam start (controls.md): eligible intersections, in index order.
    const std::vector<CityStart>& starts() const { return starts_; }
    int intersectionCount() const { return int(bai_.intersections.size()); }
    // The AI network's intersection records (bai.md), in file order.
    const std::vector<BaiIntersection>& intersections() const { return bai_.intersections; }
    bool hasAiNetwork() const { return haveBai_; }

    int landmarkCount() const;

    // The AI road network (spec/formats/bai.md), loaded with the city; null without one.
    const ai::RoadNetwork* network() const { return network_.get(); }
    ai::RoadNetwork* network() { return network_.get(); }
    // A breakable street prop (a traffic-light pole, traffic-control.md): placed with `world`,
    // in the room of its centre-of-gravity point. Returns its object index, −1 if it cannot be made.
    int addStreetProp(const Vfs& vfs, AssetCache& assets, const std::string& name, const mat4& world);
    // A prop's position: placement + R·CG with R its full placement matrix (collision-objects.md).
    vec3 propPosition(int object, const vec3& cg) const;
    const std::vector<CityDecal>& decals() const { return decals_; }
    // Room visibility (cpvs.md): whether the city has its own valid .cpvs, and the decoded row of
    // `room` (B = (rooms + 3) / 4 bytes, 2 bits per room, room 0 in the lowest bits of byte 0).
    bool hasPvs() const { return pvsValid_; }
    void pvsRow(int room, std::vector<uint8_t>& out) const;
    const StreetPropStats& streetPropStats() const { return spStats_; }

    // Moving level objects (level-objects.md): boats, leaves, trains, ferries; the parked cars are
    // breakable props of objects() with origin 3.
    LevelObjects& levelObjects() { return levelObjects_; }
    const LevelObjects& levelObjects() const { return levelObjects_; }
    struct ParkedStats {
        int slots = 0, empty = 0, pcar01 = 0, pcar02 = 0;
    };
    const ParkedStats& parkedStats() const { return parked_; }
    // The game-wide generator's state at the first level-object draw (street props' final state,
    // then the player car's 192 draws) and after the last one (where the cable cars draw on).
    uint32_t gizmoRandomStart() const { return gizmoRngStart_; }
    // How many objects existed when the level objects were created (they come after those and
    // before the parked cars and the traffic-light poles in creation order).
    long long levelObjectsCreated() const { return levelObjectsCreated_; }
    uint32_t gizmoRandomEnd() const { return gizmoRngEnd_; }

private:
    void buildRooms(const Vfs& vfs);
    void placeObjects(const Vfs& vfs, AssetCache& assets, const std::string& path);
    void addXrefs(const Vfs& vfs, AssetCache& assets, const CityObject& owner, uint8_t ownerVariant);
    bool hasBangerData(const Vfs& vfs, const std::string& name);
    std::vector<vec3> glowPoints(const Vfs& vfs, const std::string& name);
    std::map<std::string, std::vector<vec3>> glowCache_;
    void findStarts();
    // street_props.cpp: the props along the AI roads, props.pathset, decals.pathset.
    void generateStreetProps(const Vfs& vfs, AssetCache& assets);
    // props.pathset, or (`file`) a race path set (pathset.md, "Path-set props").
    void placePathsetProps(const Vfs& vfs, AssetCache& assets, const std::string& file = std::string());
    void loadDecals(const Vfs& vfs);
    void placeParkedCars(const Vfs& vfs, AssetCache& assets, Lcg& rng, const std::string& race);
    uint32_t streetRngState_ = 1;
    uint32_t gizmoRngStart_ = 1, gizmoRngEnd_ = 1;
    long long levelObjectsCreated_ = 0;
    LevelObjects levelObjects_;
    ParkedStats parked_;
    // A generated breakable prop with its full placement matrix (X, Y, Z, P); `turnOnly` keeps
    // only P and the x and z of X. False if the model has no prop data or package.
    bool addGeneratedProp(const Vfs& vfs, AssetCache& assets, const std::string& model, const vec3& X, const vec3& Y,
                          const vec3& Z, const vec3& P, bool turnOnly, int room, int origin, long long created);
    std::vector<CityDecal> decals_;
    bool pvsValid_ = false;
    std::vector<uint32_t> pvsOffsets_;
    std::vector<uint8_t> pvsData_;
    void loadPvs(const Vfs& vfs);
    StreetPropStats spStats_;
    std::map<std::string, int> billFlagCache_;
    int billFlagsOf(const Vfs& vfs, const std::string& name);
    void buildRoomGrid();
    void buildOverlaps();

    std::string name_;
    std::string raceDir_;
    LevelSet levelSet_;
    Psdl psdl_;
    std::vector<CityRoom> rooms_;
    std::vector<int> stackedRooms_;
    std::vector<CityObject> objects_;
    std::vector<FogRow> fog_;
    CityLights lights_[16];
    bool haveLights_[16] = {};
    SkyInfo sky_;
    bool haveSky_ = false;
    std::vector<uint32_t> lmap_;
    BaiNetwork bai_;
    bool haveBai_ = false;
    std::vector<CityStart> starts_;
    std::shared_ptr<ai::RoadNetwork> network_;
    float waterLevel_ = 0.0f;
    // global room search buckets
    vec3 gridMin_;
    float gridSx_ = 0, gridSz_ = 0;  // 64 / box size (64 × 64 buckets)
    int gridW_ = 0, gridH_ = 0;
    int bucketOf(float x, float z) const;  // -1 outside
    std::vector<std::vector<int>> grid_;
};

}  // namespace cc
