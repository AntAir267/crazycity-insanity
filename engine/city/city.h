// A loaded city for drawing: PSDL surfaces (grouped in spatial chunks), INST objects, sky, fog
// and lights, plus cars placed in it. Drawing follows spec/formats/psdl.md, inst.md,
// city-files.md and car-model.md. The CPU-side data lives in CityData.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ai/pedestrians.h"
#include "ai/police.h"
#include "ai/traffic.h"
#include "app/engine_context.h"
#include "city/city_data.h"
#include "city/psdl_geometry.h"
#include "formats/texture_lookup.h"
#include "core/math.h"
#include "model/light_bar.h"
#include "model/parts.h"
#include "model/vehicle.h"
#include "physics/collision_world.h"

namespace cc {

struct GpuMesh;
struct GpuTexture;
struct Pkg;
struct SimpleVertex;
struct Billboard;
struct DecalVertex;
namespace fx {
class CarFx;
class EffectsWorld;
}

struct CityOptions {
    int timeOfDay = 1;   // 0 morning, 1 noon, 2 evening, 3 night
    int weather = 0;     // 0 clear, 1 cloudy, 2 foggy, 3 rain
    bool props = true;
    bool fog = true;
    bool sky = true;
    bool reflections = true;  // "Vehicle Reflections"
    int objectDetail = 3;     // inst.md object detail setting 0..3 (MM2 picks 3 on modern machines)
    int wallShading = 0;      // viewer.md "Wall shading": 0 Soft (default), 1 MM2 exact, 2 Flat
    bool aiOverlay = false;   // debug overlay: AI lanes, intersections, light states
};

// A car to draw in the city this frame.
struct CityCar {
    const VehicleModel* model = nullptr;
    mat4 world = mat4::identity();  // model frame (car space -> world)
    int paint = 0;
    int room = 0;                   // 0 = unknown (looked up)
    bool bodyHidden = false;        // bumper/dash camera: not drawn (cameras.md)
    bool shadowHidden = false;      // dash camera: the car's own shadow is not drawn either (QUESTIONS 33)
    WheelPoseFn wheelPose;          // moving wheels (optional)
    bool braking = false;           // brake light glows (night.md, section 4); a trailer: its TLIGHT mesh
    bool reverse = false;           // reversing light glow
    const LightBar* lightBar = nullptr;  // police light bar (glows, flashing headlights, lens flares)
    fx::CarFx* fx = nullptr;        // visual effects: texture damage at H and M (damage-visuals.md)
};

class City {
public:
    City();
    ~City();

    // `levelSet`: which moving level objects the game creates (network games differ, multiplayer.md 1.4).
    bool load(const std::string& name, EngineContext& ctx, std::string& message, const std::string& raceDir = "",
              const LevelSet& levelSet = LevelSet());
    void setOptions(const CityOptions& opt, EngineContext& ctx);
    const CityOptions& options() const { return opt_; }
    const CityData& data() const { return data_; }

    void update(float dt);
    // Moving level objects (level-objects.md): once per frame after the AI update and before the
    // physics while a drive runs (`cars`: the player's body position for the proximity bridges);
    // the viewer moves them in update() when no drive runs. A race reset puts them back.
    void updateLevelObjects(float F, const std::vector<vec3>& cars);
    void resetLevelObjects();
    void setDriving(bool d) { driving_ = d; }
    // AI reset (session start): the light cycle restarts and the stop-sign queues empty.
    void resetTraffic();
    // Ambient traffic of a drive (traffic.md): pool for `density` (0 = none), then a reset around
    // the players. While it runs, the traffic update advances the lights.
    void startTraffic(EngineContext& ctx, float density, const std::vector<ai::TrafficPlayer>& players);
    void stopTraffic();
    ai::TrafficSystem& traffic() { return traffic_; }
    const ai::TrafficSystem& traffic() const { return traffic_; }
    // The police of a drive (police.md): the cops of roam.aimap (none when `on` is false), parked.
    void startPolice(EngineContext& ctx, bool on, const std::string& playerCar, std::shared_ptr<phys::Bound> playerBound);
    // Race modes: the cops of the race file at cop density `density` (police.md, "How many cops").
    void startPoliceDensity(EngineContext& ctx, float density, const std::string& playerCar,
                            std::shared_ptr<phys::Bound> playerBound);
    void stopPolice();
    ai::PoliceSystem& police() { return police_; }
    const ai::PoliceSystem& police() const { return police_; }
    // The pedestrians of a drive (pedestrians.md): a pool of `count` (0 = none; the rain models when
    // `rain`), then a reset around the players.
    void startPedestrians(EngineContext& ctx, int count, bool rain, const std::vector<ai::PedPlayer>& players);
    void stopPedestrians();
    ai::PedestrianSystem& pedestrians() { return peds_; }
    const ai::PedestrianSystem& pedestrians() const { return peds_; }
    // Pedestrians drawn in the last frame (near figures, far strips).
    int pedsDrawnNear() const { return pedsNear_; }
    int pedsDrawnFar() const { return pedsFar_; }
    // Debug view: 0 as MM2 (near figures within 35 m, strips beyond), 1 strips only, 2 figures only.
    void setPedDetailDebug(int v) { pedDetailDebug_ = v; }
    // `farPlane` clamps the fog (city-files.md). `cars` are drawn after the city, then their shadows.
    // `fullMap` (viewer.md, "Draw distance"): no fog and nothing dropped for distance (levels of
    // detail still switch), the sky scaled inside the far plane.
    void render(EngineContext& ctx, const mat4& view, const mat4& proj, const vec3& eye, float farPlane,
                const std::vector<CityCar>& cars, bool fullMap = false);
    // A far plane past the city's farthest corner from `eye` (Full map draw distance).
    float fullMapFar(const vec3& eye) const;

    // Highest street/ground surface at (x, z) that is not above maxY; false if none.
    bool groundBelow(float x, float z, float maxY, float& y) const;

    const std::string& name() const { return data_.name(); }
    std::string summary() const;
    vec3 clearColor() const;  // fog colour of the current conditions
    int roomAt(const vec3& p, int hint = 0) const { return data_.findRoom(p, hint); }

    // Object detail thresholds (inst.md table).
    struct Detail {
        float roomLimit, t3, t2, t1;
    };
    Detail detail() const;

    // The effects drawn with the city (engine/effects.md): skid marks in the decal pass, the
    // effects pass after the glows, the rain. `mainView`: this view decides where the rain falls
    // (not the mirror).
    void setEffects(fx::EffectsWorld* e, bool mainView = true) {
        effects_ = e;
        effectsMain_ = mainView;
    }

    // The city's physical world (collision.md and its pages): wheels, body, cameras. Broken props
    // are not drawn.
    phys::CollisionWorld& collision() { return collision_; }

private:
    struct Chunk {
        GpuMesh* gpu = nullptr;
        vec3 centre;
        float radius = 0;
    };

    void buildSurfaces(EngineContext& ctx);
    TextureCondition textureCondition() const;
    void fetchSlotTextures(EngineContext& ctx);
    void freeSurfaces();
    bool canBlend(const Pkg* pkg, EngineContext& ctx);
    void drawCar(EngineContext& ctx, const CityCar& car, const vec3& eye, const vec3& viewDir);
    Lod carLod(const CityCar& car, const vec3& eye, const vec3& viewDir) const;
    void drawGlows(EngineContext& ctx, const std::vector<CityCar>& cars, const vec3& eye, const vec3& viewDir,
                   const mat4& viewProj, bool fullMap);
    uint32_t glowRand_ = 12345;  // visual-only random numbers (lamp flicker)
    void drawTrafficLights(EngineContext& ctx, const vec3& eye, const vec3& viewDir, const mat4& viewProj, bool fullMap);
    void drawAiOverlay(EngineContext& ctx);
    void drawDecals(EngineContext& ctx, const vec3& eye, const vec3& viewDir, int camRoom, bool fullMap);
    std::vector<std::vector<DecalVertex>> decalTris_;
    void drawTraffic(EngineContext& ctx, const vec3& eye, const vec3& viewDir, const mat4& viewProj, bool fullMap);
    void drawTrafficShadows(EngineContext& ctx, const vec3& eye, const vec3& viewDir, bool fullMap);
    void drawTrafficGlows(EngineContext& ctx, const vec3& eye, const vec3& viewDir, bool fullMap,
                          std::vector<Billboard>& heads);
    ai::TrafficSystem traffic_;
    bool trafficRunning_ = false;
    bool driving_ = false;
    ai::PedestrianSystem peds_;
    std::vector<GpuMesh*> pedMeshes_;  // one streamed mesh per pedestrian (posed every frame)
    GpuMesh* pedFarMesh_ = nullptr;    // every far pedestrian's strips of the frame (world space)
    Pkg pedStripPkg_;                  // one white untextured material for the strips
    int pedsNear_ = 0, pedsFar_ = 0;
    int pedDetailDebug_ = 0;
    void drawPedestrians(EngineContext& ctx, const mat4& view, const vec3& eye, const vec3& viewDir, const mat4& viewProj,
                         bool fullMap);
    void freePedMeshes();
    ai::PoliceSystem police_;
    void buildAiOverlay();
    struct LightMeshes {
        const Pkg* pkg = nullptr;
        const Mesh* m[10] = {};  // REDGLOW, YELLOWGLOW, GREENGLOW, WALK, NOWALK: DAY then NIGHT
    };
    std::vector<LightMeshes> lightMeshes_;
    const LightMeshes& lightMeshesOf(const Pkg* pkg);
    std::vector<SimpleVertex> overlayStatic_;
    bool overlayBuilt_ = false;

    CityData data_;
    vec3 extentMin_, extentMax_;  // PSDL bounds and every object's sphere
    CityOptions opt_;
    std::vector<const GpuTexture*> slotTextures_;
    unsigned slotGeneration_ = ~0u;
    std::vector<Chunk> chunks_;
    std::vector<Chunk> roomChunks_;  // per room (gpu null: nothing to draw)
    // The frame's room list (cpvs.md) in the "MM2" draw distance; empty use = every room.
    bool useRoomList_ = false;
    std::vector<int> roomList_;
    std::vector<uint8_t> roomListed_;
    int pvsCamRoom_ = 0, pvsRowRoom_ = -1;
    std::vector<uint8_t> pvsRow_;
    std::vector<std::vector<int>> spanRooms_;  // rooms a spanning object is filed in
    void makeRoomList(const mat4& view, const mat4& proj, const vec3& eye);
    bool listed(int room) const {
        return !useRoomList_ || (room > 0 && size_t(room) < roomListed_.size() && roomListed_[size_t(room)]);
    }
    std::shared_ptr<const Pkg> skyPkg_;
    const Mesh* skyMesh_ = nullptr;
    float skyAngle_ = 0;
    size_t triangles_ = 0;
    std::vector<std::pair<const Pkg*, bool>> blendCache_;
    int lastCamRoom_ = 0;
    phys::CollisionWorld collision_;
    fx::EffectsWorld* effects_ = nullptr;
    bool effectsMain_ = true;
    void placeRain(const mat4& view, const vec3& eye);

    // Ground lookup grid over upward-facing surface triangles.
    float cellSize_ = 16.0f;
    vec3 gridMin_;
    int gridW_ = 0, gridH_ = 0;
    std::vector<std::vector<uint32_t>> grid_;
    std::vector<vec3> groundTris_;  // 3 per triangle
};

}  // namespace cc
