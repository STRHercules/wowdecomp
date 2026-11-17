#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "ground-tuning.h"
namespace airroot {
struct Point { float x,y,z; };
static_assert(sizeof(Point)==12,"native by-value collision vector");
struct History {
    uint32_t epoch=0,at=0;
    uint64_t tick=0;
    float x=0,y=0,vx=0,vy=0;
    bool valid=false;
    void Clear() { *this={}; }
    bool Step(uint32_t current,uint64_t next,float period,float px,float py,uint32_t published) {
        if (!current || !next || !std::isfinite(period) || period<.001f || period>.05f
            || !std::isfinite(px) || !std::isfinite(py) || fabsf(px)>50000 || fabsf(py)>50000) return false;
        if (epoch!=current) Clear();
        if (valid && next<tick) return false;
        if (valid && next==tick) return px==x && py==y; // Never freshen a repeated snapshot.
        float dx=0,dy=0;
        if (valid) {
            if (next-tick>9) return false;
            const float elapsed=static_cast<float>(next-tick)*period;
            dx=(px-x)/elapsed; dy=(py-y)/elapsed;
            if (!std::isfinite(dx) || !std::isfinite(dy) || hypotf(dx,dy)>(std::max)(35.f,groundtuning::Maximum()*2)) return false;
        }
        epoch=current; tick=next; at=published; x=px; y=py; vx=dx; vy=dy; valid=true; return true;
    }
    bool Plan(uint32_t now,uint32_t owner,unsigned ms,float currentX,float currentY,Point& delta,float verticalError=0) const {
        if (!valid || owner!=epoch || now-at>=150 || ms>100 || !std::isfinite(currentX)
            || !std::isfinite(currentY) || !std::isfinite(delta.z) || !std::isfinite(verticalError)
            || fabsf(verticalError)>3 || hypotf(currentX-x,currentY-y)>=3) return false;
        const float dt=ms*.001f,age=(now-at)*.001f;
        float correctionX=(x+vx*age-currentX)*12,correctionY=(y+vy*age-currentY)*12;
        const float correction=sqrtf(correctionX*correctionX+correctionY*correctionY+verticalError*verticalError*144);
        // Native Z already limits tracking correction to3yd/s. Bound XY from
        // the same 3D root error instead of chasing it at14yd/s into an uphill
        // face while Z is still catching up. Source travel itself is immediate.
        if (correction>3) { correctionX*=3/correction; correctionY*=3/correction; }
        float dx=(vx+correctionX)*dt,dy=(vy+correctionY)*dt;
        const float length=hypotf(dx,dy),maximum=groundtuning::Maximum()*dt;
        if(groundtuning::NaturalMomentum && hypotf(vx,vy)>groundtuning::Maximum())return false;
        if (!std::isfinite(length)) return false;
        if (length>maximum && length>0) { dx*=maximum/length; dy*=maximum/length; }
        delta.x=dx; delta.y=dy; return true; // Native sweep owns position and contact.
    }
    bool FallRequest(uint32_t now,uint32_t owner,unsigned ms,float currentX,float currentY,float& distance,Point& direction,float verticalError=0) const {
        Point delta{0,0,0};
        if (!Plan(now,owner,ms,currentX,currentY,delta,verticalError)) return false;
        distance=hypotf(delta.x,delta.y);
        direction=distance>0?Point{delta.x/distance,delta.y/distance,0}:Point{1,0,0};
        return true;
    }
};
}
