-- Creator skin (wowdecomp)
-- Restyles the two customization pages (Appearance and Body) without changing how they work: dark edge panels with
-- a gold accent, slim option rows, a step indicator, underlined name fields and flat buttons. It only restyles the
-- existing frames while customization is open and restores the shared ones (Back) when it closes. Any error turns
-- the skin off for the session and leaves the stock look.
-- Its own function: the creator chunk is near Lua's 200-locals-per-function limit.
(function()
local SK ={ on = false, broken = nil, rows = {}, sliders = {}, buttons = {} };

local GOLD = { 0.91, 0.77, 0.42 };
local GOLD_DIM = { 0.54, 0.43, 0.18 };
local INK = { 0.03, 0.04, 0.055 };
local TEXT = { 0.88, 0.86, 0.80 };
local MUTED = { 0.60, 0.59, 0.55 };
-- Everything uses the game font; body text runs a size below headers.
local HEAD_FONT, BODY_FONT = "Fonts\\FRIZQT__.TTF", "Fonts\\FRIZQT__.TTF";
local WHITE = "Interface\\Buttons\\WHITE8X8";
local PANEL_W, EDGE_X = 440, 55; -- rows sit EDGE_X in from each screen edge and are 340 wide

local function SK_Log(text)
    if BodyShapeLog then BodyShapeLog("skin: " .. tostring(text)); end
end

local function Solid(parent, layer, r, g, b, a)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(WHITE);
    tex:SetVertexColor(r, g, b, a or 1);
    return tex;
end

local function Text(parent, font, size, color, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GlueFontHighlightSmall");
    fs:SetFont(font, size);
    fs:SetTextColor(color[1], color[2], color[3]);
    fs:SetShadowOffset(1, -1);
    fs:SetShadowColor(0, 0, 0, 0.8);
    return fs;
end

local function Personalizing()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

local function BodyPage()
    return BodyShapePage_Active and BodyShapePage_Active() and BodyShapePanel and BodyShapePanel:IsShown();
end

-- ---- static chrome -------------------------------------------------------------------------------------------

local function BuildChrome()
    local chrome = CreateFrame("Frame", "CreatorSkinChrome", CharacterCreateFrame);
    chrome:SetAllPoints(CharacterCreateFrame);
    chrome:SetFrameLevel(CharacterCreateFrame:GetFrameLevel());
    chrome:Hide();
    SK.chrome = chrome;

    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local left = side == "LEFT";
        local panel = Solid(chrome, "BACKGROUND", INK[1], INK[2], INK[3], 0.84);
        panel:SetPoint("TOP" .. side, chrome, "TOP" .. side, 0, 0);
        panel:SetPoint("BOTTOM" .. side, chrome, "BOTTOM" .. side, 0, 0);
        panel:SetWidth(PANEL_W);
        -- A soft fade past the panel so it melts into the scene instead of ending in a hard block.
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

        local title = Text(chrome, HEAD_FONT, 16, GOLD);
        local subtitle = Text(chrome, BODY_FONT, 12, MUTED);
        subtitle:SetPoint("TOP" .. side, title, "BOTTOM" .. side, 0, -4);
        local rule = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.6);
        rule:SetSize(340, 1);
        SK[side] = { title = title, subtitle = subtitle, rule = rule, panel = panel, fade = fade };
    end

    -- Step indicator: Race, Appearance, Body (the last only for races with the Body page).
    local steps, lines = {}, {};
    for index, name in ipairs({ "RACE", "APPEARANCE", "BODY" }) do
        steps[index] = Text(chrome, HEAD_FONT, 13, MUTED);
        steps[index]:SetText(index .. "   " .. name);
    end
    for index = 1, 2 do
        lines[index] = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        lines[index]:SetSize(40, 1);
    end
    steps[2]:SetPoint("TOP", chrome, "TOP", 0, -24);
    lines[1]:SetPoint("RIGHT", steps[2], "LEFT", -12, 0);
    steps[1]:SetPoint("RIGHT", lines[1], "LEFT", -12, 0);
    lines[2]:SetPoint("LEFT", steps[2], "RIGHT", 12, 0);
    steps[3]:SetPoint("LEFT", lines[2], "RIGHT", 12, 0);
    SK.steps, SK.stepLines = steps, lines;

    local hint = Text(chrome, BODY_FONT, 12, MUTED);
    hint:SetPoint("BOTTOM", chrome, "BOTTOM", 0, 14);
    hint:SetText("Left-drag rotate   \194\183   right-drag pan   \194\183   wheel zoom");
    SK.hint = hint;
end

-- Header text sits flush with the rows. On the race page `slide` (0 to -PANEL_W) moves the left panel, its header
-- and rule off to the left; the fade and edge line follow the panel by their anchors.
local function PlaceHeaders(slide)
    slide = slide or 0;
    local panel = SK.LEFT.panel;
    panel:ClearAllPoints();
    panel:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", slide, 0);
    panel:SetPoint("BOTTOMLEFT", SK.chrome, "BOTTOMLEFT", slide, 0);
    -- Fade the shadow out with the slide. SetAlpha would drop the gradient and leave a white strip; re-set it instead.
    SK.LEFT.fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0.5 * (1 + slide / PANEL_W), INK[1], INK[2], INK[3], 0);
    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local inset = EDGE_X + 12;
        local header = SK[side];
        local x = side == "LEFT" and slide or 0;
        header.rule:ClearAllPoints();
        header.rule:SetPoint("TOP" .. side, SK.chrome, "TOP" .. side, side == "LEFT" and EDGE_X + x or -EDGE_X, -100);
        header.title:ClearAllPoints();
        header.title:SetPoint("TOP" .. side, SK.chrome, "TOP" .. side, side == "LEFT" and inset + x or -inset, -62);
        header.subtitle:ClearAllPoints();
        header.subtitle:SetPoint("TOP" .. side, header.title, "BOTTOM" .. side, 0, -4);
    end
end

-- ---- appearance rows -----------------------------------------------------------------------------------------

local function SkinArrow(button, direction)
    local path = "Interface\\MoneyFrame\\Arrow-" .. direction;
    button:SetNormalTexture(path .. "-Up");
    button:SetPushedTexture(path .. "-Down");
    button:GetNormalTexture():SetTexCoord(0, 1, 0, 1);
    button:GetPushedTexture():SetTexCoord(0, 1, 0, 1);
    button:GetNormalTexture():SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
    button:SetSize(14, 14);
    local highlight = button:GetHighlightTexture();
    if highlight then highlight:SetAlpha(0); end
end

local function SkinRow(row)
    local name = row:GetName();
    local background = _G[name .. "Background"];
    if background then background:SetAlpha(0); end
    _G[name .. "Text"]:SetAlpha(0);

    local hairline = Solid(row, "BACKGROUND", 1, 1, 1, 0.07);
    hairline:SetHeight(1);
    hairline:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0);
    hairline:SetPoint("TOPRIGHT", row, "TOPRIGHT", 0, 0);
    local accent = Solid(row, "ARTWORK", GOLD[1], GOLD[2], GOLD[3], 1);
    accent:SetWidth(2);
    accent:SetPoint("TOPLEFT", row, "TOPLEFT", 0, -1);
    accent:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 0, 1);
    accent:Hide();

    local right, left = _G[name .. "RightButton"], _G[name .. "LeftButton"];
    SkinArrow(right, "Right");
    SkinArrow(left, "Left");
    right:ClearAllPoints();
    right:SetPoint("RIGHT", row, "RIGHT", -10, 0);
    local value = Text(row, BODY_FONT, 13, GOLD);
    value:SetWidth(128);
    value:SetJustifyH("CENTER");
    value:SetPoint("RIGHT", right, "LEFT", -4, 0);
    left:ClearAllPoints();
    left:SetPoint("RIGHT", value, "LEFT", -4, 0);
    local label = Text(row, BODY_FONT, 13, TEXT);
    label:SetJustifyH("LEFT");
    label:SetPoint("LEFT", row, "LEFT", 14, 0);
    label:SetPoint("RIGHT", left, "LEFT", -6, 0);

    -- The whole row opens the choice list; the arrows stay on top of it.
    local choice = _G[name .. "ChoiceButton"];
    choice:ClearAllPoints();
    choice:SetAllPoints(row);
    local highlight = choice:GetHighlightTexture();
    if highlight then
        highlight:SetTexture(WHITE);
        highlight:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 0.10);
        highlight:SetBlendMode("BLEND");
        highlight:SetAllPoints(row);
    end
    choice:HookScript("OnEnter", function() accent:Show(); end);
    choice:HookScript("OnLeave", function() accent:Hide(); end);
    right:SetFrameLevel(choice:GetFrameLevel() + 2);
    left:SetFrameLevel(choice:GetFrameLevel() + 2);

    SK.rows[row] = { label = label, value = value };
end

-- The stock refresh writes "Label  3/36" into the hidden row text; split it, and prefer the choice's own name.
local function UpdateRow(row, skin)
    local text = _G[row:GetName() .. "Text"]:GetText() or "";
    local label, rest = string.match(text, "^(.-)%s%s+(.+)$");
    if not label then label, rest = text, ""; end
    local value, count = string.match(rest, "^(%d+)/(%d+)$");
    local shown = rest;
    if value then
        shown = value .. " / " .. count;
        if EA_IS_RETAIL() then
            local ok, choiceName = pcall(CycleCharCustomization, "EA_CHOICE_NAME", row:GetID(), tonumber(value) - 1);
            if ok and type(choiceName) == "string" and choiceName ~= "" and not string.match(choiceName, "^%d+$") then
                shown = choiceName;
            end
        end
    end
    -- Races on the stock option layout write only the label into the row; read the value from the native selector so
    -- they show the same "n / total" (or the choice's name) as the other races.
    if shown == "" then
        local ok, current, total = pcall(CycleCharCustomization, EA_ACTIVE() and "EA_GET" or "EA_STOCK_GET", row:GetID());
        if ok and type(current) == "number" and type(total) == "number" and total > 0 then
            shown = (current + 1) .. " / " .. total;
            local named, choiceName = pcall(ChoiceText, row:GetID(), current, label);
            if named and type(choiceName) == "string" and choiceName ~= "" and choiceName ~= label .. " " .. (current + 1) then
                shown = choiceName;
            end
        end
    end
    skin.label:SetText(label);
    skin.value:SetText(shown);
end

-- ---- buttons and name fields ---------------------------------------------------------------------------------

-- Back / Next / Accept / Customize already use the stock GlueButtonTemplate that character select uses; the
-- skin only hides their arrow icon so they read exactly like Enter World.
local function SkinButton(button)
    SK.buttons[button] = { icon = _G[button:GetName() .. "Icon"] };
end

local function ApplyButton(button, on)
    local skin = SK.buttons[button];
    if skin and skin.icon then skin.icon:SetAlpha(on and 0 or 1); end
end

-- The same glue button art (GlueButtonTemplate) on buttons that were not built from it.
local GLUE = "Interface\\Glues\\Common\\Glue-Panel-Button-";
local function GlueLook(button)
    for _, state in ipairs({ { "SetNormalTexture", "GetNormalTexture", "Up", 0.578125, 0.75 },
                             { "SetPushedTexture", "GetPushedTexture", "Down", 0.578125, 0.75 },
                             { "SetHighlightTexture", "GetHighlightTexture", "Highlight", 0.625, 0.6875 },
                             { "SetCheckedTexture", "GetCheckedTexture", "Highlight", 0.625, 0.6875 } }) do
        if button[state[1]] then
            button[state[1]](button, GLUE .. state[3]);
            local texture = button[state[2]](button);
            if texture then
                texture:SetTexCoord(0, state[4], 0, state[5]);
                texture:ClearAllPoints();
                texture:SetAllPoints(button);
                texture:SetAlpha(1);
                if state[3] == "Highlight" then texture:SetBlendMode("ADD"); end
            end
        end
    end
end

local function SkinNameEdit(edit)
    for _, region in ipairs({ edit:GetRegions() }) do
        local kind = region:GetObjectType();
        if kind == "Texture" then
            region:SetAlpha(0);
        elseif kind == "FontString" and region:GetText() then -- the "First Name" / "Last Name" caption
            -- Explicit flags and shadow: the template's outline and big drop shadow read as a doubled caption.
            region:SetFont(BODY_FONT, 12, "");
            region:SetShadowOffset(1, -1);
            region:SetShadowColor(0, 0, 0, 0.8);
            region:SetTextColor(MUTED[1], MUTED[2], MUTED[3]);
            region:ClearAllPoints();
            region:SetPoint("BOTTOMLEFT", edit, "TOPLEFT", 15, -8);
            region:SetJustifyH("LEFT");
            region:SetHeight(16);
        end
    end
    edit:SetFont(HEAD_FONT, 15);
    edit:SetTextColor(TEXT[1], TEXT[2], TEXT[3]);
    local underline = Solid(edit, "BORDER", GOLD[1], GOLD[2], GOLD[3], 0.85);
    underline:SetHeight(1);
    underline:SetPoint("BOTTOMLEFT", edit, "BOTTOMLEFT", 12, 6);
    underline:SetPoint("BOTTOMRIGHT", edit, "BOTTOMRIGHT", -10, 6);
end

-- ---- body page -----------------------------------------------------------------------------------------------

local function SkinSlider(slider)
    slider:SetBackdrop({ bgFile = WHITE, tile = false, insets = { left = 0, right = 0, top = 6, bottom = 6 } });
    slider:SetBackdropColor(1, 1, 1, 0.16);
    slider:SetThumbTexture(WHITE);
    local thumb = slider:GetThumbTexture();
    thumb:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 1);
    thumb:SetSize(3, 12);
    local fill = Solid(slider, "ARTWORK", GOLD[1], GOLD[2], GOLD[3], 0.9);
    fill:SetHeight(2);
    local function Paint()
        local lo, hi = slider:GetMinMaxValues();
        local v = slider:GetValue();
        local width = slider:GetWidth();
        local zero = (0 - lo) / math.max(hi - lo, 0.0001) * width;
        local at = (v - lo) / math.max(hi - lo, 0.0001) * width;
        fill:ClearAllPoints();
        if math.abs(at - zero) < 1 then fill:Hide(); return; end
        fill:SetPoint("LEFT", slider, "LEFT", math.min(zero, at), 0);
        fill:SetWidth(math.abs(at - zero));
        fill:Show();
    end
    slider:HookScript("OnValueChanged", Paint);
    Paint();
    local row = slider:GetParent();
    for _, region in ipairs({ row:GetRegions() }) do
        if region:GetObjectType() == "FontString" then
            local isValue = (region:GetText() or ""):find("%%$");
            region:SetFont(BODY_FONT, 13);
            if isValue then region:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); else region:SetTextColor(TEXT[1], TEXT[2], TEXT[3]); end
        end
    end
    SK.sliders[slider] = Paint;
end

local function SkinBodyPage()
    if SK.bodySkinned or not BodyShapePanel then return; end
    -- The page's own column titles are replaced by the skin's panel headers.
    for _, region in ipairs({ BodyShapePanel:GetRegions() }) do
        if region:GetObjectType() == "FontString" and (region:GetText() == "Body" or region:GetText() == "Face") then
            region:SetAlpha(0);
        end
    end
    if BodyShapeTriangle then
        -- The painted triangle art replaces the strip-drawn triangle and its corner labels; the knob stays.
        for _, region in ipairs({ BodyShapeTriangle:GetRegions() }) do
            local kind = region:GetObjectType();
            if kind == "Texture" then
                if region:GetDrawLayer() == "OVERLAY" then region:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
                else region:SetAlpha(0); end
            elseif kind == "FontString" then
                region:SetAlpha(0);
            end
        end
        -- Placement from body_triangle.py placement(): the art's stroke corners land on the knob's
        -- 180 x 156 triangle (CORNERS in body_page.lua), so dragging still lines up with the drawing.
        local art = BodyShapeTriangle:CreateTexture(nil, "ARTWORK");
        art:SetTexture("Interface\\Glues\\CharacterCreate\\EsteriaBodyTriangle");
        art:SetTexCoord(0, 1, 0, 0.888672);
        art:SetSize(217.92, 201.08);
        art:SetPoint("TOPLEFT", BodyShapeTriangle, "BOTTOMLEFT", -17.36, 177.17);
    end
    for _, button in ipairs({ BodyShapeResetButton, BodyShapeRandomButton }) do
        if button then
            button:SetBackdrop(nil);
            GlueLook(button);
            button:SetSize(150, 36);
            button:ClearAllPoints();
        end
    end
    -- Each sits at the inner end of its panel header, clear of the title.
    if BodyShapeResetButton then BodyShapeResetButton:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPLEFT", EDGE_X + 340, -56); end
    if BodyShapeRandomButton then BodyShapeRandomButton:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPRIGHT", -(EDGE_X + 340), -56); end
    SK.bodySkinned = true;
end

-- ---- per-tick state ------------------------------------------------------------------------------------------

local function RaceLine()
    local race = (GetNameForRace and GetNameForRace()) or "";
    local sex = GetSelectedSex and GetSelectedSex() == SEX_MALE and "male" or "female";
    if race == "" then return ""; end
    return race .. ", " .. sex;
end

-- ---- race page -----------------------------------------------------------------------------------------------

-- Portrait centres start here: the tallest portrait (60) clears the header rule at -100.
local GRID_TOP = -150;
SK.races, SK.toggles = {}, {};

-- The three playable factions, in tab order. Membership comes from CharacterCreate.raceIndices<key>: the stock
-- enumeration fills Alliance/Horde from GetFactionForRaceID, and CharacterCreateEnumerateRaces below moves the
-- Freeborn races out of them.
local FACTIONS = {
    { key = "Alliance", name = ALLIANCE or "Alliance", crest = "Interface\\Glues\\CharacterCreate\\AllianceLogo",
      crop = { 0.2, 0.8, 0.2, 0.8 }, color = { 0.36, 0.60, 1.00 }, dark = { 0.03, 0.09, 0.26 }, hex = "|cff5c99ff" },
    { key = "Horde", name = HORDE or "Horde", crest = "Interface\\Glues\\CharacterCreate\\HordeLogo",
      crop = { 0.21, 0.79, 0.2, 0.8 }, color = { 0.92, 0.27, 0.22 }, dark = { 0.24, 0.02, 0.03 }, hex = "|cffeb4538" },
    { key = "Freeborn", name = "Freeborn", crest = "Interface\\Glues\\CharacterSelect\\FreebornLogo",
      crop = { 0, 1, 0, 1 }, color = { 0.74, 0.56, 0.98 }, dark = { 0.14, 0.05, 0.26 }, hex = "|cffbd8ffa" },
};

local function FactionRaces(key)
    return CharacterCreate["raceIndices" .. key] or {};
end

local function FactionOfRace(index)
    for _, faction in ipairs(FACTIONS) do
        for _, raceIndex in ipairs(FactionRaces(faction.key)) do
            if raceIndex == index then return faction; end
        end
    end
end

-- Freeborn is a faction of its own: these races leave the Alliance/Horde grids for a Freeborn grid. The server
-- puts every character of these races on the Freeborn team (both twins of a race; only the Alliance twin is offered).
local FREEBORN_FILES = { VRYKUL = true, THINHUMAN = true, OGREALLIANCE = true, SETHRAKALLIANCE = true,
    DRAKONIDALLIANCE = true, SLIMEGIANTALLIANCE = true, CENTAURALLIANCE = true, ETHEREALALLIANCE = true,
    NAGAHORDE = true, EREDAR = true, GNOLLHORDE = true, SABERONHORDE = true, ARAKKOAHORDE = true };
local FREEBORN_TWINS = { THINHUMANHORDE = true, OGREHORDE = true, DRAKONIDHORDE = true, SLIMEGIANTHORDE = true,
    CENTAURHORDE = true, ETHEREALHORDE = true };
-- Character select only has the localized race name.
local FREEBORN_NAMES = { ["Sethrak"] = true, ["Vrykul"] = true, ["Forgotten"] = true, ["Ogre"] = true,
    ["Slime Giant"] = true, ["Centaur"] = true, ["Drakonid"] = true, ["Ethereal"] = true, ["Naga"] = true,
    ["Eredar"] = true, ["Gnoll"] = true, ["Saberon"] = true, ["Arakkoa"] = true };

local function RaceFile(index)
    local button = _G["CharacterCreateRaceButton" .. (index or 0)];
    return strupper(button and button.raceFileString or "");
end

local function WithoutFreeborn(indices)
    local kept = {};
    for _, index in ipairs(indices or {}) do
        local file = RaceFile(index);
        if not (FREEBORN_FILES[file] or FREEBORN_TWINS[file]) then kept[#kept + 1] = index; end
    end
    return kept;
end

local enumerateRaces = CharacterCreateEnumerateRaces;
function CharacterCreateEnumerateRaces(...)
    enumerateRaces(...);
    for _, index in ipairs(CharacterCreate.raceIndicesHorde or {}) do
        for _, texture in ipairs({ _G["CharacterCreateRaceButton" .. index .. "NormalTexture"],
                                   _G["CharacterCreateRaceButton" .. index .. "PushedTexture"] }) do
            if texture and texture.GetTexCoord then
                local ulx, uly, llx, lly, urx, ury, lrx, lry = texture:GetTexCoord();
                texture:SetTexCoord(urx, ury, lrx, lry, ulx, uly, llx, lly);
            end
        end
    end
    local freeborn = {};
    for index = 1, CharacterCreate.numRaces or 0 do
        local button = _G["CharacterCreateRaceButton" .. index];
        local file = RaceFile(index);
        if FREEBORN_FILES[file] then
            freeborn[#freeborn + 1] = index;
        elseif FREEBORN_TWINS[file] then
            button.enable = false;
            button:SetChecked(nil);
            button:Disable();
            button:Hide();
        end
    end
    CharacterCreate.raceIndicesAlliance = WithoutFreeborn(CharacterCreate.raceIndicesAlliance);
    CharacterCreate.raceIndicesHorde = WithoutFreeborn(CharacterCreate.raceIndicesHorde);
    CharacterCreate.raceIndicesFreeborn = freeborn;
    if FREEBORN_TWINS[RaceFile(GetSelectedRace())] and freeborn[1] then SetSelectedRace(freeborn[1]); end
    -- The team comes from the race now, so the old Freeborn pick is gone for good.
    local toggle = CharacterCreateFreebornButton;
    if toggle then toggle:Hide(); toggle.Show = toggle.Hide; end
    CharacterCreate_PositionRaceButtons();
end
CharacterFreeborn_IsSelected = function() return false; end

-- The character-select emblem (and ECS) read Freeborn membership from these hashes; derive them from race.
function CharacterFreeborn_BadgeHashes()
    local hashes = {};
    for index = 1, (GetNumCharacters and GetNumCharacters()) or 0 do
        local name, race = GetCharacterInfo(index);
        if name and FREEBORN_NAMES[race] then
            hashes[tostring(CharacterFreeborn_RecordFor(name) - CharacterFreeborn_RecordBase)] = true;
        end
    end
    return hashes;
end

local function RacePage()
    return CharacterCreateRaceButtonsContainer and CharacterCreateRaceButtonsContainer:IsShown()
        and CharacterCreateFrame:IsShown() and not Personalizing();
end

-- Portraits keep their own round rim; the selection ring is the stock round border, tinted gold.
local function SkinRaceButton(button)
    if button.checkedTexture then button.checkedTexture:SetAlpha(0); end
    if button.highlightTexture then button.highlightTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
    if button.staticTexture then button.staticTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
    SK.races[button] = true;
end

-- False once the left pane has slid all the way out.
local function PaneShown()
    return (SK.slide or 0) > -PANEL_W;
end

-- The browsed faction's portraits as one grid in the left panel; the other factions' portraits are hidden. Five
-- large portraits a row, seven smaller ones once a faction has more than 30 races, so the grid stays on screen.
local function LayoutRaces()
    if not (SK.on and SK.mode == "race") then return; end
    SK.factionOf = {};
    for _, faction in ipairs(FACTIONS) do
        local indices = FactionRaces(faction.key);
        local columns = #indices > 30 and 7 or 5;
        local spacing = 340 / columns;
        local size = spacing - 8;
        local rows = 0;
        for slot, index in ipairs(indices) do
            local button = _G["CharacterCreateRaceButton" .. index];
            if button then
                if not SK.races[button] then SkinRaceButton(button); end
                SK.factionOf[button] = faction.key;
                local column, row = (slot - 1) % columns, math.floor((slot - 1) / columns);
                rows = math.max(rows, row + 1);
                button:ClearAllPoints();
                button:SetSize(size, size);
                button:SetPoint("CENTER", CharacterCreateFrame, "TOPLEFT",
                    (SK.slide or 0) + EDGE_X + spacing / 2 + column * spacing, GRID_TOP - row * spacing);
                button:GetNormalTexture():SetSize(size, size);
                button:GetPushedTexture():SetSize(size, size);
                for _, name in ipairs({ "staticTexture", "highlightTexture", "checkedTexture" }) do
                    if button[name] then button[name]:SetSize(size * 112 / 50, size * 112 / 50); end
                end
                if faction.key == SK.faction and PaneShown() then button:Show(); else button:Hide(); end
            end
        end
    end
    -- The faction rail carries the crests; the stock corner crests and captions stay out of the way.
    for _, region in ipairs({ CustomizationLogoAlliance, CustomizationLogoHorde, CustomizationTextAlliance,
                              CustomizationTextHorde }) do
        if region then region:SetAlpha(0); end
    end
    for _, hitbox in ipairs({ AllianceLogoFrame, HordeLogoFrame }) do
        if hitbox then hitbox:EnableMouse(false); end
    end
end

-- ---- faction rail ------------------------------------------------------------------------------------------
-- One crest per faction in a slim column on the screen's left edge. A crest shows that faction's races in the left
-- pane (sliding it in if hidden); the browsed faction's crest slides the pane out, leaving the scene clear.

local RAIL_TOP, RAIL_ICON, RAIL_STEP, SLIDE_TIME = -150, 40, 50, 0.18;
local UpdateFactionRail;

local function Edge(parent, point, w, h)
    local edge = Solid(parent, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 1);
    edge:SetPoint(point, parent, point, 0, 0);
    if w then edge:SetWidth(w); end
    if h then edge:SetHeight(h); end
    return edge;
end

local function SlidePane(open)
    SK.paneOpen = open;
    SK.rail.mover:Show();
end

local function BuildRailIcon(faction, slot)
    local icon = CreateFrame("Button", "CreatorSkinFaction" .. faction.key, SK.rail);
    icon:SetSize(RAIL_ICON, RAIL_ICON);
    icon:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", 6, RAIL_TOP + RAIL_ICON / 2 - (slot - 1) * RAIL_STEP);
    icon.faction = faction;
    icon.wash = Solid(icon, "BACKGROUND", faction.color[1], faction.color[2], faction.color[3], 0.16);
    icon.wash:SetAllPoints(icon);
    icon.edges = { Edge(icon, "TOPLEFT", RAIL_ICON, 1), Edge(icon, "BOTTOMLEFT", RAIL_ICON, 1),
                   Edge(icon, "TOPLEFT", 1, RAIL_ICON), Edge(icon, "TOPRIGHT", 1, RAIL_ICON) };
    -- Faction-coloured marker on the outer edge of the browsed crest.
    icon.bar = Solid(icon, "OVERLAY", faction.color[1], faction.color[2], faction.color[3], 1);
    icon.bar:SetPoint("TOPLEFT", icon, "TOPLEFT", 1, -1);
    icon.bar:SetPoint("BOTTOMLEFT", icon, "BOTTOMLEFT", 1, 1);
    icon.bar:SetWidth(2);
    local highlight = Solid(icon, "HIGHLIGHT", GOLD[1], GOLD[2], GOLD[3], 0.08);
    highlight:SetAllPoints(icon);
    icon.crest = icon:CreateTexture(nil, "ARTWORK");
    icon.crest:SetSize(RAIL_ICON - 6, RAIL_ICON - 6);
    icon.crest:SetTexture(faction.crest);
    icon.crest:SetTexCoord(unpack(faction.crop));
    icon.crest:SetPoint("CENTER", icon, "CENTER", 0, 0);
    icon:SetScript("OnClick", function(self)
        PlaySound("gsCharacterCreationClass");
        if SK.faction == self.faction.key then
            SlidePane(not SK.paneOpen);
        else
            SK.faction = self.faction.key;
            pcall(LayoutRaces);
            if not SK.paneOpen then SlidePane(true); end
        end
        pcall(UpdateFactionRail);
        self:GetScript("OnEnter")(self);
    end);
    -- Hover names the faction; the crest is all the rail shows otherwise.
    icon:SetScript("OnEnter", function(self)
        local tip = SK.rail.tip;
        local n = #FactionRaces(self.faction.key);
        local action = SK.faction ~= self.faction.key and "Show races" or SK.paneOpen and "Hide races" or "Show races";
        tip.text:SetText(self.faction.hex .. string.upper(self.faction.name) .. "|r  " .. n .. " races  \194\183  " .. action);
        tip:SetWidth(tip.text:GetStringWidth() + 20);
        tip:ClearAllPoints();
        tip:SetPoint("LEFT", self, "RIGHT", 8, 0);
        tip:Show();
    end);
    icon:SetScript("OnLeave", function() SK.rail.tip:Hide(); end);
    return icon;
end

local function BuildFactionRail()
    local rail = CreateFrame("Frame", "CreatorSkinFactionRail", SK.chrome);
    rail:SetAllPoints(SK.chrome);
    rail:SetFrameLevel(SK.chrome:GetFrameLevel() + 4);
    SK.rail = rail;
    -- A slim ink plate behind the crests, so they read the same with the pane in or out.
    local plate = Solid(rail, "BACKGROUND", INK[1], INK[2], INK[3], 0.9);
    plate:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", 0, RAIL_TOP + RAIL_ICON / 2 + 6);
    plate:SetSize(52, (#FACTIONS - 1) * RAIL_STEP + RAIL_ICON + 12);
    local seam = Solid(rail, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
    seam:SetPoint("TOPLEFT", plate, "TOPRIGHT", 0, 0);
    seam:SetPoint("BOTTOMLEFT", plate, "BOTTOMRIGHT", 0, 0);
    seam:SetWidth(1);
    for slot, faction in ipairs(FACTIONS) do BuildRailIcon(faction, slot); end

    local tip = CreateFrame("Frame", nil, rail);
    tip:SetHeight(26);
    tip:SetFrameLevel(rail:GetFrameLevel() + 2);
    local back = Solid(tip, "BACKGROUND", INK[1], INK[2], INK[3], 0.95);
    back:SetAllPoints(tip);
    Edge(tip, "TOPLEFT", nil, 1):SetPoint("TOPRIGHT", tip, "TOPRIGHT", 0, 0);
    Edge(tip, "BOTTOMLEFT", nil, 1):SetPoint("BOTTOMRIGHT", tip, "BOTTOMRIGHT", 0, 0);
    tip.text = Text(tip, HEAD_FONT, 12, GOLD);
    tip.text:SetPoint("LEFT", tip, "LEFT", 10, 0);
    tip:Hide();
    rail.tip = tip;

    -- Eases the pane between open (slide 0) and hidden (slide -PANEL_W) over SLIDE_TIME.
    local mover = CreateFrame("Frame", nil, rail);
    mover:Hide();
    mover:SetScript("OnUpdate", function(self, elapsed)
        if SK.mode ~= "race" then self:Hide(); return; end
        local target = SK.paneOpen and 0 or -PANEL_W;
        local step = PANEL_W * elapsed / SLIDE_TIME;
        local slide = SK.slide or 0;
        slide = slide < target and math.min(slide + step, target) or math.max(slide - step, target);
        SK.slide = slide;
        pcall(PlaceHeaders, slide);
        pcall(LayoutRaces);
        if slide == target then self:Hide(); end
    end);
    rail.mover = mover;
    rail:Hide();
end

function UpdateFactionRail()
    for _, faction in ipairs(FACTIONS) do
        local icon = _G["CreatorSkinFaction" .. faction.key];
        local on = faction.key == SK.faction;
        icon.wash:SetAlpha(on and SK.paneOpen and 1 or 0);
        icon.bar:SetAlpha(on and 1 or 0);
        icon.crest:SetAlpha(on and 1 or 0.6);
        for _, edge in ipairs(icon.edges) do
            if on then edge:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 1);
            else edge:SetVertexColor(GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.5); end
        end
    end
end

-- Male / Female: glue buttons whose highlight stays lit when picked. Their clicks are the stock ones.
local function SkinToggle(button, text)
    local normal, pushed = button:GetNormalTexture(), button:GetPushedTexture();
    local skin = { extras = {}, size = { button:GetWidth(), button:GetHeight() },
                   normal = normal and normal:GetTexture(), pushed = pushed and pushed:GetTexture() };
    local own = {};
    for _, getter in ipairs({ "GetNormalTexture", "GetPushedTexture", "GetHighlightTexture", "GetCheckedTexture" }) do
        local texture = button[getter] and button[getter](button);
        if texture then own[texture] = true; end
    end
    -- Everything else drawn on the button: ring borders.
    for _, region in ipairs({ button:GetRegions() }) do
        local kind = region:GetObjectType();
        if (kind == "Texture" or kind == "FontString") and not own[region] then skin.extras[#skin.extras + 1] = region; end
    end
    for _, name in ipairs({ "staticTexture", "highlightTexture", "checkedTexture" }) do
        if button[name] and not own[button[name]] then skin.extras[#skin.extras + 1] = button[name]; end
    end
    skin.label = Text(button, HEAD_FONT, 12, GOLD);
    skin.label:SetPoint("CENTER", button, "CENTER", 0, 2);
    skin.label:SetText(text);
    SK.toggles[button] = skin;
end

local function ApplyToggle(button, on)
    local skin = SK.toggles[button];
    if not skin then return; end
    for _, texture in ipairs(skin.extras) do texture:SetAlpha(on and 0 or 1); end
    if skin.state ~= on then
        skin.state = on;
        if on then
            GlueLook(button);
        elseif skin.normal then
            button:SetNormalTexture(skin.normal);
            button:SetPushedTexture(skin.pushed or skin.normal);
            button:GetNormalTexture():SetTexCoord(0, 1, 0, 1);
            button:GetPushedTexture():SetTexCoord(0, 1, 0, 1);
        end
    end
    if not on then skin.label:Hide(); button:SetSize(skin.size[1], skin.size[2]); return; end
    skin.label:Show();
    -- Glue fonts: gold normally, white when picked.
    if button:GetChecked() then skin.label:SetTextColor(1, 1, 1); else skin.label:SetTextColor(1, 0.82, 0); end
end

local function LayoutToggles()
    if not (SK.on and SK.mode == "race") then return; end
    local male, female = CharacterCreateGenderButtonMale, CharacterCreateGenderButtonFemale;
    for _, button in ipairs({ male, female }) do
        if button then
            button:SetSize(130, 38);
            button:ClearAllPoints();
            button:SetPoint("BOTTOM", CharacterCreateFrame, "BOTTOM", button == male and -66 or 66, 64);
        end
    end
end

-- The selected race: name, faction, description and racials, in the right panel whichever faction is browsed.
local function BuildCard()
    local card = CreateFrame("Frame", "CreatorSkinRaceCard", SK.chrome);
    card:SetSize(340, 220);
    card.rule = Solid(card, "BORDER", 1, 1, 1, 0.08);
    card.rule:SetHeight(1);
    card.rule:SetPoint("TOPLEFT", card, "TOPLEFT", 0, 0);
    card.rule:SetPoint("TOPRIGHT", card, "TOPRIGHT", 0, 0);
    card.name = Text(card, HEAD_FONT, 15, GOLD);
    card.name:SetPoint("TOPLEFT", card, "TOPLEFT", 0, -12);
    card.crest = card:CreateTexture(nil, "ARTWORK");
    card.crest:SetSize(18, 18);
    card.faction = Text(card, HEAD_FONT, 12, GOLD);
    card.crest:SetPoint("TOPLEFT", card.name, "BOTTOMLEFT", 0, -6);
    card.faction:SetPoint("LEFT", card.crest, "RIGHT", 6, 0);
    card.description = Text(card, BODY_FONT, 12, TEXT);
    card.description:SetPoint("TOPLEFT", card.crest, "BOTTOMLEFT", 0, -8);
    card.description:SetWidth(340);
    card.description:SetJustifyH("LEFT");
    card.traits = {};
    for i = 1, 4 do
        local icon = card:CreateTexture(nil, "ARTWORK");
        icon:SetSize(18, 18);
        local name = Text(card, BODY_FONT, 12, TEXT);
        name:SetPoint("LEFT", icon, "RIGHT", 8, 0);
        card.traits[i] = { icon = icon, name = name };
    end
    card.hint = Text(card, BODY_FONT, 11, MUTED);
    card.hint:SetText("Hover a portrait for details  \194\183  right-click it for every racial");
    SK.card = card;
end

local function UpdateCard()
    local card = SK.card;
    local index = GetSelectedRace and GetSelectedRace();
    local button = index and _G["CharacterCreateRaceButton" .. index];
    local faction = button and FactionOfRace(index);
    if not faction then card:Hide(); return; end
    card:ClearAllPoints();
    card:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPRIGHT", -EDGE_X, -104);
    local info = button.raceInfo;
    card.name:SetText(string.upper((info and info.Name) or button.name or ""));
    card.crest:SetTexture(faction.crest);
    card.crest:SetTexCoord(unpack(faction.crop));
    card.faction:SetText(faction.hex .. string.upper(faction.name) .. "|r");
    card.description:SetText(info and info.Description or "");
    local last = card.description;
    for i, trait in ipairs(card.traits) do
        local spell = info and info["Spell_" .. i];
        if spell and spell.name and spell.name ~= "" then
            trait.icon:SetTexture("Interface\\Icons\\" .. (spell.icon or "INV_Misc_QuestionMark"));
            trait.icon:ClearAllPoints();
            trait.icon:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, last == card.description and -10 or -6);
            trait.name:SetText(spell.name);
            trait.icon:Show();
            trait.name:Show();
            last = trait.icon;
        else
            trait.icon:Hide();
            trait.name:Hide();
        end
    end
    card.hint:ClearAllPoints();
    card.hint:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, -12);
    card:Show();
end

-- After an error: the race page goes back to the stock layout and selection glow.
local function RestoreRace()
    for button in pairs(SK.races) do
        if button.checkedTexture then button.checkedTexture:SetAlpha(1); end
        if button.staticTexture then button.staticTexture:Hide(); end
    end
    for button in pairs(SK.toggles) do ApplyToggle(button, false); end
    for _, region in ipairs({ CustomizationLogoAlliance, CustomizationLogoHorde, CustomizationTextAlliance,
                              CustomizationTextHorde }) do
        if region then region:SetAlpha(1); end
    end
    for _, hitbox in ipairs({ AllianceLogoFrame, HordeLogoFrame }) do
        if hitbox then hitbox:EnableMouse(true); end
    end
    for button in pairs(SK.races) do if SK.factionOf and SK.factionOf[button] then button:Show(); end end
    if SK.positionRaces then SK.positionRaces(); end
    if SK.positionGender then SK.positionGender(); end
end

-- After an error: give the rows their stock text back (their arrows keep working either way).
local function RestoreRows()
    for row, skin in pairs(SK.rows) do
        local background = _G[row:GetName() .. "Background"];
        if background then background:SetAlpha(1); end
        _G[row:GetName() .. "Text"]:SetAlpha(1);
        skin.label:Hide();
        skin.value:Hide();
    end
end

local function SetSkin(on)
    if SK.on == on then return; end
    SK.on = on;
    if SK.chrome then if on then SK.chrome:Show(); else SK.chrome:Hide(); end end
    for button in pairs(SK.buttons) do ApplyButton(button, on); end
    if not on and CharCreateRandomizeButton then
        CharCreateRandomizeButton:SetAlpha(1);
        CharCreateRandomizeButton:EnableMouse(true);
    end
    if not on then
        CharacterCreateRotateLeft:SetAlpha(1);
        CharacterCreateRotateRight:SetAlpha(1);
    end
end

local function SetSteps(active)
    local body = BodyShapePage_Active and type(GetBodyShapeNames) == "function" and EA_IS_RETAIL();
    for index, step in ipairs(SK.steps) do
        if index == active then step:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); else step:SetTextColor(MUTED[1], MUTED[2], MUTED[3]); end
    end
    -- Only show the Body step when the race has that page.
    if body then SK.steps[3]:Show(); SK.stepLines[2]:Show(); else SK.steps[3]:Hide(); SK.stepLines[2]:Hide(); end
end

local function RaceTick()
    if SK.mode ~= "race" then
        SK.mode = "race";
        -- Coming onto the race page, browse the selected race's faction; tab clicks change it after that.
        local selected = FactionOfRace(GetSelectedRace());
        SK.faction = selected and selected.key or SK.faction or FACTIONS[1].key;
        -- The pane keeps whatever the player last left it as (open by default).
        if SK.paneOpen == nil then SK.paneOpen = true; end
        SK.slide = SK.paneOpen and 0 or -PANEL_W;
        LayoutRaces();
        LayoutToggles();
    end
    for button in pairs(SK.buttons) do ApplyButton(button, true); end
    for button in pairs(SK.toggles) do ApplyToggle(button, true); end
    for button in pairs(SK.races) do
        -- Race enumeration re-tints these in faction colours every time; keep them gold.
        if button.highlightTexture then button.highlightTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
        if button.checkedTexture then button.checkedTexture:SetAlpha(0); end
        if button.staticTexture then
            button.staticTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
            if button:GetChecked() and button:IsShown() then button.staticTexture:Show(); else button.staticTexture:Hide(); end
        end
        -- The stock code re-shows portraits on enumeration; only the browsed faction's stay up.
        local browsed = SK.factionOf and SK.factionOf[button] == SK.faction and PaneShown();
        if browsed and not button:IsShown() then button:Show();
        elseif not browsed and button:IsShown() then button:Hide(); end
    end
    if not SK.rail.mover:IsShown() then PlaceHeaders(SK.slide); end
    SK.rail:Show();
    UpdateFactionRail();
    local browsing;
    for _, faction in ipairs(FACTIONS) do if faction.key == SK.faction then browsing = faction; end end
    SK.LEFT.title:SetText("CHOOSE A RACE");
    SK.LEFT.subtitle:SetText(browsing and (browsing.hex .. string.upper(browsing.name) .. "|r  \194\183  "
        .. #FactionRaces(browsing.key) .. " races") or "");
    SK.RIGHT.title:SetText("RACE DETAILS");
    SK.RIGHT.subtitle:SetText("Your current pick");
    SetSteps(1);
    UpdateCard();
end

-- Two Forms (Worgen Wolf/Human, Dracthyr Dragon/Visage): a tab per form, each with its own option set.
local function BuildFormTabs()
    SK.formTabs = {};
    for index = 0, 1 do
        local tab = CreateFrame("CheckButton", "CreatorSkinFormTab" .. index, CharacterCreateFrame);
        tab:SetSize(110, 30);
        tab:SetFrameLevel(CharacterCreateFrame:GetFrameLevel() + 5);
        tab:SetPoint("TOP", CharacterCreateFrame, "TOP", index == 0 and -120 or 0, -56);
        GlueLook(tab);
        tab.label = Text(tab, HEAD_FONT, 12, GOLD);
        tab.label:SetPoint("CENTER", tab, "CENTER", 0, 2);
        tab:SetScript("OnClick", function()
            CycleCharCustomization("EA_FORM_SET", index);
            CharacterCreate_UpdateHairCustomization();
        end);
        SK.formTabs[index] = tab;
    end
    -- Shows both forms in the viewport at once; the tabs still pick which one the rows edit.
    local both = CreateFrame("CheckButton", "CreatorSkinFormBoth", CharacterCreateFrame);
    both:SetSize(110, 30);
    both:SetFrameLevel(CharacterCreateFrame:GetFrameLevel() + 5);
    both:SetPoint("TOP", CharacterCreateFrame, "TOP", 120, -56);
    GlueLook(both);
    both.label = Text(both, HEAD_FONT, 12, GOLD);
    both.label:SetPoint("CENTER", both, "CENTER", 0, 2);
    both.label:SetText("BOTH");
    both:SetScript("OnClick", function()
        local on = CycleCharCustomization("EA_FORM_DUAL", 2) == 1;
        CycleCharCustomization("EA_FORM_DUAL", on and 0 or 1);
    end);
    SK.formBoth = both;
end

local function UpdateFormTabs(show)
    local ok, count = pcall(CycleCharCustomization, "EA_FORM_COUNT", 1);
    show = show and EA_IS_RETAIL() and ok and count == 2;
    if show and not SK.formTabs then BuildFormTabs(); end
    if not SK.formTabs then return; end
    local current = show and CycleCharCustomization("EA_FORM_GET", 1);
    for index = 0, 1 do
        local tab = SK.formTabs[index];
        if show then
            tab:Show();
            tab.label:SetText(string.upper(CycleCharCustomization("EA_FORM_NAME", index) or ""));
            tab:SetChecked(current == index and 1 or nil);
            if current == index then tab.label:SetTextColor(1, 1, 1); else tab.label:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); end
        else
            tab:Hide();
        end
    end
    local both = SK.formBoth;
    if show then
        both:Show();
        local on = CycleCharCustomization("EA_FORM_DUAL", 2) == 1;
        both:SetChecked(on and 1 or nil);
        if on then both.label:SetTextColor(1, 1, 1); else both.label:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); end
    else
        both:Hide();
    end
end

local function Tick()
    if RacePage() then
        UpdateFormTabs(false);
        SetSkin(true);
        RaceTick();
        return;
    end
    if SK.card then SK.card:Hide(); end
    if SK.rail then SK.rail:Hide(); end
    if not Personalizing() then UpdateFormTabs(false); SK.mode = nil; SetSkin(false); return; end
    SetSkin(true);
    SK.mode = "custom";
    PlaceHeaders(0);

    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row then
            if not SK.rows[row] then SkinRow(row); end
            if row:IsShown() then UpdateRow(row, SK.rows[row]); end
        end
    end
    for button in pairs(SK.buttons) do ApplyButton(button, true); end

    local body = BodyPage();
    UpdateFormTabs(not body);
    if body then
        SkinBodyPage();
        for i = 1, 128 do
            local slider = _G["BodyShapeSlider" .. i];
            if not slider then break; end
            if not SK.sliders[slider] then SkinSlider(slider); else SK.sliders[slider](); end
        end
    end

    SK.LEFT.title:SetText(body and "BODY" or "APPEARANCE");
    SK.LEFT.subtitle:SetText(body and "Drag the triangle to blend body types" or RaceLine());
    SK.RIGHT.title:SetText(body and "FACE" or "DETAILS");
    SK.RIGHT.subtitle:SetText(body and "Right-click a slider to reset it" or "Click a value for the full list");
    SetSteps(body and 3 or 2);

    -- The hint replaces the rotate arrows; the whole-character randomize sits in the details header.
    CharacterCreateRotateLeft:SetAlpha(0);
    CharacterCreateRotateRight:SetAlpha(0);
    if CharCreateRandomizeButton then
        CharCreateRandomizeButton:ClearAllPoints();
        -- Inner end of the Details header, clear of its title.
        CharCreateRandomizeButton:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPRIGHT", -(EDGE_X + 340), -58);
        if body then CharCreateRandomizeButton:SetAlpha(0); CharCreateRandomizeButton:EnableMouse(false);
        else CharCreateRandomizeButton:SetAlpha(1); CharCreateRandomizeButton:EnableMouse(true); end
    end
end

local function SK_Install()
    BuildChrome();
    SkinButton(CharCreateOkayButton);
    SkinButton(CharCreateBackButton);
    SkinNameEdit(CharacterCreateNameEdit);
    SkinNameEdit(CharacterCreateLastNameEdit);
    if CharCreatePersonalizeButton then SkinButton(CharCreatePersonalizeButton); end
    BuildCard();
    BuildFactionRail();
    for button, text in pairs({ [CharacterCreateGenderButtonMale or 0] = "MALE", [CharacterCreateGenderButtonFemale or 0] = "FEMALE" }) do
        if button ~= 0 then SkinToggle(button, text); end
    end
    -- The stock code re-lays the portraits and gender picks on enumeration and resize; keep the skin's layout.
    SK.positionRaces, SK.positionGender = CharacterCreate_PositionRaceButtons, CharacterCreate_PositionGenderButtons;
    function CharacterCreate_PositionRaceButtons(...)
        local a, b = SK.positionRaces(...);
        if not SK.broken then pcall(LayoutRaces); end
        return a, b;
    end
    function CharacterCreate_PositionGenderButtons(...)
        local a, b = SK.positionGender(...);
        if not SK.broken then pcall(LayoutToggles); end
        return a, b;
    end
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        local ok, err = pcall(Tick);
        if not ok then
            SK.broken = tostring(err);
            SK_Log("tick error: " .. SK.broken);
            pcall(SetSkin, false);
            pcall(RestoreRows);
            pcall(RestoreRace);
            self:SetScript("OnUpdate", nil);
        end
    end);
    SK_Log("installed");
end

local skinWaiter = CreateFrame("Frame");
skinWaiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton and CharacterCreateNameEdit) then return; end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(SK_Install);
    if not ok then
        SK.broken = tostring(err);
        SK_Log("install failed: " .. SK.broken);
        SK.on = true;
        pcall(SetSkin, false);
    end
end);
end)();
