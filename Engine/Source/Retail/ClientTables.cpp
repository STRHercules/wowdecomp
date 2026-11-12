#include "Engine/Source/Retail/ClientTables.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace Retail {
namespace {
uint32_t Word(const std::vector<uint8_t>& data, size_t p) {
    return uint32_t(data[p]) | uint32_t(data[p + 1]) << 8 | uint32_t(data[p + 2]) << 16 | uint32_t(data[p + 3]) << 24;
}
void Put(std::vector<uint8_t>& data, size_t p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[p + i] = uint8_t(value >> (8 * i));
}
bool Reject(std::string& error, const char* message) { error = message; return false; }
bool Validate(const std::vector<uint8_t>& data, uint32_t fields, const std::vector<unsigned>& strings,
    std::set<uint32_t>& ids, bool additive, std::string& error) {
    if (data.size() < 21 || std::memcmp(data.data(), "WDBC", 4) || Word(data, 8) != fields || Word(data, 12) != fields * 4)
        return Reject(error, "Invalid supplemental client table layout");
    const uint32_t count = Word(data, 4), size = Word(data, 16);
    const uint64_t start = 20ull + uint64_t(count) * fields * 4;
    if (count > 1000000 || !size || start + size != data.size() || data[size_t(start)] || data.back())
        return Reject(error, "Invalid supplemental client table bounds or string pool");
    for (uint32_t i = 0; i < count; ++i) {
        const size_t row = 20 + size_t(i) * fields * 4;
        const uint32_t id = Word(data, row);
        if (additive && (id > 5000000 || !id || ids.count(id))) {
            error = std::string(additive ? "New" : "Existing") + " client table ID " + std::to_string(id) +
                (id > 5000000 ? " exceeds index budget" : " conflicts with an existing record");
            return false;
        }
        ids.insert(id); // Preserve existing zero IDs/duplicate rows exactly as loaded by stock code.
        // Existing target rows are opaque metadata owned by that client. Some
        // Esteria providers contain unused out-of-pool texture offsets; copying
        // their rows/pool verbatim must not turn this into an unrelated repair.
        // Every NEW string reference remains strictly bounded and terminated.
        if (!additive) continue;
        for (auto field : strings) {
            const uint32_t offset = Word(data, row + field * 4);
            if (offset >= size || !std::memchr(data.data() + size_t(start) + offset, 0, size - offset)) {
                error = std::string(additive ? "New" : "Existing") + " table row " + std::to_string(id) +
                    ", field " + std::to_string(field) + ": invalid string offset " + std::to_string(offset);
                return false;
            }
        }
    }
    return true;
}
}

bool MergeClientTable(const std::string& table, const std::vector<uint8_t>& base,
    const std::vector<uint8_t>& additions, std::vector<uint8_t>& merged, std::string& error) {
    error.clear();
    merged.clear();
    // Field count and string columns per table; localized names are 16 string columns starting at the given one.
    struct Layout { const char* name; uint32_t fields; std::vector<unsigned> strings, localized; };
    static const Layout layouts[] = {
        {"item.dbc", 8, {}, {}}, {"itemdisplayinfo.dbc", 25, {1, 2, 3, 4, 5, 6, 15, 16, 17, 18, 19, 20, 21, 22}, {}},
        {"creaturemodeldata.dbc", 28, {2}, {}}, {"creaturedisplayinfo.dbc", 16, {6, 7, 8, 9}, {}},
        {"spell.dbc", 234, {}, {136, 153, 170, 187}}, {"spellicon.dbc", 2, {1}, {}}, {"skilllineability.dbc", 14, {}, {}},
        {"spellvisual.dbc", 32, {}, {}}, {"spellvisualkit.dbc", 38, {}, {}}, {"spellvisualeffectname.dbc", 7, {1, 2}, {}},
        {"spellvisualkitmodelattach.dbc", 10, {}, {}},
        // Item and spell lookups that content ported from other 3.3.5 realms (Manastorm) brings along.
        {"spellcategory.dbc", 2, {}, {}}, {"spellduration.dbc", 4, {}, {}}, {"spellradius.dbc", 4, {}, {}}, {"spellrange.dbc", 40, {}, {6, 23}},
        {"itemset.dbc", 53, {}, {1}}, {"itemlimitcategory.dbc", 20, {}, {1}}, {"itemextendedcost.dbc", 16, {}, {}},
        {"spellitemenchantment.dbc", 38, {}, {14}}, {"scalingstatdistribution.dbc", 22, {}, {}},
        {"gameobjectdisplayinfo.dbc", 19, {1}, {}},
    };
    const Layout* layout = nullptr;
    for (const auto& candidate : layouts) if (table == candidate.name) layout = &candidate;
    if (!layout) return Reject(error, "Unsupported supplemental client table");
    const uint32_t fields = layout->fields;
    std::vector<unsigned> strings = layout->strings;
    for (unsigned start : layout->localized)
        for (unsigned i = start; i < start + 16; ++i) strings.push_back(i);
    std::set<uint32_t> ids;
    if (!Validate(base, fields, strings, ids, false, error) || !Validate(additions, fields, strings, ids, true, error)) return false;
    if (base.size() + additions.size() > 128 * 1024 * 1024)
        return Reject(error, "Supplemental client table exceeds size limit");
    const size_t baseStrings = 20 + size_t(Word(base, 4)) * fields * 4;
    const size_t addedStrings = 20 + size_t(Word(additions, 4)) * fields * 4;
    const uint32_t oldStringSize = Word(base, 16);
    merged.assign(base.begin(), base.begin() + baseStrings);
    merged.insert(merged.end(), additions.begin() + 20, additions.begin() + addedStrings);
    for (uint32_t i = 0; i < Word(additions, 4); ++i) {
        for (auto field : strings) {
            const size_t p = baseStrings + size_t(i) * fields * 4 + field * 4;
            if (Word(merged, p)) Put(merged, p, Word(merged, p) + oldStringSize);
        }
    }
    merged.insert(merged.end(), base.begin() + baseStrings, base.end());
    merged.insert(merged.end(), additions.begin() + addedStrings, additions.end());
    Put(merged, 4, Word(base, 4) + Word(additions, 4));
    Put(merged, 16, oldStringSize + Word(additions, 16));
    return true;
}
}
