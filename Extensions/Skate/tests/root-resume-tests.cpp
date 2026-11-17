#include "skate-vertical.h"
#include "air-root-motion.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks=0;
static void Check(bool ok){++checks;if(!ok){fprintf(stderr,"FAIL root resume %u\n",checks);exit(1);}}
int main(){
    // Exact failure tick/age/new-height fields from three logged interruptions.
    // Prior sample height/native XYZ are bounded fixture values, not full replay.
    struct Recorded{unsigned epoch,previous,tick,age,incomingAge,ms;float height,vy;};
    const Recorded events[]={
        {16,1313,1321,156,16,11,484.260437f,-.457765f},
        {25,1809,1818,156,15,2,483.940887f,1.264653f},
        {29,2475,2483,157,32,11,483.941040f,1.481329f}};
    for(const auto& e:events){
        skatevertical::Motor motor;
        Check(motor.RootSample(e.epoch,1000,e.previous,1.f/60,e.height-.1f,1,false,0,1000));
        const auto launch=motor.launchVelocity;
        const unsigned now=1000+e.age;
        float down=123;Check(!motor.Delta(now,e.epoch,0,e.ms,e.height-.2f,down));
        Check(motor.Waiting(now,e.epoch));
        Check(motor.Hold(now,e.epoch,0,e.height-.2f,down) && down==0);
        Check(motor.tick==e.previous && motor.at==1000 && motor.segment.span==0);
        Check(!motor.Hold(now,e.epoch+1,0,e.height,down));
        Check(!motor.Hold(now,e.epoch,0,NAN,down));
        Check(!motor.RootSample(e.epoch,now,e.previous,1.f/60,e.height,e.vy,false,e.ms,now));
        Check(!motor.RootSample(e.epoch+1,now,e.tick,1.f/60,e.height,e.vy,false,e.ms,now));
        Check(!motor.RootSample(e.epoch,now,e.previous+10,1.f/60,e.height,e.vy,false,e.ms,now));
        Check(!motor.RootSample(e.epoch,now,e.tick,1.f/60,e.height,e.vy,false,e.ms,now-150));
        Check(motor.RootSample(e.epoch,now,e.tick,1.f/60,e.height,e.vy,false,e.ms,now-e.incomingAge));
        Check(motor.launchVelocity==launch && motor.tick==e.tick && !motor.Waiting(now,e.epoch));
        Check(motor.Delta(now,e.epoch,e.ms,e.ms+11,e.height-.1f,down) && std::isfinite(down) && fabsf(down)<.1f);
        Check(!motor.Hold(now,e.epoch,e.ms,e.height,down));
        Check(!motor.Delta(now+150,e.epoch,e.ms,e.ms+11,e.height,down));
    }
    skatevertical::Motor ballistic;
    // Wall impact: genuine Source bail7905, fresh root7910, then native clock
    // asks9->166ms. Hold that oversized sweep, keep pose, accept clock167.
    skatevertical::Motor wall;
    Check(wall.RootSample(4,1000,7902,1.f/60,503.1f,4,false,0,1000));
    Check(wall.RootSample(4,1157,7910,1.f/60,503.027588f,-.919651f,false,9,1157));
    float wallDown=123;
    Check(!wall.Delta(1172,4,9,166,503.128235f,wallDown));
    Check(wall.HoldOversized(1172,4,9,166,503.128235f,wallDown) && wallDown==0 && wall.stepHeld);
    Check(wall.RootSample(4,1188,7910,1.f/60,503.027588f,-.919651f,false,167,1157));
    Check(wall.at==1157 && wall.tick==7910 && wall.launchVelocity==4 && wall.arcSeconds==0);
    Check(wall.Delta(1188,4,167,178,503.128235f,wallDown) && wallDown>0 && wallDown<.1f && !wall.stepHeld);
    Check(!wall.HoldOversized(1188,5,178,335,503,wallDown));
    Check(!wall.HoldOversized(1310,4,178,335,503,wallDown));
    Check(!wall.HoldOversized(1188,4,178,177,503,wallDown));
    Check(!wall.HoldOversized(1188,4,178,335,NAN,wallDown));
    Check(ballistic.Sample(1,1000,1,1.f/60,4,false,0));float down=0;
    Check(!ballistic.Waiting(1200,1) && !ballistic.Hold(1200,1,0,10,down));
    Check(!ballistic.Sample(1,1200,2,1.f/60,4,false,20));
    Check(!ballistic.HoldOversized(1000,1,0,157,10,down));
    printf("{\"passed\":true,\"checks\":%u,\"recorded_interruptions\":3,\"recorded_wall_wipeout_clock_catchup\":true,\"maximum_sweep_ms\":100,\"stale_motion_held\":true,\"distinct_fresh_tick_required\":true,\"new_impulse\":false,\"live_verified\":false}\n",checks);
}
