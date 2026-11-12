-- Body Slider window (/bodyslider), in the Esteria Incarnations look: a category rail, a slider pane and a stage
-- with a live preview of your own model. BodySlider_Set(previewFrame) asks BodySlider.dll to shape the preview with
-- your values every frame. Presets are saved per race and sex in BodySliderDB.presets; share codes are the same
-- BodySlider/2 strings the MSP field carries (Protocol.lua Encode/Decode).
local addon = BodySlider
local ES = EsteriaStyle
local W, H, RAIL_W, PANE_W, MARGIN = 1000, 620, 200, 400, 14
local SLIDER_W, ROW_H = PANE_W - 40, 34
local PRESET_ROWS, MAX_PRESETS, MAX_NAME = 8, 60, 32
local PAGES = {
    { title = "Body", first = 1, last = 14, icon = "Interface\\Icons\\INV_Chest_Cloth_17", zoom = 0 },
    { title = "Face", first = 15, last = 26, icon = "Interface\\Icons\\INV_Misc_Head_Human_01", zoom = 1.6 },
    { title = "Features", first = 27, last = 37, icon = "Interface\\Icons\\Spell_Shadow_MindSteal", zoom = 1.6 },
    { title = "Presets", presets = true, icon = "Interface\\Icons\\INV_Misc_Note_01", zoom = 0 },
}

local function minimum(name) return (name == "BreastSize" or name == "ButtSize") and 0 or -1 end
local function label(name) return (name:gsub("(%l)(%u)", "%1 %2")) end
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
    b.SetActive = function(self, on)
        if on then self:Enable(); self.label:SetAlpha(1); self.bg:SetAlpha(primary and 0.95 or 0.9)
        else self:Disable(); self.label:SetAlpha(0.35); self.bg:SetAlpha(0.5); self.glow:SetAlpha(0) end
    end
    return b
end

local function field(parent, name, width, maxLetters)
    local box = CreateFrame("EditBox", name, parent, "InputBoxTemplate")
    box:SetSize(width, 20)
    box:SetAutoFocus(false)
    box:SetMaxLetters(maxLetters)
    box:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    return box
end

local login = CreateFrame("Frame")
login:RegisterEvent("PLAYER_LOGIN")
login:SetScript("OnEvent", function()
    local ready = type(BodySlider_Supported) == "function" and BodySlider_Supported()
    if not ready then
        SlashCmdList.BODYSLIDER = function()
            DEFAULT_CHAT_FRAME:AddMessage("Body Slider requires the supported client patch and BodySlider.dll.")
        end
        SLASH_BODYSLIDER1 = "/bodyslider"
        return
    end
    local key = GetRealmName() .. ":" .. UnitName("player")
    if type(BodySliderDB) ~= "table" then BodySliderDB = {} end
    if type(BodySliderDB[key]) ~= "table" then BodySliderDB[key] = {} end
    local store = BodySliderDB[key]
    if type(store.values) ~= "table" then store.values = {} end
    local revision = tonumber(store.version)
    store.version = revision and revision == revision and revision >= 0 and revision <= 1000000000
        and math.floor(revision) or 0
    local guid, available, sliders = UnitGUID("player"), {}, {}
    BodySlider_Clear() -- A UI reload must not leave peers cached by the previous addon instance.
    for _, name in ipairs(BodySlider_Names()) do available[name] = true end
    for _, name in ipairs(addon.names) do
        local value = tonumber(store.values[name]) or 0
        if value ~= value or value < minimum(name) or value > 1 then value = 0 end
        store.values[name] = value
        if available[name] then BodySlider_Set(guid, name, value) end
    end

    -- Presets: account-wide, one list per race and sex. Hand-edited SavedVariables are sanitized here.
    if type(BodySliderDB.presets) ~= "table" then BodySliderDB.presets = {} end
    for listKey, list in pairs(BodySliderDB.presets) do
        local clean = {}
        if type(list) == "table" then
            for _, preset in ipairs(list) do
                if type(preset) == "table" and type(preset.name) == "string" and preset.name ~= ""
                    and #preset.name <= MAX_NAME and addon.Decode(preset.code) and #clean < MAX_PRESETS then
                    clean[#clean + 1] = { name = preset.name, code = preset.code }
                end
            end
        end
        BodySliderDB.presets[listKey] = clean
    end
    local raceName, raceFile = UnitRace("player")
    local sexName = UnitSex("player") == 3 and "Female" or "Male"
    local presetKey = (raceFile or "Unknown") .. ":" .. sexName
    BodySliderDB.presets[presetKey] = BodySliderDB.presets[presetKey] or {}
    local presets = BodySliderDB.presets[presetKey]

    local panel, rail, pane, stage, presetPane, note, codeBox, nameBox, status, preview
    local railRows, presetRows, selected, presetPage, snapshot, syncing = {}, {}, 1, 1, nil, false
    local refresh

    local function changedCount(first, last)
        local count = 0
        for i = first or 1, last or #addon.names do
            if store.values[addon.names[i]] ~= 0 then count = count + 1 end
        end
        return count
    end

    local function say(text, good)
        status:SetText(text)
        status:SetTextColor(unpack(good and ES.GOOD or ES.BAD))
        ES.Tween("bodyslider-status", 0.3, 0, function(p) status:SetAlpha(p) end)
    end

    -- Every full-set change (preset, code, reset, revert) goes through here; values are already validated.
    local function applyValues(values)
        syncing = true
        for _, name in ipairs(addon.names) do
            local value = values[name] or 0
            store.values[name] = value
            if available[name] then BodySlider_Set(guid, name, value) end
            sliders[name]:SetValue(value)
        end
        syncing = false
        refresh()
    end

    local function copyValues()
        local copy = {}
        for _, name in ipairs(addon.names) do copy[name] = store.values[name] end
        return copy
    end

    -- -----------------------------------------------------------------------------------------------------------
    -- window and rail
    -- -----------------------------------------------------------------------------------------------------------
    panel = CreateFrame("Frame", "BodySliderPanel", UIParent)
    panel:SetSize(W, H)
    panel:SetPoint("CENTER")
    panel:SetFrameStrata("HIGH")
    panel:SetToplevel(true)
    panel:EnableMouse(true)
    panel:SetMovable(true)
    panel:SetClampedToScreen(true)
    panel:RegisterForDrag("LeftButton")
    panel:SetScript("OnDragStart", panel.StartMoving)
    panel:SetScript("OnDragStop", panel.StopMovingOrSizing)
    panel:Hide()
    tinsert(UISpecialFrames, "BodySliderPanel")
    ES.Solid(panel, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
    local header = panel:CreateTexture(nil, "BORDER")
    header:SetTexture(ES.WHITE)
    header:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    header:SetPoint("TOPLEFT", 1, -1); header:SetPoint("TOPRIGHT", -1, -1); header:SetHeight(56)
    ES.Border(panel, ES.GOLD)
    local title = ES.Text(panel, 22, ES.GOLD)
    title:SetPoint("TOPLEFT", 20, -12)
    title:SetText("Body Slider")
    local subtitle = ES.Text(panel, 11, ES.MUTED)
    subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 1, -4)
    subtitle:SetText("Shape your body and face. Players running Body Slider see your shape.")
    local close = CreateFrame("Button", nil, panel, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 2, 2)

    rail = CreateFrame("Frame", nil, panel)
    rail:SetPoint("TOPLEFT", MARGIN, -64)
    rail:SetPoint("BOTTOMLEFT", MARGIN, 16)
    rail:SetWidth(RAIL_W)
    ES.Solid(rail, "BACKGROUND", { 0.05, 0.06, 0.08 }, 0.9):SetAllPoints()
    ES.Border(rail, ES.GOLD_DIM, 0.6)
    local y = -10
    for i, page in ipairs(PAGES) do
        if i == 1 or page.presets then
            local head = ES.Text(rail, 10, ES.GOLD_DIM)
            head:SetPoint("TOPLEFT", 14, y)
            head:SetText(page.presets and "SHARING" or "SHAPE")
            y = y - 18
        end
        local row = CreateFrame("Button", "BodySliderPage" .. i, rail)
        row:SetSize(RAIL_W - 12, 40)
        row:SetPoint("TOPLEFT", 6, y)
        row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0)
        row.wash:SetAllPoints()
        row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
        row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
        local icon = CreateFrame("Frame", nil, row)
        icon:SetSize(30, 30)
        icon:SetPoint("LEFT", 10, 0)
        local tex = icon:CreateTexture(nil, "ARTWORK")
        tex:SetPoint("TOPLEFT", 1, -1); tex:SetPoint("BOTTOMRIGHT", -1, 1)
        tex:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        tex:SetTexture(page.icon)
        ES.Border(icon, ES.GOLD_DIM)
        row.name = ES.Text(row, 12, ES.TEXT)
        row.name:SetPoint("TOPLEFT", icon, "TOPRIGHT", 10, -2)
        row.name:SetText(page.title)
        row.sub = ES.Text(row, 10, ES.MUTED)
        row.sub:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3)
        row.sub:SetPoint("RIGHT", -22, 0)
        row.sub:SetJustifyH("LEFT")
        row.dot = ES.Solid(row, "OVERLAY", ES.GOOD, 1)
        row.dot:SetSize(6, 6)
        row.dot:SetPoint("RIGHT", -12, 0)
        row:SetScript("OnClick", function()
            PlaySound("igMainMenuOptionCheckBoxOn")
            selected = i
            preview.zoomTarget = page.zoom
            refresh()
            ES.Tween("bodyslider-pane", 0.25, 0, function(p) pane:SetAlpha(p); presetPane:SetAlpha(p) end)
        end)
        row:SetScript("OnEnter", function(self) if selected ~= i then self.wash:SetAlpha(0.06) end end)
        row:SetScript("OnLeave", function(self) if selected ~= i then self.wash:SetAlpha(0) end end)
        railRows[i] = row
        y = y - 44
    end

    -- -----------------------------------------------------------------------------------------------------------
    -- slider pane
    -- -----------------------------------------------------------------------------------------------------------
    pane = CreateFrame("Frame", nil, panel)
    pane:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0)
    pane:SetPoint("BOTTOMLEFT", rail, "BOTTOMRIGHT", 14, 0)
    pane:SetWidth(PANE_W)
    ES.Solid(pane, "BACKGROUND", { 0.05, 0.06, 0.08 }, 0.9):SetAllPoints()
    ES.Border(pane, ES.GOLD_DIM, 0.6)
    pane.title = ES.Text(pane, 15, ES.GOLD)
    pane.title:SetPoint("TOPLEFT", 20, -14)
    note = ES.Text(pane, 10, ES.MUTED)
    note:SetPoint("TOPLEFT", pane.title, "BOTTOMLEFT", 0, -4)
    note:SetPoint("RIGHT", -20, 0)
    note:SetJustifyH("LEFT")

    for i, name in ipairs(addon.names) do
        local page = i <= 14 and 1 or i <= 26 and 2 or 3
        local row = CreateFrame("Frame", nil, pane)
        row:SetSize(SLIDER_W, ROW_H)
        row:SetPoint("TOPLEFT", 20, -58 - (i - PAGES[page].first) * ROW_H)
        row.page = page
        row.label = ES.Text(row, 12, ES.TEXT)
        row.label:SetPoint("TOPLEFT", 0, -2)
        row.label:SetText(label(name))
        row.value = ES.Text(row, 11, ES.GOLD)
        row.value:SetPoint("TOPRIGHT", 0, -2)
        local low = minimum(name)
        local slider = CreateFrame("Slider", "BodySlider_" .. name, row)
        slider:SetOrientation("HORIZONTAL")
        slider:SetSize(SLIDER_W, 14)
        slider:SetPoint("BOTTOMLEFT", 0, 3)
        slider:SetMinMaxValues(low, 1)
        slider:SetValueStep(0.01)
        slider:EnableMouseWheel(true)
        local track = ES.Solid(slider, "BACKGROUND", { 0.16, 0.15, 0.13 }, 1)
        track:SetPoint("LEFT"); track:SetPoint("RIGHT"); track:SetHeight(4)
        local fill = ES.Solid(slider, "BORDER", ES.GOLD_DIM, 1)
        fill:SetHeight(4)
        if low < 0 then
            local tick = ES.Solid(slider, "ARTWORK", ES.MUTED, 0.8)
            tick:SetSize(1, 10)
            tick:SetPoint("CENTER")
        end
        slider:SetThumbTexture(ES.WHITE)
        local thumb = slider:GetThumbTexture()
        thumb:SetSize(8, 16)
        thumb:SetVertexColor(ES.GOLD[1], ES.GOLD[2], ES.GOLD[3])
        local function paint(value)
            -- The fill runs from 0 (the stock shape) to the value, so a glance shows which way each control leans.
            local zero = -low / (1 - low) * SLIDER_W
            local at = (value - low) / (1 - low) * SLIDER_W
            fill:ClearAllPoints()
            fill:SetPoint("LEFT", slider, "LEFT", math.min(zero, at), 0)
            fill:SetWidth(math.max(1, math.abs(at - zero)))
            fill:SetVertexColor(unpack(value ~= 0 and ES.GOLD or ES.GOLD_DIM))
            row.value:SetText(row.enabled == false and "n/a" or string.format("%+d%%", math.floor(value * 100 + 0.5)))
        end
        slider:SetScript("OnValueChanged", function(_, value)
            value = math.floor(value * 100 + 0.5) / 100
            paint(value)
            if syncing then return end
            store.values[name] = value
            BodySlider_Set(guid, name, value)
        end)
        slider:SetScript("OnMouseUp", function(self, button) if button == "RightButton" then self:SetValue(0) end end)
        slider:SetScript("OnMouseWheel", function(self, delta) self:SetValue(self:GetValue() + delta * 0.05) end)
        slider:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(label(name), 1, 0.82, 0.1)
            GameTooltip:AddLine("Drag or scroll to adjust. Right-click resets.", 1, 1, 1, true)
            GameTooltip:Show()
        end)
        slider:SetScript("OnLeave", function() GameTooltip:Hide() end)
        slider:SetValue(store.values[name])
        paint(store.values[name])
        slider.row, slider.paint = row, paint
        sliders[name] = slider
    end

    -- -----------------------------------------------------------------------------------------------------------
    -- presets pane: share code, saved list for this race and sex, save field
    -- -----------------------------------------------------------------------------------------------------------
    presetPane = CreateFrame("Frame", nil, panel)
    presetPane:SetAllPoints(pane)
    presetPane:Hide()
    local function heading(text, anchor, gap)
        local fs = ES.Text(presetPane, 10, ES.GOLD_DIM)
        if anchor then fs:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", 0, -gap) else fs:SetPoint("TOPLEFT", 20, -58) end
        fs:SetText(text)
        return fs
    end
    local codeHead = heading("SHARE CODE")
    codeBox = field(presetPane, "BodySliderCode", SLIDER_W - 8, 220)
    codeBox:SetPoint("TOPLEFT", codeHead, "BOTTOMLEFT", 6, -8)
    codeBox:SetScript("OnEditFocusGained", function(self) self:HighlightText() end)
    codeBox:SetScript("OnEnterPressed", function(self) self.apply:Click() end)
    local copy = flatButton(presetPane, 120, 26, "Copy")
    copy:SetPoint("TOPLEFT", codeBox, "BOTTOMLEFT", -6, -8)
    copy:SetScript("OnClick", function()
        codeBox:SetText(addon.Encode(store.values))
        codeBox:SetFocus()
        codeBox:HighlightText()
        say("Press Ctrl+C to copy the code.", true)
    end)
    codeBox.apply = flatButton(presetPane, 120, 26, "Apply Code", true)
    codeBox.apply:SetPoint("LEFT", copy, "RIGHT", 8, 0)
    codeBox.apply:SetScript("OnClick", function()
        local values = addon.Decode(strtrim(codeBox:GetText() or ""))
        if not values then say("That is not a Body Slider code."); return end
        codeBox:ClearFocus()
        applyValues(values)
        say("Code applied.", true)
    end)
    local codeTip = ES.Text(presetPane, 10, ES.MUTED)
    codeTip:SetPoint("LEFT", codeBox.apply, "RIGHT", 10, 0)
    codeTip:SetPoint("RIGHT", presetPane, "RIGHT", -20, 0)
    codeTip:SetJustifyH("LEFT")
    codeTip:SetText("Paste a code, then Apply.")

    local listHead = heading("SAVED", copy, 22)
    local listBack = ES.Solid(presetPane, "BACKGROUND", ES.INK, 0.8)
    listBack:SetPoint("TOPLEFT", listHead, "BOTTOMLEFT", 0, -8)
    listBack:SetSize(SLIDER_W, PRESET_ROWS * 30 + 4)
    local empty = ES.Text(presetPane, 11, ES.MUTED)
    empty:SetPoint("CENTER", listBack, "CENTER")
    empty:SetText("Nothing saved for this race yet.\nName your look below.")
    local function disarm(row) row.delete.armed = nil; row.delete.label:SetText("Delete") end
    for r = 1, PRESET_ROWS do
        local row = CreateFrame("Button", "BodySliderPreset" .. r, presetPane)
        row:SetSize(SLIDER_W - 4, 28)
        row:SetPoint("TOPLEFT", listBack, "TOPLEFT", 2, -2 - (r - 1) * 30)
        row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0)
        row.wash:SetAllPoints()
        row.name = ES.Text(row, 12, ES.TEXT)
        row.name:SetPoint("LEFT", 10, 0)
        row.name:SetPoint("RIGHT", -70, 0)
        row.name:SetJustifyH("LEFT")
        row.delete = flatButton(row, 60, 20, "Delete")
        row.delete:SetPoint("RIGHT", -4, 0)
        row:SetScript("OnEnter", function(self) self.wash:SetAlpha(0.08) end)
        row:SetScript("OnLeave", function(self) self.wash:SetAlpha(0) end)
        row:SetScript("OnClick", function(self)
            local preset = presets[self.index]
            local values = preset and addon.Decode(preset.code)
            if not values then return end
            PlaySound("igMainMenuOptionCheckBoxOn")
            applyValues(values)
            nameBox:SetText(preset.name)
            say("Loaded " .. preset.name .. ".", true)
        end)
        -- Two clicks delete: the first arms the button, so a stray click never loses a preset.
        row.delete:SetScript("OnClick", function(self)
            local preset = presets[row.index]
            if not preset then return end
            if not self.armed then
                self.armed = true
                self.label:SetText("|cffff6a5cSure?|r")
                return
            end
            table.remove(presets, row.index)
            for _, other in ipairs(presetRows) do disarm(other) end
            say("Deleted " .. preset.name .. ".", true)
            refresh()
        end)
        presetRows[r] = row
    end
    local prev = flatButton(presetPane, 30, 22, "<")
    prev:SetPoint("TOPLEFT", listBack, "BOTTOMLEFT", 0, -6)
    prev:SetScript("OnClick", function() presetPage = presetPage - 1; refresh() end)
    local nextPage = flatButton(presetPane, 30, 22, ">")
    nextPage:SetPoint("TOPRIGHT", listBack, "BOTTOMRIGHT", 0, -6)
    nextPage:SetScript("OnClick", function() presetPage = presetPage + 1; refresh() end)
    local pageText = ES.Text(presetPane, 11, ES.MUTED)
    pageText:SetPoint("TOP", listBack, "BOTTOM", 0, -10)

    local saveHead = heading("SAVE CURRENT LOOK", prev, 16)
    nameBox = field(presetPane, "BodySliderPresetName", SLIDER_W - 136, MAX_NAME)
    nameBox:SetPoint("TOPLEFT", saveHead, "BOTTOMLEFT", 6, -8)
    local save = flatButton(presetPane, 120, 26, "Save", true)
    save:SetPoint("LEFT", nameBox, "RIGHT", 8, 0)
    local function savePreset()
        local name = strtrim(nameBox:GetText() or "")
        if name == "" then say("Name the preset first."); return end
        local code = addon.Encode(store.values)
        for index, preset in ipairs(presets) do
            if preset.name:lower() == name:lower() then
                presets[index] = { name = name, code = code }
                say("Updated " .. name .. ".", true)
                nameBox:ClearFocus()
                refresh()
                return
            end
        end
        if #presets >= MAX_PRESETS then say("This race already has " .. MAX_PRESETS .. " presets."); return end
        presets[#presets + 1] = { name = name, code = code }
        presetPage = math.ceil(#presets / PRESET_ROWS)
        say("Saved " .. name .. ".", true)
        nameBox:ClearFocus()
        refresh()
    end
    save:SetScript("OnClick", savePreset)
    nameBox:SetScript("OnEnterPressed", savePreset)
    status = ES.Text(presetPane, 11, ES.GOOD)
    status:SetPoint("TOPLEFT", nameBox, "BOTTOMLEFT", -6, -12)
    status:SetPoint("RIGHT", presetPane, "RIGHT", -20, 0)
    status:SetJustifyH("LEFT")

    -- -----------------------------------------------------------------------------------------------------------
    -- stage: live preview of your own model
    -- -----------------------------------------------------------------------------------------------------------
    stage = CreateFrame("Frame", nil, panel)
    stage:SetPoint("TOPLEFT", pane, "TOPRIGHT", 14, 0)
    stage:SetPoint("BOTTOMRIGHT", -MARGIN, 16)
    ES.Solid(stage, "BACKGROUND", { 0.05, 0.05, 0.07 }, 1):SetAllPoints()
    local glow = stage:CreateTexture(nil, "BORDER")
    glow:SetTexture(ES.WHITE)
    glow:SetGradientAlpha("VERTICAL", 0.62, 0.46, 0.15, 0.3, 0.62, 0.46, 0.15, 0)
    glow:SetPoint("BOTTOMLEFT", 0, 120); glow:SetPoint("BOTTOMRIGHT", 0, 120); glow:SetHeight(170)
    ES.Border(stage, ES.GOLD, 0.8)
    preview = CreateFrame("PlayerModel", "BodySliderPreview", stage)
    preview:SetPoint("TOPLEFT", 4, -4)
    preview:SetPoint("BOTTOMRIGHT", -4, 120)
    preview.facing, preview.zoom, preview.zoomTarget = -0.4, 0, 0
    preview:EnableMouse(true)
    preview:EnableMouseWheel(true)
    preview:SetScript("OnMouseDown", function(self) self.dragX = GetCursorPosition() end)
    preview:SetScript("OnMouseUp", function(self) self.dragX = nil end)
    preview:SetScript("OnMouseWheel", function(self, delta)
        self.zoomTarget = math.max(0, math.min(2.5, self.zoomTarget + delta * 0.25))
    end)
    preview:SetScript("OnUpdate", function(self, elapsed)
        if self.dragX then
            local x = GetCursorPosition()
            self.facing = self.facing + (x - self.dragX) * 0.012
            self.dragX = x
        end
        self:SetFacing(self.facing)
        if self.zoom ~= self.zoomTarget then
            local step = elapsed * 4
            self.zoom = math.abs(self.zoomTarget - self.zoom) <= step and self.zoomTarget
                or self.zoom + (self.zoomTarget > self.zoom and step or -step)
            -- shortcut: moving toward the camera and down frames the head for most races; a per-race offset
            -- would be exact but needs in-game calibration per model.
            self:SetPosition(self.zoom, 0, -self.zoom * 0.35)
        end
        BodySlider_Set(self) -- BodySlider.dll mirrors your values onto this model; the model may load late.
    end)
    preview:RegisterEvent("UNIT_MODEL_CHANGED")
    preview:SetScript("OnEvent", function(self, _, unit) if unit == "player" then self:SetUnit("player") end end)
    local hint = ES.Text(stage, 9, ES.MUTED)
    hint:SetPoint("BOTTOM", preview, "BOTTOM", 0, 6)
    hint:SetText("Drag to rotate  ·  Scroll to zoom")

    local info = CreateFrame("Frame", nil, stage)
    info:SetPoint("BOTTOMLEFT", 1, 1); info:SetPoint("BOTTOMRIGHT", -1, 1); info:SetHeight(118)
    ES.Solid(info, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    local rule = ES.Solid(info, "ARTWORK", ES.GOLD_DIM, 0.8)
    rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    local who = ES.Text(info, 10, ES.GOLD_DIM)
    who:SetPoint("TOPLEFT", 16, -12)
    who:SetText(((raceName or "") .. " " .. sexName):upper())
    info.name = ES.Text(info, 17, ES.GOLD)
    info.name:SetPoint("TOPLEFT", who, "BOTTOMLEFT", 0, -5)
    info.name:SetText(UnitName("player"))
    info.state = ES.Text(info, 11, ES.MUTED)
    info.state:SetPoint("TOPLEFT", info.name, "BOTTOMLEFT", 0, -5)
    info.state:SetPoint("RIGHT", -16, 0)
    info.state:SetJustifyH("LEFT")
    local reset = flatButton(info, 120, 28, "Reset All")
    reset:SetPoint("BOTTOMRIGHT", -14, 12)
    reset:SetScript("OnClick", function()
        BodySlider_Clear(guid)
        applyValues({})
        addon.Publish()
    end)
    local revert = flatButton(info, 120, 28, "Revert")
    revert:SetPoint("RIGHT", reset, "LEFT", -8, 0)
    revert:SetScript("OnClick", function() if snapshot then applyValues(snapshot) end end)

    -- -----------------------------------------------------------------------------------------------------------
    -- painting
    -- -----------------------------------------------------------------------------------------------------------
    local function enabled(name)
        return available[name] and (type(BodySlider_HasSlider) ~= "function" or BodySlider_HasSlider(name))
    end

    refresh = function()
        local page = PAGES[selected]
        for i, row in ipairs(railRows) do
            local on, p = i == selected, PAGES[i]
            row.wash:SetAlpha(on and 0.12 or 0)
            row.accent:SetAlpha(on and 1 or 0)
            row.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
            if p.presets then
                row.sub:SetText(#presets .. " saved for " .. sexName:lower() .. " " .. (raceName or ""):lower())
                shown(row.dot, false)
            else
                local changed = changedCount(p.first, p.last)
                row.sub:SetText(changed > 0 and ("|cff8cd97f" .. changed .. " changed|r")
                    or ((p.last - p.first + 1) .. " controls"))
                shown(row.dot, changed > 0)
            end
        end
        shown(pane, not page.presets)
        shown(presetPane, page.presets)
        local supported = 0
        for i, name in ipairs(addon.names) do
            local slider = sliders[name]
            local on = enabled(name)
            slider.row.enabled = on and true or false
            if on then slider:Enable() else slider:Disable() end
            slider.row:SetAlpha(on and 1 or 0.4)
            slider.paint(store.values[name])
            local visible = not page.presets and i >= page.first and i <= page.last
            shown(slider.row, visible)
            if visible and on then supported = supported + 1 end
        end
        if page.presets then
            listHead:SetText(("SAVED FOR %s %s"):format((raceName or ""):upper(), sexName:upper()))
            local pages = math.max(1, math.ceil(#presets / PRESET_ROWS))
            presetPage = math.min(math.max(presetPage, 1), pages)
            pageText:SetText(("%d / %d"):format(presetPage, pages))
            prev:SetActive(presetPage > 1)
            nextPage:SetActive(presetPage < pages)
            shown(empty, #presets == 0)
            for r, row in ipairs(presetRows) do
                local index = (presetPage - 1) * PRESET_ROWS + r
                if row.index ~= index then disarm(row) end
                row.index = index
                shown(row, presets[index] ~= nil)
                if presets[index] then row.name:SetText(presets[index].name) end
            end
            if not codeBox:HasFocus() then codeBox:SetText(addon.Encode(store.values)) end
        else
            pane.title:SetText(page.title)
            if supported == 0 then
                note:SetText("Unsupported model. Controls are unavailable.")
            else
                note:SetText(supported .. " of " .. (page.last - page.first + 1)
                    .. " controls available for this model.  ·  " .. addon.Status())
            end
        end
        local changed = changedCount()
        info.state:SetText(changed > 0 and (changed .. " of " .. #addon.names .. " controls changed")
            or "Stock shape")
        revert:SetActive(snapshot ~= nil and addon.Encode(snapshot) ~= addon.Encode(store.values))
        reset:SetActive(changed > 0)
    end

    local supportElapsed = 0
    panel:SetScript("OnShow", function(self)
        PlaySound("igCharacterInfoOpen")
        snapshot = copyValues()
        preview:SetUnit("player")
        preview.zoom = -1 -- forces the first position update
        status:SetText("")
        refresh()
        ES.Tween("bodyslider-open", 0.22, 0, function(p) self:SetAlpha(p) end)
    end)
    panel:SetScript("OnHide", function() PlaySound("igCharacterInfoClose") end)
    panel:SetScript("OnUpdate", function(_, delta)
        supportElapsed = supportElapsed + delta
        if supportElapsed >= 0.5 then supportElapsed = 0; refresh() end
    end)
    refresh()

    SLASH_BODYSLIDER1 = "/bodyslider"
    SlashCmdList.BODYSLIDER = function(text)
        local command = (text or ""):lower():match("^%s*(%S*)")
        if command == "channel" or command == "group" or command == "whisper" then
            addon.SetTransport(command)
            DEFAULT_CHAT_FRAME:AddMessage("Body Slider: " .. addon.Status())
            refresh()
        elseif command == "status" then
            DEFAULT_CHAT_FRAME:AddMessage("Body Slider: " .. addon.Status())
        elseif command == "" then
            shown(panel, not panel:IsShown())
        else
            DEFAULT_CHAT_FRAME:AddMessage("Body Slider: /bodyslider [channel | group | whisper | status]")
        end
    end
    addon.Start(store)
end)
