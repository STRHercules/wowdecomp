#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Extensions/EsteriaPerks/src/PerkWire.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include <cstdint>
#include <vector>

// Paths of Fate in game (README.md). The server holds the state; this keeps its last snapshot for the Codex addon.

namespace {
using namespace EsteriaPerkWire;

struct Card {
    uint8_t id = 0, path = 0, tier = 0, rank = 0, maxRank = 0, cost = 0, lock = 0, flags = 0;
};

struct Snapshot {
    bool valid = false;
    uint8_t earned = 0, spent = 0, trait = 0, respecs = 0, respecLock = 0;
    uint32_t respecCost = 0;
    std::vector<Card> cards;
};

Snapshot s_snapshot;

// Starting traits: their own snapshot, on their own messages (PerkWire.h).
struct Traits {
    bool valid = false;
    uint8_t base = 0, fromFlaws = 0, spent = 0, lock = 0, repair = 0;
    uint16_t respecs = 0;
    uint32_t cost = 0;
    std::vector<uint16_t> ids;
};

Traits s_traits;

bool Read(CDataStore* message, std::vector<uint8_t>& bytes) {
    if (!message || message->m_read > message->m_size) return false;
    uint32_t size = message->m_size - message->m_read;
    if (size > 4096 || !message->FetchRead(message->m_read, size)) return false;
    bytes.resize(size);
    message->GetData(bytes.data(), size);
    return true;
}

class Reader {
public:
    explicit Reader(std::vector<uint8_t> const& bytes) : m_bytes(bytes) { }
    template <typename T> bool Get(T& result) {
        if (sizeof(T) > m_bytes.size() - m_offset) return false;
        result = 0;
        for (size_t i = 0; i < sizeof(T); ++i) result |= T(m_bytes[m_offset++]) << (i * 8);
        return true;
    }
    bool Done() const { return m_offset == m_bytes.size(); }
private:
    std::vector<uint8_t> const& m_bytes;
    size_t m_offset = 0;
};

void Signal(char const* name, char const* format = nullptr, int a = 0, int b = 0, int c = 0, int d = 0) {
    uint32_t event = sExtensionMgr->GetFrameScriptEventIndex(name);
    if (event == UINT32_MAX) return;
    if (format) FrameScript_SignalEvent(event, format, a, b, c, d);
    else FrameScript_SignalEvent(event, nullptr);
}

int32_t ReceiveSnapshot(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    std::vector<uint8_t> bytes;
    if (!Read(message, bytes)) return 0;
    Reader reader(bytes);
    Snapshot next;
    uint8_t proto, count;
    if (!reader.Get(proto) || proto != Proto || !reader.Get(next.earned) || !reader.Get(next.spent)
        || !reader.Get(next.trait) || !reader.Get(next.respecs) || !reader.Get(next.respecCost)
        || !reader.Get(next.respecLock) || !reader.Get(count)) return 0;
    for (uint8_t i = 0; i < count; ++i) {
        Card c;
        if (!reader.Get(c.id) || !reader.Get(c.path) || !reader.Get(c.tier) || !reader.Get(c.rank)
            || !reader.Get(c.maxRank) || !reader.Get(c.cost) || !reader.Get(c.lock) || !reader.Get(c.flags)) return 0;
        next.cards.push_back(c);
    }
    if (!reader.Done()) return 0;
    next.valid = true;
    s_snapshot = std::move(next);
    Signal("ESTERIA_PERK_SNAPSHOT");
    return 1;
}

// action(u8) perk(u8) rank(u8) result(u8)
int32_t ReceiveResult(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    std::vector<uint8_t> bytes;
    if (!Read(message, bytes)) return 0;
    Reader reader(bytes);
    uint8_t action, perk, rank, result;
    if (!reader.Get(action) || !reader.Get(perk) || !reader.Get(rank) || !reader.Get(result) || !reader.Done())
        return 0;
    // An answer that carries no snapshot leaves the last one stale.
    if (result == Version || result == Disabled) s_snapshot = Snapshot{};
    Signal("ESTERIA_PERK_RESULT", "%d%d%d%d", action, perk, rank, result);
    return 1;
}

int32_t ReceiveTraits(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    std::vector<uint8_t> bytes;
    if (!Read(message, bytes)) return 0;
    Reader reader(bytes);
    Traits next;
    uint8_t proto, count;
    if (!reader.Get(proto) || proto != TraitProto || !reader.Get(next.base) || !reader.Get(next.fromFlaws)
        || !reader.Get(next.spent) || !reader.Get(next.respecs) || !reader.Get(next.cost) || !reader.Get(next.lock)
        || !reader.Get(next.repair) || !reader.Get(count)) return 0;
    next.ids.resize(count);
    for (uint16_t& id : next.ids) if (!reader.Get(id)) return 0;
    if (!reader.Done()) return 0;
    next.valid = true;
    s_traits = std::move(next);
    Signal("ESTERIA_TRAIT_SNAPSHOT");
    return 1;
}

int32_t ReceiveTraitResult(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    std::vector<uint8_t> bytes;
    if (!Read(message, bytes)) return 0;
    Reader reader(bytes);
    uint8_t result;
    if (!reader.Get(result) || !reader.Done()) return 0;
    Signal("ESTERIA_TRAIT_RESULT", "%d", result);
    return 1;
}

int32_t RequestTraits(lua_State* L) {
    uint8_t proto = TraitProto;
    lua_pushboolean(L, sExtensionMgr->SendClientMessage(CMSG_TRAIT_SNAPSHOT_REQUEST, &proto, 1));
    return 1;
}

// EsteriaPerks_RechooseTraits(id, ...): asks the server to replace the whole build; no arguments is an empty build.
int32_t RechooseTraits(lua_State* L) {
    int const count = lua_gettop(L);
    if (count > 8) return luaL_error(L, "Usage: EsteriaPerks_RechooseTraits(id, ...) with at most 8 ids");
    std::vector<uint8_t> payload = { TraitProto, uint8_t(count) };
    for (int i = 1; i <= count; ++i) {
        double id = lua_isnumber(L, i) ? lua_tonumber(L, i) : 0;
        if (id < 1 || id > 65535) return luaL_error(L, "Usage: EsteriaPerks_RechooseTraits(id, ...)");
        payload.push_back(uint8_t(uint16_t(id)));
        payload.push_back(uint8_t(uint16_t(id) >> 8));
    }
    lua_pushboolean(L, sExtensionMgr->SendClientMessage(CMSG_TRAIT_RECHOOSE, payload.data(), uint32_t(payload.size())));
    return 1;
}

// nil until the server has answered; then { base, fromFlaws, spent, respecs, cost, lock, repair, ids = { ... } }.
int32_t GetTraits(lua_State* L);

int32_t RequestSnapshot(lua_State* L) {
    uint8_t proto = Proto;
    lua_pushboolean(L, sExtensionMgr->SendClientMessage(CMSG_SNAPSHOT_REQUEST, &proto, 1));
    return 1;
}

int32_t Purchase(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) return luaL_error(L, "Usage: EsteriaPerks_Purchase(perk, rank)");
    double perk = lua_tonumber(L, 1), rank = lua_tonumber(L, 2);
    if (perk < 1 || perk > 255 || rank < 1 || rank > 255) return luaL_error(L, "Usage: EsteriaPerks_Purchase(perk, rank)");
    uint8_t payload[2] = { uint8_t(perk), uint8_t(rank) };
    lua_pushboolean(L, sExtensionMgr->SendClientMessage(CMSG_PURCHASE, payload, sizeof(payload)));
    return 1;
}

int32_t Respec(lua_State* L) {
    lua_pushboolean(L, sExtensionMgr->SendClientMessage(CMSG_RESPEC, nullptr, 0));
    return 1;
}

void NumberField(lua_State* L, char const* key, double value) {
    lua_pushnumber(L, value); lua_setfield(L, -2, key);
}

// nil until the server has answered; then { earned, spent, trait, respecs, respecCost, respecLock,
// cards = { { id, path, tier, rank, maxRank, cost, lock, planned } } } (PerkWire.h for the codes).
int32_t GetSnapshot(lua_State* L) {
    if (!s_snapshot.valid) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 7);
    NumberField(L, "earned", s_snapshot.earned); NumberField(L, "spent", s_snapshot.spent);
    NumberField(L, "trait", s_snapshot.trait); NumberField(L, "respecs", s_snapshot.respecs);
    NumberField(L, "respecCost", s_snapshot.respecCost); NumberField(L, "respecLock", s_snapshot.respecLock);
    lua_createtable(L, int(s_snapshot.cards.size()), 0);
    int i = 0;
    for (Card const& c : s_snapshot.cards) {
        lua_createtable(L, 0, 8);
        NumberField(L, "id", c.id); NumberField(L, "path", c.path); NumberField(L, "tier", c.tier);
        NumberField(L, "rank", c.rank); NumberField(L, "maxRank", c.maxRank); NumberField(L, "cost", c.cost);
        NumberField(L, "lock", c.lock);
        lua_pushboolean(L, (c.flags & CardPlanned) != 0); lua_setfield(L, -2, "planned");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "cards");
    return 1;
}

int32_t GetTraits(lua_State* L) {
    if (!s_traits.valid) {
        lua_pushnil(L);
        return 1;
    }
    lua_createtable(L, 0, 8);
    NumberField(L, "base", s_traits.base); NumberField(L, "fromFlaws", s_traits.fromFlaws);
    NumberField(L, "spent", s_traits.spent); NumberField(L, "respecs", s_traits.respecs);
    NumberField(L, "cost", s_traits.cost); NumberField(L, "lock", s_traits.lock);
    lua_pushboolean(L, s_traits.repair != 0); lua_setfield(L, -2, "repair");
    lua_createtable(L, int(s_traits.ids.size()), 0);
    int i = 0;
    for (uint16_t id : s_traits.ids) {
        lua_pushnumber(L, id);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "ids");
    return 1;
}

// A character swap or a dropped connection must not show the last character's perks.
class Lifecycle : public NetScript {
public:
    Lifecycle() : NetScript("EsteriaPerksLifecycle") { }
    void OnDisconnected(NetClient&) override { s_snapshot = Snapshot{}; s_traits = Traits{}; }
};

class Logout : public WorldScript {
public:
    Logout() : WorldScript("EsteriaPerksLogout") { }
    void OnLogoutComplete() override { s_snapshot = Snapshot{}; s_traits = Traits{}; }
};
}

void AddEsteriaPerksExtension() {
    sExtensionMgr->RegisterMessageHandler(SMSG_SNAPSHOT, ReceiveSnapshot, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_RESULT, ReceiveResult, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_RequestSnapshot", RequestSnapshot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_GetSnapshot", GetSnapshot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_Purchase", Purchase, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_Respec", Respec, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_PERK_SNAPSHOT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_PERK_RESULT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterMessageHandler(SMSG_TRAIT_SNAPSHOT, ReceiveTraits, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_TRAIT_RESULT, ReceiveTraitResult, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_RequestTraits", RequestTraits, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_GetTraits", GetTraits, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaPerks_RechooseTraits", RechooseTraits, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_TRAIT_SNAPSHOT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_TRAIT_RESULT", FRAMESCRIPT_CONTEXTS_GAME);
    new Lifecycle();
    new Logout();
}
