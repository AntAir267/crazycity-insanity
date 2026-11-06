// GPU textures by material texture name (resolved through formats/texture_lookup). Shared by
// everything drawn; cleared when the archives change.
#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "formats/texture_lookup.h"
#include "render/gl_util.h"

namespace cc {

class Vfs;
struct Image;

struct GpuTexture {
    std::vector<GLuint> frames;  // one for a still texture
    float rate = 30.0f;          // animated: frames per second
    bool hasAlpha = false;
    uint32_t flags = 0;          // .tex flags of the first frame (0x2: ground overlay, tex.md)
    int width = 0, height = 0;
    std::string path;            // first file used

    GLuint frameAt(double t) const;
};

class TextureCache {
public:
    explicit TextureCache(const Vfs& vfs) : vfs_(vfs) {}
    ~TextureCache() { clear(); }

    // nullptr if the name resolves to nothing (logged once per name with `user` for context).
    // Loaded under the current condition (night.md: _ni/_fa variants, night halving).
    const GpuTexture* get(const std::string& name, const std::string& user = "");
    // Loaded as stored, whatever the condition (the ground overlay maps, night.md section 3).
    const GpuTexture* getRaw(const std::string& name);
    bool exists(const std::string& name);
    // The session condition for textures loaded from now on. A change frees the textures of the
    // previous condition (holders re-fetch when generation() changes).
    void setCondition(const TextureCondition& c);
    const TextureCondition& condition() const { return cond_; }
    unsigned generation() const { return generation_; }
    void clear();
    size_t count() const { return cache_.size(); }
    size_t gpuBytes() const { return bytes_; }

    static GLuint uploadImage(const Image& img, size_t* bytes = nullptr);
    // Number of uploads so far (each one changes the texture bound on the active unit).
    static unsigned uploadCount();
    // Someone else bound a texture on the active unit (counts as an upload for the renderer).
    static void noteBindingChanged();

private:
    GpuTexture* load(const std::string& key, const std::string& name, bool* found, const TextureCondition& cond);
    std::string keyOf(const std::string& name) const;

    const Vfs& vfs_;
    std::unordered_set<std::string> reported_;
    std::unordered_map<std::string, GpuTexture*> cache_;  // nullptr = missing
    size_t bytes_ = 0;
    TextureCondition cond_;
    unsigned generation_ = 0;
};

}  // namespace cc
