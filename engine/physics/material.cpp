#include "physics/material.h"

#include <cstdlib>

namespace cc {
namespace phys {

Material defaultMaterial() { return Material(); }

std::vector<std::string> tokenizeWhitespace(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '\f' || s[i] == '\v'))
            ++i;
        size_t j = i;
        while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n' || s[j] == '\f' || s[j] == '\v'))
            ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

namespace {
float tf(const std::string& t) { return float(std::strtod(t.c_str(), nullptr)); }
int ti(const std::string& t) { return int(std::strtol(t.c_str(), nullptr, 10)); }
}  // namespace

bool parseMtlBlock(const std::vector<std::string>& t, size_t& pos, Material& m) {
    if (pos + 2 >= t.size() || t[pos] != "mtl") return false;
    m = Material();
    m.name = t[pos + 1];
    pos += 2;
    if (pos < t.size() && t[pos] == "{") ++pos;
    while (pos < t.size() && t[pos] != "}") {
        const std::string& k = t[pos++];
        auto next = [&]() -> std::string { return pos < t.size() ? t[pos++] : std::string(); };
        if (k == "elasticity:") m.elasticity = tf(next());
        else if (k == "friction:") m.friction = tf(next());
        else if (k == "effect:") m.effect = next();
        else if (k == "sound:") {
            std::string v = next();
            m.sound = v == "none" ? 0 : ti(v);
        } else if (k == "drag:") m.drag = tf(next());
        else if (k == "width:") m.width = tf(next());
        else if (k == "height:") m.height = tf(next());
        else if (k == "depth:") m.depth = tf(next());
        else if (k == "ptxindex:") {
            m.ptxIndex[0] = ti(next());
            m.ptxIndex[1] = ti(next());
        } else if (k == "ptxthreshold:") {
            m.ptxThreshold[0] = tf(next());
            m.ptxThreshold[1] = tf(next());
        }
    }
    if (pos < t.size()) ++pos;  // '}'
    return true;
}

void MaterialTable::reset() {
    list_.clear();
    list_.push_back(defaultMaterial());
}

void MaterialTable::loadMtl(const std::string& text) {
    auto t = tokenizeWhitespace(text);
    size_t pos = 0;
    while (pos < t.size()) {
        if (t[pos] != "mtl") {
            ++pos;
            continue;
        }
        Material m;
        if (!parseMtlBlock(t, pos, m)) break;
        const int existing = find(m.name);
        if (existing < 0) {
            list_.push_back(m);
        } else if (m.name == "_default") {
            // A second `_default` block copies its values into the existing entry and renames it.
            Material c = m;
            c.name = "default";
            list_[size_t(existing)] = c;
        }
    }
}

int MaterialTable::find(const std::string& name) const {
    for (size_t i = 0; i < list_.size(); ++i)
        if (list_[i].name == name) return int(i);
    return -1;
}

int MaterialTable::shareOrAdd(const Material& m) {
    int i = find(m.name);
    if (i >= 0) return i;
    list_.push_back(m);
    return int(list_.size() - 1);
}

}  // namespace phys
}  // namespace cc
