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

StaticPopupDialogs["ESTERIA_HOUSING_BUY_PLOT"] = {
  text = "Buy plot %s in %s for %s?\n\nA house is built on it right away.",
  button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_BuyPlot() end,
}

local function Redraw()
  if not plot then return f:Hide() end
  title:SetText(string.format("Plot %d", plot.plot))
  place:SetText(plot.neighborhood)
  message:SetText(plot.message or "")
  priceLabel:Hide(); price:Hide(); action:Show(); action:Enable()
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
    body:SetText("Walk in through the front door, or step inside from here.")
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
    StaticPopup_Show("ESTERIA_HOUSING_BUY_PLOT", plot.plot, plot.neighborhood, GetCoinTextureString(plot.price))
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
