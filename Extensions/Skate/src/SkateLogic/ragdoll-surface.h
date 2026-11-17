#pragma once
#include "character-ragdoll.h"

// WoW bind lengths differ from the Source ragdoll's collision bodies. Constrain
// only the displayed body against measured world support, never the simulation,
// native capsule, board or airborne velocity.
namespace ragdollsurface {
using namespace characterretarget;
struct Result { bool valid=false;float lift=0,minGap=INFINITY;unsigned hits=0,unresolved=0; };
struct Support { unsigned bone;float radius; };
constexpr Support Samples[]={{3,.18f},{2,.20f},{4,.20f},{16,.16f},{14,.10f},{15,.10f},
    {20,.08f},{22,.08f},{32,.06f},{33,.06f},{30,.10f},{31,.10f},{36,.08f},{38,.08f}};
template<class Floor> inline Result Fit(const Pose& body,Point player,float facing,Point& shift,Floor floor){
    Result out{};
    // Player is WORLD space, not a model-local bone (+/-20yd).
    if(!body.count || !std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z)
        || fabsf(player.x)>=20000 || fabsf(player.y)>=20000 || fabsf(player.z)>=5000
        || !characterragdoll::RootShiftValid(shift) || !std::isfinite(facing))return out;
    const float c=cosf(facing),s=sinf(facing);
    for(const auto sample:Samples){
        if(!characterrig::Finite(body.pivots[Joint(body,sample.bone)]))return {};
        const auto local=Add(body.pivots[Joint(body,sample.bone)],shift);
        const Point p{player.x+c*local.x-s*local.y,player.y+s*local.x+c*local.y,player.z+local.z};
        float height=NAN;
        if(!floor(p,player.z,height))continue; // A missing floor is not invented support.
        if(!std::isfinite(height)){++out.unresolved;continue;}
        // Keep the clearance arithmetic local. Adding small body offsets to a
        // large world Z first loses enough float precision to leave penetration.
        const float gap=local.z-sample.radius-(height-player.z);
        if(gap< -1.5f || gap>1.75f){++out.unresolved;continue;}
        ++out.hits;out.minGap=(std::min)(out.minGap,gap);out.lift=(std::max)(out.lift,-gap);
    }
    const Point adjusted{shift.x,shift.y,shift.z+out.lift};
    if(!characterragdoll::RootShiftValid(adjusted))return out;
    shift=adjusted;out.valid=true;return out;
}
}
