#pragma once
#include <cmath>
namespace startsurface {
// A grounded native character may step up to half a yard above its actual
// floor. Match the existing on-foot contact envelope, not a distant 3x3 plane.
// The ray starts only .125 yd above the feet: a roof/upper platform is not the
// surface the character is standing on. No hit means no permission to start.
constexpr float Rise=.125f,Drop=.5f,HorizontalTolerance=.02f;
struct Point { float x=0,y=0,z=0; };
struct Result {
    bool supported=false;
    const char* reason="not_sampled";
    Point hit{};
    float fraction=1,gap=0;
};
inline bool Finite(Point point){return std::isfinite(point.x)&&std::isfinite(point.y)&&std::isfinite(point.z);}
template<unsigned Side,class Patch> bool LegacyGrid(const Patch& patch){
    const int centre=static_cast<int>((Side/2)*Side+Side/2);
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        const unsigned i=static_cast<unsigned>(centre+y*static_cast<int>(Side)+x);
        if(!patch.valid[i]||fabsf(patch.height[i]-patch.centre.z)>2.f)return false;
    }
    return true;
}
template<class Query> Result Measure(Point feet,Query ray){
    Result result{};
    if(!Finite(feet)){result.reason="invalid_feet";return result;}
    result.hit={NAN,NAN,NAN};
    if(!ray({feet.x,feet.y,feet.z+Rise},{feet.x,feet.y,feet.z-Drop},result.hit,result.fraction)){
        result.reason="no_floor";return result;
    }
    if(!Finite(result.hit)||!std::isfinite(result.fraction)||result.fraction<0||result.fraction>1){
        result.reason="invalid_hit";return result;
    }
    if(fabsf(result.hit.x-feet.x)>HorizontalTolerance||fabsf(result.hit.y-feet.y)>HorizontalTolerance){
        result.reason="off_axis_hit";return result;
    }
    result.gap=feet.z-result.hit.z;
    if(result.gap< -Rise||result.gap>Drop){result.reason="floor_gap";return result;}
    result.supported=true;result.reason="measured_floor";return result;
}
}
