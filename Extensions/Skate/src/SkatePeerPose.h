#pragma once
#include "SkatePeers.h"
#include "SkateRig.h"
#include "SkateLogic/model-board-size.h"

namespace skatepeerpose {
using Q=characterretarget::Quaternion;
using Point=characterrig::Point;
constexpr unsigned Bytes=30+skaterig::BodyCount*4,TextBytes=Bytes*2;
struct Packet {
    uint32_t epoch=0,map=0,mesh=0,rig=0;
    uint16_t sequence=0;
    Q model{};
    Point worldOffset{};
    std::array<Q,skaterig::BodyCount> rotations{};
};
template<class Model> struct Hook {
    Model* model=nullptr;
    bool previous=false;
    void Release() { if(model) model->m_boneHook=previous; model=nullptr; }
    void Own(Model* next) { if(model==next) return; Release(); model=next; if(model) { previous=model->m_boneHook; model->m_boneHook=true; } }
    void Forget(Model* gone) { if(model==gone) model=nullptr; }
};
inline bool Pack(Q q,uint32_t& out) {
    float v[]{q.x,q.y,q.z,q.w}; float norm=0;
    unsigned largest=0;
    for(unsigned i=0;i<4;++i) { if(!std::isfinite(v[i])) return false; norm+=v[i]*v[i]; if(fabsf(v[i])>fabsf(v[largest])) largest=i; }
    if(norm<.9f || norm>1.1f) return false;
    const float scale=(v[largest]<0?-1.f:1.f)/sqrtf(norm);
    out=largest<<30; unsigned slot=0;
    for(unsigned i=0;i<4;++i) if(i!=largest) {
        const int n=int(lrintf(v[i]*scale*(511.f*1.41421356237f)));
        if(n<-511 || n>511) return false;
        out|=(uint32_t(n)&1023u)<<(10*slot++);
    }
    return true;
}
inline bool Unpack(uint32_t packed,Q& out) {
    float v[4]{},sum=0,maximum=0; const unsigned largest=packed>>30; unsigned slot=0;
    for(unsigned i=0;i<4;++i) if(i!=largest) {
        int n=int((packed>>(10*slot++))&1023u); if(n&512) n-=1024;
        if(n==-512) return false;
        v[i]=n/(511.f*1.41421356237f); sum+=v[i]*v[i]; maximum=(std::max)(maximum,fabsf(v[i]));
    }
    if(!std::isfinite(sum) || sum>1.f) return false;
    v[largest]=sqrtf(1.f-sum);
    if(v[largest]+.002f<maximum) return false;
    out={v[0],v[1],v[2],v[3]}; return true;
}
inline uint32_t RigId(const std::vector<skaterig::SourceBone>& bones,const skaterig::Rig& rig) {
    uint32_t hash=2166136261u;
    const auto word=[&](uint32_t n) { for(unsigned i=0;i<4;++i) { hash^=uint8_t(n>>(i*8)); hash*=16777619u; } };
    word(uint32_t(bones.size())); word(uint32_t(rig.model.size()));
    for(const auto& b:bones) {
        word(uint32_t(b.key)); word(uint32_t(b.parent)); word(b.flags); word(b.rotationSequences);
        for(float v:{b.pivot.x,b.pivot.y,b.pivot.z}) { uint32_t bits; memcpy(&bits,&v,4); word(bits); }
    }
    for(unsigned role:skaterig::BodyRoles) word(rig.model[rig.plan[role]]);
    return hash;
}
inline void Put(std::array<uint8_t,Bytes>& bytes,unsigned at,uint32_t value,unsigned count) {
    for(unsigned i=0;i<count;++i) bytes[at+i]=uint8_t(value>>(8*i));
}
inline uint32_t Get(const std::array<uint8_t,Bytes>& bytes,unsigned at,unsigned count) {
    uint32_t n=0; for(unsigned i=0;i<count;++i) n|=uint32_t(bytes[at+i])<<(8*i); return n;
}
inline std::string Encode(const Packet& p) {
    if(!p.epoch || !characterrig::Finite(p.worldOffset)) return {};
    std::array<uint8_t,Bytes> bytes{}; bytes[0]=1;
    Put(bytes,1,p.epoch,4); Put(bytes,5,p.sequence,2); Put(bytes,7,p.map,4); Put(bytes,11,p.mesh,4); Put(bytes,15,p.rig,4);
    uint32_t q; if(!Pack(p.model,q)) return {}; Put(bytes,19,q,4);
    for(unsigned i=0;i<3;++i) { const float v=(&p.worldOffset.x)[i]; if(fabsf(v)>8) return {};
        Put(bytes,23+i*2,uint16_t(int16_t(lrintf(v*2048))),2); }
    bytes[29]=skaterig::BodyCount;
    for(unsigned i=0;i<skaterig::BodyCount;++i) { if(!Pack(p.rotations[i],q)) return {}; Put(bytes,30+i*4,q,4); }
    std::string text; for(uint8_t b:bytes) { text+=skatepeer::Hex(b>>4); text+=skatepeer::Hex(b); } return text;
}
inline bool Decode(const char* text,size_t size,Packet& out) {
    if(size!=TextBytes) return false;
    std::array<uint8_t,Bytes> bytes{};
    for(unsigned i=0;i<Bytes;++i) { uint32_t n; if(!skatepeer::Number(text+i*2,2,n)) return false; bytes[i]=uint8_t(n); }
    if(bytes[0]!=1 || bytes[29]!=skaterig::BodyCount) return false;
    Packet p; p.epoch=Get(bytes,1,4); p.sequence=uint16_t(Get(bytes,5,2)); p.map=Get(bytes,7,4); p.mesh=Get(bytes,11,4); p.rig=Get(bytes,15,4);
    if(!p.epoch || !Unpack(Get(bytes,19,4),p.model)) return false;
    for(unsigned i=0;i<3;++i) { (&p.worldOffset.x)[i]=int16_t(Get(bytes,23+i*2,2))/2048.f; if(fabsf((&p.worldOffset.x)[i])>8) return false; }
    for(unsigned i=0;i<skaterig::BodyCount;++i) if(!Unpack(Get(bytes,30+i*4,4),p.rotations[i])) return false;
    out=p; return true;
}
inline bool Match(const Packet& pose,const skatepeer::Packet& board) {
    return (board.state.flags&skatepeer::Active) && !(board.state.flags&skatepeer::Hidden)
        && pose.epoch==board.epoch && pose.sequence==board.state.sequence && pose.map==board.map && pose.mesh==board.mesh;
}
inline Q Blend(Q a,Q b,float t) {
    float dot=a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w;
    if(dot<0) { b={-b.x,-b.y,-b.z,-b.w}; dot=-dot; }
    float wa=1-t,wb=t;
    if(dot<.9995f) { const float angle=acosf((std::min)(1.f,dot)),den=sinf(angle); wa=sinf((1-t)*angle)/den; wb=sinf(t*angle)/den; }
    Q out{wa*a.x+wb*b.x,wa*a.y+wb*b.y,wa*a.z+wb*b.z,wa*a.w+wb*b.w};
    const float n=sqrtf(out.x*out.x+out.y*out.y+out.z*out.z+out.w*out.w);
    return {out.x/n,out.y/n,out.z/n,out.w/n};
}
// One presentation phase for the board and every rider component. Never predicts network movement or physics.
struct Presentation {
    bool ready=false;
    bool airAnchor=false,targetAir=false,waitingNativeGround=false;
    Point fromAnchor{},toAnchor{};
    uint32_t at=0;
    skatepeer::Packet fromBoard,toBoard;
    Packet fromPose,toPose;
    void Clear() { ready=false; airAnchor=false; targetAir=false; waitingNativeGround=false; }
    // Air visuals follow the transmitted Skate trajectory, not the stock observer's ballistic prediction.
    Point Actor(uint32_t now,Point native) const {
        if(!ready || !airAnchor) return native;
        const float t=(std::min)(1.f,(now-at)*.01f);
        const Point end=(targetAir || waitingNativeGround)?toAnchor:native;
        const Point shown{fromAnchor.x+t*(end.x-fromAnchor.x),fromAnchor.y+t*(end.y-fromAnchor.y),fromAnchor.z+t*(end.z-fromAnchor.z)};
        const Point delta{shown.x-native.x,shown.y-native.y,shown.z-native.z};
        return characterretarget::Length(delta)<=16.f?shown:native;
    }
    void Sample(uint32_t now,skatepeer::Packet& board,Packet& pose) const {
        board=toBoard; pose=toPose;
        const float t=(std::min)(1.f,(now-at)*.01f);
        for(unsigned i=0;i<3;++i) {
            board.state.offset[i]=fromBoard.state.offset[i]+t*(toBoard.state.offset[i]-fromBoard.state.offset[i]);
            (&pose.worldOffset.x)[i]=(&fromPose.worldOffset.x)[i]+t*((&toPose.worldOffset.x)[i]-(&fromPose.worldOffset.x)[i]);
        }
        board.scale=fromBoard.scale+t*(toBoard.scale-fromBoard.scale);
        const auto q=Blend({fromBoard.state.rotation[0],fromBoard.state.rotation[1],fromBoard.state.rotation[2],fromBoard.state.rotation[3]},
            {toBoard.state.rotation[0],toBoard.state.rotation[1],toBoard.state.rotation[2],toBoard.state.rotation[3]},t);
        memcpy(board.state.rotation,&q,sizeof(q));
        pose.model=Blend(fromPose.model,toPose.model,t);
        for(unsigned i=0;i<skaterig::BodyCount;++i) {
            const Q world=Blend(characterretarget::Multiply(fromPose.model,fromPose.rotations[i]),
                characterretarget::Multiply(toPose.model,toPose.rotations[i]),t);
            pose.rotations[i]=characterretarget::Multiply(characterretarget::Inverse(pose.model),world);
        }
    }
    void Push(const skatepeer::Packet& board,const Packet& pose,uint32_t now,Point native,bool observerGrounded) {
        const bool reset=!ready || toPose.epoch!=pose.epoch || toPose.rig!=pose.rig || toPose.map!=pose.map
            || toPose.mesh!=pose.mesh || now-at>=skatepeer::ExpireMs;
        const Point previous=reset?native:Actor(now,native);
        const bool landing=!reset && airAnchor && (targetAir || waitingNativeGround || now-at<100);
        targetAir=board.actorKnown && (board.state.flags&skatepeer::Airborne) && !(board.state.flags&skatepeer::Free);
        // Source touchdown can precede the observer's stock jump. Stay on the source floor until it catches up.
        waitingNativeGround=board.actorKnown && landing && !targetAir && !observerGrounded;
        airAnchor=board.actorKnown && (targetAir || landing);
        fromAnchor=previous;
        toAnchor={board.actor[0],board.actor[1],board.actor[2]};
        if(reset) { fromBoard=board; fromPose=pose; }
        else { skatepeer::Packet currentBoard; Packet currentPose; Sample(now,currentBoard,currentPose); fromBoard=currentBoard; fromPose=currentPose; }
        toBoard=board; toPose=pose; at=now; ready=true;
    }
};
inline bool ModelRotation(const float* matrix,Q& rotation,float& scale) {
    if(!modelboardsize::UniformScale(matrix,scale)) return false;
    float r[9]; for(unsigned i=0;i<3;++i) for(unsigned j=0;j<3;++j) r[i*3+j]=matrix[i*4+j]/scale;
    skateboard::Quaternion(r,&rotation.x); uint32_t check; return Pack(rotation,check);
}
inline bool FiniteWorld(Point p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
        && fabsf(p.x)<=1e6f && fabsf(p.y)<=1e6f && fabsf(p.z)<=1e6f;
}
inline bool Capture(const characterretarget::Pose& solved,Point offset,const float* model,Point actor,Packet& packet) {
    float scale;
    if(!ModelRotation(model,packet.model,scale) || !characterrig::Finite(offset) || !FiniteWorld(actor)) return false;
    packet.worldOffset=characterretarget::Add(characterretarget::Sub({model[12],model[13],model[14]},actor),
        characterretarget::Scale(characterretarget::Rotate(packet.model,offset),scale));
    for(unsigned i=0;i<skaterig::BodyCount;++i) packet.rotations[i]=solved.global[solved.rig[skaterig::BodyRoles[i]]];
    return true;
}
inline bool Restore(const Packet& packet,const skaterig::Rig& rig,const float* model,Point actor,
                    characterretarget::Pose& target,Point& offset) {
    Q rotation; float scale;
    if(!ModelRotation(model,rotation,scale) || !FiniteWorld(actor)) return false;
    const Q correction=characterretarget::Multiply(characterretarget::Inverse(rotation),packet.model);
    target={}; target.count=rig.skeleton.count; target.rig=rig.plan;
    for(unsigned i=0;i<skaterig::BodyCount;++i) target.global[rig.plan[skaterig::BodyRoles[i]]]=characterretarget::Multiply(correction,packet.rotations[i]);
    for(unsigned i=0;i<target.count;++i) {
        const int parent=rig.skeleton.bones[i].parent;
        target.local[i]=parent<0?target.global[i]:characterretarget::Multiply(characterretarget::Inverse(target.global[parent]),target.global[i]);
    }
    charactercontact::Rebuild(rig.skeleton,target);
    const Point root=rig.skeleton.bones[rig.plan[0]].pivot;
    const Point shift=characterretarget::Sub(characterretarget::Add(actor,packet.worldOffset),{model[12],model[13],model[14]});
    offset=characterretarget::Add(characterretarget::Scale(characterretarget::Rotate(characterretarget::Inverse(rotation),shift),1/scale),
        characterretarget::Sub(characterretarget::Rotate(correction,root),root));
    return characterrig::Finite(offset) && characterretarget::Length(offset)<16;
}
}
