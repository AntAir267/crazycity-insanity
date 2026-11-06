// The HUD map's rules (spec/game/hudmap.md): tune/<c>.mmHudMap, the map modes and keys, the
// rectangles and projection per mode, the map camera's zoom approach, the background colour and
// the marker shapes. Drawing is app/hud_map_view.h.
#pragma once

#include <string>

#include "core/math.h"

namespace cc {
namespace game {

struct HudMapTune {
    float sizeX = 0.21f, sizeY = 0.25f;  // small map, fractions of the screen
    float posX = 0.78f, posY = 0.75f;    // left and top edges (top measured from the top)
    bool hasZoomIn = false;
    int zoomIn = 0;
    float zoomInDist = 577.0f, zoomOutDist = 1195.0f;
    float zoomInDistFS = 786.0f, zoomOutDistFS = 1580.0f;
    float iconScaleMin = 34.0f, iconScaleMax = 52.5f;
    float iconScaleMinFS = 15.0f, iconScaleMaxFS = 18.0f;
    float approachRate = 1.2f;  // only a quoted "Approach Rate" key changes it
    bool hasOcean = false;      // read, never used
    vec3 ocean;
};
// Tune-file text (vehicle-tune.md "Syntax"; keys single tokens compared exactly, a quoted key
// is one token). False when the text does not start with "type: a".
bool parseHudMapTune(const std::string& text, HudMapTune& out);

struct MapRect {
    int x = 0, y = 0, w = 0, h = 0;  // left, top (from the top of the screen), width, height
};
// The map's rectangle in mode 1 – 3 (hudmap.md, "Map rectangle and projection").
// `rhdDash`: a right-hand-drive car in the dash view (mode 1 moves the map to the left edge).
MapRect mapRect(int mode, int W, int H, const HudMapTune& t, bool rhdDash);
// The 3D view's rectangle: whole screen (modes 0, 1), the top half (2), the corner (3).
MapRect viewRect(int mode, int W, int H, const HudMapTune& t);
// Fixed projection aspect: 1.25, or 2.5 in the half-screen mode.
float mapAspect(int mode);
constexpr float kMapFov = 60.0f, kMapNear = 10.0f, kMapFar = 1600.0f;

// Background ("sea") colour: London (234, 214, 198), every other city (21, 173, 234).
void mapBackground(const std::string& cityName, int rgb[3]);

// The map's state: mode, saved mode, orientation, zoom, camera height and marker size.
class HudMapState {
public:
    void setTune(const HudMapTune& t) { tune_ = t; }
    const HudMapTune& tune() const { return tune_; }
    void setAvailable(bool a) { available_ = a; }  // false: no hudmap_<c>.pkg (mode 0, keys do nothing)
    bool available() const { return available_; }

    // A reset of the player's car: the map is set to the saved mode (view byte 1).
    void resetToSaved(int savedMode);
    int mode() const { return mode_; }
    int modeBefore() const { return before_; }
    // Keys. Each returns true when the mode changed. `pauseToggle` is set when the full-screen
    // map pauses or unpauses a single-player game (Q in and out; Tab out leaves it paused).
    bool tab(bool& leftFullScreen);
    bool fullScreen(bool& enteredOrLeft);  // Q
    void zoom() {
        if (mode_) zoomedIn_ = !zoomedIn_;
    }  // E
    void orientation() {
        if (mode_) headingUp_ = !headingUp_;
    }  // F
    void setMode(int m);  // also snaps the height and marker size to their targets
    // Re-applies the mode (every view key does): snaps height and size.
    void reapply() { setMode(mode_); }

    bool zoomedIn() const { return zoomedIn_; }
    bool headingUp() const { return headingUp_; }
    void setZoomedIn(bool z) { zoomedIn_ = z; }
    void setHeadingUp(bool h) { headingUp_ = h; }

    // Per frame (dt keeps running while paused).
    void update(float dt);
    float height() const { return h_; }
    float markerSize() const { return s_; }
    float targetHeight() const;
    float targetSize() const;

private:
    HudMapTune tune_;
    bool available_ = true;
    int mode_ = 0, before_ = 0;
    bool zoomedIn_ = false, headingUp_ = true;
    float h_ = 1195.0f, s_ = 52.5f;
};

// Car marker corners in the car's ground frame, times the marker size s (tip at the front).
// `forward` and `right` are flat unit vectors; the marker is centred `lift` metres above `pos`.
void carMarkerCorners(const vec3& pos, const vec3& forward, const vec3& right, float s, vec3 out[3]);

}  // namespace game
}  // namespace cc
