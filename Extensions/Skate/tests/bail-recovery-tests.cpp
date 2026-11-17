#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "bail-recovery.h"
#include "vanilla-root.h"
#include "air-root-motion.h"
#include "skate-vertical.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why) { ++checks;if(!ok){fprintf(stderr,"FAIL recovery %u %s\n",checks,why);exit(1);} }
struct Row { unsigned trial,source; bailrecovery::Metadata meta;float x,z,y,period; };
int main() {
    bailrecovery::Phase guards;bool reset=false;
    Check(!guards.Observe(1,{1,0,0,true},.016667f,300,false,false,false,reset),"stale cannot own recovery");
    Check(!guards.Observe(1,{2,0,0,false},.016667f,702,true,false,false,reset),"unprimed checkpoint cannot own recovery");
    Check(guards.Observe(1,{3,1,0,true},.016667f,300,true,false,false,reset) && guards.active,"real wipeout starts recovery");
    Check(!guards.Observe(1,{4,1,0,true},.016667f,0,true,false,false,reset),"unknown state is not recovery");
    Check(!guards.Observe(1,{1000,1,0,true},.016667f,300,true,false,false,reset),"bounded recovery cannot run forever");
    guards.Clear();Check(guards.Observe(1,{10,1,0,true},.016667f,300,true,false,false,reset),"begin");
    Check(!guards.Observe(1,{9,1,0,true},.016667f,300,true,false,false,reset),"reverse tick refused");
    Check(!guards.Observe(2,{11,1,1,false},.016667f,100,true,true,false,reset),"epoch reset cannot inherit checkpoint");
    std::vector<unsigned char> bytes;
    for(unsigned v:{3u,0u,1u,0u,1u})skatewire::Word(bytes,v);
    bailrecovery::Metadata decoded;
    Check(bailrecovery::Decode(bytes,3,decoded) && decoded.active,"paired metadata");
    Check(!bailrecovery::Decode(bytes,4,decoded),"wrong-tick metadata refused");
    bytes.back()=2;Check(!bailrecovery::Decode(bytes,3,decoded),"invalid boolean refused");
    std::ifstream input("D:\\world of skatecraft 3.3.5\\native-probe\\build\\recovery-trajectory.csv");
    Check(input.good(),"real engine recovery trajectories exist");
    std::vector<Row> rows;std::string line;
    while(std::getline(input,line)) {
        for(char& c:line)if(c==',')c=' ';
        std::istringstream stream(line);Row row{};unsigned active=0;
        stream>>row.trial>>row.meta.tick>>row.source>>active>>row.meta.cycle>>row.meta.checkpoint>>row.x>>row.z>>row.y>>row.period;
        Check(!stream.fail(),"trajectory parse");row.meta.active=active!=0;rows.push_back(row);
    }
    Check(rows.size()==2700,"three actual engine scenarios");
    unsigned completed=0,checkpoints=0;float maximumGap=0;
    for(unsigned stride:{1u,3u,8u}) for(unsigned trial=0;trial<3;++trial) {
        bailrecovery::Phase phase;vanillaroot::History vertical;airroot::History horizontal;skatevertical::Motor motor;
        bool seen=false,flight=false;unsigned ground=0,nativeMs=0;uint32_t now=1000;
        float x=0,y=0,z=0;unsigned count=0;
        for(const Row& row:rows) {
            if(row.trial!=trial || count++%stride)continue;
            now+=static_cast<uint32_t>(stride*row.period*1000+.5f);
            const bool contact=z<.08f;
            Check(phase.Observe(trial+1,row.meta,row.period,row.source,true,contact,flight,reset),"real lifecycle admitted at varied batching");
            if(reset) {vertical.Clear();horizontal.Clear();motor.Clear();++checkpoints;}
            Check(vertical.Step(trial+1,row.meta.tick,row.period,row.z/.9144f,now),"checkpoint does not become vertical impulse");
            Check(horizontal.Step(trial+1,row.meta.tick,row.period,row.x/.9144f,-row.y/.9144f,now),"root XY finite and bounded");
            if(!phase.active) { x=row.x/.9144f;y=-row.y/.9144f;z=0; }
            else {
                seen=true;flight=true;motor.arcSeconds=0;
                const bool rolling=bailrecovery::Rolling(row.source);
                const bool sample=motor.RootSample(trial+1,now,row.meta.tick,row.period,vertical.height,vertical.velocity,rolling,nativeMs,now);
                if(!sample)fprintf(stderr,"stride=%u trial=%u tick=%llu now=%u at=%u ms=%u sample_ms=%u root=%.6f velocity=%.6f active=%u\n",stride,trial,row.meta.tick,now,motor.at,nativeMs,motor.nativeSampleMs,vertical.height,vertical.velocity,motor.active?1u:0u);
                Check(sample,"root motor owns recovery sample");
                const unsigned dt=static_cast<unsigned>(stride*row.period*1000+.5f);
                // Approximate native floor sweep, not a claim of live ABI testing.
                // Subdivide like native movement: no individual sweep exceeds100ms.
                for(unsigned elapsed=0;elapsed<dt;) {
                    const unsigned step=(std::min)(17u,dt-elapsed);airroot::Point delta{0,0,0};float down=0;
                    Check(horizontal.Plan(now,trial+1,step,x,y,delta,vertical.height-z),"owned recovery XY request");
                    Check(motor.Delta(now,trial+1,nativeMs,nativeMs+step,z,down),"owned recovery vertical request");
                    x+=delta.x;y+=delta.y;z=(std::max)(0.f,z-down);nativeMs+=step;elapsed+=step;
                }
                maximumGap=(std::max)(maximumGap,hypotf(x-row.x/.9144f,y+row.y/.9144f));
                if(rolling && !row.meta.active && contact)++ground;else ground=0;
                if(ground>=3){flight=false;motor.Clear();}
            }
        }
        Check(seen && !phase.active,"same lifecycle returns to ordinary riding");++completed;
    }
    printf("{\"passed\":true,\"checks\":%u,\"engine_frames\":2700,\"batch_replays\":%u,\"checkpoint_resets\":%u,\"max_tracking_gap_yards\":%.6f,\"native_sweep_approximated\":true,\"live_verified\":false}\n",checks,completed,checkpoints,maximumGap);
}
