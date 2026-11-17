#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include <fstream>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok,const char* why){++checks;if(!ok){fprintf(stderr,"FAIL air board validation %u %s\n",checks,why);exit(1);}}
static std::vector<unsigned char> Load(const std::string& path){std::ifstream file(path,std::ios::binary);Check(file.good(),"exact captured fixture exists");return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
static void FloatAt(std::vector<unsigned char>& bytes,size_t offset,float value){memcpy(bytes.data()+offset,&value,4);}
int main(){
    unsigned cases=0;
    struct Fixture{const char* name;unsigned state;};
    for(auto fixture:{Fixture{"pid-39816-epoch-90-tick-1537",201},Fixture{"pid-39816-epoch-328-tick-910",200},Fixture{"pid-10728-epoch-113-tick-1073",100}}){
        const std::string prefix=std::string("build/rejected-visuals/")+fixture.name;
        const auto bytes=Load(prefix+"-frame.bin"),schema=Load(prefix+"-schema.bin");
        auto mesh=skatevisual::Schema(schema);Check(mesh!=nullptr,"exact captured schema");
        skatevisual::Failure failure;
        Check(!skatevisual::Decode(bytes,*mesh,false,&failure) && !strcmp(failure.reason,"board_root_bound"),"reproduce exact old mode-ending rejection");
        const auto tick=static_cast<uint64_t>(skatewire::Get(bytes.data())) | (static_cast<uint64_t>(skatewire::Get(bytes.data()+4))<<32);
        const unsigned state=fixture.state;
        auto frame=skatevisual::DecodePaired(bytes,*mesh,tick,state,false,&failure);
        Check(frame && frame->independentBoard && !strcmp(failure.reason,"none"),"exact formerly rejected frame has valid independently bounded board");
        const std::vector<unsigned char> summary(bytes.begin(),bytes.begin()+60);skatewire::Pose pose{};
        Check(skatewire::Decode(summary,pose) && pose.tick==frame->pose.tick
            && !memcmp(pose.position,frame->pose.position,sizeof(pose.position))
            && !memcmp(pose.velocity,frame->pose.velocity,sizeof(pose.velocity))
            && !memcmp(pose.forward,frame->pose.forward,sizeof(pose.forward))
            && pose.period==frame->pose.period && pose.bones==frame->pose.bones
            && pose.airborne==frame->pose.airborne && pose.ground==frame->pose.ground,
            "all serialized Source movement fields unchanged, excluding C++ struct padding");
        for(size_t i=0;i<24+mesh->colors.size();++i){
            const auto p=i<24?frame->anchors[i]:frame->vertices[i-24];
            Check(p.x==skatewire::GetFloat(bytes.data()+72+i*12)
                && p.y==skatewire::GetFloat(bytes.data()+76+i*12)
                && p.z==skatewire::GetFloat(bytes.data()+80+i*12),"no body/board vertex snapping, scaling or position alteration");
        }
        Check(!skatevisual::DecodePaired(bytes,*mesh,tick+1,state),"stale or unpaired state cannot borrow permission");
        for(unsigned wrong:{0u,99u,104u,199u,203u,300u,400u,500u,702u})Check(!skatevisual::DecodePaired(bytes,*mesh,tick,wrong),"no generic widened riding permission");
        for(unsigned riding:{100u,101u,102u,103u,200u,201u,202u}){
            Check(skatevisual::RidingBoard(pose,tick,riding)==((riding<200)==(state<200)),"riding state must match actual ground/air facts");
        }
        auto bad=bytes;FloatAt(bad,72,frame->pose.position[0]+4.5f);
        Check(!skatevisual::DecodePaired(bad,*mesh,tick,state,false,&failure) && !strcmp(failure.reason,"body_root_bound"),"unchanged tight body-root guard");
        bad=bytes;FloatAt(bad,72+24*12,NAN);
        Check(!skatevisual::DecodePaired(bad,*mesh,tick,state,false,&failure) && !strcmp(failure.reason,"coordinate"),"nonfinite board data remains rejected");
        bad=bytes;FloatAt(bad,72+24*12,frame->vertices[0].x+3);
        Check(!skatevisual::DecodePaired(bad,*mesh,tick,state,false,&failure) && !strcmp(failure.reason,"board_shape"),"stretched/malformed board still rejected in board-local domain");
        bad=bytes;
        for(size_t i=0;i<mesh->colors.size();++i)FloatAt(bad,72+(24+i)*12,frame->vertices[i].x+40);
        Check(!skatevisual::DecodePaired(bad,*mesh,tick,state,false,&failure) && !strcmp(failure.reason,"board_root_bound"),"bounded local scene envelope remains");
        bad=bytes;bad.pop_back();Check(!skatevisual::DecodePaired(bad,*mesh,tick,state),"truncated frame still rejected");
        auto phase=pose;phase.ground=1;phase.airborne=1;Check(!skatevisual::RidingBoard(phase,tick,state),"contradictory ground/air facts rejected");
        phase=pose;phase.ground=0;phase.airborne=0;Check(!skatevisual::RidingBoard(phase,tick,state),"actual phase fact required");
        phase=pose;phase.tick=0;Check(!skatevisual::RidingBoard(phase,0,state),"zero tick cannot grant independent validation");
        ++cases;
    }
    printf("{\"passed\":true,\"checks\":%u,\"exact_live_captures\":%u,\"old_rejections_reproduced\":true,\"paired_source_riding_required\":true,\"ground_air_continuity_tested\":true,\"body_and_shape_and_finite_guards_retained\":true,\"source_motion_and_vertices_unchanged\":true,\"no_fake_support_or_jump\":true,\"live_verified\":false}\n",checks,cases);
}
