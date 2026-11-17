#pragma once
#include <cmath>
#include <cstdint>
#include "SkateLogic/skate-ground-motion.h"

// Phase 3: board pose -> what the local mover is told to do (run speed, facing, forward bit, jump).
// Pure; Skate.cpp applies the command through the stock movement calls, so the client sends its
// ordinary movement packets and native collision stays authoritative.
namespace skatedrive {
constexpr float Yards = .9144f;
constexpr float StockJump = -7.9555473f;  // CMovementShared::Jump on land, negative is up
constexpr uint32_t PopWindowMs = 600;     // a pop counts for an ollie if the board leaves the ground this soon
constexpr uint32_t StallMs = 250;         // pose tick unchanged this long: the helper is not stepping
struct JumpRequest { float velocity=0,rootY=0,vy=0; uint64_t tick=0; };
// TryJumpOrAscend queues MOVEEVENT_JUMP; ownership survives until the later accepted StartFalling callback.
class PendingJump {
    bool pending_=false;
    uint32_t at_=0;
    JumpRequest request_{};
public:
    void Clear() { pending_=false; }
    void Arm(JumpRequest request,uint32_t now) { request_=request; at_=now; pending_=true; }
    bool Take(uint32_t now,JumpRequest& request) {
        const bool accepted=pending_ && now-at_<500;
        pending_=false;
        if (accepted) request=request_;
        return accepted;
    }
};
// A resync re-inits the helper session (about 9 s with real assets), so only a large split triggers it;
// a small one (the character capped below the board's speed) keeps driving on the board's velocity.
constexpr float ResyncXY = 20.f, ResyncZ = 10.f;

// Inverse of the helper's conversion: world yards = origin + (x, -z, y) / 0.9144.
inline void World(const float origin[3], const float engine[3], float out[3]) {
    out[0] = origin[0] + engine[0] / Yards;
    out[1] = origin[1] - engine[2] / Yards;
    out[2] = origin[2] + engine[1] / Yards;
}
// WoW fall velocity (negative up) for an ollie that leaves the ground at engine vy m/s.
// Stock gravity then owns the arc, as the DLL's default vertical motor did.
inline float TakeoffVelocity(float vy) {
    const float up = std::isfinite(vy) ? vy / Yards : 0;
    return up > 1.f ? -(std::min)(up, 20.f) : StockJump;
}
inline float Wrap(float facing) {
    facing = fmodf(facing, 6.28318530718f);
    return facing < 0 ? facing + 6.28318530718f : facing;
}
inline bool Turn(float target, float current, float epsilon = .02f) {
    const float d = fabsf(remainderf(target - current, 6.28318530718f));
    return std::isfinite(d) && d > epsilon;
}
// The server's run speed: the last value we did not write ourselves. Never restore over a newer one.
struct Lease {
    bool held = false;
    float server = 0, applied = 0;
    float Observe(float present) {
        if (!held || fabsf(present - applied) > 1e-4f) server = present;
        held = true; applied = present;
        return server;
    }
    void Applied(float speed) { applied = speed; }
    float Release() { held = false; return server; }
};
// The board's ollie arc for the character's fall (CMovementShared::RelDistanceFallen via OnMoverFallDistance).
// Phase 5 live test: stock gravity from the first airborne vy flew about half as high as the board and landed early,
// because Skate's pop keeps lifting after takeoff and its root arc is animated, not ballistic. So the character's
// height follows the helper's root height above its takeoff height, sampled as it streams in; past the newest sample
// it continues on that sample's velocity under stock gravity (the arc never stalls waiting for the helper).
constexpr float ArcGravity = 19.291105f; // stock Wrath fall gravity, yd/s^2
class Arc {
    struct Sample { float seconds, height; };
    Sample samples_[256]{};
    unsigned count_ = 0;
    float base_ = 0, velocity_ = 0; // takeoff root height (m); newest vertical velocity (yd/s)
public:
    static constexpr float MaxSeconds = 8.f, MaxDistance = 512.f;
    bool Active() const { return count_ != 0; }
    void Stop() { count_ = 0; }
    // rootY / vy: helper root height (m) and vertical velocity (m/s) at the takeoff frame.
    void Start(float rootY, float vy) {
        Stop();
        if (!std::isfinite(rootY) || fabsf(rootY)>10000 || !std::isfinite(vy) || fabsf(vy)>100) return;
        base_ = rootY; velocity_ = vy / Yards;
        samples_[0] = {0, 0}; count_ = 1;
    }
    void Add(float seconds, float rootY, float vy) {
        if (!count_ || !std::isfinite(seconds) || seconds>MaxSeconds || !std::isfinite(rootY)
            || fabsf((rootY-base_)/Yards)>64 || !std::isfinite(vy) || fabsf(vy)>100
            || !(seconds > samples_[count_-1].seconds)) return;
        if (count_ == 256) { std::move(samples_+1,samples_+count_,samples_); --count_; }
        samples_[count_++] = {seconds, (rootY - base_) / Yards};
        velocity_ = vy / Yards;
    }
    // Yards fallen (positive down) after `seconds` of fall, as RelDistanceFallen reports it.
    float Distance(float seconds) const {
        if (!count_ || !std::isfinite(seconds) || seconds>MaxSeconds) return 0;
        if (seconds <= 0) return 0;
        for (unsigned i = 1; i < count_; ++i)
            if (seconds <= samples_[i].seconds) {
                const Sample a = samples_[i-1], b = samples_[i];
                return -(a.height + (b.height - a.height) * (seconds - a.seconds) / (b.seconds - a.seconds));
            }
        const Sample last = samples_[count_-1];
        const float dt = seconds - last.seconds;
        return -(last.height + velocity_ * dt - .5f * ArcGravity * dt * dt);
    }
    // RelDistanceFallen returns remaining movement, not total distance from takeoff.
    bool Step(float seconds, float currentZ, float fallStartZ, float& distance) const {
        if (!Active() || !std::isfinite(seconds) || seconds<0 || seconds>MaxSeconds
            || !std::isfinite(currentZ) || !std::isfinite(fallStartZ)) return false;
        const float remaining=Distance(seconds)+currentZ-fallStartZ;
        if (!std::isfinite(remaining) || fabsf(remaining)>MaxDistance) return false;
        distance=remaining; return true;
    }
    // The matching CalcTimeFallen inverse keeps stock swept-contact timing consistent with the custom arc.
    bool Time(float distance, bool upward, float& seconds) const {
        if (!Active() || !std::isfinite(distance) || fabsf(distance)>MaxDistance) return false;
        for (unsigned i=1;i<count_;++i) {
            const Sample a=samples_[i-1],b=samples_[i];
            const float da=-a.height,db=-b.height;
            if ((upward ? db<da : db>da) && distance>=(std::min)(da,db)-1e-5f
                && distance<=(std::max)(da,db)+1e-5f) {
                seconds=a.seconds+(b.seconds-a.seconds)*(std::max)(0.f,(std::min)(1.f,(distance-da)/(db-da)));
                return true;
            }
        }
        const Sample last=samples_[count_-1];
        const float discriminant=velocity_*velocity_+2*ArcGravity*(distance+last.height);
        if (!std::isfinite(discriminant) || discriminant<0) return false;
        float best=INFINITY;
        for (float dt : {(velocity_-sqrtf(discriminant))/ArcGravity,(velocity_+sqrtf(discriminant))/ArcGravity}) {
            if (dt<0 || (upward ? ArcGravity*dt-velocity_>=0 : ArcGravity*dt-velocity_<0)) continue;
            best=(std::min)(best,last.seconds+dt);
        }
        if (!std::isfinite(best) || best>MaxSeconds) return false;
        seconds=best; return true;
    }
};
struct Frame {
    bool valid = false, airborne = false;
    uint64_t tick = 0;  // helper step counter; a frozen one means the helper is busy (re-init)
    float origin[3]{}, position[3]{}, velocity[3]{};
};
struct Command {
    bool drive = false, forward = false, jump = false, resync = false;
    float speed = 0, facing = 0, jumpVelocity = 0;
};
class Driver {
    bool airborne_ = false, popped_ = false;
    uint32_t popAt_ = 0, tickAt_ = 0;
    uint64_t tick_ = 0;
public:
    void Clear() { *this = {}; }
    // actual: the mover's world position; allowance: run speed the server allows (yd/s); pop: stick flicked up.
    Command Step(const Frame& f, const float actual[3], float allowance, bool pop, uint32_t now) {
        if (pop) { popped_ = true; popAt_ = now; }
        Command c;
        if (!f.valid) return c;
        if (f.tick != tick_) { tick_ = f.tick; tickAt_ = now; }
        else if (now - tickAt_ > StallMs) return c;  // never keep running on a frozen pose
        float board[3];
        World(f.origin, f.position, board);
        // A resync blocks the helper for seconds; mid-air that froze the character and its pose until the re-init
        // finished (live test 2026-11-15). Keep flying on the board's velocity and resync after landing.
        if ((!(hypotf(board[0]-actual[0], board[1]-actual[1]) < ResyncXY) || !(fabsf(board[2]-actual[2]) < ResyncZ)) && !f.airborne) {
            c.resync = true; airborne_ = f.airborne; return c;
        }
        // Speed follows the board's horizontal velocity; ownedOllie keeps it while airborne.
        auto r = skatemotion::Ground(f.velocity[0], f.velocity[2], true, false, 1, 1, 0, 0, allowance, true);
        // Synchronize catches up within 3 yd and refuses beyond; past that follow the velocity alone.
        const auto s = skatemotion::Synchronize(r, actual[0], actual[1], f.position[0], f.position[2], f.origin[0], f.origin[1], allowance);
        if (s.valid) r = s;
        if (!r.valid) return c;
        c.drive = true; c.speed = r.speed; c.forward = r.speed > 0; c.facing = Wrap(r.heading);
        if (f.airborne && !airborne_ && popped_ && now - popAt_ < PopWindowMs) {
            c.jump = true; c.jumpVelocity = TakeoffVelocity(f.velocity[1]); popped_ = false;
        }
        airborne_ = f.airborne;
        return c;
    }
};
}
