-- Fate page (wowdecomp)
-- Paths of Fate starts here: a FATE step after Body (after Appearance for races without the Body page) where the
-- player builds their starting traits from a small point budget: Merits spend points, Flaws grant them, Quirks pair
-- an advantage with a drawback for free (fate_picker.lua draws it, fate_traits.lua is the catalog). The build goes out
-- with CMSG_CHAR_CREATE through SetCreateTraitList (Engine/Source/Retail/PerkTraitWire.h); the server checks it. Perk
-- points are a separate thing that comes later, in game, from level 10. Never offered for paid customization.
-- Everything is guarded: if anything fails the step disappears and characters are created Unmarked.
-- Its own function: the creator chunk is near Lua's 200-locals-per-function limit.
(function()
local ES = EsteriaStyle;
local GOLD, GOLD_DIM, TEXT, MUTED = ES.GOLD, ES.GOLD_DIM, ES.TEXT, ES.MUTED;
local Solid, Text, Tween = ES.Solid, ES.Text, ES.Tween;
local FP = { open = false, broken = nil, paths = {}, confirmed = false };
local EDGE_X, COLUMN_W, TOP = 55, 340, -140;

local PATHS = {
    { "COMBAT", "The Warrior's Instinct" }, { "SURVIVAL", "The Unyielding Road" },
    { "EXPLORATION", "The Boundless Horizon" }, { "INDUSTRY", "The Artisan's Hand" },
    { "INFLUENCE", "The Silver Path" }, { "COMPANIONS", "The Warband's Bond" },
};

local function FP_Log(text)
    if BodyShapeLog then BodyShapeLog("fate: " .. tostring(text)); end
end

local function Personalizing()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

-- True when this creator session offers the step (the skin shows it in the step indicator).
function FatePage_Offered()
    return not FP.broken and FP.built and type(SetCreateTraitList) == "function" and not PAID_SERVICE_TYPE;
end

local function Available()
    return FatePage_Offered() and Personalizing();
end

function FatePage_Open()
    return FP.open and Available();
end

-- True on the page just before Fate: Accept reads Next there.
function FatePage_Follows()
    return not FP.open and Available();
end

-- The native pick clears after each create attempt, so this runs before every one; a failed create (name taken)
-- then resends the same build.
local function Send()
    return SetCreateTraitList(unpack(FP.picker:Ids()));
end

-- ---- building ------------------------------------------------------------------------------------------------

local function Wrapped(parent, size, color, width)
    local fs = Text(parent, size, color);
    fs:SetWidth(width);
    fs:SetJustifyH("LEFT");
    return fs;
end

-- What lies ahead: the six paths, locked until level 10. A different currency from the trait points above.
local function BuildPaths(page)
    local paths = CreateFrame("Frame", "FatePagePaths", page);
    paths:SetSize(COLUMN_W, 250);
    FP.pathsFrame = paths;
    local rule = Solid(paths, "BORDER", GOLD_DIM, 0.6);
    rule:SetSize(COLUMN_W, 1);
    rule:SetPoint("TOPLEFT", paths, "TOPLEFT", 0, 0);
    local head = Text(paths, 13, GOLD);
    head:SetPoint("TOPLEFT", rule, "BOTTOMLEFT", 0, -12);
    head:SetText("PATHS AHEAD");
    local lock = Text(paths, 11, MUTED);
    lock:SetPoint("TOPRIGHT", rule, "BOTTOMRIGHT", 0, -13);
    lock:SetText("LOCKED UNTIL LEVEL 10");
    for index, path in ipairs(PATHS) do
        local row = CreateFrame("Frame", nil, paths);
        row:SetSize(COLUMN_W, 24);
        local number = Text(row, 11, GOLD_DIM);
        number:SetPoint("LEFT", row, "LEFT", 0, 0);
        number:SetText(index);
        local label = Text(row, 12, TEXT);
        label:SetPoint("LEFT", row, "LEFT", 18, 0);
        label:SetText(path[1]);
        local motto = Text(row, 11, MUTED);
        motto:SetPoint("RIGHT", row, "RIGHT", 0, 0);
        motto:SetText(path[2]);
        FP.paths[index] = row;
    end
    local footer = Wrapped(paths, 11, MUTED, COLUMN_W);
    footer:SetPoint("TOPLEFT", paths, "TOPLEFT", 0, -50 - #PATHS * 26);
    footer:SetText("Trait points are spent now and never become perk points. Your first perk point arrives at level 10, "
        .. "then one every five levels: 15 by level 80. You can rechoose your traits once for free in the world.");
end

-- The last look before the character exists: every trait, the point sum, and that it is permanent.
local function BuildReview()
    local review = CreateFrame("Frame", "FatePageReview", CharacterCreateFrame);
    review:SetSize(520, 450);
    review:SetPoint("CENTER", CharacterCreateFrame, "CENTER", 0, 10);
    review:SetFrameStrata("DIALOG");
    review:EnableMouse(true);
    Solid(review, "BACKGROUND", ES.INK, 0.97):SetAllPoints(review);
    ES.Border(review, GOLD_DIM, 1);
    local title = Text(review, 20, GOLD);
    title:SetPoint("TOP", review, "TOP", 0, -20);
    title:SetText("CONFIRM YOUR FATE");
    local sub = Text(review, 11, MUTED);
    sub:SetPoint("TOP", title, "BOTTOM", 0, -6);
    sub:SetText("Read it once more: this is who they are.");
    local function Rule(y)
        local rule = Solid(review, "ARTWORK", GOLD_DIM, 0.6);
        rule:SetHeight(1);
        rule:SetPoint("TOPLEFT", review, "TOPLEFT", 28, y);
        rule:SetPoint("TOPRIGHT", review, "TOPRIGHT", -28, y);
    end
    Rule(-72);
    review.body = Wrapped(review, 14, TEXT, 460);
    review.body:SetPoint("TOPLEFT", review, "TOPLEFT", 30, -86);
    Rule(-326);
    review.sum = Text(review, 13, TEXT);
    review.sum:SetPoint("TOPLEFT", review, "TOPLEFT", 30, -340);
    review.foot = Wrapped(review, 11, MUTED, 460);
    review.foot:SetPoint("BOTTOMLEFT", review, "BOTTOMLEFT", 30, 20);
    review.foot:SetText("These traits become part of this character. You can rechoose them once for free in the world, "
        .. "at an inn or city; after that it costs gold.\n\nAccept again to create the character. Back to change your traits.");
    review:Hide();
    FP.review = review;
end

local function Hex(c) return string.format("|cff%02x%02x%02x", c[1] * 255, c[2] * 255, c[3] * 255); end

local function ShowReview()
    local P, lines = EsteriaFatePicker, {};
    for _, id in ipairs(FP.picker:Ids()) do
        local trait = P.byId[id];
        local c = trait.kind == "merit" and ES.BAD or trait.kind == "flaw" and ES.GOOD or ES.MUTED;
        lines[#lines + 1] = Hex(c) .. string.format("%-3s", P.Points(trait)) .. "|r   " .. trait.name
            .. "   " .. Hex(ES.MUTED) .. string.upper(trait.kind) .. "|r";
    end
    FP.review.body:SetText(table.concat(lines, "\n"));
    local flaws, spent, left = P.Budget(FP.picker.chosen);
    -- The body text keeps the plain sum as well: it is what the summary test and screen readers look for.
    FP.review.sum:SetText(EsteriaFateTraits.base .. " starting + " .. flaws .. " from flaws - " .. spent .. " on merits = "
        .. left .. " unspent" .. (left > 0 and " (lost)" or ""));
    FP.review.sum:SetTextColor((left < 0 and ES.BAD or ES.GOLD)[1], (left < 0 and ES.BAD or ES.GOLD)[2], (left < 0 and ES.BAD or ES.GOLD)[3]);
    FP.reviewOnZone = StartZonePage_Open and StartZonePage_Open() or false;
    FP.review:Show();
end

local function Build()
    local page = CreateFrame("Frame", "FatePagePanel", CharacterCreateFrame);
    page:SetAllPoints(CharacterCreateFrame);
    page:Hide();
    FP.page = page;
    FP.picker = EsteriaFatePicker.Create(page, "FatePage");
    -- A changed build must be looked at again before it is created.
    FP.picker.onChange = function() FP.confirmed = false; end;
    BuildPaths(page);
    BuildReview();
end

-- ---- motion --------------------------------------------------------------------------------------------------

local function PlaceList(p)
    FP.picker.list:ClearAllPoints();
    FP.picker.list:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", EDGE_X - 14 * (1 - p), TOP);
    FP.picker.list:SetAlpha(p);
end

local function PlaceDetail(p)
    FP.picker.detail:ClearAllPoints();
    FP.picker.detail:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPRIGHT", -EDGE_X, TOP - 8 * (1 - p));
    FP.picker.detail:SetAlpha(p);
end

local function PlacePaths(p)
    FP.pathsFrame:ClearAllPoints();
    FP.pathsFrame:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPRIGHT", -EDGE_X, TOP - 270);
    FP.pathsFrame:SetAlpha(p);
    for index, row in ipairs(FP.paths) do
        -- Rows land one after another inside the same tween.
        local q = math.max(0, math.min(1, p * 1.6 - (index - 1) * 0.1));
        row:ClearAllPoints();
        row:SetPoint("TOPLEFT", FP.pathsFrame, "TOPLEFT", 10 * (1 - q), -44 - (index - 1) * 26);
        row:SetAlpha(q);
    end
end

local function SetOpen(open)
    if FP.open == open then return; end
    FP.open = open;
    FP.review:Hide();
    if CharacterCustomization_CloseChoices then CharacterCustomization_CloseChoices(); end
    PlaySound("gsCharacterCreationLook");
    if not open then
        FP.page:Hide();
        CharCreateOkayButton:Enable();
        -- The stock refresh puts back the rows the guard hid.
        if CharacterCreate_UpdateHairCustomization then CharacterCreate_UpdateHairCustomization(); end
        return;
    end
    FP.page:Show();
    FP.picker:Refresh();
    Tween("fate.list", 0.3, 0, PlaceList);
    Tween("fate.detail", 0.3, 0.08, PlaceDetail);
    Tween("fate.paths", 0.5, 0.18, PlacePaths);
end

-- ---- flow ----------------------------------------------------------------------------------------------------

local function Hook()
    -- Accept on the page before Fate opens Fate; Accept on Fate (or on the zone page after it) creates the
    -- character with the build, after one look at the summary.
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if FatePage_Open() then
            if FP.picker:Problem() then return; end
            if not FP.confirmed and #FP.picker:Ids() > 0 then
                FP.confirmed = true;
                ShowReview();
                return;
            end
            FP.review:Hide();
            Send();
            return okay(...);
        end
        local bodyFlow = BodyShapePage_FlowAvailable and BodyShapePage_FlowAvailable();
        if FatePage_Follows() and (not bodyFlow or BodyShapePage_Active()) then
            SetOpen(true);
            return;
        end
        return okay(...);
    end
    local back = CharacterCreate_Back;
    function CharacterCreate_Back(...)
        if FP.review:IsShown() then FP.review:Hide(); FP.confirmed = false; return; end
        if FP.open then SetOpen(false); return; end
        return back(...);
    end
    local toggle = CharacterCreate_TogglePersonalization;
    function CharacterCreate_TogglePersonalization(...)
        if FP.open then SetOpen(false); return; end
        return toggle(...);
    end
    -- Every new character starts Unmarked.
    local onShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        FP.open = false;
        FP.confirmed = false;
        if FP.picker then FP.picker:SetChosen({}); end
        if SetCreateTraitList then SetCreateTraitList(); end
        if FP.review then FP.review:Hide(); end
        if FP.page then FP.page:Hide(); end
        return onShow(...);
    end
end

local function Tick()
    if not FatePage_Open() then
        if FP.open then CharCreateOkayButton:Enable(); end
        FP.open = false;
        FP.page:Hide();
        FP.review:Hide();
        return;
    end
    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row and row:IsShown() then row:Hide(); end
    end
    -- A flaw has to be able to bite: the class decides which traits can be picked.
    if GetSelectedClass then
        local _, class = GetSelectedClass();
        FP.picker:SetClass(class);
    end
    -- Follow the skin's collapsible side panels.
    local shown = CreatorSkin_PaneShown and CreatorSkin_PaneShown() or 1;
    FP.page:SetAlpha(shown);
    FP.picker.list:EnableMouse(shown > 0.5);
    -- The starting zone page covers this one; Accept reads Next here while it still follows.
    local zoneOpen = StartZonePage_Open and StartZonePage_Open() or false;
    if zoneOpen then FP.page:Hide(); else FP.page:Show(); end
    -- Leaving the page the summary was shown on (Back off the zone page) takes the summary with it.
    if FP.review:IsShown() and zoneOpen ~= FP.reviewOnZone then FP.review:Hide(); FP.confirmed = false; end
    local label = StartZonePage_Follows and StartZonePage_Follows() and "Next" or FP.okayText;
    if label and CharCreateOkayButton:GetText() ~= label then CharCreateOkayButton:SetText(label); end
    -- Editing may go over budget; leaving the page may not.
    if FP.picker:Problem() then CharCreateOkayButton:Disable(); else CharCreateOkayButton:Enable(); end
end

local function Install()
    if type(SetCreateTraitList) ~= "function" then FP_Log("no SetCreateTraitList native; step off"); return; end
    if not (EsteriaFatePicker and EsteriaFatePicker.Create and EsteriaFateTraits) then FP_Log("no trait catalog; step off"); return; end
    Build();
    Hook();
    FP.built = true;
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        -- Accept's own label (the Body page swaps it for Next), read before anyone changes it.
        if not FP.okayText and CharCreateOkayButton:GetText() ~= "Next" then FP.okayText = CharCreateOkayButton:GetText(); end
        local ok, err = pcall(Tick);
        if not ok then
            FP.broken = tostring(err);
            FP.open = false;
            pcall(function() FP.page:Hide(); FP.review:Hide(); CharCreateOkayButton:Enable(); SetCreateTraitList(); end);
            FP_Log("tick error: " .. FP.broken);
            self:SetScript("OnUpdate", nil);
        end
    end);
    FP_Log("installed");
end

-- The creator's frames do not exist yet while this file loads, so build the page on the first frame that has them.
local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton) then return; end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(Install);
    if not ok then
        FP.broken = tostring(err);
        FP_Log("install failed: " .. FP.broken);
    end
end);
end)();
