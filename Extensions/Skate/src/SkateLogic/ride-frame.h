#pragma once
#include "dynamic-world.h"
namespace rideframe {
struct Frame {uint64_t guid=0;uint32_t entry=0,display=0;float matrix[16]{};};
inline bool Valid(const Frame& f){
    if(!f.guid)return true;
    if((f.guid>>48)!=0xF120 || !f.entry || !f.display || !dynamicworld::MatrixValid(f.matrix))return false;
    for(unsigned i=0;i<3;++i){float n=0;for(unsigned j=0;j<3;++j)n+=f.matrix[i*4+j]*f.matrix[i*4+j];if(fabsf(n-1)>.002f)return false;}
    return fabsf(f.matrix[2])<.001f && fabsf(f.matrix[6])<.001f && fabsf(f.matrix[8])<.001f
        && fabsf(f.matrix[9])<.001f && fabsf(f.matrix[10]-1)<.001f;
}
inline dynamicworld::Point World(const Frame& f,dynamicworld::Point p){return f.guid?dynamicworld::Transform(f.matrix,p):p;}
inline dynamicworld::Point Local(const Frame& f,dynamicworld::Point p){
    if(!f.guid)return p;
    const auto* m=f.matrix;const float x=p.x-m[12],y=p.y-m[13],z=p.z-m[14];
    return {x*m[0]+y*m[1]+z*m[2],x*m[4]+y*m[5]+z*m[6],x*m[8]+y*m[9]+z*m[10]};
}
inline float Yaw(const Frame& f){return f.guid?atan2f(f.matrix[1],f.matrix[0]):0;}
}
