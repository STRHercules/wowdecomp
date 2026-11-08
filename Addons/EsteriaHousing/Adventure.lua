-- Esteria housing: combat, training and adventure (the spec's section 3). Training dummies, portals and meeting stones are
-- decor in the Utility category; this file is their windows. It talks over the commerce channel: commands start with
-- "adv\t" (EsteriaHousing_Commerce) and the answers are "training", "portal", "stone", "stoneinvite" and "homechoice"
-- records (ESTERIA_HOUSING_COMMERCE; mod-esteria-housing HousingAdventure.inc writes them).

if not EsteriaHousing_IsAvailable then return end
EsteriaHousingUI = EsteriaHousingUI or {}
local H = EsteriaHousingUI

local DUMMY_FIRST, DUMMY_LAST = 9505000, 9505003 -- creature entries of the four dummies
local KINDS = { "Training Dummy", "Tank Dummy", "Healing Dummy", "Boss Dummy" }
local ROW_H = 22

local function Send(...) EsteriaHousing_Commerce(table.concat({ "adv", ... }, "\t")) end
local function Fields(line) local out = {} for f in (line .. "\t"):gmatch("([^\t]*)\t") do out[#out + 1] = f end return out end
local function Num(v) return tonumber(v) or 0 end
local function Short(n)
  n = Num(n)
  if n >= 1e6 then return string.format("%.2fM", n / 1e6) elseif n >= 1e4 then return string.format("%.1fk", n / 1e3) end
  return tostring(math.floor(n))
end

local menu = CreateFrame("Frame", "EsteriaHousingAdventureMenu", UIParent, "UIDropDownMenuTemplate")

-- a window in the housing style: title, a close button and striped lines with a label on the left
local function Window(name, w, h, titleText)
  local f = H.Panel(name, w, h)
  tinsert(UISpecialFrames, name)
  f.title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
  f.title:SetPoint("TOP", 0, -9); f.title:SetText(titleText)
  f.sub = f:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  f.sub:SetPoint("TOP", f.title, "BOTTOM", 0, -2)
  local close = CreateFrame("Button", nil, f, "UIPanelCloseButton")
  close:SetPoint("TOPRIGHT", 2, 2)
  f.lines = {}
  function f:Line(i, top, label)
    local line = CreateFrame("Frame", nil, self)
    line:SetSize(w - 28, ROW_H); line:SetPoint("TOPLEFT", 14, top - (i - 1) * (ROW_H + 2))
    local bg = line:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
    line.label = line:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    line.label:SetPoint("LEFT", 8, 0); line.label:SetText(label or "")
    line.value = line:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    line.value:SetPoint("RIGHT", -8, 0); line.value:SetJustifyH("RIGHT")
    return line
  end
  return f
end

local function Box(parent, width, numeric)
  local b = CreateFrame("EditBox", nil, parent, "InputBoxTemplate")
  b:SetSize(width, 18); b:SetAutoFocus(false); b:SetPoint("RIGHT", -8, 0)
  if numeric then b:SetNumeric(true) end
  b:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
  b:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
  return b
end

local function Check(parent)
  local c = CreateFrame("CheckButton", nil, parent, "UICheckButtonTemplate")
  c:SetSize(22, 22); c:SetPoint("RIGHT", -4, 0)
  return c
end

local function Button(parent, text, width)
  local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
  b:SetSize(width or 80, 22); b:SetText(text)
  return b
end

-- ------------------------------------------------------------------------------------------------ training

local T = Window("EsteriaHousingTraining", 300, 470, "Training Hall")
T:SetPoint("RIGHT", -60, 40)
local dummy -- the last "training" answer

-- the session: what it measured so far
local STATS = { { "elapsed", "Time" }, { "damage", "Damage" }, { "dps", "DPS" }, { "healing", "Healing" }, { "hps", "HPS" },
  { "overheal", "Overhealing" }, { "taken", "Damage taken" }, { "kicks", "Interrupts" }, { "puddles", "Puddle hits" },
  { "drops", "Patient dropped" }, { "targets", "Targets" }, { "best", "Personal best" } }
local statLines = {}
for i, s in ipairs(STATS) do statLines[s[1]] = T:Line(i, -50, s[2]) end
local result = T:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
result:SetPoint("TOPLEFT", 18, -50 - #STATS * (ROW_H + 2) - 4); result:SetWidth(264); result:SetJustifyH("LEFT")

local stop = Button(T, "Stop", 80); stop:SetPoint("TOPLEFT", 14, -50 - #STATS * (ROW_H + 2) - 22)
stop:SetScript("OnClick", function() Send("stop") end)
local reset = Button(T, "Reset", 80); reset:SetPoint("LEFT", stop, "RIGHT", 6, 0)
reset:SetScript("OnClick", function() Send("reset") end)
local hint = T:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
hint:SetPoint("LEFT", reset, "RIGHT", 8, 0); hint:SetWidth(100); hint:SetJustifyH("LEFT")
hint:SetText("Attack or heal to start.")

-- the owner's settings, a second page
local setup = CreateFrame("Frame", nil, T)
setup:SetAllPoints()
setup:Hide()
local CONFIG = { { "level", "Level", true }, { "armor", "Armor (0: normal)", true }, { "resist", "Resistances", true },
  { "health", "Health", true }, { "killable", "Can be defeated" }, { "duration", "Session seconds (0: until stopped)", true },
  { "interrupts", "Boss: interruptible casts" }, { "puddles", "Boss: puddles" }, { "enrageOn", "Boss: enrage timer" },
  { "enrage", "Enrage after (seconds)", true } }
local fields = {}
for i, c in ipairs(CONFIG) do
  local line = setup.Line and setup:Line(i, -50, c[2]) or T.Line(setup, i, -50, c[2])
  fields[c[1]] = c[3] and Box(line, 80, true) or Check(line)
  fields[c[1]].line = line
end
local save = Button(setup, "Save", 80); save:SetPoint("BOTTOMRIGHT", -14, 14)
local back = Button(setup, "Back", 80); back:SetPoint("RIGHT", save, "LEFT", -6, 0)
local settings = Button(T, "Settings", 80); settings:SetPoint("BOTTOMRIGHT", -14, 14)

local function ShowPage(config)
  if config then setup:Show() else setup:Hide() end
  for _, l in pairs(statLines) do if config then l:Hide() else l:Show() end end
  for _, w in ipairs({ result, stop, reset, hint, settings }) do if config then w:Hide() else w:Show() end end
  if not config and not (dummy and dummy.owner) then settings:Hide() end
end
settings:SetScript("OnClick", function() ShowPage(true) end)
back:SetScript("OnClick", function() ShowPage(false) end)
save:SetScript("OnClick", function()
  if not dummy then return end
  local drills = (fields.interrupts:GetChecked() and 1 or 0) + (fields.puddles:GetChecked() and 2 or 0) + (fields.enrageOn:GetChecked() and 4 or 0)
  Send("dummyset", dummy.placed, fields.level:GetText(), fields.armor:GetText(), fields.resist:GetText(), fields.health:GetText(),
    fields.killable:GetChecked() and 1 or 0, fields.duration:GetText(), drills, fields.enrage:GetText())
  ShowPage(false)
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
  dummy = d
  local s, kind = d.session, d.kind
  T.title:SetText(d.name or KINDS[kind] or "Training Dummy")
  T.sub:SetText(s.active and "|cff40ff40Session running|r" or "Training Hall")
  local secs = s.duration > 0 and string.format("%d / %d s", s.elapsed, s.duration) or string.format("%d s", s.elapsed)
  local values = { elapsed = secs, damage = Short(s.damage), dps = Short(s.dps), healing = Short(s.healing), hps = Short(s.hps),
    overheal = Short(s.overheal), taken = Short(s.taken), kicks = string.format("%d of %d", s.kicks, s.kicks + s.missed),
    puddles = s.puddles, drops = s.drops, targets = s.targets,
    best = Short(d.best) .. (kind == 3 and " HPS" or " DPS") }
  -- each kind shows what it trains
  local shown = { elapsed = true, targets = true, best = true, damage = kind ~= 3, dps = kind ~= 3, healing = kind == 3, hps = kind == 3,
    overheal = kind == 3, drops = kind == 3, taken = kind == 2 or kind == 4, kicks = kind == 4, puddles = kind == 4 }
  local row = 0
  for _, st in ipairs(STATS) do
    local l = statLines[st[1]]
    if shown[st[1]] then
      row = row + 1
      l:ClearAllPoints(); l:SetPoint("TOPLEFT", 14, -50 - (row - 1) * (ROW_H + 2))
      l.value:SetText(values[st[1]]); l.show = true
    else l.show = false; l:Hide() end
  end
  result:ClearAllPoints(); result:SetPoint("TOPLEFT", 18, -50 - row * (ROW_H + 2) - 4)
  result:SetText(s.result ~= "" and s.result or (s.active and "" or "Attack the dummy to start a session."))
  stop:ClearAllPoints(); stop:SetPoint("TOPLEFT", 14, -50 - row * (ROW_H + 2) - 24)
  if not setup:IsShown() then
    for _, st in ipairs(STATS) do if statLines[st[1]].show then statLines[st[1]]:Show() end end
    if d.owner then settings:Show() else settings:Hide() end
  end
  local c = d.config
  for _, k in ipairs({ "level", "armor", "resist", "health", "duration", "enrage" }) do
    if not fields[k]:HasFocus() then fields[k]:SetText(c[k]) end
  end
  fields.killable:SetChecked(c.killable)
  fields.interrupts:SetChecked(c.drills % 2 == 1)
  fields.puddles:SetChecked(math.floor(c.drills / 2) % 2 == 1)
  fields.enrageOn:SetChecked(math.floor(c.drills / 4) % 2 == 1)
  for _, k in ipairs({ "interrupts", "puddles", "enrageOn", "enrage" }) do
    if kind == 4 then fields[k].line:Show() else fields[k].line:Hide() end
  end
  if not T:IsShown() then ShowPage(false); T:Show() end
end

-- targeting one of the dummies opens its panel; the server keeps it live while a session runs
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
T:SetScript("OnHide", function() Send("close"); ShowPage(false) end)

-- ------------------------------------------------------------------------------------------------ portals

local P = Window("EsteriaHousingPortal", 300, 230, "Portal")
P:SetPoint("LEFT", 60, 40)
local portal, destinations = nil, {}
local pDest = P:Line(1, -50, "Leads to")
local travel = Button(P, "Travel", 100); travel:SetPoint("TOP", 0, -86)
travel:SetScript("OnClick", function()
  if not portal then return end
  local dialog = StaticPopup_Show("ESTERIA_HOUSING_PORTAL_GO", portal.destName)
  if dialog then dialog.data = portal.placed end
end)
StaticPopupDialogs["ESTERIA_HOUSING_PORTAL_GO"] = {
  text = "Step through the portal to %s?", button1 = YES, button2 = NO, timeout = 0, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, placed) Send("portalgo", placed); P:Hide() end,
}
local pName = P:Line(2, -118, "Name")
local nameBox = Box(pName, 150)
local pTo = P:Line(3, -118, "Destination")
local pLook = P:Line(4, -118, "Look")
local pAccess = P:Line(5, -118, "Who may use it")
local LOOKS = { "K'areshi Portal", "Portal to Damnation", "Translocation Portal", "Mage Portal" }
local ACCESS = { { 16, "Party members" }, { 2, "Friends" }, { 4, "Guild members" }, { 1, "Everyone" } }
local choice = {}
local function Small(line, text, fn)
  local b = Button(line, text, 76); b:SetPoint("RIGHT", -4, 0); b:SetScript("OnClick", fn)
  return b
end
Small(pTo, "Choose", function(self)
  local items = { { text = "Destination", isTitle = true, notCheckable = true } }
  local cities, homes = {}, {}
  for _, d in ipairs(destinations) do
    local item = { text = d.name, checked = choice.kind == d.kind and choice.dest == d.id and choice.hood == d.hood,
      func = function() choice.kind, choice.dest, choice.hood, choice.name = d.kind, d.id, d.hood, d.name; H.RedrawPortal() end }
    table.insert(d.kind == 1 and homes or cities, item)
  end
  items[#items + 1] = { text = "Cities", notCheckable = true, hasArrow = true, menuList = cities }
  if #homes > 0 then items[#items + 1] = { text = "My Homes", notCheckable = true, hasArrow = true, menuList = homes } end
  EasyMenu(items, menu, self, 0, 0, "MENU")
end)
Small(pLook, "Choose", function(self)
  local items = { { text = "Look", isTitle = true, notCheckable = true } }
  for i, name in ipairs(LOOKS) do
    items[#items + 1] = { text = name, checked = choice.look == i - 1, func = function() choice.look = i - 1; H.RedrawPortal() end }
  end
  EasyMenu(items, menu, self, 0, 0, "MENU")
end)
Small(pAccess, "Choose", function(self)
  local items = { { text = "Who may use it", isTitle = true, notCheckable = true } }
  for _, a in ipairs(ACCESS) do
    items[#items + 1] = { text = a[2], checked = bit.band(choice.access or 0, a[1]) ~= 0, keepShownOnClick = true, func = function()
      choice.access = bit.bxor(choice.access or 0, a[1]); H.RedrawPortal() end }
  end
  EasyMenu(items, menu, self, 0, 0, "MENU")
end)
local pSave = Button(P, "Save", 80); pSave:SetPoint("BOTTOMRIGHT", -14, 14)
pSave:SetScript("OnClick", function()
  if portal then
    Send("portalset", portal.placed, choice.kind or 0, choice.dest or 0, choice.hood or 0, choice.look or 0, choice.access or 0,
      (nameBox:GetText():gsub("[\t\n]", " ")))
  end
end)

function H.RedrawPortal()
  if not portal then return end
  pTo.label:SetText("Destination: |cffffffff" .. (choice.name ~= "" and choice.name or "none") .. "|r")
  pLook.label:SetText("Look: |cffffffff" .. (LOOKS[(choice.look or 0) + 1] or "") .. "|r")
  local who = { "you" }
  for _, a in ipairs(ACCESS) do if bit.band(choice.access or 0, a[1]) ~= 0 then who[#who + 1] = a[2]:lower() end end
  pAccess.label:SetText("Who: |cffffffff" .. table.concat(who, ", ") .. "|r")
end

local function ReadPortal(lines)
  destinations = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "portal" then
      portal = { placed = f[2], owner = f[3] == "1", name = f[4], destName = f[5] ~= "" and f[5] or nil }
      choice = { kind = Num(f[6]), dest = Num(f[7]), hood = Num(f[8]), look = Num(f[9]), access = Num(f[10]), name = f[5] }
    elseif f[1] == "dest" then
      destinations[#destinations + 1] = { kind = Num(f[2]), id = Num(f[3]), hood = Num(f[4]), name = f[5] }
    end
  end
  P.title:SetText(portal.name ~= "" and portal.name or "Portal")
  P.sub:SetText(portal.owner and "Your portal" or "")
  pDest.value:SetText(portal.destName or "|cff808080Nowhere yet|r")
  if portal.destName then travel:Enable() else travel:Disable() end
  for _, w in ipairs({ pName, pTo, pLook, pAccess, pSave }) do if portal.owner then w:Show() else w:Hide() end end
  if not nameBox:HasFocus() then nameBox:SetText(portal.name) end
  P:SetHeight(portal.owner and 290 or 130)
  H.RedrawPortal()
  P:Show()
end

-- ------------------------------------------------------------------------------------------------ meeting stones

local M = Window("EsteriaHousingStone", 300, 200, "Meeting Stone")
M:SetPoint("LEFT", 60, 40)
local stone, stoneOptions = nil, {}
local EXPANSIONS = { [0] = "Classic", [1] = "Burning Crusade", [2] = "Wrath of the Lich King" }
local sDungeon = M:Line(1, -50, "Dungeon")
local sLevel = M:Line(2, -50, "Requires")
local sNote = M:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
sNote:SetPoint("TOPLEFT", 18, -102); sNote:SetWidth(264); sNote:SetJustifyH("LEFT")
sNote:SetText("Takes you to the dungeon's entrance, not inside. Each party member goes only if they qualify.")
local go = Button(M, "Travel", 90); go:SetPoint("TOPLEFT", 14, -134)
go:SetScript("OnClick", function() if stone then Send("stonego", stone.placed); M:Hide() end end)
local party = Button(M, "Take Party", 100); party:SetPoint("LEFT", go, "RIGHT", 6, 0)
party:SetScript("OnClick", function() if stone then Send("stoneparty", stone.placed); M:Hide() end end)
local bind = Button(M, "Bind", 80); bind:SetPoint("LEFT", party, "RIGHT", 6, 0)
bind:SetScript("OnClick", function(self)
  local items = { { text = "Bind to a dungeon", isTitle = true, notCheckable = true } }
  for x = 0, 2 do
    local sub = {}
    for _, o in ipairs(stoneOptions) do
      if o.expansion == x then
        sub[#sub + 1] = { text = string.format("%s (%d+)", o.name, o.level), checked = stone and stone.guid == o.guid,
          func = function() Send("stoneset", stone.placed, o.guid); CloseDropDownMenus() end }
      end
    end
    table.sort(sub, function(a, b) return a.text < b.text end)
    if #sub > 0 then items[#items + 1] = { text = EXPANSIONS[x], notCheckable = true, hasArrow = true, menuList = sub } end
  end
  EasyMenu(items, menu, self, 0, 0, "MENU")
end)

local function ReadStone(lines)
  stoneOptions = {}
  for _, line in ipairs(lines) do
    local f = Fields(line)
    if f[1] == "stone" then
      stone = { placed = f[2], owner = f[3] == "1", name = f[4], level = Num(f[5]), expansion = Num(f[6]), guid = f[7],
        grouped = f[8] == "1" }
    elseif f[1] == "stoneopt" then
      stoneOptions[#stoneOptions + 1] = { guid = f[2], name = f[3], level = Num(f[4]), expansion = Num(f[5]) }
    end
  end
  local bound = stone.name ~= ""
  sDungeon.value:SetText(bound and stone.name or "|cff808080Not bound yet|r")
  sLevel.value:SetText(bound and string.format("Level %d (%s)", stone.level, EXPANSIONS[stone.expansion] or "") or "")
  if bound then go:Enable() else go:Disable() end
  if bound and stone.grouped then party:Enable() else party:Disable() end
  if stone.owner then bind:Show() else bind:Hide() end
  M:Show()
end

StaticPopupDialogs["ESTERIA_HOUSING_STONE_INVITE"] = {
  text = "%s", button1 = ACCEPT, button2 = DECLINE, timeout = 60, whileDead = false, hideOnEscape = true,
  OnAccept = function(self, placed) Send("stonego", placed) end,
}

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
