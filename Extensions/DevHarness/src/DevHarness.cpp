// DevHarness: lets a developer (or an agent) drive a running client without touching its files. Everything is off
// unless WOW_DEV_HARNESS=1 is in the client's environment, so a normal launch is unchanged.
//
//   WOW_DEV_HARNESS=1                  Lua errors (with stacks) go to Logs\DevLua.log; DevHarness_Log(text) appends.
//                                      Logs\DevRun.lua, when it appears, is run once in the current Lua state and
//                                      deleted; print output and errors land in DevLua.log.
//   WOW_DEV_NAME=B                     Use DevLuaB.log / DevRunB.lua instead (a second client in the same folder).
//   WOW_DEV_AUTOLOGIN=acct|pass|Name   At the login screen, log in; at character select, enter the world as Name.
//                                      Use test accounts only: the value lives in the process environment.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include "Engine/Source/Base/CDataStore.h"
#include "Engine/Source/Frame/CSimpleTop.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Os/TimeManager.h"
#include "WoW/Source/Extension/ExtensionMgr.h"

namespace {

bool s_enabled;
std::string s_logPath = "Logs\\DevLua.log";
std::string s_runPath = "Logs\\DevRun.lua";

void Append(const char* text) {
    if (FILE* f = std::fopen(s_logPath.c_str(), "ab")) {
        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%H:%M:%S", std::localtime(&now));
        std::fprintf(f, "[%s] %s\n", stamp, text ? text : "(nil)");
        std::fclose(f);
    }
}

int32_t Script_DevHarness_Log(lua_State* L) {
    std::string line;
    for (int32_t i = 1; i <= lua_gettop(L); ++i) {
        if (i > 1) line += " ";
        const char* s = lua_tostring(L, i);
        line += s ? s : luaL_typename(L, i);
    }
    Append(line.c_str());
    return 0;
}

// Installed in each state. In the game state the UI replaces the error handler while FrameXML loads, so the hook
// is re-applied on PLAYER_LOGIN, wrapping whatever handler the UI set.
const char* kBootstrap = R"lua(
local log = DevHarness_Log
-- Errors go to the log only: the UI's own handler (the script error popup) is never called, so a broken popup
-- cannot recurse into an error storm, and handlers the UI installs later are replaced by this one.
local set = seterrorhandler
local function handler(message)
  pcall(log, "LUA ERROR: " .. tostring(message) .. "\n" .. (debugstack and debugstack(2, 20, 0) or ""))
end
seterrorhandler = function() return set(handler) end
set(handler)
-- Wrapped before FrameXML loads, so code that keeps its own reference still goes through the log.
for _, name in ipairs({ "Logout", "Quit", "ForceLogout", "ForceQuit" }) do
  local original = _G[name]
  if original then
    _G[name] = function(...) pcall(log, "CALLED " .. name .. "\n" .. (debugstack and debugstack(2, 20, 0) or "")) return original(...) end
  end
end
local oldPrint = print
print = function(...) local t = {} for i = 1, select("#", ...) do t[i] = tostring((select(i, ...))) end log("print: " .. table.concat(t, " ")) if oldPrint then return oldPrint(...) end end
local f = CreateFrame("Frame")
f:RegisterEvent("PLAYER_ENTERING_WORLD")
f:SetScript("OnEvent", function() log("PLAYER_LOGIN") end)

local login = DEVHARNESS_AUTOLOGIN
DEVHARNESS_AUTOLOGIN = nil
if login and DefaultServerLogin then
  local account, password, character = login:match("^([^|]+)|([^|]+)|?(.*)$")
  local stage, wait = "login", 0
  f:SetScript("OnUpdate", function(_, elapsed)
    wait = wait + elapsed
    if wait < 1.5 then return end
    wait = 0
    if stage == "login" and AccountLogin and AccountLogin:IsShown() then
      log("autologin: logging in " .. account); stage = "select"; DefaultServerLogin(account, password)
    elseif stage == "select" and CharacterSelect and CharacterSelect:IsShown() and GetNumCharacters() > 0 then
      for i = 1, GetNumCharacters() do
        if character == "" or GetCharacterInfo(i) == character then
          log("autologin: entering world as " .. tostring(GetCharacterInfo(i))); stage = "done"
          SelectCharacter(i); EnterWorld(); return
        end
      end
      log("autologin: character not found: " .. character); stage = "done"
    end
  end)
end
)lua";

class DevHarnessLua : public LuaScript {
    public:
    DevHarnessLua() : LuaScript("DevHarnessLua") {}

    void OnFrameScriptInitialized(lua_State* L, FrameScriptContext context) override {
        if (!s_enabled) return;

        const char* autologin = std::getenv("WOW_DEV_AUTOLOGIN");
        if (context == FRAMESCRIPT_CONTEXT_GLUE && autologin && *autologin) {
            lua_pushstring(L, autologin);
            lua_setglobal(L, "DEVHARNESS_AUTOLOGIN");
        }

        if (luaL_loadbuffer(L, kBootstrap, std::strlen(kBootstrap), "=DevHarness") || lua_pcall(L, 0, 0, 0)) {
            Append(lua_tostring(L, -1));
            lua_pop(L, 1);
        }

        Append(context == FRAMESCRIPT_CONTEXT_GLUE ? "glue state ready" : "game state ready");
    }
};

// A logout the UI did not ask for (PLAYER_CAMPING with no Lua Logout call) is found by its packet: the Lua stack at
// the moment CMSG_LOGOUT_REQUEST goes out names the caller.
class DevHarnessNet : public NetScript {
    public:
    DevHarnessNet() : NetScript("DevHarnessNet") {}

    void OnMessageSent(NETMESSAGE msgId, CDataStore& msg) override {
        if (msgId != 0x4B) return; // CMSG_LOGOUT_REQUEST
        lua_State* L = FrameScript_GetContext();
        const char* probe = "DevHarness_Log('CMSG_LOGOUT_REQUEST sent\\n' .. (debugstack and debugstack(1, 20, 0) or ''))";
        if (!L || luaL_loadbuffer(L, probe, std::strlen(probe), "=DevHarness") || lua_pcall(L, 0, 0, 0)) {
            if (L) lua_pop(L, 1);
            Append("CMSG_LOGOUT_REQUEST sent (no Lua stack)");
        }
    }
};

class DevHarnessClient : public ClientScript {
    public:
    DevHarnessClient() : ClientScript("DevHarnessClient") {}

    void OnUpdate(float elapsedSec) override {
        if (!s_enabled) return;

        // A driven client gets no keyboard or mouse input; count it as present so the idle logout never fires.
        CSimpleTop::s_eventTime = OsGetAsyncTimeMs();

        m_wait += elapsedSec;
        if (m_wait < 0.5f) return;
        m_wait = 0.0f;

        FILE* f = std::fopen(s_runPath.c_str(), "rb");
        if (!f) return;

        std::string source;
        char buffer[4096];
        for (size_t n; (n = std::fread(buffer, 1, sizeof(buffer), f)) > 0;) source.append(buffer, n);
        std::fclose(f);
        std::remove(s_runPath.c_str());

        Append("DevRun.lua: running");
        lua_State* L = FrameScript_GetContext();
        if (luaL_loadbuffer(L, source.data(), source.size(), "=DevRun") || lua_pcall(L, 0, 0, 0)) {
            Append((std::string("DevRun.lua error: ") + (lua_tostring(L, -1) ? lua_tostring(L, -1) : "?")).c_str());
            lua_pop(L, 1);
        }
    }

    private:
    float m_wait = 0.0f;
};

}

void AddDevHarnessExtension() {
    const char* enabled = std::getenv("WOW_DEV_HARNESS");
    s_enabled = enabled && *enabled == '1';

    // WOW_DEV_NAME=B uses Logs\DevLuaB.log and Logs\DevRunB.lua, so two driven clients can share a folder.
    if (const char* name = std::getenv("WOW_DEV_NAME"); name && *name) {
        s_logPath = std::string("Logs\\DevLua") + name + ".log";
        s_runPath = std::string("Logs\\DevRun") + name + ".lua";
    }

    if (!s_enabled) return;

    sExtensionMgr->RegisterLuaFunction("DevHarness_Log", Script_DevHarness_Log);
    new DevHarnessLua();
    new DevHarnessClient();
    new DevHarnessNet();
    sExtensionMgr->Log("DevHarness: enabled");
}
