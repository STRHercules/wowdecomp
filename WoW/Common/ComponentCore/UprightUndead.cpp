#include "WoW/Common/ComponentCore/UprightUndead.h"
#include <initializer_list>
#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "WoW/Common/ComponentCore/EsteriaAppearance.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/CharacterFacialHairStylesRec.h"
#include "WoW/Source/Glue/Types.h"

namespace Upright {

namespace {

const char* const kModels[2] = {
    "Character\\Scourge\\Upright\\Male\\ScourgeMale.m2",
    "Character\\Scourge\\Upright\\Female\\ScourgeFemale.m2",
};

thread_local bool g_scoped = false;
bool g_wanted = false;

bool UprightIndex(int32_t index) { return index >= Base; }

int32_t Relative(int32_t index) { return UprightIndex(index) ? index - Base : index; }

}  // namespace

bool Available(uint32_t race, uint32_t sex) {
    if (race != Race || sex > 1) return false;
    static int8_t present[2] = {-1, -1};
    if (present[sex] < 0) {
        // The same open the model loader does, so "available" means the model will load.
        SFileImpl* file = nullptr;
        present[sex] = SFile::OpenEx(nullptr, kModels[sex], 0, &file) && file ? 1 : 0;
        if (file) SFileCloseFile(file);
        EsteriaTrace("upright: %s %s", kModels[sex], present[sex] ? "found" : "NOT FOUND");
    }
    return present[sex] == 1;
}

const char* ModelFor(uint32_t race, uint32_t sex, uint32_t skin) {
    return Index(race, static_cast<int32_t>(skin)) && Available(race, sex) ? kModels[sex] : nullptr;
}

bool Other(const CharacterPreferences& prefs, int32_t index) {
    return prefs.m_raceID == Race && UprightIndex(index) != UprightIndex(static_cast<int32_t>(prefs.m_skinID));
}

int32_t FacialBase(uint32_t race, int32_t hairColor) {
    return Index(race, hairColor) ? Base : 0;
}

int32_t FacialCount(uint32_t race, uint32_t sex, int32_t hairColor) {
    const bool upright = Index(race, hairColor);
    int32_t count = 0;
    for (int32_t i = 0; i < g_characterFacialHairStylesDB.m_numRecords; i++) {
        const CharacterFacialHairStylesRec& rec = g_characterFacialHairStylesDB.m_records[i];
        if (static_cast<uint32_t>(rec.m_raceID) == race && static_cast<uint32_t>(rec.m_sexID) == sex
            && (race != Race || UprightIndex(rec.m_variationID) == upright)) {
            count++;
        }
    }
    return count;
}

bool FacialInRange(uint32_t race, uint32_t sex, int32_t id, uint32_t listCount) {
    if (id < 0) return false;
    if (race != Race) return static_cast<uint32_t>(id) <= listCount;
    const int32_t upright = FacialCount(race, sex, Base);
    return UprightIndex(id) ? id - Base <= upright : static_cast<int32_t>(listCount) - upright >= id;
}

void Convert(CharacterPreferences& prefs, bool upright) {
    const int32_t base = upright ? Base : 0;
    for (uint32_t* choice : {&prefs.m_skinID, &prefs.m_faceId, &prefs.m_hairStyleId, &prefs.m_hairColorId, &prefs.m_facialHairId}) {
        *choice = static_cast<uint32_t>(base + Relative(static_cast<int32_t>(*choice)));
    }
}

Scope::Scope(const CharacterPreferences& prefs) : m_previous(g_scoped) {
    g_scoped = Index(prefs.m_raceID, static_cast<int32_t>(prefs.m_skinID)) && Available(prefs.m_raceID, prefs.m_sexID);
}

Scope::~Scope() {
    g_scoped = m_previous;
}

bool Scoped() { return g_scoped; }

bool Wanted() { return g_wanted; }

void SetWanted(bool upright) { g_wanted = upright; }

}  // namespace Upright
