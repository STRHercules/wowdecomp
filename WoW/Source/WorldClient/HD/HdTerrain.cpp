// SPDX-License-Identifier: GPL-3.0-or-later
// Azeroth Remastered's HdTerrain.cpp: its terrain shaders (the 3.3.5 terrain shaders rewritten in HLSL, with WoW Forever's
// terrain lighting), the HLSL below unchanged, compiled at start-up by the system's d3dcompiler_47 and put into the
// client's terrain CGxShaders (HdShaderSwap.h) when the Vulkan backend has their translated modules. Only with visuals 2.
// Not ported yet: hdLights (the per-pixel lamps; the shaders get no light, the vertex shader keeps 3.3.5's per-vertex ones).
#include "WoW/Source/WorldClient/HD/HdTerrain.h"

#include <windows.h>
#include <d3dcompiler.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "Engine/Source/ConsoleBase/ConsoleVar.h"
#include "Engine/Source/Gx/CGxDevice/CGxDevice.h"
#include "Engine/Source/Gx/CGxDevice/CGxShader.h"
#include "WoW/Source/DB/Rec/LightRec.h"
#include "WoW/Source/Ui/OptionCVars.h"
#include "WoW/Source/Ui/Visuals.h"
#include "WoW/Source/WorldClient/DayNight.h"
#include "WoW/Source/WorldClient/DayNight__DNInfo.h"
#include "WoW/Source/WorldClient/HD/HdForever.h"
#include "WoW/Source/WorldClient/HD/HdShaderSwap.h"

namespace {
    // Terrain vertex shader. Source of truth: the 3.3.5 shader rewritten from its bytecode (Shaders\Vertex\vs_3_0\
    // Terrain.bls), checked numerically against it for all 128 permutations, plus the Forever lighting path.
    const char kVertexShader[] = R"HLSL(
// Waystone HD terrain vertex shader (vs_3_0). Replaces Shaders\Vertex\vs_3_0\Terrain.bls permutation P:
//   P = 64*POINT_LIGHTS + 16*(LAYERS-1) + 8*SPECULAR + 4*VERTEX_COLOR + 2*ENV_LAYER0 + SHADOW
// With c49.z = 0 it computes exactly what the 3.3.5 shader does (TEXCOORD7 = 0).
// With c49.z = 1 it lights the ground like WoW Forever's terrain shader: the ambient comes from a sky / horizon /
// ground hemisphere and is shaded by a soft wrap of the sun (0.7 .. 1.1); the sun itself goes out separately in
// TEXCOORD7 so the pixel shader can cut it fully in shadow instead of darkening everything to 70%.
// HD lights (hdLights): the pixel shader lights the ground with the point lights itself, per pixel (WoW Forever's
// light buffer maths). For that this shader also writes the camera-relative world position and the world normal:
// HD_POS = (position - camera, normal.x), TEXCOORD0.w = normal.y (a ground normal always points up, so the pixel
// shader rebuilds z). To fit in the pixel shader's 10 inputs our pixel shader reads the fog from TEXCOORD7.w, not
// FOG, and the Forever / test-view flags of TEXCOORD7.w move to pixel
// constant c16. With c53.w = 1 the 3.3.5 per-vertex point lights are left out of the Forever ambient (the pixel
// shader does them); with c53.w = 0 nothing else changes.
// HD: no FOG output. vs_3_0 has 11 o# registers besides PSIZE, and SHADOW with LAYERS 3 or 4 needed 12 with it
// (error X5622). A vs_3_0 shader only ever runs with a ps_3_0 one, which never gets fixed-function fog, and our
// pixel shader reads the fog from TEXCOORD7.w, so FOG was read by nobody.
#ifndef LAYERS
#define LAYERS 1
#endif
#ifndef POINT_LIGHTS
#define POINT_LIGHTS 0
#endif
#ifndef SPECULAR
#define SPECULAR 0
#endif
#ifndef VERTEX_COLOR
#define VERTEX_COLOR 0
#endif
#ifndef ENV_LAYER0
#define ENV_LAYER0 0
#endif
#ifndef SHADOW
#define SHADOW 0
#endif
// HD lights: the free output slot for the position (TEXCOORD3 is free with SHADOW, TEXCOORD5 without)
#if SHADOW
#define HD_POS TEXCOORD3
#else
#define HD_POS TEXCOORD5
#endif

float4 cWorldView[4]  : register(c0);   // rows of (translate(-camera) * view)
float4 cProj[4]       : register(c4);   // rows of native projection
float4 cViewT[3]      : register(c8);   // rows of transpose(view); only read when ENV_LAYER0
float4 cFog           : register(c12);  // x = -fogMul/(end-start), y = end/(end-start), z = density exponent
float4 cTexOffset[5]  : register(c13);  // per tex-coord set k (c17 = m_texScale[0], always 0)
float4 cTexScale[5]   : register(c18);  // per tex-coord set k
float4 cChunkPos      : register(c23);  // chunk origin, world
float4 cSunDir        : register(c24);  // view-space direction to the sun
float4 cSunAmbient    : register(c25);
float4 cSunDiffuse    : register(c26);
float4 cSunSpecular   : register(c27);  // w = exponent (20)
float4 cPointLight[9] : register(c28);  // 3 x {pos (world - camera), colour, attenuation (k0,k1,k2)}
float4 cShadowMtx[12] : register(c37);  // 3 rows each: dyn/cascade0, band0, band1, band2
float4 cHdLight       : register(c49);  // z = 1 Forever lighting, w = 1 test view (the pixel shader shows only the sun
                                        // after the shadow); x, y unused
float4 cHdLight2      : register(c50);  // x = ambient strength in Forever lighting (1 = the ambient as it is)
float4 cHdHorizon     : register(c51);  // rgb = Forever's horizon ambient / its sky ambient, per channel
float4 cHdGround      : register(c52);  // rgb = Forever's ground ambient / its sky ambient, per channel
float4 cHdCamera      : register(c53);  // HD lights: xyz = camera world position (the origin of the light positions),
                                        // w = 1 when the point lights are done per pixel (left out here)

// x = term from world X, y = term from world Y. The UV a pixel shader samples with is .yx
float2 LayerTerms(float3 p, int k)
{
    return (p.xy - cChunkPos.xy) * cTexScale[k].xy + cTexOffset[k].xy;
}

float3 PointLight(int i, float3 vp, float3 n)
{
    float3 lp = cPointLight[3 * i].x * cWorldView[0].xyz + cPointLight[3 * i].y * cWorldView[1].xyz + cPointLight[3 * i].z * cWorldView[2].xyz;
    float3 l = lp - vp;
    float dist = length(l);
    float3 att = cPointLight[3 * i + 2].xyz;
    return saturate(dot(l / dist, n)) * cPointLight[3 * i + 1].xyz / (att.x + att.y * dist + att.z * dist * dist);
}

void main(
    float3 iPos : POSITION0,
    float3 iNormal : NORMAL0,
#if VERTEX_COLOR
    float4 iColor : COLOR0,
#endif
    out float4 oPos : POSITION0,
    out float4 oDiffuse : COLOR0,
    out float4 oSpecular : COLOR1,
    out float4 oTex0 : TEXCOORD0,
#if SHADOW
#if LAYERS == 1
    out float2 oTex1 : TEXCOORD1,
#else
    out float4 oTex1 : TEXCOORD1,
#endif
#if LAYERS == 3
    out float2 oTex2 : TEXCOORD2,
#endif
#if LAYERS == 4
    out float4 oTex2 : TEXCOORD2,
#endif
    out float4 oShadow0 : TEXCOORD4,
    out float4 oShadow1 : TEXCOORD5,
    out float4 oShadow2 : TEXCOORD6,
#else
    out float2 oTex1 : TEXCOORD1,
#if LAYERS >= 2
    out float2 oTex2 : TEXCOORD2,
#endif
#if LAYERS >= 3
    out float2 oTex3 : TEXCOORD3,
#endif
#if LAYERS >= 4
    out float2 oTex4 : TEXCOORD4,
#endif
#endif
    out float4 oHdPos : HD_POS,
    out float4 oDirect : TEXCOORD7) // HD: no FOG output (see the top)
{
    float4 vp = iPos.x * cWorldView[0] + iPos.y * cWorldView[1] + iPos.z * cWorldView[2] + cWorldView[3];
    oPos = vp.x * cProj[0] + vp.y * cProj[1] + vp.z * cProj[2] + vp.w * cProj[3];
    float fog = min(pow(max(vp.z * cFog.x + cFog.y, 0.0), cFog.z), 1.0); // HD: goes out in TEXCOORD7.w only

    // normal: worldView 3x3, NOT renormalised
    float3 n = iNormal.x * cWorldView[0].xyz + iNormal.y * cWorldView[1].xyz + iNormal.z * cWorldView[2].xyz;

#if VERTEX_COLOR
    float3 shade = iColor.xyz;
#else
    float3 shade = float3(0.5, 0.5, 0.5);
#endif
    float3 points = float3(0.0, 0.0, 0.0);
#if POINT_LIGHTS
    points = points + PointLight(0, vp.xyz, n);
    points = points + PointLight(1, vp.xyz, n);
    points = points + PointLight(2, vp.xyz, n);
#endif
    // HD lights: camera-relative world position and the unit world normal for the pixel shader
    float3 nw = normalize(iNormal);
    oHdPos = float4(iPos - cHdCamera.xyz, nw.x);
    oDirect = float4(0.0, 0.0, 0.0, fog);
    [branch] if (cHdLight.z > 0.5) {
        // HD: Forever. Ambient: horizon colour for a wall, sky colour for flat ground facing up, ground colour for an
        // overhang; the client's ambient is the sky colour (Forever's when hdForeverLight is 1, so horizon and ground
        // are then Forever's own colours). Then a wrap of the sun over it, and the sun on its own.
        float3 nn = normalize(n);
        float up = dot(normalize(cWorldView[2].xyz), nn);
        float nl = dot(cSunDir.xyz, nn);
        float3 sky = cSunAmbient.xyz * cHdLight2.x;
        float3 horizon = sky * cHdHorizon.xyz;
        float3 hemi = horizon + (sky - horizon) * saturate(up) + (sky * cHdGround.xyz - horizon) * saturate(-up);
        float3 ambient = hemi * (nl * 0.2 + 0.9);
        if (cHdCamera.w < 0.5) ambient = ambient + points;
        float3 direct = cSunDiffuse.xyz * saturate(nl);
        // like 3.3.5, a channel never goes over 1 (both parts are scaled down together)
        float3 fit = 1.0 / max(ambient + direct, 1.0);
        oDiffuse = float4(ambient * fit * shade, 1.0);
        oDirect = float4(direct * fit * shade, fog);
    } else {
        float3 light = cSunDiffuse.xyz * saturate(dot(cSunDir.xyz, n)) + cSunAmbient.xyz;
        light = light + points;
        light = min(light, 1.0);
        oDiffuse = float4(light * shade, 1.0);
    }

#if SPECULAR
    float3 h = normalize(normalize(-vp.xyz) + cSunDir.xyz);
    oSpecular = float4(pow(max(dot(h, n), 0.0), cSunSpecular.w) * cSunSpecular.xyz, 1.0);
#else
    oSpecular = float4(0.0, 0.0, 0.0, 0.0);
#endif

#if ENV_LAYER0
    float3 r = vp.xyz - 2.0 * dot(vp.xyz, n) * n;
    oTex0 = float4(r.x * cViewT[0].xzy + r.y * cViewT[1].xzy + r.z * cViewT[2].xzy, nw.y);
#else
    oTex0 = float4(LayerTerms(iPos, 0).yx, 0.0, nw.y);
#endif

#if SHADOW
#if LAYERS == 1
    oTex1 = LayerTerms(iPos, 1).yx;
#else
    oTex1 = float4(LayerTerms(iPos, 1).yx, LayerTerms(iPos, 2));
#endif
#if LAYERS == 3
    oTex2 = LayerTerms(iPos, 3).yx;
#endif
#if LAYERS == 4
    oTex2 = float4(LayerTerms(iPos, 3).yx, LayerTerms(iPos, 4));
#endif
    // HD: TEXCOORD3 (view position) is not written: no terrain pixel shader reads it, and ps_3_0 reads only 10
    // inputs, so its slot holds HD_POS (TEXCOORD7 took the FOG slot's place).
    oShadow0 = float4(dot(vp, cShadowMtx[0]), dot(vp, cShadowMtx[1]), dot(vp, cShadowMtx[2]), dot(vp, cShadowMtx[9]));
    oShadow1 = float4(dot(vp, cShadowMtx[3]), dot(vp, cShadowMtx[4]), dot(vp, cShadowMtx[5]), dot(vp, cShadowMtx[10]));
    oShadow2 = float4(dot(vp, cShadowMtx[6]), dot(vp, cShadowMtx[7]), dot(vp, cShadowMtx[8]), dot(vp, cShadowMtx[11]));
#else
    oTex1 = LayerTerms(iPos, 1).yx;
#if LAYERS >= 2
    oTex2 = LayerTerms(iPos, 2).yx;
#endif
#if LAYERS >= 3
    oTex3 = LayerTerms(iPos, 3).yx;
#endif
#if LAYERS >= 4
    oTex4 = LayerTerms(iPos, 4).yx;
#endif
#endif
}
)HLSL";

    // Terrain pixel shaders Terrain1 (32), Terrain3 and Terrain3_pcf (96 each), same method (224 permutations checked).
    const char kPixelShader[] = R"HLSL(
// Waystone HD terrain pixel shader (ps_3_0). Replaces
//   Terrain1      (SHADOW_MODE 0)    perm = 16*UNLIT_MASK + 8*BIG_ALPHA + 2*(LAYERS-1) + ENV_LAYER0
//   Terrain3[_pcf](SHADOW_MODE 1..3) perm = 48*UNLIT_MASK + 24*BIG_ALPHA + 8*(SHADOW_MODE-1) + 2*(LAYERS-1) + ENV_LAYER0
// PCF = 1 selects the Terrain3_pcf variant (hardware depth compare); 0 the R32F manual compare.
// TEXCOORD7 comes from the HD vertex shader: rgb = the sun's light, w = the fog. With c16.x = 1 (Forever lighting) the sun is
// cut fully by the shadow and the ambient (COLOR0) is left alone. c16.x = 0 (and rgb = 0) gives the 3.3.5 result exactly.
// HD lights (hdLights): with c16.z = n > 0 the pixel shader adds n point lights (c17.., 4 registers each) the way WoW
// Forever's light buffer does (lightbufferomni.bls), per pixel, from HD_POS = (position - camera, normal.x) and
// TEXCOORD0.w = normal.y. Forever adds its light buffer in linear space: colour = albedo * (light^2.2 + buffer)^(1/2.2).
// c16.z = 0 leaves the result exactly as without the lights.
#ifndef LAYERS
#define LAYERS 1
#endif
#ifndef ENV_LAYER0
#define ENV_LAYER0 0
#endif
#ifndef BIG_ALPHA
#define BIG_ALPHA 0
#endif
#ifndef UNLIT_MASK
#define UNLIT_MASK 0
#endif
#ifndef SHADOW_MODE
#define SHADOW_MODE 0
#endif
#ifndef PCF
#define PCF 0
#endif

#define SAMPLE_BLEND (LAYERS > 1 || BIG_ALPHA || SHADOW_MODE < 2)
// HD lights: where the vertex shader puts the position (TEXCOORD3 is free with a shadow, TEXCOORD5 without)
#if SHADOW_MODE >= 1
#define HD_POS TEXCOORD3
#else
#define HD_POS TEXCOORD5
#endif
#define HD_MAX_LIGHTS 4

float4 cLayerLit     : register(c1);  // x,y,z,w = layer 0..3; 0 means layer is unlit (MCLY flag 0x80)
float4 cFogColor     : register(c2);
float4 cPcfOffset[8] : register(c3);  // xy = PCF tap offsets in shadow-map UV (c3..c10)
float4 cHdPs         : register(c16); // HD: x = 1 Forever lighting, y = 1 test view (sun after shadow), z = point light count
                                      // (0..4), w = 1 lights test view (only the point lights)
float4 cHdLight[16]  : register(c17); // HD lights, 4 registers each (HdLights.h):
                                      //  0: xyz = position - camera (world, yards), w = 1 legacy falloff 1/(0.7 d + 0.03 d^2)
                                      //  1: rgb = colour A (near), w = attenuation start         (windowed falloff)
                                      //  2: rgb = colour B (far),  w = 1 / (attenuation end - start)
                                      //  3: x, y = colour blend t = saturate(d * x + y), z = cut radius, w = 1 / cut fade width

#if ENV_LAYER0
samplerCUBE sLayer0  : register(s0);
#else
sampler2D sLayer0    : register(s0);
#endif
#if LAYERS == 1
sampler2D sBlend     : register(s1);
#endif
#if LAYERS >= 2
sampler2D sLayer1    : register(s1);
#endif
#if LAYERS == 2
sampler2D sBlend     : register(s2);
#endif
#if LAYERS >= 3
sampler2D sLayer2    : register(s2);
#endif
#if LAYERS == 3
sampler2D sBlend     : register(s3);
#endif
#if LAYERS >= 4
sampler2D sLayer3    : register(s3);
sampler2D sBlend     : register(s4);
#endif
#if SHADOW_MODE >= 1
sampler2D sShadow0   : register(s5);  // m_extDynShadow (PC/NPC shadow, or near cascade in mode 3)
#endif
#if SHADOW_MODE >= 2
sampler2D sShadow1   : register(s6);  // extShadowBand[0]
sampler2D sShadow2   : register(s7);  // extShadowBand[1]
sampler2D sShadow3   : register(s8);  // extShadowBand[2]
#endif

// one shadow tap at shadow-map UV uv for light-space depth z: 1 = lit, 0 = shadowed
float Tap(sampler2D s, float2 uv, float z)
{
#if PCF
    return tex2Dlod(s, float4(uv, z, 0.0)).x;
#else
    return step(z, tex2Dlod(s, float4(uv, 0.0, 0.0)).x);
#endif
}

// centre + offsets 0,2,4,6 ; returns the SUM (0..5)
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

// centre + all 8 offsets ; returns the SUM (0..9)
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

// edge fades: 1 inside, 0 at |coord| >= 0.99 ; dyn fades over 0.70..0.99, band2 over 0.90..0.99
float EdgeFade(float2 c)
{
    return saturate(max(abs(c.x), abs(c.y)) * -3.44827580 + 3.41379309);
}
float EdgeFade2(float2 c)
{
    return saturate(max(abs(c.x), abs(c.y)) * -11.1111107 + 11.0);
}

// One point light at camera-relative world position p, unit normal n: WoW Forever's lightbufferomni.bls maths
// (permutation 1 = legacy falloff, permutation 0 = windowed falloff with a colour gradient), times our cut window.
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

// Forever: the light buffer is added in linear space to the light, then back (gamma path of terrain.bls)
float3 HdCombine(float3 light, float3 buffer)
{
    return pow(pow(max(light, 0.0), 2.2) + buffer, 1.0 / 2.2);
}

void main(
    float3 iDiffuse : COLOR0,
    float3 iSpecular : COLOR1,
    float4 iTex0 : TEXCOORD0,
#if SHADOW_MODE >= 1
#if LAYERS == 1
#if SAMPLE_BLEND
    float2 iTex1 : TEXCOORD1,
#endif
#else
    float4 iTex1 : TEXCOORD1,
#endif
#if LAYERS == 3
    float2 iTex2 : TEXCOORD2,
#endif
#if LAYERS == 4
    float4 iTex2 : TEXCOORD2,
#endif
#if SHADOW_MODE == 1
    float3 iShadow0 : TEXCOORD4,
#else
    float4 iShadow0 : TEXCOORD4,
    float4 iShadow1 : TEXCOORD5,
    float4 iShadow2 : TEXCOORD6,
#endif
#else
    float2 iTex1 : TEXCOORD1,
#if LAYERS >= 2
    float2 iTex2 : TEXCOORD2,
#endif
#if LAYERS >= 3
    float2 iTex3 : TEXCOORD3,
#endif
#if LAYERS >= 4
    float2 iTex4 : TEXCOORD4,
#endif
#endif
    float4 iHdPos : HD_POS,
    float4 iDirect : TEXCOORD7,
    out float4 oColor : COLOR0)
{
    // ---- texture coordinate sets (k = 0..LAYERS; set LAYERS is the blend/alpha texture)
#if SHADOW_MODE >= 1
#if SAMPLE_BLEND
    float2 uv1 = iTex1.xy;
#endif
#if LAYERS >= 2
    float2 uv2 = iTex1.wz;
#endif
#if LAYERS >= 3
    float2 uv3 = iTex2.xy;
#endif
#if LAYERS >= 4
    float2 uv4 = iTex2.wz;
#endif
#else
    float2 uv1 = iTex1;
#if LAYERS >= 2
    float2 uv2 = iTex2;
#endif
#if LAYERS >= 3
    float2 uv3 = iTex3;
#endif
#if LAYERS >= 4
    float2 uv4 = iTex4;
#endif
#endif

    // ---- layer textures
#if ENV_LAYER0
    float4 t0 = texCUBE(sLayer0, iTex0.xyz);
#else
    float4 t0 = tex2D(sLayer0, iTex0.xy);
#endif
#if LAYERS >= 2
    float4 t1 = tex2D(sLayer1, uv1);
#endif
#if LAYERS >= 3
    float4 t2 = tex2D(sLayer2, uv2);
#endif
#if LAYERS >= 4
    float4 t3 = tex2D(sLayer3, uv3);
#endif
#if SAMPLE_BLEND
#if LAYERS == 1
    float4 a = tex2D(sBlend, uv1);
#endif
#if LAYERS == 2
    float4 a = tex2D(sBlend, uv2);
#endif
#if LAYERS == 3
    float4 a = tex2D(sBlend, uv3);
#endif
#if LAYERS == 4
    float4 a = tex2D(sBlend, uv4);
#endif
#endif

    // ---- shadow term S (1 = lit)
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

    // ---- HD lights: Forever's light buffer for this pixel
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

    // ---- per-layer vertex lighting (only in the unlit-mask variants)
#if UNLIT_MASK
    float3 lit2 = (iDiffuse + iDirect.xyz * S) * 2.0;
    [branch] if (cHdPs.z > 0.5) lit2 = HdCombine(lit2, buffer);
    if (cLayerLit.x != 0.0) t0.xyz = t0.xyz * lit2;
#if LAYERS >= 2
    if (cLayerLit.y != 0.0) t1.xyz = t1.xyz * lit2;
#endif
#if LAYERS >= 3
    if (cLayerLit.z != 0.0) t2.xyz = t2.xyz * lit2;
#endif
#if LAYERS >= 4
    if (cLayerLit.w != 0.0) t3.xyz = t3.xyz * lit2;
#endif
#endif

    // ---- layer blend (rgb = albedo, a = specular mask)
#if BIG_ALPHA
    float4 b = t0 * (1.0 - saturate(dot(a.xyz, float3(1.0, 1.0, 1.0))));
#if LAYERS >= 2
    b = b + t1 * a.x;
#endif
#if LAYERS >= 3
    b = b + t2 * a.y;
#endif
#if LAYERS >= 4
    b = b + t3 * a.z;
#endif
#else
    float4 b = t0;
#if LAYERS >= 2
    b = lerp(b, t1, a.x);
#endif
#if LAYERS >= 3
    b = lerp(b, t2, a.y);
#endif
#if LAYERS >= 4
    b = lerp(b, t3, a.z);
#endif
#endif

    // ---- combine. f = what the shadow leaves of COLOR0: 3.3.5 keeps 70% in shadow, Forever keeps the ambient whole.
    float3 spec = b.w * iSpecular;
    float f = lerp(S * 0.3 + 0.7, 1.0, cHdPs.x);
#if UNLIT_MASK
    float3 col = b.xyz * f + S * spec;
#else
    float3 col = b.xyz * (iDiffuse * f + iDirect.xyz * S);
    col = col + col;
    [branch] if (cHdPs.z > 0.5) col = b.xyz * HdCombine((iDiffuse * f + iDirect.xyz * S) * 2.0, buffer);
    col = spec * S + col;
#endif
    float fog = iDirect.w;
    oColor = float4((col - cFogColor.xyz) * fog + cFogColor.xyz, 1.0);
    // HD: test view (hdTerrainLight 3): only the sun after the shadow, no texture, no ambient, no fog
    if (cHdPs.y > 0.5) oColor = float4(iDirect.xyz * S * 2.0, 1.0);
    // HD: lights test view (hdLights 3): only the point lights (gamma encoded), no texture, no fog
    if (cHdPs.w > 0.5) oColor = float4(pow(buffer, 1.0 / 2.2), 1.0);
}
)HLSL";

    typedef HRESULT(WINAPI* D3DCompileFn)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);

    // Families of compiled code: 0 = vertex (128), 1 = Terrain1 (32), 2 = Terrain3 (96), 3 = Terrain3_pcf (96)
    const int32_t kFamilyCount[4] = { 128, 32, 96, 96 };
    const int32_t kOursMark = 0x4844544C;  // 'HDTL' in CGxShader::unused_0028: the shader holds our code
    const int32_t kUnknownMark = 0x4844544E;  // 'HDTN': our code for it has no translated module: not asked again this run

    enum { StateIdle, StateCompiling, StateReady, StateFailed };

    std::atomic<int32_t> s_state(StateIdle);
    std::vector<uint8_t> s_code[4][128];    // written by the compile thread before s_state = StateReady
    char s_threadInfo[260] = "";            // written by the compile thread before s_state changes to Ready/Failed
    int32_t s_inUse = 0;                    // shaders holding our code right now (shown in hdTerrainInfo)
    int32_t s_unknown = 0;                  // permutations asked for whose translated module is missing
    char s_shownInfo[300] = "";

    std::map<CGxShader*, std::vector<uint8_t>> s_original;  // Blizzard's code of each shader we changed

    CVar* s_light = nullptr;
    CVar* s_hemi = nullptr;
    CVar* s_ambient = nullptr;
    CVar* s_info = nullptr;

    float Clamp(float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Forever's sky, horizon and ground ambient for one light: clear weather blended to storm like the fog does.
    bool LightAmbients(const LightRec* light, int32_t time, float storm, float (*amb)[3]) {
        if (!HD::ForeverAmbientsAt(light, 0, time, amb[0], amb[1], amb[2])) return false;
        float stormAmb[3][3];
        if (storm > 0.0f && HD::ForeverAmbientsAt(light, 2, time, stormAmb[0], stormAmb[1], stormAmb[2])) {
            for (int32_t k = 0; k < 3; k++) {
                for (int32_t c = 0; c < 3; c++) amb[k][c] += (stormAmb[k][c] - amb[k][c]) * storm;
            }
        }
        return true;
    }

    // The horizon / sky and ground / sky ratios (per channel) of Forever's ambients where the camera is: the zone light,
    // then the nearby area lights blended in by distance, nearest last (the fog's walk). The shader multiplies the
    // client's ambient (the sky colour) by them. A light Forever has no setting for counts as horizon = ground = sky;
    // one Forever does not have at all is skipped (this server's own lights, HD::ForeverHasLight).
    void CurrentHemisphere(float* horizon, float* ground) {
        float amb[3][3] = { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
        const DayNight::DNInfo& info = DayNight::g_dnInfo;
        const TSGrowableArray<const LightRec*>& lights = DayNight::g_areaLights;
        if (lights.m_count) {
            int32_t time = static_cast<int32_t>(info.m_dayProgression * 2880.0f);
            float storm = info.m_stormBlend;
            float none[3][3] = { { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f } };
            if (!LightAmbients(lights.m_data[0], time, storm, amb)) memcpy(amb, none, sizeof(amb));

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
                if (weight <= 0.0f || !HD::ForeverHasLight(light)) continue;
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
                float other[3][3];
                if (!LightAmbients(nearLights[i].light, time, storm, other)) memcpy(other, none, sizeof(other));
                float w = nearLights[i].weight;
                for (int32_t k = 0; k < 3; k++) {
                    for (int32_t c = 0; c < 3; c++) amb[k][c] += (other[k][c] - amb[k][c]) * w;
                }
            }
        }
        for (int32_t c = 0; c < 3; c++) {
            // a channel Forever leaves at 0 in the sky colour has no ratio: keep the sky's (1)
            horizon[c] = amb[0][c] > 0.001f ? Clamp(amb[1][c] / amb[0][c], 0.0f, 4.0f) : 1.0f;
            ground[c] = amb[0][c] > 0.001f ? Clamp(amb[2][c] / amb[0][c], 0.0f, 4.0f) : 1.0f;
        }
    }

    // The light walk is per frame, not per chunk: redo it at most every 50 ms.
    ULONGLONG s_hemiTime = 0;
    float s_horizonRatio[3] = { 1.0f, 1.0f, 1.0f };
    float s_groundRatio[3] = { 1.0f, 1.0f, 1.0f };

    void RefreshHemisphere() {
        ULONGLONG now = GetTickCount64();
        if (s_hemiTime && now - s_hemiTime < 50) return;
        s_hemiTime = now;
        int32_t hemi = s_hemi ? s_hemi->m_intValue : 1;
        if (hemi == 2) return;  // test: keep the last ratios
        CurrentHemisphere(s_horizonRatio, s_groundRatio);
        if (hemi == 0) {
            for (int32_t c = 0; c < 3; c++) s_horizonRatio[c] = s_groundRatio[c] = 1.0f;
        }
    }

    void Decode(int32_t family, int32_t p, D3D_SHADER_MACRO* macros, char (*values)[4]) {
        const char* names[6];
        int32_t v[6];
        int32_t n = 6;
        if (family == 0) {
            names[0] = "SHADOW";       v[0] = p & 1;
            names[1] = "ENV_LAYER0";   v[1] = (p >> 1) & 1;
            names[2] = "VERTEX_COLOR"; v[2] = (p >> 2) & 1;
            names[3] = "SPECULAR";     v[3] = (p >> 3) & 1;
            names[4] = "LAYERS";       v[4] = ((p >> 4) & 3) + 1;
            names[5] = "POINT_LIGHTS"; v[5] = (p >> 6) & 1;
        } else if (family == 1) {
            names[0] = "ENV_LAYER0";   v[0] = p & 1;
            names[1] = "LAYERS";       v[1] = ((p >> 1) & 3) + 1;
            names[2] = "BIG_ALPHA";    v[2] = (p >> 3) & 1;
            names[3] = "UNLIT_MASK";   v[3] = (p >> 4) & 1;
            names[4] = "SHADOW_MODE";  v[4] = 0;
            names[5] = "PCF";          v[5] = 0;
        } else {
            names[0] = "ENV_LAYER0";   v[0] = p & 1;
            names[1] = "LAYERS";       v[1] = ((p >> 1) & 3) + 1;
            names[2] = "SHADOW_MODE";  v[2] = (p % 24) / 8 + 1;
            names[3] = "BIG_ALPHA";    v[3] = (p % 48) / 24;
            names[4] = "UNLIT_MASK";   v[4] = p / 48;
            names[5] = "PCF";          v[5] = family == 3;
        }
        for (int32_t i = 0; i < n; i++) {
            snprintf(values[i], 4, "%d", v[i]);
            macros[i].Name = names[i];
            macros[i].Definition = values[i];
        }
        macros[n].Name = nullptr;
        macros[n].Definition = nullptr;
    }

    // Compile threads: every permutation, all or nothing, spread over the cores (352 take about a minute on one; the
    // compiler is thread-safe). The translated modules are made from this same system d3dcompiler_47's output
    // (Tools/HdMaterials), so the md5s match on this machine.
    void CompileAll() {
        ULONGLONG start = GetTickCount64();
        HMODULE module = LoadLibraryA("d3dcompiler_47.dll");
        D3DCompileFn compile = module ? reinterpret_cast<D3DCompileFn>(GetProcAddress(module, "D3DCompile")) : nullptr;
        if (!compile) {
            snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: d3dcompiler_47.dll not found, Blizzard shaders kept");
            s_state.store(StateFailed);
            return;
        }
        static const char* kFamilyName[4] = { "Terrain (vertex)", "Terrain1", "Terrain3", "Terrain3_pcf" };
        const int32_t jobs = kFamilyCount[0] + kFamilyCount[1] + kFamilyCount[2] + kFamilyCount[3];
        std::atomic<int32_t> next(0);
        std::atomic<bool> failed(false);
        std::mutex errorLock;
        auto worker = [&]() {
            for (int32_t job = next++; job < jobs && !failed.load(); job = next++) {
                int32_t family = 0;
                int32_t p = job;
                while (p >= kFamilyCount[family]) p -= kFamilyCount[family++];
                D3D_SHADER_MACRO macros[7];
                char values[6][4];
                Decode(family, p, macros, values);
                const char* source = family == 0 ? kVertexShader : kPixelShader;
                ID3DBlob* code = nullptr;
                ID3DBlob* errors = nullptr;
                HRESULT hr = compile(source, strlen(source), family == 0 ? "HdTerrainVS" : "HdTerrainPS", macros, nullptr, "main",
                                     family == 0 ? "vs_3_0" : "ps_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors);
                if (FAILED(hr) || !code) {
                    std::lock_guard<std::mutex> lock(errorLock);
                    if (!failed.exchange(true)) {
                        snprintf(s_threadInfo, sizeof(s_threadInfo), "ERROR: %s %d: %s", kFamilyName[family], p,
                                 errors ? static_cast<const char*>(errors->GetBufferPointer()) : "unknown");
                    }
                } else {
                    const uint8_t* bytes = static_cast<const uint8_t*>(code->GetBufferPointer());
                    s_code[family][p].assign(bytes, bytes + code->GetBufferSize());
                }
                if (errors) errors->Release();
                if (code) code->Release();
            }
        };
        const unsigned cores = std::thread::hardware_concurrency();
        std::vector<std::thread> threads(cores > 2 ? cores - 1 : 1);
        for (std::thread& t : threads) t = std::thread(worker);
        for (std::thread& t : threads) t.join();
        if (failed.load()) {
            s_state.store(StateFailed);
            return;
        }
        snprintf(s_threadInfo, sizeof(s_threadInfo), "%d shaders compiled in %u ms on %u threads", jobs,
                 static_cast<uint32_t>(GetTickCount64() - start), static_cast<uint32_t>(threads.size()));
        s_state.store(StateReady);
    }

    void ShowState(int32_t state) {
        if (!s_info) return;
        char text[300];
        if (state == StateCompiling) snprintf(text, sizeof(text), "compiling (Blizzard shaders until done)");
        else if (state == StateIdle) snprintf(text, sizeof(text), "not started");
        else if (state == StateReady && s_unknown)
            snprintf(text, sizeof(text), "%s, %d in use, %d not translated yet (run Tools/HdMaterials/install_hd_shaders.py, restart)",
                     s_threadInfo, s_inUse, s_unknown);
        else if (state == StateReady) snprintf(text, sizeof(text), "%s, %d in use", s_threadInfo, s_inUse);
        else snprintf(text, sizeof(text), "%s", s_threadInfo);
        if (!strcmp(text, s_shownInfo)) return;
        snprintf(s_shownInfo, sizeof(s_shownInfo), "%s", text);
        s_info->Set(text, true, false, false, false);
    }
}

void HD::RegisterTerrainCVars() {
    if (s_light) return;
    s_light = OptionCVars::Register("hdTerrainLight", "HD: 1 = WoW Forever terrain lighting, 2 = our shaders with 3.3.5 lighting, 3 = test view (sun after shadow only), 0 = Blizzard shaders", "1");
    s_ambient = OptionCVars::Register("hdLightAmbient", "HD: ambient strength on the ground in Forever lighting, 1 = Forever's (test only)", "1");
    s_hemi = OptionCVars::Register("hdTerrainHemi", "HD test: 1 = Forever hemisphere ambient, 0 = none (horizon = ground = sky), 2 = ratios frozen", "1");
    s_info = CVar::Register("hdTerrainInfo", "HD: hdTerrainLight status (read only)", 0, "not started", nullptr, CATEGORY_GRAPHICS, false, nullptr, false);
}

void HD::TerrainShadersStart() {
    int32_t expected = StateIdle;
    if (!s_state.compare_exchange_strong(expected, StateCompiling)) return;
    std::thread(CompileAll).detach();
}

void HD::TerrainShader(CGxShader* shader, int32_t kind, int32_t permutation) {
    if (!shader) return;
    int32_t state = s_state.load();
    ShowState(state);

    bool want = state == StateReady && Visuals::Current() == Visuals::AzerothRemastered && OptionCVars::Bool(s_light, true);
    static bool s_offered = false;
    if (want && !s_offered) {
        // Once: every permutation the device lacks goes to hd-pending, so one install covers the whole terrain.
        s_offered = true;
        char name[48];
        for (int32_t family = 0; family < 4; family++) {
            for (int32_t p = 0; p < kFamilyCount[family]; p++) {
                snprintf(name, sizeof(name), "HdTerrain-%d-%d", family, p);
                HD::ShaderUsable(s_code[family][p].data(), s_code[family][p].size(), name);
            }
        }
    }
    bool ours = shader->unused_0028 == kOursMark;
    if (want == ours || (want && shader->unused_0028 == kUnknownMark)) return;

    if (!want) {
        auto it = s_original.find(shader);
        if (it != s_original.end() && !it->second.empty()) HD::ShaderSetCode(shader, it->second.data(), static_cast<uint32_t>(it->second.size()));
        shader->unused_0028 = 0;
        s_inUse--;
        ShowState(state);
        return;
    }

    int32_t family = kind == HD::TerrainVertex ? 0 : (kind == HD::TerrainPixel1 ? 1 : 2);
    if (family == 2 && shader->m_key.m_str && strstr(shader->m_key.m_str, "_pcf")) family = 3;
    if (permutation < 0 || permutation >= kFamilyCount[family]) return;

    // Only over the shader model 3 code.
    uint32_t version = family == 0 ? 0xFFFE0300u : 0xFFFF0300u;
    if (shader->m_code.Count() < 4 || *reinterpret_cast<const uint32_t*>(shader->m_code.m_data) != version) return;
    const std::vector<uint8_t>& code = s_code[family][permutation];
    if (code.size() < 4) return;
    char name[48];
    snprintf(name, sizeof(name), "HdTerrain-%d-%d", family, permutation);
    if (!HD::ShaderUsable(code.data(), code.size(), name)) {
        // Vulkan without its translated module: Blizzard's shader, until the install tool translates the pending code.
        shader->unused_0028 = kUnknownMark;
        s_unknown++;
        ShowState(state);
        return;
    }

    s_original[shader].assign(shader->m_code.m_data, shader->m_code.m_data + shader->m_code.Count());
    HD::ShaderSetCode(shader, code.data(), static_cast<uint32_t>(code.size()));
    shader->unused_0028 = kOursMark;
    s_inUse++;
    ShowState(state);
}

void HD::TerrainVertexConstants() {
    if (!g_theGxDevicePtr || Visuals::Current() != Visuals::AzerothRemastered) return;
    RefreshHemisphere();

    float constants[20] = {};
    int32_t mode = OptionCVars::Int(s_light, 0, 3, 1);
    constants[2] = mode == 1 || mode == 3 ? 1.0f : 0.0f;  // Forever lighting
    constants[3] = mode == 3 ? 1.0f : 0.0f;              // test view
    constants[4] = Clamp(s_ambient ? s_ambient->m_floatValue : 1.0f, 0.0f, 2.0f);
    for (int32_t c = 0; c < 3; c++) {
        constants[8 + c] = s_horizonRatio[c];
        constants[12 + c] = s_groundRatio[c];
    }
    // c53: the origin of the per-pixel light positions and whether the pixel shader does them (hdLights, not ported
    // yet: the vertex shader keeps the client's 3 per-vertex lights).
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 49, constants, 5);
    if (mode == 0) return;  // Blizzard's shaders read no pixel constant of ours

    // pixel c16 = (Forever lighting, test view, point light count, lights test view)
    float pixel[4] = { constants[2], constants[3], 0.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 16, pixel, 1);
}

void HD::TerrainHemisphere(float* horizon, float* ground) {
    RefreshHemisphere();
    for (int32_t c = 0; c < 3; c++) {
        horizon[c] = s_horizonRatio[c];
        ground[c] = s_groundRatio[c];
    }
}

float HD::LightAmbientStrength() {
    return s_ambient ? Clamp(s_ambient->m_floatValue, 0.0f, 2.0f) : 1.0f;
}
