-- Esteria's spellbook, in the PvP/Collections/Quest Log look: a rail of books (General, each talent tree, Pet,
-- Cosmetics) with filters, a searchable grid of spell cards that cast and drag like the stock book, and a detail
-- pane built from the spell's own tooltip. Cards are secure action buttons, so casting works in combat; browsing
-- (pages, books, search) re-arms them and therefore waits for combat to end, as Blizzard's rules require.
local ES = EsteriaStyle
local W, H = 960, 640
local RAIL_W, GRID_W = 210, 400
local COLS, ROWS = 2, 8
local CARD_W, CARD_H, ROW_STEP = 184, 50, 56
local PER_PAGE = COLS * ROWS
local RAIL_ROWS = 13
-- Cosmetic wings (and a few cosmetic extras) sit in the General tab range on this client; file them by spell ID.
local COSMETIC_FIRST, COSMETIC_LAST = 970000, 970999
local COSMETIC_EXTRA = { [201111] = true }
local SPELL, PETBOOK = BOOKTYPE_SPELL or "spell", BOOKTYPE_PET or "pet"
local OWN_BOOK = { pet = true, emotes = true, wings = true }

local frame, grid, detail, search, counter, banner, pageText, prevPage, nextPage
local cards, railRows, toggles = {}, {}, {}
local spells, books, filtered, bound = {}, {}, {}, {}
local state = { book = "all", page = 1 }
local railOffset = 0
local pinned, shownSpell
local dirty, barsDirty, cooldownsDirty, pendingArm = true, true, false, false
local db, refresh

-- DragonUI ships its own spellbook module; two books would fight over the key binding. Its profile is ready by the
-- time this file runs (its OnInitialize ran at its ADDON_LOADED) and the module applies at PLAYER_LOGIN, so switching
-- it off here keeps it out. It can be switched back on in DragonUI's options.
do
    local dui = _G.DragonUI
    local modules = dui and dui.db and dui.db.profile and dui.db.profile.modules
    if modules and modules.spellbook then modules.spellbook.enabled = false end
end

local function shown(region, on) if on then region:Show() else region:Hide() end end
local function locked() return InCombatLockdown() and true or false end

-- Model --------------------------------------------------------------------------------------------------------------
local function spellID(link) return link and tonumber(link:match("spell:(%d+)")) end
local function isCosmetic(id) return id ~= nil and (id >= COSMETIC_FIRST and id <= COSMETIC_LAST or COSMETIC_EXTRA[id] == true) end

local function scan()
    spells, books = {}, {}
    local byKey, tabs = {}, {}
    for t = 1, GetNumSpellTabs() or 0 do
        local name, icon, offset, count = GetSpellTabInfo(t)
        tabs[t] = { key = "tab" .. t, name = name or ("Book " .. t), icon = icon, first = (offset or 0) + 1,
                    last = (offset or 0) + (count or 0), count = 0 }
        books[#books + 1] = tabs[t]
    end
    -- Emotes and wings live in the General range on this client; each gets a book of its own.
    local emotes = { key = "emotes", name = "Emotes", count = 0 }
    local wings = { key = "wings", name = "Wings", count = 0 }
    local pet = { key = "pet", name = PET or "Pet", count = 0 }
    local function tabOf(index)
        for _, tab in ipairs(tabs) do if index >= tab.first and index <= tab.last then return tab end end
        return tabs[#tabs] or wings
    end
    local function add(index, book, tab)
        local name, rank = GetSpellName(index, book)
        if not name then return false end
        local id = spellID(GetSpellLink(index, book))
        if book == SPELL then
            if (rank or ""):lower() == "emote" then tab = emotes
            elseif isCosmetic(id) or tab.name == "Cosmetics" then tab = wings end
        end
        local key = tab.key .. "\001" .. name
        local s = byKey[key]
        if not s then
            s = { name = name, book = book, tab = tab, ranks = {}, order = #spells + 1 }
            byKey[key] = s
            spells[#spells + 1] = s
            tab.count = tab.count + 1
        end
        -- Ranks follow one another in the book, highest last; the card casts the highest one known.
        s.index, s.rank, s.id = index, rank or "", id
        s.icon = GetSpellTexture(index, book)
        s.passive = IsPassiveSpell(index, book) and true or false
        if rank and rank ~= "" then s.ranks[#s.ranks + 1] = rank end
        tab.icon = tab.icon or s.icon
        return true
    end
    -- Walk until the book runs out rather than trusting tab ranges: on this client they can be off by a few.
    local index = 1
    while index < 4096 and add(index, SPELL, tabOf(index)) do index = index + 1 end
    books[#books + 1] = emotes
    books[#books + 1] = wings
    local petCount = HasPetSpells()
    for i = 1, petCount or 0 do add(i, PETBOOK, pet) end
    books[#books + 1] = pet
    -- Empty books (the client's own Cosmetics tab, once its wings moved out) stay off the rail.
    local kept = {}
    for _, b in ipairs(books) do if b.count > 0 then kept[#kept + 1] = b end end
    books = kept
end

local function scanBars()
    bound = {}
    for slot = 1, 120 do
        local kind, id, sub, sid = GetActionInfo(slot)
        if kind == "spell" then
            local name = sid and GetSpellInfo(sid)
            if not name and id then name = GetSpellName(id, sub == PETBOOK and PETBOOK or SPELL) end
            if name then bound[name] = true end
        end
    end
end

local function unbound(s) return s.book == SPELL and not s.passive and not bound[s.name] end

local function refilter()
    local query = search and search:GetText():lower() or ""
    filtered = {}
    for _, s in ipairs(spells) do
        local ok
        if query ~= "" then
            ok = s.name:lower():find(query, 1, true) or s.tab.name:lower():find(query, 1, true) or
                s.rank:lower():find(query, 1, true)
        elseif state.book == "all" then
            ok = not OWN_BOOK[s.tab.key] -- pet, emotes and wings are browsed in their own books
        else
            ok = s.tab.key == state.book
        end
        if ok and db.hidePassive and s.passive then ok = false end
        if ok and db.unbound and not unbound(s) then ok = false end
        if ok then filtered[#filtered + 1] = s end
    end
    if db.sort == "name" then
        table.sort(filtered, function(a, b) if a.name ~= b.name then return a.name < b.name end return a.order < b.order end)
    end
    local pages = math.max(1, math.ceil(#filtered / PER_PAGE))
    state.page = math.max(1, math.min(state.page, pages))
    return pages
end

-- Secure arming (out of combat only) ---------------------------------------------------------------------------------
local function castName(s) return s.rank ~= "" and (s.name .. "(" .. s.rank .. ")") or s.name end

local function arm(card, s)
    card:SetAttribute("type1", nil)
    card:SetAttribute("spell", nil)
    card:SetAttribute("type2", nil)
    card:SetAttribute("macrotext2", nil)
    card:SetAttribute("shift-type1", "none") -- shift-click links the spell instead of casting it
    if s and not s.passive then
        card:SetAttribute("type1", "spell")
        card:SetAttribute("spell", castName(s))
        if s.book == PETBOOK then
            card:SetAttribute("type2", "macro")
            card:SetAttribute("macrotext2", "/petautocasttoggle " .. s.name)
        end
    end
    card.spell = s
    shown(card, s ~= nil)
end

local function pickup(s)
    if s and not s.passive then PickupSpell(s.index, s.book) end
end

local function link(s)
    local spellLink = s and GetSpellLink(s.index, s.book)
    if not spellLink then return end
    if not (ChatEdit_GetActiveWindow and ChatEdit_GetActiveWindow()) and ChatFrame_OpenChat then ChatFrame_OpenChat("") end
    ChatEdit_InsertLink(spellLink)
end

EsteriaSpellbook_Test = {
    scan = scan, refilter = refilter, scanBars = scanBars, arm = arm, castName = castName,
    state = function() return { spells = spells, books = books, filtered = filtered, page = state.page } end,
    setBook = function(key) state.book, state.page = key, 1 end,
}

-- Widgets ------------------------------------------------------------------------------------------------------------
local function panel(parent)
    local p = CreateFrame("Frame", nil, parent)
    ES.Solid(p, "BACKGROUND", { 0.06, 0.07, 0.10 }, 0.95):SetAllPoints()
    p.lines = ES.Border(p, ES.GOLD_DIM, 0.7)
    return p
end

local function heading(parent, text)
    local h = ES.Text(parent, 10, ES.GOLD_DIM)
    h:SetText(text:upper())
    return h
end

local function button(parent, width, text, primary, height)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height or 28)
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, 0.95); b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0); b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, primary and 12 or 12, primary and { 1, 0.92, 0.7 } or ES.TEXT)
    b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12) end end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0) end)
    b.Active = function(self, on)
        if on then self:Enable() else self:Disable(); self.glow:SetAlpha(0) end
        self.label:SetAlpha(on and 1 or 0.35)
    end
    return b
end

local function railRow(parent, onClick, withIcon)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(RAIL_W - 12, 28)
    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
    if withIcon then
        row.icon = row:CreateTexture(nil, "ARTWORK"); row.icon:SetSize(18, 18); row.icon:SetPoint("LEFT", 12, 0)
        row.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    end
    row.label = ES.Text(row, 12, ES.TEXT); row.label:SetPoint("LEFT", withIcon and 38 or 14, 0)
    row.label:SetPoint("RIGHT", -40, 0); row.label:SetJustifyH("LEFT")
    row.count = ES.Text(row, 11, ES.MUTED); row.count:SetPoint("RIGHT", -10, 0)
    row:SetScript("OnClick", function(self)
        if locked() then return end
        PlaySound("igMainMenuOptionCheckBoxOn"); onClick(self)
    end)
    row:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.06) end end)
    row:SetScript("OnLeave", function(self) if not self.selected then self.wash:SetAlpha(0) end end)
    row.Paint = function(self, label, on, count, icon)
        self.selected = on
        self.label:SetText(label)
        if self.icon then self.icon:SetTexture(icon) end
        self.wash:SetAlpha(on and 0.12 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        self.count:SetText(count or "")
    end
    return row
end

-- A filter switch: a small gold box, filled while on.
local function toggle(parent, label, key)
    local t = CreateFrame("Button", nil, parent)
    t:SetSize(RAIL_W - 12, 24)
    t.box = CreateFrame("Frame", nil, t); t.box:SetSize(12, 12); t.box:SetPoint("LEFT", 14, 0)
    ES.Solid(t.box, "BACKGROUND", ES.INK, 1):SetAllPoints()
    t.box.lines = ES.Border(t.box, ES.GOLD_DIM)
    t.fill = ES.Solid(t.box, "ARTWORK", ES.GOLD, 1); t.fill:SetPoint("TOPLEFT", 3, -3); t.fill:SetPoint("BOTTOMRIGHT", -3, 3)
    t.label = ES.Text(t, 11, ES.TEXT); t.label:SetPoint("LEFT", 34, 0); t.label:SetText(label)
    t:SetScript("OnClick", function()
        if locked() then return end
        PlaySound("igMainMenuOptionCheckBoxOn")
        db[key] = not db[key] or nil
        state.page = 1
        refresh()
    end)
    t.Paint = function(self)
        shown(self.fill, db[key]); ES.Tint(self.box.lines, db[key] and ES.GOLD or ES.GOLD_DIM, 1)
    end
    return t
end

-- Detail -------------------------------------------------------------------------------------------------------------
local scanner
local pool, usedRows = {}, 0

local function tag(parent)
    local t = CreateFrame("Frame", nil, parent); t:SetHeight(18)
    t.bg = ES.Solid(t, "BACKGROUND", ES.GOLD, 0.16); t.bg:SetAllPoints()
    t.lines = ES.Border(t, ES.GOLD, 0.8)
    t.text = ES.Text(t, 9, ES.GOLD); t.text:SetPoint("CENTER", 0, 1)
    t.Set = function(self, text, color)
        if not text then self:Hide(); return end
        self.text:SetText(text); self.text:SetTextColor(unpack(color))
        self.bg:SetVertexColor(color[1], color[2], color[3], 0.14); ES.Tint(self.lines, color, 0.8)
        self:SetWidth(self.text:GetStringWidth() + 14); self:Show()
    end
    return t
end

local function paintDetail()
    local s = pinned or shownSpell
    for i = 1, usedRows do pool[i].left:Hide(); pool[i].right:Hide() end
    usedRows = 0
    shown(detail.none, s == nil)
    for _, region in ipairs({ detail.icon, detail.title, detail.sub, detail.scroll }) do shown(region, s ~= nil) end
    detail.link:Active(s ~= nil); detail.pin:Active(s ~= nil)
    for _, t in ipairs(detail.tags) do t:Hide() end
    if not s then return end

    detail.icon.tex:SetTexture(s.icon)
    ES.Tint(detail.icon.lines, s.passive and ES.MUTED or ES.GOLD, 1)
    detail.title:SetText(s.name)
    local sub = { s.tab.name }
    if s.rank ~= "" then table.insert(sub, 1, s.rank) end
    detail.sub:SetText(table.concat(sub, "  ·  "))
    local labels = {}
    if s.passive then labels[#labels + 1] = { "PASSIVE", ES.MUTED } end
    if s.book == SPELL and not s.passive then
        labels[#labels + 1] = bound[s.name] and { "ON ACTION BARS", ES.GOOD } or { "NOT ON ACTION BARS", ES.BAD }
    end
    if s.book == PETBOOK and select(2, GetSpellAutocast(s.index, s.book)) then labels[#labels + 1] = { "AUTOCAST", ES.GOLD } end
    if pinned then labels[#labels + 1] = { "PINNED", ES.GOLD } end
    local x = 0
    for i, l in ipairs(labels) do
        local t = detail.tags[i]
        t:Set(l[1], l[2]); t:ClearAllPoints(); t:SetPoint("TOPLEFT", detail.sub, "BOTTOMLEFT", x, -8)
        x = x + t:GetWidth() + 6
    end

    -- The spell's own tooltip carries cost, range, cast time, cooldown and description, already worded and colored.
    scanner:SetOwner(UIParent, "ANCHOR_NONE")
    scanner:SetSpell(s.index, s.book)
    local child, width, y = detail.child, detail.width - 32, 0
    local function row()
        usedRows = usedRows + 1
        local r = pool[usedRows]
        if not r then
            r = { left = ES.Text(child, 12, ES.TEXT), right = ES.Text(child, 12, ES.TEXT) }
            r.left:SetJustifyH("LEFT"); r.right:SetJustifyH("RIGHT")
            pool[usedRows] = r
        end
        return r
    end
    for i = 2, scanner:NumLines() do
        local left = _G["EsteriaSpellbookScanTextLeft" .. i]
        local right = _G["EsteriaSpellbookScanTextRight" .. i]
        local lt, rt = left and left:GetText(), right and right:IsShown() and right:GetText()
        if lt and lt ~= "" then
            local r = row()
            r.left:SetTextColor(left:GetTextColor())
            r.left:SetWidth(rt and width - 110 or width); r.left:SetText(lt)
            r.left:ClearAllPoints(); r.left:SetPoint("TOPLEFT", 16, -y); r.left:Show()
            if rt then
                r.right:SetTextColor(right:GetTextColor()); r.right:SetText(rt)
                r.right:ClearAllPoints(); r.right:SetPoint("TOPRIGHT", child, "TOPRIGHT", -16, -y); r.right:Show()
            end
            y = y + r.left:GetStringHeight() + 6
        end
    end
    scanner:Hide()
    if #s.ranks > 1 then
        y = y + 8
        local r = row()
        r.left:SetTextColor(unpack(ES.GOLD_DIM)); r.left:SetWidth(width); r.left:SetText("RANKS KNOWN")
        r.left:ClearAllPoints(); r.left:SetPoint("TOPLEFT", 16, -y); r.left:Show()
        y = y + 16
        r = row()
        r.left:SetTextColor(unpack(ES.MUTED)); r.left:SetWidth(width); r.left:SetText(table.concat(s.ranks, ", "))
        r.left:ClearAllPoints(); r.left:SetPoint("TOPLEFT", 16, -y); r.left:Show()
        y = y + r.left:GetStringHeight() + 6
    end
    child:SetHeight(y + 16)
    detail.scroll:UpdateScrollChildRect()
    detail.scroll:SetVerticalScroll(0)
    detail.pin.label:SetText(pinned and "Unpin" or "Pin")
end

local function hover(s)
    shownSpell = s
    if not pinned then paintDetail() end
end

-- Grid ---------------------------------------------------------------------------------------------------------------
local function paintCard(c, s)
    c.icon:SetTexture(s.icon)
    c.icon:SetDesaturated(false)
    ES.Tint(c.iconLines, s.passive and ES.MUTED or ES.GOLD, s.passive and 0.7 or 0.9)
    c.name:SetText(s.name)
    c.name:SetTextColor(unpack(s == pinned and ES.GOLD or ES.TEXT))
    local sub = s.passive and "Passive" or (s.rank ~= "" and s.rank or "")
    if state.book == "all" or (search and search:GetText() ~= "") then
        sub = sub ~= "" and (sub .. "  ·  " .. s.tab.name) or s.tab.name
    end
    c.sub:SetText(sub)
    c.wash:SetAlpha(s == pinned and 0.12 or 0)
    local start, duration, enabled = GetSpellCooldown(s.index, s.book)
    CooldownFrame_SetTimer(c.cooldown, start or 0, duration or 0, enabled or 0)
    shown(c.mark, unbound(s))
    shown(c.auto, s.book == PETBOOK and select(2, GetSpellAutocast(s.index, s.book)) and true or false)
end

local function paintGrid()
    local inCombat = locked()
    local first = (state.page - 1) * PER_PAGE
    for i, c in ipairs(cards) do
        -- In combat a card keeps the spell it is armed with, so what it shows is always what it casts.
        if not inCombat then arm(c, filtered[first + i]) end
        if c.spell and c:IsShown() then paintCard(c, c.spell) end
    end
    if inCombat then pendingArm = true end
    shown(grid.empty, #filtered == 0)
    shown(banner, inCombat)
end

local function makeCard(i)
    local c = CreateFrame("Button", "EsteriaSpellbookCard" .. i, grid, "SecureActionButtonTemplate")
    c:SetSize(CARD_W, CARD_H)
    c:SetPoint("TOPLEFT", 12 + ((i - 1) % COLS) * (CARD_W + 8), -48 - math.floor((i - 1) / COLS) * ROW_STEP)
    c:RegisterForClicks("AnyUp")
    c:RegisterForDrag("LeftButton")
    c.plate = ES.Solid(c, "BACKGROUND", { 0.10, 0.11, 0.15 }, 0.6); c.plate:SetAllPoints()
    c.lines = ES.Border(c, ES.GOLD_DIM, 0.25)
    c.wash = ES.Solid(c, "ARTWORK", ES.GOLD, 0); c.wash:SetAllPoints()
    c.iconBox = CreateFrame("Frame", nil, c); c.iconBox:SetSize(40, 40); c.iconBox:SetPoint("LEFT", 5, 0)
    c.icon = c.iconBox:CreateTexture(nil, "ARTWORK")
    c.icon:SetPoint("TOPLEFT", 1, -1); c.icon:SetPoint("BOTTOMRIGHT", -1, 1); c.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    c.iconLines = ES.Border(c.iconBox, ES.GOLD)
    c.cooldown = CreateFrame("Cooldown", nil, c.iconBox); c.cooldown:SetAllPoints(c.icon)
    c.auto = ES.Text(c.iconBox, 9, ES.GOLD); c.auto:SetPoint("BOTTOM", 0, 2); c.auto:SetText("AUTO")
    c.name = ES.Text(c, 12, ES.TEXT); c.name:SetPoint("TOPLEFT", 52, -9); c.name:SetPoint("RIGHT", -14, 0)
    c.name:SetJustifyH("LEFT")
    c.sub = ES.Text(c, 10, ES.MUTED); c.sub:SetPoint("TOPLEFT", 52, -27); c.sub:SetPoint("RIGHT", -8, 0)
    c.sub:SetJustifyH("LEFT")
    c.mark = ES.Solid(c, "OVERLAY", ES.BAD, 0.9); c.mark:SetSize(6, 6); c.mark:SetPoint("TOPRIGHT", -5, -5)
    c:SetScript("OnEnter", function(self)
        if not self.spell then return end
        if self.spell ~= pinned then self.wash:SetAlpha(0.07) end
        hover(self.spell)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetSpell(self.spell.index, self.spell.book)
        if unbound(self.spell) then GameTooltip:AddLine("Not on your action bars", 0.95, 0.42, 0.36) end
        GameTooltip:AddLine(self.spell.book == PETBOOK and "Right-click: toggle autocast" or "Right-click: pin details",
            0.54, 0.43, 0.18)
        GameTooltip:Show()
    end)
    c:SetScript("OnLeave", function(self)
        if self.spell ~= pinned then self.wash:SetAlpha(0) end
        GameTooltip:Hide()
    end)
    c:SetScript("OnDragStart", function(self) pickup(self.spell) end)
    c:SetScript("PostClick", function(self, mouse)
        local s = self.spell
        if not s then return end
        if IsModifiedClick("CHATLINK") then link(s); return end
        if mouse == "RightButton" and s.book ~= PETBOOK then
            pinned = pinned ~= s and s or nil
            shownSpell = s
            refresh()
        end
    end)
    cards[i] = c
end

-- Rail ---------------------------------------------------------------------------------------------------------------
local function railEntries()
    local out = { { key = "all", name = "All spells", count = 0, icon = "Interface\\Icons\\INV_Misc_Book_09" } }
    for _, b in ipairs(books) do
        if not OWN_BOOK[b.key] then out[1].count = out[1].count + b.count end
        out[#out + 1] = b
    end
    return out
end

local function paintRail()
    local entries = railEntries()
    railOffset = math.max(0, math.min(railOffset, #entries - RAIL_ROWS))
    for i, row in ipairs(railRows) do
        local e = entries[i + railOffset]
        row.key = e and e.key
        if e then row:Paint(e.name, state.book == e.key, e.count, e.icon); row:Show() else row:Hide() end
        row:SetAlpha(locked() and 0.5 or 1)
    end
    for _, t in ipairs(toggles) do t:Paint() end
    toggles.sort.label:SetText(db.sort == "name" and "Order: A to Z" or "Order: as in the book")
end

local function paintHeader(pages)
    local passive, missing = 0, 0
    for _, s in ipairs(spells) do
        if s.passive then passive = passive + 1 end
        if unbound(s) then missing = missing + 1 end
    end
    counter:SetText(("|cffe8c46b%d|r spells     |cffe8c46b%d|r passive     |cffe8c46b%d|r not on action bars"):format(
        #spells, passive, missing))
    pageText:SetText(("Page %d / %d"):format(state.page, pages))
    prevPage:Active(not locked() and state.page > 1)
    nextPage:Active(not locked() and state.page < pages)
end

refresh = function()
    if not frame or not frame:IsShown() then return end
    if not locked() or not pendingArm then
        local pages = refilter()
        paintGrid()
        paintHeader(pages)
    else
        paintGrid()
    end
    paintRail()
    paintDetail()
end

local function turn(delta)
    if locked() then return end
    local pages = math.max(1, math.ceil(#filtered / PER_PAGE))
    local page = math.max(1, math.min(pages, state.page + delta))
    if page ~= state.page then
        state.page = page
        PlaySound("igAbiliityPageTurn")
        refresh()
    end
end

-- Build --------------------------------------------------------------------------------------------------------------
local keyOwner, lastKeys

local function bindKeys()
    if locked() then return end
    local book, petBook = { GetBindingKey("TOGGLESPELLBOOK") }, { GetBindingKey("TOGGLEPETBOOK") }
    local signature = table.concat(book, " ") .. "/" .. table.concat(petBook, " ")
    if signature == lastKeys then return end -- setting override bindings fires UPDATE_BINDINGS again
    lastKeys = signature
    ClearOverrideBindings(keyOwner)
    for _, key in ipairs(book) do SetOverrideBindingClick(keyOwner, false, key, "EsteriaSpellbookToggle", "LeftButton") end
    for _, key in ipairs(petBook) do SetOverrideBindingClick(keyOwner, false, key, "EsteriaSpellbookToggle", "RightButton") end
end

local function build()
    EsteriaSpellbookDB = EsteriaSpellbookDB or {}
    db = EsteriaSpellbookDB

    -- Secure show/hide: the key binding opens and closes it in combat, and Escape is bound while it is open.
    frame = CreateFrame("Frame", "EsteriaSpellbookFrame", UIParent, "SecureHandlerShowHideTemplate")
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) if not locked() then self:StartMoving() end end)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide()
    frame:SetAttribute("_onshow", [[ self:SetBindingClick(true, "ESCAPE", "EsteriaSpellbookClose") ]])
    frame:SetAttribute("_onhide", [[ self:ClearBindings() ]])
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(frame, ES.GOLD)
    local band = frame:CreateTexture(nil, "BORDER"); band:SetTexture(ES.WHITE)
    band:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.28)
    band:SetPoint("TOPLEFT", 1, -1); band:SetPoint("TOPRIGHT", -1, -1); band:SetHeight(42)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12); title:SetText(SPELLBOOK or "Spellbook")
    counter = ES.Text(frame, 11, ES.TEXT); counter:SetPoint("TOPRIGHT", -40, -15)

    local close = CreateFrame("Button", "EsteriaSpellbookClose", frame, "SecureHandlerClickTemplate")
    close:SetSize(32, 32); close:SetPoint("TOPRIGHT", 2, 2)
    close:SetNormalTexture("Interface\\Buttons\\UI-Panel-MinimizeButton-Up")
    close:SetPushedTexture("Interface\\Buttons\\UI-Panel-MinimizeButton-Down")
    close:SetHighlightTexture("Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight", "ADD")
    close:RegisterForClicks("AnyUp")
    SecureHandlerSetFrameRef(close, "book", frame)
    close:SetAttribute("_onclick", [[ self:GetFrameRef("book"):Hide() ]])

    local toggleButton = CreateFrame("Button", "EsteriaSpellbookToggle", UIParent, "SecureHandlerClickTemplate")
    toggleButton:RegisterForClicks("AnyUp")
    SecureHandlerSetFrameRef(toggleButton, "book", frame)
    toggleButton:SetAttribute("_onclick", [[
        local book = self:GetFrameRef("book")
        if book:IsShown() then book:Hide() else book:Show() end
    ]])
    toggleButton:HookScript("OnClick", function(_, mouse)
        if mouse == "RightButton" and frame:IsShown() and not locked() then state.book, state.page = "pet", 1; refresh() end
    end)

    local pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")

    -- Rail: books with counts, then filters.
    local rail = panel(pane); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    heading(rail, "Books"):SetPoint("TOPLEFT", 14, -12)
    local bookList = CreateFrame("Frame", nil, rail); bookList:SetPoint("TOPLEFT", 6, -28)
    bookList:SetSize(RAIL_W - 12, RAIL_ROWS * 29)
    bookList:EnableMouseWheel(true)
    bookList:SetScript("OnMouseWheel", function(_, delta) railOffset = math.max(0, railOffset - delta); paintRail() end)
    for i = 1, RAIL_ROWS do
        railRows[i] = railRow(bookList, function(self) state.book, state.page = self.key, 1; search:SetText(""); refresh() end, true)
        railRows[i]:SetPoint("TOPLEFT", 0, -(i - 1) * 29)
    end
    heading(rail, "Filters"):SetPoint("TOPLEFT", 14, -(28 + RAIL_ROWS * 29 + 12))
    local fy = -(28 + RAIL_ROWS * 29 + 28)
    for i, spec in ipairs({ { "Hide passives", "hidePassive" }, { "Not on action bars", "unbound" } }) do
        toggles[i] = toggle(rail, spec[1], spec[2]); toggles[i]:SetPoint("TOPLEFT", 6, fy - (i - 1) * 26)
    end
    toggles.sort = button(rail, RAIL_W - 28, "", false, 24); toggles.sort:SetPoint("BOTTOMLEFT", 14, 14)
    toggles.sort:SetScript("OnClick", function()
        if locked() then return end
        PlaySound("igMainMenuOptionCheckBoxOn"); db.sort = db.sort ~= "name" and "name" or nil; state.page = 1; refresh()
    end)

    -- Grid: search, cards, pager.
    grid = panel(pane); grid:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); grid:SetPoint("BOTTOM", 0, 14); grid:SetWidth(GRID_W)
    search = CreateFrame("EditBox", "EsteriaSpellbookSearch", grid)
    search:SetSize(GRID_W - 24, 26); search:SetPoint("TOPLEFT", 12, -12); search:SetAutoFocus(false)
    search:SetFontObject(ChatFontNormal); search:SetTextInsets(10, 8, 0, 0)
    ES.Solid(search, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    search.lines = ES.Border(search, ES.GOLD_DIM)
    local placeholder = ES.Text(search, 11, ES.MUTED); placeholder:SetPoint("LEFT", 10, 0)
    placeholder:SetText("Search every spell by name, rank or book")
    search:SetScript("OnEscapePressed", function(self) if self:GetText() ~= "" then self:SetText("") else self:ClearFocus() end end)
    search:SetScript("OnEnterPressed", search.ClearFocus)
    search:SetScript("OnEditFocusGained", function(self)
        if locked() then self:ClearFocus(); return end
        ES.Tint(self.lines, ES.GOLD, 1)
    end)
    search:SetScript("OnEditFocusLost", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 1) end)
    search:SetScript("OnTextChanged", function(self) shown(placeholder, self:GetText() == ""); state.page = 1; refresh() end)
    for i = 1, PER_PAGE do makeCard(i) end
    grid.empty = ES.Text(grid, 12, ES.MUTED); grid.empty:SetPoint("TOP", 0, -120); grid.empty:SetText("No spells match.")
    banner = ES.Text(grid, 11, ES.BAD); banner:SetPoint("BOTTOMLEFT", 14, 20)
    banner:SetText("In combat: casting works, browsing waits until combat ends.")
    prevPage = button(grid, 28, "<", false, 24); prevPage:SetPoint("BOTTOMRIGHT", -118, 12)
    nextPage = button(grid, 28, ">", false, 24); nextPage:SetPoint("BOTTOMRIGHT", -12, 12)
    pageText = ES.Text(grid, 11, ES.MUTED); pageText:SetPoint("RIGHT", nextPage, "LEFT", -10, 0)
    prevPage:SetScript("OnClick", function() turn(-1) end)
    nextPage:SetScript("OnClick", function() turn(1) end)
    grid:EnableMouseWheel(true)
    grid:SetScript("OnMouseWheel", function(_, delta) turn(-delta) end)

    -- Detail.
    detail = panel(pane); detail:SetPoint("TOPLEFT", grid, "TOPRIGHT", 14, 0); detail:SetPoint("BOTTOMRIGHT", -14, 14)
    detail.width = W - (14 + RAIL_W + 14 + GRID_W + 14) - 14
    local tint = detail:CreateTexture(nil, "BORDER"); tint:SetTexture(ES.WHITE)
    tint:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    tint:SetPoint("TOPLEFT", 1, -1); tint:SetPoint("TOPRIGHT", -1, -1); tint:SetHeight(110)
    detail.icon = CreateFrame("Button", nil, detail); detail.icon:SetSize(58, 58); detail.icon:SetPoint("TOPLEFT", 16, -16)
    detail.icon.tex = detail.icon:CreateTexture(nil, "ARTWORK")
    detail.icon.tex:SetPoint("TOPLEFT", 1, -1); detail.icon.tex:SetPoint("BOTTOMRIGHT", -1, 1)
    detail.icon.tex:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    detail.icon.lines = ES.Border(detail.icon, ES.GOLD)
    detail.icon:RegisterForDrag("LeftButton")
    detail.icon:SetScript("OnDragStart", function() pickup(pinned or shownSpell) end)
    detail.icon:SetScript("OnClick", function() pickup(pinned or shownSpell) end)
    detail.title = ES.Text(detail, 16, ES.GOLD); detail.title:SetPoint("TOPLEFT", 86, -20); detail.title:SetPoint("RIGHT", -14, 0)
    detail.title:SetJustifyH("LEFT")
    detail.sub = ES.Text(detail, 11, ES.MUTED); detail.sub:SetPoint("TOPLEFT", detail.title, "BOTTOMLEFT", 0, -5)
    detail.sub:SetPoint("RIGHT", -14, 0); detail.sub:SetJustifyH("LEFT")
    detail.tags = { tag(detail), tag(detail), tag(detail) }
    detail.none = ES.Text(detail, 12, ES.MUTED); detail.none:SetPoint("CENTER"); detail.none:SetWidth(detail.width - 40)
    detail.none:SetText("Point at a spell to see its details. Right-click a spell to keep it here.")
    detail.scroll = CreateFrame("ScrollFrame", nil, detail)
    detail.scroll:SetPoint("TOPLEFT", 0, -112); detail.scroll:SetPoint("BOTTOMRIGHT", 0, 52)
    detail.child = CreateFrame("Frame", nil, detail.scroll); detail.child:SetSize(detail.width, 10)
    detail.scroll:SetScrollChild(detail.child)
    detail.scroll:EnableMouseWheel(true)
    detail.scroll:SetScript("OnMouseWheel", function(self, delta)
        local range = math.max(0, detail.child:GetHeight() - self:GetHeight())
        self:SetVerticalScroll(math.max(0, math.min(range, self:GetVerticalScroll() - delta * 40)))
    end)
    local rule = ES.Solid(detail, "ARTWORK", ES.GOLD_DIM, 0.4)
    rule:SetPoint("BOTTOMLEFT", 12, 50); rule:SetPoint("BOTTOMRIGHT", -12, 50); rule:SetHeight(1)
    local bw = math.floor((detail.width - 30) / 2)
    detail.link = button(detail, bw, "Link in chat", true); detail.link:SetPoint("BOTTOMLEFT", 12, 12)
    detail.pin = button(detail, bw, "Pin"); detail.pin:SetPoint("BOTTOMLEFT", 18 + bw, 12)
    detail.link:SetScript("OnClick", function() link(pinned or shownSpell) end)
    detail.pin:SetScript("OnClick", function()
        local s = pinned or shownSpell
        pinned = pinned == nil and s or nil
        refresh()
    end)
    scanner = CreateFrame("GameTooltip", "EsteriaSpellbookScan", UIParent, "GameTooltipTemplate")

    frame:HookScript("OnShow", function()
        PlaySound("igSpellBookOpen")
        dirty, barsDirty = true, true
        if SpellbookMicroButton then SpellbookMicroButton:SetButtonState("PUSHED", 1) end
    end)
    frame:HookScript("OnHide", function()
        PlaySound("igSpellBookClose")
        GameTooltip:Hide()
        if SpellbookMicroButton then SpellbookMicroButton:SetButtonState("NORMAL") end
    end)
    frame:SetScript("OnUpdate", function()
        if dirty then dirty = false; scan(); refresh() end
        if barsDirty then barsDirty = false; scanBars(); refresh() end
        if cooldownsDirty then
            cooldownsDirty = false
            for _, c in ipairs(cards) do
                if c.spell and c:IsShown() then
                    local start, duration, enabled = GetSpellCooldown(c.spell.index, c.spell.book)
                    CooldownFrame_SetTimer(c.cooldown, start or 0, duration or 0, enabled or 0)
                end
            end
        end
    end)

    local events = CreateFrame("Frame")
    keyOwner = events
    for _, ev in ipairs({ "SPELLS_CHANGED", "LEARNED_SPELL_IN_TAB", "SPELL_UPDATE_COOLDOWN", "ACTIONBAR_SLOT_CHANGED",
                          "PET_BAR_UPDATE", "UNIT_PET", "PLAYER_REGEN_DISABLED", "PLAYER_REGEN_ENABLED",
                          "UPDATE_BINDINGS" }) do
        events:RegisterEvent(ev)
    end
    events:SetScript("OnEvent", function(_, ev)
        if ev == "SPELL_UPDATE_COOLDOWN" then cooldownsDirty = true
        elseif ev == "ACTIONBAR_SLOT_CHANGED" then barsDirty = true
        elseif ev == "UPDATE_BINDINGS" then bindKeys()
        elseif ev == "PLAYER_REGEN_DISABLED" then refresh()
        elseif ev == "PLAYER_REGEN_ENABLED" then
            pendingArm = false
            bindKeys()
            refresh()
        else dirty = true end
    end)

    -- Arm the first page now (out of combat), so a first opening in combat already casts.
    scan(); scanBars(); refilter(); paintGrid()
    bindKeys()
end

-- Opening ------------------------------------------------------------------------------------------------------------
local stockToggle = ToggleSpellBook

function EsteriaSpellbook_Open(bookKey)
    if not frame or locked() then return end
    if bookKey then state.book, state.page = bookKey, 1 end
    if frame:IsShown() then refresh() else frame:Show() end
end

-- The micro button and anything else calling ToggleSpellBook land here. In combat this window can only be opened by
-- its key binding (a secure click), so the stock book answers instead.
ToggleSpellBook = function(bookType)
    if not frame or locked() then
        if stockToggle then return stockToggle(bookType) end
        return
    end
    local key = bookType == PETBOOK and "pet" or nil
    if frame:IsShown() and (not key or state.book == key) then
        frame:Hide()
    else
        if not key and state.book == "pet" then key = "all" end
        EsteriaSpellbook_Open(key)
    end
end

if SpellBookFrame then
    SpellBookFrame:HookScript("OnShow", function(self)
        if not frame or locked() then return end -- in combat the stock book stays usable
        local pet = self.bookType == PETBOOK
        HideUIPanel(self)
        EsteriaSpellbook_Open(pet and "pet" or nil)
    end)
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:SetScript("OnEvent", function() build() end)

SLASH_ESTERIASPELLBOOK1 = "/spellbook"
SLASH_ESTERIASPELLBOOK2 = "/sb"
SlashCmdList.ESTERIASPELLBOOK = function() ToggleSpellBook(SPELL) end
