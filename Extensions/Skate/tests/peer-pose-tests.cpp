#include "SkatePeerPose.h"
#include <cassert>
int main() {
    using namespace skatepeerpose;
    static_assert(Bytes==122 && TextBytes==244 && TextBytes+6<255,"addon message including prefix separator");
    for(unsigned i=0;i<1000;++i) {
        const float t=i*.017f;
        Q q{sinf(t)*.6f,cosf(t)*.6f,sinf(t*.7f)*.5f,cosf(t*.7f)*.5f};
        const float n=sqrtf(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w); q={q.x/n,q.y/n,q.z/n,q.w/n};
        uint32_t bits; Q decoded; assert(Pack(q,bits) && Unpack(bits,decoded));
        const float dot=fabsf(q.x*decoded.x+q.y*decoded.y+q.z*decoded.z+q.w*decoded.w);
        assert(2*acosf((std::min)(1.f,dot))<.005f);
    }
    Q q; assert(!Unpack(512,q));
    assert(!Unpack((3u<<30)|511u|(511u<<10)|(511u<<20),q)); // sum of retained squares > 1
    assert(!Unpack((3u<<30)|511u|(511u<<10),q)); // omitted component cannot be the largest
    uint32_t packed; assert(!Pack({NAN,0,0,1},packed) && !Pack({0,0,0,0},packed));
    Packet p; p.epoch=123; p.sequence=5; p.map=1820; p.mesh=42; p.rig=21; p.worldOffset={1,-2,3};
    auto text=Encode(p); Packet back;
    assert(text.size()==TextBytes && Decode(text.data(),text.size(),back));
    assert(back.epoch==123 && back.sequence==5 && back.worldOffset.y==-2);
    assert(!Decode(text.data(),text.size()-2,back));
    auto bad=text; bad[58]='0'; bad[59]='0'; assert(!Decode(bad.data(),bad.size(),back));
    p.worldOffset.x=8.1f; assert(Encode(p).empty());
    skatepeer::Packet board; board.epoch=123; board.map=1820; board.mesh=42; board.state.sequence=5; board.state.flags=skatepeer::Active;
    assert(Match(back,board));
    ++board.state.sequence; assert(!Match(back,board)); // a pose cannot precede/follow another board sequence
    --board.state.sequence; ++board.epoch; assert(!Match(back,board));
    --board.epoch; board.state.flags=0; assert(!Match(back,board));
    board.state.flags=skatepeer::Active|skatepeer::Hidden; assert(!Match(back,board));
    struct Model { bool m_boneHook=false; } a,b;
    Hook<Model> hook; hook.Own(&a); assert(a.m_boneHook); hook.Release(); assert(!a.m_boneHook);
    a.m_boneHook=true; hook.Own(&a); hook.Release(); assert(a.m_boneHook); // preserve another listener's ownership
    a.m_boneHook=false; hook.Own(&a); hook.Own(&b); assert(!a.m_boneHook && b.m_boneHook);
    hook.Release(); assert(!b.m_boneHook); // stale/off/roster/map release
    hook.Own(&a); hook.Forget(&a); a.m_boneHook=false; hook.Release(); assert(!a.m_boneHook); // destroyed model is never touched/released
    Presentation presentation;
    board.state.flags=skatepeer::Active; board.epoch=123; board.state.sequence=5;
    back.epoch=123; back.sequence=5;
    presentation.Push(board,back,1000,{},true);
    auto nextBoard=board; auto nextPose=back;
    nextBoard.state.sequence=6; nextPose.sequence=6;
    nextBoard.state.offset[0]=1; nextPose.worldOffset.x=2;
    const Q turn{0,0,sinf(.6f),cosf(.6f)};
    memcpy(nextBoard.state.rotation,&turn,sizeof(turn)); nextPose.model=turn;
    presentation.Push(nextBoard,nextPose,1100,{},true);
    skatepeer::Packet shown; Packet body;
    presentation.Sample(1150,shown,body);
    assert(fabsf(shown.state.offset[0]-.5f)<1e-5f && fabsf(body.worldOffset.x-1.5f)<1e-5f);
    assert(fabsf(shown.state.rotation[2]-body.model.z)<1e-5f); // one phase for deck and rider
    nextBoard.state.sequence=7; nextPose.sequence=7; nextBoard.state.offset[0]=2;
    presentation.Push(nextBoard,nextPose,1150,{},true); presentation.Sample(1150,shown,body);
    assert(fabsf(shown.state.offset[0]-.5f)<1e-5f); // jittered early update continues from the displayed pair, no snap
    presentation.Sample(1170,shown,body); assert(shown.state.offset[0]>.5f && shown.state.offset[0]<1);
    nextPose.epoch=124; nextBoard.epoch=124; presentation.Push(nextBoard,nextPose,1180,{},true);
    presentation.Sample(1180,shown,body); assert(shown.state.offset[0]==2); // never interpolate across epochs
    presentation.Clear(); assert(!presentation.ready);
}
