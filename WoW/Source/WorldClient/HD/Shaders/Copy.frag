// A GxEffects texture of the target's size back onto the target, texel for texel.
#version 450
layout(set = 0, binding = 0) uniform sampler2D t0;
layout(location = 0) out vec4 colour;

void main()
{
    colour = texelFetch(t0, ivec2(gl_FragCoord.xy), 0);
}
