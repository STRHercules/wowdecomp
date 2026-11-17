#include "Engine/Source/lua-5.1/src/lua.h"
#include "Engine/Source/lua-5.1/src/lauxlib.h"
#include "Engine/Source/lua-5.1/src/lualib.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <windows.h>
#include <cstdint>

// Standalone Lua uses the Windows clock without booting the client's OS/event services.
int64_t OsGetAsyncClocksPerSecond() { LARGE_INTEGER value; QueryPerformanceFrequency(&value); return value.QuadPart; }
uint64_t OsGetAsyncTimeClocks() { LARGE_INTEGER value; QueryPerformanceCounter(&value); return value.QuadPart; }
void OsValidateFunctionPointer(const void* function) { assert(function); }

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream input(argv[1]);
    const std::string source{std::istreambuf_iterator<char>(input),{}};
    const auto start=source.find("R\"lua(");
    const auto end=source.find(")lua\"",start);
    assert(start!=source.npos && end!=source.npos);
    const auto script=source.substr(start+6,end-start-6);
    // Phase 3 regression: the drive must run every frame after the helper input, and leaving the world stops the helper.
    assert(source.find("helper.Input(controls.active,controls.push,controls.brake,controls.steer,controls.pop,controls.flip);\r\n        Drive(controls);")!=source.npos
        || source.find("helper.Input(controls.active,controls.push,controls.brake,controls.steer,controls.pop,controls.flip);\n        Drive(controls);")!=source.npos);
    // Phase 4: the rig pose is prepared every world update, before the scene animates.
    assert(source.find("UpdateCollision(elapsed); UpdateRig();")!=source.npos);
    assert(source.find("void OnLeaveWorld() override")!=source.npos);
    lua_State* L=luaL_newstate(); assert(L); luaL_openlibs(L);
    assert(luaL_dostring(L,R"lua(
function CreateFrame(_,_,parent)
 assert(parent==UIParent and UIParent, "UIParent not loaded")
 local f={}
 function f:SetPoint() end
 function f:SetSize() end
 function f:CreateFontString(_,_,font)
  assert(GameFontNormal and font=="GameFontNormal", "font not loaded")
  local t={}
  function t:SetAllPoints() end
  function t:SetText(text) debugText=text end
  return t
 end
 function f:RegisterEvent(event) assert(eventsReady and event=="WRATH_SKATE_DEBUG", "event array not created") end
 function f:SetScript(_,callback) debugCallback=callback end
 return f
end
function WrathSkateGetPose() return poseValid,false,1,2,3,4,5,6,0,1,2 end
poseValid=true
)lua")==0);
    // The real bootstrap fails before FrameXML globals and event creation, as the old hook did.
    assert(luaL_dostring(L,script.c_str())!=0); lua_pop(L,1);
    assert(luaL_dostring(L,"UIParent={} GameFontNormal={}")==0);
    assert(luaL_dostring(L,script.c_str())!=0); lua_pop(L,1);
    assert(luaL_dostring(L,"eventsReady=true")==0);
    assert(luaL_dostring(L,script.c_str())==0);
    assert(luaL_dostring(L,R"lua(
assert(WrathSkateDebugReady)
debugCallback(nil,"WRATH_SKATE_DEBUG",1)
assert(string.find(debugText,"pos 1.00 2.00 3.00",1,true))
assert(string.find(debugText,"vel 4.00 5.00 6.00",1,true))
assert(string.find(debugText,"GROUNDED | ground 1 air 0 | collision gen 2",1,true))
poseValid=false debugCallback(nil,"WRATH_SKATE_DEBUG",1)
assert(string.find(debugText,"waiting for world collision",1,true))
debugCallback(nil,"WRATH_SKATE_DEBUG",0)
assert(debugText=="")
)lua")==0);
    lua_close(L);
}
