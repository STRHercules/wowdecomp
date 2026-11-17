// Draenor garrison window for the 3.3.5 client. The server (mod-esteria-garrison) owns the rules and sends one
// text blob; EsteriaGarrison.lua draws it. Opcodes sit past the stock client's table (NUM_MSG_TYPES is 0x51F).

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr uint32_t CMSG_HELLO = 0x54A;
constexpr uint32_t SMSG_STATE = 0x54B;
constexpr uint32_t CMSG_ACTION = 0x54C;

CVar* s_enabled = nullptr;
std::string s_state;

bool Enabled() { return s_enabled && s_enabled->m_intValue != 0; }

void Signal() {
    uint32_t event = sExtensionMgr->GetFrameScriptEventIndex("ESTERIA_GARRISON_STATE");
    if (event != UINT32_MAX) FrameScript_SignalEvent(event, nullptr);
}

class Writer {
public:
    template <typename T> Writer& Put(T value) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T));
        m_bytes.insert(m_bytes.end(), raw, raw + sizeof(T));
        return *this;
    }
    bool Send(uint32_t opcode) { return sExtensionMgr->SendClientMessage(opcode, m_bytes.data(), uint32_t(m_bytes.size())); }
private:
    std::vector<uint8_t> m_bytes;
};

class Reader {
public:
    explicit Reader(CDataStore* message) {
        if (!message || message->m_read > message->m_size) return;
        uint32_t size = message->m_size - message->m_read;
        if (size > (1u << 20) || !message->FetchRead(message->m_read, size)) return;
        m_bytes.resize(size);
        message->GetData(m_bytes.data(), size);
        m_ok = true;
    }
    bool Get(std::string& result) {
        if (!m_ok) return false;
        auto end = std::find(m_bytes.begin() + m_offset, m_bytes.end(), uint8_t(0));
        if (end == m_bytes.end()) return m_ok = false;
        result.assign(m_bytes.begin() + m_offset, end);
        m_offset = size_t(end - m_bytes.begin()) + 1;
        return true;
    }
private:
    std::vector<uint8_t> m_bytes;
    size_t m_offset = 0;
    bool m_ok = false;
};

int32_t ReceiveState(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    if (reader.Get(s_state)) Signal();
    return 1;
}

int32_t Hello(lua_State*) {
    if (Enabled()) Writer().Send(CMSG_HELLO);
    return 0;
}

int32_t Action(lua_State* L) {
    if (!Enabled()) return 0;
    uint8_t op = uint8_t(luaL_checknumber(L, 1));
    uint32_t a = uint32_t(luaL_optnumber(L, 2, 0));
    uint32_t b = uint32_t(luaL_optnumber(L, 3, 0));
    Writer().Put(op).Put(a).Put(b).Send(CMSG_ACTION);
    return 0;
}

int32_t GetState(lua_State* L) {
    lua_pushstring(L, s_state.c_str());
    return 1;
}
}

void AddGarrisonExtension() {
    s_enabled = sExtensionMgr->RegisterCVar("garrisonEnabled", "Esteria garrison window (needs an Esteria realm with the garrison on)",
        0x1, "1", nullptr, CATEGORY_GAME);
    sExtensionMgr->RegisterMessageHandler(SMSG_STATE, ReceiveState, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaGarrison_Hello", Hello, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaGarrison_Action", Action, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaGarrison_GetState", GetState, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_GARRISON_STATE", FRAMESCRIPT_CONTEXTS_GAME);
}
