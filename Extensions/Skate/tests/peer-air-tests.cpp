#include "SkatePeerPose.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

using Point=skatepeerpose::Point;
static float AirGap(Point a,Point b) { return characterretarget::Length(characterretarget::Sub(a,b)); }
static std::vector<unsigned char> File(const std::string& path) {
    std::ifstream f(path,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}};
}
int main() {
    skatepeer::Packet board; board.epoch=1; board.map=1820; board.mesh=42;
    board.state.flags=skatepeer::Active; board.actorKnown=true;
    board.actor[0]=-9480; board.actor[1]=70; board.actor[2]=56;
    auto text=skatepeer::Encode(board); skatepeer::Packet decoded;
    assert(text.size()==95 && text.size()+6<255 && skatepeer::Decode(text.data(),text.size(),decoded));
    assert(decoded.actorKnown && decoded.actor[0]==-9480 && decoded.actor[2]==56);
    assert(skatepeer::NearActor(decoded,{-9480,70,56}) && skatepeer::NearActor(decoded,{-9496,70,56}));
    assert(!skatepeer::NearActor(decoded,{-9496.1f,70,56}));
    auto bad=text; bad[70]=';'; assert(!skatepeer::Decode(bad.data(),bad.size(),decoded));
    bad=text; bad.replace(71,8,"7F800000"); assert(!skatepeer::Decode(bad.data(),bad.size(),decoded));
    bad=text; bad.replace(71,8,"7FC00000"); assert(!skatepeer::Decode(bad.data(),bad.size(),decoded));
    board.actor[0]=1e6f+1; assert(skatepeer::Encode(board).empty()); board.actor[0]=-9480;
    assert(!skatepeer::Decode(text.data(),text.size()-1,decoded));
    auto legacy=board; legacy.actorKnown=false; text=skatepeer::Encode(legacy);
    assert(text.size()==70 && skatepeer::Decode(text.data(),text.size(),decoded) && !decoded.actorKnown);

    skatepeerpose::Packet pose; pose.epoch=1; pose.map=board.map; pose.mesh=board.mesh; pose.rig=3;
    skatepeerpose::Presentation p;
    const Point ground{-9480,70,56};
    p.Push(board,pose,1000,ground,true);
    const Point moving{-9479,70,56};
    assert(AirGap(p.Actor(1050,moving),moving)==0); // Accepted ground prediction remains authoritative.
    board.state.flags|=skatepeer::Airborne; board.actor[0]=-9479; board.actor[2]=58;
    const Point takeoff{-9479,70,56.5f};
    p.Push(board,pose,1100,takeoff,true);
    assert(AirGap(p.Actor(1100,takeoff),takeoff)==0); // No jump when handing off from the native anchor.
    const Point halfway=p.Actor(1150,{-9478,70,57});
    assert(fabsf(halfway.z-57.25f)<1e-5f);
    assert(AirGap(p.Actor(1200,{-9478,70,56}),p.Actor(1200,{-9478,70,59}))==0);
    assert(fabsf(p.Actor(1200,takeoff).z-58)<1e-5f); // A stock-arc correction cannot drag the presentation.
    const Point before=p.Actor(1250,takeoff);
    board.actor[0]=-9478.5f; board.actor[2]=58.5f;
    p.Push(board,pose,1250,takeoff,true);
    assert(AirGap(p.Actor(1250,takeoff),before)<1e-5f); // Packet jitter starts from the displayed anchor.
    board.state.flags=skatepeer::Active; board.actor[2]=56;
    const Point landing=p.Actor(1300,takeoff);
    p.Push(board,pose,1300,ground,true);
    assert(AirGap(p.Actor(1300,ground),landing)<1e-5f);
    assert(AirGap(p.Actor(1400,ground),ground)<1e-5f); // Bounded landing handoff to native movement.
    ++board.epoch; ++pose.epoch; p.Push(board,pose,1450,ground,true);
    assert(!p.airAnchor && AirGap(p.Actor(1450,ground),ground)==0);
    p.Clear(); assert(AirGap(p.Actor(1500,moving),moving)==0);
    // The board and body consume the same visual anchor; native actor/model data are not rewritten.
    skaterig::Rig rig; rig.skeleton.count=1; rig.skeleton.bones[0].parent=-1;
    float model[16]; characterretarget::Matrix({},ground,model);
    const Point wanted{-9480,70,59}; characterretarget::Pose restored; Point offset;
    assert(skatepeerpose::Restore(pose,rig,model,wanted,restored,offset));
    assert(fabsf(model[14]+offset.z-wanted.z)<1e-5f && model[14]==ground.z);
    skateboard::M3 basis; const double id[]{1,0,0,0,1,0,0,0,1}; std::copy(id,id+9,basis.a);
    float deck[16]; assert(skatepeer::Matrix(board,basis,{wanted.x,wanted.y,wanted.z},deck));
    assert(fabsf(deck[14]-(model[14]+offset.z))<1e-5f);

    {
        skatepeerpose::Presentation delayedLanding;
        auto landingBoard=board; auto landingPose=pose;
        landingBoard.actorKnown=true; landingBoard.actor[0]=0; landingBoard.actor[1]=0; landingBoard.actor[2]=1;
        landingBoard.state.flags=skatepeer::Active|skatepeer::Airborne;
        delayedLanding.Push(landingBoard,landingPose,1000,{0,0,1},false);
        landingBoard.state.flags=skatepeer::Active; landingBoard.actor[2]=0;
        delayedLanding.Push(landingBoard,landingPose,1100,{0,0,1.5f},false);
        // Source has landed, but stock observer prediction remains in flight. Do not render another hop.
        assert(fabsf(delayedLanding.Actor(1200,{0,0,1.5f}).z)<1e-5f);
        assert(delayedLanding.waitingNativeGround);
        delayedLanding.Push(landingBoard,landingPose,1250,{0,0,1.25f},false);
        assert(fabsf(delayedLanding.Actor(1350,{0,0,.75f}).z)<1e-5f);
        // Start the handoff only after native landing, without a late snap or a renewed airborne excursion.
        delayedLanding.Push(landingBoard,landingPose,1400,{0,0,.15f},true);
        assert(!delayedLanding.waitingNativeGround && fabsf(delayedLanding.Actor(1400,{0,0,.15f}).z)<1e-5f);
        assert(fabsf(delayedLanding.Actor(1450,{0,0,.15f}).z-.075f)<1e-5f);
        assert(fabsf(delayedLanding.Actor(1500,{0,0,.15f}).z-.15f)<1e-5f);
        delayedLanding.Push(landingBoard,landingPose,1510,{0,0,.15f},true);
        assert(!delayedLanding.airAnchor);
        delayedLanding.Clear();
        assert(!delayedLanding.waitingNativeGround && !delayedLanding.airAnchor);
    }
    if(const char* capture=getenv("SKATE_OLLIE_CAPTURE")) {
        const auto schema=skatevisual::Schema(File(std::string(capture)+"/mesh.bin")); assert(schema);
        const auto bytes=File(std::string(capture)+"/frames.bin");
        std::vector<Point> actors; std::vector<bool> airborne; size_t at=0;
        while(at<bytes.size()) {
            assert(bytes.size()-at>=4); const size_t size=skatewire::Get(bytes.data()+at); at+=4;
            assert(size<=bytes.size()-at && bytes.size()-at-size>=4);
            std::vector<unsigned char> frame(bytes.begin()+at,bytes.begin()+at+size); at+=size+4;
            const auto f=skatevisual::Decode(frame,*schema,true,nullptr,true); assert(f);
            actors.push_back({12345+f->pose.position[0]/.9144f,-23456-f->pose.position[2]/.9144f,1000+f->pose.position[1]/.9144f});
            airborne.push_back(f->pose.airborne!=0);
        }
        assert(actors.size()==604); unsigned airFrames=0; float worstNative=0,worstVisual=0;
        skatepeerpose::Presentation replay; Point priorNative{},priorVisual{}; bool previousAir=false;
        board.epoch=pose.epoch=1; board.actorKnown=true;
        for(unsigned i=0;i<actors.size();++i) {
            const uint32_t now=i*1000/60;
            Point native=actors[i];
            // Controlled observer corrections exercise the captured trajectory independently of stock jump prediction.
            if(airborne[i]) native.z+=(i%12==0?.9f:0.f);
            if(i%6==0) {
                board.state.flags=skatepeer::Active|(airborne[i]?skatepeer::Airborne:0);
                board.actor[0]=actors[i].x; board.actor[1]=actors[i].y; board.actor[2]=actors[i].z;
                board.state.sequence=pose.sequence=uint16_t(i);
                replay.Push(board,pose,now,native,true);
            }
            const auto visual=replay.Actor(now,native);
            if(airborne[i] && previousAir && i%6!=0) {
                worstNative=(std::max)(worstNative,AirGap(native,priorNative));
                worstVisual=(std::max)(worstVisual,AirGap(visual,priorVisual)); ++airFrames;
            }
            priorNative=native; priorVisual=visual; previousAir=airborne[i];
        }
        printf("captured airborne frames=%u injected-correction max step native=%.6f paired-visual=%.6f yd\n",airFrames,worstNative,worstVisual);
        assert(airFrames>60 && worstNative>.8f && worstVisual<worstNative*.6f);
    }
}
