-- Esteria housing: household services (the spec's section 5). The innkeeper, bank, mailbox, repairs, auctioneer, trainers
-- and crafting stations all answer with ordinary gossip and the game's own windows; this file is the one window of
-- their own, a storage chest's: what it holds, what you can put in, its log and the owner's settings. Commands start with
-- "col\t" (EsteriaHousing_Commerce); the answer is a "storage" record (mod-esteria-housing HousingHousehold.inc).

if not EsteriaHousing_IsAvailable or not EsteriaHousingUI or not EsteriaHousingUI.Kit then return end
local H = EsteriaHousingUI
local K = H.Kit
local Fields, Num, Show, BOX_TOP = K.Fields, K.Num, K.Show, K.BOX_TOP

local function Send(verb, placed, ...) EsteriaHousing_Commerce(strjoin("\t", "col", verb, placed or "0", ...)) end
local function Clean(text) return ((text or ""):gsub("[\t\n|]", " ")) end
local function ItemName(link) return link and link:match("%[(.-)%]") or "?" end

-- who may: 0 the owner only, else the house access bits (1 anyone who can come in, 2 friends, 4 guild, 16 group)
local ACCESS = { { 0, "Only me" }, { 1, "Anyone who can come in" }, { 2, "My friends" }, { 4, "My guild" }, { 16, "My group" } }
local function AccessName(v) for _, a in ipairs(ACCESS) do if a[1] == v then return a[2] end end return "Only me" end
local function NextAccess(v) for i, a in ipairs(ACCESS) do if a[1] == v then return ACCESS[i % #ACCESS + 1][1] end end return 0 end

local W = K.Window("EsteriaHousingStorage", 380, 470, "Interface\\Icons\\INV_Box_02")
W:SetPoint("LEFT", 60, 40)
local chest
local box = K.Inset(W, BOX_TOP, 16)
local pages = K.Tabs(W, box, { "Contents", "Put In", "Log", "Settings" }, function(i) W:PickTab(i, chest and chest.shown) end)
local contents = K.List(pages[1], -10, 11, 24)
local puts = K.List(pages[2], -10, 11, 24)
local log = K.List(pages[3], -10, 11, 24)

local nameLine = K.Line(pages[4], 1, -10, "Name")
local nameBox = K.Box(nameLine, 180); nameBox:SetPoint("RIGHT", -4, 0)
local takeLine = K.Line(pages[4], 2, -10, "Who may take things out")
local putLine = K.Line(pages[4], 3, -10, "Who may put things in")
local function Cycler(line)
  local b = K.Button(line, "", 170); b:SetPoint("RIGHT", -4, 0)
  b:SetScript("OnClick", function(self) self.value = NextAccess(self.value or 0); self:SetText(AccessName(self.value)) end)
  return b
end
local takeButton, putButton = Cycler(takeLine), Cycler(putLine)
local rules = pages[4]:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
rules:SetPoint("TOPLEFT", 12, -120); rules:SetWidth(330); rules:SetJustifyH("LEFT")
rules:SetText("You can always use your own chest. Soulbound things go back only to the character who stored them, "
  .. "account-bound things only to that account. Conjured, timed and refundable items stay out. Pick up a chest and its "
  .. "contents go with it: they come back when you place a chest of the same kind again.")
local save = K.Button(pages[4], "Save", 90); save:SetPoint("BOTTOMRIGHT", -8, 8)
save:SetScript("OnClick", function()
  if not chest then return end
  nameBox:ClearFocus()
  Send("stset", chest.placed, Clean(nameBox:GetText()), takeButton.value or 0, putButton.value or 0)
end)

local function When(t)
  t = Num(t)
  return t > 0 and date("%b %d %H:%M", t) or ""
end

local function Read(lines)
  local f = Fields(lines[1])
  chest = { placed = f[2], name = f[3], used = Num(f[4]), slots = Num(f[5]), canPut = f[6] == "1", canTake = f[7] == "1",
    owner = f[8] == "1", take = Num(f[9]), put = Num(f[10]), message = f[11] or "" }
  chest.shown = { true, chest.canPut, true, chest.owner }
  local held, add, logged = { { header = true, text = string.format("%d of %d slots used", chest.used, chest.slots) } }, {}, {}
  for i = 2, #lines do
    local r = Fields(lines[i])
    if r[1] == "S" then
      local guid, count, link, bound = r[2], Num(r[4]), r[5], r[6] ~= "" and r[6] or nil
      held[#held + 1] = { text = ItemName(link) .. (count > 1 and (" x" .. count) or ""), link = link, dim = bound ~= nil or not chest.canTake,
        sub = bound or (chest.canTake and "take" or ""), fn = chest.canTake and not bound and function() Send("sttake", chest.placed, guid) end or nil }
    elseif r[1] == "D" then
      local guid, count, link = r[2], Num(r[4]), r[5]
      add[#add + 1] = { text = ItemName(link) .. (count > 1 and (" x" .. count) or ""), link = link, sub = "put in",
        fn = function() Send("stput", chest.placed, guid) end }
    elseif r[1] == "L" then
      -- L, time, character, action, entry, count, item name
      local count = Num(r[6])
      logged[#logged + 1] = { text = string.format("%s %s %s%s", r[3], r[4], r[7] ~= "" and r[7] or ("item " .. r[5]),
        count > 1 and (" x" .. count) or ""), sub = When(r[2]) }
    end
  end
  table.sort(add, function(a, b) return a.text < b.text end)
  if #add == 0 then add[1] = { header = true, text = "Nothing in your bags can go in." } end
  if #logged == 0 then logged[1] = { header = true, text = "Nobody has used this chest yet." } end
  contents.items, puts.items, log.items = held, add, logged
  contents:Update(); puts:Update(); log:Update()
  W.title:SetText(chest.name)
  W.status:SetText(string.format("%d of %d slots used", chest.used, chest.slots))
  W.note:SetText(chest.message ~= "" and chest.message or (chest.owner and "Your chest." or
    (chest.canTake and chest.canPut and "You may take and put in." or chest.canTake and "You may take things out." or "You may put things in.")))
  if not nameBox:HasFocus() then nameBox:SetText(chest.name) end
  takeButton.value, putButton.value = chest.take, chest.put
  takeButton:SetText(AccessName(chest.take)); putButton:SetText(AccessName(chest.put))
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
  if lines[1] and lines[1]:match("^storage\t") then Read(lines) end
end)
