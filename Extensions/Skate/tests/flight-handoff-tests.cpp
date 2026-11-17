#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include "flight-handoff.h"
#include "ollie-controller.h"
static unsigned checks=0;
static void Check(bool ok,const char* why) { ++checks; if (!ok) { fprintf(stderr,"FAIL flight-handoff %u %s\n",checks,why); exit(1); } }
struct NativeFixture { unsigned flags=0,ms=0,resumes=0,lands=0; float down=0; };
using ResumeFn=int (__thiscall*)(void*,float);
using LandFn=void (__thiscall*)(void*);
__declspec(noinline) static int __fastcall ResumeFixture(void* p,void*,float down) {
    auto& n=*static_cast<NativeFixture*>(p); n.flags|=0x1000; n.ms=0; n.down=down; ++n.resumes; return 1;
}
__declspec(noinline) static void __fastcall LandFixture(void* p,void*) {
    auto& n=*static_cast<NativeFixture*>(p); n.flags&=~0x3000u; ++n.lands;
}
int main() {
    using flighthandoff::Action;
    ollie::Flight flight;
    Check(flight.Begin(1000,4,63,90.53877f),"recorded flight");
    Check(!flight.Check(1156,4,0x80000001,false,91.01669f,NAN,true),"recorded legacy acknowledgement failure reproduced");
    Check(flight.Check(1156,4,0x80000001,false,91.01669f,NAN,true,true),"fresh owned Source can remain airborne without native flag acknowledgement");
    Check(!flight.Check(1156,5,0x80000001,false,91,NAN,true,true)
        && !flight.Check(1156,4,0x80000801,false,91,NAN,true,true)
        && !flight.Check(1156,4,0x80000001,true,91,NAN,true,true),"epoch root spline protections retained");
    Check(flight.Check(9000,4,0x80000001,false,91,NAN,true,true)
        && !flight.Check(9000,4,0x80000001,false,91,NAN,true,false),"fresh Source-owned flight has no old8s deadline; legacy watchdog remains");
    flighthandoff::State state;
    Check(state.Observe(1156,4,6935,true,false,true,0x80000001,false,91.5f,91.01669f,156)==Action::ResumeAir,"recorded rising-ground mismatch resumes Source fall");
    NativeFixture native{0x80000001,1,0,0,0};
    const auto resume=reinterpret_cast<ResumeFn>(&ResumeFixture);
    const auto land=reinterpret_cast<LandFn>(&LandFixture);
    Check(resume(&native,-6.34665f)==1 && native.down==-6.34665f && native.ms==0 && (native.flags&0x1000),"stock ret4 initializer consumes existing Source velocity without extra impulse");
    Check(state.Observe(1172,4,6936,true,false,true,0,false,91.7f,91.1f,172)==Action::Keep,"contact retry bounded to75ms");
    Check(state.Observe(1231,4,6940,true,false,true,0,false,92,91.2f,231)==Action::ResumeAir,"later interrupted native flight can resume");
    Check(state.Observe(1240,4,6941,false,false,true,0,false,92,91.2f,240)==Action::Refuse,"stale Source cannot keep or resume flight");
    Check(state.Observe(1250,4,6942,true,false,true,0,false,95,91,250)==Action::Refuse,"vertical coherence unchanged");
    state.Clear();
    Check(state.Observe(1050,4,1,true,false,true,0,false,92,91,50)==Action::Keep,"queued native jump gets normal initial acknowledgement interval");
    float heights[5]={10,10.02f,9.98f,10.01f,9.99f}; bool valid[5]={true,true,true,true,true};
    Check(flighthandoff::Supported(10,heights,valid),"real nearby support");
    Check(!flighthandoff::Supported(10.2f,heights,valid),"no midair snap to remote ground");
    valid[0]=false; Check(!flighthandoff::Supported(10,heights,valid),"center hole blocks landing"); valid[0]=true;
    valid[1]=valid[2]=valid[3]=valid[4]=false;
    Check(!flighthandoff::Supported(10,heights,valid) && flighthandoff::Supported(10,heights,valid,1),"falling body needs distributed support; native ground permits center proof");
    state.Clear();
    Check(state.Observe(1500,4,100,true,true,false,0x3000,false,10,10,500)==Action::Keep,"Source ground alone cannot finish native fall");
    Check(state.Observe(1516,4,101,true,true,false,0x3000,true,10,10,516)==Action::Keep,"first actual contact");
    Check(state.Observe(1530,4,101,true,true,false,0x3000,true,10,10,530)==Action::Keep && state.groundSamples==1,"duplicate render frames do not confirm landing");
    Check(state.Observe(1532,4,102,true,true,false,0x3000,true,10,10,532)==Action::Keep,"second actual source contact");
    Check(state.Observe(1548,4,103,true,true,false,0x3000,true,10,10,548)==Action::Land,"confirmed Source and measured native support release stale falling");
    native.flags=0x80003001; land(&native);
    Check(!(native.flags&0x3000) && native.lands==1,"normal native void/ret0 landing handler fixture");
    state.Clear();
    state.Observe(2000,4,1,true,true,false,0x3000,true,10,10,1000);
    state.Observe(2016,4,2,true,true,false,0x3000,true,10,10,1016);
    Check(state.Observe(2032,4,3,true,true,false,0x3000,false,10,10,1032)==Action::Keep && state.groundSamples==0,"lost support resets confirmation");
    Check(state.Observe(2048,4,4,true,false,true,0x1000,true,10,10,1048)==Action::Keep && state.groundSamples==0,"new airborne Source cannot be landed by floor proximity");
    unsigned transitions=0;
    for (unsigned frameMs:{4u,8u,16u,25u,50u}) {
        state.Clear(); uint64_t tick=0; unsigned contacts=0,resumes=0;
        for (unsigned ms=0;ms<800;ms+=frameMs) {
            tick=1+ms/17;
            const bool air=ms<400;
            const auto action=state.Observe(1000+ms,1,tick,true,!air,air,0,ms>=420,air?10.3f:10,10,ms);
            if (action==Action::Land) { ++contacts; break; }
            if (action==Action::ResumeAir) ++resumes;
            Check(action!=Action::Refuse,"variable clocks remain owned"); ++transitions;
        }
        Check(contacts==1 && resumes>0,"takeoff and verified contact work across render clocks");
    }
    printf("{\"passed\":true,\"checks\":%u,\"clock_transitions\":%u,\"recorded_156ms_failure_reproduced\":true,\"source_authority_recovery_tested\":true,\"no_second_impulse\":true,\"verified_contact_required\":true,\"distinct_ground_samples\":3,\"native_resume_and_land_abi_tested\":true,\"stale_epoch_spline_root_watchdog_tested\":true,\"raw_flag_or_coordinate_writes\":false,\"live_verified\":false}\n",checks,transitions);
    return 0;
}
