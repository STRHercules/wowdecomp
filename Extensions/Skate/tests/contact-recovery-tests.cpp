#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "contact-recovery.h"
#include "flight-handoff.h"
#include "ollie-controller.h"
#include "board-terrain.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0;
static void Check(bool ok,const char* why) { ++checks; if (!ok) { fprintf(stderr,"FAIL contact-recovery %u %s\n",checks,why); exit(1); } }
int main() {
    contactrecovery::Ground ground;
    // Captured: native fall flag persists at250ms, floor gap .002655yd,
    // fresh grounded Source root only .020737yd lower. Do not extend the timer.
    for (uint64_t tick=2289;tick<=2291;++tick) {
        const bool ready=ground.Observe(1000+static_cast<uint32_t>(tick-2289)*17,48,tick,true,true,
            0x80001001,false,true,-.020737f,true,false);
        Check(ready==(tick==2291),"recorded contact needs three distinct Source confirmations");
    }
    ground.Clear();
    Check(!ground.Observe(1000,1,1,true,true,0x1000,false,true,0,true,false),"first contact");
    for (unsigned ms=1;ms<150;++ms)
        Check(!ground.Observe(1000+ms,1,1,true,true,0x1000,false,true,0,true,false) && ground.samples==1,
            "repeated render frames cannot manufacture contact confirmations");
    Check(!ground.Observe(1150,1,1,true,false,0x1000,false,true,0,true,false) && !ground.samples,"stale support clears history");
    for (unsigned frame:{4u,8u,16u,25u,50u}) {
        ground.Clear(); unsigned readyAt=0;
        for (unsigned ms=0;ms<250;ms+=frame) {
            const auto tick=1+ms/17;
            if (ground.Observe(1000+ms,1,tick,true,true,0x1000,false,true,0,true,false)) { readyAt=ms; break; }
        }
        Check(readyAt>=34 && readyAt<=100,"confirmed recovery before legacy250ms expiry across render clocks");
    }
    for (unsigned failure=0;failure<9;++failure) {
        ground.Clear();
        ground.Observe(1000,1,1,true,true,0x1000,false,true,0,true,false);
        ground.Observe(1017,1,2,true,true,0x1000,false,true,0,true,false);
        const bool ready=ground.Observe(1034,1,3,failure!=0,failure!=1,failure==2?0x3000:0x1000,
            failure==3,failure!=4,failure==5?.5f:failure==6?NAN:0,failure!=7,failure==8);
        Check(!ready && !ground.samples,"lease freshness unsafe spline floor root and phase failure resets recovery");
    }
    ground.Clear();
    ground.Observe(1000,1,1,true,true,0x1000,false,true,0,true,false);
    ground.Observe(1017,1,2,true,true,0x1000,false,true,0,true,false);
    Check(!ground.Observe(1034,2,3,true,true,0x1000,false,true,0,true,false) && ground.samples==1,"epoch does not borrow contact proofs");
    using contactrecovery::Pop;
    Check(ollie::Classify(false,true,5.60301f,313,true,ollie::Support::Contact)==ollie::Takeoff::Refuse,
        "captured313ms uphill classification failure reproduced");
    Check(Pop(true,true,0x80001001,false,true,1.9f,false,true,true,313,5.6f),"fresh verified uphill Source pop can resume its existing velocity");
    for (unsigned age:{0u,150u,299u,313u,599u})
        Check(Pop(true,true,0x1000,false,true,.04f,false,true,true,age,1),"owned pop window retained without requiring native ground flag");
    for (float gap:{NAN,-.1f,0.f,.025f,3.f})
        Check(!Pop(true,true,0x1000,false,true,gap,false,true,true,313,1),"unrisen or incoherent source cannot resume takeoff");
    for (float velocity:{NAN,-1.f,0.f,.5f,55.01f})
        Check(!Pop(true,true,0x1000,false,true,.04f,false,true,true,313,velocity),"no second impulse or upward relaunch from non-rising source");
    for (uint32_t flags:{0u,0x1100u,0x1800u,0x3000u,0x101000u,0x1001000u})
        Check(!Pop(true,true,flags,false,true,.04f,false,true,true,313,1),"unsafe native pop refused");
    Check(!Pop(false,true,0x1000,false,true,.04f,false,true,true,313,1)
        && !Pop(true,false,0x1000,false,true,.04f,false,true,true,313,1)
        && !Pop(true,true,0x1000,true,true,.04f,false,true,true,313,1)
        && !Pop(true,true,0x1000,false,false,.04f,false,true,true,313,1)
        && !Pop(true,true,0x1000,false,true,.04f,true,false,true,313,1)
        && !Pop(true,true,0x1000,false,true,.04f,false,true,false,313,1)
        && !Pop(true,true,0x1000,false,true,.04f,false,true,true,600,1),"all takeoff ownership phase timing floor guards retained");
    unsigned bankCases=0;
    for (float slope:{0.f,.5f,1.f,2.f,3.f,4.f}) for (unsigned angle=0;angle<360;angle+=15) {
        const float a=angle*.01745329252f;
        float heights[5]={80,80+slope*.025f*cosf(a),80-slope*.025f*cosf(a),80+slope*.025f*sinf(a),80-slope*.025f*sinf(a)};
        bool valid[5]={true,true,true,true,true};
        Check(flighthandoff::Supported(80,heights,valid),"small real footprint proves bank contact rather than demanding flat terrain");
        valid[0]=false; Check(!flighthandoff::Supported(80,heights,valid),"missing centre remains unlandable");
        ++bankCases;
    }
    skatevisual::Mesh mesh; skatevisual::Frame frame;
    frame.pose.ground=true;
    for (float x:{-.4f,.4f}) for (float z:{-.15f,.15f}) {
        mesh.colors.push_back(0xffe8dbb8); frame.vertices.push_back({x,.5f*x-.25f*z+.01f,z});
    }
    mesh.colors.push_back(0xff32859d); frame.vertices.push_back({0,-.9f,0}); // never mistake a deck vertex for a wheel
    boardplacement::Fit fit; fit.valid=true; fit.rootRelative=true; fit.x=.3f;
    boardterrain::Sample points[4]{};
    const auto oldFrame=frame;
    Check(boardterrain::Wheels(mesh,frame,{0,0,0},{0,0,0},fit,points)==4,"four separated wheel-material mesh samples");
    for (const auto p:points) {
        const float sourceGap=p.source.z-(.5f*p.source.x+.25f*p.source.y);
        const float visualGap=p.displayed.z-(.5f*p.displayed.x+.25f*p.displayed.y);
        Check(sourceGap>0 && sourceGap<.02f && visualGap<-.1f,"distinguish visual stance clipping from nonpenetrating Source wheel mesh");
    }
    Check(frame.vertices[0].y==oldFrame.vertices[0].y && frame.pose.position[0]==oldFrame.pose.position[0],"measurement does not alter mesh or physics root");
    frame.pose.airborne=true;
    Check(!boardterrain::Wheels(mesh,frame,{0,0,0},{0,0,0},fit,points),"do not interpret flip geometry as grounded penetration");
    frame.pose.airborne=false; fit.valid=false;
    Check(!boardterrain::Wheels(mesh,frame,{0,0,0},{0,0,0},fit,points),"unmatched visual fit is not measured");
    printf("{\"passed\":true,\"checks\":%u,\"recorded_contact_timeout_reproduced\":true,\"recorded_contact_pop_reproduced\":true,\"distinct_ground_ticks\":3,\"synthetic_bank_directions\":%u,\"ownership_phase_gap_guards_tested\":true,\"wheel_mesh_vs_source_measurement_tested\":true,\"physics_and_visual_writes_added\":false,\"live_verified\":false}\n",checks,bankCases);
}
