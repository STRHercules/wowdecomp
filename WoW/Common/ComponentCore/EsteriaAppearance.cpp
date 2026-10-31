#include <functional>
#include <memory>
#include <unordered_set>
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "Engine/Source/Retail/RetailLookWire.h"
#include "Engine/Source/Retail/BodyShape.h"
#include "WoW/Common/ComponentCore/EsteriaAppearance.h"
#include "Engine/Source/Retail/EyeEffects.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Common/ComponentCore/ComponentUtils.h"
#include "WoW/Common/ComponentCore/CreatureAppearance.h"
#include "WoW/Common/ComponentCore/EarthenAppearance.h"
#include "WoW/Common/ComponentCore/RetailRace.h"
#include "WoW/Common/ComponentCore/HaranirAppearance.h"
#include "WoW/Common/ComponentCore/HighmountainAppearance.h"
#include "WoW/Common/ComponentCore/VulperaAppearance.h"
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/BLPFile/Types.h"
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Retail/LegacyAppearance.h"
#include "Engine/Source/Gx/CGxTexFlags.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Common/TextureCache.h"
#include "WoW/Source/Glue/CGlueLoading.h"
#include "WoW/Source/Glue/CharacterCreation.h"
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include <random>
#include <cmath>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

struct Selection {
    uint32_t gender;
    uint32_t kind; // 0: geoset group, 1: replaceable texture, 2: extra geoset, 3: composed layer, 4: helmet hide, 5: equipment texture
    uint32_t target;
    uint32_t value;
    uint32_t choices[3]; // {option | value << 16}, 0xffffffff = unconditional
    char path[128];
};

// Catalogs are read from the MPQ stack (Esteria\<name>); a copy beside the executable is the fallback.
struct Catalog {
    const std::vector<uint8_t>* data = nullptr;
    size_t pos = 0;

    explicit operator bool() const { return data != nullptr; }

    size_t Read(void* out, size_t size, size_t count) {
        if (!size || count > (data->size() - pos) / size) {
            return 0;
        }
        memcpy(out, data->data() + pos, size * count);
        pos += size * count;
        return count;
    }

    bool Seek(size_t offset) {
        if (offset > data->size()) {
            return false;
        }
        pos = offset;
        return true;
    }
};

void Trace(const char* format, ...);

Catalog CatalogFile(const wchar_t* name) {
    static std::unordered_map<std::wstring, std::vector<uint8_t>> cache;
    auto found = cache.find(name);
    if (found == cache.end()) {
        std::vector<uint8_t> bytes;
        char path[MAX_PATH] = {};
        sprintf_s(path, "Esteria\\%ls", name);
        void* file = nullptr;
        uint32_t size = 0;
        const bool packed = SFile::LoadFile(path, &file, &size, 0, nullptr) && file;
        Trace("catalog %ls from %s", name, packed ? "mpq" : "disk");
        if (packed) {
            bytes.assign(static_cast<const uint8_t*>(file), static_cast<const uint8_t*>(file) + size);
            SFile::Unload(file);
        } else {
            wchar_t disk[MAX_PATH] = {};
            wchar_t* slash = GetModuleFileNameW(nullptr, disk, MAX_PATH) ? wcsrchr(disk, L'\\') : nullptr;
            FILE* input = nullptr;
            if (slash && slash - disk + 1 + wcslen(name) < MAX_PATH) {
                wcscpy(slash + 1, name);
                if (!_wfopen_s(&input, disk, L"rb") && input) {
                    fseek(input, 0, SEEK_END);
                    bytes.resize(ftell(input));
                    fseek(input, 0, SEEK_SET);
                    if (fread(bytes.data(), 1, bytes.size(), input) != bytes.size()) {
                        bytes.clear();
                    }
                    fclose(input);
                }
            }
        }
        // An empty entry caches a miss so absent catalogs are not re-probed.
        found = cache.emplace(name, std::move(bytes)).first;
    }
    Catalog catalog;
    if (!found->second.empty()) {
        catalog.data = &found->second;
    }
    return catalog;
}

void Trace(const char* format, ...) {
    static int lines = 0;
    char path[MAX_PATH] = {};
    if (lines >= 400 || !GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return;
    }
    char* slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    strcpy(slash + 1, "EsteriaDecomp.log");
    FILE* file = nullptr;
    if (fopen_s(&file, path, "a") || !file) {
        return;
    }
    va_list args;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputc('\n', file);
    fclose(file);
    lines++;
}

void KTrace(const char* tag, const char* format, ...) {
    // Kyrian diagnostics: logs only when a tag's message changes, beside the executable.
    static std::unordered_map<std::string, std::string> last;
    char message[512] = {};
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    auto& previous = last[tag];
    char path[MAX_PATH] = {};
    if (previous == message || !GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return;
    }
    previous = message;
    char* slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    strcpy(slash + 1, "EsteriaKyrian.log");
    FILE* file = nullptr;
    if (!fopen_s(&file, path, "a") && file) {
        fprintf(file, "[%s] %s\n", tag, message);
        fclose(file);
    }
}

void KDump(const char* name, const std::vector<unsigned char>& pixels, unsigned width, unsigned height) {
    // Kyrian diagnostics: raw 32-bit TGA of a composed atlas (channel order as held in memory), beside the exe.
    char path[MAX_PATH] = {};
    if (pixels.size() < size_t(width) * height * 4 || !GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return;
    }
    char* slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    sprintf_s(slash + 1, MAX_PATH - (slash + 1 - path), "Logs\\kyrian-%s.tga", name);
    FILE* file = nullptr;
    if (fopen_s(&file, path, "wb") || !file) {
        return;
    }
    unsigned char header[18] = { 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, static_cast<unsigned char>(width & 255), static_cast<unsigned char>(width >> 8),
        static_cast<unsigned char>(height & 255), static_cast<unsigned char>(height >> 8), 32, 0x28 };
    fwrite(header, 1, sizeof(header), file);
    fwrite(pixels.data(), 1, size_t(width) * height * 4, file);
    fclose(file);
}

void KVisible(CM2Model* model, unsigned sex) {
    // Kyrian diagnostics: which skin sections (index:geoset id) are drawn after the last bind.
    if (!model || !model->m_shared || !model->m_shared->m_skinData || !model->m_skinSections) {
        return;
    }
    M2SkinProfile* skin = model->m_shared->m_skinData;
    char text[480] = {};
    size_t used = 0;
    for (uint32_t i = 0; i < skin->m_skinSections.m_count && used + 16 < sizeof(text); i++) {
        if (model->m_skinSections[i]) {
            used += sprintf_s(text + used, sizeof(text) - used, "%u:%u ", i, skin->m_skinSections[i].m_skinSectionId & 0xFFFF);
        }
    }
    KTrace("vis", "sex=%u %s", sex, text);
}

void KOverrides(CM2Model* model) {
    // Kyrian diagnostics: Logs\\kyrian-geosets.txt holds lines such as "hide 20300" or "show 1302-1302" applied after every bind.
    char path[MAX_PATH] = {};
    if (!model || !GetModuleFileNameA(nullptr, path, MAX_PATH)) {
        return;
    }
    char* slash = strrchr(path, '\\');
    if (!slash) {
        return;
    }
    sprintf_s(slash + 1, MAX_PATH - (slash + 1 - path), "Logs\\kyrian-geosets.txt");
    FILE* file = nullptr;
    static unsigned calls = 0;
    if (fopen_s(&file, path, "r") || !file) {
        if (++calls <= 40) {
            KTrace("ovr", "call=%u no file at %s", calls, path);
        }
        return;
    }
    unsigned applied = 0;
    char verb[16] = {};
    unsigned first = 0;
    unsigned last = 0;
    char line[128] = {};
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "hidesec", 7) == 0 || strncmp(line, "showsec", 7) == 0) {
            // By skin-section index (the "vis" log's first number), for sections that share geoset id 0.
            unsigned index = 0;
            if (sscanf_s(line + 7, "%u", &index) == 1 && model->m_shared && model->m_shared->m_skinData
                && index < model->m_shared->m_skinData->m_skinSections.m_count) {
                model->m_skinSections[index] = line[0] == 's' ? 1 : 0;
                model->UnoptimizeVisibleGeometry();
                applied++;
            }
            continue;
        }
        int count = sscanf_s(line, "%15s %u-%u", verb, unsigned(sizeof(verb)), &first, &last);
        if (count == 2) {
            last = first;
        }
        if (count >= 2) {
            model->SetGeometryVisible(first, last, strcmp(verb, "show") == 0 ? 1 : 0);
            applied++;
        }
    }
    fclose(file);
    if (++calls <= 40) {
        KTrace("ovr", "call=%u applied=%u", calls, applied);
    }
}

std::vector<Selection> ReadSelections(const wchar_t* name) {
    std::vector<Selection> result;
    Catalog file = CatalogFile(name);
    if (!file) {
        return result;
    }

    uint32_t header[3] = {};
    if (file.Read(header, sizeof(header), 1) == 1 && header[0] == 0x314D4845 && header[1] == 1 && header[2] <= 16384) {
        for (uint32_t i = 0; i < header[2]; i++) {
            Selection record = {};
            if (file.Read(&record, sizeof(record), 1) != 1) {
                break;
            }

            bool valid = record.gender < 2 && record.kind <= 5 && memchr(record.path, 0, sizeof(record.path));
            switch (record.kind) {
                case 0: valid = valid && record.target < 52 && record.value >= record.target * 100 && record.value < record.target * 100 + 100; break;
                case 1: valid = valid && record.target < 16 && record.path[0]; break;
                case 2: valid = valid && record.target < 5200 && record.value >= 10000 && record.value < 30000; break;
                case 3: valid = valid && record.target < 9 && record.value >= 16; break;
                case 4: valid = valid && record.target && record.value < 52; break;
                case 5: valid = valid && record.target < 16 && record.value && record.path[0]; break;
            }

            for (uint32_t selector : record.choices) {
                valid = valid && (selector == 0xFFFFFFFF || ((selector & 0xFFFF) < 32 && (selector >> 16) < 256));
            }

            if (valid) {
                result.push_back(record);
            }
        }
    }

    return result;
}

const std::vector<Selection>& RaceSelections(uint32_t race) {
    static const std::vector<Selection> naga = ReadSelections(L"EsteriaNaga.bin");
    static const std::vector<Selection> tuskarr = ReadSelections(L"EsteriaTuskarr.bin");
    static const std::vector<Selection> vrykul = ReadSelections(L"EsteriaVrykul.bin");
    static const std::vector<Selection> human = ReadSelections(L"EsteriaThinHuman.bin");
    static const std::vector<Selection> sethrak = ReadSelections(L"EsteriaSethrak.bin");
    static const std::vector<Selection> gnoll = ReadSelections(L"EsteriaGnoll.bin");
    static const std::vector<Selection> saberon = ReadSelections(L"EsteriaSaberon.bin");
    static const std::vector<Selection> tarasek = ReadSelections(L"EsteriaTarasek.bin");
    static const std::vector<Selection> drakonid = ReadSelections(L"EsteriaDrakonid.bin");
    static const std::vector<Selection> darkfallen = ReadSelections(L"EsteriaDarkfallen.bin");
    static const std::vector<Selection> vulpera = ReadSelections(L"EsteriaVulpera.bin");
    static const std::vector<Selection> highmountain = ReadSelections(L"EsteriaHighmountain.bin");
    static const std::vector<Selection> earthen = ReadSelections(L"EsteriaEarthen.bin");
    static const std::vector<Selection> haranir = ReadSelections(L"EsteriaHaranir.bin");
    static const auto kyrian = ReadSelections(L"EsteriaKyrian.bin");
    static const auto venthyr = ReadSelections(L"EsteriaVenthyr.bin");
    static const std::vector<Selection> none;

    switch (race) {
        case 75: return kyrian;
        case 76: return venthyr;
        case 20: return vulpera;
        case 46: return highmountain;
        case 48:
        case 49: return earthen;
        case 50:
        case 51: return haranir;
        case 54: return naga;
        case 55: return tuskarr;
        case 56:
        case 57: return vrykul;
        case 58:
        case 59: return human;
        case 62: return sethrak;
        case 63: return gnoll;
        case 64: return saberon;
        case 43:
        case 44: return darkfallen;
        case 66: return tarasek;
        case 69:
        case 70: return drakonid;
        default: return none;
    }
}

// Body regions painted by the equipped items' texture components (bit n = Wrath region n, 0..7).
uint32_t DrakonidTextureCoverage(CCharacterComponent* character) {
    static const std::unordered_map<uint32_t, uint32_t> masks = [] {
        std::unordered_map<uint32_t, uint32_t> result;
        Catalog file = CatalogFile(L"EsteriaDrakonidCoverage.bin");
        if (!file) {
            return result;
        }

        uint32_t header[3] = {};
        if (file.Read(header, sizeof(header), 1) == 1 && header[0] == 0x56434253 && header[1] == 1 && header[2] <= 65536) {
            for (uint32_t i = 0; i < header[2]; i++) {
                uint32_t row[2] = {};
                if (file.Read(row, sizeof(row), 1) != 1 || !row[0] || !row[1] || row[1] >= 256) {
                    result.clear();
                    break;
                }
                result.emplace(row[0], row[1]);
            }
        }

        return result;
    }();

    uint32_t result = 0;
    for (uint32_t slot = 2; slot <= 9; slot++) {
        auto found = masks.find(character->m_itemDisplayID[slot]);
        if (found != masks.end()) {
            result |= found->second;
        }
    }

    return result;
}

// Preferences in the catalog's option-field order: skin, face, hair style, hair colour, facial hair.
uint32_t* PreferenceField(CharacterPreferences& prefs, uint32_t index) {
    uint32_t* fields[] = { &prefs.m_skinID, &prefs.m_faceId, &prefs.m_hairStyleId, &prefs.m_hairColorId, &prefs.m_facialHairId };
    return fields[index];
}

using Fields13 = std::array<uint8_t, 13>;

// Skin, face, hair style, hair colour, facial hair; the extra bytes (13 - 5) are only sent by the server for some races.
Fields13 CurrentFields(CharacterPreferences& prefs) {
    Fields13 fields = {};
    for (uint32_t i = 0; i < 5; i++) {
        fields[i] = static_cast<uint8_t>(*PreferenceField(prefs, i));
    }

    return prefs.m_raceID == 75 ? CreatureAppearance::DecodeKyrian(prefs.m_sexID, fields) : fields;
}

bool ValidFields(uint32_t race, uint32_t gender, const Fields13& fields) {
    auto zeroFrom = [&](size_t first) {
        return std::all_of(fields.begin() + first, fields.end(), [](uint8_t value) { return value == 0; });
    };

    if (race == 47) return Retail::LegacyAppearance::ValidateFields(race, gender, fields);

    if (CreatureAppearance::Uses(race)) {
        std::array<uint8_t, 5> appearance = {};
        std::copy_n(fields.begin(), appearance.size(), appearance.begin());
        if (race == 75) appearance = CreatureAppearance::EncodeKyrian(gender, appearance);
        return zeroFrom(5) && CreatureAppearance::Validate(race, gender, appearance);
    }

    if (race == 20) {
        std::array<uint8_t, 5> vulpera = {};
        std::copy_n(fields.begin(), vulpera.size(), vulpera.begin());
        return zeroFrom(5) && VulperaAppearance::Validate(gender, vulpera);
    }

    if (race == 50 || race == 51) {
        return HaranirAppearance::Validate(gender, fields);
    }

    std::array<uint8_t, 6> legacy = {};
    std::copy_n(fields.begin(), legacy.size(), legacy.begin());
    return zeroFrom(6) && (race == 46 ? HighmountainAppearance::Validate(gender, legacy) : EarthenAppearance::Validate(gender, legacy));
}

// Body regions covered by equipped items for Saberon (belts have their own raised waist surface).
uint32_t SaberonTextureCoverage(CCharacterComponent* character) {
    static const std::unordered_map<uint32_t, uint32_t> masks = [] {
        std::unordered_map<uint32_t, uint32_t> result;
        Catalog file = CatalogFile(L"EsteriaSaberonCoverage.bin");
        if (!file) {
            return result;
        }

        uint32_t header[3] = {};
        if (file.Read(header, sizeof(header), 1) == 1 && header[0] == 0x56434253 && header[1] == 1 && header[2] <= 65536) {
            for (uint32_t i = 0; i < header[2]; i++) {
                uint32_t row[2] = {};
                if (file.Read(row, sizeof(row), 1) != 1 || !row[0] || !row[1] || row[1] >= 128) {
                    result.clear();
                    break;
                }
                result.emplace(row[0], row[1]);
            }
        }

        return result;
    }();

    uint32_t result = 0;
    for (uint32_t slot = 2; slot <= 9; slot++) {
        if (slot == 4) {
            continue;
        }

        auto found = masks.find(character->m_itemDisplayID[slot]);
        if (found != masks.end()) {
            result |= found->second;
        }
    }

    return result;
}

Fields13 FieldsOf(CCharacterComponent* character);

template <class Options>
void ApplyCatalog(CCharacterComponent* character, const Options& options, const std::vector<Selection>& records) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    uint32_t race = prefs.m_raceID;
    uint32_t gender = prefs.m_sexID;
    CM2Model* model = character->m_data.m_model;

    Fields13 fields = FieldsOf(character);
    if (race == 75) KTrace("catalog", "sex=%u logical=%u,%u,%u,%u,%u wire=%u,%u,%u,%u,%u valid=%d records=%zu gear=%u,%u,%u,%u,%u,%u,%u,%u,%u,%u", gender, fields[0], fields[1], fields[2], fields[3], fields[4], prefs.m_skinID, prefs.m_faceId, prefs.m_hairStyleId, prefs.m_hairColorId, prefs.m_facialHairId, ValidFields(race, gender, fields) ? 1 : 0, records.size(), character->m_itemDisplayID[0], character->m_itemDisplayID[1], character->m_itemDisplayID[2], character->m_itemDisplayID[3], character->m_itemDisplayID[4], character->m_itemDisplayID[5], character->m_itemDisplayID[6], character->m_itemDisplayID[7], character->m_itemDisplayID[8], character->m_itemDisplayID[9]);

    if (!ValidFields(race, gender, fields)) {
        fields = {};
        for (uint32_t i = 0; i < 5; i++) {
            *PreferenceField(prefs, i) = 0;
        }
    }


    uint64_t helmetGroups = 0;
    if (race == 20 || race == 54 || (race == 66 && gender == 0) || race == 69 || race == 70) {
        for (const Selection& record : records) {
            if (record.kind == 4 && record.gender == gender && record.value < 52 && record.target == character->m_itemDisplayID[0]) {
                helmetGroups |= uint64_t{1} << record.value;
            }
        }
    }

    uint64_t geometryGroups = 0;
    uint32_t selectedGeosets[52] = {};
    uint32_t materials = 0;

    model->SetGeometryVisible(10000, 29999, 0);

    // Native clothing yields its generated shells (21xxx) once any armour slot is filled.
    bool nativeClothingCovered = false;
    if (race == 62 || race == 64) {
        for (uint32_t slot = 2; slot <= 9; slot++) {
            nativeClothingCovered = nativeClothingCovered || character->m_itemDisplayID[slot] != 0;
        }
    }

    for (const Selection& record : records) {
        // Kind 3 (composed layers) is handled by ComposeGroup, kind 4 (helmet hides) by helmetGroups above; kind 5 is the
        // Saberon belt/trousers texture matching the equipped item display.
        bool equipmentTexture = record.kind == 5 && race == 64
            && ((record.target == 9 && record.value == character->m_itemDisplayID[4]) || (record.target == 10 && record.value == character->m_itemDisplayID[5]));
        if (record.gender != gender || (record.kind >= 3 && !equipmentTexture)) {
            continue;
        }

        bool selected = true;
        for (uint32_t selector : record.choices) {
            if (selector == 0xFFFFFFFF) {
                continue;
            }

            uint32_t index = selector & 0xFFFF;
            if (index >= options.size()) {
                selected = false;
                break;
            }

            const auto& option = options[index];
            selected = selected && (fields[option.field] / option.factor) % option.count == (selector >> 16);
        }

        if (!selected) {
            continue;
        }


        if (record.kind == 2) {
            if (nativeClothingCovered && record.value >= 21000 && record.value < 22000) {
                continue;
            }

            if (race == 64 && record.value >= 20001 && record.value <= 20009) {
                uint32_t coverage = SaberonTextureCoverage(character);
                uint32_t beltDisplay = character->m_itemDisplayID[4];
                bool belt = std::any_of(records.begin(), records.end(), [beltDisplay](const Selection& item) {
                    return item.kind == 5 && item.target == 9 && item.value == beltDisplay;
                });
                uint32_t pantsDisplay = character->m_itemDisplayID[5];
                bool separatePants = belt && std::any_of(records.begin(), records.end(), [pantsDisplay](const Selection& item) {
                    return item.kind == 5 && item.target == 10 && item.value == pantsDisplay;
                });
                uint32_t region = record.value == 20009 ? 5 : record.value - 20001;
                bool covered = record.value == 20008 ? belt : (coverage & (1u << region)) != 0;
                if (record.value == 20006 && separatePants) {
                    covered = false;
                }

                if (record.value == 20009 && !separatePants) {
                    covered = false;
                }

                if (!covered) {
                    continue;
                }
            }

            // 2000x body shells, 2001x / 2002x dorsal shells of tail 0 / 1
            if ((race == 69 || race == 70) && record.value >= 20001 && record.value <= 20028 && (record.value - 20001) % 10 < 8
                && !(DrakonidTextureCoverage(character) & (1u << ((record.value - 20001) % 10)))) {
                continue;
            }

            uint32_t group = record.target / 100;
            uint32_t source = group < 19 ? character->m_data.m_geosets[group] : selectedGeosets[group];
            if (record.target == 0 || source == record.target) {
                model->SetGeometryVisible(record.value, record.value, 1);
            }
        } else if (record.kind == 0) {
            if (geometryGroups & (uint64_t{1} << record.target)) {
                continue;
            }

            geometryGroups |= uint64_t{1} << record.target;
            uint32_t value = record.value;
            if (helmetGroups & (uint64_t{1} << record.target)) {
                value = record.target * 100;
            }

            // The cloak geoset only shows when a cloak is equipped.
            if (record.target == 15 && !character->m_itemDisplayID[ITEMSLOT_CAPE]) {
                value = 1500;
            }

            // Native equipment slot 7 (feet) maps to component slot 6: boots replace the authored foot geoset.
            if (record.target == 20 && (race == 43 || race == 44 || race == 48 || race == 49 || race == 50 || race == 51 || race == 56 || race == 57)) {
                value = character->m_itemDisplayID[6] ? 2002 : (race == 50 || race == 51 ? record.value : 2001);
            }

            selectedGeosets[record.target] = value;
            if (race == 75) KTrace(("geo" + std::to_string(record.target)).c_str(), "group=%u value=%u", record.target, value);
            if (record.target < 19) {
                character->m_data.m_geosets[record.target] = value;
            } else {
                model->SetGeometryVisible(record.target * 100, record.target * 100 + 99, 0);
                model->SetGeometryVisible(value, value, 1);
            }
        } else if (!(materials & (1u << record.target))) {
            CStatus status;
            CTexture* texture = CreateTexture(record.path, &status);
            if (texture) {
                model->ReplaceTexture(record.target, texture);
                HandleClose(reinterpret_cast<HOBJECT>(texture));
            }

            materials |= 1u << record.target;
        }
    }

    // Some source-hidden groups have no customization selector. Apply their hide after all selections.
    for (uint32_t group = 0; group < 52; group++) {
        if (helmetGroups & (uint64_t{1} << group)) {
            if (group < 19) {
                character->m_data.m_geosets[group] = group * 100;
            }

            model->SetGeometryVisible(group * 100, group * 100 + 99, 0);
        }
    }
}

const wchar_t* TextureBank(uint32_t race) {
    switch (race) {
        case 75: return L"EsteriaKyrianTextures.bin";
        case 20: return L"EsteriaVulperaTextures.bin";
        case 50:
        case 51: return L"EsteriaHaranirTextures.bin";
        case 54: return L"EsteriaNagaTextures.bin";
        case 55: return L"EsteriaTuskarrTextures.bin";
        case 56:
        case 57: return L"EsteriaVrykulTextures.bin";
        case 58:
        case 59: return L"EsteriaThinHumanTextures.bin";
        case 62: return L"EsteriaSethrakTextures.bin";
        case 63: return L"EsteriaGnollTextures.bin";
        case 64: return L"EsteriaSaberonTextures.bin";
        case 66: return L"EsteriaTarasekTextures.bin";
        case 69:
        case 70: return L"EsteriaDrakonidTextures.bin";
        default: return nullptr;
    }
}

const char* CacheFamily(uint32_t race) {
    switch (race) {
        case 20: return "Vulpera";
        case 50:
        case 51: return "Haranir";
        case 54: return "Naga";
        case 55: return "Tuskarr";
        case 56:
        case 57: return "Vrykul";
        case 58:
        case 59: return "ThinHuman";
        case 62: return "Sethrak";
        case 63: return "Gnoll";
        case 64: return "Saberon";
        case 66: return "Tarasek";
        case 43:
        case 44: return "Darkfallen";
        default: return "Drakonid";
    }
}

// Builds a BLP2 with a full mip chain: palettized (quantized through WIC) or raw BGRA.
bool BuildBlp(std::vector<uint8_t> pixels, uint32_t width, uint32_t height, bool indexed, bool preserveAlpha, std::vector<uint8_t>& data) {
    using Microsoft::WRL::ComPtr;

    struct Apartment {
        HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() {
            if (SUCCEEDED(result)) {
                CoUninitialize();
            }
        }
    } apartment;

    if (FAILED(apartment.result) && apartment.result != RPC_E_CHANGED_MODE) {
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICPalette> palette;
    if (indexed && (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) || FAILED(factory->CreatePalette(&palette)))) {
        return false;
    }

    data.assign(1172, 0);
    memcpy(data.data(), "BLP2", 4);
    BLPHeader* header = reinterpret_cast<BLPHeader*>(data.data());
    header->m_formatVersion = 1;
    header->m_colorEncoding = indexed ? 1 : 3;
    header->m_alphaSize = indexed && !preserveAlpha ? 0 : 8;
    header->m_preferredFormat = 8;
    header->m_hasMips = 1;
    header->m_width = width;
    header->m_height = height;

    for (uint32_t mip = 0; mip < 16; mip++) {
        std::vector<uint8_t> level;
        if (indexed) {
            ComPtr<IWICBitmap> bitmap;
            ComPtr<IWICFormatConverter> converter;
            if (FAILED(factory->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppRGBA, width * 4, static_cast<UINT>(pixels.size()), pixels.data(), &bitmap))) {
                return false;
            }

            if (!mip) {
                if (FAILED(palette->InitializeFromBitmap(bitmap.Get(), 256, FALSE))) {
                    return false;
                }

                WICColor colors[256] = {};
                UINT count = 0;
                if (FAILED(palette->GetColors(256, colors, &count))) {
                    return false;
                }

                for (UINT i = 0; i < count; i++) {
                    uint32_t color = colors[i] & 0xFFFFFF;
                    memcpy(data.data() + 148 + i * 4, &color, 4);
                }
            }

            level.resize(width * height);
            if (FAILED(factory->CreateFormatConverter(&converter))
                || FAILED(converter->Initialize(bitmap.Get(), GUID_WICPixelFormat8bppIndexed, WICBitmapDitherTypeNone, palette.Get(), 0, WICBitmapPaletteTypeCustom))
                || FAILED(converter->CopyPixels(nullptr, width, static_cast<UINT>(level.size()), level.data()))) {
                return false;
            }

            if (preserveAlpha) {
                for (uint32_t i = 0; i < width * height; i++) {
                    level.push_back(pixels[i * 4 + 3]);
                }
            }
        } else {
            level = pixels;
            for (size_t i = 0; i < level.size(); i += 4) {
                std::swap(level[i], level[i + 2]);
            }
        }

        header = reinterpret_cast<BLPHeader*>(data.data());
        header->m_mipOffsets[mip] = static_cast<uint32_t>(data.size());
        header->m_mipSizes[mip] = static_cast<uint32_t>(level.size());
        data.insert(data.end(), level.begin(), level.end());
        if (width == 1 && height == 1) {
            break;
        }

        uint32_t nextWidth = std::max(1u, width / 2);
        uint32_t nextHeight = std::max(1u, height / 2);
        std::vector<uint8_t> smaller(nextWidth * nextHeight * 4);
        for (uint32_t y = 0; y < nextHeight; y++) {
            for (uint32_t x = 0; x < nextWidth; x++) {
                for (uint32_t channel = 0; channel < 4; channel++) {
                    uint32_t sum = 0;
                    for (uint32_t dy = 0; dy < 2; dy++) {
                        for (uint32_t dx = 0; dx < 2; dx++) {
                            sum += pixels[(std::min(y * 2 + dy, height - 1) * width + std::min(x * 2 + dx, width - 1)) * 4 + channel];
                        }
                    }
                    smaller[(y * nextWidth + x) * 4 + channel] = static_cast<uint8_t>(sum / 4);
                }
            }
        }

        pixels = std::move(smaller);
        width = nextWidth;
        height = nextHeight;
    }

    return true;
}

// Returns the cache texture for `name`, filling it from `data` if no data has been attached yet.
void* CacheTexture(const std::string& name, const std::vector<uint8_t>& data) {
    CACHEENTRY* entry = static_cast<CACHEENTRY*>(TextureCacheCreateTexture(name.c_str()));
    if (!entry || entry->m_data || data.size() > 0xFFFFF) {
        return entry;
    }

    entry->m_data = static_cast<BLPHeader*>(SMemAlloc(data.size(), __FILE__, __LINE__, 0));
    memcpy(entry->m_data, data.data(), data.size());
    entry->m_size = static_cast<uint32_t>(data.size());
    entry->m_info.m_alphaSize = entry->m_data->m_alphaSize;
    entry->m_info.m_opaque = entry->m_data->m_alphaSize == 0;
    entry->m_info.m_width = entry->m_data->m_width;
    entry->m_info.m_height = entry->m_data->m_height;
    entry->m_info.m_mipCount = TextureCalcMipCount(entry->m_info.m_width, entry->m_info.m_height);
    return entry;
}

// Returns an owned reference to `name` if its data is already cached, else null.
void* CachedTexture(const std::string& name) {
    CACHEENTRY* entry = static_cast<CACHEENTRY*>(TextureCacheCreateTexture(name.c_str()));
    if (entry && entry->m_data) {
        return entry;
    }

    TextureCacheDestroyTexture(entry);
    return nullptr;
}

// Composes the catalog's kind-3 layers for one texture group (0 = skin, 1 = face) into cached BLP layers.
// Returns false if the race has no layers for the group. `upper` is only filled for the face group.
template <class Options>
bool ComposeGroup(CCharacterComponent* character, const Options& options, uint32_t group, void** lower, void** upper) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    uint32_t race = prefs.m_raceID;
    uint32_t gender = prefs.m_sexID;
    const wchar_t* bankName = TextureBank(race);
    if (!bankName) {
        return false;
    }

    Fields13 fields = FieldsOf(character);
    if (!ValidFields(race, gender, fields)) {
        fields = {};
    }

    std::vector<const Selection*> layers;
    uint64_t seen = 0;
    for (const Selection& record : RaceSelections(race)) {
        if (record.kind != 3 || record.gender != gender || record.target != group) {
            continue;
        }

        bool selected = true;
        for (uint32_t selector : record.choices) {
            if (selector == 0xFFFFFFFF) {
                continue;
            }

            uint32_t index = selector & 0xFFFF;
            if (index >= options.size()) {
                selected = false;
                break;
            }

            selected = selected && fields[options[index].field] / options[index].factor % options[index].count == (selector >> 16);
        }

        uint32_t target = 0;
        uint32_t blend = 0;
        if (!selected || sscanf_s(record.path, "%u:%u", &target, &blend) != 2 || target >= 64 || (seen & (uint64_t{1} << target))) {
            continue;
        }

        seen |= uint64_t{1} << target;
        layers.push_back(&record);
    }

    if (layers.empty()) {
        return false;
    }

    uint64_t hash = 14695981039346656037ull;
    for (const Selection* record : layers) {
        hash = (hash ^ record->value) * 1099511628211ull;
    }

    char key[160];
    sprintf_s(key, "Esteria\\%s\\%u-%u-%016llx", CacheFamily(race), gender, group, static_cast<unsigned long long>(hash));
    std::string name = key;

    // Groups 2.. are plain material textures (not component layers), cached by content hash.
    static std::unordered_map<std::string, HOBJECT> materialTextures;
    if (group >= 2) {
        auto found = materialTextures.find(name);
        if (found != materialTextures.end()) {
            *lower = reinterpret_cast<void*>(found->second);
            return true;
        }
    } else if (group == 0) {
        *lower = CachedTexture(name);
        if (*lower) {
            return true;
        }
    } else {
        *lower = CachedTexture(name + "-lower");
        *upper = CachedTexture(name + "-upper");
        if (*lower && *upper) {
            return true;
        }

        TextureCacheDestroyTexture(*lower);
        TextureCacheDestroyTexture(*upper);
        *lower = *upper = nullptr;
    }

    Catalog input = CatalogFile(bankName);
    if (!input) {
        return false;
    }

    using DecompressFn = LONG(WINAPI*)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, PULONG);
    DecompressFn decompress = reinterpret_cast<DecompressFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlDecompressBuffer"));
    uint32_t header[4] = {};
    bool valid = decompress && input.Read(header, sizeof(header), 1) == 1 && header[0] == 0x31544845 && header[1] == 1 && header[2] <= 4096;
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;

    for (size_t i = 0; valid && i < layers.size(); i++) {
        uint32_t item[3] = {};
        if (!input.Seek(layers[i]->value) || input.Read(item, sizeof(item), 1) != 1 || !item[0] || !item[1] || item[0] > 512 || item[1] > 512 || item[2] > 2 * 1024 * 1024) {
            valid = false;
            break;
        }

        std::vector<uint8_t> packed(item[2]);
        std::vector<uint8_t> layer(item[0] * item[1] * 4);
        ULONG size = 0;
        if (input.Read(packed.data(), 1, packed.size()) != packed.size()
            || decompress(2, layer.data(), static_cast<ULONG>(layer.size()), packed.data(), item[2], &size) || size != layer.size()) {
            valid = false;
            break;
        }

        if (pixels.empty()) {
            pixels.resize(layer.size());
            width = item[0];
            height = item[1];
        }

        if (width != item[0] || height != item[1]) {
            valid = false;
            break;
        }

        for (size_t p = 0; p < layer.size(); p += 4) {
            uint32_t alpha = layer[p + 3];
            uint32_t priorAlpha = pixels[p + 3];
            uint32_t outputAlpha = alpha * 255 + priorAlpha * (255 - alpha);
            for (uint32_t channel = 0; channel < 3; channel++) {
                pixels[p + channel] = static_cast<uint8_t>(outputAlpha ? (layer[p + channel] * alpha * 255 + pixels[p + channel] * priorAlpha * (255 - alpha) + outputAlpha / 2) / outputAlpha : 0);
            }
            pixels[p + 3] = static_cast<uint8_t>((outputAlpha + 127) / 255);
        }
    }

    if (!valid || pixels.empty()) {
        return false;
    }

    std::vector<uint8_t> blp;
    if (group >= 2) {
        if (!BuildBlp(pixels, width, height, false, false, blp)) {
            return false;
        }

        CGxTexFlags flags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
        CTexture* texture = TextureCreateFromBlpData(name.c_str(), flags, blp.data());
        if (!texture) {
            return false;
        }

        materialTextures[name] = reinterpret_cast<HOBJECT>(texture);
        *lower = texture;
        return true;
    }

    if (group == 0) {
        bool alpha = race == 63 || race == 64;
        if (!BuildBlp(pixels, width, height, true, alpha, blp)) {
            return false;
        }

        *lower = CacheTexture(name, blp);
        return true;
    }

    // The face group is projected into Wrath's 256x192 destination, split into a 64-row upper and a lower part.
    if (width != 256 || height != 192) {
        return false;
    }

    std::vector<uint8_t> upperPixels(pixels.begin(), pixels.begin() + width * 64 * 4);
    std::vector<uint8_t> lowerPixels(pixels.begin() + width * 64 * 4, pixels.end());
    std::vector<uint8_t> upperBlp;
    if (!BuildBlp(upperPixels, width, 64, true, false, upperBlp) || !BuildBlp(lowerPixels, width, height - 64, true, false, blp)) {
        return false;
    }

    *lower = CacheTexture(name + "-lower", blp);
    *upper = CacheTexture(name + "-upper", upperBlp);
    return true;
}

// Reads one pixel (RGBA) from a decoded BLP2 item layer, honouring its palette and alpha.
bool ClothingPixel(const BLPHeader* blp, uint32_t size, uint32_t x, uint32_t y, uint8_t pixel[4]) {
    uint32_t width = blp->m_width;
    uint32_t height = blp->m_height;
    uint32_t offset = blp->m_mipOffsets[0];
    uint32_t length = blp->m_mipSizes[0];
    const uint8_t* data = reinterpret_cast<const uint8_t*>(blp);
    if (size < 1172 || blp->m_magic != 0x32504C42 || !width || !height || width > 2048 || height > 2048 || x >= width || y >= height || offset > size || length > size - offset) {
        return false;
    }

    uint32_t at = y * width + x;
    if (blp->m_colorEncoding == 3 && length == width * height * 4) {
        const uint8_t* color = data + offset + at * 4;
        pixel[0] = color[2];
        pixel[1] = color[1];
        pixel[2] = color[0];
        pixel[3] = color[3];
        return true;
    }

    uint32_t alpha = blp->m_alphaSize;
    if (blp->m_colorEncoding != 1 || (alpha != 0 && alpha != 1 && alpha != 4 && alpha != 8) || length != width * height + (width * height * alpha + 7) / 8) {
        return false;
    }

    const uint8_t* color = data + 148 + data[offset + at] * 4;
    uint32_t opacity = 255;
    if (alpha) {
        uint32_t mask = (1u << alpha) - 1;
        opacity = ((data[offset + width * height + at * alpha / 8] >> (at * alpha % 8)) & mask) * 255 / mask;
    }

    pixel[0] = color[2];
    pixel[1] = color[1];
    pixel[2] = color[0];
    pixel[3] = static_cast<uint8_t>(opacity);
    return true;
}

// Wrath's indexed body compositor writes alpha = 255 even for transparent input, so the equipped item layers are
// recomposed here with alpha into a render-only material (texture slot 14) used by the catalog's body shells.
void SetClothingMaterial(CCharacterComponent* character) {
    static const uint32_t counts[] = { 2, 7, 1, 5, 7, 3, 6, 1 };
    static const uint32_t rectangles[][4] = { { 0, 0, 256, 128 }, { 0, 128, 256, 128 }, { 0, 256, 256, 64 }, { 256, 0, 256, 128 },
        { 256, 128, 256, 64 }, { 256, 192, 256, 128 }, { 256, 320, 256, 128 }, { 256, 448, 256, 64 } };

    // Resolve every retained item display first; stock only resolves dirty sections.
    uint32_t dirty = character->m_dirtySections;
    character->m_dirtySections = dirty | 0xFF;
    character->ItemsLoaded(1);
    character->m_dirtySections = dirty;

    struct Layer {
        uint32_t region;
        uint32_t size;
        const BLPHeader* blp;
    };
    std::vector<Layer> layers;
    uint64_t hash = 14695981039346656037ull;
    bool robe = (character->m_flags & 0x20) != 0;

    for (uint32_t region = 0; region < 8; region++) {
        CharacterSection& section = character->m_section[region];
        uint32_t first = 0;
        uint32_t count = counts[region];
        if (region == 5 && robe) {
            first = 1;
            count = 2;
        }
        if (region == 6 && robe) {
            first = 1;
            count = 4;
        }

        for (uint32_t slot = first; slot < count; slot++) {
            CACHEENTRY* layer = static_cast<CACHEENTRY*>(section.m_layerTex[slot]);
            if (!(section.m_layerMask & (1u << slot)) || !layer) {
                continue;
            }

            if (!layer->m_data || !layer->m_info.m_width) {
                TCTEXTUREINFO info;
                TextureCacheGetInfo(layer, info, 1);
            }

            uint8_t pixel[4];
            if (!layer->m_data || !ClothingPixel(layer->m_data, layer->m_size, 0, 0, pixel)) {
                continue; // Failed input stays transparent rather than hiding the underlying fur.
            }

            hash = (hash ^ region) * 1099511628211ull;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(layer->m_data);
            for (uint32_t i = 0; i < layer->m_size; i++) {
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            }

            layers.push_back({ region, layer->m_size, layer->m_data });
        }
    }

    static std::unordered_map<CCharacterComponent*, std::pair<uint64_t, HOBJECT>> cache;
    auto& entry = cache[character];
    if (!entry.second || entry.first != hash) {
        std::vector<uint8_t> pixels(512 * 512 * 4);
        for (const Layer& layer : layers) {
            const uint32_t* rect = rectangles[layer.region];
            uint32_t width = layer.blp->m_width;
            uint32_t height = layer.blp->m_height;
            for (uint32_t y = 0; y < rect[3]; y++) {
                for (uint32_t x = 0; x < rect[2]; x++) {
                    uint8_t source[4];
                    if (!ClothingPixel(layer.blp, layer.size, x * width / rect[2], y * height / rect[3], source)) {
                        return;
                    }

                    uint8_t* target = pixels.data() + ((rect[1] + y) * 512 + rect[0] + x) * 4;
                    uint32_t alpha = source[3];
                    uint32_t prior = target[3];
                    uint32_t result = alpha * 255 + prior * (255 - alpha);
                    for (uint32_t channel = 0; channel < 3; channel++) {
                        target[channel] = static_cast<uint8_t>(result ? (source[channel] * alpha * 255 + target[channel] * prior * (255 - alpha) + result / 2) / result : 0);
                    }
                    target[3] = static_cast<uint8_t>((result + 127) / 255);
                }
            }
        }

        std::vector<uint8_t> blp;
        if (!BuildBlp(pixels, 512, 512, false, false, blp)) {
            return;
        }

        char name[96];
        sprintf_s(name, "Esteria\\clothing-%016llx", static_cast<unsigned long long>(hash));
        CGxTexFlags flags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
        CTexture* texture = TextureCreateFromBlpData(name, flags, blp.data());
        if (!texture) {
            return;
        }

        if (entry.second) {
            HandleClose(entry.second);
        }

        entry = { hash, reinterpret_cast<HOBJECT>(texture) };
    }

    character->m_data.m_model->ReplaceTexture(14, reinterpret_cast<CTexture*>(entry.second));
}

// Decodes level 0 of a DXT1/DXT3/DXT5 BLP2 into RGBA8; false for any other encoding.
bool DecodeDxtBlp(const BLPHeader* blp, uint32_t size, std::vector<uint8_t>& rgba) {
    if (size < 1172 || blp->m_magic != 0x32504C42 || blp->m_colorEncoding != 2 || !blp->m_width || !blp->m_height) {
        return false;
    }

    uint32_t width = blp->m_width;
    uint32_t height = blp->m_height;
    uint32_t format = blp->m_preferredFormat;
    bool explicitAlpha = format != 0;
    uint32_t blockBytes = explicitAlpha ? 16 : 8;
    uint32_t offset = blp->m_mipOffsets[0];
    uint32_t length = blp->m_mipSizes[0];
    uint32_t blocksX = (width + 3) / 4;
    uint32_t blocksY = (height + 3) / 4;
    if (offset > size || length > size - offset || length < blocksX * blocksY * blockBytes) {
        return false;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(blp) + offset;
    rgba.assign(static_cast<size_t>(width) * height * 4, 255);
    for (uint32_t by = 0; by < blocksY; by++) {
        for (uint32_t bx = 0; bx < blocksX; bx++) {
            const uint8_t* block = data + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
            const uint8_t* color = block + (explicitAlpha ? 8 : 0);
            uint32_t c0 = color[0] | color[1] << 8;
            uint32_t c1 = color[2] | color[3] << 8;
            uint8_t palette[4][3];
            auto expand = [](uint32_t value, uint8_t* out) {
                out[0] = static_cast<uint8_t>(((value >> 11) & 31) * 255 / 31);
                out[1] = static_cast<uint8_t>(((value >> 5) & 63) * 255 / 63);
                out[2] = static_cast<uint8_t>((value & 31) * 255 / 31);
            };
            expand(c0, palette[0]);
            expand(c1, palette[1]);
            for (uint32_t k = 0; k < 3; k++) {
                if (c0 > c1 || explicitAlpha) {
                    palette[2][k] = static_cast<uint8_t>((2 * palette[0][k] + palette[1][k]) / 3);
                    palette[3][k] = static_cast<uint8_t>((palette[0][k] + 2 * palette[1][k]) / 3);
                } else {
                    palette[2][k] = static_cast<uint8_t>((palette[0][k] + palette[1][k]) / 2);
                    palette[3][k] = 0;
                }
            }

            uint8_t alpha[8] = {};
            uint64_t alphaBits = 0;
            if (format == 7) {
                alpha[0] = block[0];
                alpha[1] = block[1];
                for (uint32_t k = 2; k < 8; k++) {
                    if (alpha[0] > alpha[1]) {
                        alpha[k] = static_cast<uint8_t>(((8 - k) * alpha[0] + (k - 1) * alpha[1]) / 7);
                    } else if (k < 6) {
                        alpha[k] = static_cast<uint8_t>(((6 - k) * alpha[0] + (k - 1) * alpha[1]) / 5);
                    } else {
                        alpha[k] = k == 6 ? 0 : 255;
                    }
                }

                for (uint32_t k = 0; k < 6; k++) {
                    alphaBits |= static_cast<uint64_t>(block[2 + k]) << (8 * k);
                }
            }

            uint32_t indices = color[4] | color[5] << 8 | color[6] << 16 | static_cast<uint32_t>(color[7]) << 24;
            for (uint32_t py = 0; py < 4; py++) {
                for (uint32_t px = 0; px < 4; px++) {
                    uint32_t x = bx * 4 + px;
                    uint32_t y = by * 4 + py;
                    if (x >= width || y >= height) {
                        continue;
                    }

                    uint32_t pixel = py * 4 + px;
                    uint32_t index = (indices >> (pixel * 2)) & 3;
                    uint8_t* out = rgba.data() + (static_cast<size_t>(y) * width + x) * 4;
                    out[0] = palette[index][0];
                    out[1] = palette[index][1];
                    out[2] = palette[index][2];
                    if (format == 1) {
                        out[3] = static_cast<uint8_t>(((block[pixel / 2] >> ((pixel & 1) * 4)) & 15) * 17);
                    } else if (format == 7) {
                        out[3] = alpha[(alphaBits >> (pixel * 3)) & 7];
                    } else {
                        out[3] = (c0 <= c1 && index == 3) ? 0 : 255;
                    }
                }
            }
        }
    }

    return true;
}

#include "EsteriaNativeEquipment.inl"

// Retail HD player atlases (layout 103) hold the classic component regions at exactly twice their Wrath position and size
// in the left 1024x1024 of the 2048x1024 skin, so equipped item layers are pasted into a copy of the chosen skin.
void ComposeHdBody(CCharacterComponent* character) {
    static const uint32_t counts[] = { 2, 7, 1, 5, 7, 3, 6, 1 };
    static const uint32_t rectangles[][4] = { { 0, 0, 256, 128 }, { 0, 128, 256, 128 }, { 0, 256, 256, 64 }, { 256, 0, 256, 128 },
        { 256, 128, 256, 64 }, { 256, 192, 256, 128 }, { 256, 320, 256, 128 }, { 256, 448, 256, 64 } };

    CharacterPreferences& prefs = character->m_data.m_preferences;
    Fields13 fields = FieldsOf(character);
    uint32_t skin = fields[0] % 5;

    uint32_t dirty = character->m_dirtySections;
    character->m_dirtySections = dirty | 0xFF;
    character->ItemsLoaded(1);
    character->m_dirtySections = dirty;

    struct Layer {
        uint32_t region;
        uint32_t size;
        const BLPHeader* blp;
    };
    std::vector<Layer> layers;
    uint64_t hash = 14695981039346656037ull ^ (skin + 1) ^ (static_cast<uint64_t>(prefs.m_sexID) << 8);
    bool robe = (character->m_flags & 0x20) != 0;
    for (uint32_t region = 0; region < 8; region++) {
        CharacterSection& section = character->m_section[region];
        uint32_t first = 0;
        uint32_t count = counts[region];
        if (region == 5 && robe) {
            first = 1;
            count = 2;
        }
        if (region == 6 && robe) {
            first = 1;
            count = 4;
        }

        for (uint32_t slot = first; slot < count; slot++) {
            CACHEENTRY* layer = static_cast<CACHEENTRY*>(section.m_layerTex[slot]);
            if (!(section.m_layerMask & (1u << slot)) || !layer) {
                continue;
            }

            if (!layer->m_data || !layer->m_info.m_width) {
                TCTEXTUREINFO info;
                TextureCacheGetInfo(layer, info, 1);
            }

            uint8_t pixel[4];
            if (!layer->m_data || !ClothingPixel(layer->m_data, layer->m_size, 0, 0, pixel)) {
                continue;
            }

            hash = (hash ^ region) * 1099511628211ull;
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(layer->m_data);
            for (uint32_t i = 0; i < layer->m_size; i++) {
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            }

            layers.push_back({ region, layer->m_size, layer->m_data });
        }
    }

    if (layers.empty()) {
        return;
    }

    static std::unordered_map<CCharacterComponent*, std::pair<uint64_t, HOBJECT>> cache;
    auto& entry = cache[character];
    if (!entry.second || entry.first != hash) {
        char path[128];
        sprintf_s(path, "Character\\DarkfallenHD\\%s\\DarkfallenSkin%02u.blp", prefs.m_sexID ? "Female" : "Male", skin);
        static std::unordered_map<std::string, std::vector<uint8_t>> bases;
        std::vector<uint8_t>& base = bases[path];
        if (base.empty()) {
            void* file = nullptr;
            uint32_t size = 0;
            if (!SFile::LoadFile(path, &file, &size, 0, nullptr) || !file) {
                return;
            }

            bool decoded = DecodeDxtBlp(static_cast<const BLPHeader*>(file), size, base);
            SFile::Unload(file);
            if (!decoded || base.size() != 2048u * 1024u * 4u) {
                base.clear();
                return;
            }
        }

        std::vector<uint8_t> pixels = base;
        for (const Layer& layer : layers) {
            const uint32_t* rect = rectangles[layer.region];
            uint32_t width = layer.blp->m_width;
            uint32_t height = layer.blp->m_height;
            uint32_t rectW = rect[2] * 2;
            uint32_t rectH = rect[3] * 2;
            for (uint32_t y = 0; y < rectH; y++) {
                for (uint32_t x = 0; x < rectW; x++) {
                    uint8_t source[4];
                    if (!ClothingPixel(layer.blp, layer.size, x * width / rectW, y * height / rectH, source)) {
                        return;
                    }

                    uint8_t* target = pixels.data() + ((rect[1] * 2 + y) * 2048 + rect[0] * 2 + x) * 4;
                    uint32_t alpha = source[3];
                    for (uint32_t channel = 0; channel < 3; channel++) {
                        target[channel] = static_cast<uint8_t>((source[channel] * alpha + target[channel] * (255 - alpha) + 127) / 255);
                    }
                }
            }
        }

        std::vector<uint8_t> blp;
        if (!BuildBlp(pixels, 2048, 1024, false, false, blp)) {
            return;
        }

        char name[96];
        sprintf_s(name, "Esteria\\darkfallen-body-%016llx", static_cast<unsigned long long>(hash));
        CGxTexFlags flags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
        CTexture* texture = TextureCreateFromBlpData(name, flags, blp.data());
        if (!texture) {
            return;
        }

        if (entry.second) {
            HandleClose(entry.second);
        }

        entry = { hash, reinterpret_cast<HOBJECT>(texture) };
    }

    character->m_data.m_model->ReplaceTexture(12, reinterpret_cast<CTexture*>(entry.second));
}

} // namespace

static bool Disabled();

// Calls `visit(options, selections)` with the option table of the character's race; false if the race is not ours.
template <class Visitor>
static bool VisitRace(const CharacterPreferences& prefs, Visitor&& visit) {
    uint32_t race = prefs.m_raceID;
    if (race == 75) {
        if (prefs.m_sexID) visit(CreatureAppearance::KyrianFemaleControls, RaceSelections(race));
        else visit(CreatureAppearance::KyrianMaleControls, RaceSelections(race));
    } else if (race == 76) {
        if (prefs.m_sexID) visit(CreatureAppearance::VenthyrFemaleControls, RaceSelections(race));
        else visit(CreatureAppearance::VenthyrMaleControls, RaceSelections(race));
    } else if (race == 69 || race == 70) {
        visit(CreatureAppearance::DrakonidControls, RaceSelections(race));
    } else if (race == 63) {
        visit(CreatureAppearance::GnollControls, RaceSelections(race));
    } else if (CreatureAppearance::Uses(race)) {
        visit(CreatureAppearance::Options(race, prefs.m_sexID), RaceSelections(race));
    } else if (race == 20) {
        if (prefs.m_sexID == 0) {
            visit(VulperaAppearance::MaleOptions, RaceSelections(race));
        } else {
            visit(VulperaAppearance::FemaleOptions, RaceSelections(race));
        }
    } else if (race == 50 || race == 51) {
        if (prefs.m_sexID == 0) {
            visit(HaranirAppearance::MaleOptions, RaceSelections(race));
        } else {
            visit(HaranirAppearance::FemaleOptions, RaceSelections(race));
        }
    } else if (race == 46) {
        if (prefs.m_sexID == 0) {
            visit(HighmountainAppearance::MaleOptions, RaceSelections(race));
        } else {
            visit(HighmountainAppearance::FemaleOptions, RaceSelections(race));
        }
    } else if (race == 48 || race == 49) {
        if (prefs.m_sexID == 0) {
            visit(EarthenAppearance::MaleOptions, RaceSelections(race));
        } else {
            visit(EarthenAppearance::FemaleOptions, RaceSelections(race));
        }
    } else {
        return false;
    }

    return true;
}

bool EsteriaGeometry(CCharacterComponent* character) {
    if (Disabled() || !character || RetailRaceActive(character)) {
        return false;
    }

    CharacterPreferences& prefs = character->m_data.m_preferences;
    Trace("geometry race=%u sex=%u", prefs.m_raceID, prefs.m_sexID);
    if (!character->m_data.m_model || CreatureAppearance::UsesStockMaterials(prefs.m_raceID, prefs.m_sexID)) {
        return false;
    }

    return VisitRace(prefs, [&](const auto& options, const std::vector<Selection>& records) {
        uint32_t race = prefs.m_raceID;
        if (race == 63 || race == 64 || (race == 66 && prefs.m_sexID == 0) || race == 69 || race == 70) {
            SetClothingMaterial(character);
        }

        // Material textures for groups 2.. (bank-backed races only), bound to the model slots the stock sections use.
        if (TextureBank(race)) {
            static const uint32_t slots[] = { 0, 0, 6, 5, 8, 12, 13, 9, 10 };
            for (uint32_t group = 2; group < 9; group++) {
                void* texture = nullptr;
                void* unused = nullptr;
                if (ComposeGroup(character, options, group, &texture, &unused) && texture) {
                    character->m_data.m_model->ReplaceTexture(slots[group], reinterpret_cast<CTexture*>(texture));
                }
            }
        }

        ApplyCatalog(character, options, records);
        if (race == 43 || race == 44) {
            ComposeHdBody(character);
        }

        // The model's own hardcoded textures (type 0) can come back as the solid placeholder; load them again by name.
        {
            CM2Model* loaded = character->m_data.m_model;
            if (loaded->m_shared && loaded->m_shared->m_data && loaded->m_textures) {
                M2Data* modelData = loaded->m_shared->m_data;
                for (uint32_t i = 0; i < modelData->m_textures.m_count; i++) {
                    const M2Texture& entry = modelData->m_textures[i];
                    if (entry.m_type != 0 || !entry.m_filename.m_count || !loaded->m_textures[i] || strncmp(loaded->m_textures[i]->m_filename, "SolidTexture", 12)) {
                        continue;
                    }

                    CStatus status;
                    CTexture* texture = CreateTexture(entry.m_filename.Data(), &status);
                    if (texture && strncmp(texture->m_filename, "SolidTexture", 12)) {
                        HandleClose(reinterpret_cast<HOBJECT>(loaded->m_textures[i]));
                        loaded->m_textures[i] = reinterpret_cast<CTexture*>(HandleDuplicate(reinterpret_cast<HOBJECT>(texture)));
                    }

                    if (texture) {
                        HandleClose(reinterpret_cast<HOBJECT>(texture));
                    }
                }
            }
        }

        // Custom models draw their cloak geoset even without a cape texture; show it only with an equipped cloak.
        if (!character->m_itemDisplayID[ITEMSLOT_CAPE]) {
            character->m_data.m_geosets[15] = 1500;
        }
    });
}

static bool Disabled() {
    static const bool disabled = [] {
        char path[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* slash = strrchr(path, 92);
        if (!slash) {
            return false;
        }
        strcpy(slash + 1, "EsteriaOff.txt");
        return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    }();
    return disabled;
}

bool EsteriaOwnsRace(uint32_t race, uint32_t sex) {
    if (Disabled()) {
        return false;
    }

    return race == 20 || race == 50 || race == 51 || (CreatureAppearance::Uses(race) && !CreatureAppearance::UsesStockMaterials(race, sex));
}

bool EsteriaBaseLayer(CCharacterComponent* character, int32_t variation, int32_t textureIndex, void** texture) {
    if (!character || RetailRaceActive(character)) {
        return false;
    }

    const CharacterPreferences& prefs = character->m_data.m_preferences;
    if (!EsteriaOwnsRace(prefs.m_raceID, prefs.m_sexID)) {
        return false;
    }

    *texture = nullptr;
    Trace("layer race=%u var=%d tex=%d skin=%u", prefs.m_raceID, variation, textureIndex, prefs.m_skinID);

    // Skin texture 0 is the body, face textures 0/1 are lower/upper face; everything else is unused for these races.
    uint32_t group = variation == VARIATION_SKIN && textureIndex == 0 ? 0 : variation == VARIATION_FACE && textureIndex < 2 ? 1 : 2;
    if (group > 1) {
        return true;
    }

    void* lower = nullptr;
    void* upper = nullptr;
    bool composed = false;
    VisitRace(prefs, [&](const auto& options, const std::vector<Selection>&) {
        composed = ComposeGroup(character, options, group, &lower, &upper);
    });

    Trace("compose group=%u -> %d lower=%p upper=%p", group, composed, lower, upper);
    if (composed) {
        // Face group: layer slot 0 is the lower part, slot 1 the upper; release the half this call does not use.
        if (group == 1 && textureIndex == 1) {
            TextureCacheDestroyTexture(lower);
            *texture = upper;
        } else {
            TextureCacheDestroyTexture(upper);
            *texture = lower;
        }
    }

    return true;
}

void EsteriaTrace(const char* format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    Trace("%s", text);
}

void EsteriaPreviewScale(CCharacterComponent* character) {
    CM2Model* model = character->m_data.m_model;
    if (!model || !model->m_shared || _strnicmp(model->m_shared->m_fileName, "custom\\vrykul\\", 14)) {
        return;
    }

    const C44Matrix& matrix = model->m_worldTransform;
    float current = std::hypot(matrix.a0, matrix.a1);
    if (!std::isfinite(current) || current < 0.001f || std::fabs(current - 0.55f) < 0.0001f) {
        return;
    }

    // Keep the preview's pedestal position and facing; only the scale changes.
    C3Vector position = { matrix.d0, matrix.d1, matrix.d2 };
    model->SetWorldTransform(position, std::atan2(matrix.a1, matrix.a0), 0.55f);
}

// ---- EA_* customization commands (port of HighmountainControl / PreviewAppearance) ----

namespace {

// Extra appearance bytes (fields 5..12); the first five live in the component's preferences.
std::unordered_map<CCharacterComponent*, std::array<uint8_t, 8>> g_extraFields;

Fields13 FieldsOf(CCharacterComponent* character) {
    Fields13 fields = CurrentFields(character->m_data.m_preferences);
    auto found = g_extraFields.find(character);
    if (found != g_extraFields.end()) {
        std::copy(found->second.begin(), found->second.end(), fields.begin() + 5);
    }

    return fields;
}

void StoreFields(CCharacterComponent* character, const Fields13& fields) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    const auto wire = prefs.m_raceID == 75 ? CreatureAppearance::EncodeKyrian(prefs.m_sexID, fields) : fields;
    for (uint32_t i = 0; i < 5; i++) {
        *PreferenceField(prefs, i) = wire[i];
    }

    std::array<uint8_t, 8> extra = {};
    std::copy(fields.begin() + 5, fields.end(), extra.begin());
    g_extraFields[character] = extra;
}

// Same setter sequence the stock component initializer uses, then rebuild geosets/textures.
void RefreshCharacter(CCharacterComponent* character, bool startLoad = true) {
    CharacterPreferences& prefs = character->m_data.m_preferences;
    character->SetHairColor(prefs.m_hairColorId, false, "");
    character->SetSkinColor(prefs.m_skinID, false, true, "");
    character->SetFace(prefs.m_faceId, false, "");
    character->SetHairStyle(prefs.m_hairStyleId, "");
    character->SetBeardStyle(prefs.m_facialHairId, false, "");
    character->m_flags |= 0x4;
    character->m_dirtySections = 0xFFFFFFFF;
    if (startLoad) {
        CGlueLoading::StartLoad(character, 1);
    }
}

bool PreviewAppearance(CCharacterComponent* character, const char* command) {
    // Character Create owns one model; keep its complete packed appearance for transient hovers.
    static CCharacterComponent* owner = nullptr;
    static uint32_t race = 0;
    static uint32_t gender = 0;
    static uint32_t characterClass = 0;
    static Fields13 saved = {};
    const CharacterPreferences& prefs = character->m_data.m_preferences;

    if (!strcmp(command, "EA_PREVIEW_END")) {
        owner = nullptr;
        return true;
    }

    if (!strcmp(command, "EA_PREVIEW_SAVE")) {
        owner = character;
        race = prefs.m_raceID;
        gender = prefs.m_sexID;
        characterClass = prefs.m_classId;
        saved = FieldsOf(character);
        return true;
    }

    if (owner != character || race != prefs.m_raceID || gender != prefs.m_sexID || characterClass != prefs.m_classId) {
        return false;
    }

    StoreFields(character, saved);
    RefreshCharacter(character);
    return true;
}

// Returns the number of Lua results pushed, or -1 when the command is not an option command.
template <class Options, class Labels, class Requirements>
int OptionControl(lua_State* L, const char* command, CCharacterComponent* character, const Options& options, const Labels& labels, const Requirements& requirements) {
    double requested = lua_tonumber(L, 2);
    bool isGet = !strcmp(command, "EA_GET");
    bool isChoices = !strcmp(command, "EA_CHOICES");
    if (!std::isfinite(requested) || requested < 1 || requested > options.size() || requested != std::floor(requested)) {
        if (isGet) {
            lua_pushnumber(L, 0);
            lua_pushnumber(L, 1);
            lua_pushstring(L, "");
            return 3;
        }

        if (isChoices) {
            lua_pushstring(L, "");
            return 1;
        }

        return 0;
    }

    uint32_t index = static_cast<uint32_t>(requested) - 1;
    Fields13 fields = FieldsOf(character);
    const CharacterPreferences& prefs = character->m_data.m_preferences;
    uint32_t race = prefs.m_raceID;
    uint32_t gender = prefs.m_sexID;
    uint32_t characterClass = prefs.m_classId;

    auto normalize = [&](Fields13& values) {
        if (race != 20) {
            return;
        }

        std::array<uint8_t, 5> vulpera = {};
        std::copy_n(values.begin(), vulpera.size(), vulpera.begin());
        vulpera = VulperaAppearance::Normalize(gender, characterClass, vulpera);
        std::copy(vulpera.begin(), vulpera.end(), values.begin());
    };
    auto allowed = [&](const Fields13& values) {
        if (!ValidFields(race, gender, values)) {
            return false;
        }

        if (race != 20) {
            return true;
        }

        std::array<uint8_t, 5> vulpera = {};
        std::copy_n(values.begin(), vulpera.size(), vulpera.begin());
        return VulperaAppearance::ValidateClass(gender, characterClass, vulpera);
    };

    Fields13 original = fields;
    normalize(fields);
    if (fields != original) {
        StoreFields(character, fields);
        RefreshCharacter(character);
    }

    if (!strcmp(command, "EA_RANDOM")) {
        static std::mt19937 random(GetTickCount());
        fields = {};
        normalize(fields);
        for (uint32_t i = 0; i < options.size(); i++) {
            const auto& descriptor = options[i];
            uint32_t first = std::uniform_int_distribution<uint32_t>(0, descriptor.count - 1)(random);
            for (uint32_t attempt = 0; attempt < descriptor.count; attempt++) {
                Fields13 candidate = fields;
                uint32_t value = (first + attempt) % descriptor.count;
                uint32_t prior = candidate[descriptor.field] / descriptor.factor % descriptor.count;
                candidate[descriptor.field] = static_cast<uint8_t>(candidate[descriptor.field] - prior * descriptor.factor + value * descriptor.factor);
                if (allowed(candidate)) {
                    fields = candidate;
                    break;
                }
            }
        }

        StoreFields(character, fields);
        RefreshCharacter(character);
        return 0;
    }

    const auto& option = options[index];
    uint32_t value = (fields[option.field] / option.factor) % option.count;
    if (isGet) {
        lua_pushnumber(L, value);
        uint32_t count = option.count;
        if (race == 20 && index == 9) {
            Fields13 candidate = fields;
            candidate[option.field] = static_cast<uint8_t>(candidate[option.field] - value * option.factor + option.factor);
            if (!allowed(candidate)) {
                count = 1;
            }
        }

        lua_pushnumber(L, count);
        lua_pushstring(L, labels[index]);
        return 3;
    }

    bool direct = !strcmp(command, "EA_SET");
    if (!isChoices && !direct && strcmp(command, "EA_CYCLE")) {
        return -1;
    }

    double direction = isChoices ? 1 : lua_tonumber(L, 3);
    if (direct ? (!std::isfinite(direction) || direction < 0 || direction >= option.count || direction != std::floor(direction)) : (direction != 1 && direction != -1)) {
        return isChoices ? 0 : 0;
    }

    std::string available;
    for (uint32_t step = 1; step <= (direct ? 1u : option.count); step++) {
        uint32_t next = isChoices ? step - 1 : direct ? static_cast<uint32_t>(direction)
            : static_cast<uint32_t>((static_cast<int>(value) + static_cast<int>(option.count) * 2 + static_cast<int>(step) * static_cast<int>(direction)) % static_cast<int>(option.count));
        Fields13 candidate = fields;
        candidate[option.field] = static_cast<uint8_t>(candidate[option.field] - value * option.factor + next * option.factor);

        // Changing a prerequisite resets incompatible dependent accessories to their authored default.
        for (uint32_t pass = 0; pass < options.size(); pass++) {
            for (const auto& rule : requirements) {
                const auto& selected = options[rule.option];
                const auto& required = options[rule.required];
                uint32_t dependent = (candidate[selected.field] / selected.factor) % selected.count;
                uint32_t prerequisite = (candidate[required.field] / required.factor) % required.count;
                if (rule.option != index && dependent == rule.value && !(rule.mask & (uint64_t{1} << prerequisite))) {
                    candidate[selected.field] -= static_cast<uint8_t>(dependent * selected.factor);
                }
            }
        }

        if (!allowed(candidate)) {
            continue;
        }

        if (isChoices) {
            available += std::to_string(next) + ",";
            continue;
        }

        StoreFields(character, candidate);
        RefreshCharacter(character);
        break;
    }

    if (isChoices) {
        lua_pushstring(L, available.c_str());
        return 1;
    }

    return 0;
}

}  // namespace

bool EsteriaRetailChoice(CCharacterComponent* character, uint32_t optionId, uint32_t& choiceId) {
    if (!character) {
        return false;
    }

    const CharacterPreferences& prefs = character->m_data.m_preferences;
    return Retail::LegacyAppearance::Decode(prefs.m_raceID, prefs.m_sexID, optionId, FieldsOf(character), choiceId);
}

bool EsteriaRetailStoreChoice(CCharacterComponent* character, uint32_t optionId, uint32_t choiceId) {
    if (!character) {
        return false;
    }

    const CharacterPreferences& prefs = character->m_data.m_preferences;
    Fields13 fields = FieldsOf(character);
    if (!Retail::LegacyAppearance::Encode(prefs.m_raceID, prefs.m_sexID, optionId, choiceId, fields)
        || !ValidFields(prefs.m_raceID, prefs.m_sexID, fields)) {
        return false;
    }

    StoreFields(character, fields);
    return true;
}

std::array<uint8_t, 13> EsteriaRetailFields(CCharacterComponent* character) {
    return character ? FieldsOf(character) : Fields13{};
}

void EsteriaRetailFields(CCharacterComponent* character, const std::array<uint8_t, 13>& fields) {
    if (character) {
        StoreFields(character, fields);
    }
}

int EsteriaCycleCommand(lua_State* L, CCharacterComponent* character, const char* command) {
    const int retail = RetailRaceCycleCommand(L, character, command);
    if (retail >= 0) {
        return retail;
    }

    if (Disabled() || !character) {
        return -1;
    }

    const CharacterPreferences& prefs = character->m_data.m_preferences;
    uint32_t race = prefs.m_raceID;
    uint32_t gender = prefs.m_sexID;

    // The five stock options (skin, face, hair style, hair colour, facial feature) of races on the stock layout:
    // EA_STOCK_GET -> value, count; EA_STOCK_CHOICES -> "0,1,3," (the values the stock cycling would land on).
    if (!strcmp(command, "EA_STOCK_GET") || !strcmp(command, "EA_STOCK_CHOICES")) {
        if (!lua_isnumber(L, 2)) {
            return 0;
        }

        const double requested = lua_tonumber(L, 2);

        if (requested < 1 || requested > 5 || requested != floor(requested)) {
            return 0;
        }

        st_race* table = CCharacterComponent::s_chrVarArray;
        const COMPONENT_SELECTION selection = GetSelectionFromContext(COMPONENT_CONTEXT_0, prefs.m_classId);
        auto allowed = [&](COMPONENT_VARIATIONS variation, int32_t index, int32_t color) {
            CharSectionsRec* rec = ComponentGetSectionsRecord(table, race, gender, variation, index, color, nullptr);
            return rec && ComponentFlagsMatch(rec->m_flags, selection);
        };

        int32_t value = 0;
        int32_t count = 0;
        bool noRecords = false; // facial features without section rows are counted from a plain list
        std::function<bool(int32_t)> valid;

        switch (static_cast<int32_t>(requested)) {
            case 1:
                value = prefs.m_skinID;
                count = ComponentGetNumColors(table, race, gender, VARIATION_SKIN, 0);
                valid = [&](int32_t v) { return allowed(VARIATION_SKIN, 0, v) && allowed(VARIATION_FACE, prefs.m_faceId, v); };
                break;
            case 2:
                value = prefs.m_faceId;
                count = ComponentGetNumVariations(table, race, gender, VARIATION_FACE);
                valid = [&](int32_t v) { return allowed(VARIATION_FACE, v, prefs.m_skinID); };
                break;
            case 3:
                value = prefs.m_hairStyleId;
                count = ComponentGetNumVariations(table, race, gender, VARIATION_HAIR);
                valid = [&](int32_t v) { return allowed(VARIATION_HAIR, v, prefs.m_hairColorId); };
                break;
            case 4:
                value = prefs.m_hairColorId;
                count = ComponentGetNumColors(table, race, gender, VARIATION_HAIR, prefs.m_hairStyleId);
                valid = [&](int32_t v) { return allowed(VARIATION_HAIR, prefs.m_hairStyleId, v); };
                break;
            default:
                value = prefs.m_facialHairId;
                count = ComponentGetNumVariations(table, race, gender, VARIATION_FACIAL_HAIR);

                if (!count) {
                    noRecords = true;
                    count = CCharacterComponent::s_characterFacialHairStylesList[race * 2 + gender];
                }

                valid = [&](int32_t v) { return noRecords || allowed(VARIATION_FACIAL_HAIR, v, prefs.m_hairColorId); };
                break;
        }

        if (count < 0 || count > 256) {
            return 0;
        }

        if (!strcmp(command, "EA_STOCK_GET")) {
            lua_pushnumber(L, value);
            lua_pushnumber(L, count);
            return 2;
        }

        std::string choices;

        for (int32_t v = 0; v < count; v++) {
            if (valid(v)) {
                choices += std::to_string(v) + ",";
            }
        }

        lua_pushstring(L, choices.c_str());
        return 1;
    }

    if (!strcmp(command, "EA_PREVIEW_SAVE") || !strcmp(command, "EA_PREVIEW_RESTORE") || !strcmp(command, "EA_PREVIEW_END")) {
        lua_pushnumber(L, PreviewAppearance(character, command) ? 1 : 0);
        return 1;
    }

    if (!strcmp(command, "EA_WHEEL_BLOCK") || !lua_isnumber(L, 2)) {
        return 0;
    }

    static const std::array<const char*, 5> nagaMale = { "Skin Color", "Face", "Crest Style", "Eye Color", "Facial Features" };
    static const std::array<const char*, 5> nagaFemale = { "Skin Color", "Face", "Hair Style", "Hair Color", "Facial Features" };
    static const std::array<const char*, 5> sethrak = { "Skin Color", "Face", "Armor Style", "Armor Color", "Facial Features" };
    static const std::array<const char*, 5> saberon = { "Fur Color", "Outfit Style", "Armor Color", "Cloth Color", "Facial Features" };
    static const std::array<VulperaAppearance::Requirement, 0> none = {};

    if (race == 75) {
        if (gender) return OptionControl(L, command, character, CreatureAppearance::KyrianFemaleControls, CreatureAppearance::KyrianFemaleLabels, none);
        return OptionControl(L, command, character, CreatureAppearance::KyrianMaleControls, CreatureAppearance::KyrianMaleLabels, none);
    }
    if (race == 76) {
        if (gender) return OptionControl(L, command, character, CreatureAppearance::VenthyrFemaleControls, CreatureAppearance::VenthyrFemaleLabels, none);
        return OptionControl(L, command, character, CreatureAppearance::VenthyrMaleControls, CreatureAppearance::VenthyrMaleLabels, none);
    }
    if (race == 54) {
        return OptionControl(L, command, character, CreatureAppearance::Options(54, gender), gender ? nagaFemale : nagaMale, none);
    }

    if (race == 62) {
        return OptionControl(L, command, character, CreatureAppearance::Options(62, gender), sethrak, none);
    }

    if (race == 63) {
        return OptionControl(L, command, character, CreatureAppearance::GnollControls, CreatureAppearance::GnollLabels, none);
    }

    if (race == 69 || race == 70) {
        return OptionControl(L, command, character, CreatureAppearance::DrakonidControls, CreatureAppearance::DrakonidLabels, none);
    }

    if (race == 64) {
        return OptionControl(L, command, character, CreatureAppearance::Options(64, 0), saberon, none);
    }

    if (CreatureAppearance::Uses(race)) {
        return -1;
    }

    if (race == 20) {
        return gender == 0 ? OptionControl(L, command, character, VulperaAppearance::MaleOptions, VulperaAppearance::MaleLabels, VulperaAppearance::MaleRequirements)
                           : OptionControl(L, command, character, VulperaAppearance::FemaleOptions, VulperaAppearance::FemaleLabels, VulperaAppearance::FemaleRequirements);
    }

    if (race == 50 || race == 51) {
        return gender == 0 ? OptionControl(L, command, character, HaranirAppearance::MaleOptions, HaranirAppearance::MaleLabels, HaranirAppearance::MaleRequirements)
                           : OptionControl(L, command, character, HaranirAppearance::FemaleOptions, HaranirAppearance::FemaleLabels, HaranirAppearance::FemaleRequirements);
    }

    if (race == 46) {
        return gender == 0 ? OptionControl(L, command, character, HighmountainAppearance::MaleOptions, HighmountainAppearance::MaleLabels, HighmountainAppearance::MaleRequirements)
                           : OptionControl(L, command, character, HighmountainAppearance::FemaleOptions, HighmountainAppearance::FemaleLabels, HighmountainAppearance::FemaleRequirements);
    }

    if (race == 48 || race == 49) {
        return gender == 0 ? OptionControl(L, command, character, EarthenAppearance::MaleOptions, EarthenAppearance::MaleLabels, EarthenAppearance::MaleRequirements)
                           : OptionControl(L, command, character, EarthenAppearance::FemaleOptions, EarthenAppearance::FemaleLabels, EarthenAppearance::FemaleRequirements);
    }

    return -1;
}

namespace {

struct CatalogSkin {
    char model[128];
    std::vector<unsigned char> skin;
};

template <class T>
T& SkinField(std::vector<unsigned char>& skin, size_t offset) {
    return *reinterpret_cast<T*>(skin.data() + offset);
}

bool ValidSkin(std::vector<unsigned char>& skin) {
    if (skin.size() < 48 || SkinField<uint32_t>(skin, 0) != 0x4E494B53) {
        return false;
    }

    const uint32_t strides[] = { 2, 2, 4, 48, 24 };
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t count = SkinField<uint32_t>(skin, 4 + i * 8);
        uint32_t offset = SkinField<uint32_t>(skin, 8 + i * 8);
        if (offset > skin.size() || count > (skin.size() - offset) / strides[i]) {
            return false;
        }
    }

    uint32_t vertices = SkinField<uint32_t>(skin, 4);
    uint32_t indices = SkinField<uint32_t>(skin, 12);
    uint32_t meshes = SkinField<uint32_t>(skin, 28);
    if (!vertices || vertices > 65535 || !indices || !meshes || meshes > 4096) {
        return false;
    }

    unsigned char* sections = skin.data() + SkinField<uint32_t>(skin, 32);
    for (uint32_t i = 0; i < meshes; i++) {
        unsigned char* section = sections + i * 48;
        uint32_t start = *reinterpret_cast<uint16_t*>(section + 8) | (*reinterpret_cast<uint16_t*>(section + 2) << 16);
        if (start > indices || *reinterpret_cast<uint16_t*>(section + 10) > indices - start
            || uint32_t(*reinterpret_cast<uint16_t*>(section + 4)) + *reinterpret_cast<uint16_t*>(section + 6) > vertices
            || *reinterpret_cast<uint16_t*>(section + 12) > 75) {
            return false;
        }
    }

    return true;
}

std::vector<CatalogSkin>& CatalogSkins() {
    static std::vector<CatalogSkin> skins = [] {
        std::vector<CatalogSkin> result;
        Catalog file = CatalogFile(L"EsteriaAppearanceGeometry.bin");
        if (!file) {
            return result;
        }

        uint32_t header[3] = {};
        if (file.Read(header, sizeof(header), 1) == 1 && header[0] == 0x4D474145 && header[1] == 1 && header[2] <= 258) {
            for (uint32_t i = 0; i < header[2]; i++) {
                CatalogSkin entry = {};
                uint32_t length = 0;
                if (file.Read(entry.model, sizeof(entry.model), 1) != 1 || !memchr(entry.model, 0, sizeof(entry.model))
                    || file.Read(&length, sizeof(length), 1) != 1 || length > 16 * 1024 * 1024) {
                    break;
                }

                entry.skin.resize(length);
                if (file.Read(entry.skin.data(), length, 1) != 1 || !ValidSkin(entry.skin)) {
                    break;
                }

                result.push_back(std::move(entry));
            }
        }

        return result;
    }();

    return skins;
}

}  // namespace

const std::vector<unsigned char>* EsteriaSkinBlob(const char* modelFile) {
    if (Disabled() || !modelFile) {
        return nullptr;
    }

    for (const CatalogSkin& entry : CatalogSkins()) {
        if (!_stricmp(modelFile, entry.model)) {
            return &entry.skin;
        }
    }

    return nullptr;
}

namespace {
thread_local CCharacterComponent* g_sectionContext = nullptr;
}

void EsteriaSetContext(CCharacterComponent* character) {
    g_sectionContext = character;
}

void EsteriaSectionArguments(int32_t race, int32_t sex, int32_t variation, int32_t* style, int32_t* color) {
    if (Disabled() || !g_sectionContext || variation < 0 || variation > 4 || sex < 0 || sex > 1) {
        return;
    }

    const CharacterPreferences& prefs = g_sectionContext->m_data.m_preferences;
    if (static_cast<int32_t>(prefs.m_raceID) != race || static_cast<int32_t>(prefs.m_sexID) != sex) {
        return;
    }

    if (race == 75 || race == 20 || race == 50 || race == 51) {
        *style = 0;
        *color = 0;
        return;
    }

    if (CreatureAppearance::Uses(race)) {
        return;
    }

    if (race != 46 && race != 48 && race != 49) {
        return;
    }

    Fields13 fields = FieldsOf(g_sectionContext);
    if (race != 46) {
        *style = variation == 1 ? fields[1] % 10 : 0;
        *color = variation == 3 ? fields[3] % 14 : variation == 2 ? 0 : fields[0] % 14;
        return;
    }

    // Highmountain resolves its real logical choices before the stock lookup indexes CharSections.
    uint32_t skin = fields[0] % 9;
    uint32_t face = fields[1] % (sex ? 4 : 5);
    uint32_t paint = sex ? (fields[1] / 4) % 4 : fields[3] / 64;
    uint32_t paintColor = sex ? fields[2] / 81 : fields[5] % 3;
    if (variation == 0 || variation == 1 || variation == 4) {
        *style = variation == 1 ? face : 0;
        *color = skin + 9 * (paint + 4 * paintColor);
    } else {
        *style = 0;
        *color = 0;
    }
}

int32_t EsteriaSectionCount(int32_t race, int32_t sex, int32_t variation) {
    if (Disabled() || sex < 0 || sex > 1 || variation < 0 || variation > 4) {
        return -1;
    }

    bool hair = variation == 2 || variation == 3;
    switch (race) {
        case 20: {
            const auto& capacities = sex ? VulperaAppearance::FemaleCapacities : VulperaAppearance::MaleCapacities;
            return hair ? capacities[3] : capacities[0];
        }
        case 46: {
            const auto& capacities = sex ? HighmountainAppearance::FemaleCapacities : HighmountainAppearance::MaleCapacities;
            return hair ? capacities[3] : capacities[0];
        }
        case 48:
        case 49: {
            const auto& capacities = sex ? EarthenAppearance::FemaleCapacities : EarthenAppearance::MaleCapacities;
            return hair ? capacities[3] : capacities[0];
        }
        case 50:
        case 51: {
            const auto& capacities = sex ? HaranirAppearance::FemaleCapacities : HaranirAppearance::MaleCapacities;
            return hair ? capacities[3] : capacities[0];
        }
        default:
            return -1;
    }
}

// ---- network / world plumbing ----

namespace {

std::unordered_map<uint64_t, uint64_t> g_roster;
std::unordered_map<uint64_t, uint32_t> g_wings;

bool ExtendedRace(uint32_t race) {
    return race == 20 || race == 46 || race == 47 || race == 48 || race == 49 || race == 50 || race == 51 || CreatureAppearance::Uses(race);
}

void SetExtra(CCharacterComponent* character, uint64_t extra) {
    uint32_t race = character->m_data.m_preferences.m_raceID;
    std::array<uint8_t, 8> bytes = {};
    if (race != 20 && race != 47 && !CreatureAppearance::Uses(race)) {
        for (uint32_t i = 0; i < 8; i++) {
            bytes[i] = static_cast<uint8_t>(extra >> (i * 8));
        }
    }

    g_extraFields[character] = bytes;
}

}  // namespace

bool EsteriaKeepsAppearance(uint32_t race) {
    return !Disabled() && ExtendedRace(race);
}

void EsteriaEnumExtra(CDataStore* packet) {
    g_roster.clear();
    g_wings.clear();
    BodyShape::Roster().clear();
    RetailRaceClearLooks();
    if (!packet || Disabled()) {
        return;
    }

    uint32_t length = packet->m_size;
    uint32_t base = packet->m_base;
    if (length < base || length - base < 8 || length - base > packet->m_alloc || !packet->m_data) {
        return;
    }

    uint8_t* bytes = packet->m_data;
    uint32_t size = length - base;
    for (uint32_t tail = 0; tail < 6 && size >= 8; tail++) {
        uint32_t magic = *reinterpret_cast<uint32_t*>(bytes + size - 4);
        uint32_t stride = magic == 0x31455848 ? 9 : magic == 0x32455848 ? 16 : magic == 0x31475743 ? 12
            : magic == CharacterBodyShape::EnumMagic ? CharacterBodyShape::EnumStride
            : magic == CharacterRetailLook::EnumMagic || magic == CharacterRetailLook::ParkedMagic ? CharacterRetailLook::EnumStride : 0;
        if (!stride) {
            break;
        }

        uint32_t count = *reinterpret_cast<uint32_t*>(bytes + size - 8);
        if (count > 100 || count > (size - 8) / stride) {
            break;
        }

        uint32_t start = size - 8 - count * stride;
        for (uint32_t i = 0; i < count; i++) {
            uint64_t guid = *reinterpret_cast<uint64_t*>(bytes + start + i * stride);
            if (stride == 9) {
                g_roster[guid] = bytes[start + i * stride + 8];
            } else if (stride == 12) {
                g_wings[guid] = *reinterpret_cast<uint32_t*>(bytes + start + i * stride + 8);
            } else if (stride == 16) {
                g_roster[guid] = *reinterpret_cast<uint64_t*>(bytes + start + i * stride + 8);
            } else if (stride == CharacterRetailLook::EnumStride) {
                CharacterRetailLook::Look look;
                if (CharacterRetailLook::ReadPayload(bytes + start + i * stride + 8, CharacterRetailLook::PayloadSize, look)) {
                    if (magic == CharacterRetailLook::ParkedMagic) RetailRaceSetParkedLook(guid, look);
                    else RetailRaceSetLook(guid, look);
                }
            } else if (stride == CharacterBodyShape::EnumStride) {
                CharacterBodyShape::Values wire{};
                if (CharacterBodyShape::ReadPayload(bytes + start + i * stride + 8, CharacterBodyShape::PayloadSize, wire)) {
                    BodyShape::Roster()[guid] = BodyShape::FromWire(wire);
                }
            }
        }

        size = start;
        packet->m_size = base + size;
    }
}

void EsteriaTraceText(const char* text) { Trace("%s", text); }

uint32_t EsteriaSelectWingSpell(uint64_t guid) {
    const auto found = g_wings.find(guid);
    Trace("wings: guid %llx spell %u of %zu", static_cast<unsigned long long>(guid), found == g_wings.end() ? 0 : found->second, g_wings.size());
    return found == g_wings.end() ? 0 : found->second;
}

bool EsteriaEyeLightning(uint64_t guid) {
    const auto found = g_roster.find(guid);
    return found != g_roster.end() && CharacterEyeEffects::Lightning(found->second);
}

void EsteriaSelectCharacter(CCharacterComponent* character, uint64_t guid, uint32_t skin, uint32_t face, uint32_t hairStyle, uint32_t hairColor, uint32_t facialHair) {
    RetailRaceEyeLightning(character, EsteriaEyeLightning(guid));
    RetailRaceApplyLookTo(character, guid, false);
    if (Disabled() || !character || !ExtendedRace(character->m_data.m_preferences.m_raceID)) {
        return;
    }

    CharacterPreferences& prefs = character->m_data.m_preferences;
    prefs.m_skinID = skin;
    prefs.m_faceId = face;
    prefs.m_hairStyleId = hairStyle;
    prefs.m_hairColorId = hairColor;
    prefs.m_facialHairId = facialHair;

    auto found = g_roster.find(guid);
    SetExtra(character, found == g_roster.end() ? 0 : CharacterEyeEffects::Appearance(found->second));

    Fields13 fields = FieldsOf(character);
    if (!ValidFields(prefs.m_raceID, prefs.m_sexID, fields)) {
        Trace("select: invalid appearance race=%u skin=%u face=%u style=%u color=%u facial=%u extra=%llu", prefs.m_raceID, skin, face, hairStyle, hairColor, facialHair, static_cast<unsigned long long>(found == g_roster.end() ? 0 : found->second));
        return;
    }

    RefreshCharacter(character, false);
}

void EsteriaCaptureUnit(CCharacterComponent* character, uint32_t unitPadding, uint32_t objectPadding, bool isPlayer) {
    if (Disabled() || !character || !ExtendedRace(character->m_data.m_preferences.m_raceID)) {
        return;
    }

    uint32_t race = character->m_data.m_preferences.m_raceID;
    uint64_t extra = race == 50 || race == 51 ? CharacterEyeEffects::Appearance(unitPadding | (isPlayer ? uint64_t{objectPadding} << 32 : 0)) : (unitPadding & 255);
    SetExtra(character, extra);
}

void EsteriaCreateExtra(CDataStore* packet) {
    CCharacterComponent* character = CCharacterCreation::m_character;
    if (Disabled() || !packet || !character) {
        return;
    }

    uint32_t race = character->m_data.m_preferences.m_raceID;
    if (!ExtendedRace(race) || race == 20 || race == 47 || CreatureAppearance::Uses(race)) {
        return;
    }

    uint32_t size = packet->m_size;
    uint32_t base = packet->m_base;
    if (!size || size < base || size - base > packet->m_alloc || !packet->m_data) {
        return;
    }

    Fields13 fields = FieldsOf(character);
    packet->m_data[size - base - 1] = race == 50 || race == 51 ? 0 : fields[5];
    if (race == 50 || race == 51) {
        for (uint32_t i = 5; i < fields.size(); i++) {
            packet->Put(static_cast<uint8_t>(fields[i]));
        }

        for (uint32_t i = 0; i < 4; i++) {
            packet->Put(static_cast<uint8_t>((0x31435248 >> (i * 8)) & 255));
        }
    }
}

void EsteriaForgetCharacter(CCharacterComponent* character) {
    selectorOutfits.erase(character);
    pendingSelectorBind.erase(character);
    g_extraFields.erase(character);
}

void EsteriaNativeEquipment(CCharacterComponent* character) {
    if (!character || !character->m_data.m_model || !character->m_data.m_model->m_shared
        || !character->m_data.m_model->m_shared->m_data || !(character->m_data.m_model->m_fFlags & 1)) return;
    BindSelectorOutfit(character);
}
bool EsteriaNativeEquipmentPending(CCharacterComponent* character) {
    return pendingSelectorBind.count(character) != 0;
}
void EsteriaClearNativeEquipment() {
    selectorOutfits.clear();
    pendingSelectorBind.clear();
}
