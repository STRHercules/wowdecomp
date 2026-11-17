#pragma once
#include <cmath>
#include <cstdint>
namespace flighthandoff {
enum class Action { Keep, ResumeAir, Land, Refuse };
inline bool PendingPop(bool lease,bool fresh,bool ground,bool air,uint32_t state,uint32_t flags,bool spline,
    bool pending,uint32_t age,float rootGap) {
    constexpr uint32_t forbidden=0x100u|0x200u|0x400u|0x800u|0x2000u|0x100000u|0x200000u|0x1000000u|0x2000000u;
    // GroundAnimation can outlast native floor contact during a real flick.
    // Continue its paired Source root through stock fall sweeps, no new impulse.
    return lease && fresh && ground && !air && state==103 && (flags&0x1000u) && !(flags&forbidden)
        && !spline && pending && age<600 && std::isfinite(rootGap) && fabsf(rootGap)<3.f;
}
inline bool SupportedDescent(bool lease,bool fresh,bool ground,bool air,uint32_t flags,bool spline,
    float floorGap,float rootGap,float rootVelocity,unsigned nativeMs) {
    constexpr uint32_t forbidden=0x100u|0x200u|0x400u|0x800u|0x2000u|0x100000u|0x200000u|0x1000000u|0x2000000u;
    return lease && fresh && ground && !air && !spline && (flags&0x1000u) && !(flags&forbidden)
        && std::isfinite(floorGap) && floorGap>.125f && floorGap<=.5f
        && std::isfinite(rootGap) && fabsf(rootGap)<.5f && rootGap<=.125f
        && std::isfinite(rootVelocity) && rootVelocity<-.15f && nativeMs<250;
}
inline bool Supported(float feet,const float* heights,const bool* valid,unsigned required=3) {
    if (!std::isfinite(feet) || !valid[0] || !std::isfinite(heights[0]) || fabsf(feet-heights[0])>.08f) return false;
    unsigned count=0;
    for (unsigned i=0;i<5;++i) if (valid[i] && std::isfinite(heights[i]) && fabsf(feet-heights[i])<=.08f) ++count;
    return count>=required;
}
struct State {
    uint32_t epoch=0,lastResume=0;
    uint64_t tick=0;
    unsigned groundSamples=0;
    void Clear() { *this={}; }
    Action Observe(uint32_t now,uint32_t current,uint64_t sourceTick,bool fresh,bool sourceGround,bool sourceAir,
        uint32_t flags,bool contact,float sourceZ,float nativeZ,uint32_t flightAge,bool sourceRail=false) {
        if (!fresh || !current || !sourceTick || !std::isfinite(sourceZ) || !std::isfinite(nativeZ)
            || fabsf(sourceZ-nativeZ)>3) return Action::Refuse;
        if (epoch!=current) { Clear(); epoch=current; }
        if (tick && sourceTick<tick) return Action::Refuse;
        const bool distinct=sourceTick!=tick;
        tick=sourceTick;
        if (sourceGround && !sourceAir && contact) {
            if (distinct && groundSamples<3) ++groundSamples;
            return groundSamples>=3?Action::Land:Action::Keep;
        }
        groundSamples=0;
        // Resume the existing Source arc, not a second jump impulse. Contact
        // can legitimately clear native fall state before Source has finished.
        const bool unsettledGround=sourceGround && !sourceAir && !contact;
        if (((sourceAir && !sourceGround && (sourceRail || sourceZ-nativeZ>.025f)) || unsettledGround) && !(flags&0x1000u)
            && flightAge>=75 && (!lastResume || now-lastResume>=75)) {
            // A Source landing frame is not proof of native contact. If the
            // native ground path already cleared FALLING over a ledge, retain
            // the paired root through its swept fall path until actual support.
            lastResume=now; return Action::ResumeAir;
        }
        return Action::Keep;
    }
};
}
