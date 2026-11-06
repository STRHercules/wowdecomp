-- Esteria housing: Layout mode, a top-down blueprint of the house (north up). Each free wall a room can be built
-- through gets a "+"; it opens a menu of the room kinds that fit there and what they cost. Clicking a room opens its
-- menu: wall, floor and ceiling style, and the ceiling on or off.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local CANVAS, PLUS, PLUS_GAP = 380, 16, 11 -- the "+" sits PLUS_GAP px outside its wall
local PARTS = { { 1, "Walls", "wallTheme" }, { 2, "Floor", "floorTheme" }, { 3, "Ceiling", "ceilingTheme" } }
local layout

local f = H.Panel("EsteriaHousingLayout", CANVAS + 24, CANVAS + 80)
f:SetPoint("CENTER", -120, 40)
local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -12); title:SetText("Layout")
local count = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
count:SetPoint("TOP", title, "BOTTOM", 0, -2)
local canvas = CreateFrame("Frame", nil, f)
canvas:SetSize(CANVAS, CANVAS); canvas:SetPoint("TOP", 0, -44)
local grid = canvas:CreateTexture(nil, "BACKGROUND"); grid:SetAllPoints(); grid:SetTexture(0.05, 0.08, 0.12, 0.9)
local north = canvas:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
north:SetPoint("TOPRIGHT", -6, -4); north:SetText("North")
local hint = canvas:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("BOTTOM", 0, 4); hint:SetText("+ builds a room    Click a room to style it")

local remove = H.Button(f, "Remove Newest Room", 170, 22)
remove:SetPoint("BOTTOM", 0, 10)
StaticPopupDialogs["ESTERIA_HOUSING_REMOVE_ROOM"] = {
  text = "Remove the newest room? Its decor must be stored first, and its price is not refunded.",
  button1 = YES, button2 = NO, timeout = 0, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_RemoveRoom() end,
}
remove:SetScript("OnClick", function() StaticPopup_Show("ESTERIA_HOUSING_REMOVE_ROOM") end)

local menu = CreateFrame("Frame", "EsteriaHousingLayoutMenu", UIParent, "UIDropDownMenuTemplate")

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
  return items
end

-- the "+" menu: every kind that fits this wall, with its price; ones you cannot afford are greyed out
local function BuildMenu(socket)
  local items = { { text = "Build a Room", isTitle = true, notCheckable = true } }
  for _, id in ipairs(socket.fits) do
    local kind = layout.kinds[id]
    if kind then
      local afford = GetMoney() >= kind.price
      items[#items + 1] = { notCheckable = true, disabled = not afford,
        text = string.format("%s  |cffa0a0a0%dx%d|r  %s", kind.name, kind.sizeX, kind.sizeY, GetCoinTextureString(kind.price)),
        func = function()
          if not EsteriaHousing_AddRoom(socket.room, socket.piece, id) then H.Toast("You can only edit your own house.") end
        end }
    end
  end
  items[#items + 1] = { text = CANCEL, notCheckable = true, func = function() CloseDropDownMenus() end }
  return items
end

local function Rect(i)
  local r = rects[i]
  if not r then
    r = CreateFrame("Button", nil, canvas)
    r.border = r:CreateTexture(nil, "BORDER"); r.border:SetAllPoints(); r.border:SetTexture(0.85, 0.7, 0.35, 1)
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

  for i, room in ipairs(layout.rooms) do
    local kind = layout.kinds[room.kind]
    local sx, sy = Size(kind, room.quarter)
    local x, y = ToCanvas(room.x, room.y)
    local w, h = math.max(8, sy * scale), math.max(8, sx * scale)
    local r = Rect(i)
    r.index = i
    r:ClearAllPoints()
    r:SetPoint("CENTER", canvas, "BOTTOMLEFT", x, y)
    r:SetSize(w, h)
    local newest = i == #layout.rooms and i > 1
    if newest then r.fill:SetTexture(0.25, 0.32, 0.45, 0.95) else r.fill:SetTexture(0.16, 0.2, 0.28, 0.95) end
    -- names only where they fit; the tooltip always has them
    local name = kind and kind.name or "Room"
    r.label:SetWidth(w - 6)
    r.label:SetText(w >= 44 and h >= 22 and name or "")
    r:Show()
  end

  for i, socket in ipairs(layout.sockets) do
    local b = Plus(i)
    local x, y = ToCanvas(socket.x, socket.y)
    -- outward in screen space: world +x (north) is up, +y (west) is left
    local gap = PLUS_GAP + PLUS / 2
    b.socket = socket
    b:ClearAllPoints(); b:SetPoint("CENTER", canvas, "BOTTOMLEFT", x - socket.ny * gap, y + socket.nx * gap)
    b:Show()
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
  if not x then return player:Hide() end
  local px, py = ToCanvas(x - layout.originX, y - layout.originY)
  player:ClearAllPoints(); player:SetPoint("CENTER", canvas, "BOTTOMLEFT", px, py)
  if player.SetRotation then player:SetRotation(math.rad(facing)) end
  player:Show()
end)

f:SetScript("OnShow", H.RedrawLayout)
f:SetScript("OnHide", function() CloseDropDownMenus(); StaticPopup_Hide("ESTERIA_HOUSING_REMOVE_ROOM") end)
f:RegisterEvent("ESTERIA_HOUSING_LAYOUT")
f:RegisterEvent("PLAYER_MONEY")
f:SetScript("OnEvent", function(self, event)
  if event == "ESTERIA_HOUSING_LAYOUT" then CloseDropDownMenus() end -- a new layout makes an open menu stale
  H.RedrawLayout()
end)

function H.ShowLayout(show)
  if show then f:Show() else f:Hide() end
end
