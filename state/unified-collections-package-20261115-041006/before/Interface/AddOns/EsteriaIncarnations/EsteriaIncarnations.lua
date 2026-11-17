-- Esteria Incarnations (/incarnation, /incantation): choose the body of each shapeshift form and summoned demon.
-- Looks are EsteriaIncarnationsData (Tools/EclipseImport/eclipse_incarnations.py data). The realm saves and applies a
-- choice (EsteriaWoW Incarnations.h) so everyone sees it, at once on a form you wear or a demon you have out.
-- Previews: RetailModelBindDisplay dresses a model frame in a creature display, RetailModelReportLoaded frames it
-- (Extensions/RetailPreview). Wire: addon whispers to yourself, prefix ESTINC.
local ES = EsteriaStyle
local PREFIX = "ESTINC"
local W, H, RAIL_W, MARGIN = 1180, 680, 236, 14
local COLS, ROWS = 3, 2
local PER_PAGE = COLS * ROWS
local CARD_W, CARD_H, GAP = 170, 248, 12
local GRID_W = COLS * CARD_W + (COLS - 1) * GAP
local STAGE_W = W - MARGIN - (MARGIN + RAIL_W + 18 + GRID_W + 18)   -- whatever the grid leaves, so nothing overlaps
local DEFAULT = 0
local GROUPS = { { kind = "form", title = "SHAPESHIFT FORMS" }, { kind = "summon", title = "DEMONS" } }
local HINT = { form = "Shows whenever you shapeshift into %s.", summon = "Your %s is summoned in this body." }

local frame, rail, grid, stage, search, pageText, prevButton, nextButton, applyButton, defaultButton
local categories, filtered, cards, railRows = {}, {}, {}, {}
local selected, page, focus = nil, 1, nil   -- focus: the look on the stage (a display, or DEFAULT)
local choices = {}                           -- category id -> chosen display

-- ---------------------------------------------------------------------------------------------------------------
-- helpers
-- ---------------------------------------------------------------------------------------------------------------
local function send(body) SendAddonMessage(PREFIX, body, "WHISPER", UnitName("player")) end

local function shown(region, on) if on then region:Show() else region:Hide() end end

local function shortName(category) return (category.name:gsub("^Summoned ", "")) end

-- The stock body of a slot, as the realm picks it: your race's form model, else your faction's; a demon's own.
local function defaultLook(category)
    local d = category.default
    if not d then return nil end
    local pick = d.any
    if not pick then
        local _, raceFile = UnitRace("player")
        local race = EsteriaIncarnationsRaces and EsteriaIncarnationsRaces[raceFile]
        pick = (race and d.races and d.races[race]) or (UnitFactionGroup("player") == "Horde" and d.h or d.a)
    end
    return pick and { pick[1], "Default", pick[2] } or nil
end

local function lookName(category, display)
    if not display or display == DEFAULT then return "Default" end
    for _, look in ipairs(category.looks) do if look[1] == display then return look[2] end end
    return "Custom"
end

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

-- Dress a model frame in one creature display; it is framed to its bounds once loaded (modelFrame's OnUpdate).
local function showLook(model, look)
    model.waiting = nil
    model:ClearModel()
    if not look then return end
    model:SetModel(look[3])
    if RetailModelBindDisplay then RetailModelBindDisplay(model, look[1]) end
    model.waiting = true
end

-- Frame the loaded model by its real vertex box (RetailModelFit); older clients fall back to the header bounds.
local function fit(model)
    if RetailModelFit then return RetailModelFit(model, model.zoom or 1) end
    return not RetailModelReportLoaded or RetailModelReportLoaded(model)
end

local function modelFrame(parent)
    local model = CreateFrame("PlayerModel", nil, parent)
    model.facing = -0.5
    model:SetScript("OnUpdate", function(self, elapsed)
        if self.waiting and fit(self) then
            self.waiting = nil
            self:SetFacing(self.facing)
        end
        if self.dragX then
            local x = GetCursorPosition()
            self.facing = self.facing + (x - self.dragX) * 0.012
            self.dragX = x
            self:SetFacing(self.facing)
        elseif self.spin and not self.waiting then
            self.facing = self.facing + elapsed * self.spin
            self:SetFacing(self.facing)
        end
    end)
    return model
end

local function iconFrame(parent, size)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetSize(size + 2, size + 2)
    holder.icon = holder:CreateTexture(nil, "ARTWORK")
    holder.icon:SetPoint("TOPLEFT", 1, -1)
    holder.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    holder.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    ES.Border(holder, ES.GOLD_DIM)
    return holder
end

-- ---------------------------------------------------------------------------------------------------------------
-- state
-- ---------------------------------------------------------------------------------------------------------------
local refresh

local function choose(display)
    focus = display
    refresh()
end

local function selectCategory(category)
    selected, page, focus = category, 1, choices[category.id] or DEFAULT
    if search then search:SetText("") end
    refresh()
    ES.Tween("incarnations-grid", 0.25, 0, function(p) grid:SetAlpha(p) end)
end

local function applyFocus()
    if selected then send("set\t" .. selected.id .. "\t" .. (focus or DEFAULT)) end
end

-- ---------------------------------------------------------------------------------------------------------------
-- category rail
-- ---------------------------------------------------------------------------------------------------------------
local function buildRail()
    rail = CreateFrame("Frame", nil, frame)
    rail:SetPoint("TOPLEFT", 14, -64)
    rail:SetPoint("BOTTOMLEFT", 14, 16)
    rail:SetWidth(RAIL_W)
    ES.Solid(rail, "BACKGROUND", { 0.05, 0.06, 0.08 }, 0.9):SetAllPoints()
    ES.Border(rail, ES.GOLD_DIM, 0.6)
    local y = -10
    for _, group in ipairs(GROUPS) do
        local any = false
        for _, category in ipairs(categories) do if category.kind == group.kind then any = true end end
        if any then
            local head = ES.Text(rail, 10, ES.GOLD_DIM)
            head:SetPoint("TOPLEFT", 14, y)
            head:SetText(group.title)
            y = y - 18
            for _, category in ipairs(categories) do
                if category.kind == group.kind then
                    local row = CreateFrame("Button", nil, rail)
                    row:SetSize(RAIL_W - 12, 38)
                    row:SetPoint("TOPLEFT", 6, y)
                    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0)
                    row.wash:SetAllPoints()
                    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
                    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
                    local icon = iconFrame(row, 28)
                    icon:SetPoint("LEFT", 10, 0)
                    icon.icon:SetTexture(category.icon)
                    row.name = ES.Text(row, 12, ES.TEXT)
                    row.name:SetPoint("TOPLEFT", icon, "TOPRIGHT", 10, -2)
                    row.name:SetText(shortName(category))
                    row.sub = ES.Text(row, 10, ES.MUTED)
                    row.sub:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3)
                    row.sub:SetPoint("RIGHT", -22, 0)
                    row.sub:SetJustifyH("LEFT")
                    row.dot = ES.Solid(row, "OVERLAY", ES.GOOD, 1)
                    row.dot:SetSize(6, 6)
                    row.dot:SetPoint("RIGHT", -12, 0)
                    row.category = category
                    row:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); selectCategory(category) end)
                    row:SetScript("OnEnter", function(self) if self.category ~= selected then self.wash:SetAlpha(0.06) end end)
                    row:SetScript("OnLeave", function(self) if self.category ~= selected then self.wash:SetAlpha(0) end end)
                    railRows[#railRows + 1] = row
                    y = y - 40
                end
            end
            y = y - 8
        end
    end
end

local function paintRail()
    for _, row in ipairs(railRows) do
        local on, chosen = row.category == selected, choices[row.category.id]
        row.wash:SetAlpha(on and 0.12 or 0)
        row.accent:SetAlpha(on and 1 or 0)
        row.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        if chosen then
            row.sub:SetText("|cff8cd97f" .. lookName(row.category, chosen) .. "|r")
        else
            row.sub:SetText(#row.category.looks .. " looks")
        end
        shown(row.dot, chosen ~= nil)
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- grid of preview cards
-- ---------------------------------------------------------------------------------------------------------------
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
    card.model = modelFrame(card)
    card.model:SetPoint("TOPLEFT", 2, -2)
    card.model:SetPoint("BOTTOMRIGHT", -2, 46)
    card.icon = iconFrame(card, 64)
    card.icon:SetPoint("CENTER", 0, 24)
    card.plate = ES.Solid(card, "ARTWORK", ES.INK, 0.92)
    card.plate:SetPoint("BOTTOMLEFT", 1, 1); card.plate:SetPoint("BOTTOMRIGHT", -1, 1); card.plate:SetHeight(45)
    card.name = ES.Text(card, 11, ES.TEXT)
    card.name:SetPoint("TOPLEFT", card.plate, "TOPLEFT", 8, -6)
    card.name:SetPoint("TOPRIGHT", card.plate, "TOPRIGHT", -8, -6)
    card.name:SetJustifyH("CENTER")
    card.name:SetHeight(32)
    card.badge = CreateFrame("Frame", nil, card)
    card.badge:SetSize(58, 16)
    card.badge:SetPoint("TOPRIGHT", -6, -6)
    card.badge:SetFrameLevel(card.model:GetFrameLevel() + 2)
    ES.Solid(card.badge, "BACKGROUND", { 0.16, 0.32, 0.12 }, 0.95):SetAllPoints()
    ES.Border(card.badge, ES.GOOD)
    local badgeText = ES.Text(card.badge, 9, { 0.85, 1, 0.8 })
    badgeText:SetPoint("CENTER", 0, 0)
    badgeText:SetText("ACTIVE")
    card.sweep = ES.Solid(card, "OVERLAY", ES.GOLD, 0)
    card.sweep:SetAllPoints()
    card:RegisterForClicks("LeftButtonUp")
    card:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); choose(self.display) end)
    card:SetScript("OnDoubleClick", function(self) choose(self.display); applyFocus() end)
    card:SetScript("OnEnter", function(self)
        self.model.spin = 0.9
        if self.display ~= focus then ES.Tint(self.lines, ES.GOLD, 0.9) end
        ES.Tween("incarnations-card" .. i, 0.18, 0, function(p) self.sweep:SetAlpha(0.07 * p) end)
    end)
    card:SetScript("OnLeave", function(self)
        self.model.spin = nil
        if self.display ~= focus then ES.Tint(self.lines, ES.GOLD_DIM, 0.7) end
        self.sweep:SetAlpha(0)
    end)
    card:EnableMouseWheel(true)
    card:SetScript("OnMouseWheel", function(_, delta)
        if delta > 0 and page > 1 then page = page - 1; refresh()
        elseif delta < 0 then page = page + 1; refresh() end
    end)
    return card
end

local function paintGrid()
    local total = #filtered + 1                                   -- slot 1 is the stock body
    local pages = math.max(1, math.ceil(total / PER_PAGE))
    page = math.min(math.max(page, 1), pages)
    pageText:SetText(("Page %d / %d"):format(page, pages))
    prevButton:SetActive(page > 1)
    nextButton:SetActive(page < pages)
    local current = choices[selected.id] or DEFAULT
    for i = 1, PER_PAGE do
        local card, index = cards[i], (page - 1) * PER_PAGE + i
        if index <= total then
            local look = index > 1 and filtered[index - 1] or nil
            local display = look and look[1] or DEFAULT
            if card.display ~= display or card.category ~= selected then
                card.display, card.category = display, selected
                local shownLook = look or defaultLook(selected)
                if shownLook then
                    card.icon:Hide()
                    card.model:Show()
                    showLook(card.model, shownLook)
                else
                    card.model:Hide()
                    card.icon.icon:SetTexture(selected.icon)
                    card.icon:Show()
                end
                card.name:SetText(look and look[2] or ("Default\n|cff9a968cyour own " .. shortName(selected):lower() .. "|r"))
            end
            shown(card.badge, display == current)
            local on = display == focus
            ES.Tint(card.lines, on and ES.GOLD or ES.GOLD_DIM, on and 1 or 0.7)
            card.bg:SetVertexColor(on and 0.13 or 0.06, on and 0.11 or 0.07, on and 0.07 or 0.10)
            card:Show()
        else
            card.display = nil
            card:Hide()
        end
    end
end

-- ---------------------------------------------------------------------------------------------------------------
-- stage
-- ---------------------------------------------------------------------------------------------------------------
local function buildStage()
    stage = CreateFrame("Frame", nil, frame)
    stage:SetPoint("TOPRIGHT", -14, -64)
    stage:SetPoint("BOTTOMRIGHT", -14, 16)
    stage:SetWidth(STAGE_W)
    ES.Solid(stage, "BACKGROUND", { 0.05, 0.05, 0.07 }, 1):SetAllPoints()
    local glow = stage:CreateTexture(nil, "BORDER")
    glow:SetTexture(ES.WHITE)
    glow:SetGradientAlpha("VERTICAL", 0.62, 0.46, 0.15, 0.3, 0.62, 0.46, 0.15, 0)
    glow:SetPoint("BOTTOMLEFT", 0, 170); glow:SetPoint("BOTTOMRIGHT", 0, 170); glow:SetHeight(170)
    ES.Border(stage, ES.GOLD, 0.8)
    stage.model = modelFrame(stage)
    stage.model:SetPoint("TOPLEFT", 4, -4)
    stage.model:SetPoint("BOTTOMRIGHT", -4, 170)
    stage.model.spin = 0.25
    stage.model:EnableMouse(true)
    stage.model:SetScript("OnMouseDown", function(self) self.dragX, self.spin = GetCursorPosition(), nil end)
    stage.model:SetScript("OnMouseUp", function(self) self.dragX = nil end)
    stage.model:EnableMouseWheel(true)
    stage.model:SetScript("OnMouseWheel", function(self, delta)
        self.zoom = math.max(0.6, math.min(3, (self.zoom or 1) + delta * 0.2))
        if not self.waiting then fit(self); self:SetFacing(self.facing) end
    end)
    stage.icon = iconFrame(stage, 96)
    stage.icon:SetPoint("CENTER", stage.model, "CENTER")
    stage.hint = ES.Text(stage, 9, ES.MUTED)
    stage.hint:SetPoint("BOTTOM", stage.model, "BOTTOM", 0, 6)
    stage.hint:SetText("Drag to rotate  ·  Scroll to zoom")

    local info = CreateFrame("Frame", nil, stage)
    info:SetPoint("BOTTOMLEFT", 1, 1); info:SetPoint("BOTTOMRIGHT", -1, 1); info:SetHeight(168)
    ES.Solid(info, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    local rule = ES.Solid(info, "ARTWORK", ES.GOLD_DIM, 0.8)
    rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    stage.kind = ES.Text(info, 10, ES.GOLD_DIM)
    stage.kind:SetPoint("TOPLEFT", 16, -14)
    stage.name = ES.Text(info, 17, ES.GOLD)
    stage.name:SetPoint("TOPLEFT", stage.kind, "BOTTOMLEFT", 0, -6)
    stage.name:SetPoint("RIGHT", -16, 0)
    stage.name:SetJustifyH("LEFT")
    stage.state = ES.Text(info, 11, ES.MUTED)
    stage.state:SetPoint("TOPLEFT", stage.name, "BOTTOMLEFT", 0, -6)
    stage.note = ES.Text(info, 10, ES.MUTED)
    stage.note:SetPoint("TOPLEFT", stage.state, "BOTTOMLEFT", 0, -6)
    stage.note:SetPoint("RIGHT", -16, 0)
    stage.note:SetJustifyH("LEFT")
    applyButton = flatButton(info, 150, 30, "Apply Look", true)
    applyButton:SetPoint("BOTTOMRIGHT", -14, 14)
    applyButton:SetScript("OnClick", applyFocus)
    defaultButton = flatButton(info, 130, 30, "Use Default")
    defaultButton:SetPoint("RIGHT", applyButton, "LEFT", -8, 0)
    defaultButton:SetScript("OnClick", function() focus = DEFAULT; applyFocus() end)
end

local function paintStage()
    local current = choices[selected.id] or DEFAULT
    local look
    for _, l in ipairs(selected.looks) do if l[1] == focus then look = l end end
    local shownLook = look or defaultLook(selected)
    if shownLook then
        stage.icon:Hide(); stage.model:Show(); stage.hint:Show()
        local key = shownLook[1] .. ":" .. selected.id
        if stage.model.display ~= key then stage.model.display = key; showLook(stage.model, shownLook) end
    else
        stage.model.display = nil
        stage.model:ClearModel(); stage.model:Hide(); stage.hint:Hide()
        stage.icon.icon:SetTexture(selected.icon); stage.icon:Show()
    end
    stage.kind:SetText(selected.name:upper())
    stage.name:SetText(look and look[2] or "Default")
    if focus == current then
        stage.state:SetText("|cff8cd97fActive|r")
    else
        stage.state:SetText("Active: " .. lookName(selected, current))
    end
    stage.note:SetText(HINT[selected.kind]:format(shortName(selected)))
    applyButton:SetActive(focus ~= current)
    applyButton:SetLabel(focus == current and "Applied" or "Apply Look")
    defaultButton:SetActive(current ~= DEFAULT)
end

refresh = function()
    if not frame or not frame:IsShown() or not selected then return end
    local query = search and search:GetText():lower() or ""
    filtered = {}
    for _, look in ipairs(selected.looks) do
        if query == "" or look[2]:lower():find(query, 1, true) then filtered[#filtered + 1] = look end
    end
    frame.count:SetText(("%d of %d looks"):format(#filtered, #selected.looks))
    paintRail()
    paintGrid()
    paintStage()
end

-- ---------------------------------------------------------------------------------------------------------------
-- window
-- ---------------------------------------------------------------------------------------------------------------
local function build()
    for _, category in ipairs(EsteriaIncarnationsData or {}) do
        if #category.looks > 0 then categories[#categories + 1] = category end
    end
    frame = CreateFrame("Frame", "EsteriaIncarnationsFrame", UIParent)
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
    tinsert(UISpecialFrames, "EsteriaIncarnationsFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
    local header = frame:CreateTexture(nil, "BORDER")
    header:SetTexture(ES.WHITE)
    header:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    header:SetPoint("TOPLEFT", 1, -1); header:SetPoint("TOPRIGHT", -1, -1); header:SetHeight(56)
    ES.Border(frame, ES.GOLD)

    local title = ES.Text(frame, 22, ES.GOLD)
    title:SetPoint("TOPLEFT", 20, -12)
    title:SetText("Incarnations")
    local subtitle = ES.Text(frame, 11, ES.MUTED)
    subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 1, -4)
    subtitle:SetText("Choose the body you take in each form and the demons you summon. Everyone sees your choice.")
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)

    buildRail()
    buildStage()

    grid = CreateFrame("Frame", nil, frame)
    grid:SetPoint("TOPLEFT", rail, "TOPRIGHT", 18, -40)
    grid:SetSize(COLS * CARD_W + (COLS - 1) * GAP, ROWS * CARD_H + (ROWS - 1) * GAP)
    for i = 1, PER_PAGE do cards[i] = buildCard(i) end

    search = CreateFrame("EditBox", "EsteriaIncarnationsSearch", frame, "InputBoxTemplate")
    search:SetSize(220, 20)
    search:SetPoint("BOTTOMLEFT", grid, "TOPLEFT", 6, 12)
    search:SetAutoFocus(false)
    search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    local placeholder = ES.Text(search, 11, ES.MUTED)
    placeholder:SetPoint("LEFT", 2, 0)
    placeholder:SetText("Search looks")
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

    frame:SetScript("OnShow", function(self)
        PlaySound("igCharacterInfoOpen")
        if not selected and categories[1] then selectCategory(categories[1]) else refresh() end
        ES.Tween("incarnations-open", 0.22, 0, function(p) self:SetAlpha(p) end)
    end)
    frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose") end)
end

-- ---------------------------------------------------------------------------------------------------------------
-- realm traffic and slash commands
-- ---------------------------------------------------------------------------------------------------------------
local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(_, event, prefix, message, _, sender)
    if event == "PLAYER_LOGIN" then send("list"); return end
    if prefix ~= PREFIX or sender ~= UnitName("player") then return end
    local kind, a, b = strsplit("\t", message)
    if kind == "list" then
        choices = {}
        for category, display in string.gmatch(a or "", "(%d+):(%d+)") do choices[tonumber(category)] = tonumber(display) end
    elseif kind == "ok" then
        choices[tonumber(a)] = tonumber(b) ~= 0 and tonumber(b) or nil
        PlaySound("igQuestListComplete")
        if frame and frame:IsShown() then
            ES.Tween("incarnations-applied", 0.4, 0, function(p) stage.name:SetAlpha(0.4 + 0.6 * p) end)
        end
    elseif kind == "err" then
        UIErrorsFrame:AddMessage("That incarnation is not available to you.", 1, 0.1, 0.1)
    end
    refresh()
end)

local function toggle()
    if not frame then build() end
    if #categories == 0 then
        DEFAULT_CHAT_FRAME:AddMessage("|cff00ccff[Incarnations]|r No incarnations are installed.")
        return
    end
    shown(frame, not frame:IsShown())
end

EsteriaIncarnations_Toggle = toggle   -- DragonUI's micro strip (Addons/EsteriaShell/esteria_microbuttons.lua)

-- Without DragonUI, a button on the stock micro bar, after Neighborhood's (or Help's) as EsteriaHousing does.
if not _G.IncarnationMicroButton and MainMenuBarArtFrame then
    local anchor = _G.NeighborhoodMicroButton or HelpMicroButton
    local b = CreateFrame("Button", "IncarnationMicroButton", MainMenuBarArtFrame, "MainMenuBarMicroButton")
    b:SetPoint("BOTTOMLEFT", anchor, "BOTTOMRIGHT", -3, 0)
    b:SetNormalTexture("Interface\\Esteria\\IncarnationMicro-Up")
    b:SetPushedTexture("Interface\\Esteria\\IncarnationMicro-Down")
    b:SetDisabledTexture("Interface\\Esteria\\IncarnationMicro-Disabled")
    b:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight")
    b:SetScript("OnClick", toggle)
    b:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Incarnations", 1, 0.82, 0.1)
        GameTooltip:AddLine("Choose the body of your shapeshift forms and summoned demons.", 1, 1, 1, true)
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    hooksecurefunc("VehicleMenuBar_MoveMicroButtons", function() b:SetParent(anchor:GetParent()) end)
end

SLASH_ESTERIAINCARNATIONS1, SLASH_ESTERIAINCARNATIONS2 = "/incarnation", "/incarnations"
SLASH_ESTERIAINCARNATIONS3, SLASH_ESTERIAINCARNATIONS4 = "/incantation", "/incantations"
SlashCmdList.ESTERIAINCARNATIONS = toggle
