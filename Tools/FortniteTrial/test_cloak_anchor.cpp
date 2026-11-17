#include "Engine/Source/Retail/CloakAnchor.h"
#include "Engine/Source/Retail/BackpackCatalog.h"
#include "Engine/Source/Retail/CosmeticPreviewFit.h"
#include "Engine/Source/Retail/AttachmentBindingPolicy.h"
#include "Engine/Source/Retail/BackpackFit.h"
#include "Engine/Source/Retail/BackpackFitExport.h"
#include "Engine/Source/Lumenveil/WetExclusions.h"
#include "Engine/Source/Retail/PickaxeDefaults.h"
#include "Engine/Source/Retail/PickaxeCatalog.h"
#include <cassert>
#include <limits>

int main() {
    assert(Retail::PickaxeDisplay("item/objectcomponents/weapon/esteria_rainbowsmash.m2")==3430001);
    assert(Retail::PickaxeDisplay("item/objectcomponents/weapon/esteria_fortnite_3430083.m2")==3430083);
    assert(!Retail::PickaxeDisplay("item/objectcomponents/weapon/axe_2h_battle_b_01.m2"));
    assert(Retail::IsPickaxeDisplay(3430083) && Retail::IsPickaxeDisplay(3430086) && !Retail::IsPickaxeDisplay(3430087));
    const auto drawn=Retail::PickaxeDefaultFit(3430001,false),sheathed=Retail::PickaxeDefaultFit(3430001,true);
    assert(drawn[0]==1.26f && drawn[1]==.25f && drawn[2]==0.035f && drawn[3]==-0.085f && drawn[4]==180);
    assert(sheathed[1]==0.29f && sheathed[2]==-0.06f && sheathed[3]==0.06f && sheathed[5]==177.5f);
    Lumenveil::RegisterWetExclusion(reinterpret_cast<void*>(1),{1,2,3},{4,5,6});
    Lumenveil::RegisterWetExclusion(reinterpret_cast<void*>(1),{2,3,4},{5,6,7});
    assert(Lumenveil::CurrentWetExclusions().size()==1);
    assert(Lumenveil::CurrentWetExclusions()[0].low[0]==2);
    Lumenveil::wetExclusions.begin()->second.seen-=std::chrono::seconds(1);
    assert(Lumenveil::CurrentWetExclusions().empty());
    Lumenveil::RegisterWetExclusion(reinterpret_cast<void*>(2),{0,0,0},{1,1,1});
    assert(Lumenveil::wetExclusions.size()==1);
    assert(Retail::ValidBackpackFitExport("3430001;retail/a.m2;1;0;0;0;0;0;0;drawn\n3430001;retail/a.m2;1;0;0;.3;0;0;0;sheathed\n"));
    assert(!Retail::ValidBackpackFitExport("3430001;retail/a.m2;1;0;0;0;0;0;0;other\n"));
    assert(Retail::ValidBackpackFitExport("3430001;retail/a.m2;1;0.1;0;0;15;-30;0\n"));
    assert(!Retail::ValidBackpackFitExport("3430001;retail/a.m2;1;0;0;0;200;0;0\n"));
    assert(Retail::ValidBackpackFitExport("# all saved profiles\n3400001;retail/a.m2;1;0;0;0\n3400002;retail/b.m2;1.25;-0.1;0;0.05\n"));
    assert(!Retail::ValidBackpackFitExport("3400001;retail/a.m2;1;0;0;nan\n"));
    assert(!Retail::ValidBackpackFitExport("3400001;retail/a.m2;1;0;0\n"));
    assert(!Retail::ValidBackpackFitExport("3400001;retail/a.m2;1;0;0;0;extra\n"));
    assert(Retail::ValidBackpackFit({1,0,0,0}));
    assert(!Retail::ValidBackpackFit({0,0,0,0}));
    assert(!Retail::ValidBackpackFit({1,4,0,0}));
    assert(!Retail::ValidBackpackFit({1,0,0,std::numeric_limits<float>::quiet_NaN()}));
    assert(Retail::ValidBackpackFit({1,0,0,0,15,-30,90}));
    assert(!Retail::ValidBackpackFit({1,0,0,0,181,0,0}));
    assert(Retail::ReuseBackpackFit(true,false,true));
    assert(!Retail::ReuseBackpackFit(true,false,false)); // A resource reload drops the frame even if fit metadata survives.
    assert(!Retail::ReuseBackpackFit(true,true,true));
    assert(!Retail::ReuseBackpackFit(false,false,true));
    assert(Retail::BackpackFitPending(true,false,false,false)); // First asynchronous load needs geometry-stage retry.
    assert(Retail::BackpackFitPending(true,true,false,true)); // Loaded mesh alone does not finalize body geometry.
    assert(!Retail::BackpackFitPending(true,true,true,true));
    assert(Retail::BackpackFitPending(true,true,true,false)); // Parent resource refresh invalidates the mount.
    assert(!Retail::BackpackFitPending(false,false,false,false));
    using namespace Retail;
    std::vector<CloakVertex> vertices = {
        {{-0.2f,-0.3f,1.5f}, {2,0,0,0}, {255,0,0,0}},
        {{-0.2f, 0.3f,1.5f}, {2,0,0,0}, {255,0,0,0}},
        {{-0.3f,-0.4f,0.5f}, {7,0,0,0}, {255,0,0,0}},
        {{-0.3f, 0.4f,0.5f}, {7,0,0,0}, {255,0,0,0}}
    };
    CloakAnchor frame;
    assert(FindCloakAnchor(vertices, 8, frame));
    assert(frame.bone == 2 && std::fabs(frame.width - 0.6f) < 0.0001f);
    assert(frame.position[2] == 1.5f && frame.position[1] == 0);
    CloakAnchor rigid;
    assert(FindCloakAnchor(vertices,8,rigid,3));
    assert(rigid.bone==3 && rigid.position==frame.position && rigid.width==frame.width);
    assert(FindCloakAnchor(vertices,8,rigid,0xffffffffu) && rigid.bone==2);
    const std::vector<std::array<float,3>> surface = {
        {-0.2f,-0.3f,1.5f}, {-0.2f,0.3f,1.5f},
        {-0.4f,0,1.1f}, // The back protrudes below the neck.
        {-0.9f,0.7f,1.3f}, // An arm outside the shield must not push it away.
        {-0.8f,0,0.5f} // Neither should the hips below the shield.
    };
    assert(FitCloakBack(surface, frame));
    assert(std::fabs(frame.position[0] + 0.330f) < 0.0001f);
    assert(!FitCloakBack({}, frame));
    assert(!FindCloakAnchor(vertices, 2, frame));
    vertices[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    assert(!FindCloakAnchor(vertices, 8, frame));
    assert(!FindCloakAnchor({}, 8, frame));
    std::map<uint32_t, BackpackAppearance> catalog;
    assert(ParseBackpacks("3400001;custom/fortnite/shield.m2;.373;.37;.075;1;0;0;0;15;-30;90\n",catalog));
    assert(catalog.at(3400001).fit.pitch==15 && catalog.at(3400001).fit.yaw==-30 && catalog.at(3400001).fit.roll==90);
    assert(ParseBackpacks("3400001;custom/fortnite/shield.m2;0.373;0.37;0.075\n", catalog));
    assert(catalog.size() == 1 && catalog.at(3400001).height == 0.37f);
    assert(catalog.at(3400001).fit.scale == 1 && catalog.at(3400001).fit.z == 0);
    assert(ParseBackpacks("3400001;custom/fortnite/shield.m2;0.373;0.37;0.075;1.43;0;-0.06;0.035\n", catalog));
    assert(catalog.at(3400001).fit.scale == 1.43f && catalog.at(3400001).fit.y == -0.06f);
    assert(catalog.at(3400001).fit.z == 0.035f);
    for (const char* tail : {";1;0;0", ";1;0;0;0;extra", ";nan;0;0;0", ";4.01;0;0;0", ";1;0;0;3.01"})
        assert(!ParseBackpacks(std::string("3400001;custom/fortnite/shield.m2;1;1;1") + tail, catalog));
    assert(catalog.at(3400001).fit.scale == 1.43f); // Invalid fits preserve the last complete catalog.
    assert(!ParseBackpacks("3400001;custom/fortnite/../shield.m2;1;1;1", catalog));
    assert(!ParseBackpacks("3400001;custom/fortnite/shield.m2;0;1;1", catalog));
    assert(!ParseBackpacks("3400001;custom/fortnite/shield.m2;1;1;1\n3400001;custom/fortnite/other.m2;1;1;1", catalog));
    assert(catalog.size() == 1); // Invalid catalogs cannot partially replace the last valid one.
    const auto wide = FitCosmeticBounds({-1,-1,-3},{1,1,0},0,1,1);
    const auto narrow = FitCosmeticBounds({-1,-1,-3},{1,1,0},0,0.5f,1);
    assert(narrow.distance > wide.distance && narrow.center[2] == -1.5f);
    const auto turned = FitCosmeticBounds({1,0,0},{3,2,2},3.14159265f,1,1);
    assert(std::fabs(turned.center[0]+2)<0.0001f && std::fabs(turned.center[1]+1)<0.0001f);
}
