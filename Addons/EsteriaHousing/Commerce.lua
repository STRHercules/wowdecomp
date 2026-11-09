-- Esteria housing: player shops and crafting commissions. The server holds the stock, gold and orders and checks every
-- command; this file draws its answers. Commands are tab-separated text (EsteriaHousing_Commerce); answers are lines of
-- tab-separated fields (ESTERIA_HOUSING_COMMERCE, mod-esteria-housing HousingCommerce.inc).

if not EsteriaHousing_IsAvailable then return end
local H = EsteriaHousingUI

local W, HEIGHT, ROWS, ROW_H = 520, 480, 8, 36
local SYMBOLS = { "INV_Hammer_20", "INV_Potion_93", "INV_Misc_Gem_01", "INV_Misc_Bag_10", "INV_Misc_Coin_02", "INV_Fabric_Linen_01",
  "INV_Misc_LeatherScrap_02", "INV_Gizmo_02", "INV_Enchant_DustArcane", "INV_Misc_Book_09", "INV_Sword_04", "INV_Shield_06",
  "INV_Misc_Food_15", "INV_Scroll_03", "INV_Jewelry_Ring_03", "INV_Misc_Herb_01" }
local STATES = { [0] = "Open", "Accepted", "Completed", "Cancelled", "Expired" }
local QUALITY = { [0] = "9d9d9d", "ffffff", "1eff00", "0070dd", "a335ee", "ff8000", "e6cc80", "e6cc80" }

local function Send(...) EsteriaHousing_Commerce(strjoin("\t", ...)) end
local function Money(c) return GetCoinTextureString(tonumber(c) or 0) end

local function Fields(line)
  local out, start = {}, 1
  while true do
    local tab = line:find("\t", start, true)
    out[#out + 1] = line:sub(start, tab and tab - 1 or nil)
    if not tab then return out end
    start = tab + 1
  end
end

local function Input(parent, name, w, letters, numeric)
  local b = CreateFrame("EditBox", name, parent)
  b:SetSize(w, 22); b:SetAutoFocus(false); b:SetMaxLetters(letters); b:SetNumeric(numeric or false)
  b:SetFontObject(ChatFontNormal); b:SetTextInsets(6, 6, 0, 0)
  b:SetBackdrop({ bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10,
    insets = { left = 2, right = 2, top = 2, bottom = 2 } })
  b:SetBackdropColor(0, 0, 0, 0.55); b:SetBackdropBorderColor(0.55, 0.42, 0.25)
  b:SetScript("OnEnterPressed", b.ClearFocus); b:SetScript("OnEscapePressed", b.ClearFocus)
  b:SetScript("OnEditFocusGained", function(self) self:SetBackdropBorderColor(1, 0.82, 0.2) end)
  b:SetScript("OnEditFocusLost", function(self) self:SetBackdropBorderColor(0.55, 0.42, 0.25); self:HighlightText(0, 0) end)
  return b
end

local function Label(parent, text, font)
  local l = parent:CreateFontString(nil, "OVERLAY", font or "GameFontHighlightSmall")
  l:SetJustifyH("LEFT"); l:SetText(text or "")
  return l
end

-- A window with tabs, a status line, and a page of rows: icon, text, money on the right, up to three short buttons.
local function ListWindow(name, heading, tabs)
  local f = H.Panel(name, W, HEIGHT)
  f:SetPoint("CENTER", -60, 40); tinsert(UISpecialFrames, name)
  f.title = f:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
  f.title:SetPoint("TOP", 0, -9); f.title:SetText(heading)
  CreateFrame("Button", nil, f, "UIPanelCloseButton"):SetPoint("TOPRIGHT", 2, 2)
  f.status = Label(f, "", "GameFontNormalSmall"); f.status:SetPoint("BOTTOMLEFT", 18, 44); f.status:SetWidth(W - 36)
  f.tabs, f.rows, f.page, f.items = {}, {}, 0, {}
  for i, t in ipairs(tabs) do
    local b = CreateFrame("Button", name .. "Tab" .. i, f, "OptionsFrameTabButtonTemplate")
    b:SetID(i); b:SetText(t[2])
    if i == 1 then b:SetPoint("TOPLEFT", 18, -92) else b:SetPoint("LEFT", f.tabs[i - 1], "RIGHT", -14, 0) end
    PanelTemplates_TabResize(b, -8)
    b:SetScript("OnShow", function(self) PanelTemplates_TabResize(self, -8) end)
    b:SetScript("OnClick", function() f.page = 0; f.tab = t[1]; PlaySound("igCharacterInfoTab"); if f.OnTab then f.OnTab(t[1]) end; f.Redraw() end)
    f.tabs[i] = b
  end
  for i = 1, ROWS do
    local row = CreateFrame("Frame", nil, f)
    row:SetSize(W - 36, ROW_H - 2); row:SetPoint("TOP", 0, -122 - (i - 1) * ROW_H)
    local bg = row:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
    row.iconButton = CreateFrame("Button", nil, row); row.iconButton:SetSize(28, 28); row.iconButton:SetPoint("LEFT", 3, 0)
    row.icon = row.iconButton:CreateTexture(nil, "ARTWORK"); row.icon:SetAllPoints()
    row.iconButton:SetScript("OnEnter", function(self)
      if not row.link then return end
      GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetHyperlink(row.link); GameTooltip:Show()
    end)
    row.iconButton:SetScript("OnLeave", function() GameTooltip:Hide() end)
    row.label = Label(row); row.label:SetPoint("LEFT", row.iconButton, "RIGHT", 6, 0); row.label:SetSize(W - 250, ROW_H - 4)
    row.buttons = {}
    for b = 1, 3 do
      local button = CreateFrame("Button", nil, row, "UIPanelButtonTemplate")
      button:SetSize(56, 20)
      if b == 1 then button:SetPoint("RIGHT", -3, 0) else button:SetPoint("RIGHT", row.buttons[b - 1], "LEFT", -2, 0) end
      button:SetScript("OnClick", function() if button.run then button.run() end end)
      row.buttons[b] = button
    end
    f.rows[i] = row
  end
  local prev = H.Button(f, "Previous", 76, 22); prev:SetPoint("BOTTOMLEFT", 16, 14)
  local nxt = H.Button(f, "Next", 76, 22); nxt:SetPoint("BOTTOMRIGHT", -16, 14)
  local pageText = Label(f); pageText:SetPoint("BOTTOM", 0, 20)
  prev:SetScript("OnClick", function() f.page = f.page - 1; f.Redraw() end)
  nxt:SetScript("OnClick", function() f.page = f.page + 1; f.Redraw() end)

  -- every row is { icon, text, link, { {label, fn}, ... } }
  function f.Fill(items)
    f.items = items
    local pages = math.max(1, math.ceil(#items / ROWS))
    f.page = math.min(f.page, pages - 1)
    for i, row in ipairs(f.rows) do
      local item = items[f.page * ROWS + i]
      if item then
        row:Show()
        row.icon:SetTexture(item[1] or "Interface\\Icons\\INV_Misc_QuestionMark")
        row.label:SetText(item[2]); row.link = item[3]
        for b, button in ipairs(row.buttons) do
          local action = item[4] and item[4][b]
          if action then button:SetText(action[1]); button.run = action[2]; button:Show() else button:Hide() end
        end
      else row:Hide() end
    end
    if f.page > 0 then prev:Enable() else prev:Disable() end
    if f.page < pages - 1 then nxt:Enable() else nxt:Disable() end
    pageText:SetText(f.page + 1 .. " / " .. pages)
    for i, b in ipairs(f.tabs) do
      if f.tabList and f.tabList[i] == f.tab then PanelTemplates_SelectTab(b) else PanelTemplates_DeselectTab(b) end
    end
  end
  function f.HideRows() f.Fill({}) end
  return f
end

-- ------------------------------------------------------------------------------------------------ the shop

local shop, listings, stockable = nil, {}, {}
local worn, wearable = {}, {}
local ledger
local s = ListWindow("EsteriaHousingShopFrame", "Shop", { { "shop", "Shop" }, { "bags", "Add Items" }, { "ledger", "Ledger" }, { "setup", "Setup" }, { "gear", "Equipment" } })
s.tabList = { "shop", "bags", "ledger", "setup", "gear" }; s.tab = "shop"

local signBorder = s:CreateTexture(nil, "OVERLAY"); signBorder:SetSize(66, 66); signBorder:SetPoint("TOPLEFT", 8, -32)
signBorder:SetTexture("Interface\\Buttons\\UI-Quickslot2")
local signIcon = s:CreateTexture(nil, "ARTWORK"); signIcon:SetSize(38, 38); signIcon:SetPoint("CENTER", signBorder)
local signText = Label(s, "", "GameFontNormal"); signText:SetPoint("TOPLEFT", 74, -44); signText:SetWidth(W - 230)
local greeting = Label(s); greeting:SetPoint("TOPLEFT", signText, "BOTTOMLEFT", 0, -4); greeting:SetWidth(W - 230)
local treasury = Label(s); treasury:SetPoint("TOPRIGHT", -20, -40); treasury:SetJustifyH("RIGHT")
local withdraw = H.Button(s, "Withdraw", 76, 20); withdraw:SetPoint("TOPRIGHT", -20, -54)
withdraw:SetScript("OnClick", function() Send("withdraw") end)
withdraw:SetScript("OnEnter", function(self)
  GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:AddLine("Takes the earnings of all your shops."); GameTooltip:Show()
end)
withdraw:SetScript("OnLeave", function() GameTooltip:Hide() end)

local bundleLabel = Label(s, "Sold in bundles of"); bundleLabel:SetPoint("TOPRIGHT", -62, -96)
local bundleBox = Input(s, "EsteriaHousingShopBundle", 36, 3, true); bundleBox:SetPoint("LEFT", bundleLabel, "RIGHT", 8, 0); bundleBox:SetText("1")

local pendingPrice
StaticPopupDialogs["ESTERIA_HOUSING_PRICE"] = {
  text = "Price for each bundle:", button1 = ACCEPT, button2 = CANCEL, hasMoneyInputFrame = 1, timeout = 0, hideOnEscape = 1,
  OnAccept = function(self)
    local copper = MoneyInputFrame_GetCopper(_G[self:GetName() .. "MoneyInputFrame"])
    if pendingPrice then pendingPrice(copper) end
  end,
}
local function AskPrice(fn) pendingPrice = fn; StaticPopup_Show("ESTERIA_HOUSING_PRICE") end
local function Bundle() local n = tonumber(bundleBox:GetText()) or 1; return math.max(n, 1) end

StaticPopupDialogs["ESTERIA_HOUSING_UNLOCK_BANK"] = {
  text = "Pay %s to stock your shops from your personal bank? This unlocks it for your whole account.", button1 = ACCEPT, button2 = CANCEL,
  timeout = 0, hideOnEscape = 1, OnAccept = function() Send("unlockbank") end,
}

-- Setup: one form, one Save
local setup = CreateFrame("Frame", nil, s); setup:SetPoint("TOPLEFT", 18, -122); setup:SetPoint("BOTTOMRIGHT", -18, 60); setup:Hide()
local FIELD_X = 104
local function SetupLine(y, text, w, letters)
  local l = Label(setup, text); l:SetPoint("TOPLEFT", 0, y - 4)
  local box = Input(setup, nil, w, letters); box:SetPoint("TOPLEFT", FIELD_X, y)
  return box
end
local function Section(y, text)
  local l = Label(setup, text, "GameFontNormal"); l:SetPoint("TOPLEFT", 0, y)
  local rule = setup:CreateTexture(nil, "ARTWORK"); rule:SetTexture(0.55, 0.42, 0.25, 0.6); rule:SetHeight(1)
  rule:SetPoint("TOPLEFT", 0, y - 15); rule:SetPoint("TOPRIGHT", 0, y - 15)
end
local FIELD_W = W - 36 - FIELD_X - 4
Section(0, "Shop")
local nameBox = SetupLine(-22, "Name", 220, 40)
local themeBox = SetupLine(-48, "Title", 220, 40)
local hiBox = SetupLine(-74, "Greeting", FIELD_W, 200)
local thanksBox = SetupLine(-100, "After a sale", FIELD_W, 200)
local byeBox = SetupLine(-126, "Farewell", FIELD_W, 200)
local choice = { symbol = 0, border = 0 }
local function Cycler(y, text, key, count, labelOf)
  local l = Label(setup, text); l:SetPoint("TOPLEFT", 0, y - 4)
  local value = Label(setup); value:SetPoint("TOPLEFT", FIELD_X + 64, y - 4); value:SetWidth(240)
  local function Paint() value:SetText(labelOf(choice[key])); if s.PaintSign then s.PaintSign() end end
  local function Step(d) choice[key] = (choice[key] + d) % count(); Paint() end
  local back = H.Button(setup, "<", 28, 20); back:SetPoint("TOPLEFT", FIELD_X, y); back:SetScript("OnClick", function() Step(-1) end)
  local fwd = H.Button(setup, ">", 28, 20); fwd:SetPoint("LEFT", back, "RIGHT", 4, 0); fwd:SetScript("OnClick", function() Step(1) end)
  return Paint, { l, value, back, fwd }
end
local customize = H.Button(setup, "Customize Appearance", 170, 22); customize:SetPoint("TOPLEFT", FIELD_X, -152)
customize:SetScript("OnClick", function() Send("appearance") end)
-- a case, shelf, rack, counter or sign: how high its name floats over it, in yards
local heightLabel = Label(setup, "Name height"); heightLabel:SetPoint("TOPLEFT", 0, -156)
local heightBox = Input(setup, nil, 60, 4); heightBox:SetPoint("TOPLEFT", FIELD_X, -152)
local heightHint = Label(setup, "yards above its base (0 to 10)", "GameFontDisableSmall"); heightHint:SetPoint("LEFT", heightBox, "RIGHT", 8, 0)
Section(-184, "Sign")
local signBox = SetupLine(-206, "Sign text", 220, 40)
local BORDERS = { [0] = "Plain", "Gold", "Silver", "Bronze" }
local paints = {
  (Cycler(-232, "Symbol", "symbol", function() return #SYMBOLS end, function(v) return v + 1 .. " of " .. #SYMBOLS end)),
  (Cycler(-258, "Border", "border", function() return 4 end, function(v) return BORDERS[v] end)),
}
local save = H.Button(setup, "Save Changes", 120, 24); save:SetPoint("BOTTOMRIGHT", 0, -2)
save:SetScript("OnClick", function()
  Send("setup", nameBox:GetText(), themeBox:GetText(), 0, hiBox:GetText(), thanksBox:GetText(), byeBox:GetText(), signBox:GetText(), choice.symbol, choice.border,
    tonumber(heightBox:GetText()) or (shop and shop.labelHeight) or 2.4)
end)

local function PaintSign(symbol, border)
  signIcon:SetTexture("Interface\\Icons\\" .. SYMBOLS[symbol % #SYMBOLS + 1])
  local tint = ({ [0] = { 1, 1, 1 }, { 1, 0.82, 0.2 }, { 0.8, 0.85, 0.95 }, { 0.75, 0.5, 0.25 } })[border % 4]
  signBorder:SetVertexColor(tint[1], tint[2], tint[3])
end
s.PaintSign = function() if s.tab == "setup" then PaintSign(choice.symbol, choice.border) end end

local function ListingRow(l)
  local nameText = "|cff" .. (QUALITY[l.quality] or "ffffff") .. l.name .. "|r"
  if l.bundle > 1 then nameText = nameText .. " x" .. l.bundle end
  local stockText = l.stock > 0 and (l.stock .. " in stock") or "|cffff5050Sold out|r"
  if not l.published then stockText = stockText .. " |cffa0a0a0(hidden)|r" end
  local text = nameText .. "\n" .. Money(l.price) .. "  " .. stockText
  local buttons
  if shop.owner then
    buttons = {
      { "Take", function() Send("retrieve", l.id) end },
      { l.published and "Hide" or "Show", function() Send("publish", l.id, l.published and 0 or 1) end },
      { "Price", function() AskPrice(function(c) Send("price", l.id, Bundle(), c) end) end },
    }
  elseif l.stock >= l.bundle then
    buttons = { { "Buy", function() Send("buy", l.id, l.price) end } }
  end
  return { GetItemIcon(l.entry), text, l.link, buttons }
end

local SOURCES = { [0] = "Bags", "Esteria Inventory", "Personal Bank" }
local function StockRows()
  local rows = {}
  if not shop.bank then
    rows[1] = { "Interface\\Icons\\INV_Misc_Coin_02", "|cffffd100Personal Bank|r\nStock your shops from your bank. " .. Money(shop.bankPrice) .. ", once per account.", nil,
      { { "Unlock", function() StaticPopup_Show("ESTERIA_HOUSING_UNLOCK_BANK", Money(shop.bankPrice)) end } } }
  end
  for _, i in ipairs(stockable) do
    local buttons = { { "List", function() AskPrice(function(c) Send("deposit", i.source, i.guid, Bundle(), c, 0) end) end } }
    for _, l in ipairs(listings) do
      if l.entry == i.entry then buttons[2] = { "Add", function() Send("deposit", i.source, i.guid, 0, 0, l.id) end }; break end
    end
    rows[#rows + 1] = { GetItemIcon(i.entry), "|cff" .. (QUALITY[i.quality] or "ffffff") .. i.name .. "|r" .. (i.count > 1 and (" x" .. i.count) or "") ..
      "\n|cffa0a0a0" .. SOURCES[i.source] .. "|r", i.link, buttons }
  end
  return rows
end

function s.Redraw()
  if not shop then return end
  s.title:SetText(shop.name .. (shop.theme ~= "" and (" |cffa0a0a0" .. shop.theme .. "|r") or ""))
  signText:SetText(shop.sign ~= "" and shop.sign or shop.name)
  greeting:SetText(shop.greeting)
  if s.tab ~= "setup" then PaintSign(shop.symbol, shop.border) end
  for i, b in ipairs(s.tabs) do if (shop.owner and (i < 5 or shop.kind == 1)) or i == 1 then b:Show() else b:Hide() end end
  if shop.owner then
    treasury:SetText("Earnings: " .. Money(shop.treasury)); withdraw:Show()
  else treasury:SetText(""); withdraw:Hide() end
  if shop.owner and (s.tab == "shop" or s.tab == "bags") then bundleLabel:Show(); bundleBox:Show() else bundleLabel:Hide(); bundleBox:Hide() end
  setup:Hide()
  if s.tab == "setup" then
    s.HideRows(); setup:Show()
    if shop.kind == 1 then customize:Show() else customize:Hide() end
for _, w in ipairs({ heightLabel, heightBox, heightHint }) do if shop.kind == 1 then w:Hide() else w:Show() end end
    for _, p in ipairs(paints) do p() end
  elseif s.tab == "bags" then s.Fill(StockRows())
  elseif s.tab == "gear" then
    local rows = {}
    for _, g in ipairs(worn) do
      rows[#rows + 1] = { GetItemIcon(g.entry), "|cff" .. (QUALITY[g.quality] or "ffffff") .. g.name .. "|r\n|cff80ff80Wearing|r", g.link, { { "Take", function() Send("unequip", g.slot) end } } }
    end
    for _, g in ipairs(wearable) do
      rows[#rows + 1] = { GetItemIcon(g.entry), "|cff" .. (QUALITY[g.quality] or "ffffff") .. g.name .. "|r\n|cffa0a0a0" .. SOURCES[g.source] .. "|r", g.link, { { "Equip", function() Send("equip", g.source, g.guid) end } } }
    end
    s.Fill(rows)
  elseif s.tab == "ledger" then
    local rows = {}
    if ledger then
      rows[1] = { "Interface\\Icons\\INV_Misc_Coin_02", "All shops: " .. ledger.sales .. " sales\nGross " .. Money(ledger.gross) .. ", available " .. Money(ledger.available) }
      for _, b in ipairs(ledger.best) do rows[#rows + 1] = { "Interface\\Icons\\Achievement_Reputation_01", "|cffffd100Best seller:|r " .. b[1] .. " x" .. b[2] .. "\n" .. Money(b[3]) } end
      for _, t in ipairs(ledger.sales_list) do
        rows[#rows + 1] = { "Interface\\Icons\\INV_Letter_15", t[1] .. " x" .. t[2] .. " to |cffffffff" .. t[4] .. "|r at " .. t[6] .. "\n" .. Money(t[3]) .. ", " .. SecondsToTime(tonumber(t[5])) .. " ago" }
      end
    end
    s.Fill(rows)
  else
    local rows = {}
    for _, l in ipairs(listings) do rows[#rows + 1] = ListingRow(l) end
    s.Fill(rows)
  end
end

s.OnTab = function(t)
  if t == "ledger" then Send("ledger") elseif t == "bags" then Send("stockable") elseif t == "gear" then Send("gear") end
end
s:SetScript("OnHide", function()
  if not shop then return end
  if not EsteriaHousingCustomizer:IsShown() then Send("close") end -- the customizer still needs the shop open on the server
  shop = nil
end)

local function ReadShop(lines)
  local a = Fields(lines[1])
  local keep = shop and shop.name
  shop = { owner = a[2] == "1", name = a[3], theme = a[4], greeting = a[5], message = a[6], treasury = tonumber(a[7]) or 0, kind = tonumber(a[8]) or 1,
    thanks = a[10], farewell = a[11], sign = a[12], symbol = tonumber(a[13]) or 0, border = tonumber(a[14]) or 0,
    bank = a[15] == "1", bankPrice = tonumber(a[16]) or 0, labelHeight = tonumber(a[17]) or 2.4 }
  listings = {}
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "L" then
      listings[#listings + 1] = { id = tonumber(f[2]), entry = tonumber(f[3]), name = f[4], bundle = tonumber(f[5]), price = tonumber(f[6]),
        stock = tonumber(f[7]), link = f[8], published = f[9] == "1", quality = tonumber(f[10]) } end
  end
  if not keep then -- newly opened
    s.page = 0; s.tab = "shop"; stockable = {}
    nameBox:SetText(shop.name); themeBox:SetText(shop.theme); hiBox:SetText(shop.greeting); thanksBox:SetText(shop.thanks); byeBox:SetText(shop.farewell); signBox:SetText(shop.sign)
    choice.symbol, choice.border = shop.symbol, shop.border
    heightBox:SetText(string.format("%.1f", shop.labelHeight))
    s:Show()
  end
  s.status:SetText(shop.message or "")
  s.Redraw()
end

local function ReadStockable(lines)
  stockable = {}
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "I" then
      stockable[#stockable + 1] = { source = tonumber(f[2]), guid = f[3], entry = tonumber(f[4]), name = f[5], count = tonumber(f[6]), link = f[7], quality = tonumber(f[8]) }
    end
  end
  table.sort(stockable, function(a, b) if a.name ~= b.name then return a.name < b.name end return a.source < b.source end)
  s.Redraw()
end

local function ReadGear(lines)
  worn, wearable = {}, {}
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "W" then worn[#worn + 1] = { slot = f[2], entry = tonumber(f[3]), name = f[4], link = f[5], quality = tonumber(f[6]) }
    elseif f[1] == "G" then wearable[#wearable + 1] = { source = tonumber(f[2]), guid = f[3], entry = tonumber(f[4]), name = f[5], link = f[6], quality = tonumber(f[7]) } end
  end
  table.sort(wearable, function(a, b) return a.name < b.name end)
  s.status:SetText(Fields(lines[1])[2] or "")
  s.Redraw()
end

local function ReadLedger(lines)
  local a = Fields(lines[1])
  ledger = { sales = a[2], gross = a[3], available = a[4], best = {}, sales_list = {} }
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "B" then ledger.best[#ledger.best + 1] = { f[2], f[3], f[4] }
    elseif f[1] == "T" then ledger.sales_list[#ledger.sales_list + 1] = { f[2], f[3], f[4], f[5], f[6], f[7] } end
  end
  s.Redraw()
end

-- ------------------------------------------------------------------------------------------------ the commission board

local orders, recipes = {}, {}
local b = ListWindow("EsteriaHousingBoardFrame", "Commission Board", { { "open", "Open Orders" }, { "mine", "My Orders" }, { "post", "Request" } })
b.tabList = { "open", "mine", "post" }; b.tab = "open"
local boardHelp = Label(b, "Crafters pick up orders here. Materials and payment stay with the board until the job ends."); boardHelp:SetPoint("TOPLEFT", 18, -46); boardHelp:SetWidth(W - 36)
local search = Input(b, "EsteriaHousingBoardSearch", 190, 60); search:SetPoint("TOPLEFT", 90, -68)
local searchLabel = Label(b, "Item name"); searchLabel:SetPoint("RIGHT", search, "LEFT", -10, 0)
search:SetScript("OnEnterPressed", function(self) self:ClearFocus(); Send("search", self:GetText()) end)
local qty = Input(b, "EsteriaHousingBoardQuantity", 30, 2, true); qty:SetPoint("LEFT", search, "RIGHT", 62, 0); qty:SetText("1")
local qtyLabel = Label(b, "How many"); qtyLabel:SetPoint("RIGHT", qty, "LEFT", -8, 0)
local find = H.Button(b, "Find", 60, 20); find:SetPoint("LEFT", qty, "RIGHT", 10, 0)
find:SetScript("OnClick", function() Send("search", search:GetText()) end)

-- shift-clicking an item link while typing asks for that item
local insertLink = ChatEdit_InsertLink
function ChatEdit_InsertLink(link)
  if search:IsVisible() and search:HasFocus() and link then
    search:SetText(link:match("%[(.-)%]") or link); return true
  end
  return insertLink(link)
end

local pendingOrder
StaticPopupDialogs["ESTERIA_HOUSING_FEE"] = {
  text = "Payment for the crafter:", button1 = ACCEPT, button2 = CANCEL, hasMoneyInputFrame = 1, timeout = 0, hideOnEscape = 1,
  OnAccept = function(self)
    if pendingOrder then Send("post", pendingOrder, math.max(tonumber(qty:GetText()) or 1, 1), MoneyInputFrame_GetCopper(_G[self:GetName() .. "MoneyInputFrame"])) end
  end,
}

function b.Redraw()
  local posting = b.tab == "post"
  for _, w in ipairs({ search, searchLabel, qty, qtyLabel, find }) do if posting then w:Show() else w:Hide() end end
  local rows = {}
  if posting then
    for _, r in ipairs(recipes) do
      rows[#rows + 1] = { GetItemIcon(r.item), r.name .. " |cffa0a0a0" .. r.skill .. "|r\n" .. r.materials, "item:" .. r.item,
        { { "Pick", function() pendingOrder = r.spell; StaticPopup_Show("ESTERIA_HOUSING_FEE") end } } }
    end
  else
    for _, o in ipairs(orders) do
      local mine = o.role > 0
      if (b.tab == "mine") == mine then
        local text = o.name .. " x" .. o.quantity .. "  " .. Money(o.fee) .. " |cffa0a0a0" .. STATES[o.state] .. (o.crafter ~= "" and (", " .. o.crafter) or "") .. "|r\n" .. o.skill .. ": " .. o.materials
        local buttons
        if o.role == 0 and o.state == 0 and o.knows then buttons = { { "Accept", function() Send("accept", o.id) end } }
        elseif o.role == 1 and o.state < 2 then buttons = { { "Cancel", function() Send("cancel", o.id) end } }
        elseif o.role == 2 and o.state == 1 then buttons = { { "Finish", function() Send("complete", o.id) end }, { "Drop", function() Send("drop", o.id) end } } end
        rows[#rows + 1] = { GetItemIcon(o.item), text, "item:" .. o.item, buttons }
      end
    end
  end
  b.Fill(rows)
end

local function ReadBoard(lines)
  local a = Fields(lines[1])
  orders = {}
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "O" then
      orders[#orders + 1] = { id = f[2], state = tonumber(f[3]), item = tonumber(f[4]), name = f[5], quantity = f[6], fee = tonumber(f[7]), customer = f[8],
        crafter = f[9], role = tonumber(f[10]), knows = f[11] == "1", skill = f[12], materials = (f[13] or ""):gsub(", $", "") }
    end
  end
  b.status:SetText(a[2] or "")
  b:Show()
  b.Redraw()
end

local function ReadRecipes(lines)
  recipes = {}
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "R" then recipes[#recipes + 1] = { spell = f[2], item = tonumber(f[3]), name = f[4], skill = f[5], materials = (f[6] or ""):gsub(", $", "") } end
  end
  b.status:SetText(#recipes == 0 and "No recipe makes that." or "")
  b.Redraw()
end

-- ------------------------------------------------------------------------------------------------ the merchant customizer
-- It edits the real merchant in the world, with the character creator's option tables, so the NPC is the preview. Others see
-- the result when it is saved.

local CUST_ROWS, CROW_H, CBOX_TOP = 10, 28, -132
local c = H.Panel("EsteriaHousingCustomizer", 360, 560)
c:SetPoint("RIGHT", -40, 20)
tinsert(UISpecialFrames, "EsteriaHousingCustomizer")
local cTitle = c:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge"); cTitle:SetPoint("TOP", 0, -12); cTitle:SetText("Merchant Appearance")
CreateFrame("Button", nil, c, "UIPanelCloseButton"):SetPoint("TOPRIGHT", 2, 2)
local cRaces, cRace, cSex, cTab, cPage, cSaved = {}, 1, 0, "looks", 0, false
-- the customizer edits a merchant (its shop is open) or a mannequin (cTarget: its placed id, over the collections channel)
local cTarget
local function CSend(verb, ...)
  if cTarget then EsteriaHousing_Commerce(strjoin("\t", "col", "mq" .. verb, cTarget, ...)) else Send(verb, ...) end
end

-- the page-turn arrows of the spellbook: round, and the same size everywhere
local function Arrow(parent, dir)
  local b = CreateFrame("Button", nil, parent)
  b:SetSize(24, 24)
  local base = "Interface\\Buttons\\UI-SpellbookIcon-" .. (dir < 0 and "Prev" or "Next") .. "Page-"
  b:SetNormalTexture(base .. "Up"); b:SetPushedTexture(base .. "Down"); b:SetDisabledTexture(base .. "Disabled")
  b:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
  return b
end

-- a dark bar with a value between two arrows
local function Stepper(parent, width)
  local bar = CreateFrame("Frame", nil, parent)
  bar:SetSize(width, 26)
  bar:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    edgeSize = 10, insets = { left = 2, right = 2, top = 2, bottom = 2 } })
  bar:SetBackdropColor(0, 0, 0, 0.6); bar:SetBackdropBorderColor(0.55, 0.42, 0.25)
  bar.prev = Arrow(bar, -1); bar.prev:SetPoint("LEFT", 1, 0)
  bar.next = Arrow(bar, 1); bar.next:SetPoint("RIGHT", -1, 0)
  bar.text = bar:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
  bar.text:SetPoint("LEFT", 26, 0); bar.text:SetPoint("RIGHT", -26, 0); bar.text:SetHeight(14)
  return bar
end

-- race, and sex as two toggles
local raceBar = Stepper(c, 240); raceBar:SetPoint("TOP", 0, -48)
raceBar.text:SetFontObject(GameFontNormal)
local function ChangeRace(d)
  local at = 1
  for i, r in ipairs(cRaces) do if r.id == cRace then at = i end end
  CSend("race", cRaces[(at - 1 + d) % #cRaces + 1].id, cSex)
end
raceBar.prev:SetScript("OnClick", function() ChangeRace(-1) end); raceBar.next:SetScript("OnClick", function() ChangeRace(1) end)
local male = H.Button(c, "Male", 90, 22); male:SetPoint("TOPRIGHT", c, "TOP", -3, -82)
local female = H.Button(c, "Female", 90, 22); female:SetPoint("TOPLEFT", c, "TOP", 3, -82)
male:SetScript("OnClick", function() if cSex ~= 0 then CSend("race", cRace, 0) end end)
female:SetScript("OnClick", function() if cSex ~= 1 then CSend("race", cRace, 1) end end)

-- Appearance and Body, as tabs on an inset
local cbox = CreateFrame("Frame", nil, c)
cbox:SetPoint("TOPLEFT", 14, CBOX_TOP); cbox:SetPoint("BOTTOMRIGHT", -14, 46)
cbox:SetBackdrop({ bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
  edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } })
cbox:SetBackdropColor(0, 0, 0, 0.45); cbox:SetBackdropBorderColor(0.55, 0.42, 0.25)
local CTABS = { { "looks", "Appearance" }, { "body", "Body" } }
local ctabs = {}
for i, t in ipairs(CTABS) do
  local b = CreateFrame("Button", "EsteriaHousingCustomizerTab" .. i, c, "OptionsFrameTabButtonTemplate")
  b:SetID(i); b:SetText(t[2])
  if i == 1 then b:SetPoint("BOTTOMLEFT", cbox, "TOPLEFT", 6, -2) else b:SetPoint("LEFT", ctabs[i - 1], "RIGHT", -14, 0) end
  PanelTemplates_TabResize(b, 0)
  ctabs[i] = b
end
c.numTabs = #CTABS
local cHint = Label(cbox, "Waiting for the character to load...", "GameFontNormalSmall"); cHint:SetPoint("TOPLEFT", 12, -12)

-- each option a striped line: its name, then its value between arrows (or a slider for the body)
local cRowsList = {}
for i = 1, CUST_ROWS do
  local row = CreateFrame("Frame", nil, cbox)
  row:SetHeight(CROW_H - 2); row:SetPoint("TOPLEFT", 6, -8 - (i - 1) * CROW_H); row:SetPoint("RIGHT", -6, 0)
  local bg = row:CreateTexture(nil, "BACKGROUND"); bg:SetAllPoints(); bg:SetTexture(1, 1, 1, i % 2 == 0 and 0.03 or 0.07)
  row.label = Label(row, "", "GameFontNormalSmall"); row.label:SetPoint("LEFT", 8, 0); row.label:SetWidth(112)
  row.step = Stepper(row, 176); row.step:SetPoint("RIGHT", -4, 0)
  row.slider = CreateFrame("Slider", "EsteriaHousingCustomizerSlider" .. i, row, "OptionsSliderTemplate")
  row.slider:SetWidth(170); row.slider:SetHeight(16); row.slider:SetPoint("RIGHT", -10, 0); row.slider:SetValueStep(0.01)
  for _, part in ipairs({ "Low", "High", "Text" }) do _G[row.slider:GetName() .. part]:SetText("") end
  row.slider:SetScript("OnValueChanged", function(self, v) if self.name and not self.filling then EsteriaHousing_MerchantSlider(self.name, v) end end)
  cRowsList[i] = row
end

-- under the lines: the page, and (Body) the creator's saved presets
local pageBar = Stepper(cbox, 140); pageBar:SetPoint("TOP", 0, -8 - CUST_ROWS * CROW_H - 4)
local presetLine = CreateFrame("Frame", nil, cbox)
presetLine:SetHeight(CROW_H - 2); presetLine:SetPoint("BOTTOMLEFT", 6, 8); presetLine:SetPoint("RIGHT", -6, 0)
local presetBg = presetLine:CreateTexture(nil, "BACKGROUND"); presetBg:SetAllPoints(); presetBg:SetTexture(1, 1, 1, 0.07)
local presetTitle = Label(presetLine, "Preset", "GameFontNormalSmall"); presetTitle:SetPoint("LEFT", 8, 0)
local presetBar = Stepper(presetLine, 170); presetBar:SetPoint("LEFT", 60, 0)
local presetApply = H.Button(presetLine, "Use", 60, 20); presetApply:SetPoint("RIGHT", -4, 0)
local presets, presetAt = {}, 0

local random = H.Button(c, "Random", 90, 22); random:SetPoint("BOTTOMLEFT", 16, 14)
local cancel = H.Button(c, "Cancel", 90, 22); cancel:SetPoint("BOTTOMRIGHT", -112, 14)
local saveLook = H.Button(c, "Save", 90, 22); saveLook:SetPoint("BOTTOMRIGHT", -16, 14)

local function PaintPreset()
  local p = presets[presetAt + 1]
  presetBar.text:SetText(p and p.name or "|cff808080None saved|r")
  if p then presetApply:Enable() else presetApply:Disable() end
  if #presets > 1 then presetBar.prev:Enable(); presetBar.next:Enable() else presetBar.prev:Disable(); presetBar.next:Disable() end
end

local function SliderNames() return { GetBodyShapeNames() } end

local function DrawCustomizer()
  local race = "?"
  for _, r in ipairs(cRaces) do if r.id == cRace then race = r.name end end
  raceBar.text:SetText(race)
  if cSex == 0 then male:LockHighlight(); female:UnlockHighlight() else female:LockHighlight(); male:UnlockHighlight() end
  for i, t in ipairs(CTABS) do if t[1] == cTab then PanelTemplates_SetTab(c, i) end end
  local names = cTab == "body" and SliderNames() or nil
  local count = names and #names or tonumber((EsteriaHousing_MerchantEA("EA_COUNT"))) or 0
  local pages = math.max(1, math.ceil(count / CUST_ROWS))
  cPage = math.min(cPage, pages - 1)
  if count == 0 then cHint:Show() else cHint:Hide() end
  for i, row in ipairs(cRowsList) do
    local index = cPage * CUST_ROWS + i
    if index > count then row:Hide()
    else
      row:Show()
      if names then
        local name = names[index]
        row.step:Hide(); row.slider:Show()
        row.label:SetText((name:gsub("(%l)(%u)", "%1 %2")))
        local low, high = GetBodyShapeRange(name)
        row.slider.filling = true
        row.slider.name = name
        row.slider:SetMinMaxValues(low or -1, high or 1)
        row.slider:SetValue(EsteriaHousing_MerchantSlider(name) or 0)
        row.slider.filling = false
      else
        row.slider:Hide(); row.step:Show()
        local current, total, label = EsteriaHousing_MerchantEA("EA_GET", index)
        current, total = tonumber(current) or 0, tonumber(total) or 1
        row.label:SetText(label or ("Option " .. index))
        local choice = EsteriaHousing_MerchantEA("EA_CHOICE_NAME", index, current - 1)
        row.step.text:SetText((choice and choice ~= "") and choice or (current .. " / " .. total))
        row.step.prev:SetScript("OnClick", function() EsteriaHousing_MerchantEA("EA_CYCLE", index, -1); DrawCustomizer() end)
        row.step.next:SetScript("OnClick", function() EsteriaHousing_MerchantEA("EA_CYCLE", index, 1); DrawCustomizer() end)
      end
    end
  end
  if cTab == "body" then presetLine:Show() else presetLine:Hide() end
  if pages > 1 then pageBar:Show() else pageBar:Hide() end
  pageBar.text:SetText(string.format("Page %d of %d", cPage + 1, pages))
  if cPage > 0 then pageBar.prev:Enable() else pageBar.prev:Disable() end
  if cPage < pages - 1 then pageBar.next:Enable() else pageBar.next:Disable() end
  PaintPreset()
end

for i, t in ipairs(CTABS) do
  ctabs[i]:SetScript("OnClick", function() PlaySound("igCharacterInfoTab"); cTab = t[1]; cPage = 0; DrawCustomizer() end)
end
pageBar.prev:SetScript("OnClick", function() cPage = cPage - 1; DrawCustomizer() end)
pageBar.next:SetScript("OnClick", function() cPage = cPage + 1; DrawCustomizer() end)
presetBar.prev:SetScript("OnClick", function() if #presets > 0 then presetAt = (presetAt - 1) % #presets; PaintPreset() end end)
presetBar.next:SetScript("OnClick", function() if #presets > 0 then presetAt = (presetAt + 1) % #presets; PaintPreset() end end)
presetApply:SetScript("OnClick", function() local p = presets[presetAt + 1]; if p then EsteriaHousing_MerchantBody(p.code); DrawCustomizer() end end)
random:SetScript("OnClick", function() EsteriaHousing_MerchantEA("EA_RANDOM"); DrawCustomizer() end)
cancel:SetScript("OnClick", function() c:Hide() end)
saveLook:SetScript("OnClick", function()
  local look = EsteriaHousing_MerchantLook()
  if not look then return UIErrorsFrame:AddMessage("The merchant is still loading.", 1, 0.3, 0.3) end
  cSaved = true
  CSend("look", look, (EsteriaHousing_MerchantBody():gsub("^BS:", "")))
  c:Hide()
end)
c:SetScript("OnHide", function() if not cSaved then CSend("lookreset") end end)

-- the merchant's character takes a moment to build after a race change
local waited = 0
c:SetScript("OnUpdate", function(self, elapsed)
  waited = waited + elapsed
  if waited < 0.5 then return end
  waited = 0
  if cHint:IsShown() then DrawCustomizer() end
end)

-- the key the creator's body presets are saved under: race, and the creator's sex number
function H.PresetKey(race, sex) return race .. ":" .. (sex + 2) end

function H.OpenCustomizer(lines)
  local a = Fields(lines[1])
  cRaces = {}
  for i = 2, #lines do
    local f = Fields(lines[i])
    if f[1] == "R" and EsteriaHousing_RaceSupported(tonumber(f[2])) then cRaces[#cRaces + 1] = { id = tonumber(f[2]), name = f[3] } end
  end
  EsteriaHousing_SetMerchant(a[2])
  cTarget = (a[6] and a[6] ~= "") and a[6] or nil
  cTitle:SetText(cTarget and ((a[7] and a[7] ~= "" and a[7] or "Mannequin") .. " Appearance") or "Merchant Appearance")
  cRace, cSex, cPage = tonumber(a[3]) or 1, tonumber(a[4]) or 0, 0
  if a[5] and a[5] ~= "" then EsteriaHousing_MerchantBody("BS:" .. a[5]) end
  presets, presetAt = {}, 0
  local list = { GetBodyShapePresets(H.PresetKey(cRace, cSex)) }
  for i = 1, #list, 2 do presets[#presets + 1] = { name = list[i], code = list[i + 1] } end
  cSaved = false
  -- a race change comes back here while the window stays open; only a fresh open needs Show
  c:Show()
  DrawCustomizer()
end

-- ------------------------------------------------------------------------------------------------ answers

local listener = CreateFrame("Frame")
listener:RegisterEvent("ESTERIA_HOUSING_COMMERCE")
listener:SetScript("OnEvent", function()
  local lines = {}
  for line in EsteriaHousing_GetCommerce():gmatch("[^\n]+") do lines[#lines + 1] = line end
  if not lines[1] then return end
  local kind = lines[1]:match("^[^\t]+")
  if kind == "shop" then ReadShop(lines)
  elseif kind == "stockable" then ReadStockable(lines)
  elseif kind == "gear" then ReadGear(lines)
  elseif kind == "customize" then H.OpenCustomizer(lines)
  elseif kind == "ledger" then ReadLedger(lines)
  elseif kind == "board" then ReadBoard(lines)
  elseif kind == "recipes" then ReadRecipes(lines)
  elseif kind == "note" then UIErrorsFrame:AddMessage(lines[1]:sub(6), 1, 0.82, 0) end
end)
