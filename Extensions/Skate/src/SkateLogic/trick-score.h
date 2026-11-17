#pragma once
#include "trick-state.h"
#include <algorithm>
// Vanilla score.rs presentation policy, not a new scoring/reward system.
// Original-engine live points and banked totals are the only score source.
namespace trickscore {
struct Shown { unsigned kind=0;float points=0,age=0;const char* name=""; };
struct Tracker {
    bool primed=false,hasLanded=false,hasBail=false;
    uint32_t epoch=0,count=0,landedAt=0,bailAt=0;
    uint64_t tick=0;double total=0;
    float peak=0,landedPoints=0,lost=0;
    char live[97]{},landedName[97]{};
    void ClearLive(){peak=0;live[0]=0;}
    void Observe(uint32_t newEpoch,uint32_t now,bool active,const trickstate::State& state) {
        if(!active || !state.valid){ClearLive();return;}
        if(!std::isfinite(state.points) || state.points<0 || state.points>1e8f
            || !std::isfinite(state.total) || state.total<0 || state.total>1e12)return;
        if(primed && epoch==newEpoch && state.tick==tick)return;
        const bool baseline=!primed || epoch!=newEpoch || state.tick<tick || state.landed<count || state.total<total;
        if(baseline){ClearLive();hasLanded=hasBail=false;}
        const bool banked=!baseline && state.landed!=count;
        const float was=peak;
        if(banked) {
            landedPoints=(std::max)(was,static_cast<float>(state.total-total));
            strcpy_s(landedName,live[0]?live:state.last);
            landedAt=now;hasLanded=true;hasBail=false;
        } else if(was>0 && state.points<=0) {
            lost=was;bailAt=now;hasBail=true;
        }
        peak=!banked && state.points>0?(std::max)(state.points,was):state.points;
        strcpy_s(live,state.live);
        primed=true;epoch=newEpoch;tick=state.tick;count=state.landed;total=state.total;
    }
    Shown Show(uint32_t now) const {
        if(peak>0)return {1,peak,0,live};
        const float landedAge=static_cast<uint32_t>(now-landedAt)*.001f;
        if(hasLanded && landedAge<3.5f)return {2,landedPoints,landedAge,landedName};
        const float bailAge=static_cast<uint32_t>(now-bailAt)*.001f;
        if(hasBail && bailAge<2.5f)return {3,lost,bailAge,"BAIL"};
        return {};
    }
};
}
