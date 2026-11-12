#ifndef UPRIGHT_UNDEAD_H
#define UPRIGHT_UNDEAD_H

#include <cstdint>

struct CharacterPreferences;

// Upright undead: a second body for race 5 (Tools/UprightUndead packs it). Its customization rows sit in race 5's own
// CharSections / CharHairGeosets / CharacterFacialHairStyles rows at index Base and up, so the five appearance bytes
// say which body a character has: a skin of Base or more is upright. The server stores and sends them unchanged.
// An upright character is drawn with a plain 3.3.5a model, so the Retail race path (which needs a Retail model) stands
// aside and the stock selectors drive it.
namespace Upright {

constexpr uint32_t Race = 5;
constexpr int32_t Base = 64;

inline bool Index(uint32_t race, int32_t index) { return race == Race && index >= Base; }

// The upright body's model for race/sex/skin, or null (another body, or the package is not installed).
const char* ModelFor(uint32_t race, uint32_t sex, uint32_t skin);
bool Available(uint32_t race, uint32_t sex);

// True when choice `index` belongs to the other body than the one `prefs` shows (the skin decides).
bool Other(const CharacterPreferences& prefs, int32_t index);

// Facial features (CharacterFacialHairStyles rows) of the body a hair colour belongs to: its first id and how many.
int32_t FacialBase(uint32_t race, int32_t hairColor);
int32_t FacialCount(uint32_t race, uint32_t sex, int32_t hairColor);
// The stock range check (id 0..count inclusive, as the client always allowed), on the id's own body.
bool FacialInRange(uint32_t race, uint32_t sex, int32_t id, uint32_t listCount);

// Moves all five choices to the other body, keeping each index where both lists have it (retail creation order).
void Convert(CharacterPreferences& prefs, bool upright);

// While a Scope for an upright character is open, the Retail race's glue-screen counts stand aside for race 5 so the
// stock lookups count the upright rows.
struct Scope {
    explicit Scope(const CharacterPreferences& prefs);
    ~Scope();
    bool m_previous;
};
bool Scoped();

// The creator's choice, kept across sex and race switches (a fresh character of an upright pick starts upright).
bool Wanted();
void SetWanted(bool upright);

}  // namespace Upright

#endif
