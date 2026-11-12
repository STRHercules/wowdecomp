"""Runs the EsteriaManastorm addon (style kit, generated data, window) against the creator skin test's fake widget layer
and a fake native API (Extensions/Manastorm), and checks: modes, checkpoints for a group, entering, the run panel and
tracker, gadgets, rewards, toasts on a cleared depth (with its unlock), and the server's refusals."""
import re
from pathlib import Path
from lupa.lua51 import LuaRuntime

REPO = Path(__file__).resolve().parents[2]
GLUE = REPO / "Tools" / "RetailAssets" / "glue"
ADDON = REPO / "Addons" / "EsteriaManastorm"
HARNESS = re.search(r"HARNESS = r'''(.*?)'''", (GLUE / "test_creator_skin.py").read_text(encoding="utf-8"), re.S).group(1)

GAME = r'''
GlueFontHighlightSmall = nil
local W = HarnessWidget
function W:Click() local f = self._scripts.OnClick if f then f(self) end end
function W:Enable() self._disabled = nil end
function W:Disable() self._disabled = true end
function W:IsEnabled() return self._disabled and 0 or 1 end
function W:SetDesaturated(v) self._desat = v end
function W:SetTexture(t) self._tex = t end
-- as in the game: showing a hidden frame runs OnShow, hiding a shown one OnHide
function W:Show() local was = self._shown self._shown = true if not was and self._scripts.OnShow then self._scripts.OnShow(self) end end
function W:Hide() local was = self._shown self._shown = false if was and self._scripts.OnHide then self._scripts.OnHide(self) end end
now = 100
function GetTime() return now end
sounds, errors, chat, popups = {}, {}, {}, {}
function PlaySound(s) sounds[#sounds + 1] = s end
UIErrorsFrame = { AddMessage = function(_, m) errors[#errors + 1] = m end }
DEFAULT_CHAT_FRAME = { AddMessage = function(_, m) chat[#chat + 1] = m end }
GameTooltip = setmetatable({}, { __index = function() return function() end end })
SlashCmdList, UISpecialFrames, StaticPopupDialogs = {}, {}, {}
function StaticPopup_Show(name, a) popups[#popups + 1] = { name, a } end
tinsert = table.insert
YES, NO, MAX_PLAYER_LEVEL = "Yes", "No", 80

-- a group: player plus party members with levels and GUIDs
party = {}
function GetNumPartyMembers() return #party end
function GetNumRaidMembers() return 0 end
function UnitIsUnit(a, b) return a == b end
function UnitLevel(u) if u == "player" then return 70 end local i = tonumber(u:match("%d+")) return party[i] and party[i].level end
function UnitName(u) if u == "player" then return "Tester" end local i = tonumber(u:match("%d+")) return party[i] and party[i].name end
function UnitGUID(u) local i = tonumber(u:match("%d+")) return party[i] and party[i].guid end
leader = true
function IsPartyLeader() return leader and 1 or nil end
function IsRaidLeader() return nil end
known = {}
function IsSpellKnown(s) return known[s] == true end
function GetSpellInfo(s) if not s or s == 0 then return nil end return "Gadget" .. s, "Rank 1", "icon" .. s end
function GetItemIcon(i) return "item" .. i end
function GetItemCount(i) return 7 end

-- the fake native API
config, progress, active, link, loadout, rewards, sent = {}, {}, nil, 0, { 0, 0, 0, 0 }, {}, {}
function EsteriaManastorm_GetConfig(k) return config[k] end
function EsteriaManastorm_GetCompletedDepths(mode, guid) local p = progress[guid or "player"] return p and p[mode] or {} end
function EsteriaManastorm_GetActive() if active then return unpack(active) end end
function EsteriaManastorm_GetChaoticLink() return link end
function EsteriaManastorm_GetLoadout() return unpack(loadout) end
function EsteriaManastorm_GetRewards() return rewards end
function EsteriaManastorm_Enter(d) sent[#sent + 1] = "enter " .. d return true end
function EsteriaManastorm_Leave() sent[#sent + 1] = "leave" return true end
function EsteriaManastorm_SetLoadoutSlot(s, spell) sent[#sent + 1] = "slot " .. s .. " " .. spell return true end

function fire(times, dt)
    for _ = 1, times or 1 do
        now = now + (dt or 1)
        for _, o in ipairs(created) do local f = o._scripts.OnUpdate if f then f(o, dt or 1) end end
    end
end
function event(name, ...)
    for _, o in ipairs(created) do local f = o._scripts.OnEvent if f then f(o, name, ...) end end
end
'''

L = LuaRuntime()
L.execute(HARNESS + "\nHarnessWidget = W\n")
L.execute(GAME)
for path in (GLUE / "esteria_style.lua", ADDON / "EsteriaManastormData.lua", ADDON / "EsteriaManastorm.lua"):
    L.execute(path.read_text(encoding="utf-8"))
L.execute(r'''
local function check(cond, msg) if not cond then error("FAIL: " .. msg, 2) end print("ok  " .. msg) end
local M = EsteriaManastorm
local D = EsteriaManastormData
local function visible(region) return region:IsVisible() end

check(#D.gadgets == 149 and D.segments[1][1] == "Ragefire Chasm", "generated data loaded (149 gadgets, segment names)")

-- Solo, normal track: best 12 opens checkpoints 1, 6, 11 (deepest first in the level drop-down).
progress.player = { { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 } }
SlashCmdList.ESTERIAMANASTORM("")
fire(3)
check(EsteriaManastormFrame:IsShown(), "/manastorm opens the window")
check(M.Mode() == 1 and M.mode._text == "SOLO", "solo mode under level 80")
check(M.best._text == 12 and M.bestRows[1].value._text == 12 and M.bestRows[2].value._text == 0, "best solo 12, best duo 0")
check(M.levels[1] == 11 and M.levels[3] == 1 and #M.levels == 3, "levels 11, 6, 1")
check(M.selected == 11 and M.dropdown.text._text == "Level 11" and M.enter.label._text == "Enter Solo Manastorm",
    "the deepest checkpoint is preselected; Enter names the mode")
check(M.currency[1].text._text:find("Bedlam Bullion: |cffffffff7"), "currencies show what is in the bags")
-- The Enter button's border streaks: at every moment the lit pieces add up to two whole streaks (corners included).
local function lit()
    local total = 0
    for _, pieces in ipairs(M.enter.streaks) do
        for _, t in ipairs(pieces) do if t:IsShown() then total = total + math.max(t._w, t._h) end end
    end
    return total
end
local exact = true
for _ = 1, 40 do
    M.enter._scripts.OnUpdate(M.enter, 0.37)
    if math.abs(lit() - 140) > 0.01 then exact = false end
end
check(exact, "the Enter border streaks stay whole around corners and the lap's start")
M.enter:Disable(); M.enter._scripts.OnUpdate(M.enter, 0.1)
check(lit() == 0, "a disabled Enter button has no streaks")
M.enter:Enable()

M.dropdown:Click()
check(M.levelList:IsShown() and M.levelRows[2].depth == 6, "the level drop-down opens")
M.levelRows[2]:Click()
check(M.selected == 6 and M.dropdown.text._text == "Level 6", "a level can be picked")
M.enter:Click()
check(sent[#sent] == "enter 6" and M.enter:IsEnabled() == 0, "Enter sends the level and waits for the answer")

-- A refusal is explained and the window stays.
event("MANASTORM_ENTER_RESULT", "ENTER_MANASTORM_COOLDOWN")
check(errors[#errors]:find("recovering"), "a refusal shows its reason")
check(EsteriaManastormFrame:IsShown() and M.enter:IsEnabled() == 1, "the window stays and Enter works again")

-- A duo with a level 80 partner: endgame track, checkpoints limited by the partner's best.
party = { { name = "Buddy", level = 80, guid = "0x0000000000000042" } }
progress.player = { {}, {}, {}, {}, {}, { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 } }
progress["0x0000000000000042"] = { {}, {}, {}, {}, {}, { 1, 2, 3, 4, 5, 6, 7 } }
event("PARTY_MEMBERS_CHANGED")
check(M.Mode() == 6 and M.mode._text:find("DUO") and M.mode._text:find("ENDGAME"), "duo on the endgame track")
check(M.bestRows[2].value._text == 17 and M.enter.label._text == "Enter Duo Manastorm", "best duo (endgame) and Enter Duo")
check(M.group._text:find("Buddy") and M.group._text:find("7"), "the partner's best is listed")
check(M.levels[1] == 6 and #M.levels == 2, "levels stop at what every member reached")
leader = false
event("PARTY_MEMBERS_CHANGED")
check(M.enter:IsEnabled() == 0 and M.depthHint._text:find("leader"), "only the leader can start")
leader = true

-- The run: the window closes on success; the tracker and the run card show the scene.
event("MANASTORM_ENTER_RESULT", "ENTER_MANASTORM_OK")
check(not EsteriaManastormFrame:IsShown(), "entering closes the window")
active = { 7, 1, "DUO_END_GAME", 2, 12.5, 97884 }
link = 3
event("MANASTORM_ACTIVE_UPDATED")
check(EsteriaManastormTracker:IsShown() and EsteriaManastormTracker.depth._text == 7, "the tracker shows the depth")
check(EsteriaManastormTracker.boss._text == "Defeat Taragaman the Hungerer", "the tracker names the objective")
check(EsteriaManastormTracker.extra._text:find("Chaotic Link x3") and EsteriaManastormTracker.extra._text:find("2 caches"), "link and caches")
EsteriaManastorm_Toggle()
check(M.run:IsShown() and not M.about:IsShown() and M.enter.label._text == "Leave the Manastorm", "in a run the window shows the run")
check(M.runScene._text == "Ragefire Chasm" and M.runCacheText._text:find("13%% chance"), "scene and cache chance")
M.leave:Click()
check(popups[#popups][1] == "ESTERIA_MANASTORM_LEAVE" and popups[#popups][2] == 6, "leaving warns about checkpoint 6")
StaticPopupDialogs.ESTERIA_MANASTORM_LEAVE.OnAccept()
check(sent[#sent] == "leave", "accepting sends leave")

-- Clearing depth 10 on the normal track toasts the clear, then the unlock (Grim Marsh).
active = { 10, 4, "SOLO", 1, 0, 97884 }
party = {}
event("MANASTORM_LEVEL_COMPLETED", 10)
check(EsteriaManastormToast:IsShown() and EsteriaManastormToast.title._text == "Depth 10 cleared", "a cleared depth is toasted")
check(EsteriaManastormToast.text._text:find("start at depth 11"), "a checkpoint clear says where you can restart")
fire(8, 1)
check(EsteriaManastormToast.title._text:find("Grim Marsh"), "then the unlock that depth grants")
active = nil
event("MANASTORM_ACTIVE_UPDATED")
check(not EsteriaManastormTracker:IsShown(), "the tracker hides after the run")

-- Gadgets: highest known rank per family; slotting sends; refusals are explained; locked in a run.
known = { [991540] = true, [293492] = true, [93431] = true }
config.CONFIG_MANASTORM_LOADOUTS_ENABLED = true
event("LEARNED_SPELL_IN_TAB")
local gadgets = M.Gadgets()
check(#gadgets == 2 and gadgets[1].spell == 293492, "one gadget per family, its highest known rank")
check(M.slots[1].plus:IsShown(), "an empty slot shows +")
M.slots[1]:Click()
check(M.picker:IsShown() and M.picks[1].name._text:find("Gadget293492"), "a slot opens the gadget picker")
M.picks[1]:Click()
check(sent[#sent] == "slot 1 293492" and not M.picker:IsShown(), "picking sends the slot")
loadout = { 293492, 0, 0, 0 }
event("MANASTORM_LOADOUT_UPDATED", 1, 293492)
check(M.slots[1].spell == 293492 and not M.slots[1].plus:IsShown() and M.slotNames._text == "Gadget293492",
    "the slot shows the server's loadout")
M.slots[2]:Click()
check(M.picks[1].name._text:find("Gadget93431") and not M.picks[2]:IsShown(), "a family already slotted is not offered again")
event("MANASTORM_LOADOUT_RESULT", 2, "SET_MANASTORM_LOADOUT_BAD_RANK")
check(errors[#errors]:find("higher rank"), "a refused slot is explained")
config.CONFIG_MANASTORM_LOADOUTS_ENABLED = false
event("MANASTORM_CONFIG_UPDATED")
check(not M.loadoutTitle:IsShown() and not M.slots[1]:IsShown(), "gadgets hide when the realm turns loadouts off")
config.CONFIG_MANASTORM_LOADOUTS_ENABLED = true

-- Rewards: hidden ones are left out; a locked one is greyed with its time left.
rewards = { { item = 97884, action = 0, earned = 1, max = 3, secondsLeft = 0 },
            { item = 1297307, action = 1, earned = 0, max = 0, secondsLeft = 0 },
            { item = 1278049, action = 2, earned = 3, max = 3, secondsLeft = 7200 } }
event("MANASTORM_REWARDS_UPDATED")
check(M.rewardIcons[1].count._text == "1/3" and M.rewardIcons[2].item == 1278049, "hidden rewards are skipped")
check(M.rewardIcons[2]._desat or M.rewardIcons[2].icon._desat, "a locked reward is greyed")
check(M.rewardIcons[2].lock._text == "2h 0m", "with the time until it unlocks")
check(not M.rewardMore:IsShown() and not M.rewardIcons[3]:IsShown(), "two rewards need no overflow tile")

-- Many rewards: six tiles and a "+N" tile that drops the whole grid down.
rewards = {}
for i = 1, 60 do rewards[i] = { item = 1000 + i, action = 0, earned = 0, max = 5, secondsLeft = 0 } end
event("MANASTORM_REWARDS_UPDATED")
check(M.rewardIcons[6]:IsShown() and not M.rewardIcons[7]:IsShown() and M.rewardMore.label._text == "+54", "six tiles and +54")
M.rewardMore:Click()
check(M.grid:IsShown() and M.gridIcons[60]:IsShown() and M.gridIcons[60].item == 1060, "+N opens all 60")
local p = M.gridIcons[60]._points[1]
check(p[2] + 40 <= M.grid._w and -p[3] + 40 <= M.grid._h, "the grid holds its last tile (" .. M.grid._w .. "x" .. M.grid._h .. ")")
M.slots[1]:Click()
fire(2)
check(not M.grid:IsShown() and M.picker:IsShown(), "opening another drop-down closes the grid")

-- Failure toasts; a client without the extension says so instead of erroring.
event("MANASTORM_FAILED")
fire(20, 1)
EsteriaManastorm_GetActive = nil
EsteriaManastormFrame:Hide()
EsteriaManastorm_Toggle()
check(chat[#chat]:find("wowdecomp client"), "a stock client gets a message, not an error")
print("all Manastorm UI checks passed")
''')
