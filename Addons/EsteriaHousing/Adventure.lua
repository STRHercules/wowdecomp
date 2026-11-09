-- Esteria housing: combat, training and adventure (the spec's section 3). Training dummies, portals and meeting stones are
-- decor in the Utility category; this file is their windows. It talks over the commerce channel: commands start with
-- "adv\t" (EsteriaHousing_Commerce) and the answers are "training", "portal", "stone", "stoneinvite" and "homechoice"
-- records (ESTERIA_HOUSING_COMMERCE; mod-esteria-housing HousingAdventure.inc writes them).
--
-- The windows share one look, after retail's housing panels: a round portrait in a gold ring, a title and status line,
-- then tabs standing on an inset, striped lines and searchable lists inside it.

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local DUMMY_FIRST, DUMMY_LAST = 9505000, 9505003 -- creature entries of the four dummies
local KINDS = { "Training Dummy", "Tank Dummy", "Healing Dummy", "Boss Dummy" }
local KIND_NOTE = { "Practice your damage.", "It fights back: take its hits.", "It bleeds: keep it alive.",
  "It heals itself: interrupt it." }
local LINE_H, BOX_TOP = 24, -116

local function Send(...) EsteriaHousing_Commerce(table.concat({ "adv", ... }, "\t")) end
local function Fields(line) local out = {} for f in (line .. "\t"):gmatch("([^\t]*)\t") do out[#out + 1] = f end return out end
local function Num(v) return tonumber(v) or 0 end
local function Short(n)
  n = Num(n)
  if n >= 1e6 then return string.format("%.2fM", n / 1e6) elseif n >= 1e4 then return string.format("%.1fk", n / 1e3) end
  return tostring(math.floor(n))
end
local function Clock(s) s = Num(s) return string.format("%d:%02d", math.floor(s / 60), s % 60) end
local function Show(w, on) if on then w:Show() else w:Hide() end end

-- ------------------------------------------------------------------------------------------------ the shared look

local function Window(name, w, h, icon)
  local f = H.Panel(name, w, h)
  tinsert(UISpecialFrames, name)
  local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
  close:SetPoint("TOPRIGHT", 2, 2)
  f.portrait = f:CreateTexture(nil, "ARTWORK")
  f.portrait:SetSize(54, 54); f.portrait:SetPoint("TOPLEFT", 18, -14)
  SetPortraitToTexture(f.portrait, icon)
  local ring = H.Atlas(f:CreateTexture(nil, "OVERLAY"), "housing-layout-room-orb-ring-default")
  ring:SetSize(74, 74); ring:SetPoint("CENTER", f.portrait, "CENTER", 0, 0)
  -- the title sits in the panel's header band, the status and note below it, each one line cut short
  f.title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
  f.title:SetPoint("TOPLEFT", 86, -14); f.title:SetPoint("RIGHT", -30, 0); f.title:SetJustifyH("LEFT"); f.title:SetHeight(18)
  f.status = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  f.status:SetPoint("TOPLEFT", 86, -48); f.status:SetPoint("RIGHT", -16, 0); f.status:SetJustifyH("LEFT"); f.status:SetHeight(12)
  f.note = f:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
  f.note:SetPoint("TOPLEFT", 86, -63); f.note:SetPoint("RIGHT", -16, 0); f.note:SetJustifyH("LEFT"); f.note:SetHeight(12)
  local divider = H.Atlas(f:CreateTexture(nil, "ARTWORK"), "housing-floor-list-divider-active")
  divider:SetPoint("TOPLEFT", 16, -82); divider:SetPoint("TOPRIGHT", -16, -82); divider:SetHeight(2)
  return f
end

-- the inset the tabs stand on; pages are frames filling it
local function Inset(f, top, bottom)
  local box = CreateFrame("Frame", nil, f)
  box:SetPoint("TOPLEFT", 14, top); box:SetPoint("BOTTOMRIGHT", -14, bottom)
  box:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
  box:SetBackdropColor(0, 0, 0, 0.45); box:SetBackdropBorderColor(0.55, 0.42, 0.25)
  return box
end

local function Tabs(f, box, names, onPick)
  local tabs, pages = {}, {}
  for i, name in ipairs(names) do
    local page = CreateFrame("Frame", nil, box); page:SetAllPoints(); page:Hide()
    pages[i] = page
    local b = CreateFrame("Button", f:GetName() .. "Tab" .. i, f, "OptionsFrameTabButtonTemplate")
    b:SetID(i); b:SetText(name)
    if i == 1 then b:SetPoint("BOTTOMLEFT", box, "TOPLEFT", 6, -2) else b:SetPoint("LEFT", tabs[i - 1], "RIGHT", -14, 0) end
    PanelTemplates_TabResize(b, 0)
    b:SetScript("OnClick", function() PlaySound("igCharacterInfoTab"); onPick(i) end)
    tabs[i] = b
  end
  f.numTabs = #names
  function f:PickTab(i, shown)
    for k, page in ipairs(pages) do Show(page, k == i); Show(tabs[k], shown == nil or shown[k]) end
    PanelTemplates_SetTab(self, i)
  end
  return pages, tabs
end

-- a striped line inside a page: its label on the left, a value or control on the right
local function Line(parent, i, top, label)
  local line = CreateFrame("Frame", nil, parent)
  line:SetHeight(LINE_H); line:SetPoint("TOPLEFT", 6, top - (i - 1) * (LINE_H + 2)); line:SetPoint("RIGHT", -6, 0)
  local bg = line:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
  line.label = line:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
  line.label:SetPoint("LEFT", 8, 0); line.label:SetText(label or "")
  line.value = line:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  line.value:SetPoint("RIGHT", -8, 0); line.value:SetJustifyH("RIGHT")
  return line
end

local function Button(parent, text, width)
  local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
  b:SetSize(width or 90, 22); b:SetText(text)
  return b
end

-- a text field drawn like the dropdowns: a dark box with the housing border
local function Box(parent, width, numeric)
  local b = CreateFrame("EditBox", nil, parent)
  b:SetSize(width, 20); b:SetAutoFocus(false); b:SetFontObject(GameFontHighlightSmall)
  b:SetTextInsets(6, 6, 0, 0)
  b:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize = 10, insets = { left = 2, right = 2, top = 2, bottom = 2 } })
  b:SetBackdropColor(0, 0, 0, 0.8); b:SetBackdropBorderColor(0.55, 0.42, 0.25)
  if numeric then b:SetNumeric(true) end
  b:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
  b:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
  return b
end

local function Check(parent)
  local c = CreateFrame("CheckButton", nil, parent, "UICheckButtonTemplate")
  c:SetSize(22, 22)
  return c
end

-- A searchable list: header rows (an expansion, "Cities") and pickable rows, the picked one lit as retail's floor list.
-- items = { { header = true, text } | { text, sub, picked, fn } }
local listCount = 0
local function List(parent, top, rows, rowH)
  listCount = listCount + 1
  local name = "EsteriaHousingAdventureList" .. listCount
  local l = { items = {}, filter = "" }
  l.search = Box(parent, 180)
  l.search:SetPoint("TOPLEFT", 10, top)
  local hint = l.search:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
  hint:SetPoint("LEFT", 7, 0); hint:SetText("Search")
  local sf = CreateFrame("ScrollFrame", name, parent, "FauxScrollFrameTemplate")
  sf:SetPoint("TOPLEFT", 6, top - 26); sf:SetPoint("RIGHT", -28, 0); sf:SetHeight(rows * rowH)
  l.buttons = {}
  for i = 1, rows do
    local b = CreateFrame("Button", nil, parent)
    b:SetHeight(rowH - 2); b:SetPoint("TOPLEFT", 8, top - 26 - (i - 1) * rowH); b:SetPoint("RIGHT", -30, 0)
    b.picked = H.Atlas(b:CreateTexture(nil, "BACKGROUND"), "housing-floor-list-bg-active"); b.picked:SetAllPoints()
    b.line = H.Atlas(b:CreateTexture(nil, "ARTWORK"), "housing-floor-list-divider-active")
    b.line:SetPoint("BOTTOMLEFT", 0, 2); b.line:SetPoint("BOTTOMRIGHT", 0, 2); b.line:SetHeight(2)
    b.icon = b:CreateTexture(nil, "ARTWORK"); b.icon:SetSize(rowH - 6, rowH - 6); b.icon:SetPoint("LEFT", 6, 0)
    b.text = b:CreateFontString(nil, "OVERLAY", "GameFontHighlight"); b.text:SetPoint("LEFT", 10, 0); b.text:SetJustifyH("LEFT")
    b.sub = b:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall"); b.sub:SetPoint("RIGHT", -8, 0)
    b.text:SetPoint("RIGHT", b.sub, "LEFT", -6, 0); b.text:SetHeight(14) -- one line, cut short before the detail
    b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    b:SetScript("OnClick", function(self) if self.item and self.item.fn then PlaySound("igMainMenuOptionCheckBoxOn"); self.item.fn() end end)
    -- an item's tooltip (link) or a line of detail (tip)
    b:SetScript("OnEnter", function(self)
      local it = self.item
      if not it or not (it.link or it.tip) then return end
      GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
      if it.link then GameTooltip:SetHyperlink(it.link) else GameTooltip:AddLine(it.text); GameTooltip:AddLine(it.tip, 1, 1, 1, true) end
      GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    l.buttons[i] = b
  end
  function l:Update()
    local shown, lastHeader = {}, nil
    local q = self.filter:lower()
    for _, it in ipairs(self.items) do
      if it.header then lastHeader = it
      elseif q == "" or it.text:lower():find(q, 1, true) then
        if lastHeader then shown[#shown + 1] = lastHeader; lastHeader = nil end
        shown[#shown + 1] = it
      end
    end
    FauxScrollFrame_Update(sf, #shown, rows, rowH)
    local offset = FauxScrollFrame_GetOffset(sf)
    for i, b in ipairs(self.buttons) do
      local it = shown[offset + i]
      b.item = it
      if not it then b:Hide()
      else
        b:Show()
        b.text:SetText(it.text); b.sub:SetText(it.sub or "")
        b.icon:SetTexture(it.icon); Show(b.icon, it.icon ~= nil)
        b.text:SetPoint("LEFT", it.icon and (rowH + 6) or 10, 0)
        b.text:SetFontObject(it.header and GameFontNormal or it.dim and GameFontDisable or GameFontHighlight)
        Show(b.picked, it.picked); Show(b.line, it.header)
        if it.header then b:Disable() else b:Enable() end
      end
    end
  end
  sf:SetScript("OnVerticalScroll", function(self, offset) FauxScrollFrame_OnVerticalScroll(self, offset, rowH, function() l:Update() end) end)
  l.search:SetScript("OnTextChanged", function(self) l.filter = self:GetText(); Show(hint, l.filter == "" and not self:HasFocus()); l:Update() end)
  l.search:SetScript("OnEditFocusGained", function() hint:Hide() end)
  l.search:SetScript("OnEditFocusLost", function(self) Show(hint, self:GetText() == "") end)
  return l
end

-- ------------------------------------------------------------------------------------------------ training

local T = Window("EsteriaHousingTraining", 340, 470, "Interface\\Icons\\Ability_Warrior_DefensiveStance")
T:SetPoint("RIGHT", -60, 40)
local dummy -- the last "training" answer

-- the headline number: what the dummy trains (DPS, HPS, damage taken per second)
local big = T:CreateFontString(nil, "OVERLAY", "GameFontNormalHuge")
big:SetPoint("TOP", 0, -92)
local bigLabel = T:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
bigLabel:SetPoint("TOP", big, "BOTTOM", 0, -2)

local tbox = Inset(T, -150, 46)
local tpages = Tabs(T, tbox, { "Session", "Settings" }, function(i) T:PickTab(i, { true, dummy and dummy.owner }); H.TrainingButtons(i) end)

-- Session: a two-column grid of what it measured
local STATS = { { "elapsed", "Time" }, { "damage", "Damage" }, { "dps", "DPS" }, { "healing", "Healing" }, { "hps", "HPS" },
  { "overheal", "Overhealing" }, { "taken", "Damage taken" }, { "kicks", "Interrupts" }, { "drops", "Patient dropped" },
  { "targets", "Targets" } }
local cells = {}
for i, s in ipairs(STATS) do
  local c = CreateFrame("Frame", nil, tpages[1])
  c:SetSize(146, 34)
  local bg = c:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, 0.05)
  c.label = c:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall"); c.label:SetPoint("TOPLEFT", 8, -4); c.label:SetText(s[2])
  c.value = c:CreateFontString(nil, "OVERLAY", "GameFontHighlight"); c.value:SetPoint("BOTTOMLEFT", 8, 4)
  cells[s[1]] = c
end
local best = tpages[1]:CreateFontString(nil, "OVERLAY", "GameFontNormal")
local trophy = H.Atlas(tpages[1]:CreateTexture(nil, "ARTWORK"), "house-chest-trophy-icon_32")
trophy:SetSize(16, 16)
local result = tpages[1]:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
result:SetWidth(290); result:SetJustifyH("LEFT")

local stop = Button(T, "Stop", 90); stop:SetPoint("BOTTOMLEFT", 16, 16)
stop:SetScript("OnClick", function() Send("stop") end)
local reset = Button(T, "Reset", 90); reset:SetPoint("LEFT", stop, "RIGHT", 6, 0)
reset:SetScript("OnClick", function() Send("reset") end)
function H.TrainingButtons(tab) Show(stop, tab == 1); Show(reset, tab == 1) end

-- Settings (the owner's): striped lines, Save at the foot
local CONFIG = { { "level", "Level", 50 }, { "armor", "Armor (0: normal for its level)", 70 }, { "resist", "Resistances", 70 },
  { "health", "Health", 90 }, { "killable", "Can be defeated" }, { "duration", "Session length, seconds (0: until stopped)", 60 },
  { "interrupts", "Boss: heals itself (interrupt them)" }, { "enrageOn", "Boss: enrage timer" }, { "enrage", "Enrage after, seconds", 60 } }
local fields = {}
for i, c in ipairs(CONFIG) do
  local line = Line(tpages[2], i, -8, c[2])
  local w = c[3] and Box(line, c[3], true) or Check(line)
  w:SetPoint("RIGHT", c[3] and -8 or -4, 0)
  w.line = line
  fields[c[1]] = w
end
local save = Button(tpages[2], "Save", 90); save:SetPoint("BOTTOMRIGHT", -8, 8)
save:SetScript("OnClick", function()
  if not dummy then return end
  local drills = (fields.interrupts:GetChecked() and 1 or 0) + (fields.enrageOn:GetChecked() and 4 or 0)
  Send("dummyset", dummy.placed, fields.level:GetText(), fields.armor:GetText(), fields.resist:GetText(), fields.health:GetText(),
    fields.killable:GetChecked() and 1 or 0, fields.duration:GetText(), drills, fields.enrage:GetText())
  T:PickTab(1, { true, true })
end)

local function ReadTraining(lines)
  local d = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "dummy" then d.kind, d.owner, d.placed, d.name = Num(f[3]), f[4] == "1", f[5], f[6]
    elseif f[1] == "config" then
      d.config = { level = f[2], armor = f[3], resist = f[4], health = f[5], killable = f[6] == "1", duration = f[7],
        drills = Num(f[8]), enrage = f[9] }
    elseif f[1] == "session" then
      d.session = { active = f[2] == "1", elapsed = Num(f[3]), damage = f[4], dps = f[5], healing = f[6], hps = f[7],
        overheal = f[8], taken = f[9], kicks = Num(f[10]), missed = Num(f[11]), puddles = f[12], drops = f[13], targets = f[14],
        duration = Num(f[15]), result = f[16] or "" }
    elseif f[1] == "best" then d.best = f[2] end
  end
  local first = not T:IsShown() or not dummy or dummy.placed ~= d.placed
  dummy = d
  local s, kind = d.session, d.kind
  T.title:SetText(d.name ~= "" and d.name or KINDS[kind] or "Training Dummy")
  T.note:SetText(KIND_NOTE[kind] or "")
  if UnitExists("target") then SetPortraitTexture(T.portrait, "target") end
  local secs = s.duration > 0 and string.format("%s / %s", Clock(s.elapsed), Clock(s.duration)) or Clock(s.elapsed)
  T.status:SetText(s.active and ("|cff40ff40Session running|r  " .. secs) or (s.elapsed > 0 and "|cffffd100Session over|r" or "Ready"))
  local heal, tank = kind == 3, kind == 2
  big:SetText(Short(heal and s.hps or tank and (s.elapsed > 0 and Num(s.taken) / s.elapsed or 0) or s.dps))
  bigLabel:SetText(heal and "Healing per second" or tank and "Damage taken per second" or "Damage per second")
  local values = { elapsed = secs, damage = Short(s.damage), dps = Short(s.dps), healing = Short(s.healing), hps = Short(s.hps),
    overheal = Short(s.overheal), taken = Short(s.taken), kicks = string.format("%d of %d", s.kicks, s.kicks + s.missed),
    drops = s.drops, targets = s.targets }
  local shown = { elapsed = true, targets = true, damage = not heal, dps = not heal, healing = heal, hps = heal, overheal = heal,
    drops = heal, taken = tank, kicks = kind == 4 }
  local n = 0
  for _, st in ipairs(STATS) do
    local c = cells[st[1]]
    if shown[st[1]] then
      c:ClearAllPoints(); c:SetPoint("TOPLEFT", 8 + (n % 2) * 150, -8 - math.floor(n / 2) * 38)
      c.value:SetText(values[st[1]]); c:Show(); n = n + 1
    else c:Hide() end
  end
  local below = -8 - math.ceil(n / 2) * 38 - 6
  trophy:ClearAllPoints(); trophy:SetPoint("TOPLEFT", 10, below)
  best:ClearAllPoints(); best:SetPoint("LEFT", trophy, "RIGHT", 6, 0)
  best:SetText("Personal best: |cffffffff" .. Short(d.best) .. (heal and " HPS" or " DPS") .. "|r")
  result:ClearAllPoints(); result:SetPoint("TOPLEFT", 10, below - 24)
  result:SetText(s.result ~= "" and s.result or (s.active and "" or (heal and "Heal it to start a session." or "Attack it to start a session.")))
  local c = d.config
  for _, k in ipairs({ "level", "armor", "resist", "health", "duration", "enrage" }) do
    if not fields[k]:HasFocus() then fields[k]:SetText(c[k]) end
  end
  fields.killable:SetChecked(c.killable)
  fields.interrupts:SetChecked(c.drills % 2 == 1)
  fields.enrageOn:SetChecked(math.floor(c.drills / 4) % 2 == 1)
  for _, k in ipairs({ "interrupts", "enrageOn", "enrage" }) do Show(fields[k].line, kind == 4) end
  if first then T:PickTab(1, { true, d.owner }); H.TrainingButtons(1) end
  T:Show()
end

-- targeting one of the dummies opens its window; the server keeps it live while a session runs
local function DummyGuid()
  local guid = UnitGUID("target")
  if not guid or #guid < 12 then return end
  local entry = tonumber(guid:sub(7, 12), 16)
  if entry and entry >= DUMMY_FIRST and entry <= DUMMY_LAST then return guid end
end
local watcher = CreateFrame("Frame")
watcher:RegisterEvent("PLAYER_TARGET_CHANGED")
watcher:SetScript("OnEvent", function()
  local guid = DummyGuid()
  if guid then Send("dummy", guid) end
end)
T:SetScript("OnHide", function() Send("close") end)

-- ------------------------------------------------------------------------------------------------ portals

local P = Window("EsteriaHousingPortal", 380, 470, "Interface\\Icons\\Spell_Arcane_PortalDalaran")
P:SetPoint("LEFT", 60, 40)
local portal, destinations, choice = nil, {}, {}
local LOOKS = { "K'areshi Portal", "Portal to Damnation", "Translocation Portal", "Mage Portal" }
local ACCESS = { { 16, "Party and raid members" }, { 2, "Friends" }, { 4, "Guild members" }, { 1, "Everyone" } }
local FACTION = { [0] = "", [1] = "|cff4080ffAlliance|r", [2] = "|cffff4040Horde|r" }

local travel = Button(P, "Travel", 100); travel:SetPoint("BOTTOMRIGHT", -16, 14)
travel:SetScript("OnClick", function()
  if not portal then return end
  local dialog = StaticPopup_Show("ESTERIA_HOUSING_PORTAL_GO", portal.destName)
  if dialog then dialog.data = portal.placed end
end)
StaticPopupDialogs["ESTERIA_HOUSING_PORTAL_GO"] = {
  text = "Step through the portal to %s?", button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, placed) Send("portalgo", placed); P:Hide() end,
}

local pbox = Inset(P, BOX_TOP, 44)
local ppages = Tabs(P, pbox, { "Destination", "Appearance", "Access" }, function(i) P:PickTab(i) end)

-- every change saves at once
local function SavePortal()
  if portal then
    Send("portalset", portal.placed, choice.kind or 0, choice.dest or 0, choice.hood or 0, choice.look or 0, choice.access or 0,
      ((choice.name or ""):gsub("[\t\n]", " ")))
  end
end

local destList = List(ppages[1], -10, 11, 24)

local nameLine = Line(ppages[2], 1, -10, "Name")
local nameBox = Box(nameLine, 200); nameBox:SetPoint("RIGHT", -4, 0)
nameBox:SetScript("OnEnterPressed", function(self) self:ClearFocus(); choice.name = self:GetText(); SavePortal() end)
local lookTitle = ppages[2]:CreateFontString(nil, "OVERLAY", "GameFontNormal")
lookTitle:SetPoint("TOPLEFT", 12, -46); lookTitle:SetText("Look")
local lookLines = {}
for i, name in ipairs(LOOKS) do
  local line = Line(ppages[2], i, -64, name)
  local c = Check(line); c:SetPoint("RIGHT", -4, 0)
  c:SetScript("OnClick", function() choice.look = i - 1; SavePortal() end)
  line.check = c
  lookLines[i] = line
end

local accessTitle = ppages[3]:CreateFontString(nil, "OVERLAY", "GameFontNormal")
accessTitle:SetPoint("TOPLEFT", 12, -12); accessTitle:SetText("Who may step through")
local accessLines = {}
for i, a in ipairs(ACCESS) do
  local line = Line(ppages[3], i, -30, a[2])
  local c = Check(line); c:SetPoint("RIGHT", -4, 0)
  c:SetScript("OnClick", function(self)
    local on = bit.band(choice.access or 0, a[1]) ~= 0
    if on ~= (self:GetChecked() and true or false) then choice.access = bit.bxor(choice.access or 0, a[1]) end
    SavePortal()
  end)
  line.check = c
  accessLines[i] = line
end
local accessNote = ppages[3]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
accessNote:SetPoint("TOPLEFT", 12, -30 - 4 * (LINE_H + 2) - 8); accessNote:SetWidth(320); accessNote:SetJustifyH("LEFT")
accessNote:SetText("You can always use your own portals. A city also checks the traveler's faction and level.")

local function ReadPortal(lines)
  destinations = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "portal" then
      portal = { placed = f[2], owner = f[3] == "1", name = f[4], destName = f[5] ~= "" and f[5] or nil }
      choice = { kind = Num(f[6]), dest = Num(f[7]), hood = Num(f[8]), look = Num(f[9]), access = Num(f[10]), name = f[4] }
    elseif f[1] == "dest" then
      destinations[#destinations + 1] = { kind = Num(f[2]), id = Num(f[3]), hood = Num(f[4]), name = f[5], faction = Num(f[6]),
        level = Num(f[7]) }
    end
  end
  P.title:SetText(portal.name ~= "" and portal.name or "Portal")
  P.status:SetText(portal.destName and ("Leads to |cffffffff" .. portal.destName .. "|r") or "|cff808080Leads nowhere yet|r")
  P.note:SetText(portal.owner and "Your portal. Changes save at once." or "")
  if portal.destName then travel:Enable() else travel:Disable() end
  -- the owner's tabs
  local items = { { header = true, text = "Cities" } }
  for _, d in ipairs(destinations) do
    if d.kind == 0 then
      items[#items + 1] = { text = d.name, sub = FACTION[d.faction] .. (d.level > 1 and ("  level " .. d.level) or ""),
        picked = choice.kind == 0 and choice.dest == d.id, fn = function() choice.kind, choice.dest, choice.hood = 0, d.id, 0; SavePortal() end }
    end
  end
  items[#items + 1] = { header = true, text = "My Homes" }
  for _, d in ipairs(destinations) do
    if d.kind == 1 then
      items[#items + 1] = { text = d.name, picked = choice.kind == 1 and choice.dest == d.id and choice.hood == d.hood,
        fn = function() choice.kind, choice.dest, choice.hood = 1, d.id, d.hood; SavePortal() end }
    end
  end
  destList.items = items
  destList:Update()
  if not nameBox:HasFocus() then nameBox:SetText(portal.name) end
  for i, line in ipairs(lookLines) do line.check:SetChecked(choice.look == i - 1) end
  for i, a in ipairs(ACCESS) do accessLines[i].check:SetChecked(bit.band(choice.access or 0, a[1]) ~= 0) end
  Show(pbox, portal.owner)
  P:SetHeight(portal.owner and 470 or 124)
  if not P:IsShown() then P:PickTab(1) end
  for i = 1, 3 do Show(_G["EsteriaHousingPortalTab" .. i], portal.owner) end
  P:Show()
end

-- ------------------------------------------------------------------------------------------------ meeting stones

local M = Window("EsteriaHousingStone", 380, 470, "Interface\\Icons\\Spell_Shadow_Twilight")
M:SetPoint("LEFT", 60, 40)
local stone, stoneOptions = nil, {}
local EXPANSIONS = { [0] = "Classic", [1] = "Burning Crusade", [2] = "Wrath of the Lich King" }

local go = Button(M, "Travel", 100); go:SetPoint("BOTTOMRIGHT", -16, 14)
go:SetScript("OnClick", function() if stone then Send("stonego", stone.placed); M:Hide() end end)
local party = Button(M, "Take Party", 100); party:SetPoint("RIGHT", go, "LEFT", -6, 0)
party:SetScript("OnClick", function() if stone then Send("stoneparty", stone.placed); M:Hide() end end)
party:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:AddLine("Take Party")
  GameTooltip:AddLine("You go, and each party member beside you is asked to come along. Each goes only if they qualify.", 1, 1, 1, true)
  GameTooltip:Show()
end)
party:SetScript("OnLeave", function() GameTooltip:Hide() end)

local mbox = Inset(M, BOX_TOP, 44)
local mpages = Tabs(M, mbox, { "Dungeons", "Appearance" }, function(i) M:PickTab(i) end)
local dungeonList = List(mpages[1], -10, 10, 24)
-- its look, as the portal's
local STONE_LOOKS = { "Harandar Runestone", "Runestone Ring", "Rune Pillar", "Meeting Stone" }
local stoneLookTitle = mpages[2]:CreateFontString(nil, "OVERLAY", "GameFontNormal")
stoneLookTitle:SetPoint("TOPLEFT", 12, -12); stoneLookTitle:SetText("Look")
local stoneLookLines = {}
for i, name in ipairs(STONE_LOOKS) do
  local line = Line(mpages[2], i, -30, name)
  local c = Check(line); c:SetPoint("RIGHT", -4, 0)
  c:SetScript("OnClick", function() if stone then Send("stonelook", stone.placed, i - 1) end end)
  line.check = c
  stoneLookLines[i] = line
end
local mNote = mpages[1]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
mNote:SetPoint("BOTTOMLEFT", 12, 8); mNote:SetWidth(320); mNote:SetJustifyH("LEFT")
mNote:SetText("Travelers arrive at the entrance, never inside.")

local function ReadStone(lines)
  stoneOptions = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "stone" then
      stone = { placed = f[2], owner = f[3] == "1", name = f[4], level = Num(f[5]), expansion = Num(f[6]), guid = f[7],
        grouped = f[8] == "1", look = Num(f[9]) }
    elseif f[1] == "stoneopt" then
      stoneOptions[#stoneOptions + 1] = { guid = f[2], name = f[3], level = Num(f[4]), expansion = Num(f[5]) }
    end
  end
  local bound = stone.name ~= ""
  M.title:SetText(bound and stone.name or "Meeting Stone")
  M.status:SetText(bound and string.format("Level %d+  |cffb0b0b0%s|r", stone.level, EXPANSIONS[stone.expansion] or "")
    or "|cff808080Not bound to a dungeon yet|r")
  M.note:SetText(bound and "Travels to the dungeon's entrance." or "")
  for i, line in ipairs(stoneLookLines) do line.check:SetChecked(stone.look == i - 1) end
  if bound then go:Enable() else go:Disable() end
  if bound and stone.grouped then party:Enable() else party:Disable() end
  local items = {}
  for x = 0, 2 do
    local group = {}
    for _, o in ipairs(stoneOptions) do
      if o.expansion == x then
        group[#group + 1] = { text = o.name, sub = "level " .. o.level, picked = stone.guid == o.guid,
          fn = function() Send("stoneset", stone.placed, o.guid) end }
      end
    end
    table.sort(group, function(a, b) return a.text < b.text end)
    if #group > 0 then
      items[#items + 1] = { header = true, text = EXPANSIONS[x] }
      for _, it in ipairs(group) do items[#items + 1] = it end
    end
  end
  dungeonList.items = items
  dungeonList:Update()
  Show(mbox, stone.owner)
  M:SetHeight(stone.owner and 470 or 124)
  if stone.owner and not M:IsShown() then M:PickTab(1) end
  for i = 1, 2 do Show(_G["EsteriaHousingStoneTab" .. i], stone.owner) end
  M:Show()
end

StaticPopupDialogs["ESTERIA_HOUSING_STONE_INVITE"] = {
  text = "%s", button1 = ACCEPT, button2 = DECLINE, timeout = 60, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, placed) Send("stonego", placed) end,
}

-- the shared look, for the collections windows (Collections.lua)
H.Kit = { Window = Window, Inset = Inset, Tabs = Tabs, Line = Line, Button = Button, Box = Box, Check = Check, List = List,
  Fields = Fields, Num = Num, Show = Show, LINE_H = LINE_H, BOX_TOP = BOX_TOP }

-- ------------------------------------------------------------------------------------------------ answers

local listener = CreateFrame("Frame")
listener:RegisterEvent("ESTERIA_HOUSING_COMMERCE")
listener:SetScript("OnEvent", function()
  local lines = {}
  for line in EsteriaHousing_GetCommerce():gmatch("[^\n]+") do lines[#lines + 1] = line end
  local kind = lines[1] and lines[1]:match("^[^\t]+")
  if kind == "training" then ReadTraining(lines)
  elseif kind == "portal" then ReadPortal(lines)
  elseif kind == "stone" then ReadStone(lines)
  elseif kind == "stoneinvite" then
    local f = Fields(lines[1])
    local dialog = StaticPopup_Show("ESTERIA_HOUSING_STONE_INVITE", string.format("%s is taking the party to %s. Go along?", f[4], f[3]))
    if dialog then dialog.data = f[2] end
  elseif kind == "homechoice" then
    local f = Fields(lines[1])
    H.homeChoice = { house = Num(f[2]), hood = Num(f[3]) }
    if H.RedrawNeighborhood then H.RedrawNeighborhood() end
  end
end)
