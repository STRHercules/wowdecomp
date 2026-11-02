-- Fate page (wowdecomp)
-- Paths of Fate starts here: a FATE step after Body (after Appearance for races without the Body page) where the
-- player picks a background trait. SetCreateTrait sends it with CMSG_CHAR_CREATE (Engine/Source/Retail/
-- PerkTraitWire.h); perk points come later, in game, from level 10. Never offered for paid customization.
-- Everything is guarded: if anything fails the step disappears and characters are created Unmarked.
-- Its own function: the creator chunk is near Lua's 200-locals-per-function limit.
(function()
local ES = EsteriaStyle;
local GOLD, GOLD_DIM, TEXT, MUTED, GOOD, BAD = ES.GOLD, ES.GOLD_DIM, ES.TEXT, ES.MUTED, ES.GOOD, ES.BAD;
local Solid, Text, Tween = ES.Solid, ES.Text, ES.Tween;
local FP = { open = false, trait = 0, broken = nil, rows = {}, paths = {} };
local EDGE_X, COLUMN_W, TOP = 55, 340, -140;

-- Ids are PerkTraitWire.h's.
local TRAITS = {
    { id = 0, name = "Unmarked", short = "No advantage, no drawback",
      lore = "No past shapes you. Your story starts the moment you step into the world.",
      up = "None", down = "None" },
    { id = 1, name = "Wanderer", short = "Faster on foot, outdoors",
      lore = "Roads have always called you. You travel light and far, but rarely stay long enough to be known.",
      up = "+5% movement speed outdoors, on foot and out of combat", down = "-10% reputation from quests" },
    { id = 2, name = "Glass Cannon", short = "More damage, less health",
      lore = "You strike first and hard, and trust that nothing gets the chance to strike back.",
      up = "+2% damage against non-player enemies", down = "-4% maximum health in PvE",
      note = "Inactive in rated PvP." },
    { id = 3, name = "Ironbound", short = "More health, less damage",
      lore = "Years of hardship left you hard to break, if a little slower to finish a fight.",
      up = "+4% maximum health in PvE", down = "-2% damage against non-player enemies",
      note = "Inactive in rated PvP." },
    { id = 4, name = "Artisan Soul", short = "Learns crafts faster",
      lore = "Your hands learn faster than your feet wander. A workshop feels more like home than the open road.",
      up = "+10% chance of profession skill gain", down = "-10% first-discovery exploration rewards" },
    { id = 5, name = "Curious Mind", short = "Rewarded for discovery",
      lore = "Every horizon is a question. You learn the world by walking it, not by working at a bench.",
      up = "+10% first-discovery exploration rewards", down = "-10% chance of profession skill gain" },
    { id = 6, name = "Folk Hero", short = "Trusted by the factions",
      lore = "You earned people's trust by doing right by them, even when it left your purse light.",
      up = "+10% reputation from quests", down = "-10% gold looted from humanoids" },
    { id = 7, name = "Outlaw Heart", short = "Richer, less trusted",
      lore = "You take what the world will not give, and the factions have not forgotten it.",
      up = "+10% gold looted from humanoids", down = "-10% reputation from quests" },
    { id = 8, name = "Frugal Survivor", short = "Rations last longer",
      lore = "You make every meal and ration last, though you never learned to trust a potion.",
      up = "+15% duration of food, drink and other non-combat buffs", down = "-10% healing from your own potions" },
};
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
    return not FP.broken and FP.built and type(SetCreateTrait) == "function" and not PAID_SERVICE_TYPE;
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

local function TraitById(id)
    for _, trait in ipairs(TRAITS) do if trait.id == id then return trait; end end
    return TRAITS[1];
end

-- ---- building ------------------------------------------------------------------------------------------------

local function Wrapped(parent, size, color, width)
    local fs = Text(parent, size, color);
    fs:SetWidth(width);
    fs:SetJustifyH("LEFT");
    return fs;
end

local function BuildRow(page, index, trait)
    local row = CreateFrame("Button", "FatePageTrait" .. index, page);
    row:SetSize(COLUMN_W, 42);
    row.trait = trait;
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
    row.name = Text(row, 13, TEXT);
    row.name:SetPoint("TOPLEFT", row, "TOPLEFT", 14, -7);
    row.name:SetText(trait.name);
    row.short = Text(row, 11, MUTED);
    row.short:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3);
    row.short:SetText(trait.short);
    row.mark = Text(row, 11, GOLD);
    row.mark:SetPoint("RIGHT", row, "RIGHT", -10, 0);
    row.mark:SetText("CHOSEN");
    row:SetScript("OnEnter", function() row.accent:Show(); end);
    row:SetScript("OnLeave", function() if FP.trait ~= trait.id then row.accent:Hide(); end end);
    row:SetScript("OnClick", function() FP.Select(trait.id, true); end);
    return row;
end

local function BuildDetail(page)
    local detail = CreateFrame("Frame", "FatePageDetail", page);
    detail:SetSize(COLUMN_W, 300);
    FP.detail = detail;
    local name = Text(detail, 18, GOLD);
    name:SetPoint("TOPLEFT", detail, "TOPLEFT", 0, 0);
    local lore = Wrapped(detail, 12, MUTED, COLUMN_W);
    lore:SetPoint("TOPLEFT", name, "BOTTOMLEFT", 0, -8);
    local upLabel = Text(detail, 11, GOLD_DIM);
    upLabel:SetPoint("TOPLEFT", lore, "BOTTOMLEFT", 0, -14);
    upLabel:SetText("ADVANTAGE");
    local up = Wrapped(detail, 13, GOOD, COLUMN_W);
    up:SetPoint("TOPLEFT", upLabel, "BOTTOMLEFT", 0, -4);
    local downLabel = Text(detail, 11, GOLD_DIM);
    downLabel:SetPoint("TOPLEFT", up, "BOTTOMLEFT", 0, -10);
    downLabel:SetText("DRAWBACK");
    local down = Wrapped(detail, 13, BAD, COLUMN_W);
    down:SetPoint("TOPLEFT", downLabel, "BOTTOMLEFT", 0, -4);
    local note = Wrapped(detail, 11, MUTED, COLUMN_W);
    note:SetPoint("TOPLEFT", down, "BOTTOMLEFT", 0, -10);
    FP.text = { name = name, lore = lore, up = up, down = down, note = note };

    -- What lies ahead: the six paths, locked until level 10.
    local paths = CreateFrame("Frame", "FatePagePaths", page);
    paths:SetSize(COLUMN_W, 260);
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
    footer:SetText("Your first perk point arrives at level 10, then one every five levels: 15 by level 80. "
        .. "You can change this trait once for free in the world.");
end

local function Build()
    local page = CreateFrame("Frame", "FatePagePanel", CharacterCreateFrame);
    page:SetAllPoints(CharacterCreateFrame);
    page:Hide();
    FP.page = page;
    for index, trait in ipairs(TRAITS) do FP.rows[index] = BuildRow(page, index, trait); end
    BuildDetail(page);
end

-- ---- motion --------------------------------------------------------------------------------------------------

local function PlaceRow(row, index, p)
    row:ClearAllPoints();
    row:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", EDGE_X - 14 * (1 - p), TOP - (index - 1) * 46);
    row:SetAlpha(p);
end

local function PlaceDetail(p)
    FP.detail:ClearAllPoints();
    FP.detail:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPRIGHT", -EDGE_X, TOP - 8 * (1 - p));
    FP.detail:SetAlpha(p);
end

local function PlacePaths(p)
    FP.pathsFrame:ClearAllPoints();
    FP.pathsFrame:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPRIGHT", -EDGE_X, TOP - 230);
    FP.pathsFrame:SetAlpha(p);
    for index, row in ipairs(FP.paths) do
        -- Rows land one after another inside the same tween.
        local q = math.max(0, math.min(1, p * 1.6 - (index - 1) * 0.1));
        row:ClearAllPoints();
        row:SetPoint("TOPLEFT", FP.pathsFrame, "TOPLEFT", 10 * (1 - q), -44 - (index - 1) * 26);
        row:SetAlpha(q);
    end
end

local function Refresh()
    for _, row in ipairs(FP.rows) do
        local on = row.trait.id == FP.trait;
        local color = on and GOLD or TEXT;
        row.name:SetTextColor(color[1], color[2], color[3]);
        if on then row.accent:Show(); row.mark:Show(); else row.accent:Hide(); row.mark:Hide(); end
    end
    local trait = TraitById(FP.trait);
    FP.text.name:SetText(string.upper(trait.name));
    FP.text.lore:SetText(trait.lore);
    FP.text.up:SetText(trait.up);
    FP.text.down:SetText(trait.down);
    FP.text.note:SetText(trait.note or "");
end

function FP.Select(id, animate)
    if not SetCreateTrait(id) then return; end
    FP.trait = id;
    Refresh();
    if not animate then return; end
    PlaySound("gsCharacterCreationLook");
    for _, row in ipairs(FP.rows) do
        if row.trait.id == id then
            Tween("fate.flash", 0.6, 0, function(p) row.wash:SetAlpha(0.3 * (1 - p)); end);
        end
    end
    Tween("fate.detail", 0.24, 0, PlaceDetail);
end

local function SetOpen(open)
    if FP.open == open then return; end
    FP.open = open;
    if CharacterCustomization_CloseChoices then CharacterCustomization_CloseChoices(); end
    PlaySound("gsCharacterCreationLook");
    if not open then
        FP.page:Hide();
        -- The stock refresh puts back the rows the guard hid.
        if CharacterCreate_UpdateHairCustomization then CharacterCreate_UpdateHairCustomization(); end
        return;
    end
    FP.page:Show();
    Refresh();
    for index, row in ipairs(FP.rows) do
        Tween("fate.row" .. index, 0.3, (index - 1) * 0.04, function(p) PlaceRow(row, index, p); end);
    end
    Tween("fate.detail", 0.3, 0.08, PlaceDetail);
    Tween("fate.paths", 0.5, 0.18, PlacePaths);
end

-- ---- flow ----------------------------------------------------------------------------------------------------

local function Hook()
    -- Accept on the page before Fate opens Fate; Accept on Fate creates the character with the picked trait.
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if FatePage_Open() then
            SetCreateTrait(FP.trait); -- the native pick clears after each create attempt; a failed one (name taken) retries
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
        FP.trait = 0;
        if SetCreateTrait then SetCreateTrait(0); end
        if FP.page then FP.page:Hide(); end
        return onShow(...);
    end
end

local function Tick()
    if not FatePage_Open() then
        FP.open = false;
        FP.page:Hide();
        return;
    end
    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row and row:IsShown() then row:Hide(); end
    end
    -- Follow the skin's collapsible side panels.
    local shown = CreatorSkin_PaneShown and CreatorSkin_PaneShown() or 1;
    FP.page:SetAlpha(shown);
    for _, row in ipairs(FP.rows) do row:EnableMouse(shown > 0.5); end
    -- The starting zone page covers this one; Accept reads Next here while it still follows.
    if StartZonePage_Open and StartZonePage_Open() then FP.page:Hide(); else FP.page:Show(); end
    local label = StartZonePage_Follows and StartZonePage_Follows() and "Next" or FP.okayText;
    if label and CharCreateOkayButton:GetText() ~= label then CharCreateOkayButton:SetText(label); end
end

local function Install()
    if type(SetCreateTrait) ~= "function" then FP_Log("no SetCreateTrait native; step off"); return; end
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
            pcall(function() FP.page:Hide(); SetCreateTrait(0); end);
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
