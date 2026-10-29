// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdGI.cpp on GxEffects. The donor drew three D3D9 pixel passes with hdFog's INTZ depth and a copy
// of its target; here they are compute passes over CopyDepth's copy and the shared frame (HdFrame.h), which already
// holds the contact shadows, as the donor's copy did. The constants and the cvars are the donor's.
#include "WoW/Source/WorldClient/HD/HdGI.h"

#include <cstdio>
#include <cstring>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdCamera.h"
#include "WoW/Source/WorldClient/HD/HdFrame.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    enum Pass : uint32_t { Generate, Blur, Apply, kPasses };
    constexpr int kRows = 7;             // c0..c6
    constexpr float kFadeStart = 150.0f; // yards: the bounce fades out from here...
    constexpr float kFadeLength = 100.0f;  // ...over this distance
    constexpr float kMaxAlbedo = 0.5f;   // brightest albedo assumed for a receiving surface (linear)

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* programs[kPasses] = {};
        GxEffects::Texture* gi[2] = {};
        GxEffects::Texture* depth = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failed = false;
    };

    State s_state;
    CVar* s_enabled = nullptr;
    CVar* s_radius = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_info = nullptr;
    const char* s_lastInfo = "";

    void Info(const char* value) {
        if (s_info && std::strcmp(s_lastInfo, value)) {
            s_info->Set(value, true, false, false, false);
            s_lastInfo = value;
        }
    }

    bool Ready(GxEffects::Device* device, uint32_t width, uint32_t height) {
        State& st = s_state;
        if (device != st.device) {
            // A new device: what the old one made is gone with it.
            st = State();
            st.device = device;
        }
        if (st.failed) return false;
        if (!st.programs[0]) {
            const uint32_t* code[kPasses] = { kGIGenerate, kGIBlur, kGIApply };
            const size_t bytes[kPasses] = { sizeof kGIGenerate, sizeof kGIBlur, sizeof kGIApply };
            bool ok = device->CanStore(GxEffects::Format::RGBA16F);
            for (uint32_t i = 0; i < kPasses && ok; ++i) {
                st.programs[i] = device->CreateProgram(code[i], bytes[i], "HD GI");
                ok = st.programs[i] != nullptr;
            }
            if (!ok) {
                st.failed = true;
                Info("GI: no compute on this device (programs or RGBA16F storage)");
                return false;
            }
        }
        if (st.width != width || st.height != height) {
            for (GxEffects::Texture** t : { &st.gi[0], &st.gi[1], &st.depth }) {
                device->Destroy(*t);
                *t = nullptr;
            }
            st.gi[0] = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.gi[1] = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            st.width = width;
            st.height = height;
        }
        return st.gi[0] && st.gi[1] && st.depth;
    }

    bool Run(Pass pass, GxEffects::Texture* gi, GxEffects::Texture* scene, GxEffects::Texture* output, GxEffects::Texture* frame,
             const float (*rows)[4]) {
        State& st = s_state;
        GxEffects::Compute compute;
        compute.program = st.programs[pass];
        compute.groupsX = (st.width + 7) / 8;
        compute.groupsY = (st.height + 7) / 8;
        compute.textures[0].texture = st.depth;
        compute.textures[1].texture = gi;
        compute.textures[2].texture = scene;
        compute.images[0] = output;
        compute.images[1] = frame;
        compute.constants = rows;
        compute.constantRows = kRows;
        return st.device->Dispatch(compute);
    }
}

void HD::RegisterGICVars() {
    if (s_enabled) return;
    s_enabled = OptionCVars::Register("hdGI", "HD: 1 = screen-space global illumination (one colour bounce), 0 = off", "1");
    s_radius = OptionCVars::Register("hdGIRadius", "HD: bounce reach in yards, 4 = normal", "4");
    s_strength = OptionCVars::Register("hdGIStrength", "HD: bounce strength, 1 = normal", "1");
    s_debug = CVar::Register("hdGIDebug", "HD: 1 = test view, the bounced light alone, 2 = test view, the surface colour estimate", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdGIInfo", "HD: hdGI status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::GIRenderOpaque() {
    if (Visuals::Current() != Visuals::AzerothRemastered || !OptionCVars::Bool(s_enabled, false)) {
        Info("off");
        return;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!device || !device->TargetSize(width, height)) {
        Info("GI: no GxEffects device (Vulkan only)");
        return;
    }
    if (!Ready(device, width, height)) {
        if (!s_state.failed) Info("GI: its textures could not be made");
        return;
    }
    float c[kRows][4] = {};
    if (!HD::ScreenRows(HD::CurrentCamera(), width, height, reinterpret_cast<float(*)[4]>(c))) {
        Info("on, projection not supported this frame: GI skipped");
        return;
    }
    const float radius = OptionCVars::Float(s_radius, .5f, 12, 4);
    const int32_t debug = OptionCVars::Int(s_debug, 0, 2, 0);
    // The client's ambient light this frame (DayNight at the hdTime hour), made linear like the shader's colours (gamma 2).
    const CImVector& amb = DayNight::g_dnInfo.m_light1.m_ambient;
    const float ar = amb.r / 255.0f, ag = amb.g / 255.0f, ab = amb.b / 255.0f;
    const float row4[4] = { radius, OptionCVars::Float(s_strength, 0, 4, 1), float(debug), 1.0f / (radius * 0.2f) };
    const float row5[4] = { 0, 0, kFadeStart, 1.0f / kFadeLength };
    const float row6[4] = { ar * ar, ag * ag, ab * ab, kMaxAlbedo };
    std::memcpy(c[4], row4, sizeof row4);
    std::memcpy(c[5], row5, sizeof row5);
    std::memcpy(c[6], row6, sizeof row6);

    // The colours that bounce: the world as drawn so far (the shared frame, with the contact shadows in it).
    State& st = s_state;
    GxEffects::Texture* frame = device->CopyDepth(st.depth) ? HD::Frame::Open(width, height) : nullptr;
    bool ok = frame && Run(Generate, nullptr, frame, st.gi[0], nullptr, c);
    c[5][0] = 1.0f / width;
    c[5][1] = 0.0f;
    ok = ok && Run(Blur, st.gi[0], nullptr, st.gi[1], nullptr, c);
    c[5][0] = 0.0f;
    c[5][1] = 1.0f / height;
    ok = ok && Run(Blur, st.gi[1], nullptr, st.gi[0], nullptr, c);
    ok = ok && Run(Apply, st.gi[0], nullptr, nullptr, frame, c);
    if (!ok) {
        Info("GI: a copy or a compute failed");
        return;
    }
    Info(debug == 2 ? "on, test view (hdGIDebug 2)" : (debug ? "on, test view (hdGIDebug 1)" : "on"));
}
