-- Esteria housing: collections and display (the spec's section 4). Trophy stands, mount stalls and pedestals, mannequins,
-- library pieces and pet habitats are decor in the Collections category; this file is their windows, in the look of the
-- training, portal and stone windows (Adventure.lua's H.Kit). Commands start with "col\t" (EsteriaHousing_Commerce); the
-- answers are "trophy", "mountdisp", "mannequin", "mqpick", "library", "libdata"/"libmore", "book" and "habitat" records
-- (mod-esteria-housing HousingCollections.inc writes them).

if not EsteriaHousing_IsAvailable or not EsteriaHousingUI or not EsteriaHousingUI.Kit then return end
local H = EsteriaHousingUI
local K = H.Kit
local Fields, Num, Show, LINE_H, BOX_TOP = K.Fields, K.Num, K.Show, K.LINE_H, K.BOX_TOP

local function Send(verb, placed, ...) EsteriaHousing_Commerce(strjoin("\t", "col", verb, placed or "0", ...)) end
local function Clean(text) return ((text or ""):gsub("[\t\n|]", " ")) end
local function Quality(name, q)
  local c = ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[Num(q)]
  return c and (c.hex .. name .. "|r") or name
end
local function Title(page, text, y)
  local t = page:CreateFontString(nil, "OVERLAY", "GameFontNormal")
  t:SetPoint("TOPLEFT", 12, y); t:SetText(text)
  return t
end

-- A row of a page: a label and a check on the right; set(i) is called when it is picked.
local function Choices(page, top, names, set)
  local lines = {}
  for i, name in ipairs(names) do
    local line = K.Line(page, i, top, name)
    local c = K.Check(line); c:SetPoint("RIGHT", -4, 0)
    c:SetScript("OnClick", function() set(i - 1) end)
    line.check = c
    lines[i] = line
  end
  return function(picked) for i, line in ipairs(lines) do line.check:SetChecked(picked == i - 1) end end, lines
end

-- Name and title lines with a Save, for any display that shows a name.
local function LabelPage(page, top, onSave)
  local name = K.Line(page, 1, top, "Name"); name.box = K.Box(name, 200); name.box:SetPoint("RIGHT", -4, 0)
  local sub = K.Line(page, 2, top, "Title"); sub.box = K.Box(sub, 200); sub.box:SetPoint("RIGHT", -4, 0)
  local hint = page:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
  hint:SetPoint("TOPLEFT", 12, top - 2 * (LINE_H + 2) - 6); hint:SetWidth(320); hint:SetJustifyH("LEFT")
  hint:SetText("Leave the name empty to show the trophy's or mount's own.")
  local save = K.Button(page, "Save", 90); save:SetPoint("TOPRIGHT", -8, top - 2 * (LINE_H + 2) - 26)
  save:SetScript("OnClick", function() name.box:ClearFocus(); sub.box:ClearFocus(); onSave(Clean(name.box:GetText()), Clean(sub.box:GetText())) end)
  return function(n, s)
    if not name.box:HasFocus() then name.box:SetText(n or "") end
    if not sub.box:HasFocus() then sub.box:SetText(s or "") end
  end, hint
end

-- Where a replica or mount stands on its object, on the house's placement keys (Plot.lua). Each press saves; Shift: a quarter
-- step. get() is the display (its fields hold the server's values), step its yards a press, reach its limit ahead and to the
-- side, defaults by kind the server's NewDisplay. Returns the redraw.
local function PlacementKeys(page, name, noun, step, reach, defaults, get, save)
  local function Nudge(ahead, right, up, turn, grow)
    local d = get()
    if not d then return end
    local by = (IsShiftKeyDown() and 0.25 or 1) * step
    local function Add(v, n, lo, hi) return math.max(lo, math.min(hi, (tonumber(v) or 0) + n)) end
    d.forward = Add(d.forward, (ahead or 0) * by, -reach, reach)
    d.side = Add(d.side, (right or 0) * by, -reach, reach)
    d.height = Add(d.height, (up or 0) * by, -2, 6)
    d.turn = ((tonumber(d.turn) or 0) + (turn or 0) + 180) % 360 - 180
    d.size = Add(d.size, (grow or 0) * 0.05, 0.1, 3)
    save()
  end
  local values = {}
  local function Row(i, label, keys)
    local line = K.Line(page, i, 0, label)
    local last
    for _, s in ipairs(keys) do
      local b = H.PlotKey(name .. s[1], line, s[2], s[3], s[4], s[5])
      if last then b:SetPoint("LEFT", last, "RIGHT", 4, 0) else b:SetPoint("LEFT", 92, 0) end
      last = b
    end
    line.value:SetPoint("RIGHT", -10, 0); line.value:SetJustifyH("RIGHT")
    values[i] = line.value
  end
  Row(1, "Position", {
    { "Left", "housing-hotkey-icon-arrow-left", function() Nudge(0, -1) end, "Move left" },
    { "Ahead", "housing-hotkey-icon-arrow-up", function() Nudge(1, 0) end, "Move ahead" },
    { "Back", "housing-hotkey-icon-arrow-down", function() Nudge(-1, 0) end, "Move back" },
    { "Right", "housing-hotkey-icon-arrow-right", function() Nudge(0, 1) end, "Move right" } })
  Row(2, "Height", {
    { "Down", "housing-floor-arrow-down-default", function() Nudge(0, 0, -1) end, "Lower the " .. noun },
    { "Up", "housing-floor-arrow-up-default", function() Nudge(0, 0, 1) end, "Raise the " .. noun } })
  Row(3, "Rotation", {
    { "TurnLeft", "decor-ability-expert-rotate-default", function() Nudge(0, 0, 0, 15) end, "Turn left 15\194\176" },
    { "TurnRight", "decor-ability-expert-rotate-default", function() Nudge(0, 0, 0, -15) end, "Turn right 15\194\176", true } })
  Row(4, "Size", {
    { "Smaller", "-", function() Nudge(0, 0, 0, 0, -1) end, "Make the " .. noun .. " smaller" },
    { "Bigger", "+", function() Nudge(0, 0, 0, 0, 1) end, "Make the " .. noun .. " bigger" } })
  local reset = K.Button(K.Line(page, 5, 0, "Back to its spot"), "Reset", 76); reset:SetPoint("RIGHT", -4, 0)
  reset:SetScript("OnClick", function()
    local t = get()
    if not t then return end
    local d = defaults[t.kind] or {}
    t.forward, t.side, t.height, t.turn, t.size = d.forward or 0, 0, d.height or 0, d.turn or 0, d.size or 1
    save()
  end)
  return function(d)
    values[1]:SetText(string.format("%.2f ahead  %.2f right", tonumber(d.forward) or 0, tonumber(d.side) or 0))
    values[2]:SetText(string.format("%+.2f yd", tonumber(d.height) or 0))
    values[3]:SetText(string.format("%d\194\176", tonumber(d.turn) or 0))
    values[4]:SetText(string.format("x%.2f", tonumber(d.size) or 1))
  end
end

-- ------------------------------------------------------------------------------------------------ trophies

local TROPHY_KINDS = { [29] = "Trophy Rug", "Trophy Wall Mount", "Trophy Pedestal", "Trophy Plaque", "Trophy Case" }
local ROOTS = { [92] = "General", [96] = "Quests", [97] = "Exploration", [95] = "Player vs. Player", [168] = "Dungeons & Raids",
  [169] = "Professions", [201] = "Reputation", [155] = "World Events", [81] = "Feats of Strength" }
local ROOT_ORDER = { 168, 95, 155, 96, 97, 169, 201, 92, 81 }
local STYLES = { "Statue of the boss", "Aureate Trophy", "Argent Trophy", "Gleaming Trophy", "Alliance Banner", "Horde Banner",
  "Head of the Broodmother", "Horrific Head of the Horseman", "Skeletal Dragon Head", "Name only" }
local EFFECTS = { "None", "Radiance", "Arcane glow", "Blue flame", "Victory" }

local T = K.Window("EsteriaHousingTrophy", 380, 500, "Interface\\Icons\\Achievement_BG_winAB")
T:SetPoint("LEFT", 60, 40)
local trophy
-- an item trophy's tooltip over the title
local titleTip = CreateFrame("Button", nil, T)
titleTip:SetPoint("TOPLEFT", T.title, "TOPLEFT"); titleTip:SetPoint("BOTTOMRIGHT", T.title, "BOTTOMRIGHT")
titleTip:SetScript("OnEnter", function(self)
  if trophy and trophy.link ~= "" then GameTooltip:SetOwner(self, "ANCHOR_BOTTOMRIGHT"); GameTooltip:SetHyperlink(trophy.link); GameTooltip:Show() end
end)
titleTip:SetScript("OnLeave", function() GameTooltip:Hide() end)

local tbox = K.Inset(T, BOX_TOP, 16)
local tpages = K.Tabs(T, tbox, { "Trophy", "Style", "Display" }, function(i) T:PickTab(i) end)
local trophyList = K.List(tpages[1], -10, 13, 24)

Title(tpages[2], "Show it as", -12)
local styleFill, styleLines = Choices(tpages[2], -28, { "Statue", "Trophy cup (gold)", "Trophy cup (silver)", "Trophy cup (gleaming)",
  "Alliance banner", "Horde banner", "Mounted head", "Horseman's head", "Dragon skull", "Name only" }, function(i)
  if trophy then trophy.style = i; H.SaveTrophyStyle() end
end)
-- the effect and placement go below, on the second half of the page
local effectBar = CreateFrame("Frame", nil, tpages[2]); effectBar:SetPoint("TOPLEFT", 0, -28 - 10 * (LINE_H + 2) - 6); effectBar:SetPoint("RIGHT")
effectBar:SetHeight(LINE_H)
local effectLine = K.Line(effectBar, 1, 0, "Effect")
local effectText = effectLine.value; effectText:SetPoint("RIGHT", -54, 0)
local effectNext = K.Button(effectLine, ">", 24); effectNext:SetPoint("RIGHT", -4, 0)
local effectPrev = K.Button(effectLine, "<", 24); effectPrev:SetPoint("RIGHT", effectNext, "LEFT", -2, 0)
local function StepEffect(d) if trophy then trophy.effect = (trophy.effect + d) % #EFFECTS; H.SaveTrophyStyle() end end
effectNext:SetScript("OnClick", function() StepEffect(1) end); effectPrev:SetScript("OnClick", function() StepEffect(-1) end)

function H.SaveTrophyStyle()
  local t = trophy
  Send("trophystyle", t.placed, t.style, t.effect, t.size, t.height, t.forward, t.turn, t.side)
end
local tplace = CreateFrame("Frame", nil, tpages[3]); tplace:SetPoint("TOPLEFT", 0, -12); tplace:SetPoint("RIGHT"); tplace:SetHeight(170)
local trophyPlaceFill = PlacementKeys(tplace, "Trophy", "trophy", 0.25, 3, { [1] = { height = 1, forward = 0.3, size = 0.6 },
  [2] = { height = 1.1, size = 0.6 }, [3] = { height = 1.2, forward = 0.2, size = 0.4 }, [4] = { height = 1.15, size = 0.2 }, [29] = { size = 0.6 } },
  function() return trophy end, function() H.SaveTrophyStyle() end)
local tlabel = CreateFrame("Frame", nil, tpages[3]); tlabel:SetPoint("TOPLEFT", 0, -200); tlabel:SetPoint("BOTTOMRIGHT")
local trophyLabelFill, trophyHint = LabelPage(tlabel, 0, function(name, sub) if trophy then Send("label", trophy.placed, name, sub) end end)
trophyHint:SetText("Leave the name empty to show the trophy's own.")

local function ReadTrophy(lines)
  local achievements, items = {}, {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "trophy" then
      trophy = { placed = f[2], owner = f[3] == "1", kind = Num(f[4]), source = Num(f[5]), id = Num(f[6]), date = f[7], name = f[8],
        style = Num(f[9]), effect = Num(f[10]), height = f[11], forward = f[12], turn = f[13], size = f[14], label = f[15], sub = f[16],
        link = f[17] or "", hasStatue = f[18] == "1", side = f[19] or "0", allowed = {} }
      -- the styles this piece can show (the plaque: heads and the skull; the case: cups and the Horseman's head; the rug:
      -- statues and banners; the pedestal: statues; any: a name)
      for v in (f[20] or ""):gmatch("%d+") do trophy.allowed[tonumber(v)] = true end
    elseif f[1] == "ta" then achievements[#achievements + 1] = { id = Num(f[2]), date = f[3], name = f[4], root = Num(f[5]), statue = f[6] == "1" }
    elseif f[1] == "ti" then items[#items + 1] = { entry = Num(f[2]), name = f[3], quality = f[4], link = f[5] } end
  end
  local t = trophy
  T.title:SetText(t.source > 0 and (t.source == 2 and Quality(t.name, select(3, GetItemInfo(t.id)) or 1) or t.name) or (TROPHY_KINDS[t.kind] or "Trophy"))
  T.status:SetText(t.source == 1 and ("Earned |cffffffff" .. t.date .. "|r") or t.source == 2 and ("On show since |cffffffff" .. t.date .. "|r")
    or "|cff808080Nothing on show yet|r")
  local desc = ""
  if t.source == 1 then desc = select(8, GetAchievementInfo(t.id)) or "" elseif t.source == 2 then desc = "Hover the name for the item." end
  T.note:SetText(desc)
  -- the owner's list: what the account has earned, by category, then the character's items
  local items_ = {}
  if t.source > 0 then items_[#items_ + 1] = { text = "|cffff8080Take the trophy down|r", fn = function() Send("trophyset", t.placed, 0, 0) end } end
  local byRoot = {}
  for _, a in ipairs(achievements) do byRoot[a.root] = byRoot[a.root] or {}; table.insert(byRoot[a.root], a) end
  local order = {}
  for _, r in ipairs(ROOT_ORDER) do order[#order + 1] = r end
  for r in pairs(byRoot) do if not ROOTS[r] then order[#order + 1] = r end end
  for _, r in ipairs(order) do
    local group = byRoot[r]
    if group then
      table.sort(group, function(a, b) return a.name < b.name end)
      items_[#items_ + 1] = { header = true, text = ROOTS[r] or "Other Achievements" }
      for _, a in ipairs(group) do
        local _, _, _, _, _, _, _, description = GetAchievementInfo(a.id)
        items_[#items_ + 1] = { text = a.name, sub = a.statue and "statue" or a.date, picked = t.source == 1 and t.id == a.id,
          tip = description, fn = function() Send("trophyset", t.placed, 1, a.id) end }
      end
    end
  end
  if #items > 0 then
    table.sort(items, function(a, b) return a.name < b.name end)
    items_[#items_ + 1] = { header = true, text = "My Items (a copy of the name; the item stays yours)" }
    for _, it in ipairs(items) do
      items_[#items_ + 1] = { text = Quality(it.name, it.quality), link = it.link, picked = t.source == 2 and t.id == it.entry,
        fn = function() Send("trophyset", t.placed, 2, it.entry) end }
    end
  end
  trophyList.items = items_
  trophyList:Update()
  styleFill(t.style)
  -- only what this piece can show, packed together under the title
  local row = 0
  for i, line in ipairs(styleLines) do
    local ok = next(t.allowed) == nil or t.allowed[i - 1]
    Show(line, ok)
    if ok then
      row = row + 1
      line:ClearAllPoints()
      line:SetPoint("TOPLEFT", 6, -28 - (row - 1) * (LINE_H + 2)); line:SetPoint("RIGHT", -6, 0)
    end
  end
  effectText:SetText(EFFECTS[t.effect + 1] or "None")
  trophyPlaceFill(t)
  trophyLabelFill(t.label, t.sub)
  Show(tbox, t.owner)
  T:SetHeight(t.owner and 500 or 124)
  if not T:IsShown() then T:PickTab(1) end
  for i = 1, 3 do Show(_G["EsteriaHousingTrophyTab" .. i], t.owner) end
  T:Show()
end

-- ------------------------------------------------------------------------------------------------ mounts

local MOUNT_KINDS = { [5] = { "Mount Stall", "Your mount spends its time around the stall." }, [6] = { "Mount Paddock", "Your mount roams around the paddock." },
  [7] = { "Mount Pedestal", "A statue of your mount." }, [28] = { "Mount Mat", "Your mount stands on an unseen mat." } }
local MOTIONS = { "Still (a statue)", "Idle in place", "Wander around" }
local MO = K.Window("EsteriaHousingMountDisplay", 380, 480, "Interface\\Icons\\Ability_Mount_RidingHorse")
MO:SetPoint("LEFT", 60, 40)
local mount
local mbox = K.Inset(MO, BOX_TOP, 16)
local mpages = K.Tabs(MO, mbox, { "Mount", "Display", "Motion" }, function(i) MO:PickTab(i) end)
local mountList = K.List(mpages[1], -10, 12, 24)
-- Motion: still, idle or wandering; a wanderer's radius; a still mount's finish
Title(mpages[3], "How it stands", -12)
local motionFill = Choices(mpages[3], -28, MOTIONS, function(i) if mount then mount.motion = i; H.SaveMountStyle() end end)
local radiusLine = K.Line(mpages[3], 1, -28 - 3 * (LINE_H + 2) - 10, "Wander radius (2 to 10 yd)")
local radiusBox = K.Box(radiusLine, 50, true); radiusBox:SetPoint("RIGHT", -4, 0)
local radiusSave = K.Button(mpages[3], "Save", 90); radiusSave:SetPoint("TOPRIGHT", -8, -28 - 4 * (LINE_H + 2) - 16)
radiusSave:SetScript("OnClick", function() if mount then radiusBox:ClearFocus(); H.SaveMountStyle() end end)
local finishTitle = Title(mpages[3], "Statue finish", -28 - 4 * (LINE_H + 2) - 56)
local finishFill, finishLines = Choices(mpages[3], -28 - 4 * (LINE_H + 2) - 72, { "Natural", "Stone", "Spectral" },
  function(i) if mount then mount.finish = i; H.SaveMountStyle() end end)
local mplace = CreateFrame("Frame", nil, mpages[2]); mplace:SetPoint("TOPLEFT", 0, -12); mplace:SetPoint("RIGHT"); mplace:SetHeight(170)
local mountPlaceFill = PlacementKeys(mplace, "Mount", "mount", 1, 6, { [5] = { forward = 2, turn = 90 }, [7] = { height = 1, size = 0.5 } },
  function() return mount end, function() H.SaveMountStyle() end)
local mountRoamNote = mplace:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
mountRoamNote:SetPoint("TOPLEFT", 12, -5 * (LINE_H + 2) - 4); mountRoamNote:SetWidth(320); mountRoamNote:SetJustifyH("LEFT")
mountRoamNote:SetText("While it wanders, a mount walks the ground and only its size applies.")
local mlabel = CreateFrame("Frame", nil, mpages[2]); mlabel:SetPoint("TOPLEFT", 0, -200); mlabel:SetPoint("BOTTOMRIGHT")
local mountLabelFill, mountHint = LabelPage(mlabel, 0, function(name, sub) if mount then Send("label", mount.placed, name, sub) end end)
mountHint:SetText("Leave the name empty to show the mount's own.")

function H.SaveMountStyle()
  Send("mountstyle", mount.placed, mount.finish, mount.size, mount.height, mount.forward, mount.turn, mount.side, mount.motion or 1,
    Num(radiusBox:GetText()))
end

local function ReadMount(lines)
  local known = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "mount" then
      mount = { placed = f[2], owner = f[3] == "1", kind = Num(f[4]), spell = Num(f[5]), name = f[6], finish = Num(f[7]), height = f[8],
        forward = f[9], turn = f[10], size = f[11], label = f[12], sub = f[13], side = f[14] or "0", motion = Num(f[15]),
        radius = f[16] or "5" }
    elseif f[1] == "mo" then known[#known + 1] = { spell = Num(f[2]), name = f[3] } end
  end
  local m, info = mount, MOUNT_KINDS[mount.kind] or { "Mount Display", "" }
  MO.title:SetText(m.spell > 0 and m.name or info[1])
  MO.status:SetText(m.spell > 0 and info[2] or "|cff808080No mount on show yet|r")
  MO.note:SetText(m.owner and "Your display. Changes show at once." or "")
  local icon = m.spell > 0 and select(3, GetSpellInfo(m.spell))
  SetPortraitToTexture(MO.portrait, icon or "Interface\\Icons\\Ability_Mount_RidingHorse")
  table.sort(known, function(a, b) return a.name < b.name end)
  local items = { }
  if m.spell > 0 then items[1] = { text = "|cffff8080Lead the mount away|r", fn = function() Send("mountset", m.placed, 0) end } end
  items[#items + 1] = { header = true, text = "Mounts of your account (" .. #known .. ")" }
  for _, k in ipairs(known) do
    items[#items + 1] = { text = k.name, picked = k.spell == m.spell, fn = function() Send("mountset", m.placed, k.spell) end }
  end
  mountList.items = items
  mountList:Update()
  local still = m.motion == 0
  Show(finishTitle, still)
  for _, l in ipairs(finishLines) do Show(l, still) end
  finishFill(m.finish)
  motionFill(m.motion)
  Show(radiusLine, m.motion == 2); Show(radiusSave, m.motion == 2)
  if not radiusBox:HasFocus() then radiusBox:SetText(m.radius) end
  mountPlaceFill(m)
  Show(mountRoamNote, m.motion == 2)
  mountLabelFill(m.label, m.sub)
  Show(mbox, m.owner)
  MO:SetHeight(m.owner and 480 or 124)
  if not MO:IsShown() then MO:PickTab(1) end
  for i = 1, 3 do Show(_G["EsteriaHousingMountDisplayTab" .. i], m.owner) end
  MO:Show()
end

-- ------------------------------------------------------------------------------------------------ mannequins

local SLOTS = { { 0, "Head" }, { 2, "Shoulders" }, { 14, "Back" }, { 4, "Chest" }, { 3, "Shirt" }, { 18, "Tabard" }, { 8, "Wrists" },
  { 9, "Hands" }, { 5, "Waist" }, { 6, "Legs" }, { 7, "Feet" }, { 15, "Main Hand" }, { 16, "Off Hand" }, { 17, "Ranged" } }
local TYPES = { "Character", "Stone statue", "Spectral", "Still (frozen)" }
local POSES = { "Standing", "Ready (one-handed)", "Ready (two-handed)", "Ready (bow)", "Ready (gun)", "Spellcasting", "Kneeling", "Sitting" }

local MQ = K.Window("EsteriaHousingMannequin", 380, 560, "Interface\\Icons\\INV_Chest_Plate16")
MQ:SetPoint("LEFT", 60, 30)
local mq, mqSlots, pickSlot = nil, {}, nil
local qbox = K.Inset(MQ, BOX_TOP, 16)
-- One window for every character: a mannequin, and the service NPCs the owner dresses (the innkeeper, banker, smith,
-- auctioneer, trainers), whose Service tab Household.lua fills (H.FigureServicePage, H.ShowFigure).
local qtabs = { false, true, true, true, true }
local qpages = K.Tabs(MQ, qbox, { "Service", "Outfit", "Style", "Settings", "Outfits" }, function(i) MQ:PickTab(i, qtabs) end)
H.FigureServicePage = qpages[1]
local figure, figureIcon, figureFresh -- the service the window shows (its owner's), its icon, a first open
function H.ShowFigure(svc, icon)
  figureFresh = not (MQ:IsShown() and figure and figure.placed == svc.placed)
  figure, figureIcon = svc, icon
  Send("mqopen", svc.placed)
end
function H.HideFigure() MQ:Hide() end

-- Outfit: a line per slot (click to change it, right-click to clear it); the pick list replaces them while choosing
local slotFrame = CreateFrame("Frame", nil, qpages[2]); slotFrame:SetAllPoints()
local pickFrame = CreateFrame("Frame", nil, qpages[2]); pickFrame:SetAllPoints(); pickFrame:Hide()
local slotLines = {}
for i, s in ipairs(SLOTS) do
  local line = K.Line(slotFrame, i, -6, s[2])
  local b = CreateFrame("Button", nil, line); b:SetAllPoints()
  b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
  b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
  b:SetScript("OnClick", function(_, button)
    if not mq or not mq.owner then return end
    if button == "RightButton" then Send("mqclear", mq.placed, s[1]) else pickSlot = s; Send("mqpick", mq.placed, s[1]) end
  end)
  b:SetScript("OnEnter", function(self)
    local it = mqSlots[s[1]]
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    if it and it.link ~= "" then GameTooltip:SetHyperlink(it.link)
    elseif mq and mq.owner then GameTooltip:AddLine(s[2]); GameTooltip:AddLine("Click to choose what it wears here.", 1, 1, 1) end
    if mq and mq.owner and it then GameTooltip:AddLine("Right-click to take it off.", 0.6, 0.6, 0.6) end
    GameTooltip:Show()
  end)
  b:SetScript("OnLeave", function() GameTooltip:Hide() end)
  line.value:SetPoint("LEFT", 110, 0); line.value:SetHeight(14)
  slotLines[i] = line
end
local strip = K.Button(slotFrame, "Take All Off", 110); strip:SetPoint("BOTTOMRIGHT", -8, 8)
strip:SetScript("OnClick", function() if mq then Send("mqstrip", mq.placed) end end)
local pickTitle = Title(pickFrame, "", -8)
local pickBack = K.Button(pickFrame, "Back", 70); pickBack:SetPoint("TOPRIGHT", -8, -4)
pickBack:SetScript("OnClick", function() pickFrame:Hide(); slotFrame:Show() end)
local pickList = K.List(pickFrame, -30, 13, 24)

-- Style: what it is made of, and how it stands
Title(qpages[3], "Mannequin", -10)
local typeFill = Choices(qpages[3], -26, TYPES, function(i) if mq then Send("mqstyle", mq.placed, i, mq.pose) end end)
Title(qpages[3], "Pose", -26 - 4 * (LINE_H + 2) - 8)
local poseFill = Choices(qpages[3], -26 - 4 * (LINE_H + 2) - 24, POSES, function(i) if mq then Send("mqstyle", mq.placed, mq.type, i) end end)

-- Label: its name and title, and the character creator for its race and features
local mqLabelFill, mqHint = LabelPage(qpages[4], -10, function(name, sub) if mq then Send("label", mq.placed, name, sub) end end)
mqHint:SetText("Its nameplate: a name, and a title under it.")
local customize = K.Button(qpages[4], "Customize Appearance", 170); customize:SetPoint("TOPLEFT", 12, -130)
customize:SetScript("OnClick", function() if mq then Send("mqappearance", mq.placed) end end)
local customizeNote = qpages[4]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
customizeNote:SetPoint("TOPLEFT", 12, -160); customizeNote:SetWidth(320); customizeNote:SetJustifyH("LEFT")
customizeNote:SetText("Race, body type, face, hair and the body sliders, as in character creation.")

-- Outfits: named sets of gear to put on again
local outfitLine = K.Line(qpages[5], 1, -10, "Name")
local outfitBox = K.Box(outfitLine, 150); outfitBox:SetPoint("RIGHT", -84, 0)
local outfitSave = K.Button(outfitLine, "Save", 74); outfitSave:SetPoint("RIGHT", -4, 0)
outfitSave:SetScript("OnClick", function() if mq then Send("mqsave", mq.placed, Clean(outfitBox:GetText())); outfitBox:ClearFocus() end end)
local outfitPicked
local outfitList = K.List(qpages[5], -44, 11, 24)
local outfitLoad = K.Button(qpages[5], "Wear", 90); outfitLoad:SetPoint("BOTTOMRIGHT", -8, 8)
outfitLoad:SetScript("OnClick", function() if mq and outfitPicked then Send("mqload", mq.placed, outfitPicked) end end)
local outfitDelete = K.Button(qpages[5], "Delete", 90); outfitDelete:SetPoint("RIGHT", outfitLoad, "LEFT", -6, 0)
outfitDelete:SetScript("OnClick", function() if mq and outfitPicked then Send("mqdel", mq.placed, outfitPicked); outfitPicked = nil end end)

local function DrawOutfits(names)
  local items = {}
  for _, n in ipairs(names) do
    items[#items + 1] = { text = n, picked = n == outfitPicked, fn = function() outfitPicked = n; DrawOutfits(names) end }
  end
  if #items == 0 then items[1] = { header = true, text = "No outfits saved yet" } end
  outfitList.items = items
  outfitList:Update()
end

local function ReadMannequin(lines)
  mqSlots = {}
  local outfits = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "mq" then
      mq = { placed = f[2], owner = f[3] == "1", race = Num(f[4]), sex = Num(f[5]), type = Num(f[6]), pose = Num(f[7]), label = f[8], sub = f[9],
        kind = Num(f[11]) }
    elseif f[1] == "slot" then mqSlots[Num(f[2])] = { entry = Num(f[3]), name = f[4], quality = f[5], link = f[6] or "" }
    elseif f[1] == "outfit" then outfits[#outfits + 1] = f[2] end
  end
  local n = 0
  for i, s in ipairs(SLOTS) do
    local it = mqSlots[s[1]]
    slotLines[i].value:SetText(it and Quality(it.name, it.quality) or "|cff606060Empty|r")
    if it then n = n + 1 end
  end
  -- a service NPC wears an outfit too; the type and pose (statues, poses) stay a mannequin's
  local mannequin = mq.kind == 0 or mq.kind == 8
  local service = not mannequin and figure ~= nil and figure.placed == mq.placed
  SetPortraitToTexture(MQ.portrait, service and figureIcon or "Interface\\Icons\\INV_Chest_Plate16")
  MQ.title:SetText(mq.label ~= "" and mq.label or "Mannequin")
  MQ.status:SetText(mannequin and string.format("%s, %s  |cffb0b0b0%d %s|r", TYPES[mq.type + 1] or "", POSES[mq.pose + 1] or "", n,
    n == 1 and "piece" or "pieces") or string.format("%s  |cffb0b0b0%d %s|r", mq.sub ~= "" and mq.sub or "Outfit", n, n == 1 and "piece" or "pieces"))
  MQ.note:SetText(mq.owner and "Gear here is a copy of its look; your items stay yours." or (mq.sub ~= "" and mq.sub or ""))
  typeFill(mq.type); poseFill(mq.pose)
  mqLabelFill(mq.label, mq.sub)
  table.sort(outfits)
  DrawOutfits(outfits)
  Show(strip, mq.owner)
  -- visitors inspect the outfit only
  local first = not MQ:IsShown()
  MQ:SetHeight(560)
  qtabs = { service, true, mq.owner and mannequin, mq.owner, mq.owner }
  if first or (service and figureFresh) then
    pickFrame:Hide(); slotFrame:Show()
    MQ:PickTab(service and 1 or 2, qtabs)
  elseif not qtabs[MQ.selectedTab or 2] then
    MQ:PickTab(2, qtabs)
  else
    for i = 1, 5 do Show(_G["EsteriaHousingMannequinTab" .. i], qtabs[i]) end
  end
  figureFresh = false
  if not mq.owner then MQ:PickTab(2, qtabs) end
  MQ:Show()
end

local function ReadPicks(lines)
  local f = Fields(lines[1])
  local slot = Num(f[3])
  local owned, collected = {}, {}
  for i = 2, #lines do
    local c = Fields(lines[i])
    if c[1] == "c" then
      local row = { entry = Num(c[2]), name = c[3], quality = c[4], source = c[5] == "Owned" and 0 or 1, link = c[6] }
      table.insert(row.source == 0 and owned or collected, row)
    end
  end
  local items = {}
  local function Group(title, rows)
    if #rows == 0 then return end
    table.sort(rows, function(a, b) return a.name < b.name end)
    items[#items + 1] = { header = true, text = title }
    for _, r in ipairs(rows) do
      items[#items + 1] = { text = Quality(r.name, r.quality), link = r.link, picked = mqSlots[slot] and mqSlots[slot].entry == r.entry,
        fn = function() Send("mqslot", mq.placed, slot, r.entry, r.source); pickFrame:Hide(); slotFrame:Show() end }
    end
  end
  Group("Your gear", owned)
  Group("Collected appearances", collected)
  if #items == 0 then items[1] = { header = true, text = "Nothing you have fits here." } end
  pickTitle:SetText((pickSlot and pickSlot[2] or "Slot") .. ": choose a look")
  pickList.items = items
  pickList:Update()
  slotFrame:Hide(); pickFrame:Show()
end

-- ------------------------------------------------------------------------------------------------ the library

local LIB_TABS = { "Books", "Exploration", "Achievements", "Storylines" }
local LB = K.Window("EsteriaHousingLibrary", 420, 540, "Interface\\Icons\\INV_Misc_Book_09")
LB:SetPoint("LEFT", 60, 30)
local lib, libTab, libRows = nil, 1, {}
local lbox = K.Inset(LB, BOX_TOP, 40)
local lpages = K.Tabs(LB, lbox, LIB_TABS, function(i) if lib then libTab = i; LB:PickTab(i); Send("libtab", lib.placed, i) end end)
local libLists = {}
for i = 1, 4 do libLists[i] = K.List(lpages[i], -10, 15, 24) end
local missingCheck = K.Check(LB); missingCheck:SetPoint("BOTTOMLEFT", 16, 12)
local missingLabel = LB:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
missingLabel:SetPoint("LEFT", missingCheck, "RIGHT", 2, 0); missingLabel:SetText("Show missing")
missingCheck:SetChecked(true)

local function DrawLibrary()
  local items, have, total = {}, 0, 0
  local showMissing = missingCheck:GetChecked()
  if libTab == 3 then
    -- the achievements the client knows, by category, marked by what the owner's account earned
    local earned = {}
    for _, r in ipairs(libRows) do if r[1] == "a" then earned[Num(r[2])] = r[3] end end
    for _, cat in ipairs(GetCategoryList() or {}) do
      local title, parent = GetCategoryInfo(cat)
      local count = GetCategoryNumAchievements(cat) or 0
      local group = {}
      for i = 1, count do
        local id, name, points, _, _, _, _, description = GetAchievementInfo(cat, i)
        if id then
          local date = earned[id]
          total = total + 1
          if date then have = have + 1 end
          if date or showMissing then
            group[#group + 1] = { text = name, sub = date or "", dim = not date, tip = description }
          end
        end
      end
      if #group > 0 then
        local parentName = parent and parent > 0 and GetCategoryInfo(parent)
        items[#items + 1] = { header = true, text = parentName and (parentName .. ": " .. title) or title }
        for _, g in ipairs(group) do items[#items + 1] = g end
      end
    end
  else
    for _, r in ipairs(libRows) do
      if r[1] == "g" then items[#items + 1] = { header = true, text = r[2] }
      elseif r[1] == "e" then
        local got = r[4] == "1"
        if got or showMissing then
          local page = Num(r[2])
          items[#items + 1] = { text = got and r[3] or ("|cff808080" .. r[3] .. "|r"), sub = r[5] ~= "" and r[5] or (got and "" or "missing"),
            dim = not got, fn = (libTab == 1 and got) and function() Send("book", lib.placed, page) end or nil,
            tip = (libTab == 1 and got) and "Click to read it." or nil }
        end
      end
    end
    -- headers with nothing under them go
    local kept = {}
    for i, it in ipairs(items) do
      if not it.header or (items[i + 1] and not items[i + 1].header) then kept[#kept + 1] = it end
    end
    items = kept
    have, total = lib.have, lib.total
  end
  libLists[libTab].items = items
  libLists[libTab]:Update()
  LB.status:SetText(string.format("%s: |cffffffff%d|r of %d", LIB_TABS[libTab], have, total))
end
missingCheck:SetScript("OnClick", function() if lib then DrawLibrary() end end)

local function ReadLibrary(lines)
  local f = Fields(lines[1])
  lib = { placed = f[2], owner = f[3] == "1", ownerName = f[4], have = 0, total = 0 }
  libTab = Num(f[5]); libRows = {}
  LB.title:SetText((lib.ownerName ~= "" and (lib.ownerName .. "'s ") or "") .. "Library")
  LB.note:SetText(lib.owner and "Everything your account has found. Books count from the first time you read them." or "What its owner has found.")
  LB:PickTab(libTab)
  LB:Show()
end

-- a tab's rows come in chunks: "libdata" starts them, "libmore" adds to them, "libend" closes the last
local function ReadLibraryData(lines, first)
  if first then
    local f = Fields(lines[1])
    if lib then lib.have, lib.total = Num(f[4]), Num(f[5]) end
    libTab = Num(f[3]); libRows = {}
  end
  for i = 2, #lines do
    if lines[i] == "libend" then if lib then DrawLibrary() end return end
    libRows[#libRows + 1] = Fields(lines[i])
  end
end

-- the book reader: a page at a time
local B = H.Panel("EsteriaHousingBook", 380, 460)
B:SetPoint("CENTER", 120, 20)
tinsert(UISpecialFrames, "EsteriaHousingBook")
CreateFrame("Button", nil, B, "UIPanelCloseButton"):SetPoint("TOPRIGHT", 2, 2)
local bTitle = B:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge"); bTitle:SetPoint("TOP", 0, -14); bTitle:SetWidth(320)
local bScroll = CreateFrame("ScrollFrame", "EsteriaHousingBookScroll", B, "UIPanelScrollFrameTemplate")
bScroll:SetPoint("TOPLEFT", 22, -48); bScroll:SetPoint("BOTTOMRIGHT", -40, 52)
local bChild = CreateFrame("Frame", nil, bScroll); bChild:SetSize(310, 10)
bScroll:SetScrollChild(bChild)
local bText = bChild:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
bText:SetPoint("TOPLEFT"); bText:SetWidth(306); bText:SetJustifyH("LEFT"); bText:SetJustifyV("TOP")
local bPrev = K.Button(B, "Previous", 90); bPrev:SetPoint("BOTTOMLEFT", 18, 16)
local bNext = K.Button(B, "Next", 90); bNext:SetPoint("BOTTOMRIGHT", -18, 16)
local bPage = B:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall"); bPage:SetPoint("BOTTOM", 0, 22)
local pages, at = {}, 1
local function DrawPage()
  bText:SetText(pages[at] or "")
  bChild:SetHeight(math.max(10, bText:GetStringHeight() + 8))
  bScroll:SetVerticalScroll(0)
  bPage:SetText(string.format("Page %d of %d", at, #pages))
  if at > 1 then bPrev:Enable() else bPrev:Disable() end
  if at < #pages then bNext:Enable() else bNext:Disable() end
end
bPrev:SetScript("OnClick", function() at = at - 1; DrawPage() end)
bNext:SetScript("OnClick", function() at = at + 1; DrawPage() end)

local function ReadBook(lines)
  local f = Fields(lines[1])
  bTitle:SetText(f[3] or "")
  pages, at = {}, 1
  for i = 2, #lines do
    local p = lines[i]:match("^p\t(.*)$")
    if p then pages[#pages + 1] = (p:gsub("\r", "\n")) end
  end
  DrawPage()
  B:Show()
end

-- ------------------------------------------------------------------------------------------------ pet habitats

local HB = K.Window("EsteriaHousingHabitat", 380, 470, "Interface\\Icons\\INV_Box_PetCarrier_01")
HB:SetPoint("LEFT", 60, 40)
local hab, chosen = nil, {}
local hbox = K.Inset(HB, BOX_TOP, 16)
local hpages = K.Tabs(HB, hbox, { "Pets", "Area" }, function(i) HB:PickTab(i) end)
local petList = K.List(hpages[1], -10, 11, 24)
local petSave = K.Button(hpages[1], "Save", 90); petSave:SetPoint("BOTTOMRIGHT", -8, 8)
local radiusLine = K.Line(hpages[2], 1, -10, "Roaming radius (2 to 10 yd)")
local radiusBox = K.Box(radiusLine, 50, true); radiusBox:SetPoint("RIGHT", -4, 0)
local areaNote = hpages[2]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
areaNote:SetPoint("TOPLEFT", 12, -46); areaNote:SetWidth(330); areaNote:SetJustifyH("LEFT")
areaNote:SetText("Pets wander within this distance of the habitat. Place a Pet Bed, Pet Toy or Feeding Station nearby and they will sleep, play and eat there.")
-- the rug itself: shown, or hidden so only the pets show
local rugLine = K.Line(hpages[2], 1, -130, "Hide the rug (only the pets show)")
local rugCheck = K.Check(rugLine); rugCheck:SetPoint("RIGHT", -4, 0)
local areaSave = K.Button(hpages[2], "Save", 90); areaSave:SetPoint("TOPRIGHT", -8, -162)

local function SaveHabitat()
  local list = {}
  for spell in pairs(chosen) do list[#list + 1] = spell end
  Send("habset", hab.placed, Num(radiusBox:GetText()), table.concat(list, ","), rugCheck:GetChecked() and 1 or 0)
end
petSave:SetScript("OnClick", function() if hab then SaveHabitat() end end)
areaSave:SetScript("OnClick", function() if hab then radiusBox:ClearFocus(); SaveHabitat() end end)

local pets = {}
local function DrawPets()
  local n = 0
  for _ in pairs(chosen) do n = n + 1 end
  local items = { { header = true, text = string.format("Companions (%d of %d chosen)", n, hab.max) } }
  for _, p in ipairs(pets) do
    local on = chosen[p.spell]
    items[#items + 1] = { text = p.name, picked = on, sub = on and "home" or "",
      fn = hab.owner and function()
        if chosen[p.spell] then chosen[p.spell] = nil elseif n < hab.max then chosen[p.spell] = true
        else UIErrorsFrame:AddMessage("A habitat holds " .. hab.max .. " pets.", 1, 0.3, 0.3) end
        DrawPets()
      end or nil }
  end
  petList.items = items
  petList:Update()
  HB.status:SetText(string.format("%d %s live here", n, n == 1 and "pet" or "pets"))
end

local function ReadHabitat(lines)
  pets, chosen = {}, {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "hab" then hab = { placed = f[2], owner = f[3] == "1", radius = f[4], max = Num(f[5]), hideRug = f[6] == "1" }
    elseif f[1] == "pet" then
      pets[#pets + 1] = { spell = Num(f[2]), name = f[3] }
      if f[4] == "1" then chosen[Num(f[2])] = true end
    end
  end
  table.sort(pets, function(a, b) return a.name < b.name end)
  HB.title:SetText("Pet Habitat")
  HB.note:SetText(hab.owner and "Choose who lives here; they stay in your collection." or "")
  if not radiusBox:HasFocus() then radiusBox:SetText(hab.radius) end
  rugCheck:SetChecked(hab.hideRug)
  DrawPets()
  Show(petSave, hab.owner)
  if not HB:IsShown() then HB:PickTab(1) end
  for i = 1, 2 do Show(_G["EsteriaHousingHabitatTab" .. i], hab.owner or i == 1) end
  if not hab.owner then HB:PickTab(1, { true, false }) end
  HB:Show()
end

-- ------------------------------------------------------------------------------------------------ answers

local listener = CreateFrame("Frame")
listener:RegisterEvent("ESTERIA_HOUSING_COMMERCE")
listener:SetScript("OnEvent", function()
  local lines = {}
  for line in EsteriaHousing_GetCommerce():gmatch("[^\n]+") do lines[#lines + 1] = line end
  local kind = lines[1] and lines[1]:match("^[^\t]+")
  if kind == "trophy" then ReadTrophy(lines)
  elseif kind == "mountdisp" then ReadMount(lines)
  elseif kind == "mannequin" then ReadMannequin(lines)
  elseif kind == "mqpick" then ReadPicks(lines)
  elseif kind == "library" then ReadLibrary(lines)
  elseif kind == "libdata" then ReadLibraryData(lines, true)
  elseif kind == "libmore" then ReadLibraryData(lines, false)
  elseif kind == "book" then ReadBook(lines) end
  if kind == "habitat" then ReadHabitat(lines) end
end)
