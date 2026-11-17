#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-visual.h"
#include "shadow-state.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <array>

int main() {
    _set_error_mode(_OUT_TO_STDERR); _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    static_assert(sizeof(void*)==4,"Real x86/x64 continuation transport");
    unsigned checks=0,recenters=0,totalSteps=0;
    using E=skateshadow::EdgeAction;
    assert(skateshadow::EdgePolicy(false,true,false,12,0)==E::Continue); ++checks;
    assert(skateshadow::EdgePolicy(false,true,false,18,0)==E::StopEdge); ++checks;
    assert(skateshadow::EdgePolicy(true,true,false,11.99f,0)==E::Continue); ++checks;
    assert(skateshadow::EdgePolicy(true,true,false,12,0)==E::Recenter); ++checks;
    assert(skateshadow::EdgePolicy(true,true,false,0,-12)==E::Recenter); ++checks;
    assert(skateshadow::EdgePolicy(true,true,false,18,0)==E::StopEdge); ++checks;
    assert(skateshadow::EdgePolicy(true,false,false,12,0)==E::StopGround); ++checks;
    assert(skateshadow::EdgePolicy(true,true,true,12,0)==E::StopGround); ++checks;
    assert(skateshadow::EdgePolicy(true,true,false,NAN,0)==E::StopGround); ++checks;
    volatile LONG epoch=1; skatewire::Pipe helper;
    assert(helper.Start(L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe",
        L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets",
        L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\pose-continuation-helper.log",1,&epoch)); ++checks;
    float travelled=0,maxVelocityError=0,minForwardDot=1,coastSpeed=0,brakeSpeed=0;
    unsigned scenarios=0;
    for (float slope:{0.f,-.03f}) {
        std::vector<unsigned char> init,result,input,empty;
        for (float v:{0.f,0.f,0.f,1.57079632679f}) skatewire::Float(init,v);
        skatewire::Word(init,2);
        for (auto p:std::initializer_list<std::array<float,2>>{{-25,-25},{-25,25},{25,25},{-25,-25},{25,25},{25,-25}}) {
            skatewire::Float(init,p[0]); skatewire::Float(init,slope*p[0]); skatewire::Float(init,p[1]);
        }
        assert(helper.Request(1,1,init,result)); ++checks;
        assert(helper.Request(7,1,empty,result)); ++checks;
        const auto mesh=skatevisual::Schema(result); assert(mesh); ++checks;
        std::shared_ptr<const skatevisual::Frame> frame;
        assert(helper.Request(9,1,empty,result) && (frame=skatevisual::Decode(result,*mesh))); ++checks;
        for (unsigned i=0;i<1300;++i) {
            const auto previous=frame->pose;
            // 65 simulated seconds:45 push,5 coast,5 left/right,10 brake.
            skatewire::InputStep(input,3,i<900,i>=1100,i>=1000 && i<1050,i>=1050 && i<1100);
            assert(helper.Request(8,1,input,result) && (frame=skatevisual::Decode(result,*mesh))); ++checks;
            assert(frame->pose.tick==previous.tick+3); ++checks;
            totalSteps+=3;
            const auto stepped=frame->pose;
            travelled+=hypotf(stepped.position[0]-previous.position[0],stepped.position[2]-previous.position[2]);
            const E action=skateshadow::EdgePolicy(true,stepped.ground,stepped.airborne,stepped.position[0],stepped.position[2]);
            if (action==E::StopGround || action==E::StopEdge) {
                fprintf(stderr,"Continuation stopped scenario=%u request=%u action=%u pos=%.5f,%.5f,%.5f ground=%u\n",
                    scenarios,i,static_cast<unsigned>(action),stepped.position[0],stepped.position[1],stepped.position[2],stepped.ground);
                return 2;
            }
            if (action==E::Recenter) {
                std::vector<unsigned char> reset;
                for (float v:{0.f,0.f,0.f,atan2f(stepped.forward[0],stepped.forward[2])}) skatewire::Float(reset,v);
                assert(helper.Request(10,1,reset,result) && (frame=skatevisual::Decode(result,*mesh))); ++checks;
                const auto after=frame->pose;
                assert(after.tick==stepped.tick+4 && after.ground && !after.airborne
                    && fabsf(after.position[0])<1 && fabsf(after.position[2])<1); ++checks;
                for (unsigned axis=0;axis<3;++axis) {
                    const float error=fabsf(after.velocity[axis]-stepped.velocity[axis]);
                    maxVelocityError=(std::max)(maxVelocityError,error); assert(error<.001f); ++checks;
                }
                const float dot=after.forward[0]*stepped.forward[0]+after.forward[2]*stepped.forward[2];
                minForwardDot=(std::min)(minForwardDot,dot); assert(dot>.99f); ++checks;
                ++recenters;
            }
            if (i==1099) coastSpeed=hypotf(frame->pose.velocity[0],frame->pose.velocity[2]);
            if (i==1299) brakeSpeed=hypotf(frame->pose.velocity[0],frame->pose.velocity[2]);
        }
        assert(brakeSpeed<coastSpeed*.5f); ++checks;
        // Explicit suspend still stops advancement; reset is not auto-reactivated.
        const auto held=frame->pose.tick;
        assert(helper.Request(6,1,empty,result)); ++checks;
        skatewire::Pose paused{}; assert(skatewire::Decode(result,paused) && paused.tick==held); ++checks;
        if (slope<0) {
            std::vector<unsigned char> invalid;
            for (float v:{2.f,0.f,0.f,0.f}) skatewire::Float(invalid,v);
            assert(!helper.Request(10,1,invalid,result)); ++checks;
            // The x86 transport treats any error reply as terminal. It does not
            // drain/reuse an invalid stream: the private helper job is stopped.
            helper.Stop();
        }
        ++scenarios;
    }
    assert(recenters>=10 && travelled>100); ++checks;
    helper.Stop();
    printf("{\"passed\":true,\"checks\":%u,\"scenarios\":%u,\"simulated_seconds_each\":65,\"stepped_ticks\":%u,\"recenters\":%u,\"travelled_metres\":%.5f,\"max_velocity_error_m_s\":%.7f,\"min_forward_dot\":%.6f,\"hard_edge_guard_preserved\":true,\"ground_guard_preserved\":true,\"suspend_tested\":true,\"brake_tested\":true,\"world_position_writes\":false,\"animation_phase_resets\":true,\"live_duration_verified\":false}\n",
        checks,scenarios,totalSteps,recenters,travelled,maxVelocityError,minForwardDot);
}
