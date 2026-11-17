#pragma once
#include "skate-visual.h"

namespace boardplacement {
constexpr float MetresPerYard=.9144f;
struct Fit {
    bool valid=false; float scale=1,x=0,y=0,yaw=0;
    bool rootRelative=false;
    float sourceBottomFromRoot=0;
    bool physical=false;
    skatevisual::Point sourceOrigin{},sourceCentre{};
    float z=0; // Measured ground-contact presentation offset; never physics.
    float modelScale=1; // Native rendered model size; not the engine/body scale.
};
inline bool Valid(const Fit& fit) {
    const auto o=fit.sourceOrigin,c=fit.sourceCentre;
    const bool physicalValid=!fit.physical || (std::isfinite(o.x) && std::isfinite(o.y) && std::isfinite(o.z)
        && std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z)
        && fabsf(o.x+c.x/MetresPerYard)<20000 && fabsf(o.y-c.z/MetresPerYard)<20000
        && fabsf(o.z+c.y/MetresPerYard)<5000 && fabsf(c.x)<10000 && fabsf(c.y)<10000 && fabsf(c.z)<10000);
    return physicalValid && fit.valid && std::isfinite(fit.scale) && fit.scale>=.7f && fit.scale<=1.4f
        && std::isfinite(fit.modelScale) && fit.modelScale>=.01f && fit.modelScale<=64
        && std::isfinite(fit.x) && std::isfinite(fit.y) && hypotf(fit.x,fit.y)<=.75f
        && std::isfinite(fit.yaw) && fabsf(fit.yaw)<=6.283186f
        && std::isfinite(fit.z) && fabsf(fit.z)<=.75f
        && std::isfinite(fit.sourceBottomFromRoot) && fabsf(fit.sourceBottomFromRoot)<=5;
}
inline skatevisual::Point WorldPoint(skatevisual::Point point,const skatevisual::Frame& frame,
                                    skatevisual::Point player,float bottom,const Fit& fit={}) {
    const bool aligned=Valid(fit);
    const float scale=aligned?fit.scale*fit.modelScale:1;
    if(aligned && fit.physical) {
        const auto o=fit.sourceOrigin,c=fit.sourceCentre;
        // Preserve actual board-centre travel. Uniform mesh sizing is around
        // the detached board itself, never around the falling rider or floor.
        return {o.x+(c.x+scale*(point.x-c.x))/MetresPerYard,
            o.y-(c.z+scale*(point.z-c.z))/MetresPerYard,
            o.z+(c.y+scale*(point.y-c.y))/MetresPerYard};
    }
    if (aligned && fit.rootRelative) bottom=frame.pose.position[1]+fit.sourceBottomFromRoot;
    // One UNIFORM scale for deck, trucks and wheels. No anisotropic stretching.
    // Bottom remains on sampled ground; XY is a bounded visual-only stance fit.
    const float x=scale*(point.x-frame.pose.position[0])/MetresPerYard+(aligned?fit.x*fit.modelScale:0);
    const float y=-scale*(point.z-frame.pose.position[2])/MetresPerYard+(aligned?fit.y*fit.modelScale:0);
    const float c=aligned?cosf(fit.yaw):1,s=aligned?sinf(fit.yaw):0;
    return {player.x+c*x-s*y,player.y+s*x+c*y,
        player.z+scale*(point.y-bottom)/MetresPerYard+(aligned?fit.z*fit.modelScale:0)};
}
}
