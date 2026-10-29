// Not in the original 3.3.5a binary. Lumenveil's controller: what the mod's hooks.cpp and dllmain.cpp did, called
// from the client's world render instead of from patched call sites. See Lumenveil.h.
#include "Engine/Source/Lumenveil/Lumenveil.h"
#include "Engine/Source/Lumenveil/OptionsPages.h"
#include "Engine/Source/Lumenveil/Renderer.h"

#include "Engine/Source/Lumenveil/Core/config.h"
#include "Engine/Source/Lumenveil/Core/engine.h"
#include "Engine/Source/Lumenveil/Core/fog_data.h"
#include "Engine/Source/Lumenveil/Core/fog_model.h"
#include "Engine/Source/Lumenveil/Core/hooks.h"
#include "Engine/Source/Lumenveil/Core/lamp_catalog.h"
#include "Engine/Source/Lumenveil/Core/liquid_map.h"
#include "Engine/Source/Lumenveil/Core/log.h"
#include "Engine/Source/Lumenveil/Core/regions.h"
#include "Engine/Source/Lumenveil/Core/spots.h"
#include "Engine/Source/Lumenveil/Core/terrain_shadow.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "Engine/Source/ShaderEffect/ShadowCache.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"

#include "Engine/Source/Lumenveil/Shaders/DebugSpirv.inc"

namespace {
    constexpr float kOutOfRangeStockFogStart = 50000.0f;
    constexpr unsigned kConfigReloadIntervalMs = 1000;
    constexpr unsigned kMaxSkipsLogged = 50;
    constexpr float kMinWorldDepthRangeSpan = 0.01f;
    constexpr uint32_t kProbeSize = 4;
    constexpr unsigned kProbeIntervalMs = 3000;

    CVar* s_cvarEnabled;
    CVar* s_cvarDepthView;
    CVar* s_cvarLampTiles;
    CVar* s_cvarTime;

    bool s_started = false;
    bool s_renderedLastFrame = false;
    bool s_liquidDepthArmed = false;
    bool s_renderedThisFrame = false;
    bool s_stockFogPushed = false;
    engine::StockFog s_savedStockFog = {};
    unsigned s_skipsLogged = 0;
    unsigned s_lastReload = 0;
    const char* s_lastSkip = "";

    struct OpaqueState {
        bool valid;
        D3DVIEWPORT9 viewport;
        float cameraRelativeView[16];
        float glProjection[16];
    };

    OpaqueState s_opaque = {};

    // The passes' GPU objects. Phase 1 of the port: the depth copy every pass reads, and a view of it.
    struct Passes {
        GxEffects::Device* device = nullptr;
        GxEffects::Texture* depth = nullptr;
        // The depth after the liquids, before names and spell effects draw: what casts shafts.
        GxEffects::Texture* occluder = nullptr;
        bool occluderFresh = false;
        // The depth as the liquid pass found it, before the water writes its surface.
        GxEffects::Texture* waterBefore = nullptr;
        bool waterBeforeFresh = false;
        GxEffects::Program* depthView = nullptr;
        GxEffects::Program* depthProbe = nullptr;
        GxEffects::Texture* probe = nullptr;
        unsigned probedAt = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool failed = false;
        bool logged = false;
    };

    Passes s_passes;
    LumenveilRenderer s_renderer;

    bool Finite(const float* v, int count) {
        for (int i = 0; i < count; ++i) {
            if (!std::isfinite(v[i])) {
                return false;
            }
        }
        return true;
    }

    bool IsPerspective(const float* p) {
        return Finite(p, 16) && std::fabs(p[11] - 1.0f) < 1e-3f && std::fabs(p[15]) < 1e-3f && std::fabs(p[0]) > 1e-4f && std::fabs(p[5]) > 1e-4f;
    }

    double MonotonicSeconds() {
        static const double frequency = [] {
            LARGE_INTEGER f;
            QueryPerformanceFrequency(&f);
            return static_cast<double>(f.QuadPart);
        }();
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart) / frequency;
    }

    std::string ExecutableDirectory() {
        char path[MAX_PATH] = {};
        const DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
        std::string dir(path, length);
        const size_t slash = dir.find_last_of("\\/");
        return slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
    }

    // lumenveilTime "HH:MM" holds the time of day on this screen only (the region panel's clock override); empty gives
    // the clock back to the server.
    bool TimeChanged(CVar*, const char*, const char* value, void*) {
        int hour = 0;
        int minute = 0;
        if (!value || !*value) {
            Regions().clock.Release();
            VF_LOG_INFO("time: the server's clock again");
            return true;
        }
        if (std::sscanf(value, "%d:%d", &hour, &minute) != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            return false;
        }
        Regions().clock.Hold(hour * 60 + minute, RegionsNow());
        VF_LOG_INFO("time: held at %02d:%02d on this screen", hour, minute);
        return true;
    }

    bool Enabled() {
        return s_started && Visuals::Current() == Visuals::Lumenveil && OptionCVars::Bool(s_cvarEnabled, true) && GlobalConfig().Get().enable;
    }

    bool FogDrawsInPlaceOfStockFog() {
        return s_renderedLastFrame && GlobalConfig().Get().stockFog == 1 && !engine::CameraInLiquid();
    }

    void PushStockFogOutOfRange() {
        s_savedStockFog = engine::ReadStockFog();
        engine::StockFog outOfRange;
        for (int group = 0; group < engine::kDayNightFogGroupCount; ++group) {
            outOfRange.start[group] = kOutOfRangeStockFogStart;
            outOfRange.end[group] = kOutOfRangeStockFogStart * 2.0f;
        }
        engine::WriteStockFog(outOfRange);
        s_stockFogPushed = true;
    }

    void RestorePushedStockFog() {
        if (!s_stockFogPushed) {
            return;
        }
        engine::WriteStockFog(s_savedStockFog);
        s_stockFogPushed = false;
    }

    void ReloadConfigAfterInterval() {
        const unsigned now = GetTickCount();
        if (now - s_lastReload > kConfigReloadIntervalMs) {
            s_lastReload = now;
            GlobalConfig().ReloadIfChanged();
            LogSetLevel(GlobalConfig().Get().logLevel);
        }
    }

    // The passes' objects for this target size; false when the backend has no GxEffects (logged once).
    bool EnsurePasses(uint32_t width, uint32_t height) {
        Passes& p = s_passes;
        GxEffects::Device* device = GxEffects::Get();
        if (!device) {
            if (!p.logged) {
                p.logged = true;
                VF_LOG_INFO("no post passes on this graphics API (Lumenveil draws on Vulkan); the settings still load");
            }
            return false;
        }
        if (p.failed) {
            return false;
        }
        if (device != p.device) {
            p = Passes();
            p.device = device;
        }
        if (!p.depthView) {
            p.depthView = device->CreateProgram(kDepthViewFrag, sizeof kDepthViewFrag, "Lumenveil DepthView");
            p.depthProbe = device->CreateProgram(kDepthProbeFrag, sizeof kDepthProbeFrag, "Lumenveil DepthProbe");
            p.probe = device->CreateTexture(kProbeSize, kProbeSize, GxEffects::Format::R32F, true);
        }
        if (p.width != width || p.height != height) {
            device->Destroy(p.depth);
            device->Destroy(p.occluder);
            device->Destroy(p.waterBefore);
            p.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            p.occluder = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            p.waterBefore = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            p.waterBeforeFresh = false;
            p.occluderFresh = false;
            p.width = width;
            p.height = height;
            VF_LOG_INFO("passes: %ux%u depth copy %s", width, height, p.depth ? "made" : "could not be made");
        }
        if (!p.depthView || !p.depth) {
            p.failed = true;
            VF_LOG_ERROR("passes: the depth copy or its view could not be made; the effects are off for this session");
            return false;
        }
        return true;
    }

    // The projection's depth terms, the viewport's z range, and where the viewport lies in the target.
    void DepthViewConstants(const FrameInputs& in, uint32_t width, uint32_t height, float (*c)[4]) {
        const float span = std::max(in.viewport.MaxZ - in.viewport.MinZ, kMinWorldDepthRangeSpan);
        c[0][0] = in.glProjection[10];
        c[0][1] = in.glProjection[14];
        c[0][2] = in.farClip;
        c[0][3] = in.viewport.MinZ;
        c[1][0] = 1.0f / span;
        c[1][1] = 0.0f;
        c[1][2] = 0.0f;
        c[1][3] = 0.0f;
        c[2][0] = static_cast<float>(in.viewport.X) / static_cast<float>(width);
        c[2][1] = static_cast<float>(in.viewport.Y) / static_cast<float>(height);
        c[2][2] = static_cast<float>(in.viewport.Width) / static_cast<float>(width);
        c[2][3] = static_cast<float>(in.viewport.Height) / static_cast<float>(height);
    }

    // While the depth view is on: every few seconds, the depth copy's stored values on a 4x4 grid over the viewport
    // and the constants the view converts them with. Blocks on the GPU, so only then.
    void LogDepthProbe(GxEffects::Device* device, const float (*constants)[4]) {
        Passes& p = s_passes;
        const unsigned now = GetTickCount();
        if (!p.depthProbe || !p.probe || now - p.probedAt < kProbeIntervalMs) {
            return;
        }
        p.probedAt = now;
        GxEffects::Pass pass;
        pass.target = p.probe;
        pass.program = p.depthProbe;
        pass.textures[0].texture = p.depth;
        pass.constants = constants;
        pass.constantRows = 3;
        float stored[kProbeSize * kProbeSize] = {};
        if (!device->Draw(pass) || !device->Read(p.probe, 0, 0, kProbeSize, kProbeSize, stored)) {
            VF_LOG_ERROR("depth probe: the probe could not be drawn or read back");
            return;
        }
        char rows[kProbeSize][96];
        for (uint32_t y = 0; y < kProbeSize; ++y) {
            std::snprintf(rows[y], sizeof rows[y], "%.5f %.5f %.5f %.5f", stored[y * 4], stored[y * 4 + 1], stored[y * 4 + 2], stored[y * 4 + 3]);
        }
        VF_LOG_INFO("depth probe: proj _33 %.6f _43 %.6f far %.1f z %.3f..span^-1 %.3f, viewport in target %.3f %.3f %.3f %.3f; stored | %s | %s | %s | %s",
                    constants[0][0], constants[0][1], constants[0][2], constants[0][3], constants[1][0], constants[2][0], constants[2][1],
                    constants[2][2], constants[2][3], rows[0], rows[1], rows[2], rows[3]);
    }

    // Copies the depth every pass reads, then draws the fog. With lumenveilDepthView the depth is shown instead of the
    // fog, which proves the copy, the camera and the pass path on this machine.
    bool RenderPasses(const FrameInputs& in, const Config& cfg, const char** skip) {
        GxEffects::Device* device = GxEffects::Get();
        uint32_t width = 0;
        uint32_t height = 0;
        if (!device || !device->TargetSize(width, height) || !EnsurePasses(width, height)) {
            *skip = "no post passes on this device";
            return false;
        }
        if (!device->CopyDepth(s_passes.depth)) {
            *skip = "the world's depth could not be copied";
            return false;
        }
        if (OptionCVars::Bool(s_cvarDepthView, false)) {
            float constants[3][4];
            DepthViewConstants(in, width, height, constants);
            GxEffects::Pass pass;
            pass.x = static_cast<int32_t>(in.viewport.X);
            pass.y = static_cast<int32_t>(in.viewport.Y);
            pass.width = in.viewport.Width;
            pass.height = in.viewport.Height;
            pass.program = s_passes.depthView;
            pass.textures[0].texture = s_passes.depth;
            pass.constants = constants;
            pass.constantRows = 3;
            device->Draw(pass);
            LogDepthProbe(device, constants);
            *skip = "the depth view is on";
            return false;
        }
        GxEffects::Texture* occluder = s_passes.occluderFresh ? s_passes.occluder : nullptr;
        s_passes.occluderFresh = false;
        s_renderer.SetLampTiles(OptionCVars::Bool(s_cvarLampTiles, true));
        // MED-HIGH and above cast shadows from buildings and the environment as well as characters (ShadowCache.cpp's mode names).
        const bool stockObjectShadows = CShadowCache::GetShadowMode() >= SHADOW_MEDIUM_HIGH;
        static int s_loggedStock = -1;
        if (s_loggedStock != (stockObjectShadows ? 1 : 0)) {
            s_loggedStock = stockObjectShadows ? 1 : 0;
            VF_LOG_INFO("shadows: the client's %s; Lumenveil's screen-space shadow of objects is %s", stockObjectShadows ? "own shadows cover buildings and characters" : "shadows do not cover buildings",
                        stockObjectShadows ? "off" : "on");
        }
        s_renderer.SetStockObjectShadows(stockObjectShadows);
        return s_renderer.Render(device, s_passes.depth, occluder, width, height, in, cfg, skip);
    }

    void LogInputsOnce(const FrameInputs& in) {
        static bool logged = false;
        if (logged) {
            return;
        }
        logged = true;
        VF_LOG_INFO("frame inputs: map %d zone %d, camera %.1f %.1f %.1f, day %.3f, %s at elevation %.2f, weather %d (%.2f), "
                    "fog %.0f..%.0f colour %08X, far clip %.0f (client %.0f), viewport %ux%u at %u,%u z %.2f..%.2f, indoors %.2f",
                    in.mapId, in.zoneId, in.camPos[0], in.camPos[1], in.camPos[2], in.dayFraction, in.lightIsMoon ? "moon" : "sun",
                    in.lightIsMoon ? in.moonElevation : in.sunElevation, static_cast<int>(in.weather), in.weatherIntensity, in.fogStart,
                    in.fogEnd, in.fogColor, in.farClip, in.clientFarClip, in.viewport.Width, in.viewport.Height, in.viewport.X,
                    in.viewport.Y, in.viewport.MinZ, in.viewport.MaxZ, in.indoors);
    }
}

// --- engine.h: the opaque state --------------------------------------------------------------------------------------

// The viewport in target pixels as the Vulkan backend places it (CGxDeviceVulkan::IPlaceD3d): the engine's viewport
// is y up, and rows are flipped unless a texture is the target.
void engine::CaptureOpaqueState(uint32_t width, uint32_t height) {
    OpaqueState state = {};
    const CGxDevice* gx = g_theGxDevicePtr;
    if (gx && width && height) {
        const float W = static_cast<float>(width);
        const float H = static_cast<float>(height);
        const CBoundingBox& v = gx->m_viewport;
        const bool target = gx->m_textureTarget[0].m_texture != nullptr || gx->m_textureTarget[1].m_texture != nullptr;
        const float x0 = std::floor(v.m_x.m_l * W + 0.5f);
        const float x1 = std::floor(v.m_x.m_h * W + 0.5f);
        const float y0 = target ? std::floor(v.m_y.m_l * H + 0.5f) : std::floor((1.0f - v.m_y.m_h) * H + 0.5f);
        const float y1 = target ? std::floor(v.m_y.m_h * H + 0.5f) : std::floor((1.0f - v.m_y.m_l) * H + 0.5f);
        state.viewport.X = static_cast<DWORD>(std::clamp(x0, 0.0f, W));
        state.viewport.Y = static_cast<DWORD>(std::clamp(y0, 0.0f, H));
        state.viewport.Width = static_cast<DWORD>(std::clamp(x1, x0, W) - std::clamp(x0, 0.0f, W));
        state.viewport.Height = static_cast<DWORD>(std::clamp(y1, y0, H) - std::clamp(y0, 0.0f, H));
        state.viewport.MinZ = std::clamp(v.m_z.m_l, 0.0f, 1.0f);
        state.viewport.MaxZ = std::clamp(v.m_z.m_h, 0.0f, 1.0f);
        C44Matrix view;
        GxXformView(view);
        std::memcpy(state.cameraRelativeView, &view, sizeof state.cameraRelativeView);
        std::memcpy(state.glProjection, &gx->m_projection, sizeof state.glProjection);
        state.valid = state.viewport.Width > 0 && state.viewport.Height > 0 && IsPerspective(state.glProjection) && Finite(state.cameraRelativeView, 16);
    }
    s_opaque = state;
}

bool engine::HasOpaqueState() {
    return s_opaque.valid;
}

bool engine::OpaqueViewport(D3DVIEWPORT9& out) {
    if (!s_opaque.valid) {
        return false;
    }
    out = s_opaque.viewport;
    return true;
}

void engine::ClearOpaqueState() {
    s_opaque.valid = false;
}

bool engine::BuildFrameInputs(FrameInputs& out) {
    if (!s_opaque.valid) {
        return false;
    }
    std::memcpy(out.cameraRelativeView, s_opaque.cameraRelativeView, sizeof out.cameraRelativeView);
    std::memcpy(out.glProjection, s_opaque.glProjection, sizeof out.glProjection);
    out.viewport = s_opaque.viewport;
    out.seconds = MonotonicSeconds();
    const bool valid = ReadClientWorld(out);
    out.farClip = out.glProjection[14] / (1.0f - out.glProjection[10]);
    if (!(out.farClip > 10.0f && out.farClip < 100000.0f)) {
        out.farClip = out.clientFarClip;
    }
    if (!(out.farClip > 10.0f && out.farClip < 100000.0f)) {
        out.farClip = 1000.0f;
    }
    return valid;
}

float engine::IndoorWeight(float current, bool indoors, double stepSeconds) {
    const float change = static_cast<float>(stepSeconds / kIndoorFadeSeconds);
    return indoors ? std::min(current + change, 1.0f) : std::max(current - change, 0.0f);
}

// --- hooks.h ---------------------------------------------------------------------------------------------------------

void RequestFarClipRefresh() {
}

void RefreshFarClipNow() {
}

FogFrameStatus LastFogFrameStatus() {
    if (s_renderedLastFrame) {
        return { true, "" };
    }
    return { false, *s_lastSkip ? s_lastSkip : "waiting for the world to render" };
}

// --- the controller --------------------------------------------------------------------------------------------------

void Lumenveil::Startup() {
    if (s_started) {
        return;
    }
    Visuals::Register();
    s_cvarEnabled = OptionCVars::Register("lumenveil", "Lumenveil: fog, light shafts, night lights and water", "1");
    s_cvarDepthView = CVar::Register("lumenveilDepthView", "Lumenveil: show the world depth the effects read", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    // A debug aid: 0 makes every pixel test every lamp (no tile lists); reset to 1 at every start.
    s_cvarLampTiles = CVar::Register("lumenveilLampTiles", "Lumenveil: lamp tile lists (0 tests every lamp at every pixel)", 0, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    if (s_cvarLampTiles) {
        s_cvarLampTiles->Set("1", true, false, false, true);
    }
    // A debug view: off at every start, whatever Config.wtf kept from the last session.
    if (s_cvarDepthView) {
        s_cvarDepthView->Set("0", true, false, false, true);
    }
    // Not saved: a held clock is for looking at a time of day now, not for the next session.
    s_cvarTime = CVar::Register("lumenveilTime", "Lumenveil: hold the time of day on this screen (HH:MM; empty for the server's)", 0, "", TimeChanged,
                                CATEGORY_GRAPHICS, false, nullptr, false);
    if (s_cvarTime) {
        s_cvarTime->Set("", true, false, false, true);
    }

    const std::string dir = ExecutableDirectory();
    CreateDirectoryA((dir + "Logs").c_str(), nullptr);
    LogOpen((dir + "Logs\\Lumenveil.log").c_str());
    GlobalConfig().Load(dir + "CoAVolFog.ini");
    LogSetLevel(GlobalConfig().Get().logLevel);
    SpotsSetDirectory(dir);
    RegionsAttach(dir);
    RegionsInstallClient();
    GlobalFogData().Load(dir + "fogdata.bin");
    GlobalLampCatalog().Load(dir + "lampdata.bin");
    LumenveilOptions::RegisterCVars();
    SetHeightSource(&engine::ProbeGroundHeight, nullptr);
    if (GlobalLiquidMap().Load(dir + "liquiddata.bin")) {
        SetLiquidSource(&LiquidMap::Probe, &GlobalLiquidMap());
    }
    VF_LOG_INFO("Lumenveil (native) started in %s; settings %s", dir.c_str(), GlobalConfig().Path().c_str());
    s_started = true;
}

void Lumenveil::FrameBegin() {
    s_renderedThisFrame = false;
    if (Enabled() && FogDrawsInPlaceOfStockFog()) {
        PushStockFogOutOfRange();
    }
}

void Lumenveil::OpaqueBegin() {
}

void Lumenveil::OpaqueDone() {
    GxEffects::Device* device = Enabled() ? GxEffects::Get() : nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    if (device && device->TargetSize(width, height)) {
        engine::CaptureOpaqueState(width, height);
    }
}

// The client draws liquids without depth writes; with LiquidDepth the water surface writes its own, so the depth the
// passes read after the liquids has the surface, and fog and the water see it at its own distance.
void Lumenveil::LiquidBegin() {
    s_passes.waterBeforeFresh = false;
    Config zoned;
    const Config& cfg = ZonedLook(GlobalConfig().Get(), GlobalZoneClimate().DensityFactor(), zoned);
    GxEffects::Device* device = Enabled() && s_renderedLastFrame && cfg.liquidDepth ? GxEffects::Get() : nullptr;
    if (!device) {
        return;
    }
    GxEffects::ForceDepthWrite(true);
    s_liquidDepthArmed = true;
    uint32_t width = 0;
    uint32_t height = 0;
    D3DVIEWPORT9 viewport = {};
    if (cfg.waterClarity > 0.0f && !engine::CameraInLiquid() && s_passes.waterBefore && device->TargetSize(width, height) && width == s_passes.width
        && height == s_passes.height && engine::OpaqueViewport(viewport)) {
        s_passes.waterBeforeFresh = device->CopyDepth(s_passes.waterBefore) && s_renderer.CaptureWaterBefore(device, viewport);
    }
}

void Lumenveil::LiquidInstance(bool magma) {
    if (s_liquidDepthArmed) {
        GxEffects::ForceDepthWrite(!magma);
    }
}

void Lumenveil::LiquidEnd() {
    GxEffects::ForceDepthWrite(false);
    s_liquidDepthArmed = false;
    GxEffects::Device* device = Enabled() ? GxEffects::Get() : nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    s_passes.occluderFresh = device && s_passes.occluder && device->TargetSize(width, height) && width == s_passes.width
        && height == s_passes.height && device->CopyDepth(s_passes.occluder);
    Config zoned;
    const Config& look = ZonedLook(GlobalConfig().Get(), GlobalZoneClimate().DensityFactor(), zoned);
    if (s_passes.waterBeforeFresh && s_passes.occluderFresh && engine::HasOpaqueState()) {
        FrameInputs in = {};
        if (engine::BuildFrameInputs(in)) {
            if (s_stockFogPushed) {
                in.fogStart = s_savedStockFog.start[engine::kFrameInputsFogGroup];
                in.fogEnd = s_savedStockFog.end[engine::kFrameInputsFogGroup];
            }
            s_renderer.DrawWater(device, s_passes.waterBefore, s_passes.occluder, in, look);
        }
    }
    s_passes.waterBeforeFresh = false;
    // The spell lights need the scene as it stands here, before the glowing effects draw.
    D3DVIEWPORT9 viewport = {};
    if (device && look.effectLight > 0.0f && engine::OpaqueViewport(viewport)) {
        s_renderer.CaptureEffectBaseline(device, viewport);
    }
}

void Lumenveil::WorldDone() {
    if (!Enabled()) {
        engine::ClearOpaqueState();
        s_renderedThisFrame = false;
        return;
    }
    if (!engine::HasOpaqueState()) {
        return;
    }
    ReloadConfigAfterInterval();

    FrameInputs in = {};
    const bool valid = engine::BuildFrameInputs(in);
    engine::ClearOpaqueState();
    if (s_stockFogPushed) {
        in.fogStart = s_savedStockFog.start[engine::kFrameInputsFogGroup];
        in.fogEnd = s_savedStockFog.end[engine::kFrameInputsFogGroup];
    }
    const Config& look = GlobalConfig().Get();
    if (valid) {
        LogInputsOnce(in);
        RegionsOnWorld(in);
        GlobalZoneClimate().Update(in, look);
        SaveSpotIfArmed(in, look);
    }
    Config zoned;
    const Config& cfg = ZonedLook(look, GlobalZoneClimate().DensityFactor(), zoned);
    const char* skip = "invalid frame inputs";
    bool rendered = false;
    if (valid && (!in.inLiquid || cfg.underwater)) {
        rendered = RenderPasses(in, cfg, &skip);
    } else if (valid) {
        skip = "camera under liquid";
    }
    s_renderedThisFrame = rendered;
    if (!rendered && skip != s_lastSkip && s_skipsLogged < kMaxSkipsLogged) {
        ++s_skipsLogged;
        VF_LOG_INFO("fog skipped: %s", skip);
    }
    s_lastSkip = rendered ? "" : skip;
}

void Lumenveil::FrameEnd() {
    RestorePushedStockFog();
    s_renderedLastFrame = s_renderedThisFrame;
    if (s_started) {
        RegionsAtPresent();
    }
}
