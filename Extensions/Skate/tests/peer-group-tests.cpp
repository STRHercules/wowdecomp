#include "SkatePeerPose.h"
#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <cassert>
#include <windows.h>
int64_t OsGetAsyncClocksPerSecond() { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x.QuadPart; }
uint64_t OsGetAsyncTimeClocks() { LARGE_INTEGER x; QueryPerformanceCounter(&x); return x.QuadPart; }
void OsValidateFunctionPointer(const void* p) { assert(p); }

int main() {
    using namespace skatepeer;
    Packet p; p.epoch=123; p.map=1820; p.mesh=42; p.state.flags=Active; p.state.sequence=65534;
    auto text=Encode(p); Packet decoded;
    assert(text.size()==70 && Decode(text.data(),text.size(),decoded));
    assert(decoded.map==1820 && decoded.epoch==123 && decoded.mesh==42);
    auto bad=text; bad[8]='G'; assert(!Decode(bad.data(),bad.size(),decoded));
    assert(!Decode(text.data(),69,decoded));
    assert(Token("party1") && Token("raid40") && !Token("raid41") && !Token("target") && !Token("party01"));
    Lease lease; assert(lease.Accept(p,1000) && !lease.Accept(p,1100));
    p.state.sequence=65535; assert(lease.Accept(p,1100)); p.state.sequence=0; assert(lease.Accept(p,1200));
    p.epoch=124; p.state.sequence=1; assert(lease.Accept(p,1300));
    p.epoch=123; p.state.sequence=1; assert(!lease.Accept(p,1400));
    assert(lease.Visible(2799) && !lease.Visible(2800));

    skatevisual::Mesh mesh; mesh.colors.assign(8,0xff32859d); mesh.uv.resize(8);
    mesh.indices={0,1,2,0,2,3}; mesh.materials={0,0};
    std::vector<skatevisual::Point> a={{-.1f,-.03f,-.5f},{.1f,-.03f,-.5f},{.1f,.03f,-.5f},{-.1f,.03f,-.5f},
        {-.1f,-.03f,.5f},{.1f,-.03f,.5f},{.1f,.03f,.5f},{-.1f,.03f,.5f}};
    auto b=a; for(auto& v:b) { float x=v.x; v.x=v.z+2; v.z=-x+3; v.y+=1; }
    skateboard::Rest ar,br; assert(ar.Make(a) && br.Make(b));
    skateboard::M3 ab,bb; assert(Basis(mesh,ar,ab) && Basis(mesh,br,bb));
    p={}; p.epoch=1; p.map=1820; p.mesh=MeshId(mesh); p.state.flags=Active;
    skateboard::Placed placed{}; float rotation[16];
    characterretarget::Matrix({sinf(.2f),0,0,cosf(.2f)},{},rotation);
    placed.scale=1.3f;
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) {
        placed.rotation[r*3+c]=rotation[r*4+c]; placed.matrix[r*4+c]=rotation[r*4+c]*placed.scale;
    }
    placed.matrix[12]=10; placed.matrix[13]=20; placed.matrix[14]=30; placed.matrix[15]=1;
    Capture(p,placed,{1,2,3},ab); text=Encode(p); assert(Decode(text.data(),text.size(),decoded));
    float remote[16]; assert(Matrix(decoded,bb,{1,2,3},remote));
    for(unsigned i=0;i<a.size();++i) {
        const float x=(a[i].x-ar.centre.x)/.9144f,y=-(a[i].z-ar.centre.z)/.9144f,z=(a[i].y-ar.centre.y)/.9144f;
        const float bx=(b[i].x-br.centre.x)/.9144f,by=-(b[i].z-br.centre.z)/.9144f,bz=(b[i].y-br.centre.y)/.9144f;
        for(unsigned k=0;k<3;++k) {
            const float want=x*placed.matrix[k]+y*placed.matrix[4+k]+z*placed.matrix[8+k]+placed.matrix[12+k];
            const float got=bx*remote[k]+by*remote[4+k]+bz*remote[8+k]+remote[12+k];
            assert(fabsf(want-got)<.002f);
        }
    }
    auto changed=mesh; changed.indices[0]=1; assert(MeshId(changed)!=p.mesh);

    // Execute the actual embedded Lua transport without using any live account or sending a real message.
    lua_State* L=luaL_newstate(); assert(L); luaL_openlibs(L);
    lua_pushlstring(L,text.data(),text.size()); lua_setglobal(L,"packet");
    skatepeerpose::Packet body; body.epoch=p.epoch; body.sequence=p.state.sequence; body.map=p.map; body.mesh=p.mesh; body.rig=1;
    const auto poseText=skatepeerpose::Encode(body); assert(poseText.size()==244);
    lua_pushlstring(L,poseText.data(),poseText.size()); lua_setglobal(L,"posePacket");
    auto next=p; ++next.state.sequence; const auto nextText=Encode(next);
    auto nextBody=body; nextBody.sequence=next.state.sequence; const auto nextPose=skatepeerpose::Encode(nextBody);
    lua_pushlstring(L,nextText.data(),nextText.size()); lua_setglobal(L,"packetNext");
    lua_pushlstring(L,nextPose.data(),nextPose.size()); lua_setglobal(L,"poseNext");
    assert(luaL_dostring(L,R"lua(
now=0 active=false sent=0 received=0 poseReceived=0 party=2 raid=0 poseEnabled=false
function GetTime() return now end
function GetNumPartyMembers() return party end
function GetNumRaidMembers() return raid end
function UnitName(unit) if unit=="party1" or unit=="raid2" then return "Peer" end if unit=="party2" then return "Other" end return "Self" end
function UnitIsUnit(unit,other) return unit=="raid1" end
function CreateFrame() frame={} function frame:RegisterEvent() end function frame:SetScript(k,v) self[k]=v end return frame end
function WrathSkatePeerReceive(unit,text) received=received+1 lastUnit=unit lastText=text end
function WrathSkatePeerPoseReceive(unit,text) poseReceived=poseReceived+1 lastPoseUnit=unit end
function WrathSkatePeerState() return packet,active,poseEnabled and posePacket or nil end
function SendAddonMessage(prefix,text,channel)
 assert((prefix=="WRSP5" and (#text==70 or #text==95)) or (prefix=="WRRP5" and #text==244))
 assert(#prefix+#text+1<255) sent=sent+1 lastChannel=channel
end
)lua")==0);
    assert(luaL_dostring(L,Lua)==0);
    assert(luaL_dostring(L,"function Event(...) frame.OnEvent(...) frame.OnUpdate(frame,0) end")==0);
    assert(luaL_dostring(L,R"lua(
frame.OnUpdate(frame,.11) assert(sent==0)
active=true frame.OnUpdate(frame,.05) assert(sent==0)
frame.OnUpdate(frame,.06) assert(sent==1 and lastChannel=="PARTY")
active=false frame.OnUpdate(frame,.11) assert(sent==2)
frame.OnUpdate(frame,.11) assert(sent==2) -- one off message, not continuous idle spam
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"PARTY","Stranger") assert(received==0)
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"WHISPER","Peer") assert(received==0)
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"PARTY","Peer") assert(received==1 and lastUnit=="party1")
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"PARTY","Peer") assert(received==1)
packet=packetNext posePacket=poseNext now=.1 Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"PARTY","Peer") assert(received==2)
now=.2 Event(frame,"CHAT_MSG_ADDON","WRSP5",packet.."X","PARTY","Peer") assert(received==2)
party=0 Event(frame,"PARTY_MEMBERS_CHANGED")
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"PARTY","Peer") assert(received==2)
raid=2 Event(frame,"RAID_ROSTER_UPDATE") active=true frame.OnUpdate(frame,.11)
assert(lastChannel=="RAID")
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"RAID","Peer") assert(received==3 and lastUnit=="raid2")
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"RAID","Self") assert(received==3)
poseEnabled=true sent=0 now=1
frame.OnUpdate(frame,.11) assert(sent==2) -- paired board then rider pose, same native sequence
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"RAID","Peer") assert(received==3) -- identical state is deduplicated
Event(frame,"CHAT_MSG_ADDON","WRRP5",posePacket,"RAID","Peer") assert(poseReceived==1 and lastPoseUnit=="raid2")
Event(frame,"CHAT_MSG_ADDON","WRRP5",posePacket,"RAID","Peer") assert(poseReceived==1)
now=1.1 Event(frame,"CHAT_MSG_ADDON","WRRP5",posePacket.."X","RAID","Peer") assert(poseReceived==1)
Event(frame,"CHAT_MSG_ADDON","WRRP5",posePacket,"WHISPER","Peer") assert(poseReceived==1)
Event(frame,"CHAT_MSG_ADDON","WRRP5",posePacket,"RAID","Stranger") assert(poseReceived==1)
active=false frame.OnUpdate(frame,.11) assert(sent==3) -- off board only, no stale pose
raid=0 party=0 Event(frame,"PARTY_MEMBERS_CHANGED")
Event(frame,"CHAT_MSG_ADDON","WRRP5",posePacket,"RAID","Peer") assert(poseReceived==1)
)lua")==0);
    p.state.sequence+=2; p.actorKnown=true; p.actor[0]=-9480; p.actor[1]=70; p.actor[2]=56;
    const auto motion=Encode(p); assert(motion.size()==95);
    body.sequence=p.state.sequence; const auto motionPose=skatepeerpose::Encode(body);
    lua_pushlstring(L,motionPose.data(),motionPose.size()); lua_setglobal(L,"posePacket");
    lua_pushlstring(L,motion.data(),motion.size()); lua_setglobal(L,"packet");
    assert(luaL_dostring(L,R"lua(
raid=2 now=2 active=true Event(frame,"RAID_ROSTER_UPDATE")
local before=received
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet,"RAID","Peer") assert(received==before+1)
Event(frame,"CHAT_MSG_ADDON","WRSP5",packet.."X","RAID","Peer") assert(received==before+1)
local beforeSend=sent
frame.OnUpdate(frame,.11) assert(sent==beforeSend+2) -- 95-byte board envelope and paired rider packet
)lua")==0);
    lua_close(L);
}
