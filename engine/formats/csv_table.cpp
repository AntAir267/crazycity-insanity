#include "formats/csv_table.h"

#include <cstdlib>

#include "core/strutil.h"

namespace cc {

namespace {

bool isStop(unsigned char c) { return c < 0x20 || c >= 0x80; }

std::vector<std::string> splitCells(const std::string& line) {
    std::vector<std::string> out;
    size_t pos = 0;
    const size_t n = line.size();
    while (true) {
        while (pos < n && isStop(static_cast<unsigned char>(line[pos]))) ++pos;
        const size_t start = pos;
        while (pos < n && line[pos] != ',' && !isStop(static_cast<unsigned char>(line[pos]))) ++pos;
        std::string cell = line.substr(start, pos - start);
        bool consumed = false;
        if (pos < n) {
            ++pos;
            consumed = true;
        }
        if (!cell.empty() || consumed) out.push_back(cell);
        if (pos >= n) break;
    }
    return out;
}

}  // namespace

const std::string& CsvTable::Row::cell(int col) const {
    static const std::string none;
    return present(col) ? cells[size_t(col)] : none;
}

int CsvTable::column(const std::string& name) const {
    for (size_t i = 0; i < columns.size(); ++i)
        if (iequals(columns[i], name)) return int(i);
    return -1;
}

const CsvTable::Row* CsvTable::row(int col, const std::string& value) const {
    if (col < 0) return nullptr;
    for (const Row& r : rows)
        if (r.present(col) && iequals(r.cells[size_t(col)], value)) return &r;
    return nullptr;
}

void parseCsvTable(const std::string& bytes, CsvTable& out) {
    out = CsvTable();
    // Lines of at most 255 bytes, as fgets with a 256-byte buffer reads them.
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < bytes.size()) {
        size_t end = pos;
        while (end < bytes.size() && end - pos < 255) {
            ++end;
            if (bytes[end - 1] == '\n') break;
        }
        std::string line = bytes.substr(pos, end - pos);
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        lines.push_back(line);
        pos = end;
    }
    if (lines.empty()) return;
    std::vector<std::string> head = splitCells(lines[0]);
    if (head.size() > 16) head.resize(16);
    out.columns = head;
    for (size_t i = 1; i < lines.size(); ++i) {
        CsvTable::Row r;
        r.cells = splitCells(lines[i]);
        if (r.cells.size() > out.columns.size()) r.cells.resize(out.columns.size());
        out.rows.push_back(r);
    }
}

float csvFloat(const std::string& cell) { return float(std::strtod(cell.c_str(), nullptr)); }

int csvInt(const std::string& cell) { return std::atoi(cell.c_str()); }

}  // namespace cc
