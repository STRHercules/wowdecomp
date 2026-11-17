#pragma once
#include <cmath>
#include <cstdint>

namespace groundheading {
constexpr float Pi=3.14159265359f,Turn=2*Pi;
constexpr float BlendSeconds=0.f;
constexpr float FacingEpsilon=.001f;
inline float Delta(float to,float from) { return std::remainder(to-from,Turn); }
// Vanilla takes the Source travel direction directly. Camera follow owns its
// own smoothing; never filter the board's movement direction to smooth a camera.
struct Handoff {
    bool active=false,sent=false;
    float heading=0,lastSent=0;
    uint32_t at=0;
    void Clear() { *this={}; }
    bool Step(float target,float actual,uint32_t now,float& out) {
        if (!std::isfinite(target) || !std::isfinite(actual)) { Clear(); return false; }
        if (!active) { lastSent=std::remainder(actual,Turn); sent=true; }
        active=true; heading=std::remainder(target,Turn); at=now;
        out=heading;
        return std::isfinite(out);
    }
    bool Changed(float angle) const {
        return std::isfinite(angle) && (!sent || fabsf(Delta(angle,lastSent))>=FacingEpsilon);
    }
    bool Changed(float angle,float actual) const {
        // Native mouse/input handling may alter the actor after our last
        // accepted event. Source travel is still the riding-facing authority.
        return std::isfinite(actual) && (Changed(angle)
            || (std::isfinite(angle) && fabsf(Delta(angle,actual))>=FacingEpsilon));
    }
    void Accepted(float angle) { lastSent=angle; sent=true; }
};
}
