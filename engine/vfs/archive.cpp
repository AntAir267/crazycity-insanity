#include "vfs/archive.h"

#include <zlib.h>

#include <cstring>

#include "core/strutil.h"

namespace cc {

namespace {
constexpr uint32_t kHeaderArea = 0x800;

inline unsigned char foldName(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<unsigned char>(c + 32);
    if (c == '\\') return '/';
    return c;
}
}  // namespace

int arNameCompare(std::string_view a, std::string_view b) {
    const size_t n = a.size() < b.size() ? a.size() : b.size();
    for (size_t i = 0; i < n; ++i) {
        unsigned char x = foldName(static_cast<unsigned char>(a[i]));
        unsigned char y = foldName(static_cast<unsigned char>(b[i]));
        if (x != y) return x < y ? -1 : 1;
    }
    if (a.size() == b.size()) return 0;
    return a.size() < b.size() ? -1 : 1;
}

int Archive::find(std::string_view path) const {
    int lo = 0, hi = int(entries_.size()) - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        int c = arNameCompare(entries_[size_t(mid)].name, path);
        if (c == 0) return entries_[size_t(mid)].usable() ? mid : -1;
        if (c < 0) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int Archive::unreachableEntries() const {
    int n = 0;
    for (size_t i = 0; i < entries_.size(); ++i)
        if (entries_[i].usable() && find(entries_[i].name) < 0) ++n;
    return n;
}

bool Archive::open(std::shared_ptr<ByteSource> source, const std::string& displayName,
                   std::string* err) {
    name_ = displayName;
    source_ = std::move(source);
    entries_.clear();
    dropped_ = 0;
    if (!source_) {
        if (err) *err = "no data";
        return false;
    }
    const uint64_t fileSize = source_->size();
    uint8_t hdr[16];
    if (fileSize < kHeaderArea || !source_->read(0, hdr, sizeof hdr)) {
        if (err) *err = "too small to be an archive";
        return false;
    }
    if (std::memcmp(hdr, "Dave", 4) == 0) {
        if (err) *err = "lower-case 'Dave' archives (other Angel Studios games) are not supported";
        return false;
    }
    if (std::memcmp(hdr, "DAVE", 4) != 0) {
        if (err) *err = "not a DAVE archive";
        return false;
    }
    const uint32_t count = readU32LE(hdr + 4);
    const uint32_t dirSize = readU32LE(hdr + 8);
    const uint32_t namesSize = readU32LE(hdr + 12);
    if (uint64_t(count) * 16 > dirSize ||
        uint64_t(kHeaderArea) + dirSize + namesSize > fileSize) {
        if (err) *err = strf("bad header (count %u, directory %u, names %u)", count, dirSize, namesSize);
        return false;
    }

    Bytes dir(size_t(count) * 16);
    Bytes names(namesSize);
    if (!source_->read(kHeaderArea, dir.data(), dir.size()) ||
        !source_->read(uint64_t(kHeaderArea) + dirSize, names.data(), names.size())) {
        if (err) *err = "cannot read directory";
        return false;
    }

    entries_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* d = dir.data() + size_t(i) * 16;
        ArEntry e;
        uint32_t nameOff = readU32LE(d);
        e.offset = readU32LE(d + 4);
        e.size = readU32LE(d + 8);
        e.packed = readU32LE(d + 12);
        if (nameOff >= namesSize) {
            e.bad = true;
        } else {
            const char* np = reinterpret_cast<const char*>(names.data() + nameOff);
            size_t maxLen = namesSize - nameOff;
            size_t len = strnlen(np, maxLen);
            if (len == maxLen || len == 0) e.bad = true;
            else e.name.assign(np, len);
        }
        if (!e.bad && e.name.back() == '/') e.folder = true;
        if (!e.bad && !e.folder && uint64_t(e.offset) + e.packed > fileSize) e.bad = true;
        if (e.bad) ++dropped_;
        entries_.push_back(std::move(e));
    }
    return true;
}

bool Archive::read(const ArEntry& e, Bytes& out, std::string* err) const {
    out.clear();
    if (e.packed == e.size) {
        out.resize(e.size);
        if (!source_->read(e.offset, out.data(), e.size)) {
            if (err) *err = "read failed";
            out.clear();
            return false;
        }
        return true;
    }
    Bytes packed(e.packed);
    if (!source_->read(e.offset, packed.data(), packed.size())) {
        if (err) *err = "read failed";
        return false;
    }
    out.resize(e.size);
    if (!inflateRaw(packed.data(), packed.size(), out.data(), out.size(), err)) {
        out.clear();
        return false;
    }
    return true;
}

bool inflateRaw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t size, std::string* err) {
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, -15) != Z_OK) {
        if (err) *err = "inflateInit failed";
        return false;
    }
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = uInt(srcLen);
    zs.next_out = dst;
    zs.avail_out = uInt(size);
    int rc = inflate(&zs, Z_FINISH);
    size_t produced = size - zs.avail_out;
    inflateEnd(&zs);
    if (rc != Z_STREAM_END && !(rc == Z_BUF_ERROR && produced == size)) {
        if (err) *err = strf("deflate data is broken (zlib %d after %zu of %zu bytes)", rc, produced, size);
        return false;
    }
    if (produced != size) {
        if (err) *err = strf("unpacked to %zu bytes, expected %zu", produced, size);
        return false;
    }
    return true;
}

bool deflateRaw(const uint8_t* src, size_t srcLen, Bytes& out) {
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, Z_BEST_COMPRESSION, Z_DEFLATED, -15, 9, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    out.resize(deflateBound(&zs, uLong(srcLen)));
    zs.next_in = const_cast<Bytef*>(src);
    zs.avail_in = uInt(srcLen);
    zs.next_out = out.data();
    zs.avail_out = uInt(out.size());
    int rc = deflate(&zs, Z_FINISH);
    size_t produced = out.size() - zs.avail_out;
    deflateEnd(&zs);
    if (rc != Z_STREAM_END) return false;
    out.resize(produced);
    return true;
}

}  // namespace cc
