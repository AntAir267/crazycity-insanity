#include "core/js_value.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cc {

namespace {
const std::string kEmpty;

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}
}  // namespace

class JsParser {
public:
    explicit JsParser(std::string_view t) : t_(t) {}
    bool run(JsValue& out, std::string* err) {
        ws();
        if (!value(out, 0)) {
            if (err) *err = why_.empty() ? "bad JSON" : why_;
            return false;
        }
        ws();
        if (i_ != t_.size()) {
            if (err) *err = "text after the value";
            return false;
        }
        return true;
    }

private:
    bool fail(const char* w) {
        if (why_.empty()) why_ = w;
        return false;
    }
    void ws() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r')) ++i_;
    }
    bool lit(const char* s) {
        const size_t n = std::strlen(s);
        if (t_.substr(i_, n) != s) return false;
        i_ += n;
        return true;
    }
    bool hex4(uint32_t& v) {
        if (i_ + 4 > t_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = t_[i_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        if (i_ >= t_.size() || t_[i_] != '"') return fail("a string expected");
        ++i_;
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (uint8_t(c) < 0x20) return fail("a control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (i_ >= t_.size()) break;
            const char e = t_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!hex4(cp)) return fail("a bad \\u escape");
                    if (cp >= 0xD800 && cp < 0xDC00 && i_ + 6 <= t_.size() && t_[i_] == '\\' && t_[i_ + 1] == 'u') {
                        const size_t keep = i_;
                        i_ += 2;
                        uint32_t lo;
                        if (hex4(lo) && lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else i_ = keep;
                    }
                    if (cp >= 0xD800 && cp < 0xE000) cp = 0xFFFD;  // a lone surrogate
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("a bad escape");
            }
        }
        return fail("an unterminated string");
    }
    bool value(JsValue& v, int depth) {
        if (depth > 64) return fail("nested too deep");
        if (i_ >= t_.size()) return fail("a value expected");
        const char c = t_[i_];
        if (c == '{') {
            ++i_;
            v.type_ = JsValue::Object;
            ws();
            if (i_ < t_.size() && t_[i_] == '}') {
                ++i_;
                return true;
            }
            for (;;) {
                ws();
                std::string k;
                if (!string(k)) return false;
                ws();
                if (i_ >= t_.size() || t_[i_] != ':') return fail("':' expected");
                ++i_;
                ws();
                JsValue m;
                if (!value(m, depth + 1)) return false;
                v.set(k, std::move(m));
                ws();
                if (i_ < t_.size() && t_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < t_.size() && t_[i_] == '}') {
                    ++i_;
                    return true;
                }
                return fail("',' or '}' expected");
            }
        }
        if (c == '[') {
            ++i_;
            v.type_ = JsValue::Array;
            ws();
            if (i_ < t_.size() && t_[i_] == ']') {
                ++i_;
                return true;
            }
            for (;;) {
                ws();
                JsValue m;
                if (!value(m, depth + 1)) return false;
                v.items_.push_back(std::move(m));
                ws();
                if (i_ < t_.size() && t_[i_] == ',') {
                    ++i_;
                    continue;
                }
                if (i_ < t_.size() && t_[i_] == ']') {
                    ++i_;
                    return true;
                }
                return fail("',' or ']' expected");
            }
        }
        if (c == '"') {
            v.type_ = JsValue::String;
            return string(v.str_);
        }
        if (lit("true")) {
            v = JsValue(true);
            return true;
        }
        if (lit("false")) {
            v = JsValue(false);
            return true;
        }
        if (lit("null")) {
            v = JsValue(nullptr);
            return true;
        }
        // A number: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?
        const size_t start = i_;
        if (i_ < t_.size() && t_[i_] == '-') ++i_;
        if (i_ >= t_.size() || !(t_[i_] >= '0' && t_[i_] <= '9')) return fail("a value expected");
        if (t_[i_] == '0') ++i_;
        else
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
        if (i_ < t_.size() && t_[i_] == '.') {
            ++i_;
            if (i_ >= t_.size() || !(t_[i_] >= '0' && t_[i_] <= '9')) return fail("a digit expected");
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
        }
        if (i_ < t_.size() && (t_[i_] == 'e' || t_[i_] == 'E')) {
            ++i_;
            if (i_ < t_.size() && (t_[i_] == '+' || t_[i_] == '-')) ++i_;
            if (i_ >= t_.size() || !(t_[i_] >= '0' && t_[i_] <= '9')) return fail("a digit expected");
            while (i_ < t_.size() && t_[i_] >= '0' && t_[i_] <= '9') ++i_;
        }
        const std::string num(t_.substr(start, i_ - start));
        v = JsValue(std::strtod(num.c_str(), nullptr));
        return true;
    }

    std::string_view t_;
    size_t i_ = 0;
    std::string why_;
};

const JsValue& JsValue::undefined() {
    static const JsValue u;
    return u;
}

bool JsValue::parse(std::string_view text, JsValue& out, std::string* error) {
    out = JsValue();
    JsParser p(text);
    JsValue v;
    if (!p.run(v, error)) return false;
    out = std::move(v);
    return true;
}

const std::string& JsValue::str() const { return type_ == String ? str_ : kEmpty; }

bool JsValue::truthy() const {
    switch (type_) {
        case Bool: return num_ != 0;
        case Number: return num_ != 0 && !std::isnan(num_);
        case String: return !str_.empty();
        case Array:
        case Object: return true;
        default: return false;
    }
}

std::string JsValue::jsString() const {
    switch (type_) {
        case Undefined: return "undefined";
        case Null: return "null";
        case Bool: return num_ != 0 ? "true" : "false";
        case Number:
            if (std::isnan(num_)) return "NaN";
            if (std::isinf(num_)) return num_ > 0 ? "Infinity" : "-Infinity";
            return jsonNumberText(num_);
        case String: return str_;
        case Array: {
            std::string s;
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i) s += ',';
                if (!items_[i].isNull() && !items_[i].isUndefined()) s += items_[i].jsString();
            }
            return s;
        }
        case Object: return "[object Object]";
    }
    return std::string();
}

double JsValue::jsNumber() const {
    switch (type_) {
        case Null: return 0;
        case Bool: return num_;
        case Number: return num_;
        case String: {
            size_t a = 0, b = str_.size();
            while (a < b && std::strchr(" \t\n\r\f\v", str_[a])) ++a;
            while (b > a && std::strchr(" \t\n\r\f\v", str_[b - 1])) --b;
            if (a == b) return 0;
            const std::string t = str_.substr(a, b - a);
            if (t == "Infinity" || t == "+Infinity") return HUGE_VAL;
            if (t == "-Infinity") return -HUGE_VAL;
            char* end = nullptr;
            double v;
            if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) v = double(std::strtoull(t.c_str() + 2, &end, 16));
            else v = std::strtod(t.c_str(), &end);
            if (!end || *end != 0) return std::nan("");
            // strtod takes "inf", "nan" and hex floats that Number() does not.
            for (char c : t)
                if (std::strchr("iInNpP", c)) return std::nan("");
            return v;
        }
        case Array:
            if (items_.empty()) return 0;
            if (items_.size() == 1) return JsValue(items_[0].jsString()).jsNumber();
            return std::nan("");
        default: return std::nan("");
    }
}

const JsValue& JsValue::operator[](std::string_view key) const {
    if (type_ != Object) return undefined();
    for (const auto& m : members_)
        if (m.first == key) return m.second;
    return undefined();
}

const JsValue& JsValue::at(size_t i) const { return type_ == Array && i < items_.size() ? items_[i] : undefined(); }

bool JsValue::has(std::string_view key) const { return !(*this)[key].isUndefined(); }

JsValue& JsValue::set(std::string_view key, JsValue v) {
    if (type_ != Object) {
        *this = object();
    }
    for (auto& m : members_)
        if (m.first == key) {
            m.second = std::move(v);
            return m.second;
        }
    members_.emplace_back(std::string(key), std::move(v));
    return members_.back().second;
}

JsValue& JsValue::push(JsValue v) {
    if (type_ != Array) *this = array();
    items_.push_back(std::move(v));
    return items_.back();
}

std::string jsonNumberText(double v) {
    if (std::isnan(v) || std::isinf(v)) return "null";
    if (v == std::floor(v) && std::fabs(v) < 1e21) {
        char b[32];
        std::snprintf(b, sizeof b, "%.0f", v);
        if (std::strcmp(b, "-0") == 0) return "0";
        return b;
    }
    // The shortest text that reads back as the same double.
    char b[40];
    for (int prec = 1; prec <= 17; ++prec) {
        std::snprintf(b, sizeof b, "%.*g", prec, v);
        if (std::strtod(b, nullptr) == v) break;
    }
    return b;
}

void jsonQuote(std::string_view s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    out += b;
                } else {
                    out += char(c);
                }
        }
    }
    out += '"';
}

void JsValue::dumpTo(std::string& out) const {
    switch (type_) {
        case Undefined:
        case Null: out += "null"; break;
        case Bool: out += num_ != 0 ? "true" : "false"; break;
        case Number: out += jsonNumberText(num_); break;
        case String: jsonQuote(str_, out); break;
        case Array:
            out += '[';
            for (size_t i = 0; i < items_.size(); ++i) {
                if (i) out += ',';
                items_[i].dumpTo(out);
            }
            out += ']';
            break;
        case Object: {
            out += '{';
            bool first = true;
            for (const auto& m : members_) {
                if (m.second.isUndefined()) continue;
                if (!first) out += ',';
                first = false;
                jsonQuote(m.first, out);
                out += ':';
                m.second.dumpTo(out);
            }
            out += '}';
            break;
        }
    }
}

std::string JsValue::dump() const {
    std::string s;
    dumpTo(s);
    return s;
}

std::string utf16Slice(std::string_view s, size_t units) {
    size_t i = 0, n = 0;
    while (i < s.size()) {
        const uint8_t c = uint8_t(s[i]);
        size_t len = 1, u = 1;
        if (c >= 0xF0) len = 4, u = 2;
        else if (c >= 0xE0) len = 3;
        else if (c >= 0xC0) len = 2;
        if (n + u > units) break;
        n += u;
        i += len;
    }
    return std::string(s.substr(0, i < s.size() ? i : s.size()));
}

}  // namespace cc
