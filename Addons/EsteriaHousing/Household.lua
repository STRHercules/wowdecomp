-- Esteria housing: household services (the spec's section 5). Two windows in the look of the other housing windows
-- (Adventure.lua's H.Kit): a service's (innkeeper, bank, mailbox, repairs, auctioneer, trainers, crafting stations), and
-- a storage chest's. Commands start with "col\t" (EsteriaHousing_Commerce); the answers are "service" and "storage"
-- records (mod-esteria-housing HousingHousehold.inc). The bank, mail, auction house and trainer frames are the game's own.

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
local function Coins(copper) return GetCoinTextureString and GetCoinTextureString(Num(copper)) or tostring(copper) end

-- who may: 0 the owner only, else the house access bits (1 anyone who can come in, 2 friends, 4 guild, 16 group)
local ACCESS = { { 0, "Only me" }, { 1, "Anyone who can come in" }, { 2, "My friends" }, { 4, "My guild" }, { 16, "My group" } }
local function AccessName(v) for _, a in ipairs(ACCESS) do if a[1] == v then return a[2] end end return "Only me" end
local function NextAccess(v) for i, a in ipairs(ACCESS) do if a[1] == v then return ACCESS[i % #ACCESS + 1][1] end end return 0 end
local function Cycler(line)
  local b = K.Button(line, "", 180); b:SetPoint("RIGHT", -4, 0)
  b:SetScript("OnClick", function(self) self.value = NextAccess(self.value or 0); self:SetText(AccessName(self.value)) end)
  function b:Set(v) self.value = v; self:SetText(AccessName(v)) end
  return b
end

-- ------------------------------------------------------------------------------------------------ services

-- kinds (HousingCollections.inc CollectionKind): 17 innkeeper .. 27 trainer
local ICONS = { [17] = "Interface\\Icons\\INV_Misc_Rune_01", [18] = "Interface\\Icons\\INV_Misc_Bag_10_Blue",
  [19] = "Interface\\Icons\\INV_Misc_Bag_10_Blue", [20] = "Interface\\Icons\\INV_Misc_Bag_10_Green",
  [21] = "Interface\\Icons\\INV_Letter_15", [22] = "Interface\\Icons\\Trade_BlackSmithing", [23] = "Interface\\Icons\\Trade_BlackSmithing",
  [24] = "Interface\\Icons\\INV_Misc_Coin_02", [25] = "Interface\\Icons\\INV_Box_02", [26] = "Interface\\Icons\\Trade_Engineering",
  [27] = "Interface\\Icons\\INV_Misc_Book_11" }
local KIND_NAME = { [17] = "Innkeeper", [18] = "Banker", [19] = "Bank", [20] = "Guild Vault", [21] = "Mailbox", [22] = "Repairs",
  [23] = "Repairs", [24] = "Auctioneer", [25] = "Storage", [26] = "Crafting Station", [27] = "Trainer" }
local INNKEEPER, AUCTIONEER = 17, 24

StaticPopupDialogs["ESTERIA_HOUSING_SERVICE_CONFIRM"] = {
  text = "%s", button1 = ACCEPT, button2 = CANCEL, timeout = 0, whileDead = false, hideOnEscape = true, hasMoneyFrame = 1,
  OnAccept = function(self) if self.data then Send(self.data.verb, self.data.placed) end end,
}

-- What a service is, its costs (repairs) and its actions as buttons, on any page. onUse: the game's own window took over.
local function ServiceBody(page, onUse)
  local about = page:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
  about:SetPoint("TOPLEFT", 14, -14); about:SetPoint("RIGHT", -14, 0); about:SetJustifyH("LEFT"); about:SetSpacing(3)
  local costLines, buttons = {}, {}
  for i = 1, 3 do costLines[i] = K.Line(page, i, -104, "") end
  for i = 1, 3 do
    local b = K.Button(page, "", 240); b:SetHeight(26)
    b:SetPoint("TOP", 0, -104 - 3 * (LINE_H + 2) - 10 - (i - 1) * 32)
    buttons[i] = b
  end
  return function(svc, info, costs, actions)
    about:SetText(table.concat(info, "\n\n"))
    for i, line in ipairs(costLines) do
      local c = costs[i]
      Show(line, c ~= nil)
      if c then line.label:SetText(c[1]); line.value:SetText(Coins(c[2])) end
    end
    -- with no costs, the buttons move up under the text
    local top = #costs > 0 and (-104 - 3 * (LINE_H + 2) - 10) or -104
    for i, b in ipairs(buttons) do
      local a = actions[i]
      Show(b, a ~= nil)
      b:ClearAllPoints(); b:SetPoint("TOP", 0, top - (i - 1) * 32)
      if a then
        b:SetText(a.money > 0 and (a.label .. "  " .. Coins(a.money)) or a.label)
        b:SetScript("OnClick", function()
          if a.confirm ~= "" then
            local d = StaticPopup_Show("ESTERIA_HOUSING_SERVICE_CONFIRM", a.confirm)
            if d then
              d.data = { verb = "sv" .. a.verb, placed = svc.placed }
              if a.money > 0 then MoneyFrame_Update(d.moneyFrame, a.money) else d.moneyFrame:Hide() end
            end
          else
            Send("sv" .. a.verb, svc.placed)
            if a.verb == "use" and onUse then onUse() end
          end
        end)
      end
    end
  end
end

-- The owner's settings, rows from `top` down: name and title (withNames), who may use it, guests (innkeeper), greeting
-- (auctioneer), and Save. A character's name and title live on its own Settings tab, so it passes them through unchanged.
local function ServiceSettings(page, top, withNames)
  local row, rows = 0, {}
  local function Row(label)
    row = row + 1
    local line = K.Line(page, row, top, label)
    rows[#rows + 1] = line
    return line
  end
  local nameLine, nameBox, titleLine, titleBox
  if withNames then
    nameLine = Row("Name"); nameBox = K.Box(nameLine, 180); nameBox:SetPoint("RIGHT", -4, 0)
    titleLine = Row("Title"); titleBox = K.Box(titleLine, 180); titleBox:SetPoint("RIGHT", -4, 0)
  end
  local accessLine = Row("Who may use it"); local accessButton = Cycler(accessLine)
  local guestLine = Row("Guests may bind their hearthstone here")
  local guestCheck = K.Check(guestLine); guestCheck:SetPoint("RIGHT", -4, 0)
  local greetLine = Row("Greeting"); local greetBox = K.Box(greetLine, 220); greetBox:SetPoint("RIGHT", -4, 0)
  local save = K.Button(page, "Save", 90); save:SetPoint("TOPRIGHT", -8, top - row * (LINE_H + 2) - 8)
  local svc
  save:SetScript("OnClick", function()
    if not svc then return end
    greetBox:ClearFocus()
    local name, title = svc.figure and svc.name or "", svc.title
    if withNames then nameBox:ClearFocus(); titleBox:ClearFocus(); name, title = Clean(nameBox:GetText()), Clean(titleBox:GetText()) end
    Send("svset", svc.placed, name, title, accessButton.value or 0, guestCheck:GetChecked() and 1 or 0, Clean(greetBox:GetText()))
  end)
  return function(s)
    svc = s
    if withNames then
      Show(nameLine, s.figure); Show(titleLine, s.figure)
      if not nameBox:HasFocus() then nameBox:SetText(s.figure and s.name or "") end
      if not titleBox:HasFocus() then titleBox:SetText(s.title) end
    end
    Show(guestLine, s.kind == INNKEEPER); Show(greetLine, s.kind == AUCTIONEER)
    if not greetBox:HasFocus() then greetBox:SetText(s.greeting) end
    accessButton:Set(s.access)
    guestCheck:SetChecked(s.flags % 2 == 1)
  end, save
end

-- The service window: what a visitor sees, and the owner's for the services that are no character (a vault, a mailbox,
-- a repair bench, a station). A character the owner dresses opens the character window instead (Collections.lua).
local S = K.Window("EsteriaHousingService", 380, 470, "Interface\\Icons\\INV_Misc_Rune_01")
S:SetPoint("LEFT", 60, 40)
local current
local sbox = K.Inset(S, BOX_TOP, 16)
local spages = K.Tabs(S, sbox, { "Service", "Settings" }, function(i) S:PickTab(i, { true, current and current.owner }) end)
local fillBody = ServiceBody(spages[1], function() S:Hide() end)
local fillSettings = ServiceSettings(spages[2], -10, true)

-- The character window's Service tab (its page is Collections.lua's): the same parts, the settings under the actions.
local fillFigureBody, fillFigureSettings
if H.FigureServicePage then
  fillFigureBody = ServiceBody(H.FigureServicePage, function() if H.HideFigure then H.HideFigure() end end)
  fillFigureSettings = ServiceSettings(H.FigureServicePage, -104 - 6 * (LINE_H + 2) - 30, false)
end

local function ReadService(lines)
  local f = Fields(lines[1])
  local svc = { placed = f[2], kind = Num(f[3]), owner = f[4] == "1", name = f[5], title = f[6], access = Num(f[7]), flags = Num(f[8]),
    greeting = f[9], figure = f[10] == "1", message = f[12] or "" }
  local info, costs, actions = {}, {}, {}
  for i = 2, #lines do
    local r = Fields(lines[i])
    if r[1] == "I" then info[#info + 1] = r[2]
    elseif r[1] == "C" then costs[#costs + 1] = { r[2], Num(r[3]) }
    elseif r[1] == "A" then actions[#actions + 1] = { verb = r[2], label = r[3], confirm = r[4], money = Num(r[5]) } end
  end
  -- the owner's character: one window with its service, outfit and settings
  if svc.figure and svc.owner and fillFigureBody and H.ShowFigure then
    fillFigureBody(svc, info, costs, actions)
    fillFigureSettings(svc)
    S:Hide()
    H.ShowFigure(svc, ICONS[svc.kind])
    return
  end
  current = svc
  SetPortraitToTexture(S.portrait, ICONS[svc.kind] or "Interface\\Icons\\INV_Misc_QuestionMark")
  S.title:SetText(svc.name)
  S.status:SetText(svc.title ~= "" and svc.title or (KIND_NAME[svc.kind] or ""))
  S.note:SetText(svc.message ~= "" and svc.message or (svc.owner and "Yours. Settings are on the second tab." or ""))
  fillBody(svc, info, costs, actions)
  fillSettings(svc)
  if not S:IsShown() then S:PickTab(1, { true, svc.owner }) else Show(_G["EsteriaHousingServiceTab2"], svc.owner) end
  S:Show()
end

-- ------------------------------------------------------------------------------------------------ storage chests

local W = K.Window("EsteriaHousingStorage", 380, 470, "Interface\\Icons\\INV_Box_02")
W:SetPoint("LEFT", 60, 40)
local chest
local box = K.Inset(W, BOX_TOP, 16)
local pages = K.Tabs(W, box, { "Contents", "Put In", "Log", "Settings" }, function(i) W:PickTab(i, chest and chest.shown) end)
local contents = K.List(pages[1], -10, 11, 24)
local puts = K.List(pages[2], -10, 11, 24)
local log = K.List(pages[3], -10, 11, 24)

local chestNameLine = K.Line(pages[4], 1, -10, "Name")
local chestNameBox = K.Box(chestNameLine, 180); chestNameBox:SetPoint("RIGHT", -4, 0)
local takeButton = Cycler(K.Line(pages[4], 2, -10, "Who may take things out"))
local putButton = Cycler(K.Line(pages[4], 3, -10, "Who may put things in"))
local rules = pages[4]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
rules:SetPoint("TOPLEFT", 12, -10 - 3 * (LINE_H + 2) - 12); rules:SetWidth(330); rules:SetJustifyH("LEFT")
rules:SetText("You can always use your own chest. You can store soulbound and account-bound things too; only you can take "
  .. "them out again. Conjured, timed and refundable items stay out. Pick up a chest and its contents go with it: they come "
  .. "back when you place a chest of the same kind again.")
local save = K.Button(pages[4], "Save", 90); save:SetPoint("BOTTOMRIGHT", -8, 8)
save:SetScript("OnClick", function()
  if not chest then return end
  chestNameBox:ClearFocus()
  Send("stset", chest.placed, Clean(chestNameBox:GetText()), takeButton.value or 0, putButton.value or 0)
end)

local function When(t)
  t = Num(t)
  return t > 0 and date("%b %d %H:%M", t) or ""
end
local function Counted(name, count) return Num(count) > 1 and (name .. " x" .. count) or name end

local function ReadStorage(lines)
  local f = Fields(lines[1])
  chest = { placed = f[2], name = f[3], used = Num(f[4]), slots = Num(f[5]), canPut = f[6] == "1", canTake = f[7] == "1",
    owner = f[8] == "1", take = Num(f[9]), put = Num(f[10]), message = f[11] or "" }
  chest.shown = { true, chest.canPut, true, chest.owner }
  local held, add, logged = { { header = true, text = string.format("%d of %d slots used", chest.used, chest.slots) } }, {}, {}
  for i = 2, #lines do
    local r = Fields(lines[i])
    if r[1] == "S" then
      -- S, item guid, entry, count, link, bound to, name, quality
      local guid, bound = r[2], r[6] ~= "" and r[6] or nil
      held[#held + 1] = { text = Quality(Counted(r[7] or "?", r[4]), r[8]), link = r[5], icon = GetItemIcon(Num(r[3])),
        dim = bound ~= nil or not chest.canTake,
        sub = bound or (chest.canTake and "take" or ""), fn = chest.canTake and not bound and function() Send("sttake", chest.placed, guid) end or nil }
    elseif r[1] == "D" then
      -- D, item guid, entry, count, link, name, quality, where (0 bags, 1 Esteria Inventory)
      local guid, from = r[2], r[8] or "0"
      add[#add + 1] = { text = Quality(Counted(r[6] or "?", r[4]), r[7]), sort = r[6] or "", link = r[5], icon = GetItemIcon(Num(r[3])),
        sub = from == "1" and "inventory" or "bags", fn = function() Send("stput", chest.placed, from, guid) end }
    elseif r[1] == "L" then
      -- L, time, character, action, entry, count, item name
      logged[#logged + 1] = { text = string.format("%s %s %s", r[3], r[4], Counted(r[7] ~= "" and r[7] or ("item " .. r[5]), r[6])),
        sub = When(r[2]) }
    end
  end
  table.sort(add, function(a, b) return a.sort < b.sort end)
  if #add == 0 then add[1] = { header = true, text = "Nothing in your bags can go in." } end
  if #logged == 0 then logged[1] = { header = true, text = "Nobody has used this chest yet." } end
  contents.items, puts.items, log.items = held, add, logged
  contents:Update(); puts:Update(); log:Update()
  W.title:SetText(chest.name)
  W.status:SetText(string.format("%d of %d slots used", chest.used, chest.slots))
  W.note:SetText(chest.message ~= "" and chest.message or (chest.owner and "Your chest." or
    (chest.canTake and chest.canPut and "You may take and put in." or chest.canTake and "You may take things out." or "You may put things in.")))
  if not chestNameBox:HasFocus() then chestNameBox:SetText(chest.name) end
  takeButton:Set(chest.take); putButton:Set(chest.put)
  if not W:IsShown() then W:PickTab(1, chest.shown) else
    for i = 1, 4 do Show(_G["EsteriaHousingStorageTab" .. i], chest.shown[i]) end
  end
  W:Show()
end

local listener = CreateFrame("Frame")
listener:RegisterEvent("ESTERIA_HOUSING_COMMERCE")
listener:SetScript("OnEvent", function()
  local lines = {}
  for line in EsteriaHousing_GetCommerce():gmatch("[^\n]+") do lines[#lines + 1] = line end
  local kind = lines[1] and lines[1]:match("^[^\t]+")
  if kind == "storage" then ReadStorage(lines) elseif kind == "service" then ReadService(lines) end
end)
