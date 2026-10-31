#include "WoW/Source/WorldClient/CustomTable.h"

#include <algorithm>
#include <cstdio>

#include "Engine/Source/BlizzardCore/SFile2-Core.h"

// Columns are only appended to these tables, so a file with at least `minFields` columns is accepted.
bool CustomTable::LoadTable(const char* path, uint32_t minFields, Table& out, char* status, size_t statusSize) {
    std::vector<uint8_t> bytes;
    void* data = nullptr;
    uint32_t size = 0;
    if (SFile::LoadFile(path, &data, &size, 0, nullptr) && data) {
        bytes.assign(static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
        SFile::Unload(data);
    } else if (FILE* file = std::fopen(path, "rb")) {
        // shortcut: loose files are read relative to the client folder when the archives do not have them
        std::fseek(file, 0, SEEK_END);
        long length = std::ftell(file);
        std::fseek(file, 0, SEEK_SET);
        bytes.resize(length > 0 ? static_cast<size_t>(length) : 0);
        bytes.resize(std::fread(bytes.data(), 1, bytes.size(), file));
        std::fclose(file);
    } else {
        std::snprintf(status, statusSize, "%s not found", path);
        return false;
    }

    uint32_t header[5] = {};
    if (bytes.size() < sizeof header) {
        std::snprintf(status, statusSize, "%s is too short", path);
        return false;
    }
    std::memcpy(header, bytes.data(), sizeof header);
    const uint32_t records = header[1], fields = header[2], recordSize = header[3], stringSize = header[4];
    if (header[0] != 0x43424457u || fields < minFields || recordSize != fields * 4 ||
        static_cast<uint64_t>(records) * recordSize > bytes.size() - sizeof header) {
        std::snprintf(status, statusSize, "%s: not a WDBC table with at least %u columns", path, minFields);
        return false;
    }
    out.fields = fields;
    out.cells.resize(static_cast<size_t>(records) * fields);
    std::memcpy(out.cells.data(), bytes.data() + sizeof header, out.cells.size() * 4);
    const size_t stringStart = sizeof header + out.cells.size() * 4;
    const size_t stringEnd = stringStart + std::min<size_t>(stringSize, bytes.size() - stringStart);
    out.strings.assign(bytes.begin() + stringStart, bytes.begin() + stringEnd);
    out.strings.push_back('\0');  // the last string ends even in a cut-off file
    std::snprintf(status, statusSize, "%u row(s)", records);
    return true;
}

