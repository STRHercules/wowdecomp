#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "skate-ground-motion.h"
#include "air-root-motion.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why) { ++checks; if (!ok) { fprintf(stderr,"FAIL air-root engine %u %s\n",checks,why); exit(1); } }
int main() {
    volatile LONG epoch=1; skatewire::Pipe helper;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\air-root-engine-helper.log",1,&epoch),"helper");
    std::vector<unsigned char> input,reply,empty;
    float maximum=0; unsigned flights=0,landings=0,airSteps=0,unlocked=0;
    for (unsigned scenario=0;scenario<4;++scenario) {
        input.clear(); for (float v:{0.f,0.f,0.f,0.f}) skatewire::Float(input,v);
        skatewire::Word(input,2);
        for (float v:{-1000.f,0.f,-1000.f,-1000.f,0.f,1000.f,1000.f,0.f,1000.f,
            -1000.f,0.f,-1000.f,1000.f,0.f,1000.f,1000.f,0.f,-1000.f}) skatewire::Float(input,v);
        skatewire::Pose pose{};
        Check(helper.Request(1,1,input,reply) && skatewire::Decode(reply,pose),"flat init");
        Check(helper.Request(7,1,empty,reply),"schema");
        const auto mesh=skatevisual::Schema(reply); Check(mesh!=nullptr,"mesh");
        input.clear(); skatewire::Float(input,12.8016f); Check(helper.Request(11,1,input,reply),"14yd cap");
        input.clear(); skatewire::Float(input,1.8f); skatewire::Float(input,1.5f); Check(helper.Request(13,1,input,reply),"reference push");
        input.clear(); skatewire::Float(input,.65f); Check(helper.Request(21,1,input,reply),"reference gravity");
        airroot::History history; float x=0,y=0,facing=0,speed=0;
        bool sawAir=false,finished=false; unsigned groundFrames=0;
        for (unsigned frame=0;frame<200;++frame) {
            const int16_t rx=scenario==0?0:frame>=110 && frame<112?(scenario==1?-23405:23405):0;
            const int16_t ry=frame>=85 && frame<110?-32768:frame>=110 && frame<112?23780:0;
            const int16_t steer=frame<110 || finished?0:scenario==0?0:scenario==1?32767:scenario==2?-32768:frame<125?32767:-32768;
            skatewire::TrickStep(input,3,frame>=10 && frame<85,false,false,false,0,steer,rx,ry);
            // Same routing as the adapter: no native feedback during pending pop/owned flight.
            const bool feedback=frame<110 || finished;
            if (feedback) {
                skatewire::Float(input,x*.9144f); skatewire::Float(input,-y*.9144f);
                skatewire::Float(input,facing); skatewire::Float(input,speed*.9144f);
                skatewire::Word(input,0); skatewire::Word(input,20);
            } else ++unlocked;
            for (unsigned step=0;step<3;++step) {
                skatewire::Word(input,static_cast<uint32_t>(static_cast<int32_t>(rx)));
                skatewire::Word(input,static_cast<uint32_t>(static_cast<int32_t>(ry)));
            }
            Check(helper.Request(feedback?23:22,1,input,reply),"full-stick source trajectory");
            const auto visual=skatevisual::Decode(reply,*mesh); Check(visual!=nullptr,"visual"); pose=visual->pose;
            const bool air=!pose.ground || pose.airborne;
            if (air) sawAir=true;
            if (sawAir && !air && !finished) { if (++groundFrames>=2) { finished=true; ++landings; } }
            else if (air) groundFrames=0;
            const unsigned stepMs=static_cast<unsigned>(lroundf(3000*pose.period));
            const unsigned now=1000+frame*stepMs;
            Check(history.Step(1,pose.tick,pose.period,pose.position[0]/.9144f,-pose.position[2]/.9144f,now),"actual root publish");
            const auto motion=skatemotion::Ground(pose.velocity[0],pose.velocity[2],true,false,1,1,now,now,14,true);
            Check(motion.valid,"source velocity/facing"); facing=motion.heading; speed=motion.speed;
            if (frame>=110 && !finished) {
                airroot::Point direction{1,0,0}; float distance=0;
                // Native sweep is NOT invoked by this replay: free-space commit fixture only.
                const bool planned=history.FallRequest(now,1,stepMs,x,y,distance,direction);
                if (!planned) fprintf(stderr,"ROOT_TRACE scenario=%u frame=%u period=%.7f tick=%llu native=%.5f,%.5f root=%.5f,%.5f rate=%.5f,%.5f gap=%.5f air=%u\n",
                    scenario,frame,pose.period,pose.tick,x,y,history.x,history.y,history.vx,history.vy,hypotf(x-history.x,y-history.y),air?1u:0u);
                Check(planned,"bounded native distance/direction fall request");
                x+=direction.x*distance; y+=direction.y*distance; ++airSteps;
            } else { x+=cosf(facing)*speed*3*pose.period; y+=sinf(facing)*speed*3*pose.period; }
            const float gap=hypotf(x-history.x,y-history.y); maximum=(std::max)(maximum,gap);
        }
        Check(sawAir && finished,"real engine airborne and landed"); ++flights;
    }
    Check(maximum<1.5f && unlocked>40 && airSteps>40,"coherent unlocked air root");
    printf("{\"passed\":true,\"checks\":%u,\"flights\":%u,\"landings\":%u,\"air_steps\":%u,\"unlocked_steps\":%u,\"max_tracking_gap_yards\":%.6f,\"native_collision_live_tested\":false,\"live_verified\":false}\n",checks,flights,landings,airSteps,unlocked,maximum);
}
