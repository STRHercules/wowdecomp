// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WOW_SOURCE_WORLDCLIENT_HD_HDFOREVER_H
#define WOW_SOURCE_WORLDCLIENT_HD_HDFOREVER_H

#include <cstdint>

class LightRec;

// Not in the original binary. Azeroth Remastered's WoW Forever data (forever_data\ beside the executable), the part
// the grading and the fog read so far: which Forever setting each light uses (hd_forever_light.txt "L" lines), that
// setting's grading LUT by time of day (hd_forever_grading.txt) and its fog layers (hd_forever_fog.txt). Without the
// files every light has no grading and no fog.
namespace HD {

// One Forever fog layer (LightDataGlobalVolumeFog), as Forever's global fog shader reads it. Colours 0..1.
struct ForeverFogLayer {
    int32_t flags;              // 1 sun shadow, 2 heights relative to the player, 4 noise, 8 drawn; 0 no layer
    float diffuse[3];           // sun-lit colour, added toward the sun
    float emissive[3];          // base colour
    float shadow[3];            // base colour in shadow
    float start;                // distance where the layer starts, yards
    float density;              // x 0.01 = per yard
    float shadowMul;            // density in shadow
    float topRate, topHeight;   // thins above topHeight: exp(-topRate * 0.01 * (z - topHeight))
    float bottomRate, bottomHeight;
    float intensity;            // sun scatter strength
    float g;                    // phase: 0 even, 0.9 strongly toward the sun
    float strength, exponent;   // denser with distance: 1 + strength * (distance share)^exponent
};

struct ForeverFog {
    int32_t count;
    int32_t setting;
    ForeverFogLayer layers[3];
};

void RegisterForeverCVars();

// Blend two layers (t = 0 keeps a, 1 gives b); a missing layer (flags 0) fades in or out by density only.
void LerpFogLayer(ForeverFogLayer& a, const ForeverFogLayer& b, float t);
// This light's Forever fog for `slot` at `time`, between Forever's time keys. False when it has none.
bool ForeverFogAt(const LightRec* light, int32_t slot, int32_t time, ForeverFog* out);

// The LUTs either side of `time` (half-minutes, 0..2880) in this light's Forever setting for `slot` (0 clear,
// 2 storm; a slot with none falls back to 0), and t, how far from lutA to lutB. False when it has no grading.
bool ForeverGradingAt(const LightRec* light, int32_t slot, int32_t time, int32_t* lutA, int32_t* lutB, float* t);

}  // namespace HD

#endif
