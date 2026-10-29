// SPDX-License-Identifier: GPL-3.0-or-later
#include "WoW/Source/WorldClient/HD/HdLights.h"
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Model2/M2Light.h"
#include "Engine/Source/Model2/M2Scene.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CAaSphere.h"

// Pixel constants of one light (4 float4, what HdPointLight in the terrain pixel shader reads):
//   0: xyz = position - camera (world, yards), w = 1 legacy falloff colour * N.L / (0.7 d + 0.03 d^2)
//   1: rgb = colour A, w = attenuation start            (windowed falloff, lightbufferomni permutation 0:
//   2: rgb = colour B, w = 1 / (attenuation end - start)  (1 - saturate((d - start) / (end - start)))^2 * N.L,
//   3: x, y = colour blend t = saturate(d * x + y)         colour = lerp(B, A, smoothstep(t)); not used by M2 lights)
//      z = radius where the light ends, w = 1 / fade width (our cut: Forever's legacy falloff never reaches 0, its
//      light volume ends it; its size is not known yet)

namespace {
    struct FrameLight {
        C3Vector pos;      // world
        C3Vector color;    // CM2Light::m_dirColor (diffuse colour * intensity * model alpha), as 3.3.5 uses it
        float strength;    // brightest channel
        float radius;      // where it ends (hdLightsRadius, or from its colour)
        float fade;        // fade width at the end
    };

    const float kGatherDistance = 300.0f; // lights farther than this from the camera are left out
    const float kEndLight = 0.02f;        // automatic radius: where colour / (0.7 d + 0.03 d^2) falls to this
    const float kMinRadius = 2.0f;
    const float kMaxRadius = 32.0f;
    const uint32_t kNoFrame = 0xFFFFFFFFu;

    CVar* s_mode = nullptr;
    CVar* s_radius = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_info = nullptr;

    std::vector<FrameLight> s_frame;       // this frame's lights
    // HD: the last animated state of each model light. A lamp's model is animated only while it is drawn, and
    // SelectLights hides (unlinks) a light its model did not animate this frame: a lamp just off screen stopped
    // lighting the ground in view, so the ground's colour changed with the camera angle. Kept until the model
    // leaves the scene (HD::LightsForget).
    struct KnownLight {
        C3Vector pos;
        C3Vector color;
        uint32_t frame;    // the frame it was last seen in the grid
    };
    std::unordered_map<const CM2Light*, KnownLight> s_known;
    const CM2Scene* s_knownScene = nullptr;
    const CM2Scene* s_frameScene = nullptr;
    uint32_t s_frameStamp = kNoFrame;
    int32_t s_otherFalloff = 0;            // this frame's lights whose attenuation is not CM2Light's 0 / 0.7 / 0.03
    int32_t s_mostPerDraw = 0;             // most lights one draw took since the info was last shown
    ULONGLONG s_infoTime = 0;

    void RegisterCVars() {
        static bool registered = false;
        if (registered) return;
        registered = true;
        s_mode = CVar::Register("hdLights", "HD: 1 = lamps and fires light the ground per pixel (WoW Forever), 2 = light path with no light (must equal 0), 3 = test view (lights only), 0 = 3.3.5 per-vertex lights", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_radius = CVar::Register("hdLightsRadius", "HD: yards where a lamp's light ends, 0 = from its colour", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
        s_strength = CVar::Register("hdLightsStrength", "HD: lamp light strength (ours: scales every light's colour, and so its reach)", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdLightsInfo", "HD: hdLights status (read only)", 0, "no light yet", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    }

    float Radius(float strength) {
        float fixed = s_radius ? s_radius->m_floatValue : 0.0f;
        if (fixed > 0.0f) return fixed;
        // 0.03 r^2 + 0.7 r = strength / kEndLight
        float r = (sqrtf(0.49f + 0.12f * strength / kEndLight) - 0.7f) / 0.06f;
        return r < kMinRadius ? kMinRadius : (r > kMaxRadius ? kMaxRadius : r);
    }

    void AddFrameLight(const C3Vector& pos, const C3Vector& base, const C3Vector& camera) {
        float gain = s_strength ? s_strength->m_floatValue : 1.0f;
        C3Vector c(base.x * gain, base.y * gain, base.z * gain);
        float dx = pos.x - camera.x;
        float dy = pos.y - camera.y;
        float dz = pos.z - camera.z;
        if (dx * dx + dy * dy + dz * dz > kGatherDistance * kGatherDistance) return;
        float strength = c.x > c.y ? c.x : c.y;
        if (c.z > strength) strength = c.z;
        if (!(strength > 0.0f)) return;
        FrameLight frameLight;
        frameLight.pos = pos;
        frameLight.color = c;
        frameLight.strength = strength;
        frameLight.radius = Radius(strength);
        frameLight.fade = frameLight.radius * 0.25f;
        s_frame.push_back(frameLight);
    }

    // The lights CM2Scene::SelectLights would give anything this frame: point lights in the light grid, visible,
    // and animated this frame (or not owned by a model). Unlike SelectLights it never hides a stale light.
    // HD: plus the lamps whose model was not animated this frame, at their last animated state (s_known).
    void GatherFrame(const CM2Scene* scene, const C3Vector& camera) {
        s_frame.clear();
        s_otherFalloff = 0;
        if (scene != s_knownScene) {
            s_known.clear();
            s_knownScene = scene;
        }
        uint32_t frame = scene->m_frameStamp;
        if (scene->m_lightGrid) {
            for (uint32_t cell = 0; cell < 64 * 64; cell++) {
                for (const CM2Light* light = scene->m_lightGrid[cell]; light; light = light->m_lightNext) {
                    if (light->m_scene && light->m_stamp != frame) continue; // stale: from s_known below
                    if (!light->m_visible || light->m_type != M2LIGHT_POINT) continue;
                    if (light->m_scene) {
                        KnownLight& known = s_known[light];
                        known.pos = light->m_pos;
                        known.color = light->m_dirColor;
                        known.frame = frame;
                    }
                    if (light->m_constantAttenuation != 0.0f || light->m_linearAttenuation != 0.7f || light->m_quadraticAttenuation != 0.03f) s_otherFalloff++;
                    AddFrameLight(light->m_pos, light->m_dirColor, camera);
                }
            }
        }
        for (std::unordered_map<const CM2Light*, KnownLight>::iterator it = s_known.begin(); it != s_known.end();) {
            if (it->second.frame == frame) { ++it; continue; }
            // animated this frame but not in the grid: its model hid it (switched off)
            if (it->first->m_stamp == frame) { it = s_known.erase(it); continue; }
            AddFrameLight(it->second.pos, it->second.color, camera);
            ++it;
        }
    }

    void ShowInfo(const C3Vector& camera) {
        ULONGLONG now = GetTickCount64();
        if (!s_info || (s_infoTime && now - s_infoTime < 500)) return;
        s_infoTime = now;
        char text[300];
        int32_t nearest = -1;
        float best = 0.0f;
        for (size_t i = 0; i < s_frame.size(); i++) {
            float dx = s_frame[i].pos.x - camera.x;
            float dy = s_frame[i].pos.y - camera.y;
            float dz = s_frame[i].pos.z - camera.z;
            float d = dx * dx + dy * dy + dz * dz;
            if (nearest < 0 || d < best) { nearest = static_cast<int32_t>(i); best = d; }
        }
        int32_t n = snprintf(text, sizeof(text), "hdLights %d: %u lights, up to %d per draw, %d other falloff", HD::LightsMode(), static_cast<uint32_t>(s_frame.size()), s_mostPerDraw, s_otherFalloff);
        if (nearest >= 0 && n > 0 && n < static_cast<int32_t>(sizeof(text))) {
            const FrameLight& l = s_frame[nearest];
            snprintf(text + n, sizeof(text) - n, "; nearest %.1f yd at (%.2f, %.2f, %.2f) colour (%.3f, %.3f, %.3f) radius %.1f", sqrtf(best), l.pos.x, l.pos.y, l.pos.z, l.color.x, l.color.y, l.color.z, l.radius);
        }
        s_info->Set(text, true, false, false, false);
        s_mostPerDraw = 0;
    }
}

int32_t HD::LightsMode() {
    RegisterCVars();
    int32_t mode = s_mode ? s_mode->m_intValue : 1;
    return mode < 0 || mode > 3 ? 0 : mode;
}

int32_t HD::LightsForSphere(CM2Scene* scene, const C3Vector& camera, const CAaSphere& sphere, float* out, int32_t limit) {
    if (limit > kLightsBuilding) limit = kLightsBuilding;
    RegisterCVars();
    if (!scene) return 0;
    if (scene != s_frameScene || scene->m_frameStamp != s_frameStamp) {
        s_frameScene = scene;
        s_frameStamp = scene->m_frameStamp;
        GatherFrame(scene, camera);
        ShowInfo(camera);
    }

    // the strongest lights at the sphere's nearest point (brightest first)
    int32_t picked[kLightsBuilding];
    float score[kLightsBuilding];
    int32_t count = 0;
    for (size_t i = 0; i < s_frame.size(); i++) {
        const FrameLight& l = s_frame[i];
        float dx = l.pos.x - sphere.c.x;
        float dy = l.pos.y - sphere.c.y;
        float dz = l.pos.z - sphere.c.z;
        float reach = l.radius + sphere.r;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq >= reach * reach) continue;
        float d = sqrtf(distSq) - sphere.r;
        if (d < 1.0f) d = 1.0f;
        float s = l.strength / (0.7f * d + 0.03f * d * d);
        if (count == limit && s <= score[limit - 1]) continue;
        int32_t j = count < limit ? count++ : limit - 1;
        while (j > 0 && score[j - 1] < s) { score[j] = score[j - 1]; picked[j] = picked[j - 1]; j--; }
        score[j] = s;
        picked[j] = static_cast<int32_t>(i);
    }

    for (int32_t k = 0; k < count; k++) {
        const FrameLight& l = s_frame[picked[k]];
        float* c = out + 16 * k;
        c[0] = l.pos.x - camera.x;  c[1] = l.pos.y - camera.y;  c[2] = l.pos.z - camera.z;  c[3] = 1.0f;
        c[4] = l.color.x;           c[5] = l.color.y;           c[6] = l.color.z;           c[7] = 0.0f;
        c[8] = l.color.x;           c[9] = l.color.y;           c[10] = l.color.z;          c[11] = 0.0f;
        c[12] = 0.0f;               c[13] = 1.0f;               c[14] = l.radius;           c[15] = 1.0f / l.fade;
    }
    if (count > s_mostPerDraw) s_mostPerDraw = count;
    return count;
}

void HD::LightsForget(const CM2Light* light) {
    s_known.erase(light);
}
