// Writes ".ar" archives (spec/formats/ar.md). Used by the mod packer and the tests.
#pragma once

#include <string>
#include <vector>

#include "core/bytes.h"

namespace cc {

struct ArWriteFile {
    std::string name;  // '/'-separated path inside the archive
    Bytes data;
};

struct ArWriteOptions {
    bool deflate = false;        // store files deflated when that makes them smaller
    bool folderEntries = true;   // add "folder/" entries like the shipped archives
    bool sortEntries = true;     // MM2's directory order (needed for MM2 to find the files)
};

// Builds the archive image in memory.
Bytes buildArchive(std::vector<ArWriteFile> files, const ArWriteOptions& opt = {});

}  // namespace cc
