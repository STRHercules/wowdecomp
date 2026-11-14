-- Esteria Emotes (/emotes): your dance collection, laid out like Incarnations (rail, 3x2 live cards, stage), plus the
-- emote wheel editor. Emotes are EsteriaEmotesData (WoW-Dance-Studio emote_addon_data.py). The realm keeps what you
-- have collected and your wheel, account-wide (EsteriaWoW mod-esteria-emotes); wire: addon whisper to self, ESTEMO.
-- Previews: Model:SetSequence(emote id) on a PlayerModel of you (the client allows ids past 506 for this).
local ES = EsteriaStyle
local PREFIX = "ESTEMO"
local W, H, RAIL_W, MARGIN = 1180, 680, 236, 14
local COLS, ROWS = 3, 2
local PER_PAGE = COLS * ROWS
local CARD_W, CARD_H, GAP = 170, 248, 12
local GRID_W = COLS * CARD_W + (COLS - 1) * GAP
local STAGE_W = W - MARGIN - (MARGIN + RAIL_W + 18 + GRID_W + 18)
local WHEEL_PAGES, WHEEL_SIZE = 4, 8

-- Shared with EsteriaEmotesWheel.lua
EsteriaEmotes = { known = {}, wheel = {}, byId = {}, list = {}, listeners = {} }
local EE = EsteriaEmotes
for _, row in ipairs(EsteriaEmotesData or {}) do
    local emote = { id = row[1], name = row[2], slash = row[3], icon = row[4], duration = row[5] }
    EE.list[#EE.list + 1] = emote
    EE.byId[emote.id] = emote
end
for i = 1, WHEEL_PAGES * WHEEL_SIZE do EE.wheel[i] = 0 end

local db                                      -- EsteriaEmotesDB: favorites and seen flags (this client only)
local frame, grid, stage, search, pageText, prevButton, nextButton, danceButton, favoriteButton, wheelButton
local cards, railRows, wheelSlots = {}, {}, {}
local filtered, filter, page, focus, wheelPage = {}, "all", 1, nil, 1
local dragging, ghost                         -- { emote = id, fromSlot = n }, cursor icon

-- ---------------------------------------------------------------------------------------------------------------
-- realm traffic
-- ---------------------------------------------------------------------------------------------------------------
local function send(body) SendAddonMessage(PREFIX, body, "WHISPER", UnitName("player")) end
local function notify() for _, f in ipairs(EE.listeners) do f() end end

function EE.Play(id) if id and id > 0 then send("play\t" .. id) end end
function EE.Stop() send("stop") end
function EE.SetSlot(slot, id) send("slot\t" .. (slot - 1) .. "\t" .. (id or 0)) end
function EE.SlotOf(id)
    for i, v in ipairs(EE.wheel) do if v == id then return i end end
end
function EE.FirstEmpty()
    for i, v in ipairs(EE.wheel) do if v == 0 then return i end end
end

local syncing
local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, prefix, message, _, sender)
    if event == "PLAYER_LOGIN" then
        EsteriaEmotesDB = EsteriaEmotesDB or {}
        db = EsteriaEmotesDB
        db.favorites, db.seen = db.favorites or {}, db.seen or {}
        -- Hold CTRL-G for the wheel, unless that key is already taken.
        if (GetBindingAction("CTRL-G") or "") == "" then
            SetBinding("CTRL-G", "ESTERIAEMOTES_WHEEL")
            SaveBindings(GetCurrentBindingSet())
        end
        send("sync")
        return
    end
    if prefix ~= PREFIX or sender ~= UnitName("player") then return end
    local kind, a, b = strsplit("\t", message)
    if kind == "reset" then
        syncing = {}
    elseif kind == "known" and syncing then
        for id in string.gmatch(a or "", "%d+") do syncing[tonumber(id)] = true end
    elseif kind == "wheel" then
        local i = 1
        for id in string.gmatch(a or "", "%d+") do EE.wheel[i] = tonumber(id); i = i + 1 end
    elseif kind == "done" and syncing then
        EE.known, syncing = syncing, nil
    elseif kind == "slot" then
        EE.wheel[tonumber(a) + 1] = tonumber(b)
        PlaySound("igMainMenuOptionCheckBoxOn")
    elseif kind == "err" then
        UIErrorsFrame:AddMessage(a or "That emote is not available.", 1, 0.1, 0.1)
    end
    notify()
end)

-- /emotes opens the window; every emote's own slash command dances it (/floss, /getgriddy, ...).
for _, emote in ipairs(EE.list) do
    local key = "ESTERIAEMOTE" .. emote.id
    _G["SLASH_" .. key .. "1"] = "/" .. emote.slash
    SlashCmdList[key] = function() EE.Play(emote.id) end
end

-- ---------------------------------------------------------------------------------------------------------------
-- helpers
-- ---------------------------------------------------------------------------------------------------------------
local function shown(region, on) if on then region:Show() else region:Hide() end end

local function flatButton(parent, width, height, text, primary)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height)
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, primary and 0.95 or 0.9)
    b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0)
    b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, primary and 13 or 12, primary and { 1, 0.92, 0.7 } or ES.TEXT)
    b.label:SetPoint("CENTER", 0, 1)
    b.label:SetText(text)
    b:SetScript("OnEnter", function(self)
        if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12); ES.Tint(self.lines, ES.GOLD, 1) end
    end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0); ES.Tint(self.lines, primary and ES.GOLD or ES.GOLD_DIM, 1) end)
    b.SetLabel = function(self, t) self.label:SetText(t) end
    b.SetActive = function(self, on)
        if on then self:Enable(); self.label:SetAlpha(1); self.bg:SetAlpha(primary and 0.95 or 0.9)
        else self:Disable(); self.label:SetAlpha(0.35); self.bg:SetAlpha(0.5); self.glow:SetAlpha(0) end
    end
    return b
end

local function iconFrame(parent, size)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetSize(size + 2, size + 2)
    holder.icon = holder:CreateTexture(nil, "ARTWORK")
    holder.icon:SetPoint("TOPLEFT", 1, -1)
    holder.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    holder.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    holder.lines = ES.Border(holder, ES.GOLD_DIM)
    return holder
end

-- A PlayerModel of you looping one emote. The model frame drops back to Stand when an emote sequence ends (it never
-- reports OnAnimFinished for them), so the preview drives the clock itself: SetSequenceTime every frame, wrapped at
-- the emote's cycle length, never lets the sequence end.
local function danceModel(parent)
    local model = CreateFrame("PlayerModel", nil, parent)
    model.facing = -0.4
    model:SetScript("OnShow", function(self) self:SetUnit("player"); self:SetFacing(self.facing) end)
    model:SetScript("OnUpdate", function(self, elapsed)
        if self.emote then
            self.clock = (self.clock + elapsed * 1000) % self.duration
            self:SetSequenceTime(self.emote, self.clock)
        end
        if self.dragX then
            local x = GetCursorPosition()
            self.facing = self.facing + (x - self.dragX) * 0.012
            self.dragX = x
            self:SetFacing(self.facing)
        elseif self.spin then
            self.facing = self.facing + elapsed * self.spin
            self:SetFacing(self.facing)
        end
    end)
    model.Dance = function(self, id)
        local emote = EE.byId[id]
        self.emote, self.duration, self.clock = emote and id, emote and math.max(emote.duration or 0, 1), 0
        if not self.emote then self:SetSequence(0) end
    end
    return model
end

-- ---------------------------------------------------------------------------------------------------------------
-- state
-- ---------------------------------------------------------------------------------------------------------------
local refresh

local FILTERS = {
    { key = "all",     group = "COLLECTION", label = "All emotes",    test = function() return true end },
    { key = "known",   group = "COLLECTION", label = "Collected",     test = function(e) return EE.known[e.id] end },
    { key = "fav",     group = "COLLECTION", label = "Favorites",     test = function(e) return db and db.favorites[e.id] end },
    { key = "new",     group = "COLLECTION", label = "New",           test = function(e) return EE.known[e.id] and db and not db.seen[e.id] end },
    { key = "wheel",   group = "FILTER",     label = "On my wheel",   test = function(e) return EE.SlotOf(e.id) ~= nil end },
    { key = "locked",  group = "FILTER",     label = "Not collected", test = function(e) return not EE.known[e.id] end },
}
local FILTER_BY_KEY = {}
for _, f in ipairs(FILTERS) do FILTER_BY_KEY[f.key] = f end

local function choose(id) focus = id; refresh() end

local function toggleWheel(id)
    local slot = EE.SlotOf(id)
    if slot then EE.SetSlot(slot, 0)
    elseif EE.known[id] then
        local empty = EE.FirstEmpty()
        if empty then EE.SetSlot(empty, id) else UIErrorsFrame:AddMessage("Your emote wheel is full.", 1, 0.1, 0.1) end
    end
end

-- Drag a card or a wheel slot; dropping on a wheel slot assigns (or swaps), dropping a slot elsewhere clears it.
local function beginDrag(id, fromSlot)
    if not EE.known[id] then return end
    dragging = { emote = id, fromSlot = fromSlot }
    ghost.icon:SetTexture(EE.byId[id].icon)
    ghost:Show()
end

local function endDrag()
    if not dragging then return end
    local target
    for i, slot in ipairs(wheelSlots) do if slot:IsShown() and MouseIsOver(slot) then target = (wheelPage - 1) * WHEEL_SIZE + i end end
    local d = dragging
    dragging = nil
    ghost:Hide()
    if target and target ~= d.fromSlot then
        if d.fromSlot then EE.SetSlot(d.fromSlot, EE.wheel[target]) end   -- swap; an empty target just moves it
        EE.SetSlot(target, d.emote)
    elseif not target and d.fromSlot then
        EE.SetSlot(d.fromSlot, 0)
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- rail
-- ---------------------------------------------------------------------------------------------------------------
local function buildRail()
    local rail = CreateFrame("Frame", nil, frame)
    rail:SetPoint("TOPLEFT", 14, -64)
    rail:SetPoint("BOTTOMLEFT", 14, 16)
    rail:SetWidth(RAIL_W)
    ES.Solid(rail, "BACKGROUND", { 0.05, 0.06, 0.08 }, 0.9):SetAllPoints()
    ES.Border(rail, ES.GOLD_DIM, 0.6)
    local y, group = -10, nil
    for _, f in ipairs(FILTERS) do
        if f.group ~= group then
            group = f.group
            if y < -10 then y = y - 8 end
            local head = ES.Text(rail, 10, ES.GOLD_DIM)
            head:SetPoint("TOPLEFT", 14, y)
            head:SetText(group)
            y = y - 18
        end
        local row = CreateFrame("Button", nil, rail)
        row:SetSize(RAIL_W - 12, 32)
        row:SetPoint("TOPLEFT", 6, y)
        row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0)
        row.wash:SetAllPoints()
        row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
        row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
        row.name = ES.Text(row, 12, ES.TEXT)
        row.name:SetPoint("LEFT", 14, 0)
        row.name:SetText(f.label)
        row.count = ES.Text(row, 11, ES.MUTED)
        row.count:SetPoint("RIGHT", -12, 0)
        row.filter = f
        row:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); filter = f.key; page = 1; refresh() end)
        row:SetScript("OnEnter", function(self) if self.filter.key ~= filter then self.wash:SetAlpha(0.06) end end)
        row:SetScript("OnLeave", function(self) if self.filter.key ~= filter then self.wash:SetAlpha(0) end end)
        railRows[#railRows + 1] = row
        y = y - 34
    end
end

local function paintRail()
    for _, row in ipairs(railRows) do
        local on, n = row.filter.key == filter, 0
        for _, e in ipairs(EE.list) do if row.filter.test(e) then n = n + 1 end end
        row.wash:SetAlpha(on and 0.12 or 0)
        row.accent:SetAlpha(on and 1 or 0)
        row.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        row.count:SetText(n)
        row.count:SetTextColor(unpack(row.filter.key == "new" and n > 0 and ES.GOOD or ES.MUTED))
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- cards
-- ---------------------------------------------------------------------------------------------------------------
local function badge(parent, text, color, fill)
    local b = CreateFrame("Frame", nil, parent)
    b:SetSize(40, 16)
    ES.Solid(b, "BACKGROUND", fill, 0.95):SetAllPoints()
    ES.Border(b, color)
    b.text = ES.Text(b, 9, color)
    b.text:SetPoint("CENTER", 0, 0)
    b.text:SetText(text)
    return b
end

local function buildCard(i)
    local col, row = (i - 1) % COLS, math.floor((i - 1) / COLS)
    local card = CreateFrame("Button", nil, grid)
    card:SetSize(CARD_W, CARD_H)
    card:SetPoint("TOPLEFT", col * (CARD_W + GAP), -row * (CARD_H + GAP))
    card.bg = ES.Solid(card, "BACKGROUND", { 0.06, 0.07, 0.10 }, 1)
    card.bg:SetAllPoints()
    local floor = card:CreateTexture(nil, "BORDER")
    floor:SetTexture(ES.WHITE)
    floor:SetGradientAlpha("VERTICAL", 0.55, 0.42, 0.16, 0.22, 0.55, 0.42, 0.16, 0)
    floor:SetPoint("BOTTOMLEFT", 0, 46); floor:SetPoint("BOTTOMRIGHT", 0, 46); floor:SetHeight(90)
    card.lines = ES.Border(card, ES.GOLD_DIM, 0.7)
    card.model = danceModel(card)
    card.model:SetPoint("TOPLEFT", 2, -2)
    card.model:SetPoint("BOTTOMRIGHT", -2, 46)
    card.lock = iconFrame(card, 48)
    card.lock:SetPoint("CENTER", 0, 24)
    card.lock.icon:SetTexture("Interface\\Icons\\INV_Misc_Key_03")
    card.lock.icon:SetDesaturated(1)
    card.plate = ES.Solid(card, "ARTWORK", ES.INK, 0.92)
    card.plate:SetPoint("BOTTOMLEFT", 1, 1); card.plate:SetPoint("BOTTOMRIGHT", -1, 1); card.plate:SetHeight(45)
    card.name = ES.Text(card, 12, ES.TEXT)
    card.name:SetPoint("TOPLEFT", card.plate, "TOPLEFT", 8, -8)
    card.name:SetPoint("TOPRIGHT", card.plate, "TOPRIGHT", -8, -8)
    card.slash = ES.Text(card, 10, ES.MUTED)
    card.slash:SetPoint("TOP", card.name, "BOTTOM", 0, -4)
    card.slotBadge = badge(card, "1", ES.GOOD, { 0.16, 0.32, 0.12 })
    card.slotBadge:SetPoint("TOPRIGHT", -6, -6)
    card.newBadge = badge(card, "NEW", ES.GOLD, { 0.30, 0.22, 0.06 })
    card.newBadge:SetPoint("TOPLEFT", 6, -6)
    for _, b in ipairs({ card.slotBadge, card.newBadge }) do b:SetFrameLevel(card.model:GetFrameLevel() + 2) end
    card:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    card:RegisterForDrag("LeftButton")
    card:SetScript("OnClick", function(self, button)
        PlaySound("igMainMenuOptionCheckBoxOn")
        if button == "RightButton" then toggleWheel(self.emote.id) else choose(self.emote.id) end
    end)
    card:SetScript("OnDoubleClick", function(self) choose(self.emote.id); EE.Play(self.emote.id) end)
    card:SetScript("OnDragStart", function(self) beginDrag(self.emote.id) end)
    card:SetScript("OnDragStop", endDrag)
    card:SetScript("OnEnter", function(self)
        self.model.spin = 0.9
        if self.emote.id ~= focus then ES.Tint(self.lines, ES.GOLD, 0.9) end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(self.emote.name, 1, 0.82, 0.1)
        if EE.known[self.emote.id] then
            GameTooltip:AddLine("Click: preview.  Double-click: dance.", 0.6, 1, 0.6)
            GameTooltip:AddLine("Drag onto the wheel, or right-click to add it.", 0.6, 1, 0.6)
        else
            GameTooltip:AddLine("Not collected yet.", 0.9, 0.4, 0.35)
        end
        GameTooltip:Show()
    end)
    card:SetScript("OnLeave", function(self)
        self.model.spin = nil
        if self.emote.id ~= focus then ES.Tint(self.lines, ES.GOLD_DIM, 0.7) end
        GameTooltip:Hide()
    end)
    card:EnableMouseWheel(true)
    card:SetScript("OnMouseWheel", function(_, delta) page = page - delta; refresh() end)
    return card
end

local function paintGrid()
    local pages = math.max(1, math.ceil(#filtered / PER_PAGE))
    page = math.min(math.max(page, 1), pages)
    pageText:SetText(("Page %d / %d"):format(page, pages))
    prevButton:SetActive(page > 1)
    nextButton:SetActive(page < pages)
    for i = 1, PER_PAGE do
        local card, emote = cards[i], filtered[(page - 1) * PER_PAGE + i]
        if emote then
            local known = EE.known[emote.id]
            card.emote = emote
            card.name:SetText(emote.name)
            card.name:SetTextColor(unpack(known and ES.TEXT or ES.MUTED))
            card.slash:SetText(known and ("/" .. emote.slash) or "Not collected")
            shown(card.model, known)
            shown(card.lock, not known)
            if known and card.model.emote ~= emote.id then card.model:Dance(emote.id) end
            local slot = EE.SlotOf(emote.id)
            shown(card.slotBadge, slot ~= nil)
            if slot then card.slotBadge.text:SetText(slot) end
            shown(card.newBadge, known and db and not db.seen[emote.id])
            local on = emote.id == focus
            ES.Tint(card.lines, on and ES.GOLD or ES.GOLD_DIM, on and 1 or 0.7)
            card.bg:SetVertexColor(on and 0.13 or 0.06, on and 0.11 or 0.07, on and 0.07 or 0.10)
            card:Show()
        else
            card.emote = nil
            card:Hide()
        end
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- stage and wheel editor
-- ---------------------------------------------------------------------------------------------------------------
local function buildWheelEditor(parent)
    local editor = CreateFrame("Frame", nil, parent)
    editor:SetPoint("BOTTOMLEFT", 1, 1); editor:SetPoint("BOTTOMRIGHT", -1, 1); editor:SetHeight(214)
    ES.Solid(editor, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    local rule = ES.Solid(editor, "ARTWORK", ES.GOLD_DIM, 0.8)
    rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    local head = ES.Text(editor, 10, ES.GOLD_DIM)
    head:SetPoint("TOPLEFT", 16, -10)
    head:SetText("EMOTE WHEEL")
    editor.pageText = ES.Text(editor, 11, ES.TEXT)
    editor.pageText:SetPoint("TOPRIGHT", -44, -10)
    local prev = flatButton(editor, 22, 18, "<")
    prev:SetPoint("RIGHT", editor.pageText, "LEFT", -8, 0)
    prev:SetScript("OnClick", function() wheelPage = (wheelPage - 2) % WHEEL_PAGES + 1; refresh() end)
    local nxt = flatButton(editor, 22, 18, ">")
    nxt:SetPoint("LEFT", editor.pageText, "RIGHT", 8, 0)
    nxt:SetScript("OnClick", function() wheelPage = wheelPage % WHEEL_PAGES + 1; refresh() end)
    local ring = CreateFrame("Frame", nil, editor)
    ring:SetSize(180, 160)
    ring:SetPoint("TOP", 0, -30)
    editor.hub = ES.Text(ring, 12, ES.MUTED)
    editor.hub:SetPoint("CENTER", 0, 0)
    for i = 1, WHEEL_SIZE do
        local a = (i - 1) * 2 * math.pi / WHEEL_SIZE
        local slot = CreateFrame("Button", nil, ring)
        slot:SetSize(36, 36)
        slot:SetPoint("CENTER", ring, "CENTER", math.sin(a) * 62, math.cos(a) * 62)
        ES.Solid(slot, "BACKGROUND", { 0.06, 0.07, 0.10 }, 1):SetAllPoints()
        slot.lines = ES.Border(slot, ES.GOLD_DIM, 0.8)
        slot.icon = slot:CreateTexture(nil, "ARTWORK")
        slot.icon:SetPoint("TOPLEFT", 2, -2); slot.icon:SetPoint("BOTTOMRIGHT", -2, 2)
        slot.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        slot.index = i
        slot:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        slot:RegisterForDrag("LeftButton")
        local function absolute() return (wheelPage - 1) * WHEEL_SIZE + i end
        slot:SetScript("OnClick", function(_, button)
            local id = EE.wheel[absolute()]
            if button == "RightButton" and id ~= 0 then EE.SetSlot(absolute(), 0)
            elseif id ~= 0 then choose(id) end
        end)
        slot:SetScript("OnDragStart", function() local id = EE.wheel[absolute()]; if id ~= 0 then beginDrag(id, absolute()) end end)
        slot:SetScript("OnDragStop", endDrag)
        slot:SetScript("OnEnter", function(self)
            ES.Tint(self.lines, ES.GOLD, 1)
            local emote = EE.byId[EE.wheel[absolute()]]
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            if emote then
                GameTooltip:SetText(emote.name, 1, 0.82, 0.1)
                GameTooltip:AddLine("Drag to move, right-click to clear.", 0.6, 1, 0.6)
            else
                GameTooltip:SetText("Empty slot", 0.7, 0.7, 0.7)
                GameTooltip:AddLine("Drag an emote here.", 0.6, 1, 0.6)
            end
            GameTooltip:Show()
        end)
        slot:SetScript("OnLeave", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 0.8); GameTooltip:Hide() end)
        wheelSlots[i] = slot
    end
    local hint = ES.Text(editor, 10, ES.MUTED)
    hint:SetPoint("BOTTOM", 0, 10)
    hint:SetText("Hold the wheel key in game (CTRL-G unless rebound)")
    return editor
end

local function paintWheelEditor()
    local filled = 0
    for i, slot in ipairs(wheelSlots) do
        local emote = EE.byId[EE.wheel[(wheelPage - 1) * WHEEL_SIZE + i]]
        slot.icon:SetTexture(emote and emote.icon or nil)
        if emote then filled = filled + 1 end
        ES.Tint(slot.lines, emote and emote.id == focus and ES.GOLD or ES.GOLD_DIM, 0.8)
    end
    stage.editor.hub:SetText(filled .. "/" .. WHEEL_SIZE)
    stage.editor.pageText:SetText(wheelPage .. "/" .. WHEEL_PAGES)
end

local function buildStage()
    stage = CreateFrame("Frame", nil, frame)
    stage:SetPoint("TOPRIGHT", -14, -64)
    stage:SetPoint("BOTTOMRIGHT", -14, 16)
    stage:SetWidth(STAGE_W)
    ES.Solid(stage, "BACKGROUND", { 0.05, 0.05, 0.07 }, 1):SetAllPoints()
    local glow = stage:CreateTexture(nil, "BORDER")
    glow:SetTexture(ES.WHITE)
    glow:SetGradientAlpha("VERTICAL", 0.62, 0.46, 0.15, 0.3, 0.62, 0.46, 0.15, 0)
    glow:SetPoint("BOTTOMLEFT", 0, 340); glow:SetPoint("BOTTOMRIGHT", 0, 340); glow:SetHeight(120)
    ES.Border(stage, ES.GOLD, 0.8)
    stage.model = danceModel(stage)
    stage.model:SetPoint("TOPLEFT", 4, -4)
    stage.model:SetPoint("BOTTOMRIGHT", -4, 340)
    stage.model.spin = 0.25
    stage.model.zoom = 0
    stage.model:EnableMouse(true)
    stage.model:SetScript("OnMouseDown", function(self) self.dragX, self.spin = GetCursorPosition(), nil end)
    stage.model:SetScript("OnMouseUp", function(self) self.dragX = nil end)
    stage.model:EnableMouseWheel(true)
    stage.model:SetScript("OnMouseWheel", function(self, delta)
        self.zoom = math.max(0, math.min(2.5, self.zoom + delta * 0.25))
        self:SetPosition(self.zoom, 0, 0)
    end)
    local hint = ES.Text(stage, 9, ES.MUTED)
    hint:SetPoint("BOTTOM", stage.model, "BOTTOM", 0, 6)
    hint:SetText("Drag to rotate  ·  Scroll to zoom")

    local info = CreateFrame("Frame", nil, stage)
    info:SetPoint("BOTTOMLEFT", 1, 215); info:SetPoint("BOTTOMRIGHT", -1, 215); info:SetHeight(124)
    ES.Solid(info, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    local rule = ES.Solid(info, "ARTWORK", ES.GOLD_DIM, 0.8)
    rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    stage.kind = ES.Text(info, 10, ES.GOLD_DIM)
    stage.kind:SetPoint("TOPLEFT", 16, -12)
    stage.kind:SetText("DANCE")
    stage.name = ES.Text(info, 17, ES.GOLD)
    stage.name:SetPoint("TOPLEFT", stage.kind, "BOTTOMLEFT", 0, -6)
    stage.state = ES.Text(info, 11, ES.MUTED)
    stage.state:SetPoint("TOPLEFT", stage.name, "BOTTOMLEFT", 0, -6)
    danceButton = flatButton(info, 96, 28, "Dance", true)
    danceButton:SetPoint("BOTTOMLEFT", 14, 12)
    danceButton:SetScript("OnClick", function() EE.Play(focus) end)
    favoriteButton = flatButton(info, 96, 28, "Favorite")
    favoriteButton:SetPoint("LEFT", danceButton, "RIGHT", 8, 0)
    favoriteButton:SetScript("OnClick", function()
        if focus and db then db.favorites[focus] = not db.favorites[focus] or nil; refresh() end
    end)
    wheelButton = flatButton(info, 110, 28, "Add to wheel")
    wheelButton:SetPoint("LEFT", favoriteButton, "RIGHT", 8, 0)
    wheelButton:SetScript("OnClick", function() if focus then toggleWheel(focus) end end)
    stage.editor = buildWheelEditor(stage)
end

local function paintStage()
    local emote = EE.byId[focus]
    local known = emote and EE.known[emote.id]
    shown(stage.model, known)
    if known and stage.model.emote ~= emote.id then stage.model:Dance(emote.id) end
    stage.name:SetText(emote and emote.name or "No emotes yet")
    local slot = emote and EE.SlotOf(emote.id)
    if not emote then
        stage.state:SetText("Collect emotes to fill your wheel.")
    elseif not known then
        stage.state:SetText("|cffe86c5cNot collected yet|r")
    elseif slot then
        stage.state:SetText(("|cff8cd97fOn wheel, page %d slot %d|r"):format(math.ceil(slot / WHEEL_SIZE), (slot - 1) % WHEEL_SIZE + 1))
    else
        stage.state:SetText("Not on your wheel  ·  /" .. emote.slash)
    end
    danceButton:SetActive(known)
    favoriteButton:SetActive(known)
    favoriteButton:SetLabel(emote and db and db.favorites[emote.id] and "Unfavorite" or "Favorite")
    wheelButton:SetActive(known and (slot or EE.FirstEmpty()) ~= nil)
    wheelButton:SetLabel(slot and "Remove" or "Add to wheel")
    paintWheelEditor()
end

refresh = function()
    if not frame or not frame:IsShown() then return end
    local query = search and search:GetText():lower() or ""
    local test = FILTER_BY_KEY[filter].test
    filtered = {}
    for _, e in ipairs(EE.list) do
        if test(e) and (query == "" or e.name:lower():find(query, 1, true) or e.slash:find(query, 1, true)) then
            filtered[#filtered + 1] = e
        end
    end
    -- Collected first, then by name
    table.sort(filtered, function(a, b)
        local ka, kb = EE.known[a.id] and 1 or 0, EE.known[b.id] and 1 or 0
        if ka ~= kb then return ka > kb end
        return a.name < b.name
    end)
    if not EE.byId[focus] then focus = filtered[1] and filtered[1].id end
    local collected = 0
    for _ in pairs(EE.known) do collected = collected + 1 end
    frame.total:SetText(("%d of %d collected"):format(collected, #EE.list))
    frame.count:SetText(("%d shown"):format(#filtered))
    paintRail()
    paintGrid()
    paintStage()
    -- A card counts as seen once it has been on screen.
    if db then for _, card in ipairs(cards) do if card:IsShown() and card.emote and EE.known[card.emote.id] then db.seen[card.emote.id] = true end end end
end
EE.listeners[#EE.listeners + 1] = function() refresh() end

-- ---------------------------------------------------------------------------------------------------------------
-- window
-- ---------------------------------------------------------------------------------------------------------------
local function build()
    frame = CreateFrame("Frame", "EsteriaEmotesFrame", UIParent)
    frame:SetSize(W, H)
    frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH")
    frame:SetToplevel(true)
    frame:EnableMouse(true)
    frame:SetMovable(true)
    frame:SetClampedToScreen(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide()
    tinsert(UISpecialFrames, "EsteriaEmotesFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
    local header = frame:CreateTexture(nil, "BORDER")
    header:SetTexture(ES.WHITE)
    header:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    header:SetPoint("TOPLEFT", 1, -1); header:SetPoint("TOPRIGHT", -1, -1); header:SetHeight(56)
    ES.Border(frame, ES.GOLD)

    local title = ES.Text(frame, 22, ES.GOLD)
    title:SetPoint("TOPLEFT", 20, -12)
    title:SetText("Emotes")
    local subtitle = ES.Text(frame, 11, ES.MUTED)
    subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 1, -4)
    subtitle:SetText("Your dance collection. Everyone nearby sees you dance; moving stops it.")
    frame.total = ES.Text(frame, 12, ES.TEXT)
    frame.total:SetPoint("TOPRIGHT", -40, -20)
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)
    frame.cosmeticsClose = close

    buildRail()
    buildStage()

    grid = CreateFrame("Frame", nil, frame)
    grid:SetPoint("TOPLEFT", railRows[1]:GetParent(), "TOPRIGHT", 18, -40)
    grid:SetSize(GRID_W, ROWS * CARD_H + (ROWS - 1) * GAP)
    for i = 1, PER_PAGE do cards[i] = buildCard(i) end

    search = CreateFrame("EditBox", "EsteriaEmotesSearch", frame, "InputBoxTemplate")
    search:SetSize(220, 20)
    search:SetPoint("BOTTOMLEFT", grid, "TOPLEFT", 6, 12)
    search:SetAutoFocus(false)
    search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    local placeholder = ES.Text(search, 11, ES.MUTED)
    placeholder:SetPoint("LEFT", 2, 0)
    placeholder:SetText("Search emotes")
    search:SetScript("OnTextChanged", function(self) shown(placeholder, self:GetText() == ""); page = 1; refresh() end)
    frame.count = ES.Text(frame, 11, ES.MUTED)
    frame.count:SetPoint("BOTTOMRIGHT", grid, "TOPRIGHT", 0, 16)

    prevButton = flatButton(frame, 36, 26, "<")
    prevButton:SetPoint("TOPLEFT", grid, "BOTTOMLEFT", 0, -14)
    prevButton:SetScript("OnClick", function() page = page - 1; refresh() end)
    nextButton = flatButton(frame, 36, 26, ">")
    nextButton:SetPoint("TOPRIGHT", grid, "BOTTOMRIGHT", 0, -14)
    nextButton:SetScript("OnClick", function() page = page + 1; refresh() end)
    pageText = ES.Text(frame, 12, ES.TEXT)
    pageText:SetPoint("TOP", grid, "BOTTOM", 0, -20)

    ghost = CreateFrame("Frame", nil, UIParent)
    ghost:SetSize(36, 36)
    ghost:SetFrameStrata("TOOLTIP")
    ghost.icon = ghost:CreateTexture(nil, "OVERLAY")
    ghost.icon:SetAllPoints()
    ghost:Hide()
    ghost:SetScript("OnUpdate", function(self)
        local x, y = GetCursorPosition()
        local s = UIParent:GetEffectiveScale()
        self:ClearAllPoints()
        self:SetPoint("CENTER", UIParent, "BOTTOMLEFT", x / s, y / s)
    end)

    frame:SetScript("OnShow", function(self)
        PlaySound("igCharacterInfoOpen")
        send("sync")
        refresh()
        ES.Tween("emotes-open", 0.22, 0, function(p) self:SetAlpha(p) end)
    end)
    frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); if dragging then dragging = nil; ghost:Hide() end end)
end

function EsteriaEmotes_Toggle()
    if EsteriaCosmetics_Toggle then EsteriaCosmetics_Toggle("Emotes"); return end
    if not frame then build() end
    if #EE.list == 0 then
        DEFAULT_CHAT_FRAME:AddMessage("|cff00ccff[Emotes]|r No emotes are installed.")
        return
    end
    shown(frame, not frame:IsShown())
end

-- The Cosmetics tab hosts this complete panel; playback, collection state and the wheel stay unchanged.
function EsteriaEmotes_Embed(parent)
    if not frame then build() end
    frame:SetParent(parent)
    frame:ClearAllPoints()
    frame:SetPoint("TOPLEFT")
    frame:SetMovable(false)
    frame:SetScript("OnDragStart", nil)
    frame.cosmeticsClose:SetScript("OnClick", function() parent:GetParent():Hide() end)
    frame:Show()
end

BINDING_HEADER_ESTERIAEMOTES = "Esteria Emotes"
BINDING_NAME_ESTERIAEMOTES_WHEEL = "Emote wheel (hold)"
BINDING_NAME_ESTERIAEMOTES_TOGGLE = "Emote collection"

SLASH_ESTERIAEMOTES1, SLASH_ESTERIAEMOTES2 = "/emotes", "/emotecollection"
SlashCmdList.ESTERIAEMOTES = EsteriaEmotes_Toggle
