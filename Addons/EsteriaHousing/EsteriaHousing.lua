-- Esteria housing, Phase 1 slice: catalog list, place, select, nudge, rotate, remove, error toasts. No polish.
-- Everything goes through the Housing extension (wowdecomp Extensions/Housing); the server validates every edit and
-- the panel only redraws from the server's answers.

if not EsteriaHousing_IsAvailable then return end

local STEP, ZSTEP, TURN, ROWS = 0.25, 0.1, 15, 12
local RESULT = {
  [1] = "Housing is disabled on this realm.", [2] = "You are not in a house.", [3] = "Only the owner can edit this house.",
  [4] = "Duplicate request ignored.", [5] = "The house changed; resynced. Try again.", [6] = "Unknown decor.",
  [7] = "You have none of that decor left.", [8] = "That decor is no longer placed.", [9] = "Out of the room's bounds.",
  [10] = "Invalid position or rotation.", [11] = "The house is full.", [12] = "Too fast; slow down.",
  [13] = "The server did not answer.", [14] = "Unknown edit.",
}

local catalog, state, names = {}, nil, {}
local selectedDecor, selectedPlaced, scroll, userClosed = nil, nil, 0, false

local f = CreateFrame("Frame", "EsteriaHousingFrame", UIParent)
f:SetSize(300, 470)
f:SetPoint("RIGHT", -40, 0)
f:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background", edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border",
  tile = true, tileSize = 32, edgeSize = 24, insets = { left = 6, right = 6, top = 6, bottom = 6 } })
f:SetMovable(true); f:EnableMouse(true); f:RegisterForDrag("LeftButton")
f:SetScript("OnDragStart", f.StartMoving); f:SetScript("OnDragStop", f.StopMovingOrSizing)
f:Hide()
tinsert(UISpecialFrames, "EsteriaHousingFrame")

local title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -14); title:SetText("Housing")
local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -4, -4)
close:SetScript("OnClick", function() userClosed = true; f:Hide() end)
local status = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
status:SetPoint("TOP", 0, -36)

local function Button(text, width, x, y, onClick)
  local b = CreateFrame("Button", nil, f, "UIPanelButtonTemplate")
  b:SetSize(width, 22); b:SetPoint("TOPLEFT", x, y); b:SetText(text); b:SetScript("OnClick", onClick)
  return b
end

local function Toast(text) UIErrorsFrame:AddMessage(text, 1, 0.2, 0.2, 1) end

local function Placed(id)
  if state then for _, d in ipairs(state.decor) do if d.placedId == id then return d end end end
end

local function Sorted()
  local px, py, pz = EsteriaHousing_GetPlayerPosition()
  local list = {}
  if state and px then
    for _, d in ipairs(state.decor) do
      d.distance = (d.x - px) ^ 2 + (d.y - py) ^ 2 + (d.z - pz) ^ 2
      list[#list + 1] = d
    end
    table.sort(list, function(a, b) return a.distance < b.distance end)
  end
  return list
end

-- catalog rows
local rows = {}
for i = 1, ROWS do
  local r = CreateFrame("Button", nil, f)
  r:SetSize(260, 16); r:SetPoint("TOPLEFT", 20, -52 - (i - 1) * 17)
  r:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
  r.text = r:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  r.text:SetAllPoints(); r.text:SetJustifyH("LEFT")
  r:SetScript("OnClick", function(self) selectedDecor = self.decorId; EsteriaHousingFrame_Redraw() end)
  rows[i] = r
end
Button("<", 30, 20, -258, function() scroll = math.max(0, scroll - ROWS); EsteriaHousingFrame_Redraw() end)
Button(">", 30, 250, -258, function() if scroll + ROWS < #catalog then scroll = scroll + ROWS end; EsteriaHousingFrame_Redraw() end)
local pageText = f:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
pageText:SetPoint("TOP", 0, -264)

Button("Place", 120, 20, -286, function()
  if not selectedDecor then return Toast("Pick a decor first.") end
  local x, y, z, facing = EsteriaHousing_GetPlayerPosition()
  if not x then return end
  local o = math.rad(facing)
  if not EsteriaHousing_Place(selectedDecor, x + 2.5 * math.cos(o), y + 2.5 * math.sin(o), z, facing + 180) then
    Toast("You can only edit your own house.")
  end
end)

local selectedText = f:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
selectedText:SetPoint("TOPLEFT", 20, -318); selectedText:SetWidth(260); selectedText:SetJustifyH("LEFT")

local function Cycle(delta)
  local list = Sorted()
  if #list == 0 then selectedPlaced = nil; return EsteriaHousingFrame_Redraw() end
  local at = 0
  for i, d in ipairs(list) do if d.placedId == selectedPlaced then at = i end end
  at = delta == 0 and 1 or ((at - 1 + delta) % #list) + 1
  selectedPlaced = list[at].placedId
  EsteriaHousingFrame_Redraw()
end
Button("Nearest", 80, 20, -336, function() Cycle(0) end)
Button("Prev", 80, 110, -336, function() Cycle(-1) end)
Button("Next", 80, 200, -336, function() Cycle(1) end)

-- dx, dy are forward/right relative to the player's facing; dz up; turn in degrees
local function Nudge(forward, right, dz, turn)
  local d = Placed(selectedPlaced)
  if not d then return Toast("Select a placed decor first.") end
  local _, _, _, facing = EsteriaHousing_GetPlayerPosition()
  local o = math.rad(facing or 0)
  local x = d.x + forward * math.cos(o) + right * math.sin(o)
  local y = d.y + forward * math.sin(o) - right * math.cos(o)
  if not EsteriaHousing_Move(d.placedId, x, y, d.z + dz, d.yaw + turn) then Toast("You can only edit your own house.") end
end
Button("Fwd", 60, 20, -362, function() Nudge(STEP, 0, 0, 0) end)
Button("Back", 60, 85, -362, function() Nudge(-STEP, 0, 0, 0) end)
Button("Left", 60, 150, -362, function() Nudge(0, -STEP, 0, 0) end)
Button("Right", 60, 215, -362, function() Nudge(0, STEP, 0, 0) end)
Button("Up", 60, 20, -388, function() Nudge(0, 0, ZSTEP, 0) end)
Button("Down", 60, 85, -388, function() Nudge(0, 0, -ZSTEP, 0) end)
Button("-15", 60, 150, -388, function() Nudge(0, 0, 0, -TURN) end)
Button("+15", 60, 215, -388, function() Nudge(0, 0, 0, TURN) end)
Button("Move here", 120, 20, -414, function()
  local d = Placed(selectedPlaced)
  if not d then return Toast("Select a placed decor first.") end
  local x, y, z = EsteriaHousing_GetPlayerPosition()
  if not EsteriaHousing_Move(d.placedId, x, y, z, d.yaw) then Toast("You can only edit your own house.") end
end)
Button("Remove", 120, 160, -414, function()
  local d = Placed(selectedPlaced)
  if not d then return Toast("Select a placed decor first.") end
  if not EsteriaHousing_Remove(d.placedId) then Toast("You can only edit your own house.") end
end)
Button("Leave house", 120, 90, -440, function() EsteriaHousing_Leave() end)

function EsteriaHousingFrame_Redraw()
  for i = 1, ROWS do
    local e, r = catalog[scroll + i], rows[i]
    if e then
      r.decorId = e.decorId
      local color = e.decorId == selectedDecor and "|cffffd100" or (e.available > 0 and "|cffffffff" or "|cff808080")
      r.text:SetText(string.format("%s%s|r  (%d left, %d placed)", color, e.name, e.available, e.placed))
      r:Show()
    else
      r:Hide()
    end
  end
  pageText:SetText(string.format("%d-%d of %d", math.min(scroll + 1, #catalog), math.min(scroll + ROWS, #catalog), #catalog))
  if state and state.inHouse then
    status:SetText(string.format("%s  -  decor %d/%d  -  rev %d%s", state.isOwner and "Your house" or "Visiting",
      state.placed, state.maxDecor, state.revision, state.pending > 0 and ("  -  sending " .. state.pending) or ""))
  else
    status:SetText("Not in a house. Talk to a Housing Steward.")
  end
  local d = Placed(selectedPlaced)
  selectedText:SetText(d and string.format("Selected: %s #%d (%.1f, %.1f, %.1f) %d\194\176", names[d.decorId] or d.decorId,
    d.placedId, d.x, d.y, d.z, d.yaw) or "Selected: nothing")
end

f:RegisterEvent("ESTERIA_HOUSING_AVAILABLE")
f:RegisterEvent("ESTERIA_HOUSING_CATALOG")
f:RegisterEvent("ESTERIA_HOUSING_STATE")
f:RegisterEvent("ESTERIA_HOUSING_RESULT")
f:SetScript("OnEvent", function(self, event, result, op, placedId)
  if event == "ESTERIA_HOUSING_CATALOG" then
    catalog = EsteriaHousing_GetCatalog()
    for _, e in ipairs(catalog) do names[e.decorId] = e.name end
  elseif event == "ESTERIA_HOUSING_STATE" or event == "ESTERIA_HOUSING_AVAILABLE" then
    state = EsteriaHousing_GetState()
    if state and not Placed(selectedPlaced) then selectedPlaced = nil end
    if state and state.inHouse and state.isOwner and not userClosed then f:Show() end
    if not (state and state.inHouse) then userClosed = false; f:Hide() end
  elseif event == "ESTERIA_HOUSING_RESULT" then
    if result ~= 0 then Toast(RESULT[result] or ("Housing error " .. result))
    elseif op == 1 then selectedPlaced = placedId end
    state = EsteriaHousing_GetState()
  end
  if f:IsShown() then EsteriaHousingFrame_Redraw() end
end)
f:SetScript("OnShow", EsteriaHousingFrame_Redraw)

SLASH_ESTERIAHOUSING1 = "/housing"
SlashCmdList.ESTERIAHOUSING = function()
  if not EsteriaHousing_IsAvailable() then return Toast("Housing is off (set housingEnabled 1, and the realm must enable it).") end
  if f:IsShown() then userClosed = true; f:Hide() else userClosed = false; f:Show() end
end
