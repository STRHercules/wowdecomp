// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDDETAILDOODAD_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDDETAILDOODAD_H

class C44Matrix;

// Not in the original binary. Azeroth Remastered's grass (detail doodad) shaders (HdDetailDoodad.cpp): the 3.3.5
// DetailDoodad shaders rewritten in HLSL (checked by the donor against Blizzard's bytecode for all 6 vertex and 8 pixel
// slots) with WoW Forever's grass lighting, the terrain's (sky / horizon / ground ambient, a soft wrap of the sun, the
// sun cut fully by the shadow), and wind and parting around the player in the vertex shader (HdGrassWind.h). The fade,
// texture, vertex colour and fog stay Blizzard's. Only with visuals 2; on Vulkan only once
// Tools/HdMaterials/install_hd_shaders.py has translated them.
//   hdGrassLight 1 = Forever lighting (default), 2 = our shaders with 3.3.5's lighting (must look like 0), 3 = test view
//                (only the sun after the shadow), 0 = Blizzard's shaders
//   hdGrassWind, hdGrassWindSpeed, hdGrassWindDirection, hdGrassWindStrength, hdGrassReact, hdGrassReactRadius: HdGrassWind.h
//   hdGrassShadow share of the sun grass keeps in shadow with Forever lighting (default 0.5, 0 = Forever's)
//   hdGrassInfo  status: /run print(GetCVar("hdGrassInfo"))
namespace HD {

void RegisterDetailDoodadCVars();
// Starts compiling our grass shaders on a background thread (once; from CDetailDoodad::Initialize).
void DetailDoodadShadersStart();
// In CDetailDoodad::InitializeRenderState just before the grass pixel shader is bound, once per frame: gives the 6 vertex
// and 8 pixel grass shaders our code or Blizzard's (all of them together) and sets pixel c16.
void DetailDoodadShaders();
// In CDetailDoodad::SetupVertexShader after the client's grass constants: vertex c49..c52 of our shader and c53..c59
// grass wind for this chunk (world = the chunk's grass world matrix). Nothing while Blizzard's shaders are in place.
void DetailDoodadVertexConstants(const C44Matrix& world);
// In CDetailDoodad::Destroy before the shaders are released: Blizzard's code back.
void DetailDoodadShadersForget();

}  // namespace HD

#endif
