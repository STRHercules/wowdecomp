#pragma once
#include "skate-ground-motion.h"
namespace recoverytransport {
// Native forward dispatch must keep running to reach the existing Source-root
// fall/sweep hooks. This carrier is NOT displacement: those hooks supply exact
// bounded XY, including zero travel, before native collision commits position.
constexpr float MinimumCarrier=.3f;
inline skatemotion::Request Plan(skatemotion::Request source,bool recovering,bool ownedAir,
    bool pairedRoot,float facing,float allowance,bool nativeFalling=true){
    if(!recovering)return source;
    if(!source.valid || !std::isfinite(source.speed) || source.speed<0 || source.speed>groundtuning::Maximum()
        || !std::isfinite(facing) || !std::isfinite(allowance) || allowance<.1f || allowance>(std::max)(30.f,groundtuning::Maximum()))return {};
    // Native grounded dispatch follows actual root travel. Keeping old facing
    // here drives the capsule AWAY after a collision reverses the ragdoll root.
    if(nativeFalling || source.speed<MinimumCarrier)source.heading=facing;
    if(!ownedAir){source.speed=0;return source;} // no ground propulsion while handing back
    if(!pairedRoot)return {};
    source.speed=(std::min)((std::max)(source.speed,MinimumCarrier),(std::min)(allowance,groundtuning::Maximum()));
    return source;
}
// A stationary off-board jump needs the same dispatch carrier, but must not
// inherit the bail's facing lock or ground stop. The paired root still owns XY.
inline skatemotion::Request OnFoot(skatemotion::Request source,bool onFoot,bool ownedAir,
    bool pairedRoot,float facing,float allowance){
    if(!onFoot || !ownedAir)return source;
    const float travelHeading=source.heading;
    auto result=Plan(source,true,true,pairedRoot,facing,allowance);
    if(result.valid && source.speed>=MinimumCarrier)result.heading=travelHeading;
    return result;
}
}
