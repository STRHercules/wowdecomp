#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace skateshadow {
enum class EdgeAction { Continue, Recenter, StopEdge, StopGround };
inline EdgeAction EdgePolicy(bool poseOnlyRecenter,bool grounded,bool airborne,float x,float z) {
    if (!std::isfinite(x) || !std::isfinite(z) || !grounded || airborne) return EdgeAction::StopGround;
    const float distance=(std::max)(fabsf(x),fabsf(z));
    // No weakened hard edge: invalid/late requests still stop. Only explicitly
    // armed stationary posing can reset before it reaches the original18m gate.
    if (distance>=18) return EdgeAction::StopEdge;
    if (poseOnlyRecenter && distance>=12) return EdgeAction::Recenter;
    return EdgeAction::Continue;
}
// 'now' must be read AFTER copying the worker's snapshot, not at frame start.
// A worker can publish between those events. Unsigned subtraction from the
// earlier frame clock would wrap and falsely report a fresh result as stale.
inline bool Fresh(std::uint32_t receivedEpoch,std::uint32_t expectedEpoch,
                  std::uint32_t now,std::uint32_t publishedAt) {
    return receivedEpoch!=0 && receivedEpoch==expectedEpoch && now-publishedAt<750;
}
}
