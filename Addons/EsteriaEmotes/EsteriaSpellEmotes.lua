-- Esteria spell emotes: the Spellbook's "Emotes" book (spells ranked "Emote": HAH, KEKW, gg, ...) as the Collections
-- "Emotes" tab, plus a hold-key wheel that casts them. Casting is protected, so the wheel key is a CLICK binding on a
-- SecureActionButton: key down opens the wheel, key up points the button at the aimed spell and the click casts it.
-- The wheel is kept per account in EsteriaEmotesDB.spellWheel (slot -> spell id); the realm is not involved.
local ES, EE = EsteriaStyle, EsteriaEmotes
local W, H, RAIL_W, MARGIN = 1180, 680, 236, 14
local COLS, ROWS, TILE_H, GAP = 2, 10, 44, 6
local GRID_W = 3 * 170 + 2 * 12          -- the Dances grid width, so both tabs line up
local TILE_W = (GRID_W - GAP) / COLS
local PER_PAGE = COLS * ROWS
local STAGE_W = W - MARGIN - (MARGIN + RAIL_W + 18 + GRID_W + 18)
local PAGES, SIZE = 16, 8
local BINDING = "CLICK EsteriaSpellEmoteWheelButton:LeftButton"

EsteriaSpellEmotes = { list = {}, byId = {} }
local SE = EsteriaSpellEmotes
local db, wheel = nil, {}                 -- EsteriaEmotesDB, and its spellWheel once logged in
local frame, grid, stage, search, pageText, prevButton, nextButton, castButton, wheelButton, ghost
local tiles, railRows, editorSlots = {}, {}, {}
local filtered, filter, page, focus, editorPage = {}, "all", 1, nil, 1
local dragging                            -- { id = spell id, fromSlot = n }
local refresh

local function shown(region, on) if on then region:Show() else region:Hide() end end

-- ---------------------------------------------------------------------------------------------------------------
-- model
-- ---------------------------------------------------------------------------------------------------------------
local function scan()
    SE.list, SE.byId = {}, {}
    local i = 1
    while i < 4096 do
        local name, rank = GetSpellName(i, BOOKTYPE_SPELL)
        if not name then break end
        if (rank or ""):lower() == "emote" then
            local id = tonumber((GetSpellLink(i, BOOKTYPE_SPELL) or ""):match("spell:(%d+)"))
            if id and not SE.byId[id] then
                local emote = { id = id, name = name, icon = GetSpellTexture(i, BOOKTYPE_SPELL) }
                SE.list[#SE.list + 1] = emote
                SE.byId[id] = emote
            end
        end
        i = i + 1
    end
    table.sort(SE.list, function(a, b) return a.name:lower() < b.name:lower() end)
end

function SE.SlotOf(id) for k = 1, PAGES * SIZE do if wheel[k] == id then return k end end end
-- A slot holding a spell this character lacks still counts as taken: the wheel is shared by the account.
function SE.FirstEmpty() for k = 1, PAGES * SIZE do if not wheel[k] then return k end end end

local function setSlot(k, id)
    local from = id and SE.SlotOf(id)
    if from and from ~= k then wheel[from] = nil end
    wheel[k] = id
    PlaySound("igMainMenuOptionCheckBoxOn")
    refresh()
end

local function toggleWheel(id)
    local slot = SE.SlotOf(id)
    if slot then setSlot(slot, nil)
    else
        local empty = SE.FirstEmpty()
        if empty then setSlot(empty, id) else UIErrorsFrame:AddMessage("Your emote wheel is full.", 1, 0.1, 0.1) end
    end
end

local function where(slot) return math.ceil(slot / SIZE), (slot - 1) % SIZE + 1 end

-- ---------------------------------------------------------------------------------------------------------------
-- the wheel
-- ---------------------------------------------------------------------------------------------------------------
local spellWheel = EE.NewWheel("EsteriaSpellEmotesWheelFrame", {
    Get = function(k) return SE.byId[wheel[k]] end,
    Pages = function()          -- only as far as the last page in use, so the mouse wheel never flips through blanks
        local last = 1
        for k, id in pairs(wheel) do if SE.byId[id] then last = math.max(last, math.ceil(k / SIZE)) end end
        return last
    end,
    empty = "This page is empty. Fill it in Collections > Emotes.",
    prompt = "Point at an emote and release the key. Mouse wheel: next page.",
})

local button = CreateFrame("Button", "EsteriaSpellEmoteWheelButton", UIParent, "SecureActionButtonTemplate")
button:RegisterForClicks("AnyDown", "AnyUp")
button:SetScript("PreClick", function(self, _, down)
    if InCombatLockdown() then
        if down ~= false then UIErrorsFrame:AddMessage("Leave combat to use the emote wheel.", 1, 0.1, 0.1) end
        return
    end
    self:SetAttribute("type", nil)
    if down == nil then down = not spellWheel.frame:IsShown() end   -- a click without key state toggles
    if down then spellWheel.Open(); return end
    local emote = spellWheel.Close(true)
    if emote then self:SetAttribute("type", "spell"); self:SetAttribute("spell", emote.name) end
end)
button:SetScript("PostClick", function(self) if not InCombatLockdown() then self:SetAttribute("type", nil) end end)
_G["BINDING_NAME_" .. BINDING] = "Emote wheel (hold)"

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("SPELLS_CHANGED")
events:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then
        EsteriaEmotesDB = EsteriaEmotesDB or {}
        db = EsteriaEmotesDB
        db.spellWheel = db.spellWheel or {}
        wheel = db.spellWheel
        -- Hold CTRL-H for this wheel, once, unless that key is already taken.
        if not db.spellWheelBound then
            db.spellWheelBound = true
            if not GetBindingKey(BINDING) and (GetBindingAction("CTRL-H") or "") == "" then
                SetBinding("CTRL-H", BINDING)
                SaveBindings(GetCurrentBindingSet())
            end
        end
    end
    scan()
    if refresh then refresh() end
end)

-- ---------------------------------------------------------------------------------------------------------------
-- rail
-- ---------------------------------------------------------------------------------------------------------------
local FILTERS = {
    { key = "all",   group = "EMOTES", label = "All emotes",    test = function() return true end },
    { key = "wheel", group = "FILTER", label = "On my wheel",   test = function(e) return SE.SlotOf(e.id) ~= nil end },
    { key = "off",   group = "FILTER", label = "Not on wheel",  test = function(e) return SE.SlotOf(e.id) == nil end },
}
local FILTER_BY_KEY = {}
for _, f in ipairs(FILTERS) do FILTER_BY_KEY[f.key] = f end

local function choose(id) focus = id; refresh() end

local function beginDrag(id, fromSlot)
    dragging = { id = id, fromSlot = fromSlot }
    ghost.icon:SetTexture(SE.byId[id] and SE.byId[id].icon or nil)
    ghost:Show()
end

-- Dropping on a wheel slot assigns (or swaps); dropping a wheel slot anywhere else clears it.
local function endDrag()
    if not dragging then return end
    local target
    for i, slot in ipairs(editorSlots) do if MouseIsOver(slot) then target = (editorPage - 1) * SIZE + i end end
    local d = dragging
    dragging = nil
    ghost:Hide()
    if target and target ~= d.fromSlot then
        local displaced = wheel[target]
        wheel[target] = nil
        setSlot(target, d.id)
        if d.fromSlot and displaced then wheel[d.fromSlot] = displaced; refresh() end
    elseif not target and d.fromSlot then
        setSlot(d.fromSlot, nil)
    end
end

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
        for _, e in ipairs(SE.list) do if row.filter.test(e) then n = n + 1 end end
        row.wash:SetAlpha(on and 0.12 or 0)
        row.accent:SetAlpha(on and 1 or 0)
        row.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        row.count:SetText(n)
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- tiles
-- ---------------------------------------------------------------------------------------------------------------
local function buildTile(i)
    local col, row = (i - 1) % COLS, math.floor((i - 1) / COLS)
    local tile = CreateFrame("Button", nil, grid)
    tile:SetSize(TILE_W, TILE_H)
    tile:SetPoint("TOPLEFT", col * (TILE_W + GAP), -row * (TILE_H + GAP))
    tile.bg = ES.Solid(tile, "BACKGROUND", { 0.06, 0.07, 0.10 }, 1)
    tile.bg:SetAllPoints()
    tile.lines = ES.Border(tile, ES.GOLD_DIM, 0.7)
    tile.icon = EE.iconFrame(tile, 36)
    tile.icon:SetPoint("LEFT", 4, 0)
    tile.name = ES.Text(tile, 12, ES.TEXT)
    tile.name:SetPoint("TOPLEFT", tile.icon, "TOPRIGHT", 10, -4)
    tile.name:SetWidth(TILE_W - 120)
    tile.name:SetJustifyH("LEFT")
    tile.sub = ES.Text(tile, 10, ES.MUTED)
    tile.sub:SetPoint("BOTTOMLEFT", tile.icon, "BOTTOMRIGHT", 10, 4)
    tile.slot = ES.Text(tile, 10, ES.GOOD)
    tile.slot:SetPoint("RIGHT", -10, 0)
    tile:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    tile:RegisterForDrag("LeftButton")
    tile:SetScript("OnClick", function(self, mouse)
        PlaySound("igMainMenuOptionCheckBoxOn")
        if mouse == "RightButton" then toggleWheel(self.emote.id) else choose(self.emote.id) end
    end)
    tile:SetScript("OnDragStart", function(self) beginDrag(self.emote.id) end)
    tile:SetScript("OnDragStop", endDrag)
    tile:SetScript("OnEnter", function(self)
        if self.emote.id ~= focus then ES.Tint(self.lines, ES.GOLD, 0.9) end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(self.emote.name, 1, 0.82, 0.1)
        GameTooltip:AddLine("Click: select.  Right-click: add to or remove from the wheel.", 0.6, 1, 0.6)
        GameTooltip:AddLine("Drag onto a wheel slot to choose its page and slot.", 0.6, 1, 0.6)
        GameTooltip:Show()
    end)
    tile:SetScript("OnLeave", function(self)
        if self.emote.id ~= focus then ES.Tint(self.lines, ES.GOLD_DIM, 0.7) end
        GameTooltip:Hide()
    end)
    tile:EnableMouseWheel(true)
    tile:SetScript("OnMouseWheel", function(_, delta) page = page - delta; refresh() end)
    return tile
end

local function paintGrid()
    local pages = math.max(1, math.ceil(#filtered / PER_PAGE))
    page = math.min(math.max(page, 1), pages)
    pageText:SetText(("Page %d / %d"):format(page, pages))
    prevButton:SetActive(page > 1)
    nextButton:SetActive(page < pages)
    for i = 1, PER_PAGE do
        local tile, emote = tiles[i], filtered[(page - 1) * PER_PAGE + i]
        tile.emote = emote
        if emote then
            tile.icon.icon:SetTexture(emote.icon)
            tile.name:SetText(emote.name)
            tile.sub:SetText("Emote")
            local slot = SE.SlotOf(emote.id)
            tile.slot:SetText(slot and ("%d · %d"):format(where(slot)) or "")
            local on = emote.id == focus
            ES.Tint(tile.lines, on and ES.GOLD or ES.GOLD_DIM, on and 1 or 0.7)
            tile.bg:SetVertexColor(on and 0.13 or 0.06, on and 0.11 or 0.07, on and 0.07 or 0.10)
            tile:Show()
        else
            tile:Hide()
        end
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- stage and wheel editor
-- ---------------------------------------------------------------------------------------------------------------
local function buildEditor(parent)
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
    local prev = EE.flatButton(editor, 22, 18, "<")
    prev:SetPoint("RIGHT", editor.pageText, "LEFT", -8, 0)
    prev:SetScript("OnClick", function() editorPage = (editorPage - 2) % PAGES + 1; refresh() end)
    local nxt = EE.flatButton(editor, 22, 18, ">")
    nxt:SetPoint("LEFT", editor.pageText, "RIGHT", 8, 0)
    nxt:SetScript("OnClick", function() editorPage = editorPage % PAGES + 1; refresh() end)
    local ring = CreateFrame("Frame", nil, editor)
    ring:SetSize(180, 160)
    ring:SetPoint("TOP", 0, -30)
    ring:EnableMouseWheel(true)
    ring:SetScript("OnMouseWheel", function(_, delta) editorPage = (editorPage - 1 - delta) % PAGES + 1; refresh() end)
    editor.hub = ES.Text(ring, 12, ES.MUTED)
    editor.hub:SetPoint("CENTER", 0, 0)
    for i = 1, SIZE do
        local a = (i - 1) * 2 * math.pi / SIZE
        local slot = CreateFrame("Button", nil, ring)
        slot:SetSize(36, 36)
        slot:SetPoint("CENTER", ring, "CENTER", math.sin(a) * 62, math.cos(a) * 62)
        ES.Solid(slot, "BACKGROUND", { 0.06, 0.07, 0.10 }, 1):SetAllPoints()
        slot.lines = ES.Border(slot, ES.GOLD_DIM, 0.8)
        slot.icon = slot:CreateTexture(nil, "ARTWORK")
        slot.icon:SetPoint("TOPLEFT", 2, -2); slot.icon:SetPoint("BOTTOMRIGHT", -2, 2)
        slot.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        slot:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        slot:RegisterForDrag("LeftButton")
        local function absolute() return (editorPage - 1) * SIZE + i end
        slot:SetScript("OnClick", function(_, mouse)
            local id = wheel[absolute()]
            if mouse == "RightButton" then if id then setSlot(absolute(), nil) end
            elseif id and SE.byId[id] then choose(id)
            elseif focus then setSlot(absolute(), focus) end      -- an empty slot takes the selected emote
        end)
        slot:SetScript("OnDragStart", function()
            local id = wheel[absolute()]
            if SE.byId[id] then beginDrag(id, absolute()) end
        end)
        slot:SetScript("OnDragStop", endDrag)
        slot:SetScript("OnEnter", function(self)
            ES.Tint(self.lines, ES.GOLD, 1)
            local emote = SE.byId[wheel[absolute()]]
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            if emote then
                GameTooltip:SetText(emote.name, 1, 0.82, 0.1)
                GameTooltip:AddLine("Drag to move, right-click to clear.", 0.6, 1, 0.6)
            elseif wheel[absolute()] then
                GameTooltip:SetText("Another character's emote", 0.7, 0.7, 0.7)
                GameTooltip:AddLine("Right-click to clear.", 0.6, 1, 0.6)
            else
                GameTooltip:SetText("Empty slot", 0.7, 0.7, 0.7)
                GameTooltip:AddLine("Click to put the selected emote here, or drag one in.", 0.6, 1, 0.6)
            end
            GameTooltip:Show()
        end)
        slot:SetScript("OnLeave", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 0.8); GameTooltip:Hide() end)
        editorSlots[i] = slot
    end
    local hint = ES.Text(editor, 10, ES.MUTED)
    hint:SetPoint("BOTTOM", 0, 10)
    hint:SetText("Hold the emote wheel key in game (CTRL-H unless rebound)")
    return editor
end

local function paintEditor()
    local filled = 0
    for i, slot in ipairs(editorSlots) do
        local emote = SE.byId[wheel[(editorPage - 1) * SIZE + i]]
        slot.icon:SetTexture(emote and emote.icon or nil)
        if emote then filled = filled + 1 end
        ES.Tint(slot.lines, emote and emote.id == focus and ES.GOLD or ES.GOLD_DIM, 0.8)
    end
    stage.editor.hub:SetText(filled .. "/" .. SIZE)
    stage.editor.pageText:SetText(editorPage .. "/" .. PAGES)
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
    stage.icon = EE.iconFrame(stage, 128)
    stage.icon:SetPoint("CENTER", stage, "TOP", 0, -130)
    ES.Tint(stage.icon.lines, ES.GOLD, 0.9)

    local info = CreateFrame("Frame", nil, stage)
    info:SetPoint("BOTTOMLEFT", 1, 215); info:SetPoint("BOTTOMRIGHT", -1, 215); info:SetHeight(124)
    ES.Solid(info, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    local rule = ES.Solid(info, "ARTWORK", ES.GOLD_DIM, 0.8)
    rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    local kind = ES.Text(info, 10, ES.GOLD_DIM)
    kind:SetPoint("TOPLEFT", 16, -12)
    kind:SetText("EMOTE")
    stage.name = ES.Text(info, 17, ES.GOLD)
    stage.name:SetPoint("TOPLEFT", kind, "BOTTOMLEFT", 0, -6)
    stage.state = ES.Text(info, 11, ES.MUTED)
    stage.state:SetPoint("TOPLEFT", stage.name, "BOTTOMLEFT", 0, -6)
    castButton = EE.flatButton(info, 96, 28, "Cast", true, "SecureActionButtonTemplate")
    castButton:SetPoint("BOTTOMLEFT", 14, 12)
    wheelButton = EE.flatButton(info, 120, 28, "Add to wheel")
    wheelButton:SetPoint("LEFT", castButton, "RIGHT", 8, 0)
    wheelButton:SetScript("OnClick", function() if focus then toggleWheel(focus) end end)
    stage.editor = buildEditor(stage)
end

local function paintStage()
    local emote = SE.byId[focus]
    shown(stage.icon, emote ~= nil)
    if emote then stage.icon.icon:SetTexture(emote.icon) end
    stage.name:SetText(emote and emote.name or "No emotes yet")
    local slot = emote and SE.SlotOf(emote.id)
    if not emote then stage.state:SetText("Emote spells you learn show up here.")
    elseif slot then stage.state:SetText(("|cff8cd97fOn wheel, page %d slot %d|r"):format(where(slot)))
    else stage.state:SetText("Not on your wheel") end
    if not InCombatLockdown() then
        castButton:SetAttribute("type", emote and "spell" or nil)
        castButton:SetAttribute("spell", emote and emote.name or nil)
        castButton:SetActive(emote ~= nil)
    end
    wheelButton:SetActive(emote ~= nil and (slot or SE.FirstEmpty()) ~= nil)
    wheelButton:SetLabel(slot and "Remove" or "Add to wheel")
    paintEditor()
end

refresh = function()
    if not frame or not frame:IsShown() then return end
    local query = search:GetText():lower()
    local test = FILTER_BY_KEY[filter].test
    filtered = {}
    for _, e in ipairs(SE.list) do
        if test(e) and (query == "" or e.name:lower():find(query, 1, true)) then filtered[#filtered + 1] = e end
    end
    if not SE.byId[focus] then focus = filtered[1] and filtered[1].id end
    local onWheel = 0
    for _, e in ipairs(SE.list) do if SE.SlotOf(e.id) then onWheel = onWheel + 1 end end
    frame.total:SetText(("%d emotes  ·  %d on your wheel"):format(#SE.list, onWheel))
    frame.count:SetText(("%d shown"):format(#filtered))
    paintRail()
    paintGrid()
    paintStage()
end

-- ---------------------------------------------------------------------------------------------------------------
-- window (hosted by the Collections "Emotes" tab)
-- ---------------------------------------------------------------------------------------------------------------
local function build()
    frame = CreateFrame("Frame", "EsteriaSpellEmotesFrame", UIParent)
    frame:SetSize(W, H)
    frame:SetFrameStrata("HIGH")
    frame:EnableMouse(true)
    frame:Hide()
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
    subtitle:SetText("Your emote spells. Put them on the emote wheel, then hold its key to cast one.")
    frame.total = ES.Text(frame, 12, ES.TEXT)
    frame.total:SetPoint("TOPRIGHT", -40, -20)
    frame.close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    frame.close:SetPoint("TOPRIGHT", 2, 2)

    buildRail()
    buildStage()

    grid = CreateFrame("Frame", nil, frame)
    grid:SetPoint("TOPLEFT", railRows[1]:GetParent(), "TOPRIGHT", 18, -40)
    grid:SetSize(GRID_W, ROWS * TILE_H + (ROWS - 1) * GAP)
    for i = 1, PER_PAGE do tiles[i] = buildTile(i) end

    search = CreateFrame("EditBox", "EsteriaSpellEmotesSearch", frame, "InputBoxTemplate")
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

    prevButton = EE.flatButton(frame, 36, 26, "<")
    prevButton:SetPoint("TOPLEFT", grid, "BOTTOMLEFT", 0, -14)
    prevButton:SetScript("OnClick", function() page = page - 1; refresh() end)
    nextButton = EE.flatButton(frame, 36, 26, ">")
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

    frame:SetScript("OnShow", function() scan(); refresh() end)
    frame:SetScript("OnHide", function() if dragging then dragging = nil; ghost:Hide() end end)
end

function EsteriaSpellEmotes_Embed(parent)
    if not frame then build() end
    frame:SetParent(parent)
    frame:ClearAllPoints()
    frame:SetPoint("TOPLEFT")
    frame.close:SetScript("OnClick", function() parent:GetParent():Hide() end)
    frame:Show()
end
