-- Body sliders page (wowdecomp)
-- A second creator page after Appearance: native sliders for every body/face slider the chosen race and sex
-- supports. Flow: Appearance -> Next -> Body -> Accept. Values live in the client (SetBodyShape / GetBodyShape)
-- and show on the creator preview model.
-- Everything here is guarded: if anything fails the creator behaves exactly as before and a small note says why.
local BS = { page = false, rows = {}, built = false, broken = nil, okayText = nil };

local function BS_Log(text)
    if BodyShapeLog then BodyShapeLog("lua: " .. tostring(text)); end
end
BS_Log("page chunk reached; natives=" .. tostring(type(GetBodyShapeNames) == "function"));

local function BS_Retail()
    return type(GetBodyShapeNames) == "function" and EA_IS_RETAIL();
end

local function BS_InPersonalization()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

-- True when Accept should lead to the Body page first.
local function BS_FlowAvailable()
    return not BS.broken and BS_InPersonalization() and BS_Retail();
end

function BodyShapePage_Active()
    return BS.page and BS_FlowAvailable();
end
BodyShapePage_FlowAvailable = BS_FlowAvailable;

-- Accept's label on the Body page: Next while the Fate page still follows.
local function BS_FinalText()
    return FatePage_Follows and FatePage_Follows() and "Next" or BS.okayText;
end

local function BS_Pretty(name)
    return (name:gsub("(%l)(%u)", "%1 %2"));
end

local function BS_Status()
    -- Other executables sharing this archive have no body-slider commands: say nothing there.
    if type(GetBodyShapeNames) ~= "function" then return nil; end
    if BS.broken then return "Body sliders unavailable: " .. tostring(BS.broken); end
    if not EA_IS_RETAIL() then return "Body sliders: not available for this race (not a Retail model)"; end
    return nil;
end

-- Presets drawer: share codes and saved presets per race and sex, in a card that slides down from a tab under the
-- step indicator. Styled like the creator skin. Natives: BodyShapeExport/Import/Copy/Paste, Get/Save/DeleteBodyShapePreset.
function BS.BuildDrawer(panel)
    local ES = EsteriaStyle;
    local GOLD, GOLD_DIM, INK, TEXT, MUTED, GOOD, BAD = ES.GOLD, ES.GOLD_DIM, ES.INK, ES.TEXT, ES.MUTED, ES.GOOD, ES.BAD;
    local FONT = ES.FONT;
    local W, PAD, ROWS, ROW_H, ROWS_TOP = 400, 18, 5, 24, -140;
    local OPEN_TIME, CLOSE_TIME, FADE, FLASH, TOAST = 0.32, 0.18, 0.16, 0.6, 2.5;
    local D = { open = false, t = 0, presets = {}, scroll = 0, parts = {}, flashes = {} };

    local Solid, Text, Border, Tint, Ease = ES.Solid, ES.Text, ES.Border, ES.Tint, ES.Ease;
    -- Sections fade in one after another while the card opens.
    local function Stage(object, delay) D.parts[#D.parts + 1] = { object = object, delay = delay }; return object; end

    -- ---- tab -------------------------------------------------------------------------------------------------
    local tab = CreateFrame("Button", "BodyShapeDrawerTab", panel);
    tab:SetSize(132, 24);
    tab:SetPoint("TOP", CharacterCreateFrame, "TOP", 0, -50);
    tab:SetFrameLevel(panel:GetFrameLevel() + 12);
    Solid(tab, "BACKGROUND", INK, 0.9):SetAllPoints(tab);
    local tabEdges = Border(tab, GOLD_DIM, 0.9);
    Solid(tab, "HIGHLIGHT", GOLD, 0.08):SetAllPoints(tab);
    local tabLabel = Text(tab, 12, GOLD);
    tabLabel:SetText("PRESETS");
    tabLabel:SetPoint("CENTER", tab, "CENTER", -8, 0);
    local tabArrow = tab:CreateTexture(nil, "ARTWORK");
    tabArrow:SetTexture("Interface\\MoneyFrame\\Arrow-Right-Up");
    tabArrow:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
    tabArrow:SetSize(12, 12);
    tabArrow:SetPoint("LEFT", tabLabel, "RIGHT", 6, 0);

    -- ---- card ------------------------------------------------------------------------------------------------
    local card = CreateFrame("Frame", "BodyShapeDrawer", panel);
    card:SetSize(W, 326);
    card:SetFrameLevel(panel:GetFrameLevel() + 10);
    card:EnableMouse(true);
    card:EnableMouseWheel(true);
    card:Hide();
    Solid(card, "BACKGROUND", INK, 0.94):SetAllPoints(card);
    Border(card, GOLD_DIM, 0.9);
    local accent = Solid(card, "ARTWORK", GOLD, 1);
    accent:SetHeight(2);
    accent:SetPoint("TOP", card, "TOP", 0, 0);

    local function Caption(text, y, delay)
        local fs = Stage(Text(card, 11, MUTED), delay);
        fs:SetPoint("TOPLEFT", card, "TOPLEFT", PAD, y);
        fs:SetText(text);
        return fs;
    end

    local function Field(name, width, letters)
        local edit = CreateFrame("EditBox", name, card);
        edit:SetSize(width, 26);
        edit:SetFontObject(GlueFontHighlightSmall);
        edit:SetFont(FONT, 12);
        edit:SetTextColor(TEXT[1], TEXT[2], TEXT[3]);
        edit:SetTextInsets(8, 8, 0, 0);
        edit:SetAutoFocus(false);
        edit:SetMaxLetters(letters);
        Solid(edit, "BACKGROUND", { 0, 0, 0 }, 0.45):SetAllPoints(edit);
        local edges = Border(edit, GOLD_DIM, 0.35);
        local line = Solid(edit, "ARTWORK", GOLD, 0.55);
        line:SetHeight(1);
        line:SetPoint("BOTTOMLEFT", edit, "BOTTOMLEFT", 0, 0);
        line:SetPoint("BOTTOMRIGHT", edit, "BOTTOMRIGHT", 0, 0);
        edit:SetScript("OnEscapePressed", function(self) self:ClearFocus(); end);
        edit:SetScript("OnEnterPressed", function(self) self:ClearFocus(); end);
        edit:SetScript("OnEditFocusGained", function(self)
            self.focused = true; self:HighlightText(); Tint(edges, GOLD, 0.6);
        end);
        edit:SetScript("OnEditFocusLost", function(self)
            self.focused = false; self:HighlightText(0, 0); Tint(edges, GOLD_DIM, 0.35);
        end);
        edit.line = line;
        return edit;
    end

    local function Flat(name, text, width)
        local button = CreateFrame("Button", name, card);
        button:SetSize(width, 24);
        Solid(button, "BACKGROUND", { 1, 1, 1 }, 0.04):SetAllPoints(button);
        local edges = Border(button, GOLD_DIM, 0.9);
        Solid(button, "HIGHLIGHT", GOLD, 0.12):SetAllPoints(button);
        local label = Text(button, 12, GOLD);
        label:SetPoint("CENTER", button, "CENTER", 0, 0);
        label:SetText(text);
        button.label = label;
        button:SetScript("OnEnter", function() Tint(edges, GOLD, 1); end);
        button:SetScript("OnLeave", function() Tint(edges, GOLD_DIM, 0.9); end);
        button:SetScript("OnMouseDown", function() label:ClearAllPoints(); label:SetPoint("CENTER", button, "CENTER", 1, -1); end);
        button:SetScript("OnMouseUp", function() label:ClearAllPoints(); label:SetPoint("CENTER", button, "CENTER", 0, 0); end);
        return button;
    end

    -- Header.
    local title = Stage(Text(card, 14, GOLD), 0);
    title:SetPoint("TOPLEFT", card, "TOPLEFT", PAD, -16);
    title:SetText("PRESETS & SHARING");
    local who = Stage(Text(card, 12, MUTED), 0);
    who:SetPoint("TOPRIGHT", card, "TOPRIGHT", -PAD, -18);
    local rule = Stage(Solid(card, "BORDER", GOLD_DIM, 0.6), 0);
    rule:SetHeight(1);
    rule:SetPoint("TOPLEFT", card, "TOPLEFT", PAD, -40);
    rule:SetPoint("TOPRIGHT", card, "TOPRIGHT", -PAD, -40);

    -- Status line at the bottom; fades after a moment.
    local status = Text(card, 12, GOOD);
    status:SetPoint("BOTTOM", card, "BOTTOM", 0, 12);
    local function Say(text, good)
        local color = good and GOOD or BAD;
        status:SetText(text);
        status:SetTextColor(color[1], color[2], color[3]);
        D.toast = GetTime();
    end

    local function Key()
        return tostring(CharacterCreate.selectedRaceID or GetSelectedRace()) .. ":" .. tostring(GetSelectedSex());
    end
    local function Who()
        local race = GetNameForRace and GetNameForRace() or "";
        return race .. (race ~= "" and ", " or "") .. (GetSelectedSex() == SEX_MALE and "male" or "female");
    end

    local function Import(text)
        if not BodyShapeImport(text or "") then
            PlaySound("gsCharacterCreationCancel");
            Say("That is not a valid body code");
            return false;
        end
        PlaySound("gsCharacterCreationLook");
        if BS.ResetTriangle then BS.ResetTriangle(); end
        return true;
    end

    -- Share code: shows the current sliders; Copy puts it on the clipboard, Paste applies the clipboard's code.
    Caption("SHARE CODE", -54, 0.05);
    local code = Stage(Field("BodyShapeCodeBox", W - 2 * PAD, 120), 0.05);
    code:SetPoint("TOPLEFT", card, "TOPLEFT", PAD, -72);
    code:SetScript("OnEnterPressed", function(self)
        if Import(self:GetText()) then Say("Body code applied", true); D.flashes[code] = GetTime(); end
        self:ClearFocus();
    end);
    local paste = Stage(Flat("BodyShapePasteButton", "PASTE", 64), 0.05);
    paste:SetPoint("BOTTOMRIGHT", code, "TOPRIGHT", 0, 4);
    paste:SetScript("OnClick", function()
        local text = BodyShapePaste();
        if not text then Say("The clipboard is empty"); return; end
        if Import(text) then
            code:SetText(BodyShapeExport());
            code:SetCursorPosition(0);
            Say("Code from clipboard applied", true);
            D.flashes[code] = GetTime();
        end
    end);
    local copy = Stage(Flat("BodyShapeCopyButton", "COPY", 64), 0.05);
    copy:SetPoint("RIGHT", paste, "LEFT", -6, 0);
    copy:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationLook");
        if BodyShapeCopy(BodyShapeExport()) then
            Say("Code copied to the clipboard", true);
            D.flashes[code] = GetTime();
        else
            Say("Could not reach the clipboard");
        end
    end);

    -- Saved presets for this race and sex. Click a row to load it; X twice deletes it; the wheel scrolls.
    Caption("SAVED LOOKS", -118, 0.10);
    local count = Stage(Text(card, 11, MUTED), 0.10);
    count:SetPoint("TOPRIGHT", card, "TOPRIGHT", -PAD, -118);
    local empty = Stage(Text(card, 12, MUTED), 0.10);
    empty:SetPoint("TOP", card, "TOP", 0, ROWS_TOP - ROW_H * 2 + 4);
    empty:SetText("Nothing saved for this race yet. Name your look below.");

    local rows, Refresh = {};
    local function Load(row)
        local preset = D.presets[row.index];
        if preset and Import(preset.code) then
            Say("Loaded \"" .. preset.name .. "\"", true);
            D.flashes[row] = GetTime();
        end
    end
    for i = 1, ROWS do
        local row = Stage(CreateFrame("Button", nil, card), 0.10 + i * 0.012);
        row:SetHeight(ROW_H);
        row:SetPoint("TOPLEFT", card, "TOPLEFT", PAD, ROWS_TOP - (i - 1) * ROW_H);
        row:SetPoint("TOPRIGHT", card, "TOPRIGHT", -PAD, ROWS_TOP - (i - 1) * ROW_H);
        local hairline = Solid(row, "BACKGROUND", { 1, 1, 1 }, 0.07);
        hairline:SetHeight(1);
        hairline:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0);
        hairline:SetPoint("TOPRIGHT", row, "TOPRIGHT", 0, 0);
        row.wash = Solid(row, "BACKGROUND", GOLD, 0);
        row.wash:SetAllPoints(row);
        Solid(row, "HIGHLIGHT", GOLD, 0.10):SetAllPoints(row);
        row.accent = Solid(row, "ARTWORK", GOLD, 1);
        row.accent:SetWidth(2);
        row.accent:SetPoint("TOPLEFT", row, "TOPLEFT", 0, -1);
        row.accent:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 0, 1);
        row.accent:Hide();
        row.name = Text(row, 13, TEXT);
        row.name:SetPoint("LEFT", row, "LEFT", 12, 0);
        row.name:SetPoint("RIGHT", row, "RIGHT", -150, 0);
        row.name:SetJustifyH("LEFT");
        row.hint = Text(row, 11, MUTED);
        row.hint:SetPoint("RIGHT", row, "RIGHT", -30, 0);
        row.hint:Hide();
        local delete = CreateFrame("Button", nil, row);
        delete:SetSize(22, 22);
        delete:SetPoint("RIGHT", row, "RIGHT", -2, 0);
        delete.label = Text(delete, 12, MUTED);
        delete.label:SetPoint("CENTER", delete, "CENTER", 0, 0);
        delete.label:SetText("X");
        local function Hover(on)
            if on then row.accent:Show(); row.hint:Show(); else row.accent:Hide(); if not row.armed then row.hint:Hide(); end end
        end
        row:SetScript("OnEnter", function() Hover(true); end);
        row:SetScript("OnLeave", function() Hover(false); end);
        row:SetScript("OnClick", Load);
        delete:SetScript("OnEnter", function() delete.label:SetTextColor(BAD[1], BAD[2], BAD[3]); Hover(true); end);
        delete:SetScript("OnLeave", function() delete.label:SetTextColor(MUTED[1], MUTED[2], MUTED[3]); Hover(false); end);
        delete:SetScript("OnClick", function()
            local preset = D.presets[row.index];
            if not preset then return; end
            if not row.armed or GetTime() - row.armed > 3 then
                row.armed = GetTime();
                row.hint:SetText("Click X again to delete");
                row.hint:SetTextColor(BAD[1], BAD[2], BAD[3]);
                row.hint:Show();
                return;
            end
            PlaySound("gsCharacterCreationCancel");
            DeleteBodyShapePreset(Key(), preset.name);
            Refresh();
            Say("Deleted \"" .. preset.name .. "\"", true);
        end);
        rows[i] = row;
    end

    local function Draw()
        local total = #D.presets;
        D.scroll = math.max(0, math.min(D.scroll, total - ROWS));
        for i, row in ipairs(rows) do
            local index = D.scroll + i;
            local preset = D.presets[index];
            row.index, row.armed = index, nil;
            row.hint:SetText("Click to load");
            row.hint:SetTextColor(MUTED[1], MUTED[2], MUTED[3]);
            row.hint:Hide();
            row.accent:Hide();
            if preset then row.name:SetText(preset.name); row:Show(); else row:Hide(); end
        end
        if total == 0 then empty:Show(); else empty:Hide(); end
        if total > ROWS then
            count:SetText((D.scroll + 1) .. "-" .. math.min(D.scroll + ROWS, total) .. " of " .. total .. "  \194\183  scroll");
        else
            count:SetText(total == 1 and "1 look" or total .. " looks");
        end
    end

    -- Re-reads this race and sex's presets; `select` scrolls to and flashes the named one.
    function Refresh(select)
        D.key, D.presets = Key(), {};
        local list = { GetBodyShapePresets(D.key) };
        for i = 1, #list, 2 do
            D.presets[#D.presets + 1] = { name = list[i], code = list[i + 1] };
            if list[i] == select then D.scroll = math.max(0, #D.presets - ROWS); end
        end
        Draw();
        for _, row in ipairs(rows) do
            if select and D.presets[row.index] and D.presets[row.index].name == select then D.flashes[row] = GetTime(); end
        end
    end
    card:SetScript("OnMouseWheel", function(_, delta) D.scroll = D.scroll - delta; Draw(); end);

    -- Name and save; typing an existing name turns Save into Update.
    local name = Stage(Field("BodyShapePresetName", W - 2 * PAD - 94, 32), 0.16);
    name:SetPoint("TOPLEFT", card, "TOPLEFT", PAD, ROWS_TOP - ROWS * ROW_H - 16);
    local save = Stage(Flat("BodyShapePresetSave", "SAVE", 88), 0.16);
    save:SetPoint("LEFT", name, "RIGHT", 6, 0);
    local function Typed() return ((name:GetText() or ""):gsub("^%s+", ""):gsub("%s+$", "")); end
    local function Save()
        local label = Typed();
        if label == "" then label = "Look " .. (#D.presets + 1); end
        if not SaveBodyShapePreset(Key(), label, BodyShapeExport()) then
            Say("Could not write WTF\\BodyShapePresets.txt");
            return;
        end
        PlaySound("gsCharacterCreationLook");
        name:SetText("");
        name:ClearFocus();
        Refresh(label);
        Say("Saved \"" .. label .. "\"", true);
    end
    save:SetScript("OnClick", Save);
    name:SetScript("OnEnterPressed", Save);
    local nameHint = Text(name, 12, MUTED);
    nameHint:SetPoint("LEFT", name, "LEFT", 8, 0);
    nameHint:SetText("Name this look...");

    -- ---- motion ----------------------------------------------------------------------------------------------
    local DOWN, UP = { 0, 1, 1, 1, 0, 0, 1, 0 }, { 1, 0, 0, 0, 1, 1, 0, 1 }; -- the right arrow turned 90 degrees
    local function Place()
        local e = Ease(D.t);
        tabArrow:SetTexCoord(unpack(e > 0.5 and UP or DOWN));
        Tint(tabEdges, e > 0.5 and GOLD or GOLD_DIM, 0.9);
        if D.t <= 0 then card:Hide(); return; end
        card:Show();
        card:ClearAllPoints();
        card:SetPoint("TOP", tab, "BOTTOM", 0, -6 + 24 * (1 - e));
        card:SetAlpha(e);
        accent:SetWidth(math.max(1, W * e));
        for _, part in ipairs(D.parts) do
            part.object:SetAlpha(D.open and Ease(math.max(0, math.min(1, (D.t * OPEN_TIME - part.delay) / FADE))) or 1);
        end
    end
    local function Toggle(open)
        if D.open == open then return; end
        D.open = open;
        PlaySound(open and "gsCharacterCreationLook" or "gsCharacterCreationCancel");
        if open then
            Refresh();
            code:SetText(BodyShapeExport());
            code:SetCursorPosition(0);
        else
            code:ClearFocus();
            name:ClearFocus();
        end
    end
    tab:SetScript("OnClick", function() Toggle(not D.open); end);

    local animator = CreateFrame("Frame", nil, panel);
    animator:SetScript("OnUpdate", function(_, elapsed)
        local target = D.open and 1 or 0;
        if D.t ~= target then
            local step = elapsed / (D.open and OPEN_TIME or CLOSE_TIME);
            D.t = D.open and math.min(1, D.t + step) or math.max(0, D.t - step);
            Place();
        end
        local now = GetTime();
        for object, at in pairs(D.flashes) do
            local left = math.max(0, 1 - (now - at) / FLASH);
            if left <= 0 then D.flashes[object] = nil; end
            if object == code then object.line:SetVertexColor(GOLD[1] + (1 - GOLD[1]) * left, GOLD[2] + (1 - GOLD[2]) * left,
                GOLD[3] + (0.8 - GOLD[3]) * left, 0.55 + 0.45 * left);
            else object.wash:SetAlpha(0.35 * left); end
        end
        local shown = D.toast and math.max(0, math.min(1, (TOAST - (now - D.toast)) / 0.5)) or 0;
        status:SetAlpha(shown);
        if D.toast and shown <= 0 then D.toast = nil; end
    end);
    Place();

    -- Escape closes the drawer before it backs out of the page.
    local keyDown = CharacterCreate_OnKeyDown;
    function CharacterCreate_OnKeyDown(key, ...)
        if key == "ESCAPE" and D.open and BS.page then Toggle(false); return; end
        return keyDown(key, ...);
    end

    -- From the page tick: keep the code current and the list on the selected race and sex.
    function BS.SyncShare()
        if D.open and not code.focused then
            local current = BodyShapeExport();
            if code:GetText() ~= current then code:SetText(current); code:SetCursorPosition(0); end
        end
        if D.key ~= Key() then D.scroll = 0; Refresh(); end
        who:SetText(Who());
        local typed = Typed();
        local exists = false;
        for _, preset in ipairs(D.presets) do if preset.name == typed then exists = true; end end
        save.label:SetText(exists and "UPDATE" or "SAVE");
        if typed == "" and not name.focused then nameHint:Show(); else nameHint:Hide(); end
        for _, row in ipairs(rows) do
            if row.armed and GetTime() - row.armed > 3 then
                row.armed = nil;
                row.hint:SetText("Click to load");
                row.hint:SetTextColor(MUTED[1], MUTED[2], MUTED[3]);
                if not row.accent:IsShown() then row.hint:Hide(); end
            end
        end
    end
end

local function BS_Install()
    local BACKDROP = {
        bgFile = "Interface\\Glues\\Common\\Glue-Tooltip-Background",
        edgeFile = "Interface\\Glues\\Common\\Glue-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 5, right = 5, top = 5, bottom = 5 },
    };

    local panel = CreateFrame("Frame", "BodyShapePanel", CharacterCreateFrame);
    panel:SetAllPoints(CharacterCreateFrame);
    panel:Hide();

    local note = CharacterCreateFrame:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
    note:SetPoint("BOTTOMLEFT", CharacterCreateFrame, "BOTTOMLEFT", 20, 8);
    note:SetTextColor(1, 0.6, 0.4);
    note:Hide();

    local function MakeButton(name, text, width, x, y)
        local button = CreateFrame("Button", name, panel);
        button:SetSize(width, 26);
        button:SetPoint("TOP", CharacterCreateFrame, "TOP", x, y or -64);
        button:SetNormalFontObject(GlueFontNormalSmall);
        button:SetHighlightFontObject(GlueFontHighlightSmall);
        button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight");
        button:SetBackdrop(BACKDROP);
        button:SetText(text);
        return button;
    end

    local buttonRandom = MakeButton("BodyShapeRandomButton", "Randomize Body", 140, -80);
    local buttonReset = MakeButton("BodyShapeResetButton", "Reset Body", 110, 60);
    buttonRandom:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationLook");
        BodyShapeRandomize();
        if BS.ResetTriangle then BS.ResetTriangle(); end
    end);
    buttonReset:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationCancel");
        ResetBodyShape();
        if BS.ResetTriangle then BS.ResetTriangle(); end
    end);

    if BodyShapeExport and BodyShapeCopy then BS.BuildDrawer(panel); end

    local columnTitle = {};
    for _, side in ipairs({ "Body", "Face" }) do
        local title = panel:CreateFontString(nil, "ARTWORK", "GlueFontNormal");
        title:SetText(side);
        columnTitle[side] = title;
    end

    -- Body-type triangle: Muscular (top), Large (bottom left), Thin (bottom right). The knob's
    -- barycentric weights blend three presets over a few body sliders. Its contribution is added on
    -- top of the sliders, so manual fine-tuning survives moving the knob.
    local TRI_W, TRI_H, TRI_SPACE = 180, 156, 200;
    local TRI_PRESETS = {
        { ShoulderWidth = 0.8, ArmGirth = 0.8, ChestSize = 0.7, WaistWidth = -0.2, HipWidth = -0.1, LegGirth = 0.4 },
        { ShoulderWidth = 0.2, ArmGirth = 0.4, ChestSize = 0.5, WaistWidth = 0.9, HipWidth = 0.7, LegGirth = 0.7 },
        { ShoulderWidth = -0.2, ArmGirth = -0.4, ChestSize = -0.3, WaistWidth = -0.35, HipWidth = -0.25, LegGirth = -0.35 },
    };
    local CORNERS = { { TRI_W / 2, TRI_H }, { 0, 0 }, { TRI_W, 0 } };
    BS.tri = { w = { 1 / 3, 1 / 3, 1 / 3 }, applied = {} };

    local tri = CreateFrame("Frame", "BodyShapeTriangle", panel);
    tri:SetSize(TRI_W, TRI_H);
    tri:EnableMouse(true);
    local STRIP = 3;
    for y = 0, TRI_H - STRIP, STRIP do
        local width = TRI_W * (1 - (y + STRIP / 2) / TRI_H);
        local edge = tri:CreateTexture(nil, "BACKGROUND");
        edge:SetTexture(0.75, 0.85, 0.8, 0.55);
        edge:SetSize(width + 4, STRIP);
        edge:SetPoint("BOTTOM", tri, "BOTTOMLEFT", TRI_W / 2, y);
        if width > 6 and y > 1 then
            local fill = tri:CreateTexture(nil, "BORDER");
            fill:SetTexture(0.04, 0.07, 0.09, 0.85);
            fill:SetSize(width - 4, STRIP);
            fill:SetPoint("BOTTOM", tri, "BOTTOMLEFT", TRI_W / 2, y);
        end
    end
    local function CornerLabel(text, point, x, y)
        local label = tri:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall");
        label:SetText(text);
        label:SetPoint(point, tri, "BOTTOMLEFT", x, y);
    end
    CornerLabel("Muscular", "BOTTOM", TRI_W / 2, TRI_H + 4);
    CornerLabel("Large", "TOPLEFT", -6, -4);
    CornerLabel("Thin", "TOPRIGHT", TRI_W + 6, -4);

    local knob = tri:CreateTexture(nil, "OVERLAY");
    knob:SetTexture("Interface\\Buttons\\UI-RadioButton");
    knob:SetTexCoord(0.25, 0.5, 0, 1);
    knob:SetSize(18, 18);

    local function PlaceKnob()
        local w = BS.tri.w;
        local x = w[1] * CORNERS[1][1] + w[2] * CORNERS[2][1] + w[3] * CORNERS[3][1];
        local y = w[1] * CORNERS[1][2] + w[2] * CORNERS[2][2] + w[3] * CORNERS[3][2];
        knob:ClearAllPoints();
        knob:SetPoint("CENTER", tri, "BOTTOMLEFT", x, y);
    end

    -- Weights for a local point, clamped onto the triangle.
    local function Weights(x, y)
        local ax, ay = CORNERS[1][1], CORNERS[1][2];
        local bx, by = CORNERS[2][1], CORNERS[2][2];
        local cx, cy = CORNERS[3][1], CORNERS[3][2];
        local d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        local a = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / d;
        local b = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / d;
        local w = { math.max(a, 0), math.max(b, 0), math.max(1 - a - b, 0) };
        local sum = w[1] + w[2] + w[3];
        return { w[1] / sum, w[2] / sum, w[3] / sum };
    end

    local function ApplyTriangle()
        local w = BS.tri.w;
        local wanted = {};
        -- Each corner's preset fades in from the centre (0) to its corner (1), so a corner shows
        -- exactly its own preset rather than also subtracting the other two.
        for k = 1, 3 do
            local share = math.max(0, (w[k] - 1 / 3) * 1.5);
            for name, amount in pairs(TRI_PRESETS[k]) do
                wanted[name] = (wanted[name] or 0) + share * amount;
            end
        end
        for name, value in pairs(wanted) do
            if GetBodyShapeSupport(name) ~= false then
                local lo, hi = GetBodyShapeRange(name);
                local base = GetBodyShape(name) - (BS.tri.applied[name] or 0);
                local target = math.max(lo or -1, math.min(hi or 1, base + value));
                SetBodyShape(name, target);
                BS.tri.applied[name] = target - base;
            end
        end
        PlaceKnob();
    end

    -- Back to the centre without touching the sliders (used after Reset/Randomize/new character).
    function BS.ResetTriangle()
        BS.tri.w = { 1 / 3, 1 / 3, 1 / 3 };
        BS.tri.applied = {};
        PlaceKnob();
    end

    local function TrackCursor()
        local scale = tri:GetEffectiveScale();
        local cx, cy = GetCursorPosition();
        BS.tri.w = Weights(cx / scale - tri:GetLeft(), cy / scale - tri:GetBottom());
        ApplyTriangle();
    end
    tri:SetScript("OnMouseDown", function(self, button)
        if button == "RightButton" then
            BS.tri.w = { 1 / 3, 1 / 3, 1 / 3 };
            ApplyTriangle();
            return;
        end
        self.dragging = true;
        TrackCursor();
    end);
    tri:SetScript("OnMouseUp", function(self) self.dragging = false; end);
    tri:SetScript("OnHide", function(self) self.dragging = false; end);
    tri:SetScript("OnUpdate", function(self)
        if self.dragging then TrackCursor(); end
    end);
    BS.triangle = tri;
    PlaceKnob();

    local function BuildRows()
        local index = 0;
        for _, group in ipairs({ "body", "face" }) do
            for _, name in ipairs({ GetBodyShapeNames(group) }) do
                index = index + 1;
                local row = CreateFrame("Frame", nil, panel);
                row:SetSize(290, 22);
                row.name, row.group = name, group;

                local label = row:CreateFontString(nil, "ARTWORK", "GlueFontHighlightSmall");
                label:SetPoint("LEFT", 0, 0);
                label:SetWidth(100);
                label:SetJustifyH("LEFT");
                label:SetText(BS_Pretty(name));

                local value = row:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall");
                value:SetPoint("RIGHT", 0, 0);
                value:SetWidth(52);
                value:SetJustifyH("RIGHT");

                local slider = CreateFrame("Slider", "BodyShapeSlider" .. index, row);
                slider:SetOrientation("HORIZONTAL");
                slider:SetSize(135, 14);
                slider:SetPoint("LEFT", 104, 0);
                slider:SetBackdrop({
                    bgFile = "Interface\\Buttons\\UI-SliderBar-Background",
                    edgeFile = "Interface\\Buttons\\UI-SliderBar-Border",
                    tile = true, tileSize = 8, edgeSize = 8,
                    insets = { left = 3, right = 3, top = 6, bottom = 6 },
                });
                slider:SetThumbTexture("Interface\\Buttons\\UI-SliderBar-Button-Horizontal");
                local lo, hi = GetBodyShapeRange(name);
                slider:SetMinMaxValues(lo or -1, hi or 1);
                slider:SetValueStep(0.05);
                -- The client only places the knob once a value has been set; Sync skips unchanged values.
                slider:SetValue(GetBodyShape(name) or 0);
                slider:EnableMouse(true);
                slider:SetScript("OnValueChanged", function(self, v)
                    v = math.floor(v * 20 + 0.5) / 20;
                    value:SetText(string.format("%d%%", math.floor(v * 100 + 0.5)));
                    if not self.syncing then SetBodyShape(name, v); end
                end);
                -- Right click puts a slider back to its default.
                slider:SetScript("OnMouseUp", function(self, button)
                    if button == "RightButton" then self:SetValue(0); end
                end);

                row.slider, row.value = slider, value;
                BS.rows[#BS.rows + 1] = row;
            end
        end
    end

    local function Sync(row)
        local v = GetBodyShape(row.name);
        if math.abs(row.slider:GetValue() - v) > 0.001 then
            row.slider.syncing = true;
            row.slider:SetValue(v);
            row.slider.syncing = false;
        end
        row.value:SetText(string.format("%d%%", math.floor(v * 100 + 0.5)));
    end

    local function Layout()
        local counts = { body = 0, face = 0 };
        local shown = {};
        for _, row in ipairs(BS.rows) do
            if GetBodyShapeSupport(row.name) == false then
                row:Hide();
            else
                counts[row.group] = counts[row.group] + 1;
                shown[#shown + 1] = row;
            end
        end
        local triShown = false;
        for name in pairs({ ShoulderWidth = 1, ArmGirth = 1, ChestSize = 1, WaistWidth = 1, HipWidth = 1, LegGirth = 1 }) do
            if GetBodyShapeSupport(name) ~= false then triShown = true; end
        end
        BS.triangle:ClearAllPoints();
        BS.triangle:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", 105, -160);
        if triShown then BS.triangle:Show(); else BS.triangle:Hide(); end
        local bodyTop = triShown and 200 or 0;
        local tallest = math.max(counts.body, counts.face, 1);
        local spacing = math.min(34, math.max(22, math.floor((GetScreenHeight() - 240) / tallest)));
        local bodySpacing = math.min(spacing, math.max(20, math.floor((GetScreenHeight() - 240 - bodyTop) / math.max(counts.body, 1))));
        local placed = { body = 0, face = 0 };
        for _, row in ipairs(shown) do
            local right = row.group == "face";
            local n = placed[row.group];
            placed[row.group] = n + 1;
            row:ClearAllPoints();
            row:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -140 - (right and 0 or bodyTop) - n * (right and spacing or bodySpacing));
            Sync(row);
            row:Show();
        end
        for side, group in pairs({ Body = "body", Face = "face" }) do
            local title = columnTitle[side];
            local right = group == "face";
            title:ClearAllPoints();
            title:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -116);
            if counts[group] > 0 then title:Show(); else title:Hide(); end
        end
    end

    local function SetPage(body)
        if BS.page == body then return; end
        BS.page = body;
        PlaySound("gsCharacterCreationLook");
        if CharacterCustomization_CloseChoices then CharacterCustomization_CloseChoices(); end
    end

    -- Accept becomes Next on the Appearance page; Back returns from the Body page before leaving customization.
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if BS_FlowAvailable() and not BS.page then
            SetPage(true);
            return;
        end
        return okay(...);
    end
    local back = CharacterCreate_Back;
    function CharacterCreate_Back(...)
        if BS.page then
            SetPage(false);
            return;
        end
        return back(...);
    end
    local toggle = CharacterCreate_TogglePersonalization;
    function CharacterCreate_TogglePersonalization(...)
        if BS.page then
            SetPage(false);
            return;
        end
        return toggle(...);
    end
    -- A new character starts with default sliders.
    local onShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        if ResetBodyShape then ResetBodyShape(); end
        if BS.ResetTriangle then BS.ResetTriangle(); end
        BS.page = false;
        local a, b = onShow(...);
        -- Customizing an existing character: start from its saved sliders (the stock handler just selected it).
        if PAID_SERVICE_TYPE and BodyShapeLoadExisting then BodyShapeLoadExisting(); end
        return a, b;
    end

    local function Tick(self)

        if CharCreateOkayButton and not BS.okayText then BS.okayText = CharCreateOkayButton:GetText(); end

        local status = BS_InPersonalization() and BS_Status() or nil;
        if status then note:SetText(status); note:Show(); else note:Hide(); end

        -- The Fate page owns the screen (and Accept's label) while it is open.
        if FatePage_Open and FatePage_Open() then
            panel:Hide();
            return;
        end

        if not BS_FlowAvailable() then
            BS.page = false;
            panel:Hide();
            local text = BS_FinalText();
            if text and CharCreateOkayButton:GetText() ~= text then CharCreateOkayButton:SetText(text); end
            return;
        end

        if not BS.built then
            local ok, err = pcall(BuildRows);
            if not ok then
                BS.broken = tostring(err);
                BS.page = false;
                return;
            end
            BS.built = true;
        end

        if BS.page then
            local ok, err = pcall(Layout);
            if ok and BS.SyncShare then ok, err = pcall(BS.SyncShare); end
            if not ok then BS.broken = tostring(err); BS.page = false; return; end
            panel:Show();
            local text = BS_FinalText();
            if text and CharCreateOkayButton:GetText() ~= text then CharCreateOkayButton:SetText(text); end
        else
            panel:Hide();
            if CharCreateOkayButton:GetText() ~= "Next" then CharCreateOkayButton:SetText("Next"); end
        end
    end

    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        self.logAt = (self.logAt or 0) + 1;
        if self.logAt >= 30 then
            self.logAt = 0;
            if BS_InPersonalization() then
                BS_Log("state natives=" .. tostring(type(GetBodyShapeNames) == "function")
                    .. " retail=" .. tostring(EA_IS_RETAIL()) .. " flow=" .. tostring(BS_FlowAvailable())
                    .. " page=" .. tostring(BS.page) .. " built=" .. tostring(BS.built)
                    .. " broken=" .. tostring(BS.broken) .. " okayText=" .. tostring(CharCreateOkayButton:GetText()));
            end
        end
        local ok, err = pcall(Tick, self);
        if not ok then
            BS.broken = tostring(err);
            BS.page = false;
            BS_Log("tick error: " .. BS.broken);
        end
    end);
    BS_Log("page installed");
end

-- The creator's frames do not exist yet while this file loads, so build the page on the first frame that has them.
local installWaiter = CreateFrame("Frame");
installWaiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton) then return; end
    self:SetScript("OnUpdate", nil);
    local installed, installError = pcall(BS_Install);
    if not installed then
        BS.broken = tostring(installError);
        BS_Log("install failed: " .. BS.broken);
        pcall(function()
            local note = CharacterCreateFrame:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
            note:SetPoint("BOTTOMLEFT", CharacterCreateFrame, "BOTTOMLEFT", 20, 8);
            note:SetTextColor(1, 0.4, 0.4);
            note:SetText("Body page failed to load: " .. BS.broken);
        end);
    end
end);
