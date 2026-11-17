#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>
#include "ground-tuning.h"
namespace ollie {
enum class Takeoff { Ground, Prepare, Rise, Refuse };
enum class Support { None, Contact, Ground };
inline Takeoff Classify(bool ground,bool air,float vertical,uint32_t age,bool pending,Support support) {
    if (!std::isfinite(vertical)) return Takeoff::Refuse;
    if (ground && !air) return Takeoff::Ground;
    if (!pending || age>=600 || support==Support::None) return Takeoff::Refuse;
    // World-axis velocity includes slope travel and suspension compression.
    // Only the owned engine phase plus measured native support authorizes pop.
    if (air && support==Support::Ground) return Takeoff::Rise;
    // Tiny native falling/contact transitions must settle before stock Jump.
    if (age<300 && (!air || support==Support::Contact)) return Takeoff::Prepare;
    return Takeoff::Refuse;
}
struct Launch {
    bool held=false;
    bool takeoffAligned=false;
    float heading=0,speed=0;
    uint32_t landedAt=0;
    void Clear() { *this={}; }
    bool Capture(float facing,float horizontal) {
        if (!std::isfinite(facing) || !std::isfinite(horizontal) || horizontal<0 || horizontal>groundtuning::Maximum()) { Clear(); return false; }
        held=true; takeoffAligned=false; landedAt=0; heading=std::remainder(facing,6.28318530718f); speed=horizontal<.3f?0:horizontal; return true;
    }
    bool AlignTakeoff(float sourceHeading) {
        if (!held || landedAt || takeoffAligned || !std::isfinite(sourceHeading)
            || !std::isfinite(speed) || speed<0 || speed>groundtuning::Maximum()) return false;
        // Gesture capture precedes Source takeoff. Finalize once, from the
        // accepted Source travel velocity, before submitting native facing/jump.
        // Do not follow transient deck headings throughout the flight.
        heading=std::remainder(sourceHeading,6.28318530718f);
        // Preserve accepted takeoff speed: a flip's first deck-velocity sample
        // includes pop/animation impulses and must not retune native propulsion.
        takeoffAligned=true; return true;
    }
    void Landed(uint32_t now) { landedAt=now; }
    bool Obstruct() {
        if (!held) return false;
        speed=0; return true; // Keep launch/vertical ownership; do not turn or land.
    }
    bool Holding(uint32_t now,bool pendingOrAir) const {
        return held && (pendingOrAir || (landedAt && now-landedAt<250));
    }
    bool Apply(float& facing,float& horizontal,uint32_t now=0) const {
        if (!held) return false;
        if (landedAt) {
            const float blend=(std::min)(1.f,(now-landedAt)/250.f);
            facing=std::remainder(heading+std::remainder(facing-heading,6.28318530718f)*blend,6.28318530718f);
            horizontal=speed+(horizontal-speed)*blend; return true;
        }
        facing=heading; horizontal=speed; return true;
    }
};
// The engine still makes the actual crouch/flick animation. This gate prevents
// forward-only input, held sticks, stale focus/session input and midair repeats.
struct Gesture {
    bool back=false,armed=false,neutralRequired=true;
    uint32_t backAt=0,releasedAt=0;
    void Clear() { *this={}; }
    bool Step(bool allowed,int16_t axis,uint32_t now) {
        return Step(allowed,0,axis,now);
    }
    bool Step(bool allowed,int16_t sideways,int16_t axis,uint32_t now) {
        if (!allowed) { Clear(); return false; }
        if (neutralRequired) { if (std::abs(static_cast<int>(axis))<8000 && std::abs(static_cast<int>(sideways))<8000) neutralRequired=false; return false; }
        if (axis<=-16000) {
            if (!back) { back=true; backAt=now; }
            if (now-backAt>=80) armed=true;
            releasedAt=now; return false;
        }
        back=false;
        if (armed && now-releasedAt>350) armed=false;
        if ((axis>=16000 || (std::abs(static_cast<int>(sideways))>=16000 && axis> -8000)) && armed) { Clear(); return true; }
        return false;
    }
};
struct Flight {
    bool active=false,sawNativeAir=false;
    uint32_t at=0,epoch=0,serial=0;
    float startZ=0;
    void Clear() { *this={}; }
    bool Begin(uint32_t now,uint32_t currentEpoch,uint32_t pop,float z) {
        if (active || !currentEpoch || !pop || !std::isfinite(z)) return false;
        active=true; at=now; epoch=currentEpoch; serial=pop; startZ=z; return true;
    }
    bool Check(uint32_t now,uint32_t currentEpoch,uint32_t flags,bool spline,float z,float terrainZ=NAN,bool sourceVertical=false,bool sourceAuthority=false) {
        const uint32_t forbidden=0x100u|0x200u|0x400u|0x800u|0x100000u|0x200000u|0x1000000u|0x2000000u;
        // A fresh coherent Source flight owns its lifetime, not wall-clock age.
        // Legacy diagnostic jumps without Source authority retain their bounds.
        const bool sourceOwned=sourceVertical && sourceAuthority;
        if (!active || epoch!=currentEpoch || (!sourceOwned && now-at>=(sourceVertical?8000u:1800u)) || spline || flags&forbidden
            || !std::isfinite(z) || (!sourceVertical && z-startZ>3.f)) return false;
        // A downhill landing is below takeoff. Permit only a bounded descent
        // with measured nearby support; missing terrain/deep drops still stop.
        if (!sourceVertical && z-startZ<-.75f && (!std::isfinite(terrainZ) || z-startZ< -4.f
            || z-terrainZ<-.125f || z-terrainZ>3.f)) return false;
        if (flags&0x1000u) sawNativeAir=true;
        // Only the fully owned/fresh Source-root adapter opts out of the
        // legacy native-flag acknowledgement timer. All other guards remain.
        return (sourceVertical && sourceAuthority) || sawNativeAir || now-at<150;
    }
    bool Landed(uint32_t now,uint32_t flags,bool engineGround,bool engineAir) const {
        return active && sawNativeAir && now-at>=150 && !(flags&(0x1000u|0x2000u)) && engineGround && !engineAir;
    }
};
}
