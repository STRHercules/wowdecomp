-- Esteria mount customization: the retail drake/D.R.I.V.E. customizer, opened from the Customize button in the
-- Collections Mounts tab (EsteriaCosmetics) or the old Mount Journal (EsteriaMountCustomize_UpdateJournal).
-- Styled like the Collections tabs: gold header band, glowing stage, flat gold-bordered controls.
-- Options, preview and saving are the client's (Extensions/MountCustomize); every choice is unlocked.
local ES = EsteriaStyle
local WIDTH, HEIGHT, PANEL_W, ROW_H = 940, 620, 330, 34

local frame, model, list, title
local display, creature, mountName
local options, current, saved = {}, {}, {}
local rows = {}
local journalButton

local function look()
    local choices = {}
    for i, option in ipairs(options) do choices[i] = option.choices[current[i]].id end
    return choices
end

-- The model loads asynchronously after SetCreature: apply now and again once it has had a moment.
local pending = 0
local function preview()
    if model and display then EsteriaMountCustomize_Preview(model, display, look()) end
    pending = 1.0
end

local function swatchColor(swatch)
    if not swatch or swatch == 0 then return nil end
    return math.floor(swatch / 65536) % 256 / 255, math.floor(swatch / 256) % 256 / 255, swatch % 256 / 255
end

local function refreshRow(i)
    local row, option = rows[i], options[i]
    local choice = option.choices[current[i]]
    row.label:SetText(option.name)
    row.value:SetText(choice.name)
    local r, g, b = swatchColor(choice.swatch)
    if r then row.swatch:SetVertexColor(r, g, b); row.swatch:Show(); row.swatchBorder:Show()
    else row.swatch:Hide(); row.swatchBorder:Hide() end
    row.count:SetText(current[i] .. "/" .. #option.choices)
end

local function step(i, delta)
    local n = #options[i].choices
    current[i] = (current[i] - 1 + delta) % n + 1
    refreshRow(i)
    preview()
end

-- Flat Esteria button: gold border, soft gold wash on hover; primary is the gold call to action.
local function flat(parent, width, height, text, primary, size)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height)
    local bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, primary and 0.95 or 0.9)
    bg:SetAllPoints()
    local glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0)
    glow:SetAllPoints()
    local lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    local label = ES.Text(b, size or (primary and 13 or 12), primary and { 1, 0.92, 0.7 } or ES.TEXT)
    label:SetPoint("CENTER", 0, 1)
    label:SetText(text)
    b:SetScript("OnEnter", function() glow:SetAlpha(0.12); ES.Tint(lines, ES.GOLD, 1) end)
    b:SetScript("OnLeave", function() glow:SetAlpha(0); ES.Tint(lines, primary and ES.GOLD or ES.GOLD_DIM, 1) end)
    return b
end

local function arrow(parent, text, onClick)
    local button = flat(parent, 26, 26, text, false, 14)
    button:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); onClick() end)
    return button
end

local function row(i)
    if rows[i] then return rows[i] end
    local r = CreateFrame("Frame", nil, list)
    r:SetSize(PANEL_W - 24, ROW_H + 18)
    r:SetPoint("TOPLEFT", 4, -(i - 1) * (ROW_H + 18))
    r.label = ES.Text(r, 10, ES.GOLD_DIM)
    r.label:SetPoint("TOPLEFT", 2, 0)
    r.count = ES.Text(r, 10, ES.MUTED)
    r.count:SetPoint("TOPRIGHT", -2, 0)
    local left = arrow(r, "<", function() step(i, -1) end)
    left:SetPoint("BOTTOMLEFT", 0, 0)
    local right = arrow(r, ">", function() step(i, 1) end)
    right:SetPoint("BOTTOMRIGHT", 0, 0)
    local box = CreateFrame("Button", nil, r)
    box:SetPoint("TOPLEFT", left, "TOPRIGHT", 4, 0)
    box:SetPoint("BOTTOMRIGHT", right, "BOTTOMLEFT", -4, 0)
    ES.Solid(box, "BACKGROUND", ES.INK, 0.85):SetAllPoints()
    ES.Border(box, ES.GOLD_DIM)
    box:SetScript("OnClick", function(_, button) step(i, button == "RightButton" and -1 or 1) end)
    box:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    box:EnableMouseWheel(true)
    box:SetScript("OnMouseWheel", function(_, delta) step(i, delta > 0 and -1 or 1) end)
    r.swatch = ES.Solid(box, "ARTWORK", { 1, 1, 1 })
    r.swatch:SetSize(16, 16)
    r.swatch:SetPoint("LEFT", 6, 0)
    r.swatchBorder = ES.Solid(box, "BORDER", ES.GOLD, 1)
    r.swatchBorder:SetSize(18, 18)
    r.swatchBorder:SetPoint("CENTER", r.swatch)
    r.value = ES.Text(box, 12, ES.TEXT)
    r.value:SetPoint("CENTER", 8, 0)
    rows[i] = r
    return r
end

local function button(parent, text, width, onClick, primary)
    local b = flat(parent, width, 30, text, primary)
    b:SetScript("OnClick", onClick)
    return b
end

local function build()
    frame = CreateFrame("Frame", "EsteriaMountCustomizeFrame", UIParent)
    frame:SetSize(WIDTH, HEIGHT)
    frame:SetPoint("CENTER")
    frame:SetFrameStrata("DIALOG")   -- opens over the Collections window
    frame:SetToplevel(true)
    frame:EnableMouse(true)
    frame:SetMovable(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide()
    tinsert(UISpecialFrames, "EsteriaMountCustomizeFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
    local header = frame:CreateTexture(nil, "BORDER")
    header:SetTexture(ES.WHITE)
    header:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    header:SetPoint("TOPLEFT", 1, -1); header:SetPoint("TOPRIGHT", -1, -1); header:SetHeight(56)
    ES.Border(frame, ES.GOLD)

    title = ES.Text(frame, 22, ES.GOLD)
    title:SetPoint("TOPLEFT", 20, -12)
    local subtitle = ES.Text(frame, 11, ES.MUTED)
    subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 1, -4)
    subtitle:SetText("Pick colors and parts for this mount. Your look is saved and everyone sees it.")
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)

    -- The stage: a large preview you turn by dragging and zoom with the wheel, like retail's.
    local stageFrame = CreateFrame("Frame", nil, frame)
    stageFrame:SetPoint("TOPLEFT", 14, -64)
    stageFrame:SetPoint("BOTTOMRIGHT", -PANEL_W - 30, 58)
    ES.Solid(stageFrame, "BACKGROUND", { 0.05, 0.05, 0.07 }, 1):SetAllPoints()
    local glow = stageFrame:CreateTexture(nil, "BORDER")
    glow:SetTexture(ES.WHITE)
    glow:SetGradientAlpha("VERTICAL", 0.62, 0.46, 0.15, 0.3, 0.62, 0.46, 0.15, 0)
    glow:SetPoint("BOTTOMLEFT"); glow:SetPoint("BOTTOMRIGHT"); glow:SetHeight(180)
    ES.Border(stageFrame, ES.GOLD, 0.8)
    local hint = ES.Text(stageFrame, 9, ES.MUTED)
    hint:SetPoint("BOTTOM", 0, 8)
    hint:SetText("Drag to rotate  ·  Scroll to zoom")
    model = CreateFrame("PlayerModel", nil, stageFrame)
    model:SetPoint("TOPLEFT", 4, -4)
    model:SetPoint("BOTTOMRIGHT", -4, 22)
    model.rotation, model.zoom = 0.6, 0
    model:EnableMouse(true)
    model:EnableMouseWheel(true)
    model:SetScript("OnMouseDown", function(self) self.dragX = GetCursorPosition() end)
    model:SetScript("OnMouseUp", function(self) self.dragX = nil end)
    model:SetScript("OnMouseWheel", function(self, delta)
        self.zoom = math.max(-1, math.min(4, self.zoom + delta * 0.4))
        self:SetPosition(self.zoom, 0, 0)
    end)
    model:SetScript("OnUpdate", function(self, elapsed)
        if self.dragX then
            local x = GetCursorPosition()
            self.rotation = self.rotation + (x - self.dragX) * 0.012
            self.dragX = x
            self:SetRotation(self.rotation)
        end
        if pending > 0 then
            pending = pending - elapsed
            if pending <= 0 and display then EsteriaMountCustomize_Preview(self, display, look()) end
        end
    end)

    -- Options rail: the Collections rail look, with the option rows scrolling inside it.
    local side = CreateFrame("Frame", nil, frame)
    side:SetPoint("TOPRIGHT", -14, -64)
    side:SetPoint("BOTTOMRIGHT", -14, 58)
    side:SetWidth(PANEL_W + 2)
    ES.Solid(side, "BACKGROUND", { 0.05, 0.06, 0.08 }, 0.9):SetAllPoints()
    ES.Border(side, ES.GOLD_DIM, 0.6)
    local head = ES.Text(side, 10, ES.GOLD_DIM)
    head:SetPoint("TOPLEFT", 14, -12)
    head:SetText("OPTIONS")
    local panel = CreateFrame("ScrollFrame", "EsteriaMountCustomizeScroll", side, "UIPanelScrollFrameTemplate")
    panel:SetPoint("TOPLEFT", 8, -30)
    panel:SetSize(PANEL_W - 20, HEIGHT - 64 - 58 - 40)
    list = CreateFrame("Frame", nil, panel)
    list:SetSize(PANEL_W - 20, 10)
    panel:SetScrollChild(list)

    local accept = button(frame, ACCEPT, 130, function()
        if EsteriaMountCustomize_Save(display, look()) then
            saved = { unpack(current) }
            PlaySound("igQuestListComplete")
            if EsteriaCosmetics_MountCustomized then EsteriaCosmetics_MountCustomized() end
        end
        frame:Hide()
    end, true)
    accept:SetPoint("BOTTOMRIGHT", -14, 16)
    local cancel = button(frame, CANCEL, 120, function() frame:Hide() end)
    cancel:SetPoint("RIGHT", accept, "LEFT", -8, 0)
    local reset = button(frame, RESET, 110, function()
        current = { unpack(saved) }
        for i in ipairs(options) do refreshRow(i) end
        preview()
    end)
    reset:SetPoint("BOTTOMLEFT", 14, 16)
    local random = button(frame, "Randomize", 110, function()
        for i, option in ipairs(options) do current[i] = math.random(#option.choices) end
        for i in ipairs(options) do refreshRow(i) end
        preview()
    end)
    random:SetPoint("LEFT", reset, "RIGHT", 8, 0)
end

-- Opens the customizer for a mount the journal has posed (its creature is cached, so its display is known).
function EsteriaMountCustomize_Open(creatureID, name)
    local d = creatureID and EsteriaMountCustomize_GetDisplay(creatureID)
    local opts = d and EsteriaMountCustomize_GetOptions(d)
    if not opts then return end
    if not frame then build() end
    display, creature, mountName, options = d, creatureID, name, opts
    -- Start from the player's saved look (else the display's own), matched to each option by choice.
    local chosen = {}
    for _, id in ipairs(EsteriaMountCustomize_GetLook(d)) do chosen[id] = true end
    for i, option in ipairs(options) do
        current[i] = 1
        for j, choice in ipairs(option.choices) do
            if chosen[choice.id] then current[i] = j end
        end
    end
    for i = #options + 1, #rows do rows[i]:Hide() end
    for i in ipairs(options) do row(i):Show(); refreshRow(i) end
    list:SetHeight(#options * (ROW_H + 18))
    saved = { unpack(current) }
    title:SetText("Customize " .. (name or ""))
    frame:Show()
    ES.Tween("mountcustomize-open", 0.22, 0, function(p) frame:SetAlpha(p) end)
    model:SetCreature(creatureID)
    model:SetRotation(model.rotation)
    preview()
end

-- Called by the journal's info pane: shows the Customize button for an owned customizable mount and dresses the
-- journal's own preview in the player's saved look.
function EsteriaMountCustomize_UpdateJournal(entry, journalModel)
    local owned = entry and entry.index and entry.creatureID
    local d = owned and EsteriaMountCustomize_GetDisplay(entry.creatureID)
    local customizable = d and EsteriaMountCustomize_GetOptions(d) ~= nil
    local anchor = _G.DragonUICollectionsActionButton
    if not journalButton and anchor then
        journalButton = CreateFrame("Button", "EsteriaMountCustomizeJournalButton", anchor:GetParent(), "UIPanelButtonTemplate")
        journalButton:SetSize(110, 22)
        journalButton:SetPoint("LEFT", anchor, "RIGHT", 6, 0)
        journalButton:SetText("Customize")
    end
    if not journalButton then return end
    if customizable then
        journalButton:SetScript("OnClick", function() EsteriaMountCustomize_Open(entry.creatureID, entry.name) end)
        journalButton:Show()
        if journalModel then
            local function dress() EsteriaMountCustomize_Preview(journalModel, d, EsteriaMountCustomize_GetLook(d)) end
            dress()
            -- shortcut: one delayed re-dress for a model still loading; a journal OnModelLoaded hook would be exact.
            journalButton.left = 0.8
            journalButton:SetScript("OnUpdate", function(self, elapsed)
                self.left = self.left - elapsed
                if self.left <= 0 then self:SetScript("OnUpdate", nil); dress() end
            end)
        end
    else
        journalButton:Hide()
    end
end
