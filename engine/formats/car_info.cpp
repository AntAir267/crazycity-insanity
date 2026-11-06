#include "formats/car_info.h"

#include <cstdlib>
#include <set>

#include "core/log.h"
#include "core/strutil.h"
#include "vfs/vfs.h"

namespace cc {

std::string InfoFile::get(const std::string& key, const std::string& fallback) const {
    for (const auto& kv : values)
        if (iequals(kv.first, key)) return kv.second;
    return fallback;
}

InfoFile parseInfoFile(const std::string& text) {
    InfoFile f;
    for (const std::string& raw : split(text, '\n')) {
        std::string line = trim(raw);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        f.values.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
    }
    return f;
}

const std::vector<std::string>& builtInCarNames() {
    static const std::vector<std::string> names = {
        "vpCOOP", "vpbug",    "vpcab",     "vpcaddie", "vpford",  "vpmustang99", "vpcop",
        "vpbullet", "vppanoz", "vpbus",    "vpddbus",  "vpcentury", "vpcoop2k",  "vpdune",
        "vpvwcup", "vp4x4",   "vpauditt",  "vpdb7",    "vppanozgt", "vpsemi"};
    return names;
}

namespace {

bool isWs(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Matches a key written in a scanf format: a space in the key matches any run of whitespace or none.
bool matchKey(const std::string& line, size_t& pos, const char* key) {
    for (const char* k = key; *k; ++k) {
        if (*k == ' ') {
            while (pos < line.size() && isWs(line[pos])) ++pos;
            continue;
        }
        if (pos >= line.size() || line[pos] != *k) return false;
        ++pos;
    }
    return true;
}

}  // namespace

bool readCarInfoStrict(const std::string& b, InfoFile& out, std::string& why) {
    out = InfoFile();
    struct Turn {
        const char* key;
        int kind;  // 0 word, 1 text, 2 integer, 3 number
        size_t maxLen;
    };
    static const Turn turns[12] = {{"BaseName=", 0, 39},    {"Description=", 1, 79}, {"Colors=", 1, 99},
                                   {"Flags=", 2, 0},       {"Order=", 2, 0},        {"ScoringBias=", 3, 0},
                                   {"UnlockScore=", 2, 0}, {"UnlockFlags=", 2, 0},  {"Horsepower=", 2, 0},
                                   {"Top Speed=", 2, 0},   {"Durability=", 2, 0},   {"Mass=", 2, 0}};
    size_t pos = 0;
    auto readLine = [&](size_t maxChars, std::string& line) {
        line.clear();
        while (pos < b.size() && line.size() < maxChars) {
            const char c = b[pos++];
            line.push_back(c);
            if (c == '\n') break;
        }
    };
    for (int t = 0; t < 12; ++t) {
        while (pos < b.size() && isWs(b[pos])) ++pos;
        if (pos >= b.size()) {
            why = std::string("the file ends before ") + turns[t].key;
            return false;
        }
        std::string line;
        readLine(255, line);
        size_t p = 0;
        if (!matchKey(line, p, turns[t].key)) {
            why = std::string("line ") + std::to_string(t + 1) + " is not " + turns[t].key + "…";
            return false;
        }
        std::string value;
        if (turns[t].kind == 0) {
            while (p < line.size() && isWs(line[p])) ++p;
            const size_t st = p;
            while (p < line.size() && line[p] != ' ' && line[p] != '\t' && line[p] != '\r' && line[p] != '\n') ++p;
            value = line.substr(st, p - st);
        } else if (turns[t].kind == 1) {
            const size_t st = p;
            while (p < line.size() && line[p] != '\r') ++p;
            value = line.substr(st, p - st);
        } else {
            while (p < line.size() && isWs(line[p])) ++p;
            const char* start = line.c_str() + p;
            char* end = nullptr;
            if (turns[t].kind == 2) {
                const size_t q = p + ((line[p] == '+' || line[p] == '-') ? 1 : 0);
                if (q >= line.size() || line[q] < '0' || line[q] > '9') {
                    why = std::string(turns[t].key) + " has no number";
                    return false;
                }
                value = std::to_string(std::strtol(start, &end, 10));
            } else {
                const double v = std::strtod(start, &end);
                if (end == start) {
                    why = std::string(turns[t].key) + " has no number";
                    return false;
                }
                value = std::to_string(v);
            }
        }
        if (value.empty() || (turns[t].maxLen && value.size() > turns[t].maxLen)) {
            why = std::string(turns[t].key) + (value.empty() ? " is empty" : " is too long");
            return false;
        }
        std::string key = turns[t].key;
        key.pop_back();  // the '='
        out.values.emplace_back(key, value);
    }
    // UIDist lines, read 127 characters at a time; the last one wins.
    double uiDist = 6.0;
    while (pos < b.size()) {
        std::string line;
        readLine(127, line);
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line.substr(0, eq) != "UIDist") continue;
        size_t p = eq + 1;
        size_t e = line.find(' ', p);
        std::string v = line.substr(p, e == std::string::npos ? std::string::npos : e - p);
        uiDist = std::strtod(v.c_str(), nullptr);
    }
    out.values.emplace_back("UIDist", std::to_string(uiDist));
    return true;
}

namespace {

bool loadCar(const Vfs& vfs, const std::string& path, bool builtIn, CarInfo& car) {
    BytesPtr b = vfs.open(path);
    if (!b) return false;
    car.infoPath = vfs.storedName(path);
    car.archive = vfs.archiveOf(path);
    car.fromMod = !vfs.fromBaseArchive(path);
    car.builtIn = builtIn;
    // MM2's strict twelve-line reading (car-model.md); a car that fails it is skipped.
    std::string why;
    if (!readCarInfoStrict(latin1ToUtf8(std::string(b->begin(), b->end())), car.info, why)) {
        logFile(LogLevel::Warn, car.infoPath, why + "; MM2 skips this car");
        return false;
    }
    car.baseName = car.info.get("BaseName");
    car.description = trim(car.info.get("Description"));  // LF-only files keep a trailing line feed
    car.uiDist = float(std::atof(car.info.get("UIDist").c_str()));
    std::string colors = car.info.get("Colors");
    if (!colors.empty())
        for (const std::string& c : split(colors, '|')) car.colors.push_back(trim(c));
    if (car.baseName.empty()) {
        logFile(LogLevel::Warn, car.infoPath, "empty BaseName=; skipped");
        return false;
    }
    return true;
}

}  // namespace

std::vector<CarInfo> buildCarList(const Vfs& vfs) {
    std::vector<CarInfo> cars;
    std::set<std::string> seen;  // BaseName values already listed (exact, case-sensitive)
    auto add = [&](const std::string& path, bool builtIn) {
        CarInfo car;
        if (!loadCar(vfs, path, builtIn, car)) return;
        if (!seen.insert(car.baseName).second) return;  // the first car with a BaseName wins
        cars.push_back(std::move(car));
    };
    for (const std::string& name : builtInCarNames()) {
        std::string path = "tune/" + name + ".info";
        if (!vfs.exists(path)) {
            logFile(LogLevel::Warn, path, "built-in car's .info file is missing");
            continue;
        }
        add(path, true);
    }
    // tune/ walked archive by archive in priority order, entries in stored order (ar.md).
    for (const std::string& stored : vfs.list("tune/")) {
        // Only the spellings ".info" and ".INFO" count, and only directly inside tune/.
        if (!(endsWith(stored, ".info") || endsWith(stored, ".INFO"))) continue;
        std::string rest = stored.substr(5);
        if (rest.find('/') != std::string::npos || rest.find('\\') != std::string::npos) continue;
        add(stored, false);
    }
    return cars;
}

}  // namespace cc
