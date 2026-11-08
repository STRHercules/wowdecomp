-- Esteria housing: the plot window, in retail's housing art. Clicking a plot's cornerstone opens it (the server sends
-- SMSG_PLOT instead of a gossip menu). A plot for sale shows its price and a Buy button; a bought plot offers Enter or
-- Visit; your own plot also restyles your house, swaps its parts (walls, roof, door, windows, chimney: retail's shapes and
-- their colours) and moves or turns it on the plot.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local REACH = 15 -- yards from where the window opened; the server stops answering past this too
local W, SALE_H, PART_H = 360, 260, 28

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

-- a neighbor's house: keep it on the Neighborhood window's Favorites
local favorite = CreateFrame("Button", "EsteriaHousingPlotFavorite", f, "UIPanelButtonTemplate")
favorite:SetSize(90, 20); favorite:SetPoint("TOPRIGHT", -26, -60)
favorite:SetScript("OnClick", function()
  if plot and plot.house > 0 then EsteriaHousing_SetFavorite(plot.house, plot.hood, not plot.favorite) end
end)

-- your plot: take the house off it (saved, with its rooms and indoor decor, to put on another plot)
local moveOut = CreateFrame("Button", "EsteriaHousingPlotMoveOut", f, "UIPanelButtonTemplate")
moveOut:SetSize(80, 20); moveOut:SetPoint("BOTTOMLEFT", 14, 19); moveOut:SetText("Move Out")
moveOut:SetScript("OnClick", function() StaticPopup_Show("ESTERIA_HOUSING_MOVE_OUT") end)
StaticPopupDialogs["ESTERIA_HOUSING_MOVE_OUT"] = {
  text = "Move your house off this plot?\n\nThe house, its rooms and indoor decor are saved, and the decor in its yard goes back to storage. Buy a plot anywhere to put it there again.",
  button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_MoveOut() end,
}

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

-- one step: 1 yd, Shift 0.25 yd. dx is north, dy west (the world's axes); the keys read as the map does, north up.
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

-- Placement, as retail's key hints: one row each for position, height, rotation and size, its keycaps on the left and
-- its value on the right, in a dark inset.
local ROW_H, ROWS_TOP = 30, -82
local function Inset(top, height)
  local box = H.Atlas(house:CreateTexture(nil, "BACKGROUND"), "housing-basic-panel-innerblackbox")
  box:SetPoint("TOPLEFT", 0, top); box:SetPoint("TOPRIGHT", 0, top); box:SetHeight(height); box:SetAlpha(0.6)
  return box
end
local placeBox = Inset(ROWS_TOP + 4, 4 * ROW_H + 30)

local values = {}
local function Row(i, label)
  local y = ROWS_TOP - (i - 1) * ROW_H
  local text = house:CreateFontString(nil, "OVERLAY", "GameFontNormal")
  text:SetPoint("TOPLEFT", 12, y - 7); text:SetText(label)
  local value = house:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  value:SetPoint("TOPRIGHT", -12, y - 7); value:SetJustifyH("RIGHT")
  values[i] = value
  return y
end

-- a keycap: retail's key art with an arrow, rotate icon or sign on it
local function Key(name, y, column, icon, onClick, tip, mirror)
  local b = CreateFrame("Button", "EsteriaHousingPlot" .. name, house)
  b:SetSize(26, 26); b:SetPoint("TOPLEFT", 96 + (column - 1) * 30, y - 2)
  H.Atlas(b:CreateTexture(nil, "BACKGROUND"), "housing-hotkey-icon-key-9slice"):SetAllPoints()
  if H.ATLAS and H.ATLAS[icon] then
    b.icon = H.Atlas(b:CreateTexture(nil, "ARTWORK"), icon)
    b.icon:SetPoint("TOPLEFT", 5, -5); b.icon:SetPoint("BOTTOMRIGHT", -5, 5)
    local a = H.ATLAS[icon]
    if mirror then b.icon:SetTexCoord(a[5], a[4], a[6], a[7]) end
  else
    b.label = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
    b.label:SetPoint("CENTER", 0, 1); b.label:SetText(icon)
  end
  b:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
  b:SetScript("OnClick", onClick)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(tip)
    GameTooltip:AddLine("Shift: a quarter step", 1, 1, 1)
    GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  return b
end

local y = Row(1, "Position")
Key("West", y, 1, "housing-hotkey-icon-arrow-left", function() Nudge(0, 1, 0) end, "Move west")
Key("North", y, 2, "housing-hotkey-icon-arrow-up", function() Nudge(1, 0, 0) end, "Move north")
Key("South", y, 3, "housing-hotkey-icon-arrow-down", function() Nudge(-1, 0, 0) end, "Move south")
Key("East", y, 4, "housing-hotkey-icon-arrow-right", function() Nudge(0, -1, 0) end, "Move east")
y = Row(2, "Height")
Key("Down", y, 1, "housing-floor-arrow-down-default", function() Nudge(0, 0, 0, -1) end, "Lower the house")
Key("Up", y, 2, "housing-floor-arrow-up-default", function() Nudge(0, 0, 0, 1) end, "Raise the house")
y = Row(3, "Rotation")
Key("TurnLeft", y, 1, "decor-ability-expert-rotate-default", function() Nudge(0, 0, 15) end, "Turn left 15\194\176")
Key("TurnRight", y, 2, "decor-ability-expert-rotate-default", function() Nudge(0, 0, -15) end, "Turn right 15\194\176", true)
y = Row(4, "Size")
Key("Smaller", y, 1, "-", function() Nudge(0, 0, 0, 0, -1) end, "Make the house smaller")
Key("Bigger", y, 2, "+", function() Nudge(0, 0, 0, 0, 1) end, "Make the house bigger")

local reset = CreateFrame("Button", "EsteriaHousingPlotReset", house, "UIPanelButtonTemplate")
reset:SetSize(70, 20); reset:SetPoint("TOPRIGHT", -10, ROWS_TOP - 4 * ROW_H - 2); reset:SetText("Reset")
reset:SetScript("OnClick", function() EsteriaHousing_PlacePlotHouse(0, 0, 0, 0, 1) end)
local hint = house:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("TOPLEFT", 12, ROWS_TOP - 4 * ROW_H - 6); hint:SetWidth(220); hint:SetJustifyH("LEFT")
hint:SetText("Or drag it: Decorate, then Expert, and click your house.")

local function RedrawPlacement()
  values[1]:SetText(string.format("%.1f N  %.1f W", plot.dx, plot.dy))
  values[2]:SetText(string.format("%+.2f yd", plot.dz or 0))
  values[3]:SetText(string.format("%d\194\176", plot.turn))
  values[4]:SetText(string.format("x%.2f", plot.scale or 1))
end

-- the parts: one dropdown each; a shape with colours opens them (retail names a variant "Shape - Colour")
local PART_NAMES = { [9] = "Walls", [10] = "Roof", [11] = "Door", [12] = "Windows", [14] = "Dormers", [15] = "Towers",
  [16] = "Chimneys" }
local partsTitle = house:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
partsTitle:SetPoint("TOPLEFT", 6, -242); partsTitle:SetText("House Parts")
local partsBox = Inset(-256, 10)
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
  row.label:SetPoint("TOPLEFT", 12, -260 - (i - 1) * PART_H - 6); row.label:SetWidth(70); row.label:SetJustifyH("LEFT")
  row.label:SetFontObject(GameFontNormal)
  row.button = DropButton("EsteriaHousingPlotPart" .. i, W - 136, 22)
  row.button:SetPoint("TOPLEFT", 90, -260 - (i - 1) * PART_H)
  row.button:SetScript("OnClick", function(self) if row.part then EasyMenu(PartMenu(row.part), styleMenu, self, 0, 0, "MENU") end end)
  partRows[i] = row
  return row
end

-- Roof spots: step through the places on the walls and roof that take a dormer, tower or chimney; the handles mark the
-- one you are on, and the menu puts something there or empties it.
local SPOT_TYPES = { { 0, "Empty" }, { 16, "Chimney", "chimney" }, { 15, "Tower", "tower" }, { 14, "Dormer", "dormer" } }
local spotsTitle = house:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
spotsTitle:SetText("Roof Spots")
local spotsBox = Inset(0, 38)
local spotIndex = 1
local spotLabel = house:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
local spotPrev = Key("SpotPrev", 0, 1, "housing-hotkey-icon-arrow-left", function() spotIndex = spotIndex - 1; H.RedrawSpots() end,
  "Previous spot")
local spotNext = Key("SpotNext", 0, 2, "housing-hotkey-icon-arrow-right", function() spotIndex = spotIndex + 1; H.RedrawSpots() end,
  "Next spot")
local spotButton = DropButton("EsteriaHousingPlotSpot", 120, 22)

local function TypeName(t)
  for _, x in ipairs(SPOT_TYPES) do if x[1] == t then return x[2] end end
  return "Empty"
end

spotButton:SetScript("OnClick", function(self)
  local spot = plot and plot.spots and plot.spots[spotIndex]
  if not spot then return end
  local items = { { text = "Spot " .. spotIndex, isTitle = true, notCheckable = true } }
  for _, x in ipairs(SPOT_TYPES) do
    if x[1] == 0 or spot[x[3]] then
      items[#items + 1] = { text = x[2], checked = spot.type == x[1], func = function() EsteriaHousing_SetPlotSpot(spotIndex, x[1]) end }
    end
  end
  EasyMenu(items, styleMenu, self, 0, 0, "MENU")
end)

function H.RedrawSpots()
  local spots = plot and plot.status == "yours" and plot.spots or {}
  local show = #spots > 0 and f:IsShown()
  for _, w in ipairs({ spotsTitle, spotsBox, spotLabel, spotPrev, spotNext, spotButton }) do if show then w:Show() else w:Hide() end end
  if not show then
    if H.markingSpot then H.markingSpot = nil; EsteriaHousing_SetGizmo(0) end
    return
  end
  spotIndex = (spotIndex - 1) % #spots + 1
  local spot = spots[spotIndex]
  spotLabel:SetText(string.format("%d of %d", spotIndex, #spots))
  spotButton:SetText(TypeName(spot.type))
  H.markingSpot = true
  EsteriaHousing_SetGizmo(1, spot.x, spot.y, spot.z, 1.5, 0, false) -- the arrows point it out on the house
end

-- below the parts, wherever they end
local function PlaceSpots(top)
  spotsTitle:ClearAllPoints(); spotsTitle:SetPoint("TOPLEFT", 6, top)
  spotsBox:ClearAllPoints(); spotsBox:SetPoint("TOPLEFT", 0, top - 14); spotsBox:SetPoint("TOPRIGHT", 0, top - 14)
  spotPrev:ClearAllPoints(); spotPrev:SetPoint("TOPLEFT", 12, top - 20)
  spotNext:ClearAllPoints(); spotNext:SetPoint("LEFT", spotPrev, "RIGHT", 4, 0)
  spotLabel:ClearAllPoints(); spotLabel:SetPoint("LEFT", spotNext, "RIGHT", 10, 0)
  spotButton:ClearAllPoints(); spotButton:SetPoint("TOPRIGHT", -34, top - 22)
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
  partsBox:SetHeight(#parts * PART_H + 8)
  if #parts > 0 then partsTitle:Show(); partsBox:Show() else partsTitle:Hide(); partsBox:Hide() end
end

-- ------------------------------------------------------------------------------------------------ redraw

StaticPopupDialogs["ESTERIA_HOUSING_BUY_PLOT"] = {
  -- StaticPopup_Show formats with two values only, so the whole question arrives as the first
  text = "%s",
  button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, house) EsteriaHousing_BuyPlot(house or 0) end,
}
local buyMenu = CreateFrame("Frame", "EsteriaHousingBuyMenu", UIParent, "UIDropDownMenuTemplate")

-- house 0 builds a new one; another mirrors that house of yours here (the same rooms and decor in both places)
local function ConfirmBuy(house, what)
  local dialog = StaticPopup_Show("ESTERIA_HOUSING_BUY_PLOT", string.format("Buy plot %d in %s for %s?\n\n%s",
    plot.plot, plot.neighborhood, GetCoinTextureString(plot.price), what))
  if dialog then dialog.data = house end
end

local function Redraw()
  if not plot then return f:Hide() end
  title:SetText(string.format("Plot %d", plot.plot))
  place:SetText(plot.neighborhood)
  message:SetText(plot.message or "")
  priceLabel:Hide(); price:Hide(); action:Show(); action:Enable()
  if plot.status == "visit" and plot.house > 0 then
    favorite:SetText(plot.favorite and "Unfavorite" or "Favorite"); favorite:Show()
  else
    favorite:Hide()
  end
  if plot.status == "yours" then moveOut:Show() else moveOut:Hide() end
  local mine = plot.status == "yours" and plot.styles and #plot.styles > 0
  if mine then house:Show() else house:Hide() end
  local parts = mine and plot.parts or {}
  local houseH = 242 + (#parts > 0 and 22 + #parts * PART_H or 0)
  PlaceSpots(-houseH - 6)
  if mine and plot.spots and #plot.spots > 0 then houseH = houseH + 62 end
  house:SetHeight(houseH)
  f:SetHeight(mine and 146 + houseH + 64 or SALE_H)
  RedrawParts(parts)
  if mine then
    local name = "Style"
    for _, st in ipairs(plot.styles) do if st.id == plot.style then name = st.name end end
    styleButton:SetText(name)
    RedrawPlacement()
  end
  if plot.status == "sale" then
    status:SetText("|cffffd100For Sale|r")
    H.Atlas(icon, "housefinder_forsale-plot-icon")
    body:SetText("An empty plot. Buying it builds a house here that you can furnish, expand and restyle.")
    priceLabel:Show(); price:Show()
    local afford = GetMoney() >= plot.price
    price:SetText((afford and "" or "|cffff4040") .. GetCoinTextureString(plot.price))
    action:SetText(#plot.choices > 0 and "Buy Plot..." or "Buy Plot")
    if not afford or not plot.canBuy then action:Disable() end
    if not plot.canBuy and plot.message == "" then message:SetText("Only those who live in this neighborhood may buy here.") end
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
    local fresh = "A new house is built on it right away."
    if #plot.choices == 0 then return ConfirmBuy(0, fresh) end
    local items = { { text = "Put on this plot", isTitle = true, notCheckable = true },
      { text = "A new house", notCheckable = true, func = function() ConfirmBuy(0, fresh) end } }
    for _, c in ipairs(plot.choices) do
      items[#items + 1] = { text = c.label, notCheckable = true, func = function()
        ConfirmBuy(c.house, "Your house stands here too: the same rooms and decor in both places.") end }
    end
    EasyMenu(items, buyMenu, action, 0, 0, "MENU")
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
f:SetScript("OnHide", function()
  StaticPopup_Hide("ESTERIA_HOUSING_BUY_PLOT"); StaticPopup_Hide("ESTERIA_HOUSING_MOVE_OUT"); CloseDropDownMenus()
  if H.markingSpot then H.markingSpot = nil; EsteriaHousing_SetGizmo(0) end
end)

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
  if self:IsShown() then Redraw(); H.RedrawSpots() end
end)
f:SetScript("OnShow", function() Redraw(); H.RedrawSpots() end)
