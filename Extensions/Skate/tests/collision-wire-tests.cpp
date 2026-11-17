// Ops 48/49/50 client encoding against docs/SKATE_HELPER_PROTOCOL.md.
#include "SkateCollision.h"
#include <cassert>

int main() {
    using namespace skatecollision;
    const float origin[3]{-8800.f,640.f,95.f}, moved[3]{-8790.f,645.f,96.f};
    std::vector<float> tris{-8810.f,630.f,94.f, -8790.f,630.f,94.f, -8790.f,650.f,94.f};
    std::vector<unsigned char> bytes;
    // Init: generation 1, centre == origin, finite heading at offset 32.
    assert(Packet(bytes,true,1820,1,origin,origin,1.5f,tris));
    assert(bytes.size()==44+36);
    assert(skatetransport::Get(bytes.data())==1820 && skatetransport::Get(bytes.data()+4)==1);
    for (int i=0;i<3;++i) assert(skatetransport::GetFloat(bytes.data()+8+i*4)==origin[i] && skatetransport::GetFloat(bytes.data()+20+i*4)==origin[i]);
    assert(skatetransport::GetFloat(bytes.data()+32)==1.5f);
    assert(skatetransport::Get(bytes.data()+36)==0 && skatetransport::Get(bytes.data()+40)==1);
    for (int i=0;i<9;++i) assert(skatetransport::GetFloat(bytes.data()+44+i*4)==tris[i]); // vertex order kept
    assert(!Packet(bytes,true,0,2,origin,origin,0,tris) && bytes.empty());   // init is generation 1
    assert(!Packet(bytes,true,0,1,origin,moved,0,tris));                     // init centre must equal origin
    assert(!Packet(bytes,true,0,1,origin,origin,7.5f,tris));                 // |heading| <= 7
    // Refresh: map 0 is opaque, reserved heading word zero, centre may move.
    assert(Packet(bytes,false,0,2,origin,moved,3.f,tris));
    assert(skatetransport::Get(bytes.data())==0 && skatetransport::Get(bytes.data()+4)==2 && skatetransport::Get(bytes.data()+32)==0);
    assert(skatetransport::GetFloat(bytes.data()+20)==moved[0]);
    assert(!Packet(bytes,false,0,0,origin,moved,0,tris));
    assert(!Packet(bytes,false,0,2,origin,moved,0,{}));                      // zero triangles rejected
    std::vector<float> partial(tris.begin(),tris.end()-1);
    assert(!Packet(bytes,false,0,2,origin,moved,0,partial));
    std::vector<float> full(size_t(MaxTriangles)*9,1.f);
    assert(Packet(bytes,false,0,2,origin,moved,0,full) && bytes.size()==1769516);
    full.resize(full.size()+9,1.f);
    assert(!Packet(bytes,false,0,2,origin,moved,0,full));

    // Triangles the helper would reject never reach the wire.
    assert(Keep(tris.data(),origin));
    const float flat[9]{1,1,1, 1,1,1, 2,2,2};
    assert(!Keep(flat,origin));
    float distant[9]; for (int i=0;i<9;++i) distant[i]=tris[i]+(i%3==0?20000.f:0.f);
    assert(!Keep(distant,origin));
    float nan[9]; for (int i=0;i<9;++i) nan[i]=tris[i];
    nan[4]=std::nanf("");
    assert(!Keep(nan,origin));

    // TRUNCATED gathers shrink the horizontal box 24 -> 12 -> 6 yd, then give up (old world stays).
    assert(Shrink(HalfXY)==12.f && Shrink(12.f)==6.f && Shrink(6.f)==0.f && Shrink(8.f)==MinHalfXY);
    assert(RefreshDistance(HalfXY)==8.f && RefreshDistance(6.f)==2.f);

    // Generations: strictly increasing; only the latest accepted one is awaited.
    auto status=[](uint32_t queued,uint32_t installed,uint32_t failed) {
        std::vector<unsigned char> r; for (uint32_t v : {1820u,queued,installed,failed}) skatetransport::Word(r,v); return r;
    };
    Generations g;
    g.Initialized(); assert(g.installed==1 && g.Next()==2 && !g.awaiting);
    g.Queued(2); g.Queued(3);                                   // 3 supersedes 2
    assert(g.Next()==4 && g.awaiting==3);
    assert(g.Status(status(3,2,0))==0 && g.awaiting==3);        // installing 2 does not finish 3
    assert(g.Status(status(3,2,2))==0);                         // superseded 2 failing is ignored
    assert(g.Status(status(3,3,2))==1 && !g.awaiting && g.installed==3);
    g.Queued(g.Next());
    assert(g.Status(status(4,3,4))==-1 && !g.awaiting && g.installed==3 && g.Next()==5); // failure keeps old world
    assert(g.Status({})==-2);
}
