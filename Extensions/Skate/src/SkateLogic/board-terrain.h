#pragma once
#include "board-placement.h"
namespace boardterrain {
struct Sample { skatevisual::Point displayed{},source{}; };
inline unsigned Wheels(const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,
    skatevisual::Point player,skatevisual::Point origin,const boardplacement::Fit& fit,Sample (&out)[4]) {
    if (!boardplacement::Valid(fit) || !frame.pose.ground || frame.pose.airborne || frame.vertices.empty()
        || frame.vertices.size()!=mesh.colors.size() || frame.vertices.size()>4096) return 0;
    float bottom=INFINITY;
    for (const auto p:frame.vertices) bottom=(std::min)(bottom,p.y);
    unsigned count=0;
    // Sample lowest wheel-material vertices at separated XY locations. These
    // are measured mesh samples, not claimed physics wheel/contact centres.
    for (unsigned attempt=0;attempt<4;++attempt) {
        float lowest=INFINITY; size_t selected=frame.vertices.size();
        for (size_t i=0;i<frame.vertices.size();++i) {
            if (mesh.colors[i]!=0xffe8dbb8) continue;
            const auto p=boardplacement::WorldPoint(frame.vertices[i],frame,player,bottom,fit);
            bool separate=true;
            for (unsigned j=0;j<count;++j)
                if (hypotf(p.x-out[j].displayed.x,p.y-out[j].displayed.y)<.15f) separate=false;
            if (separate && std::isfinite(p.z) && p.z<lowest) { lowest=p.z; selected=i; }
        }
        if (selected==frame.vertices.size()) break;
        const auto p=frame.vertices[selected];
        out[count++]={boardplacement::WorldPoint(p,frame,player,bottom,fit),
            {origin.x+p.x/.9144f,origin.y-p.z/.9144f,origin.z+p.y/.9144f}};
    }
    return count;
}
struct GroundFit { bool valid=false;float correction=0,visualGap=INFINITY,sourceGap=INFINITY;unsigned pairs=0; };
template<class Floor> inline GroundFit FitGround(const skatevisual::Mesh& mesh,const skatevisual::Frame& frame,
    skatevisual::Point player,skatevisual::Point origin,boardplacement::Fit& fit,Floor floor){
    GroundFit out{};Sample wheels[4]{};
    if(fit.physical || fit.rootRelative || Wheels(mesh,frame,player,origin,fit,wheels)!=4)return out;
    for(const auto& wheel:wheels){float source=NAN,visual=NAN;
        if(!floor(wheel.source,source) || !floor(wheel.displayed,visual) || !std::isfinite(source) || !std::isfinite(visual))continue;
        out.sourceGap=(std::min)(out.sourceGap,wheel.source.z-source);
        out.visualGap=(std::min)(out.visualGap,wheel.displayed.z-visual);++out.pairs;
    }
    // Original Source wheel support must independently agree with real terrain.
    // Never pin an airborne/free board or treat an unknown floor as support.
    if(out.pairs!=4 || fabsf(out.sourceGap)>.08f || !std::isfinite(out.visualGap))return out;
    out.correction=out.sourceGap-out.visualGap;
    if(fabsf(out.correction)>.75f || fabsf(fit.z+out.correction)>.75f)return out;
    fit.z+=out.correction;out.valid=boardplacement::Valid(fit);return out;
}
}
