-- Draenor garrison window, drawn with retail Interface/Garrison art (Textures.lua → retail/<FDID>.blp).
-- Layout mirrors Blizzard_GarrisonBuildingUI: building list, info card, map with plot pins, town hall.
-- Missions and followers are secondary pages on the same frame. Server state is EsteriaGarrison_GetState.

EsteriaGarrisonUI = EsteriaGarrisonUI or {}
local G = EsteriaGarrisonUI

local RESULT = {
  [0] = "", [1] = "No garrison yet.", [2] = "Not while you are busy.",
  [3] = "That plot is not on this garrison.", [4] = "That building does not fit this plot.",
  [5] = "You have not learned that plan.", [6] = "You already have one of those.",
  [7] = "The garrison is not a high enough level.", [8] = "Not enough garrison resources.",
  [9] = "Not enough gold.", [10] = "That plot is still under construction.",
  [11] = "The garrison is already level 3.", [12] = "Unknown mission.", [13] = "That follower cannot go.",
}

-- Retail card prefix for each building type id from the server catalog.
local CARD = {
  [1] = "mine", [2] = "farm", [3] = "barn", [4] = "lumbermill", [5] = "inn",
  [7] = "petstable", [8] = "barracks", [10] = "armory", [13] = "magetower",
  [14] = "salvageyard", [15] = "storehouse", [17] = "blacksmith", [24] = "fishing",
}

-- Plot circle size on the map for GarrPlot ids.
local PLOT_SIZE = { [12] = "sm", [13] = "md", [14] = "lg", [28] = "md", [29] = "md", [31] = "sm", [42] = "md" }

-- Normalized map positions (0..1 from bottom-left), tuned to the painted Lunarfall / Frostwall maps.
local PLOT_UI = {
  alliance = {
    [0] = { 0.432, 0.722 }, -- town hall
    [18] = { 0.55, 0.62 }, [19] = { 0.62, 0.55 }, [20] = { 0.48, 0.58 },
    [22] = { 0.70, 0.48 }, [23] = { 0.40, 0.36 }, [24] = { 0.72, 0.36 },
    [25] = { 0.58, 0.42 }, [59] = { 0.86, 0.38 }, [63] = { 0.22, 0.48 },
  },
  horde = {
    [0] = { 0.48, 0.58 },
    [18] = { 0.62, 0.70 }, [19] = { 0.68, 0.78 }, [20] = { 0.55, 0.68 },
    [22] = { 0.30, 0.72 }, [23] = { 0.42, 0.58 }, [24] = { 0.58, 0.52 },
    [25] = { 0.72, 0.58 }, [59] = { 0.22, 0.52 }, [63] = { 0.18, 0.78 },
    [67] = { 0.28, 0.88 }, [81] = { 0.55, 0.90 },
  },
}

local state = { plots = {}, buildings = {}, followers = {}, missions = {}, orders = {}, catalog = {}, recruits = {} }
local page, selectedPlot, selectedBuilding, sizeTab = "buildings", 0, 0, 12
local frame, mapFrame, listFrame, infoFrame, missionFrame, titleFS, statusFS, resourceFS

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

local function tex(target, name, w, h)
  local path = G.Path(name)
  if not path then return nil end
  target:SetTexture(path)
  if w then target:SetSize(w, h or w) end
  return target
end

local function card(kind, level, horde, which)
  local side = horde and "h" or "a"
  local prefix = CARD[kind]
  if not prefix then return nil end
  local name = string.format("garrbuilding%s%d%s%s", prefix, level or 1, side, which or "info")
  if G.TEX[name] then return name end
  name = string.format("garrbuilding%s1%s%s", prefix, side, which or "info")
  if G.TEX[name] then return name end
  name = string.format("garrbuilding%s%d%s%s", prefix, level or 1, "a", which or "info")
  return G.TEX[name] and name or nil
end

local function emptyCard(garrPlot, horde)
  local size = PLOT_SIZE[garrPlot] or "sm"
  local side = horde and "h" or "a"
  local name = string.format("garrbuildingemptyplot%s%sinfo", size, side)
  return G.TEX[name] and name or string.format("garrbuildingemptyplot%sainfo", size)
end

local function mapName(level, horde)
  if horde then
    if level >= 3 then return "garrisonmapfrostfire4h" end
    if level == 2 then return "garrisonmapfrostfire2h" end
    return "garrisonmapfrostfire1h"
  end
  if level >= 3 then return "garrisonmapshadowmoon4a" end
  if level == 2 then return "garrisonmapshadowmoon2a" end
  return "garrisonmapshadowmoon1a"
end

local function townHallMap(level, horde)
  return string.format("garrbuildingtownhall%d%smap", level or 1, horde and "h" or "a")
end

local function townHallInfo(level, horde)
  return string.format("garrbuildingtownhall%d%sinfo", level or 1, horde and "h" or "a")
end

local function parse(text)
  state = { plots = {}, buildings = {}, followers = {}, missions = {}, orders = {}, catalog = {}, recruits = {},
    owned = false, horde = false, level = 0, resources = 0, pending = 0, site = 0, map = 0, upgradeRes = 0, upgradeGold = 0, result = 0 }
  for line in string.gmatch(text or "", "[^\n]+") do
    local row = split(line, "\t")
    local kind = row[1]
    if kind == "R" then
      state.result = tonumber(row[2]) or 0
    elseif kind == "G" then
      state.owned = (tonumber(row[2]) or 0) == 1
      state.horde = (tonumber(row[3]) or 0) == 1
      state.level = tonumber(row[4]) or 0
      state.resources = tonumber(row[5]) or 0
      state.pending = tonumber(row[6]) or 0
      state.site = tonumber(row[7]) or 0
      state.map = tonumber(row[8]) or 0
      state.upgradeRes = tonumber(row[9]) or 0
      state.upgradeGold = tonumber(row[10]) or 0
      state.townX = tonumber(row[11])
      state.townY = tonumber(row[12])
    elseif kind == "P" then
      state.plots[#state.plots + 1] = {
        id = tonumber(row[2]) or 0, garr = tonumber(row[3]) or 0, building = tonumber(row[4]) or 0,
        active = (tonumber(row[5]) or 0) == 1, left = tonumber(row[6]) or 0, name = row[7] or "",
        x = tonumber(row[8]), y = tonumber(row[9]), marker = tonumber(row[10]) or 0,
      }
    elseif kind == "B" then
      local b = {
        id = tonumber(row[2]) or 0, type = tonumber(row[3]) or 0, level = tonumber(row[4]) or 0,
        garr = tonumber(row[5]) or 0, resources = tonumber(row[6]) or 0, gold = tonumber(row[7]) or 0,
        seconds = tonumber(row[8]) or 0, known = (tonumber(row[9]) or 0) == 1, name = row[10] or "",
      }
      state.buildings[#state.buildings + 1] = b
      state.catalog[b.id] = b
    elseif kind == "F" then
      state.followers[#state.followers + 1] = {
        db = tonumber(row[2]) or 0, id = tonumber(row[3]) or 0, level = tonumber(row[4]) or 0,
        quality = tonumber(row[5]) or 0, mission = tonumber(row[6]) or 0, name = row[7] or "",
      }
    elseif kind == "M" then
      state.missions[#state.missions + 1] = {
        db = tonumber(row[2]) or 0, id = tonumber(row[3]) or 0, st = tonumber(row[4]) or 0,
        left = tonumber(row[5]) or 0, success = tonumber(row[6]) or 0, name = row[7] or "",
      }
    elseif kind == "O" then
      state.orders[#state.orders + 1] = {
        db = tonumber(row[2]) or 0, plot = tonumber(row[3]) or 0, left = tonumber(row[4]) or 0,
      }
    elseif kind == "C" then
      state.catalogM = state.catalogM or {}
      state.catalogM[#state.catalogM + 1] = {
        id = tonumber(row[2]) or 0, followers = tonumber(row[3]) or 0, resources = tonumber(row[4]) or 0,
        seconds = tonumber(row[5]) or 0, rewardRes = tonumber(row[6]) or 0, rewardGold = tonumber(row[7]) or 0,
        name = row[8] or "",
      }
    elseif kind == "D" then
      state.recruits[#state.recruits + 1] = { id = tonumber(row[2]) or 0, name = row[3] or "" }
    end
  end
end

local function plotOf(id)
  for _, plot in ipairs(state.plots) do if plot.id == id then return plot end end
end

local function buildingOf(id)
  return state.catalog[id]
end

local function clearChildren(parent)
  parent.pool = parent.pool or {}
  parent.used = 0
end

local function take(parent, maker)
  parent.used = parent.used + 1
  local child = parent.pool[parent.used]
  if not child then
    child = maker()
    parent.pool[parent.used] = child
  end
  child:Show()
  return child
end

local function hideRest(parent)
  for i = parent.used + 1, #(parent.pool or {}) do parent.pool[i]:Hide() end
end

local function setInfo(title, body, artName, buttons)
  infoFrame.title:SetText(title or "")
  infoFrame.body:SetText(body or "")
  if artName then tex(infoFrame.art, artName) else infoFrame.art:SetTexture(nil) end
  for i, btn in ipairs(infoFrame.buttons) do
    local spec = buttons and buttons[i]
    if spec then
      btn:SetText(spec[1])
      btn:SetScript("OnClick", spec[2])
      btn:Show()
    else
      btn:Hide()
    end
  end
end

local function showTownHall()
  selectedPlot, selectedBuilding = 0, 0
  local side = state.horde and "Frostwall" or "Lunarfall"
  local body = string.format("Level %d town hall.\nUpgrade costs %d resources and %d gold.",
    state.level, state.upgradeRes, state.upgradeGold)
  local buttons = {
    { "Enter garrison", function() act(1) end },
    { "Leave", function() act(2) end },
    { "Collect cache (" .. state.pending .. ")", function() act(6) end },
  }
  if state.level < 3 then
    buttons[#buttons + 1] = { "Upgrade town hall", function() act(5) end }
  end
  setInfo(side, body, townHallInfo(state.level, state.horde), buttons)
end

local function showPlot(plot)
  selectedPlot, selectedBuilding = plot.id, 0
  if plot.garr == 12 or plot.garr == 13 or plot.garr == 14 then
    sizeTab = plot.garr
  else
    sizeTab = plot.garr
  end
  refreshList()
  if plot.building == 0 then
    setInfo(plot.name, "Empty plot. Choose a plan from the list that fits this size.",
      emptyCard(plot.garr, state.horde), nil)
    return
  end
  local building = buildingOf(plot.building)
  local body
  if not plot.active then
    body = string.format("Under construction.\n%d seconds remaining.", plot.left)
  else
    body = "Active. You can place a work order if this building produces shipments."
  end
  local buttons = {}
  if not plot.active then
    if plot.left > 0 then
      buttons[#buttons + 1] = { "Cancel construction", function() act(4, plot.id) end }
    else
      buttons[#buttons + 1] = { "Finish construction", function() act(11, plot.id) end }
    end
  else
    buttons[#buttons + 1] = { "Start work order", function() act(9, plot.id) end }
  end
  setInfo(plot.name, body, building and card(building.type, building.level, state.horde, "info"), buttons)
end

local function emptyPlotFor(garr)
  local fallback
  for _, plot in ipairs(state.plots) do
    if plot.building == 0 and plot.active then
      if plot.garr == garr then return plot end
      fallback = fallback or plot
    end
  end
  return fallback
end

local function showBuilding(building)
  selectedBuilding = building.id
  local current = selectedPlot ~= 0 and plotOf(selectedPlot) or nil
  if not current or current.garr ~= building.garr or current.building ~= 0 then
    local fit = emptyPlotFor(building.garr)
    if fit then
      selectedPlot = fit.id
      sizeTab = fit.garr
      current = fit
    end
  end
  local plot = current
  local body = string.format("Level %d\n%d resources, %d gold\nBuild time %d seconds",
    building.level, building.resources, building.gold, building.seconds)
  local buttons = {}
  if plot and building.garr == plot.garr and building.known then
    buttons[1] = { "Build here", function() act(3, plot.id, building.id) end }
  elseif not building.known then
    body = body .. "\nPlan not learned."
  elseif not plot then
    body = body .. "\nSelect an empty plot on the map."
  end
  setInfo(building.name, body, card(building.type, building.level, state.horde, "info"), buttons)
end

local function refreshList()
  clearChildren(listFrame.body)
  local y = 0
  for _, plot in ipairs(state.plots) do
    local row = take(listFrame.body, function()
      local b = CreateFrame("Button", nil, listFrame.body)
      b:SetSize(250, 42)
      b.bg = b:CreateTexture(nil, "BACKGROUND")
      b.bg:SetAllPoints()
      b.icon = b:CreateTexture(nil, "ARTWORK")
      b.icon:SetSize(36, 36)
      b.icon:SetPoint("LEFT", 4, 0)
      b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
      b.label:SetPoint("LEFT", b.icon, "RIGHT", 8, 0)
      b.label:SetPoint("RIGHT", -4, 0)
      b.label:SetJustifyH("LEFT")
      return b
    end)
    row:ClearAllPoints()
    row:SetPoint("TOPLEFT", 4, -y)
    local art = plot.building ~= 0 and buildingOf(plot.building) or nil
    if art then
      tex(row.icon, card(art.type, art.level, state.horde, "info"))
    else
      tex(row.icon, emptyCard(plot.garr, state.horde))
    end
    row.bg:SetTexture("Interface\\Buttons\\WHITE8X8")
    row.bg:SetVertexColor(0.45, 0.32, 0.05, selectedPlot == plot.id and 0.85 or 0.45)
    local title = plot.name ~= "" and plot.name or ("Plot " .. plot.id)
    if plot.building == 0 then
      title = title .. "  —  empty"
    elseif not plot.active then
      title = title .. "  —  building"
    end
    row.label:SetText(title)
    local plotId = plot.id
    row:SetScript("OnClick", function()
      local current = plotOf(plotId)
      if current then showPlot(current); refreshMap(); refreshList() end
    end)
    y = y + 44
  end
  for _, building in ipairs(state.buildings) do
    local fits = building.garr == sizeTab
      or (sizeTab == 28 and (building.garr == 28 or building.garr == 29 or building.garr == 31 or building.garr == 42))
    if fits and building.known then
      local row = take(listFrame.body, function()
        local b = CreateFrame("Button", nil, listFrame.body)
        b:SetSize(250, 42)
        b.bg = b:CreateTexture(nil, "BACKGROUND")
        b.bg:SetAllPoints()
        b.icon = b:CreateTexture(nil, "ARTWORK")
        b.icon:SetSize(36, 36)
        b.icon:SetPoint("LEFT", 4, 0)
        b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        b.label:SetPoint("LEFT", b.icon, "RIGHT", 8, 0)
        b.label:SetPoint("RIGHT", -4, 0)
        b.label:SetJustifyH("LEFT")
        return b
      end)
      row:ClearAllPoints()
      row:SetPoint("TOPLEFT", 4, -y)
      local art = card(building.type, building.level, state.horde, "info")
      if art then tex(row.icon, art) end
      row.bg:SetTexture("Interface\\Buttons\\WHITE8X8")
      row.bg:SetVertexColor(0, 0, 0, selectedBuilding == building.id and 0.55 or 0.25)
      row.label:SetText(string.format("%s  (L%d)", building.name, building.level))
      row:SetScript("OnClick", function() showBuilding(building); refreshList() end)
      y = y + 44
    end
  end
  hideRest(listFrame.body)
  listFrame.body:SetHeight(math.max(y, 1))
end

local function refreshMap()
  local level = math.max(state.level, 1)
  tex(mapFrame.bg, mapName(level, state.horde))
  local layout = state.horde and PLOT_UI.horde or PLOT_UI.alliance
  local w, h = mapFrame:GetWidth(), mapFrame:GetHeight()

  if not mapFrame.townHall then
    local b = CreateFrame("Button", nil, mapFrame)
    b:SetSize(96, 120)
    b.icon = b:CreateTexture(nil, "ARTWORK")
    b.icon:SetAllPoints()
    b.glow = b:CreateTexture(nil, "OVERLAY")
    b.glow:SetAllPoints()
    b.glow:SetTexture("Interface\\Buttons\\WHITE8X8")
    b.glow:SetVertexColor(1, 1, 0.4, 0.3)
    b.glow:Hide()
    b.level = b:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    b.level:SetPoint("BOTTOM", 0, 8)
    mapFrame.townHall = b
  end
  local th = mapFrame.townHall
  local thPos = (state.townX and state.townY) and { state.townX, state.townY } or layout[0] or { 0.45, 0.7 }
  th:ClearAllPoints()
  th:SetPoint("CENTER", mapFrame, "BOTTOMLEFT", thPos[1] * w, thPos[2] * h)
  tex(th.icon, townHallMap(level, state.horde))
  th.level:SetText(tostring(level))
  th.glow:SetShown(selectedPlot == 0)
  th:SetScript("OnClick", function() showTownHall(); refreshMap() end)
  th:Show()

  clearChildren(mapFrame.pins)
  for _, plot in ipairs(state.plots) do
    local pos = (plot.x and plot.y) and { plot.x, plot.y } or layout[plot.id]
    if pos then
      local pin = take(mapFrame.pins, function()
        local b = CreateFrame("Button", nil, mapFrame)
        b:SetSize(78, 54)
        b.plate = b:CreateTexture(nil, "BACKGROUND")
        b.plate:SetAllPoints()
        b.plate:SetTexture("Interface\\Buttons\\WHITE8X8")
        b.plate:SetVertexColor(0.15, 0.1, 0.02, 0.9)
        b.icon = b:CreateTexture(nil, "ARTWORK")
        b.icon:SetSize(36, 36)
        b.icon:SetPoint("LEFT", 4, 0)
        b.ring = b:CreateTexture(nil, "OVERLAY")
        b.ring:SetAllPoints()
        b.ring:SetTexture("Interface\\Buttons\\WHITE8X8")
        b.ring:SetVertexColor(1, 0.85, 0.2, 0.35)
        b.ring:Hide()
        b.caption = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        b.caption:SetPoint("LEFT", b.icon, "RIGHT", 2, 0)
        b.caption:SetPoint("RIGHT", -2, 0)
        b.caption:SetJustifyH("LEFT")
        b.timer = b:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
        b.timer:SetPoint("TOP", 0, 12)
        return b
      end)
      pin:ClearAllPoints()
      pin:SetPoint("CENTER", mapFrame, "BOTTOMLEFT", pos[1] * w, pos[2] * h)
      local building = plot.building ~= 0 and buildingOf(plot.building) or nil
      if building then
        tex(pin.icon, card(building.type, building.level, state.horde, "map") or card(building.type, building.level, state.horde, "info"))
      else
        tex(pin.icon, emptyCard(plot.garr, state.horde))
      end
      pin.ring:SetShown(selectedPlot == plot.id)
      pin.plate:SetVertexColor(plot.building == 0 and 0.55 or 0.1, plot.building == 0 and 0.38 or 0.25, 0.05, 0.92)
      local caption = plot.name ~= "" and plot.name or ("Plot " .. plot.id)
      if plot.building == 0 then caption = caption .. "\nempty" end
      pin.caption:SetText(caption)
      if plot.building ~= 0 and not plot.active and plot.left > 0 then
        pin.timer:SetText(plot.left .. "s")
      else
        pin.timer:SetText("")
      end
      local plotId = plot.id
      pin:SetScript("OnClick", function()
        local current = plotOf(plotId)
        if current then showPlot(current); refreshMap(); refreshList() end
      end)
    end
  end
  hideRest(mapFrame.pins)
end

local function refreshMissions()
  clearChildren(missionFrame.body)
  local y = 0
  local function add(text, click)
    local row = take(missionFrame.body, function()
      local b = CreateFrame("Button", nil, missionFrame.body, "UIPanelButtonTemplate")
      b:SetSize(520, 24)
      return b
    end)
    row:ClearAllPoints()
    row:SetPoint("TOPLEFT", 8, -y)
    row:SetText(text)
    row:SetScript("OnClick", click or function() end)
    y = y + 28
  end

  for _, order in ipairs(state.orders) do
    if order.left > 0 then
      add(string.format("Work order on plot %d — %ds", order.plot, order.left))
    else
      add(string.format("Collect work order on plot %d", order.plot), function() act(10, order.db) end)
    end
  end
  for _, mission in ipairs(state.missions) do
    if mission.st == 1 then
      if mission.left > 0 then
        add(string.format("%s — %ds remaining", mission.name, mission.left))
      else
        add(string.format("Claim %s (%s)", mission.name, mission.success == 1 and "success" or "failed"),
          function() act(8, mission.db) end)
      end
    end
  end
  for _, mission in ipairs(state.catalogM or {}) do
    add(string.format("Send follower: %s (%d resources)", mission.name, mission.resources), function()
      local free
      for _, follower in ipairs(state.followers) do
        if follower.mission == 0 then free = follower.db end
      end
      if free then act(7, mission.id, free) end
    end)
  end
  for _, follower in ipairs(state.followers) do
    add(string.format("%s — level %d%s", follower.name, follower.level, follower.mission ~= 0 and " (away)" or ""))
  end
  for _, recruit in ipairs(state.recruits) do
    local have = false
    for _, follower in ipairs(state.followers) do if follower.id == recruit.id then have = true end end
    if not have then
      add("Recruit " .. recruit.name, function() act(7, recruit.id) end)
    end
  end
  hideRest(missionFrame.body)
  missionFrame.body:SetHeight(math.max(y, 1))
end

local function refresh()
  if not EsteriaGarrison_GetState then return end
  parse(EsteriaGarrison_GetState())
  statusFS:SetText(RESULT[state.result] or "")
  if not state.owned then
    titleFS:SetText("Garrison")
    resourceFS:SetText("Found a garrison with Enter.")
    setInfo("Garrison", "Your first visit founds Lunarfall or Frostwall.",
      state.horde and "hordegarrisontier1" or "alliancegarrisontier1",
      { { "Enter garrison", function() act(1) end } })
    return
  end
  local side = state.horde and "Frostwall" or "Lunarfall"
  titleFS:SetText(string.format("%s  ·  Level %d", side, state.level))
  resourceFS:SetText(string.format("Resources %d    Cache %d", state.resources, state.pending))
  if page == "buildings" then
    listFrame:Show(); mapFrame:Show(); infoFrame:Show(); missionFrame:Hide()
    refreshList(); refreshMap()
    if selectedPlot == 0 and selectedBuilding == 0 then showTownHall()
    elseif selectedPlot ~= 0 then
      local plot = plotOf(selectedPlot)
      if plot then showPlot(plot) end
    elseif selectedBuilding ~= 0 then
      local building = buildingOf(selectedBuilding)
      if building then showBuilding(building) end
    end
  else
    listFrame:Hide(); mapFrame:Hide(); infoFrame:Hide(); missionFrame:Show()
    refreshMissions()
  end
end

local function chrome(parent)
  local bg = parent:CreateTexture(nil, "BACKGROUND")
  tex(bg, "garrisonuibackground")
  bg:SetAllPoints()
  local edge = parent:CreateTexture(nil, "BORDER")
  tex(edge, "garrisonuiatlas")
  edge:SetAllPoints()
  edge:SetAlpha(0.35)
end

local function makeButton(parent, label, x, y, w, click)
  local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
  b:SetSize(w or 90, 22)
  b:SetPoint("TOPLEFT", x, y)
  b:SetText(label)
  b:SetScript("OnClick", click)
  return b
end

frame = CreateFrame("Frame", "EsteriaGarrisonFrame", UIParent)
frame:SetSize(1002, 662)
frame:SetPoint("CENTER")
frame:EnableMouse(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:Hide()
chrome(frame)

titleFS = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
titleFS:SetPoint("TOPLEFT", 36, -18)
titleFS:SetText("Garrison")
resourceFS = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
resourceFS:SetPoint("TOPRIGHT", -160, -20)
statusFS = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
statusFS:SetPoint("TOPLEFT", 36, -40)
statusFS:SetTextColor(1, 0.82, 0)

makeButton(frame, "Buildings", 36, -58, 90, function() page = "buildings"; refresh() end)
makeButton(frame, "Missions", 130, -58, 90, function() page = "missions"; refresh() end)
makeButton(frame, "Enter", 230, -58, 70, function() act(1) end)
makeButton(frame, "Leave", 304, -58, 70, function() act(2) end)
makeButton(frame, "Cache", 378, -58, 70, function() act(6) end)
makeButton(frame, "Report", 452, -58, 70, function()
  frame:Hide()
  if G.ShowLanding then G.ShowLanding() end
end)
makeButton(frame, "Close", 910, -16, 70, function() frame:Hide() end)

listFrame = CreateFrame("Frame", nil, frame)
listFrame:SetSize(282, 520)
listFrame:SetPoint("BOTTOMLEFT", 30, 34)
local listBg = listFrame:CreateTexture(nil, "BACKGROUND")
tex(listBg, "garrisonuibackground2")
listBg:SetAllPoints()
listFrame.tabs = {}
for i, garr in ipairs({ 12, 13, 14, 28 }) do
  local label = ({ "Small", "Medium", "Large", "Special" })[i]
  local tab = makeButton(listFrame, label, 4 + (i - 1) * 68, 4, 66, function()
    -- Special shows mine / farm / fishing / menagerie plans together.
    sizeTab = garr
    refreshList()
  end)
  listFrame.tabs[i] = tab
end
local listScroll = CreateFrame("ScrollFrame", nil, listFrame)
listScroll:SetPoint("TOPLEFT", 8, -32)
listScroll:SetPoint("BOTTOMRIGHT", -8, 8)
listScroll:EnableMouseWheel(true)
listFrame.body = CreateFrame("Frame", nil, listScroll)
listFrame.body:SetSize(250, 400)
listScroll:SetScrollChild(listFrame.body)
listScroll:SetScript("OnMouseWheel", function(self, delta)
  local height = math.max(0, listFrame.body:GetHeight() - self:GetHeight())
  self:SetVerticalScroll(math.max(0, math.min(height, self:GetVerticalScroll() - delta * 36)))
end)

infoFrame = CreateFrame("Frame", nil, frame)
infoFrame:SetSize(672, 150)
infoFrame:SetPoint("TOPLEFT", listFrame, "TOPRIGHT", 20, 0)
local infoBg = infoFrame:CreateTexture(nil, "BACKGROUND")
tex(infoBg, "garrisonuibackground2")
infoBg:SetAllPoints()
infoFrame.art = infoFrame:CreateTexture(nil, "ARTWORK")
infoFrame.art:SetSize(128, 128)
infoFrame.art:SetPoint("LEFT", 12, 0)
infoFrame.title = infoFrame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
infoFrame.title:SetPoint("TOPLEFT", infoFrame.art, "TOPRIGHT", 12, -4)
infoFrame.body = infoFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
infoFrame.body:SetPoint("TOPLEFT", infoFrame.title, "BOTTOMLEFT", 0, -8)
infoFrame.body:SetPoint("RIGHT", -12, 0)
infoFrame.body:SetJustifyH("LEFT")
infoFrame.buttons = {}
for i = 1, 4 do
  infoFrame.buttons[i] = CreateFrame("Button", nil, infoFrame, "UIPanelButtonTemplate")
  infoFrame.buttons[i]:SetSize(140, 22)
  infoFrame.buttons[i]:SetPoint("BOTTOMLEFT", infoFrame.art, "BOTTOMRIGHT", 12 + (i - 1) * 148, 8)
  infoFrame.buttons[i]:Hide()
end

mapFrame = CreateFrame("Frame", nil, frame)
mapFrame:SetSize(672, 468)
mapFrame:SetPoint("TOPLEFT", infoFrame, "BOTTOMLEFT", 0, -8)
mapFrame.bg = mapFrame:CreateTexture(nil, "BACKGROUND")
mapFrame.bg:SetAllPoints()
mapFrame.pins = { pool = {}, used = 0 }

missionFrame = CreateFrame("Frame", nil, frame)
missionFrame:SetSize(940, 560)
missionFrame:SetPoint("BOTTOMLEFT", 30, 34)
missionFrame:Hide()
local missionBg = missionFrame:CreateTexture(nil, "BACKGROUND")
tex(missionBg, "garrisonmissionparchment")
missionBg:SetAllPoints()
local missionScroll = CreateFrame("ScrollFrame", nil, missionFrame)
missionScroll:SetPoint("TOPLEFT", 20, -20)
missionScroll:SetPoint("BOTTOMRIGHT", -20, 20)
missionScroll:EnableMouseWheel(true)
missionFrame.body = CreateFrame("Frame", nil, missionScroll)
missionFrame.body:SetSize(880, 400)
missionScroll:SetScrollChild(missionFrame.body)
missionScroll:SetScript("OnMouseWheel", function(self, delta)
  local height = math.max(0, missionFrame.body:GetHeight() - self:GetHeight())
  self:SetVerticalScroll(math.max(0, math.min(height, self:GetVerticalScroll() - delta * 36)))
end)

local function show()
  frame:Show()
  hello()
  if not EsteriaGarrison_Hello then
    statusFS:SetText("Rebuild the client with the Garrison extension for the live window.")
  end
  refresh()
end

frame:SetScript("OnEvent", function(_, event)
  if event == "PLAYER_ENTERING_WORLD" then hello()
  elseif event == "ESTERIA_GARRISON_STATE" then
    local text = EsteriaGarrison_GetState and EsteriaGarrison_GetState() or ""
    local mode = string.match(text, "^OPEN\t(%w+)")
    if mode == "buildings" or mode == "missions" then
      frame:Hide()
      if mode == "buildings" and GarrisonBuildingUI_ToggleFrame and GarrisonBuildingFrame and not GarrisonBuildingFrame:IsShown() then
        GarrisonBuildingUI_ToggleFrame()
      end
      return
    end
    if mode then
      page = (mode == "missions") and "missions" or "buildings"
      frame:Show()
    end
    if frame:IsShown() then refresh() end
  end
end)
frame:RegisterEvent("PLAYER_ENTERING_WORLD")
frame:RegisterEvent("ESTERIA_GARRISON_STATE")

SLASH_ESTERIA_GARRISON1 = "/garrison"
SlashCmdList["ESTERIA_GARRISON"] = function()
  if frame:IsShown() then frame:Hide() else show() end
end
