#include "core/json_value.h"

#include <cstdlib>
#include <cstring>

namespace cc {

namespace {
const JValue& nullValue() {
    static const JValue v;
    return v;
}
const std::string& emptyString() {
    static const std::string s;
    return s;
}

void appendUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) {
        s += char(cp);
    } else if (cp < 0x800) {
        s += char(0xC0 | (cp >> 6));
        s += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += char(0xE0 | (cp >> 12));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    } else {
        s += char(0xF0 | (cp >> 18));
        s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F));
        s += char(0x80 | (cp & 0x3F));
    }
}
}  // namespace

struct JValue::Parser {
    const char* p;
    const char* end;
    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    unsigned hex4() {
        unsigned v = 0;
        for (int i = 0; i < 4 && p < end; ++i, ++p) {
            const char c = *p;
            v <<= 4;
            if (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
        }
        return v;
    }
    std::string string() {
        std::string s;
        ++p;  // "
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                const char c = *p++;
                switch (c) {
                    case 'n': s += '\n'; break;
                    case 't': s += '\t'; break;
                    case 'r': s += '\r'; break;
                    case 'b': s += '\b'; break;
                    case 'f': s += '\f'; break;
                    case 'u': {
                        unsigned cp = hex4();
                        if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < end && p[0] == '\\' && p[1] == 'u') {
                            p += 2;
                            const unsigned lo = hex4();
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        }
                        appendUtf8(s, cp);
                        break;
                    }
                    default: s += c; break;
                }
            } else {
                s += *p++;
            }
        }
        if (p < end) ++p;
        return s;
    }
    void value(JValue& v) {
        ws();
        if (p >= end) return;
        const char c = *p;
        if (c == '{') {
            v.type_ = Object;
            ++p;
            ws();
            if (p < end && *p == '}') {
                ++p;
                return;
            }
            while (p < end) {
                ws();
                if (p >= end || *p != '"') break;
                std::string k = string();
                ws();
                if (p < end && *p == ':') ++p;
                v.o_.emplace_back(std::move(k), JValue());
                value(v.o_.back().second);
                ws();
                if (p < end && *p == ',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == '}') ++p;
                break;
            }
        } else if (c == '[') {
            v.type_ = Array;
            ++p;
            ws();
            if (p < end && *p == ']') {
                ++p;
                return;
            }
            while (p < end) {
                v.a_.emplace_back();
                value(v.a_.back());
                ws();
                if (p < end && *p == ',') {
                    ++p;
                    continue;
                }
                if (p < end && *p == ']') ++p;
                break;
            }
        } else if (c == '"') {
            v.type_ = String;
            v.s_ = string();
        } else if (c == 't' && end - p >= 4 && !std::strncmp(p, "true", 4)) {
            v.type_ = Bool;
            v.b_ = true;
            p += 4;
        } else if (c == 'f' && end - p >= 5 && !std::strncmp(p, "false", 5)) {
            v.type_ = Bool;
            v.b_ = false;
            p += 5;
        } else if (c == 'n' && end - p >= 4 && !std::strncmp(p, "null", 4)) {
            p += 4;
        } else {
            char* e = nullptr;
            const double d = std::strtod(p, &e);
            if (e && e > p) {
                v.type_ = Number;
                v.n_ = d;
                p = e;
            } else {
                ++p;
            }
        }
    }
};

JValue JValue::parse(const std::string& text) {
    JValue v;
    Parser ps{text.data(), text.data() + text.size()};
    ps.value(v);
    return v;
}

const std::string& JValue::str() const { return type_ == String ? s_ : emptyString(); }

bool JValue::truthy() const {
    switch (type_) {
        case Null: return false;
        case Bool: return b_;
        case Number: return n_ != 0;
        case String: return !s_.empty();
        default: return true;
    }
}

const JValue& JValue::operator[](size_t i) const { return type_ == Array && i < a_.size() ? a_[i] : nullValue(); }

const JValue& JValue::operator[](const char* key) const {
    if (type_ != Object) return nullValue();
    for (const auto& kv : o_)
        if (kv.first == key) return kv.second;
    return nullValue();
}

bool JValue::has(const char* key) const {
    if (type_ != Object) return false;
    for (const auto& kv : o_)
        if (kv.first == key) return true;
    return false;
}

}  // namespace cc
