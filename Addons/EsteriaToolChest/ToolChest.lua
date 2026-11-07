-- GM Tool Chest: the client half of EsteriaWoW/modules/mod-esteria-toolchest. Drawn with the Character Advancement
-- style kit: a rail of tools on the left, the tool's board to the right. The panel opens only after the server has
-- answered "hello", which it does for GM accounts alone; the server re-checks every request.
local ES = EsteriaStyle
local PREFIX = "ESTC"
local W, H, HEAD, RAIL, FOOT, ROW_H = 1000, 680, 72, 196, 34, 24

local TC = { gm = false, tool = nil }
local pages, railRows = {}, {}

local function Request(...)
    SendAddonMessage(PREFIX, table.concat({ ... }, "\t"), "WHISPER", UnitName("player"))
end

local frame = CreateFrame("Frame", "EsteriaToolChestFrame", UIParent)
frame:SetSize(W, H)
frame:SetPoint("CENTER", 0, 20)
frame:SetFrameStrata("HIGH")
frame:SetToplevel(true)
frame:SetClampedToScreen(true)
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:Hide()
tinsert(UISpecialFrames, "EsteriaToolChestFrame")
ES.Solid(frame, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
ES.Border(frame, ES.GOLD_DIM, 0.9)

local title = ES.Text(frame, 22, ES.GOLD)
title:SetPoint("TOPLEFT", 24, -18)
title:SetText("GM Tool Chest")
local subtitle = ES.Text(frame, 11, ES.MUTED)
subtitle:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -4)

local status = ES.Text(frame, 11, ES.MUTED)
status:SetPoint("BOTTOMLEFT", RAIL + 18, 11)
status:SetPoint("BOTTOMRIGHT", -18, 11)
status:SetJustifyH("LEFT")
local function Status(text, color)
    color = color or ES.MUTED
    status:SetTextColor(color[1], color[2], color[3])
    status:SetText(text)
end

-- Widgets ---------------------------------------------------------------------------------------------------------
local function Button(parent, label, width, onClick)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, ROW_H)
    ES.Solid(b, "BACKGROUND", ES.GOLD_DIM, 0.15):SetAllPoints()
    b.edges = ES.Border(b, ES.GOLD_DIM, 0.9)
    local hover = ES.Solid(b, "HIGHLIGHT", ES.GOLD, 0.12)
    hover:SetAllPoints()
    b.label = ES.Text(b, 11, ES.GOLD)
    b.label:SetPoint("CENTER")
    b.label:SetText(label)
    b:SetScript("OnClick", onClick)
    return b
end

local function Input(parent, width, onEnter, onChange)
    local e = CreateFrame("EditBox", nil, parent)
    e:SetSize(width, ROW_H)
    e:SetAutoFocus(false)
    e:SetFont(ES.FONT, 12)
    e:SetTextColor(ES.TEXT[1], ES.TEXT[2], ES.TEXT[3])
    e:SetTextInsets(6, 6, 0, 0)
    ES.Solid(e, "BACKGROUND", { 0, 0, 0 }, 0.5):SetAllPoints()
    ES.Border(e, ES.GOLD_DIM, 0.8)
    e:SetScript("OnEscapePressed", e.ClearFocus)
    e:SetScript("OnEnterPressed", function(self) self:ClearFocus(); if onEnter then onEnter(self) end end)
    if onChange then e:SetScript("OnTextChanged", onChange) end
    return e
end

-- A virtual list: only the visible rows exist, the mouse wheel moves the window over `items`.
local function List(parent, width, count, build, render)
    local list = { items = {}, offset = 0, rows = {} }
    local host = CreateFrame("Frame", nil, parent)
    host:SetSize(width, count * (ROW_H + 2))
    host:EnableMouseWheel(true)
    list.host = host
    for i = 1, count do
        local row = build(host, width)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * (ROW_H + 2))
        list.rows[i] = row
    end
    function list:Refresh()
        self.offset = math.max(0, math.min(self.offset, #self.items - count))
        for i, row in ipairs(self.rows) do
            local item = self.items[i + self.offset]
            row.item = item
            if item then render(row, item); row:Show() else row:Hide() end
        end
    end
    function list:Set(items)
        self.items, self.offset = items, 0
        self:Refresh()
    end
    host:SetScript("OnMouseWheel", function(_, delta)
        list.offset = list.offset - delta * 3
        list:Refresh()
    end)
    return list
end

-- A clickable row with left text and muted right text. `isSelected(item)` draws the gold selection.
local function PickList(parent, width, count, text, isSelected, onPick)
    local list
    list = List(parent, width, count, function(host, w)
        local r = CreateFrame("Button", nil, host)
        r:SetSize(w, ROW_H)
        ES.Solid(r, "BACKGROUND", ES.GOLD_DIM, 0.08):SetAllPoints()
        ES.Solid(r, "HIGHLIGHT", ES.GOLD, 0.12):SetAllPoints()
        r.mark = ES.Solid(r, "BORDER", ES.GOLD, 0.25)
        r.mark:SetAllPoints()
        r.left = ES.Text(r, 11, ES.TEXT)
        r.left:SetPoint("LEFT", 8, 0)
        r.left:SetPoint("RIGHT", -90, 0)
        r.left:SetJustifyH("LEFT")
        r.right = ES.Text(r, 10, ES.MUTED)
        r.right:SetPoint("RIGHT", -8, 0)
        r:SetScript("OnClick", function(self)
            onPick(self.item)
            list:Refresh()
        end)
        return r
    end, function(r, item)
        local left, right = text(item)
        r.left:SetText(left)
        r.right:SetText(right or "")
        if isSelected(item) then r.mark:Show() else r.mark:Hide() end
    end)
    return list
end

local function Heading(parent, label, x, y)
    local fs = ES.Text(parent, 13, ES.GOLD)
    fs:SetPoint("TOPLEFT", x, y)
    fs:SetText(label)
    return fs
end

-- Runs a GM chat command; the server's own permission check and its reply apply. Always dot-prefixed, so a typo can't
-- be spoken aloud as /say.
local function Run(command)
    command = command:gsub("^%s*%.*", "")
    if command == "" then return end
    SendChatMessage("." .. command, "SAY")
    Status("." .. command)
end

local function AddPage(id, label, hint)
    local page = CreateFrame("Frame", nil, frame)
    page:SetPoint("TOPLEFT", RAIL + 18, -HEAD - 12)
    page:SetPoint("BOTTOMRIGHT", -18, FOOT)
    page:Hide()
    page.id, page.label, page.hint = id, label, hint
    pages[#pages + 1] = page
    return page
end

local function Show(id)
    TC.tool = id
    for i, page in ipairs(pages) do
        local on = page.id == id
        if on then
            page:Show()
            subtitle:SetText(page.label .. ": " .. page.hint)
            if page.OnOpen then page:OnOpen() end
        else
            page:Hide()
        end
        local r = railRows[i]
        if on then r.bar:Show() else r.bar:Hide() end
        local c = on and ES.GOLD or ES.TEXT
        r.text:SetTextColor(c[1], c[2], c[3])
    end
end

-- Asks for a fresh nearby list a moment after a command, once the server has done it.
local refresher = CreateFrame("Frame")
refresher:Hide()
refresher:SetScript("OnUpdate", function(self, elapsed)
    self.t = self.t - elapsed
    if self.t <= 0 then
        self:Hide()
        Request(self.op)
    end
end)
local function RefreshSoon(op)
    refresher.t, refresher.op = 0.4, op
    refresher:Show()
end

-- Rows the server is streaming, by tag; "<tag>." closes the batch.
local inbox = {}
local handlers = {}

-- Home and toolbar -------------------------------------------------------------------------------------------------
local ICONS = {
    npcplace = "Interface\\Icons\\INV_Misc_Horn_01",
    objplace = "Interface\\Icons\\INV_Crate_01",
    npcedit = "Interface\\Icons\\INV_Misc_Note_02",
    objedit = "Interface\\Icons\\INV_Misc_Wrench_01",
    commands = "Interface\\Icons\\INV_Misc_Gem_Pearl_04",
}

local OpenTool
local toolbar = CreateFrame("Frame", "EsteriaToolChestToolbar", UIParent)
toolbar:SetSize(40, 40)
toolbar:SetPoint("TOP", 0, -8)
toolbar:SetMovable(true)
toolbar:EnableMouse(true)
toolbar:RegisterForDrag("LeftButton")
toolbar:SetScript("OnDragStart", toolbar.StartMoving)
toolbar:SetScript("OnDragStop", toolbar.StopMovingOrSizing)
toolbar:SetClampedToScreen(true)
toolbar:Hide()
toolbar.buttons = {}

local function Pins()
    EsteriaToolChestDB.pins = EsteriaToolChestDB.pins or {}
    return EsteriaToolChestDB.pins
end

local function RefreshToolbar()
    for _, b in ipairs(toolbar.buttons) do b:Hide() end
    local n = 0
    for _, page in ipairs(pages) do
        if Pins()[page.id] then
            n = n + 1
            local b = toolbar.buttons[n]
            if not b then
                b = CreateFrame("Button", nil, toolbar)
                b:SetSize(36, 36)
                b:SetPoint("LEFT", 2 + (n - 1) * 38, 0)
                b.icon = b:CreateTexture(nil, "ARTWORK")
                b.icon:SetAllPoints()
                ES.Border(b, ES.GOLD_DIM, 0.9)
                ES.Solid(b, "HIGHLIGHT", ES.GOLD, 0.2):SetAllPoints()
                b:RegisterForDrag("LeftButton")
                b:SetScript("OnDragStart", function() toolbar:StartMoving() end)
                b:SetScript("OnDragStop", function() toolbar:StopMovingOrSizing() end)
                b:SetScript("OnClick", function(self) OpenTool(self.id) end)
                b:SetScript("OnEnter", function(self)
                    GameTooltip:SetOwner(self, "ANCHOR_BOTTOM")
                    GameTooltip:SetText(self.label)
                    GameTooltip:Show()
                end)
                b:SetScript("OnLeave", function() GameTooltip:Hide() end)
                toolbar.buttons[n] = b
            end
            b.id, b.label = page.id, page.label
            b.icon:SetTexture(ICONS[page.id])
            b:Show()
        end
    end
    toolbar:SetWidth(math.max(n * 38 + 2, 40))
    if TC.gm and n > 0 then toolbar:Show() else toolbar:Hide() end
end

do
    local home = AddPage("home", "Home", "every tool; click to open, the check pins it to the toolbar")
    home.tiles = {}
    home.OnOpen = function()
        local i = 0
        for _, page in ipairs(pages) do
            if page.id ~= "home" then
                i = i + 1
                local t = home.tiles[i]
                if not t then
                    t = CreateFrame("Button", nil, home)
                    t:SetSize(375, 60)
                    t:SetPoint("TOPLEFT", ((i - 1) % 2) * 387, -math.floor((i - 1) / 2) * 70)
                    t:RegisterForClicks("LeftButtonUp", "RightButtonUp")
                    ES.Solid(t, "BACKGROUND", ES.GOLD_DIM, 0.08):SetAllPoints()
                    ES.Solid(t, "HIGHLIGHT", ES.GOLD, 0.1):SetAllPoints()
                    ES.Border(t, ES.GOLD_DIM, 0.8)
                    t.icon = t:CreateTexture(nil, "ARTWORK")
                    t.icon:SetSize(44, 44)
                    t.icon:SetPoint("LEFT", 8, 0)
                    t.name = ES.Text(t, 14, ES.GOLD)
                    t.name:SetPoint("TOPLEFT", 62, -12)
                    t.hint = ES.Text(t, 10, ES.MUTED)
                    t.hint:SetPoint("TOPLEFT", 62, -34)
                    t.hint:SetPoint("RIGHT", -44, 0)
                    t.hint:SetJustifyH("LEFT")
                    t.pin = CreateFrame("CheckButton", nil, t, "UICheckButtonTemplate")
                    t.pin:SetSize(26, 26)
                    t.pin:SetPoint("RIGHT", -8, 0)
                    t.pin:SetScript("OnClick", function(self)
                        Pins()[t.id] = self:GetChecked() and true or nil
                        RefreshToolbar()
                    end)
                    t:SetScript("OnClick", function(self, mouse)
                        if mouse == "RightButton" then
                            Pins()[self.id] = not Pins()[self.id] or nil
                            self.pin:SetChecked(Pins()[self.id])
                            RefreshToolbar()
                        else
                            Show(self.id)
                        end
                    end)
                    home.tiles[i] = t
                end
                t.id = page.id
                t.icon:SetTexture(ICONS[page.id])
                t.name:SetText(page.label)
                t.hint:SetText(page.hint)
                t.pin:SetChecked(Pins()[page.id])
            end
        end
    end
end

-- NPC and object placement -----------------------------------------------------------------------------------------
local OpenEditor -- set by the editors below
local function PlacementPage(id, label, hint, searchOp, searchTag, nearOp, nearTag, searchText, actions)
    local page = AddPage(id, label, hint)
    local pick, spawn = nil, nil
    local LEFT_W, RIGHT_W = 372, 372

    Heading(page, "Find", 0, 0)
    local results
    local box = Input(page, LEFT_W - 70, function(self)
        local term = self:GetText():gsub("%s+$", "")
        if term ~= "" then Request(searchOp, term) end
    end)
    box:SetPoint("TOPLEFT", 0, -22)
    Button(page, "Search", 64, function() box:GetScript("OnEnterPressed")(box) end):SetPoint("LEFT", box, "RIGHT", 6, 0)
    results = PickList(page, LEFT_W, 20, searchText, function(item) return item == pick end, function(item) pick = item end)
    results.host:SetPoint("TOPLEFT", 0, -52)
    actions.place(page, function() return pick end, results.host)

    Heading(page, "Spawned nearby", LEFT_W + 22, 0)
    local near = PickList(page, RIGHT_W, 12, function(item)
        return item.name, item.dist .. " yd  #" .. item.spawn
    end, function(item) return spawn and item.spawn == spawn.spawn end, function(item)
        spawn = item
        if actions.selected then actions.selected(item) end
    end)
    near.host:SetPoint("TOPLEFT", LEFT_W + 22, -52)
    Button(page, "Refresh", 64, function() Request(nearOp) end):SetPoint("TOPLEFT", LEFT_W + 22 + RIGHT_W - 64, -22)
    actions.manage(page, function() return spawn end, near.host, function() RefreshSoon(nearOp) end)

    handlers[searchTag] = function(f)
        inbox[searchTag] = inbox[searchTag] or {}
        tinsert(inbox[searchTag], f)
    end
    handlers[searchTag .. "."] = function()
        local found = inbox[searchTag] or {}
        inbox[searchTag], pick = nil, nil
        results:Set(found)
        Status(#found >= 60 and "First 60 matches; search more narrowly." or (#found .. " found."))
    end
    handlers[nearTag] = function(f)
        inbox[nearTag] = inbox[nearTag] or {}
        tinsert(inbox[nearTag], { spawn = f[1], entry = f[2], x = tonumber(f[3]), y = tonumber(f[4]), z = tonumber(f[5]),
            o = tonumber(f[6]), dist = f[7], name = f[8] })
    end
    handlers[nearTag .. "."] = function()
        near.items, near.offset = inbox[nearTag] or {}, near.offset
        inbox[nearTag] = nil
        if spawn then
            local still
            for _, item in ipairs(near.items) do
                if item.spawn == spawn.spawn then still = item end
            end
            spawn = still
            if actions.selected then actions.selected(still) end
        end
        near:Refresh()
    end
    page.OnOpen = function() Request(nearOp) end
    return page
end

local function ButtonRow(page, anchor, y, specs)
    local x = 0
    for _, s in ipairs(specs) do
        local b = Button(page, s[1], s[2], s[3])
        b:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", x, y)
        x = x + s[2] + 6
    end
end

-- Asks twice for anything that removes a live spawn from the world database.
StaticPopupDialogs["ESTERIA_TOOLCHEST_DELETE"] = {
    text = "Permanently delete this spawn from the world?\n%s",
    button1 = YES, button2 = NO, timeout = 0, whileDead = 1, hideOnEscape = 1, preferredIndex = 3,
    OnAccept = function(_, data) Run(data.command); data.done() end,
}

PlacementPage("npcplace", "NPC Placement", "find NPCs and spawn them at your feet", "npcs", "n", "nearnpcs", "N",
    function(i) return i[4] .. (i[5] ~= "" and i[5] and (" <" .. i[5] .. ">") or ""), "#" .. i[1] .. "  L" .. i[2] end, {
    place = function(page, getPick, under)
        local function with(fmt)
            return function()
                local p = getPick()
                if p then Run(fmt:format(p[1])); RefreshSoon("nearnpcs") else Status("Pick an NPC first.", ES.BAD) end
            end
        end
        ButtonRow(page, under, -8, {
            { "Spawn here", 110, with("npc add %s") },
            { "Temporary spawn", 130, with("npc add temp %s") },
            { "Edit template", 110, function()
                local p = getPick()
                if p then OpenEditor("npcedit", p[1]) else Status("Pick an NPC first.", ES.BAD) end
            end },
        })
    end,
    manage = function(page, getSpawn, under, refresh)
        local function with(fmt)
            return function()
                local s = getSpawn()
                if s then Run(fmt:format(s.spawn)); refresh() else Status("Pick a spawn first.", ES.BAD) end
            end
        end
        ButtonRow(page, under, -8, {
            { "Go to", 70, with("go creature %s") },
            { "Move to me", 100, with("npc move %s") },
            { "Edit fields", 90, function()
                local s = getSpawn()
                if s then OpenEditor("npcedit", s.entry, s.spawn) else Status("Pick a spawn first.", ES.BAD) end
            end },
            { "Delete", 70, function()
                local s = getSpawn()
                if not s then Status("Pick a spawn first.", ES.BAD); return end
                local dialog = StaticPopup_Show("ESTERIA_TOOLCHEST_DELETE", s.name .. " #" .. s.spawn)
                if dialog then dialog.data = { command = "npc delete " .. s.spawn, done = refresh } end
            end },
        })
    end,
})

local objectStep
local function ObjectMoveControls(page, getSpawn, under, refresh)
    local anchor = under
    local function nudge(dx, dy, dz)
        return function()
            local s = getSpawn()
            if not s then Status("Pick an object first.", ES.BAD); return end
            local step = tonumber(objectStep:GetText()) or 1
            s.x, s.y, s.z = s.x + dx * step, s.y + dy * step, s.z + dz * step
            Run(("gobject move %s %.3f %.3f %.3f"):format(s.spawn, s.x, s.y, s.z))
            refresh()
        end
    end
    local function turn(sign)
        return function()
            local s = getSpawn()
            if not s then Status("Pick an object first.", ES.BAD); return end
            s.o = (s.o + sign * math.rad(tonumber(objectStep:GetText()) or 15)) % (2 * math.pi)
            Run(("gobject turn %s %.4f"):format(s.spawn, s.o))
            refresh()
        end
    end
    local function with(fmt)
        return function()
            local s = getSpawn()
            if s then Run(fmt:format(s.spawn)); refresh() else Status("Pick an object first.", ES.BAD) end
        end
    end
    ButtonRow(page, anchor, -8, {
        { "X+", 44, nudge(1, 0, 0) }, { "X-", 44, nudge(-1, 0, 0) },
        { "Y+", 44, nudge(0, 1, 0) }, { "Y-", 44, nudge(0, -1, 0) },
        { "Z+", 44, nudge(0, 0, 1) }, { "Z-", 44, nudge(0, 0, -1) },
        { "Edit fields", 72, function()
            local s = getSpawn()
            if s then OpenEditor("objedit", s.entry, s.spawn) else Status("Pick an object first.", ES.BAD) end
        end },
    })
    ButtonRow(page, anchor, -8 - ROW_H - 6, {
        { "Turn left", 80, turn(1) }, { "Turn right", 80, turn(-1) },
        { "To me", 60, with("gobject move %s") }, { "Go to", 54, with("go gameobject %s") },
        { "Delete", 60, function()
            local s = getSpawn()
            if not s then Status("Pick an object first.", ES.BAD); return end
            local dialog = StaticPopup_Show("ESTERIA_TOOLCHEST_DELETE", s.name .. " #" .. s.spawn)
            if dialog then dialog.data = { command = "gobject delete " .. s.spawn, done = refresh } end
        end },
    })
    local label = ES.Text(page, 11, ES.MUTED)
    label:SetPoint("TOPLEFT", anchor, "BOTTOMLEFT", 0, -8 - 2 * (ROW_H + 6) - 6)
    label:SetText("Step (yards, or degrees for turns)")
    objectStep = Input(page, 60)
    objectStep:SetPoint("LEFT", label, "RIGHT", 8, 0)
    objectStep:SetText("1")
end

PlacementPage("objplace", "Objects", "place, move, rotate and remove game objects", "objects", "o", "nearobjects", "O",
    function(i) return i[4], "#" .. i[1] .. "  type " .. i[2] end, {
    place = function(page, getPick, under)
        ButtonRow(page, under, -8, {
            { "Place here", 110, function()
                local p = getPick()
                if p then Run("gobject add " .. p[1]); RefreshSoon("nearobjects") else Status("Pick an object first.", ES.BAD) end
            end },
            { "Edit template", 110, function()
                local p = getPick()
                if p then OpenEditor("objedit", p[1]) else Status("Pick an object first.", ES.BAD) end
            end },
        })
    end,
    manage = ObjectMoveControls,
})

-- Template editors --------------------------------------------------------------------------------------------------
-- Every column of the template, its model/equipment/spell/resistance rows and optionally one spawn. Enter saves a field,
-- and the server validates it. Undo is the old value the server reports, sent back through the same checks.
local Meta = EsteriaToolChestMeta
local SCHOOLS = { "Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane" }
local GROUPS = {
    creature_template_model = "Model", creature_equip_template = "Equipment", creature_template_spell = "Spell",
    creature_template_resistance = "Resistance", gameobject_template_addon = "Addon", creature = "This spawn", gameobject = "This spawn",
}
local STATIC = {
    dmgschool = { kind = "enum", { 0, "Physical" }, { 1, "Holy" }, { 2, "Fire" }, { 3, "Nature" }, { 4, "Frost" }, { 5, "Shadow" }, { 6, "Arcane" } },
    unit_class = { kind = "enum", { 1, "Warrior" }, { 2, "Paladin" }, { 4, "Rogue" }, { 8, "Mage" } },
}
local PICKERS = { faction = "faction", Spell = "spell", ItemID1 = "item", ItemID2 = "item", ItemID3 = "item" }

-- Popup: flags as checks, enums as a list, searches through the server for spells, items and factions.
local popup = CreateFrame("Frame", nil, frame)
popup:SetSize(330, 470)
popup:SetFrameStrata("DIALOG")
popup:EnableMouse(true)
popup:Hide()
ES.Solid(popup, "BACKGROUND", ES.INK, 1):SetAllPoints()
ES.Border(popup, ES.GOLD, 0.9)
local popupTitle = ES.Text(popup, 13, ES.GOLD)
popupTitle:SetPoint("TOPLEFT", 12, -10)
local popupValue, popupMode, popupCommit, popupKind = 0, nil, nil, nil
local function HasBit(value, b) return math.floor(value / b) % 2 == 1 end
local popupList = PickList(popup, 306, 14, function(row)
    return row[2], tostring(row[1])
end, function(row)
    if popupMode == "flags" then return HasBit(popupValue, row[1]) end
    return popupMode == "enum" and row[1] == popupValue
end, function(row)
    if popupMode == "flags" then
        popupValue = popupValue + (HasBit(popupValue, row[1]) and -row[1] or row[1])
    else
        popup:Hide()
        popupCommit(tostring(row[1]))
    end
end)
popupList.host:SetPoint("TOPLEFT", 12, -66)
local popupSearch = Input(popup, 306, function(self)
    if self:GetText() ~= "" then Request("pick", popupKind, self:GetText()) end
end)
popupSearch:SetPoint("TOPLEFT", 12, -34)
local popupApply = Button(popup, "Apply", 80, function()
    popup:Hide()
    popupCommit(string.format("%.0f", popupValue))
end)
popupApply:SetPoint("BOTTOMLEFT", 12, 8)
Button(popup, "Close", 80, function() popup:Hide() end):SetPoint("BOTTOMRIGHT", -12, 8)

local function OpenPopup(anchor, title, meta, kind, value, commit)
    popupTitle:SetText(title)
    popupCommit, popupKind = commit, kind
    popupValue = tonumber(value) or 0
    popup:ClearAllPoints()
    popup:SetPoint("CENTER", frame, "CENTER", 0, 0)
    if kind then
        popupMode = "pick"
        popupSearch:Show(); popupApply:Hide()
        popupSearch:SetText("")
        popupList:Set({})
        popup:Show()
        popupSearch:SetFocus()
    else
        popupMode = meta.kind
        popupSearch:Hide()
        if meta.kind == "flags" then popupApply:Show() else popupApply:Hide() end
        local rows = {}
        for _, row in ipairs(meta) do rows[#rows + 1] = row end
        popupList:Set(rows)
        popup:Show()
    end
end
handlers["p"] = function(f)
    inbox.p = inbox.p or {}
    tinsert(inbox.p, { tonumber(f[1]), f[2] })
end
handlers["p."] = function()
    popupList:Set(inbox.p or {})
    inbox.p = nil
end

local editor -- the page whose request is outstanding
local function EditorPage(id, label, hint, readOp, writeOp, cloneOp, baseTable)
    local page = AddPage(id, label, hint)
    local entry, spawn, all = nil, nil, {}
    local list, box, model
    local undo, sent = {}, nil
    local facing = 0

    local function Split(name)
        return name:match("^([%w_]+)%.(%d+)%.([%w_]+)$")
    end

    local function Label(field)
        local tableName, key, column = Split(field.name)
        if tableName then
            if tableName == "creature_template_resistance" then return "Resistance: " .. (SCHOOLS[tonumber(key)] or key) end
            if tableName == "creature_template_spell" then return "Spell " .. (tonumber(key) + 1) end
            if tableName == "creature_template_model" then return "Model " .. (tonumber(key) + 1) .. ": " .. column end
            if tableName == "creature_equip_template" then return "Equipment " .. key .. ": " .. column end
            return GROUPS[tableName] .. ": " .. column
        end
        local n = field.name:match("^Data(%d+)$")
        if n then
            for _, f in ipairs(all) do
                if f.name == "type" then
                    local names = Meta.data[tonumber(f.value)]
                    local name = names and names[tonumber(n)]
                    return name and (field.name .. " (" .. name .. ")") or field.name
                end
            end
        end
        return field.name
    end

    local function MetaFor(field)
        local tableName, _, column = Split(field.name)
        column = column or field.name
        local qualified = (tableName or baseTable) .. "." .. column
        return Meta.fields[qualified] or Meta.fields[column] or STATIC[column], PICKERS[column]
    end

    local function ApplyFilter()
        local text = page.filter:GetText():lower()
        local items = {}
        for _, field in ipairs(all) do
            if text == "" or Label(field):lower():find(text, 1, true) then items[#items + 1] = field end
        end
        list.items = items
        list:Refresh()
    end

    local function Commit(fieldName, value, isUndo)
        value = value:gsub("[\t\n|]", " ")
        editor = page
        sent = { name = fieldName, isUndo = isUndo, entry = entry, spawn = spawn }
        local tableName = Split(fieldName)
        if tableName then
            Request("setrow", (tableName == "creature" or tableName == "gameobject") and spawn or entry, fieldName, value)
        else
            Request(writeOp, entry, fieldName, value)
        end
    end

    local function Load(n, spawnId)
        editor, all, spawn = page, {}, spawnId
        if spawnId then Request(readOp, n, spawnId) else Request(readOp, n) end
    end

    Heading(page, "Entry", 0, 0)
    box = Input(page, 100, function(self)
        local n = tonumber(self:GetText())
        if n then Load(n) end
    end)
    box:SetPoint("TOPLEFT", 0, -22)
    local load = Button(page, "Load", 54, function() box:GetScript("OnEnterPressed")(box) end)
    load:SetPoint("LEFT", box, "RIGHT", 6, 0)
    local clone = Button(page, "Clone as new", 96, function()
        if entry then editor = page; Request(cloneOp, entry) else Status("Load an entry first.", ES.BAD) end
    end)
    clone:SetPoint("LEFT", load, "RIGHT", 6, 0)
    local undoButton = Button(page, "Undo", 54, function()
        local last = tremove(undo)
        if not last then Status("Nothing to undo."); return end
        if last.entry ~= entry or last.spawn ~= spawn then
            box:SetText(tostring(last.entry))
            Load(last.entry, last.spawn)
        end
        entry, spawn = last.entry, last.spawn
        Commit(last.name, last.old, true)
    end)
    undoButton:SetPoint("LEFT", clone, "RIGHT", 6, 0)
    page.filter = Input(page, 150, nil, function() if list then ApplyFilter() end end)
    page.filter:SetPoint("TOPRIGHT", -216, -22)
    local filterLabel = ES.Text(page, 11, ES.MUTED)
    filterLabel:SetPoint("BOTTOMLEFT", page.filter, "TOPLEFT", 0, 4)
    filterLabel:SetText("Filter fields")

    model = CreateFrame("PlayerModel", nil, page)
    model:SetSize(200, 300)
    model:SetPoint("TOPRIGHT", 0, -52)
    ES.Solid(model, "BACKGROUND", { 0, 0, 0 }, 0.4):SetAllPoints()
    ES.Border(model, ES.GOLD_DIM, 0.8)
    model:EnableMouseWheel(true)
    model:SetScript("OnMouseWheel", function(self, delta)
        facing = facing + delta * 0.4
        self:SetFacing(facing)
    end)
    local modelHint = ES.Text(page, 10, ES.MUTED)
    modelHint:SetPoint("TOP", model, "BOTTOM", 0, -6)
    modelHint:SetText("Mouse wheel turns the model")

    local width = W - RAIL - 36 - 216
    list = List(page, width, 20, function(host, w)
        local r = CreateFrame("Frame", nil, host)
        r:SetSize(w, ROW_H)
        r.name = ES.Text(r, 11, ES.TEXT)
        r.name:SetPoint("LEFT", 6, 0)
        r.name:SetPoint("RIGHT", r, "RIGHT", -190, 0)
        r.name:SetJustifyH("LEFT")
        r.input = Input(r, 150, function(self)
            local field = r.item
            if field and entry and self:GetText() ~= field.value then Commit(field.name, self:GetText()) end
        end)
        r.input:SetPoint("RIGHT", -34, 0)
        r.input:EnableMouseWheel(true)
        r.input:SetScript("OnMouseWheel", function(_, delta) host:GetScript("OnMouseWheel")(host, delta) end)
        r.more = Button(r, "...", 28, function()
            local field = r.item
            if not field then return end
            local meta, kind = MetaFor(field)
            OpenPopup(r, Label(field), meta, kind, field.value, function(value) Commit(field.name, value) end)
        end)
        r.more:SetPoint("RIGHT", 0, 0)
        return r
    end, function(r, field)
        r.name:SetText(Label(field))
        r.input:SetText(field.value)
        r.input:ClearFocus()
        local meta, kind = MetaFor(field)
        if meta or kind then r.more:Show() else r.more:Hide() end
    end)
    list.host:SetPoint("TOPLEFT", 0, -56)

    local function ShowModel()
        if readOp == "npc" and entry then
            model:SetCreature(entry)
            model:SetFacing(facing)
        end
    end

    page.Open = function(n, spawnId)
        box:SetText(tostring(n))
        Load(n, spawnId)
    end
    page.Preview = function(path)
        if path:lower():find("%.wmo$") then
            model:Hide(); modelHint:SetText("World objects have no preview")
        else
            model:Show(); modelHint:SetText("Mouse wheel turns the model")
            model:SetModel((path:gsub("%.[mM][dD][xXlL]$", ".m2")))
            model:SetFacing(facing)
        end
    end
    page.AddField = function(name, value) all[#all + 1] = { name = name, value = value } end
    page.Loaded = function(n)
        entry = n
        list.offset = 0
        ApplyFilter()
        ShowModel()
        Status("Entry " .. n .. (spawn and (", spawn " .. spawn) or "") .. ": " .. #all .. " fields. Enter saves a field; clear a Model, Equipment, Spell or Resistance field to remove its row.")
    end
    page.Saved = function(name, value, old)
        for _, field in ipairs(all) do
            if field.name == name then field.value = value end
        end
        if sent and sent.name == name and not sent.isUndo then
            tinsert(undo, { name = name, old = old or "", entry = sent.entry, spawn = sent.spawn })
            if #undo > 50 then tremove(undo, 1) end
        end
        sent = nil
        ApplyFilter()
        if name:find("Model", 1, true) or name:find("creature_template_model", 1, true) then ShowModel() end
        Status("Saved " .. Label({ name = name }) .. " = " .. value .. ". Respawn existing copies to see it.", ES.GOOD)
    end
    page.Failed = function()
        sent = nil
        list:Refresh() -- back to the stored value
    end
    return page
end

local npcEditor = EditorPage("npcedit", "NPC Editor", "template, models, equipment, spells, resistances, spawn", "npc", "setnpc", "clonenpc", "creature_template")
local objEditor = EditorPage("objedit", "Object Editor", "template, addon fields and spawn", "object", "setobject", "cloneobject", "gameobject_template")
OpenEditor = function(id, entry, spawn)
    Show(id)
    (id == "npcedit" and npcEditor or objEditor).Open(tonumber(entry), spawn and tonumber(spawn))
end
handlers["f"] = function(f) if editor then editor.AddField(f[1], f[2] or "") end end
handlers["f."] = function(f) if editor then editor.Loaded(tonumber(f[1])) end end
handlers["f!"] = function(f) Status(f[1], ES.BAD) end
handlers["pv"] = function(f) if editor then editor.Preview(f[1]) end end
handlers["cl"] = function(f)
    Status("Cloned as new entry " .. f[1] .. ".", ES.GOOD)
    if editor then editor.Open(tonumber(f[1])) end
end
handlers["w"] = function(f) if editor then editor.Saved(f[1], f[2] or "", f[3]) end end
handlers["w!"] = function(f)
    Status(f[1] .. ": " .. f[2], ES.BAD)
    if editor then editor.Failed() end
end

-- Commands ----------------------------------------------------------------------------------------------------------
do
    local page = AddPage("commands", "Commands", "search, browse, favorite and run every GM command")
    local all, selected, onlyFavorites = nil, nil, false
    local list, search, argBox, help

    local function Favorites()
        EsteriaToolChestDB.favorites = EsteriaToolChestDB.favorites or {}
        return EsteriaToolChestDB.favorites
    end

    local function Apply()
        if not all then return end
        local term, items, favorites = search:GetText():lower(), {}, Favorites()
        for _, c in ipairs(all) do
            if (not onlyFavorites or favorites[c.name])
                and (term == "" or c.name:lower():find(term, 1, true) or c.help:lower():find(term, 1, true)) then
                items[#items + 1] = c
            end
        end
        -- favorites float to the top of whatever matches
        table.sort(items, function(a, b)
            local fa, fb = favorites[a.name] and 1 or 0, favorites[b.name] and 1 or 0
            if fa ~= fb then return fa > fb end
            return a.name < b.name
        end)
        list:Set(items)
    end

    search = Input(page, 372, nil, function() Apply() end)
    search:SetPoint("TOPLEFT", 0, -22)
    Heading(page, "Search commands", 0, 0)
    local favButton
    favButton = Button(page, "All", 100, function()
        onlyFavorites = not onlyFavorites
        favButton.label:SetText(onlyFavorites and "Favorites" or "All")
        Apply()
    end)
    favButton:SetPoint("LEFT", search, "RIGHT", 6, 0)

    list = PickList(page, 478, 22, function(c)
        return (Favorites()[c.name] and "|cffe8c46b*|r " or "") .. c.name
    end, function(c) return c == selected end, function(c)
        selected = c
        argBox:SetText("." .. c.name .. " ")
        help:SetText(c.help ~= "" and c.help or "No help text.")
        argBox:SetFocus()
    end)
    list.host:SetPoint("TOPLEFT", 0, -52)

    local side = 478 + 18
    local sideW = W - RAIL - 36 - side
    Heading(page, "Run", side, 0)
    argBox = Input(page, sideW, function(self) Run(self:GetText()) end)
    argBox:SetPoint("TOPLEFT", side, -22)
    Button(page, "Run", 80, function() Run(argBox:GetText()) end):SetPoint("TOPLEFT", side, -52)
    Button(page, "Favorite", 90, function()
        if not selected then Status("Pick a command first.", ES.BAD); return end
        local favorites = Favorites()
        favorites[selected.name] = not favorites[selected.name] or nil
        Apply()
    end):SetPoint("TOPLEFT", side + 86, -52)
    help = ES.Text(page, 11, ES.TEXT)
    help:SetPoint("TOPLEFT", side, -88)
    help:SetWidth(sideW)
    help:SetJustifyH("LEFT")
    help:SetJustifyV("TOP")

    handlers["c"] = function(f)
        inbox.c = inbox.c or {}
        tinsert(inbox.c, { name = f[1], help = f[2] or "" })
    end
    handlers["c."] = function()
        all, inbox.c = inbox.c or {}, nil
        Apply()
        Status(#all .. " commands available to this account.")
    end
    page.OnOpen = function()
        if not all then Request("commands") else Apply() end
    end
end

-- Rail --------------------------------------------------------------------------------------------------------------
do
    local line = ES.Solid(frame, "BORDER", ES.GOLD_DIM, 0.6)
    line:ClearAllPoints()
    line:SetPoint("TOPLEFT", RAIL, -HEAD)
    line:SetPoint("BOTTOMLEFT", RAIL, 0)
    line:SetWidth(1)
    for i, page in ipairs(pages) do
        local r = CreateFrame("Button", nil, frame)
        r:SetSize(RAIL - 1, 42)
        r:SetPoint("TOPLEFT", 0, -HEAD - (i - 1) * 42)
        ES.Solid(r, "HIGHLIGHT", ES.GOLD, 0.1):SetAllPoints()
        r.bar = ES.Solid(r, "ARTWORK", ES.GOLD, 1)
        r.bar:SetPoint("TOPLEFT")
        r.bar:SetPoint("BOTTOMLEFT")
        r.bar:SetWidth(3)
        r.text = ES.Text(r, 13, ES.TEXT)
        r.text:SetPoint("LEFT", 18, 0)
        r.text:SetText(page.label)
        r:SetScript("OnClick", function() Show(page.id) end)
        railRows[i] = r
    end
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", -4, -4)
end

-- Plumbing ----------------------------------------------------------------------------------------------------------
OpenTool = function(id)
    frame:Show()
    Show(id)
end

local function Toggle()
    if not TC.gm then
        Request("hello") -- answered only for GM accounts, and the answer opens the panel
        TC.wantOpen = true
        return
    end
    if frame:IsShown() then frame:Hide() else
        frame:Show()
        Show(TC.tool or pages[1].id)
    end
end
EsteriaToolChest_Toggle = Toggle
SLASH_ESTERIATOOLCHEST1 = "/tc"
SLASH_ESTERIATOOLCHEST2 = "/toolchest"
SlashCmdList.ESTERIATOOLCHEST = Toggle

local events = CreateFrame("Frame")
events:RegisterEvent("VARIABLES_LOADED")
events:RegisterEvent("PLAYER_ENTERING_WORLD")
events:RegisterEvent("CHAT_MSG_ADDON")
events:RegisterEvent("CHAT_MSG_SYSTEM")
events:SetScript("OnEvent", function(_, event, a1, a2, _, a4)
    if event == "VARIABLES_LOADED" then
        EsteriaToolChestDB = EsteriaToolChestDB or {}
    elseif event == "PLAYER_ENTERING_WORLD" then
        Request("hello")
    elseif event == "CHAT_MSG_SYSTEM" then
        if frame:IsShown() then Status(a1) end
    elseif a1 == PREFIX and a4 == UnitName("player") then
        local fields = { strsplit("\t", a2) }
        local op = tremove(fields, 1)
        if op == "hello" then
            TC.gm = true
            RefreshToolbar()
            if TC.wantOpen then
                TC.wantOpen = nil
                Toggle()
            end
        elseif handlers[op] then
            handlers[op](fields)
        end
    end
end)
