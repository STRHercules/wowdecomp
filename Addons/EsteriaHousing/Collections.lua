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

-- Size, height, forward and turn for a replica or mount: what stands on the object, and where.
local function PlacementLines(page, top)
  local rows = { { "size", "Size" }, { "height", "Height (yd above the object)" }, { "forward", "Ahead (yd)" },
    { "side", "Right (yd, negative: left)" }, { "turn", "Turn (degrees)" } }
  local boxes = {}
  for i, r in ipairs(rows) do
    local line = K.Line(page, i, top, r[2])
    local b = K.Box(line, 60); b:SetPoint("RIGHT", -4, 0)
    boxes[r[1]] = b
  end
  local function Fill(size, height, forward, turn, side)
    for k, v in pairs({ size = size, height = height, forward = forward, turn = turn, side = side }) do
      if not boxes[k]:HasFocus() then boxes[k]:SetText(v or "0") end
    end
  end
  local function Read()
    return Clean(boxes.size:GetText()), Clean(boxes.height:GetText()), Clean(boxes.forward:GetText()), Clean(boxes.turn:GetText()),
      Clean(boxes.side:GetText())
  end
  return Fill, Read
end

-- ------------------------------------------------------------------------------------------------ trophies

local TROPHY_KINDS = { "Trophy Wall Mount", "Trophy Pedestal", "Trophy Plaque", "Trophy Case" }
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
local tpages = K.Tabs(T, tbox, { "Trophy", "Style", "Label" }, function(i) T:PickTab(i) end)
local trophyList = K.List(tpages[1], -10, 13, 24)

Title(tpages[2], "Show it as", -12)
local styleFill = Choices(tpages[2], -28, { "Statue", "Trophy cup (gold)", "Trophy cup (silver)", "Trophy cup (gleaming)",
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

local tplace = CreateFrame("Frame", nil, tpages[3]); tplace:SetPoint("TOPLEFT", 0, -120); tplace:SetPoint("BOTTOMRIGHT")
Title(tpages[3], "Placement", -110)
local trophyPlaceFill, trophyPlaceRead = PlacementLines(tplace, -18)
local trophyLabelFill = LabelPage(tpages[3], -10, function(name, sub) if trophy then Send("label", trophy.placed, name, sub) end end)
local tplaceSave = K.Button(tplace, "Save Placement", 120); tplaceSave:SetPoint("TOPRIGHT", -8, -18 - 5 * (LINE_H + 2) - 6)

function H.SaveTrophyStyle()
  local size, height, forward, turn, side = trophyPlaceRead()
  Send("trophystyle", trophy.placed, trophy.style, trophy.effect, size, height, forward, turn, side)
end
tplaceSave:SetScript("OnClick", function() if trophy then H.SaveTrophyStyle() end end)

local function ReadTrophy(lines)
  local achievements, items = {}, {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "trophy" then
      trophy = { placed = f[2], owner = f[3] == "1", kind = Num(f[4]), source = Num(f[5]), id = Num(f[6]), date = f[7], name = f[8],
        style = Num(f[9]), effect = Num(f[10]), size = f[11], height = f[12], forward = f[13], turn = f[14], label = f[15], sub = f[16],
        link = f[17] or "", hasStatue = f[18] == "1", side = f[19] or "0" }
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
  effectText:SetText(EFFECTS[t.effect + 1] or "None")
  trophyPlaceFill(t.size, t.height, t.forward, t.turn, t.side)
  trophyLabelFill(t.label, t.sub)
  Show(tbox, t.owner)
  T:SetHeight(t.owner and 500 or 124)
  if not T:IsShown() then T:PickTab(1) end
  for i = 1, 3 do Show(_G["EsteriaHousingTrophyTab" .. i], t.owner) end
  T:Show()
end

-- ------------------------------------------------------------------------------------------------ mounts

local MOUNT_KINDS = { [5] = { "Mount Stall", "Your mount stands at its stall." }, [6] = { "Mount Paddock", "Your mount roams the paddock." },
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
local mountPlaceFill, mountPlaceRead = PlacementLines(mplace, 0)
local mplaceSave = K.Button(mplace, "Save Placement", 120); mplaceSave:SetPoint("TOPRIGHT", -8, -5 * (LINE_H + 2) - 4)
local mlabel = CreateFrame("Frame", nil, mpages[2]); mlabel:SetPoint("TOPLEFT", 0, -200); mlabel:SetPoint("BOTTOMRIGHT")
local mountLabelFill, mountHint = LabelPage(mlabel, 0, function(name, sub) if mount then Send("label", mount.placed, name, sub) end end)
mountHint:SetText("Leave the name empty to show the mount's own.")

function H.SaveMountStyle()
  local size, height, forward, turn, side = mountPlaceRead()
  Send("mountstyle", mount.placed, mount.finish, size, height, forward, turn, side, mount.motion or 1, Num(radiusBox:GetText()))
end
mplaceSave:SetScript("OnClick", function() if mount then H.SaveMountStyle() end end)

local function ReadMount(lines)
  local known = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "mount" then
      mount = { placed = f[2], owner = f[3] == "1", kind = Num(f[4]), spell = Num(f[5]), name = f[6], finish = Num(f[7]), size = f[8],
        height = f[9], forward = f[10], turn = f[11], label = f[12], sub = f[13], side = f[14] or "0", motion = Num(f[15]),
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
  mountPlaceFill(m.size, m.height, m.forward, m.turn, m.side)
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
local qpages = K.Tabs(MQ, qbox, { "Outfit", "Style", "Label", "Outfits" }, function(i) MQ:PickTab(i) end)

-- Outfit: a line per slot (click to change it, right-click to clear it); the pick list replaces them while choosing
local slotFrame = CreateFrame("Frame", nil, qpages[1]); slotFrame:SetAllPoints()
local pickFrame = CreateFrame("Frame", nil, qpages[1]); pickFrame:SetAllPoints(); pickFrame:Hide()
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
Title(qpages[2], "Mannequin", -10)
local typeFill = Choices(qpages[2], -26, TYPES, function(i) if mq then Send("mqstyle", mq.placed, i, mq.pose) end end)
Title(qpages[2], "Pose", -26 - 4 * (LINE_H + 2) - 8)
local poseFill = Choices(qpages[2], -26 - 4 * (LINE_H + 2) - 24, POSES, function(i) if mq then Send("mqstyle", mq.placed, mq.type, i) end end)

-- Label: its name and title, and the character creator for its race and features
local mqLabelFill, mqHint = LabelPage(qpages[3], -10, function(name, sub) if mq then Send("label", mq.placed, name, sub) end end)
mqHint:SetText("Its nameplate: a name, and a title under it.")
local customize = K.Button(qpages[3], "Customize Appearance", 170); customize:SetPoint("TOPLEFT", 12, -130)
customize:SetScript("OnClick", function() if mq then Send("mqappearance", mq.placed) end end)
local customizeNote = qpages[3]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
customizeNote:SetPoint("TOPLEFT", 12, -160); customizeNote:SetWidth(320); customizeNote:SetJustifyH("LEFT")
customizeNote:SetText("Race, body type, face, hair and the body sliders, as in character creation.")

-- Outfits: named sets of gear to put on again
local outfitLine = K.Line(qpages[4], 1, -10, "Name")
local outfitBox = K.Box(outfitLine, 150); outfitBox:SetPoint("RIGHT", -84, 0)
local outfitSave = K.Button(outfitLine, "Save", 74); outfitSave:SetPoint("RIGHT", -4, 0)
outfitSave:SetScript("OnClick", function() if mq then Send("mqsave", mq.placed, Clean(outfitBox:GetText())); outfitBox:ClearFocus() end end)
local outfitPicked
local outfitList = K.List(qpages[4], -44, 11, 24)
local outfitLoad = K.Button(qpages[4], "Wear", 90); outfitLoad:SetPoint("BOTTOMRIGHT", -8, 8)
outfitLoad:SetScript("OnClick", function() if mq and outfitPicked then Send("mqload", mq.placed, outfitPicked) end end)
local outfitDelete = K.Button(qpages[4], "Delete", 90); outfitDelete:SetPoint("RIGHT", outfitLoad, "LEFT", -6, 0)
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
      mq = { placed = f[2], owner = f[3] == "1", race = Num(f[4]), sex = Num(f[5]), type = Num(f[6]), pose = Num(f[7]), label = f[8], sub = f[9] }
    elseif f[1] == "slot" then mqSlots[Num(f[2])] = { entry = Num(f[3]), name = f[4], quality = f[5], link = f[6] or "" }
    elseif f[1] == "outfit" then outfits[#outfits + 1] = f[2] end
  end
  local n = 0
  for i, s in ipairs(SLOTS) do
    local it = mqSlots[s[1]]
    slotLines[i].value:SetText(it and Quality(it.name, it.quality) or "|cff606060Empty|r")
    if it then n = n + 1 end
  end
  MQ.title:SetText(mq.label ~= "" and mq.label or "Mannequin")
  MQ.status:SetText(string.format("%s, %s  |cffb0b0b0%d %s|r", TYPES[mq.type + 1] or "", POSES[mq.pose + 1] or "", n, n == 1 and "piece" or "pieces"))
  MQ.note:SetText(mq.owner and "Gear here is a copy of its look; your items stay yours." or (mq.sub ~= "" and mq.sub or ""))
  typeFill(mq.type); poseFill(mq.pose)
  mqLabelFill(mq.label, mq.sub)
  table.sort(outfits)
  DrawOutfits(outfits)
  Show(strip, mq.owner)
  -- visitors inspect the outfit only
  local first = not MQ:IsShown()
  MQ:SetHeight(560)
  if first then pickFrame:Hide(); slotFrame:Show(); MQ:PickTab(1) end
  for i = 1, 4 do Show(_G["EsteriaHousingMannequinTab" .. i], mq.owner or i == 1) end
  if not mq.owner then MQ:PickTab(1, { true, false, false, false }) end
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
local areaSave = K.Button(hpages[2], "Save", 90); areaSave:SetPoint("TOPRIGHT", -8, -100)

local function SaveHabitat()
  local list = {}
  for spell in pairs(chosen) do list[#list + 1] = spell end
  Send("habset", hab.placed, Num(radiusBox:GetText()), table.concat(list, ","))
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
    if f[1] == "hab" then hab = { placed = f[2], owner = f[3] == "1", radius = f[4], max = Num(f[5]) }
    elseif f[1] == "pet" then
      pets[#pets + 1] = { spell = Num(f[2]), name = f[3] }
      if f[4] == "1" then chosen[Num(f[2])] = true end
    end
  end
  table.sort(pets, function(a, b) return a.name < b.name end)
  HB.title:SetText("Pet Habitat")
  HB.note:SetText(hab.owner and "Choose who lives here; they stay in your collection." or "")
  if not radiusBox:HasFocus() then radiusBox:SetText(hab.radius) end
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
