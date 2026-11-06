// The parts of the HUD drawn in 3D (spec/game/hudmap.md, spec/game/hud.md): the HUD map with its
// markers, the direction arrow and the opponent markers. The 2D parts (gauges, timers, messages,
// panel) are drawn by the page from the HUD state.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/engine_context.h"
#include "core/math.h"
#include "game/hud_map.h"

namespace cc {

struct Pkg;
struct Mesh;
struct CamPose;

class HudMapView {
public:
    // geometry/hudmap_<c>.pkg, geometry/hudmap_square.pkg and tune/<c>.mmHudMap. False (map
    // disabled for the game) without the map package.
    bool load(EngineContext& ctx, const std::string& mapName);
    bool available() const { return map_ != nullptr; }
    game::HudMapState& state() { return state_; }
    const game::HudMapState& state() const { return state_; }
    const game::HudMapTune& tune() const { return state_.tune(); }

    struct CarMarker {
        vec3 pos;      // the car body's position
        vec3 forward;  // the car's forward direction
        uint8_t rgb[3] = {255, 0, 0};
        uint8_t a = 255;      // alpha (network slots 6 and 7, multiplayer.md 1.5)
        bool player = false;  // black outline 1.3 times the size under a yellow triangle
    };
    struct Dot {
        vec3 pos;
        int set = 2;  // hudmap_square material set: 0 red, 1 blue, 2 green, 3 grey, 4 yellow, 5 finish, …
    };
    // Draws the map into `rect` (top-left origin) of a W × H screen: clear to the city's sea
    // colour, the map plane, the dots, then the car markers (player last). `carPos` and `carZ`
    // are the player's body position and backward axis.
    void draw(EngineContext& ctx, const game::MapRect& rect, int W, int H, const vec3& carPos, const vec3& carZ,
              const std::vector<Dot>& dots, const std::vector<CarMarker>& cars, const std::string& cityName);

private:
    std::shared_ptr<const Pkg> map_, square_;
    std::vector<const Mesh*> mapMeshes_;
    const Mesh* squareMesh_ = nullptr;
    std::string name_;
    game::HudMapState state_;
};

// The direction arrow (hud.md): geometry/hudarrow01.pkg floating in front of the camera.
class HudArrow {
public:
    bool load(EngineContext& ctx);
    // Draws it pointing horizontally at `target`; green (set 0) while the target is in front of
    // the camera, yellow (set 1) behind.
    void draw(EngineContext& ctx, const CamPose& cam, float fovDeg, float aspect, const vec3& target);

private:
    std::shared_ptr<const Pkg> pkg_;
    const Mesh* mesh_ = nullptr;
    int set_ = 0;
};

// Opponent markers (hud.md): camera-facing triangles above objects, in the object's colour.
struct WorldMarker {
    vec3 pos;
    uint8_t rgb[3] = {180, 0, 255};
    float lift = 5.0f;  // metres above the object
};
void drawWorldMarkers(EngineContext& ctx, const CamPose& cam, const std::vector<WorldMarker>& m);

}  // namespace cc
