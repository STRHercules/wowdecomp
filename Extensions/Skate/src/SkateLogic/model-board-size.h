#pragma once
#include "board-placement.h"
#include <cstdint>

// Presentation only. The engine, collision capsules and Source vertices never
// receive this value. Read the uniform scale of the actual native model transform
// rather than the character's race/name or an enclosing weapon/cape AABB.
namespace modelboardsize {
inline bool UniformScale(const float* m,float& out) {
    if(!m)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    if(fabsf(m[3])>.001f || fabsf(m[7])>.001f || fabsf(m[11])>.001f || fabsf(m[15]-1)>.001f)return false;
    float lengths[3]{};
    for(unsigned i=0;i<3;++i)lengths[i]=sqrtf(m[i*4]*m[i*4]+m[i*4+1]*m[i*4+1]+m[i*4+2]*m[i*4+2]);
    const float size=(lengths[0]+lengths[1]+lengths[2])/3;
    if(!std::isfinite(size) || size<.01f || size>64)return false;
    for(float length:lengths)if(fabsf(length-size)>size*.01f)return false;
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j){
        const float dot=m[i*4]*m[j*4]+m[i*4+1]*m[j*4+1]+m[i*4+2]*m[j*4+2];
        if(fabsf(dot)>size*size*.01f)return false;
    }
    out=fabsf(size-1)<.00001f?1:size;return true;
}
struct State {
    uintptr_t model=0,header=0;uint64_t guid=0;
    float scale=1;bool measured=false;
    bool Update(uintptr_t currentModel,uintptr_t currentHeader,uint64_t currentGuid,const float* matrix){
        const bool identity=model!=currentModel || header!=currentHeader || guid!=currentGuid;
        if(identity){*this={};model=currentModel;header=currentHeader;guid=currentGuid;}
        float next=1;
        if(!currentModel || !currentGuid || !UniformScale(matrix,next))return identity;
        const bool changed=!measured || fabsf(next-scale)>(std::max)(.0001f,scale*.0001f);
        // Rotation-rounding noise must not retire the displayed board every
        // frame. Retain the measured value until an actual size change occurs.
        if(changed)scale=next;
        measured=true;return identity || changed;
    }
};
inline boardplacement::Fit Apply(boardplacement::Fit fit,float scale){
    fit.modelScale=scale;
    return boardplacement::Valid(fit)?fit:boardplacement::Fit{};
}
}
