// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDSURFACECOVER_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDSURFACECOVER_H

#include <cstdint>

class CGxShader;

// Not in the original binary. The surface cover's shaders (SurfaceCover.h): Azeroth Remastered's terrain shaders
// (HdTerrain.cpp) cut down to one layer, with the cover's lift, where it ends (the chunk's alpha maps per pixel) and
// its look. Compiled at start-up by the system's d3dcompiler_47; on Vulkan drawn only once
// Tools/HdMaterials/install_hd_shaders.py has translated them. Any visuals.
namespace HD {

// Starts compiling them on a background thread (once).
void SurfaceCoverShadersStart();

// The vertex and pixel shader for the terrain's shadow mode (CShadowCache::GetShadowValue, 0..3) and PCF; false
// while they are compiling, failed, or not translated yet (then written to hd-pending).
bool SurfaceCoverShaders(int32_t shadowMode, bool pcf, CGxShader** vertex, CGxShader** pixel);

// For surfaceCoverInfo.
const char* SurfaceCoverShaderInfo();

}  // namespace HD

#endif
