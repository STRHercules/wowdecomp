#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

// Bounded, read-only live-object evidence. This is an overlay on authored map
// collision, never a sampled replacement floor or a new movement rule.
namespace dynamicworld {
constexpr unsigned MaxObjects=4,MaxVertices=1024,MaxObjectTriangles=512,MaxTriangles=2048;
constexpr uint32_t ScanMs=100,SendMs=333,LeaseMs=500;
struct Point {float x=0,y=0,z=0;};
struct Triangle {Point p[3]{};};
struct Snapshot {
    uint32_t epoch=0,map=0,revision=0,at=0,objects=0,count=0;
    uint64_t doorSignature=0;
    Triangle triangles[MaxTriangles]{};
};
inline bool PointValid(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)
    &&fabsf(p.x)<20000&&fabsf(p.y)<20000&&fabsf(p.z)<5000;}
inline bool Door(unsigned type){return type==0 || type==1;}
inline bool AllowedType(unsigned type){return Door(type) || type==5 || type==7;}
inline bool Eligible(unsigned type,unsigned state){return AllowedType(type) && (!Door(type) || state==1);}
inline bool MatrixValid(const float* m){
    if(!m)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
    if(fabsf(m[3])>.001f || fabsf(m[7])>.001f || fabsf(m[11])>.001f || fabsf(m[15]-1)>.001f)return false;
    float len[3]{};
    for(unsigned i=0;i<3;++i){len[i]=m[4*i]*m[4*i]+m[4*i+1]*m[4*i+1]+m[4*i+2]*m[4*i+2];
        if(len[i]<.0001f || len[i]>4096)return false;}
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<3;++j)
        if(fabsf(m[4*i]*m[4*j]+m[4*i+1]*m[4*j+1]+m[4*i+2]*m[4*j+2])>.01f*sqrtf(len[i]*len[j]))return false;
    const float det=m[0]*(m[5]*m[10]-m[6]*m[9])-m[4]*(m[1]*m[10]-m[2]*m[9])+m[8]*(m[1]*m[6]-m[2]*m[5]);
    return det>0 && PointValid({m[12],m[13],m[14]});
}
inline Point Transform(const float* m,Point p){return {p.x*m[0]+p.y*m[4]+p.z*m[8]+m[12],
    p.x*m[1]+p.y*m[5]+p.z*m[9]+m[13],p.x*m[2]+p.y*m[6]+p.z*m[10]+m[14]};}
inline bool Solid(const Triangle& t){
    for(const auto& p:t.p)if(!PointValid(p))return false;
    const Point a{t.p[1].x-t.p[0].x,t.p[1].y-t.p[0].y,t.p[1].z-t.p[0].z};
    const Point b{t.p[2].x-t.p[0].x,t.p[2].y-t.p[0].y,t.p[2].z-t.p[0].z};
    const Point c{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    return c.x*c.x+c.y*c.y+c.z*c.z>1e-10f;
}
inline bool SameGeometry(const Snapshot& a,const Snapshot& b){return a.epoch==b.epoch&&a.map==b.map
    &&a.objects==b.objects&&a.count==b.count&&a.count<=MaxTriangles
    &&memcmp(a.triangles,b.triangles,a.count*sizeof(Triangle))==0;}
inline bool Fresh(const Snapshot& s,uint32_t epoch,uint32_t map,uint32_t now){
    return s.epoch==epoch&&s.map==map&&static_cast<uint32_t>(now-s.at)<=LeaseMs;
}
}
