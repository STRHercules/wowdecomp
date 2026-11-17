#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-pipe.h"
#include "shadow-state.h"
#include <cstdio>
#include <cassert>

static const wchar_t* kHelper=L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe";
static const wchar_t* kAssets=L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets";
static const wchar_t* kLog=L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\skate-x86-helper.log";

int main() {
    static_assert(sizeof(void*)==4,"The parent test must be x86.");
    unsigned checks=0;
    // Reproduce the old race: frame starts at 100, worker publishes at 120,
    // main thread copies at 125. Frame-start subtraction incorrectly wraps.
    assert(!skateshadow::Fresh(1,1,100,120)); ++checks;
    assert(skateshadow::Fresh(1,1,125,120)); ++checks;
    assert(skateshadow::Fresh(1,1,849,100)); ++checks;
    assert(!skateshadow::Fresh(1,1,850,100)); ++checks;
    assert(!skateshadow::Fresh(1,2,125,120)); ++checks;
    assert(!skateshadow::Fresh(0,0,125,120)); ++checks;
    assert(skateshadow::Fresh(1,1,25,0xfffffff0u)); ++checks;
    assert(!skateshadow::Fresh(1,1,800,0xfffffff0u)); ++checks;
    skatewire::Pose pose{};
    std::vector<unsigned char> bytes;
    assert(!skatewire::Decode(bytes,pose)); ++checks;
    bytes.resize(60,0);
    assert(!skatewire::Decode(bytes,pose)); ++checks;
    volatile LONG epoch=1;
    skatewire::Pipe helper;
    assert(helper.Start(kHelper,kAssets,kLog,1,&epoch)); ++checks;
    std::vector<unsigned char> init;
    for (float value : {0.f,0.f,0.f,0.f}) skatewire::Float(init,value);
    skatewire::Word(init,2);
    const float floor[]={-100,0,-100,-100,0,100,100,0,100,
        -100,0,-100,100,0,100,100,0,-100};
    for (float value : floor) skatewire::Float(init,value);
    assert(helper.Request(1,1,init,bytes) && skatewire::Decode(bytes,pose)); ++checks;
    const uint64_t initial=pose.tick;
    for (unsigned i=0;i<100;++i) {
        std::vector<unsigned char> step; skatewire::Word(step,3);
        const uint64_t previous=pose.tick;
        assert(helper.Request(2,1,step,bytes) && skatewire::Decode(bytes,pose));
        assert(pose.tick-previous==3 && pose.bones==36); ++checks;
    }
    assert(pose.tick-initial==300 && pose.ground && !pose.airborne); ++checks;
    assert(fabsf(pose.position[1]-0.087f)<0.02f && hypotf(pose.velocity[0],pose.velocity[2])<0.01f); ++checks;
    assert(pose.forward[2]>0.99f); ++checks;
    // WoW-facing zero becomes engine heading pi/2: forward points +X, not +Z.
    const float quarterTurn=1.57079632679f;
    memcpy(init.data()+12,&quarterTurn,4);
    assert(helper.Request(1,1,init,bytes) && skatewire::Decode(bytes,pose)); ++checks;
    for (unsigned i=0;i<20;++i) {
        std::vector<unsigned char> neutral; skatewire::Word(neutral,3);
        assert(helper.Request(2,1,neutral,bytes) && skatewire::Decode(bytes,pose));
    }
    // The physical deck settles with a small yaw offset (~1.6 degrees on this
    // stock floor). Test alignment within 3 degrees, not an exact actor bearing.
    assert(pose.forward[0]>0.9986f && fabsf(pose.forward[2])<0.0524f); ++checks;
    // The rigid payload decoder rejects an invalid period and airborne flag.
    auto malformed=bytes;
    const uint32_t invalidFlag=2;
    memcpy(malformed.data()+52,&invalidFlag,4);
    assert(!skatewire::Decode(malformed,pose)); ++checks;
    const float invalidPeriod=0;
    malformed=bytes; memcpy(malformed.data()+44,&invalidPeriod,4);
    assert(!skatewire::Decode(malformed,pose)); ++checks;
    std::vector<unsigned char> input;
    skatewire::InputStep(input,3,true,true,true,true);
    assert(input.size()==16 && skatewire::Get(input.data()+4)==0x2000
        && skatewire::Get(input.data()+8)==0); ++checks;
    const auto activate=[&]() {
        std::vector<unsigned char> activation;
        for (float value : {0.f,0.f,0.f,quarterTurn}) skatewire::Float(activation,value);
        assert(helper.Request(4,1,activation,bytes) && skatewire::Decode(bytes,pose)); ++checks;
        for (unsigned i=0;i<40;++i) {
            skatewire::InputStep(input,3,false,false,false,false);
            assert(helper.Request(5,1,input,bytes) && skatewire::Decode(bytes,pose));
        }
        assert(pose.ground && !pose.airborne && hypotf(pose.velocity[0],pose.velocity[2])<0.1f); ++checks;
    };
    const auto drive=[&](unsigned ticks,bool push,bool brake,bool left,bool right) {
        for (unsigned i=0;i<ticks;i+=3) {
            skatewire::InputStep(input,3,push,brake,left,right);
            const uint64_t previous=pose.tick;
            assert(helper.Request(5,1,input,bytes) && skatewire::Decode(bytes,pose));
            assert(pose.tick==previous+3 && pose.bones==36 && pose.ground && !pose.airborne); ++checks;
        }
    };
    activate();
    drive(180,true,false,false,false);
    const float pushed=hypotf(pose.velocity[0],pose.velocity[2]);
    assert(pushed>1); ++checks;
    drive(60,false,false,false,false);
    const float coasted=hypotf(pose.velocity[0],pose.velocity[2]);
    assert(coasted>1); ++checks;
    drive(120,false,true,false,false);
    const float braked=hypotf(pose.velocity[0],pose.velocity[2]);
    assert(braked<coasted*0.5f); ++checks;
    const uint64_t paused=pose.tick;
    input.clear(); assert(helper.Request(6,1,input,bytes) && skatewire::Decode(bytes,pose));
    assert(pose.tick==paused); ++checks;
    activate(); drive(120,true,false,false,false);
    const float beforeLeft=atan2f(-pose.forward[2],pose.forward[0]);
    drive(30,false,false,true,false);
    const float turnedLeft=atan2f(-pose.forward[2],pose.forward[0])-beforeLeft;
    activate(); drive(120,true,false,false,false);
    const float beforeRight=atan2f(-pose.forward[2],pose.forward[0]);
    drive(30,false,false,false,true);
    const float turnedRight=atan2f(-pose.forward[2],pose.forward[0])-beforeRight;
    assert(fabsf(turnedLeft)>0.01f && fabsf(turnedRight)>0.01f && turnedLeft*turnedRight<0); ++checks;
    printf("{\"input_metrics\":true,\"push_m_s\":%.5f,\"coast_m_s\":%.5f,\"brake_m_s\":%.5f,\"left_radians\":%.5f,\"right_radians\":%.5f}\n",
        pushed,coasted,braked,turnedLeft,turnedRight);
    // A stale epoch terminates only this test's owned helper job, never WoW/server.
    InterlockedIncrement(&epoch);
    Sleep(40);
    std::vector<unsigned char> step; skatewire::Word(step,1);
    assert(!helper.Request(2,1,step,bytes)); ++checks;
    helper.Stop();
    printf("{\"passed\":true,\"checks\":%u,\"parent\":\"x86\",\"helper\":\"x64\",\"ticks\":300,\"ground\":true,\"epoch_cancellation\":true}\n",checks);
    return 0;
}
