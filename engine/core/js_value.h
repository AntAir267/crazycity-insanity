// A JSON value with JavaScript's meanings (parse and print) for the network server's control
// messages and the native transport (platform/native_transport.*, platform/host_server.*): a
// missing member is undefined (not null), and String(), Number() and truthiness work as in
// server/signal.js, which the engine's server must answer exactly like. Numbers are doubles
// (player ids stay below 2^31, so they are exact). Members keep their order; printing is compact,
// numbers as JavaScript prints them. (core/json_read.h is the native program's plain reader.)
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cc {

class JsValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object, Undefined };

    JsValue() = default;
    JsValue(std::nullptr_t) : type_(Null) {}
    JsValue(bool b) : type_(Bool), num_(b ? 1 : 0) {}
    JsValue(int v) : type_(Number), num_(double(v)) {}
    JsValue(unsigned v) : type_(Number), num_(double(v)) {}
    JsValue(long long v) : type_(Number), num_(double(v)) {}
    JsValue(double v) : type_(Number), num_(v) {}
    JsValue(const char* s) : type_(String), str_(s) {}
    JsValue(std::string s) : type_(String), str_(std::move(s)) {}
    JsValue(std::string_view s) : type_(String), str_(s) {}
    static JsValue array() {
        JsValue v;
        v.type_ = Array;
        return v;
    }
    static JsValue object() {
        JsValue v;
        v.type_ = Object;
        return v;
    }
    static const JsValue& undefined();

    // Parses one value (whitespace around it allowed). False on bad JSON (`out` is then Undefined).
    static bool parse(std::string_view text, JsValue& out, std::string* error = nullptr);

    Type type() const { return type_; }
    bool isUndefined() const { return type_ == Undefined; }
    bool isNull() const { return type_ == Null; }
    bool isBool() const { return type_ == Bool; }
    bool isNumber() const { return type_ == Number; }
    bool isString() const { return type_ == String; }
    bool isArray() const { return type_ == Array; }
    bool isObject() const { return type_ == Object; }

    // Plain reads (the default when the type differs).
    double num(double def = 0) const { return type_ == Number ? num_ : def; }
    long long integer(long long def = 0) const { return type_ == Number ? (long long)num_ : def; }
    bool boolean(bool def = false) const { return type_ == Bool ? num_ != 0 : def; }
    const std::string& str() const;

    // JavaScript's conversions, for the server's parity with server/signal.js: !!v, String(v),
    // Number(v) (NaN for what JavaScript makes NaN).
    bool truthy() const;
    std::string jsString() const;
    double jsNumber() const;

    // Members and items. A missing member is the shared Undefined value.
    const JsValue& operator[](std::string_view key) const;
    const JsValue& at(size_t i) const;
    bool has(std::string_view key) const;
    size_t size() const { return type_ == Array ? items_.size() : type_ == Object ? members_.size() : 0; }
    const std::vector<JsValue>& items() const { return items_; }
    const std::vector<std::pair<std::string, JsValue>>& members() const { return members_; }

    // Building (an object member is replaced if present).
    JsValue& set(std::string_view key, JsValue v);
    JsValue& push(JsValue v);

    // Compact JSON text (an Undefined member is left out, as JSON.stringify does).
    std::string dump() const;
    void dumpTo(std::string& out) const;

private:
    Type type_ = Undefined;
    double num_ = 0;
    std::string str_;
    std::vector<JsValue> items_;
    std::vector<std::pair<std::string, JsValue>> members_;
    friend class JsParser;
};

// A number as JavaScript prints it (whole numbers without a fraction; NaN and infinities as null
// in JSON, as JSON.stringify does).
std::string jsonNumberText(double v);
// A JSON string literal of UTF-8 text.
void jsonQuote(std::string_view s, std::string& out);
// The first `units` UTF-16 code units of UTF-8 text (String.prototype.slice(0, units)).
std::string utf16Slice(std::string_view s, size_t units);

}  // namespace cc
