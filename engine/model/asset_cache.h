// Shared, parsed game assets (packages and pivots), keyed by path. One instance per mounted
// file system; cleared when the archives change.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "formats/mtx.h"
#include "formats/ped_anim.h"
#include "formats/pkg.h"

namespace cc {

class Vfs;

class AssetCache {
public:
    explicit AssetCache(const Vfs& vfs) : vfs_(vfs) {}

    // "geometry/<name>.pkg". nullptr if missing or broken (broken files are logged; missing
    // ones are logged only when `required`).
    std::shared_ptr<const Pkg> package(const std::string& name, bool required = true);
    bool packageExists(const std::string& name) const;

    // Pivot of a part ("geometry/<package>_<PART>.mtx"); found == false if missing/broken.
    Pivot pivot(const std::string& package, const std::string& part);

    // "anim/<name>.anim" with MM2's forward fix-up applied once (ped-anim.md, "Animation files"):
    // cached by file name for the whole game and shared by every model naming it (the sequence
    // tables' drift removal changes the shared data). nullptr if missing or refused.
    std::shared_ptr<PedAnimation> pedAnimation(const std::string& name);

    const Vfs& vfs() const { return vfs_; }
    void clear();
    size_t packageCount() const { return pkgs_.size(); }

private:
    const Vfs& vfs_;
    std::unordered_map<std::string, std::shared_ptr<const Pkg>> pkgs_;
    std::unordered_map<std::string, Pivot> pivots_;
    std::unordered_map<std::string, std::shared_ptr<PedAnimation>> pedAnims_;
};

}  // namespace cc
