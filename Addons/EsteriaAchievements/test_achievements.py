"""Lua behavior check for EsteriaAchievements with UI mocks; does not prove in-game rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(r'''
unpack = unpack or table.unpack; tinsert = table.insert; format = string.format
bit = { band = function(a, b) local r, p = 0, 1; while a > 0 and b > 0 do
  if a % 2 == 1 and b % 2 == 1 then r = r + p end; a, b, p = math.floor(a / 2), math.floor(b / 2), p * 2 end; return r end }
UISpecialFrames, SlashCmdList, tracked = {}, {}, {}
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetStringHeight() return 14 end
function methods:GetWidth() return 200 end
function methods:GetHeight() return 300 end
function methods:Show() local was = self.shown; self.shown = true; if not was and self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
function methods:IsEnabled() return 1 end
function methods:SetScript(k, f) self.scripts[k] = f end
function methods:HookScript(k, f) self.hooks[k] = f end
function methods:RegisterEvent(e) self.events[e] = true end
function methods:UnregisterEvent(e) self.events[e] = nil end
function methods:GetVerticalScroll() return 0 end
function methods:SetMinMaxValues(a, b) self.min, self.max = a, b end
function methods:SetValue(v) if self.scripts.OnValueChanged then self.scripts.OnValueChanged(self, v) end end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
frames = {}
function New(kind, name)
  local f = setmetatable({ kind = kind, scripts = {}, hooks = {}, events = {}, shown = kind ~= "Frame" or name == nil },
    { __index = function(_, k) return methods[k] or function() end end })
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name) return New(kind, name) end
function PlaySound() end
function HideUIPanel(f) f.shown = false end
function IsModifiedClick() return false end

-- Categories: 1 General (child 11), 2 Quests (children 21, 22); 300 achievements each in 1, 11, 21, 22.
cats = { 1, 11, 2, 21, 22 }
parents = { [1] = -1, [11] = 1, [2] = -1, [21] = 2, [22] = 2 }
names = { [1] = "General", [11] = "Level", [2] = "Quests", [21] = "Eastern Kingdoms", [22] = "Kalimdor" }
ach, byCat = {}, {}
for _, c in ipairs(cats) do
  byCat[c] = {}
  for i = 1, 300 do
    local id = c * 1000 + i
    ach[id] = { id, (i == 7 and "Explorer of Ages " or "Deed ") .. id, (i % 3 + 1) * 5, i % 4 == 0, 11, 20, 26,
                "Do thing " .. id, i == 5 and 0x80 or 0, "icon" .. id, i == 9 and "Title: the Bold" or "" }
    table.insert(byCat[c], id)
  end
end
function GetCategoryList() return cats end
function GetStatisticsCategoryList() return { 900 } end
function GetCategoryInfo(id) if id == 900 then return "Combat", -1 end return names[id], parents[id] end
function GetCategoryNumAchievements(id)
  if id == 900 then return 3, 0 end
  local done = 0; for _, a in ipairs(byCat[id]) do if ach[a][4] then done = done + 1 end end
  return #byCat[id], done
end
function GetAchievementInfo(a, b)
  if b then if a == 900 then return 90000 + b, "Stat " .. b, 0, false end a = byCat[a][b] end
  local r = ach[a]; if not r then return nil end
  return unpack(r)
end
function GetAchievementCategory(id) return math.floor(id / 1000) end
function GetAchievementNumCriteria(id) return (id % 1000 == 5) and 1 or 4 end
function GetAchievementCriteriaInfo(id, i)
  if id % 1000 == 5 then return "Loot gold", 0, false, 30, 120, nil, 1, 0, "30 / 120" end
  if id == 1006 and i == 1 then return "", 8, true, 0, 0, nil, 0, 1004 end -- meta criterion: another achievement
  return "Step " .. i, 0, i <= 2, 0, 0, nil, i == 4 and 2 or 0, 0
end
function GetPreviousAchievement(id) if id == 1003 then return 1002 elseif id == 1002 then return 1001 end end
function GetNextAchievement(id) if id == 1001 then return 1002 elseif id == 1002 then return 1003 end end
function GetStatistic(id) return id == 90002 and "--" or tostring(id) end
function GetTotalAchievementPoints() return 4200 end
function GetNumCompletedAchievements() return 1500, 375 end
function GetLatestCompletedAchievements() return 1004, 1008 end
function GetAchievementLink(id) return "|Hachievement:" .. id .. "|h[x]|h" end
function IsTrackedAchievement(id) return tracked[id] end
function AddTrackedAchievement(id) tracked[id] = true end
function RemoveTrackedAchievement(id) tracked[id] = nil end
function GetNumTrackedAchievements() local n = 0; for _ in pairs(tracked) do n = n + 1 end return n end
GameTooltip = New("GameTooltip"); UIErrorsFrame = New("Frame"); ChatFontNormal = {}
''')
    for name in ("esteria_style.lua", "EsteriaAchievements.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute(r'''
local T = EsteriaAchievements_Test
-- Binding / micro button path.
ToggleAchievementFrame()
assert(EsteriaAchievementsFrame:IsShown(), "opens from ToggleAchievementFrame")
local m = T.models.ach
assert(#m.items == 1500 and #m.roots == 2, "scanned 1500 achievements in 2 root categories")
assert(m.cats[2].sumTotal == 900 and m.cats[2].sumDone == 225, "root counts roll up children")
assert(#T.models.stat.items == 3)

T.selectedCat.ach = 21
local rail = T.railEntries("ach")
assert(#rail == 4 and rail[3].id == 21 and rail[3].depth == 1, "selected root opens its children")
T.selectedCat.ach = 2
assert(#T.filtered("ach", "", "all", "default") == 900, "a root shows its whole subtree")
assert(#T.filtered("ach", "", "earned", "default") == 225)
assert(#T.filtered("ach", "explorer of ages", "all", "default") == 5, "search ignores category")
local recent = T.filtered("ach", "", "earned", "points")
assert(recent[1].points >= recent[#recent].points)
assert(T.progress(1005, 0x80) == 0.25, "progress-bar achievement uses quantity/required")
assert(math.abs(T.progress(1006, 0) - 2 / 3) < 1e-9, "hidden criteria don't count")

-- Tabs paint without errors.
EsteriaAchievements_Select("Statistics")
EsteriaAchievements_Select("Achievements")
EsteriaAchievements_Select("Summary")

-- Alert / tracker path: the stock addon loads, its functions redirect to this window.
AchievementFrame = New("Frame", "AchievementFrame")
function AchievementFrame_DisplayComparison() AchievementFrame:Show() end
local watcher
for _, f in ipairs(frames) do if f.events.ADDON_LOADED then watcher = f end end
watcher.scripts.OnEvent(watcher, "ADDON_LOADED", "Blizzard_AchievementUI")
AchievementFrame_SelectAchievement(21017)
assert(T.selectedCat.ach == 21, "select jumps to the achievement's category")
local list = nil
-- Detail paints series and the meta criterion for these ids.
AchievementFrame_SelectAchievement(1002)
AchievementFrame_SelectAchievement(1006)
-- Stock frame showing on its own is hidden and ours shown; comparison keeps the stock frame.
AchievementFrame.shown = false
AchievementFrame:Show(); AchievementFrame.hooks.OnShow(AchievementFrame)
assert(not AchievementFrame.shown and EsteriaAchievementsFrame:IsShown())
AchievementFrame_DisplayComparison("target"); AchievementFrame.hooks.OnShow(AchievementFrame)
assert(AchievementFrame.shown, "inspect comparison stays on the stock frame")

-- Toggle closes; stats opens the Statistics tab.
ToggleAchievementFrame()
assert(not EsteriaAchievementsFrame:IsShown())
ToggleAchievementFrame(true)
assert(EsteriaAchievementsFrame:IsShown())
''')
    print("EsteriaAchievements: all checks passed")


if __name__ == "__main__":
    main()
