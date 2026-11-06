// The AI road network of a city (spec/formats/bai.md, spec/formats/aimap.md,
// spec/ai/road-network.md): roads, lanes and intersections after MM2's load procedure, the lane
// pieces cars follow, the next-road rules, position look-ups and the police routing graph. The
// traffic lights and stop signs live in traffic_control.h.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/game_random.h"
#include "core/math.h"
#include "formats/aimap.h"
#include "formats/bai.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;

namespace phys {
class CollisionWorld;
}

namespace ai {

class TrafficControl;

// Obstacle list entries (road-network.md, "Obstacles"): a CityData object index, or from here on
// a moving level object (drawbridge leaf, Underground train car, ferry; level-objects.md "AI
// obstacles"), kLevelObstacleBase + its index in LevelObjects::objects().
constexpr int kLevelObstacleBase = 1 << 24;
// An obstacle list entry → its collision object id (CollisionWorld::object), −1 if none: a city
// object through `propOf` (CityData object → collision object), a level object through the world.
int obstacleObject(int entry, const std::vector<int>& propOf, const phys::CollisionWorld& world);

// Side numbers as the spec writes them: +1 side B (travels section 0 → n − 1), −1 side A.
inline int sideSlot(int s) { return s > 0 ? 1 : 0; }

struct Lane {
    std::vector<vec3> P;   // n points, travel order for lanes after loading
    std::vector<float> D;  // line distances
};

struct RoadSide {
    int lanes = 0, sidewalks = 0;
    bool cable = false, subway = false;
    uint16_t flags = 0;  // bit 0: no ambient vehicles, bit 1: no pedestrians
    float band[10] = {};
    std::vector<Lane> lines;  // lanes 0 … L − 1, then the sidewalks
    std::vector<vec3> cableLine, subwayLine, kerb, outerEdge;
    bool open() const { return (flags & 1) == 0; }
};

struct RoadEnd {
    int intersection = 0;
    int lightSlot = -1;  // light number in the intersection's set (control 1 only)
    int control = 3;     // 0 stop, 1 light, 2 yield, 3 none
    int turnRule = 0;
    int slot = 0;        // position in the intersection's road list
    vec3 signPos, facingPoint;
};

// A bend of a road (driver-planner.md §4): v, A, s and K fixed at load; the rest refreshed by any
// driver that plans through the road (shared by all drivers).
struct Bend {
    int v = 0;      // first raw section
    float A = 0;    // signed angle
    int s = 1;      // sign of A
    vec3 K;         // corner
    float rho = 0, dt = 0;
    vec3 G, Sd, Ed;
};

struct Road {
    int index = 0;
    int n = 0;
    uint16_t flags = 0;  // 0x01 divided, 0x02 alley, 0x04 freeway, 0x08 level
    std::vector<int> rooms;
    float halfWidth = 0;
    RoadSide side[2];  // [0] side A, [1] side B
    std::vector<float> CD;
    std::vector<vec3> C, X, Y, Z, T;
    RoadEnd end[2];  // [0] at section n − 1, [1] at section 0
    bool shortcut = false;
    float speedLimit = 15.0f;
    // Runtime switches of the "may enter" rule (traffic-control.md).
    bool alwaysGreen = false, held = false;
    // Obstacles (road-network.md, "Obstacles"): CityData object indices per section k (1 … n − 1)
    // and side ([0] side −1, [1] side +1).
    std::vector<std::vector<int>> obstacles[2];
    // Heading change θ_i at raw section i (driver-planner.md §4.1, rounded; i = 0 … n − 2) and
    // the bends in list order.
    std::vector<float> theta;
    std::vector<Bend> bends;

    const RoadSide& sideOf(int s) const { return side[sideSlot(s)]; }
    float length() const { return CD.empty() ? 0.0f : CD.back() - CD.front(); }
};

struct Intersection {
    int index = 0;
    int room = 0;
    vec3 centre;               // stored centre (the room's sphere centre if shortcuts joined)
    std::vector<int> roads;    // sorted by angle (bai.md, load step 4)
    int lightSet = -1;         // index into TrafficControl::sets(), −1 = none
    std::vector<int> obstacles;  // heavy props in its room (CityData object indices)
};

struct Component {
    int index = 0;
    int kind = 0;  // 1 road, 2 shortcut, 3 intersection, 0 none (index = the room)
    bool operator==(const Component& o) const { return index == o.index && kind == o.kind; }
};

// A cubic Hermite piece in the ground plane (road-network.md, "Lane paths").
struct Piece {
    vec3 P0, P1, T0, T1;
    float L = 0;
};

// The coefficients a moving car stores (road-network.md, "Evaluating a piece").
struct PieceCurve {
    float ax = 0, bx = 0, cx = 0, dx = 0;
    float az = 0, bz = 0, cz = 0, dz = 0;
    float R = 0;  // 1 / L
};

enum class PieceSetup { Advance, Start, Rejoin, Arrive };

struct NextChoice {
    int road = -1;  // −1: no next road
    int side = 0;   // its outgoing side
    int lane = 0;
    int rule = 0;   // which rule decided (1 … 7); with road −1 the rule that found nothing
};

struct LaneRef {
    int road = -1, side = 1, lane = 0;
};

class RoadNetwork {
public:
    RoadNetwork();
    ~RoadNetwork();

    // The load procedure of bai.md (steps 1–9) for `city` (already loaded with its rooms and
    // objects). Traffic-light poles are added to the city's objects as street props. `raceName`
    // picks race/<c>/<raceName>.aimap ("roam" for Cruise).
    bool load(const Vfs& vfs, AssetCache& assets, CityData& city, const std::string& raceName, std::string& message);
    bool loaded() const { return loaded_; }

    const std::vector<Road>& roads() const { return roads_; }
    std::vector<Road>& roads() { return roads_; }
    const std::vector<Intersection>& intersections() const { return ix_; }
    std::vector<Intersection>& intersections() { return ix_; }
    int mainRoadCount() const { return mainRoads_; }
    const CityAimap& cityAimap() const { return cityAimap_; }
    const CityData* city() const { return city_; }
    const RaceAimap& raceAimap() const { return raceAimap_; }
    // A new game mode or race: race/<RaceDir>/<raceName>.aimap (.aimap_p at Professional) replaces
    // the race settings (police, opponents, exceptions, ambient types) and the speed limits are
    // applied again. Returns whether the file exists.
    bool setRace(const Vfs& vfs, const std::string& raceName, bool professional);
    const std::string& raceName() const { return raceName_; }
    // The ambient car types of this session (the race file's list if it has one).
    const std::vector<AmbientType>& ambientTypes() const;
    TrafficControl& control() { return *control_; }
    const TrafficControl& control() const { return *control_; }

    // ---- Terms ----
    // Exit / entry intersection of a side.
    int exitIntersection(int road, int s) const;
    int entryIntersection(int road, int s) const;
    // Outgoing side of road q at intersection I: +1 if q's end 1 names I, else −1.
    int outgoingSide(int q, int I) const;
    // The end record traffic on side s arrives at (end 0 for side B, end 1 for side A).
    const RoadEnd& arrivalEnd(int road, int s) const { return roads_[size_t(road)].end[s > 0 ? 0 : 1]; }
    vec3 startDir(int road, int s) const;
    vec3 endDir(int road, int s) const;
    vec3 travelTangent(int road, int s, int i) const;
    vec3 axisX(int road, int s, int i) const;  // X(i) = X[sec(i)]
    static int sectionOf(int n, int s, int i) { return s > 0 ? i : n - 1 - i; }

    // ---- Lane paths ----
    // Q(t): the point t before the lane's end, measured with lane 0's last segment.
    vec3 pointBeforeEnd(int road, int s, int lane, float t) const;
    // Piece k (1 … n − 1) of a lane as a car builds it. `skipped` is set when the last piece has
    // L ≤ 0 (it is then not driven).
    Piece lanePiece(int road, int s, int lane, int k, float o, float f, PieceSetup setup = PieceSetup::Advance) const;
    // The piece through the exit intersection to (next road, side, lane); `t` = f, or the last
    // segment's length when the last piece was skipped.
    Piece turnPiece(int road, int s, int lane, float o, float t, const NextChoice& next) const;
    float turnStart(int road, int s, int lane, float f) const;  // f, or seglen(n − 1) if the last piece is skipped
    static PieceCurve curveOf(const Piece& p);
    static vec3 curvePosition(const PieceCurve& c, float e);    // e metres into the piece (u = e·R)
    static vec3 curveDerivative(const PieceCurve& c, float e);
    // The same at a given parameter u (no e·R step).
    static vec3 curvePositionAt(const PieceCurve& c, float u);
    static vec3 curveDerivativeAt(const PieceCurve& c, float u);
    // Direct evaluation (placement and re-attach rules): u = dist / L, clamped below at 0.
    static vec3 directPosition(const Piece& p, float dist, vec3* derivative = nullptr);
    // Piece k and the distance into it for distance s along a lane.
    void pieceAt(int road, int s, int lane, float dist, int& k, float& into) const;
    float laneLength(int road, int s, int lane) const;
    // Point at a route distance (placement variant) along (road, s, lane) then `next`.
    vec3 pointAtRouteDistance(int road, int s, int lane, float o, float f, float dist, const NextChoice& next) const;
    // The re-attach variant: also returns the derivative.
    vec3 pointAtRouteDistanceRejoin(int road, int s, int lane, float o, float f, float dist, const NextChoice& next,
                                    vec3& derivative) const;

    // ---- Choosing the next road ----
    // Draws from `rng` exactly as the rules say (traffic has its own generator, traffic.md).
    NextChoice chooseNext(int road, int s, int lane, Lcg& rng) const;
    // Turn class of road → next (rule 4): +1 right, −1 left, 0 straight.
    int turnClass(int road, int s, int next, int nextSide) const;
    // Every road the rules can give for this lane end (no random numbers drawn), for tests and
    // the debug overlay.
    std::vector<NextChoice> nextOptions(int road, int s, int lane) const;

    // ---- Room lists ----
    const std::vector<Component>& roomComponents(int room) const;
    Component componentOfRoom(int room) const;
    Component componentAt(const vec3& p, int hint) const;
    Component componentAtPreferring(const vec3& p, int hint, int q, bool roadsOnly, int* roomOut = nullptr) const;
    std::vector<Component> allComponentsAt(const vec3& p, int hint, int q) const;

    // ---- Road queries ----
    // 1 on the roadway, 2 on a sidewalk, 3 off the road; `lateral` gets λ.
    int onRoad(int road, const vec3& p, float margin, float* lateral = nullptr) const;
    int sectionOfPoint(int road, const vec3& p, int s) const;
    vec3 centrePointAt(int road, float dist) const;
    float distanceAlongCentre(int road, const vec3& p) const;
    int carSection(int road, const vec3& p) const;
    bool facingRoadDirection(int road, const vec3& p, const vec3& carZ) const;

    // ---- Activation tables ----
    const std::vector<uint16_t>& ambientRoads(int room) const;
    const std::vector<uint16_t>& pedRoads(int room) const;
    // Roads in b's list but not a's (populate) and in a's but not b's (empty).
    void activationChange(int a, int b, std::vector<int>& populate, std::vector<int>& empty) const;

    // ---- Police routing graph ----
    struct Edge {
        int to = 0, road = 0;
        float cost = 0;
    };
    const std::vector<std::vector<Edge>>& graph() const { return graph_; }

    // Diagnostics for the native test.
    struct Stats {
        int mainRoads = 0, supRoads = 0, intersections = 0, tableRooms = 0;
        int listMismatches = 0;  // non-loop ends whose slot differs from the list position after loading
    };
    const Stats& stats() const { return stats_; }

private:
    void addComponent(int room, Component c);
    void registerRoadRooms(int r, int kind);
    void mergeShortcut(int r, int I, const CityData& city);
    void registerObstacles(const CityData& city, const Vfs& vfs);
    // The prop objects of each room in its list order at this moment (newest first): the city's
    // props and the colliding level objects (level-objects.md, "AI obstacles").
    struct ObstacleCandidate {
        int id = 0;  // obstacle list entry (CityData object, or kLevelObstacleBase + level object)
        vec3 pos;    // the prop's position (centre of gravity) / the level object's stored origin
        int collisionType = 0x10;
        float impulseLimit2 = 0;
        bool hasData = false;
    };
    std::vector<std::vector<ObstacleCandidate>> obstacleCandidates(const CityData& city, const Vfs& vfs);
    void mirrorForLeft();
    void applySpeedLimits();
    void buildBends();
    void buildGraph();

    bool loaded_ = false;
    std::vector<Road> roads_;
    std::vector<Intersection> ix_;
    int mainRoads_ = 0;
    std::vector<std::vector<Component>> rooms_;
    std::vector<std::vector<uint16_t>> ambientTable_, pedTable_;
    CityAimap cityAimap_;
    RaceAimap raceAimap_;
    std::string raceName_ = "roam";
    std::unique_ptr<TrafficControl> control_;
    std::vector<std::vector<Edge>> graph_;
    const CityData* city_ = nullptr;
    Stats stats_;
};

}  // namespace ai
}  // namespace cc
