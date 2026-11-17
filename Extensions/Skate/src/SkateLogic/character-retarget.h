#pragma once
#include "character-rig-schema.h"
#include "character-rig-plan.h"
#include "skate-visual.h"
#include <cstring>

// Pure, model-local pose solver. Never reads or writes a game process.
// Joint-direction retargeting preserves WoW bind lengths and attachment ancestry.
// It is not full source-bone twist/finger retargeting or a deployed native pose hook.
namespace characterretarget {
using Point=characterrig::Point;
struct Quaternion { float x=0,y=0,z=0,w=1; };
inline Point Add(Point a,Point b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Point Sub(Point a,Point b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Point Scale(Point a,float s) { return {a.x*s,a.y*s,a.z*s}; }
inline float Dot(Point a,Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Point Cross(Point a,Point b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
inline float Length(Point a) { return sqrtf(Dot(a,a)); }
inline bool Unit(Point a,Point& out) {
    const float length=Length(a);
    if (!std::isfinite(length) || length<.00001f) return false;
    out=Scale(a,1/length); return true;
}
inline Quaternion Multiply(Quaternion a,Quaternion b) {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
        a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
        a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
        a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Quaternion Inverse(Quaternion q) { return {-q.x,-q.y,-q.z,q.w}; }
inline Point Rotate(Quaternion q,Point v) {
    const Point twice=Scale(Cross({q.x,q.y,q.z},v),2);
    return Add(v,Add(Scale(twice,q.w),Cross({q.x,q.y,q.z},twice)));
}
inline bool Align(Point from,Point to,Quaternion& q) {
    Point a{},b{}; if (!Unit(from,a) || !Unit(to,b)) return false;
    const float dot=Dot(a,b);
    if (dot<-.99999f) {
        Point axis{};
        const Point basis=fabsf(a.x)<.8f?Point{1,0,0}:Point{0,1,0};
        if (!Unit(Cross(a,basis),axis)) return false;
        q={axis.x,axis.y,axis.z,0}; return true;
    }
    const Point cross=Cross(a,b); const float w=1+dot;
    const float length=sqrtf(Dot(cross,cross)+w*w);
    if (!std::isfinite(length) || length<.00001f) return false;
    q={cross.x/length,cross.y/length,cross.z/length,w/length}; return true;
}
struct Link { unsigned bone,child,source,sourceChild; };
constexpr Link Links[]={
    {3,2,0,1}, {2,4,1,3}, {4,9,3,5}, {9,16,5,7},
    {10,17,12,13}, {17,30,13,14}, {30,36,14,15},
    {11,18,8,9}, {18,31,9,10}, {31,38,10,11},
    {7,14,20,21}, {14,20,21,22}, {20,32,22,23},
    {8,15,16,17}, {15,22,17,18}, {22,33,18,19}
};
inline Point BindSegment(const characterrig::Snapshot& s,const characterrig::Plan& plan,const Link& link){
    auto bind=Sub(s.bones[plan[link.child]].pivot,s.bones[plan[link.bone]].pivot);
    if(link.bone==3 && Length(bind)<.00001f)bind=Sub(s.bones[plan[4]].pivot,s.bones[plan[3]].pivot);
    return bind;
}
struct Pose {
    unsigned count=0;
    characterrig::Plan rig{};
    Point pivots[characterrig::MaxBones]{};
    Quaternion global[characterrig::MaxBones]{},local[characterrig::MaxBones]{};
    float matrices[characterrig::MaxBones][16]{};
};
inline bool Supported(const characterrig::Snapshot& s) {
    characterrig::Plan plan;return characterrig::Discover(s,plan);
}
inline unsigned Joint(const Pose& p,unsigned role){return p.rig[role];}
inline Point ModelPoint(skatevisual::Point p,const skatewire::Pose& pose,float facing) {
    const float x=(p.x-pose.position[0])/.9144f,y=-(p.z-pose.position[2])/.9144f;
    const float c=cosf(facing),s=sinf(facing);
    return {c*x+s*y,-s*x+c*y,(p.y-pose.position[1])/.9144f};
}
inline void Matrix(Quaternion q,Point translation,float* m) {
    const Point x=Rotate(q,{1,0,0}),y=Rotate(q,{0,1,0}),z=Rotate(q,{0,0,1});
    const float values[]={x.x,x.y,x.z,0,y.x,y.y,y.z,0,z.x,z.y,z.z,0,translation.x,translation.y,translation.z,1};
    std::memcpy(m,values,sizeof(values));
}
inline bool Solve(const characterrig::Snapshot& skeleton,const skatevisual::Frame& frame,float facing,Pose& out,bool onFoot=false) {
    out={};
    if (!characterrig::Discover(skeleton,out.rig) || !std::isfinite(facing)) return false;
    Point source[24]{};
    for (unsigned i=0;i<24;++i) {
        source[i]=ModelPoint(frame.anchors[i],frame.pose,facing);
        if (!characterrig::Finite(source[i]) || Length(source[i])>6) return false;
    }
    Point up{},left{},forward{};
    if (!Unit(Sub(source[onFoot?1:7],source[0]),up) || !Unit(onFoot?Sub(source[20],source[16]):Sub(source[12],source[8]),left)
        || !Unit(Cross(left,up),forward) || hypotf(forward.x,forward.y)<.1f) return false;
    // Chest-facing yaw comes from shoulder/torso axes, not board velocity: the
    // rider stands sideways and coasting can be opposite to board facing.
    // On foot, shoulders counter-rotate during each stride. They must not turn
    // the whole model/root and its hip offset. Use the actual pelvis axis there,
    // as the reference maps the waist to Source HIPS, retaining ordinary gait.
    const float heading=atan2f(forward.y,forward.x);
    const Quaternion yaw{0,0,sinf(heading*.5f),cosf(heading*.5f)};
    Quaternion desired[characterrig::MaxBones]{};
    bool mapped[characterrig::MaxBones]{};
    desired[out.rig[0]]=yaw; mapped[out.rig[0]]=true;
    for (const auto& link:Links) {
        Quaternion swing{};
        const unsigned bone=out.rig[link.bone];
        const Point bind=BindSegment(skeleton,out.rig,link);
        if (!Align(Rotate(yaw,bind),Sub(source[link.sourceChild],source[link.source]),swing)) return false;
        desired[bone]=Multiply(swing,yaw); mapped[bone]=true;
    }
    out.count=skeleton.count;
    for (unsigned i=0;i<out.count;++i) {
        const auto& b=skeleton.bones[i];
        if (b.parent<0) {
            out.global[i]=mapped[i]?desired[i]:Quaternion{};
            out.local[i]=out.global[i]; out.pivots[i]=b.pivot;
        } else {
            const unsigned parent=static_cast<unsigned>(b.parent);
            out.global[i]=mapped[i]?desired[i]:out.global[parent];
            out.local[i]=Multiply(Inverse(out.global[parent]),out.global[i]);
            out.pivots[i]=Add(out.pivots[parent],Rotate(out.global[parent],Sub(b.pivot,skeleton.bones[parent].pivot)));
        }
        if (!characterrig::Finite(out.pivots[i])) { out={}; return false; }
        // Global bind-to-pose matrices for tests/attachments, not native camera
        // palettes. Native integration must supply the correct local pose stage.
        Matrix(out.global[i],Sub(out.pivots[i],Rotate(out.global[i],b.pivot)),out.matrices[i]);
    }
    return true;
}
}
