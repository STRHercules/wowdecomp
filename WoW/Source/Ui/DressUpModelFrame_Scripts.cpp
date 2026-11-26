#include "DressUpModelFrame.h"
#include <cstdint>
#include <vector>
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "WoW/Common/ComponentCore/CharacterComponent.h"
#include "Engine/Source/Frame/ModelSnapshot.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/FrameScript/FrameScript_Object.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/Storm/H/storm.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/DB/Rec/SpellRec.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Tooltip.h"
#include "WoW/Source/Ui/GameUI.h"

// VERIFY: d9ce6dd3 auto
// OFFSET: 0x00597CE0
void CGDressUpModelFrame::DestroyScriptMetaTable() {
    luaL_unref(FrameScript_GetContext(), LUA_REGISTRYINDEX, CGDressUpModelFrame::s_metatable);
    CGDressUpModelFrame::s_metatable = LUA_NOREF;
}

// VERIFY: 067c0bb2 auto
// OFFSET: 0x00597F20
void CGDressUpModelFrame::CreateScriptMetaTable() {
    CGDressUpModelFrame::s_metatable = FrameScript_Object::CreateScriptMetaTable(FrameScript_GetContext(), &CGDressUpModelFrame::RegisterScriptMethods);
}

// VERIFY: 4167113a manual
// OFFSET: 0x00597E90
static int32_t Script_Undress(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    frame->Undress();
    return 0;
}

// VERIFY: de21a638 manual
// OFFSET: 0x00597ED0
static int32_t Script_Dress(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    frame->RefreshUnit(frame->m_unitGuid);
    return 0;
}

// VERIFY: 9c209306 auto
// OFFSET: 0x00598830
static int32_t Script_TryOn(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));

    if (lua_isnumber(L, 2)) {
        frame->TryOn(static_cast<int32_t>(static_cast<int64_t>(lua_tonumber(L, 2))), nullptr, -1);
        return 0;
    }

    if (lua_isstring(L, 2)) {
        const char* link = lua_tolstring(L, 2, nullptr);

        if (SStrStr(link, "item:")) {
            ItemLinkInfo info;
            info.Clear();
            info.GetItemIdFromLink(link);
            frame->TryOn(info.m_itemId, info.m_enchants, -1);
            return 0;
        }

        if (SStrStr(link, "enchant:")) {
            const char* enchant = SStrStr(link, "enchant:");

            if (enchant) {
                int32_t spellId = SStrToInt(enchant + 8);

                if (spellId > 0) {
                    SpellRec spell;

                    if (g_spellDB.GetRecord(spellId, &spell) && (spell.m_attributes & 0x20) && spell.m_effectItemType[0]) {
                        frame->TryOn(spell.m_effectItemType[0], nullptr, -1);
                        return 0;
                    }
                }
            }
        } else {
            CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
            CGItem_C* item = player->m_bag.FindItemByFullName(lua_tolstring(L, 2, nullptr), 0);

            if (item) {
                frame->TryOnItem(item, -1);
                return 0;
            }
        }
    }

    CGGameUI::DisplayError(ERR_NOT_EQUIPPABLE);
    return 0;
}

// NOT IN BINARY. SetSheathed(true) makes the weapons TryOn adds from then on hang on the model's back or hip
// (the item's own sheathe point) instead of sitting in its hands. Items already on the model are left as they
// are: the caller dresses it again.
static int32_t Script_SetSheathed(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    frame->m_sheathed = lua_toboolean(L, 2) ? 1 : 0;
    return 0;
}

// NOT IN BINARY. SetBodyHidden(true) hides the character itself and leaves what it holds (a weapon) on show.
static int32_t Script_SetBodyHidden(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    frame->m_bodyHidden = lua_toboolean(L, 2) ? 1 : 0;
    return 0;
}

// NOT IN BINARY. SetSilhouette(true) draws the character as a dark shape; what it wears keeps its own look.
static int32_t Script_SetSilhouette(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    frame->m_silhouette = lua_toboolean(L, 2) ? 1 : 0;
    return 0;
}

// NOT IN BINARY. SetHumanPreview(female[, hairStyle]) loads the plain human man or woman and dresses that, whoever the unit is.
static int32_t Script_SetHumanPreview(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    uint8_t sex = lua_toboolean(L, 2) ? 2 : 1;

    // already that model: reloading it would only make the next dressing wait for it again
    uint8_t hair = static_cast<uint8_t>(luaL_optinteger(L, 3, 0));

    if (frame->m_previewSex == sex && frame->m_previewHair == hair && frame->m_model) {
        return 0;
    }

    frame->m_previewSex = sex;
    frame->m_previewHair = hair;

    // flags 0 like the unit's duplicated model: CSimpleModel::SetModel(path) passes 1, which makes the attached
    // item models (shoulders, helm, weapons) take their own lighting, and that has no light, so they draw black
    CM2Model* model = frame->GetScene()->CreateModel(sex == 2 ? "Character\\Human\\Female\\HumanFemale.m2" : "Character\\Human\\Male\\HumanMale.m2", 0);
    frame->SetModel(model);

    if (model) {
        model->Release();
    }

    return 0;
}

// NOT IN BINARY. SetSnapshotOnly(true) keeps the frame off the screen; it only draws when Snapshot asks it to.
static int32_t Script_SetSnapshotOnly(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    frame->m_snapshotOnly = lua_toboolean(L, 2) ? 1 : 0;
    return 0;
}

// NOT IN BINARY. Snapshot(key[, minFrames[, needAttach[, force[, refKey]]]]) draws the model, as it stands, into the picture cache under key once it has finished
// loading and been drawn for at least the optional minFrames (effects and attached models need time to appear).
// Watch for the result with HasSnapshot(key); a new request replaces one still waiting, and an empty key
// withdraws it (a request left behind would photograph whatever the model wears when it is ready).
static int32_t Script_Snapshot(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    const char* key = luaL_checkstring(L, 2);
    SStrCopy(frame->m_snapshotKey, key, sizeof(frame->m_snapshotKey));
    frame->m_snapshotPending = *key ? 1 : 0;
    frame->m_readyFrames = 0;
    frame->m_drawnFrames = 0;
    frame->m_minFrames = static_cast<uint16_t>(luaL_optinteger(L, 3, 0));
    frame->m_needAttach = lua_toboolean(L, 4) ? 1 : 0;
    frame->m_force = lua_isnumber(L, 5) ? static_cast<uint8_t>(lua_tointeger(L, 5)) : (lua_toboolean(L, 5) ? 1 : 0);
    SStrCopy(frame->m_refKey, luaL_optstring(L, 6, ""), sizeof(frame->m_refKey));
    frame->m_commitNext = 0;
    return 0;
}

// NOT IN BINARY. KeepSnapshots(key, ...) frees the textures of every stored picture not listed: the keys are the cards
// on screen. Listing none frees them all.
static int32_t Script_KeepSnapshots(lua_State* L) {
    int32_t count = lua_gettop(L) - 1;
    std::vector<const char*> keys;

    for (int32_t i = 0; i < count; i++) {
        keys.push_back(luaL_checkstring(L, 2 + i));
    }

    ModelSnapshot::Keep(keys.data(), keys.size());
    return 0;
}

static int32_t Script_IsSnapshotPending(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    lua_pushboolean(L, frame->m_snapshotPending);
    return 1;
}

// NOT IN BINARY. One line saying why a snapshot has not come out yet (/tmog shot prints it).
static int32_t Script_GetSnapshotDebug(lua_State* L) {
    CGDressUpModelFrame* frame = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, CGDressUpModelFrame::GetObjectType()));
    char text[320];
    SStrPrintf(text, sizeof(text), "pending=%d only=%d model=%d loaded=%d component=%d dirty=%x ready=%d readyFrames=%d sex=%d resident=%u saved=%u rejected=%u lastFailure=%d",
               frame->m_snapshotPending, frame->m_snapshotOnly, frame->m_model != nullptr,
               frame->m_model ? frame->m_model->IsLoaded(0, 0) : 0, frame->m_component != nullptr,
               frame->m_component ? frame->m_component->m_dirtySections : 0, frame->m_ready, frame->m_readyFrames, frame->m_previewSex,
               static_cast<unsigned>(ModelSnapshot::Count()), ModelSnapshot::Saved(), ModelSnapshot::Rejected(), ModelSnapshot::Failed());
    lua_pushstring(L, text);
    return 1;
}

static int32_t Script_HasSnapshot(lua_State* L) {
    lua_pushboolean(L, ModelSnapshot::Has(luaL_checkstring(L, 2)));
    return 1;
}

// NOT IN BINARY. ReleaseSnapshotTargets() frees the working render targets (the window closed).
static int32_t Script_ReleaseSnapshotTargets(lua_State* L) {
    ModelSnapshot::ReleaseTargets();
    return 0;
}

static int32_t Script_ClearSnapshots(lua_State* L) {
    ModelSnapshot::Clear();
    return 0;
}

// STATIC: 0x00ACF550
static FrameScript_Method s_ScriptMethods[] = {
    { "Undress", &Script_Undress },
    { "Dress", &Script_Dress },
    { "TryOn", &Script_TryOn },
    { "SetSheathed", &Script_SetSheathed },
    { "SetBodyHidden", &Script_SetBodyHidden },
    { "SetSilhouette", &Script_SetSilhouette },
    { "SetHumanPreview", &Script_SetHumanPreview },
    { "SetSnapshotOnly", &Script_SetSnapshotOnly },
    { "Snapshot", &Script_Snapshot },
    { "IsSnapshotPending", &Script_IsSnapshotPending },
    { "HasSnapshot", &Script_HasSnapshot },
    { "GetSnapshotDebug", &Script_GetSnapshotDebug },
    { "ClearSnapshots", &Script_ClearSnapshots },
    { "KeepSnapshots", &Script_KeepSnapshots },
    { "ReleaseSnapshotTargets", &Script_ReleaseSnapshotTargets },
};

// VERIFY: 9ed91f35 auto
// OFFSET: 0x00597CC0
void CGDressUpModelFrame::RegisterScriptMethods(lua_State* L) {
    CGCharacterModelBase::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, s_ScriptMethods, sizeof(s_ScriptMethods) / sizeof(s_ScriptMethods[0]));
}
