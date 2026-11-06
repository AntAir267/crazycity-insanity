#include "app/city_view.h"

#include <cmath>

#include "app/car_view.h"
#include "city/city.h"
#include "core/json.h"
#include "core/log.h"
#include "core/strutil.h"
#include "platform/platform.h"
#include "render/renderer.h"
#include "render/texture_cache.h"
#include "vfs/vfs.h"

namespace cc {

CityView::CityView() = default;
CityView::~CityView() = default;

void CityView::refreshCityList(const Vfs& vfs) { cities_ = buildCityList(vfs); }

bool CityView::loadCity(const CityEntry& entry, EngineContext& ctx, const LevelSet& levelSet) {
    const std::string& name = entry.mapName;
    CityOptions keep = city_ ? city_->options() : pending_;
    dropped_.clear();
    city_.reset();
    auto c = std::make_unique<City>();
    std::string msg;
    c->setOptions(keep, ctx);
    if (!c->load(name, ctx, msg, entry.raceDir, levelSet)) {
        logError("cannot load city %s: %s", name.c_str(), msg.c_str());
        message_ = msg;
        return false;
    }
    message_ = msg;
    // Fly camera starts above the first eligible free-roam start (controls.md), facing north.
    const auto& starts = c->data().starts();
    if (!starts.empty()) {
        cam_.pos = starts.front().pos + vec3(0, 3.0f, 0);
    } else {
        const Psdl& p = c->data().psdl();
        vec3 mid = (p.boundsMin + p.boundsMax) * 0.5f;
        cam_.pos = vec3(mid.x, p.boundsMax.y + 20.0f, mid.z);
    }
    cam_.yaw = 0;
    cam_.pitch = -0.05f;
    cam_.speed = 20.0f;
    city_ = std::move(c);
    return true;
}

void CityView::unload(EngineContext& ctx) {
    dropped_.clear();
    city_.reset();
}
bool CityView::loaded() const { return city_ != nullptr; }

void CityView::setOption(const std::string& name, int value, EngineContext& ctx) {
    if (!city_) {
        // Remember for the next city.
        if (name == "tod") pending_.timeOfDay = value;
        else if (name == "weather") pending_.weather = value;
        else if (name == "detail") pending_.objectDetail = value;
        else if (name == "walls") pending_.wallShading = value;
        else if (name == "aiOverlay") pending_.aiOverlay = value != 0;
        return;
    }
    CityOptions o = city_->options();
    if (name == "tod") o.timeOfDay = value & 3;
    else if (name == "weather") o.weather = value & 3;
    else if (name == "props") o.props = value != 0;
    else if (name == "fog") o.fog = value != 0;
    else if (name == "sky") o.sky = value != 0;
    else if (name == "detail") o.objectDetail = value;
    else if (name == "walls") o.wallShading = value;
    else if (name == "reflections") o.reflections = value != 0;
    else if (name == "aiOverlay") o.aiOverlay = value != 0;
    city_->setOptions(o, ctx);
}

bool CityView::dropCar(const CarEntry& car, int paint, EngineContext& ctx) {
    if (!city_) return false;
    // A little ahead of the camera, on the surface below that point.
    vec3 f = cam_.forward();
    vec3 flat = normalize(vec3(f.x, 0, f.z));
    vec3 p = cam_.pos + flat * 6.0f;
    float gy;
    if (!city_->groundBelow(p.x, p.z, cam_.pos.y + 0.5f, gy)) {
        p = cam_.pos;
        if (!city_->groundBelow(p.x, p.z, cam_.pos.y + 0.5f, gy)) return false;
    }
    auto m = std::make_unique<VehicleModel>();
    if (!loadVehicle(*ctx.assets, car.baseName, car.kind, *m, false)) return false;
    DroppedCar d;
    d.paint = paint;
    d.world = mat4::translate(vec3(p.x, gy - m->groundY, p.z)) * mat4::rotateY(cam_.yaw);
    d.model = std::move(m);
    dropped_.push_back(std::move(d));
    if (dropped_.size() > 24) dropped_.erase(dropped_.begin());
    return true;
}

void CityView::update(const InputState& in, float dt, EngineContext& ctx) {
    cam_.update(in, dt);
    if (city_) city_->update(dt);
    if (dt > 0) fps_ = fps_ * 0.9f + (1.0f / dt) * 0.1f;
}

void CityView::render(EngineContext& ctx, int width, int height) {
    Renderer& r = *ctx.renderer;
    float aspect = float(width) / float(height > 0 ? height : 1);
    // Draw distance (viewer.md): MM2 = 600 m for the fly camera, else the chosen metres, or the
    // whole city with logarithmic depth.
    const bool fullMap = ctx.drawDistance < 0 && city_;
    const float farPlane = fullMap ? city_->fullMapFar(cam_.pos) : ctx.drawDistance > 0 ? float(ctx.drawDistance) : 600.0f;
    const mat4 proj = mat4::perspective(60.0f * kPi / 180.0f, aspect, 0.3f, farPlane);
    const mat4 view = cam_.view();
    if (!city_) {
        r.beginFrame(width, height, vec3(0.5f, 0.6f, 0.7f));
        r.drawGradient(vec3(0.45f, 0.6f, 0.8f), vec3(0.75f, 0.8f, 0.85f));
        r.setCamera(view, proj, cam_.pos);
        // A 2 km grid so the camera has something to fly over until a city is loaded.
        std::vector<SimpleVertex> lines;
        for (int i = -20; i <= 20; ++i) {
            uint8_t a = (i % 5 == 0) ? 200 : 90;
            auto v = [&](float x, float z) {
                SimpleVertex s;
                s.pos = vec3(x, 0, z);
                s.color[0] = 40; s.color[1] = 50; s.color[2] = 60; s.color[3] = a;
                return s;
            };
            lines.push_back(v(float(i) * 50, -1000));
            lines.push_back(v(float(i) * 50, 1000));
            lines.push_back(v(-1000, float(i) * 50));
            lines.push_back(v(1000, float(i) * 50));
        }
        r.drawLines(lines, mat4::identity());
        return;
    }
    r.beginFrame(width, height, city_->clearColor());
    if (ctx.drawDistance != 0) r.setDepthMode(true, farPlane);
    r.setCamera(view, proj, cam_.pos);
    std::vector<CityCar> cars;
    for (const DroppedCar& d : dropped_) {
        CityCar c;
        c.model = d.model.get();
        c.world = d.world;
        c.paint = d.paint;
        cars.push_back(std::move(c));
    }
    city_->render(ctx, view, proj, cam_.pos, farPlane, cars, fullMap);
}

std::string CityView::infoJson() const {
    JsonWriter j;
    j.beginObject();
    j.field("loaded", city_ != nullptr);
    j.field("message", message_);
    if (city_) {
        j.field("name", city_->name());
        j.field("summary", city_->summary());
    }
    j.endObject();
    return j.str();
}

std::string CityView::readout() const {
    float heading = std::fmod(-cam_.yaw * 180.0f / kPi + 360.0f, 360.0f);
    std::string room;
    if (city_) {
        int r = city_->roomAt(cam_.pos);
        if (r) room = strf("  ·  room %d", r);
    }
    return strf("x %.1f  y %.1f  z %.1f  ·  heading %03.0f°  ·  %.0f m/s%s  ·  %.0f fps", double(cam_.pos.x),
                double(cam_.pos.y), double(cam_.pos.z), double(heading), double(cam_.speed), room.c_str(),
                double(fps_));
}

}  // namespace cc
