#include "ground-transition.h"
#include "flight-handoff.h"
#include "air-root-motion.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static unsigned checks=0,profiles=0;
static void Check(bool ok,const char* why) { ++checks; if (!ok) { fprintf(stderr,"FAIL steep-bank %u %s\n",checks,why); exit(1); } }
int main() {
    using namespace groundtransition;
    float heights[DenseIntervals+1]{}; bool valid[DenseIntervals+1]{};
    // Captured end heights are evidence of the coarse rejection. Intermediate
    // heights here are synthetic linear banks, not unrecorded live ray results.
    for (const auto rise:{91.838310f-91.539383f,99.558418f-99.262070f}) {
        const auto coarse=Predict(0,.016f,0),dense=Refine(coarse);
        auto bank=[&](float d,float base,float up,float down,float& h) {
            h=90+d*rise/.25f; return h<=base+up && h>=base-down;
        };
        Check(!Sample(coarse,90,false,heights,valid,bank),"recorded endpoints exceed coarse envelope");
        Check(Sample(dense,90,true,heights,valid,bank),"continuous measured bank accepted when refined");
    }
    for (float slope:{-4.f,-3.f,-2.f,-1.2f,0.f,1.2f,2.f,3.f,4.f})
    for (float speed:{0.f,7.f,14.f}) for (uint32_t age:{0u,80u,149u}) {
        const auto coarse=Predict(speed,.033f,age),dense=Refine(coarse);
        unsigned queries=0;
        auto bank=[&](float d,float base,float up,float down,float& h) {
            ++queries; h=80+d*slope; return h<=base+up && h>=base-down;
        };
        Check(dense.valid && dense.intervals<=24 && dense.spacing<=DenseSpacing+.00001f,"bounded refinement");
        Check(Sample(dense,80,true,heights,valid,bank) && queries==dense.intervals+1,"real query at every support point");
        for (unsigned missing=0;missing<=dense.intervals;++missing) {
            queries=0;
            Check(!Sample(dense,80,true,heights,valid,[&](float d,float base,float up,float down,float& h) {
                const unsigned index=queries; const bool hit=bank(d,base,up,down,h); return hit && index!=missing;
            }),"missing support never interpolated");
        }
        ++profiles;
    }
    const auto dense=Refine(Predict(14,.05f,149));
    for (float step:{-.3f,.3f,-1.08f,1.08f}) for (unsigned edge=1;edge<=dense.intervals;++edge) {
        Check(!Sample(dense,80,true,heights,valid,[&](float d,float base,float up,float down,float& h) {
            h=80+(d>=edge*dense.spacing-.00001f?step:0); return h<=base+up && h>=base-down;
        }),"real abrupt step or cliff remains blocked");
    }
    Check(!Refine({true,NAN,.25f,1}).valid && !Refine({true,2,.25f,8}).valid,"invalid refinement refused");
    airroot::History root; airroot::Point delta{0,0,.1f};
    Check(root.Step(1,1,.02f,.8f,0,1000) && root.Step(1,2,.02f,.84f,0,1020),"synthetic uphill root history");
    Check(root.Plan(1020,1,20,0,0,delta,1.27f),"uphill coherent catchup");
    const float correctionX=delta.x/.02f-root.vx;
    Check(correctionX>0 && correctionX<3 && delta.z==.1f,"3D tracking correction bounded, source travel and native Z preserved");
    // With a synthetic1.2-grade floor and this rising native step, the old
    // full14yd/s positional chase would hit the face; bounded catchup clears it.
    Check(.28f*1.2f>.1f && delta.x*1.2f<.1f,"avoid horizontal catchup driving into uphill face");
    Check(!root.Plan(1020,1,20,0,0,delta,NAN) && !root.Plan(1020,1,20,0,0,delta,3.01f),"vertical coherence stays bounded");
    root.vx=10; root.x=0;
    Check(root.Plan(1020,1,20,0,0,delta,0) && fabsf(delta.x-.2f)<.00001f,"matching root travel not smoothed");
    using flighthandoff::SupportedDescent;
    Check(SupportedDescent(true,true,true,false,0x80001001u,false,.178322f,-.178322f,-6.3f,18),"recorded short downhill gap adopts fall motor");
    for (float gap:{NAN,-.1f,0.f,.125f,.501f,3.f})
        Check(!SupportedDescent(true,true,true,false,0x1000,false,gap,-.1f,-1,18),"unsupported floor gap rejected");
    for (uint32_t flag:{0u,0x1100u,0x1800u,0x3000u,0x101000u,0x1001000u})
        Check(!SupportedDescent(true,true,true,false,flag,false,.18f,-.18f,-1,18),"unsafe native state rejected");
    Check(!SupportedDescent(false,true,true,false,0x1000,false,.18f,-.18f,-1,18)
        && !SupportedDescent(true,false,true,false,0x1000,false,.18f,-.18f,-1,18)
        && !SupportedDescent(true,true,false,true,0x1000,false,.18f,-.18f,-1,18)
        && !SupportedDescent(true,true,true,false,0x1000,true,.18f,-.18f,-1,18)
        && !SupportedDescent(true,true,true,false,0x1000,false,.18f,-.18f,1,18)
        && !SupportedDescent(true,true,true,false,0x1000,false,.18f,-.18f,-1,250)
        && !SupportedDescent(true,true,true,false,0x1000,false,.18f,.2f,-1,18),"lease freshness phase spline direction clock coherence guards");
    flighthandoff::State flight;
    for (uint64_t tick=1;tick<=3;++tick)
        Check(flight.Observe(1000+static_cast<uint32_t>(tick)*20,1,tick,true,true,false,0x1000,true,80,80,60)
            ==(tick==3?flighthandoff::Action::Land:flighthandoff::Action::Keep),"supported descent still needs verified landing");
    printf("{\"passed\":true,\"checks\":%u,\"synthetic_profiles\":%u,\"recorded_coarse_rejections\":2,\"dense_support_queries_tested\":true,\"abrupt_steps_and_gaps_rejected\":true,\"bounded_3d_catchup_tested\":true,\"supported_descent_tested\":true,\"source_travel_unfiltered\":true,\"live_verified\":false}\n",checks,profiles);
}
