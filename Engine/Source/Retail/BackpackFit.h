#pragma once
#include <cmath>
namespace Retail {
struct BackpackFit { float scale=1, x=0, y=0, z=0; };
inline bool ValidBackpackFit(const BackpackFit& fit) {
    return std::isfinite(fit.scale) && fit.scale>=0.1f && fit.scale<=4 &&
        std::isfinite(fit.x) && std::fabs(fit.x)<=3 && std::isfinite(fit.y) && std::fabs(fit.y)<=3 &&
        std::isfinite(fit.z) && std::fabs(fit.z)<=3;
}
}
