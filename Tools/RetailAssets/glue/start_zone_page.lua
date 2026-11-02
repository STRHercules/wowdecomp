-- Starting zone page (wowdecomp)
-- The last step, after Fate: pick where the character begins. Retail's zone art (EsteriaStartZones.blp, built by
-- start_zone_art.py) sits beside a list of every starting zone; SetCreateStartZone sends the pick with
-- CMSG_CHAR_CREATE (Engine/Source/Retail/StartZoneWire.h) and the server places the character and its hearthstone
-- there. The race's own zone starts chosen (id 0, left to the server, for custom races). Needs the Fate page (Accept on Fate opens this page; Accept here
-- creates). Everything is guarded: if anything fails the step disappears and the race's own zone is used.
-- Its own function: the creator chunk is near Lua's 200-locals-per-function limit.
(function()
local ES = EsteriaStyle;
local GOLD, GOLD_DIM, TEXT, MUTED = ES.GOLD, ES.GOLD_DIM, ES.TEXT, ES.MUTED;
local Solid, Text, Tween = ES.Solid, ES.Text, ES.Tween;
local SZ = { open = false, zone = 0, broken = nil, rows = {} };
local EDGE_X, COLUMN_W, TOP, ROW_H = 55, 340, -140, 46;
local ART = "Interface\\Glues\\CharacterCreate\\EsteriaStartZones";
local ART_W, ART_H = 340, 227; -- one 512x341 slot of the 2048x1024 atlas

-- Ids are StartZoneWire.h's; slot is the zone's place in the art atlas (4 per row).
local ZONES = {
    [1] = { name = "Northshire Valley", region = "Elwynn Forest", slot = 0,
            lore = "The abbey's bells call every new recruit of Stormwind. Wolves and kobolds trouble the farms below, and the Stormwind Guard teaches the rest." },
    [2] = { name = "Coldridge Valley", region = "Dun Morogh", slot = 1,
            lore = "Snow, pine and the forges of the dwarves and gnomes. Troggs and Frostmane trolls crowd the pass to Anvilmar." },
    [3] = { name = "Shadowglen", region = "Teldrassil", slot = 2,
            lore = "Moonlit glades beneath the World Tree. The sentinels train you here while the corruption of Teldrassil stirs." },
    [4] = { name = "Ammen Vale", region = "Azuremyst Isle", slot = 3,
            lore = "The Exodar fell from the sky and broke on these shores. Survivors gather among the wreckage and the strange crystals." },
    [5] = { name = "Valley of Trials", region = "Durotar", slot = 4,
            lore = "A red, hard land where Orcs and Trolls prove themselves before the road to Orgrimmar opens to them." },
    [6] = { name = "Camp Narache", region = "Mulgore", slot = 5,
            lore = "Green plains, still lakes and the totems of the Tauren. The Earthmother watches over the hunters of Red Cloud Mesa." },
    [7] = { name = "Deathknell", region = "Tirisfal Glades", slot = 6,
            lore = "The Forsaken wake among the graves of a ruined chapel and take back what the Scourge left behind." },
    [8] = { name = "Sunstrider Isle", region = "Eversong Woods", slot = 7,
            lore = "Golden trees and the glow of the Sunwell. The blood elves rebuild beneath the spires of Quel'Thalas." },
};
-- Stock race id -> its own zone id, chosen when the page opens. Custom races have none.
local HOME = { [1] = 1, [3] = 2, [7] = 2, [4] = 3, [11] = 4, [2] = 5, [8] = 5, [6] = 6, [5] = 7, [10] = 8 };

local function SZ_Log(text)
    if BodyShapeLog then BodyShapeLog("zone: " .. tostring(text)); end
end

local function Personalizing()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

local function IsDeathKnight()
    -- The native reads the creator's character, which only exists once the player is customizing one: any earlier call
    -- (the frame is "shown" even on the login screen) crashes the client.
    if not CharacterCreate.personalizationMode then return false; end
    local _, class = GetSelectedClass();
    return class == "DEATHKNIGHT"; -- Acherus is the only start
end

-- True when this creator session offers the step.
function StartZonePage_Offered()
    return not SZ.broken and SZ.built and type(SetCreateStartZone) == "function" and FatePage_Offered
        and FatePage_Offered() and not IsDeathKnight();
end

local function Available()
    return StartZonePage_Offered() and FatePage_Open and FatePage_Open();
end

function StartZonePage_Open()
    return SZ.open and Available();
end

-- True on the Fate page: Accept reads Next there.
function StartZonePage_Follows()
    return not SZ.open and Available();
end

-- Every zone, whatever the race (the realm is faction free).
local function Entries()
    local list = {};
    for id = 1, #ZONES do list[id] = { id = id, zone = ZONES[id] }; end
    return list;
end

-- ---- building ------------------------------------------------------------------------------------------------

local function Art(parent, width, height, slot)
    local tex = parent:CreateTexture(nil, "ARTWORK");
    tex:SetTexture(ART);
    tex:SetSize(width, height);
    SZ.Slot(tex, slot);
    return tex;
end

function SZ.Slot(tex, slot)
    if not slot then tex:SetTexture(ES.WHITE); tex:SetTexCoord(0, 1, 0, 1); tex:SetVertexColor(0.05, 0.06, 0.08); return; end
    tex:SetTexture(ART);
    tex:SetVertexColor(1, 1, 1);
    local col, row = slot % 4, math.floor(slot / 4);
    tex:SetTexCoord(col * 0.25, (col + 1) * 0.25, row * 341 / 1024, (row + 1) * 341 / 1024);
end

local function BuildRow(page, index)
    local row = CreateFrame("Button", "StartZonePageRow" .. index, page);
    row:SetSize(COLUMN_W, ROW_H);
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
    row.thumb = Art(row, 51, 34, 0);
    row.thumb:SetPoint("LEFT", row, "LEFT", 12, 0);
    row.name = Text(row, 13, TEXT);
    row.name:SetPoint("TOPLEFT", row, "TOPLEFT", 74, -7);
    row.short = Text(row, 11, MUTED);
    row.short:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3);
    row.mark = Text(row, 11, GOLD);
    row.mark:SetPoint("RIGHT", row, "RIGHT", -10, 0);
    row.mark:SetText("CHOSEN");
    row:SetScript("OnEnter", function() row.accent:Show(); end);
    row:SetScript("OnLeave", function() if SZ.zone ~= row.entry.id then row.accent:Hide(); end end);
    row:SetScript("OnClick", function() SZ.Select(row.entry.id, true); end);
    return row;
end

local function BuildDetail(page)
    local detail = CreateFrame("Frame", "StartZonePageDetail", page);
    detail:SetSize(COLUMN_W, 420);
    SZ.detail = detail;
    SZ.art = Art(detail, ART_W, ART_H, 0);
    SZ.art:SetPoint("TOPLEFT", detail, "TOPLEFT", 0, 0);
    local frame = CreateFrame("Frame", nil, detail);
    frame:SetAllPoints(SZ.art);
    ES.Border(frame, GOLD_DIM, 0.8);
    local name = Text(detail, 18, GOLD);
    name:SetPoint("TOPLEFT", SZ.art, "BOTTOMLEFT", 0, -16);
    local region = Text(detail, 12, GOLD_DIM);
    region:SetPoint("TOPLEFT", name, "BOTTOMLEFT", 0, -4);
    local lore = Text(detail, 12, MUTED);
    lore:SetWidth(COLUMN_W);
    lore:SetJustifyH("LEFT");
    lore:SetPoint("TOPLEFT", region, "BOTTOMLEFT", 0, -10);
    local note = Text(detail, 11, MUTED);
    note:SetWidth(COLUMN_W);
    note:SetJustifyH("LEFT");
    note:SetPoint("TOPLEFT", lore, "BOTTOMLEFT", 0, -14);
    note:SetText("Your first quests, trainers and hearthstone are here. The rest of the world stays open to you.");
    SZ.text = { name = name, region = region, lore = lore };
end

local function Build()
    local page = CreateFrame("Frame", "StartZonePagePanel", CharacterCreateFrame);
    page:SetAllPoints(CharacterCreateFrame);
    page:Hide();
    SZ.page = page;
    for index = 1, 8 do SZ.rows[index] = BuildRow(page, index); end
    BuildDetail(page);
end

-- ---- motion --------------------------------------------------------------------------------------------------

local function PlaceRow(row, index, p)
    row:ClearAllPoints();
    row:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", EDGE_X - 14 * (1 - p), TOP - (index - 1) * (ROW_H + 4));
    row:SetAlpha(p);
end

local function PlaceDetail(p)
    SZ.detail:ClearAllPoints();
    SZ.detail:SetPoint("TOPRIGHT", CharacterCreateFrame, "TOPRIGHT", -EDGE_X, TOP - 8 * (1 - p));
    SZ.detail:SetAlpha(p);
end

local function Refresh()
    local chosen;
    for index, row in ipairs(SZ.rows) do
        local entry = SZ.entries[index];
        row.entry = entry;
        if entry then
            local on = entry.id == SZ.zone;
            if on then chosen = entry; end
            local color = on and GOLD or TEXT;
            row.name:SetTextColor(color[1], color[2], color[3]);
            row.name:SetText(entry.zone.name);
            row.short:SetText(entry.zone.region);
            SZ.Slot(row.thumb, entry.zone.slot);
            if on then row.accent:Show(); row.mark:Show(); else row.accent:Hide(); row.mark:Hide(); end
            row:Show();
        else
            row:Hide();
        end
    end
    local zone = chosen and chosen.zone;
    SZ.Slot(SZ.art, zone and zone.slot);
    SZ.text.name:SetText(zone and string.upper(zone.name) or "STARTING ZONE");
    SZ.text.region:SetText(zone and zone.region or "");
    SZ.text.lore:SetText(zone and zone.lore or "Pick where your story begins. Left alone, your character starts where their people do.");
end

function SZ.Select(id, animate)
    if not SetCreateStartZone(id) then return; end
    SZ.zone = id;
    Refresh();
    if not animate then return; end
    PlaySound("gsCharacterCreationLook");
    for _, row in ipairs(SZ.rows) do
        if row.entry and row.entry.id == id then
            Tween("zone.flash", 0.6, 0, function(p) row.wash:SetAlpha(0.3 * (1 - p)); end);
        end
    end
    Tween("zone.detail", 0.24, 0, PlaceDetail);
end

local function SetOpen(open)
    if SZ.open == open then return; end
    SZ.open = open;
    PlaySound("gsCharacterCreationLook");
    if not open then
        SZ.page:Hide();
        return;
    end
    SZ.entries = Entries();
    SZ.zone = HOME[CharacterCreate.selectedRaceID] or 0; -- the race's own zone; custom races leave it to the server
    SetCreateStartZone(SZ.zone);
    SZ.page:Show();
    Refresh();
    for index, row in ipairs(SZ.rows) do
        Tween("zone.row" .. index, 0.3, (index - 1) * 0.04, function(p) PlaceRow(row, index, p); end);
    end
    Tween("zone.detail", 0.3, 0.08, PlaceDetail);
end

-- ---- flow ----------------------------------------------------------------------------------------------------

local function Hook()
    -- Accept on Fate opens this page; Accept here falls through to Fate's, which creates.
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if StartZonePage_Open() then
            SetCreateStartZone(SZ.zone); -- the native pick clears after each create attempt; a failed one (name taken) retries
            return okay(...);
        end
        if StartZonePage_Follows() then
            SetOpen(true);
            return;
        end
        return okay(...);
    end
    local back = CharacterCreate_Back;
    function CharacterCreate_Back(...)
        if SZ.open then SetOpen(false); return; end
        return back(...);
    end
    local toggle = CharacterCreate_TogglePersonalization;
    function CharacterCreate_TogglePersonalization(...)
        if SZ.open then SetOpen(false); return; end
        return toggle(...);
    end
    -- Every new character starts in its race's own zone.
    local onShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        SZ.open = false;
        SZ.zone = 0;
        if SetCreateStartZone then SetCreateStartZone(0); end
        if SZ.page then SZ.page:Hide(); end
        return onShow(...);
    end
end

local function Tick()
    if not StartZonePage_Open() then
        SZ.open = false;
        SZ.page:Hide();
        return;
    end
    -- Follow the skin's collapsible side panels.
    local shown = CreatorSkin_PaneShown and CreatorSkin_PaneShown() or 1;
    SZ.page:SetAlpha(shown);
    for _, row in ipairs(SZ.rows) do row:EnableMouse(shown > 0.5); end
    SZ.page:Show();
end

local function Install()
    if type(SetCreateStartZone) ~= "function" then SZ_Log("no SetCreateStartZone native; step off"); return; end
    Build();
    Hook();
    SZ.built = true;
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        local ok, err = pcall(Tick);
        if not ok then
            SZ.broken = tostring(err);
            SZ.open = false;
            pcall(function() SZ.page:Hide(); SetCreateStartZone(0); end);
            SZ_Log("tick error: " .. SZ.broken);
            self:SetScript("OnUpdate", nil);
        end
    end);
    SZ_Log("installed");
end

-- The creator's frames do not exist yet while this file loads, so build the page on the first frame that has them.
local waiter = CreateFrame("Frame");
waiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton) then return; end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(Install);
    if not ok then
        SZ.broken = tostring(err);
        SZ_Log("install failed: " .. SZ.broken);
    end
end);
end)();
