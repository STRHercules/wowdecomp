#pragma once
#include <cmath>
#include <cstdint>
namespace vanillaroot {
// Vanilla skate::present consumes pose.root.w_axis, not motion().deck velocity.
// Keep its actual height and measure that same root across physics ticks.
struct History {
    uint32_t epoch=0,at=0;
    uint64_t tick=0;
    float height=0,velocity=0;
    bool valid=false;
    void Clear() { *this={}; }
    bool Step(uint32_t currentEpoch,uint64_t sourceTick,float period,float z,uint32_t publishedAt) {
        if (!currentEpoch || !sourceTick || !std::isfinite(period) || period<.001f || period>.05f
            || !std::isfinite(z) || fabsf(z)>5000) return false;
        if (epoch!=currentEpoch || sourceTick<tick) Clear();
        if (valid && sourceTick==tick) return true;
        float rate=0;
        if (valid && sourceTick>tick && sourceTick-tick<=9) {
            rate=(z-height)/((sourceTick-tick)*period);
            if (!std::isfinite(rate) || fabsf(rate)>55) return false;
        }
        epoch=currentEpoch; tick=sourceTick; height=z; velocity=rate; at=publishedAt; valid=true; return true;
    }
};
}
