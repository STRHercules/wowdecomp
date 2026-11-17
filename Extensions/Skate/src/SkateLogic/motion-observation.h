#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "ground-tuning.h"
namespace motionobservation {
// Read-only native travel rate. The configured allowance is not necessarily
// the actual horizontal distance covered on terrain or during a stock jump.
struct Speed {
    bool active=false;
    uint32_t epoch=0,at=0;
    float x=0,y=0,value=0,heading=0;
    bool direction=false;
    float Heading(float fallback) const {return direction?heading:fallback;}
    void Clear() { *this={}; }
    float Step(uint32_t currentEpoch,uint32_t now,float px,float py,float fallback) {
        if (!currentEpoch || !std::isfinite(px) || !std::isfinite(py) || !std::isfinite(fallback)
            || fallback<0 || fallback>groundtuning::Maximum()) { Clear(); return fallback; }
        if (!active || epoch!=currentEpoch || now-at>120) {
            active=true; epoch=currentEpoch; at=now; x=px; y=py; value=fallback;direction=false; return value;
        }
        const auto elapsed=now-at;
        if (elapsed<30) return value;
        const float seconds=elapsed*.001f;
        const float dx=px-x,dy=py-y,raw=hypotf(dx,dy)/seconds;
        x=px; y=py; at=now;
        if (!std::isfinite(raw) || raw>(std::max)(20.f,groundtuning::Maximum()*1.5f)) { value=fallback;direction=false; return value; }
        if(raw>.15f){heading=atan2f(dy,dx);direction=true;}
        const float blend=-std::expm1(-seconds/.10f);
        value=(std::min)(groundtuning::Maximum(),(std::max)(0.f,value+(raw-value)*blend));
        return value;
    }
};
}
