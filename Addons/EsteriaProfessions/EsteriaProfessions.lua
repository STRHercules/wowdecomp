-- Esteria's professions window, in the PvP/Collections/Character look, built for characters who know every profession.
--   Rail        every known profession, primary and secondary, with rank bars; Overview on top
--   Overview    a card per profession: rank title, progress, what each one opens
--   Profession  searchable recipes in collapsible categories (difficulty colors, how many you can make), and the recipe:
--               product, reagents with have/need, tools, cooldown, quantity, Create / Create all
-- Opening a profession casts its spell, which only secure code may do, so the rail rows and overview cards are secure
-- action buttons and the window is a secure frame (Escape bound while open). The trade skill API serves one profession
-- at a time; this window follows whichever one the client has open, whoever opened it.
local ES = EsteriaStyle
local W, H = 860, 560
local RAIL_W = 190
local MAIN_W = W - 14 - RAIL_W - 10 - 14
local PANE_H = H - 42 - 20
local LIST_W = 300
local DETAIL_W = MAIN_W - 8 - LIST_W - 8 - 8

local frame, rail, overview, view, gathering
local railRows, cards = {}, {}
local professions = {}
local minor = {}          -- secondary skills that never have recipes (Riding): a line at the foot of the rail
local unlearned = {}      -- professions this character hasn't learned: dim cards on the overview
local open = nil          -- name of the profession the client has open, or nil
local selectedGather      -- a gathering profession shown in the info card
local selectedRecipe
local collapsed = {}
local dirty, pendingArm = true, false
local db

-- DragonUI's professions module would restyle the stock window this one replaces; keep it out.
do
    local dui = _G.DragonUI
    local modules = dui and dui.db and dui.db.profile and dui.db.profile.modules
    if modules and modules.professions then modules.professions.enabled = false end
end

local function shown(region, on) if on then region:Show() else region:Hide() end end
local function locked() return InCombatLockdown() and true or false end

-- Model -------------------------------------------------------------------------------------------------------------
-- English skill names, for the spell that opens each one and an icon when the profession has no spell to borrow from.
local KNOWN = {
    Alchemy = { "Alchemy", "Trade_Alchemy" }, Blacksmithing = { "Blacksmithing", "Trade_BlackSmithing" },
    Enchanting = { "Enchanting", "Trade_Engraving" }, Engineering = { "Engineering", "Trade_Engineering" },
    Inscription = { "Inscription", "INV_Inscription_Tradeskill01" }, Jewelcrafting = { "Jewelcrafting", "INV_Misc_Gem_01" },
    Leatherworking = { "Leatherworking", "INV_Misc_ArmorKit_17" }, Tailoring = { "Tailoring", "Trade_Tailoring" },
    Mining = { "Smelting", "Trade_Mining" }, Herbalism = { nil, "Trade_Herbalism" }, Skinning = { nil, "INV_Misc_Pelt_Wolf_01" },
    Cooking = { "Cooking", "INV_Misc_Food_15" }, ["First Aid"] = { "First Aid", "Spell_Holy_SealOfSacrifice" },
    Fishing = { nil, "Trade_Fishing" },
}
local SECONDARY = { Cooking = true, ["First Aid"] = true, Fishing = true }
local RANKS = { { 75, "Apprentice" }, { 150, "Journeyman" }, { 225, "Expert" }, { 300, "Artisan" }, { 375, "Master" },
                { 450, "Grand Master" } }

local function rankTitle(maxRank)
    for _, r in ipairs(RANKS) do if (maxRank or 0) <= r[1] then return r[2] end end
    return "Grand Master"
end

local function scanProfessions()
    -- Collapsed headers hide their skills; expanding only when one is collapsed avoids an update loop.
    for i = 1, (GetNumSkillLines()) or 0 do
        local _, isHeader, isExpanded = GetSkillLineInfo(i)
        if isHeader and not isExpanded and ExpandSkillHeader then ExpandSkillHeader(0); break end
    end
    professions, minor, unlearned = {}, {}, {}
    local header = ""
    for i = 1, (GetNumSkillLines()) or 0 do
        local name, isHeader, _, rank, _, modifier, maxRank = GetSkillLineInfo(i)
        if isHeader then
            header = name or ""
        elseif name and not KNOWN[name] and header:find("Secondary") then
            minor[#minor + 1] = { name = name, rank = rank or 0, maxRank = maxRank or 0 }
        elseif name and (KNOWN[name] or header:find("Profession")) then
            local known = KNOWN[name]
            local spell = known and known[1] or name
            local spellName, _, icon = nil, nil, nil
            if spell then spellName, _, icon = GetSpellInfo(spell) end
            professions[#professions + 1] = {
                name = name, rank = rank or 0, maxRank = maxRank or 0, modifier = modifier or 0,
                spell = spellName, -- nil: nothing to open (gathering, fishing, or a spell this character lacks)
                icon = icon or ("Interface\\Icons\\" .. (known and known[2] or "INV_Misc_QuestionMark")),
                secondary = SECONDARY[name] or header:find("Secondary") ~= nil,
            }
        end
    end
    local order = function(a, b)
        if a.secondary ~= b.secondary then return not a.secondary end
        return a.name < b.name
    end
    table.sort(professions, order)
    local known = {}
    for _, prof in ipairs(professions) do known[prof.name] = true end
    for name, info in pairs(KNOWN) do
        if not known[name] then
            unlearned[#unlearned + 1] = { name = name, rank = 0, maxRank = 0, modifier = 0, unlearned = true,
                                          icon = "Interface\\Icons\\" .. info[2], secondary = SECONDARY[name] or false }
        end
    end
    table.sort(unlearned, order)
end

local DIFFICULTY = { optimal = { 1, 0.5, 0.25 }, medium = { 1, 1, 0 }, easy = { 0.25, 0.75, 0.25 }, trivial = { 0.5, 0.5, 0.5 } }

-- The open profession's recipes, filtered here (search over names and reagents, materials, skill-ups) and grouped.
local function recipeRows(query)
    -- Collapsed categories hide their recipes from the API; this window collapses its own.
    for i = 1, (GetNumTradeSkills()) or 0 do
        local _, kind, _, isExpanded = GetTradeSkillInfo(i)
        if kind == "header" and not isExpanded and ExpandTradeSkillSubClass then ExpandTradeSkillSubClass(0); break end
    end
    local rows, header = {}, nil
    for i = 1, (GetNumTradeSkills()) or 0 do
        local name, kind, available = GetTradeSkillInfo(i)
        if name then
            if kind == "header" then
                header = { header = true, name = name, count = 0 }
                rows[#rows + 1] = header
            else
                local ok = true
                if db.haveMats and (available or 0) <= 0 then ok = false end
                if db.skillUps and kind == "trivial" then ok = false end
                if ok and query ~= "" and not name:lower():find(query, 1, true) then
                    ok = false
                    for r = 1, (GetTradeSkillNumReagents(i)) or 0 do
                        local reagent = GetTradeSkillReagentInfo(i, r)
                        if reagent and reagent:lower():find(query, 1, true) then ok = true; break end
                    end
                end
                if ok then
                    if header then header.count = header.count + 1 end
                    if not (header and collapsed[header.name]) then
                        rows[#rows + 1] = { index = i, name = name, kind = kind, available = available or 0, group = header }
                    end
                end
            end
        end
    end
    -- Categories left empty by the filters drop out.
    local out = {}
    for _, r in ipairs(rows) do if not r.header or r.count > 0 then out[#out + 1] = r end end
    return out
end

EsteriaProfessions_Test = { scan = scanProfessions, professions = function() return professions end, recipeRows = recipeRows,
                            minor = function() return minor end, unlearned = function() return unlearned end,
                            rankTitle = rankTitle }

-- Widgets ------------------------------------------------------------------------------------------------------------
local function panel(parent)
    local p = CreateFrame("Frame", nil, parent)
    ES.Solid(p, "BACKGROUND", { 0.06, 0.07, 0.10 }, 0.95):SetAllPoints()
    p.lines = ES.Border(p, ES.GOLD_DIM, 0.7)
    return p
end

local function heading(parent, text)
    local h = ES.Text(parent, 10, ES.GOLD_DIM)
    h:SetText(text:upper())
    return h
end

local function gradient(parent, height, alpha)
    local t = parent:CreateTexture(nil, "BORDER"); t:SetTexture(ES.WHITE)
    t:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, alpha or 0.22)
    t:SetPoint("TOPLEFT", 1, -1); t:SetPoint("TOPRIGHT", -1, -1); t:SetHeight(height)
    return t
end

local function button(parent, width, text, primary, height)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height or 26)
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, 0.95); b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0); b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, 11, primary and { 1, 0.92, 0.7 } or ES.TEXT)
    b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12) end end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0) end)
    b.Active = function(self, on)
        if on then self:Enable() else self:Disable(); self.glow:SetAlpha(0) end
        self.label:SetAlpha(on and 1 or 0.35)
    end
    return b
end

local function bar(parent, height)
    local bg = ES.Solid(parent, "ARTWORK", { 0, 0, 0 }, 0.6); bg:SetHeight(height)
    local fill = ES.Solid(parent, "OVERLAY", ES.GOLD, 1); fill:SetPoint("TOPLEFT", bg, "TOPLEFT"); fill:SetHeight(height)
    return bg, fill
end

local function setBar(fill, width, value, color)
    fill:SetWidth(math.max(1, width * math.max(0, math.min(1, value or 0))))
    local c = color or ES.GOLD
    fill:SetVertexColor(c[1], c[2], c[3], 1)
end

local function editBox(parent, width, hint, onEnter, maxLetters)
    local e = CreateFrame("EditBox", nil, parent)
    e:SetSize(width, 24); e:SetAutoFocus(false); e:SetFontObject(ChatFontNormal); e:SetTextInsets(8, 8, 0, 0)
    e:SetMaxLetters(maxLetters or 64)
    ES.Solid(e, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    e.lines = ES.Border(e, ES.GOLD_DIM)
    e.hint = ES.Text(e, 11, ES.MUTED); e.hint:SetPoint("LEFT", 8, 0); e.hint:SetText(hint or "")
    e:SetScript("OnEscapePressed", function(self) if self:GetText() ~= "" then self:SetText("") else self:ClearFocus() end end)
    e:SetScript("OnEnterPressed", function(self) if onEnter then onEnter(self, self:GetText()) else self:ClearFocus() end end)
    e:SetScript("OnEditFocusGained", function(self) ES.Tint(self.lines, ES.GOLD, 1) end)
    e:SetScript("OnEditFocusLost", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 1) end)
    e:HookScript("OnTextChanged", function(self) shown(self.hint, self:GetText() == "") end)
    return e
end

local function toggle(parent, label, key)
    local t = CreateFrame("Button", nil, parent)
    t:SetSize(110, 22)
    t.box = CreateFrame("Frame", nil, t); t.box:SetSize(12, 12); t.box:SetPoint("LEFT", 2, 0)
    ES.Solid(t.box, "BACKGROUND", ES.INK, 1):SetAllPoints()
    t.box.lines = ES.Border(t.box, ES.GOLD_DIM)
    t.fill = ES.Solid(t.box, "ARTWORK", ES.GOLD, 1); t.fill:SetPoint("TOPLEFT", 3, -3); t.fill:SetPoint("BOTTOMRIGHT", -3, 3)
    t.label = ES.Text(t, 10, ES.TEXT); t.label:SetPoint("LEFT", 20, 0); t.label:SetText(label)
    t:SetScript("OnClick", function()
        PlaySound("igMainMenuOptionCheckBoxOn"); db[key] = not db[key] or nil; view.list.offset = 0; dirty = true
    end)
    t.Paint = function(self) shown(self.fill, db[key]); ES.Tint(self.box.lines, db[key] and ES.GOLD or ES.GOLD_DIM, 1) end
    return t
end

-- Secure opening -----------------------------------------------------------------------------------------------------
-- A rail row or card casts its profession's spell; with nothing to cast, the click shows the gathering card instead.
local function arm(b, prof)
    b:SetAttribute("type1", prof and prof.spell and "spell" or nil)
    b:SetAttribute("spell", prof and prof.spell or nil)
    b.prof = prof
end

local function onSecureClick(self)
    local prof = self.prof
    if prof and prof.unlearned then return end
    PlaySound("igMainMenuOptionCheckBoxOn")
    if self.overviewRow then
        if open then CloseTradeSkill() end
        open, selectedGather = nil, nil
    elseif prof and not prof.spell then
        if open then CloseTradeSkill() end
        open, selectedGather = nil, prof.name
    end
    dirty = true
end

local function profTooltip(owner, prof)
    GameTooltip:SetOwner(owner, "ANCHOR_RIGHT")
    GameTooltip:SetText(prof.name, 1, 0.82, 0.1)
    if prof.unlearned then
        GameTooltip:AddLine("Not learned yet. Any trainer for it can teach you.", 0.88, 0.86, 0.8, true)
    else
        GameTooltip:AddLine(("%s  ·  %d / %d"):format(rankTitle(prof.maxRank), prof.rank, prof.maxRank), 0.88, 0.86, 0.8)
        GameTooltip:AddLine(prof.spell and ("Click to open " .. prof.spell .. ".") or
            (KNOWN[prof.name] and not KNOWN[prof.name][1] and "A gathering profession: no recipes." or
             "No recipes known yet."), 0.54, 0.43, 0.18)
    end
    GameTooltip:Show()
end

-- Rail ---------------------------------------------------------------------------------------------------------------
local function makeRailRow(parent, i)
    local r = CreateFrame("Button", "EsteriaProfessionsRail" .. i, parent, "SecureActionButtonTemplate")
    r:SetSize(RAIL_W - 12, 26)
    r:RegisterForClicks("LeftButtonUp")
    r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
    r.accent = ES.Solid(r, "ARTWORK", ES.GOLD, 0); r.accent:SetPoint("TOPLEFT"); r.accent:SetPoint("BOTTOMLEFT"); r.accent:SetWidth(3)
    r.icon = r:CreateTexture(nil, "ARTWORK"); r.icon:SetSize(20, 20); r.icon:SetPoint("LEFT", 9, 0)
    r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    r.name = ES.Text(r, 11, ES.TEXT); r.name:SetPoint("TOPLEFT", 36, -3); r.name:SetPoint("RIGHT", -48, 0); r.name:SetJustifyH("LEFT")
    r.rank = ES.Text(r, 9, ES.MUTED); r.rank:SetPoint("TOPRIGHT", -8, -4)
    r.barBg, r.barFill = bar(r, 3); r.barBg:SetPoint("BOTTOMLEFT", 36, 4); r.barBg:SetPoint("BOTTOMRIGHT", -8, 4)
    r:SetScript("PostClick", onSecureClick)
    r:SetScript("OnEnter", function(self)
        if not self.on then self.wash:SetAlpha(0.06) end
        if self.prof then profTooltip(self, self.prof) end
    end)
    r:SetScript("OnLeave", function(self) if not self.on then self.wash:SetAlpha(0) end; GameTooltip:Hide() end)
    return r
end

local function paintRailRow(r, prof, on)
    r.on = on
    r.wash:SetAlpha(on and 0.12 or 0); r.accent:SetAlpha(on and 1 or 0)
    r.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
    if r.overviewRow then
        r.icon:SetTexture("Interface\\Icons\\INV_Misc_Book_09"); r.name:SetText("Overview")
        r.rank:SetText(#professions); r.barBg:Hide(); r.barFill:Hide()
        return
    end
    r.icon:SetTexture(prof.icon)
    r.name:SetText(prof.name)
    r.rank:SetText(("%d/%d"):format(prof.rank, prof.maxRank))
    r.barBg:Show(); r.barFill:Show()
    setBar(r.barFill, RAIL_W - 12 - 44, prof.maxRank > 0 and prof.rank / prof.maxRank or 0,
        prof.rank >= prof.maxRank and ES.GOOD or ES.GOLD)
end

-- Overview -----------------------------------------------------------------------------------------------------------
local CARD_COLS, CARD_W, CARD_H = 3, math.floor((MAIN_W - 24 - 2 * 8) / 3), 70

local function makeCard(parent, i)
    local c = CreateFrame("Button", "EsteriaProfessionsCard" .. i, parent, "SecureActionButtonTemplate")
    c:SetSize(CARD_W, CARD_H)
    c:SetPoint("TOPLEFT", 12 + ((i - 1) % CARD_COLS) * (CARD_W + 8), -60 - math.floor((i - 1) / CARD_COLS) * (CARD_H + 8))
    c:RegisterForClicks("LeftButtonUp")
    ES.Solid(c, "BACKGROUND", { 0.10, 0.11, 0.15 }, 0.8):SetAllPoints()
    c.lines = ES.Border(c, ES.GOLD_DIM, 0.5)
    c.glow = ES.Solid(c, "ARTWORK", ES.GOLD, 0); c.glow:SetAllPoints()
    c.iconBox = CreateFrame("Frame", nil, c); c.iconBox:SetSize(38, 38); c.iconBox:SetPoint("TOPLEFT", 10, -10)
    c.icon = c.iconBox:CreateTexture(nil, "ARTWORK"); c.icon:SetPoint("TOPLEFT", 1, -1); c.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    c.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    c.iconLines = ES.Border(c.iconBox, ES.GOLD)
    c.name = ES.Text(c, 12, ES.GOLD); c.name:SetPoint("TOPLEFT", 56, -10); c.name:SetPoint("RIGHT", -8, 0); c.name:SetJustifyH("LEFT")
    c.title = ES.Text(c, 10, ES.MUTED); c.title:SetPoint("TOPLEFT", 56, -29)
    c.rank = ES.Text(c, 10, ES.TEXT); c.rank:SetPoint("TOPRIGHT", -10, -29)
    c.barBg, c.barFill = bar(c, 5); c.barBg:SetPoint("BOTTOMLEFT", 56, 12); c.barBg:SetPoint("BOTTOMRIGHT", -10, 12)
    c:SetScript("PostClick", onSecureClick)
    c:SetScript("OnEnter", function(self) self.glow:SetAlpha(0.08); if self.prof then profTooltip(self, self.prof) end end)
    c:SetScript("OnLeave", function(self) self.glow:SetAlpha(0); GameTooltip:Hide() end)
    return c
end

local function paintCard(c, prof)
    c.icon:SetTexture(prof.icon)
    c.icon:SetDesaturated(prof.unlearned and true or false)
    c:SetAlpha(prof.unlearned and 0.45 or 1)
    c.name:SetText(prof.name)
    shown(c.barBg, not prof.unlearned); shown(c.barFill, not prof.unlearned)
    if prof.unlearned then
        c.title:SetText("Not learned"); c.rank:SetText("")
        ES.Tint(c.iconLines, ES.GOLD_DIM, 1)
        return
    end
    c.title:SetText(rankTitle(prof.maxRank))
    c.rank:SetText(("%d / %d"):format(prof.rank, prof.maxRank) .. (prof.modifier ~= 0 and (" %+d"):format(prof.modifier) or ""))
    local maxed = prof.maxRank > 0 and prof.rank >= prof.maxRank
    setBar(c.barFill, CARD_W - 66, prof.maxRank > 0 and prof.rank / prof.maxRank or 0, maxed and ES.GOOD or ES.GOLD)
    ES.Tint(c.iconLines, maxed and ES.GOOD or ES.GOLD, 1)
end

-- Profession view: recipe list and recipe detail ------------------------------------------------------------------
local function makeRecipeRow(parent, w, h)
    local r = CreateFrame("Button", nil, parent); r:SetSize(w, h - 1)
    r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
    r.edge = ES.Solid(r, "ARTWORK", ES.GOLD, 0); r.edge:SetPoint("TOPLEFT"); r.edge:SetPoint("BOTTOMLEFT"); r.edge:SetWidth(2)
    r.toggle = ES.Text(r, 12, ES.GOLD); r.toggle:SetPoint("LEFT", 6, 0)
    r.name = ES.Text(r, 11, ES.TEXT); r.name:SetPoint("LEFT", 10, 0); r.name:SetPoint("RIGHT", -34, 0); r.name:SetJustifyH("LEFT")
    r.count = ES.Text(r, 10, ES.MUTED); r.count:SetPoint("RIGHT", -6, 0)
    r:SetScript("OnClick", function(self)
        local e = self.item
        if not e then return end
        if e.header then collapsed[e.name] = not collapsed[e.name] or nil; dirty = true; return end
        if IsModifiedClick("CHATLINK") then
            local link = GetTradeSkillRecipeLink and GetTradeSkillRecipeLink(e.index) or GetTradeSkillItemLink(e.index)
            if link and ChatEdit_InsertLink then ChatEdit_InsertLink(link) end
            return
        end
        PlaySound("igMainMenuOptionCheckBoxOn")
        selectedRecipe = e.name; SelectTradeSkill(e.index); view.quantity:SetText("1"); dirty = true
    end)
    r:SetScript("OnEnter", function(self) if self.item and not self.item.header and not self.on then self.wash:SetAlpha(0.06) end end)
    r:SetScript("OnLeave", function(self) if not self.on then self.wash:SetAlpha(0) end end)
    return r
end

local function fillRecipeRow(r, e)
    if e.header then
        r.on = false; r.wash:SetAlpha(0.04); r.edge:SetAlpha(0)
        r.toggle:Show(); r.toggle:SetText(collapsed[e.name] and "+" or "-")
        r.name:ClearAllPoints(); r.name:SetPoint("LEFT", 20, 0); r.name:SetPoint("RIGHT", -34, 0)
        r.name:SetText(e.name:upper()); r.name:SetTextColor(unpack(ES.GOLD_DIM))
        r.count:SetText(e.count)
        return
    end
    local on = e.name == selectedRecipe
    r.on = on
    r.wash:SetAlpha(on and 0.12 or 0); r.edge:SetAlpha(on and 1 or 0)
    r.toggle:Hide()
    r.name:ClearAllPoints(); r.name:SetPoint("LEFT", 14, 0); r.name:SetPoint("RIGHT", -34, 0)
    r.name:SetText(e.name); r.name:SetTextColor(unpack(DIFFICULTY[e.kind] or ES.TEXT))
    r.count:SetText(e.available > 0 and ("[" .. e.available .. "]") or "")
end

-- A fixed set of rows over `data` with a slim gold scrollbar.
local function vlist(parent, width, rowH, count, make, fill)
    local l = CreateFrame("Frame", nil, parent)
    l:SetSize(width, rowH * count)
    l.rows, l.data, l.offset = {}, {}, 0
    for i = 1, count do
        local row = make(l, width - 10, rowH)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * rowH)
        l.rows[i] = row
    end
    l.empty = ES.Text(l, 11, ES.MUTED); l.empty:SetPoint("TOP", 0, -60); l.empty:SetWidth(width - 40)
    local function paint()
        for i, row in ipairs(l.rows) do
            local item = l.data[i + l.offset]
            row.item = item
            if item then fill(row, item); row:Show() else row:Hide() end
        end
        shown(l.empty, #l.data == 0)
    end
    l.slider = CreateFrame("Slider", nil, l); l.slider:SetOrientation("VERTICAL"); l.slider:SetWidth(5)
    l.slider:SetPoint("TOPRIGHT", 0, -2); l.slider:SetPoint("BOTTOMRIGHT", 0, 2)
    ES.Solid(l.slider, "BACKGROUND", { 0, 0, 0 }, 0.5):SetAllPoints()
    local thumb = l.slider:CreateTexture(nil, "OVERLAY"); thumb:SetTexture(ES.WHITE); thumb:SetSize(5, 40)
    thumb:SetVertexColor(unpack(ES.GOLD_DIM)); l.slider:SetThumbTexture(thumb)
    l.slider:SetValueStep(1); l.slider:SetMinMaxValues(0, 0); l.slider:SetValue(0)
    l.slider:SetScript("OnValueChanged", function(_, v) l.offset = math.floor(v + 0.5); paint() end)
    l.Refresh = function(self)
        local maxOffset = math.max(0, #self.data - count)
        self.offset = math.max(0, math.min(self.offset, maxOffset))
        self.slider:SetMinMaxValues(0, maxOffset); shown(self.slider, maxOffset > 0); self.slider:SetValue(self.offset)
        paint()
    end
    l:EnableMouseWheel(true)
    l:SetScript("OnMouseWheel", function(self, delta) self.offset = self.offset - delta * 3; self:Refresh() end)
    return l
end

local function makeReagent(parent, i)
    local r = CreateFrame("Button", nil, parent); r:SetSize(math.floor((DETAIL_W - 32) / 2), 38)
    r:SetPoint("TOPLEFT", 14 + ((i - 1) % 2) * (math.floor((DETAIL_W - 32) / 2) + 4), -(118 + math.floor((i - 1) / 2) * 42))
    ES.Solid(r, "BACKGROUND", { 0.10, 0.11, 0.15 }, 0.8):SetAllPoints()
    r.iconBox = CreateFrame("Frame", nil, r); r.iconBox:SetSize(32, 32); r.iconBox:SetPoint("LEFT", 3, 0)
    r.icon = r.iconBox:CreateTexture(nil, "ARTWORK"); r.icon:SetPoint("TOPLEFT", 1, -1); r.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    r.lines = ES.Border(r.iconBox, ES.GOLD_DIM)
    r.count = ES.Text(r.iconBox, 10, ES.TEXT); r.count:SetFont(ES.FONT, 10, "OUTLINE"); r.count:SetPoint("BOTTOMRIGHT", -2, 2)
    r.name = ES.Text(r, 10, ES.TEXT); r.name:SetPoint("LEFT", 40, 0); r.name:SetPoint("RIGHT", -4, 0); r.name:SetJustifyH("LEFT")
    r:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetTradeSkillItem(self.recipe, self.reagent); GameTooltip:Show()
    end)
    r:SetScript("OnLeave", function() GameTooltip:Hide() end)
    r:SetScript("OnClick", function(self)
        local link = GetTradeSkillReagentItemLink(self.recipe, self.reagent)
        if link then HandleModifiedItemClick(link) end
    end)
    return r
end

local function buildView(parent)
    view = CreateFrame("Frame", nil, parent); view:SetAllPoints()
    -- Header strip: profession icon, name, rank bar, search, filters.
    view.iconBox = CreateFrame("Frame", nil, view); view.iconBox:SetSize(36, 36); view.iconBox:SetPoint("TOPLEFT", 12, -10)
    view.icon = view.iconBox:CreateTexture(nil, "ARTWORK"); view.icon:SetPoint("TOPLEFT", 1, -1); view.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    view.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    ES.Border(view.iconBox, ES.GOLD)
    view.name = ES.Text(view, 15, ES.GOLD); view.name:SetPoint("TOPLEFT", 56, -10)
    view.rankText = ES.Text(view, 10, ES.MUTED); view.rankText:SetPoint("TOPLEFT", view.name, "BOTTOMLEFT", 0, -3)
    view.barBg, view.barFill = bar(view, 4); view.barBg:SetPoint("TOPLEFT", 56, -42); view.barBg:SetWidth(200)
    view.linked = ES.Text(view, 10, ES.BAD); view.linked:SetPoint("LEFT", view.barBg, "RIGHT", 10, 0)
    view.search = editBox(view, 210, "Search recipes and reagents", nil, 40); view.search:SetPoint("TOPRIGHT", -12, -10)
    view.search:HookScript("OnTextChanged", function() view.list.offset = 0; dirty = true end)
    view.haveMats = toggle(view, "Have materials", "haveMats"); view.haveMats:SetPoint("TOPRIGHT", -112, -38)
    view.skillUps = toggle(view, "Skill-ups only", "skillUps"); view.skillUps:SetPoint("TOPRIGHT", -2, -38)
    local rule = ES.Solid(view, "ARTWORK", ES.GOLD_DIM, 0.5); rule:SetHeight(1)
    rule:SetPoint("TOPLEFT", 1, -62); rule:SetPoint("TOPRIGHT", -1, -62)

    local listH = PANE_H - 70 - 8
    view.list = vlist(view, LIST_W, 20, math.floor(listH / 20), makeRecipeRow, fillRecipeRow)
    view.list:SetPoint("TOPLEFT", 8, -68)
    view.list.empty:SetText("No recipes match.")

    local detail = CreateFrame("Frame", nil, view)
    detail:SetPoint("TOPLEFT", view.list, "TOPRIGHT", 8, 0); detail:SetSize(DETAIL_W, listH)
    ES.Solid(detail, "BACKGROUND", { 0.04, 0.05, 0.07 }, 0.9):SetAllPoints(); ES.Border(detail, ES.GOLD_DIM, 0.5)
    view.detail = detail
    detail.product = CreateFrame("Button", nil, detail); detail.product:SetSize(44, 44); detail.product:SetPoint("TOPLEFT", 14, -14)
    ES.Solid(detail.product, "BACKGROUND", ES.INK, 1):SetAllPoints()
    detail.productIcon = detail.product:CreateTexture(nil, "ARTWORK")
    detail.productIcon:SetPoint("TOPLEFT", 1, -1); detail.productIcon:SetPoint("BOTTOMRIGHT", -1, 1)
    detail.productIcon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    detail.productLines = ES.Border(detail.product, ES.GOLD)
    detail.made = ES.Text(detail.product, 11, ES.TEXT); detail.made:SetFont(ES.FONT, 11, "OUTLINE"); detail.made:SetPoint("BOTTOMRIGHT", -2, 2)
    detail.product:SetScript("OnEnter", function(self)
        if not self.recipe then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetTradeSkillItem(self.recipe); GameTooltip:Show()
    end)
    detail.product:SetScript("OnLeave", function() GameTooltip:Hide() end)
    detail.product:SetScript("OnClick", function(self)
        local link = self.recipe and GetTradeSkillItemLink(self.recipe)
        if link then HandleModifiedItemClick(link) end
    end)
    detail.title = ES.Text(detail, 14, ES.GOLD); detail.title:SetPoint("TOPLEFT", 68, -16); detail.title:SetPoint("RIGHT", -12, 0)
    detail.title:SetJustifyH("LEFT")
    detail.sub = ES.Text(detail, 10, ES.MUTED); detail.sub:SetPoint("TOPLEFT", detail.title, "BOTTOMLEFT", 0, -4)
    detail.sub:SetPoint("RIGHT", -12, 0); detail.sub:SetJustifyH("LEFT")
    detail.req = ES.Text(detail, 10, ES.TEXT); detail.req:SetPoint("TOPLEFT", 14, -70); detail.req:SetWidth(DETAIL_W - 28)
    detail.req:SetJustifyH("LEFT")
    detail.reagentHead = heading(detail, "Reagents"); detail.reagentHead:SetPoint("TOPLEFT", 14, -100)
    detail.reagents = {}
    for i = 1, 8 do detail.reagents[i] = makeReagent(detail, i) end
    detail.desc = ES.Text(detail, 10, ES.MUTED); detail.desc:SetWidth(DETAIL_W - 28); detail.desc:SetJustifyH("LEFT")
    detail.none = ES.Text(detail, 11, ES.MUTED); detail.none:SetPoint("CENTER"); detail.none:SetText("Select a recipe.")

    -- Crafting: quantity, Create, Create all.
    local footer = CreateFrame("Frame", nil, detail); footer:SetPoint("BOTTOMLEFT", 1, 1); footer:SetPoint("BOTTOMRIGHT", -1, 1)
    footer:SetHeight(44)
    ES.Solid(footer, "BACKGROUND", { 0.09, 0.10, 0.14 }, 1):SetAllPoints()
    detail.footer = footer
    view.minus = button(footer, 24, "-", false, 24); view.minus:SetPoint("LEFT", 10, 0)
    view.quantity = editBox(footer, 44, "", function(self) self:ClearFocus() end, 3); view.quantity:SetPoint("LEFT", view.minus, "RIGHT", 4, 0)
    view.quantity:SetNumeric(true); view.quantity:SetJustifyH("CENTER"); view.quantity:SetText("1")
    view.plus = button(footer, 24, "+", false, 24); view.plus:SetPoint("LEFT", view.quantity, "RIGHT", 4, 0)
    local function nudge(delta)
        view.quantity:SetText(math.max(1, math.min(999, (tonumber(view.quantity:GetText()) or 1) + delta)))
    end
    view.minus:SetScript("OnClick", function() nudge(-1) end)
    view.plus:SetScript("OnClick", function() nudge(1) end)
    view.createAll = button(footer, 86, "Create all", false, 26); view.createAll:SetPoint("RIGHT", -10, 0)
    view.create = button(footer, 86, "Create", true, 26); view.create:SetPoint("RIGHT", view.createAll, "LEFT", -6, 0)
    view.create:SetScript("OnClick", function()
        local i = GetTradeSkillSelectionIndex()
        if i and i > 0 then DoTradeSkill(i, tonumber(view.quantity:GetText()) or 1) end
    end)
    view.createAll:SetScript("OnClick", function()
        local i = GetTradeSkillSelectionIndex()
        local _, _, available = GetTradeSkillInfo(i or 0)
        if i and (available or 0) > 0 then view.quantity:SetText(available); DoTradeSkill(i, available) end
    end)
end

local function paintDetail()
    local d = view.detail
    local index
    for i = 1, (GetNumTradeSkills()) or 0 do
        local name, kind = GetTradeSkillInfo(i)
        if kind ~= "header" and name == selectedRecipe then index = i end
    end
    local has = index ~= nil
    for _, region in ipairs({ d.product, d.title, d.sub, d.req, d.reagentHead, d.desc, d.footer }) do shown(region, has) end
    shown(d.none, not has)
    for _, r in ipairs(d.reagents) do r:Hide() end
    if not has then return end
    if GetTradeSkillSelectionIndex() ~= index then SelectTradeSkill(index) end
    local name, kind, available = GetTradeSkillInfo(index)
    d.product.recipe = index
    d.productIcon:SetTexture(GetTradeSkillIcon(index))
    local low, high = GetTradeSkillNumMade(index)
    d.made:SetText((high or 1) > 1 and (low == high and low or (low .. "-" .. high)) or "")
    local link = GetTradeSkillItemLink(index)
    local quality = link and select(3, GetItemInfo(link))
    local q = quality and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
    ES.Tint(d.productLines, q and { q.r, q.g, q.b } or ES.GOLD, 1)
    d.title:SetText(name)
    local diffNames = { optimal = "Orange: always a skill-up", medium = "Yellow: likely a skill-up",
                        easy = "Green: rarely a skill-up", trivial = "Grey: no skill-ups" }
    d.sub:SetText(diffNames[kind] or ""); d.sub:SetTextColor(unpack(DIFFICULTY[kind] or ES.MUTED))
    local req = {}
    local tools = { GetTradeSkillTools(index) }
    for t = 1, #tools, 2 do
        if tools[t] then req[#req + 1] = (tools[t + 1] and "|cff8cd97f" or "|cfff26b5c") .. tools[t] .. "|r" end
    end
    local cooldown = GetTradeSkillCooldown(index)
    if cooldown and cooldown > 0 then req[#req + 1] = "|cfff26b5cCooldown " .. SecondsToTime(cooldown) .. "|r" end
    d.req:SetText(#req > 0 and ("Requires " .. table.concat(req, ", ")) or "")
    local n = (GetTradeSkillNumReagents(index)) or 0
    for r = 1, math.min(n, #d.reagents) do
        local reagentName, texture, need, have = GetTradeSkillReagentInfo(index, r)
        local row = d.reagents[r]
        row.recipe, row.reagent = index, r
        row.icon:SetTexture(texture)
        local enough = (have or 0) >= (need or 0)
        row.count:SetText(("%d/%d"):format(have or 0, need or 0))
        row.count:SetTextColor(unpack(enough and ES.TEXT or ES.BAD))
        row.icon:SetDesaturated(not enough)
        row.name:SetText(reagentName or ""); row.name:SetTextColor(unpack(enough and ES.TEXT or ES.MUTED))
        row:Show()
    end
    local rows = math.ceil(math.min(n, #d.reagents) / 2)
    local desc = GetTradeSkillDescription and GetTradeSkillDescription(index)
    d.desc:ClearAllPoints(); d.desc:SetPoint("TOPLEFT", 14, -(122 + rows * 42))
    d.desc:SetText(desc or "")
    local linked = IsTradeSkillLinked and IsTradeSkillLinked()
    view.create:Active(not linked and (available or 0) > 0)
    view.createAll:Active(not linked and (available or 0) > 0)
    view.createAll.label:SetText((available or 0) > 0 and ("Create all (" .. available .. ")") or "Create all")
end

local function paintView()
    local name, rank, maxRank = GetTradeSkillLine()
    local prof
    for _, p in ipairs(professions) do if p.name == name or p.spell == name then prof = p end end
    view.icon:SetTexture(prof and prof.icon or GetTradeSkillTexture and GetTradeSkillTexture() or "Interface\\Icons\\INV_Misc_QuestionMark")
    view.name:SetText(name or "")
    view.rankText:SetText(("%s  ·  %d / %d"):format(rankTitle(maxRank), rank or 0, maxRank or 0))
    setBar(view.barFill, 200, (maxRank or 0) > 0 and (rank or 0) / maxRank or 0, (rank or 0) >= (maxRank or 1) and ES.GOOD or ES.GOLD)
    local linked, player = false, nil
    if IsTradeSkillLinked then linked, player = IsTradeSkillLinked() end
    view.linked:SetText(linked and ("Viewing " .. (player or "a linked") .. "'s recipes") or "")
    view.haveMats:Paint(); view.skillUps:Paint()
    view.list.data = recipeRows(view.search:GetText():lower())
    if not selectedRecipe then
        for _, r in ipairs(view.list.data) do if not r.header then selectedRecipe = r.name; break end end
    end
    view.list:Refresh()
    paintDetail()
end

-- Window -------------------------------------------------------------------------------------------------------------
-- Point every rail row and overview card at its profession. Secure attributes only change out of combat, so this runs
-- at login (the window may first open in combat), on any change, and again once combat ends.
local function armAll()
    if locked() then pendingArm = true; return end
    railRows[1].overviewRow = true
    for i = 2, #railRows do arm(railRows[i], professions[i - 1]); shown(railRows[i], professions[i - 1] ~= nil) end
    for i, c in ipairs(cards) do
        local prof = professions[i] or unlearned[i - #professions]
        arm(c, prof); shown(c, prof ~= nil)
    end
    pendingArm = false
end

local header
local function refresh()
    if not frame or not frame:IsShown() then return end
    local maxed = 0
    for _, p in ipairs(professions) do if p.maxRank > 0 and p.rank >= p.maxRank then maxed = maxed + 1 end end
    header:SetText(("|cffe8c46b%d|r professions     |cffe8c46b%d|r at max rank"):format(#professions, maxed))

    -- Rail: Overview, then every profession (armed by armAll; in combat each keeps what it casts).
    armAll()
    paintRailRow(railRows[1], nil, open == nil and selectedGather == nil)
    for i = 2, #railRows do
        local row = railRows[i]
        if row.prof and row:IsShown() then
            paintRailRow(row, row.prof, (open ~= nil and (row.prof.name == open or row.prof.spell == open)) or
                row.prof.name == selectedGather)
        end
    end
    local parts = {}
    for _, m in ipairs(minor) do parts[#parts + 1] = ("%s  |cff9a9790%d/%d|r"):format(m.name, m.rank, m.maxRank) end
    rail.minorText:SetText(table.concat(parts, "     "))
    shown(rail.minorHead, #minor > 0); shown(rail.minorRule, #minor > 0); shown(rail.minorText, #minor > 0)

    shown(overview, open == nil and selectedGather == nil)
    shown(view, open ~= nil)
    shown(gathering, open == nil and selectedGather ~= nil)
    if open then
        paintView()
    elseif selectedGather then
        local prof
        for _, p in ipairs(professions) do if p.name == selectedGather then prof = p end end
        if prof then
            gathering.icon:SetTexture(prof.icon)
            gathering.name:SetText(prof.name)
            gathering.rank:SetText(("%s  ·  %d / %d"):format(rankTitle(prof.maxRank), prof.rank, prof.maxRank))
            setBar(gathering.barFill, 300, prof.maxRank > 0 and prof.rank / prof.maxRank or 0,
                prof.rank >= prof.maxRank and ES.GOOD or ES.GOLD)
            gathering.text:SetText(prof.name == "Fishing" and "Fish from your action bar or spellbook. Fishing has no recipes." or
                prof.name == "Herbalism" and "Gather herbs from the world. Track them from the minimap to find more." or
                prof.name == "Skinning" and "Skin beasts you or your group have looted." or "This profession has no recipes.")
        end
    else
        for _, c in ipairs(cards) do
            if c.prof and c:IsShown() then paintCard(c, c.prof) end
        end
        overview.empty:SetText("")
    end
end

local function build()
    EsteriaProfessionsDB = EsteriaProfessionsDB or {}
    db = EsteriaProfessionsDB

    -- Secure show/hide (its rows cast profession spells); Escape is bound to the close button while open.
    frame = CreateFrame("Frame", "EsteriaProfessionsFrame", UIParent, "SecureHandlerShowHideTemplate")
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) if not locked() then self:StartMoving() end end)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide()
    frame:SetAttribute("_onshow", [[ self:SetBindingClick(true, "ESCAPE", "EsteriaProfessionsClose") ]])
    frame:SetAttribute("_onhide", [[ self:ClearBindings() ]])
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(frame, ES.GOLD)
    gradient(frame, 42, 0.28)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 18, -12); title:SetText(TRADE_SKILLS or "Professions")
    header = ES.Text(frame, 11, ES.TEXT); header:SetPoint("TOPRIGHT", -40, -15)
    local close = CreateFrame("Button", "EsteriaProfessionsClose", frame, "SecureHandlerClickTemplate")
    close:SetSize(32, 32); close:SetPoint("TOPRIGHT", 2, 2)
    close:SetNormalTexture("Interface\\Buttons\\UI-Panel-MinimizeButton-Up")
    close:SetPushedTexture("Interface\\Buttons\\UI-Panel-MinimizeButton-Down")
    close:SetHighlightTexture("Interface\\Buttons\\UI-Panel-MinimizeButton-Highlight", "ADD")
    close:RegisterForClicks("AnyUp")
    SecureHandlerSetFrameRef(close, "book", frame)
    close:SetAttribute("_onclick", [[ self:GetFrameRef("book"):Hide() ]])
    local pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")

    rail = panel(pane); rail:SetPoint("TOPLEFT", 14, -10); rail:SetPoint("BOTTOMLEFT", 14, 10); rail:SetWidth(RAIL_W)
    heading(rail, "Professions"):SetPoint("TOPLEFT", 12, -10)
    for i = 1, 15 do
        local row = makeRailRow(rail, i)
        row:SetPoint("TOPLEFT", 6, -26 - (i - 1) * 27 - (i > 1 and 4 or 0))
        railRows[i] = row
    end

    rail.minorHead = heading(rail, "Other skills"); rail.minorHead:SetPoint("BOTTOMLEFT", 12, 30)
    rail.minorRule = ES.Solid(rail, "ARTWORK", ES.GOLD_DIM, 0.4); rail.minorRule:SetHeight(1)
    rail.minorRule:SetPoint("BOTTOMLEFT", 12, 44); rail.minorRule:SetPoint("BOTTOMRIGHT", -12, 44)
    rail.minorText = ES.Text(rail, 10, ES.TEXT); rail.minorText:SetPoint("BOTTOMLEFT", 12, 12)
    rail.minorText:SetWidth(RAIL_W - 24); rail.minorText:SetJustifyH("LEFT")

    local main = panel(pane); main:SetPoint("TOPLEFT", rail, "TOPRIGHT", 10, 0); main:SetPoint("BOTTOMRIGHT", -14, 10)
    gradient(main, 60, 0.16)

    overview = CreateFrame("Frame", nil, main); overview:SetAllPoints()
    local oh = ES.Text(overview, 15, ES.GOLD); oh:SetPoint("TOPLEFT", 14, -12); oh:SetText("Overview")
    local os = ES.Text(overview, 10, ES.MUTED); os:SetPoint("TOPLEFT", oh, "BOTTOMLEFT", 0, -3)
    os:SetText("Click a profession to open its recipes. Dim ones aren't learned yet.")
    for i = 1, 15 do cards[i] = makeCard(overview, i) end
    overview.empty = ES.Text(overview, 12, ES.MUTED); overview.empty:SetPoint("CENTER")

    buildView(main)

    gathering = CreateFrame("Frame", nil, main); gathering:SetAllPoints()
    local gIconBox = CreateFrame("Frame", nil, gathering); gIconBox:SetSize(56, 56); gIconBox:SetPoint("TOPLEFT", 20, -20)
    gathering.icon = gIconBox:CreateTexture(nil, "ARTWORK"); gathering.icon:SetPoint("TOPLEFT", 1, -1)
    gathering.icon:SetPoint("BOTTOMRIGHT", -1, 1); gathering.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    ES.Border(gIconBox, ES.GOLD)
    gathering.name = ES.Text(gathering, 18, ES.GOLD); gathering.name:SetPoint("TOPLEFT", 90, -22)
    gathering.rank = ES.Text(gathering, 11, ES.MUTED); gathering.rank:SetPoint("TOPLEFT", gathering.name, "BOTTOMLEFT", 0, -4)
    gathering.barBg, gathering.barFill = bar(gathering, 6); gathering.barBg:SetPoint("TOPLEFT", 90, -66); gathering.barBg:SetWidth(300)
    gathering.text = ES.Text(gathering, 12, ES.TEXT); gathering.text:SetPoint("TOPLEFT", 20, -100); gathering.text:SetWidth(MAIN_W - 40)
    gathering.text:SetJustifyH("LEFT")

    frame:SetScript("OnUpdate", function() if dirty then dirty = false; refresh() end end)
    frame:HookScript("OnShow", function() PlaySound("igCharacterInfoOpen"); scanProfessions(); dirty = true end)
    frame:HookScript("OnHide", function()
        PlaySound("igCharacterInfoClose"); GameTooltip:Hide()
        if open then CloseTradeSkill() end
        open, selectedGather = nil, nil
    end)

    local events = CreateFrame("Frame")
    for _, ev in ipairs({ "TRADE_SKILL_SHOW", "TRADE_SKILL_UPDATE", "TRADE_SKILL_CLOSE", "TRADE_SKILL_FILTER_UPDATE",
                          "SKILL_LINES_CHANGED", "BAG_UPDATE", "PLAYER_REGEN_ENABLED", "SPELLS_CHANGED" }) do
        events:RegisterEvent(ev)
    end
    events:SetScript("OnEvent", function(_, ev)
        if ev == "TRADE_SKILL_SHOW" then
            local name = GetTradeSkillLine()
            if name ~= open then selectedRecipe = nil; view.search:SetText("") end
            open, selectedGather = name, nil
            -- Blizzard's own filters (made in the stock window) would hide recipes this window expects to list.
            if TradeSkillOnlyShowMakeable then TradeSkillOnlyShowMakeable(false) end
            if SetTradeSkillSubClassFilter then SetTradeSkillSubClassFilter(0, 1, 1) end
            if SetTradeSkillInvSlotFilter then SetTradeSkillInvSlotFilter(0, 1, 1) end
            if not frame:IsShown() then
                if locked() then
                    -- The secure frame can't be shown from here in combat; let Blizzard's window take this one.
                    if TradeSkillFrame_LoadUI then TradeSkillFrame_LoadUI() end
                    if TradeSkillFrame then ShowUIPanel(TradeSkillFrame) end
                    return
                end
                frame:Show()
            end
        elseif ev == "TRADE_SKILL_CLOSE" then
            open = nil
        elseif ev == "SKILL_LINES_CHANGED" or ev == "SPELLS_CHANGED" then
            if frame:IsShown() then scanProfessions() end
        elseif ev == "PLAYER_REGEN_ENABLED" then
            if pendingArm then armAll() end
        end
        dirty = true
    end)

    scanProfessions(); dirty = true
    armAll() -- out of combat at login, so a first opening in combat can already cast
end

function EsteriaProfessions_Toggle()
    if not frame then return end
    if locked() then
        UIErrorsFrame:AddMessage(ERR_NOT_IN_COMBAT or "You can't do that while in combat.", 1, 0.1, 0.1)
        return
    end
    if frame:IsShown() then frame:Hide() else frame:Show() end
end

-- The stock window shows itself on TRADE_SKILL_SHOW from UIParent; this window answers that event instead.
UIParent:UnregisterEvent("TRADE_SKILL_SHOW")

BINDING_HEADER_ESTERIAPROFESSIONS = "Esteria"
BINDING_NAME_ESTERIAPROFESSIONS_TOGGLE = "Toggle Professions"

local loader = CreateFrame("Frame")
loader:RegisterEvent("PLAYER_LOGIN")
loader:SetScript("OnEvent", function()
    build()
    -- DragonUI's Professions micro button hides with its module; bring it back, pointed at this window.
    local dui = _G.DragonUI
    if dui and _G.ProfessionMicroButton then
        dui.ToggleProfessionsBook = EsteriaProfessions_Toggle
        _G.ProfessionMicroButton.dragonUISuppressed = nil
        if dui.RefreshMicromenuButtons then pcall(dui.RefreshMicromenuButtons) end
    end
end)

SLASH_ESTERIAPROFESSIONS1 = "/prof"
SLASH_ESTERIAPROFESSIONS2 = "/professions"
SlashCmdList.ESTERIAPROFESSIONS = function() EsteriaProfessions_Toggle() end
