-- Draenor Garrison Landing Page: a compact report of resources, buildings, missions and followers.
-- Art: retail GarrisonLandingPage.blp / middle tile. Opens with /garrisonlanding or from the building window.

EsteriaGarrisonUI = EsteriaGarrisonUI or {}
local G = EsteriaGarrisonUI

local landing, body, title, resources, status
local parsed = { buildings = {}, missions = {}, followers = {}, orders = {} }

local function hello()
  if EsteriaGarrison_Hello then EsteriaGarrison_Hello() end
end

local function act(op, a, b)
  if EsteriaGarrison_Action then EsteriaGarrison_Action(op, a or 0, b or 0) end
end

local function split(text, sep)
  local out, start = {}, 1
  while true do
    local found = string.find(text, sep, start, true)
    if not found then out[#out + 1] = string.sub(text, start); return out end
    out[#out + 1] = string.sub(text, start, found - 1)
    start = found + string.len(sep)
  end
end

local function parse(text)
  parsed = { owned = false, horde = false, level = 0, resources = 0, pending = 0,
    buildings = {}, missions = {}, followers = {}, orders = {}, recruits = {} }
  for line in string.gmatch(text or "", "[^\n]+") do
    local row = split(line, "\t")
    local kind = row[1]
    if kind == "G" then
      parsed.owned = (tonumber(row[2]) or 0) == 1
      parsed.horde = (tonumber(row[3]) or 0) == 1
      parsed.level = tonumber(row[4]) or 0
      parsed.resources = tonumber(row[5]) or 0
      parsed.pending = tonumber(row[6]) or 0
    elseif kind == "P" then
      parsed.buildings[#parsed.buildings + 1] = {
        name = row[7] or "", building = tonumber(row[4]) or 0,
        active = (tonumber(row[5]) or 0) == 1, left = tonumber(row[6]) or 0,
      }
    elseif kind == "M" then
      parsed.missions[#parsed.missions + 1] = {
        db = tonumber(row[2]) or 0, name = row[7] or "", st = tonumber(row[4]) or 0,
        left = tonumber(row[5]) or 0, success = tonumber(row[6]) or 0,
      }
    elseif kind == "F" then
      parsed.followers[#parsed.followers + 1] = {
        name = row[7] or "", level = tonumber(row[4]) or 0, mission = tonumber(row[6]) or 0,
      }
    elseif kind == "O" then
      parsed.orders[#parsed.orders + 1] = {
        db = tonumber(row[2]) or 0, plot = tonumber(row[3]) or 0, left = tonumber(row[4]) or 0,
      }
    end
  end
end

local function clear()
  body.pool = body.pool or {}
  body.used = 0
end

local function take()
  body.used = body.used + 1
  local row = body.pool[body.used]
  if not row then
    row = CreateFrame("Button", nil, body)
    row:SetSize(360, 22)
    row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    row.text:SetPoint("LEFT", 4, 0)
    row.text:SetPoint("RIGHT", -4, 0)
    row.text:SetJustifyH("LEFT")
    body.pool[body.used] = row
  end
  row:Show()
  return row
end

local function hideRest()
  for i = body.used + 1, #(body.pool or {}) do body.pool[i]:Hide() end
end

local function add(text, click)
  local row = take()
  row:ClearAllPoints()
  row:SetPoint("TOPLEFT", 0, -((body.used - 1) * 22))
  row.text:SetText(text)
  row:SetScript("OnClick", click or function() end)
end

local function refresh()
  if not EsteriaGarrison_GetState then return end
  parse(EsteriaGarrison_GetState())
  local side = parsed.horde and "Frostwall" or "Lunarfall"
  if not parsed.owned then
    title:SetText("Garrison Report")
    resources:SetText("No garrison yet.")
    clear(); hideRest(); body:SetHeight(1)
    return
  end
  title:SetText(string.format("%s Report  ·  Level %d", side, parsed.level))
  resources:SetText(string.format("Resources %d    Cache ready %d", parsed.resources, parsed.pending))
  clear()
  add("--- Buildings ---")
  for _, b in ipairs(parsed.buildings) do
    if b.building ~= 0 then
      if b.active then add("  " .. b.name)
      else add(string.format("  %s  (building %ds)", b.name, b.left)) end
    end
  end
  add("--- Work orders ---")
  local anyOrder = false
  for _, o in ipairs(parsed.orders) do
    anyOrder = true
    if o.left > 0 then add(string.format("  Plot %d  %ds", o.plot, o.left))
    else add(string.format("  Collect plot %d", o.plot), function() act(10, o.db) end) end
  end
  if not anyOrder then add("  None") end
  add("--- Missions ---")
  local anyMission = false
  for _, m in ipairs(parsed.missions) do
    if m.st == 1 then
      anyMission = true
      if m.left > 0 then add(string.format("  %s  %ds", m.name, m.left))
      else add(string.format("  Claim %s", m.name), function() act(8, m.db) end) end
    end
  end
  if not anyMission then add("  None in progress") end
  add("--- Followers ---")
  for _, f in ipairs(parsed.followers) do
    add(string.format("  %s  L%d%s", f.name, f.level, f.mission ~= 0 and " (away)" or ""))
  end
  if #parsed.followers == 0 then add("  None recruited") end
  hideRest()
  body:SetHeight(math.max(body.used * 22, 1))
end

local function show()
  landing:Show()
  hello()
  refresh()
end

landing = CreateFrame("Frame", "EsteriaGarrisonLanding", UIParent)
landing:SetSize(420, 520)
landing:SetPoint("CENTER", 220, 40)
landing:EnableMouse(true)
landing:SetMovable(true)
landing:SetClampedToScreen(true)
landing:RegisterForDrag("LeftButton")
landing:SetScript("OnDragStart", landing.StartMoving)
landing:SetScript("OnDragStop", landing.StopMovingOrSizing)
landing:Hide()

-- garrisonlandingpage is retail's atlas sheet, and Atlas.lua has no texcoords to cut it, so the frame is the
-- Neighborhood window's panel (EsteriaHousing) around retail's standalone middle tile
landing:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background-Dark",
  edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32, edgeSize = 32,
  insets = { left = 11, right = 12, top = 12, bottom = 11 } })
local mid = landing:CreateTexture(nil, "BORDER")
if G.Path then mid:SetTexture(G.Path("garrisonlandingpagemiddletile")) end
mid:SetPoint("TOPLEFT", 14, -64)
mid:SetPoint("BOTTOMRIGHT", -14, 44)
mid:SetVertexColor(0.75, 0.75, 0.75, 1)
local inset = CreateFrame("Frame", nil, landing)
inset:SetPoint("TOPLEFT", mid, -2, 2); inset:SetPoint("BOTTOMRIGHT", mid, 2, -2)
inset:SetBackdrop({ edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 12 })
inset:SetBackdropBorderColor(0.55, 0.42, 0.25)

title = landing:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOP", 0, -28)
title:SetText("Garrison Report")
resources = landing:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
resources:SetPoint("TOP", title, "BOTTOM", 0, -6)
status = landing:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
status:SetPoint("BOTTOM", 0, 14)

local close = CreateFrame("Button", nil, landing, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -8, -8)

local enter = CreateFrame("Button", nil, landing, "UIPanelButtonTemplate")
enter:SetSize(100, 22)
enter:SetPoint("BOTTOMLEFT", 20, 14)
enter:SetText("Enter")
enter:SetScript("OnClick", function() act(1) end)

local cache = CreateFrame("Button", nil, landing, "UIPanelButtonTemplate")
cache:SetSize(100, 22)
cache:SetPoint("LEFT", enter, "RIGHT", 8, 0)
cache:SetText("Cache")
cache:SetScript("OnClick", function() act(6); hello() end)

local openBuild = CreateFrame("Button", nil, landing, "UIPanelButtonTemplate")
openBuild:SetSize(120, 22)
openBuild:SetPoint("LEFT", cache, "RIGHT", 8, 0)
openBuild:SetText("Architect")
openBuild:SetScript("OnClick", function()
  landing:Hide()
  if SlashCmdList["ESTERIA_GARRISON"] then SlashCmdList["ESTERIA_GARRISON"]() end
end)

local scroll = CreateFrame("ScrollFrame", nil, landing)
scroll:SetPoint("TOPLEFT", 36, -90)
scroll:SetPoint("BOTTOMRIGHT", -36, 48)
scroll:EnableMouseWheel(true)
body = CreateFrame("Frame", nil, scroll)
body:SetSize(340, 300)
scroll:SetScrollChild(body)
scroll:SetScript("OnMouseWheel", function(self, delta)
  local height = math.max(0, body:GetHeight() - self:GetHeight())
  self:SetVerticalScroll(math.max(0, math.min(height, self:GetVerticalScroll() - delta * 28)))
end)

landing:SetScript("OnEvent", function(_, event)
  if event == "ESTERIA_GARRISON_STATE" and landing:IsShown() then refresh() end
end)
landing:RegisterEvent("ESTERIA_GARRISON_STATE")

SLASH_ESTERIA_GARRISON_LANDING1 = "/garrisonlanding"
SLASH_ESTERIA_GARRISON_LANDING2 = "/garr"
SlashCmdList["ESTERIA_GARRISON_LANDING"] = function()
  if landing:IsShown() then landing:Hide() else show() end
end

G.ShowLanding = show
