from pathlib import Path

from lupa.lua51 import LuaRuntime


ADDON = Path(__file__).resolve().parents[2] / "Addons" / "EsteriaWanted" / "EsteriaWanted.lua"


def test_visible_wanted_pins():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute("""
        frames = {}
        local frame = {}
        frame.__index = frame
        function frame:SetScript(event, fn) self.scripts[event] = fn end
        function frame:RegisterEvent(event) self.events[event] = true end
        function frame:UnregisterAllEvents() self.events = {} end
        function frame:SetWidth(width) self.width = width end
        function frame:SetHeight(height) self.height = height end
        function frame:GetWidth() return self.width end
        function frame:GetHeight() return self.height end
        function frame:SetFrameLevel(level) self.level = level end
        function frame:GetFrameLevel() return self.level end
        function frame:EnableMouse(enabled) self.mouse = enabled end
        function frame:ClearAllPoints() self.anchor = nil end
        function frame:SetPoint(...) self.anchor = {...} end
        function frame:Show() self.shown = true end
        function frame:IsShown() return self.shown end
        function frame:Hide()
          if self.shown and self.scripts.OnHide then self.scripts.OnHide(self) end
          self.shown = false
        end
        function frame:CreateTexture()
          local texture = {}
          function texture:SetAllPoints(owner) self.owner = owner end
          function texture:SetTexture(file) self.file = file end
          function texture:SetTexCoord(...) self.coords = {...} end
          function texture:SetVertexColor(...) self.color = {...} end
          self.texture = texture
          return texture
        end
        function CreateFrame(_, _, parent)
          local object = setmetatable({parent = parent, scripts = {}, events = {},
                                       shown = true, level = 1}, frame)
          table.insert(frames, object)
          return object
        end
        GameTooltip = {}
        function GameTooltip:SetOwner(owner) self.owner = owner end
        function GameTooltip:IsOwned(owner) return self.owner == owner end
        function GameTooltip:SetText(text) self.title = text end
        function GameTooltip:AddLine(text) self.line = text end
        function GameTooltip:Show() self.shown = true end
        function GameTooltip:Hide() self.shown = false; self.owner = nil end
    """)
    lua.execute(ADDON.read_text(encoding="utf-8"))
    assert lua.eval("#frames") == 0, "The addon must be inert without its native API."

    lua.execute("""
        targets = {{name = "Offender", x = 0.25, y = 0.5},
                   {name = "Outlaw", x = 0.5, y = 0.75}}
        function EsteriaGetVisibleWantedMapPins() return targets end
    """)
    lua.execute(ADDON.read_text(encoding="utf-8"))
    assert lua.eval("#frames") == 1, "The map frames may be loaded later."
    lua.execute("""
        WorldMapFrame = CreateFrame("Frame")
        WorldMapFrame:Hide()
        WorldMapButton = CreateFrame("Frame", nil, WorldMapFrame)
        WorldMapButton:SetWidth(800)
        WorldMapButton:SetHeight(600)
        frames[1].scripts.OnEvent(frames[1], "ADDON_LOADED")
        frames[4].scripts.OnShow(frames[4])
        frames[6].scripts.OnEnter(frames[6])
    """)
    assert lua.eval("frames[5].anchor[4]") == 200
    assert lua.eval("frames[5].anchor[5]") == -300
    assert lua.eval("frames[5].texture.coords[1]") == 0.75
    assert lua.eval("GameTooltip.title") == "Outlaw"

    lua.execute("""
        targets[1].x = 0.5
        targets[1].age = 12
        targets[2] = nil
        frames[4].scripts.OnUpdate(frames[4], 0.25)
    """)
    assert lua.eval("frames[5].anchor[4]") == 400
    assert not lua.eval("frames[6].shown")
    assert not lua.eval("GameTooltip.shown")
    lua.execute("frames[5].scripts.OnEnter(frames[5])")
    assert lua.eval("GameTooltip.line") == "Last seen 12s ago"
    assert lua.eval("#frames") == 6, "Refreshing the map should reuse pins."

    lua.execute("frames[4].scripts.OnHide(frames[4])")
    assert not lua.eval("frames[5].shown")


if __name__ == "__main__":
    test_visible_wanted_pins()
    print("Wanted map addon checks passed.")
