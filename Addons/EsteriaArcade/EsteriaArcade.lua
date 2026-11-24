-- Esteria Arcade: the cabinet's screen. The client's Arcade extension runs the game (Arcade_* functions); this window
-- shows its picture, takes the keyboard while a game runs (Esc closes Options, else quits, which saves) and hides when
-- the game ends. Options (saved per account in EsteriaArcadeDB) change the picture, window, sound, look and keys.
-- Looks: the Esteria style kit with Esteria Inventory's styles and accent colour; every surface is painted from the
-- active style's roles (bg, panel, edge, dim, text, muted, primary), so a change repaints both windows at once.
if not Arcade_GetState then return end

local SYSTEMS = { fceumm = "Nintendo Entertainment System", snes9x = "Super Nintendo", genesis_plus_gx = "Sega Genesis",
    gambatte = "Game Boy", mgba = "Game Boy Advance", parallel_n64 = "Nintendo 64", pcsx_rearmed = "PlayStation",
    fbneo = "Arcade", dosbox_pure = "MS-DOS", starcraft = "PC" }
local dos  -- a computer game (DOSBox Pure, the StarCraft bridge): the keyboard and the mouse over the picture go straight to it
local ACTION_NAMES = { UP = "Up", DOWN = "Down", LEFT = "Left", RIGHT = "Right", A = "A", B = "B", X = "X", Y = "Y",
    L = "L", R = "R", START = "Start", SELECT = "Select", FAST = "Fast-forward", L2 = "L2 / Z", R2 = "R2", L3 = "L3",
    R3 = "R3", LSTICK_UP = "Stick up", LSTICK_DOWN = "Stick down", LSTICK_LEFT = "Stick left", LSTICK_RIGHT = "Stick right",
    RSTICK_UP = "C up", RSTICK_DOWN = "C down", RSTICK_LEFT = "C left", RSTICK_RIGHT = "C right" }
-- RetroArch's default keyboard layout. A gamepad works too (the extension reads XInput pad 1 directly).
-- On N64 the arrow keys steer the stick; the right stick (C buttons) defaults to I/J/K/L.
local DEFAULT_KEYS = { UP = "UP", DOWN = "DOWN", LEFT = "LEFT", RIGHT = "RIGHT", A = "X", B = "Z", X = "S", Y = "A",
    L = "Q", R = "W", START = "ENTER", SELECT = "SPACE", FAST = "TAB", L2 = "E", R2 = "R", L3 = "C", R3 = "V",
    RSTICK_UP = "I", RSTICK_DOWN = "K", RSTICK_LEFT = "J", RSTICK_RIGHT = "L" }
local BIND_ORDER = { "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L", "R", "L2", "R2", "L3", "R3", "START",
    "SELECT", "LSTICK_UP", "LSTICK_DOWN", "LSTICK_LEFT", "LSTICK_RIGHT", "RSTICK_UP", "RSTICK_DOWN", "RSTICK_LEFT",
    "RSTICK_RIGHT", "FAST" }
local SHORT_KEYS = { UP = "Up", DOWN = "Down", LEFT = "Left", RIGHT = "Right", SPACE = "Space", ENTER = "Enter",
    TAB = "Tab", ESCAPE = "Esc", BACKSPACE = "Bksp", LSHIFT = "LShift", RSHIFT = "RShift", LCTRL = "LCtrl",
    RCTRL = "RCtrl", LALT = "LAlt", RALT = "RAlt" }
local SHAPES, SHAPE_NAMES = { "core", "4:3", "pixel", "stretch" },
    { core = "Original", ["4:3"] = "4:3 TV", pixel = "Pixel perfect", stretch = "Widescreen" }
local PALETTES = { "Normal", "Grayscale", "Green screen", "Sepia", "Amber", "Inverted" }
-- Esteria Inventory's styles (EsteriaInventory.lua STYLES), so the arcade can match the rest of the UI.
local STYLES = {
    { key = "esteria", name = "Esteria", bg = { 0.03, 0.04, 0.055 }, panel = { 0.06, 0.07, 0.10 }, edge = { 0.91, 0.77, 0.42 },
      dim = { 0.54, 0.43, 0.18 }, text = { 0.88, 0.86, 0.80 }, muted = { 0.60, 0.59, 0.55 }, primary = { 0.36, 0.25, 0.06 } },
    { key = "midnight", name = "Midnight", bg = { 0.02, 0.03, 0.07 }, panel = { 0.05, 0.07, 0.13 }, edge = { 0.50, 0.70, 1.00 },
      dim = { 0.22, 0.32, 0.55 }, text = { 0.85, 0.89, 0.96 }, muted = { 0.55, 0.60, 0.70 }, primary = { 0.10, 0.20, 0.42 } },
    { key = "verdant", name = "Verdant", bg = { 0.03, 0.05, 0.03 }, panel = { 0.06, 0.09, 0.06 }, edge = { 0.55, 0.85, 0.45 },
      dim = { 0.26, 0.42, 0.20 }, text = { 0.86, 0.90, 0.82 }, muted = { 0.56, 0.62, 0.52 }, primary = { 0.16, 0.30, 0.10 } },
    { key = "crimson", name = "Crimson", bg = { 0.06, 0.02, 0.02 }, panel = { 0.10, 0.04, 0.04 }, edge = { 0.95, 0.35, 0.28 },
      dim = { 0.50, 0.16, 0.12 }, text = { 0.93, 0.85, 0.82 }, muted = { 0.66, 0.55, 0.52 }, primary = { 0.40, 0.08, 0.06 } },
    { key = "frost", name = "Frost", bg = { 0.05, 0.07, 0.09 }, panel = { 0.09, 0.12, 0.15 }, edge = { 0.70, 0.90, 0.98 },
      dim = { 0.32, 0.45, 0.52 }, text = { 0.90, 0.95, 0.98 }, muted = { 0.58, 0.66, 0.72 }, primary = { 0.16, 0.30, 0.38 } },
    { key = "minimal", name = "Minimal", bg = { 0.05, 0.05, 0.05 }, panel = { 0.09, 0.09, 0.09 }, edge = { 0.42, 0.42, 0.42 },
      dim = { 0.22, 0.22, 0.22 }, text = { 0.90, 0.90, 0.90 }, muted = { 0.55, 0.55, 0.55 }, primary = { 0.24, 0.24, 0.24 } },
    { key = "classic", name = "Classic", bg = { 0.04, 0.03, 0.02 }, panel = { 0.08, 0.06, 0.04 }, edge = { 1.00, 0.82, 0.00 },
      dim = { 0.45, 0.35, 0.10 }, text = { 1.00, 0.95, 0.85 }, muted = { 0.65, 0.60, 0.50 }, primary = { 0.50, 0.06, 0.03 } },
}
local DEFAULTS = {
    size = 0.62, aspect = "core", smooth = false, scanlines = 0, brightness = 1, saturation = 1, palette = 0,
    cabinet = true, dim = 0.35, volume = 0.8, mute = false, opacity = 0.97, help = true, lock = false,
    pauseInOptions = true, fastSpeed = 3, style = "esteria", accent = false, titleSize = 20, frameLine = true,
    optionsScale = 1, optionsW = 440, optionsH = 470,
}
local WHITE, FONT = "Interface\\Buttons\\WHITE8X8", "Fonts\\FRIZQT__.TTF"
local HEADER, PAD, FOOTER, RAIL = 56, 14, 28, 112

local db, keyAction  -- keyAction: pressed key -> action, rebuilt from db.keys

local function Rebind()
    keyAction = {}
    for action, key in pairs(db.keys) do keyAction[key] = action end
end

local function LoadSettings()
    EsteriaArcadeDB = EsteriaArcadeDB or {}
    db = EsteriaArcadeDB
    if db.size and db.size > 1 then db.size = nil end  -- pixels before 2026-11-23; now a share of the screen height
    for k, v in pairs(DEFAULTS) do if db[k] == nil then db[k] = v end end
    db.keys = db.keys or {}
    for action, key in pairs(DEFAULT_KEYS) do if db.keys[action] == nil then db.keys[action] = key end end
    Rebind()
end

local function KeyText(key) return key and (SHORT_KEYS[key] or key) or "-" end
local function shown(region, on) if on then region:Show() else region:Hide() end end

-- ---- painting ---------------------------------------------------------------------------------------------------
local function Style()
    local chosen = STYLES[1]
    for _, s in ipairs(STYLES) do if db and s.key == db.style then chosen = s end end
    if not (db and db.accent) then return chosen end
    local copy = {}
    for k, v in pairs(chosen) do copy[k] = v end
    local a = db.accent
    copy.edge, copy.dim = { a[1], a[2], a[3] }, { a[1] * 0.55, a[2] * 0.55, a[3] * 0.55 }
    return copy
end

local painted = {}  -- { object, role, alpha, kind }: kind "tex", "text" or "wash"
local function Paint(object, role, alpha, kind)
    painted[#painted + 1] = { object, role, alpha or 1, kind or "tex" }
    return object
end

local function PaintAll()
    local st = Style()
    for _, p in ipairs(painted) do
        local c = st[p[2]]
        if p[4] == "text" then p[1]:SetTextColor(c[1], c[2], c[3])
        elseif p[4] == "wash" then p[1]:SetGradientAlpha("VERTICAL", c[1] * 0.6, c[2] * 0.6, c[3] * 0.6, 0, c[1] * 0.6, c[2] * 0.6, c[3] * 0.6, p[3])
        else p[1]:SetVertexColor(c[1], c[2], c[3], p[3]) end
    end
end

local function Solid(parent, layer, role, alpha)
    local t = parent:CreateTexture(nil, layer or "ARTWORK")
    t:SetTexture(WHITE)
    return Paint(t, role, alpha)
end

local function Text(parent, size, role, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GameFontHighlightSmall")
    fs:SetFont(FONT, size)
    fs:SetShadowOffset(1, -1)
    fs:SetShadowColor(0, 0, 0, 0.8)
    return Paint(fs, role, 1, "text")
end

-- A 1px line per side; returns them for hover tints.
local function Lines(frame, role, alpha)
    local lines = {}
    for _, side in ipairs({ { "TOPLEFT", "TOPRIGHT", true }, { "BOTTOMLEFT", "BOTTOMRIGHT", true },
        { "TOPLEFT", "BOTTOMLEFT" }, { "TOPRIGHT", "BOTTOMRIGHT" } }) do
        local line = Solid(frame, "BORDER", role, alpha)
        line:SetPoint(side[1], frame, side[1])
        line:SetPoint(side[2], frame, side[2])
        if side[3] then line:SetHeight(1) else line:SetWidth(1) end
        lines[#lines + 1] = line
    end
    return lines
end

local function TintLines(lines, role)
    local c = Style()[role]
    for _, line in ipairs(lines) do line:SetVertexColor(c[1], c[2], c[3], 1) end
end

local function FlatButton(parent, width, height, text, primary)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height)
    local bg = Solid(b, "BACKGROUND", primary and "primary" or "bg", primary and 0.95 or 0.9)
    bg:SetAllPoints()
    b.glow = Solid(b, "ARTWORK", "edge", 0)
    b.glow:SetAllPoints()
    b.lines = Lines(b, primary and "edge" or "dim")
    b.label = Text(b, 12, "text")
    b.label:SetPoint("LEFT", 4, 1)
    b.label:SetPoint("RIGHT", -4, 1)
    b.label:SetHeight(height - 4)  -- one line; longer text truncates instead of spilling out
    b.label:SetText(text)
    b:SetScript("OnEnter", function(self) self.glow:SetAlpha(0.14); TintLines(self.lines, "edge") end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0); TintLines(self.lines, primary and "edge" or "dim") end)
    b.SetLabel = function(self, t) self.label:SetText(t) end
    return b
end

-- The window that was clicked or opened last goes on top. Frame levels inside one strata did not keep Options above
-- a large game window (its children's levels win), so the front one takes the strata above the other.
local windows = {}
local function Front(f)
    for _, w in ipairs(windows) do w:SetFrameStrata(w == f and "FULLSCREEN_DIALOG" or "DIALOG") end
end

-- A movable window in the kit's look.
local function Window(name)
    local f = CreateFrame("Frame", name, UIParent)
    f:SetFrameStrata("DIALOG")
    f:SetToplevel(true)
    f:EnableMouse(true)
    f:SetMovable(true)
    f:SetClampedToScreen(true)
    f:RegisterForDrag("LeftButton")
    f.bg = Solid(f, "BACKGROUND", "bg", 0.97)
    f.bg:SetAllPoints()
    local wash = f:CreateTexture(nil, "BORDER")
    wash:SetTexture(WHITE)
    wash:SetPoint("TOPLEFT", 1, -1); wash:SetPoint("TOPRIGHT", -1, -1); wash:SetHeight(HEADER)
    Paint(wash, "edge", 0.36, "wash")
    f.lines = Lines(f, "edge")
    f.title = Text(f, 20, "edge")
    f.title:SetPoint("TOPLEFT", 18, -10)
    f.title:SetJustifyH("LEFT")
    f.subtitle = Text(f, 11, "muted")
    f.subtitle:SetPoint("TOPLEFT", f.title, "BOTTOMLEFT", 1, -3)
    f.subtitle:SetJustifyH("LEFT")
    f:SetScript("OnMouseDown", Front)
    f:Hide()
    windows[#windows + 1] = f
    return f
end

-- ---- the game window --------------------------------------------------------------------------------------------
local frame = Window("EsteriaArcadeFrame")
frame:SetPoint("CENTER", 0, 20)
frame:SetSize(700, 600)
frame:SetScript("OnDragStart", function(self) if not db.lock then self:StartMoving() end end)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)

local dimmer = CreateFrame("Frame", nil, UIParent)  -- dims the world behind the window while a game runs
dimmer:SetFrameStrata("HIGH")
dimmer:SetAllPoints(UIParent)
local dimTexture = dimmer:CreateTexture(nil, "BACKGROUND")
dimTexture:SetTexture(WHITE)
dimTexture:SetAllPoints()
dimmer:Hide()

local well = CreateFrame("Frame", nil, frame)  -- the screen well: black, a thin line, the picture centred
well:SetPoint("TOPLEFT", PAD, -(HEADER + 6))
well:SetPoint("BOTTOMRIGHT", -PAD, FOOTER)
local wellBg = well:CreateTexture(nil, "BACKGROUND")
wellBg:SetTexture(0, 0, 0, 1)
wellBg:SetAllPoints()
local wellLines = Lines(well, "dim")
local screen = well:CreateTexture(nil, "ARTWORK")
screen:SetPoint("CENTER")
Arcade_SetScreen(screen)
local status = Text(well, 16, "edge")
status:SetPoint("CENTER")

-- DOS games: the cursor over the picture is the game's mouse (shown only while one runs)
local pointer = CreateFrame("Frame", nil, well)
pointer:SetAllPoints(screen)
pointer:EnableMouse(true)
pointer:Hide()
pointer:SetScript("OnMouseDown", function(_, button)
    Front(frame)
    Arcade_SetMouseButton(button, true)
end)
pointer:SetScript("OnMouseUp", function(_, button) Arcade_SetMouseButton(button, false) end)
local cursorHidden  -- the player's cursor is off over the picture: the game draws its own
local function HideCursor(hide)
    if hide then Arcade_HideCursor(true) elseif cursorHidden then Arcade_HideCursor(false) end
    cursorHidden = hide
end
pointer:SetScript("OnHide", function()
    HideCursor(false)
    Arcade_SetMouseButton("LeftButton", false)
    Arcade_SetMouseButton("RightButton", false)
    Arcade_SetMouseButton("MiddleButton", false)
    Arcade_SetPointer(0, 0, false)
end)
pointer:SetScript("OnUpdate", function(self)
    local l, b, w, h = self:GetLeft(), self:GetBottom(), self:GetWidth(), self:GetHeight()
    if not l or w <= 0 or h <= 0 then return end
    local x, y = GetCursorPosition()
    local scale = self:GetEffectiveScale()
    local u, v = (x / scale - l) / w, 1 - (y / scale - b) / h
    -- topmost under the cursor: not while another window (Options) covers this part of the picture
    local inside = u >= 0 and u <= 1 and v >= 0 and v <= 1 and GetMouseFocus() == self
    Arcade_SetPointer(u, v, inside)
    HideCursor(inside)
end)

local help = Text(frame, 11, "muted")
help:SetPoint("BOTTOMLEFT", PAD, 8)
help:SetPoint("BOTTOMRIGHT", -PAD, 8)
help:SetHeight(14)  -- one line: truncates on a narrow window rather than running past the edges

local options  -- created below
local quit = FlatButton(frame, 70, 26, "Quit", true)
quit:SetPoint("TOPRIGHT", -14, -15)
quit:SetScript("OnClick", function() Arcade_Quit() end)
local optionsButton = FlatButton(frame, 76, 26, "Options")
optionsButton:SetPoint("RIGHT", quit, "LEFT", -6, 0)
optionsButton:SetScript("OnClick", function()
    if options:IsShown() then options:Hide() else options:Show(); Front(options) end
end)
-- Back to the default size and the middle of the screen, whatever the size slider was left at.
local resetSize = FlatButton(frame, 84, 26, "Reset size")
resetSize:SetPoint("RIGHT", optionsButton, "LEFT", -6, 0)
local pause = FlatButton(frame, 70, 26, "Pause")
pause:SetPoint("RIGHT", resetSize, "LEFT", -6, 0)
-- the title and subtitle stop short of the buttons (one line each; long names end in "...")
frame.title:SetPoint("RIGHT", pause, "LEFT", -12, 0)
frame.title:SetHeight(24)
frame.subtitle:SetPoint("RIGHT", pause, "LEFT", -12, 0)
frame.subtitle:SetHeight(14)

local userPaused = false
local function ApplyPause()
    local paused = userPaused or (options:IsShown() and db.pauseInOptions)
    Arcade_SetPaused(paused)
    if paused then Arcade_ReleaseButtons(); Arcade_SetFastForward(false) end
    pause:SetLabel(userPaused and "Resume" or "Pause")
    status:SetText(paused and not options:IsShown() and "Paused" or "")
    -- Paused by the button: the keyboard goes back to the game world. Options keeps it (for rebinding and Esc).
    frame:EnableKeyboard(frame:IsShown() and (not userPaused or options:IsShown()))
end
pause:SetScript("OnClick", function() userPaused = not userPaused; ApplyPause() end)

local function HelpText()
    if dos then return "Mouse and keyboard play the game  ·  Quit with the button above" end
    local k = db.keys
    local move = (k.UP == "UP" and k.DOWN == "DOWN" and k.LEFT == "LEFT" and k.RIGHT == "RIGHT") and "Arrows"
        or table.concat({ KeyText(k.UP), KeyText(k.DOWN), KeyText(k.LEFT), KeyText(k.RIGHT) }, "/")
    return string.format("%s move  ·  %s/%s B/A  ·  %s/%s Y/X  ·  %s Start  ·  %s Select  ·  %s fast  ·  Esc quits",
        move, KeyText(k.B), KeyText(k.A), KeyText(k.Y), KeyText(k.X), KeyText(k.START), KeyText(k.SELECT), KeyText(k.FAST))
end

-- The picture: db.size of the screen height, shaped by db.aspect, always small enough that the whole window fits.
local function PictureSize(w, h, aspect)
    local maxW = UIParent:GetWidth() * 0.96 - 2 * PAD
    local maxH = UIParent:GetHeight() * 0.94 - HEADER - 6 - FOOTER
    local H = math.min(UIParent:GetHeight() * db.size, maxH)
    local pw, ph
    if db.aspect == "pixel" then
        local n = math.max(1, math.floor(H / h + 0.001))
        pw, ph = w * n, h * n
    else
        local shape = db.aspect == "4:3" and 4 / 3 or db.aspect == "stretch" and 16 / 9 or aspect
        pw, ph = H * shape, H
    end
    local fit = math.min(1, maxW / pw, maxH / ph)
    return pw * fit, ph * fit
end

local layoutKey
local function Layout(force)
    local _, _, w, h, u, v, aspect = Arcade_GetState()
    local key = table.concat({ w, h, u, v, aspect, db.size, db.aspect, UIParent:GetWidth(), UIParent:GetHeight() }, ":")
    if key == layoutKey and not force then return end
    layoutKey = key
    screen:SetTexCoord(0, u, 0, v)
    local pw, ph = PictureSize(w, h, aspect)
    screen:SetSize(pw, ph)
    frame:SetSize(math.max(pw + 2 * PAD + 2, 520), ph + HEADER + 6 + FOOTER + 2)
end

local function ApplyLook()
    PaintAll()
    frame.bg:SetAlpha(db.opacity)
    frame.title:SetFont(FONT, db.titleSize)
    for _, line in ipairs(wellLines) do line:SetAlpha(db.frameLine and 1 or 0) end
    help:SetText(HelpText())
    shown(help, db.help)
    dimTexture:SetVertexColor(0, 0, 0, db.dim)
    shown(dimmer, frame:IsShown() and db.dim > 0)
    Arcade_SetVolume(db.mute and 0 or db.volume)
    if options then options:SetScale(db.optionsScale) end
    Layout(true)
end

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

-- ---- options: a rail of sections on the left (like Inventory's options), resizable from the corner --------------
options = Window("EsteriaArcadeOptions")
options:SetPoint("CENTER", 260, 0)
options:SetResizable(true)
options:SetMinResize(380, 420)
options:SetMaxResize(900, 800)
options:SetScript("OnDragStart", options.StartMoving)
options:SetScript("OnDragStop", options.StopMovingOrSizing)
options.title:SetText("Arcade Options")
options.title:SetFont(FONT, 18)
options.subtitle:SetText("Saved for your account")
options:SetScript("OnShow", function(self)
    self:SetSize(db.optionsW, db.optionsH)
    ApplyPause()
    if EsteriaStyle then EsteriaStyle.Tween("arcade-options", 0.18, 0, function(p) self:SetAlpha(p) end) end
end)
options:SetScript("OnHide", function() options.waiting = nil; ApplyPause() end)
local done = FlatButton(options, 70, 26, "Done", true)
done:SetPoint("TOPRIGHT", -14, -15)
done:SetScript("OnClick", function() options:Hide() end)

local railBg = Solid(options, "BACKGROUND", "panel", 0.9)
railBg:SetPoint("TOPLEFT", 1, -(HEADER + 1)); railBg:SetPoint("BOTTOMLEFT", 1, 1); railBg:SetWidth(RAIL)
local railRule = Solid(options, "BORDER", "dim", 0.5)
railRule:SetPoint("TOPLEFT", RAIL + 1, -(HEADER + 1)); railRule:SetPoint("BOTTOMLEFT", RAIL + 1, 1); railRule:SetWidth(1)
local headRule = Solid(options, "BORDER", "dim", 0.6)
headRule:SetPoint("TOPLEFT", 1, -HEADER); headRule:SetPoint("TOPRIGHT", -1, -HEADER); headRule:SetHeight(1)

-- Corner grip: drag to resize; the size is remembered.
local grip = CreateFrame("Button", nil, options)
grip:SetSize(14, 14)
grip:SetPoint("BOTTOMRIGHT", -2, 2)
for i = 1, 3 do
    local dot = Solid(grip, "OVERLAY", "dim", 1)
    dot:SetSize(2 * i + 2, 1)
    dot:SetPoint("BOTTOMRIGHT", 0, 3 * i - 2)
end
grip:SetScript("OnMouseDown", function() options:StartSizing("BOTTOMRIGHT") end)
grip:SetScript("OnMouseUp", function()
    options:StopMovingOrSizing()
    db.optionsW, db.optionsH = math.floor(options:GetWidth()), math.floor(options:GetHeight())
end)

local refreshers, pages, railButtons, current, pageCount = {}, {}, {}, nil, 0
local function Refresh() for _, refresh in ipairs(refreshers) do refresh() end end

local function ShowPage(name)
    current = name
    for key, page in pairs(pages) do shown(page, key == name) end
    for key, b in pairs(railButtons) do
        b.label:SetTextColor(unpack(Style()[key == name and "edge" or "text"]))
        b.mark:SetAlpha(key == name and 1 or 0)
    end
end

local function NewPage(name)
    local page = CreateFrame("Frame", nil, options)
    page:SetPoint("TOPLEFT", RAIL + 16, -(HEADER + 12))
    page:SetPoint("BOTTOMRIGHT", -16, 16)
    page.y = 0
    pages[name] = page
    pageCount = pageCount + 1
    local b = CreateFrame("Button", nil, options)
    b:SetSize(RAIL, 28)
    b:SetPoint("TOPLEFT", 1, -(HEADER + 8) - 30 * (pageCount - 1))
    b.mark = Solid(b, "ARTWORK", "edge", 1)
    b.mark:SetPoint("TOPLEFT", 0, -4); b.mark:SetPoint("BOTTOMLEFT", 0, 4); b.mark:SetWidth(2)
    b.label = Text(b, 12, "text")
    b.label:SetPoint("LEFT", 14, 0)
    b.label:SetText(name)
    b:SetScript("OnClick", function() ShowPage(name) end)
    railButtons[name] = b
    page:Hide()
    return page
end

-- Widgets stretch with the page (anchored to both sides), so the window can be any width.
local function Row(page, height)
    local row = CreateFrame("Frame", nil, page)
    row:SetPoint("TOPLEFT", 0, -page.y)
    row:SetPoint("TOPRIGHT", 0, -page.y)
    row:SetHeight(height)
    page.y = page.y + height
    return row
end

local function Section(page, text)
    if page.y > 0 then page.y = page.y + 6 end
    local row = Row(page, 22)
    local label = Text(row, 11, "edge")
    label:SetPoint("TOPLEFT")
    label:SetText(text:upper())
    local rule = Solid(row, "ARTWORK", "dim", 0.6)
    rule:SetPoint("TOPLEFT", 0, -15); rule:SetPoint("TOPRIGHT", 0, -15); rule:SetHeight(1)
end

local sliderCount = 0
local function Slider(page, label, key, min, max, step, show, apply)
    sliderCount = sliderCount + 1
    local row = Row(page, 36)
    local name = Text(row, 12, "text")
    name:SetPoint("TOPLEFT")
    name:SetText(label)
    local value = Text(row, 12, "edge")
    value:SetPoint("TOPRIGHT")
    local s = CreateFrame("Slider", "EsteriaArcadeSlider" .. sliderCount, row)
    s:SetOrientation("HORIZONTAL")
    s:SetHeight(14)
    s:SetPoint("TOPLEFT", 0, -16); s:SetPoint("TOPRIGHT", 0, -16)
    s:SetMinMaxValues(min, max)
    s:SetValueStep(step)
    s:EnableMouseWheel(true)
    local track = Solid(s, "BACKGROUND", "dim", 0.55)
    track:SetHeight(2)
    track:SetPoint("LEFT"); track:SetPoint("RIGHT")
    s:SetThumbTexture(WHITE)
    local thumb = Paint(s:GetThumbTexture(), "edge", 1)
    thumb:SetSize(8, 14)
    s:SetScript("OnValueChanged", function(self, v)
        v = math.floor(v / step + 0.5) * step
        db[key] = v
        value:SetText(show(v))
        apply()
    end)
    s:SetScript("OnMouseWheel", function(self, delta) self:SetValue(self:GetValue() + delta * step) end)
    refreshers[#refreshers + 1] = function() s:SetValue(db[key]) end
end

local function percent(v) return math.floor(v * 100 + 0.5) .. "%" end

local function Toggle(page, label, key, apply)
    local row = Row(page, 22)
    local b = CreateFrame("Button", nil, row)
    b:SetAllPoints()
    local box = CreateFrame("Frame", nil, b)
    box:SetSize(14, 14)
    box:SetPoint("LEFT")
    Solid(box, "BACKGROUND", "bg", 1):SetAllPoints()
    local lines = Lines(box, "dim")
    local fill = Solid(box, "ARTWORK", "edge", 1)
    fill:SetPoint("TOPLEFT", 3, -3); fill:SetPoint("BOTTOMRIGHT", -3, 3)
    local text = Text(b, 12, "text")
    text:SetPoint("LEFT", box, "RIGHT", 8, 0)
    text:SetPoint("RIGHT")
    text:SetJustifyH("LEFT")
    text:SetText(label)
    local function show() shown(fill, db[key]) end
    b:SetScript("OnClick", function() db[key] = not db[key]; show(); apply() end)
    b:SetScript("OnEnter", function() TintLines(lines, "edge") end)
    b:SetScript("OnLeave", function() TintLines(lines, "dim") end)
    refreshers[#refreshers + 1] = show
end

-- < value >: left click for the next choice, right click for the previous.
local function Choice(page, label, key, values, names, apply)
    local row = Row(page, 30)
    local text = Text(row, 12, "text")
    text:SetPoint("LEFT")
    text:SetText(label)
    local b = FlatButton(row, 150, 24, "")
    b:SetPoint("RIGHT")
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    local function name(v) return (type(v) == "number" and names[v + 1] or names[v]) or tostring(v) end
    local function show() b:SetLabel("<   " .. name(db[key]) .. "   >") end
    b:SetScript("OnClick", function(_, button)
        local index = 1
        for i, v in ipairs(values) do if v == db[key] then index = i end end
        index = button == "RightButton" and (index - 2) % #values + 1 or index % #values + 1
        db[key] = values[index]
        show()
        apply()
    end)
    refreshers[#refreshers + 1] = show
end

local function ApplyAll() ApplyLook(); ShowPage(current) end

resetSize:SetScript("OnClick", function()
    db.size = DEFAULTS.size
    frame:ClearAllPoints()
    frame:SetPoint("CENTER", 0, 20)
    Refresh()
    ApplyLook()
end)

local picture = NewPage("Picture")
Section(picture, "Size and shape")
Slider(picture, "Screen size", "size", 0.3, 0.92, 0.02, percent, ApplyLook)
Choice(picture, "Shape", "aspect", SHAPES, SHAPE_NAMES, ApplyLook)
Section(picture, "Colour")
Choice(picture, "Colours", "palette", { 0, 1, 2, 3, 4, 5 }, PALETTES, ApplyEmulator)
Slider(picture, "Brightness", "brightness", 0.5, 1.5, 0.05, percent, ApplyEmulator)
Slider(picture, "Colour strength", "saturation", 0, 2, 0.05, percent, ApplyEmulator)
Section(picture, "Effects")
Slider(picture, "Scanlines", "scanlines", 0, 0.9, 0.05, percent, ApplyEmulator)
Toggle(picture, "Smooth pixels", "smooth", ApplyEmulator)

local sound = NewPage("Sound")
Section(sound, "Sound")
Slider(sound, "Volume", "volume", 0, 1, 0.05, percent, ApplyLook)
Toggle(sound, "Mute", "mute", ApplyLook)
Section(sound, "The world around you")
Toggle(sound, "Show the game on the cabinet", "cabinet", ApplyEmulator)
Slider(sound, "Dim the world", "dim", 0, 0.9, 0.05, percent, ApplyLook)

local window = NewPage("Window")
Section(window, "Behaviour")
Toggle(window, "Pause while options are open", "pauseInOptions", ApplyPause)
Toggle(window, "Lock the game window in place", "lock", function() end)
Slider(window, "Fast-forward speed", "fastSpeed", 2, 8, 1, function(v) return v .. "x" end, ApplyEmulator)
Section(window, "Layout")
Toggle(window, "Show the key help", "help", ApplyLook)
Slider(window, "Options window scale", "optionsScale", 0.7, 1.3, 0.05, percent, ApplyLook)

local appearance = NewPage("Appearance")
Section(appearance, "Style")
local styleNames, styleKeys = {}, {}
for _, s in ipairs(STYLES) do styleKeys[#styleKeys + 1] = s.key; styleNames[s.key] = s.name end
Choice(appearance, "Look", "style", styleKeys, styleNames, ApplyAll)
do  -- accent colour: a swatch opening the stock colour picker, and a reset back to the style's own colour
    local row = Row(appearance, 30)
    local text = Text(row, 12, "text")
    text:SetPoint("LEFT")
    text:SetText("Accent colour")
    local reset = FlatButton(row, 60, 24, "Reset")
    reset:SetPoint("RIGHT")
    local swatch = CreateFrame("Button", nil, row)
    swatch:SetSize(84, 24)
    swatch:SetPoint("RIGHT", reset, "LEFT", -6, 0)
    local fill = Solid(swatch, "ARTWORK", "edge", 1)
    fill:SetPoint("TOPLEFT", 3, -3); fill:SetPoint("BOTTOMRIGHT", -3, 3)
    Lines(swatch, "dim")
    local function set(r, g, b) db.accent = r and { r, g, b } or false; ApplyAll() end
    swatch:SetScript("OnClick", function()
        local c = Style().edge
        ColorPickerFrame.hasOpacity, ColorPickerFrame.opacityFunc = false, nil
        ColorPickerFrame.previousValues = db.accent and { c[1], c[2], c[3] } or false
        ColorPickerFrame.func = function() set(ColorPickerFrame:GetColorRGB()) end
        ColorPickerFrame.cancelFunc = function(previous)
            if previous then set(previous[1], previous[2], previous[3]) else set() end
        end
        ColorPickerFrame:SetColorRGB(c[1], c[2], c[3])
        ColorPickerFrame:Hide(); ColorPickerFrame:Show()
    end)
    reset:SetScript("OnClick", function() set() end)
end
Section(appearance, "Game window")
Slider(appearance, "Background opacity", "opacity", 0.3, 1, 0.05, percent, ApplyLook)
Slider(appearance, "Title size", "titleSize", 14, 26, 1, function(v) return v .. " pt" end, ApplyLook)
Toggle(appearance, "Line around the picture", "frameLine", ApplyLook)

-- Controls: click one, then press its new key (Esc cancels).
local controls = NewPage("Controls")
Section(controls, "Click, then press a key")
local bindButtons = {}
local function RefreshBinds()
    for action, b in pairs(bindButtons) do
        if options.waiting == action then
            b:SetLabel("Press a key...")
        else
            local st = Style().muted
            b:SetLabel(string.format("%s  |cff%02x%02x%02x%s|r", ACTION_NAMES[action], st[1] * 255, st[2] * 255, st[3] * 255,
                KeyText(db.keys[action])))
        end
    end
end
local grid = Row(controls, math.ceil(#BIND_ORDER / 2) * 26)
for i, action in ipairs(BIND_ORDER) do
    local b = FlatButton(grid, 10, 22, "")
    local row, col = math.floor((i - 1) / 2), (i - 1) % 2
    if col == 0 then
        b:SetPoint("TOPLEFT", 0, -row * 26)
        b:SetPoint("TOPRIGHT", grid, "TOP", -3, -row * 26)
    else
        b:SetPoint("TOPLEFT", grid, "TOP", 3, -row * 26)
        b:SetPoint("TOPRIGHT", 0, -row * 26)
    end
    b:SetScript("OnClick", function() options.waiting = action; RefreshBinds() end)
    bindButtons[action] = b
end
refreshers[#refreshers + 1] = RefreshBinds

-- Bottom of the rail: put things back.
local resetWindow = FlatButton(options, RAIL - 16, 24, "Reset window")
resetWindow:SetPoint("BOTTOMLEFT", 8, 44)
resetWindow:SetScript("OnClick", function()
    db.size, db.optionsW, db.optionsH, db.optionsScale = DEFAULTS.size, DEFAULTS.optionsW, DEFAULTS.optionsH, 1
    frame:ClearAllPoints(); frame:SetPoint("CENTER", 0, 20)
    options:ClearAllPoints(); options:SetPoint("CENTER", 260, 0)
    options:SetSize(db.optionsW, db.optionsH)
    Refresh()
    ApplyLook()
end)
local defaults = FlatButton(options, RAIL - 16, 24, "All defaults")
defaults:SetPoint("BOTTOMLEFT", 8, 14)
defaults:SetScript("OnClick", function()
    for k, v in pairs(DEFAULTS) do db[k] = v end
    db.keys = {}
    for action, key in pairs(DEFAULT_KEYS) do db.keys[action] = key end
    Rebind()
    Refresh()
    ApplyEmulator()
    ApplyAll()
end)

local function Bind(action, key)
    for other, bound in pairs(db.keys) do if bound == key then db.keys[other] = nil end end  -- one action per key
    db.keys[action] = key
    Rebind()
    help:SetText(HelpText())
end

-- ---- keys -------------------------------------------------------------------------------------------------------
frame:SetScript("OnKeyDown", function(self, key)
    if dos and not options.waiting then  -- every key belongs to the game, Escape included (Quit is the button)
        if key == "ESCAPE" and options:IsShown() then options:Hide() else Arcade_SetKey(key, true) end
        return
    end
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
    if dos then return Arcade_SetKey(key, false) end
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
frame:SetScript("OnShow", function(self)
    ApplyLook()
    if EsteriaStyle then EsteriaStyle.Tween("arcade-window", 0.2, 0, function(p) self:SetAlpha(p) end) end
end)
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
frame:SetScript("OnEvent", function(self, event, arg1, arg2)
    if event == "VARIABLES_LOADED" then
        LoadSettings()
        Refresh()
        ShowPage("Picture")
        ApplyEmulator()
        PaintAll()
    elseif event == "ARCADE_LOADING" or event == "ARCADE_STARTED" then
        if not db then LoadSettings() end
        frame.title:SetText(arg1)
        local _, _, _, _, _, _, _, _, core = Arcade_GetState()
        dos = (arg2 or core) == "dosbox_pure" or (arg2 or core) == "starcraft"
        if dos then pointer:Show() else pointer:Hide() end
        help:SetText(HelpText())
        frame.subtitle:SetText((SYSTEMS[arg2 or core] or "Arcade") .. "  ·  your progress is saved to this character")
        if event == "ARCADE_LOADING" then
            layoutKey = nil
            userPaused = false
            ApplyEmulator()
        end
        frame:Show()
        Front(frame)
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
