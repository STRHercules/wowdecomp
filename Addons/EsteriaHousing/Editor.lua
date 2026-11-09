-- Esteria housing: the House Editor, drawn with retail's housing UI art (Atlas.lua, textures as retail/<FDID>.blp).
-- Inside a house: the controls bar at the top (inspect, house info, decorate mode, settings, exit) and the decor budget
-- at the top right. Decorate mode opens the decor chest on the left (Storage: what you own; Catalog: what you can buy,
-- into the cart at the bottom right) and the ability bar at the bottom: 1 Basic (place from storage), 2 Expert (select
-- and move, turn, raise), 3 Customize (style the room you stand in), 4 Cleanup (click decor to store it), 5 Layout.
-- Everything goes through the Housing extension and the server validates every edit; the UI redraws from its answers.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local TURN, ZSTEP, PICK = 15, 0.1, 2.5
local COLS, ROWS, TILE, GAP = 3, 5, 78, 6
local RESULT = {
  [15] = "Empty this shop's stock before removing it: take its items back from its Shop tab.",
  [1] = "Housing is disabled on this realm.", [2] = "You are not in a house.", [3] = "Only the owner can edit this house.",
  [4] = "Duplicate request ignored.", [5] = "The house changed; resynced. Try again.", [6] = "Unknown decor.",
  [7] = "You have none of that decor left.", [8] = "That decor is no longer placed.", [9] = "That spot is outside your rooms.",
  [10] = "Invalid position or rotation.", [11] = "The house is full.", [12] = "Too fast; slow down.",
  [13] = "The server did not answer.", [14] = "Unknown edit.",
}
-- our categories and the retail category icon for each
local CATEGORIES = { { nil, "All", "all" }, { 1, "Furniture", "furnishings" }, { 2, "Accents", "accents" },
  { 3, "Lighting", "lighting" }, { 4, "Nature", "plants" }, { 5, "Structure", "structural" }, { 0, "Misc", "misc" }, { 6, "Shops", "interactive" },
  { 7, "Utility", "utility" }, { 8, "Collections", "featured" }, { 9, "Services", "storage" } }

-- shop decor shows the picture of the retail piece it borrows its model from
H.SHOP_MODEL = { [40002] = 4428, [40003] = 4422, [40004] = 16098, [40005] = 15061, [40006] = 26483, [40007] = 3985,
  -- collections (housing_collections_world.sql)
  [40202] = 3836, [40203] = 27617, [40204] = 21106, [40205] = 2537, [40206] = 14346, [40207] = 7607,
  [40209] = 11895, [40210] = 3832, [40211] = 11881, [40212] = 15064, [40213] = 15290, [40214] = 12246, [40215] = 23548,
  [40216] = 23549, [40265] = 2094, [40266] = 2094, [40267] = 1698, [40268] = 12194, [40269] = 14839, [40270] = 14838,
  [40271] = 26479, [40272] = 498, [40273] = 4026,
  -- characters (merchant, mannequin, service NPCs) show a statue: a bare character model draws white
  [40001] = 5564, [40208] = 5564, [40217] = 5564, [40218] = 5564, [40224] = 5564, [40226] = 5564, [40251] = 5564, [40252] = 5564, [40253] = 5564, [40254] = 5564, [40255] = 5564, [40256] = 5564, [40257] = 5564, [40258] = 5564, [40259] = 5564, [40260] = 5564, [40261] = 5564, [40262] = 5564, [40263] = 5564, [40264] = 5564,
  -- household services (housing_household_world.sql)
  [40219] = 1322, [40220] = 4482, [40221] = 15460, [40222] = 15741, [40223] = 26374, [40225] = 2226, [40227] = 1280,
  [40228] = 9243, [40229] = 2437, [40230] = 11371, [40231] = 11375, [40232] = 11274, [40233] = 11374, [40234] = 4450,
  [40235] = 8236, [40236] = 11378, [40237] = 14590, [40238] = 14631, [40239] = 5888 }
-- pieces drawn at a share of their placed size (gameobject_template.size, the server's BaseScale); the ghost matches.
H.BASE_SCALE = { [40205] = 0.4, [40266] = 0.6, [40267] = 0.7, [40268] = 0.45 }
-- characters preview as the statue; it is 11.9 yd tall, so draw it at about a person's height
for decor, model in pairs(H.SHOP_MODEL) do if model == 5564 then H.BASE_SCALE[decor] = 0.17 end end
local catalog, state, names, fdids = {}, nil, {}, {}

-- the placement ghost for a decor, turned to yaw degrees and at the piece's base scale
local function Ghost(decorId, yaw)
  if not EsteriaHousing_SetGhost then return end
  local fdid = decorId and fdids[decorId] or nil
  local base = decorId and H.BASE_SCALE[decorId]
  if fdid and base then
    local half = math.rad(yaw or 0) / 2
    EsteriaHousing_SetGhost(fdid, 0, 0, math.sin(half), math.cos(half), base)
  else
    EsteriaHousing_SetGhost(fdid, yaw)
  end
end
local editing         -- decorate mode on
local ability = 1     -- 1 basic, 2 expert, 3 customize, 4 cleanup, 5 layout
local tool            -- nil, "place" (holding storage decor), "move" (holding a placed one), "select" or "cleanup"
local holding         -- { decorId, placedId, yaw }
local selected        -- placedId
local tab, category, search, page = "storage", nil, "", 0
local cart = {}       -- { { decorId, count } }

function H.Toast(text) UIErrorsFrame:AddMessage(text, 1, 0.2, 0.2, 1) end
local Toast = H.Toast

-- A retail atlas element on a texture; sized to the element when w is true, or to w x h.
function H.Atlas(tex, name, w, h)
  local a = H.ATLAS and H.ATLAS[name]
  if not a then return tex end
  tex:SetTexture("retail/" .. a[1] .. ".blp")
  tex:SetTexCoord(a[4], a[5], a[6], a[7])
  if w == true then tex:SetSize(a[2], a[3]) elseif w then tex:SetSize(w, h or w) end
  return tex
end

-- A button state texture (Normal, Pushed, Highlight, Disabled) from a retail atlas element: the button takes a file
-- path, then its own texture gets the element's coordinates.
function H.ButtonTexture(b, state, name)
  local a = H.ATLAS and H.ATLAS[name]
  if not a then return end
  b["Set" .. state .. "Texture"](b, "retail/" .. a[1] .. ".blp")
  local t = b["Get" .. state .. "Texture"](b)
  if t then t:SetTexCoord(a[4], a[5], a[6], a[7]); t:SetAllPoints() end
  return t
end

-- "|T...|t" for an atlas element inside text
function H.Icon(name, size)
  local a = H.ATLAS and H.ATLAS[name]
  if not a then return "" end
  return string.format("|Tretail/%d.blp:%d:%d:0:0:1024:1024:%d:%d:%d:%d|t", a[1], size, size, a[4] * 1024, a[5] * 1024,
    a[6] * 1024, a[7] * 1024)
end

local function Drag(f)
  -- every housing panel drags; the client keeps a named, user-placed frame where it was left (layout-local.txt)
  f:SetMovable(true); f:SetClampedToScreen(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
  f:SetScript("OnDragStart", function(self) self:StartMoving() end)
  f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing(); self:SetUserPlaced(true) end)
end

-- A panel in retail's housing style: the basic panel's dark background and its gradient header.
function H.Panel(name, w, h)
  local f = CreateFrame("Frame", name, UIParent)
  f:SetSize(w, h)
  f:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
    insets = { left = 3, right = 3, top = 3, bottom = 3 } })
  f:SetBackdropBorderColor(0.55, 0.42, 0.25)
  local bg = H.Atlas(f:CreateTexture(nil, "BACKGROUND"), "housing-basic-panel-background")
  bg:SetPoint("TOPLEFT", 3, -3); bg:SetPoint("BOTTOMRIGHT", -3, 3)
  local header = H.Atlas(f:CreateTexture(nil, "BORDER"), "housing-basic-panel-gradient-header-bg")
  header:SetPoint("TOPLEFT", 3, -3); header:SetPoint("TOPRIGHT", -3, -3); header:SetHeight(40)
  Drag(f)
  f:Hide()
  return f
end

function H.Button(parent, text, w, h)
  local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
  b:SetSize(w, h or 22); b:SetText(text)
  return b
end

-- A button drawn from retail atlas states: its default, active (SetOn) and pressed elements.
local function AtlasButton(parent, name, w, h, default, active, pressed)
  local b = CreateFrame("Button", name, parent)
  b:SetSize(w, h)
  b.normal = H.Atlas(b:CreateTexture(nil, "ARTWORK"), default); b.normal:SetAllPoints()
  b.on = active
  b.off = default
  H.ButtonTexture(b, "Pushed", pressed or default)
  local glow = b:CreateTexture(nil, "HIGHLIGHT"); glow:SetAllPoints()
  H.Atlas(glow, active or default); glow:SetBlendMode("ADD"); glow:SetAlpha(0.35)
  function b:SetOn(on) H.Atlas(self.normal, on and self.on or self.off) end
  return b
end

local function Placed(id)
  if state then for _, d in ipairs(state.decor) do if d.placedId == id then return d end end end
end

local function Owner() return state and state.inHouse and state.isOwner end

local function Entry(decorId)
  for _, e in ipairs(catalog) do if e.decorId == decorId then return e end end
end

-- ------------------------------------------------------------------------------------------------ top controls

local controls = CreateFrame("Frame", "EsteriaHousingControls", UIParent)
controls:SetSize(310, 39)
controls:SetPoint("TOP", 0, -10)
Drag(controls)
controls:Hide()
local controlsBg = H.Atlas(controls:CreateTexture(nil, "BACKGROUND"), "controls-frame"); controlsBg:SetAllPoints()

local function Control(key, x, size, onClick, tip)
  local b = AtlasButton(controls, "EsteriaHousingControl" .. key, size, size, "decor-controls-" .. key .. "-default",
    "decor-controls-" .. key .. "-active", "decor-controls-" .. key .. "-pressed")
  b:SetPoint("CENTER", controls, "CENTER", x, 0)
  b:SetScript("OnClick", onClick)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_BOTTOM"); GameTooltip:AddLine(tip); GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  return b
end

local decorate = AtlasButton(controls, "EsteriaHousingDecorate", 68, 68, "decor-controls-decoratemode-inactive",
  "decor-controls-decoratemode-active", "decor-controls-decoratemode-pressed")
decorate:SetPoint("CENTER", controls, "CENTER", 0, -4)
decorate:SetFrameLevel(controls:GetFrameLevel() + 2)
decorate:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_BOTTOM"); GameTooltip:AddLine(editing and "Stop Decorating" or "Decorate"); GameTooltip:Show()
end)
decorate:SetScript("OnLeave", function() GameTooltip:Hide() end)

local inspect = Control("inspect", -100, 30, function() H.SetEditing(true); H.SetAbility(2) end, "Select decor (Expert)")
local houseinfo = Control("houseinfo", -64, 30, function() H.ToggleUpgrades() end, "House")
houseinfo:SetScript("OnEnter", function(self)
  local layout = EsteriaHousing_GetLayout and EsteriaHousing_GetLayout()
  GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
  GameTooltip:AddLine("Your House")
  if state then GameTooltip:AddDoubleLine("Decor placed", state.placed .. " / " .. state.maxDecor, 1, 1, 1, 1, 0.82, 0) end
  if layout then GameTooltip:AddDoubleLine("Rooms", #layout.rooms .. " / " .. layout.maxRooms, 1, 1, 1, 1, 0.82, 0) end
  if layout and layout.maxFloors then GameTooltip:AddDoubleLine("Floors", layout.maxFloors, 1, 1, 1, 1, 0.82, 0) end
  GameTooltip:AddLine("Click for upgrades.", 0.5, 0.8, 1)
  GameTooltip:Show()
end)
local settingsMenu = CreateFrame("Frame", "EsteriaHousingSettingsMenu", UIParent, "UIDropDownMenuTemplate")
local settings = Control("settings", 64, 30, function()
  EasyMenu({ { text = "Housing", isTitle = true, notCheckable = true },
    { text = "Who can visit...", notCheckable = true, keepShownOnClick = true, func = function() H.ShowAccessMenu() end },
    { text = "Reset panel positions", notCheckable = true, func = function() H.ResetPositions() end } },
    settingsMenu, "cursor", 0, 0, "MENU")
end, "Settings")
local exit = Control("exit", 100, 30, function() EsteriaHousing_Leave() end, "Leave House")

-- the decor budget, top right
local budget = CreateFrame("Frame", "EsteriaHousingBudget", UIParent)
budget:SetSize(120, 30)
budget:SetPoint("TOPRIGHT", -200, -12)
Drag(budget)
budget:Hide()
local budgetIcon = H.Atlas(budget:CreateTexture(nil, "ARTWORK"), "house-decor-budget-icon", 26, 26)
budgetIcon:SetPoint("LEFT")
local budgetText = budget:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
budgetText:SetPoint("LEFT", budgetIcon, "RIGHT", 6, 0)
budget:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_BOTTOMLEFT"); GameTooltip:AddLine("Decor Budget")
  GameTooltip:AddLine("Decor placed in this house and how much it can hold.", 1, 1, 1, true); GameTooltip:Show()
end)
budget:SetScript("OnLeave", function() GameTooltip:Hide() end)

-- ------------------------------------------------------------------------------------------------ upgrades

-- More floors and a larger decor budget, bought with gold (the server sets the prices and the caps).
local upgrades = H.Panel("EsteriaHousingUpgrades", 300, 196)
upgrades:SetPoint("TOP", 0, -70)
local upgradesTitle = upgrades:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
upgradesTitle:SetPoint("TOP", 0, -10); upgradesTitle:SetText("House Upgrades")
local upgradesClose = CreateFrame("Button", nil, upgrades, "UIPanelCloseButton")
upgradesClose:SetPoint("TOPRIGHT", 2, 2)
tinsert(UISpecialFrames, "EsteriaHousingUpgrades")
StaticPopupDialogs["ESTERIA_HOUSING_UPGRADE"] = {
  text = "%s", button1 = YES, button2 = NO, timeout = 0, hideOnEscape = true,
  OnAccept = function(self, what) EsteriaHousing_Upgrade(what) end,
}
local function UpgradeRow(y, icon, what)
  local row = CreateFrame("Frame", nil, upgrades)
  row:SetSize(276, 40); row:SetPoint("TOP", 0, y)
  row.icon = H.Atlas(row:CreateTexture(nil, "ARTWORK"), icon, 28, 28); row.icon:SetPoint("LEFT")
  row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight"); row.text:SetPoint("LEFT", row.icon, "RIGHT", 8, 0)
  row.text:SetJustifyH("LEFT"); row.text:SetWidth(130)
  row.buy = H.Button(row, "", 110, 22); row.buy:SetPoint("RIGHT")
  row.buy:SetScript("OnClick", function(self)
    StaticPopup_Show("ESTERIA_HOUSING_UPGRADE", self.question, nil, what) -- data: which upgrade, handed to OnAccept
  end)
  return row
end
local floorRow = UpgradeRow(-44, "house-room-limit-icon", "floors")
local decorRow = UpgradeRow(-90, "house-decor-budget-icon", "decor")
local roomRow = UpgradeRow(-136, "house-room-limit-icon", "rooms")

local function SetUpgrade(row, label, have, cap, price, gain, question)
  row.text:SetText(string.format("%s\n|cffffd100%d|r / %d", label, have, cap))
  if price and price > 0 then
    row.buy:SetText(gain .. "  " .. GetCoinTextureString(price))
    row.buy.question = string.format(question, GetCoinTextureString(price))
    if GetMoney() >= price then row.buy:Enable() else row.buy:Disable() end
  else
    row.buy:SetText("Maxed"); row.buy:Disable()
  end
end

function H.RedrawUpgrades()
  if not upgrades:IsShown() then return end
  local layout = EsteriaHousing_GetLayout()
  if not (layout and state) then return end
  SetUpgrade(floorRow, "Floors", layout.maxFloors, 15, layout.floorPrice, "+1",
    "Add a floor to your house for %s?")
  SetUpgrade(decorRow, "Decor Budget", state.maxDecor, layout.decorCap, layout.decorPrice, "+" .. layout.decorStep,
    "Raise your decor budget by " .. layout.decorStep .. " for %s?")
  SetUpgrade(roomRow, "Room Limit", layout.maxRooms, layout.roomCap or 64, layout.roomPrice, "+" .. (layout.roomStep or 4),
    "Raise your room limit by " .. (layout.roomStep or 4) .. " for %s?")
end
upgrades:SetScript("OnShow", H.RedrawUpgrades)
upgrades:RegisterEvent("ESTERIA_HOUSING_LAYOUT")
upgrades:RegisterEvent("PLAYER_MONEY")
upgrades:SetScript("OnEvent", H.RedrawUpgrades)

function H.ToggleUpgrades()
  if upgrades:IsShown() then upgrades:Hide() else upgrades:Show() end
end

-- ------------------------------------------------------------------------------------------------ decor chest

local chest = CreateFrame("Frame", "EsteriaHousingDecor", UIParent)
-- sized to its contents: a 44 px header, the tile grid, and the pager row under it
local CHEST_W, CHEST_H = 64 + COLS * (TILE + GAP) + 8, 50 + ROWS * (TILE + GAP) + 30
chest:SetSize(CHEST_W, CHEST_H)
chest:SetPoint("LEFT", 20, 30)
Drag(chest)
chest:Hide()
chest:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 14,
  insets = { left = 3, right = 3, top = 3, bottom = 3 } })
chest:SetBackdropBorderColor(0.55, 0.42, 0.25)
local chestBg = chest:CreateTexture(nil, "BACKGROUND"); chestBg:SetPoint("TOPLEFT", 3, -3); chestBg:SetPoint("BOTTOMRIGHT", -3, 3)
chestBg:SetTexture(0.06, 0.045, 0.03, 0.95)
local chestPanel = H.Atlas(chest:CreateTexture(nil, "BACKGROUND", nil, 1), "housing-basic-panel-background")
chestPanel:SetAllPoints(chestBg); chestPanel:SetAlpha(0.6)
local chestHeader = H.Atlas(chest:CreateTexture(nil, "BORDER"), "house-chest-header-bg")
chestHeader:SetPoint("TOPLEFT"); chestHeader:SetPoint("TOPRIGHT"); chestHeader:SetHeight(44)
local corbel = H.Atlas(chest:CreateTexture(nil, "OVERLAY"), "house-chest-corbel-topright", 44, 29)
corbel:SetPoint("TOPRIGHT", 1, 1)
local nav = H.Atlas(chest:CreateTexture(nil, "BORDER"), "house-chest-nav-bg_primary")
nav:SetPoint("TOPLEFT", 3, -42); nav:SetPoint("BOTTOMLEFT", 3, 3); nav:SetWidth(52) -- inside the border, header to foot
local navTop = H.Atlas(chest:CreateTexture(nil, "ARTWORK"), "house-chest-nav-top-corbels", 52, 21)
navTop:SetPoint("TOPLEFT", 3, -40)

local searchBox = CreateFrame("EditBox", "EsteriaHousingDecorSearch", chest, "InputBoxTemplate")
searchBox:SetSize(170, 20); searchBox:SetPoint("TOPLEFT", 20, -12); searchBox:SetAutoFocus(false)
searchBox:SetScript("OnTextChanged", function(self) search = self:GetText():lower(); page = 0; H.RedrawDecor() end)
searchBox:SetScript("OnEscapePressed", searchBox.ClearFocus)
searchBox:SetScript("OnEnterPressed", searchBox.ClearFocus)
local searchHint = searchBox:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
searchHint:SetPoint("LEFT", 4, 0); searchHint:SetText("Search")
searchBox:SetScript("OnEditFocusGained", function() searchHint:Hide() end)
searchBox:SetScript("OnEditFocusLost", function(self) if self:GetText() == "" then searchHint:Show() end end)
local countText = chest:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
countText:SetPoint("LEFT", searchBox, "RIGHT", 10, 0)

-- the category column
local tabs = {}
for i, c in ipairs(CATEGORIES) do
  local icon = "category-icons_" .. c[3]
  local b = AtlasButton(chest, nil, 40, 40, icon .. "_inactive", icon .. "_active", icon .. "_pressed")
  b:SetPoint("TOP", chest, "TOPLEFT", 27, -48 - (i - 1) * 41) -- eleven categories fit the chest's height at 41
  b.category = c[1]
  b:SetScript("OnClick", function() category = c[1]; page = 0; H.RedrawDecor() end)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:AddLine(c[2]); GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  tabs[i] = b
end

local function FitModel(model)
  model:SetScript("OnUpdate", function(self)
    if not RetailModelReportLoaded then self:SetScript("OnUpdate", nil); return end
    if RetailModelReportLoaded(self) then self:SetScript("OnUpdate", nil) end
  end)
end

local function Coins(copper) return GetCoinTextureString(copper) end

local tiles = {}
for i = 1, COLS * ROWS do
  local t = CreateFrame("Button", nil, chest)
  t:SetSize(TILE, TILE)
  t:SetPoint("TOPLEFT", 64 + ((i - 1) % COLS) * (TILE + GAP), -50 - math.floor((i - 1) / COLS) * (TILE + GAP))
  t.frame = H.Atlas(t:CreateTexture(nil, "BORDER"), "house-chest-list-item-default"); t.frame:SetAllPoints()
  local hl = H.ButtonTexture(t, "Highlight", "house-chest-list-item-active")
  if hl then hl:SetBlendMode("ADD") end
  t.model = CreateFrame("Model", nil, t)
  t.model:SetPoint("TOPLEFT", 6, -6); t.model:SetPoint("BOTTOMRIGHT", -6, 14)
  t.thumb = t:CreateTexture(nil, "ARTWORK") -- retail's picture of the decor (Thumbnails.lua); the 3D model when none
  t.thumb:SetPoint("TOPLEFT", 6, -6); t.thumb:SetPoint("BOTTOMRIGHT", -6, 14)
  t.count = t:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
  t.count:SetPoint("BOTTOMRIGHT", -6, 5)
  t:SetScript("OnClick", function(self)
    local e = self.entry
    if not e then return end
    if tab == "catalog" then return H.AddToCart(e.decorId) end
    if e.available < 1 then return Toast("You have none of that left to place.") end
    H.SetAbility(1)
    H.Hold({ decorId = e.decorId })
  end)
  t:SetScript("OnEnter", function(self)
    local e = self.entry
    if not e then return end
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    GameTooltip:AddLine(e.name)
    GameTooltip:AddLine(string.format("In storage: %d    Placed: %d", e.available, e.placed), 1, 1, 1)
    if tab == "catalog" then
      GameTooltip:AddLine("Price: " .. Coins(e.price or 0), 1, 1, 1)
      GameTooltip:AddLine("Click to add it to your cart.", 0.5, 0.8, 1)
    else
      GameTooltip:AddLine(H.Icon("housing-hotkey-icon-leftclick", 14) .. " Pick it up, then click the floor to place it.", 0.5, 0.8, 1)
    end
    GameTooltip:Show()
  end)
  t:SetScript("OnLeave", function() GameTooltip:Hide() end)
  tiles[i] = t
end
chest:EnableMouseWheel(true)
chest:SetScript("OnMouseWheel", function(self, delta)
  page = math.max(0, page - delta)
  H.RedrawDecor()
end)

local pageText = chest:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
pageText:SetPoint("BOTTOM", 28, 10)
local prev = H.Button(chest, "<", 24, 18); prev:SetPoint("RIGHT", pageText, "LEFT", -8, 0)
local nextButton = H.Button(chest, ">", 24, 18); nextButton:SetPoint("LEFT", pageText, "RIGHT", 8, 0)
prev:SetScript("OnClick", function() page = math.max(0, page - 1); H.RedrawDecor() end)
nextButton:SetScript("OnClick", function() page = page + 1; H.RedrawDecor() end)

-- Storage | Catalog along the bottom
local function TabButton(text, key, x)
  local b = CreateFrame("Button", nil, chest)
  b:SetSize(100, 26); b:SetPoint("TOPLEFT", chest, "BOTTOMLEFT", x, 3) -- below the panel, like retail's tabs
  b:SetFrameLevel(chest:GetFrameLevel() - 1 >= 0 and chest:GetFrameLevel() - 1 or 0)
  b.bg = H.Atlas(b:CreateTexture(nil, "BACKGROUND"), "housing-woodsign"); b.bg:SetAllPoints()
  b.text = b:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall"); b.text:SetPoint("CENTER", 0, 1); b.text:SetText(text)
  local hl = H.ButtonTexture(b, "Highlight", "house-chest-active-nav_selected-bg-glow")
  if hl then hl:SetBlendMode("ADD") end
  b:SetScript("OnClick", function() tab = key; page = 0; H.RedrawDecor() end)
  b.key = key
  return b
end
local storageTab = TabButton("Storage", "storage", 62)
local catalogTab = TabButton("Catalog", "catalog", 170)

local function Filtered()
  local list = {}
  for _, e in ipairs(catalog) do
    if (tab == "catalog" and e.category ~= 6 or tab ~= "catalog" and (e.available > 0 or e.placed > 0)) and (category == nil or e.category == category) -- shops are free: storage only
      and (search == "" or e.name:lower():find(search, 1, true)) then
      list[#list + 1] = e
    end
  end
  return list
end

function H.RedrawDecor()
  if not chest:IsShown() then return end
  local list = Filtered()
  local per = COLS * ROWS
  page = math.max(0, math.min(page, math.ceil(#list / per) - 1))
  for i, t in ipairs(tiles) do
    local e = list[page * per + i]
    t.entry = e
    if e then
      if tab == "catalog" then
        t.count:SetText(Coins(e.price or 0)); t:SetAlpha(1)
      else
        t.count:SetText(e.available); t:SetAlpha(e.available > 0 and 1 or 0.45)
      end
      local thumb = H.THUMBNAILS and H.THUMBNAILS[H.SHOP_MODEL[e.decorId] or e.decorId]
      if thumb then
        t.thumb:SetTexture("retail/" .. thumb[1] .. ".blp"); t.thumb:SetTexCoord(0, thumb[2], 0, thumb[2])
        t.thumb:Show(); t.model:Hide()
      elseif e.fileDataId >= 2147483648 then -- a WMO decor (high bit) without a picture: no M2 to preview
        t.thumb:Hide(); t.model:Hide()
      else
        t.thumb:Hide(); t.model:Show()
        if t.fdid ~= e.fileDataId then
          t.fdid = e.fileDataId
          t.model:ClearModel()
          t.model:SetModel("retail/" .. e.fileDataId .. ".m2")
          FitModel(t.model)
        end
      end
      t:Show()
    else
      t:Hide()
    end
  end
  for _, b in ipairs(tabs) do b:SetOn(b.category == category) end
  for _, b in ipairs({ storageTab, catalogTab }) do
    if b.key == tab then b.text:SetFontObject("GameFontHighlightSmall"); b:SetAlpha(1)
    else b.text:SetFontObject("GameFontNormalSmall"); b:SetAlpha(0.7) end
  end
  pageText:SetText(#list > 0 and string.format("%d / %d", page + 1, math.ceil(#list / per))
    or (tab == "storage" and "Nothing in storage. Try the Catalog." or "No decor matches."))
  countText:SetText(#list .. " items")
end
chest:SetScript("OnShow", H.RedrawDecor)

-- ------------------------------------------------------------------------------------------------ cart

local cartFrame = H.Panel("EsteriaHousingCart", 260, 120)
cartFrame:SetPoint("BOTTOMRIGHT", -40, 160)
local cartTitle = cartFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
cartTitle:SetPoint("TOPLEFT", 14, -12)
local cartRows = {}
local trash = H.Button(cartFrame, "X", 34, 26)
trash:SetPoint("BOTTOMLEFT", 12, 10)
trash:SetScript("OnClick", function() cart = {}; H.RedrawCart() end)
trash:SetScript("OnEnter", function(self) GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine("Empty the cart"); GameTooltip:Show() end)
trash:SetScript("OnLeave", function() GameTooltip:Hide() end)
local purchase = H.Button(cartFrame, "", 190, 26)
purchase:SetPoint("BOTTOMRIGHT", -12, 10)

local function CartTotal()
  local total = 0
  for _, c in ipairs(cart) do
    local e = Entry(c.decorId)
    total = total + (e and e.price or 0) * c.count
  end
  return total
end

function H.AddToCart(decorId)
  for _, c in ipairs(cart) do
    if c.decorId == decorId then c.count = c.count + 1; return H.RedrawCart() end
  end
  cart[#cart + 1] = { decorId = decorId, count = 1 }
  H.RedrawCart()
end

function H.RedrawCart()
  for _, r in ipairs(cartRows) do r:Hide() end
  if #cart == 0 or not editing then return cartFrame:Hide() end
  local items = 0
  for i, c in ipairs(cart) do
    items = items + c.count
    local r = cartRows[i]
    if not r then
      r = CreateFrame("Button", nil, cartFrame)
      r:SetSize(236, 18)
      r.name = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); r.name:SetPoint("LEFT"); r.name:SetWidth(150)
      r.name:SetJustifyH("LEFT")
      r.price = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); r.price:SetPoint("RIGHT")
      r:SetScript("OnClick", function(self) -- a click takes one off
        local item = cart[self.index]
        item.count = item.count - 1
        if item.count < 1 then table.remove(cart, self.index) end
        H.RedrawCart()
      end)
      cartRows[i] = r
    end
    local e = Entry(c.decorId)
    r.index = i
    r:SetPoint("TOPLEFT", 12, -38 - (i - 1) * 20)
    r.name:SetText((e and e.name or "Decor") .. (c.count > 1 and (" x" .. c.count) or ""))
    r.price:SetText(Coins((e and e.price or 0) * c.count))
    r:Show()
  end
  cartTitle:SetText(string.format("Cart Preview (%d)", items))
  cartFrame:SetHeight(86 + #cart * 20)
  local total = CartTotal()
  purchase:SetText((GetMoney() < total and "|cffff4040" or "") .. "Buy  " .. Coins(total))
  if GetMoney() < total then purchase:Disable() else purchase:Enable() end
  cartFrame:Show()
end

purchase:SetScript("OnClick", function()
  for _, c in ipairs(cart) do EsteriaHousing_BuyDecor(c.decorId, c.count) end
  cart = {}
  tab = "storage"
  H.RedrawCart()
  H.RedrawDecor()
end)

-- ------------------------------------------------------------------------------------------------ ability bar

local abilityBar = CreateFrame("Frame", "EsteriaHousingAbilities", UIParent)
abilityBar:SetSize(5 * 52 + 70, 60)
abilityBar:SetPoint("BOTTOM", 0, 120)
Drag(abilityBar)
abilityBar:Hide()
local barLeft = H.Atlas(abilityBar:CreateTexture(nil, "BACKGROUND"), "decor-abilitybar-bookend-left", 59, 59)
barLeft:SetPoint("LEFT", -24, 0)
local barRight = H.Atlas(abilityBar:CreateTexture(nil, "BACKGROUND"), "decor-abilitybar-bookend-right", 59, 59)
barRight:SetPoint("RIGHT", 24, 0)
local barMid = H.Atlas(abilityBar:CreateTexture(nil, "BACKGROUND"), "decor-abilitybar-bg-horz")
barMid:SetPoint("LEFT", barLeft, "RIGHT", -1, 0); barMid:SetPoint("RIGHT", barRight, "LEFT", 1, 0); barMid:SetHeight(59)

local ABILITIES = { { "move", "Basic", "Place decor from your storage." },
  { "expertmode", "Expert", "Select placed decor to move, turn or raise it." },
  { "alterations", "Customize", "Style the room you are standing in." },
  { "cleanup", "Cleanup", "Click decor to put it back in storage." },
  { "layoutmode", "Layout", "Build and remove rooms." } }
local abilityButtons = {}
for i, a in ipairs(ABILITIES) do
  local b = AtlasButton(abilityBar, "EsteriaHousingAbility" .. i, 46, 46, "decor-ability-" .. a[1] .. "-default",
    "decor-ability-" .. a[1] .. "-active", "decor-ability-" .. a[1] .. "-pressed")
  b:SetPoint("LEFT", 35 + (i - 1) * 52 + (i == 5 and 8 or 0), 0)
  local key = b:CreateFontString(nil, "OVERLAY", "NumberFontNormalSmall"); key:SetPoint("TOPLEFT", 2, -2); key:SetText(i)
  b:SetScript("OnClick", function() H.SetAbility(i) end)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(a[2] .. "  |cffa0a0a0(" .. i .. ")|r")
    GameTooltip:AddLine(a[3], 1, 1, 1, true); GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  abilityButtons[i] = b
end
local divider = H.Atlas(abilityBar:CreateTexture(nil, "ARTWORK"), "decor-abilitybar-divider", 10, 46)

-- beside the bar: snapping onto surfaces (retail's Snap), undo and redo
local snapOn = true
local snap = AtlasButton(abilityBar, "EsteriaHousingSnap", 34, 34, "decor-ability-snap-default", "decor-ability-snap-active",
  "decor-ability-snap-pressed")
snap:SetPoint("LEFT", abilityBar, "RIGHT", 34, 8)
snap:SetOn(snapOn)
snap:SetScript("OnClick", function(self)
  snapOn = not snapOn
  self:SetOn(snapOn)
  if EsteriaHousing_SetSurfaceSnap then EsteriaHousing_SetSurfaceSnap(snapOn) end
end)
snap:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine("Snap to Surfaces")
  GameTooltip:AddLine(snapOn and "On: decor lands on tables, shelves and other decor." or "Off: decor lands on floors only.",
    1, 1, 1, true)
  GameTooltip:Show()
end)
snap:SetScript("OnLeave", function() GameTooltip:Hide() end)
local function SmallButton(name, text, tip, onClick)
  local b = CreateFrame("Button", name, abilityBar, "UIPanelButtonTemplate")
  b:SetSize(46, 18); b:SetText(text)
  b:SetScript("OnClick", onClick)
  b:SetScript("OnEnter", function(self) GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(tip); GameTooltip:Show() end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  return b
end
local undoButton = SmallButton("EsteriaHousingUndoButton", "Undo", "Undo (Ctrl+Z)", function() H.Undo() end)
undoButton:SetPoint("LEFT", snap, "RIGHT", 6, 9)
local redoButton = SmallButton("EsteriaHousingRedoButton", "Redo", "Redo (Ctrl+Y)", function() H.Redo() end)
redoButton:SetPoint("TOP", undoButton, "BOTTOM", 0, -2)
divider:SetPoint("LEFT", abilityButtons[5], "LEFT", -11, 0)

-- ------------------------------------------------------------------------------------------------ undo, redo

-- Each edit you make, as what it was and what it became; undo sends the reverse edit and redo sends it again. A piece
-- put back from storage gets a new placedId, so the history is renumbered when its ack arrives.
local undo, redo = {}, {}
local placeWaiters = {} -- callbacks for the next successful place acks, oldest first
local historyKey        -- the house (and indoors or out) the history belongs to
local HISTORY_MAX = 50

local function TransformOf(d)
  local h = math.rad(d.yaw or 0)
  return { d.x, d.y, d.z, d.qx or 0, d.qy or 0, d.qz or math.sin(h / 2), d.qw or math.cos(h / 2), d.scale or 1 }
end

local function Remap(old, new)
  for _, stack in ipairs({ undo, redo }) do
    for _, e in ipairs(stack) do if e.id == old then e.id = new end end
  end
  if selected == old then selected = new end
end

local function Push(stack, e)
  stack[#stack + 1] = e
  if #stack > HISTORY_MAX then table.remove(stack, 1) end
end

-- Places decor; when the server says where it went, then(placedId) runs (a restored piece's full transform, renumbering).
local function RawPlace(decorId, t, after)
  local yaw = math.deg(2 * math.atan2(t[6] or 0, t[7] or 1))
  if not EsteriaHousing_Place(decorId, t[1], t[2], t[3], yaw) then return false end
  placeWaiters[#placeWaiters + 1] = function(placedId)
    local tilted = math.abs(t[4] or 0) > 1e-4 or math.abs(t[5] or 0) > 1e-4 or math.abs((t[8] or 1) - 1) > 1e-4
    if tilted then EsteriaHousing_Move(placedId, unpack(t)) end
    if after then after(placedId) end
  end
  return true
end

-- The editor's three edits, recorded for undo.
function H.Move(placedId, ...)
  local d = Placed(placedId)
  local ok = EsteriaHousing_Move(placedId, ...)
  if ok and d then
    local args = { ... }
    local after = #args >= 8 and args or { args[1], args[2], args[3], 0, 0, math.sin(math.rad(args[4]) / 2),
      math.cos(math.rad(args[4]) / 2), 1 }
    Push(undo, { kind = "move", id = placedId, before = TransformOf(d), after = after }); wipe(redo)
  end
  return ok
end

function H.Place(decorId, x, y, z, yaw, transform)
  local t = transform or { x, y, z, 0, 0, math.sin(math.rad(yaw) / 2), math.cos(math.rad(yaw) / 2), 1 }
  t[1], t[2], t[3] = x, y, z
  local entry = { kind = "place", decorId = decorId, at = t }
  return RawPlace(decorId, t, function(placedId)
    entry.id = placedId
    Push(undo, entry); wipe(redo)
  end)
end

function H.Remove(placedId)
  local d = Placed(placedId)
  local ok = EsteriaHousing_Remove(placedId)
  if ok and d then Push(undo, { kind = "remove", id = placedId, decorId = d.decorId, before = TransformOf(d) }); wipe(redo) end
  return ok
end

local function Replay(from, to, backwards)
  local e = table.remove(from)
  if not e then return Toast(backwards and "Nothing to undo." or "Nothing to redo.") end
  local function done() Push(to, e) end
  if e.kind == "move" then
    if EsteriaHousing_Move(e.id, unpack(backwards and e.before or e.after)) then done() end
  elseif (e.kind == "place") == backwards then -- take it away: undo a place, redo a removal
    if EsteriaHousing_Remove(e.id) then done() end
  else -- bring it back: undo a removal, redo a place
    local old = e.id
    RawPlace(e.decorId, e.kind == "place" and e.at or e.before, function(placedId) Remap(old, placedId); e.id = placedId; done() end)
  end
end
function H.Undo() if Owner() then Replay(undo, redo, true) end end
function H.Redo() if Owner() then Replay(redo, undo, false) end end

-- Duplicate: pick up a copy of the selected decor, turned and sized as it is.
function H.Duplicate()
  local d = Placed(selected)
  if not d then return Toast("Select decor to copy.") end
  H.Hold({ decorId = d.decorId, yaw = d.yaw, copyOf = TransformOf(d) }, "place")
end

local function KeyButton(name, onClick)
  local b = CreateFrame("Button", name, UIParent)
  b:Hide()
  b:SetScript("OnClick", onClick)
  return b
end
KeyButton("EsteriaHousingUndo", function() H.Undo() end)
KeyButton("EsteriaHousingRedo", function() H.Redo() end)
KeyButton("EsteriaHousingDuplicate", function() H.Duplicate() end)

-- Expert: retail's submodes for the selected decor (Translate, Rotate, Scale; Gizmo.lua drags their handles), Reset
-- and Store, above the bar.
local expert = CreateFrame("Frame", "EsteriaHousingExpert", UIParent)
expert:SetSize(6 * 44 + 20, 72)
expert:SetPoint("BOTTOM", abilityBar, "TOP", 0, 6)
expert:Hide()
local expertBg = expert:CreateTexture(nil, "BACKGROUND"); expertBg:SetAllPoints(); expertBg:SetTexture(0, 0, 0, 0.55)
local expertName = expert:CreateFontString(nil, "OVERLAY", "GameFontNormal")
expertName:SetPoint("TOP", 0, -4)

local function Edit(d, x, y, z, yaw)
  if not H.Move(d.placedId, x, y, z, yaw) then Toast("You can only edit your own house.") end
end
local EXPERT = {
  { "expert-horzmove", "Translate", "Drag an arrow to slide the decor along that axis. Shift: no snapping.", 1 },
  { "expert-rotate", "Rotate", "Drag a ring to turn the decor about that axis. Shift: no snapping.", 2 },
  { "expert-scale", "Scale", "Drag the handle above the decor up or down to resize it. Shift: no snapping.", 3 },
  { "expert-height", "Reset", "Level it and set its size back; its heading stays.", function() H.Gizmo.Reset() end },
  { "move", "Duplicate", "Pick up a copy of it, turned and sized the same (Ctrl+D).", function() H.Duplicate() end },
  { "cleanup", "Store", "Put it back in storage.", function(d)
    if not H.Remove(d.placedId) then Toast("You can only edit your own house.") end end },
}
local submodeButtons = {}
for i, x in ipairs(EXPERT) do
  local b = AtlasButton(expert, "EsteriaHousingExpert" .. x[2], 40, 40, "decor-ability-" .. x[1] .. "-default",
    "decor-ability-" .. x[1] .. "-active", "decor-ability-" .. x[1] .. "-pressed")
  b:SetPoint("BOTTOMLEFT", 10 + (i - 1) * 44, 6)
  b:SetScript("OnClick", function()
    local d = Placed(selected)
    if type(x[4]) == "number" then H.Gizmo.SetMode(x[4]); H.RedrawExpert()
    elseif selected == "house" and (x[2] == "Store" or x[2] == "Duplicate") then Toast("Your house stays on your plot.")
    elseif selected == "house" then x[4]()
    elseif d then x[4](d) end
  end)
  b:SetScript("OnEnter", function(self)
    GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:AddLine(x[2]); GameTooltip:AddLine(x[3], 1, 1, 1, true); GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  b.submode = type(x[4]) == "number" and x[4] or nil
  submodeButtons[i] = b
end

-- Outdoors your house is selectable too: the handles move, turn and size it on its plot (EsteriaHousing_PlacePlotHouse).
local function HouseTarget()
  local h = state and state.outdoors and state.house
  if not h then return end
  local function clamp(v, lo, hi) return math.max(lo, math.min(hi, v)) end
  return { placedId = "house", x = h.spotX + h.dx, y = h.spotY + h.dy, z = h.spotZ + h.dz,
    yaw = math.deg(h.spotYaw) + h.turn, scale = h.scale, yawOnly = true, minScale = 0.5, maxScale = 1.5,
    commit = function(pos, q, scale)
      local dx, dy = pos[1] - h.spotX, pos[2] - h.spotY
      local far = math.sqrt(dx * dx + dy * dy)
      if far > h.reach then dx, dy = dx * h.reach / far, dy * h.reach / far end
      local turn = math.floor((math.deg(H.Gizmo.heading(q) - h.spotYaw)) % 360 + 0.5)
      return EsteriaHousing_PlacePlotHouse(dx, dy, turn, clamp(pos[3] - h.spotZ, -15, 15), scale)
    end }
end
H.HouseTarget = HouseTarget

function H.RedrawExpert()
  local d = Owner() and editing and ability == 2 and (selected == "house" and HouseTarget() or Placed(selected))
  H.Gizmo.SetTarget(d or nil, d and d.decorId and fdids[d.decorId])
  if not d then return expert:Hide() end
  for _, b in ipairs(submodeButtons) do
    if b.submode then b:SetOn(b.submode == H.Gizmo.Mode()) end
  end
  local scale = d.scale and math.abs(d.scale - 1) > 0.001 and string.format("  x%.2f", d.scale) or ""
  local name = d.placedId == "house" and "Your House" or names[d.decorId] or ("Decor " .. d.decorId)
  expertName:SetText(string.format("%s  |cffa0a0a0%d\194\176%s|r", name, d.yaw % 360, scale))
  expert:Show()
end
local RedrawExpert = H.RedrawExpert

-- ------------------------------------------------------------------------------------------------ world clicks

-- While holding, selecting or cleaning up, a click-catcher covers the world (under every panel) so clicks act on
-- decor instead of targeting.
local catcher = CreateFrame("Button", "EsteriaHousingCatcher", UIParent)
catcher:SetAllPoints(UIParent)
catcher:SetFrameStrata("BACKGROUND")
catcher:RegisterForClicks("LeftButtonUp", "RightButtonUp")
catcher:EnableMouseWheel(true)
catcher:Hide()
local hint = catcher:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
hint:SetJustifyH("LEFT")

local function CursorFloor()
  local x, y, z = EsteriaHousing_GetCursorWorld()
  if not x then return end
  local _, _, pz = EsteriaHousing_GetPlayerPosition()
  -- A ray that slips past the room's floor meets the map's ground plane; keep things at the player's floor then.
  if pz and math.abs(z - pz) > 4 then z = pz end
  return x, y, z
end

local function Nearest(x, y, z)
  local best, bestDistance = nil, PICK * PICK
  for _, d in ipairs(state and state.decor or {}) do
    local distance = (d.x - x) ^ 2 + (d.y - y) ^ 2
    if distance < bestDistance and math.abs(d.z - z) < 3 then best, bestDistance = d, distance end
  end
  return best
end

local function AbilityTool()
  return editing and (ability == 2 and "select" or ability == 4 and "cleanup") or nil
end

function H.Hold(what, which)
  holding = what
  tool = which or (what and "place")
  if what and not what.yaw then
    local _, _, _, facing = EsteriaHousing_GetPlayerPosition()
    what.yaw = math.floor(((facing or 0) + 180) / TURN + 0.5) * TURN -- face the player, on the server's 15 degree snap
  end
  -- the ghost: a see-through copy under the cursor that the click drops in place
  Ghost(what and what.decorId, what and what.yaw)
  if tool then catcher:Show() else catcher:Hide() end
  H.RedrawDecor()
end

-- back to the ability's own tool: Expert selects, Cleanup stores
function H.Drop() H.Hold(nil, AbilityTool()) end

catcher:SetScript("OnMouseDown", function(self, button)
  if button == "LeftButton" and tool == "select" and H.Gizmo.MouseDown() then self.dragged = true end
end)
catcher:SetScript("OnMouseUp", function(self, button)
  if button == "LeftButton" then H.Gizmo.MouseUp() end
end)

catcher:SetScript("OnClick", function(self, button)
  if self.dragged then self.dragged = nil; return end -- that press dragged a handle
  if button == "RightButton" then
    if holding then return H.Drop() end
    return H.SetAbility(1) -- right-click leaves Expert / Cleanup
  end
  local x, y, z
  if holding and EsteriaHousing_GetGhost then x, y, z = EsteriaHousing_GetGhost() end -- exactly where the ghost stands
  if not x then x, y, z = CursorFloor() end
  if not x then return Toast("Point at the floor.") end
  if tool == "place" then
    if not H.Place(holding.decorId, x, y, z, holding.yaw, holding.copyOf and { unpack(holding.copyOf) }) then
      Toast("You can only edit your own house.")
    end
    if not IsShiftKeyDown() then H.Drop() end -- shift-click keeps placing copies
  elseif tool == "move" then
    local d = Placed(holding.placedId)
    if d then Edit(d, x, y, z, holding.yaw) end
    H.Drop()
  elseif tool == "select" then
    local best = Nearest(x, y, z)
    selected = best and best.placedId
    local house = not best and HouseTarget()
    if house and (house.x - x) ^ 2 + (house.y - y) ^ 2 < (12 * house.scale) ^ 2 then selected = "house" end
    if not selected then Toast("No decor there.") end
    RedrawExpert()
  elseif tool == "cleanup" then
    local best = Nearest(x, y, z)
    if not best then return Toast("No decor there.") end
    if not H.Remove(best.placedId) then Toast("You can only edit your own house.") end
  end
end)

catcher:SetScript("OnMouseWheel", function(self, delta)
  if holding then
    holding.yaw = holding.yaw + delta * TURN
    Ghost(holding.decorId, holding.yaw)
  elseif tool == "select" and not H.Gizmo.Dragging() then
    if (Placed(selected) or selected == "house") and not H.Gizmo.Turn(delta * TURN) then Toast("You can only edit your own house.") end
  end
end)

local LEFT, RIGHT, WHEEL = "housing-hotkey-icon-leftclick", "housing-hotkey-icon-rightclick", "housing-hotkey-icon-mousewheel"
catcher:SetScript("OnUpdate", function(self)
  local scale = UIParent:GetEffectiveScale()
  local cx, cy = GetCursorPosition()
  hint:ClearAllPoints()
  hint:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", cx / scale + 24, cy / scale - 8)
  local l, r, w = H.Icon(LEFT, 16), H.Icon(RIGHT, 16), H.Icon(WHEEL, 16)
  if tool == "select" then
    H.Gizmo.Update()
    hint:SetText(Placed(selected) and ("Expert\n" .. l .. " drag a handle, or select   " .. w .. " turn   " .. r .. " done")
      or ("Select Decor\n" .. l .. " select   " .. r .. " done"))
  elseif tool == "cleanup" then
    hint:SetText("Cleanup\n" .. l .. " store decor   " .. r .. " done")
  elseif holding then
    hint:SetText(string.format("%s  %d\194\176\n%s %s   %s turn   %s%s cancel", names[holding.decorId] or "Decor",
      holding.yaw % 360, l, tool == "move" and "move here" or "place", w, tool == "place" and "shift: more   " or "", r))
  end
end)

-- ------------------------------------------------------------------------------------------------ modes

function H.SetAbility(i)
  if state and state.outdoors and (i == 3 or i == 5) then
    Toast("Rooms are styled and built inside your house.")
    i = ability ~= 3 and ability ~= 5 and ability or 1
  end
  if i == 3 then -- Customize is a menu for the room you stand in, not a mode of its own
    if H.CustomizeHere then H.CustomizeHere() end
    i = ability == 3 and 1 or ability
  end
  ability = i
  for n, b in ipairs(abilityButtons) do b:SetOn(n == i) end
  if H.ShowLayout then H.ShowLayout(editing and ability == 5) end
  if editing and ability ~= 5 then chest:Show() else chest:Hide() end
  H.Hold(nil, AbilityTool())
  RedrawExpert()
end

local function Bind(on)
  ClearOverrideBindings(abilityBar)
  if on then
    for i = 1, 5 do SetOverrideBindingClick(abilityBar, true, tostring(i), "EsteriaHousingAbility" .. i) end
    SetOverrideBindingClick(abilityBar, true, "CTRL-Z", "EsteriaHousingUndo")
    SetOverrideBindingClick(abilityBar, true, "CTRL-Y", "EsteriaHousingRedo")
    SetOverrideBindingClick(abilityBar, true, "CTRL-SHIFT-Z", "EsteriaHousingRedo")
    SetOverrideBindingClick(abilityBar, true, "CTRL-D", "EsteriaHousingDuplicate")
  end
end

function H.SetEditing(on)
  editing = on and Owner() or nil
  decorate:SetOn(editing)
  if editing then abilityBar:Show() else abilityBar:Hide() end
  Bind(editing)
  if EsteriaHousing_ShowPlotBounds then EsteriaHousing_ShowPlotBounds(editing and true or false) end
  H.SetAbility(editing and ability or 1)
  H.RedrawCart()
end
decorate:SetScript("OnClick", function() H.SetEditing(not editing) end)

function H.ResetPositions()
  for _, f in ipairs({ controls, budget, chest, abilityBar, cartFrame, EsteriaHousingLayout }) do
    if f then f:SetUserPlaced(false) end
  end
  ReloadUI()
end

-- /housing and older callers: "decor" toggles decorating, "layout" opens the blueprint
function H.SetMode(m)
  if m == "layout" then H.SetEditing(true); H.SetAbility(5)
  elseif m == "decor" then H.SetEditing(not editing)
  else H.SetEditing(false) end
end

local function RedrawControls()
  if not (state and state.inHouse) then
    if editing then H.SetEditing(false) end
    controls:Hide(); budget:Hide(); upgrades:Hide(); return
  end
  controls:Show()
  local owner, outdoors = state.isOwner, state.outdoors
  for _, b in ipairs({ decorate, inspect, houseinfo, settings }) do if owner then b:Show() else b:Hide() end end
  if outdoors then houseinfo:Hide(); exit:Hide(); upgrades:Hide() else exit:Show() end -- outside: the plot, no house to leave
  H.Atlas(controlsBg, owner and "controls-frame" or "controls-frame-guest")
  if not owner and editing then H.SetEditing(false) end
  budgetText:SetText(string.format("%d/%d", state.placed, state.maxDecor))
  H.Atlas(budgetIcon, outdoors and "house-outdoor-budget-icon" or "house-decor-budget-icon")
  if owner then budget:Show() else budget:Hide() end
  -- rooms are inside only
  for _, i in ipairs({ 3, 5 }) do abilityButtons[i]:SetAlpha(outdoors and 0.35 or 1) end
  if outdoors and (ability == 3 or ability == 5) then H.SetAbility(1) end
end

-- ------------------------------------------------------------------------------------------------ events

controls:RegisterEvent("ESTERIA_HOUSING_AVAILABLE")
controls:RegisterEvent("ESTERIA_HOUSING_CATALOG")
controls:RegisterEvent("ESTERIA_HOUSING_STATE")
controls:RegisterEvent("ESTERIA_HOUSING_RESULT")
controls:RegisterEvent("PLAYER_MONEY")
controls:SetScript("OnEvent", function(self, event, result, op, placedId)
  if event == "ESTERIA_HOUSING_CATALOG" then
    catalog = EsteriaHousing_GetCatalog()
    table.sort(catalog, function(a, b) return a.name < b.name end)
    for _, e in ipairs(catalog) do names[e.decorId] = e.name; fdids[e.decorId] = e.fileDataId end
  elseif event == "ESTERIA_HOUSING_STATE" or event == "ESTERIA_HOUSING_AVAILABLE" then
    state = EsteriaHousing_GetState()
    local key = state and state.inHouse and (state.houseId .. (state.outdoors and "out" or "in"))
    if key ~= historyKey then historyKey = key; wipe(undo); wipe(redo); wipe(placeWaiters) end
    if not Placed(selected) and not (selected == "house" and HouseTarget()) then selected = nil end
    if holding and holding.placedId and not Placed(holding.placedId) then H.Drop() end
    RedrawControls()
  elseif event == "ESTERIA_HOUSING_RESULT" then
    if result ~= 0 then Toast(RESULT[result] or ("Housing error " .. result))
    elseif op == 1 then selected = placedId end
    state = EsteriaHousing_GetState()
    if op == 1 then -- a place's ack: whoever waits for it learns where it went (or that it did not happen)
      local waiter = table.remove(placeWaiters, 1)
      if waiter and result == 0 then waiter(placedId) end
    end
    RedrawControls()
  end
  H.RedrawDecor()
  H.RedrawCart()
  RedrawExpert()
end)

SLASH_ESTERIAHOUSING1 = "/housing"
SlashCmdList.ESTERIAHOUSING = function()
  if not EsteriaHousing_IsAvailable() then return Toast("Housing is off (set housingEnabled 1, and the realm must enable it).") end
  if not Owner() then return Toast("Open the editor inside your own house.") end
  H.SetEditing(not editing)
end
