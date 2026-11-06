// The string table of the player's mmlang.dll (spec/formats/mmlang.md): read as data only, never
// loaded or run. Strings are kept as UTF-8 (converted from the file's UTF-16).
#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "core/bytes.h"

namespace cc {

class StringTable {
public:
    // Reads every string of resource type 6. False (with `why`) when the file is not a PE file
    // with a resource directory.
    bool load(const uint8_t* data, size_t size, std::string& why, uint16_t language = 0x0409);
    bool loaded() const { return loaded_; }
    void clear() {
        strings_.clear();
        loaded_ = false;
    }

    // A missing string is empty (MM2 shows it blank).
    const std::string& get(int id) const;
    bool has(int id) const { return strings_.count(id) != 0; }
    // A string holding a number setting (read as an integer, like atoi).
    int number(int id) const;
    const std::map<int, std::string>& all() const { return strings_; }

    // Facts about the file (for the checks).
    int blocks = 0;               // string blocks found
    uint16_t firstLanguage = 0;   // language of the first entry of the first block
    int languages = 0;            // distinct languages seen
    size_t longest = 0;           // longest string in UTF-16 units
    bool ascii = true;            // every character below 128

private:
    std::map<int, std::string> strings_;
    bool loaded_ = false;
};

}  // namespace cc
