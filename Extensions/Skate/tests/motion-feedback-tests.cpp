#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "skate-ground-motion.h"
#include "ground-heading.h"
#include "skate-vertical.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok) { ++checks; if (!ok) { fprintf(stderr,"FAIL feedback %u\n",checks); exit(1); } }
int main() {
    volatile LONG epoch=1; skatewire::Pipe helper;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\motion-feedback-helper.log",1,&epoch));
    std::vector<unsigned char> input,reply,empty;
    for (float v:{0.f,0.f,0.f,0.f}) skatewire::Float(input,v);
    skatewire::Word(input,2);
    for (float v:{-1000.f,0.f,-1000.f,-1000.f,0.f,1000.f,1000.f,0.f,1000.f,
        -1000.f,0.f,-1000.f,1000.f,0.f,1000.f,1000.f,0.f,-1000.f}) skatewire::Float(input,v);
    skatewire::Pose pose{};
    Check(helper.Request(1,1,input,reply) && skatewire::Decode(reply,pose));
    Check(helper.Request(7,1,empty,reply));
    const auto mesh=skatevisual::Schema(reply); Check(mesh!=nullptr);
    input.clear(); skatewire::Float(input,12.8016f); Check(helper.Request(11,1,input,reply));
    input.clear(); skatewire::Float(input,1.8f); skatewire::Float(input,1.5f); Check(helper.Request(13,1,input,reply));
    float x=0,y=0,facing=0,speed=0,maxGap=0;
    groundheading::Handoff heading;
    for (unsigned i=0;i<900;++i) {
        const auto previous=pose;
        skatewire::OllieStep(input,3,i>=40 && i<850,false,false,false,0,
            i>=200 && i<450?32767:i>=500 && i<750?-32768:0,0);
        skatewire::Float(input,x*.9144f); skatewire::Float(input,-y*.9144f);
        skatewire::Float(input,facing); skatewire::Float(input,speed*.9144f);
        skatewire::Word(input,0); skatewire::Word(input,0);
        Check(helper.Request(19,1,input,reply));
        const auto frame=skatevisual::Decode(reply,*mesh); Check(frame!=nullptr); pose=frame->pose;
        const float seconds=3*pose.period;
        const auto now=static_cast<uint32_t>(i*seconds*1000);
        Check(pose.tick==previous.tick+3 && pose.ground && !pose.airborne);
        const auto raw=skatemotion::Ground(pose.velocity[0],pose.velocity[2],true,false,1,1,now,now,14);
        const auto target=skatemotion::Synchronize(raw,x,y,pose.position[0],pose.position[2],0,0,14);
        Check(target.valid && target.speed<=14);
        Check(heading.Step(target.heading,facing,now,facing)); speed=target.speed;
        x+=cosf(facing)*speed*seconds; y+=sinf(facing)*speed*seconds;
        maxGap=(std::max)(maxGap,hypotf(x-pose.position[0]/.9144f,y+pose.position[2]/.9144f));
    }
    Check(maxGap<2.5f);
    unsigned airFrames=0,landings=0; bool wasAir=false;
    skatevertical::Motor vertical;
    float nativeZ=0,maxVertical=0;
    unsigned verticalSteps=0,verticalContacts=0,nativeFallMs=0;
    float launchHeading=0,launchSpeed=0; unsigned heldUntil=0;
    for (unsigned i=900;i<1440;++i) {
        const unsigned cycle=(i-900)%100;
        const int16_t ry=i<1400 && cycle>=40 && cycle<65?-32768:
            i<1400 && cycle>=65 && cycle<67?32767:0;
        if (ry>0 && cycle==65) { heldUntil=i+18; launchHeading=facing; launchSpeed=speed; }
        const bool locked=i<heldUntil;
        skatewire::OllieStep(input,3,cycle<30,false,false,false,0,cycle>=65 && cycle<83?18000:0,ry);
        skatewire::Float(input,x*.9144f); skatewire::Float(input,-y*.9144f);
        skatewire::Float(input,locked?launchHeading:facing); skatewire::Float(input,(locked?launchSpeed:speed)*.9144f);
        skatewire::Word(input,locked?1u:0u); skatewire::Word(input,0);
        Check(helper.Request(19,1,input,reply));
        const auto frame=skatevisual::Decode(reply,*mesh); Check(frame!=nullptr); pose=frame->pose;
        const bool air=pose.airborne!=0; if (air) ++airFrames; else if (wasAir) ++landings; wasAir=air;
        const float seconds=3*pose.period; const auto now=static_cast<uint32_t>(i*seconds*1000);
        if (air) {
            if (!wasAir || !vertical.active) { vertical.Clear(); nativeFallMs=0; nativeZ=0; }
            Check(vertical.Sample(1,now,pose.tick,pose.period,pose.velocity[1],false,nativeFallMs));
            float down=0,hitTime=0;
            Check(vertical.Delta(now,1,nativeFallMs,nativeFallMs+50,nativeZ,down));
            const float middle=vertical.segment.Height(.025f);
            if (fabsf(middle-nativeZ)>.00001f) {
                Check(vertical.segment.Contact(middle,hitTime));
                Check(fabsf(hitTime-(nativeFallMs*.001f+.025f))<.002f); ++verticalContacts;
            }
            nativeZ-=down; nativeFallMs+=50; ++verticalSteps;
            maxVertical=(std::max)(maxVertical,nativeZ);
            Check(std::isfinite(nativeZ) && fabsf(nativeZ)<10);
        } else vertical.Clear();
        const auto raw=skatemotion::Ground(pose.velocity[0],pose.velocity[2],pose.ground,pose.airborne,1,1,now,now,14,locked);
        const auto target=skatemotion::Synchronize(raw,x,y,pose.position[0],pose.position[2],0,0,14);
        Check(target.valid);
        if (locked) { facing=launchHeading; speed=launchSpeed; heading.heading=facing; heading.at=now; }
        else { Check(heading.Step(target.heading,facing,now,facing)); speed=target.speed; }
        x+=cosf(facing)*speed*seconds; y+=sinf(facing)*speed*seconds;
        maxGap=(std::max)(maxGap,hypotf(x-pose.position[0]/.9144f,y+pose.position[2]/.9144f));
    }
    Check(landings==5 && airFrames>40 && maxGap<2.5f);
    Check(verticalSteps==airFrames && verticalContacts>40 && maxVertical>.1f);
    const auto heldPose=pose; float maxHoldDrift=0;
    for (unsigned i=0;i<200;++i) {
        const auto before=pose.tick;
        // Even conflicting push/pop input cannot accelerate a stopped helper.
        skatewire::OllieStep(input,3,true,false,false,false,0,0,32767);
        skatewire::Float(input,x*.9144f); skatewire::Float(input,-y*.9144f);
        skatewire::Float(input,facing); skatewire::Float(input,0);
        skatewire::Word(input,2); skatewire::Word(input,0);
        Check(helper.Request(19,1,input,reply));
        const auto frame=skatevisual::Decode(reply,*mesh); Check(frame!=nullptr); pose=frame->pose;
        Check(pose.tick==before+3 && pose.ground && !pose.airborne);
        Check(hypotf(pose.velocity[0],pose.velocity[2])<.00001f);
        maxHoldDrift=(std::max)(maxHoldDrift,hypotf(pose.position[0]-heldPose.position[0],pose.position[2]-heldPose.position[2]));
        if (i%20==0) {
            Check(helper.Request(20,1,empty,reply) && reply.size()==12);
            Check(skatewire::Get(reply.data()+8)>=100 && skatewire::Get(reply.data()+8)<=103);
            Check((static_cast<uint64_t>(skatewire::Get(reply.data()))|(static_cast<uint64_t>(skatewire::Get(reply.data()+4))<<32))==pose.tick);
        }
    }
    Check(maxHoldDrift<.5f);
    for (unsigned i=0;i<40;++i) {
        skatewire::OllieStep(input,3,true,false,false,false,0,0,0);
        skatewire::Float(input,x*.9144f); skatewire::Float(input,-y*.9144f);
        skatewire::Float(input,facing); skatewire::Float(input,speed*.9144f);
        skatewire::Word(input,0); skatewire::Word(input,0);
        Check(helper.Request(19,1,input,reply));
        const auto frame=skatevisual::Decode(reply,*mesh); Check(frame!=nullptr); pose=frame->pose;
        const auto request=skatemotion::Ground(pose.velocity[0],pose.velocity[2],pose.ground,pose.airborne,1,1,i,i,14);
        Check(request.valid); facing=request.heading; speed=request.speed;
        x+=cosf(facing)*speed*3*pose.period; y+=sinf(facing)*speed*3*pose.period;
    }
    Check(hypotf(pose.velocity[0],pose.velocity[2])>2);
    printf("{\"passed\":true,\"checks\":%u,\"real_engine_turn_frames\":900,\"owned_jump_air_frames\":%u,\"owned_jump_landings\":%u,\"max_tracking_gap_yards\":%.6f,\"horizontal_feedback\":true,\"obstacle_hold_frames\":200,\"max_hold_drift_m\":%.6f,\"hold_release_push_tested\":true,\"vertical_source_steps\":%u,\"vertical_contact_inversions\":%u,\"max_vertical_yards\":%.6f,\"actor_reset\":false,\"live_verified\":false}\n",checks,airFrames,landings,maxGap,maxHoldDrift,verticalSteps,verticalContacts,maxVertical);
}
