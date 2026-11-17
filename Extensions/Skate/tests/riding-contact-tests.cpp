#define NOMINMAX
#include "riding-contact.h"
#include "skate-ground-motion.h"
#include "flight-handoff.h"
#include "ledge-authority.h"
#include <array>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL riding contact %u %s\n",checks,why);exit(1);}}
static bool Support(float nativeGap,float sourceGap,float floorDelta=0,float rootGap=NAN,
    unsigned flags=0x80001001,unsigned state=100,bool fresh=true,bool lease=true,bool spline=false,bool ground=true,bool air=false){
    if(std::isnan(rootGap))rootGap=floorDelta+sourceGap-nativeGap;
    return ridingcontact::Supported(lease,fresh,flags,spline,state,ground,air,nativeGap,sourceGap,floorDelta,rootGap);
}
int main(){
    // Exact rejection fields; the second root floor was not captured by the old
    // build. Explicit paired-floor fixtures must NOT be described as live rays.
    for(const auto& event : {std::array<float,5>{.169525f,.201691f,.500795f,18,15},
        std::array<float,5>{.169739f,.106110f,0,8,0}}){
        Check(!skatemotion::GroundContactWait(0x80001001,false,true,event[0],static_cast<unsigned>(event[4])),"old contact check reproduces logged rejection");
        Check(!flighthandoff::SupportedDescent(true,true,true,false,0x80001001,false,event[0],event[1],event[2],static_cast<unsigned>(event[3])),"old descending-only fallback rejects level/rising Source contact");
        const float sourceGap=event[0]+event[1];
        Check(Support(event[0],sourceGap,0,event[1]),"measured matching floor fixture supports recorded root/native relationship");
        Check(ridingcontact::Wait(true,0x80001001,static_cast<unsigned>(event[4])),"current measured riding contact keeps short native handoff");
    }
    const float nativeGap=498.307617f-497.857727f,rootGap=-.503815f;
    const float sourceGap=nativeGap+rootGap;
    Check(!ledgeauthority::Ground(true,true,0x84000001,false,rootGap,100,true,false),"third logged landing rejected by older fixed half-yard root bound");
    Check(Support(nativeGap,sourceGap,0,rootGap,0x84000001),"third recorded relationship has two near-floor roots without widening missing-floor permission");
    const float heights[]={497.857727f,497.863f,497.869f,497.875f,497.881866f};
    const bool valid[]={true,true,true,true,true};
    Check(ledgeauthority::DownwardProfile(498.307617f,heights,valid,4),"recorded profile endpoints allow measured downward native continuation");
    for(unsigned state : {0u,200u,201u,202u,300u,400u,500u,702u})Check(!Support(.17f,.05f,0,NAN,0x80001001,state),"no borrowed air/ragdoll/onfoot/grind ownership");
    for(unsigned bit : {0x100u,0x200u,0x400u,0x800u,0x2000u,0x100000u,0x200000u,0x1000000u,0x2000000u})
        Check(!Support(.17f,.05f,0,NAN,0x80001001|bit),"native unsafe control and far-fall retained");
    Check(!Support(.17f,.05f,0,NAN,0x80001001,100,false),"stale or unpaired root refused");
    Check(!Support(.17f,.05f,0,NAN,0x80001001,100,true,false),"lease required");
    Check(!Support(.17f,.05f,0,NAN,0x80001001,100,true,true,true),"server spline refused");
    Check(!Support(.17f,.05f,0,NAN,0x80001001,100,true,true,false,false),"actual source ground required");
    Check(!Support(.17f,.05f,0,NAN,0x80001001,100,true,true,false,true,true),"contradictory phase refused");
    for(float gap : {NAN,INFINITY,-INFINITY,-.126f,.501f}){
        Check(!Support(gap,.05f),"missing/invalid/remote native floor refused");
        Check(!Support(.17f,gap),"missing/invalid/remote Source floor refused");
    }
    Check(!Support(.17f,.05f,.501f),"different distant platform not borrowed");
    Check(!Support(.17f,.05f,0,.2f),"mismatched root/ray records refused");
    for(unsigned age : {0u,15u,100u,249u})Check(ridingcontact::Wait(Support(.17f,.05f),0x80001001,age),"bounded supported contact wait");
    for(unsigned age : {250u,251u,1000u})Check(!ridingcontact::Wait(true,0x80001001,age),"wait cannot renew forever");
    Check(!ridingcontact::Wait(false,0x80001001,0),"unknown floor never accepted by wait");
    for(unsigned i=0;i<600;++i){
        const float g=(i%100)*.004f,s=.05f,delta=(static_cast<int>(i%11)-5)*.03f;
        Check(Support(g,s,delta),"measured step/contact height combinations");
    }
    printf("{\"passed\":true,\"checks\":%u,\"recorded_rejections_reproduced\":3,\"unrecorded_source_floor_is_fixture\":true,\"two_measured_floors_required\":true,\"original_wait_limit_ms\":250,\"missing_floor_unsafe_stale_and_bail_exclusions\":true,\"native_sweeps_unchanged\":true,\"live_verified\":false}\n",checks);
}
