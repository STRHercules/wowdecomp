-- Esteria housing: the plot window, in retail's housing art. Clicking a plot's cornerstone opens it (the server sends
-- SMSG_PLOT instead of a gossip menu). A plot for sale shows its price and a Buy button; a bought plot offers Enter or
-- Visit; your own plot also restyles your house, swaps its parts (walls, roof, door, windows, chimney: retail's shapes and
-- their colours) and moves or turns it on the plot.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local REACH = 15 -- yards from where the window opened; the server stops answering past this too
local W, SALE_H, YOURS_H, PART_H = 360, 260, 400, 28

local f = H.Panel("EsteriaHousingPlotFrame", W, SALE_H)
f:SetPoint("LEFT", 60, 40)
tinsert(UISpecialFrames, "EsteriaHousingPlotFrame")

local art = H.Atlas(f:CreateTexture(nil, "BACKGROUND"), "housing-cornerstone-background")
art:SetPoint("TOPLEFT", 4, -40); art:SetPoint("BOTTOMRIGHT", -4, 4); art:SetAlpha(0.35)
local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -9)
local place = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
place:SetPoint("TOP", title, "BOTTOM", 0, -2)
local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", 2, 2)

local icon = f:CreateTexture(nil, "ARTWORK")
icon:SetSize(44, 44); icon:SetPoint("TOPLEFT", 18, -52)
local status = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
status:SetPoint("LEFT", icon, "RIGHT", 10, 0)
local body = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
body:SetPoint("TOPLEFT", 20, -106); body:SetWidth(W - 40); body:SetJustifyH("LEFT")

local priceLabel = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
priceLabel:SetPoint("TOPLEFT", 20, -156); priceLabel:SetText("Price:")
local price = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
price:SetPoint("LEFT", priceLabel, "RIGHT", 8, 0)

local message = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
message:SetPoint("BOTTOM", 0, 50); message:SetWidth(W - 40)

local action = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
action:SetSize(160, 26); action:SetPoint("BOTTOM", 0, 16)

local plot, openedAt

-- ------------------------------------------------------------------------------------------------ your house

-- Your plot: the house's style, and where it stands (moved in yd from its spot, turned in 15 degree steps).
local house = CreateFrame("Frame", nil, f)
house:SetSize(W - 32, 190); house:SetPoint("TOP", 0, -146)
house:Hide()
local divider = H.Atlas(house:CreateTexture(nil, "ARTWORK"), "housing-floor-list-divider-active")
divider:SetPoint("TOPLEFT", 0, 0); divider:SetPoint("TOPRIGHT", 0, 0); divider:SetHeight(2)
local sign = H.Atlas(house:CreateTexture(nil, "ARTWORK"), "housing-woodsign", 150, 24)
sign:SetPoint("TOP", 0, -10)
local houseTitle = house:CreateFontString(nil, "OVERLAY", "GameFontNormal")
houseTitle:SetPoint("CENTER", sign, "CENTER", 0, 1); houseTitle:SetText("Your House")

-- a dropdown in the client's own style
local styleMenu = CreateFrame("Frame", "EsteriaHousingStyleMenu", UIParent, "UIDropDownMenuTemplate")
local function DropButton(name, width, height)
  local b = CreateFrame("Button", name, house)
  b:SetSize(width, height)
  b:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
  b:SetBackdropColor(0, 0, 0, 0.7); b:SetBackdropBorderColor(0.55, 0.42, 0.25)
  b:SetFontString(b:CreateFontString(nil, "OVERLAY", "GameFontHighlight"))
  b:GetFontString():SetPoint("LEFT", 10, 0)
  local arrow = b:CreateTexture(nil, "OVERLAY")
  arrow:SetTexture("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up"); arrow:SetSize(height - 4, height - 4)
  arrow:SetPoint("RIGHT", height - 4, 0)
  b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
  return b
end
local styleButton = DropButton("EsteriaHousingPlotStyle", W - 72, 26)
styleButton:SetPoint("TOP", -4, -44)
styleButton:SetScript("OnClick", function()
  local items = { { text = "House Style", isTitle = true, notCheckable = true } }
  for _, st in ipairs(plot and plot.styles or {}) do
    items[#items + 1] = { text = st.name, checked = st.id == plot.style,
      func = function() EsteriaHousing_SetPlotStyle(st.id) end }
  end
  EasyMenu(items, styleMenu, styleButton, 0, 0, "MENU")
end)

local placeTitle = house:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
placeTitle:SetPoint("TOPLEFT", 6, -84); placeTitle:SetText("Placement")
local placeText = house:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
placeText:SetPoint("TOPLEFT", placeTitle, "BOTTOMLEFT", 0, -4); placeText:SetWidth(170); placeText:SetJustifyH("LEFT")

-- one step: 1 yd, Shift 0.25 yd. dx is north, dy west (the world's axes); the pad reads as the map does, north up.
-- Up and down move it 1 yd (Shift 0.25) up to 15 yd either way; size steps by 0.05 from x0.5 to x1.5.
local function Nudge(north, west, turn, up, grow)
  if not plot then return end
  local step = IsShiftKeyDown() and 0.25 or 1
  local dx, dy = plot.dx + north * step, plot.dy + west * step
  local reach = plot.reach or 8
  local d = math.sqrt(dx * dx + dy * dy)
  if d > reach then dx, dy = dx * reach / d, dy * reach / d end
  local dz = math.max(-15, math.min(15, (plot.dz or 0) + (up or 0) * step))
  local scale = math.max(0.5, math.min(1.5, (plot.scale or 1) + (grow or 0) * 0.05))
  EsteriaHousing_PlacePlotHouse(dx, dy, plot.turn + turn, dz, math.floor(scale * 100 + 0.5) / 100)
end

local function IconButton(name, atlas, size, onClick, tip)
  local b = CreateFrame("Button", "EsteriaHousingPlot" .. name, house)
  b:SetSize(size, size)
  b.bg = b:CreateTexture(nil, "BACKGROUND"); b.bg:SetAllPoints(); b.bg:SetTexture(0, 0, 0, 0.55)
  b.icon = H.Atlas(b:CreateTexture(nil, "ARTWORK"), atlas); b.icon:SetPoint("TOPLEFT", 3, -3); b.icon:SetPoint("BOTTOMRIGHT", -3, 3)
  b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
  b:SetScript("OnClick", onClick)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(tip); GameTooltip:AddLine("Shift: a quarter step", 1, 1, 1)
    GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  return b
end

-- the move pad on the right, a compass with north up, and Up / Down beside it
local PAD_X, PAD_Y, PAD = W - 128, -100, 28
local north = IconButton("North", "housing-hotkey-icon-arrow-up", PAD, function() Nudge(1, 0, 0) end, "Move north")
north:SetPoint("TOP", house, "TOPLEFT", PAD_X, PAD_Y + 12)
local west = IconButton("West", "housing-hotkey-icon-arrow-left", PAD, function() Nudge(0, 1, 0) end, "Move west")
west:SetPoint("TOP", house, "TOPLEFT", PAD_X - PAD - 2, PAD_Y - PAD + 10)
local east = IconButton("East", "housing-hotkey-icon-arrow-right", PAD, function() Nudge(0, -1, 0) end, "Move east")
east:SetPoint("TOP", house, "TOPLEFT", PAD_X + PAD + 2, PAD_Y - PAD + 10)
local south = IconButton("South", "housing-hotkey-icon-arrow-down", PAD, function() Nudge(-1, 0, 0) end, "Move south")
south:SetPoint("TOP", house, "TOPLEFT", PAD_X, PAD_Y - 2 * PAD + 8)
local function TextButton(name, text, width, onClick, tip)
  local b = CreateFrame("Button", "EsteriaHousingPlot" .. name, house, "UIPanelButtonTemplate")
  b:SetSize(width, 22); b:SetText(text)
  b:SetScript("OnClick", onClick)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(tip); GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  return b
end
local raise = TextButton("Up", "Up", 50, function() Nudge(0, 0, 0, 1) end, "Raise the house (Shift: a quarter step)")
raise:SetPoint("TOP", house, "TOPLEFT", PAD_X + 2 * PAD + 34, PAD_Y + 2)
local lower = TextButton("Down", "Down", 50, function() Nudge(0, 0, 0, -1) end, "Lower the house (Shift: a quarter step)")
lower:SetPoint("TOP", raise, "BOTTOM", 0, -4)
local compass = house:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
compass:SetPoint("TOP", house, "TOPLEFT", PAD_X, PAD_Y - PAD + 2); compass:SetText("N")

-- turning on the left, under the placement text
local turnLeft = IconButton("TurnLeft", "decor-ability-expert-rotate-default", 34, function() Nudge(0, 0, 15) end,
  "Turn left 15\194\176")
turnLeft:SetPoint("TOPLEFT", 6, -138)
local turnRight = IconButton("TurnRight", "decor-ability-expert-rotate-default", 34, function() Nudge(0, 0, -15) end,
  "Turn right 15\194\176")
turnRight:SetPoint("LEFT", turnLeft, "RIGHT", 6, 0)
local rotate = H.ATLAS and H.ATLAS["decor-ability-expert-rotate-default"]
if rotate then turnRight.icon:SetTexCoord(rotate[5], rotate[4], rotate[6], rotate[7]) end -- mirrored: turns the other way
local reset = CreateFrame("Button", "EsteriaHousingPlotReset", house, "UIPanelButtonTemplate")
reset:SetSize(64, 22); reset:SetPoint("LEFT", turnRight, "RIGHT", 8, 0); reset:SetText("Reset")
reset:SetScript("OnClick", function() EsteriaHousing_PlacePlotHouse(0, 0, 0, 0, 1) end)
local smaller = TextButton("Smaller", "-", 24, function() Nudge(0, 0, 0, 0, -1) end, "Make the house smaller")
smaller:SetPoint("LEFT", reset, "RIGHT", 8, 0)
local bigger = TextButton("Bigger", "+", 24, function() Nudge(0, 0, 0, 0, 1) end, "Make the house bigger")
bigger:SetPoint("LEFT", smaller, "RIGHT", 2, 0)

-- the parts: one dropdown each; a shape with colours opens them (retail names a variant "Shape - Colour")
local PART_NAMES = { [9] = "Walls", [10] = "Roof", [11] = "Door", [12] = "Windows", [16] = "Chimney" }
local partsTitle = house:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
partsTitle:SetPoint("TOPLEFT", 6, -186); partsTitle:SetText("House Parts")
local partRows = {}

local function Shape(name) return (name:match("^(.-) %- ") or name) end
local function Colour(name) return name:match(" %- (.+)$") or name end

local function PartMenu(part)
  local items = { { text = PART_NAMES[part.type] or "Part", isTitle = true, notCheckable = true } }
  local variants = {}
  for _, o in ipairs(part.options) do
    if o.parent ~= 0 then variants[o.parent] = variants[o.parent] or {}; table.insert(variants[o.parent], o) end
  end
  local function Pick(o) return function() EsteriaHousing_SetPlotPart(part.type, o.id); CloseDropDownMenus() end end
  for _, o in ipairs(part.options) do
    if o.parent == 0 then
      local colours = variants[o.id]
      local item = { text = Shape(o.name), checked = o.id == part.chosen, func = Pick(o) }
      if colours then
        item.hasArrow = true
        item.menuList = { { text = Colour(o.name), checked = o.id == part.chosen, func = Pick(o) } }
        for _, v in ipairs(colours) do
          item.checked = item.checked or v.id == part.chosen
          table.insert(item.menuList, { text = Colour(v.name), checked = v.id == part.chosen, func = Pick(v) })
        end
      end
      items[#items + 1] = item
    end
  end
  return items
end

local function PartRow(i)
  if partRows[i] then return partRows[i] end
  local row = {}
  row.label = house:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  row.label:SetPoint("TOPLEFT", 10, -204 - (i - 1) * PART_H - 6); row.label:SetWidth(70); row.label:SetJustifyH("LEFT")
  row.button = DropButton("EsteriaHousingPlotPart" .. i, W - 136, 22)
  row.button:SetPoint("TOPLEFT", 82, -204 - (i - 1) * PART_H)
  row.button:SetScript("OnClick", function(self) if row.part then EasyMenu(PartMenu(row.part), styleMenu, self, 0, 0, "MENU") end end)
  partRows[i] = row
  return row
end

local function RedrawParts(parts)
  for i, part in ipairs(parts) do
    local row = PartRow(i)
    row.part = part
    row.label:SetText(PART_NAMES[part.type] or "Part")
    local name = "Default"
    for _, o in ipairs(part.options) do if o.id == part.chosen then name = o.name end end
    row.button:SetText(name)
    row.label:Show(); row.button:Show()
  end
  for i = #parts + 1, #partRows do partRows[i].part = nil; partRows[i].label:Hide(); partRows[i].button:Hide() end
  if #parts > 0 then partsTitle:Show() else partsTitle:Hide() end
end

-- ------------------------------------------------------------------------------------------------ redraw

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
  if mine then house:Show() else house:Hide() end
  local parts = mine and plot.parts or {}
  f:SetHeight(mine and YOURS_H + (#parts > 0 and 24 + #parts * PART_H or 0) or SALE_H)
  house:SetHeight(190 + (#parts > 0 and 24 + #parts * PART_H or 0))
  RedrawParts(parts)
  if mine then
    local name = "Style"
    for _, st in ipairs(plot.styles) do if st.id == plot.style then name = st.name end end
    styleButton:SetText(name)
    placeText:SetText(string.format("%.2f yd north, %.2f yd west, %.2f yd up\nturned %d\194\176, size x%.2f\nup to %d yd from its spot",
      plot.dx, plot.dy, plot.dz or 0, plot.turn, plot.scale or 1, plot.reach or 8))
  end
  if plot.status == "sale" then
    status:SetText("|cffffd100For Sale|r")
    H.Atlas(icon, "housefinder_forsale-plot-icon")
    body:SetText("An empty plot. Buying it builds a house here that you can furnish, expand and restyle.")
    priceLabel:Show(); price:Show()
    local afford = GetMoney() >= plot.price
    price:SetText((afford and "" or "|cffff4040") .. GetCoinTextureString(plot.price))
    action:SetText("Buy Plot")
    if not afford or plot.houseElsewhere then action:Disable() end
    if plot.houseElsewhere and plot.message == "" then message:SetText("Your house already stands on another plot.") end
  elseif plot.status == "yours" then
    status:SetText("|cff40ff40Your Home|r")
    H.Atlas(icon, "housefinder_main-icon")
    body:SetText("Walk in through the front door or step inside from here.")
    action:SetText("Enter House")
  elseif plot.status == "visit" then
    status:SetText("|cff80c0ffA Neighbor's Home|r")
    H.Atlas(icon, "housefinder_friend-icon-circle")
    body:SetText((plot.owner ~= "" and plot.owner .. "'s home. " or "") .. "The owner lets you visit.")
    action:SetText("Visit House")
  else
    status:SetText("|cffc0c0c0Occupied|r")
    H.Atlas(icon, "housefinder_main-icon")
    body:SetText((plot.owner ~= "" and plot.owner .. " lives here." or "Someone lives here.") .. " The owner has not opened it to you.")
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
f:SetScript("OnHide", function() StaticPopup_Hide("ESTERIA_HOUSING_BUY_PLOT"); CloseDropDownMenus() end)

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
