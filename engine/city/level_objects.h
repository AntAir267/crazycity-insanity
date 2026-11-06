// Moving level objects (spec/game/level-objects.md): boats, drawbridge leaves, Underground trains
// and ferries, each read from its own path set and moved once per frame. The parked cars are
// breakable props of the city (CityData::placeParkedCars).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/game_random.h"
#include "core/math.h"
#include "formats/banger.h"
#include "formats/pkg.h"

namespace cc {

class Vfs;
class AssetCache;
class CityData;

// A follower on the closed Catmull-Rom curve through a path's points ("Following a path").
struct PathFollower {
    std::vector<vec3> p;
    float s = 0, t = 0;
    int i = 0;
    vec3 a, b, c, d;
    float L = 0;

    int n() const { return int(p.size()); }
    void setup(int seg);
    void restart();
    vec3 pointAt(float u) const;
    vec3 directionAt(float u) const;
    // Update by a time step τ; the point and direction at the new place.
    void update(float tau, vec3& point, vec3& dir);
};

// One object as the collision world and the renderer see it.
struct LevelObject {
    enum Kind { Boat, Leaf, TrainCar, Ferry };
    Kind kind = Boat;
    std::string model;
    std::shared_ptr<const Pkg> pkg;
    const Mesh* lod[4] = {nullptr, nullptr, nullptr, nullptr};
    float radius = 0;  // largest level radius of the model
    int set = 0;       // material set
    BangerData bd;     // its prop data (bound, material, CG)
    bool hasData = false;
    vec3 X{1, 0, 0}, Y{0, 1, 0}, Z{0, 0, 1}, o;  // the stored matrix
    int room = 0;
    vec3 ref;          // drawing by distance: a leaf's hinge, a ferry's position
    bool collides() const { return kind != Boat; }
    bool drawnByDistance() const { return kind == Leaf || kind == Ferry; }
};

class LevelObjects {
public:
    // The game's pathset for `word` (sailboat, bridge, train, ferry, parkedcar): race/<city>/
    // <city>_<word>_<race>.pathset outside Cruise if it exists, else <city>_<word>.pathset; "" if
    // neither exists. `race` empty = Cruise.
    static std::string pathsetFor(const Vfs& vfs, const std::string& raceDir, const std::string& word,
                                  const std::string& race);

    // Creation (level-objects.md, "Creation order"): boats, leaves, trains, ferries, in path order,
    // drawing from `rng` as "Randomness" says. Then the reset the game makes before its first frame.
    // `ferries` false: no ferries (network games); `openBridges`: every leaf in the "open" mode
    // whatever its path name says (network games in London, multiplayer.md 1.4).
    void create(const Vfs& vfs, AssetCache& assets, const CityData& city, Lcg& rng, const std::string& race,
                bool ferries = true, bool openBridges = false);
    void clear();
    // A race reset (boats, leaves, trains, ferries in creation order).
    void reset(const CityData& city);
    // One frame (not per physics step): boats, leaves, trains, ferries. `cars` are the body
    // positions of the cars registered for the proximity bridges (the player's).
    void update(float F, const CityData& city, const std::vector<vec3>& cars);

    const std::vector<LevelObject>& objects() const { return objs_; }
    // Rooms marked by the drawbridge leaves (skid marks are not laid in them).
    const std::vector<int>& bridgeRooms() const { return bridgeRooms_; }
    bool isBridgeRoom(int room) const;

    // Counts and states for tests.
    struct Stats {
        int boats = 0, leaves = 0, trains = 0, ferries = 0;
    };
    const Stats& stats() const { return stats_; }
    int leafState(int leaf) const { return leaves_[size_t(leaf)].state; }
    float leafAngle(int leaf) const { return leaves_[size_t(leaf)].angle; }
    const LevelObject& leafObject(int leaf) const { return objs_[size_t(leaves_[size_t(leaf)].obj)]; }
    int trainCount() const { return int(trains_.size()); }
    int trainState(int t) const { return trains_[size_t(t)].state; }
    bool trainForward(int t) const { return trains_[size_t(t)].forward; }
    int trainCarSegment(int t, int k) const { return trains_[size_t(t)].f[k].i; }
    int trainPoints(int t) const { return trains_[size_t(t)].f[0].n(); }
    enum LeafState { Closed = 0, Opening = 1, Open = 2, Closing = 3 };
    enum TrainState { Waiting = 0, Running = 1, Braking = 2, Accelerating = 3 };

private:
    struct Boat {
        int obj = 0;
        PathFollower f;
        float speed = 0;
    };
    struct Leaf {
        int obj = 0;
        int mode = 1;  // 0 inactive, 1 timed, 2 proximity, 3 open
        int partner = -1;
        vec3 H, PX, PY, PZ;  // hinge and placement rows
        float h = 0;         // Size.z · 0.5
        int state = Closed;
        float timer = 0, angle = 0;
    };
    struct Train {
        int obj[3] = {0, 0, 0};
        PathFollower f[3];
        int state = Waiting;
        bool forward = true;
        float timer = 0, factor = 1;
    };
    struct Ferry {
        int obj = 0;
        PathFollower f;
        float speed = 0;
    };
    void leafMatrix(Leaf& l);
    void moveTrainCar(Train& t, int k, float tau, const CityData& city);
    void placeFollowing(LevelObject& o, const vec3& point, const vec3& dir, bool turnOnly);

    std::vector<LevelObject> objs_;
    std::vector<Boat> boats_;
    std::vector<Leaf> leaves_;
    std::vector<Train> trains_;
    std::vector<Ferry> ferries_;
    std::vector<int> bridgeRooms_;
    Stats stats_;
};

// "Matrix from a direction" (level-objects.md): rows X, Y, Z renormalised from (1,0,0), (0,1,0), D.
void matrixFromDirection(const vec3& D, vec3& X, vec3& Y, vec3& Z);

}  // namespace cc
