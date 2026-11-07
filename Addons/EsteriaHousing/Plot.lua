-- Esteria housing: the plot window. Clicking a plot's cornerstone opens it (the server sends SMSG_PLOT instead of a
-- gossip menu). A plot for sale shows its price and a Buy button; a bought plot offers Enter or Visit.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local REACH = 15 -- yards from where the window opened; the server stops answering past this too

local f = CreateFrame("Frame", "EsteriaHousingPlotFrame", UIParent)
f:SetSize(340, 250)
f:SetPoint("LEFT", 60, 40)
f:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border",
  tile = true, tileSize = 32, edgeSize = 32, insets = { left = 11, right = 12, top = 12, bottom = 11 } })
f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart", f.StartMoving); f:SetScript("OnDragStop", f.StopMovingOrSizing)
f:Hide()
tinsert(UISpecialFrames, "EsteriaHousingPlotFrame")

local header = f:CreateTexture(nil, "ARTWORK")
header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
header:SetSize(300, 64); header:SetPoint("TOP", 0, 12)
local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
title:SetPoint("TOP", header, "TOP", 0, -14)
local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)

local icon = f:CreateTexture(nil, "ARTWORK")
icon:SetSize(48, 48); icon:SetPoint("TOPLEFT", 24, -34)
icon:SetTexture("Interface\\Icons\\INV_Misc_Note_02")
local status = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
status:SetPoint("TOPLEFT", icon, "TOPRIGHT", 12, -4)
local place = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
place:SetPoint("TOPLEFT", status, "BOTTOMLEFT", 0, -4)

local body = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
body:SetPoint("TOPLEFT", 26, -96); body:SetWidth(288); body:SetJustifyH("LEFT")

local priceLabel = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
priceLabel:SetPoint("TOPLEFT", 26, -160); priceLabel:SetText("Price:")
local price = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
price:SetPoint("LEFT", priceLabel, "RIGHT", 8, 0)

local message = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
message:SetPoint("BOTTOM", 0, 52); message:SetWidth(290)

local action = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
action:SetSize(150, 26); action:SetPoint("BOTTOM", 0, 20)

local plot, openedAt

-- Your plot: the house's style, and where it stands (moved in yd from its spot, turned in 15 degree steps).
local house = CreateFrame("Frame", nil, f)
house:SetSize(300, 150); house:SetPoint("TOP", 0, -112)
house:Hide()
local houseTitle = house:CreateFontString(nil, "OVERLAY", "GameFontNormal")
houseTitle:SetPoint("TOPLEFT", 6, 0); houseTitle:SetText("Your House")
local styleMenu = CreateFrame("Frame", "EsteriaHousingStyleMenu", UIParent, "UIDropDownMenuTemplate")
local styleButton = CreateFrame("Button", nil, house, "UIPanelButtonTemplate")
styleButton:SetSize(288, 24); styleButton:SetPoint("TOPLEFT", 6, -18)
styleButton:SetScript("OnClick", function()
  local items = { { text = "House Style", isTitle = true, notCheckable = true } }
  for _, st in ipairs(plot and plot.styles or {}) do
    items[#items + 1] = { text = st.name, checked = st.id == plot.style,
      func = function() EsteriaHousing_SetPlotStyle(st.id) end }
  end
  EasyMenu(items, styleMenu, "cursor", 0, 0, "MENU")
end)
local placeText = house:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
placeText:SetPoint("TOPLEFT", 6, -50); placeText:SetWidth(150); placeText:SetJustifyH("LEFT")

-- step: 1 yd, Shift 0.25 yd. dx is north, dy west (the world's axes); the arrows read as the map does.
local function Nudge(north, west, turn)
  if not plot then return end
  local step = IsShiftKeyDown() and 0.25 or 1
  local dx, dy = plot.dx + north * step, plot.dy + west * step
  local reach = plot.reach or 8
  local d = math.sqrt(dx * dx + dy * dy)
  if d > reach then dx, dy = dx * reach / d, dy * reach / d end
  EsteriaHousing_PlacePlotHouse(dx, dy, plot.turn + turn)
end
local function Pad(text, x, y, north, west, turn, tip)
  local b = CreateFrame("Button", nil, house, "UIPanelButtonTemplate")
  b:SetSize(text:len() > 2 and 56 or 30, 24); b:SetPoint("TOPLEFT", x, y); b:SetText(text)
  b:SetScript("OnClick", function() Nudge(north, west, turn) end)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(tip); GameTooltip:AddLine("Shift: a quarter step", 1, 1, 1)
    GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  return b
end
Pad("N", 200, -50, 1, 0, 0, "Move north")
Pad("W", 168, -76, 0, 1, 0, "Move west")
Pad("E", 232, -76, 0, -1, 0, "Move east")
Pad("S", 200, -102, -1, 0, 0, "Move south")
Pad("Turn L", 6, -102, 0, 0, 15, "Turn left 15\194\176")
Pad("Turn R", 66, -102, 0, 0, -15, "Turn right 15\194\176")
local reset = CreateFrame("Button", nil, house, "UIPanelButtonTemplate")
reset:SetSize(56, 24); reset:SetPoint("TOPLEFT", 126, -102); reset:SetText("Reset")
reset:SetScript("OnClick", function() EsteriaHousing_PlacePlotHouse(0, 0, 0) end)

StaticPopupDialogs["ESTERIA_HOUSING_BUY_PLOT"] = {
  -- StaticPopup_Show formats with two values only, so the whole question arrives as the first
  text = "%s",
  button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_BuyPlot() end,
}

local function Redraw()
  if not plot then return f:Hide() end
  title:SetText(string.format("Plot %d", plot.plot))
  place:SetText(plot.neighborhood)
  message:SetText(plot.message or "")
  priceLabel:Hide(); price:Hide(); action:Show(); action:Enable()
  local mine = plot.status == "yours" and plot.styles and #plot.styles > 0
  if mine then
    house:Show()
    f:SetHeight(400)
    local name = "Style"
    for _, st in ipairs(plot.styles) do if st.id == plot.style then name = st.name end end
    styleButton:SetText(name .. "  \226\150\190")
    placeText:SetText(string.format("%.2f yd north, %.2f yd west\nturned %d\194\176 (up to %d yd from its spot)",
      plot.dx, plot.dy, plot.turn, plot.reach or 8))
  else
    house:Hide()
    f:SetHeight(250)
  end
  if plot.status == "sale" then
    status:SetText("|cffffd100For Sale|r")
    icon:SetTexture("Interface\\Icons\\INV_Misc_Note_02")
    body:SetText("An empty plot. Buying it builds a Human House here that you can furnish and expand with new rooms.")
    priceLabel:Show(); price:Show()
    local afford = GetMoney() >= plot.price
    price:SetText((afford and "" or "|cffff4040") .. GetCoinTextureString(plot.price))
    action:SetText("Buy Plot")
    if not afford or plot.houseElsewhere then action:Disable() end
    if plot.houseElsewhere and plot.message == "" then message:SetText("Your house already stands on another plot.") end
  elseif plot.status == "yours" then
    status:SetText("|cff40ff40Your Home|r")
    icon:SetTexture("Interface\\Icons\\Spell_Holy_Restoration")
    body:SetText("Walk in through the front door, or step inside from here. Restyle and move your house below.")
    action:SetText("Enter House")
  elseif plot.status == "visit" then
    status:SetText("|cff80c0ffA Neighbor's Home|r")
    icon:SetTexture("Interface\\Icons\\Spell_Holy_PrayerOfSpirit")
    body:SetText("You are grouped with the owner, so you may visit.")
    action:SetText("Visit House")
  else
    status:SetText("|cffc0c0c0Occupied|r")
    icon:SetTexture("Interface\\Icons\\INV_Misc_Key_03")
    body:SetText("Someone lives here. Group with them to visit.")
    action:Hide()
  end
end

action:SetScript("OnClick", function()
  if not plot then return end
  if plot.status == "sale" then
    StaticPopup_Show("ESTERIA_HOUSING_BUY_PLOT", string.format("Buy plot %d in %s for %s?\n\nA house is built on it right away.",
      plot.plot, plot.neighborhood, GetCoinTextureString(plot.price)))
  else
    EsteriaHousing_EnterPlot()
    f:Hide()
  end
end)

f:SetScript("OnUpdate", function(self, elapsed)
  self.wait = (self.wait or 0) + elapsed
  if self.wait < 0.25 then return end
  self.wait = 0
  local x, y = EsteriaHousing_GetPlayerPosition()
  if openedAt and x and ((x - openedAt[1]) ^ 2 + (y - openedAt[2]) ^ 2) > REACH * REACH then self:Hide() end
end)
f:SetScript("OnHide", function() StaticPopup_Hide("ESTERIA_HOUSING_BUY_PLOT") end)

f:RegisterEvent("ESTERIA_HOUSING_PLOT")
f:RegisterEvent("PLAYER_MONEY")
f:SetScript("OnEvent", function(self, event)
  if event == "ESTERIA_HOUSING_PLOT" then
    plot = EsteriaHousing_GetPlot()
    if not self:IsShown() then
      local x, y = EsteriaHousing_GetPlayerPosition()
      openedAt = x and { x, y }
      if plot then PlaySound("igCharacterInfoOpen") end
    end
    if plot then self:Show() end
  end
  if self:IsShown() then Redraw() end
end)
f:SetScript("OnShow", Redraw)
