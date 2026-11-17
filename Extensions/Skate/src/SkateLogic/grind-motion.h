#pragma once
#include <cstdint>
#include <cmath>
namespace grindmotion {
// Original Skate physical state IDs, not a guessed contact or trick label.
inline bool Riding(uint32_t state) { return state>=400 && state<=405; }
struct Phase {
    uint32_t epoch=0;
    uint64_t lastGrind=0,tick=0;
    void Clear() { *this={}; }
    bool Observe(uint32_t current,uint64_t sourceTick,float period,uint32_t state) {
        if (!current || !sourceTick || !std::isfinite(period) || period<.001f || period>.05f) { Clear(); return false; }
        if (epoch!=current || sourceTick<tick) { Clear(); epoch=current; }
        tick=sourceTick;
        if (Riding(state)) { lastGrind=sourceTick; return true; }
        // Native Nonspecific is a real original transition used between grind
        // variants and pop-out. Do not admit unknown state0 or arbitrary air.
        if (state==701 && lastGrind && sourceTick>=lastGrind &&
            (sourceTick-lastGrind)*period<.6f) return true;
        lastGrind=0; return false;
    }
};
struct Lease {
    uint32_t epoch=0;
    uint64_t tick=0;
    void Clear() { *this={}; }
    bool Renew(uint32_t current,uint64_t sourceTick,uint32_t age,uint32_t state,bool owned,bool fresh) {
        if (!owned || !fresh || !Riding(state) || !current || !sourceTick || age>=150) { Clear(); return false; }
        if (epoch==current && sourceTick<=tick) return false;
        epoch=current; tick=sourceTick; return true;
    }
};
}
