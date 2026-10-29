// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDAO_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDAO_H

// Not in the original binary. Azeroth Remastered's ambient occlusion (hdAO, HdVulkanAO.cpp: Intel's XeGTAO) over
// GxEffects: a depth pyramid, the horizon search, an edge-aware blur, an optional temporal pass that reprojects the
// last frame's result, then the world darkened by it. Only with visuals 2 (Visuals::AzerothRemastered).
namespace HD::AO {

void RegisterCVars();
// After the opaque world, right after HD::CaptureCamera (the donor's AfterOpaqueWorld point).
void World();

}  // namespace HD::AO

#endif
