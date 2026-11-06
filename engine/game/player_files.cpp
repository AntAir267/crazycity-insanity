#include "game/player_files.h"

#include <cstring>

namespace cc {
namespace game {

// ---------------------------------------------------------------------------------------------
// Check value

namespace {
struct CrcTable {
    uint32_t t[256];
    CrcTable() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c >> 1) ^ ((c & 1u) ? 0xED7282A0u : 0u);
            t[i] = c;
        }
    }
};
const CrcTable& crcTable() {
    static const CrcTable t;
    return t;
}
}  // namespace

void Mm2Crc::add(const uint8_t* p, size_t n) {
    const CrcTable& t = crcTable();
    for (size_t i = 0; i < n; ++i) s_ = (s_ >> 8) ^ t.t[(s_ ^ p[i]) & 0xFFu];
}

uint32_t mm2Crc(const uint8_t* p, size_t n) {
    Mm2Crc c;
    c.add(p, n);
    return c.value();
}

// ---------------------------------------------------------------------------------------------
// Text

namespace {
// Windows-1252 0x80 … 0x9F (0 = unassigned, kept as the C1 control).
const uint16_t kCp1252High[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                  0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                  0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                  0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};

void putUtf8(std::string& o, uint32_t c) {
    if (c < 0x80) {
        o += char(c);
    } else if (c < 0x800) {
        o += char(0xC0 | (c >> 6));
        o += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        o += char(0xE0 | (c >> 12));
        o += char(0x80 | ((c >> 6) & 0x3F));
        o += char(0x80 | (c & 0x3F));
    } else {
        o += char(0xF0 | (c >> 18));
        o += char(0x80 | ((c >> 12) & 0x3F));
        o += char(0x80 | ((c >> 6) & 0x3F));
        o += char(0x80 | (c & 0x3F));
    }
}
}  // namespace

std::string cp1252ToUtf8(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        uint32_t u = c;
        if (c >= 0x80 && c < 0xA0 && kCp1252High[c - 0x80]) u = kCp1252High[c - 0x80];
        putUtf8(o, u);
    }
    return o;
}

std::string utf8ToCp1252(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        uint32_t u = 0;
        int len = 1;
        if (c < 0x80) u = c;
        else if ((c & 0xE0) == 0xC0) { u = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { u = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { u = c & 0x07; len = 4; }
        else { o += '?'; ++i; continue; }
        if (i + size_t(len) > s.size()) { o += '?'; break; }
        for (int k = 1; k < len; ++k) u = (u << 6) | ((unsigned char)s[i + size_t(k)] & 0x3F);
        i += size_t(len);
        if (u < 0x80 || (u >= 0xA0 && u < 0x100)) { o += char(u); continue; }
        char m = '?';
        for (int k = 0; k < 32; ++k)
            if (kCp1252High[k] == u) m = char(0x80 + k);
        o += m;
    }
    return o;
}

namespace {

// A fixed text field: up to the first zero (leftovers ignored).
std::string readText(const uint8_t* p, size_t n) {
    size_t k = 0;
    while (k < n && p[k]) ++k;
    return cp1252ToUtf8(std::string(reinterpret_cast<const char*>(p), k));
}

// Written zero-filled; the text is cut to n − 1 bytes so the terminator always fits.
void writeText(uint8_t* p, size_t n, const std::string& utf8) {
    std::memset(p, 0, n);
    std::string s = utf8ToCp1252(utf8);
    if (s.size() > n - 1) s.resize(n - 1);
    std::memcpy(p, s.data(), s.size());
}

uint32_t rd32(const Bytes& b, size_t o) { return readU32LE(b.data() + o); }
float rdf(const Bytes& b, size_t o) { return ConfigFile::asFloat(rd32(b, o)); }
void wr32(Bytes& b, size_t o, uint32_t v) { writeU32LE(b.data() + o, v); }
void wrf(Bytes& b, size_t o, float v) { wr32(b, o, ConfigFile::asBits(v)); }

}  // namespace

float ConfigFile::asFloat(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
uint32_t ConfigFile::asBits(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

// ---------------------------------------------------------------------------------------------
// Modes

int recordTypeOfMode(int mode) {
    switch (mode) {
        case ModeCheckpoint: return RecCheckpoint;
        case ModeCircuit: return RecCircuit;
        case ModeBlitz: return RecBlitz;
        case ModeCrash: return RecCrash;
        default: return -1;
    }
}

int modeOfRecordType(int type) {
    static const int m[4] = {ModeCheckpoint, ModeCircuit, ModeBlitz, ModeCrash};
    return type >= 0 && type < 4 ? m[type] : -1;
}

// ---------------------------------------------------------------------------------------------
// players.dir

bool readPlayersDir(const Bytes& b, PlayersDir& out) {
    out = PlayersDir();
    if (b.size() < 8) return false;
    if (rd32(b, 0) != 6) return false;
    const uint32_t n = rd32(b, 4);
    if (n == 0 || n > uint32_t(kMaxDrivers)) return false;
    if (b.size() < 88 + 80 * size_t(n)) return false;
    for (uint32_t i = 0; i < n; ++i) {
        DriverEntry e;
        e.name = readText(b.data() + 8 + 80 * i, 40);
        e.file = readText(b.data() + 48 + 80 * i, 40);
        out.drivers.push_back(e);
    }
    out.last = readText(b.data() + 8 + 80 * n, 80);
    return true;
}

Bytes writePlayersDir(const PlayersDir& d) {
    const size_t n = d.drivers.size();
    Bytes b(88 + 80 * n, 0);
    wr32(b, 0, 6);
    wr32(b, 4, uint32_t(n));
    for (size_t i = 0; i < n; ++i) {
        writeText(b.data() + 8 + 80 * i, 40, d.drivers[i].name);
        writeText(b.data() + 48 + 80 * i, 40, d.drivers[i].file);
    }
    writeText(b.data() + 8 + 80 * n, 80, d.last);
    return b;
}

std::string newDriverFileName(const PlayersDir& d) {
    // k = 0; repeat n times: go through the drivers in order, adding 1 each time one is player<k>.
    int k = 0;
    for (size_t rep = 0; rep < d.drivers.size(); ++rep)
        for (const DriverEntry& e : d.drivers)
            if (e.file == "player" + std::to_string(k)) ++k;
    return "player" + std::to_string(k);
}

// ---------------------------------------------------------------------------------------------
// .sav

uint32_t saveCrc(const Bytes& b) {
    // Covered in this order: stamp, skill, car, paint, mode, race, city, name, network name,
    // file name, address.
    Mm2Crc c;
    c.add(b, 8, 4);
    c.add(b, 12, 1);
    c.add(b, 13, 80);
    c.add(b, 93, 4);
    c.add(b, 97, 4);
    c.add(b, 101, 4);
    c.add(b, 225, 80);
    c.add(b, 105, 40);
    c.add(b, 145, 40);
    c.add(b, 185, 40);
    c.add(b, 305, 40);
    return c.value();
}

SaveRead readSave(const Bytes& b, SaveFile& out) {
    out = SaveFile();
    if (b.empty()) return SaveRead::Missing;
    if (b.size() < 4) return SaveRead::TooShort;
    const uint32_t v = rd32(b, 0);
    if (v < 2) return SaveRead::BadVersion;
    const size_t need = v >= 3 ? 345 : 305;
    if (b.size() < need) return SaveRead::TooShort;
    out.version = v;
    out.storedCrc = rd32(b, 4);
    if (v == 3 && saveCrc(b) != out.storedCrc) return SaveRead::BadCrc;
    out.stamp = rdf(b, 8);
    out.skill = b[12];
    out.car = readText(b.data() + 13, 80);
    out.paint = int32_t(rd32(b, 93));
    out.mode = int32_t(rd32(b, 97));
    out.race = int32_t(rd32(b, 101));
    out.name = readText(b.data() + 105, 40);
    out.netName = readText(b.data() + 145, 40);
    out.fileName = readText(b.data() + 185, 40);
    out.city = readText(b.data() + 225, 80);
    if (v >= 3) out.address = readText(b.data() + 305, 40);  // version 2: counts as empty
    return SaveRead::Ok;
}

Bytes writeSave(const SaveFile& s) {
    Bytes b(345, 0);
    wr32(b, 0, 3);
    wrf(b, 8, s.stamp);
    b[12] = uint8_t(s.skill);
    writeText(b.data() + 13, 80, s.car);
    wr32(b, 93, uint32_t(s.paint));
    wr32(b, 97, uint32_t(s.mode));
    wr32(b, 101, uint32_t(s.race));
    writeText(b.data() + 105, 40, s.name);
    writeText(b.data() + 145, 40, s.netName);
    writeText(b.data() + 185, 40, s.fileName);
    writeText(b.data() + 225, 80, s.city);
    writeText(b.data() + 305, 40, s.address);
    wr32(b, 4, saveCrc(b));
    return b;
}

// ---------------------------------------------------------------------------------------------
// .cfg

namespace {
// DirectInput keyboard scan codes of the default keys (controls.md, "Default keyboard bindings").
enum : uint32_t {
    kUp = 0xC8, kDown = 0xD0, kLeft = 0xCB, kRight = 0xCD, kSpace = 0x39, kReturn = 0x1C, kC = 0x2E,
    kV = 0x2F, kD = 0x20, kW = 0x11, kNum4 = 0x4B, kNum6 = 0x4D, kNum2 = 0x50, kNum8 = 0x48, kT = 0x14,
    kA = 0x1E, kZ = 0x2C, kR = 0x13, kH = 0x23, kTab = 0x0F, kQ = 0x10, kE = 0x12, kF = 0x21,
    kBack = 0x0E, kS = 0x1F, kX = 0x2D, k2 = 0x03, k3 = 0x04, k4 = 0x05, k5 = 0x06, kI = 0x17, kY = 0x15
};
constexpr uint32_t kHeld = 1, kAnalog = 2, kPress = 3;
constexpr uint32_t kDevMouse = 2, kDevKeyboard = 3, kDevJoy = 4;

// Held actions (controls.md, "Default binding table"): 5 – 10, 13 – 17 and 31; every other action
// is a press action.
bool heldAction(int a) { return (a >= 5 && a <= 10) || (a >= 13 && a <= 17) || a == 31; }

}  // namespace

void defaultBindings(Binding (&t)[170], int controllerButtons) {
    // The keyboard key of each action (0 = none). The keyboard's steering-axis entry is the
    // main-row 8 key (scan code 9), unused by that device.
    static const uint32_t key[34] = {kTab, kQ,     kE,    kF,    kH,    0x09, kLeft, kRight, kUp, kDown, kSpace, kC,
                                     kV,   kReturn, kNum4, kNum6, kNum2, kNum8, kW,   kD,     kT,  kA,    kZ,     kR,
                                     kS,   kX,     k2,    k3,    k4,    k5,   kBack, 0,     kI,  kY};
    auto keyOf = [&](int a) { return key[a]; };
    for (int d = 0; d < 5; ++d)
        for (int a = 0; a < 34; ++a) {
            Binding& e = t[d * 34 + a];
            e = Binding();
            const uint32_t k = keyOf(a);
            if (k) e = Binding{heldAction(a) ? kHeld : kPress, kDevKeyboard, k};
        }
    // A controller button n (1-based) or axis component, with the kind rule.
    auto button = [&](int d, int a, int n) { t[d * 34 + a] = Binding{heldAction(a) ? kHeld : kPress, kDevJoy, uint32_t(20 + n)}; };
    auto axis = [&](int d, int a, uint32_t comp) { t[d * 34 + a] = Binding{kAnalog, kDevJoy, comp}; };
    // The look-around axis is the controller's hat on every device.
    for (int d = 0; d < 5; ++d) axis(d, 31, 16);
    // Mouse (device 0): X axis steering, left button throttle, right button brake.
    t[0 * 34 + 5] = Binding{kAnalog, kDevMouse, 10};
    t[0 * 34 + 8] = Binding{kHeld, kDevMouse, 1};
    t[0 * 34 + 9] = Binding{kHeld, kDevMouse, 2};
    // Joystick (device 2).
    button(2, 0, 4);
    axis(2, 5, 10);
    axis(2, 8, 19);
    axis(2, 9, 20);
    button(2, 10, 1);
    button(2, 11, 3);
    button(2, 13, 2);
    // Gamepad (device 3).
    axis(3, 5, 10);
    button(3, 8, 1);
    button(3, 9, 2);
    button(3, 10, 4);
    button(3, 13, 3);
    button(3, 18, 6);
    button(3, 19, 5);
    button(3, 21, 8);
    button(3, 22, 7);
    // Wheel (device 4); some buttons only with a controller of more than 5 (or 7) buttons.
    axis(4, 5, 10);
    axis(4, 8, 19);
    axis(4, 9, 20);
    button(4, 10, 1);
    button(4, 13, 2);
    if (controllerButtons > 5) {
        button(4, 0, 5);
        button(4, 4, 4);
        button(4, 11, 3);
        button(4, 30, 6);
    }
    if (controllerButtons > 7) {
        button(4, 21, 7);
        button(4, 22, 8);
    }
}

ConfigFile::ConfigFile() {
    // Graphics program defaults (profiles.md table).
    graphics[0] = 1;
    graphics[1] = 2;
    graphics[2] = 0;
    graphics[3] = 1;
    graphics[4] = 1;
    graphics[5] = 1;
    graphics[6] = asBits(400.0f);
    graphics[7] = asBits(3.0f);
    graphics[8] = asBits(2.0f / 3.0f + 0.25f);
    graphics[9] = 0;
    graphics[13] = 1;
    // Control program defaults: keyboard, automatic, automatic reverse, force feedback on, dead
    // zone 0.1, K 1, U 1.
    controls[0] = 1;
    controls[1] = 1;
    controls[2] = asBits(0.75f);
    controls[3] = 1;
    controls[4] = 0;
    controls[5] = 1;
    controls[6] = asBits(1.0f);
    controls[7] = asBits(1.0f);
    controls[8] = asBits(1.0f);
    controls[9] = asBits(0.1f);
    controls[10] = 0;
    controls[11] = asBits(1.0f);
    controls[12] = asBits(1.0f);
    defaultBindings(bindings, 0);
    // View bytes before any profile is loaded (controls.md): all 0, map heading-up.
    std::memset(view, 0, sizeof view);
    view[10] = 1;
    view[11] = 0;
}

CfgRead readConfig(const Bytes& b, ConfigFile& out) {
    if (b.empty()) return CfgRead::Missing;
    if (b.size() < 8 || rd32(b, 0) != 1234) return CfgRead::Damaged;
    if (rd32(b, 4) != 31) return CfgRead::OtherVersion;
    if (b.size() < 32) return CfgRead::Damaged;
    const uint32_t n = rd32(b, 28);
    if (n > 200 || b.size() < 2192 + size_t(n)) return CfgRead::Damaged;
    ConfigFile c;
    c.sfxVolume = rdf(b, 8);
    c.musicVolume = rdf(b, 12);
    c.balance = rdf(b, 16);
    c.audioBits = rd32(b, 20);
    c.channels = rd32(b, 24);
    c.device.assign(reinterpret_cast<const char*>(b.data() + 32), n);
    for (int k = 0; k < 14; ++k) c.graphics[k] = rd32(b, 32 + n + 4 * size_t(k));
    for (int k = 0; k < 13; ++k) c.controls[k] = rd32(b, 88 + n + 4 * size_t(k));
    for (int k = 0; k < 170; ++k) {
        const size_t o = 140 + n + 12 * size_t(k);
        c.bindings[k] = Binding{rd32(b, o), rd32(b, o + 4), rd32(b, o + 8)};
    }
    std::memcpy(c.view, b.data() + 2180 + n, 12);
    out = c;
    return CfgRead::Ok;
}

Bytes writeConfig(const ConfigFile& c) {
    std::string dev = c.device;
    if (dev.empty() || dev.back() != '\0') dev += '\0';
    const size_t n = dev.size();
    Bytes b(2192 + n, 0);
    wr32(b, 0, 1234);
    wr32(b, 4, 31);
    wrf(b, 8, c.sfxVolume);
    wrf(b, 12, c.musicVolume);
    wrf(b, 16, c.balance);
    wr32(b, 20, c.audioBits);
    wr32(b, 24, c.channels);
    wr32(b, 28, uint32_t(n));
    std::memcpy(b.data() + 32, dev.data(), n);
    for (int k = 0; k < 14; ++k) wr32(b, 32 + n + 4 * size_t(k), c.graphics[k]);
    for (int k = 0; k < 13; ++k) wr32(b, 88 + n + 4 * size_t(k), c.controls[k]);
    for (int k = 0; k < 170; ++k) {
        const size_t o = 140 + n + 12 * size_t(k);
        wr32(b, o, c.bindings[k].kind);
        wr32(b, o + 4, c.bindings[k].device);
        wr32(b, o + 8, c.bindings[k].component);
    }
    std::memcpy(b.data() + 2180 + n, c.view, 12);
    return b;
}

void newDriverViewBytes(uint8_t view[12], const uint8_t current[12]) {
    for (int i = 0; i < 12; ++i) view[i] = 0;
    view[6] = current[6];  // kept
    view[7] = 1;  // opponent markers on
    view[9] = current[9];  // "0, kept"
    view[10] = current[10];
    view[11] = current[11];
}

// ---------------------------------------------------------------------------------------------
// .rec

int RecordFile::recordIndex(int type, int race) const {
    if (type < 0 || type >= 4 || race < 0 || race >= counts[type]) return -1;
    int i = race;
    for (int t = 0; t < type; ++t) i += counts[t];
    return i < int(records.size()) ? i : -1;
}

RaceRecord* RecordFile::record(int type, int race) {
    const int i = recordIndex(type, race);
    return i < 0 ? nullptr : &records[size_t(i)];
}

const RaceRecord* RecordFile::record(int type, int race) const {
    const int i = recordIndex(type, race);
    return i < 0 ? nullptr : &records[size_t(i)];
}

int RecordFile::passedCount(int type) const {
    if (type < 0 || type >= 4) return 0;
    int n = 0;
    for (int r = 0; r < counts[type] && r < 32; ++r)
        if (passed[type] & (1u << r)) ++n;
    return n;
}

namespace {
uint32_t recordHeaderCrc(const Bytes& b) {
    Mm2Crc c;
    c.add(b, 8, 36);
    return c.value();
}
void writeRecordEntry(uint8_t* p, const RaceRecord& r) {
    std::memset(p, 0, 96);
    uint32_t t = ConfigFile::asBits(r.time);
    writeU32LE(p + 4, t);
    writeText(p + 8, 80, r.car);
    writeU32LE(p + 88, uint32_t(r.passed));
    writeU32LE(p + 92, uint32_t(r.score));
    writeU32LE(p, mm2Crc(p + 4, 92));
}
}  // namespace

uint32_t recordEntryCrc(const RaceRecord& r) {
    uint8_t buf[96];
    writeRecordEntry(buf, r);
    return readU32LE(buf);
}

RecRead readRecord(const Bytes& b, RecordFile& out) {
    out = RecordFile();
    if (b.empty()) return RecRead::Missing;
    if (b.size() < 44) return RecRead::TooShort;
    out.version = rdf(b, 0);
    out.storedCrc = rd32(b, 4);
    out.stamp = rdf(b, 8);
    long total = 0;
    for (int t = 0; t < 4; ++t) {
        out.counts[t] = int32_t(rd32(b, 12 + 4 * size_t(t)));
        out.passed[t] = rd32(b, 28 + 4 * size_t(t));
        if (out.counts[t] < 0 || out.counts[t] > 4096) return RecRead::TooShort;
        total += out.counts[t];
    }
    if (out.version != 2.0f) return RecRead::BadVersion;
    out.headerCrcOk = recordHeaderCrc(b) == out.storedCrc;
    if (!out.headerCrcOk) return RecRead::BadCrc;
    if (b.size() < 44 + 96 * size_t(total)) return RecRead::TooShort;
    out.records.resize(size_t(total));
    for (long i = 0; i < total; ++i) {
        const uint8_t* p = b.data() + 44 + 96 * size_t(i);
        RaceRecord& r = out.records[size_t(i)];
        r.storedCrc = readU32LE(p);
        r.crcOk = mm2Crc(p + 4, 92) == r.storedCrc;
        r.time = ConfigFile::asFloat(readU32LE(p + 4));
        r.car = readText(p + 8, 80);
        r.passed = int32_t(readU32LE(p + 88));
        r.score = int32_t(readU32LE(p + 92));
    }
    return RecRead::Ok;
}

Bytes writeRecord(const RecordFile& r) {
    Bytes b(44 + 96 * r.records.size(), 0);
    wrf(b, 0, r.version);
    wrf(b, 8, r.stamp);
    for (int t = 0; t < 4; ++t) {
        wr32(b, 12 + 4 * size_t(t), uint32_t(r.counts[t]));
        wr32(b, 28 + 4 * size_t(t), r.passed[t]);
    }
    wr32(b, 4, recordHeaderCrc(b));
    for (size_t i = 0; i < r.records.size(); ++i) writeRecordEntry(b.data() + 44 + 96 * i, r.records[i]);
    return b;
}

RecordFile freshRecord(float stamp, int checkpoint, int circuit, int blitz) {
    RecordFile r;
    r.version = 2.0f;
    r.stamp = stamp;
    r.counts[0] = checkpoint;
    r.counts[1] = circuit;
    r.counts[2] = blitz;
    r.counts[3] = kCrashCourseCount;
    r.records.assign(size_t(checkpoint + circuit + blitz + kCrashCourseCount), RaceRecord());
    for (RaceRecord& e : r.records) e.storedCrc = 0x695A3C42u;
    return r;
}

// ---------------------------------------------------------------------------------------------
// Hall of fame

int HallOfFame::entryIndex(int type, int race, int e) const {
    if (type < 0 || type >= 3 || race < 0 || race >= counts[type] || e < 0 || e >= 10) return -1;
    int r = race;
    for (int t = 0; t < type; ++t) r += counts[t];
    const int i = 10 * r + e;
    return i < int(entries.size()) ? i : -1;
}

namespace {
void writeFameEntry(uint8_t* p, const FameEntry& e) {
    std::memset(p, 0, 132);
    writeText(p + 4, 40, e.name);
    writeText(p + 44, 80, e.car);
    writeU32LE(p + 124, ConfigFile::asBits(e.value));
    writeU32LE(p + 128, e.passed);
    writeU32LE(p, mm2Crc(p + 4, 128));
}
}  // namespace

uint32_t fameEntryCrc(const FameEntry& e) {
    uint8_t buf[132];
    writeFameEntry(buf, e);
    return readU32LE(buf);
}

FameRead readHallOfFame(const Bytes& b, HallOfFame& out) {
    out = HallOfFame();
    if (b.empty()) return FameRead::Missing;
    if (b.size() < 16) return FameRead::TooShort;
    out.version = rd32(b, 0);
    if (out.version != 1) return FameRead::BadVersion;
    long races = 0;
    for (int t = 0; t < 3; ++t) {
        out.counts[t] = int32_t(rd32(b, 4 + 4 * size_t(t)));
        if (out.counts[t] < 0 || out.counts[t] > 4096) return FameRead::TooShort;
        races += out.counts[t];
    }
    if (b.size() < 16 + 1320 * size_t(races)) return FameRead::TooShort;
    out.entries.resize(size_t(races) * 10);
    for (size_t i = 0; i < out.entries.size(); ++i) {
        const uint8_t* p = b.data() + 16 + 132 * i;
        FameEntry& e = out.entries[i];
        e.storedCrc = readU32LE(p);
        e.crcOk = mm2Crc(p + 4, 128) == e.storedCrc;
        e.name = readText(p + 4, 40);
        e.car = readText(p + 44, 80);
        e.value = ConfigFile::asFloat(readU32LE(p + 124));
        e.passed = readU32LE(p + 128);
    }
    return FameRead::Ok;
}

Bytes writeHallOfFame(const HallOfFame& h) {
    Bytes b(16 + 132 * h.entries.size(), 0);
    wr32(b, 0, 1);
    for (int t = 0; t < 3; ++t) wr32(b, 4 + 4 * size_t(t), uint32_t(h.counts[t]));
    for (size_t i = 0; i < h.entries.size(); ++i) writeFameEntry(b.data() + 16 + 132 * i, h.entries[i]);
    return b;
}

HallOfFame freshHallOfFame(int checkpoint, int circuit, int blitz) {
    HallOfFame h;
    h.counts[0] = checkpoint;
    h.counts[1] = circuit;
    h.counts[2] = blitz;
    h.entries.assign(size_t(checkpoint + circuit + blitz) * 10, FameEntry());
    for (FameEntry& e : h.entries) e.storedCrc = 0x3DE0A897u;
    return h;
}

// ---------------------------------------------------------------------------------------------
// Rewards

namespace {
int atoiC(const std::string& s) {
    // C atoi: leading white space, optional sign, digits.
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\v' || s[i] == '\f'))
        ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    long v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        v = v * 10 + (s[i++] - '0');
        if (v > 0x7fffffff) v = 0x7fffffff;
    }
    return int(neg ? -v : v);
}

bool iequalsAscii(const std::string& a, const char* b) {
    const size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; ++i) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}
}  // namespace

std::vector<RewardLine> parseRewardsAll(const std::string& text) {
    std::vector<RewardLine> out;
    // Lines as fgets with a 128-byte buffer: at most 127 bytes, up to and including the line feed.
    size_t pos = 0;
    bool header = true;
    while (pos < text.size()) {
        std::string line;
        while (pos < text.size() && line.size() < 127) {
            const char c = text[pos++];
            line += c;
            if (c == '\n') break;
        }
        if (header) {
            header = false;
            continue;
        }
        // Split at commas; runs of commas count as one (strtok).
        std::vector<std::string> f;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && line[i] == ',') ++i;
            if (i >= line.size()) break;
            size_t j = i;
            while (j < line.size() && line[j] != ',') ++j;
            f.push_back(line.substr(i, j - i));
            i = j;
        }
        if (f.empty() || f[0].empty() || f[0][0] == '#') continue;
        static const struct {
            const char* word;
            int mode;
        } kWords[] = {{"roam", ModeCruise},     {"race", ModeCheckpoint}, {"multicop", ModeCopsRobbers},
                      {"circuit", ModeCircuit}, {"blitz", ModeBlitz},     {"croam", ModeMultiCruise},
                      {"crash", ModeCrash}};
        int mode = -1;
        for (const auto& w : kWords)
            if (iequalsAscii(f[0], w.word)) mode = w.mode;
        if (mode < 0 || f.size() < 4) continue;
        RewardLine r;
        r.mode = mode;
        if (iequalsAscii(f[1], "half")) r.condition = RewardLine::kHalf;
        else if (iequalsAscii(f[1], "all")) r.condition = RewardLine::kAll;
        else r.condition = atoiC(f[1]);
        r.car = f[2];
        r.variant = atoiC(f[3]);
        if (f.size() > 4) {
            std::string m = f[4];
            while (!m.empty() && (m.back() == '\n' || m.back() == '\r')) m.pop_back();
            r.message = cp1252ToUtf8(m);
        }
        out.push_back(std::move(r));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// Base64

std::string base64Encode(const Bytes& b) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((b.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < b.size(); i += 3) {
        const uint32_t v = (uint32_t(b[i]) << 16) | (uint32_t(b[i + 1]) << 8) | b[i + 2];
        o += k[v >> 18];
        o += k[(v >> 12) & 63];
        o += k[(v >> 6) & 63];
        o += k[v & 63];
    }
    if (i + 1 == b.size()) {
        const uint32_t v = uint32_t(b[i]) << 16;
        o += k[v >> 18];
        o += k[(v >> 12) & 63];
        o += "==";
    } else if (i + 2 == b.size()) {
        const uint32_t v = (uint32_t(b[i]) << 16) | (uint32_t(b[i + 1]) << 8);
        o += k[v >> 18];
        o += k[(v >> 12) & 63];
        o += k[(v >> 6) & 63];
        o += '=';
    }
    return o;
}

bool base64Decode(const std::string& s, Bytes& out) {
    out.clear();
    uint32_t acc = 0;
    int bits = 0;
    for (char c : s) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        acc = (acc << 6) | uint32_t(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(uint8_t((acc >> bits) & 0xFF));
        }
    }
    return true;
}

}  // namespace game
}  // namespace cc
