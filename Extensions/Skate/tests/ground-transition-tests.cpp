#include "ground-transition.h"
#include "ollie-controller.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0;
static unsigned resolutionCases=0,recordedCases=0;
static void Check(bool value) { ++checks; if (!value) { fprintf(stderr,"FAIL transition check %u\n",checks); exit(1); } }
int main() {
    {
        const auto profile=groundtransition::Refine(groundtransition::Predict(1,1.f/60,31));
        float h[groundtransition::DenseIntervals+1]{};bool found[groundtransition::DenseIntervals+1]{};
        Check(groundtransition::Sample(profile,501.668427f,true,h,found,[](float d,float,float rise,float,float& z){
            z=d==0?(rise>=.127838f?501.796265f:501.668427f):501.796082f;return true;
        }));
        Check(h[0]==501.668427f); // Current lower floor, not the nearby upper rim.
    }
    bool valid[7]={true,true,true,true,true,true,true};
    float heights[7]={};
    Check(groundtransition::Traversable(heights,valid));
    // Actual walking geometry from the recorded camera-class guard stop.
    const float recorded[7]={-.00047868f,.11228247f,.22504345f,.33780450f,.45056561f,.56332660f,.67608768f};
    Check(groundtransition::Traversable(recorded,valid));
    // A larger aggregate rise is safe only when every short interval supports it.
    for (float rise : {-1.5f,-1.2f,-.853126f,-.75f,0.f,.75f,.853126f,1.2f,1.5f}) {
        for (unsigned i=0;i<7;++i) heights[i]=static_cast<float>(i)*rise/6;
        Check(groundtransition::Traversable(heights,valid));
        for (unsigned i=0;i<7;++i) {
            valid[i]=false; Check(!groundtransition::Traversable(heights,valid)); valid[i]=true;
        }
    }
    // No manufactured ramp across a hole, roof/ground discontinuity or cliff.
    for (float rise : {-1.f,-.3f,.3f,.853126f,1.f}) {
        for (unsigned edge=1;edge<7;++edge) {
            for (unsigned i=0;i<7;++i) heights[i]=i>=edge?rise:0;
            Check(!groundtransition::Traversable(heights,valid));
        }
    }
    for (unsigned edge=1;edge<7;++edge) {
        for (unsigned i=0;i<7;++i) heights[i]=i>=edge?.2f:0; // bounded small stair
        Check(groundtransition::Traversable(heights,valid));
    }
    for (float rise : {-3.f,3.f}) {
        for (unsigned i=0;i<7;++i) heights[i]=static_cast<float>(i)*rise/6;
        Check(!groundtransition::Traversable(heights,valid));
    }
    for (unsigned missing=0;missing<7;++missing) {
        for (auto& h:heights) h=0;
        heights[missing]=NAN; Check(!groundtransition::Traversable(heights,valid));
        heights[missing]=INFINITY; Check(!groundtransition::Traversable(heights,valid));
    }
    // Absolute map height must not change classification.
    for (float base : {-1000.f,0.f,80.46186f,1000.f}) {
        for (unsigned i=0;i<7;++i) heights[i]=base+static_cast<float>(i)*.15f;
        Check(groundtransition::Traversable(heights,valid));
    }
    unsigned predicted=0;
    for (float speed:{0.f,.3f,1.f,4.f,7.f,14.f}) for (float period:{.008f,.0166667f,.033f,.05f})
        for (uint32_t age:{0u,16u,33u,80u,149u}) {
            const auto probe=groundtransition::Predict(speed,period,age);
            Check(probe.valid && probe.distance>=.25f && probe.distance<=1.5f && probe.intervals>=1 && probe.intervals<=6);
            Check(fabsf(probe.spacing*probe.intervals-probe.distance)<.00001f);
            if (speed<=1) Check(probe.distance==.25f);
            for (float slope:{-1.f,-.45f,0.f,.45f,1.f}) {
                for (unsigned i=0;i<=probe.intervals;++i) { heights[i]=80+i*probe.spacing*slope; valid[i]=true; }
                Check(groundtransition::Traversable(heights,valid,probe.intervals,probe.spacing));
                valid[probe.intervals]=false;
                Check(!groundtransition::Traversable(heights,valid,probe.intervals,probe.spacing));
            }
            ++predicted;
        }
    Check(!groundtransition::Predict(NAN,.016f,0).valid && !groundtransition::Predict(15,.016f,0).valid
        && !groundtransition::Predict(7,0,0).valid && !groundtransition::Predict(7,.016f,150).valid);
    // The adaptive sample spacing must not silently shrink the original local
    // bump envelope. Both real collision solvers remain authoritative; this
    // guard does not manufacture ramps or move the actor over a step.
    for (float spacing:{.125f,.15f,.18f,.2f,.249f,.25f}) {
        for (float step:{-.28f,-.22f,-.2f,.2f,.22f,.28f}) {
            for (unsigned edge=1;edge<7;++edge) {
                for (unsigned i=0;i<7;++i) { heights[i]=80.f+(i>=edge?step:0); valid[i]=true; }
                Check(groundtransition::Traversable(heights,valid,6,spacing));
                ++resolutionCases;
                valid[edge]=false;
                Check(!groundtransition::Traversable(heights,valid,6,spacing));
                valid[edge]=true;
            }
        }
        // Restore the same rejection boundary at every spacing. Do not turn
        // a real 0.3-yard riser/drop, missing support or cliff into a ramp.
        for (float step:{-.3f,.3f,-.8f,.8f}) {
            for (unsigned i=0;i<7;++i) heights[i]=i>=3?step:0;
            Check(!groundtransition::Traversable(heights,valid,6,spacing));
        }
    }
    // Heights captured in the user's ride. Their old log omitted spacing,
    // so replay representative short spans explicitly rather than claiming
    // reconstruction of the original native queries.
    const float captured[][7]={
        {89.361885f,89.460487f,89.566101f,89.793648f},
        {89.477921f,89.588562f,89.807472f},
        {89.515038f,89.661270f,89.879211f},
        {89.546089f,89.716263f,89.916069f}
    };
    const unsigned counts[]={3,2,2,2};
    for (unsigned c=0;c<4;++c) for (float spacing:{.125f,.15f}) {
        bool oldAccepted=true;
        for (unsigned i=1;i<=counts[c];++i)
            if (fabsf(captured[c][i]-captured[c][i-1])>spacing*groundtransition::MaxSlope+groundtransition::SampleTolerance) oldAccepted=false;
        Check(!oldAccepted);
        Check(groundtransition::Traversable(captured[c],valid,counts[c],spacing));
        ++recordedCases;
    }
    const float stillTooLarge[]={89.432205f,89.560989f,89.856117f};
    Check(!groundtransition::Traversable(stillTooLarge,valid,2,.125f));
    Check(fabsf(groundtransition::MaxAdjacentChange-.29f)<.00001f);
    ollie::Launch launch; ollie::Flight flight;
    Check(!launch.Obstruct());
    Check(launch.Capture(1.2f,14) && flight.Begin(100,1,1,80));
    Check(flight.Check(110,1,0x1000,false,80.1f));
    Check(launch.Obstruct() && launch.held && launch.speed==0 && launch.heading==1.2f);
    Check(flight.active && flight.Check(200,1,0x3000,false,80.8f));
    Check(flight.Landed(500,0,true,false));
    printf("{\"passed\":true,\"checks\":%u,\"recorded_slope_passed\":true,\"missing_ground_rejected\":true,\"abrupt_step_rejected\":true,\"contour_samples\":7,\"adaptive_probe_trials\":%u,\"adaptive_horizon_tested\":true,\"air_impact_preserves_flight_tested\":true,\"sample_resolution_invariant_tested\":true,\"resolution_step_cases\":%u,\"captured_short_span_replays\":%u,\"max_adjacent_change_yards\":%.3f,\"native_step_height_overridden\":false,\"live_client_verified\":false}\n",checks,predicted,resolutionCases,recordedCases,groundtransition::MaxAdjacentChange);
}
