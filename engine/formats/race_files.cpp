#include "formats/race_files.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace cc {

namespace {

bool cSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
bool digit(char c) { return c >= '0' && c <= '9'; }

// Lines as fgets with a buffer of `max` + 1 bytes returns them: at most `max` bytes, the line feed
// included when it fits; a longer line is cut and its rest comes as the next line.
std::vector<std::string> fgetsLines(const std::string& text, size_t max) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = p;
        while (e < text.size() && e - p < max) {
            if (text[e++] == '\n') break;
        }
        out.push_back(text.substr(p, e - p));
        p = e;
    }
    return out;
}

bool blank(const std::string& l) {
    for (char c : l)
        if (!cSpace(c)) return false;
    return true;
}

struct Tok {
    std::string text;
    size_t end = 0;  // index just after the token's delimiter (where strtok continues)
};

std::vector<Tok> strtokPositions(const std::string& line) {
    std::vector<Tok> out;
    size_t p = 0;
    const size_t n = line.size();
    while (true) {
        while (p < n && line[p] == ',') ++p;
        if (p >= n) break;
        const size_t s = p;
        while (p < n && line[p] != ',') ++p;
        Tok t;
        t.text = line.substr(s, p - s);
        t.end = p < n ? p + 1 : p;
        out.push_back(t);
        if (p < n) ++p;
    }
    return out;
}

}  // namespace

int cAtoi(const char* s) {
    while (cSpace(*s)) ++s;
    bool neg = false;
    if (*s == '+' || *s == '-') neg = *s++ == '-';
    long long v = 0;
    while (digit(*s)) {
        v = v * 10 + (*s++ - '0');
        if (v > 0x7fffffffLL + 1) v = 0x7fffffffLL + 1;
    }
    return int(neg ? -v : v);
}

double cAtof(const char* s) {
    while (cSpace(*s)) ++s;
    const char* b = s;
    const char* p = s;
    if (*p == '+' || *p == '-') ++p;
    int digits = 0;
    while (digit(*p)) ++p, ++digits;
    if (*p == '.') {
        ++p;
        while (digit(*p)) ++p, ++digits;
    }
    if (digits == 0) return 0.0;
    if (*p == 'e' || *p == 'E') {
        const char* q = p + 1;
        if (*q == '+' || *q == '-') ++q;
        if (digit(*q)) {
            while (digit(*q)) ++q;
            p = q;
        }
    }
    const std::string num(b, size_t(p - b));
    return std::strtod(num.c_str(), nullptr);
}

std::vector<std::string> strtokCommas(const std::string& line) {
    std::vector<std::string> out;
    for (const Tok& t : strtokPositions(line)) out.push_back(t.text);
    return out;
}

// ---- Race settings ----

std::vector<RaceDataRow> parseRaceData(const std::string& text) {
    std::vector<RaceDataRow> rows;
    const std::vector<std::string> lines = fgetsLines(text, 255);
    for (size_t i = 1; i < lines.size() && rows.size() < 15; ++i) {
        if (blank(lines[i])) continue;  // engine: MM2 crashes on a blank line
        const std::vector<std::string> t = strtokCommas(lines[i]);
        RaceDataRow r;
        r.fields = int(t.size());
        auto f = [&](size_t k) -> const char* { return k < t.size() ? t[k].c_str() : ""; };
        r.description = t.empty() ? std::string() : t[0];
        for (int half = 0; half < 2; ++half) {
            RaceSettings& s = half ? r.professional : r.amateur;
            const size_t b = 1 + size_t(half) * 10;
            s.carType = cAtoi(f(b + 0));
            s.timeOfDay = cAtoi(f(b + 1));
            s.weather = cAtoi(f(b + 2));
            s.opponents = cAtoi(f(b + 3));
            s.cops = cAtoi(f(b + 4));
            s.ambient = float(cAtof(f(b + 5)));
            s.peds = float(cAtof(f(b + 6)));
            s.numLaps = cAtoi(f(b + 7));
            s.timeLimit = float(cAtof(f(b + 8)));
            s.difficulty = float(cAtof(f(b + 9)));
        }
        rows.push_back(r);
    }
    return rows;
}

// ---- Waypoints ----

void computeGate(Waypoint& w) {
    const float h = w.heading * 0.017453292f;
    const float c = float(std::cos(double(h)));
    const double s = std::sin(double(h));
    const float cr = c * w.radius;
    const float sr = float(s * double(w.radius));
    w.A = vec2{w.pos.x + cr, w.pos.z + sr};
    w.B = vec2{w.pos.x - cr, w.pos.z - sr};
}

namespace {
bool zeroHeading(float a) { return a == 0.0f || std::isnan(a); }
float aimHeading(const Waypoint& from, const Waypoint& to) {
    const float dx = from.pos.x - to.pos.x, dz = from.pos.z - to.pos.z;
    return float(double(-57.295776f) * std::atan2(double(dx), double(dz)));
}
}  // namespace

WaypointFile parseWaypoints(const std::string& text, int slots, bool circuit) {
    WaypointFile f;
    f.found = true;
    const std::vector<std::string> lines = fgetsLines(text, 127);
    std::vector<bool> fileZero;
    auto add = [&](const Waypoint& w, bool zero) {
        f.points.push_back(w);
        fileZero.push_back(zero);
        const size_t i = f.points.size() - 1;
        if (i >= 2 && zeroHeading(f.points[i - 1].heading)) {
            f.points[i - 1].heading = aimHeading(f.points[i - 1], f.points[i]);
            computeGate(f.points[i - 1]);
            ++f.filled;
        }
    };
    for (size_t li = 1; li < lines.size(); ++li) {
        if (int(f.points.size()) >= slots - 1) break;  // the full list ignores further lines
        const std::string& line = lines[li];
        if (blank(line)) continue;  // engine: MM2 counts it and crashes
        ++f.lines;
        const std::vector<Tok> t = strtokPositions(line);
        auto fld = [&](size_t k) -> const char* { return k < t.size() ? t[k].text.c_str() : ""; };
        Waypoint w;
        w.pos = vec3(float(cAtof(fld(0))), float(cAtof(fld(1))), float(cAtof(fld(2))));
        w.heading = float(cAtof(fld(3)));
        int r = cAtoi(fld(4));
        if (r == 0) r = 15;
        w.radius = float(r);
        w.sphere = cAtoi(fld(5));
        if (t.size() >= 8) {
            ++f.fullLines;
            // The message: the rest after field 8 up to a carriage return or line feed.
            size_t p = t[7].end;
            while (p < line.size() && (line[p] == '\r' || line[p] == '\n')) ++p;
            size_t q = p;
            while (q < line.size() && line[q] != '\r' && line[q] != '\n') ++q;
            if (q > p) ++f.messages;
        }
        computeGate(w);
        add(w, zeroHeading(w.heading));
        if (int(f.points.size()) == slots - 1) {
            Waypoint o;
            o.pos = vec3(0, 0, 0);
            o.heading = 0;
            o.radius = 15;
            o.appended = true;
            computeGate(o);
            add(o, true);
        }
    }
    const size_t n = f.points.size();
    if (circuit && n >= 2 && fileZero[n - 1] && zeroHeading(f.points[n - 1].heading)) {
        f.points[n - 1].heading = aimHeading(f.points[n - 1], f.points[0]);
        computeGate(f.points[n - 1]);
        ++f.filled;
    }
    return f;
}

// ---- Crash Course events ----

std::vector<CrashEvent> parseCrashEvents(const std::string& text) {
    std::vector<CrashEvent> out;
    const std::vector<std::string> lines = fgetsLines(text, 255);
    for (size_t i = 1; i < lines.size() && out.size() < 10; ++i) {
        if (blank(lines[i])) continue;
        const std::vector<std::string> t = strtokCommas(lines[i]);
        auto f = [&](size_t k) -> const char* { return k < t.size() ? t[k].c_str() : ""; };
        CrashEvent e;
        e.fields = int(t.size());
        e.filename = t.empty() ? std::string() : t[0];
        if (e.filename.size() > 31) e.filename.resize(31);
        e.kind = cAtoi(f(1));
        e.checkpoints = cAtoi(f(2));
        e.timeLimit = float(cAtof(f(3)));
        e.ambDensity = float(cAtof(f(4)));
        e.misc = float(cAtof(f(5)));
        e.onlyTarget = cAtoi(f(6));
        e.opponents = cAtoi(f(7));
        out.push_back(e);
    }
    return out;
}

// ---- Opponent route files ----

OppFile parseOpp(const std::string& text) {
    OppFile f;
    f.found = true;
    // Pieces of at most 127 bytes ending after a line feed; a 0xFF byte ends the reading.
    std::vector<std::string> pieces;
    size_t p = 0;
    bool stop = false;
    while (p < text.size() && !stop) {
        std::string piece;
        while (p < text.size() && piece.size() < 127) {
            const unsigned char c = static_cast<unsigned char>(text[p]);
            if (c == 0xFF) {
                stop = true;
                break;
            }
            piece.push_back(char(c));
            ++p;
            if (c == '\n') break;
        }
        if (piece.empty()) break;
        if (piece.size() == 127 && piece.back() != '\n') ++f.longLines;
        pieces.push_back(piece);
    }
    if (pieces.empty()) return f;
    f.header = pieces[0];
    while (!f.header.empty() && (f.header.back() == '\n' || f.header.back() == '\r')) f.header.pop_back();
    for (size_t i = 1; i < pieces.size(); ++i) {
        if (blank(pieces[i])) ++f.blankLines;
        const std::vector<std::string> t = strtokCommas(pieces[i]);
        auto fld = [&](size_t k) -> const char* { return k < t.size() ? t[k].c_str() : ""; };
        OppPoint pt;
        pt.tokens = int(t.size());
        pt.pos = vec3(float(cAtof(fld(0))), float(cAtof(fld(1))), float(cAtof(fld(2))));
        pt.heading = cAtof(fld(3));
        f.points.push_back(pt);
    }
    return f;
}

}  // namespace cc
