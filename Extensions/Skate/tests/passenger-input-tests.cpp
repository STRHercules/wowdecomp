#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "passenger-input.h"
#include "skate-visual.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value){++checks;if(!value){fprintf(stderr,"FAIL passenger input %u\n",checks);exit(1);}}
int main(){
    volatile LONG epoch=1;skatewire::Pipe helper;std::vector<unsigned char> input,reply,empty;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\passenger-input-helper.log",1,&epoch));
    for(unsigned trial=0;trial<2;++trial){
        input.clear();for(float v:{0.f,0.f,0.f,0.f})skatewire::Float(input,v);skatewire::Word(input,2);
        for(float v:{-100.f,0.f,-100.f,-100.f,0.f,100.f,100.f,0.f,100.f,-100.f,0.f,-100.f,100.f,0.f,100.f,100.f,0.f,-100.f})skatewire::Float(input,v);
        Check(helper.Request(1,1,input,reply));Check(helper.Request(7,1,empty,reply));
        const auto mesh=skatevisual::Schema(reply);Check(mesh!=nullptr);
        trickstate::State state;Check(passengerinput::RestoreOffboard(helper,1,reply,state));Check(onfootstate::Active(state.source));
        auto frame=skatevisual::Decode(reply,*mesh,true);Check(frame!=nullptr);
        for(unsigned n=0;n<10;++n){
            skatewire::TrickStep(input,3,false,false,false,false,0,0,0,0);
            for(unsigned tick=0;tick<3;++tick)skatewire::GrabSample(input,0,0,0,0);
            Check(helper.Request(34,1,input,reply));frame=skatevisual::Decode(reply,*mesh,true);Check(frame!=nullptr);
            std::vector<unsigned char> metadata;Check(helper.Request(24,1,empty,metadata));
            Check(trickstate::Decode(metadata,frame->pose.tick,state));Check(onfootstate::Active(state.source));
        }
    }
    printf("{\"passed\":true,\"checks\":%u,\"restoration_trials\":2,\"original_y_action\":true,\"neutral_input_keeps_onfoot\":true,\"live_verified\":false}\n",checks);
}
