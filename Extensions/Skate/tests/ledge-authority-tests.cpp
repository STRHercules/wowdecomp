#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ledge-authority.h"
#include "ollie-controller.h"
#include "vanilla-root.h"
#include "air-root-motion.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL ledge %u %s\n",checks,why);exit(1);}}
int main(){
    groundtuning::NaturalMomentum=true;
    // The live sample has thirteen hits with these endpoints. Interior heights
    // below are explicit fixtures, NOT claimed exact recorded controller replay.
    float heights[13]{};bool hits[13]{};
    for(unsigned i=0;i<13;++i){heights[i]=413.080475f-(413.080475f-411.855194f)*i/12;hits[i]=true;}
    Check(!groundtransition::Traversable(heights,hits,12,.057297f) || fabsf(413.245331f-heights[0])>.125f,"recorded endpoints fail older native walking criterion");
    Check(ledgeauthority::Ground(true,true,0x80000001,false,.2883f,100,true,false)
        && ledgeauthority::DownwardProfile(413.245331f,heights,hits,12),"recorded floor gap no longer requires a jump or stops momentum");
    unsigned drops=0;
    for(float down:{.3f,1.f,5.f,40.f}){
        const float stair[]={10,10,10-down,10-down,9-down};const bool valid[]={true,true,true,true,true};
        Check(!groundtransition::Traversable(stair,valid,4,.0625f),"true drop is not falsely declared traversable walking ground");
        Check(ledgeauthority::DownwardProfile(10,stair,valid,4),"descending steps are not a mandatory floor-ahead stop");++drops;
    }
    const float unknown[]={10,10,NAN,0,0};const bool missing[]={true,true,false,false,false};
    Check(ledgeauthority::DownwardProfile(10,unknown,missing,4),"unknown floor beyond a measured rim permits native movement, not invented support");
    const float uphill[]={10,10.2f,10.5f,11,11};const bool all[]={true,true,true,true,true};
    Check(!ledgeauthority::DownwardProfile(10,uphill,all,4),"uphill wall/step not misclassified as a drop");
    Check(!ledgeauthority::DownwardProfile(10,unknown,missing,0),"empty prediction refused");
    Check(!ledgeauthority::DownwardProfile(11,unknown,missing,4),"lost current support not hidden by future drop");
    bool noCurrent[]={false,true,false,false,false};
    Check(!ledgeauthority::DownwardProfile(10,unknown,noCurrent,4),"missing current support not invented");
    const float bad[]={10,INFINITY,9,8,7};
    Check(!ledgeauthority::DownwardProfile(10,bad,all,4),"invalid successful world result refused");
    for(unsigned state:{100u,103u})Check(ledgeauthority::Ground(true,true,0x80000001,false,.2883f,state,true,false),"explicit Source riding-ground ownership");
    for(unsigned state:{0u,200u,300u,400u,500u,702u})Check(!ledgeauthority::Ground(true,true,0x80000001,false,.1f,state,true,false),"no borrowed bail/unknown/offboard permission");
    for(uint32_t flags:{0x100u,0x200u,0x400u,0x800u,0x2000u,0x100000u,0x200000u,0x1000000u,0x2000000u})
        Check(!ledgeauthority::Ground(true,true,flags,false,.1f,100,true,false),"native unsafe/control-lost exclusions remain");
    Check(!ledgeauthority::Ground(false,true,0,false,0,100,true,false),"lease required");
    Check(!ledgeauthority::Ground(true,false,0,false,0,100,true,false),"fresh paired identity required");
    Check(!ledgeauthority::Ground(true,true,0,true,0,100,true,false),"server spline retained");
    Check(!ledgeauthority::Ground(true,true,0,false,.5f,100,true,false),"ground root mismatch bound retained");
    Check(!ledgeauthority::Ground(true,true,0,false,NAN,100,true,false),"finite root required");
    for(unsigned state:{200u,201u,202u})Check(ledgeauthority::Air(true,true,0x80001001,false,.1f,state,false,true),"actual Source riding-air, not native floor, proves departure");
    for(unsigned state:{0u,100u,103u,300u,400u,500u,702u})Check(!ledgeauthority::Air(true,true,0,false,0,state,false,true),"native missing-floor exception cannot manufacture Source air or hide a genuine bail");
    Check(!ledgeauthority::Air(true,false,0,false,0,200,false,true),"stale Source air refused");
    Check(!ledgeauthority::Air(true,true,0,false,0,200,true,true),"contradictory phase refused");
    Check(!ledgeauthority::Air(true,true,0,false,0,200,false,false),"actual airborne fact required");
    vanillaroot::History vertical;airroot::History horizontal;
    ollie::Flight flight;Check(flight.Begin(1000,1,1,10),"departure has one native flight owner");
    const float step=1.f/60;const float speed=12.f;
    for(unsigned tick=1;tick<=60;++tick){
        const float t=(tick-1)*step,z=10-5*t*t,x=speed*t;
        Check(vertical.Step(1,tick,step,z,1000+tick*16),"unchanged Source root height tracked");
        Check(horizontal.Step(1,tick,step,x,0,1000+tick*16),"unchanged Source momentum tracked");
        Check(flight.Check(1000+tick*16,1,0x80001001,false,z,NAN,true,true),"owned Source flight survives absence of nearby native floor");
        Check(fabsf(vertical.height-z)<.00001f && fabsf(horizontal.x-x)<.00001f,"no synthesized native parabola or changed momentum");
    }
    printf("{\"passed\":true,\"checks\":%u,\"drop_profiles\":%u,\"recorded_endpoints_tested\":true,\"unknown_floor_not_invented\":true,\"source_air_authority_tested\":true,\"unsafe_and_stale_exclusions\":true,\"source_motion_unchanged\":true,\"live_verified\":false}\n",checks,drops);
}
