-- Two windows share one implementation: Windows[0] is the bag window (with a remote Bank tab) and
-- Windows[1] is the banker's Bank window. A window's "view" says which storage it is showing.
local Inv = {}
Inv.__index = Inv
local Windows = {}
local State = { unlocked = false, atBanker = false, atGuildBank = false, cost = 0 }
local GEOMETRY = { [0] = { "size", "position" }, [1] = { "bankSize", "bankPosition" }, [2] = { "guildSize", "guildPosition" } }
local WIDTH, HEIGHT, DEFAULT_PAGE, MAX_PAGE, BASE_TILE = 640, 380, 20, 255, 53
local MIN_W, MIN_H = 590, 300
local ACTION = { browse = 0, deposit = 1, depositAll = 2, withdraw = 3, split = 4, merge = 5,
    expand = 6, favorite = 7, activate = 8, use = 9, equip = 10, sell = 11, setTag = 12, unlock = 13 }
local sorts = { "Name", "Quality", "Count", "Category", "Tag" }
local categories = { "All", "Equipment", "Consumables", "Materials", "Other", "Favorites", "Ears", "Keyring" }
local tabWidths = { 36, 87, 104, 78, 54, 76, 45, 67 }
local EARS_CATEGORY, KEYRING_CATEGORY = 6, 7
local border = { bgFile = "Interface\\Tooltips\\UI-Tooltip-Background", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 } }
local dialog = { bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border", tile = true, tileSize = 32, edgeSize = 32,
    insets = { left = 11, right = 12, top = 12, bottom = 11 } }
local DEFAULTS = { replaceBags = true, tileScale = 1, gap = 6, maxTiles = MAX_PAGE, scale = 1, alpha = 1,
    lockPosition = false, lockSize = false, showNames = false, qualityBorders = true, sounds = true,
    hints = true, remember = true, showGold = true, contextMenu = false, bagButton = true, showBankTab = true, guildBank = true,
    autoSort = true, scrollMode = false, style = "esteria", accent = false, tileLook = "framed", viewMode = "grid",
    textSize = 10, showBar = true, showStatus = true }
local ART = "Interface\\EsteriaInventory\\"
local tileBorder = { edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", edgeSize = 10,
    insets = { left = 2, right = 2, top = 2, bottom = 2 } }

-- Saved settings with missing keys filled in; hiddenCategories/hiddenSorts are sets of disabled options.
local function db()
    EsteriaInventoryDB = EsteriaInventoryDB or {}
    local d = EsteriaInventoryDB
    for key, value in pairs(DEFAULTS) do if d[key] == nil then d[key] = value end end
    d.hiddenCategories, d.hiddenSorts = d.hiddenCategories or {}, d.hiddenSorts or {}
    d.currencies = d.currencies or {}
    d.layouts = d.layouts or {}
    return d
end

-- Esteria style kit: the flat look of the redone Esteria windows (Social, Cosmetics) drawn from WHITE8X8, so every
-- colour follows the chosen style. This chunk is embedded on its own, so it carries its own copy of the kit.
local WHITE, FONT = "Interface\\Buttons\\WHITE8X8", "Fonts\\FRIZQT__.TTF"
local STYLES = {
    { key = "esteria", name = "Esteria", note = "Ink and gold, like Social",
      bg = { 0.03, 0.04, 0.055 }, panel = { 0.06, 0.07, 0.10 }, edge = { 0.91, 0.77, 0.42 }, dim = { 0.54, 0.43, 0.18 },
      text = { 0.88, 0.86, 0.80 }, muted = { 0.60, 0.59, 0.55 }, primary = { 0.36, 0.25, 0.06 } },
    { key = "midnight", name = "Midnight", note = "Deep navy, arcane blue",
      bg = { 0.02, 0.03, 0.07 }, panel = { 0.05, 0.07, 0.13 }, edge = { 0.50, 0.70, 1.00 }, dim = { 0.22, 0.32, 0.55 },
      text = { 0.85, 0.89, 0.96 }, muted = { 0.55, 0.60, 0.70 }, primary = { 0.10, 0.20, 0.42 } },
    { key = "verdant", name = "Verdant", note = "Druidic greens and bark",
      bg = { 0.03, 0.05, 0.03 }, panel = { 0.06, 0.09, 0.06 }, edge = { 0.55, 0.85, 0.45 }, dim = { 0.26, 0.42, 0.20 },
      text = { 0.86, 0.90, 0.82 }, muted = { 0.56, 0.62, 0.52 }, primary = { 0.16, 0.30, 0.10 } },
    { key = "crimson", name = "Crimson", note = "Blood red and iron",
      bg = { 0.06, 0.02, 0.02 }, panel = { 0.10, 0.04, 0.04 }, edge = { 0.95, 0.35, 0.28 }, dim = { 0.50, 0.16, 0.12 },
      text = { 0.93, 0.85, 0.82 }, muted = { 0.66, 0.55, 0.52 }, primary = { 0.40, 0.08, 0.06 } },
    { key = "frost", name = "Frost", note = "Northrend ice and steel",
      bg = { 0.05, 0.07, 0.09 }, panel = { 0.09, 0.12, 0.15 }, edge = { 0.70, 0.90, 0.98 }, dim = { 0.32, 0.45, 0.52 },
      text = { 0.90, 0.95, 0.98 }, muted = { 0.58, 0.66, 0.72 }, primary = { 0.16, 0.30, 0.38 } },
    { key = "minimal", name = "Minimal", note = "Quiet greys, no ornament",
      bg = { 0.05, 0.05, 0.05 }, panel = { 0.09, 0.09, 0.09 }, edge = { 0.42, 0.42, 0.42 }, dim = { 0.22, 0.22, 0.22 },
      text = { 0.90, 0.90, 0.90 }, muted = { 0.55, 0.55, 0.55 }, primary = { 0.24, 0.24, 0.24 } },
    { key = "classic", name = "Classic", note = "The stock Blizzard dialog", classic = true,
      bg = { 0.04, 0.03, 0.02 }, panel = { 0.08, 0.06, 0.04 }, edge = { 1.00, 0.82, 0.00 }, dim = { 0.45, 0.35, 0.10 },
      text = { 1.00, 0.95, 0.85 }, muted = { 0.65, 0.60, 0.50 }, primary = { 0.50, 0.06, 0.03 } },
}

-- The active style, with the user's accent colour (if any) replacing its edge and dim tones.
local function style()
    local d, chosen = db(), STYLES[1]
    for _, candidate in ipairs(STYLES) do if candidate.key == d.style then chosen = candidate end end
    if not d.accent then return chosen end
    local copy = {}
    for key, value in pairs(chosen) do copy[key] = value end
    local a = d.accent
    copy.edge, copy.dim = { a[1], a[2], a[3] }, { a[1] * 0.55, a[2] * 0.55, a[3] * 0.55 }
    return copy
end

local function solid(parent, layer)
    local t = parent:CreateTexture(nil, layer or "ARTWORK"); t:SetTexture(WHITE)
    return t
end

local function tint(t, c, a) t:SetVertexColor(c[1], c[2], c[3], a or 1) end

-- One 1px line per side; returned as a list so a style change can recolour them together.
local function lines(frame)
    local list = {}
    for _, side in ipairs({ { "TOPLEFT", "TOPRIGHT", true }, { "BOTTOMLEFT", "BOTTOMRIGHT", true },
        { "TOPLEFT", "BOTTOMLEFT" }, { "TOPRIGHT", "BOTTOMRIGHT" } }) do
        local line = solid(frame, "BORDER")
        line:SetPoint(side[1]); line:SetPoint(side[2])
        if side[3] then line:SetHeight(1) else line:SetWidth(1) end
        list[#list + 1] = line
    end
    return list
end

local function tintAll(list, c, a) for _, t in ipairs(list) do tint(t, c, a) end end

local function kitText(parent, size, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GameFontHighlightSmall")
    fs:SetFont(FONT, size); fs:SetShadowOffset(1, -1); fs:SetShadowColor(0, 0, 0, 0.8)
    return fs
end

-- Everything drawn from the style registers here; paintAll() recolours it after a style or accent change.
local painted = {}
local function paintAll()
    local st = style()
    for _, widget in ipairs(painted) do widget:Paint(st) end
end
local function paintText(fs, role)
    painted[#painted + 1] = { Paint = function(_, st) local c = st[role]; fs:SetTextColor(c[1], c[2], c[3]) end }
    local c = style()[role]; fs:SetTextColor(c[1], c[2], c[3])
    return fs
end

-- Header wash: the accent fading down from the top edge, as on the Social window.
local function wash(t, st, alpha)
    t:SetGradientAlpha("VERTICAL", st.edge[1], st.edge[2], st.edge[3], 0, st.edge[1], st.edge[2], st.edge[3], alpha)
end

local function skin(frame, r, g, b)
    frame:SetBackdrop(border)
    frame:SetBackdropColor(r or 0, g or 0, b or 0, 0.9)
    frame:SetBackdropBorderColor(0.4, 0.4, 0.4, 1)
end

local function text(parent, font, value, point, x, y)
    local label = parent:CreateFontString(nil, "OVERLAY", font or "GameFontHighlightSmall")
    label:SetPoint(point or "TOPLEFT", x or 0, y or 0)
    label:SetText(value)
    return label
end

-- Flat style button. LockHighlight marks it selected (tabs, the Bank toggle, guild modes).
local function button(parent, name, label, width, point, x, y, callback)
    local b = CreateFrame("Button", name, parent)
    b:SetSize(width, 26); b:SetPoint(point, x, y)
    b.bg = solid(b, "BACKGROUND"); b.bg:SetAllPoints()
    b.glow = solid(b, "ARTWORK"); b.glow:SetAllPoints()
    b.edges = lines(b)
    b.text = kitText(b, 11); b.text:SetPoint("LEFT", 4, 1); b.text:SetPoint("RIGHT", -4, 1); b.text:SetText(label)
    b.label = { SetText = function(_, value) b.text:SetText(value) end }
    b.SetText = function(self, value) self.text:SetText(value) end
    b.LockHighlight = function(self) self.selected = true; self:Paint() end
    b.UnlockHighlight = function(self) self.selected = false; self:Paint() end
    b.Paint = function(self, st)
        st = st or style()
        local on = self.selected
        tint(self.bg, on and st.primary or st.bg, 0.95)
        tint(self.glow, st.edge, self.hover and 0.12 or 0)
        tintAll(self.edges, on and st.edge or st.dim)
        local c = on and st.edge or st.text
        self.text:SetTextColor(c[1], c[2], c[3])
    end
    b:SetScript("OnEnter", function(self) self.hover = true; self:Paint() end)
    b:SetScript("OnLeave", function(self) self.hover = false; self:Paint() end)
    b:SetScript("OnDisable", function(self) self.text:SetAlpha(0.4) end)
    b:SetScript("OnEnable", function(self) self.text:SetAlpha(1) end)
    b:SetScript("OnClick", function(...) PlaySound("igMainMenuOptionCheckBoxOn"); callback(...) end)
    b:Paint(); painted[#painted + 1] = b
    return b
end

local function enabled()
    local carried = Windows[0]
    return carried and carried.active and EsteriaInventoryDB and EsteriaInventoryDB.replaceBags ~= false
end

-- Bags stay blocked while the realm handshake is outstanding (gateUntil), so stock bags never flash before takeover.
-- A realm that never answers is released after the gate, and the stock bags work as before.
local function pending()
    local carried = Windows[0]
    return EsteriaInventory_Request and carried and not carried.active and carried.gateUntil
        and GetTime() < carried.gateUntil and EsteriaInventoryDB and EsteriaInventoryDB.replaceBags ~= false
end

local function hideStockBags()
    for i = 1, 13 do
        local f = _G["ContainerFrame" .. i]
        if f and f:IsShown() and f:GetID() <= 4 then f:Hide() end
    end
    for i = 1, 4 do
        local f = _G["DragonUI_BagsterFrame" .. i]
        if f and f:IsShown() then f:Hide() end
    end
end

local function sound(name)
    if db().sounds then PlaySound(name) end
end

-- The stock frame may be created or shown after our login-time setup, so this runs again whenever the vault opens.
local function silenceStockGuildBank()
    if not db().guildBank then return end
    if not GuildBankFrame and LoadAddOn then LoadAddOn("Blizzard_GuildBankUI") end
    local stock = GuildBankFrame
    if not stock then return end
    stock:UnregisterAllEvents()
    stock:SetScript("OnShow", nil); stock:SetScript("OnHide", nil)
    if stock:IsShown() then stock:Hide() end
    if GuildBankPopupFrame and GuildBankPopupFrame:IsShown() then GuildBankPopupFrame:Hide() end
end

-- A failing window must not stop the others (or the repaint) from picking up a setting; each error is reported once.
local reportedErrors = {}
local function applyAll()
    for scope = 0, 2 do
        local window = Windows[scope]
        if window then
            local ok, err = pcall(window.Apply, window)
            if not ok and not reportedErrors[err] then
                reportedErrors[err] = true
                DEFAULT_CHAT_FRAME:AddMessage("|cffff6060Esteria Inventory:|r " .. tostring(err))
            end
        end
    end
    paintAll()
end

local function newWindow(scope)
    local window = setmetatable({ scope = scope, view = scope,
        prefix = scope == 0 and "EsteriaInventory" or (scope == 1 and "EsteriaBank" or "EsteriaGuildBank"), category = 0, sort = 0, active = false,
        buttons = {}, hooked = false, search = "" }, Inv)
    window.snaps = { [0] = { rows = {}, revision = 0, offset = 0, total = 0 },
        [1] = { rows = {}, revision = 0, offset = 0, total = 0 },
        [2] = { rows = {}, revision = 0, offset = 0, total = 0 } }
    window.snapshot = window.snaps[scope]
    Windows[scope] = window
    return window
end

function Inv:Discover()
    if not EsteriaInventory_Request or self.serverReady then return end
    local now, name = GetTime(), UnitName("player")
    if not name or (self.nextDiscovery and now < self.nextDiscovery) then return end
    self.nextDiscovery, self.discoveryDeadline = now + 8, now + 8
    -- Old cores safely echo HELLO; only a matched core generates READY.
    SendAddonMessage("ESTERIADI", "HELLO:1", "WHISPER", name)
    self.frame.status:SetText("Checking realm support. Normal bags remain available.")
end

-- scope overrides the storage a request targets (a bag-window deposit goes to the bank).
function Inv:Request(action, row, count, offset, text, scope)
    if self.scope == 2 then
        -- Guild bank data comes from the stock client API, so browsing is local.
        if action == ACTION.browse then self:GuildRefresh(offset) end
        return
    end
    if not EsteriaInventory_Request then
        self.frame.status:SetText("The matched Esteria native client is required. Normal bags remain available.")
        return
    end
    local carried = Windows[0]
    if not carried.serverReady then carried:Discover(); return end
    if self.pending then
        -- Keep only the newest browse so tab/page/sort clicks are never lost; mutations are never queued.
        if action == ACTION.browse then self.queued = { offset = offset } end
        return
    end
    scope = scope or self.view
    -- Free placement needs every item at once; the client pages and positions them.
    if action == ACTION.browse and self:ManualActive() then offset, count = 0, MAX_PAGE
    elseif action == ACTION.browse and self:Scrolling() then
        -- Scrolling fetches up to 255 rows around the view and scrolls through them locally (see Page).
        local cols, page = self.cols or 1, self.page or DEFAULT_PAGE
        local want = offset or self.viewOffset or 0
        local margin = math.floor(math.max(0, MAX_PAGE - page) / 2 / cols) * cols
        offset, count = math.max(0, want - margin), MAX_PAGE
    end
    local snap = self.snaps[scope]
    local vendor = action == ACTION.sell and UnitGUID("npc") or nil
    -- Browse carries the wanted tile count; the server answers with at most that many rows.
    local id = EsteriaInventory_Request(action, snap.revision or 0, row and row.guid or 0,
        count or (action == ACTION.browse and self.page or 0), offset or snap.offset or 0,
        self.category, self.sort, text or self.search, vendor, scope, true)
    if id then
        self.pending = { id = id, action = action, started = GetTime() }
        self.frame.status:SetText(action == ACTION.browse and "Searching..." or "Updating inventory...")
    else self.frame.status:SetText("No realm connection. Open inventory again after reconnecting.") end
end

function Inv:Refresh()
    if self.active and not self.pending then self:Request(ACTION.browse) end
end

-- True when items sit where the user put them (auto sort off, no filter): positions live in the saved layout.
function Inv:ManualActive()
    return self.scope ~= 2 and not db().autoSort and self.category == 0 and self.search == ""
end

function Inv:Offset()
    if self:ManualActive() then return self.manualOffset or 0 end
    if self:Scrolling() then return self.viewOffset or 0 end
    return self.snapshot.offset or 0
end

function Inv:Total()
    return self:ManualActive() and (self.arrangedTotal or 0) or (self.snapshot.total or 0)
end

-- Scroll mode moves one tile row at a time; page mode moves a whole page. The guild bank always pages.
function Inv:Scrolling() return self.scope ~= 2 and db().scrollMode end
function Inv:Step() return self:Scrolling() and (self.cols or 1) or (self.page or DEFAULT_PAGE) end

function Inv:MaxOffset()
    local page, total = self.page or DEFAULT_PAGE, self:Total()
    if self:Scrolling() then
        local cols = self.cols or 1
        return math.max(0, math.ceil(total / cols) * cols - page)
    end
    return math.floor(math.max(0, total - 1) / page) * page
end

function Inv:Scroll(delta)
    local current = self.targetOffset or self:Offset()
    local offset = math.max(0, math.min(current + delta, self:MaxOffset()))
    if offset ~= current then self:Page(offset, true) end
end

-- True when the fetched block holds every row the view at `offset` needs.
function Inv:Covers(offset)
    local snap, page = self.snapshot, self.page or DEFAULT_PAGE
    local first, rows = snap.offset or 0, #(snap.rows or {})
    local last = math.min(offset + page, snap.total or 0)
    return offset >= first and last <= first + rows
end

-- scrolled: a scroll move, which may reuse the fetched block. Tab, search and view changes always ask the server.
function Inv:Page(offset, scrolled)
    if self:Scrolling() and not self:ManualActive() then
        -- Scrolling moves through the fetched block at once; the server is asked only past its edge.
        self.viewOffset = offset
        self:Render()
        if not (scrolled and self:Covers(offset)) then self:Request(ACTION.browse, nil, nil, offset) end
        return
    end
    self.targetOffset, self.manualOffset = offset, offset
    if self:ManualActive() then self:Render() end
    self:Request(ACTION.browse, nil, nil, offset)
end

local function rowKey(row)
    return row.digital and ("d" .. row.guid) or ("p" .. row.bag .. ":" .. row.slot)
end

function Inv:LayoutStore()
    local layouts, name = db().layouts, self.view == 1 and "bank" or (UnitName("player") or "?")
    layouts[name] = layouts[name] or {}
    return layouts[name]
end

-- Maps absolute position -> row. Saved positions win; new items take the first free position in server order.
-- shortcut: the server sends at most MAX_PAGE rows, so free placement covers the first 255 items; revisit if banks grow.
function Inv:Arrange()
    local store, rows = self:LayoutStore(), self.snapshot.rows
    local placed, loose, live, top = {}, {}, {}, 0
    for _, row in ipairs(rows) do
        local key = rowKey(row)
        live[key] = true
        local at = store[key]
        if at and not placed[at] then placed[at] = row; top = math.max(top, at) else loose[#loose + 1] = row end
    end
    local at = 1
    for _, row in ipairs(loose) do
        while placed[at] do at = at + 1 end
        placed[at] = row; store[rowKey(row)] = at; top = math.max(top, at)
    end
    if #rows >= (self.snapshot.total or 0) then
        for key in pairs(store) do if not live[key] then store[key] = nil end end
    end
    self.arranged, self.arrangedTotal = placed, math.max(top, self.snapshot.capacity or 0)
end

-- Cursor-following icon for stored items being rearranged (they have no real cursor item).
local holder
function Inv:HoldIcon(tile)
    if not holder then
        holder = CreateFrame("Frame", nil, UIParent)
        holder:SetSize(37, 37); holder:SetFrameStrata("TOOLTIP"); holder:Hide()
        holder.icon = holder:CreateTexture(nil, "OVERLAY"); holder.icon:SetAllPoints()
        holder:SetScript("OnUpdate", function(f)
            local x, y = GetCursorPosition()
            local scale = UIParent:GetEffectiveScale()
            f:ClearAllPoints(); f:SetPoint("CENTER", UIParent, "BOTTOMLEFT", x / scale, y / scale)
        end)
    end
    local art = _G[tile:GetName() .. "IconTexture"]
    -- Match the tile's on-screen icon size (the window may be scaled).
    local size = art:GetWidth() * tile:GetEffectiveScale() / UIParent:GetEffectiveScale()
    holder:SetSize(size, size)
    holder.icon:SetTexture(art:GetTexture())
    SetItemButtonDesaturated(tile, true)
    holder:Show()
end

function Inv:DropIcon() if holder then holder:Hide() end end

-- Swaps the item at fromPos with whatever sits at toPos.
function Inv:MoveTo(key, fromPos, toPos)
    local store, other = self:LayoutStore(), self.arranged and self.arranged[toPos]
    if other then store[rowKey(other)] = fromPos end
    store[key] = toPos
    for _, tile in ipairs(self.buttons) do tile.sig = nil end
    self:Render()
    self.refreshAt = GetTime() + 0.25
end

-- Something is on the cursor and was released over a tile (or the window): reposition our own item, or
-- put a foreign one into the first free carried slot (and onto that tile when free placement is on).
function Inv:DropCursor(tile)
    if self.scope ~= 0 or not CursorHasItem() then return end
    if self.view == 1 then self.frame.status:SetText("Switch to the bag view to drop items here."); return end
    local from, manual = self.dragFrom, self:ManualActive()
    self.dragFrom = nil
    if from then
        ClearCursor()
        if manual and tile and tile.pos and tile.pos ~= from.pos then self:MoveTo(from.key, from.pos, tile.pos) end
        return
    end
    for bag = 0, NUM_BAG_SLOTS or 4 do
        for slot = 1, GetContainerNumSlots(bag) do
            if not GetContainerItemInfo(bag, slot) then
                if manual and tile and tile.pos and not (self.arranged and self.arranged[tile.pos]) then
                    self:LayoutStore()["p" .. bag .. ":" .. slot] = tile.pos
                end
                PickupContainerItem(bag, slot)
                self.refreshAt = GetTime() + 0.25
                return
            end
        end
    end
    self.frame.status:SetText("Your bags are full.")
end

function Inv:Flush()
    local queued = self.queued
    if queued and not self.pending then
        self.queued = nil
        self:Request(ACTION.browse, nil, nil, queued.offset)
    end
end

function Inv:Show()
    self.frame:Show(); self.frame:Raise()
    if self.scope == 2 then self:GuildOpen(); return end
    if self.scope == 1 then
        if self.snaps[1].active then self:Page(0) else self:Request(ACTION.activate) end
    elseif not self.active then self:Request(ACTION.activate)
    elseif self.view == 1 then self:Page(0)
    else self:Refresh() end
end

function Inv:Toggle()
    if self.frame:IsShown() then self.frame:Hide() else self:Show() end
end

-- Switches the bag window between carried items (0) and the account bank (1).
function Inv:SetView(view)
    if self.scope ~= 0 or self.view == view then return end
    self.view = view
    self.snapshot = self.snaps[view]
    self.targetOffset, self.dragFrom = nil, nil
    -- A view switch redraws every tile: cached tiles would otherwise keep state from the other view.
    self:DropIcon()
    for _, b in ipairs(self.buttons) do b.sig = nil; SetItemButtonDesaturated(b, false) end
    if not self:HasCategory(self.category) then self.category = 0 end
    self:Layout()
    self:LayoutTabs()
    self:Render()
    if view == 1 and not self.snaps[1].active then self:Request(ACTION.activate) else self:Page(0) end
end

local function fullLink(row)
    local _, _, _, color = GetItemQualityColor(row.quality)
    return "|c" .. (color or "ffffffff") .. "|H" .. row.link .. "|h[" .. row.name .. "]|h|r"
end

local function tooltip(self)
    local row = self.row
    if not row then return end
    local hints, bank = db().hints, self.window.view == 1
    GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
    if row.guild then
        GameTooltip:SetGuildBankItem(row.guild.tab, row.guild.slot)
        GameTooltip:Show()
        return
    end
    if row.digital then
        GameTooltip:SetHyperlink(row.link)
        if row.bound then GameTooltip:AddLine(ITEM_SOULBOUND or "Soulbound", 1, 1, 1) end
        if row.maxDurability > 0 then
            GameTooltip:AddLine("Durability " .. row.durability .. " / " .. row.maxDurability, 0.8, 0.8, 0.8)
        end
        GameTooltip:AddLine(bank and "Account bank" or "Digital storage", 0.6, 0.6, 0.6)
        if row.tag ~= "" then GameTooltip:AddLine("Tag: " .. row.tag, 0.4, 0.8, 1) end
        if row.split then GameTooltip:AddLine("Split stack: stays separate until you press Stack", 1, 0.82, 0, true) end
        if hints then
            if bank then
                GameTooltip:AddLine("Left-click: move to your bags   Alt-click: favorite", 0.8, 0.8, 0.8, true)
            else
                GameTooltip:AddLine("Right-click: use, equip or sell at a merchant", 0.8, 0.8, 0.8, true)
                GameTooltip:AddLine("Left-click: withdraw   Alt-click: favorite", 0.8, 0.8, 0.8, true)
            end
            if row.count > 1 then GameTooltip:AddLine("Shift-click: split stack", 0.8, 0.8, 0.8) end
        end
    else
        GameTooltip:SetBagItem(row.bag, row.slot)
        GameTooltip:AddLine("Carried action slot", 0.6, 0.6, 0.6)
    end
    GameTooltip:Show()
end

local function carriedSlots()
    local found = {}
    for bag = 0, NUM_BAG_SLOTS or 4 do
        for slot = 1, GetContainerNumSlots(bag) do
            local link = GetContainerItemLink(bag, slot)
            if link then found[bag .. ":" .. slot] = tonumber(link:match("item:(%d+)")) end
        end
    end
    return found
end

-- Run fn(bag, slot) once the row is a carried item: immediately, or after withdrawing it from storage.
function Inv:Physical(row, fn)
    if not row.digital then fn(row.bag, row.slot); return end
    self.followUp = { before = carriedSlots(), entry = row.entry, fn = fn, expires = GetTime() + 10 }
    self:Request(ACTION.withdraw, row)
    if not self.pending then self.followUp = nil end
end

function Inv:RunFollowUp()
    local follow = self.followUp
    if not follow then return end
    if GetTime() > follow.expires then self.followUp = nil; return end
    for key, entry in pairs(carriedSlots()) do
        if entry == follow.entry and not follow.before[key] then
            local bag, slot = key:match("(%d+):(%d+)")
            self.followUp = nil
            follow.fn(tonumber(bag), tonumber(slot))
            return
        end
    end
end

local popupWindow, popupRow
local menuFrame
function Inv:OpenMenu(tile)
    local I, row = self, tile.row
    if not row then return end
    menuFrame = menuFrame or CreateFrame("Frame", "EsteriaInventoryMenu", UIParent, "UIDropDownMenuTemplate")
    local function entry(level, label, func, extra)
        local info = extra or {}
        info.text, info.func, info.notCheckable = label, func, 1
        UIDropDownMenu_AddButton(info, level)
    end
    local bank = I.view == 1
    UIDropDownMenu_Initialize(menuFrame, function(_, level)
        entry(level, row.name, nil, { isTitle = 1 })
        local equipment = row.inventoryType > 0
        if bank then
            entry(level, "Move to bags", function() I:Request(ACTION.withdraw, row) end)
        else
            if MerchantFrame and MerchantFrame:IsShown() then
                entry(level, "Sell", function() I:Request(ACTION.sell, row) end)
            end
            entry(level, equipment and "Equip" or "Use", function()
                if row.digital then I:Request(equipment and ACTION.equip or ACTION.use, row)
                else ContainerFrameItemButton_OnClick(tile, "RightButton") end
            end)
            if State.atBanker or State.unlocked then
                entry(level, "Send to bank", function() I:Request(ACTION.deposit, row, nil, nil, nil, 1) end)
            end
            if row.digital then
                entry(level, "Withdraw", function() I:Request(ACTION.withdraw, row) end)
            end
        end
        if row.digital then
            entry(level, row.favorite and "Remove favorite" or "Favorite", function() I:Request(ACTION.favorite, row) end)
            if row.count > 1 then
                entry(level, "Split stack...", function()
                    OpenStackSplitFrame(row.count - 1, tile, "BOTTOMLEFT", "TOPLEFT")
                end)
            end
            entry(level, "Set tag...", function()
                popupWindow, popupRow = I, row
                StaticPopup_Show("ESTERIA_INVENTORY_TAG", row.name)
            end)
        end
        if not bank then
            if equipment and row.quality >= 2 and row.quality <= 4 then
                entry(level, "Disenchant", function()
                    I:Physical(row, function(bag, slot) CastSpellByName("Disenchant"); UseContainerItem(bag, slot) end)
                end)
            end
            if equipment then
                entry(level, "Socket gems...", function()
                    I:Physical(row, function(bag, slot)
                        if LoadAddOn then LoadAddOn("Blizzard_ItemSocketingUI") end
                        SocketContainerItem(bag, slot)
                    end)
                end)
            end
        end
        entry(level, "Link in chat", function() ChatEdit_InsertLink(fullLink(row)) end)
        if not bank then
            entry(level, "|cffff4040Destroy...|r", function()
                popupWindow, popupRow = I, row
                StaticPopup_Show("ESTERIA_INVENTORY_DESTROY", row.name)
            end)
        end
        entry(level, "Cancel", nil)
    end, "MENU")
    ToggleDropDownMenu(1, nil, menuFrame, "cursor", 0, 0)
end

-- Guild bank tiles: left-click picks up/drops, right-click withdraws, shift splits (stock behavior).
local function guildClick(self, mouse)
    local I, row = self.window, self.row
    if not row then
        -- An empty tile takes a held item into the first free slot of the tab.
        if CursorHasItem and CursorHasItem() and I.guildFree then PickupGuildBankItem(GetCurrentGuildBankTab(), I.guildFree) end
        return
    end
    local g = row.guild
    if IsControlKeyDown() then DressUpItemLink(fullLink(row)); return end
    if IsShiftKeyDown() and ChatEdit_InsertLink(fullLink(row)) then return end
    if mouse == "RightButton" then AutoStoreGuildBankItem(g.tab, g.slot)
    elseif IsShiftKeyDown() and row.count > 1 then OpenStackSplitFrame(row.count, self, "BOTTOMLEFT", "TOPLEFT")
    else PickupGuildBankItem(g.tab, g.slot) end
end

local function click(self, mouse)
    local I, row = self.window, self.row
    if I.scope == 2 then return guildClick(self, mouse) end
    if CursorHasItem() and (not row or row.digital or I.dragFrom) then I:DropCursor(self); return end
    if not row then return end
    if IsControlKeyDown() then DressUpItemLink(fullLink(row)); return end
    if IsAltKeyDown() and row.digital then I:Request(ACTION.favorite, row); return end
    if IsShiftKeyDown() and ChatEdit_InsertLink(fullLink(row)) then return end
    if mouse == "RightButton" and db().contextMenu then I:OpenMenu(self); return end
    if I.view == 1 then
        if IsShiftKeyDown() and row.count > 1 then
            OpenStackSplitFrame(row.count - 1, self, "BOTTOMLEFT", "TOPLEFT")
        else I:Request(ACTION.withdraw, row) end
        return
    end
    -- With the banker's window open, right-click sends an item to the bank like the stock UI.
    if mouse == "RightButton" and State.atGuildBank and Windows[2].frame:IsShown() then
        -- With the guild bank open, right-click deposits into the selected guild tab like the stock UI.
        I:Physical(row, function(bag, slot) UseContainerItem(bag, slot) end)
        return
    end
    local teller = Windows[1]
    if mouse == "RightButton" and State.atBanker and teller.frame:IsShown() then
        I:Request(ACTION.deposit, row, nil, nil, nil, 1)
        return
    end
    if row.digital then
        if IsShiftKeyDown() and row.count > 1 then
            OpenStackSplitFrame(row.count - 1, self, "BOTTOMLEFT", "TOPLEFT")
        elseif mouse == "RightButton" then
            local action = MerchantFrame and MerchantFrame:IsShown() and ACTION.sell
                or (row.inventoryType > 0 and ACTION.equip or ACTION.use)
            I:Request(action, row)
        else I:Request(ACTION.withdraw, row) end
    else
        ContainerFrameItemButton_OnClick(self, mouse)
        I.refreshAt = GetTime() + 0.25
    end
end

function Inv:Render()
    local snapshot, frame, settings = self.snapshot, self.frame, db()
    local page = self.page or DEFAULT_PAGE
    local bank = self.view == 1
    local guild = self.scope == 2
    frame.title:SetText(guild and ("Esteria Guild Bank" .. (self.guildTabName and (" - " .. self.guildTabName) or ""))
        or (bank and (self.scope == 1 and "Esteria Bank" or "Esteria Bank (Remote)") or "Esteria Inventory"))
    local locked = bank and not State.unlocked and not State.atBanker
    if locked then frame.lock:Show() else frame.lock:Hide() end
    if frame.depositAll then if bank then frame.depositAll:Hide() else frame.depositAll:Show() end end
    if self.scope == 0 and frame.bankTab then
        if bank then frame.bankTab:LockHighlight() else frame.bankTab:UnlockHighlight() end
    end
    frame.capacity:SetText((snapshot.used or 0) .. " / " .. (snapshot.capacity or 0))
    local ratio = snapshot.capacity and snapshot.capacity > 0 and snapshot.used / snapshot.capacity or 0
    frame.bar:SetValue(math.min(ratio, 1))
    frame.percent:SetText(string.format("%.1f%% full", ratio * 100))
    local manual = self:ManualActive()
    local scrolling = not manual and self:Scrolling()
    local offset = snapshot.offset or 0
    if manual then
        self:Arrange()
        offset = math.min(self.manualOffset or 0, self:MaxOffset())
        self.manualOffset = offset
    elseif scrolling then
        offset = math.min(self.viewOffset or 0, self:MaxOffset())
        self.viewOffset = offset
    end
    -- Scrolling shows a slice of the fetched block; rows outside it stay blank until the next block arrives.
    local first = scrolling and offset - (snapshot.offset or 0) or 0
    local step, total = self:Step(), self:Total()
    local pages = math.max(1, math.ceil(math.max(total - page, 0) / step) + 1)
    frame.page:SetText((math.floor(offset / step) + 1) .. " / " .. pages)
    local bar = frame.scroll
    if bar and self:Scrolling() then
        self.syncingScroll = true
        bar:SetMinMaxValues(0, math.max(0, self:MaxOffset() / step)); bar:SetValue(offset / step)
        self.syncingScroll = nil
    end
    frame.expand.label:SetText(guild and "Expand Tab" or (snapshot.upgrade and snapshot.upgrade > 0
        and (bank and "Expand Bank" or "Expand Storage") or "At capacity limit"))
    if guild then if self.canExpand then frame.expand:Show() else frame.expand:Hide() end end
    frame.sort.label:SetText((db().autoSort or guild) and ("Sort: " .. sorts[self.sort + 1]) or ("Sort now: " .. sorts[self.sort + 1]))
    for index = 1, page do
        local b = self.buttons[index]
        local row
        if manual then row = self.arranged[offset + index] else row = snapshot.rows[first + index] end
        b.row, b.pos = row, manual and offset + index or nil
        if row then row.tag = row.tag or "" end
        local sig = row and row.digital and (row.guid .. ":" .. row.count .. ":" .. row.quality
            .. ":" .. (row.favorite and 1 or 0) .. ":" .. row.tag .. ":" .. (row.split and 1 or 0)) or nil
        if sig and sig == b.sig then
            -- Stored stack unchanged since last render: keep icon, text and border as they are.
        elseif row then
            b:GetParent():SetID(row.bag); b:SetID(row.slot)
            b.hasItem, b.readable = 1, nil
            SetItemButtonTexture(b, row.texture or ("Interface\\Icons\\" .. row.icon))
            SetItemButtonCount(b, row.count)
            b.name:SetText(row.name)
            b.favorite:SetText(row.favorite and "*" or "")
            local r, g, blue = GetItemQualityColor(row.quality)
            local lockedItem = false
            if not row.digital then
                local _, _, isLocked = GetContainerItemInfo(row.bag, row.slot)
                lockedItem = isLocked
            end
            if row.guild then lockedItem = row.locked end
            SetItemButtonDesaturated(b, lockedItem)
            b.name:SetTextColor(r, g, blue)
            b.tagMark:SetText(row.tag ~= "" and "#" or "")
            if settings.qualityBorders and row.quality > 1 then b:SetBackdropBorderColor(r, g, blue, 1)
            else b:SetBackdropBorderColor(0.4, 0.4, 0.4, 0) end
            local start, duration, enable = GetItemCooldown(row.entry)
            CooldownFrame_SetTimer(_G[b:GetName() .. "Cooldown"], start or 0, duration or 0, enable or 0)
            if not row.digital then ContainerFrame_UpdateCooldown(row.bag, b) end
            b:Enable()
            b.sig = sig
        else
            b.sig = nil
            SetItemButtonTexture(b, nil); SetItemButtonCount(b, 0)
            SetItemButtonDesaturated(b, false); _G[b:GetName() .. "IconTexture"]:Hide()
            b.hasItem = nil; b.name:SetText(""); b.favorite:SetText("")
            b.tagMark:SetText("")
            b:SetBackdropBorderColor(0.25, 0.25, 0.25, 0)
            CooldownFrame_SetTimer(_G[b:GetName() .. "Cooldown"], 0, 0, 0)
            -- Guild and bag empty tiles stay live so a held item can be dropped on them.
            if guild or self.view == 0 then b:Enable() else b:Disable() end
        end
        local quest = _G[b:GetName() .. "IconQuestTexture"]
        if quest then quest:Hide() end
        if sig ~= b.tipSig or not sig then
            b.tipSig = sig
            if GameTooltip:IsOwned(b) then tooltip(b) end
        end
    end
    for category, tab in pairs(frame.tabs) do
        if category == self.category then tab:LockHighlight() else tab:UnlockHighlight() end
    end
    self:UpdateMoney()
end

-- Gold plus any currencies the user ticked in the Currencies options page.
function Inv:UpdateMoney()
    local settings, frame = db(), self.frame
    if self.scope == 2 then
        frame.gold:SetText("Guild bank: " .. GetCoinTextureString(GetGuildBankMoney and GetGuildBankMoney() or 0))
        frame.currency:SetText(self.guildInfo or "")
        return
    end
    frame.gold:SetText(settings.showGold and GetCoinTextureString(GetMoney()) or "")
    local parts = {}
    if next(settings.currencies) and GetCurrencyListSize then
        for index = 1, GetCurrencyListSize() do
            local name, header, _, _, _, count, _, icon = GetCurrencyListInfo(index)
            if not header and settings.currencies[name] then
                parts[#parts + 1] = "|T" .. (icon or "") .. ":14|t " .. (count or 0)
            end
        end
    end
    frame.currency:SetText(table.concat(parts, "   "))
end

-- The guild window always shows its full footer; the others can drop the help line and the capacity bar.
function Inv:FooterParts()
    local settings, guild = db(), self.scope == 2
    return guild or settings.showStatus, guild or settings.showBar,
        guild or settings.showGold or next(settings.currencies) ~= nil
end

function Inv:FooterHeight()
    local status, bar, money = self:FooterParts()
    return 12 + (status and 34 or 0) + 30 + (bar and 20 or 0) + (money and 22 or 0)
end

-- Footer rows stack upwards from the bottom edge: help line, buttons, capacity bar, then gold and currencies.
function Inv:LayoutFooter()
    local f = self.frame
    if not f.footerRow then return end
    local status, bar = self:FooterParts()
    local base = status and 46 or 12
    for _, item in ipairs(f.footerRow) do
        item[1]:ClearAllPoints(); item[1]:SetPoint(item[2], item[3], base + item[4])
    end
    local barY = base + 30
    f.bar:ClearAllPoints(); f.bar:SetPoint("BOTTOMLEFT", 26, barY); f.bar:SetPoint("BOTTOMRIGHT", -100, barY)
    f.percent:ClearAllPoints(); f.percent:SetPoint("BOTTOMRIGHT", -26, barY)
    if bar then f.bar:Show(); f.percent:Show() else f.bar:Hide(); f.percent:Hide() end
    local moneyY = bar and barY + 20 or barY
    f.gold:ClearAllPoints(); f.gold:SetPoint("BOTTOMRIGHT", -26, moneyY)
    f.currency:ClearAllPoints(); f.currency:SetPoint("BOTTOMLEFT", 26, moneyY)
    if status then f.status:Show() else f.status:Hide() end
end

-- Draws the Blizzard bag bar (DragonUI) as one button: our icon on the backpack, bag slots and fold arrow gone.
function Inv:SkinBagButtons()
    if not db().bagButton or not (enabled() or pending()) then return end
    for _, slot in ipairs({ CharacterBag0Slot, CharacterBag1Slot, CharacterBag2Slot, CharacterBag3Slot,
        _G.DragonUI_BagsToggle }) do
        if slot then
            if not slot.esteriaHooked then
                slot.esteriaHooked = true
                slot:HookScript("OnShow", function(self) if db().bagButton and (enabled() or pending()) then self:Hide() end end)
            end
            if slot:IsShown() then slot:Hide() end
        end
    end
    local icon = _G.MainMenuBarBackpackButtonIconTexture
    if icon and icon:GetTexture() ~= ART .. "MenuIcon" then
        icon:SetTexture(ART .. "MenuIcon"); icon:SetTexCoord(0, 1, 0, 1)
    end
end

function Inv:InstallHooks()
    local I = self
    local functions = { ToggleBackpack = "toggle", ToggleAllBags = "toggle", ToggleBag = "toggle",
        OpenBackpack = "open", OpenAllBags = "open", OpenBag = "open",
        CloseBackpack = "close", CloseAllBags = "close", CloseBag = "close" }
    self.originals, self.wrappers = self.originals or {}, self.wrappers or {}
    for name, action in pairs(functions) do
        local original = _G[name]
        -- Re-run to re-wrap: other bag add-ons (DragonUI Bagster) replace these globals after login.
        if original and original ~= self.wrappers[name] then
            self.originals[name] = original
            local method = action
            self.wrappers[name] = function(bag, ...)
                if type(bag) == "number" and (bag < 0 or bag > 4) then return original(bag, ...) end
                if pending() then return end
                if not enabled() then return original(bag, ...) end
                if method == "toggle" then I:Toggle()
                elseif method == "open" then I:Show()
                else I.frame:Hide() end
            end
            _G[name] = self.wrappers[name]
        end
    end
    -- The backpack tooltip reports Esteria storage, not the 16-slot backpack. Bar add-ons may replace OnEnter later,
    -- so the wrapper is re-applied whenever it is no longer the current script.
    local backpack = _G.MainMenuBarBackpackButton
    if backpack and (not backpack.esteriaEnter or backpack:GetScript("OnEnter") ~= backpack.esteriaEnter) then
        local previous = backpack:GetScript("OnEnter")
        backpack.esteriaEnter = function(owner, ...)
            if previous then previous(owner, ...) end
            local snap = Windows[0] and Windows[0].snaps[0]
            if not (enabled() and snap and snap.capacity) then return end
            local key = GetBindingKey("OPENALLBAGS") or GetBindingKey("TOGGLEBACKPACK")
            GameTooltip:SetOwner(owner, "ANCHOR_LEFT")
            GameTooltip:SetText("Esteria Inventory" .. (key and (" |cffffd100(" .. key .. ")|r") or ""), 1, 1, 1)
            GameTooltip:AddLine((snap.used or 0) .. " / " .. snap.capacity .. " slots used", 1, 0.82, 0)
            GameTooltip:AddLine(math.max(0, snap.capacity - (snap.used or 0)) .. " free", 0.8, 0.8, 0.8)
            GameTooltip:Show()
        end
        backpack:SetScript("OnEnter", backpack.esteriaEnter)
    end
    if self.hooked then return end
    self.hooked = true
    -- The existing large-bag addon retains its implementation for /inventory legacy.
    if EsteriaSatchel then
        -- Satchel polls its hook installer every half-second. Keep the negotiated inventory
        -- wrappers in place; their captured originals still serve /inventory legacy.
        local install = EsteriaSatchel.InstallHooks
        if install then
            EsteriaSatchel.InstallHooks = function(satchel, ...)
                if I.hooked then return end
                return install(satchel, ...)
            end
        end
        for _, name in ipairs({ "Show", "ShowAll", "Toggle" }) do
            local original = EsteriaSatchel[name]
            if original then
                EsteriaSatchel[name] = function(satchel, bag, ...)
                    if enabled() and not (type(bag) == "number" and bag > 4) then return end
                    return original(satchel, bag, ...)
                end
            end
        end
    end
end

-- Tile background for the chosen look. Square slot art does not stretch to list rows, so rows use the flat look.
function Inv:StyleTile(b)
    local settings, st = db(), style()
    local look = settings.tileLook
    if look == "framed" and settings.viewMode == "list" then look = "flat" end
    if look == "framed" then b.slotArt:Show() else b.slotArt:Hide() end
    if look == "flat" then b.flat:Show(); tint(b.flat, st.panel, 0.9) else b.flat:Hide() end
    b.name:SetFont(FONT, settings.textSize)
end

-- One pooled item tile; created on demand when a larger window or tile limit needs more of them.
function Inv:MakeTile(index)
    local I, frame = self, self.frame
    local proxy = CreateFrame("Frame", self.prefix .. "ContainerFrame" .. index, frame)
    proxy:Show()
    local name = self.prefix .. "ContainerFrameItem" .. index
    local b = CreateFrame("Button", name, proxy, "ContainerFrameItemButtonTemplate")
    b.window, b.index = I, index
    b:ClearAllPoints(); b:SetAllPoints(proxy); b:SetBackdrop(tileBorder); b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local slotArt = b:CreateTexture(nil, "BACKGROUND")
    slotArt:SetTexture(ART .. "SlotBackdrop"); slotArt:SetAllPoints(b)
    b.slotArt, b.flat = slotArt, solid(b, "BACKGROUND")
    b.flat:SetPoint("TOPLEFT", 1, -1); b.flat:SetPoint("BOTTOMRIGHT", -1, 1)
    b:RegisterForDrag("LeftButton"); b:SetNormalTexture(""); b:SetPushedTexture("")
    b:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight", "ADD")
    -- The bag template refreshes its tooltip from bag/slot IDs, which blanks digital rows after ~0.2s.
    b.UpdateTooltip = nil
    local icon = _G[name .. "IconTexture"]
    icon:ClearAllPoints(); icon:SetPoint("CENTER", b, "CENTER", 0, 0)
    -- Not "count": the stock SetItemButtonCount stores the stack size in button.count.
    b.countText = _G[name .. "Count"]
    b.name = text(b, "GameFontHighlightSmall", "", "BOTTOM", 0, 4)
    b.name:SetHeight(26); b.name:SetJustifyH("CENTER"); b.name:Hide()
    b.favorite = text(b, "GameFontNormal", "", "TOPLEFT", 4, -3)
    b.tagMark = text(b, "GameFontNormalSmall", "", "TOPRIGHT", -4, -3)
    b.SplitStack = function(tile, amount)
        if not tile.row then return end
        if tile.row.guild then SplitGuildBankItem(tile.row.guild.tab, tile.row.guild.slot, amount)
        else I:Request(ACTION.split, tile.row, amount) end
    end
    b:SetScript("OnEnter", tooltip)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    b:SetScript("OnClick", click)
    b:SetScript("OnDragStart", function(tile)
        if not tile.row then return end
        if tile.row.guild then PickupGuildBankItem(tile.row.guild.tab, tile.row.guild.slot); return end
        if I:ManualActive() then I.dragFrom = { key = rowKey(tile.row), pos = tile.pos, internal = tile.row.digital, tile = tile } end
        if tile.row.digital and I.dragFrom then
            -- Stored items can't ride the cursor: a floating icon stands in, and OnDragStop drops it on the tile under the mouse.
            I:HoldIcon(tile)
        elseif tile.row.digital then
            I:Request(ACTION.withdraw, tile.row)
            frame.status:SetText(I.view == 1 and "Moving item to your bags."
                or "Withdrawing item. Drag its carried icon after the inventory updates.")
        else ContainerFrameItemButton_OnDrag(tile) end
    end)
    b:SetScript("OnDragStop", function()
        local from = I.dragFrom
        if not (from and from.internal) then return end
        I.dragFrom = nil
        I:DropIcon()
        SetItemButtonDesaturated(from.tile, false)
        local target = GetMouseFocus()
        if target and target.window == I and target.pos and target.pos ~= from.pos then I:MoveTo(from.key, from.pos, target.pos) end
    end)
    b:SetScript("OnReceiveDrag", function(tile)
        if I.scope == 2 then guildClick(tile, "LeftButton")
        elseif I.dragFrom or not tile.row or tile.row.digital then I:DropCursor(tile)
        else ContainerFrameItemButton_OnClick(tile, "LeftButton") end
    end)
    -- Bag templates can inherit hidden="true"; the custom container owns their visibility.
    b:Show(); self.buttons[index] = b
    self:StyleTile(b)
end

function Inv:GridTop() return self.scope == 2 and 165 or 137 end

-- Fit as many tiles as the window holds (bounded by Max Tiles and what the server can send), then
-- stretch them to span the width.
function Inv:Layout()
    local f, W, H, settings = self.frame, self.w, self.h, db()
    local gap = settings.gap
    f:SetSize(W, H)
    local top = self:GridTop()
    local availW, availH = W - 52, H - top - self:FooterHeight()
    local list = settings.viewMode == "list"
    local cellW, cellH, cols
    if list then
        -- List rows: one item per line, in as many columns of at least 240px as the window holds.
        cellH = math.floor(30 * settings.tileScale + 0.5)
        cols = math.max(1, math.floor((availW + gap) / (240 + gap)))
        cellW = (availW - (cols - 1) * gap) / cols
    else
        local tile = BASE_TILE * settings.tileScale
        cols = math.max(1, math.floor((availW + gap) / (tile + gap)))
        cellW = (availW - (cols - 1) * gap) / cols
        cellH = cellW
    end
    local rows = math.max(1, math.floor((availH + gap) / (cellH + gap)))
    local page = math.max(1, math.min(cols * rows, settings.maxTiles, self.cap or MAX_PAGE, MAX_PAGE))
    cols = math.min(cols, page)
    self.cols = cols
    local left = 26 + (availW - (cols * cellW + (cols - 1) * gap)) / 2
    for index = #self.buttons + 1, page do self:MakeTile(index) end
    local names = list or (settings.showNames and cellW >= 64)
    local icon = list and (cellH - 6) or math.max(12, math.min(cellW * (names and 0.5 or 0.65), 56))
    for index, b in ipairs(self.buttons) do
        local proxy = b:GetParent()
        if index <= page then
            proxy:SetSize(cellW, cellH); proxy:ClearAllPoints()
            proxy:SetPoint("TOPLEFT", left + ((index - 1) % cols) * (cellW + gap),
                -top - math.floor((index - 1) / cols) * (cellH + gap))
            local art = _G[b:GetName() .. "IconTexture"]
            art:SetSize(icon, icon); art:ClearAllPoints()
            b.name:ClearAllPoints(); b.countText:ClearAllPoints()
            if list then
                art:SetPoint("LEFT", b, "LEFT", 3, 0)
                b.name:SetPoint("LEFT", art, "RIGHT", 8, 0); b.name:SetPoint("RIGHT", b, "RIGHT", -44, 0)
                b.name:SetHeight(cellH); b.name:SetJustifyH("LEFT")
                b.countText:SetPoint("RIGHT", b, "RIGHT", -8, 0)
            else
                art:SetPoint("CENTER", b, "CENTER", 0, names and 8 or 0)
                b.name:SetPoint("BOTTOM", 0, 4); b.name:SetWidth(cellW - 6); b.name:SetHeight(26)
                b.name:SetJustifyH("CENTER")
                b.countText:SetPoint("BOTTOMRIGHT", art, "BOTTOMRIGHT", 4, -3)
            end
            if names then b.name:Show() else b.name:Hide() end
            proxy:Show()
        else proxy:Hide() end
    end
    self:LayoutFooter()
    f.status:SetWidth(W - 52)
    if f.scroll then
        f.scroll:ClearAllPoints()
        f.scroll:SetPoint("TOPRIGHT", -9, -top); f.scroll:SetPoint("BOTTOMRIGHT", -9, self:FooterHeight())
        if self:Scrolling() then f.scroll:Show() else f.scroll:Hide() end
    end
    if self.page and self.page ~= page and (self.active or self.view == 1) then self.pageAt = GetTime() + 0.15 end
    self.page = page
end

-- Guild bank tabs replace the category buttons; unviewable tabs are disabled.
function Inv:LayoutGuildTabs()
    local I, frame = self, self.frame
    for _, tab in pairs(frame.tabs) do tab:Hide() end
    if type(frame.guildTabs) ~= "table" then frame.guildTabs = {} end
    local numTabs = GetNumGuildBankTabs and GetNumGuildBankTabs() or 0
    local current, left = GetCurrentGuildBankTab and GetCurrentGuildBankTab() or 1, 26
    for index = 1, 6 do
        local tab = frame.guildTabs[index]
        if index <= numTabs then
            local name, _, viewable = GetGuildBankTabInfo(index)
            if not tab then
                tab = button(frame, nil, "", 84, "TOPLEFT", left, -99, function()
                    SetCurrentGuildBankTab(index); QueryGuildBankTab(index)
                    I:GuildRefresh(0)
                    I:SetMode(I.mode or "items")
                end)
                frame.guildTabs[index] = tab
            end
            tab:ClearAllPoints(); tab:SetPoint("TOPLEFT", left, -99)
            tab:SetText(string.sub(name or ("Tab " .. index), 1, 11))
            if viewable then tab:Enable() else tab:Disable() end
            if index == current then tab:LockHighlight() else tab:UnlockHighlight() end
            tab:Show()
            left = left + 89
        elseif tab then tab:Hide() end
    end
    local buy = frame.buyTab
    if type(buy) == "table" then
        if self.canBuyTab and numTabs < 6 then
            buy:ClearAllPoints(); buy:SetPoint("TOPLEFT", left, -99); buy:Show()
        else buy:Hide() end
    end
end

function Inv:HasCategory(category)
    if category == EARS_CATEGORY then return self.snapshot.specialTabs and self.snapshot.hasEars end
    if category == KEYRING_CATEGORY then return self.snapshot.specialTabs and self.snapshot.hasKeys end
    return true
end

function Inv:LayoutTabs()
    if self.scope == 2 then self:LayoutGuildTabs(); return end
    local settings = db()
    local hidden, left = settings.hiddenCategories, 26
    local bankTab = self.scope == 0 and self.frame.bankTab
    local totalWidth = bankTab and settings.showBankTab and 74 or 0
    for category = 0, #categories - 1 do
        if category == 0 or (not hidden[category] and self:HasCategory(category)) then
            totalWidth = totalWidth + tabWidths[category + 1] + 5
        end
    end
    local scale = math.min(1, (self.w - 52) / math.max(1, totalWidth))
    for category = 0, #categories - 1 do
        local tab = self.frame.tabs[category]
        if category == 0 or (not hidden[category] and self:HasCategory(category)) then
            local width = tabWidths[category + 1] * scale
            tab:SetWidth(width)
            tab:ClearAllPoints(); tab:SetPoint("TOPLEFT", left, -99); tab:Show()
            left = left + width + 5 * scale
        else tab:Hide() end
    end
    if bankTab then
        if settings.showBankTab then
            bankTab:SetWidth(64 * scale)
            bankTab:ClearAllPoints(); bankTab:SetPoint("TOPLEFT", left + 10 * scale, -99); bankTab:Show()
        else
            bankTab:Hide()
            if self.view == 1 then self:SetView(0) end
        end
    end
end

-- Re-applies every setting after a change.
function Inv:Apply()
    local settings = db()
    if settings.hiddenCategories[self.category] then self.category = 0 end
    local anySort = false
    for s = 1, #sorts do if not settings.hiddenSorts[s] then anySort = true end end
    if not anySort then settings.hiddenSorts[1] = nil end
    if settings.hiddenSorts[self.sort + 1] then
        for s = 1, #sorts do if not settings.hiddenSorts[s] then self.sort = s - 1; break end end
    end
    local frame = self.frame
    if not frame then return end
    if style().classic then frame:SetBackdropColor(1, 1, 1, settings.alpha) end
    if settings.lockSize then frame.grip:Hide() else frame.grip:Show() end
    self:Layout(); self:LayoutTabs()
    if self.scope == 0 then self:SkinBagButtons() end
    for _, b in ipairs(self.buttons) do b.sig = nil; self:StyleTile(b) end
    if frame:IsShown() then
        frame:SetScale(math.min(settings.scale, (UIParent:GetHeight() - 35) / self.h,
            (UIParent:GetWidth() - 35) / self.w))
    end
    self:Render()
    -- Toggling auto sort changes what the server must send.
    if self.sortMode ~= settings.autoSort then
        self.sortMode = settings.autoSort
        if frame:IsShown() and self.active then self.pageAt = GetTime() + 0.15 end
    end
end

function Inv:ResetGeometry()
    self.w, self.h, self.cap = WIDTH, HEIGHT, nil
    self.frame:ClearAllPoints()
    if self.scope == 0 then self.frame:SetPoint("CENTER")
    else self.frame:SetPoint("TOPRIGHT", Windows[0].frame, "TOPLEFT", -10, 0) end
end

local function resetAll()
    local d = db()
    for key, value in pairs(DEFAULTS) do d[key] = value end
    d.hiddenCategories, d.hiddenSorts = {}, {}
    for _, keys in pairs(GEOMETRY) do d[keys[1]], d[keys[2]] = nil, nil end
    for scope = 0, 2 do if Windows[scope] then Windows[scope]:ResetGeometry() end end
    applyAll()
    if Windows[0].refreshOptions then Windows[0].refreshOptions() end
end

function Inv:Create()
    local I = self
    local frame = CreateFrame("Frame", self.prefix .. "Frame", UIParent)
    I.frame = frame
    local settings = db()
    local sizeKey, positionKey = GEOMETRY[self.scope][1], GEOMETRY[self.scope][2]
    local size = settings[sizeKey]
    I.w = size and tonumber(size[1]) and math.max(MIN_W, size[1]) or WIDTH
    I.h = size and tonumber(size[2]) and math.max(MIN_H, size[2]) or HEIGHT
    if settings.remember and self.scope == 0 then I.category, I.sort = settings.lastCategory or 0, settings.lastSort or 0 end
    frame:Hide(); frame:SetSize(I.w, I.h); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:SetMovable(true)
    frame:EnableMouse(true); frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    local skinBg, skinEdges, skinWash, skinRule = solid(frame, "BACKGROUND"), lines(frame), solid(frame, "BORDER"),
        solid(frame, "BORDER")
    skinBg:SetAllPoints()
    skinWash:SetPoint("TOPLEFT", 1, -1); skinWash:SetPoint("TOPRIGHT", -1, -1); skinWash:SetHeight(42)
    skinRule:SetPoint("TOPLEFT", 1, -42); skinRule:SetPoint("TOPRIGHT", -1, -42); skinRule:SetHeight(1)
    frame:SetScript("OnDragStart", function(f) if not db().lockPosition then f:StartMoving() end end)
    frame:SetScript("OnDragStop", function(f)
        f:StopMovingOrSizing()
        local point, _, relativePoint, x, y = f:GetPoint()
        EsteriaInventoryDB[positionKey] = { point, relativePoint, x, y }
    end)
    frame:SetScript("OnShow", function(f)
        f:SetScale(math.min(db().scale, (UIParent:GetHeight() - 35) / I.h, (UIParent:GetWidth() - 35) / I.w))
        sound("igBackPackOpen")
    end)
    frame:SetScript("OnHide", function()
        GameTooltip:Hide(); sound("igBackPackClose")
        -- Closing the banker's window ends the bank interaction like closing the stock bank frame.
        if I.scope == 1 and State.atBanker and CloseBankFrame then CloseBankFrame() end
        if I.scope == 2 and State.atGuildBank then
            State.atGuildBank = false
            if CloseGuildBankFrame then CloseGuildBankFrame() end
        end
    end)
    frame:SetScript("OnReceiveDrag", function() I:DropCursor() end)
    frame:EnableMouseWheel(true)
    frame:SetScript("OnMouseWheel", function(_, delta)
        local rows = I:Scrolling() and db().viewMode == "list" and 3 or 1
        I:Scroll(-delta * I:Step() * rows)
    end)
    local scroll = CreateFrame("Slider", nil, frame)
    scroll:SetWidth(10); scroll:SetOrientation("VERTICAL"); scroll:SetValueStep(1); scroll:Hide()
    local track = solid(scroll, "BACKGROUND"); track:SetAllPoints()
    scroll:SetThumbTexture(WHITE)
    local thumb = scroll:GetThumbTexture(); thumb:SetSize(10, 28)
    scroll:SetScript("OnValueChanged", function(_, value)
        if I.syncingScroll then return end
        I:Scroll(math.floor(value + 0.5) * I:Step() - (I.targetOffset or I:Offset()))
    end)
    frame.scroll = scroll
    local header = frame:CreateTexture(nil, "ARTWORK")
    header:SetTexture("Interface\\DialogFrame\\UI-DialogBox-Header")
    header:SetSize(300, 64); header:SetPoint("TOP", 0, 12)
    frame.title = text(frame, "GameFontNormal", self.scope == 0 and "Esteria Inventory" or "Esteria Bank", "TOP", 0, -2)
    frame.capacity = text(frame, "GameFontHighlightSmall", "Connecting...", "TOPRIGHT", -44, -22)
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -6, -6)
    local options = button(frame, nil, "Options", 66, "TOPLEFT", 24, -18, function()
        local panel = Windows[0].options
        if panel then if panel:IsShown() then panel:Hide() else panel:Show(); panel:Raise() end end
    end)
    options:SetHeight(20)
    local search = CreateFrame("EditBox", self.prefix .. "Search", frame, "InputBoxTemplate")
    search:SetPoint("TOPLEFT", 26, -52); search:SetPoint("TOPRIGHT", -26, -52); search:SetHeight(24)
    search:SetAutoFocus(false); search:SetMaxLetters(80)
    search:SetTextInsets(22, 8, 0, 0)
    local magnifier = search:CreateTexture(nil, "OVERLAY")
    magnifier:SetTexture("Interface\\Common\\UI-Searchbox-Icon")
    magnifier:SetSize(14, 14); magnifier:SetPoint("LEFT", 4, -1)
    local placeholder = text(search, "GameFontDisable", "Search inventory...", "LEFT", 22, 0)
    local searchBg, searchEdges = solid(search, "BACKGROUND"), lines(search)
    searchBg:SetPoint("TOPLEFT", -6, 0); searchBg:SetPoint("BOTTOMRIGHT", 0, 0)
    searchEdges[1]:SetPoint("TOPLEFT", -6, 0); searchEdges[2]:SetPoint("BOTTOMLEFT", -6, 0)
    searchEdges[3]:SetPoint("TOPLEFT", -6, 0); searchEdges[3]:SetPoint("BOTTOMLEFT", -6, 0)
    search:SetScript("OnTextChanged", function(box)
        local value = box:GetText()
        if value == "" then placeholder:Show() else placeholder:Hide() end
        I.search, I.searchAt = value, GetTime() + 0.12
    end)
    search:SetScript("OnEscapePressed", function(box) box:ClearFocus(); frame:Hide() end)
    search:SetScript("OnEnterPressed", function(box) box:ClearFocus(); I.searchAt = GetTime() end)
    frame.tabs = {}
    for category = 0, #categories - 1 do
        frame.tabs[category] = button(frame, nil, categories[category + 1], tabWidths[category + 1],
            "TOPLEFT", 26, -99, function()
                I.category = category
                if I.scope == 0 and category < EARS_CATEGORY then settings.lastCategory = category end
                I:Render(); I:Page(0)
            end)
    end
    if self.scope == 0 then
        frame.bankTab = button(frame, nil, "Bank", 64, "TOPLEFT", 26, -99, function()
            I:SetView(I.view == 1 and 0 or 1)
        end)
    end
    local bar = CreateFrame("StatusBar", nil, frame)
    bar:SetHeight(12); bar:SetPoint("BOTTOMLEFT", 26, 76); bar:SetPoint("BOTTOMRIGHT", -100, 76)
    local barBg, barEdges = solid(bar, "BACKGROUND"), lines(bar)
    barBg:SetAllPoints()
    bar:SetStatusBarTexture(WHITE); bar:SetMinMaxValues(0, 1); bar:SetValue(0)
    frame.bar = bar
    frame.percent = text(frame, "GameFontNormalSmall", "0.0% full", "BOTTOMRIGHT", -26, 76)
    frame.sort = button(frame, nil, "Sort: Name", 95, "BOTTOMLEFT", 26, 46, function()
        -- With auto sort off a click re-sorts once (wiping the layout); Shift-click also changes the order.
        local once = I.scope ~= 2 and not settings.autoSort
        if once then wipe(I:LayoutStore()) end
        if once and not IsShiftKeyDown() then I:Render(); I:Page(0); return end
        for step = 1, #sorts do
            local next = (I.sort + step) % #sorts
            if not settings.hiddenSorts[next + 1] and not (I.scope == 2 and next == 4) then I.sort = next; break end
        end
        if I.scope == 0 then settings.lastSort = I.sort end
        I:Render(); I:Page(0)
    end)
    local guildWindow = self.scope == 2
    frame.depositAll = button(frame, nil, guildWindow and "Deposit Gold" or "Deposit All", guildWindow and 100 or 92,
        "BOTTOMLEFT", 126, 46, function()
            if guildWindow then StaticPopup_Show("GUILDBANK_DEPOSIT") else I:Request(ACTION.depositAll) end
        end)
    local stack = button(frame, nil, guildWindow and "Withdraw" or "Stack", guildWindow and 74 or 52, "BOTTOMLEFT",
        guildWindow and 230 or 224, 46, function()
            if guildWindow then StaticPopup_Show("GUILDBANK_WITHDRAW") else I:Request(ACTION.merge) end
        end)
    local prev = button(frame, nil, "<", 25, "BOTTOMLEFT", 303, 46, function() I:Scroll(-I.page) end)
    frame.page = text(frame, "GameFontDisableSmall", "1 / 1", "BOTTOMLEFT", 334, 54)
    local nextPage = button(frame, nil, ">", 25, "BOTTOMLEFT", 400, 46, function() I:Scroll(I.page) end)
    frame.expand = button(frame, nil, "Expand Storage", 130, "BOTTOMRIGHT", -26, 46, function()
        if I.scope == 2 then I:GuildExpandPrompt(); return end
        if I.pending or not I.snapshot.upgrade or I.snapshot.upgrade == 0 then return end
        I.expandOffer = { price = I.snapshot.price, upgrade = I.snapshot.upgrade, scope = I.view }
        popupWindow = I
        StaticPopup_Show("ESTERIA_INVENTORY_EXPAND", I.expandOffer.upgrade, GetCoinTextureString(I.expandOffer.price))
    end)
    frame.gold = text(frame, "GameFontHighlightSmall", "", "BOTTOMRIGHT", -26, 96)
    frame.currency = text(frame, "GameFontHighlightSmall", "", "BOTTOMLEFT", 26, 96)
    frame.status = text(frame, "GameFontDisableSmall", "Connecting to digital inventory...", "BOTTOMLEFT", 26, 12)
    frame.status:SetWidth(I.w - 52); frame.status:SetHeight(32); frame.status:SetJustifyH("LEFT")
    frame.status:SetJustifyV("TOP")
    -- Buttons on the footer row, with their anchor, x and height above the row's base line (see LayoutFooter).
    frame.footerRow = { { frame.sort, "BOTTOMLEFT", 26, 0 }, { frame.depositAll, "BOTTOMLEFT", 126, 0 },
        { stack, "BOTTOMLEFT", guildWindow and 230 or 224, 0 }, { prev, "BOTTOMLEFT", 303, 0 },
        { frame.page, "BOTTOMLEFT", 334, 8 }, { nextPage, "BOTTOMLEFT", 400, 0 },
        { frame.expand, "BOTTOMRIGHT", -26, 0 } }
    -- Repaints the frame chrome for the active style; Classic keeps the stock dialog art.
    frame.Paint = function(_, st)
        local alpha, classic = db().alpha, st.classic
        if classic then
            frame:SetBackdrop(dialog); frame:SetBackdropColor(1, 1, 1, alpha)
            header:Show(); skinBg:Hide(); skinWash:Hide(); skinRule:Hide()
            for _, line in ipairs(skinEdges) do line:Hide() end
            frame.title:ClearAllPoints(); frame.title:SetPoint("TOP", 0, -2); frame.title:SetFont(FONT, 13)
        else
            frame:SetBackdrop(nil)
            header:Hide(); skinBg:Show(); skinWash:Show(); skinRule:Show()
            for _, line in ipairs(skinEdges) do line:Show() end
            tint(skinBg, st.bg, 0.97 * alpha); tintAll(skinEdges, st.edge); wash(skinWash, st, 0.22 * alpha)
            tint(skinRule, st.dim, 0.6)
            frame.title:ClearAllPoints(); frame.title:SetPoint("TOP", 0, -14); frame.title:SetFont(FONT, 15)
        end
        local template = { "Left", "Middle", "Right" }
        for _, part in ipairs(template) do
            local region = _G[search:GetName() .. part]
            if region then if classic then region:Show() else region:Hide() end end
        end
        if classic then searchBg:Hide() else searchBg:Show() end
        for _, line in ipairs(searchEdges) do if classic then line:Hide() else line:Show() end end
        tint(searchBg, st.panel, 0.95); tintAll(searchEdges, st.dim)
        tint(barBg, st.panel, 0.95); tintAll(barEdges, st.dim)
        bar:SetStatusBarColor(st.edge[1], st.edge[2], st.edge[3])
        tint(track, st.panel, 0.9); tint(thumb, st.edge, 0.9)
        local function color(fs, c) fs:SetTextColor(c[1], c[2], c[3]) end
        color(frame.title, st.edge); color(frame.capacity, st.muted); color(frame.page, st.muted)
        color(frame.percent, st.edge); color(frame.status, st.muted); color(frame.gold, st.text)
        color(frame.currency, st.text); color(placeholder, st.muted)
    end
    painted[#painted + 1] = frame
    -- Covers the grid while the remote bank is locked and not standing at a banker.
    local lock = CreateFrame("Frame", nil, frame)
    lock:SetPoint("TOPLEFT", 20, -130); lock:SetPoint("BOTTOMRIGHT", -20, 118)
    lock:SetBackdrop({ bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background" })
    lock:SetFrameLevel((frame:GetFrameLevel() or 1) + 40); lock:EnableMouse(true); lock:Hide()
    text(lock, "GameFontNormalLarge", "Remote bank access is locked", "CENTER", 0, 34)
    text(lock, "GameFontHighlightSmall", "Visit a banker for free access, or unlock it for your whole account.", "CENTER", 0, 8)
    button(lock, nil, "Unlock Bank", 160, "CENTER", 0, -26, function()
        popupWindow = I
        StaticPopup_Show("ESTERIA_INVENTORY_UNLOCK", GetCoinTextureString(State.cost))
    end)
    frame.lock = lock
    if self.scope == 2 then self:BuildGuildExtras() end
    -- Corner grip: drag reshapes the window, Shift-drag scales it. Top-left corner stays pinned.
    local grip = CreateFrame("Button", nil, frame)
    frame.grip = grip
    grip:SetSize(16, 16); grip:SetPoint("BOTTOMRIGHT", -8, 8)
    grip:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
    grip:SetPushedTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Down")
    grip:SetHighlightTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Highlight")
    grip:SetScript("OnEnter", function(g)
        GameTooltip:SetOwner(g, "ANCHOR_RIGHT")
        GameTooltip:SetText("Drag: resize")
        GameTooltip:AddLine("Shift-drag: scale", 0.8, 0.8, 0.8)
        GameTooltip:Show()
    end)
    grip:SetScript("OnLeave", function() GameTooltip:Hide() end)
    grip:SetScript("OnMouseDown", function(g)
        if db().lockSize then return end
        local scale, shift = frame:GetScale(), IsShiftKeyDown()
        local left, top = frame:GetLeft() * scale, frame:GetTop() * scale
        frame:ClearAllPoints()
        frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left / scale, top / scale)
        g:SetScript("OnUpdate", function()
            local ui = UIParent:GetEffectiveScale()
            local x, y = GetCursorPosition()
            x, y = x / ui, y / ui
            if shift then
                local wanted = math.max((x - left) / I.w, (top - y) / I.h)
                wanted = math.max(0.5, math.min(wanted, 2, (UIParent:GetHeight() - 35) / I.h,
                    (UIParent:GetWidth() - 35) / I.w))
                frame:SetScale(wanted)
                frame:ClearAllPoints()
                frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left / wanted, top / wanted)
            else
                I.w = math.max(MIN_W, math.min((x - left) / scale, (UIParent:GetWidth() - left) / scale))
                I.h = math.max(MIN_H, math.min((top - y) / scale, top / scale))
                I:Layout(); I:LayoutTabs()
            end
        end)
    end)
    grip:SetScript("OnMouseUp", function(g)
        g:SetScript("OnUpdate", nil)
        if db().lockSize then return end
        EsteriaInventoryDB.scale = frame:GetScale()
        EsteriaInventoryDB[sizeKey] = { I.w, I.h }
        local point, _, relativePoint, x, y = frame:GetPoint()
        EsteriaInventoryDB[positionKey] = { point, relativePoint, x, y }
        if Windows[0].refreshOptions then Windows[0].refreshOptions() end
    end)
    frame:Paint(style())
    I:Apply()
    tinsert(UISpecialFrames, self.prefix .. "Frame")
end

local function createPopups()
    StaticPopupDialogs.ESTERIA_INVENTORY_TAG = {
        text = "Tag for %s (blank clears it):", button1 = ACCEPT, button2 = CANCEL, hasEditBox = 1, maxLetters = 24,
        OnShow = function(self)
            self.editBox:SetText(popupRow and popupRow.tag or ""); self.editBox:HighlightText()
        end,
        OnAccept = function(self)
            if popupRow then popupWindow:Request(ACTION.setTag, popupRow, nil, nil, self.editBox:GetText()) end
        end,
        EditBoxOnEnterPressed = function(self)
            local popup = self:GetParent()
            if popupRow then popupWindow:Request(ACTION.setTag, popupRow, nil, nil, self:GetText()) end
            popup:Hide()
        end,
        EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
        timeout = 0, whileDead = 1, hideOnEscape = 1, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_INVENTORY_DESTROY = {
        text = "Permanently destroy %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            local row = popupRow
            if row then popupWindow:Physical(row, function(bag, slot) PickupContainerItem(bag, slot); DeleteCursorItem() end) end
        end,
        timeout = 0, whileDead = 1, hideOnEscape = 1, showAlert = 1, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_INVENTORY_EXPAND = {
        text = "Unlock %d more inventory positions for %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            local I = popupWindow
            if I and I.expandOffer then I:Request(ACTION.expand, nil, I.expandOffer.price, nil, nil, I.expandOffer.scope) end
        end,
        timeout = 0, whileDead = false,
        hideOnEscape = true, preferredIndex = 3,
    }
    local function iconIndex(icon)
        local wanted = icon and string.lower(icon:match("([^\\/]+)$") or "")
        local count = GetNumMacroItemIcons and GetNumMacroItemIcons() or 0
        if wanted and wanted ~= "" then
            for index = 1, count do
                local name = GetMacroItemIconInfo(index)
                if name and string.lower(name:match("([^\\/]+)$") or "") == wanted then return index end
            end
        end
        return 1
    end
    local function rename(text)
        local tab = GetCurrentGuildBankTab()
        local _, icon = GetGuildBankTabInfo(tab)
        text = text and text:gsub("^%s+", ""):gsub("%s+$", "") or ""
        if text ~= "" then
            SetGuildBankTabInfo(tab, text, iconIndex(icon))
            QueryGuildBankTab(tab)
        end
    end
    StaticPopupDialogs.ESTERIA_GUILD_RENAME = {
        text = "Rename this guild bank tab:", button1 = ACCEPT, button2 = CANCEL, hasEditBox = 1, maxLetters = 16,
        OnShow = function(self)
            local name = GetGuildBankTabInfo(GetCurrentGuildBankTab())
            self.editBox:SetText(name or ""); self.editBox:HighlightText()
        end,
        OnAccept = function(self) rename(self.editBox:GetText()) end,
        EditBoxOnEnterPressed = function(self) local popup = self:GetParent(); rename(self:GetText()); popup:Hide() end,
        EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
        timeout = 0, whileDead = 1, hideOnEscape = 1, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_GUILD_EXPAND = {
        text = "Add %d slots to this guild bank tab for %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            local I = popupWindow
            local tab = GetCurrentGuildBankTab() or 1
            local info = I and I.meta and I.meta.tabs[tab - 1]
            if info then I:GuildMeta(ACTION.expand, info.expandPrice, tab - 1) end
        end,
        timeout = 0, whileDead = false, hideOnEscape = true, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_GUILD_BUYTAB = {
        text = "Buy a new guild bank tab for %s?", button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function() BuyGuildBankTab() end,
        timeout = 0, whileDead = false, hideOnEscape = true, preferredIndex = 3,
    }
    StaticPopupDialogs.ESTERIA_INVENTORY_UNLOCK = {
        text = "Unlock remote bank access for every character on your account for %s?",
        button1 = ACCEPT, button2 = CANCEL,
        OnAccept = function()
            if popupWindow then popupWindow:Request(ACTION.unlock, nil, State.cost, nil, nil, 1) end
        end,
        timeout = 0, whileDead = false, hideOnEscape = true, preferredIndex = 3,
    }
end

-- The options window: its own Esteria-styled frame (not Interface Options), opened from the Options button or
-- /inventory options. A rail of sections on the left, the section's settings on the right; every change applies live.
local function buildOptions()
    local carried = Windows[0]
    local W, H, RAIL = 760, 560, 172
    local f = CreateFrame("Frame", "EsteriaInventoryOptionsFrame", UIParent)
    f:SetSize(W, H); f:SetPoint("CENTER"); f:SetFrameStrata("DIALOG"); f:SetToplevel(true)
    f:EnableMouse(true); f:SetMovable(true); f:SetClampedToScreen(true); f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving); f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:Hide(); tinsert(UISpecialFrames, "EsteriaInventoryOptionsFrame")
    local bg, edges, top, rule = solid(f, "BACKGROUND"), lines(f), solid(f, "BORDER"), solid(f, "BORDER")
    bg:SetAllPoints()
    top:SetPoint("TOPLEFT", 1, -1); top:SetPoint("TOPRIGHT", -1, -1); top:SetHeight(46)
    rule:SetPoint("TOPLEFT", 1, -46); rule:SetPoint("TOPRIGHT", -1, -46); rule:SetHeight(1)
    local rail = solid(f, "BACKGROUND")
    rail:SetPoint("TOPLEFT", 1, -47); rail:SetPoint("BOTTOMLEFT", 1, 1); rail:SetWidth(RAIL)
    local railRule = solid(f, "BORDER")
    railRule:SetPoint("TOPLEFT", RAIL + 1, -47); railRule:SetPoint("BOTTOMLEFT", RAIL + 1, 1); railRule:SetWidth(1)
    local title = paintText(kitText(f, 18), "edge")
    title:SetPoint("TOPLEFT", 20, -14); title:SetText("Esteria Inventory")
    local subtitle = paintText(kitText(f, 11), "muted"); subtitle:SetPoint("LEFT", title, "RIGHT", 12, -2)
    subtitle:SetText("Options")
    local close = CreateFrame("Button", nil, f, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    f.Paint = function(_, st)
        tint(bg, st.bg, 0.98); tintAll(edges, st.edge); wash(top, st, 0.28); tint(rule, st.dim, 0.6)
        tint(rail, st.panel, 0.9); tint(railRule, st.dim, 0.5)
    end
    painted[#painted + 1] = f

    local refreshers = {}
    local function refresh() for _, update in ipairs(refreshers) do update() end end
    local function changed() applyAll(); refresh() end
    local function flag(key) return function() return db()[key] end, function(value) db()[key] = value end end

    -- A page is a scrolling column; controls stack downwards from page.y.
    local pageCount = 0
    local function newPage()
        -- The stock scroll template finds its scrollbar by name ($parentScrollBar), so pages must be named.
        pageCount = pageCount + 1
        local scroll = CreateFrame("ScrollFrame", "EsteriaInventoryOptionsPage" .. pageCount, f,
            "UIPanelScrollFrameTemplate")
        scroll:SetPoint("TOPLEFT", RAIL + 20, -60); scroll:SetPoint("BOTTOMRIGHT", -34, 54)
        local body = CreateFrame("Frame", nil, scroll)
        body:SetSize(W - RAIL - 60, 10); scroll:SetScrollChild(body)
        scroll:Hide()
        return { scroll = scroll, body = body, y = 0, width = W - RAIL - 60 }
    end
    local function grow(page, height)
        page.y = page.y - height
        page.body:SetHeight(-page.y + 12)
    end

    local function heading(page, value, note)
        if page.y < 0 then grow(page, 14) end
        local h = paintText(kitText(page.body, 10), "edge"); h:SetPoint("TOPLEFT", 0, page.y); h:SetText(value:upper())
        local line = solid(page.body, "BORDER"); line:SetHeight(1)
        line:SetPoint("LEFT", h, "RIGHT", 8, 0); line:SetPoint("RIGHT", page.body, "RIGHT", -4, 0)
        painted[#painted + 1] = { Paint = function(_, st) tint(line, st.dim, 0.5) end }
        grow(page, 18)
        if note then
            local n = paintText(kitText(page.body, 10), "muted"); n:SetPoint("TOPLEFT", 0, page.y)
            n:SetWidth(page.width - 8); n:SetJustifyH("LEFT"); n:SetText(note)
            grow(page, math.max(14, n:GetHeight()) + 6)
        end
    end

    -- Checkbox row: square box, label, optional muted note underneath.
    local function toggle(page, labelText, note, get, set)
        local row = CreateFrame("Button", nil, page.body)
        row:SetPoint("TOPLEFT", 0, page.y); row:SetSize(page.width - 8, note and 36 or 24)
        local box = CreateFrame("Frame", nil, row); box:SetSize(16, 16); box:SetPoint("TOPLEFT", 0, -3)
        local boxBg, boxEdges, mark = solid(box, "BACKGROUND"), lines(box), solid(box, "ARTWORK")
        boxBg:SetAllPoints(); mark:SetPoint("TOPLEFT", 4, -4); mark:SetPoint("BOTTOMRIGHT", -4, 4)
        local labelFs = kitText(row, 12); labelFs:SetPoint("TOPLEFT", 26, -4); labelFs:SetText(labelText)
        local noteFs
        if note then
            noteFs = kitText(row, 10); noteFs:SetPoint("TOPLEFT", 26, -19); noteFs:SetText(note)
            noteFs:SetWidth(page.width - 40); noteFs:SetJustifyH("LEFT")
        end
        row.Paint = function(self, st)
            st = st or style()
            tint(boxBg, st.panel, 0.95); tintAll(boxEdges, self.hover and st.edge or st.dim)
            tint(mark, st.edge, get() and 1 or 0)
            labelFs:SetTextColor(st.text[1], st.text[2], st.text[3])
            if noteFs then noteFs:SetTextColor(st.muted[1], st.muted[2], st.muted[3]) end
        end
        row:SetScript("OnClick", function(self)
            PlaySound("igMainMenuOptionCheckBoxOn"); set(not get()); changed()
        end)
        row:SetScript("OnEnter", function(self) self.hover = true; self:Paint() end)
        row:SetScript("OnLeave", function(self) self.hover = false; self:Paint() end)
        painted[#painted + 1] = row; refreshers[#refreshers + 1] = function() row:Paint() end
        grow(page, note and 40 or 26)
        return row
    end

    -- Labelled slider with its value on the right.
    local function slider(page, labelText, key, low, high, step, percent)
        local labelFs = paintText(kitText(page.body, 12), "text"); labelFs:SetPoint("TOPLEFT", 0, page.y)
        labelFs:SetText(labelText)
        local valueFs = paintText(kitText(page.body, 12), "edge")
        valueFs:SetPoint("TOPRIGHT", page.body, "TOPRIGHT", -8, page.y)
        local s = CreateFrame("Slider", nil, page.body)
        s:SetPoint("TOPLEFT", 0, page.y - 20); s:SetPoint("TOPRIGHT", page.body, "TOPRIGHT", -8, page.y - 20)
        s:SetHeight(14); s:SetOrientation("HORIZONTAL"); s:SetMinMaxValues(low, high); s:SetValueStep(step)
        s:EnableMouseWheel(true)
        local track = solid(s, "BACKGROUND"); track:SetPoint("LEFT"); track:SetPoint("RIGHT"); track:SetHeight(4)
        local fill = solid(s, "BORDER"); fill:SetPoint("LEFT"); fill:SetHeight(4)
        s:SetThumbTexture(WHITE)
        local thumb = s:GetThumbTexture(); thumb:SetSize(8, 14)
        local function show(value)
            valueFs:SetText(percent and (math.floor(value * 100 + 0.5) .. "%") or tostring(value))
            local width = s:GetWidth() or 0
            fill:SetWidth(math.max(1, width * (value - low) / (high - low)))
        end
        s:SetScript("OnValueChanged", function(_, value)
            value = math.floor(value / step + 0.5) * step
            show(value)
            if f.loading or db()[key] == value then return end
            db()[key] = value
            applyAll()
        end)
        s:SetScript("OnMouseWheel", function(self, delta) self:SetValue(self:GetValue() + delta * step) end)
        s:SetScript("OnSizeChanged", function() show(db()[key]) end)
        painted[#painted + 1] = { Paint = function(_, st)
            tint(track, st.panel, 1); tint(fill, st.dim, 1); tint(thumb, st.edge, 1)
        end }
        refreshers[#refreshers + 1] = function() s:SetValue(db()[key]); show(db()[key]) end
        grow(page, 44)
    end

    -- A row of mutually exclusive buttons.
    local function choice(page, labelText, note, options, get, set)
        local labelFs = paintText(kitText(page.body, 12), "text"); labelFs:SetPoint("TOPLEFT", 0, page.y)
        labelFs:SetText(labelText)
        grow(page, 18)
        if note then
            local n = paintText(kitText(page.body, 10), "muted"); n:SetPoint("TOPLEFT", 0, page.y)
            n:SetWidth(page.width - 8); n:SetJustifyH("LEFT"); n:SetText(note)
            grow(page, 16)
        end
        local buttons, x = {}, 0
        for _, option in ipairs(options) do
            local width = option[3] or 110
            local b = button(page.body, nil, option[2], width, "TOPLEFT", x, page.y, function()
                set(option[1]); changed()
            end)
            b:SetHeight(24)
            buttons[#buttons + 1] = { b, option[1] }
            x = x + width + 6
        end
        refreshers[#refreshers + 1] = function()
            for _, entry in ipairs(buttons) do
                if entry[2] == get() then entry[1]:LockHighlight() else entry[1]:UnlockHighlight() end
            end
        end
        grow(page, 34)
    end

    -- Style cards: each previews its own palette; the active one gets a bright double edge.
    local function styleCards(page)
        local cardW, cardH, perRow = math.floor((page.width - 8 - 2 * 10) / 3), 74, 3
        for index, preset in ipairs(STYLES) do
            local col, row = (index - 1) % perRow, math.floor((index - 1) / perRow)
            local card = CreateFrame("Button", nil, page.body)
            card:SetSize(cardW, cardH); card:SetPoint("TOPLEFT", col * (cardW + 10), page.y - row * (cardH + 10))
            local cardBg, cardEdges, inner = solid(card, "BACKGROUND"), lines(card), CreateFrame("Frame", nil, card)
            cardBg:SetAllPoints(); tint(cardBg, preset.bg, 1)
            inner:SetPoint("TOPLEFT", 1, -1); inner:SetPoint("BOTTOMRIGHT", -1, 1)
            local innerEdges = lines(inner)
            local band = solid(card, "BORDER"); band:SetPoint("TOPLEFT", 1, -1); band:SetPoint("TOPRIGHT", -1, -1)
            band:SetHeight(22); wash(band, preset, 0.35)
            local name = kitText(card, 13); name:SetPoint("TOPLEFT", 10, -6); name:SetText(preset.name)
            name:SetTextColor(preset.edge[1], preset.edge[2], preset.edge[3])
            local note = kitText(card, 10); note:SetPoint("TOPLEFT", 10, -26); note:SetText(preset.note)
            note:SetTextColor(preset.muted[1], preset.muted[2], preset.muted[3])
            for i, swatchColor in ipairs({ preset.panel, preset.dim, preset.edge, preset.text, preset.primary }) do
                local swatch = solid(card, "ARTWORK"); swatch:SetSize(18, 10)
                swatch:SetPoint("BOTTOMLEFT", 10 + (i - 1) * 22, 10); tint(swatch, swatchColor, 1)
            end
            card.Paint = function(self, st)
                local active = db().style == preset.key
                tintAll(cardEdges, active and st.edge or preset.dim, 1)
                tintAll(innerEdges, st.edge, active and 1 or (self.hover and 0.4 or 0))
            end
            card:SetScript("OnClick", function()
                PlaySound("igMainMenuOptionCheckBoxOn"); db().style = preset.key; changed()
            end)
            card:SetScript("OnEnter", function(self) self.hover = true; self:Paint(style()) end)
            card:SetScript("OnLeave", function(self) self.hover = false; self:Paint(style()) end)
            painted[#painted + 1] = card
        end
        grow(page, math.ceil(#STYLES / perRow) * (cardH + 10) + 4)
    end

    -- Accent colour: a swatch that opens the stock colour picker, and a reset back to the style's own colour.
    local function accentPicker(page)
        local labelFs = paintText(kitText(page.body, 12), "text"); labelFs:SetPoint("TOPLEFT", 0, page.y - 5)
        labelFs:SetText("Accent colour")
        local swatch = CreateFrame("Button", nil, page.body); swatch:SetSize(40, 22)
        swatch:SetPoint("TOPLEFT", 150, page.y)
        local swatchFill, swatchEdges = solid(swatch, "ARTWORK"), lines(swatch)
        swatchFill:SetPoint("TOPLEFT", 2, -2); swatchFill:SetPoint("BOTTOMRIGHT", -2, 2)
        local function setAccent(r, g, b) db().accent = r and { r, g, b } or false; applyAll() end
        swatch:SetScript("OnClick", function()
            local c = style().edge
            ColorPickerFrame.hasOpacity, ColorPickerFrame.opacityFunc = false, nil
            ColorPickerFrame.previousValues = db().accent and { c[1], c[2], c[3] } or false
            ColorPickerFrame.func = function() setAccent(ColorPickerFrame:GetColorRGB()) end
            ColorPickerFrame.cancelFunc = function(previous)
                if previous then setAccent(previous[1], previous[2], previous[3]) else setAccent() end
            end
            ColorPickerFrame:SetColorRGB(c[1], c[2], c[3])
            ColorPickerFrame:SetFrameStrata("FULLSCREEN_DIALOG")
            ColorPickerFrame:Hide(); ColorPickerFrame:Show()
        end)
        local reset = button(page.body, nil, "Use style colour", 130, "TOPLEFT", 200, page.y,
            function() setAccent() end)
        reset:SetHeight(22)
        painted[#painted + 1] = { Paint = function(_, st)
            tint(swatchFill, st.edge, 1); tintAll(swatchEdges, st.dim)
        end }
        grow(page, 34)
    end

    local SECTIONS = {
        { "Appearance", function(page)
            heading(page, "Style", "Pick a look for the inventory, bank and guild bank windows. Changes show at once.")
            styleCards(page)
            accentPicker(page)
            heading(page, "Window")
            slider(page, "Window opacity", "alpha", 0.3, 1, 0.05, true)
            slider(page, "Window scale", "scale", 0.5, 1.5, 0.05, true)
            heading(page, "Items")
            choice(page, "Tile look", nil, { { "framed", "Framed" }, { "flat", "Flat" }, { "clean", "Clean" } },
                flag("tileLook"))
            toggle(page, "Quality-coloured borders", "Outline uncommon and better items in their quality colour.",
                flag("qualityBorders"))
            slider(page, "Item name text size", "textSize", 8, 14, 1)
        end },
        { "Layout", function(page)
            heading(page, "View")
            choice(page, "Show items as", "A grid of icons, or a list of rows with each item's name.",
                { { "grid", "Icon grid" }, { "list", "List" } }, flag("viewMode"))
            toggle(page, "Item names on large grid tiles", "Shown once tiles are big enough to fit a name.",
                flag("showNames"))
            heading(page, "Size")
            slider(page, "Tile scale", "tileScale", 0.6, 2, 0.05, true)
            slider(page, "Tile spacing", "gap", 0, 16, 1)
            slider(page, "Most items per page", "maxTiles", 1, 255, 1)
            heading(page, "Footer")
            toggle(page, "Capacity bar", "The used / total bar above the buttons.", flag("showBar"))
            toggle(page, "Help line", "The hint text along the bottom edge.", flag("showStatus"))
            toggle(page, "Gold", "Your gold in the footer.", flag("showGold"))
        end },
        { "Browsing", function(page)
            heading(page, "Moving through items")
            choice(page, "Paging",
                "Pages flip a screenful at a time; continuous scrolling moves one row per wheel step.",
                { { false, "Pages" }, { true, "Continuous scroll", 140 } }, flag("scrollMode"))
            heading(page, "Order")
            toggle(page, "Automatic sorting", "Off: drag items wherever you like and they stay put.", flag("autoSort"))
            toggle(page, "Remember last category and sort", nil, flag("remember"))
            heading(page, "Sort button cycles through")
            for index = 1, #sorts do
                toggle(page, sorts[index], nil,
                    function() return not db().hiddenSorts[index] end,
                    function(value) db().hiddenSorts[index] = (not value) or nil end)
            end
        end },
        { "Window", function(page)
            heading(page, "Opening")
            toggle(page, "Bag keys open this window", "B and the backpack button open Esteria Inventory.",
                flag("replaceBags"))
            toggle(page, "Single bag button", "Hide the DragonUI bag bar and keep one backpack button.",
                flag("bagButton"))
            toggle(page, "Open and close sounds", nil, flag("sounds"))
            heading(page, "Position")
            toggle(page, "Lock window position", nil, flag("lockPosition"))
            toggle(page, "Lock window size and scale", "Hides the corner grip.", flag("lockSize"))
            heading(page, "Banks")
            toggle(page, "Bank tab in the inventory window", nil, flag("showBankTab"))
            toggle(page, "Esteria guild bank window", "Replaces the stock guild bank. Takes effect after /reload.",
                flag("guildBank"))
        end },
        { "Items", function(page)
            heading(page, "Clicking items")
            toggle(page, "Right-click item menu", "Right-click opens Use, Equip, Disenchant, Destroy and more.",
                flag("contextMenu"))
            toggle(page, "Extra tooltip hints", "Adds the click shortcuts to item tooltips.", flag("hints"))
        end },
        { "Tabs", function(page)
            heading(page, "Category tabs", "Ears and Keyring only appear while you carry those items.")
            for category = 1, #categories - 1 do
                toggle(page, categories[category + 1], nil,
                    function() return not db().hiddenCategories[category] end,
                    function(value) db().hiddenCategories[category] = (not value) or nil end)
            end
        end },
        { "Currencies", function(page)
            heading(page, "Footer currencies", "Ticked currencies show next to your gold.")
            page.listTop = page.y
        end },
    }

    -- Currency rows depend on the character, so they are rebuilt each time the page is opened.
    local currencyRows = {}
    local function listCurrencies()
        local names, collapsed = {}, {}
        if not GetCurrencyListSize then return names end
        carried.scanning = true
        for index = GetCurrencyListSize(), 1, -1 do
            local _, header, expanded = GetCurrencyListInfo(index)
            if header and not expanded then collapsed[#collapsed + 1] = index; ExpandCurrencyList(index, 1) end
        end
        for index = 1, GetCurrencyListSize() do
            local name, header = GetCurrencyListInfo(index)
            if not header then names[#names + 1] = name end
        end
        for _, index in ipairs(collapsed) do ExpandCurrencyList(index, 0) end
        carried.scanning = false
        return names
    end
    local function fillCurrencies(page)
        page.y = page.listTop
        for _, row in pairs(currencyRows) do row:Hide() end
        for _, name in ipairs(listCurrencies()) do
            local row = currencyRows[name]
            if row then
                row:ClearAllPoints(); row:SetPoint("TOPLEFT", 0, page.y); row:Show(); grow(page, 26)
            else
                currencyRows[name] = toggle(page, name, nil,
                    function() return db().currencies[name] and true or false end,
                    function(value) db().currencies[name] = value or nil end)
            end
        end
        carried.refreshOptions()
    end

    local pages, railButtons, current = {}, {}, nil
    local function showSection(index)
        current = index
        for i, page in ipairs(pages) do if i == index then page.scroll:Show() else page.scroll:Hide() end end
        for i, b in ipairs(railButtons) do b.selected = i == index; b:Paint() end
        if SECTIONS[index][1] == "Currencies" then fillCurrencies(pages[index]) end
    end
    for index, section in ipairs(SECTIONS) do
        local page = newPage()
        section[2](page)
        pages[index] = page
        local b = CreateFrame("Button", nil, f)
        b:SetSize(RAIL - 12, 30); b:SetPoint("TOPLEFT", 7, -56 - (index - 1) * 32)
        local wash2, accent = solid(b, "BACKGROUND"), solid(b, "ARTWORK")
        wash2:SetAllPoints(); accent:SetPoint("TOPLEFT"); accent:SetPoint("BOTTOMLEFT"); accent:SetWidth(3)
        local labelFs = kitText(b, 12); labelFs:SetPoint("LEFT", 14, 0); labelFs:SetText(section[1])
        b.Paint = function(self, st)
            st = st or style()
            tint(wash2, st.edge, self.selected and 0.12 or (self.hover and 0.06 or 0))
            tint(accent, st.edge, self.selected and 1 or 0)
            local c = self.selected and st.edge or st.text
            labelFs:SetTextColor(c[1], c[2], c[3])
        end
        b:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); showSection(index) end)
        b:SetScript("OnEnter", function(self) self.hover = true; self:Paint() end)
        b:SetScript("OnLeave", function(self) self.hover = false; self:Paint() end)
        painted[#painted + 1] = b; railButtons[index] = b
    end

    -- Footer: a two-click reset and Done.
    local resetButton = button(f, nil, "Reset all to defaults", 170, "BOTTOMLEFT", RAIL + 20, 14, function(self)
        if self.armed and GetTime() - self.armed < 3 then
            self.armed = nil; self:SetText("Reset all to defaults"); resetAll()
        else
            self.armed = GetTime(); self:SetText("Click again to reset")
        end
    end)
    resetButton:SetScript("OnUpdate", function(self)
        if self.armed and GetTime() - self.armed >= 3 then self.armed = nil; self:SetText("Reset all to defaults") end
    end)
    local done = button(f, nil, "Done", 100, "BOTTOMRIGHT", -20, 14, function() f:Hide() end)
    done.selected = true; done:Paint()
    local hint = paintText(kitText(f, 10), "muted"); hint:SetPoint("BOTTOMLEFT", 14, 20)
    hint:SetText("/inventory options")

    carried.refreshOptions = function()
        f.loading = true
        refresh()
        f.loading = false
        paintAll()
    end
    f:SetScript("OnShow", function()
        PlaySound("igCharacterInfoOpen")
        carried.refreshOptions()
        showSection(current or 1)
    end)
    f:SetScript("OnHide", function() PlaySound("igCharacterInfoClose") end)
    f:Paint(style())
    carried.options = f
end

local guildClassOrder
-- Maps an item to the same coarse categories the server uses (1 equipment, 2 consumable, 3 material, 4 other).
local function guildCategory(link)
    local _, _, _, _, _, itemType, _, _, equipLoc = GetItemInfo(link)
    if not itemType then return 4 end
    guildClassOrder = guildClassOrder or { GetAuctionItemClasses() }
    local class
    for index, name in ipairs(guildClassOrder) do if name == itemType then class = index; break end end
    if class == 1 or class == 2 or (equipLoc and equipLoc ~= "") then return 1 end
    if class == 4 then return 2 end
    if class == 6 or class == 10 then return 3 end
    return 4
end

function Inv:GuildCapacity(tab)
    local info = self.meta and self.meta.tabs[tab - 1]
    return info and info.capacity or 128
end

-- Scope-2 request: tab sizes and prices, or (action expand) buy more slots for a tab.
function Inv:GuildMeta(action, count, tab)
    if not EsteriaInventory_Request or not Windows[0].serverReady then return end
    EsteriaInventory_Request(action or ACTION.browse, 1, 0, count or 0, tab or 0, 0, 0, "", nil, 2)
end

function Inv:GuildMetaDeliver(snapshot)
    local meta = { tabs = {}, nextTabPrice = snapshot.price or 0 }
    for _, row in ipairs(snapshot.rows) do
        meta.tabs[row.guid] = { capacity = row.entry, expandPrice = row.count, step = row.durability,
            max = row.maxDurability, name = row.name }
    end
    self.meta = meta
    self:GuildRefresh()
    if snapshot.message ~= "" then self.frame.status:SetText(snapshot.message) end
end

local function guildAgo(years, months, days, hours)
    if (years or 0) > 0 then return years .. "y ago" end
    if (months or 0) > 0 then return months .. "mo ago" end
    if (days or 0) > 0 then return days .. "d ago" end
    if (hours or 0) > 0 then return hours .. "h ago" end
    return "just now"
end

-- Item log, money log and tab info all come from the stock guild bank API.
function Inv:RefreshPanel()
    local frame = self.frame
    local panel, mode = frame.logPanel, self.mode or "items"
    if type(panel) ~= "table" then return end
    if mode == "items" then panel:Hide(); return end
    panel:Show()
    local tab = GetCurrentGuildBankTab and GetCurrentGuildBankTab() or 1
    if mode == "info" then
        panel.scroll:Hide(); panel.edit:Show()
        local canEdit = (CanEditGuildTabInfo and CanEditGuildTabInfo(tab)) or (IsGuildLeader and IsGuildLeader())
        if not panel.edit:HasFocus() then panel.edit:SetText(GetGuildBankText and GetGuildBankText(tab) or "") end
        panel.edit:EnableMouse(canEdit and true or false)
        if canEdit then panel.save:Show() else panel.save:Hide() end
        return
    end
    panel.edit:Hide(); panel.save:Hide(); panel.scroll:Show()
    local lines = {}
    if mode == "log" then
        for index = (GetNumGuildBankTransactions(tab) or 0), 1, -1 do
            local kind, name, link, count, tab1, tab2, years, months, days, hours = GetGuildBankTransaction(tab, index)
            local what = (link or "?") .. ((count or 1) > 1 and (" x" .. count) or "")
            local line = (name or "?") .. (kind == "deposit" and " deposited " or kind == "withdraw" and " withdrew "
                or " moved ") .. what
            if kind == "move" then line = line .. " (tab " .. (tab1 or 0) .. " > " .. (tab2 or 0) .. ")" end
            lines[#lines + 1] = "|cff999999" .. guildAgo(years, months, days, hours) .. "|r  " .. line
        end
    else
        local verbs = { deposit = " deposited ", withdraw = " withdrew ", repair = " spent on repairs: ",
            withdrawForTab = " withdrew for a new tab: ", buyTab = " bought a tab for ", depositSummary = " total deposits: " }
        for index = (GetNumGuildBankMoneyTransactions and GetNumGuildBankMoneyTransactions() or 0), 1, -1 do
            local kind, name, amount, years, months, days, hours = GetGuildBankMoneyTransaction(index)
            lines[#lines + 1] = "|cff999999" .. guildAgo(years, months, days, hours) .. "|r  " .. (name or "?")
                .. (verbs[kind] or (" " .. tostring(kind) .. " ")) .. GetCoinTextureString(amount or 0)
        end
    end
    if #lines == 0 then lines[1] = "|cff999999Nothing logged yet.|r" end
    local width = math.max(200, (panel.scroll:GetWidth() or 520) - 8)
    panel.content:SetWidth(width); panel.logText:SetWidth(width - 8)
    panel.logText:SetText(table.concat(lines, "\n"))
    panel.content:SetHeight(math.max(20, (panel.logText:GetHeight() or 20) + 8))
end

function Inv:SetMode(mode)
    self.mode = mode
    local tab = GetCurrentGuildBankTab and GetCurrentGuildBankTab() or 1
    for key, b in pairs(self.frame.modeButtons) do
        if key == mode then b:LockHighlight() else b:UnlockHighlight() end
    end
    if mode == "log" and QueryGuildBankLog then QueryGuildBankLog(tab)
    elseif mode == "money" and QueryGuildBankLog then QueryGuildBankLog(7)
    elseif mode == "info" and QueryGuildBankText then QueryGuildBankText(tab) end
    self:RefreshPanel()
end

function Inv:GuildExpandPrompt()
    local tab = GetCurrentGuildBankTab() or 1
    local info = self.meta and self.meta.tabs[tab - 1]
    if not info or not self.canExpand then return end
    popupWindow = self
    StaticPopup_Show("ESTERIA_GUILD_EXPAND", info.step, GetCoinTextureString(info.expandPrice))
end

function Inv:GuildBuyPrompt()
    local price = self.meta and self.meta.nextTabPrice or 0
    if price == 0 and GetGuildBankTabCost then price = GetGuildBankTabCost() end
    StaticPopup_Show("ESTERIA_GUILD_BUYTAB", GetCoinTextureString(price))
end

function Inv:BuildGuildExtras()
    local I, frame = self, self.frame
    frame.modeButtons = {}
    local x = 26
    for _, mode in ipairs({ { "items", "Items", 64 }, { "log", "Log", 56 }, { "money", "Money Log", 92 },
        { "info", "Info", 56 } }) do
        frame.modeButtons[mode[1]] = button(frame, nil, mode[2], mode[3], "TOPLEFT", x, -127, function()
            I:SetMode(mode[1])
        end)
        x = x + mode[3] + 5
    end
    frame.rename = button(frame, nil, "Rename Tab", 100, "TOPRIGHT", -26, -127, function()
        popupWindow = I
        StaticPopup_Show("ESTERIA_GUILD_RENAME")
    end)
    frame.buyTab = button(frame, nil, "Buy Tab", 84, "TOPLEFT", 26, -99, function() I:GuildBuyPrompt() end)
    frame.buyTab:Hide()
    local panel = CreateFrame("Frame", nil, frame)
    panel:SetPoint("TOPLEFT", 20, -158); panel:SetPoint("BOTTOMRIGHT", -20, 118)
    skin(panel, 0.05, 0.05, 0.05)
    panel:SetFrameLevel((frame:GetFrameLevel() or 1) + 30); panel:EnableMouse(true); panel:Hide()
    local scroll = CreateFrame("ScrollFrame", self.prefix .. "LogScroll", panel, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 8, -8); scroll:SetPoint("BOTTOMRIGHT", -28, 8)
    local content = CreateFrame("Frame", nil, scroll)
    content:SetSize(500, 20); scroll:SetScrollChild(content)
    local logText = content:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
    logText:SetPoint("TOPLEFT", 4, -4); logText:SetWidth(490); logText:SetJustifyH("LEFT")
    local edit = CreateFrame("EditBox", nil, panel)
    edit:SetMultiLine(true); edit:SetMaxLetters(500); edit:SetFontObject("GameFontHighlight"); edit:SetAutoFocus(false)
    edit:SetPoint("TOPLEFT", 12, -12); edit:SetPoint("BOTTOMRIGHT", -12, 44); edit:Hide()
    edit:SetScript("OnEscapePressed", function(box) box:ClearFocus() end)
    local save = button(panel, nil, "Save Info", 100, "BOTTOMRIGHT", -12, 10, function()
        SetGuildBankText(GetCurrentGuildBankTab(), edit:GetText()); edit:ClearFocus()
    end)
    save:Hide()
    panel.scroll, panel.content, panel.logText, panel.edit, panel.save = scroll, content, logText, edit, save
    frame.logPanel = panel
end

function Inv:GuildOpen()
    self.mode = "items"
    local tabs = GetNumGuildBankTabs and GetNumGuildBankTabs() or 0
    if tabs > 0 and (GetCurrentGuildBankTab() or 0) < 1 then SetCurrentGuildBankTab(1) end
    self.active = true
    if tabs > 0 then QueryGuildBankTab(GetCurrentGuildBankTab()) end
    self:GuildMeta()
    self:GuildRefresh(0)
    if type(self.frame.modeButtons) == "table" then self:SetMode("items") end
end

-- Rebuilds the visible page from the stock guild bank API: search, sort and paging run on the client.
function Inv:GuildRefresh(offset)
    if not GetCurrentGuildBankTab then return end
    local tab = GetCurrentGuildBankTab() or 1
    local numTabs = GetNumGuildBankTabs() or 0
    local name, _, viewable, canDeposit, _, remaining
    if numTabs > 0 then name, _, viewable, canDeposit, _, remaining = GetGuildBankTabInfo(tab) end
    local all, filtered, free = 0, {}, nil
    local needle = string.lower(self.search or "")
    if viewable then
        for slot = 1, self:GuildCapacity(tab) do
            local link = GetGuildBankItemLink(tab, slot)
            if link then
                all = all + 1
                local texture, count, locked = GetGuildBankItemInfo(tab, slot)
                local itemName = link:match("%[(.-)%]") or "?"
                local entry = tonumber(link:match("item:(%d+)")) or 0
                local quality = 1
                for q = 0, 7 do
                    local _, _, _, hex = GetItemQualityColor(q)
                    if hex and link:find("|c" .. hex, 1, true) then quality = q; break end
                end
                local equipLoc = select(9, GetItemInfo(link))
                if needle == "" or string.find(string.lower(itemName), needle, 1, true)
                    or tostring(entry):find(needle, 1, true) then
                    filtered[#filtered + 1] = { guid = tab * 1000 + slot, entry = entry, count = count or 1,
                        digital = true, bag = 0, slot = 0, quality = quality, category = guildCategory(link),
                        inventoryType = (equipLoc and equipLoc ~= "") and 1 or 0, durability = 0, maxDurability = 0,
                        favorite = false, bound = false, split = false, tag = "", name = itemName, icon = "",
                        texture = texture, locked = locked, link = link:match("|H(item:[^|]+)|h") or ("item:" .. entry),
                        guild = { tab = tab, slot = slot } }
                end
            elseif not free then free = slot end
        end
    end
    local sort = self.sort
    table.sort(filtered, function(a, b)
        if sort == 1 and a.quality ~= b.quality then return a.quality > b.quality end
        if sort == 2 and a.count ~= b.count then return a.count > b.count end
        if sort == 3 and a.category ~= b.category then return a.category < b.category end
        if a.name ~= b.name then return a.name < b.name end
        return a.guid < b.guid
    end)
    local page, total = self.page or DEFAULT_PAGE, #filtered
    offset = offset or self.targetOffset or self.snapshot.offset or 0
    offset = total > 0 and math.min(math.floor(offset / page), math.floor((total - 1) / page)) * page or 0
    local rows = {}
    for index = offset + 1, math.min(total, offset + page) do rows[#rows + 1] = filtered[index] end
    local message = ""
    if numTabs == 0 then message = "This guild has no bank tabs."
    elseif not viewable then message = "You do not have permission to view this tab."
    elseif not canDeposit then message = "You can withdraw from this tab but not deposit." end
    self.rev = (self.rev or 0) + 1
    self.snapshot = { rows = rows, total = total, offset = offset, used = all, capacity = self:GuildCapacity(tab), upgrade = 0, price = 0,
        revision = self.rev, request = 0, active = true, status = 0, message = message }
    self.snaps[2] = self.snapshot
    self.targetOffset, self.guildFree, self.guildTab, self.guildTabName = nil, free, tab, name
    local unlimited = remaining == nil or remaining < 0 or remaining >= 2147483647
    self.guildInfo = numTabs > 0 and viewable and (unlimited and "Unlimited withdrawals"
        or (remaining .. " withdrawals left today")) or ""
    self.canBuyTab = numTabs < 6 and IsGuildLeader and IsGuildLeader() and true or false
    local meta = self.meta and self.meta.tabs[tab - 1]
    self.canExpand = IsGuildLeader and IsGuildLeader() and meta and meta.capacity < meta.max and true or false
    if type(self.frame.rename) == "table" then
        if IsGuildLeader and IsGuildLeader() and numTabs > 0 then self.frame.rename:Show() else self.frame.rename:Hide() end
    end
    self:LayoutGuildTabs()
    self:Render()
    self:RefreshPanel()
    self.frame.status:SetText(message ~= "" and message
        or "Left-click to pick up or drop. Right-click to withdraw. Shift-click to split.")
end

-- Delivers one server snapshot (scope 0 carried, 1 bank) to the window(s) that show that storage.
function Inv:Deliver(snapshot, scope)
    local learnedSpecialTabs = scope == 0 and snapshot.specialTabs and not self.snaps[0].specialTabs
    self.snaps[scope] = snapshot
    local asked = self.pending and snapshot.request == self.pending.id and self.pending.action == ACTION.browse
    if self.pending and snapshot.request == self.pending.id then self.pending = nil end
    if scope == 0 then self.active = snapshot.active end
    if self.view == scope then
        self.snapshot = snapshot
        self.targetOffset = nil
        if not self:HasCategory(self.category) then
            self.category = 0
            self.queued = { offset = 0 }
        elseif learnedSpecialTabs then
            self.queued = self.queued or { offset = 0 }
        end
        self:LayoutTabs()
        -- A server without variable pages always answers with 20 rows; stop asking for more.
        local rows = #snapshot.rows
        if asked and rows == DEFAULT_PAGE and self.page > DEFAULT_PAGE and snapshot.offset + rows < snapshot.total then
            self.cap = DEFAULT_PAGE
            self:Layout()
        end
    end
    if scope == 0 and self.scope == 0 and snapshot.active then
        self:InstallHooks()
        if enabled() and EsteriaSatchel and EsteriaSatchel.frames then
            for bag = 1, 4 do
                if EsteriaSatchel.frames[bag] then EsteriaSatchel.frames[bag]:Hide() end
            end
        end
    end
    self:Render()
    if self.scope == 0 and scope == 0 then
        self:RunFollowUp()
        self:SkinBagButtons()
    end
    if self.view == scope then
        self.frame.status:SetText(snapshot.message ~= "" and snapshot.message
            or (scope == 1 and "Left-click to move to your bags. Right-click a bag item to send it here."
                or "Left-click to withdraw. Right-click to use or equip. Alt-click to favorite."))
    end
    self:Flush()
end

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN"); events:RegisterEvent("PLAYER_LEAVING_WORLD")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("BAG_UPDATE"); events:RegisterEvent("ITEM_LOCK_CHANGED")
events:RegisterEvent("BAG_UPDATE_COOLDOWN")
events:RegisterEvent("PLAYER_MONEY"); events:RegisterEvent("CURRENCY_DISPLAY_UPDATE")
events:RegisterEvent("BANKFRAME_OPENED"); events:RegisterEvent("BANKFRAME_CLOSED")
events:RegisterEvent("GUILDBANKFRAME_OPENED"); events:RegisterEvent("GUILDBANKFRAME_CLOSED")
events:RegisterEvent("GUILDBANKBAGSLOTS_CHANGED"); events:RegisterEvent("GUILDBANK_UPDATE_TABS")
events:RegisterEvent("GUILDBANK_UPDATE_MONEY"); events:RegisterEvent("GUILDBANK_UPDATE_WITHDRAWMONEY")
events:RegisterEvent("GUILDBANKLOG_UPDATE"); events:RegisterEvent("GUILDBANK_TEXT_CHANGED")
events:SetScript("OnEvent", function(_, event, arg1, arg2, arg3, arg4)
    local I = Windows[0]
    if event ~= "PLAYER_LOGIN" and not I then return end
    if event == "PLAYER_LOGIN" then
        EsteriaInventoryDB = EsteriaInventoryDB or { replaceBags = true }
        I = newWindow(0); newWindow(1); newWindow(2)
        EsteriaInventory = I
        I:Create(); Windows[1]:Create(); Windows[2]:Create()
        createPopups()
        buildOptions()
        for scope = 0, 2 do
            local w, key = Windows[scope], GEOMETRY[scope][2]
            local p = EsteriaInventoryDB[key]
            if p then w.frame:ClearAllPoints(); w.frame:SetPoint(p[1], UIParent, p[2], p[3], p[4])
            elseif scope >= 1 then w:ResetGeometry() end
        end
        -- Our guild bank window replaces the stock one: load it now and stop it from ever opening.
        silenceStockGuildBank()
        -- The stock bank frame is replaced by our banker window once digital storage is active.
        if BankFrame and EsteriaInventory_Request then BankFrame:UnregisterEvent("BANKFRAME_OPENED") end
        if EsteriaInventory_Request then
            events:RegisterEvent("ESTERIA_INVENTORY_UPDATE")
            I.activateAt = GetTime() + 2; I.gateUntil = GetTime() + 10
        else
            I.frame.status:SetText("The matched Esteria native client is required. Normal bags remain available.")
            if BankFrame then BankFrame:RegisterEvent("BANKFRAME_OPENED") end
        end
    elseif event == "PLAYER_LEAVING_WORLD" then
        for scope = 0, 2 do
            local w = Windows[scope]
            if w then
                w.active, w.pending, w.cap, w.queued = false, nil, nil, nil
                w.snaps[0].active, w.snaps[1].active = false, false
                w.snaps[0].specialTabs, w.snaps[1].specialTabs = false, false
            end
        end
        State.unlocked, State.atBanker, State.atGuildBank = false, false, false
        I.serverReady, I.nextDiscovery, I.discoveryDeadline, I.activateAt = false, nil, nil, nil
        if I.frame then I.frame:Hide(); Windows[1].frame:Hide(); Windows[2].frame:Hide() end
    elseif event == "PLAYER_ENTERING_WORLD" then
        if I.frame and EsteriaInventory_Request then I.activateAt = GetTime() + 2; I.gateUntil = GetTime() + 10 end
    elseif event == "CHAT_MSG_ADDON" then
        local prefix, message, channel, sender = arg1, arg2, arg3, arg4
        if I.frame and EsteriaInventory_Request and I.discoveryDeadline and GetTime() <= I.discoveryDeadline
            and prefix == "ESTERIADI" and message == "READY:1" and channel == "WHISPER"
            and sender == UnitName("player") then
            I.serverReady, I.discoveryDeadline, I.activateAt = true, nil, nil
            I:Request(ACTION.activate)
        end
    elseif event == "BANKFRAME_OPENED" then
        -- Only reachable once our window owns the bank (see PLAYER_LOGIN); stock frame stays closed.
        if I.frame and I.active then
            State.atBanker = true
            Windows[1]:Show()
            if not I.frame:IsShown() then I:Show() end
        elseif BankFrame then
            ShowUIPanel(BankFrame)
        end
    elseif event == "GUILDBANKFRAME_OPENED" then
        if db().guildBank and I.frame then
            silenceStockGuildBank()
            I.silenceUntil = GetTime() + 2
            State.atGuildBank = true
            Windows[2]:Show()
            if not I.frame:IsShown() and I.active then I:Show() end
        end
    elseif event == "GUILDBANKFRAME_CLOSED" then
        State.atGuildBank = false
        if Windows[2] and Windows[2].frame:IsShown() then Windows[2].frame:Hide() end
    elseif event == "GUILDBANKBAGSLOTS_CHANGED" or event == "GUILDBANK_UPDATE_TABS"
        or event == "GUILDBANK_UPDATE_MONEY" or event == "GUILDBANK_UPDATE_WITHDRAWMONEY" then
        if Windows[2] and Windows[2].frame:IsShown() then Windows[2]:GuildRefresh() end
    elseif event == "GUILDBANKLOG_UPDATE" or event == "GUILDBANK_TEXT_CHANGED" then
        if Windows[2] and Windows[2].frame:IsShown() then Windows[2]:RefreshPanel() end
    elseif event == "BANKFRAME_CLOSED" then
        State.atBanker = false
        if Windows[1] and Windows[1].frame:IsShown() then Windows[1].frame:Hide() end
        applyAll()
    elseif event == "ESTERIA_INVENTORY_UPDATE" then
        local scope = tonumber(arg1) or 0
        if scope > 2 then scope = 0 end
        local snapshot = EsteriaInventory_GetSnapshot(scope)
        if scope == 2 then
            Windows[2]:GuildMetaDeliver(snapshot)
            return
        end
        State.unlocked, State.atBanker, State.cost = snapshot.bankUnlocked, snapshot.atBanker, snapshot.unlockCost or 0
        if scope == 0 then
            I:Deliver(snapshot, 0)
            if Windows[1] then Windows[1].snaps[0] = snapshot end
        else
            I:Deliver(snapshot, 1)
            Windows[1]:Deliver(snapshot, 1)
        end
    elseif event == "PLAYER_MONEY" or event == "CURRENCY_DISPLAY_UPDATE" then
        if I.frame and not I.scanning then
            if I.frame:IsShown() then I:UpdateMoney() end
            if Windows[1].frame:IsShown() then Windows[1]:UpdateMoney() end
            if Windows[2].frame:IsShown() then Windows[2]:UpdateMoney() end
        end
    elseif event == "ITEM_LOCK_CHANGED" and Windows[2] and Windows[2].frame:IsShown() and not (I.active and I.frame:IsShown()) then
        Windows[2]:GuildRefresh()
    elseif I.frame and I.active and I.frame:IsShown() then
        -- Lock/cooldown changes are local; only bag changes need a server refresh.
        if event == "BAG_UPDATE" then
            if I.view == 0 then I.refreshAt = GetTime() + 0.1 end
            I:RunFollowUp()
        else I:Render() end
    end
end)
events:SetScript("OnUpdate", function()
    local now = GetTime()
    local I = Windows[0]
    if now >= (I.hookAt or 0) then
        I.hookAt = now + 0.25
        I:InstallHooks()
        if I.active or pending() then hideStockBags(); I:SkinBagButtons() end
    end
    if I.silenceUntil and now < I.silenceUntil then silenceStockGuildBank() end
    for scope = 0, 2 do
        local w = Windows[scope]
        if w.pending and now - w.pending.started > 8 then
            w.pending = nil
            w.frame.status:SetText("No reply received. Open inventory to refresh; the action has not been repeated.")
            w:Flush()
        end
    end
    if I.discoveryDeadline and now >= I.discoveryDeadline then
        I.discoveryDeadline = nil
        I.frame.status:SetText("This realm has not advertised digital inventory support. Normal bags remain available.")
    end
    if I.activateAt and now >= I.activateAt then
        I.activateAt = nil; I:Request(ACTION.activate)
        return
    end
    if I.dragFrom and not I.dragFrom.internal and not CursorHasItem() then I.dragFrom = nil end
    for scope = 0, 2 do
        local w = Windows[scope]
        if w.searchAt and now >= w.searchAt then
            w.searchAt = nil; w:Page(0)
        elseif w.pageAt and now >= w.pageAt then
            -- Window or tile settings changed the page size; refetch from the first visible item.
            w.pageAt = nil
            if (w.active or scope >= 1) and w.frame:IsShown() then w:Page(w:Offset()) end
        elseif w.refreshAt and now >= w.refreshAt and not w.pending then
            w.refreshAt = nil
            if w.frame:IsShown() then w:Refresh() end
        end
    end
end)

SLASH_ESTERIAINVENTORY1, SLASH_ESTERIAINVENTORY2 = "/inventory", "/inv"
SlashCmdList.ESTERIAINVENTORY = function(message)
    local I = Windows[0]
    if not I or not I.frame then return end
    if message == "legacy" then
        EsteriaInventoryDB.replaceBags = not EsteriaInventoryDB.replaceBags
        I.frame:Hide()
        DEFAULT_CHAT_FRAME:AddMessage("Esteria Inventory bag replacement: "
            .. (EsteriaInventoryDB.replaceBags and "enabled" or "disabled"))
    elseif message == "config" or message == "options" then
        if I.options then I.options:Show(); I.options:Raise() end
    elseif message == "bank" then
        I:Show(); I:SetView(1)
    elseif message == "reset" then
        resetAll()
    elseif message == "debug" then
        -- Reports what sits on screen: a held item, the stuck drag icon, and any tile whose icon left its tile.
        local say = function(text) DEFAULT_CHAT_FRAME:AddMessage("|cffffd100Esteria debug:|r " .. text) end
        local kind, id = GetCursorInfo()
        say("cursor " .. tostring(kind) .. " " .. tostring(id) .. ", drag icon " .. (holder and holder:IsShown()
            and "SHOWN" or "hidden") .. ", view " .. I.view .. ", page " .. tostring(I.page) .. ", cols "
            .. tostring(I.cols) .. ", tiles " .. #I.buttons)
        local found = 0
        for index, b in ipairs(I.buttons) do
            local art = _G[b:GetName() .. "IconTexture"]
            local bx, by = b:GetCenter()
            local ax, ay = art:GetCenter()
            local point, relative = art:GetPoint()
            if b:IsVisible() and art:IsVisible() and bx and ax
                and (math.abs(ax - bx) > 3 or math.abs(ay - by) > 12 or relative ~= b) then
                found = found + 1
                say("tile " .. index .. " icon off by " .. math.floor(ax - bx) .. "," .. math.floor(ay - by)
                    .. " anchor " .. tostring(point) .. " to " .. tostring(relative and relative:GetName())
                    .. " size " .. math.floor(art:GetWidth()) .. ", row " .. tostring(b.row and b.row.name))
            end
        end
        say(found .. " misplaced icons")
        local settings = db()
        say("viewMode " .. tostring(settings.viewMode) .. ", style " .. tostring(settings.style) .. ", scroll "
            .. tostring(settings.scrollMode))
        -- Hover the stray icon while running this: names the frame drawing it and its parents.
        local focus, chain = GetMouseFocus(), {}
        while focus and #chain < 6 do
            chain[#chain + 1] = tostring(focus:GetName() or ("<" .. tostring(focus:GetObjectType()) .. ">"))
            focus = focus:GetParent()
        end
        say("under mouse: " .. (#chain > 0 and table.concat(chain, " < ") or "nothing"))
        for err in pairs(reportedErrors) do say("error: " .. tostring(err)) end
    elseif tonumber(message) then
        EsteriaInventoryDB.scale = math.max(0.4, math.min(1.5, tonumber(message)))
        I.frame:Hide(); I:Show()
    else I:Toggle() end
end
