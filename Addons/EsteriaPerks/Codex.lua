-- The Perk Codex: Paths of Fate in game, drawn in the character creator's style (esteria_style.lua).
-- The server holds the state (Extensions/EsteriaPerks); the Codex shows the last snapshot, sends purchases and
-- respecs, and animates only from the server's answers.

BINDING_HEADER_ESTERIAPERKS = "Esteria: Paths of Fate";
BINDING_NAME_ESTERIAPERKS_TOGGLE = "Paths of Fate";

local ES, D = EsteriaStyle, EsteriaPerksData;
local GOLD, GOLD_DIM, INK, TEXT, MUTED, GOOD, BAD = ES.GOLD, ES.GOLD_DIM, ES.INK, ES.TEXT, ES.MUTED, ES.GOOD, ES.BAD;
local Solid, Text, Border, Tint, Tween = ES.Solid, ES.Text, ES.Border, ES.Tint, ES.Tween;

local W, H, HEADER_H, RAIL_W, DETAIL_W, FOOTER_H = 940, 680, 96, 200, 276, 30;
local BOARD_X = RAIL_W + 18;
local BOARD_W = W - RAIL_W - DETAIL_W - 36;
local CARD_W, CARD_H, CARD_GAP, BAND_H, LEGENDARY_H = 136, 86, 15, 122, 100;
local SUMMARY = 6; -- the rail's seventh row
local LOCK_LEVEL, LOCK_PATH, LOCK_POINTS, LOCK_LEGENDARY, LOCK_FEAT, LOCK_PLANNED = 3, 4, 5, 6, 7, 8;
local ACTION_PURCHASE, ACTION_RESPEC = 1, 2;
local LEGENDARY = 3;

local C = { path = 0, cards = {}, rail = {}, level = 0 };
EsteriaCodex = C; -- for /dump while testing

-- ---- small parts ---------------------------------------------------------------------------------------------

local function Hue(path) return D.PATHS[path] and D.PATHS[path].color or GOLD; end

local function Label(parent, size, color, text, layer)
    local fs = Text(parent, size, color, layer);
    if text then fs:SetText(text); end
    return fs;
end

local function SetColor(fs, color) fs:SetTextColor(color[1], color[2], color[3]); end
local function Shown(region, on) if on then region:Show(); else region:Hide(); end end

local function Wrapped(parent, size, color, width)
    local fs = Label(parent, size, color);
    fs:SetWidth(width);
    fs:SetJustifyH("LEFT");
    return fs;
end

local function Format(card, rank)
    local text = D.CARDS[card] and D.CARDS[card].text or "";
    local values = D.CARDS[card] and D.CARDS[card].values or {};
    -- Not string.format: the texts carry literal percents ("below 20% health").
    local value = string.gsub(values[rank] or values[#values] or "", "%%", "%%%%");
    return (string.gsub(text, "%%s", value, 1));
end

-- A flat button: ink fill, gold-dim edge that turns gold on hover, gold caps label.
local function FlatButton(parent, width, height, text, size)
    local button = CreateFrame("Button", nil, parent);
    button:SetSize(width, height);
    Solid(button, "BACKGROUND", INK, 0.9):SetAllPoints(button);
    button.edges = Border(button, GOLD_DIM, 0.9);
    Solid(button, "HIGHLIGHT", GOLD, 0.12):SetAllPoints(button);
    button.label = Label(button, size or 12, GOLD, text);
    button.label:SetPoint("CENTER", button, "CENTER", 0, 0);
    button:SetScript("OnEnter", function(self) if self.on ~= false then Tint(self.edges, GOLD, 1); end end);
    button:SetScript("OnLeave", function(self) Tint(self.edges, GOLD_DIM, 0.9); end);
    function button:SetOn(on)
        self.on = on;
        SetColor(self.label, on and GOLD or MUTED);
        self:SetAlpha(on and 1 or 0.6);
    end
    return button;
end

-- A tooltip in the kit's look.
local tip;
local function Tip(owner, title, lines)
    if not tip then
        tip = CreateFrame("Frame", nil, UIParent);
        tip:SetFrameStrata("TOOLTIP");
        Solid(tip, "BACKGROUND", INK, 0.96):SetAllPoints(tip);
        Border(tip, GOLD_DIM, 1);
        tip.title = Label(tip, 13, GOLD);
        tip.title:SetPoint("TOPLEFT", tip, "TOPLEFT", 10, -9);
        tip.lines = {};
    end
    tip.title:SetText(title);
    local height, width, previous = 30, tip.title:GetStringWidth() + 20, tip.title;
    for i = 1, math.max(#lines, #tip.lines) do
        local line = tip.lines[i];
        if not line then
            line = Wrapped(tip, 11, TEXT, 260);
            tip.lines[i] = line;
        end
        line:ClearAllPoints();
        line:SetPoint("TOPLEFT", previous, "BOTTOMLEFT", 0, -4);
        if lines[i] then
            line:SetText(lines[i][1]);
            SetColor(line, lines[i][2] or TEXT);
            line:Show();
            height = height + line:GetHeight() + 4;
            width = math.max(width, math.min(280, line:GetStringWidth() + 20));
            previous = line;
        else
            line:Hide();
        end
    end
    tip:SetSize(width, height + 6);
    tip:ClearAllPoints();
    tip:SetPoint("TOPLEFT", owner, "BOTTOMLEFT", 0, -4);
    tip:SetClampedToScreen(true);
    tip:Show();
end
local function HideTip() if tip then tip:Hide(); end end

-- ---- snapshot helpers ----------------------------------------------------------------------------------------

local function Snap() return C.snap; end

local function CardsOf(path)
    local list = {};
    if not C.snap then return list; end
    for _, card in ipairs(C.snap.cards) do
        if card.path == path then list[#list + 1] = card; end
    end
    table.sort(list, function(a, b) return a.id < b.id; end);
    return list;
end

local function SpentIn(path)
    local total = 0;
    if not C.snap then return 0; end
    for _, card in ipairs(C.snap.cards) do
        if card.path == path then total = total + card.rank * card.cost; end
    end
    return total;
end

local function CardById(id)
    if not C.snap then return nil; end
    for _, card in ipairs(C.snap.cards) do if card.id == id then return card; end end
    return nil;
end

local function Unspent() return C.snap and math.max(0, C.snap.earned - C.snap.spent) or 0; end

local function OwnsLegendary(except)
    if not C.snap then return false; end
    for _, card in ipairs(C.snap.cards) do
        if card.tier == LEGENDARY and card.rank > 0 and card.id ~= except then return true; end
    end
    return false;
end

-- Short state for the card face: a word, its colour.
local function CardState(card)
    if card.rank >= card.maxRank then return "MAXED", GOLD; end
    if card.lock == 0 then
        return card.rank > 0 and ("RANK " .. card.rank .. " OF " .. card.maxRank) or "AVAILABLE", GOOD;
    end
    if card.rank > 0 then return "RANK " .. card.rank .. " OF " .. card.maxRank, TEXT; end
    local tier = D.TIERS[card.tier];
    if card.lock == LOCK_LEVEL then return "LOCKED · LEVEL " .. tier.level, MUTED; end
    if card.lock == LOCK_PATH then return "LOCKED · " .. tier.points .. " IN PATH", MUTED; end
    if card.lock == LOCK_POINTS then return "NEEDS A POINT", MUTED; end
    if card.lock == LOCK_FEAT then return "LOCKED · FEAT", MUTED; end
    if card.lock == LOCK_PLANNED then return "PREVIEW", MUTED; end
    if card.lock == LOCK_LEGENDARY then return "LEGENDARY TAKEN", MUTED; end
    return "LOCKED", MUTED;
end

-- ---- frame ---------------------------------------------------------------------------------------------------

local f = CreateFrame("Frame", "EsteriaCodexFrame", UIParent);
f:SetSize(W, H);
f:SetPoint("CENTER", UIParent, "CENTER", 0, 20);
f:SetFrameStrata("HIGH");
f:SetToplevel(true);
f:EnableMouse(true);
f:SetMovable(true);
f:SetClampedToScreen(true);
f:RegisterForDrag("LeftButton");
f:SetScript("OnDragStart", function(self) self:StartMoving(); end);
f:SetScript("OnDragStop", function(self) self:StopMovingOrSizing(); end);
f:Hide();
f:SetScript("OnHide", function(self)
    C.confirm = nil;
    if C.confirmFrame then C.confirmFrame:Hide(); end
    HideTip();
    self:EnableKeyboard(false);
    self.keys = nil;
end);
C.frame = f;
if UISpecialFrames then table.insert(UISpecialFrames, "EsteriaCodexFrame"); end

Solid(f, "BACKGROUND", INK, 0.95):SetAllPoints(f);
Border(f, GOLD_DIM, 0.9);
-- Soft shadow past the edges, like the creator's side panels.
for _, side in ipairs({ "LEFT", "RIGHT" }) do
    local fade = f:CreateTexture(nil, "BACKGROUND");
    fade:SetTexture(ES.WHITE);
    fade:SetWidth(40);
    fade:SetPoint("TOP" .. (side == "LEFT" and "RIGHT" or "LEFT"), f, "TOP" .. side, 0, 0);
    fade:SetPoint("BOTTOM" .. (side == "LEFT" and "RIGHT" or "LEFT"), f, "BOTTOM" .. side, 0, 0);
    if side == "LEFT" then fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0, INK[1], INK[2], INK[3], 0.5);
    else fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0.5, INK[1], INK[2], INK[3], 0); end
end
C.topRule = Solid(f, "OVERLAY", GOLD, 1);
C.topRule:SetHeight(2);
C.topRule:SetPoint("TOP", f, "TOP", 0, 0);

local close = FlatButton(f, 26, 22, "X", 12);
close:SetPoint("TOPRIGHT", f, "TOPRIGHT", -12, -12);
close:SetScript("OnClick", function() EsteriaPerks_Toggle(false); end);

-- ---- header --------------------------------------------------------------------------------------------------

local header = CreateFrame("Frame", nil, f);
header:SetPoint("TOPLEFT", f, "TOPLEFT", 0, 0);
header:SetSize(W, HEADER_H);
local headerRule = Solid(header, "BORDER", GOLD_DIM, 0.6);
headerRule:SetHeight(1);
headerRule:SetPoint("BOTTOMLEFT", header, "BOTTOMLEFT", 18, 0);
headerRule:SetPoint("BOTTOMRIGHT", header, "BOTTOMRIGHT", -18, 0);

local portraitBox = CreateFrame("Frame", nil, header);
portraitBox:SetSize(72, 72);
portraitBox:SetPoint("TOPLEFT", header, "TOPLEFT", 18, -12);
Solid(portraitBox, "BACKGROUND", { 0, 0, 0 }, 0.6):SetAllPoints(portraitBox);
Border(portraitBox, GOLD_DIM, 1);
local portrait = CreateFrame("PlayerModel", nil, portraitBox);
portrait:SetPoint("TOPLEFT", portraitBox, "TOPLEFT", 1, -1);
portrait:SetPoint("BOTTOMRIGHT", portraitBox, "BOTTOMRIGHT", -1, 1);
C.portrait = portrait;

C.name = Label(header, 20, GOLD);
C.name:SetPoint("TOPLEFT", portraitBox, "TOPRIGHT", 16, -6);
C.subtitle = Label(header, 12, MUTED);
C.subtitle:SetPoint("TOPLEFT", C.name, "BOTTOMLEFT", 0, -5);

local trait = CreateFrame("Button", nil, header);
trait:SetSize(180, 22);
trait:SetPoint("TOPLEFT", C.subtitle, "BOTTOMLEFT", 0, -8);
Solid(trait, "BACKGROUND", INK, 0.9):SetAllPoints(trait);
trait.edges = Border(trait, GOLD_DIM, 0.8);
trait.label = Label(trait, 11, GOLD);
trait.label:SetPoint("LEFT", trait, "LEFT", 8, 0);
trait:SetScript("OnEnter", function(self)
    Tint(self.edges, GOLD, 1);
    local t = D.TRAITS[C.snap and C.snap.trait or 0] or D.TRAITS[0];
    Tip(self, "BACKGROUND · " .. string.upper(t.name), { { "+ " .. t.up, GOOD }, { "- " .. t.down, BAD },
        { "Chosen when you were created. Change it once for free in the world.", MUTED } });
end);
trait:SetScript("OnLeave", function(self) Tint(self.edges, GOLD_DIM, 0.8); HideTip(); end);
C.trait = trait;

-- Points: unspent big, spent of earned beside it, a fill bar under both. Its own box, left of the reset button.
local pointsBox = CreateFrame("Frame", nil, header);
pointsBox:SetSize(220, 72);
pointsBox:SetPoint("TOPRIGHT", header, "TOPRIGHT", -214, -12);
C.unspent = Label(pointsBox, 30, GOLD);
C.unspent:SetPoint("TOPLEFT", pointsBox, "TOPLEFT", 0, -2);
local unspentLabel = Label(pointsBox, 11, MUTED, "UNSPENT");
unspentLabel:SetPoint("TOPLEFT", pointsBox, "TOPLEFT", 44, -6);
C.points = Label(pointsBox, 11, TEXT);
C.points:SetPoint("TOPLEFT", unspentLabel, "BOTTOMLEFT", 0, -3);
local barBack = Solid(pointsBox, "ARTWORK", { 1, 1, 1 }, 0.08);
barBack:SetSize(220, 4);
barBack:SetPoint("TOPLEFT", pointsBox, "TOPLEFT", 0, -46);
C.bar = Solid(pointsBox, "OVERLAY", GOLD, 0.9);
C.bar:SetHeight(4);
C.bar:SetPoint("TOPLEFT", barBack, "TOPLEFT", 0, 0);
C.barWidth = 220;
C.nextPoint = Label(pointsBox, 10, MUTED);
C.nextPoint:SetPoint("TOPLEFT", barBack, "BOTTOMLEFT", 0, -5);

local respec = FlatButton(header, 132, 28, "RESET PERKS", 11);
respec:SetPoint("TOPRIGHT", header, "TOPRIGHT", -52, -40);
respec:HookScript("OnEnter", function(self)
    local s = C.snap;
    if not s then return; end
    local cost = s.respecCost > 0 and GetCoinTextureString(s.respecCost) or "Free (your first reset)";
    local lines = { { "Refunds every point you have spent.", TEXT }, { "Cost: " .. cost, GOLD } };
    if s.respecLock ~= 0 then lines[#lines + 1] = { D.RESULTS[s.respecLock] or "", BAD }; end
    Tip(self, "RESET PERKS", lines);
end);
respec:HookScript("OnLeave", HideTip);
respec:SetScript("OnClick", function() C.OpenConfirm({ kind = "respec" }); end);
C.respec = respec;

-- The ribbon that announces a new point.
local ribbon = CreateFrame("Frame", nil, header);
ribbon:SetSize(260, 26);
Solid(ribbon, "BACKGROUND", GOLD, 0.92):SetAllPoints(ribbon);
ribbon.label = Label(ribbon, 12, INK);
ribbon.label:SetShadowOffset(0, 0);
ribbon.label:SetPoint("CENTER", ribbon, "CENTER", 0, 0);
ribbon:Hide();
C.ribbon = ribbon;

-- ---- path rail -----------------------------------------------------------------------------------------------

local rail = CreateFrame("Frame", nil, f);
rail:SetPoint("TOPLEFT", f, "TOPLEFT", 0, -HEADER_H);
rail:SetSize(RAIL_W, H - HEADER_H - FOOTER_H);
local railEdge = Solid(rail, "BORDER", GOLD_DIM, 0.5);
railEdge:SetWidth(1);
railEdge:SetPoint("TOPRIGHT", rail, "TOPRIGHT", 0, -14);
railEdge:SetPoint("BOTTOMRIGHT", rail, "BOTTOMRIGHT", 0, 14);
C.railAccent = Solid(rail, "OVERLAY", GOLD, 1);
C.railAccent:SetSize(3, 46);
C.railWash = Solid(rail, "ARTWORK", GOLD, 0.08);
C.railWash:SetSize(RAIL_W - 1, 52);

local RAIL_ROW = 64;
local function RailY(index) return -16 - index * RAIL_ROW; end

for index = 0, SUMMARY do
    local row = CreateFrame("Button", nil, rail);
    row:SetSize(RAIL_W - 1, 52);
    row.index = index;
    Solid(row, "HIGHLIGHT", GOLD, 0.06):SetAllPoints(row);
    local path = D.PATHS[index];
    row.number = Label(row, 11, GOLD_DIM, index == SUMMARY and "·" or tostring(index + 1));
    row.number:SetPoint("TOPLEFT", row, "TOPLEFT", 18, -8);
    row.swatch = Solid(row, "ARTWORK", path and path.color or GOLD, 1);
    row.swatch:SetSize(6, 6);
    row.swatch:SetPoint("LEFT", row.number, "RIGHT", 6, 0);
    row.name = Label(row, 13, TEXT, path and path.name or "SUMMARY");
    row.name:SetPoint("LEFT", row.swatch, "RIGHT", 8, 0);
    row.motto = Label(row, 10, MUTED, path and path.motto or "Everything you carry");
    row.motto:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3);
    row.spent = Label(row, 11, GOLD_DIM);
    row.spent:SetPoint("TOPRIGHT", row, "TOPRIGHT", -14, -8);
    if path then
        row.barBack = Solid(row, "ARTWORK", { 1, 1, 1 }, 0.07);
        row.barBack:SetSize(RAIL_W - 50, 2);
        row.barBack:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 32, 4);
        row.bar = Solid(row, "OVERLAY", path.color, 0.9);
        row.bar:SetHeight(2);
        row.bar:SetPoint("LEFT", row.barBack, "LEFT", 0, 0);
        row.bar:SetWidth(0.01);
    end
    row:SetScript("OnClick", function() C.SetPath(index); end);
    row:SetScript("OnEnter", function(self)
        if not path then return; end
        local spent = SpentIn(index);
        local nextTier;
        for tier = 0, LEGENDARY do if spent < D.TIERS[tier].points then nextTier = tier; break; end end
        local lines = { { path.identity, TEXT }, { spent .. " point" .. (spent == 1 and "" or "s") .. " spent in this path.", GOLD } };
        if nextTier then
            lines[#lines + 1] = { D.TIERS[nextTier].name .. " cards need " .. D.TIERS[nextTier].points .. " points here and level "
                .. D.TIERS[nextTier].level .. ".", MUTED };
        end
        Tip(self, (index + 1) .. "  " .. path.name, lines);
    end);
    row:SetScript("OnLeave", HideTip);
    C.rail[index] = row;
end

local function PlaceRailRow(index, p)
    local row = C.rail[index];
    row:ClearAllPoints();
    row:SetPoint("TOPLEFT", rail, "TOPLEFT", -12 * (1 - p), RailY(index));
    row:SetAlpha(p);
end

local function MoveRailAccent(from, to)
    Tween("codex.railAccent", 0.24, 0, function(p)
        local y = RailY(from) + (RailY(to) - RailY(from)) * p;
        C.railAccent:ClearAllPoints();
        C.railAccent:SetPoint("TOPLEFT", rail, "TOPLEFT", 0, y - 3);
        C.railWash:ClearAllPoints();
        C.railWash:SetPoint("TOPLEFT", rail, "TOPLEFT", 0, y);
    end);
end

-- Rank bars in `width` px from (x, y) up off the anchor's bottom-left: one segment per rank, `gap` apart.
local function LayoutRankBars(bars, data, anchor, width, x, y, height, gap)
    local each = (width - (data.maxRank - 1) * gap) / data.maxRank;
    for i, bar in ipairs(bars) do
        local used = i <= data.maxRank;
        for _, part in ipairs({ bar.edge, bar.fill }) do
            part:ClearAllPoints();
            part:SetPoint("BOTTOMLEFT", anchor, "BOTTOMLEFT", x + (i - 1) * (each + gap), y);
            part:SetSize(each, height);
        end
        bar.height = height;
        Shown(bar.edge, used);
        Shown(bar.fill, used and i <= data.rank);
        bar.fill:SetAlpha(1);
    end
end

-- ---- board ---------------------------------------------------------------------------------------------------

local board = CreateFrame("Frame", nil, f);
board:SetPoint("TOPLEFT", f, "TOPLEFT", BOARD_X, -HEADER_H);
board:SetSize(BOARD_W, H - HEADER_H - FOOTER_H);
C.board = board;

C.boardTitle = Label(board, 17, GOLD);
C.boardTitle:SetPoint("TOPLEFT", board, "TOPLEFT", 0, -16);
C.boardMotto = Label(board, 12, MUTED);
C.boardMotto:SetPoint("LEFT", C.boardTitle, "RIGHT", 10, 0);

local BANDS_TOP = -50;
C.bands = {};
for tier = 0, LEGENDARY do
    local band = {};
    local y = BANDS_TOP - tier * BAND_H;
    band.label = Label(board, 11, GOLD_DIM, D.TIERS[tier].name);
    band.label:SetPoint("TOPLEFT", board, "TOPLEFT", 0, y);
    band.gate = Label(board, 10, MUTED);
    band.gate:SetPoint("TOPRIGHT", board, "TOPRIGHT", 0, y);
    band.rule = Solid(board, "BORDER", GOLD_DIM, 0.35);
    band.rule:SetHeight(1);
    band.rule:SetPoint("LEFT", band.label, "RIGHT", 8, 0);
    band.rule:SetPoint("RIGHT", band.gate, "LEFT", -8, 0);
    band.y = y - 18;
    C.bands[tier] = band;
end

-- The status line shown over the board while there is no snapshot.
C.status = Wrapped(board, 13, MUTED, BOARD_W - 40);
C.status:SetJustifyH("CENTER");
C.status:SetPoint("CENTER", board, "CENTER", 0, 20);

local function BuildCard(tier, col)
    local wide = tier == LEGENDARY;
    local card = CreateFrame("Button", nil, board);
    card:SetSize(wide and BOARD_W or CARD_W, wide and LEGENDARY_H or CARD_H);
    card.tier, card.col = tier, col;
    card.baseX, card.baseY = col * (CARD_W + CARD_GAP), C.bands[tier].y;
    card.enter, card.lift = 0, 0;
    card.liftKey, card.enterKey = {}, {};

    card.glow = Solid(card, "BACKGROUND", GOLD, 0);
    card.glow:SetPoint("TOPLEFT", card, "TOPLEFT", -4, 4);
    card.glow:SetPoint("BOTTOMRIGHT", card, "BOTTOMRIGHT", 4, -4);
    card.glow:SetBlendMode("ADD");
    card.fill = Solid(card, "BACKGROUND", INK, 0.97);
    card.fill:SetAllPoints(card);
    card.wash = card:CreateTexture(nil, "BORDER");
    card.wash:SetTexture(ES.WHITE);
    card.wash:SetPoint("TOPLEFT", card, "TOPLEFT", 1, -1);
    card.wash:SetPoint("TOPRIGHT", card, "TOPRIGHT", -1, -1);
    card.wash:SetHeight(36);
    card.edges = Border(card, GOLD_DIM, 0.8);

    card.iconBox = CreateFrame("Frame", nil, card);
    card.iconBox:SetSize(30, 30);
    card.iconBox:SetPoint("TOPLEFT", card, "TOPLEFT", 8, -8);
    card.icon = card.iconBox:CreateTexture(nil, "ARTWORK");
    card.icon:SetPoint("TOPLEFT", card.iconBox, "TOPLEFT", 1, -1);
    card.icon:SetPoint("BOTTOMRIGHT", card.iconBox, "BOTTOMRIGHT", -1, 1);
    card.icon:SetTexCoord(0.07, 0.93, 0.07, 0.93);
    Border(card.iconBox, GOLD_DIM, 1);

    -- The tag has its own line above the name, so the two never meet.
    card.badge = Label(card, 8, GOLD_DIM);
    card.badge:SetPoint("TOPRIGHT", card, "TOPRIGHT", -6, -5);
    card.name = Wrapped(card, 11, TEXT, (wide and BOARD_W or CARD_W) - 50);
    card.name:SetPoint("TOPLEFT", card.iconBox, "TOPRIGHT", 6, -9);
    card.state = Label(card, 9, MUTED);
    card.state:SetPoint("BOTTOMLEFT", card, "BOTTOMLEFT", 8, 13);
    if wide then
        card.blurb = Wrapped(card, 10, MUTED, BOARD_W - 60);
        card.blurb:SetPoint("TOPLEFT", card.name, "BOTTOMLEFT", 0, -5);
    end

    -- Rank bars: one segment per rank along the bottom edge, gold when learned. Laid out by PaintCard.
    card.pips = {};
    for i = 1, 2 do
        card.pips[i] = { edge = Solid(card, "ARTWORK", GOLD_DIM, 0.35), fill = Solid(card, "OVERLAY", GOLD, 1) };
    end

    -- Effects: a burst on purchase, a sweep when it unlocks, the legendary shimmer along the edges.
    card.burst = Solid(card, "OVERLAY", GOLD, 0);
    card.burst:SetBlendMode("ADD");
    card.burst:SetPoint("CENTER", card, "CENTER", 0, 0);
    card.sweep = card:CreateTexture(nil, "OVERLAY");
    card.sweep:SetTexture(ES.WHITE);
    card.sweep:SetBlendMode("ADD");
    card.sweep:SetSize(48, (wide and LEGENDARY_H or CARD_H) - 2);
    card.sweep:SetGradientAlpha("HORIZONTAL", GOLD[1], GOLD[2], GOLD[3], 0, GOLD[1], GOLD[2], GOLD[3], 0.45);
    card.sweep:Hide();
    if wide then
        card.shimmer = {};
        for i, point in ipairs({ "TOP", "BOTTOM" }) do
            local s = card:CreateTexture(nil, "OVERLAY");
            s:SetTexture(ES.WHITE);
            s:SetBlendMode("ADD");
            s:SetSize(90, 2);
            s:SetGradientAlpha("HORIZONTAL", 1, 0.9, 0.6, 0, 1, 0.9, 0.6, 0.9);
            s.point = point;
            card.shimmer[i] = s;
        end
    end

    card:SetScript("OnEnter", function(self)
        if not self.data then return; end
        C.hover = self;
        Tint(self.edges, GOLD, 1);
        Tween(self.liftKey, 0.14, 0, function(p) self.lift = 2 * p; self.glow:SetAlpha(0.18 * p); C.PlaceCard(self); end);
        C.ShowDetail(self.data, false);
    end);
    card:SetScript("OnLeave", function(self)
        if C.hover == self then C.hover = nil; end
        C.PaintCard(self);
        local from = self.lift;
        Tween(self.liftKey, 0.14, 0, function(p) self.lift = from * (1 - p); self.glow:SetAlpha(0.18 * (1 - p)); C.PlaceCard(self); end);
        local selected = C.selected and CardById(C.selected);
        if selected then C.ShowDetail(selected, false); end
    end);
    card:SetScript("OnClick", function(self) if self.data then C.Select(self.data.id, true); end end);
    return card;
end

local SLOTS = {};
for tier = 0, LEGENDARY do
    for col = 0, (tier == 0 or tier == 1) and 2 or (tier == 2 and 1 or 0) do
        local card = BuildCard(tier, col);
        SLOTS[#SLOTS + 1] = card;
        C.cards[#C.cards + 1] = card;
    end
end

function C.PlaceCard(card)
    card:ClearAllPoints();
    card:SetPoint("TOPLEFT", C.board, "TOPLEFT", card.baseX + (C.slide or 0), card.baseY + card.enter + card.lift);
end

function C.PaintCard(card)
    local data = card.data;
    if not data then card:Hide(); return; end
    local info = D.CARDS[data.id] or { name = "Perk " .. data.id, icon = "Interface\\Icons\\INV_Misc_QuestionMark" };
    local hue = Hue(data.path);
    card.icon:SetTexture(info.icon);
    card.name:SetText(info.name);
    if card.blurb then card.blurb:SetText(Format(data.id, 1)); end
    local word, color = CardState(data);
    card.state:SetText(word);
    SetColor(card.state, color);
    card.badge:SetText(data.planned and "PREVIEW" or (data.cost > 1 and data.cost .. " POINTS" or ""));
    card.wash:SetGradientAlpha("VERTICAL", hue[1], hue[2], hue[3], 0, hue[1], hue[2], hue[3], 0.16);

    local open = data.rank > 0 or data.lock == 0;
    card.icon:SetDesaturated(not open);
    card.icon:SetAlpha(open and 1 or 0.55);
    SetColor(card.name, open and TEXT or MUTED);
    local legendary = data.tier == LEGENDARY;
    local edge = (C.hover == card or C.selected == data.id) and GOLD
        or (legendary or data.rank >= data.maxRank) and GOLD or GOLD_DIM;
    Tint(card.edges, edge, (legendary or C.selected == data.id) and 1 or 0.8);
    LayoutRankBars(card.pips, data, card, card:GetWidth() - 16, 8, 6, 3, 4);
    if card.shimmer then
        for _, s in ipairs(card.shimmer) do Shown(s, not ES.reduceMotion); end
    end
    card:Show();
end

-- Fills the slots with the path's cards (Steady Focus is missing for classes that cannot use it).
local function FillBoard(path)
    for _, card in ipairs(SLOTS) do card.data = nil; end
    local byTier = {};
    for _, data in ipairs(CardsOf(path)) do
        byTier[data.tier] = byTier[data.tier] or {};
        table.insert(byTier[data.tier], data);
    end
    for _, card in ipairs(SLOTS) do
        local list = byTier[card.tier];
        card.data = list and list[card.col + 1] or nil;
        C.PaintCard(card);
        C.PlaceCard(card);
    end
end

-- The summary: everything owned, grouped under its path, in a list the mouse wheel scrolls. The trait and the
-- totals sit in the detail panel (C.ShowOverview).
local summary = CreateFrame("ScrollFrame", nil, board);
summary:SetPoint("TOPLEFT", board, "TOPLEFT", 0, BANDS_TOP);
summary:SetPoint("BOTTOMRIGHT", board, "BOTTOMRIGHT", 0, 22);
local list = CreateFrame("Frame", nil, summary);
list:SetSize(BOARD_W, 10);
summary:SetScrollChild(list);
summary:EnableMouseWheel(true);
summary:Hide();
C.summaryFrame, C.summaryList = summary, list;
C.more = Label(board, 9, GOLD_DIM, "SCROLL FOR MORE");
C.more:SetPoint("BOTTOMRIGHT", board, "BOTTOMRIGHT", 0, 6);
C.more:Hide();

local function ScrollSummary(offset)
    local most = math.max(0, list:GetHeight() - summary:GetHeight());
    C.summaryScroll = math.max(0, math.min(most, offset));
    summary:SetVerticalScroll(C.summaryScroll);
    Shown(C.more, C.summaryScroll < most - 1);
end
summary:SetScript("OnMouseWheel", function(_, delta) ScrollSummary((C.summaryScroll or 0) - delta * 48); end);

C.summaryHeads, C.summaryEntries = {}, {};

local function SummaryHead(i)
    local head = C.summaryHeads[i];
    if not head then
        head = { name = Label(list, 11, GOLD), points = Label(list, 10, GOLD_DIM), rule = Solid(list, "BORDER", GOLD_DIM, 0.35) };
        head.rule:SetHeight(1);
        C.summaryHeads[i] = head;
    end
    return head;
end

local function SummaryEntry(i)
    local entry = C.summaryEntries[i];
    if not entry then
        entry = {
            accent = Solid(list, "ARTWORK", GOLD, 0.8),
            name = Label(list, 12, TEXT),
            rank = Label(list, 11, GOLD),
            tag = Label(list, 8, GOLD_DIM),
            text = Wrapped(list, 11, TEXT, BOARD_W - 24),
        };
        entry.accent:SetWidth(2);
        C.summaryEntries[i] = entry;
    end
    return entry;
end

local function ShowSummary()
    local owned = {};
    if C.snap then
        for _, data in ipairs(C.snap.cards) do if data.rank > 0 then owned[#owned + 1] = data; end end
    end
    table.sort(owned, function(a, b) return a.id < b.id; end);

    local y, heads, lastPath = 0, 0, nil;
    for i, data in ipairs(owned) do
        if data.path ~= lastPath then
            lastPath = data.path;
            heads = heads + 1;
            local head, hue = SummaryHead(heads), Hue(data.path);
            if heads > 1 then y = y + 10; end
            head.name:ClearAllPoints();
            head.name:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -y);
            head.name:SetText((data.path + 1) .. "  " .. D.PATHS[data.path].name);
            SetColor(head.name, hue);
            head.points:ClearAllPoints();
            head.points:SetPoint("TOPRIGHT", list, "TOPRIGHT", -4, -y - 1);
            local spent = SpentIn(data.path);
            head.points:SetText(spent .. " PT" .. (spent == 1 and "" or "S"));
            head.rule:ClearAllPoints();
            head.rule:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -y - 16);
            head.rule:SetPoint("TOPRIGHT", list, "TOPRIGHT", -4, -y - 16);
            head.rule:SetVertexColor(hue[1], hue[2], hue[3], 0.35);
            for _, part in pairs(head) do part:Show(); end
            y = y + 24;
        end
        local entry, info = SummaryEntry(i), D.CARDS[data.id] or { name = "Perk " .. data.id };
        local top = y;
        entry.name:ClearAllPoints();
        entry.name:SetPoint("TOPLEFT", list, "TOPLEFT", 12, -y);
        entry.name:SetText(info.name);
        entry.rank:ClearAllPoints();
        entry.rank:SetPoint("TOPRIGHT", list, "TOPRIGHT", -4, -y);
        entry.rank:SetText(data.rank .. " / " .. data.maxRank);
        entry.tag:ClearAllPoints();
        entry.tag:SetPoint("RIGHT", entry.rank, "LEFT", -10, 0);
        entry.tag:SetText(data.planned and "PREVIEW" or "");
        entry.text:ClearAllPoints();
        entry.text:SetPoint("TOPLEFT", list, "TOPLEFT", 12, -y - 17);
        entry.text:SetText(Format(data.id, data.rank));
        SetColor(entry.text, data.planned and MUTED or TEXT);
        y = y + 17 + entry.text:GetHeight();
        local hue = Hue(data.path);
        entry.accent:ClearAllPoints();
        entry.accent:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -top);
        entry.accent:SetHeight(y - top);
        entry.accent:SetVertexColor(hue[1], hue[2], hue[3], 0.8);
        for _, part in pairs(entry) do part:Show(); end
        y = y + 12;
    end
    for i = heads + 1, #C.summaryHeads do for _, part in pairs(C.summaryHeads[i]) do part:Hide(); end end
    for i = #owned + 1, #C.summaryEntries do for _, part in pairs(C.summaryEntries[i]) do part:Hide(); end end

    list:SetHeight(math.max(10, y));
    summary:Show();
    ScrollSummary(0);
    if #owned == 0 then
        C.status:SetText("No perks yet. Spend a point in any path; the first comes at level 10.");
        C.status:Show();
    end
    C.ShowOverview(owned);
end

local function HideSummary()
    summary:Hide();
    C.more:Hide();
    if C.overview then C.overview:Hide(); end
end

-- ---- detail --------------------------------------------------------------------------------------------------

local detail = CreateFrame("Frame", nil, f);
detail:SetPoint("TOPRIGHT", f, "TOPRIGHT", 0, -HEADER_H);
detail:SetSize(DETAIL_W, H - HEADER_H - FOOTER_H);
local detailEdge = Solid(detail, "BORDER", GOLD_DIM, 0.5);
detailEdge:SetWidth(1);
detailEdge:SetPoint("TOPLEFT", detail, "TOPLEFT", 0, -14);
detailEdge:SetPoint("BOTTOMLEFT", detail, "BOTTOMLEFT", 0, 14);
C.detail = detail;

local DW = DETAIL_W - 40;
local body = CreateFrame("Frame", nil, detail);
body:SetPoint("TOPLEFT", detail, "TOPLEFT", 20, -18);
body:SetSize(DW, 400);
C.body = body;

local dIconBox = CreateFrame("Frame", nil, body);
dIconBox:SetSize(52, 52);
dIconBox:SetPoint("TOPLEFT", body, "TOPLEFT", 0, 0);
C.dIcon = dIconBox:CreateTexture(nil, "ARTWORK");
C.dIcon:SetPoint("TOPLEFT", dIconBox, "TOPLEFT", 1, -1);
C.dIcon:SetPoint("BOTTOMRIGHT", dIconBox, "BOTTOMRIGHT", -1, 1);
C.dIcon:SetTexCoord(0.07, 0.93, 0.07, 0.93);
C.dIconEdges = Border(dIconBox, GOLD_DIM, 1);
C.dName = Wrapped(body, 16, GOLD, DW - 62);
C.dName:SetPoint("TOPLEFT", dIconBox, "TOPRIGHT", 10, -2);
C.dKind = Label(body, 10, MUTED);
C.dKind:SetPoint("TOPLEFT", C.dName, "BOTTOMLEFT", 0, -4);
C.dPips = {};
for i = 1, 2 do C.dPips[i] = { edge = Solid(body, "ARTWORK", GOLD_DIM, 0.35), fill = Solid(body, "OVERLAY", GOLD, 1) }; end
-- The rank line under the icon: bars on the left, the words after them.
C.dRankRow = CreateFrame("Frame", nil, body);
C.dRankRow:SetSize(DW, 14);
C.dRankRow:SetPoint("TOPLEFT", dIconBox, "BOTTOMLEFT", 0, -10);
C.dRank = Label(body, 11, TEXT);
C.dRank:SetPoint("LEFT", C.dRankRow, "LEFT", 108, 0);

-- Detail lines in order; each anchors under the one before it that is shown.
local LINES = { "nowHead", "now", "nextHead", "next", "reqHead", "req1", "req2", "req3", "req4", "note1", "note2" };
C.dLines = {};
for _, key in ipairs(LINES) do
    local head = string.find(key, "Head");
    local fs = Wrapped(body, head and 10 or 12, head and GOLD_DIM or TEXT, key:find("^req%d") and DW - 18 or DW);
    fs.isHead = head and true or nil;
    C.dLines[key] = fs;
    if key:find("^req%d") then
        local box = CreateFrame("Frame", nil, body);
        box:SetSize(9, 9);
        box:SetPoint("TOPRIGHT", fs, "TOPLEFT", -8, -2);
        fs.box = box;
        fs.boxFill = Solid(box, "ARTWORK", GOLD, 1);
        fs.boxFill:SetAllPoints(box);
        fs.boxEdges = Border(box, GOLD_DIM, 1);
    end
end

local buy = FlatButton(detail, DW, 32, "", 12);
buy:SetPoint("BOTTOMLEFT", detail, "BOTTOMLEFT", 20, 22);
buy:SetScript("OnClick", function()
    local data = C.shown and CardById(C.shown);
    if data and data.lock == 0 and data.rank < data.maxRank then
        C.OpenConfirm({ kind = "buy", id = data.id, rank = data.rank + 1 });
    end
end);
C.buy = buy;

C.message = Wrapped(detail, 11, BAD, DW);
C.message:SetJustifyH("CENTER");
C.message:SetPoint("BOTTOM", buy, "TOP", 0, 10);
C.message:SetAlpha(0);

local function SetLine(key, text, color)
    local fs = C.dLines[key];
    if not text or text == "" then
        fs:Hide();
        if fs.box then fs.box:Hide(); end
        return;
    end
    fs:SetText(text);
    SetColor(fs, color or TEXT);
    fs:Show();
    if fs.box then fs.box:Show(); end
end

local function Requirement(key, met, text)
    SetLine(key, text, met and TEXT or MUTED);
    local fs = C.dLines[key];
    if not text then return; end
    Shown(fs.boxFill, met);
    Tint(fs.boxEdges, met and GOLD or GOLD_DIM, 1);
end

-- Chains the shown lines under the pips; a head or a new section gets more room above it.
local function StackLines()
    local previous, previousIndent, first = C.dRankRow, 0, true;
    for _, key in ipairs(LINES) do
        local fs = C.dLines[key];
        if fs:IsShown() then
            local indent = fs.box and 18 or 0;
            local gap = first and -18 or ((fs.isHead or key == "note1") and -14 or (previous.isHead and -4 or -6));
            fs:ClearAllPoints();
            fs:SetPoint("TOPLEFT", previous, "BOTTOMLEFT", indent - previousIndent, gap);
            previous, previousIndent, first = fs, indent, false;
        end
    end
end

-- staged: animate the lines in one after another (selection); otherwise swap at once (hover).
function C.ShowDetail(data, staged)
    C.shown = data and data.id;
    if not data then body:Hide(); buy:Hide(); return; end
    body:Show();
    buy:Show();
    local info = D.CARDS[data.id] or { name = "Perk " .. data.id, icon = "Interface\\Icons\\INV_Misc_QuestionMark", text = "" };
    local tier, path = D.TIERS[data.tier], D.PATHS[data.path];
    C.dIcon:SetTexture(info.icon);
    C.dIcon:SetDesaturated(data.rank == 0 and data.lock ~= 0);
    Tint(C.dIconEdges, data.tier == LEGENDARY and GOLD or GOLD_DIM, 1);
    C.dName:SetText(info.name);
    C.dKind:SetText(tier.name .. "  ·  " .. path.name .. (data.cost > 1 and ("  ·  " .. data.cost .. " POINTS") or ""));
    LayoutRankBars(C.dPips, data, C.dRankRow, 96, 0, 5, 4, 6);
    C.dRank:SetText("RANK " .. data.rank .. " OF " .. data.maxRank);

    SetLine("nowHead", "NOW");
    SetLine("now", data.rank > 0 and Format(data.id, data.rank) or "Not learned.", data.rank > 0 and TEXT or MUTED);
    if data.rank < data.maxRank then
        SetLine("nextHead", data.rank > 0 and "NEXT RANK" or "RANK 1");
        SetLine("next", Format(data.id, data.rank + 1), GOLD);
    else
        SetLine("nextHead", nil);
        SetLine("next", nil);
    end

    -- Why locked: the gates as a checklist; the server's lock code decides, these explain it.
    if data.rank < data.maxRank then
        local level, spent = C.level, SpentIn(data.path);
        SetLine("reqHead", "REQUIRES");
        Requirement("req1", level >= tier.level, "Level " .. tier.level .. (level < tier.level and ("  (you are " .. level .. ")") or ""));
        Requirement("req2", tier.points == 0 or spent >= tier.points,
            tier.points > 0 and (tier.points .. " points in " .. path.name .. "  (" .. spent .. " spent)") or nil);
        Requirement("req3", Unspent() >= data.cost, data.cost .. " unspent point" .. (data.cost > 1 and "s" or "")
            .. "  (" .. Unspent() .. " left)");
        if data.tier == LEGENDARY then
            Requirement("req4", data.lock ~= LOCK_FEAT and not OwnsLegendary(data.id),
                (info.feat or "Its feat.") .. " One legendary at a time.");
        else
            Requirement("req4", true, nil);
        end
    else
        SetLine("reqHead", nil);
        for i = 1, 4 do Requirement("req" .. i, true, nil); end
    end
    SetLine("note1", data.planned and "Preview: this perk has no effect yet." or nil, MUTED);
    SetLine("note2", info.pvp, MUTED);
    StackLines();

    -- Learn button: what it would do, or why it cannot.
    if data.rank >= data.maxRank then
        buy.label:SetText("FULLY LEARNED");
        buy:SetOn(false);
    elseif data.lock == 0 then
        buy.label:SetText("LEARN RANK " .. (data.rank + 1) .. "  ·  " .. data.cost .. " POINT" .. (data.cost > 1 and "S" or ""));
        buy:SetOn(true);
    else
        buy.label:SetText(string.upper(CardState(data)));
        buy:SetOn(false);
    end

    if staged then
        local index = 0;
        for _, key in ipairs(LINES) do
            local fs = C.dLines[key];
            if fs:IsShown() then
                index = index + 1;
                Tween("codex.line." .. key, 0.22, index * 0.03, function(p) fs:SetAlpha(p); end);
            end
        end
        Tween("codex.detailHead", 0.2, 0, function(p) C.dName:SetAlpha(p); C.dIcon:SetAlpha(p); end);
    else
        for _, key in ipairs(LINES) do C.dLines[key]:SetAlpha(1); end
        C.dName:SetAlpha(1);
        C.dIcon:SetAlpha(1);
    end
end

-- The detail panel on the summary page: the background trait and what the build adds up to.
local overview = CreateFrame("Frame", nil, detail);
overview:SetPoint("TOPLEFT", detail, "TOPLEFT", 20, -18);
overview:SetSize(DW, 400);
overview:Hide();
C.overview = overview;
local OV = {};
for _, spec in ipairs({
    { "head", 10, GOLD_DIM, "BACKGROUND", -0 }, { "trait", 16, GOLD, nil, -4 }, { "up", 12, GOOD, nil, -8 },
    { "down", 12, BAD, nil, -4 }, { "traitNote", 10, MUTED, nil, -6 },
    { "head2", 10, GOLD_DIM, "YOUR BUILD", -20 }, { "points", 12, TEXT, nil, -6 }, { "perks", 12, TEXT, nil, -4 },
    { "active", 12, TEXT, nil, -4 }, { "previews", 12, MUTED, nil, -4 },
    { "head3", 10, GOLD_DIM, "PATHS", -20 },
}) do
    local fs = Wrapped(overview, spec[2], spec[3], DW);
    if spec[4] then fs:SetText(spec[4]); end
    fs:SetPoint("TOPLEFT", OV.last or overview, OV.last and "BOTTOMLEFT" or "TOPLEFT", 0, spec[5]);
    OV[spec[1]] = fs;
    OV.last = fs;
end
-- One row per path with points in it: name on the left, points on the right, a bar under both.
OV.paths = {};
for i = 0, SUMMARY - 1 do
    local row = { name = Label(overview, 11, TEXT), points = Label(overview, 10, GOLD_DIM),
        back = Solid(overview, "ARTWORK", { 1, 1, 1 }, 0.07), bar = Solid(overview, "OVERLAY", GOLD, 0.9) };
    row.back:SetSize(DW, 2);
    row.bar:SetHeight(2);
    OV.paths[i] = row;
end

function C.ShowOverview(owned)
    local s = C.snap;
    overview:Show();
    local trait = D.TRAITS[s and s.trait or 0] or D.TRAITS[0];
    OV.trait:SetText(string.upper(trait.name));
    OV.up:SetText("+ " .. trait.up);
    OV.down:SetText("- " .. trait.down);
    OV.traitNote:SetText("Chosen at creation. Change it once for free in the world.");
    local active, previews = 0, 0;
    for _, data in ipairs(owned) do if data.planned then previews = previews + 1; else active = active + 1; end end
    OV.points:SetText((s and s.spent or 0) .. " of " .. (s and s.earned or 0) .. " points spent, " .. Unspent() .. " unspent");
    OV.perks:SetText(#owned .. " perk" .. (#owned == 1 and "" or "s") .. " learned");
    OV.active:SetText(active .. " working now");
    OV.previews:SetText(previews > 0 and (previews .. " preview" .. (previews == 1 and "" or "s") .. " with no effect yet") or "");
    local previous, y = OV.head3, -10;
    for i = 0, SUMMARY - 1 do
        local row, spent = OV.paths[i], SpentIn(i);
        local on = spent > 0;
        for _, part in pairs(row) do Shown(part, on); end
        if on then
            local hue = Hue(i);
            row.name:ClearAllPoints();
            row.name:SetPoint("TOPLEFT", previous, "BOTTOMLEFT", 0, y);
            row.name:SetText(D.PATHS[i].name);
            SetColor(row.name, hue);
            row.points:ClearAllPoints();
            row.points:SetPoint("RIGHT", row.name, "LEFT", DW, 0);
            row.points:SetText(spent .. " PT" .. (spent == 1 and "" or "S"));
            row.back:ClearAllPoints();
            row.back:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -4);
            row.bar:ClearAllPoints();
            row.bar:SetPoint("TOPLEFT", row.back, "TOPLEFT", 0, 0);
            row.bar:SetWidth(math.max(0.01, DW * math.min(1, spent / 15)));
            row.bar:SetVertexColor(hue[1], hue[2], hue[3], 0.9);
            previous, y = row.back, -8;
        end
    end
end

local function Message(text, color)
    C.message:SetText(text or "");
    SetColor(C.message, color or BAD);
    Tween("codex.message", 0.2, 0, function(p) C.message:SetAlpha(p); end);
    C.messageUntil = GetTime() + 3.5;
end

-- ---- confirm -------------------------------------------------------------------------------------------------

local confirm = CreateFrame("Frame", nil, detail);
confirm:SetSize(DW, 132);
confirm:SetFrameLevel(detail:GetFrameLevel() + 10);
Solid(confirm, "BACKGROUND", INK, 0.99):SetAllPoints(confirm);
Border(confirm, GOLD, 1);
confirm.title = Wrapped(confirm, 13, GOLD, DW - 24);
confirm.title:SetPoint("TOPLEFT", confirm, "TOPLEFT", 12, -12);
confirm.text = Wrapped(confirm, 11, TEXT, DW - 24);
confirm.text:SetPoint("TOPLEFT", confirm.title, "BOTTOMLEFT", 0, -6);
confirm.yes = FlatButton(confirm, (DW - 30) / 2, 28, "CONFIRM", 11);
confirm.yes:SetPoint("BOTTOMLEFT", confirm, "BOTTOMLEFT", 10, 10);
confirm.no = FlatButton(confirm, (DW - 30) / 2, 28, "CANCEL", 11);
confirm.no:SetPoint("BOTTOMRIGHT", confirm, "BOTTOMRIGHT", -10, 10);
confirm:Hide();
C.confirmFrame = confirm;

local function PlaceConfirm(p, shake)
    confirm:ClearAllPoints();
    confirm:SetPoint("BOTTOMLEFT", detail, "BOTTOMLEFT", 20 + (shake or 0), 22 + 40 * p);
end

function C.OpenConfirm(spec)
    if C.pending or not C.snap then return; end
    local s = C.snap;
    if spec.kind == "buy" then
        local data = CardById(spec.id);
        local info = D.CARDS[spec.id] or { name = "Perk " .. spec.id };
        if not data then return; end
        confirm.title:SetText("Learn " .. info.name .. (data.maxRank > 1 and (", rank " .. spec.rank) or "") .. "?");
        confirm.text:SetText("Costs " .. data.cost .. " point" .. (data.cost > 1 and "s" or "") .. ".  Unspent: "
            .. Unspent() .. " to " .. (Unspent() - data.cost) .. "."
            .. (data.tier == LEGENDARY and "\nYou can carry only one legendary." or "")
            .. (data.planned and "\nPreview: it has no effect yet." or ""));
    else
        if s.respecLock ~= 0 and s.respecLock ~= 12 then Message(D.RESULTS[s.respecLock]); return; end
        local cost = s.respecCost > 0 and GetCoinTextureString(s.respecCost) or "free, your first reset";
        confirm.title:SetText("Reset every perk?");
        confirm.text:SetText("Refunds " .. s.spent .. " point" .. (s.spent == 1 and "" or "s") .. ".  Spent: " .. s.spent
            .. " to 0,  unspent: " .. Unspent() .. " to " .. s.earned .. ".\nCost: " .. cost .. "."
            .. (s.respecLock == 12 and "\nYou cannot afford it." or ""));
    end
    C.confirm = spec;
    confirm.yes:SetOn(not (spec.kind == "respec" and s.respecLock == 12));
    confirm.yes.label:SetText("CONFIRM");
    confirm:Show();
    PlaySound("igMainMenuOptionCheckBoxOn");
    Tween("codex.confirm", 0.22, 0, function(p) confirm:SetAlpha(p); PlaceConfirm(p); end);
end

function C.CloseConfirm()
    if not C.confirm or C.pending then return; end
    C.confirm = nil;
    Tween("codex.confirm", 0.14, 0, function(p)
        confirm:SetAlpha(1 - p);
        PlaceConfirm(1 - p);
        if p >= 1 then confirm:Hide(); end
    end);
end

local function Confirm()
    local spec = C.confirm;
    if not spec or C.pending or confirm.yes.on == false then return; end
    local sent;
    if spec.kind == "buy" then sent = EsteriaPerks_Purchase(spec.id, spec.rank);
    else sent = EsteriaPerks_Respec(); end
    if not sent then Message("Not connected to the realm."); return; end
    C.pending = spec;
    C.pendingAt = GetTime();
end
confirm.yes:SetScript("OnClick", Confirm);
confirm.no:SetScript("OnClick", function() C.CloseConfirm(); end);

-- ---- footer --------------------------------------------------------------------------------------------------

local footerRule = Solid(f, "BORDER", GOLD_DIM, 0.4);
footerRule:SetHeight(1);
footerRule:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 18, FOOTER_H);
footerRule:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -18, FOOTER_H);
local hint = Label(f, 10, MUTED, "Arrows choose  ·  Enter learns  ·  Tab or 1-7 changes path  ·  Esc closes");
hint:SetPoint("BOTTOM", f, "BOTTOM", 0, 10);
local motion = CreateFrame("Button", nil, f);
motion:SetSize(140, 20);
motion:SetPoint("BOTTOMLEFT", f, "BOTTOMLEFT", 18, 5);
motion.label = Label(motion, 10, GOLD_DIM);
motion.label:SetPoint("LEFT", motion, "LEFT", 0, 0);
motion:SetScript("OnClick", function() EsteriaPerks_SetReduceMotion(not ES.reduceMotion); end);
C.motion = motion;
local brand = Label(f, 10, GOLD_DIM, "PATHS OF FATE");
brand:SetPoint("BOTTOMRIGHT", f, "BOTTOMRIGHT", -18, 10);

-- ---- drawing -------------------------------------------------------------------------------------------------

local function ShowCounter(unspent)
    C.unspent:SetText(tostring(math.floor(unspent + 0.5)));
end

local function DrawHeader(animateFrom)
    local s = C.snap;
    local name, level = UnitName("player"), UnitLevel("player");
    local className = UnitClass("player");
    C.level = level or 0;
    C.name:SetText(name or "");
    C.subtitle:SetText("Level " .. C.level .. " " .. (className or ""));
    local t = D.TRAITS[s and s.trait or 0] or D.TRAITS[0];
    C.trait.label:SetText("BACKGROUND  ·  " .. string.upper(t.name));
    C.trait:SetWidth(C.trait.label:GetStringWidth() + 18);
    if not s then
        C.unspent:SetText("-");
        C.points:SetText("");
        C.bar:SetWidth(0.01);
        C.nextPoint:SetText("");
        C.respec:SetOn(false);
        return;
    end
    local unspent = s.earned - s.spent;
    if animateFrom and animateFrom ~= unspent then
        Tween("codex.counter", 0.5, 0.1, function(p) ShowCounter(animateFrom + (unspent - animateFrom) * p); end);
    else
        ShowCounter(unspent);
    end
    C.points:SetText(s.spent .. " OF " .. s.earned .. " SPENT");
    local width = math.max(0.01, C.barWidth * s.spent / 15);
    local from = C.bar:GetWidth();
    Tween("codex.bar", 0.4, 0.05, function(p) C.bar:SetWidth(math.max(0.01, from + (width - from) * p)); end);
    if C.level < 10 then C.nextPoint:SetText("FIRST POINT AT LEVEL 10");
    elseif s.earned < 15 then C.nextPoint:SetText("NEXT POINT AT LEVEL " .. (10 + s.earned * 5));
    else C.nextPoint:SetText("EVERY POINT EARNED"); end
    C.respec:SetOn(s.spent > 0 and (s.respecLock == 0 or s.respecLock == 12));
end

local function DrawRail()
    for index = 0, SUMMARY do
        local row = C.rail[index];
        local on = index == C.path;
        SetColor(row.name, on and GOLD or TEXT);
        SetColor(row.number, on and GOLD or GOLD_DIM);
        if row.bar then
            local spent = SpentIn(index);
            row.spent:SetText(spent > 0 and (spent .. " PT" .. (spent == 1 and "" or "S")) or "");
            local goal = 6;
            for tier = 1, LEGENDARY do if spent < D.TIERS[tier].points then goal = D.TIERS[tier].points; break; end end
            local width = math.max(0.01, (RAIL_W - 50) * math.min(1, spent / goal));
            local from = row.bar:GetWidth();
            Tween(row.bar, 0.4, 0.05 * index, function(p) row.bar:SetWidth(math.max(0.01, from + (width - from) * p)); end);
        else
            local owned = 0;
            if C.snap then for _, d in ipairs(C.snap.cards) do if d.rank > 0 then owned = owned + 1; end end end
            row.spent:SetText(owned > 0 and tostring(owned) or "");
        end
    end
end

local function DrawBoard()
    C.status:Hide();
    if C.path == SUMMARY then
        C.boardTitle:SetText("SUMMARY");
        SetColor(C.boardTitle, GOLD);
        C.boardMotto:SetText("What your choices do now");
        for tier = 0, LEGENDARY do
            local band = C.bands[tier];
            band.label:Hide(); band.gate:Hide(); band.rule:Hide();
        end
        for _, card in ipairs(SLOTS) do card.data = nil; card:Hide(); end
        ShowSummary();
        return;
    end
    HideSummary();
    local path = D.PATHS[C.path];
    C.boardTitle:SetText((C.path + 1) .. "  " .. path.name);
    SetColor(C.boardTitle, path.color);
    C.boardMotto:SetText(path.motto);
    local spent = SpentIn(C.path);
    for tier = 0, LEGENDARY do
        local band, gate = C.bands[tier], D.TIERS[tier];
        local open = C.level >= gate.level and spent >= gate.points;
        band.label:Show(); band.gate:Show(); band.rule:Show();
        band.gate:SetText("LEVEL " .. gate.level .. (gate.points > 0 and ("  ·  " .. gate.points .. " IN PATH") or "")
            .. (tier == LEGENDARY and "  ·  FEAT" or ""));
        SetColor(band.label, open and GOLD or GOLD_DIM);
        SetColor(band.gate, open and GOOD or MUTED);
    end
    if not C.snap then
        for _, card in ipairs(SLOTS) do card.data = nil; card:Hide(); end
        C.status:SetText(C.statusText or "Consulting the threads of fate...");
        C.status:Show();
        return;
    end
    FillBoard(C.path);
end

function C.Redraw(animateFrom)
    DrawHeader(animateFrom);
    DrawRail();
    DrawBoard();
    local data = (C.hover and C.hover.data) or (C.selected and CardById(C.selected));
    if data and data.path ~= C.path then data = nil; end
    if not data and C.path ~= SUMMARY then
        local first = SLOTS[1].data;
        data = first;
        C.selected = first and first.id or nil;
    end
    C.ShowDetail(C.path ~= SUMMARY and data or nil, false);
    C.motion.label:SetText("MOTION: " .. (ES.reduceMotion and "REDUCED" or "FULL"));
end

-- Cards cascade in band by band; with `from` set the board slides in from that side.
local function Cascade(from)
    local index = 0;
    for _, card in ipairs(SLOTS) do
        if card.data then
            index = index + 1;
            card:SetAlpha(0);
            Tween(card.enterKey, 0.26, 0.03 * index, function(p)
                card.enter = -8 * (1 - p);
                card:SetAlpha(p);
                C.PlaceCard(card);
            end);
        end
    end
    if from then
        Tween("codex.slide", 0.24, 0, function(p)
            C.slide = from * 24 * (1 - p);
            C.boardTitle:SetAlpha(p);
            C.boardMotto:SetAlpha(p);
            for _, card in ipairs(SLOTS) do if card.data then C.PlaceCard(card); end end
        end);
    end
end

function C.SetPath(path, quiet)
    if path == C.path or path < 0 or path > SUMMARY then return; end
    local from = C.path;
    C.path = path;
    C.hover = nil;
    C.selected = nil;
    C.CloseConfirm();
    if not quiet then PlaySound("igCharacterInfoTab"); end
    MoveRailAccent(from, path);
    C.Redraw();
    Cascade(path > from and 1 or -1);
end

function C.Select(id, staged)
    local data = CardById(id);
    if not data then return; end
    C.selected = id;
    C.CloseConfirm();
    for _, card in ipairs(SLOTS) do
        C.PaintCard(card);
        if card.data and card.data.id == id and staged then
            PlaySound("igMainMenuOptionCheckBoxOn");
            Tween("codex.pulse", 0.45, 0, function(p) card.glow:SetAlpha(0.4 * (1 - p)); end);
        end
    end
    C.ShowDetail(data, staged);
end

-- ---- reactions to the server ---------------------------------------------------------------------------------

local function CardFrame(id)
    for _, card in ipairs(SLOTS) do if card.data and card.data.id == id then return card; end end
end

local function Burst(card, big)
    local size = big and 260 or 150;
    Tween(card.burst, big and 0.9 or 0.6, 0, function(p)
        card.burst:SetSize(CARD_H + size * p, CARD_H + size * p * 0.5);
        card.burst:SetAlpha((big and 0.7 or 0.45) * (1 - p));
    end);
end

local function Sweep(card, delay)
    card.sweep:Show();
    Tween(card.sweep, 0.6, delay, function(p)
        card.sweep:ClearAllPoints();
        card.sweep:SetPoint("LEFT", card, "LEFT", -48 + (card:GetWidth() + 48) * p, 0);
        card.sweep:SetAlpha(math.sin(math.pi * p));
        if p >= 1 then card.sweep:Hide(); end
    end);
end

local celebration = Label(f, 26, GOLD, "", "OVERLAY");
celebration:SetPoint("CENTER", board, "CENTER", 0, 40);
celebration:SetAlpha(0);

-- After a purchase: the new pip pops, the card bursts, cards that just opened sweep, a legendary celebrates.
local function Purchased(id, rank, before)
    local card = CardFrame(id);
    local data = CardById(id);
    if card and data then
        local pip = card.pips[rank];
        if pip then
            pip.fill:Show();
            Tween(pip.fill, 0.5, 0, function(p)
                pip.fill:SetHeight(p >= 1 and pip.height or pip.height + 4 * math.sin(math.pi * p));
                pip.fill:SetAlpha(0.4 + 0.6 * p);
            end);
        end
        Burst(card, data.tier == LEGENDARY);
        if data.tier == LEGENDARY then
            PlaySoundFile("Sound\\Interface\\LevelUp.wav");
            celebration:SetText("LEGENDARY  ·  " .. string.upper((D.CARDS[id] or {}).name or ""));
            Tween("codex.celebrate", 2.4, 0, function(p) celebration:SetAlpha(math.min(1, p * 5) * math.min(1, (1 - p) * 3)); end);
            C.shimmerBoost = GetTime() + 2.5;
            Message("Legendary learned: " .. ((D.CARDS[id] or {}).name or "") .. ".", GOLD);
        else
            PlaySound("igQuestListComplete");
        end
    end
    local delay = 0.25;
    for _, other in ipairs(SLOTS) do
        local was = other.data and before[other.data.id];
        if was and was ~= 0 and other.data.lock == 0 then
            Sweep(other, delay);
            delay = delay + 0.08;
        end
    end
end

-- Respec: owned pips drain right to left before the board redraws.
local function Drain()
    local delay = 0;
    for i = #SLOTS, 1, -1 do
        local card = SLOTS[i];
        if card.data and card.data.rank > 0 then
            for r = card.data.rank, 1, -1 do
                local pip = card.pips[r].fill;
                Tween(pip, 0.2, delay, function(p) pip:SetAlpha(1 - p); end);
                delay = delay + 0.04;
            end
        end
    end
    C.holdUntil = GetTime() + math.min(0.8, delay + 0.25);
end

local function Shake()
    PlaySound("igQuestFailed");
    Tween("codex.shake", 0.4, 0, function(p) PlaceConfirm(1, math.sin(p * math.pi * 6) * 7 * (1 - p)); end);
end

local function Ribbon(gained)
    if not C.frame:IsShown() then return; end
    ribbon.label:SetText(gained > 1 and (gained .. " NEW PERK POINTS") or "NEW PERK POINT");
    ribbon:Show();
    Tween(ribbon, 0.45, 0.35, function(p)
        ribbon:ClearAllPoints();
        ribbon:SetPoint("TOPRIGHT", header, "TOPRIGHT", -214 - 120 * (1 - p), -64);
        ribbon:SetAlpha(p);
    end);
    C.ribbonUntil = GetTime() + 4;
end

local function OnSnapshot()
    local before = {};
    local oldUnspent;
    if C.snap then
        oldUnspent = C.snap.earned - C.snap.spent;
        for _, data in ipairs(C.snap.cards) do before[data.id] = data.lock; end
    end
    local oldEarned = C.snap and C.snap.earned or C.lastEarned;
    C.snap = EsteriaPerks_GetSnapshot();
    C.statusText = nil;
    if not C.snap then return; end
    if oldEarned and C.snap.earned > oldEarned then
        C.newPoints = (C.newPoints or 0) + (C.snap.earned - oldEarned);
        if DEFAULT_CHAT_FRAME then
            DEFAULT_CHAT_FRAME:AddMessage("|cffe8c46bPaths of Fate:|r you earned a perk point. Open the Codex to spend it.");
        end
    end
    C.lastEarned = C.snap.earned;
    if not C.frame:IsShown() then return; end
    local finish = function()
        C.Redraw(oldUnspent);
        if C.bought then Purchased(C.bought.id, C.bought.rank, before); C.bought = nil; end
        if C.newPoints then Ribbon(C.newPoints); C.newPoints = nil; end
    end
    if C.holdUntil and GetTime() < C.holdUntil then C.afterHold = finish; else finish(); end
end

local function OnResult(action, perk, rank, result)
    if result == 14 or result == 10 then
        C.snap = nil;
        C.statusText = D.RESULTS[result];
        if C.frame:IsShown() then C.Redraw(); end
    end
    if action ~= ACTION_PURCHASE and action ~= ACTION_RESPEC then return; end
    local mine = C.pending;
    C.pending = nil;
    if result == 0 then
        if mine then C.CloseConfirm(); end
        if action == ACTION_PURCHASE then C.bought = { id = perk, rank = rank };
        else Drain(); PlaySound("igQuestLogAbandonQuest"); Message("Your perks are reset. Every point is yours to spend again.", GOOD); end
    else
        Message(D.RESULTS[result] or ("Refused (" .. tostring(result) .. ")."));
        if C.confirm then Shake(); end
    end
end

-- ---- open, close, keys ---------------------------------------------------------------------------------------

local function Open()
    C.frame:Show();
    PlaySound("igSpellBookOpen");
    C.portrait:SetUnit("player");
    C.portrait:SetCamera(0);
    if EsteriaPerks_RequestSnapshot then EsteriaPerks_RequestSnapshot(); end
    C.snap = EsteriaPerks_GetSnapshot and EsteriaPerks_GetSnapshot() or C.snap;
    if not EsteriaPerks_RequestSnapshot then C.statusText = "Paths of Fate needs the Esteria client."; end
    C.Redraw();
    MoveRailAccent(C.path, C.path);
    Tween("codex.open", 0.28, 0, function(p) C.frame:SetScale(0.96 + 0.04 * p); C.frame:SetAlpha(p); end);
    Tween("codex.rule", 0.5, 0.08, function(p) C.topRule:SetWidth(math.max(0.01, W * p)); end);
    for index = 0, SUMMARY do Tween(C.rail[index], 0.26, 0.1 + index * 0.03, function(p) PlaceRailRow(index, p); end); end
    Cascade();
    if C.newPoints then Ribbon(C.newPoints); C.newPoints = nil; end
end

local function Close()
    C.confirm = nil;
    confirm:Hide();
    HideTip();
    PlaySound("igSpellBookClose");
    Tween("codex.open", 0.16, 0, function(p)
        C.frame:SetAlpha(1 - p);
        C.frame:SetScale(1 - 0.03 * p);
        if p >= 1 then C.frame:Hide(); C.frame:SetAlpha(1); C.frame:SetScale(1); end
    end);
end

function EsteriaPerks_Toggle(show)
    if show == nil then show = not C.frame:IsShown() or C.frame:GetAlpha() < 1; end
    if show then Open(); else Close(); end
end

-- The micro button passes itself and the mouse button; it only toggles.
function EsteriaPerks_MicroClick() EsteriaPerks_Toggle(); end

function EsteriaPerks_SetReduceMotion(on)
    ES.reduceMotion = on and true or nil;
    EsteriaPerksDB = EsteriaPerksDB or {};
    EsteriaPerksDB.reduceMotion = ES.reduceMotion;
    if C.frame:IsShown() then C.Redraw(); end
end

-- Moves the selection through the board: left/right in a band, up/down between bands.
local function MoveSelection(dx, dy)
    local current;
    for _, card in ipairs(SLOTS) do if card.data and card.data.id == C.selected then current = card; end end
    if not current then
        for _, card in ipairs(SLOTS) do if card.data then return C.Select(card.data.id, true); end end
        return;
    end
    local best, bestScore;
    for _, card in ipairs(SLOTS) do
        if card.data and card ~= current then
            local dt, dc = card.tier - current.tier, card.col - current.col;
            local ok = (dy ~= 0 and dt * dy > 0) or (dx ~= 0 and dt == 0 and dc * dx > 0);
            if ok then
                local score = math.abs(dt) * 10 + math.abs(dc);
                if not bestScore or score < bestScore then best, bestScore = card, score; end
            end
        end
    end
    if best then C.Select(best.data.id, true); end
end

f:SetScript("OnKeyDown", function(_, key)
    if key == "ESCAPE" then
        if C.confirm then C.CloseConfirm(); else Close(); end
    elseif key == "ENTER" then
        if C.confirm then Confirm(); else C.buy:Click(); end
    elseif key == "LEFT" then MoveSelection(-1, 0);
    elseif key == "RIGHT" then MoveSelection(1, 0);
    elseif key == "UP" then MoveSelection(0, -1);
    elseif key == "DOWN" then MoveSelection(0, 1);
    elseif key == "TAB" then C.SetPath((C.path + (IsShiftKeyDown() and SUMMARY or 1)) % (SUMMARY + 1));
    elseif tonumber(key) and tonumber(key) >= 1 and tonumber(key) <= SUMMARY + 1 then C.SetPath(tonumber(key) - 1);
    end
end);

-- Keys go to the Codex only while the cursor is over it, so the game keeps its keys otherwise.
f:SetScript("OnUpdate", function(self)
    local over = MouseIsOver and MouseIsOver(self);
    if over ~= self.keys then self.keys = over; self:EnableKeyboard(over and true or false); end
    local now = GetTime();
    if C.portrait:IsShown() and not ES.reduceMotion then C.portrait:SetFacing(math.sin(now * 0.45) * 0.35); end
    if C.messageUntil and now > C.messageUntil then
        C.messageUntil = nil;
        Tween("codex.message", 0.4, 0, function(p) C.message:SetAlpha(1 - p); end);
    end
    if C.ribbonUntil and now > C.ribbonUntil then
        C.ribbonUntil = nil;
        Tween(ribbon, 0.6, 0, function(p) ribbon:SetAlpha(1 - p); if p >= 1 then ribbon:Hide(); end end);
    end
    if C.holdUntil and now >= C.holdUntil then
        C.holdUntil = nil;
        if C.afterHold then local after = C.afterHold; C.afterHold = nil; after(); end
    end
    -- No answer: let the player try again rather than hang.
    if C.pending and now - (C.pendingAt or now) > 5 then C.pending = nil; Message("The realm did not answer."); end
    if C.pending then
        confirm.yes.label:SetText(string.rep(".", 1 + math.floor(now * 3) % 3));
    end
    -- The legendary card's edges shimmer.
    local card = SLOTS[#SLOTS];
    if card.shimmer and card:IsShown() and not ES.reduceMotion then
        local speed = (C.shimmerBoost and now < C.shimmerBoost) and 420 or 140;
        local width = card:GetWidth();
        local span = width + 90;
        for i, s in ipairs(card.shimmer) do
            -- Cut the streak at the card's edges as it enters and leaves.
            local x = (now * speed + (i - 1) * span / 2) % span - 90;
            local left, right = math.max(0, x), math.min(width, x + 90);
            if right - left < 2 then
                s:Hide();
            else
                s:Show();
                s:SetWidth(right - left);
                s:ClearAllPoints();
                s:SetPoint(s.point .. "LEFT", card, s.point .. "LEFT", left, 0);
            end
        end
    end
end);

-- ---- events --------------------------------------------------------------------------------------------------

local events = CreateFrame("Frame");
events:RegisterEvent("ADDON_LOADED");
events:RegisterEvent("PLAYER_ENTERING_WORLD");
events:RegisterEvent("PLAYER_LEVEL_UP");
events:RegisterEvent("PLAYER_UPDATE_RESTING");
events:RegisterEvent("PLAYER_REGEN_ENABLED");
events:RegisterEvent("PLAYER_MONEY");
events:RegisterEvent("ESTERIA_PERK_SNAPSHOT");
events:RegisterEvent("ESTERIA_PERK_RESULT");
events:SetScript("OnEvent", function(_, event, ...)
    if event == "ADDON_LOADED" then
        if ... == "EsteriaPerks" then ES.reduceMotion = EsteriaPerksDB and EsteriaPerksDB.reduceMotion or nil; end
    elseif event == "ESTERIA_PERK_SNAPSHOT" then
        OnSnapshot();
    elseif event == "ESTERIA_PERK_RESULT" then
        OnResult(...);
    elseif event == "PLAYER_ENTERING_WORLD" or event == "PLAYER_LEVEL_UP" then
        -- Known points let a level-up announce the new one.
        if EsteriaPerks_RequestSnapshot then EsteriaPerks_RequestSnapshot(); end
    elseif C.frame:IsShown() and EsteriaPerks_RequestSnapshot then
        -- Resting, combat and gold change whether a reset is allowed.
        EsteriaPerks_RequestSnapshot();
    end
end);

SLASH_ESTERIAPERKS1 = "/fate";
SLASH_ESTERIAPERKS2 = "/perks";
SlashCmdList["ESTERIAPERKS"] = function(msg)
    if msg == "motion" then
        EsteriaPerks_SetReduceMotion(not ES.reduceMotion);
        DEFAULT_CHAT_FRAME:AddMessage("Paths of Fate: motion " .. (ES.reduceMotion and "reduced." or "full."));
    else
        EsteriaPerks_Toggle();
    end
end;
