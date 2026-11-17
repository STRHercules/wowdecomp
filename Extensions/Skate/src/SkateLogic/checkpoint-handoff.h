#pragma once
#include <cstdint>
#include <cmath>
namespace checkpointhandoff {
enum class Action { None, Wait, Resume, Refuse };
constexpr unsigned MaxWaitMs=100;
constexpr float MaxResetYards=1.25f;
inline bool Ordinary(unsigned state) {
    return (state>=100 && state<=103) || (state>=200 && state<=202) || (state>=500 && state<=503);
}
inline bool Near(float x,float y,float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z)
        && x*x+y*y+z*z<=MaxResetYards*MaxResetYards;
}
struct Gate {
    bool active=false;
    uint32_t epoch=0,startAt=0;
    uint64_t tick=0;
    Action Observe(uint32_t owner,uint64_t nextTick,unsigned state,uint32_t now,
        bool fresh,bool priorAccepted,float dx,float dy,float dz,bool nativeSupport) {
        if(!active && state!=702)return Action::None;
        if(!owner || !nextTick || !fresh || !priorAccepted || !Near(dx,dy,dz))return Action::Refuse;
        if(!active){active=true;epoch=owner;startAt=now;tick=nextTick;return Action::Wait;}
        if(owner!=epoch || nextTick<=tick || now-startAt>MaxWaitMs)return Action::Refuse;
        tick=nextTick;
        if(state==702)return Action::Wait;
        if(!Ordinary(state) || !nativeSupport)return Action::Refuse;
        active=false;return Action::Resume;
    }
};
}
