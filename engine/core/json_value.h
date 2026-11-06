// A small JSON reader: the engine's JSON replies (the front end's state, the HUD) read back into
// a tree. Missing members read as null; numbers are doubles.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace cc {

class JValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    static JValue parse(const std::string& text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Null; }
    bool isArray() const { return type_ == Array; }
    bool isObject() const { return type_ == Object; }
    bool isNumber() const { return type_ == Number; }
    bool isString() const { return type_ == String; }
    bool isBool() const { return type_ == Bool; }

    double num(double def = 0) const { return type_ == Number ? n_ : type_ == Bool ? (b_ ? 1 : 0) : def; }
    int integer(int def = 0) const { return type_ == Number ? int(n_) : type_ == Bool ? (b_ ? 1 : 0) : def; }
    float f(float def = 0) const { return float(num(def)); }
    bool boolean(bool def = false) const { return type_ == Bool ? b_ : type_ == Number ? n_ != 0 : def; }
    const std::string& str() const;
    std::string str(const std::string& def) const { return type_ == String ? s_ : def; }
    // JavaScript truthiness (a non-empty string, a non-zero number, true, any array or object).
    bool truthy() const;

    size_t size() const { return type_ == Array ? a_.size() : type_ == Object ? o_.size() : 0; }
    const JValue& operator[](size_t i) const;
    const JValue& operator[](int i) const { return i < 0 ? (*this)[size_t(-1)] : (*this)[size_t(i)]; }
    const JValue& operator[](const char* key) const;
    const JValue& operator[](const std::string& key) const { return (*this)[key.c_str()]; }
    bool has(const char* key) const;
    const std::vector<JValue>& items() const { return a_; }
    const std::vector<std::pair<std::string, JValue>>& members() const { return o_; }

    // Building (tests).
    JValue() = default;
    explicit JValue(double v) : type_(Number), n_(v) {}
    explicit JValue(bool v) : type_(Bool), b_(v) {}
    explicit JValue(std::string v) : type_(String), s_(std::move(v)) {}

private:
    struct Parser;
    Type type_ = Null;
    double n_ = 0;
    bool b_ = false;
    std::string s_;
    std::vector<JValue> a_;
    std::vector<std::pair<std::string, JValue>> o_;
};

}  // namespace cc
