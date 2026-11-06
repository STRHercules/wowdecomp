-- Esteria housing: the House Editor. Inside a house a bar sits above the action bars: Decor and Layout for the owner,
-- Leave for everyone. Decor mode is a catalog of 3D previews; pick one and click the floor to place it, or Select and
-- click placed decor to move, turn, raise or store it. Everything goes through the Housing extension and the server
-- validates every edit; the UI only redraws from the server's answers.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local TURN, ZSTEP, PICK = 15, 0.1, 2.5
local COLS, ROWS = 3, 4
local RESULT = {
  [1] = "Housing is disabled on this realm.", [2] = "You are not in a house.", [3] = "Only the owner can edit this house.",
  [4] = "Duplicate request ignored.", [5] = "The house changed; resynced. Try again.", [6] = "Unknown decor.",
  [7] = "You have none of that decor left.", [8] = "That decor is no longer placed.", [9] = "That spot is outside your rooms.",
  [10] = "Invalid position or rotation.", [11] = "The house is full.", [12] = "Too fast; slow down.",
  [13] = "The server did not answer.", [14] = "Unknown edit.",
}
local CATEGORIES = { { nil, "All" }, { 1, "Furniture" }, { 2, "Accents" }, { 3, "Lighting" }, { 4, "Nature" },
  { 5, "Structure" }, { 0, "Misc" } }

local catalog, state, names, fdids = {}, nil, {}, {}
local mode            -- nil, "decor" or "layout"
local tool            -- nil, "place" (holding a catalog decor), "move" (holding a placed one) or "select"
local holding         -- { decorId, placedId, yaw }
local selected        -- placedId
local category, search, page = nil, "", 0

function H.Toast(text) UIErrorsFrame:AddMessage(text, 1, 0.2, 0.2, 1) end
local Toast = H.Toast

function H.Panel(name, w, h)
  local f = CreateFrame("Frame", name, UIParent)
  f:SetSize(w, h)
  f:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16, insets = { left = 4, right = 4, top = 4, bottom = 4 } })
  f:SetBackdropBorderColor(0.8, 0.65, 0.3)
  f:EnableMouse(true)
  f:Hide()
  return f
end

function H.Button(parent, text, w, h)
  local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
  b:SetSize(w, h or 22); b:SetText(text)
  return b
end

local function Placed(id)
  if state then for _, d in ipairs(state.decor) do if d.placedId == id then return d end end end
end

local function Owner() return state and state.inHouse and state.isOwner end

-- ------------------------------------------------------------------------------------------------ bar

local bar = H.Panel("EsteriaHousingBar", 330, 44)
bar:SetPoint("BOTTOM", 0, 150)
local barTitle = bar:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
barTitle:SetPoint("BOTTOM", bar, "TOP", 0, 2)
local decorButton = H.Button(bar, "Decor", 90, 26)
decorButton:SetPoint("LEFT", 10, 0)
local layoutButton = H.Button(bar, "Layout", 90, 26)
layoutButton:SetPoint("LEFT", decorButton, "RIGHT", 6, 0)
local leaveButton = H.Button(bar, "Leave House", 110, 26)
leaveButton:SetPoint("LEFT", layoutButton, "RIGHT", 6, 0)
leaveButton:SetScript("OnClick", function() EsteriaHousing_Leave() end)

-- ------------------------------------------------------------------------------------------------ decor catalog

local decor = H.Panel("EsteriaHousingDecor", 300, 500)
decor:SetPoint("RIGHT", -30, 20)
local decorTitle = decor:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
decorTitle:SetPoint("TOP", 0, -12); decorTitle:SetText("Decor")
local budget = decor:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
budget:SetPoint("TOP", decorTitle, "BOTTOM", 0, -2)

local tabs = {}
for i, c in ipairs(CATEGORIES) do
  local b = CreateFrame("Button", nil, decor)
  b:SetSize(i <= 4 and 68 or 90, 18)
  if i == 1 then b:SetPoint("TOPLEFT", 14, -46)
  elseif i == 5 then b:SetPoint("TOPLEFT", tabs[1], "BOTTOMLEFT", 0, -2)
  else b:SetPoint("LEFT", tabs[i - 1], "RIGHT", 2, 0) end
  b.bg = b:CreateTexture(nil, "BACKGROUND"); b.bg:SetAllPoints(); b.bg:SetTexture(1, 1, 1, 0.08)
  b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
  b.text = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); b.text:SetAllPoints(); b.text:SetText(c[2])
  b:SetScript("OnClick", function() category = c[1]; page = 0; H.RedrawDecor() end)
  b.category = c[1]
  tabs[i] = b
end

local searchBox = CreateFrame("EditBox", "EsteriaHousingDecorSearch", decor, "InputBoxTemplate")
searchBox:SetSize(260, 20); searchBox:SetPoint("TOPLEFT", 22, -90); searchBox:SetAutoFocus(false)
searchBox:SetScript("OnTextChanged", function(self) search = self:GetText():lower(); page = 0; H.RedrawDecor() end)
searchBox:SetScript("OnEscapePressed", searchBox.ClearFocus)
searchBox:SetScript("OnEnterPressed", searchBox.ClearFocus)
local searchHint = searchBox:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
searchHint:SetPoint("LEFT", 4, 0); searchHint:SetText("Search")
searchBox:SetScript("OnEditFocusGained", function() searchHint:Hide() end)
searchBox:SetScript("OnEditFocusLost", function(self) if self:GetText() == "" then searchHint:Show() end end)

local function FitModel(model)
  model:SetScript("OnUpdate", function(self)
    if not RetailModelReportLoaded then self:SetScript("OnUpdate", nil); return end
    if RetailModelReportLoaded(self) then self:SetScript("OnUpdate", nil) end
  end)
end

local tiles = {}
for i = 1, COLS * ROWS do
  local t = CreateFrame("Button", nil, decor)
  t:SetSize(88, 90)
  t:SetPoint("TOPLEFT", 12 + ((i - 1) % COLS) * 92, -118 - math.floor((i - 1) / COLS) * 94)
  t.bg = t:CreateTexture(nil, "BACKGROUND"); t.bg:SetAllPoints(); t.bg:SetTexture(0, 0, 0, 0.45)
  t:SetHighlightTexture("Interface\\Buttons\\ButtonHilight-Square", "ADD")
  t.model = CreateFrame("Model", nil, t)
  t.model:SetPoint("TOPLEFT", 2, -2); t.model:SetPoint("BOTTOMRIGHT", -2, 22)
  t.name = t:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  t.name:SetPoint("BOTTOMLEFT", 3, 3); t.name:SetPoint("BOTTOMRIGHT", -3, 3); t.name:SetHeight(20)
  t.count = t:CreateFontString(nil, "OVERLAY", "NumberFontNormal")
  t.count:SetPoint("TOPRIGHT", -4, -4)
  t:SetScript("OnClick", function(self)
    if not self.entry then return end
    if self.entry.available < 1 then return Toast("You have none of that left to place.") end
    H.Hold({ decorId = self.entry.decorId })
  end)
  t:SetScript("OnEnter", function(self)
    if not self.entry then return end
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    GameTooltip:AddLine(self.entry.name)
    GameTooltip:AddLine(string.format("In storage: %d    Placed: %d", self.entry.available, self.entry.placed), 1, 1, 1)
    GameTooltip:AddLine("Click, then click the floor to place it.", 0.5, 0.8, 1)
    GameTooltip:Show()
  end)
  t:SetScript("OnLeave", function() GameTooltip:Hide() end)
  tiles[i] = t
end

local prev = H.Button(decor, "<", 28, 22); prev:SetPoint("BOTTOMLEFT", 14, 44)
local nextButton = H.Button(decor, ">", 28, 22); nextButton:SetPoint("BOTTOMRIGHT", -14, 44)
local pageText = decor:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
pageText:SetPoint("BOTTOM", 0, 50)
local selectButton = H.Button(decor, "Select Placed Decor", 180, 24); selectButton:SetPoint("BOTTOM", 0, 14)
selectButton:SetScript("OnClick", function() if tool == "select" then H.Drop() else H.Hold(nil, "select") end end)

local function Filtered()
  local list = {}
  for _, e in ipairs(catalog) do
    if (category == nil or e.category == category) and (search == "" or e.name:lower():find(search, 1, true)) then
      list[#list + 1] = e
    end
  end
  return list
end

prev:SetScript("OnClick", function() page = math.max(0, page - 1); H.RedrawDecor() end)
nextButton:SetScript("OnClick", function() if (page + 1) * COLS * ROWS < #Filtered() then page = page + 1 end; H.RedrawDecor() end)

function H.RedrawDecor()
  if not decor:IsShown() then return end
  local list = Filtered()
  local per = COLS * ROWS
  page = math.min(page, math.max(0, math.ceil(#list / per) - 1))
  for i, t in ipairs(tiles) do
    local e = list[page * per + i]
    t.entry = e
    if e then
      t.name:SetText(e.name)
      t.count:SetText(e.available)
      t:SetAlpha(e.available > 0 and 1 or 0.45)
      if t.fdid ~= e.fileDataId then
        t.fdid = e.fileDataId
        t.model:ClearModel()
        t.model:SetModel("retail/" .. e.fileDataId .. ".m2")
        FitModel(t.model)
      end
      t:Show()
    else
      t:Hide()
    end
  end
  for _, b in ipairs(tabs) do
    if b.category == category then b.bg:SetTexture(1, 0.82, 0, 0.35) else b.bg:SetTexture(1, 1, 1, 0.08) end
  end
  pageText:SetText(#list > 0 and string.format("Page %d of %d", page + 1, math.ceil(#list / per)) or "No decor matches.")
  if state then budget:SetText(string.format("Placed %d / %d", state.placed, state.maxDecor)) end
  if tool == "select" then selectButton:LockHighlight() else selectButton:UnlockHighlight() end
end
decor:SetScript("OnShow", H.RedrawDecor)

-- ------------------------------------------------------------------------------------------------ selected decor

local sel = H.Panel("EsteriaHousingSelected", 330, 70)
sel:SetPoint("BOTTOM", bar, "TOP", 0, 18)
local selName = sel:CreateFontString(nil, "OVERLAY", "GameFontNormal")
selName:SetPoint("TOP", 0, -10)
local function SelButton(text, x, onClick)
  local b = H.Button(sel, text, 50, 22); b:SetPoint("BOTTOMLEFT", 10 + x, 10); b:SetScript("OnClick", onClick)
  return b
end

local function Edit(d, x, y, z, yaw)
  if not EsteriaHousing_Move(d.placedId, x, y, z, yaw) then Toast("You can only edit your own house.") end
end
SelButton("Move", 0, function() local d = Placed(selected); if d then H.Hold({ placedId = d.placedId, decorId = d.decorId, yaw = d.yaw }, "move") end end)
SelButton("Turn L", 52, function() local d = Placed(selected); if d then Edit(d, d.x, d.y, d.z, d.yaw + TURN) end end)
SelButton("Turn R", 104, function() local d = Placed(selected); if d then Edit(d, d.x, d.y, d.z, d.yaw - TURN) end end)
SelButton("Up", 156, function() local d = Placed(selected); if d then Edit(d, d.x, d.y, d.z + ZSTEP, d.yaw) end end)
SelButton("Down", 208, function() local d = Placed(selected); if d then Edit(d, d.x, d.y, d.z - ZSTEP, d.yaw) end end)
SelButton("Store", 260, function()
  local d = Placed(selected)
  if d and not EsteriaHousing_Remove(d.placedId) then Toast("You can only edit your own house.") end
end)

local function RedrawSelected()
  local d = Owner() and mode == "decor" and Placed(selected)
  if not d then return sel:Hide() end
  selName:SetText(string.format("%s  |cffa0a0a0%d\194\176|r", names[d.decorId] or ("Decor " .. d.decorId), d.yaw % 360))
  sel:Show()
end

-- ------------------------------------------------------------------------------------------------ world clicks

-- While holding or selecting, a click-catcher covers the world (under every panel) so clicks place instead of target.
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

function H.Hold(what, which)
  holding = what
  tool = which or (what and "place")
  if what and not what.yaw then
    local _, _, _, facing = EsteriaHousing_GetPlayerPosition()
    what.yaw = math.floor(((facing or 0) + 180) / TURN + 0.5) * TURN -- face the player, on the server's 15 degree snap
  end
  -- the ghost: a see-through copy under the cursor that the click drops in place
  if EsteriaHousing_SetGhost then EsteriaHousing_SetGhost(what and fdids[what.decorId] or nil, what and what.yaw) end
  if tool then catcher:Show() else catcher:Hide() end
  H.RedrawDecor()
end

function H.Drop() H.Hold(nil) end

catcher:SetScript("OnClick", function(self, button)
  if button == "RightButton" then return H.Drop() end
  local x, y, z
  if holding and EsteriaHousing_GetGhost then x, y, z = EsteriaHousing_GetGhost() end -- exactly where the ghost stands
  if not x then x, y, z = CursorFloor() end
  if not x then return Toast("Point at the floor.") end
  if tool == "place" then
    if not EsteriaHousing_Place(holding.decorId, x, y, z, holding.yaw) then Toast("You can only edit your own house.") end
    if not IsShiftKeyDown() then H.Drop() end -- shift-click keeps placing copies
  elseif tool == "move" then
    local d = Placed(holding.placedId)
    if d then Edit(d, x, y, z, holding.yaw) end
    H.Drop()
  elseif tool == "select" then
    local best, bestDistance = nil, PICK * PICK
    for _, d in ipairs(state and state.decor or {}) do
      local distance = (d.x - x) ^ 2 + (d.y - y) ^ 2
      if distance < bestDistance and math.abs(d.z - z) < 3 then best, bestDistance = d, distance end
    end
    selected = best and best.placedId
    if not best then Toast("No decor there.") end
    RedrawSelected()
  end
end)

catcher:SetScript("OnMouseWheel", function(self, delta)
  if holding then
    holding.yaw = holding.yaw + delta * TURN
    if EsteriaHousing_SetGhost then EsteriaHousing_SetGhost(fdids[holding.decorId], holding.yaw) end
  elseif tool == "select" then
    local d = Placed(selected)
    if d then Edit(d, d.x, d.y, d.z, d.yaw + delta * TURN) end
  end
end)

catcher:SetScript("OnUpdate", function(self)
  local scale = UIParent:GetEffectiveScale()
  local cx, cy = GetCursorPosition()
  hint:ClearAllPoints()
  hint:SetPoint("BOTTOMLEFT", UIParent, "BOTTOMLEFT", cx / scale + 24, cy / scale - 8)
  if tool == "select" then
    hint:SetText("Click decor to select it\n|cffa0a0a0Wheel: turn the selection    Right-click: done|r")
  elseif holding then
    hint:SetText(string.format("%s  %d\194\176\n|cffa0a0a0Click: %s    Wheel: turn    %sRight-click: cancel|r",
      names[holding.decorId] or "Decor", holding.yaw % 360, tool == "move" and "move here" or "place",
      tool == "place" and "Shift-click: place more    " or ""))
  end
end)

-- ------------------------------------------------------------------------------------------------ modes

function H.SetMode(m)
  if m == mode then m = nil end
  mode = m
  H.Drop()
  if mode == "decor" then decor:Show() else decor:Hide() end
  if H.ShowLayout then H.ShowLayout(mode == "layout") end
  if mode == "decor" then decorButton:LockHighlight() else decorButton:UnlockHighlight() end
  if mode == "layout" then layoutButton:LockHighlight() else layoutButton:UnlockHighlight() end
  RedrawSelected()
end
decorButton:SetScript("OnClick", function() H.SetMode("decor") end)
layoutButton:SetScript("OnClick", function() H.SetMode("layout") end)

local function RedrawBar()
  if not (state and state.inHouse) then
    if mode then H.SetMode(mode) end -- closes it
    bar:Hide(); return
  end
  bar:Show()
  if state.isOwner then
    barTitle:SetText("House Editor")
    decorButton:Show(); layoutButton:Show()
    leaveButton:ClearAllPoints(); leaveButton:SetPoint("LEFT", layoutButton, "RIGHT", 6, 0)
    bar:SetWidth(320)
  else
    barTitle:SetText("Visiting")
    if mode then H.SetMode(mode) end
    decorButton:Hide(); layoutButton:Hide()
    leaveButton:ClearAllPoints(); leaveButton:SetPoint("CENTER")
    bar:SetWidth(130)
  end
end

-- ------------------------------------------------------------------------------------------------ events

bar:RegisterEvent("ESTERIA_HOUSING_AVAILABLE")
bar:RegisterEvent("ESTERIA_HOUSING_CATALOG")
bar:RegisterEvent("ESTERIA_HOUSING_STATE")
bar:RegisterEvent("ESTERIA_HOUSING_RESULT")
bar:SetScript("OnEvent", function(self, event, result, op, placedId)
  if event == "ESTERIA_HOUSING_CATALOG" then
    catalog = EsteriaHousing_GetCatalog()
    table.sort(catalog, function(a, b) return a.name < b.name end)
    for _, e in ipairs(catalog) do names[e.decorId] = e.name; fdids[e.decorId] = e.fileDataId end
  elseif event == "ESTERIA_HOUSING_STATE" or event == "ESTERIA_HOUSING_AVAILABLE" then
    state = EsteriaHousing_GetState()
    if not Placed(selected) then selected = nil end
    if holding and holding.placedId and not Placed(holding.placedId) then H.Drop() end
    RedrawBar()
  elseif event == "ESTERIA_HOUSING_RESULT" then
    if result ~= 0 then Toast(RESULT[result] or ("Housing error " .. result))
    elseif op == 1 then selected = placedId end
    state = EsteriaHousing_GetState()
  end
  H.RedrawDecor()
  RedrawSelected()
end)

SLASH_ESTERIAHOUSING1 = "/housing"
SlashCmdList.ESTERIAHOUSING = function()
  if not EsteriaHousing_IsAvailable() then return Toast("Housing is off (set housingEnabled 1, and the realm must enable it).") end
  if not Owner() then return Toast("Open the editor inside your own house.") end
  H.SetMode(mode or "decor")
end
