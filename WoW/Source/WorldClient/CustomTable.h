#ifndef WOW_SOURCE_WORLDCLIENT_CUSTOMTABLE_H
#define WOW_SOURCE_WORLDCLIENT_CUSTOMTABLE_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Not in the original binary. A custom WDBC table (.cdbc, seyris-living-azeroth's tables): rows of 32-bit columns, read
// from the archives or a loose file under the client folder, and its string block.
namespace CustomTable {

struct Table {
    uint32_t fields = 0;
    std::vector<uint32_t> cells;   // rows * fields
    std::vector<char> strings;     // the string block (a string column holds an offset into it)
    uint32_t Rows() const { return fields ? static_cast<uint32_t>(cells.size() / fields) : 0; }
    uint32_t U(uint32_t row, uint32_t column) const { return cells[row * fields + column]; }
    float F(uint32_t row, uint32_t column) const {
        float f;
        std::memcpy(&f, &cells[row * fields + column], sizeof f);
        return f;
    }
    const char* S(uint32_t row, uint32_t column) const {
        const uint32_t offset = U(row, column);
        return offset < strings.size() ? strings.data() + offset : "";
    }
};


// Columns are only appended to these tables, so a file with at least `minFields` columns is accepted.
bool LoadTable(const char* path, uint32_t minFields, Table& out, char* status, size_t statusSize);

}  // namespace CustomTable

#endif
