-- Esteria housing: the Neighborhood window, opened from a button beside Character, Talents and Social. Every account has
-- a neighborhood of its own (Founder's Point or Razorwind Shores) and a guild may buy more; the window shows the one you
-- are in (or yours), your homes, the neighborhoods you can go to, favorite and recently visited houses, and lets the
-- owner invite others in, move it to the other map, or a Guild Master buy one for the guild.
-- DragonUI's strip owns NeighborhoodMicroButton (Addons/EsteriaShell/esteria_microbuttons.lua); without DragonUI this
-- file adds one to the stock micro bar.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local W, HEIGHT, ROWS, ROW_H, LIST_TOP = 430, 520, 8, 34, -210
local MAPS = { [0] = "Founder's Point", [1] = "Razorwind Shores" }

local f = H.Panel("EsteriaHousingNeighborhoodFrame", W, HEIGHT)
f:SetPoint("CENTER", 0, 40)
tinsert(UISpecialFrames, "EsteriaHousingNeighborhoodFrame")

local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -9); title:SetText("Neighborhood")
local place = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
place:SetPoint("TOP", title, "BOTTOM", 0, -2)
local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", 2, 2)

-- the neighborhood shown
local hoodIcon = H.Atlas(f:CreateTexture(nil, "ARTWORK"), "housefinder_main-icon")
hoodIcon:SetSize(44, 44); hoodIcon:SetPoint("TOPLEFT", 18, -52)
local hoodTitle = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
hoodTitle:SetPoint("TOPLEFT", hoodIcon, "TOPRIGHT", 10, -2); hoodTitle:SetWidth(W - 90); hoodTitle:SetJustifyH("LEFT")
local hoodBody = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hoodBody:SetPoint("TOPLEFT", hoodTitle, "BOTTOMLEFT", 0, -4); hoodBody:SetWidth(W - 90); hoodBody:SetJustifyH("LEFT")

-- the way there
local travel = CreateFrame("Button", "EsteriaHousingNeighborhoodTravel", f, "UIPanelButtonTemplate")
travel:SetSize(240, 26); travel:SetPoint("TOP", 0, -114)
local reason = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
reason:SetPoint("TOP", travel, "BOTTOM", 0, -3); reason:SetWidth(W - 40); reason:SetTextColor(1, 0.3, 0.3)

-- tabs
local TABS = { { "plots", "Plots" }, { "homes", "My Homes" }, { "hoods", "Neighborhoods" }, { "favorites", "Favorites" },
  { "recent", "Recent" }, { "manage", "Manage" } }
local tab = "plots"
local tabButtons = {}
local divider = H.Atlas(f:CreateTexture(nil, "ARTWORK"), "housing-floor-list-divider-active")
divider:SetPoint("TOPLEFT", 16, -176); divider:SetPoint("TOPRIGHT", -16, -176); divider:SetHeight(2)
local summary = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
summary:SetPoint("TOPLEFT", 20, -186); summary:SetWidth(W - 40); summary:SetJustifyH("LEFT")

local info      -- the server's last answer
local listed = {}
local page = 0
local rows = {}
local Redraw

for i, t in ipairs(TABS) do
  local b = CreateFrame("Button", "EsteriaHousingNeighborhoodTab" .. i, f, "UIPanelButtonTemplate")
  b:SetSize((W - 32) / #TABS, 20); b:SetPoint("TOPLEFT", 16 + (i - 1) * (W - 32) / #TABS, -152)
  b:SetText(t[2]); b:GetFontString():SetFontObject(GameFontNormalSmall)
  b:SetScript("OnClick", function() tab = t[1]; page = 0; Redraw() end)
  tabButtons[t[1]] = b
end

for i = 1, ROWS do
  local row = CreateFrame("Frame", nil, f)
  row:SetSize(W - 40, ROW_H - 2); row:SetPoint("TOP", 0, LIST_TOP - (i - 1) * ROW_H)
  row.icon = row:CreateTexture(nil, "ARTWORK"); row.icon:SetSize(24, 24); row.icon:SetPoint("LEFT", 2, 0)
  row.label = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  row.label:SetPoint("LEFT", row.icon, "RIGHT", 8, 0); row.label:SetWidth(W - 210); row.label:SetJustifyH("LEFT")
  row.go = CreateFrame("Button", "EsteriaHousingNeighborhoodGo" .. i, row, "UIPanelButtonTemplate")
  row.go:SetSize(52, 22); row.go:SetPoint("RIGHT", 0, 0)
  row.go:SetScript("OnClick", function() if row.onGo then row.onGo(row.go) end end)
  row.extra = CreateFrame("Button", "EsteriaHousingNeighborhoodExtra" .. i, row, "UIPanelButtonTemplate")
  row.extra:SetSize(72, 22); row.extra:SetPoint("RIGHT", row.go, "LEFT", -4, 0)
  row.extra:SetScript("OnClick", function() if row.onExtra then row.onExtra(row.extra) end end)
  rows[i] = row
end

local prev = CreateFrame("Button", "EsteriaHousingNeighborhoodPrev", f, "UIPanelButtonTemplate")
prev:SetSize(70, 22); prev:SetPoint("BOTTOMLEFT", 20, 14); prev:SetText("Previous")
local nextPage = CreateFrame("Button", "EsteriaHousingNeighborhoodNext", f, "UIPanelButtonTemplate")
nextPage:SetSize(70, 22); nextPage:SetPoint("BOTTOMRIGHT", -20, 14); nextPage:SetText("Next")
local pageText = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
pageText:SetPoint("BOTTOM", 0, 20)

-- Manage: invite by name, move to the other map, and the guild's neighborhoods
local manage = CreateFrame("Frame", nil, f)
manage:SetSize(W - 32, 120); manage:SetPoint("TOP", 0, -204)
local inviteBox = CreateFrame("EditBox", "EsteriaHousingInviteName", manage, "InputBoxTemplate")
inviteBox:SetSize(150, 20); inviteBox:SetPoint("TOPLEFT", 10, -4); inviteBox:SetAutoFocus(false); inviteBox:SetMaxLetters(12)
local inviteButton = CreateFrame("Button", "EsteriaHousingInvite", manage, "UIPanelButtonTemplate")
inviteButton:SetSize(80, 22); inviteButton:SetPoint("LEFT", inviteBox, "RIGHT", 8, 0); inviteButton:SetText("Invite")
local inviteHint = manage:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
inviteHint:SetPoint("LEFT", inviteButton, "RIGHT", 8, 0); inviteHint:SetWidth(140); inviteHint:SetJustifyH("LEFT")
inviteHint:SetText("Invited players may visit and buy plots here.")
local moveButton = CreateFrame("Button", "EsteriaHousingMoveHood", manage, "UIPanelButtonTemplate")
moveButton:SetSize(W - 52, 22); moveButton:SetPoint("TOPLEFT", 10, -32)
local guildButton = CreateFrame("Button", "EsteriaHousingBuyGuildHood", manage, "UIPanelButtonTemplate")
guildButton:SetSize((W - 58) / 2, 22); guildButton:SetPoint("TOPLEFT", 10, -60); guildButton:SetText("Buy Guild Neighborhood")
local ranksButton = CreateFrame("Button", "EsteriaHousingGuildRanks", manage, "UIPanelButtonTemplate")
ranksButton:SetSize((W - 58) / 2, 22); ranksButton:SetPoint("LEFT", guildButton, "RIGHT", 6, 0); ranksButton:SetText("Rank Permissions")
local guildText = manage:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
guildText:SetPoint("TOPLEFT", 12, -88); guildText:SetWidth(W - 56); guildText:SetJustifyH("LEFT")
local membersTitle = manage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
membersTitle:SetPoint("TOPLEFT", 10, -110); membersTitle:SetText("Invited")

-- Why the server would refuse travel right now (TravelBlock in mod_esteria_housing.cpp), so the buttons can say so first.
local function Blocked()
  if UnitAffectingCombat("player") then return "You cannot travel while in combat." end
  if UnitIsDeadOrGhost("player") then return "You cannot travel while dead." end
  if UnitOnTaxi("player") then return "You cannot travel while on a flight path." end
  local inInstance, kind = IsInInstance()
  if kind == "pvp" or kind == "arena" then return "You cannot travel from a battleground or arena." end
  if inInstance and info and info.where == "away" then return "You cannot travel from inside a dungeon or raid." end
end

local function SetEnabled(button, on)
  if on then button:Enable() else button:Disable() end
end

local menu = CreateFrame("Frame", "EsteriaHousingAccessMenu", UIParent, "UIDropDownMenuTemplate")

-- ------------------------------------------------------------------------------------------------ who may visit

local ACCESS = { { "party", "Party and raid members" }, { "friends", "Friends" }, { "guild", "Guild members" },
  { "everyone", "Everyone" } }
local accessWanted -- { anchor, house } to open once the server says what the setting is

local function VisitorText(access)
  local who = {}
  for _, a in ipairs(ACCESS) do if access and access[a[1]] then who[#who + 1] = a[2]:lower() end end
  if access and access.everyone then return "everyone" end
  return #who > 0 and table.concat(who, ", ") or "only you"
end

local function HomeOf(house)
  for _, h in ipairs(info and info.homes or {}) do if h.house == house then return h end end
end

-- Owner's checkboxes for who may visit a house (default: the one you are in, else your first); each change is saved
-- at once and sends out visitors who no longer may stay.
function H.ShowAccessMenu(anchor, house)
  if not house then
    local state = EsteriaHousing_GetState()
    house = state and state.inHouse and state.isOwner and state.houseId or nil
  end
  local home = info and (house and HomeOf(house) or info.homes[1])
  if not home then
    accessWanted = { anchor or "cursor", house }
    return EsteriaHousing_RequestNeighborhood()
  end
  accessWanted = nil
  local now = {}
  for k, v in pairs(home.access) do now[k] = v end
  local items = { { text = "Who can visit", isTitle = true, notCheckable = true } }
  for _, a in ipairs(ACCESS) do
    local key = a[1]
    items[#items + 1] = { text = a[2], checked = now[key], keepShownOnClick = true, func = function()
      now[key] = not now[key]
      EsteriaHousing_SetHouseAccess(home.house, now.party, now.friends, now.guild, now.everyone)
    end }
  end
  EasyMenu(items, menu, anchor or "cursor", 0, 0, "MENU")
end

-- ------------------------------------------------------------------------------------------------ dialogs

StaticPopupDialogs["ESTERIA_HOUSING_MOVE_HOOD"] = {
  text = "%s", button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, data) EsteriaHousing_MoveNeighborhood(data.hood, data.map) end,
}
StaticPopupDialogs["ESTERIA_HOUSING_BUY_GUILD_HOOD"] = {
  text = "%s", button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, map) EsteriaHousing_BuyGuildNeighborhood(map) end,
}

local function Ask(which, text, data)
  local dialog = StaticPopup_Show(which, text)
  if dialog then dialog.data = data end
end

inviteButton:SetScript("OnClick", function()
  local name = strtrim(inviteBox:GetText() or "")
  if info and name ~= "" then EsteriaHousing_Invite(info.id, name); inviteBox:SetText(""); inviteBox:ClearFocus() end
end)
inviteBox:SetScript("OnEnterPressed", function() inviteButton:Click() end)
inviteBox:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)

moveButton:SetScript("OnClick", function()
  if not info then return end
  local to = 1 - info.map
  Ask("ESTERIA_HOUSING_MOVE_HOOD", string.format("Move %s to %s?\n\nEvery house keeps its plot number and stands on that " ..
    "plot's spot there; the decor in each yard goes back to storage, and everyone inside leaves.", info.name, MAPS[to]),
    { hood = info.id, map = to })
end)

guildButton:SetScript("OnClick", function(self)
  local g = info and info.guild
  if not g then return end
  local items = { { text = "Guild Neighborhood", isTitle = true, notCheckable = true } }
  for map = 0, 1 do
    items[#items + 1] = { text = MAPS[map], notCheckable = true, func = function()
      Ask("ESTERIA_HOUSING_BUY_GUILD_HOOD", string.format("Buy a guild neighborhood at %s for %s from the guild bank?",
        MAPS[map], GetCoinTextureString(g.price)), map)
    end }
  end
  EasyMenu(items, menu, self, 0, 0, "MENU")
end)

-- the Guild Master's per-rank rights: buy plots, invite visitors, manage (move the neighborhood)
ranksButton:SetScript("OnClick", function(self)
  local g = info and info.guild
  if not g or not g.leader then return end
  local items = { { text = "Rank Permissions", isTitle = true, notCheckable = true } }
  for rank, r in ipairs(g.ranks) do
    local now = { buy = r.buy, invite = r.invite, manage = r.manage }
    local sub = {}
    for _, right in ipairs({ { "buy", "Buy plots" }, { "invite", "Invite visitors" }, { "manage", "Manage the neighborhood" } }) do
      sub[#sub + 1] = { text = right[2], checked = now[right[1]], keepShownOnClick = true, func = function()
        now[right[1]] = not now[right[1]]
        EsteriaHousing_SetGuildRankRights(rank, now.buy, now.invite, now.manage)
      end }
    end
    items[#items + 1] = { text = GuildControlGetRankName(rank + 1) or ("Rank " .. rank), notCheckable = true, hasArrow = true,
      menuList = sub }
  end
  EasyMenu(items, menu, self, 0, 0, "MENU")
end)

-- ------------------------------------------------------------------------------------------------ lists

local PLOT_LOOK = {
  yours = { icon = "housefinder_main-icon", order = 1 },
  visit = { icon = "housefinder_friend-plot-icon", order = 2 },
  sale = { icon = "housefinder_forsale-plot-icon", order = 3 },
}
local ROLE_TEXT = { own = "|cff40ff40Yours|r", member = "|cff80c0ffInvited|r", guild = "|cffffd100Your guild's|r",
  visit = "Visiting" }

local function GoPlot(hood, plot) return function() EsteriaHousing_TravelPlot(hood, plot) end end

-- each row: { icon, text, go = { label, fn, enabled }, extra = { label, fn } }
local function ListFor(blocked)
  local out = {}
  if tab == "plots" then
    local list = {}
    for _, p in ipairs(info.plots) do if PLOT_LOOK[p.status] then list[#list + 1] = p end end
    table.sort(list, function(a, b)
      if a.status ~= b.status then return PLOT_LOOK[a.status].order < PLOT_LOOK[b.status].order end
      return a.plot < b.plot
    end)
    for _, p in ipairs(list) do
      local text = p.status == "yours" and string.format("Plot %d: |cff40ff40Your home|r", p.plot)
        or p.status == "visit" and string.format("Plot %d: |cff80c0ff%s|r", p.plot, p.name ~= "" and p.name .. "'s home" or "A neighbor's home")
        or string.format("Plot %d: For sale", p.plot)
      out[#out + 1] = { icon = PLOT_LOOK[p.status].icon, text = text, go = { "Go", GoPlot(info.id, p.plot), not blocked } }
    end
  elseif tab == "homes" then
    for _, h in ipairs(info.homes) do
      local where = h.hood > 0 and string.format("Plot %d, %s", h.plot, h.hoodName) or "|cffc0c0c0Saved: on no plot|r"
      out[#out + 1] = { icon = "housefinder_main-icon",
        text = string.format("|cff40ff40House %d|r  %s\n%d rooms, %d/%d decor, visitors: %s", h.house, where, h.rooms, h.placed,
          h.maxDecor, VisitorText(h.access)),
        go = h.hood > 0 and { "Go", GoPlot(h.hood, h.plot), not blocked }
          or { "Enter", function() EsteriaHousing_EnterHouse(h.house) end, not blocked },
        extra = { "Visitors", function(b) H.ShowAccessMenu(b, h.house) end } }
    end
  elseif tab == "hoods" then
    for _, n in ipairs(info.hoods) do
      out[#out + 1] = { icon = n.kind == "guild" and "housefinder_friend-icon-circle" or "housefinder_main-icon",
        text = string.format("%s\n%s, %s, %d plots free", n.name, MAPS[n.map] or "", ROLE_TEXT[n.role] or "", n.free),
        go = { "Go", function() EsteriaHousing_TravelNeighborhood(n.id) end, not blocked },
        extra = { "View", function() tab = "plots"; page = 0; EsteriaHousing_RequestNeighborhood(n.id) end } }
    end
  elseif tab == "favorites" or tab == "recent" then
    for _, v in ipairs(info[tab]) do
      local text = string.format("%s\n%s", v.owner ~= "" and v.owner .. "'s home" or "A house",
        v.plot > 0 and string.format("Plot %d, %s", v.plot, v.hoodName) or "No longer standing")
      if not v.ok then text = "|cff808080" .. text:gsub("|c%x%x%x%x%x%x%x%x", ""):gsub("|r", "") .. " (closed to you)|r" end
      local fav = tab == "favorites"
      if not fav then
        for _, x in ipairs(info.favorites) do if x.house == v.house and x.hood == v.hood then fav = true end end
      end
      out[#out + 1] = { icon = "housefinder_friend-plot-icon", text = text,
        go = { "Go", GoPlot(v.hood, v.plot), v.ok and not blocked },
        extra = { fav and "Unfavorite" or "Favorite", function() EsteriaHousing_SetFavorite(v.house, v.hood, not fav) end } }
    end
  elseif tab == "manage" then
    for _, m in ipairs(info.members) do
      out[#out + 1] = { icon = "housefinder_friend-icon-circle", text = m.name,
        go = { "Remove", function() EsteriaHousing_Uninvite(info.id, m.account) end, true } }
    end
  end
  return out
end

local SUMMARY = {
  homes = "Every house your account owns, on every plot it stands on. A house on several plots is the same house in each.",
  hoods = "Your own neighborhood, those you were invited to, your guild's, and any where a house lets you in.",
  favorites = "Houses you marked as favorites, shared by all your characters.",
  recent = "This character's last visits to other players' houses.",
}

function Redraw()
  local blocked = Blocked()
  reason:SetText(blocked or (info and info.message) or "")
  SetEnabled(travel, info and not blocked)
  for key, b in pairs(tabButtons) do if key == tab then b:LockHighlight() else b:UnlockHighlight() end end
  if not info then
    place:SetText(""); hoodTitle:SetText(""); hoodBody:SetText(""); summary:SetText(""); manage:Hide()
    for _, row in ipairs(rows) do row:Hide() end
    prev:Hide(); nextPage:Hide(); pageText:SetText("")
    return
  end
  place:SetText(info.mapName)
  hoodTitle:SetText(info.name)
  local yours, free = 0, 0
  for _, p in ipairs(info.plots) do
    if p.status == "yours" then yours = yours + 1 elseif p.status == "sale" then free = free + 1 end
  end
  local who = info.rights.manage and (info.kind == "guild" and "You manage it." or "It is yours.")
    or info.rights.buy and "You may buy plots here." or "You are visiting."
  hoodBody:SetText(string.format("%s, %d of %d plots free at %s.\n%s%s", info.mapName, free, #info.plots,
    GetCoinTextureString(info.price), who, yours > 0 and string.format(" You have %d home%s here.", yours, yours > 1 and "s" or "") or ""))
  H.Atlas(hoodIcon, info.kind == "guild" and "housefinder_friend-icon-circle" or "housefinder_main-icon")
  travel:SetText(info.here and "Leave " .. info.name or "Teleport to " .. info.name)

  if tab == "manage" then
    manage:Show()
    local canInvite = info.rights.invite
    for _, w in ipairs({ inviteBox, inviteButton, inviteHint }) do if canInvite then w:Show() else w:Hide() end end
    moveButton:SetText(string.format("Move this neighborhood to %s", MAPS[1 - info.map]))
    SetEnabled(moveButton, info.rights.manage)
    local g = info.guild
    if g then
      guildButton:Show(); ranksButton:Show()
      SetEnabled(guildButton, g.leader and g.canBuy and g.bank >= g.price); SetEnabled(ranksButton, g.leader)
      guildText:SetText(g.leader and string.format("A guild neighborhood costs %s from the guild bank (%s). Another can be " ..
        "bought once every one the guild has is full.", GetCoinTextureString(g.price), GetCoinTextureString(g.bank))
        or "Your Guild Master can buy a guild neighborhood; your guild's appear under Neighborhoods.")
    else
      guildButton:Hide(); ranksButton:Hide(); guildText:SetText("Join a guild to live in a guild neighborhood.")
    end
    membersTitle:SetText(canInvite and (#info.members > 0 and "Invited" or "No one is invited yet.") or "")
    summary:SetText(canInvite and "" or "Only the owner (or a guild rank the Guild Master allows) can invite or move this neighborhood.")
  else
    manage:Hide()
    summary:SetText(tab == "plots" and string.format("Plots of %s", info.name) or SUMMARY[tab] or "")
  end

  listed = ListFor(blocked)
  local top = tab == "manage" and LIST_TOP - 120 or LIST_TOP
  local shown = tab == "manage" and ROWS - 4 or ROWS
  local pages = math.max(1, math.ceil(#listed / shown))
  page = math.min(page, pages - 1)
  for i, row in ipairs(rows) do
    local item = i <= shown and listed[page * shown + i]
    row:ClearAllPoints(); row:SetPoint("TOP", 0, top - (i - 1) * ROW_H)
    if item then
      H.Atlas(row.icon, item.icon)
      row.label:SetText(item.text)
      row.go:SetText(item.go[1]); row.onGo = item.go[2]; SetEnabled(row.go, item.go[3])
      if item.extra then row.extra:SetText(item.extra[1]); row.onExtra = item.extra[2]; row.extra:Show() else row.extra:Hide() end
      row:Show()
    else
      row:Hide()
    end
  end
  if pages > 1 then prev:Show(); nextPage:Show() else prev:Hide(); nextPage:Hide() end
  SetEnabled(prev, page > 0); SetEnabled(nextPage, page < pages - 1)
  pageText:SetText(pages > 1 and string.format("Page %d of %d", page + 1, pages) or "")
end

prev:SetScript("OnClick", function() page = math.max(page - 1, 0); Redraw() end)
nextPage:SetScript("OnClick", function() page = page + 1; Redraw() end)
travel:SetScript("OnClick", function()
  if not info then return end
  if info.here then EsteriaHousing_TravelBack() else EsteriaHousing_TravelNeighborhood(info.id) end
end)

-- ------------------------------------------------------------------------------------------------ the button

local function StockButton()
  local b = CreateFrame("Button", "NeighborhoodMicroButton", MainMenuBarArtFrame, "MainMenuBarMicroButton")
  b:SetPoint("BOTTOMLEFT", HelpMicroButton, "BOTTOMRIGHT", -3, 0)
  b:SetNormalTexture("Interface\\Esteria\\NeighborhoodMicro-Up")
  b:SetPushedTexture("Interface\\Esteria\\NeighborhoodMicro-Down")
  b:SetDisabledTexture("Interface\\Esteria\\NeighborhoodMicro-Disabled")
  b:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight")
  b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
  b:SetScript("OnClick", function() EsteriaHousing_ToggleNeighborhood() end)
  b:SetScript("OnEnter", function(self) EsteriaHousing_MicroTooltip(self) end)
  -- the vehicle bars move Blizzard's own buttons to their frames; this one follows Help, so it follows its parent too
  hooksecurefunc("VehicleMenuBar_MoveMicroButtons", function() b:SetParent(HelpMicroButton:GetParent()) end)
  return b
end

local button = _G.NeighborhoodMicroButton or StockButton()
-- a greyed button still has to explain itself
if button.SetMotionScriptsWhileDisabled then button:SetMotionScriptsWhileDisabled(true) end

function EsteriaHousing_MicroTooltip(self)
  GameTooltip:SetOwner(self, "ANCHOR_LEFT")
  GameTooltip:SetText("Neighborhood", 1, 0.82, 0.1)
  GameTooltip:AddLine(EsteriaHousing_IsAvailable() and "Your homes, your neighborhood and the way there."
    or "Housing is not available on this realm.", 1, 1, 1, true)
  GameTooltip:Show()
end

function EsteriaHousing_ToggleNeighborhood()
  if f:IsShown() then f:Hide() elseif EsteriaHousing_IsAvailable() then f:Show() end
end

f:SetScript("OnShow", function()
  PlaySound("igCharacterInfoOpen")
  button:SetButtonState("PUSHED", 1)
  EsteriaHousing_RequestNeighborhood()
  Redraw()
end)
f:SetScript("OnHide", function()
  PlaySound("igCharacterInfoClose")
  button:SetButtonState("NORMAL")
  CloseDropDownMenus()
end)

local function SyncAvailable()
  local on = EsteriaHousing_IsAvailable()
  SetEnabled(button, on)
  if not on then f:Hide() end
end
SyncAvailable()

for _, event in ipairs({ "ESTERIA_HOUSING_AVAILABLE", "ESTERIA_HOUSING_NEIGHBORHOOD", "PLAYER_ENTERING_WORLD", "PLAYER_REGEN_DISABLED",
  "PLAYER_REGEN_ENABLED", "PLAYER_DEAD", "PLAYER_ALIVE", "PLAYER_UNGHOST", "PLAYER_CONTROL_GAINED", "PLAYER_CONTROL_LOST" }) do
  f:RegisterEvent(event)
end
f:SetScript("OnEvent", function(self, event)
  if event == "ESTERIA_HOUSING_AVAILABLE" then SyncAvailable()
  elseif event == "ESTERIA_HOUSING_NEIGHBORHOOD" then
    info = EsteriaHousing_GetNeighborhood()
    if accessWanted and info and #info.homes > 0 then H.ShowAccessMenu(accessWanted[1], accessWanted[2]) end
  elseif event == "PLAYER_ENTERING_WORLD" and self:IsShown() then EsteriaHousing_RequestNeighborhood() end
  if self:IsShown() then Redraw() end
end)
