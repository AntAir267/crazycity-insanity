// How a material's texture name becomes an image (spec/formats/tex.md, "Finding the file").
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/image.h"

namespace cc {

class Vfs;

struct TextureAsset {
    std::string name;                // the name that was asked for
    std::vector<std::string> paths;  // file(s) used, one per frame
    std::vector<Image> frames;       // one for a still texture
    bool animated = false;
    float rate = 30.0f;              // frames per second (animated only)

    const Image& first() const { return frames.front(); }
    bool hasAlpha() const { return !frames.empty() && frames.front().hasAlpha; }
    // Frame shown at time t (seconds): advances every 1/rate seconds and loops.
    size_t frameAt(double t) const;
};

// Session conditions for texture loading (night.md, section 1).
struct TextureCondition {
    bool night = false;  // try <name>_ni; everything else at half brightness
    bool rain = false;   // try <name>_fa first
    bool operator==(const TextureCondition& o) const { return night == o.night && rain == o.rain; }
};

// Resolves and decodes a texture by name. Returns nullptr if nothing usable was found (files
// that exist but are refused are logged as file problems). With a condition, the night/rain rules
// of night.md apply to the still name and to every animated frame name on its own.
std::shared_ptr<TextureAsset> loadTextureAsset(const Vfs& vfs, const std::string& name,
                                               const TextureCondition& cond = TextureCondition());

// The texture a car material slot shows when the car is undamaged: a name ending in "_dmg"
// (any case) shows the name without the suffix if that texture exists, else itself.
std::string undamagedTextureName(const std::string& name,
                                 const std::function<bool(const std::string&)>& exists);

// CPU-side cache of decoded textures, keyed by lower-case name (nullptr = not found).
class TextureLibrary {
public:
    explicit TextureLibrary(const Vfs& vfs) : vfs_(vfs) {}
    std::shared_ptr<const TextureAsset> get(const std::string& name);
    bool exists(const std::string& name) { return get(name) != nullptr; }
    void clear() { cache_.clear(); }
    size_t size() const { return cache_.size(); }

private:
    const Vfs& vfs_;
    std::unordered_map<std::string, std::shared_ptr<const TextureAsset>> cache_;
};

}  // namespace cc
