-- Fate trait picker (wowdecomp)
-- Choosing starting traits: one widget, used by the creator's FATE step and by the in-game Codex's rechoose dialog
-- (build_shell_mpq.py ships this file and fate_traits.lua into the EsteriaPerks addon). Data: EsteriaFateTraits.
-- The rules here are a preview; the server's PerkTraitRules.h decides. A global table and one closure: the creator
-- chunk is near Lua's 200-locals-per-function limit.
EsteriaFatePicker = {};
(function()
local ES, T, P = EsteriaStyle, EsteriaFateTraits, EsteriaFatePicker;
local GOLD, GOLD_DIM, TEXT, MUTED, GOOD, BAD = ES.GOLD, ES.GOLD_DIM, ES.TEXT, ES.MUTED, ES.GOOD, ES.BAD;
local ROWS, ROW_H, W = 11, 30, 340;
local KINDS = { "all", "merit", "flaw", "quirk", "chosen" };
local KIND_TEXT = { all = "ALL", merit = "MERITS", flaw = "FLAWS", quirk = "QUIRKS", chosen = "CHOSEN" };
local CAP = { quirk = T.maxQuirks, merit = T.maxMerits, flaw = T.maxFlaws };
local NEED = { "a mana pool", "a combat pet", "healing spells" };
local NEED_CLASS = {
    { PALADIN = 1, HUNTER = 1, PRIEST = 1, SHAMAN = 1, MAGE = 1, WARLOCK = 1, DRUID = 1 },
    { HUNTER = 1, DEATHKNIGHT = 1, WARLOCK = 1 },
    { PALADIN = 1, PRIEST = 1, SHAMAN = 1, DRUID = 1 },
};

local byId, cats = {}, { "All categories" };
for _, trait in ipairs(T.list) do
    byId[trait.id] = trait;
    local known;
    for _, cat in ipairs(cats) do known = known or cat == trait.cat; end
    if not known then cats[#cats + 1] = trait.cat; end
end
P.byId = byId;

-- ---- rules (chosen is a set: chosen[id] = true) ---------------------------------------------------------------

local function Shares(a, b)
    for _, g in ipairs(a.groups or {}) do
        for _, h in ipairs(b.groups or {}) do if g == h then return true; end end
    end
end

local function Usable(trait, class)
    return not trait.need or not class or NEED_CLASS[trait.need][class] ~= nil;
end

-- Points granted by flaws, points spent on merits, points left, and how many of each kind.
function P.Budget(chosen)
    local flaws, spent, count = 0, 0, { quirk = 0, merit = 0, flaw = 0 };
    for id in pairs(chosen) do
        local trait = byId[id];
        if trait then
            count[trait.kind] = count[trait.kind] + 1;
            if trait.kind == "flaw" then flaws = flaws + trait.pts; end
            if trait.kind == "merit" then spent = spent + trait.pts; end
        end
    end
    return flaws, spent, T.base + flaws - spent, count;
end

-- Why `trait` cannot be added to the build, or nil. Going over budget is allowed while editing.
function P.Blocked(trait, chosen, class)
    if trait.lock then return trait.lock; end
    if not Usable(trait, class) then return "Your class has no use for it: needs " .. NEED[trait.need]; end
    local flaws, _, _, count = P.Budget(chosen);
    if count[trait.kind] >= CAP[trait.kind] then return "At most " .. CAP[trait.kind] .. " " .. trait.kind .. "s"; end
    for id in pairs(chosen) do
        if byId[id] and Shares(trait, byId[id]) then return "Cannot be combined with " .. byId[id].name; end
    end
    if trait.kind == "flaw" and flaws + trait.pts > T.maxFlawPoints then
        return "Flaws grant at most " .. T.maxFlawPoints .. " points";
    end
end

-- Why the whole build cannot be used, or nil when it is allowed.
function P.Problem(chosen, class)
    local _, _, left = P.Budget(chosen);
    for id in pairs(chosen) do
        local trait, rest = byId[id], {};
        if not trait then return "Unknown trait " .. tostring(id); end
        for other in pairs(chosen) do if other ~= id then rest[other] = true; end end
        local why = P.Blocked(trait, rest, class);
        if why then return trait.name .. ": " .. why; end
    end
    if left < 0 then return "Over budget by " .. -left .. (left == -1 and " point" or " points"); end
end

function P.Sorted(chosen)
    local ids = {};
    for id in pairs(chosen) do ids[#ids + 1] = id; end
    table.sort(ids);
    return ids;
end

function P.Points(trait)
    if trait.kind == "merit" then return "-" .. trait.pts; end
    if trait.kind == "flaw" then return "+" .. trait.pts; end
    return "0";
end

-- ---- widget ---------------------------------------------------------------------------------------------------

local function Wrapped(parent, size, color, width)
    local fs = ES.Text(parent, size, color);
    fs:SetWidth(width);
    fs:SetJustifyH("LEFT");
    return fs;
end

local function Flat(parent, width, height, text, onClick)
    local b = CreateFrame("Button", nil, parent);
    b:SetSize(width, height);
    ES.Solid(b, "BACKGROUND", ES.INK, 0.85):SetAllPoints(b);
    ES.Solid(b, "HIGHLIGHT", GOLD, 0.12):SetAllPoints(b);
    b.edges = ES.Border(b, GOLD_DIM, 0.7);
    b.label = ES.Text(b, 10, TEXT);
    b.label:SetPoint("CENTER", b, "CENTER", 0, 0);
    b.label:SetText(text);
    b:SetScript("OnClick", onClick);
    function b.SetOn(on)
        local color = on and GOLD or TEXT;
        b.label:SetTextColor(color[1], color[2], color[3]);
        ES.Tint(b.edges, on and GOLD or GOLD_DIM, on and 1 or 0.7);
    end
    return b;
end

-- Returns the picker: .list and .detail are frames (340 wide) for the host to place; :SetChosen(ids), :Ids(),
-- :Problem(), :SetClass(token) and .onChange (called after every pick) are the rest of the interface.
function P.Create(parent, name)
    local pk = { chosen = {}, kind = "all", cat = 1, search = "", offset = 0, view = {}, rows = {}, hideLocked = false };
    local list = CreateFrame("Frame", name and (name .. "List") or nil, parent);
    list:SetSize(W, 118 + ROWS * ROW_H + 44);
    pk.list = list;

    pk.budget = ES.Text(list, 18, GOLD);
    pk.budget:SetPoint("TOPLEFT", list, "TOPLEFT", 0, 0);
    pk.status = Wrapped(list, 11, MUTED, W);
    pk.status:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -24);

    pk.tabs = {};
    for index, kind in ipairs(KINDS) do
        local tab = Flat(list, 64, 22, KIND_TEXT[kind], function() pk.kind = kind; pk.offset = 0; pk:Refresh(); end);
        tab:SetPoint("TOPLEFT", list, "TOPLEFT", (index - 1) * 69, -52);
        pk.tabs[kind] = tab;
    end

    local search = CreateFrame("EditBox", name and (name .. "Search") or nil, list);
    search:SetSize(150, 22);
    search:SetPoint("TOPLEFT", list, "TOPLEFT", 4, -82);
    search:SetAutoFocus(false);
    search:SetFontObject(GlueFontHighlightSmall or GameFontHighlightSmall);
    search:SetMaxLetters(24);
    ES.Solid(search, "BACKGROUND", ES.INK, 0.85):SetAllPoints(search);
    ES.Border(search, GOLD_DIM, 0.7);
    search.hint = ES.Text(search, 10, MUTED);
    search.hint:SetPoint("LEFT", search, "LEFT", 4, 0);
    search.hint:SetText("Search");
    search:SetScript("OnTextChanged", function(self)
        pk.search = string.lower(self:GetText() or "");
        if pk.search == "" then self.hint:Show(); else self.hint:Hide(); end
        pk.offset = 0;
        pk:Refresh();
    end);
    search:SetScript("OnEscapePressed", function(self) self:ClearFocus(); end);
    search:SetScript("OnEnterPressed", function(self) self:ClearFocus(); end);
    pk.searchBox = search;

    pk.catButton = Flat(list, 178, 22, "", function()
        pk.cat = pk.cat % #cats + 1;
        pk.offset = 0;
        pk:Refresh();
    end);
    pk.catButton:SetPoint("TOPLEFT", list, "TOPLEFT", 162, -82);

    for index = 1, ROWS do
        local row = CreateFrame("Button", name and (name .. "Row" .. index) or nil, list);
        row:SetSize(W, ROW_H - 2);
        row:SetPoint("TOPLEFT", list, "TOPLEFT", 0, -112 - (index - 1) * ROW_H);
        ES.Solid(row, "BACKGROUND", { 1, 1, 1 }, 0.04):SetAllPoints(row);
        ES.Solid(row, "HIGHLIGHT", GOLD, 0.10):SetAllPoints(row);
        row.accent = ES.Solid(row, "ARTWORK", GOLD, 1);
        row.accent:SetWidth(2);
        row.accent:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0);
        row.accent:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 0, 0);
        row.points = ES.Text(row, 13, TEXT);
        row.points:SetPoint("LEFT", row, "LEFT", 8, 0);
        row.name = ES.Text(row, 12, TEXT);
        row.name:SetPoint("LEFT", row, "LEFT", 48, 0);
        row.mark = ES.Text(row, 9, MUTED);
        row.mark:SetPoint("RIGHT", row, "RIGHT", -8, 0);
        row:SetScript("OnEnter", function() if row.trait then pk:Show(row.trait); end end);
        row:SetScript("OnClick", function() if row.trait then pk:Toggle(row.trait); end end);
        pk.rows[index] = row;
    end
    list:EnableMouseWheel(true);
    list:SetScript("OnMouseWheel", function(_, delta) pk:Scroll(-delta * 3); end);

    local below = -112 - ROWS * ROW_H - 4;
    pk.up = Flat(list, 26, 20, "<", function() pk:Scroll(-ROWS); end);
    pk.up:SetPoint("TOPLEFT", list, "TOPLEFT", 0, below);
    pk.down = Flat(list, 26, 20, ">", function() pk:Scroll(ROWS); end);
    pk.down:SetPoint("TOPLEFT", list, "TOPLEFT", 30, below);
    pk.range = ES.Text(list, 10, MUTED);
    pk.range:SetPoint("LEFT", pk.down, "RIGHT", 8, 0);
    pk.lockToggle = Flat(list, 130, 20, "", function() pk.hideLocked = not pk.hideLocked; pk.offset = 0; pk:Refresh(); end);
    pk.lockToggle:SetPoint("TOPRIGHT", list, "TOPRIGHT", 0, below);
    pk.counts = ES.Text(list, 11, MUTED);
    pk.counts:SetPoint("TOPLEFT", list, "TOPLEFT", 0, below - 26);

    local detail = CreateFrame("Frame", name and (name .. "Detail") or nil, parent);
    detail:SetSize(W, 250);
    pk.detail = detail;
    local d = {};
    d.name = ES.Text(detail, 18, GOLD);
    d.name:SetPoint("TOPLEFT", detail, "TOPLEFT", 0, 0);
    d.kind = ES.Text(detail, 11, GOLD_DIM);
    d.kind:SetPoint("TOPLEFT", d.name, "BOTTOMLEFT", 0, -6);
    d.upLabel = ES.Text(detail, 10, GOLD_DIM);
    d.upLabel:SetPoint("TOPLEFT", d.kind, "BOTTOMLEFT", 0, -14);
    d.upLabel:SetText("ADVANTAGE");
    d.up = Wrapped(detail, 13, GOOD, W);
    d.up:SetPoint("TOPLEFT", d.upLabel, "BOTTOMLEFT", 0, -4);
    d.downLabel = ES.Text(detail, 10, GOLD_DIM);
    d.downLabel:SetPoint("TOPLEFT", d.up, "BOTTOMLEFT", 0, -10);
    d.downLabel:SetText("DRAWBACK");
    d.down = Wrapped(detail, 13, BAD, W);
    d.down:SetPoint("TOPLEFT", d.downLabel, "BOTTOMLEFT", 0, -4);
    d.note = Wrapped(detail, 11, MUTED, W);
    d.note:SetPoint("TOPLEFT", d.down, "BOTTOMLEFT", 0, -12);
    pk.text = d;

    -- The rows to draw: tab, category, search and the locked toggle, in catalog order.
    function pk:Filter()
        local view = {};
        for _, trait in ipairs(T.list) do
            local keep = self.kind == "all" or self.kind == trait.kind or (self.kind == "chosen" and self.chosen[trait.id]);
            keep = keep and (self.cat == 1 or trait.cat == cats[self.cat]) and not (self.hideLocked and trait.lock);
            if keep and self.search ~= "" then
                local hay = string.lower(trait.name .. " " .. (trait.up or "") .. " " .. (trait.down or "") .. " " .. trait.cat);
                keep = string.find(hay, self.search, 1, true) ~= nil;
            end
            if keep then view[#view + 1] = trait; end
        end
        self.view = view;
    end

    function pk:Refresh()
        self:Filter();
        local view = self.view;
        self.offset = math.max(0, math.min(self.offset, #view - ROWS));
        for index, row in ipairs(self.rows) do
            local trait = view[self.offset + index];
            row.trait = trait;
            if trait then
                local on = self.chosen[trait.id];
                local blocked = not on and P.Blocked(trait, self.chosen, self.class);
                local color = on and GOLD or blocked and MUTED or TEXT;
                row.points:SetText(P.Points(trait));
                -- red = costs points, green = gives points, grey = free (a quirk's drawback pays for its advantage)
                local pc = trait.kind == "merit" and BAD or trait.kind == "flaw" and GOOD or MUTED;
                row.points:SetTextColor(pc[1], pc[2], pc[3]);
                row.name:SetText(trait.name);
                row.name:SetTextColor(color[1], color[2], color[3]);
                row.mark:SetText(on and "CHOSEN" or trait.lock and "COMING LATER" or blocked and "UNAVAILABLE"
                    or string.upper(trait.kind));
                if on then row.accent:Show(); else row.accent:Hide(); end
                row:Show();
            else
                row:Hide();
            end
        end
        for kind, tab in pairs(self.tabs) do tab.SetOn(kind == self.kind); end
        self.catButton.label:SetText(string.upper(cats[self.cat]));
        self.lockToggle.label:SetText(self.hideLocked and "LOCKED: HIDDEN" or "LOCKED: SHOWN");
        self.range:SetText(#view == 0 and "Nothing matches"
            or (self.offset + 1) .. "-" .. math.min(self.offset + ROWS, #view) .. " of " .. #view);

        local flaws, spent, left, count = P.Budget(self.chosen);
        self.budget:SetText("POINTS  " .. left);
        local bc = left < 0 and BAD or left == 0 and MUTED or GOLD;
        self.budget:SetTextColor(bc[1], bc[2], bc[3]);
        local problem = P.Problem(self.chosen, self.class);
        local color = problem and BAD or GOOD;
        self.status:SetText("Base " .. T.base .. "  +  flaws " .. flaws .. "  -  merits " .. spent .. "  =  " .. left .. "\n"
            .. (problem and ("NOT VALID: " .. problem) or "VALID. Unspent points are simply lost."));
        self.status:SetTextColor(color[1], color[2], color[3]);
        self.counts:SetText("Selected: " .. count.quirk .. "/" .. CAP.quirk .. " Quirks  -  " .. count.merit .. "/" .. CAP.merit
            .. " Merits  -  " .. count.flaw .. "/" .. CAP.flaw .. " Flaws");
        if self.shown then self:Show(self.shown); end
    end

    function pk:Scroll(by)
        self.offset = self.offset + by;
        self:Refresh();
    end

    function pk:Show(trait)
        self.shown = trait;
        local text, on = self.text, self.chosen[trait.id];
        text.name:SetText(string.upper(trait.name));
        text.kind:SetText(string.upper(trait.kind) .. "  -  " .. (trait.kind == "merit" and ("costs " .. trait.pts .. " points")
            or trait.kind == "flaw" and ("grants " .. trait.pts .. " points") or "free: the drawback pays for the advantage")
            .. "  -  " .. trait.cat);
        text.up:SetText(trait.up or "None");
        text.down:SetText(trait.down or "None");
        local lines = {};
        local blocked = not on and P.Blocked(trait, self.chosen, self.class);
        if on then lines[#lines + 1] = "Chosen. Click it again to remove it."; end
        if blocked then lines[#lines + 1] = "Cannot be added: " .. blocked .. "."; end
        if trait.need then lines[#lines + 1] = "Needs " .. NEED[trait.need] .. "."; end
        local rivals = {};
        for _, other in ipairs(T.list) do
            if other ~= trait and Shares(trait, other) then rivals[#rivals + 1] = other.name; end
        end
        if #rivals > 0 then lines[#lines + 1] = "Cannot be combined with: " .. table.concat(rivals, ", ") .. "."; end
        lines[#lines + 1] = "Has no effect in battlegrounds, arenas, duels or fights between players.";
        text.note:SetText(table.concat(lines, "\n"));
    end

    function pk:Toggle(trait)
        if self.chosen[trait.id] then
            self.chosen[trait.id] = nil;
        elseif P.Blocked(trait, self.chosen, self.class) then
            self:Show(trait);
            return;
        else
            self.chosen[trait.id] = true;
        end
        if PlaySound then PlaySound("gsCharacterCreationLook"); end
        self.shown = trait;
        self:Refresh();
        if self.onChange then self.onChange(); end
    end

    function pk:SetChosen(ids)
        self.chosen = {};
        for _, id in ipairs(ids or {}) do self.chosen[id] = true; end
        self:Refresh();
    end

    function pk:SetClass(class)
        if self.class == class then return; end
        self.class = class;
        self:Refresh();
    end

    function pk:Ids() return P.Sorted(self.chosen); end
    function pk:Problem() return P.Problem(self.chosen, self.class); end

    pk:Show(T.list[1]);
    pk:Refresh();
    return pk;
end
end)();
