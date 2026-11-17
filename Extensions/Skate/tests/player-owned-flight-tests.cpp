#define NOMINMAX
#include "ollie-controller.h"
#include "skate-vertical.h"
#include "bail-recovery.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok){++checks;if(!ok){fprintf(stderr,"FAIL player flight %u\n",checks);exit(1);}}
int main(){
    ollie::Flight flight;Check(flight.Begin(1000,1,1,500));
    for(uint32_t age=0;age<3600000;age+=1000)
        Check(flight.Check(1000+age,1,0x3000,false,500-age*.0001f,NAN,true,true));
    Check(!flight.Check(20000,2,0x3000,false,490,NAN,true,true));
    Check(!flight.Check(20000,1,0x3000,true,490,NAN,true,true));
    Check(!flight.Check(20000,1,0x3100,false,490,NAN,true,true));
    Check(!flight.Check(20000,1,0x3000,false,NAN,NAN,true,true));
    Check(!flight.Check(20000,1,0x3000,false,490,NAN,true,false));
    skatevertical::Motor motor;motor.active=true;motor.rootDriven=true;motor.epoch=1;
    for(uint32_t ms=1;ms<=120000;++ms)Check(motor.Advance(ms));
    const float sourceSeconds=motor.arcSeconds;
    Check(sourceSeconds>110 && motor.Advance(120151));
    Check(motor.arcSeconds==sourceSeconds && !motor.Advance(120150));
    skatevertical::Motor legacy;legacy.active=true;
    Check(!legacy.Advance(151));
    bailrecovery::Phase phase;bool reset=false;
    for(uint64_t tick=1;tick<=6000;++tick)
        Check(phase.Observe(1,{tick,1,0,true},1.f/60,300,true,false,true,reset,true));
    Check(!phase.Observe(1,{6001,1,0,true},1.f/60,999,true,false,true,reset,true));
    printf("{\"passed\":true,\"checks\":%u,\"fresh_flight_no_age_limit\":true,\"source_motor_no_age_limit\":true,\"recovery_no_age_limit\":true,\"identity_freshness_finite_guards_retained\":true}\n",checks);
}
