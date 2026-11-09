#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Base/Coordinate.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Extensions/Housing/src/HousingWire.h"
#include "Engine/Source/Retail/BodyShape.h"
#include "Engine/Source/Retail/RetailLookWire.h"
#include "WoW/Common/ComponentCore/RetailRace.h"
#include "WoW/Source/Extension/ExtensionMgr.h"
#include "WoW/Source/LoadingScreen.h"
#include "WoW/Source/Object/ObjectClient/Player_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/World.h"
#include "Engine/Source/Model2/M2Model.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Base/Status.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/Tempest/C4Quaternion.h"
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
    uint32_t price = 0; // copper, at the house catalog
    std::string name;
};

struct PlacedEntry {
    uint64_t placedId = 0;
    uint32_t decorId = 0;
    float pos[3] = { 0, 0, 0 };
    float yaw = 0;                    // heading of rot, radians
    float rot[4] = { 0, 0, 0, 1 };    // xyzw
    float scale = 1;
};

struct State {
    bool valid = false, inHouse = false, isOwner = false, outdoors = false;
    uint32_t houseId = 0, revision = 0;
    uint16_t placed = 0, maxDecor = 0;
    float boundsMin[3] = { 0, 0, 0 }, boundsMax[3] = { 0, 0, 0 };
    std::vector<PlacedEntry> decor;
    float house[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0 }; // outdoors: spot x y z yaw, dx dy dz turn scale reach
};

struct PlotInfo {
    bool valid = false;
    uint16_t plot = 0;
    uint8_t status = 0, houseElsewhere = 0;
    uint32_t price = 0;
    std::string neighborhood, owner, message;
    uint32_t style = 0;                 // your plot: its house style, offset and turn, and the styles there are
    float dx = 0, dy = 0, dz = 0, turn = 0, scale = 1, reach = 0;
    std::vector<std::pair<uint32_t, std::string>> styles;
    struct Option { uint32_t id = 0, parent = 0; std::string name; };
    struct Part { uint8_t type = 0; uint32_t chosen = 0; std::vector<Option> options; };
    std::vector<Part> parts;            // your plot: the house parts you may swap
    struct SpotInfo { float pos[3] = { 0, 0, 0 }; uint8_t accepts = 0, type = 0; };
    std::vector<SpotInfo> spots;        // your plot: the wall and roof spots for dormers, towers and chimneys
    uint32_t hood = 0, house = 0;       // the neighborhood, and the house on the plot (0 none)
    uint8_t favorite = 0, canBuy = 0;
    std::vector<std::pair<uint32_t, std::string>> choices; // your houses you may mirror here
};

struct LayoutKind { uint32_t id = 0; float size[2] = { 0, 0 }; uint32_t price = 0; std::string name; };
struct LayoutRoom { uint32_t kind = 0; float x = 0, y = 0; uint8_t quarter = 0; uint32_t theme[3] = { 0, 0, 0 }; uint8_t ceilingHidden = 0; int8_t level = 0; };
struct LayoutTheme { uint32_t id = 0; std::string name; };
struct LayoutSocket { uint8_t room = 0, piece = 0; int8_t level = 0; float x = 0, y = 0, nx = 0, ny = 0; std::vector<uint32_t> fits; };
struct Layout {
    bool valid = false;
    uint8_t maxRooms = 0;
    float origin[2] = { 0, 0 };
    std::vector<LayoutKind> kinds;
    std::vector<LayoutRoom> rooms;
    std::vector<LayoutSocket> sockets;
    std::vector<LayoutTheme> themes;
    uint8_t maxFloors = 4;
    uint32_t floorPrice = 0, decorPrice = 0;
    uint16_t decorStep = 0, decorCap = 0;
    uint32_t roomPrice = 0;
    uint8_t roomStep = 0, roomCap = 0;
};

struct NeighborhoodPlot {
    uint16_t plot = 0;
    uint8_t status = 0;
    std::string name;
};

struct Home {
    uint32_t house = 0, hood = 0;
    uint16_t plot = 0, placed = 0, maxDecor = 0;
    std::string hoodName;
    uint8_t rooms = 0, access = 0;
};

struct HoodEntry {
    uint32_t id = 0;
    uint8_t kind = 0, role = 0, map = 0;
    uint16_t free = 0;
    std::string name;
};

struct Visit {
    uint32_t house = 0, hood = 0;
    uint16_t plot = 0;
    std::string owner, hoodName;
    uint8_t ok = 0;
};

struct Neighborhood {
    bool valid = false;
    uint32_t id = 0;
    uint8_t kind = 0, map = 0, rights = 0, here = 0, choose = 0;
    std::string name, mapName, message;
    uint32_t price = 0;
    uint8_t where = 0;
    std::vector<NeighborhoodPlot> plots;
    std::vector<Home> homes;
    std::vector<HoodEntry> hoods;
    std::vector<Visit> favorites, recent;
    std::vector<std::pair<uint32_t, std::string>> members;
    uint32_t guild = 0, guildPrice = 0, bank = 0;
    uint8_t leader = 0, canBuyGuild = 0;
    std::vector<uint8_t> rankRights;
};

struct PendingEdit {
    uint8_t op = 0;
    uint64_t placedId = 0;
    uint32_t decorId = 0;
    float pos[3] = { 0, 0, 0 };
    float rot[4] = { 0, 0, 0, 1 }; // xyzw
    float scale = 1;
};

constexpr double Pi = 3.14159265358979323846;

// Heading (radians, about Z) of a rotation quaternion xyzw.
float Heading(float const q[4]) {
    return float(std::atan2(2.0 * (q[3] * q[2] + q[0] * q[1]), 1.0 - 2.0 * (q[1] * q[1] + q[2] * q[2])));
}

CVar* s_enabled = nullptr;
bool s_available = false;
float s_snapDegrees = 15;
std::vector<CatalogEntry> s_catalog, s_catalogNext;
State s_state;
PlotInfo s_plot;
Neighborhood s_neighborhood;
Layout s_layout;
std::deque<PendingEdit> s_queue;
uint32_t s_seq = 0;
uint32_t s_inFlightSeq = 0;
float s_inFlightAge = 0;
bool s_helloDue = false;
float s_helloWait = 0;
constexpr float AckTimeout = 5.0f;

// The ghost: a client-only copy of the decor being placed, drawn where the cursor meets the floor (the way the client
// draws a ground-targeted spell's object, Spell_C_CreateSpellWorldObject).
C4Quaternion Quat(float x, float y, float z, float w) {
    C4Quaternion q;
    q.x = x; q.y = y; q.z = z; q.w = w;
    return q;
}

// A catalog model_fdid with this bit is a WMO decor (the server sets it): its ghost is a client-only map object.
constexpr uint32_t WmoDecorBit = 0x80000000u;

struct Ghost {
    CM2Model* model = nullptr;
    CMapEntity* entity = nullptr;
    CMapBaseObj* wmo = nullptr; // a WMO decor's ghost (CWorld::ObjectCreate, as a game object's WMO)
    C4Quaternion rot = Quat(0, 0, 0, 1);
    float scale = 1;
    bool onFloor = false;       // the cursor met the floor this frame (or the ghost is pinned)
    bool pinned = false;        // stays at pos instead of following the cursor
    C3Vector pos;
} s_ghost;

C44Matrix GhostMatrix() {
    C44Matrix matrix(s_ghost.rot);
    matrix.Scale(s_ghost.scale);
    matrix.d0 = s_ghost.pos.x; matrix.d1 = s_ghost.pos.y; matrix.d2 = s_ghost.pos.z;
    return matrix;
}

void DestroyGhost() {
    if (s_ghost.wmo) CWorld::ObjectDestroy(s_ghost.wmo);
    if (s_ghost.entity) World::ObjectDestroy(s_ghost.entity);
    if (s_ghost.model) s_ghost.model->Release();
    s_ghost = Ghost{};
}

int32_t UpdateGhost(void*, uint32_t, uint64_t, uintptr_t) {
    if (!s_ghost.model) return 1;
    s_ghost.model->m_fFlags |= 0x8000;
    s_ghost.model->m_worldTransform = GhostMatrix();
    s_ghost.model->m_baseAlpha = 0.7f;
    return 1;
}

// The plot boundary: retail's glowing fence (spells/fx_housingareatriggervisualsmediumplot_areatrigger.m2), stretched
// over the plot box from SMSG_STATE while the owner decorates outdoors.
constexpr uint32_t PlotBoundsFdid = 7637275;
constexpr float PlotBoundsHalfX = 35.75f, PlotBoundsHalfY = 30.6f; // the model's own half extents
struct PlotBounds {
    CM2Model* model = nullptr;
    CMapEntity* entity = nullptr;
    bool wanted = false;
} s_bounds;

C44Matrix BoundsMatrix() {
    C44Matrix matrix;
    matrix.Scale(C3Vector((s_state.boundsMax[0] - s_state.boundsMin[0]) / 2 / PlotBoundsHalfX,
                          (s_state.boundsMax[1] - s_state.boundsMin[1]) / 2 / PlotBoundsHalfY, 1));
    matrix.d0 = (s_state.boundsMin[0] + s_state.boundsMax[0]) / 2;
    matrix.d1 = (s_state.boundsMin[1] + s_state.boundsMax[1]) / 2;
    matrix.d2 = s_state.boundsMin[2] + 10; // the server's box runs from 10 yd below the plot's house spot
    return matrix;
}

void DestroyBounds() {
    if (s_bounds.entity) World::ObjectDestroy(s_bounds.entity);
    if (s_bounds.model) s_bounds.model->Release();
    s_bounds.model = nullptr;
    s_bounds.entity = nullptr;
}

int32_t UpdateBounds(void*, uint32_t, uint64_t, uintptr_t) {
    if (!s_bounds.model) return 1;
    s_bounds.model->m_fFlags |= 0x8000;
    s_bounds.model->m_worldTransform = BoundsMatrix();
    return 1;
}

void TickBounds() {
    bool const show = s_bounds.wanted && s_state.valid && s_state.inHouse && s_state.outdoors && s_state.isOwner;
    if (!show) { DestroyBounds(); return; }
    if (!s_bounds.model && World::s_m2Scene) {
        s_bounds.model = World::s_m2Scene->CreateModel(("retail/" + std::to_string(PlotBoundsFdid) + ".m2").c_str(), 0);
        if (s_bounds.model) s_bounds.entity = CMap::ObjectCreate(s_bounds.model, UpdateBounds, nullptr, 0, 0, 10);
    }
    if (!s_bounds.model || !s_bounds.entity || !s_bounds.model->IsLoaded(0, 0)) return;
    C44Matrix matrix = BoundsMatrix();
    CAaBox box = s_bounds.model->GetBoundingBox();
    CAaSphere sphere = s_bounds.model->GetBoundingSphere();
    CMap::ObjectUpdate(s_bounds.entity, matrix, box, sphere, sphere.c, false, 0xFFFFFFFF);
}

bool s_snapSurfaces = true; // the cursor lands on decor too (game objects), so decor can sit on tables and shelves
bool CursorRay(C3Vector& out);

struct Gizmo;
void HideGizmo();

void Reset() {
    DestroyGhost();
    DestroyBounds();
    s_bounds.wanted = false;
    HideGizmo();
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
    Writer& PutString(char const* text) { // NUL-terminated
        m_bytes.insert(m_bytes.end(), text, text + std::strlen(text) + 1);
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
        // a full house's SMSG_STATE: 5000 decor x 44 bytes
        if (size > (1u << 20) || !message->FetchRead(message->m_read, size)) return;
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
    Writer w;
    w.Put<uint32_t>(s_inFlightSeq).Put<uint32_t>(s_state.revision).Put<uint8_t>(e.op).Put<uint64_t>(e.placedId)
        .Put<uint32_t>(e.decorId).Put(e.pos[0]).Put(e.pos[1]).Put(e.pos[2])
        .Put(e.rot[0]).Put(e.rot[1]).Put(e.rot[2]).Put(e.rot[3]).Put(e.scale);
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
    // A page 0 (the server's push after a purchase, or our own request) starts a fresh pass; a page from an older pass
    // still arriving is dropped, so overlapping passes (a cart of purchases) cannot splice the list together.
    static uint32_t expected = 0;
    if (page == 0) {
        s_catalogNext.clear();
        expected = 0;
    }
    if (page != expected) return 1;
    ++expected;
    for (uint16_t i = 0; i < n; ++i) {
        CatalogEntry e;
        if (!r.Get(e.decorId) || !r.Get(e.goEntry) || !r.Get(e.fdid) || !r.Get(e.available) || !r.Get(e.placed)
            || !r.Get(e.category) || !r.Get(e.surface) || !r.Get(e.price) || !r.Get(e.name)) return 0;
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
    uint8_t inHouse, isOwner, outdoors;
    uint16_t n;
    if (!r.Get(inHouse) || !r.Get(isOwner) || !r.Get(outdoors) || !r.Get(next.houseId) || !r.Get(next.revision) || !r.Get(next.placed)
        || !r.Get(next.maxDecor)) return 0;
    for (float& v : next.boundsMin) if (!r.Get(v)) return 0;
    for (float& v : next.boundsMax) if (!r.Get(v)) return 0;
    if (!r.Get(n) || n > MaxStateEntries) return 0;
    for (uint16_t i = 0; i < n; ++i) {
        PlacedEntry p;
        if (!r.Get(p.placedId) || !r.Get(p.decorId) || !r.Get(p.pos[0]) || !r.Get(p.pos[1]) || !r.Get(p.pos[2])
            || !r.Get(p.rot[0]) || !r.Get(p.rot[1]) || !r.Get(p.rot[2]) || !r.Get(p.rot[3]) || !r.Get(p.scale)) return 0;
        p.yaw = Heading(p.rot);
        next.decor.push_back(p);
    }
    for (float& v : next.house) if (!r.Get(v)) return 0;
    if (!r.Done()) return 0;
    next.valid = true;
    next.inHouse = inHouse != 0;
    next.isOwner = isOwner != 0;
    next.outdoors = outdoors != 0;
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
        || !r.Get(next.neighborhood) || !r.Get(next.owner) || !r.Get(next.message)) return 0;
    uint8_t styles;
    if (!r.Get(next.style) || !r.Get(next.dx) || !r.Get(next.dy) || !r.Get(next.dz) || !r.Get(next.turn) || !r.Get(next.scale)
        || !r.Get(next.reach) || !r.Get(styles))
        return 0;
    for (uint8_t i = 0; i < styles; ++i) {
        std::pair<uint32_t, std::string> style;
        if (!r.Get(style.first) || !r.Get(style.second)) return 0;
        next.styles.push_back(std::move(style));
    }
    uint8_t parts;
    if (!r.Get(parts)) return 0;
    for (uint8_t i = 0; i < parts; ++i) {
        PlotInfo::Part part;
        uint16_t options;
        if (!r.Get(part.type) || !r.Get(part.chosen) || !r.Get(options)) return 0;
        for (uint16_t k = 0; k < options; ++k) {
            PlotInfo::Option o;
            if (!r.Get(o.id) || !r.Get(o.parent) || !r.Get(o.name)) return 0;
            part.options.push_back(std::move(o));
        }
        next.parts.push_back(std::move(part));
    }
    uint8_t spots;
    if (!r.Get(spots)) return 0;
    for (uint8_t i = 0; i < spots; ++i) {
        PlotInfo::SpotInfo spot;
        if (!r.Get(spot.pos[0]) || !r.Get(spot.pos[1]) || !r.Get(spot.pos[2]) || !r.Get(spot.accepts) || !r.Get(spot.type)) return 0;
        next.spots.push_back(spot);
    }
    uint8_t choices;
    if (!r.Get(next.hood) || !r.Get(next.favorite) || !r.Get(next.house) || !r.Get(next.canBuy) || !r.Get(choices)) return 0;
    for (uint8_t i = 0; i < choices; ++i) {
        std::pair<uint32_t, std::string> c;
        if (!r.Get(c.first) || !r.Get(c.second)) return 0;
        next.choices.push_back(std::move(c));
    }
    if (!r.Done()) return 0;
    next.valid = true;
    s_plot = std::move(next);
    Signal("ESTERIA_HOUSING_PLOT");
    return 1;
}

int32_t ReceiveNeighborhood(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    Neighborhood next;
    uint16_t plots;
    if (!r.Get(next.id) || !r.Get(next.kind) || !r.Get(next.map) || !r.Get(next.name) || !r.Get(next.mapName)
        || !r.Get(next.price) || !r.Get(next.where) || !r.Get(next.message) || !r.Get(next.rights) || !r.Get(next.here)
        || !r.Get(next.choose) || !r.Get(plots)) return 0;
    for (uint16_t i = 0; i < plots; ++i) {
        NeighborhoodPlot p;
        if (!r.Get(p.plot) || !r.Get(p.status) || !r.Get(p.name)) return 0;
        next.plots.push_back(std::move(p));
    }
    uint8_t n;
    if (!r.Get(n)) return 0;
    for (uint8_t i = 0; i < n; ++i) {
        Home h;
        if (!r.Get(h.house) || !r.Get(h.hood) || !r.Get(h.plot) || !r.Get(h.hoodName) || !r.Get(h.rooms) || !r.Get(h.placed)
            || !r.Get(h.maxDecor) || !r.Get(h.access)) return 0;
        next.homes.push_back(std::move(h));
    }
    if (!r.Get(n)) return 0;
    for (uint8_t i = 0; i < n; ++i) {
        HoodEntry h;
        if (!r.Get(h.id) || !r.Get(h.kind) || !r.Get(h.role) || !r.Get(h.map) || !r.Get(h.free) || !r.Get(h.name)) return 0;
        next.hoods.push_back(std::move(h));
    }
    for (std::vector<Visit>* list : { &next.favorites, &next.recent }) {
        if (!r.Get(n)) return 0;
        for (uint8_t i = 0; i < n; ++i) {
            Visit v;
            if (!r.Get(v.house) || !r.Get(v.hood) || !r.Get(v.plot) || !r.Get(v.owner) || !r.Get(v.hoodName) || !r.Get(v.ok)) return 0;
            list->push_back(std::move(v));
        }
    }
    if (!r.Get(n)) return 0;
    for (uint8_t i = 0; i < n; ++i) {
        std::pair<uint32_t, std::string> m;
        if (!r.Get(m.first) || !r.Get(m.second)) return 0;
        next.members.push_back(std::move(m));
    }
    if (!r.Get(next.guild) || !r.Get(next.leader) || !r.Get(next.guildPrice) || !r.Get(next.bank) || !r.Get(next.canBuyGuild)
        || !r.Get(n)) return 0;
    for (uint8_t i = 0; i < n; ++i) {
        uint8_t rights;
        if (!r.Get(rights)) return 0;
        next.rankRights.push_back(rights);
    }
    if (!r.Done()) return 0;
    next.valid = true;
    s_neighborhood = std::move(next);
    Signal("ESTERIA_HOUSING_NEIGHBORHOOD");
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
        if (!r.Get(k.id) || !r.Get(k.size[0]) || !r.Get(k.size[1]) || !r.Get(k.price) || !r.Get(k.name)) return 0;
        next.kinds.push_back(std::move(k));
    }
    if (!r.Get(rooms)) return 0;
    for (uint8_t i = 0; i < rooms; ++i) {
        LayoutRoom room;
        if (!r.Get(room.kind) || !r.Get(room.x) || !r.Get(room.y) || !r.Get(room.quarter) || !r.Get(room.theme[0])
            || !r.Get(room.theme[1]) || !r.Get(room.theme[2]) || !r.Get(room.ceilingHidden) || !r.Get(room.level)) return 0;
        next.rooms.push_back(room);
    }
    if (!r.Get(sockets)) return 0;
    for (uint16_t i = 0; i < sockets; ++i) {
        LayoutSocket socket;
        uint8_t fits;
        if (!r.Get(socket.room) || !r.Get(socket.piece) || !r.Get(socket.level) || !r.Get(socket.x) || !r.Get(socket.y) || !r.Get(socket.nx)
            || !r.Get(socket.ny) || !r.Get(fits)) return 0;
        for (uint8_t f = 0; f < fits; ++f) {
            uint32_t kind;
            if (!r.Get(kind)) return 0;
            socket.fits.push_back(kind);
        }
        next.sockets.push_back(std::move(socket));
    }
    uint8_t themes;
    if (!r.Get(themes)) return 0;
    for (uint8_t i = 0; i < themes; ++i) {
        LayoutTheme theme;
        if (!r.Get(theme.id) || !r.Get(theme.name)) return 0;
        next.themes.push_back(std::move(theme));
    }
    if (!r.Get(next.maxFloors) || !r.Get(next.floorPrice) || !r.Get(next.decorPrice) || !r.Get(next.decorStep)
        || !r.Get(next.decorCap) || !r.Get(next.roomPrice) || !r.Get(next.roomStep) || !r.Get(next.roomCap)) return 0;
    if (!r.Done()) return 0;
    next.valid = true;
    s_layout = std::move(next);
    Signal("ESTERIA_HOUSING_LAYOUT");
    return 1;
}

// A shop or commission answer, held for Commerce.lua: the server and the addon are the only ones who read it.
std::string s_commerce;

int32_t ReceiveCommerce(void*, NETMESSAGE, uint32_t, CDataStore* message) {
    Reader r(message);
    if (r.Get(s_commerce)) Signal("ESTERIA_HOUSING_COMMERCE");
    return 1;
}

// ------------------------------------------------------------------------------------------------ Lua

void NumberField(lua_State* L, char const* key, double value) { lua_pushnumber(L, value); lua_setfield(L, -2, key); }

int32_t IsAvailable(lua_State* L) { lua_pushboolean(L, s_available); return 1; }

int32_t Commerce(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionCommerce).PutString(luaL_checkstring(L, 1)).Send(CMSG_ACTION));
    return 1;
}

int32_t GetCommerce(lua_State* L) { lua_pushstring(L, s_commerce.c_str()); return 1; }

// ---- the merchant being customized: the real NPC in the world is the preview
uint64_t s_merchant = 0;
BodyShape::Values s_merchantBody;

CCharacterComponent* MerchantComponent() {
    WOWGUID guid;
    guid.m_guid = s_merchant;
    auto* unit = s_merchant ? static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPEMASK_UNIT, __FILE__, __LINE__)) : nullptr;
    return unit ? unit->m_characterComponent : nullptr;
}

std::string ToHex(uint8_t const* bytes, size_t size) {
    static char const digits[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
    return out;
}

bool FromHex(char const* text, uint8_t* out, size_t size) {
    if (!text || std::strlen(text) != size * 2) return false;
    for (size_t i = 0; i < size; ++i) {
        char pair[3] = { text[2 * i], text[2 * i + 1], 0 };
        if (!std::isxdigit(uint8_t(pair[0])) || !std::isxdigit(uint8_t(pair[1]))) return false;
        out[i] = uint8_t(std::strtoul(pair, nullptr, 16));
    }
    return true;
}

// EsteriaHousing_SetMerchant("hex guid")
int32_t SetMerchant(lua_State* L) {
    s_merchant = std::strtoull(luaL_checkstring(L, 1), nullptr, 16);
    s_merchantBody = BodyShape::Values();
    return 0;
}

// EsteriaHousing_MerchantEA("EA_GET", option, ...): the character creator's option commands, on the merchant.
int32_t MerchantEA(lua_State* L) {
    int const results = RetailRaceCycleCommand(L, MerchantComponent(), luaL_checkstring(L, 1));
    return results < 0 ? 0 : results;
}

// EsteriaHousing_MerchantBody(["BS:hex"]) -> the body code; with one, applies it. The code is the sliders' own preset format.
int32_t MerchantBody(lua_State* L) {
    if (lua_isstring(L, 1)) {
        char const* code = lua_tostring(L, 1);
        uint8_t payload[CharacterBodyShape::PayloadSize];
        CharacterBodyShape::Values wire{};
        if (std::strlen(code) > 3 && FromHex(code + 3, payload, sizeof(payload)) && CharacterBodyShape::ReadPayload(payload, sizeof(payload), wire)) {
            s_merchantBody = BodyShape::FromWire(wire);
            BodyShape::SetUnit(s_merchant, s_merchantBody);
        }
    }
    uint8_t payload[CharacterBodyShape::PayloadSize];
    CharacterBodyShape::WritePayload(payload, BodyShape::ToWire(s_merchantBody));
    lua_pushstring(L, ("BS:" + ToHex(payload, sizeof(payload))).c_str());
    return 1;
}

// EsteriaHousing_MerchantSlider("Height"[, value]) -> value
int32_t MerchantSlider(lua_State* L) {
    char const* name = luaL_checkstring(L, 1);
    int32_t const found = BodyShape::FindSlider(name);
    if (found < 0) return 0;
    if (lua_isnumber(L, 2)) {
        s_merchantBody.v[name] = std::max(BodyShape::Sliders()[found].minValue, std::min(1.0f, float(lua_tonumber(L, 2))));
        BodyShape::SetUnit(s_merchant, s_merchantBody);
    }
    lua_pushnumber(L, s_merchantBody.Get(name));
    return 1;
}

// EsteriaHousing_MerchantLook() -> hex of the look to save, nil until the merchant has finished loading
int32_t MerchantLook(lua_State* L) {
    CharacterRetailLook::Look look;
    if (!RetailRaceCaptureLook(MerchantComponent(), look)) return 0;
    uint8_t payload[CharacterRetailLook::PayloadSize];
    CharacterRetailLook::WritePayload(payload, look);
    lua_pushstring(L, ToHex(payload, sizeof(payload)).c_str());
    return 1;
}

int32_t RaceSupported(lua_State* L) { lua_pushboolean(L, RetailRaceMapped(uint32_t(luaL_checknumber(L, 1)))); return 1; }

// { inHouse, isOwner, houseId, revision, placed, maxDecor, minX..maxZ, decor = { { placedId, decorId, x, y, z, yaw } } }
// yaw in degrees. nil before the server has said anything.
int32_t GetState(lua_State* L) {
    if (!s_available || !s_state.valid) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 13);
    lua_pushboolean(L, s_state.inHouse); lua_setfield(L, -2, "inHouse");
    lua_pushboolean(L, s_state.isOwner); lua_setfield(L, -2, "isOwner");
    lua_pushboolean(L, s_state.outdoors); lua_setfield(L, -2, "outdoors");
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
        NumberField(L, "yaw", std::fmod(p.yaw * 180.0 / Pi + 360.0, 360.0));
        NumberField(L, "qx", p.rot[0]); NumberField(L, "qy", p.rot[1]); NumberField(L, "qz", p.rot[2]); NumberField(L, "qw", p.rot[3]);
        NumberField(L, "scale", p.scale);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "decor");
    if (s_state.outdoors) { // your plot's house, for the editor's handles
        static char const* const names[] = { "spotX", "spotY", "spotZ", "spotYaw", "dx", "dy", "dz", "turn", "scale", "reach" };
        lua_createtable(L, 0, 10);
        for (int k = 0; k < 10; ++k) NumberField(L, names[k], s_state.house[k]);
        lua_setfield(L, -2, "house");
    }
    return 1;
}

// { { decorId, name, available, placed, category, surface, fileDataId, price } }
int32_t GetCatalog(lua_State* L) {
    lua_createtable(L, int(s_catalog.size()), 0);
    int i = 0;
    for (CatalogEntry const& e : s_catalog) {
        lua_createtable(L, 0, 7);
        NumberField(L, "decorId", e.decorId); NumberField(L, "available", e.available); NumberField(L, "placed", e.placed);
        NumberField(L, "category", e.category); NumberField(L, "surface", e.surface); NumberField(L, "fileDataId", e.fdid);
        NumberField(L, "price", e.price);
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

// x, y, z, then either yawDegrees (snapped like the server snaps a turn about Z) or qx, qy, qz, qw, scale.
bool Transform(lua_State* L, int first, PendingEdit& e) {
    for (int i = 0; i < 4; ++i) if (!lua_isnumber(L, first + i)) return false;
    for (int i = 0; i < 3; ++i) e.pos[i] = float(lua_tonumber(L, first + i));
    // first .. first+2 position, first+3 .. first+6 the quaternion, first+7 the scale
    if (lua_isnumber(L, first + 6)) {
        for (int i = 0; i < 4; ++i) e.rot[i] = float(lua_tonumber(L, first + 3 + i));
        e.scale = lua_isnumber(L, first + 7) ? float(lua_tonumber(L, first + 7)) : 1.0f;
        return true;
    }
    double const step = s_snapDegrees, degrees = std::round(lua_tonumber(L, first + 3) / step) * step;
    double const half = degrees * Pi / 360.0;
    e.rot[0] = e.rot[1] = 0;
    e.rot[2] = float(std::sin(half));
    e.rot[3] = float(std::cos(half));
    return true;
}

int32_t Place(lua_State* L) {
    PendingEdit e;
    e.op = OpPlace;
    if (!lua_isnumber(L, 1) || !Transform(L, 2, e)) return luaL_error(L, "Usage: EsteriaHousing_Place(decorId, x, y, z, yawDegrees | qx, qy, qz, qw, scale)");
    e.decorId = uint32_t(lua_tonumber(L, 1));
    return Queue(L, e);
}

int32_t Move(lua_State* L) {
    PendingEdit e;
    e.op = OpMove;
    if (!lua_isnumber(L, 1) || !Transform(L, 2, e)) return luaL_error(L, "Usage: EsteriaHousing_Move(placedId, x, y, z, yawDegrees | qx, qy, qz, qw, scale)");
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
    NumberField(L, "style", s_plot.style); NumberField(L, "dx", s_plot.dx); NumberField(L, "dy", s_plot.dy);
    NumberField(L, "turn", s_plot.turn); NumberField(L, "reach", s_plot.reach);
    NumberField(L, "dz", s_plot.dz); NumberField(L, "scale", s_plot.scale);
    lua_createtable(L, int(s_plot.styles.size()), 0);
    int i = 0;
    for (auto const& [id, name] : s_plot.styles) {
        lua_createtable(L, 0, 2);
        NumberField(L, "id", id);
        lua_pushstring(L, name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "styles");
    // parts = { { type, chosen, options = { { id, parent, name } } } }: type 9 walls 10 roof 11 door 12 window 16 chimney
    lua_createtable(L, int(s_plot.parts.size()), 0);
    i = 0;
    for (PlotInfo::Part const& part : s_plot.parts) {
        lua_createtable(L, 0, 3);
        NumberField(L, "type", part.type); NumberField(L, "chosen", part.chosen);
        lua_createtable(L, int(part.options.size()), 0);
        int k = 0;
        for (PlotInfo::Option const& o : part.options) {
            lua_createtable(L, 0, 3);
            NumberField(L, "id", o.id); NumberField(L, "parent", o.parent);
            lua_pushstring(L, o.name.c_str()); lua_setfield(L, -2, "name");
            lua_rawseti(L, -2, ++k);
        }
        lua_setfield(L, -2, "options");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "parts");
    // spots = { { x, y, z, dormer, tower, chimney (what it takes), type (0 empty, 14 dormer, 15 tower, 16 chimney) } }
    lua_createtable(L, int(s_plot.spots.size()), 0);
    i = 0;
    for (PlotInfo::SpotInfo const& spot : s_plot.spots) {
        lua_createtable(L, 0, 7);
        NumberField(L, "x", spot.pos[0]); NumberField(L, "y", spot.pos[1]); NumberField(L, "z", spot.pos[2]);
        lua_pushboolean(L, spot.accepts & SpotDormer); lua_setfield(L, -2, "dormer");
        lua_pushboolean(L, spot.accepts & SpotTower); lua_setfield(L, -2, "tower");
        lua_pushboolean(L, spot.accepts & SpotChimney); lua_setfield(L, -2, "chimney");
        NumberField(L, "type", spot.type);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "spots");
    NumberField(L, "hood", s_plot.hood); NumberField(L, "house", s_plot.house);
    lua_pushboolean(L, s_plot.favorite); lua_setfield(L, -2, "favorite");
    lua_pushboolean(L, s_plot.canBuy); lua_setfield(L, -2, "canBuy");
    // choices = { { house, label } }: your houses not in this neighborhood yet, to mirror here instead of a new one
    lua_createtable(L, int(s_plot.choices.size()), 0);
    i = 0;
    for (auto const& [house, label] : s_plot.choices) {
        lua_createtable(L, 0, 2);
        NumberField(L, "house", house);
        lua_pushstring(L, label.c_str()); lua_setfield(L, -2, "label");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "choices");
    return 1;
}

// EsteriaHousing_SetSurfaceSnap(on): whether placing lands on decor (tables, shelves) as well as floors and walls.
int32_t SetSurfaceSnap(lua_State* L) {
    s_snapSurfaces = lua_toboolean(L, 1) != 0;
    return 0;
}

// EsteriaHousing_SetPlotSpot(spot, type): what stands on that wall or roof spot (as GetPlot lists them); 0 empties it.
int32_t SetPlotSpot(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionPlotSpot).Put<uint8_t>(uint8_t(luaL_checknumber(L, 1) - 1))
        .Put<uint8_t>(uint8_t(luaL_checknumber(L, 2))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_SetPlotPart(type, component): swaps that part of your plot's house (plot window open); 0 is the default.
int32_t SetPlotPart(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionPlotFixture).Put<uint8_t>(uint8_t(luaL_checknumber(L, 1)))
        .Put<uint32_t>(uint32_t(luaL_checknumber(L, 2))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_SetPlotStyle(style): rebuilds your plot's house in that exterior style (plot window open).
int32_t SetPlotStyle(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionPlotStyle).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_PlacePlotHouse(dx, dy, turnDegrees[, dz, scale]): where your house stands, yd north (dx), west (dy) and up
// (dz) of its spot, and its size. From the plot window, or the outdoor editor's handles while you stand on your plot.
int32_t PlacePlotHouse(lua_State* L) {
    float const dz = lua_isnumber(L, 4) ? float(lua_tonumber(L, 4)) : 0.0f;
    float const scale = lua_isnumber(L, 5) ? float(lua_tonumber(L, 5)) : 1.0f;
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionPlotPlace).Put(float(luaL_checknumber(L, 1)))
        .Put(float(luaL_checknumber(L, 2))).Put(dz).Put(float(luaL_checknumber(L, 3))).Put(scale).Send(CMSG_ACTION));
    return 1;
}

int32_t SendAction(lua_State* L, uint8_t action) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(action).Send(CMSG_ACTION));
    return 1;
}

void AccessTable(lua_State* L, uint8_t access) {
    lua_createtable(L, 0, 4);
    lua_pushboolean(L, access & AccessParty); lua_setfield(L, -2, "party");
    lua_pushboolean(L, access & AccessFriends); lua_setfield(L, -2, "friends");
    lua_pushboolean(L, access & AccessGuild); lua_setfield(L, -2, "guild");
    lua_pushboolean(L, access & AccessEveryone); lua_setfield(L, -2, "everyone");
}

void RightsTable(lua_State* L, uint8_t rights) {
    lua_createtable(L, 0, 3);
    lua_pushboolean(L, rights & RightBuy); lua_setfield(L, -2, "buy");
    lua_pushboolean(L, rights & RightInvite); lua_setfield(L, -2, "invite");
    lua_pushboolean(L, rights & RightManage); lua_setfield(L, -2, "manage");
}

void VisitList(lua_State* L, std::vector<Visit> const& list) {
    lua_createtable(L, int(list.size()), 0);
    int i = 0;
    for (Visit const& v : list) {
        lua_createtable(L, 0, 6);
        NumberField(L, "house", v.house); NumberField(L, "hood", v.hood); NumberField(L, "plot", v.plot);
        lua_pushstring(L, v.owner.c_str()); lua_setfield(L, -2, "owner");
        lua_pushstring(L, v.hoodName.c_str()); lua_setfield(L, -2, "hoodName");
        lua_pushboolean(L, v.ok); lua_setfield(L, -2, "ok");
        lua_rawseti(L, -2, ++i);
    }
}

// The Neighborhood window's contents (HousingWire.h SMSG_NEIGHBORHOOD), nil until the first answer:
// { id, kind ("account" "guild"), map (0 Founder's Point, 1 Razorwind Shores), name, mapName, price, where ("away"
//   "neighborhood" "house"), message, rights = { buy, invite, manage }, here,
//   plots = { { plot, status (as GetPlot), name } },
//   homes = { { house, hood (0 saved), plot, hoodName, rooms, placed, maxDecor, access = { party, friends, guild, everyone } } },
//   hoods = { { id, kind, role ("own" "member" "guild" "visit"), map, free, name } },
//   favorites, recent = { { house, hood, plot (0 gone), owner, hoodName, ok } }, members = { { account, name } },
//   guild = { id, leader, price, bank, canBuy, ranks = { rights tables, rank 1.. } } or nil }
int32_t GetNeighborhood(lua_State* L) {
    if (!s_available || !s_neighborhood.valid) { lua_pushnil(L); return 1; }
    Neighborhood const& n = s_neighborhood;
    static char const* const wheres[] = { "away", "neighborhood", "house" };
    static char const* const statuses[] = { "sale", "yours", "visit", "private" };
    static char const* const roles[] = { "own", "member", "guild", "visit" };
    lua_createtable(L, 0, 20);
    NumberField(L, "id", n.id); NumberField(L, "map", n.map); NumberField(L, "price", n.price);
    lua_pushstring(L, n.kind ? "guild" : "account"); lua_setfield(L, -2, "kind");
    lua_pushstring(L, n.name.c_str()); lua_setfield(L, -2, "name");
    lua_pushstring(L, n.mapName.c_str()); lua_setfield(L, -2, "mapName");
    lua_pushstring(L, wheres[n.where < 3 ? n.where : 0]); lua_setfield(L, -2, "where");
    lua_pushstring(L, n.message.c_str()); lua_setfield(L, -2, "message");
    lua_pushboolean(L, n.here); lua_setfield(L, -2, "here");
    lua_pushboolean(L, n.choose); lua_setfield(L, -2, "choose"); // pick your account's neighborhood first
    RightsTable(L, n.rights); lua_setfield(L, -2, "rights");
    lua_createtable(L, int(n.plots.size()), 0);
    int i = 0;
    for (NeighborhoodPlot const& p : n.plots) {
        lua_createtable(L, 0, 3);
        NumberField(L, "plot", p.plot);
        lua_pushstring(L, statuses[p.status < 4 ? p.status : 3]); lua_setfield(L, -2, "status");
        lua_pushstring(L, p.name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "plots");
    lua_createtable(L, int(n.homes.size()), 0);
    i = 0;
    for (Home const& h : n.homes) {
        lua_createtable(L, 0, 8);
        NumberField(L, "house", h.house); NumberField(L, "hood", h.hood); NumberField(L, "plot", h.plot);
        NumberField(L, "rooms", h.rooms); NumberField(L, "placed", h.placed); NumberField(L, "maxDecor", h.maxDecor);
        lua_pushstring(L, h.hoodName.c_str()); lua_setfield(L, -2, "hoodName");
        AccessTable(L, h.access); lua_setfield(L, -2, "access");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "homes");
    lua_createtable(L, int(n.hoods.size()), 0);
    i = 0;
    for (HoodEntry const& h : n.hoods) {
        lua_createtable(L, 0, 6);
        NumberField(L, "id", h.id); NumberField(L, "map", h.map); NumberField(L, "free", h.free);
        lua_pushstring(L, h.kind ? "guild" : "account"); lua_setfield(L, -2, "kind");
        lua_pushstring(L, roles[h.role < 4 ? h.role : 3]); lua_setfield(L, -2, "role");
        lua_pushstring(L, h.name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "hoods");
    VisitList(L, n.favorites); lua_setfield(L, -2, "favorites");
    VisitList(L, n.recent); lua_setfield(L, -2, "recent");
    lua_createtable(L, int(n.members.size()), 0);
    i = 0;
    for (auto const& [account, name] : n.members) {
        lua_createtable(L, 0, 2);
        NumberField(L, "account", account);
        lua_pushstring(L, name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "members");
    if (n.guild) {
        lua_createtable(L, 0, 6);
        NumberField(L, "id", n.guild); NumberField(L, "price", n.guildPrice); NumberField(L, "bank", n.bank);
        lua_pushboolean(L, n.leader); lua_setfield(L, -2, "leader");
        lua_pushboolean(L, n.canBuyGuild); lua_setfield(L, -2, "canBuy");
        lua_createtable(L, int(n.rankRights.size()), 0);
        i = 0;
        for (uint8_t rights : n.rankRights) {
            RightsTable(L, rights);
            lua_rawseti(L, -2, ++i);
        }
        lua_setfield(L, -2, "ranks");
        lua_setfield(L, -2, "guild");
    }
    return 1;
}

uint32_t OptId(lua_State* L, int index) { return lua_isnumber(L, index) ? uint32_t(lua_tonumber(L, index)) : 0; }

// EsteriaHousing_RequestNeighborhood([hood]): the window for that neighborhood (default: the one you are in, else yours).
int32_t RequestNeighborhood(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionNeighborhood).Put<uint32_t>(OptId(L, 1)).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_TravelNeighborhood([hood]): to its entry (default: your own neighborhood).
int32_t TravelNeighborhood(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionTravelNeighborhood).Put<uint32_t>(OptId(L, 1)).Send(CMSG_ACTION));
    return 1;
}

int32_t TravelBack(lua_State* L) { return SendAction(L, ActionTravelBack); }

// EsteriaHousing_SetHouseAccess(house, party, friends, guild, everyone): who may visit that house of yours. The answer
// is another ESTERIA_HOUSING_NEIGHBORHOOD.
int32_t SetHouseAccess(lua_State* L) {
    uint8_t const access = (lua_toboolean(L, 2) ? AccessParty : 0) | (lua_toboolean(L, 3) ? AccessFriends : 0)
        | (lua_toboolean(L, 4) ? AccessGuild : 0) | (lua_toboolean(L, 5) ? AccessEveryone : 0);
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionHouseAccess).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Put<uint8_t>(access).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_TravelPlot(hood, plot): plot as GetNeighborhood gives it. The server refuses what isn't allowed.
int32_t TravelPlot(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionTravelPlot).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Put<uint16_t>(uint16_t(luaL_checknumber(L, 2))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_BuyPlot([house]): buy the viewed plot for a new house, or to mirror one of yours (GetPlot().choices).
int32_t BuyPlot(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionBuyPlot).Put<uint32_t>(OptId(L, 1)).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_SetFavorite(house, hood, on)
int32_t SetFavorite(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionFavorite).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Put<uint32_t>(uint32_t(luaL_checknumber(L, 2))).Put<uint8_t>(lua_toboolean(L, 3) ? 1 : 0).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_Invite(hood, characterName) / EsteriaHousing_Uninvite(hood, account)
int32_t Invite(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionInvite).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .PutString(luaL_checkstring(L, 2)).Send(CMSG_ACTION));
    return 1;
}

int32_t Uninvite(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionUninvite).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Put<uint32_t>(uint32_t(luaL_checknumber(L, 2))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_MoveNeighborhood(hood, map): 0 Founder's Point, 1 Razorwind Shores.
int32_t MoveNeighborhood(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionHoodMap).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Put<uint8_t>(uint8_t(luaL_checknumber(L, 2))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_BuyGuildNeighborhood(map): the Guild Master, from the guild bank.
int32_t BuyGuildNeighborhood(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionBuyGuildHood).Put<uint8_t>(uint8_t(luaL_checknumber(L, 1)))
        .Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_SetGuildRankRights(rank, buy, invite, manage): rank 1.. (as GuildControlGetRankName(rank + 1)).
int32_t SetGuildRankRights(lua_State* L) {
    uint8_t const rights = (lua_toboolean(L, 2) ? RightBuy : 0) | (lua_toboolean(L, 3) ? RightInvite : 0)
        | (lua_toboolean(L, 4) ? RightManage : 0);
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionGuildRights).Put<uint8_t>(uint8_t(luaL_checknumber(L, 1)))
        .Put<uint8_t>(rights).Send(CMSG_ACTION));
    return 1;
}

int32_t MoveOut(lua_State* L) { return SendAction(L, ActionMoveOut); }

// EsteriaHousing_EnterHouse(house): into one of your houses, wherever you are (as travel allows).
int32_t EnterHouse(lua_State* L) {
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionEnterHouse).Put<uint32_t>(uint32_t(luaL_checknumber(L, 1)))
        .Send(CMSG_ACTION));
    return 1;
}
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

// { maxRooms, originX, originY, kinds = { [id] = { id, name, sizeX, sizeY, price } },
//   rooms = { { kind, x, y, quarter, wallTheme, floorTheme, ceilingTheme, ceilingHidden, level } },
//   sockets = { { room, piece, level, x, y, nx, ny, fits = { kind... } } }, themes = { { id, name } } }
// level is the floor, 0 the ground and -1 the basement; floors are 12 yd apart. A fits kind above 1000 builds that
// stairwell down from the socket ("Stairs Down" in kinds).
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
        NumberField(L, "price", k.price);
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
        NumberField(L, "wallTheme", room.theme[0]); NumberField(L, "floorTheme", room.theme[1]);
        NumberField(L, "ceilingTheme", room.theme[2]);
        lua_pushboolean(L, room.ceilingHidden); lua_setfield(L, -2, "ceilingHidden");
        NumberField(L, "level", room.level);
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "rooms");
    lua_createtable(L, int(s_layout.sockets.size()), 0);
    i = 0;
    for (LayoutSocket const& socket : s_layout.sockets) {
        lua_createtable(L, 0, 7);
        NumberField(L, "room", socket.room); NumberField(L, "piece", socket.piece); NumberField(L, "level", socket.level);
        NumberField(L, "x", socket.x); NumberField(L, "y", socket.y); NumberField(L, "nx", socket.nx); NumberField(L, "ny", socket.ny);
        lua_createtable(L, int(socket.fits.size()), 0);
        int f = 0;
        for (uint32_t kind : socket.fits) { lua_pushnumber(L, kind); lua_rawseti(L, -2, ++f); }
        lua_setfield(L, -2, "fits");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "sockets");
    lua_createtable(L, int(s_layout.themes.size()), 0);
    i = 0;
    for (LayoutTheme const& theme : s_layout.themes) {
        lua_createtable(L, 0, 2);
        NumberField(L, "id", theme.id);
        lua_pushstring(L, theme.name.c_str()); lua_setfield(L, -2, "name");
        lua_rawseti(L, -2, ++i);
    }
    lua_setfield(L, -2, "themes");
    NumberField(L, "maxFloors", s_layout.maxFloors); NumberField(L, "floorPrice", s_layout.floorPrice);
    NumberField(L, "decorPrice", s_layout.decorPrice); NumberField(L, "decorStep", s_layout.decorStep);
    NumberField(L, "decorCap", s_layout.decorCap);
    NumberField(L, "roomPrice", s_layout.roomPrice); NumberField(L, "roomStep", s_layout.roomStep);
    NumberField(L, "roomCap", s_layout.roomCap);
    return 1;
}

// EsteriaHousing_Upgrade("floors" | "decor"): buys the house's next floor or decor budget step.
int32_t UpgradeHouse(lua_State* L) {
    char const* what = luaL_checkstring(L, 1);
    uint8_t const kind = !std::strcmp(what, "floors") ? UpgradeFloors : !std::strcmp(what, "decor") ? UpgradeDecor
        : !std::strcmp(what, "rooms") ? UpgradeRooms : 0;
    lua_pushboolean(L, kind && s_available && s_state.isOwner && Writer().Put<uint8_t>(ActionUpgrade).Put<uint8_t>(kind)
        .Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_StyleRoom(room, part, theme): room 0-based, part 1 walls 2 floor 3 ceiling, theme from GetLayout().themes.
int32_t StyleRoom(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3))
        return luaL_error(L, "Usage: EsteriaHousing_StyleRoom(room, part, theme)");
    lua_pushboolean(L, s_available && s_state.isOwner && Writer().Put<uint8_t>(ActionRoomStyle)
        .Put<uint8_t>(uint8_t(lua_tonumber(L, 1))).Put<uint8_t>(uint8_t(lua_tonumber(L, 2)))
        .Put<uint32_t>(uint32_t(lua_tonumber(L, 3))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_StairsUp(room): a stairwell's top (room 0-based) becomes another stairwell with a new top above.
int32_t StairsUp(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "Usage: EsteriaHousing_StairsUp(room)");
    lua_pushboolean(L, s_available && s_state.isOwner && Writer().Put<uint8_t>(ActionStairsUp)
        .Put<uint8_t>(uint8_t(lua_tonumber(L, 1))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_BuyDecor(decorId, count): buys decor into storage at the catalog price.
int32_t BuyDecor(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) return luaL_error(L, "Usage: EsteriaHousing_BuyDecor(decorId, count)");
    lua_pushboolean(L, s_available && Writer().Put<uint8_t>(ActionBuyDecor).Put<uint32_t>(uint32_t(lua_tonumber(L, 1)))
        .Put<uint16_t>(uint16_t(lua_tonumber(L, 2))).Send(CMSG_ACTION));
    return 1;
}

// EsteriaHousing_SetCeilingHidden(room, hidden): room 0-based.
int32_t SetCeilingHidden(lua_State* L) {
    if (!lua_isnumber(L, 1)) return luaL_error(L, "Usage: EsteriaHousing_SetCeilingHidden(room, hidden)");
    lua_pushboolean(L, s_available && s_state.isOwner && Writer().Put<uint8_t>(ActionRoomCeiling)
        .Put<uint8_t>(uint8_t(lua_tonumber(L, 1))).Put<uint8_t>(lua_toboolean(L, 2) ? 1 : 0).Send(CMSG_ACTION));
    return 1;
}

// x, y, z where the mouse cursor's ray meets terrain or a world model, or nothing.
int32_t GetCursorWorld(lua_State* L) {
    C3Vector hit;
    if (!CursorRay(hit)) return 0;
    lua_pushnumber(L, hit.x); lua_pushnumber(L, hit.y); lua_pushnumber(L, hit.z);
    return 3;
}

// EsteriaHousing_SetGhost(fileDataId, yawDegrees | qx, qy, qz, qw, scale) shows the decor under the cursor;
// EsteriaHousing_SetGhost() removes it.
int32_t SetGhost(lua_State* L) {
    uint32_t const fdid = lua_isnumber(L, 1) ? uint32_t(lua_tonumber(L, 1)) : 0;
    if (lua_isnumber(L, 5)) {
        s_ghost.rot = Quat(float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3)), float(lua_tonumber(L, 4)),
            float(lua_tonumber(L, 5)));
        s_ghost.scale = lua_isnumber(L, 6) ? float(lua_tonumber(L, 6)) : 1.0f;
    } else if (lua_isnumber(L, 2)) {
        double const half = lua_tonumber(L, 2) * Pi / 360.0;
        s_ghost.rot = Quat(0, 0, float(std::sin(half)), float(std::cos(half)));
        s_ghost.scale = 1;
    }
    static uint32_t shown = 0;
    if (!fdid || !s_available) { DestroyGhost(); shown = 0; return 0; }
    if ((s_ghost.model || s_ghost.wmo) && shown == fdid) return 0; // only the rotation or scale changed
    Ghost const keep = s_ghost;
    DestroyGhost();
    s_ghost.rot = keep.rot;
    s_ghost.scale = keep.scale;
    s_ghost.pinned = keep.pinned;
    s_ghost.pos = keep.pos;
    if (fdid & WmoDecorBit) {
        std::string const path = "retail/" + std::to_string(fdid & ~WmoDecorBit) + ".wmo";
        s_ghost.wmo = CWorld::ObjectCreate(path.c_str(), s_ghost.pos, 0, 0, 0, 0, 0, nullptr, nullptr, 0);
        shown = fdid;
        return 0;
    }
    std::string const path = "retail/" + std::to_string(fdid) + ".m2";
    s_ghost.model = World::s_m2Scene ? World::s_m2Scene->CreateModel(path.c_str(), 0) : nullptr;
    if (s_ghost.model) s_ghost.entity = CMap::ObjectCreate(s_ghost.model, UpdateGhost, nullptr, 0, 0, 10);
    shown = fdid;
    return 0;
}

// EsteriaHousing_ShowPlotBounds(on) shows the plot's boundary while you stand on your own plot.
int32_t ShowPlotBounds(lua_State* L) {
    s_bounds.wanted = lua_toboolean(L, 1) != 0;
    return 0;
}

// EsteriaHousing_PinGhost(x, y, z) holds the ghost there (a handle drag); EsteriaHousing_PinGhost() lets it follow the
// cursor again.
int32_t PinGhost(lua_State* L) {
    s_ghost.pinned = lua_isnumber(L, 3) != 0;
    if (s_ghost.pinned) s_ghost.pos = C3Vector(float(lua_tonumber(L, 1)), float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3)));
    return 0;
}

// x, y, z of the ghost (where a click places it), or nothing when the cursor is off the floor.
int32_t GetGhost(lua_State* L) {
    if ((!s_ghost.model && !s_ghost.wmo) || !s_ghost.onFloor) return 0;
    lua_pushnumber(L, s_ghost.pos.x); lua_pushnumber(L, s_ghost.pos.y); lua_pushnumber(L, s_ghost.pos.z);
    return 3;
}

// The cursor's ray through the world camera: start and end points (the handles pick and drag against it).
bool CursorSegment(C3Vector& start, C3Vector& end) {
    CGWorldFrame* frame = CGWorldFrame::s_currentWorldFrame;
    if (!frame || !frame->m_camera || !frame->m_simpleTop) return false;
    float x, y;
    NDCToDDC(frame->m_simpleTop->m_mousePosition.m_x, frame->m_simpleTop->m_mousePosition.m_y, &x, &y);
    CGxDevice* device = g_theGxDevicePtr;
    C44Matrix const projection = device->m_projection;
    CGxMatrixStack& stack = device->m_xforms[GxXform_View];
    C44Matrix const view = stack.m_mtx[stack.m_level];
    frame->m_camera->SetupWorldProjection(frame->m_viewportDDC);
    bool const ok = frame->GetLineSegment(x, y, &start, &end) != 0;
    device->XformSetProjection(projection);
    device->XformSetView(view);
    return ok;
}

// sx, sy, sz, ex, ey, ez of the cursor's ray, or nothing.
int32_t GetCursorRay(lua_State* L) {
    C3Vector start, end;
    if (!CursorSegment(start, end)) return 0;
    for (float v : { start.x, start.y, start.z, end.x, end.y, end.z }) lua_pushnumber(L, v);
    return 6;
}

// The manipulator: retail draws its decor handles in the engine (the "manipulator" shader); we draw ours as
// camera-facing ribbons after the world renders. Lua picks and drags them (Gizmo.lua); this only draws.
struct Gizmo {
    int mode = 0;          // 0 off, 1 translate (arrows), 2 rotate (rings), 3 scale (a handle above)
    C3Vector pos;
    float radius = 1.5f;
    int hover = 0;         // axis 1 x, 2 y, 3 z (4 the scale handle) under the cursor, or being dragged
    bool dragging = false;
} s_gizmo;
CTexture* s_white = nullptr;

// EsteriaHousing_SetGizmo(mode, x, y, z, radius, hoverAxis, dragging); EsteriaHousing_SetGizmo(0) hides it.
int32_t SetGizmo(lua_State* L) {
    s_gizmo.mode = lua_isnumber(L, 1) ? int(lua_tonumber(L, 1)) : 0;
    if (s_gizmo.mode && lua_isnumber(L, 4)) {
        s_gizmo.pos = C3Vector(float(lua_tonumber(L, 2)), float(lua_tonumber(L, 3)), float(lua_tonumber(L, 4)));
        s_gizmo.radius = lua_isnumber(L, 5) ? float(lua_tonumber(L, 5)) : 1.5f;
        s_gizmo.hover = lua_isnumber(L, 6) ? int(lua_tonumber(L, 6)) : 0;
        s_gizmo.dragging = lua_toboolean(L, 7) != 0;
    }
    return 0;
}

void Ribbon(C3Vector const* points, uint32_t count, CImVector color, float width, C3Vector const& eye) {
    if (count < 2 || count > 200) return;
    C3Vector positions[400];
    CImVector colors[400];
    C2Vector tex[400];
    uint16_t indices[400];
    for (uint32_t i = 0; i < count; ++i) {
        C3Vector const along = points[i + 1 < count ? i + 1 : i] - points[i > 0 ? i - 1 : 0];
        C3Vector const view = points[i] - eye;
        C3Vector side(along.y * view.z - along.z * view.y, along.z * view.x - along.x * view.z, along.x * view.y - along.y * view.x);
        float const length = std::sqrt(side.x * side.x + side.y * side.y + side.z * side.z);
        if (length > 1e-6f) side = side * (width * 0.5f / length);
        positions[2 * i] = points[i] + side;
        positions[2 * i + 1] = points[i] - side;
        colors[2 * i] = colors[2 * i + 1] = color;
        tex[2 * i] = C2Vector(0, 0);
        tex[2 * i + 1] = C2Vector(1, 1);
        indices[2 * i] = uint16_t(2 * i);
        indices[2 * i + 1] = uint16_t(2 * i + 1);
    }
    C3Vector const normal(0, 0, 1);
    GxPrimLockVertexPtrs(2 * count, positions, sizeof(C3Vector), &normal, 0, colors, sizeof(CImVector), nullptr, 0, tex,
        sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_TriangleStrip, 2 * count, indices);
    GxPrimUnlockVertexPtrs();
}

CImVector AxisColor(int axis) {
    CImVector c;
    c.a = 0xE6;
    bool const hot = s_gizmo.hover == axis;
    if (hot && s_gizmo.dragging) { c.r = c.g = c.b = 0xFF; return c; }  // the handle being dragged: white
    if (hot) { c.r = 0xFF; c.g = 0xE0; c.b = 0x30; return c; }           // under the cursor: yellow
    c.r = axis == 1 ? 0xF0 : 0x40; c.g = axis == 2 ? 0xE0 : 0x50; c.b = axis == 3 ? 0xFF : 0x40;
    if (axis == 4) { c.r = 0xE0; c.g = 0xE0; c.b = 0xE0; }
    return c;
}

void DrawGizmo() {
    if (!s_gizmo.mode || !CGWorldFrame::GetActiveCamera()) return;
    if (!s_white) {
        CStatus status;
        s_white = TextureCreate("Interface\\Buttons\\WHITE8X8", CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1, 0, 0, 0), &status, 0);
    }
    CGxTex* white = s_white ? TextureGetGxTex(s_white, 0, nullptr) : nullptr;
    if (!white) return;
    C3Vector const eye = CGWorldFrame::GetActiveCamera()->m_position;
    g_theGxDevicePtr->RsPush();
    GxXformPush(GxXform_World);
    GxXformTranslate(GxXform_World, -eye);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_DepthTest, 0);   // always on top, like retail's handles
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_BlendingMode, 3);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, white);
    GxRsSet(GxRs_TextureShader0, 1);
    float const r = s_gizmo.radius, width = r * 0.045f;
    C3Vector const c = s_gizmo.pos;
    C3Vector const axes[3] = { C3Vector(1, 0, 0), C3Vector(0, 1, 0), C3Vector(0, 0, 1) };
    for (int a = 0; a < 3; ++a) {
        C3Vector const& d = axes[a];
        if (s_gizmo.mode == 1) {
            C3Vector shaft[2] = { c, c + d * r };
            Ribbon(shaft, 2, AxisColor(a + 1), width, eye);
            C3Vector head[2] = { c + d * r, c + d * (r * 1.28f) }; // a wide-to-thin tip
            Ribbon(head, 2, AxisColor(a + 1), width * 3.5f, eye);
        } else if (s_gizmo.mode == 2) {
            C3Vector ring[49];
            C3Vector const u = axes[(a + 1) % 3], v = axes[(a + 2) % 3];
            for (int i = 0; i <= 48; ++i) {
                float const t = float(i) / 48.0f * 6.2831853f;
                ring[i] = c + (u * std::cos(t) + v * std::sin(t)) * r;
            }
            Ribbon(ring, 49, AxisColor(a + 1), width, eye);
        }
    }
    if (s_gizmo.mode == 3) {
        C3Vector stem[2] = { c, c + axes[2] * (r * 1.3f) };
        Ribbon(stem, 2, AxisColor(4), width, eye);
        C3Vector knob[2] = { c + axes[2] * (r * 1.24f), c + axes[2] * (r * 1.42f) };
        Ribbon(knob, 2, AxisColor(4), width * 6.0f, eye);
    }
    GxXformPop(GxXform_World);
    g_theGxDevicePtr->RsPop();
}

void HideGizmo() { s_gizmo.mode = 0; }

class GizmoRender : public RenderScript {
public:
    GizmoRender() : RenderScript("HousingGizmo") { }
    void OnWorldRendered() override { DrawGizmo(); }
};

bool CursorRay(C3Vector& out) {
    CGWorldFrame* frame = CGWorldFrame::s_currentWorldFrame;
    if (!frame || !frame->m_camera || !frame->m_simpleTop) return false;
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
    bool const found = frame->GetLineSegment(x, y, &start, &end) && frame->HitTest(start, end, s_snapSurfaces ? 0x7 : 0x3, &hit) != 0;
    device->XformSetProjection(projection);
    device->XformSetView(view);
    if (!found) return false;
    out = hit.m_point;
    // A ray that slips past the room's floor meets the map's ground plane; keep things at the player's floor then.
    if (CGPlayer_C* player = ClntObjMgrGetActivePlayerObj())
        if (std::fabs(out.z - player->GetPosition().z) > 4.0f) out.z = player->GetPosition().z;
    return true;
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
        TickBounds();
        if (s_ghost.wmo) {
            // A map object has collision: park it far below while the cursor's ray looks for the floor, so the ray
            // never lands on the ghost itself, and leave it there while the cursor is off the floor (hidden).
            C44Matrix away;
            away.Identity();
            away.d2 = -100000.0f;
            // World::ObjectUpdate takes its CMapEntity as the CMapBaseObj it is (a CMapObjDef for a WMO)
            World::ObjectUpdate(reinterpret_cast<CMapEntity*>(s_ghost.wmo), away);
            s_ghost.onFloor = s_ghost.pinned || CursorRay(s_ghost.pos);
            if (s_ghost.onFloor) World::ObjectUpdate(reinterpret_cast<CMapEntity*>(s_ghost.wmo), GhostMatrix());
        }
        if (s_ghost.model && s_ghost.entity) {
            s_ghost.onFloor = s_ghost.pinned || CursorRay(s_ghost.pos);
            // Move the map entity too (CGWorldFrame does this for a spell's ground object), or it stays culled at 0,0,0.
            CAaBox box;
            box.b = C3Vector(0, 0, 0);
            box.t = C3Vector(0, 0, 0);
            CAaSphere sphere;
            C3Vector center(0, 0, 0);
            if (s_ghost.model->IsLoaded(0, 0)) {
                box = s_ghost.model->GetBoundingBox();
                sphere = s_ghost.model->GetBoundingSphere();
                center = sphere.c;
            }
            C44Matrix matrix = GhostMatrix();
            CMap::ObjectUpdate(s_ghost.entity, matrix, box, sphere, center, false, 0xFFFFFFFF);
            World::ObjectSetHidden(s_ghost.entity, !s_ghost.onFloor);
        }
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
        0x1, "1", nullptr, CATEGORY_GAME); // 0x1: saved to Config.wtf, or the client forgets it on exit; on by default,
                                            // since only a realm with housing answers the hello
    sExtensionMgr->RegisterMessageHandler(SMSG_HELLO, ReceiveHello, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_CATALOG, ReceiveCatalog, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_EDIT_ACK, ReceiveAck, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_STATE, ReceiveState, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_PLOT, ReceivePlot, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_LAYOUT, ReceiveLayout, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_NEIGHBORHOOD, ReceiveNeighborhood, nullptr);
    sExtensionMgr->RegisterMessageHandler(SMSG_COMMERCE, ReceiveCommerce, nullptr);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Commerce", Commerce, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetCommerce", GetCommerce, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetMerchant", SetMerchant, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_MerchantEA", MerchantEA, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_MerchantBody", MerchantBody, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_MerchantSlider", MerchantSlider, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_MerchantLook", MerchantLook, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_RaceSupported", RaceSupported, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_COMMERCE", FRAMESCRIPT_CONTEXTS_GAME);
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
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetNeighborhood", GetNeighborhood, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_RequestNeighborhood", RequestNeighborhood, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_TravelNeighborhood", TravelNeighborhood, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetHouseAccess", SetHouseAccess, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetPlotPart", SetPlotPart, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetPlotSpot", SetPlotSpot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetSurfaceSnap", SetSurfaceSnap, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_TravelBack", TravelBack, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_TravelPlot", TravelPlot, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetFavorite", SetFavorite, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Invite", Invite, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Uninvite", Uninvite, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_MoveNeighborhood", MoveNeighborhood, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_BuyGuildNeighborhood", BuyGuildNeighborhood, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetGuildRankRights", SetGuildRankRights, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_MoveOut", MoveOut, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_EnterHouse", EnterHouse, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetLayout", GetLayout, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_AddRoom", AddRoom, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_RemoveRoom", RemoveRoom, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_StyleRoom", StyleRoom, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetCeilingHidden", SetCeilingHidden, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_StairsUp", StairsUp, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_BuyDecor", BuyDecor, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_Upgrade", UpgradeHouse, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetPlotStyle", SetPlotStyle, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_PlacePlotHouse", PlacePlotHouse, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetCursorWorld", GetCursorWorld, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetGhost", SetGhost, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetGhost", GetGhost, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_PinGhost", PinGhost, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_ShowPlotBounds", ShowPlotBounds, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_GetCursorRay", GetCursorRay, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterLuaFunction("EsteriaHousing_SetGizmo", SetGizmo, FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_AVAILABLE", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_CATALOG", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_STATE", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_RESULT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_PLOT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_LAYOUT", FRAMESCRIPT_CONTEXTS_GAME);
    sExtensionMgr->RegisterFrameScriptEvent("ESTERIA_HOUSING_NEIGHBORHOOD", FRAMESCRIPT_CONTEXTS_GAME);
    new Maps();
    new Ticks();
    new GizmoRender();
    new Lifecycle();
}
