#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-pipe.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL authored collision check %u\n",checks); exit(1); } }
int main() {
    static_assert(sizeof(void*)==4,"Exercise the real x86-to-x64 boundary");
    volatile LONG epoch=1; skatewire::Pipe helper;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\authored-collision-helper.log",1,&epoch));
    const float origin[3]={-9038.17773f,-96.59671f,87.87754f};
    std::vector<unsigned char> payload,reply;
    skatewire::Word(payload,0); for (unsigned repeat=0;repeat<2;++repeat) for (float v:origin) skatewire::Float(payload,v);
    skatewire::Float(payload,1.57079632679f);
    skatewire::Pose pose{};
    Check(helper.Request(16,1,payload,reply) && skatewire::Decode(reply,pose));
    for (unsigned i=0;i<20;++i) {
        skatewire::InputStep(payload,3,false,false,false,false);
        const auto tick=pose.tick;
        Check(helper.Request(5,1,payload,reply) && skatewire::Decode(reply,pose) && pose.tick==tick+3);
    }
    Check(pose.ground && !pose.airborne);
    const auto before=pose;
    unsigned replacements=0;
    for (unsigned generation=2;generation<=5;++generation) {
        payload.clear(); skatewire::Word(payload,generation); skatewire::Word(payload,0);
        for (float v:origin) skatewire::Float(payload,v);
        skatewire::Float(payload,origin[0]+static_cast<float>(generation)*.5f);
        skatewire::Float(payload,origin[1]+static_cast<float>(generation)*.5f);
        skatewire::Float(payload,origin[2]);
        Check(helper.Request(17,1,payload,reply) && reply.empty());
        bool installed=false; const DWORD start=GetTickCount();
        while (GetTickCount()-start<5000) {
            payload.clear();
            Check(helper.Request(15,1,payload,reply) && reply.size()==8 && skatewire::Get(reply.data())==generation);
            if (skatewire::Get(reply.data()+4)==generation) { installed=true; ++replacements; break; }
            Sleep(10);
        }
        Check(installed);
        payload.clear(); Check(helper.Request(6,1,payload,reply) && skatewire::Decode(reply,pose));
        Check(pose.tick==before.tick);
        for (unsigned i=0;i<3;++i) { Check(fabsf(pose.position[i]-before.position[i])<1e-6f); Check(fabsf(pose.velocity[i]-before.velocity[i])<1e-6f); }
    }
    // Wrong map/epoch/origin cannot install replacement geometry.
    payload.clear(); skatewire::Word(payload,6); skatewire::Word(payload,1);
    for (unsigned repeat=0;repeat<2;++repeat) for (float v:origin) skatewire::Float(payload,v);
    Check(!helper.Request(17,1,payload,reply));
    payload[4]=0; const float wrongOrigin=origin[0]+1; memcpy(payload.data()+8,&wrongOrigin,4);
    Check(!helper.Request(17,1,payload,reply));
    memcpy(payload.data()+8,origin,4); Check(!helper.Request(17,2,payload,reply));
    payload.clear(); Check(helper.Request(6,1,payload,reply) && skatewire::Decode(reply,pose) && pose.tick==before.tick);
    for (unsigned i=0;i<30;++i) {
        skatewire::InputStep(payload,3,i<10,i>=10,false,false);
        const auto tick=pose.tick;
        Check(helper.Request(5,1,payload,reply) && skatewire::Decode(reply,pose) && pose.tick==tick+3);
    }
    helper.Stop();
    printf("{\"passed\":true,\"checks\":%u,\"authored_swaps\":%u,\"actor_reset\":false,\"owned_data\":true,\"live_client_verified\":false}\n",checks,replacements);
}
