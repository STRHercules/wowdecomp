#pragma once
#include <cstdint>
#include <cmath>
namespace roughcontact {
constexpr uint32_t MaximumMs=400;
// Only actual riding-air phases may bridge a small bump. Wipeout/offboard/
// grind/unknown states never borrow this permission. Native support is separate.
inline bool RidingAir(uint32_t state) { return state>=200 && state<=202; }
struct Grace {
    uint32_t epoch=0,at=0;
    bool active=false;
    void Clear() { *this={}; }
    bool Step(uint32_t currentEpoch,uint32_t now,bool ground,uint32_t state,
        bool supported,float gap,float vertical) {
        if (ground) { Clear(); return false; }
        if (!currentEpoch || !RidingAir(state) || !supported || !std::isfinite(gap)
            || fabsf(gap)>.65f || !std::isfinite(vertical) || fabsf(vertical)>4.f) { Clear(); return false; }
        if (!active || epoch!=currentEpoch) { active=true; epoch=currentEpoch; at=now; }
        return now-at<MaximumMs;
    }
};
}
