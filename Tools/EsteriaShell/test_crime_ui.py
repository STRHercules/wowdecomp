"""Smoke test for the Esteria PvP window and crime banners against a stubbed 3.3.5 UI API."""
from pathlib import Path

from lupa.lua51 import LuaRuntime

ADDON = Path(__file__).resolve().parents[2] / "Addons" / "EsteriaWanted"
FILES = ["esteria_style.lua", "CrimeBanner.lua", "WildernessBanner.lua", "BountyDebuff.lua", "CrimeWindow.lua"]

STUBS = r"""
sent, popups, pvp, frames, now = {}, {}, {}, {}, 0
local W = {}
W.__index = function(t, k) return W[k] or (k:match("^%u") and function() end or nil) end
local function widget(kind)
    local w = setmetatable({ kind = kind, width = 0, height = 0, shown = true, scripts = {}, events = {}, text = "" }, W)
    frames[#frames + 1] = w
    return w
end
function W:SetSize(w, h) self.width, self.height = w, h end
function W:SetWidth(w) self.width = w end
function W:SetHeight(h) self.height = h end
function W:GetWidth() return self.width end
function W:GetHeight() return self.height end
function W:Show() local was = self.shown; self.shown = true; if not was and self.scripts.OnShow then self.scripts.OnShow(self) end end
function W:Hide() local was = self.shown; self.shown = false; if was and self.scripts.OnHide then self.scripts.OnHide(self) end end
function W:IsShown() return self.shown end
function W:IsVisible() return self.shown end
function W:SetScript(e, f) self.scripts[e] = f end
function W:GetScript(e) return self.scripts[e] end
function W:HookScript(e, f) local old = self.scripts[e]; self.scripts[e] = function(...) if old then old(...) end f(...) end end
function W:RegisterEvent(e) self.events[e] = true end
function W:CreateTexture() return widget("Texture") end
function W:CreateFontString() return widget("FontString") end
function W:SetText(t) self.text = t == nil and "" or tostring(t) end
function W:SetFormattedText(f, ...) self.text = f:format(...) end
function W:GetText() return self.text end
function W:GetStringWidth() return #self.text * 8 end
function W:GetStringHeight() return 28 end
function W:IsEnabled() return self.disabled and 0 or 1 end
function W:Enable() self.disabled = false end
function W:Disable() self.disabled = true end
function W:Click() if self.scripts.OnClick then self.scripts.OnClick(self, "LeftButton") end end
function W:GetFrameLevel() return 1 end
function W:GetEffectiveScale() return 1 end
function W:IsOwned() return false end
function W:NumLines() return 1 end
function W:GetName() return "GameTooltip" end

function CreateFrame(kind, name) local w = widget(kind); if name then _G[name] = w end; return w end
function fire(event, ...)
    for _, f in ipairs(frames) do
        if f.events[event] and f.scripts.OnEvent then f.scripts.OnEvent(f, event, ...) end
    end
end
function tick(seconds)
    local step = 0.05
    for _ = 1, math.floor(seconds / step) do
        now = now + step
        for _, f in ipairs(frames) do
            if f.shown and f.scripts.OnUpdate then f.scripts.OnUpdate(f, step) end
        end
    end
end
UIParent, GameTooltip, WorldMapButton, UIErrorsFrame = widget(), widget(), widget(), widget()
PVPParentFrame = widget(); PVPParentFrame.shown = false
UISpecialFrames, StaticPopupDialogs, ChatFontNormal = {}, {}, {}
UnitPopupMenus = { SELF = { "SET_FOCUS", "PVP_FLAG", "LEAVE", "CANCEL" }, PVP_FLAG = { "PVP_ENABLE", "PVP_DISABLE" } }
RAID_CLASS_COLORS = { ROGUE = { r = 1, g = 0.96, b = 0.41 } }
ACCEPT, CANCEL, ERR_NOT_ENOUGH_MONEY = "Accept", "Cancel", "Not enough money"
MAX_ARENA_TEAMS, MAX_BATTLEFIELD_QUEUES = 3, 3
debuffs = {}
function GetTime() return now end
function PlaySound() end
function hooksecurefunc() end
function HideUIPanel(f) f:Hide() end
function SendAddonMessage(prefix, msg) sent[#sent + 1] = prefix .. ":" .. msg end
function UnitName() return "Tester" end
function UnitFactionGroup() return "Horde" end
function UnitIsPVP() return pvp.on end
function UnitIsPVPFreeForAll() return false end
function UnitIsPlayer() return false end
function UnitDebuff(_, i)
    local id = debuffs[i]
    if id then return "x", nil, nil, nil, nil, nil, nil, nil, nil, nil, id end
end
function UnitAura(...) return UnitDebuff(...) end
function GetPVPDesired() return pvp.on and 1 or 0 end
function SetPVP(v) pvp.on = v ~= nil end
function IsPVPTimerRunning() return false end
function GetPVPTimer() return 0 end
function GetHonorCurrency() return 1234 end
function GetArenaCurrency() return 56 end
function GetPVPSessionStats() return 2, 30 end
function GetPVPYesterdayStats() return 5, 70 end
function GetPVPLifetimeStats() return 400, 0, 0 end
function GetWintergraspWaitTime() return 4800 end
function GetNumBattlegroundTypes() return 2 end
function GetBattlegroundInfo(i)
    if i == 1 then return "Warsong Gulch", true, false, false, 2 end
    return "Random Battleground", true, true, true, 32
end
function RequestBattlegroundInstanceInfo(i) requested = i end
function GetBattlefieldInfo() return "Warsong Gulch", "Capture the flag.", 10 end
function GetBattlefieldStatus() return "none" end
function GetHolidayBGHonorCurrencyBonuses() return true, 3767, 25, 628, 0 end
function JoinBattlefield(id, group) joined = { id, group } end
function CloseBattlefield() end
function GetNumPartyMembers() return 0 end
function GetNumRaidMembers() return 0 end
function IsPartyLeader() return false end
function GetArenaTeam(i) if i == 1 then return "Bladestorm", 2, 1850, 10, 7, 40, 25, 10, 40, 12, 1810 end end
function GetCurrentArenaSeason() return 8 end
function GetMoney() return 5000000 end
function GetItemIcon() return "icon" end
function GetZoneText() return "The Storm Peaks" end
function GetCoinTextureString(c) return tostring(c) .. "c" end
function SecondsToTime(s) return tostring(s) .. "s" end
function StaticPopup_Show(which, a, b, data) popups[#popups + 1] = { which, a, b, data } end
function strtrim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
function strsplit(sep, s)
    local out, start = {}, 1
    while true do
        local i = s:find(sep, start, true)
        if not i then out[#out + 1] = s:sub(start); break end
        out[#out + 1] = s:sub(start, i - 1); start = i + 1
    end
    return unpack(out)
end
tinsert = table.insert
date = os.date
SlashCmdList = {}
"""

SCENARIO = r"""
-- Portrait menu and stock frame replacement.
for _, v in ipairs(UnitPopupMenus.SELF) do assert(v ~= "PVP_FLAG", "portrait PvP menu still present") end
TogglePVPFrame()
assert(EsteriaCrimeFrame:IsShown(), "window did not open")
local function asked(body) for _, s in ipairs(sent) do if s == "EsteriaCrime:" .. body then return true end end end
assert(asked("status"), "status not requested")

local function server(body) fire("CHAT_MSG_ADDON", "EsteriaCrime", body, "WHISPER", "Tester") end
server("S\t1\t1\t0\t3\t2500000\t7200\t9\t120000")
EsteriaCrimeFrame.Refresh()

-- The stock frame opening (key binding paths, NPC queue) lands in our window instead.
EsteriaCrimeFrame:Hide(); PVPParentFrame.justBG = true; PVPParentFrame:Show()
assert(not PVPParentFrame:IsShown() and EsteriaCrimeFrame:IsShown(), "stock frame not redirected")
assert(requested, "battleground info not requested")

for _, tab in ipairs({ "Overview", "Laws", "Wilderness", "Battlegrounds", "Arena", "Bounties", "Crime Log", "Leaderboard" }) do
    EsteriaCrime_Toggle(tab)
end
server("B\tVillain\t4\t80\t250000\t3000000"); server("E\tB")
server("L\t1\tVictim\t0\t0\tZul'Drak\t1790000000"); server("L\t2\tMark\t4500\t2\t\t1790000100")
server("L\t3\tGuard\t0\t0\t\t1790000200"); server("E\tL")
server("T\t1\tVillain\t12"); server("T\t2\tTester\t90000"); server("T\t3\tVillain\t3250000"); server("E\tT")
for _, tab in ipairs({ "Bounties", "Crime Log", "Leaderboard", "Overview" }) do EsteriaCrime_Toggle(tab) end
tick(2)

-- Wilderness zones and the Bloody Auction House.
EsteriaCrime_Toggle("Wilderness")
assert(asked("wild") and asked("auction"), "wilderness data not requested")
server("W	5400	4	The Storm Peaks")
server("A	77	1	45000	0	0		4500"); server("A	78	0	0	19019	1	Thunderfury	900000"); server("E	A")
local cardOn
for _, f in ipairs(frames) do if f.kind == "FontString" and f.text:find("ACTIVE NOW") then cardOn = true end end
assert(cardOn, "active zone card not lit")
local buy
for _, f in ipairs(frames) do if f.kind == "Button" and rawget(f, "label") and f.label.text == "Buy" and rawget(f, "row") and f.row.item then buy = f end end
assert(buy, "buy button missing"); buy:Click()
assert(popups[#popups][1] == "ESTERIA_BLOODY_BUY", "no buy confirmation")
StaticPopupDialogs.ESTERIA_BLOODY_BUY.OnAccept(nil, popups[#popups][4])
assert(asked("buy	77") or asked("buy	78"), "buy not sent")
popups = {}

-- Earned bounties list and the collect button.
EsteriaCrime_Toggle("Bounties")
assert(asked("claims"), "claims not requested")
server("C	1	Villain	2500000"); server("C	0	Mark	250000"); server("E	C")
local collect
for _, f in ipairs(frames) do if f.kind == "Button" and rawget(f, "label") and f.label.text == "Collect" then collect = f end end
assert(collect and not collect.disabled, "collect button not enabled")
collect:Click(); assert(asked("collect"), "collect not sent")

-- Posting a bounty goes through a confirmation, then the addon channel.
EsteriaCrime_Toggle("Bounties")
local post
for _, f in ipairs(frames) do if f.kind == "Button" and rawget(f, "label") and f.label.text == "Post Bounty" then post = f end end
assert(post, "post button missing")
for _, f in ipairs(frames) do
    if f.kind == "EditBox" then if rawget(f, "numeric") then f.text = "150" else f.text = "Villain" end end
end
post:Click()
assert(#popups == 1 and popups[1][4].name == "Villain" and popups[1][4].gold == 150, "no confirmation")
StaticPopupDialogs.ESTERIA_CRIME_POST.OnAccept(nil, popups[1][4])
assert(asked("post\tVillain\t150"), "post not sent")

-- PvP toggle.
pvp.on = false
local toggle
for _, f in ipairs(frames) do if f.kind == "Button" and rawget(f, "label") and f.label.text == "Turn PvP On" then toggle = f end end
EsteriaCrime_Toggle("Overview"); EsteriaCrimeFrame.Refresh()
for _, f in ipairs(frames) do if f.kind == "Button" and rawget(f, "label") and f.label.text == "Turn PvP On" then toggle = f end end
assert(toggle, "toggle missing"); toggle:Click(); assert(pvp.on, "PvP not enabled")

-- Crime banners: quiet right after login, then play for new states and bounty increases.
fire("PLAYER_ENTERING_WORLD"); tick(3.5)
debuffs = { 955080 }; fire("UNIT_AURA", "player")
debuffs = { 955080, 955083 }; fire("UNIT_AURA", "player")
fire("CHAT_MSG_ADDON", "EsteriaBounty", "125g 0s 0c", "WHISPER", "Tester")
local function bannerShown(title)
    for _, f in ipairs(frames) do
        if f.kind == "FontString" and f.text == title then
            return true
        end
    end
end
assert(bannerShown("W A N T E D"), "wanted banner did not play")
tick(5); assert(bannerShown("T H I E F"), "queued thief banner did not play")
tick(5); assert(bannerShown("B O U N T Y   P L A C E D"), "queued bounty banner did not play")

-- Wilderness hold banner.
fire("CHAT_MSG_ADDON", "EsteriaWild", "1", "WHISPER", "Tester"); tick(1)
assert(bannerShown("W I L D E R N E S S"), "wilderness banner missing")
fire("CHAT_MSG_ADDON", "EsteriaWild", "0", "WHISPER", "Tester"); tick(2)
"""


def test_crime_ui():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(STUBS)
    lua.execute("function W_numeric() end")
    # EditBox:SetNumeric marks numeric boxes so the scenario can fill them.
    lua.execute("local mt = getmetatable(CreateFrame('Frame')); "
                "local idx = mt.__index; mt.__index = function(t, k) if k == 'SetNumeric' then "
                "return function(self, v) self.numeric = v end end return idx(t, k) end")
    for name in FILES:
        lua.execute((ADDON / name).read_text(encoding="utf-8"))
    lua.execute(SCENARIO)


if __name__ == "__main__":
    test_crime_ui()
    print("Crime UI smoke test passed.")
