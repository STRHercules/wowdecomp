#pragma once
#include "ground-transition.h"
#include "contact-recovery.h"
namespace ledgeauthority {
// Vanilla skate::present follows the same Source body through a drop; it does
// not require the next native walking samples to form a continuous floor.
inline bool Owner(bool lease,bool fresh,uint32_t flags,bool spline,float rootGap){
    return lease && fresh && !(flags&contactrecovery::Unsafe) && !spline
        && std::isfinite(rootGap) && fabsf(rootGap)<.5f;
}
inline bool DownwardProfile(float feet,const float* heights,const bool* valid,unsigned intervals){
    if(!heights || !valid || !intervals || intervals>groundtransition::Capacity()
        || !std::isfinite(feet) || !valid[0] || !std::isfinite(heights[0])
        || feet-heights[0]<-.125f || feet-heights[0]>.5f)return false;
    bool drop=feet-heights[0]>.125f;
    for(unsigned i=1;i<=intervals;++i){
        // Unknown space is not claimed as support. Native swept collision and
        // the actual Source phase still decide whether/how the body falls.
        if(!valid[i])return true;
        if(!std::isfinite(heights[i]) || heights[i]>heights[0]+.125f
            || heights[i]-heights[i-1]>groundtransition::MaxAdjacentChange)return false;
        if(heights[0]-heights[i]>.125f)drop=true;
    }
    return drop;
}
inline bool Ground(bool lease,bool fresh,uint32_t flags,bool spline,float rootGap,
    unsigned state,bool sourceGround,bool sourceAir){
    return Owner(lease,fresh,flags,spline,rootGap) && (state==100 || state==103)
        && sourceGround && !sourceAir;
}
inline bool Air(bool lease,bool fresh,uint32_t flags,bool spline,float rootGap,
    unsigned state,bool sourceGround,bool sourceAir){
    // A genuine current riding-air phase is stronger evidence than a native
    // floor ray below a ledge. Never extend this to unknown or wipeout states.
    return Owner(lease,fresh,flags,spline,rootGap) && state>=200 && state<=202
        && !sourceGround && sourceAir;
}
}
