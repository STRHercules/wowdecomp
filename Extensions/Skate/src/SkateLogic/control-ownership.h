#pragma once
#include <cstdint>
namespace controlownership {
// CInputControl's FORWARD bit is0x10; movement's actual FORWARD flag is0x1.
// Native focus/input handling may clear the latter without changing our cache.
inline bool ReassertForward(unsigned desired,unsigned owned,bool owner,uint32_t flags,bool spline){
    constexpr uint32_t unsafe=0x100u|0x200u|0x400u|0x800u|0x100000u|0x200000u|0x1000000u|0x2000000u;
    return owner && !spline && (desired&0x10u) && (owned&0x10u) && !(flags&1u) && !(flags&unsafe);
}
}
