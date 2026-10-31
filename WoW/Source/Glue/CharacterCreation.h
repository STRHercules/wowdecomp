#ifndef WOW_SOURCE_GLUE_CHARACTERCREATION_H
#define WOW_SOURCE_GLUE_CHARACTERCREATION_H

#include <cstdint>
#include "Engine/Source/Storm/H/stpl.h"
#include "WoW/Source/Game/NameGen.h"
#include "WoW/Source/Glue/Types.h"

class CCamera;
class CCharacterComponent;
class CM2Model;
class ChrClassesRec;
class ComponentData;
class CSimpleModelFFX;
struct CharacterPreferences;

class CCharacterCreation {
    public:
    // Esteria: race IDs 0..128 (stock client: 22 slots)
    static const uint32_t MAX_RACES = 129;
    static CharacterPreferences* m_charPreferences[MAX_RACES][2];
    static CCharacterComponent* m_character;
    // Two Forms creator: a second preview showing the form the main character is not.
    static CCharacterComponent* m_formMirror;
    static CNameGen m_nameGen;
    static CSimpleModelFFX* m_charCustomizeFrame;
    static float m_charFacing;
    static TSGrowableArray<int32_t> m_races;
    static TSFixedArray<ChrClassesRec*> m_classes;
    static int32_t m_existingCharacterIndex;
    static int32_t m_selectedClassID;
    static uint32_t m_raceIndex;
    static uint32_t m_prevSkinIndex;
    static uint32_t m_prevFaceIndex;
    static uint32_t m_prevHairColorIndex;
    static uint32_t m_prevHairStyleIndex;
    static uint32_t m_prevFacialHairIndex;

    static void CalcClasses(int32_t raceID);
    static void CreateCharacter(const char* name);
    static void CycleCharCustomization(CHAR_CUSTOMIZATION_TYPE type, int32_t delta);
    static void Dress();
    static void Dress(CCharacterComponent* target);
    static void Undress(CCharacterComponent* target);
    static void SyncFormMirror();
    static void ClearFormMirror();
    static int32_t GetExistingClassIndex();
    static int32_t GetExistingRaceIndex();
    static int32_t GetRandomClassID();
    static void GetRandomRaceAndSex(ComponentData* data);
    static int32_t Idle(const void* data, void* param);
    static void InitCharacterComponents(ComponentData* data, bool randomize);
    static void Initialize();
    static bool IsClassAvailable(int32_t classID);
    static void OnHide();
    static void RandomizeCharCustomization();
    static void ResetCharCustomizeInfo();
    static void SetCharacterFacing(float facing);
    static void ZoomCamera(float delta);
    static void PanCamera(float dx, float dy);
    static void ResetPanCamera();
    static void UpdateZoomCamera();
    // Shared by the select screen: aims `camera` (made on first use) at the body's face, then zooms and pans.
    static void ApplyFaceCamera(CSimpleModelFFX* frame, CM2Model* body, float zoom, float panX, float panY, CCamera*& camera);
    static void ResetZoomCamera();
    static void SetCharCustomizeModel(const char* modelName);
    static void SetSelectedClass(int32_t classID);
    static void SetSelectedRace(uint32_t raceIndex);
    static void SetSelectedSex(uint32_t sexID);
    static void SetToExistingCharacter(uint32_t index);
    static void Shutdown();
};

bool IsRaceClassValid(int32_t raceID, int32_t classID);
void CharacterCreateRegisterScriptFunctions();
void CharacterCreateUnregisterScriptFunctions();

#endif
