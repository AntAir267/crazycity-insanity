#include "model/asset_cache.h"

#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {

bool AssetCache::packageExists(const std::string& name) const {
    return vfs_.exists("geometry/" + name + ".pkg");
}

std::shared_ptr<const Pkg> AssetCache::package(const std::string& name, bool required) {
    const std::string path = "geometry/" + name + ".pkg";
    const std::string key = pathKey(path);
    auto it = pkgs_.find(key);
    if (it != pkgs_.end()) return it->second;
    std::shared_ptr<const Pkg> result;
    BytesPtr bytes = vfs_.open(path);
    if (!bytes) {
        if (required && !vfs_.exists(path)) logFile(LogLevel::Error, path, "package not found");
    } else {
        auto pkg = std::make_shared<Pkg>();
        std::string err;
        const std::string stored = vfs_.storedName(path);
        if (parsePkg(bytes->data(), bytes->size(), *pkg, err)) {
            for (const auto& w : pkg->warnings) logFile(LogLevel::Warn, stored, w);
            result = pkg;
        } else {
            logFile(LogLevel::Error, stored, "package broken: " + err);
        }
    }
    pkgs_.emplace(key, result);
    return result;
}

Pivot AssetCache::pivot(const std::string& package, const std::string& part) {
    const std::string path = pivotPath(package, part);
    const std::string key = pathKey(path);
    auto it = pivots_.find(key);
    if (it != pivots_.end()) return it->second;
    Pivot p;
    BytesPtr bytes = vfs_.open(path);
    if (bytes) {
        std::string err;
        if (!parseMtx(bytes->data(), bytes->size(), p, err))
            logFile(LogLevel::Warn, vfs_.storedName(path), "pivot broken: " + err);
    }
    pivots_.emplace(key, p);
    return p;
}

std::shared_ptr<PedAnimation> AssetCache::pedAnimation(const std::string& name) {
    const std::string path = "anim/" + name + ".anim";
    const std::string key = pathKey(path);
    auto it = pedAnims_.find(key);
    if (it != pedAnims_.end()) return it->second;
    std::shared_ptr<PedAnimation> result;
    BytesPtr bytes = vfs_.open(path);
    if (!bytes) {
        logFile(LogLevel::Error, path, "animation not found");
    } else {
        auto a = std::make_shared<PedAnimation>();
        std::string err;
        if (parsePedAnimation(bytes->data(), bytes->size(), *a, err)) {
            pedAnimForwardFixup(*a);
            result = a;
        } else {
            logFile(LogLevel::Error, vfs_.storedName(path), "animation refused: " + err);
        }
    }
    pedAnims_.emplace(key, result);
    return result;
}

void AssetCache::clear() {
    pkgs_.clear();
    pivots_.clear();
    pedAnims_.clear();
}

}  // namespace cc
