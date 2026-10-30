// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDWATERWAVES_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDWATERWAVES_H

// Not in the original binary. Azeroth Remastered's FFT ocean waves for hdWater 1, on Vulkan: the textures are GxEffects
// textures made each update (CreateFilled, with their mips); the CPU spectrum and FFT are the donor's.
// WoW Forever's PBR water reads its slopes and whitecaps from FFT wave tiles (Tessendorf), up to 4 cascades per water
// type, described by FFTTile.db2 and LiquidTypeXFFTTile.db2. Forever's FFT shaders are not among the extracted
// files, so the spectrum here is the textbook one (JONSWAP, or Pierson-Moskowitz without a fetch, cos^2s spreading,
// deep-water dispersion), fed with each tile's numbers as read by forever_tools\make_hd_forever_fft.py into
// forever_data\hd_forever_fft.txt: tile size, random seed, wind speed, JONSWAP gamma, and up to two (direction,
// fetch km, spreading) groups. Field meanings are inferred, NOT proven: it looks like Forever's kind of water, it is
// not proven to be Forever's exact waves. Without the file, built-in tiles (hand-made, not Forever's) are used.
// Each frame, on the CPU: the spectrum at time t, 3 inverse FFTs of 64 x 64 per cascade (height, slopes, the
// Jacobian of the horizontal displacement), into one 64 x 64 A16B16G16R16F texture per cascade with its mips:
//   r = slope dh/dx, g = slope dh/dy, b = height (yards), a = Jacobian (1 = calm, < 0.5 = folding crest: whitecap).
// The water pixel shader adds the cascades' slopes to the 3.3.5 vertex waves instead of the 3.3.5 normal maps.
//   hdWaterWaves     1 = FFT waves (default), 0 = the 3.3.5 normal maps as before
//   hdWaterWaveScale wave height multiplier (1 = the tiles' own, 0 to 4)
// The wind heading is hdGrassWindDirection (grass and water move with the same wind).

#include <cstdint>

#include "Engine/Source/Gx/GxEffects.h"


namespace HD {
    enum { WaterWaveCascades = 4 };

    // Once per frame before the water draws: steps the waves of this Forever water type to the current time and
    // uploads them. Returns false (no FFT waves this frame) when hdWaterWaves is 0 or a texture cannot be made.
    bool WaterWavesUpdate(GxEffects::Device* device, int32_t pbrType);

    // The cascade textures (valid after WaterWavesUpdate returned true; an unused cascade gets a 1 x 1 calm texture:
    // no slope, height 0, Jacobian 1) and their tiling: out[i] = 1 / tile size (yards) of cascade i, 0 for an unused
    // cascade; returns the number of cascades in use.
    GxEffects::Texture* WaterWavesTexture(int32_t cascade);
    int32_t WaterWavesTiling(float* out);

    // Wave height multiplier (hdWaterWaveScale) and whether hdWaterWaves is on, for the shader constants.
    float WaterWavesScale();
    bool WaterWavesEnabled();

    // Called with HD::WaterReleaseDeviceResources (device lost, resize).
    void WaterWavesRelease();
}

#endif
