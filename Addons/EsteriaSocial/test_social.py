"""Lua behavior check for EsteriaSocial with UI and service mocks; does not prove in-game rendering."""
from pathlib import Path
from lupa.lua51 import LuaRuntime

HERE = Path(__file__).resolve().parent

MOCKS = r'''
unpack = unpack or table.unpack; tinsert = table.insert; format = string.format
time = os.time
function strtrim(s) return (s:gsub("^%s+", ""):gsub("%s+$", "")) end
UISpecialFrames, SlashCmdList, calls, sent = {}, {}, {}, {}
LOCALIZED_CLASS_NAMES_MALE = { MAGE = "Mage", WARRIOR = "Warrior" }
RAID_CLASS_COLORS = { MAGE = { r = 0.4, g = 0.8, b = 1 }, WARRIOR = { r = 0.78, g = 0.61, b = 0.43 } }
CLASS_ICON_TCOORDS = { MAGE = { 0, 1, 0, 1 }, WARRIOR = { 0, 1, 0, 1 } }
local methods = {}
function methods:SetText(t) self.text = t end
function methods:GetText() return rawget(self, "text") or "" end
function methods:GetStringHeight() return 14 end
function methods:GetStringWidth() return 50 end
function methods:Show() local was = rawget(self, "shown"); self.shown = true
  local s = rawget(self, "scripts"); if not was and s and s.OnShow then s.OnShow(self) end
  for _, f in ipairs(rawget(self, "onshow") or {}) do f(self) end end
function methods:Hide() local was = rawget(self, "shown"); self.shown = false
  local s = rawget(self, "scripts"); if was and s and s.OnHide then s.OnHide(self) end end
function methods:IsShown() return rawget(self, "shown") end
function methods:IsEnabled() return rawget(self, "disabled") and 0 or 1 end
function methods:Enable() self.disabled = false end
function methods:Disable() self.disabled = true end
function methods:HasFocus() return false end
function methods:SetScript(k, f) self.scripts = rawget(self, "scripts") or {}; self.scripts[k] = f end
function methods:HookScript(k, f)
  if k == "OnShow" then self.onshow = rawget(self, "onshow") or {}; table.insert(self.onshow, f); return end
  local s = rawget(self, "scripts") or {}; self.scripts = s
  local old = s[k]; s[k] = function(...) if old then old(...) end f(...) end
end
function methods:RegisterEvent(e) self.events = rawget(self, "events") or {}; self.events[e] = true end
function methods:CreateTexture() return New("Texture") end
function methods:CreateFontString() return New("FontString") end
function methods:AddMessage(t) self.msgs = rawget(self, "msgs") or {}; table.insert(self.msgs, t) end
function methods:Clear() self.msgs = {} end
function methods:SetMinMaxValues(a, b) self.min, self.max = a, b end
function methods:SetValue(v) local s = rawget(self, "scripts"); if s and s.OnValueChanged then s.OnValueChanged(self, v) end end
frames = {}
function New(kind, name)
  local f = setmetatable({ kind = kind, shown = kind ~= "Frame" or name == nil },
    { __index = function(_, k) return methods[k] or function() end end })
  if name then _G[name] = f end
  frames[#frames + 1] = f
  return f
end
function CreateFrame(kind, name) return New(kind, name) end
function PlaySound() end
function GetTime() return 0 end
function HideUIPanel(f) f.shown = false end
function SecondsToTime(s) return math.floor(s / 86400) .. " days" end
function UnitName() return "Zach" end
function UnitLevel() return 80 end
function UnitClass() return "Warrior", "WARRIOR" end
function GetRealZoneText() return "Dalaran" end
UIErrorsFrame = New("Frame"); GameTooltip = New("GameTooltip"); ChatFontNormal = {}

-- Character friends and ignore list.
function GetNumFriends() return 2, 1 end
function GetFriendInfo(i)
  if i == 1 then return "Jaina", 80, "Mage", "Dalaran", true, "", "raid buddy" end
  return "Thrall", 78, "Warrior", "", false, "", "" end
function GetNumIgnores() return 1 end
function GetIgnoreName() return "Spammer" end
function ShowFriends() calls.showFriends = true end
for _, n in ipairs({ "AddFriend", "RemoveFriend", "AddIgnore", "DelIgnore", "SetFriendNotes", "InviteUnit", "ChatFrame_SendTell",
  "GuildInvite", "GuildPromote", "GuildDemote", "GuildUninvite", "GuildLeave", "GuildSetMOTD", "SetGuildInfoText",
  "GuildRosterSetPublicNote", "GuildRosterSetOfficerNote", "SendWho", "SetWhoToUI", "GuildRoster", "SetGuildRosterShowOffline" }) do
  _G[n] = function(...) calls[n] = { ... } end
end

-- Who.
function GetNumWhoResults() return 2, 5 end
function GetWhoInfo(i) if i == 1 then return "Arthas", "Scourge", 80, "Human", "Paladin", "Icecrown", "PALADIN" end
  return "Uther", "", 75, "Human", "Paladin", "Stratholme", "PALADIN" end

-- Guild with officer rights.
function IsInGuild() return true end
function GetGuildInfo() return "Esteria Vanguard", "Officer", 1 end
function GetNumGuildMembers() return 3 end
local roster = {
  { "Zach", "Officer", 1, 81, "Warrior", "Dalaran", "me", "", true, 0, "WARRIOR" },
  { "Jaina", "Member", 2, 80, "Mage", "Dalaran", "frost", "trial", true, 1, "MAGE" },
  { "Old", "Member", 2, 70, "Warrior", "", "", "", false, 0, "WARRIOR" },
}
function GetGuildRosterInfo(i) return unpack(roster[i]) end
function GetGuildRosterLastOnline(i) if i == 3 then return 0, 0, 5, 2 end return 0, 0, 0, 0 end
function GetGuildRosterMOTD() return "Raid at 8" end
function GetGuildInfoText() return "Be kind." end
for _, n in ipairs({ "CanGuildInvite", "CanGuildPromote", "CanGuildDemote", "CanGuildRemove", "CanEditPublicNote",
  "CanEditOfficerNote", "CanViewOfficerNote", "CanEditMOTD", "CanEditGuildInfo" }) do _G[n] = function() return 1 end end

-- Esteria service.
listeners = {}
EsteriaChat = {
  listen = function(fn) table.insert(listeners, fn) end,
  sendAccountWhisper = function(tag, body) table.insert(sent, { "dm", tag, body }) end,
  request = function(p, cb)
    table.insert(sent, p)
    if p.op == "feed" then cb({ me = { tag = "Zach#1", displayName = "Zach", presence = "online" },
      friends = { { tag = "Ana#2", displayName = "Ana", presence = "online", statusMessage = "questing",
                    gamePresence = { characterName = "Anaya", level = 80, class = "Mage", zone = "Ulduar" } },
                  { tag = "Bo#3", displayName = "Bo", presence = "offline", lastSeen = "2026-11-18T10:00:00" } },
      incoming = { { tag = "Cy#4", displayName = "Cy" } } })
    elseif p.op == "cList" then cb({ communities = { { id = 7, name = "Tavern", memberCount = 12, role = "owner",
      channels = { { id = 1, name = "General", topic = "Hi all", unread = 2 }, { id = 2, name = "Trade" } } } },
      invites = { { communityId = 9, name = "Raiders", from = "Ana#2" } } })
    elseif p.op == "cGet" then cb({ members = { { tag = "Zach#1", displayName = "Zach", role = "owner" },
      { tag = "Ana#2", displayName = "Ana", role = "member" } } })
    elseif p.op == "cHistory" then cb({ messages = { { id = 2, from = "Ana#2", displayName = "Ana", body = "second" },
      { id = 1, from = "Zach#1", displayName = "Zach", body = "first" } } })
    elseif p.op == "cInviteCreate" then cb({ code = "ABC123" })
    else cb({}) end
  end,
}
FriendsFrame = New("Frame", "FriendsFrame"); FriendsFrame.shown = false
function PanelTemplates_GetSelectedTab(f) return f.tab end
retailRaid = 0
function ToggleFriendsFrame(tab) retailRaid = retailRaid + 1 end
'''

CHECKS = r'''
local T = EsteriaSocial_Test
ToggleFriendsFrame(1)
local frame = EsteriaSocialFrame
assert(frame:IsShown(), "O opens the social window")
assert(calls.showFriends and T.social.connected, "friends requested, account feed loaded")
local online, total, requests, ignored = T.friendCounts()
assert(online == 2 and total == 4 and requests == 1 and ignored == 1, ("%d %d %d %d"):format(online, total, requests, ignored))
T.view.friends = "online"
local rows = T.friendRows()
assert(#rows == 2, "two online (Jaina, Ana)")
T.view.friends = "all"; rows = T.friendRows()
assert(#rows == 4 and rows[#rows].presence == "offline", "offline friends sort last")
local ana = T.accountFriend({ tag = "Ana#2", displayName = "Ana", presence = "online",
  gamePresence = { characterName = "Anaya", level = 80, class = "Mage" } })
assert(ana.charName == "Anaya" and ana.level == 80)

-- Paint each friend kind's card.
for _, key in ipairs({ "c:Jaina", "a:Ana#2", "a:Bo#3" }) do T.selected.friend = key; frame.scripts.OnEvent() end
T.view.friends = "requests"; T.selected.friend = "r:Cy#4"; frame.scripts.OnEvent()
T.view.friends = "ignored"; T.selected.friend = "i:Spammer"; frame.scripts.OnEvent()

-- Who: quick search sends, results list and card.
EsteriaSocial_Open("Who")
T.selected.who = "Arthas"; frame.scripts.OnEvent()

-- Guild: roster, offline filter, notes, officer actions.
EsteriaSocial_Open("Guild")
assert(calls.SetGuildRosterShowOffline and calls.GuildRoster, "guild roster requested with offline members")
T.selected.member = "Jaina"; frame.scripts.OnEvent()
T.view.guild = "info"; frame.scripts.OnEvent()
T.view.guild = "roster"

-- Communities: list, history ordering, channel switch, invite.
EsteriaSocial_Open("Communities")
assert(T.selected.community == 7, "first community selected")
frame.scripts.OnEvent()
local log
for _, f in ipairs(frames) do if rawget(f, "msgs") and #f.msgs >= 2 then log = f end end
assert(log and log.msgs[1]:find("first") and log.msgs[2]:find("second"), "messages in id order")
local sawRead
for _, p in ipairs(sent) do if p.op == "cRead" and p.cursor == 2 then sawRead = true end end
assert(sawRead, "channel marked read up to the newest message")
T.selected.community, T.selected.invite = nil, T.social.invites[1]; frame.scripts.OnEvent()

-- Entry points.
ToggleFriendsFrame(5)
assert(retailRaid == 1, "raid tab left to the existing window")
ToggleGuildFrame()
FriendsFrame.tab = 2; FriendsFrame:Show()
assert(not FriendsFrame:IsShown() and frame:IsShown(), "stock window redirected here")
ToggleGuildFrame(); ToggleGuildFrame()
'''


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.execute(MOCKS)
    for name in ("esteria_style.lua", "EsteriaSocial.lua"):
        lua.execute((HERE / name).read_text(encoding="utf-8"))
    lua.execute(CHECKS)
    print("EsteriaSocial: all checks passed")


if __name__ == "__main__":
    main()
