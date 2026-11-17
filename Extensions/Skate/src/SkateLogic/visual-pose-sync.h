#pragma once
#include <cmath>
#include <cstdint>
#include "board-placement.h"

namespace visualposesync {
struct Stamp { uint32_t epoch=0,at=0; uint64_t tick=0; float facing=0; bool applied=false; };
inline bool Match(const Stamp& stamp,uint32_t epoch,uint64_t tick,uint32_t now) {
    return stamp.applied && epoch && stamp.epoch==epoch && stamp.tick==tick
        && now-stamp.at<100 && std::isfinite(stamp.facing);
}
inline boardplacement::Fit Facing(const boardplacement::Fit& fit,float captured,float displayed) {
    if (!boardplacement::Valid(fit) || !std::isfinite(captured) || !std::isfinite(displayed)) return {};
    auto out=fit;
    if(out.physical)return out; // Absolute Source board, independent of character facing.
    out.yaw=atan2f(sinf(displayed-captured),cosf(displayed-captured));
    return out;
}
}
