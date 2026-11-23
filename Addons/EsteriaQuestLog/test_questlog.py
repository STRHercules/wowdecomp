"""Lua behavior check for EsteriaQuestLog with UI mocks and a 1000-quest log; does not prove in-game rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(r'''
unpack = unpack or table.unpack; tinsert = table.insert; format = string.format
UISpecialFrames, SlashCmdList, watched, popups = {}, {}, {}, {}
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetStringHeight() return 14 end
function methods:GetStringWidth() return 60 end
function methods:GetWidth() return 220 end
function methods:GetHeight() return 300 end
function methods:Show() self.shown = true; if self.scripts.OnShow then self.scripts.OnShow(self) end end
function methods:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function methods:IsShown() return self.shown end
function methods:IsEnabled() return 1 end
function methods:SetScript(k, f) self.scripts[k] = f end
function methods:HookScript(k, f) self.hooks[k] = f end
function methods:GetVerticalScroll() return 0 end
function methods:SetMinMaxValues(a, b) self.min, self.max = a, b end
function methods:GetMinMaxValues() return rawget(self, "min") or 0, rawget(self, "max") or 0 end
function methods:SetValue(v) self.value = v; if self.scripts.OnValueChanged then self.scripts.OnValueChanged(self, v) end end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
function New(kind, name)
  local f = setmetatable({ kind = kind, scripts = {}, hooks = {}, shown = true },
    { __index = function(_, k) return methods[k] or function() end end })
  if name then _G[name] = f end
  return f
end
function CreateFrame(kind, name) local f = New(kind, name); if kind == "Frame" or kind == "Button" then end return f end
function PlaySound() end
function HideUIPanel(f) f.shown = false end

-- A 1000-quest log: 40 zones x 25 quests, zone 3 collapsed until expanded.
log, collapsed3 = {}, true
for z = 1, 40 do
  table.insert(log, { header = true, title = "Zone " .. z })
  for k = 1, 25 do
    local id = z * 100 + k
    table.insert(log, { title = (k == 1 and "Kobold Camp " or "Quest ") .. id, level = 10 + k, id = id,
      tag = (k == 2 and "Elite") or (k == 3 and "Dungeon") or (k == 4 and "Raid") or nil,
      complete = (k == 5 and 1) or (k == 6 and -1) or nil, daily = (k == 7) and 1 or nil, zone = z,
      objs = { { "Kobold slain: " .. (k % 9) .. "/8", k % 9 >= 8 } } })
  end
end
local function visible()
  local v = {}
  for _, e in ipairs(log) do if e.header or not (collapsed3 and e.zone == 3) then v[#v + 1] = e end end
  return v
end
function GetNumQuestLogEntries() return #visible() end
function GetQuestLogTitle(i)
  local e = visible()[i]
  if e.header then return e.title, nil, nil, 0, 1, (e.title == "Zone 3" and collapsed3) and 1 or nil end
  return e.title, e.level, e.tag, 0, nil, nil, e.complete, e.daily, e.id
end
function ExpandQuestHeader(i) assert(i == 0); collapsed3 = false end
function IsQuestWatched(i) return watched[visible()[i].id] end
function AddQuestWatch(i) watched[visible()[i].id] = 1 end
function RemoveQuestWatch(i) watched[visible()[i].id] = nil end
function GetNumQuestWatches() local n = 0; for _ in pairs(watched) do n = n + 1 end return n end
function GetNumQuestLeaderBoards(i) return #visible()[i].objs end
function GetQuestLogLeaderBoard(j, i) local o = visible()[i].objs[j]; return o[1], "monster", o[2] end
selection = 0
function SelectQuestLogEntry(i) selection = i end
function GetQuestLogSelection() return selection end
function GetQuestLogQuestText() return "Story text", "Kill kobolds" end
function GetQuestLogTimeLeft() return nil end
function GetQuestLogRequiredMoney() return 0 end
function GetQuestLogGroupNum() return 0 end
function GetNumQuestLogChoices() return 2 end
function GetQuestLogChoiceInfo(i) return "Choice " .. i, "icon", 1, 2 end
function GetNumQuestLogRewards() return 1 end
function GetQuestLogRewardInfo() return "Bag", "icon", 3, 1 end
function GetQuestLogRewardMoney() return 12345 end
function GetQuestLogRewardSpell() return nil end
function GetCoinTextureString(c) return c .. "c" end
function GetNumPartyMembers() return 0 end
function GetNumRaidMembers() return 0 end
function GetQuestLogPushable() return 1 end
function GetQuestLogCapacity() return 1000 end
function GetQuestDifficultyColor() return { r = 1, g = 1, b = 0 } end
function SetAbandonQuest() end
function GetAbandonQuestName() return "Quest" end
function StaticPopup_Show(n) popups[#popups + 1] = n end
function IsModifiedClick() return false end
QuestLogFrame = New("Frame", "QuestLogFrame"); QuestLogDetailFrame = New("Frame", "QuestLogDetailFrame")
GameTooltip = New("GameTooltip"); UIErrorsFrame = New("Frame"); ChatFontNormal = {}
''')
    for name in ("esteria_style.lua", "EsteriaQuestLog.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute(r'''
assert(MAX_QUESTLOG_QUESTS == 1000)
-- The binding/micro button path: stock frame shows, gets hidden, ours opens.
QuestLogFrame.hooks.OnShow(QuestLogFrame)
assert(EsteriaQuestLogFrame:IsShown(), "window opens from the stock path")
local T = EsteriaQuestLog_Test
local s = T.state()
assert(#s.quests == 1000, "all 1000 quests scanned, got " .. #s.quests)
assert(not collapsed3, "collapsed header expanded")
assert(#s.zones == 40)
assert(#s.rows == 1040, "40 zone headers + 1000 quests, got " .. #s.rows)
assert(s.statusCounts.complete == 40 and s.statusCounts.failed == 40 and s.statusCounts.all == 1000)
assert(s.typeCounts.daily == 40 and s.typeCounts.raid == 40 and s.typeCounts.dungeon == 40 and s.typeCounts.group == 40)
assert(s.selectedID == 101, "first quest auto-selected")

local function rowsNow(st, ty, zo, q)
  T.setFilters(st, ty, zo); EsteriaQuestLogSearch:SetText(q or ""); T.rebuild(); return T.state().rows
end
assert(#rowsNow("complete") == 80, "ready-to-turn-in: 40 headers + 40 quests")
assert(#rowsNow("all", "raid") == 80)
assert(#rowsNow("all", nil, "Zone 7") == 26)
local r = rowsNow("all", nil, nil, "3917")
assert(#r == 2 and r[2].id == 3917, "search by quest id")
assert(#rowsNow("all", nil, nil, "kobold camp") == 80, "search by title")
assert(#rowsNow("all", nil, nil, "slain: 8/8") > 0, "search objectives text")

EsteriaQuestLogDB.sort = "progress"
r = rowsNow("all")
assert(#r == 1000 and r[1].progress == 1 and r[1000].progress <= r[999].progress, "progress sort is flat and descending")
EsteriaQuestLogDB.sort = "zone"
EsteriaQuestLogDB.collapsed["Zone 1"] = true
assert(#rowsNow("all") == 1015, "collapsing a zone hides its 25 quests")
EsteriaQuestLogDB.collapsed["Zone 1"] = nil

assert(T.objectiveProgress("Kobold slain: 3/8", false) == 0.375)
assert(T.objectiveProgress("Find it", true) == 1 and T.objectiveProgress("Find it", false) == 0)

-- Tracker / popup path selects the quest in our window; quest 999 of the log (index past 255) works.
QuestLog_OpenToQuest(1040)
assert(T.state().selectedID == 4025, "open-to-quest selects the last entry (index 1040), got " .. tostring(T.state().selectedID))
EsteriaQuestLogFrame.scripts.OnUpdate(EsteriaQuestLogFrame, 0.1)
assert(selection == 1040, "detail pane selected the log entry")

-- Toggle closes from the stock path when already open.
QuestLogFrame.hooks.OnShow(QuestLogFrame)
assert(not EsteriaQuestLogFrame:IsShown())
''')
    print("EsteriaQuestLog: all checks passed")


if __name__ == "__main__":
    main()
