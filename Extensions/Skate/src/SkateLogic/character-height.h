#pragma once
#include <cmath>
#include <cstdint>

// Visual root correction only: never filters engine poses, feet, board geometry,
// entity position, collision, crouch input or movement speed. Not full foot IK.
namespace characterheight {
constexpr float BlendSeconds=.16f;
constexpr float MaxRateYards=1.25f;
constexpr uint32_t MaxGapMs=250;
struct Filter {
    bool active=false;
    float height=0;
    uint32_t at=0,epoch=0;
    uint64_t tick=0;
    void Clear() { *this={}; }
    bool Step(float target,uint32_t now,uint32_t currentEpoch,uint64_t currentTick,float& out) {
        out=0;
        if (!std::isfinite(target) || target<-.6f || target>.75f || !currentEpoch) {
            Clear(); return false;
        }
        // A new activation/model is cleared by the publisher; epoch, stale time,
        // backwards clock/tick also prevent carrying correction into another pose.
        if (!active || epoch!=currentEpoch || now-at>=MaxGapMs || currentTick<tick) {
            active=true; height=target;
        } else if (now!=at) {
            float seconds=(now-at)*.001f;
            const float error=target-height,amount=fabsf(error);
            const float threshold=MaxRateYards*BlendSeconds;
            float remaining=amount;
            // Exact rate-limited exponential step. Constant targets give the
            // same result at different render rates (including duplicate ticks).
            if (remaining>threshold) {
                const float linearSeconds=(remaining-threshold)/MaxRateYards;
                if (seconds<=linearSeconds) { remaining-=MaxRateYards*seconds; seconds=0; }
                else { remaining=threshold; seconds-=linearSeconds; }
            }
            remaining*=std::exp(-seconds/BlendSeconds);
            height=target-std::copysign(remaining,error);
        }
        at=now; epoch=currentEpoch; tick=currentTick; out=height;
        return std::isfinite(out);
    }
};
}
