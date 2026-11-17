#pragma once
#include "skate-pipe.h"
namespace powerslideinput {
struct ButtonGate {
    bool neutralRequired=true;
    void Clear() { neutralRequired=true; }
    bool Step(bool allowed,bool held) {
        if (!allowed) { Clear(); return false; }
        if (neutralRequired) { if (!held) neutralRequired=false; return false; }
        return held;
    }
};
struct Shortcut {
    bool active=false,braking=false;
    int side=1;
    bool Apply(bool held,bool ground,bool air,int16_t left,bool footBrake,std::vector<unsigned char>& bytes) {
        braking=false;
        if (!held) { active=false; return false; }
        if (!ground || air || footBrake || bytes.size()!=32) { active=false; return false; }
        if(left>=-7849 && left<=7849){
            // Vanilla forwards B as foot brake. Only an explicit lateral stick
            // requests the user's powerslide remap; never invent a remembered
            // direction with a centred stick, even at zero/low speed.
            active=false;braking=true;
            const uint32_t buttons=(skatewire::Get(bytes.data()+4)&~0x7000u)|0x2000u,zero=0;
            memcpy(bytes.data()+4,&buttons,4);memcpy(bytes.data()+8,&zero,4);memcpy(bytes.data()+12,&zero,4);
            return false;
        }
        if (!active) { side=left<0?-1:1; active=true; }
        // Stock riding_intentions8259A430 requires full-length rear diagonal
        // (angle +/-0.64), NOT a pure lateral steer. Engine owns entry speed,
        // slide friction, animation and release; this is only a B shortcut.
        const uint32_t x=static_cast<uint32_t>(static_cast<int32_t>(side*19660));
        const uint32_t y=static_cast<uint32_t>(static_cast<int32_t>(-26214));
        const uint32_t buttons=skatewire::Get(bytes.data()+4)&~0x7000u;
        memcpy(bytes.data()+4,&buttons,4); memcpy(bytes.data()+8,&x,4); memcpy(bytes.data()+12,&y,4);
        return true;
    }
};
}
