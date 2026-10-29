// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdForever.cpp, the light-to-setting table, the grading keys and the fog layers only (the
// donor's light colour bands come with the effects that read them).
#include "WoW/Source/WorldClient/HD/HdForever.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "WoW/Source/DB/Rec/LightRec.h"

namespace {
    const int32_t kMaxKeys = 32;

    struct LightEntry {
        int32_t map;
        int32_t slots[8];
    };

    struct GradeKey {
        int32_t time;
        int32_t lut;
    };

    struct FogKey {
        int32_t time;
        int32_t count;
        HD::ForeverFogLayer layers[3];
    };

    std::unordered_map<int32_t, LightEntry> s_lights;
    std::unordered_map<int32_t, std::vector<GradeKey>> s_grading;
    std::unordered_map<int32_t, std::vector<FogKey>> s_fog;

    CVar* s_force = nullptr;
    CVar* s_info = nullptr;
    bool s_triedLoad = false;

    FILE* OpenData(const char* name) {
        char path[260];
        snprintf(path, sizeof(path), "forever_data\\%s", name);
        FILE* file = fopen(path, "rb");
        if (file) return file;
        snprintf(path, sizeof(path), "..\\..\\forever_data\\%s", name);
        return fopen(path, "rb");
    }

    // "L light map slot0..slot7" lines; the file's "B" colour band lines are skipped.
    void LoadLights(FILE* file) {
        static char line[8192];
        while (fgets(line, sizeof(line), file)) {
            char* p = line;
            if (*p != 'L') continue;
            LightEntry entry;
            int32_t id = strtol(p + 1, &p, 10);
            entry.map = strtol(p, &p, 10);
            for (int32_t i = 0; i < 8; i++) entry.slots[i] = strtol(p, &p, 10);
            s_lights[id] = entry;
        }
        fclose(file);
    }

    // "G setting time lut" lines.
    int32_t LoadGrading(FILE* file) {
        char line[128];
        int32_t count = 0;
        while (fgets(line, sizeof(line), file)) {
            if (line[0] != 'G') continue;
            char* p = line + 1;
            GradeKey key;
            int32_t setting = strtol(p, &p, 10);
            key.time = strtol(p, &p, 10);
            key.lut = strtol(p, &p, 10);
            if (key.lut <= 0) continue;
            s_grading[setting].push_back(key);
            count++;
        }
        fclose(file);
        return count;
    }

    void SetColor(float* dst, uint32_t rgb) {
        dst[0] = ((rgb >> 16) & 0xFF) * (1.0f / 255.0f);
        dst[1] = ((rgb >> 8) & 0xFF) * (1.0f / 255.0f);
        dst[2] = (rgb & 0xFF) * (1.0f / 255.0f);
    }

    // (V 2) "F setting time layer flags diffuse emissive shadow start density shadowMul topRate topHeight bottomRate
    // bottomHeight intensity g strength exponent" lines.
    int32_t LoadFog(FILE* file) {
        char line[512];
        int32_t layers = 0;
        while (fgets(line, sizeof(line), file)) {
            if (line[0] != 'F') continue;
            char* p = line + 1;
            int32_t setting = strtol(p, &p, 10);
            int32_t time = strtol(p, &p, 10);
            int32_t index = strtol(p, &p, 10);
            if (index < 0 || index > 2) continue;
            HD::ForeverFogLayer layer;
            layer.flags = strtol(p, &p, 10);
            SetColor(layer.diffuse, static_cast<uint32_t>(strtoul(p, &p, 16)));
            SetColor(layer.emissive, static_cast<uint32_t>(strtoul(p, &p, 16)));
            SetColor(layer.shadow, static_cast<uint32_t>(strtoul(p, &p, 16)));
            layer.start = strtof(p, &p);
            layer.density = strtof(p, &p);
            layer.shadowMul = strtof(p, &p);
            layer.topRate = strtof(p, &p);
            layer.topHeight = strtof(p, &p);
            layer.bottomRate = strtof(p, &p);
            layer.bottomHeight = strtof(p, &p);
            layer.intensity = strtof(p, &p);
            layer.g = strtof(p, &p);
            layer.strength = strtof(p, &p);
            layer.exponent = strtof(p, &p);

            std::vector<FogKey>& keys = s_fog[setting];
            if (keys.empty() || keys.back().time != time) {
                FogKey key;
                memset(&key, 0, sizeof(key));
                key.time = time;
                keys.push_back(key);
            }
            FogKey& key = keys.back();
            key.layers[index] = layer;
            if (key.count < index + 1) key.count = index + 1;
            layers++;
        }
        fclose(file);
        return layers;
    }

    void Load() {
        s_triedLoad = true;
        FILE* lights = OpenData("hd_forever_light.txt");
        FILE* grading = OpenData("hd_forever_grading.txt");
        FILE* fog = OpenData("hd_forever_fog.txt");
        if (lights) LoadLights(lights);
        int32_t keys = grading ? LoadGrading(grading) : 0;
        int32_t layers = fog ? LoadFog(fog) : 0;
        char info[224];
        snprintf(info, sizeof(info), "loaded %d lights, %d grading keys, %d fog layers%s%s%s", static_cast<int>(s_lights.size()),
                 static_cast<int>(keys), static_cast<int>(layers), lights ? "" : ", no forever_data\\hd_forever_light.txt",
                 grading ? "" : ", no forever_data\\hd_forever_grading.txt", fog ? "" : ", no forever_data\\hd_forever_fog.txt");
        if (s_info) s_info->Set(info, true, false, false, false);
    }

    // Same search as the client's GetBandTimeRange (Lights.cpp), with up to 32 keys.
    void BandTimeRange(const int32_t* times, int32_t count, int32_t time, int32_t& index, float& t, int32_t& next) {
        next = 0;
        t = 0.0f;
        index = 0;
        if (count <= 0) return;
        do {
            int32_t start = times[index];
            next = (index + 1) % count;
            int32_t end = times[next];
            if (end > start) {
                if (time >= start && time <= end) {
                    t = static_cast<float>(time - start) / (end - start);
                    return;
                }
            } else if (time <= end || time >= start) {
                end += 2880;
                if (time < start) time += 2880;
                t = static_cast<float>(time - start) / (end - start);
                return;
            }
            index++;
        } while (index < count);
    }

    // The Forever setting for this light and slot (0 = none); hdForeverForce overrides it everywhere.
    int32_t SettingFor(const LightRec* light, int32_t slot) {
        if (s_force && s_force->m_intValue > 0) return s_force->m_intValue;
        auto found = s_lights.find(light->m_id);
        if (found == s_lights.end() || found->second.map != light->m_continentID) return 0;
        int32_t foreverId = found->second.slots[slot];
        if (!foreverId && slot != 0) foreverId = found->second.slots[0];  // e.g. no storm setting: clear weather
        return foreverId;
    }
}

void HD::RegisterForeverCVars() {
    if (s_info) return;
    s_force = CVar::Register("hdForeverForce", "HD: 0 = Forever setting by light, else use this Forever setting everywhere (test)", 1, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdForeverInfo", "HD: what hdForever loaded (read only)", 0, "not loaded yet", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

bool HD::ForeverGradingAt(const LightRec* light, int32_t slot, int32_t time, int32_t* lutA, int32_t* lutB, float* t) {
    if (!s_triedLoad) Load();
    if (!light || slot < 0 || slot > 7) return false;

    int32_t foreverId = SettingFor(light, slot);
    if (!foreverId) return false;
    auto grading = s_grading.find(foreverId);
    if (grading == s_grading.end() || grading->second.empty()) return false;

    const std::vector<GradeKey>& keys = grading->second;
    int32_t times[kMaxKeys];
    int32_t count = static_cast<int32_t>(keys.size() < kMaxKeys ? keys.size() : kMaxKeys);
    for (int32_t i = 0; i < count; i++) times[i] = keys[i].time;

    int32_t index;
    int32_t next;
    BandTimeRange(times, count, time, index, *t, next);
    *lutA = keys[index].lut;
    *lutB = keys[next].lut;
    return true;
}

bool HD::ForeverFogAt(const LightRec* light, int32_t slot, int32_t time, ForeverFog* out) {
    if (!s_triedLoad) Load();
    if (!light || slot < 0 || slot > 7) return false;

    int32_t foreverId = SettingFor(light, slot);
    if (!foreverId) return false;
    auto fog = s_fog.find(foreverId);
    if (fog == s_fog.end() || fog->second.empty()) return false;

    const std::vector<FogKey>& keys = fog->second;
    int32_t times[kMaxKeys];
    int32_t count = static_cast<int32_t>(keys.size() < kMaxKeys ? keys.size() : kMaxKeys);
    for (int32_t i = 0; i < count; i++) times[i] = keys[i].time;

    int32_t index;
    float t;
    int32_t next;
    BandTimeRange(times, count, time, index, t, next);
    const FogKey& a = keys[index];
    const FogKey& b = keys[next];

    out->setting = foreverId;
    out->count = a.count > b.count ? a.count : b.count;
    for (int32_t i = 0; i < 3; i++) {
        out->layers[i] = a.layers[i];
        LerpFogLayer(out->layers[i], b.layers[i], t);
    }
    return true;
}

void HD::LerpFogLayer(ForeverFogLayer& a, const ForeverFogLayer& b, float t) {
    if (!a.flags && !b.flags) return;
    if (!b.flags) {
        // The layer fades out: keep its look, lose its density.
        a.density *= 1.0f - t;
        return;
    }
    if (!a.flags) {
        float density = b.density * t;
        a = b;
        a.density = density;
        return;
    }
    if (t >= 0.5f) a.flags = b.flags;
    for (int32_t c = 0; c < 3; c++) {
        a.diffuse[c] += (b.diffuse[c] - a.diffuse[c]) * t;
        a.emissive[c] += (b.emissive[c] - a.emissive[c]) * t;
        a.shadow[c] += (b.shadow[c] - a.shadow[c]) * t;
    }
    a.start += (b.start - a.start) * t;
    a.density += (b.density - a.density) * t;
    a.shadowMul += (b.shadowMul - a.shadowMul) * t;
    a.topRate += (b.topRate - a.topRate) * t;
    a.topHeight += (b.topHeight - a.topHeight) * t;
    a.bottomRate += (b.bottomRate - a.bottomRate) * t;
    a.bottomHeight += (b.bottomHeight - a.bottomHeight) * t;
    a.intensity += (b.intensity - a.intensity) * t;
    a.g += (b.g - a.g) * t;
    a.strength += (b.strength - a.strength) * t;
    a.exponent += (b.exponent - a.exponent) * t;
}
