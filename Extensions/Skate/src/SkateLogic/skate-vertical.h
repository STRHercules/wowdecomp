#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace skatevertical {
// Takeoff impulse is source physics velocity. Thereafter one continuous arc
// owns the actor, not the deck's animated drive/collision-correction velocities.
// Use the audited stock Wrath gravity locally; do not alter source/global gravity.
constexpr float ArcGravity=19.291105f;
struct Segment {
    bool valid=false;
    float z=0,velocity=0,acceleration=0,startSeconds=0,span=0;
    float Height(float seconds) const { return z+velocity*seconds+.5f*acceleration*seconds*seconds; }
    bool Contact(float target,float& absoluteSeconds) const {
        if (!valid || !std::isfinite(target) || !std::isfinite(z) || !std::isfinite(startSeconds)
            || !std::isfinite(velocity) || !std::isfinite(acceleration)
            || !std::isfinite(span) || span<0 || span>.1001f) return false;
        const float end=Height(span);
        if (!std::isfinite(end)) return false;
        // World-height floats lose sub-ULP travel near a resting floor. A
        // 5ms segment at -0.00366yd/s is shorter than one ULP at Ironforge.
        // Accept only representational endpoint error, not a larger step or
        // an arbitrary floor outside the sweep. Use the earliest such contact.
        const float ulp=(std::max)(fabsf(std::nextafter(z,INFINITY)-z),
            (std::max)(fabsf(std::nextafter(end,INFINITY)-end),fabsf(std::nextafter(target,INFINITY)-target)));
        const float endpointError=(std::max)(1.e-7f,2*ulp);
        if (fabsf(target-z)<=endpointError) { absoluteSeconds=startSeconds; return true; }
        float roots[2]={NAN,NAN};
        if (fabsf(acceleration)<.001f) {
            if (fabsf(velocity)<.001f) {
                if (fabsf(target-z)>.002f) return false;
                absoluteSeconds=startSeconds; return true;
            }
            roots[0]=(target-z)/velocity;
        } else {
            const float discriminant=velocity*velocity+2*acceleration*(target-z);
            if (!std::isfinite(discriminant) || discriminant<0) return false;
            const float r=sqrtf(discriminant);
            // Explicitly bounded divisor also keeps MSVC's constant-propagated
            // x87 path from warning about an unreachable zero-acceleration arm.
            const float divisor=std::copysign((std::max)(.001f,fabsf(acceleration)),acceleration);
            roots[0]=(-velocity+r)/divisor; roots[1]=(-velocity-r)/divisor;
        }
        float first=INFINITY;
        for (float t:roots) if (std::isfinite(t) && t>=-.0005f && t<=span+.002f) first=(std::min)(first,(std::max)(0.f,t));
        if (!std::isfinite(first)) {
            if (fabsf(target-end)>endpointError) return false;
            absoluteSeconds=startSeconds+span; return std::isfinite(absoluteSeconds);
        }
        absoluteSeconds=startSeconds+first; return std::isfinite(absoluteSeconds);
    }
};
struct Motor {
    bool active=false,settling=false,rootDriven=false,stepHeld=false;
    uint32_t epoch=0,at=0,nativeSampleMs=0;
    uint64_t tick=0;
    float velocity=0,acceleration=-ArcGravity,period=0,launchVelocity=0,arcSeconds=0;
    float rootHeight=0,rootVelocity=0;
    Segment segment{};
    void Clear() { *this={}; }
    bool Advance(uint32_t nativeMs) {
        if (!active || nativeMs<nativeSampleMs || (!rootDriven && nativeMs-nativeSampleMs>150)) return false;
        // The root-driven path follows fresh measured Source position, not a
        // native elapsed-time ballistic arc. Never integrate a delayed clock
        // catch-up as extra movement or invalidate a fresh ragdoll for it.
        if(nativeMs-nativeSampleMs<=150)arcSeconds+=(nativeMs-nativeSampleMs)*.001f;
        nativeSampleMs=nativeMs;
        if (!rootDriven) velocity=launchVelocity-ArcGravity*arcSeconds;
        return std::isfinite(velocity) && std::isfinite(arcSeconds) && (rootDriven || arcSeconds<=8.f);
    }
    bool Rebase(uint32_t oldNativeMs,uint32_t newNativeMs) {
        if (!Advance(oldNativeMs)) return false;
        nativeSampleMs=newNativeMs; segment={}; return true;
    }
    bool SweepRestart(uint32_t now,uint32_t currentEpoch,bool scoped,uint32_t flags,
        uint32_t nativeMs,float baseZ,float z,float nativeDown) {
        // Owned12340 sweep7617B6 resets80/B8/84 inline, without988370.
        // Require its complete reset signature inside that SAME swept call.
        if(!scoped || !rootDriven || !Fresh(now,currentEpoch) || (flags&0x3000u)!=0x3000u
            || nativeMs!=0 || !nativeSampleMs
            || !std::isfinite(baseZ) || !std::isfinite(z) || nativeDown!=0.f
            || fabsf(baseZ-z)>2.f*fabsf(std::nextafter(z,INFINITY)-z))return false;
        nativeSampleMs=0;segment={};return true; // No new Source sample/age/impulse.
    }
    bool Sample(uint32_t currentEpoch,uint32_t now,uint64_t sourceTick,float secondsPerTick,
                float verticalMeters,bool ground,uint32_t nativeMs) {
        if (!currentEpoch || !std::isfinite(verticalMeters) || fabsf(verticalMeters)>50
            || !std::isfinite(secondsPerTick) || secondsPerTick<.001f || secondsPerTick>.05f) return false;
        if (active && (epoch!=currentEpoch || sourceTick<tick || now-at>=150)) return false;
        if (active && !Advance(nativeMs)) return false;
        if (active && sourceTick==tick) return true; // Never refresh stale data's age.
        if (!active) {
            // One launch only. Ground/animation corrections after this point
            // refresh source ownership but cannot suspend/relaunch the body.
            launchVelocity=velocity=verticalMeters/.9144f;
            arcSeconds=0;
        }
        acceleration=-ArcGravity;
        active=true; settling=ground; epoch=currentEpoch; at=now; tick=sourceTick;
        nativeSampleMs=nativeMs; period=secondsPerTick; segment={}; return true;
    }
    bool Fresh(uint32_t now,uint32_t currentEpoch) const { return active && epoch==currentEpoch && now-at<150; }
    bool Waiting(uint32_t now,uint32_t currentEpoch) const {
        return active && rootDriven && epoch==currentEpoch && now-at>=150;
    }
    bool Hold(uint32_t now,uint32_t currentEpoch,uint32_t nativeMs,float z,float& down){
        if(!Waiting(now,currentEpoch) || !std::isfinite(z) || fabsf(z)>5000)return false;
        segment={true,z,0,0,nativeMs*.001f,0};stepHeld=true;down=0;return true;
    }
    bool HoldOversized(uint32_t now,uint32_t currentEpoch,uint32_t nativeMs,uint32_t requestedMs,float z,float& down){
        if(!rootDriven || !Fresh(now,currentEpoch) || requestedMs<nativeMs || requestedMs-nativeMs<=100
            || !std::isfinite(z) || fabsf(z)>5000 || !Advance(nativeMs))return false;
        segment={true,z,0,0,nativeMs*.001f,0};stepHeld=true;down=0;return true;
    }
    bool RootSample(uint32_t currentEpoch,uint32_t now,uint64_t sourceTick,float secondsPerTick,
        float sourceWorldZ,float sourceRootVelocity,bool ground,uint32_t nativeMs,uint32_t publishedAt) {
        if (!currentEpoch || !sourceTick || now-publishedAt>=150 || !std::isfinite(sourceWorldZ) || fabsf(sourceWorldZ)>5000
            || !std::isfinite(sourceRootVelocity) || fabsf(sourceRootVelocity)>55
            || !std::isfinite(secondsPerTick) || secondsPerTick<.001f || secondsPerTick>.05f) return false;
        if (active) {
            if(!rootDriven || epoch!=currentEpoch || sourceTick<tick)return false;
            if(Waiting(now,currentEpoch)){
                // Never extrapolate an expired root. A distinct fresh paired
                // sample may renew this same owner without replaying lost time,
                // adding impulse, resetting Source or reviving a duplicate.
                if(sourceTick<=tick || sourceTick-tick>9 || nativeMs<nativeSampleMs)return false;
            }else if(!Advance(nativeMs))return false;
        }
        if (active && sourceTick==tick) return true;
        if (!active) { rootDriven=true; launchVelocity=sourceRootVelocity; arcSeconds=0; }
        active=true; epoch=currentEpoch; at=publishedAt; tick=sourceTick; period=secondsPerTick; settling=ground;
        rootHeight=sourceWorldZ; rootVelocity=sourceRootVelocity;
        velocity=sourceRootVelocity; acceleration=0; nativeSampleMs=nativeMs; segment={};stepHeld=false; return true;
    }
    bool Delta(uint32_t now,uint32_t currentEpoch,uint32_t nativeMs,uint32_t requestedMs,float z,float& down) {
        if (!Fresh(now,currentEpoch) || !std::isfinite(z) || fabsf(z)>5000
            || requestedMs<nativeMs || requestedMs-nativeMs>100 || nativeMs<nativeSampleMs
            || (!rootDriven && nativeMs-nativeSampleMs>150)) return false;
        if (!Advance(nativeMs)) return false;
        const float dt=(requestedMs-nativeMs)*.001f;
        if (rootDriven) {
            const float predicted=rootHeight+rootVelocity*((now-at)*.001f);
            const float error=predicted-z;
            if (!std::isfinite(error) || fabsf(error)>3.f) return false;
            // Follow the reference trajectory through stock swept collision.
            // Bounded tracking corrects publication lag; never assign coordinates.
            float correction=(std::max)(-3.f,(std::min)(3.f,error*12.f));
            // Vanilla presents the Source root directly. When its airborne
            // root is descending, publication lag must not make native fall
            // faster than that root to recover a small height discrepancy.
            // Brake an ahead-of-source body without reversing it upward. This
            // also applies to a descending root on Source's landing frame.
            // A stationary grounded Source root still permits bounded floor
            // settlement, so a small remaining native gap cannot hover.
            if (rootVelocity<0) correction=(std::min)(-rootVelocity,(std::max)(0.f,correction));
            velocity=rootVelocity+correction;
            // Source has already landed. Its rising root now describes rolling
            // uphill/ground animation, NOT another airborne climb. Always let
            // native swept contact settle before releasing flight ownership;
            // chasing a positive ground-root error can keep direction locked
            // for seconds while the Source rider is already steering on land.
            if (settling && rootVelocity>=0) velocity=(std::min)(-.25f,velocity);
            acceleration=0;
        }
        stepHeld=false;segment={true,z,velocity,acceleration,nativeMs*.001f,dt};
        down=z-segment.Height(dt);
        return std::isfinite(down) && fabsf(down)<=6.f;
    }
};
}
