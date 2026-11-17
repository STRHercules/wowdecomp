#include "SkatePeerSpeed.h"
#include "SkatePeers.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

int main() {
    skatepeerspeed::Lease lease; float speed=7,out=0;
    assert(lease.Apply(speed,3,true,out) && out==3); speed=out;
    assert(lease.Apply(speed,0,true,out) && out==0); speed=out;
    assert(lease.Release(speed,out) && out==7 && !lease.Held());
    speed=7; assert(lease.Apply(speed,7.1f,true,out) && out==7); // Never exceed cap through rounding tolerance.
    assert(!lease.Apply(out,7.25f,true,speed)); assert(lease.Release(out,speed) && speed==7);
    for(float bad:{-1.f,NAN,INFINITY,63.75f}) assert(!lease.Apply(7,bad,true,out));
    assert(!lease.Apply(7,3,false,out) && !lease.Held()); // self/stale/unsafe/off eligibility is false
    assert(lease.Apply(7,3,true,out));
    assert(!lease.Release(1,out)); // New server slow wins, even when cleanup races it.
    assert(lease.Apply(1,.5f,true,out) && out==.5f);
    assert(lease.Release(.5f,out) && out==1);
    assert(lease.Apply(7,3,true,out));
    assert(lease.Apply(14,5,true,out) && out==5); // Observed server boost replaces old allowance.
    assert(lease.Release(5,out) && out==14);
    assert(lease.Apply(7,3,true,out));
    assert(lease.Release(3,out) && out==7); // Pre-handler release, then server sends exactly our old applied value3.
    assert(lease.Apply(3,2,true,out) && out==2); assert(lease.Release(2,out) && out==3);
    assert(lease.Apply(7,0,true,out)); assert(lease.Apply(0,1,true,speed) && speed==1);
    assert(lease.Release(1,out) && out==7);
    assert(!lease.Apply(0,1,true,out)); // A true server zero cannot be raised.
    {
        skatepeerspeed::Lease predicted; float remote=7;
        double actual=0,old=0,corrected=0,worstOld=0,worstFixed=0;
        for(unsigned ms=0;ms<4000;ms+=10) {
            const float control=5.f-4.f*ms/4000.f;
            const unsigned delayed=ms>100?ms-100:0;
            const float report=nearbyintf((5.f-4.f*((delayed/100)*100)/4000.f)*4)/4;
            assert(predicted.Apply(remote,report,true,out)); remote=out;
            actual+=control*.01; old+=7*.01; corrected+=remote*.01;
            worstOld=(std::max)(worstOld,fabs(old-actual)); worstFixed=(std::max)(worstFixed,fabs(corrected-actual));
            if(ms%500==490) old=corrected=actual;
        }
        printf("synthetic coast native prediction max error constant7=%.6f replicated=%.6f yd\n",worstOld,worstFixed);
        assert(worstOld>2 && worstFixed<.2 && worstFixed<worstOld*.1);
    }
    uint64_t guid; const uint8_t packed[]{0x85,1,2,3};
    assert(skatepeerspeed::PackedGuid(packed,sizeof(packed),guid) && guid==(1ull|(2ull<<16)|(3ull<<56)));
    assert(!skatepeerspeed::PackedGuid(packed,3,guid) && !skatepeerspeed::PackedGuid(nullptr,0,guid));
    if(const char* file=getenv("SKATE_SPEED_TRACE")) {
        struct Sample { double t,run; uint32_t flags; };
        std::vector<Sample> samples; std::ifstream in(file); std::string line; std::getline(in,line);
        while(std::getline(in,line)) {
            std::istringstream row(line); std::vector<std::string> cells; std::string value;
            while(std::getline(row,value,',')) cells.push_back(value);
            assert(cells.size()==40);
            if(cells[2]=="1" && cells[3]=="1" && cells[4]=="4")
                samples.push_back({std::stod(cells[0]),std::stod(cells[11]),uint32_t(std::stoul(cells[13]))});
        }
        assert(samples.size()>500);
        double truePosition=0,baseline=0,fixed=0,baseMax=0,fixedMax=0,resetAt=0,lastReport=0;
        float reported=0; unsigned ground=0; size_t delayed=0;
        skatepeerspeed::Lease prediction; float receiverRun=7;
        for(size_t i=1;i<samples.size();++i) {
            const auto& s=samples[i]; const double dt=(s.t-samples[i-1].t)/1000;
            const bool eligible=(s.flags&1) && !(s.flags&(0x3000|0x100|0x200000|0x1000000)) && s.run>=0 && s.run<=7;
            if(!eligible || dt<=0 || dt>.15) { truePosition=baseline=fixed=0; resetAt=s.t; prediction.Release(receiverRun,out); receiverRun=7; continue; }
            // Native ordinary forward extrapolation, equal exact position snapshots every500ms.
            // Replay the recorded sender speed unchanged; only the viewer's speed knowledge differs.
            while(delayed+1<i && samples[delayed+1].t<=s.t-100) ++delayed;
            if(s.t-lastReport>=100) {
                skatepeer::State state; state.speed=float(samples[delayed].run); uint8_t bytes[skatepeer::Size];
                assert(skatepeer::Encode(state,bytes)); skatepeer::State decoded; assert(skatepeer::Decode(bytes,sizeof(bytes),decoded));
                reported=decoded.speed; lastReport=s.t;
            }
            if(prediction.Apply(receiverRun,reported,true,out)) receiverRun=out;
            truePosition+=s.run*dt; baseline+=7*dt; fixed+=receiverRun*dt;
            baseMax=(std::max)(baseMax,fabs(baseline-truePosition)); fixedMax=(std::max)(fixedMax,fabs(fixed-truePosition));
            if(s.t-resetAt>=500) { baseline=fixed=truePosition; resetAt=s.t; }
            ++ground;
        }
        printf("recorded ground samples=%u 500ms prediction error constant7=%.6f replicated=%.6f yd (100ms delay,10Hz,0.25yd/s quantization)\n",ground,baseMax,fixedMax);
        assert(ground>300 && baseMax>1 && fixedMax<baseMax*.3);
    }
}
