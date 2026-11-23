-- Esteria's PvP window: notoriety, the PvP toggle and rules, honor and arena, battleground queue, bounties,
-- the player's crime record and the crime leaderboard. Replaces the stock PvP frame and the portrait PvP menu.
local ES = EsteriaStyle
local PREFIX = "EsteriaCrime"
local W, H = 960, 640
local frame, tabs, panes = nil, {}, {}
local current = "Overview"
local status = {}
local board, log, leaders, claims = {}, {}, { {}, {}, {} }, {}
local pending = {}

local CLASSES = { "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK",
                  nil, "DRUID" }
local NOTORIETY = {
    Innocent = { color = { 0.55, 0.85, 0.5 }, icon = "Interface\\Icons\\Spell_Holy_SealOfSacrifice",
                 text = "No crimes on record. Killing you would be murder." },
    Bounty = { color = { 1, 0.78, 0.25 }, icon = "Interface\\Icons\\INV_Misc_Coin_02",
               text = "Someone has paid for your head. Hunters may attack you outside sanctuaries." },
    Thief = { color = { 0.68, 0.42, 1 }, icon = "Interface\\Icons\\INV_Misc_Bag_11",
              text = "You were seen stealing. Your PvP flag stays on until the exposure fades." },
    Wanted = { color = { 0.95, 0.3, 0.22 }, icon = "Interface\\Icons\\INV_Misc_Bone_Skull_01",
               text = "You committed murder. Killing you is lawful until your wanted status ends or you die." },
    Murderer = { color = { 0.85, 0.08, 0.08 }, icon = "Interface\\Icons\\Ability_Rogue_FeignDeath",
                 text = "A repeat killer. Automatic bounties grow with every innocent you slay." },
}
local BG_ART = {
    [1] = "Interface\\PVPFrame\\PvpBg-AlteracValley", [2] = "Interface\\PVPFrame\\PvpBg-WarsongGulch",
    [3] = "Interface\\PVPFrame\\PvpBg-ArathiBasin", [7] = "Interface\\PVPFrame\\PvpBg-EyeOfTheStorm",
    [9] = "Interface\\PVPFrame\\PvpBg-StrandOfTheAncients", [30] = "Interface\\PVPFrame\\PvpBg-IsleOfConquest",
    [32] = "Interface\\PVPFrame\\PvpRandomBg",
}

local function shown(region, on) if on then region:Show() else region:Hide() end end
local function send(body) SendAddonMessage(PREFIX, body, "WHISPER", UnitName("player")) end
local function money(copper) return GetCoinTextureString(math.floor(tonumber(copper) or 0)) end
local function since(seconds)
    seconds = tonumber(seconds) or 0
    if seconds <= 0 then return "" end
    return SecondsToTime(seconds, seconds > 3600)
end
local function classColor(id)
    local c = RAID_CLASS_COLORS[CLASSES[tonumber(id) or 0] or ""]
    return c and string.format("ff%02x%02x%02x", c.r * 255, c.g * 255, c.b * 255) or "ffbfbfbf"
end

-- Shared building blocks in the Collections look.
local function panel(parent, w, h)
    local p = CreateFrame("Frame", nil, parent)
    p:SetSize(w, h)
    ES.Solid(p, "BACKGROUND", { 0.06, 0.07, 0.10 }, 0.95):SetAllPoints()
    p.lines = ES.Border(p, ES.GOLD_DIM, 0.7)
    return p
end

local function heading(parent, text, size)
    local h = ES.Text(parent, size or 11, ES.GOLD_DIM)
    h:SetText(text:upper())
    return h
end

local function band(parent, height, r, g, b)
    local t = parent:CreateTexture(nil, "BORDER")
    t:SetTexture(ES.WHITE)
    t:SetGradientAlpha("VERTICAL", r, g, b, 0, r, g, b, 0.28)
    t:SetPoint("TOPLEFT", 1, -1); t:SetPoint("TOPRIGHT", -1, -1); t:SetHeight(height)
    return t
end

local function button(parent, width, text, primary, height)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height or 28)
    b.primary = primary
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, 0.95); b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0); b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, primary and 13 or 12, primary and { 1, 0.92, 0.7 } or ES.TEXT)
    b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12) end end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0) end)
    b.Active = function(self, on)
        if on then self:Enable() else self:Disable(); self.glow:SetAlpha(0) end
        self.label:SetAlpha(on and 1 or 0.35)
    end
    return b
end

local function tabButton(parent, width, text)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, 32)
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

local function icon(parent, size, path)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetSize(size + 2, size + 2)
    holder.icon = holder:CreateTexture(nil, "ARTWORK")
    holder.icon:SetPoint("TOPLEFT", 1, -1); holder.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    holder.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    if path then holder.icon:SetTexture(path) end
    holder.lines = ES.Border(holder, ES.GOLD_DIM)
    return holder
end

local function stat(parent, label, width)
    local s = CreateFrame("Frame", nil, parent)
    s:SetSize(width or 110, 40)
    s.value = ES.Text(s, 16, ES.TEXT); s.value:SetPoint("TOPLEFT", 0, 0)
    s.label = ES.Text(s, 10, ES.MUTED); s.label:SetPoint("TOPLEFT", s.value, "BOTTOMLEFT", 0, -3)
    s.label:SetText(label:upper())
    return s
end

local function editBox(parent, width, numeric)
    local e = CreateFrame("EditBox", nil, parent)
    e:SetSize(width, 28); e:SetAutoFocus(false); e:SetFontObject(ChatFontNormal); e:SetTextInsets(8, 8, 0, 0)
    e:SetMaxLetters(numeric and 7 or 12)
    if numeric then e:SetNumeric(true) end
    ES.Solid(e, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    e.lines = ES.Border(e, ES.GOLD_DIM)
    e:SetScript("OnEscapePressed", e.ClearFocus)
    e:SetScript("OnEditFocusGained", function(self) ES.Tint(self.lines, ES.GOLD, 1) end)
    e:SetScript("OnEditFocusLost", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 1) end)
    return e
end

-- A fixed set of rows scrolled with the mouse wheel; fill(row, data, index) paints one row.
local function list(parent, w, rowH, count, makeRow, fill)
    local l = CreateFrame("Frame", nil, parent)
    l:SetSize(w, rowH * count)
    l.rows, l.offset, l.data = {}, 0, {}
    for i = 1, count do
        local row = makeRow(l, w, rowH)
        row:SetPoint("TOPLEFT", 0, -(i - 1) * rowH)
        if i % 2 == 0 then ES.Solid(row, "BACKGROUND", ES.GOLD, 0.03):SetAllPoints() end
        l.rows[i] = row
    end
    l.empty = ES.Text(l, 12, ES.MUTED); l.empty:SetPoint("TOP", 0, -40); l.empty:SetWidth(w - 40)
    l.Refresh = function(self)
        local maxOffset = math.max(0, #self.data - count)
        self.offset = math.min(self.offset, maxOffset)
        for i, row in ipairs(self.rows) do
            local item = self.data[i + self.offset]
            if item then fill(row, item, i + self.offset); row:Show() else row:Hide() end
        end
        shown(self.empty, #self.data == 0)
    end
    l:EnableMouseWheel(true)
    l:SetScript("OnMouseWheel", function(self, delta)
        self.offset = math.max(0, self.offset - delta * 3); self:Refresh()
    end)
    return l
end

-- Notoriety -------------------------------------------------------------------------------------------------------
local function notoriety()
    if status.wanted == 1 and (status.notoriety or 0) >= 3 then return "Murderer" end
    if status.wanted == 1 then return "Wanted" end
    if status.thief == 1 then return "Thief" end
    if (status.bounty or 0) > 0 then return "Bounty" end
    return "Innocent"
end

local function pvpState()
    local flagged = UnitIsPVP("player") or UnitIsPVPFreeForAll("player")
    local desired = GetPVPDesired() == 1
    return flagged, desired
end

local function buildOverview(p)
    -- Left: the notoriety card.
    local card = panel(p, 420, 360); card:SetPoint("TOPLEFT", 20, -16)
    card.tint = band(card, 120, 1, 1, 1)
    heading(card, "Notoriety"):SetPoint("TOPLEFT", 18, -16)
    card.emblem = icon(card, 72); card.emblem:SetPoint("TOPLEFT", 18, -40)
    card.state = ES.Text(card, 32, ES.TEXT); card.state:SetPoint("TOPLEFT", card.emblem, "TOPRIGHT", 16, -6)
    card.state:SetFont(ES.FONT, 32, "OUTLINE")
    card.timer = ES.Text(card, 11, ES.MUTED); card.timer:SetPoint("TOPLEFT", card.state, "BOTTOMLEFT", 2, -6)
    card.desc = ES.Text(card, 12, ES.TEXT); card.desc:SetPoint("TOPLEFT", 18, -128); card.desc:SetWidth(384)
    card.desc:SetJustifyH("LEFT")
    card.badges = {}
    for i, name in ipairs({ "Bounty", "Thief", "Wanted", "Murderer" }) do
        local b = CreateFrame("Frame", nil, card)
        b:SetSize(90, 20); b:SetPoint("TOPLEFT", 18 + (i - 1) * 96, -176)
        b.bg = ES.Solid(b, "BACKGROUND", NOTORIETY[name].color, 0.18); b.bg:SetAllPoints()
        b.lines = ES.Border(b, NOTORIETY[name].color, 0.8)
        b.text = ES.Text(b, 10, NOTORIETY[name].color); b.text:SetPoint("CENTER"); b.text:SetText(name:upper())
        card.badges[name] = b
    end
    local divider = ES.Solid(card, "ARTWORK", ES.GOLD_DIM, 0.4)
    divider:SetPoint("TOPLEFT", 18, -210); divider:SetPoint("TOPRIGHT", -18, -210); divider:SetHeight(1)
    heading(card, "Current bounty"):SetPoint("TOPLEFT", 18, -226)
    card.bounty = ES.Text(card, 22, ES.GOLD); card.bounty:SetPoint("TOPLEFT", 18, -246)
    card.murders = stat(card, "Murders on record"); card.murders:SetPoint("TOPLEFT", 18, -294)
    card.lifetime = stat(card, "Lifetime murders"); card.lifetime:SetPoint("TOPLEFT", 148, -294)
    card.stolen = stat(card, "Gold stolen", 150); card.stolen:SetPoint("TOPLEFT", 278, -294)
    p.card = card

    -- Right: the PvP toggle and the rules of world PvP.
    local rules = panel(p, 480, 360); rules:SetPoint("TOPRIGHT", -20, -16)
    heading(rules, "World PvP"):SetPoint("TOPLEFT", 18, -16)
    rules.toggle = button(rules, 200, "", true, 36); rules.toggle:SetPoint("TOPLEFT", 18, -40)
    rules.toggle:SetScript("OnClick", function()
        PlaySound("igMainMenuOptionCheckBoxOn")
        local _, desired = pvpState()
        SetPVP(not desired and 1 or nil)
    end)
    rules.flag = rules:CreateTexture(nil, "ARTWORK"); rules.flag:SetSize(42, 42)
    rules.flag:SetPoint("LEFT", rules.toggle, "RIGHT", 14, -4)
    rules.state = ES.Text(rules, 13, ES.TEXT); rules.state:SetPoint("TOPLEFT", rules.toggle, "TOPRIGHT", 62, -2)
    rules.hint = ES.Text(rules, 10, ES.MUTED); rules.hint:SetPoint("TOPLEFT", rules.state, "BOTTOMLEFT", 0, -4)
    rules.hint:SetWidth(200); rules.hint:SetJustifyH("LEFT")
    local entries = {
        { "Interface\\TargetingFrame\\UI-PVP-FFA", "Free-for-all", ES.GOLD, true,
          "With PvP on you can attack, and be attacked by, any other flagged player: Alliance, Horde or Freeborn, " ..
          "even your own faction." },
        { "Interface\\Icons\\Ability_Warrior_RallyingCry", "Freeborn", { 0.55, 0.85, 0.5 }, false,
          "Freeborn never harm, and are never harmed by, members of their own group or guild, even while flagged." },
        { "Interface\\Icons\\Spell_Holy_DivineIntervention", "Sanctuaries", { 0.6, 0.8, 1 }, false,
          "Capital cities are sanctuaries: no fighting, and the only place PvP can be turned off. Small towns " ..
          "offer no shelter, and their guards ignore criminals." },
        { "Interface\\Icons\\INV_Misc_Bone_Skull_01", "The law", { 0.95, 0.3, 0.22 }, false,
          "Murder, bounties and pickpocketing are covered in the Laws tab. Wilderness zones have their own tab." },
    }
    for i, e in ipairs(entries) do
        local y = -96 - (i - 1) * 64
        local t = rules:CreateTexture(nil, "ARTWORK")
        if e[4] then
            t:SetSize(42, 42); t:SetPoint("TOPLEFT", 16, y + 2); t:SetTexCoord(0, 0.62, 0, 0.62)
        else
            t:SetSize(30, 30); t:SetPoint("TOPLEFT", 22, y - 4); t:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        end
        t:SetTexture(e[1])
        local name = ES.Text(rules, 13, e[3]); name:SetPoint("TOPLEFT", 66, y); name:SetText(e[2])
        local body = ES.Text(rules, 11, ES.TEXT); body:SetPoint("TOPLEFT", 66, y - 18); body:SetWidth(396)
        body:SetJustifyH("LEFT"); body:SetText(e[5])
    end
    p.rules = rules

    -- Bottom: honor and arena, as on the stock PvP frame.
    local honor = panel(p, 920, 190); honor:SetPoint("BOTTOM", 0, 16)
    heading(honor, "Honor & Arena"):SetPoint("TOPLEFT", 18, -16)
    honor.points = stat(honor, "Honor points", 160); honor.points:SetPoint("TOPLEFT", 18, -44)
    honor.arena = stat(honor, "Arena points", 160); honor.arena:SetPoint("TOPLEFT", 18, -104)
    honor.points.value:SetFont(ES.FONT, 22); honor.arena.value:SetFont(ES.FONT, 22)
    honor.cols = {}
    for c, head in ipairs({ "Today", "Yesterday", "Lifetime" }) do
        local h = heading(honor, head, 10); h:SetPoint("TOPLEFT", 260 + (c - 1) * 150, -44)
        honor.cols[c] = { kills = stat(honor, "Honorable kills"), honor = stat(honor, "Honor gained") }
        honor.cols[c].kills:SetPoint("TOPLEFT", 260 + (c - 1) * 150, -66)
        honor.cols[c].honor:SetPoint("TOPLEFT", 260 + (c - 1) * 150, -124)
    end
    honor.wg = ES.Text(honor, 12, ES.TEXT); honor.wg:SetPoint("TOPRIGHT", -18, -44); honor.wg:SetJustifyH("RIGHT")
    honor.wgLabel = heading(honor, "Wintergrasp", 10); honor.wgLabel:SetPoint("BOTTOMRIGHT", honor.wg, "TOPRIGHT", 0, 4)
    p.honor = honor
end

local function paintOverview(p)
    local name = notoriety()
    local look = NOTORIETY[name]
    local card = p.card
    card.state:SetText(name); card.state:SetTextColor(unpack(look.color))
    card.emblem.icon:SetTexture(look.icon); ES.Tint(card.emblem.lines, look.color, 1)
    card.tint:SetGradientAlpha("VERTICAL", look.color[1], look.color[2], look.color[3], 0,
        look.color[1], look.color[2], look.color[3], 0.22)
    ES.Tint(card.lines, look.color, 0.6)
    card.desc:SetText(status.enabled == 0 and "World PvP crime tracking is disabled on this realm." or look.text)
    local timer = {}
    if status.wanted == 1 and (status.wantedLeft or 0) > 0 then timer[#timer + 1] = "Wanted for " .. since(status.wantedLeft) end
    card.timer:SetText(table.concat(timer, "  "))
    for badge, b in pairs(card.badges) do
        local on = badge == "Bounty" and (status.bounty or 0) > 0 or badge == "Thief" and status.thief == 1 or
            badge == "Wanted" and status.wanted == 1 or badge == "Murderer" and name == "Murderer"
        b:SetAlpha(on and 1 or 0.18)
    end
    card.bounty:SetText((status.bounty or 0) > 0 and money(status.bounty) or "|cff8a8a80None|r")
    card.murders.value:SetText(status.notoriety or 0)
    card.lifetime.value:SetText(status.murders or 0)
    card.stolen.value:SetText(money(status.stolen or 0))

    local rules = p.rules
    local flagged, desired = pvpState()
    rules.toggle.label:SetText(desired and "Turn PvP Off" or "Turn PvP On")
    rules.state:SetText(flagged and "|cffff5a40PvP is ON|r" or "|cff8ad87fPvP is OFF|r")
    local faction = UnitFactionGroup("player")
    rules.flag:SetTexture(faction == "Alliance" and "Interface\\TargetingFrame\\UI-PVP-Alliance" or
        faction == "Horde" and "Interface\\TargetingFrame\\UI-PVP-Horde" or "Interface\\TargetingFrame\\UI-PVP-FFA")
    rules.flag:SetTexCoord(0, 0.62, 0, 0.62)
    rules.flag:SetDesaturated(not flagged)
    local hint
    if flagged and not desired and IsPVPTimerRunning() then
        hint = "Turning off in " .. since(math.ceil(GetPVPTimer() / 1000)) .. "."
    elseif desired and (status.wanted == 1 or status.thief == 1 or (status.bounty or 0) > 0) then
        hint = "Locked on while you are Wanted, a Thief, or hunted."
    elseif desired then
        hint = "Can only be turned off in a capital city."
    else
        hint = "You cannot be attacked by other players unless a bounty or Wilderness zone allows it."
    end
    rules.hint:SetText(hint)

    local honor = p.honor
    honor.points.value:SetText(GetHonorCurrency()); honor.arena.value:SetText(GetArenaCurrency())
    local sk, sh = GetPVPSessionStats()
    local yk, yh = GetPVPYesterdayStats()
    local lk = GetPVPLifetimeStats()
    local values = { { sk, sh }, { yk, yh }, { lk, "-" } }
    for c, col in ipairs(honor.cols) do
        col.kills.value:SetText(values[c][1] or 0); col.honor.value:SetText(values[c][2] or 0)
    end
    local wait = GetWintergraspWaitTime and GetWintergraspWaitTime()
    honor.wg:SetText(wait and wait > 0 and ("Next battle in " .. SecondsToTime(wait, wait > 60)) or
        (WINTERGRASP_IN_PROGRESS or "In progress"))
end

-- Battlegrounds -----------------------------------------------------------------------------------------------------
local selectedBG
local function bgRows()
    local rows = {}
    if SortBGList then SortBGList() end
    for i = 1, GetNumBattlegroundTypes() do
        local name, canEnter, isHoliday, isRandom, id = GetBattlegroundInfo(i)
        if name and canEnter then rows[#rows + 1] = { index = i, name = name, holiday = isHoliday, random = isRandom, id = id } end
    end
    return rows
end

local function queued(name)
    for i = 1, MAX_BATTLEFIELD_QUEUES or 3 do
        local s, map = GetBattlefieldStatus(i)
        if s ~= "none" and map == name then return s end
    end
end

local function buildBattlegrounds(p)
    local left = panel(p, 330, 566); left:SetPoint("TOPLEFT", 20, -16)
    heading(left, "Battlegrounds"):SetPoint("TOPLEFT", 18, -16)
    p.list = list(left, 310, 44, 11, function(parent, w, h)
        local row = CreateFrame("Button", nil, parent); row:SetSize(w, h)
        row.wash = ES.Solid(row, "ARTWORK", ES.GOLD, 0); row.wash:SetAllPoints()
        row.accent = ES.Solid(row, "OVERLAY", ES.GOLD, 0)
        row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
        row.name = ES.Text(row, 13, ES.TEXT); row.name:SetPoint("LEFT", 16, 6)
        row.tag = ES.Text(row, 10, ES.MUTED); row.tag:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3)
        row:SetScript("OnClick", function(self)
            PlaySound("igMainMenuOptionCheckBoxOn")
            selectedBG = self.item.index
            RequestBattlegroundInstanceInfo(selectedBG)
            frame.Refresh()
        end)
        return row
    end, function(row, item)
        row.item = item
        local on = item.index == selectedBG
        row.wash:SetAlpha(on and 0.12 or 0); row.accent:SetAlpha(on and 1 or 0)
        row.name:SetText(item.name); row.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        local tags = {}
        if item.holiday then tags[#tags + 1] = "|cffe8c36aCall to Arms|r" end
        local q = queued(item.name)
        if q == "queued" then tags[#tags + 1] = "|cff8ad87fIn queue|r" elseif q == "confirm" then tags[#tags + 1] = "|cff8ad87fReady to enter|r" end
        row.tag:SetText(table.concat(tags, "   "))
    end)
    p.list:SetPoint("TOPLEFT", 10, -44)
    p.list.empty:SetText("No battlegrounds are available at your level.")

    local right = panel(p, 570, 566); right:SetPoint("TOPRIGHT", -20, -16)
    right.art = right:CreateTexture(nil, "ARTWORK"); right.art:SetPoint("TOPLEFT", 1, -1); right.art:SetPoint("TOPRIGHT", -1, -1)
    right.art:SetHeight(220); right.art:SetTexCoord(0.015, 0.635, 0.06, 0.55)
    local fade = right:CreateTexture(nil, "OVERLAY"); fade:SetTexture(ES.WHITE)
    fade:SetGradientAlpha("VERTICAL", 0.06, 0.07, 0.10, 1, 0.06, 0.07, 0.10, 0)
    fade:SetPoint("BOTTOMLEFT", right.art); fade:SetPoint("BOTTOMRIGHT", right.art); fade:SetHeight(110)
    right.name = ES.Text(right, 24, ES.GOLD); right.name:SetPoint("TOPLEFT", 22, -176)
    right.name:SetFont(ES.FONT, 24, "OUTLINE")
    right.desc = ES.Text(right, 12, ES.TEXT); right.desc:SetPoint("TOPLEFT", 22, -236); right.desc:SetWidth(526)
    right.desc:SetJustifyH("LEFT")
    right.rewardHead = heading(right, "Rewards"); right.rewardHead:SetPoint("TOPLEFT", 22, -330)
    right.win = ES.Text(right, 13, ES.GOOD); right.win:SetPoint("TOPLEFT", 22, -352)
    right.loss = ES.Text(right, 13, ES.BAD); right.loss:SetPoint("TOPLEFT", 22, -376)
    right.queue = ES.Text(right, 12, ES.MUTED); right.queue:SetPoint("BOTTOMLEFT", 22, 64)
    right.join = button(right, 170, "Join Battle", true, 32); right.join:SetPoint("BOTTOMRIGHT", -22, 20)
    right.group = button(right, 170, "Join as Group", false, 32); right.group:SetPoint("RIGHT", right.join, "LEFT", -10, 0)
    right.join:SetScript("OnClick", function() JoinBattlefield(0) end)
    right.group:SetScript("OnClick", function() JoinBattlefield(0, true) end)
    p.right = right
end

local function paintBattlegrounds(p)
    local rows = bgRows()
    if not selectedBG and rows[1] then selectedBG = rows[1].index; RequestBattlegroundInstanceInfo(selectedBG) end
    p.list.data = rows; p.list:Refresh()
    local item
    for _, r in ipairs(rows) do if r.index == selectedBG then item = r end end
    local right = p.right
    right.art:SetTexture(item and BG_ART[item.id] or "Interface\\PVPFrame\\PvpRandomBg")
    right.name:SetText(item and item.name or "")
    local _, description, maxGroup = GetBattlefieldInfo()
    right.desc:SetText(item and (item.random or item.holiday) and
        (item.holiday and "Call to Arms: your urgent assistance is needed. Completing this battleground earns extra rewards."
         or "Join a random battleground for bonus honor on your first win of the day.") or description or "")
    local bonuses = item and (item.holiday and GetHolidayBGHonorCurrencyBonuses or item.random and GetRandomBGHonorCurrencyBonuses)
    local show = bonuses ~= nil
    if bonuses then
        local _, winHonor, winArena, lossHonor = bonuses()
        right.win:SetText(("Win:  %d honor%s"):format(winHonor or 0, (winArena or 0) > 0 and ("  +  " .. winArena .. " arena points") or ""))
        right.loss:SetText(("Loss:  %d honor"):format(lossHonor or 0))
    end
    shown(right.rewardHead, show); shown(right.win, show); shown(right.loss, show)
    local q = item and queued(item.name)
    right.queue:SetText(q == "queued" and "You are in the queue for this battleground." or
        q == "confirm" and "Your battleground is ready." or "")
    right.join:Active(item ~= nil)
    right.group:Active(item ~= nil and (GetNumPartyMembers() > 0 or GetNumRaidMembers() > 0) and IsPartyLeader())
    right.group.label:SetText(maxGroup == 5 and (JOIN_AS_PARTY or "Join as Party") or (JOIN_AS_GROUP or "Join as Group"))
end

-- Arena -------------------------------------------------------------------------------------------------------------
local function buildArena(p)
    p.cards = {}
    for i, size in ipairs({ 2, 3, 5 }) do
        local c = panel(p, 293, 530); c:SetPoint("TOPLEFT", 20 + (i - 1) * 310, -16)
        band(c, 140, 0.91, 0.77, 0.42)
        c.size = ES.Text(c, 40, ES.GOLD); c.size:SetPoint("TOP", 0, -22); c.size:SetFont(ES.FONT, 40, "OUTLINE")
        c.size:SetText(size .. "v" .. size)
        c.team = ES.Text(c, 14, ES.TEXT); c.team:SetPoint("TOP", 0, -78); c.team:SetWidth(260)
        c.rating = stat(c, "Team rating", 120); c.rating:SetPoint("TOPLEFT", 22, -130)
        c.personal = stat(c, "Personal rating", 120); c.personal:SetPoint("TOPLEFT", 156, -130)
        c.rating.value:SetFont(ES.FONT, 26); c.personal.value:SetFont(ES.FONT, 26)
        c.week = stat(c, "This week", 120); c.week:SetPoint("TOPLEFT", 22, -210)
        c.season = stat(c, "This season", 120); c.season:SetPoint("TOPLEFT", 156, -210)
        c.played = stat(c, "Your games (week / season)", 250); c.played:SetPoint("TOPLEFT", 22, -280)
        c.rank = stat(c, "Team rank", 120); c.rank:SetPoint("TOPLEFT", 22, -350)
        c.none = ES.Text(c, 12, ES.MUTED); c.none:SetPoint("TOP", 0, -140); c.none:SetWidth(240)
        c.none:SetText("No team. Speak to an Arena Organizer in a capital city to create one.")
        c.sizeValue = size
        p.cards[i] = c
    end
    p.off = ES.Text(p, 13, ES.GOLD); p.off:SetPoint("BOTTOM", 0, 22)
end

local function paintArena(p)
    local teams = {}
    for i = 1, MAX_ARENA_TEAMS or 3 do
        local name, size, rating, played, wins, sPlayed, sWins, pPlayed, spPlayed, rank, personal = GetArenaTeam(i)
        if name then teams[size] = { name, rating, played, wins, sPlayed, sWins, pPlayed, spPlayed, rank, personal } end
    end
    for _, c in ipairs(p.cards) do
        local t = teams[c.sizeValue]
        for _, s in ipairs({ c.rating, c.personal, c.week, c.season, c.played, c.rank }) do shown(s, t ~= nil) end
        shown(c.none, t == nil)
        c.team:SetText(t and t[1] or "")
        if t then
            c.rating.value:SetText(t[2]); c.personal.value:SetText(t[10] or 0)
            c.week.value:SetText(("%d - %d"):format(t[4], t[3] - t[4]))
            c.season.value:SetText(("%d - %d"):format(t[6], t[5] - t[6]))
            c.played.value:SetText(("%d / %d"):format(t[7] or 0, t[8] or 0))
            c.rank.value:SetText(t[9] and t[9] > 0 and ("#" .. t[9]) or "-")
        end
    end
    local season = GetCurrentArenaSeason and GetCurrentArenaSeason() or 0
    p.off:SetText(season == 0 and "The arena season is currently off. Ratings resume next season." or
        ("Arena Season " .. season))
end

-- Bounties ----------------------------------------------------------------------------------------------------------
StaticPopupDialogs["ESTERIA_CRIME_POST"] = {
    text = "Place a bounty of %s on %s?\n\nThe gold is held in escrow for 7 days and refunded if no one claims it.",
    button1 = ACCEPT, button2 = CANCEL, timeout = 0, whileDead = 1, hideOnEscape = 1,
    OnAccept = function(_, data) send("post\t" .. data.name .. "\t" .. data.gold) end,
}

local function buildBounties(p)
    local left = panel(p, 560, 566); left:SetPoint("TOPLEFT", 20, -16)
    heading(left, "Active bounties"):SetPoint("TOPLEFT", 18, -16)
    p.total = ES.Text(left, 11, ES.MUTED); p.total:SetPoint("TOPRIGHT", -18, -16)
    p.list = list(left, 540, 40, 9, function(parent, w, h)
        local row = CreateFrame("Button", nil, parent); row:SetSize(w, h)
        row.rank = ES.Text(row, 14, ES.GOLD_DIM); row.rank:SetPoint("LEFT", 12, 0); row.rank:SetWidth(30)
        row.name = ES.Text(row, 13, ES.TEXT); row.name:SetPoint("LEFT", 48, 6)
        row.level = ES.Text(row, 10, ES.MUTED); row.level:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -2)
        row.amount = ES.Text(row, 13, ES.GOLD); row.amount:SetPoint("RIGHT", -14, 6)
        row.split = ES.Text(row, 10, ES.MUTED); row.split:SetPoint("TOPRIGHT", row.amount, "BOTTOMRIGHT", 0, -2)
        row:SetScript("OnClick", function(self) p.name:SetText(self.item.name); p.gold:SetFocus() end)
        return row
    end, function(row, item, index)
        row.item = item
        row.rank:SetText(index)
        row.name:SetText("|c" .. classColor(item.class) .. item.name .. "|r")
        row.level:SetText("Level " .. item.level)
        row.amount:SetText(money(item.auto + item.player))
        local parts = {}
        if item.player > 0 then parts[#parts + 1] = "Players " .. money(item.player) end
        if item.auto > 0 then parts[#parts + 1] = "Automatic " .. money(item.auto) end
        row.split:SetText(table.concat(parts, "   "))
    end)
    p.list:SetPoint("TOPLEFT", 10, -44)
    p.list.empty:SetText("There are no active bounties. The realm is quiet... for now.")

    -- Bounties this character has earned with a qualifying killing blow, paid out from here.
    local earned = CreateFrame("Frame", nil, left); earned:SetPoint("BOTTOMLEFT", 1, 1); earned:SetPoint("BOTTOMRIGHT", -1, 1)
    earned:SetHeight(150)
    local glow = earned:CreateTexture(nil, "BORDER"); glow:SetTexture(ES.WHITE); glow:SetAllPoints()
    glow:SetGradientAlpha("VERTICAL", 1, 0.78, 0.25, 0.16, 1, 0.78, 0.25, 0)
    local line = ES.Solid(earned, "ARTWORK", ES.GOLD_DIM, 0.6); line:SetPoint("TOPLEFT", 16, 0); line:SetPoint("TOPRIGHT", -16, 0)
    line:SetHeight(1)
    heading(earned, "Your earned bounties"):SetPoint("TOPLEFT", 17, -16)
    p.earnedIcon = icon(earned, 52, "Interface\\Icons\\INV_Misc_Bag_10_Black"); p.earnedIcon:SetPoint("TOPLEFT", 17, -40)
    p.earnedTotal = ES.Text(earned, 22, ES.GOLD); p.earnedTotal:SetPoint("TOPLEFT", p.earnedIcon, "TOPRIGHT", 14, -2)
    p.earnedList = ES.Text(earned, 11, ES.MUTED); p.earnedList:SetPoint("TOPLEFT", p.earnedTotal, "BOTTOMLEFT", 0, -6)
    p.earnedList:SetWidth(300); p.earnedList:SetJustifyH("LEFT")
    p.collect = button(earned, 170, "Collect", true, 34); p.collect:SetPoint("BOTTOMRIGHT", -16, 18)
    p.collect:SetScript("OnClick", function() PlaySound("LOOTWINDOWCOINSOUND"); send("collect") end)

    local right = panel(p, 340, 566); right:SetPoint("TOPRIGHT", -20, -16)
    band(right, 90, 1, 0.78, 0.25)
    heading(right, "Place a bounty"):SetPoint("TOPLEFT", 18, -16)
    local crest = icon(right, 48, "Interface\\Icons\\INV_Misc_Coin_02"); crest:SetPoint("TOPLEFT", 18, -42)
    local pitch = ES.Text(right, 12, ES.TEXT); pitch:SetPoint("TOPLEFT", crest, "TOPRIGHT", 12, -2)
    pitch:SetWidth(240); pitch:SetJustifyH("LEFT")
    pitch:SetText("Put a price on a character's head. The player who lands the killing blow collects it.")
    heading(right, "Character", 10):SetPoint("TOPLEFT", 18, -116)
    p.name = editBox(right, 220); p.name:SetPoint("TOPLEFT", 18, -132)
    local useTarget = button(right, 78, "Target"); useTarget:SetPoint("LEFT", p.name, "RIGHT", 6, 0)
    useTarget:SetScript("OnClick", function()
        if UnitIsPlayer("target") then p.name:SetText(UnitName("target")) end
    end)
    heading(right, "Gold", 10):SetPoint("TOPLEFT", 18, -176)
    p.gold = editBox(right, 140, true); p.gold:SetPoint("TOPLEFT", 18, -192)
    p.gold:SetText("100")
    local coin = ES.Text(right, 12, ES.MUTED); coin:SetPoint("LEFT", p.gold, "RIGHT", 8, 0); coin:SetText("gold  (minimum 100)")
    p.post = button(right, 304, "Post Bounty", true, 34); p.post:SetPoint("TOPLEFT", 18, -240)
    p.post:SetScript("OnClick", function()
        local name, gold = strtrim(p.name:GetText() or ""), tonumber(p.gold:GetText())
        if name == "" or not gold then UIErrorsFrame:AddMessage("Enter a character name and an amount.", 1, 0.2, 0.2); return end
        if gold < 100 then UIErrorsFrame:AddMessage("Each bounty contribution must be at least 100 gold.", 1, 0.2, 0.2); return end
        if gold * 10000 > GetMoney() then UIErrorsFrame:AddMessage(ERR_NOT_ENOUGH_MONEY, 1, 0.2, 0.2); return end
        p.name:ClearFocus(); p.gold:ClearFocus()
        StaticPopup_Show("ESTERIA_CRIME_POST", money(gold * 10000), name, { name = name, gold = gold })
    end)
    p.name:SetScript("OnEnterPressed", function() p.gold:SetFocus() end)
    p.gold:SetScript("OnEnterPressed", function() p.post:Click() end)
    local rules = ES.Text(right, 11, ES.MUTED); rules:SetPoint("TOPLEFT", 18, -296); rules:SetWidth(304)
    rules:SetJustifyH("LEFT")
    rules:SetText("- Each contribution must be at least 100 gold. Bounties stack.\n\n" ..
        "- A bounty lets hunters attack the target outside sanctuaries, even with PvP off.\n\n" ..
        "- Killing a target who is not Wanted still counts as murder, even for the bounty.\n\n" ..
        "- Unclaimed gold returns to you after 7 days. The target's death clears all bounties.\n\n" ..
        "- You cannot place a bounty on your own account's characters.")
end

local function paintBounties(p)
    p.list.data = board; p.list:Refresh()
    local earned, names = 0, {}
    for i, c in ipairs(claims) do
        earned = earned + c.amount
        if i <= 3 then names[#names + 1] = c.name .. " (" .. money(c.amount) .. ")" end
    end
    if #claims > 3 then names[#names + 1] = "and " .. (#claims - 3) .. " more" end
    p.earnedTotal:SetText(earned > 0 and money(earned) or "|cff8a8a80Nothing to collect|r")
    p.earnedList:SetText(#claims > 0 and table.concat(names, ", ") or
        "Land the killing blow on a Wanted or hunted player to earn their bounty.")
    p.earnedIcon.icon:SetDesaturated(earned == 0)
    p.collect:Active(earned > 0)
    local total = 0
    for _, b in ipairs(board) do total = total + b.auto + b.player end
    p.total:SetText(#board > 0 and ("%d targets  -  %s"):format(#board, money(total)) or "")
    p.post:Active(status.enabled ~= 0)
end

-- Crime log ---------------------------------------------------------------------------------------------------------
local KIND = {
    ["1"] = { "Interface\\Icons\\INV_Misc_Bone_Skull_01", { 0.95, 0.3, 0.22 } },
    ["2"] = { "Interface\\Icons\\INV_Misc_Bag_11", { 0.68, 0.42, 1 } },
    ["3"] = { "Interface\\Icons\\Ability_Rogue_Disguise", ES.MUTED },
}

local function buildLog(p)
    local box = panel(p, 920, 566); box:SetPoint("TOP", 0, -16)
    heading(box, "Your criminal record"):SetPoint("TOPLEFT", 18, -16)
    p.summary = ES.Text(box, 11, ES.MUTED); p.summary:SetPoint("TOPRIGHT", -18, -16)
    p.list = list(box, 900, 40, 12, function(parent, w, h)
        local row = CreateFrame("Frame", nil, parent); row:SetSize(w, h)
        row.icon = icon(row, 28); row.icon:SetPoint("LEFT", 12, 0)
        row.text = ES.Text(row, 13, ES.TEXT); row.text:SetPoint("LEFT", 54, 6)
        row.detail = ES.Text(row, 10, ES.MUTED); row.detail:SetPoint("TOPLEFT", row.text, "BOTTOMLEFT", 0, -2)
        row.when = ES.Text(row, 11, ES.MUTED); row.when:SetPoint("RIGHT", -16, 0)
        return row
    end, function(row, item)
        local look = KIND[item.kind] or KIND["3"]
        row.icon.icon:SetTexture(look[1]); ES.Tint(row.icon.lines, look[2], 1)
        local who = "|cffffffff" .. item.name .. "|r"
        if item.kind == "1" then
            row.text:SetText("Murdered " .. who)
            row.detail:SetText(item.zone ~= "" and ("in " .. item.zone) or "")
        elseif item.kind == "2" then
            row.text:SetText("Robbed " .. who)
            local loot = {}
            if item.amount > 0 then loot[#loot + 1] = money(item.amount) end
            if item.count > 0 then loot[#loot + 1] = item.count .. (item.count == 1 and " item" or " items") end
            row.detail:SetText("Stole " .. table.concat(loot, " and "))
        else
            row.text:SetText("Tried to pick " .. who .. "'s pocket")
            row.detail:SetText("Caught empty-handed")
        end
        row.when:SetText(date("%b %d, %Y  %H:%M", item.time))
    end)
    p.list:SetPoint("TOPLEFT", 10, -44)
    p.list.empty:SetText("Your record is clean. Murders and thefts you commit will be listed here.")
end

local function paintLog(p)
    p.list.data = log; p.list:Refresh()
    local m, t = 0, 0
    for _, e in ipairs(log) do if e.kind == "1" then m = m + 1 else t = t + 1 end end
    p.summary:SetText(#log > 0 and ("%d murders  -  %d theft attempts  (most recent 50)"):format(m, t) or "")
end

-- Leaderboard -------------------------------------------------------------------------------------------------------
local MEDALS = { { 1, 0.82, 0.25 }, { 0.82, 0.84, 0.88 }, { 0.8, 0.5, 0.25 } }
local function buildLeaders(p)
    p.columns = {}
    local heads = {
        { "Most murders", "Interface\\Icons\\INV_Misc_Bone_Skull_01", { 0.95, 0.3, 0.22 } },
        { "Most gold stolen", "Interface\\Icons\\INV_Misc_Bag_11", { 0.68, 0.42, 1 } },
        { "Highest bounty", "Interface\\Icons\\INV_Misc_Coin_02", { 1, 0.78, 0.25 } },
    }
    for i, h in ipairs(heads) do
        local c = panel(p, 293, 566); c:SetPoint("TOPLEFT", 20 + (i - 1) * 310, -16)
        band(c, 110, h[3][1], h[3][2], h[3][3])
        local crest = icon(c, 44, h[2]); crest:SetPoint("TOP", 0, -18); ES.Tint(crest.lines, h[3], 1)
        local title = ES.Text(c, 15, h[3]); title:SetPoint("TOP", crest, "BOTTOM", 0, -10); title:SetText(h[1])
        c.rows = {}
        for r = 1, 10 do
            local row = CreateFrame("Frame", nil, c); row:SetSize(273, 40); row:SetPoint("TOPLEFT", 10, -112 - (r - 1) * 44)
            row.bg = ES.Solid(row, "BACKGROUND", r <= 3 and MEDALS[r] or ES.GOLD, r <= 3 and 0.08 or 0.03); row.bg:SetAllPoints()
            row.rank = ES.Text(row, r <= 3 and 18 or 13, r <= 3 and MEDALS[r] or ES.MUTED); row.rank:SetPoint("LEFT", 12, 0)
            row.rank:SetText(r)
            row.name = ES.Text(row, 13, ES.TEXT); row.name:SetPoint("LEFT", 46, 0)
            row.value = ES.Text(row, 12, ES.GOLD); row.value:SetPoint("RIGHT", -12, 0)
            c.rows[r] = row
        end
        c.empty = ES.Text(c, 12, ES.MUTED); c.empty:SetPoint("TOP", 0, -140); c.empty:SetText("No entries yet.")
        p.columns[i] = c
    end
end

local function paintLeaders(p)
    local me = UnitName("player")
    for i, c in ipairs(p.columns) do
        local entries = leaders[i]
        for r, row in ipairs(c.rows) do
            local e = entries[r]
            shown(row, e ~= nil)
            if e then
                row.name:SetText(e.name == me and ("|cff8ad87f" .. e.name .. "|r") or e.name)
                row.value:SetText(i == 1 and e.value or money(e.value))
            end
        end
        shown(c.empty, #entries == 0)
    end
end

-- Laws ------------------------------------------------------------------------------------------------------------
-- Stacks icon + title + paragraph sections down a panel, measuring each paragraph so nothing overlaps.
local function sections(parent, y, width, items)
    for _, e in ipairs(items) do
        local t = parent:CreateTexture(nil, "ARTWORK"); t:SetSize(26, 26); t:SetPoint("TOPLEFT", 18, y - 2)
        t:SetTexture(e[1])
        if e[1]:find("TargetingFrame") then t:SetTexCoord(0, 0.62, 0, 0.62) else t:SetTexCoord(0.08, 0.92, 0.08, 0.92) end
        local title = ES.Text(parent, 13, e[3]); title:SetPoint("TOPLEFT", 56, y); title:SetText(e[2])
        local body = ES.Text(parent, 11, ES.TEXT); body:SetPoint("TOPLEFT", 56, y - 18); body:SetWidth(width - 74)
        body:SetJustifyH("LEFT"); body:SetText(e[4])
        y = y - 18 - (body:GetStringHeight() or 30) - 14
    end
    return y
end

local RED, GOLD, VIOLET = { 0.95, 0.3, 0.22 }, { 1, 0.78, 0.25 }, { 0.68, 0.42, 1 }
local function buildLaws(p)
    local law = panel(p, 452, 566); law:SetPoint("TOPLEFT", 20, -16)
    band(law, 80, 0.95, 0.3, 0.22)
    heading(law, "The law"):SetPoint("TOPLEFT", 18, -16)
    sections(law, -44, 452, {
        { "Interface\\Icons\\INV_Misc_Bone_Skull_01", "Murder", RED,
          "Killing a flagged player who is not Wanted is murder, unless they attacked you first. Self-defense " ..
          "lasts one minute after their last hostile action. Murder makes you Wanted for 24 hours." },
        { "Interface\\Icons\\Spell_Holy_SealOfSacrifice", "Lawful kills", ES.GOOD,
          "Killing a Wanted player, self-defense, duels, battlegrounds, arenas, and every kill inside an active " ..
          "Wilderness zone." },
        { "Interface\\Icons\\Ability_Rogue_FeignDeath", "Wanted and Murderer", RED,
          "Wanted players show on nameplates, tooltips, the minimap and the world map, and anyone may kill them " ..
          "lawfully. Three murders on record make you a Murderer. Murdering three different players within a day " ..
          "raises an automatic bounty of 25g, growing to 250g." },
        { "Interface\\Icons\\INV_Misc_Coin_02", "Bounties", GOLD,
          "Anyone can fund a bounty of 100g or more on any character, even one with PvP off. Flagged hunters may " ..
          "then attack the target outside sanctuaries, and the target can fight back. The killing blow earns " ..
          "every bounty on the target; collect it in the Bounties tab. Unclaimed gold returns after 7 days." },
        { "Interface\\Icons\\Spell_Holy_Resurrection", "Paying for your crimes", ES.TEXT,
          "Dying clears your Wanted status, your murders on record and every bounty on you. Your crime log and " ..
          "the leaderboard remember." },
    })

    local pick = panel(p, 452, 566); pick:SetPoint("TOPRIGHT", -20, -16)
    band(pick, 80, 0.68, 0.42, 1)
    heading(pick, "Pickpocketing"):SetPoint("TOPLEFT", 18, -16)
    sections(pick, -44, 452, {
        { "Interface\\Icons\\INV_Misc_Bag_11", "Who can steal", VIOLET,
          "Rogues and Heroes who know Pick Pocket can rob another player while stealthed, from within 5 yards. " ..
          "The target must be flagged for PvP, except inside a Wilderness zone, where everyone is fair game." },
        { "Interface\\Icons\\INV_Misc_Coin_04", "What you get", GOLD,
          "A theft succeeds 60% of the time, or 70% while you carry an Underbelly Pickpocket Kit. It takes 1% of " ..
          "the victim's gold, up to 5g. In the Wilderness it also has a 10% chance to lift a stack of up to five " ..
          "unbound supplies, which arrive in your mailbox." },
        { "Interface\\Icons\\Ability_Rogue_Disguise", "Getting caught", RED,
          "Win or lose, the victim is told who tried. You are marked as a Thief for five minutes, visible to " ..
          "everyone nearby, and your PvP flag is locked on." },
        { "Interface\\Icons\\Spell_Nature_TimeStop", "Cooldown", ES.TEXT,
          "You can only try the same player's pockets once every ten minutes, across all of your characters." },
        { "Interface\\Icons\\INV_Misc_Ear_NightElf_02", "The Underbelly", ES.TEXT,
          "Murders earn Ear Trophies. Wanted players can trade them with the Underbelly Fence in Dalaran for " ..
          "gold, Stun Powder and Pickpocket Kits." },
    })
end

local function paintLaws() end

-- Wilderness ------------------------------------------------------------------------------------------------------
local WILD_POOL = { "Zul'Drak", "The Storm Peaks", "Icecrown", "Sholazar Basin" }
local wild, auction = { left = 0, hours = 4, zones = {} }, {}

StaticPopupDialogs["ESTERIA_BLOODY_BUY"] = {
    text = "Buy this pouch from the Bloody Auction House for %s?",
    button1 = ACCEPT, button2 = CANCEL, timeout = 0, whileDead = 1, hideOnEscape = 1,
    OnAccept = function(_, id) send("buy\t" .. id) end,
}

local function buildWilderness(p)
    local top = panel(p, 920, 150); top:SetPoint("TOP", 0, -16)
    band(top, 60, 0.95, 0.36, 0.14)
    heading(top, "Wilderness zones"):SetPoint("TOPLEFT", 18, -16)
    p.rotation = ES.Text(top, 11, ES.MUTED); p.rotation:SetPoint("TOPRIGHT", -18, -16)
    p.cards = {}
    for i, name in ipairs(WILD_POOL) do
        local c = CreateFrame("Frame", nil, top); c:SetSize(212, 86); c:SetPoint("TOPLEFT", 18 + (i - 1) * 222, -44)
        c.bg = ES.Solid(c, "BACKGROUND", { 0.95, 0.36, 0.14 }, 0.12); c.bg:SetAllPoints()
        c.lines = ES.Border(c, { 0.95, 0.36, 0.14 }, 0.8)
        c.icon = c:CreateTexture(nil, "ARTWORK"); c.icon:SetSize(34, 34); c.icon:SetPoint("LEFT", 12, 0)
        c.icon:SetTexture("Interface\\Icons\\Ability_DualWield"); c.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        c.name = ES.Text(c, 14, ES.TEXT); c.name:SetPoint("TOPLEFT", 56, -24); c.name:SetText(name)
        c.state = ES.Text(c, 10, ES.MUTED); c.state:SetPoint("TOPLEFT", c.name, "BOTTOMLEFT", 0, -4)
        p.cards[i] = c
    end

    local how = panel(p, 452, 400); how:SetPoint("BOTTOMLEFT", 20, 16)
    heading(how, "How the Wilderness works"):SetPoint("TOPLEFT", 18, -16)
    sections(how, -42, 452, {
        { "Interface\\TargetingFrame\\UI-PVP-FFA", "Forced free-for-all", { 0.95, 0.36, 0.14 },
          "Everyone inside is flagged and can't turn it off. Every kill is lawful. Freeborn groups and guilds " ..
          "stay protected, and anyone can be pickpocketed." },
        { "Interface\\Icons\\INV_Misc_Coin_17", "Greater rewards", GOLD,
          "Enemies have 25% more health and hit 15% harder, and give 25% more XP, gold and reputation. Honor is " ..
          "up 25% (50% for Freeborn), and a killing blow on a player earns arena points once a day per foe." },
        { "Interface\\Icons\\Spell_Shadow_DeathScream", "Dying costs you", RED,
          "You lose 5% of your XP toward the next level and drop a pouch with 1% of your gold (2% if a Freeborn " ..
          "killed you). There's a 10% chance an item drops too, even the weapon in your hands. You can't " ..
          "resurrect at your corpse." },
        { "Interface\\Icons\\INV_Misc_Bag_10_Black", "Getting it back", ES.TEXT,
          "Only you can loot your pouch for two minutes. After 15 minutes it goes to the Bloody Auction House, " ..
          "where you get the first 24 hours to buy it back." },
    })

    local house = panel(p, 452, 400); house:SetPoint("BOTTOMRIGHT", -20, 16)
    band(house, 70, 0.6, 0.05, 0.05)
    heading(house, "Bloody Auction House"):SetPoint("TOPLEFT", 18, -16)
    local note = ES.Text(house, 11, ES.MUTED); note:SetPoint("TOPLEFT", 18, -36); note:SetWidth(416)
    note:SetJustifyH("LEFT")
    note:SetText("Buying back your own gold costs a 10% fee, your own gear 25%. Other players' unbound pouches " ..
        "open to everyone after a day. Items arrive by mail.")
    p.list = list(house, 432, 56, 5, function(parent, w, h)
        local row = CreateFrame("Frame", nil, parent); row:SetSize(w, h)
        row.icon = icon(row, 36); row.icon:SetPoint("LEFT", 10, 0)
        row.name = ES.Text(row, 13, ES.TEXT); row.name:SetPoint("TOPLEFT", 58, -10); row.name:SetWidth(240)
        row.name:SetJustifyH("LEFT")
        row.tag = ES.Text(row, 10, ES.MUTED); row.tag:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -3)
        row.buy = button(row, 96, "Buy", true, 26); row.buy:SetPoint("RIGHT", -10, -6)
        row.price = ES.Text(row, 11, ES.GOLD); row.price:SetPoint("BOTTOMRIGHT", row.buy, "TOPRIGHT", 0, 4)
        row.buy.row = row
        row.buy:SetScript("OnClick", function(self)
            local item = self.row.item
            StaticPopup_Show("ESTERIA_BLOODY_BUY", money(item.price), nil, item.id)
        end)
        return row
    end, function(row, item)
        row.item = item
        if item.entry > 0 then
            row.icon.icon:SetTexture(GetItemIcon(item.entry) or "Interface\\Icons\\INV_Misc_QuestionMark")
            row.name:SetText((item.name ~= "" and item.name or "Unknown item") ..
                (item.count > 1 and (" x" .. item.count) or ""))
        else
            row.icon.icon:SetTexture("Interface\\Icons\\INV_Misc_Bag_10_Black")
            row.name:SetText(money(item.copper))
        end
        row.tag:SetText(item.owner and "|cff8ad87fYour pouch - buyback|r" or "Public sale")
        row.price:SetText(money(item.price))
        row.buy:Active(GetMoney() >= item.price)
    end)
    p.list:SetPoint("TOPLEFT", 10, -84)
    p.list.empty:SetText("No pouches are for sale right now.")
end

local function paintWilderness(p)
    local active = {}
    for _, z in ipairs(wild.zones) do active[z:lower()] = true end
    for i, c in ipairs(p.cards) do
        local on = active[WILD_POOL[i]:lower()]
        c:SetAlpha(on and 1 or 0.45)
        c.bg:SetAlpha(on and 0.22 or 0.05)
        c.icon:SetDesaturated(not on)
        c.state:SetText(on and "|cffff7a3aACTIVE NOW|r" or "Dormant")
    end
    p.rotation:SetText(#wild.zones == 0 and "No Wilderness zone is active on this realm." or
        ("Rotates every %d hours. Next rotation in %s."):format(wild.hours, since(wild.left)))
    p.list.data = auction; p.list:Refresh()
end

-- Window ------------------------------------------------------------------------------------------------------------
local PANES = {
    { "Overview", buildOverview, paintOverview, "status" },
    { "Laws", buildLaws, paintLaws },
    { "Wilderness", buildWilderness, paintWilderness, "wild" },
    { "Battlegrounds", buildBattlegrounds, paintBattlegrounds },
    { "Arena", buildArena, paintArena },
    { "Bounties", buildBounties, paintBounties, "board" },
    { "Crime Log", buildLog, paintLog, "log" },
    { "Leaderboard", buildLeaders, paintLeaders, "top" },
}

local function selectTab(name)
    current = name
    for _, spec in ipairs(PANES) do
        local on = spec[1] == name
        tabs[spec[1]]:SetSelected(on)
        shown(panes[spec[1]], on)
        if on then
            if spec[4] then send(spec[4]) end
            if spec[4] ~= "status" then send("status") end
            if spec[1] == "Bounties" then send("claims") end
            if spec[1] == "Wilderness" then send("auction") end
        end
    end
    if name ~= "Battlegrounds" and CloseBattlefield then CloseBattlefield() end
    frame.Refresh()
end

local function build()
    frame = CreateFrame("Frame", "EsteriaCrimeFrame", UIParent)
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving); frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames, "EsteriaCrimeFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); band(frame, 42, 0.55, 0.40, 0.12); ES.Border(frame, ES.GOLD)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12); title:SetText("Player vs. Player")
    for i, spec in ipairs(PANES) do
        local b = tabButton(frame, 94, spec[1]); b:SetPoint("TOPLEFT", 186 + (i - 1) * 96, -6)
        b:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); selectTab(spec[1]) end)
        tabs[spec[1]] = b
        local pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")
        spec[2](pane); pane:Hide()
        panes[spec[1]] = pane
    end
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    frame.Refresh = function()
        for _, spec in ipairs(PANES) do if spec[1] == current then spec[3](panes[current]) end end
    end
    local clock = 0
    frame:SetScript("OnUpdate", function(_, elapsed)
        clock = clock + elapsed
        if clock < 1 then return end
        clock = 0
        if status.wantedLeft and status.wantedLeft > 0 then status.wantedLeft = status.wantedLeft - 1 end
        if wild.left > 1 then wild.left = wild.left - 1 elseif wild.left == 1 then wild.left = 0; send("wild") end
        if current == "Overview" or current == "Battlegrounds" or current == "Wilderness" then frame.Refresh() end
    end)
    frame:SetScript("OnShow", function() PlaySound("igCharacterInfoOpen"); selectTab(current) end)
    frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); if CloseBattlefield then CloseBattlefield() end end)
    for _, ev in ipairs({ "CHAT_MSG_ADDON", "UNIT_FACTION", "PLAYER_FLAGS_CHANGED", "HONOR_CURRENCY_UPDATE",
                          "PLAYER_PVP_KILLS_CHANGED", "ARENA_TEAM_UPDATE", "UPDATE_BATTLEFIELD_STATUS",
                          "PVPQUEUE_ANYWHERE_SHOW", "PVPQUEUE_ANYWHERE_UPDATE_AVAILABLE", "PARTY_MEMBERS_CHANGED",
                          "UNIT_AURA" }) do
        frame:RegisterEvent(ev)
    end
    frame:SetScript("OnEvent", function(_, ev, prefix, message, channel, sender)
        if ev == "UNIT_AURA" then
            -- Crime states arrive as debuffs; ask the server for the detail behind a change.
            if prefix == "player" and frame:IsShown() then send("status") end
            return
        elseif ev ~= "CHAT_MSG_ADDON" then
            if frame:IsShown() then frame.Refresh() end
            return
        end
        if prefix ~= PREFIX or channel ~= "WHISPER" or sender ~= UnitName("player") then return end
        local f = { strsplit("\t", message) }
        local kind = f[1]
        if kind == "E" then
            local done = f[2]
            if done == "B" then board = pending.B or {} elseif done == "L" then log = pending.L or {}
            elseif done == "C" then claims = pending.C or {}
            elseif done == "A" then auction = pending.A or {}
            elseif done == "T" then
                leaders = { {}, {}, {} }
                for _, e in ipairs(pending.T or {}) do table.insert(leaders[e.cat] or {}, e) end
            end
            pending[done] = nil
            if frame:IsShown() then frame.Refresh() end
        elseif kind == "S" then
            status = { enabled = tonumber(f[2]), wanted = tonumber(f[3]), thief = tonumber(f[4]),
                       notoriety = tonumber(f[5]), bounty = tonumber(f[6]), wantedLeft = tonumber(f[7]),
                       murders = tonumber(f[8]), stolen = tonumber(f[9]) }
        elseif kind == "B" then
            pending.B = pending.B or {}
            table.insert(pending.B, { name = f[2], class = f[3], level = f[4], auto = tonumber(f[5]) or 0,
                                      player = tonumber(f[6]) or 0 })
        elseif kind == "W" then
            wild = { left = tonumber(f[2]) or 0, hours = tonumber(f[3]) or 4, zones = {} }
            for i = 4, #f do wild.zones[#wild.zones + 1] = f[i] end
            if frame:IsShown() then frame.Refresh() end
        elseif kind == "A" then
            pending.A = pending.A or {}
            table.insert(pending.A, { id = f[2], owner = f[3] == "1", copper = tonumber(f[4]) or 0,
                entry = tonumber(f[5]) or 0, count = tonumber(f[6]) or 0, name = f[7] or "",
                price = tonumber(f[8]) or 0 })
        elseif kind == "C" then
            pending.C = pending.C or {}
            table.insert(pending.C, { player = f[2] == "1", name = f[3], amount = tonumber(f[4]) or 0 })
        elseif kind == "L" then
            pending.L = pending.L or {}
            table.insert(pending.L, { kind = f[2], name = f[3], amount = tonumber(f[4]) or 0, count = tonumber(f[5]) or 0,
                                      zone = f[6] or "", time = tonumber(f[7]) or 0 })
        elseif kind == "T" then
            pending.T = pending.T or {}
            table.insert(pending.T, { cat = tonumber(f[2]), name = f[3], value = tonumber(f[4]) or 0 })
        elseif kind == "P" then
            if panes.Bounties then panes.Bounties.name:SetText("") end
        end
    end)
end

function EsteriaCrime_Toggle(tab)
    if not frame then build() end
    if tab then current = tab end
    if frame:IsShown() and not tab then frame:Hide() else frame:Show(); if tab then selectTab(tab) end end
end

-- Replace the stock PvP frame everywhere it opens: key binding, micro button, and NPC queue-anywhere.
TogglePVPFrame = function() EsteriaCrime_Toggle() end
if PVPParentFrame then
    PVPParentFrame:HookScript("OnShow", function(self)
        local justBG = self.justBG
        HideUIPanel(self)
        EsteriaCrime_Toggle(justBG and "Battlegrounds" or "Overview")
    end)
end

-- PvP is toggled from this window; drop the enable/disable submenu from the player's own portrait menu.
for _, menu in pairs(UnitPopupMenus) do
    for i = #menu, 1, -1 do if menu[i] == "PVP_FLAG" then table.remove(menu, i) end end
end

SLASH_ESTERIACRIME1 = "/notoriety"
SLASH_ESTERIACRIME2 = "/crime"
SlashCmdList.ESTERIACRIME = function() EsteriaCrime_Toggle() end
