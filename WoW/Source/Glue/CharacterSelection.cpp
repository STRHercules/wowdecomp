#include "Engine/Source/Retail/BodyShape.h"
#include "WoW/Common/ComponentCore/RetailRace.h"
#include "WoW/Common/ComponentCore/UprightUndead.h"
#include "WoW/Common/ComponentCore/EsteriaAppearance.h"
#include "CharacterSelection.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cctype>
#include <atomic>
#include <chrono>
#include <thread>
#include <cstring>
#include <fstream>
#include <map>
#include <sys/stat.h>
#include "Engine/Source/Base/Handle.h"
#include "Engine/Source/Services/Camera.h"
#include "Engine/Source/Services/CameraInt.h"
#include "Engine/Source/Os/Input.h"
#include <vector>
#include <cstring>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Event/Event.h"
#include "Engine/Source/FFX/FFX.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Model2/M2Shared.h"
#include "Engine/Source/Os/OsFile.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Storm/H/storm.h"
#include "Engine/Source/Tempest/C44Matrix.h"
#include "Engine/Source/Tempest/CAaSphere.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "WoW/Source/Client.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/CreatureDisplayInfoRec.h"
#include "WoW/Source/DB/Rec/CreatureFamilyRec.h"
#include "WoW/Source/DB/Rec/CreatureModelDataRec.h"
#include "WoW/Source/DB/Rec/ItemDisplayInfoRec.h"
#include "WoW/Source/DB/Rec/SpellVisualEffectNameRec.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/DB/Rec/SpellVisualKitModelAttachRec.h"
#include "WoW/Source/DB/Rec/SpellVisualKitRec.h"
#include "WoW/Source/DB/Rec/SpellVisualRec.h"
#include "WoW/Source/Magic/MagicClient/Spell_C.h"
#include "WoW/Source/Magic/MagicClient/SpellVisuals.h"
#include "WoW/Source/Game/GameClient/CDrawBowString.h"
#include "WoW/Source/Glue/CGlueLoading.h"
#include "WoW/Source/Glue/CGlueMgr.h"
#include "WoW/Source/Glue/CSimpleModelFFX.h"
#include "WoW/Source/Glue/CharacterCreation.h"
#include "WoW/Source/Glue/CharacterSelectionDisplay.h"
#include "Engine/Source/Services/Camera.h"
#include "Engine/Source/Base/Handle.h"
#include <algorithm>
#include <cmath>
#include "WoW/Source/Glue/ComponentData.h"
#include "WoW/Source/Glue/RealmList.h"
#include "WoW/Source/LoadingScreen.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/Object/ObjectClient/Types.h"
#include "WoW/Source/Ui/AddOns.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WowSvcs/WowSvcsClient/ClientServices.h"
#include "WoW/Source/Glue/InnWmoBackdrop.h"
#include "WoW/Source/Glue/InnWalk.h"
#include "WoW/Source/Glue/InnWalkers.h"

extern void (*g_innSelectBackdropHook)(CSimpleModel*);   // defined in Engine/Source/Frame/CSimpleModel.cpp

// STATIC: 0x009F3CD0
const int32_t g_scriptSexIndex[3] = { 2, 3, 1 };

// STATIC: 0x00AC441C
static float s_characterGlow = 0.4f;

// STATIC: 0x00AC4420
static float s_ghostGlow = 0.15f;

// STATIC: 0x00AC436C
int32_t CCharacterSelection::s_selectionIndex = -1;

WOWGUID CCharacterSelection::s_autoEnterGuid = { 0 };

// STATIC: 0x00B6B1FC
CSimpleModelFFX* CCharacterSelection::m_modelFrame;

namespace {
float s_selectZoom = 0.0f;
// Camera pan in view distances (right, up), as in the creator.
float s_selectPanX = 0.0f;
float s_selectPanY = 0.0f;
CCamera* s_selectCamera = nullptr;
}

void CCharacterSelection::ZoomCamera(float delta) {
    if (std::isfinite(delta)) {
        s_selectZoom = std::clamp(s_selectZoom + delta / 12.0f, 0.0f, 1.0f);
        if (s_selectZoom < 0.0001f) s_selectZoom = 0.0f;
    }
}

void CCharacterSelection::PanCamera(float dx, float dy) {
    if (!std::isfinite(dx) || !std::isfinite(dy)) return;
    s_selectPanX = std::clamp(s_selectPanX - dx, -1.0f, 1.0f);
    s_selectPanY = std::clamp(s_selectPanY - dy, -1.0f, 1.0f);
}

void CCharacterSelection::ResetPanCamera() {
    s_selectPanX = s_selectPanY = 0.0f;
}

void CCharacterSelection::ResetZoomCamera() {
    s_selectZoom = 0.0f;
    ResetPanCamera();
    if (m_modelFrame && s_selectCamera && m_modelFrame->m_camera == s_selectCamera) {
        m_modelFrame->SetCameraByIndex(0);
    }
    if (s_selectCamera) {
        HandleClose(reinterpret_cast<HOBJECT>(s_selectCamera));
        s_selectCamera = nullptr;
    }
}

void CCharacterSelection::UpdateZoomCamera() {
    CM2Model* body = nullptr;
    if (s_selectionIndex >= 0 && s_selectionIndex < static_cast<int32_t>(s_characterList.Count())) {
        auto* component = s_characterList[s_selectionIndex].m_component;
        body = component ? component->m_data.m_model : nullptr;
    }
    CCharacterCreation::ApplyFaceCamera(m_modelFrame, body, s_selectZoom, s_selectPanX, s_selectPanY, s_selectCamera);
}

// STATIC: 0x00B6B200
int32_t CCharacterSelection::s_characterCount;

// STATIC: 0x00B6B204
float CCharacterSelection::s_charFacing;

// STATIC: 0x00B6B208
ClassMask CCharacterSelection::s_raceRestrictions[10];

// STATIC: 0x00B6B238
TSGrowableArray<CharacterSelectionDisplay> CCharacterSelection::s_characterList;

// VERIFY: ef583a6e manual
// OFFSET: 0x005EEB70
void CCharacterSelection::Initialize() {
}

// VERIFY: cead22db auto
// OFFSET: 0x004E38F0
void CCharacterSelection::Shutdown() {
    CCharacterSelection::InnCastReset(true);
    LoadingScreenMiniDisable();
    CharacterSelectionDisplay::ClearFormMirror();

    if (CCharacterComponent::m_activePlayerComponent) {
        CCharacterComponent::FreeComponent(CCharacterComponent::m_activePlayerComponent);
        CCharacterComponent::m_activePlayerComponent = nullptr;
    }

    for (uint32_t i = 0; i < CCharacterSelection::s_characterList.Count(); i++) {
        if (!CCharacterSelection::s_characterList[i].m_component) {
            continue;
        }

        CM2Model* model = CCharacterSelection::s_characterList[i].m_component->m_data.m_model;

        if (!model || i != static_cast<uint32_t>(CGlueMgr::s_displayedCharacterIndex)) {
            continue;
        }

        if (CCharacterSelection::s_characterList[i].m_characterInfo.m_classID == 3) {
            CCharacterComponent::RemoveHandItem(model, INVSLOT_RANGED, SHEATHE_NONE, false);
            CCharacterSelection::s_characterList[i].m_component->RemoveItem(ITEMSLOT_11);
        }

        CCharacterComponent::RemoveVisuals(model);
        model->AttachToSceneRecursive(World::s_m2Scene);
        CCharacterComponent::m_activePlayerComponent = CCharacterSelection::s_characterList[i].m_component;
        CGPlayer_C::s_displayId = GetDisplayIdFromRaceAndGender(CCharacterSelection::s_characterList[i].m_characterInfo.m_raceID, CCharacterSelection::s_characterList[i].m_characterInfo.m_sexID);
        CCharacterSelection::s_characterList[i].m_component = nullptr;
    }

    CCharacterSelection::s_characterList.Clear();
    CharacterSelectionDisplay::SetSelectedCharacterInfo(nullptr);
}

// VERIFY: d8b19604 auto
// OFFSET: 0x004E2C30
void CGlueMgr::ProcessAttachment(CM2Model* model, int32_t effectID, uint32_t attachID) {
    if (!model->HasAttachment(attachID)) {
        return;
    }

    SpellVisualEffectNameRec* effectRec = g_spellVisualEffectNameDB.GetRecord(effectID);

    if (!effectRec) {
        return;
    }

    CM2Model* effect = CCharacterSelection::m_modelFrame->GetScene()->CreateModel(effectRec->m_fileName, 0);

    if (!effect) {
        return;
    }

    C44Matrix matrix;
    matrix.Scale(effectRec->m_scale);
    effect->m_fFlags |= 0x8000;
    effect->m_worldTransform = matrix;
    effect->AttachToParent(model, attachID, nullptr, 0);
    effect->Release();
}

static void AttachSelectKit(CM2Model* model, SpellVisualKitRec* kitRec);

// VERIFY: 01cf543c manual
// OFFSET: 0x004E2CE0
void CGlueMgr::AttachCharacterSelectVisuals(CM2Model* model) {
    AttachSelectKit(model, g_spellVisualKitDB.GetRecord(989));
}

// Kits like the wing auras carry their models in SpellVisualKitModelAttach rows, which the world plays through
// CGObject_C::PlaySpellVisualKit; this is the select screen's copy of that, minus effects that need a unit.
static void AttachSelectModelAttachments(CM2Model* model, int32_t kitId) {
    const uint32_t count = SpellVisualsGetModelAttachRecCount(kitId);
    char text[256];

    for (uint32_t i = 0; i < count; i++) {
        const SpellVisualKitModelAttachRec* attach = SpellVisualsGetModelAttachRec(kitId, i);
        SpellVisualEffectNameRec* effectRec = attach ? g_spellVisualEffectNameDB.GetRecord(attach->m_spellVisualEffectNameID) : nullptr;

        if (!effectRec) {
            continue;
        }

        const uint32_t attachId = attach->m_attachmentID < 0 ? 0 : attach->m_attachmentID;
        snprintf(text, sizeof(text), "wing attach %u: effect %d attachment %d has %d scale %.2f off %.2f %.2f %.2f", i,
            attach->m_spellVisualEffectNameID, attach->m_attachmentID, model->HasAttachment(attachId), effectRec->m_scale,
            attach->m_offset[0], attach->m_offset[1], attach->m_offset[2]);
        EsteriaTraceText(text);

        if (!model->HasAttachment(attachId)) {
            continue;
        }

        CM2Model* effect = CCharacterSelection::m_modelFrame->GetScene()->CreateModel(effectRec->m_fileName, 0);

        if (!effect) {
            continue;
        }

        C44Matrix matrix;
        matrix.Scale(effectRec->m_scale);
        matrix.RotateAroundX(attach->m_roll);
        matrix.RotateAroundY(attach->m_pitch);
        matrix.RotateAroundZ(attach->m_yaw);
        matrix.Translate(C3Vector(attach->m_offset[0], attach->m_offset[1], attach->m_offset[2]));
        effect->m_fFlags |= 0x8000;
        effect->m_worldTransform = matrix;
        effect->AttachToParent(model, attachId, nullptr, 0);
        effect->Release();
    }
}

// The state kit of a character's cosmetic wing aura (server tail CWG1), attached the way the select kit is.
static void AttachSelectWings(CM2Model* model, uint64_t guid) {
    SpellRec spell;
    const uint32_t spellId = EsteriaSelectWingSpell(guid);

    if (!spellId || !g_spellDB.GetRecord(spellId, &spell)) {
        return;
    }

    const SpellVisualRec* visual = GetSpellVisual(&spell);
    char text[256];
    snprintf(text, sizeof(text), "wing visual %d state %d cast %d precast %d impact %d", spell.m_spellVisualID[0],
        visual ? visual->m_stateKit : -1, visual ? visual->m_castKit : -1, visual ? visual->m_precastKit : -1, visual ? visual->m_impactKit : -1);
    EsteriaTraceText(text);

    if (visual) {
        SpellVisualKitRec* kit = g_spellVisualKitDB.GetRecord(visual->m_stateKit);
        snprintf(text, sizeof(text), "wing kit %p head %d chest %d base %d lh %d rh %d sp %d %d %d proc %d %d", static_cast<void*>(kit),
            kit ? kit->m_headEffect : 0, kit ? kit->m_chestEffect : 0, kit ? kit->m_baseEffect : 0, kit ? kit->m_leftHandEffect : 0,
            kit ? kit->m_rightHandEffect : 0, kit ? kit->m_specialEffect[0] : 0, kit ? kit->m_specialEffect[1] : 0,
            kit ? kit->m_specialEffect[2] : 0, kit ? kit->m_charProc[0] : 0, kit ? kit->m_charProc[1] : 0);
        EsteriaTraceText(text);
        AttachSelectKit(model, kit);
        AttachSelectModelAttachments(model, visual->m_stateKit);
    }
}

static void AttachSelectKit(CM2Model* model, SpellVisualKitRec* kitRec) {
    if (!kitRec) {
        return;
    }

    CGlueMgr::ProcessAttachment(model, kitRec->m_headEffect, 20);
    CGlueMgr::ProcessAttachment(model, kitRec->m_chestEffect, 34);
    CGlueMgr::ProcessAttachment(model, kitRec->m_baseEffect, 19);
    CGlueMgr::ProcessAttachment(model, kitRec->m_leftHandEffect, 21);
    CGlueMgr::ProcessAttachment(model, kitRec->m_rightHandEffect, 22);
    CGlueMgr::ProcessAttachment(model, kitRec->m_breathEffect, 17);
    CGlueMgr::ProcessAttachment(model, kitRec->m_specialEffect[0], 23);
    CGlueMgr::ProcessAttachment(model, kitRec->m_specialEffect[1], 24);
    CGlueMgr::ProcessAttachment(model, kitRec->m_specialEffect[2], 25);

    for (uint32_t i = 0; i < 4; i++) {
        switch (kitRec->m_charProc[i]) {
            case 1: {
                CImVector color;
                *reinterpret_cast<uint32_t*>(&color) = static_cast<uint32_t>(kitRec->m_charParamZero[i]) | 0xFF000000;
                model->m_diffuseTintR = color.r * (1.0f / 255.0f);
                model->m_diffuseTintG = color.g * (1.0f / 255.0f);
                model->m_diffuseTintB = color.b * (1.0f / 255.0f);
                break;
            }

            case 14: {
                float alpha = kitRec->m_charParamZero[i];

                if (alpha < 0.0f) {
                    model->m_fadeAlpha = 0.0f;
                } else {
                    model->m_fadeAlpha = alpha > 1.0f ? 1.0f : alpha;
                }

                break;
            }

            default: {
                break;
            }
        }
    }
}

// VERIFY: 61c6f157 auto
// OFFSET: 0x004E0110
void CCharacterSelection::ModelLoadedCallback(CM2Model* model, void* arg) {
    if (model->HasEvent('TTW$') && model->HasEvent('BTW$')) {
        model->m_fFlags |= 0x20;
        model->m_drawCallback = &CDrawBowString::GlueDrawBowStringCB;
        model->m_drawArg = arg;
    }
}

// VERIFY: 91e60469 auto
// OFFSET: 0x004E27C0
void CCharacterSelection::GuildCallback(int32_t guildID, const WOWGUID& guid, void* param, bool success) {
    if (!guildID || !success) {
        return;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(CCharacterSelection::s_characterList.Count()); i++) {
        if (CCharacterSelection::s_characterList[i].m_characterInfo.m_guildID != static_cast<uint32_t>(guildID)) {
            continue;
        }

        int32_t emblemStyle;
        int32_t emblemColor;
        int32_t borderStyle;
        int32_t borderColor;
        int32_t background;

        if (GuildGetGuildTabard(WOWGUID(), guildID, nullptr, nullptr, emblemStyle, emblemColor, borderStyle, borderColor, background)) {
            CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component->ApplyGuildColor(emblemStyle, emblemColor, borderStyle, borderColor, background);
        }
    }
}

// VERIFY: 9fe08e4d auto
// OFFSET: 0x004E2870
void CCharacterSelection::UpdateCharacterName(WOWGUID guid, const char* name) {
    uint32_t index;

    for (index = 0; index < CCharacterSelection::s_characterList.Count(); index++) {
        if (CCharacterSelection::s_characterList[index].m_characterInfo.m_guid == guid) {
            break;
        }
    }

    if (index >= CCharacterSelection::s_characterList.Count()) {
        return;
    }

    char oldPath[260];
    char newPath[260];
    SStrPrintf(oldPath, sizeof(oldPath), "WTF\\Account\\%s\\%s\\%s", CGlueMgr::m_accountName, ClientServices::GetSelectedRealmName(), CCharacterSelection::s_characterList[index].m_characterInfo.m_name);
    SStrPrintf(newPath, sizeof(newPath), "WTF\\Account\\%s\\%s\\%s", CGlueMgr::m_accountName, ClientServices::GetSelectedRealmName(), name);
    OsMoveFile(oldPath, newPath);
    SStrPrintf(oldPath, sizeof(oldPath), "WTF\\Account\\%s\\%s", CGlueMgr::m_accountName, CCharacterSelection::s_characterList[index].m_characterInfo.m_name);
    SStrPrintf(newPath, sizeof(newPath), "WTF\\Account\\%s\\%s", CGlueMgr::m_accountName, name);
    OsMoveFile(oldPath, newPath);
    ClearAddOnEnableState(CCharacterSelection::s_characterList[index].m_characterInfo.m_name);
    LoadAddOnEnableState(name);
    SStrCopy(CCharacterSelection::s_characterList[index].m_characterInfo.m_name, name, sizeof(CCharacterSelection::s_characterList[index].m_characterInfo.m_name));
    CCharacterSelection::s_characterList[index].m_characterInfo.m_flags &= ~0x4000;
    CCharacterSelection::s_characterList[index].m_characterInfo.m_flags &= ~0x2000000;
    FrameScript_SignalEvent(GLUE_EVENT_CHARACTER_LIST_UPDATE, nullptr);
}

// VERIFY: 945f4dbd auto
// OFFSET: 0x004E29E0
void CCharacterSelection::UpdateCharacterInfo(WOWGUID guid, const char* name, uint8_t sexID, uint8_t skinColorID, uint8_t faceID, uint8_t hairStyleID, uint8_t hairColorID, uint8_t facialHairStyleID, uint8_t raceID) {
    uint32_t index;

    for (index = 0; index < CCharacterSelection::s_characterList.Count(); index++) {
        if (CCharacterSelection::s_characterList[index].m_characterInfo.m_guid == guid) {
            break;
        }
    }

    if (index >= CCharacterSelection::s_characterList.Count()) {
        return;
    }

    if (SStrCmpI(name, CCharacterSelection::s_characterList[index].m_characterInfo.m_name, STORM_MAX_STR)) {
        char oldPath[260];
        char newPath[260];
        SStrPrintf(oldPath, sizeof(oldPath), "WTF\\Account\\%s\\%s\\%s", CGlueMgr::m_accountName, ClientServices::GetSelectedRealmName(), CCharacterSelection::s_characterList[index].m_characterInfo.m_name);
        SStrPrintf(newPath, sizeof(newPath), "WTF\\Account\\%s\\%s\\%s", CGlueMgr::m_accountName, ClientServices::GetSelectedRealmName(), name);
        OsMoveFile(oldPath, newPath);
        SStrPrintf(oldPath, sizeof(oldPath), "WTF\\Account\\%s\\%s", CGlueMgr::m_accountName, CCharacterSelection::s_characterList[index].m_characterInfo.m_name);
        SStrPrintf(newPath, sizeof(newPath), "WTF\\Account\\%s\\%s", CGlueMgr::m_accountName, name);
        OsMoveFile(oldPath, newPath);
        ClearAddOnEnableState(CCharacterSelection::s_characterList[index].m_characterInfo.m_name);
        LoadAddOnEnableState(name);
        SStrCopy(CCharacterSelection::s_characterList[index].m_characterInfo.m_name, name, sizeof(CCharacterSelection::s_characterList[index].m_characterInfo.m_name));
    }

    CHARACTER_INFO& info = CCharacterSelection::s_characterList[index].m_characterInfo;
    info.m_sexID = sexID;
    info.m_skinColorID = skinColorID;
    info.m_faceID = faceID;
    info.m_hairStyleID = hairStyleID;
    info.m_hairColorID = hairColorID;
    info.m_facialHairStyleID = facialHairStyleID;

    if (raceID) {
        info.m_raceID = raceID;
    }

    info.m_customizeFlags &= 0xFFEEFFFE;
    FrameScript_SignalEvent(GLUE_EVENT_CHARACTER_LIST_UPDATE, nullptr);
}

// VERIFY: db6735fe manual
// OFFSET: 0x004E2BD0
void CCharacterSelection::UpdateCharacterNameDeclension(WOWGUID guid) {
    for (uint32_t index = 0; index < CCharacterSelection::s_characterList.Count(); index++) {
        if (CCharacterSelection::s_characterList[index].m_characterInfo.m_guid == guid) {
            CCharacterSelection::s_characterList[index].m_characterInfo.m_flags |= 0x2000000;
            return;
        }
    }
}

// VERIFY: 805a43b3 auto
// OFFSET: 0x004E2EF0
CharacterSelectionDisplay* CCharacterSelection::GetCharacterDisplay(uint32_t index) {
    if (index < CCharacterSelection::s_characterList.Count()) {
        return &CCharacterSelection::s_characterList[index];
    }

    return nullptr;
}

// Each pick fades the character in, then plays the salute once and settles back to the idle stance.
static const uint32_t INTRO_FADE_MS = 550;
static bool s_introFading;
static int32_t s_introSalute = 2; // 0 waiting for the model, 1 playing, 2 done
static uint32_t s_introStartMs;
static uint32_t s_introEndMs;
static float s_introBaseAlpha = 1.0f;
static CCharacterComponent* s_introComponent;

// Hand-held items (attach links 1 and 2) vanish for the salute; the shield on the arm stays.
static void SetHandItemsHidden(CM2Model* model, bool hidden) {
    for (CM2Model* attached = model->m_attachList; attached; attached = attached->m_attachNext) {
        if (attached->m_attachmentId == ATTACH_HANDRIGHT || attached->m_attachmentId == ATTACH_HANDLEFT) {
            attached->m_fadeAlpha = hidden ? 0.0f : 1.0f;
        }
    }
}

static void BeginSelectionIntro(CharacterSelectionDisplay& display, bool newComponent) {
    // A pick made mid-salute must not leave the previous character unarmed.
    for (uint32_t i = 0; i < CCharacterSelection::s_characterList.Count(); i++) {
        CharacterSelectionDisplay& other = CCharacterSelection::s_characterList[i];

        if (other.m_component && other.m_component->m_data.m_model) {
            SetHandItemsHidden(other.m_component->m_data.m_model, false);
        }
    }

    CM2Model* model = display.m_component ? display.m_component->m_data.m_model : nullptr;

    if (!model) {
        return;
    }

    // Reselecting mid-fade must not take the half-faded alpha as the character's real one (ghosts are translucent).
    if (newComponent || !s_introFading || s_introComponent != display.m_component) {
        s_introBaseAlpha = model->m_fadeAlpha;
    }

    s_introComponent = display.m_component;
    s_introFading = true;
    s_introSalute = 0;
    s_introStartMs = OsGetAsyncTimeMs();
    model->m_fadeAlpha = 0.0f;

    if (display.m_petModel) {
        display.m_petModel->m_fadeAlpha = 0.0f;
    }
}

static void UpdateSelectionIntro(CharacterSelectionDisplay& display) {
    CM2Model* model = display.m_component ? display.m_component->m_data.m_model : nullptr;

    if (!model || display.m_component != s_introComponent) {
        return;
    }

    uint32_t now = OsGetAsyncTimeMs();

    if (s_introFading) {
        uint32_t elapsed = now - s_introStartMs;
        float t = elapsed >= INTRO_FADE_MS ? 1.0f : static_cast<float>(elapsed) / INTRO_FADE_MS;
        float ease = t * t * (3.0f - 2.0f * t);
        model->m_fadeAlpha = s_introBaseAlpha * ease;

        if (display.m_petModel) {
            display.m_petModel->m_fadeAlpha = display.m_petAlpha * ease;
        }

        s_introFading = t < 1.0f;
    }

    if (s_introSalute == 0 && model->IsLoaded(0, 0)) {
        if (model->HasSequence(ANIM_EMOTE_SALUTE)) {
            M2SequenceInfo info;
            info.Clear();
            model->GetSequenceInfo(ANIM_EMOTE_SALUTE, 0, info);
            model->SetBoneSequence(0xFFFFFFFF, ANIM_EMOTE_SALUTE, 0xFFFFFFFF, 0, 1.0f, 1, 1);
            s_introEndMs = now + (info.m_duration ? info.m_duration : 2000);
            s_introSalute = 1;
            SetHandItemsHidden(model, true);
        } else {
            s_introSalute = 2;
        }
    } else if (s_introSalute == 1 && static_cast<int32_t>(now - s_introEndMs) >= 0) {
        model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        SetHandItemsHidden(model, false);
        s_introSalute = 2;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Inn character select, stage 0 (Claude 2026-11-14). A prototype, off by default.
// WOW_INN_CAST=N (1..24) shows the first N characters of the list at once, in rows of WOW_INN_CAST_PER_ROW (6), idle only,
// and eases the camera (about 0.6 s) from an overview of the group to the selected character's face; the selected character
// plays a greeting emote (bow, else salute, else wave) for WOW_INN_CAST_EMOTE_MS (2400) and then idles.
// Tuning without a rebuild: WOW_INN_CAST_SPACING (1.4, sideways), WOW_INN_CAST_ROW_SPACING (1.6, depth), WOW_INN_CAST_DEPTH_SIGN
// (1 or -1, which way the rows go), WOW_INN_CAST_PULL (1.0, overview distance multiplier), WOW_INN_CAST_ZOOM (0.35, how close
// the selected camera gets: smaller is closer; 0 turns the zoom off and keeps the overview), WOW_INN_CAST_TAU_MS (180, ease time constant).
// With the variable unset or 0 nothing here runs and the character select behaves exactly as before.
// Not done in stage 0: clicking characters in the 3D view, wandering, the inn art, pets of characters built as extras.
// ---------------------------------------------------------------------------------------------------------------------
void* InnWatchdogOpenCurrentThread();                  // InnWatchdogStack.cpp
void InnWatchdogWriteStack(void* thread, FILE* file);  // InnWatchdogStack.cpp
namespace {

// v49: hang watchdog (inn cast only). The select screen ticks InnCastTick every frame (the beat). If the beat stops for 6 seconds while the screen is open, a helper
// thread appends one line to InnWatchdog.log with the phase the main thread was last in, so a silent freeze can be located (our code or the engine/renderer after it).
std::atomic<uint32_t> s_innBeat{0};
std::atomic<const char*> s_innPhase{"start"};
std::atomic<bool> s_innDogActive{false};
std::atomic<bool> s_innDogStarted{false};
inline void InnPhase(const char* phase) { s_innPhase.store(phase, std::memory_order_relaxed); }
void InnWatchdogStart() {
    if (s_innDogStarted.exchange(true)) return;
    void* mainThread = InnWatchdogOpenCurrentThread();   // called from the glue loop, i.e. the main thread
    std::thread([mainThread]() {
        uint32_t last = s_innBeat.load();
        uint32_t stuckMs = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            const uint32_t beat = s_innBeat.load();
            if (!s_innDogActive.load() || beat != last) { last = beat; stuckMs = 0; continue; }
            stuckMs += 500;
            if (stuckMs == 6000 || (stuckMs > 6000 && stuckMs % 30000 == 0)) {
                FILE* file = std::fopen("InnWatchdog.log", "ab");
                if (file) {
                    std::fprintf(file, "main thread stalled %u ms, last phase: %s (beat %u)\n", stuckMs, s_innPhase.load(), beat);
                    InnWatchdogWriteStack(mainThread, file);
                    std::fclose(file);
                }
            }
        }
    }).detach();
}
// Settings come from InnCast.cfg in the game folder (lines "NAME=value", same names as the environment variables, # comments), re-read
// within half a second of the file changing, so edits apply on the next frame without restarting anything. The environment variable
// of the same name is the fallback.
std::map<std::string, std::string>& InnCastFileSettings() {
    static std::map<std::string, std::string> settings;
    return settings;
}

void InnCastReloadFile() {
    static uint32_t lastCheck = 0;
    static long long lastStamp = -2;
    const uint32_t now = OsGetAsyncTimeMs();
    if (lastCheck && now - lastCheck < 500) return;
    lastCheck = now;

    struct _stat64 info;
    const long long stamp = _stat64("InnCast.cfg", &info) == 0 ? static_cast<long long>(info.st_mtime) * 1000003LL + static_cast<long long>(info.st_size) : -1;
    if (stamp == lastStamp) return;
    lastStamp = stamp;
    InnCastFileSettings().clear();
    if (stamp < 0) return;

    std::ifstream file("InnCast.cfg");
    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto trim = [](std::string text) {
            const char* space = " \t\r\n";
            const size_t first = text.find_first_not_of(space);
            if (first == std::string::npos) return std::string();
            return text.substr(first, text.find_last_not_of(space) - first + 1);
        };
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (!key.empty() && !value.empty()) InnCastFileSettings()[key] = value;
    }
    sExtensionMgr->Log("InnCast settings file read: %u entries", static_cast<uint32_t>(InnCastFileSettings().size()));
}

float InnCastEnvFloat(const char* name, float fallback) {
    InnCastReloadFile();
    const auto found = InnCastFileSettings().find(name);
    const char* value = found != InnCastFileSettings().end() ? found->second.c_str() : std::getenv(name);
    if (!value || !*value) return fallback;
    const float parsed = static_cast<float>(std::atof(value));
    return std::isfinite(parsed) ? parsed : fallback;
}

// Body scale of a cast member in the inn. The inn is built to world proportions, so the character-select pedestal shrink (Ethereal 0.55, Venthyr 0.8) is not applied:
// Ethereal (race 77/78) = WOW_INN_SCALE_ETHEREAL (1.0), Venthyr (61) = full native humanoid scale x WOW_INN_SCALE_VENTHYR (1.0); every other race keeps its preview scale.
float InnCastScale(uint32_t race, uint32_t sex) {
    const float preview = RetailRacePreviewScale(race);
    if (race == 77 || race == 78) return std::max(0.1f, InnCastEnvFloat("WOW_INN_SCALE_ETHEREAL", 1.0f));
    if (race == 61) return preview / 0.80f * std::max(0.1f, InnCastEnvFloat("WOW_INN_SCALE_VENTHYR", 1.0f));
    return preview;
}

// Text setting (file first, environment fallback); empty when not set.
std::string InnCastEnvString(const char* name) {
    InnCastReloadFile();
    const auto found = InnCastFileSettings().find(name);
    if (found != InnCastFileSettings().end()) return found->second;
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

constexpr int32_t kInnMax = 100;     // list entries the inn cast tracks (the hall holds WOW_INN_CAST of them at once, the rest rotate through)

int32_t InnCastLimit() {
    const int32_t n = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_CAST", 0.0f));
    return n < 0 ? 0 : (n > 24 ? 24 : n);
}

// List entries the cast tracks (v45: all of them up to kInnMax; which ones are in the hall is decided by InnCastPlan / InnCastRotate, capacity InnCastLimit()).
int32_t InnCastShown() {
    if (InnCastLimit() <= 0) return 0;
    const int32_t listed = static_cast<int32_t>(CCharacterSelection::s_characterList.Count());
    return listed < kInnMax ? listed : kInnMax;
}

// Entrance plan (cold-start crash mitigation + life in the room): only the first WOW_INN_CAST_START (15) characters are built when the select opens;
// the rest are built ONE at a time, WOW_INN_CAST_TRICKLE_MS (4000) apart, and each enters through an entrance spot (see InnWalk.cfg SPOT=<id> entrance).
// WOW_INN_CAST_START=0 builds everything at once as before.
int32_t InnCastStartCount(int32_t shown) {
    const int32_t start = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_CAST_START", 15.0f));
    return (start > 0 && start < shown) ? start : shown;
}

// WMO backdrop (see InnWmoBackdrop.h). WOW_INN_WMO=<path of the .wmo root> turns it on while the cast is on; the per-race backdrop model is then
// pinned to the first one requested (so every character stands in the same place) and hidden, and the WMO is drawn in its space.
// Placement/tuning, all live from InnCast.cfg: WOW_INN_WMO_X/_Y/_Z/_YAW (radians)/_SCALE, _BRIGHT (1.6), _SWAPRB (-1 auto, 0, 1), _DOODADS (1), _MAX_DOODADS (400).
InnWmo::Settings InnWmoSettings() {
    InnWmo::Settings cfg;
    cfg.path = InnCastEnvString("WOW_INN_WMO");
    cfg.x = InnCastEnvFloat("WOW_INN_WMO_X", 0.0f);
    cfg.y = InnCastEnvFloat("WOW_INN_WMO_Y", 0.0f);
    cfg.z = InnCastEnvFloat("WOW_INN_WMO_Z", 0.0f);
    cfg.yaw = InnCastEnvFloat("WOW_INN_WMO_YAW", 0.0f);
    cfg.scale = InnCastEnvFloat("WOW_INN_WMO_SCALE", 1.0f);
    cfg.bright = InnCastEnvFloat("WOW_INN_WMO_BRIGHT", 1.6f);
    cfg.swapRB = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_WMO_SWAPRB", -1.0f));
    cfg.doodads = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_WMO_DOODADS", 1.0f));
    cfg.maxDoodads = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_WMO_MAX_DOODADS", 900.0f));
    cfg.clearRadius = InnCastEnvFloat("WOW_INN_WMO_CLEAR", 2.6f);
    cfg.clearX = InnCastEnvFloat("WOW_INN_WMO_CLEAR_X", -1.1f);
    cfg.clearY = InnCastEnvFloat("WOW_INN_WMO_CLEAR_Y", 0.0f);
    cfg.clearZ = InnCastEnvFloat("WOW_INN_WMO_CLEAR_Z", 1.6f);
    cfg.doodadFlags = static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_WMO_DOODAD_FLAGS", 0.0f)));
    cfg.doodadSet = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_WMO_DOODAD_SET", 2.0f));
    return cfg;
}

// v43 see-through line (all live from InnCast.cfg): WOW_INN_FADE (1 on, 0 off), WOW_INN_FADE_MODE (1 fade furniture, 2 hide it instantly), WOW_INN_FADE_WALLS (1),
// _RADIUS 0.12, _TARGET_CLEAR 0.45, _DOODAD_MAX 1.0, _DOODAD_SHRINK 0.7, _TIME 0.25 s, _TRI_MAX 0.6 (scene units).
InnWmo::FadeSettings InnFadeSettings() {
    InnWmo::FadeSettings f;
    f.mode = InnCastEnvFloat("WOW_INN_FADE", 1.0f) > 0.5f ? static_cast<int32_t>(InnCastEnvFloat("WOW_INN_FADE_MODE", 1.0f)) : 0;
    f.walls = InnCastEnvFloat("WOW_INN_FADE_WALLS", 1.0f) > 0.5f ? 1 : 0;
    f.radius = std::max(0.0f, InnCastEnvFloat("WOW_INN_FADE_RADIUS", 0.12f));
    f.targetClear = std::max(0.0f, InnCastEnvFloat("WOW_INN_FADE_TARGET_CLEAR", 0.45f));
    f.doodadMax = std::max(0.0f, InnCastEnvFloat("WOW_INN_FADE_DOODAD_MAX", 1.0f));
    f.doodadShrink = std::max(0.05f, InnCastEnvFloat("WOW_INN_FADE_DOODAD_SHRINK", 0.7f));
    f.time = std::max(0.0f, InnCastEnvFloat("WOW_INN_FADE_TIME", 0.25f));
    f.triMax = std::max(0.0f, InnCastEnvFloat("WOW_INN_FADE_TRI_MAX", 0.6f));
    return f;
}

void InnWmoHook(CSimpleModel* simple) {
    if (!simple || simple != CCharacterSelection::m_modelFrame || !simple->m_model || InnCastLimit() <= 0) return;
    const InnWmo::Settings cfg = InnWmoSettings();
    if (cfg.path.empty()) return;
    CM2Model* anchor = simple->m_model;
    if (!(anchor->m_fFlags & 1)) return;    // backdrop model not loaded yet
    anchor->SetGeometryVisible(0, 0xFFFF, 0);

    InnWmo::State& state = InnWmo::S();
    if (!state.attempted || state.loadedPath != cfg.path) {
        InnWmo::Forget();
        InnWalk::Forget();
        InnWalkers::Reset();
        InnWmo::Load(cfg, simple->GetScene());
    }
    const C44Matrix placement = InnWmo::Placement(cfg, anchor->m_worldTransform);
    InnWmo::PlaceDoodads(cfg, placement, simple->GetScene(), CCharacterSelection::m_modelFrame->m_lightArray1, &CCharacterSelection::GenericLightingCallback);
    C3Vector cameraPos(0.0f, 0.0f, 0.0f);
    if (simple->m_camera) DataMgrGetCoord(simple->m_camera, 7, &cameraPos);   // same source RenderModelImpl hands to the scene's Animate
    const InnWmo::FadeSettings fade = InnFadeSettings();
    {
        static uint32_t fadeLast = 0;
        const uint32_t fadeNow = OsGetAsyncTimeMs();
        const float fadeDt = fadeLast ? std::min(0.1f, static_cast<float>(fadeNow - fadeLast) * 0.001f) : 0.016f;
        fadeLast = fadeNow;
        InnWmo::UpdateDoodadFade(fade, cameraPos, fadeDt);
    }
    InnPhase("inn wmo draw");
    InnWmo::Draw(cfg, placement, cameraPos, fade);
    InnPhase("after the inn wmo draw");

    // Step 3.2b: walking-area debug markers (InnWalk.cfg + WOW_INN_WALK_DEBUG=1). Default marker model = the inn's own green bottle.
    {
        std::string markerM2 = InnCastEnvString("WOW_INN_WALK_MARKER");
        if (markerM2.empty()) {
            for (const InnWmo::Doodad& d : state.doodads) {
                std::string lower = d.name;
                for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                if (lower.find("greenbottle02") != std::string::npos) { markerM2 = InnWmo::ToM2(d.name); break; }
            }
            if (markerM2.empty() && !state.doodads.empty()) markerM2 = InnWmo::ToM2(state.doodads.front().name);
        }
        InnWalk::Debug(InnCastEnvFloat("WOW_INN_WALK_DEBUG", 0.0f) > 0.5f, InnCastEnvFloat("WOW_INN_WALK_LINKS", 1.0f) > 0.5f, markerM2,
            InnCastEnvFloat("WOW_INN_WALK_MARKER_SCALE", 3.0f), InnCastEnvFloat("WOW_INN_WALK_BEAD_SCALE", 1.0f), placement, simple->GetScene(),
            CCharacterSelection::m_modelFrame->m_lightArray1, &CCharacterSelection::GenericLightingCallback, OsGetAsyncTimeMs());
    }

    const uint32_t now = OsGetAsyncTimeMs();
    if (now - state.lastLog >= 5000) {
        state.lastLog = now;
        sExtensionMgr->Log("InnWmo draw batches=%u waitingForTexture=%u fadeFocus=%d fadedFurniture=%u fadedTriangles=%u", state.drawnBatches, state.skippedBatches, state.focusActive ? 1 : 0, state.fadedDoodads, state.fadedTris);

        // Step 3.1: furniture census. created = a scene model exists (visible unless hidden by the clearing), failed = CreateModel gave nothing,
        // hidden = inside the cleared radius, pending = not requested yet (spread over frames or past the maximum).
        {
            uint32_t created = 0, failed = 0, hidden = 0, pending = 0, notLoaded = 0;
            for (const InnWmo::Doodad& d : state.doodads) {
                if (d.failed) failed++;
                else if (d.model) { created++; if (d.hidden) hidden++; if (!(d.model->m_fFlags & 1)) notLoaded++; }
                else pending++;
            }
            sExtensionMgr->Log("InnWmo furniture total=%u created=%u (still loading=%u, hidden by clearing=%u) failed=%u pending=%u", static_cast<uint32_t>(state.doodads.size()), created, notLoaded, hidden, failed, pending);
            static bool namedFailures = false;
            if (!namedFailures && failed > 0 && pending == 0) {
                namedFailures = true;
                std::string names;
                uint32_t shown = 0;
                for (const InnWmo::Doodad& d : state.doodads) {
                    if (!d.failed) continue;
                    if (shown++ >= 12) break;
                    names += d.name; names += "; ";
                }
                sExtensionMgr->Log("InnWmo furniture failed (first %u): %s", shown, names.c_str());
            }
        }

        // Diagnostics (UPDATE 12): what placement does the game really use?
        const C44Matrix& am = anchor->m_worldTransform;
        sExtensionMgr->Log("InnWmo anchor matrix rows (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) t=(%.3f %.3f %.3f)",
            am.a0, am.a1, am.a2, am.b0, am.b1, am.b2, am.c0, am.c1, am.c2, am.d0, am.d1, am.d2);
        sExtensionMgr->Log("InnWmo placement rows (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) t=(%.3f %.3f %.3f)",
            placement.a0, placement.a1, placement.a2, placement.b0, placement.b1, placement.b2, placement.c0, placement.c1, placement.c2,
            placement.d0, placement.d1, placement.d2);
        const C3Vector probeCam = C3Vector(5.45f, -4.72f, 5.27f) * placement;     // the .gps spot, in scene space
        const C3Vector probeFloor = C3Vector(2.2f, -0.92f, -0.19f) * placement;   // the stage floor point, in scene space
        sExtensionMgr->Log("InnWmo probes: gps spot -> scene (%.2f %.2f %.2f)  stage floor -> scene (%.2f %.2f %.2f)  camera=(%.2f %.2f %.2f)",
            probeCam.x, probeCam.y, probeCam.z, probeFloor.x, probeFloor.y, probeFloor.z, cameraPos.x, cameraPos.y, cameraPos.z);
        if (simple->m_camera) {
            sExtensionMgr->Log("InnWmo camera fields f2=%.3f f3(near)=%.4f f4(fov)=%.3f f5(far)=%.1f",
                CameraGetFloat(simple->m_camera, 2), CameraGetFloat(simple->m_camera, 3), CameraGetFloat(simple->m_camera, 4), CameraGetFloat(simple->m_camera, 5));
        }
        const CGxMatrixStack& viewStack = g_theGxDevicePtr->m_xforms[GxXform_View];
        const C44Matrix& vm = viewStack.m_mtx[viewStack.m_level];
        sExtensionMgr->Log("InnWmo gx view rows (%.3f %.3f %.3f) (%.3f %.3f %.3f) (%.3f %.3f %.3f) t=(%.3f %.3f %.3f)",
            vm.a0, vm.a1, vm.a2, vm.b0, vm.b1, vm.b2, vm.c0, vm.c1, vm.c2, vm.d0, vm.d1, vm.d2);
    }
}

const bool s_innHookRegistered = (g_innSelectBackdropHook = &InnWmoHook, true);

CCamera* s_innCamera = nullptr;
C3Vector s_innPos, s_innTgt;
C3Vector s_innBasePos, s_innBaseTgt;   // the backdrop model's own camera (diagnostics)
float s_innOverviewPull = 0.0f;

// Sequence table check (Sol's question: why do both skeleton files contain lean 1260 but HasSequence says no?). Compares a linear scan of the model's own
// sequence array with the hash lookup HasSequence uses, and prints the table sizes.
void InnCastLogSequenceTable(CM2Model* model, const char* tag, int32_t index, uint32_t race, uint32_t sex) {
    if (!model || !model->m_shared || !model->m_shared->m_data) return;
    M2Data* data = model->m_shared->m_data;
    const uint32_t count = data->m_sequences.m_count;
    uint32_t maxId = 0, distinct = 0, lastId = 0xFFFFFFFFu;
    bool linear1260 = false, linear1262 = false, linear1264 = false;
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t id = data->m_sequences[i].m_id;
        if (id != lastId) { distinct++; lastId = id; }
        if (id > maxId) maxId = id;
        if (id == 1260) linear1260 = true;
        if (id == 1262) linear1262 = true;
        if (id == 1264) linear1264 = true;
    }
    sExtensionMgr->Log("InnCast seqtable %s index=%d race=%u sex=%u sequences=%u distinct~%u hashSlots=%u maxId=%u linear(1260,1262,1264)=%d,%d,%d hash(1260,1262,1264)=%d,%d,%d",
        tag, index, race, sex, count, distinct, static_cast<uint32_t>(data->m_sequenceIdxHashById.m_count), maxId,
        linear1260 ? 1 : 0, linear1262 ? 1 : 0, linear1264 ? 1 : 0,
        model->HasSequence(1260) ? 1 : 0, model->HasSequence(1262) ? 1 : 0, model->HasSequence(1264) ? 1 : 0);
}

// Step 3.1: once per cast member, log which animation sequence ids its model has (0..199), so walking/emotes are planned from facts.
// ---- v45: door rotation. The hall holds WOW_INN_CAST characters at once; the others wait outside. Every WOW_INN_ROTATE_MIN..MAX seconds (30..45) one of the
// characters in the hall walks out through a door and another one walks in through a door. Never leave: the selected one, the last played one, the first
// WOW_INN_STAY_FIRST (6) of the list, and the names in WOW_INN_STAY_NAMES (comma separated, case does not matter: favourites). A character clicked while
// outside walks in from a door (running when it takes long) and the camera zooms when it arrived. ----
bool s_innAnimLogged[kInnMax] = {};
uint32_t s_innLastTrickle = 0;          // when the last late character was built
bool s_innArrive[kInnMax] = {};         // built by the trickle, not yet placed at an entrance
bool s_innTrickleFailed[kInnMax] = {};  // a build that failed once is not retried
bool s_innWanted[kInnMax] = {};         // in the hall (or on its way in)
bool s_innInitial[kInnMax] = {};        // built when the select opens (the others come in one by one)
bool s_innStay[kInnMax] = {};           // never rotates out
bool s_innLeaving[kInnMax] = {};        // walking out through a door
bool s_innComing[kInnMax] = {};         // clicked while outside: walking in, the camera waits
bool s_innPresent[kInnMax] = {};        // has a walker in the hall
bool s_innDropCache[kInnMax] = {};      // the model was freed: forget the cached animation masks
uint32_t s_innLastLeft[kInnMax] = {};   // when it last left (0 = never): the longest absent comes in next
uint32_t s_innLeaveStart[kInnMax] = {};
uint32_t s_innSince[kInnMax] = {};       // v48: when a late or rotating-in character arrived (0 = none): it may not be picked to leave before WOW_INN_STAY_MIN seconds have passed
bool s_innPlanned = false;
uint32_t s_innNextRotate = 0;
uint32_t s_innComeStart = 0;
int32_t s_innRotSeenSel = -2;
bool InnComing(int32_t i) { return i >= 0 && i < kInnMax && s_innComing[i]; }

// Who never rotates out: the selected, the last played, the first WOW_INN_STAY_FIRST of the list, the names in WOW_INN_STAY_NAMES. Recomputed when the list order changes.
void InnCastComputeStay(int32_t listed) {
    for (int32_t k = 0; k < kInnMax; k++) s_innStay[k] = false;
    auto stay = [&](int32_t i) { if (i >= 0 && i < listed) s_innStay[i] = true; };
    stay(CCharacterSelection::s_selectionIndex);
    if (g_lastCharacterIndexVar) stay(g_lastCharacterIndexVar->m_intValue);
    const int32_t first = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_STAY_FIRST", 6.0f));
    for (int32_t i = 0; i < first; i++) stay(i);
    std::string names = InnCastEnvString("WOW_INN_STAY_NAMES");
    if (!names.empty()) {
        std::string clean;
        for (char ch : names) if (ch != ' ') clean += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        const std::string wrapped = "," + clean + ",";
        for (int32_t i = 0; i < listed; i++) {
            std::string mine;
            for (const char* p = CCharacterSelection::s_characterList[i].m_characterInfo.m_name; *p; p++) mine += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
            if (!mine.empty() && wrapped.find("," + mine + ",") != std::string::npos) stay(i);
        }
    }
}

// Decides once per visit to the select who is in the hall: the stay group first, then the lowest list entries, up to the capacity.
void InnCastPlan() {
    if (s_innPlanned) return;
    const int32_t listed = InnCastShown();
    if (listed <= 0) return;
    s_innPlanned = true;
    for (int32_t k = 0; k < kInnMax; k++) {
        s_innWanted[k] = s_innInitial[k] = s_innStay[k] = s_innLeaving[k] = s_innComing[k] = s_innPresent[k] = false;
        s_innLastLeft[k] = 0; s_innLeaveStart[k] = 0; s_innSince[k] = 0;
    }
    const int32_t capacity = std::min(listed, InnCastLimit());
    InnCastComputeStay(listed);
    int32_t count = 0, stayCount = 0;
    for (int32_t i = 0; i < listed && count < capacity; i++) if (s_innStay[i]) { s_innWanted[i] = true; count++; stayCount++; }
    for (int32_t i = 0; i < listed && count < capacity; i++) if (!s_innWanted[i]) { s_innWanted[i] = true; count++; }
    const int32_t start = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_CAST_START", 15.0f));
    int32_t made = 0;
    for (int32_t i = 0; i < listed; i++) if (s_innWanted[i] && (start <= 0 || made < start)) { s_innInitial[i] = true; made++; }
    s_innNextRotate = OsGetAsyncTimeMs() + static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_ROTATE_FIRST", 30.0f)) * 1000.0f);
    sExtensionMgr->Log("InnCast plan: listed=%d capacity=%d stay=%d inHall=%d builtAtOpen=%d", listed, capacity, stayCount, count, made);
}

// A character that walked out of a door: free its model (it is built again when it comes back in).
void InnCastRemove(int32_t i, uint32_t now) {
    if (i < 0 || i >= kInnMax || i >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) return;
    CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[i];
    if (entry.m_component) {
        CM2Model* model = entry.m_component->m_data.m_model;
        if (model && model->m_attachParent) model->DetachFromParent();
        CCharacterComponent::FreeComponent(entry.m_component);
        entry.m_component = nullptr;
    }
    s_innLeaving[i] = false; s_innArrive[i] = false; s_innPresent[i] = false; s_innComing[i] = false;
    s_innDropCache[i] = true;
    s_innLastLeft[i] = now ? now : 1;
    s_innSince[i] = 0;
    sExtensionMgr->Log("InnCast left: index=%d walked out through a door", i);
}
void InnCastAnimInventory() {
    const int32_t shown = InnCastShown();
    for (int32_t i = 0; i < shown; i++) {
        if (s_innAnimLogged[i]) continue;
        CCharacterComponent* component = CCharacterSelection::s_characterList[i].m_component;
        CM2Model* model = component ? component->m_data.m_model : nullptr;
        if (!model || !(model->m_fFlags & 1)) continue;
        s_innAnimLogged[i] = true;
        std::string present;
        uint32_t count = 0;
        for (uint32_t id = 0; id < 200; id++) {
            if (model->HasSequence(id)) { present += std::to_string(id); present += ","; count++; }
        }
        const CHARACTER_INFO& info = CCharacterSelection::s_characterList[i].m_characterInfo;
        InnCastLogSequenceTable(model, "cast", i, static_cast<uint32_t>(info.m_raceID), static_cast<uint32_t>(info.m_sexID));
        sExtensionMgr->Log("InnCast anims index=%d race=%u sex=%u count=%u ids=%s", i, static_cast<uint32_t>(info.m_raceID), static_cast<uint32_t>(info.m_sexID), count, present.c_str());
        // Retail (CASC) models may carry animation ids beyond the old client's table (the /lean emote is one): list 200..2999 too.
        std::string high;
        uint32_t highCount = 0;
        for (uint32_t id = 200; id < 3000; id++) {
            if (model->HasSequence(id)) { high += std::to_string(id); high += ","; highCount++; }
        }
        sExtensionMgr->Log("InnCast anims-high index=%d race=%u sex=%u count=%u ids=%s", i, static_cast<uint32_t>(info.m_raceID), static_cast<uint32_t>(info.m_sexID), highCount, high.c_str());
    }

    // The selected character may lie beyond the shown cast (built by the normal path): log its high ids too, once per model.
    const int32_t sel = CCharacterSelection::s_selectionIndex;
    if (sel >= shown && sel < static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) {
        static CM2Model* loggedSelected = nullptr;
        CCharacterComponent* component = CCharacterSelection::s_characterList[sel].m_component;
        CM2Model* model = component ? component->m_data.m_model : nullptr;
        if (model && (model->m_fFlags & 1) && model != loggedSelected) {
            loggedSelected = model;
            std::string all;
            uint32_t total = 0;
            for (uint32_t id = 0; id < 3000; id++) {
                if (model->HasSequence(id)) { all += std::to_string(id); all += ","; total++; }
            }
            const CHARACTER_INFO& info = CCharacterSelection::s_characterList[sel].m_characterInfo;
            InnCastLogSequenceTable(model, "selected", sel, static_cast<uint32_t>(info.m_raceID), static_cast<uint32_t>(info.m_sexID));
            sExtensionMgr->Log("InnCast anims-selected index=%d race=%u sex=%u count=%u ids=%s", sel, static_cast<uint32_t>(info.m_raceID), static_cast<uint32_t>(info.m_sexID), total, all.c_str());
        }
    }
}
bool s_innHaveCam = false;
bool s_innZoomed = false;       // false = overview of the whole room (on entering the select); a click on a character zooms in, Z pulls back
int32_t s_innSeenSel = -2;      // selection last seen by the camera (-2 = none yet)
uint32_t s_innCamEnter = 0;     // when the camera first ran on this visit to the select screen
bool s_innZKeyDown = false;
int32_t s_innReleasedSel = -3;   // Z releases the selected character: it walks about again until another one is clicked (selection index it was released at)
// Explore camera (right mouse button): hold the right button and drag to look around from the current view; W/A/S/D move, Q/E down/up, Shift faster.
// Z (back to the room view) or choosing a character (zoom) ends it. Settings: WOW_INN_EXPLORE (1), _LOOK (0.004 rad per pixel), _SPEED (2.0 scene units/s).
bool s_innExplore = false, s_innRmbDown = false;
float s_innExYaw = 0.0f, s_innExPitch = 0.0f;
C3Vector s_innExPos(0.0f, 0.0f, 0.0f);
int32_t s_innLastMx = 0, s_innLastMy = 0;
uint32_t s_innExLog = 0;
uint32_t s_innLastTick = 0;
int32_t s_innEmoteIndex = -1;
uint32_t s_innEmoteStart = 0;
uint32_t s_innEmoteMs = 0;
bool s_innEmoteIdled = false;     // v44: the idle pose after the greeting is sent once (it was re-sent every frame, which froze and flickered the stand animation)
uint32_t s_innStatStart = 0, s_innStatFrames = 0;
float s_innStatWorst = 0.0f;

void InnCastIdlePose(CCharacterComponent* component) {
    if (component && component->m_data.m_model) {
        component->m_data.m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    }
}

// Builds the component of list entry `index` the way ShowCharacter does (same dressing and saved Retail appearance), but without
// touching the pet, the loading state or the selection. Owned by the list entry, so ShowCharacter reuses it later.
// Inn cast members keep their weapons sheathed (hip / back / shield) while they walk about; WOW_INN_SHEATHE=0 puts them back in the hands.
bool s_innDressSheathed = false;
bool InnCastBuild(uint32_t index) {
    CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[index];
    if (entry.m_component) return true;
    if (!CCharacterSelection::m_modelFrame || !CCharacterSelection::m_modelFrame->GetScene()) return false;

    CHARACTER_INFO& info = entry.m_characterInfo;
    CreatureModelDataRec* playerRec = GetModelDataRec(info.m_raceID, info.m_sexID);
    if (!playerRec || !playerRec->m_modelName) return false;

    const uint32_t started = OsGetAsyncTimeMs();
    // Logged BEFORE the build, so a crash while building this character names it (the v23 test crashed twice right after index 13, i.e. while building index 14).
    sExtensionMgr->Log("InnCast building index=%u race=%u sex=%u model=%s", index, static_cast<uint32_t>(info.m_raceID), static_cast<uint32_t>(info.m_sexID), playerRec->m_modelName);
    ComponentData data;
    data.m_preferences.m_raceID = info.m_raceID;
    data.m_preferences.m_sexID = info.m_sexID;
    data.m_preferences.m_classId = info.m_classID;
    data.m_preferences.m_skinID = info.m_skinColorID;
    data.m_preferences.m_faceId = info.m_faceID;
    data.m_preferences.m_hairStyleId = info.m_hairStyleID;
    data.m_preferences.m_hairColorId = info.m_hairColorID;
    data.m_preferences.m_facialHairId = info.m_facialHairStyleID;
    char retailPath[260];
    const char* uprightPath = Upright::ModelFor(info.m_raceID, info.m_sexID, info.m_skinColorID);
    const bool retailModel = !uprightPath && RetailRaceModelPath(info.m_raceID, info.m_sexID, retailPath, sizeof(retailPath), RetailRaceLookForm(info.m_guid.m_guid));
    data.m_model = CCharacterSelection::m_modelFrame->GetScene()->CreateModel(uprightPath ? uprightPath : retailModel ? retailPath : playerRec->m_modelName, 0);
    if (!data.m_model) return false;

    CCharacterComponent* component = CCharacterComponent::AllocComponent();
    if (!component) {
        data.m_model->Release();
        return false;
    }
    if (retailModel) data.m_model->WaitForLoad(nullptr);
    data.m_flags |= 0x2;
    component->Init(&data, nullptr);
    EsteriaSelectCharacter(component, info.m_guid.m_guid, info.m_skinColorID, info.m_faceID, info.m_hairStyleID, info.m_hairColorID, info.m_facialHairStyleID);

    CM2Model* model = component->m_data.m_model;
    if (!model || !model->m_shared) {
        sExtensionMgr->Log("InnCast build index=%u race=%u: the component has no usable model, skipped", index, static_cast<uint32_t>(info.m_raceID));
        return false;
    }
    model->m_lightingCallback = &CCharacterSelection::GenericLightingCallback;
    model->m_lightingArg = CCharacterSelection::m_modelFrame->m_lightArray2;
    model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    s_innDressSheathed = InnCastEnvFloat("WOW_INN_SHEATHE", 1.0f) > 0.5f;
    CCharacterSelection::DressComponent(info, component);
    s_innDressSheathed = false;

    if (info.m_flags & 0x2000) {
        CGlueMgr::AttachCharacterSelectVisuals(model);
    }
    AttachSelectWings(model, info.m_guid.m_guid);

    entry.m_component = component;
    CCharacterSelection::s_characterCount++;

    // Attach, place and prepare now (ShowCharacter's StartLoad does the same for the selected one).
    if (CCharacterSelection::m_modelFrame->m_model) {
        model->AttachToParent(CCharacterSelection::m_modelFrame->m_model, 0, nullptr, 0);
        C3Vector position(0.0f, 0.0f, 0.0f);
        CCharacterSelection::InnCastSlot(index, position);
        model->SetWorldTransform(position, 0.0f, InnCastScale(info.m_raceID, info.m_sexID));
        component->RenderPrep(1);
        model->IsDrawable(1, 1);
    }

    sExtensionMgr->Log("InnCast built index=%u race=%u elapsed=%u ms", index, info.m_raceID, OsGetAsyncTimeMs() - started);
    return true;
}

// Camera field order used by the creator camera: 2, 4, 5 copy, 3 near plane, 7 position, 8 target.
void InnCastCamera(float dtMs) {
    CSimpleModelFFX* frame = CCharacterSelection::m_modelFrame;
    if (!frame || !frame->m_model || !(frame->m_model->m_fFlags & 1) || !frame->m_model->m_shared->m_data->m_cameras.m_count) return;
    auto* base = static_cast<CCamera*>(frame->m_model->GetCameraByIndex(0));
    if (!base) return;

    const int32_t shown = std::min(InnCastShown(), InnCastLimit());
    const float spacing = InnCastEnvFloat("WOW_INN_CAST_SPACING", 1.4f);
    const int32_t perRow = std::max(1, static_cast<int32_t>(InnCastEnvFloat("WOW_INN_CAST_PER_ROW", 6.0f)));
    const int32_t cols = std::min(shown, perRow);
    const int32_t rows = (shown + perRow - 1) / perRow;
    const float width = std::max(0, cols - 1) * spacing;
    const float depth = std::max(0, rows - 1) * InnCastEnvFloat("WOW_INN_CAST_ROW_SPACING", 1.6f) * InnCastEnvFloat("WOW_INN_CAST_DEPTH_SIGN", 1.0f);

    C3Vector basePos, baseTgt;
    DataMgrGetCoord(base, 7, &basePos);
    DataMgrGetCoord(base, 8, &baseTgt);
    const C3Vector offset = basePos - baseTgt;

    // Overview: look at the middle of the group from further back the wider it is.
    // WOW_INN_CAST_OVERVIEW_PULL (default 3) multiplies that distance for the room view; _OVERVIEW_UP raises the look-at point (yards).
    const float pull = std::clamp((0.6f + width * 0.45f) * InnCastEnvFloat("WOW_INN_CAST_PULL", 1.0f) * InnCastEnvFloat("WOW_INN_CAST_OVERVIEW_PULL", 3.0f), 1.0f, 60.0f);
    C3Vector goalTgt = baseTgt;
    goalTgt.x += depth * 0.5f;
    goalTgt.z += InnCastEnvFloat("WOW_INN_CAST_OVERVIEW_UP", 0.0f);
    C3Vector goalPos = goalTgt + offset * pull;
    // Manual room view: WOW_INN_CAM_ABS=1 with WOW_INN_CAM_X/_Y/_Z (camera) and WOW_INN_CAM_TX/_TY/_TZ (look-at point), in scene coordinates
    // (the InnCast stats log line prints the cast and base camera positions to pick them from).
    const bool manualCamera = InnCastEnvFloat("WOW_INN_CAM_ABS", 0.0f) > 0.5f;
    C3Vector roomCamera = goalPos;
    if (manualCamera) {
        goalPos = C3Vector(InnCastEnvFloat("WOW_INN_CAM_X", goalPos.x), InnCastEnvFloat("WOW_INN_CAM_Y", goalPos.y), InnCastEnvFloat("WOW_INN_CAM_Z", goalPos.z));
        goalTgt = C3Vector(InnCastEnvFloat("WOW_INN_CAM_TX", goalTgt.x), InnCastEnvFloat("WOW_INN_CAM_TY", goalTgt.y), InnCastEnvFloat("WOW_INN_CAM_TZ", goalTgt.z));
        roomCamera = goalPos;
    }
    s_innBasePos = basePos;
    s_innBaseTgt = baseTgt;
    s_innOverviewPull = pull;
    float nearScale = 1.0f;

    // Zoom in when the selected character CHANGES (a click in the list); the selection made on entering the screen stays an overview.
    // Press Z to pull back to the room view. WOW_INN_CAST_START_ZOOMED=1 restores the old behaviour (always zoomed on the selection).
    const int32_t sel = CCharacterSelection::s_selectionIndex;
    // The game itself changes the selection right after entering (default character, then the last played one), so selection changes
    // during the first WOW_INN_CAST_GRACE_MS (3000) after entering do not count as clicks.
    const uint32_t camNow = OsGetAsyncTimeMs();
    const bool wasZoomed = s_innZoomed;
    if (s_innSeenSel == -2) {
        s_innZoomed = InnCastEnvFloat("WOW_INN_CAST_START_ZOOMED", 0.0f) > 0.5f;
        s_innCamEnter = camNow;
    } else if (sel != s_innSeenSel && camNow - s_innCamEnter >= static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_CAST_GRACE_MS", 3000.0f)))) {
        s_innZoomed = true;
    }
    s_innSeenSel = sel;
    const bool zDown = EventIsKeyDown(KEY_Z) != 0;
    if (sel != s_innReleasedSel) s_innReleasedSel = -3;
    if (zDown && !s_innZKeyDown) { s_innZoomed = false; s_innExplore = false; s_innReleasedSel = sel; }
    if (s_innZoomed && !wasZoomed) s_innExplore = false;
    if (s_innZoomed != wasZoomed) sExtensionMgr->Log("InnCast camera zoomed=%d selection=%d", s_innZoomed ? 1 : 0, sel);
    s_innZKeyDown = zDown;

    InnWmo::S().focusActive = false;     // v43: set again below while a character is zoomed on
    // Selected: its face, from a fraction of the base distance.
    const float zoom = InnCastEnvFloat("WOW_INN_CAST_ZOOM", 0.35f);
    if (s_innZoomed && zoom > 0.0f && sel >= 0 && sel < static_cast<int32_t>(CCharacterSelection::s_characterList.Count()) && !InnComing(sel)) {
        CCharacterComponent* component = CCharacterSelection::s_characterList[sel].m_component;
        CM2Model* body = component ? component->m_data.m_model : nullptr;
        if (body && (body->m_fFlags & 1)) {
            const auto& bounds = body->m_shared->m_data->m_bounds.m_extent;
            const float height = bounds.t.z - bounds.b.z;
            C3Vector face = body->HasBone(6) ? body->GetBonePivot(6) : C3Vector(0.0f, 0.0f, bounds.b.z + height * 0.87f);
            face.z += height * 0.035f;
            face = face * body->GetAnimatedWorldTransform();
            goalTgt = face;
            InnWmo::S().focus = face; InnWmo::S().focusActive = true;
            goalPos = face + offset * zoom;
            if (manualCamera) {
                // The base camera's direction (+x) can point out through the wall of the inn; with a manual room camera the zoom comes in from
                // the side the room camera is on (always inside the room), at the same distance.
                C3Vector toRoom = roomCamera - face;
                const float toRoomLen = std::sqrt(toRoom.x * toRoom.x + toRoom.y * toRoom.y + toRoom.z * toRoom.z);
                const float baseLen = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
                if (toRoomLen > 0.01f) goalPos = face + toRoom * (baseLen * zoom / toRoomLen);
            }
            nearScale = std::max(0.05f, zoom);

            // Whole body in frame (WOW_INN_CAST_FULLBODY=1, default): look at the middle of the body from far enough that its height fits the view.
            // WOW_INN_CAST_FRAME = margin (1.3 = 30% extra room), _FRAME_UP = raise the look-at point (fraction of the height), _FRAME_PITCH = how much of the
            // room camera's downward angle to keep (0.35), _ASPECT = window width/height (1.78).
            if (InnCastEnvFloat("WOW_INN_CAST_FULLBODY", 1.0f) > 0.5f) {
                const C44Matrix bodyMatrix = body->GetAnimatedWorldTransform();
                const float bodyScale = std::sqrt(bodyMatrix.a0 * bodyMatrix.a0 + bodyMatrix.a1 * bodyMatrix.a1 + bodyMatrix.a2 * bodyMatrix.a2);
                // The M2 bounding box covers every animation, wings, hair and tails, so for most races it is far taller than the body and the camera sat too
                // far away (dwarf, whose box is tight, framed well). The body height is estimated from the head bone (model origin = feet, the head bone pivot
                // is about 0.9 of the height); WOW_INN_CAST_FRAME_BONE=0 goes back to the bounding box. Per race: WOW_INN_CAST_FRAME_RACE_<raceId> (default 1.0)
                // multiplies the distance, live.
                float localBase = bounds.b.z, localHeight = height;
                bool boneFrame = false;
                if (InnCastEnvFloat("WOW_INN_CAST_FRAME_BONE", 1.0f) > 0.5f && body->HasBone(6)) {
                    const float headZ = body->GetBonePivot(6).z;
                    const float estimated = headZ * InnCastEnvFloat("WOW_INN_CAST_FRAME_HEAD", 1.12f);
                    if (std::isfinite(estimated) && estimated > 0.3f * height && estimated < height) { localBase = 0.0f; localHeight = estimated; boneFrame = true; }
                }
                // No usable head bone (Night/Blood/Void/Illidari elves, Nightborne, Ethereal, Kyrian ...): the bounding box is several times the body, so the body
                // height is box / divisor, feet at the model origin (so the camera aims at the body, not above it). Divisor 2.6 default,
                // WOW_INN_CAST_BOUNDS_DIV_RACE_<raceId> per race (live). WOW_INN_CAST_FRAME_BONE=0 keeps the plain box.
                if (!boneFrame && InnCastEnvFloat("WOW_INN_CAST_FRAME_BONE", 1.0f) > 0.5f) {
                    char divKey[48];
                    snprintf(divKey, sizeof(divKey), "WOW_INN_CAST_BOUNDS_DIV_RACE_%u", static_cast<uint32_t>(CCharacterSelection::s_characterList[sel].m_characterInfo.m_raceID));
                    localBase = 0.0f;
                    localHeight = height / std::max(1.0f, InnCastEnvFloat(divKey, 2.6f));
                }
                const float sceneHeight = std::max(0.05f, localHeight * bodyScale);
                const C3Vector center = C3Vector(0.0f, 0.0f, localBase + localHeight * (0.5f + InnCastEnvFloat("WOW_INN_CAST_FRAME_UP", 0.0f))) * bodyMatrix;
                C3Vector toCamera = goalPos - face;
                const float toCameraLen = std::sqrt(toCamera.x * toCamera.x + toCamera.y * toCamera.y + toCamera.z * toCamera.z);
                if (toCameraLen > 0.001f) {
                    toCamera = toCamera * (1.0f / toCameraLen);
                    toCamera.z *= InnCastEnvFloat("WOW_INN_CAST_FRAME_PITCH", 0.35f);
                    const float flat = std::sqrt(toCamera.x * toCamera.x + toCamera.y * toCamera.y + toCamera.z * toCamera.z);
                    if (flat > 0.001f) toCamera = toCamera * (1.0f / flat);
                    // Real shape of the model frame (the 3D view): the projection uses the diagonal convention tan(0.5 * fov / sqrt(1 + aspect^2)) (see
                    // CSimpleModel::RenderModelImpl). WOW_INN_CAST_ASPECT > 0 overrides the measured value.
                    float aspect = InnCastEnvFloat("WOW_INN_CAST_ASPECT", 0.0f);
                    if (aspect <= 0.0f) {
                        CRect frameRect = { 0.0f, 0.0f, 0.0f, 0.0f };
                        if (frame->GetRect(&frameRect) && frameRect.maxY > frameRect.minY && frameRect.maxX > frameRect.minX) aspect = (frameRect.maxX - frameRect.minX) / (frameRect.maxY - frameRect.minY);
                    }
                    aspect = std::clamp(aspect > 0.0f ? aspect : 1.78f, 0.5f, 6.0f);
                    const float tanVertical = std::tan(0.5f * CameraGetFloat(base, 4) / std::sqrt(1.0f + aspect * aspect));
                    char raceKey[48];
                    snprintf(raceKey, sizeof(raceKey), "WOW_INN_CAST_FRAME_RACE_%u", static_cast<uint32_t>(CCharacterSelection::s_characterList[sel].m_characterInfo.m_raceID));
                    const float distance = std::max(0.3f, 0.5f * sceneHeight * InnCastEnvFloat("WOW_INN_CAST_FRAME", 1.3f) * std::max(0.1f, InnCastEnvFloat(raceKey, 1.0f)) / std::max(0.05f, tanVertical));
                    // Keep the camera inside the hall: from the character, try the direction to the room camera first, then turn away in 10 degree steps
                    // (left/right alternately) until the camera position lies inside the hall's floor plan (WOW_INN_ZOOM_BOX_X0/_X1/_Y0/_Y1, WMO-local yards).
                    {
                        const InnWmo::Settings roomCfg = InnWmoSettings();
                        if (!roomCfg.path.empty() && InnWmo::S().ok) {
                            const C44Matrix roomPlacement = InnWmo::Placement(roomCfg, frame->m_model->m_worldTransform);
                            const C44Matrix roomInverse = roomPlacement.Inverse(roomPlacement.Determinant());
                            const float bx0 = InnCastEnvFloat("WOW_INN_ZOOM_BOX_X0", -5.0f), bx1 = InnCastEnvFloat("WOW_INN_ZOOM_BOX_X1", 4.9f);
                            const float by0 = InnCastEnvFloat("WOW_INN_ZOOM_BOX_Y0", -5.0f), by1 = InnCastEnvFloat("WOW_INN_ZOOM_BOX_Y1", 14.0f);
                            // The other characters on stage (a point at chest height), so the camera spot has a clear view of the selected one.
                            std::vector<C3Vector> others;
                            const int32_t castShown = InnCastShown();
                            for (int32_t i = 0; i < castShown; i++) {
                                if (i == sel) continue;
                                CCharacterComponent* otherComponent = CCharacterSelection::s_characterList[i].m_component;
                                CM2Model* otherBody = otherComponent ? otherComponent->m_data.m_model : nullptr;
                                if (!otherBody || !(otherBody->m_fFlags & 1)) continue;
                                const C44Matrix otherMatrix = otherBody->GetAnimatedWorldTransform();
                                others.push_back(C3Vector(otherMatrix.d0, otherMatrix.d1, otherMatrix.d2 + sceneHeight * 0.5f));
                            }
                            const float clearRadius = std::max(0.05f, InnCastEnvFloat("WOW_INN_ZOOM_CLEAR", 0.22f));
                            auto viewClear = [&](const C3Vector& cam) {
                                const C3Vector ray = center - cam;
                                const float rayLenSq = ray.x * ray.x + ray.y * ray.y + ray.z * ray.z;
                                for (const C3Vector& other : others) {
                                    const C3Vector toOther = other - cam;
                                    float t = rayLenSq > 0.000001f ? (toOther.x * ray.x + toOther.y * ray.y + toOther.z * ray.z) / rayLenSq : 0.0f;
                                    t = std::clamp(t, 0.0f, 1.0f);
                                    const C3Vector closest(cam.x + ray.x * t, cam.y + ray.y * t, cam.z + ray.z * t);
                                    const C3Vector gap = other - closest;
                                    if (gap.x * gap.x + gap.y * gap.y + gap.z * gap.z < clearRadius * clearRadius) return false;
                                }
                                return true;
                            };
                            auto insideHall = [&](const C3Vector& cam) {
                                const C3Vector local = cam * roomInverse;
                                return local.x >= bx0 && local.x <= bx1 && local.y >= by0 && local.y <= by1;
                            };
                            // First choice: inside the hall with a clear view; second: just inside the hall; else keep the direction to the room camera.
                            bool found = false;
                            C3Vector fallbackDirection = toCamera;
                            bool haveFallback = false;
                            float chosenAngle = 0.0f;
                            for (int32_t step = 0; step <= 18 && !found; step++) {
                                for (int32_t side = 0; side < 2 && !found; side++) {
                                    if (step == 0 && side == 1) continue;
                                    const float angle = (side == 0 ? 1.0f : -1.0f) * static_cast<float>(step) * 0.1745329f;
                                    const float ca = std::cos(angle), sa = std::sin(angle);
                                    const C3Vector turned(toCamera.x * ca - toCamera.y * sa, toCamera.x * sa + toCamera.y * ca, toCamera.z);
                                    const C3Vector candidate = center + turned * distance;
                                    if (!insideHall(candidate)) continue;
                                    if (viewClear(candidate)) { toCamera = turned; found = true; chosenAngle = angle; }
                                    else if (!haveFallback) { fallbackDirection = turned; haveFallback = true; chosenAngle = angle; }
                                }
                            }
                            if (!found && haveFallback) toCamera = fallbackDirection;
                            static int32_t loggedSel = -1;
                            static bool loggedZoom = false;
                            if (!loggedZoom || loggedSel != sel) {
                                loggedZoom = true; loggedSel = sel;
                                sExtensionMgr->Log("InnCast zoom race=%u aspect=%.2f tanV=%.3f height=%.2f (bounds %.2f, bone=%d) distance=%.2f angle=%.0f deg clearView=%d others=%u",
                                    static_cast<uint32_t>(CCharacterSelection::s_characterList[sel].m_characterInfo.m_raceID), aspect, tanVertical, sceneHeight, height * bodyScale, boneFrame ? 1 : 0, distance,
                                    chosenAngle * 57.29578f, found ? 1 : 0, static_cast<uint32_t>(others.size()));
                            }
                        }
                    }
                    goalTgt = center;
                    InnWmo::S().focus = center; InnWmo::S().focusActive = true;
                    goalPos = center + toCamera * distance;
                    nearScale = 0.5f;
                }
            }
        }
    }

    // Explore camera: the right button takes over from wherever the camera is; the view direction follows the mouse while it is held.
    if (InnCastEnvFloat("WOW_INN_EXPLORE", 1.0f) > 0.5f) {
        const bool rmb = EventIsButtonDown(MOUSE_BUTTON_RIGHT) != 0;
        int32_t mx = 0, my = 0;
        OsInputGetMousePosition(&mx, &my);
        if (rmb && !s_innRmbDown && s_innHaveCam) {
            C3Vector dir = s_innTgt - s_innPos;
            const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (len > 0.001f && std::isfinite(len)) {
                if (!s_innExplore) {
                    s_innExPos = s_innPos;
                    s_innExYaw = std::atan2(dir.y, dir.x);
                    s_innExPitch = std::asin(std::clamp(dir.z / len, -1.0f, 1.0f));
                    sExtensionMgr->Log("InnCast explore on");
                }
                s_innExplore = true;
                s_innZoomed = false;
            }
        }
        if (s_innExplore) {
            if (rmb && s_innRmbDown) {
                const float look = InnCastEnvFloat("WOW_INN_EXPLORE_LOOK", 0.004f);
                s_innExYaw -= static_cast<float>(mx - s_innLastMx) * look;
                s_innExPitch = std::clamp(s_innExPitch - static_cast<float>(my - s_innLastMy) * look, -1.45f, 1.45f);
            }
            const float cp = std::cos(s_innExPitch), sp = std::sin(s_innExPitch), cy = std::cos(s_innExYaw), sy = std::sin(s_innExYaw);
            const C3Vector fwd(cp * cy, cp * sy, sp);
            const C3Vector right(sy, -cy, 0.0f);
            const float speed = InnCastEnvFloat("WOW_INN_EXPLORE_SPEED", 2.0f) * (EventIsKeyDown(KEY_LSHIFT) ? 3.0f : 1.0f) * std::min(dtMs, 100.0f) * 0.001f;
            C3Vector move(0.0f, 0.0f, 0.0f);
            if (EventIsKeyDown(KEY_W)) move += fwd;
            if (EventIsKeyDown(KEY_S)) move -= fwd;
            if (EventIsKeyDown(KEY_D)) move += right;
            if (EventIsKeyDown(KEY_A)) move -= right;
            if (EventIsKeyDown(KEY_E)) move.z += 1.0f;
            if (EventIsKeyDown(KEY_Q)) move.z -= 1.0f;
            s_innExPos += move * speed;
            goalPos = s_innExPos;
            goalTgt = s_innExPos + fwd;
            nearScale = 1.0f;
            if (camNow - s_innExLog >= 1000 && (rmb || move.x != 0.0f || move.y != 0.0f || move.z != 0.0f)) {
                s_innExLog = camNow;
                sExtensionMgr->Log("InnCast explore cam=(%.2f %.2f %.2f) look=(%.2f %.2f %.2f)", s_innExPos.x, s_innExPos.y, s_innExPos.z, fwd.x, fwd.y, fwd.z);
            }
        }
        s_innRmbDown = rmb;
        s_innLastMx = mx;
        s_innLastMy = my;
    }

    const float tauMs = std::max(30.0f, InnCastEnvFloat("WOW_INN_CAST_TAU_MS", 180.0f));
    if (!s_innHaveCam) {
        s_innPos = goalPos;
        s_innTgt = goalTgt;
        s_innHaveCam = true;
    } else {
        const float s = 1.0f - std::exp(-dtMs / tauMs);
        s_innPos += (goalPos - s_innPos) * s;
        s_innTgt += (goalTgt - s_innTgt) * s;
    }

    const C3Vector& p = s_innPos;
    const C3Vector& t = s_innTgt;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || !std::isfinite(t.x) || !std::isfinite(t.y) || !std::isfinite(t.z)) return;
    if (!s_innCamera) s_innCamera = CameraCreate();
    if (!s_innCamera) return;
    for (uint32_t field : { 2u, 4u, 5u }) CameraSetFloat(s_innCamera, field, CameraGetFloat(base, field));
    CameraSetFloat(s_innCamera, 3, std::max(0.01f, CameraGetFloat(base, 3) * nearScale));
    CameraSetCoord(s_innCamera, 7, p);
    CameraSetCoord(s_innCamera, 8, t);
    if (frame->m_camera != s_innCamera) frame->SetCamera(s_innCamera);
}

void InnCastEmote(uint32_t now) {
    const int32_t sel = CCharacterSelection::s_selectionIndex;
    const int32_t count = static_cast<int32_t>(CCharacterSelection::s_characterList.Count());

    if (s_innEmoteIndex >= 0 && s_innEmoteIndex < count && (s_innEmoteIndex != sel || (!s_innEmoteIdled && now - s_innEmoteStart >= s_innEmoteMs))) {
        InnCastIdlePose(CCharacterSelection::s_characterList[s_innEmoteIndex].m_component);
        s_innEmoteIdled = true;
        if (s_innEmoteIndex != sel) s_innEmoteIndex = -1;
    }

    if (sel < 0 || sel >= count || s_innEmoteIndex == sel || InnComing(sel)) return;
    CCharacterComponent* component = CCharacterSelection::s_characterList[sel].m_component;
    CM2Model* model = component ? component->m_data.m_model : nullptr;
    if (!model || !(model->m_fFlags & 1)) return;

    // Verify the animation exists on this model; do not assume (ids: 66 bow, 113 salute, 67 wave).
    uint32_t sequence = 0;
    // Men salute the camera (113); women bow (66) or salute, picked by the click time (there is no separate curtsey animation); then bow / wave as fallbacks.
    const bool female = CCharacterSelection::s_characterList[sel].m_characterInfo.m_sexID == 1;
    const bool womanBows = ((now / 7u) & 1u) == 0u;
    const uint32_t maleOrder[4] = { 113u, 66u, 67u, 0u };
    const uint32_t womanBowOrder[4] = { 66u, 113u, 67u, 0u };
    const uint32_t womanSaluteOrder[4] = { 113u, 66u, 67u, 0u };
    const uint32_t* order = !female ? maleOrder : (womanBows ? womanBowOrder : womanSaluteOrder);
    for (int32_t k = 0; k < 3; k++) {
        if (model->HasSequence(order[k])) { sequence = order[k]; break; }
    }
    s_innEmoteIndex = sel;
    s_innEmoteStart = now;
    s_innEmoteIdled = false;
    s_innEmoteMs = static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_CAST_EMOTE_MS", 2400.0f)));
    sExtensionMgr->Log("InnCast emote index=%d race=%u sequence=%u", sel, CCharacterSelection::s_characterList[sel].m_characterInfo.m_raceID, sequence);
    if (sequence) {
        model->SetBoneSequence(0xFFFFFFFF, sequence, 0xFFFFFFFF, 0, 1.0f, 1, 1);
    } else {
        s_innEmoteMs = 0;
    }
}
// One late character per interval: builds the next list entry beyond the first WOW_INN_CAST_START, attaches it and flags it to enter through an entrance spot.
// Waits until the selected character is loaded, so the first group is up before more work starts.
void InnCastTrickle(uint32_t now) {
    CSimpleModelFFX* frame = CCharacterSelection::m_modelFrame;
    if (!frame || !frame->m_model || !(frame->m_model->m_fFlags & 1)) return;
    const int32_t shown = InnCastShown();
    if (!s_innPlanned) return;
    const int32_t listed = static_cast<int32_t>(CCharacterSelection::s_characterList.Count());
    const int32_t sel = CCharacterSelection::s_selectionIndex;
    if (sel >= 0 && sel < listed) {
        CCharacterComponent* selectedComponent = CCharacterSelection::s_characterList[sel].m_component;
        CM2Model* selectedModel = selectedComponent ? selectedComponent->m_data.m_model : nullptr;
        if (!selectedModel || !(selectedModel->m_fFlags & 1)) return;
    }
    const uint32_t interval = static_cast<uint32_t>(std::max(500.0f, InnCastEnvFloat("WOW_INN_CAST_TRICKLE_MS", 4000.0f)));
    if (s_innLastTrickle && now - s_innLastTrickle < interval) return;

    for (int32_t i = 0; i < shown && i < listed; i++) {
        if (i == sel || s_innTrickleFailed[i] || !s_innWanted[i] || s_innInitial[i] || s_innLeaving[i]) continue;
        CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[i];
        if (entry.m_component) continue;
        s_innLastTrickle = now;
        if (!InnCastBuild(static_cast<uint32_t>(i))) { s_innTrickleFailed[i] = true; return; }
        CM2Model* model = entry.m_component ? entry.m_component->m_data.m_model : nullptr;
        if (model) {
            if (model->m_attachParent != frame->m_model) model->AttachToParent(frame->m_model, 0, nullptr, 0);
            C3Vector position(0.0f, 0.0f, 0.0f);
            CCharacterSelection::InnCastSlot(static_cast<uint32_t>(i), position);
            model->SetWorldTransform(position, 0.0f, InnCastScale(entry.m_characterInfo.m_raceID, entry.m_characterInfo.m_sexID));
            s_innArrive[i] = true;
        }
        sExtensionMgr->Log("InnCast trickle: built index=%d (late or rotating-in character)", i);
        return;
    }
}

// v45: who walks out and who walks in (see the comment at s_innWanted). Every frame, before the walkers.
void InnCastRotate(uint32_t now) {
    if (!s_innPlanned) return;
    CSimpleModelFFX* frame = CCharacterSelection::m_modelFrame;
    if (!frame || !frame->m_model || !(frame->m_model->m_fFlags & 1)) return;
    const int32_t listed = InnCastShown();
    const int32_t capacity = std::min(listed, InnCastLimit());
    const int32_t sel = CCharacterSelection::s_selectionIndex;
    InnWalkers::State& ws = InnWalkers::S();

    // A click on a character that is not in the hall: it walks in from a door (the camera waits until it arrived).
    if (sel != s_innRotSeenSel) {
        s_innRotSeenSel = sel;
        const uint32_t grace = static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_CAST_GRACE_MS", 3000.0f)));
        if (sel >= 0 && sel < listed && now - s_innCamEnter >= grace) {
            if (s_innLeaving[sel]) { s_innLeaving[sel] = false; s_innWanted[sel] = true; }
            else if (!s_innPresent[sel] && !s_innArrive[sel]) {
                s_innWanted[sel] = true; s_innInitial[sel] = false; s_innArrive[sel] = true; s_innComing[sel] = true; s_innComeStart = now;
                s_innTrickleFailed[sel] = false;
                sExtensionMgr->Log("InnCast click on a character outside the hall: index=%d comes in through a door", sel);
            }
        }
    }
    if (sel >= 0 && sel < kInnMax && s_innComing[sel] && now - s_innComeStart > 45000u) s_innComing[sel] = false;

    int32_t wantedCount = 0, leavingCount = 0;
    for (int32_t i = 0; i < listed; i++) {
        if (s_innWanted[i]) wantedCount++;
        if (s_innLeaving[i]) {
            leavingCount++;
            if (s_innLeaveStart[i] && now - s_innLeaveStart[i] > 60000u) { s_innLeaving[i] = false; s_innWanted[i] = true; leavingCount--; wantedCount++; }    // could not reach a door: stays
        }
    }

    // v46: a character moved into the stay group comes in (or stops leaving); the others make room
    for (int32_t i = 0; i < listed; i++) {
        if (!s_innStay[i]) continue;
        if (s_innLeaving[i]) { s_innLeaving[i] = false; s_innWanted[i] = true; leavingCount--; wantedCount++; }
        else if (!s_innWanted[i] && !s_innTrickleFailed[i]) { s_innWanted[i] = true; s_innInitial[i] = false; wantedCount++; }
    }

    const uint32_t stayMinMs = static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_STAY_MIN", 60.0f)) * 1000.0f);
    auto pickLeaver = [&]() -> int32_t {
        int32_t pool[kInnMax]; int32_t n = 0;
        for (int32_t i = 0; i < listed; i++) {
            if (s_innWanted[i] && !s_innStay[i] && i != sel && !s_innLeaving[i] && s_innPresent[i] && !s_innArrive[i] && !s_innComing[i]
                && !(s_innSince[i] && now - s_innSince[i] < stayMinMs)) pool[n++] = i;     // v48: new arrivals stay at least WOW_INN_STAY_MIN seconds
        }
        return n > 0 ? pool[InnWalkers::Rand(ws) % static_cast<uint32_t>(n)] : -1;
    };
    auto startLeave = [&](int32_t i) {
        s_innLeaving[i] = true; s_innWanted[i] = false; s_innInitial[i] = false; s_innLeaveStart[i] = now ? now : 1;
        sExtensionMgr->Log("InnCast leaving: index=%d heads for a door", i);
    };

    if (leavingCount != 0) return;
    if (wantedCount > capacity) {                 // somebody clicked came in: one of the others walks out
        const int32_t out = pickLeaver();
        if (out >= 0) startLeave(out);
        return;
    }
    if (listed <= capacity || wantedCount != capacity || now < s_innNextRotate) return;

    // everybody who should be in the hall is built (or failed) before the next swap
    for (int32_t i = 0; i < listed; i++) {
        if (s_innWanted[i] && i != sel && !s_innTrickleFailed[i] && !CCharacterSelection::s_characterList[i].m_component) return;
        if (s_innWanted[i] && !s_innPresent[i] && i != sel && !s_innTrickleFailed[i]) return;
    }
    const float rotMin = std::max(5.0f, InnCastEnvFloat("WOW_INN_ROTATE_MIN", 30.0f));
    const float rotMax = std::max(rotMin, InnCastEnvFloat("WOW_INN_ROTATE_MAX", 45.0f));
    const float roll = static_cast<float>(InnWalkers::Rand(ws) & 0xFFFF) / 65535.0f;
    s_innNextRotate = now + static_cast<uint32_t>((rotMin + (rotMax - rotMin) * roll) * 1000.0f);
    const int32_t leaver = pickLeaver();
    int32_t arriver = -1;
    for (int32_t i = 0; i < listed; i++) {
        if (s_innWanted[i] || s_innLeaving[i] || s_innTrickleFailed[i] || i == sel || CCharacterSelection::s_characterList[i].m_component) continue;
        if (arriver < 0 || s_innLastLeft[i] < s_innLastLeft[arriver]) arriver = i;
    }
    if (leaver < 0 || arriver < 0) return;
    startLeave(leaver);
    s_innWanted[arriver] = true; s_innInitial[arriver] = false;
    sExtensionMgr->Log("InnCast swap: index=%d walks out, index=%d walks in", leaver, arriver);
}

// Step 3.3: the cast walks around the inn (InnWalkers.h is pure logic; this applies its result). Every frame, before the greeting emote.
// Settings (InnCast.cfg, live): WOW_INN_WALKERS (1; 0 = the old standing line-up), WOW_INN_WALK_SPEED (yards/s, 2.2), WOW_INN_WALK_PAUSE_MIN/_MAX (2/8 s),
// WOW_INN_WALK_WANDER_W / _STAND_W (26/26: weight of a short walk vs standing still), WOW_INN_WALK_SPREAD (3.6 yd preferred gap to others), WOW_INN_WALK_RANGE (9 yd, short walks stay near),
// WOW_INN_WALK_MAX (most characters walking at once, 0 = automatic), WOW_INN_WALK_LONG_MIN/_MAX (35/90 s, drinking and leaning), WOW_INN_WALK_AWAY_MIN/_MAX (25/70 s).
void InnCastWalkers(float dtMs) {
    if (InnCastEnvFloat("WOW_INN_WALKERS", 1.0f) < 0.5f) return;
    CSimpleModelFFX* frame = CCharacterSelection::m_modelFrame;
    if (!frame || !frame->m_model || !(frame->m_model->m_fFlags & 1)) return;
    const InnWmo::Settings cfg = InnWmoSettings();
    if (cfg.path.empty() || !InnWmo::S().ok) return;
    const uint32_t now = OsGetAsyncTimeMs();
    InnWalk::Reload(now);
    const InnWalk::State& graph = InnWalk::S();
    const int32_t shown = InnCastShown();
    if (shown <= 0) return;

    static CM2Model* cachedModel[kInnMax] = {};
    static uint32_t cachedMask[kInnMax] = {};
    static bool cachedWalk[kInnMax] = {};
    static bool cachedRun[kInnMax] = {};
    static uint32_t cachedSequences[kInnMax] = {};
    std::vector<InnWalkers::CharIn> in(static_cast<size_t>(shown));
    for (int32_t i = 0; i < shown; i++) {
        CCharacterComponent* component = CCharacterSelection::s_characterList[i].m_component;
        CM2Model* model = component ? component->m_data.m_model : nullptr;
        if (!model || !(model->m_fFlags & 1) || model->m_attachParent != frame->m_model) continue;
        // A retail model reports its full sequence table only after the skeleton is merged (the human showed 262 sequences when first loaded, 406 later,
        // lean included), so the animation mask is rebuilt whenever the model or its sequence count changes.
        const uint32_t sequenceCount = (model->m_shared && model->m_shared->m_data) ? model->m_shared->m_data->m_sequences.m_count : 0;
        if (s_innDropCache[i] || cachedModel[i] != model || cachedSequences[i] != sequenceCount) {
            s_innDropCache[i] = false;
            cachedModel[i] = model;
            cachedSequences[i] = sequenceCount;
            cachedWalk[i] = model->HasSequence(static_cast<uint32_t>(InnWalkers::kAnimWalk)) != 0;
            cachedRun[i] = model->HasSequence(static_cast<uint32_t>(InnWalkers::kAnimRun)) != 0;
            cachedMask[i] = 0;
            for (int32_t k = 0; k < InnWalkers::kEmoteCount; k++) {
                if (model->HasSequence(static_cast<uint32_t>(InnWalkers::kEmoteIds[k]))) cachedMask[i] |= (1u << k);
            }
        }
        in[i].ready = true;
        in[i].arrive = s_innArrive[i];
        in[i].coming = s_innComing[i];
        in[i].leave = s_innLeaving[i];
        in[i].hasRun = cachedRun[i];
        in[i].hasWalk = cachedWalk[i] && InnCastEnvFloat("WOW_INN_ANIM_TEST", 0.0f) < 0.5f;
        in[i].emoteMask = cachedMask[i];
    }

    // the camera in WMO-local yards (the selected character turns to it)
    const C44Matrix placement = InnWmo::Placement(cfg, frame->m_model->m_worldTransform);
    C44Matrix local;
    local.RotateAroundZ(cfg.yaw);
    local.Scale(cfg.scale);
    local.d0 = cfg.x; local.d1 = cfg.y; local.d2 = cfg.z;
    float camX = 100.0f, camY = 0.0f;
    if (s_innHaveCam) {
        const C44Matrix inverse = placement.Inverse(placement.Determinant());
        const C3Vector camLocal = s_innPos * inverse;
        if (std::isfinite(camLocal.x) && std::isfinite(camLocal.y)) { camX = camLocal.x; camY = camLocal.y; }
    }

    InnWalkers::Config walkCfg;
    walkCfg.speed = 2.2f * std::max(0.1f, InnCastEnvFloat("WOW_INN_WALK_SPEED", 1.0f));
    walkCfg.pauseMin = InnCastEnvFloat("WOW_INN_WALK_PAUSE_MIN", 2.0f);
    walkCfg.pauseMax = InnCastEnvFloat("WOW_INN_WALK_PAUSE_MAX", 8.0f);
    walkCfg.maxWalking = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_WALK_MAX", 0.0f));
    walkCfg.longMin = InnCastEnvFloat("WOW_INN_WALK_LONG_MIN", 35.0f);
    walkCfg.longMax = InnCastEnvFloat("WOW_INN_WALK_LONG_MAX", 90.0f);
    walkCfg.awayMin = InnCastEnvFloat("WOW_INN_WALK_AWAY_MIN", 25.0f);
    walkCfg.awayMax = InnCastEnvFloat("WOW_INN_WALK_AWAY_MAX", 70.0f);
    walkCfg.wanderWeight = std::max(0.0f, InnCastEnvFloat("WOW_INN_WALK_WANDER_W", 26.0f));
    walkCfg.standWeight = std::max(0.0f, InnCastEnvFloat("WOW_INN_WALK_STAND_W", 26.0f));
    walkCfg.spread = std::max(0.0f, InnCastEnvFloat("WOW_INN_WALK_SPREAD", 3.6f));
    walkCfg.range = std::max(1.0f, InnCastEnvFloat("WOW_INN_WALK_RANGE", 9.0f));
    walkCfg.comeWalkSec = std::max(0.0f, InnCastEnvFloat("WOW_INN_COME_WALK_SEC", 3.0f));
    walkCfg.runSpeedMult = std::max(1.0f, InnCastEnvFloat("WOW_INN_RUN_MULT", 2.0f));

    static std::vector<InnWalkers::CharOut> out;
    InnWalkers::Update(dtMs * 0.001f, walkCfg, graph, in, (CCharacterSelection::s_selectionIndex == s_innReleasedSel) ? -1 : static_cast<int32_t>(CCharacterSelection::s_selectionIndex), camX, camY, out);
    for (int32_t i = 0; i < shown && i < static_cast<int32_t>(InnWalkers::S().walkers.size()); i++) {
        const InnWalkers::Walker& placedWalker = InnWalkers::S().walkers[i];
        { const bool wasPresent = s_innPresent[i]; s_innPresent[i] = placedWalker.placed; if (!wasPresent && placedWalker.placed && !s_innInitial[i]) s_innSince[i] = now ? now : 1; }     // v48: arrival time of late characters
        if (s_innArrive[i] && placedWalker.placed) s_innArrive[i] = false;     // placed at its entrance (or anywhere): the flag is done
        if (s_innComing[i] && placedWalker.placed && !placedWalker.coming) s_innComing[i] = false;     // arrived: the camera may zoom and the greeting plays
        if (i < static_cast<int32_t>(out.size()) && out[i].gone) InnCastRemove(i, now);
    }
    const int32_t testId = static_cast<int32_t>(InnCastEnvFloat("WOW_INN_ANIM_TEST", 0.0f));

    for (int32_t i = 0; i < shown && i < static_cast<int32_t>(out.size()); i++) {
        const InnWalkers::CharOut& o = out[i];
        if (!o.valid || !in[i].ready) continue;
        CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[i];
        if (!entry.m_component) continue;     // v47: freed this frame (walked out through a door)
        CM2Model* model = entry.m_component->m_data.m_model;
        // Seated on a chair or stool: sideways correction per race (WOW_INN_SIT_SIDE = all races, WOW_INN_SIT_SIDE_<raceId> = that race, added; yards,
        // positive = toward the character's own right). Some races' sit animation sits the body further left than the chair seat (worgen, race 22).
        float sideX = 0.0f, sideY = 0.0f;
        {
            const InnWalkers::State& ws = InnWalkers::S();
            if (i < static_cast<int32_t>(ws.walkers.size())) {
                const InnWalkers::Walker& w = ws.walkers[i];
                if (w.placed && w.mode == InnWalkers::M_Long && w.spotKind == InnWalkers::K_Sit && w.node >= 0 && w.node < static_cast<int32_t>(ws.nodes.size()) && ws.nodes[w.node].anim != 97) {
                    char key[48];
                    snprintf(key, sizeof(key), "WOW_INN_SIT_SIDE_%u", static_cast<uint32_t>(entry.m_characterInfo.m_raceID));
                    const float raceDefault = entry.m_characterInfo.m_raceID == 22 ? 0.6f : 0.0f;     // worgen starts at 0.6 yd, tune live
                    const float side = InnCastEnvFloat("WOW_INN_SIT_SIDE", 0.0f) + InnCastEnvFloat(key, raceDefault);
                    sideX = std::sin(o.heading) * side; sideY = -std::cos(o.heading) * side;
                }
            }
        }
        const C3Vector at = C3Vector(o.x + sideX, o.y + sideY, o.z) * local;
        C44Matrix matrix;
        matrix.RotateAroundZ(o.heading + cfg.yaw + (i == CCharacterSelection::s_selectionIndex ? CCharacterSelection::s_charFacing : 0.0f));     // v49: plus the mouse rotation
        matrix.Scale(InnCastScale(entry.m_characterInfo.m_raceID, entry.m_characterInfo.m_sexID));
        matrix.d0 = at.x; matrix.d1 = at.y; matrix.d2 = at.z;
        model->SetWorldTransform(matrix);
        if (o.animChanged && o.anim >= 0 && testId <= 0) model->SetBoneSequence(0xFFFFFFFF, static_cast<uint32_t>(o.anim), 0xFFFFFFFF, 0, 1.0f, 1, 1);
    }

    // A selected character beyond the shown cast (list entry >= N) is not a walker: it stands at the stage point of the hall, facing the camera
    // (WOW_INN_STAGE_X/_Y in WMO-local yards, default the old stage 2.2 / -0.9). Without this it kept the old line-up slot, now an awkward spot by the wall.
    {
        const int32_t sel = CCharacterSelection::s_selectionIndex;
        if (sel >= shown && sel < static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) {
            CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[sel];
            CM2Model* model = entry.m_component ? entry.m_component->m_data.m_model : nullptr;
            if (model && (model->m_fFlags & 1) && model->m_attachParent == frame->m_model) {
                const float stageX = InnCastEnvFloat("WOW_INN_STAGE_X", 2.2f), stageY = InnCastEnvFloat("WOW_INN_STAGE_Y", -0.9f);
                const C3Vector at = C3Vector(stageX, stageY, graph.floorZ) * local;
                C44Matrix matrix;
                matrix.RotateAroundZ(std::atan2(camY - stageY, camX - stageX) + cfg.yaw + CCharacterSelection::s_charFacing);     // v49: plus the mouse rotation
                matrix.Scale(InnCastScale(entry.m_characterInfo.m_raceID, entry.m_characterInfo.m_sexID));
                matrix.d0 = at.x; matrix.d1 = at.y; matrix.d2 = at.z;
                model->SetWorldTransform(matrix);
            }
        }
    }

    // Animation test (WOW_INN_ANIM_TEST=<id> in InnCast.cfg, live): every cast member whose model has that sequence plays it, standing still; 0 = off.
    // Used to find out which retail sequence id is the /lean emote and what it looks like per race.
    static int32_t testApplied[24] = {};
    static CM2Model* testModel[24] = {};
    for (int32_t i = 0; i < shown && i < 24; i++) {
        if (!in[i].ready) { testApplied[i] = 0; testModel[i] = nullptr; continue; }
        CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[i];
        CM2Model* model = entry.m_component ? entry.m_component->m_data.m_model : nullptr;     // v47: a character that just walked out has no component this frame
        if (!model) { testApplied[i] = 0; testModel[i] = nullptr; continue; }
        if (testId > 0) {
            if (testApplied[i] != testId || testModel[i] != model) {
                testApplied[i] = testId; testModel[i] = model;
                const bool has = model->HasSequence(static_cast<uint32_t>(testId)) != 0;
                if (has) model->SetBoneSequence(0xFFFFFFFF, static_cast<uint32_t>(testId), 0xFFFFFFFF, 0, 1.0f, 1, 1);
                sExtensionMgr->Log("InnAnimTest id=%d index=%d race=%u sex=%u: %s", testId, i, static_cast<uint32_t>(entry.m_characterInfo.m_raceID), static_cast<uint32_t>(entry.m_characterInfo.m_sexID), has ? "playing" : "model has no such sequence");
            }
        } else if (testApplied[i] > 0) {
            testApplied[i] = 0;
            if (i < static_cast<int32_t>(InnWalkers::S().walkers.size())) InnWalkers::S().walkers[i].anim = -1;   // the walker sends its animation again
        }
    }

    // Animation stepper (WOW_INN_ANIM_STEP=1 in InnCast.cfg, live): the SELECTED character plays the sequences it has from id 144 up, one at a time.
    // X = next, C = previous, V = back to standing. Every change logs `InnAnimStep id=<n> (<position>/<count>) race=..`: when the lean pose shows,
    // the last logged id is the one. Works for a selected character outside the shown cast too (it stands in front of the group).
    {
        static std::vector<uint32_t> stepIds;
        static size_t stepPos = 0;
        static CM2Model* stepModel = nullptr;
        static bool stepWasOn = false, xDown = false, cDown = false, vDown = false, bDown = false, autoHold = false;
        static float autoTimer = 0.0f;
        const bool stepOn = InnCastEnvFloat("WOW_INN_ANIM_STEP", 0.0f) > 0.5f;
        const int32_t selectedIndex = CCharacterSelection::s_selectionIndex;
        CCharacterComponent* selComponent = (selectedIndex >= 0 && selectedIndex < static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) ? CCharacterSelection::s_characterList[selectedIndex].m_component : nullptr;
        CM2Model* selModel = selComponent ? selComponent->m_data.m_model : nullptr;
        if (stepOn && selModel && (selModel->m_fFlags & 1)) {
            const CHARACTER_INFO& selInfo = CCharacterSelection::s_characterList[selectedIndex].m_characterInfo;
            bool apply = false;
            if (stepModel != selModel) {
                stepModel = selModel;
                stepIds.clear();
                const uint32_t fromId = static_cast<uint32_t>(std::max(0.0f, InnCastEnvFloat("WOW_INN_ANIM_FROM", 144.0f)));
                const uint32_t toId = static_cast<uint32_t>(std::min(2999.0f, std::max(0.0f, InnCastEnvFloat("WOW_INN_ANIM_TO", 2999.0f))));
                for (uint32_t id = fromId; id <= toId; id++) if (selModel->HasSequence(id)) stepIds.push_back(id);
                stepPos = 0;
                apply = true;
                sExtensionMgr->Log("InnAnimStep list for index=%d race=%u sex=%u: %u sequences from id 144 (X next, C previous, V stand)", selectedIndex, static_cast<uint32_t>(selInfo.m_raceID), static_cast<uint32_t>(selInfo.m_sexID), static_cast<uint32_t>(stepIds.size()));
            }
            const bool x = EventIsKeyDown(KEY_X) != 0, c = EventIsKeyDown(KEY_C) != 0, v = EventIsKeyDown(KEY_V) != 0;
            if (!stepIds.empty()) {
                if (x && !xDown) { stepPos = (stepPos + 1) % stepIds.size(); apply = true; }
                if (c && !cDown) { stepPos = (stepPos + stepIds.size() - 1) % stepIds.size(); apply = true; }
            }
            if (v && !vDown) { selModel->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1); sExtensionMgr->Log("InnAnimStep stand"); }
            // WOW_INN_ANIM_AUTO=<seconds>: step forward by itself every N seconds; B holds / releases it (log says which id it stopped on).
            const bool b = EventIsKeyDown(KEY_B) != 0;
            if (b && !bDown) {
                autoHold = !autoHold;
                sExtensionMgr->Log("InnAnimStep auto %s at id=%u", autoHold ? "HELD" : "running", stepIds.empty() ? 0u : stepIds[stepPos]);
            }
            const float autoSeconds = InnCastEnvFloat("WOW_INN_ANIM_AUTO", 0.0f);
            if (autoSeconds > 0.0f && !autoHold && !stepIds.empty()) {
                autoTimer += dtMs * 0.001f;
                if (autoTimer >= autoSeconds) { autoTimer = 0.0f; stepPos = (stepPos + 1) % stepIds.size(); apply = true; }
            }
            bDown = b;
            xDown = x; cDown = c; vDown = v;
            if (apply && !stepIds.empty()) {
                selModel->SetBoneSequence(0xFFFFFFFF, stepIds[stepPos], 0xFFFFFFFF, 0, 1.0f, 1, 1);
                sExtensionMgr->Log("InnAnimStep id=%u (%u/%u) race=%u sex=%u", stepIds[stepPos], static_cast<uint32_t>(stepPos + 1), static_cast<uint32_t>(stepIds.size()), static_cast<uint32_t>(selInfo.m_raceID), static_cast<uint32_t>(selInfo.m_sexID));
            }
            stepWasOn = true;
        } else if (stepWasOn) {
            stepWasOn = false;
            stepModel = nullptr;
            if (selModel && (selModel->m_fFlags & 1)) selModel->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
        }
    }

    static uint32_t lastLog = 0;
    if (now - lastLog >= 5000) {
        lastLog = now;
        const InnWalkers::Stats& st = InnWalkers::S().stats;
        sExtensionMgr->Log("InnWalkers: walking=%d standing=%d long-stay=%d chatting=%d selected=%d (nodes=%u spots=%u)", st.walking, st.paused, st.longStay, st.chatting, st.selected,
            static_cast<uint32_t>(graph.waypoints.size()), static_cast<uint32_t>(graph.spots.size()));
        // who is where on a seat / in the circle / leaning (node id, x, y in WMO-local yards), so seat positions can be checked against the log
        char spots[512]; spots[0] = 0;
        size_t used = 0; int32_t sitting = 0, circling = 0, leaning = 0;
        const InnWalkers::State& ws = InnWalkers::S();
        for (const InnWalkers::Walker& w : ws.walkers) {
            if (!w.placed || w.mode != InnWalkers::M_Long || w.node < 0 || w.node >= static_cast<int32_t>(ws.nodes.size())) continue;
            const char* tag = w.spotKind == InnWalkers::K_Sit ? "sit" : (w.spotKind == InnWalkers::K_Circle ? "circle" : (w.spotKind == InnWalkers::K_Lean ? "lean" : nullptr));
            if (!tag) continue;
            if (w.spotKind == InnWalkers::K_Sit) sitting++; else if (w.spotKind == InnWalkers::K_Circle) circling++; else leaning++;
            if (used + 48 < sizeof(spots)) used += static_cast<size_t>(snprintf(spots + used, sizeof(spots) - used, " %s%d@%.2f,%.2f", tag, ws.nodes[w.node].id, w.x, w.y));
        }
        sExtensionMgr->Log("InnWalkers spots: sitting=%d circle=%d leaning=%d:%s", sitting, circling, leaning, spots);
        // every placed walker: list index, mode, animation id, node it stands at / last left -> node reserved, position (to match anything that looks wrong, e.g. a sit pose away from a seat)
        char walks[1400]; walks[0] = 0;
        size_t wused = 0;
        for (size_t wi = 0; wi < ws.walkers.size(); wi++) {
            const InnWalkers::Walker& w = ws.walkers[wi];
            if (!w.placed) continue;
            const int32_t from = (w.node >= 0 && w.node < static_cast<int32_t>(ws.nodes.size())) ? ws.nodes[w.node].id : -1;
            const int32_t to = (w.reserved >= 0 && w.reserved < static_cast<int32_t>(ws.nodes.size())) ? ws.nodes[w.reserved].id : -1;
            if (wused + 64 < sizeof(walks)) wused += static_cast<size_t>(snprintf(walks + wused, sizeof(walks) - wused, " %u:m%d/a%d/%d>%d@%.1f,%.1f", static_cast<unsigned>(wi), static_cast<int>(w.mode), static_cast<int>(w.anim), from, to, w.x, w.y));
        }
        if (wused) sExtensionMgr->Log("InnWalkers census:%s", walks);
    }
}

}  // namespace


// ---- v46: the owner's own order of the character list (drag a name in the list; the first WOW_INN_STAY_FIRST always stay in the hall). Saved by character
// name in InnCharOrder.cfg (one lower-case name per line); applied whenever the list is loaded. Names not in the file keep the server order, after the known ones. ----
static std::string InnCastNameKey(const char* name) {
    std::string key;
    for (const char* p = name; p && *p; p++) key += static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return key;
}

static std::vector<std::string> InnCastReadOrderFile() {
    std::vector<std::string> order;
    std::ifstream file("InnCharOrder.cfg");
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) order.push_back(line);
    }
    return order;
}

static void InnCastSaveOrder() {
    const int32_t listed = static_cast<int32_t>(CCharacterSelection::s_characterList.Count());
    std::vector<std::string> current;
    for (int32_t i = 0; i < listed; i++) current.push_back(InnCastNameKey(CCharacterSelection::s_characterList[i].m_characterInfo.m_name));
    const std::vector<std::string> previous = InnCastReadOrderFile();
    std::ofstream file("InnCharOrder.cfg", std::ios::trunc);
    if (!file) { sExtensionMgr->Log("InnCast order: could not write InnCharOrder.cfg"); return; }
    for (const std::string& name : current) file << name << "\n";
    for (const std::string& name : previous) if (std::find(current.begin(), current.end(), name) == current.end()) file << name << "\n";     // other accounts / realms keep their place
    sExtensionMgr->Log("InnCast order saved: %d names", listed);
}

// Called right after the server list was copied into s_characterList (no models exist yet): sorts it by the saved order.
static void InnCastApplySavedOrder() {
    const uint32_t count = CCharacterSelection::s_characterList.Count();
    if (count < 2) return;
    const std::vector<std::string> order = InnCastReadOrderFile();
    if (order.empty()) return;
    std::vector<std::pair<int32_t, int32_t>> rank;     // (place in the file or after it, server index)
    for (uint32_t i = 0; i < count; i++) {
        const std::string key = InnCastNameKey(CCharacterSelection::s_characterList[i].m_characterInfo.m_name);
        const auto found = std::find(order.begin(), order.end(), key);
        const int32_t place = found != order.end() ? static_cast<int32_t>(found - order.begin()) : static_cast<int32_t>(order.size()) + static_cast<int32_t>(i);
        rank.push_back(std::make_pair(place, static_cast<int32_t>(i)));
    }
    std::stable_sort(rank.begin(), rank.end(), [](const std::pair<int32_t, int32_t>& a, const std::pair<int32_t, int32_t>& b) { return a.first < b.first; });
    std::vector<CHARACTER_INFO> copy(count);
    for (uint32_t i = 0; i < count; i++) memcpy(&copy[i], &CCharacterSelection::s_characterList[i].m_characterInfo, sizeof(CHARACTER_INFO));
    for (uint32_t k = 0; k < count; k++) memcpy(&CCharacterSelection::s_characterList[k].m_characterInfo, &copy[static_cast<size_t>(rank[k].second)], sizeof(CHARACTER_INFO));
    sExtensionMgr->Log("InnCast order applied to %u characters", count);
}

// Two neighbouring list entries swap places, with everything that is keyed by list index (model, pet, selection, inn state, walkers).
static void InnCastSwapEntries(int32_t a, int32_t b) {
    CharacterSelectionDisplay& x = CCharacterSelection::s_characterList[static_cast<uint32_t>(a)];
    CharacterSelectionDisplay& y = CCharacterSelection::s_characterList[static_cast<uint32_t>(b)];
    CHARACTER_INFO temp;
    memcpy(&temp, &x.m_characterInfo, sizeof(CHARACTER_INFO));
    memcpy(&x.m_characterInfo, &y.m_characterInfo, sizeof(CHARACTER_INFO));
    memcpy(&y.m_characterInfo, &temp, sizeof(CHARACTER_INFO));
    std::swap(x.m_component, y.m_component);
    std::swap(x.m_petModel, y.m_petModel);
    std::swap(x.m_petAlpha, y.m_petAlpha);
    auto map = [&](int32_t v) { return v == a ? b : (v == b ? a : v); };
    CCharacterSelection::s_selectionIndex = map(CCharacterSelection::s_selectionIndex);
    CGlueMgr::s_displayedCharacterIndex = map(CGlueMgr::s_displayedCharacterIndex);
    if (g_lastCharacterIndexVar) g_lastCharacterIndexVar->m_intValue = map(g_lastCharacterIndexVar->m_intValue);
    s_innSeenSel = map(s_innSeenSel);
    s_innRotSeenSel = map(s_innRotSeenSel);
    s_innReleasedSel = map(s_innReleasedSel);
    s_innEmoteIndex = map(s_innEmoteIndex);
    if (a < kInnMax && b < kInnMax) {
        std::swap(s_innAnimLogged[a], s_innAnimLogged[b]); std::swap(s_innArrive[a], s_innArrive[b]); std::swap(s_innTrickleFailed[a], s_innTrickleFailed[b]);
        std::swap(s_innWanted[a], s_innWanted[b]); std::swap(s_innInitial[a], s_innInitial[b]); std::swap(s_innLeaving[a], s_innLeaving[b]);
        std::swap(s_innComing[a], s_innComing[b]); std::swap(s_innPresent[a], s_innPresent[b]); std::swap(s_innDropCache[a], s_innDropCache[b]);
        std::swap(s_innLastLeft[a], s_innLastLeft[b]); std::swap(s_innLeaveStart[a], s_innLeaveStart[b]); std::swap(s_innSince[a], s_innSince[b]);
        s_innDropCache[a] = s_innDropCache[b] = true;
    }
    InnWalkers::SwapIndex(a, b);
}

// MoveCharacter(from, to) from the character list (0-based here). Returns false when nothing moved. *selectedAfter = the selected character's new 0-based index.
bool InnCastMoveEntry(int32_t from, int32_t to, int32_t* selectedAfter) {
    const int32_t count = static_cast<int32_t>(CCharacterSelection::s_characterList.Count());
    if (from < 0 || to < 0 || from >= count || to >= count || from == to) return false;
    const int32_t step = to > from ? 1 : -1;
    for (int32_t k = from; k != to; k += step) InnCastSwapEntries(k, k + step);
    if (s_innPlanned) InnCastComputeStay(std::min(count, kInnMax));
    InnCastSaveOrder();
    if (selectedAfter) *selectedAfter = CCharacterSelection::s_selectionIndex;
    sExtensionMgr->Log("InnCast order: moved list entry %d to %d", from + 1, to + 1);
    return true;
}

// Position of list entry `index` in the cast (attachment space of the background model). False (and untouched) when the cast is off.
// v49: the mouse rotation (right-drag) of the selected character. With the inn cast on the character is driven by its walker, so the old behaviour (put the model on its
// line-up slot and turn it) made it jump to the stage. Now only the angle is kept (s_charFacing, reset to 0 at every selection) and it is added to the facing below.
bool CCharacterSelection::InnCastUserFacing(float angle) {
    if (InnCastShown() <= 0) return false;
    CCharacterSelection::s_charFacing = angle;
    return true;
}

bool CCharacterSelection::InnCastSlot(uint32_t index, C3Vector& position) {
    const int32_t shown = std::min(InnCastShown(), InnCastLimit());
    if (shown <= 0) return false;

    const float spacing = InnCastEnvFloat("WOW_INN_CAST_SPACING", 1.4f);
    const float rowSpacing = InnCastEnvFloat("WOW_INN_CAST_ROW_SPACING", 1.6f) * InnCastEnvFloat("WOW_INN_CAST_DEPTH_SIGN", 1.0f);
    const int32_t perRow = std::max(1, static_cast<int32_t>(InnCastEnvFloat("WOW_INN_CAST_PER_ROW", 6.0f)));

    // A selected character beyond the first N stands in front of the group.
    if (static_cast<int32_t>(index) >= shown) {
        position.x = -rowSpacing;
        position.y = 0.0f;
        return true;
    }

    const int32_t row = static_cast<int32_t>(index) / perRow;
    const int32_t col = static_cast<int32_t>(index) % perRow;
    const int32_t inRow = std::min(perRow, shown - row * perRow);
    position.x = row * rowSpacing;
    position.y = (col - (inRow - 1) * 0.5f) * spacing;
    return true;
}

// After ShowCharacter has attached the selected character: build, attach and place the rest of the cast.
void CCharacterSelection::InnCastShow() {
    const int32_t shown = InnCastShown();
    if (shown <= 0 || !CCharacterSelection::m_modelFrame || !CCharacterSelection::m_modelFrame->m_model) return;
    InnCastPlan();

    for (int32_t i = 0; i < shown; i++) {
        if (i == CCharacterSelection::s_selectionIndex) continue;
        if (!CCharacterSelection::s_characterList[i].m_component && !(s_innWanted[i] && s_innInitial[i])) continue;   // the rest is built later by the trickle (v45: only those in the hall)
        if (!InnCastBuild(static_cast<uint32_t>(i))) continue;

        CharacterSelectionDisplay& entry = CCharacterSelection::s_characterList[i];
        CM2Model* model = entry.m_component->m_data.m_model;
        if (!model) continue;
        if (model->m_attachParent != CCharacterSelection::m_modelFrame->m_model) {
            model->AttachToParent(CCharacterSelection::m_modelFrame->m_model, 0, nullptr, 0);
        }
        C3Vector position(0.0f, 0.0f, 0.0f);
        CCharacterSelection::InnCastSlot(static_cast<uint32_t>(i), position);
        model->SetWorldTransform(position, 0.0f, InnCastScale(entry.m_characterInfo.m_raceID, entry.m_characterInfo.m_sexID));
    }
}

// Every frame on the character select: camera ease, greeting emote, and a frame-time line in the log every 5 seconds.
void CCharacterSelection::InnCastTick() {
    if (InnCastShown() <= 0) return;

    const uint32_t now = OsGetAsyncTimeMs();
    float dtMs = s_innLastTick ? static_cast<float>(now - s_innLastTick) : 16.0f;
    s_innLastTick = now;
    dtMs = std::clamp(dtMs, 0.0f, 100.0f);

    InnWatchdogStart();
    s_innDogActive.store(true);
    s_innBeat.fetch_add(1);
    InnPhase("camera");
    InnCastCamera(dtMs);
    InnPhase("rotate");
    InnCastRotate(now);
    InnPhase("walkers");
    InnCastWalkers(dtMs);
    InnPhase("trickle");
    InnCastTrickle(now);
    InnPhase("emote");
    InnCastEmote(now);
    InnPhase("anim inventory");
    InnCastAnimInventory();
    InnPhase("after the inn tick (engine, renderer or driver)");

    s_innStatFrames++;
    s_innStatWorst = std::max(s_innStatWorst, dtMs);
    if (!s_innStatStart) s_innStatStart = now;
    if (now - s_innStatStart >= 5000) {
        sExtensionMgr->Log("InnCast stats shown=%d listed=%u frames=%u avg=%.2f ms worst=%.1f ms",
            InnCastShown(), CCharacterSelection::s_characterList.Count(), s_innStatFrames,
            static_cast<float>(now - s_innStatStart) / static_cast<float>(std::max<uint32_t>(1, s_innStatFrames)), s_innStatWorst);
        // Where things are (scene coordinates): our camera, the backdrop's own camera, the backdrop model, the first cast member, the selected one.
        auto where = [](CM2Model* m, float* out) {
            out[0] = out[1] = out[2] = 0.0f;
            if (!m) return;
            const C44Matrix t = m->GetAnimatedWorldTransform();
            out[0] = t.d0; out[1] = t.d1; out[2] = t.d2;
        };
        float anchorAt[3], firstAt[3], selAt[3];
        where(CCharacterSelection::m_modelFrame ? CCharacterSelection::m_modelFrame->m_model : nullptr, anchorAt);
        const int32_t listed = static_cast<int32_t>(CCharacterSelection::s_characterList.Count());
        const int32_t sel = CCharacterSelection::s_selectionIndex;
        where(listed > 0 && CCharacterSelection::s_characterList[0].m_component ? CCharacterSelection::s_characterList[0].m_component->m_data.m_model : nullptr, firstAt);
        where(sel >= 0 && sel < listed && CCharacterSelection::s_characterList[sel].m_component ? CCharacterSelection::s_characterList[sel].m_component->m_data.m_model : nullptr, selAt);
        sExtensionMgr->Log("InnCast where zoomed=%d pull=%.2f cam=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) base=(%.1f %.1f %.1f)->(%.1f %.1f %.1f) anchor=(%.1f %.1f %.1f) first=(%.1f %.1f %.1f) selected[%d]=(%.1f %.1f %.1f)",
            s_innZoomed ? 1 : 0, s_innOverviewPull, s_innPos.x, s_innPos.y, s_innPos.z, s_innTgt.x, s_innTgt.y, s_innTgt.z,
            s_innBasePos.x, s_innBasePos.y, s_innBasePos.z, s_innBaseTgt.x, s_innBaseTgt.y, s_innBaseTgt.z,
            anchorAt[0], anchorAt[1], anchorAt[2], firstAt[0], firstAt[1], firstAt[2], sel, selAt[0], selAt[1], selAt[2]);
        s_innStatStart = now;
        s_innStatFrames = 0;
        s_innStatWorst = 0.0f;
    }
}

// Leaving the character select: give the frame its own camera back.
void CCharacterSelection::InnCastReset(bool detachExtras) {
    s_innDogActive.store(false);
    // On shutdown the other characters are freed with the list: detach those placed by the cast first (the selected one is handled by Shutdown).
    if (detachExtras && InnCastShown() > 0) {
        for (int32_t i = 0; i < InnCastShown(); i++) {
            CCharacterComponent* component = CCharacterSelection::s_characterList[i].m_component;
            CM2Model* model = component ? component->m_data.m_model : nullptr;
            if (i != CCharacterSelection::s_selectionIndex && model && model->m_attachParent) {
                model->DetachFromParent();
            }
        }
    }
    if (CCharacterSelection::m_modelFrame && s_innCamera && CCharacterSelection::m_modelFrame->m_camera == s_innCamera) {
        CCharacterSelection::m_modelFrame->SetCameraByIndex(0);
    }
    if (s_innCamera) {
        HandleClose(reinterpret_cast<HOBJECT>(s_innCamera));
        s_innCamera = nullptr;
    }
    if (detachExtras) { InnWmo::Forget(); InnWalk::Forget(); InnWalkers::Reset(); }
    s_innHaveCam = false;
    for (bool& logged : s_innAnimLogged) logged = false;
    s_innSeenSel = -2;
    s_innZoomed = false;
    s_innExplore = false;
    s_innRmbDown = false;
    s_innLastTrickle = 0;
    for (int32_t i = 0; i < kInnMax; i++) { s_innArrive[i] = false; s_innTrickleFailed[i] = false; s_innWanted[i] = s_innInitial[i] = s_innStay[i] = s_innLeaving[i] = s_innComing[i] = s_innPresent[i] = s_innDropCache[i] = false; s_innLastLeft[i] = 0; s_innLeaveStart[i] = 0; s_innSince[i] = 0; }
    s_innPlanned = false;
    s_innRotSeenSel = -2;
    s_innLastTick = 0;
    s_innEmoteIndex = -1;
    s_innStatStart = 0;
    s_innStatFrames = 0;
    s_innStatWorst = 0.0f;
}

// VERIFY: 68f0d05a auto
// OFFSET: 0x004E2F10
int32_t CCharacterSelection::Idle(const void* data, void* param) {
    RetailFormCommitTick();
    CCharacterSelection::InnCastTick();
    if (CCharacterSelection::s_selectionIndex < 0 || CCharacterSelection::s_selectionIndex >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) {
        CGlueLoading::Idle(nullptr);
        return 1;
    }

    CGlueLoading::Idle(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component);
    UpdateSelectionIntro(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex]);

    // Saved body sliders of the selected character (none = defaults, which also releases a previous character's).
    CharacterSelectionDisplay& selected = CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex];
    if (selected.m_component && selected.m_component->m_data.m_model) {
        static const bool noTwist[2] = { false, false };
        static const C44Matrix noBase[2];
        static const BodyShape::Values none;
        const BodyShape::Values* saved = BodyShape::RosterFor(selected.m_characterInfo.m_guid.m_guid);
        BodyShape::Apply(selected.m_component->m_data.m_model, saved ? *saved : none, noTwist, noBase);
    }

    return 1;
}

// VERIFY: f62a960c auto
// OFFSET: 0x004E3120
void CCharacterSelection::OnHide() {
    CCharacterSelection::InnCastReset();
    EventUnregister(EVENT_ID_IDLE, &CCharacterSelection::Idle);
    LoadingScreenMiniDisable();
}

// VERIFY: 61faf5ca auto
// OFFSET: 0x004E3A20
void CCharacterSelection::GenericLightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    CM2LightArray* lights = static_cast<CM2LightArray*>(arg);
    CharacterSelectionDisplay* display = CCharacterSelection::s_selectionIndex < 0 || CCharacterSelection::s_selectionIndex >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count()) ? nullptr : &CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex];

    if (!lights->unk_01BC && display && (display->m_characterInfo.m_flags & 0x2000)) {
        lights = &lights[1];

        if (!lights->m_hasCustomLight) {
            CAaSphere sphere;
            sphere.c = model->GetWorldPosition();
            sphere.r = 0.0f;
            lighting->Initialize(model->m_scene, sphere);

            LightParamsRec* paramsRec = g_lightParamsDB.GetRecord(3);
            CImVector direct = DayNight::LoadLightIntBand(0, paramsRec, 0);
            CImVector ambient = DayNight::LoadLightIntBand(0, paramsRec, 1);

            CM2Light light;
            light.SetLightType(M2LIGHT_DIRECT);
            light.m_ambColor = C3Vector(ambient);
            light.m_dirColor = C3Vector(direct);
            light.SetDirection(C3Vector(0.0f, 0.0f, -1.0f));
            light.SetVisible(1);
            lighting->AddLight(&light);

            return;
        }
    }

    if (lights->m_hasCustomLight) {
        lighting->Reset();

        for (uint32_t i = 0; i < lights->m_lightCount; i++) {
            lighting->AddLight(&lights->m_lights[i]);
        }
    }

    if (lights->unk_01B5) {
        CSimpleModelFFX* frame = lights->m_modelFrame;

        if (frame->m_flags & 0x1) {
            lighting->SetFog(C3Vector(frame->m_fogColor), frame->m_fogNear, frame->m_fogFar);
        }
    }
}

// VERIFY: ca485bb6 auto
// OFFSET: 0x004E3C10
void CCharacterSelection::SetBackgroundModel(const char* modelName) {
    if (!CCharacterSelection::m_modelFrame || !modelName || !*modelName) {
        return;
    }

    // Inn cast: WOW_INN_CAST_BACKGROUND pins one backdrop model for every character while the cast is on (the scene loads M2 models;
    // the path is as in the archives, e.g. World\Generic\...\Thing.m2). Unset or cast off = the per-race backdrop as before.
    std::string innBackground;
    if (InnCastLimit() > 0) {
        innBackground = InnCastEnvString("WOW_INN_CAST_BACKGROUND");
        if (!innBackground.empty()) {
            static std::string lastLogged;
            if (lastLogged != innBackground) {
                lastLogged = innBackground;
                sExtensionMgr->Log("InnCast background override: %s (was %s)", innBackground.c_str(), modelName);
            }
            modelName = innBackground.c_str();
        }

        // WMO inn: keep one backdrop model for every character (its attachment point is where the cast stands).
        // v49: the room and the room camera are tuned to the Orc backdrop (the one the game asks for first when the list is still empty). Pin that one (the native takes a full model path, not a race name)
        // on every client instead of whatever the first listed character happens to use (another backdrop scales the room differently, so the
        // characters ended up outside it). WOW_INN_CAST_BACKGROUND still overrides.
        if (!InnCastEnvString("WOW_INN_WMO").empty()) {
            static std::string pinned;
            if (pinned.empty()) pinned = innBackground.empty() ? std::string(R"(Interface\Glues\Models\UI_Orc\UI_Orc.m2)") : std::string(modelName);
            modelName = pinned.c_str();
        }
    }

    if (CCharacterSelection::m_modelFrame->m_model && !SStrCmpI(modelName, CCharacterSelection::m_modelFrame->m_model->m_shared->m_fileName, STORM_MAX_STR)) {
        return;
    }

    CCharacterSelection::m_modelFrame->SetModel(modelName);
    CCharacterSelection::m_modelFrame->m_lightArray1[0].unk_01B5 = 1;

    CM2Model* model = CCharacterSelection::m_modelFrame->m_model;

    if (model) {
        model->m_lightingCallback = &CCharacterSelection::GenericLightingCallback;
        model->m_lightingArg = CCharacterSelection::m_modelFrame->m_lightArray1;
        model->IsDrawable(1, 1);
    }
}

// VERIFY: fcc3bd5f auto
// OFFSET: 0x004E3CA0
void CCharacterSelection::EnumerateCharactersCallback(CHARACTER_INFO& info, void* param) {
    memcpy(&CCharacterSelection::s_characterList.New()->m_characterInfo, &info, sizeof(info));
    LoadAddOnEnableState(info.m_name);
}

// Equips a character select model with the character's gear (also used for the second Two Forms model).
void CCharacterSelection::DressComponent(const CHARACTER_INFO& info, CCharacterComponent* component) {
    for (uint32_t slot = 0; slot < 23; slot++) {
        int32_t displayID = info.m_inventoryItemDisplayID[slot];
        int32_t itemVisual = info.m_inventoryItemVisualID[slot];

        if (!displayID) {
            continue;
        }

        bool hunter = info.m_classID == 3;

        if (hunter) {
            if (slot == INVSLOT_MAINHAND || slot == INVSLOT_OFFHAND) {
                continue;
            }
        } else if (slot == INVSLOT_RANGED) {
            continue;
        }

        uint32_t flags = info.m_flags;

        if ((flags & 0x400) && slot == INVSLOT_HEAD) {
            continue;
        }

        if ((flags & 0x800) && slot == INVSLOT_BACK) {
            continue;
        }

        ItemDisplayInfoRec itemRec;
        int32_t found = g_itemDisplayInfoDB.GetRecord(displayID, &itemRec);
        int32_t visual = found && g_itemVisualsDB.GetRecord(itemRec.m_itemVisual) ? 0 : itemVisual;

        if (slot == INVSLOT_MAINHAND || slot == INVSLOT_OFFHAND || (slot == INVSLOT_RANGED && hunter)) {
            if (found) {
                SHEATHE_TYPE sheatheType = SHEATHE_NONE;
                if (s_innDressSheathed) {
                    const int32_t invType = info.m_inventoryItemType[slot];
                    if (invType == INVTYPE_SHIELD) sheatheType = SHEATHE_SHIELD;
                    else if (invType == INVTYPE_2HWEAPON) {
                        // Retail sheath types: two-handed swords/axes/maces hang diagonally on the back (SHEATHE_MAINHAND); staves, polearms and spears lie on the back (SHEATHE_BACK).
                        const char* modelName = itemRec.m_modelName[0] ? itemRec.m_modelName[0] : "";
                        bool longHaft = false;
                        for (const char* word : { "staff", "stave", "polearm", "spear", "pike" }) {
                            const size_t len = strlen(word);
                            for (const char* p = modelName; *p && !longHaft; p++) {
                                size_t k = 0;
                                while (k < len && p[k] && tolower(static_cast<unsigned char>(p[k])) == word[k]) k++;
                                if (k == len) longHaft = true;
                            }
                        }
                        sheatheType = longHaft ? SHEATHE_BACK : SHEATHE_MAINHAND;
                    }
                    else if (invType == INVTYPE_RANGED || invType == INVTYPE_RANGEDRIGHT) sheatheType = SHEATHE_BACK;
                    else if (invType != INVTYPE_HOLDABLE) sheatheType = SHEATHE_HIP;
                }
                GEOCOMPONENTLINKS link = CCharacterComponent::AddHandItem(component->m_data.m_model, &itemRec, static_cast<INVENTORY_SLOTS>(slot), sheatheType, sheatheType != SHEATHE_NONE, info.m_inventoryItemType[slot] == INVTYPE_SHIELD, false, visual);

                if (slot == INVSLOT_RANGED) {
                    if (info.m_inventoryItemType[INVSLOT_RANGED] == INVTYPE_RANGED) {
                        for (CM2Model* attached = component->m_data.m_model->m_attachList; attached; attached = attached->m_attachNext) {
                            if (attached->m_attachmentId == static_cast<uint32_t>(link)) {
                                attached->SetLoadedCallback(&CCharacterSelection::ModelLoadedCallback, nullptr);
                                break;
                            }
                        }
                    }

                    continue;
                }
            }
        } else if (static_cast<int32_t>(slot) >= 19 && static_cast<int32_t>(slot) <= 22) {
            if (*itemRec.m_modelName[0]) {
                component->AddItem(ITEMSLOT_11, &itemRec, 0);
            }
        } else {
            component->AddItemBySlot(static_cast<INVENTORY_SLOTS>(slot), displayID, visual);
        }

        if (slot == INVSLOT_TABARD && found && (itemRec.m_flags & 0x1)) {
            int32_t emblemStyle;
            int32_t emblemColor;
            int32_t borderStyle;
            int32_t borderColor;
            int32_t background;
            const CHARACTER_INFO& tabardInfo = info;

            if (GuildGetGuildTabard(tabardInfo.m_guid, tabardInfo.m_guildID, &CCharacterSelection::GuildCallback, nullptr, emblemStyle, emblemColor, borderStyle, borderColor, background)) {
                component->ApplyGuildColor(emblemStyle, emblemColor, borderStyle, borderColor, background);
            }
        }
    }
}

// VERIFY: 3fe94d69 manual
// OFFSET: 0x004E3CD0
void CCharacterSelection::ShowCharacter() {
    CCharacterSelection::ResetZoomCamera();
    if (CCharacterSelection::s_selectionIndex < 0 || CCharacterSelection::s_selectionIndex >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) {
        return;
    }

    CreatureDisplayInfoRec* displayRec = nullptr;
    CreatureModelDataRec* modelRec = nullptr;
    ComponentData data;

    if (CCharacterSelection::m_modelFrame && CCharacterSelection::m_modelFrame->m_model) {
        CCharacterSelection::m_modelFrame->m_model->DetachAllChildrenById(0);
    }

    if (CCharacterSelection::m_modelFrame && CCharacterSelection::m_modelFrame->m_model) {
        CCharacterSelection::m_modelFrame->m_model->DetachAllChildrenById(1);
    }

    CCharacterSelection::s_charFacing = 0.0f;
    CharacterSelectionDisplay::ClearFormMirror();

    int32_t newComponent = 0;

    if (!CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component) {
        newComponent = 1;

        CreatureModelDataRec* playerRec = GetModelDataRec(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_characterInfo.m_raceID, CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_characterInfo.m_sexID);

        if (!playerRec || !playerRec->m_modelName) {
            return;
        }

        CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component = CCharacterComponent::AllocComponent();

        CHARACTER_INFO& info = CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_characterInfo;
        data.m_preferences.m_raceID = info.m_raceID;
        data.m_preferences.m_sexID = info.m_sexID;
        data.m_preferences.m_classId = info.m_classID;
        data.m_preferences.m_skinID = info.m_skinColorID;
        data.m_preferences.m_faceId = info.m_faceID;
        data.m_preferences.m_hairStyleId = info.m_hairStyleID;
        data.m_preferences.m_hairColorId = info.m_hairColorID;
        data.m_preferences.m_facialHairId = info.m_facialHairStyleID;
        char retailPath[260];
        const char* uprightPath = Upright::ModelFor(info.m_raceID, info.m_sexID, info.m_skinColorID);
        const bool retailModel = !uprightPath && RetailRaceModelPath(info.m_raceID, info.m_sexID, retailPath, sizeof(retailPath), RetailRaceLookForm(info.m_guid.m_guid));
        data.m_model = CCharacterSelection::m_modelFrame->GetScene()->CreateModel(uprightPath ? uprightPath : retailModel ? retailPath : playerRec->m_modelName, 0);
        data.m_flags |= 0x2;

        CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component->Init(&data, nullptr);
        EsteriaSelectCharacter(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component, info.m_guid.m_guid, info.m_skinColorID, info.m_faceID, info.m_hairStyleID, info.m_hairColorID, info.m_facialHairStyleID);

        CM2Model* characterModel = CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component->m_data.m_model;
        characterModel->m_lightingCallback = &CCharacterSelection::GenericLightingCallback;
        characterModel->m_lightingArg = CCharacterSelection::m_modelFrame->m_lightArray2;

        CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component->m_data.m_model->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);

        CCharacterSelection::DressComponent(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_characterInfo, CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component);

        if (CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_characterInfo.m_flags & 0x2000) {
            CGlueMgr::AttachCharacterSelectVisuals(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component->m_data.m_model);
        }

        AttachSelectWings(characterModel, info.m_guid.m_guid);

        CharacterSelectionDisplay* display = &CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex];

        if (display->m_characterInfo.m_petDisplayInfoID) {
            displayRec = g_creatureDisplayInfoDB.GetRecord(display->m_characterInfo.m_petDisplayInfoID);

            if (displayRec) {
                modelRec = g_creatureModelDataDB.GetRecord(displayRec->m_modelID);

                if (modelRec) {
                    if (display->m_petModel) {
                        display->m_petModel->Release();
                    }

                    CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel = CCharacterSelection::m_modelFrame->GetScene()->CreateModel(modelRec->m_modelName, 0);
                }
            }
        }

        CharacterSelectionDisplay* current = &CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex];
        CM2Model* petModel = current->m_petModel;

        if (petModel) {
            float scale = modelRec->m_modelScale * displayRec->m_creatureModelScale;
            CreatureFamilyRec* familyRec = g_creatureFamilyDB.GetRecord(current->m_characterInfo.m_petCreatureFamilyID);

            if (familyRec) {
                int32_t petLevel = static_cast<int32_t>(current->m_characterInfo.m_petExperienceLevel);
                int32_t level = petLevel < familyRec->m_minScaleLevel ? 0 : petLevel - familyRec->m_minScaleLevel;
                int32_t range = familyRec->m_maxScaleLevel - familyRec->m_minScaleLevel;

                if (level > range) {
                    level = range;
                }

                scale = static_cast<float>(level) / range * (familyRec->m_maxScale - familyRec->m_minScale) + familyRec->m_minScale;
            }

            petModel->m_lightingCallback = &CCharacterSelection::GenericLightingCallback;
            petModel->m_lightingArg = CCharacterSelection::m_modelFrame->m_lightArray3;

            CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel->SetBoneSequence(0xFFFFFFFF, 0, 0xFFFFFFFF, 0, 1.0f, 1, 1);
            CCharacterComponent::ReplaceMonsterSkin(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel, displayRec, modelRec);
            C44Matrix matrix(scale);
            CM2Model* scaledModel = CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel;
            scaledModel->m_fFlags |= 0x8000;
            scaledModel->m_worldTransform = matrix;
            CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petAlpha = displayRec->m_creatureModelAlpha * (1.0f / 255.0f);
            CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel->m_fadeAlpha = CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petAlpha;
        }

        CCharacterSelection::s_characterCount++;
    }

    DayNight::DNInfo* info = DayNight::GetInfo();

    if (CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_characterInfo.m_flags & 0x2000) {
        FFX::SetEffect(CGlueMgr::m_deathEffect);
        info->m_bands.m_glow = s_ghostGlow;
    } else {
        FFX::SetEffect(CGlueMgr::m_glowEffect);
        info->m_bands.m_glow = s_characterGlow;
    }

    if (CCharacterSelection::m_modelFrame->m_model) {
        CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component->m_data.m_model->AttachToParent(CCharacterSelection::m_modelFrame->m_model, 0, nullptr, 0);
        CharacterSelectionDisplay::ShowFormMirror(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex]);
        CharacterSelectionDisplay::SetCharacterFacing(0.0f);

        if (CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel) {
            CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_petModel->AttachToParent(CCharacterSelection::m_modelFrame->m_model, 1, nullptr, 0);
        }

        CCharacterSelection::InnCastShow();
    }

    CGlueLoading::StartLoad(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex].m_component, newComponent);
    BeginSelectionIntro(CCharacterSelection::s_characterList[CCharacterSelection::s_selectionIndex], newComponent != 0);
}

// VERIFY: eafa3254 manual
// OFFSET: 0x004E4610
void CCharacterSelection::UpdateCharacterList() {
    ClearAddOnEnableState(nullptr);
    CCharacterSelection::s_characterList.SetCount(0);

    CCharacterSelection::s_raceRestrictions[0] = 0;
    CCharacterSelection::s_raceRestrictions[1] = 0;
    CCharacterSelection::s_raceRestrictions[2] = 0;
    CCharacterSelection::s_raceRestrictions[3] = 0;
    CCharacterSelection::s_raceRestrictions[4] = 0;
    CCharacterSelection::s_raceRestrictions[5] = 0;
    CCharacterSelection::s_raceRestrictions[6] = 0;
    CCharacterSelection::s_raceRestrictions[7] = 0;
    CCharacterSelection::s_raceRestrictions[8] = 0;
    CCharacterSelection::s_raceRestrictions[9] = 0;

    ClientServices::Connection()->EnumerateCharacters(&CCharacterSelection::EnumerateCharactersCallback, nullptr);
    InnCastApplySavedOrder();     // v46: the owner's own order of the list

    // A character swap with nothing to enter: uncover the character select (ClientDestroyGame covered it).
    if (!CCharacterSelection::s_characterList.Count() && CCharacterSelection::s_autoEnterGuid) {
        CCharacterSelection::s_autoEnterGuid = { 0 };
        LoadingScreenDisable();
    }

    if (!CCharacterSelection::s_characterList.Count()) {
        CCharacterSelection::s_selectionIndex = 0;
        CCharacterSelection::ShowCharacter();
        FrameScript_SignalEvent(GLUE_EVENT_UPDATE_SELECTED_CHARACTER, "%d", CCharacterSelection::s_selectionIndex + 1);

        if (CCharacterSelection::m_modelFrame && CCharacterSelection::m_modelFrame->m_model) {
            CCharacterSelection::m_modelFrame->m_model->DetachAllChildrenById(0);
        }

        if (CCharacterSelection::m_modelFrame && CCharacterSelection::m_modelFrame->m_model) {
            CCharacterSelection::m_modelFrame->m_model->DetachAllChildrenById(1);
        }
    } else {
        CCharacterSelection::s_raceRestrictions[0] = ClientServices::Connection()->m_restrictHuman;
        CCharacterSelection::s_raceRestrictions[1] = ClientServices::Connection()->m_restrictDwarf;
        CCharacterSelection::s_raceRestrictions[2] = ClientServices::Connection()->m_restrictGnome;
        CCharacterSelection::s_raceRestrictions[3] = ClientServices::Connection()->m_restrictNightElf;
        CCharacterSelection::s_raceRestrictions[4] = ClientServices::Connection()->m_restrictDraenei;
        CCharacterSelection::s_raceRestrictions[5] = ClientServices::Connection()->m_restrictOrc;
        CCharacterSelection::s_raceRestrictions[6] = ClientServices::Connection()->m_restrictTroll;
        CCharacterSelection::s_raceRestrictions[7] = ClientServices::Connection()->m_restrictTauren;
        CCharacterSelection::s_raceRestrictions[8] = ClientServices::Connection()->m_restrictUndead;
        CCharacterSelection::s_raceRestrictions[9] = ClientServices::Connection()->m_restrictBloodElf;

        CRealmList::m_preferredCategory = 0;

        // Character swap: enter the world as the chosen character at once. The screen is covered
        // (ClientDestroyGame), so the list is not built; the chosen character's model is, because the game takes it
        // over (Shutdown -> m_activePlayerComponent) and shows the character at once instead of building it again.
        // Only once: a target missing from the list, or one EnterWorld refuses, uncovers the normal character select.
        WOWGUID autoEnter = CCharacterSelection::s_autoEnterGuid;
        CCharacterSelection::s_autoEnterGuid = { 0 };

        if (autoEnter) {
            for (uint32_t i = 0; i < CCharacterSelection::s_characterList.Count(); i++) {
                if (CCharacterSelection::s_characterList[i].m_characterInfo.m_guid == autoEnter) {
                    CCharacterSelection::s_selectionIndex = static_cast<int32_t>(i);
                    CCharacterSelection::ShowCharacter();
                    CGlueMgr::EnterWorld();
                    break;
                }
            }

            if (CGlueMgr::m_idleState == IDLE_ENTER_WORLD) {
                return;
            }

            LoadingScreenDisable();
        }

        int32_t index = g_lastCharacterIndexVar->m_intValue < 0 ? 0 : g_lastCharacterIndexVar->m_intValue;

        if (index >= static_cast<int32_t>(CCharacterSelection::s_characterList.Count())) {
            index = 0;
        }

        CCharacterSelection::s_selectionIndex = index;
        CCharacterSelection::ShowCharacter();
        FrameScript_SignalEvent(GLUE_EVENT_UPDATE_SELECTED_CHARACTER, "%d", CCharacterSelection::s_selectionIndex + 1);
    }

    FrameScript_SignalEvent(GLUE_EVENT_CHARACTER_LIST_UPDATE, nullptr);
}

// VERIFY: 83731d6e auto
// OFFSET: 0x004E47F0
void CCharacterSelection::ClearCharacterList() {
    CCharacterSelection::s_characterList.SetCount(0);

    if (CCharacterSelection::m_modelFrame && CCharacterSelection::m_modelFrame->m_model) {
        CCharacterSelection::m_modelFrame->m_model->DetachAllChildrenById(0);
    }

    if (CCharacterSelection::m_modelFrame && CCharacterSelection::m_modelFrame->m_model) {
        CCharacterSelection::m_modelFrame->m_model->DetachAllChildrenById(1);
    }

    CCharacterSelection::s_selectionIndex = 0;
    CCharacterSelection::ShowCharacter();
    FrameScript_SignalEvent(GLUE_EVENT_UPDATE_SELECTED_CHARACTER, "%d", CCharacterSelection::s_selectionIndex + 1);

    if (CCharacterSelection::m_modelFrame) {
        CCharacterSelection::m_modelFrame->SetCameraByIndex(0);
    }

    FrameScript_SignalEvent(GLUE_EVENT_CHARACTER_LIST_UPDATE, nullptr);
}
