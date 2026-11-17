#include "ground-heading.h"
#include "skate-ground-motion.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL heading handoff check %u\n",checks); exit(1); } }
static float Settle(unsigned interval) {
    groundheading::Handoff state; float out=0;
    Check(state.Step(1,0,0,out) && out==1);
    unsigned now=0;
    while (now<1000) { now=(std::min)(1000u,now+interval); Check(state.Step(1,out,now,out)); }
    return out;
}
int main() {
    groundheading::Handoff state; float out=0;
    Check(!state.Step(NAN,0,0,out) && !state.active);
    Check(!state.Step(0,INFINITY,0,out) && !state.active);
    Check(state.Step(2,1,100,out) && out==2 && state.Changed(out));
    Check(state.Step(2,out,116,out) && out==2 && state.Changed(out));
    Check(state.Step(3,out,116,out) && out==3);
    state.Accepted(out); Check(!state.Changed(out));
    Check(!state.Changed(out+.0002f));
    Check(state.Changed(out+.002f));
    for (unsigned now=132;now<=600;now+=16) {
        const auto old=out;
        (void)old;
        Check(state.Step(2,out,now,out) && out==2);
    }
    Check(fabsf(out-2)<.001f);
    // Shortest path across +/-pi, not a full-circle camera snap.
    state.Clear(); const float degree=groundheading::Pi/180;
    Check(state.Step(-179*degree,179*degree,0,out));
    Check(state.Step(-179*degree,out,20,out));
    Check(fabsf(groundheading::Delta(out,179*degree)-2*degree)<.00001f);
    Check(state.Step(-179*degree,out,400,out) && fabsf(groundheading::Delta(out,-179*degree))<2*degree);
    Check(state.Step(1,.3f,550,out) && out==1); // no stale movement filter after a pause
    state.Clear(); Check(state.Step(1,0,0xfffffff0u,out));
    Check(state.Step(1,out,8,out) && out==1);
    Check(fabsf(Settle(8)-Settle(33))<1e-5f);
    // A 20ms engine stream at a 1ms render cadence: retain turning rate but
    // preserve Source direction exactly at publication boundaries. Camera is separate.
    state.Clear(); Check(state.Step(0,0,0,out));
    float maxStep=0,maxLag=0,lastRaw=0,maxRawStep=0;
    for (unsigned now=1;now<=2500;++now) {
        const float target=static_cast<float>((now/20)*20)*.001f*1.5f;
        maxRawStep=(std::max)(maxRawStep,fabsf(groundheading::Delta(target,lastRaw))); lastRaw=target;
        const float old=out; Check(state.Step(target,out,now,out));
        maxStep=(std::max)(maxStep,fabsf(groundheading::Delta(out,old)));
        maxLag=(std::max)(maxLag,fabsf(groundheading::Delta(target,out)));
    }
    Check(fabsf(maxStep-maxRawStep)<.00001f && maxLag<.00001f);
    Check(fabsf(groundheading::Delta(out,3.75f))<.12f); // same sustained steering, only short handoff lag
    // Replay native tracking at 100Hz against a full-speed circular path.
    // The positional correction remains bounded; no body teleport is needed.
    float maxGap=0,doubleSpeedGap=0;
    for (float cap : {7.f,14.f}) {
    state.Clear(); Check(state.Step(0,0,0,out));
    float actualX=0,actualY=0,engineX=0,engineY=0,gap=0;
    for (unsigned now=10;now<=6000;now+=10) {
        const float heading=static_cast<float>(now)*.001f*1.5f;
        engineX+=cosf(heading)*cap*.01f; engineY+=sinf(heading)*cap*.01f;
        const auto raw=skatemotion::Ground(cosf(heading)*cap*.9144f,-sinf(heading)*cap*.9144f,true,false,1,1,now,now,cap);
        const auto request=skatemotion::Synchronize(raw,actualX,actualY,engineX*.9144f,-engineY*.9144f,0,0,cap);
        Check(request.valid && request.speed<=cap);
        Check(state.Step(request.heading,out,now,out));
        actualX+=cosf(out)*request.speed*.01f; actualY+=sinf(out)*request.speed*.01f;
        gap=(std::max)(gap,hypotf(actualX-engineX,actualY-engineY));
    }
    if (cap==7) maxGap=gap; else doubleSpeedGap=gap;
    }
    // Without correction-induced yaw, circular tracking stays within twice
    // the smoothing travel distance (2*v*60ms), not the old yaw-assisted bound.
    Check(maxGap<.84f);
    Check(doubleSpeedGap<1.68f); // preserve the previously accepted tracking bounds
    // Smoothing cannot alter the immediate stop/speed-limit request.
    const auto stopped=skatemotion::Ground(0,0,true,false,1,1,1,1,7);
    Check(stopped.valid && stopped.speed==0);
    Check(skatemotion::Ground(50,0,true,false,1,1,1,1,2).speed==2);
    printf("{\"passed\":true,\"checks\":%u,\"blend_ms\":0,\"direct_source_heading\":true,\"max_raw_step_rad\":%.6f,\"max_applied_step_rad\":%.6f,\"max_lag_rad\":%.6f,\"max_tracking_gap_yards\":%.6f,\"double_speed_tracking_gap_yards\":%.6f,\"wrap_tested\":true,\"reset_tested\":true,\"live_camera_verified\":false}\n",checks,maxRawStep,maxStep,maxLag,maxGap,doubleSpeedGap);
}
