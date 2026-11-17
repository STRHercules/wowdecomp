#pragma once
#include "character-retarget.h"
#include "board-placement.h"
namespace characterragdoll {
using namespace characterretarget;
// Proper 3D orthonormal torso frame. No projection onto the horizontal plane.
inline bool Basis(Point left,Point up,Quaternion& q) {
    Point z{},y{},x{};
    if (!Unit(up,z) || !Unit(Sub(left,Scale(z,Dot(left,z))),y) || !Unit(Cross(y,z),x)) return false;
    const float trace=x.x+y.y+z.z;
    if(trace>0) { const float s=2*sqrtf(trace+1);q={(y.z-z.y)/s,(z.x-x.z)/s,(x.y-y.x)/s,.25f*s}; }
    else if(x.x>y.y && x.x>z.z) { const float s=2*sqrtf(1+x.x-y.y-z.z);q={.25f*s,(y.x+x.y)/s,(z.x+x.z)/s,(y.z-z.y)/s}; }
    else if(y.y>z.z) { const float s=2*sqrtf(1+y.y-x.x-z.z);q={(y.x+x.y)/s,.25f*s,(z.y+y.z)/s,(z.x-x.z)/s}; }
    else { const float s=2*sqrtf(1+z.z-x.x-y.y);q={(z.x+x.z)/s,(z.y+y.z)/s,.25f*s,(x.y-y.x)/s}; }
    const float length=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!std::isfinite(length) || length<.001f)return false;
    q={q.x/length,q.y/length,q.z/length,q.w/length};return true;
}
inline bool Solve(const characterrig::Snapshot& skeleton,const skatevisual::Frame& frame,float facing,Pose& out) {
    out={};if(!characterrig::Discover(skeleton,out.rig) || !std::isfinite(facing))return false;
    Point source[24]{};
    for(unsigned i=0;i<24;++i){source[i]=ModelPoint(frame.anchors[i],frame.pose,facing);if(!characterrig::Finite(source[i]) || Length(source[i])>6)return false;}
    Quaternion base{};
    if(!Basis(Sub(source[20],source[16]),Sub(source[1],source[0]),base))return false;
    Quaternion desired[characterrig::MaxBones]{};bool mapped[characterrig::MaxBones]{};
    desired[out.rig[0]]=base;mapped[out.rig[0]]=true;
    for(const auto& link:Links) {
        const unsigned bone=out.rig[link.bone];
        Quaternion swing{};const auto bind=BindSegment(skeleton,out.rig,link);
        if(!Align(Rotate(base,bind),Sub(source[link.sourceChild],source[link.source]),swing))return false;
        desired[bone]=Multiply(swing,base);mapped[bone]=true;
    }
    out.count=skeleton.count;
    for(unsigned i=0;i<out.count;++i) {
        const auto& b=skeleton.bones[i];
        out.global[i]=mapped[i]?desired[i]:b.parent<0?Quaternion{}:out.global[b.parent];
        out.local[i]=b.parent<0?out.global[i]:Multiply(Inverse(out.global[b.parent]),out.global[i]);
        out.pivots[i]=b.parent<0?b.pivot:Add(out.pivots[b.parent],Rotate(out.global[b.parent],Sub(b.pivot,skeleton.bones[b.parent].pivot)));
        if(!characterrig::Finite(out.pivots[i]))return false;
        Matrix(out.global[i],Sub(out.pivots[i],Rotate(out.global[i],b.pivot)),out.matrices[i]);
    }
    return true;
}
inline Point ToModel(Point world,float facing) {
    const float c=cosf(facing),s=sinf(facing);return {c*world.x+s*world.y,-s*world.x+c*world.y,world.z};
}
inline bool RootShiftValid(Point p) { return characterrig::Finite(p) && Length(p)<=4.5f; }
inline Point SourceHip(const skatevisual::Frame& frame,Point origin,Point player,float facing) {
    const auto p=frame.anchors[0];return ToModel({origin.x+p.x/.9144f-player.x,origin.y-p.z/.9144f-player.y,origin.z+p.y/.9144f-player.z},facing);
}
struct Anchor {
    bool ready=false;Point residual{};float scale=1;
    void Clear(){*this={};}
    bool Capture(const skatevisual::Frame& frame,const Pose& body,Point origin,Point player,float facing,Point visual,float boardScale) {
        if(!frame.pose.ground || frame.pose.airborne || !body.count || !RootShiftValid(visual))return false;
        const auto r=Rotate(Inverse(body.global[Joint(body,0)]),Sub(Add(body.pivots[Joint(body,3)],visual),SourceHip(frame,origin,player,facing)));
        if(!characterrig::Finite(r) || Length(r)>1.5f || !std::isfinite(boardScale) || boardScale<.7f || boardScale>1.4f)return false;
        residual=r;scale=boardScale;ready=true;return true;
    }
    bool Shift(const skatevisual::Frame& frame,const Pose& body,Point origin,Point player,float facing,Point& out) const {
        if(!body.count || !std::isfinite(facing))return false;
        const auto desired=Add(SourceHip(frame,origin,player,facing),Rotate(body.global[Joint(body,0)],ready?residual:Point{}));
        out=Sub(desired,body.pivots[Joint(body,3)]);return RootShiftValid(out);
    }
};
inline boardplacement::Fit Board(const skatevisual::Frame& frame,Point origin,float scale) {
    boardplacement::Fit fit{};if(frame.vertices.empty())return fit;
    Point centre{};for(auto v:frame.vertices)centre=Add(centre,{v.x,v.y,v.z});centre=Scale(centre,1.f/static_cast<float>(frame.vertices.size()));
    fit.valid=true;fit.scale=scale;fit.physical=true;fit.sourceCentre={centre.x,centre.y,centre.z};fit.sourceOrigin={origin.x,origin.y,origin.z};
    return boardplacement::Valid(fit)?fit:boardplacement::Fit{};
}
// The original held-board pose attaches to a Source hand. Retargeted WoW arms
// retain different bind lengths: translate the visual mesh to that same hand,
// preserving the Source orientation and scaling uniformly about the grip.
inline boardplacement::Fit CarriedBoard(const skatevisual::Frame& frame,const Pose& body,
    Point origin,Point player,float facing,Point shift,float scale,float modelScale=1) {
    auto fit=Board(frame,origin,scale);
    fit.modelScale=modelScale;
    if(!boardplacement::Valid(fit) || !frame.carriedHand)return fit;
    if(!body.count || frame.carriedHand>2 || !std::isfinite(facing))return {};
    const unsigned source=frame.carriedHand==1?11:15,bone=Joint(body,frame.carriedHand==1?38:36);
    const auto grip=frame.anchors[source];
    const auto sourceWorld=Point{origin.x+grip.x/.9144f,origin.y-grip.z/.9144f,origin.z+grip.y/.9144f};
    const auto hand=Scale(Add(body.pivots[bone],shift),modelScale);const float c=cosf(facing),s=sinf(facing);
    const auto target=Add(player,{c*hand.x-s*hand.y,s*hand.x+c*hand.y,hand.z});
    const auto delta=Sub(target,sourceWorld);
    if(!characterrig::Finite(Scale(delta,1/modelScale)) || Length(delta)>2.5f*(std::max)(1.f,modelScale))return {};
    fit.sourceCentre=grip;fit.sourceOrigin={origin.x+delta.x,origin.y+delta.y,origin.z+delta.z};
    return boardplacement::Valid(fit)?fit:boardplacement::Fit{};
}
}
