///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2016-2021, Intel Corporation 
// 
// SPDX-License-Identifier: MIT
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// XeGTAO is based on GTAO/GTSO "Jimenez et al. / Practical Real-Time Strategies for Accurate Indirect Occlusion", 
// https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf
// 
// Implementation:  Filip Strugar (filip.strugar@intel.com), Steve Mccalla <stephen.mccalla@intel.com>         (\_/)
// Version:         (see XeGTAO.h)                                                                            (='.'=)
// Details:         https://github.com/GameTechDev/XeGTAO                                                     (")_(")
//
// Version history: see XeGTAO.h
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// HD: GLSL 450 Vulkan port, FP32 arithmetic, fixed spatial noise, no bent normals.

const float XE_PI=3.14159265358979323846;
float sat(float x){return clamp(x,0.0,1.0);}vec4 sat(vec4 x){return clamp(x,0.0,1.0);}
vec4 XeEdges(float z,float l,float r,float t,float b){
    vec4 e=vec4(l,r,t,b)-z;vec2 slope=vec2(e.y-e.x,e.w-e.z)*.5;
    return sat(1.25-min(abs(e),abs(e+vec4(slope.x,-slope.x,slope.y,-slope.y)))/(max(z,1e-6)*.011));
}
float XePackEdges(vec4 e){return dot(round(sat(e)*2.9),vec4(64,16,4,1)/255.0);}
vec4 XeUnpackEdges(float x){uint v=uint(round(x*255));return vec4((v>>6)&3u,(v>>4)&3u,(v>>2)&3u,v&3u)/3.0;}
float XeDepthFilter(vec4 z,float radius,vec4 coverage){
    float farthest=max(max(z.x,z.y),max(z.z,z.w));float r=.75*radius*1.457,range=.615*r;
    vec4 weight=clamp(1.0-(farthest-z-r*(1.0-.615))/range,0,1)*coverage;
    return dot(weight,z)/max(dot(weight,vec4(1)),1e-10);
}
vec3 XeDirection(vec3 v){return dot(v,v)>1e-20?v*inversesqrt(dot(v,v)):vec3(0);}
vec3 XeNormal(vec4 edges,vec3 c,vec3 l,vec3 r,vec3 t,vec3 b){
    vec4 accepted=clamp(vec4(edges.x*edges.z,edges.z*edges.y,edges.y*edges.w,edges.w*edges.x)+.01,0,1);
    // HD: clamped image-edge neighbours coincide with c; do not normalize a zero derivative.
    vec3 L=XeDirection(l-c),R=XeDirection(r-c),T=XeDirection(t-c),B=XeDirection(b-c);
    vec3 n=accepted.x*cross(L,T)+accepted.y*cross(T,R)+accepted.z*cross(R,B)+accepted.w*cross(B,L);
    return dot(n,n)>1e-18?normalize(n):-normalize(c);
}
uint XeHilbert(uvec2 p){
    p&=uvec2(63);uint index=0;
    for(uint level=32u;level>0u;level>>=1u){uint x=(p.x&level)!=0u?1u:0u,y=(p.y&level)!=0u?1u:0u;index+=level*level*((3u*x)^y);
        if(y==0u){if(x==1u)p=uvec2(63)-p;p=p.yx;}}
    return index;
}
// HD (Claude): upstream SpatioTemporalNoise; frame 0 is the fixed pattern, temporal accumulation cycles 64 frames.
vec2 XeNoise(ivec2 p,uint frame){return fract(.5+float(XeHilbert(uvec2(p))+288u*(frame%64u))*vec2(.75487766624669276005,.56984029099805326591));}
// HD: exact acos in FP32 replaces upstream fast sqrt/acos approximation for slope accuracy.
float XeACos(float x){return acos(clamp(x,-1,1));}
