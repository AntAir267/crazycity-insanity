#include "audio/text_reader.h"

#include <cstdlib>

#include "vfs/vfs.h"

namespace cc {
namespace audio {

bool LineReader::read(std::string& raw) {
    raw.clear();
    if (!b_ || ended_) return false;
    const Bytes& b = *b_;
    while (pos_ < b.size() && raw.size() < 999) {
        const uint8_t c = b[pos_];
        if (c == 0xFF) {
            ended_ = true;
            break;
        }
        ++pos_;
        raw.push_back(char(c));
        if (c == '\n') break;
    }
    return !raw.empty();
}

std::string cutLine(const std::string& raw, bool skipLeading) {
    size_t i = 0;
    if (skipLeading)
        while (i < raw.size() && (raw[i] == '\r' || raw[i] == '\n')) ++i;
    size_t j = i;
    while (j < raw.size() && raw[j] != '\r' && raw[j] != '\n') ++j;
    return raw.substr(i, j - i);
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && line[i] == ',') ++i;
        if (i >= line.size()) break;
        size_t j = i;
        while (j < line.size() && line[j] != ',') ++j;
        out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

double atofC(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

int atoiC(const std::string& s) { return std::atoi(s.c_str()); }

double fieldD(const std::vector<std::string>& f, size_t i) { return i < f.size() ? atofC(f[i]) : 0.0; }

int fieldI(const std::vector<std::string>& f, size_t i) { return i < f.size() ? atoiC(f[i]) : 0; }

const std::string& fieldS(const std::vector<std::string>& f, size_t i) {
    static const std::string empty;
    return i < f.size() ? f[i] : empty;
}

LineReader openCsv(const Vfs& vfs, const std::string& path) {
    BytesPtr b = vfs.open(path + ".csv");
    return LineReader(b);
}

}  // namespace audio
}  // namespace cc
