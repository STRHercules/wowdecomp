#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "ground-tuning.h"
#include "local-player.h"

// Integration boundary: real Skate velocity -> stock Wrath horizontal run request.
// No artificial W acceleration and no direct position/teleport writes. Native
// speed/control ownership and moving collision must be connected separately.
namespace skatemotion {
struct Request { bool valid=false; float speed=0,heading=0; };
struct Velocity {float x=0,z=0;};
inline Velocity TransportVelocity(float publishedX,float publishedZ,float rootX,float rootY,
    bool pairedRoot,bool onFoot,bool recovering,bool transportAir){
    // Vanilla present() uses frame.velocity for riding speed/yaw, independently
    // of frame.root position. A root derivative contains animation/quantization
    // offsets and must not become a competing ground-facing authority.
    const bool rootCarrier=pairedRoot && (onFoot || recovering || transportAir);
    return rootCarrier?Velocity{rootX*.9144f,-rootY*.9144f}:Velocity{publishedX,publishedZ};
}
inline bool OwnBodyHit(uint64_t guid,float fraction,uint64_t owner) {
    return localplayer::Owns(guid,owner) && std::isfinite(fraction) && fraction>=0 && fraction<=1;
}
inline bool GroundContactWait(uint32_t flags,bool spline,bool lease,float gap,uint32_t age) {
    // Wrath: FAR_FALL=2000, PENDING_STOP=4000 (NOT the Vanilla far-fall bit).
    const uint32_t forbidden=0x100u|0x200u|0x400u|0x800u|0x2000u|0x100000u|0x200000u|0x1000000u|0x2000000u;
    return lease && !spline && (flags&0x1000u) && !(flags&forbidden)
        && std::isfinite(gap) && fabsf(gap)<=.125f && age<250;
}
inline bool PatchContains(float x,float y,float centreX,float centreY) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(centreX) && std::isfinite(centreY)
        && fabsf(x-centreX)<18.f && fabsf(y-centreY)<18.f;
}
inline bool Coherent(float actualX,float actualY,float engineX,float engineZ,float originX,float originY) {
    const float x=originX+engineX/.9144f,y=originY-engineZ/.9144f;
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(actualX) && std::isfinite(actualY)
        && hypotf(actualX-x,actualY-y)<3.f;
}
inline Request Ground(float engineX,float engineZ,bool ground,bool airborne,
    uint32_t epoch,uint32_t expectedEpoch,uint32_t now,uint32_t publishedAt,float serverRunLimit,bool ownedOllie=false) {
    Request out{};
    if (!epoch || epoch!=expectedEpoch || now-publishedAt>=150 || ((!ground || airborne) && !ownedOllie)
        || !std::isfinite(engineX) || !std::isfinite(engineZ)
        || fabsf(engineX)>(std::max)(50.f,groundtuning::Maximum()*.9144f) || fabsf(engineZ)>(std::max)(50.f,groundtuning::Maximum()*.9144f)
        || !std::isfinite(serverRunLimit) || serverRunLimit<.1f || serverRunLimit>(std::max)(30.f,groundtuning::Maximum())) return out;
    const float x=engineX/.9144f,y=-engineZ/.9144f;
    const float physical=hypotf(x,y);
    if(groundtuning::NaturalMomentum && physical>(std::min)(serverRunLimit,groundtuning::Maximum()))return out;
    out.speed=(std::min)(physical,(std::min)(serverRunLimit,groundtuning::Maximum()));
    // Fresh native activation has a small settling velocity before a push.
    // Do not turn that sub-0.15yard/s numerical wobble into player movement.
    if (out.speed<.15f) out.speed=0;
    out.heading=out.speed?atan2f(y,x):0;
    out.valid=true; return out;
}
inline Request Synchronize(Request in,float actualX,float actualY,float engineX,float engineZ,
    float originX,float originY,float serverRunLimit) {
    if (!in.valid || !Coherent(actualX,actualY,engineX,engineZ,originX,originY)) return {};
    if (in.speed<.3f) return in; // No creeping to chase animation/settling offsets.
    float correctionX=(originX+engineX/.9144f-actualX)*1.25f;
    float correctionY=(originY-engineZ/.9144f-actualY)*1.25f;
    const float correction=hypotf(correctionX,correctionY);
    if (correction>.5f) { correctionX*=.5f/correction; correctionY*=.5f/correction; }
    // Catch up along travel only. Lateral alignment error must not steer the
    // actor (and WoW's following camera) toward a different direction.
    const float along=cosf(in.heading)*correctionX+sinf(in.heading)*correctionY;
    in.speed=(std::max)(0.f,(std::min)(in.speed+along,(std::min)(serverRunLimit,groundtuning::Maximum())));
    return in;
}
}
