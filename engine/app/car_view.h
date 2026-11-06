// Car view: one vehicle on a turntable, orbit/zoom camera, paint and level-of-detail choice.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/engine_context.h"
#include "model/vehicle.h"
#include "render/camera.h"

namespace cc {

struct InputState;

// One entry in the page's car list.
struct CarEntry {
    enum Group { Menu = 0, Unlisted = 1, Traffic = 2 };
    Group group = Menu;
    std::string baseName;
    std::string description;
    std::vector<std::string> colors;  // from the .info Colors= line (empty for packages without one)
    std::string archive;               // where the .info (or package) came from
    bool fromMod = false;
    bool builtIn = false;
    VehicleKind kind = VehicleKind::Player;
    int infoFlags = 0;  // .info Flags= (indoor chase camera bits)
};

class CarView {
public:
    // Loads the car; false if it cannot be shown (problems are logged).
    bool setCar(const CarEntry& entry, EngineContext& ctx);
    void clear();
    bool hasCar() const { return model_ != nullptr; }
    const CarEntry& entry() const { return entry_; }
    const VehicleModel* model() const { return model_.get(); }

    void setPaint(int colorIndex) { paint_ = colorIndex; }
    void setLod(Lod l) { lod_ = l; }
    int paint() const { return paint_; }
    Lod lod() const { return lod_; }
    void setOption(const std::string& name, int value);
    void resetCamera();

    void update(const InputState& in, float dt, int viewportHeight);
    void render(EngineContext& ctx, int width, int height);

    // Paint names (the .info colours, plus "Set n" for sets without a name), levels present,
    // parts and problems, for the side panel.
    std::string infoJson() const;

private:
    CarEntry entry_;
    std::unique_ptr<VehicleModel> model_;
    int paint_ = 0;
    Lod lod_ = LodH;
    StillCarOptions opt_;
    bool autoRotate_ = true;
    bool reflections_ = true;
    float spin_ = 0;
    float idle_ = 0;
    OrbitCamera cam_;
    vec3 pivot_;            // turntable centre (car space)
    float fitSize_ = 1.0f;  // largest dimension, for framing
    bool needsFit_ = false; // frame the car on the next render (needs the aspect ratio)
};

}  // namespace cc
