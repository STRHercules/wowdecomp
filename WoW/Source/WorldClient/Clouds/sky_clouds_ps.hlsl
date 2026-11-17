#include "sky_common.hlsli"
sampler2D tPanoClouds : register(s3, space0);
sampler2D tDepth : register(s4, space0);
// The clouds' march is offset in a 4x4 ordered pattern (sky_panorama_ps.hlsl). A tent of nine bilinear taps a texel
// apart, weighted (2 - |x|)(2 - |y|), covers about a 4x4 block: the pattern and the fine grain average out into soft
// cloud. The nine taps share one fraction, so their sum is the 4x4 block of texels itself, each axis weighted
// 1 - f, 2 - f, 1 + f, f (over 4): the same blur in 16 loads instead of 36. Wrapped around the horizon and clamped at
// the top and bottom, as PanoSample does.
float4 CloudSample(float2 uv)
{
    uint width = (uint)cWind.z;
    uint height = (uint)cWind.w;
    float2 p = uv * float2(width, height) - 0.5;
    float2 f = frac(p);
    int2 i = (int2)floor(p);
    int w = (int)width;
    float4 wx = float4(1 - f.x, 2 - f.x, 1 + f.x, f.x);
    float4 wy = float4(1 - f.y, 2 - f.y, 1 + f.y, f.y);
    float4 sum = 0;

    [unroll] for (int y = 0; y < 4; y++)
    {
        int row = clamp(i.y - 1 + y, 0, (int)height - 1);
        float4 across = 0;

        [unroll] for (int x = 0; x < 4; x++)
        {
            int column = i.x - 1 + x;
            column = column < 0 ? column + w : (column >= w ? column - w : column);
            across += tex2Dlod(tPanoClouds, float4((float2(column, row) + 0.5) / cWind.zw, 0, 0)) * wx[x];
        }

        sum += across * wy[y];
    }

    return sum / 16;
}

float4 main(float4 pos : SV_Position) : SV_Target
{
    if (tex2Dlod(tDepth, float4(pos.xy / cDomeView[2].xy, 0, 0)).r < cDomeView[2].z) { discard; }
    float2 uv = (pos.xy - cDomeView[0].xy) / cDomeView[0].zw;
    float2 ray = uv * cDomeView[1].xy + cDomeView[1].zw;
    float3 dir = normalize(ray.x * cDomeWorld[0].xyz + ray.y * cDomeWorld[1].xyz + cDomeWorld[2].xyz);
    float4 cloud = CloudSample(PanoUv(dir));
    if (cloud.a < 0.003) { discard; }
    return float4(Flash(cloud.rgb / cloud.a), cloud.a);
}

