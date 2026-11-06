#include "vfs/ar_writer.h"

#include <algorithm>
#include <set>

#include "vfs/archive.h"

namespace cc {

namespace {
constexpr uint32_t kBlock = 2048;
uint32_t roundUp(uint32_t v) { return (v + kBlock - 1) / kBlock * kBlock; }
}  // namespace

Bytes buildArchive(std::vector<ArWriteFile> files, const ArWriteOptions& opt) {
    for (auto& f : files)
        for (char& c : f.name)
            if (c == '\\') c = '/';

    struct Item {
        std::string name;
        const Bytes* raw = nullptr;  // null for folders
        Bytes packed;
        bool isPacked = false;
    };
    std::vector<Item> items;
    std::set<std::string> folders;
    for (auto& f : files) {
        Item it;
        it.name = f.name;
        it.raw = &f.data;
        if (opt.deflate && !f.data.empty()) {
            Bytes z;
            if (deflateRaw(f.data.data(), f.data.size(), z) && z.size() < f.data.size()) {
                it.packed = std::move(z);
                it.isPacked = true;
            }
        }
        items.push_back(std::move(it));
        if (opt.folderEntries) {
            for (size_t p = f.name.find('/'); p != std::string::npos; p = f.name.find('/', p + 1))
                folders.insert(f.name.substr(0, p + 1));
        }
    }
    for (const auto& d : folders) {
        Item it;
        it.name = d;
        items.push_back(std::move(it));
    }
    // MM2 binary-searches the directory with A-Z compared as a-z (ar.md).
    if (opt.sortEntries)
        std::stable_sort(items.begin(), items.end(),
                         [](const Item& a, const Item& b) { return arNameCompare(a.name, b.name) < 0; });

    const uint32_t count = uint32_t(items.size());
    const uint32_t dirSize = roundUp(count * 16);
    uint32_t namesLen = 0;
    for (const auto& it : items) namesLen += uint32_t(it.name.size()) + 1;
    const uint32_t namesSize = roundUp(namesLen);

    uint32_t dataStart = 0x800 + dirSize + namesSize;
    uint32_t total = dataStart;
    std::vector<uint32_t> offsets(items.size(), 0);
    for (size_t i = 0; i < items.size(); ++i) {
        if (!items[i].raw) continue;
        offsets[i] = total;
        uint32_t stored = uint32_t(items[i].isPacked ? items[i].packed.size() : items[i].raw->size());
        total = roundUp(total + stored);
    }

    Bytes out(total, 0);
    std::copy_n("DAVE", 4, out.begin());
    writeU32LE(&out[4], count);
    writeU32LE(&out[8], dirSize);
    writeU32LE(&out[12], namesSize);
    uint32_t nameOff = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        uint8_t* d = &out[0x800 + i * 16];
        uint32_t size = it.raw ? uint32_t(it.raw->size()) : 0;
        uint32_t stored = it.raw ? uint32_t(it.isPacked ? it.packed.size() : it.raw->size()) : 0;
        writeU32LE(d, nameOff);
        writeU32LE(d + 4, offsets[i]);
        writeU32LE(d + 8, size);
        writeU32LE(d + 12, stored);
        std::copy(it.name.begin(), it.name.end(), out.begin() + 0x800 + dirSize + nameOff);
        nameOff += uint32_t(it.name.size()) + 1;
        if (it.raw && stored) {
            const Bytes& src = it.isPacked ? it.packed : *it.raw;
            std::copy(src.begin(), src.end(), out.begin() + offsets[i]);
        }
    }
    return out;
}

}  // namespace cc
