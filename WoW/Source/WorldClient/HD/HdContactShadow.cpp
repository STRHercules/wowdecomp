// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdContactShadow.cpp (v4) on GxEffects. The donor drew four D3D9 pixel passes over its target with
// hdFog's INTZ depth; here they are compute passes over CopyDepth's copy and the shared frame (HdFrame.h), the sun
// shadow bands are bound as engine textures (R32F distances in this backend), and the camera is HD::CurrentCamera.
// The constants, their meaning and the cvars are the donor's.
#include "WoW/Source/WorldClient/HD/HdContactShadow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "Engine/Source/Services/Texture.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdCamera.h"
#include "WoW/Source/WorldClient/HD/HdFrame.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    enum Pass : uint32_t { Visibility, Generate, Blur, Apply, kPasses };
    constexpr int kRows = 38;           // c0..c37
    constexpr int kMaxUnits = 16;       // characters nearest to the camera kept out of the contact shadows
    constexpr float kFadeStart = 80.0f; // yards: the contact shadows fade out from here...
    constexpr float kFadeLength = 40.0f;  // ...over this distance

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* programs[kPasses] = {};
        GxEffects::Texture* mask[2] = {};
        GxEffects::Texture* depth = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failed = false;
        bool prepared = false;  // the mask holds this frame's shadows, built before the grass
    };

    State s_state;
    CVar* s_enabled = nullptr;
    CVar* s_length = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_units = nullptr;
    CVar* s_thickness = nullptr;
    CVar* s_noise = nullptr;
    CVar* s_grass = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_info = nullptr;
    char s_lastInfo[160] = {};

    void Info(const char* value) {
        if (s_info && std::strcmp(s_lastInfo, value)) {
            std::snprintf(s_lastInfo, sizeof s_lastInfo, "%s", value);
            s_info->Set(s_lastInfo, true, false, false, false);
        }
    }

    // The programs and the textures for this device and target size. False when they cannot be made (for good on this
    // device when it is the programs).
    bool Ready(GxEffects::Device* device, uint32_t width, uint32_t height) {
        State& st = s_state;
        if (device != st.device) {
            // A new device: what the old one made is gone with it.
            st = State();
            st.device = device;
        }
        if (st.failed) return false;
        if (!st.programs[0]) {
            const uint32_t* code[kPasses] = { kContactVisibility, kContactGenerate, kContactBlur, kContactApply };
            const size_t bytes[kPasses] = { sizeof kContactVisibility, sizeof kContactGenerate, sizeof kContactBlur, sizeof kContactApply };
            bool ok = device->CanStore(GxEffects::Format::RGBA16F);
            for (uint32_t i = 0; i < kPasses && ok; ++i) {
                st.programs[i] = device->CreateProgram(code[i], bytes[i], "HD contact shadows");
                ok = st.programs[i] != nullptr;
            }
            if (!ok) {
                st.failed = true;
                Info("contact shadows: no compute on this device (programs or RGBA16F storage)");
                return false;
            }
        }
        if (st.width != width || st.height != height) {
            for (GxEffects::Texture** t : { &st.mask[0], &st.mask[1], &st.depth }) {
                device->Destroy(*t);
                *t = nullptr;
            }
            st.mask[0] = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.mask[1] = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            st.width = width;
            st.height = height;
        }
        return st.mask[0] && st.mask[1] && st.depth;
    }

    // The client's three sun shadow bands (shadow quality above Medium), as the fog takes them. False when any is missing.
    bool ShadowBands(CGxTex* maps[3]) {
        if (CShadowCache::GetShadowMode() <= SHADOW_MEDIUM) return false;
        for (int i = 0; i < 3; ++i) {
            const CShadowBand& band = CShadowCache::s_extShadowBands[i];
            CTexture* texture = band.m_textures[band.m_current];
            maps[i] = texture ? TextureGetGxTex(texture, 1, nullptr) : nullptr;
            if (!maps[i] || maps[i]->m_needsCreation) return false;
        }
        return true;
    }

    bool Run(Pass pass, GxEffects::Texture* mask, GxEffects::Texture* output, GxEffects::Texture* frame, CGxTex* const* maps,
             const float (*rows)[4]) {
        State& st = s_state;
        GxEffects::Compute compute;
        compute.program = st.programs[pass];
        compute.groupsX = (st.width + 7) / 8;
        compute.groupsY = (st.height + 7) / 8;
        compute.textures[0].texture = st.depth;
        compute.textures[1].texture = mask;
        for (int i = 0; maps && i < 3; ++i) compute.textures[2 + i].engine = maps[i];
        compute.images[0] = output;
        compute.images[1] = frame;
        compute.constants = rows;
        compute.constantRows = kRows;
        return st.device->Dispatch(compute);
    }
}

void HD::RegisterContactShadowCVars() {
    if (s_enabled) return;
    s_enabled = OptionCVars::Register("hdContactShadow", "HD: 1 = screen-space contact shadows, 0 = off", "1");
    s_length = OptionCVars::Register("hdContactShadowLength", "HD: contact shadow ray length in yards, 1.5 = normal", "1.5");
    s_strength = OptionCVars::Register("hdContactShadowStrength", "HD: contact shadow strength, 1 = as dark as the shaders' own shadows (normal), 0 = none", "1");
    s_units = OptionCVars::Register("hdContactShadowUnits", "HD: 1 = characters cast contact shadows too, 0 = they only receive them (default)", "0");
    s_thickness = OptionCVars::Register("hdContactShadowThickness", "HD: yards an occluder is taken to be thick along the view, 1 = donor; lower keeps thin grass shadows from swinging with the camera", "0.3");
    s_noise = OptionCVars::Register("hdContactShadowNoise", "HD: ray start jitter, 1 = donor (a pattern fixed to the screen, which slides over the world when the camera moves), 0 = none", "0");
    s_grass = OptionCVars::Register("hdContactShadowGrass", "HD: 1 = grass casts contact shadows (they shift with the camera: thin cards), 0 = it only receives them", "0");
    s_debug = CVar::Register("hdContactShadowDebug", "HD: 1 = test view, the shadow alone, 2 = the same without the shadow-map check (red = shadow maps)", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdContactShadowInfo", "HD: hdContactShadow status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

// maskOnly: build the shadow mask only (from the depth as it is now). Otherwise apply it, building it first if it was not.
static void Render(bool maskOnly) {
    if (Visuals::Current() != Visuals::AzerothRemastered || !OptionCVars::Bool(s_enabled, false)) {
        Info("off");
        return;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!device || !device->TargetSize(width, height)) {
        Info("contact shadows: no GxEffects device (Vulkan only)");
        return;
    }
    if (!Ready(device, width, height)) {
        if (!s_state.failed) Info("contact shadows: their textures could not be made");
        return;
    }
    const HD::Camera& cam = HD::CurrentCamera();
    float c[kRows][4] = {};
    if (!HD::ScreenRows(cam, width, height, reinterpret_cast<float(*)[4]>(c))) {
        Info("on, projection not supported this frame: contact shadows skipped");
        return;
    }
    // The light the shadow maps use (CShadowQuery::Update: the client's world light, z scaled by hdShadowSun), which
    // falls along s_sunDir; into view space with this frame's view matrix (row vectors), reversed.
    const float* view = cam.view;
    const C3Vector& fall = CShadowCache::s_sunDir;
    float lx = -(fall.x * view[0] + fall.y * view[4] + fall.z * view[8]);
    float ly = -(fall.x * view[1] + fall.y * view[5] + fall.z * view[9]);
    float lz = -(fall.x * view[2] + fall.y * view[6] + fall.z * view[10]);
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    if (!(ll > 1e-4f)) {
        Info("on, no world light direction yet: contact shadows skipped");
        return;
    }
    const int32_t debug = OptionCVars::Int(s_debug, 0, 2, 0);
    // Whether the ground has WoW Forever's lighting (hdTerrainLight 1: its shadow keeps the ambient only).
    const CVar* terrainLight = CVar::LookupRegistered("hdTerrainLight");
    const bool forever = terrainLight && terrainLight->m_intValue == 1;
    const CImVector& amb = DayNight::g_dnInfo.m_light1.m_ambient;
    const CImVector& dif = DayNight::g_dnInfo.m_light1.m_diffuse;
    CGxTex* maps[3] = {};
    const bool shadows = ShadowBands(maps);

    const float row4[4] = { OptionCVars::Float(s_length, .25f, 6, 1.5f), OptionCVars::Float(s_strength, 0, 1, 1), float(debug), OptionCVars::Float(s_thickness, .05f, 2, .3f) };
    const float row5[4] = { 0, 0, kFadeStart, 1.0f / kFadeLength };
    const float row6[4] = { lx / ll, ly / ll, lz / ll, 0 };
    const float row7[4] = { amb.r / 255.0f, amb.g / 255.0f, amb.b / 255.0f, forever ? 1.0f : 0.0f };
    const float row8[4] = { dif.r / 255.0f, dif.g / 255.0f, dif.b / 255.0f, 0 };
    const float row9[4] = { shadows ? 1.0f : 0.0f, OptionCVars::Float(s_noise, 0, 1, 0), 0, 0 };
    std::memcpy(c[4], row4, sizeof row4);
    std::memcpy(c[5], row5, sizeof row5);
    std::memcpy(c[6], row6, sizeof row6);
    std::memcpy(c[7], row7, sizeof row7);
    std::memcpy(c[8], row8, sizeof row8);
    std::memcpy(c[9], row9, sizeof row9);
    if (shadows) std::memcpy(c[10], &CShadowCache::s_shadowMatrices[3], 9 * sizeof c[0]);
    // The characters near the camera (camera-relative world -> view space, rotation only), and the world's up.
    int32_t unitCount = 0;
    if (!OptionCVars::Bool(s_units, false)) {
        HD::ContactShadowUnit units[kMaxUnits];
        unitCount = HD::ContactShadowGatherUnits(units, kMaxUnits);
        for (int32_t i = 0; i < unitCount; i++) {
            const HD::ContactShadowUnit& u = units[i];
            for (int32_t j = 0; j < 3; j++) c[19 + i][j] = u.x * view[j] + u.y * view[4 + j] + u.z * view[8 + j];
            c[19 + i][3] = u.height;
        }
    }
    float ux = view[8], uy = view[9], uz = view[10];
    float ul = std::sqrt(ux * ux + uy * uy + uz * uz);
    if (!(ul > 1e-6f)) {
        ux = 0.0f;
        uy = 1.0f;
        uz = 0.0f;
        ul = 1.0f;
    }
    const float row35[4] = { ux / ul, uy / ul, uz / ul, float(unitCount) };
    std::memcpy(c[35], row35, sizeof row35);
    // The shaders' 5 PCF taps (CShadowCache::InitPcfOffsets: texel steps of this shadow map size).
    const C4Vector* o = CShadowCache::s_pcfOffsets;
    const float pcf[2][4] = { { o[0].x, o[0].y, o[2].x, o[2].y }, { o[4].x, o[4].y, o[6].x, o[6].y } };
    std::memcpy(c[36], pcf, sizeof pcf);

    State& st = s_state;
    CGxTex* const* bound = shadows ? maps : nullptr;
    bool ok = true;
    if (maskOnly || !st.prepared) {
        ok = device->CopyDepth(st.depth);
        ok = ok && Run(Visibility, nullptr, st.mask[1], nullptr, bound, c);
        ok = ok && Run(Generate, st.mask[1], st.mask[0], nullptr, bound, c);
        c[5][0] = 1.0f / width;
        c[5][1] = 0.0f;
        ok = ok && Run(Blur, st.mask[0], st.mask[1], nullptr, bound, c);
        c[5][0] = 0.0f;
        c[5][1] = 1.0f / height;
        ok = ok && Run(Blur, st.mask[1], st.mask[0], nullptr, bound, c);
        st.prepared = ok;
    }
    if (maskOnly) return;
    st.prepared = false;
    GxEffects::Texture* frame = ok ? HD::Frame::Open(width, height) : nullptr;
    if (!frame || !Run(Apply, st.mask[0], nullptr, frame, bound, c)) {
        Info("contact shadows: a copy or a compute failed");
        return;
    }
    char text[160];
    std::snprintf(text, sizeof text, "on%s, shadow maps %s, ground shadow %s, characters %s, v4",
                  debug == 2 ? ", test view (hdContactShadowDebug 2)" : (debug ? ", test view (hdContactShadowDebug 1)" : ""),
                  shadows ? "R32F" : "none (shadow quality below high: no check)", forever ? "Forever (ambient)" : "3.3.5 (70%)",
                  OptionCVars::Bool(s_units, false) ? "cast too" : "receive only");
    Info(text);
}

// Thin grass cards only occlude from one side, whichever way the ray crosses them, so as casters they shift with the
// camera: the mask is built before the grass and applied after it (grass receives the shadows, casts none).
void HD::ContactShadowPrepare() {
    if (!OptionCVars::Bool(s_grass, false)) Render(true);
}

void HD::ContactShadowRenderOpaque() {
    Render(false);
}
