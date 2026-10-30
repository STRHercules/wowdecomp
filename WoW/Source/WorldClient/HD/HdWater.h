// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDWATER_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDWATER_H

#include <cstdint>

class CGxShader;

namespace Liquid {
    class CMaterialSettings;
}

// Not in the original binary. Azeroth Remastered's WoW Forever PBR water (HdWater.cpp): the lighting of Forever's
// deferred water shader moved into the 3.3.5 forward water pass: absorption over the real water depth (Beer-Lambert),
// refraction, single scattering with the crest and base colours, reflections with Forever's EnvBRDF (the world by a
// screen-space march, else the DayNight sky colours), sun and moon highlight, soft shoreline, depth and shore foam.
// Colours and strengths per water type from forever_data\hd_forever_liquid.txt, built-in values when it is missing.
// Only with visuals 2, on Vulkan: the water draws with our shaders (translated modules, Tools/HdMaterials), reading the
// world's linear depth and a copy of the world image through GxEffects::SetDrawTexture (stages 6 and 7).
// Stage 1: no FFT waves, ripples or planar reflection yet (the donor's HD_FFT / HD_PLANAR variants).
//   hdWater         1 = Forever's PBR water (default), 2 = our shaders with the 3.3.5 look (must look like 0), 0 = Blizzard
//   hdWaterDebug    test views (hdWater 1): 1 = water depth (white = 10 yards), 2 = scattering alone, 3 = reflections
//                   alone, 4 = foam, 5 = roughness, 6 = world depth under the water (white = 100 yards), 7 = the world
//                   under the water with the fog taken out
//   hdWaterBody     strength of the water's own colour, 1 = Forever's (0 to 5)
//   hdWaterReflect  reflections of the world, 1 = on, 0 = sky only (0 to 1)
//   hdWaterColor    1 = the game's own water colours (follow the hour), 0 = Forever's scattering
//   hdWaterShore    yards over which the water fades in from the shore (default 2; Forever's own is 0.5)
//   hdWaterInfo     read only, status: /run print(GetCVar("hdWaterInfo"))
namespace HD {

void RegisterWaterCVars();

// CWorldScene::RenderLiquidPass, before the liquids are drawn: linear world depth and a copy of the world image.
// cameraInLiquid keeps Blizzard's water.
void WaterBeginLiquidPass(bool cameraInLiquid);
void WaterEndLiquidPass();

// Liquid::CMaterialProcWater::Render, instead of binding its two shaders (inside its RsPush, after its constants):
// binds ours with their constants and returns true, or returns false (off, not ready, no depth) and the caller binds
// Blizzard's. lightCount: the vertex shader permutation the client picked.
bool WaterBindProcWater(Liquid::CMaterialSettings* settings, uint32_t lightCount);
// The same for the 3.3.5 water material (Liquid::CMaterialWater and CMaterialWaterNoSpec: this client's lakes), with the
// material's own client shaders (their shader model is checked). hdWater 2 leaves these to Blizzard.
bool WaterBindLake(Liquid::CMaterialSettings* settings, CGxShader* clientVertex, CGxShader* clientPixel);
// After its Draw when WaterBindProcWater or WaterBindLake returned true.
void WaterAfterDraw();

// Every liquid material's Render: counts the draws by material for hdWaterInfo (1 water, 2 water without specular, 3 water
// fixed-function, 4 magma, 5 magma fixed-function).
void WaterSeen(int32_t kind);

}  // namespace HD

#endif
