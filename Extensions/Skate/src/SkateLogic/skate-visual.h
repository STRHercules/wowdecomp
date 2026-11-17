#pragma once
#include "skate-pipe.h"
#include <memory>
#include <array>

namespace skatevisual {
struct Point { float x,y,z; };
struct Texture { unsigned width=0,height=0; std::vector<unsigned char> rgba; };
struct Mesh {
    std::vector<uint32_t> colors,indices,materials;
    std::vector<std::array<float,2>> uv;
    std::shared_ptr<const std::vector<Texture>> textures;
};
struct Frame {
    skatewire::Pose pose{};
    bool independentBoard=false; // Validation domain only; never a physics phase.
    unsigned carriedHand=0; // Tick-paired original possession, never inferred from pose.
    bool boardHidden=false;
    Point anchors[24]{};
    std::vector<Point> vertices;
};
struct Failure {
    const char* reason="none";
    size_t point=0;
    Point value{},root{};
    float horizontal=0,vertical=0,bound=0;
};
inline std::shared_ptr<const Mesh> Schema(const std::vector<unsigned char>& bytes) {
    if (bytes.size()<16 || skatewire::Get(bytes.data()+12)!=24) return {};
    const unsigned version=skatewire::Get(bytes.data());if(version!=1 && version!=2)return {};
    const uint32_t vertices=skatewire::Get(bytes.data()+4),indices=skatewire::Get(bytes.data()+8);
    const size_t base=16+(vertices+indices)*4;
    if (!vertices || vertices>4096 || !indices || indices>12000 || indices%3 || bytes.size()<base || (version==1 && bytes.size()!=base)) return {};
    auto mesh=std::make_shared<Mesh>();
    for (uint32_t i=0;i<vertices;++i) {
        const uint32_t color=skatewire::Get(bytes.data()+16+i*4);
        if ((color>>24)!=255) return {};
        mesh->colors.push_back(color);
    }
    for (uint32_t i=0;i<indices;++i) {
        const uint32_t index=skatewire::Get(bytes.data()+16+(vertices+i)*4);
        if (index>=vertices) return {};
        mesh->indices.push_back(index);
    }
    if(version==2){
        size_t at=base;
        if(bytes.size()<at+vertices*8+indices/3*4+4)return {};
        for(unsigned i=0;i<vertices;++i){
            const float u=skatewire::GetFloat(bytes.data()+at),v=skatewire::GetFloat(bytes.data()+at+4);at+=8;
            if(!std::isfinite(u) || !std::isfinite(v) || fabsf(u)>32 || fabsf(v)>32)return {};
            mesh->uv.push_back({u,v});
        }
        for(unsigned i=0;i<indices/3;++i){mesh->materials.push_back(skatewire::Get(bytes.data()+at));at+=4;}
        const unsigned count=skatewire::Get(bytes.data()+at);at+=4;if(!count || count>16)return {};
        auto images=std::make_shared<std::vector<Texture>>();size_t budget=0;
        for(unsigned i=0;i<count;++i){
            if(bytes.size()-at<12)return {};
            const unsigned w=skatewire::Get(bytes.data()+at),h=skatewire::Get(bytes.data()+at+4),n=skatewire::Get(bytes.data()+at+8);at+=12;
            if(!w || !h || w>512 || h>512 || (w&(w-1)) || (h&(h-1)) || n!=w*h*4 || bytes.size()-at<n || (budget+=n)>1536*1024)return {};
            images->push_back({w,h,{bytes.begin()+at,bytes.begin()+at+n}});at+=n;
        }
        if(at!=bytes.size())return {};
        for(auto m:mesh->materials)if(m>=count)return {};
        mesh->textures=images;
    }
    return mesh;
}
inline std::shared_ptr<const Frame> Decode(const std::vector<unsigned char>& bytes,const Mesh& mesh,bool independentBoard=false,Failure* failure=nullptr,bool detachedBoard=false) {
    if (failure) *failure={};
    const auto refuse=[&](const char* reason,size_t point=0,Point value=Point{},Point root=Point{},float horizontal=0,float vertical=0,float bound=0)
        ->std::shared_ptr<const Frame> {
        if (failure) *failure={reason,point,value,root,horizontal,vertical,bound};
        return {};
    };
    if (bytes.size()!=72+(24+mesh.colors.size())*12 || skatewire::Get(bytes.data()+60)!=1
        || skatewire::Get(bytes.data()+64)!=mesh.colors.size() || skatewire::Get(bytes.data()+68)!=24) return refuse("layout");
    auto frame=std::make_shared<Frame>();
    frame->independentBoard=independentBoard;
    const std::vector<unsigned char> summary(bytes.begin(),bytes.begin()+60);
    if (!skatewire::Decode(summary,frame->pose)) return refuse("summary");
    const Point root{frame->pose.position[0],frame->pose.position[1],frame->pose.position[2]};
    for (size_t i=0;i<24+mesh.colors.size();++i) {
        Point p{skatewire::GetFloat(bytes.data()+72+i*12),skatewire::GetFloat(bytes.data()+76+i*12),skatewire::GetFloat(bytes.data()+80+i*12)};
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)
            || fabsf(p.x)>10000 || fabsf(p.y)>10000 || fabsf(p.z)>10000) return refuse("coordinate",i,p,root);
        // This is a tightly bounded local preview, never arbitrary world geometry.
        const float bound=independentBoard && i>=24?32.f:4.f;
        const float horizontal=hypotf(p.x-root.x,p.z-root.z),vertical=fabsf(p.y-root.y);
        if (horizontal>bound || vertical>bound) {
            // A genuine detached board can travel away while the rider is
            // pinned against a wall. Cull that prop, not the valid body/physics.
            if(detachedBoard && independentBoard && i>=24)frame->boardHidden=true;
            else return refuse(i<24?"body_root_bound":"board_root_bound",i,p,root,horizontal,vertical,bound);
        }
        if (i<24) frame->anchors[i]=p;
        else frame->vertices.push_back(p);
    }
    if(independentBoard){
        Point centre{};
        for(auto p:frame->vertices){centre.x+=p.x;centre.y+=p.y;centre.z+=p.z;}
        const float count=static_cast<float>(frame->vertices.size());
        centre={centre.x/count,centre.y/count,centre.z/count};
        // Vanilla board.rs measures vertices in the board's own local frame.
        // Explicitly owned riding/bail/on-foot frames share this geometry check;
        // body anchors retain4m and the board remains bounded/local to itself.
        for(size_t i=0;i<frame->vertices.size();++i) {
            const auto p=frame->vertices[i];
            const float horizontal=hypotf(p.x-centre.x,p.z-centre.z),vertical=fabsf(p.y-centre.y);
            if(horizontal>2 || vertical>2) return refuse("board_shape",24+i,p,centre,horizontal,vertical,2);
        }
    }
    return frame;
}
inline bool RidingBoard(const skatewire::Pose& pose,uint64_t metadataTick,unsigned state){
    if(!pose.tick || pose.tick!=metadataTick) return false;
    return (state>=100 && state<=103 && pose.ground==1 && pose.airborne==0)
        || (state>=200 && state<=202 && pose.ground==0 && pose.airborne==1);
}
inline std::shared_ptr<const Frame> DecodePaired(const std::vector<unsigned char>& bytes,const Mesh& mesh,
    uint64_t metadataTick,unsigned state,bool detachedBoard=false,Failure* failure=nullptr){
    skatewire::Pose pose{};
    bool riding=false;
    if(bytes.size()>=60){
        const std::vector<unsigned char> summary(bytes.begin(),bytes.begin()+60);
        riding=skatewire::Decode(summary,pose) && RidingBoard(pose,metadataTick,state);
    }
    return Decode(bytes,mesh,detachedBoard || riding,failure,detachedBoard);
}
}
