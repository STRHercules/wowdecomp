#pragma once
#include "skate-pipe.h"
#include "trick-state.h"
#include "onfoot-state.h"
namespace passengerinput {
// Reapply the player's prior offboard intent after coordinate-epoch rebind,
// through the original Y graph action, never private state/pose field writes.
inline bool RestoreOffboard(skatewire::Pipe& helper,uint32_t epoch,std::vector<unsigned char>& frame,trickstate::State& state){
    std::vector<unsigned char> input,metadata,empty;
    for(unsigned batch=0;batch<20;++batch){
        skatewire::TrickStep(input,3,false,false,false,false,0,0,0,0);
        const uint32_t buttons=batch==0?0x8000u:0u;memcpy(input.data()+4,&buttons,4);
        for(unsigned tick=0;tick<3;++tick)skatewire::GrabSample(input,0,0,0,0);
        if(!helper.Request(34,epoch,input,frame)||frame.size()<60)return false;
        uint64_t tick=0;memcpy(&tick,frame.data(),8);
        if(!helper.Request(24,epoch,empty,metadata)||!trickstate::Decode(metadata,tick,state))return false;
        if(onfootstate::Active(state.source))return true;
        if(state.source==300)return false;
    }
    return false;
}
}
