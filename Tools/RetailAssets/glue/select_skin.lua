-- Character select skin (wowdecomp)
-- Chrome around the Esteria Character Select (ECS_*.lua), which owns the roster, search and favorites. The skin
-- never touches rows or controls ECS positions; it adds: dark edge panels in the creator's style, a realm header, a
-- compact favorites list (ECS's own rows, scaled down, portraits kept), a details panel for the selected character,
-- a name plate, and the scene dip (out to black, swap at the darkest point, fade back in; the native side fades the
-- new model in and plays its salute). Any error turns the skin off for the session.
-- The stock screen is saved in Tools\RetailAssets\glue\backup\charselect-stock (ECS files in ..\ecs).
-- Its own function: keeps the chunk clear of Lua's 200-locals-per-function limit.
(function()
local SK = { ready = false, broken = nil, fade = nil, pending = nil, alpha = 0 };

local GOLD = { 0.91, 0.77, 0.42 };
local GOLD_DIM = { 0.54, 0.43, 0.18 };
local INK = { 0.03, 0.04, 0.055 };
local TEXT = { 0.88, 0.86, 0.80 };
local MUTED = { 0.60, 0.59, 0.55 };
local FONT = "Fonts\\FRIZQT__.TTF";
local WHITE = "Interface\\Buttons\\WHITE8X8";
local PANEL_W, EDGE_X = 300, 14;
-- The logo texture is a 1024 square whose art spans y 240..692, so crop to that band to fill the panel's width.
local LOGO_W, LOGO_H = PANEL_W - 2 * EDGE_X, 122;
local COUNT_Y, RULE_Y, LABEL_Y = LOGO_H + 12, LOGO_H + 34, LOGO_H + 42;
local FAV_SCALE, FAV_TOP = 0.72, LOGO_H + 62; -- favorites keep ECS's rows (portraits and all), drawn at 72%
local GEAR_SLOTS = 19;

-- Scene dip: out to black, swap, back in.
local FADE_PEAK, FADE_OUT, FADE_IN = 0.92, 0.16, 0.55;

local function SK_Log(text)
    if BodyShapeLog then BodyShapeLog("select skin: " .. tostring(text)); end
end

local function Solid(parent, layer, r, g, b, a)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(WHITE);
    tex:SetVertexColor(r, g, b, a or 1);
    return tex;
end

local function Text(parent, size, color, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GlueFontHighlightSmall");
    fs:SetFont(FONT, size);
    fs:SetTextColor(color[1], color[2], color[3]);
    fs:SetShadowOffset(1, -1);
    fs:SetShadowColor(0, 0, 0, 0.8);
    return fs;
end

local function Smooth(t)
    if t <= 0 then return 0; elseif t >= 1 then return 1; end
    return t * t * (3 - 2 * t);
end

local function Hex(colour)
    colour = colour or { 1, 1, 1 };
    return string.format("|cff%02x%02x%02x", math.floor(colour[1] * 255 + 0.5), math.floor(colour[2] * 255 + 0.5),
        math.floor(colour[3] * 255 + 0.5));
end

-- The selected character as ECS sees it (custom race names, faction, note); stock fields when ECS is absent.
local function Current()
    local selected = CharacterSelect.selectedIndex or 0;
    local data = ECS and ECS.Integrate and ECS.Integrate.data;
    if data then
        for _, character in ipairs(data) do
            if character.realIndex == selected then return character; end
        end
    end
    local name, race, class, level, zone, _, ghost = GetCharacterInfo(selected);
    if not name then return nil; end
    return { realIndex = selected, name = name, raceName = race, className = class, level = level, zone = zone, ghost = ghost };
end

local function InfoLine(character)
    return (LEVEL or "Level") .. " " .. tostring(character.level or "?") .. "  " .. (character.raceName or "") .. "  "
        .. Hex(character.classColour) .. (character.className or "") .. "|r";
end

-- ---- static chrome -------------------------------------------------------------------------------------------

local function BuildChrome()
    local chrome = CreateFrame("Frame", "CharSelectSkinChrome", CharacterSelectUI);
    chrome:SetAllPoints(CharacterSelectUI);
    chrome:SetFrameLevel(CharacterSelectUI:GetFrameLevel());
    SK.chrome = chrome;

    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local left = side == "LEFT";
        local panel = Solid(chrome, "BACKGROUND", INK[1], INK[2], INK[3], 0.84);
        panel:SetPoint("TOP" .. side, chrome, "TOP" .. side, 0, 0);
        panel:SetPoint("BOTTOM" .. side, chrome, "BOTTOM" .. side, 0, 0);
        panel:SetWidth(PANEL_W);
        local fade = chrome:CreateTexture(nil, "BACKGROUND");
        fade:SetTexture(WHITE);
        fade:SetWidth(70);
        fade:SetPoint("TOP" .. (left and "LEFT" or "RIGHT"), panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        fade:SetPoint("BOTTOM" .. (left and "LEFT" or "RIGHT"), panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);
        if left then
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0.5, INK[1], INK[2], INK[3], 0);
        else
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0, INK[1], INK[2], INK[3], 0.5);
        end
        local edge = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        edge:SetWidth(1);
        edge:SetPoint("TOP", panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        edge:SetPoint("BOTTOM", panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);
    end

    -- Logo across the panel with the character count under it, then the favorites label.
    -- The stock logo belongs to the parent frame and would sit under the panel; draw the same art from the chrome.
    local logo = chrome:CreateTexture(nil, "OVERLAY");
    logo:SetTexture(CharacterSelectLogo:GetTexture());
    logo:SetTexCoord(0, 1, 0.23, 0.68);
    logo:SetSize(LOGO_W, LOGO_H);
    logo:SetPoint("TOPLEFT", chrome, "TOPLEFT", EDGE_X, -8);
    CharacterSelectLogo:SetAlpha(0);
    SK.count = Text(chrome, 12, MUTED);
    SK.count:SetPoint("TOP", chrome, "TOPLEFT", PANEL_W / 2, -COUNT_Y);
    local rule = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.6);
    rule:SetSize(PANEL_W - 2 * EDGE_X, 1);
    rule:SetPoint("TOPLEFT", chrome, "TOPLEFT", EDGE_X, -RULE_Y);
    SK.favLabel = Text(chrome, 13, GOLD);
    SK.favLabel:SetPoint("TOPLEFT", chrome, "TOPLEFT", EDGE_X + 6, -LABEL_Y);
    SK.favLabel:SetText("FAVORITES");

    -- The name and info line under the character are hidden (the details panel and roster already show them).
    -- Tick still fills SK.nameInfo, so keep it, hidden.
    SK.nameInfo = Text(chrome, 14, MUTED);
    SK.nameInfo:Hide();
    CharSelectCharacterName:Hide();

    -- Scene dip, above the 3D view and below every control.
    local overlay = CreateFrame("Frame", "CharSelectSkinFade", CharacterSelect);
    overlay:SetAllPoints(CharacterSelect);
    overlay:SetFrameLevel(CharacterSelectUI:GetFrameLevel());
    local black = overlay:CreateTexture(nil, "BACKGROUND");
    black:SetAllPoints(overlay);
    black:SetTexture(WHITE);
    black:SetVertexColor(0, 0, 0, 1);
    overlay:SetAlpha(0);
    SK.overlay = overlay;
end

-- ---- details panel -------------------------------------------------------------------------------------------

local DETAIL_ROWS = { "Level", "Race", "Class", "Faction", "Zone", "Status", "Gear", "Pet" };
local ROW_PITCH = 20;

local function BuildDetails()
    local panel = CreateFrame("Frame", "CharSelectSkinDetails", SK.chrome);
    panel:SetWidth(PANEL_W - 2 * EDGE_X);
    panel:SetHeight(48 + #DETAIL_ROWS * ROW_PITCH + 40);
    local back = Solid(panel, "BACKGROUND", 1, 1, 1, 0.04);
    back:SetAllPoints(panel);
    local top = Solid(panel, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.7);
    top:SetHeight(1);
    top:SetPoint("TOPLEFT", panel, "TOPLEFT", 0, 0);
    top:SetPoint("TOPRIGHT", panel, "TOPRIGHT", 0, 0);

    panel.title = Text(panel, 15, GOLD);
    panel.title:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, -12);
    panel.title:SetPoint("RIGHT", panel, "RIGHT", -12, 0);
    panel.title:SetJustifyH("LEFT");
    local sub = Text(panel, 11, MUTED);
    sub:SetPoint("TOPLEFT", panel.title, "BOTTOMLEFT", 0, -3);
    sub:SetText("CHARACTER DETAILS");

    panel.labels, panel.values = {}, {};
    for index, label in ipairs(DETAIL_ROWS) do
        local y = -(44 + (index - 1) * ROW_PITCH);
        local line = Solid(panel, "BACKGROUND", 1, 1, 1, 0.06);
        line:SetHeight(1);
        line:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, y + 3);
        line:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -12, y + 3);
        panel.labels[index] = Text(panel, 12, MUTED);
        panel.labels[index]:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, y - 2);
        panel.labels[index]:SetText(label);
        panel.values[index] = Text(panel, 12, TEXT);
        panel.values[index]:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -12, y - 2);
        panel.values[index]:SetJustifyH("RIGHT");
        panel.values[index]:SetWidth(170);
    end
    panel.note = Text(panel, 11, MUTED);
    panel.note:SetPoint("TOPLEFT", panel, "TOPLEFT", 12, -(48 + #DETAIL_ROWS * ROW_PITCH + 2));
    panel.note:SetWidth(PANEL_W - 2 * EDGE_X - 24);
    panel.note:SetJustifyH("LEFT");
    SK.details = panel;
end

local function UpdateDetails(character)
    local panel = SK.details;
    if not character then panel:Hide(); return; end
    panel:Show();
    panel.title:SetText(character.name or "");
    local faction = character.factionAccent or MUTED;
    local equipped, petLevel = 0, 0;
    if GetCharacterDetails then equipped, petLevel = GetCharacterDetails(character.realIndex); end
    local values = {
        tostring(character.level or "?"),
        character.raceName or "Unknown",
        Hex(character.classColour) .. (character.className or "Hero") .. "|r",
        Hex(faction) .. (character.factionName or "Neutral") .. "|r",
        character.zone or "Unknown Zone",
        character.ghost and "|cffff4040Dead|r" or "|cff5fd35fAlive|r",
        (equipped or 0) .. " / " .. GEAR_SLOTS .. " slots",
        (petLevel and petLevel > 0) and ("Level " .. petLevel) or "None",
    };
    for index, value in ipairs(values) do panel.values[index]:SetText(value); end
    local note = character.note;
    if type(note) == "string" and note ~= "" then
        panel.note:SetText("Note: " .. note);
    else
        panel.note:SetText("");
    end
end

-- ---- bottom-left buttons -------------------------------------------------------------------------------------

-- Back, then Options to its right. AddOns lives inside the Options menu now, so the standalone button goes.
-- ECS re-lays these on every roster refresh, so this runs after its layout and again on each tick.
local function Buttons()
    local back, options, addons = CharacterSelectBackButton, OptionsButton2, CharacterSelectAddonsButton;
    if addons then addons:Hide(); addons:EnableMouse(false); end
    if back and options then
        options:ClearAllPoints();
        options:SetPoint("BOTTOMLEFT", back, "BOTTOMRIGHT", -14, 0);
    end
end

-- ---- per-tick refresh ----------------------------------------------------------------------------------------

local function Tick()
    local numChars = GetNumCharacters();
    SK.count:SetText(numChars .. (numChars == 1 and " character" or " characters"));

    -- Favorites: ECS's own rows, smaller. Re-assert every tick; ECS only sets the anchor once at build.
    Buttons();
    local favorites = ECSFavoritesPanel;
    local below = nil;
    if favorites then
        if favorites:GetScale() ~= FAV_SCALE then favorites:SetScale(FAV_SCALE); end
        favorites:ClearAllPoints();
        favorites:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", EDGE_X / FAV_SCALE, -FAV_TOP / FAV_SCALE);
        if favorites:IsShown() then below = favorites; end
    end
    if below then SK.favLabel:Show(); else SK.favLabel:Hide(); end
    SK.details:ClearAllPoints();
    if below then
        SK.details:SetPoint("TOPLEFT", below, "BOTTOMLEFT", 0, -10);
    else
        SK.details:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", EDGE_X, -LABEL_Y);
    end

    local character = Current();
    UpdateDetails(character);
    SK.nameInfo:SetText(character and InfoLine(character) .. ((character.zone and character.zone ~= "") and ("  \194\183  " .. character.zone) or "") or "");
end

-- ---- scene dip -----------------------------------------------------------------------------------------------

local stockSelect = CharacterSelect_SelectCharacter;
function CharacterSelect_SelectCharacter(id, noCreate)
    if SK.broken or not SK.ready or noCreate or id == CharacterSelect.createIndex
        or id == CharacterSelect.selectedIndex or not CharacterSelect:IsVisible() then
        return stockSelect(id, noCreate);
    end
    SK.pending = id;
    if not SK.fade or SK.fade.phase == "in" then
        -- Continue from the current darkness so a quick second pick does not flash.
        SK.fade = { phase = "out", t = SK.alpha / FADE_PEAK * FADE_OUT };
    end
end

local function Fade(elapsed)
    local fade = SK.fade;
    if not fade then return; end
    fade.t = fade.t + elapsed;
    if fade.phase == "out" then
        SK.alpha = FADE_PEAK * Smooth(fade.t / FADE_OUT);
        if fade.t >= FADE_OUT then
            local id = SK.pending;
            SK.pending = nil;
            SK.fade = { phase = "in", t = 0 };
            if id then stockSelect(id); end
        end
    else
        SK.alpha = FADE_PEAK * (1 - Smooth(fade.t / FADE_IN));
        if fade.t >= FADE_IN then SK.alpha = 0; SK.fade = nil; end
    end
    SK.overlay:SetAlpha(SK.alpha);
end

-- ---- install -------------------------------------------------------------------------------------------------

local function SetSkin(on)
    if SK.chrome then if on then SK.chrome:Show(); else SK.chrome:Hide(); end end
    if not on then
        if SK.overlay then SK.overlay:SetAlpha(0); end
        if ECSFavoritesPanel then ECSFavoritesPanel:SetScale(1); end
        if SK.pending then local id = SK.pending; SK.pending = nil; SK.fade = nil; stockSelect(id); end
    end
end

local function Fail(where, err)
    SK.broken = tostring(err);
    SK_Log(where .. ": " .. SK.broken);
    pcall(SetSkin, false);
end

local function SK_Install()
    BuildChrome();
    BuildDetails();
    CharacterSelectUI:EnableMouseWheel(true);
    CharacterSelectUI:SetScript("OnMouseWheel", function(_, delta) CycleCharCustomization("EA_ZOOM", delta); end);
    SK.ready = true;
    if ECS and ECS.UI and type(ECS.UI.Layout) == "function" then
        local layout = ECS.UI.Layout;
        ECS.UI.Layout = function(...)
            layout(...);
            if not SK.broken then pcall(Buttons); end
        end;
    end
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        if not CharacterSelectUI:IsVisible() then return; end
        -- "Hide All" hides the roster frame; the panels go with it.
        if CharacterSelectCharacterFrame:IsShown() then SK.chrome:Show(); else SK.chrome:Hide(); end
        local ok, err = pcall(Fade, elapsed);
        if not ok then Fail("fade error", err); self:SetScript("OnUpdate", nil); return; end
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        ok, err = pcall(Tick);
        if not ok then Fail("tick error", err); self:SetScript("OnUpdate", nil); end
    end);
    SK_Log("installed");
end

-- ---- camera zoom and pan -------------------------------------------------------------------------------------

-- Mouse wheel zooms to the face and right-drag pans, as in the creator (the native side handles the camera).
-- Left-drag still turns the character. Right-click without moving resets the pan.
local stockMouseDown, stockMouseUp, stockUpdate = CharacterSelectFrame_OnMouseDown, CharacterSelectFrame_OnMouseUp, CharacterSelectFrame_OnUpdate;
local panX, panY, panMoved;

function CharacterSelectFrame_OnMouseDown(button, ...)
    if button == "RightButton" then
        panX, panY = GetCursorPosition();
        panMoved = false;
    end
    return stockMouseDown(button, ...);
end

function CharacterSelectFrame_OnMouseUp(button, ...)
    if button == "RightButton" and panX then
        if not panMoved then CycleCharCustomization("EA_PAN_RESET", 1); end
        panX = nil;
    end
    return stockMouseUp(button, ...);
end

function CharacterSelectFrame_OnUpdate(...)
    if panX then
        local x, y = GetCursorPosition();
        local dx, dy = x - panX, y - panY;
        if dx ~= 0 or dy ~= 0 then
            panMoved = true;
            CycleCharCustomization("EA_PAN", dx / GetScreenHeight(), dy / GetScreenHeight());
            panX, panY = x, y;
        end
    end
    return stockUpdate(...);
end

local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    if not (CharacterSelect and CharacterSelectUI and CharacterSelectCharacterFrame and CharacterSelectLogo
        and CharSelectCharacterName) then
        return;
    end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(SK_Install);
    if not ok then Fail("install failed", err); end
end);
end)();

-- Screen transitions (wowdecomp)
-- Character select <-> create and the create screen's race page <-> customization page dip to black and back
-- instead of snapping. The change itself runs at the darkest point. Any error runs the change at once and turns
-- the fade off for the session.
(function()
local FADE_OUT, FADE_IN = 0.18, 0.35;
local T = { alpha = 0, fade = nil, pending = nil, running = false, broken = nil };
local SCREENS = { charselect = true, charcreate = true };
local WHITE_TEX = "Interface" .. string.char(92) .. "Buttons" .. string.char(92) .. "WHITE8X8";

local function Smooth(t)
    if t <= 0 then return 0; elseif t >= 1 then return 1; end
    return t * t * (3 - 2 * t);
end

local function Run(fn)
    T.running = true;
    local ok, err = pcall(fn);
    T.running = false;
    if not ok then T.broken = tostring(err); T.fade = nil; T.alpha = 0; if T.overlay then T.overlay:SetAlpha(0); end end
end

local function Step(elapsed)
    local fade = T.fade;
    if not fade then return; end
    fade.t = fade.t + elapsed;
    if fade.phase == "out" then
        T.alpha = Smooth(fade.t / FADE_OUT);
        if fade.t >= FADE_OUT then
            local fn = T.pending;
            T.pending = nil;
            T.fade = { phase = "in", t = 0 };
            if fn then Run(fn); end
        end
    else
        T.alpha = 1 - Smooth(fade.t / FADE_IN);
        if fade.t >= FADE_IN then T.alpha = 0; T.fade = nil; end
    end
    T.overlay:SetAlpha(T.alpha);
end

local function Overlay()
    if T.overlay then return T.overlay; end
    if not (GlueParent and CreateFrame) then return nil; end
    local frame = CreateFrame("Frame", "GlueTransitionFade", GlueParent);
    frame:SetAllPoints(GlueParent);
    frame:SetFrameStrata("TOOLTIP");
    local black = frame:CreateTexture(nil, "BACKGROUND");
    black:SetAllPoints(frame);
    black:SetTexture(WHITE_TEX);
    black:SetVertexColor(0, 0, 0, 1);
    frame:SetAlpha(0);
    frame:SetScript("OnUpdate", function(_, elapsed)
        if not T.broken then
            local ok, err = pcall(Step, elapsed);
            if not ok then
                T.broken = tostring(err);
                local fn = T.pending;
                T.pending, T.fade, T.alpha = nil, nil, 0;
                frame:SetAlpha(0);
                if fn then Run(fn); end
            end
        end
    end);
    T.overlay = frame;
    return frame;
end

local function Transition(fn)
    if T.broken or T.running or not Overlay() then return fn(); end
    if T.pending then
        -- A second change while the first waits: do both now and fade in over them.
        local old = T.pending;
        T.pending = nil;
        Run(old);
        Run(fn);
        T.fade = { phase = "in", t = 0 };
        return;
    end
    T.pending = fn;
    -- Continue from the current darkness so a change mid fade-in does not flash.
    T.fade = { phase = "out", t = T.alpha * FADE_OUT };
end

local stockScreen = SetGlueScreen;
function SetGlueScreen(name, ...)
    local from = CURRENT_GLUE_SCREEN;
    if T.broken or T.running or not (SCREENS[name] and SCREENS[from] and name ~= from) then
        return stockScreen(name, ...);
    end
    local args = { ... };
    Transition(function() stockScreen(name, unpack(args)); end);
end

-- The create screen's functions are defined after this file loads, so wrap them once they exist.
local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    local toggle, back = CharacterCreate_TogglePersonalization, CharacterCreate_Back;
    if type(toggle) ~= "function" or type(back) ~= "function" then return; end
    self:SetScript("OnUpdate", nil);
    function CharacterCreate_TogglePersonalization(...)
        if T.broken or T.running then return toggle(...); end
        local args = { ... };
        Transition(function() toggle(unpack(args)); end);
    end
    function CharacterCreate_Back(...)
        -- Back from the customization page toggles it; Back from the race page goes through SetGlueScreen.
        if T.broken or T.running or not (CharacterCreate and CharacterCreate.personalizationMode) then return back(...); end
        local args = { ... };
        Transition(function() back(unpack(args)); end);
    end
end);
end)();

-- AddOns entry in the Options menu (wowdecomp)
-- The character select screen's AddOns button moved into the Options menu. The menu is shared with the login screen,
-- so the entry only shows (and the menu only grows to fit it) while character select is the current screen.
(function()
local EXTRA = 55;
local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    local container = OptionsSelectFrameBackgroundContainer;
    local last = OptionsSelectFrameBackgroundContainerRealmResetOptionsButton;
    if not (OptionsSelectFrame and OptionsSelectFrameBackground and container and last and AddonList) then return; end
    self:SetScript("OnUpdate", nil);
    local ok = pcall(function()
        local button = CreateFrame("Button", "OptionsSelectAddOnsButton", container, "GlueButtonTemplateBlue");
        button:SetSize(220, 45);
        button:SetPoint("TOP", last, "BOTTOM", 0, -10);
        button:SetText(ADDONS or "AddOns");
        button:SetScript("OnClick", function()
            PlaySound("igMainMenuOption");
            OptionsSelectFrame:Hide();
            AddonList:Show();
        end);
        local height = OptionsSelectFrameBackground:GetHeight();
        OptionsSelectFrame:HookScript("OnShow", function()
            local inSelect = CURRENT_GLUE_SCREEN == "charselect";
            if inSelect then button:Show(); else button:Hide(); end
            OptionsSelectFrameBackground:SetHeight(height + (inSelect and EXTRA or 0));
        end);
    end);
    if not ok then return; end
end);
end)();
