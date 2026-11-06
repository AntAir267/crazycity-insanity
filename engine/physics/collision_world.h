// The physical world of a city (or the flat world) for the car: materials, city collision
// polygons, placed collision objects, the wheel probe, per-sample body collision, world limits
// (spec/physics/collision.md and its companion pages).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "physics/bound.h"
#include "physics/city_collision.h"
#include "physics/contacts.h"
#include "physics/loose_bodies.h"
#include "physics/material.h"
#include "physics/world.h"

namespace cc {

class Vfs;
class CityData;
class AssetCache;

namespace phys {

class Car;
struct SceneMover;
}  // namespace phys
class LevelObjects;
namespace phys {

// Moved = a moved-prop slot's object (loose-bodies.md), kept apart from the city's objects.
// Gizmo = a drawbridge leaf, Underground train car or ferry (level-objects.md): collides as an
// immovable landmark with its prop data's bound and moves once per frame.
enum class ObjKind { Landmark, Spanning, Prop, Moved, Gizmo };
enum class PropPrim { Geometry = 0, Box = 1, Capsule = 2, Sphere = 3 };

struct CollisionObject {
    ObjKind kind = ObjKind::Landmark;
    std::shared_ptr<Bound> bound;  // geometry, terrain or box (null for capsule/sphere props)
    Mtx O;                         // collision matrix (prop: origin at its centre of gravity)
    vec3 position;                 // = O.o
    float radius = 0;
    bool ground = false;           // wheels drive on it (probe mask "ground")
    // props
    PropPrim prim = PropPrim::Box;
    Material material;             // a prop's one material
    vec3 size, cg, placementPos;
    float yRadius = 0, impulseLimit2 = 0;
    int collisionType = 0;
    bool broken = false;           // out of collision: a broken prop, or a slot that is not collidable
    int cityObject = -1;           // index in CityData::objects()
    int identity = 0;              // push-source identity
    std::shared_ptr<LooseData> loose;  // props: what its test body and moved copy are made of
    int slot = -1;                 // Moved: the slot number
    int room = 0;                  // props and moving objects: the room whose list holds it
    int set = 0;                   // props: paint set (a broken prop's copy keeps it)
    long long filed = 0;           // filing stamp in its room's list (fileStamp; 0: filed at setup
                                   // or a reset, older than anything filed while driving)
};

// Car-side state the world needs (kept by the car between samples).
struct CarCollisionState {
    int room = 0;                  // Find(reference point, previous room)
    bool roomPending = true;       // placed: find the room with no hint before the first step
    Mtx prevMatrix;                // M0 (before any lastPush shift)
    int prevLargestSource = 0;     // identity of the largest push in the previous sample (0 = none)
    int largestSource = 0;         // ... in the current sample
    float largestPush2 = 0;
    bool water = false;            // water effect on
    float waterLevel = 0;
    float buoyancy = 0.7f;
    float waterTimer = 0;
    // Drive trace: the impacts of the current step in resolution order (only while tracing).
    struct TraceImpact {
        int kind = 0;  // 0 city, 1 obj, 2 prop, 3 loose
        Impact im;
        int obj = -1;
    };
    bool tracing = false;
    std::vector<TraceImpact> traceImpacts;
};

class CollisionWorld : public World {
public:
    // Flat world: one endless polygon at y = h with the `cobblestone` material.
    void setupFlat(const Vfs& vfs, float h);
    // City world from loaded city data. `streetProps` false leaves out the street props and the
    // props.pathset props (drive-check.md, "Street props in city runs"); `levelObjects` false the
    // level objects and parked cars (level-objects.md; not part of the check runs).
    void setupCity(const Vfs& vfs, const CityData& city, bool streetProps = true, bool levelObjects = true);

    bool probe(const vec3& S, const vec3& E, ProbeState& state, ProbeHit& hit) override;
    const char* name() const override { return flat_ ? "flat" : "city"; }

    void placeCar(Car& car) override;
    void afterCarUpdate(Car& car, float dt) override;
    // Step 12 of the car's physics step (collision.md "The physics step", 2-3).
    void collideCar(Car& car, float dt, float invdt) override;

    // Camera probes: the city and the solid objects, no ignore.
    bool probeStatic(const vec3& S, const vec3& E, ProbeHit& hit);
    // Find(p, hint) of city-collision.md (0 on the flat world).
    int findRoomAt(const vec3& p, int hint) const;

    MaterialTable& materials() { return table_; }
    const CityData* city() const { return city_; }
    bool isFlat() const { return flat_; }
    const std::vector<CollisionObject>& objects() const { return objs_; }
    // The collision object id (object()) of level object i (LevelObjects::objects()), −1 if it
    // does not collide.
    int levelObjectId(int i) const {
        return i >= 0 && size_t(i) < gizmoOf_.size() && gizmoOf_[size_t(i)] >= 0 ? firstGizmo_ + gizmoOf_[size_t(i)] : -1;
    }
    size_t cityPolygonCount() const { return coll_.polygonCount(); }
    const CityCollision& cityCollision() const { return coll_; }
    // Props broken so far (by CityData object index), for the renderer.
    const std::vector<int>& brokenProps() const { return broken_; }
    bool cityObjectBroken(int cityObject) const {
        return cityObject >= 0 && size_t(cityObject) < brokenFlags_.size() && brokenFlags_[size_t(cityObject)] != 0;
    }
    // Every prop stands again and every loose body goes (a new drive, a race or world-limit reset).
    void resetProps();
    // multiplayer.md 1.4: a network Circuit makes some props sturdier — the prop data `name`
    // (its Mass and ImpulseLimit2) multiplied by `k` (1 = as loaded; repeated calls do not
    // accumulate).
    void setPropDataScale(const std::string& name, float k);

    // ---- Pieces of a mover's collision (collision.md, car-collision.md 1.2) ----
    // Identities (collision-object-contacts.md, "Push source and the previous matrix").
    static constexpr int kCityIdentity = 1;
    static constexpr int kFixedIdentity = 2;      // fixed objects and traffic cars without a body
    static constexpr int kLaneFirstIdentity = 3;  // a body-less traffic car as the first side
    static constexpr int kPropIdentityBase = 1000;
    // The bound against the city polygons of `room` (or the flat world). M0 already shifted.
    void cityImpacts(const Bound& bound, const Mtx& M, const Mtx& M0, const Material& mat, int room, float dt,
                     std::vector<Impact>& out);
    // Gathering (collision-objects.md): `statics` landmarks and spanning objects, `props` props.
    void gatherObjects(const vec3& ref, float radius, int room, bool statics, bool props, std::vector<int>& out) const;
    // One room's part of the gathering, appended to `out` while `count` < 32 (count = everything
    // gathered so far, lane cars included).
    void gatherRoom(int r, const vec3& ref, float radius, bool statics, bool props, std::vector<int>& out, size_t& count) const;
    // One object of a room's list for gathering: can it collide and does the trivial test pass?
    bool gatherTest(int oi, const vec3& ref, float radius, bool statics, bool props) const;
    // A room's object list (front first; empty for room 0 or out of range).
    const std::vector<int>& roomList(int r) const;
    // The room lists' filing clock (loose-bodies.md 5.4): one list per room holds the moved props,
    // the moving level objects, the cable cars and the traffic cars without a body, newest filed
    // first; the traffic stamps its cars from this same clock so they interleave by filing time.
    long long fileStamp() { return ++fileClock_; }
    // The bound against object oi (impacts with A = the mover, B = the object); false if the object
    // is skipped (broken, sphere prop). `capsule`: the mover is a loose capsule copy (its data; M,
    // M0 its matrix and shifted previous matrix), tested with its capsule against polygon objects
    // (loose-bodies.md 6.3, "Capsules in these tests"; `bound` is then its box, used for the rest).
    bool objectImpacts(int oi, const Bound& bound, const Mtx& M, const Mtx& M0, const Material& mat, float dt,
                       std::vector<Impact>& out, const LooseData* capsule = nullptr);
    CollisionObject& object(int oi) {
        return oi >= firstGizmo_ ? gizObjs_[size_t(oi - firstGizmo_)]
                                 : oi >= firstSlot_ ? slotObjs_[size_t(oi - firstSlot_)] : objs_[size_t(oi)];
    }
    const CollisionObject& object(int oi) const {
        return oi >= firstGizmo_ ? gizObjs_[size_t(oi - firstGizmo_)]
                                 : oi >= firstSlot_ ? slotObjs_[size_t(oi - firstSlot_)] : objs_[size_t(oi)];
    }
    // ---- Moving objects (level-objects.md, cable-cars.md) ----
    // The level objects' matrices and rooms after their frame update (a room change files the
    // object at the front of its new room's list).
    void syncLevelObjects(const LevelObjects& lo);
    // A breakable prop that moves (a cable car): filed at the front of `room`; returns its id.
    int addDynamicProp(const std::shared_ptr<LooseData>& data, const Mtx& M, int room);
    void moveDynamicProp(int oi, const Mtx& M, int room);
    void clearDynamicProps();
    // A broken prop comes back untouched where it stands (its moved copy is removed).
    void restoreProp(int oi);
    // The object id of the drive trace (drive-trace.md): the position among the city's collidable
    // objects, or 100000 + the slot number for a moved-prop slot.
    int traceId(int oi) const {
        return oi >= firstGizmo_ ? -2 : oi >= firstSlot_ ? kSlotIdBase + (oi - firstSlot_) : oi;
    }
    // Visual hooks (engine/effects.md, "Props"; never change the physics). The prop emitter
    // belongs to the pool body (key = the pool element): onLooseActive every time an object gets a
    // body (an untouched prop before the responses of a test, each piece, a moved prop hit again,
    // a thrown part: its prop data name, its matrix, whether it was untouched before this hit);
    // onLooseFrame in the after-samples step for each loose mover whose body stays (its object's
    // matrix); onLooseInactive when the body is let go or dropped. onPropBroken: a prop broken by
    // a test (object index).
    std::function<void(int oi)> onPropBroken;
    std::function<void(int key, const std::string& name, const Mtx& M, bool untouched)> onLooseActive;
    std::function<void(int key, const Mtx& M)> onLooseFrame;
    std::function<void(int key)> onLooseInactive;
    const MaterialTable& table() const { return table_; }
    void resetPlacedObjects() override { resetProps(); }
    float waterLevel() const { return waterLevel_; }

    // ---- Loose bodies (loose-bodies.md) ----
    static constexpr int kBodyCount = 32, kSlotCount = 40, kSlotIdBase = 100000;
    static constexpr int kLooseIdentityBase = 500;  // + pool element
    enum class Join { Simple, Replacing, LyingStill };  // 5.3
    LooseBody& looseBody(int e) { return bodies_[size_t(e)]; }
    const LooseBody& looseBody(int e) const { return bodies_[size_t(e)]; }
    const MovedSlot& movedSlot(int s) const { return slots_[size_t(s)]; }
    int slotObject(int s) const { return firstSlot_ + s; }
    int bodiesInUse() const { return inUse_; }
    int bodyAt(int listPos) const { return order_[size_t(listPos)]; }
    // The movers of the running frame (the loose system appends to them and switches them off).
    void setLooseMovers(std::vector<SceneMover>* movers) { movers_ = movers; }
    // 5.1: the pool drops still, fallen and room-0 bodies; the bodies in use are declared as movers
    // (appended to `movers`); first-kind movers outside the player's rooms are removed.
    // `extraRooms`: the rooms of race opponents within 200 m of a player (opponents.md, "One
    // frame" step 3): each with its neighbours, after the player's rooms, at most 20 in all.
    void declareLoose(std::vector<SceneMover>& movers, int playerRoom, const std::vector<int>* extraRooms = nullptr);
    // 5.2, after the frame's samples.
    void afterLooseSamples(std::vector<SceneMover>& movers);
    // 6.1 and 6.4 for pool element e.
    void looseUpdate(int e, float dt, float invdt);
    void looseEnd(int e);
    // 2.1: a body for untouched prop oi before the response of a test that touched it.
    int propTestBody(int oi);
    // After that test (collision.md, "Breakable props"): it breaks (3, 8) or its body is let go.
    // `v` is the hitting body's velocity (glass panes).
    void finishPropTest(int oi, int e, bool broke, const vec3& v);
    // 2.1 / 5.3: a moved prop lying still that a test touched gets a body and joins the movers.
    int slotTestBody(int slot);
    // 2.3.
    void letGo(int e);
    // A mover entry for body e during a sample (5.3); false if refused.
    bool joinMovers(int e, Join kind);
    // 4: throw car part `part` (index in the car's tune list) with momentum P and angular impulse H.
    void throwCarPart(Car& car, int part, const vec3& P, const vec3& H) override;
    // 7: a car's reset takes its thrown parts back.
    void takeBackCarParts(const Car& car) override;
    // Events since the last call (drive trace, log).
    std::vector<LooseEvent> takeLooseEvents() {
        std::vector<LooseEvent> e;
        e.swap(events_);
        return e;
    }
    const std::vector<PaneBurst>& paneBursts() const { return panes_; }

private:
    std::map<std::string, std::pair<float, float>> propScaleBase_;  // setPropDataScale: loaded values
    void buildObjects(const Vfs& vfs, const CityData& city, bool streetProps, bool levelObjects);
    void buildGizmos(const Vfs& vfs, const CityData& city);
    void refileMoving();
    void fileAtFront(int oi, int room);
    void unfile(int oi, int room);
    void resetLoose();
    int takeBody();
    void detachBody(int e);
    int takeSlot();
    void clearSlot(int s);
    void slotToRoomFront(int s, int room);
    void slotLeaveRoom(int s);
    void syncSlotObject(int s);
    void setupBody(int e, const LooseData& d, const Mtx& M);
    void switchOffMover(int e);
    const LooseData* pieceData(int oi, int i);
    bool probeObject(const CollisionObject& o, const vec3& S, const vec3& E, float& best, ProbeHit& hit);
    void gather(const Car& car, const vec3& ref, float radius, int room, std::vector<int>& out) const;
    SurfaceMaterial surfaceOf(const Material& m) const;

    bool flat_ = true;
    long long fileClock_ = 0;
    float flatY_ = 0;
    Material flatMaterial_;
    int flatCode_ = 0;
    MaterialTable table_;
    const CityData* city_ = nullptr;
    CityCollision coll_;
    std::vector<CollisionObject> objs_;
    std::vector<std::vector<int>> roomObjects_;
    std::vector<std::vector<int>> roomObjectsAtReset_;
    std::vector<int> broken_;
    std::vector<uint8_t> brokenFlags_;
    float waterLevel_ = 0;
    std::vector<const CollPoly*> scratch_;
    const Vfs* vfs_ = nullptr;
    // Loose bodies.
    int firstSlot_ = 0x7fffffff;    // object index of slot 0 (= the number of city objects)
    int firstGizmo_ = 0x7fffffff;   // object index of the first moving object (slots + 40)
    std::vector<CollisionObject> gizObjs_;  // level objects, then dynamic props
    size_t levelGizmos_ = 0;                // how many of gizObjs_ are level objects
    std::vector<int> gizmoOf_;              // level object index → gizObjs_ index (−1: not collidable)
    LooseBody bodies_[kBodyCount];
    int order_[kBodyCount] = {};
    int inUse_ = 0;
    MovedSlot slots_[kSlotCount];
    CollisionObject slotObjs_[kSlotCount];
    int ring_ = 0;
    std::vector<SceneMover>* movers_ = nullptr;
    std::vector<LooseEvent> events_;
    std::vector<PaneBurst> panes_;
    int paneNext_ = 0;
    std::map<std::string, std::shared_ptr<LooseData>> pieces_;  // "<prop>_BREAKnn"
};

}  // namespace phys
}  // namespace cc
