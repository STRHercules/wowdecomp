-- Esteria housing: the Neighborhood window, opened from a button beside Character, Talents and Social. Every account has
-- a neighborhood of its own (Founder's Point or Razorwind Shores) and a guild may buy more; the window shows the one you
-- are in (or yours), your homes, the neighborhoods you can go to, favorite and recently visited houses, and lets the
-- owner invite others in, move it to the other map, or a Guild Master buy one for the guild.
-- DragonUI's strip owns NeighborhoodMicroButton (Addons/EsteriaShell/esteria_microbuttons.lua); without DragonUI this
-- file adds one to the stock micro bar.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local W, HEIGHT, ROWS, ROW_H = 470, 520, 8, 34
local BOX_TOP, LIST_TOP = -136, -170 -- the inset the tabs stand on, and its first row
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

-- the neighborhood shown, and the way there: a short button on the right, its name and state on the left
local hoodIcon = H.Atlas(f:CreateTexture(nil, "ARTWORK"), "housefinder_main-icon")
hoodIcon:SetSize(44, 44); hoodIcon:SetPoint("TOPLEFT", 18, -52)
local hoodTitle = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
hoodTitle:SetPoint("TOPLEFT", hoodIcon, "TOPRIGHT", 10, -2); hoodTitle:SetWidth(W - 210); hoodTitle:SetJustifyH("LEFT")
local hoodBody = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
hoodBody:SetPoint("TOPLEFT", hoodTitle, "BOTTOMLEFT", 0, -4); hoodBody:SetWidth(W - 210); hoodBody:SetJustifyH("LEFT")
local travel = CreateFrame("Button", "EsteriaHousingNeighborhoodTravel", f, "UIPanelButtonTemplate")
travel:SetSize(100, 22); travel:SetPoint("TOPRIGHT", -20, -58)
travel:SetScript("OnEnter", function(self)
  if not info then return end
  GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
  GameTooltip:AddLine(info.here and "Leave " .. info.name or "Teleport to " .. info.name)
  GameTooltip:Show()
end)
travel:SetScript("OnLeave", function() GameTooltip:Hide() end)
-- why travel is greyed, or the server's last word: one line under the name, cut short rather than wrapped
local reason = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
reason:SetPoint("TOPLEFT", hoodIcon, "BOTTOMLEFT", 0, -6); reason:SetSize(W - 40, 12); reason:SetJustifyH("LEFT")
reason:SetTextColor(1, 0.3, 0.3)

-- tabs: the Interface Options' small tabs, standing on the list's inset
local TABS = { { "plots", "Plots" }, { "homes", "My Homes" }, { "hoods", "Neighborhoods" }, { "favorites", "Favorites" },
  { "recent", "Recent" }, { "manage", "Manage" } }
local tab = "plots"
local tabButtons = {}
local box = CreateFrame("Frame", nil, f)
box:SetPoint("TOPLEFT", 14, BOX_TOP); box:SetPoint("BOTTOMRIGHT", -14, 42)
box:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
  edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
box:SetBackdropColor(0, 0, 0, 0.45); box:SetBackdropBorderColor(0.55, 0.42, 0.25)
local divider = box -- hidden with the rest while choosing
local summary = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
summary:SetPoint("TOPLEFT", 22, BOX_TOP - 10); summary:SetWidth(W - 44); summary:SetJustifyH("LEFT")
summary:SetTextColor(0.75, 0.75, 0.75)

local info      -- the server's last answer
local listed = {}
local page = 0
local rows = {}
local Redraw

for i, t in ipairs(TABS) do
  local b = CreateFrame("Button", "EsteriaHousingNeighborhoodFrameTab" .. i, f, "OptionsFrameTabButtonTemplate")
  b:SetID(i); b:SetText(t[2])
  if i == 1 then b:SetPoint("BOTTOMLEFT", box, "TOPLEFT", 6, -2)
  else b:SetPoint("LEFT", tabButtons[TABS[i - 1][1]], "RIGHT", -14, 0) end
  PanelTemplates_TabResize(b, -8)
  b:SetScript("OnShow", function(self) PanelTemplates_TabResize(self, -8) end) -- the template resizes with 0 padding
  b:SetScript("OnClick", function() tab = t[1]; page = 0; PlaySound("igCharacterInfoTab"); Redraw() end)
  tabButtons[t[1]] = b
end
f.numTabs = #TABS

for i = 1, ROWS do
  local row = CreateFrame("Frame", nil, f)
  row:SetSize(W - 44, ROW_H - 2); row:SetPoint("TOP", 0, LIST_TOP - (i - 1) * ROW_H)
  row.bg = row:CreateTexture(nil, "BACKGROUND"); row.bg:SetAllPoints(); row.bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
  row.icon = row:CreateTexture(nil, "ARTWORK"); row.icon:SetSize(24, 24); row.icon:SetPoint("LEFT", 4, 0)
  row.label = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  row.label:SetPoint("LEFT", row.icon, "RIGHT", 8, 0); row.label:SetWidth(W - 210); row.label:SetJustifyH("LEFT")
  row.go = CreateFrame("Button", "EsteriaHousingNeighborhoodGo" .. i, row, "UIPanelButtonTemplate")
  row.go:SetSize(64, 22); row.go:SetPoint("RIGHT", -4, 0)
  row.go:SetScript("OnClick", function() if row.onGo then row.onGo(row.go) end end)
  row.extra = CreateFrame("Button", "EsteriaHousingNeighborhoodExtra" .. i, row, "UIPanelButtonTemplate")
  row.extra:SetSize(76, 22); row.extra:SetPoint("RIGHT", row.go, "LEFT", -4, 0)
  row.extra:SetScript("OnClick", function() if row.onExtra then row.onExtra(row.extra) end end)
  rows[i] = row
end

local prev = CreateFrame("Button", "EsteriaHousingNeighborhoodPrev", f, "UIPanelButtonTemplate")
prev:SetSize(76, 22); prev:SetPoint("BOTTOMLEFT", 16, 14); prev:SetText("Previous")
local nextPage = CreateFrame("Button", "EsteriaHousingNeighborhoodNext", f, "UIPanelButtonTemplate")
nextPage:SetSize(76, 22); nextPage:SetPoint("BOTTOMRIGHT", -16, 14); nextPage:SetText("Next")
local pageText = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
pageText:SetPoint("BOTTOM", 0, 20)

-- Manage: a line per setting, what it is on the left and one short button on the right
local manage = CreateFrame("Frame", nil, f)
manage:SetSize(W - 44, 4 * ROW_H); manage:SetPoint("TOP", 0, LIST_TOP)
local function Setting(i, buttonText)
  local line = CreateFrame("Frame", nil, manage)
  line:SetSize(W - 44, ROW_H - 2); line:SetPoint("TOP", 0, -(i - 1) * ROW_H)
  local bg = line:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
  line.label = line:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  line.label:SetPoint("LEFT", 10, 0); line.label:SetWidth(W - 150); line.label:SetJustifyH("LEFT")
  line.button = CreateFrame("Button", nil, line, "UIPanelButtonTemplate")
  line.button:SetSize(76, 22); line.button:SetPoint("RIGHT", -4, 0); line.button:SetText(buttonText)
  return line
end
local inviteLine = Setting(1, "Invite")
inviteLine.label:SetWidth(110)
local inviteBox = CreateFrame("EditBox", "EsteriaHousingInviteName", inviteLine, "InputBoxTemplate")
inviteBox:SetSize(150, 20); inviteBox:SetPoint("RIGHT", inviteLine.button, "LEFT", -10, 0)
inviteBox:SetAutoFocus(false); inviteBox:SetMaxLetters(12)
local inviteButton = inviteLine.button
local moveLine = Setting(2, "Move")
local moveButton = moveLine.button
local guildLine = Setting(3, "Buy")
local guildButton = guildLine.button
local ranksLine = Setting(4, "Edit")
local ranksButton = ranksLine.button
local membersTitle = manage:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
membersTitle:SetPoint("TOPLEFT", 8, -4 * ROW_H - 4)
local MANAGE_LIST_TOP = LIST_TOP - 4 * ROW_H - 18 -- the invited players, under the settings

-- The first time (until the account picks): where the account's neighborhood is. Either can be changed later
-- from Manage.
local chooser = CreateFrame("Frame", nil, f)
chooser:SetPoint("TOPLEFT", 12, -50); chooser:SetPoint("BOTTOMRIGHT", -12, 12)
chooser:Hide()
local chooseTitle = chooser:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
chooseTitle:SetPoint("TOP", 0, -6); chooseTitle:SetText("Choose Your Neighborhood")
local chooseText = chooser:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
chooseText:SetPoint("TOP", chooseTitle, "BOTTOM", 0, -8); chooseText:SetWidth(W - 60)
chooseText:SetText("Your account gets a neighborhood of its own, and every one of your characters lives there. " ..
  "Pick where it is. You can move it later from the Manage tab.")
local CHOICES = {
  { map = 0, name = "Founder's Point", art = "Interface\\Glues\\LoadingScreens\\LoadScreenFoundersPoint",
    text = "Green hills, oak groves and a lighthouse coast on the Alliance side of the sea." },
  { map = 1, name = "Razorwind Shores", art = "Interface\\Glues\\LoadingScreens\\LoadScreenRazorwindShores",
    text = "Red canyons, hot springs and a windswept shore on the Horde side of the sea." },
}
StaticPopupDialogs["ESTERIA_HOUSING_CHOOSE_HOOD"] = {
  text = "%s", button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, map) if info then EsteriaHousing_MoveNeighborhood(info.id, map) end end,
}
for i, c in ipairs(CHOICES) do
  local card = CreateFrame("Button", "EsteriaHousingChoose" .. i, chooser)
  card:SetSize((W - 60) / 2, 300); card:SetPoint("TOP", (i == 1 and -1 or 1) * ((W - 60) / 4 + 6), -70)
  card:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
    insets = { left = 3, right = 3, top = 3, bottom = 3 } })
  card:SetBackdropBorderColor(0.55, 0.42, 0.25)
  local art = card:CreateTexture(nil, "ARTWORK")
  art:SetPoint("TOPLEFT", 4, -4); art:SetPoint("TOPRIGHT", -4, -4); art:SetHeight(((W - 60) / 2 - 8) * 3 / 4)
  art:SetTexture(c.art)
  local name = card:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
  name:SetPoint("TOP", art, "BOTTOM", 0, -10); name:SetText(c.name)
  local text = card:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  text:SetPoint("TOP", name, "BOTTOM", 0, -6); text:SetWidth((W - 60) / 2 - 20); text:SetText(c.text)
  local pick = CreateFrame("Button", "EsteriaHousingChoosePick" .. i, card, "UIPanelButtonTemplate")
  pick:SetSize(120, 24); pick:SetPoint("BOTTOM", 0, 12); pick:SetText("Live Here")
  local function Choose()
    local dialog = StaticPopup_Show("ESTERIA_HOUSING_CHOOSE_HOOD", "Make " .. c.name .. " your account's neighborhood?")
    if dialog then dialog.data = c.map end
  end
  pick:SetScript("OnClick", Choose)
  card:SetScript("OnClick", Choose)
  card:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
end
function H.ShowChooser() chooser:Show(); place:SetText("") end
function H.HideChooser() chooser:Hide(); StaticPopup_Hide("ESTERIA_HOUSING_CHOOSE_HOOD") end

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

local ACCESS = { { "party", "Party and raid members", "party" }, { "friends", "Friends", "friends" },
  { "guild", "Guild members", "guild" }, { "everyone", "Everyone", "everyone" } }
local accessWanted -- { anchor, house } to open once the server says what the setting is

local function VisitorText(access)
  if access and access.everyone then return "everyone" end
  local who = {}
  for _, a in ipairs(ACCESS) do if access and access[a[1]] then who[#who + 1] = a[3] end end
  return #who > 0 and table.concat(who, ", ") or "only you"
end

local function Plural(n, word) return n .. " " .. word .. (n == 1 and "" or "s") end

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
      local where = h.hood > 0 and string.format("Plot %d, %s", h.plot, h.hoodName) or "|cffc0c0c0Saved, on no plot|r"
      local home = H.homeChoice and H.homeChoice.house == h.house and (H.homeChoice.hood == h.hood or H.homeChoice.hood == 0)
      out[#out + 1] = { icon = "housefinder_main-icon",
        text = string.format("|cff40ff40House %d|r  |cffffd100%s|r%s\n|cffb0b0b0%s, %d/%d decor, visitors: %s|r", h.house, where,
          home and "  |cff80c0ff(Home)|r" or "", Plural(h.rooms, "room"), h.placed, h.maxDecor, VisitorText(h.access)),
        go = h.hood > 0 and { "Go", GoPlot(h.hood, h.plot), not blocked }
          or { "Enter", function() EsteriaHousing_EnterHouse(h.house) end, not blocked },
        -- who may visit, and whether Teleport: Home and Portal: Home lead here
        extra = { "Options", function(b)
          EasyMenu({ { text = "House " .. h.house, isTitle = true, notCheckable = true },
            { text = "Who can visit...", notCheckable = true, func = function() H.ShowAccessMenu(b, h.house) end },
            { text = "Teleport: Home leads here", checked = home, func = function()
              EsteriaHousing_Commerce(table.concat({ "adv", "home", h.house, h.hood }, "\t")) end } }, menu, b, 0, 0, "MENU")
        end } }
    end
  elseif tab == "hoods" then
    for _, n in ipairs(info.hoods) do
      out[#out + 1] = { icon = n.kind == "guild" and "housefinder_friend-icon-circle" or "housefinder_main-icon",
        text = string.format("%s\n|cffb0b0b0%s, %s free|r  %s", n.name, MAPS[n.map] or "", Plural(n.free, "plot"),
          ROLE_TEXT[n.role] or ""),
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
        extra = { fav and "Remove" or "Favorite", function() EsteriaHousing_SetFavorite(v.house, v.hood, not fav) end } }
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
  homes = "Your account's houses. A mirrored house is the same house on each plot.",
  hoods = "Neighborhoods you can go to: yours, invited, your guild's and friends'.",
  favorites = "Favorite houses, shared by all your characters.",
  recent = "Houses this character visited lately.",
}

function Redraw()
  local blocked = Blocked()
  reason:SetText(blocked or (info and info.message) or "")
  SetEnabled(travel, info and not blocked)
  for i, t in ipairs(TABS) do if t[1] == tab then PanelTemplates_SetTab(f, i) end end
  -- until the account picks its neighborhood, the window is only the choice
  local choosing = info and info.choose
  if choosing then H.ShowChooser() else H.HideChooser() end
  for _, b in pairs(tabButtons) do if choosing then b:Hide() else b:Show() end end
  if choosing then
    for _, w in ipairs({ hoodIcon, hoodTitle, hoodBody, travel, reason, divider, summary, manage, prev, nextPage }) do w:Hide() end
    for _, row in ipairs(rows) do row:Hide() end
    pageText:SetText("")
    return
  end
  for _, w in ipairs({ hoodIcon, hoodTitle, hoodBody, travel, reason, divider, summary }) do w:Show() end
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
  hoodBody:SetText(string.format("|cffb0b0b0%d of %d plots free, %s each|r\n%s%s", free, #info.plots,
    GetCoinTextureString(info.price), who, yours > 0 and " You have " .. Plural(yours, "home") .. " here." or ""))
  H.Atlas(hoodIcon, info.kind == "guild" and "housefinder_friend-icon-circle" or "housefinder_main-icon")
  travel:SetText(info.here and "Leave" or "Teleport")

  if tab == "manage" then
    manage:Show()
    local canInvite = info.rights.invite
    inviteLine.label:SetText(canInvite and "Invite a player" or "|cff808080Only the owner can invite|r")
    if canInvite then inviteBox:Show() else inviteBox:Hide() end
    SetEnabled(inviteButton, canInvite)
    moveLine.label:SetText(string.format("Location: |cffffd100%s|r\n|cffb0b0b0Move it to %s|r", MAPS[info.map], MAPS[1 - info.map]))
    SetEnabled(moveButton, info.rights.manage)
    local g = info.guild
    if g then
      guildLine.label:SetText(string.format("Guild neighborhood: %s\n|cffb0b0b0From the guild bank (%s)|r",
        GetCoinTextureString(g.price), GetCoinTextureString(g.bank)))
      SetEnabled(guildButton, g.leader and g.canBuy and g.bank >= g.price)
      ranksLine.label:SetText(g.leader and "Guild rank permissions" or "|cff808080Guild rank permissions (Guild Master)|r")
      SetEnabled(ranksButton, g.leader)
      guildLine:Show(); ranksLine:Show()
    else
      guildLine.label:SetText("|cff808080Join a guild to live in a guild neighborhood.|r")
      SetEnabled(guildButton, false)
      ranksLine:Hide()
    end
    membersTitle:SetText(canInvite and (#info.members > 0 and "Invited players" or "No one is invited yet.") or "")
    summary:SetText("")
  else
    manage:Hide()
    summary:SetText(tab == "plots" and string.format("Plots of %s", info.name) or SUMMARY[tab] or "")
  end

  listed = ListFor(blocked)
  local top = tab == "manage" and MANAGE_LIST_TOP or LIST_TOP
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
  if EsteriaHousing_Commerce then EsteriaHousing_Commerce("adv\thomeget") end -- where Teleport: Home leads (Adventure.lua)
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

H.RedrawNeighborhood = function() if f:IsShown() then Redraw() end end
