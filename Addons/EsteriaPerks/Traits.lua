-- Starting traits in game: what the character holds, and the Trait Rechoose dialog. The server holds the build
-- (EsteriaPerks_GetTraits, Extensions/EsteriaPerks); this shows it and asks for a whole new one, never a piece.
-- The picker and the catalog are the character creator's (fate_picker.lua, fate_traits.lua; build_shell_mpq.py
-- copies them into this addon). Trait points are not perk points: nothing here touches the Codex's cards.

EsteriaPerksTraits = {};
local X, ES, P = EsteriaPerksTraits, EsteriaStyle, EsteriaFatePicker;
local GOLD, GOLD_DIM, TEXT, MUTED, GOOD, BAD = ES.GOLD, ES.GOLD_DIM, ES.TEXT, ES.MUTED, ES.GOOD, ES.BAD;
local LOCK_PLACE, LOCK_GOLD = 11, 12;

-- EsteriaTraits::Result (the server's PerkTraitRules.h), as a sentence for the player.
local RESULTS = {
    [0] = "Your traits have been rechosen.",
    [1] = "One of those traits does not exist.",
    [2] = "That build could not be read.",
    [3] = "Your class has no use for one of those traits.",
    [4] = "One of those traits is not available yet.",
    [5] = "Two of those traits cannot be combined.",
    [6] = "Too many quirks.",
    [7] = "Too many merits.",
    [8] = "Too many flaws.",
    [9] = "Flaws cannot grant that many points.",
    [10] = "Not enough trait points for those merits.",
    [11] = "Rechoosing needs an inn or a city, out of combat.",
    [12] = "You cannot afford to rechoose.",
    [13] = "That is the build you already have.",
    [14] = "This client is too old or too new for the server's traits.",
};

local function Ids() return X.snap and X.snap.ids or {}; end

-- "TRAITS  ·  UNMARKED", the one trait's name, or how many.
function X.Label()
    local ids = Ids();
    if #ids == 0 then return "TRAITS  ·  UNMARKED"; end
    if #ids == 1 and P.byId[ids[1]] then return "TRAITS  ·  " .. string.upper(P.byId[ids[1]].name); end
    return "TRAITS  ·  " .. #ids .. " CHOSEN";
end

-- Tooltip lines: each trait with its advantage and drawback, in words as well as color.
function X.Lines()
    local lines = {};
    for _, id in ipairs(Ids()) do
        local trait = P.byId[id];
        if trait then
            lines[#lines + 1] = { trait.name .. "  (" .. trait.kind .. (trait.lock and ", inactive" or "") .. ")", GOLD };
            if trait.up then lines[#lines + 1] = { "+ " .. trait.up, GOOD }; end
            if trait.down then lines[#lines + 1] = { "- " .. trait.down, BAD }; end
        end
    end
    if #lines == 0 then lines[1] = { "No starting traits: no advantage, no drawback.", MUTED }; end
    if X.snap and X.snap.repair then
        lines[#lines + 1] = { "A trait you hold was switched off. Your next rechoose is free.", BAD };
    end
    lines[#lines + 1] = { "Click to rechoose. The first time is free.", MUTED };
    return lines;
end

-- The advantages and the drawbacks as two blocks of text, for the Codex's summary page.
function X.Text()
    local ups, downs = {}, {};
    for _, id in ipairs(Ids()) do
        local trait = P.byId[id];
        if trait and trait.up then ups[#ups + 1] = "+ " .. trait.up; end
        if trait and trait.down then downs[#downs + 1] = "- " .. trait.down; end
    end
    return #ups > 0 and table.concat(ups, "\n") or "+ No advantage", #downs > 0 and table.concat(downs, "\n") or "- No drawback";
end

-- ---- the rechoose dialog -------------------------------------------------------------------------------------

local dialog;

local function SameAsNow(ids)
    local now = Ids();
    if #ids ~= #now then return false; end
    for i, id in ipairs(ids) do if now[i] ~= id then return false; end end
    return true;
end

local function Paint()
    if not dialog or not X.snap then return; end
    local s, ids = X.snap, dialog.picker:Ids();
    local cost = s.cost > 0 and GetCoinTextureString(s.cost) or "free";
    local why = dialog.picker:Problem() or (SameAsNow(ids) and "Change the build to rechoose.")
        or (s.lock == LOCK_PLACE and RESULTS[LOCK_PLACE]) or (s.lock == LOCK_GOLD and RESULTS[LOCK_GOLD]);
    dialog.go.ready = not why;
    dialog.go.label:SetText(dialog.armed and ("CONFIRM  ·  " .. cost) or ("RECHOOSE  ·  " .. cost));
    dialog.go:SetAlpha(why and 0.5 or 1);
    dialog.note:SetText(dialog.message or why
        or "The whole build is replaced at once. Rechosen " .. s.respecs .. (s.respecs == 1 and " time." or " times."));
    local color = dialog.message and GOLD or why and BAD or MUTED;
    dialog.note:SetTextColor(color[1], color[2], color[3]);
end

local function Build()
    dialog = CreateFrame("Frame", "EsteriaPerksTraitsFrame", UIParent);
    dialog:SetSize(760, 600);
    dialog:SetPoint("CENTER", UIParent, "CENTER", 0, 0);
    dialog:SetFrameStrata("DIALOG");
    dialog:EnableMouse(true);
    ES.Solid(dialog, "BACKGROUND", ES.INK, 0.97):SetAllPoints(dialog);
    ES.Border(dialog, GOLD_DIM, 1);
    tinsert(UISpecialFrames, "EsteriaPerksTraitsFrame"); -- Escape closes it
    local title = ES.Text(dialog, 16, GOLD);
    title:SetPoint("TOPLEFT", dialog, "TOPLEFT", 20, -16);
    title:SetText("STARTING TRAITS");
    local sub = ES.Text(dialog, 11, MUTED);
    sub:SetPoint("LEFT", title, "RIGHT", 12, -1);
    sub:SetText("Trait points, not perk points: your Paths of Fate cards are untouched.");

    dialog.picker = P.Create(dialog, "EsteriaPerksTraits");
    dialog.picker.list:SetPoint("TOPLEFT", dialog, "TOPLEFT", 20, -52);
    dialog.picker.detail:SetPoint("TOPRIGHT", dialog, "TOPRIGHT", -20, -52);
    dialog.picker.onChange = function() dialog.armed = false; dialog.message = nil; Paint(); end;

    dialog.note = ES.Text(dialog, 11, MUTED);
    dialog.note:SetWidth(340);
    dialog.note:SetJustifyH("LEFT");
    dialog.note:SetPoint("BOTTOMRIGHT", dialog, "BOTTOMRIGHT", -20, 60);

    local function Button(text, x, onClick)
        local b = CreateFrame("Button", nil, dialog);
        b:SetSize(164, 28);
        b:SetPoint("BOTTOMRIGHT", dialog, "BOTTOMRIGHT", x, 20);
        ES.Solid(b, "BACKGROUND", ES.INK, 0.9):SetAllPoints(b);
        ES.Solid(b, "HIGHLIGHT", GOLD, 0.12):SetAllPoints(b);
        ES.Border(b, GOLD_DIM, 0.9);
        b.label = ES.Text(b, 11, GOLD);
        b.label:SetPoint("CENTER", b, "CENTER", 0, 0);
        b.label:SetText(text);
        b:SetScript("OnClick", onClick);
        return b;
    end
    Button("CLOSE", -20, function() dialog:Hide(); end);
    -- Two clicks: the first arms it and shows the price again, the second sends the build.
    dialog.go = Button("", -196, function(self)
        if not self.ready then return; end
        if not dialog.armed then dialog.armed = true; Paint(); return; end
        dialog.armed = false;
        EsteriaPerks_RechooseTraits(unpack(dialog.picker:Ids()));
    end);
    dialog:Hide();
end

function X.Open()
    if not EsteriaPerks_RequestTraits then return; end
    EsteriaPerks_RequestTraits();
    if not X.snap then return; end -- shown once the server has answered
    if not dialog then Build(); end
    local _, class = UnitClass("player");
    dialog.armed, dialog.message = false, nil;
    dialog.picker:SetClass(class);
    dialog.picker:SetChosen(X.snap.ids);
    dialog:Show();
    Paint();
end

local events = CreateFrame("Frame");
events:RegisterEvent("PLAYER_ENTERING_WORLD");
events:RegisterEvent("PLAYER_UPDATE_RESTING");
events:RegisterEvent("PLAYER_REGEN_ENABLED");
events:RegisterEvent("ESTERIA_TRAIT_SNAPSHOT");
events:RegisterEvent("ESTERIA_TRAIT_RESULT");
events:SetScript("OnEvent", function(_, event, result)
    if event == "ESTERIA_TRAIT_SNAPSHOT" then
        X.snap = EsteriaPerks_GetTraits();
        if dialog and dialog:IsShown() and X.snap and dialog.message == RESULTS[0] then dialog.picker:SetChosen(X.snap.ids); end
        Paint();
        if EsteriaCodex and EsteriaCodex.frame:IsShown() then EsteriaCodex.Redraw(); end
    elseif event == "ESTERIA_TRAIT_RESULT" then
        if dialog then dialog.message = RESULTS[result] or ("The server refused the build (" .. tostring(result) .. ")."); end
        if result == 0 then PlaySound("igSpellBookOpen"); end
        Paint();
    elseif EsteriaPerks_RequestTraits and (event == "PLAYER_ENTERING_WORLD" or (dialog and dialog:IsShown())) then
        -- Resting and combat change whether a rechoose is allowed.
        EsteriaPerks_RequestTraits();
    end
end);
