-- Esteria housing: Layout mode, a top-down blueprint of the house (north up), one floor at a time. Each free wall a room
-- can be built through gets a "+"; it opens a menu of the room kinds that fit there and what they cost. Clicking a room
-- opens its menu: wall, floor and ceiling style, and the ceiling on or off. A stairwell leads up to the next floor; the
-- floor below shows faintly under the one shown.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local CANVAS, PLUS, PLUS_GAP = 380, 16, 1 -- the "+" sits PLUS_GAP px outside its wall
local PARTS = { { 1, "Walls", "wallTheme" }, { 2, "Floor", "floorTheme" }, { 3, "Ceiling", "ceilingTheme" } }
local layout
local floor = 0 -- the floor shown: 0 the ground, -1 the basement
local LEVEL, MIN_LEVEL, MAX_LEVEL, STAIRWELL_TOP = 12, -1, 14, 48 -- MAX_LEVEL: the 15th floor, the most a house can buy
local FLOOR_ROWS = 9 -- floors listed at once; the list follows the floor shown
-- each floor's colour on its rooms and in the floor list: the basement violet, then amber, teal, green, rose, sky
-- and gold, repeating up the house
local BASEMENT_COLOR = { 0.55, 0.4, 0.8 }
local FLOOR_COLORS = { { 0.85, 0.62, 0.28 }, { 0.3, 0.72, 0.75 }, { 0.45, 0.78, 0.35 }, { 0.85, 0.42, 0.5 },
  { 0.45, 0.6, 0.95 }, { 0.9, 0.85, 0.4 } }
local function FloorColor(level)
  return unpack(level < 0 and BASEMENT_COLOR or FLOOR_COLORS[level % #FLOOR_COLORS + 1])
end
-- retail's room icon (housingroomicons) for each room kind; stairs down share their stairwell's
local ROOM_ICONS = { [1] = "layout_square_s_icon", [2] = "layout_hallway_s_icon", [3] = "layout_closet_xs_icon",
  [6] = "layout_t-shape_s_icon", [7] = "layout_square_xs_icon", [8] = "layout_l-shape_s_icon", [9] = "layout_octogon_m_icon",
  [10] = "layout_staircase_s_icon", [11] = "layout_square_m_icon", [12] = "layout_square_l_icon", [13] = "layout_cross_s_icon",
  [14] = "layout_octogon_s_icon", [15] = "layout_octogon_l_icon", [48] = "layout_tallroom_s_icon",
  [50] = "layout_staircase_mirrored_s_icon" }

-- EasyMenu fields that draw a retail atlas element as the item's icon
local function MenuIcon(item, name)
  local a = H.ATLAS and H.ATLAS[name]
  if a then
    item.icon = "retail/" .. a[1] .. ".blp"
    item.tCoordLeft, item.tCoordRight, item.tCoordTop, item.tCoordBottom = a[4], a[5], a[6], a[7]
  end
  return item
end

local f = H.Panel("EsteriaHousingLayout", CANVAS + 24 + 110, CANVAS + 80)
f:SetPoint("CENTER", -80, 40)
local roomLimit = H.Atlas(f:CreateTexture(nil, "OVERLAY"), "house-room-limit-icon", 16, 16)
local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -7); title:SetText("Layout")
local count = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
count:SetPoint("TOP", title, "BOTTOM", 0, -2)
roomLimit:SetPoint("RIGHT", count, "LEFT", -4, 0)
local canvas = CreateFrame("Frame", nil, f)
canvas:SetSize(CANVAS, CANVAS); canvas:SetPoint("TOPLEFT", 12, -44)
local grid = canvas:CreateTexture(nil, "BACKGROUND"); grid:SetAllPoints(); grid:SetTexture(0.06, 0.045, 0.03, 0.92)
local inner = H.Atlas(canvas:CreateTexture(nil, "BORDER"), "housing-basic-panel-innerblackbox")
inner:SetAllPoints(); inner:SetAlpha(0.5)
local north = canvas:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
north:SetPoint("TOPRIGHT", -6, -4); north:SetText("North")


local remove = H.Button(f, "Remove Newest Room", 170, 22)
remove:SetPoint("BOTTOM", -55, 10)
StaticPopupDialogs["ESTERIA_HOUSING_REMOVE_ROOM"] = {
  text = "Remove the newest room? Its decor must be stored first, and its price is not refunded.",
  button1 = YES, button2 = NO, timeout = 0, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_RemoveRoom() end,
}
remove:SetScript("OnClick", function() StaticPopup_Show("ESTERIA_HOUSING_REMOVE_ROOM") end)

local menu = CreateFrame("Frame", "EsteriaHousingLayoutMenu", UIParent, "UIDropDownMenuTemplate")

-- the floor list to the right of the blueprint, highest floor on top, as retail's layout editor stacks them
local floors = CreateFrame("Frame", nil, f)
floors:SetSize(100, CANVAS); floors:SetPoint("TOPLEFT", canvas, "TOPRIGHT", 6, 0)
local floorsBg = H.Atlas(floors:CreateTexture(nil, "BACKGROUND"), "housing-floor-container-bg"); floorsBg:SetAllPoints()
local floorsTitle = floors:CreateFontString(nil, "OVERLAY", "GameFontNormal")
floorsTitle:SetPoint("TOP", 0, -8); floorsTitle:SetText("Floors")
local floorTabs = {}
for level = MAX_LEVEL, MIN_LEVEL, -1 do
  local b = CreateFrame("Button", nil, floors)
  b:SetSize(92, 26)
  b.level = level
  b.bg = H.Atlas(b:CreateTexture(nil, "BACKGROUND"), "housing-floor-list-bg-active"); b.bg:SetAllPoints()
  b.door = H.Atlas(b:CreateTexture(nil, "ARTWORK"), "housing-floor-door", 16, 16); b.door:SetPoint("LEFT", 4, 0)
  b.line = H.Atlas(b:CreateTexture(nil, "ARTWORK"), "housing-floor-list-divider-default"); b.line:SetPoint("BOTTOMLEFT")
  b.line:SetPoint("BOTTOMRIGHT"); b.line:SetHeight(1)
  b:SetFontString(b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"))
  b:GetFontString():SetPoint("LEFT", 24, 0)
  b:SetText(level < 0 and "Basement" or ("Floor " .. (level + 1)))
  local hl = H.ButtonTexture(b, "Highlight", "housing-floor-list-bg-active")
  if hl then hl:SetBlendMode("ADD"); hl:SetAlpha(0.4) end
  b:SetScript("OnClick", function() floor = level; H.RedrawLayout() end)
  floorTabs[#floorTabs + 1] = b
end
local function Arrow(dir, y)
  local b = CreateFrame("Button", nil, floors)
  b:SetSize(16, 16); b:SetPoint("BOTTOM", 0, y)
  for state, look in pairs({ Normal = "default", Pushed = "pressed", Disabled = "disabled" }) do
    H.ButtonTexture(b, state, "housing-floor-arrow-" .. dir .. "-" .. look)
  end
  b:SetScript("OnClick", function() floor = floor + (dir == "up" and 1 or -1); H.RedrawLayout() end)
  return b
end
local floorUp, floorDown = Arrow("up", 30), Arrow("down", 10)
local hint = floors:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("BOTTOM", 0, 56); hint:SetWidth(90); hint:SetText("+ builds a room\n\nClick a room to style it")

local function LowestLevel()
  local lowest = 0
  for _, room in ipairs(layout and layout.rooms or {}) do lowest = math.min(lowest, room.level) end
  return lowest
end

-- the floor the player stands on, from the house's lowest point (the state's bounds start 0.5 yd under that floor)
local function PlayerFloor()
  local state = EsteriaHousing_GetState()
  local _, _, z = EsteriaHousing_GetPlayerPosition()
  if not (state and state.minZ and z) then return 0 end
  local ground = state.minZ + 0.5 - LowestLevel() * LEVEL
  return math.max(MIN_LEVEL, math.min(MAX_LEVEL, math.floor((z - ground + 2) / LEVEL)))
end

local rects, plusses = {}, {}
local player = canvas:CreateTexture(nil, "OVERLAY")
player:SetTexture("Interface\\Minimap\\MinimapArrow"); player:SetSize(20, 20)

local scale, cx, cy = 1, 0, 0
-- world offset (dx north, dy west) to canvas point: north is up, east is right
local function ToCanvas(dx, dy) return CANVAS / 2 + (-dy - cx) * scale, CANVAS / 2 + (dx - cy) * scale end

local function Size(kind, quarter)
  if not kind then return 10, 10 end
  if quarter % 2 == 1 then return kind.sizeY, kind.sizeX end
  return kind.sizeX, kind.sizeY
end

local function ThemeName(id)
  for _, t in ipairs(layout and layout.themes or {}) do if t.id == id then return t.name end end
  return "Generic"
end

-- the room's menu: a style submenu per part, and the ceiling switch
local function RoomMenu(index)
  local room = layout.rooms[index]
  local kind = layout.kinds[room.kind]
  local items = { { text = kind and kind.name or "Room", isTitle = true, notCheckable = true } }
  for _, part in ipairs(PARTS) do
    local styles = {}
    for _, t in ipairs(layout.themes) do
      styles[#styles + 1] = { text = t.name, checked = room[part[3]] == t.id,
        func = function() EsteriaHousing_StyleRoom(index - 1, part[1], t.id); CloseDropDownMenus() end }
    end
    items[#items + 1] = { text = string.format("%s: |cffffd100%s|r", part[2], ThemeName(room[part[3]])), hasArrow = true,
      notCheckable = true, menuList = styles }
  end
  items[#items + 1] = { text = room.ceilingHidden and "Show Ceiling" or "Hide Ceiling", notCheckable = true,
    func = function() EsteriaHousing_SetCeilingHidden(index - 1, not room.ceilingHidden) end }
  if room.kind == STAIRWELL_TOP and room.level < (layout.maxFloors or 4) - 1 then
    local stairs = layout.kinds[10]
    local price = stairs and stairs.price or 0
    items[#items + 1] = { notCheckable = true, disabled = GetMoney() < price,
      text = "Extend Stairs Up  " .. GetCoinTextureString(price), func = function() EsteriaHousing_StairsUp(index - 1) end }
  end
  return items
end

-- the "+" menu: every kind that fits this wall, with its price; ones you cannot afford are greyed out
local function BuildMenu(socket)
  local items = { { text = "Build a Room", isTitle = true, notCheckable = true } }
  for _, id in ipairs(socket.fits) do
    local kind = layout.kinds[id]
    if kind then
      local afford = GetMoney() >= kind.price
      items[#items + 1] = MenuIcon({ notCheckable = true, disabled = not afford,
        text = string.format("%s  |cffa0a0a0%dx%d|r  %s", kind.name, kind.sizeX, kind.sizeY, GetCoinTextureString(kind.price)),
        func = function()
          if not EsteriaHousing_AddRoom(socket.room, socket.piece, id) then H.Toast("You can only edit your own house.") end
        end }, ROOM_ICONS[id % 1000])
    end
  end
  items[#items + 1] = { text = CANCEL, notCheckable = true, func = function() CloseDropDownMenus() end }
  return items
end

local function Rect(i)
  local r = rects[i]
  if not r then
    r = CreateFrame("Button", nil, canvas)
    r.border = r:CreateTexture(nil, "BORDER"); r.border:SetAllPoints(); r.border:SetTexture(0.78, 0.6, 0.3, 1)
    r.fill = r:CreateTexture(nil, "ARTWORK"); r.fill:SetPoint("TOPLEFT", 2, -2); r.fill:SetPoint("BOTTOMRIGHT", -2, 2)
    r:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
    r.label = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    r.label:SetFont(STANDARD_TEXT_FONT, 9); r.label:SetPoint("CENTER")
    r:SetScript("OnClick", function(self) EasyMenu(RoomMenu(self.index), menu, "cursor", 0, 0, "MENU") end)
    r:SetScript("OnEnter", function(self)
      local room = layout.rooms[self.index]
      local kind = layout.kinds[room.kind]
      GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
      GameTooltip:AddLine(kind and kind.name or "Room")
      for _, part in ipairs(PARTS) do GameTooltip:AddDoubleLine(part[2], ThemeName(room[part[3]]), 1, 1, 1, 1, 0.82, 0) end
      if room.ceilingHidden then GameTooltip:AddLine("Ceiling hidden", 0.6, 0.6, 0.6) end
      GameTooltip:AddLine("Click to style this room.", 0.5, 0.8, 1)
      GameTooltip:Show()
    end)
    r:SetScript("OnLeave", function() GameTooltip:Hide() end)
    rects[i] = r
  end
  return r
end

local function Plus(i)
  local b = plusses[i]
  if not b then
    b = CreateFrame("Button", nil, canvas)
    b:SetSize(PLUS, PLUS)
    b:SetNormalTexture("Interface\\Buttons\\UI-PlusButton-Up")
    b:SetPushedTexture("Interface\\Buttons\\UI-PlusButton-Down")
    b:SetHighlightTexture("Interface\\Buttons\\UI-PlusButton-Hilight", "ADD")
    b:SetScript("OnClick", function(self) EasyMenu(BuildMenu(self.socket), menu, "cursor", 0, 0, "MENU") end)
    b:SetScript("OnEnter", function(self)
      GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:AddLine("Build a room here"); GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    plusses[i] = b
  end
  return b
end

function H.RedrawLayout()
  if not f:IsShown() then return end
  layout = EsteriaHousing_GetLayout()
  for _, r in ipairs(rects) do r:Hide() end
  for _, b in ipairs(plusses) do b:Hide() end
  if not layout then count:SetText("Waiting for the house..."); return end

  -- fit every room, plus room for the "+" markers, into the canvas
  local minX, maxX, minY, maxY = 1e9, -1e9, 1e9, -1e9
  for _, room in ipairs(layout.rooms) do
    local sx, sy = Size(layout.kinds[room.kind], room.quarter)
    minX = math.min(minX, -room.y - sy / 2); maxX = math.max(maxX, -room.y + sy / 2)
    minY = math.min(minY, room.x - sx / 2); maxY = math.max(maxY, room.x + sx / 2)
  end
  if minX > maxX then minX, maxX, minY, maxY = -12, 12, -12, 12 end
  local edge = 2 * (PLUS + PLUS_GAP) + 12 -- px kept free around the house
  scale = math.min(10, (CANVAS - edge) / (maxX - minX), (CANVAS - edge) / (maxY - minY))
  cx, cy = (minX + maxX) / 2, (minY + maxY) / 2

  -- one tab per floor the house has, lowest first
  local lowest, highest = 0, 0
  for _, room in ipairs(layout.rooms) do lowest, highest = math.min(lowest, room.level), math.max(highest, room.level) end
  floor = math.max(lowest, math.min(floor, highest))
  -- the list shows FLOOR_ROWS floors at most, around the one shown (floorTabs runs from the top floor down)
  local top = math.min(highest, math.max(floor + math.floor(FLOOR_ROWS / 2), lowest + FLOOR_ROWS - 1))
  local bottom = math.max(lowest, top - FLOOR_ROWS + 1)
  local y = -30
  for _, b in ipairs(floorTabs) do
    if b.level >= bottom and b.level <= top then
      b:ClearAllPoints(); b:SetPoint("TOP", floors, "TOP", 0, y); y = y - 28
      b.bg:SetAlpha(b.level == floor and 1 or 0)
      H.Atlas(b.door, b.level == floor and "housing-floor-door" or "housing-floor-door-inactive")
      b.door:SetVertexColor(FloorColor(b.level))
      b:GetFontString():SetTextColor(FloorColor(b.level))
      b:Show()
    else
      b:Hide()
    end
  end
  if floor < highest then floorUp:Enable() else floorUp:Disable() end
  if floor > lowest then floorDown:Enable() else floorDown:Disable() end

  local n = 0
  for i, room in ipairs(layout.rooms) do
    local here, below = room.level == floor, room.level == floor - 1
    if here or below then
      n = n + 1
      local kind = layout.kinds[room.kind]
      local sx, sy = Size(kind, room.quarter)
      local x, y = ToCanvas(room.x, room.y)
      local w, h = math.max(8, sy * scale), math.max(8, sx * scale)
      local r = Rect(n)
      r.index = i
      r:ClearAllPoints()
      r:SetPoint("CENTER", canvas, "BOTTOMLEFT", x, y)
      r:SetSize(w, h)
      r:SetFrameLevel(canvas:GetFrameLevel() + (here and 3 or 1))
      r:EnableMouse(here) -- the floor below is a faint guide only
      r:SetAlpha(here and 1 or 0.25)
      local newest = i == #layout.rooms and i > 1
      local cr, cg, cb = FloorColor(room.level)
      r.border:SetTexture(cr, cg, cb, 1)
      local shade = newest and 0.45 or 0.28 -- the newest room a little brighter
      r.fill:SetTexture(cr * shade, cg * shade, cb * shade, 0.95)
      -- names only where they fit; the tooltip always has them
      local name = room.kind == STAIRWELL_TOP and "Stairs down" or (kind and kind.name or "Room")
      if kind and kind.name:find("^Stairwell %(") then name = "Stairs up" end
      r.label:SetWidth(w - 6)
      r.label:SetText(here and w >= 44 and h >= 22 and name or "")
      r:Show()
    end
  end

  local m = 0
  for _, socket in ipairs(layout.sockets) do
    if socket.level == floor then
    m = m + 1
    local b = Plus(m)
    local x, y = ToCanvas(socket.x, socket.y)
    -- outward in screen space: world +x (north) is up, +y (west) is left
    local gap = PLUS_GAP + PLUS / 2
    b.socket = socket
    b:ClearAllPoints(); b:SetPoint("CENTER", canvas, "BOTTOMLEFT", x - socket.ny * gap, y + socket.nx * gap)
    b:SetFrameLevel(canvas:GetFrameLevel() + 5)
    b:Show()
    end
  end

  local full = #layout.rooms >= layout.maxRooms
  count:SetText(full and string.format("|cffff8040Rooms %d / %d: full. Remove a room to build elsewhere.|r", #layout.rooms,
    layout.maxRooms) or string.format("Rooms %d / %d", #layout.rooms, layout.maxRooms))
  if #layout.rooms > 1 then remove:Enable() else remove:Disable() end
end

f:SetScript("OnUpdate", function(self, elapsed)
  self.wait = (self.wait or 0) + elapsed
  if self.wait < 0.1 or not layout then return end
  self.wait = 0
  local x, y, _, facing = EsteriaHousing_GetPlayerPosition()
  if not x or PlayerFloor() ~= floor then return player:Hide() end
  local px, py = ToCanvas(x - layout.originX, y - layout.originY)
  player:ClearAllPoints(); player:SetPoint("CENTER", canvas, "BOTTOMLEFT", px, py)
  if player.SetRotation then player:SetRotation(math.rad(facing)) end
  player:Show()
end)

f:SetScript("OnShow", function() floor = PlayerFloor(); H.RedrawLayout() end)
f:SetScript("OnHide", function() CloseDropDownMenus(); StaticPopup_Hide("ESTERIA_HOUSING_REMOVE_ROOM") end)
f:RegisterEvent("ESTERIA_HOUSING_LAYOUT")
f:RegisterEvent("PLAYER_MONEY")
f:SetScript("OnEvent", function(self, event)
  if event == "ESTERIA_HOUSING_LAYOUT" then CloseDropDownMenus() end -- a new layout makes an open menu stale
  H.RedrawLayout()
end)

-- Customize (ability 3): the style menu of the room the player stands in.
function H.CustomizeHere()
  layout = EsteriaHousing_GetLayout()
  local x, y = EsteriaHousing_GetPlayerPosition()
  if not (layout and x) then return end
  local level, dx, dy = PlayerFloor(), x - layout.originX, y - layout.originY
  for i, room in ipairs(layout.rooms) do
    local sx, sy = Size(layout.kinds[room.kind], room.quarter)
    if room.level == level and math.abs(dx - room.x) <= sx / 2 and math.abs(dy - room.y) <= sy / 2 then
      return EasyMenu(RoomMenu(i), menu, "cursor", 0, 0, "MENU")
    end
  end
  H.Toast("Stand in a room to customize it.")
end

function H.ShowLayout(show)
  if show then f:Show() else f:Hide() end
end
