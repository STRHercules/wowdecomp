"""Lua behavior check for EsteriaProfessions with UI and trade skill mocks; does not prove in-game rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert; format = string.format
SlashCmdList, calls, combat = {}, {}, false
DragonUI = { db = { profile = { modules = { professions = { enabled = true } } } } }
ITEM_QUALITY_COLORS = { [3] = { r = 0, g = 0.44, b = 0.87 } }
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetStringHeight() return 14 end
function methods:GetStringWidth() return 50 end
function methods:GetWidth() return 200 end
function methods:Show() local was = rawget(self, "shown"); self.shown = true
  for _, f in ipairs(rawget(self, "onshow") or {}) do if not was then f(self) end end end
function methods:Hide() local was = rawget(self, "shown"); self.shown = false
  for _, f in ipairs(rawget(self, "onhide") or {}) do if was then f(self) end end end
function methods:IsShown() return rawget(self, "shown") end
function methods:IsEnabled() return rawget(self, "disabled") and 0 or 1 end
function methods:Enable() self.disabled = false end
function methods:Disable() self.disabled = true end
function methods:SetScript(k, f) self.scripts = rawget(self, "scripts") or {}; self.scripts[k] = f end
function methods:GetScript(k) return (rawget(self, "scripts") or {})[k] end
function methods:HookScript(k, f)
  if k == "OnShow" then self.onshow = rawget(self, "onshow") or {}; table.insert(self.onshow, f); return end
  if k == "OnHide" then self.onhide = rawget(self, "onhide") or {}; table.insert(self.onhide, f); return end
  local s = rawget(self, "scripts") or {}; self.scripts = s
  local old = s[k]; s[k] = function(...) if old then old(...) end f(...) end
end
function methods:SetAttribute(k, v)
  if combat and rawget(self, "secure") then error("SetAttribute in combat") end
  self.attrs = rawget(self, "attrs") or {}; self.attrs[k] = v
end
function methods:GetAttribute(k) return (rawget(self, "attrs") or {})[k] end
function methods:RegisterEvent(e) self.events = rawget(self, "events") or {}; self.events[e] = true end
function methods:UnregisterEvent(e) self.unregistered = rawget(self, "unregistered") or {}; self.unregistered[e] = true end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
function methods:SetMinMaxValues(a, b) self.min, self.max = a, b end
function methods:SetValue(v) local s = rawget(self, "scripts"); if s and s.OnValueChanged then s.OnValueChanged(self, v) end end
frames = {}
function New(kind, name)
  local f = setmetatable({ kind = kind, shown = kind ~= "Frame" and kind ~= "Button" or nil },
    { __index = function(_, k) return methods[k] or (type(k) == "string" and k:match("^%u") and function() end) or nil end })
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name, parent, template)
  local f = New(kind, name)
  if kind == "Button" and not template then f.shown = true end
  if template and template:find("Secure") then f.secure = true; if template:find("ActionButton") then f.shown = true end end
  return f
end
function InCombatLockdown() return combat end
function PlaySound() end
function SecureHandlerSetFrameRef() end
function HideUIPanel(f) f.shown = false end
function SecondsToTime(s) return s .. "s" end
function IsModifiedClick() return false end
function HandleModifiedItemClick() end
UIParent = New("Frame"); GameTooltip = New("GameTooltip"); UIErrorsFrame = New("Frame"); ChatFontNormal = {}

-- Skill lines: every profession, secondary header collapsed until expanded.
primary = { "Alchemy", "Blacksmithing", "Enchanting", "Engineering", "Herbalism", "Inscription", "Jewelcrafting",
  "Leatherworking", "Mining", "Skinning", "Tailoring" }
local secondary = { "Cooking", "First Aid", "Fishing", "Riding" }
secondaryOpen = false
local function lines()
  local out = { { "Professions", true, true } }
  for i, n in ipairs(primary) do out[#out + 1] = { n, false, nil, i == 1 and 450 or 300, 0, 0, 450 } end
  out[#out + 1] = { "Secondary Skills", true, secondaryOpen }
  if secondaryOpen then for _, n in ipairs(secondary) do out[#out + 1] = { n, false, nil, 450, 0, 0, 450 } end end
  return out
end
function GetNumSkillLines() return #lines() end
function GetSkillLineInfo(i) return unpack(lines()[i]) end
function ExpandSkillHeader(i) assert(i == 0); secondaryOpen = true end
local spells = { Alchemy = 1, Blacksmithing = 1, Enchanting = 1, Engineering = 1, Inscription = 1, Jewelcrafting = 1,
  Leatherworking = 1, Tailoring = 1, Smelting = 1, Cooking = 1, ["First Aid"] = 1 }
function GetSpellInfo(name) if spells[name] then return name, nil, "icon-" .. name end end

-- The open trade skill: Alchemy with two categories.
tradeOpen = nil
local recipes = {
  { "Potions", "header", 0, true },
  { "Healing Potion", "trivial", 4 },
  { "Runic Mana Potion", "optimal", 0 },
  { "Flasks", "header", 0, true },
  { "Flask of Endless Rage", "medium", 2 },
}
local reagents = { [2] = { { "Peacebloom", "i", 1, 9 } }, [3] = { { "Goldclover", "i", 2, 0 } },
  [5] = { { "Lichbloom", "i", 7, 14 }, { "Frost Lotus", "i", 1, 2 } } }
function GetTradeSkillLine() return tradeOpen, 450, 450 end
function GetNumTradeSkills() return tradeOpen and #recipes or 0 end
function GetTradeSkillInfo(i) local r = recipes[i]; if r then return r[1], r[2], r[3], r[4] end end
function ExpandTradeSkillSubClass() calls.expandTrade = (calls.expandTrade or 0) + 1 end
function GetTradeSkillNumReagents(i) return #(reagents[i] or {}) end
function GetTradeSkillReagentInfo(i, r) return unpack(reagents[i][r]) end
function GetTradeSkillIcon() return "icon" end
function GetTradeSkillNumMade() return 1, 1 end
function GetTradeSkillItemLink() return nil end
function GetTradeSkillTools() return "Mortar", true end
function GetTradeSkillCooldown() return nil end
selection = 0
function SelectTradeSkill(i) selection = i end
function GetTradeSkillSelectionIndex() return selection end
function DoTradeSkill(i, n) calls.craft = { i, n } end
function CloseTradeSkill() calls.closed = true; tradeOpen = nil end
function IsTradeSkillLinked() return false end
function TradeSkillOnlyShowMakeable(v) calls.makeable = v end
function SetTradeSkillSubClassFilter() calls.subclass = true end
'''

CHECKS = r'''
assert(UIParent.unregistered.TRADE_SKILL_SHOW, "stock window no longer opens itself")
assert(DragonUI.db.profile.modules.professions.enabled == false, "DragonUI professions switched off")
local function fire(event)
  for _, f in ipairs(frames) do
    local ev, sc = rawget(f, "events"), rawget(f, "scripts")
    if ev and ev[event] and sc and sc.OnEvent then sc.OnEvent(f, event) end
  end
end
fire("PLAYER_LOGIN")
local T = EsteriaProfessions_Test
local profs = T.professions()
assert(#profs == 14 and secondaryOpen, "all 14 professions, collapsed header opened: " .. #profs)
assert(profs[1].name == "Alchemy" and profs[12].secondary, "primary first, then secondary")
local mining, herb
for _, p in ipairs(profs) do if p.name == "Mining" then mining = p elseif p.name == "Herbalism" then herb = p end end
assert(mining.spell == "Smelting" and herb.spell == nil, "Mining opens Smelting; Herbalism has nothing to open")
assert(T.rankTitle(450) == "Grand Master" and T.rankTitle(75) == "Apprentice")
assert(#T.minor() == 1 and T.minor()[1].name == "Riding", "Riding is a footer line, not a profession")
assert(#T.unlearned() == 0, "everything learned")

-- Rail armed at login (out of combat).
local alchemyRow = EsteriaProfessionsRail2
assert(alchemyRow:GetAttribute("type1") == "spell" and alchemyRow:GetAttribute("spell") == "Alchemy", "rail casts Alchemy")
local herbRow
for i = 2, 15 do local r = _G["EsteriaProfessionsRail" .. i]; if r.prof and r.prof.name == "Herbalism" then herbRow = r end end
assert(herbRow:GetAttribute("type1") == nil, "Herbalism row casts nothing")

EsteriaProfessions_Toggle()
local frame = EsteriaProfessionsFrame
assert(frame:IsShown(), "window opens")
frame.scripts.OnUpdate(frame)
assert(EsteriaProfessionsCard1:GetAttribute("spell") == "Alchemy", "overview cards armed")

-- Herbalism click shows the gathering card.
herbRow.scripts.PostClick(herbRow)
frame.scripts.OnUpdate(frame)

-- The client opens Alchemy (as if the row's spell was cast).
tradeOpen = "Alchemy"
fire("TRADE_SKILL_SHOW")
frame.scripts.OnUpdate(frame)
assert(calls.makeable == false and calls.subclass, "Blizzard's filters reset")
assert(calls.expandTrade == nil, "no expand when nothing is collapsed (no update loop)")
local rows = T.recipeRows("")
assert(#rows == 5, "two categories, three recipes")
EsteriaProfessionsDB.haveMats = true
rows = T.recipeRows("")
assert(#rows == 4 and rows[2].name == "Healing Potion", "have materials drops Runic Mana Potion")
EsteriaProfessionsDB.haveMats = nil
EsteriaProfessionsDB.skillUps = true
rows = T.recipeRows("")
for _, r in ipairs(rows) do assert(r.name ~= "Healing Potion", "skill-ups drops grey recipes") end
EsteriaProfessionsDB.skillUps = nil
rows = T.recipeRows("lotus")
assert(#rows == 2 and rows[2].name == "Flask of Endless Rage", "search reaches reagent names")

-- Select the flask and craft.
local list
for _, f in ipairs(frames) do if rawget(f, "rows") and rawget(f, "data") then list = f end end
frame.scripts.OnUpdate(frame)
selection = 5
local create, createAll
for _, f in ipairs(frames) do
  local label = rawget(f, "label")
  if label and rawget(label, "text") == "Create" then create = f end
  if label and rawget(label, "text") and rawget(label, "text"):find("^Create all") then createAll = f end
end
create.scripts.OnClick(create)
assert(calls.craft[1] == 5 and calls.craft[2] == 1, "Create makes one")
createAll.scripts.OnClick(createAll)
assert(calls.craft[2] == 2, "Create all makes every one you can")

-- Combat: data changes repaint without touching secure attributes.
combat = true
fire("SKILL_LINES_CHANGED"); frame.scripts.OnUpdate(frame)
combat = false
fire("PLAYER_REGEN_ENABLED"); frame.scripts.OnUpdate(frame)

-- Closing the trade skill returns to the overview; closing the window closes the trade skill.
fire("TRADE_SKILL_CLOSE"); frame.scripts.OnUpdate(frame)
tradeOpen = "Alchemy"; fire("TRADE_SKILL_SHOW")
EsteriaProfessions_Toggle()
assert(not frame:IsShown() and calls.closed, "hiding closes the trade skill")

-- A character missing a profession sees it as a dim, inert card.
table.remove(primary, 6) -- Inscription
EsteriaProfessions_Toggle(); frame.scripts.OnUpdate(frame)
assert(#T.unlearned() == 1 and T.unlearned()[1].name == "Inscription", "unlearned profession listed")
local dim = _G["EsteriaProfessionsCard" .. (#T.professions() + 1)]
assert(dim.prof.unlearned and dim:GetAttribute("type1") == nil, "unlearned card casts nothing")
'''


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(MOCKS)
    for name in ("esteria_style.lua", "EsteriaProfessions.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute(CHECKS)
    print("EsteriaProfessions: all checks passed")


if __name__ == "__main__":
    main()
