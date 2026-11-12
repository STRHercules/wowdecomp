#include "ASECM3/ParticleAppearance.h"
#include <algorithm>
#include <cmath>
namespace ASECM3 {
namespace {
double Value(const MaterialRecord& record,const char* field,double fallback=0){const auto* p=record.Find(field);const double v=p?std::visit([](auto x){return double(x);},p->value):fallback;if(!std::isfinite(v))throw Error(std::string("Non-finite particle appearance: ")+field);return v;}
uint32_t UInt(const MaterialRecord& record,const char* field){const double v=Value(record,field);if(v<0||v>UINT32_MAX||std::floor(v)!=v)throw Error(std::string("Invalid particle appearance integer: ")+field);return uint32_t(v);}
float Fraction(const MaterialRecord& record,const char* field,float fallback=.5f){const double v=Value(record,field,fallback);if(v<0||v>1)throw Error(std::string("Invalid particle lifespan midpoint: ")+field);return float(v);}
float Random(uint32_t& state){if(!state)state=0x6d2b79f5u;state^=state<<13;state^=state>>17;state^=state<<5;return float(double(state)/4294967296.0);}
// shortcut: hold keeps the mid value from the midpoint for `hold` of the lifespan; SC2's exact hold placement is unverified.
float Curve(float a,float b,float c,double age,float midpoint,uint32_t mode,float hold=0){
    if(mode>1)throw Error("Unsupported particle appearance smoothing/hold mode");
    const double end=std::min(1.0,double(midpoint)+hold);
    if(age>=midpoint&&age<end)return b;
    const bool first=age<midpoint;const double duration=first?midpoint:1-end;
    double t=duration>0?(first?age:age-end)/duration:1;t=std::clamp(t,0.0,1.0);
    if(mode==1)t=t*t*(3-2*t);
    return float((first?a:b)*(1-t)+(first?b:c)*t);
}
KeyValue Vector(const MaterialRecord& source,const MaterialEvaluator& values,const char* field,const char* alternate,bool randomized,uint32_t& seed){
    auto out=values.Sample(source,field,2,{});
    if(randomized){const auto other=values.Sample(source,alternate,2,out);const float amount=Random(seed);for(unsigned i=0;i<3;++i)out[i]+=(other[i]-out[i])*amount;}
    for(auto v:out)if(!std::isfinite(v))throw Error("Non-finite particle lifespan vector");return out;
}
}
ParticleAppearance ParticleAppearance::Capture(const MaterialRecord& source,const MaterialEvaluator& values,uint32_t seed){
    ParticleAppearance out;
    out.m_size=Vector(source,values,"size","size2",UInt(source,"size_randomize")!=0,seed);
    out.m_rotation=Vector(source,values,"rotation","rotation2",UInt(source,"rotation_randomize")!=0,seed);
    for(unsigned i=0;i<3;++i)if(out.m_size[i]<0)throw Error("Negative particle lifespan size");
    const bool rgb=UInt(source,"color_randomize")!=0,alpha=UInt(source,"alpha_randomize")!=0;
    const float rgbAmount=rgb?Random(seed):0,alphaAmount=alpha?Random(seed):0;
    const char* fields[]={"color_init","color_mid","color_end"};const char* alternates[]={"color2_init","color2_mid","color2_end"};
    for(unsigned i=0;i<3;++i){out.m_colors[i]=values.Sample(source,fields[i],4,{255,255,255,255});const auto other=rgb||alpha?values.Sample(source,alternates[i],4,out.m_colors[i]):out.m_colors[i];
        for(unsigned c=0;c<4;++c){out.m_colors[i][c]+=(other[c]-out.m_colors[i][c])*(c==3?alphaAmount:rgbAmount);if(!std::isfinite(out.m_colors[i][c])||out.m_colors[i][c]<0||out.m_colors[i][c]>255)throw Error("Invalid particle lifespan color");out.m_colors[i][c]/=255;}}
    out.m_sizeMid=Fraction(source,"size_anim_mid");out.m_rotationMid=Fraction(source,"rotation_anim_mid");out.m_colorMid=Fraction(source,"color_anim_mid");out.m_alphaMid=Fraction(source,"alpha_anim_mid");
    out.m_sizeMode=UInt(source,"size_smoothing");out.m_rotationMode=UInt(source,"rotation_smoothing");out.m_colorMode=UInt(source,"color_smoothing");
    if(out.m_sizeMode>1||out.m_rotationMode>1||out.m_colorMode>1)throw Error("Particle Bezier/hold interpolation requires further decoding");
    out.m_sizeHold=Fraction(source,"size_hold",0);out.m_rotationHold=Fraction(source,"rotation_hold",0);out.m_colorHold=Fraction(source,"color_hold",0);out.m_alphaHold=Fraction(source,"alpha_hold",0);
    out.m_alpha=(UInt(source,"flags")&0x200000)!=0;
    out.m_columns=std::max(1u,UInt(source,"uv_flipbook_cols"));out.m_rows=std::max(1u,UInt(source,"uv_flipbook_rows"));
    if(out.m_columns>1024||out.m_rows>1024)throw Error("Particle atlas exceeds 1024 rows/columns");
    const char* frames[]={"uv_flipbook_start_init_index","uv_flipbook_start_stop_index","uv_flipbook_end_init_index","uv_flipbook_end_stop_index"};
    for(unsigned i=0;i<4;++i){out.m_frames[i]=UInt(source,frames[i]);if(out.m_frames[i]>=out.m_columns*out.m_rows)throw Error("Particle flipbook index exceeds its atlas");}
    if(out.m_frames[1]<out.m_frames[0]||out.m_frames[3]<out.m_frames[2])throw Error("Particle flipbook range is reversed");
    if(UInt(source,"flags")&0x10000){const uint32_t count=out.m_frames[1]-out.m_frames[0]+1;out.m_frames[0]+=std::min(count-1,uint32_t(double(Random(seed))*count));}
    out.m_uvMid=Fraction(source,"uv_flipbook_start_lifespan_factor",1);
    return out;
}
ParticleVisual ParticleAppearance::At(double age)const{
    if(!std::isfinite(age))throw Error("Non-finite particle normalized age");age=std::clamp(age,0.0,1.0);
    ParticleVisual out;out.size=Curve(m_size[0],m_size[1],m_size[2],age,m_sizeMid,m_sizeMode,m_sizeHold);out.rotation=Curve(m_rotation[0],m_rotation[1],m_rotation[2],age,m_rotationMid,m_rotationMode,m_rotationHold);
    for(unsigned c=0;c<4;++c)out.color[c]=c==3&&!m_alpha?1:Curve(m_colors[0][c],m_colors[1][c],m_colors[2][c],age,c==3?m_alphaMid:m_colorMid,m_colorMode,c==3?m_alphaHold:m_colorHold);
    const bool first=age<m_uvMid;const double duration=first?m_uvMid:1-m_uvMid;
    const double t=duration>0?std::clamp((first?age:age-m_uvMid)/duration,0.0,1.0):1;
    const uint32_t start=m_frames[first?0:2],stop=m_frames[first?1:3];
    const uint32_t frame=std::min(stop,start+uint32_t(std::floor(t*(stop-start+1))));
    const uint32_t column=frame%m_columns,row=frame/m_columns;
    out.u0=float(column)/m_columns;out.v0=float(row)/m_rows;out.u1=float(column+1)/m_columns;out.v1=float(row+1)/m_rows;return out;
}
}
