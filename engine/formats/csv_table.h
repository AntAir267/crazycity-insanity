// MM2's table reader for CSV files (spec/formats/street-props.md, "Reading the tables"): raw-byte
// lines of at most 255 bytes, `#` comments, a header of at most 16 column names, rows whose cells
// fill the columns in order (absent versus empty cells), case-insensitive look-ups.
#pragma once

#include <string>
#include <vector>

namespace cc {

struct CsvTable {
    std::vector<std::string> columns;
    struct Row {
        std::vector<std::string> cells;  // present cells (columns 0 … cells.size() − 1); later ones are absent
        bool present(int col) const { return col >= 0 && size_t(col) < cells.size(); }
        const std::string& cell(int col) const;
    };
    std::vector<Row> rows;

    // −1 if there is no such column / row.
    int column(const std::string& name) const;
    const Row* row(int col, const std::string& value) const;
};

void parseCsvTable(const std::string& bytes, CsvTable& out);

// Numbers as MM2 reads them: a decimal float rounded to float32; a decimal integer.
float csvFloat(const std::string& cell);
int csvInt(const std::string& cell);

}  // namespace cc
