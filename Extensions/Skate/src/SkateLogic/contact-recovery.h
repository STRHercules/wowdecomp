#pragma once
#include <cmath>
#include <cstdint>
namespace contactrecovery {
constexpr uint32_t Unsafe=0x100u|0x200u|0x400u|0x800u|0x2000u|0x100000u|0x200000u|0x1000000u|0x2000000u;
inline bool DescendingContour(bool lease,bool fresh,uint32_t flags,bool spline,bool sourceGround,bool sourceAir,
    bool continuous,float floorGap,float rootGap,float rootVelocity){
    // A measured downhill contour can outlive native grounded contact for a
    // frame. Resume existing Source descent; don't invent a ramp/ollie impulse.
    return lease && fresh && !(flags&Unsafe) && !spline && sourceGround && !sourceAir && continuous
        && std::isfinite(floorGap) && floorGap>.125f && floorGap<=.5f
        && std::isfinite(rootGap) && fabsf(rootGap)<.5f
        && std::isfinite(rootVelocity) && rootVelocity<-.15f && rootVelocity>=-55;
}
inline bool OwnedContact(bool lease,bool fresh,uint32_t flags,bool spline,bool contact,float rootGap) {
    return lease && fresh && (flags&0x1000u) && !(flags&Unsafe) && !spline && contact
        && std::isfinite(rootGap) && fabsf(rootGap)<3.f;
}
inline bool Pop(bool lease,bool fresh,uint32_t flags,bool spline,bool contact,float rootGap,
    bool ground,bool air,bool pending,uint32_t age,float rootVelocity) {
    return OwnedContact(lease,fresh,flags,spline,contact,rootGap) && !ground && air && pending && age<600
        && rootGap>.025f && std::isfinite(rootVelocity) && rootVelocity>.5f && rootVelocity<=55.f;
}
struct Ground {
    uint32_t epoch=0,at=0;
    uint64_t tick=0;
    unsigned samples=0;
    void Clear() { *this={}; }
    bool Observe(uint32_t now,uint32_t current,uint64_t next,bool lease,bool fresh,uint32_t flags,bool spline,
        bool contact,float rootGap,bool ground,bool air) {
        if (!current || !next || !OwnedContact(lease,fresh,flags,spline,contact,rootGap)
            || fabsf(rootGap)>=.5f || !ground || air) { Clear(); return false; }
        if (epoch!=current || (tick && (next<tick || now-at>=150))) Clear();
        epoch=current;
        if (next!=tick) { tick=next; at=now; if (samples<3) ++samples; }
        return samples>=3;
    }
};
}
