#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace Retail {
struct CosmeticCameraFit { std::array<float,3> center; float radius, distance; };
inline CosmeticCameraFit FitCosmeticBounds(const std::array<float,3>& low, const std::array<float,3>& high,
    float facing, float aspect, float zoom) {
    const float x=(low[0]+high[0])*0.5f, y=(low[1]+high[1])*0.5f;
    CosmeticCameraFit result{{x*std::cos(facing)-y*std::sin(facing),
        x*std::sin(facing)+y*std::cos(facing),(low[2]+high[2])*0.5f},0,0};
    float diagonal=0;
    for (unsigned i=0;i<3;++i) diagonal+=(high[i]-low[i])*(high[i]-low[i]);
    result.radius=std::max(0.1f,std::sqrt(diagonal)*0.5f);
    const float halfFov=std::atan(std::tan(0.4f)*std::min(1.0f,std::max(0.05f,aspect)));
    result.distance=result.radius/std::sin(halfFov)*1.12f/std::max(0.2f,zoom);
    return result;
}
}
