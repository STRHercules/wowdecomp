#include "SkateHelper.h"
#include "SkatePeers.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>
#include <mmsystem.h>
#pragma comment(lib,"winmm.lib")

int wmain(int argc,wchar_t** argv) {
    assert(argc==2);
    timeBeginPeriod(1); // Match the game's timer resolution, including the transport's existing Sleep(1) polling.
    const char* assets=getenv("SKATE_TIMING_ASSETS");
    const char* binary=getenv("SKATE_TIMING_HELPER");
    skate::Helper helper;
    helper.Start(binary?std::filesystem::path(binary).wstring():argv[1],assets?std::filesystem::path(assets).wstring():L"delayed");
    skate::CollisionSnapshot world;
    world.init=true; world.map=1820;
    world.tris={-80,80,0,-80,-80,0,80,-80,0, -80,80,0,80,-80,0,80,80,0};
    helper.Submit(std::move(world));
    skatetransport::Pose pose; bool failed=false,ready=false;
    for(unsigned n=0;n<3000 && !ready;++n) {
        skate::VisualSnapshot visual;
        ready=helper.Snapshot(pose,failed) && helper.Collision().session;
        assert(!failed); if(!ready) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(ready);
    if (assets) {
        skate::VisualSnapshot viewer; assert(helper.Visual(viewer));
        skateboard::Rest rest; skateboard::M3 basis; skateboard::Model files;
        assert(rest.Make(viewer.frame->vertices) && skatepeer::Basis(*viewer.mesh,rest,basis));
        assert(skateboard::Build(*viewer.mesh,rest,1,files) && !files.m2.empty());
        printf("idle viewer bootstrap: %zu vertices, %zu textures, mesh checksum %u\n",rest.points.size(),files.blp.size(),skatepeer::MeshId(*viewer.mesh));
    }
    helper.Input(true,true,false,0); helper.Cap(6.4008f);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    assert(helper.Snapshot(pose,failed) && !failed);
    const uint64_t first=pose.tick;
    const auto start=std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    assert(helper.Snapshot(pose,failed) && !failed);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    const double rate=(pose.tick-first)/seconds;
    printf("%s worker cadence: ticks=%llu seconds=%.6f rate=%.3f Hz\n",assets?"real":"delayed fixture",
        static_cast<unsigned long long>(pose.tick-first),seconds,rate);
    helper.Input(false,false,false,0);
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(helper.Snapshot(pose,failed)); const auto suspended=pose.tick;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    assert(helper.Snapshot(pose,failed) && pose.tick==suspended);
    helper.Input(true,true,false,0);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    assert(helper.Snapshot(pose,failed) && pose.tick>suspended && pose.tick-suspended<12);
    const auto stop=std::chrono::steady_clock::now(); helper.Stop();
    assert(std::chrono::steady_clock::now()-stop<std::chrono::seconds(2));
    assert(rate>54 && rate<66);
    timeEndPeriod(1);
}
