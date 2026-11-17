#include "character-height.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>

static unsigned checks=0;
static void Check(bool value,const char* message) {
    ++checks; if (!value) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}
static float Constant(unsigned step,float initial,float target) {
    characterheight::Filter filter{}; float out=0;
    Check(filter.Step(initial,0,1,1,out),"initial height");
    for (unsigned at=step;at<=960;at+=step) {
        const float before=out;
        Check(filter.Step(target,at,1,1+at/32,out),"bounded height");
        Check(std::isfinite(out) && out>=-.6f && out<=.75f,"finite solver bounds");
        Check(fabsf(out-before)<=characterheight::MaxRateYards*step*.001f+1e-6f,"rate cap");
        Check(target>=initial?out>=before:out<=before,"monotonic crouch/release correction");
    }
    return out;
}
int main() {
    // Live observed extrema, not a claim that the sampled log captured every frame.
    constexpr float low=-.39839f,high=.14149f;
    const float down=Constant(1,high,low),up=Constant(1,low,high);
    for (unsigned step:{2u,4u,8u,16u,32u,48u,96u}) {
        Check(fabsf(Constant(step,high,low)-down)<.00001f,"render-rate independent descent");
        Check(fabsf(Constant(step,low,high)-up)<.00001f,"render-rate independent ascent");
    }
    Check(fabsf(down-low)<.003f && fabsf(up-high)<.003f,"full held crouch/release target retained");
    characterheight::Filter filter{}; float out=0;
    Check(filter.Step(0,0,1,20,out),"oscillation start");
    float minimum=1,maximum=-1,maxStep=0;
    for (unsigned at=8;at<=4000;at+=8) {
        const float raw=(low+high)/2+(high-low)/2*sinf(at*.001f*8*6.28318530718f);
        const float before=out;
        Check(filter.Step(raw,at,1,20+at/32,out),"duplicate source ticks remain smooth");
        maxStep=(std::fmax)(maxStep,fabsf(out-before));
        if (at>=2000) { minimum=(std::fmin)(minimum,out); maximum=(std::fmax)(maximum,out); }
    }
    const float span=maximum-minimum;
    Check(span<(high-low)*.2f,"high-frequency correction attenuated");
    Check(maxStep<=.010001f,"8ms render step capped");
    const float duplicate=out;
    Check(filter.Step(high,4000,1,145,out) && out==duplicate,"zero elapsed time cannot move root");
    Check(filter.Step(low,4001,2,1,out) && out==low,"epoch reset");
    Check(filter.Step(high,4002,2,0,out) && out==high,"backwards source tick reset");
    Check(filter.Step(low,4252,2,2,out) && out==low,"stale pose reset");
    Check(filter.Step(high,4200,2,3,out) && out==high,"backwards clock reset");
    filter.Clear();
    Check(!filter.active && !filter.epoch && !filter.at,"mode/focus/model clear");
    Check(filter.Step(low,0xfffffff8u,3,1,out),"clock wrap start");
    Check(filter.Step(high,8,3,2,out) && out>low && out-low<=.020001f,"normal clock wrap smooth");
    for (float bad:{-.601f,.751f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) {
        Check(!filter.Step(bad,9,3,3,out) && out==0 && !filter.active,"invalid correction clears state");
    }
    Check(!filter.Step(high,10,0,4,out) && !filter.active,"invalid epoch rejected");
    Check(filter.Step(high,11,4,1,out) && out==high,"clean reactivation");
    std::printf("{\"passed\":true,\"checks\":%u,\"blend_ms\":160,\"max_rate_yards\":1.25,"
        "\"render_rate_independent\":true,\"held_crouch_target_retained\":true,"
        "\"lifecycle_reset_tested\":true,\"oscillation_span_yards\":%.6f,\"max_8ms_step_yards\":%.6f}\n",
        checks,span,maxStep);
}
