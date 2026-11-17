#pragma once
#include <cmath>
namespace groundgrid {
template<unsigned Side,class Patch>
unsigned Reuse(const Patch& old,Patch& next,bool (&known)[Side*Side],float spacing) {
    const float dx=(next.centre.x-old.centre.x)/spacing,dy=(next.centre.y-old.centre.y)/spacing;
    if (old.epoch!=next.epoch || !std::isfinite(dx) || !std::isfinite(dy)
        || fabsf(dx)>static_cast<float>(Side) || fabsf(dy)>static_cast<float>(Side)) return 0;
    const int sx=static_cast<int>(lroundf(dx)),sy=static_cast<int>(lroundf(dy));
    if (fabsf(dx-sx)>.001f || fabsf(dy-sy)>.001f) return 0;
    unsigned reused=0;
    for (unsigned y=0;y<Side;++y) for (unsigned x=0;x<Side;++x) {
        const int ox=static_cast<int>(x)+sx,oy=static_cast<int>(y)+sy;
        if (ox<0 || oy<0 || ox>=static_cast<int>(Side) || oy>=static_cast<int>(Side)) continue;
        const unsigned i=y*Side+x,j=static_cast<unsigned>(oy)*Side+static_cast<unsigned>(ox);
        next.height[i]=old.height[j]; next.valid[i]=old.valid[j]; known[i]=true; ++reused;
    }
    return reused;
}
}
