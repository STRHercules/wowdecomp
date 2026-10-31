// SPDX-License-Identifier: GPL-3.0-or-later
// The surface cover's shaders (HdSurfaceCover.h). The lighting, fog, shadow and HD lights code is Azeroth Remastered's
// terrain shaders' (HdTerrain.cpp) for one layer, unchanged; what is new is the cover's: the lift far away, where it
// lies (the chunk's alpha maps weighted by each layer's cover), its edges and its look.
#include "WoW/Source/WorldClient/HD/HdSurfaceCover.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "WoW/Source/WorldClient/HD/HdShaderSwap.h"

namespace {
    const char kVertexShader[] = R"HLSL(
// Surface cover vertex shader (vs_3_0): HdTerrain's vertex shader for one layer, without per-vertex point lights, over
// the cover's own vertices (the chunk's, lifted by the cover's depth; COLOR0 = rgb its tint, a how much of the slope
// holds it). The terrain's constants c0..c53 are those of the chunk just drawn.
#ifndef SHADOW
#define SHADOW 0
#endif
#if SHADOW
#define HD_POS TEXCOORD3
#else
#define HD_POS TEXCOORD5
#endif

float4 cWorldView[4]  : register(c0);
float4 cProj[4]       : register(c4);
float4 cFog           : register(c12);
float4 cChunkPos      : register(c23);
float4 cSunDir        : register(c24);
float4 cSunAmbient    : register(c25);
float4 cSunDiffuse    : register(c26);
float4 cShadowMtx[12] : register(c37);
float4 cHdLight       : register(c49);  // z = 1 Forever lighting
float4 cHdLight2      : register(c50);  // x = ambient strength in Forever lighting
float4 cHdHorizon     : register(c51);
float4 cHdGround      : register(c52);
float4 cHdCamera      : register(c53);  // xyz = camera world position
float4 cCoverUv       : register(c54);  // x = cover texture scale (per yard), yz = alpha map scale (x term, y term)
float4 cCoverLift     : register(c55);  // x = yards added at the far end, y = 1 / the distance that is reached at,
                                        // z = 1 when s0 holds a cover texture (passed on in COLOR1.a)

void main(
    float3 iPos : POSITION0,
    float3 iNormal : NORMAL0,
    float4 iColor : COLOR0,
    out float4 oPos : POSITION0,
    out float4 oDiffuse : COLOR0,
    out float4 oTint : COLOR1,
    out float4 oTex0 : TEXCOORD0,
    out float4 oTex1 : TEXCOORD1,
#if SHADOW
    out float4 oShadow0 : TEXCOORD4,
    out float4 oShadow1 : TEXCOORD5,
    out float4 oShadow2 : TEXCOORD6,
#endif
    out float4 oHdPos : HD_POS,
    out float4 oDirect : TEXCOORD7)
{
    float4 vp = iPos.x * cWorldView[0] + iPos.y * cWorldView[1] + iPos.z * cWorldView[2] + cWorldView[3];
    // Far away the terrain is drawn coarser than its heights (and the depth buffer is coarse): lift the cover there,
    // only where it lies.
    float lift = saturate(length(vp.xyz) * cCoverLift.y) * cCoverLift.x * step(0.004, iColor.a);
    vp = vp + lift * cWorldView[2];
    float3 p = iPos + float3(0.0, 0.0, lift);
    oPos = vp.x * cProj[0] + vp.y * cProj[1] + vp.z * cProj[2] + vp.w * cProj[3];
    float fog = min(pow(max(vp.z * cFog.x + cFog.y, 0.0), cFog.z), 1.0);

    float3 n = iNormal.x * cWorldView[0].xyz + iNormal.y * cWorldView[1].xyz + iNormal.z * cWorldView[2].xyz;
    float3 shade = float3(0.5, 0.5, 0.5);
    float3 nw = normalize(iNormal);
    oHdPos = float4(p - cHdCamera.xyz, nw.x);
    oDirect = float4(0.0, 0.0, 0.0, fog);
    [branch] if (cHdLight.z > 0.5) {
        float3 nn = normalize(n);
        float up = dot(normalize(cWorldView[2].xyz), nn);
        float nl = dot(cSunDir.xyz, nn);
        float3 sky = cSunAmbient.xyz * cHdLight2.x;
        float3 horizon = sky * cHdHorizon.xyz;
        float3 hemi = horizon + (sky - horizon) * saturate(up) + (sky * cHdGround.xyz - horizon) * saturate(-up);
        float3 ambient = hemi * (nl * 0.2 + 0.9);
        float3 direct = cSunDiffuse.xyz * saturate(nl);
        float3 fit = 1.0 / max(ambient + direct, 1.0);
        oDiffuse = float4(ambient * fit * shade, iColor.a);
        oDirect = float4(direct * fit * shade, fog);
    } else {
        float3 light = cSunDiffuse.xyz * saturate(dot(cSunDir.xyz, n)) + cSunAmbient.xyz;
        light = min(light, 1.0);
        oDiffuse = float4(light * shade, iColor.a);
    }
    oTint = float4(iColor.rgb, cCoverLift.z);  // a = 1 when s0 holds a cover texture

    float2 rel = p.xy - cChunkPos.xy;
    oTex0 = float4((rel * cCoverUv.x).yx, 0.0, nw.y);
    oTex1 = float4((rel * cCoverUv.yz).yx, p.xy);
#if SHADOW
    oShadow0 = float4(dot(vp, cShadowMtx[0]), dot(vp, cShadowMtx[1]), dot(vp, cShadowMtx[2]), dot(vp, cShadowMtx[9]));
    oShadow1 = float4(dot(vp, cShadowMtx[3]), dot(vp, cShadowMtx[4]), dot(vp, cShadowMtx[5]), dot(vp, cShadowMtx[10]));
    oShadow2 = float4(dot(vp, cShadowMtx[6]), dot(vp, cShadowMtx[7]), dot(vp, cShadowMtx[8]), dot(vp, cShadowMtx[11]));
#endif
}
)HLSL";

    const char kPixelShader[] = R"HLSL(
// Surface cover pixel shader (ps_3_0). SHADOW_MODE and PCF as HdTerrain's pixel shader (Terrain1 = 0, Terrain3[_pcf] =
// 1..3); its shadow, HD lights and fog code unchanged. The chunk's blend texture (s1) gives each terrain layer's share
// here, cCoverLayer how much of the cover lies on each layer; the result, times the slope's share (COLOR0.a), is the
// cover's alpha, its edge broken up by noise in world space.
#ifndef SHADOW_MODE
#define SHADOW_MODE 0
#endif
#ifndef PCF
#define PCF 0
#endif
#if SHADOW_MODE >= 1
#define HD_POS TEXCOORD3
#else
#define HD_POS TEXCOORD5
#endif

float4 cFogColor     : register(c2);
float4 cPcfOffset[8] : register(c3);
float4 cHdPs         : register(c16); // x = 1 Forever lighting, z = point light count (0..4)
float4 cHdLight[16]  : register(c17);
float4 cCoverLayer   : register(c33); // the cover's share of terrain layer 0..3
float4 cCoverLook    : register(c34); // rgb = albedo, w = glints (1 for snow, 0 for sand and ash)
float4 cCoverEdge    : register(c35); // x = edge breakup 0..1, y = 1 for big alpha maps, z = noise scale (per yard),
                                      // w = 1 for the near patch (its share is COLOR0.a, no blend texture)
float4 cNearRect     : register(c36); // the near patch (world min x, min y, max x, max y): chunks leave it to the patch

sampler2D sCover     : register(s0);
sampler2D sBlend     : register(s1);
#if SHADOW_MODE >= 1
sampler2D sShadow0   : register(s5);
#endif
#if SHADOW_MODE >= 2
sampler2D sShadow1   : register(s6);
sampler2D sShadow2   : register(s7);
sampler2D sShadow3   : register(s8);
#endif

float Tap(sampler2D s, float2 uv, float z)
{
#if PCF
    return tex2Dlod(s, float4(uv, z, 0.0)).x;
#else
    return step(z, tex2Dlod(s, float4(uv, 0.0, 0.0)).x);
#endif
}

float Sum5(sampler2D s, float2 c, float z)
{
    float2 uv = c * 0.5 + 0.5;
    float sum = Tap(s, uv, z);
    sum = sum + Tap(s, uv + cPcfOffset[0].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[2].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[4].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[6].xy, z);
    return sum;
}

float Sum9(sampler2D s, float2 c, float z)
{
    float2 uv = c * 0.5 + 0.5;
    float sum = Tap(s, uv, z);
    sum = sum + Tap(s, uv + cPcfOffset[0].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[1].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[2].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[3].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[4].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[5].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[6].xy, z);
    sum = sum + Tap(s, uv + cPcfOffset[7].xy, z);
    return sum;
}

float EdgeFade(float2 c)
{
    return saturate(max(abs(c.x), abs(c.y)) * -3.44827580 + 3.41379309);
}
float EdgeFade2(float2 c)
{
    return saturate(max(abs(c.x), abs(c.y)) * -11.1111107 + 11.0);
}

float3 HdPointLight(float3 p, float3 n, float4 l0, float4 l1, float4 l2, float4 l3)
{
    float3 l = l0.xyz - p;
    float d2 = dot(l, l);
    float inv = rsqrt(d2);
    float d = inv * d2;
    float nl = max(dot(l, n) * inv, 0.0);
    float3 c;
    if (l0.w > 0.5) {
        c = l1.xyz * (nl / ((inv * 0.7 + 0.03) * d2));
    } else {
        float a = 1.0 - saturate((d - l1.w) * l2.w);
        float t = saturate(d * l3.x + l3.y);
        t = t * t * (3.0 - t * 2.0);
        c = (l2.xyz + (l1.xyz - l2.xyz) * t) * (a * a * nl);
    }
    return c * saturate((l3.z - d) * l3.w);
}

float3 HdCombine(float3 light, float3 buffer)
{
    return pow(pow(max(light, 0.0), 2.2) + buffer, 1.0 / 2.2);
}

// Value noise, 0..1, periodic over 256 lattice cells so world-sized coordinates keep their precision.
float Hash(float2 i)
{
    i = i - 256.0 * floor(i / 256.0);
    return frac(sin(dot(i, float2(12.9898, 78.233))) * 43758.5453);
}
float Noise(float2 p)
{
    float2 i = floor(p);
    float2 f = p - i;
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(Hash(i), Hash(i + float2(1.0, 0.0)), f.x);
    float b = lerp(Hash(i + float2(0.0, 1.0)), Hash(i + float2(1.0, 1.0)), f.x);
    return lerp(a, b, f.y);
}

void main(
    float4 iDiffuse : COLOR0,
    float4 iTint : COLOR1,
    float4 iTex0 : TEXCOORD0,
    float4 iTex1 : TEXCOORD1,
#if SHADOW_MODE >= 1
    float4 iShadow0 : TEXCOORD4,
#endif
#if SHADOW_MODE >= 2
    float4 iShadow1 : TEXCOORD5,
    float4 iShadow2 : TEXCOORD6,
#endif
    float4 iHdPos : HD_POS,
    float4 iDirect : TEXCOORD7,
    out float4 oColor : COLOR0)
{
    // ---- where the cover lies: each layer's share here (as the terrain blends them) times the cover on that layer
    float4 a = tex2D(sBlend, iTex1.xy);
    float n = Noise(iTex1.zw * cCoverEdge.z) * 0.65 + Noise(iTex1.zw * (cCoverEdge.z * 2.7) + 17.0) * 0.35;
    float cover;
    [branch] if (cCoverEdge.w > 0.5) {
        // the near patch: its edges (and their slope down to the ground) are worked out per vertex
        cover = iDiffuse.a;
        a = float4(0.0, 0.0, 0.0, 1.0);
    } else {
        // inside the near patch the patch draws the cover
        clip(any(iTex1.zw < cNearRect.xy) || any(iTex1.zw > cNearRect.zw) ? 1.0 : -1.0);
        float4 w;
        [flatten] if (cCoverEdge.y > 0.5) {
            w = float4(1.0 - saturate(a.x + a.y + a.z), a.x, a.y, a.z);
        } else {
            w.w = a.z;
            w.z = a.y * (1.0 - a.z);
            w.y = a.x * (1.0 - a.y) * (1.0 - a.z);
            w.x = (1.0 - a.x) * (1.0 - a.y) * (1.0 - a.z);
        }
        float share = dot(w, cCoverLayer) * iDiffuse.a;
        // The noise moves the edge (all of it at breakup 1); a smooth edge fades over a wide band, a ragged one over a
        // narrow one. Full cover stays full, no cover stays none.
        float b = cCoverEdge.x;
        float soft = lerp(0.35, 0.08, b);
        cover = saturate((share * (1.0 + b) - b * n - 0.5) / soft + 0.5) * step(0.002, share);
    }
    clip(cover - 0.004);

    // ---- shadow term S (1 = lit), as the terrain
#if SHADOW_MODE == 0
    float S = a.w;
#endif
#if SHADOW_MODE == 1
    float S = a.w;
    float fade = EdgeFade(iShadow0.xy);
    [branch] if (fade != 0.0) S = min(fade * (Sum5(sShadow0, iShadow0.xy, iShadow0.z) * 0.2 - 1.0) + 1.0, a.w);
#endif
#if SHADOW_MODE == 2
    float dyn = 1.0;
    float fade = EdgeFade(iShadow0.xy);
    [branch] if (fade != 0.0) dyn = fade * (Sum5(sShadow0, iShadow0.xy, iShadow0.z) * 0.2 - 1.0) + 1.0;
    float band;
    [branch] if (max(abs(iShadow1.x), abs(iShadow1.y)) < 1.0) {
        band = Sum5(sShadow1, iShadow1.xy, iShadow1.z) * 0.2;
    } else {
        [branch] if (max(abs(iShadow2.x), abs(iShadow2.y)) < 1.0) {
            band = Sum5(sShadow2, iShadow2.xy, iShadow2.z) * 0.2;
        } else {
            float3 c3 = float3(iShadow0.w, iShadow1.w, iShadow2.w);
            band = EdgeFade2(c3.xy) * (Sum5(sShadow3, c3.xy, c3.z) * 0.2 - 1.0) + 1.0;
        }
    }
    float S = min(band, dyn);
#endif
#if SHADOW_MODE == 3
    float S;
    [branch] if (EdgeFade(iShadow0.xy) != 0.0) {
        S = Sum9(sShadow0, iShadow0.xy, iShadow0.z) * 0.111111112;
    } else {
        [branch] if (max(abs(iShadow1.x), abs(iShadow1.y)) < 1.0) {
            S = Sum5(sShadow1, iShadow1.xy, iShadow1.z) * 0.2;
        } else {
            [branch] if (max(abs(iShadow2.x), abs(iShadow2.y)) < 1.0) {
                S = Sum5(sShadow2, iShadow2.xy, iShadow2.z) * 0.2;
            } else {
                float3 c3 = float3(iShadow0.w, iShadow1.w, iShadow2.w);
                S = EdgeFade2(c3.xy) * (Sum5(sShadow3, c3.xy, c3.z) * 0.2 - 1.0) + 1.0;
            }
        }
    }
#endif

    // ---- HD lights, as the terrain
    float3 buffer = float3(0.0, 0.0, 0.0);
    [branch] if (cHdPs.z > 0.5) {
        float3 nw = float3(iHdPos.w, iTex0.w, sqrt(saturate(1.0 - iHdPos.w * iHdPos.w - iTex0.w * iTex0.w)));
        nw = normalize(nw);
        buffer = buffer + HdPointLight(iHdPos.xyz, nw, cHdLight[0], cHdLight[1], cHdLight[2], cHdLight[3]);
        [branch] if (cHdPs.z > 1.5) {
            buffer = buffer + HdPointLight(iHdPos.xyz, nw, cHdLight[4], cHdLight[5], cHdLight[6], cHdLight[7]);
            [branch] if (cHdPs.z > 2.5) {
                buffer = buffer + HdPointLight(iHdPos.xyz, nw, cHdLight[8], cHdLight[9], cHdLight[10], cHdLight[11]);
                [branch] if (cHdPs.z > 3.5) {
                    buffer = buffer + HdPointLight(iHdPos.xyz, nw, cHdLight[12], cHdLight[13], cHdLight[14], cHdLight[15]);
                }
            }
        }
    }

    // ---- the look: the cover texture (or plain, with a little unevenness), tint, brightness; lit like the terrain
    float4 t = tex2D(sCover, iTex0.xy);
    float3 plain = (0.94 + 0.06 * n).xxx;
    float3 albedo = lerp(plain, t.rgb, iTint.a) * cCoverLook.rgb * iTint.rgb;
    float f = lerp(S * 0.3 + 0.7, 1.0, cHdPs.x);
    float3 light = (iDiffuse.rgb * f + iDirect.xyz * S) * 2.0;
    [branch] if (cHdPs.z > 0.5) light = HdCombine(light, buffer);
    // Snow: a faint blue where the sun does not reach, and glints where it does (fixed points in the snow that flash as
    // the view moves over them).
    light = light + (1.0 - S) * float3(-0.010, 0.004, 0.030);
    float2 glintCell = floor(iTex1.zw * 7.0);
    float glint = Hash(glintCell + 31.0);
    float view = frac(glint * 13.7 + dot(normalize(iHdPos.xyz), float3(5.1, 7.3, 3.7)));
    float sparkle = step(0.965, glint) * step(0.86, view) * S * saturate(length(iDirect.xyz) * 3.0) * cCoverLook.w;
    float3 col = albedo * light + sparkle * 0.9;
    col = (col - cFogColor.xyz) * iDirect.w + cFogColor.xyz;
    oColor = float4(col, cover);
}
)HLSL";

    typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    // vertex: SHADOW 0/1; pixel: SHADOW_MODE 0..3 + 4 * PCF
    constexpr int32_t kVertexSlots = 2;
    constexpr int32_t kPixelSlots = 8;

    enum { StateIdle, StateCompiling, StateReady, StateFailed };

    std::atomic<int32_t> s_state(StateIdle);
    std::vector<uint8_t> s_vertexCode[kVertexSlots];  // written by the compile thread before s_state = StateReady
    std::vector<uint8_t> s_pixelCode[kPixelSlots];
    char s_threadInfo[260] = "not started";           // written by the compile thread before s_state changes
    char s_info[320] = "not started";

    CGxShader* s_vertex[kVertexSlots] = {};
    CGxShader* s_pixel[kPixelSlots] = {};
    int8_t s_usable[kVertexSlots + kPixelSlots] = {};  // 0 not checked, 1 ours in place, -1 not translated

    bool Compile(D3DCompileFn compile, const char* source, const char* name, const char* macro0, int32_t value0,
                 const char* macro1, int32_t value1, const char* profile, std::vector<uint8_t>& out) {
        char values[2][4];
        snprintf(values[0], 4, "%d", value0);
        snprintf(values[1], 4, "%d", value1);
        D3D_SHADER_MACRO macros[3] = { { macro0, values[0] }, { macro1, values[1] }, { nullptr, nullptr } };
        if (!macro1) macros[1].Definition = nullptr;  // macro1 null ends the list there
        ID3DBlob* code = nullptr;
        ID3DBlob* errors = nullptr;
        HRESULT hr = compile(source, strlen(source), name, macros, nullptr, "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
        bool ok = SUCCEEDED(hr) && code;
        if (ok) {
            const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
            out.assign(bytes, bytes + code->GetBufferSize());
        } else {
            snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: %s %s=%d: %s", name, macro0, value0,
                     errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown");
        }
        if (errors) errors->Release();
        if (code) code->Release();
        return ok;
    }

    void CompileAll() {
        ULONGLONG start = GetTickCount64();
        HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
        D3DCompileFn compile = module ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
        if (!compile) {
            snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: d3dcompiler_47.dll not found, no cover");
            s_state.store(StateFailed);
            return;
        }
        for (int32_t p = 0; p < kVertexSlots; p++) {
            if (!Compile(compile, kVertexShader, "SurfaceCoverVS", "SHADOW", p, nullptr, 0, "vs_3_0", s_vertexCode[p])) {
                s_state.store(StateFailed);
                return;
            }
        }
        for (int32_t p = 0; p < kPixelSlots; p++) {
            if (!Compile(compile, kPixelShader, "SurfaceCoverPS", "SHADOW_MODE", p & 3, "PCF", p >> 2, "ps_3_0", s_pixelCode[p])) {
                s_state.store(StateFailed);
                return;
            }
        }
        snprintf(s_threadInfo, sizeof(s_threadInfo), "%d shaders compiled in %u ms", kVertexSlots + kPixelSlots,
                 static_cast<uint32_t>(GetTickCount64() - start));
        s_state.store(StateReady);
    }

    // Our code into the shader object of this slot, once the device has its translated module. Every slot is offered
    // at the first call, so one install covers them all.
    bool Usable(int32_t index, bool vertex, int32_t slot) {
        if (s_usable[index]) return s_usable[index] > 0;
        const std::vector<uint8_t>& code = vertex ? s_vertexCode[slot] : s_pixelCode[slot];
        char name[48];
        snprintf(name, sizeof(name), "SurfaceCover-%s-%d", vertex ? "v" : "p", slot);
        if (!HD::ShaderUsable(code.data(), code.size(), name)) {
            s_usable[index] = -1;
            return false;
        }
        CGxShader* shader = vertex ? s_vertex[slot] : s_pixel[slot];
        HD::ShaderSetCode(shader, code.data(), static_cast<uint32_t>(code.size()));
        s_usable[index] = 1;
        return true;
    }
}

void HD::SurfaceCoverShadersStart() {
    int32_t expected = StateIdle;
    if (!s_state.compare_exchange_strong(expected, StateCompiling)) return;
    snprintf(s_threadInfo, sizeof(s_threadInfo), "compiling");
    std::thread(CompileAll).detach();
}

bool HD::SurfaceCoverShaders(int32_t shadowMode, bool pcf, CGxShader** vertex, CGxShader** pixel) {
    if (s_state.load() == StateIdle) HD::SurfaceCoverShadersStart();
    if (s_state.load() != StateReady || !g_theGxDevicePtr) {
        snprintf(s_info, sizeof(s_info), "%s", s_threadInfo);
        return false;
    }
    if (!s_vertex[0]) {
        // Shader objects of our own, named after no file: nothing loads into them but our code. More than one
        // permutation, so the backend makes no program before the code is there.
        g_theGxDevicePtr->ShaderCreate(s_vertex, GxSh_Vertex, "Shaders\\Vertex", "EsteriaSurfaceCover", kVertexSlots);
        g_theGxDevicePtr->ShaderCreate(s_pixel, GxSh_Pixel, "Shaders\\Pixel", "EsteriaSurfaceCover", kPixelSlots);
        int32_t missing = 0;
        for (int32_t p = 0; p < kVertexSlots; p++) missing += !Usable(p, true, p);
        for (int32_t p = 0; p < kPixelSlots; p++) missing += !Usable(kVertexSlots + p, false, p);
        if (missing) {
            snprintf(s_info, sizeof(s_info), "%s; %d not translated yet (run Tools/HdMaterials/install_hd_shaders.py, restart)", s_threadInfo, missing);
        } else {
            snprintf(s_info, sizeof(s_info), "%s, in use", s_threadInfo);
        }
    }
    int32_t v = shadowMode != 0 ? 1 : 0;
    int32_t p = (shadowMode < 0 ? 0 : shadowMode > 3 ? 3 : shadowMode) + (pcf ? 4 : 0);
    if (s_usable[v] <= 0 || s_usable[kVertexSlots + p] <= 0) return false;
    *vertex = s_vertex[v];
    *pixel = s_pixel[p];
    return true;
}

const char* HD::SurfaceCoverShaderInfo() {
    return s_info;
}
