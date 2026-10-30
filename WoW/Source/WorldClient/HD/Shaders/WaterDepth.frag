// SPDX-License-Identifier: GPL-3.0-or-later
// The water's scene depth: the world's depth (CopyDepth, 0..1 over the viewport's depth range) as linear view depth in
// yards in an R32F target of the same size, before the liquids are drawn. The donor's HdWater depth shader (HLSL).
#version 450
layout(set = 0, binding = 0) uniform sampler2D DepthTex;
layout(std140, set = 0, binding = 16) uniform Constants { vec4 c[2]; };
layout(location = 0) out vec4 colour;

#define cScale c[0]     // depth scale A, depth offset B (the world viewport's depth range back to 0..1)
#define cProj c[1]      // projection c2, -d2, sky threshold, depth written where nothing was drawn (yards)

void main()
{
    float raw = texelFetch(DepthTex, ivec2(gl_FragCoord.xy), 0).r;
    float z = clamp(raw * cScale.x + cScale.y, 0.0, 1.0);
    // view depth: z = c2 + d2 / zv  ->  zv = -d2 / (c2 - z)
    float zv = cProj.y / max(cProj.x - z, 1e-7);
    if (raw >= cProj.z) zv = cProj.w;
    colour = vec4(min(zv, cProj.w), 0.0, 0.0, 1.0);
}
