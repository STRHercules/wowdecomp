// Private real-helper captures stay outside Git. SKATE_OLLIE_CAPTURE holds mesh.bin and
// frames.bin (u32 length, op22 frame, u32 state), produced by the local record_ollie.py.
#include "SkateRig.h"
#include "SkateDrive.h"
#include "SkatePeerPose.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>
#include <array>

static std::vector<unsigned char> File(const std::string& path) {
    std::ifstream f(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(f),{}};
}
static std::vector<std::array<float,16>> Palette(const std::vector<skaterig::SourceBone>& bones,const skaterig::Pose& pose) {
    std::vector<std::array<float,16>> matrices(bones.size());
    for(uint32_t b=0;b<bones.size();++b) {
        float m[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; skaterig::Apply(pose,b,m);
        const auto p=bones[b].pivot;
        for(unsigned c=0;c<3;++c) m[12+c]-=p.x*m[c]+p.y*m[4+c]+p.z*m[8+c];
        if(bones[b].parent<0) memcpy(matrices[b].data(),m,sizeof(m));
        else for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c) {
            float v=0; for(unsigned k=0;k<4;++k) v+=m[r*4+k]*matrices[bones[b].parent][k*4+c]; matrices[b][r*4+c]=v;
        }
    }
    return matrices;
}
static skaterig::Point Transform(skaterig::Point p,const float* m) {
    return {p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12],p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13],p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14]};
}
static void Model(float yaw,float tilt,skaterig::Point translation,float* m) {
    const auto q=characterretarget::Multiply({0,0,sinf(yaw*.5f),cosf(yaw*.5f)},{sinf(tilt*.5f),0,0,cosf(tilt*.5f)});
    characterretarget::Matrix(q,translation,m);
}
int main() {
    const char* directory=getenv("SKATE_OLLIE_CAPTURE");
    if (!directory) { puts("rig-real-tests skipped (SKATE_OLLIE_CAPTURE not set)"); return 0; }
    std::string path=__FILE__; path=path.substr(0,path.find_last_of("/\\"))+"/fixtures/retail-rig.txt";
    std::ifstream input(path); std::string line; std::getline(input,line); std::getline(input,line);
    std::vector<skaterig::SourceBone> bones; std::vector<uint16_t> keys(291,0xffff);
    while (std::getline(input,line)) {
        std::istringstream row(line); unsigned index; skaterig::SourceBone b; std::string flags;
        assert(row>>index>>b.key>>b.parent>>flags>>b.pivot.x>>b.pivot.y>>b.pivot.z);
        assert(index==bones.size()); b.flags=uint32_t(std::stoul(flags,nullptr,16)); bones.push_back(b);
        if (b.key>=0 && b.key<int32_t(keys.size())) keys[b.key]=uint16_t(index);
    }
    assert(bones.size()==216);
    const char* skinPath=getenv("SKATE_RETAIL_SKIN");
    std::vector<unsigned char> skin;
    uint32_t vertexCount=0;
    if (skinPath) {
        skin=File(skinPath); assert(skin.size()>=872 && skatewire::Get(skin.data())==bones.size());
        vertexCount=skatewire::Get(skin.data()+4); assert(skin.size()==872+size_t(vertexCount)*20);
        for (unsigned b=0;b<bones.size();++b) bones[b].rotationSequences=skatewire::Get(skin.data()+8+b*4);
    }
    static skaterig::Rig rig;
    assert(!skaterig::Plan(bones.data(),uint32_t(bones.size()),keys.data(),uint32_t(keys.size()),rig));
    skaterig::Pose output; assert(skaterig::Bind(bones.data(),uint32_t(bones.size()),rig,output));
    if (skinPath) {
        assert(rig.model[rig.plan[20]]==26 && rig.model[rig.plan[22]]==25);
        // The 18/19 passive wrappers deform the lower shin and must inherit the knee rather than foot rotation.
        for (auto pair:{std::pair<unsigned,unsigned>{18,12},{19,13}})
            assert(output.entries[output.slot[pair.first]].source==output.entries[output.slot[pair.second]].source);
        // Apply a known foot flex to the ACTUAL weighted shin vertices; the old wrapper mapping incorrectly
        // rotated them around the ankle. Reference is the unchanged shin palette, not another joint-point test.
        unsigned affected=0; float worstOld=0,worstFixed=0;
        for (uint32_t i=0;i<vertexCount;++i) {
            const auto* v=skin.data()+872+i*20;
            float xyz[3]; memcpy(xyz,v,12);
            skaterig::Point before{},after{},reference{};
            unsigned total=0;
            for (unsigned k=0;k<4;++k) {
                const unsigned weight=v[12+k],bone=v[16+k]; if (!weight) continue;
                if (bone!=18 && bone!=19) continue;
                const auto pivot=bones[bone].pivot;
                const skaterig::Point p{xyz[0],xyz[1],xyz[2]},d=characterretarget::Sub(p,pivot);
                const auto wrong=characterretarget::Add(pivot,{d.z,d.y,-d.x}); // 90-degree ankle flex
                const auto scale=float(weight)/255;
                before=characterretarget::Add(before,characterretarget::Scale(wrong,scale));
                const auto& entry=output.entries[output.slot[bone]];
                const bool foot=entry.source==static_cast<int32_t>(rig.plan[20]) || entry.source==static_cast<int32_t>(rig.plan[22]);
                const auto actual=foot?wrong:p; // Apply the controlled foot palette through the actual bound bone source.
                after=characterretarget::Add(after,characterretarget::Scale(actual,scale));
                reference=characterretarget::Add(reference,characterretarget::Scale(p,scale));
                total+=weight;
            }
            if (total) { ++affected; worstOld=(std::max)(worstOld,skaterig::detail::Dist(before,reference));
                worstFixed=(std::max)(worstFixed,skaterig::detail::Dist(after,reference)); }
        }
        printf("actual skin vertices=%u affected=%u old shin flex displacement=%.6f fixed=%.6f yd\n",vertexCount,affected,worstOld,worstFixed);
        assert(affected>1000 && worstOld>.1f && worstFixed<.000001f);
    }
    // The actual parallel shin joints must receive the same solved rotation as their anatomical twins.
    for (auto pair : {std::pair<unsigned,unsigned>{11,12},{14,13},{33,35},{37,36}}) {
        assert(output.slot[pair.first]>=0 && output.slot[pair.second]>=0);
        assert(output.entries[output.slot[pair.first]].source==output.entries[output.slot[pair.second]].source);
    }
    assert(output.slot[44]<0 && output.slot[56]<0);
    const auto mesh=skatevisual::Schema(File(std::string(directory)+"/mesh.bin")); assert(mesh);
    const auto bytes=File(std::string(directory)+"/frames.bin"); assert(!bytes.empty());
    skaterig::Rider rider;
    static characterretarget::Pose target;
    unsigned count=0,planted=0,legs=0,air=0,refused=0;
    float soleError=0,legError=0,paletteError=0;
    float peerJointError=0,peerSkinError=0,peerFootDeckError=0;
    float staleMatrixError=0;
    std::vector<skatepeer::Packet> timelineBoard;
    std::vector<skatepeerpose::Packet> timelinePose;
    unsigned peerFrames=0;
    skateboard::Rest rest; skateboard::M3 basis;
    skaterig::Pose remoteOutput; assert(skaterig::Bind(bones.data(),uint32_t(bones.size()),rig,remoteOutput));
    static characterretarget::Pose remoteTarget;
    for (size_t at=0;at<bytes.size();) {
        assert(bytes.size()-at>=4);
        const uint32_t length=skatewire::Get(bytes.data()+at); at+=4;
        assert(length<=2*1024*1024 && bytes.size()-at>=size_t(length)+4);
        std::vector<unsigned char> payload(bytes.begin()+at,bytes.begin()+at+length); at+=length;
        const uint32_t state=skatewire::Get(bytes.data()+at); at+=4;
        const auto frame=skatevisual::Decode(payload,*mesh,true,nullptr,true); assert(frame);
        ++count;
        if (frame->pose.airborne) ++air;
        skaterig::Point offset; skaterig::Contact contact;
        const bool solved=skaterig::Solve(rider,rig.skeleton,mesh.get(),*frame,state,0,{},
            {frame->pose.position[0]/.9144f,-frame->pose.position[2]/.9144f,frame->pose.position[1]/.9144f},
            count*17,1,target,offset,nullptr,&contact);
        if (!solved) { ++refused; continue; }
        assert(skaterig::Fill(bones.data(),rig,target,offset,output));
        if (contact.planted) { ++planted; soleError=(std::max)(soleError,contact.soleError); }
        if (contact.legs) { ++legs; legError=(std::max)(legError,contact.legError); }
        if(rest.points.empty()) { assert(rest.Make(frame->vertices) && skatepeer::Basis(*mesh,rest,basis)); }
        skatepeerpose::Packet body;
        body.epoch=1; body.sequence=uint16_t(count); body.map=1820; body.mesh=skatepeer::MeshId(*mesh); body.rig=skatepeerpose::RigId(bones,rig);
        const skaterig::Point sender{10,20,50},receiver{-100,80,20};
        float senderModel[16]; Model(.37f,.06f,{10.02f,19.97f,50.1f},senderModel);
        if(!skatepeerpose::Capture(target,offset,senderModel,sender,body)) {
            float scale; skatepeerpose::Q q;
            fprintf(stderr,"capture fail frame %u offset %f %f %f uniform=%d rotation=%d matrixw=%f\n",count,offset.x,offset.y,offset.z,
                modelboardsize::UniformScale(senderModel,scale),skatepeerpose::ModelRotation(senderModel,q,scale),senderModel[15]);
            assert(false);
        }
        const auto wire=skatepeerpose::Encode(body); skatepeerpose::Packet decoded;
        assert(wire.size()==244 && skatepeerpose::Decode(wire.data(),wire.size(),decoded));
        const auto sourcePalette=Palette(bones,output);
        skatevisual::Point centre{};
        for(auto v:frame->vertices) { centre.x+=v.x; centre.y+=v.y; centre.z+=v.z; }
        const float nv=float(frame->vertices.size()); centre={centre.x/nv,centre.y/nv,centre.z/nv};
        const auto fit=skateboard::ChooseFit(skaterig::Ragdoll(state),*frame,rider.air,1,{},centre);
        skateboard::Placed placed; assert(skateboard::Place(rest,*frame,{sender.x,sender.y,sender.z},fit,placed));
        skatepeer::Packet board; board.epoch=1; board.map=1820; board.mesh=body.mesh; board.state.sequence=body.sequence; board.state.flags=skatepeer::Active;
        skatepeer::Capture(board,placed,{sender.x,sender.y,sender.z},basis);
        auto boardWire=skatepeer::Encode(board); skatepeer::Packet remoteBoard;
        assert(skatepeer::Decode(boardWire.data(),boardWire.size(),remoteBoard) && skatepeerpose::Match(decoded,remoteBoard));
        timelineBoard.push_back(remoteBoard); timelinePose.push_back(decoded);
        float peerBoard[16]; assert(skatepeer::Matrix(remoteBoard,basis,{receiver.x,receiver.y,receiver.z},peerBoard));
        for(float yaw:{0.f,1.5707963f,6.2f}) {
            float receiverModel[16]; Model(yaw,-.04f,{-100.05f,80.01f,20.03f},receiverModel);
            skaterig::Point remoteOffset;
            assert(skatepeerpose::Restore(decoded,rig,receiverModel,receiver,remoteTarget,remoteOffset));
            assert(skaterig::Fill(bones.data(),rig,remoteTarget,remoteOffset,remoteOutput));
            if(count%30==0) {
                float previousModel[16]; Model(yaw-.12f,-.04f,{-100.40f,80.04f,20.03f},previousModel);
                static characterretarget::Pose staleTarget; skaterig::Point staleOffset;
                assert(skatepeerpose::Restore(decoded,rig,previousModel,receiver,staleTarget,staleOffset));
                // Old call order prepares against yesterday's basis, then Animate replaces it before MT consumes locals.
                for(unsigned role:{3u,20u,22u}) {
                    const auto old=Transform(characterretarget::Add(staleTarget.pivots[rig.plan[role]],staleOffset),receiverModel);
                    const auto correct=Transform(characterretarget::Add(remoteTarget.pivots[rig.plan[role]],remoteOffset),receiverModel);
                    staleMatrixError=(std::max)(staleMatrixError,skaterig::detail::Dist(old,correct));
                }
            }
            for(unsigned role:skaterig::BodyRoles) {
                const auto a=Transform(characterretarget::Add(target.pivots[rig.plan[role]],offset),senderModel);
                const auto b=Transform(characterretarget::Add(remoteTarget.pivots[rig.plan[role]],remoteOffset),receiverModel);
                peerJointError=(std::max)(peerJointError,skaterig::detail::Dist(characterretarget::Sub(a,sender),characterretarget::Sub(b,receiver)));
                if(role==20 || role==22) {
                    const skaterig::Point sourceDeck{placed.matrix[12],placed.matrix[13],placed.matrix[14]},remoteDeck{peerBoard[12],peerBoard[13],peerBoard[14]};
                    peerFootDeckError=(std::max)(peerFootDeckError,skaterig::detail::Dist(characterretarget::Sub(a,sourceDeck),characterretarget::Sub(b,remoteDeck)));
                }
            }
            if(skinPath && count%30==0) {
                const auto remotePalette=Palette(bones,remoteOutput);
                for(uint32_t i=0;i<vertexCount;++i) {
                    const auto* v=skin.data()+872+i*20; float xyz[3]; memcpy(xyz,v,12);
                    skaterig::Point a{},b{};
                    for(unsigned k=0;k<4;++k) if(v[12+k]) {
                        const auto weight=v[12+k]/255.f; const auto bone=v[16+k];
                        a=characterretarget::Add(a,characterretarget::Scale(Transform({xyz[0],xyz[1],xyz[2]},sourcePalette[bone].data()),weight));
                        b=characterretarget::Add(b,characterretarget::Scale(Transform({xyz[0],xyz[1],xyz[2]},remotePalette[bone].data()),weight));
                    }
                    a=characterretarget::Sub(Transform(a,senderModel),sender); b=characterretarget::Sub(Transform(b,receiverModel),receiver);
                    peerSkinError=(std::max)(peerSkinError,skaterig::detail::Dist(a,b));
                }
            }
        }
        ++peerFrames;
        // Compose the same pivot-relative row matrices as AnimateMT and check the actual joint positions.
        std::vector<std::array<float,16>> matrices(bones.size());
        for (uint32_t b=0;b<bones.size();++b) {
            float local[16]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
            skaterig::Apply(output,b,local);
            const auto p=bones[b].pivot;
            for (unsigned axis=0;axis<3;++axis)
                local[12+axis]-=p.x*local[axis]+p.y*local[4+axis]+p.z*local[8+axis];
            if (bones[b].parent<0) memcpy(matrices[b].data(),local,sizeof(local));
            else for (unsigned row=0;row<4;++row) for (unsigned column=0;column<4;++column) {
                float value=0;
                for (unsigned k=0;k<4;++k) value+=local[row*4+k]*matrices[bones[b].parent][k*4+column];
                matrices[b][row*4+column]=value;
            }
        }
        for (unsigned j=0;j<rig.model.size();++j) {
            const auto p=bones[rig.model[j]].pivot; const auto& m=matrices[rig.model[j]];
            const skaterig::Point got{p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12],p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13],p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14]};
            const auto expected=characterretarget::Add(target.pivots[j],offset);
            paletteError=(std::max)(paletteError,skaterig::detail::Dist(got,expected));
        }
    }
    printf("frames=%u air=%u planted=%u airLegs=%u refused=%u soleError=%.6f legError=%.6f paletteError=%.6f\n",
        count,air,planted,legs,refused,soleError,legError,paletteError);
    assert(count>500 && air>30 && planted>100 && legs>10 && !refused);
    assert(soleError<.0001f && legError<.02f && paletteError<.0001f);
    printf("peer frames=%u world joint error=%.6f world skin error=%.6f foot/deck error=%.6f yd\n",peerFrames,peerJointError,peerSkinError,peerFootDeckError);
    assert(peerFrames==604 && peerJointError<.015f && peerSkinError<.015f && peerFootDeckError<.015f);
    printf("pre-Animate stale matrix error=%.6f yd; post-transform reconstruction max=%.6f yd\n",staleMatrixError,peerJointError);
    assert(staleMatrixError>.3f);
    // Recreate 10Hz delivery with bounded jitter, lost packets and a changing native render basis every display frame.
    skatepeerpose::Presentation presentation;
    size_t next=0; int previousIndex=-1;
    skatepeer::Packet heldBoard; skatepeerpose::Packet heldPose;
    skaterig::Point previousHeld{},previousSmooth{};
    float heldJump=0,smoothJump=0,heldDeckJump=0,smoothDeckJump=0;
    skaterig::Point priorHeldDeck{},priorSmoothDeck{};
    unsigned samples=0;
    for(unsigned now=0;now<10000;now+=16) {
        while(next<timelinePose.size()) {
            const unsigned jitter=(unsigned(next/6)%4)*9;
            const unsigned due=unsigned(next*1000/60)+jitter;
            if(now<due) break;
            if(next==0 || (next/6)%7!=0) {
                heldBoard=timelineBoard[next]; heldPose=timelinePose[next];
                presentation.Push(heldBoard,heldPose,now,{},true); previousIndex=int(next);
            }
            next+=6;
        }
        if(previousIndex<0) continue;
        skatepeer::Packet smoothBoard; skatepeerpose::Packet smoothPose;
        presentation.Sample(now,smoothBoard,smoothPose);
        assert(skatepeerpose::Match(smoothPose,smoothBoard));
        const skaterig::Point actor{-100+now*.003f,80,20};
        float model[16]; Model(sinf(now*.003f)*.7f,.02f,{actor.x-.04f,actor.y+.02f,actor.z+.1f},model);
        static characterretarget::Pose smoothTarget,heldTarget; skaterig::Point smoothOffset,heldOffset;
        assert(skatepeerpose::Restore(smoothPose,rig,model,actor,smoothTarget,smoothOffset));
        assert(skatepeerpose::Restore(heldPose,rig,model,actor,heldTarget,heldOffset));
        const unsigned ankle=rig.plan[20];
        const auto smooth=characterretarget::Sub(Transform(characterretarget::Add(smoothTarget.pivots[ankle],smoothOffset),model),actor);
        const auto held=characterretarget::Sub(Transform(characterretarget::Add(heldTarget.pivots[ankle],heldOffset),model),actor);
        const skaterig::Point smoothDeck{smoothBoard.state.offset[0],smoothBoard.state.offset[1],smoothBoard.state.offset[2]},
            heldDeck{heldBoard.state.offset[0],heldBoard.state.offset[1],heldBoard.state.offset[2]};
        if(samples++) {
            smoothJump=(std::max)(smoothJump,skaterig::detail::Dist(smooth,previousSmooth));
            heldJump=(std::max)(heldJump,skaterig::detail::Dist(held,previousHeld));
            smoothDeckJump=(std::max)(smoothDeckJump,skaterig::detail::Dist(smoothDeck,priorSmoothDeck));
            heldDeckJump=(std::max)(heldDeckJump,skaterig::detail::Dist(heldDeck,priorHeldDeck));
        }
        previousSmooth=smooth; previousHeld=held; priorSmoothDeck=smoothDeck; priorHeldDeck=heldDeck;
        // Per-frame receiver-facing/translation does not leak into the requested world-relative root.
        assert(skaterig::detail::Dist(characterretarget::Sub(Transform(smoothOffset,model),actor),smoothPose.worldOffset)<.0001f);
    }
    printf("jitter/drop display samples=%u ankle max-step hold=%.6f paired=%.6f; deck hold=%.6f paired=%.6f yd\n",samples,heldJump,smoothJump,heldDeckJump,smoothDeckJump);
    assert(samples>500 && heldJump>.05f && smoothJump<heldJump*.5f && smoothDeckJump<heldDeckJump*.5f);
}
