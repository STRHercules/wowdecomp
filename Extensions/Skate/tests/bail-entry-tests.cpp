#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "bail-recovery.h"
#include "skate-visual.h"
#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL bail-entry %u %s\n",checks,why);exit(1);}}
struct Row{unsigned trial,state; bailrecovery::Metadata m;float period;};
static std::vector<unsigned char> Load(const char* path){std::ifstream f(path,std::ios::binary);Check(f.good(),"recorded fixture exists");return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
int main(){
    bailrecovery::Entry entry;
    Check(!entry.Observe(1,10,.016667f,500,true),"unprimed biped denied");
    entry.Observe(1,11,.016667f,100,true);
    Check(entry.Observe(1,12,.016667f,500,true),"riding to collision biped admitted");
    for(unsigned n=0;n<100;++n)Check(entry.Observe(1,12,.016667f,500,true),"same tick does not alter start");
    Check(!entry.Observe(1,28,.016667f,500,true),"expired or skipped entry denied");
    Check(!entry.Observe(1,29,.016667f,500,true),"persisting biped cannot renew");
    for(unsigned state:{0u,104u,105u,501u,502u,503u,600u,601u,602u,700u,701u,702u}){
        entry.Clear();entry.Observe(1,10,.016667f,100,true);
        Check(!entry.Observe(1,11,.016667f,state,true),"unproven states not entry");
    }
    entry.Clear();entry.Observe(1,10,.016667f,100,true);
    Check(!entry.Observe(1,11,.016667f,500,false),"stale denied");
    entry.Observe(1,12,.016667f,100,true);
    Check(!entry.Observe(2,13,.016667f,500,true),"epoch cannot inherit priming");
    entry.Observe(2,14,.016667f,100,true);
    Check(!entry.Observe(2,13,.016667f,500,true),"reverse tick denied");
    entry.Clear();entry.Observe(1,10,.016667f,100,true);
    for(unsigned t=11;t<=76;++t){
        const bool accepted=entry.Observe(1,t,.016667f,500,true);
        Check(accepted==((t-11)*.016667f<=bailrecovery::EntrySeconds),"advancing ticks bound real entry to one second without renewal");
    }
    Check(entry.expired,"timeout distinguished from unsupported state");
    entry.Clear();entry.Observe(1,10,.016667f,100,true);entry.Observe(1,11,.016667f,500,true);
    Check(!entry.Observe(1,12,.008333f,500,true) && entry.expired,"changed period cannot extend budget");
    unsigned longHandoffs=0;
    for(unsigned stride:{1u,3u,8u}) {
        entry.Clear();entry.Observe(1,100,1.f/60,100,true);
        for(uint64_t tick=101;tick<144;tick+=stride)Check(entry.Observe(1,tick,1.f/60,500,true),"longer supported impact remains typed entry");
        Check(!entry.Observe(1,144,1.f/60,300,true) && !entry.active,"genuine ragdoll replaces long entry, not a fabricated state");
        ++longHandoffs;
    }
    const auto recordedBytes=Load("D:\\world of skatecraft 3.3.5\\native-probe\\tests\\prebail-elevated-frame.bin");
    const auto recordedSchema=Load("D:\\world of skatecraft 3.3.5\\native-probe\\tests\\prebail-elevated-schema.bin");
    const auto recordedMesh=skatevisual::Schema(recordedSchema);Check(recordedMesh!=nullptr,"exact captured elevated-impact schema");
    skatevisual::Failure failure;
    Check(!skatevisual::Decode(recordedBytes,*recordedMesh,false,&failure) && !strcmp(failure.reason,"board_root_bound")
        && failure.point==24 && failure.horizontal>4 && failure.horizontal<4.1f,"recorded old four-metre rejection reproduced");
    bailrecovery::Metadata m{740,0,0,false};
    entry.Clear();entry.Observe(369,723,1.f/60,100,true);
    for(uint64_t t=725;t<=740;++t)Check(entry.Observe(369,t,1.f/60,500,true),"exact captured transition retains typed scope");
    Check(bailrecovery::DetachedBoard(m,500,entry.active),"pending transition can detach without claiming a bail cycle");
    Check(skatevisual::Decode(recordedBytes,*recordedMesh,bailrecovery::DetachedBoard(m,500,entry.active))!=nullptr,"exact rejected elevated-surface frame accepted with bounded detached validation");
    Check(!bailrecovery::DetachedBoard(m,500,false) && !bailrecovery::DetachedBoard(m,100,true)
        && !bailrecovery::DetachedBoard({},500,true),"unprimed biped, riding and unpaired metadata never detach");
    auto malformed=recordedBytes;const float anchor=skatewire::GetFloat(malformed.data()+8)+5;
    memcpy(malformed.data()+72,&anchor,4);
    Check(!skatevisual::Decode(malformed,*recordedMesh,true,&failure) && !strcmp(failure.reason,"body_root_bound"),"entry still retains body four-metre limit");
    malformed=recordedBytes;
    for(size_t i=24;i<24+recordedMesh->colors.size();++i){const float x=skatewire::GetFloat(malformed.data()+72+i*12)+40;memcpy(malformed.data()+72+i*12,&x,4);}
    Check(!skatevisual::Decode(malformed,*recordedMesh,true,&failure) && !strcmp(failure.reason,"board_root_bound")
        && failure.bound==32,"entry retains detached board absolute separation bound");
    malformed=recordedBytes;const float vertex=skatewire::GetFloat(malformed.data()+72+24*12)+6;
    memcpy(malformed.data()+72+24*12,&vertex,4);
    Check(!skatevisual::Decode(malformed,*recordedMesh,true,&failure) && !strcmp(failure.reason,"board_shape"),"entry retains compact board geometry check");
    std::ifstream input("D:\\world of skatecraft 3.3.5\\native-probe\\build\\bail-entry-trajectory.csv");
    Check(input.good(),"actual collision fixture exists");
    std::vector<Row> rows;std::string line;
    while(std::getline(input,line)){
        for(char& c:line)if(c==',')c=' ';
        std::istringstream stream(line);Row r{};unsigned active=0;
        stream>>r.trial>>r.m.tick>>r.state>>r.m.cycle>>r.m.checkpoint>>active>>r.period;
        Check(!stream.fail(),"real trajectory parses");r.m.active=active!=0;rows.push_back(r);
    }
    Check(rows.size()==4320,"six real 720-tick approaches");
    unsigned replays=0,entrySamples=0,baselineStops=0,completed=0;float longest=0;
    for(unsigned stride:{1u,3u,8u})for(unsigned trial=0;trial<6;++trial){
        bailrecovery::Entry window;bailrecovery::Phase phase;bool bailed=false,seen=false,reset=false;
        uint64_t first=0,last=0;unsigned count=0;
        for(const Row& r:rows){
            if(r.trial!=trial || count++%stride)continue;
            const bool pending=window.Observe(trial+1,r.m.tick,r.period,r.state,true);
            if(r.state==500){Check(pending,"real pre-bail window retained at all batches");++entrySamples;seen=true;if(!first)first=r.m.tick;last=r.m.tick;}
            const bool recovering=r.m.active && bailrecovery::Recoverable(r.state);
            const bool ordinary=bailrecovery::Rolling(r.state) || (r.state>=200 && r.state<=202);
            if(!ordinary && !recovering && !pending)Check(false,"no unsupported state allowed in fixture");
            Check(phase.Observe(trial+1,r.m,r.period,r.state,true,false,false,reset),"actual recovery metadata stays valid");
            if(pending)Check(!r.m.active && r.m.cycle==0 && !phase.active,"entry never fabricates actual bail cycle");
            if(r.state==300)bailed=true;
        }
        Check(bailed,"same engine reaches genuine ragdoll without reset");
        if(seen){++baselineStops;longest=(std::max)(longest,static_cast<float>(last-first+1)/60.f);}
        if(trial>=3){Check(seen,"reported-location approach reproduces pre-bail state");++completed;}
        ++replays;
    }
    printf("{\"passed\":true,\"checks\":%u,\"engine_frames\":4320,\"batch_replays\":%u,\"baseline_stop_replays\":%u,\"entry_samples\":%u,\"entry_to_genuine_bail_replays\":%u,\"longest_observed_entry_seconds\":%.6f,\"timeout_seconds\":%.2f,\"long_transition_policy_replays\":%u,\"exact_rejected_frame_tested\":true,\"body_and_board_bounds_tested\":true,\"unknown_states_rejected\":true,\"fake_bail_cycle\":false,\"exact_recorded_route\":false,\"live_verified\":false}\n",checks,replays,baselineStops,entrySamples,completed,longest,bailrecovery::EntrySeconds,longHandoffs);
    return 0;
}
