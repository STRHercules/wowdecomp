#pragma once
#include "bail-recovery.h"
#include "vanilla-root.h"
#include "air-root-motion.h"
namespace ragdollroothandoff {
// Wipeout's original Skeleton::UpdateWipeout reconstructs animation_to_world
// from the physical hips, replacing the riding root. Its first displacement
// includes that anchor change; it is not a one-tick body-velocity measurement.
// Rebase only the two derivative histories. Actual Source target coordinates,
// the native motor, collision sweeps and following ordinary samples are intact.
inline bool Rebase(const bailrecovery::Phase& before,const bailrecovery::Phase& after,
    const bailrecovery::Metadata& metadata,const skatewire::Pose& pose,unsigned source,
    uint32_t epoch,uint32_t now,uint32_t publishedAt,const airroot::Point& root,
    const airroot::Point& native,vanillaroot::History& vertical,airroot::History& horizontal) {
    if (!epoch || source!=300 || pose.ground!=0 || pose.airborne!=0 || !metadata.active
        || !metadata.cycle || before.cycle==UINT32_MAX || metadata.cycle!=before.cycle+1
        || before.epoch!=epoch || after.epoch!=epoch || !after.active
        || after.cycle!=metadata.cycle || after.tick!=metadata.tick || pose.tick!=metadata.tick
        || metadata.checkpoint!=before.checkpoint || after.checkpoint!=before.checkpoint
        || now-publishedAt>=150 || !std::isfinite(pose.period) || pose.period<.001f || pose.period>.05f
        || !vertical.valid || !horizontal.valid || vertical.epoch!=epoch || horizontal.epoch!=epoch
        || vertical.tick!=horizontal.tick || before.tick!=vertical.tick || pose.tick<=vertical.tick
        || pose.tick-vertical.tick>9 || now-vertical.at>=150 || now-horizontal.at>=150
        || !std::isfinite(root.x) || !std::isfinite(root.y) || !std::isfinite(root.z)
        || fabsf(root.x)>50000 || fabsf(root.y)>50000 || fabsf(root.z)>5000
        || !std::isfinite(native.x) || !std::isfinite(native.y) || !std::isfinite(native.z)
        || !std::isfinite(vertical.velocity) || fabsf(vertical.velocity)>55
        || !std::isfinite(horizontal.vx) || !std::isfinite(horizontal.vy)
        || hypotf(horizontal.vx,horizontal.vy)>groundtuning::Maximum()) return false;
    const float seconds=static_cast<float>(pose.tick-vertical.tick)*pose.period;
    const float dx=root.x-horizontal.x,dy=root.y-horizontal.y,dz=root.z-vertical.height;
    const float nx=root.x-native.x,ny=root.y-native.y,nz=root.z-native.z;
    const float ax=dx-horizontal.vx*seconds,ay=dy-horizontal.vy*seconds,az=dz-vertical.velocity*seconds;
    // Existing native3yd coherence is not widened. A separate1.5yd residual
    // bounds the phase-anchor change after subtracting preceding actual travel.
    if (!std::isfinite(dx+dy+dz+nx+ny+nz+ax+ay+az)
        || dx*dx+dy*dy+dz*dz>=9 || nx*nx+ny*ny+nz*nz>=9
        || ax*ax+ay*ay+az*az>2.25f) return false;
    vertical.tick=pose.tick;vertical.at=publishedAt;vertical.height=root.z;
    horizontal.tick=pose.tick;horizontal.at=publishedAt;horizontal.x=root.x;horizontal.y=root.y;
    return true; // Retain measured pre-transition rates, not zero or a new impulse.
}
}
