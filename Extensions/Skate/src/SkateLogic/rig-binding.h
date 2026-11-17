#pragma once
#include <cstdint>

// A native model address can survive a world transfer, or be reused by a new
// local player object. A rig is owned by that lifetime, not just its M2 layout.
namespace rigbinding {
struct Owner {
    uint64_t guid=0;
    uintptr_t player=0;
    uint32_t map=0,world=0;
};
inline bool Same(const Owner& a,const Owner& b) {
    return a.guid && a.player && a.guid==b.guid && a.player==b.player
        && a.map==b.map && a.world==b.world;
}
inline uint32_t BodyMask(unsigned count) {
    return count && count<32 ? (uint32_t(1)<<count)-1 : 0;
}
inline bool Complete(unsigned hits,unsigned count) {
    const auto mask=BodyMask(count);
    return mask && hits==mask;
}
}
