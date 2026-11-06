-- Esteria housing: Layout mode, a top-down blueprint of the house. Rooms are drawn to scale (north up), each free
-- wall a room can be built through gets a "+", and picking one lists the room kinds that fit there.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local CANVAS = 380
local layout, chosen -- chosen: the socket whose kinds are listed

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

local remove = H.Button(f, "Remove Newest Room", 170, 22)
remove:SetPoint("BOTTOM", 0, 10)
StaticPopupDialogs["ESTERIA_HOUSING_REMOVE_ROOM"] = {
  text = "Remove the newest room? Its decor must be stored first.",
  button1 = YES, button2 = NO, timeout = 0, hideOnEscape = true,
  OnAccept = function() EsteriaHousing_RemoveRoom() end,
}
remove:SetScript("OnClick", function() StaticPopup_Show("ESTERIA_HOUSING_REMOVE_ROOM") end)

-- the kinds that fit the chosen wall
local picker = H.Panel("EsteriaHousingRoomPicker", 220, 60)
picker:SetPoint("TOPLEFT", f, "TOPRIGHT", 4, 0)
local pickerTitle = picker:CreateFontString(nil, "OVERLAY", "GameFontNormal")
pickerTitle:SetPoint("TOP", 0, -10); pickerTitle:SetText("Build a Room")

local rects, plusses, kindButtons = {}, {}, {}
local player = canvas:CreateTexture(nil, "OVERLAY")
player:SetTexture("Interface\\Minimap\\MinimapArrow"); player:SetSize(20, 20)

local scale, cx, cy = 1, 0, 0
-- world offset (dx north, dy west) to canvas point: north is up, east is right
local function ToCanvas(dx, dy) return CANVAS / 2 + (-dy - cx) * scale, CANVAS / 2 + (dx - cy) * scale end

local function Rect(i)
  local r = rects[i]
  if not r then
    r = CreateFrame("Frame", nil, canvas)
    r.border = r:CreateTexture(nil, "BORDER"); r.border:SetAllPoints(); r.border:SetTexture(0.85, 0.7, 0.35, 1)
    r.fill = r:CreateTexture(nil, "ARTWORK"); r.fill:SetPoint("TOPLEFT", 2, -2); r.fill:SetPoint("BOTTOMRIGHT", -2, 2)
    r.label = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); r.label:SetPoint("CENTER"); r.label:SetWidth(90)
    rects[i] = r
  end
  return r
end

local function Plus(i)
  local b = plusses[i]
  if not b then
    b = CreateFrame("Button", nil, canvas)
    b:SetSize(20, 20); b:SetFrameLevel(canvas:GetFrameLevel() + 5)
    b:SetNormalTexture("Interface\\Buttons\\UI-PlusButton-Up")
    b:SetPushedTexture("Interface\\Buttons\\UI-PlusButton-Down")
    b:SetHighlightTexture("Interface\\Buttons\\UI-PlusButton-Hilight", "ADD")
    b:SetScript("OnClick", function(self) chosen = self.socket; H.RedrawLayout() end)
    b:SetScript("OnEnter", function(self)
      GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:AddLine("Build a room here"); GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    plusses[i] = b
  end
  return b
end

local function KindButton(i)
  local b = kindButtons[i]
  if not b then
    b = H.Button(picker, "", 196, 22)
    b:SetPoint("TOP", 0, -10 - i * 24)
    b:SetScript("OnClick", function(self)
      if chosen and not EsteriaHousing_AddRoom(chosen.room, chosen.piece, self.kind) then H.Toast("You can only edit your own house.") end
      chosen = nil
      H.RedrawLayout()
    end)
    kindButtons[i] = b
  end
  return b
end

local function Size(kind, quarter)
  if not kind then return 10, 10 end
  if quarter % 2 == 1 then return kind.sizeY, kind.sizeX end
  return kind.sizeX, kind.sizeY
end

function H.RedrawLayout()
  if not f:IsShown() then return end
  layout = EsteriaHousing_GetLayout()
  for _, r in ipairs(rects) do r:Hide() end
  for _, b in ipairs(plusses) do b:Hide() end
  for _, b in ipairs(kindButtons) do b:Hide() end
  picker:Hide()
  if not layout then count:SetText("Waiting for the house..."); return end

  -- fit every room (plus a margin for the next one) into the canvas
  local minX, maxX, minY, maxY = 1e9, -1e9, 1e9, -1e9
  for _, room in ipairs(layout.rooms) do
    local sx, sy = Size(layout.kinds[room.kind], room.quarter)
    minX = math.min(minX, -room.y - sy / 2); maxX = math.max(maxX, -room.y + sy / 2)
    minY = math.min(minY, room.x - sx / 2); maxY = math.max(maxY, room.x + sx / 2)
  end
  if minX > maxX then minX, maxX, minY, maxY = -12, 12, -12, 12 end
  local margin = 30
  scale = math.min(8, (CANVAS - 20) / (maxX - minX + 2 * margin), (CANVAS - 20) / (maxY - minY + 2 * margin))
  cx, cy = (minX + maxX) / 2, (minY + maxY) / 2

  for i, room in ipairs(layout.rooms) do
    local kind = layout.kinds[room.kind]
    local sx, sy = Size(kind, room.quarter)
    local x, y = ToCanvas(room.x, room.y)
    local r = Rect(i)
    r:ClearAllPoints()
    r:SetPoint("CENTER", canvas, "BOTTOMLEFT", x, y)
    r:SetSize(math.max(8, sy * scale), math.max(8, sx * scale))
    local newest = i == #layout.rooms and i > 1
    if newest then r.fill:SetTexture(0.25, 0.32, 0.45, 0.95) else r.fill:SetTexture(0.16, 0.2, 0.28, 0.95) end
    r.label:SetText((kind and kind.name or "Room") .. (newest and "\n|cffa0a0a0newest|r" or ""))
    r:Show()
  end

  for i, socket in ipairs(layout.sockets) do
    local b = Plus(i)
    local x, y = ToCanvas(socket.x + socket.nx * 3, socket.y + socket.ny * 3)
    b.socket = socket
    b:ClearAllPoints(); b:SetPoint("CENTER", canvas, "BOTTOMLEFT", x, y)
    if socket == chosen then b:LockHighlight() else b:UnlockHighlight() end
    b:Show()
  end

  count:SetText(#layout.rooms >= layout.maxRooms
    and string.format("|cffff8040Rooms %d / %d: full. Remove a room to build elsewhere.|r", #layout.rooms, layout.maxRooms)
    or string.format("Rooms %d / %d", #layout.rooms, layout.maxRooms))
  if #layout.rooms > 1 then remove:Enable() else remove:Disable() end

  if chosen then
    for i, id in ipairs(chosen.fits) do
      local kind = layout.kinds[id]
      local b = KindButton(i)
      b.kind = id
      b:SetText(kind and string.format("%s  |cffa0a0a0%dx%d|r", kind.name, kind.sizeX, kind.sizeY) or ("Room " .. id))
      b:Show()
    end
    picker:SetHeight(44 + #chosen.fits * 24)
    picker:Show()
  end
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
f:SetScript("OnHide", function() chosen = nil; picker:Hide(); StaticPopup_Hide("ESTERIA_HOUSING_REMOVE_ROOM") end)
f:RegisterEvent("ESTERIA_HOUSING_LAYOUT")
f:SetScript("OnEvent", function()
  -- a new layout invalidates the chosen wall
  chosen = nil
  H.RedrawLayout()
end)

function H.ShowLayout(show)
  if show then f:Show() else f:Hide() end
end
