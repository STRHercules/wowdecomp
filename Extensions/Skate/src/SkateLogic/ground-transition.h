#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "ground-tuning.h"

namespace groundtransition {
constexpr unsigned Intervals=6;
constexpr unsigned DenseIntervals=128;
inline unsigned Capacity(){return groundtuning::NaturalMomentum?DenseIntervals:24u;}
constexpr float DenseSpacing=.0625f;
constexpr float Spacing=.25f,Distance=Intervals*Spacing;
constexpr float MaxSlope=1.f,SampleTolerance=.04f;
// Preserve the original quarter-yard probe's local bump/step envelope when
// adaptive prediction uses shorter intervals. Otherwise merely dividing the
// same supported path into more samples makes it fail before native collision
// or the Source board can attempt it. This is NOT a native step-height override.
constexpr float MaxAdjacentChange=Spacing*MaxSlope+SampleTolerance;
struct Probe { bool valid=false; float distance=0,spacing=0; unsigned intervals=0; };
inline Probe Refine(Probe coarse) {
    if (!coarse.valid || !std::isfinite(coarse.distance) || coarse.distance<Spacing || coarse.distance>(groundtuning::NaturalMomentum?8.f:Distance)) return {};
    const unsigned count=(std::min)(Capacity(),static_cast<unsigned>(ceilf(coarse.distance/DenseSpacing)));
    return {true,coarse.distance,coarse.distance/count,count};
}
inline Probe Predict(float speed,float period,uint32_t ageMs) {
    if (!std::isfinite(speed) || speed<0 || speed>groundtuning::Maximum() || !std::isfinite(period)
        || period<.001f || period>.1f || ageMs>=150) return {};
    // Guard only the next two simulation steps plus measured publication age,
    // not a fixed1.5-yard obstacle margin at every speed. No motion filtering.
    const float ahead=(std::max)(Spacing,(std::min)(groundtuning::NaturalMomentum?8.f:Distance,speed*(2*period+ageMs*.001f)));
    const unsigned count=(std::min)(groundtuning::NaturalMomentum?32u:Intervals,static_cast<unsigned>(ceilf(ahead/Spacing)));
    return {true,ahead,ahead/count,count};
}
inline bool Traversable(const float* heights,const bool* valid,unsigned intervals,float spacing) {
    if (!heights || !valid || !intervals || intervals>Capacity() || !std::isfinite(spacing) || spacing<=0 || spacing>Spacing+.00001f) return false;
    for (unsigned i=0;i<=intervals;++i) {
        if (!valid[i] || !std::isfinite(heights[i])) return false;
        if (i && fabsf(heights[i]-heights[i-1])>MaxAdjacentChange) return false;
    }
    return true;
}
// Each interval must have real support. A cumulative rise is not a ledge:
// judge adjacent heights, never invent a ramp between absent/disjoint surfaces.
inline bool Traversable(const float (&heights)[Intervals+1],const bool (&valid)[Intervals+1]) {
    return Traversable(heights,valid,Intervals,Spacing);
}
template<class Query> bool Sample(Probe probe,float feet,bool dense,float* heights,bool* valid,Query query,float feetTolerance=.125f) {
    if (!probe.valid || !probe.intervals || probe.intervals>Capacity() || !std::isfinite(feet)
        || !heights || !valid || !std::isfinite(feetTolerance) || feetTolerance<.125f || feetTolerance>.5f) return false;
    float previous=feet;
    for (unsigned i=0;i<=probe.intervals;++i) {
        // BEGIN on the current lower supporting floor, not a nearby upper rim.
        // Later samples still test actual step/ramp rises and may block them.
        const float rise=i==0?.125f:dense?.75f:3.f;
        valid[i]=query(i*probe.spacing,dense?previous:feet,rise,dense?1.25f:6.f,heights[i]);
        if (!valid[i] || !std::isfinite(heights[i])) { heights[i]=NAN; return false; }
        previous=heights[i];
    }
    return fabsf(heights[0]-feet)<=feetTolerance && Traversable(heights,valid,probe.intervals,probe.spacing);
}
}
