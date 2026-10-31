#ifndef WOW_SOURCE_WORLDCLIENT_LIVINGWIND_H
#define WOW_SOURCE_WORLDCLIENT_LIVINGWIND_H

#include <cstdint>

struct lua_State;

// Not in the original binary. The world's wind and climate, ported from seyris-living-azeroth (GPL-3.0, Morfium-G):
// WindProfile.cdbc sets the wind per map and area (most specific row wins, -1 inherits), bad weather pushes it toward
// its maximum, gusts travel downwind, roofs shelter the ground below and walls cast a lee. AreaClimate.cdbc sets
// temperature and humidity for the Lua API. Both tables are optional: without WindProfile the trees and the grass keep
// their own fixed wind (the treeWind* / hdGrassWind* sliders), without AreaClimate a mild built-in climate applies.
namespace LivingWind {

void RegisterCVars();

// Once per frame on the main thread, before the world is animated and drawn.
void Update(float elapsedSec);

// True while a WindProfile row gives the current place any wind and worldWind is on.
bool Active();

// The steady wind for this frame. Safe to read from the animate threads (plain values written once per frame).
float Strength();         // 0..1 as authored, times worldWindStrength
float BearingDegrees();   // compass heading the wind blows toward: 0 north (+X), 90 east (-Y)
float GustStrength();     // WindProfile GustStrength, 0..1

// The heading as an angle from +X toward +Y (the trees' and the grass's convention), degrees.
float HeadingDegrees();

// (x, y) in the wind frame: u downwind, v across, yards. Lay travelling waves out in u and v, never in world x and y:
// the frame turns about the player, so a turning wind shifts nothing near the player. Thread-safe.
void WindFrame(float x, float y, float& u, float& v);

// 0..1: how much of a gust passes over (x, y) now. Pure noise, thread-safe; no shelter rays.
float GustAt(float x, float y);

// Wind strength at a point with shelter and lee (main thread only: may trace rays within this frame's budget).
float StrengthAt(const float pos[3]);

// 0 under a roof or overhang within 40 yd above pos, 1 in the open (cached per 4-yard cell, rays within this frame's
// budget; -1 where not known yet because the budget is spent, 1 with worldWindShelter off). Main thread only.
float RoofOpenness(const float pos[3]);

// The player's position this frame (false outside the world).
bool PlayerPosition(float out[3]);

// What the ground under the player sees now (main thread; costs a few dozen queries: ask a few times a second).
struct GroundState {
    float temperature;   // degC, warmed near magma
    float air;           // degC, the air's (what a whole zone shares)
    float humidity;      // 0..1
    float wind;          // 0..1
    uint32_t weatherType;  // 0 clear, 1 rain, 2 snow, 3 sand
    float weather;       // 0..1
};
bool GroundConditions(GroundState& out);

// A zone's air temperature (degC, no weather) and humidity from its own climate rows, wherever the player is.
bool ZoneAir(uint32_t zone, float& air, float& humidity);

// An area and its parents from AreaTable, most specific first; returns how many were written.
int AreaChain(uint32_t area, uint32_t* out, int max);

// Lua: LivingAzeroth_GetVersion / GetWind / GetWeather / GetTemperature / GetHumidity.
int32_t Script_GetVersion(lua_State* L);
int32_t Script_GetWind(lua_State* L);
int32_t Script_GetWeather(lua_State* L);
int32_t Script_GetTemperature(lua_State* L);
int32_t Script_GetHumidity(lua_State* L);

}  // namespace LivingWind

#endif
