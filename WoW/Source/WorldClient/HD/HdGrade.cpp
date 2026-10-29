// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdVulkanGrade.cpp on GxEffects. The donor graded the back buffer in place; here a compute grades
// the shared frame (HdFrame.h), which bloom has already written when it is on. LUT choice, the shader's formulas and
// the cvars are the donor's.
#include "WoW/Source/WorldClient/HD/HdGrade.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "WoW/Source/DB/Rec/LightRec.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdForever.h"
#include "WoW/Source/WorldClient/HD/HdFrame.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    constexpr uint32_t kLutWidth = 1024;
    constexpr uint32_t kLutHeight = 32;
    constexpr size_t kLutBytes = size_t(kLutWidth) * kLutHeight * 4;

    // Up to 16 LUT IDs with their weights; the strongest two are graded between.
    struct LutWeights {
        int32_t id[16] = {};
        float weight[16] = {};
        int32_t count = 0;

        void Add(int32_t lut, float w) {
            if (w <= 0) return;
            for (int32_t i = 0; i < count; ++i) {
                if (id[i] == lut) {
                    weight[i] += w;
                    return;
                }
            }
            if (count < 16) {
                id[count] = lut;
                weight[count] = w;
                ++count;
            }
        }

        void Scale(float f) {
            for (int32_t i = 0; i < count; ++i) weight[i] *= f;
        }
    };

    struct Selection {
        int32_t a = 0;
        int32_t b = 0;
        float blend = 0;
        bool hasGrade = false;
    };

    struct State {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* grade = nullptr;
        std::map<int32_t, GxEffects::Texture*> luts;
        std::set<int32_t> missing;
        bool failed = false;
    };

    State s_state;
    CVar* s_enabled = nullptr;
    CVar* s_strength = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_forced = nullptr;
    CVar* s_info = nullptr;
    char s_lastInfo[240] = {};

    void Info(const char* value) {
        if (s_info && std::strcmp(s_lastInfo, value)) {
            std::snprintf(s_lastInfo, sizeof s_lastInfo, "%s", value);
            s_info->Set(s_lastInfo, true, false, false, false);
        }
    }

    float Clamp(float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; }

    Selection Select(const LutWeights& weights) {
        int32_t first = -1;
        int32_t second = -1;
        for (int32_t i = 0; i < weights.count; ++i) {
            if (first < 0 || weights.weight[i] > weights.weight[first]) {
                second = first;
                first = i;
            } else if (second < 0 || weights.weight[i] > weights.weight[second]) {
                second = i;
            }
        }
        Selection out;
        out.a = first >= 0 ? weights.id[first] : 0;
        out.b = second >= 0 ? weights.id[second] : out.a;
        float a = first >= 0 ? weights.weight[first] : 1;
        float b = second >= 0 ? weights.weight[second] : 0;
        out.blend = a + b > 0 ? b / (a + b) : 0;
        out.hasGrade = first >= 0 && (out.a != 0 || out.b != 0);
        return out;
    }

    // LUT 0: every colour to itself, as BGRA texels (blue slice along x, red within a slice, green down).
    void Identity(std::vector<uint8_t>& pixels) {
        for (uint32_t y = 0; y < kLutHeight; ++y) {
            for (uint32_t x = 0; x < kLutWidth; ++x) {
                uint8_t* t = &pixels[(size_t(y) * kLutWidth + x) * 4];
                t[0] = uint8_t(((x / 32) * 255 + 15) / 31);
                t[1] = uint8_t((y * 255 + 15) / 31);
                t[2] = uint8_t(((x % 32) * 255 + 15) / 31);
                t[3] = 255;
            }
        }
    }

    // An uncompressed BLP2 (encoding 3: BGRA) of exactly 1024x32; its first mip only.
    bool DecodeLut(const uint8_t* data, size_t size, std::vector<uint8_t>& pixels) {
        if (!data || size < 148 || std::memcmp(data, "BLP2", 4) || data[8] != 3) return false;
        uint32_t w = 0;
        uint32_t h = 0;
        uint32_t offset = 0;
        std::memcpy(&w, data + 12, 4);
        std::memcpy(&h, data + 16, 4);
        std::memcpy(&offset, data + 20, 4);
        if (w != kLutWidth || h != kLutHeight || offset < 148 || size_t(offset) > size || kLutBytes > size - size_t(offset)) return false;
        std::memcpy(pixels.data(), data + offset, kLutBytes);
        return true;
    }

    // One light's grading (clear weather blended to storm), as weights. No Forever grading = identity (0).
    void AddLight(LutWeights& luts, const LightRec* light, int32_t time, float storm, float weight) {
        for (int32_t pass = 0; pass < 2; pass++) {
            float w = weight * (pass == 0 ? 1.0f - storm : storm);
            if (w <= 0.0f) continue;
            int32_t a;
            int32_t b;
            float t;
            if (HD::ForeverGradingAt(light, pass == 0 ? 0 : 2, time, &a, &b, &t)) {
                luts.Add(a, w * (1.0f - t));
                luts.Add(b, w * t);
            } else {
                luts.Add(0, w);
            }
        }
    }

    // The zone light, then the nearby area lights blended in by distance like DayNight::BlendAreaLight, farthest
    // first so the nearest wins.
    void CurrentLuts(LutWeights& luts) {
        const DayNight::DNInfo& info = DayNight::g_dnInfo;
        const TSGrowableArray<const LightRec*>& lights = DayNight::g_areaLights;
        if (!lights.m_count) return;
        int32_t time = static_cast<int32_t>(info.m_dayProgression * 2880.0f);
        float storm = Clamp(info.m_stormBlend, 0.0f, 1.0f);
        AddLight(luts, lights.m_data[0], time, storm, 1.0f);

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
            if (weight <= 0.0f) continue;
            nearLights[count].light = light;
            nearLights[count].distSq = distSq;
            nearLights[count].weight = weight;
            count++;
        }
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
            luts.Scale(1.0f - nearLights[i].weight);
            AddLight(luts, nearLights[i].light, time, storm, nearLights[i].weight);
        }
    }

    // The LUT's texture, loaded the first time it is asked for. Null when the file is missing or bad (remembered).
    GxEffects::Texture* Lut(int32_t id) {
        State& st = s_state;
        auto found = st.luts.find(id);
        if (found != st.luts.end()) return found->second;
        if (id < 0 || st.missing.count(id)) return nullptr;
        std::vector<uint8_t> pixels(kLutBytes);
        if (id == 0) {
            Identity(pixels);
        } else {
            char paths[2][96];
            std::snprintf(paths[0], sizeof paths[0], "forever_data\\grading\\%d.blp", id);
            std::snprintf(paths[1], sizeof paths[1], "..\\..\\forever_data\\grading\\%d.blp", id);
            FILE* file = nullptr;
            for (int i = 0; i < 2 && !file; ++i) file = std::fopen(paths[i], "rb");
            if (!file) {
                st.missing.insert(id);
                return nullptr;
            }
            std::vector<uint8_t> data(2 * 1024 * 1024);
            size_t got = std::fread(data.data(), 1, data.size(), file);
            bool bad = std::ferror(file) != 0;
            std::fclose(file);
            if (bad || !DecodeLut(data.data(), got, pixels)) {
                st.missing.insert(id);
                return nullptr;
            }
        }
        const void* level = pixels.data();
        GxEffects::Texture* texture = st.device->CreateFilled(kLutWidth, kLutHeight, 1, GxEffects::Format::BGRA8, &level);
        if (!texture) {
            st.missing.insert(id);
            return nullptr;
        }
        st.luts[id] = texture;
        return texture;
    }

    // The program for this device. False (logged in hdGradeInfo) when it cannot be made, for good on this device.
    bool Ready(GxEffects::Device* device) {
        State& st = s_state;
        if (device != st.device) {
            // A new device: what the old one made is gone with it.
            st = State();
            st.device = device;
        }
        if (st.failed) return false;
        if (!st.grade) {
            st.grade = device->CreateProgram(kGradeComp, sizeof kGradeComp, "HD Grade");
            if (!st.grade || !device->CanStore(GxEffects::Format::RGBA16F)) {
                st.failed = true;
                Info("grade: no compute on this device (programs or RGBA16F storage)");
                return false;
            }
        }
        return true;
    }
}

void HD::Grade::RegisterCVars() {
    if (s_enabled) return;
    HD::RegisterForeverCVars();
    s_enabled = OptionCVars::Register("hdGrade", "HD: colour grading, 0 off / 1 on", "1");
    s_strength = OptionCVars::Register("hdGradeStrength", "HD: grading strength 0..2", "1");
    s_debug = CVar::Register("hdGradeDebug", "HD: left original / right graded", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_forced = OptionCVars::Register("hdGradeLut", "HD: positive LUT ID overrides zone choice", "0");
    s_info = CVar::Register("hdGradeInfo", "HD: grading status", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::Grade::World() {
    if (Visuals::Current() != Visuals::AzerothRemastered || !OptionCVars::Bool(s_enabled, false)) {
        Info("off");
        return;
    }
    GxEffects::Device* device = GxEffects::Get();
    uint32_t width = 0;
    uint32_t height = 0;
    if (!device || !device->TargetSize(width, height)) {
        Info("grade: no GxEffects device (Vulkan only)");
        return;
    }
    if (!Ready(device)) return;

    LutWeights weights;
    if (s_forced && s_forced->m_intValue > 0) {
        weights.Add(s_forced->m_intValue, 1);
    } else {
        CurrentLuts(weights);
    }
    const Selection selected = Select(weights);
    float params[4] = {};
    params[0] = Clamp(selected.blend, 0.0f, 1.0f);
    params[1] = Clamp(OptionCVars::Float(s_strength, 0.0f, 2.0f, 1.0f), 0.0f, 2.0f);
    params[2] = OptionCVars::Bool(s_debug, false) ? 1.0f : 0.0f;
    if (!selected.hasGrade && params[2] <= 0.5f) {
        Info("grade: no grading for this light");
        return;
    }
    GxEffects::Texture* lutA = Lut(selected.a);
    GxEffects::Texture* lutB = Lut(selected.b);
    if (!lutA || !lutB) {
        Info("grade: LUT missing, invalid or not made; image unchanged");
        return;
    }

    GxEffects::Compute compute;
    compute.program = s_state.grade;
    compute.groupsX = (width + 7) / 8;
    compute.groupsY = (height + 7) / 8;
    compute.textures[0].texture = lutA;
    compute.textures[0].linear = true;
    compute.textures[1].texture = lutB;
    compute.textures[1].linear = true;
    compute.images[0] = HD::Frame::Open(width, height);
    compute.push = params;
    compute.pushBytes = sizeof params;

    if (!compute.images[0] || !device->Dispatch(compute)) {
        Info("grade: the frame copy or the compute failed");
        return;
    }
    char text[240];
    std::snprintf(text, sizeof text, "grade: LUT %d (%.0f%%) + LUT %d (%.0f%%), strength %.2f%s", selected.a, (1 - params[0]) * 100, selected.b,
                  params[0] * 100, params[1], params[2] > 0.5f ? ", split screen" : "");
    Info(text);
}
