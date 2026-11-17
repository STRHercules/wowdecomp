#include "checkpoint-handoff.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <initializer_list>
static unsigned checks=0;
static void Check(bool result){++checks;if(!result){std::fprintf(stderr,"checkpoint check %u failed\n",checks);std::exit(1);}}
int main(){
    using namespace checkpointhandoff;
    for(unsigned source:{100u,103u,200u,201u,202u,500u,501u,502u,503u}){
        Check(Ordinary(source));
        Gate g;
        Check(g.Observe(4,10,source,1000,true,true,0,0,0,true)==Action::None);
        Check(g.Observe(4,11,702,1010,true,true,0,0,0,false)==Action::Wait);
        Check(g.Observe(4,12,702,1030,true,true,0,0,0,false)==Action::Wait);
        Check(g.Observe(4,13,500,1050,true,true,0,0,.98f,true)==Action::Resume);
        Check(!g.active);
    }
    for(unsigned bad:{0u,300u,400u,405u,701u,999u}){
        Check(!Ordinary(bad));Gate g;
        Check(g.Observe(1,3,702,0,true,true,0,0,0,true)==Action::Wait);
        Check(g.Observe(1,4,bad,20,true,true,0,0,0,true)==Action::Refuse);
    }
    for(unsigned n=0;n<8;++n){
        Gate g;Check(g.Observe(1,3,702,100,true,true,0,0,0,true)==Action::Wait);
        const float nan=std::numeric_limits<float>::quiet_NaN();
        Check(g.Observe(n==0?2:1,n==1?3:4,500,n==2?201:120,n!=3,n!=4,
            n==5?1.251f:0,n==6?nan:0,0,n!=7)==Action::Refuse);
    }
    Gate wrap;Check(wrap.Observe(1,1,702,0xfffffff0u,true,true,0,0,0,true)==Action::Wait);
    Check(wrap.Observe(1,2,500,20,true,true,0,0,.5f,true)==Action::Resume);
    for(unsigned n=0;n<1000;++n){
        Gate g;Check(g.Observe(9,100,702,10,true,true,0,0,0,false)==Action::Wait);
        Check(g.Observe(9,102,500,45,true,true,0,0,1.0f,true)==Action::Resume);
    }
    std::printf("{\"passed\":true,\"checks\":%u,\"typed_checkpoint_only\":true,\"max_wait_ms\":100,\"max_reset_yards\":1.25,\"fresh_epoch_tick_and_finite_guards\":true,\"unsupported_or_large_reset_refused\":true,\"no_position_writes\":true,\"live_verified\":false}\n",checks);
}
