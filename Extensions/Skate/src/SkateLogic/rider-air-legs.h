#pragma once
#include "character-foot-contact.h"

// Frame-local visual retarget only. No retained foot/catch calibration, deck
// adjustment, world contact queries, entity motion or helper/physics mutation.
namespace riderlegs {
using namespace characterretarget;
struct Result { bool valid=false; unsigned clamped=0; float before=0,after=0,maxChange=0; };
// Audited owned assets/rig.json bind-space FOOT.y (metres), not a tuning value.
constexpr float SourceAnkleHeight=.1100436f;
inline Point Model(skatevisual::Point p,float facing) {
    const float c=cosf(facing),s=sinf(facing);
    return {c*p.x+s*p.y,-s*p.x+c*p.y,p.z};
}
inline bool Target(const characterrig::Snapshot& skeleton,const Pose& body,
                   const skatevisual::Frame& frame,const boardplacement::Fit& fit,
                   float facing,Point shift,unsigned ankle,Point& out) {
    if ((ankle!=20 && ankle!=22) || !Supported(skeleton) || body.count!=skeleton.count
        || !boardplacement::Valid(fit) || !fit.rootRelative || fit.physical
        || !std::isfinite(facing) || !characterrig::Finite(shift)
        || (frame.pose.ground && !frame.pose.airborne)) return false;
    const auto source=boardplacement::WorldPoint(frame.anchors[ankle==20?22:18],frame,{0,0,0},0,fit);
    ankle=Joint(body,ankle);
    // Match the Source foot's bind-ground offset rather than treating its ankle
    // as the sole. The WoW foot keeps its own orientation and mesh proportions.
    const float offset=skeleton.bones[ankle].pivot.z-SourceAnkleHeight*fit.scale/boardplacement::MetresPerYard;
    out=Sub(Add(Model(source,facing),Rotate(body.global[ankle],{0,0,offset})),shift);
    return characterrig::Finite(out) && Length(out)<6;
}
inline bool Place(const characterrig::Snapshot& skeleton,Pose& pose,unsigned ankle,Point target,bool& clamped) {
    const unsigned knee=Joint(pose,ankle==20?14:15),hip=Joint(pose,ankle==20?7:8);ankle=Joint(pose,ankle);
    const auto a=pose.pivots[hip],oldKnee=pose.pivots[knee],oldFoot=pose.pivots[ankle];
    const float upper=Length(Sub(skeleton.bones[knee].pivot,skeleton.bones[hip].pivot));
    const float lower=Length(Sub(skeleton.bones[ankle].pivot,skeleton.bones[knee].pivot));
    Point direction{};
    const float requested=Length(Sub(target,a));
    if (!Unit(Sub(target,a),direction)) return false;
    const float distance=(std::max)(fabsf(upper-lower)+.0001f,(std::min)(upper+lower-.0001f,requested));
    clamped=fabsf(distance-requested)>.00002f;
    const auto goal=Add(a,Scale(direction,distance));
    Point pole=Sub(oldKnee,a),bend{};
    pole=Sub(pole,Scale(direction,Dot(pole,direction)));
    if (!Unit(pole,bend)) {
        pole=Rotate(pose.global[hip],{1,0,0});pole=Sub(pole,Scale(direction,Dot(pole,direction)));
        if (!Unit(pole,bend)) return false;
    }
    const float along=(upper*upper-lower*lower+distance*distance)/(2*distance);
    const auto newKnee=Add(a,Add(Scale(direction,along),Scale(bend,sqrtf((std::max)(0.f,upper*upper-along*along)))));
    Quaternion upperSwing{},lowerSwing{};
    if (!Align(Sub(oldKnee,a),Sub(newKnee,a),upperSwing) || !Align(Sub(oldFoot,oldKnee),Sub(goal,newKnee),lowerSwing)) return false;
    const auto upperGlobal=Multiply(upperSwing,pose.global[hip]),lowerGlobal=Multiply(lowerSwing,pose.global[knee]);
    const auto footGlobal=pose.global[ankle];
    pose.local[hip]=Multiply(Inverse(pose.global[static_cast<unsigned>(skeleton.bones[hip].parent)]),upperGlobal);
    pose.local[knee]=Multiply(Inverse(upperGlobal),lowerGlobal);
    pose.local[ankle]=Multiply(Inverse(lowerGlobal),footGlobal);
    charactercontact::Rebuild(skeleton,pose);
    return Length(Sub(pose.pivots[ankle],goal))<.0001f;
}
inline Result Solve(const characterrig::Snapshot& skeleton,Pose& body,
                    const skatevisual::Frame& frame,const boardplacement::Fit& fit,float facing,Point shift) {
    Result out{};Point targets[2]{};
    if (!Target(skeleton,body,frame,fit,facing,shift,20,targets[0])
        || !Target(skeleton,body,frame,fit,facing,shift,22,targets[1])) return out;
    Pose corrected=body;
    for (unsigned i=0;i<2;++i) {
        const unsigned ankle=i?22:20;
        const float before=Length(Sub(body.pivots[Joint(body,ankle)],targets[i]));
        // Large/malformed targets leave the accepted pose unchanged. A visual
        // refusal must never become a movement pause or release the current pose.
        if (!std::isfinite(before) || before>.35f) return {};
        out.before=(std::max)(out.before,before);
        bool clamped=false;
        if (!Place(skeleton,corrected,ankle,targets[i],clamped)) return {};
        out.clamped+=clamped?1u:0u;
        const float after=Length(Sub(corrected.pivots[Joint(body,ankle)],targets[i]));
        if (!std::isfinite(after) || after>before+.0001f) return {};
        out.after=(std::max)(out.after,after);
        out.maxChange=(std::max)(out.maxChange,Length(Sub(corrected.pivots[Joint(body,ankle)],body.pivots[Joint(body,ankle)])));
    }
    body=corrected;out.valid=true;return out;
}
}
