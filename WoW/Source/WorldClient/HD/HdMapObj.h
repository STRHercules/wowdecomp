// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDMAPOBJ_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDMAPOBJ_H

#include <cstdint>

class CShaderEffect;

// Not in the original binary. Azeroth Remastered's building (WMO / MapObj) shaders (HdMapObj.cpp): the 3.3.5 MapObj
// shaders rewritten in HLSL (checked by the donor against Blizzard's bytecode for all 7 vertex families x 90 slots and
// 7 pixel families x 16 permutations) with WoW Forever's lighting on exterior-lit batches, the terrain's: the ambient
// from sky (roofs), horizon (walls) and ground (overhangs), the same ratios as the terrain (HD::TerrainHemisphere); a
// soft wrap of the sun; the sun cut fully by the shadow. Interior batches and the interior / exterior transition stay
// Blizzard's. Only with visuals 2; on Vulkan only once Tools/HdMaterials/install_hd_shaders.py has translated them.
//   hdWmoLight 1 = Forever lighting (default), 2 = our shaders with 3.3.5's lighting (must look like 0), 3 = test view
//              (exterior-lit batches show only the sun after the shadow), 0 = Blizzard's shaders
//   hdWmoInfo  status: /run print(GetCVar("hdWmoInfo"))
namespace HD {

void RegisterMapObjCVars();
// Starts compiling our MapObj shaders on a background thread (once; from CMapObj::Initialize).
void MapObjShadersStart();
// In CMapObj::SelectWorldShaders just before CShaderEffect::SetShaders, every batch: gives the vertex and pixel shader
// about to be bound our code or Blizzard's (always both together), to match hdWmoLight and visuals, and sets the
// constants our shaders read (vertex c49..c53, pixel c16). lightingMode = CMapObj::s_lightingMode.
void MapObjShaders(CShaderEffect* effect, uint32_t vertexPermute, uint32_t pixelPermute, int32_t lightingMode);

}  // namespace HD

#endif
