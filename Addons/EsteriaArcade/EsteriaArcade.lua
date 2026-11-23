-- Esteria Arcade: the cabinet's screen. The client's Arcade extension runs the game (Arcade_* functions); this window
-- shows its picture, takes the keyboard while a game runs (Esc quits, which saves) and hides when the game ends.
-- Options (saved per account in EsteriaArcadeDB) change the picture, the window, sound and the keys.
if not Arcade_GetState then return end

local ACTIONS = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "START", "SELECT" }
local ACTION_NAMES = { UP = "Up", DOWN = "Down", LEFT = "Left", RIGHT = "Right", A = "A", B = "B", X = "X", Y = "Y",
    L = "L", R = "R", START = "Start", SELECT = "Select", FAST = "Fast-forward (hold)" }
-- RetroArch's default keyboard layout. A gamepad works too (the extension reads XInput pad 1 directly).
local DEFAULT_KEYS = { UP = "UP", DOWN = "DOWN", LEFT = "LEFT", RIGHT = "RIGHT", A = "X", B = "Z", X = "S", Y = "A",
    L = "Q", R = "W", START = "ENTER", SELECT = "SPACE", FAST = "TAB" }
local ASPECTS = { "core", "4:3", "pixel", "stretch" }
local ASPECT_NAMES = { core = "Original", ["4:3"] = "4:3 TV", pixel = "Pixel perfect", stretch = "Widescreen" }
local PALETTES = { "Normal", "Grayscale", "Green screen", "Sepia", "Amber", "Inverted" }
local DEFAULTS = {
    size = 448, aspect = "core", smooth = false, scanlines = 0, brightness = 1, saturation = 1, palette = 0,
    cabinet = true, dim = 0.35, volume = 0.8, mute = false, opacity = 0.96, help = true, lock = false,
    pauseInOptions = true, fastSpeed = 3,
}

local db, keyAction  -- keyAction: pressed key -> action, rebuilt from db.keys

local function Rebind()
    keyAction = {}
    for action, key in pairs(db.keys) do keyAction[key] = action end
end

local function LoadSettings()
    EsteriaArcadeDB = EsteriaArcadeDB or {}
    db = EsteriaArcadeDB
    for k, v in pairs(DEFAULTS) do if db[k] == nil then db[k] = v end end
    db.keys = db.keys or {}
    for action, key in pairs(DEFAULT_KEYS) do if db.keys[action] == nil then db.keys[action] = key end end
    Rebind()
end

-- ---- the window -------------------------------------------------------------------------------------------------
local frame = CreateFrame("Frame", "EsteriaArcadeFrame", UIParent)
frame:SetFrameStrata("DIALOG")
frame:SetToplevel(true)
frame:SetPoint("CENTER", 0, 30)
frame:SetSize(640, 544)
local BACKDROP = {
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Gold-Border",
    tile = true, tileSize = 16, edgeSize = 32,
    insets = { left = 11, right = 11, top = 11, bottom = 11 },
}
frame:SetBackdrop(BACKDROP)
frame:EnableMouse(true)
frame:SetMovable(true)
frame:SetClampedToScreen(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", function(self) if not db.lock then self:StartMoving() end end)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:Hide()

-- Dims the world behind the window while a game runs.
local dimmer = CreateFrame("Frame", nil, UIParent)
dimmer:SetFrameStrata("HIGH")
dimmer:SetAllPoints(UIParent)
local dimTexture = dimmer:CreateTexture(nil, "BACKGROUND")
dimTexture:SetAllPoints()
dimmer:Hide()

local title = frame:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 22, -20)

local bezel = frame:CreateTexture(nil, "BACKGROUND")
bezel:SetTexture(0, 0, 0, 1)

local screen = frame:CreateTexture(nil, "ARTWORK")
screen:SetPoint("TOP", 0, -48)
Arcade_SetScreen(screen)
bezel:SetPoint("TOPLEFT", screen, -4, 4)
bezel:SetPoint("BOTTOMRIGHT", screen, 4, -4)

local status = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlightLarge")
status:SetPoint("CENTER", screen)

local help = frame:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
help:SetPoint("BOTTOM", 0, 18)

local function Button(text, width, onClick, parent)
    local b = CreateFrame("Button", nil, parent or frame, "UIPanelButtonTemplate")
    b:SetSize(width, 22)
    b:SetText(text)
    b:SetScript("OnClick", onClick)
    return b
end

local options  -- the options panel, created below
local quit = Button("Quit", 80, function() Arcade_Quit() end)
quit:SetPoint("TOPRIGHT", -16, -16)
local optionsButton = Button("Options", 80, function() if options:IsShown() then options:Hide() else options:Show() end end)
optionsButton:SetPoint("RIGHT", quit, "LEFT", -6, 0)
local pause = Button("Pause", 80, nil)
pause:SetPoint("RIGHT", optionsButton, "LEFT", -6, 0)

local userPaused = false
local function ApplyPause()
    local paused = userPaused or (options:IsShown() and db.pauseInOptions)
    Arcade_SetPaused(paused)
    if paused then Arcade_ReleaseButtons(); Arcade_SetFastForward(false) end
    pause:SetText(userPaused and "Resume" or "Pause")
    -- Paused by the button: the keyboard goes back to the game world. The options panel keeps it (for rebinding).
    frame:EnableKeyboard(frame:IsShown() and (not userPaused or options:IsShown()))
end
pause:SetScript("OnClick", function() userPaused = not userPaused; ApplyPause() end)

local function KeyText(key)
    return key and (_G["KEY_" .. key] or key) or "-"
end

local function HelpText()
    local k = db.keys
    return string.format("%s/%s/%s/%s move  ·  %s/%s  B/A  ·  %s/%s  Y/X  ·  %s/%s  L/R  ·  %s Start  ·  %s Select  ·  hold %s fast  ·  Esc quits",
        KeyText(k.UP), KeyText(k.DOWN), KeyText(k.LEFT), KeyText(k.RIGHT), KeyText(k.B), KeyText(k.A), KeyText(k.Y),
        KeyText(k.X), KeyText(k.L), KeyText(k.R), KeyText(k.START), KeyText(k.SELECT), KeyText(k.FAST))
end

-- Size of the picture for the current game and settings.
local function PictureSize(w, h, aspect)
    local H = db.size
    if db.aspect == "pixel" then
        local n = math.max(1, math.floor(H / h + 0.001))
        return w * n, h * n
    elseif db.aspect == "4:3" then
        return H * 4 / 3, H
    elseif db.aspect == "stretch" then
        return H * 16 / 9, H
    end
    return H * aspect, H
end

local layoutKey
local function Layout(force)
    local _, _, w, h, u, v, aspect = Arcade_GetState()
    local key = table.concat({ w, h, u, v, aspect, db.size, db.aspect, tostring(db.help) }, ":")
    if key == layoutKey and not force then return end
    layoutKey = key
    screen:SetTexCoord(0, u, 0, v)
    local pw, ph = PictureSize(w, h, aspect)
    screen:SetSize(pw, ph)
    frame:SetSize(math.max(pw + 48, 420), ph + (db.help and 96 or 76))
end

local function ApplyLook()
    frame:SetBackdropColor(0.05, 0.04, 0.03, db.opacity)
    frame:SetBackdropBorderColor(1, 1, 1, math.min(1, db.opacity + 0.2))
    help:SetText(HelpText())
    if db.help then help:Show() else help:Hide() end
    dimTexture:SetTexture(0, 0, 0, db.dim)
    if frame:IsShown() and db.dim > 0 then dimmer:Show() else dimmer:Hide() end
    Arcade_SetVolume(db.mute and 0 or db.volume)
    Layout(true)
end

-- Pushes every emulator-side option to the extension.
local function ApplyEmulator()
    Arcade_SetOption("smooth", db.smooth)
    Arcade_SetScreen(screen)  -- a smoothing change replaces the texture
    Arcade_SetOption("scanlines", db.scanlines)
    Arcade_SetOption("brightness", db.brightness)
    Arcade_SetOption("saturation", db.saturation)
    Arcade_SetOption("palette", db.palette)
    Arcade_SetOption("cabinet", db.cabinet)
    Arcade_SetOption("fastSpeed", db.fastSpeed)
end

local function ApplyAll()
    ApplyEmulator()
    ApplyLook()
end

-- ---- options panel ----------------------------------------------------------------------------------------------
options = CreateFrame("Frame", "EsteriaArcadeOptions", frame)
options:SetSize(500, 470)
options:SetPoint("TOPLEFT", frame, "TOPRIGHT", -6, 0)
options:SetBackdrop(BACKDROP)
options:SetBackdropColor(0.05, 0.04, 0.03, 0.97)
options:EnableMouse(true)
options:Hide()
options:SetScript("OnShow", ApplyPause)
options:SetScript("OnHide", function() options.waiting = nil; ApplyPause() end)

local optionsTitle = options:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
optionsTitle:SetPoint("TOPLEFT", 22, -20)
optionsTitle:SetText("Arcade Options")
Button("Close", 70, function() options:Hide() end, options):SetPoint("TOPRIGHT", -16, -16)

local refreshers = {}
local column = { left = { x = 22, y = -52 }, right = { x = 262, y = -52 } }
local nameCount = 0

local function Place(widget, side, height)
    local c = column[side]
    widget:SetPoint("TOPLEFT", options, "TOPLEFT", c.x, c.y)
    c.y = c.y - height
end

local function Header(side, text)
    local h = options:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    h:SetText(text)
    Place(h, side, 20)
end

local function Slider(side, label, key, min, max, step, format, apply)
    nameCount = nameCount + 1
    local name = "EsteriaArcadeSlider" .. nameCount
    local s = CreateFrame("Slider", name, options, "OptionsSliderTemplate")
    s:SetWidth(200)
    s:SetMinMaxValues(min, max)
    s:SetValueStep(step)
    _G[name .. "Low"]:SetText("")
    _G[name .. "High"]:SetText("")
    local text = _G[name .. "Text"]
    local percent = format:find("%%%%") ~= nil
    s:SetScript("OnValueChanged", function(self, value)
        value = math.floor(value / step + 0.5) * step
        db[key] = value
        text:SetText(label .. ": " .. string.format(format, math.floor(value * (percent and 100 or 1) + 0.5)))
        apply()
    end)
    refreshers[#refreshers + 1] = function() s:SetValue(db[key]) end
    column[side].y = column[side].y - 14  -- the template's label sits above the bar
    s:SetPoint("TOPLEFT", options, "TOPLEFT", column[side].x + 4, column[side].y)
    column[side].y = column[side].y - 26
end

local function Check(side, label, key, apply)
    nameCount = nameCount + 1
    local c = CreateFrame("CheckButton", "EsteriaArcadeCheck" .. nameCount, options, "UICheckButtonTemplate")
    c:SetSize(24, 24)
    _G[c:GetName() .. "Text"]:SetText(label)
    c:SetScript("OnClick", function(self) db[key] = self:GetChecked() and true or false; apply() end)
    refreshers[#refreshers + 1] = function() c:SetChecked(db[key]) end
    Place(c, side, 24)
end

local function Cycle(side, label, key, values, names, apply)
    local b = Button("", 200, nil, options)
    local function show()
        local v = db[key]
        b:SetText(label .. ": " .. ((type(v) == "number" and names[v + 1] or names[v]) or tostring(v)))
    end
    b:SetScript("OnClick", function()
        local index = 1
        for i, v in ipairs(values) do if v == db[key] then index = i end end
        db[key] = values[index % #values + 1]
        show()
        apply()
    end)
    refreshers[#refreshers + 1] = show
    Place(b, side, 28)
end

Header("left", "Picture")
Slider("left", "Size", "size", 224, 900, 8, "%d", ApplyLook)
Cycle("left", "Shape", "aspect", ASPECTS, ASPECT_NAMES, ApplyLook)
Check("left", "Smooth pixels", "smooth", ApplyEmulator)
Slider("left", "Scanlines", "scanlines", 0, 0.9, 0.05, "%d%%", ApplyEmulator)
Slider("left", "Brightness", "brightness", 0.5, 1.5, 0.05, "%d%%", ApplyEmulator)
Slider("left", "Colour", "saturation", 0, 2, 0.05, "%d%%", ApplyEmulator)
Cycle("left", "Colours", "palette", { 0, 1, 2, 3, 4, 5 }, PALETTES, ApplyEmulator)
Header("left", "World")
Check("left", "Show the game on the cabinet", "cabinet", ApplyEmulator)
Slider("left", "Dim the world", "dim", 0, 0.9, 0.05, "%d%%", ApplyLook)

Header("right", "Sound and window")
Slider("right", "Volume", "volume", 0, 1, 0.05, "%d%%", ApplyLook)
Check("right", "Mute", "mute", ApplyLook)
Slider("right", "Window opacity", "opacity", 0.2, 1, 0.05, "%d%%", ApplyLook)
Check("right", "Show the key help", "help", ApplyLook)
Check("right", "Lock the window in place", "lock", ApplyLook)
Check("right", "Pause while options are open", "pauseInOptions", ApplyPause)
Slider("right", "Fast-forward speed", "fastSpeed", 2, 8, 1, "%dx", ApplyEmulator)

-- Controls: click a key, then press the new one (Esc cancels).
Header("right", "Controls")
local bindButtons = {}
local function RefreshBinds()
    for action, b in pairs(bindButtons) do
        b:SetText(options.waiting == action and "Press a key..." or (ACTION_NAMES[action] .. ": " .. KeyText(db.keys[action])))
    end
end
local bindOrder = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "START", "SELECT", "FAST" }
for i, action in ipairs(bindOrder) do
    local b = Button("", action == "FAST" and 216 or 106, function() options.waiting = action; RefreshBinds() end, options)
    local c = column.right
    local row, col = math.floor((i - 1) / 2), (i - 1) % 2
    b:SetPoint("TOPLEFT", options, "TOPLEFT", c.x + col * 110, c.y - row * 24)
    bindButtons[action] = b
end
column.right.y = column.right.y - 7 * 24
refreshers[#refreshers + 1] = RefreshBinds

local function Refresh()
    for _, refresh in ipairs(refreshers) do refresh() end
end

local resetPosition = Button("Reset position", 120, function()
    frame:ClearAllPoints()
    frame:SetPoint("CENTER", 0, 30)
end, options)
resetPosition:SetPoint("BOTTOMLEFT", 20, 16)
local defaults = Button("Defaults", 100, function()
    for k, v in pairs(DEFAULTS) do db[k] = v end
    db.keys = {}
    for action, key in pairs(DEFAULT_KEYS) do db.keys[action] = key end
    Rebind()
    Refresh()
    ApplyAll()
end, options)
defaults:SetPoint("LEFT", resetPosition, "RIGHT", 6, 0)

local function Bind(action, key)
    for other, bound in pairs(db.keys) do if bound == key then db.keys[other] = nil end end  -- one action per key
    db.keys[action] = key
    Rebind()
    help:SetText(HelpText())
end

-- ---- keys -------------------------------------------------------------------------------------------------------
frame:SetScript("OnKeyDown", function(self, key)
    if options.waiting then
        if key ~= "ESCAPE" then Bind(options.waiting, key) end
        options.waiting = nil
        RefreshBinds()
    elseif key == "ESCAPE" then
        if options:IsShown() then options:Hide() else Arcade_Quit() end
    elseif keyAction[key] == "FAST" then
        Arcade_SetFastForward(true)
    elseif keyAction[key] then
        Arcade_SetButton(keyAction[key], true)
    end
end)
frame:SetScript("OnKeyUp", function(self, key)
    local action = keyAction and keyAction[key]
    if action == "FAST" then Arcade_SetFastForward(false)
    elseif action then Arcade_SetButton(action, false) end
end)

local loading
frame:SetScript("OnUpdate", function()
    local phase = Arcade_GetState()
    if phase == "loading" then
        if not loading then
            loading = true
            status:SetText("Loading...")
            screen:SetTexture(0, 0, 0, 1)
        end
        return
    end
    if phase ~= "running" then return end
    if loading then
        loading = false
        status:SetText("")
        Arcade_SetScreen(screen)
    end
    Layout()
end)
frame:SetScript("OnShow", function() ApplyLook() end)
frame:SetScript("OnHide", function()
    Arcade_ReleaseButtons()
    Arcade_SetFastForward(false)
    options:Hide()
    dimmer:Hide()
end)

frame:RegisterEvent("VARIABLES_LOADED")
frame:RegisterEvent("ARCADE_LOADING")
frame:RegisterEvent("ARCADE_STARTED")
frame:RegisterEvent("ARCADE_STOPPED")
frame:SetScript("OnEvent", function(self, event, arg1)
    if event == "VARIABLES_LOADED" then
        LoadSettings()
        Refresh()
        ApplyEmulator()
    elseif event == "ARCADE_LOADING" or event == "ARCADE_STARTED" then
        if not db then LoadSettings() end
        title:SetText(arg1)
        if event == "ARCADE_LOADING" then
            layoutKey = nil
            userPaused = false
            ApplyEmulator()
        end
        frame:Show()
        ApplyPause()
    elseif event == "ARCADE_STOPPED" then
        frame:EnableKeyboard(false)
        frame:Hide()
        if arg1 == "walked away" then
            UIErrorsFrame:AddMessage("You walked away from the cabinet. Your game was saved.", 1, 0.82, 0)
        elseif arg1 == "error" then
            UIErrorsFrame:AddMessage("The cabinet flickers and goes dark.", 1, 0.1, 0.1)
        end
    end
end)
