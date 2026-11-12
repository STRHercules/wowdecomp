#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"

// Manastorm (EsteriaWoW mod-manastorm-endless, ported from CoA): the server speaks Ascension's native protocol
// (EsteriaWoW modules/mod-manastorm-endless/src/ManastormNativeProtocol.h). This keeps what it sends, so the window
// (Addons/EsteriaManastorm) can be reopened or reloaded at any time, and turns each message into a MANASTORM_* event.
// Lua: EsteriaManastorm_* below. Modes are 1..8: Solo, Duo, Trio, Group, then the same four in the endgame track.

namespace {
constexpr uint32_t CMSG_ENTER = 0x651, CMSG_LEAVE = 0x665, CMSG_SET_LOADOUT_SLOT = 0x689;
constexpr uint32_t SMSG_CONFIG = 0x58D, SMSG_ENTER_RESULT = 0x652, SMSG_PROGRESS = 0x65D, SMSG_LEVEL_COMPLETED = 0x65E,
    SMSG_COMPLETED_DEPTHS = 0x65F, SMSG_ACTIVE = 0x660, SMSG_LEAVE_RESULT = 0x666, SMSG_FAILED = 0x67B,
    SMSG_CHAOTIC_LINK = 0x67F, SMSG_LOADOUT = 0x688, SMSG_LOADOUT_RESULT = 0x68A, SMSG_LOADOUT_SLOT = 0x68B,
    SMSG_REWARD_VISIBILITY = 0x6AD;
constexpr size_t Modes = 8, LoadoutSlots = 4, MaxDepths = 16384, MaxRewards = 64, MaxConfig = 256;

struct Active {
    uint32_t depth = 0, scene = 0, caches = 0, cacheItem = 0;
    std::string mode;
    float cacheChance = 0;
};

struct Reward {
    uint32_t item = 0, lockEnd = 0, secondsLeft = 0, earned = 0, maximum = 0, remaining = 0, windowStart = 0, windowEnd = 0;
    uint8_t action = 0;
};

using Progress = std::vector<std::vector<uint32_t>>;

std::map<std::string, uint32_t> s_integers;
std::map<std::string, bool> s_booleans;
std::map<uint64_t, Progress> s_progress;  // per player GUID: you and your group
Active s_active;
uint32_t s_chaoticLink = 0;
uint32_t s_loadout[LoadoutSlots] = {};
std::vector<Reward> s_rewards;

class Reader {
public:
    explicit Reader(CDataStore* message) {
        if (!message || message->m_read > message->m_size) return;
        const uint32_t size = message->m_size - message->m_read;
        if (size > 256 * 1024 || !message->FetchRead(message->m_read, size)) return;
        m_bytes.resize(size);
        message->GetData(m_bytes.data(), size);
        m_ok = true;
    }
    template <typename T> bool Get(T& out) {
        if (!m_ok || sizeof(T) > m_bytes.size() - m_at) return m_ok = false;
        std::memcpy(&out, m_bytes.data() + m_at, sizeof(T));
        m_at += sizeof(T);
        return true;
    }
    bool CString(std::string& out) {
        const auto* start = m_ok && m_at < m_bytes.size() ? m_bytes.data() + m_at : nullptr;
        const void* end = start ? std::memchr(start, 0, m_bytes.size() - m_at) : nullptr;
        if (!end) return m_ok = false;
        out.assign(reinterpret_cast<const char*>(start), static_cast<const uint8_t*>(end) - start);
        m_at += out.size() + 1;
        return true;
    }
    bool Key(std::string& out) {  // u32 length + bytes (0x58D)
        uint32_t length = 0;
        if (!Get(length) || length > 256 || length > m_bytes.size() - m_at) return m_ok = false;
        out.assign(reinterpret_cast<const char*>(m_bytes.data() + m_at), length);
        m_at += length;
        return true;
    }
    bool Done() const { return m_ok && m_at == m_bytes.size(); }

private:
    std::vector<uint8_t> m_bytes;
    size_t m_at = 0;
    bool m_ok = false;
};

void Signal(const char* name, const char* format = nullptr, ...) {
    const uint32_t event = sExtensionMgr->GetFrameScriptEventIndex(name);
    if (event == UINT32_MAX) return;
    va_list args;
    va_start(args, format);
    vFrameScript_SignalEvent(event, format, args);
    va_end(args);
}

// u32 modes, then per mode: u32 count, count * u32 depth
bool ReadProgress(Reader& reader, Progress& progress) {
    uint32_t modes = 0;
    if (!reader.Get(modes) || modes > Modes) return false;
    progress.assign(Modes, {});
    for (uint32_t mode = 0; mode < modes; ++mode) {
        uint32_t count = 0;
        if (!reader.Get(count) || count > MaxDepths) return false;
        progress[mode].resize(count);
        for (auto& depth : progress[mode])
            if (!reader.Get(depth)) return false;
    }
    return true;
}

uint64_t PlayerGuid() { return ClntObjMgrGetActivePlayer().m_guid; }

// ---------------------------------------------------------------------------------------------------- server
// key maps: integers (u32), booleans (u8); then float, rate, integer-list and float-list maps this server leaves empty
int32_t ReceiveConfig(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    std::map<std::string, uint32_t> integers;
    std::map<std::string, bool> booleans;
    uint32_t count = 0;
    std::string key;
    if (!reader.Get(count) || count > MaxConfig) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t value = 0;
        if (!reader.Key(key) || !reader.Get(value)) return 0;
        integers[key] = value;
    }
    if (!reader.Get(count) || count > MaxConfig) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t value = 0;
        if (!reader.Key(key) || !reader.Get(value)) return 0;
        booleans[key] = value != 0;
    }
    s_integers = std::move(integers);
    s_booleans = std::move(booleans);
    Signal("MANASTORM_CONFIG_UPDATED");
    return 1;
}

int32_t ReceiveCompletedDepths(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    Progress progress;
    if (!ReadProgress(reader, progress) || !reader.Done()) return 0;
    s_progress[PlayerGuid()] = std::move(progress);
    Signal("MANASTORM_PROGRESS_UPDATED", "%s", "player");
    return 1;
}

// guid(8) + progress, sent for each member of your group (and for you)
int32_t ReceiveProgress(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint64_t guid = 0;
    Progress progress;
    if (!reader.Get(guid) || !ReadProgress(reader, progress) || !reader.Done()) return 0;
    s_progress[guid] = std::move(progress);
    char text[24];
    std::snprintf(text, sizeof(text), "0x%016llX", static_cast<unsigned long long>(guid));
    Signal("MANASTORM_PROGRESS_UPDATED", "%s", guid == PlayerGuid() ? "player" : text);
    return 1;
}

// depth, scene(segment), mode name, guaranteed caches, cache chance %, cache item; depth 0 = not in a run
int32_t ReceiveActive(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    Active active;
    if (!reader.Get(active.depth) || !reader.Get(active.scene) || !reader.CString(active.mode) || !reader.Get(active.caches)
        || !reader.Get(active.cacheChance) || !reader.Get(active.cacheItem) || !reader.Done()) return 0;
    s_active = active;
    Signal("MANASTORM_ACTIVE_UPDATED");
    return 1;
}

int32_t ReceiveResult(void*, NETMESSAGE id, uint32_t, CDataStore* message) {
    Reader reader(message);
    std::string result;
    if (!reader.CString(result) || !reader.Done()) return 0;
    Signal(id == SMSG_ENTER_RESULT ? "MANASTORM_ENTER_RESULT" : "MANASTORM_LEAVE_RESULT", "%s", result.c_str());
    return 1;
}

int32_t ReceiveLevelCompleted(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint32_t depth = 0;
    if (!reader.Get(depth) || !reader.Done()) return 0;
    Signal("MANASTORM_LEVEL_COMPLETED", "%u", depth);
    return 1;
}

int32_t ReceiveFailed(void*, NETMESSAGE, uint32_t, CDataStore*) {
    Signal("MANASTORM_FAILED");
    return 1;
}

int32_t ReceiveChaoticLink(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint32_t stacks = 0;
    if (!reader.Get(stacks) || !reader.Done()) return 0;
    s_chaoticLink = stacks;
    Signal("MANASTORM_CHAOTIC_LINK_UPDATED", "%u", stacks);
    return 1;
}

// u32 count, count * spell
int32_t ReceiveLoadout(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint32_t count = 0;
    if (!reader.Get(count) || count > LoadoutSlots) return 0;
    uint32_t slots[LoadoutSlots] = {};
    for (uint32_t i = 0; i < count; ++i)
        if (!reader.Get(slots[i])) return 0;
    if (!reader.Done()) return 0;
    std::memcpy(s_loadout, slots, sizeof(slots));
    Signal("MANASTORM_LOADOUT_UPDATED", "%u%u", 0u, 0u);
    return 1;
}

int32_t ReceiveLoadoutSlot(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint32_t slot = 0, spell = 0;
    if (!reader.Get(slot) || !reader.Get(spell) || !reader.Done() || slot >= LoadoutSlots) return 0;
    s_loadout[slot] = spell;
    Signal("MANASTORM_LOADOUT_UPDATED", "%u%u", slot + 1, spell);
    return 1;
}

int32_t ReceiveLoadoutResult(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint32_t slot = 0;
    std::string result;
    if (!reader.Get(slot) || !reader.CString(result) || !reader.Done()) return 0;
    Signal("MANASTORM_LOADOUT_RESULT", "%u%s", slot + 1, result.c_str());
    return 1;
}

// context(u64) count(u32), then per reward: item(u32) action(u8) lockEnd secondsLeft earned max remaining windowStart
// windowEnd (u32 each)
int32_t ReceiveRewardVisibility(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader reader(message);
    uint64_t context = 0;
    uint32_t count = 0;
    if (!reader.Get(context) || !reader.Get(count) || count > MaxRewards) return 0;
    std::vector<Reward> rewards(count);
    for (auto& r : rewards)
        if (!reader.Get(r.item) || !reader.Get(r.action) || !reader.Get(r.lockEnd) || !reader.Get(r.secondsLeft)
            || !reader.Get(r.earned) || !reader.Get(r.maximum) || !reader.Get(r.remaining) || !reader.Get(r.windowStart)
            || !reader.Get(r.windowEnd)) return 0;
    if (!reader.Done()) return 0;
    s_rewards = std::move(rewards);
    Signal("MANASTORM_REWARDS_UPDATED");
    return 1;
}

// -------------------------------------------------------------------------------------------------------- Lua
// EsteriaManastorm_GetConfig(key) -> number or boolean, nil when the server did not send it
int GetConfig(lua_State* L) {
    const std::string key = luaL_checkstring(L, 1);
    if (auto it = s_integers.find(key); it != s_integers.end()) { lua_pushnumber(L, it->second); return 1; }
    if (auto it = s_booleans.find(key); it != s_booleans.end()) { lua_pushboolean(L, it->second); return 1; }
    return 0;
}

// EsteriaManastorm_GetCompletedDepths(mode[, guid]) -> { depth, ... } ascending, for you or a group member's GUID
// ("0x..." as in MANASTORM_PROGRESS_UPDATED). Depths you may enter follow from these (see the addon).
int GetCompletedDepths(lua_State* L) {
    const int mode = int(luaL_checknumber(L, 1));
    uint64_t guid = PlayerGuid();
    if (lua_isstring(L, 2)) guid = std::strtoull(lua_tostring(L, 2), nullptr, 16);
    lua_newtable(L);
    auto it = s_progress.find(guid);
    if (mode < 1 || mode > int(Modes) || it == s_progress.end()) return 1;
    int index = 1;
    for (uint32_t depth : it->second[mode - 1]) {
        lua_pushnumber(L, depth);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// EsteriaManastorm_GetActive() -> depth, scene, mode, guaranteedCaches, cacheChance, cacheItem; nil outside a run
int GetActive(lua_State* L) {
    if (!s_active.depth) return 0;
    lua_pushnumber(L, s_active.depth);
    lua_pushnumber(L, s_active.scene);
    lua_pushstring(L, s_active.mode.c_str());
    lua_pushnumber(L, s_active.caches);
    lua_pushnumber(L, s_active.cacheChance);
    lua_pushnumber(L, s_active.cacheItem);
    return 6;
}

int GetChaoticLink(lua_State* L) {
    lua_pushnumber(L, s_chaoticLink);
    return 1;
}

// EsteriaManastorm_GetLoadout() -> spell1, spell2, spell3, spell4 (0 = empty)
int GetLoadout(lua_State* L) {
    for (uint32_t spell : s_loadout) lua_pushnumber(L, spell);
    return int(LoadoutSlots);
}

// EsteriaManastorm_GetRewards() -> { { item, action, lockEnd, secondsLeft, earned, max, remaining, windowStart, windowEnd }, ... }
int GetRewards(lua_State* L) {
    lua_createtable(L, int(s_rewards.size()), 0);
    int index = 1;
    for (const auto& r : s_rewards) {
        lua_createtable(L, 0, 9);
        const std::pair<const char*, double> fields[] = {{"item", r.item}, {"action", r.action}, {"lockEnd", r.lockEnd},
            {"secondsLeft", r.secondsLeft}, {"earned", r.earned}, {"max", r.maximum}, {"remaining", r.remaining},
            {"windowStart", r.windowStart}, {"windowEnd", r.windowEnd}};
        for (const auto& [name, value] : fields) {
            lua_pushnumber(L, value);
            lua_setfield(L, -2, name);
        }
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

// EsteriaManastorm_Enter(depth) -> sent; the answer is MANASTORM_ENTER_RESULT
int Enter(lua_State* L) {
    const uint32_t depth = uint32_t(luaL_checknumber(L, 1));
    lua_pushboolean(L, depth && sExtensionMgr->SendClientMessage(CMSG_ENTER, &depth, sizeof(depth)));
    return 1;
}

// EsteriaManastorm_Leave() -> sent; the answer is MANASTORM_LEAVE_RESULT
int Leave(lua_State* L) {
    lua_pushboolean(L, sExtensionMgr->SendClientMessage(CMSG_LEAVE, nullptr, 0));
    return 1;
}

// EsteriaManastorm_SetLoadoutSlot(slot 1-4, spell) -> sent; the answer is MANASTORM_LOADOUT_RESULT
int SetLoadoutSlot(lua_State* L) {
    const uint32_t slot = uint32_t(luaL_checknumber(L, 1)), spell = uint32_t(luaL_checknumber(L, 2));
    const uint32_t payload[2] = {slot - 1, spell};
    lua_pushboolean(L, slot >= 1 && slot <= LoadoutSlots
        && sExtensionMgr->SendClientMessage(CMSG_SET_LOADOUT_SLOT, payload, sizeof(payload)));
    return 1;
}

class Lifecycle : public ClientScript {
public:
    Lifecycle() : ClientScript("ManastormLifecycle") { }
    // The server resends everything at login; nothing may leak from one character to the next.
    void OnLeaveWorld() override {
        s_integers.clear();
        s_booleans.clear();
        s_progress.clear();
        s_active = {};
        s_chaoticLink = 0;
        std::memset(s_loadout, 0, sizeof(s_loadout));
        s_rewards.clear();
    }
};
}

void AddManastormExtension() {
    const std::pair<uint32_t, ExtensionMessageHandler> handlers[] = {
        {SMSG_CONFIG, ReceiveConfig}, {SMSG_ENTER_RESULT, ReceiveResult}, {SMSG_LEAVE_RESULT, ReceiveResult},
        {SMSG_PROGRESS, ReceiveProgress}, {SMSG_LEVEL_COMPLETED, ReceiveLevelCompleted},
        {SMSG_COMPLETED_DEPTHS, ReceiveCompletedDepths}, {SMSG_ACTIVE, ReceiveActive}, {SMSG_FAILED, ReceiveFailed},
        {SMSG_CHAOTIC_LINK, ReceiveChaoticLink}, {SMSG_LOADOUT, ReceiveLoadout}, {SMSG_LOADOUT_RESULT, ReceiveLoadoutResult},
        {SMSG_LOADOUT_SLOT, ReceiveLoadoutSlot}, {SMSG_REWARD_VISIBILITY, ReceiveRewardVisibility}};
    for (const auto& [opcode, handler] : handlers) sExtensionMgr->RegisterMessageHandler(opcode, handler, nullptr);
    const std::pair<const char*, lua_CFunction> functions[] = {
        {"EsteriaManastorm_GetConfig", GetConfig}, {"EsteriaManastorm_GetCompletedDepths", GetCompletedDepths},
        {"EsteriaManastorm_GetActive", GetActive}, {"EsteriaManastorm_GetChaoticLink", GetChaoticLink},
        {"EsteriaManastorm_GetLoadout", GetLoadout}, {"EsteriaManastorm_GetRewards", GetRewards},
        {"EsteriaManastorm_Enter", Enter}, {"EsteriaManastorm_Leave", Leave},
        {"EsteriaManastorm_SetLoadoutSlot", SetLoadoutSlot}};
    for (const auto& [name, function] : functions) sExtensionMgr->RegisterLuaFunction(name, function, FRAMESCRIPT_CONTEXTS_GAME);
    for (const char* event : {"MANASTORM_CONFIG_UPDATED", "MANASTORM_PROGRESS_UPDATED", "MANASTORM_ACTIVE_UPDATED",
             "MANASTORM_ENTER_RESULT", "MANASTORM_LEAVE_RESULT", "MANASTORM_LEVEL_COMPLETED", "MANASTORM_FAILED",
             "MANASTORM_CHAOTIC_LINK_UPDATED", "MANASTORM_LOADOUT_UPDATED", "MANASTORM_LOADOUT_RESULT",
             "MANASTORM_REWARDS_UPDATED"})
        sExtensionMgr->RegisterFrameScriptEvent(event, FRAMESCRIPT_CONTEXTS_GAME);
    new Lifecycle();
}
