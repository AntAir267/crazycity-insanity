#include "formats/banger.h"

#include <cstdlib>

#include "core/bytes.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {

namespace {
std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        size_t j = i;
        while (j < s.size() && !(s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}
}  // namespace

bool loadBangerData(const Vfs& vfs, const std::string& name, BangerData& b) {
    BytesPtr bytes = vfs.open("tune/banger/" + name + ".dgBangerData");
    if (!bytes) return false;
    const std::vector<std::string> t = tokens(std::string(bytes->begin(), bytes->end()));
    size_t pos = 0;
    if (t.size() >= 2 && t[0] == "type:" && t[1] == "a") pos = 2;
    pos += 2;  // normally "dgBangerData {"
    auto have = [&](size_t n) { return pos + n <= t.size(); };
    auto readF = [&](float& v) {
        if (!have(2)) return false;
        v = std::strtof(t[pos + 1].c_str(), nullptr);
        pos += 2;
        return true;
    };
    auto readI = [&](int& v) {
        if (!have(2)) return false;
        v = std::atoi(t[pos + 1].c_str());
        pos += 2;
        return true;
    };
    auto readV = [&](vec3& v) {
        if (!have(4)) return false;
        v = vec3(std::strtof(t[pos + 1].c_str(), nullptr), std::strtof(t[pos + 2].c_str(), nullptr),
                 std::strtof(t[pos + 3].c_str(), nullptr));
        pos += 4;
        return true;
    };
    int dummy = 0;
    if (!readI(dummy) || !readV(b.size) || !readV(b.cg)) return true;  // AudioId, Size, CG
    int glows = 1;
    bool listed = false;
    if (have(1) && t[pos] == "NumGlows") {
        if (!readI(glows)) return true;
        listed = true;
    }
    for (int g = 0; g < glows; ++g) {
        vec3 off;
        if (!readV(off)) return true;  // GlowOffset
        if (listed) b.glowOffsets.push_back(off);
    }
    if (!readF(b.mass) || !readF(b.elasticity) || !readF(b.friction) || !readF(b.impulseLimit2)) return true;
    if (!readI(dummy) || !readI(dummy) || !readI(b.numParts)) return true;  // SpinAxis, Flash, NumParts
    // BirthRule { ... } (particles, not collision)
    if (!have(2)) return true;
    pos += 1;
    if (t[pos] == "{") {
        int depth = 0;
        for (; pos < t.size(); ++pos) {
            if (t[pos] == "{") ++depth;
            else if (t[pos] == "}" && --depth == 0) {
                ++pos;
                break;
            }
        }
    }
    if (!readI(dummy) || !readI(b.billFlags) || !readF(b.yRadius)) return true;  // TexNumber, BillFlags, YRadius
    if (have(1) && t[pos] == "ColliderId" && !readI(b.colliderId)) return true;
    if (have(1) && t[pos] == "CollisionPrim" && !readI(b.collisionPrim)) return true;
    if (have(1) && t[pos] == "CollisionType") readI(b.collisionType);
    return true;
}

}  // namespace cc
