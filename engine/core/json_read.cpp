#include "core/json_read.h"

#include <cstdlib>

namespace cc {

namespace {

const JsonValue kNull;

struct Parser {
    const std::string& t;
    size_t i = 0;
    explicit Parser(const std::string& text) : t(text) {}

    void ws() {
        while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i;
    }
    bool lit(const char* w) {
        size_t k = 0;
        while (w[k]) {
            if (i + k >= t.size() || t[i + k] != w[k]) return false;
            ++k;
        }
        i += k;
        return true;
    }
    static void utf8(std::string& o, unsigned c) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) {
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
    bool hex4(unsigned& v) {
        if (i + 4 > t.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& o) {
        if (i >= t.size() || t[i] != '"') return false;
        ++i;
        while (i < t.size()) {
            const char c = t[i++];
            if (c == '"') return true;
            if (c != '\\') {
                o += c;
                continue;
            }
            if (i >= t.size()) return false;
            const char e = t[i++];
            switch (e) {
                case '"': o += '"'; break;
                case '\\': o += '\\'; break;
                case '/': o += '/'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'n': o += '\n'; break;
                case 'r': o += '\r'; break;
                case 't': o += '\t'; break;
                case 'u': {
                    unsigned v = 0;
                    if (!hex4(v)) return false;
                    if (v >= 0xD800 && v < 0xDC00 && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u') {
                        i += 2;
                        unsigned lo = 0;
                        if (!hex4(lo)) return false;
                        v = 0x10000 + ((v - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    utf8(o, v);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool value(JsonValue& v, int depth) {
        if (depth > 64) return false;
        ws();
        if (i >= t.size()) return false;
        const char c = t[i];
        if (c == '{') {
            ++i;
            v.type = JsonValue::Object;
            ws();
            if (i < t.size() && t[i] == '}') {
                ++i;
                return true;
            }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i >= t.size() || t[i] != ':') return false;
                ++i;
                JsonValue x;
                if (!value(x, depth + 1)) return false;
                v.fields.emplace_back(std::move(k), std::move(x));
                ws();
                if (i < t.size() && t[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == '}') {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++i;
            v.type = JsonValue::Array;
            ws();
            if (i < t.size() && t[i] == ']') {
                ++i;
                return true;
            }
            for (;;) {
                JsonValue x;
                if (!value(x, depth + 1)) return false;
                v.items.push_back(std::move(x));
                ws();
                if (i < t.size() && t[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == ']') {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            v.type = JsonValue::String;
            return string(v.s);
        }
        if (lit("true")) {
            v.type = JsonValue::Bool;
            v.b = true;
            return true;
        }
        if (lit("false")) {
            v.type = JsonValue::Bool;
            return true;
        }
        if (lit("null")) return true;
        char* end = nullptr;
        const double d = std::strtod(t.c_str() + i, &end);
        if (end == t.c_str() + i) return false;
        i = size_t(end - t.c_str());
        v.type = JsonValue::Number;
        v.n = d;
        return true;
    }
};

}  // namespace

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (type != Object) return kNull;
    for (const auto& f : fields)
        if (f.first == key) return f.second;
    return kNull;
}

const JsonValue& JsonValue::at(size_t i) const { return type == Array && i < items.size() ? items[i] : kNull; }

bool JsonValue::has(const std::string& key) const {
    if (type != Object) return false;
    for (const auto& f : fields)
        if (f.first == key) return true;
    return false;
}

bool parseJson(const std::string& text, JsonValue& out) {
    out = JsonValue();
    Parser p(text);
    if (!p.value(out, 0)) {
        out = JsonValue();
        return false;
    }
    return true;
}

JsonValue parseJson(const std::string& text) {
    JsonValue v;
    parseJson(text, v);
    return v;
}

}  // namespace cc
