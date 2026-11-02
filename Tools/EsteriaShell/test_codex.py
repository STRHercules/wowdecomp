"""Runs the EsteriaPerks addon (style kit, Data.lua, Codex.lua) against the creator skin test's fake widget layer and a
fake server, and checks the Codex: opening, paths, keys, buying, refusals, respec, level-up and reduce motion."""
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

REPO = Path(__file__).resolve().parents[2]
GLUE = REPO / "Tools" / "RetailAssets" / "glue"
ADDON = REPO / "Addons" / "EsteriaPerks"
# shortcut: reuses the skin test's harness by slicing its source; move HARNESS to a shared module if a third test needs it.
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (GLUE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)

GAME = r'''
GlueFontHighlightSmall = nil
local W = HarnessWidget
function W:GetAlpha() return self._alpha or 1 end
function W:Click() local f = self._scripts.OnClick if f then f(self) end end
function W:GetScale() return self._scale or 1 end
function W:SetScale(s) self._scale = s end
now = 100
function GetTime() return now end
function UnitName() return "Tester" end
level = 30
function UnitLevel() return level end
function UnitClass() return "Warrior", "WARRIOR" end
function GetCoinTextureString(c) return c .. "c" end
function IsShiftKeyDown() return false end
function PlaySound(s) sounds[#sounds + 1] = s end
function PlaySoundFile() end
function MouseIsOver() return true end
sounds, chat = {}, {}
DEFAULT_CHAT_FRAME = { AddMessage = function(_, m) chat[#chat + 1] = m end }
SlashCmdList, UISpecialFrames = {}, {}
tinsert = table.insert

-- A fake realm: ranks by id; locks worked out like PerkRules.h for the cards used here.
ranks, sent, earned, respecLock = {}, {}, 5, 0
local CARDS = {}
for path = 0, 5 do
    for slot = 1, 9 do
        local tier = slot <= 3 and 0 or slot <= 6 and 1 or slot <= 8 and 2 or 3
        local id = path * 10 + slot
        if id ~= 5 then CARDS[#CARDS + 1] = { id = id, path = path, tier = tier, maxRank = tier >= 2 and 1 or 2,
            cost = tier == 3 and 2 or 1 } end
    end
end
local GATES = { [0] = { 10, 0 }, { 30, 2 }, { 50, 4 }, { 70, 6 } }
local function spentIn(path) local t = 0 for _, c in ipairs(CARDS) do if path == nil or c.path == path then t = t + (ranks[c.id] or 0) * c.cost end end return t end
local function lockOf(c)
    local r = ranks[c.id] or 0
    if r >= c.maxRank then return 2 end
    if level < GATES[c.tier][1] then return 3 end
    if spentIn(c.path) < GATES[c.tier][2] then return 4 end
    if spentIn() + c.cost > earned then return 5 end
    return 0
end
function snapshot()
    local s = { earned = earned, spent = spentIn(), trait = 2, respecs = 0, respecCost = 0, respecLock = respecLock, cards = {} }
    if s.spent == 0 and respecLock == 0 then s.respecLock = 13 end
    for _, c in ipairs(CARDS) do
        s.cards[#s.cards + 1] = { id = c.id, path = c.path, tier = c.tier, rank = ranks[c.id] or 0, maxRank = c.maxRank,
            cost = c.cost, lock = lockOf(c), planned = c.id ~= 13 }
    end
    return s
end
current = nil
function EsteriaPerks_RequestSnapshot() sent[#sent + 1] = "snapshot" return true end
function EsteriaPerks_GetSnapshot() return current end
function EsteriaPerks_Purchase(id, rank) sent[#sent + 1] = "buy " .. id .. " " .. rank return true end
function EsteriaPerks_Respec() sent[#sent + 1] = "respec" return true end

function fire(times, dt)
    for _ = 1, times or 1 do
        now = now + (dt or 1)
        for _, o in ipairs(created) do local f = o._scripts.OnUpdate if f then f(o, dt or 1) end end
    end
end
function event(name, ...)
    for _, o in ipairs(created) do local f = o._scripts.OnEvent if f then f(o, name, ...) end end
end
function deliver() current = snapshot(); event("ESTERIA_PERK_SNAPSHOT") end
'''

L = LuaRuntime()
L.execute(HARNESS + "\nHarnessWidget = W\n")
L.execute(GAME)
for path in (GLUE / "esteria_style.lua", ADDON / "Data.lua", ADDON / "Codex.lua"):
    L.execute(path.read_text(encoding="utf-8"))
L.execute(r'''
local function check(cond, msg) if not cond then error("FAIL: " .. msg, 2) end print("ok  " .. msg) end
local C = EsteriaCodex
local function key(k) EsteriaCodexFrame._scripts.OnKeyDown(EsteriaCodexFrame, k) fire() end
local function shownCards() local n = 0 for _, c in ipairs(C.cards) do if c.data and c:IsShown() then n = n + 1 end end return n end

event("ADDON_LOADED", "EsteriaPerks")
event("PLAYER_ENTERING_WORLD")
check(sent[#sent] == "snapshot", "asks for a snapshot on entering the world")
deliver()
check(not EsteriaCodexFrame:IsShown(), "a snapshot alone does not open the Codex")

-- Open: the window, then cards cascade in; titles and points from the snapshot.
SlashCmdList.ESTERIAPERKS("")
fire(3)
check(EsteriaCodexFrame:IsShown() and EsteriaCodexFrame:GetAlpha() == 1 and EsteriaCodexFrame:GetScale() == 1, "/fate opens the Codex fully")
check(C.boardTitle._text == "1  COMBAT", "starts on Combat")
check(shownCards() == 8, "Combat shows 8 cards (no Steady Focus for a warrior)")
check(C.unspent._text == "5" and C.points._text:find("0 OF 5"), "5 unspent of 5")
check(C.trait.label._text:find("GLASS CANNON"), "trait badge")
check(C.topRule._w == 940, "top rule grew to full width")
local legendary = C.cards[#C.cards]
local inside = true
for step = 1, 40 do
    fire(1, 0.05)
    for _, s in ipairs(legendary.shimmer) do
        if s:IsShown() then local x = s._points[1][4] if x < 0 or x + s._w > legendary._w + 0.01 then inside = false end end
    end
end
check(inside, "the legendary shimmer stays inside its card")
check(C.cards[1].badge._text == "PREVIEW" and C.cards[1].state._text == "AVAILABLE", "a planned card on sale is badged PREVIEW")

-- Keys: Tab and digits change path, arrows move the selection.
key("2")
check(C.boardTitle._text == "2  SURVIVAL", "2 opens Survival")
key("TAB")
check(C.boardTitle._text == "3  EXPLORATION", "Tab moves to the next path")
key("2")
check(C.selected == 11, "the first card is selected after a path change")
key("RIGHT"); key("RIGHT")
check(C.selected == 13 and C.dName._text == "Surefooted", "Right twice selects Surefooted")
key("DOWN")
check(C.selected == 16, "Down moves to the same column in the band below")
key("UP"); key("RIGHT")
check(C.selected == 13, "Up comes back; Right stops at the band's end")
check(C.buy.label._text:find("LEARN RANK 1"), "Learn button offers rank 1")

-- Buy: Enter opens the confirm, Enter again sends it; the board redraws from the server's answer.
key("ENTER")
check(C.confirm and C.confirm.id == 13 and C.confirmFrame:IsShown(), "Enter opens the confirm")
check(C.confirmFrame.text._text:find("Unspent: 5 to 4", 1, true), "confirm shows the points after")
key("ENTER")
check(sent[#sent] == "buy 13 1" and C.pending, "Enter on the confirm sends the purchase")
key("ENTER")
check(sent[#sent] == "buy 13 1" and #sent == 3, "a second Enter while pending sends nothing")
ranks[13] = 1
event("ESTERIA_PERK_RESULT", 1, 13, 1, 0)
deliver()
fire(3)
check(not C.pending and not C.confirmFrame:IsShown(), "confirm closes on success")
local surefooted
for _, c in ipairs(C.cards) do if c.data and c.data.id == 13 then surefooted = c end end
check(surefooted.pips[1].fill:IsShown() and surefooted.pips[1].fill._h == 3 and not surefooted.pips[2].fill:IsShown(), "the bought rank bar is filled and back to height")
check(C.unspent._text == "4", "unspent ticked down to 4")
check(C.dRank._text == "RANK 1 OF 2", "detail shows rank 1")

-- A refusal: the confirm shakes and says why.
key("ENTER"); key("ENTER")
event("ESTERIA_PERK_RESULT", 1, 13, 2, 3)
fire()
check(C.message._text == "You need a higher level." and C.confirmFrame:IsShown(), "refusal explains itself, confirm stays")
key("ESCAPE")
fire(2)
check(not C.confirm and EsteriaCodexFrame:IsShown(), "Esc closes the confirm first")

-- Locked card: Learn is off and the checklist says why.
key("DOWN")
check(C.selected == 16, "on a Specialist card")
deliver()
fire()
check(C.dLines.req2._text:find("2 points in SURVIVAL") and not C.dLines.req2.boxFill:IsShown(), "unmet path points shown unchecked")

-- Respec: refused away from rest, then allowed; pips drain before the board redraws.
respecLock = 11; deliver()
C.respec:Click()
check(C.message._text:find("Rest in an inn"), "reset refused while not resting")
respecLock = 0; deliver()
C.respec:Click()
check(C.confirm and C.confirm.kind == "respec", "reset opens its confirm")
key("ENTER")
check(sent[#sent] == "respec", "reset sent")
ranks = {}
event("ESTERIA_PERK_RESULT", 2, 0, 0, 0)
deliver()
check(C.holdUntil ~= nil and C.unspent._text == "4", "board holds while the pips drain")
fire(2)
check(C.unspent._text == "5", "after the drain the board shows the refund")

-- Level-up while closed: a chat line, then the ribbon on the next open.
key("ESCAPE"); fire(2)
check(not EsteriaCodexFrame:IsShown(), "Esc closes the Codex")
earned = 6; level = 35
event("PLAYER_LEVEL_UP")
deliver()
check(chat[#chat]:find("perk point"), "a new point is announced in chat")
EsteriaPerks_Toggle()
fire(2)
check(C.ribbon:IsShown() and C.ribbon.label._text == "NEW PERK POINT", "ribbon on the next open")

-- Summary lists what is owned.
ranks[13] = 2; ranks[11] = 1; deliver()
key("7")
check(C.boardTitle._text == "SUMMARY" and shownCards() == 0, "7 opens the summary")
local E = C.summaryEntries
check(E[1].name._text == "Field Medic" and E[2].name._text == "Surefooted" and E[2].rank._text == "2 / 2", "summary lists owned perks with ranks")
check(C.summaryHeads[1].name._text == "2  SURVIVAL" and C.summaryHeads[1].points._text == "3 PTS", "grouped under their path's heading")
local flat = true
for _, e in ipairs(E) do if e.name:IsShown() and (e.name._points[1][4] ~= 12 or e.text._points[1][4] ~= 12) then flat = false end end
check(flat and E[1].text._w <= 438 - 24, "entries share one indent and fit the board")
check(E[2].tag._text == "" and E[1].tag._text == "PREVIEW", "previews are tagged, working perks are not")
check(C.overview:IsShown() and not C.body:IsShown() and not C.buy:IsShown(), "the detail panel shows the overview")
for id = 21, 28 do ranks[id] = 1 end
deliver()
check(C.more:IsShown(), "a long list says it scrolls")
C.summaryFrame._scripts.OnMouseWheel(C.summaryFrame, -1)
check(C.summaryScroll == 48, "the wheel scrolls the list")

-- Disabled realm: the board says so.
event("ESTERIA_PERK_RESULT", 0, 0, 0, 10)
key("1")
check(C.status:IsShown() and C.status._text:find("not enabled"), "disabled realm explained")

-- Reduce motion: tweens land at once.
SlashCmdList.ESTERIAPERKS("motion")
check(EsteriaStyle.reduceMotion and EsteriaPerksDB.reduceMotion, "/fate motion turns motion down and saves it")
EsteriaPerks_Toggle(false)
check(not EsteriaCodexFrame:IsShown(), "closes at once with reduced motion")
print("PASS")
''')
