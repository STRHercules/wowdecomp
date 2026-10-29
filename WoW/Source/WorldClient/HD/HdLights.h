// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDLIGHTS_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDLIGHTS_H

// Waystone HD: lamps, lanterns, braziers and fires light the world around them, like in WoW Forever (not part of
// the original client).
// The lights are the client's own M2 point lights (the M2Light of a lamppost model, animated by CM2Model), the ones
// 3.3.5 already gives the ground per vertex (3 nearest per chunk, clamped, gamma space: barely visible). Forever
// draws each of them into a light buffer, per pixel, with lightbufferomni.bls: colour * N.L / (0.7 d + 0.03 d^2)
// (the same falloff as CM2Light's attenuation 0 / 0.7 / 0.03), and adds that buffer to the ground in linear space:
// albedo * (light^2.2 + buffer)^(1/2.2). Our pixel shaders do the same maths per pixel for the few lights near the
// thing they draw: HD::LightsForSphere picks them from this frame's lights and packs them as pixel constants.
//   hdLights 1 = per-pixel lights (Forever's maths) on the ground, with hdTerrainLight 1 (default)
//            2 = the light path with no light: must look exactly like 0 (proves the wiring changes nothing)
//            3 = test view: the ground shows only the point lights (no texture, no sun, no fog)
//            0 = off: 3.3.5's per-vertex point lights
//   hdLightsRadius  yards where a light ends (faded over its last quarter); 0 = from its colour (default)
//   hdLightsInfo    read only, this frame's lights and the nearest one's values:  /run print(GetCVar("hdLightsInfo"))

#include <cstdint>

class C3Vector;
class CAaSphere;
class CM2Scene;

class CM2Light;

namespace HD {
    // Lights one draw can take (4 pixel constants each).
    const int32_t kLightsMax = 4;
    // Lights the building shaders take (4 pixel constants each, c17..c48): the nearest to the camera, once per frame.
    const int32_t kLightsBuilding = 8;

    // hdLights (0..3).
    int32_t LightsMode();

    // Up to kLightsMax of this frame's point lights that reach `sphere` (world), strongest first, packed for the
    // pixel shader in out[16 * kLightsMax] (4 float4 per light, layout in HdLights.cpp), positions relative to
    // `camera`. Returns how many. The frame's lights are gathered from `scene` the first time it is called in a frame.
    // `limit` (at most kLightsBuilding) is how many it may return.
    int32_t LightsForSphere(CM2Scene* scene, const C3Vector& camera, const CAaSphere& sphere, float* out, int32_t limit = kLightsMax);

    // A model's light leaves its scene (detached or freed): forget its last animated state.
    void LightsForget(const CM2Light* light);
}

#endif
