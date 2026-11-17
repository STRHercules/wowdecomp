#include "WoW/Source/WorldClient/Clouds/VolumetricClouds.h"
#include "WoW/Source/WorldClient/Clouds/Noise.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/Gx.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "Engine/Source/Os/TimeManager.h"
#include "Engine/Source/Tempest/CRect.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/MapWeather.h"
#include "WoW/Source/WorldClient/World.h"
#include <chrono>
#include <cstring>
#include <future>

// Manastorm's cloud march and noise, adapted to GxEffects; the existing atmosphere stays in use.
namespace {
#include "WoW/Source/WorldClient/Clouds/Spirv.inc"
constexpr uint32_t kPanoramaWidth = 1536;
constexpr uint32_t kPanoramaHeight = 768;
constexpr float kSunIlluminance = 30.0f;
constexpr float kMoonIlluminance = 0.5f;
constexpr float kYardsToKm = 0.0009144f;
constexpr float kPi = 3.14159265f;
constexpr float kWeatherCoverSeconds = 40.0f;
using CloudNoise::Clamp01;

float SmoothStep(float a, float b, float x) {
    float t = Clamp01((x - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

struct Constants {
    float sun[4];
    float cloudLight[4];
    float view[4];
    float camera[4];
    float clouds[4];
    float wind[4];
    float bands[6][4];
    float flash[4];
    float stars[4];
    float cloudTint[4];
    float moon[4];
    float domeWorld[16];
    float domeView[16];
    float domeProj[16];
    float ambient[4];
    float cloudShape[4];
    float cloudZone[3][4];
};

static_assert(sizeof(Constants) == 33 * 16, "matches SkyConstants in sky_common.hlsli");
Constants s_constants = {};
CVar* s_enabled = nullptr;
CVar* s_info = nullptr;
uint32_t s_startMs = 0;
uint32_t s_frame = 0;
double s_lastCoverSeconds = 0;
double s_windKm = 0;
float s_weatherCover = 0;
std::future<std::vector<std::vector<uint8_t>>> s_noise[2];

struct State {
    GxEffects::Device* device = nullptr;
    GxEffects::Program* panoramaProgram = nullptr;
    GxEffects::Program* scatterProgram = nullptr;
    GxEffects::Program* drawProgram = nullptr;
    GxEffects::Texture* shape = nullptr;
    GxEffects::Texture* detail = nullptr;
    GxEffects::Texture* scatter = nullptr;
    GxEffects::Texture* panorama = nullptr;
    GxEffects::Texture* depth = nullptr;
    uint32_t width = 0, height = 0;
    uint32_t lastMs = 0;
    C3Vector lastCamera = C3Vector(0, 0, 0);
    C3Vector lastSun = C3Vector(0, 0, -1);
    bool valid = false;
    bool failed = false;
    bool baked = false;
} s_state;

void Info(const char* text) {
    if (s_info && std::strcmp(s_info->m_stringValue.m_str, text))
        s_info->Set(text, true, false, false, false);
}

const float (*Rows())[4] {
    return reinterpret_cast<const float (*)[4]>(&s_constants);
}

C3Vector Toward(const C3Vector& position, const C3Vector& from) {
    C3Vector d(position.x - from.x, position.y - from.y, position.z - from.z);
    float length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    return length > 1e-3f ? C3Vector(d.x / length, d.y / length, d.z / length) : C3Vector(0.0f, 0.0f, -1.0f);
}

void SetRow(float* row, float x, float y, float z, float w) {
    row[0] = x;
    row[1] = y;
    row[2] = z;
    row[3] = w;
}

// How far the weather closes the sky, 0..1: by its kind (rain and snow fall from overcast; dust is not cloud, so a
// sandstorm only thickens it a little) and by its strength, light rain already greying most of it.
float WeatherCloudiness() {
    const Weather* weather = World::s_weather;

    if (!weather) {
        return 0.0f;
    }

    static const float kKind[4] = { 0.0f, 1.0f, 0.9f, 0.3f };  // none, rain, snow, sand (Weather::GetType)
    const uint32_t type = weather->GetType();
    return type < 4 ? kKind[type] * Clamp01(weather->m_intensity * 2.5f) : 0.0f;
}

// The frame's sky state into the constants (all but the dome's transforms, which are the draw's).
void FillFrameConstants() {
    DayNight::DNInfo& info = DayNight::g_dnInfo;
    const C3Vector& camera = info.m_cameraPos;
    const C3Vector sun = Toward(DayNight::g_planets.m_sun.m_position, camera);
    const C3Vector moon = Toward(DayNight::g_planets.m_moon.m_position, camera);
    const double seconds = static_cast<double>(OsGetAsyncTimeMs() - s_startMs) / 1000.0;

    // The clouds take the sun until it is well under the horizon (its light is gone by then), then the moon.
    const bool byDay = sun.z > -0.08f;
    const float night = SmoothStep(-0.03f, -0.2f, sun.z);
    // Night is brought up, as the eye adapts, once the sun is at the horizon: while it is up the sky is bright enough, and
    // boosting it then pushed the whole low sky into the tonemap's shoulder, flattening the sun side against the far side.
    // Fixed sky brightness, no user setting (the Brightness slider was removed).
    constexpr float kSkyExposure = 0.25f;
    const float exposure = kSkyExposure * (1.0f + 3.0f * SmoothStep(0.0f, -0.12f, sun.z));

    // Keep the zone's cloud density; weather closes it toward overcast gradually.
    const float base = Clamp01(0.15f + 0.75f * Clamp01(info.m_bands.m_cloudDensity) + 0.5f * Clamp01(info.m_weatherIntensity));
    const float gathered = WeatherCloudiness();
    const float dt = static_cast<float>(std::max(seconds - s_lastCoverSeconds, 0.0));
    s_lastCoverSeconds = seconds;
    s_weatherCover += (gathered - s_weatherCover) * (1.0f - std::exp(-dt / kWeatherCoverSeconds));
    const float cover = base + (1.0f - base) * s_weatherCover;

    // The donor's default 24 m/s wind, capped after pauses; 720 km wraps all the noise periods.
    s_windKm = std::fmod(s_windKm + std::min(static_cast<double>(dt), 1.0) * 0.024, 720.0);
    const double wind = s_windKm;

    Constants& c = s_constants;
    SetRow(c.sun, sun.x, sun.y, sun.z, kSunIlluminance);
    SetRow(c.cloudLight, byDay ? sun.x : moon.x, byDay ? sun.y : moon.y, byDay ? sun.z : moon.z, byDay ? kSunIlluminance : kMoonIlluminance);
    SetRow(c.view, 0.0f, exposure, 1.0f, night);
    SetRow(c.camera, camera.x * kYardsToKm, camera.y * kYardsToKm, 0.25f + std::max(camera.z, 0.0f) * kYardsToKm, static_cast<float>(s_frame % 1024));
    SetRow(c.clouds, cover, 1.0f, static_cast<float>(std::fmod(seconds, 3600.0)), 0.0f);
    SetRow(c.wind, static_cast<float>(wind * 0.95), static_cast<float>(wind * 0.31), static_cast<float>(kPanoramaWidth), static_cast<float>(kPanoramaHeight));

    // The bands without the lightning flash: the panorama refreshes a sixteenth a frame, so a flash baked into it would
    // dither in over 16 frames. The dome and the fog apply cFlash where they read the panorama instead.
    for (int32_t i = 0; i < 6; i++) {
        const CImVector& colour = info.m_bands.m_skyColors[i];
        SetRow(c.bands[i], colour.r / 255.0f, colour.g / 255.0f, colour.b / 255.0f, 0.0f);
    }

    SetRow(c.flash, info.m_flashColor.r / 255.0f, info.m_flashColor.g / 255.0f, info.m_flashColor.b / 255.0f, info.m_flashBlend / 255.0f * 0.6f);

    const float turn = info.m_dayProgression * 2.0f * kPi;
    SetRow(c.stars, std::cos(turn), std::sin(turn), 1.0f, static_cast<float>(std::fmod(seconds, 3600.0)));

    // The clouds' light colour: white by day; by night the moonlight's, the world light's diffuse brought to full
    // brightness, saturated half again so moonlit clouds read blue-silver instead of grey.
    float tint[3] = { 1.0f, 1.0f, 1.0f };

    if (!byDay) {
        const CImVector& moonlight = info.m_light0.m_diffuse;
        const float r = moonlight.r / 255.0f;
        const float g = moonlight.g / 255.0f;
        const float b = moonlight.b / 255.0f;
        const float peak = std::max(r, std::max(g, b));
        float light[3] = { 0.6f, 0.7f, 1.0f };

        if (peak > 0.01f) {
            light[0] = r / peak;
            light[1] = g / peak;
            light[2] = b / peak;
        }

        const float grey = light[0] * 0.2126f + light[1] * 0.7152f + light[2] * 0.0722f;

        for (int32_t i = 0; i < 3; i++) {
            tint[i] = Clamp01(grey + (light[i] - grey) * 1.5f);
        }
    }

    SetRow(c.cloudTint, tint[0], tint[1], tint[2], 35.0f);

    // The moon for its glow (sky_dome_ps.hlsl): where it is and how wide its disc looks. The disc quad's half side is
    // 0.5 * m_scale (DNPlanet::GenGeometry), seen from 12 yards; the visible disc in moon.blp fills ~80% of it.
    const float moonRadius = std::atan(DayNight::g_planets.m_moon.m_scale * 0.5f * 0.8f / 12.0f);
    SetRow(c.moon, moon.x, moon.y, moon.z, moonRadius);

    // The world's ambient light (what the land in shade is lit by), for the clouds' darkest tone by night.
    const CImVector& ambient = info.m_light1.m_ambient;
    SetRow(c.ambient, ambient.r / 255.0f, ambient.g / 255.0f, ambient.b / 255.0f, 0.0f);

    SetRow(c.cloudShape, 1.0f, 0.0f, 0.0f, 0.0f);

    // The zone's own cloud colours as the stock clouds take them (DayNight.cpp SetupBumpMap): [2] the body, lit side, [3] the
    // base, shaded side. The lightning flash is left to the cloud shader's own blend (cFlash).
    const CImVector& cloudLit = info.m_bands.m_sunCloudColors[2];
    const CImVector& cloudShaded = info.m_bands.m_sunCloudColors[3];
    SetRow(c.cloudZone[0], cloudLit.r / 255.0f, cloudLit.g / 255.0f, cloudLit.b / 255.0f, 0.0f);
    SetRow(c.cloudZone[1], cloudShaded.r / 255.0f, cloudShaded.g / 255.0f, cloudShaded.b / 255.0f, 0.0f);
    const CImVector& cloudSun = info.m_bands.m_sunCloudColors[1];
    SetRow(c.cloudZone[2], cloudSun.r / 255.0f, cloudSun.g / 255.0f, cloudSun.b / 255.0f, 0.0f);
}

GxEffects::Texture* Volume(GxEffects::Device* device, int size, std::vector<std::vector<uint8_t>> levels) {
    const void* data[7] = {};
    for (size_t level = 0; level < levels.size(); ++level) {
        // GxEffects' byte format is BGRA; the donor generates RGBA.
        for (size_t i = 0; i < levels[level].size(); i += 4)
            std::swap(levels[level][i], levels[level][i + 2]);
        data[level] = levels[level].data();
    }
    return device->CreateVolume(size, size, size, static_cast<uint32_t>(levels.size()), GxEffects::Format::BGRA8, data);
}

bool Ready(GxEffects::Device* device, uint32_t width, uint32_t height) {
    State& st = s_state;
    if (st.device != device) {
        st = State();
        st.device = device;
    }
    if (st.failed) return false;
    if (!st.panoramaProgram) {
        if (!device->CanStore(GxEffects::Format::RGBA32F)) {
            st.failed = true;
            Info("unsupported cloud storage format; existing clouds retained");
            return false;
        }
        st.panoramaProgram = device->CreateProgram(kCloudPanorama, sizeof kCloudPanorama, "Cloud panorama");
        st.scatterProgram = device->CreateProgram(kCloudScatter, sizeof kCloudScatter, "Cloud atmosphere lighting");
        st.drawProgram = device->CreateProgram(kCloudDraw, sizeof kCloudDraw, "Volumetric clouds");
        st.failed = !st.panoramaProgram || !st.scatterProgram || !st.drawProgram;
        if (st.failed) { Info("cloud shaders failed; existing clouds retained"); return false; }
    }
    if (!st.shape || !st.detail) {
        try {
            if (!s_noise[0].valid()) {
                s_noise[0] = std::async(std::launch::async, CloudNoise::VolumeLevels, 64, true);
                s_noise[1] = std::async(std::launch::async, CloudNoise::VolumeLevels, 32, false);
            }
            for (auto& noise : s_noise) {
                if (noise.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    Info("preparing cloud noise; existing clouds retained");
                    return false;
                }
            }
            st.shape = Volume(device, 64, s_noise[0].get());
            st.detail = Volume(device, 32, s_noise[1].get());
        } catch (...) {
            st.failed = true;
            Info("cloud noise failed; existing clouds retained");
            return false;
        }
        st.panorama = device->CreateStorage(kPanoramaWidth, kPanoramaHeight, GxEffects::Format::RGBA32F);
        st.scatter = device->CreateTexture(32, 32, GxEffects::Format::RGBA32F, true);
        st.failed = !st.shape || !st.detail || !st.panorama || !st.scatter;
        if (st.failed) { Info("cloud resources failed; existing clouds retained"); return false; }
    }
    if (width != st.width || height != st.height || !st.depth) {
        device->Destroy(st.depth);
        st.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
        st.width = width;
        st.height = height;
    }
    if (!st.depth) { Info("cloud depth copy unavailable; existing clouds retained"); return false; }
    if (!st.baked) {
        GxEffects::Pass bake;
        bake.program = st.scatterProgram;
        bake.target = st.scatter;
        st.baked = device->Draw(bake);
    }
    return st.baked;
}
}  // namespace

void VolumetricClouds::RegisterCVars() {
    s_enabled = OptionCVars::Register("skyClouds", "Volumetric clouds (Vulkan), 0 existing clouds / 1 on", "0");
    s_info = CVar::Register("skyCloudsInfo", "Volumetric cloud status", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_startMs = OsGetAsyncTimeMs();
}

bool VolumetricClouds::Render(const CRect& rect) {
    State& st = s_state;
    if (!OptionCVars::Bool(s_enabled, false)) {
        st.valid = false;
        Info("off");
        return false;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0, height = 0;
    if (!device || !g_theGxDevicePtr || !device->TargetSize(width, height)) {
        Info("Vulkan required; existing clouds retained");
        return false;
    }
    const auto& p = g_theGxDevicePtr->m_projection;
    if (!std::isfinite(p.a0) || !std::isfinite(p.b1) || p.a0 == 0 || p.b1 == 0 || std::fabs(p.c3 - 1) > 0.001f) return false;
    if (!Ready(device, width, height)) return false;

    const uint32_t now = OsGetAsyncTimeMs();
    const C3Vector& camera = DayNight::g_dnInfo.m_cameraPos;
    const bool cameraChanged = (camera - st.lastCamera).SquaredMag() > 300.0f * 300.0f;
    const C3Vector sun = Toward(DayNight::g_planets.m_sun.m_position, camera);
    const bool sunChanged = sun.x * st.lastSun.x + sun.y * st.lastSun.y + sun.z * st.lastSun.z < 0.9995f;
    if (!st.valid || now != st.lastMs || cameraChanged || sunChanged) {
        FillFrameConstants();
        const bool whole = !st.valid || cameraChanged || sunChanged || now - st.lastMs > 250;
        s_constants.clouds[3] = whole ? -1.0f : static_cast<float>(s_frame % 16);
        GxEffects::Compute compute;
        compute.program = st.panoramaProgram;
        compute.groupsX = kPanoramaWidth / (whole ? 8 : 32);
        compute.groupsY = kPanoramaHeight / (whole ? 8 : 32);
        compute.textures[0] = { st.shape, nullptr, true, true, true };
        compute.textures[1] = { st.detail, nullptr, true, true, true };
        compute.textures[2] = { st.scatter };
        compute.images[0] = st.panorama;
        compute.constants = Rows();
        compute.constantRows = sizeof(Constants) / 16;
        if (!device->Dispatch(compute)) {
            st.valid = false;
            Info("cloud dispatch failed; existing clouds retained");
            return false;
        }
        st.lastMs = now;
        st.lastCamera = camera;
        st.lastSun = sun;
        st.valid = true;
        ++s_frame;
    }

    C44Matrix view;
    GxXformView(view);
    SetRow(s_constants.domeWorld, view.a0, view.b0, view.c0, 0);
    SetRow(s_constants.domeWorld + 4, view.a1, view.b1, view.c1, 0);
    SetRow(s_constants.domeWorld + 8, view.a2, view.b2, view.c2, 0);
    const auto& viewport = g_theGxDevicePtr->m_viewport;
    const bool target = g_theGxDevicePtr->m_textureTarget[0].m_texture || g_theGxDevicePtr->m_textureTarget[1].m_texture;
    const auto pixel = [](float v, uint32_t size) { return std::clamp(std::floor(v * size + 0.5f), 0.0f, static_cast<float>(size)); };
    const float x0 = pixel(viewport.m_x.m_l, width), x1 = pixel(viewport.m_x.m_h, width);
    const float y0 = pixel(target ? viewport.m_y.m_l : 1.0f - viewport.m_y.m_h, height);
    const float y1 = pixel(target ? viewport.m_y.m_h : 1.0f - viewport.m_y.m_l, height);
    if (x1 <= x0 || y1 <= y0) return false;
    SetRow(s_constants.domeView, x0, y0, x1 - x0, y1 - y0);
    SetRow(s_constants.domeView + 4, 2.0f / p.a0, -2.0f / p.b1, (-1.0f - p.c0) / p.a0, (1.0f - p.c1) / p.b1);
    SetRow(s_constants.domeView + 8, static_cast<float>(width), static_cast<float>(height), viewport.m_z.m_l, 0);
    if (!device->CopyDepth(st.depth)) { Info("cloud depth copy failed; existing clouds retained"); return false; }

    GxEffects::Pass draw;
    draw.x = static_cast<int32_t>(pixel(rect.minX, width));
    draw.y = static_cast<int32_t>(pixel(target ? rect.minY : 1.0f - rect.maxY, height));
    draw.width = static_cast<uint32_t>(pixel(rect.maxX, width) - draw.x);
    draw.height = static_cast<uint32_t>(pixel(target ? rect.maxY : 1.0f - rect.minY, height) - draw.y);
    if (!draw.width || !draw.height) return false;
    draw.program = st.drawProgram;
    draw.textures[3] = { st.panorama };
    draw.textures[4] = { st.depth };
    draw.constants = Rows();
    draw.constantRows = sizeof(Constants) / 16;
    draw.blend = GxEffects::Blend::Alpha;
    const bool ok = device->Draw(draw);
    Info(ok ? "on (Vulkan)" : "cloud draw failed; existing clouds retained");
    return ok;
}

