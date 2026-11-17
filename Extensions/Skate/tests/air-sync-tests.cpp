#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "skate-ground-motion.h"
#include "trick-state.h"
#include "ollie-controller.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why) { ++checks; if (!ok) { fprintf(stderr,"FAIL air-sync %u %s\n",checks,why); exit(1); } }
int main() {
    volatile LONG epoch=1; skatewire::Pipe helper;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\air-sync-helper.log",1,&epoch),"helper");
    std::vector<unsigned char> input,reply,empty;
    float maximum=0; unsigned flights=0,landings=0,failures=0,airSamples=0,aligned=0;
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
        float x=0,y=0,facing=0,speed=0,launchHeading=0,launchSpeed=0,gap=0;
        bool locked=false,sawAir=false,finished=false;
        ollie::Launch launch;
        unsigned groundFrames=0;
        for (unsigned frame=0;frame<210;++frame) {
            if (frame==110) { locked=true; launchHeading=facing; launchSpeed=speed; Check(launch.Capture(facing,speed),"gesture capture"); }
            const int16_t rx=frame>=85 && frame<110?0:frame>=110 && frame<112?-23405:0;
            const int16_t ry=frame>=85 && frame<110?-32768:frame>=110 && frame<112?23780:0;
            const int16_t steer=frame<110 || finished?0:scenario==0?0:scenario==1?32767:scenario==2?-32768:frame<125?32767:-32768;
            skatewire::TrickStep(input,3,frame>=10 && frame<85,false,false,false,0,steer,rx,ry);
            skatewire::Float(input,x*.9144f); skatewire::Float(input,-y*.9144f);
            skatewire::Float(input,locked?launchHeading:facing); skatewire::Float(input,(locked?launchSpeed:speed)*.9144f);
            skatewire::Word(input,locked?1u:0u); skatewire::Word(input,20);
            for (unsigned step=0;step<3;++step) {
                skatewire::Word(input,static_cast<uint32_t>(static_cast<int32_t>(rx)));
                skatewire::Word(input,static_cast<uint32_t>(static_cast<int32_t>(ry)));
            }
            if (!helper.Request(23,1,input,reply)) { ++failures; fprintf(stderr,"AIR_SYNC scenario=%u rejected frame=%u gap=%.6f\n",scenario,frame,gap); break; }
            const auto visual=skatevisual::Decode(reply,*mesh); Check(visual!=nullptr,"visual"); pose=visual->pose;
            const bool air=!pose.ground || pose.airborne;
            if (air) { sawAir=true; ++airSamples; }
            if (locked && sawAir && !air) {
                if (++groundFrames>=2) { locked=false; finished=true; ++landings; }
            } else groundFrames=0;
            const auto request=skatemotion::Ground(pose.velocity[0],pose.velocity[2],true,false,1,1,frame,frame,14,true);
            Check(request.valid,"motion request");
            if (locked && air && !launch.takeoffAligned) {
                Check(launch.AlignTakeoff(request.heading),"one-shot Source takeoff alignment");
                launchHeading=launch.heading; launchSpeed=launch.speed; ++aligned;
            }
            if (locked) { facing=launchHeading; speed=launchSpeed; }
            else { facing=request.heading; speed=request.speed; }
            x+=cosf(facing)*speed*3*pose.period; y+=sinf(facing)*speed*3*pose.period;
            gap=hypotf(x-pose.position[0]/.9144f,y+pose.position[2]/.9144f);
            maximum=(std::max)(maximum,gap);
            if (frame>=110 && frame<145 && frame%5==0)
                fprintf(stderr,"AIR_TRACE s=%u frame=%u air=%u lock=%u gap=%.5f root=%.3f,%.3f native=%.3f,%.3f vel=%.3f,%.3f\n",scenario,frame,air?1u:0u,locked?1u:0u,gap,pose.position[0],pose.position[2],x*.9144f,-y*.9144f,pose.velocity[0],pose.velocity[2]);
        }
        if (sawAir) ++flights;
        fprintf(stderr,"AIR_SYNC scenario=%u air=%u landed=%u final_gap=%.6f max_gap=%.6f\n",scenario,sawAir?1u:0u,finished?1u:0u,gap,maximum);
    }
    // Replay the observed stale heading pair as a boundary fixture, not a
    // claim to reconstruct all collisions/Source state from the sparse log.
    ollie::Launch recorded;
    Check(recorded.Capture(-1.57517f,13.57756f),"recorded gesture momentum");
    const float sourceHeading=-1.33710f,sourceSpeed=14.f,seconds=1.1f;
    const float oldGap=hypotf(cosf(recorded.heading)*recorded.speed-cosf(sourceHeading)*sourceSpeed,
        sinf(recorded.heading)*recorded.speed-sinf(sourceHeading)*sourceSpeed)*seconds;
    Check(oldGap>3,"recorded direction difference can exhaust unchanged live guard");
    Check(recorded.AlignTakeoff(sourceHeading),"recorded actual-takeoff capture");
    Check(!recorded.AlignTakeoff(2),"airborne heading cannot continuously recapture");
    Check(recorded.heading==sourceHeading && recorded.speed==13.57756f,"one-shot Source heading and unchanged accepted speed");
    const float newGap=fabsf(recorded.speed-sourceSpeed)*seconds;
    Check(newGap<.5f,"recorded heading boundary removes lateral divergence without boosting speed");
    recorded.Landed(1000); Check(!recorded.AlignTakeoff(0),"landing does not reopen capture");
    recorded.Clear(); Check(!recorded.AlignTakeoff(0),"unowned takeoff refused");
    Check(recorded.Capture(1,7) && !recorded.AlignTakeoff(NAN) && recorded.heading==1 && recorded.speed==7,"nonfinite takeoff rejected without modifying momentum");
    Check(recorded.Capture(1,0) && recorded.AlignTakeoff(2) && recorded.speed==0,"stationary wobble cannot launch horizontal movement");
    const bool passed=failures==0 && flights==4 && landings==4 && aligned==4 && maximum<2.5f;
    printf("{\"passed\":%s,\"checks\":%u,\"full_flight_steering_scenarios\":4,\"flights\":%u,\"landings\":%u,\"takeoff_alignments\":%u,\"air_samples\":%u,\"request_failures\":%u,\"max_tracking_gap_yards\":%.6f,\"recorded_heading_boundary_old_gap_yards\":%.6f,\"recorded_full_route_reconstructed\":false,\"coherence_limit_yards\":3,\"live_verified\":false}\n",passed?"true":"false",checks,flights,landings,aligned,airSamples,failures,maximum,oldGap);
    return passed?0:1;
}
