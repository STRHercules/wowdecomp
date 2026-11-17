#include "control-ownership.h"
#include "movement-phase.h"
#include "ground-heading.h"
#include "motion-observation.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok){++checks;if(!ok){fprintf(stderr,"FAIL movement stop %u\n",checks);exit(1);}}
int main(){
    // Epoch548/tick36281: native FORWARD was gone (80000000) while the
    // adapter continued reporting bits10 and Source travelled past3 yards.
    Check(controlownership::ReassertForward(0x10,0x10,true,0x80000000,false));
    Check(!controlownership::ReassertForward(0x10,0x10,true,0x80000001,false));
    Check(!controlownership::ReassertForward(0,0x10,true,0x80000000,false));
    Check(!controlownership::ReassertForward(0x10,0,true,0x80000000,false));
    Check(!controlownership::ReassertForward(0x10,0x10,false,0x80000000,false));
    Check(!controlownership::ReassertForward(0x10,0x10,true,0x80000000,true));
    for(unsigned unsafe:{0x100u,0x200u,0x400u,0x800u,0x100000u,0x200000u,0x1000000u,0x2000000u})
        Check(!controlownership::ReassertForward(0x10,0x10,true,0x80000000|unsafe,false));
    // Exact control-event model: release the native latch before reasserting it.
    unsigned owned=0x10,flags=0x80000000,events=0;bool inputLatch=true;
    if(controlownership::ReassertForward(0x10,owned,true,flags,false)){
        inputLatch=false;owned&=~0x10u;++events;
    }
    if(!(owned&0x10u)){
        Check(!inputLatch);inputLatch=true;owned|=0x10u;flags|=1;++events;
    }
    Check(events==2 && owned==0x10 && (flags&1) && inputLatch);

    // Epoch696: requested travel -1.050716rad but native feedback heading
    // 3.233045rad. The old last-sent-only comparison missed external rotation.
    groundheading::Handoff heading;float out=0;
    Check(heading.Step(-1.050716f,-1.050716f,100,out));heading.Accepted(out);
    Check(!heading.Changed(out) && heading.Changed(out,3.233045f));
    Check(!heading.Changed(out,out));Check(!heading.Changed(out,out+.0002f));
    Check(!heading.Changed(out,NAN));
    for(unsigned i=0;i<360;++i){
        const float angle=i*groundheading::Turn/360;
        heading.Accepted(angle);
        Check(!heading.Changed(angle,angle+groundheading::Turn));
        Check(heading.Changed(angle,angle+.01f));
    }
    motionobservation::Speed observed;
    observed.Step(696,1000,-4941.53223f,-1224.92468f,13);
    Check(observed.Heading(3.233045f)==3.233045f);
    // These periodic log snapshots are250ms apart. Feed a100ms subinterval,
    // within the observer's normal interval, not a fabricated30yd/s jump.
    const float nextX=-4941.53223f+(-4940.13916f+4941.53223f)*.4f;
    const float nextY=-1224.92468f+(-1227.62939f+1224.92468f)*.4f;
    observed.Step(696,1100,nextX,nextY,13);
    const float travel=atan2f(nextY+1224.92468f,nextX+4941.53223f);
    Check(fabsf(observed.Heading(3.233045f)-travel)<.00001f);
    Check(fabsf(groundheading::Delta(observed.Heading(3.233045f),3.233045f))>1);
    observed.Step(697,1110,0,0,7);Check(observed.Heading(2)==2);
    observed.Step(697,1150,100,0,7);Check(observed.Heading(2)==2);

    // Two actual failed profiles: ground100, fresh root and no native fall
    // flag. Neither is a Source bail; enter root-driven swept transport rather
    // than demand that walking rays prove a continuous native floor.
    for(float gap:{-.344055f,.255798f}){
        Check(!movementphase::GroundRoot(true,true,100,true,false,0x80000001,false,gap));
        Check(movementphase::ProfileRoot(true,true,100,true,false,0x80000001,false,gap));
        Check(!movementphase::ProfileRoot(true,false,100,true,false,0x80000001,false,gap));
        Check(!movementphase::ProfileRoot(false,true,100,true,false,0x80000001,false,gap));
        Check(!movementphase::ProfileRoot(true,true,100,true,false,0x80000801,false,gap));
        Check(!movementphase::ProfileRoot(true,true,100,true,false,0x80000001,true,gap));
        for(unsigned state:{200u,300u,301u,302u,500u,502u,600u,700u,702u})
            Check(!movementphase::ProfileRoot(true,true,state,true,false,0x80000001,false,gap));
    }
    Check(!movementphase::ProfileRoot(true,true,100,true,false,0x80000001,false,3));
    Check(!movementphase::ProfileRoot(true,true,100,true,false,0x80000001,false,NAN));

    // Source landed at7089 but native walking updates could resume before
    // verified floor contact. Re-enter the existing root arc before the
    // subsequently logged >3yard height mismatch; do NOT widen that guard.
    flighthandoff::State landing;
    using flighthandoff::Action;
    Check(landing.Observe(1000,1,7089,true,true,false,0x84000001,false,499.6f,499.6f,1100)==Action::ResumeAir);
    Check(landing.Observe(1016,1,7090,true,true,false,0x84000001,false,499.5f,499.5f,1116)==Action::Keep);
    Check(landing.Observe(1080,1,7094,true,true,false,0x84000001,false,499.4f,499.4f,1180)==Action::ResumeAir);
    Check(landing.Observe(1100,1,7095,true,true,false,0x80001001,false,499.4f,499.4f,1200)==Action::Keep);
    Check(landing.Observe(1116,1,7096,true,true,false,0x80000001,true,499.4f,499.4f,1216)==Action::Keep);
    Check(landing.Observe(1132,1,7097,true,true,false,0x80000001,true,499.4f,499.4f,1232)==Action::Keep);
    Check(landing.Observe(1148,1,7098,true,true,false,0x80000001,true,499.4f,499.4f,1248)==Action::Land);
    Check(landing.Observe(1164,1,7103,true,true,false,0x84000001,false,498.882080f,495.857605f,1264)==Action::Refuse);
    printf("{\"passed\":true,\"checks\":%u,\"recorded_stops\":5,\"native_forward_ownership\":true,\"external_facing_recovery\":true,\"travel_not_model_feedback\":true,\"paired_profile_root\":true,\"unsettled_ground_root_resumes\":true,\"coherence_bounds_unchanged\":true,\"full_input_replay\":false,\"live_verified\":false}\n",checks);
}
