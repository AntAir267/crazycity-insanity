#include "formats/dat_text.h"

#include <cstdlib>

namespace cc {
namespace dat {

const Field* Schema::find(const std::string& k) const {
    for (const auto& f : fields)
        if (k == f.key) return &f;  // case-sensitive
    return nullptr;
}

namespace {

struct Token {
    std::string text;
    int line = 0;
};

std::vector<Token> tokenize(const std::string& s) {
    std::vector<Token> out;
    int line = 1;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (c == '\n') {
            ++line;
            ++i;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n' || s[j] == '\f' ||
                                 s[j] == '\v'))
            ++j;
        out.push_back({s.substr(i, j - i), line});
        i = j;
    }
    return out;
}

// Integer: optional sign, then digits; stops at the first other character.
int parseIntToken(const std::string& t) {
    size_t i = 0;
    bool neg = false;
    if (i < t.size() && (t[i] == '+' || t[i] == '-')) {
        neg = t[i] == '-';
        ++i;
    }
    long v = 0;
    while (i < t.size() && t[i] >= '0' && t[i] <= '9') {
        v = v * 10 + (t[i] - '0');
        if (v > 0x7fffffff) v = 0x7fffffff;
        ++i;
    }
    return int(neg ? -v : v);
}

// Float: the longest valid prefix (strtod), stored as float32.
float parseFloatToken(const std::string& t) { return float(std::strtod(t.c_str(), nullptr)); }


class Parser {
public:
    explicit Parser(std::vector<Token> t) : t_(std::move(t)) {}
    bool atEnd() const { return pos_ >= t_.size(); }
    const Token* peek() const { return atEnd() ? nullptr : &t_[pos_]; }
    const Token* next() { return atEnd() ? nullptr : &t_[pos_++]; }

    void skipBlock() {
        // The '{' has been consumed; skip to the matching '}'.
        int depth = 1;
        while (!atEnd() && depth > 0) {
            const Token* k = next();
            if (k->text == "{") ++depth;
            else if (k->text == "}") --depth;
        }
    }

    void parseBlock(const Schema& s) {
        while (!atEnd()) {
            const Token* k = next();
            if (k->text == "}") return;
            if (k->text == "{") continue;  // a stray '{' is ignored
            const Field* f = s.find(k->text);
            if (!f) {
                const Token* n = peek();
                if (n && n->text == "{") {
                    next();
                    skipBlock();
                } else {
                    int line = k->line;
                    while (!atEnd() && peek()->line == line) next();
                }
                continue;
            }
            switch (f->type) {
                case FieldType::Int: {
                    const Token* v = next();
                    if (v) *static_cast<int*>(f->ptr) = parseIntToken(v->text);
                    break;
                }
                case FieldType::Float: {
                    const Token* v = next();
                    if (v) *static_cast<float*>(f->ptr) = parseFloatToken(v->text);
                    break;
                }
                case FieldType::Vector: {
                    vec3* p = static_cast<vec3*>(f->ptr);
                    const Token* a = next();
                    const Token* b = next();
                    const Token* c = next();
                    if (a) p->x = parseFloatToken(a->text);
                    if (b) p->y = parseFloatToken(b->text);
                    if (c) p->z = parseFloatToken(c->text);
                    break;
                }
                case FieldType::Block: {
                    const Token* n = peek();
                    if (n && n->text == "{") next();
                    parseBlock(*f->block);
                    break;
                }
            }
        }
    }

private:
    std::vector<Token> t_;
    size_t pos_ = 0;
};

}  // namespace

bool parseText(const std::string& text, const Schema& schema, std::string* err) {
    Parser p(tokenize(text));
    const Token* a = p.next();
    const Token* b = p.next();
    if (!a || !b || a->text != "type:" || b->text != "a") {
        if (err) *err = "not a 'type: a' tune file";
        return false;
    }
    p.next();  // root name, not checked
    const Token* open = p.peek();
    if (open && open->text == "{") p.next();
    p.parseBlock(schema);
    return true;
}


}  // namespace dat
}  // namespace cc
