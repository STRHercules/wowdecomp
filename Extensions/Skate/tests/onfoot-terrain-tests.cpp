#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "onfoot-terrain.h"
#include "ground-transition.h"
#include "skate-ground-motion.h"
#include "trick-state.h"
#include "skate-visual.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL foot terrain %u %s\n",checks,why);exit(1);}}
static bool Proof(float nativeGap,float sourceGap,float rootGap,unsigned flags=0x80001001){
    return onfootterrain::Supported(true,500,true,true,flags,false,nativeGap,sourceGap,rootGap);
}
int main(){
    // Recorded native/root gaps reproduce the old rejection. The source-floor
    // query is a NEW required live measurement, represented by a fixture here.
    unsigned recorded=0;
    for(auto values:std::vector<std::vector<float>>{{.272308f,-.226898f},{.126984f,.167206f}}){
        Check(!skatemotion::GroundContactWait(0x80001001,false,true,values[0],15),"recorded native contact refusal reproduced");
        const bool measured=Proof(values[0],.1f,values[1]);
        Check(measured && onfootterrain::ContactWait(onfootterrain::Ground(measured,500,true,false),0x80001001,15),"measured onfoot contact can hand back to stock fall/ground");++recorded;
    }
    for(float bad:{NAN,INFINITY,-.126f,.501f}){
        Check(!Proof(bad,.1f,0),"unmeasured/deep native gap refused");
        Check(!Proof(.1f,bad,0),"source floor also required");
    }
    Check(!Proof(.1f,.1f,.501f) && !Proof(.1f,.1f,-.501f),"source/native root coherence kept tighter than overall guard");
    for(unsigned flags:{0x100u,0x200u,0x400u,0x800u,0x2000u,0x100000u,0x200000u,0x1000000u,0x2000000u})Check(!Proof(.1f,.1f,0,flags|0x1000),"unsafe movement flags cannot use foot permission");
    for(unsigned state:{0u,100u,200u,300u,499u,504u,702u})Check(!onfootterrain::Supported(true,state,true,true,0x1000,false,.1f,.1f,0),"riding/recovery/unknown cannot inherit foot terrain permission");
    Check(!onfootterrain::Supported(false,500,true,true,0x1000,false,.1f,.1f,0),"disabled feature unchanged");
    Check(!onfootterrain::Supported(true,500,false,true,0x1000,false,.1f,.1f,0),"speed lease required");
    Check(!onfootterrain::Supported(true,500,true,false,0x1000,false,.1f,.1f,0),"fresh paired same epoch/tick required");
    Check(!onfootterrain::Supported(true,500,true,true,0x1000,true,.1f,.1f,0),"spline refused");
    Check(!onfootterrain::ContactWait(true,0x1000,250) && !onfootterrain::ContactWait(false,0x1000,0),"wait remains bounded and measured");
    Check(!onfootterrain::Ground(true,501,false,true),"jump not reclassified as ground");
    const auto probe=groundtransition::Refine(groundtransition::Predict(4,.016667f,0));
    float heights[groundtransition::DenseIntervals+1]{};bool valid[groundtransition::DenseIntervals+1]{};
    const auto ramp=[](float distance,float,float,float,float& h){h=483.154633f+distance;return true;};
    Check(!groundtransition::Sample(probe,483.299896f,true,heights,valid,ramp),"recorded .145yard profile start gap still refused for ordinary board");
    Check(groundtransition::Sample(probe,483.299896f,true,heights,valid,ramp,.5f),"measured foot support tolerates transient current-foot gap");++recorded;
    Check(!groundtransition::Sample(probe,483.299896f,true,heights,valid,[](float d,float,float,float,float& h){h=483.154633f+d;return d<.1f;},.5f),"missing contour never filled in");
    Check(!groundtransition::Sample(probe,483.299896f,true,heights,valid,[](float d,float,float,float,float& h){h=483.154633f+(d>.1f?1.f:0.f);return true;},.5f),"abrupt unsupported step remains blocked");
    const auto overhead=[](float,float base,float rise,float,float& h){h=rise>=.494233f?base+.494233f:501.631683f;return true;};
    Check(!groundtransition::Sample(probe,501.631683f,true,heights,valid,overhead,.5f) && heights[0]==501.631683f,"foot profile starts on lower floor and blocks unsupported upper step instead of losing current support");
    Check(onfootterrain::QueryRise<.494233f,"both onfoot support queries exclude recorded overhead surface");
    volatile LONG epoch=1;skatewire::Pipe helper;std::vector<unsigned char> input,reply,empty;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\onfoot-terrain-helper.log",1,&epoch),"original helper");
    unsigned routes=0,groundFrames=0,accepted=0,airFrames=0;float maxSourceGap=-100,minSourceGap=100;
    for(float grade:{.15f,.35f}){
        std::vector<float> vertices;
        const auto quad=[&](float z0,float z1,float y0,float y1){
            const float a[]={-40,y0,z0,-40,y1,z1,40,y1,z1,-40,y0,z0,40,y1,z1,40,y0,z0};
            vertices.insert(vertices.end(),a,a+18);
        };
        quad(-60,0,0,0);quad(0,10,0,10*grade);quad(10,20,10*grade,10*grade);quad(20,30,10*grade,0);quad(30,150,0,0);
        input.clear();for(float v:{0.f,0.f,-6.f,0.f})skatewire::Float(input,v);skatewire::Word(input,static_cast<uint32_t>(vertices.size()/9));for(float v:vertices)skatewire::Float(input,v);
        Check(helper.Request(1,1,input,reply) && helper.Request(7,1,empty,reply),"actual ramp collision geometry");
        const auto mesh=skatevisual::Schema(reply);Check(mesh!=nullptr,"mesh");
        float peak=-100,end=0;unsigned ground=0,okay=0;
        for(unsigned f=0;f<430;++f){
            skatewire::TrickStep(input,3,false,false,false,false,0,0,0,0);
            const uint32_t button=f==40?0x8000u:0u;memcpy(input.data()+4,&button,4);
            const int32_t forward=f>=80 && f<380?32767:0;memcpy(input.data()+12,&forward,4);
            for(unsigned n=0;n<3;++n)skatewire::GrabSample(input,0,0,0,0);
            Check(helper.Request(34,1,input,reply),"real engine onfoot ramp steps");
            const auto bytes=reply;skatewire::Pose p;Check(skatewire::Decode({bytes.begin(),bytes.begin()+60},p),"pose");
            Check(helper.Request(24,1,empty,reply),"state");trickstate::State s;Check(trickstate::Decode(reply,p.tick,s),"paired state");
            Check(skatevisual::Decode(bytes,*mesh,onfootstate::Active(s.source))!=nullptr,"actual ramp walker independent board frame");
            if(f<80)continue;
            Check(onfootstate::Active(s.source),"original walker stays in onfoot states over ramps");
            peak=(std::max)(peak,p.position[1]);end=p.position[2];
            if(s.source==501)++airFrames;
            if(!onfootstate::Ground(s.source))continue;
            const float z=p.position[2],floor=z<=0?0:z<10?z*grade:z<=20?10*grade:z<30?(30-z)*grade:0;
            const float sourceGap=p.position[1]-floor;
            minSourceGap=(std::min)(minSourceGap,sourceGap);maxSourceGap=(std::max)(maxSourceGap,sourceGap);
            ++ground;++groundFrames;
            // Replay a measured transient Wrath gap at the same actual Source
            // root. This is a policy approximation, NOT live native physics.
            const bool measured=Proof(.145263f,sourceGap/.9144f,sourceGap/.9144f-.145263f);
            if(measured && onfootterrain::ContactWait(true,0x1000,27)){++okay;++accepted;}
        }
        fprintf(stderr,"ONFOOT_RAMP grade=%.3f peak_m=%.6f final_z_m=%.6f ground=%u supported_policy=%u source_gap_range_m=%.6f,%.6f\n",grade,peak,end,ground,okay,minSourceGap,maxSourceGap);
        Check(end>35 && peak>grade*9,"actual uphill plateau downhill traversal");
        Check(ground>200 && okay*100>=ground*95,"measured policy supports ordinary Source biped ramp roots");++routes;
    }
    printf("{\"passed\":true,\"checks\":%u,\"recorded_native_rejections\":%u,\"real_engine_ramp_routes\":%u,\"engine_ground_frames\":%u,\"supported_policy_frames\":%u,\"engine_air_frames\":%u,\"legacy_board_profile_unchanged\":true,\"both_floor_queries_required\":true,\"unsafe_and_stale_refused\":true,\"missing_and_abrupt_support_refused\":true,\"wait_limit_ms\":250,\"foot_floor_gap_yards\":0.5,\"native_step_height_overridden\":false,\"native_policy_approximated\":true,\"live_verified\":false}\n",checks,recorded,routes,groundFrames,accepted,airFrames);
}
