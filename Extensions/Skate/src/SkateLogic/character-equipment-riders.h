#pragma once
#include "character-retarget.h"

// Vanilla rig.rs::hip_riders, with its converted lateral axis restored to native
// M2 +Y. These are model attachment bones, not equipment-specific offsets. No
// entity, collision, source pose, animation-track or asset memory is modified.
namespace characterequipment {
using namespace characterretarget;
constexpr unsigned MaxRiders=8;
struct Plan { unsigned bones[MaxRiders]{},count=0,spine=0,waist=0; bool valid=false; };
inline Plan Discover(const characterrig::Snapshot& skeleton) {
    characterrig::Plan body;
    Plan out{};
    if(!characterrig::Discover(skeleton,body))return out;
    out.spine=body[2];out.waist=body[3];
    constexpr unsigned roles[]={0,1,2,3,4,7,8,9,10,11,14,15,16,17,18,20,22,30,31,32,33,36,38};
    for(unsigned i=0;i<skeleton.count;++i){
        const auto& bone=skeleton.bones[i];
        if(bone.parent!=static_cast<int>(out.spine)
            || fabsf(bone.pivot.y-skeleton.bones[out.spine].pivot.y)<=.1f)continue;
        bool mapped=false;for(unsigned role:roles)if(body[role]==i)mapped=true;
        if(mapped)continue;
        if(out.count==MaxRiders)return {};
        out.bones[out.count++]=i;
    }
    out.valid=true;return out;
}
inline Plan Scoped(const characterrig::Snapshot& skeleton,const Plan& discovered){
    if(!discovered.valid || discovered.count>MaxRiders || discovered.spine>=skeleton.count
        || discovered.waist>=skeleton.count)return {};
    Plan out=discovered;out.count=0;
    for(unsigned i=0;i<discovered.count;++i){
        const unsigned bone=discovered.bones[i];
        if(bone>=skeleton.count)return {};
        // Native scopes only own ordinary deform/attachment bones. Inactive
        // flags=0 authoring helpers (e.g. Blood Elf Female110) stay untouched.
        if((skeleton.bones[bone].flags&0x287)==0x200)out.bones[out.count++]=bone;
    }
    return out;
}
struct Compensation { Quaternion rotation{}; Point translation{}; bool valid=false; };
inline Compensation Solve(const characterrig::Snapshot& skeleton,const Pose& body,const Plan& plan){
    Compensation out{};
    if(!plan.valid || !plan.count || plan.count>MaxRiders || body.count!=skeleton.count
        || plan.spine>=body.count || plan.waist>=body.count)return out;
    const auto& spine=body.global[plan.spine];const auto& waist=body.global[plan.waist];
    const auto inverse=Inverse(spine);
    const Point spineShift=Sub(body.pivots[plan.spine],Rotate(spine,skeleton.bones[plan.spine].pivot));
    const Point waistShift=Sub(body.pivots[plan.waist],Rotate(waist,skeleton.bones[plan.waist].pivot));
    out.rotation=Multiply(inverse,waist);
    out.translation=Rotate(inverse,Sub(waistShift,spineShift));
    const float norm=out.rotation.x*out.rotation.x+out.rotation.y*out.rotation.y
        +out.rotation.z*out.rotation.z+out.rotation.w*out.rotation.w;
    out.valid=characterrig::Finite(out.translation) && Length(out.translation)<=4
        && std::isfinite(norm) && fabsf(norm-1)<.001f;
    return out;
}
inline bool Apply(const Compensation& compensation,float* local){
    if(!compensation.valid || !local)return false;
    const auto q=compensation.rotation;
    const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
    if(!std::isfinite(norm) || fabsf(norm-1)>.001f
        || !characterrig::Finite(compensation.translation) || Length(compensation.translation)>4)return false;
    float result[16];std::memcpy(result,local,sizeof(result));
    // Premultiply the native local skin by inverse(SpineLow skin)*Waist skin.
    // Its own animated rotation/translation and descendant attachment transforms
    // remain intact. Works on either side of native negative-pivot translation.
    for(unsigned column=0;column<4;++column){
        const unsigned at=column*4;
        Point value{local[at],local[at+1],local[at+2]};
        if(!characterrig::Finite(value) || !std::isfinite(local[at+3]))return false;
        value=Add(Rotate(compensation.rotation,value),Scale(compensation.translation,local[at+3]));
        if(!characterrig::Finite(value))return false;
        result[at]=value.x;result[at+1]=value.y;result[at+2]=value.z;
    }
    std::memcpy(local,result,sizeof(result));return true;
}
}
