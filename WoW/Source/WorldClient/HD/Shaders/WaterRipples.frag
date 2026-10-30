// SPDX-License-Identifier: GPL-3.0-or-later
// The water's ripples: every live 3.3.5 ripple (units walking, swimming, turning or entering the water) adds a ring of small
// waves to a top-down 512 x 512 texture of 32 x 32 yards around the camera, which the water pixel shader reads. The donor's
// HdWaterRipples HLSL (one square per ripple, additive), as one pass over the whole target looping over the ripples.
// Signed slopes in an 8-bit texture: r = +dh/dx, g = -dh/dx, b = +dh/dy, a = -dh/dy (x 2, calm water = 0).
#version 450
layout(std140, set = 0, binding = 16) uniform Constants { vec4 c[256]; };
layout(location = 0) out vec4 colour;

// c[0] = strength, half the area (yards), ripple count; c[1 + 2 i] = ripple i's centre from the area's centre (yards),
// ring radius, alpha; c[2 + 2 i] = packet width, 1 = directional (a wake), the unit's forward direction
void main()
{
    const float area = 32.0;
    const float size = 512.0;
    vec2 a = (gl_FragCoord.xy / size - 0.5) * area;   // this texel from the area's centre (yards)
    float strength = c[0].x;
    float halfArea = c[0].y;
    int count = int(c[0].z);
    float edge = clamp((halfArea - max(abs(a.x), abs(a.y))) * 0.5, 0.0, 1.0);   // fade out at the area's edge
    vec4 sum = vec4(0.0);
    for (int i = 0; i < count; ++i) {
        vec4 r0 = c[1 + 2 * i];
        vec4 r1 = c[2 + 2 * i];
        vec2 d = a - r0.xy;                  // from the ripple's centre
        float radius = r0.z;
        float w = r1.x;
        if (max(abs(d.x), abs(d.y)) > radius + 3.0 * w) continue;   // the square that covers the ring and its packet
        float r = max(length(d), 1e-4);
        float x = (r - radius) / w;          // across the packet, in packet widths
        float env = exp2(-1.4427 * x * x) * r0.w;
        // directional ripples (moving): mostly ahead and to the sides, the trailing arcs make the wake
        env *= mix(1.0, clamp(0.35 + 0.65 * dot(d / r, r1.zw), 0.0, 1.0), r1.y);
        float k = 6.2831853 / (w * 1.6);     // about two crests per packet
        float s = env * (-2.0 * x / w * cos(k * (r - radius)) - k * sin(k * (r - radius)));
        vec2 slope = s * d / r * (strength * 0.02 * 2.0 * edge);
        sum += vec4(max(slope.x, 0.0), max(-slope.x, 0.0), max(slope.y, 0.0), max(-slope.y, 0.0));
    }
    colour = clamp(sum, 0.0, 1.0);
}
