"""Runs upright_toggle.lua against stub glue frames: shown only for undead on the Appearance page when the client has
the upright body, labelled by the native state, and a click asks for the other body."""

from pathlib import Path

from lupa.lua51 import LuaRuntime

STUBS = r"""
calls = {}
local function frame(name)
    local f = { shown = false, scripts = {}, text = nil }
    function f:SetSize() end
    function f:GetParent() return CharacterCreateFrame end
    function f:SetFrameStrata() end
    function f:GetFrameStrata() return "MEDIUM" end
    function f:SetFrameLevel() end
    function f:GetFrameLevel() return 3 end
    function f:SetPoint() end
    function f:Hide() self.shown = false end
    function f:Show() self.shown = true end
    function f:SetText(t) self.text = t end
    function f:SetScript(event, fn) self.scripts[event] = fn end
    function f:IsShown() return self.shown end
    function f:IsVisible() return self.shown end
    function f:GetLeft() return 0 end
    function f:GetBottom() return 0 end
    if name then _G[name] = f end
    return f
end
frames = {}
function CreateFrame(_, name) local f = frame(name); frames[#frames + 1] = f; return f end
CharacterCreateFrame = frame()
CharCreateOkayButton = frame(); CharCreateOkayButton.shown = true
frame("CharacterCreateGenderButtonMale")
CharacterCreate = { selectedRaceID = 5, personalizationMode = true }
upright = 0
function CycleCharCustomization(command, value)
    calls[#calls + 1] = { command, value }
    if value ~= nil then upright = value end
    return upright
end
function PlaySound() end
function CharacterCustomization_CloseChoices() end
function CharacterCreate_UpdateHairCustomization() calls[#calls + 1] = { "refresh" } end
function tick() frames[1].scripts.OnUpdate(frames[1], 1) end  -- the driver; the button is built on the first tick
"""


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(STUBS)
    lua.execute(Path(__file__).with_name("upright_toggle.lua").read_text(encoding="utf-8"))
    g = lua.globals()
    checks = []

    def ok(label, value):
        checks.append((label, bool(value)))
        print(("ok  " if value else "FAIL") + "  " + label)

    ok("no button before the first tick", g.CharacterCreateUprightButton is None)
    g.tick()
    button = g.CharacterCreateUprightButton
    ok("shown for undead on the Appearance page", button.shown and button.text == "Posture: Hunched")
    button.scripts.OnClick(button)
    last, before = g.calls[len(g.calls)], g.calls[len(g.calls) - 1]  # Lua tables are 1-based
    ok("click asks for the upright body", before[1] == "EA_UPRIGHT" and before[2] == 1 and last[1] == "refresh" and g.upright == 1)
    g.tick()
    ok("label follows the native state", button.text == "Posture: Upright")
    g.CharacterCreate.selectedRaceID = 42  # a creator list id, not the race: the native answer decides
    g.tick()
    ok("selectedRaceID does not hide it", button.shown)
    g.FatePage_Open = lua.eval("function() return true end")
    g.tick()
    ok("hidden while the Fate page is open", not button.shown)
    g.FatePage_Open = None
    g.CycleCharCustomization = lua.eval("function() return nil end")
    g.tick()
    ok("hidden when the client has no upright body", not button.shown)
    print("PASS" if all(v for _, v in checks) else "FAILED")
    raise SystemExit(0 if all(v for _, v in checks) else 1)


if __name__ == "__main__":
    main()
