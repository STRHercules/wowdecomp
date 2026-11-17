#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "trick-score.h"
#include <cstdio>
#include <cstdlib>
int main() {
    unsigned checks=0;const auto check=[&](bool ok){++checks;if(!ok){fprintf(stderr,"score check %u failed\n",checks);exit(1);}};
    trickstate::State s{};s.valid=true;s.tick=1;
    trickscore::Tracker t;t.Observe(1,0,true,s);check(t.Show(0).kind==0);
    strcpy_s(s.live,"ID_TRICK_KICKFLIP");s.points=2000;++s.tick;t.Observe(1,100,true,s);
    check(t.Show(100).points==2000 && t.Show(100).kind==1);
    s.points=300;++s.tick;t.Observe(1,150,true,s);check(t.Show(150).points==2000);
    s.points=0;s.live[0]=0;s.landed=1;s.total=120;strcpy_s(s.last,"ID_TRICK_KICKFLIP");++s.tick;t.Observe(1,200,true,s);
    check(t.Show(200).kind==2 && t.Show(200).points==2000);
    check(!strcmp(t.Show(200).name,"ID_TRICK_KICKFLIP"));
    check(t.Show(2699).kind==2 && t.Show(3699).kind==2 && t.Show(3701).kind==0);
    t.Observe(1,300,true,s);check(t.landedAt==200); // duplicate Source tick cannot rebank
    s.points=120;strcpy_s(s.live,"ID_TRICK_OLLIE");++s.tick;t.Observe(1,10000,true,s);
    s.points=0;++s.tick;t.Observe(1,10500,true,s);check(t.Show(10600).kind==3 && t.Show(10600).points==120);
    check(t.Show(12999).kind==3 && t.Show(13001).kind==0);
    ++s.tick;s.landed=2;s.total=420;strcpy_s(s.last,"ID_TRICK_HEELFLIP");t.Observe(1,14000,true,s);
    check(t.Show(14000).points==300 && t.Show(14000).kind==2);
    ++s.tick;t.Observe(2,14050,true,s);check(t.Show(14050).kind==0); // epoch baselines cannot replay old totals
    ++s.tick;s.points=80;t.Observe(2,15000,true,s);t.Observe(2,15010,false,{});
    check(t.Show(15010).kind==0 && !t.hasBail); // technical pause is not an invented bail
    ++s.tick;s.points=0;t.Observe(2,15020,true,s);check(t.Show(15020).kind==0);
    ++s.tick;s.points=NAN;t.Observe(2,15030,true,s);check(t.Show(15030).kind==0);
    ++s.tick;s.points=50;t.Observe(2,0xfffffff0u,true,s);++s.tick;s.points=0;t.Observe(2,0xfffffff8u,true,s);
    check(t.Show(10).kind==3 && t.Show(10).age<.1f);
    printf("{\"passed\":true,\"checks\":%u,\"reference_peak_and_bank_delta\":true,\"no_replayed_epoch_rewards\":true,\"pause_is_not_bail\":true,\"server_rewards_added\":false}\n",checks);
}
