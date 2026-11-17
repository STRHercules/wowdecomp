#pragma once
#include <cmath>
#include <cstdint>
namespace camerafollow {
constexpr float Pi=3.14159265359f,Turn=2*Pi,FollowRate=2.5f,MovingSpeed=.5f;
inline float Delta(float to,float from) { return std::remainder(to-from,Turn); }
inline bool Step(float target,float current,float seconds,float speed,bool looking,float& delta) {
    delta=0;
    if (!std::isfinite(target) || !std::isfinite(current) || !std::isfinite(seconds)
        || !std::isfinite(speed) || seconds<0 || seconds>.1f || speed<0 || speed>55) return false;
    // Exact default Vanilla follow law; stationary and held mouse retain yaw.
    if (looking || speed<=MovingSpeed) return true;
    delta=Delta(target,current)*(-std::expm1(-FollowRate*seconds));
    return std::isfinite(delta);
}
inline bool Composed(float rendered,float facing,float relative) {
    return std::isfinite(rendered) && std::isfinite(facing) && std::isfinite(relative)
        && fabsf(Delta(rendered,facing+relative))<=.10f;
}
struct WorldYaw {
    bool active=false,wasLooking=false;
    float yaw=0;
    void Clear() { *this={}; }
    bool Step(float observedWorld,float appliedFacing,float currentRelative,float target,
              float seconds,float speed,bool looking,float& nativeDelta) {
        nativeDelta=0;
        float check=0;
        if (!camerafollow::Step(target,observedWorld,seconds,speed,looking,check)
            || !std::isfinite(appliedFacing) || !std::isfinite(currentRelative)) { Clear(); return false; }
        // World yaw is independent of the body. A mouse drag/release adopts
        // the actual view; a model turn must never reseed our held world yaw.
        if (!active || looking || wasLooking) yaw=std::remainder(observedWorld,Turn);
        active=true; wasLooking=looking;
        if (looking) return true; // No camera writes while either mouse owns it.
        float eased=0;
        if (!camerafollow::Step(target,yaw,seconds,speed,false,eased)) { Clear(); return false; }
        yaw=std::remainder(yaw+eased,Turn);
        const float desiredRelative=Delta(yaw,appliedFacing);
        nativeDelta=Delta(desiredRelative,currentRelative);
        return std::isfinite(nativeDelta);
    }
};
struct Clock {
    bool active=false; uint32_t at=0;
    void Clear() { *this={}; }
    float Step(uint32_t now) {
        const float seconds=active && now-at<=100?(now-at)*.001f:0;
        active=true; at=now; return seconds;
    }
};
// Follow the actual composed yaw at the native view-build phase. Unlike a
// late render callback, this sees facing events AFTER the mover applied them.
struct ViewYaw {
    bool active=false,wasLooking=false;
    float yaw=0;
    void Clear() { *this={}; }
    bool Step(float composedWorld,float target,float seconds,float speed,bool looking,
              float& desiredWorld,float& relativeDelta) {
        desiredWorld=composedWorld; relativeDelta=0;
        float eased=0;
        if (!camerafollow::Step(target,composedWorld,seconds,speed,looking,eased)) { Clear(); return false; }
        if (!active || looking || wasLooking) yaw=std::remainder(composedWorld,Turn);
        active=true; wasLooking=looking;
        if (looking) return true;
        if (!camerafollow::Step(target,yaw,seconds,speed,false,eased)) { Clear(); return false; }
        yaw=std::remainder(yaw+eased,Turn);
        desiredWorld=yaw; relativeDelta=Delta(desiredWorld,composedWorld);
        return std::isfinite(relativeDelta);
    }
};
inline void Carry(ViewYaw& view,float delta){
    if(view.active && std::isfinite(delta))view.yaw=std::remainder(view.yaw+delta,Turn);
}
}
