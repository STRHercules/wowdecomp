// Not in the original binary. See LivingWind.h. The wind model (profiles, noise, gusts, shelter, lee) and the climate
// rules follow seyris-living-azeroth's env/Wind.cpp, env/Shelter.cpp and env/Climate.cpp (GPL-3.0); the client
// addresses the donor read are the native World::Intersect, CMap::QueryAreaId, s_weather, g_clientGameTime and
// DayNight::GetInfo here.
#include "WoW/Source/WorldClient/LivingWind.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "Engine/Source/BlizzardCore/SFile2-Core.h"
#include "Engine/Source/ConsoleBase/IConsole.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "WoW/Source/DB/DBClient/DBClient.h"
#include "WoW/Source/Game/GameTime.h"
#include "WoW/Source/Object/ObjectClient/Object_C.h"
#include "WoW/Source/ObjectMgrClient/ObjectMgrClient.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/WorldFrame.h"
#include "WoW/Source/UIUtil/Camera.h"
#include "WoW/Source/WorldClient/CustomTable.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/Map.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr int32_t kApiVersion = 1;
constexpr int kMaxAreaChain = 8;

CVar* s_enabled = nullptr;
CVar* s_strength = nullptr;
CVar* s_shelter = nullptr;
CVar* s_lee = nullptr;

float Clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float AngleDelta(float a, float b) {
    float d = std::fmod(b - a, 360.0f);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return d;
}

enum Scope { kScopeGlobal = 0, kScopeMap = 1, kScopeArea = 2 };

// --- WindProfile.cdbc: ID, ScopeType, ScopeID, then these, then Flags (and Comment, never read) ---------------------

enum Field { GroundMin, GroundMax, AloftMin, AloftMax, AloftHeight, WeatherInfluence, GustStrengthField, GustFrequency,
             TerrainWeight, LockedDirection, LockStrength, FieldCount };

struct WindRow {
    int32_t scope;
    int32_t id;
    float v[FieldCount];
};

std::vector<WindRow> s_windRows;
char s_windStatus[128] = "not loaded";

// --- AreaClimate.cdbc: ID, ScopeType, ScopeID, DayTemp, NightTemp, SeasonAmplitude, SeasonOffset, Humidity --------

struct ClimateRow {
    float dayTemp = 18.0f;          // degC at the warmest time of day
    float nightTemp = 8.0f;         // degC at the coldest
    float seasonAmplitude = 12.0f;  // degC between midsummer and midwinter
    float seasonOffset = 0.0f;      // months the seasons are shifted (6 = flipped)
    float humidity = 0.5f;
};

struct ClimateRaw {
    int32_t scope;
    int32_t id;
    float v[5];
};

std::vector<ClimateRaw> s_climateRows;
char s_climateStatus[128] = "not loaded";
bool s_loaded = false;

// --- this frame ---------------------------------------------------------------------------------------------------

struct Frame {
    bool inWorld = false;
    C3Vector player;
    int32_t mapId = -1;
    uint32_t areaChain[kMaxAreaChain] = {};
    int areaCount = 0;
    float weather = 0.0f;   // 0..1 after the engine's dead zone
    uint32_t weatherType = 0;
};

Frame s_frame;
float s_profile[FieldCount] = {};
bool s_haveProfile = false;
bool s_active = false;
float s_time = 0.0f;
float s_steadyGround = 0.0f;
float s_steadyAloft = 0.0f;
float s_bearing = 0.0f;
float s_multiplier = 1.0f;

// The wind frame (WindFrame): world positions turned so +u is downwind, measured from the player, plus an offset kept
// so the frame is continuous at the player. Waves laid out in it do not jump when the wind turns: the shift is the
// turn times the distance from the player, not from the map's origin 10000 yards away.
bool s_frameReady = false;
float s_frameX = 0.0f, s_frameY = 0.0f;   // the player, last frame
double s_frameU = 0.0, s_frameV = 0.0;    // the frame's coordinates there
float s_cosH = 1.0f, s_sinH = 0.0f;       // heading, turning from +X toward +Y
float s_scroll = 0.0f;                    // yards the gusts have travelled downwind (accumulated: speed changes)

// --- value noise (deterministic, cheap) ---------------------------------------------------------------------------

float Hash(int32_t x, int32_t y) {
    uint32_t h = static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

float Noise1(float x) {
    const float fx = std::floor(x);
    const int32_t i = static_cast<int32_t>(fx);
    return Lerp(Hash(i, 0), Hash(i + 1, 0), Smooth(x - fx));
}

float Noise2(float x, float y) {
    const float fx = std::floor(x), fy = std::floor(y);
    const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy);
    const float tx = Smooth(x - fx), ty = Smooth(y - fy);
    return Lerp(Lerp(Hash(ix, iy), Hash(ix + 1, iy), tx), Lerp(Hash(ix, iy + 1), Hash(ix + 1, iy + 1), tx), ty);
}

float Fbm2(float x, float y) {
    return 0.55f * Noise2(x, y) + 0.3f * Noise2(x * 2.1f + 7.3f, y * 2.1f - 3.1f) + 0.15f * Noise2(x * 4.3f - 1.7f, y * 4.3f + 5.9f);
}

// --- the world --------------------------------------------------------------------------------------------------

int AreaChainOf(uint32_t area, uint32_t* out, int max) {
    int n = 0;
    while (area && n < max) {
        out[n++] = area;
        const AreaTableRec* rec = g_areaTableDB.GetRecord(static_cast<int32_t>(area));
        if (!rec || static_cast<uint32_t>(rec->m_parentAreaID) == area) break;
        area = static_cast<uint32_t>(rec->m_parentAreaID);
    }
    return n;
}

Frame ReadWorld() {
    Frame f;
    CGCamera* camera = CGWorldFrame::GetActiveCamera();
    CGObject_C* object = camera ? ClntObjMgrObjectPtr(camera->m_targetGUID, TYPEMASK_OBJECT, __FILE__, __LINE__) : nullptr;
    if (!object) return f;
    f.inWorld = true;
    f.player = object->GetPosition();
    f.mapId = static_cast<int32_t>(ClntObjMgrGetMapID());
    uint32_t area = 0;
    if (object->m_worldObject && CMap::QueryAreaId(object->m_worldObject, area) && area) {
        f.areaCount = AreaChainOf(area, f.areaChain, kMaxAreaChain);
    }
    if (World::s_weather) {
        // the engine's own dead zone: nothing reacts below the knee, knee..1 maps onto 0..1
        const float raw = World::s_weather->m_intensity;
        f.weather = raw <= 0.25f ? 0.0f : Clamp01((raw - 0.25f) / 0.75f);
        f.weatherType = World::s_weather->GetType();
    }
    return f;
}

// --- shelter: roofs and lee, cached per 4-yard cell, a ray budget per frame -------------------------------------

constexpr float kCell = 4.0f;
constexpr float kRoofSearch = 40.0f;
constexpr float kRecheckAfter = 10.0f;   // WMOs stream in after the terrain
constexpr float kEvictRange = 120.0f;
constexpr uint32_t kRayBudget = 64;
constexpr float kLeeReach = 15.0f;       // yards upwind an obstacle still casts shadow
constexpr float kLeeAngle = 15.0f;       // ray rise, degrees: only obstacles steeper than this block
constexpr float kLeeStrength = 0.85f;    // wind removed right behind an obstacle
constexpr float kLeeHeight = 1.5f;
constexpr int kSectors = 8;

struct Entry {
    float value;   // roof: 0 or 1; lee: hit fraction along the ray (1 = no hit)
    float tracedAt;
    float x, y;
};

std::unordered_map<uint64_t, Entry> s_roof;
std::unordered_map<uint64_t, Entry> s_leeCache[kSectors];
uint32_t s_rays = 0;

int32_t Cell(float v) { return static_cast<int32_t>(std::floor(v / kCell)); }

uint64_t Key(int32_t cx, int32_t cy, int32_t cz) {
    const uint64_t ux = static_cast<uint64_t>(cx + (1 << 20)) & 0x1FFFFF;
    const uint64_t uy = static_cast<uint64_t>(cy + (1 << 20)) & 0x1FFFFF;
    const uint64_t uz = static_cast<uint64_t>(cz + (1 << 20)) & 0x1FFFFF;
    return (ux << 42) | (uy << 21) | uz;
}

// Fraction along the segment of the first terrain or building hit, 1 for none.
float Trace(const C3Vector& from, const C3Vector& to) {
    C3Vector hit;
    float distance = 1.0f;
    return World::Intersect(from, to, &hit, &distance, 0x01000124, nullptr) ? distance : 1.0f;
}

void ShelterBeginFrame() {
    s_rays = 0;
    const float r2 = kEvictRange * kEvictRange;
    auto evict = [&](std::unordered_map<uint64_t, Entry>& map) {
        for (auto it = map.begin(); it != map.end();) {
            const float dx = it->second.x - s_frame.player.x, dy = it->second.y - s_frame.player.y;
            it = dx * dx + dy * dy > r2 ? map.erase(it) : std::next(it);
        }
    };
    evict(s_roof);
    for (auto& map : s_leeCache) evict(map);
}

// The cached value for a cell, tracing within this frame's budget; `fallback` when the budget is spent.
template <class TraceFn>
float Cached(std::unordered_map<uint64_t, Entry>& map, const float pos[3], float fallback, TraceFn trace) {
    const int32_t cx = Cell(pos[0]), cy = Cell(pos[1]), cz = Cell(pos[2]);
    const uint64_t key = Key(cx, cy, cz);
    auto it = map.find(key);
    const bool fresh = it != map.end() && s_time - it->second.tracedAt <= kRecheckAfter;
    if (fresh) return it->second.value;
    if (s_rays >= kRayBudget) return it != map.end() ? it->second.value : fallback;
    ++s_rays;
    // shortcut: rays start at the queried height in the cell's centre column, not above the terrain there; a query on
    // a steep slope can read as sheltered. Upgrade with a terrain height query if it shows on cliffs.
    const float x = (cx + 0.5f) * kCell, y = (cy + 0.5f) * kCell;
    const float value = trace(x, y, pos[2]);
    map[key] = Entry{ value, s_time, x, y };
    return value;
}

float Openness(const float pos[3]) {
    if (!OptionCVars::Bool(s_shelter, true)) return 1.0f;
    return Cached(s_roof, pos, 1.0f, [](float x, float y, float z) {
        // half a yard up, so a query at floor level does not hit the floor it stands on
        return Trace(C3Vector(x, y, z + 0.5f), C3Vector(x, y, z + kRoofSearch)) < 1.0f ? 0.0f : 1.0f;
    });
}

float Lee(const float pos[3], float dirX, float dirY) {
    if (!OptionCVars::Bool(s_lee, true)) return 1.0f;
    // sector of the direction the wind comes FROM, so a slowly drifting wind reuses its rays
    float angle = std::atan2(-dirY, -dirX);
    if (angle < 0.0f) angle += 2.0f * kPi;
    const int sector = static_cast<int>(angle / (2.0f * kPi / kSectors) + 0.5f) % kSectors;
    const float fraction = Cached(s_leeCache[sector], pos, 1.0f, [sector](float x, float y, float z) {
        const float a = sector * (2.0f * kPi / kSectors);
        const float rise = kLeeReach * std::tan(kLeeAngle * kPi / 180.0f);
        return Trace(C3Vector(x, y, z + kLeeHeight), C3Vector(x + std::cos(a) * kLeeReach, y + std::sin(a) * kLeeReach, z + kLeeHeight + rise));
    });
    // right behind the obstacle (fraction ~ 0) loses most of the wind; one at the far end of the reach barely matters
    return 1.0f - kLeeStrength * (1.0f - Clamp01(fraction));
}

// --- loading and resolving --------------------------------------------------------------------------------------

void Load() {
    s_loaded = true;
    CustomTable::Table table;
    s_windRows.clear();
    if (CustomTable::LoadTable("DBFilesClient\\WindProfile.cdbc", 3 + FieldCount, table, s_windStatus, sizeof s_windStatus)) {
        for (uint32_t r = 0; r < table.Rows(); ++r) {
            WindRow row{ static_cast<int32_t>(table.U(r, 1)), static_cast<int32_t>(table.U(r, 2)), {} };
            for (int f = 0; f < FieldCount; ++f) row.v[f] = table.F(r, 3 + f);
            s_windRows.push_back(row);
        }
    }
    s_climateRows.clear();
    if (CustomTable::LoadTable("DBFilesClient\\AreaClimate.cdbc", 8, table, s_climateStatus, sizeof s_climateStatus)) {
        for (uint32_t r = 0; r < table.Rows(); ++r) {
            ClimateRaw row{ static_cast<int32_t>(table.U(r, 1)), static_cast<int32_t>(table.U(r, 2)), {} };
            for (int f = 0; f < 5; ++f) row.v[f] = table.F(r, 3 + f);
            s_climateRows.push_back(row);
        }
    }
    char line[300];
    std::snprintf(line, sizeof line, "World wind: WindProfile.cdbc %s, AreaClimate.cdbc %s", s_windStatus, s_climateStatus);
    ConsoleWrite(line, DEFAULT_COLOR);
}

template <class Row>
const Row* FindRow(const std::vector<Row>& rows, int32_t scope, int32_t id) {
    for (const Row& row : rows) {
        if (row.scope == scope && (scope == kScopeGlobal || row.id == id)) return &row;
    }
    return nullptr;
}

// Global < Map < area chain (least specific first, so the sub-area has the last word); a negative field inherits.
bool ResolveWind(float out[FieldCount]) {
    bool any = false;
    for (int f = 0; f < FieldCount; ++f) out[f] = 0.0f;
    auto apply = [&](const WindRow* row) {
        if (!row) return;
        any = true;
        for (int f = 0; f < FieldCount; ++f) {
            if (row->v[f] >= 0.0f) out[f] = row->v[f];
        }
    };
    apply(FindRow(s_windRows, kScopeGlobal, 0));
    apply(FindRow(s_windRows, kScopeMap, s_frame.mapId));
    for (int i = s_frame.areaCount - 1; i >= 0; --i) apply(FindRow(s_windRows, kScopeArea, static_cast<int32_t>(s_frame.areaChain[i])));
    return any;
}

// Most specific first: the area chain, the map, Global. Signed temperatures inherit at -1000 and below, the rest when negative.
// shortcut: the player's own area row, a step at area borders (the donor blends 33-yard chunks over ~100 yd);
// upgrade when something other than the Lua API reads the climate.
ClimateRow ResolveClimate() {
    ClimateRow out;
    float* fields[5] = { &out.dayTemp, &out.nightTemp, &out.seasonAmplitude, &out.seasonOffset, &out.humidity };
    bool set[5] = {};
    auto apply = [&](const ClimateRaw* row) {
        if (!row) return;
        for (int f = 0; f < 5; ++f) {
            const float v = row->v[f];
            if (!set[f] && (f < 2 ? v > -1000.0f : v >= 0.0f)) {
                *fields[f] = v;
                set[f] = true;
            }
        }
    };
    for (int i = 0; i < s_frame.areaCount; ++i) apply(FindRow(s_climateRows, kScopeArea, static_cast<int32_t>(s_frame.areaChain[i])));
    apply(FindRow(s_climateRows, kScopeMap, s_frame.mapId));
    apply(FindRow(s_climateRows, kScopeGlobal, 0));
    out.humidity = Clamp01(out.humidity);
    return out;
}

// 0 at the coldest time of day (05:00), 1 at the warmest (15:00): warming over 10 hours, cooling over 14.
float DailyCurve(float f) {
    constexpr float kMin = 5.0f / 24.0f, kMax = 15.0f / 24.0f;
    const float t = f - kMin - std::floor(f - kMin);
    const float warm = kMax - kMin;
    return t < warm ? 0.5f - 0.5f * std::cos(kPi * t / warm) : 0.5f + 0.5f * std::cos(kPi * (t - warm) / (1.0f - warm));
}

float AirTemperature(const ClimateRow& row) {
    const DayNight::DNInfo* info = DayNight::GetInfo();
    float day = info ? info->m_dayProgression : 0.5f;
    if (!(day >= 0.0f && day <= 1.0f)) day = (g_clientGameTime.m_hour * 60 + g_clientGameTime.m_minute) / 1440.0f;
    // the season peaks around 20 July (0.55 of the year); month and day are 0-based in the game time
    const float year = (g_clientGameTime.m_month + g_clientGameTime.m_monthDay / 31.0f) / 12.0f;
    float y = year - 0.55f - row.seasonOffset / 12.0f;
    y -= std::floor(y);
    float t = row.nightTemp + (row.dayTemp - row.nightTemp) * DailyCurve(day) + 0.5f * row.seasonAmplitude * std::cos(2.0f * kPi * y);
    switch (s_frame.weatherType) {
        case 1: t -= 3.0f * s_frame.weather; break;   // rain
        case 2: t -= 6.0f * s_frame.weather; break;   // snow
        case 3: t += 2.0f * s_frame.weather; break;   // sand
        default: break;
    }
    return t;
}

}  // namespace

void LivingWind::RegisterCVars() {
    s_enabled = OptionCVars::Register("worldWind", "World wind from WindProfile.cdbc moves the trees and the grass", "1");
    s_strength = OptionCVars::Register("worldWindStrength", "World wind: multiplier on every place's wind (1 = as the table says)", "1");
    s_shelter = OptionCVars::Register("worldWindShelter", "World wind: roofs and overhangs keep the ground below calm", "1");
    s_lee = OptionCVars::Register("worldWindLee", "World wind: walls and cliffs cast a wind shadow downwind", "1");
}

void LivingWind::Update(float elapsedSec) {
    if (!s_loaded) Load();
    float dt = elapsedSec;
    if (!(dt >= 0.0f && dt <= 1.0f)) dt = 1.0f / 60.0f;   // hitches and the first frame
    s_time += dt;
    s_frame = ReadWorld();
    s_multiplier = OptionCVars::Float(s_strength, 0.0f, 3.0f, 1.0f);

    float target[FieldCount];
    const bool any = s_frame.inWorld && ResolveWind(target);
    s_active = any && OptionCVars::Bool(s_enabled, true);
    if (!any) {
        s_haveProfile = false;
        return;
    }
    if (!s_haveProfile) {
        std::memcpy(s_profile, target, sizeof s_profile);
        s_haveProfile = true;
    } else {
        const float k = 1.0f - std::exp(-dt / 1.5f);   // ~1.5 s to settle after crossing a border
        for (int f = 0; f < FieldCount; ++f) {
            s_profile[f] += (f == LockedDirection ? AngleDelta(s_profile[f], target[f]) : target[f] - s_profile[f]) * k;
        }
    }
    ShelterBeginFrame();

    // Where in [min, max] the steady wind sits: a slow wander in the lower middle, pushed toward max by the weather.
    float rangePos = 0.2f + 0.5f * Noise1(s_time / 40.0f + 17.3f);
    rangePos += (1.0f - rangePos) * Clamp01(s_frame.weather * s_profile[WeatherInfluence]);
    s_steadyGround = Lerp(s_profile[GroundMin], s_profile[GroundMax], rangePos);
    s_steadyAloft = Lerp(s_profile[AloftMin], s_profile[AloftMax], rangePos);

    // A per-map base direction that wanders +-60 degrees over minutes, pulled toward LockedDirection by LockStrength.
    float bearing = Hash(s_frame.mapId, 91) * 360.0f + 60.0f * (Noise1(s_time / 90.0f + 3.1f) * 2.0f - 1.0f);
    bearing += AngleDelta(bearing, s_profile[LockedDirection]) * Clamp01(s_profile[LockStrength]);
    s_bearing = std::fmod(bearing + 360.0f, 360.0f);

    // Carry the frame to the player's new position with last frame's heading, then turn it about the player.
    const float px = s_frame.player.x, py = s_frame.player.y;
    if (s_frameReady) {
        const double dx = px - s_frameX, dy = py - s_frameY;
        s_frameU += dx * s_cosH + dy * s_sinH;
        s_frameV += -dx * s_sinH + dy * s_cosH;
        s_frameU = std::fmod(s_frameU, 65536.0);   // waves and noise only need it continuous near the player
        s_frameV = std::fmod(s_frameV, 65536.0);
    }
    s_frameReady = true;
    s_frameX = px;
    s_frameY = py;
    const float heading = -s_bearing * kPi / 180.0f;
    s_cosH = std::cos(heading);
    s_sinH = std::sin(heading);
    s_scroll = std::fmod(s_scroll + (4.0f + 16.0f * s_steadyGround) * dt, 65536.0f);
}

void LivingWind::WindFrame(float x, float y, float& u, float& v) {
    const float dx = x - s_frameX, dy = y - s_frameY;
    u = dx * s_cosH + dy * s_sinH + static_cast<float>(s_frameU);
    v = -dx * s_sinH + dy * s_cosH + static_cast<float>(s_frameV);
}

float LivingWind::HeadingDegrees() { return -s_bearing; }

bool LivingWind::Active() { return s_active; }

bool LivingWind::PlayerPosition(float out[3]) {
    out[0] = s_frame.player.x;
    out[1] = s_frame.player.y;
    out[2] = s_frame.player.z;
    return s_frame.inWorld;
}

int LivingWind::AreaChain(uint32_t area, uint32_t* out, int max) { return AreaChainOf(area, out, max); }
float LivingWind::Strength() { return s_active ? s_steadyGround * s_multiplier : 0.0f; }
float LivingWind::BearingDegrees() { return s_bearing; }
float LivingWind::GustStrength() { return s_active ? s_profile[GustStrengthField] : 0.0f; }

float LivingWind::GustAt(float x, float y) {
    if (!s_active) return 0.0f;
    // A noise field stretched along the wind and scrolled downwind, faster in stronger wind: bursts travel across the land.
    const float freq = s_profile[GustFrequency] > 0.05f ? s_profile[GustFrequency] : 0.05f;
    float u, v;
    LivingWind::WindFrame(x, y, u, v);
    return Clamp01((Fbm2((u - s_scroll) / (50.0f / freq), v / (35.0f / freq)) - 0.5f) * 3.0f);
}

float LivingWind::StrengthAt(const float pos[3]) {
    if (!s_active || s_steadyGround <= 0.0f) return 0.0f;
    const float rad = s_bearing * kPi / 180.0f;
    const float open = Openness(pos);
    // no lee ray under a roof: already calm there
    const float lee = open > 0.0f ? Lee(pos, std::cos(rad), -std::sin(rad)) : 1.0f;
    return s_steadyGround * (1.0f + s_profile[GustStrengthField] * GustAt(pos[0], pos[1])) * open * lee * s_multiplier;
}

// --- Lua: read-only, numbers and strings only; nil outside the world ------------------------------------------------

int32_t LivingWind::Script_GetVersion(lua_State* L) {
    lua_pushnumber(L, kApiVersion);
    return 1;
}

// strength (0..1, gusts above 1) where the player stands, the compass heading it blows toward, and the gust share.
int32_t LivingWind::Script_GetWind(lua_State* L) {
    if (!s_frame.inWorld) {
        lua_pushnil(L);
        return 1;
    }
    const float pos[3] = { s_frame.player.x, s_frame.player.y, s_frame.player.z };
    lua_pushnumber(L, LivingWind::StrengthAt(pos));
    lua_pushnumber(L, s_bearing);
    lua_pushnumber(L, LivingWind::GustAt(pos[0], pos[1]));
    return 3;
}

int32_t LivingWind::Script_GetWeather(lua_State* L) {
    if (!s_frame.inWorld) {
        lua_pushnil(L);
        return 1;
    }
    static const char* const kNames[] = { "fine", "rain", "snow", "sand" };
    lua_pushstring(L, kNames[s_frame.weatherType < 4 ? s_frame.weatherType : 0]);
    lua_pushnumber(L, s_frame.weather);
    return 2;
}

// ground, air in degC. shortcut: ground = air; the donor warms the ground near lava and hot ground (its world fields).
int32_t LivingWind::Script_GetTemperature(lua_State* L) {
    if (!s_frame.inWorld) {
        lua_pushnil(L);
        return 1;
    }
    const float air = AirTemperature(ResolveClimate());
    lua_pushnumber(L, air);
    lua_pushnumber(L, air);
    return 2;
}

int32_t LivingWind::Script_GetHumidity(lua_State* L) {
    if (!s_frame.inWorld) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushnumber(L, ResolveClimate().humidity);
    return 1;
}
