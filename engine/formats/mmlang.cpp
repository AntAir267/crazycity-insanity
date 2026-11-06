#include "formats/mmlang.h"

#include <cstdlib>
#include <set>
#include <vector>

namespace cc {

namespace {

struct Pe {
    const uint8_t* d = nullptr;
    size_t n = 0;
    bool ok = true;
    uint32_t u32(size_t o) {
        if (o + 4 > n || o + 4 < o) { ok = false; return 0; }
        return readU32LE(d + o);
    }
    uint16_t u16(size_t o) {
        if (o + 2 > n || o + 2 < o) { ok = false; return 0; }
        return uint16_t(d[o] | (d[o + 1] << 8));
    }
    struct Section {
        uint32_t vsize, va, rawSize, rawOff;
    };
    std::vector<Section> sections;
    // Address to file offset (step 4); false when no section holds it.
    bool offsetOf(uint32_t a, size_t& off) const {
        for (const Section& s : sections) {
            const uint32_t span = s.vsize > s.rawSize ? s.vsize : s.rawSize;
            if (a >= s.va && a - s.va < span) {
                if (a - s.va >= s.rawSize) return false;
                off = size_t(a - s.va) + s.rawOff;
                return off < n;
            }
        }
        return false;
    }
};

void appendUtf8(std::string& o, uint32_t c) {
    if (c < 0x80) {
        o += char(c);
    } else if (c < 0x800) {
        o += char(0xC0 | (c >> 6));
        o += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        o += char(0xE0 | (c >> 12));
        o += char(0x80 | ((c >> 6) & 0x3F));
        o += char(0x80 | (c & 0x3F));
    } else {
        o += char(0xF0 | (c >> 18));
        o += char(0x80 | ((c >> 12) & 0x3F));
        o += char(0x80 | ((c >> 6) & 0x3F));
        o += char(0x80 | (c & 0x3F));
    }
}

}  // namespace

bool StringTable::load(const uint8_t* data, size_t size, std::string& why, uint16_t language) {
    clear();
    blocks = 0;
    firstLanguage = 0;
    languages = 0;
    longest = 0;
    ascii = true;
    Pe pe;
    pe.d = data;
    pe.n = size;
    // 1. MZ, then PE\0\0 at the offset stored at 0x3C.
    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z') {
        why = "not a Windows program file (no MZ header)";
        return false;
    }
    const uint32_t p = pe.u32(0x3C);
    if (!pe.ok || size_t(p) + 24 > size || data[p] != 'P' || data[p + 1] != 'E' || data[p + 2] || data[p + 3]) {
        why = "no PE header";
        return false;
    }
    // 2. File header.
    const uint16_t nsec = pe.u16(p + 4 + 2);
    const uint16_t optSize = pe.u16(p + 4 + 16);
    // 3. Optional header and data directories.
    const size_t opt = size_t(p) + 24;
    const uint16_t magic = pe.u16(opt);
    size_t dirs;
    if (magic == 0x10B) dirs = opt + 96;
    else if (magic == 0x20B) dirs = opt + 112;
    else {
        why = "unknown optional header";
        return false;
    }
    const uint32_t resAddr = pe.u32(dirs + 2 * 8);
    if (!pe.ok) {
        why = "the file is cut short";
        return false;
    }
    if (resAddr == 0) {
        why = "the file has no resources";
        return false;
    }
    // 4. Section table.
    const size_t secs = opt + optSize;
    for (uint16_t i = 0; i < nsec; ++i) {
        const size_t s = secs + 40 * size_t(i);
        Pe::Section sec{pe.u32(s + 8), pe.u32(s + 12), pe.u32(s + 16), pe.u32(s + 20)};
        pe.sections.push_back(sec);
    }
    size_t root = 0;
    if (!pe.ok || !pe.offsetOf(resAddr, root)) {
        why = "the resource directory is outside the file";
        return false;
    }
    // 5. Directory walking.
    struct Entry {
        bool named;
        uint32_t id;
        bool sub;
        size_t target;
    };
    auto readDir = [&](size_t at) {
        std::vector<Entry> out;
        const uint16_t named = pe.u16(at + 12), numbered = pe.u16(at + 14);
        for (uint32_t i = 0; i < uint32_t(named) + numbered && pe.ok; ++i) {
            const uint32_t name = pe.u32(at + 16 + 8 * size_t(i));
            const uint32_t tgt = pe.u32(at + 16 + 8 * size_t(i) + 4);
            Entry e;
            e.named = (name & 0x80000000u) != 0;
            e.id = name & 0xFFFFu;
            e.sub = (tgt & 0x80000000u) != 0;
            e.target = root + (tgt & 0x7FFFFFFFu);
            out.push_back(e);
        }
        return out;
    };
    // 6. Type 6 (string table).
    size_t typeDir = 0;
    bool haveType = false;
    for (const Entry& e : readDir(root))
        if (!e.named && e.id == 6 && e.sub) {
            typeDir = e.target;
            haveType = true;
        }
    if (!pe.ok) {
        why = "damaged resource directory";
        return false;
    }
    if (!haveType) {
        why = "the file has no string table";
        return false;
    }
    std::set<uint16_t> langs;
    for (const Entry& blk : readDir(typeDir)) {
        if (blk.named || !blk.sub || blk.id == 0) continue;
        const std::vector<Entry> ls = readDir(blk.target);
        if (ls.empty()) continue;
        const Entry* pick = &ls[0];
        for (const Entry& l : ls) {
            langs.insert(uint16_t(l.id));
            if (!l.named && l.id == language) pick = &l;
        }
        if (blocks == 0) firstLanguage = uint16_t(ls[0].id);
        if (pick->sub) continue;
        // 7. Data entry: address (not relative to root) and size.
        const uint32_t addr = pe.u32(pick->target), len = pe.u32(pick->target + 4);
        size_t off = 0;
        if (!pe.ok || !pe.offsetOf(addr, off) || off + len > size) continue;
        ++blocks;
        // 8. Sixteen counted UTF-16 strings.
        size_t q = off;
        const size_t end = off + len;
        for (int k = 0; k < 16 && q + 2 <= end; ++k) {
            const uint16_t n = uint16_t(data[q] | (data[q + 1] << 8));
            q += 2;
            if (q + 2 * size_t(n) > end) break;
            if (n > 0) {
                std::string s;
                for (uint16_t j = 0; j < n; ++j) {
                    uint32_t c = uint32_t(data[q + 2 * j] | (data[q + 2 * j + 1] << 8));
                    if (c >= 0xD800 && c < 0xDC00 && j + 1 < n) {
                        const uint32_t c2 = uint32_t(data[q + 2 * (j + 1)] | (data[q + 2 * (j + 1) + 1] << 8));
                        if (c2 >= 0xDC00 && c2 < 0xE000) {
                            c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
                            ++j;
                        }
                    }
                    if (c >= 0x80) ascii = false;
                    appendUtf8(s, c);
                }
                if (n > longest) longest = n;
                strings_[int((blk.id - 1) * 16 + uint32_t(k))] = s;
            }
            q += 2 * size_t(n);
        }
    }
    languages = int(langs.size());
    loaded_ = true;
    return true;
}

const std::string& StringTable::get(int id) const {
    static const std::string empty;
    auto it = strings_.find(id);
    return it == strings_.end() ? empty : it->second;
}

int StringTable::number(int id) const { return std::atoi(get(id).c_str()); }

}  // namespace cc
