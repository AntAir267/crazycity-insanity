#include "vfs/vfs.h"

#include <algorithm>
#include <unordered_set>

#include "core/log.h"
#include "core/strutil.h"

namespace cc {

namespace {
const char* const kBaseArchives[] = {"mm2core.ar", "mm2tex.ar", "mm2aud.ar", "mm2audex.ar"};

int baseRank(const std::string& name) {
    std::string b = toLower(baseName(name));
    for (int i = 0; i < 4; ++i)
        if (b == kBaseArchives[i]) return i;
    return -1;
}

std::string upperAscii(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        if (c >= 'a' && c <= 'z') c = char(c - 32);
    return r;
}
}  // namespace

void Vfs::clear() {
    archives_.clear();
    uniqueFiles_ = 0;
    prefetch_.clear();
}

bool Vfs::isBaseArchive(const std::string& name) { return baseRank(name) >= 0; }

bool Vfs::isAudioArchive(const std::string& name) {
    int r = baseRank(name);
    return r == 2 || r == 3;
}

std::vector<size_t> Vfs::mountOrder(const std::vector<std::string>& names) {
    std::vector<size_t> idx(names.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::vector<std::string> keys;
    keys.reserve(names.size());
    for (const auto& n : names) keys.push_back(upperAscii(baseName(n)));
    std::stable_sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        // Byte order (unsigned).
        const std::string& x = keys[a];
        const std::string& y = keys[b];
        return std::lexicographical_compare(
            x.begin(), x.end(), y.begin(), y.end(),
            [](char c1, char c2) { return static_cast<unsigned char>(c1) < static_cast<unsigned char>(c2); });
    });
    return idx;
}

bool Vfs::mount(const std::string& name, std::shared_ptr<ByteSource> source) {
    if (archives_.size() >= kMaxArchives) {
        logFile(LogLevel::Warn, name, "more than 256 archives: MM2 ignores the rest");
        return false;
    }
    auto ar = std::make_unique<Archive>();
    std::string err;
    if (!ar->open(std::move(source), name, &err)) {
        logFile(LogLevel::Error, name, "archive not mounted: " + err);
        return false;
    }
    if (ar->droppedEntries() > 0)
        logFile(LogLevel::Warn, name,
                strf("%d directory entries have bad names or data ranges and were skipped",
                     ar->droppedEntries()));
    int unreachable = ar->unreachableEntries();
    if (unreachable > 0)
        logFile(LogLevel::Warn, name,
                strf("directory is not in MM2's order: %d files cannot be found (as in MM2)", unreachable));
    int usable = 0, shadowed = 0;
    for (const auto& e : ar->entries()) {
        if (!e.usable()) continue;
        ++usable;
        VfsEntryRef r;
        if (find(e.name, r)) ++shadowed;
    }
    logInfo("mounted %s: %d files%s", name.c_str(), usable,
            shadowed ? strf(", %d already supplied by earlier archives", shadowed).c_str() : "");
    archives_.push_back(std::move(ar));

    std::unordered_set<std::string> keys;
    for (const auto& a : archives_)
        for (const auto& e : a->entries())
            if (e.usable()) keys.insert(pathKey(e.name));
    uniqueFiles_ = keys.size();
    return true;
}

bool Vfs::find(const std::string& path, VfsEntryRef& out) const {
    for (size_t a = 0; a < archives_.size(); ++a) {
        int e = archives_[a]->find(path);
        if (e >= 0) {
            out.archive = int(a);
            out.entry = e;
            return true;
        }
    }
    return false;
}

bool Vfs::exists(const std::string& path) const {
    VfsEntryRef r;
    return find(path, r);
}

long long Vfs::fileSize(const std::string& path) const {
    VfsEntryRef r;
    if (!find(path, r)) return -1;
    return archives_[size_t(r.archive)]->entries()[size_t(r.entry)].size;
}

BytesPtr Vfs::open(const std::string& path) const {
    VfsEntryRef r;
    if (!find(path, r)) return nullptr;
    const Archive& ar = *archives_[size_t(r.archive)];
    const ArEntry& e = ar.entries()[size_t(r.entry)];
    auto bytes = std::make_shared<Bytes>();
    std::string err;
    if (!ar.read(e, *bytes, &err)) {
        logFile(LogLevel::Error, e.name, "cannot read from " + ar.name() + ": " + err);
        return nullptr;
    }
    return bytes;
}

Vfs::FileState Vfs::request(const std::string& path) const {
    VfsEntryRef r;
    if (!find(path, r)) return FileState::Missing;
    const Archive& ar = *archives_[size_t(r.archive)];
    ByteSource* src = ar.source();
    if (!src || !src->lazy()) return FileState::Ready;
    const ArEntry& e = ar.entries()[size_t(r.entry)];
    if (src->has(e.offset, e.packed)) return FileState::Ready;
    src->fetch(e.offset, e.packed);
    return FileState::Pending;
}

void Vfs::forget(const std::string& path) const {
    VfsEntryRef r;
    if (!find(path, r)) return;
    const Archive& ar = *archives_[size_t(r.archive)];
    ByteSource* src = ar.source();
    if (!src || !src->lazy()) return;
    const ArEntry& e = ar.entries()[size_t(r.entry)];
    src->forget(e.offset, e.packed);
}

void Vfs::prefetch(const std::function<bool(const std::string&)>& want) {
    // Ranges of the wanted files per archive, merged when the gap between them is small (the
    // directory is sorted by name and the data mostly follows it, so a folder is one range).
    constexpr uint64_t kGap = 256 * 1024;
    for (size_t a = 0; a < archives_.size(); ++a) {
        ByteSource* src = archives_[a]->source();
        if (!src || !src->lazy()) continue;
        std::vector<std::pair<uint64_t, uint64_t>> spans;
        for (const auto& e : archives_[a]->entries())
            if (e.usable() && e.packed > 0 && want(e.name)) spans.push_back({e.offset, uint64_t(e.offset) + e.packed});
        std::sort(spans.begin(), spans.end());
        std::vector<std::pair<uint64_t, uint64_t>> merged;
        for (const auto& s : spans) {
            if (!merged.empty() && s.first <= merged.back().second + kGap)
                merged.back().second = std::max(merged.back().second, s.second);
            else
                merged.push_back(s);
        }
        for (const auto& m : merged) {
            Range rg;
            rg.archive = int(a);
            rg.offset = m.first;
            rg.len = size_t(m.second - m.first);
            prefetch_.push_back(rg);
            src->fetch(rg.offset, rg.len);
        }
    }
}

bool Vfs::prefetched() const {
    for (const Range& r : prefetch_) {
        ByteSource* src = archives_[size_t(r.archive)]->source();
        if (src && !src->has(r.offset, r.len)) return false;
    }
    return true;
}

std::string Vfs::storedName(const std::string& path) const {
    VfsEntryRef r;
    if (!find(path, r)) return std::string();
    return archives_[size_t(r.archive)]->entries()[size_t(r.entry)].name;
}

std::string Vfs::archiveOf(const std::string& path) const {
    VfsEntryRef r;
    if (!find(path, r)) return std::string();
    return archives_[size_t(r.archive)]->name();
}

bool Vfs::fromBaseArchive(const std::string& path) const {
    VfsEntryRef r;
    return find(path, r) && isBaseArchive(archives_[size_t(r.archive)]->name());
}

std::vector<std::string> Vfs::list(const std::string& prefix) const {
    const std::string p = pathKey(prefix);
    std::vector<std::string> out;
    for (const auto& a : archives_)
        for (const auto& e : a->entries())
            if (e.usable() && e.name.size() >= p.size() && pathKey(e.name.substr(0, p.size())) == p)
                out.push_back(e.name);
    return out;
}

std::vector<std::string> Vfs::listUnique(const std::string& prefix) const {
    std::unordered_set<std::string> seen;
    std::vector<std::string> out;
    for (auto& n : list(prefix))
        if (seen.insert(pathKey(n)).second) out.push_back(std::move(n));
    return out;
}

}  // namespace cc
