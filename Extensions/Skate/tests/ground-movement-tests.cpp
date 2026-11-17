#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-pipe.h"
#include "skate-ground-motion.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL moving ground check %u\n",checks); exit(1); } }
static void Floor(std::vector<unsigned char>& bytes,float x,float z) {
    skatewire::Word(bytes,2);
    for (float v : {x-24,0.f,z-24,x-24,0.f,z+24,x+24,0.f,z+24,
        x-24,0.f,z-24,x+24,0.f,z+24,x+24,0.f,z-24}) skatewire::Float(bytes,v);
}
int main() {
    static_assert(sizeof(void*)==4,"Test the actual x86-to-x64 boundary");
    Check(skatemotion::PatchContains(17.9f,0,0,0));
    Check(!skatemotion::PatchContains(18,0,0,0));
    Check(skatemotion::PatchContains(30,0,30,0));
    Check(!skatemotion::PatchContains(NAN,0,0,0));
    Check(skatemotion::Coherent(101,201,.9144f,-.9144f,100,200));
    Check(!skatemotion::Coherent(104,200,0,0,100,200));
    unsigned totalSwaps=0,asyncSwaps=0; float totalDistance=0,baselinePushDistance=0,tunedPushDistance=0;
    for (float yards : {4.f,2.f,7.f,14.f}) {
        volatile LONG epoch=1;
        skatewire::Pipe helper;
        Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
            L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
            L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\ground-movement-helper.log",1,&epoch));
        std::vector<unsigned char> payload,reply;
        for (float v : {0.f,0.f,0.f,1.57079632679f}) skatewire::Float(payload,v);
        Floor(payload,0,0);
        skatewire::Pose pose{};
        Check(helper.Request(1,1,payload,reply) && skatewire::Decode(reply,pose));
        const float cap=yards*.9144f;
        payload.clear(); skatewire::Float(payload,cap);
        Check(helper.Request(11,1,payload,reply) && skatewire::Decode(reply,pose));
        if (yards==7) {
            for (unsigned i=0;i<40;++i) {
                skatewire::InputStep(payload,3,false,false,false,false);
                Check(helper.Request(5,1,payload,reply) && skatewire::Decode(reply,pose));
            }
            const auto settled=pose;
            for (unsigned i=0;i<30;++i) {
                skatewire::InputStep(payload,3,true,false,false,false);
                Check(helper.Request(5,1,payload,reply) && skatewire::Decode(reply,pose));
            }
            baselinePushDistance=hypotf(pose.position[0]-settled.position[0],pose.position[2]-settled.position[2]);
            payload.clear(); for (float v : {0.f,0.f,0.f,1.57079632679f}) skatewire::Float(payload,v);
            Check(helper.Request(4,1,payload,reply) && skatewire::Decode(reply,pose));
            payload.clear(); skatewire::Float(payload,groundtuning::PushMultiplier); skatewire::Float(payload,groundtuning::AccelerationMultiplier);
            Check(helper.Request(13,1,payload,reply) && skatewire::Decode(reply,pose));
        }
        if (yards==14) {
            // Same existing tuning as the installed client; only cap is doubled.
            payload.clear(); skatewire::Float(payload,groundtuning::PushMultiplier); skatewire::Float(payload,groundtuning::AccelerationMultiplier);
            Check(helper.Request(13,1,payload,reply) && skatewire::Decode(reply,pose));
        }
        float centreX=0,centreZ=0,maxSpeed=0,pushSpeed=0;
        float nextCentreX=0,nextCentreZ=0,settleX=0,settleZ=0;
        unsigned generation=1; bool pending=false;
        for (unsigned i=0;i<540;++i) {
            const bool push=i>=40 && i<380,brake=i>=460;
            skatewire::InputStep(payload,3,push,brake,i>=380 && i<410,i>=410 && i<440);
            const auto before=pose.tick;
            Check(helper.Request(5,1,payload,reply) && skatewire::Decode(reply,pose));
            Check(pose.tick==before+3 && pose.ground && !pose.airborne);
            if (yards==7 && i==39) { settleX=pose.position[0]; settleZ=pose.position[2]; }
            if (yards==7 && i==69) tunedPushDistance=hypotf(pose.position[0]-settleX,pose.position[2]-settleZ);
            const float speed=hypotf(pose.velocity[0],pose.velocity[2]);
            Check(speed<=cap+.0001f);
            maxSpeed=(std::max)(maxSpeed,speed);
            if (i==379) pushSpeed=speed;
            const auto request=skatemotion::Ground(pose.velocity[0],pose.velocity[2],pose.ground,pose.airborne,1,1,10,10,yards);
            Check(request.valid && request.speed<=yards+.0001f);
            if (pending) {
                payload.clear(); Check(helper.Request(15,1,payload,reply) && reply.size()==8);
                Check(skatewire::Get(reply.data())==generation);
                if (skatewire::Get(reply.data()+4)==generation) {
                    const auto previous=pose;
                    payload.clear(); Check(helper.Request(6,1,payload,reply) && skatewire::Decode(reply,pose));
                    Check(pose.tick==previous.tick);
                    for (unsigned c=0;c<3;++c) {
                        Check(fabsf(pose.position[c]-previous.position[c])<.00001f);
                        Check(fabsf(pose.velocity[c]-previous.velocity[c])<.00001f);
                    }
                    centreX=nextCentreX; centreZ=nextCentreZ; pending=false; ++totalSwaps; ++asyncSwaps;
                }
            }
            if (!pending && (fabsf(pose.position[0]-centreX)>4 || fabsf(pose.position[2]-centreZ)>4)) {
                if (yards>=7) {
                    ++generation; nextCentreX=pose.position[0]; nextCentreZ=pose.position[2];
                    payload.clear(); skatewire::Word(payload,generation); Floor(payload,nextCentreX,nextCentreZ);
                    Check(helper.Request(14,1,payload,reply) && reply.empty()); pending=true;
                } else {
                const auto previous=pose;
                centreX=pose.position[0]; centreZ=pose.position[2];
                payload.clear(); Floor(payload,centreX,centreZ);
                Check(helper.Request(12,1,payload,reply) && skatewire::Decode(reply,pose));
                Check(pose.tick==previous.tick);
                for (unsigned c=0;c<3;++c) {
                    Check(fabsf(pose.position[c]-previous.position[c])<.00001f);
                    Check(fabsf(pose.velocity[c]-previous.velocity[c])<.00001f);
                }
                ++totalSwaps;
                }
            }
            Check(skatemotion::PatchContains(pose.position[0]/.9144f,-pose.position[2]/.9144f,centreX/.9144f,-centreZ/.9144f));
        }
        fprintf(stderr,"Ground scenario cap %.4f m/s max %.4f push %.4f final %.4f\n",cap,maxSpeed,pushSpeed,hypotf(pose.velocity[0],pose.velocity[2]));
        Check(maxSpeed>cap*.95f && pushSpeed>cap*.9f);
        Check(hypotf(pose.velocity[0],pose.velocity[2])<pushSpeed*.5f);
        totalDistance+=hypotf(pose.position[0],pose.position[2]);
    }
    Check(totalSwaps>10 && totalDistance>50 && asyncSwaps>10);
    Check(tunedPushDistance>baselinePushDistance*1.05f);
    printf("{\"passed\":true,\"checks\":%u,\"scenarios\":4,\"max_cap_yards\":14,\"collision_swaps\":%u,\"async_swaps\":%u,\"distance_m\":%.3f,\"baseline_push_distance_m\":%.4f,\"tuned_push_distance_m\":%.4f,\"actor_reset\":false,\"live_world_movement_verified\":false}\n",checks,totalSwaps,asyncSwaps,totalDistance,baselinePushDistance,tunedPushDistance);
}
