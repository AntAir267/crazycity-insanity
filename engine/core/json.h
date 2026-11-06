// Minimal JSON writer for passing small results to the page.
#pragma once

#include <cstdio>
#include <string>
#include <string_view>

namespace cc {

class JsonWriter {
public:
    JsonWriter& beginObject() { sep(); out_ += '{'; first_ = true; return *this; }
    JsonWriter& endObject() { out_ += '}'; first_ = false; return *this; }
    JsonWriter& beginArray() { sep(); out_ += '['; first_ = true; return *this; }
    JsonWriter& endArray() { out_ += ']'; first_ = false; return *this; }

    JsonWriter& key(std::string_view k) {
        sep();
        str(k);
        out_ += ':';
        first_ = true;  // the value follows without a comma
        return *this;
    }
    JsonWriter& value(std::string_view s) { sep(); str(s); return *this; }
    JsonWriter& value(const char* s) { return value(std::string_view(s)); }
    JsonWriter& value(const std::string& s) { return value(std::string_view(s)); }
    JsonWriter& value(bool b) { sep(); out_ += b ? "true" : "false"; return *this; }
    JsonWriter& value(int v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(long long v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(unsigned v) { sep(); out_ += std::to_string(v); return *this; }
    JsonWriter& value(double v) {
        sep();
        char b[64];
        std::snprintf(b, sizeof b, "%.6g", v);
        out_ += b;
        return *this;
    }
    JsonWriter& null() { sep(); out_ += "null"; return *this; }
    // An already formatted JSON value.
    JsonWriter& raw(std::string_view json) { sep(); out_ += json; return *this; }

    template <typename T>
    JsonWriter& field(std::string_view k, const T& v) { key(k); return value(v); }

    const std::string& str() const { return out_; }

private:
    void sep() {
        if (!first_) out_ += ',';
        first_ = false;
    }
    // Strings are expected to be UTF-8 already.
    void str(std::string_view s) {
        out_ += '"';
        for (unsigned char c : s) {
            switch (c) {
                case '"': out_ += "\\\""; break;
                case '\\': out_ += "\\\\"; break;
                case '\n': out_ += "\\n"; break;
                case '\r': out_ += "\\r"; break;
                case '\t': out_ += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char b[8];
                        std::snprintf(b, sizeof b, "\\u%04x", c);
                        out_ += b;
                    } else {
                        out_ += char(c);
                    }
            }
        }
        out_ += '"';
    }

    std::string out_;
    bool first_ = true;
};

}  // namespace cc
