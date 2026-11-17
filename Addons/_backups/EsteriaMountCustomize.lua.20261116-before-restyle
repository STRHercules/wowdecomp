-- Esteria mount customization: the retail drake/D.R.I.V.E. customizer, opened from the Mount Journal's Customize
-- button (DragonUI's journal calls EsteriaMountCustomize_UpdateJournal; Tools/EsteriaShell patches that in).
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

local function arrow(parent, text, onClick)
    local button = CreateFrame("Button", nil, parent)
    button:SetSize(26, 26)
    local bg = ES.Solid(button, "BACKGROUND", ES.INK, 0.9)
    bg:SetAllPoints()
    ES.Border(button, ES.GOLD_DIM)
    local label = ES.Text(button, 16, ES.GOLD)
    label:SetPoint("CENTER", 0, 1)
    label:SetText(text)
    button:SetScript("OnEnter", function() bg:SetVertexColor(0.16, 0.13, 0.07) end)
    button:SetScript("OnLeave", function() bg:SetVertexColor(ES.INK[1], ES.INK[2], ES.INK[3]) end)
    button:SetScript("OnClick", onClick)
    return button
end

local function row(i)
    if rows[i] then return rows[i] end
    local r = CreateFrame("Frame", nil, list)
    r:SetSize(PANEL_W - 24, ROW_H + 18)
    r:SetPoint("TOPLEFT", 4, -(i - 1) * (ROW_H + 18))
    r.label = ES.Text(r, 12, ES.GOLD)
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

local function button(parent, text, width, onClick)
    local b = CreateFrame("Button", nil, parent, "UIPanelButtonTemplate")
    b:SetSize(width, 24)
    b:SetText(text)
    b:SetScript("OnClick", onClick)
    return b
end

local function build()
    frame = CreateFrame("Frame", "EsteriaMountCustomizeFrame", UIParent)
    frame:SetSize(WIDTH, HEIGHT)
    frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH")
    frame:SetToplevel(true)
    frame:EnableMouse(true)
    frame:SetMovable(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide()
    tinsert(UISpecialFrames, "EsteriaMountCustomizeFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    ES.Border(frame, ES.GOLD)

    title = ES.Text(frame, 18, ES.GOLD)
    title:SetPoint("TOPLEFT", 18, -14)
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)

    -- The stage: a large preview you turn by dragging and zoom with the wheel, like retail's.
    model = CreateFrame("PlayerModel", nil, frame)
    model:SetPoint("TOPLEFT", 12, -44)
    model:SetPoint("BOTTOMRIGHT", -PANEL_W - 12, 52)
    local stage = ES.Solid(model, "BACKGROUND", { 0.07, 0.07, 0.09 }, 1)
    stage:SetAllPoints()
    ES.Border(model, ES.GOLD_DIM)
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

    local panel = CreateFrame("ScrollFrame", "EsteriaMountCustomizeScroll", frame, "UIPanelScrollFrameTemplate")
    panel:SetPoint("TOPRIGHT", -30, -48)
    panel:SetSize(PANEL_W - 20, HEIGHT - 110)
    list = CreateFrame("Frame", nil, panel)
    list:SetSize(PANEL_W - 20, 10)
    panel:SetScrollChild(list)

    local accept = button(frame, ACCEPT, 120, function()
        if EsteriaMountCustomize_Save(display, look()) then
            saved = { unpack(current) }
            PlaySound("igMainMenuOptionCheckBoxOn")
        end
        frame:Hide()
    end)
    accept:SetPoint("BOTTOMRIGHT", -14, 14)
    local cancel = button(frame, CANCEL, 120, function() frame:Hide() end)
    cancel:SetPoint("RIGHT", accept, "LEFT", -8, 0)
    local reset = button(frame, RESET, 110, function()
        current = { unpack(saved) }
        for i in ipairs(options) do refreshRow(i) end
        preview()
    end)
    reset:SetPoint("BOTTOMLEFT", 14, 14)
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
