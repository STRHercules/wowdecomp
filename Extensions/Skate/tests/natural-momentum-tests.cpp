#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "trick-state.h"
#include "run-speed-lease.h"
#include "skate-ground-motion.h"
#include "ground-transition.h"
#include "air-root-motion.h"
#include "motion-observation.h"
#include "ollie-controller.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL momentum %u %s\n",checks,why);exit(1);}}
int main(){
    groundtuning::NaturalMomentum=true;
    RunSpeedLease lease;
    Check(lease.Capture(7,70,70) && lease.Allows(35) && !lease.Allows(70.1f),"confirmed envelope without physics boost");
    Check(lease.original==7 && lease.applied==7,"allowance itself never applies running speed");
    lease.Clear();Check(lease.Capture(7,70,0) && !lease.Allows(15),"no acknowledgement no extra permission");
    lease.Clear();Check(!lease.Capture(7,70,14),"old confirmation cannot grant new envelope");
    unsigned highPaths=0;
    for(float speed:{16.55f,25.f,40.f,60.f}){
        auto move=skatemotion::Ground(speed*.9144f,0,true,false,1,1,1000,1000,70);
        Check(move.valid && fabsf(move.speed-speed)<.0001f,"natural speed not clamped to14");
        const auto contour=groundtransition::Predict(speed,1.f/60,32);
        Check(contour.valid && contour.distance>1. && contour.distance<=8 && contour.spacing<=.25f,"fast motion extends actual collision lookahead");
        const auto dense=groundtransition::Refine(contour);
        Check(dense.valid && dense.spacing<=.062501f,"fast contour preserves dense support resolution");
        float heights[groundtransition::DenseIntervals+1]{};bool valid[groundtransition::DenseIntervals+1]{};
        Check(groundtransition::Sample(dense,0,true,heights,valid,[](float d,float,float,float,float& z){z=-d*.4f;return true;}),"dense real slope support");
        Check(!groundtransition::Sample(dense,0,true,heights,valid,[](float d,float,float,float,float& z){z=-d*.4f;return d<.8f;}),"missing fast-route support remains blocked");
        airroot::History root;float x=0;uint64_t tick=1;
        Check(root.Step(1,tick,.02f,0,0,1000),"fast root start");
        for(unsigned ms=20;ms<=2400;ms+=20){
            Check(root.Step(1,++tick,.02f,speed*ms*.001f,0,1000+ms),"fast fresh source derivative");
            airroot::Point delta{0,0,-.02f};Check(root.Plan(1000+ms,1,20,x,0,delta),"fast source through native sweep plan");
            Check(delta.z==-.02f && delta.x<=1.40001f,"vertical and finite transport limit retained");x+=delta.x;
            Check(x>=root.x-.25f && x-root.x<speed*.025f+.25f,"no accumulating lag; native step predicts the next20ms");
        }
        ollie::Launch pop;Check(pop.Capture(0,speed) && pop.AlignTakeoff(.1f),"faster takeoff remains owned");++highPaths;
    }
    volatile LONG epoch=1;skatewire::Pipe helper;std::vector<unsigned char> input,reply,empty;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\natural-momentum-helper.log",1,&epoch),"real retained engine");
    float peaks[2]{},coastStart=0,coastPeak=0,maxNativeGap=0;unsigned engineFrames=0,pairedNativeSteps=0;
    for(unsigned natural=0;natural<2;++natural){
        input.clear();for(float v:{0.f,0.f,-8.f,0.f})skatewire::Float(input,v);skatewire::Word(input,4);
        const auto quad=[&](float a,float b,float ya,float yb){for(float v:{-100.f,ya,a,-100.f,yb,b,100.f,yb,b,-100.f,ya,a,100.f,yb,b,100.f,ya,a})skatewire::Float(input,v);};
        quad(-300,0,0,0);quad(0,400,0,-100);
        Check(helper.Request(1,1,input,reply) && helper.Request(7,1,empty,reply),"real flat-to-downhill geometry");
        auto mesh=skatevisual::Schema(reply);Check(mesh!=nullptr,"schema");
        if(natural){
            Check(!helper.Request(37,2,empty,reply),"stale epoch cannot arm natural motion");Check(helper.Request(37,1,empty,reply),"opt in without velocity injection");
            input.clear();skatewire::Float(input,12.8016f);Check(!helper.Request(11,1,input,reply),"natural session cannot silently restore old physics cap");
        }
        else{input.clear();skatewire::Float(input,12.8016f);Check(helper.Request(11,1,input,reply),"legacy14 baseline");}
        input.clear();skatewire::Float(input,1.8f);skatewire::Float(input,1.5f);Check(helper.Request(13,1,input,reply),"unchanged push and acceleration");
        input.clear();skatewire::Float(input,.65f);Check(helper.Request(21,1,input,reply),"unchanged gravity");
        airroot::History measured;float nativeX=0,nativeY=0;bool started=false;
        for(unsigned f=0;f<300;++f){
            skatewire::TrickStep(input,3,f>=10 && f<70,false,false,false,0,0,0,0);
            for(unsigned n=0;n<3;++n)skatewire::GrabSample(input,0,0,0,0);
            Check(helper.Request(34,1,input,reply),"real downhill stepping");
            const auto frame=skatevisual::Decode(reply,*mesh,true);Check(frame!=nullptr,"real finite downhill pose");
            const auto& p=frame->pose;const float speed=hypotf(p.velocity[0],p.velocity[2])/.9144f;
            Check(helper.Request(24,1,empty,reply),"paired actual downhill phase");trickstate::State state;
            Check(trickstate::Decode(reply,p.tick,state),"downhill phase tick");
            const bool riding=state.source>=100 && state.source<=202;
            if(natural && riding){
                const float x=p.position[0]/.9144f,y=-p.position[2]/.9144f;
                const uint32_t now=1000+static_cast<uint32_t>(lroundf(p.tick*p.period*1000));
                if(!started){nativeX=x;nativeY=y;started=true;}
                Check(measured.Step(1,p.tick,p.period,x,y,now),"actual downhill Source-root derivative");
                const unsigned ms=static_cast<unsigned>(lroundf(3000*p.period));
                airroot::Point delta{};Check(measured.Plan(now,1,ms,nativeX,nativeY,delta),"actual Source trajectory fits higher native transport envelope");
                nativeX+=delta.x;nativeY+=delta.y;
                const float gap=hypotf(nativeX-x,nativeY-y);maxNativeGap=(std::max)(maxNativeGap,gap);
                Check(gap<3,"actual downhill roots stay within original3yard tracking guard");++pairedNativeSteps;
            }else if(natural){measured.Clear();started=false;}
            if(riding)peaks[natural]=(std::max)(peaks[natural],speed);
            if(natural && f==70){Check(riding,"riding when coast starts");coastStart=speed;}
            if(natural && f>70 && riding)coastPeak=(std::max)(coastPeak,speed);
            ++engineFrames;
        }
    }
    helper.Stop();Check(peaks[0]<14.001f && peaks[1]>16 && coastPeak>coastStart+1,"real downhill gains momentum above old cap after pushing ends");
    printf("{\"passed\":true,\"checks\":%u,\"engine_request_frames\":%u,\"fast_tracking_paths\":%u,\"source_root_transport_steps\":%u,\"max_source_root_transport_gap_yards\":%.5f,\"legacy_peak_yards_s\":%.5f,\"natural_peak_yards_s\":%.5f,\"coast_start_yards_s\":%.5f,\"coast_peak_yards_s\":%.5f,\"transport_envelope_yards_s\":70,\"physics_speed_cap\":false,\"missing_support_refused\":true,\"native_collision_live_tested\":false,\"live_verified\":false}\n",checks,engineFrames,highPaths,pairedNativeSteps,maxNativeGap,peaks[0],peaks[1],coastStart,coastPeak);
}
