#include "formats/aimap.h"

#include <cstdio>
#include <cstring>
#include <functional>

#include "core/log.h"

namespace cc {

namespace {

// Lines as the game's line reader returns them: at most 127 bytes, the newline included when it
// fits; a longer line is cut and its rest comes as the next line.
std::vector<std::string> readLines(const std::string& text) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = p;
        while (e < text.size() && e - p < 127) {
            if (text[e++] == '\n') break;
        }
        out.push_back(text.substr(p, e - p));
        p = e;
    }
    return out;
}

bool startsWithN(const std::string& line, const char* header, size_t k) {
    return std::strncmp(line.c_str(), header, k) == 0;
}

struct Header {
    const char* text;
    size_t k;
    // value section: called with the value line; list section: called per entry line
    bool list;
    std::function<void(const std::string&)> handle;
    bool recheckValueLine = false;  // the value line is compared with the remaining headers
    std::function<void()> begin;    // list: before its entries
    std::function<void(int)> countLine = {};  // list: the count line's value
};

void readFile(const std::string& text, std::vector<Header>& headers) {
    const std::vector<std::string> lines = readLines(text);
    size_t i = 0;
    while (i < lines.size()) {
        std::string cur = lines[i++];
        for (size_t h = 0; h < headers.size(); ++h) {
            Header& H = headers[h];
            if (!startsWithN(cur, H.text, H.k)) continue;
            if (!H.list) {
                if (i >= lines.size()) break;
                const std::string& value = lines[i++];
                H.handle(value);
                if (H.recheckValueLine) {
                    cur = value;  // the chain goes on with the value line
                    continue;
                }
                break;
            }
            // List: count line, then entries.
            int count = 0;
            if (i < lines.size()) {
                if (std::sscanf(lines[i].c_str(), "%d", &count) != 1) count = 0;
                ++i;
            }
            if (H.begin) H.begin();
            if (H.countLine) H.countLine(count);
            int stored = 0;
            while (stored < count && i < lines.size()) {
                const std::string& l = lines[i++];
                if (!l.empty() && l[0] == '#') continue;
                if (!l.empty() && l[0] == '[') break;  // ends the list; this line is lost
                H.handle(l);
                ++stored;
            }
            break;
        }
    }
}

void fixThresholds(std::vector<AmbientType>& v) {
    if (v.empty() || !(v[0].threshold < 0.0f)) return;
    const float inv = 1.0f / float(v.size());
    for (size_t i = 0; i < v.size(); ++i) v[i].threshold = inv * float(i + 1);
}

void ambientRow(const std::string& l, std::vector<AmbientType>& out) {
    char name[256] = {};
    AmbientType t;
    if (std::sscanf(l.c_str(), "%255s %f %d", name, &t.threshold, &t.value) >= 2) {
        t.model = name;
        if (t.model.size() > 31) logWarn("aimap: model name longer than 31 characters: %s", name);
        out.push_back(t);
    }
}

void pedRow(const std::string& l, std::vector<PedPair>& out) {
    char a[256] = {}, b[256] = {};
    if (std::sscanf(l.c_str(), "%255s %255s", a, b) == 2) out.push_back({a, b});
}

}  // namespace

CityAimap parseCityAimap(const std::string& text) {
    CityAimap m;
    std::vector<Header> h;
    h.push_back({"[Ped Pool]", 10, false, [&](const std::string& l) { std::sscanf(l.c_str(), "%d", &m.pedPool); }, true, {}});
    h.push_back({"[Speed Limit]", 13, false, [&](const std::string& l) { std::sscanf(l.c_str(), "%f", &m.speedLimit); }, false, {}});
    h.push_back({"[Subway]", 8, false,
                 [&](const std::string& l) {
                     char name[256] = {};
                     const int got = std::sscanf(l.c_str(), "%255s %d", name, &m.subwayCars);
                     if (got >= 1) {
                         m.subwayModel = name;
                         m.haveSubway = true;
                     }
                 },
                 false, {}});
    h.push_back({"[Ambients Drive On The Left]", 28, false,
                 [&](const std::string& l) {
                     int v = 0;
                     if (std::sscanf(l.c_str(), "%d", &v) == 1) m.driveOnLeft = v != 0;
                 },
                 false, {}});
    h.push_back({"[Ambient Types/Density]", 13, true, [&](const std::string& l) { ambientRow(l, m.ambient); }, false,
                 [&]() { m.ambient.clear(); }});
    h.push_back({"[GoodWeatherPedName / BadWeatherPedName]", 24, true, [&](const std::string& l) { pedRow(l, m.peds); },
                 false, [&]() { m.peds.clear(); }});
    h.push_back({"[Traffic Lights]", 16, false,
                 [&](const std::string& l) {
                     char a[256] = {}, b[256] = {};
                     const int got = std::sscanf(l.c_str(), "%255s %255s", a, b);
                     if (got >= 1) m.lightSingle = a;
                     if (got >= 2) m.lightDual = b;
                 },
                 false, {}});
    readFile(text, h);
    fixThresholds(m.ambient);
    return m;
}

RaceAimap parseRaceAimap(const std::string& text) {
    RaceAimap m;
    m.found = true;
    std::vector<Header> h;
    h.push_back({"[AmbientLaneChanges]", 20, false,
                 [&](const std::string& l) {
                     int v = 0;
                     if (std::sscanf(l.c_str(), "%d", &v) == 1) m.laneChanges = v != 0;
                 },
                 true, {}});
    h.push_back({"[CopChaseDistance]", 18, false, [&](const std::string& l) { std::sscanf(l.c_str(), "%f", &m.copChaseDistance); },
                 false, {}});
    h.push_back({"[Ambient Types/Density]", 13, true, [&](const std::string& l) { ambientRow(l, m.ambient); }, false,
                 [&]() { m.ambient.clear(); }});
    h.push_back({"[GoodWeatherPedName / BadWeatherPedName]", 24, true, [&](const std::string& l) { pedRow(l, m.peds); },
                 false, [&]() { m.peds.clear(); }});
    h.push_back({"[Exceptions]", 12, true,
                 [&](const std::string& l) {
                     RoadException e;
                     if (std::sscanf(l.c_str(), "%d %f %f", &e.road, &e.density, &e.speedLimit) == 3) m.exceptions.push_back(e);
                 },
                 false, [&]() { m.exceptions.clear(); }});
    h.push_back({"[Police]", 8, true,
                 [&](const std::string& l) {
                     char name[256] = {};
                     PoliceEntry p;
                     float deg = 0;
                     const int got = std::sscanf(l.c_str(), "%255s %f %f %f %f %d %d %f %f", name, &p.pos.x, &p.pos.y, &p.pos.z,
                                                 &deg, &p.i0, &p.i1, &p.f0, &p.f1);
                     if (got >= 5) {
                         p.model = name;
                         p.heading = deg * -0.017444445f;
                         m.police.push_back(p);
                     }
                 },
                 false, [&]() { m.police.clear(); }});
    // Opponent rows: the four numbers default before each row; the six switches come from scratch
    // integers that only this reader writes, so a short row keeps the switches of the latest
    // earlier row of the file that gave them (our choice before any row: 0, 1, 1, 1, 1, 0).
    int scratch[6] = {0, 1, 1, 1, 1, 0};
    h.push_back({"[Opponent]", 10, true,
                 [&](const std::string& l) {
                     ++m.opponentRows;
                     char car[256] = {}, file[256] = {};
                     OpponentRow r;
                     int s1 = scratch[0], s2 = scratch[1], s3 = scratch[2], s4 = scratch[3], s5 = scratch[4],
                         s6 = scratch[5];
                     const int got = std::sscanf(l.c_str(), "%255s %255s %f %d %f %f %d %d %d %d %d %f", car, file, &r.throttle,
                                                 &s1, &r.pad, &r.brake, &s2, &s3, &s4, &s5, &s6, &r.corner);
                     r.fields = got < 0 ? 0 : got;
                     const int v[6] = {s1, s2, s3, s4, s5, s6};
                     for (int k = 0; k < 6; ++k) scratch[k] = v[k];
                     for (int k = 0; k < 6; ++k) r.sw[k] = scratch[k] != 0;
                     if (r.fields < 2) return;  // no car or route file: unusable (still counted)
                     r.car = car;
                     r.file = file;
                     m.opponents.push_back(r);
                 },
                 false,
                 [&]() {
                     m.opponentRows = 0;
                     m.opponents.clear();
                 }});
    h.back().countLine = [&](int n) { m.opponentCount = n; };
    h.push_back({"[Hookmen]", 9, true, [&](const std::string&) { ++m.hookmenRows; }, false, [&]() { m.hookmenRows = 0; }});
    readFile(text, h);
    fixThresholds(m.ambient);
    return m;
}

}  // namespace cc
