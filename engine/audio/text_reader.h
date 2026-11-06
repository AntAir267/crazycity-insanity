// The line and field reading shared by MM2's audio text files (spec/audio/car-sounds.md "Reading
// the text files", tyre-impact-sounds.md "Reading rules shared by these files",
// traffic-sounds.md "Reading lines and fields", world-sounds.md "The ambient file").
#pragma once

#include <string>
#include <vector>

#include "core/bytes.h"

namespace cc {
class Vfs;

namespace audio {

class LineReader {
public:
    LineReader() = default;
    explicit LineReader(BytesPtr bytes) : b_(std::move(bytes)) {}
    bool valid() const { return b_ != nullptr; }
    // One read: up to and including its line feed, at most 999 characters (a longer line continues
    // as the next read). False when the read gets no byte at all (end of file). A byte 0xFF ends
    // the read like the end of the file.
    bool read(std::string& raw);
    bool atEnd() const { return !b_ || ended_ || pos_ >= b_->size(); }

private:
    BytesPtr b_;
    size_t pos_ = 0;
    bool ended_ = false;
};

// The line ended at its first CR or LF. With skipLeading, CR/LF at its very start are skipped
// first (tyre-impact-sounds.md "Cut the line").
std::string cutLine(const std::string& raw, bool skipLeading = false);
// Fields split at commas the way C strtok(",") does: runs of commas count as one separator and
// leading commas are skipped (an empty field cannot be expressed).
std::vector<std::string> splitFields(const std::string& line);
// C atof / strtod (decimal, optional sign, point and exponent; longest valid prefix after white
// space; no number = 0) and atoi.
double atofC(const std::string& s);
inline float atofF(const std::string& s) { return float(atofC(s)); }
int atoiC(const std::string& s);
// Field i as a number (0 when missing; MM2 would crash).
double fieldD(const std::vector<std::string>& f, size_t i);
inline float fieldF(const std::vector<std::string>& f, size_t i) { return float(fieldD(f, i)); }
int fieldI(const std::vector<std::string>& f, size_t i);
const std::string& fieldS(const std::vector<std::string>& f, size_t i);

// Opens `path` + ".csv" (the readers add the extension) or returns an invalid reader.
LineReader openCsv(const Vfs& vfs, const std::string& pathWithoutExtension);

}  // namespace audio
}  // namespace cc
