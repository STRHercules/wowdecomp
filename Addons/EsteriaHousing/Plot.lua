-- Esteria housing: the plot window, in retail's housing art. Clicking a plot's cornerstone opens it (the server sends
-- SMSG_PLOT instead of a gossip menu). A plot for sale shows its price and a Buy button; a bought plot offers Enter or
-- Visit; your own plot adds tabs: Style (the house style and its parts: walls, roof, door, windows, chimneys, towers,
-- dormers, in retail's shapes and colours), Placement (move, raise, turn and size it on the plot) and Roof Spots.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local REACH = 15 -- yards from where the window opened; the server stops answering past this too
local W, SALE_H, YOURS_H, PART_H = 380, 250, 470, 28
local BOX_TOP = -134 -- your plot: the inset the tabs stand on

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
status:SetPoint("TOPLEFT", icon, "TOPRIGHT", 10, -4)
local body = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
body:SetPoint("TOPLEFT", status, "BOTTOMLEFT", 0, -4); body:SetWidth(W - 190); body:SetJustifyH("LEFT")

local priceLabel = f:CreateFontString(nil, "OVERLAY", "GameFontNormal")
priceLabel:SetPoint("TOPLEFT", 20, -130); priceLabel:SetText("Price:")
local price = f:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
price:SetPoint("LEFT", priceLabel, "RIGHT", 8, 0)

-- the server's last word, one line, cut short rather than wrapped into what is below
local message = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
message:SetSize(W - 40, 12)

-- the plot's main button (Buy, Enter, Visit): short, at the right of the header
local action = CreateFrame("Button", "EsteriaHousingPlotAction", f, "UIPanelButtonTemplate")
action:SetSize(100, 22); action:SetPoint("TOPRIGHT", -18, -56)

local plot, openedAt

-- a neighbor's house: keep it on the Neighborhood window's Favorites
local favorite = CreateFrame("Button", "EsteriaHousingPlotFavorite", f, "UIPanelButtonTemplate")
favorite:SetSize(100, 22); favorite:SetPoint("TOP", action, "BOTTOM", 0, -4)
favorite:SetScript("OnClick", function()
  if plot and plot.house > 0 then EsteriaHousing_SetFavorite(plot.house, plot.hood, not plot.favorite) end
end)

-- your plot: take the house off it (saved, with its rooms and indoor decor, to put on another plot)
local moveOut = CreateFrame("Button", "EsteriaHousingPlotMoveOut", f, "UIPanelButtonTemplate")
moveOut:SetSize(100, 22); moveOut:SetPoint("TOP", action, "BOTTOM", 0, -4); moveOut:SetText("Move Out")
moveOut:SetScript("OnClick", function() StaticPopup_Show("ESTERIA_HOUSING_MOVE_OUT") end)
moveOut:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
  GameTooltip:AddLine("Move Out")
  GameTooltip:AddLine("Take your house off this plot. It is saved, to put on another.", 1, 1, 1, true)
  GameTooltip:Show()
end)
moveOut:SetScript("OnLeave", function() GameTooltip:Hide() end)
StaticPopupDialogs["ESTERIA_HOUSING_MOVE_OUT"] = {
  text = "Move your house off this plot?\n\nThe house, its rooms and indoor decor are saved, and the decor in its yard goes back to storage. Buy a plot anywhere to put it there again.",
  button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_MoveOut() end,
}

-- ------------------------------------------------------------------------------------------------ your house

-- Your plot: an inset with three tabs. Each tab's widgets sit in their own frame inside it.
local house = CreateFrame("Frame", nil, f)
house:SetPoint("TOPLEFT", 14, BOX_TOP); house:SetPoint("BOTTOMRIGHT", -14, 14)
house:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
  edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
house:SetBackdropColor(0, 0, 0, 0.45); house:SetBackdropBorderColor(0.55, 0.42, 0.25)
house:Hide()
local IW = W - 28 -- the inset's width

local TABS = { { "style", "Style" }, { "place", "Placement" }, { "spots", "Roof Spots" } }
local tab = "style"
local pages, tabButtons = {}, {}
for i, t in ipairs(TABS) do
  local page = CreateFrame("Frame", nil, house)
  page:SetAllPoints()
  page:Hide()
  pages[t[1]] = page
  local b = CreateFrame("Button", "EsteriaHousingPlotFrameTab" .. i, f, "OptionsFrameTabButtonTemplate")
  b:SetID(i); b:SetText(t[2])
  if i == 1 then b:SetPoint("BOTTOMLEFT", house, "TOPLEFT", 6, -2)
  else b:SetPoint("LEFT", tabButtons[TABS[i - 1][1]], "RIGHT", -14, 0) end
  PanelTemplates_TabResize(b, 0)
  b:SetScript("OnClick", function() tab = t[1]; PlaySound("igCharacterInfoTab"); H.RedrawPlotTabs() end)
  b:Hide()
  tabButtons[t[1]] = b
end
f.numTabs = #TABS

-- a dropdown in the client's own style
local styleMenu = CreateFrame("Frame", "EsteriaHousingStyleMenu", UIParent, "UIDropDownMenuTemplate")
local function DropButton(name, parent, width, height)
  local b = CreateFrame("Button", name, parent)
  b:SetSize(width, height)
  b:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
  b:SetBackdropColor(0, 0, 0, 0.7); b:SetBackdropBorderColor(0.55, 0.42, 0.25)
  b:SetFontString(b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"))
  b:GetFontString():SetPoint("LEFT", 10, 0); b:GetFontString():SetPoint("RIGHT", -height, 0)
  b:GetFontString():SetJustifyH("LEFT")
  local arrow = b:CreateTexture(nil, "OVERLAY")
  arrow:SetTexture("Interface\\ChatFrame\\UI-ChatIcon-ScrollDown-Up"); arrow:SetSize(height - 6, height - 6)
  arrow:SetPoint("RIGHT", -3, 0)
  b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
  return b
end

-- a striped line: its label on the left
local function Line(parent, i, top, label)
  local line = CreateFrame("Frame", nil, parent)
  line:SetSize(IW - 12, PART_H); line:SetPoint("TOPLEFT", 6, top - (i - 1) * (PART_H + 2))
  local bg = line:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
  line.label = line:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
  line.label:SetPoint("LEFT", 8, 0); line.label:SetWidth(80); line.label:SetJustifyH("LEFT"); line.label:SetText(label or "")
  return line
end

-- ---------------------------------------------------------------- Style: the house style and its parts

local stylePage = pages.style
local styleLine = Line(stylePage, 1, -10, "Style")
local styleButton = DropButton("EsteriaHousingPlotStyle", styleLine, IW - 112, 22)
styleButton:SetPoint("RIGHT", -4, 0)
styleButton:SetScript("OnClick", function()
  local items = { { text = "House Style", isTitle = true, notCheckable = true } }
  for _, st in ipairs(plot and plot.styles or {}) do
    items[#items + 1] = { text = st.name, checked = st.id == plot.style,
      func = function() EsteriaHousing_SetPlotStyle(st.id) end }
  end
  EasyMenu(items, styleMenu, styleButton, 0, 0, "MENU")
end)

-- the parts: one dropdown each; a shape with colours opens them (retail names a variant "Shape - Colour")
local PART_NAMES = { [9] = "Walls", [10] = "Roof", [11] = "Door", [12] = "Windows", [14] = "Dormers", [15] = "Towers",
  [16] = "Chimneys" }
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
  local row = Line(stylePage, i + 1, -10)
  row.button = DropButton("EsteriaHousingPlotPart" .. i, row, IW - 112, 22)
  row.button:SetPoint("RIGHT", -4, 0)
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
    row:Show()
  end
  for i = #parts + 1, #partRows do partRows[i].part = nil; partRows[i]:Hide() end
end

-- ---------------------------------------------------------------- Placement

local placePage = pages.place

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

-- a keycap: retail's key art with an arrow, rotate icon or sign on it
local function Key(name, parent, icon, onClick, tip, mirror)
  local b = CreateFrame("Button", "EsteriaHousingPlot" .. name, parent)
  b:SetSize(24, 24)
  H.Atlas(b:CreateTexture(nil, "BACKGROUND"), "housing-hotkey-icon-key-9slice"):SetAllPoints()
  if H.ATLAS and H.ATLAS[icon] then
    b.icon = H.Atlas(b:CreateTexture(nil, "ARTWORK"), icon)
    b.icon:SetPoint("TOPLEFT", 4, -4); b.icon:SetPoint("BOTTOMRIGHT", -4, 4)
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
H.PlotKey = Key -- the mount window's placement uses the same keys

-- a line: its label, its keys after it, and its value at the right
local values = {}
local function PlaceRow(i, label, keys)
  local line = Line(placePage, i, -10, label)
  local last
  for k, spec in ipairs(keys) do
    local b = Key(spec[1], line, spec[2], spec[3], spec[4], spec[5])
    if last then b:SetPoint("LEFT", last, "RIGHT", 4, 0) else b:SetPoint("LEFT", 92, 0) end
    last = b
  end
  local value = line:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  value:SetPoint("RIGHT", -10, 0); value:SetJustifyH("RIGHT")
  values[i] = value
end
PlaceRow(1, "Position", {
  { "West", "housing-hotkey-icon-arrow-left", function() Nudge(0, 1, 0) end, "Move west" },
  { "North", "housing-hotkey-icon-arrow-up", function() Nudge(1, 0, 0) end, "Move north" },
  { "South", "housing-hotkey-icon-arrow-down", function() Nudge(-1, 0, 0) end, "Move south" },
  { "East", "housing-hotkey-icon-arrow-right", function() Nudge(0, -1, 0) end, "Move east" } })
PlaceRow(2, "Height", {
  { "Down", "housing-floor-arrow-down-default", function() Nudge(0, 0, 0, -1) end, "Lower the house" },
  { "Up", "housing-floor-arrow-up-default", function() Nudge(0, 0, 0, 1) end, "Raise the house" } })
PlaceRow(3, "Rotation", {
  { "TurnLeft", "decor-ability-expert-rotate-default", function() Nudge(0, 0, 15) end, "Turn left 15\194\176" },
  { "TurnRight", "decor-ability-expert-rotate-default", function() Nudge(0, 0, -15) end, "Turn right 15\194\176", true } })
PlaceRow(4, "Size", {
  { "Smaller", "-", function() Nudge(0, 0, 0, 0, -1) end, "Make the house smaller" },
  { "Bigger", "+", function() Nudge(0, 0, 0, 0, 1) end, "Make the house bigger" } })

local resetLine = Line(placePage, 5, -10, "")
local reset = CreateFrame("Button", "EsteriaHousingPlotReset", resetLine, "UIPanelButtonTemplate")
reset:SetSize(76, 22); reset:SetPoint("RIGHT", -4, 0); reset:SetText("Reset")
reset:SetScript("OnClick", function() EsteriaHousing_PlacePlotHouse(0, 0, 0, 0, 1) end)
resetLine.label:SetText("Back to the plot's spot"); resetLine.label:SetWidth(IW - 110)
resetLine.label:SetFontObject(GameFontHighlightSmall)
local hint = placePage:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("TOPLEFT", 14, -10 - 5 * (PART_H + 2) - 8); hint:SetWidth(IW - 28); hint:SetJustifyH("LEFT")
hint:SetText("You can also drag the house: open Decorate, choose Expert, and click your house.")

local function RedrawPlacement()
  values[1]:SetText(string.format("%.1f N  %.1f W", plot.dx, plot.dy))
  values[2]:SetText(string.format("%+.2f yd", plot.dz or 0))
  values[3]:SetText(string.format("%d\194\176", plot.turn))
  values[4]:SetText(string.format("x%.2f", plot.scale or 1))
end

-- ---------------------------------------------------------------- Roof Spots

-- step through the places on the walls and roof that take a dormer, tower or chimney; the handles mark the one you are
-- on, and the menu puts something there or empties it.
local spotsPage = pages.spots
local SPOT_TYPES = { { 0, "Empty" }, { 16, "Chimney", "chimney" }, { 15, "Tower", "tower" }, { 14, "Dormer", "dormer" } }
local spotIndex = 1
local spotLine = Line(spotsPage, 1, -10, "Spot")
local spotPrev = Key("SpotPrev", spotLine, "housing-hotkey-icon-arrow-left", function() spotIndex = spotIndex - 1; H.RedrawSpots() end,
  "Previous spot")
spotPrev:SetPoint("LEFT", 60, 0)
local spotNext = Key("SpotNext", spotLine, "housing-hotkey-icon-arrow-right", function() spotIndex = spotIndex + 1; H.RedrawSpots() end,
  "Next spot")
spotNext:SetPoint("LEFT", spotPrev, "RIGHT", 4, 0)
local spotLabel = spotLine:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
spotLabel:SetPoint("LEFT", spotNext, "RIGHT", 10, 0)
local spotButton = DropButton("EsteriaHousingPlotSpot", spotLine, 120, 22)
spotButton:SetPoint("RIGHT", -4, 0)
local spotHint = spotsPage:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
spotHint:SetPoint("TOPLEFT", 14, -10 - (PART_H + 2) - 8); spotHint:SetWidth(IW - 28); spotHint:SetJustifyH("LEFT")
spotHint:SetText("The arrows on your house point at the spot. Put a chimney, tower or dormer there, or leave it empty.")

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

-- the gizmo marks the spot only while the Roof Spots tab is open
function H.RedrawSpots()
  local spots = plot and plot.status == "yours" and plot.spots or {}
  local show = #spots > 0 and f:IsShown() and house:IsShown() and tab == "spots"
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

function H.RedrawPlotTabs()
  local mine = plot and plot.status == "yours" and plot.styles and #plot.styles > 0
  local hasSpots = mine and plot.spots and #plot.spots > 0
  if tab == "spots" and not hasSpots then tab = "style" end
  for i, t in ipairs(TABS) do
    local b = tabButtons[t[1]]
    if mine and (t[1] ~= "spots" or hasSpots) then b:Show() else b:Hide() end
    if t[1] == tab then PanelTemplates_SetTab(f, i) end
    if mine and t[1] == tab then pages[t[1]]:Show() else pages[t[1]]:Hide() end
  end
  H.RedrawSpots()
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
  f:SetHeight(mine and YOURS_H or SALE_H)
  -- the message: at the foot of the tabs on your plot, of the window otherwise
  message:ClearAllPoints()
  message:SetParent(mine and house or f) -- the inset draws over the window's own text
  if mine then message:SetPoint("BOTTOMLEFT", house, "BOTTOMLEFT", 12, 10) else message:SetPoint("BOTTOM", 0, 16) end
  RedrawParts(mine and plot.parts or {})
  if mine then
    local name = "Style"
    for _, st in ipairs(plot.styles) do if st.id == plot.style then name = st.name end end
    styleButton:SetText(name)
    RedrawPlacement()
  end
  if plot.status == "sale" then
    status:SetText("|cffffd100For Sale|r")
    H.Atlas(icon, "housefinder_forsale-plot-icon")
    body:SetText("An empty plot. Buying it builds a house here to furnish, expand and restyle.")
    priceLabel:Show(); price:Show()
    local afford = GetMoney() >= plot.price
    price:SetText((afford and "" or "|cffff4040") .. GetCoinTextureString(plot.price))
    action:SetText(#plot.choices > 0 and "Buy..." or "Buy Plot")
    if not afford or not plot.canBuy then action:Disable() end
    if not plot.canBuy and plot.message == "" then message:SetText("Only those who live in this neighborhood may buy here.") end
  elseif plot.status == "yours" then
    status:SetText("|cff40ff40Your Home|r")
    H.Atlas(icon, "housefinder_main-icon")
    body:SetText("Walk in through the front door.")
    action:SetText("Enter")
  elseif plot.status == "visit" then
    status:SetText("|cff80c0ffA Neighbor's Home|r")
    H.Atlas(icon, "housefinder_friend-icon-circle")
    body:SetText((plot.owner ~= "" and plot.owner .. "'s home. " or "") .. "The owner lets you visit.")
    action:SetText("Visit")
  else
    status:SetText("|cffc0c0c0Occupied|r")
    H.Atlas(icon, "housefinder_main-icon")
    body:SetText((plot.owner ~= "" and plot.owner .. " lives here." or "Someone lives here.") .. " The owner has not opened it to you.")
    action:Hide()
  end
  H.RedrawPlotTabs()
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
  if self:IsShown() then Redraw() end
end)
f:SetScript("OnShow", function() Redraw() end)
