#include <cstring>
#include <map>
#include <vector>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Frame/CScriptObject.h"
#include "Engine/Source/Frame/CSimpleModel.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Common/ComponentCore/MountCustomize.h"
#include "WoW/Source/DB/DBClient/DBCacheInstances.h"
#include "WoW/Source/DB/DBClient/CreatureStats_C.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Object/ObjectClient/Unit_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

// Mount customization (docs: WoW/Common/ComponentCore/MountCustomize.h). The server sends each player's mount looks
// when that player comes into view and after every save; the Customize window (Addons/EsteriaMountCustomize) reads
// the options, previews on a model frame and saves through the functions below.

namespace {
constexpr uint32_t CMSG_ESTERIA_MOUNT_LOOK = 0x544;
constexpr uint32_t SMSG_ESTERIA_MOUNT_LOOKS = 0x545;
constexpr size_t MaxChoices = 64;

// guid(8) + count(1) + count * (display(4) + choices(1) + choices * choice(4))
int32_t ReceiveLooks(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    if (!message || message->m_read > message->m_size) return 0;
    const uint32_t size = message->m_size - message->m_read;
    if (size < 9 || size > 64 * 1024 || !message->FetchRead(message->m_read, size)) return 0;
    std::vector<uint8_t> bytes(size);
    message->GetData(bytes.data(), size);
    size_t at = 0;
    auto take = [&](void* out, size_t n) {
        if (n > bytes.size() - at) return false;
        std::memcpy(out, bytes.data() + at, n);
        at += n;
        return true;
    };
    uint64_t guid = 0;
    uint8_t count = 0;
    if (!take(&guid, 8) || !take(&count, 1)) return 0;
    std::map<uint32_t, std::vector<uint32_t>> looks;
    for (uint8_t i = 0; i < count; ++i) {
        uint32_t display = 0;
        uint8_t choices = 0;
        if (!take(&display, 4) || !take(&choices, 1) || choices > MaxChoices) return 0;
        std::vector<uint32_t>& look = looks[display];
        look.resize(choices);
        if (choices && !take(look.data(), size_t(choices) * 4)) return 0;
    }
    MountCustomize::SetLooks(guid, looks);
    // A player already riding one of these mounts is redrawn now; others pick the look up when they mount.
    if (auto* unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(WOWGUID{guid}, TYPEMASK_UNIT, __FILE__, __LINE__))) {
        if (unit->m_mountModel) MountCustomize::ApplyOwner(unit->m_mountModel, uint32_t(unit->m_mountDisplayId), guid);
    }
    return 1;
}

std::vector<uint32_t> ChoicesArg(lua_State* L, int index) {
    std::vector<uint32_t> choices;
    if (!lua_istable(L, index)) return choices;
    for (int i = 1; choices.size() < MaxChoices; ++i) {
        lua_rawgeti(L, index, i);
        const bool more = lua_isnumber(L, -1);
        if (more && lua_tonumber(L, -1) > 0) choices.push_back(uint32_t(lua_tonumber(L, -1)));
        lua_pop(L, 1);
        if (!more) break;
    }
    return choices;
}

// EsteriaMountCustomize_GetDisplay(creatureID) -> display or nil (the mount's creature must be cached: the journal
// has posed it).
int GetDisplay(lua_State* L) {
    WOWGUID none{};
    CreatureStats_C* stats = g_creatureDBCache.GetRecord(int32_t(luaL_checknumber(L, 1)), none, nullptr, nullptr, false);
    if (!stats || !stats->m_displayId[0]) return 0;
    lua_pushnumber(L, stats->m_displayId[0]);
    return 1;
}

// EsteriaMountCustomize_GetOptions(display) -> { { id, name, choices = { { id, name, swatch }, ... } }, ... } or nil
int GetOptions(lua_State* L) {
    std::vector<MountCustomize::Option> options;
    if (!MountCustomize::Options(uint32_t(luaL_checknumber(L, 1)), options)) return 0;
    lua_createtable(L, int(options.size()), 0);
    for (size_t i = 0; i < options.size(); ++i) {
        lua_createtable(L, 0, 3);
        lua_pushnumber(L, options[i].id); lua_setfield(L, -2, "id");
        lua_pushstring(L, options[i].name.c_str()); lua_setfield(L, -2, "name");
        lua_createtable(L, int(options[i].choices.size()), 0);
        for (size_t j = 0; j < options[i].choices.size(); ++j) {
            const auto& choice = options[i].choices[j];
            lua_createtable(L, 0, 3);
            lua_pushnumber(L, choice.id); lua_setfield(L, -2, "id");
            lua_pushstring(L, choice.name.c_str()); lua_setfield(L, -2, "name");
            lua_pushnumber(L, choice.swatch); lua_setfield(L, -2, "swatch");
            lua_rawseti(L, -2, int(j + 1));
        }
        lua_setfield(L, -2, "choices");
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

// EsteriaMountCustomize_GetLook(display) -> { choiceID, ... }: the player's saved look, else the display's own.
int GetLook(lua_State* L) {
    const uint32_t display = uint32_t(luaL_checknumber(L, 1));
    const std::vector<uint32_t>* saved = MountCustomize::Look(ClntObjMgrGetActivePlayer().m_guid, display);
    const std::vector<uint32_t> look = saved ? *saved : MountCustomize::Defaults(display);
    lua_createtable(L, int(look.size()), 0);
    for (size_t i = 0; i < look.size(); ++i) {
        lua_pushnumber(L, look[i]);
        lua_rawseti(L, -2, int(i + 1));
    }
    return 1;
}

// EsteriaMountCustomize_Preview(modelFrame, display, { choiceID, ... }): draws the frame's posed mount with choices.
int Preview(lua_State* L) {
    auto* frame = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, CSimpleModel::GetObjectType()));
    if (frame && frame->m_model) MountCustomize::Apply(frame->m_model, uint32_t(luaL_checknumber(L, 2)), ChoicesArg(L, 3));
    return 0;
}

// EsteriaMountCustomize_Save(display, { choiceID, ... }) -> true when sent; the server echoes the saved looks back.
int Save(lua_State* L) {
    const uint32_t display = uint32_t(luaL_checknumber(L, 1));
    const std::vector<uint32_t> choices = ChoicesArg(L, 2);
    std::vector<uint8_t> payload(5 + choices.size() * 4);
    std::memcpy(payload.data(), &display, 4);
    payload[4] = uint8_t(choices.size());
    if (!choices.empty()) std::memcpy(payload.data() + 5, choices.data(), choices.size() * 4);
    lua_pushboolean(L, display && !choices.empty()
        && sExtensionMgr->SendClientMessage(CMSG_ESTERIA_MOUNT_LOOK, payload.data(), uint32_t(payload.size())));
    return 1;
}

class Lifecycle : public ClientScript {
public:
    Lifecycle() : ClientScript("MountCustomizeLifecycle") { }
    void OnLeaveWorld() override { MountCustomize::ClearLooks(); }
};
}

void AddMountCustomizeExtension() {
    sExtensionMgr->RegisterMessageHandler(SMSG_ESTERIA_MOUNT_LOOKS, ReceiveLooks, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaMountCustomize_GetDisplay", GetDisplay, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaMountCustomize_GetOptions", GetOptions, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaMountCustomize_GetLook", GetLook, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaMountCustomize_Preview", Preview, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaMountCustomize_Save", Save, FRAMESCRIPT_CONTEXTS_GAME);
    new Lifecycle();
}
