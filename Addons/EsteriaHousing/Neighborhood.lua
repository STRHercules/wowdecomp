-- Esteria housing: the Neighborhood window, opened from a button beside Character, Talents and Social. It shows your
-- home and the neighborhood's plots and takes you there. DragonUI's strip owns NeighborhoodMicroButton
-- (Addons/EsteriaShell/esteria_microbuttons.lua); without DragonUI this file adds one to the stock micro bar.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local W, HEIGHT, ROWS, ROW_H = 380, 470, 6, 32

local f = H.Panel("EsteriaHousingNeighborhoodFrame", W, HEIGHT)
f:SetPoint("CENTER", 0, 40)
tinsert(UISpecialFrames, "EsteriaHousingNeighborhoodFrame")

local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -9); title:SetText("Neighborhood")
local place = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
place:SetPoint("TOP", title, "BOTTOM", 0, -2)
local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", 2, 2)

-- your home
local homeIcon = H.Atlas(f:CreateTexture(nil, "ARTWORK"), "housefinder_main-icon")
homeIcon:SetSize(44, 44); homeIcon:SetPoint("TOPLEFT", 18, -52)
local homeTitle = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
homeTitle:SetPoint("TOPLEFT", homeIcon, "TOPRIGHT", 10, -2)
local homeBody = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
homeBody:SetPoint("TOPLEFT", homeTitle, "BOTTOMLEFT", 0, -4); homeBody:SetWidth(W - 90); homeBody:SetJustifyH("LEFT")

-- the way there
local travel = CreateFrame("Button", "EsteriaHousingNeighborhoodTravel", f, "UIPanelButtonTemplate")
travel:SetSize(240, 28); travel:SetPoint("TOP", 0, -128)
local reason = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
reason:SetPoint("TOP", travel, "BOTTOM", 0, -4); reason:SetWidth(W - 40); reason:SetTextColor(1, 0.3, 0.3)

-- the plots
local divider = H.Atlas(f:CreateTexture(nil, "ARTWORK"), "housing-floor-list-divider-active")
divider:SetPoint("TOPLEFT", 16, -182); divider:SetPoint("TOPRIGHT", -16, -182); divider:SetHeight(2)
local summary = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
summary:SetPoint("TOPLEFT", 20, -190); summary:SetWidth(W - 40); summary:SetJustifyH("LEFT")

local LOOK = {
  yours = { icon = "housefinder_main-icon", order = 1 },
  visit = { icon = "housefinder_friend-plot-icon", order = 2 },
  sale = { icon = "housefinder_forsale-plot-icon", order = 3 },
}

local info      -- the server's last answer
local listed = {} -- its plots you can do something with: yours, your group's, those for sale
local page = 0
local rows = {}

for i = 1, ROWS do
  local row = CreateFrame("Frame", nil, f)
  row:SetSize(W - 40, ROW_H - 2); row:SetPoint("TOP", 0, -208 - (i - 1) * ROW_H)
  row.icon = row:CreateTexture(nil, "ARTWORK"); row.icon:SetSize(24, 24); row.icon:SetPoint("LEFT", 2, 0)
  row.label = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  row.label:SetPoint("LEFT", row.icon, "RIGHT", 8, 0); row.label:SetWidth(W - 150); row.label:SetJustifyH("LEFT")
  row.go = CreateFrame("Button", "EsteriaHousingNeighborhoodGo" .. i, row, "UIPanelButtonTemplate")
  row.go:SetSize(60, 22); row.go:SetPoint("RIGHT", 0, 0); row.go:SetText("Go")
  row.go:SetScript("OnClick", function() if row.plot then EsteriaHousing_TravelPlot(row.plot) end end)
  rows[i] = row
end

local prev = CreateFrame("Button", "EsteriaHousingNeighborhoodPrev", f, "UIPanelButtonTemplate")
prev:SetSize(70, 22); prev:SetPoint("BOTTOMLEFT", 20, 14); prev:SetText("Previous")
local nextPage = CreateFrame("Button", "EsteriaHousingNeighborhoodNext", f, "UIPanelButtonTemplate")
nextPage:SetSize(70, 22); nextPage:SetPoint("BOTTOMRIGHT", -20, 14); nextPage:SetText("Next")
local pageText = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
pageText:SetPoint("BOTTOM", 0, 20)

-- Why the server would refuse travel right now (TravelBlock in mod_esteria_housing.cpp), so the buttons can say so first.
local function Blocked()
  if UnitAffectingCombat("player") then return "You cannot travel while in combat." end
  if UnitIsDeadOrGhost("player") then return "You cannot travel while dead." end
  if UnitOnTaxi("player") then return "You cannot travel while on a flight path." end
  local inInstance, kind = IsInInstance()
  if kind == "pvp" or kind == "arena" then return "You cannot travel from a battleground or arena." end
  if inInstance and info and info.where == "away" then return "You cannot travel from inside a dungeon or raid." end
end

-- ------------------------------------------------------------------------------------------------ who may visit

local ACCESS = { { "party", "Party and raid members" }, { "friends", "Friends" }, { "guild", "Guild members" },
  { "everyone", "Everyone" } }
local accessMenu = CreateFrame("Frame", "EsteriaHousingAccessMenu", UIParent, "UIDropDownMenuTemplate")
local accessWanted -- the anchor to open the menu at once the server says what the setting is

local function VisitorText(access)
  local who = {}
  for _, a in ipairs(ACCESS) do if access and access[a[1]] then who[#who + 1] = a[2]:lower() end end
  if access and access.everyone then return "everyone" end
  return #who > 0 and table.concat(who, ", ") or "only you"
end

-- Owner's checkboxes for who may visit; each change is saved at once and sends out visitors who no longer may stay.
function H.ShowAccessMenu(anchor)
  if not (info and info.homePlot > 0) then
    accessWanted = anchor or "cursor"
    return EsteriaHousing_RequestNeighborhood()
  end
  accessWanted = nil
  local now = {}
  for k, v in pairs(info.access) do now[k] = v end
  local menu = { { text = "Who can visit", isTitle = true, notCheckable = true } }
  for _, a in ipairs(ACCESS) do
    local key = a[1]
    menu[#menu + 1] = { text = a[2], checked = now[key], keepShownOnClick = true, func = function()
      now[key] = not now[key]
      EsteriaHousing_SetHouseAccess(now.party, now.friends, now.guild, now.everyone)
    end }
  end
  EasyMenu(menu, accessMenu, anchor or "cursor", 0, 0, "MENU")
end

local function SetEnabled(button, on)
  if on then button:Enable() else button:Disable() end
end

local function RowText(p)
  if p.status == "yours" then return string.format("Plot %d: |cff40ff40Your home|r", p.plot) end
  if p.status == "visit" then
    return string.format("Plot %d: |cff80c0ff%s|r", p.plot, p.name ~= "" and p.name .. "'s home" or "A neighbor's home")
  end
  return string.format("Plot %d: For sale", p.plot)
end

local visitors = CreateFrame("Button", "EsteriaHousingNeighborhoodVisitors", f, "UIPanelButtonTemplate")
visitors:SetSize(90, 22); visitors:SetPoint("TOPRIGHT", -18, -52); visitors:SetText("Visitors")
visitors:SetScript("OnClick", function(self) H.ShowAccessMenu(self) end)

local function Redraw()
  local blocked = Blocked()
  place:SetText(info and info.name or "")
  reason:SetText(blocked or (info and info.message) or "")
  SetEnabled(travel, info and not blocked)
  travel:SetText(info and info.where == "neighborhood" and "Leave " .. info.name or "Teleport to Neighborhood")
  if not info then
    homeTitle:SetText(""); homeBody:SetText(""); summary:SetText(""); visitors:Hide()
    for _, row in ipairs(rows) do row:Hide() end
    prev:Hide(); nextPage:Hide(); pageText:SetText("")
    return
  end
  if info.homePlot > 0 then
    homeTitle:SetText("|cff40ff40Your Home|r")
    homeBody:SetText(string.format("Plot %d, %s\nOwner: %s\n%d rooms, %d of %d decor placed\nVisitors: %s",
      info.homePlot, info.name, UnitName("player"), info.rooms, info.placed, info.maxDecor, VisitorText(info.access)))
    visitors:Show()
  else
    homeTitle:SetText("No Home Yet")
    visitors:Hide()
    homeBody:SetText("Go to a plot for sale and click its cornerstone to buy it.\nYour house is built there right away.")
  end

  local forSale = 0
  listed = {}
  for _, p in ipairs(info.plots) do
    if p.status == "sale" then forSale = forSale + 1 end
    if LOOK[p.status] then listed[#listed + 1] = p end
  end
  table.sort(listed, function(a, b)
    if a.status ~= b.status then return LOOK[a.status].order < LOOK[b.status].order end
    return a.plot < b.plot
  end)
  summary:SetText(string.format("Properties: %d of %d plots for sale at %s", forSale, #info.plots, GetCoinTextureString(info.price)))

  local pages = math.max(1, math.ceil(#listed / ROWS))
  page = math.min(page, pages - 1)
  for i, row in ipairs(rows) do
    local p = listed[page * ROWS + i]
    row.plot = p and p.plot
    if p then
      H.Atlas(row.icon, LOOK[p.status].icon)
      row.label:SetText(RowText(p))
      SetEnabled(row.go, not blocked)
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
  if info and info.where == "neighborhood" then EsteriaHousing_TravelBack() else EsteriaHousing_TravelNeighborhood() end
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
  GameTooltip:AddLine(EsteriaHousing_IsAvailable() and "Your home, your neighborhood and the way there."
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
    if accessWanted and info and info.homePlot > 0 then H.ShowAccessMenu(accessWanted) end
  elseif event == "PLAYER_ENTERING_WORLD" and self:IsShown() then EsteriaHousing_RequestNeighborhood() end
  if self:IsShown() then Redraw() end
end)
