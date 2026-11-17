#include "rolling-controller.h"
#include "run-speed-lease.h"
#include "skate-ground-motion.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks = 0;
static void Require(bool condition, const char* name) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}
int main() {
    Require(skatemotion::GroundContactWait(0x80001001,false,true,.001f,0),"observed activation flag waits with verified near-ground contact");
    Require(skatemotion::GroundContactWait(0x80001001,false,true,-.01f,249),"brief contact settlement keeps J active");
    Require(!skatemotion::GroundContactWait(0x80001001,false,true,0,250),"persistent falling still stops");
    Require(!skatemotion::GroundContactWait(0x80001001,false,true,.126f,0),"real airborne gap still stops");
    Require(!skatemotion::GroundContactWait(0x80001001,false,true,NAN,0),"missing ground still stops");
    Require(!skatemotion::GroundContactWait(0x80001001,true,true,0,0),"server spline cannot enter hold");
    Require(!skatemotion::GroundContactWait(0x80001001,false,false,0,0),"no stale unowned hold");
    Require(!skatemotion::GroundContactWait(1,false,true,0,0),"normal movement does not enter falling hold");
    Require(skatemotion::GroundContactWait(0x80005001,false,true,.000694f,16),"observed Wrath pending-stop is not Vanilla far-fall");
    for (unsigned flag : {0x100u,0x200u,0x400u,0x800u,0x2000u,0x100000u,0x200000u,0x1000000u,0x2000000u})
        Require(!skatemotion::GroundContactWait(0x1000u|flag,false,true,0,0),"walking/rooted/transport/swim/flying/far-fall still stop");
    Require(skatemotion::OwnBodyHit(1,.5f,1),"only local model can be skipped by body ray");
    Require(!skatemotion::OwnBodyHit(0,.5f,1) && !skatemotion::OwnBodyHit(2,.5f,1),"terrain and other objects remain obstacles");
    Require(!skatemotion::OwnBodyHit(1,NAN,1) && !skatemotion::OwnBodyHit(1,-.1f,1)
        && !skatemotion::OwnBodyHit(1,1.1f,1),"invalid collision hits never grant movement");
    Require(skatemotion::Ground(.1f*.9144f,0,true,false,1,1,10,10,7).speed==0,"settling wobble cannot nudge player");
    Require(skatemotion::Ground(.2f*.9144f,0,true,false,1,1,10,10,7).speed>.19f,"actual push speed passes settling filter");
    const auto motion=skatemotion::Ground(.9144f,0,true,false,2,2,110,100,7);
    Require(motion.valid && fabsf(motion.speed-1)<.0001f && motion.heading==0,"Skate metres map to Wrath yards");
    const auto turn=skatemotion::Ground(0,.9144f,true,false,2,2,110,100,7);
    Require(turn.valid && fabsf(turn.heading+1.5707963268f)<.0001f,"Skate Z maps to negative WoW Y");
    Require(skatemotion::Ground(10,0,true,false,2,2,110,100,7).speed==7,"ground parity uses ordinary server run cap");
    Require(skatemotion::Ground(10,0,true,false,2,2,110,100,2).speed==2,"real engine request obeys lower server limit");
    Require(!skatemotion::Ground(1,0,true,false,2,2,250,100,7).valid,"stale Skate velocity cannot move Wrath");
    Require(!skatemotion::Ground(1,0,true,false,2,3,110,100,7).valid,"wrong epoch cannot move Wrath");
    Require(!skatemotion::Ground(1,0,false,false,2,2,110,100,7).valid
        && !skatemotion::Ground(1,0,true,true,2,2,110,100,7).valid,"first movement adapter is ground-only");
    Require(!skatemotion::Ground(NAN,0,true,false,2,2,110,100,7).valid,"invalid engine velocity rejected");
    Require(skatemotion::Ground(0,0,true,false,2,2,110,100,7).speed==0,"real Skate braking yields stop request");
    RollingController c;
    c.Step(.02f, true, false);
    Require(c.speed == 0, "normal mode cannot accelerate");
    c.enabled = true;
    for (int i=0;i<50;++i) c.Step(.02f, true, false);
    Require(std::fabs(c.speed-3.0f)<.001f, "one second acceleration");
    for (int i=0;i<50;++i) c.Step(.02f, true, false);
    Require(c.speed == 4.0f, "maximum speed clamp");
    c.Step(.02f, false, false);
    Require(c.speed > 3.9f && c.speed < 4, "push release coasts");
    for (int i=0;i<25;++i) c.Step(.02f, true, true);
    Require(c.speed == 0, "brake wins over push and stops");
    c.Step(10, true, false);
    Require(std::fabs(c.speed-.15f)<.001f, "long frame bounded");
    float before = c.speed;
    c.Step(NAN, true, false); c.Step(-1, true, false);
    Require(c.speed == before, "invalid time rejected");
    c.Disable(); c.Step(.02f, true, false);
    Require(!c.enabled && c.speed == 0, "toggle off resets velocity");
    c.enabled = true; c.Step(.02f, true, false);
    Require(std::fabs(c.speed-.06f)<.001f, "toggle on starts from rest");
    for(int i=0;i<500;++i) c.Step(.02f, false, false);
    Require(c.speed == 0, "coasting cannot reverse");
    RunSpeedLease lease;
    Require(!lease.Capture(NAN) && !lease.Capture(0) && !lease.Capture(31), "invalid original speed refused");
    Require(lease.Capture(7) && lease.Owns(7), "capture original speed");
    Require(!lease.Capture(8), "cannot recapture over an owned lease");
    Require(lease.Allows(0) && lease.Allows(4) && !lease.Allows(4.01f)
        && !lease.Allows(-1) && !lease.Allows(NAN), "safe actual speed bounds");
    lease.applied=3;
    Require(lease.Owns(3) && !lease.Owns(7) && !lease.Owns(NAN), "external updates not overwritten");
    Require(lease.original==7, "normal speed retained for restoration");
    lease.Clear();
    Require(lease.Capture(7,7) && lease.Allows(7) && !lease.Allows(7.01f),"explicit parity lease accepts7 but not above server/run cap");
    lease.Clear();
    Require(!lease.Capture(7,15) && !lease.Capture(7,NAN),"unsupported parity caps rejected");
    Require(!lease.held && !lease.Owns(0) && !lease.Allows(0), "clear releases ownership");
    Require(lease.Capture(7,14) && !lease.Allows(14),"higher local cap cannot bypass ordinary server allowance");
    lease.Clear();
    Require(lease.Capture(14,14) && lease.Allows(14) && !lease.Allows(14.01f),"acknowledged double-speed allowance remains bounded");
    lease.applied=8;
    Require(lease.Owns(8) && !lease.Owns(7) && lease.original==14,"newer server value is not owned by double-speed lease");
    lease.Clear();
    Require(lease.Capture(7,14,14) && lease.Allows(14) && lease.original==7 && lease.serverAllowance==14,
        "explicit server acknowledgement separates allowance14 from native restoration7");
    lease.applied=12;
    Require(lease.Owns(12) && !lease.Owns(10),"confirmed allowance retains compare-and-restore ownership");
    lease.Clear();
    Require(!lease.Capture(7,14,15) && !lease.Capture(7,14,NAN) && !lease.Capture(10,14,14)
        && !lease.Capture(7,4,14),"unconfirmed tiers and mismatched baseline/cap are rejected");
    Require(lease.Capture(2) && lease.Allows(2) && !lease.Allows(3), "never exceed captured server speed");
    c.Disable(); c.maximum=2; c.enabled=true;
    for (int i=0;i<50;++i) c.Step(.02f, true, false);
    Require(c.speed==2, "controller respects lowered server cap");
    c.Step(.02f, false, true);
    Require(c.speed>0 && c.speed<2, "brake decelerates rather than stopping immediately");
    const auto synchronized=skatemotion::Synchronize(skatemotion::Ground(2*.9144f,0,true,false,1,1,10,10,7),
        0,0,.9144f,0,0,0,7);
    Require(synchronized.valid && fabsf(synchronized.speed-2.5f)<.001f,"bounded native velocity correction catches position lag");
    const auto capped=skatemotion::Synchronize(skatemotion::Ground(7*.9144f,0,true,false,1,1,10,10,7),0,0,.9144f,0,0,0,7);
    Require(capped.valid && capped.speed==7,"position correction cannot exceed server cap");
    const auto still=skatemotion::Synchronize(skatemotion::Ground(0,0,true,false,1,1,10,10,7),0,0,.9144f,0,0,0,7);
    Require(still.valid && still.speed==0,"no stationary creep from position feedback");
    Require(!skatemotion::Synchronize(motion,0,0,4*.9144f,0,0,0,7).valid,"position disagreement still stops rather than teleporting");
    std::printf("PASS %u controller/ownership checks; live native movement still requires testing\n", checks);
}
