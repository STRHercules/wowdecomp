#ifndef ESTERIA_APPEARANCE_H
#define ESTERIA_APPEARANCE_H

#include <array>
#include <cstdint>

class CCharacterComponent;
class CDataStore;
struct lua_State;

// Esteria custom-race appearance (port of EsteriaAppearance.dll). Catalogs are read from the exe directory.
// Applies the catalog's geosets and replaceable textures; returns true if the race is handled.
bool EsteriaGeometry(CCharacterComponent* character);

// Vrykul models are authored at full size; the glue previews shrink them to 0.55 (EA_PREVIEW_SCALE).
void EsteriaPreviewScale(CCharacterComponent* character);

// Runs an EA_* glue command for the create-screen character; returns the number of Lua results, or -1 if unhandled.
int EsteriaCycleCommand(lua_State* L, CCharacterComponent* character, const char* command);

// Character whose setters are running; lets the CharSections lookups below see which appearance they serve.
void EsteriaSetContext(CCharacterComponent* character);

// Remaps (variation index, colour index) of a CharSections lookup for races whose choices are packed bytes.
void EsteriaSectionArguments(int32_t race, int32_t sex, int32_t variation, int32_t* style, int32_t* color);

// Authored colour capacity for custom races, or -1 for stock races.
int32_t EsteriaSectionCount(int32_t race, int32_t sex, int32_t variation);

// Character list: strips the appended extension records (extra appearance bytes per character) off SMSG_CHAR_ENUM.
void EsteriaEnumExtra(CDataStore* packet);

// Character select: applies the unclamped appearance bytes and the roster's extra bytes to the shown character.
void EsteriaSelectCharacter(CCharacterComponent* character, uint64_t guid, uint32_t skin, uint32_t face, uint32_t hairStyle, uint32_t hairColor, uint32_t facialHair);
bool EsteriaEyeLightning(uint64_t guid);
// The cosmetic wing spell the realm reported for a character (CWG1 tail of SMSG_CHAR_ENUM), 0 for none.
uint32_t EsteriaSelectWingSpell(uint64_t guid);
void EsteriaTraceText(const char* text);

// World objects: records the extra appearance bytes carried in the unit/object padding fields for a new component.
void EsteriaCaptureUnit(CCharacterComponent* character, uint32_t unitPadding, uint32_t objectPadding, bool isPlayer);

// True if stock component validation must be skipped (it clamps packed appearance bytes against sparse DBC rows).
bool EsteriaKeepsAppearance(uint32_t race);

// Character create: appends the extra appearance bytes to CMSG_CHAR_CREATE.
void EsteriaCreateExtra(CDataStore* packet);

// Drops per-component Esteria state when the component is freed.
void EsteriaForgetCharacter(CCharacterComponent* character);

// Appends a line to EsteriaDecomp.log (diagnostics).
void EsteriaTrace(const char* format, ...);

// True for races whose skin/face layers come from the Esteria catalog rather than CharSections.dbc.
bool EsteriaOwnsRace(uint32_t race, uint32_t sex);

// Stand-in for CCharacterComponent::LoadBaseVariation on Esteria races: `*texture` receives an owned layer (or null).
bool EsteriaBaseLayer(CCharacterComponent* character, int32_t variation, int32_t textureIndex, void** texture);

// Stable Retail choice IDs backed by the installed custom race's frozen appearance codec.
bool EsteriaRetailChoice(CCharacterComponent* character, uint32_t optionId, uint32_t& choiceId);
bool EsteriaRetailStoreChoice(CCharacterComponent* character, uint32_t optionId, uint32_t choiceId);

// Exact appearance snapshots for Retail preview save/restore, including extended server bytes.
std::array<uint8_t, 13> EsteriaRetailFields(CCharacterComponent* character);
void EsteriaRetailFields(CCharacterComponent* character, const std::array<uint8_t, 13>& fields);

void EsteriaNativeEquipment(CCharacterComponent* character);
bool EsteriaNativeEquipmentPending(CCharacterComponent* character);
void EsteriaClearNativeEquipment();

#endif
