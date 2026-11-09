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

local S = K.Window("EsteriaHousingService", 380, 470, "Interface\\Icons\\INV_Misc_Rune_01")
S:SetPoint("LEFT", 60, 40)
local svc
local sbox = K.Inset(S, BOX_TOP, 16)
local spages = K.Tabs(S, sbox, { "Service", "Settings" }, function(i) S:PickTab(i, { true, svc and svc.owner }) end)

-- Service: what it is, its costs (repairs), and its actions as buttons
local about = spages[1]:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
about:SetPoint("TOPLEFT", 14, -14); about:SetPoint("RIGHT", -14, 0); about:SetJustifyH("LEFT"); about:SetSpacing(3)
local costLines = {}
for i = 1, 3 do costLines[i] = K.Line(spages[1], i, -110, "") end
local actionButtons = {}
for i = 1, 3 do
  local b = K.Button(spages[1], "", 240); b:SetHeight(26)
  b:SetPoint("BOTTOM", 0, 14 + (3 - i) * 32)
  actionButtons[i] = b
end

StaticPopupDialogs["ESTERIA_HOUSING_SERVICE_CONFIRM"] = {
  text = "%s", button1 = ACCEPT, button2 = CANCEL, timeout = 0, whileDead = false, hideOnEscape = true, hasMoneyFrame = 1,
  OnAccept = function(self) if self.data then Send(self.data.verb, self.data.placed) end end,
}

-- Settings (the owner's): name and title (characters), who may use it, guests (innkeeper), greeting (auctioneer), look
local nameLine = K.Line(spages[2], 1, -10, "Name"); local nameBox = K.Box(nameLine, 180); nameBox:SetPoint("RIGHT", -4, 0)
local titleLine = K.Line(spages[2], 2, -10, "Title"); local titleBox = K.Box(titleLine, 180); titleBox:SetPoint("RIGHT", -4, 0)
local accessLine = K.Line(spages[2], 3, -10, "Who may use it"); local accessButton = Cycler(accessLine)
local guestLine = K.Line(spages[2], 4, -10, "Guests may bind their hearthstone here")
local guestCheck = K.Check(guestLine); guestCheck:SetPoint("RIGHT", -4, 0)
local greetLine = K.Line(spages[2], 5, -10, "Greeting"); local greetBox = K.Box(greetLine, 220); greetBox:SetPoint("RIGHT", -4, 0)
local look = K.Button(spages[2], "Change Appearance", 150); look:SetPoint("BOTTOMLEFT", 8, 8)
look:SetScript("OnClick", function() if svc then Send("mqappearance", svc.placed) end end)
local ssave = K.Button(spages[2], "Save", 90); ssave:SetPoint("BOTTOMRIGHT", -8, 8)
ssave:SetScript("OnClick", function()
  if not svc then return end
  nameBox:ClearFocus(); titleBox:ClearFocus(); greetBox:ClearFocus()
  Send("svset", svc.placed, Clean(nameBox:GetText()), Clean(titleBox:GetText()), accessButton.value or 0,
    guestCheck:GetChecked() and 1 or 0, Clean(greetBox:GetText()))
end)

local function ReadService(lines)
  local f = Fields(lines[1])
  svc = { placed = f[2], kind = Num(f[3]), owner = f[4] == "1", name = f[5], title = f[6], access = Num(f[7]), flags = Num(f[8]),
    greeting = f[9], figure = f[10] == "1", message = f[12] or "" }
  local info, costs, actions = {}, {}, {}
  for i = 2, #lines do
    local r = Fields(lines[i])
    if r[1] == "I" then info[#info + 1] = r[2]
    elseif r[1] == "C" then costs[#costs + 1] = { r[2], Num(r[3]) }
    elseif r[1] == "A" then actions[#actions + 1] = { verb = r[2], label = r[3], confirm = r[4], money = Num(r[5]) } end
  end
  SetPortraitToTexture(S.portrait, ICONS[svc.kind] or "Interface\\Icons\\INV_Misc_QuestionMark")
  S.title:SetText(svc.name)
  S.status:SetText(svc.title ~= "" and svc.title or (KIND_NAME[svc.kind] or ""))
  S.note:SetText(svc.message ~= "" and svc.message or (svc.owner and "Yours. Settings are on the second tab." or ""))
  about:SetText(table.concat(info, "\n\n"))
  for i, line in ipairs(costLines) do
    local c = costs[i]
    Show(line, c ~= nil)
    if c then line.label:SetText(c[1]); line.value:SetText(Coins(c[2])) end
  end
  for i, b in ipairs(actionButtons) do
    local a = actions[i]
    Show(b, a ~= nil)
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
          if a.verb == "use" then S:Hide() end -- the game's own window takes over
        end
      end)
    end
  end
  -- settings
  Show(nameLine, svc.figure); Show(titleLine, svc.figure); Show(look, svc.figure)
  Show(guestLine, svc.kind == INNKEEPER); Show(greetLine, svc.kind == AUCTIONEER)
  if not nameBox:HasFocus() then nameBox:SetText(svc.figure and svc.name or "") end
  if not titleBox:HasFocus() then titleBox:SetText(svc.title) end
  if not greetBox:HasFocus() then greetBox:SetText(svc.greeting) end
  accessButton:Set(svc.access)
  guestCheck:SetChecked(svc.flags % 2 == 1)
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
rules:SetText("You can always use your own chest. Soulbound things go back only to the character who stored them, "
  .. "account-bound things only to that account. Conjured, timed and refundable items stay out. Pick up a chest and its "
  .. "contents go with it: they come back when you place a chest of the same kind again.")
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
      held[#held + 1] = { text = Quality(Counted(r[7] or "?", r[4]), r[8]), link = r[5], dim = bound ~= nil or not chest.canTake,
        sub = bound or (chest.canTake and "take" or ""), fn = chest.canTake and not bound and function() Send("sttake", chest.placed, guid) end or nil }
    elseif r[1] == "D" then
      -- D, item guid, entry, count, link, name, quality
      local guid = r[2]
      add[#add + 1] = { text = Quality(Counted(r[6] or "?", r[4]), r[7]), sort = r[6] or "", link = r[5], sub = "put in",
        fn = function() Send("stput", chest.placed, guid) end }
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
