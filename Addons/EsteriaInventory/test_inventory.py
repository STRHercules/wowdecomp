"""Lua smoke check for EsteriaInventory with UI mocks: login, every options section and style, list view and the
backpack tooltip run without errors. Does not prove in-game rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert; wipe = function(t) for k in pairs(t) do t[k] = nil end end
UISpecialFrames, SlashCmdList, StaticPopupDialogs = {}, {}, {}
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetName() return rawget(self, "_name") end
function methods:GetParent() return rawget(self, "parent") end
function methods:GetWidth() return 120 end
function methods:GetHeight() return 14 end
function methods:GetScale() return 1 end
function methods:GetEffectiveScale() return 1 end
function methods:GetLeft() return 100 end
function methods:GetCenter() return 50, 50 end
function methods:GetTop() return 600 end
function methods:GetPoint() return "CENTER", nil, "CENTER", 0, 0 end
function methods:GetFrameLevel() return 1 end
function methods:GetValue() return rawget(self, "value") or 0 end
function methods:Show() local was = rawget(self, "shown"); self.shown = true
  local s = rawget(self, "scripts"); if not was and s and s.OnShow then s.OnShow(self) end end
function methods:Hide() local was = rawget(self, "shown"); self.shown = false
  local s = rawget(self, "scripts"); if was and s and s.OnHide then s.OnHide(self) end end
function methods:IsShown() return rawget(self, "shown") end
function methods:SetScript(k, f) self.scripts = rawget(self, "scripts") or {}; self.scripts[k] = f end
function methods:GetScript(k) local s = rawget(self, "scripts"); return s and s[k] end
function methods:HookScript(k, f)
  local s = rawget(self, "scripts") or {}; self.scripts = s
  local old = s[k]; s[k] = function(...) if old then old(...) end f(...) end
end
function methods:RegisterEvent(e) self.events = rawget(self, "events") or {}; self.events[e] = true end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
function methods:SetThumbTexture() self.thumb = New("Texture") end
function methods:GetThumbTexture() return rawget(self, "thumb") end
function methods:SetValue(v) self.value = v
  local s = rawget(self, "scripts"); if s and s.OnValueChanged then s.OnValueChanged(self, v) end end
function methods:Click(button) local s = rawget(self, "scripts"); if s and s.OnClick then s.OnClick(self, button or "LeftButton") end end
frames = {}
function New(kind, name, parent)
  local f = setmetatable({ kind = kind, _name = name, parent = parent, shown = true },
    { __index = function(_, k)
      -- Like a real frame: API methods (capitalised) exist, plain fields start out nil.
      return methods[k] or (type(k) == "string" and k:match("^%u") and function() end) or nil
    end })
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name, parent, template)
  local f = New(kind, name, parent)
  if name then for _, part in ipairs({ "IconTexture", "Count", "Cooldown", "Text", "Left", "Middle", "Right" }) do
    New("Texture", name .. part) end end
  return f
end
UIParent = New("Frame", "UIParent"); GameTooltip = New("GameTooltip"); ColorPickerFrame = New("Frame")
MainMenuBarBackpackButton = New("Button", "MainMenuBarBackpackButton")
function PlaySound() end
function GetTime() return clock or 0 end
function UnitName() return "Zach" end
function GetMoney() return 1000 end
function GetCoinTextureString(v) return tostring(v) end
function GetItemQualityColor() return 1, 1, 1, "ffffffff" end
function GetBindingKey() return "B" end
function GetItemCooldown() return 0, 0, 0 end
function CooldownFrame_SetTimer() end
function SetItemButtonTexture() end
function SetItemButtonCount(button, count) button.count = count end
function SetItemButtonDesaturated() end
function GetContainerNumSlots() return 0 end
function IsShiftKeyDown() return false end
function SendAddonMessage() end
function EsteriaInventory_Request() return nil end
DEFAULT_CHAT_FRAME = New("Frame")
function StaticPopup_Show() end
function GetCursorInfo() return nil end
function GetMouseFocus() return nil end
function IsAltKeyDown() return false end
function IsControlKeyDown() return false end
function CursorHasItem() return false end
function ClearCursor() end
function GetCurrentGuildBankTab() return 1 end
function GetNumGuildBankTabs() return 0 end
function IsGuildLeader() return false end
'''

DRIVE = r'''
local events
for _, f in ipairs(frames) do
  local s = rawget(f, "events")
  if s and s.PLAYER_LOGIN then events = f end
end
events.scripts.OnEvent(events, "PLAYER_LOGIN")
local inv = EsteriaInventory
-- A carried snapshot as the server would deliver it.
inv.active = true
inv.snaps[0] = { rows = { { guid = 1, entry = 4306, count = 20, digital = true, bag = 0, slot = 0, quality = 1,
  favorite = false, tag = "", split = false, name = "Silk Cloth", icon = "INV_Fabric_Silk_01", inventoryType = 0,
  link = "item:4306", durability = 0, maxDurability = 0 } },
  revision = 1, offset = 0, total = 1, used = 121, capacity = 1024, active = true }
inv.snapshot = inv.snaps[0]
inv:Render()
local options = EsteriaInventoryOptionsFrame
assert(options, "options window was not built")
options:Show()
-- Click every button and move every slider in the options window, twice (toggles go on and back off).
local clicked = 0
for pass = 1, 2 do
  for _, f in ipairs(frames) do
    local s = rawget(f, "scripts")
    local inside, node = false, f
    while node do if node == options then inside = true end; node = rawget(node, "parent") end
    if inside and s and s.OnClick then s.OnClick(f, "LeftButton"); clicked = clicked + 1 end
    if inside and s and s.OnValueChanged then s.OnValueChanged(f, 1) end
  end
end
for _, key in ipairs({ "esteria", "midnight", "verdant", "crimson", "frost", "minimal", "classic" }) do
  EsteriaInventoryDB.style = key
  inv:Apply(); inv:Render()
end
EsteriaInventoryDB.accent = { 0.2, 0.9, 0.4 }; inv:Apply()
EsteriaInventoryDB.viewMode = "list"; inv:Apply(); inv:Render()
EsteriaInventoryDB.viewMode = "grid"; EsteriaInventoryDB.showBar = false; EsteriaInventoryDB.showStatus = false
inv:Apply()
assert(inv:FooterHeight() == 12 + 30 + 22, "footer height without bar and help line: " .. inv:FooterHeight())
-- Backpack tooltip shows storage usage.
clock = 1; events.scripts.OnUpdate(events, 0.3)
EsteriaInventoryDB.replaceBags = true
local enter = MainMenuBarBackpackButton.scripts.OnEnter
assert(enter, "backpack tooltip not hooked")
local lines = {}
GameTooltip.AddLine = function(_, text) lines[#lines + 1] = text end
GameTooltip.SetText = function(_, text) lines[#lines + 1] = text end
enter(MainMenuBarBackpackButton)
assert(lines[2] == "121 / 1024 slots used", tostring(lines[2]))
-- Continuous scrolling: moves inside the fetched block are local; leaving it asks the server once.
local requests = {}
EsteriaInventory_Request = function(action, revision, guid, count, offset)
  requests[#requests + 1] = { action = action, count = count, offset = offset }; return nil
end
inv.serverReady = true
EsteriaInventoryDB.scrollMode, EsteriaInventoryDB.viewMode = true, "list"
EsteriaInventoryDB.showBar, EsteriaInventoryDB.showStatus = true, true
inv:Apply()
local rows = {}
for i = 1, 255 do rows[i] = { guid = i, entry = 4306, count = 1, digital = true, bag = 0, slot = 0, quality = 1,
  favorite = false, tag = "", split = false, name = "Item " .. i, icon = "x", inventoryType = 0, link = "item:4306",
  durability = 0, maxDurability = 0 } end
inv.snaps[0] = { rows = rows, revision = 1, offset = 0, total = 600, used = 600, capacity = 1024, active = true }
inv.snapshot = inv.snaps[0]; inv.viewOffset = 0; inv.targetOffset = nil
inv:Render()
local step = inv:Step()
inv:Scroll(step * 3)
assert(#requests == 0, "a scroll inside the block asked the server")
assert(inv.buttons[1].row == rows[step * 3 + 1], "view did not move locally")
inv:Scroll(400)
assert(#requests == 1 and requests[1].count == 255, "leaving the block should ask once for a full block")
assert(requests[1].offset > 0 and requests[1].offset <= inv.viewOffset, "block should start at or before the view")
EsteriaInventory_Request = function() return nil end
SlashCmdList.ESTERIAINVENTORY("debug")
SlashCmdList.ESTERIAINVENTORY("reset")
return clicked
'''

lua = LuaRuntime()
lua.execute(MOCKS)
lua.execute((HERE / "EsteriaInventory.lua").read_text(encoding="utf-8"))
clicked = lua.execute(DRIVE)
assert clicked > 40, clicked
print(f"ok: {clicked} clicks across options, 7 styles, list view, footer, local scrolling and backpack tooltip")
