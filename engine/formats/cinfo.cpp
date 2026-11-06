#include "formats/cinfo.h"

#include <cstdlib>

#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {

namespace {
bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
}  // namespace

bool readCinfo(const std::string& b, CityEntry& out, std::string& why) {
    out = CityEntry();
    struct Turn {
        const char* key;
        int kind;  // 0 rest of line up to CR, 1 word, 2 integer
    };
    static const Turn turns[9] = {{"LocalizedName=", 0}, {"MapName=", 1},      {"RaceDir=", 1},
                                  {"BlitzCount=", 2},    {"CircuitCount=", 2}, {"CheckpointCount=", 2},
                                  {"BlitzNames=", 0},    {"CircuitNames=", 0}, {"CheckpointNames=", 0}};
    size_t pos = 0;
    std::string values[9];
    for (int t = 0; t < 9; ++t) {
        while (pos < b.size() && isWs(b[pos])) ++pos;
        if (pos >= b.size()) {
            why = std::string("the file ends before ") + turns[t].key;
            return false;
        }
        std::string line;
        while (pos < b.size() && line.size() < 255) {
            const char c = b[pos++];
            line.push_back(c);
            if (c == '\n') break;
        }
        const std::string key = turns[t].key;
        if (line.compare(0, key.size(), key) != 0) {
            why = "line " + std::to_string(t + 1) + " is not " + key + "…";
            return false;
        }
        size_t p = key.size();
        std::string v;
        if (turns[t].kind == 0) {
            const size_t st = p;
            while (p < line.size() && line[p] != '\r') ++p;
            v = line.substr(st, p - st);
        } else if (turns[t].kind == 1) {
            while (p < line.size() && isWs(line[p])) ++p;
            const size_t st = p;
            while (p < line.size() && !isWs(line[p])) ++p;
            v = line.substr(st, p - st);
        } else {
            while (p < line.size() && isWs(line[p])) ++p;
            const size_t q = p + ((p < line.size() && (line[p] == '+' || line[p] == '-')) ? 1 : 0);
            if (q >= line.size() || line[q] < '0' || line[q] > '9') {
                why = key + " has no number";
                return false;
            }
            v = std::to_string(std::strtol(line.c_str() + p, nullptr, 10));
        }
        if (v.empty()) {
            why = key + " is empty";
            return false;
        }
        if (t < 3 && v.size() > 39) {
            why = key + " is 40 characters or more";
            return false;
        }
        values[t] = v;
    }
    out.localizedName = values[0];
    out.mapName = values[1];
    out.raceDir = values[2];
    auto count = [](const std::string& n, const std::string& names) {
        const int c = std::atoi(n.c_str());
        if (c == 0) return 0;
        int bars = 0;
        for (char ch : names)
            if (ch == '|') ++bars;
        return 1 + bars;
    };
    out.blitz = count(values[3], values[6]);
    out.circuit = count(values[4], values[7]);
    out.checkpoint = count(values[5], values[8]);
    auto names = [](int n, const std::string& line) {
        std::vector<std::string> v;
        if (n == 0) return v;
        std::string cur;
        for (char ch : line) {
            if (ch == '|') {
                v.push_back(cur);
                cur.clear();
            } else if (ch != '\n' && ch != '\r') {
                cur += ch;
            }
        }
        v.push_back(cur);
        return v;
    };
    out.blitzNames = names(out.blitz, values[6]);
    out.circuitNames = names(out.circuit, values[7]);
    out.checkpointNames = names(out.checkpoint, values[8]);
    return true;
}

std::vector<CityEntry> buildCityList(const Vfs& vfs) {
    std::vector<CityEntry> list;
    auto consider = [&](const std::string& path) {
        BytesPtr bytes = vfs.open(path);
        if (!bytes) return;
        CityEntry e;
        std::string why;
        if (!readCinfo(std::string(bytes->begin(), bytes->end()), e, why)) {
            logFile(LogLevel::Warn, vfs.storedName(path), why + "; the city is not listed");
            return;
        }
        for (const CityEntry& o : list)
            if (iequals(o.raceDir, e.raceDir)) return;  // the first city with this RaceDir wins
        e.cinfoPath = vfs.storedName(path);
        list.push_back(e);
    };
    consider("tune/sf.cinfo");
    for (const std::string& p : vfs.list("tune/")) {
        const std::string rest = p.substr(5);
        if (rest.find('/') != std::string::npos) continue;
        const size_t dot = rest.rfind('.');
        if (dot == std::string::npos || dot == 0 || !iequals(rest.substr(dot + 1), "cinfo")) continue;
        consider(p);
    }
    // Ant's choice (viewer.md): every city/*.psdl that no listed city uses, as a test layout.
    for (const std::string& p : vfs.listUnique("city/")) {
        const std::string rest = p.substr(5);
        if (rest.find('/') != std::string::npos || !iendsWith(p, ".psdl")) continue;
        const std::string name = stem(p);
        bool used = false;
        for (const CityEntry& e : list)
            if (iequals(e.mapName, name)) used = true;
        if (used) continue;
        CityEntry e;
        e.localizedName = name;
        e.mapName = name;
        e.raceDir = name;
        e.testLayout = true;
        list.push_back(e);
    }
    return list;
}

}  // namespace cc
