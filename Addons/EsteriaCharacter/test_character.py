"""Lua behavior check for EsteriaCharacter with UI mocks; does not prove in-game rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert; format = string.format; time = os.time
function strtrim(s, chars) assert(chars == nil, "strtrim got a second argument"); return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
function strupper(s) return s:upper() end
UISpecialFrames, SlashCmdList, calls = {}, {}, {}
DragonUI = { db = { profile = { modules = { characterpanel = { enabled = true } } } } }
ClasslessWildcard_API = { state = { universalResources = 1 }, stats = { have = true, mp5 = 42, mp5Casting = 12 },
  ShowStatTooltip = function(frame, i) calls.cwTooltip = i end }
ITEM_QUALITY_COLORS = { [3] = { r = 0, g = 0.44, b = 0.87 }, [4] = { r = 0.64, g = 0.21, b = 0.93 } }
FACTION_BAR_COLORS = { [4] = { r = 1, g = 1, b = 0 }, [7] = { r = 0, g = 0.6, b = 0.1 } }
FACTION_STANDING_LABEL4, FACTION_STANDING_LABEL7 = "Neutral", "Revered"
SPELL_STAT1_NAME, SPELL_STAT2_NAME, SPELL_STAT3_NAME, SPELL_STAT4_NAME, SPELL_STAT5_NAME = "Strength", "Agility", "Stamina", "Intellect", "Spirit"
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetStringHeight() return 14 end
function methods:GetStringWidth() return 50 end
function methods:GetWidth() return rawget(self, "w") or 200 end
function methods:SetWidth(w) self.w = w end
function methods:GetFrameLevel() return 1 end
function methods:GetLeft() return 100 end
function methods:GetTop() return 700 end
function methods:SetSize(w, h) self.w = w end
function methods:GetHeight() return 300 end
function methods:Show() local was = rawget(self, "shown"); self.shown = true
  local s = rawget(self, "scripts"); if not was and s and s.OnShow then s.OnShow(self) end
  for _, f in ipairs(rawget(self, "onshow") or {}) do f(self) end end
function methods:Hide() local was = rawget(self, "shown"); self.shown = false
  local s = rawget(self, "scripts"); if was and s and s.OnHide then s.OnHide(self) end end
function methods:IsShown() return rawget(self, "shown") end
function methods:IsEnabled() return 1 end
function methods:SetScript(k, f) self.scripts = rawget(self, "scripts") or {}; self.scripts[k] = f end
function methods:GetScript(k) return (rawget(self, "scripts") or {})[k] end
function methods:HookScript(k, f)
  if k == "OnShow" then self.onshow = rawget(self, "onshow") or {}; table.insert(self.onshow, f); return end
  local s = rawget(self, "scripts") or {}; self.scripts = s
  local old = s[k]; s[k] = function(...) if old then old(...) end f(...) end
end
function methods:RegisterEvent(e) end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
function methods:SetMinMaxValues(a, b) self.min, self.max = a, b end
function methods:SetValue(v) local s = rawget(self, "scripts"); if s and s.OnValueChanged then s.OnValueChanged(self, v) end end
function methods:GetVerticalScroll() return 0 end
function methods:SetUnit(u) calls.model = u end
function methods:SetTexture(t) self.texture = t; return 1 end
function methods:SetTexCoord(...) self.coords = { ... } end
function methods:SetScale(v) self.scale = v end
function methods:SetVertexColor(r, g, b, a) self.vc = { r, g, b, a } end
frames = {}
function New(kind, name)
  local f = setmetatable({ kind = kind, shown = kind ~= "Frame" or name == nil },
    { __index = function(_, k) return methods[k] or (type(k) == "string" and k:match("^%u") and function() end) or nil end })
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name) return New(kind, name) end
function PlaySound() end
function GetTime() return 0 end
function HideUIPanel(f) f.shown = false end
function GetCursorPosition() return 0, 0 end
function InCombatLockdown() return false end
function IsModifiedClick() return false end
UIErrorsFrame = New("Frame"); GameTooltip = New("GameTooltip"); ChatFontNormal = {}

function UnitPVPName() return "Zach the Immortal" end
function UnitName() return "Zach" end
function UnitRace() return "Human" end
function UnitClass() return "Warrior", "WARRIOR" end
function UnitLevel() return 81 end
function GetGuildInfo() return "Immortal" end
function UnitFactionGroup() return "Alliance" end

-- Inventory: 2H main hand (ilvl 245, enchanted, 2 gems), helm (232), broken chest, everything else empty.
local SLOT_IDS = { HeadSlot = 1, NeckSlot = 2, ShoulderSlot = 3, ShirtSlot = 4, ChestSlot = 5, WaistSlot = 6, LegsSlot = 7,
  FeetSlot = 8, WristSlot = 9, HandsSlot = 10, Finger0Slot = 11, Finger1Slot = 12, Trinket0Slot = 13, Trinket1Slot = 14,
  BackSlot = 15, MainHandSlot = 16, SecondaryHandSlot = 17, RangedSlot = 18, TabardSlot = 19 }
local links = { [1] = "|Hitem:100:0:0:0:0|h[Helm]|h", [5] = "|Hitem:101:0:0:0:0|h[Chest]|h",
  [16] = "|Hitem:102:3789:40111:40112:0|h[Axe]|h" }
local items = { [100] = { "Helm", 4, 232, "INVTYPE_HEAD" }, [101] = { "Chest", 3, 200, "INVTYPE_CHEST" },
  [102] = { "Axe", 4, 245, "INVTYPE_2HWEAPON" }, [500] = { "Badge", 4, 1, "", "badge-icon" } }
function GetInventorySlotInfo(name) return SLOT_IDS[name], "empty-" .. name end
function GetInventoryItemLink(_, id) return links[id] end
function GetInventoryItemTexture(_, id) return links[id] and "icon" end
function GetInventoryItemCount() return 1 end
function GetInventoryItemDurability(id) if id == 5 then return 0, 100 end if links[id] then return 100, 100 end end
function GetItemInfo(x)
  local id = type(x) == "number" and x or tonumber(x:match("item:(%d+)"))
  local i = items[id]; if not i then return end
  return i[1], "|Hitem:" .. id .. "|h", i[2], i[3], 0, "", "", 1, i[4], i[5]
end
function PickupInventoryItem(id) calls.pickup = id end
function HandleModifiedItemClick() return false end

-- Stats.
function UnitStat(_, i) return 100, 120, 20, 0 end
function UnitHealthMax() return 30000 end
function UnitPowerMax() return 0 end
function UnitAttackPower() return 3000, 500, 0 end
function UnitDamage() return 1200, 1500 end
function UnitAttackSpeed() return 3.6 end
function GetCombatRatingBonus() return 5.5 end
function GetCritChance() return 25 end
function GetExpertise() return 26 end
function GetArmorPenetration() return 10 end
function UnitRangedAttackPower() return 0, 0, 0 end
function GetRangedCritChance() return 5 end
function GetSpellBonusDamage() return 80 end
function GetSpellCritChance() return 4 end
function GetSpellBonusHealing() return 80 end
function GetManaRegen() return 0, 0 end
function GetSpellPenetration() return 0 end
function UnitArmor() return 20000, 21000 end
function UnitDefense() return 400, 140 end
function GetDodgeChance() return 15 end
function GetParryChance() return 14 end
function GetBlockChance() return 20 end
function GetCombatRating() return 0 end
function UnitResistance() return 0, 25 end

-- Titles and sets.
function GetNumTitles() return 3 end
function IsTitleKnown(i) return i ~= 2 and 1 or nil end
function GetTitleName(i) return ({ "%s the Immortal ", "Hidden", "Champion of the Naaru " })[i] end
function GetCurrentTitle() return 1 end
function SetCurrentTitle(i) calls.title = i end
function GetNumEquipmentSets() return 1 end
function GetEquipmentSetInfo() return "Tank", "icon" end

-- Reputation: one collapsed header.
reps = { { "Wrath", true, false, true }, { "Kirin Tor", false, false, false, 7 }, { "Classic", true, false, false },
  { "Stormwind", false, false, false, 4 } }
expandedWrath = false
local function visibleReps()
  local v = {}
  for _, r in ipairs(reps) do if not (r[1] == "Kirin Tor" and not expandedWrath) then v[#v + 1] = r end end
  return v
end
function GetNumFactions() return #visibleReps() end
function GetFactionInfo(i)
  local r = visibleReps()[i]
  if r[2] then return r[1], "", 0, 0, 0, 0, false, false, true, r[1] == "Wrath" and not expandedWrath end
  return r[1], r[1] .. " desc", r[5], 0, 21000, 10500, false, true, false, false, true, false, false
end
function ExpandFactionHeader(i) assert(visibleReps()[i][1] == "Wrath"); expandedWrath = true end
function IsFactionInactive() return false end

-- Skills and currency.
function ExpandSkillHeader() end
function GetNumSkillLines() return 3 end
function GetSkillLineInfo(i)
  return ({ { "Professions", true }, { "Mining", false, nil, 450, 0, 0, 450, true, nil, nil, nil, nil, "Dig" },
            { "Herbalism", false, nil, 300, 0, 5, 450, true } })[i][1], ({ true, false, false })[i], nil,
         ({ 0, 450, 300 })[i], 0, ({ 0, 0, 5 })[i], ({ 0, 450, 450 })[i], i > 1, nil, nil, nil, nil, i == 2 and "Dig" or nil
end
function GetCurrencyListSize() return 2 end
function GetCurrencyListInfo(i)
  if i == 1 then return "Dungeon and Raid", true, true end
  return "Emblem of Frost", false, true, false, false, 34, 0, nil, 500
end
function GetMoney() return 123456 end
function GetCoinTextureString(c) return c .. "c" end

CharacterFrame = New("Frame", "CharacterFrame"); CharacterFrame.shown = false
function PanelTemplates_GetSelectedTab(f) return f.tab end
stockPet = 0
function ToggleCharacter(name) stockPet = stockPet + 1 end
'''

CHECKS = r'''
assert(DragonUI.db.profile.modules.characterpanel.enabled == false, "DragonUI character panel switched off")
local T = EsteriaCharacter_Test
ToggleCharacter("PaperDollFrame")
local frame = EsteriaCharacterFrame
assert(frame:IsShown() and calls.model == "player", "C opens the sheet with the model on the player")

local d = T.itemDetails("|Hitem:102:3789:40111:40112:0|h[Axe]|h")
assert(d.ilvl == 245 and d.enchanted and d.gems == 2, "item details parsed")
-- Helm 232 + Chest 200 + 2H 245 for both hands; 12 other empty slots count as 0 (shirt, tabard, empty ranged excluded).
local avg = T.averageItemLevel()
assert(math.abs(avg - (232 + 200 + 245 + 245) / 16) < 0.01, "average item level " .. avg)

local found
for _, s in ipairs(T.statSections()) do
  for _, row in ipairs(s.rows) do if s.title == "Spell" and row[1] == "Mana regen" then found = row[2] end end
end
assert(found == "42  (12 casting)", "classless mana regen from the server: " .. tostring(found))

local titles = T.titleRows("")
assert(#titles == 3 and titles[3].name == "the Immortal", "titles: none + known, %s stripped")

local facs, groups = T.factions()
assert(expandedWrath and #groups == 2 and groups[1].count == 1, "collapsed header expanded, grouped")
local skillList = T.skills()
assert(#skillList == 2 and skillList[2].modifier == 5)
local cur = T.currencies()
assert(#cur == 1 and cur[1].icon == "badge-icon" and cur[1].count == 34, "currency icon from its item")

-- Paint every tab and side panel.
for _, side in ipairs({ "Stats", "Titles", "Gear sets" }) do T.selected.side = side; T.refresh() end
T.selected.faction = "Kirin Tor"; EsteriaCharacter_Toggle("Reputation")
T.selected.skill = "Mining"; EsteriaCharacter_Toggle("Skills")
T.selected.currency = "Emblem of Frost"; EsteriaCharacter_Toggle("Currency")

-- Pet tab stays stock; the stock window is redirected; toggle closes.
ToggleCharacter("PetPaperDollFrame")
assert(stockPet == 1, "pet tab left to the stock window")
CharacterFrame.tab = 3; CharacterFrame:Show()
assert(not CharacterFrame:IsShown() and frame:IsShown(), "stock window redirected")
ToggleCharacter("ReputationFrame")
assert(not frame:IsShown(), "same tab toggles closed")

-- Collapse: side panel and tabs slide away, the window narrows to the gear, other tabs open it back up.
EsteriaStyle.reduceMotion = true
ToggleCharacter("PaperDollFrame")
assert(frame:IsShown() and frame.w == 720, "opens full width")
EsteriaCharacter_SetCollapsed(true)
local narrow = frame.w
assert(narrow < 400 and EsteriaCharacterDB.collapsed, "collapsed to " .. tostring(narrow))
EsteriaCharacter_Toggle("Skills")
assert(frame.w == 720, "other tabs open the window up")
EsteriaCharacter_Toggle("Character")
assert(frame.w == narrow, "Character comes back collapsed")
EsteriaCharacter_SetCollapsed(false)
assert(frame.w == 720 and not EsteriaCharacterDB.collapsed, "expands again")

-- Settings popout and the dressing room picker.
EsteriaCharacter_ToggleSettings(true)
local pop = T.popout()
assert(pop and pop:IsShown(), "settings popout opens")
local cp = T.panes.Character
T.refresh()
assert(cp.room[1].texture:find("DressUpBackground%-Human1") and cp.room[4]:IsShown(), "automatic: own race, four tiles")
T.setOpt("room", "class:mage"); T.refresh()
assert(cp.room[1].texture:find("EsteriaCharacter", 1, true) and cp.room[1].texture:sub(-4) == "mage"
  and not cp.room[2]:IsShown(), "retail room: one picture")
assert(cp.room[1].coords and cp.room[1].coords[2] <= 480 / 512 + 1e-9, "retail room cropped to its painted area")
T.setOpt("room", "race:Tauren"); T.refresh()
assert(cp.room[1].texture:find("Tauren1"), "picked race room")
T.setOpt("room", "none"); T.refresh()
assert(not cp.room[1]:IsShown() and not cp.fade:IsShown(), "no room, no fade")
T.setOpt("dim", 0.5); T.refresh()
assert(cp.wash.vc[4] == 0.5, "dimming applied")
T.setOpt("scale", 1.2)
assert(frame.scale == 1.2, "window scale applied")
EsteriaCharacterDB.settings.hiddenSections = { Melee = true }; T.setOpt("hideZero", true)
T.selected.side = "Stats"; T.refresh()
pop.Refresh()
EsteriaCharacter_ToggleSettings(false)
assert(not pop:IsShown(), "settings popout closes (reduced motion)")
EsteriaCharacterDB.settings = nil; T.refresh()
'''


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(MOCKS)
    for name in ("esteria_style.lua", "EsteriaCharacter.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute(CHECKS)
    print("EsteriaCharacter: all checks passed")


if __name__ == "__main__":
    main()
