// A small JSON reader (the engine's own replies, read back by the native program).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cc {

struct JsonValue {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<JsonValue> items;                            // Array
    std::vector<std::pair<std::string, JsonValue>> fields;  // Object, in order

    // A field of an object (a Null value when missing or not an object).
    const JsonValue& operator[](const std::string& key) const;
    // An item of an array (Null when out of range).
    const JsonValue& at(size_t i) const;
    bool has(const std::string& key) const;
    bool isNull() const { return type == Null; }
    double num(double def = 0) const { return type == Number ? n : (type == Bool ? (b ? 1 : 0) : def); }
    int integer(int def = 0) const { return type == Number ? int(n) : (type == Bool ? (b ? 1 : 0) : def); }
    bool boolean(bool def = false) const { return type == Bool ? b : (type == Number ? n != 0 : def); }
    std::string str(const std::string& def = std::string()) const { return type == String ? s : def; }
    size_t size() const { return type == Array ? items.size() : fields.size(); }
};

// Parses `text`; false (and `out` Null) on a syntax error.
bool parseJson(const std::string& text, JsonValue& out);
JsonValue parseJson(const std::string& text);

}  // namespace cc
