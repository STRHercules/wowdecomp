// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDTERRAIN_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDTERRAIN_H

#include <cstdint>

class CAaSphere;
class C3Vector;
class CGxShader;
class CM2Scene;

// Not in the original binary. Azeroth Remastered's terrain shaders (HdTerrain.cpp): the 3.3.5 terrain shaders rewritten
// in HLSL (checked by the donor against Blizzard's bytecode for all 128 vertex and 224 pixel permutations) with WoW
// Forever's terrain lighting: the ambient from three colours, sky (ground facing up), horizon (walls) and ground
// (overhangs), Forever's own (forever_data\hd_forever_ambient.txt); a soft wrap of the sun over it; the sun cut fully by
// the shadow, the ambient untouched by it. Only with visuals 2; on Vulkan only once Tools/HdMaterials/install_hd_shaders.py
// has translated them.
//   hdTerrainLight 1 = Forever lighting (default), 2 = our shaders with 3.3.5's lighting (must look like 0),
//                  3 = test view (only the sun after the shadow), 0 = Blizzard's shaders
//   hdLightAmbient ambient strength in Forever lighting (1 = Forever's; a test knob)
//   hdTerrainHemi  test: 1 Forever hemisphere, 0 none (horizon = ground = sky), 2 ratios frozen
//   hdTerrainInfo  status: /run print(GetCVar("hdTerrainInfo"))
namespace HD {

enum TerrainShaderKind {
    TerrainVertex = 0,  // CMap::vertexShader_Terrain[128]
    TerrainPixel1 = 1,  // CMap::pixelShader_Terrain1[32] (no dynamic shadow)
    TerrainPixel3 = 2,  // CMap::pixelShader_Terrain3[96] (Terrain3 or Terrain3_pcf, read from the shader's name)
};

void RegisterTerrainCVars();
// Starts compiling our terrain shaders on a background thread (once; from CMap::Initialize).
void TerrainShadersStart();
// Where the client picks a terrain shader: gives it our code or Blizzard's, to match hdTerrainLight and visuals.
void TerrainShader(CGxShader* shader, int32_t kind, int32_t permutation);
// Vertex c49..c53 and pixel c16.. for our shaders, after the client's per-chunk terrain constants. hdLights picks the
// point lights for the chunk's sphere from the scene's lights, positions relative to `camera` (s_activeWorldView).
void TerrainVertexConstants(const CAaSphere* sphere, CM2Scene* scene, const C3Vector* camera);
// Forever's horizon / sky and ground / sky ambient ratios (per channel) where the camera is (redone at most every 50 ms).
void TerrainHemisphere(float* horizon, float* ground);
// hdLightAmbient: ambient strength in Forever lighting (1 = Forever's).
float LightAmbientStrength();

}  // namespace HD

#endif
