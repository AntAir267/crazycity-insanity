// Decoded texture images for the visual effects (CPU side): texture damage working copies, body
// chips. Looked up and decoded exactly like the textures the renderer draws (tex.md, "Finding the
// file"), under the session's texture condition (night.md, section 1), and shared by every car.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "formats/texture_lookup.h"

namespace cc {

class Vfs;

namespace fx {

class ImageSource {
public:
    explicit ImageSource(const Vfs& vfs) : vfs_(vfs) {}
    // nullptr if the name resolves to nothing.
    std::shared_ptr<const TextureAsset> get(const std::string& name);
    bool exists(const std::string& name) { return get(name) != nullptr; }
    // A new condition drops the decoded images; holders compare generation() and reload.
    void setCondition(const TextureCondition& c);
    const TextureCondition& condition() const { return cond_; }
    unsigned generation() const { return generation_; }
    const Vfs& vfs() const { return vfs_; }

private:
    const Vfs& vfs_;
    TextureCondition cond_;
    unsigned generation_ = 0;
    std::unordered_map<std::string, std::shared_ptr<const TextureAsset>> cache_;
};

}  // namespace fx
}  // namespace cc
