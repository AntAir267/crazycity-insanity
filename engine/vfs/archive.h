// One MM2 ".ar" archive ("DAVE" layout, spec/formats/ar.md). Only the header, directory and
// name table are read when it is opened; file data is read (and inflated) on request.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/bytes.h"
#include "vfs/source.h"

namespace cc {

struct ArEntry {
    std::string name;  // as stored (Latin-1, '/' separated)
    uint32_t offset = 0;
    uint32_t size = 0;    // unpacked size
    uint32_t packed = 0;  // stored size; == size means stored as-is, else raw deflate
    bool folder = false;  // name ends in '/': no data
    bool bad = false;     // name or data range is broken
    bool usable() const { return !folder && !bad; }
};

// MM2's name order inside an archive: bytes compared unsigned, with A-Z compared as a-z and '\'
// as '/'. Negative / 0 / positive like strcmp.
int arNameCompare(std::string_view a, std::string_view b);

class Archive {
public:
    // Reads the archive's directory. On failure returns false and sets *err.
    bool open(std::shared_ptr<ByteSource> source, const std::string& displayName, std::string* err);

    const std::string& name() const { return name_; }
    // Every directory entry in stored order (folders and broken entries included, see usable()).
    const std::vector<ArEntry>& entries() const { return entries_; }
    // Finds a path the way MM2 does: binary search over the entries as stored. -1 if not found
    // (or if the entry found is a folder or broken).
    int find(std::string_view path) const;
    // Usable entries that the binary search cannot reach because the directory is out of order.
    int unreachableEntries() const;
    uint64_t byteSize() const { return source_ ? source_->size() : 0; }
    ByteSource* source() const { return source_.get(); }

    // Reads (and if needed inflates) one entry. False + *err on failure.
    bool read(const ArEntry& e, Bytes& out, std::string* err) const;

    // Number of directory entries that were dropped while opening (bad ranges/names).
    int droppedEntries() const { return dropped_; }

private:
    std::string name_;
    std::shared_ptr<ByteSource> source_;
    std::vector<ArEntry> entries_;
    int dropped_ = 0;
};

// Raw deflate (RFC 1951, no header) into exactly `size` bytes.
bool inflateRaw(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t size, std::string* err);
// Raw deflate compression (used by the packer). Returns false if zlib fails.
bool deflateRaw(const uint8_t* src, size_t srcLen, Bytes& out);

}  // namespace cc
