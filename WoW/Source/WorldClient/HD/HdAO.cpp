// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdVulkanAO.cpp on GxEffects. The donor ran in place on its back buffer with its own depth and
// optional G-buffer normals; here the depth is CopyDepth, the normals always come from the depth (the donor's
// hdAONormals 0), the apply pass writes the shared frame (HdFrame.h), and the view matrix is made absolute from the engine's camera-relative one. Passes, sizes and constants
// are the donor's.
#include "WoW/Source/WorldClient/HD/HdAO.h"

#include <algorithm>
#include <cmath>
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

    struct Constants {
        float screen[4], view[4], rect[4], depth[4], params[4], blur[4], full[4];
        uint32_t flags[4];  // pass, debug, G-buffer normals (never here), half resolution
    };
    static_assert(sizeof(Constants) == GxEffects::kMaxPushBytes, "the shader's push block");

    // Pipelines in the donor's order: 2..5 are the main pass at quality 0..3.
    enum Program : uint32_t { Prefilter, Mip, MainLow, MainMedium, MainHigh, MainUltra, Blur, Apply, Temporal, kPrograms };

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* programs[kPrograms] = {};
        GxEffects::Texture* z[5] = {};          // the view depth pyramid (R32F, each level half the one before)
        GxEffects::Texture* ao[2] = {};         // AO + packed edges, A then B (the blurs ping-pong)
        GxEffects::Texture* history[2] = {};    // temporal: AO, view depth, frames, valid (one per frame, ping-pong)
        GxEffects::Texture* depth = nullptr;
        uint32_t width = 0, height = 0, aoWidth = 0, aoHeight = 0;
        bool half = false;
        bool failed = false;
        // Temporal: the frame this ran last (World calls, counted), the history it wrote, and its camera.
        uint64_t frameNumber = 0, historyFrame = 0;
        uint32_t historyIndex = 0;
        bool havePrevious = false;
        double previousView[16] = {}, previousProjection[16] = {};
        float previousViewport[4] = {};
    };

    State s_state;
    CVar* s_enabled = nullptr;
    CVar* s_radius = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_half = nullptr;
    CVar* s_quality = nullptr;
    CVar* s_temporal = nullptr;
    CVar* s_info = nullptr;
    const char* s_lastInfo = "";

    void Info(const char* value) {
        if (s_info && std::strcmp(s_lastInfo, value)) {
            s_info->Set(value, true, false, false, false);
            s_lastInfo = value;
        }
    }

    // Row-vector 4x4 helpers in double: world positions are large, the products are not.
    bool Invert(const double* m, double* out) {
        double a[4][8];
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 8; ++k) a[r][k] = k < 4 ? m[r * 4 + k] : (k - 4 == r ? 1 : 0);
        for (int col = 0; col < 4; ++col) {
            int pivot = col;
            for (int r = col + 1; r < 4; ++r)
                if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
            if (std::fabs(a[pivot][col]) < 1e-12) return false;
            if (pivot != col)
                for (int k = 0; k < 8; ++k) std::swap(a[pivot][k], a[col][k]);
            double d = 1 / a[col][col];
            for (int k = 0; k < 8; ++k) a[col][k] *= d;
            for (int r = 0; r < 4; ++r) {
                if (r == col) continue;
                double f = a[r][col];
                for (int k = 0; k < 8; ++k) a[r][k] -= f * a[col][k];
            }
        }
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) out[r * 4 + k] = a[r][k + 4];
        return true;
    }

    void Multiply(const double* a, const double* b, double* out) {
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k) {
                double v = 0;
                for (int i = 0; i < 4; ++i) v += a[r * 4 + i] * b[i * 4 + k];
                out[r * 4 + k] = v;
            }
    }

    // The world-to-view matrix: the engine's view is camera-relative (Lumenveil's WorldViewFromCameraRelative), so the
    // camera's position goes back into its inverse.
    bool AbsoluteView(const HD::Camera& cam, double* out) {
        double relative[16], toWorld[16];
        for (int i = 0; i < 16; ++i) relative[i] = cam.view[i];
        if (!Invert(relative, toWorld)) return false;
        const DayNight::DNInfo& dn = DayNight::g_dnInfo;
        toWorld[12] = dn.m_cameraPos.x;
        toWorld[13] = dn.m_cameraPos.y;
        toWorld[14] = dn.m_cameraPos.z;
        return Invert(toWorld, out);
    }

    // The donor's BuildConstants, from this frame's camera.
    bool BuildConstants(const HD::Camera& cam, uint32_t fullW, uint32_t fullH, bool half, Constants& out) {
        const float* p = cam.projection;
        for (int i = 0; i < 16; ++i) {
            if (!std::isfinite(p[i])) return false;
        }
        // D3D9's perspective and forward-Z contract; refuse a reversed or orthographic depth.
        if (!cam.valid || cam.maxZ <= cam.minZ || p[0] == 0 || p[5] == 0 || std::fabs(p[11] - 1) > 1e-3f || std::fabs(p[15]) > 1e-3f
            || p[10] <= 0 || p[14] >= 0) {
            return false;
        }
        const uint32_t width = half ? (fullW + 1) / 2 : fullW;
        const uint32_t height = half ? (fullH + 1) / 2 : fullH;
        Constants c = {};
        c.screen[0] = 1.0f / width;
        c.screen[1] = 1.0f / height;
        c.screen[2] = 1.0f / (cam.maxZ - cam.minZ);
        c.screen[3] = -cam.minZ * c.screen[2];
        c.view[0] = 2 / p[0];
        c.view[1] = -2 / p[5];
        c.view[2] = (-1 - p[8]) / p[0];
        c.view[3] = (1 - p[9]) / p[5];
        c.rect[0] = cam.x / fullW;
        c.rect[1] = cam.y / fullH;
        c.rect[2] = fullW / cam.width;
        c.rect[3] = fullH / cam.height;
        c.depth[0] = p[10];
        c.depth[1] = -p[14];
        c.depth[2] = .99999f;
        c.params[0] = OptionCVars::Float(s_radius, .25f, 6, 1);
        c.params[1] = OptionCVars::Float(s_strength, 0, 3, .85f);
        c.params[2] = 2.2f;  // upstream's final power
        c.params[3] = 3.3f;  // and depth mip offset
        c.blur[2] = 150;
        c.blur[3] = .01f;
        c.full[0] = 1.0f / fullW;
        c.full[1] = 1.0f / fullH;
        c.flags[1] = OptionCVars::Bool(s_debug, false) ? 1 : 0;
        c.flags[3] = half ? 1 : 0;
        const float* rows[] = { c.screen, c.view, c.rect, c.depth, c.params, c.blur, c.full };
        for (const float* row : rows)
            for (int i = 0; i < 4; ++i)
                if (!std::isfinite(row[i])) return false;
        out = c;
        return true;
    }

    // View position -> the previous frame's work pixel (x * w, y * w) and w (its view depth), from that frame's camera.
    bool Reprojection(const State& st, const double* view, bool half, float out[12]) {
        double inverse[16], a[16], m[16];
        if (!st.havePrevious || !Invert(view, inverse)) return false;
        Multiply(inverse, st.previousView, a);
        Multiply(a, st.previousProjection, m);
        const float* vp = st.previousViewport;
        const double ax = .5 * vp[2], bx = vp[0] - .5 + .5 * vp[2], ay = -.5 * vp[3], by = vp[1] - .5 + .5 * vp[3];
        for (int k = 0; k < 4; ++k) {
            double w = m[k * 4 + 3], x = ax * m[k * 4] + bx * w, y = ay * m[k * 4 + 1] + by * w;
            if (half) {
                x = (x - w) * .5;  // full pixel index -> work pixel (centre 2p+1)
                y = (y - w) * .5;
            }
            out[k] = float(x);
            out[4 + k] = float(y);
            out[8 + k] = float(w);
        }
        for (int i = 0; i < 12; ++i)
            if (!std::isfinite(out[i])) return false;
        return true;
    }

    // The programs and the textures for this device, target size and resolution. False when they cannot be made (for
    // good on this device when it is the programs).
    bool Ready(GxEffects::Device* device, uint32_t width, uint32_t height, bool half) {
        State& st = s_state;
        if (device != st.device) {
            // A new device: what the old one made is gone with it.
            st = State();
            st.device = device;
        }
        if (st.failed) return false;
        if (!st.programs[0]) {
            const uint32_t* code[kPrograms] = { kAOPrefilter, kAOMip, kAOMainLow, kAOMainMedium, kAOMainHigh, kAOMainUltra, kAOBlur, kAOApply, kAOTemporal };
            const size_t bytes[kPrograms] = { sizeof kAOPrefilter, sizeof kAOMip, sizeof kAOMainLow, sizeof kAOMainMedium, sizeof kAOMainHigh,
                                              sizeof kAOMainUltra, sizeof kAOBlur, sizeof kAOApply, sizeof kAOTemporal };
            bool ok = device->CanStore(GxEffects::Format::RGBA16F) && device->CanStore(GxEffects::Format::R32F);
            for (uint32_t i = 0; i < kPrograms && ok; ++i) {
                st.programs[i] = device->CreateProgram(code[i], bytes[i], "HD AO");
                ok = st.programs[i] != nullptr;
            }
            if (!ok) {
                st.failed = true;
                Info("AO: no compute on this device (programs, RGBA16F or R32F storage)");
                return false;
            }
        }
        if (st.width != width || st.height != height || st.half != half) {
            for (GxEffects::Texture** t : { &st.depth, &st.ao[0], &st.ao[1], &st.history[0], &st.history[1], &st.z[0], &st.z[1], &st.z[2], &st.z[3], &st.z[4] }) {
                device->Destroy(*t);
                *t = nullptr;
            }
            const uint32_t aw = half ? (width + 1) / 2 : width;
            const uint32_t ah = half ? (height + 1) / 2 : height;
            for (uint32_t level = 0; level < 5; ++level) {
                const uint32_t block = 1u << level;
                st.z[level] = device->CreateStorage((aw + block - 1) / block, (ah + block - 1) / block, GxEffects::Format::R32F);
            }
            for (uint32_t i = 0; i < 2; ++i) {
                st.ao[i] = device->CreateStorage(aw, ah, GxEffects::Format::RGBA16F);
                st.history[i] = device->CreateStorage(aw, ah, GxEffects::Format::RGBA16F);
            }
            st.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            st.width = width;
            st.height = height;
            st.aoWidth = aw;
            st.aoHeight = ah;
            st.half = half;
            st.historyFrame = 0;
        }
        for (GxEffects::Texture* t : st.z) {
            if (!t) return false;
        }
        return st.ao[0] && st.ao[1] && st.history[0] && st.history[1] && st.depth;
    }
}

void HD::AO::RegisterCVars() {
    if (s_enabled) return;
    s_enabled = OptionCVars::Register("hdAO", "HD: ambient occlusion, 0 off / 1 on", "1");
    s_radius = OptionCVars::Register("hdAORadius", "HD: AO radius in yards (0.25..6)", "1");
    s_strength = OptionCVars::Register("hdAOStrength", "HD: AO strength (0..3)", "0.85");
    s_debug = CVar::Register("hdAODebug", "HD: 1 shows AO alone", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_half = OptionCVars::Register("hdAOHalfRes", "HD: GTAO: 0 full resolution / 1 bilateral half resolution", "0");
    s_quality = OptionCVars::Register("hdAOQuality", "HD: XeGTAO 0 low / 1 medium / 2 high / 3 ultra", "1");
    s_temporal = OptionCVars::Register("hdAOTemporal", "HD: GTAO 1 accumulates over frames (removes the grain) / 0 single frame", "1");
    s_info = CVar::Register("hdAOInfo", "HD: ambient occlusion status", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::AO::World() {
    State& st = s_state;
    ++st.frameNumber;
    if (Visuals::Current() != Visuals::AzerothRemastered || !OptionCVars::Bool(s_enabled, false)) {
        Info("off");
        return;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!device || !device->TargetSize(width, height)) {
        Info("AO: no GxEffects device (Vulkan only)");
        return;
    }
    const bool half = OptionCVars::Bool(s_half, false);
    if (!Ready(device, width, height, half)) {
        if (!st.failed) Info("AO: its textures could not be made");
        return;
    }
    const HD::Camera& cam = HD::CurrentCamera();
    Constants c;
    double view[16];
    if (!BuildConstants(cam, width, height, half, c) || !AbsoluteView(cam, view)) {
        Info("AO: no usable camera this frame (needs forward-Z perspective depth)");
        return;
    }

    const bool temporal = OptionCVars::Bool(s_temporal, true);
    const uint32_t previous = st.historyIndex, current = previous ^ 1u;
    float reprojection[12] = {};
    const bool reuse = temporal && st.historyFrame && st.frameNumber == st.historyFrame + 1 && Reprojection(st, view, half, reprojection);
    c.blur[0] = temporal ? float(st.frameNumber % 64) : 0;  // the noise frame; 0 is the fixed pattern
    const uint32_t quality = static_cast<uint32_t>(OptionCVars::Int(s_quality, 0, 3, 1));

    GxEffects::Texture* frame = device->CopyDepth(st.depth) ? HD::Frame::Open(width, height) : nullptr;
    bool ok = frame != nullptr;
    // Passes 0-4 the depth mips, 5 GTAO, 6-8 denoise, 9 temporal (optional), 10 apply.
    for (uint32_t pass = 0; pass < 11 && ok; ++pass) {
        if (pass == 9 && !temporal) continue;
        GxEffects::Compute compute;
        compute.textures[0].texture = st.depth;
        // A level a pass writes is not also sampled: the mips read only the levels below theirs.
        const uint32_t levels = pass < 5 ? pass : 5;
        for (uint32_t i = 0; i < levels; ++i) compute.textures[1 + i].texture = st.z[i];
        GxEffects::Texture* aoIn = pass == 7 || pass == 9 ? st.ao[1] : st.ao[0];
        if (pass == 10) aoIn = temporal ? st.history[current] : st.ao[1];
        if (pass >= 6) compute.textures[7].texture = aoIn;
        if (pass == 9) compute.textures[8].texture = st.history[previous];
        if (pass < 5) compute.images[0] = st.z[pass];
        else if (pass < 9) compute.images[0] = pass == 6 || pass == 8 ? st.ao[1] : st.ao[0];
        else if (pass == 9) compute.images[0] = st.history[current];
        else compute.images[1] = frame;

        const uint32_t program = pass == 0 ? Prefilter : pass < 5 ? Mip : pass == 5 ? MainLow + quality : pass < 9 ? Blur : pass == 9 ? Temporal : Apply;
        Constants push = c;
        push.flags[0] = pass < 5 ? pass : (pass == 8 ? 1 : 0);
        if (pass == 9) {
            std::memcpy(push.screen, reprojection, 16);
            std::memcpy(push.depth, reprojection + 4, 16);
            std::memcpy(push.params, reprojection + 8, 16);
            // max frames, history usable, clamp margin, depth tolerance
            push.blur[0] = 16;
            push.blur[1] = reuse ? 1.0f : 0.0f;
            push.blur[2] = .02f;
            push.blur[3] = .04f;
        }
        const uint32_t block = pass < 5 ? 1u << pass : 1;
        const uint32_t w = pass == 10 ? width : (st.aoWidth + block - 1) / block;
        const uint32_t h = pass == 10 ? height : (st.aoHeight + block - 1) / block;
        compute.program = st.programs[program];
        compute.groupsX = (w + 7) / 8;
        compute.groupsY = (h + 7) / 8;
        compute.push = &push;
        compute.pushBytes = sizeof push;
        ok = device->Dispatch(compute);
    }
    if (!HD::Frame::Close() || !ok) {
        Info("AO: a copy, compute or the draw back failed");
        return;
    }
    if (temporal) {
        st.historyIndex = current;
        st.historyFrame = st.frameNumber;
    } else {
        st.historyFrame = 0;
    }
    std::memcpy(st.previousView, view, sizeof view);
    for (int i = 0; i < 16; ++i) st.previousProjection[i] = cam.projection[i];
    st.previousViewport[0] = cam.x;
    st.previousViewport[1] = cam.y;
    st.previousViewport[2] = cam.width;
    st.previousViewport[3] = cam.height;
    st.havePrevious = true;
    static const char* const names[2][2][2] = {
        { { "XeGTAO: full resolution", "XeGTAO: full resolution, temporal" }, { "XeGTAO: bilateral half resolution", "XeGTAO: bilateral half resolution, temporal" } },
        { { "XeGTAO: debug", "XeGTAO: debug, temporal" }, { "XeGTAO: debug, half resolution", "XeGTAO: debug, half resolution, temporal" } },
    };
    Info(names[c.flags[1]][half ? 1 : 0][temporal ? 1 : 0]);
}
