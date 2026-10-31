-- EverlookBroadcasting: in-game internet radio player for 3.3.5a. Needs the client's EverlookRadio extension
-- (Extensions/EverlookRadio), which provides PlayRadioStream / StopRadioStream / SetRadioVolume / IsRadioPlaying /
-- GetRadioTitle / SetRadioAutoPause.
--
-- The window is built from retail art shipped in Textures/ (metal portrait frame, rock body, the
-- spellbook's parchment page, the red three-slice button, the minimal slider), so it needs no other addon.
-- With DragonUI loaded the launcher is a button on its micro bar; otherwise it is a button on the minimap.

local TEX = "Interface\\AddOns\\EverlookBroadcasting\\Textures\\"

local STATIONS = {
    { name = "Everlook Relay",      genre = "Turtle WoW",      icon = "INV_BannerPVP_02",                  url = "http://radiodirect.turtle-music.org/stream" },
    { name = "Radio Rivendell",     genre = "Fantasy Scores",  icon = "INV_Misc_Book_09",                  url = "http://play.radiorivendell.com/radio/8000/radio.mp3" },
    { name = "Plush Recordings",    genre = "Atmospheric D&B", icon = "Spell_Frost_SummonWaterElemental",  url = "http://azura.drmnbss.org/listen/plushrecs/radio.mp3" },
    { name = "City Poppers",        genre = "City Pop",        icon = "INV_Drink_05",                      url = "http://stream.zeno.fm/ricmeoweux2uv" },
    { name = "Nightwave Plaza",     genre = "Vaporwave",       icon = "Spell_Arcane_PrismaticCloak",       url = "http://radio.plaza.one/mp3" },
    { name = "Lofi Girl",           genre = "Chill Beats",     icon = "INV_Misc_Drum_01",                  url = "http://stream.zeno.fm/f3wvbbqmdg8uv" },
    { name = "SomaFM Groove Salad", genre = "Ambient",         icon = "INV_Misc_Flower_02",                url = "http://ice1.somafm.com/groovesalad-128-mp3" },
    { name = "SomaFM Secret Agent", genre = "Retro",           icon = "Ability_Rogue_Disguise",            url = "http://ice1.somafm.com/secretagent-128-mp3" },
    { name = "idobi Radio",         genre = "Emo / Pop Punk",  icon = "Spell_Shadow_DeathScream",          url = "http://idobiradio.idobi.com/;stream.mp3" },
    { name = "idobi Howl",          genre = "Metalcore",       icon = "Ability_Druid_ChallangingRoar",     url = "http://idobihowl.idobi.com/;stream.mp3" },
}

local DEFAULTS = {
    volume = 0.5, selectedStation = 1, customUrl = "", useCustom = false,
    showMinimap = true, minimapPos = 220,   -- autoPause: nil = follow the game's Sound in Background setting
}

local db                    -- EverlookBroadcastingDB, once the saved variables are in
local frame                 -- the player window
local ui = {}               -- widgets the logic below touches
local launcher              -- micro bar or minimap button
local tuned = false         -- a station was asked for and not stopped since

local function SafeCall(fn, ...)
    if type(fn) == "function" then
        local ok, res = pcall(fn, ...)
        if ok then return res end
    end
end

local function HasRadio() return type(PlayRadioStream) == "function" and type(StopRadioStream) == "function" end
local function IsPlaying() return HasRadio() and SafeCall(IsRadioPlaying) and true or false end
local function IsStreamUrl(s) return type(s) == "string" and s:match("^https?://.+") ~= nil end
local function SetShown(o, shown) if shown then o:Show() else o:Hide() end end

--- What Play would tune to: name, genre, icon path, url.
local function CurrentChoice()
    if db.useCustom and IsStreamUrl(db.customUrl) then
        return "Custom Stream", db.customUrl, "Interface\\Icons\\INV_Gizmo_GoblinBoomBox_01", db.customUrl
    end
    local st = STATIONS[db.selectedStation] or STATIONS[1]
    return st.name, st.genre, "Interface\\Icons\\" .. st.icon, st.url
end

-- ---------------------------------------------------------------------------------------------------
-- Play / stop / status
-- ---------------------------------------------------------------------------------------------------
local Refresh   -- defined with the window

function EverlookBroadcasting_PlayCurrent()
    if not HasRadio() then
        if Refresh then Refresh() end
        return
    end
    local _, _, _, url = CurrentChoice()
    SafeCall(SetRadioVolume, db.volume)
    SafeCall(PlayRadioStream, url)
    tuned = true
    if Refresh then Refresh() end
end

function EverlookBroadcasting_Stop()
    if HasRadio() then SafeCall(StopRadioStream) end
    tuned = false
    if Refresh then Refresh() end
end

-- Auto-pause follows the game's own "Sound in Background" setting unless the player ticks it otherwise:
-- with background sound on, the radio plays on while the game is minimised or unfocused.
local function GamePausesInBackground() return GetCVar("Sound_EnableSoundWhenGameIsInBG") ~= "1" end

local function AutoPauseOn()
    if db.autoPause ~= nil then return db.autoPause end
    return GamePausesInBackground()
end

local function ApplyAutoPause()
    local on = AutoPauseOn()
    if ui.autoPause then ui.autoPause:SetChecked(on) end
    if EverlookBroadcastingOptAutoPause then EverlookBroadcastingOptAutoPause:SetChecked(on) end
    if HasRadio() then SafeCall(SetRadioAutoPause or SetRadioStopOnMinimize, on and 1 or 0) end
end

local function SetAutoPause(on)
    on = on and true or false
    -- Ticked back to what the game setting says anyway: drop the override and follow the setting again.
    if on == GamePausesInBackground() then db.autoPause = nil else db.autoPause = on end
    ApplyAutoPause()
end

--- Status line: text, r, g, b.
local function StatusLine()
    if not HasRadio() then return "The radio is not available in this client.", 1, 0.25, 0.25 end
    local name = CurrentChoice()
    if not tuned then return "Stopped", 0.6, 0.6, 0.6 end
    if not IsPlaying() then return "Tuning in: " .. name .. "...", 1, 0.82, 0.2 end
    local title = SafeCall(GetRadioTitle)
    if title and title ~= "" then return "LIVE: " .. title, 0.3, 1, 0.3 end
    return "Tuned to: " .. name, 0.3, 1, 0.3
end

local function ToggleWindow()
    if not frame then return end
    if frame:IsShown() then frame:Hide() else frame:Show(); frame:Raise() end
end

local function LauncherClick(_, button)
    if button == "RightButton" then
        if tuned then EverlookBroadcasting_Stop() else EverlookBroadcasting_PlayCurrent() end
    else
        ToggleWindow()
    end
end

local function TooltipLines()
    local name = CurrentChoice()
    local status = StatusLine()
    return {
        "Everlook Broadcasting Co.",
        "|cffcccccc" .. "Station: |r|cffffffff" .. name .. "|r",
        "|cffcccccc" .. status .. "|r",
        " ",
        "|cff00ccffLeft-Click:|r |cffb0b0b0Radio player|r",
        "|cff00ccffRight-Click:|r |cffb0b0b0Play / stop|r",
    }
end

-- ---------------------------------------------------------------------------------------------------
-- Widgets
-- ---------------------------------------------------------------------------------------------------
local INK = { 0.22, 0.13, 0.05 }        -- text on the parchment
local GOLD = { 1, 0.82, 0.1 }

local function Tex(parent, layer, file, l, r, t, b)
    local x = parent:CreateTexture(nil, layer)
    x:SetTexture(file)
    if l then x:SetTexCoord(l, r, t, b) end
    return x
end

local function Tile(x, horiz, vert)   -- both exist on this client; guarded so a client without them stretches
    if x.SetHorizTile then x:SetHorizTile(horiz) end
    if x.SetVertTile then x:SetVertTile(vert) end
end

local function Text(parent, font, text, r, g, b)
    local fs = parent:CreateFontString(nil, "OVERLAY", font)
    fs:SetText(text or "")
    if r then fs:SetTextColor(r, g, b) end
    if r == INK[1] then fs:SetShadowOffset(0, 0) end   -- the fonts' drop shadow smears dark ink on the parchment
    return fs
end

local PLATE = {
    bgFile = "Interface\\Buttons\\WHITE8X8", edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 },
}

local function Plate(f, selected)
    if selected then
        f:SetBackdropColor(0.17, 0.12, 0.03, 0.95)
        f:SetBackdropBorderColor(GOLD[1], GOLD[2], GOLD[3], 1)
    else
        f:SetBackdropColor(0.07, 0.065, 0.06, 0.92)
        f:SetBackdropBorderColor(0.38, 0.34, 0.28, 1)
    end
end

--- A dark rounded plate button with an icon and two lines of text; :SetSelected(bool) turns it gold.
local function PlateButton(parent, w, h, icon, line1, line2)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(w, h)
    b:SetBackdrop(PLATE)
    Plate(b, false)

    local hl = b:CreateTexture(nil, "HIGHLIGHT")
    hl:SetTexture(1, 1, 1, 0.06)
    hl:SetPoint("TOPLEFT", 3, -3)
    hl:SetPoint("BOTTOMRIGHT", -3, 3)

    local size = h - 14
    b.icon = Tex(b, "ARTWORK", icon, 0.07, 0.93, 0.07, 0.93)
    b.icon:SetSize(size, size)
    b.icon:SetPoint("LEFT", 8, 0)

    b.name = Text(b, "GameFontHighlight", line1)
    b.name:SetPoint("TOPLEFT", b.icon, "TOPRIGHT", 10, -1)
    b.sub = Text(b, "GameFontDisableSmall", line2)
    b.sub:SetPoint("BOTTOMLEFT", b.icon, "BOTTOMRIGHT", 10, 1)

    function b:SetSelected(on)
        Plate(self, on)
        if on then self.name:SetTextColor(GOLD[1], GOLD[2], GOLD[3]) else self.name:SetTextColor(1, 1, 1) end
        if self.bars then
            for _, bar in ipairs(self.bars) do
                if on then bar:SetTexture(GOLD[1], GOLD[2], GOLD[3], 1) else bar:SetTexture(0.42, 0.4, 0.36, 1) end
            end
        end
    end
    return b
end

local function AddSignalBars(b)
    b.bars = {}
    for i = 1, 4 do
        local bar = b:CreateTexture(nil, "ARTWORK")
        bar:SetSize(3, 3 + i * 3)
        bar:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -32 + i * 5, 13)
        b.bars[i] = bar
    end
end

-- Retail's 128-RedButton, three slices: fixed caps, stretched centre.
local RED = TEX .. "128redbutton.blp"
local RED_COORDS = {
    normal  = { { 0.763672, 0.986328, 0.444824, 0.507324 }, { 0, 0.125, 0.000488, 0.062988 }, { 0.001953, 0.572266, 0.254395, 0.316895 } },
    pressed = { { 0.763672, 0.986328, 0.571777, 0.634277 }, { 0, 0.125, 0.127441, 0.189941 }, { 0.001953, 0.572266, 0.381348, 0.443848 } },
}

local function RedButton(parent, w, h, label, gray)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(w, h)
    local left, mid, right = Tex(b, "BACKGROUND", RED), Tex(b, "BACKGROUND", RED), Tex(b, "BACKGROUND", RED)
    left:SetSize(h * 114 / 128, h)
    left:SetPoint("TOPLEFT")
    right:SetSize(h * 292 / 128, h)
    right:SetPoint("TOPRIGHT")
    mid:SetPoint("TOPLEFT", left, "TOPRIGHT")
    mid:SetPoint("BOTTOMRIGHT", right, "BOTTOMLEFT")
    if gray then for _, t in ipairs({ left, mid, right }) do t:SetDesaturated(true) end end

    local hl = Tex(b, "HIGHLIGHT", RED, 0.001953, 0.863281, 0.190918, 0.253418)
    hl:SetAllPoints(b)
    hl:SetBlendMode("ADD")

    local fs = Text(b, "GameFontNormal", label)
    if gray then fs:SetTextColor(0.85, 0.85, 0.85) end

    local function state(name, dx, dy)
        local c = RED_COORDS[name]
        left:SetTexCoord(unpack(c[1])); mid:SetTexCoord(unpack(c[2])); right:SetTexCoord(unpack(c[3]))
        fs:ClearAllPoints()
        fs:SetPoint("CENTER", b, "CENTER", dx, dy)
    end
    state("normal", 0, 1)
    b:SetScript("OnMouseDown", function() state("pressed", 1, 0) end)
    b:SetScript("OnMouseUp", function() state("normal", 0, 1) end)
    return b
end

-- Retail's minimal slider: rounded caps, a one-pixel run between them, the diamond thumb.
local function VolumeSlider(parent, width)
    local SL = TEX .. "minimalsliderbar.blp"
    local s = CreateFrame("Slider", "EverlookBroadcastingVolSlider", parent)
    s:SetOrientation("HORIZONTAL")
    s:SetSize(width, 19)
    s:SetMinMaxValues(0, 100)
    s:SetValueStep(1)
    local left = Tex(s, "BACKGROUND", SL, 0.437500, 0.781250, 0.320312, 0.453125)
    left:SetSize(11, 17)
    left:SetPoint("LEFT")
    local right = Tex(s, "BACKGROUND", SL, 0.031250, 0.375000, 0.484375, 0.617188)
    right:SetSize(11, 17)
    right:SetPoint("RIGHT")
    local mid = Tex(s, "BACKGROUND", SL, 0, 0.031250, 0.007812, 0.140625)
    mid:SetHeight(17)
    mid:SetPoint("LEFT", left, "RIGHT")
    mid:SetPoint("RIGHT", right, "LEFT")
    s:SetThumbTexture(SL)
    local thumb = s:GetThumbTexture()
    thumb:SetTexCoord(0.031250, 0.656250, 0.156250, 0.304688)
    thumb:SetSize(20, 19)
    s:EnableMouseWheel(true)
    s:SetScript("OnMouseWheel", function(self, delta) self:SetValue(self:GetValue() + delta * 5) end)
    return s
end

-- ---------------------------------------------------------------------------------------------------
-- The window
-- ---------------------------------------------------------------------------------------------------
-- The parchment pane is W - 216 by H - 58: kept at the spellbook page's own 808 x 867 so the art is not stretched.
local W, H = 716, 594

-- Retail's PortraitFrameTemplate nine-slice: piece sizes, offsets and rects as DragonUI_NewEra applies them.
local function BuildChrome(f)
    local corners, vert, horiz = TEX .. "metal-corners.blp", TEX .. "metal-edges-vert.blp", TEX .. "metal-edges-horiz.blp"

    local bg = f:CreateTexture(nil, "BACKGROUND")
    bg:SetTexture(TEX .. "background-rock.blp", "REPEAT", "REPEAT")
    Tile(bg, true, true)
    bg:SetVertexColor(0.32, 0.32, 0.32)
    bg:SetPoint("TOPLEFT", 2, -21)
    bg:SetPoint("BOTTOMRIGHT", -2, 2)

    local portrait = Tex(f, "BORDER", TEX .. "radio-portrait.blp")
    portrait:SetSize(60, 60)
    portrait:SetPoint("TOPLEFT", -5, 7)

    local tl = Tex(f, "ARTWORK", corners, 0.001953, 0.294922, 0.298828, 0.591797)
    tl:SetSize(75, 75); tl:SetPoint("TOPLEFT", -13, 16)
    local tr = Tex(f, "ARTWORK", corners, 0.298828, 0.591797, 0.001953, 0.294922)
    tr:SetSize(75, 75); tr:SetPoint("TOPRIGHT", 4, 16)
    local bl = Tex(f, "ARTWORK", corners, 0.298828, 0.423828, 0.298828, 0.423828)
    bl:SetSize(32, 32); bl:SetPoint("BOTTOMLEFT", -13, -3)
    local br = Tex(f, "ARTWORK", corners, 0.427734, 0.552734, 0.298828, 0.423828)
    br:SetSize(32, 32); br:SetPoint("BOTTOMRIGHT", 4, -3)

    local top = Tex(f, "ARTWORK", horiz, 0, 1, 0.003906, 0.589844)
    top:SetPoint("TOPLEFT", tl, "TOPRIGHT"); top:SetPoint("TOPRIGHT", tr, "TOPLEFT"); top:SetHeight(75)
    Tile(top, true, false)
    local bottom = Tex(f, "ARTWORK", horiz, 0, 0.5, 0.597656, 0.847656)
    bottom:SetPoint("BOTTOMLEFT", bl, "BOTTOMRIGHT"); bottom:SetPoint("BOTTOMRIGHT", br, "BOTTOMLEFT"); bottom:SetHeight(32)
    Tile(bottom, true, false)
    local left = Tex(f, "ARTWORK", vert, 0.001953, 0.294922, 0, 1)
    left:SetPoint("TOPLEFT", tl, "BOTTOMLEFT"); left:SetPoint("BOTTOMLEFT", bl, "TOPLEFT"); left:SetWidth(75)
    Tile(left, false, true)
    local right = Tex(f, "ARTWORK", vert, 0.298828, 0.591797, 0, 1)
    right:SetPoint("TOPRIGHT", tr, "BOTTOMRIGHT"); right:SetPoint("BOTTOMRIGHT", br, "TOPRIGHT"); right:SetWidth(75)
    Tile(right, false, true)

    local title = Text(f, "GameFontNormal", "Everlook Broadcasting Co.")
    title:SetPoint("TOP", 15, -5)
    local subtitle = Text(f, "GameFontDisable", "Live Azeroth Community Radio & Sound System")
    subtitle:SetPoint("TOPLEFT", 64, -29)

    local close = CreateFrame("Button", nil, f)
    close:SetSize(24, 24)
    close:SetPoint("TOPRIGHT", 1, 0)
    local exit = TEX .. "redbutton-exit.blp"
    close:SetNormalTexture(exit);    close:GetNormalTexture():SetTexCoord(0.152344, 0.292969, 0.007812, 0.304688)
    close:SetPushedTexture(exit);    close:GetPushedTexture():SetTexCoord(0.152344, 0.292969, 0.632812, 0.929688)
    close:SetHighlightTexture(exit); close:GetHighlightTexture():SetTexCoord(0.449219, 0.589844, 0.007812, 0.304688)
    close:GetHighlightTexture():SetBlendMode("ADD")
    close:SetScript("OnClick", function() f:Hide() end)
end

local function PageHeader(page, text, hint)
    local icon = Tex(page, "ARTWORK", TEX .. "radio-portrait.blp")
    icon:SetSize(30, 30)
    icon:SetPoint("TOPLEFT", 28, -22)
    local head = Text(page, "QuestTitleFont", text, INK[1], INK[2], INK[3])
    head:SetPoint("LEFT", icon, "RIGHT", 10, 0)
    if hint then
        local fs = Text(page, "GameFontNormalSmall", hint, 0.4, 0.27, 0.13)
        fs:SetPoint("TOPRIGHT", -30, -36)
    end
    local line = Tex(page, "ARTWORK", TEX .. "spellbook-items.blp", 126 / 512, 458 / 512, 204 / 512, 220 / 512)
    line:SetHeight(12)
    line:SetPoint("TOPLEFT", 26, -54)
    line:SetPoint("TOPRIGHT", -26, -54)
end

local function BuildStationsPage(content)
    local page = CreateFrame("Frame", nil, content)
    page:SetAllPoints(content)
    PageHeader(page, "Broadcast Stations", "Click to Tune In")

    local tileW, tileH, gap = 216, 42, 6
    ui.stations = {}
    for i, st in ipairs(STATIONS) do
        local b = PlateButton(page, tileW, tileH, "Interface\\Icons\\" .. st.icon, st.name, "(" .. st.genre .. ")")
        AddSignalBars(b)
        b:SetPoint("TOPLEFT", 28 + ((i - 1) % 2) * (tileW + 12), -74 - math.floor((i - 1) / 2) * (tileH + gap))
        b:SetScript("OnClick", function()
            db.selectedStation, db.useCustom = i, false
            PlaySound("igMainMenuOptionCheckBoxOn")
            EverlookBroadcasting_PlayCurrent()
        end)
        ui.stations[i] = b
    end
    local rows = math.ceil(#STATIONS / 2)
    local y = -74 - rows * (tileH + gap) - 6

    local label = Text(page, "QuestFont", "Custom Stream URL", INK[1], INK[2], INK[3])
    label:SetPoint("TOPLEFT", 30, y)

    local box = CreateFrame("Frame", nil, page)
    box:SetBackdrop(PLATE)
    Plate(box, false)
    box:SetHeight(28)
    box:SetPoint("TOPLEFT", 28, y - 20)
    box:SetPoint("TOPRIGHT", -28, y - 20)
    ui.urlBox = box

    local edit = CreateFrame("EditBox", "EverlookBroadcastingUrlEdit", box)
    edit:SetPoint("TOPLEFT", 10, -5)
    edit:SetPoint("BOTTOMRIGHT", -10, 5)
    edit:SetFontObject("GameFontHighlight")
    edit:SetAutoFocus(false)
    edit:SetMaxLetters(500)
    local hint = Text(box, "GameFontDisable", "Enter Icecast / Shoutcast / MP3 / OGG URL...")
    hint:SetPoint("LEFT", 12, 0)
    local function hintState() SetShown(hint, edit:GetText() == "" and not edit.focused) end
    edit:SetScript("OnEditFocusGained", function(self) self.focused = true; hintState() end)
    edit:SetScript("OnEditFocusLost", function(self) self.focused = false; hintState() end)
    edit:SetScript("OnTextChanged", function(self) db.customUrl = self:GetText(); hintState() end)
    edit:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    edit:SetScript("OnEnterPressed", function(self)
        self:ClearFocus()
        if IsStreamUrl(db.customUrl) then
            db.useCustom = true
            EverlookBroadcasting_PlayCurrent()
        end
    end)
    edit:SetText(db.customUrl or "")
    hintState()
    return page
end

local function BuildNowPlayingPage(content)
    local page = CreateFrame("Frame", nil, content)
    page:SetAllPoints(content)
    PageHeader(page, "Now Playing")

    local plate = CreateFrame("Frame", nil, page)
    plate:SetBackdrop(PLATE)
    Plate(plate, true)
    plate:SetSize(84, 84)
    plate:SetPoint("TOPLEFT", 30, -80)
    ui.nowIcon = Tex(plate, "ARTWORK", nil, 0.07, 0.93, 0.07, 0.93)
    ui.nowIcon:SetPoint("TOPLEFT", 6, -6)
    ui.nowIcon:SetPoint("BOTTOMRIGHT", -6, 6)

    ui.nowName = Text(page, "QuestTitleFont", "", INK[1], INK[2], INK[3])
    ui.nowName:SetPoint("TOPLEFT", plate, "TOPRIGHT", 16, -6)
    ui.nowSub = Text(page, "QuestFont", "", 0.4, 0.27, 0.13)
    ui.nowSub:SetPoint("TOPLEFT", ui.nowName, "BOTTOMLEFT", 0, -6)
    ui.nowSub:SetWidth(330)
    ui.nowSub:SetJustifyH("LEFT")

    local label = Text(page, "QuestFont", "On the air", INK[1], INK[2], INK[3])
    label:SetPoint("TOPLEFT", 30, -186)
    ui.nowTitle = Text(page, "QuestTitleFont", "", INK[1], INK[2], INK[3])
    ui.nowTitle:SetPoint("TOPLEFT", 30, -208)
    ui.nowTitle:SetWidth(440)
    ui.nowTitle:SetJustifyH("LEFT")
    return page
end

local function ShowTab(index)
    db.tab = index
    for i, tab in ipairs(ui.tabs) do
        tab:SetSelected(i == index)
        SetShown(ui.pages[i], i == index)
    end
end

Refresh = function()
    if not frame then return end
    local name, sub, icon = CurrentChoice()
    for i, b in ipairs(ui.stations) do b:SetSelected(not db.useCustom and i == db.selectedStation) end
    Plate(ui.urlBox, db.useCustom and IsStreamUrl(db.customUrl))

    local text, r, g, b = StatusLine()
    ui.status:SetText(text)
    ui.status:SetTextColor(r, g, b)
    ui.statusIcon:SetVertexColor(r, g, b)

    ui.nowIcon:SetTexture(icon)
    ui.nowName:SetText(name)
    ui.nowSub:SetText(sub)
    local title = tuned and IsPlaying() and SafeCall(GetRadioTitle) or nil
    ui.nowTitle:SetText((title and title ~= "") and title or (tuned and "..." or "Nothing: the radio is stopped."))
    ui.tabs[2].sub:SetText(tuned and name or "Current broadcast")

    if launcher and launcher.SetRadioOpen then launcher:SetRadioOpen(frame:IsShown()) end
end

local function BuildWindow()
    local f = CreateFrame("Frame", "EverlookBroadcastingFrame", UIParent)
    frame = f
    f:SetSize(W, H)
    f:SetPoint("CENTER", UIParent, "CENTER", 0, 30)
    f:SetFrameStrata("MEDIUM")
    f:SetToplevel(true)
    f:SetMovable(true)
    f:SetClampedToScreen(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetScript("OnDragStart", f.StartMoving)
    f:SetScript("OnDragStop", f.StopMovingOrSizing)
    f:Hide()
    tinsert(UISpecialFrames, "EverlookBroadcastingFrame")   -- Escape closes it
    BuildChrome(f)

    -- Left: the two tabs on a dark pane.
    local pane = CreateFrame("Frame", nil, f)
    pane:SetBackdrop(PLATE)
    pane:SetBackdropColor(0, 0, 0, 0.5)
    pane:SetBackdropBorderColor(0.3, 0.3, 0.3, 1)
    pane:SetPoint("TOPLEFT", 8, -50)
    pane:SetPoint("BOTTOMLEFT", 8, 8)
    pane:SetWidth(196)

    ui.tabs = {
        PlateButton(pane, 180, 50, TEX .. "radio-portrait.blp", "Stations", "Click to tune in"),
        PlateButton(pane, 180, 50, "Interface\\Icons\\INV_Misc_Gear_01", "Now Playing", "Current broadcast"),
    }
    ui.tabs[1].icon:SetTexCoord(0, 1, 0, 1)
    for i, tab in ipairs(ui.tabs) do
        tab:SetPoint("TOPLEFT", 8, -8 - (i - 1) * 56)
        tab.sub:SetWidth(120)
        tab.sub:SetHeight(12)
        tab.sub:SetJustifyH("LEFT")
        tab:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); ShowTab(i); Refresh() end)
    end

    -- Right: the parchment page. The status line and the controls sit under both tabs' pages.
    local content = CreateFrame("Frame", nil, f)
    content:SetPoint("TOPLEFT", pane, "TOPRIGHT", 4, 0)
    content:SetPoint("BOTTOMRIGHT", -8, 8)
    local paper = Tex(content, "BACKGROUND", TEX .. "parchment.blp")
    paper:SetAllPoints(content)

    ui.pages = { BuildStationsPage(content), BuildNowPlayingPage(content) }

    local statusBox = CreateFrame("Frame", nil, content)
    statusBox:SetBackdrop(PLATE)
    Plate(statusBox, false)
    statusBox:SetHeight(32)
    statusBox:SetPoint("BOTTOMLEFT", 28, 112)
    statusBox:SetPoint("BOTTOMRIGHT", -28, 112)
    ui.statusIcon = Tex(statusBox, "ARTWORK", TEX .. "radio-portrait.blp")
    ui.statusIcon:SetSize(22, 22)
    ui.statusIcon:SetPoint("LEFT", 8, 0)
    ui.status = Text(statusBox, "GameFontHighlight", "")
    ui.status:SetPoint("LEFT", ui.statusIcon, "RIGHT", 8, 0)
    ui.status:SetPoint("RIGHT", -10, 0)
    ui.status:SetHeight(14)
    ui.status:SetJustifyH("LEFT")

    local play = RedButton(content, 100, 32, "Play")
    play:SetPoint("BOTTOMLEFT", 28, 58)
    play:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); EverlookBroadcasting_PlayCurrent() end)
    local stop = RedButton(content, 90, 32, "Stop", true)
    stop:SetPoint("LEFT", play, "RIGHT", 8, 0)
    stop:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOff"); EverlookBroadcasting_Stop() end)

    local slider = VolumeSlider(content, 180)
    slider:SetPoint("LEFT", stop, "RIGHT", 28, -2)
    ui.slider = slider
    local volText = Text(content, "GameFontNormalSmall", "", INK[1], INK[2], INK[3])
    volText:SetPoint("BOTTOMLEFT", slider, "TOPLEFT", 0, 3)
    local lo = Text(content, "GameFontNormalSmall", "0%", 0.4, 0.27, 0.13)
    lo:SetPoint("TOPLEFT", slider, "BOTTOMLEFT", 0, -1)
    local hi = Text(content, "GameFontNormalSmall", "100%", 0.4, 0.27, 0.13)
    hi:SetPoint("TOPRIGHT", slider, "BOTTOMRIGHT", 0, -1)
    slider:SetScript("OnValueChanged", function(_, value)
        value = math.floor(value + 0.5)
        db.volume = value / 100
        volText:SetText("Volume: " .. value .. "%")
        if HasRadio() then SafeCall(SetRadioVolume, db.volume) end
    end)
    slider:SetValue(math.floor(db.volume * 100 + 0.5))
    volText:SetText("Volume: " .. math.floor(db.volume * 100 + 0.5) .. "%")

    local check = CreateFrame("CheckButton", "EverlookBroadcastingAutoPauseCheck", content, "UICheckButtonTemplate")
    check:SetSize(26, 26)
    check:SetPoint("BOTTOMLEFT", 26, 22)
    check:SetChecked(AutoPauseOn())
    check:SetScript("OnClick", function(self) SetAutoPause(self:GetChecked()) end)
    ui.autoPause = check
    local checkText = Text(content, "GameFontNormalSmall", "Auto-pause when minimized or leaving window", INK[1], INK[2], INK[3])
    checkText:SetPoint("LEFT", check, "RIGHT", 2, 0)
    checkText:SetJustifyH("LEFT")

    -- The status line follows the stream while the window is up (tuning in -> live -> song changes).
    local since = 0
    f:SetScript("OnUpdate", function(_, elapsed)
        since = since + elapsed
        if since >= 0.5 then since = 0; Refresh() end
    end)
    f:SetScript("OnShow", function() PlaySound("igCharacterInfoOpen"); Refresh() end)
    f:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); Refresh() end)

    ShowTab(db.tab == 2 and 2 or 1)
    Refresh()
end

-- ---------------------------------------------------------------------------------------------------
-- Launcher: DragonUI's micro bar when DragonUI is there, else the minimap
-- ---------------------------------------------------------------------------------------------------
local function LauncherWanted() return db.showMinimap ~= false end
local CreateMinimapButton

-- The DragonUI micro strip owns RadioMicroButton (Addons/EsteriaShell/esteria_microbuttons.lua); it calls these.
function EverlookBroadcasting_MicroClick(_, button) LauncherClick(nil, button) end

function EverlookBroadcasting_MicroTooltip(self)
    GameTooltip:SetOwner(self, "ANCHOR_LEFT")
    for i, line in ipairs(TooltipLines()) do
        if i == 1 then GameTooltip:SetText(line, 1, 0.82, 0.1) else GameTooltip:AddLine(line) end
    end
    GameTooltip:Show()
end

local function CreateMicroButton()
    local b = _G.RadioMicroButton
    if not b then return end
    function b:SetRadioOpen(on) self:SetButtonState(on and "PUSHED" or "NORMAL", on and true or nil) end
    function b:Relayout() end
    return b
end

CreateMinimapButton = function()
    local b = CreateFrame("Button", "EverlookBroadcasting_ToggleButton", Minimap)
    b:SetSize(32, 32)
    b:SetFrameStrata("MEDIUM")
    b:SetFrameLevel(8)
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    b:RegisterForDrag("LeftButton")

    local icon = Tex(b, "BACKGROUND", TEX .. "radio-portrait.blp")
    icon:SetSize(21, 21)
    icon:SetPoint("CENTER", 0, 1)
    local border = Tex(b, "OVERLAY", "Interface\\Minimap\\MiniMap-TrackingBorder")
    border:SetSize(54, 54)
    border:SetPoint("CENTER", 11, -11)
    b:SetHighlightTexture("Interface\\Minimap\\UI-Minimap-ZoomButton-Highlight", "ADD")

    local function place()
        local angle = math.rad(db.minimapPos or 220)
        b:ClearAllPoints()
        b:SetPoint("CENTER", Minimap, "CENTER", math.cos(angle) * 80, math.sin(angle) * 80)
    end
    b:SetScript("OnDragStart", function(self)
        self:SetScript("OnUpdate", function()
            local mx, my = Minimap:GetCenter()
            local cx, cy = GetCursorPosition()
            local scale = Minimap:GetEffectiveScale()
            db.minimapPos = math.deg(math.atan2(cy / scale - my, cx / scale - mx)) % 360
            place()
        end)
    end)
    b:SetScript("OnDragStop", function(self) self:SetScript("OnUpdate", nil) end)
    b:SetScript("OnClick", LauncherClick)
    b:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        for i, line in ipairs(TooltipLines()) do
            if i == 1 then GameTooltip:SetText(line, 1, 0.82, 0.1) else GameTooltip:AddLine(line) end
        end
        GameTooltip:AddLine("|cff00ccffDrag:|r |cffb0b0b0Move around the minimap|r")
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    function b:Relayout() place(); SetShown(self, LauncherWanted()) end
    b:Relayout()
    return b
end

local function SetLauncherShown(on)
    db.showMinimap = on and true or false
    if launcher then launcher:Relayout() end
    if EverlookBroadcastingOptLauncher then EverlookBroadcastingOptLauncher:SetChecked(db.showMinimap) end
end

-- ---------------------------------------------------------------------------------------------------
-- Interface options (Escape -> Interface -> AddOns -> Everlook Broadcasting Co.)
-- ---------------------------------------------------------------------------------------------------
local function BuildOptions()
    local panel = CreateFrame("Frame", "EverlookBroadcastingOptionsPanel", UIParent)
    panel.name = "Everlook Broadcasting Co."

    local title = Text(panel, "GameFontNormalLarge", "Everlook Broadcasting Co.")
    title:SetPoint("TOPLEFT", 16, -16)
    local sub = Text(panel, "GameFontHighlightSmall", "Live internet radio in game. /radio opens the player.")
    sub:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8)

    local function check(name, label, anchor, onClick)
        local c = CreateFrame("CheckButton", name, panel, "UICheckButtonTemplate")
        c:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", anchor == sub and -2 or 0, anchor == sub and -16 or -8)
        _G[name .. "Text"]:SetText(label)
        _G[name .. "Text"]:SetFontObject("GameFontNormal")
        c:SetScript("OnClick", function(self) onClick(self:GetChecked() and true or false) end)
        return c
    end
    local autoPause = check("EverlookBroadcastingOptAutoPause", "Auto-pause when the game is minimized or loses focus (default: follows Sound in Background)", sub, SetAutoPause)
    local launch = check("EverlookBroadcastingOptLauncher", "Show the radio button (micro bar with DragonUI, otherwise the minimap)", autoPause, SetLauncherShown)

    local open = CreateFrame("Button", nil, panel, "UIPanelButtonTemplate")
    open:SetSize(160, 26)
    open:SetPoint("TOPLEFT", launch, "BOTTOMLEFT", 2, -16)
    open:SetText("Open Radio Player")
    open:SetScript("OnClick", ToggleWindow)

    panel.refresh = function()
        autoPause:SetChecked(AutoPauseOn())
        launch:SetChecked(db.showMinimap)
    end
    panel.default = function() db.autoPause = nil; ApplyAutoPause(); SetLauncherShown(true) end
    InterfaceOptions_AddCategory(panel)
end

-- ---------------------------------------------------------------------------------------------------
-- Slash commands and start-up
-- ---------------------------------------------------------------------------------------------------
local function Say(text) DEFAULT_CHAT_FRAME:AddMessage("|cff00ff00[Everlook Broadcasting]|r " .. text) end

SLASH_EVERLOOKBROADCASTING1 = "/radio"
SLASH_EVERLOOKBROADCASTING2 = "/stream"
SlashCmdList["EVERLOOKBROADCASTING"] = function(msg)
    if not frame then return end
    msg = (msg or ""):trim():lower()
    local vol = tonumber(msg:match("^vol%s*(%d+)$"))
    if msg == "play" then
        EverlookBroadcasting_PlayCurrent()
    elseif msg == "stop" then
        EverlookBroadcasting_Stop()
    elseif msg == "min" or msg == "pause" or msg == "bg" then
        SetAutoPause(not AutoPauseOn())
        Say("Auto-pause: " .. (AutoPauseOn() and "|cff00ff00on|r" or "|cffff0000off|r"))
    elseif msg == "minimap" or msg == "icon" or msg == "btn" or msg == "map" then
        SetLauncherShown(not LauncherWanted())
        Say("Radio button: " .. (db.showMinimap and "|cff00ff00shown|r" or "|cffff0000hidden|r"))
    elseif msg == "reset" then
        db.minimapPos, db.autoPause = 220, nil
        ApplyAutoPause()
        SetLauncherShown(true)
        frame:ClearAllPoints()
        frame:SetPoint("CENTER", UIParent, "CENTER", 0, 30)
        Say("Window and button positions reset.")
    elseif vol then
        ui.slider:SetValue(math.min(vol, 100))
    else
        ToggleWindow()
    end
end

local loader = CreateFrame("Frame")
loader:RegisterEvent("ADDON_LOADED")
loader:RegisterEvent("PLAYER_LOGIN")
loader:RegisterEvent("CVAR_UPDATE")
loader:RegisterEvent("PLAYER_LOGOUT")
loader:SetScript("OnEvent", function(self, event, name)
    if event == "ADDON_LOADED" and name == "EverlookBroadcasting" then
        EverlookBroadcastingDB = EverlookBroadcastingDB or {}
        db = EverlookBroadcastingDB
        for k, v in pairs(DEFAULTS) do if db[k] == nil then db[k] = v end end
        if db.stationList ~= 2 or not STATIONS[db.selectedStation] then db.selectedStation, db.stationList = 1, 2 end

        BuildWindow()
        BuildOptions()
        db.stopOnMinimize = nil   -- 1.x key: it was written as true for everyone, so it says nothing
        ApplyAutoPause()
    elseif event == "PLAYER_LOGOUT" then
        -- Leaving the world (logout, exit, and /reload, which the client cannot tell apart here): the radio
        -- belongs to the character session, so it does not play on over the character screen.
        if HasRadio() then SafeCall(StopRadioStream) end
    elseif event == "CVAR_UPDATE" and db then
        ApplyAutoPause()   -- Sound in Background may have just been changed
    elseif event == "PLAYER_LOGIN" and db then
        -- Chosen here, not at load: DragonUI builds its micro bar frame during its own start-up.
        launcher = CreateMicroButton() or CreateMinimapButton()
        ApplyAutoPause()   -- the radio functions exist by now even if they did not at load
        Say(HasRadio() and "Everlook Broadcasting Co. is on the air. Type |cffffff00/radio|r."
            or "|cffff8800The client has no radio support: the player opens but cannot play.|r")
    end
end)
