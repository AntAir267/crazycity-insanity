// City view: list of cities, free-fly camera, position readout. The city itself (PSDL streets,
// INST props, sky) is built by city/ and drawn here.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/engine_context.h"
#include "city/city.h"
#include "formats/cinfo.h"
#include "render/camera.h"

namespace cc {

struct InputState;
struct CarEntry;

class CityView {
public:
    CityView();
    ~CityView();

    // MM2's city list from tune/*.cinfo, then the unused city/*.psdl layouts (city-files.md).
    void refreshCityList(const Vfs& vfs);
    const std::vector<CityEntry>& cities() const { return cities_; }

    bool loadCity(const CityEntry& city, EngineContext& ctx, const LevelSet& levelSet = LevelSet());
    void unload(EngineContext& ctx);
    bool loaded() const;

    void setOption(const std::string& name, int value, EngineContext& ctx);
    // Puts the given car on the street below the camera.
    bool dropCar(const CarEntry& car, int paint, EngineContext& ctx);

    void update(const InputState& in, float dt, EngineContext& ctx);
    void render(EngineContext& ctx, int width, int height);

    std::string infoJson() const;
    std::string readout() const;
    FreeFlyCamera& camera() { return cam_; }
    City* city() { return city_.get(); }

private:
    std::vector<CityEntry> cities_;
    std::unique_ptr<City> city_;
    struct DroppedCar {
        std::unique_ptr<VehicleModel> model;
        int paint = 0;
        mat4 world;
    };
    std::vector<DroppedCar> dropped_;
    CityOptions pending_;
    std::string message_;
    FreeFlyCamera cam_;
    float fps_ = 0;
};

}  // namespace cc
