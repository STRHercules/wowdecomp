"""Focused Lua behaviour check of the arcade window and its options with UI mocks (no native rendering)."""
from pathlib import Path

from lupa.lua51 import LuaRuntime

lua = LuaRuntime(unpack_returned_tuples=True)
lua.execute(r'''
calls = {}
local function log(...) local t = {} for i = 1, select("#", ...) do t[i] = tostring((select(i, ...))) end calls[#calls + 1] = table.concat(t, " ") end
local state = { "idle", "", 256, 224, 0.5, 0.21875, 4 / 3, false, "fceumm" }
function Arcade_GetState() return unpack(state) end
function SetArcadeState(...) state = { ... } end
function Arcade_SetScreen() log("screen") end
function Arcade_SetButton(b, down) log("button", b, down) end
function Arcade_ReleaseButtons() log("release") end
function Arcade_Quit() log("quit") end
function Arcade_SetPaused(p) log("paused", p); state[8] = p end
function Arcade_SetOption(k, v) log("option", k, v) end
function Arcade_SetFastForward(on) log("fast", on) end
function Arcade_SetVolume(v) log("volume", v) end
UIErrorsFrame = { AddMessage = function(_, m) log("error", m) end }
UIParent = {}
local function region(name)
    local r = { shown = true, keyboard = false, scripts = {}, name = name }
    setmetatable(r, { __index = function(t, k) if not k:match("^%u") then return nil end return function(self, ...)
        if k == "Show" then self.shown = true; if self.scripts.OnShow then self.scripts.OnShow(self) end
        elseif k == "Hide" then self.shown = false; if self.scripts.OnHide then self.scripts.OnHide(self) end
        elseif k == "IsShown" then return self.shown
        elseif k == "GetName" then return self.name
        elseif k == "SetText" then self.text = ...
        elseif k == "GetChecked" then return self.checked
        elseif k == "SetChecked" then self.checked = ...
        elseif k == "SetValue" then self.value = ...; if self.scripts.OnValueChanged then self.scripts.OnValueChanged(self, self.value) end
        elseif k == "SetTexCoord" then log("texcoord", ...)
        elseif k == "SetSize" then log("size", self.name or "?", ...)
        elseif k == "EnableKeyboard" then self.keyboard = ...
        elseif k == "SetScript" then local n, fn = ...; self.scripts[n] = fn
        elseif k == "CreateFontString" or k == "CreateTexture" then return region() end
    end end })
    return r
end
function CreateFrame(kind, name, parent, template)
    local f = region(name)
    if name then
        _G[name] = f
        for _, child in ipairs({ "Text", "Low", "High" }) do _G[name .. child] = region() end
    end
    if kind ~= "Frame" then f.shown = true end
    return f
end
''')
lua.execute(Path(__file__).with_name("EsteriaArcade.lua").read_text(encoding="utf-8"))
g = lua.globals()
frame, options = g.EsteriaArcadeFrame, g.EsteriaArcadeOptions
fire = lambda event, *args: frame.scripts["OnEvent"](frame, event, *args)
key = lambda name, down=True: frame.scripts["OnKeyDown" if down else "OnKeyUp"](frame, name)

fire("VARIABLES_LOADED")
db = g.EsteriaArcadeDB
assert db.size == 448 and db["keys"]["A"] == "X" and db["keys"]["FAST"] == "TAB"
fire("ARCADE_LOADING", "Sonic the Hedgehog 2")
assert frame.shown and frame.keyboard
g.SetArcadeState("running", "Sonic", 320, 224, 0.625, 0.21875, 4 / 3, False, "genesis_plus_gx")
frame.scripts["OnUpdate"](frame)
key("X"); key("X", False); key("F"); key("TAB"); key("TAB", False)

# Options: opening pauses; rebind A to K; Esc closes the panel instead of quitting.
options.Show(options)
g.calls = lua.table()
options.waiting = "A"
key("K")
assert db["keys"]["A"] == "K" and db["keys"]["X"] == "S"
key("K"); key("ESCAPE")
assert not options.shown
calls = list(g.calls.values())
assert "paused false" in calls and "button A true" in calls and "quit" not in calls, calls

# A binding steals the key from the action that had it.
options.waiting = "B"
key("K")
assert db["keys"]["B"] == "K" and db["keys"]["A"] is None

key("ESCAPE")
fire("ARCADE_STOPPED", "walked away")
calls = list(g.calls.values())
assert "quit" in calls and not frame.shown and not frame.keyboard
print("ok")
