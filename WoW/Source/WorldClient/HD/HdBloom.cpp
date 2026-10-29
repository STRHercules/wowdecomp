// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdVulkanBloom.cpp on GxEffects. The donor worked in place on the back buffer; here the
// half-size copy of the frame is CopyColor, the depth is CopyDepth, and the composite writes the world's colour in an
// RGBA16F storage texture that a pass draws back. Pass order, sizes, formulas and cvars are the donor's.
#include "WoW/Source/WorldClient/HD/HdBloom.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdCamera.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    // The donor's images: 0 the half-size frame, 1 2 and 5 6 quarter size (halo, rays), 3 4 eighth size (wide halo).
    constexpr uint32_t kImages = 7;

    enum Pass : uint32_t { Bright, Down, Blur, RayMask, Radial, Composite, kPasses };

    struct Push {
        float target[4];
        float a[4];
        float b[4];
        float c[4];
    };

    // Where the sun is on screen and how to tell the sky in the depth copy.
    struct Post {
        float u = .5f, v = .5f, visible = 0, minDepth = 0, maxDepth = .94f, c2 = 0, d2 = 0, farClip = 1000;
    };

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* programs[kPasses] = {};
        const GxEffects::Program* copy = nullptr;
        GxEffects::Texture* images[kImages] = {};
        GxEffects::Texture* frame = nullptr;
        GxEffects::Texture* depth = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failed = false;
    };

    State s_state;
    CVar* s_bloom = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_threshold = nullptr;
    CVar* s_rays = nullptr;
    CVar* s_rayStrength = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_info = nullptr;
    char s_lastInfo[256] = {};

    void Info(const char* value) {
        if (s_info && std::strcmp(s_lastInfo, value)) {
            std::snprintf(s_lastInfo, sizeof s_lastInfo, "%s", value);
            s_info->Set(s_lastInfo, true, false, false, false);
        }
    }

    float Sat(float x) { return std::clamp(x, 0.0f, 1.0f); }
    uint32_t Divisor(uint32_t image) { return image == 0 ? 2 : (image == 3 || image == 4 ? 8 : 4); }
    uint32_t Dimension(uint32_t size, uint32_t divisor) { return std::max(size / divisor, 1u); }

    bool Finite(const float* v, int n) {
        for (int i = 0; i < n; ++i) {
            if (!std::isfinite(v[i])) return false;
        }
        return true;
    }

    // The donor's BuildPost: the sun's direction into camera space (the camera's right from its forward and world
    // up), then through the projection into the viewport, and how much of it is up and on screen.
    bool BuildPost(const HD::Camera& cam, uint32_t width, uint32_t height, const float sun[3], const float forward[3], float farClip, Post& post) {
        const float* p = cam.projection;
        if (!cam.valid || !Finite(p, 16) || p[0] == 0 || p[5] == 0 || std::fabs(p[11] - 1) > 1e-3f || std::fabs(p[15]) > 1e-3f || p[10] <= 0 || p[14] >= 0
            || cam.maxZ <= cam.minZ || cam.width <= 0 || cam.height <= 0 || !(farClip > 0)) {
            return false;
        }
        float sx = sun[0], sy = sun[1], sz = sun[2];
        float fx = forward[0], fy = forward[1], fz = forward[2];
        if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) || !std::isfinite(fx) || !std::isfinite(fy) || !std::isfinite(fz)) return false;
        float sl = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (sl < 1e-4f) {
            sx = 0;
            sy = 0;
            sz = 1;
            sl = 1;
        }
        sx /= sl;
        sy /= sl;
        sz /= sl;
        float fl = std::sqrt(fx * fx + fy * fy + fz * fz);
        if (fl < 1e-4f) {
            fx = 1;
            fy = 0;
            fz = 0;
            fl = 1;
        }
        fx /= fl;
        fy /= fl;
        fz /= fl;
        float rx = fy, ry = -fx, rl = std::sqrt(rx * rx + ry * ry);
        if (rl < 1e-4f) {
            rx = 0;
            ry = -1;
            rl = 1;
        }
        rx /= rl;
        ry /= rl;
        float ux = ry * fz, uy = -rx * fz, uz = rx * fy - ry * fx;
        float vx = sx * rx + sy * ry, vy = sx * ux + sy * uy + sz * uz, vz = sx * fx + sy * fy + sz * fz;
        Post out;
        out.minDepth = cam.minZ;
        out.maxDepth = cam.maxZ;
        out.c2 = p[10];
        out.d2 = p[14];
        out.farClip = farClip;
        if (vz > .05f) {
            float nx = p[0] * vx / vz + p[8], ny = p[5] * vy / vz + p[9];
            out.u = (cam.x + (nx * .5f + .5f) * cam.width) / width;
            out.v = (cam.y + (.5f - ny * .5f) * cam.height) / height;
            float edge = std::max(std::abs(nx), std::abs(ny));
            out.visible = Sat(sz * 4 + .2f) * Sat((1.6f - edge) / .6f);
        }
        if (!std::isfinite(out.u) || !std::isfinite(out.v)) return false;
        post = out;
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
        if (!st.copy) {
            const uint32_t* code[kPasses] = { kBloomBright, kBloomDown, kBloomBlur, kBloomRayMask, kBloomRadial, kBloomComposite };
            const size_t bytes[kPasses] = { sizeof kBloomBright, sizeof kBloomDown, sizeof kBloomBlur, sizeof kBloomRayMask, sizeof kBloomRadial, sizeof kBloomComposite };
            bool ok = device->CanStore(GxEffects::Format::RGBA16F);
            for (uint32_t i = 0; i < kPasses && ok; ++i) {
                st.programs[i] = device->CreateProgram(code[i], bytes[i], "HD Bloom");
                ok = st.programs[i] != nullptr;
            }
            st.copy = ok ? device->CreateProgram(kCopyFrag, sizeof kCopyFrag, "HD Bloom copy") : nullptr;
            if (!st.copy) {
                st.failed = true;
                Info("bloom: no compute on this device (programs or RGBA16F storage)");
                return false;
            }
        }
        if (st.width != width || st.height != height) {
            for (GxEffects::Texture*& t : st.images) {
                device->Destroy(t);
                t = nullptr;
            }
            device->Destroy(st.frame);
            device->Destroy(st.depth);
            st.depth = nullptr;
            st.images[0] = device->CreateTexture(Dimension(width, 2), Dimension(height, 2), GxEffects::Format::BGRA8, true);
            for (uint32_t i = 1; i < kImages; ++i) {
                st.images[i] = device->CreateStorage(Dimension(width, Divisor(i)), Dimension(height, Divisor(i)), GxEffects::Format::RGBA16F);
            }
            st.frame = device->CreateStorage(width, height, GxEffects::Format::RGBA16F);
            st.width = width;
            st.height = height;
        }
        for (GxEffects::Texture* t : st.images) {
            if (!t) return false;
        }
        return st.frame != nullptr;
    }

    // One compute over image `dst` (-1: the frame), sampling t0..t2 (point when `pointT1`, for the depth).
    bool Run(Pass pass, int32_t dst, GxEffects::Texture* t0, GxEffects::Texture* t1, GxEffects::Texture* t2, Push& c, bool pointT1 = false) {
        State& st = s_state;
        const uint32_t w = dst < 0 ? st.width : Dimension(st.width, Divisor(dst));
        const uint32_t h = dst < 0 ? st.height : Dimension(st.height, Divisor(dst));
        c.target[0] = 1.0f / w;
        c.target[1] = 1.0f / h;
        GxEffects::Compute compute;
        compute.program = st.programs[pass];
        compute.groupsX = (w + 7) / 8;
        compute.groupsY = (h + 7) / 8;
        GxEffects::Texture* inputs[3] = { t0, t1, t2 };
        for (uint32_t i = 0; i < 3; ++i) {
            compute.textures[i].texture = inputs[i];
            compute.textures[i].linear = !(pointT1 && i == 1);
        }
        compute.images[0] = dst < 0 ? st.frame : st.images[dst];
        compute.push = &c;
        compute.pushBytes = sizeof c;
        return st.device->Dispatch(compute);
    }

    void Set(float (&row)[4], float x, float y = 0, float z = 0, float w = 0) {
        row[0] = x;
        row[1] = y;
        row[2] = z;
        row[3] = w;
    }

    // A separable blur of image `first` through `first + 1` and back, `spread` texels apart.
    bool BlurImage(uint32_t first, float spread, Push& c) {
        State& st = s_state;
        const uint32_t divisor = Divisor(first);
        Set(c.a, spread / Dimension(st.width, divisor));
        if (!Run(Blur, first + 1, st.images[first], st.images[first], st.images[first], c)) return false;
        Set(c.a, 0, spread / Dimension(st.height, divisor));
        return Run(Blur, first, st.images[first + 1], st.images[first + 1], st.images[first + 1], c);
    }
}

void HD::Bloom::RegisterCVars() {
    if (s_bloom) return;
    s_bloom = OptionCVars::Register("hdBloom", "HD: bright world halos", "1");
    s_strength = OptionCVars::Register("hdBloomStrength", "HD: halo strength 0..5", "1");
    s_threshold = OptionCVars::Register("hdBloomThreshold", "HD: bright threshold 0..1", "0.7");
    s_rays = OptionCVars::Register("hdRays", "HD: sun rays", "1");
    s_rayStrength = OptionCVars::Register("hdRaysStrength", "HD: rays strength 0..5", "1");
    s_debug = CVar::Register("hdBloomDebug", "HD: 1 mask, 2 rays, 3 halo", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdBloomInfo", "HD: bloom status", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::Bloom::World() {
    const HD::Camera& camera = HD::CurrentCamera();
    const bool bloom = OptionCVars::Bool(s_bloom, false);
    bool rays = OptionCVars::Bool(s_rays, false);
    if (Visuals::Current() != Visuals::AzerothRemastered || (!bloom && !rays)) {
        Info("off");
        return;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!device || !device->TargetSize(width, height)) {
        Info("bloom: no GxEffects device (Vulkan only)");
        return;
    }
    if (!Ready(device, width, height)) {
        if (!s_state.failed) Info("bloom: its textures could not be made");
        return;
    }
    State& st = s_state;
    const float strength = OptionCVars::Float(s_strength, 0.0f, 5.0f, 1.0f);
    const float threshold = OptionCVars::Float(s_threshold, 0.0f, 1.0f, 0.7f);
    const float rayStrength = OptionCVars::Float(s_rayStrength, 0.0f, 5.0f, 1.0f);
    const int32_t debug = OptionCVars::Int(s_debug, 0, 3, 0);

    const DayNight::DNInfo& dn = DayNight::g_dnInfo;
    const C3Vector sunPos = DayNight::g_planets.m_sun.m_position;
    const float sun[3] = { sunPos.x - dn.m_cameraPos.x, sunPos.y - dn.m_cameraPos.y, sunPos.z - dn.m_cameraPos.z };
    const float forward[3] = { dn.m_cameraDir.x, dn.m_cameraDir.y, dn.m_cameraDir.z };
    Post post;
    const bool havePost = BuildPost(camera, width, height, sun, forward, dn.m_farClip > 50 ? dn.m_farClip : 1000, post);
    rays = rays && havePost && (post.visible > .001f || debug > 0);
    if (rays && !st.depth) {
        st.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
    }
    rays = rays && st.depth && device->CopyDepth(st.depth);
    if (!bloom && !rays) {
        Info("bloom skipped: rays need the scene depth and a visible sun");
        return;
    }

    Push c = {};
    bool ok = device->CopyColor(st.images[0]);
    if (ok && bloom) {
        Set(c.a, 1.0f / Dimension(width, 2), 1.0f / Dimension(height, 2));
        Set(c.b, threshold, std::max(threshold * .5f, .05f));
        ok = Run(Bright, 1, st.images[0], st.images[0], st.images[0], c) && BlurImage(1, 1, c) && BlurImage(1, 2, c);
        Set(c.a, 1.0f / Dimension(width, 4), 1.0f / Dimension(height, 4));
        ok = ok && Run(Down, 3, st.images[1], st.images[1], st.images[1], c) && BlurImage(3, 1, c) && BlurImage(3, 2, c);
    }
    if (ok && rays) {
        Set(c.a, post.u, post.v, float(width) / height, .6f);
        Set(c.b, .5f, post.farClip);
        Set(c.c, post.minDepth, post.maxDepth, post.c2, post.d2);
        ok = Run(RayMask, 5, st.images[0], st.depth, st.images[0], c, true);
        Set(c.a, post.u, post.v, .9f, .97f);
        Set(c.b, 1.5f);
        ok = ok && Run(Radial, 6, st.images[5], st.images[5], st.images[5], c);
    }
    const uint32_t halo = bloom ? 1 : 6;
    const uint32_t wide = bloom ? 3 : 6;
    const uint32_t ray = rays ? (debug == 1 ? 5 : 6) : halo;
    Set(c.a, bloom ? strength : 0, rays ? rayStrength * (debug > 0 ? 1 : post.visible) : 0, float(debug));
    Set(c.b, post.u, post.v, float(width) / height);
    ok = ok && device->CopyColor(st.frame) && Run(Composite, -1, st.images[halo], st.images[wide], st.images[ray], c);
    GxEffects::Pass back;
    back.program = st.copy;
    back.textures[0].texture = st.frame;
    if (!ok || !device->Draw(back)) {
        Info("bloom: a copy, compute or the draw back failed");
        return;
    }
    Info(s_rays && s_rays->m_intValue && !rays ? "bloom; rays unavailable (depth, projection or sun)"
                                               : (rays ? (bloom ? "bloom + rays: original formulas" : "rays: original formulas") : "bloom: original formulas"));
}
