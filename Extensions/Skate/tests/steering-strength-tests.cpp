#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-pipe.h"
#include "ground-tuning.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL steering check %u\n",checks); exit(1); } }
int main() {
    volatile LONG epoch=1;
    skatewire::Pipe helper;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\steering-strength-helper.log",1,&epoch));
    std::vector<unsigned char> bytes,reply;
    for (float v : {0.f,0.f,0.f,1.57079632679f}) skatewire::Float(bytes,v);
    skatewire::Word(bytes,2);
    for (float v : {-200.f,0.f,-200.f,-200.f,0.f,200.f,200.f,0.f,200.f,
        -200.f,0.f,-200.f,200.f,0.f,200.f,200.f,0.f,-200.f}) skatewire::Float(bytes,v);
    skatewire::Pose pose{};
    Check(helper.Request(1,1,bytes,reply) && skatewire::Decode(reply,pose));
    float turns[2]{};
    for (unsigned trial=0;trial<2;++trial) {
        bytes.clear(); for (float v : {0.f,0.f,0.f,1.57079632679f}) skatewire::Float(bytes,v);
        Check(helper.Request(4,1,bytes,reply) && skatewire::Decode(reply,pose));
        bytes.clear(); skatewire::Float(bytes,groundtuning::MaxYards*.9144f);
        Check(helper.Request(11,1,bytes,reply) && skatewire::Decode(reply,pose));
        bytes.clear(); skatewire::Float(bytes,groundtuning::PushMultiplier); skatewire::Float(bytes,groundtuning::AccelerationMultiplier);
        Check(helper.Request(13,1,bytes,reply) && skatewire::Decode(reply,pose));
        for (unsigned i=0;i<100;++i) {
            skatewire::InputStep(bytes,3,i>=40,false,false,false);
            Check(helper.Request(5,1,bytes,reply) && skatewire::Decode(reply,pose) && pose.ground && !pose.airborne);
        }
        const float heading=atan2f(-pose.velocity[2],pose.velocity[0]);
        for (unsigned i=0;i<40;++i) {
            skatewire::InputStep(bytes,3,false,false,true,false,trial?groundtuning::SteeringAxis:18000);
            const int wanted=trial?-groundtuning::SteeringAxis:-18000;
            Check(static_cast<int>(skatewire::Get(bytes.data()+8))==wanted);
            Check(helper.Request(5,1,bytes,reply) && skatewire::Decode(reply,pose) && pose.ground && !pose.airborne);
        }
        const float end=atan2f(-pose.velocity[2],pose.velocity[0]);
        turns[trial]=atan2f(sinf(end-heading),cosf(end-heading))*57.2957795f;
    }
    fprintf(stderr,"Measured steering: partial %.4fdeg, full %.4fdeg\n",turns[0],turns[1]);
    Check(turns[0]*turns[1]>0 && fabsf(turns[1])>fabsf(turns[0])*1.25f);
    skatewire::InputStep(bytes,3,false,false,true,true,groundtuning::SteeringAxis);
    Check(skatewire::Get(bytes.data()+8)==0);
    printf("{\"passed\":true,\"checks\":%u,\"steering_seconds\":2,\"partial_turn_degrees\":%.4f,\"full_turn_degrees\":%.4f,\"full_axis\":%d,\"live_verified\":false}\n",checks,turns[0],turns[1],groundtuning::SteeringAxis);
}
