#pragma once
// Untrusted addon input is visual-only. Never accepts world positions or movement.
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
namespace peervisual {
constexpr size_t MaxPeers=39,MaxMessage=254;
struct Point { float x=0,y=0,z=0; };
inline Point Sub(Point a,Point b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline Point Add(Point a,Point b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
inline Point Mul(Point a,float n){return {a.x*n,a.y*n,a.z*n};}
inline float Dot(Point a,Point b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Point Cross(Point a,Point b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float Length(Point a){return sqrtf(Dot(a,a));}
inline bool Finite(Point a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
inline Point Rotate(Point p,float yaw){const float c=cosf(yaw),s=sinf(yaw);return {c*p.x-s*p.y,s*p.x+c*p.y,p.z};}
struct Packet { uint64_t guid=0; uint32_t map=0,epoch=0,sequence=0; bool visible=false; Point markers[3]{}; };
inline bool Basis(const Point* p,Point& u,Point& v,Point& w){
    u=Sub(p[1],p[0]);v=Sub(p[2],p[0]);const Point normal=Cross(u,v);
    const float a=Length(u),b=Length(v),n=Length(normal);
    if(!Finite(u)||!Finite(v)||!std::isfinite(n)||a<.002f||a>256||b<.002f||b>256||n<a*b*.02f)return false;
    w=Mul(normal,a/n);return true;
}
inline bool Valid(const Packet& p){
    if(!p.guid||(p.guid>>32)||p.map>9999||!p.sequence)return false;
    for(auto a:p.markers)if(!Finite(a)||fabsf(a.x)>128||fabsf(a.y)>128||fabsf(a.z)>128)return false;
    Point u{},v{},w{};return !p.visible||Basis(p.markers,u,v,w);
}
inline bool Unsigned(const std::string& s,uint32_t& value){
    if(s.empty()||s.size()>10)return false;uint64_t v=0;
    for(char c:s){if(c<'0'||c>'9')return false;v=v*10+static_cast<unsigned>(c-'0');if(v>UINT32_MAX)return false;}
    value=static_cast<uint32_t>(v);return true;
}
inline bool Parse(const std::string& text,Packet& out){
    if(text.empty()||text.size()>MaxMessage)return false;
    std::array<std::string,16> parts{};size_t at=0;
    for(size_t i=0;i<parts.size();++i){const auto end=text.find(';',at);if((i+1<parts.size())!=(end!=std::string::npos))return false;
        parts[i]=text.substr(at,end==std::string::npos?end:end-at);at=end==std::string::npos?text.size():end+1;}
    Packet p{};if(parts[0]!="1"||parts[1].size()!=16)return false;
    for(char c:parts[1]){unsigned n=0;if(c>='0'&&c<='9')n=c-'0';else if(c>='A'&&c<='F')n=c-'A'+10;else if(c>='a'&&c<='f')n=c-'a'+10;else return false;p.guid=(p.guid<<4)|n;}
    uint32_t visible=0,reserved=0;
    if(!Unsigned(parts[2],p.map)||!Unsigned(parts[3],p.epoch)||!Unsigned(parts[4],p.sequence)
        ||!Unsigned(parts[5],visible)||visible>1||!Unsigned(parts[6],reserved)||reserved)return false;
    p.visible=visible==1;
    for(size_t i=0;i<9;++i){const auto& s=parts[i+7];const bool minus=!s.empty()&&s[0]=='-';uint32_t n=0;
        if(!Unsigned(minus?s.substr(1):s,n)||n>128000)return false;
        const float f=(minus?-static_cast<float>(n):static_cast<float>(n))*.001f;
        if(i%3==0)p.markers[i/3].x=f;else if(i%3==1)p.markers[i/3].y=f;else p.markers[i/3].z=f;
    }
    if(!Valid(p))return false;out=p;return true;
}
inline std::string Encode(const Packet& p){
    if(!Valid(p))return {};
    char b[256];const auto q=[](float f){return static_cast<int>(roundf(f*1000));};
    const int n=snprintf(b,sizeof(b),"1;%016llX;%u;%u;%u;%u;0;%d;%d;%d;%d;%d;%d;%d;%d;%d",
        static_cast<unsigned long long>(p.guid),p.map,p.epoch,p.sequence,p.visible?1u:0u,
        q(p.markers[0].x),q(p.markers[0].y),q(p.markers[0].z),q(p.markers[1].x),q(p.markers[1].y),q(p.markers[1].z),q(p.markers[2].x),q(p.markers[2].y),q(p.markers[2].z));
    return n>0&&static_cast<size_t>(n)<=MaxMessage?std::string(b,static_cast<size_t>(n)):std::string{};
}
inline Point Reconstruct(const Point* markers,Point coefficients){
    Point u{},v{},w{};if(!Basis(markers,u,v,w))return {NAN,NAN,NAN};
    return Add(markers[0],Add(Mul(u,coefficients.x),Add(Mul(v,coefficients.y),Mul(w,coefficients.z))));
}
inline bool Eligible(const Packet& p,uint64_t local,uint32_t map,uint32_t age){return Valid(p)&&p.visible&&p.guid!=local&&p.map==map&&age<=1000;}
}
