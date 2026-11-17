#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "background-input.h"
#include "xbox-input.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok){++checks;if(!ok){fprintf(stderr,"FAIL background input %u\n",checks);exit(1);}}
int main(){
    for(unsigned held=0;held<16;++held){
        backgroundinput::Keyboard keys;
        Check(keys.Step(true,held)==held);
        for(unsigned otherApp=0;otherApp<16;++otherApp)Check(keys.Step(false,otherApp)==0);
        Check(keys.Step(true,held)==0);
        Check(keys.Step(true,0)==0);
        Check(keys.Step(true,held)==held);
    }
    backgroundinput::Keyboard keys;
    keys.Step(false,15);Check(keys.Step(true,3)==0);
    Check(keys.Step(true,5)==4); // only newly released/pressed keys pass
    Check(keys.Step(true,1)==0);Check(keys.Step(true,0)==0);Check(keys.Step(true,1)==1);
    xboxinput::ResumeGate pad;
    xboxinput::Pad neutral{},held{};held.buttons=0x8000;held.ly=32767;held.rightTrigger=255;
    for(unsigned cycle=0;cycle<1000;++cycle){
        Check(!pad.Step(false,held));Check(!pad.Step(true,held));
        Check(pad.Step(true,neutral));Check(pad.Step(true,held));
    }
    printf("{\"passed\":true,\"checks\":%u,\"background_keys_neutral\":true,\"keyboard_release_gate\":true,\"pad_neutral_gate\":true,\"game_access\":false}\n",checks);
}
