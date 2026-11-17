#include "SkateHelper.h"
#include "SkateInput.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <thread>

bool Await(skate::Helper& helper,bool failure,skatetransport::Pose& pose) {
    for(int n=0;n<300;++n) {
        bool failed=false;
        bool valid=helper.Snapshot(pose,failed);
        if(failure?failed:valid) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
template<class Done> bool AwaitCollision(skate::Helper& helper,skate::CollisionState& state,Done done) {
    for(int n=0;n<300;++n) {
        state=helper.Collision();
        if(done(state)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
skate::CollisionSnapshot World(bool init,float x) {
    skate::CollisionSnapshot world;
    world.init=init; world.map=1820; world.heading=.5f;
    const float origin[3]{-8800.f,640.f,95.f};
    for(int i=0;i<3;++i) world.origin[i]=world.centre[i]=origin[i];
    world.centre[0]+=x;
    world.tris={-8810.f,630.f,94.f, -8790.f,630.f,94.f, -8790.f,650.f,94.f};
    return world;
}
int wmain(int argc,wchar_t** argv) {
    assert(argc==2);
    skate::Helper helper;
    skatetransport::Pose pose;
    skate::CollisionState state;
    const auto relativeBinary = std::filesystem::relative(argv[1]).wstring();
    assert(!std::filesystem::path(relativeBinary).is_absolute());
    helper.Start(relativeBinary,L"valid");
    // No pose and no steps until the client's own world is installed (op 48).
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    bool failed; assert(!helper.Snapshot(pose,failed) && !failed && !helper.Collision().session);
    helper.Submit(World(true,0));
    assert(Await(helper,false,pose));
    assert(AwaitCollision(helper,state,[](auto& s){return s.session && !s.pending;}));
    assert(state.map==1820 && state.installed==1 && state.origin[0]==-8800.f && !state.retry);
    {
        skate::Helper viewer;
        viewer.Start(argv[1],L"valid"); viewer.Submit(World(true,0));
        skatetransport::Pose other;
        assert(Await(viewer,false,other) && viewer.LogPath()!=helper.LogPath());
        assert(helper.Snapshot(pose,failed) && !failed); // Two clients/helpers in one working folder need separate stderr handles.
        viewer.Stop();
    }
    {
        skate::VisualSnapshot viewer;
        assert(helper.Visual(viewer) && viewer.mesh && viewer.frame && viewer.state==100);
        const auto idle=viewer.frame->pose.tick;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(helper.Snapshot(pose,failed) && pose.tick==idle); // op9 made viewer assets available without stepping/J.
    }
    helper.Input(true,true,false,10000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(helper.Snapshot(pose,failed) && pose.position[0]>0 && !failed);
    // Regression (live test 2026-11-15): pad input through the real gate must move the board after op 48 even
    // with a resting right stick; raw drift used to reach op 18 as a crouch and silently cancel every push.
    {
        skate::InputGate gate; xboxinput::Pad pad{};
        pad.ry=-5000; pad.rx=3000;
        assert(gate.Step(true,true,true,pad).active);
        pad.buttons=0x1000;
        const auto controls=gate.Step(true,true,true,pad);
        assert(controls.active && controls.push && controls.pop==0);
        assert(helper.Snapshot(pose,failed)); const float before=pose.position[0];
        helper.Input(controls.active,controls.push,controls.brake,controls.steer,controls.pop);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        assert(helper.Snapshot(pose,failed) && pose.position[0]>before+.05f);
        pad.ry=-20000; assert(gate.Step(true,true,true,pad).pop==-20000); // a deliberate crouch still reaches the helper
    }
    helper.Input(false,false,false,0);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(helper.Snapshot(pose,failed)); const auto tick=pose.tick;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    assert(helper.Snapshot(pose,failed) && pose.tick==tick);
    // Ollie: op 7 loaded the preview after op 48, so steps are op 22 carrying the full right stick (pop + flip X).
    helper.Input(true,false,false,0,32767,-20000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(helper.Snapshot(pose,failed) && pose.airborne==1 && pose.ground==0);
    // Phase 4 handoff: the skater frame, mesh and source state are published with the pose of the same step.
    {
        skate::VisualSnapshot visual;
        assert(helper.Visual(visual) && visual.mesh && visual.mesh->colors.size()==1 && visual.state==100);
        assert(helper.Snapshot(pose,failed) && visual.frame->pose.airborne==1 && visual.frame->anchors[23].y>1.f);
        assert(visual.frame->vertices.size()==1);
    }
    helper.Input(true,false,false,0,0);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(helper.Snapshot(pose,failed) && pose.airborne==0 && pose.ground==1);
    helper.Input(false,false,false,0);
    // Refreshes keep map/origin and get generations 2, 3 (the fixture rejects anything not increasing).
    helper.Submit(World(false,10));
    assert(AwaitCollision(helper,state,[](auto& s){return s.installed==2;}));
    helper.Submit(World(false,20));
    assert(AwaitCollision(helper,state,[](auto& s){return s.installed==3;}) && !state.retry);
    // An invalid snapshot (init centre != origin) is refused locally and reported for regather.
    auto bad=World(true,0); bad.centre[0]+=1;
    helper.Submit(bad);
    assert(AwaitCollision(helper,state,[](auto& s){return s.retry;}) && state.session && state.installed==3);
    // Regression (Phase 3 live test): steer, then a resync re-init at the player; the pose and its origin switch
    // together, and steering + pushing keep moving the board afterwards (it used to re-init in a loop and freeze).
    {
        helper.Input(true,true,false,32767);
        // Regression (Phase 4 live test): the board must be capped at the character's run allowance (op 11), or it
        // pulls 20 yd ahead downhill and forces a multi-second re-init; the cap survives a re-init (op 48 clears it).
        helper.Cap(7*.9144f);
        for(int n=0;n<300 && !(helper.Snapshot(pose,failed) && fabsf(pose.velocity[0]-6.4008f)<1e-3f);++n) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(fabsf(pose.velocity[0]-6.4008f)<1e-3f);
        auto moved=World(true,0); for(float& v : moved.origin) v+=30; for(int i=0;i<3;++i) moved.centre[i]=moved.origin[i];
        for(size_t i=0;i<moved.tris.size();++i) moved.tris[i]+=30;
        helper.Submit(moved);
        assert(AwaitCollision(helper,state,[](auto& s){return s.session && !s.pending && s.origin[0]==-8770.f;}));
        float origin[3]; assert(helper.Snapshot(pose,failed,origin) && origin[0]==-8770.f && origin[1]==670.f);
        const auto tick=pose.tick; const float before=pose.position[0];
        helper.Input(true,true,false,-32767);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        assert(helper.Snapshot(pose,failed) && !failed && pose.tick>tick && pose.position[0]>before);
        assert(fabsf(pose.velocity[0]-6.4008f)<1e-3f); // re-sent after the re-init
        helper.Input(false,false,false,0);
    }
    helper.Stop(); assert(!helper.Snapshot(pose,failed) && !helper.Collision().session);
    for(const wchar_t* mode : {L"reject-refresh",L"fail-build"}) {
        helper.Start(argv[1],mode); helper.Submit(World(true,0));
        assert(AwaitCollision(helper,state,[](auto& s){return s.session && !s.pending;}));
        helper.Submit(World(false,10));
        assert(AwaitCollision(helper,state,[](auto& s){return s.retry;}) && state.installed==1);
        assert(helper.Snapshot(pose,failed) && !failed); // the old world stays usable
        helper.Stop();
    }
    // Without a preview the board still rolls on ops 5/6 (no ollie, no skater frame: stock animation).
    helper.Start(argv[1],L"no-preview"); helper.Submit(World(true,0));
    assert(Await(helper,false,pose));
    helper.Input(true,true,false,0,32767);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    assert(helper.Snapshot(pose,failed) && !failed && pose.position[0]>0 && pose.airborne==0);
    { skate::VisualSnapshot visual; assert(!helper.Visual(visual)); }
    helper.Stop();
    // A wipeout (source state 300) reaches the main thread with its frame; Stop clears it.
    helper.Start(argv[1],L"bail"); helper.Submit(World(true,0));
    assert(Await(helper,false,pose));
    helper.Input(true,true,false,0);
    {
        skate::VisualSnapshot visual; bool seen=false;
        for(int n=0;n<300 && !seen;++n) { seen=helper.Visual(visual) && visual.state==300; std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        assert(seen);
        helper.Stop(); assert(!helper.Visual(visual));
    }
    for(const wchar_t* mode : {L"stale",L"stale-id",L"oversized",L"truncated",L"bad-magic"}) {
        helper.Start(argv[1],mode); helper.Submit(World(true,0)); assert(Await(helper,true,pose)); helper.Stop();
    }
    for(const wchar_t* mode : {L"hang-ready",L"hang-request"}) {
        helper.Start(argv[1],mode); helper.Submit(World(true,0));
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto begin=std::chrono::steady_clock::now(); helper.Stop();
        assert(std::chrono::steady_clock::now()-begin<std::chrono::seconds(2));
    }
}
