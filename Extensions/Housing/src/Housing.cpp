#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Extensions/Housing/src/HousingWire.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/LoadingScreen.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

// Esteria housing, Phase 1 slice (README.md). The server owns every placement; this keeps its last answers for the
// EsteriaHousing addon and sends one edit at a time so each carries the revision the previous ack returned.

namespace {
using namespace EsteriaHousingWire;

struct CatalogEntry {
    uint32_t decorId = 0, goEntry = 0, fdid = 0;
    uint16_t available = 0, placed = 0;
    uint8_t category = 0, surface = 0;
    std::string name;
};

struct PlacedEntry {
    uint64_t placedId = 0;
    uint32_t decorId = 0;
    float pos[3] = { 0, 0, 0 };
    float yaw = 0;
};

struct State {
    bool valid = false, inHouse = false, isOwner = false;
    uint32_t houseId = 0, revision = 0;
    uint16_t placed = 0, maxDecor = 0;
    float boundsMin[3] = { 0, 0, 0 }, boundsMax[3] = { 0, 0, 0 };
    std::vector<PlacedEntry> decor;
};

struct PlotInfo {
    bool valid = false;
    uint16_t plot = 0;
    uint8_t status = 0, houseElsewhere = 0;
    uint32_t price = 0;
    std::string neighborhood, owner, message;
};

struct LayoutKind { uint32_t id = 0; float size[2] = { 0, 0 }; std::string name; };
struct LayoutRoom { uint32_t kind = 0; float x = 0, y = 0; uint8_t quarter = 0; };
struct LayoutSocket { uint8_t room = 0, piece = 0; float x = 0, y = 0, nx = 0, ny = 0; std::vector<uint32_t> fits; };
struct Layout {
    bool valid = false;
    uint8_t maxRooms = 0;
    float origin[2] = { 0, 0 };
    std::vector<LayoutKind> kinds;
    std::vector<LayoutRoom> rooms;
    std::vector<LayoutSocket> sockets;
};

struct PendingEdit {
    uint8_t op = 0;
    uint64_t placedId = 0;
    uint32_t decorId = 0;
    float pos[3] = { 0, 0, 0 };
    float yaw = 0;
};

CVar* s_enabled = nullptr;
bool s_available = false;
float s_snapDegrees = 15;
std::vector<CatalogEntry> s_catalog, s_catalogNext;
State s_state;
PlotInfo s_plot;
Layout s_layout;
std::deque<PendingEdit> s_queue;
uint32_t s_seq = 0;
uint32_t s_inFlightSeq = 0;
float s_inFlightAge = 0;
bool s_helloDue = false;
float s_helloWait = 0;
constexpr float AckTimeout = 5.0f;

void Reset() {
    s_available = false;
    s_catalog.clear();
    s_catalogNext.clear();
    s_state = State{};
    s_plot = PlotInfo{};
    s_layout = Layout{};
    s_queue.clear();
    s_seq = s_inFlightSeq = 0;
}

bool Enabled() { return s_enabled && s_enabled->m_intValue != 0; }

void Signal(char const* name, char const* format = nullptr, int a = 0, int b = 0, int c = 0, int d = 0) {
    uint32_t event = sExtensionMgr->GetFrameScriptEventIndex(name);
    if (event == UINT32_MAX) return;
    if (format) FrameScript_SignalEvent(event, format, a, b, c, d);
    else FrameScript_SignalEvent(event, nullptr);
}

class Writer {
public:
    template <typename T> Writer& Put(T value) {
        uint8_t raw[sizeof(T)];
        std::memcpy(raw, &value, sizeof(T)); // x86: already little-endian
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
        if (size > 65536 || !message->FetchRead(message->m_read, size)) return;
        m_bytes.resize(size);
        message->GetData(m_bytes.data(), size);
        m_ok = true;
    }
    template <typename T> bool Get(T& result) {
        if (!m_ok || sizeof(T) > m_bytes.size() - m_offset) return m_ok = false;
        std::memcpy(&result, m_bytes.data() + m_offset, sizeof(T));
        m_offset += sizeof(T);
        return true;
    }
    bool Get(std::string& result) {
        if (!m_ok) return false;
        auto end = std::find(m_bytes.begin() + m_offset, m_bytes.end(), uint8_t(0));
        if (end == m_bytes.end()) return m_ok = false;
        result.assign(m_bytes.begin() + m_offset, end);
        m_offset = size_t(end - m_bytes.begin()) + 1;
        return true;
    }
    bool Done() const { return m_ok && m_offset == m_bytes.size(); }
private:
    std::vector<uint8_t> m_bytes;
    size_t m_offset = 0;
    bool m_ok = false;
};

void SendHello() {
    if (!Enabled()) return;
    Writer().Put<uint16_t>(Proto).Put<uint32_t>(1).Send(CMSG_HELLO);
}

void RequestCatalog(uint32_t page) { Writer().Put<uint32_t>(page).Send(CMSG_CATALOG); }

void SendNextEdit() {
    if (s_inFlightSeq || s_queue.empty()) return;
    PendingEdit const& e = s_queue.front();
    s_inFlightSeq = ++s_seq;
    s_inFlightAge = 0;
    float const half = e.yaw / 2;
    Writer w;
    w.Put<uint32_t>(s_inFlightSeq).Put<uint32_t>(s_state.revision).Put<uint8_t>(e.op).Put<uint64_t>(e.placedId)
        .Put<uint32_t>(e.decorId).Put(e.pos[0]).Put(e.pos[1]).Put(e.pos[2])
        .Put(0.0f).Put(0.0f).Put(std::sin(half)).Put(std::cos(half)).Put(1.0f);
    if (!w.Send(CMSG_EDIT)) {
        s_inFlightSeq = 0;
        s_queue.clear();
        Signal("ESTERIA_HOUSING_RESULT", "%d%d%d", int(Internal), int(e.op), 0);
    }
}

// ------------------------------------------------------------------------------------------------ server messages

int32_t ReceiveHello(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    uint16_t proto, maxDecor;
    uint8_t enabled;
    uint32_t features, catalogVersion;
    float minScale, maxScale, snap;
    if (!r.Get(proto) || !r.Get(enabled) || !r.Get(features) || !r.Get(catalogVersion) || !r.Get(maxDecor)
        || !r.Get(minScale) || !r.Get(maxScale) || !r.Get(snap) || !r.Done()) return 0;
    s_available = Enabled() && enabled && proto == Proto;
    s_snapDegrees = snap > 0 && snap < 360 ? snap : 15;
    sExtensionMgr->Log("Housing: server proto %u enabled %u catalog %u maxDecor %u", proto, enabled, catalogVersion, maxDecor);
    if (s_available) RequestCatalog(0);
    Signal("ESTERIA_HOUSING_AVAILABLE");
    return 1;
}

int32_t ReceiveCatalog(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    uint32_t page, pageCount;
    uint16_t n;
    if (!r.Get(page) || !r.Get(pageCount) || !r.Get(n)) return 0;
    if (page == 0) s_catalogNext.clear();
    for (uint16_t i = 0; i < n; ++i) {
        CatalogEntry e;
        if (!r.Get(e.decorId) || !r.Get(e.goEntry) || !r.Get(e.fdid) || !r.Get(e.available) || !r.Get(e.placed)
            || !r.Get(e.category) || !r.Get(e.surface) || !r.Get(e.name)) return 0;
        s_catalogNext.push_back(std::move(e));
    }
    if (!r.Done()) return 0;
    if (page + 1 < pageCount && pageCount < 100) {
        RequestCatalog(page + 1);
        return 1;
    }
    s_catalog = std::move(s_catalogNext);
    s_catalogNext.clear();
    Signal("ESTERIA_HOUSING_CATALOG");
    return 1;
}

int32_t ReceiveState(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    State next;
    uint8_t inHouse, isOwner;
    uint16_t n;
    if (!r.Get(inHouse) || !r.Get(isOwner) || !r.Get(next.houseId) || !r.Get(next.revision) || !r.Get(next.placed)
        || !r.Get(next.maxDecor)) return 0;
    for (float& v : next.boundsMin) if (!r.Get(v)) return 0;
    for (float& v : next.boundsMax) if (!r.Get(v)) return 0;
    if (!r.Get(n) || n > MaxStateEntries) return 0;
    for (uint16_t i = 0; i < n; ++i) {
        PlacedEntry p;
        if (!r.Get(p.placedId) || !r.Get(p.decorId) || !r.Get(p.pos[0]) || !r.Get(p.pos[1]) || !r.Get(p.pos[2])
            || !r.Get(p.yaw)) return 0;
        next.decor.push_back(p);
    }
    if (!r.Done()) return 0;
    next.valid = true;
    next.inHouse = inHouse != 0;
    next.isOwner = isOwner != 0;
    if (!next.inHouse) s_queue.clear();
    s_state = std::move(next);
    Signal("ESTERIA_HOUSING_STATE");
    return 1;
}

int32_t ReceiveAck(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    uint32_t seq, revision;
    uint8_t result;
    uint64_t placedId;
    float transform[8];
    if (!r.Get(seq) || !r.Get(result) || !r.Get(revision) || !r.Get(placedId)) return 0;
    for (float& v : transform) if (!r.Get(v)) return 0;
    if (!r.Done()) return 0;
    if (seq != s_inFlightSeq) return 1; // a late answer to an edit that already timed out
    uint8_t const op = s_queue.empty() ? 0 : s_queue.front().op;
    if (!s_queue.empty()) s_queue.pop_front();
    s_inFlightSeq = 0;
    if (result == Ok) {
        s_state.revision = revision;
        RequestCatalog(0); // owned/placed counts changed
    } else {
        s_queue.clear(); // later edits were made against a layout the server refused
        if (result == StaleRevision) Writer().Put<uint8_t>(EnterSync).Send(CMSG_ENTER);
    }
    sExtensionMgr->Log("Housing: edit seq %u op %u -> result %u revision %u placed %llu", seq, op, result, revision,
        static_cast<unsigned long long>(placedId));
    Signal("ESTERIA_HOUSING_RESULT", "%d%d%d", int(result), int(op), int(placedId));
    SendNextEdit();
    return 1;
}

int32_t ReceivePlot(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    PlotInfo next;
    if (!r.Get(next.plot) || !r.Get(next.status) || !r.Get(next.price) || !r.Get(next.houseElsewhere)
        || !r.Get(next.neighborhood) || !r.Get(next.owner) || !r.Get(next.message) || !r.Done()) return 0;
    next.valid = true;
    s_plot = std::move(next);
    Signal("ESTERIA_HOUSING_PLOT");
    return 1;
}

int32_t ReceiveLayout(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    Layout next;
    uint8_t kinds, rooms;
    uint16_t sockets;
    if (!r.Get(next.maxRooms) || !r.Get(next.origin[0]) || !r.Get(next.origin[1]) || !r.Get(kinds)) return 0;
    for (uint8_t i = 0; i < kinds; ++i) {
        LayoutKind k;
        if (!r.Get(k.id) || !r.Get(k.size[0]) || !r.Get(k.size[1]) || !r.Get(k.name)) return 0;
        next.kinds.push_back(std::move(k));
    }
    if (!r.Get(rooms)) return 0;
    for (uint8_t i = 0; i < rooms; ++i) {
        LayoutRoom room;
        if (!r.Get(room.kind) || !r.Get(room.x) || !r.Get(room.y) || !r.Get(room.quarter)) return 0;
        next.rooms.push_back(room);
    }
    if (!r.Get(sockets)) return 0;
    for (uint16_t i = 0; i < sockets; ++i) {
        LayoutSocket socket;
        uint8_t fits;
        if (!r.Get(socket.room) || !r.Get(socket.piece) || !r.Get(socket.x) || !r.Get(socket.y) || !r.Get(socket.nx)
            || !r.Get(socket.ny) || !r.Get(fits)) return 0;
        for (uint8_t f = 0; f < fits; ++f) {
            uint32_t kind;
            if (!r.Get(kind)) return 0;
            socket.fits.push_back(kind);
        }
        next.sockets.push_back(std::move(socket));
    }
    if (!r.Done()) return 0;
    next.valid = true;
    s_layout = std::move(next);
    Signal("ESTERIA_HOUSING_LAYOUT");
    return 1;
}

// ------------------------------------------------------------------------------------------------ Lua

void NumberField(lua_State* L, char const* key, double value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); }

int32_t IsAvailable(lua_State* L) { lua_pushboolean(L, s_available); return 1; }

// { inHouse, isOwner, houseId, revision, placed, maxDecor, minX..maxZ, decor = { { placedId, decorId, x, y, z, yaw } } }
// yaw in degrees. nil before the server has said anything.
int32_t GetState(lua_State* L) {
    if (!s_available || !s_state.valid) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 13);
    lua_pushboolean(L, s_state.inHouse); lua_setfield(L, -2, "inHouse");
    lua_pushboolean(L, s_state.isOwner); lua_setfield(L, -2, "isOwner");
    NumberField(L, "houseId", s_state.houseId); NumberField(L, "revision", s_state.revision);
    NumberField(L, "placed", s_state.placed); NumberField(L, "maxDecor", s_state.maxDecor);
    NumberField(L, "minX", s_state.boundsMin[0]); NumberField(L, "minY", s_state.boundsMin[1]); NumberField(L, "minZ", s_state.boundsMin[2]);
    NumberField(L, "maxX", s_state.boundsMax[0]); NumberField(L, "maxY", s_state.boundsMax[1]); NumberField(L, "maxZ", s_state.boundsMax[2]);
    NumberField(L, "pending", double(s_queue.size()));
    lua_createtable(L, int(s_state.decor.size()), 0);
    int i = 0;
    for (PlacedEntry const& p : s_state.decor) {
        lua_createtable(L, 0, 6);
        NumberField(L, "placedId", double(p.placedId)); NumberField(L, "decorId", p.decorId);
        NumberField(L, "x", p.pos[0]); NumberField(L, "y", p.pos[1]); NumberField(L, "z", p.pos[2]);
        NumberField(L, "yaw", p.yaw * 180.0 / 3.14159265358979323846);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "decor");
    return 1;
}

// { { decorId, name, available, placed, category, surface, fileDataId } }
int32_t GetCatalog(lua_State* L) {
    lua_createtable(L, int(s_catalog.size()), 0);
    int i = 0;
    for (CatalogEntry const& e : s_catalog) {
        lua_createtable(L, 0, 7);
        NumberField(L, "decorId", e.decorId); NumberField(L, "available", e.available); NumberField(L, "placed", e.placed);
        NumberField(L, "category", e.category); NumberField(L, "surface", e.surface); NumberField(L, "fileDataId", e.fdid);
        lua_pushstring(L, e.name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++i);
    }
    return 1;
}

// x, y, z, facing (degrees) of the active player in world coordinates; 3.3.5 Lua has no world position API.
int32_t GetPlayerPosition(lua_State* L) {
    CGPlayer_C* player = ClntObjMgrGetActivePlayerObj();
    if (!player) return 0;
    C3Vector const pos = player->GetPosition();
    lua_pushnumber(L, pos.x); lua_pushnumber(L, pos.y); lua_pushnumber(L, pos.z);
    lua_pushnumber(L, player->GetFacing() * 180.0 / 3.14159265358979323846);
    return 4;
}

int32_t Queue(lua_State* L, PendingEdit const& e) {
    if (!s_available || !s_state.inHouse || !s_state.isOwner || s_queue.size() >= 32) {
        lua_pushboolean(L, 0);
        return 1;
    }
    s_queue.push_back(e);
    SendNextEdit();
    lua_pushboolean(L, 1);
    return 1;
}

bool Transform(lua_State* L, int first, PendingEdit& e) {
    for (int i = 0; i < 4; ++i) if (!lua_isnumber(L, first + i)) return false;
    for (int i = 0; i < 3; ++i) e.pos[i] = float(lua_tonumber(L, first + i));
    // The server snaps too; snapping here keeps the request what the panel showed.
    double const step = s_snapDegrees, degrees = std::round(lua_tonumber(L, first + 3) / step) * step;
    e.yaw = float(degrees * 3.14159265358979323846 / 180.0);
    return true;
}

int32_t Place(lua_State* L) {
    PendingEdit e;
    e.op = OpPlace;
    if (!lua_isnumber(L, 1) || !Transform(L, 2, e)) return luaL_error(L, "Usage: EsteriaHousing_Place(decorId, x, y, z, yawDegrees)");
    e.decorId = uint32_t(lua_tonumber(L, 1));
    return Queue(L, e);
}

int32_t Move(lua_State* L) {
    PendingEdit e;
    e.op = OpMove;
    if (!lua_isnumber(L, 1) || !Transform(L, 2, e)) return luaL_error(L, "Usage: EsteriaHousing_Move(placedId, x, y, z, yawDegrees)");
    e.placedId = uint64_t(lua_tonumber(L, 1));
    return Queue(L, e);
}

int32_t Remove(lua_State* L) {
    PendingEdit e;
    e.op = OpRemove;
    if (!lua_isnumber(L, 1)) return luaL_error(L, "Usage: EsteriaHousing_Remove(placedId)");
    e.placedId = uint64_t(lua_tonumber(L, 1));
    return Queue(L, e);
}

int32_t Sync(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(EnterSync).Send(CMSG_ENTER));
    return 1;
}

int32_t Leave(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(EnterLeave).Send(CMSG_ENTER));
    return 1;
}

// { plot, status ("sale" "yours" "visit" "private"), price (copper), houseElsewhere, neighborhood, owner, message }
int32_t GetPlot(lua_State* L) {
    if (!s_available || !s_plot.valid) { lua_pushnil(L); return 1; }
    static char const* const statuses[] = { "sale", "yours", "visit", "private" };
    lua_createtable(L, 0, 7);
    NumberField(L, "plot", s_plot.plot); NumberField(L, "price", s_plot.price);
    lua_pushboolean(L, s_plot.houseElsewhere); lua_setfield(L, -2, "houseElsewhere");
    lua_pushstring(L, s_plot.status < 4 ? statuses[s_plot.status] : "private"); lua_setfield(L, -2, "status");
    lua_pushstring(L, s_plot.neighborhood.c_str()); lua_setfield(L, -2, "neighborhood");
    lua_pushstring(L, s_plot.owner.c_str()); lua_setfield(L, -2, "owner");
    lua_pushstring(L, s_plot.message.c_str()); lua_setfield(L, -2, "message");
    return 1;
}

int32_t SendAction(lua_State* L, uint8_t action) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(action).Send(CMSG_ACTION));
    return 1;
}

int32_t BuyPlot(lua_State* L) { return SendAction(L, ActionBuyPlot); }
int32_t EnterPlot(lua_State* L) { return SendAction(L, ActionEnterPlot); }
int32_t RemoveRoom(lua_State* L) { return SendAction(L, ActionRemoveRoom); }

// EsteriaHousing_AddRoom(room, piece, kind): room and piece as GetLayout's sockets give them.
int32_t AddRoom(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
        return luaL_error(L, "Usage: EsteriaHousing_AddRoom(room, piece, kind)");
    lua_pushboolean(L, s_available && s_state.isOwner && Writer().Put<uint8_t>(ActionAddRoom)
        .Put<uint8_t>(uint8_t(lua_tonumber(L, 1))).Put<uint8_t>(uint8_t(lua_tonumber(L, 2)))
        .Put<uint32_t>(uint32_t(lua_tonumber(L, 3))).Send(CMSG_ACTION));
    return 1;
}

// { maxRooms, originX, originY, kinds = { [id] = { id, name, sizeX, sizeY } },
//   rooms = { { kind, x, y, quarter } }, sockets = { { room, piece, x, y, nx, ny, fits = { kind... } } } }
// Room offsets and socket positions are relative to the origin (world = origin + offset).
int32_t GetLayout(lua_State* L) {
    if (!s_available || !s_layout.valid) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 6);
    NumberField(L, "maxRooms", s_layout.maxRooms);
    NumberField(L, "originX", s_layout.origin[0]); NumberField(L, "originY", s_layout.origin[1]);
    lua_createtable(L, 0, int(s_layout.kinds.size()));
    for (LayoutKind const& k : s_layout.kinds) {
        lua_createtable(L, 0, 4);
        NumberField(L, "id", k.id); NumberField(L, "sizeX", k.size[0]); NumberField(L, "sizeY", k.size[1]);
        lua_pushstring(L, k.name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, int(k.id));
    }
    lua_setfield(L, -2, "kinds");
    lua_createtable(L, int(s_layout.rooms.size()), 0);
    int i = 0;
    for (LayoutRoom const& room : s_layout.rooms) {
        lua_createtable(L, 0, 4);
        NumberField(L, "kind", room.kind); NumberField(L, "x", room.x); NumberField(L, "y", room.y);
        NumberField(L, "quarter", room.quarter);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "rooms");
    lua_createtable(L, int(s_layout.sockets.size()), 0);
    i = 0;
    for (LayoutSocket const& socket : s_layout.sockets) {
        lua_createtable(L, 0, 7);
        NumberField(L, "room", socket.room); NumberField(L, "piece", socket.piece);
        NumberField(L, "x", socket.x); NumberField(L, "y", socket.y); NumberField(L, "nx", socket.nx); NumberField(L, "ny", socket.ny);
        lua_createtable(L, int(socket.fits.size()), 0);
        int f = 0;
        for (uint32_t kind : socket.fits) { lua_pushnumber(L, kind); lua_rawseti(L, -2, ++f); }
        lua_setfield(L, -2, "fits");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "sockets");
    return 1;
}

// x, y, z where the mouse cursor's ray meets terrain or a world model, or nothing.
int32_t GetCursorWorld(lua_State* L) {
    CGWorldFrame* frame = CGWorldFrame::s_currentWorldFrame;
    if (!frame || !frame->m_camera || !frame->m_simpleTop) return 0;
    float x, y;
    NDCToDDC(frame->m_simpleTop->m_mousePosition.m_x, frame->m_simpleTop->m_mousePosition.m_y, &x, &y);
    // Same projection dance as CGWorldFrame::HitTestPoint: the ray needs the world camera's projection.
    CGxDevice* device = g_theGxDevicePtr;
    C44Matrix const projection = device->m_projection;
    CGxMatrixStack& stack = device->m_xforms[GxXform_View];
    C44Matrix const view = stack.m_mtx[stack.m_level];
    frame->m_camera->SetupWorldProjection(frame->m_viewportDDC);
    C3Vector start, end;
    HITTESTRESULT hit{};
    bool const found = frame->GetLineSegment(x, y, &start, &end) && frame->HitTest(start, end, 0x3, &hit) != 0;
    device->XformSetProjection(projection);
    device->XformSetView(view);
    if (!found) return 0;
    lua_pushnumber(L, hit.m_point.x); lua_pushnumber(L, hit.m_point.y); lua_pushnumber(L, hit.m_point.z);
    return 3;
}

class Maps : public WorldScript {
public:
    Maps() : WorldScript("HousingMaps") { }
    // Every login and transfer. HELLO waits for the loading screen to finish (Ticks): the server drops
    // STATUS_LOGGEDIN packets that arrive while the player is still between maps.
    void OnMapLoaded(uint32_t, char const*) override { s_helloDue = true; s_helloWait = 0; }
    void OnMapUnload() override { Reset(); }
    void OnLogoutComplete() override { Reset(); }
};

class Ticks : public ClientScript {
public:
    Ticks() : ClientScript("HousingTicks") { }
    void OnUpdate(float elapsedSec) override {
        if (s_helloDue && !LoadingScreenDrawing() && ClntObjMgrGetActivePlayerObj() && (s_helloWait += elapsedSec) >= 1.0f) {
            s_helloDue = false;
            SendHello();
        }
        if (!s_inFlightSeq || (s_inFlightAge += elapsedSec) < AckTimeout) return;
        uint8_t const op = s_queue.empty() ? 0 : s_queue.front().op;
        sExtensionMgr->Log("Housing: edit seq %u got no answer in %.0f s", s_inFlightSeq, AckTimeout);
        s_inFlightSeq = 0;
        s_queue.clear();
        Signal("ESTERIA_HOUSING_RESULT", "%d%d%d", int(Internal), int(op), 0);
    }
    void OnLeaveWorld() override { Reset(); }
};

class Lifecycle : public NetScript {
public:
    Lifecycle() : NetScript("HousingLifecycle") { }
    void OnDisconnected(NetClient&) override { Reset(); }
};
}

void AddHousingExtension() {
    s_enabled = sExtensionMgr->RegisterCVar("housingEnabled", "Esteria housing editor (needs an Esteria realm with housing on)",
        0, "0", nullptr, CATEGORY_GAME);
    sExtensionMgr->RegisterMessageHandler(SMSG_HELLO, ReceiveHello, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_CATALOG, ReceiveCatalog, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_EDIT_ACK, ReceiveAck, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_STATE, ReceiveState, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_PLOT, ReceivePlot, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_LAYOUT, ReceiveLayout, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_IsAvailable", IsAvailable, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetState", GetState, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetCatalog", GetCatalog, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetPlayerPosition", GetPlayerPosition, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Place", Place, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Move", Move, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Remove", Remove, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Sync", Sync, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Leave", Leave, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetPlot", GetPlot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_BuyPlot", BuyPlot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_EnterPlot", EnterPlot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetLayout", GetLayout, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_AddRoom", AddRoom, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_RemoveRoom", RemoveRoom, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetCursorWorld", GetCursorWorld, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_AVAILABLE", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_CATALOG", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_STATE", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_RESULT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_PLOT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_LAYOUT", FRAMESCRIPT_CONTEXTS_GAME);
    new Maps();
    new Ticks();
    new Lifecycle();
}
