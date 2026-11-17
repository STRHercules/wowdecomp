#pragma once
#include "character-alignment.h"

// Two-bone support-leg correction in model space. No source-frame mutations,
// changed bone lengths, entity coordinates, foot-mesh edits or physics changes.
namespace charactercontact {
using namespace characterretarget;
struct Result {
    bool valid=false;
    unsigned planted=0;
    float rootLift=0,maxSoleError=0;
};
inline void Rebuild(const characterrig::Snapshot& skeleton,Pose& pose) {
    for (unsigned i=0;i<pose.count;++i) {
        const auto& bone=skeleton.bones[i];
        if (bone.parent<0) { pose.global[i]=pose.local[i]; pose.pivots[i]=bone.pivot; }
        else {
            const auto parent=static_cast<unsigned>(bone.parent);
            pose.global[i]=Multiply(pose.global[parent],pose.local[i]);
            pose.pivots[i]=Add(pose.pivots[parent],Rotate(pose.global[parent],
                Sub(bone.pivot,skeleton.bones[parent].pivot)));
        }
        Matrix(pose.global[i],Sub(pose.pivots[i],Rotate(pose.global[i],bone.pivot)),pose.matrices[i]);
    }
}
inline bool Reach(const characterrig::Snapshot& skeleton,const Pose& pose,unsigned ankle,
                  float contactLift,float& low,float& high) {
    const unsigned knee=Joint(pose,ankle==20?14:15),hip=Joint(pose,ankle==20?7:8);ankle=Joint(pose,ankle);
    const float length=Length(Sub(skeleton.bones[knee].pivot,skeleton.bones[hip].pivot))
        +Length(Sub(skeleton.bones[ankle].pivot,skeleton.bones[knee].pivot));
    const Point span=Sub(pose.pivots[ankle],pose.pivots[hip]);
    const float verticalSquared=length*length-span.x*span.x-span.y*span.y;
    if (!std::isfinite(verticalSquared) || verticalSquared<=0) return false;
    const float vertical=sqrtf(verticalSquared),base=span.z+contactLift;
    low=(std::max)(low,base-vertical); high=(std::min)(high,base+vertical);
    return low<=high+.00001f;
}
inline bool Plant(const characterrig::Snapshot& skeleton,Pose& pose,unsigned ankle,float shift) {
    if (fabsf(shift)<.000001f) return true;
    const unsigned knee=Joint(pose,ankle==20?14:15),hip=Joint(pose,ankle==20?7:8);ankle=Joint(pose,ankle);
    const Point a=pose.pivots[hip],oldKnee=pose.pivots[knee],oldFoot=pose.pivots[ankle];
    const Point goal=Add(oldFoot,{0,0,shift});
    const float upper=Length(Sub(skeleton.bones[knee].pivot,skeleton.bones[hip].pivot));
    const float lower=Length(Sub(skeleton.bones[ankle].pivot,skeleton.bones[knee].pivot));
    const Point axis=Sub(goal,a); const float distance=Length(axis);
    Point direction{};
    if (!Unit(axis,direction) || distance>upper+lower+.00002f
        || distance<fabsf(upper-lower)+.00001f) return false;
    Point pole=Sub(oldKnee,a);
    pole=Sub(pole,Scale(direction,Dot(pole,direction)));
    Point bend{};
    if (!Unit(pole,bend)) {
        pole=Rotate(pose.global[hip],{1,0,0});
        pole=Sub(pole,Scale(direction,Dot(pole,direction)));
        if (!Unit(pole,bend)) return false;
    }
    const float along=(upper*upper-lower*lower+distance*distance)/(2*distance);
    const float height=sqrtf((std::max)(0.f,upper*upper-along*along));
    const Point newKnee=Add(a,Add(Scale(direction,along),Scale(bend,height)));
    Quaternion upperSwing{},lowerSwing{};
    if (!Align(Sub(oldKnee,a),Sub(newKnee,a),upperSwing)
        || !Align(Sub(oldFoot,oldKnee),Sub(goal,newKnee),lowerSwing)) return false;
    const Quaternion upperGlobal=Multiply(upperSwing,pose.global[hip]);
    const Quaternion lowerGlobal=Multiply(lowerSwing,pose.global[knee]);
    const Quaternion footGlobal=pose.global[ankle]; // Preserve source foot orientation.
    const auto parent=static_cast<unsigned>(skeleton.bones[hip].parent);
    pose.local[hip]=Multiply(Inverse(pose.global[parent]),upperGlobal);
    pose.local[knee]=Multiply(Inverse(upperGlobal),lowerGlobal);
    pose.local[ankle]=Multiply(Inverse(lowerGlobal),footGlobal);
    Rebuild(skeleton,pose);
    return Length(Sub(pose.pivots[ankle],goal))<.0001f;
}
inline Result Solve(const characterrig::Snapshot& skeleton,Pose& pose,
    const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,float facing,
    const characteralignment::Result& fit,float desiredRootLift,bool ownedOllie=false) {
    Result out{};
    if (!Supported(skeleton) || pose.count!=skeleton.count || !fit.valid || !std::isfinite(facing)
        || !boardplacement::Valid(fit.board) || !std::isfinite(fit.rootLift) || fit.rootLift<-.6f || fit.rootLift>.75f
        || (fit.supportAnkle!=20 && fit.supportAnkle!=22) || !std::isfinite(desiredRootLift)
        || desiredRootLift<-.6f || desiredRootLift>.75f || ((!frame.pose.ground || frame.pose.airborne) && !ownedOllie)) return out;
    const unsigned ankles[]={fit.supportAnkle,fit.supportAnkle==20?22u:20u};
    float lifts[]={fit.rootLift,0},bottom=INFINITY;
    for (auto point:frame.vertices) bottom=(std::min)(bottom,point.y);
    if (!std::isfinite(bottom)) return out;
    unsigned count=1;
    // Both feet can be planted during coasting. Do not pin a kicking/braking foot
    // away from the deck: require real coverage and near-equal SOURCE ankle height.
    const auto sourceA=boardplacement::WorldPoint(frame.anchors[fit.sourceAnkle],frame,{0,0,0},bottom,{true,1,0,0});
    const auto sourceB=boardplacement::WorldPoint(frame.anchors[fit.sourceAnkle==22?18:22],frame,{0,0,0},bottom,{true,1,0,0});
    float sourceDeck=0;
    if (fabsf(sourceA.z-sourceB.z)<.055f && characteralignment::DeckSurface(mesh,frame,bottom,
        {true,1,0,0},{sourceB.x,sourceB.y,sourceB.z},sourceDeck)) {
        float surface=-INFINITY;
        constexpr float solePoints[]={-.4f,0.f,1.f};
        for (float along:solePoints) {
            const auto sole=characteralignment::WorldDirection(characteralignment::SoleSample(skeleton,pose,ankles[1],along),facing);
            float deck=0;
            if (characteralignment::DeckSurface(mesh,frame,bottom,fit.board,sole,deck)) surface=(std::max)(surface,deck);
        }
        if (std::isfinite(surface)) {
            lifts[1]=surface-characteralignment::LowestSole(skeleton,pose,ankles[1])+characteralignment::SoleClearance;
            if (lifts[1]>=-.6f && lifts[1]<=.75f) count=2;
        }
    }
    float low=-.6f,high=.75f;
    for (unsigned i=0;i<count;++i) {
        if (!Reach(skeleton,pose,ankles[i],lifts[i],low,high)) {
            // Secondary contact must not destabilize an otherwise valid support.
            if (i==1) { count=1; low=-.6f; high=.75f; if (!Reach(skeleton,pose,ankles[0],lifts[0],low,high)) return out; }
            else return out;
        }
    }
    if (low>high) return out;
    out.rootLift=(std::max)(low,(std::min)(high,desiredRootLift));
    Pose corrected=pose;
    for (unsigned i=0;i<count;++i) {
        const float before=characteralignment::LowestSole(skeleton,pose,ankles[i]);
        if (!Plant(skeleton,corrected,ankles[i],lifts[i]-out.rootLift)) return {};
        const float error=fabsf(characteralignment::LowestSole(skeleton,corrected,ankles[i])+out.rootLift-before-lifts[i]);
        out.maxSoleError=(std::max)(out.maxSoleError,error);
        if (!std::isfinite(error) || error>.0001f) return {};
    }
    pose=corrected; out.planted=count; out.valid=true; return out;
}
}
