// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDWATERRIPPLES_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDWATERRIPPLES_H

#include "Engine/Source/Gx/GxEffects.h"

// Not in the original binary. Azeroth Remastered's ripples on hdWater's water, around units that walk, swim, turn or jump
// into it. The 3.3.5 client already decides when and where a ripple is born (CGUnit_C::CreateRipple -> WaterRipple.cpp);
// it only lays a flat splash texture on the water. Here each live ripple also becomes a ring of small waves in the water's
// surface (WoW Forever's waterripples do the same): once per liquid pass, one GxEffects pass draws the rings into a 512 x
// 512 top-down texture of 32 x 32 yards around the camera, and the water pixel shader (the FFT variant) adds their slopes
// to the waves, so reflections and refraction bend around them.
//   hdWaterRipples         1 = on (default), 0 = off
//   hdWaterRippleStrength  1 = normal (0 to 4)
namespace HD {

// Registers the cvars (hdWaterInfo's neighbours exist from start-up).
void WaterRipplesRegister();
// Once per liquid pass (after the depth pass): draws this frame's ripple texture.
void WaterRipplesRender(GxEffects::Device* device);
// The ripple texture (always the same one once made; it holds calm water when no ripple is near).
GxEffects::Texture* WaterRipplesTexture();
// c36, c37 of the water pixel shader: out[0..3] = world xy -> texture uv (scale, scale, offset x, offset y),
// out[4] = strength (0 when there is no ripple this frame).
void WaterRipplesConstants(float* out);
// A new device or shutdown: forget what the old one made.
void WaterRipplesRelease();

}  // namespace HD

#endif
