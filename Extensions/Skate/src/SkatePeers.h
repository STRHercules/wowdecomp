#pragma once
#include "SkatePeerWire.h"
#include "SkateBoard.h"
#include <array>

namespace skatepeer {
struct Packet { uint32_t epoch=0,map=0,mesh=0; float scale=1; State state; bool actorKnown=false; float actor[3]{}; };
inline bool ValidActor(const float* p) {
    for(unsigned i=0;i<3;++i) if(!std::isfinite(p[i]) || fabsf(p[i])>1e6f) return false;
    return true;
}
inline bool NearActor(const Packet& packet,skatevisual::Point native) {
    if(!packet.actorKnown) return true;
    if(!ValidActor(packet.actor) || !std::isfinite(native.x) || !std::isfinite(native.y) || !std::isfinite(native.z)) return false;
    const double x=packet.actor[0]-native.x,y=packet.actor[1]-native.y,z=packet.actor[2]-native.z;
    return x*x+y*y+z*z<=16.*16.;
}
inline char Hex(unsigned n) { return "0123456789ABCDEF"[n&15]; }
inline void HexWord(std::string& out,uint32_t value,unsigned digits) {
    for(unsigned i=digits;i--;) out+=Hex(value>>(i*4));
}
inline bool Number(const char* text,unsigned count,uint32_t& value) {
    value=0;
    for(unsigned i=0;i<count;++i) {
        const char c=text[i]; const unsigned n=c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:16;
        if(n>15) return false; value=(value<<4)|n;
    }
    return true;
}
inline std::string Encode(const Packet& packet) {
    uint8_t bytes[Size];
    if(!packet.epoch || !std::isfinite(packet.scale) || packet.scale<.01f || packet.scale>63.9f || !Encode(packet.state,bytes)) return {};
    std::string out; HexWord(out,packet.epoch,8); out+=':'; HexWord(out,packet.map,8); out+=':';
    HexWord(out,packet.mesh,8); out+=':'; HexWord(out,uint32_t(lrintf(packet.scale*1024)),4); out+=':';
    for(uint8_t b:bytes) { out+=Hex(b>>4); out+=Hex(b); }
    if(packet.actorKnown) {
        if(!ValidActor(packet.actor)) return {};
        out+=':';
        for(float v:packet.actor) { uint32_t bits; memcpy(&bits,&v,4); HexWord(out,bits,8); }
    }
    return out;
}
inline bool Decode(const char* text,size_t size,Packet& out) {
    if((size!=70 && size!=95) || text[8]!=':' || text[17]!=':' || text[26]!=':' || text[31]!=':') return false;
    Packet p; uint32_t scale;
    if(!Number(text,8,p.epoch) || !p.epoch || !Number(text+9,8,p.map) || !Number(text+18,8,p.mesh) || !Number(text+27,4,scale)) return false;
    p.scale=scale/1024.f; if(p.scale<.009f || p.scale>63.9f) return false;
    uint8_t bytes[Size];
    for(unsigned i=0;i<Size;++i) { uint32_t n; if(!Number(text+32+i*2,2,n)) return false; bytes[i]=uint8_t(n); }
    if(!Decode(bytes,Size,p.state)) return false;
    if(size==95) {
        if(text[70]!=':') return false;
        for(unsigned i=0;i<3;++i) { uint32_t bits; if(!Number(text+71+i*8,8,bits)) return false; memcpy(p.actor+i,&bits,4); }
        if(!ValidActor(p.actor)) return false;
        p.actorKnown=true;
    }
    out=p; return true;
}
inline bool Token(const std::string& token) {
    for(const auto& prefix:{std::string("party"),std::string("raid")}) {
        const unsigned max=prefix=="party"?4:40;
        for(unsigned n=1;n<=max;++n) if(token==prefix+std::to_string(n)) return true;
    }
    return false;
}
struct Lease {
    bool known=false;
    uint32_t epoch=0,at=0;
    uint32_t creditAt=0;
    float credits=4;
    uint16_t sequence=0;
    std::array<uint32_t,4> retired{};
    bool Accept(const Packet& packet,uint32_t now) {
        if(known && epoch==packet.epoch && !Newer(packet.state.sequence,sequence)) return false;
        if(known && epoch!=packet.epoch) {
            for(auto old:retired) if(old==packet.epoch) return false;
        }
        // Permit normal network bursts without dropping the newer sample; still bound accepted work.
        credits=known?(std::min)(4.f,credits+(now-creditAt)*.02f):4.f;
        creditAt=now;
        if(credits<1) return false;
        credits-=1;
        if(known && epoch!=packet.epoch) {
            std::move(retired.begin()+1,retired.end(),retired.begin()); retired.back()=epoch;
        }
        epoch=packet.epoch; at=now; sequence=packet.state.sequence; known=true; return true;
    }
    bool Visible(uint32_t now) const { return known && now-at<ExpireMs; }
};
// The deck's own stable geometric axes remove helper init-heading/rest-orientation differences between clients.
inline bool Basis(const skatevisual::Mesh& mesh,const skateboard::Rest& rest,skateboard::M3& out) {
    if(mesh.colors.size()!=rest.points.size()) return false;
    int first=-1,second=-1; characterretarget::Point a{},u{};
    const auto model=[](skatevisual::Point p) { return characterretarget::Point{p.x,-p.z,p.y}; };
    for(unsigned i=0;i<mesh.colors.size();++i) {
        if(mesh.colors[i]!=0xff32859d) continue;
        const auto p=model(rest.points[i]);
        if(first<0) { first=int(i); a=p; continue; }
        const auto span=characterretarget::Sub(p,a);
        if(second<0) { if(characterretarget::Length(span)>.05f && characterretarget::Unit(span,u)) second=int(i); continue; }
        auto cross=characterretarget::Cross(u,span); characterretarget::Point w{},v{};
        if(characterretarget::Length(cross)<.03f || !characterretarget::Unit(cross,w)) continue;
        v=characterretarget::Cross(w,u);
        const double values[]{u.x,u.y,u.z,v.x,v.y,v.z,w.x,w.y,w.z}; std::copy(values,values+9,out.a); return true;
    }
    return false;
}
inline uint32_t MeshId(const skatevisual::Mesh& mesh) {
    uint32_t hash=2166136261u;
    const auto bytes=[&](const void* p,size_t size) { auto b=static_cast<const uint8_t*>(p); for(size_t i=0;i<size;++i) { hash^=b[i]; hash*=16777619u; } };
    for(auto c:mesh.colors) bytes(&c,sizeof(c));
    for(auto i:mesh.indices) bytes(&i,sizeof(i));
    for(auto material:mesh.materials) bytes(&material,sizeof(material));
    for(auto uv:mesh.uv) bytes(uv.data(),sizeof(float)*2);
    if(mesh.textures) for(const auto& t:*mesh.textures) { bytes(&t.width,sizeof(t.width)); bytes(&t.height,sizeof(t.height)); bytes(t.rgba.data(),t.rgba.size()); }
    return hash;
}
inline void Capture(Packet& packet,const skateboard::Placed& placed,skatevisual::Point player,const skateboard::M3& basis) {
    skateboard::M3 rotation; for(unsigned i=0;i<9;++i) rotation.a[i]=placed.rotation[i];
    const auto world=skateboard::Mul(basis,rotation); float matrix[9];
    for(unsigned i=0;i<9;++i) matrix[i]=float(world.a[i]);
    skateboard::Quaternion(matrix,packet.state.rotation);
    packet.scale=placed.scale;
    for(unsigned i=0;i<3;++i) packet.state.offset[i]=placed.matrix[12+i]-(&player.x)[i];
}
inline bool Matrix(const Packet& packet,const skateboard::M3& basis,skatevisual::Point player,float out[16]) {
    if(!std::isfinite(player.x) || !std::isfinite(player.y) || !std::isfinite(player.z)) return false;
    float quaternion[16]; const auto* q=packet.state.rotation;
    characterretarget::Matrix({q[0],q[1],q[2],q[3]},{},quaternion);
    skateboard::M3 rotation; for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) rotation.a[r*3+c]=quaternion[r*4+c];
    const auto world=skateboard::Mul(skateboard::Transpose(basis),rotation);
    std::fill(out,out+16,0.f); out[15]=1;
    for(unsigned r=0;r<3;++r) for(unsigned c=0;c<3;++c) out[r*4+c]=float(world.a[r*3+c])*packet.scale;
    for(unsigned i=0;i<3;++i) out[12+i]=(&player.x)[i]+packet.state.offset[i];
    return true;
}
inline constexpr const char* Lua=R"peer(
local f=CreateFrame("Frame")
local prefix,posePrefix,roster,last,pending="WRSP5","WRRP5",{},{},{}
local elapsed,wasActive=0,false
local function Roster()
 roster={}
 local raid=(GetNumRaidMembers() or 0)>0
 for i=1,(raid and math.min(GetNumRaidMembers(),40) or math.min(GetNumPartyMembers() or 0,4)) do
  local unit=(raid and "raid" or "party")..i
  if not UnitIsUnit(unit,"player") then
   local name,realm=UnitName(unit)
   if name then
    name=realm and realm~="" and name.."-"..realm or name
    if roster[name]~=nil then roster[name]=false else roster[name]=unit end
   end
  end
 end
 last={} pending={}
end
f:RegisterEvent("CHAT_MSG_ADDON")
f:RegisterEvent("PARTY_MEMBERS_CHANGED")
f:RegisterEvent("RAID_ROSTER_UPDATE")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:RegisterEvent("PLAYER_LEAVING_WORLD")
f:SetScript("OnEvent",function(_,event,tag,text,channel,sender)
 if event=="CHAT_MSG_ADDON" then
  local unit=roster[sender]
  if not unit or (channel~="PARTY" and channel~="RAID") or type(text)~="string" then return end
  if not ((tag==prefix and (#text==70 or #text==95)) or (tag==posePrefix and #text==244)) then return end
  local row=pending[sender] or {} pending[sender]=row
  if tag==prefix then row.board=text else row.pose=text end
 else Roster() end
end)
f:SetScript("OnUpdate",function(_,dt)
 -- Flush the latest pair once per frame, in board/pose order, instead of discarding bunched updates.
 for sender,row in pairs(pending) do
  local unit=roster[sender]
  if unit then
   last[prefix]=last[prefix] or {} last[posePrefix]=last[posePrefix] or {}
   if row.board and last[prefix][sender]~=row.board then
    last[prefix][sender]=row.board WrathSkatePeerReceive(unit,row.board)
   end
   if row.pose and last[posePrefix][sender]~=row.pose then
    last[posePrefix][sender]=row.pose WrathSkatePeerPoseReceive(unit,row.pose)
   end
  end
 end
 pending={}
 elapsed=elapsed+dt if elapsed<.1 then return end elapsed=elapsed%.1
 local channel=(GetNumRaidMembers() or 0)>0 and "RAID" or (GetNumPartyMembers() or 0)>0 and "PARTY" or nil
 if not channel then wasActive=false return end
 local text,active,pose=WrathSkatePeerState()
 if text and (active or wasActive) then
  SendAddonMessage(prefix,text,channel)
  if active and pose and #pose==244 then SendAddonMessage(posePrefix,pose,channel) end
 end
 wasActive=active
end)
Roster()
)peer";
}
