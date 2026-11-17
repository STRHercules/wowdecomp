#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "skate-vertical.h"
#include "vanilla-root.h"
#include "ollie-controller.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool value,const char* why) { ++checks; if (!value) { fprintf(stderr,"FAIL vanilla reference %u %s\n",checks,why); exit(1); } }
int main() {
    vanillaroot::History history;
    Check(history.Step(1,10,1.f/60,80,1000),"first root");
    Check(history.Step(1,13,1.f/60,80.1f,1050) && fabsf(history.velocity-2)<.001f,"root derivative uses physics ticks");
    Check(history.Step(1,13,1.f/60,80.1f,1100) && history.at==1050,"duplicate cannot refresh root age");
    Check(!history.Step(1,14,1.f/60,NAN,1100),"nonfinite source rejected");
    Check(history.Step(2,1,1.f/60,100,1200) && history.velocity==0,"new epoch clears derivative");
    skatevertical::Motor motor;
    float body=80,maxError=0,down=0,contact=0;
    for (uint32_t ms=0;ms<=1800;ms+=5) {
        const float t=ms*.001f;
        const float height=80+(std::max)(0.f,4.f*t-3.5f*t*t);
        if (ms%50==0) {
            Check(history.Step(3,1+ms/50*3,1.f/60,height,2000+ms),"source root samples");
            Check(motor.RootSample(3,2000+ms,history.tick,1.f/60,history.height,history.velocity,
                ms>1150,ms,history.at),"reference motor sample");
        }
        Check(motor.Delta(2000+ms,3,ms,ms+5,body,down),"reference native swept step");
        const float predicted=body-down;
        if (predicted<80) {
            Check(motor.segment.Contact(80,contact),"native floor contact time"); body=80;
        } else body=predicted;
        maxError=(std::max)(maxError,fabsf(body-height));
    }
    fprintf(stderr,"Synthetic reference max error %.6f yards\n",maxError);
    // Not accelerating descent leaves a small publication-sized height lag.
    // This 50ms/5ms replay measured .207703yd, vs .172913 with the overspeed
    // servo. Bound that trade-off explicitly; do not fix lag by slamming down.
    Check(maxError<.22f,"bounded tracking lag without descending catch-up overspeed");
    Check(motor.rootDriven && motor.acceleration==0,"no stock/gravity fallback in reference mode");
    Check(!motor.Delta(4000,3,1800,1805,body,down),"stale source cannot drive movement");
    motor.Clear();
    Check(motor.RootSample(4,4100,1,1.f/60,80,3,false,750,4100),"capture previous native fall clock before queued jump");
    Check(motor.RootSample(4,4110,1,1.f/60,80,3,false,750,4100),"queued jump does not pretend initializer already ran");
    Check(motor.Rebase(750,0) && motor.Delta(4110,4,0,16,80,down) && down<0,"actual initializer owns native clock reset");
    unsigned descentChecks=0;
    // Recorded late-jump source rate was -6.48379, but the old error servo
    // added another -1.92389 yd/s. Replay that discrepancy and timing jitter.
    for (float rate:{-.1f,-1.f,-5.91044f,-6.48379f,-20.f}) {
        for (float error:{-.25f,-.160324f,-.01f,0.f,.01f,.25f}) {
            for (uint32_t age:{0u,17u,50u,100u}) {
              for (bool grounded:{false,true}) {
                motor.Clear();
                Check(motor.RootSample(5,5000,10,1.f/60,90,rate,grounded,0,5000),"descent replay sample");
                const float nativeZ=90+rate*(age*.001f)-error;
                Check(motor.Delta(5000+age,5,0,16,nativeZ,down),"delayed descending native step");
                Check(motor.velocity>=rate-.00001f,"tracking cannot amplify Source descent");
                Check(motor.velocity<=0,"braking a descent cannot bounce upward");
                if (error<=0) Check(fabsf(motor.velocity-rate)<.00001f,"behind-source descent preserves measured rate");
                if (error>0) Check(motor.velocity>rate,"ahead-of-source descent still brakes");
                const float middle=motor.segment.Height(.008f);
                Check(motor.segment.Contact(middle,contact),"descent contact inversion matches corrected step");
                ++descentChecks;
              }
            }
        }
    }
    motor.Clear();
    Check(motor.RootSample(6,6000,10,1.f/60,80,0,true,0,6000),"Source grounded settlement");
    Check(motor.Delta(6000,6,0,100,80.02f,down) && down>0,"residual gap settles instead of hovering");
    Check(motor.segment.Contact(80,contact) && contact<=.1001f,"residual floor contact within swept step");
    unsigned settledGroundCases=0;
    // A landed Source root may rise because the board has rolled uphill.
    // That must never sustain native flight or re-accelerate the player upward.
    for (float rate:{0.f,.5f,2.47788f,3.56920f}) for (float error:{-.25f,0.f,.25f}) {
        motor.Clear();
        Check(motor.RootSample(7,7000,10,1.f/60,86.92432f,rate,true,0,7000),"rising grounded Source sample");
        Check(motor.Delta(7000,7,0,16,86.92432f-error,down),"grounded native settlement step");
        Check(motor.velocity<=-.25f && down>0,"landed Source cannot relaunch native fall on either side of root height");
        ++settledGroundCases;
    }
    // Replay the stuck flight's height/rate with a synthetic nearby native
    // support plane. This tests handoff mechanics, not the original terrain.
    motor.Clear(); ollie::Flight landedFlight; ollie::Launch landedLaunch;
    Check(landedFlight.Begin(8000,7,15,85.34369f) && landedFlight.Check(8100,7,0x1000,false,86.4f),"owned native flight before Source landing");
    Check(landedLaunch.Capture(1.961491f,12.654136f),"recorded stuck direction lock");
    float legacyZ=86.48112f; bool legacyContact=false;
    for (uint32_t ms=0;ms<=2000 && !legacyContact;ms+=16) {
        const float root=86.37518f+3.56920f*(ms*.001f),error=root-legacyZ;
        float oldVelocity=3.56920f+(std::max)(-3.f,(std::min)(3.f,error*12.f));
        if (error<0) oldVelocity=(std::min)(-.25f,oldVelocity);
        legacyZ+=oldVelocity*.016f;
        legacyContact=legacyZ<=86.45f;
    }
    Check(!legacyContact && legacyZ>87,"old conditional settlement reproduces prolonged direction lock");
    float settleZ=86.48112f; bool nativeContact=false; uint32_t contactMs=0;
    for (uint32_t ms=0;ms<=500 && !nativeContact;ms+=16) {
        const float root=86.37518f+3.56920f*(ms*.001f);
        Check(motor.RootSample(7,9000+ms,100+ms/16,1.f/60,root,3.56920f,true,ms,9000+ms),"landed uphill root refresh");
        Check(motor.Delta(9000+ms,7,ms,ms+16,settleZ,down),"landed bridge remains fresh");
        if (settleZ-down<=86.45f) {
            Check(motor.segment.Contact(86.45f,contact),"native contact inversion on settled segment");
            settleZ=86.45f; nativeContact=true; contactMs=ms+16;
        } else settleZ-=down;
    }
    Check(nativeContact && contactMs<=160,"native floor reached promptly instead of chasing uphill Source root");
    Check(landedFlight.Landed(9000+contactMs,0,true,false),"real native contact can complete flight");
    landedLaunch.Landed(9000+contactMs); landedFlight.Clear();
    Check(!landedLaunch.Holding(9250+contactMs,false),"direction lock expires after existing landing handoff");
    float steerHeading=-.8f,steerSpeed=6;
    Check(landedLaunch.Apply(steerHeading,steerSpeed,9250+contactMs)
        && fabsf(std::remainder(steerHeading+.8f,6.28318530718f))<.00001f && steerSpeed==6,"post-landing steering and braking are no longer frozen");
    volatile LONG epoch=1; skatewire::Pipe helper;
    Check(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\vanilla-reference-helper.log",1,&epoch),"real source helper");
    std::vector<unsigned char> payload,reply,empty;
    for (float value:{0.f,0.f,0.f,0.f}) skatewire::Float(payload,value);
    skatewire::Word(payload,2);
    for (float value:{-1000.f,0.f,-1000.f,-1000.f,0.f,1000.f,1000.f,0.f,1000.f,
        -1000.f,0.f,-1000.f,1000.f,0.f,1000.f,1000.f,0.f,-1000.f}) skatewire::Float(payload,value);
    skatewire::Pose pose{};
    Check(helper.Request(1,1,payload,reply) && skatewire::Decode(reply,pose),"source flat world");
    Check(helper.Request(7,1,empty,reply),"source visual schema");
    const auto mesh=skatevisual::Schema(reply); Check(mesh!=nullptr,"valid schema");
    payload.clear(); skatewire::Float(payload,12.8016f); Check(helper.Request(11,1,payload,reply),"accepted speed cap");
    payload.clear(); skatewire::Float(payload,1.8f); skatewire::Float(payload,1.5f); Check(helper.Request(13,1,payload,reply),"reference push/acceleration");
    payload.clear(); skatewire::Float(payload,.65f); Check(helper.Request(21,1,payload,reply),"reference air/ground gravity profile");
    unsigned airFrames=0,landings=0,rootSteps=0,floorContacts=0;
    float peak=0,maxLiveError=0,baseline=0;
    bool wasAir=false,nativeAir=false;
    uint32_t nativeMs=0; history.Clear(); motor.Clear(); body=0;
    for (unsigned frame=0;frame<1000;++frame) {
        const unsigned cycle=frame%180;
        const int16_t right=cycle>=35 && cycle<65?-32768:cycle>=65 && cycle<68?32767:0;
        skatewire::OllieStep(payload,1,cycle>=5 && cycle<30,false,false,false,0,0,right);
        Check(helper.Request(18,1,payload,reply),"real crouch/flick step");
        const auto visual=skatevisual::Decode(reply,*mesh); Check(visual!=nullptr,"real source root/pose frame"); pose=visual->pose;
        const uint32_t now=5000+frame*17;
        const float sourceY=pose.position[1]/.9144f;
        Check(history.Step(8,pose.tick,pose.period,sourceY,now),"real root history");
        const bool air=pose.airborne!=0;
        if (!air && !wasAir) { baseline=sourceY; body=sourceY; }
        if (air && !wasAir) { motor.Clear(); nativeAir=true; nativeMs=0; }
        if (air) ++airFrames;
        if (!air && wasAir) ++landings;
        if (nativeAir) {
            Check(motor.RootSample(8,now,pose.tick,pose.period,sourceY,history.velocity,!air,nativeMs,now),"real reference root to native motor");
            Check(motor.Delta(now,8,nativeMs,nativeMs+17,body,down),"real reference swept displacement"); ++rootSteps;
            if (history.velocity<0) {
                Check(motor.velocity>=history.velocity-.00001f,"real engine descent not amplified by tracking");
                ++descentChecks;
            }
            const float next=body-down;
            if (next<baseline && motor.velocity<0) {
                Check(motor.segment.Contact(baseline,contact),"real source/native floor contact");
                body=baseline; ++floorContacts; nativeAir=false;
            } else body=next;
            nativeMs+=17;
            peak=(std::max)(peak,body-baseline);
            maxLiveError=(std::max)(maxLiveError,fabsf(body-sourceY));
        }
        wasAir=air;
    }
    fprintf(stderr,"Real reference air=%u landed=%u floor=%u peak=%.6f root_error=%.6f\n",airFrames,landings,floorContacts,peak,maxLiveError);
    Check(landings>=5 && airFrames>100,"five real reference ollies");
    Check(floorContacts>=5 && rootSteps>100,"native floor landing emulation");
    Check(peak>.75f && peak<5 && maxLiveError<.5f,"measured reference height and tracking, no arbitrary impulse boost");
    printf("{\"passed\":true,\"checks\":%u,\"source_air_gravity\":0.65,\"source_root_authoritative\":true,\"descent_not_amplified\":true,\"descent_checks\":%u,\"floor_settlement_tested\":true,\"ground_root_relaunch_rejected\":true,\"ground_settlement_cases\":%u,\"legacy_stuck_landing_reproduced\":true,\"synthetic_recontact_ms\":%u,\"post_landing_control_release_tested\":true,\"synthetic_max_error_yards\":%.6f,\"real_air_frames\":%u,\"real_landings\":%u,\"native_root_steps\":%u,\"native_floor_contacts\":%u,\"peak_yards\":%.6f,\"max_root_error_yards\":%.6f,\"position_writes\":false,\"live_verified\":false}\n",
        checks,descentChecks,settledGroundCases,contactMs,maxError,airFrames,landings,rootSteps,floorContacts,peak,maxLiveError);
}
