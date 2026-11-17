#pragma once
#include "character-alignment.h"
namespace riderair {
using namespace characterretarget;
struct Result { bool valid=false; Point shift{}; float before=0,after=0; };
inline bool ShiftValid(Point shift) {
    return characterrig::Finite(shift) && Length(shift)<=2.f;
}
inline bool SourceHip(const skatevisual::Frame& frame,const boardplacement::Fit& fit,float facing,Point& hip) {
    if (!boardplacement::Valid(fit) || !std::isfinite(facing) || frame.vertices.empty()) return false;
    float bottom=INFINITY;
    for (auto point:frame.vertices) bottom=(std::min)(bottom,point.y);
    const auto world=boardplacement::WorldPoint(frame.anchors[0],frame,{0,0,0},bottom,fit);
    const float c=cosf(facing),s=sinf(facing);
    hip={c*world.x+s*world.y,-s*world.x+c*world.y,world.z};
    return characterrig::Finite(hip) && Length(hip)<=6;
}
// Match the same Source HIPS origin used by Vanilla's full skin mapping.
// Keep the Human Male's bind lengths and accepted grounded stance. Only the
// whole visual skeleton is translated; never move the entity or pin air feet.
struct Anchor {
    bool ready=false;
    Point residualLocal{};
    void Clear() { *this={}; }
    bool Capture(const characteralignment::Result& fit,const skatevisual::Frame& frame,
                 const Pose& body,float facing,float rootLift) {
        if (!fit.valid || !frame.pose.ground || frame.pose.airborne || !body.count || !std::isfinite(rootLift)) return false;
        Point hip{};
        if (!SourceHip(frame,fit.board,facing,hip)) return false;
        const auto residual=Rotate(Inverse(body.global[Joint(body,0)]),Sub(Add(body.pivots[Joint(body,3)],{0,0,rootLift}),hip));
        if (!ShiftValid(residual)) return false;
        residualLocal=residual; ready=true; return true;
    }
    Result Solve(const characteralignment::Result& fit,const skatevisual::Frame& frame,
                 const Pose& body,float facing) const {
        Result out{}; Point hip{};
        if (!ready || !fit.valid || !fit.board.rootRelative || !body.count || !SourceHip(frame,fit.board,facing,hip)) return out;
        const auto desired=Add(hip,Rotate(body.global[Joint(body,0)],residualLocal));
        out.shift=Sub(desired,body.pivots[Joint(body,3)]);
        if (!ShiftValid(out.shift)) return {};
        out.before=Length(Sub(desired,Add(body.pivots[Joint(body,3)],{0,0,fit.rootLift})));
        out.after=Length(Sub(desired,Add(body.pivots[Joint(body,3)],out.shift)));
        out.valid=std::isfinite(out.before) && out.after<.00001f; return out;
    }
};
}
