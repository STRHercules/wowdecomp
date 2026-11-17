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
    skatepeer::Packet packet; packet.epoch=1; packet.map=1820; packet.mesh=42; packet.state.flags=skatepeer::Active;
    skatepeer::Lease stream;
    for(unsigned i=0;i<100;++i) {
        packet.state.sequence=uint16_t(i);
        const uint32_t now=1000+i*100-(i%2?30:0); // 70/130 ms arrival gaps, still exactly 10 Hz.
        assert(stream.Accept(packet,now));
    }
    skatepeer::Lease burst;
    for(unsigned i=0;i<4;++i) { packet.state.sequence=uint16_t(i); assert(burst.Accept(packet,1000)); }
    packet.state.sequence=4; assert(!burst.Accept(packet,1000));
    assert(burst.Accept(packet,1050)); // Refill is bounded; a rejected packet did not consume its sequence.
    assert(!burst.Accept(packet,1060)); // Duplicates never refresh expiry.
    assert(burst.Visible(2549) && !burst.Visible(2550));

    lua_State* L=luaL_newstate(); assert(L); luaL_openlibs(L);
    for(unsigned i=1;i<=3;++i) {
        packet.state.sequence=uint16_t(i); const auto board=skatepeer::Encode(packet);
        skatepeerpose::Packet pose; pose.epoch=packet.epoch; pose.sequence=packet.state.sequence; pose.map=packet.map; pose.mesh=packet.mesh;
        const auto body=skatepeerpose::Encode(pose);
        lua_pushlstring(L,board.data(),board.size()); lua_setglobal(L,("board"+std::to_string(i)).c_str());
        lua_pushlstring(L,body.data(),body.size()); lua_setglobal(L,("pose"+std::to_string(i)).c_str());
    }
    assert(luaL_dostring(L,R"lua(
now=0 active=true sent=0 received=0 poseReceived=0 party=1
function GetTime() return now end
function GetNumPartyMembers() return party end function GetNumRaidMembers() return 0 end
function UnitName(unit) return unit=="party1" and "Peer" or "Self" end
function UnitIsUnit() return false end
function CreateFrame() frame={} function frame:RegisterEvent() end function frame:SetScript(k,v) self[k]=v end return frame end
function WrathSkatePeerReceive(unit,text) assert(unit=="party1") received=received+1 lastBoard=text return true end
function WrathSkatePeerPoseReceive(unit,text) assert(unit=="party1") poseReceived=poseReceived+1 lastPose=text return true end
function WrathSkatePeerState() return board1,active,pose1 end
function SendAddonMessage(prefix,text,channel) assert(channel=="PARTY") assert(#prefix+#text+1<255) sent=sent+1 end
)lua")==0);
    assert(luaL_dostring(L,skatepeer::Lua)==0);
    assert(luaL_dostring(L,R"lua(
-- Keep the fractional timer remainder: all common display rates sustain the intended ten pairs/second.
for i=1,30 do now=now+.033 frame.OnUpdate(frame,.033) end
assert(sent==18) -- nine pairs in .99 s; discarding the remainder emitted only seven
-- A delayed event pump coalesces to the newest pair, not an older sample followed by a 90 ms rejection.
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board1,"PARTY","Peer")
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRRP5",pose1,"PARTY","Peer")
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board2,"PARTY","Peer")
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRRP5",pose2,"PARTY","Peer")
assert(received==0 and poseReceived==0)
frame.OnUpdate(frame,0)
assert(received==1 and poseReceived==1 and lastBoard==board2 and lastPose==pose2)
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board2,"PARTY","Peer")
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRRP5",pose2,"PARTY","Peer")
frame.OnUpdate(frame,0) assert(received==1 and poseReceived==1)
-- The next fresh pair may arrive quickly after the previous one; it must not be discarded.
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRRP5",pose3,"PARTY","Peer")
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board3,"PARTY","Peer")
frame.OnUpdate(frame,0) assert(received==2 and poseReceived==2 and lastBoard==board3 and lastPose==pose3)
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board1,"WHISPER","Peer")
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board1,"PARTY","Stranger")
frame.OnUpdate(frame,0) assert(received==2)
frame.OnEvent(frame,"CHAT_MSG_ADDON","WRSP5",board1,"PARTY","Peer")
party=0 frame.OnEvent(frame,"PARTY_MEMBERS_CHANGED")
frame.OnUpdate(frame,0) assert(received==2) -- roster changes clear queued state
)lua")==0);
    lua_close(L);
}
