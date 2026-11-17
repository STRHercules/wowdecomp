#pragma once
#include "flight-handoff.h"
namespace movementphase {
// Original Source trick states, not bails or off-board locomotion.
inline bool Plant(unsigned state){return state>=600 && state<=602;}
inline bool FootStep(bool lease,bool fresh,unsigned state,float sourceFloorGap,float rootGap,float nativeGap){
    return lease && fresh && (state==500 || state==502) && std::isfinite(sourceFloorGap)
        && sourceFloorGap>=-.125f && sourceFloorGap<=.5f && std::isfinite(rootGap)
        && rootGap<-.125f && rootGap> -3.f && std::isfinite(nativeGap) && nativeGap>.125f && nativeGap<3.f;
}
inline bool ProfileRoot(bool lease,bool fresh,unsigned state,bool ground,bool air,unsigned flags,bool spline,float gap){
    constexpr unsigned unsafe=0x100u|0x200u|0x400u|0x800u|0x2000u|0x100000u|0x200000u|0x1000000u|0x2000000u;
    return lease && fresh && (state==100 || state==103) && ground && !air && !spline
        && !(flags&unsafe) && std::isfinite(gap) && fabsf(gap)<3.f;
}
inline bool GroundRoot(bool lease,bool fresh,unsigned state,bool ground,bool air,unsigned flags,bool spline,float gap){
    return (flags&0x1000u) && ProfileRoot(lease,fresh,state,ground,air,flags,spline,gap);
}
}
