#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "flight-handoff.h"
#include <fstream>
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok){++checks;if(!ok){fprintf(stderr,"FAIL wall/jump %u\n",checks);exit(1);}}
static std::vector<unsigned char> Load(const char* path){std::ifstream file(path,std::ios::binary);Check(file.good());return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
int main(){
    const auto bytes=Load("build/rejected-visuals/pid-10708-epoch-51-tick-7328-frame.bin");
    const auto schema=Load("build/rejected-visuals/pid-10708-epoch-51-tick-7328-schema.bin");
    const auto mesh=skatevisual::Schema(schema);Check(mesh!=nullptr);skatevisual::Failure failure;
    Check(!skatevisual::Decode(bytes,*mesh,true,&failure) && !strcmp(failure.reason,"board_root_bound"));
    auto frame=skatevisual::DecodePaired(bytes,*mesh,7328,300,true,&failure);
    Check(frame && frame->boardHidden && !strcmp(failure.reason,"none"));
    Check(!skatevisual::DecodePaired(bytes,*mesh,7328,300,false));
    for(unsigned i=0;i<24;++i)Check(frame->anchors[i].x==skatewire::GetFloat(bytes.data()+72+i*12));
    auto bad=bytes;float nan=NAN;memcpy(bad.data()+72+24*12,&nan,4);
    Check(!skatevisual::DecodePaired(bad,*mesh,7328,300,true));
    bad=bytes;float huge=10001;memcpy(bad.data()+72+24*12,&huge,4);
    Check(!skatevisual::DecodePaired(bad,*mesh,7328,300,true));
    Check(flighthandoff::PendingPop(true,true,true,false,103,0x80001001,false,true,16,.529968f));
    for(unsigned state:{0u,100u,200u,300u,500u})Check(!flighthandoff::PendingPop(true,true,true,false,state,0x80001001,false,true,16,.53f));
    Check(!flighthandoff::PendingPop(true,false,true,false,103,0x80001001,false,true,16,.53f));
    Check(!flighthandoff::PendingPop(true,true,true,false,103,0x80001001,true,true,16,.53f));
    Check(!flighthandoff::PendingPop(true,true,true,false,103,0x80001001,false,false,16,.53f));
    Check(!flighthandoff::PendingPop(true,true,true,false,103,0x80001001,false,true,600,.53f));
    Check(!flighthandoff::PendingPop(true,true,true,false,103,0x80001001,false,true,16,3.f));
    Check(!flighthandoff::PendingPop(true,true,true,false,103,0x80001801,false,true,16,.53f));
    printf("{\"passed\":true,\"checks\":%u,\"exact_detached_board_rejection_reproduced\":true,\"detached_board_culled_not_body\":true,\"recorded_pending_pop_admitted\":true,\"collision_identity_finite_guards_retained\":true,\"live_verified\":false}\n",checks);
}
