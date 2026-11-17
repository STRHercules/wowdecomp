#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "xbox-input.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL persistent input %u %s\n",checks,why);exit(1);}}
int main(){
    xboxinput::ResumeGate gate;
    xboxinput::Pad neutral{};
    Check(!gate.Step(false,neutral),"not active cannot arm controller input");
    for(unsigned bit=0;bit<16;++bit){
        auto held=neutral;held.buttons=static_cast<uint16_t>(1u<<bit);
        gate.Step(false,held);
        Check(!gate.Step(true,held),"carried controller button blocked on resume");
        Check(gate.Step(true,neutral),"fresh neutral rearms controls");
        Check(gate.Step(true,held),"new deliberate controller input works after neutral");
    }
    for(unsigned axis=0;axis<4;++axis){
        for(int value : {-32768,-7850,7850,32767}){
            auto held=neutral;
            if(axis==0)held.lx=static_cast<int16_t>(value);
            if(axis==1)held.ly=static_cast<int16_t>(value);
            if(axis==2)held.rx=static_cast<int16_t>(value);
            if(axis==3)held.ry=static_cast<int16_t>(value);
            gate.Step(false,held);
            Check(!gate.Step(true,held),"carried stick displacement blocked");
            Check(gate.Step(true,neutral) && gate.Step(true,held),"stick input resumes only after neutral");
        }
    }
    for(unsigned trigger=0;trigger<2;++trigger){
        auto held=neutral;
        if(trigger)held.rightTrigger=255;else held.leftTrigger=255;
        gate.Step(false,held);
        Check(!gate.Step(true,held),"carried trigger or grab cannot repeat across a suspension");
        Check(gate.Step(true,neutral) && gate.Step(true,held),"deliberate trigger works after neutral");
    }
    for(unsigned cycle=0;cycle<1000;++cycle){
        auto held=neutral;held.buttons=0x8000;held.ly=32767;
        Check(!gate.Step(false,held) && !gate.Step(true,held),"repeated focus/disconnect/fault transitions require fresh neutral");
        Check(gate.Step(true,neutral),"repeated safe resume available");
    }
    printf("{\"passed\":true,\"checks\":%u,\"neutral_required_after_suspend\":true,\"buttons_sticks_triggers_tested\":true,\"repeat_recovery_tested\":true,\"game_access\":false}\n",checks);
}
