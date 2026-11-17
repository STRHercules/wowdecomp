#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ragdoll-root-handoff.h"
#include "skate-vertical.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
static unsigned checks=0;
static void Check(bool ok,const char* reason){++checks;if(!ok){fprintf(stderr,"FAIL ragdoll handoff %u %s\n",checks,reason);exit(1);}}
struct Fixture {
    bailrecovery::Phase before,after;
    bailrecovery::Metadata metadata{759,1,0,true};
    skatewire::Pose pose{};
    vanillaroot::History vertical;
    airroot::History horizontal;
    // Tick759 Source XYZ and session origin are logged; apply the same actual
    // metres->yards/sign conversion as ShadowTick. Native sample/timing below
    // are a nearby synthetic collision-carrier fixture, not a complete replay.
    airroot::Point root{-5033.27392578125f-102.609474f/.9144f,
        -815.859375f+158.99838f/.9144f,396.2249146f};
    airroot::Point native{-5145.24f,-642.35f,397.5f};
    unsigned source=300;uint32_t epoch=379,now=1047,published=1031;
    Fixture(){
        bool reset=false;
        pose.tick=759;pose.period=.01666660f;
        Check(before.Observe(epoch,{758,0,0,false},pose.period,201,true,false,true,reset),"owned preceding Source phase");
        after=before;
        Check(after.Observe(epoch,metadata,pose.period,source,true,false,true,reset),"actual paired new recovery accepted");
        Check(!reset,"bail entry is not checkpoint teleport");
        // Tick757 itself was not captured. Reconstruct a seed from the logged
        // tick758 measured rate; the exact logged758/759 heights drive the test.
        Check(vertical.Step(epoch,757,pose.period,397.2777710f+35.03096f*pose.period,984),"synthetic prior seed from logged root derivative");
        Check(vertical.Step(epoch,758,pose.period,397.2777710f,1000),"exact logged previous root");
        Check(horizontal.Step(epoch,757,pose.period,-5144.81323f,-643.0797f,984),"prior XY seed");
        Check(horizontal.Step(epoch,758,pose.period,-5145.03857f,-642.70605f,1000),"logged previous XY root");
    }
    bool Apply(){return ragdollroothandoff::Rebase(before,after,metadata,pose,source,epoch,now,published,root,native,vertical,horizontal);}
};
int main(){
    groundtuning::NaturalMomentum=true;
    Fixture exact;const float priorVy=exact.vertical.velocity,priorVx=exact.horizontal.vx,priorVh=exact.horizontal.vy;
    Check(!exact.vertical.Step(exact.epoch,759,exact.pose.period,exact.root.z,exact.published),"exact logged root rate63yd/s reproduces old cancellation");
    Check(exact.Apply(),"verified first300 rebases actual target");
    Check(exact.vertical.height==396.2249146f && exact.vertical.velocity==priorVy
        && exact.horizontal.vx==priorVx && exact.horizontal.vy==priorVh,"actual target and nonzero previous motion retained");
    Check(exact.vertical.Step(exact.epoch,759,exact.pose.period,exact.root.z,exact.published)
        && exact.horizontal.Step(exact.epoch,759,exact.pose.period,exact.root.x,exact.root.y,exact.published),"ordinary same-tick consumers unchanged");
    Check(!exact.Apply(),"single transition cannot repeatedly rebase");
    skatevertical::Motor motor;
    Check(motor.RootSample(379,1000,758,exact.pose.period,397.2777710f,priorVy,false,4000,1000),"existing flight motor");
    Check(motor.RootSample(379,1047,759,exact.pose.period,exact.vertical.height,exact.vertical.velocity,false,4032,1031),"same motor accepts rebased transition without new impulse or clock reset");
    float down=0;airroot::Point delta{0,0,0};
    Check(motor.Delta(1047,379,4032,4048,exact.native.z,down) && down>0 && fabsf(down)<1,"bounded ordinary native vertical request");
    Check(exact.horizontal.Plan(1047,379,16,exact.native.x,exact.native.y,delta,exact.root.z-exact.native.z)
        && hypotf(delta.x,delta.y)>.3f,"bounded native XY request keeps motion instead of zero-speed stall");
    const float nextZ=396.15f;
    Check(exact.vertical.Step(379,760,exact.pose.period,nextZ,1048),"next original ragdoll sample uses ordinary derivative");
    Check(fabsf(exact.vertical.velocity-(nextZ-exact.root.z)/exact.pose.period)<.001f,"handoff velocity not latched across ragdoll frames");
    Check(!exact.vertical.Step(379,761,exact.pose.period,nextZ-1.05f,1065),"ordinary55yd/s guard still rejects later discontinuity");
    Check(!motor.RootSample(379,1065,761,exact.pose.period,nextZ,-63,false,4064,1065),"motor55yd/s guard unchanged");
    Check(!motor.Delta(1200,379,4048,4064,exact.native.z-down,down),"motor freshness unchanged");
    Fixture xy;xy.root.x=xy.horizontal.x+1.19f;xy.native.x=xy.root.x;
    xy.root.y=xy.horizontal.y+xy.horizontal.vy*xy.pose.period;xy.native.y=xy.root.y;
    auto legacyXY=xy.horizontal;
    Check(legacyXY.Step(379,759,xy.pose.period,xy.root.x,xy.root.y,xy.published)
        && !legacyXY.Plan(xy.now,379,16,xy.native.x,xy.native.y,delta,xy.root.z-xy.native.z),"phase-root XY shift reproduces70yd/s transport guard");
    Check(xy.Apply() && xy.horizontal.vx==priorVx,"verified handoff covers XY without widening70yd/s guard");
    Check(!xy.horizontal.Step(379,760,xy.pose.period,xy.root.x+3,xy.root.y,1048),"ordinary XY derivative guard still rejects following bad sample");
    const std::function<void(Fixture&)> negatives[]={
        [](Fixture& f){f.source=201;},[](Fixture& f){f.source=702;},[](Fixture& f){f.pose.ground=1;},
        [](Fixture& f){f.pose.airborne=1;},[](Fixture& f){f.metadata.active=false;},
        [](Fixture& f){f.metadata.cycle=0;},[](Fixture& f){f.metadata.cycle=2;},
        [](Fixture& f){f.before.cycle=UINT32_MAX;f.metadata.cycle=0;},
        [](Fixture& f){f.before.epoch=380;},[](Fixture& f){f.after.active=false;},
        [](Fixture& f){f.after.cycle=2;},[](Fixture& f){f.after.tick=758;},
        [](Fixture& f){f.metadata.tick=760;},[](Fixture& f){f.metadata.checkpoint=1;},
        [](Fixture& f){f.after.checkpoint=1;},[](Fixture& f){f.published=f.now-150;},
        [](Fixture& f){f.pose.period=NAN;},[](Fixture& f){f.pose.period=0;},
        [](Fixture& f){f.vertical.valid=false;},[](Fixture& f){f.horizontal.valid=false;},
        [](Fixture& f){f.vertical.epoch=380;},[](Fixture& f){f.horizontal.tick=757;},
        [](Fixture& f){f.before.tick=757;},[](Fixture& f){f.pose.tick=758;f.metadata.tick=758;f.after.tick=758;},
        [](Fixture& f){f.pose.tick=768;f.metadata.tick=768;f.after.tick=768;},
        [](Fixture& f){f.vertical.at=f.now-150;},[](Fixture& f){f.horizontal.at=f.now-150;},
        [](Fixture& f){f.root.z=NAN;},[](Fixture& f){f.root.x=50001;},
        [](Fixture& f){f.native.z=NAN;},[](Fixture& f){f.vertical.velocity=-56;},
        [](Fixture& f){f.horizontal.vx=71;},[](Fixture& f){f.root.z=f.vertical.height-4;},
        [](Fixture& f){f.native.z=f.root.z+3;},
        [](Fixture& f){f.root.z=f.vertical.height+1.3f;f.native.z=f.root.z;},
        [](Fixture& f){f.epoch=0;}
    };
    for(const auto& change:negatives){Fixture f;change(f);const auto v=f.vertical;const auto h=f.horizontal;
        Check(!f.Apply(),"invalid/unpaired/stale/nontransition/corrupt root refused");
        Check(f.vertical.tick==v.tick && f.vertical.at==v.at && f.vertical.height==v.height
            && f.horizontal.tick==h.tick && f.horizontal.at==h.at && f.horizontal.x==h.x,"failed handoff does not mutate histories");
    }
    Fixture wrapped;wrapped.now=32;wrapped.published=16;wrapped.vertical.at=UINT32_MAX-15;wrapped.horizontal.at=UINT32_MAX-15;
    Check(wrapped.Apply(),"freshness arithmetic remains safe across DWORD clock wrap");
    printf("{\"passed\":true,\"checks\":%u,\"exact_logged_drop_reproduced\":true,\"verified_cycle_only\":true,\"ordinary_guards_retained\":true,\"native_sweeps_unchanged\":true,\"momentum_retained\":true,\"live_verified\":false}\n",checks);
}
