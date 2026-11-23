-- Esteria's quest log: a searchable, filterable journal for up to 1000 active quests, in the PvP/Collections look.
-- Left rail filters by status, type and zone; the middle lists quests (grouped by zone or sorted); the right pane shows
-- objectives, story and rewards. Replaces the stock quest log everywhere it opens (key binding, micro button, tracker).
local ES = EsteriaStyle
local W, H = 960, 640
local RAIL_W, LIST_W = 210, 344
local ROW_H, ROWS = 38, 13
local ZONE_ROWS = 7

local frame, pane, search, slider, detail, counter, meter
local listRows, statusRows, typeRows, zoneRows, sortTabs = {}, {}, {}, {}, {}
local quests, rows, zones = {}, {}, {}
local statusCounts, typeCounts = {}, {}
local selectedID, offset, zoneOffset = nil, 0, 0
local statusFilter, typeFilter, zoneFilter = "all", nil, nil
local dirty = true
local db

local function capacity() return GetQuestLogCapacity and GetQuestLogCapacity() or 25 end
-- Stock FrameXML (quest log count, tracker) reads this; the 1000-slot client reports its real ceiling.
MAX_QUESTLOG_QUESTS = capacity()

local STATUS = { { "all", "All quests" }, { "progress", "In progress" }, { "complete", "Ready to turn in" },
                 { "failed", "Failed" }, { "tracked", "Tracked" } }
local TYPES = { { "daily", "Daily" }, { "group", "Group & Elite" }, { "dungeon", "Dungeon" }, { "raid", "Raid" },
                { "pvp", "PvP" } }
local SORTS = { { "zone", "Zone" }, { "level", "Level" }, { "name", "Name" }, { "progress", "Progress" } }

local function shown(region, on) if on then region:Show() else region:Hide() end end

-- Pure helpers (also exercised by test_questlog.py) ------------------------------------------------------------------
local function isType(q, key)
    local tag = (q.tag or ""):lower()
    if key == "daily" then return q.daily end
    if key == "group" then return (q.group or 0) > 0 or tag:find("elite") ~= nil or tag:find("group") ~= nil end
    if key == "dungeon" then return tag:find("dungeon") ~= nil or tag:find("heroic") ~= nil end
    if key == "raid" then return tag:find("raid") ~= nil end
    if key == "pvp" then return tag:find("pvp") ~= nil end
    return true
end

local function isStatus(q, key)
    if key == "progress" then return q.complete ~= 1 and q.complete ~= -1 end
    if key == "complete" then return q.complete == 1 end
    if key == "failed" then return q.complete == -1 end
    if key == "tracked" then return q.watched end
    return true
end

-- "Kobold slain: 3/8" -> 0.375; finished objectives count fully.
local function objectiveProgress(text, finished)
    if finished then return 1 end
    local have, need = (text or ""):match("(%d+)%s*/%s*(%d+)")
    have, need = tonumber(have), tonumber(need)
    if have and need and need > 0 then return math.min(1, have / need) end
    return 0
end

local function matches(q, query)
    if query == "" then return true end
    return q.title:lower():find(query, 1, true) or q.zone:lower():find(query, 1, true) or
        q.search:find(query, 1, true) or tostring(q.id) == query
end

EsteriaQuestLog_Test = { isType = isType, isStatus = isStatus, objectiveProgress = objectiveProgress, matches = matches }

-- Model --------------------------------------------------------------------------------------------------------------
local function scan()
    local n = GetNumQuestLogEntries()
    -- Collapsed headers hide their quests from the API; this window does its own grouping, so open them all.
    for i = 1, n do
        local _, _, _, _, isHeader, collapsed = GetQuestLogTitle(i)
        if isHeader and collapsed then ExpandQuestHeader(0); n = GetNumQuestLogEntries(); break end
    end
    quests, zones = {}, {}
    local zone = ""
    for i = 1, n do
        local title, level, tag, group, isHeader, _, complete, daily, id = GetQuestLogTitle(i)
        if isHeader then
            zone = title or ""
            zones[#zones + 1] = { name = zone, count = 0 }
        elseif title then
            local q = { index = i, title = title, level = level or 0, tag = tag, group = group or 0,
                        complete = complete, daily = daily and true or false, id = id or 0, zone = zone,
                        watched = IsQuestWatched(i) and true or false, objs = {} }
            local texts, sum = {}, 0
            for j = 1, GetNumQuestLeaderBoards(i) or 0 do
                local text, _, finished = GetQuestLogLeaderBoard(j, i)
                q.objs[j] = { text = text or "", finished = finished and true or false }
                texts[j] = text or ""
                sum = sum + objectiveProgress(text, finished)
            end
            q.progress = complete == 1 and 1 or (#q.objs > 0 and sum / #q.objs or 0)
            q.search = (table.concat(texts, "\n") .. " " .. (tag or "")):lower()
            quests[#quests + 1] = q
            if zones[#zones] then zones[#zones].count = zones[#zones].count + 1 end
        end
    end
end

local function selected()
    for _, q in ipairs(quests) do if q.id == selectedID then return q end end
end

local function rebuild()
    local query = search and search:GetText():lower() or ""
    statusCounts, typeCounts = {}, {}
    local list = {}
    for _, q in ipairs(quests) do
        for _, s in ipairs(STATUS) do if isStatus(q, s[1]) then statusCounts[s[1]] = (statusCounts[s[1]] or 0) + 1 end end
        for _, t in ipairs(TYPES) do if isType(q, t[1]) then typeCounts[t[1]] = (typeCounts[t[1]] or 0) + 1 end end
        if isStatus(q, statusFilter) and (not typeFilter or isType(q, typeFilter)) and
            (not zoneFilter or q.zone == zoneFilter) and matches(q, query) then
            list[#list + 1] = q
        end
    end
    local sort = db.sort
    if sort == "level" then
        table.sort(list, function(a, b) if a.level ~= b.level then return a.level < b.level end return a.title < b.title end)
    elseif sort == "name" then
        table.sort(list, function(a, b) return a.title < b.title end)
    elseif sort == "progress" then
        table.sort(list, function(a, b) if a.progress ~= b.progress then return a.progress > b.progress end return a.title < b.title end)
    end
    rows = {}
    if sort == "zone" then
        -- The log keeps each zone's quests together, so a header goes in whenever the zone changes.
        local header
        for _, q in ipairs(list) do
            if not header or header.zone ~= q.zone then
                header = { header = true, zone = q.zone, count = 0 }
                rows[#rows + 1] = header
            end
            header.count = header.count + 1
            if not db.collapsed[q.zone] then rows[#rows + 1] = q end
        end
    else
        rows = list
    end
    if not selected() then
        selectedID = nil
        for _, r in ipairs(rows) do if not r.header then selectedID = r.id; break end end
    end
end

EsteriaQuestLog_Test.scan = scan
EsteriaQuestLog_Test.rebuild = rebuild
EsteriaQuestLog_Test.state = function()
    return { quests = quests, rows = rows, zones = zones, statusCounts = statusCounts, typeCounts = typeCounts,
             selectedID = selectedID }
end
EsteriaQuestLog_Test.setFilters = function(s, t, z) statusFilter, typeFilter, zoneFilter = s or "all", t, z end

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

local function railRow(parent, onClick)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(RAIL_W - 12, 26)
    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
    row.label = ES.Text(row, 12, ES.TEXT); row.label:SetPoint("LEFT", 14, 0); row.label:SetPoint("RIGHT", -40, 0)
    row.label:SetJustifyH("LEFT")
    row.count = ES.Text(row, 11, ES.MUTED); row.count:SetPoint("RIGHT", -10, 0)
    row:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); onClick(self) end)
    row:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.06) end end)
    row:SetScript("OnLeave", function(self) if not self.selected then self.wash:SetAlpha(0) end end)
    row.Paint = function(self, label, on, count)
        self.selected = on
        self.label:SetText(label)
        self.wash:SetAlpha(on and 0.12 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        self.count:SetText(count or "")
    end
    return row
end

local function tabButton(parent, width, text)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, 24)
    b.wash = ES.Solid(b, "BACKGROUND", ES.GOLD, 0); b.wash:SetAllPoints()
    b.accent = ES.Solid(b, "ARTWORK", ES.GOLD, 0)
    b.accent:SetPoint("BOTTOMLEFT", 4, 0); b.accent:SetPoint("BOTTOMRIGHT", -4, 0); b.accent:SetHeight(2)
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

local function difficulty(level)
    local c = GetQuestDifficultyColor and GetQuestDifficultyColor(level)
    return c and { c.r, c.g, c.b } or ES.TEXT
end

local function tagText(q)
    local parts = {}
    if q.daily then parts[#parts + 1] = DAILY or "Daily" end
    if q.tag and q.tag ~= "" then parts[#parts + 1] = q.tag end
    if (q.group or 0) > 1 then parts[#parts + 1] = "Group " .. q.group end
    return table.concat(parts, " · ")
end

-- Actions ----------------------------------------------------------------------------------------------------------
local refresh

local function toggleTrack(q)
    if IsQuestWatched(q.index) then
        RemoveQuestWatch(q.index)
    elseif GetNumQuestWatches() >= (MAX_WATCHABLE_QUESTS or 25) then
        UIErrorsFrame:AddMessage(format(QUEST_WATCH_TOO_MANY or "You can only track %d quests.", MAX_WATCHABLE_QUESTS or 25),
            1, 0.1, 0.1)
        return
    else
        AddQuestWatch(q.index)
    end
    if WatchFrame_Update then WatchFrame_Update() end
    dirty = true
end

local function selectQuest(q)
    selectedID = q and q.id
    if q then SelectQuestLogEntry(q.index) end
    refresh()
end

local function abandon(q)
    SelectQuestLogEntry(q.index)
    SetAbandonQuest()
    local items = GetAbandonQuestItems and GetAbandonQuestItems()
    if items then StaticPopup_Show("ABANDON_QUEST_WITH_ITEMS", GetAbandonQuestName(), items)
    else StaticPopup_Show("ABANDON_QUEST", GetAbandonQuestName()) end
end

local function share(q)
    SelectQuestLogEntry(q.index)
    QuestLogPushQuest()
end

local function showMap(q)
    if WorldMap_OpenToQuest then WorldMap_OpenToQuest(q.id) else ToggleFrame(WorldMapFrame) end
end

local function questTooltip(owner, q)
    GameTooltip:SetOwner(owner, "ANCHOR_RIGHT")
    local c = difficulty(q.level)
    GameTooltip:SetText(q.title, c[1], c[2], c[3])
    GameTooltip:AddLine(("Level %d  ·  %s"):format(q.level, q.zone), 0.6, 0.59, 0.55)
    for _, o in ipairs(q.objs) do
        if o.finished then GameTooltip:AddLine(o.text, 0.55, 0.85, 0.5) else GameTooltip:AddLine(o.text, 0.88, 0.86, 0.8) end
    end
    GameTooltip:AddLine("Shift-click to link or track. Right-click to track.", 0.54, 0.43, 0.18)
    GameTooltip:Show()
end

-- Quest list -------------------------------------------------------------------------------------------------------
local function makeListRow(parent)
    local r = CreateFrame("Button", nil, parent)
    r:SetSize(LIST_W - 22, ROW_H - 2)
    r:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
    r.stripe = ES.Solid(r, "ARTWORK", ES.GOLD, 1)
    r.stripe:SetPoint("TOPLEFT", 0, -5); r.stripe:SetPoint("BOTTOMLEFT", 0, 5); r.stripe:SetWidth(3)
    r.level = ES.Text(r, 13, ES.TEXT); r.level:SetPoint("LEFT", 8, 0); r.level:SetWidth(28)
    r.title = ES.Text(r, 12, ES.TEXT); r.title:SetPoint("TOPLEFT", 42, -5); r.title:SetPoint("RIGHT", -74, 0)
    r.title:SetJustifyH("LEFT")
    r.sub = ES.Text(r, 10, ES.MUTED); r.sub:SetPoint("BOTTOMLEFT", 42, 5); r.sub:SetPoint("RIGHT", -74, 0)
    r.sub:SetJustifyH("LEFT")
    r.barBg = ES.Solid(r, "ARTWORK", { 0, 0, 0 }, 0.6); r.barBg:SetSize(44, 4); r.barBg:SetPoint("BOTTOMRIGHT", -26, 8)
    r.bar = ES.Solid(r, "OVERLAY", ES.GOLD, 1); r.bar:SetPoint("LEFT", r.barBg, "LEFT"); r.bar:SetHeight(4)
    r.pct = ES.Text(r, 10, ES.MUTED); r.pct:SetPoint("BOTTOMRIGHT", r.barBg, "TOPRIGHT", 0, 3)
    r.state = r:CreateTexture(nil, "OVERLAY"); r.state:SetSize(16, 16); r.state:SetPoint("RIGHT", r.barBg, "LEFT", -6, 4)
    -- Tracking pin: an outlined box, filled while the quest is on the tracker.
    r.pin = CreateFrame("Button", nil, r); r.pin:SetSize(14, 14); r.pin:SetPoint("RIGHT", -6, 0)
    ES.Solid(r.pin, "BACKGROUND", ES.INK, 1):SetAllPoints()
    r.pin.lines = ES.Border(r.pin, ES.GOLD_DIM)
    r.pin.fill = ES.Solid(r.pin, "ARTWORK", ES.GOLD, 1); r.pin.fill:SetPoint("TOPLEFT", 3, -3); r.pin.fill:SetPoint("BOTTOMRIGHT", -3, 3)
    r.pin:SetScript("OnClick", function(self) if r.quest then PlaySound("igMainMenuOptionCheckBoxOn"); toggleTrack(r.quest) end end)
    r.pin:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(r.quest and r.quest.watched and "Stop tracking" or "Track on screen", 1, 0.82, 0.1)
        GameTooltip:Show()
    end)
    r.pin:SetScript("OnLeave", function() GameTooltip:Hide() end)
    -- Zone header parts.
    r.toggle = ES.Text(r, 14, ES.GOLD); r.toggle:SetPoint("LEFT", 8, 0)
    r.count = ES.Text(r, 11, ES.MUTED); r.count:SetPoint("RIGHT", -8, 0)
    r.rule = ES.Solid(r, "ARTWORK", ES.GOLD_DIM, 0.5); r.rule:SetPoint("BOTTOMLEFT", 6, 2); r.rule:SetPoint("BOTTOMRIGHT", -6, 2)
    r.rule:SetHeight(1)

    r:SetScript("OnClick", function(self, mouse)
        local e = self.entry
        if not e then return end
        if e.header then
            db.collapsed[e.zone] = not db.collapsed[e.zone] or nil
            PlaySound("igMainMenuOptionCheckBoxOn")
            dirty = true
            return
        end
        if mouse == "RightButton" then toggleTrack(e); return end
        if IsModifiedClick("CHATLINK") then
            local link = GetQuestLink(e.index)
            if link and ChatEdit_GetActiveWindow and ChatEdit_GetActiveWindow() then ChatEdit_InsertLink(link)
            else toggleTrack(e) end
            return
        end
        PlaySound("igMainMenuOptionCheckBoxOn")
        selectQuest(e)
    end)
    r:SetScript("OnEnter", function(self)
        if self.entry and not self.entry.header then
            if self.entry.id ~= selectedID then self.wash:SetAlpha(0.06) end
            questTooltip(self, self.entry)
        end
    end)
    r:SetScript("OnLeave", function(self)
        if self.entry and self.entry.id ~= selectedID then self.wash:SetAlpha(0) end
        GameTooltip:Hide()
    end)
    return r
end

local function fillListRow(r, e)
    r.entry = e
    local isHeader = e.header
    for _, region in ipairs({ r.stripe, r.level, r.sub, r.barBg, r.bar, r.pct, r.state, r.pin }) do shown(region, not isHeader) end
    shown(r.toggle, isHeader); shown(r.count, isHeader); shown(r.rule, isHeader)
    if isHeader then
        r.quest = nil
        r.wash:SetAlpha(0)
        r.title:ClearAllPoints(); r.title:SetPoint("LEFT", 26, 0); r.title:SetPoint("RIGHT", -40, 0)
        r.title:SetText((e.zone ~= "" and e.zone or "Other"):upper()); r.title:SetTextColor(unpack(ES.GOLD))
        r.toggle:SetText(db.collapsed[e.zone] and "+" or "-")
        r.count:SetText(e.count)
        return
    end
    r.quest = e
    local c = difficulty(e.level)
    r.title:ClearAllPoints(); r.title:SetPoint("TOPLEFT", 42, -5); r.title:SetPoint("RIGHT", -74, 0)
    r.title:SetText(e.title)
    r.title:SetTextColor(unpack(e.id == selectedID and ES.GOLD or ES.TEXT))
    r.stripe:SetVertexColor(c[1], c[2], c[3], 1)
    r.level:SetText(e.level); r.level:SetTextColor(c[1], c[2], c[3])
    local sub = tagText(e)
    if db.sort ~= "zone" then sub = sub ~= "" and (e.zone .. " · " .. sub) or e.zone end
    r.sub:SetText(sub)
    r.wash:SetAlpha(e.id == selectedID and 0.12 or 0)
    r.bar:SetWidth(math.max(1, 44 * e.progress))
    local col = e.complete == 1 and ES.GOOD or e.complete == -1 and ES.BAD or ES.GOLD
    r.bar:SetVertexColor(col[1], col[2], col[3], 1)
    r.pct:SetText(e.complete == -1 and "Failed" or e.complete == 1 and "Done" or ("%d%%"):format(e.progress * 100))
    if e.complete == 1 then r.state:SetTexture("Interface\\RAIDFRAME\\ReadyCheck-Ready"); r.state:Show()
    elseif e.complete == -1 then r.state:SetTexture("Interface\\RAIDFRAME\\ReadyCheck-NotReady"); r.state:Show()
    else r.state:Hide() end
    shown(r.pin.fill, e.watched)
    ES.Tint(r.pin.lines, e.watched and ES.GOLD or ES.GOLD_DIM, 1)
end

local function paintRows()
    for i, r in ipairs(listRows) do
        local e = rows[i + offset]
        if e then fillListRow(r, e); r:Show() else r.entry = nil; r:Hide() end
    end
    shown(pane.empty, #rows == 0)
end

-- Detail pane ------------------------------------------------------------------------------------------------------
local pools = { text = {}, obj = {}, reward = {} }
local used = { text = 0, obj = 0, reward = 0 }

local function take(kind, make)
    used[kind] = used[kind] + 1
    local r = pools[kind][used[kind]]
    if not r then r = make(); pools[kind][used[kind]] = r end
    r:Show()
    return r
end

local function makeObjective()
    local o = CreateFrame("Frame", nil, detail.child)
    o:SetSize(detail.width - 32, 30)
    o.text = ES.Text(o, 12, ES.TEXT); o.text:SetPoint("TOPLEFT", 18, 0); o.text:SetPoint("RIGHT", 0, 0)
    o.text:SetJustifyH("LEFT")
    o.check = o:CreateTexture(nil, "ARTWORK"); o.check:SetSize(14, 14); o.check:SetPoint("TOPLEFT", 0, 1)
    o.barBg = ES.Solid(o, "ARTWORK", { 0, 0, 0 }, 0.6); o.barBg:SetPoint("BOTTOMLEFT", 18, 2); o.barBg:SetPoint("BOTTOMRIGHT")
    o.barBg:SetHeight(5)
    o.bar = ES.Solid(o, "OVERLAY", ES.GOLD, 1); o.bar:SetPoint("LEFT", o.barBg, "LEFT"); o.bar:SetHeight(5)
    return o
end

local function makeReward()
    local b = CreateFrame("Button", nil, detail.child)
    b:SetSize((detail.width - 40) / 2, 40)
    b.icon = b:CreateTexture(nil, "ARTWORK"); b.icon:SetSize(36, 36); b.icon:SetPoint("LEFT", 1, 0)
    b.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    local holder = CreateFrame("Frame", nil, b); holder:SetSize(38, 38); holder:SetPoint("LEFT")
    b.lines = ES.Border(holder, ES.GOLD_DIM)
    b.count = ES.Text(holder, 11, ES.TEXT); b.count:SetPoint("BOTTOMRIGHT", -2, 2)
    b.name = ES.Text(b, 11, ES.TEXT); b.name:SetPoint("LEFT", 44, 0); b.name:SetPoint("RIGHT", -2, 0)
    b.name:SetJustifyH("LEFT")
    b:SetScript("OnEnter", function(self)
        if not self.kind then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if self.kind == "spell" then GameTooltip:SetQuestLogRewardSpell() else GameTooltip:SetQuestLogItem(self.kind, self.index) end
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    b:SetScript("OnClick", function(self)
        if self.kind and self.kind ~= "spell" and IsModifiedClick() then
            HandleModifiedItemClick(GetQuestLogItemLink(self.kind, self.index))
        end
    end)
    return b
end

local function paintDetail()
    for kind, pool in pairs(pools) do for _, r in ipairs(pool) do r:Hide() end used[kind] = 0 end
    local q = selected()
    shown(detail.none, not q)
    for _, b in ipairs(detail.buttons) do b:Active(q ~= nil) end
    if not q then detail.title:SetText(""); detail.meta:SetText(""); detail.badge:Hide(); return end
    SelectQuestLogEntry(q.index)

    local c = difficulty(q.level)
    detail.title:SetText(q.title)
    detail.stripe:SetVertexColor(c[1], c[2], c[3], 1)
    local meta = { ("Level %d"):format(q.level) }
    if tagText(q) ~= "" then meta[#meta + 1] = tagText(q) end
    if q.zone ~= "" then meta[#meta + 1] = q.zone end
    if q.id > 0 then meta[#meta + 1] = "#" .. q.id end
    detail.meta:SetText(table.concat(meta, "  ·  "))
    local badge = q.complete == 1 and { "READY TO TURN IN", ES.GOOD } or q.complete == -1 and { "FAILED", ES.BAD } or
        { ("%d%% COMPLETE"):format(q.progress * 100), ES.GOLD }
    detail.badge.text:SetText(badge[1]); detail.badge.text:SetTextColor(unpack(badge[2]))
    detail.badge.bg:SetVertexColor(badge[2][1], badge[2][2], badge[2][3], 0.16); ES.Tint(detail.badge.lines, badge[2], 0.8)
    detail.badge:SetWidth(detail.badge.text:GetStringWidth() + 20); detail.badge:Show()

    local child, width = detail.child, detail.width - 32
    local y = 0
    local function text(str, size, color)
        local fs = take("text", function() local t = ES.Text(child, 12, ES.TEXT); t:SetJustifyH("LEFT"); return t end)
        fs:SetFont(ES.FONT, size); fs:SetTextColor(unpack(color)); fs:SetWidth(width); fs:SetText(str)
        fs:ClearAllPoints(); fs:SetPoint("TOPLEFT", 16, -y)
        y = y + fs:GetStringHeight() + 6
        return fs
    end
    local function section(name) y = y + 8; text(name:upper(), 10, ES.GOLD_DIM); y = y + 2 end

    local left = GetQuestLogTimeLeft()
    if left and left > 0 then text("Time remaining: " .. SecondsToTime(left), 12, ES.BAD) end

    local description, objectives = GetQuestLogQuestText()
    section("Objectives")
    if #q.objs == 0 then text(objectives or "", 12, ES.TEXT) end
    for _, o in ipairs(q.objs) do
        local row = take("obj", makeObjective)
        row:ClearAllPoints(); row:SetPoint("TOPLEFT", 16, -y)
        row.text:SetText(o.text); row.text:SetTextColor(unpack(o.finished and ES.GOOD or ES.TEXT))
        row.check:SetTexture(o.finished and "Interface\\RAIDFRAME\\ReadyCheck-Ready" or "Interface\\RAIDFRAME\\ReadyCheck-Waiting")
        local p = objectiveProgress(o.text, o.finished)
        local numeric = o.text:match("%d+%s*/%s*%d+") ~= nil
        shown(row.barBg, numeric); shown(row.bar, numeric)
        row.bar:SetWidth(math.max(1, (width - 18) * p))
        row.bar:SetVertexColor(unpack(o.finished and ES.GOOD or ES.GOLD))
        local h = row.text:GetStringHeight() + (numeric and 12 or 4)
        row:SetHeight(h); y = y + h + 6
    end
    local money = GetQuestLogRequiredMoney()
    if money and money > 0 then text("Requires " .. GetCoinTextureString(money), 12, ES.TEXT) end
    if (GetQuestLogGroupNum() or 0) > 0 then text(("Suggested players: %d"):format(GetQuestLogGroupNum()), 12, ES.MUTED) end

    if objectives and objectives ~= "" and #q.objs > 0 then section("Summary"); text(objectives, 12, ES.TEXT) end
    if description and description ~= "" then section("Story"); text(description, 12, ES.MUTED) end

    local col = 0
    local function reward(kind, index, name, texture, count, quality)
        local b = take("reward", makeReward)
        b.kind, b.index = kind, index
        b.icon:SetTexture(texture); b.count:SetText((count or 0) > 1 and count or "")
        local qc = quality and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
        b.name:SetText(name or ""); b.name:SetTextColor(qc and qc.r or ES.TEXT[1], qc and qc.g or ES.TEXT[2], qc and qc.b or ES.TEXT[3])
        b:ClearAllPoints(); b:SetPoint("TOPLEFT", 16 + col * ((width - 8) / 2 + 8), -y)
        col = col + 1
        if col == 2 then col = 0; y = y + 46 end
    end
    local function endGrid() if col > 0 then col = 0; y = y + 46 end end

    local choices, items = GetNumQuestLogChoices() or 0, GetNumQuestLogRewards() or 0
    local rMoney, xp = GetQuestLogRewardMoney() or 0, GetQuestLogRewardXP and GetQuestLogRewardXP() or 0
    local spellTex, spellName = GetQuestLogRewardSpell()
    local honor = GetQuestLogRewardHonor and GetQuestLogRewardHonor() or 0
    local talents = GetQuestLogRewardTalents and GetQuestLogRewardTalents() or 0
    local rewardTitle = GetQuestLogRewardTitle and GetQuestLogRewardTitle()
    if choices + items > 0 or rMoney > 0 or xp > 0 or spellTex or honor > 0 or talents > 0 or rewardTitle then
        section("Rewards")
        if choices > 0 then
            text(choices > 1 and "Choose one of:" or "You will receive:", 11, ES.MUTED)
            for i = 1, choices do
                local name, texture, count, quality = GetQuestLogChoiceInfo(i)
                reward("choice", i, name, texture, count, quality)
            end
            endGrid()
        end
        if items > 0 then
            text(choices > 0 and "You will also receive:" or "You will receive:", 11, ES.MUTED)
            for i = 1, items do
                local name, texture, count, quality = GetQuestLogRewardInfo(i)
                reward("reward", i, name, texture, count, quality)
            end
            endGrid()
        end
        if spellTex then reward("spell", 0, spellName, spellTex, 0); endGrid() end
        local extras = {}
        if rMoney > 0 then extras[#extras + 1] = GetCoinTextureString(rMoney) end
        if xp > 0 then extras[#extras + 1] = ("%d experience"):format(xp) end
        if honor > 0 then extras[#extras + 1] = ("%d honor"):format(honor) end
        if talents > 0 then extras[#extras + 1] = ("%d talent point%s"):format(talents, talents > 1 and "s" or "") end
        if rewardTitle then extras[#extras + 1] = "Title: " .. rewardTitle end
        if #extras > 0 then text(table.concat(extras, "     "), 12, ES.GOLD) end
    end

    child:SetHeight(y + 16)
    detail.scroll:UpdateScrollChildRect()
    local maxScroll = math.max(0, y + 16 - detail.scroll:GetHeight())
    if detail.scroll:GetVerticalScroll() > maxScroll then detail.scroll:SetVerticalScroll(maxScroll) end

    detail.track.label:SetText(q.watched and "Untrack" or "Track")
    local inGroup = (GetNumPartyMembers() or 0) > 0 or (GetNumRaidMembers() or 0) > 0
    detail.share:Active(inGroup and GetQuestLogPushable() and true or false)
end

-- Rail -------------------------------------------------------------------------------------------------------------
local function paintRail()
    for i, s in ipairs(STATUS) do statusRows[i]:Paint(s[2], statusFilter == s[1], statusCounts[s[1]] or 0) end
    for i, t in ipairs(TYPES) do typeRows[i]:Paint(t[2], typeFilter == t[1], typeCounts[t[1]] or 0) end
    local entries = { { name = nil, label = "All zones", count = #quests } }
    for _, z in ipairs(zones) do entries[#entries + 1] = { name = z.name, label = z.name ~= "" and z.name or "Other", count = z.count } end
    zoneOffset = math.max(0, math.min(zoneOffset, #entries - ZONE_ROWS))
    for i, row in ipairs(zoneRows) do
        local e = entries[i + zoneOffset]
        row.zone = e and e.name
        if e then row:Paint(e.label, zoneFilter == e.name, e.count); row:Show() else row:Hide() end
    end
    pane.zoneMore:SetText(#entries > ZONE_ROWS and ("%d-%d of %d  (scroll)"):format(zoneOffset + 1,
        math.min(#entries, zoneOffset + ZONE_ROWS), #entries) or "")
end

local function paintHeader()
    local cap = capacity()
    local daily = GetDailyQuestsCompleted and GetDailyQuestsCompleted() or 0
    local maxDaily = GetMaxDailyQuests and GetMaxDailyQuests() or 25
    counter:SetText(("|cffe8c46b%d|r / %d quests     Daily |cffe8c46b%d|r / %d     Tracked |cffe8c46b%d|r / %d"):format(
        #quests, cap, daily, maxDaily, GetNumQuestWatches() or 0, MAX_WATCHABLE_QUESTS or 25))
    meter.fill:SetWidth(math.max(1, meter:GetWidth() * math.min(1, #quests / cap)))
end

refresh = function()
    if not frame or not frame:IsShown() then return end
    rebuild()
    local maxOffset = math.max(0, #rows - ROWS)
    offset = math.min(offset, maxOffset)
    slider:SetMinMaxValues(0, maxOffset)
    shown(slider, maxOffset > 0)
    slider:SetValue(offset)
    paintRows()
    paintRail()
    paintHeader()
    paintDetail()
    for _, t in ipairs(sortTabs) do t:SetSelected(t.key == db.sort) end
end

local function scrollToSelected()
    for i, r in ipairs(rows) do
        if not r.header and r.id == selectedID then
            if i <= offset or i > offset + ROWS then offset = math.max(0, i - math.floor(ROWS / 2)) end
            return
        end
    end
end

-- Build ------------------------------------------------------------------------------------------------------------
local function build()
    EsteriaQuestLogDB = EsteriaQuestLogDB or {}
    db = EsteriaQuestLogDB
    db.sort = db.sort or "zone"
    db.collapsed = db.collapsed or {}

    frame = CreateFrame("Frame", "EsteriaQuestLogFrame", UIParent)
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving); frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames, "EsteriaQuestLogFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(frame, ES.GOLD)
    local band = frame:CreateTexture(nil, "BORDER"); band:SetTexture(ES.WHITE)
    band:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.28)
    band:SetPoint("TOPLEFT", 1, -1); band:SetPoint("TOPRIGHT", -1, -1); band:SetHeight(42)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12); title:SetText("Quest Log")
    counter = ES.Text(frame, 11, ES.TEXT); counter:SetPoint("TOPRIGHT", -40, -12)
    meter = CreateFrame("Frame", nil, frame); meter:SetSize(220, 3); meter:SetPoint("TOPRIGHT", counter, "BOTTOMRIGHT", 0, -5)
    ES.Solid(meter, "BACKGROUND", { 0, 0, 0 }, 0.6):SetAllPoints()
    meter.fill = ES.Solid(meter, "ARTWORK", ES.GOLD, 0.9); meter.fill:SetPoint("TOPLEFT"); meter.fill:SetPoint("BOTTOMLEFT")
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)

    pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")

    -- Rail: status, type and zone filters with live counts.
    local rail = panel(pane); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    local y = -12
    heading(rail, "Status"):SetPoint("TOPLEFT", 14, y); y = y - 16
    for i, s in ipairs(STATUS) do
        statusRows[i] = railRow(rail, function() statusFilter = s[1]; offset = 0; refresh() end)
        statusRows[i]:SetPoint("TOPLEFT", 6, y); y = y - 27
    end
    y = y - 10
    heading(rail, "Type"):SetPoint("TOPLEFT", 14, y); y = y - 16
    for i, t in ipairs(TYPES) do
        typeRows[i] = railRow(rail, function() typeFilter = typeFilter ~= t[1] and t[1] or nil; offset = 0; refresh() end)
        typeRows[i]:SetPoint("TOPLEFT", 6, y); y = y - 27
    end
    y = y - 10
    heading(rail, "Zones"):SetPoint("TOPLEFT", 14, y); y = y - 16
    local zoneBox = CreateFrame("Frame", nil, rail); zoneBox:SetPoint("TOPLEFT", 0, y); zoneBox:SetSize(RAIL_W, ZONE_ROWS * 27)
    zoneBox:EnableMouseWheel(true)
    zoneBox:SetScript("OnMouseWheel", function(_, delta) zoneOffset = math.max(0, zoneOffset - delta * 2); paintRail() end)
    for i = 1, ZONE_ROWS do
        zoneRows[i] = railRow(zoneBox, function(self) zoneFilter = self.zone; offset = 0; refresh() end)
        zoneRows[i]:SetPoint("TOPLEFT", 6, -(i - 1) * 27)
    end
    pane.zoneMore = ES.Text(rail, 10, ES.MUTED); pane.zoneMore:SetPoint("BOTTOMLEFT", 14, 10)

    -- Middle: search, sort and the quest list.
    local middle = panel(pane); middle:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); middle:SetPoint("BOTTOM", 0, 14)
    middle:SetWidth(LIST_W)
    search = CreateFrame("EditBox", "EsteriaQuestLogSearch", middle)
    search:SetSize(LIST_W - 24, 26); search:SetPoint("TOPLEFT", 12, -12); search:SetAutoFocus(false)
    search:SetFontObject(ChatFontNormal); search:SetTextInsets(10, 8, 0, 0)
    ES.Solid(search, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    search.lines = ES.Border(search, ES.GOLD_DIM)
    local placeholder = ES.Text(search, 11, ES.MUTED); placeholder:SetPoint("LEFT", 10, 0)
    placeholder:SetText("Search titles, objectives, zones or quest ID")
    search:SetScript("OnEscapePressed", function(self) if self:GetText() ~= "" then self:SetText("") else self:ClearFocus() end end)
    search:SetScript("OnEnterPressed", search.ClearFocus)
    search:SetScript("OnEditFocusGained", function(self) ES.Tint(self.lines, ES.GOLD, 1) end)
    search:SetScript("OnEditFocusLost", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 1) end)
    search:SetScript("OnTextChanged", function(self) shown(placeholder, self:GetText() == ""); offset = 0; refresh() end)

    local sortLabel = heading(middle, "Sort"); sortLabel:SetPoint("TOPLEFT", 14, -50)
    for i, s in ipairs(SORTS) do
        local t = tabButton(middle, 66, s[2]); t.key = s[1]
        t:SetPoint("TOPLEFT", 50 + (i - 1) * 70, -44)
        t:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); db.sort = s[1]; offset = 0; refresh() end)
        sortTabs[i] = t
    end

    local list = CreateFrame("Frame", nil, middle)
    list:SetPoint("TOPLEFT", 8, -76); list:SetSize(LIST_W - 16, ROWS * ROW_H)
    for i = 1, ROWS do
        local r = makeListRow(list); r:SetPoint("TOPLEFT", 0, -(i - 1) * ROW_H)
        listRows[i] = r
    end
    pane.empty = ES.Text(list, 12, ES.MUTED); pane.empty:SetPoint("TOP", 0, -60); pane.empty:SetWidth(LIST_W - 60)
    pane.empty:SetText("No quests match these filters.")
    slider = CreateFrame("Slider", nil, middle); slider:SetOrientation("VERTICAL"); slider:SetWidth(5)
    slider:SetPoint("TOPRIGHT", -6, -78); slider:SetPoint("BOTTOMRIGHT", -6, 12)
    ES.Solid(slider, "BACKGROUND", { 0, 0, 0 }, 0.5):SetAllPoints()
    local thumb = slider:CreateTexture(nil, "OVERLAY"); thumb:SetTexture(ES.WHITE); thumb:SetSize(5, 40)
    thumb:SetVertexColor(unpack(ES.GOLD_DIM)); slider:SetThumbTexture(thumb)
    slider:SetValueStep(1); slider:SetMinMaxValues(0, 0); slider:SetValue(0)
    slider:SetScript("OnValueChanged", function(_, v) offset = math.floor(v + 0.5); paintRows() end)
    list:EnableMouseWheel(true)
    list:SetScript("OnMouseWheel", function(_, delta)
        local _, maxOffset = slider:GetMinMaxValues()
        slider:SetValue(math.max(0, math.min(maxOffset, offset - delta * 3)))
    end)

    -- Right: the selected quest.
    detail = panel(pane); detail:SetPoint("TOPLEFT", middle, "TOPRIGHT", 14, 0); detail:SetPoint("BOTTOMRIGHT", -14, 14)
    detail.width = W - (14 + RAIL_W + 14 + LIST_W + 14) - 14
    detail.tint = detail:CreateTexture(nil, "BORDER"); detail.tint:SetTexture(ES.WHITE)
    detail.tint:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    detail.tint:SetPoint("TOPLEFT", 1, -1); detail.tint:SetPoint("TOPRIGHT", -1, -1); detail.tint:SetHeight(90)
    detail.stripe = ES.Solid(detail, "ARTWORK", ES.GOLD, 1)
    detail.stripe:SetPoint("TOPLEFT", 1, -14); detail.stripe:SetSize(3, 40)
    detail.title = ES.Text(detail, 17, ES.GOLD); detail.title:SetPoint("TOPLEFT", 16, -14); detail.title:SetWidth(detail.width - 32)
    detail.title:SetJustifyH("LEFT")
    detail.meta = ES.Text(detail, 11, ES.MUTED); detail.meta:SetPoint("TOPLEFT", detail.title, "BOTTOMLEFT", 0, -5)
    detail.meta:SetWidth(detail.width - 32); detail.meta:SetJustifyH("LEFT")
    detail.badge = CreateFrame("Frame", nil, detail); detail.badge:SetHeight(20)
    detail.badge:SetPoint("TOPLEFT", detail.meta, "BOTTOMLEFT", 0, -8)
    detail.badge.bg = ES.Solid(detail.badge, "BACKGROUND", ES.GOLD, 0.16); detail.badge.bg:SetAllPoints()
    detail.badge.lines = ES.Border(detail.badge, ES.GOLD, 0.8)
    detail.badge.text = ES.Text(detail.badge, 10, ES.GOLD); detail.badge.text:SetPoint("CENTER")
    detail.none = ES.Text(detail, 12, ES.MUTED); detail.none:SetPoint("CENTER"); detail.none:SetText("Select a quest.")

    detail.scroll = CreateFrame("ScrollFrame", nil, detail)
    detail.scroll:SetPoint("TOPLEFT", 0, -100); detail.scroll:SetPoint("BOTTOMRIGHT", 0, 52)
    detail.child = CreateFrame("Frame", nil, detail.scroll); detail.child:SetSize(detail.width, 10)
    detail.scroll:SetScrollChild(detail.child)
    detail.scroll:EnableMouseWheel(true)
    detail.scroll:SetScript("OnMouseWheel", function(self, delta)
        local range = math.max(0, detail.child:GetHeight() - self:GetHeight())
        self:SetVerticalScroll(math.max(0, math.min(range, self:GetVerticalScroll() - delta * 40)))
    end)
    local rule = ES.Solid(detail, "ARTWORK", ES.GOLD_DIM, 0.4)
    rule:SetPoint("BOTTOMLEFT", 12, 50); rule:SetPoint("BOTTOMRIGHT", -12, 50); rule:SetHeight(1)

    local bw = math.floor((detail.width - 24 - 18) / 4)
    detail.track = button(detail, bw, "Track", true)
    detail.share = button(detail, bw, "Share")
    detail.map = button(detail, bw, "Map")
    detail.abandon = button(detail, bw, "Abandon")
    detail.buttons = { detail.track, detail.share, detail.map, detail.abandon }
    for i, b in ipairs(detail.buttons) do b:SetPoint("BOTTOMLEFT", 12 + (i - 1) * (bw + 6), 12) end
    detail.abandon.label:SetTextColor(unpack(ES.BAD))
    detail.track:SetScript("OnClick", function() local q = selected(); if q then toggleTrack(q) end end)
    detail.share:SetScript("OnClick", function() local q = selected(); if q then share(q) end end)
    detail.map:SetScript("OnClick", function() local q = selected(); if q then showMap(q) end end)
    detail.abandon:SetScript("OnClick", function() local q = selected(); if q then abandon(q) end end)

    local clock = 0
    frame:SetScript("OnUpdate", function(_, elapsed)
        if dirty then dirty = false; scan(); refresh() end
        clock = clock + elapsed
        if clock >= 1 then
            clock = 0
            local q = selected()
            if q and (GetQuestLogTimeLeft() or 0) > 0 then paintDetail() end
        end
    end)
    frame:SetScript("OnShow", function() PlaySound("igQuestLogOpen"); scan(); scrollToSelected(); refresh() end)
    frame:SetScript("OnHide", function() PlaySound("igQuestLogClose"); GameTooltip:Hide() end)
    for _, ev in ipairs({ "QUEST_LOG_UPDATE", "QUEST_WATCH_UPDATE", "UNIT_QUEST_LOG_CHANGED", "PARTY_MEMBERS_CHANGED",
                          "PLAYER_LEVEL_UP" }) do
        frame:RegisterEvent(ev)
    end
    frame:SetScript("OnEvent", function() dirty = true end)
end

local function open(index)
    if not frame then build() end
    if index and index > 0 then
        local _, _, _, _, _, _, _, _, id = GetQuestLogTitle(index)
        selectedID = id
        statusFilter, typeFilter, zoneFilter = "all", nil, nil
        if search then search:SetText("") end
    end
    if frame:IsShown() then scan(); scrollToSelected(); refresh() else frame:Show() end
end

function EsteriaQuestLog_Toggle()
    if frame and frame:IsShown() then frame:Hide() else open() end
end

-- Replace the stock quest log everywhere it opens: binding and micro button show QuestLogFrame, the tracker and
-- quest popups go through QuestLog_OpenToQuest, and some paths show the detail popout directly.
QuestLog_OpenToQuest = function(index) open(index) end
if QuestLogFrame then
    QuestLogFrame:HookScript("OnShow", function(self) HideUIPanel(self); EsteriaQuestLog_Toggle() end)
end
if QuestLogDetailFrame then
    QuestLogDetailFrame:HookScript("OnShow", function(self) HideUIPanel(self); open(GetQuestLogSelection()) end)
end

SLASH_ESTERIAQUESTLOG1 = "/questlog"
SLASH_ESTERIAQUESTLOG2 = "/ql"
SlashCmdList.ESTERIAQUESTLOG = function() EsteriaQuestLog_Toggle() end
