// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDSKY_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDSKY_H

#include <cstdint>

// Not in the original binary. Azeroth Remastered's sky (HdSky.cpp): WoW Forever's dnsky and dnclouds shaders on the 3.3.5
// sky dome and cloud dome, which the client draws with the fixed-function pipeline. Same meshes, colours and cloud
// texture, plus what Forever's shaders add:
//   - sky: a glow toward the sun (the moon at night), growing as the cube from the edge of a cone to the sun, stronger at
//     and below the horizon, mixed in linear light; then a 1-step dither against banding;
//   - clouds: the sun shining through thin cloud near the sun (base x (1 + sun x N.L^2 x (1 - alpha) x strength)).
// Sun, moon, stars, glares and zone skyboxes stay as they are. Only with visuals 2; on Vulkan only once
// Tools/HdMaterials/install_hd_shaders.py has translated the four shaders (hd-pending\HdSky-*.bin).
//   hdSky 1 = Forever sky and clouds (default), 2 = our shaders with the 3.3.5 look (must look like 0),
//         3 = test view (sky = glow amount in grey, clouds = sun scatter in red), 0 = Blizzard's sky
//   hdSkyGlow / hdSkyGlowHorizon  glow strength above / at and below the horizon; hdSkyGlowSize  cone half-angle (degrees)
//   hdSkyCloudSun  sun scatter in thin cloud; hdSkyDither  dither in 1/255 steps; hdSkyInfo  status (read only)
namespace HD {

enum SkyPass {
    SkyPass_Dome = 0,    // DNSky::Render (vertices: position, colour)
    SkyPass_Clouds = 1,  // DNClouds::Render (vertices: position, colour, texture coordinate)
};

void RegisterSkyCVars();
// After the render states, before the draw: binds our shaders and constants and returns true, or returns false and
// touches nothing (the fixed-function draw then happens as before). The caller's RsPop unbinds the shaders.
bool SkyBeginDraw(int32_t pass);
// After the draw when SkyBeginDraw returned true: gives the client its shader constants back.
void SkyEndDraw();

}  // namespace HD

#endif
