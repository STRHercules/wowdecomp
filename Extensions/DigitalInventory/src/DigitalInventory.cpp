#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/Ui/DigitalInventoryCounts.h"
#include "WoW/Source/Ui/TradeSkillFrame.h"
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr uint32_t CMSG_ESTERIA_INVENTORY = 0x521;
constexpr uint32_t SMSG_ESTERIA_INVENTORY = 0x522;
constexpr uint8_t ProtocolVersion = 3;
// Carried storage tops out at 10000 stacks; the account bank may hold far more.
constexpr uint32_t MaximumSlots = 10000;
constexpr uint32_t MaximumStored = 1000000;

struct Row {
    uint32_t guid = 0, entry = 0, count = 0, durability = 0, maxDurability = 0;
    uint8_t digital = 0, bag = 0, slot = 0, quality = 0, favorite = 0, category = 0, inventoryType = 0, bound = 0, split = 0;
    std::string link, name, icon, tag;
};

struct Snapshot {
    uint32_t request = 0, revision = 0, capacity = 0, used = 0, total = 0, offset = 0, price = 0, upgrade = 0, unlockCost = 0;
    uint8_t scope = 0, active = 0, status = 0, flags = 0;
    std::string message;
    std::vector<Row> rows;
};

Snapshot s_snapshots[3];  // 0 carried, 1 account bank, 2 guild bank tab descriptors
uint32_t s_request = 0;

// Parse into temporary state; an invalid/truncated packet never changes the last valid snapshot.
class Reader {
public:
    explicit Reader(std::vector<uint8_t> const& bytes) : m_bytes(bytes) { }
    template <typename T> bool Get(T& result) {
        if (m_offset > m_bytes.size() || sizeof(T) > m_bytes.size() - m_offset) return false;
        result = 0;
        for (size_t i = 0; i < sizeof(T); ++i) result |= T(m_bytes[m_offset++]) << (i * 8);
        return true;
    }
    bool String(std::string& result, size_t maximum) {
        size_t start = m_offset;
        while (m_offset < m_bytes.size() && m_bytes[m_offset] && m_offset - start <= maximum) ++m_offset;
        if (m_offset == m_bytes.size() || m_offset - start > maximum) return false;
        result.assign(reinterpret_cast<char const*>(m_bytes.data() + start), m_offset++ - start);
        return true;
    }
    bool Skip(size_t size) {
        if (size > m_bytes.size() - m_offset) return false;
        m_offset += size;
        return true;
    }
    bool Done() const { return m_offset == m_bytes.size(); }
private:
    std::vector<uint8_t> const& m_bytes;
    size_t m_offset = 0;
};

int32_t Receive(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    if (!message || message->m_read > message->m_size) return 0;
    uint32_t size = message->m_size - message->m_read;
    if (size < 45 || size > (1u << 20) || !message->FetchRead(message->m_read, size)) return 0;
    std::vector<uint8_t> bytes(size);
    message->GetData(bytes.data(), size);
    Reader reader(bytes);
    Snapshot next;
    uint8_t version = 0, rows = 0;
    if (!reader.Get(version) || version != ProtocolVersion
        || !reader.Get(next.scope) || next.scope > 2
        || !reader.Get(next.request) || !reader.Get(next.revision) || !reader.Get(next.capacity)
        || !reader.Get(next.used) || !reader.Get(next.total) || !reader.Get(next.offset)
        || !reader.Get(next.price) || !reader.Get(next.upgrade) || !reader.Get(next.unlockCost) || !reader.Get(next.active)
        || !reader.Get(next.status) || !reader.Get(next.flags) || !reader.String(next.message, 512) || !reader.Get(rows)
        || next.capacity > MaximumStored || next.used > MaximumStored
        || next.total > MaximumStored + 1100 || next.offset > next.total || rows > next.total - next.offset
        || next.active > 1) return 0;
    for (uint8_t i = 0; i < rows; ++i) {
        Row row;
        if (!reader.Get(row.guid) || !reader.Get(row.entry) || !reader.Get(row.count)
            || !reader.Get(row.digital) || !reader.Get(row.bag) || !reader.Get(row.slot)
            || !reader.Get(row.quality) || !reader.Get(row.favorite) || !reader.Get(row.category)
            || !reader.Get(row.inventoryType) || !reader.Get(row.bound)
            || !reader.Get(row.durability) || !reader.Get(row.maxDurability)
            || !reader.String(row.link, 256) || !reader.String(row.name, 512) || !reader.String(row.icon, 256)
            || !reader.Get(row.split) || !reader.String(row.tag, 64) || row.digital > 1 || row.quality > 7 || row.category > 4 || row.inventoryType > 28) return 0;
        if (!row.digital && !((row.bag == 255 && row.slot >= 23 && row.slot < 39)
            || (row.bag >= 19 && row.bag < 23 && row.slot < 254))) return 0;
        next.rows.push_back(std::move(row));
    }
    uint16_t count = 0;
    if (!reader.Get(count) || count > MaximumSlots) return 0;
    Snapshot& current = s_snapshots[next.scope];
    bool carried = next.scope == 0;
    if (!carried && count) return 0;
    // Browse/page replies repeat the aggregate index; same revision and size means it is already cached.
    bool reuse = carried && next.active && current.active && next.revision == current.revision
        && count == EsteriaInventoryClient::counts.size();
    std::unordered_map<uint32_t, EsteriaInventoryClient::CountEntry> counts;
    if (reuse) {
        if (!reader.Skip(size_t(count) * 12)) return 0;
    } else {
        for (uint16_t i = 0; i < count; ++i) {
            uint32_t entry = 0;
            EsteriaInventoryClient::CountEntry total;
            if (!reader.Get(entry) || !reader.Get(total.count) || !reader.Get(total.totemCategory)
                || !entry || !counts.emplace(entry, total).second) return 0;
        }
    }
    if (!reader.Done()) return 0;
    bool changed = next.revision != current.revision;
    uint8_t scope = next.scope;
    current = std::move(next);
    if (carried) {
        if (!reuse) EsteriaInventoryClient::counts = std::move(counts);
        if (!current.active) EsteriaInventoryClient::counts.clear();
        if (changed && CGTradeSkillInfo::m_skillLineID) CGTradeSkillInfo::RefreshList(0);
    }
    uint32_t event = sExtensionMgr->GetFrameScriptEventIndex("ESTERIA_INVENTORY_UPDATE");
    if (event != UINT32_MAX) FrameScript_SignalEvent(event, "%d%d", scope, current.revision);
    return 1;
}

bool Number(lua_State* L, int index, uint32_t maximum, uint32_t& out) {
    if (!lua_isnumber(L, index)) return false;
    double value = lua_tonumber(L, index);
    if (!std::isfinite(value) || value < 0 || value > maximum || std::floor(value) != value) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

int32_t Request(lua_State* L) {
    bool specialTabs = lua_toboolean(L, 11) && (s_snapshots[0].flags & 0x10) != 0;
    uint32_t action, revision, guid, count, argument, category, sort, scope = 0;
    if (!Number(L, 1, 13, action) || !Number(L, 2, UINT32_MAX, revision)
        || !Number(L, 3, UINT32_MAX, guid) || !Number(L, 4, UINT32_MAX, count)
        || !Number(L, 5, MaximumStored + 1100, argument) || !Number(L, 6, specialTabs ? 7 : 5, category)
        || !Number(L, 7, 4, sort))
        return luaL_error(L, "Invalid Esteria inventory request");
    size_t length = 0;
    char const* search = lua_tolstring(L, 8, &length);
    if (!search || length > 80 || std::memchr(search, 0, length))
        return luaL_error(L, "Inventory search must be at most 80 bytes");
    uint64_t target = 0;
    if (lua_isstring(L, 9)) {
        size_t size = 0;
        char const* text = lua_tolstring(L, 9, &size);
        if (size >= 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) { text += 2; size -= 2; }
        auto result = std::from_chars(text, text + size, target, 16);
        if (result.ec != std::errc() || result.ptr != text + size) return luaL_error(L, "Invalid vendor GUID");
    }
    if (lua_isnumber(L, 10) && !Number(L, 10, 2, scope)) return luaL_error(L, "Invalid inventory scope");
    if (specialTabs && scope != 2) category |= 0x80;
    if (++s_request == 0) ++s_request;
    CDataStore payload;
    payload.Put(ProtocolVersion).Put(uint8_t(action)).Put(uint8_t(scope)).Put(s_request).Put(revision).Put(guid).Put(count)
        .Put(argument).Put(target).Put(uint8_t(category)).Put(uint8_t(sort)).PutString(search);
    payload.Finalize();
    if (sExtensionMgr->SendClientMessage(CMSG_ESTERIA_INVENTORY, payload)) lua_pushnumber(L, s_request);
    else lua_pushnil(L);
    return 1;
}

void NumberField(lua_State* L, char const* key, uint32_t value) {
    lua_pushnumber(L, value); lua_setfield(L, -2, key);
}
void BoolField(lua_State* L, char const* key, bool value) {
    lua_pushboolean(L, value); lua_setfield(L, -2, key);
}
void StringField(lua_State* L, char const* key, std::string const& value) {
    lua_pushlstring(L, value.data(), value.size()); lua_setfield(L, -2, key);
}

int32_t GetSnapshot(lua_State* L) {
    uint32_t which = 0;
    if (lua_isnumber(L, 1) && !Number(L, 1, 2, which)) return luaL_error(L, "Invalid inventory scope");
    Snapshot const& s_snapshot = s_snapshots[which];
    lua_createtable(L, 0, 21);
    NumberField(L, "scope", s_snapshot.scope); NumberField(L, "unlockCost", s_snapshot.unlockCost);
    BoolField(L, "bankUnlocked", (s_snapshot.flags & 1) != 0); BoolField(L, "atBanker", (s_snapshot.flags & 2) != 0);
    BoolField(L, "hasEars", (s_snapshot.flags & 4) != 0);
    BoolField(L, "hasKeys", (s_snapshot.flags & 8) != 0);
    BoolField(L, "specialTabs", (s_snapshot.flags & 0x10) != 0);
    NumberField(L, "request", s_snapshot.request); NumberField(L, "revision", s_snapshot.revision);
    NumberField(L, "capacity", s_snapshot.capacity); NumberField(L, "used", s_snapshot.used);
    NumberField(L, "total", s_snapshot.total); NumberField(L, "offset", s_snapshot.offset);
    NumberField(L, "price", s_snapshot.price); NumberField(L, "upgrade", s_snapshot.upgrade);
    NumberField(L, "status", s_snapshot.status); BoolField(L, "active", s_snapshot.active != 0);
    StringField(L, "message", s_snapshot.message);
    lua_createtable(L, int(s_snapshot.rows.size()), 0);
    int i = 0;
    for (Row const& row : s_snapshot.rows) {
        lua_createtable(L, 0, 17);
        NumberField(L, "guid", row.guid); NumberField(L, "entry", row.entry); NumberField(L, "count", row.count);
        if (!row.digital && row.bag == 255 && row.slot >= 86 && row.slot < 118) {
            lua_pushinteger(L, -3); lua_setfield(L, -2, "bag");
            NumberField(L, "slot", row.slot - 85);
        } else {
            NumberField(L, "bag", row.bag == 255 ? 0 : row.bag - 18);
            NumberField(L, "slot", row.bag == 255 ? row.slot - 22 : row.slot + 1);
        }
        NumberField(L, "quality", row.quality); NumberField(L, "category", row.category);
        NumberField(L, "inventoryType", row.inventoryType); NumberField(L, "durability", row.durability);
        NumberField(L, "maxDurability", row.maxDurability); BoolField(L, "digital", row.digital != 0);
        BoolField(L, "favorite", row.favorite != 0); BoolField(L, "bound", row.bound != 0); BoolField(L, "split", row.split != 0);
        StringField(L, "link", row.link);
        StringField(L, "name", row.name); StringField(L, "icon", row.icon);
        StringField(L, "tag", row.tag);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "rows");
    return 1;
}

class Lifecycle : public ClientScript {
public:
    Lifecycle() : ClientScript("DigitalInventoryLifecycle") { }
    void OnLeaveWorld() override {
        s_snapshots[0] = Snapshot{};
        s_snapshots[1] = Snapshot{};
        s_snapshots[2] = Snapshot{};
        s_request = 0;
        EsteriaInventoryClient::counts.clear();
    }
};
}

void AddDigitalInventoryExtension() {
    sExtensionMgr->RegisterMessageHandler(SMSG_ESTERIA_INVENTORY, Receive, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaInventory_Request", Request, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaInventory_GetSnapshot", GetSnapshot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_INVENTORY_UPDATE", FRAMESCRIPT_CONTEXTS_GAME);
    new Lifecycle();
}
