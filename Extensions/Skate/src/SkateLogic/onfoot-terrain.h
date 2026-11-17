#pragma once
#include "onfoot-state.h"
#include <cmath>
#include <cstdint>
namespace onfootterrain {
// Original biped ground roots can briefly sit above native feet/floor while
// taking a step. This is measured support permission, not extra step height.
constexpr float FloorGap=.5f;
constexpr float FloorPenetration=.125f;
// Do not mistake an upper platform/ceiling half a yard above a standing
// character for the floor they are currently standing on.
constexpr float QueryRise=.125f;
constexpr uint32_t WaitMs=250;
constexpr uint32_t Unsafe=0x100u|0x200u|0x400u|0x800u|0x2000u|0x100000u|0x200000u|0x1000000u|0x2000000u;
inline bool NearFloor(float gap){return std::isfinite(gap) && gap>=-FloorPenetration && gap<=FloorGap;}
inline bool Supported(bool enabled,unsigned state,bool lease,bool pairedFresh,uint32_t flags,bool spline,
    float nativeFloorGap,float sourceFloorGap,float rootGap){
    return enabled && onfootstate::Active(state) && lease && pairedFresh && !(flags&Unsafe) && !spline
        && NearFloor(nativeFloorGap) && NearFloor(sourceFloorGap) && std::isfinite(rootGap) && fabsf(rootGap)<=FloorGap;
}
inline bool Ground(bool supported,unsigned state,bool sourceGround,bool sourceAir){
    return supported && onfootstate::Ground(state) && sourceGround && !sourceAir;
}
inline bool ContactWait(bool ground,uint32_t flags,uint32_t age){return ground && (flags&0x1000u) && !(flags&Unsafe) && age<WaitMs;}
}
