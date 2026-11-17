#pragma once
#include "onfoot-terrain.h"
namespace ridingcontact {
// Vanilla follows current Source contact; a native foot-height mismatch alone
// is not a bail. Retain both measured floors and the existing short wait lease.
inline bool Supported(bool lease,bool fresh,uint32_t flags,bool spline,unsigned state,
    bool ground,bool air,float nativeGap,float sourceGap,float floorDelta,float rootGap) {
    return lease && fresh && !(flags&onfootterrain::Unsafe) && !spline
        && (state==100 || state==103) && ground && !air
        && onfootterrain::NearFloor(nativeGap) && onfootterrain::NearFloor(sourceGap)
        && std::isfinite(floorDelta) && fabsf(floorDelta)<=onfootterrain::FloorGap
        && std::isfinite(rootGap) && fabsf(rootGap)<3.f
        // Reject mismatched ray/root records instead of borrowing another floor.
        && fabsf(rootGap-(floorDelta+sourceGap-nativeGap))<=.002f;
}
inline bool Wait(bool supported,uint32_t flags,uint32_t age) {
    return supported && (flags&0x1000u) && !(flags&onfootterrain::Unsafe)
        && age<onfootterrain::WaitMs;
}
}
