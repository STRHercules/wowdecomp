"""Lua behavior check for EsteriaSpellbook with UI mocks; does not prove in-game rendering or secure execution."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent


CHECKS = r'''
assert(DragonUI.db.profile.modules.spellbook.enabled == false, "DragonUI spellbook switched off before login")
local function fire(event)
  for _, f in ipairs(frames) do
    local ev, sc = rawget(f, "events"), rawget(f, "scripts")
    if ev and ev[event] and sc and sc.OnEvent then sc.OnEvent(f, event) end
  end
end
fire("PLAYER_LOGIN")
local T = EsteriaSpellbook_Test
local book = EsteriaSpellbookFrame
assert(book and book.secure, "secure book built")
assert(overrides.P[1] == "EsteriaSpellbookToggle" and overrides["SHIFT-I"][2] == "RightButton", "keys bound to the secure toggle")
assert(book:GetAttribute("_onshow"):find("ESCAPE"), "Escape bound while open")

local st = T.state()
local names = {}
for _, b in ipairs(st.books) do names[#names + 1] = b.name .. "=" .. b.count end
assert(table.concat(names, ",") == "General=3,Fire=2,Frost=2,Emotes=1,Wings=2,Pet=2", table.concat(names, ","))
local fireball
for _, s in ipairs(st.spells) do if s.name == "Fireball" then fireball = s end end
assert(fireball.index == 9 and fireball.rank == "Rank 3" and #fireball.ranks == 3, "one card per spell, highest rank")

-- First page armed at login, out of combat: All spells = General + trees (no wings, no pet), passive not cast.
local card1 = EsteriaSpellbookCard1
assert(card1:IsShown() and card1:GetAttribute("type1") == "spell" and card1:GetAttribute("spell") == "Attack")
assert(card1:GetAttribute("shift-type1") == "none", "shift-click links instead of casting")
local cards = {}
for i = 1, 16 do local c = _G["EsteriaSpellbookCard" .. i]; if c:IsShown() then cards[#cards + 1] = c end end
assert(#cards == 7, "7 spells in All (no emotes, wings or pet), got " .. #cards)
for _, c in ipairs(cards) do
  if c.spell.passive then assert(c:GetAttribute("type1") == nil, "passive does not cast") end
  if c.spell.name == "Fireball" then assert(c:GetAttribute("spell") == "Fireball(Rank 3)") end
end

-- Open, browse books and filters out of combat.
ToggleSpellBook("spell")
assert(book:IsShown() and stockOpened == 0)
book.scripts.OnUpdate(book, 0.1)
T.setBook("pet"); T.refilter()
assert(#T.state().filtered == 2)
EsteriaSpellbookDB.unbound = true; T.setBook("all"); T.refilter()
for _, s in ipairs(T.state().filtered) do assert(s.name ~= "Fireball" and not s.passive, "only unbound, castable spells") end
EsteriaSpellbookDB.unbound = nil
EsteriaSpellbookSearch:SetText("wings")
T.refilter(); assert(#T.state().filtered == 2, "search reaches the Wings book")
T.setBook("emotes"); EsteriaSpellbookSearch:SetText(""); T.refilter(); assert(#T.state().filtered == 1 and T.state().filtered[1].name == "catJam")
EsteriaSpellbookSearch:SetText("")

-- Pet cards toggle autocast on right-click.
T.setBook("pet"); book.scripts.OnUpdate(book, 0.1)
for i = 1, 16 do local c = _G["EsteriaSpellbookCard" .. i] end
local pc = EsteriaSpellbookCard1
T.refilter(); T.arm(pc, T.state().filtered[1])
assert(pc:GetAttribute("macrotext2") == "/petautocasttoggle Claw")

-- Combat: nothing re-arms (the mock errors on SetAttribute), data changes repaint visuals only.
combat = true
fire("PLAYER_REGEN_DISABLED")
fire("SPELLS_CHANGED")
book.scripts.OnUpdate(book, 0.1)
ToggleSpellBook("spell")
assert(stockOpened == 1, "in combat the micro button opens the stock book")
combat = false
fire("PLAYER_REGEN_ENABLED")
book.scripts.OnUpdate(book, 0.1)

-- Dragging picks up the spell.
EsteriaSpellbookCard1.scripts.OnDragStart(EsteriaSpellbookCard1)
assert(picked[1] ~= nil)
-- Toggle closes.
ToggleSpellBook("spell")
assert(not book:IsShown())
'''


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(r'''
unpack = unpack or table.unpack; tinsert = table.insert; format = string.format
SlashCmdList, combat, overrides, picked, sounds = {}, false, {}, {}, {}
BOOKTYPE_SPELL, BOOKTYPE_PET = "spell", "pet"
DragonUI = { db = { profile = { modules = { spellbook = { enabled = true } } } } }
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetStringHeight() return 14 end
function methods:GetStringWidth() return 50 end
function methods:GetWidth() return rawget(self, "w") or 100 end
function methods:SetWidth(w) self.w = w end
function methods:GetHeight() return 300 end
function methods:Show() local was = rawget(self, "shown"); self.shown = true
  if not was then for _, f in ipairs(rawget(self, "onshow") or {}) do f(self) end end end
function methods:Hide() local was = rawget(self, "shown"); self.shown = false
  if was then for _, f in ipairs(rawget(self, "onhide") or {}) do f(self) end end end
function methods:IsShown() return rawget(self, "shown") end
function methods:IsEnabled() return 1 end
function methods:SetScript(k, f) self.scripts = rawget(self, "scripts") or {}; self.scripts[k] = f end
function methods:HookScript(k, f)
  if k == "OnShow" then self.onshow = rawget(self, "onshow") or {}; table.insert(self.onshow, f)
  elseif k == "OnHide" then self.onhide = rawget(self, "onhide") or {}; table.insert(self.onhide, f)
  else self.hooks = rawget(self, "hooks") or {}; self.hooks[k] = f end
end
function methods:SetAttribute(k, v)
  if combat and self.secure then error("SetAttribute in combat on " .. tostring(rawget(self, "name"))) end
  self.attrs = rawget(self, "attrs") or {}; self.attrs[k] = v
end
function methods:GetAttribute(k) return (rawget(self, "attrs") or {})[k] end
function methods:RegisterEvent(e) self.events = rawget(self, "events") or {}; self.events[e] = true end
function methods:NumLines() return 3 end
function methods:GetVerticalScroll() return 0 end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
function methods:GetTextColor() return 1, 1, 1, 1 end
frames = {}
function New(kind, name)
  local f = setmetatable({ kind = kind, name = name, shown = kind ~= "Frame" and kind ~= "Button" or nil },
    { __index = function(_, k) return methods[k] or function() end end })
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name, parent, template)
  local f = New(kind, name)
  if kind == "Button" and not template then f.shown = true end
  if template then f.secure = template:find("Secure") ~= nil end
  if template == "GameTooltipTemplate" then
    for i = 1, 3 do New("FontString", name .. "TextLeft" .. i).text = "line " .. i; New("FontString", name .. "TextRight" .. i) end
  end
  return f
end
function InCombatLockdown() return combat end
function PlaySound(s) table.insert(sounds, s) end
function SecureHandlerSetFrameRef(f, k, v) f.refs = rawget(f, "refs") or {}; f.refs[k] = v end
function ClearOverrideBindings() overrides = {} end
function SetOverrideBindingClick(_, _, key, name, button) overrides[key] = { name, button } end
function GetBindingKey(cmd) if cmd == "TOGGLESPELLBOOK" then return "P" end if cmd == "TOGGLEPETBOOK" then return "SHIFT-I" end end
function HideUIPanel(f) f.shown = false end
function CooldownFrame_SetTimer() end
function IsModifiedClick() return false end
function PickupSpell(i, b) picked = { i, b } end
UIParent = New("Frame"); GameTooltip = New("GameTooltip"); ChatFontNormal = {}

-- Book: General (1-6: 4 ranks of Ice Lance... no, 2 spells + 2 wings + passive), Fire (7-9), Frost (10-11).
book = {
  { "Attack", "", 6603 }, { "Cooking", "Grand Master", 51296 }, { "catJam", "Emote", 990001 },
  { "Wings of Dawn", "", 970201 }, { "Wings of Dusk", "", 970202 }, { "Dual Wield", "", 674, passive = true },
  { "Fireball", "Rank 1", 133 }, { "Fireball", "Rank 2", 143 }, { "Fireball", "Rank 3", 145 },
  { "Fire Ward", "Rank 1", 543 }, { "Frostbolt", "Rank 1", 116 }, { "Ice Barrier", "Rank 1", 11426 },
}
tabs = { { "General", "g", 0, 6 }, { "Fire", "f", 6, 4 }, { "Frost", "r", 10, 2 }, { "Cosmetics", "c", 12, 0 } }
pet = { { "Claw", "Rank 3" }, { "Growl", "Rank 2" } }
function GetNumSpellTabs() return #tabs end
function GetSpellTabInfo(t) return unpack(tabs[t]) end
function GetSpellName(i, b) local e = (b == "pet" and pet or book)[i]; if e then return e[1], e[2] end end
function GetSpellLink(i, b) local e = (b == "pet" and pet or book)[i]; return e and e[3] and ("|Hspell:" .. e[3] .. "|h") end
function GetSpellTexture() return "icon" end
function IsPassiveSpell(i, b) return b == "spell" and book[i].passive end
function HasPetSpells() return #pet, "PET" end
function GetSpellCooldown() return 0, 0, 1 end
function GetSpellAutocast(i) return true, i == 1 end
function GetSpellInfo(id) if id == 133 then return "Fireball" end end
function GetActionInfo(slot) if slot == 1 then return "spell", 8, "spell", 133 end end
SpellBookFrame = New("Frame", "SpellBookFrame")
stockOpened = 0
function ToggleSpellBook() stockOpened = stockOpened + 1 end
''')
    for name in ("esteria_style.lua", "EsteriaSpellbook.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute(CHECKS)
    print("EsteriaSpellbook: all checks passed")


if __name__ == "__main__":
    main()
