#pragma once
#include "character-foot-contact.h"
#include <initializer_list>
namespace riderfeet {
using namespace characterretarget;
struct Result {
    bool valid=false; unsigned clamped=0,clearance=0;
    bool upright=false;
    float before=0,after=0,penetration=0,previousPenetration=0;
};
inline Point Model(skatevisual::Point world,float facing) {
    const float c=cosf(facing),s=sinf(facing);
    return {c*world.x+s*world.y,-s*world.x+c*world.y,world.z};
}
inline bool Source(const skatevisual::Frame& frame,const boardplacement::Fit& fit,unsigned source,float facing,Point& out) {
    if (source>=24 || !boardplacement::Valid(fit) || !std::isfinite(facing) || frame.vertices.empty()) return false;
    out=Model(boardplacement::WorldPoint(frame.anchors[source],frame,{0,0,0},0,fit),facing);
    return characterrig::Finite(out) && Length(out)<6;
}
// Air-only two-bone target follows the Source ankle, not a deck/ground point.
// Reach clamping preserves native limb lengths and the animated knee pole.
inline bool Place(const characterrig::Snapshot& skeleton,Pose& pose,unsigned ankle,Point target,bool& clamped) {
    const unsigned knee=ankle==20?14:15,hip=ankle==20?7:8;
    const auto a=pose.pivots[hip],oldKnee=pose.pivots[knee],oldFoot=pose.pivots[ankle];
    const float upper=Length(Sub(skeleton.bones[knee].pivot,skeleton.bones[hip].pivot));
    const float lower=Length(Sub(skeleton.bones[ankle].pivot,skeleton.bones[knee].pivot));
    Point direction{};
    const float requested=Length(Sub(target,a));
    if (!characterrig::Finite(target) || !Unit(Sub(target,a),direction)) return false;
    const float distance=(std::max)(fabsf(upper-lower)+.0001f,(std::min)(upper+lower-.0001f,requested));
    clamped=fabsf(distance-requested)>.00002f;
    const auto goal=Add(a,Scale(direction,distance));
    Point pole=Sub(oldKnee,a),bend{};
    pole=Sub(pole,Scale(direction,Dot(pole,direction)));
    if (!Unit(pole,bend)) {
        pole=Rotate(pose.global[hip],{1,0,0}); pole=Sub(pole,Scale(direction,Dot(pole,direction)));
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
inline Point Normal(const skatevisual::Frame& frame,const uint32_t* triangle) {
    for (unsigned i=0;i<3;++i) if (triangle[i]>=frame.vertices.size()) return {};
    const auto a=frame.vertices[triangle[0]],b=frame.vertices[triangle[1]],c=frame.vertices[triangle[2]];
    return Cross({b.x-a.x,b.y-a.y,b.z-a.z},{c.x-a.x,c.y-a.y,c.z-a.z});
}
inline float Penetration(const characterrig::Snapshot& skeleton,const Pose& body,
    const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,const boardplacement::Fit& fit,float facing,Point shift,unsigned ankle) {
    float worst=0;
    for (float along:{-.4f,0.f,1.f}) {
        const auto sole=characteralignment::WorldDirection(Add(characteralignment::SoleSample(skeleton,body,ankle,along),shift),facing);
        float deck=0;
        if (characteralignment::DeckSurface(mesh,frame,0,fit,sole,deck)) worst=(std::max)(worst,deck+characteralignment::SoleClearance-sole.z);
    }
    return worst;
}
struct Anchor {
    bool ready=false; Point offsets[2]{}; uint32_t triangle[3]{}; float upSign=0;
    void Clear() { *this={}; }
    bool Capture(const characteralignment::Result& fit,const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,const Pose& body,float facing,float lift) {
        if (!fit.valid || !frame.pose.ground || frame.pose.airborne || body.count!=138 || !std::isfinite(lift)) return false;
        // Ground fit uses the actual lowest board point, not a root-relative datum yet.
        float bottom=INFINITY; for (auto p:frame.vertices) bottom=(std::min)(bottom,p.y);
        for (unsigned i=0;i<2;++i) {
            const unsigned ankle=i?22:20,source=i?18:22;
            const auto world=boardplacement::WorldPoint(frame.anchors[source],frame,{0,0,0},bottom,fit.board);
            const auto delta=Sub(Add(body.pivots[ankle],{0,0,lift}),Model(world,facing));
            offsets[i]=Rotate(Inverse(body.global[ankle]),delta);
            if (!characterrig::Finite(offsets[i]) || Length(offsets[i])>1) { Clear(); return false; }
        }
        float largest=0;
        for (size_t i=0;i+2<mesh.indices.size();i+=3) {
            const uint32_t ids[]={mesh.indices[i],mesh.indices[i+1],mesh.indices[i+2]};
            if (ids[0]>=mesh.colors.size() || ids[1]>=mesh.colors.size() || ids[2]>=mesh.colors.size()) return false;
            if (mesh.colors[ids[0]]!=0xff32859d || mesh.colors[ids[1]]!=0xff32859d || mesh.colors[ids[2]]!=0xff32859d) continue;
            const auto n=Normal(frame,ids); const float area=Length(n);
            if (area>largest && fabsf(n.y)>.05f*area) {
                largest=area; for (unsigned j=0;j<3;++j) triangle[j]=ids[j];
                upSign=n.y>=0?1.f:-1.f;
            }
        }
        ready=largest>.00001f; return ready;
    }
    Result Follow(const characterrig::Snapshot& skeleton,Pose& body,const skatevisual::Mesh& mesh,
        const skatevisual::Frame& frame,const boardplacement::Fit& fit,float facing,Point shift) const {
        Result out{};
        if (!ready || !Supported(skeleton) || body.count!=138 || !fit.rootRelative || !characterrig::Finite(shift)) return out;
        Pose corrected=body;
        const auto normal=Normal(frame,triangle);
        const bool upright=Length(normal)>.00001f && normal.y*upSign/Length(normal)>.8f;
        out.upright=upright;
        for (unsigned i=0;i<2;++i) {
            const unsigned ankle=i?22:20,source=i?18:22;
            Point foot{}; if (!Source(frame,fit,source,facing,foot)) return {};
            const auto target=Sub(Add(foot,Rotate(corrected.global[ankle],offsets[i])),shift);
            out.before=(std::max)(out.before,Length(Sub(corrected.pivots[ankle],target)));
            bool clamped=false;
            if (!Place(skeleton,corrected,ankle,target,clamped)) return {};
            if (clamped) ++out.clamped;
            // Only resolve real penetration on an upright catch/contact. Never
            // pull feet down or horizontally onto a board during a flip.
            const auto sourceWorld=characteralignment::WorldDirection(foot,facing);
            float sourceDeck=0;
            if (upright && characteralignment::DeckSurface(mesh,frame,0,fit,sourceWorld,sourceDeck)
                && sourceWorld.z>=sourceDeck-.02f && sourceWorld.z-sourceDeck<.2f) {
                out.previousPenetration=(std::max)(out.previousPenetration,Penetration(skeleton,body,mesh,frame,fit,facing,shift,ankle));
                const float push=Penetration(skeleton,corrected,mesh,frame,fit,facing,shift,ankle);
                if (push>.0001f) {
                    const auto raised=Add(corrected.pivots[ankle],{0,0,push});
                    if (push>.75f || !Place(skeleton,corrected,ankle,raised,clamped)) return {};
                    ++out.clearance;
                }
                out.penetration=(std::max)(out.penetration,Penetration(skeleton,corrected,mesh,frame,fit,facing,shift,ankle));
            }
            out.after=(std::max)(out.after,Length(Sub(corrected.pivots[ankle],target)));
        }
        body=corrected; out.valid=true; return out;
    }
};
}
