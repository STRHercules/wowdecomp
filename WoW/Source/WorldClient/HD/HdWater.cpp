// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdWater.cpp, on Vulkan: the donor's water HLSL (HdWaterShaders.h) compiled at start-up by the
// system's d3dcompiler_47 and drawn through the translated modules; the donor's D3D9 passes (linear depth, world copy)
// are GxEffects passes here, and the two textures reach the water draw through GxEffects::SetDrawTexture.
#include "WoW/Source/WorldClient/HD/HdWater.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <thread>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/ConsoleBase/Types.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "Engine/Source/Gx/GxEffects.h"
#include "Engine/Source/Gx/Types.h"
#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Source/LiquidSystem/Liquid.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdCamera.h"
#include "WoW/Source/WorldClient/HD/HdShaderSwap.h"
#include "WoW/Source/WorldClient/HD/HdWaterShaders.h"

namespace {
#include "WoW/Source/WorldClient/HD/Shaders/Spirv.inc"

    const float kSkyDepth = 5000.0f;
    // Time for the foam scroll wraps every 2000 s: a whole number of turns for Forever's scroll speeds (0.001 steps)
    const uint32_t kTimeWrapMs = 2000000;
    const int32_t kFirstConst = 12;   // the donor's block c12..c37 (26 rows); c12..c15 live at c38..c41 here (HdWaterShaders.h)
    const int32_t kConstCount = 26;

    enum { StateIdle, StateCompiling, StateReady, StateFailed };
    enum { Sh_VS, Sh_PS, Sh_PS335, Sh_Count };
    const char* const kName[Sh_Count] = { "HdWater:VS", "HdWater:PS", "HdWater:PS335" };
    const char* const kFile[Sh_Count] = { "HdWater-VS", "HdWater-PS", "HdWater-PS335" };

    typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    std::atomic<int32_t> s_state(StateIdle);
    std::vector<uint8_t> s_code[Sh_Count];  // written by the compile thread before s_state = StateReady
    char s_threadInfo[260] = "";
    CGxShader* s_shaders[Sh_Count] = {};
    bool s_shadersFailed = false;

    CVar* s_mode = nullptr;
    CVar* s_debug = nullptr;
    CVar* s_body = nullptr;
    CVar* s_reflect = nullptr;
    CVar* s_color = nullptr;
    CVar* s_info = nullptr;
    char s_lastInfo[400] = "";

    // The pass's inputs
    struct Targets {
        GxEffects::Device* device = nullptr;
        const GxEffects::Program* depthProgram = nullptr;
        GxEffects::Texture* depth = nullptr;   // the world's depth (CopyDepth)
        GxEffects::Texture* linear = nullptr;  // linear view depth, R32F
        GxEffects::Texture* scene = nullptr;   // the world image before the liquids
        uint32_t width = 0;
        uint32_t height = 0;
    };
    Targets s_targets;
    bool s_passActive = false;
    bool s_drawBound = false;
    float s_invWidth = 0.0f;
    float s_invHeight = 0.0f;
    const char* s_passNote = "";
    int32_t s_drawsPbr = 0;
    int32_t s_drawsKept = 0;
    int32_t s_lastLegacy = -1;
    int32_t s_lastPbr = 0;

    // ---------------------------------------------------------------------------------------------------------------
    // Water types: Forever's LiquidTypePBRWater rows (the pixel shader's cType[0..6]: r0, r1, r2, r3, r4, r5, r10)
    struct WaterType {
        float rows[7][4];
    };
    struct LegacyMap {
        int32_t pbr;
        bool interior;
    };
    std::map<int32_t, WaterType> s_types;
    std::map<int32_t, LegacyMap> s_legacy;
    bool s_typesLoaded = false;
    bool s_fileFound = false;

    // Built-in rows when the file is missing. Hand-made, NOT Forever's numbers (those come only from the file).
    const WaterType kBuiltinLake = { {
        { 0.90f, 0.36f, 0.22f, 0.30f },
        { 0.50f, 16.0f, 4.00f, 0.50f },
        { 0.25f, 0.50f, 0.90f, 1.00f },
        { 0.03f, 0.22f, 0.45f, -0.15f },
        { 0.80f, 25.0f, 1.00f, 0.010f },
        { 0.30f, 35.0f, 1.50f, 0.015f },
        { 0.25f, 0.03f, 0.50f, 4.00f },
    } };
    const WaterType kBuiltinOcean = { {
        { 0.80f, 0.36f, 0.50f, 0.25f },
        { 0.75f, 4.00f, 6.00f, 6.00f },
        { 0.10f, 0.50f, 0.55f, 2.00f },
        { 0.03f, 0.15f, 0.20f, -0.15f },
        { 0.60f, 22.0f, 15.0f, 0.003f },
        { 0.35f, 22.0f, 1.50f, 0.015f },
        { 0.75f, 0.03f, 0.80f, 4.00f },
    } };
    // 3.3.5 LiquidType -> Forever type, interior
    const int32_t kLegacyDefault[][3] = {
        { 1, 1240, 0 }, { 5, 1240, 0 }, { 9, 1288, 0 }, { 13, 1240, 0 }, { 17, 1290, 1 },
        { 2, 1250, 0 }, { 6, 1250, 0 }, { 10, 1250, 0 }, { 14, 1250, 0 },
        { 41, 1240, 0 }, { 61, 1240, 0 }, { 81, 1240, 0 }, { 100, 1240, 0 },
    };
    std::map<const Liquid::CMaterialSettings*, int32_t> s_settingsType;

    void SetInfo(const char* text) {
        if (!s_info || !strcmp(s_lastInfo, text)) return;
        snprintf(s_lastInfo, sizeof(s_lastInfo), "%s", text);
        s_info->Set(text, true, false, false, false);
    }

    int32_t Mode() {
        return s_mode ? s_mode->m_intValue : 0;
    }

    float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Compile thread: the three shaders, all or nothing.
    void CompileAll() {
        ULONGLONG start = GetTickCount64();
        HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
        D3DCompileFn compile = module ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
        if (!compile) {
            snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: d3dcompiler_47.dll not found, Blizzard water kept");
            s_state.store(StateFailed);
            return;
        }
        struct Job { const char* source; const char* profile; const char* original; };
        const Job jobs[Sh_Count] = {
            { HdWaterShaders::kVertexShader, "vs_3_0", nullptr },
            { HdWaterShaders::kPixelShader, "ps_3_0", "0" },
            { HdWaterShaders::kPixelShader, "ps_3_0", "1" },
        };
        for (int32_t i = 0; i < Sh_Count; i++) {
            D3D_SHADER_MACRO macros[2] = {};
            if (jobs[i].original) {
                macros[0].Name = "HD_ORIGINAL";
                macros[0].Definition = jobs[i].original;
            }
            ID3DBlob* code = nullptr;
            ID3DBlob* errors = nullptr;
            HRESULT hr = compile(jobs[i].source, strlen(jobs[i].source), kFile[i], macros, nullptr, "main", jobs[i].profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
            if (FAILED(hr) || !code) {
                snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: %s: %s", kFile[i], errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown");
                if (errors) errors->Release();
                if (code) code->Release();
                s_state.store(StateFailed);
                return;
            }
            if (errors) errors->Release();
            const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
            s_code[i].assign(bytes, bytes + code->GetBufferSize());
            code->Release();
        }
        snprintf(s_threadInfo, sizeof(s_threadInfo), "shaders compiled in %u ms", static_cast<uint32_t>(GetTickCount64() - start));
        s_state.store(StateReady);
    }

    void StartCompile() {
        int32_t expected = StateIdle;
        if (s_state.compare_exchange_strong(expected, StateCompiling)) std::thread(CompileAll).detach();
    }

    // Our shaders as shader objects in the device's own list (like the sky's), once the compile is done and the
    // device has a translated module for every one of them (all or nothing; the missing ones go to hd-pending).
    bool EnsureShaders() {
        if (s_shaders[Sh_Count - 1]) return true;
        if (s_shadersFailed || s_state.load() != StateReady) return false;
        bool usable = true;
        for (int32_t i = 0; i < Sh_Count; i++) usable = HD::ShaderUsable(s_code[i].data(), s_code[i].size(), kFile[i]) && usable;
        if (!usable) {
            SetInfo("not translated yet (run Tools/HdMaterials/install_hd_shaders.py, restart), Blizzard water kept");
            s_shadersFailed = true;
            return false;
        }
        CGxShader* made[Sh_Count] = {};
        for (int32_t i = 0; i < Sh_Count; i++) {
            EGxShTarget target = i == Sh_VS ? GxSh_Vertex : GxSh_Pixel;
            CGxShader* shader = g_theGxDevicePtr->m_shaderList[target].Ptr(kName[i]);
            if (!shader) {
                shader = g_theGxDevicePtr->m_shaderList[target].New(kName[i], 0, 0);
                shader->m_refCount++;
                shader->m_target = target;
            }
            HD::ShaderSetCode(shader, s_code[i].data(), static_cast<uint32_t>(s_code[i].size()));
            if (!shader->Valid()) {
                char text[300];
                snprintf(text, sizeof(text), "ERROR: the device refused shader %s, Blizzard water kept", kFile[i]);
                SetInfo(text);
                s_shadersFailed = true;
                return false;
            }
            made[i] = shader;
        }
        for (int32_t i = 0; i < Sh_Count; i++) s_shaders[i] = made[i];
        return true;
    }

    // ---------------------------------------------------------------------------------------------------------------
    FILE* OpenData(const char* name) {
        char path[260];
        snprintf(path, sizeof(path), "forever_data\\%s", name);
        FILE* file = fopen(path, "rb");
        if (file) return file;
        snprintf(path, sizeof(path), "..\\..\\forever_data\\%s", name);
        return fopen(path, "rb");
    }

    // forever_data\hd_forever_liquid.txt (V 1): "T type 28 numbers" and "M legacyType foreverType flags"
    void LoadTypes() {
        if (s_typesLoaded) return;
        s_typesLoaded = true;
        FILE* file = OpenData("hd_forever_liquid.txt");
        if (file) {
            s_fileFound = true;
            char line[1024];
            while (fgets(line, sizeof(line), file)) {
                if (line[0] == 'T' && line[1] == ' ') {
                    char* p = line + 2;
                    char* end = nullptr;
                    long id = strtol(p, &end, 10);
                    if (end == p) continue;
                    p = end;
                    WaterType type;
                    int32_t n = 0;
                    for (; n < 28; n++) {
                        double v = strtod(p, &end);
                        if (end == p) break;
                        type.rows[n / 4][n % 4] = static_cast<float>(v);
                        p = end;
                    }
                    if (n == 28) s_types[static_cast<int32_t>(id)] = type;
                } else if (line[0] == 'M' && line[1] == ' ') {
                    int legacy = 0, pbr = 0;
                    char flags[16] = "";
                    if (sscanf(line + 2, "%d %d %15s", &legacy, &pbr, flags) >= 2) {
                        LegacyMap map = { pbr, strchr(flags, 'I') != nullptr };
                        s_legacy[legacy] = map;
                    }
                }
            }
            fclose(file);
        }
        if (s_legacy.empty()) {
            for (size_t i = 0; i < sizeof(kLegacyDefault) / sizeof(kLegacyDefault[0]); i++) {
                LegacyMap map = { kLegacyDefault[i][1], kLegacyDefault[i][2] != 0 };
                s_legacy[kLegacyDefault[i][0]] = map;
            }
        }
    }

    // The 3.3.5 liquid type of a settings object (the settings bank is indexed by liquid type).
    int32_t SettingsType(const Liquid::CMaterialSettings* settings) {
        TSGrowableArray<Liquid::CMaterialSettings*>& bank = Liquid::CSettingsBank::s_settings;
        std::map<const Liquid::CMaterialSettings*, int32_t>::iterator it = s_settingsType.find(settings);
        if (it != s_settingsType.end() && it->second >= 0 && static_cast<uint32_t>(it->second) < bank.Count() && bank.m_data[it->second] == settings) {
            return it->second;
        }
        int32_t found = -1;
        for (uint32_t i = 0; i < bank.Count(); i++) {
            if (bank.m_data[i] == settings) { found = static_cast<int32_t>(i); break; }
        }
        s_settingsType[settings] = found;
        return found;
    }

    bool NameHas(const char* name, const char* what) {
        char low[128];
        size_t i = 0;
        for (; name[i] && i + 1 < sizeof(low); i++) low[i] = static_cast<char>(tolower(static_cast<unsigned char>(name[i])));
        low[i] = 0;
        return strstr(low, what) != nullptr;
    }

    // Forever's rows for this water; interior = no sun (Forever's flag 128).
    const WaterType* TypeFor(const Liquid::CMaterialSettings* settings, bool* interior) {
        LoadTypes();
        int32_t legacy = SettingsType(settings);
        int32_t pbr = 0;
        *interior = false;
        std::map<int32_t, LegacyMap>::const_iterator m = s_legacy.find(legacy);
        if (m != s_legacy.end()) {
            pbr = m->second.pbr;
            *interior = m->second.interior;
        } else {
            // a 3.3.5 type the map does not know: by its depth ramp (texture 4: procedural Ocean / River / Wmo)
            pbr = NameHas(settings->m_textureNames[4], "ocean") ? 1250 : 1240;
        }
        s_lastLegacy = legacy;
        s_lastPbr = pbr;
        std::map<int32_t, WaterType>::const_iterator t = s_types.find(pbr);
        if (t != s_types.end()) return &t->second;
        return pbr == 1250 ? &kBuiltinOcean : &kBuiltinLake;
    }

    float Linear(uint8_t c) {
        return powf(c / 255.0f, 2.2f);
    }

    void LinearColor(const CImVector& c, float* out) {
        out[0] = Linear(c.r);
        out[1] = Linear(c.g);
        out[2] = Linear(c.b);
    }

    // The pass's targets for this size and device; false when one cannot be made.
    bool EnsureTargets(GxEffects::Device* device, uint32_t width, uint32_t height) {
        Targets& t = s_targets;
        if (t.device != device) t = Targets();
        if (!t.depthProgram) t.depthProgram = device->CreateProgram(kWaterDepthFrag, sizeof kWaterDepthFrag, "HD water depth");
        if (!t.depthProgram) return false;
        if (t.width != width || t.height != height) {
            device->Destroy(t.depth);
            device->Destroy(t.linear);
            device->Destroy(t.scene);
            t.depth = device->CreateTexture(width, height, GxEffects::Format::Depth, false);
            t.linear = device->CreateTexture(width, height, GxEffects::Format::R32F, true);
            t.scene = device->CreateTexture(width, height, GxEffects::Format::BGRA8, true);
            t.width = width;
            t.height = height;
        }
        t.device = device;
        return t.depth && t.linear && t.scene;
    }

    void ShowPassInfo() {
        char text[400];
        snprintf(text, sizeof(text), "on (%s) | %s | %d water draws Forever, %d Blizzard | last type %d -> %d | %s",
                 s_fileFound ? "forever_data\\hd_forever_liquid.txt" : "built-in values, hd_forever_liquid.txt not found",
                 s_passNote, s_drawsPbr, s_drawsKept, s_lastLegacy, s_lastPbr, s_threadInfo);
        SetInfo(text);
    }
}

void HD::RegisterWaterCVars() {
    if (s_mode) return;
    s_mode = OptionCVars::Register("hdWater", "HD: 1 = WoW Forever PBR water, 2 = our shaders with the 3.3.5 look (test), 0 = Blizzard water", "1");
    s_debug = CVar::Register("hdWaterDebug", "HD: 1 = water depth, 2 = scattering, 3 = reflections, 4 = foam, 5 = roughness, 6 = world depth, 7 = world under the water (test views)", 0, "0", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_body = CVar::Register("hdWaterBody", "HD: strength of the water's own colour, 1 = Forever", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_reflect = CVar::Register("hdWaterReflect", "HD: screen-space reflections of the world on the water, 1 = on (Forever), 0 = sky only", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_color = CVar::Register("hdWaterColor", "HD: water colour, 1 = the game's own colours (follow the hour, dark at night), 0 = Forever's scattering", 1, "1", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
    s_info = CVar::Register("hdWaterInfo", "HD: hdWater status (read only)", 0, "off", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::WaterBeginLiquidPass(bool cameraInLiquid) {
    s_passActive = false;
    s_drawsPbr = 0;
    s_drawsKept = 0;
    if (!s_mode || Mode() == 0 || Visuals::Current() != Visuals::AzerothRemastered) {
        SetInfo("off");
        return;
    }
    StartCompile();
    int32_t state = s_state.load();
    if (state == StateFailed) { SetInfo(s_threadInfo); return; }
    if (state != StateReady) { SetInfo("compiling (Blizzard water until done)"); return; }
    if (!EnsureShaders()) return;
    if (Mode() != 1) {
        char text[300];
        snprintf(text, sizeof(text), "2 = our shaders with the 3.3.5 look | %s", s_threadInfo);
        SetInfo(text);
        return;
    }
    LoadTypes();
    if (cameraInLiquid) { s_passNote = "camera under water: Blizzard water"; ShowPassInfo(); return; }

    GxEffects::Device* device = GxEffects::Get();
    if (!device) { SetInfo("needs the Vulkan device"); return; }
    uint32_t width = 0;
    uint32_t height = 0;
    float rows[4][4];
    const HD::Camera& camera = HD::CurrentCamera();
    if (!device->TargetSize(width, height) || !HD::ScreenRows(camera, width, height, rows)) {
        s_passNote = "no world camera or depth: Blizzard water";
        ShowPassInfo();
        return;
    }
    if (!EnsureTargets(device, width, height)) { SetInfo("ERROR: the water's targets could not be made"); return; }

    // The world image before the liquids, and its linear depth
    if (!device->CopyColor(s_targets.scene) || !device->CopyDepth(s_targets.depth)) {
        s_passNote = "could not copy the world: Blizzard water";
        ShowPassInfo();
        return;
    }
    const float constants[2][4] = {
        { rows[0][2], rows[0][3], 0.0f, 0.0f },
        { rows[3][0], rows[3][1], rows[3][2], kSkyDepth },
    };
    GxEffects::Pass pass;
    pass.target = s_targets.linear;
    pass.program = s_targets.depthProgram;
    pass.textures[0].texture = s_targets.depth;
    pass.constants = constants;
    pass.constantRows = 2;
    if (!device->Draw(pass)) {
        s_passNote = "linear depth pass failed: Blizzard water";
        ShowPassInfo();
        return;
    }
    s_invWidth = 1.0f / width;
    s_invHeight = 1.0f / height;
    s_passNote = "Forever PBR water";
    s_passActive = true;
}

void HD::WaterEndLiquidPass() {
    if (s_passActive || (s_mode && Mode() == 1 && (s_drawsPbr || s_drawsKept))) ShowPassInfo();
    s_passActive = false;
}

bool HD::WaterBindProcWater(Liquid::CMaterialSettings* settings, uint32_t lightCount) {
    s_drawBound = false;
    int32_t mode = s_mode ? Mode() : 0;
    if (mode == 0 || !settings || !s_shaders[Sh_VS] || Visuals::Current() != Visuals::AzerothRemastered) return false;
    if (lightCount > 3) lightCount = 3;
    // only over the shader model 3 code
    CGxShader* clientPixel = Liquid::CMaterialProcWater::s_pixelShader;
    CGxShader* clientVertex = Liquid::CMaterialProcWater::s_vertexShaders[lightCount];
    if (!clientPixel || !clientVertex || clientPixel->m_code.Count() < 4 || clientVertex->m_code.Count() < 4 ||
        *reinterpret_cast<const uint32_t*>(clientPixel->m_code.m_data) != 0xFFFF0300u ||
        *reinterpret_cast<const uint32_t*>(clientVertex->m_code.m_data) != 0xFFFE0300u) {
        return false;
    }

    if (mode != 1) {
        // hdWater 2: our vertex shader and our copy of the 3.3.5 pixel shader, the client's constants and states
        g_theGxDevicePtr->RsSet(GxRs_VertexShader, s_shaders[Sh_VS]);
        g_theGxDevicePtr->RsSet(GxRs_PixelShader, s_shaders[Sh_PS335]);
        return true;
    }
    if (!s_passActive) { s_drawsKept++; return false; }

    bool interior = false;
    const WaterType* type = TypeFor(settings, &interior);

    // The donor's c12..c37 block (the pixel shader's header lists them)
    float c[kConstCount * 4];
    memset(c, 0, sizeof(c));
    const C44Matrix& mv = Liquid::s_vsConstants.m_modelView;
    c[0] = mv.a0; c[1] = mv.a1; c[2] = mv.a2;      // model -> view rows (x, y, z axes in view space)
    c[4] = mv.b0; c[5] = mv.b1; c[6] = mv.b2;
    c[8] = mv.c0; c[9] = mv.c1; c[10] = mv.c2;
    // to the sun, view space (the client's c33 is the direction the light travels)
    const C4Vector& sun = Liquid::s_vsConstants.m_sunDir;
    float sl = sqrtf(sun.x * sun.x + sun.y * sun.y + sun.z * sun.z);
    if (sl < 1e-6f) sl = 1.0f;
    c[12] = -sun.x / sl; c[13] = -sun.y / sl; c[14] = -sun.z / sl;
    c[16] = s_invWidth; c[17] = s_invHeight; c[18] = interior ? 1.0f : 0.0f; c[19] = 1.0f;     // c16
    const C4Vector& fog = Liquid::s_vsConstants.m_fog;                                         // c17
    c[20] = fog.x; c[21] = fog.y; c[22] = fog.z;
    c[23] = static_cast<float>(OsGetAsyncTimeMs() % kTimeWrapMs) * 0.001f;
    memcpy(&c[24], type->rows, sizeof(type->rows));                                            // c18..c24
    // c25..c29: sky colours (linear): top, middle, low band, horizon, average (DayNight bands 2, 3, 5, 6)
    const DayNight::CurrentLight& bands = DayNight::g_dnInfo.m_bands;
    LinearColor(bands.m_skyColors[0], &c[52]);
    LinearColor(bands.m_skyColors[1], &c[56]);
    LinearColor(bands.m_skyColors[3], &c[60]);
    LinearColor(bands.m_skyColors[4], &c[64]);
    for (int32_t k = 0; k < 3; k++) c[68 + k] = (c[52 + k] + c[56 + k] * 2.0f + c[60 + k] + c[64 + k]) * 0.2f;
    c[55] = static_cast<float>(s_debug ? s_debug->m_intValue : 0);
    c[59] = s_body ? Clamp(s_body->m_floatValue, 0.0f, 5.0f) : 1.0f;
    c[63] = s_reflect ? Clamp(s_reflect->m_floatValue, 0.0f, 1.0f) : 1.0f;   // c27.w: screen-space reflections
    // c30: what lights interior water's scattering (Forever: 1; here the client's ambient + sun colour, linear)
    const C4Vector& amb = Liquid::s_psProcWaterConstants.m_sunAmbient;
    const C4Vector& dif = Liquid::s_psProcWaterConstants.m_sunDiffuse;
    c[72] = powf(Clamp(amb.x + dif.x, 0.0f, 4.0f), 2.2f);
    c[73] = powf(Clamp(amb.y + dif.y, 0.0f, 4.0f), 2.2f);
    c[74] = powf(Clamp(amb.z + dif.z, 0.0f, 4.0f), 2.2f);
    c[75] = s_color ? Clamp(s_color->m_floatValue, 0.0f, 1.0f) : 1.0f;      // c30.w: the game's water colour (hdWaterColor)
    // c31: view -> screen uv (ndc = a0 x / z + c0, b1 y / z + c1; uv y points down)
    const C44Matrix& proj = g_theGxDevicePtr->m_projNative;
    c[76] = 0.5f * proj.a0; c[77] = -0.5f * proj.b1; c[78] = 0.5f + 0.5f * proj.c0; c[79] = 0.5f - 0.5f * proj.c1;
    // c32, c33: the client's ambient and sun colours (c7, c8) made linear here instead of in the shader
    for (int32_t k = 0; k < 3; k++) {
        c[80 + k] = powf(Clamp((&amb.x)[k], 0.0f, 4.0f), 2.2f);
        c[84 + k] = powf(Clamp((&dif.x)[k], 0.0f, 4.0f), 2.2f);
    }
    // c16..c37 as they are; the model-view and sun rows (the donor's c12..c15) at c38..c41
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 16, c + 16, kConstCount - 4);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 38, c, 4);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, s_shaders[Sh_VS]);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, s_shaders[Sh_PS]);
    // the shader mixes the world behind the water itself and writes alpha 1: keep the target's alpha (glow);
    // write the water's depth, so what is drawn after sees the surface
    g_theGxDevicePtr->RsSet(GxRs_ColorWrite, 7);
    g_theGxDevicePtr->RsSet(GxRs_DepthWrite, 1);
    g_theGxDevicePtr->RsSet(GxRs_Texture6, static_cast<void*>(nullptr));
    g_theGxDevicePtr->RsSet(GxRs_Texture7, static_cast<void*>(nullptr));
    GxEffects::SetDrawTexture(6, s_targets.linear, false, false);
    GxEffects::SetDrawTexture(7, s_targets.scene, false, false);
    s_drawBound = true;
    s_drawsPbr++;
    return true;
}

void HD::WaterAfterDraw() {
    if (!s_drawBound) return;
    s_drawBound = false;
    GxEffects::SetDrawTexture(6, nullptr);
    GxEffects::SetDrawTexture(7, nullptr);
}
