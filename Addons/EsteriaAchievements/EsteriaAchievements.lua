-- Esteria's achievement window in the PvP/Collections/Quest Log look: a summary, a searchable achievement browser
-- (category rail, achievement cards, criteria detail) and statistics. Replaces the stock achievement frame everywhere
-- it opens (key binding, micro button, earned alerts, the tracker); inspect comparison stays on the stock frame.
local ES = EsteriaStyle
local W, H = 960, 640
local RAIL_W, LIST_W = 210, 344
local CARD_H, CARDS = 58, 8
local RAIL_H, RAILS = 26, 19
local STAT_H, STATS = 28, 18
-- 3.3.5 flag values (the stock constants live in the load-on-demand Blizzard_AchievementUI).
local FLAG_HAS_PROGRESS_BAR = 0x80
local CRITERIA_PROGRESS_BAR, CRITERIA_HIDDEN = 0x1, 0x2
local CRITERIA_TYPE_ACHIEVEMENT = 8

local frame, tabs, panes = nil, {}, {}
local current = "Summary"
local models = {}
local selectedCat = { ach = nil, stat = nil }
local selectedID
local progressCache = {}
local dirty, progressDirty = true, false
local db
local comparing = false

local function shown(region, on) if on then region:Show() else region:Hide() end end
local function band(a, b) return bit.band(a or 0, b) ~= 0 end
local function dateText(month, day, year)
    if not month or not day or not year then return "" end
    return ("%d/%d/%02d"):format(month, day, year % 100)
end

-- Model --------------------------------------------------------------------------------------------------------------
local function rollUp(m, id)
    local c = m.cats[id]
    c.sumTotal, c.sumDone = c.total, c.done
    for _, child in ipairs(c.children) do
        rollUp(m, child)
        c.sumTotal, c.sumDone = c.sumTotal + m.cats[child].sumTotal, c.sumDone + m.cats[child].sumDone
    end
end

local function scan(kind)
    local m = { cats = {}, roots = {}, items = {}, byID = {} }
    local ids = (kind == "stat" and GetStatisticsCategoryList or GetCategoryList)() or {}
    for _, id in ipairs(ids) do
        local name, parent = GetCategoryInfo(id)
        m.cats[id] = { id = id, name = name or "?", parent = parent, children = {}, total = 0, done = 0 }
    end
    for _, id in ipairs(ids) do
        local parent = m.cats[m.cats[id].parent]
        if parent then table.insert(parent.children, id) else table.insert(m.roots, id) end
    end
    for _, id in ipairs(ids) do
        local c = m.cats[id]
        for i = 1, (GetCategoryNumAchievements(id)) or 0 do
            local aid, name, points, completed, month, day, year, desc, flags, icon, reward = GetAchievementInfo(id, i)
            if aid then
                local a = { id = aid, cat = id, name = name or "", points = points or 0, completed = completed and true or false,
                            month = month, day = day, year = year, desc = desc or "", flags = flags or 0, icon = icon,
                            reward = reward or "", order = #m.items + 1 }
                a.when = a.completed and ((year or 0) * 10000 + (month or 0) * 100 + (day or 0)) or 0
                a.search = (a.name .. " " .. a.desc .. " " .. a.reward):lower()
                m.items[#m.items + 1] = a
                m.byID[aid] = a
                c.total = c.total + 1
                if a.completed then c.done = c.done + 1 end
            end
        end
    end
    for _, id in ipairs(m.roots) do rollUp(m, id) end
    models[kind] = m
    return m
end

-- Categories in the rail: roots, with the selected root's subtree opened beneath it.
local function railEntries(kind)
    local m, out = models[kind], {}
    local sel = selectedCat[kind]
    local open = {}
    while sel and m.cats[sel] do open[sel] = true; sel = m.cats[sel].parent end
    local function add(id, depth)
        out[#out + 1] = { id = id, depth = depth }
        if open[id] then for _, child in ipairs(m.cats[id].children) do add(child, depth + 1) end end
    end
    for _, id in ipairs(m.roots) do add(id, 0) end
    return out
end

local function inSubtree(m, cat, root)
    while cat do
        if cat == root then return true end
        cat = m.cats[cat] and m.cats[cat].parent
    end
    return false
end

local function filtered(kind, query, earned, sort)
    local m, out = models[kind], {}
    for _, a in ipairs(m.items) do
        local ok
        if query ~= "" then ok = a.search:find(query, 1, true) ~= nil
        else ok = not selectedCat[kind] or inSubtree(m, a.cat, selectedCat[kind]) end
        if ok and earned == "earned" then ok = a.completed elseif ok and earned == "unearned" then ok = not a.completed end
        if ok then out[#out + 1] = a end
    end
    if sort == "recent" then
        table.sort(out, function(a, b) if a.when ~= b.when then return a.when > b.when end return a.order < b.order end)
    elseif sort == "points" then
        table.sort(out, function(a, b) if a.points ~= b.points then return a.points > b.points end return a.order < b.order end)
    elseif sort == "name" then
        table.sort(out, function(a, b) return a.name < b.name end)
    end
    return out
end

-- Fraction done for an unearned achievement: its progress bar if it has one, otherwise criteria met / shown.
local function progress(id, flags)
    if progressCache[id] then return progressCache[id] end
    local n, done, shownCount, value = GetAchievementNumCriteria(id) or 0, 0, 0, 0
    for i = 1, n do
        local _, _, completed, quantity, required, _, cflags = GetAchievementCriteriaInfo(id, i)
        if band(flags, FLAG_HAS_PROGRESS_BAR) and n == 1 and (required or 0) > 0 then
            value = math.min(1, (quantity or 0) / required)
            progressCache[id] = value
            return value
        end
        if not band(cflags, CRITERIA_HIDDEN) then
            shownCount = shownCount + 1
            if completed then done = done + 1 end
        end
    end
    value = shownCount > 0 and done / shownCount or 0
    progressCache[id] = value
    return value
end

EsteriaAchievements_Test = { scan = scan, railEntries = railEntries, filtered = filtered, progress = progress,
                             models = models, selectedCat = selectedCat }

-- Widgets (the Collections look) -----------------------------------------------------------------------------------
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

local function button(parent, width, text, primary)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, 28)
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, 0.95); b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0); b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, 12, primary and { 1, 0.92, 0.7 } or ES.TEXT)
    b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12) end end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0) end)
    b.Active = function(self, on)
        if on then self:Enable() else self:Disable(); self.glow:SetAlpha(0) end
        self.label:SetAlpha(on and 1 or 0.35)
    end
    return b
end

local function tabButton(parent, width, text, height)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height or 32)
    b.wash = ES.Solid(b, "BACKGROUND", ES.GOLD, 0); b.wash:SetAllPoints()
    b.accent = ES.Solid(b, "ARTWORK", ES.GOLD, 0)
    b.accent:SetPoint("BOTTOMLEFT", 6, 0); b.accent:SetPoint("BOTTOMRIGHT", -6, 0); b.accent:SetHeight(2)
    b.label = ES.Text(b, 11, ES.MUTED); b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b.SetSelected = function(self, on)
        self.selected = on
        self.wash:SetAlpha(on and 0.10 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.MUTED))
    end
    b:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.05) end end)
    b:SetScript("OnLeave", function(self) self:SetSelected(self.selected) end)
    return b
end

local function iconBox(parent, size)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetSize(size + 2, size + 2)
    holder.icon = holder:CreateTexture(nil, "ARTWORK")
    holder.icon:SetPoint("TOPLEFT", 1, -1); holder.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    holder.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    holder.lines = ES.Border(holder, ES.GOLD_DIM)
    return holder
end

-- A points shield in the house style: a gold-edged badge with the value.
local function pointsBadge(parent)
    local b = CreateFrame("Frame", nil, parent)
    b:SetSize(34, 22)
    b.bg = ES.Solid(b, "BACKGROUND", ES.GOLD, 0.16); b.bg:SetAllPoints()
    b.lines = ES.Border(b, ES.GOLD, 0.8)
    b.text = ES.Text(b, 12, ES.GOLD); b.text:SetPoint("CENTER", 0, 1)
    b.Set = function(self, points, earned)
        self.text:SetText(points)
        local c = earned and ES.GOLD or ES.MUTED
        self.text:SetTextColor(unpack(c)); ES.Tint(self.lines, c, 0.8); self.bg:SetVertexColor(c[1], c[2], c[3], earned and 0.16 or 0.06)
    end
    return b
end

local function bar(parent, height)
    local bg = ES.Solid(parent, "ARTWORK", { 0, 0, 0 }, 0.6); bg:SetHeight(height)
    local fill = ES.Solid(parent, "OVERLAY", ES.GOLD, 1); fill:SetPoint("TOPLEFT", bg, "TOPLEFT"); fill:SetHeight(height)
    return bg, fill
end

local function setBar(bg, fill, value, color)
    local width = bg:GetWidth() or 0
    if width <= 0 then progressDirty = true end -- not laid out yet on the first frame; repaint on the next
    fill:SetWidth(math.max(1, width * math.max(0, math.min(1, value))))
    local c = color or ES.GOLD
    fill:SetVertexColor(c[1], c[2], c[3], 1)
end

local function searchBox(parent, name, width, hint, onChange)
    local e = CreateFrame("EditBox", name, parent)
    e:SetSize(width, 26); e:SetAutoFocus(false); e:SetFontObject(ChatFontNormal); e:SetTextInsets(10, 8, 0, 0)
    ES.Solid(e, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    e.lines = ES.Border(e, ES.GOLD_DIM)
    local placeholder = ES.Text(e, 11, ES.MUTED); placeholder:SetPoint("LEFT", 10, 0); placeholder:SetText(hint)
    e:SetScript("OnEscapePressed", function(self) if self:GetText() ~= "" then self:SetText("") else self:ClearFocus() end end)
    e:SetScript("OnEnterPressed", e.ClearFocus)
    e:SetScript("OnEditFocusGained", function(self) ES.Tint(self.lines, ES.GOLD, 1) end)
    e:SetScript("OnEditFocusLost", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 1) end)
    e:SetScript("OnTextChanged", function(self) shown(placeholder, self:GetText() == ""); onChange() end)
    return e
end

-- A fixed set of rows over `data`, with a slim gold scrollbar and the mouse wheel; fill(row, item) paints one row.
local function vlist(parent, width, rowH, count, make, fill, noBar)
    local l = CreateFrame("Frame", nil, parent)
    l:SetSize(width, rowH * count)
    l.rows, l.data, l.offset = {}, {}, 0
    for i = 1, count do
        local row = make(l, width - (noBar and 0 or 12), rowH)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * rowH)
        l.rows[i] = row
    end
    l.empty = ES.Text(l, 12, ES.MUTED); l.empty:SetPoint("TOP", 0, -60); l.empty:SetWidth(width - 60)
    local function paint()
        for i, row in ipairs(l.rows) do
            local item = l.data[i + l.offset]
            row.item = item
            if item then fill(row, item); row:Show() else row:Hide() end
        end
        shown(l.empty, #l.data == 0)
    end
    l.slider = false
    if not noBar then
        l.slider = CreateFrame("Slider", nil, l); l.slider:SetOrientation("VERTICAL"); l.slider:SetWidth(5)
        l.slider:SetPoint("TOPRIGHT", 0, -2); l.slider:SetPoint("BOTTOMRIGHT", 0, 2)
        ES.Solid(l.slider, "BACKGROUND", { 0, 0, 0 }, 0.5):SetAllPoints()
        local thumb = l.slider:CreateTexture(nil, "OVERLAY"); thumb:SetTexture(ES.WHITE); thumb:SetSize(5, 40)
        thumb:SetVertexColor(unpack(ES.GOLD_DIM)); l.slider:SetThumbTexture(thumb)
        l.slider:SetValueStep(1); l.slider:SetMinMaxValues(0, 0); l.slider:SetValue(0)
        l.slider:SetScript("OnValueChanged", function(_, v) l.offset = math.floor(v + 0.5); paint() end)
    end
    l.Refresh = function(self)
        local maxOffset = math.max(0, #self.data - count)
        self.offset = math.max(0, math.min(self.offset, maxOffset))
        if self.slider then
            self.slider:SetMinMaxValues(0, maxOffset); shown(self.slider, maxOffset > 0); self.slider:SetValue(self.offset)
        end
        paint()
    end
    l.Reveal = function(self, index)
        if index and (index <= self.offset or index > self.offset + count) then self.offset = math.max(0, index - math.floor(count / 2)) end
    end
    l:EnableMouseWheel(true)
    l:SetScript("OnMouseWheel", function(self, delta) self.offset = self.offset - delta * 3; self:Refresh() end)
    return l
end

-- Actions ----------------------------------------------------------------------------------------------------------
local refresh, openTo

local function toggleTrack(id)
    if IsTrackedAchievement(id) then
        RemoveTrackedAchievement(id)
    else
        local cap = WATCHFRAME_MAXACHIEVEMENTS or 10
        if (GetNumTrackedAchievements() or 0) >= cap then
            UIErrorsFrame:AddMessage(format(ACHIEVEMENT_WATCH_TOO_MANY or "You may only track %d achievements at a time.", cap), 1, 0.1, 0.1)
            return
        end
        AddTrackedAchievement(id)
    end
    if WatchFrame_Update then WatchFrame_Update() end
    dirty = true
end

local function linkOrSelect(id)
    if IsModifiedClick("CHATLINK") then
        local link = GetAchievementLink(id)
        if link and ChatEdit_GetActiveWindow and ChatEdit_GetActiveWindow() then ChatEdit_InsertLink(link) end
        return true
    end
end

local function achievementTooltip(owner, id)
    GameTooltip:SetOwner(owner, "ANCHOR_RIGHT")
    local link = GetAchievementLink(id)
    if link then GameTooltip:SetHyperlink(link) end
    GameTooltip:Show()
end

-- Summary ----------------------------------------------------------------------------------------------------------
local function buildSummary(p)
    local totals = panel(p); totals:SetPoint("TOPLEFT", 14, -14); totals:SetSize(440, 200)
    gradient(totals, 120, 0.25)
    heading(totals, "Achievement points"):SetPoint("TOPLEFT", 18, -16)
    p.points = ES.Text(totals, 44, ES.GOLD); p.points:SetFont(ES.FONT, 44, "OUTLINE"); p.points:SetPoint("TOPLEFT", 16, -38)
    p.completed = ES.Text(totals, 13, ES.TEXT); p.completed:SetPoint("TOPLEFT", 18, -100)
    p.barBg, p.barFill = bar(totals, 8)
    p.barBg:SetPoint("TOPLEFT", 18, -126); p.barBg:SetPoint("TOPRIGHT", -18, -126)
    p.percent = ES.Text(totals, 11, ES.MUTED); p.percent:SetPoint("TOPRIGHT", p.barBg, "BOTTOMRIGHT", 0, -6)
    p.tracked = ES.Text(totals, 11, ES.MUTED); p.tracked:SetPoint("TOPLEFT", p.barBg, "BOTTOMLEFT", 0, -6)

    local recent = panel(p); recent:SetPoint("TOPRIGHT", -14, -14); recent:SetSize(464, 200)
    heading(recent, "Recently earned"):SetPoint("TOPLEFT", 18, -16)
    p.recent = {}
    for i = 1, 5 do
        local r = CreateFrame("Button", nil, recent); r:SetSize(436, 32); r:SetPoint("TOPLEFT", 14, -34 - (i - 1) * 32)
        r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
        r.icon = iconBox(r, 26); r.icon:SetPoint("LEFT", 2, 0)
        r.name = ES.Text(r, 12, ES.TEXT); r.name:SetPoint("LEFT", 38, 0); r.name:SetPoint("RIGHT", -110, 0); r.name:SetJustifyH("LEFT")
        r.date = ES.Text(r, 10, ES.MUTED); r.date:SetPoint("RIGHT", -46, 0)
        r.badge = pointsBadge(r); r.badge:SetPoint("RIGHT", -4, 0)
        r:SetScript("OnEnter", function(self) self.wash:SetAlpha(0.06); if self.id then achievementTooltip(self, self.id) end end)
        r:SetScript("OnLeave", function(self) self.wash:SetAlpha(0); GameTooltip:Hide() end)
        r:SetScript("OnClick", function(self) if self.id and not linkOrSelect(self.id) then openTo(self.id) end end)
        p.recent[i] = r
    end
    p.noRecent = ES.Text(recent, 12, ES.MUTED); p.noRecent:SetPoint("CENTER", 0, -10); p.noRecent:SetText("Nothing earned yet.")

    local cats = panel(p); cats:SetPoint("TOPLEFT", totals, "BOTTOMLEFT", 0, -14); cats:SetPoint("BOTTOMRIGHT", -14, 14)
    heading(cats, "Progress by category"):SetPoint("TOPLEFT", 18, -16)
    p.cats = {}
    for i = 1, 14 do
        local c = CreateFrame("Button", nil, cats); c:SetSize(436, 40)
        c:SetPoint("TOPLEFT", 14 + ((i - 1) % 2) * 456, -36 - math.floor((i - 1) / 2) * 44)
        c.wash = ES.Solid(c, "BACKGROUND", ES.GOLD, 0); c.wash:SetAllPoints()
        c.name = ES.Text(c, 12, ES.TEXT); c.name:SetPoint("TOPLEFT", 6, -6)
        c.count = ES.Text(c, 11, ES.MUTED); c.count:SetPoint("TOPRIGHT", -6, -6)
        c.barBg, c.barFill = bar(c, 5); c.barBg:SetPoint("BOTTOMLEFT", 6, 8); c.barBg:SetPoint("BOTTOMRIGHT", -6, 8)
        c:SetScript("OnEnter", function(self) self.wash:SetAlpha(0.06) end)
        c:SetScript("OnLeave", function(self) self.wash:SetAlpha(0) end)
        c:SetScript("OnClick", function(self)
            PlaySound("igMainMenuOptionCheckBoxOn")
            selectedCat.ach = self.cat; selectedID = nil; panes.Achievements.list.offset = 0; EsteriaAchievements_Select("Achievements")
        end)
        p.cats[i] = c
    end
end

local function paintSummary(p)
    local total, done = GetNumCompletedAchievements()
    total, done = total or 0, done or 0
    p.points:SetText(GetTotalAchievementPoints() or 0)
    p.completed:SetText(("|cffe8c46b%d|r of %d achievements earned"):format(done, total))
    setBar(p.barBg, p.barFill, total > 0 and done / total or 0)
    p.percent:SetText(("%d%%"):format(total > 0 and done / total * 100 or 0))
    p.tracked:SetText(("Tracking %d / %d"):format(GetNumTrackedAchievements() or 0, WATCHFRAME_MAXACHIEVEMENTS or 10))
    local latest = { GetLatestCompletedAchievements() }
    for i, r in ipairs(p.recent) do
        local id = latest[i]
        r.id = id
        if id then
            local _, name, points, _, month, day, year, _, _, icon = GetAchievementInfo(id)
            r.icon.icon:SetTexture(icon); r.name:SetText(name); r.date:SetText(dateText(month, day, year)); r.badge:Set(points, true)
            r:Show()
        else r:Hide() end
    end
    shown(p.noRecent, #latest == 0)
    local m = models.ach
    for i, c in ipairs(p.cats) do
        local id = m.roots[i]
        local cat = id and m.cats[id]
        c.cat = id
        if cat then
            c.name:SetText(cat.name)
            c.count:SetText(("%d / %d"):format(cat.sumDone, cat.sumTotal))
            setBar(c.barBg, c.barFill, cat.sumTotal > 0 and cat.sumDone / cat.sumTotal or 0,
                cat.sumTotal > 0 and cat.sumDone == cat.sumTotal and ES.GOOD or ES.GOLD)
            c:Show()
        else c:Hide() end
    end
end

-- Rail (shared by Achievements and Statistics) ---------------------------------------------------------------------
local function buildRail(p, kind, title)
    local rail = panel(p); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    heading(rail, title):SetPoint("TOPLEFT", 14, -12)
    p.rail = vlist(rail, RAIL_W - 12, RAIL_H, RAILS, function(parent, w, h)
        local row = CreateFrame("Button", nil, parent); row:SetSize(w, h)
        row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
        row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
        row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
        row.label = ES.Text(row, 12, ES.TEXT); row.label:SetPoint("RIGHT", -52, 0); row.label:SetJustifyH("LEFT")
        row.count = ES.Text(row, 10, ES.MUTED); row.count:SetPoint("RIGHT", -8, 0)
        row:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.06) end end)
        row:SetScript("OnLeave", function(self) if not self.selected then self.wash:SetAlpha(0) end end)
        row:SetScript("OnClick", function(self)
            PlaySound("igMainMenuOptionCheckBoxOn")
            selectedCat[kind] = self.item.id; p.list.offset = 0
            if p.search then p.search:SetText("") end
            refresh()
        end)
        return row
    end, function(row, e)
        local cat = models[kind].cats[e.id]
        local on = selectedCat[kind] == e.id
        row.selected = on
        row.label:ClearAllPoints(); row.label:SetPoint("LEFT", 14 + e.depth * 14, 0); row.label:SetPoint("RIGHT", -52, 0)
        row.label:SetText(cat.name); row.label:SetFont(ES.FONT, e.depth > 0 and 11 or 12)
        row.label:SetTextColor(unpack(on and ES.GOLD or (e.depth > 0 and ES.MUTED or ES.TEXT)))
        row.wash:SetAlpha(on and 0.12 or 0); row.accent:SetAlpha(on and 1 or 0)
        row.count:SetText(kind == "ach" and ("%d/%d"):format(cat.sumDone, cat.sumTotal) or cat.sumTotal)
        row.count:SetTextColor(unpack(kind == "ach" and cat.sumTotal > 0 and cat.sumDone == cat.sumTotal and ES.GOOD or ES.MUTED))
    end, true)
    p.rail:SetPoint("TOPLEFT", 6, -30)
    return rail
end

-- Achievements -----------------------------------------------------------------------------------------------------
local FILTERS = { { "all", "All" }, { "earned", "Earned" }, { "unearned", "Unearned" } }
local SORTS = { { "default", "Default" }, { "recent", "Recent" }, { "points", "Points" }, { "name", "Name" } }

local function makeCard(parent, w, h)
    local r = CreateFrame("Button", nil, parent); r:SetSize(w, h - 4)
    r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
    r.edge = ES.Solid(r, "ARTWORK", ES.GOLD, 0); r.edge:SetPoint("TOPLEFT"); r.edge:SetPoint("BOTTOMLEFT"); r.edge:SetWidth(3)
    r.icon = iconBox(r, 40); r.icon:SetPoint("LEFT", 8, 0)
    r.name = ES.Text(r, 12, ES.TEXT); r.name:SetPoint("TOPLEFT", 58, -7); r.name:SetPoint("RIGHT", -50, 0); r.name:SetJustifyH("LEFT")
    r.desc = ES.Text(r, 10, ES.MUTED); r.desc:SetPoint("TOPLEFT", 58, -23); r.desc:SetPoint("RIGHT", -50, 0)
    r.desc:SetJustifyH("LEFT"); r.desc:SetJustifyV("TOP"); r.desc:SetHeight(24)
    r.badge = pointsBadge(r); r.badge:SetPoint("TOPRIGHT", -8, -7)
    r.date = ES.Text(r, 9, ES.MUTED); r.date:SetPoint("TOP", r.badge, "BOTTOM", 0, -4)
    r.barBg, r.barFill = bar(r, 3); r.barBg:SetPoint("BOTTOMLEFT", 58, 4); r.barBg:SetPoint("BOTTOMRIGHT", -50, 4)
    r.tracked = ES.Solid(r, "OVERLAY", ES.GOLD, 1); r.tracked:SetSize(6, 6); r.tracked:SetPoint("TOPRIGHT", r.icon, "TOPRIGHT", -2, -2)
    r:SetScript("OnEnter", function(self) if self.item and self.item.id ~= selectedID then self.wash:SetAlpha(0.06) end end)
    r:SetScript("OnLeave", function(self) if self.item and self.item.id ~= selectedID then self.wash:SetAlpha(0) end end)
    r:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    r:SetScript("OnClick", function(self, mouse)
        local a = self.item
        if mouse == "RightButton" then if not a.completed then toggleTrack(a.id) end return end
        if linkOrSelect(a.id) then return end
        PlaySound("igMainMenuOptionCheckBoxOn")
        selectedID = a.id; refresh()
    end)
    return r
end

local function fillCard(r, a)
    local on = a.id == selectedID
    r.wash:SetAlpha(on and 0.12 or (a.completed and 0.04 or 0))
    r.edge:SetAlpha(on and 1 or 0)
    r.icon.icon:SetTexture(a.icon); r.icon.icon:SetDesaturated(not a.completed)
    ES.Tint(r.icon.lines, a.completed and ES.GOLD or ES.GOLD_DIM, a.completed and 1 or 0.6)
    r.name:SetText(a.name); r.name:SetTextColor(unpack(on and ES.GOLD or (a.completed and ES.TEXT or ES.MUTED)))
    r.desc:SetText(a.desc)
    r.badge:Set(a.points, a.completed)
    r.date:SetText(a.completed and dateText(a.month, a.day, a.year) or "")
    local showBar = not a.completed
    shown(r.barBg, showBar); shown(r.barFill, showBar)
    if showBar then setBar(r.barBg, r.barFill, progress(a.id, a.flags)) end
    shown(r.tracked, IsTrackedAchievement(a.id) and true or false)
end

local pools, used = { text = {}, crit = {}, chain = {} }, { text = 0, crit = 0, chain = 0 }
local function take(kind, make)
    used[kind] = used[kind] + 1
    local r = pools[kind][used[kind]]
    if not r then r = make(); pools[kind][used[kind]] = r end
    r:Show()
    return r
end

local function buildAchievements(p)
    local rail = buildRail(p, "ach", "Categories")
    local middle = panel(p); middle:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); middle:SetPoint("BOTTOM", 0, 14)
    middle:SetWidth(LIST_W)
    p.search = searchBox(middle, "EsteriaAchievementsSearch", LIST_W - 24, "Search every achievement",
        function() p.list.offset = 0; refresh() end)
    p.search:SetPoint("TOPLEFT", 12, -12)
    p.filterTabs, p.sortTabs = {}, {}
    for i, f in ipairs(FILTERS) do
        local t = tabButton(middle, 70, f[2], 24); t.key = f[1]; t:SetPoint("TOPLEFT", 10 + (i - 1) * 72, -44)
        t:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); db.filter = f[1]; p.list.offset = 0; refresh() end)
        p.filterTabs[i] = t
    end
    p.sortButton = button(middle, 104, "", false); p.sortButton:SetHeight(24); p.sortButton:SetPoint("TOPRIGHT", -12, -44)
    p.sortButton:SetScript("OnClick", function()
        PlaySound("igMainMenuOptionCheckBoxOn")
        local idx = 1
        for i, s in ipairs(SORTS) do if s[1] == db.sort then idx = i end end
        db.sort = SORTS[idx % #SORTS + 1][1]; p.list.offset = 0; refresh()
    end)
    p.list = vlist(middle, LIST_W - 16, CARD_H, CARDS, makeCard, fillCard)
    p.list:SetPoint("TOPLEFT", 8, -78)
    p.list.empty:SetText("No achievements match.")

    -- Detail.
    local d = panel(p); d:SetPoint("TOPLEFT", middle, "TOPRIGHT", 14, 0); d:SetPoint("BOTTOMRIGHT", -14, 14)
    d.width = W - (14 + RAIL_W + 14 + LIST_W + 14) - 14
    d.tint = gradient(d, 110, 0.22)
    d.icon = iconBox(d, 56); d.icon:SetPoint("TOPLEFT", 16, -16)
    d.title = ES.Text(d, 16, ES.GOLD); d.title:SetPoint("TOPLEFT", 84, -18); d.title:SetPoint("RIGHT", -56, 0); d.title:SetJustifyH("LEFT")
    d.badge = pointsBadge(d); d.badge:SetPoint("TOPRIGHT", -16, -16)
    d.state = ES.Text(d, 11, ES.MUTED); d.state:SetPoint("TOPLEFT", d.title, "BOTTOMLEFT", 0, -6)
    d.category = ES.Text(d, 10, ES.GOLD_DIM); d.category:SetPoint("TOPLEFT", d.state, "BOTTOMLEFT", 0, -4)
    d.none = ES.Text(d, 12, ES.MUTED); d.none:SetPoint("CENTER"); d.none:SetText("Select an achievement.")
    d.scroll = CreateFrame("ScrollFrame", nil, d)
    d.scroll:SetPoint("TOPLEFT", 0, -92); d.scroll:SetPoint("BOTTOMRIGHT", 0, 52)
    d.child = CreateFrame("Frame", nil, d.scroll); d.child:SetSize(d.width, 10); d.scroll:SetScrollChild(d.child)
    d.scroll:EnableMouseWheel(true)
    d.scroll:SetScript("OnMouseWheel", function(self, delta)
        local range = math.max(0, d.child:GetHeight() - self:GetHeight())
        self:SetVerticalScroll(math.max(0, math.min(range, self:GetVerticalScroll() - delta * 40)))
    end)
    local rule = ES.Solid(d, "ARTWORK", ES.GOLD_DIM, 0.4)
    rule:SetPoint("BOTTOMLEFT", 12, 50); rule:SetPoint("BOTTOMRIGHT", -12, 50); rule:SetHeight(1)
    local bw = math.floor((d.width - 24 - 6) / 2)
    d.track = button(d, bw, "Track", true); d.track:SetPoint("BOTTOMLEFT", 12, 12)
    d.link = button(d, bw, "Link in chat"); d.link:SetPoint("BOTTOMLEFT", 18 + bw, 12)
    d.track:SetScript("OnClick", function() if selectedID then toggleTrack(selectedID) end end)
    d.link:SetScript("OnClick", function()
        local link = selectedID and GetAchievementLink(selectedID)
        if link then
            if not (ChatEdit_GetActiveWindow and ChatEdit_GetActiveWindow()) and ChatFrame_OpenChat then ChatFrame_OpenChat("") end
            ChatEdit_InsertLink(link)
        end
    end)
    p.detail = d
end

local function paintDetail(d)
    for kind, pool in pairs(pools) do for _, r in ipairs(pool) do r:Hide() end used[kind] = 0 end
    local id = selectedID
    local aid, name, points, completed, month, day, year, desc, flags, icon, reward = nil
    if id then aid, name, points, completed, month, day, year, desc, flags, icon, reward = GetAchievementInfo(id) end
    shown(d.none, not aid)
    for _, region in ipairs({ d.icon, d.title, d.badge, d.state, d.category, d.scroll }) do shown(region, aid ~= nil) end
    d.track:Active(aid ~= nil and not completed); d.link:Active(aid ~= nil)
    if not aid then return end
    d.icon.icon:SetTexture(icon); d.icon.icon:SetDesaturated(not completed)
    ES.Tint(d.icon.lines, completed and ES.GOLD or ES.GOLD_DIM, 1)
    d.title:SetText(name); d.badge:Set(points, completed)
    d.state:SetText(completed and ("|cff8cd97fEarned " .. dateText(month, day, year) .. "|r") or
        ("%d%% complete"):format(progress(aid, flags) * 100))
    local cat = GetAchievementCategory and GetAchievementCategory(aid)
    local catName = cat and GetCategoryInfo(cat)
    d.category:SetText(catName and catName:upper() or "")
    d.track.label:SetText(IsTrackedAchievement(aid) and "Untrack" or "Track")

    local child, width, y = d.child, d.width - 32, 0
    local function text(str, size, color)
        local fs = take("text", function() local t = ES.Text(child, 12, ES.TEXT); t:SetJustifyH("LEFT"); return t end)
        fs:SetFont(ES.FONT, size); fs:SetTextColor(unpack(color)); fs:SetWidth(width); fs:SetText(str)
        fs:ClearAllPoints(); fs:SetPoint("TOPLEFT", 16, -y)
        y = y + fs:GetStringHeight() + 6
    end
    local function section(label) y = y + 8; text(label:upper(), 10, ES.GOLD_DIM); y = y + 2 end

    text(desc or "", 12, ES.TEXT)
    local n = GetAchievementNumCriteria(aid) or 0
    if n > 0 then section("Criteria") end
    for i = 1, n do
        local ctext, ctype, cdone, quantity, required, _, cflags, asset, quantityString = GetAchievementCriteriaInfo(aid, i)
        local barred = band(cflags, CRITERIA_PROGRESS_BAR) or (band(flags, FLAG_HAS_PROGRESS_BAR) and n == 1)
        if not band(cflags, CRITERIA_HIDDEN) or barred then
            local row = take("crit", function()
                local r = CreateFrame("Button", nil, child); r:SetSize(width, 20)
                r.check = r:CreateTexture(nil, "ARTWORK"); r.check:SetSize(14, 14); r.check:SetPoint("TOPLEFT", 0, -1)
                r.icon = r:CreateTexture(nil, "ARTWORK"); r.icon:SetSize(16, 16); r.icon:SetPoint("TOPLEFT", 18, 0)
                r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
                r.text = ES.Text(r, 12, ES.TEXT); r.text:SetJustifyH("LEFT")
                r.amount = ES.Text(r, 10, ES.MUTED); r.amount:SetPoint("TOPRIGHT", 0, -2)
                r.barBg, r.barFill = bar(r, 5); r.barBg:SetPoint("BOTTOMLEFT", 18, 2); r.barBg:SetPoint("BOTTOMRIGHT")
                r:SetScript("OnClick", function(self) if self.asset then openTo(self.asset) end end)
                r:SetScript("OnEnter", function(self) if self.asset then achievementTooltip(self, self.asset) end end)
                r:SetScript("OnLeave", function() GameTooltip:Hide() end)
                return r
            end)
            row:ClearAllPoints(); row:SetPoint("TOPLEFT", 16, -y)
            row.check:SetTexture(cdone and "Interface\\RAIDFRAME\\ReadyCheck-Ready" or "Interface\\RAIDFRAME\\ReadyCheck-Waiting")
            local meta = ctype == CRITERIA_TYPE_ACHIEVEMENT and asset and asset > 0
            row.asset = meta and asset or nil
            local metaIcon = meta and select(10, GetAchievementInfo(asset))
            shown(row.icon, metaIcon ~= nil); if metaIcon then row.icon:SetTexture(metaIcon) end
            row.text:ClearAllPoints(); row.text:SetPoint("TOPLEFT", metaIcon and 38 or 18, -1)
            row.text:SetPoint("RIGHT", barred and -70 or 0, 0)
            row.text:SetText(ctext ~= "" and ctext or (name or "")); row.text:SetTextColor(unpack(cdone and ES.GOOD or ES.TEXT))
            shown(row.amount, barred); shown(row.barBg, barred); shown(row.barFill, barred)
            if barred then
                row.amount:SetText(quantityString or ("%d / %d"):format(quantity or 0, required or 0))
                setBar(row.barBg, row.barFill, (required or 0) > 0 and (quantity or 0) / required or (cdone and 1 or 0),
                    cdone and ES.GOOD or ES.GOLD)
            end
            local h = math.max(18, row.text:GetStringHeight() + 4) + (barred and 9 or 0)
            row:SetHeight(h); y = y + h + 4
        end
    end
    if reward and reward ~= "" then section("Reward"); text(reward, 12, ES.GOLD) end

    -- The series this achievement belongs to, oldest first.
    local chain = {}
    local prev = GetPreviousAchievement and GetPreviousAchievement(aid)
    while prev do table.insert(chain, 1, prev); prev = GetPreviousAchievement(prev) end
    chain[#chain + 1] = aid
    local nxt = GetNextAchievement and GetNextAchievement(aid)
    while nxt do chain[#chain + 1] = nxt; nxt = GetNextAchievement(nxt) end
    if #chain > 1 then
        section("Series")
        local x = 0
        for _, cid in ipairs(chain) do
            local b = take("chain", function()
                local c = CreateFrame("Button", nil, child); c:SetSize(34, 34)
                c.box = iconBox(c, 32); c.box:SetPoint("CENTER")
                c:SetScript("OnClick", function(self) openTo(self.id) end)
                c:SetScript("OnEnter", function(self) achievementTooltip(self, self.id) end)
                c:SetScript("OnLeave", function() GameTooltip:Hide() end)
                return c
            end)
            local _, _, _, cdone, _, _, _, _, _, cicon = GetAchievementInfo(cid)
            b.id = cid
            b.box.icon:SetTexture(cicon); b.box.icon:SetDesaturated(not cdone)
            ES.Tint(b.box.lines, cid == aid and ES.GOLD or ES.GOLD_DIM, cid == aid and 1 or 0.6)
            b:ClearAllPoints(); b:SetPoint("TOPLEFT", 16 + x, -y)
            x = x + 38
            if x + 34 > width then x = 0; y = y + 38 end
        end
        y = y + 40
    end
    child:SetHeight(y + 16)
    d.scroll:UpdateScrollChildRect()
end

local function paintAchievements(p)
    local m = models.ach
    if not selectedCat.ach or not m.cats[selectedCat.ach] then selectedCat.ach = m.roots[1] end
    p.rail.data = railEntries("ach"); p.rail:Refresh()
    p.list.data = filtered("ach", p.search:GetText():lower(), db.filter, db.sort)
    if not selectedID and p.list.data[1] then selectedID = p.list.data[1].id end
    p.list:Refresh()
    for _, t in ipairs(p.filterTabs) do t:SetSelected(t.key == db.filter) end
    for _, s in ipairs(SORTS) do if s[1] == db.sort then p.sortButton.label:SetText("Sort: " .. s[2]) end end
    paintDetail(p.detail)
end

-- Statistics -------------------------------------------------------------------------------------------------------
local function buildStatistics(p)
    local rail = buildRail(p, "stat", "Statistics")
    local body = panel(p); body:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); body:SetPoint("BOTTOMRIGHT", -14, 14)
    local width = W - (14 + RAIL_W + 14) - 14
    p.search = searchBox(body, "EsteriaStatisticsSearch", 300, "Search statistics", function() p.list.offset = 0; refresh() end)
    p.search:SetPoint("TOPLEFT", 12, -12)
    p.title = ES.Text(body, 14, ES.GOLD); p.title:SetPoint("TOPRIGHT", -18, -18)
    p.list = vlist(body, width - 16, STAT_H, STATS, function(parent, w, h)
        local r = CreateFrame("Button", nil, parent); r:SetSize(w, h)
        r.stripe = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.stripe:SetAllPoints()
        r.name = ES.Text(r, 12, ES.TEXT); r.name:SetPoint("LEFT", 12, 0); r.name:SetPoint("RIGHT", -200, 0); r.name:SetJustifyH("LEFT")
        r.value = ES.Text(r, 12, ES.GOLD); r.value:SetPoint("RIGHT", -12, 0); r.value:SetJustifyH("RIGHT")
        r:SetScript("OnEnter", function(self) self.stripe:SetAlpha(0.08) end)
        r:SetScript("OnLeave", function(self) self.stripe:SetAlpha(self.base or 0) end)
        r:SetScript("OnClick", function(self) if self.item then linkOrSelect(self.item.id) end end)
        return r
    end, function(r, s)
        r.base = s.order % 2 == 0 and 0.03 or 0; r.stripe:SetAlpha(r.base)
        r.name:SetText(s.name)
        local value = GetStatistic(s.id)
        r.value:SetText((value == nil or value == "" or value == "--") and "|cff8a8a80—|r" or value)
    end)
    p.list:SetPoint("TOPLEFT", 8, -48)
    p.list.empty:SetText("No statistics match.")
end

local function paintStatistics(p)
    local m = models.stat
    if not selectedCat.stat or not m.cats[selectedCat.stat] then selectedCat.stat = m.roots[1] end
    p.rail.data = railEntries("stat"); p.rail:Refresh()
    local query = p.search:GetText():lower()
    p.list.data = filtered("stat", query, "all", "default")
    p.title:SetText(query ~= "" and ("%d results"):format(#p.list.data) or (m.cats[selectedCat.stat] and m.cats[selectedCat.stat].name or ""))
    p.list:Refresh()
end

-- Window -----------------------------------------------------------------------------------------------------------
local PANES = {
    { "Summary", buildSummary, paintSummary },
    { "Achievements", buildAchievements, paintAchievements },
    { "Statistics", buildStatistics, paintStatistics },
}

local header
refresh = function()
    if not frame or not frame:IsShown() then return end
    local total, done = GetNumCompletedAchievements()
    header.points:SetText(("|cffe8c46b%d|r points     |cffe8c46b%d|r / %d earned"):format(GetTotalAchievementPoints() or 0, done or 0, total or 0))
    header.fill:SetWidth(math.max(1, header.meter:GetWidth() * ((total or 0) > 0 and done / total or 0)))
    for _, spec in ipairs(PANES) do if spec[1] == current then spec[3](panes[current]) end end
end

function EsteriaAchievements_Select(name)
    current = name
    for _, spec in ipairs(PANES) do
        tabs[spec[1]]:SetSelected(spec[1] == name)
        shown(panes[spec[1]], spec[1] == name)
    end
    refresh()
end

local function rescan()
    scan("ach"); scan("stat")
    progressCache = {}
end

local function build()
    EsteriaAchievementsDB = EsteriaAchievementsDB or {}
    db = EsteriaAchievementsDB
    db.filter = db.filter or "all"
    db.sort = db.sort or "default"

    frame = CreateFrame("Frame", "EsteriaAchievementsFrame", UIParent)
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving); frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames, "EsteriaAchievementsFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(frame, ES.GOLD)
    local top = frame:CreateTexture(nil, "BORDER"); top:SetTexture(ES.WHITE)
    top:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.28)
    top:SetPoint("TOPLEFT", 1, -1); top:SetPoint("TOPRIGHT", -1, -1); top:SetHeight(42)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12); title:SetText("Achievements")
    header = {}
    header.points = ES.Text(frame, 11, ES.TEXT); header.points:SetPoint("TOPRIGHT", -40, -12)
    header.meter = CreateFrame("Frame", nil, frame); header.meter:SetSize(220, 3)
    header.meter:SetPoint("TOPRIGHT", header.points, "BOTTOMRIGHT", 0, -5)
    ES.Solid(header.meter, "BACKGROUND", { 0, 0, 0 }, 0.6):SetAllPoints()
    header.fill = ES.Solid(header.meter, "ARTWORK", ES.GOLD, 0.9); header.fill:SetPoint("TOPLEFT"); header.fill:SetPoint("BOTTOMLEFT")
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    for i, spec in ipairs(PANES) do
        local b = tabButton(frame, 110, spec[1]); b:SetPoint("TOPLEFT", 180 + (i - 1) * 112, -6)
        b:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); EsteriaAchievements_Select(spec[1]) end)
        tabs[spec[1]] = b
        local pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")
        spec[2](pane); pane:Hide()
        panes[spec[1]] = pane
    end
    frame:SetScript("OnUpdate", function()
        if dirty then dirty = false; progressDirty = false; rescan(); refresh()
        elseif progressDirty then progressDirty = false; progressCache = {}; refresh() end
    end)
    frame:SetScript("OnShow", function() PlaySound("AchievementMenuOpen"); rescan(); dirty = false; EsteriaAchievements_Select(current) end)
    frame:SetScript("OnHide", function() PlaySound("AchievementMenuClose"); GameTooltip:Hide() end)
    for _, ev in ipairs({ "ACHIEVEMENT_EARNED", "CRITERIA_UPDATE", "TRACKED_ACHIEVEMENT_UPDATE" }) do frame:RegisterEvent(ev) end
    frame:SetScript("OnEvent", function(_, ev)
        if ev == "ACHIEVEMENT_EARNED" then dirty = true else progressDirty = true end
    end)
end

openTo = function(id)
    if not frame then build() end
    if not frame:IsShown() then frame:Show() end
    if id then
        selectedID = id
        selectedCat.ach = GetAchievementCategory and GetAchievementCategory(id) or selectedCat.ach
        local p = panes.Achievements
        p.search:SetText("")
        db.filter = "all"
        EsteriaAchievements_Select("Achievements")
        for i, a in ipairs(p.list.data) do if a.id == id then p.list:Reveal(i); break end end
        refresh()
    end
end
EsteriaAchievements_Open = openTo

function EsteriaAchievements_Toggle(tab)
    if not frame then build() end
    if frame:IsShown() and (not tab or tab == current) then frame:Hide(); return end
    if tab then current = tab end
    if frame:IsShown() then EsteriaAchievements_Select(current) else frame:Show() end
end

-- Replace the stock window: binding and micro button call ToggleAchievementFrame; alerts and the tracker load the
-- stock addon and call its select/toggle functions, which are redirected once it loads.
ToggleAchievementFrame = function(stats) EsteriaAchievements_Toggle(stats and "Statistics" or nil) end

local function redirectStock()
    AchievementFrame_SelectAchievement = function(id) openTo(id) end
    AchievementFrame_ToggleAchievementFrame = function(stats) EsteriaAchievements_Toggle(stats and "Statistics" or nil) end
    local compare = AchievementFrame_DisplayComparison
    if compare then
        AchievementFrame_DisplayComparison = function(...) comparing = true; return compare(...) end
    end
    AchievementFrame:HookScript("OnShow", function(self) if not comparing then HideUIPanel(self); openTo() end end)
    AchievementFrame:HookScript("OnHide", function() comparing = false end)
end

if AchievementFrame then
    redirectStock()
else
    local watcher = CreateFrame("Frame")
    watcher:RegisterEvent("ADDON_LOADED")
    watcher:SetScript("OnEvent", function(self, _, name)
        if name == "Blizzard_AchievementUI" then self:UnregisterEvent("ADDON_LOADED"); redirectStock() end
    end)
end

SLASH_ESTERIAACHIEVEMENTS1 = "/achievements"
SLASH_ESTERIAACHIEVEMENTS2 = "/ach"
SlashCmdList.ESTERIAACHIEVEMENTS = function() EsteriaAchievements_Toggle() end
