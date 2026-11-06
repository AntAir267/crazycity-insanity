// The engine's one virtual file system, with MM2's archive rule (spec/formats/ar.md, "Several
// archives"): every archive is mounted, sorted by its upper-cased file name; for any path the
// FIRST archive in that order that has it wins. Inside an archive a path is found by binary
// search over the stored directory, as MM2 does.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/bytes.h"
#include "vfs/archive.h"

namespace cc {

struct VfsEntryRef {
    int archive = -1;
    int entry = -1;
};

class Vfs {
public:
    static constexpr size_t kMaxArchives = 256;

    void clear();

    // Opens and mounts an archive after the ones already mounted (lower priority). Logs and
    // returns false if it cannot be read. Callers mount in mountOrder().
    bool mount(const std::string& name, std::shared_ptr<ByteSource> source);

    // MM2's priority order for a set of archive file names: names upper-cased (ASCII letters
    // only) and sorted by byte value. Returns indices into `names`.
    static std::vector<size_t> mountOrder(const std::vector<std::string>& names);
    static bool isBaseArchive(const std::string& name);
    static bool isAudioArchive(const std::string& name);

    bool exists(const std::string& path) const;
    // Size of the file once unpacked, or -1 if missing.
    long long fileSize(const std::string& path) const;

    // The file's bytes from whichever archive wins, or nullptr if it is missing or cannot be
    // read (a read failure is logged).
    BytesPtr open(const std::string& path) const;

    // Lazily read archives (ByteSource::lazy): whether a file can be read now. Pending: its bytes
    // were asked for and arrive later (ask again).
    enum class FileState { Missing, Ready, Pending };
    FileState request(const std::string& path) const;
    // The bytes of a file read from a lazy archive are no longer needed.
    void forget(const std::string& path) const;
    // Fetches every file of the lazy archives that `want` accepts, in a few merged ranges.
    void prefetch(const std::function<bool(const std::string& storedName)>& want);
    // Every range asked for by prefetch() has arrived.
    bool prefetched() const;

    // Path as stored by the winning archive (keeps its case), "" if missing.
    std::string storedName(const std::string& path) const;
    // Name of the archive that supplies the file, "" if missing.
    std::string archiveOf(const std::string& path) const;
    bool fromBaseArchive(const std::string& path) const;

    // MM2's folder listing: archives in priority order, each archive's entries in stored order,
    // stored names of all usable files whose path starts with `prefix` (compared without case).
    // A path present in two archives is listed twice.
    std::vector<std::string> list(const std::string& prefix) const;
    // The same, each path once (first occurrence kept).
    std::vector<std::string> listUnique(const std::string& prefix) const;

    size_t fileCount() const { return uniqueFiles_; }
    const std::vector<std::unique_ptr<Archive>>& archives() const { return archives_; }

private:
    bool find(const std::string& path, VfsEntryRef& out) const;

    std::vector<std::unique_ptr<Archive>> archives_;
    size_t uniqueFiles_ = 0;
    struct Range {
        int archive = 0;
        uint64_t offset = 0;
        size_t len = 0;
    };
    std::vector<Range> prefetch_;
};

}  // namespace cc
