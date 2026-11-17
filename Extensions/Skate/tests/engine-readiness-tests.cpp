#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "skate-pipe.h"
#include "shadow-state.h"
#include <cstdio>
#include <cassert>

static const wchar_t* kHelper=L"D:\\world of skatecraft 3.3.5\\build\\debug\\wrath-skate-bridge.exe";
static const wchar_t* kAssets=L"D:\\world of skatecraft 3.3.5\\skatecraft-wrath\\skate-data\\assets";
static const wchar_t* kFixture=L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\engine-readiness-fixture.exe";
static const wchar_t* kLog=L"D:\\world of skatecraft 3.3.5\\native-probe\\build\\engine-readiness-helper.log";
int main() {
    static_assert(sizeof(void*)==4,"The transport caller is x86.");
    unsigned checks=0;
    skatewire::Pipe helper;std::vector<unsigned char> init,result,step;
    assert(helper.Start(kHelper,kAssets,kLog,1,nullptr));++checks;
    const DWORD child=helper.ProcessId();assert(child && helper.Running());++checks;
    for(float value:{0.f,0.f,0.f,0.f})skatewire::Float(init,value);
    skatewire::Word(init,2);
    for(float value:{-100.f,0.f,-100.f,-100.f,0.f,100.f,100.f,0.f,100.f,
        -100.f,0.f,-100.f,100.f,0.f,100.f,100.f,0.f,-100.f})skatewire::Float(init,value);
    DWORD timings[3]{};uint64_t initialTick=0,previousTick=0;skatewire::Pose initialPose{};
    for(uint32_t epoch=1;epoch<=3;++epoch){
        const float heading=0;
        memcpy(init.data()+12,&heading,4);
        const DWORD began=GetTickCount();
        assert(helper.Request(30,epoch,init,result));++checks;
        timings[epoch-1]=GetTickCount()-began;
        skatewire::Pose pose{};assert(skatewire::Decode(result,pose));++checks;
        if(epoch==1){initialTick=pose.tick;initialPose=pose;}
        fprintf(stderr,"epoch=%u tick=%llu init_ms=%lu position=%.6f,%.6f,%.6f velocity=%.6f,%.6f,%.6f\n",
            epoch,pose.tick,timings[epoch-1],pose.position[0],pose.position[1],pose.position[2],pose.velocity[0],pose.velocity[1],pose.velocity[2]);
        assert((epoch==1 || (pose.tick>previousTick && pose.tick<=previousTick+5)) && helper.ProcessId()==child && helper.Running());++checks;
        // Fresh construction can have legitimate first-tick settling motion.
        // Compare it with the cold baseline, not an invented zero-speed rule.
        for(unsigned axis=0;axis<3;++axis){
            assert(fabsf(pose.position[axis]-initialPose.position[axis])<.01f
                && fabsf(pose.velocity[axis]-initialPose.velocity[axis])<.01f
                && fabsf(pose.forward[axis]-initialPose.forward[axis])<.01f);++checks;
        }
        assert(skateshadow::Fresh(epoch,epoch,200,100));++checks;
        assert(!skateshadow::Fresh(epoch+1,epoch,200,100));++checks;
        if(epoch>1){
            skatewire::InputStep(step,1,false,false,false,false);
            assert(!helper.Request(5,epoch-1,step,result));++checks;
            assert(strstr(helper.Rejection(),"stale epoch") && helper.Running());++checks;
        }
        skatewire::InputStep(step,3,true,false,false,false);
        for(unsigned i=0;i<20;++i)assert(helper.Request(5,epoch,step,result));++checks;
        assert(skatewire::Decode(result,pose) && pose.tick>initialTick);++checks;
        previousTick=pose.tick;
    }
    // Reuse after an actual simulated hard landing must not inherit wipeout
    // ownership or momentum. This is isolated Source physics, not a WoW probe.
    const float high=20.f;memcpy(init.data()+4,&high,4);
    assert(helper.Request(30,4,init,result));++checks;
    bool bailed=false;
    skatewire::InputStep(step,3,false,false,false,false);
    for(unsigned i=0;i<140 && !bailed;++i){
        assert(helper.Request(5,4,step,result));++checks;
        assert(helper.Request(24,4,{},result) && result.size()>=12);++checks;
        bailed=skatewire::Get(result.data()+8)==300;
    }
    assert(bailed);++checks;
    assert(helper.Request(32,4,{},result) && result.size()==20 && skatewire::Get(result.data()+16)==1);++checks;
    const float floor=0;memcpy(init.data()+4,&floor,4);
    assert(helper.Request(30,5,init,result));++checks;
    skatewire::Pose recovered{};assert(skatewire::Decode(result,recovered) && recovered.ground && !recovered.airborne);++checks;
    assert(helper.Request(32,5,{},result) && result.size()==20 && skatewire::Get(result.data()+8)==0
        && skatewire::Get(result.data()+12)==0 && skatewire::Get(result.data()+16)==0);++checks;
    const float lower=-2.f;memcpy(init.data()+4,&lower,4);
    for(unsigned vertex=0;vertex<6;++vertex)memcpy(init.data()+24+vertex*12,&lower,4);
    assert(helper.Request(30,6,init,result));++checks;
    skatewire::Pose relocated{};
    assert(skatewire::Decode(result,relocated) && relocated.ground && !relocated.airborne
        && fabsf(relocated.position[1]-initialPose.position[1]-lower)<.02f);++checks;
    HANDLE owned=OpenProcess(SYNCHRONIZE,FALSE,child);assert(owned);++checks;
    helper.Stop();assert(WaitForSingleObject(owned,2000)==WAIT_OBJECT_0);++checks;CloseHandle(owned);
    for(unsigned mode=0;mode<3;++mode){
        volatile LONG epoch=1;
        const wchar_t* behavior=mode==1?L"wrong-epoch":L"timeout";
        assert(helper.Start(kFixture,behavior,kLog,1,mode==2?&epoch:nullptr,true));++checks;
        owned=OpenProcess(SYNCHRONIZE,FALSE,helper.ProcessId());assert(owned);++checks;
        if(mode==2){
            InterlockedIncrement(&epoch);
            assert(WaitForSingleObject(owned,2000)==WAIT_OBJECT_0);++checks;
        }else{
            const DWORD began=GetTickCount();
            assert(!helper.Request(2,1,{},result) && !helper.Running());++checks;
            if(mode==0)assert(GetTickCount()-began>=1900 && GetTickCount()-began<6000);
            ++checks;
        }
        helper.Stop();assert(WaitForSingleObject(owned,2000)==WAIT_OBJECT_0);++checks;CloseHandle(owned);
    }
    printf("{\"passed\":true,\"checks\":%u,\"cold_init_ms\":%lu,\"warm_init_ms\":%lu,\"second_warm_init_ms\":%lu,\"process_reused\":true,\"session_reactivated\":true,\"fresh_collision_each_epoch\":true,\"changed_geometry_reactivation_tested\":true,\"prior_input_released\":true,\"ragdoll_reactivation_tested\":true,\"stale_epoch_rejected\":true,\"timeout_stops_helper\":true,\"job_cleanup_passed\":true,\"scope\":\"isolated x86-to-x64 wire; synthetic flat collision; no WoW access\"}\n",checks,timings[0],timings[1],timings[2]);
}
