// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDFOG_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDFOG_H

// Not in the original binary. Azeroth Remastered's volumetric fog (hdFog, HdVulkanFog.cpp) over GxEffects: WoW
// Forever's three fog layers (forever_data\hd_forever_fog.txt), picked by the zone's lights and the time of day, marched
// in 32 steps along each pixel's view ray. Only with visuals 2 (Visuals::AzerothRemastered); without the data only the
// hdFogDebug views 1 (distance) and 2 (depth) draw.
namespace HD::Fog {

void RegisterCVars();
// After the world, its liquids and weather, before the spells and particles.
void World();

}  // namespace HD::Fog

#endif
