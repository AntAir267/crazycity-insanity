#include "formats/texture_lookup.h"

#include <cmath>
#include <cstdlib>

#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {

size_t TextureAsset::frameAt(double t) const {
    if (frames.size() <= 1 || rate <= 0) return 0;
    double f = std::floor(t * double(rate));
    if (f < 0) f = 0;
    return size_t(std::fmod(f, double(frames.size())));
}

namespace {

bool isPathName(const std::string& name) {
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return true;
    return name.size() >= 2 && name[1] == ':' &&
           ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z'));
}

// Step 2: base + .tex / .tga / .bmp; the first that exists and decodes wins.
bool findStill(const Vfs& vfs, const std::string& base, Image& img, std::string& usedPath) {
    static const char* const kExts[] = {".tex", ".tga", ".bmp"};
    for (const char* ext : kExts) {
        std::string path = iendsWith(base, ext) ? base : base + ext;
        if (!vfs.exists(path)) continue;
        BytesPtr bytes = vfs.open(path);
        if (!bytes) continue;  // read failure already logged
        std::string err;
        if (decodeImage(path, bytes->data(), bytes->size(), img, err)) {
            usedPath = vfs.storedName(path);
            return true;
        }
        logFile(LogLevel::Warn, vfs.storedName(path), "texture refused: " + err);
    }
    return false;
}

float readMovieRate(const Vfs& vfs, const std::string& name) {
    BytesPtr b = vfs.open("tune/" + name + ".movie");
    if (!b) return 30.0f;
    std::string text(b->begin(), b->end());
    for (const std::string& line : split(text, '\n')) {
        std::string t = trim(line);
        if (istartsWith(t, "rate")) {
            std::string num = trim(t.substr(4));
            char* end = nullptr;
            float r = std::strtof(num.c_str(), &end);
            if (end != num.c_str() && r > 0) return r;
        }
    }
    return 30.0f;
}

}  // namespace

namespace {
// night.md section 1, rule 4: red, green and blue of every pixel in every level halved
// (integer division by 2); alpha unchanged.
void halveRgb(Image& img) {
    for (ImageLevel& L : img.levels)
        for (size_t i = 0; i + 3 < L.rgba.size(); i += 4) {
            L.rgba[i] = uint8_t(L.rgba[i] >> 1);
            L.rgba[i + 1] = uint8_t(L.rgba[i + 1] >> 1);
            L.rgba[i + 2] = uint8_t(L.rgba[i + 2] >> 1);
        }
}

// One still name through the rules: rain tries <n>_fa first; night tries <n>_ni (used as it is);
// otherwise <n>. At night everything not from a _ni file is halved.
bool findStillWithRules(const Vfs& vfs, const std::string& base, const TextureCondition& cond, Image& img,
                        std::string& path) {
    bool found = false, fromNi = false;
    if (cond.rain) found = findStill(vfs, base + "_fa", img, path);
    if (!found && cond.night) {
        found = findStill(vfs, base + "_ni", img, path);
        fromNi = found;
    }
    if (!found) found = findStill(vfs, base, img, path);
    if (found && cond.night && !fromNi) halveRgb(img);
    return found;
}
}  // namespace

std::shared_ptr<TextureAsset> loadTextureAsset(const Vfs& vfs, const std::string& name, const TextureCondition& cond) {
    if (name.empty()) return nullptr;
    std::string base = isPathName(name) ? name : "texture/" + name;
    auto asset = std::make_shared<TextureAsset>();
    asset->name = name;
    Image img;
    std::string path;
    if (findStillWithRules(vfs, base, cond, img, path)) {
        asset->frames.push_back(std::move(img));
        asset->paths.push_back(path);
        return asset;
    }
    // Animated texture: <name>-0001, -0002, ... up to 128 frames (each frame name through the rules).
    for (int i = 1; i <= 128; ++i) {
        Image frame;
        std::string fpath;
        if (!findStillWithRules(vfs, base + strf("-%04d", i), cond, frame, fpath)) break;
        asset->frames.push_back(std::move(frame));
        asset->paths.push_back(fpath);
    }
    if (asset->frames.empty()) return nullptr;
    asset->animated = true;
    asset->rate = readMovieRate(vfs, baseName(name));
    return asset;
}

std::string undamagedTextureName(const std::string& name,
                                 const std::function<bool(const std::string&)>& exists) {
    if (name.size() > 4 && iendsWith(name, "_dmg")) {
        std::string clean = name.substr(0, name.size() - 4);
        if (exists(clean)) return clean;
    }
    return name;
}

std::shared_ptr<const TextureAsset> TextureLibrary::get(const std::string& name) {
    std::string key = pathKey(name);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    std::shared_ptr<const TextureAsset> a = loadTextureAsset(vfs_, name);
    cache_.emplace(key, a);
    return a;
}

}  // namespace cc
