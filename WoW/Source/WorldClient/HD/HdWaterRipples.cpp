// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdWaterRipples.cpp on Vulkan: the same ripples, as one GxEffects pass (Shaders/WaterRipples.frag).
#include "WoW/Source/WorldClient/HD/HdWaterRipples.h"

#include <cmath>
#include <cstdint>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/WorldClient/WaterRipple.h"
#include "WoW/Source/WorldClient/World.h"
#include "WoW/Source/WorldClient/HD/HdWaterWaves.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    const uint32_t kSize = 512;          // texels
    const float kArea = 32.0f;           // yards
    const int32_t kMaxRipples = 127;     // 2 constant rows each after the header row, of 256 (the 3.3.5 pool is 128)

    CVar* s_enabled = nullptr;
    CVar* s_strength = nullptr;
    bool s_registered = false;

    GxEffects::Device* s_device = nullptr;
    const GxEffects::Program* s_program = nullptr;
    GxEffects::Texture* s_tex = nullptr;
    bool s_valid = false;                // the texture holds this frame's ripples
    float s_centerX = 0.0f;
    float s_centerY = 0.0f;

    void RegisterCVars() {
        if (s_registered) return;
        s_registered = true;
        s_enabled = CVar::Register("hdWaterRipples", "HD: 1 = ripples on hdWater's water around units in it, 0 = off", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_strength = CVar::Register("hdWaterRippleStrength", "HD: water ripple strength, 1 = normal", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    float Strength() {
        float v = s_strength ? s_strength->m_floatValue : 1.0f;
        if (!(v == v)) return 1.0f;  // NaN
        return v < 0.0f ? 0.0f : (v > 4.0f ? 4.0f : v);
    }
}

void HD::WaterRipplesRegister() {
    RegisterCVars();
}

void HD::WaterRipplesRender(GxEffects::Device* device) {
    RegisterCVars();
    s_valid = false;
    // only the FFT variant of the water shader reads the ripples (hdWaterWaves 1)
    if (!device || !s_enabled->m_intValue || Strength() <= 0.0f || !HD::WaterWavesEnabled()) return;
    if (device != s_device) {
        s_device = device;
        s_program = nullptr;
        s_tex = nullptr;
    }
    if (!s_program) s_program = device->CreateProgram(kWaterRipplesFrag, sizeof kWaterRipplesFrag, "HD water ripples");
    if (!s_tex) s_tex = device->CreateTexture(kSize, kSize, GxEffects::Format::BGRA8, true);
    if (!s_program || !s_tex) return;

    // the area: 32 yards around the camera, its centre on whole texels (the rings do not crawl as the camera moves)
    const float texel = kArea / kSize;
    const float cx = floorf(CWorldScene::s_activeWorldView.x / texel) * texel;
    const float cy = floorf(CWorldScene::s_activeWorldView.y / texel) * texel;
    const float half = kArea * 0.5f;

    static float live[kMaxRipples * 8];
    static float rows[(1 + 2 * kMaxRipples)][4];
    int32_t n = WaterRipple::HdActiveRipples(live, kMaxRipples);
    int32_t count = 0;
    for (int32_t i = 0; i < n; i++) {
        const float* r = &live[i * 8];
        float dx = r[0] - cx;
        float dy = r[1] - cy;
        float radius = r[2] * 0.75f;            // the ring in the 3.3.5 splash texture sits near its edge
        float width = 0.1f + 0.12f * radius;    // the packet widens as the ring grows
        float reach = radius + 3.0f * width;    // the square covers the ring and its packet
        if (fabsf(dx) > half + reach || fabsf(dy) > half + reach || r[3] <= 0.0f) continue;
        float* a = rows[1 + 2 * count];
        float* b = rows[2 + 2 * count];
        a[0] = dx; a[1] = dy; a[2] = radius; a[3] = r[3] > 1.0f ? 1.0f : r[3];
        b[0] = width; b[1] = r[5] != 0.0f ? 1.0f : 0.0f;
        b[2] = cosf(r[4]);                      // forward: the ripple's rotation is minus the unit's facing
        b[3] = -sinf(r[4]);
        count++;
    }
    if (!count) return;                         // no ripple near: the water shader gets strength 0
    rows[0][0] = Strength();
    rows[0][1] = half;
    rows[0][2] = static_cast<float>(count);
    rows[0][3] = 0.0f;

    GxEffects::Pass pass;
    pass.target = s_tex;
    pass.program = s_program;
    pass.constants = rows;
    pass.constantRows = static_cast<uint32_t>(1 + 2 * count);
    if (!device->Draw(pass)) return;
    s_centerX = cx;
    s_centerY = cy;
    s_valid = true;
}

GxEffects::Texture* HD::WaterRipplesTexture() {
    return s_tex;
}

void HD::WaterRipplesConstants(float* out) {
    // uv = (world - (centre - area / 2)) / area
    out[0] = 1.0f / kArea;
    out[1] = 1.0f / kArea;
    out[2] = 0.5f - s_centerX / kArea;
    out[3] = 0.5f - s_centerY / kArea;
    out[4] = s_valid ? 1.0f : 0.0f;
    out[5] = 0.0f;
    out[6] = 0.0f;
    out[7] = 0.0f;
}

void HD::WaterRipplesRelease() {
    s_device = nullptr;
    s_program = nullptr;
    s_tex = nullptr;
    s_valid = false;
}
