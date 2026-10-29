// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdVulkanFog.cpp on GxEffects. The donor fogged its back buffer in place with its own depth;
// here the depth is CopyDepth and the composite writes the shared frame (HdFrame.h). The client's three sun shadow
// bands are bound as engine textures (the backend's are R32F colour targets: the shader's raw-depth path), and there
// is no Cata water plane yet. Layer choice, the constant rows, the march and the blurs are the donor's.
#include "WoW/Source/WorldClient/HD/HdFog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "WoW/Source/DB/Rec/LightRec.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdCamera.h"
#include "WoW/Source/WorldClient/HD/HdForever.h"
#include "WoW/Source/WorldClient/HD/HdFrame.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    struct Row {
        float x = 0, y = 0, z = 0, w = 0;
    };

    // The shader's constant block (std140): the donor's D3D9 c0..c33, the viewport (normalized offset xy, inverse
    // normalized size zw) and extra (projection jitter xy, water plane height z and its flag w; all zero here).
    struct Parameters {
        Row r[34];
        Row viewport;
        Row extra;
    };
    static_assert(sizeof(Parameters) == 36 * 16, "the constant rows");

    enum Pass : uint32_t { Single, March, Blur, Composite, kPasses };

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* programs[kPasses] = {};
        GxEffects::Texture* scatter[2] = {};
        GxEffects::Texture* depth = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failed = false;
    };

    State s_state;
    CVar* s_enabled = nullptr;
    CVar* s_density = nullptr;
    CVar* s_sun = nullptr;
    CVar* s_ambient = nullptr;
    CVar* s_shadow = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_info = nullptr;
    char s_lastInfo[256] = {};
    // Which lights the fog came from this frame, for hdFogInfo: "L<id> s<setting>" with each nearby light's weight.
    char s_sources[160] = {};

    void Source(const LightRec* light, const HD::ForeverFog* fog, float weight) {
        const size_t used = std::strlen(s_sources);
        if (used + 24 >= sizeof s_sources) return;
        std::snprintf(s_sources + used, sizeof s_sources - used, "%sL%d %s%d w%.2f", used ? ", " : "", light->m_id,
                      fog ? "s" : "none", fog ? fog->setting : 0, weight);
    }

    void Info(const char* value) {
        if (s_info && std::strcmp(s_lastInfo, value)) {
            std::strncpy(s_lastInfo, value, sizeof s_lastInfo - 1);
            s_info->Set(s_lastInfo, true, false, false, false);
        }
    }

    float Clamp(float x, float lo, float hi) { return std::isfinite(x) ? std::clamp(x, lo, hi) : lo; }

    bool Invert(const float* m, float* out) {
        float inv[16];
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
        float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::fabs(det) < 1e-12f) return false;
        det = 1.0f / det;
        for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
        return true;
    }

    // The donor's Prepare: the projection, the camera's rotation and the viewport, from this frame's camera. Refuses
    // a reversed-Z or orthographic projection instead of running the D3D9 formula on it.
    bool Prepare(const HD::Camera& cam, uint32_t width, uint32_t height, Parameters& out) {
        const float* p = cam.projection;
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(p[i]) || !std::isfinite(cam.view[i])) return false;
        }
        if (!cam.valid || p[0] == 0 || p[5] == 0 || std::fabs(p[11] - 1) > 1e-3f || std::fabs(p[15]) > 1e-3f || p[10] <= 0 || p[14] >= 0
            || cam.maxZ <= cam.minZ || cam.width <= 0 || cam.height <= 0) {
            return false;
        }
        float inv[16];
        if (!Invert(cam.view, inv)) return false;
        out.r[0] = { 1.0f / width, 1.0f / height, p[10], p[14] };
        out.r[1] = { p[0], p[5], p[8], p[9] };
        for (int i = 0; i < 3; ++i) out.r[2 + i] = { inv[i * 4], inv[i * 4 + 1], inv[i * 4 + 2], 0 };
        out.r[7].x = cam.minZ;
        out.r[7].y = cam.maxZ;
        out.viewport = { cam.x / width, cam.y / height, width / cam.width, height / cam.height };
        return true;
    }

    // Two Forever fogs blended, t = 0 keeps a, 1 gives b.
    void LerpFog(HD::ForeverFog& a, const HD::ForeverFog& b, float t) {
        for (int i = 0; i < 3; i++) HD::LerpFogLayer(a.layers[i], b.layers[i], t);
        if (b.count > a.count) a.count = b.count;
    }

    // Forever's fog for one light, clear weather blended to storm like the client's colours.
    bool LightFog(const LightRec* light, int32_t time, float storm, HD::ForeverFog& out) {
        if (!HD::ForeverFogAt(light, 0, time, &out)) return false;
        if (storm > 0.0f) {
            HD::ForeverFog stormFog;
            if (HD::ForeverFogAt(light, 2, time, &stormFog)) LerpFog(out, stormFog, storm);
        }
        return true;
    }

    // The fog where the camera is: the zone light, then the nearby area lights blended in by distance (the falloff of
    // DayNight::BlendAreaLight). Lights Forever has without fog fade the fog out; nearby lights it does not have at all
    // are skipped (a custom light at full weight beside one with fog flipped the fog as their order did). How much Forever
    // fog there is here (0 none).
    float CurrentForeverFog(HD::ForeverFog& fog) {
        const DayNight::DNInfo& info = DayNight::g_dnInfo;
        const TSGrowableArray<const LightRec*>& lights = DayNight::g_areaLights;
        if (!lights.m_count) return 0.0f;
        int32_t time = static_cast<int32_t>(info.m_dayProgression * 2880.0f);
        float storm = info.m_stormBlend;

        bool have = LightFog(lights.m_data[0], time, storm, fog);
        float strength = have ? 1.0f : 0.0f;
        s_sources[0] = '\0';
        Source(lights.m_data[0], have ? &fog : nullptr, 1.0f);

        struct Near {
            const LightRec* light;
            float distSq;
            float weight;
        };
        Near nearLights[32];
        int32_t count = 0;
        for (uint32_t i = 1; i < lights.m_count && count < 32; i++) {
            const LightRec* light = lights.m_data[i];
            float dx = info.m_lightRefPos.x - light->m_gameCoords[0];
            float dy = info.m_lightRefPos.y - light->m_gameCoords[1];
            float dz = info.m_lightRefPos.z - light->m_gameCoords[2];
            float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq >= light->m_gameFalloffEnd * light->m_gameFalloffEnd) continue;
            float distance = sqrtf(distSq);
            float weight = 1.0f;
            if (distance > light->m_gameFalloffStart) {
                float span = light->m_gameFalloffEnd - light->m_gameFalloffStart;
                weight = span > 0.0f ? Clamp(1.0f - (distance - light->m_gameFalloffStart) / span, 0.0f, 1.0f) : 0.0f;
            }
            if (weight <= 0.0f || !HD::ForeverHasLight(light)) continue;
            nearLights[count].light = light;
            nearLights[count].distSq = distSq;
            nearLights[count].weight = weight;
            count++;
        }
        // farthest first, so the nearest light wins
        for (int32_t i = 1; i < count; i++) {
            Near n = nearLights[i];
            int32_t j = i - 1;
            while (j >= 0 && nearLights[j].distSq < n.distSq) {
                nearLights[j + 1] = nearLights[j];
                j--;
            }
            nearLights[j + 1] = n;
        }
        for (int32_t i = 0; i < count; i++) {
            HD::ForeverFog other;
            float w = nearLights[i].weight;
            const bool found = LightFog(nearLights[i].light, time, storm, other);
            Source(nearLights[i].light, found ? &other : nullptr, w);
            if (found) {
                if (!have) {
                    fog = other;
                    have = true;
                    strength = w;
                } else {
                    LerpFog(fog, other, w);
                    strength += (1.0f - strength) * w;
                }
            } else {
                strength *= 1.0f - w;
            }
        }
        return have ? strength : 0.0f;
    }

    // The donor's BuildLive: camera, sun, debug mode and the three layers' rows.
    void BuildLayers(const HD::ForeverFog& fog, float weight, int32_t mode, Parameters& p) {
        const DayNight::DNInfo& dn = DayNight::g_dnInfo;
        p.r[5] = { dn.m_cameraPos.x, dn.m_cameraPos.y, dn.m_cameraPos.z, dn.m_farClip > 50 ? dn.m_farClip : 1000 };
        C3Vector sun = DayNight::g_planets.m_sun.m_position;
        float sx = sun.x - dn.m_cameraPos.x, sy = sun.y - dn.m_cameraPos.y, sz = sun.z - dn.m_cameraPos.z;
        float length = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (length < 1e-4f) {
            sx = 0;
            sy = 0;
            sz = 1;
            length = 1;
        }
        sx /= length;
        sy /= length;
        sz /= length;
        p.r[6] = { sx, sy, sz, 0 };
        p.r[7].z = float(mode);
        p.r[8] = { 5000, 1.0f / 5000, 0, 0 };
        float den = OptionCVars::Float(s_density, 0, 5, 1) * weight;
        float lit = OptionCVars::Float(s_sun, 0, 5, 1) * .88f;
        float base = OptionCVars::Float(s_ambient, 0, 5, 1) * .034f;
        for (int i = 0; i < 3 && i < fog.count; ++i) {
            const HD::ForeverFogLayer& l = fog.layers[i];
            if (!l.flags || l.density <= 0) continue;
            Row& a = p.r[9 + i * 5];
            Row& b = p.r[10 + i * 5];
            Row& c = p.r[11 + i * 5];
            Row& d = p.r[12 + i * 5];
            Row& e = p.r[13 + i * 5];
            a = { std::pow(l.emissive[0], 2.2f) * base, std::pow(l.emissive[1], 2.2f) * base, std::pow(l.emissive[2], 2.2f) * base, l.density * .0035f * den };
            b = { std::pow(l.diffuse[0], 2.2f) * l.intensity * lit, std::pow(l.diffuse[1], 2.2f) * l.intensity * lit, std::pow(l.diffuse[2], 2.2f) * l.intensity * lit, l.g };
            c = { l.start, l.strength, l.exponent, (l.flags & 1) ? 1.0f : 0 };
            float rel = (l.flags & 2) ? dn.m_playerPos.z : 0;
            d = { l.topRate * .01f, l.topHeight + rel, l.bottomRate * .01f, l.bottomHeight + rel };
            e = { std::pow(l.shadow[0], 2.2f) * base, std::pow(l.shadow[1], 2.2f) * base, std::pow(l.shadow[2], 2.2f) * base, l.shadowMul };
        }
        // r[33]: x shadow maps (set by Shadows), y comparison maps (never here), z how far the sun is up.
        p.r[33].z = Clamp(sz * 4 + .2f, 0, 1);
    }

    // The donor's Maps: the client's three sun shadow bands, when hdFogShadow is on and the shadow mode draws them (above
    // Medium), into texture slots 1..3 with their view-to-shadow matrices in r[24..32]. False (no shadows: the fog is lit
    // wherever the sun is up) when any band has no texture yet: an empty slot would read depth 0, all in shadow.
    bool Shadows(GxEffects::Compute& compute, Parameters& p) {
        if (!OptionCVars::Bool(s_shadow, true) || CShadowCache::GetShadowMode() <= SHADOW_MEDIUM) return false;
        CGxTex* maps[3] = {};
        for (int i = 0; i < 3; ++i) {
            const CShadowBand& band = CShadowCache::s_extShadowBands[i];
            CTexture* texture = band.m_textures[band.m_current];
            maps[i] = texture ? TextureGetGxTex(texture, 1, nullptr) : nullptr;
            if (!maps[i] || maps[i]->m_needsCreation) return false;
        }
        for (int i = 0; i < 3; ++i) compute.textures[1 + i].engine = maps[i];
        static_assert(sizeof(CShadowCache::s_shadowMatrices[0]) == sizeof(Row), "a shadow matrix row");
        std::memcpy(&p.r[24], &CShadowCache::s_shadowMatrices[3], 9 * sizeof(Row));
        p.r[33].x = 1;
        return true;
    }

    // The programs and the textures for this device and target size. False when they cannot be made (for good on
    // this device when it is the programs).
    bool Ready(GxEffects::Device* device, uint32_t width, uint32_t height) {
        State& st = s_state;
        if (device != st.device) {
            // A new device: what the old one made is gone with it.
            st = State();
            st.device = device;
        }
        if (st.failed) return false;
        if (!st.programs[0]) {
            const uint32_t* code[kPasses] = { kFogSingle, kFogMarch, kFogBlur, kFogComposite };
            const size_t bytes[kPasses] = { sizeof kFogSingle, sizeof kFogMarch, sizeof kFogBlur, sizeof kFogComposite };
            bool ok = device->CanStore(GxEffects::Format::RGBA16F);
            for (uint32_t i = 0; i < kPasses && ok; ++i) {
                st.programs[i] = device->CreateProgram(code[i], bytes[i], "HD Fog");
                ok = st.programs[i] != nullptr;
            }
            if (!ok) {
                st.failed = true;
                Info("fog: no compute on this device (programs or RGBA16F storage)");
                return false;
            }
        }
        if (st.width != width || st.height != height) {
            device->Destroy(st.scatter[0]);
            device->Destroy(st.scatter[1]);
            device->Destroy(st.depth);
            st.scatter[0] = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.scatter[1] = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            st.width = width;
            st.height = height;
        }
        return st.scatter[0] && st.scatter[1] && st.depth;
    }
}

void HD::Fog::RegisterCVars() {
    if (s_enabled) return;
    HD::RegisterForeverCVars();
    s_enabled = OptionCVars::Register("hdFog", "HD: global volumetric fog, 0 off / 1 on", "1");
    s_density = OptionCVars::Register("hdFogDensity", "HD: fog density multiplier, 0..5", "3");
    s_sun = OptionCVars::Register("hdFogSun", "HD: sun scattering multiplier, 0..5", "1");
    s_ambient = OptionCVars::Register("hdFogAmbient", "HD: ambient fog multiplier, 0..5", "4.75");
    s_shadow = OptionCVars::Register("hdFogShadow", "HD: sun shadow cascades in the fog", "1");
    s_debug = CVar::Register("hdFogDebug", "HD: 1 distance, 2 depth, 3 fog alone, 4 sun shadows", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdFogInfo", "HD: fog status", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::Fog::World() {
    if (Visuals::Current() != Visuals::AzerothRemastered || !OptionCVars::Bool(s_enabled, false)) {
        Info("off");
        return;
    }
    const int32_t mode = OptionCVars::Int(s_debug, 0, 4, 0);
    HD::ForeverFog fog = {};
    const float weight = CurrentForeverFog(fog);
    if (weight <= 0 && !mode) {
        char text[sizeof s_lastInfo];
        std::snprintf(text, sizeof text, "fog: no fog data here (%s)", s_sources);
        Info(text);
        return;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!device || !device->TargetSize(width, height)) {
        Info("fog: no GxEffects device (Vulkan only)");
        return;
    }
    if (!Ready(device, width, height)) {
        if (!s_state.failed) Info("fog: its textures could not be made");
        return;
    }
    Parameters p = {};
    if (!Prepare(HD::CurrentCamera(), width, height, p)) {
        Info("fog: no usable camera this frame (projection or viewport)");
        return;
    }
    BuildLayers(fog, weight, mode, p);

    State& st = s_state;
    GxEffects::Compute compute;
    compute.groupsX = (width + 7) / 8;
    compute.groupsY = (height + 7) / 8;
    compute.textures[0].texture = st.depth;
    compute.images[1] = st.scatter[0];
    compute.images[2] = st.scatter[1];
    const bool shadows = Shadows(compute, p);
    compute.constants = reinterpret_cast<const float(*)[4]>(&p);
    compute.constantRows = sizeof p / 16;

    compute.images[0] = device->CopyDepth(st.depth) ? HD::Frame::Open(width, height) : nullptr;
    bool ok = compute.images[0] != nullptr;
    // The debug views 1, 2 and 4 show raw data: the single pass. The others march, then blur the march's grain.
    if (mode == 1 || mode == 2 || mode == 4) {
        compute.program = st.programs[Single];
        ok = ok && device->Dispatch(compute);
    } else {
        for (Pass pass : { March, Blur, Composite }) {
            compute.program = st.programs[pass];
            ok = ok && device->Dispatch(compute);
        }
    }
    if (!HD::Frame::Close() || !ok) {
        Info("fog: a copy, compute or the draw back failed");
        return;
    }
    char text[sizeof s_lastInfo];
    std::snprintf(text, sizeof text, "fog %.2f from %s; %s", weight, s_sources,
                  mode ? "debug view" : (shadows ? "shadow maps" : "no shadow maps (sun height retained)"));
    Info(text);
}
