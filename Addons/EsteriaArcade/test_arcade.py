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
function Arcade_SetKey(k, down) log("key", k, down) end
function Arcade_SetPointer(u, v, inside) log("pointer", inside) end
function Arcade_SetMouseButton(b, down) log("mouse", b, down) end
function GetCursorPosition() return 0, 0 end
function GetMouseFocus() return nil end
function Arcade_HideCursor(h) log("hidecursor", h) end
UIErrorsFrame = { AddMessage = function(_, m) log("error", m) end }
UIParent = nil
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
        elseif k == "SetSize" then local w, h = ...; self.w, self.h = w, h; log("size", self.name or "?", w, h)
        elseif k == "SetWidth" then self.w = ...
        elseif k == "SetFrameStrata" then self.strata = ...
        elseif k == "SetHeight" then self.h = ...
        elseif k == "GetWidth" then return self.w or 100
        elseif k == "GetHeight" then return self.h or 100
        elseif k == "GetThumbTexture" then return region()
        elseif k == "GetValue" then return self.value or 0
        elseif k == "EnableKeyboard" then self.keyboard = ...
        elseif k == "SetScript" then local n, fn = ...; self.scripts[n] = fn
        elseif k == "CreateFontString" or k == "CreateTexture" then return region() end
    end end })
    return r
end
UIParent = region("UIParent"); UIParent.w, UIParent.h = 1920, 1080
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
lua.execute(Path(__file__).with_name("esteria_style.lua").read_text(encoding="utf-8"))
lua.execute(Path(__file__).with_name("EsteriaArcade.lua").read_text(encoding="utf-8"))
g = lua.globals()
frame, options = g.EsteriaArcadeFrame, g.EsteriaArcadeOptions
fire = lambda event, *args: frame.scripts["OnEvent"](frame, event, *args)
key = lambda name, down=True: frame.scripts["OnKeyDown" if down else "OnKeyUp"](frame, name)

fire("VARIABLES_LOADED")
db = g.EsteriaArcadeDB
assert abs(db.size - 0.62) < 1e-9 and db["keys"]["A"] == "X" and db["keys"]["FAST"] == "TAB"
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

# An N64 picture (640 wide) at the largest size still fits the screen with its header and footer.
db.size = 0.92
g.SetArcadeState("running", "Ocarina", 640, 240, 0.625, 0.234375, 4 / 3, False, "parallel_n64")
frame.scripts["OnUpdate"](frame)
assert frame.w <= 1920 and frame.h <= 1080, (frame.w, frame.h)
# Opening Options puts it above the game window; clicking the game window brings that back on top.
options.Hide(options)
g.calls = lua.table()
options.Show(options)
frame.scripts["OnMouseDown"](frame)
assert frame.strata == "FULLSCREEN_DIALOG" and options.strata == "DIALOG", (frame.strata, options.strata)
options.scripts["OnMouseDown"](options)
assert options.strata == "FULLSCREEN_DIALOG" and frame.strata == "DIALOG"
options.Hide(options)
key("ESCAPE")
fire("ARCADE_STOPPED", "walked away")
calls = list(g.calls.values())
assert "quit" in calls and not frame.shown and not frame.keyboard
# A DOS game (DOSBox Pure): every key, Escape included, goes to the game rather than quitting; the mouse layer is on.
g.calls = lua.table()
fire("ARCADE_STARTED", "WarCraft: Orcs & Humans", "dosbox_pure")
key("ESCAPE"); key("ESCAPE", False); key("A"); key("LSHIFT")
calls = list(g.calls.values())
assert "key ESCAPE true" in calls and "key ESCAPE false" in calls and "key A true" in calls, calls
assert "quit" not in calls and frame.shown
g.calls = lua.table()
fire("ARCADE_STARTED", "StarCraft: Brood War", "starcraft")  # the PC bridge core plays like a DOS game
key("ESCAPE")
assert "key ESCAPE true" in list(g.calls.values()) and "quit" not in list(g.calls.values())
fire("ARCADE_STARTED", "Sonic", "genesis_plus_gx")
g.calls = lua.table()
key("ESCAPE")
assert "quit" in list(g.calls.values())
print("ok")
