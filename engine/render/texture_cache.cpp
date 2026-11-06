#include "render/texture_cache.h"

#include <cmath>

#include "core/log.h"
#include "core/strutil.h"
#include "formats/texture_lookup.h"

namespace cc {

namespace {
unsigned g_uploads = 0;
float g_maxAniso = -1.0f;
constexpr GLenum kTexMaxAniso = 0x84FE;      // GL_TEXTURE_MAX_ANISOTROPY_EXT
constexpr GLenum kMaxTexMaxAniso = 0x84FF;   // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
}  // namespace

GLuint GpuTexture::frameAt(double t) const {
    if (frames.empty()) return 0;
    if (frames.size() == 1 || rate <= 0) return frames[0];
    double f = std::floor(t * double(rate));
    if (f < 0) f = 0;
    return frames[size_t(std::fmod(f, double(frames.size())))];
}

unsigned TextureCache::uploadCount() { return g_uploads; }

void TextureCache::noteBindingChanged() { ++g_uploads; }

GLuint TextureCache::uploadImage(const Image& img, size_t* bytes) {
    if (img.levels.empty()) return 0;
    ++g_uploads;
    if (g_maxAniso < 0) {
        g_maxAniso = 0;
        if (hasGlExtension("GL_EXT_texture_filter_anisotropic") ||
            hasGlExtension("EXT_texture_filter_anisotropic")) {
            glGetFloatv(kMaxTexMaxAniso, &g_maxAniso);
        }
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    size_t total = 0;
    // Rows go up in stored order: row 0 is v = 0.
    for (size_t l = 0; l < img.levels.size(); ++l) {
        const ImageLevel& L = img.levels[l];
        glTexImage2D(GL_TEXTURE_2D, GLint(l), GL_RGBA8, L.width, L.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     L.rgba.data());
        total += L.rgba.size();
    }
    bool mips = img.levels.size() > 1;
    if (img.generateMips && img.levels.size() == 1) {
        glGenerateMipmap(GL_TEXTURE_2D);
        mips = true;
        total = total * 4 / 3;
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, GLint(img.levels.size() - 1));
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, img.clampU ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, img.clampV ? GL_CLAMP_TO_EDGE : GL_REPEAT);
    if (mips && g_maxAniso > 1.0f) glTexParameterf(GL_TEXTURE_2D, kTexMaxAniso, g_maxAniso < 8.0f ? g_maxAniso : 8.0f);
    if (bytes) *bytes = total;
    return id;
}

std::string TextureCache::keyOf(const std::string& name) const {
    return pathKey(name) + (cond_.night ? (cond_.rain ? "|nr" : "|n") : (cond_.rain ? "|r" : ""));
}

void TextureCache::setCondition(const TextureCondition& c) {
    if (c == cond_) return;
    cond_ = c;
    ++generation_;
    // Free the textures of other conditions (raw ones stay).
    const std::string mine = keyOf("");
    for (auto it = cache_.begin(); it != cache_.end();) {
        const std::string& k = it->first;
        const size_t bar = k.rfind('|');
        const std::string tag = bar == std::string::npos ? std::string() : k.substr(bar);
        if (tag == "|raw" || tag == mine) {
            ++it;
            continue;
        }
        if (it->second) {
            if (!it->second->frames.empty()) glDeleteTextures(GLsizei(it->second->frames.size()), it->second->frames.data());
            delete it->second;
        }
        it = cache_.erase(it);
    }
    bytes_ = 0;
    for (auto& kv : cache_)
        if (kv.second) bytes_ += size_t(kv.second->width) * size_t(kv.second->height) * 4u * kv.second->frames.size();
}

const GpuTexture* TextureCache::getRaw(const std::string& name) {
    if (name.empty()) return nullptr;
    bool cached = false;
    return load(pathKey(name) + "|raw", name, &cached, TextureCondition());
}

GpuTexture* TextureCache::load(const std::string& key, const std::string& name, bool* found, const TextureCondition& cond) {
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        *found = true;
        return it->second;
    }
    *found = false;
    GpuTexture* tex = nullptr;
    std::shared_ptr<TextureAsset> asset = loadTextureAsset(vfs_, name, cond);
    if (asset) {
        tex = new GpuTexture();
        tex->rate = asset->rate;
        tex->hasAlpha = asset->hasAlpha();
        tex->flags = asset->first().texFlags;
        tex->width = asset->first().width();
        tex->height = asset->first().height();
        tex->path = asset->paths.empty() ? std::string() : asset->paths[0];
        for (const Image& img : asset->frames) {
            size_t b = 0;
            tex->frames.push_back(uploadImage(img, &b));
            bytes_ += b;
        }
    }
    cache_.emplace(key, tex);
    return tex;
}

const GpuTexture* TextureCache::get(const std::string& name, const std::string& user) {
    if (name.empty()) return nullptr;
    bool cached = false;
    GpuTexture* tex = load(keyOf(name), name, &cached, cond_);
    if (!tex && !reported_.count(pathKey(name))) {
        reported_.insert(pathKey(name));
        logFile(LogLevel::Warn, "texture/" + name,
                user.empty() ? std::string("texture not found; drawn untextured")
                             : "texture not found (used by " + user + "); drawn untextured");
    }
    return tex;
}

bool TextureCache::exists(const std::string& name) {
    if (name.empty()) return false;
    bool cached = false;
    return load(keyOf(name), name, &cached, cond_) != nullptr;
}

void TextureCache::clear() {
    for (auto& kv : cache_) {
        if (!kv.second) continue;
        if (!kv.second->frames.empty())
            glDeleteTextures(GLsizei(kv.second->frames.size()), kv.second->frames.data());
        delete kv.second;
    }
    cache_.clear();
    reported_.clear();
    bytes_ = 0;
}

}  // namespace cc
