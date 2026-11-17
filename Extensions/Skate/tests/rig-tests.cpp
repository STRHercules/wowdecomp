// Phase 4: role planning (donor + structural), body-role mapping, stock fallback and the bone-hook write (SkateRig.h).
// Synthetic humanoids in M2 model space (+X forward, +Y left, +Z up, yards), plus the skeleton the client dumped for
// the user's Retail body (fixtures/retail-rig.txt, Logs\SkateRig.txt from the live test of 2026-11-15).
#include "SkateRig.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using skaterig::SourceBone;
std::vector<uint16_t> Keys(const std::vector<SourceBone>& b, size_t size = 27) {
    std::vector<uint16_t> keys(size,0xffff);
    for (uint16_t i=0;i<b.size();++i) if (b[i].key>=0 && size_t(b[i].key)<size) keys[b[i].key]=i;
    return keys;
}
std::vector<SourceBone> Humanoid() {
    return {
        {26,-1,0x200,{0,0,0}},                                              // 0 root
        {5,0,0x200,{0,0,1.0f}}, {4,1,0x200,{0,0,1.2f}}, {-1,2,0x200,{0,0,1.4f}}, // 1 waist, 2 spine, 3 chest
        {-1,3,0x200,{0,0,1.6f}}, {6,4,0x200,{0,0,1.7f}},                    // 4 neck, 5 head
        {2,3,0x200,{0,.15f,1.5f}}, {0,6,0x200,{0,.3f,1.5f}}, {-1,7,0x200,{0,.6f,1.5f}}, {-1,8,0x200,{0,.9f,1.5f}},     // left arm
        {3,3,0x200,{0,-.15f,1.5f}}, {1,10,0x200,{0,-.3f,1.5f}}, {-1,11,0x200,{0,-.6f,1.5f}}, {-1,12,0x200,{0,-.9f,1.5f}}, // right arm
        {-1,1,0x200,{0,.1f,1.0f}}, {-1,14,0x200,{0,.1f,.55f}}, {-1,15,0x200,{0,.1f,.1f}}, {-1,16,0x200,{.15f,.1f,.02f}},     // left leg
        {-1,1,0x200,{0,-.1f,1.0f}}, {-1,18,0x200,{0,-.1f,.55f}}, {-1,19,0x200,{0,-.1f,.1f}}, {-1,20,0x200,{.15f,-.1f,.02f}}, // right leg
        {-1,9,0x200,{0,1.f,1.5f}},                                          // 22 finger: not a body role
    };
}
int32_t RoleBone(const skaterig::Rig& rig, unsigned role) { return int32_t(rig.model[rig.plan[role]]); }
// A Source skeleton in Skate metres (y up) matching the rig's bind pose.
skatevisual::Frame BindFrame(const std::vector<SourceBone>& b, const skaterig::Rig& rig) {
    skatevisual::Frame f{};
    f.pose.tick=1; f.pose.ground=1;
    const int source[24]={3,2,-1,4,-1,9,-1,16, 11,18,31,38, 10,17,30,36, 8,15,22,33, 7,14,20,32}; // roles per anchor
    for (unsigned i=0;i<24;++i) {
        const auto m=source[i]<0 ? b[RoleBone(rig,3)].pivot : b[RoleBone(rig,unsigned(source[i]))].pivot;
        f.anchors[i]={m.x*.9144f,m.z*.9144f,-m.y*.9144f};
    }
    return f;
}
void Turn(skatevisual::Frame& f) { for (auto& a : f.anchors) { const float x=a.x; a.x=a.z; a.z=-x; } } // model (x,y) -> (-y,x)
bool Near(float a,float b) { return fabsf(a-b)<1e-3f; }
bool Load(std::vector<SourceBone>& bones,std::vector<uint16_t>& keys) {
    std::string path=__FILE__; path=path.substr(0,path.find_last_of("/\\"))+"/fixtures/retail-rig.txt";
    std::ifstream in(path); std::string line;
    if (!std::getline(in,line) || !std::getline(in,line)) return false;
    while (std::getline(in,line)) {
        std::istringstream row(line); uint32_t index; SourceBone b; std::string flags;
        if (!(row>>index>>b.key>>b.parent>>flags>>b.pivot.x>>b.pivot.y>>b.pivot.z) || index!=bones.size()) return false;
        b.flags=uint32_t(std::stoul(flags,nullptr,16)); bones.push_back(b);
    }
    keys=Keys(bones,291);
    return bones.size()==216;
}

int main() {
    unsigned checks=0;
    auto bones=Humanoid(); auto keys=Keys(bones);
    static skaterig::Rig rig; bool donor=false;
    assert(!skaterig::Plan(bones.data(),uint32_t(bones.size()),keys.data(),27,rig,&donor) && donor); ++checks;
    assert(RoleBone(rig,3)==1 && RoleBone(rig,16)==5 && RoleBone(rig,36)==9 && RoleBone(rig,20)==16 && RoleBone(rig,33)==21); ++checks;
    skaterig::Pose pose; const char* why=nullptr;
    assert(skaterig::Bind(bones.data(),uint32_t(bones.size()),rig,pose,&why) && !*why); ++checks;
    // The structural planner agrees with the donor on a plain Wrath rig.
    { skaterig::Roles r; assert(!skaterig::Structural(bones.data(),uint32_t(bones.size()),keys.data(),27,r));
      for (unsigned role : skaterig::BodyRoles) assert(r.bone[role]==RoleBone(rig,role)); ++checks; }

    // Bind-pose Source frame: identity rotations, pivot translation; the root also carries the offset.
    static characterretarget::Pose target;
    skaterig::Rider rider; skaterig::Point offset{};
    auto frame=BindFrame(bones,rig);
    assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,100,0,{},{},1,1,target,offset)); ++checks;
    assert(skaterig::Fill(bones.data(),rig,target,{0,0,.2f},pose)); ++checks;
    float local[16]{};
    assert(skaterig::Apply(pose,5,local) && Near(local[0],1) && Near(local[5],1) && Near(local[10],1)); ++checks;
    assert(Near(local[12],0) && Near(local[13],0) && Near(local[14],1.7f) && local[3]==0 && local[15]==0); ++checks; // 4th column untouched
    // A parallel lower-leg branch must follow the knee, not the thigh. Attachment-only and finger branches stay stock.
    {
        auto twins=bones;
        twins.push_back({-1,14,0x200,bones[15].pivot}); // parallel knee, same parent role/pivot as the main knee
        twins.push_back({-1,23,0x200,bones[16].pivot}); // parallel ankle
        twins.push_back({-1,23,0x204,{0,.2f,.3f}});    // ignores parent: this whole branch stays stock
        twins.push_back({-1,25,0x200,{0,.2f,.2f}});
        skaterig::Pose twinPose;
        assert(skaterig::Bind(twins.data(),uint32_t(twins.size()),rig,twinPose));
        assert(twinPose.entries[twinPose.slot[23]].source==twinPose.entries[twinPose.slot[15]].source);
        assert(twinPose.entries[twinPose.slot[24]].source==twinPose.entries[twinPose.slot[16]].source);
        assert(twinPose.slot[25]<0 && twinPose.slot[26]<0 && twinPose.slot[22]<0); ++checks;
    }
    assert(skaterig::Apply(pose,0,local) && Near(local[14],.2f)); ++checks;
    // Rotation lives on a co-located child; its passive skin wrapper must remain on the shin palette.
    {
        auto wrapped=bones;
        for (auto& b:wrapped) if (b.parent>=17) ++b.parent;
        wrapped.insert(wrapped.begin()+17,{-1,16,0x200,bones[16].pivot,399});
        wrapped[18].parent=17;
        for (auto& b:wrapped) if (b.parent>=22) ++b.parent;
        wrapped.insert(wrapped.begin()+22,{-1,21,0x200,bones[20].pivot,399});
        wrapped[23].parent=22;
        wrapped[16].rotationSequences=2; wrapped[21].rotationSequences=2;
        const auto wrappedKeys=Keys(wrapped);
        static skaterig::Rig planned;
        skaterig::Roles roles;
        assert(!skaterig::Structural(wrapped.data(),uint32_t(wrapped.size()),wrappedKeys.data(),27,roles));
        assert(roles.bone[20]==17 && roles.bone[22]==22);
        assert(!skaterig::Reduce(wrapped.data(),uint32_t(wrapped.size()),roles,planned));
        skaterig::Pose bound;
        assert(skaterig::Bind(wrapped.data(),uint32_t(wrapped.size()),planned,bound));
        assert(bound.entries[bound.slot[16]].source==bound.entries[bound.slot[15]].source);
        assert(bound.entries[bound.slot[17]].source!=bound.entries[bound.slot[16]].source);
        ++checks;
    }
    // Unmapped bones (fingers, helpers, anything outside the model) keep the stock local.
    float stock[16]; for (int i=0;i<16;++i) stock[i]=local[i]=float(i);
    assert(!skaterig::Apply(pose,22,local) && !memcmp(local,stock,sizeof(stock))); ++checks;
    assert(!skaterig::Apply(pose,9999,local)); ++checks;
    // Bent left elbow: the elbow's local rotation turns the forearm forward (+X).
    frame.anchors[15]={frame.anchors[14].x+.3f*.9144f,frame.anchors[14].y,frame.anchors[14].z};
    assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,100,0,{},{},2,1,target,offset)); ++checks;
    assert(skaterig::Fill(bones.data(),rig,target,offset,pose) && skaterig::Apply(pose,8,local)); ++checks;
    assert(Near(local[0],0) && Near(fabsf(local[1]),1)); ++checks;
    // Sideways stance on a rig with an animated root: the yaw sits on the root, the waist stays identity.
    frame=BindFrame(bones,rig); Turn(frame);
    assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,100,0,{},{},3,1,target,offset) && skaterig::Fill(bones.data(),rig,target,{},pose));
    assert(skaterig::Apply(pose,0,local) && Near(local[1],1) && skaterig::Apply(pose,1,local) && Near(local[0],1)); ++checks;

    // Ragdoll (wipeout 300) solves the full 3D torso and places the root at the Source hips, bounded.
    frame=BindFrame(bones,rig);
    assert(skaterig::Ragdoll(300) && skaterig::Ragdoll(702) && !skaterig::Ragdoll(100)); ++checks;
    assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,300,0,{0,0,0},{0,0,0},4,1,target,offset)); ++checks;
    assert(Near(offset.z,0) && skaterig::Fill(bones.data(),rig,target,offset,pose)); ++checks;
    // Regression (Phase 4 live test): a tumbling bail leaves the Source body more than 4.5 yd from the character; the
    // donor refused that, so no bail was ever drawn. The ragdoll now stays on the character at the hip height.
    skaterig::Rider fresh;
    auto away=frame; // hips 5 yd from the character
    assert(skaterig::Solve(fresh,rig.skeleton,nullptr,away,300,0,{0,0,0},{-5,0,0},5,1,target,offset)); ++checks;
    assert(offset.x==0 && offset.y==0 && Near(offset.z,0)); ++checks;
    auto raised=frame; for (auto& a : raised.anchors) a.y+=.9144f; // body 1 yd up (thrown), still far away
    assert(skaterig::Solve(fresh,rig.skeleton,nullptr,raised,300,0,{0,0,0},{-5,0,0},5,1,target,offset) && Near(offset.z,1)); ++checks;
    // Regression: a flip or fall tips the torso past what the riding solver takes (it needs an upright chest); the
    // frame still poses through the full-3D solver instead of going stock.
    {
        auto tipped=BindFrame(bones,rig);
        for (auto& a : tipped.anchors) { const float up=a.y; a.y=-a.x+1.2f; a.x=up; } // pitched 90 degrees forward
        static characterretarget::Pose riding; assert(!characterretarget::Solve(rig.skeleton,tipped,0,riding));
        const char* reason="";
        assert(skaterig::Solve(fresh,rig.skeleton,nullptr,tipped,201,0,{},{},6,1,target,offset,&reason)); ++checks;
        assert(skaterig::Fill(bones.data(),rig,target,offset,pose)); ++checks;
    }
    // A frame that collapses (all anchors on one point) is rejected with a reason, never a NaN pose.
    auto flat=frame; for (auto& a : flat.anchors) a={0,0,0};
    const char* reason="";
    assert(!skaterig::Solve(rider,rig.skeleton,nullptr,flat,100,0,{},{},6,1,target,offset,&reason) && !strcmp(reason,"air frame did not solve")); ++checks;
    assert(!skaterig::Solve(rider,rig.skeleton,nullptr,flat,300,0,{},{},6,1,target,offset,&reason) && !strcmp(reason,"bail frame did not solve")); ++checks;

    // Non-humanoid / missing head key: no plan, so no hook writes at all.
    auto creature=bones; creature[5].key=-1; auto creatureKeys=Keys(creature);
    why=skaterig::Plan(creature.data(),uint32_t(creature.size()),creatureKeys.data(),27,rig);
    assert(why && !strcmp(why,"no head key bone (6)")); ++checks;
    // A posed bone with ignore-parent flags (the neck) refuses the rig (stock) instead of a half pose.
    auto odd=bones; odd[4].flags|=0x4;
    assert(!skaterig::Plan(odd.data(),uint32_t(odd.size()),keys.data(),27,rig));
    assert(!skaterig::Bind(odd.data(),uint32_t(odd.size()),rig,pose,&why) && *why && !skaterig::Apply(pose,4,local)); ++checks;
    // A child listed before its parent is malformed: refused.
    auto bad=bones; bad[3].parent=4;
    why=skaterig::Plan(bad.data(),uint32_t(bad.size()),keys.data(),27,rig); assert(why && strstr(why,"before its parent")); ++checks;
    // Over 256 bones: the body still maps, wherever it sits; extra bones stay stock.
    {
        auto big=bones;
        while (big.size()<300) big.push_back({-1,int32_t(big.size()-1),0x200,{0,1.f,1.5f}});
        assert(!skaterig::Plan(big.data(),uint32_t(big.size()),keys.data(),27,rig) && skaterig::Bind(big.data(),uint32_t(big.size()),rig,pose));
        assert(pose.slot.size()==300 && pose.slot[299]<0); ++checks;
        std::vector<SourceBone> late(260,{-1,0,0x200,{0,0,.5f}}); late[0]={-1,-1,0x200,{0,0,0}};
        for (auto b : bones) { b.parent=b.parent>=0 ? b.parent+260 : 0; late.push_back(b); }
        auto lateKeys=Keys(late);
        assert(!skaterig::Plan(late.data(),uint32_t(late.size()),lateKeys.data(),27,rig) && RoleBone(rig,16)==265); ++checks;
        assert(skaterig::Bind(late.data(),uint32_t(late.size()),rig,pose) && pose.slot[1]<0); ++checks;
    }

    // Retail-shaped synthetic rig: limbs flagged 0x280, a root the engine does not animate (flags 0) and an animated
    // helper between waist and spine. The root's yaw and offset move to its animated children; the helper is posed
    // (identity) so it cannot add the run cycle back.
    {
        auto retail=bones;
        for (auto& b : retail) { if (b.parent>=2) ++b.parent; b.flags=0x280; }
        retail.insert(retail.begin()+2,SourceBone{-1,1,0x280,{0,0,1.1f}}); // 2 helper, child of the waist
        retail[3].parent=2; retail[0].flags=0;
        auto retailKeys=Keys(retail);
        assert(!skaterig::Plan(retail.data(),uint32_t(retail.size()),retailKeys.data(),27,rig)); ++checks;
        assert(skaterig::Bind(retail.data(),uint32_t(retail.size()),rig,pose,&why) && !*why); ++checks;
        assert(pose.slot[0]<0 && pose.slot[2]>=0 && pose.slot[23]<0); ++checks;
        frame=BindFrame(retail,rig); Turn(frame);
        assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,100,0,{},{},7,1,target,offset)); ++checks;
        assert(skaterig::Fill(retail.data(),rig,target,{0,0,.2f},pose)); ++checks;
        float m[16]{};
        assert(skaterig::Apply(pose,1,m) && Near(m[0],0) && Near(m[1],1) && Near(m[14],1.2f)); ++checks; // waist: yaw + offset
        assert(skaterig::Apply(pose,2,m) && Near(m[0],1) && Near(m[14],1.1f)); ++checks;                  // helper: identity
        assert(skaterig::Apply(pose,3,m) && Near(m[0],1) && Near(m[5],1)); ++checks;                      // spine
    }

    // The user's Retail body (216 bones, 291 key bones, no Waist key): the donor refuses, the structural planner maps
    // all 23 roles with the pelvis that forks the legs, clavicles meeting at the chest, and limbs on their sides.
    {
        std::vector<SourceBone> rb; std::vector<uint16_t> rk;
        assert(Load(rb,rk)); ++checks;
        donor=true;
        why=skaterig::Plan(rb.data(),uint32_t(rb.size()),rk.data(),uint32_t(rk.size()),rig,&donor);
        if (why) printf("retail plan: %s\n",why);
        assert(!why && !donor); ++checks;
        assert(RoleBone(rig,0)==0 && RoleBone(rig,1)==1 && RoleBone(rig,3)==2 && RoleBone(rig,2)==3); ++checks; // actor, root, pelvis, spine
        assert(RoleBone(rig,4)==16 && RoleBone(rig,9)==23 && RoleBone(rig,16)==39); ++checks;               // chest, neck, head
        assert(RoleBone(rig,10)==21 && RoleBone(rig,11)==22 && RoleBone(rig,17)==27 && RoleBone(rig,18)==28); ++checks;
        assert(RoleBone(rig,7)==6 && RoleBone(rig,8)==5 && RoleBone(rig,14)==13 && RoleBone(rig,15)==12); ++checks; // thighs, knees
        assert(RoleBone(rig,20)==19 && RoleBone(rig,22)==18 && RoleBone(rig,32)==32 && RoleBone(rig,33)==31); ++checks; // ankles, toes
        for (unsigned side=0;side<2;++side) {
            const auto u=rb[RoleBone(rig,17+side)].pivot,e=rb[RoleBone(rig,30+side)].pivot,h=rb[RoleBone(rig,36+2*side)].pivot;
            assert(u.z>e.z && e.z>h.z && (side ? h.y<e.y : h.y>e.y));                                        // arm hangs down and out
        }
        ++checks;
        assert(skaterig::Bind(rb.data(),uint32_t(rb.size()),rig,pose,&why) && !*why); ++checks;
        assert(pose.slot[9]>=0 && pose.slot[25]>=0 && pose.slot[44]<0 && pose.slot[56]<0); ++checks; // spine helper, ankle helper posed; origin helper, face stock
        frame=BindFrame(rb,rig); Turn(frame);
        assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,100,0,{},{},8,1,target,offset)); ++checks;
        assert(skaterig::Fill(rb.data(),rig,target,{},pose) && skaterig::Apply(pose,0,local) && Near(fabsf(local[1]),1)); ++checks; // stance yaw on the actor
        assert(skaterig::Solve(rider,rig.skeleton,nullptr,frame,300,0,{},{},9,1,target,offset)); ++checks;          // ragdoll solves too
    }
    printf("{\"passed\":true,\"checks\":%u}\n",checks);
}
