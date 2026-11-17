#include "start-surface.h"
#include "onfoot-terrain.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <limits>
using startsurface::Point;
static unsigned checks=0,geometryCases=0;
static void Check(bool ok,const char* message){++checks;if(!ok){fprintf(stderr,"FAIL start surface %u: %s\n",checks,message);exit(1);}}
static void Axis(Point& point,unsigned axis,float value){if(axis==0)point.x=value;else if(axis==1)point.y=value;else point.z=value;}
// Explicit finite planes/rectangles exercise the actual production query bounds
// and predicate. These are owned offline geometry fixtures, NOT native MPQ replays.
struct Surface {float x0,x1,y0,y1,z,slopeX,slopeY;};
static auto Scene(std::vector<Surface> surfaces){return [surfaces](Point a,Point b,Point& hit,float& fraction){
    bool found=false;fraction=1;
    for(const auto& surface:surfaces){
        if(a.x<surface.x0||a.x>surface.x1||a.y<surface.y0||a.y>surface.y1)continue;
        const float z=surface.z+a.x*surface.slopeX+a.y*surface.slopeY;
        if(z>a.z||z<b.z)continue;
        const float f=(a.z-z)/(a.z-b.z);
        if(!found||f<fraction){found=true;fraction=f;hit={a.x,a.y,z};}
    }
    return found;
};}
int main(){
    Check(startsurface::Rise==onfootterrain::FloorPenetration && startsurface::Drop==onfootterrain::FloorGap,
        "start floor envelope matches existing measured on-foot envelope");
    const Surface floor{-10,10,-10,10,0,0,0};
    for(float gap:{-.125f,-.05f,0.f,.096f,.234558f,.5f}){
        const auto result=startsurface::Measure({0,0,gap},Scene({floor}));
        Check(result.supported,"real underfoot floor in the retained contact envelope");
        Check(fabsf(result.gap-gap)<.00001f,"measured gap is original floor distance");++geometryCases;
    }
    // Wide room with a roof: old feet+6 query selects the roof first.
    for(float roof:{.25f,.5f,1.f,3.f,5.5f}){
        Surface top=floor;top.z=roof;
        const auto result=startsurface::Measure({0,0,0},Scene({floor,top}));
        Check(result.supported && result.hit.z==0,"overhead roof is not current standing floor");++geometryCases;
    }
    // Player stands on a narrow lip with a genuine drop immediately beyond it.
    for(float width:{.02f,.1f,.5f,1.f}){
        Surface ledge{-width,width,-.05f,.05f,0,0,0};
        Surface drop=floor;drop.z=-8;
        const auto query=Scene({ledge,drop});
        Check(startsurface::Measure({0,0,0},query).supported,"measured lip can start without four-yard continuity");
        Check(!startsurface::Measure({width+.025f,0,0},query).supported,"off-centre gap remains unsupported");++geometryCases;
    }
    for(float slope:{-4.f,-2.f,-1.f,.5f,1.f,2.f,4.f}){
        Surface bank=floor;bank.slopeX=slope;
        Check(startsurface::Measure({0,0,.05f},Scene({bank})).supported,"actual bank surface under feet permits start");++geometryCases;
    }
    for(float z:{-.501f,-2.f,-12.f,.126f,.5f,3.f}){
        Surface remote=floor;remote.z=z;
        Check(!startsurface::Measure({0,0,0},Scene({remote})).supported,"remote floor/upper platform alone cannot start");++geometryCases;
    }
    Check(!startsurface::Measure({0,0,0},Scene({})).supported,"empty world remains unsupported");
    for(float bad:{NAN,INFINITY,-INFINITY}){
        for(unsigned axis=0;axis<3;++axis){
            Point feet{};Axis(feet,axis,bad);
            Check(!startsurface::Measure(feet,Scene({floor})).supported,"non-finite feet rejected");
            const auto result=startsurface::Measure({0,0,0},[bad,axis](Point,Point,Point& hit,float& f){hit={};Axis(hit,axis,bad);f=.2f;return true;});
            Check(!result.supported,"non-finite hit rejected");
        }
        Check(!startsurface::Measure({0,0,0},[bad](Point,Point,Point& hit,float& f){hit={};f=bad;return true;}).supported,"non-finite fraction rejected");
    }
    for(float fraction:{-.01f,1.01f})Check(!startsurface::Measure({0,0,0},[fraction](Point,Point,Point& hit,float& f){hit={};f=fraction;return true;}).supported,"out-of-range fraction rejected");
    for(float off:{-.021f,.021f})for(unsigned axis=0;axis<2;++axis){
        Check(!startsurface::Measure({0,0,0},[off,axis](Point,Point,Point& hit,float& f){hit={};Axis(hit,axis,off);f=.2f;return true;}).supported,"off-axis native result rejected");
    }
    for(float gap:{-.126f,.501f})Check(!startsurface::Measure({0,0,0},[gap](Point,Point,Point& hit,float& f){hit={0,0,-gap};f=.2f;return true;}).supported,"malformed query outside envelope rejected");
    // World-sized coordinates must not be subjected to model-local limits.
    for(Point p:std::vector<Point>{{-4989.50732f,-866.95551f,496.98438f},{5800,600,650},{11000,-2000,900}}){
        const auto result=startsurface::Measure(p,[p](Point a,Point b,Point& hit,float& f){
            Check(a.z>=p.z && b.z<p.z,"short vertical native segment");hit=p;f=.2f;return true;
        });
        Check(result.supported,"finite real-sized continent coordinates accepted");
    }
    struct Patch{Point centre{};float height[625]{};bool valid[625]{};};
    Patch legacy{};for(auto& valid:legacy.valid)valid=true;
    Check(startsurface::LegacyGrid<25>(legacy),"legacy valid plane retained");
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        const unsigned i=static_cast<unsigned>(312+y*25+x);
        legacy.valid[i]=false;Check(!startsurface::LegacyGrid<25>(legacy),"legacy missing neighbour rejected");legacy.valid[i]=true;
        legacy.height[i]=2.f;Check(startsurface::LegacyGrid<25>(legacy),"legacy 2yd exact boundary retained");
        legacy.height[i]=2.01f;Check(!startsurface::LegacyGrid<25>(legacy),"legacy remote neighbour rejected");legacy.height[i]=0;
    }
    printf("{\"passed\":true,\"checks\":%u,\"owned_geometry_cases\":%u,\"authored_local_support_only\":true,\"legacy_grid_unchanged\":true,\"overhead_miss_gap_negative_tests\":true,\"no_invented_surface\":true,\"finite_guards\":true,\"native_live_verified\":false}\n",checks,geometryCases);
}
