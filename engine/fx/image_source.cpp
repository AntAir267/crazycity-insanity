#include "fx/image_source.h"

#include "core/strutil.h"

namespace cc {
namespace fx {

std::shared_ptr<const TextureAsset> ImageSource::get(const std::string& name) {
    if (name.empty()) return nullptr;
    const std::string key = pathKey(name);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    std::shared_ptr<const TextureAsset> a = loadTextureAsset(vfs_, name, cond_);
    cache_.emplace(key, a);
    return a;
}

void ImageSource::setCondition(const TextureCondition& c) {
    if (c == cond_) return;
    cond_ = c;
    cache_.clear();
    ++generation_;
}

}  // namespace fx
}  // namespace cc
