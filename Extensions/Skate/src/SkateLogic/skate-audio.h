#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
// Read-only translation of the installed Vanilla skate/sound.rs. Units: m/s.
namespace skateaudio {
enum class Phase { Off, Ground, Slide, Air, BoardGrind, TruckGrind, Wipeout, Other };
inline Phase Of(unsigned state) {
    if(state==101)return Phase::Slide;
    if((state>=100 && state<=105) || state==503)return Phase::Ground;
    if((state>=200 && state<=202) || state==602)return Phase::Air;
    if(state==300)return Phase::Wipeout;
    if(state==400 || state==402 || state==405)return Phase::BoardGrind;
    if(state==401 || state==403 || state==404)return Phase::TruckGrind;
    return Phase::Other;
}
inline bool Contact(Phase p){return p==Phase::Ground || p==Phase::Slide || p==Phase::BoardGrind || p==Phase::TruckGrind;}
enum Cue : unsigned { Pop=1, Land=2, HardLand=4, Bail=8, Skid=16 };
struct Sample {
    uint32_t epoch=0,at=0;
    uint64_t tick=0;
    unsigned state=0;
    bool active=false,terrain=false;
    float speed=0,vertical=0,gain=0;
};
struct Mix { float volume[5]{},rate[5]{1,1,1,1,1}; unsigned cues=0; float skidGain=0; bool active=false; };
inline float Clamp(float x,float lo,float hi){return (std::max)(lo,(std::min)(hi,x));}
inline bool Valid(const Sample& s){return s.active && s.epoch && s.tick && std::isfinite(s.speed)
    && s.speed>=0 && s.speed<=150 && std::isfinite(s.vertical) && std::isfinite(s.gain);}
class Tracker {
    Phase phase_=Phase::Off;
    uint32_t epoch_=0,skidAt_=0;
    uint64_t tick_=0;
    float fall_=0;
public:
    void Clear(){phase_=Phase::Off;epoch_=0;tick_=0;fall_=0;skidAt_=0;}
    Mix Observe(const Sample& s) {
        Mix m{};
        if(!Valid(s)){Clear();return m;}
        if(epoch_!=s.epoch){Clear();epoch_=s.epoch;}
        if(s.tick<tick_){Clear();epoch_=s.epoch;}
        const auto p=Of(s.state);
        if(s.tick!=tick_){
            if(p==Phase::Air)fall_=phase_==Phase::Air?(std::min)(fall_,s.vertical):s.vertical;
            if(p!=phase_){
                if(Contact(phase_) && p==Phase::Air)m.cues|=Pop;
                else if(phase_==Phase::Air && Contact(p))m.cues|=fall_<-5?HardLand:Land;
                else if(p==Phase::Wipeout)m.cues|=Bail;
            }
            tick_=s.tick;phase_=p;
            if(p==Phase::Slide && s.speed>1 && (!skidAt_ || s.at-skidAt_>=120)){
                m.cues|=Skid;skidAt_=s.at;
            }
        }
        m.active=true;
        const float roll=(p==Phase::Ground || p==Phase::Slide)?Clamp(s.speed/6,0,1):0;
        m.volume[0]=s.terrain?0:roll*.9f;m.volume[1]=s.terrain?roll*.9f:0;
        m.volume[2]=p==Phase::TruckGrind?Clamp(.4f+s.speed/8,0,1):0;
        m.volume[3]=p==Phase::BoardGrind?Clamp(.4f+s.speed/8,0,1):0;
        m.volume[4]=Clamp((s.speed-4)/10,0,.6f);
        m.rate[0]=m.rate[1]=.7f+(std::min)(s.speed/12,.8f);
        m.rate[2]=m.rate[3]=.85f+(std::min)(s.speed/20,.4f);
        m.skidGain=Clamp(s.speed/8,.3f,.8f);
        return m;
    }
};
}
