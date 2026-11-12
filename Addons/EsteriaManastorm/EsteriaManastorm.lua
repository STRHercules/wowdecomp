-- Esteria Manastorm (/manastorm): the endless dungeon ported from CoA (EsteriaWoW mod-manastorm-endless).
-- The client keeps what the server sends (Extensions/Manastorm: EsteriaManastorm_* functions, MANASTORM_* events);
-- this window lays it out like Ascension's queue panel, in the Esteria style: about text, potential rewards, the four
-- Manastorm spells and the currencies on the left over the crystal scene; best depths per group size, the level
-- drop-down and Enter on the right. A run tracker and toasts follow the run. Map, boss and unlock names come from
-- EsteriaManastormData (generated).
local ES = EsteriaStyle
local DATA = EsteriaManastormData or { gadgets = {}, segments = {}, unlocks = {}, messages = {} }

local W, H, LEFT_X, LEFT_W, RIGHT_W = 800, 500, 22, 350, 220
local SLOTS, SLOT = 4, 40
local REWARD, REWARD_STEP, REWARD_ROW = 38, 46, 7          -- 7 tiles fit the 350px column; more open a drop-down
local GRID_COLS = 10
local LEVEL_ROWS, LEVEL_ROW_H = 8, 22
local PICK_COLS, PICK_W = 3, 116
local CURRENCIES = { { 1297307, "Bedlam Bullion" }, { 1297308, "Bonzo Bolt" } }
local ART = "Interface\\Manastorm\\ManastormAtlas"
local MODES = { "SOLO", "DUO", "TRIO", "GROUP", "SOLO_END_GAME", "DUO_END_GAME", "TRIO_END_GAME", "GROUP_END_GAME" }
local MODE_NAMES = { SOLO = "Solo", DUO = "Duo", TRIO = "Trio", GROUP = "Group" }
local ABOUT = "The Manastorm is an endless dungeon. Enter alone or with your group: you are sent to a random place, "
    .. "find and defeat its boss, and a portal opens deeper into the storm.\n\n"
    .. "Deeper levels bring stronger monsters and better rewards, all the way to endgame raid gear. "
    .. "Every fifth level is a checkpoint you can start from again. Monsters and items scale to your level."
local RESULTS = {
    ENTER_MANASTORM_ALREADY_ACTIVE = "You are already in the Manastorm.",
    ENTER_MANASTORM_BAD_LEVEL = "You cannot enter that depth.",
    ENTER_MANASTORM_TOO_LOW_COMPLETED_LEVEL = "Someone in your group has not reached that depth yet.",
    ENTER_MANASTORM_NOT_GROUP_LEADER = "Only the group leader can start a run.",
    ENTER_MANASTORM_BAD_GROUP_SIZE = "Your group is too large, or a member is already in a run.",
    ENTER_MANASTORM_TOO_LOW_PLAYER_LEVEL = "You must be level 10 to enter the Manastorm.",
    ENTER_MANASTORM_GROUP_TOO_LOW_PLAYER_LEVEL = "Everyone in your group must be level 10.",
    ENTER_MANASTORM_ON_FLIGHT_PATH = "You cannot enter while flying.",
    ENTER_MANASTORM_GROUP_ON_FLIGHT_PATH = "Someone in your group is on a flight path.",
    ENTER_MANASTORM_IN_BATTLEGROUND_OR_ARENA = "You cannot enter from a battleground or arena.",
    ENTER_MANASTORM_GROUP_IN_BATTLEGROUND_OR_ARENA = "Someone in your group is in a battleground or arena.",
    ENTER_MANASTORM_DUELING = "You cannot enter while dueling.",
    ENTER_MANASTORM_GROUP_DUELING = "Someone in your group is dueling.",
    ENTER_MANASTORM_COOLDOWN = "The Manastorm is still recovering from your last run.",
    ENTER_MANASTORM_GROUP_COOLDOWN = "Someone in your group has to wait before entering again.",
    ENTER_MANASTORM_UNKNOWN = "The Manastorm cannot be entered right now.",
    LEAVE_MANASTORM_NOT_ACTIVE = "You are not in the Manastorm.",
    LEAVE_MANASTORM_TRANSITION = "Wait until the stage change has finished.",
    LEAVE_MANASTORM_UNKNOWN = "You cannot leave the Manastorm right now.",
    SET_MANASTORM_LOADOUT_ACTIVE_MANASTORM = "Gadgets cannot be changed during a run.",
    SET_MANASTORM_LOADOUT_NOT_KNOWN = "You have not learned that gadget.",
    SET_MANASTORM_LOADOUT_BAD_RANK = "You know a higher rank of that gadget.",
    SET_MANASTORM_LOADOUT_ALREADY_SET = "That gadget is already in your loadout.",
}

local M = { slots = {}, levelRows = {}, rewardIcons = {}, gridIcons = {}, picks = {}, bestRows = {}, currency = {} }
EsteriaManastorm = M   -- tests and other addons
local frame, tracker, toast

-- -------------------------------------------------------------------------------------------------------------------
-- state
-- -------------------------------------------------------------------------------------------------------------------
local function available() return EsteriaManastorm_GetActive ~= nil end
local function shown(region, on) if on then region:Show() else region:Hide() end end

local function members()
    local units = { "player" }
    if GetNumRaidMembers() > 0 then
        for i = 1, GetNumRaidMembers() do
            if not UnitIsUnit("raid" .. i, "player") then units[#units + 1] = "raid" .. i end
        end
    else
        for i = 1, GetNumPartyMembers() do units[#units + 1] = "party" .. i end
    end
    return units
end

-- The server's mode: group size, and the endgame track once anyone is at the realm's top level (at least 60).
function M.Mode()
    local _, _, active = EsteriaManastorm_GetActive()
    for i, name in ipairs(MODES) do if name == active then return i end end
    local units, endgame = members(), false
    local top = math.max(60, MAX_PLAYER_LEVEL or 80)
    for _, unit in ipairs(units) do if (UnitLevel(unit) or 0) >= top then endgame = true end end
    return math.min(#units, 4) + (endgame and 4 or 0)
end

local function bestOf(depths) return depths[#depths] or 0 end

-- Each member's best cleared depth in this mode (members the server has not reported count as 0).
function M.Bests(mode)
    local bests = {}
    for _, unit in ipairs(members()) do
        local depths = EsteriaManastorm_GetCompletedDepths(mode, unit ~= "player" and UnitGUID(unit) or nil)
        bests[#bests + 1] = { unit = unit, name = UnitName(unit) or "?", best = bestOf(depths) }
    end
    return bests
end

-- Depths a run may start at: every fifth from 1 (the checkpoints) that each member has reached
-- (EsteriaWoW ManastormProgression::IsDepthEnterable).
function M.Checkpoints(mode)
    local floor
    for _, member in ipairs(M.Bests(mode)) do floor = floor and math.min(floor, member.best) or member.best end
    local list, depth = {}, 1
    while depth - 1 <= (floor or 0) do
        list[#list + 1] = depth
        depth = depth + 5
    end
    return list
end

-- The gadget you would slot from each family: its highest known rank (passive upgrades are not slotted).
function M.Gadgets()
    local best = {}
    for _, g in ipairs(DATA.gadgets) do
        local spell, family, rank, passive = g[2], g[4], g[5], g[7]
        if not passive and IsSpellKnown(spell) and (not best[family] or rank > best[family].rank) then
            best[family] = { spell = spell, family = family, rank = rank }
        end
    end
    local list = {}
    for _, g in pairs(best) do list[#list + 1] = g end
    table.sort(list, function(a, b) return a.family < b.family end)
    return list
end

local function familyOf(spell)
    for _, g in ipairs(DATA.gadgets) do if g[2] == spell then return g[4] end end
end

local function scene(segment)
    local s = DATA.segments[segment]
    if not s then return "Unknown depths", "" end
    return s[1] ~= "" and s[1] or "Unknown depths", s[2]
end

local function track(mode) return (mode or M.Mode()) > 4 and 2 or 1 end

local function timeText(seconds)
    seconds = math.max(0, math.floor(seconds))
    if seconds >= 86400 then return math.floor(seconds / 86400) .. "d " .. math.floor(seconds % 86400 / 3600) .. "h" end
    if seconds >= 3600 then return math.floor(seconds / 3600) .. "h " .. math.floor(seconds % 3600 / 60) .. "m" end
    return math.floor(seconds / 60) .. "m"
end

local function modeName(mode) return MODE_NAMES[(MODES[mode]:gsub("_END_GAME", ""))] end

local function isLeader()
    return GetNumPartyMembers() == 0 and GetNumRaidMembers() == 0 or IsPartyLeader() or IsRaidLeader()
end

-- -------------------------------------------------------------------------------------------------------------------
-- widgets and motion
-- -------------------------------------------------------------------------------------------------------------------
-- Hover and selection ease from wherever they are now (obj._hp), so quick passes never jump.
local function ease(obj, target, duration, apply)
    local from = obj._hp or 0
    ES.Tween(obj, duration or 0.16, 0, function(p) obj._hp = from + (target - from) * p; apply(obj._hp) end)
end

-- A drop-down's entrance: fade in while sliding `dy` px into place (anchor re-applied each frame).
local function dropIn(f, anchor, dy)
    f:Show()
    ES.Tween(f, 0.18, 0, function(p)
        f:SetAlpha(p)
        f:ClearAllPoints()
        anchor(f, (1 - p) * dy)
    end)
end

local function dropOut(f)
    if not f:IsShown() then return end
    ES.Tween(f, 0.12, 0, function(p) f:SetAlpha(1 - p); if p >= 1 then f:Hide() end end)
end

local function wash(parent, color, layer)
    local t = ES.Solid(parent, layer or "ARTWORK", color, 0)
    t:SetAllPoints()
    return t
end

local function flatButton(parent, width, height, text, primary)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height)
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.32, 0.22, 0.07 } or { 0.06, 0.07, 0.09 }, 0.92)
    b.bg:SetAllPoints()
    b.glow = wash(b, ES.GOLD)
    local base = primary and 0.9 or 0.5
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM, base)
    b.label = ES.Text(b, primary and 14 or 12, primary and { 1, 0.92, 0.7 } or ES.TEXT)
    b.label:SetPoint("CENTER", 0, 1)
    b.label:SetText(text)
    local function paint(v) b.glow:SetAlpha(0.15 * v); ES.Tint(b.lines, v > 0.01 and ES.GOLD or (primary and ES.GOLD or ES.GOLD_DIM), base + (1 - base) * v) end
    b:SetScript("OnEnter", function(self)
        if self:IsEnabled() == 1 then ease(self, 1, 0.16, paint) end
        if self.tip then
            GameTooltip:SetOwner(self, "ANCHOR_TOP")
            GameTooltip:SetText(self.tip[1], 1, 0.82, 0.1)
            if self.tip[2] then GameTooltip:AddLine(self.tip[2], 1, 0.45, 0.4, true) end
            GameTooltip:Show()
        end
    end)
    b:SetScript("OnLeave", function(self) ease(self, 0, 0.22, paint); GameTooltip:Hide() end)
    b:SetScript("OnMouseDown", function(self) if self:IsEnabled() == 1 then self.label:SetPoint("CENTER", 0, 0) end end)
    b:SetScript("OnMouseUp", function(self) self.label:SetPoint("CENTER", 0, 1) end)
    b.SetActive = function(self, on)
        if on then self:Enable(); self.label:SetAlpha(1); self.bg:SetAlpha(0.92)
        else self:Disable(); self.label:SetAlpha(0.4); self.bg:SetAlpha(0.5); self.glow:SetAlpha(0); self._hp = 0 end
    end
    return b
end

-- Two thin streaks run around a button's border half a lap apart, like the Paths of Fate legendary card's edge
-- shimmer. Each streak is drawn on the four sides it may cover; a side shows only its part of the streak, with the
-- alpha ramping from the tail (0) to the head (0.9). Faster while hovered; still while disabled or with reduce motion.
local STREAK, STREAK_SPEED, STREAK_HOVER = 70, 60, 170
local function perimeter(b, w, h)
    local lap = 2 * (w + h)
    -- sides in lap order: top (left to right), right (down), bottom (right to left), left (up)
    local sides = { { 0, w }, { w, w + h }, { w + h, 2 * w + h }, { 2 * w + h, lap } }
    local streaks = {}
    for k = 1, 2 do
        local pieces = {}
        for i = 1, 4 do
            local t = b:CreateTexture(nil, "OVERLAY")
            t:SetTexture(ES.WHITE)
            t:SetBlendMode("ADD")
            t:Hide()
            pieces[i] = t
        end
        streaks[k] = pieces
    end
    b.streaks = streaks
    local function place(t, side, from, to, tail)
        local a0, a1 = (from - tail) / STREAK * 0.9, (to - tail) / STREAK * 0.9   -- alpha at each end
        local s = sides[side]
        local p, q = from - s[1], to - s[1]   -- offsets along the side, in travel direction
        local c = { 1, 0.9, 0.6 }
        t:ClearAllPoints()
        if side == 1 then
            t:SetPoint("TOPLEFT", b, "TOPLEFT", p, 0); t:SetSize(q - p, 1)
            t:SetGradientAlpha("HORIZONTAL", c[1], c[2], c[3], a0, c[1], c[2], c[3], a1)
        elseif side == 2 then
            t:SetPoint("TOPRIGHT", b, "TOPRIGHT", 0, -p); t:SetSize(1, q - p)
            t:SetGradientAlpha("VERTICAL", c[1], c[2], c[3], a1, c[1], c[2], c[3], a0)
        elseif side == 3 then
            t:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -p, 0); t:SetSize(q - p, 1)
            t:SetGradientAlpha("HORIZONTAL", c[1], c[2], c[3], a1, c[1], c[2], c[3], a0)
        else
            t:SetPoint("BOTTOMLEFT", b, "BOTTOMLEFT", 0, p); t:SetSize(1, q - p)
            t:SetGradientAlpha("VERTICAL", c[1], c[2], c[3], a0, c[1], c[2], c[3], a1)
        end
        t:Show()
    end
    b.lap = 0
    b:SetScript("OnUpdate", function(self, elapsed)
        local on = self:IsEnabled() == 1 and not ES.reduceMotion
        for _, pieces in ipairs(streaks) do for _, t in ipairs(pieces) do t:Hide() end end
        if not on then return end
        self.lap = (self.lap + elapsed * ((self._hp or 0) > 0.5 and STREAK_HOVER or STREAK_SPEED)) % lap
        for k, pieces in ipairs(streaks) do
            local head = (self.lap + (k - 1) * lap / 2) % lap
            -- the streak covers [head - STREAK, head]; unwrap it so it may cross the lap's start
            for _, shift in ipairs({ 0, lap }) do
                local tail, top = head - STREAK + shift, head + shift
                for i, s in ipairs(sides) do
                    local from, to = math.max(tail, s[1]), math.min(top, s[2])
                    if to - from >= 1 then place(pieces[i], i, from, to, tail) end
                end
            end
        end
    end)
end

local function iconFrame(parent, size)
    local holder = CreateFrame("Button", nil, parent)
    holder:SetSize(size + 2, size + 2)
    holder.back = ES.Solid(holder, "BACKGROUND", { 0, 0, 0 }, 0.55)
    holder.back:SetAllPoints()
    holder.icon = holder:CreateTexture(nil, "ARTWORK")
    holder.icon:SetPoint("TOPLEFT", 1, -1)
    holder.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    holder.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    holder.lines = ES.Border(holder, ES.GOLD_DIM, 0.8)
    holder.shine = holder:CreateTexture(nil, "OVERLAY")
    holder.shine:SetTexture(ES.WHITE)
    holder.shine:SetAllPoints(holder.icon)
    holder.shine:SetBlendMode("ADD")
    holder.shine:SetVertexColor(1, 0.9, 0.6, 0)
    return holder
end

-- An icon that brightens and lifts its border on hover; tip(self) fills the tooltip (false: no tooltip).
local function hoverIcon(holder, tip)
    local function paint(v) holder.shine:SetAlpha(0.14 * v); ES.Tint(holder.lines, v > 0.01 and ES.GOLD or ES.GOLD_DIM, 0.8 + 0.2 * v) end
    holder:SetScript("OnEnter", function(self)
        ease(self, 1, 0.14, paint)
        if tip(self) ~= false then GameTooltip:Show() end
    end)
    holder:SetScript("OnLeave", function(self) ease(self, 0, 0.2, paint); GameTooltip:Hide() end)
end

-- A section heading with a gold rule under it; the rule grows in when the window opens.
local function section(parent, text, anchorTo, y)
    local title = ES.Text(parent, 15, ES.GOLD)
    title:SetText(text)
    if anchorTo then title:SetPoint("TOPLEFT", anchorTo, "BOTTOMLEFT", 0, y) else title:SetPoint("TOPLEFT", LEFT_X, y) end
    local rule = ES.Solid(parent, "ARTWORK", ES.GOLD, 0.55)
    rule:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -4)
    rule:SetSize(LEFT_W, 1)
    local fade = parent:CreateTexture(nil, "ARTWORK")
    fade:SetTexture(ES.WHITE)
    fade:SetGradientAlpha("HORIZONTAL", ES.GOLD[1], ES.GOLD[2], ES.GOLD[3], 0.55, ES.GOLD[1], ES.GOLD[2], ES.GOLD[3], 0)
    fade:SetPoint("LEFT", rule, "RIGHT")
    fade:SetSize(60, 1)
    M.rules = M.rules or {}
    M.rules[#M.rules + 1] = rule
    return title, rule
end

-- -------------------------------------------------------------------------------------------------------------------
-- window
-- -------------------------------------------------------------------------------------------------------------------
local refresh

local function buildBackground()
    -- the scene is 788x512 of the 1024x512 atlas; it fills the window, still (no drift or glow: it must stay readable)
    local art = frame:CreateTexture(nil, "BACKGROUND", nil, 1)
    art:SetTexture(ART)
    art:SetPoint("TOPLEFT", 1, -1)
    art:SetPoint("BOTTOMRIGHT", -1, 1)
    art:SetTexCoord(0.012, 0.757, 0.02, 0.96)
    -- darken behind the text column and, more lightly, behind the right column
    local left = frame:CreateTexture(nil, "BORDER", nil, 1)
    left:SetTexture(ES.WHITE)
    left:SetGradientAlpha("HORIZONTAL", 0.02, 0.025, 0.04, 0.94, 0.02, 0.025, 0.04, 0)
    left:SetPoint("TOPLEFT", 1, -1); left:SetPoint("BOTTOMLEFT", 1, 1); left:SetWidth(LEFT_X + LEFT_W + 140)
    local right = frame:CreateTexture(nil, "BORDER", nil, 1)
    right:SetTexture(ES.WHITE)
    right:SetGradientAlpha("HORIZONTAL", 0.02, 0.025, 0.04, 0, 0.02, 0.025, 0.04, 0.7)
    right:SetPoint("TOPRIGHT", -1, -1); right:SetPoint("BOTTOMRIGHT", -1, 1); right:SetWidth(RIGHT_W + 90)
    local bottom = frame:CreateTexture(nil, "BORDER", nil, 1)
    bottom:SetTexture(ES.WHITE)
    bottom:SetGradientAlpha("VERTICAL", 0.02, 0.025, 0.04, 0.85, 0.02, 0.025, 0.04, 0)
    bottom:SetPoint("BOTTOMLEFT", 1, 1); bottom:SetPoint("BOTTOMRIGHT", -1, 1); bottom:SetHeight(90)
end

local function closeButton()
    local b = CreateFrame("Button", nil, frame)
    b:SetSize(26, 26)
    b:SetPoint("TOPRIGHT", -8, -8)
    b.glow = wash(b, ES.GOLD, "BACKGROUND")
    b.label = ES.Text(b, 15, ES.MUTED)
    b.label:SetPoint("CENTER")
    b.label:SetText("X")
    local function paint(v)
        b.glow:SetAlpha(0.14 * v)
        b.label:SetTextColor(ES.MUTED[1] + (ES.GOLD[1] - ES.MUTED[1]) * v, ES.MUTED[2] + (ES.GOLD[2] - ES.MUTED[2]) * v,
            ES.MUTED[3] + (ES.GOLD[3] - ES.MUTED[3]) * v)
    end
    b:SetScript("OnEnter", function(self) ease(self, 1, 0.14, paint) end)
    b:SetScript("OnLeave", function(self) ease(self, 0, 0.2, paint) end)
    b:SetScript("OnClick", function() frame:Hide() end)
end

-- Left column ---------------------------------------------------------------------------------------------------------
local function buildAbout()
    local title = ES.Text(frame, 24, ES.GOLD)
    title:SetPoint("TOPLEFT", LEFT_X, -16)
    title:SetText("The Manastorm")
    local rule = ES.Solid(frame, "ARTWORK", ES.GOLD, 0.55)
    rule:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -5)
    rule:SetSize(LEFT_W, 1)
    M.rules = { rule }
    -- outside a run: what the Manastorm is; in a run: where you are
    M.about = ES.Text(frame, 11, ES.TEXT)
    M.about:SetPoint("TOPLEFT", rule, "BOTTOMLEFT", 0, -9)
    M.about:SetWidth(LEFT_W)
    M.about:SetJustifyH("LEFT")
    M.about:SetSpacing(2)
    M.about:SetText(ABOUT)
    local run = CreateFrame("Frame", nil, frame)
    run:SetPoint("TOPLEFT", rule, "BOTTOMLEFT", 0, -9)
    run:SetSize(LEFT_W, 96)
    M.runDepth = ES.Text(run, 30, { 1, 0.95, 0.8 })
    M.runDepth:SetPoint("TOPLEFT")
    M.runScene = ES.Text(run, 14, ES.TEXT)
    M.runScene:SetPoint("TOPLEFT", M.runDepth, "TOPRIGHT", 14, -1)
    M.runScene:SetPoint("RIGHT", run, "RIGHT")
    M.runScene:SetJustifyH("LEFT")
    M.runBoss = ES.Text(run, 11, ES.MUTED)
    M.runBoss:SetPoint("TOPLEFT", M.runScene, "BOTTOMLEFT", 0, -4)
    M.runBoss:SetPoint("RIGHT", run, "RIGHT")
    M.runBoss:SetJustifyH("LEFT")
    M.runLink = ES.Text(run, 12, ES.BAD)
    M.runLink:SetPoint("TOPLEFT", 0, -44)
    M.runCache = iconFrame(run, 26)
    M.runCache:SetPoint("TOPLEFT", 0, -64)
    hoverIcon(M.runCache, function(self)
        if not self.item then return false end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink("item:" .. self.item)
    end)
    M.runCacheText = ES.Text(run, 11, ES.TEXT)
    M.runCacheText:SetPoint("LEFT", M.runCache, "RIGHT", 8, 0)
    M.run = run
    return rule
end

local function rewardTile(parent, size)
    local r = iconFrame(parent, size)
    local strip = ES.Solid(r, "OVERLAY", { 0, 0, 0 }, 0.65)
    strip:SetPoint("BOTTOMLEFT", 1, 1); strip:SetPoint("BOTTOMRIGHT", -1, 1); strip:SetHeight(11)
    r.strip = strip
    r.count = ES.Text(r, 9, ES.TEXT)
    r.count:SetPoint("BOTTOMRIGHT", -2, 2)
    r.lock = ES.Text(r, 9, ES.BAD)
    r.lock:SetPoint("CENTER", 0, 4)
    hoverIcon(r, function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetHyperlink("item:" .. self.item)
        if self.max and self.max > 0 then
            GameTooltip:AddLine(string.format("Earned %d of %d this week.", self.earned, self.max), 0.75, 0.75, 0.75)
        end
        if self.locked then GameTooltip:AddLine("Limit reached; available again in " .. timeText(self.locked) .. ".", 1, 0.4, 0.35, true) end
    end)
    return r
end

local function buildRewards(anchor)
    local title = section(frame, "Potential Rewards", anchor, -16)
    M.rewardsTitle = title
    M.rewardHint = ES.Text(frame, 11, ES.MUTED)
    M.rewardHint:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -12)
    M.rewardHint:SetText("Rewards appear once the realm has told us about them.")
    M.rewardCount = ES.Text(frame, 10, ES.MUTED)
    M.rewardCount:SetPoint("BOTTOMLEFT", title, "BOTTOMRIGHT", 8, 1)
    for i = 1, REWARD_ROW do
        local r = rewardTile(frame, REWARD)
        r:SetPoint("TOPLEFT", title, "BOTTOMLEFT", (i - 1) * REWARD_STEP, -11)
        M.rewardIcons[i] = r
    end
    -- the last tile can instead be "+N": it drops the full grid down over the window
    local more = CreateFrame("Button", nil, frame)
    more:SetSize(REWARD + 2, REWARD + 2)
    more:SetPoint("TOPLEFT", title, "BOTTOMLEFT", (REWARD_ROW - 1) * REWARD_STEP, -11)
    more.bg = ES.Solid(more, "BACKGROUND", { 0.06, 0.07, 0.09 }, 0.9)
    more.bg:SetAllPoints()
    more.glow = wash(more, ES.GOLD)
    more.lines = ES.Border(more, ES.GOLD_DIM, 0.8)
    more.label = ES.Text(more, 13, ES.GOLD)
    more.label:SetPoint("CENTER")
    local function paint(v) more.glow:SetAlpha(0.15 * v); ES.Tint(more.lines, v > 0.01 and ES.GOLD or ES.GOLD_DIM, 0.8 + 0.2 * v) end
    more:SetScript("OnEnter", function(self)
        ease(self, 1, 0.14, paint)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText("All potential rewards", 1, 0.82, 0.1)
        GameTooltip:Show()
    end)
    more:SetScript("OnLeave", function(self) ease(self, 0, 0.2, paint); GameTooltip:Hide() end)
    more:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); M.ToggleRewardGrid() end)
    M.rewardMore = more
    return title
end

local function closeAll(except)
    for _, f in ipairs({ M.grid, M.levelList, M.picker }) do if f and f ~= except then dropOut(f) end end
end

function M.ToggleRewardGrid()
    local g = M.grid
    if g and g:IsShown() then dropOut(g); return end
    closeAll(g)
    if not g then
        g = CreateFrame("Frame", nil, frame)
        g:SetFrameStrata("DIALOG")
        g:EnableMouse(true)
        ES.Solid(g, "BACKGROUND", { 0.03, 0.035, 0.05 }, 0.97):SetAllPoints()
        ES.Border(g, ES.GOLD, 0.8)
        g.title = ES.Text(g, 10, ES.GOLD_DIM)
        g.title:SetPoint("TOPLEFT", 12, -10)
        M.grid = g
    end
    local list = M.rewardList or {}
    for i, r in ipairs(list) do
        local tile = M.gridIcons[i]
        if not tile then
            tile = rewardTile(g, REWARD)
            tile:SetPoint("TOPLEFT", 12 + (i - 1) % GRID_COLS * REWARD_STEP, -28 - math.floor((i - 1) / GRID_COLS) * REWARD_STEP)
            M.gridIcons[i] = tile
        end
        M.PaintReward(tile, r)
        tile:Show()
        ES.Tween(tile, 0.22, 0.012 * i, function(p) tile:SetAlpha(p) end)
    end
    for i = #list + 1, #M.gridIcons do M.gridIcons[i]:Hide() end
    g.title:SetText(string.upper(#list .. " potential rewards"))
    g:SetSize(24 + GRID_COLS * REWARD_STEP - 8, 40 + math.ceil(math.max(1, #list) / GRID_COLS) * REWARD_STEP)
    dropIn(g, function(f, dy) f:SetPoint("TOPLEFT", M.rewardsTitle, "BOTTOMLEFT", -12, -4 + dy) end, 8)
end

-- The picker opens upward from the slots, three columns wide (20 gadget families fit).
local function openPicker(slot)
    local p = M.picker
    if not p then
        p = CreateFrame("Frame", nil, frame)
        p:SetFrameStrata("DIALOG")
        p:EnableMouse(true)
        ES.Solid(p, "BACKGROUND", { 0.03, 0.035, 0.05 }, 0.97):SetAllPoints()
        ES.Border(p, ES.GOLD, 0.8)
        p.title = ES.Text(p, 10, ES.GOLD_DIM)
        p.title:SetPoint("TOPLEFT", 10, -10)
        p.empty = ES.Text(p, 11, ES.MUTED)
        p.empty:SetPoint("TOPLEFT", 10, -30)
        p.empty:SetText("No gadgets learned yet. Cogsley sells them inside the Manastorm.")
        M.picker = p
    end
    closeAll(p)
    local taken = {}
    for i = 1, SLOTS do
        local spell = select(i, EsteriaManastorm_GetLoadout())
        if i ~= slot and spell ~= 0 then taken[familyOf(spell) or 0] = true end
    end
    local list = {}
    for _, g in ipairs(M.Gadgets()) do if not taken[g.family] then list[#list + 1] = g end end
    for i, g in ipairs(list) do
        local row = M.picks[i]
        if not row then
            row = CreateFrame("Button", nil, p)
            row:SetSize(PICK_W - 4, 26)
            row:SetPoint("TOPLEFT", 8 + (i - 1) % PICK_COLS * PICK_W, -26 - math.floor((i - 1) / PICK_COLS) * 28)
            row.wash = wash(row, ES.GOLD, "BACKGROUND")
            row.icon = row:CreateTexture(nil, "ARTWORK")
            row.icon:SetSize(20, 20)
            row.icon:SetPoint("LEFT", 3, 0)
            row.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
            row.name = ES.Text(row, 10, ES.TEXT)
            row.name:SetPoint("LEFT", row.icon, "RIGHT", 6, 0)
            row.name:SetPoint("RIGHT", row, "RIGHT", -2, 0)
            row.name:SetJustifyH("LEFT")
            row.name:SetHeight(12)
            row:SetScript("OnEnter", function(self)
                ease(self, 1, 0.12, function(v) self.wash:SetAlpha(0.12 * v) end)
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
                GameTooltip:SetHyperlink("spell:" .. self.spell)
                GameTooltip:Show()
            end)
            row:SetScript("OnLeave", function(self) ease(self, 0, 0.18, function(v) self.wash:SetAlpha(0.12 * v) end); GameTooltip:Hide() end)
            M.picks[i] = row
        end
        local name, rank, icon = GetSpellInfo(g.spell)
        row.spell = g.spell
        row.icon:SetTexture(icon)
        row.name:SetText((name or ("Gadget " .. g.spell)) .. " |cff8a8a8a" .. (rank and rank ~= "" and rank:gsub("Rank ", "R") or "R" .. g.rank) .. "|r")
        row:SetScript("OnClick", function()
            PlaySound("igMainMenuOptionCheckBoxOn")
            EsteriaManastorm_SetLoadoutSlot(slot, g.spell)
            M.picker:Hide()
        end)
        row:Show()
    end
    for i = #list + 1, #M.picks do M.picks[i]:Hide() end
    p.title:SetText("MANASTORM SPELL FOR SLOT " .. slot)
    shown(p.empty, #list == 0)
    p:SetSize(16 + PICK_COLS * PICK_W, (#list == 0 and 52) or (34 + math.ceil(#list / PICK_COLS) * 28))
    dropIn(p, function(f, dy) f:SetPoint("BOTTOMLEFT", M.slots[1], "TOPLEFT", -8, 8 - dy) end, -8)
end

local function buildLoadout(anchor)
    local title = section(frame, "Manastorm Spells", anchor, -66)
    M.loadoutTitle = title
    M.loadoutAbout = ES.Text(frame, 11, ES.MUTED)
    M.loadoutAbout:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -11)
    M.loadoutAbout:SetWidth(LEFT_W)
    M.loadoutAbout:SetJustifyH("LEFT")
    for i = 1, SLOTS do
        local s = iconFrame(frame, SLOT)
        s:SetPoint("TOPLEFT", M.loadoutAbout, "BOTTOMLEFT", (i - 1) * (SLOT + 10), -8)
        s.plus = ES.Text(s, 22, ES.GOOD)
        s.plus:SetPoint("CENTER", 0, 1)
        s.plus:SetText("+")
        s:RegisterForClicks("LeftButtonUp", "RightButtonUp")
        s:SetScript("OnClick", function(self, button)
            if EsteriaManastorm_GetActive() then
                UIErrorsFrame:AddMessage(RESULTS.SET_MANASTORM_LOADOUT_ACTIVE_MANASTORM, 1, 0.1, 0.1)
            elseif M.picker and M.picker:IsShown() and M.pickerSlot == i then dropOut(M.picker)
            else M.pickerSlot = i; PlaySound("igMainMenuOptionCheckBoxOn"); openPicker(i) end
        end)
        hoverIcon(s, function(self)
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            if self.spell and self.spell ~= 0 then GameTooltip:SetHyperlink("spell:" .. self.spell)
            else GameTooltip:SetText("Empty Manastorm spell slot", 1, 0.82, 0.1); GameTooltip:AddLine("Click to choose a gadget.", 1, 1, 1) end
        end)
        M.slots[i] = s
    end
    -- the slot names under the row: what is equipped, at a glance
    M.slotNames = ES.Text(frame, 10, ES.MUTED)
    M.slotNames:SetPoint("TOPLEFT", M.slots[1], "BOTTOMLEFT", 0, -6)
    M.slotNames:SetWidth(LEFT_W)
    M.slotNames:SetJustifyH("LEFT")
end

local function buildCurrencies()
    local bar = CreateFrame("Frame", nil, frame)
    bar:SetPoint("BOTTOMLEFT", LEFT_X - 6, 14)
    bar:SetHeight(24)
    ES.Solid(bar, "BACKGROUND", { 0.03, 0.035, 0.05 }, 0.85):SetAllPoints()
    ES.Border(bar, ES.GOLD_DIM, 0.6)
    local x = 10
    for i, c in ipairs(CURRENCIES) do
        local b = CreateFrame("Button", nil, bar)
        b:SetSize(150, 24)
        b:SetPoint("LEFT", x, 0)
        b.text = ES.Text(b, 11, ES.TEXT)
        b.text:SetPoint("LEFT")
        b.item = c[1]
        b:SetScript("OnEnter", function(self)
            GameTooltip:SetOwner(self, "ANCHOR_TOP")
            GameTooltip:SetHyperlink("item:" .. self.item)
            GameTooltip:Show()
        end)
        b:SetScript("OnLeave", function() GameTooltip:Hide() end)
        M.currency[i] = b
        x = x + 150
    end
    bar:SetWidth(x)
    M.currencyBar = bar
end

-- Right column --------------------------------------------------------------------------------------------------------
local function buildBests()
    M.mode = ES.Text(frame, 10, ES.GOLD_DIM)
    M.mode:SetPoint("TOPRIGHT", -46, -18)
    M.mode:SetJustifyH("RIGHT")
    for k = 1, 4 do
        local row = CreateFrame("Button", nil, frame)
        row:SetSize(RIGHT_W, 54)
        row:SetPoint("TOPRIGHT", -24, -38 - (k - 1) * 60)
        row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
        row.accent:SetPoint("TOPRIGHT", 8, -6); row.accent:SetPoint("BOTTOMRIGHT", 8, 6); row.accent:SetWidth(2)
        row.label = ES.Text(row, 14, ES.GOLD)
        row.label:SetPoint("TOPRIGHT")
        row.label:SetText("Best " .. MODE_NAMES[MODES[k]])
        row.value = ES.Text(row, 26, { 1, 0.96, 0.86 })
        row.value:SetPoint("TOPRIGHT", row.label, "BOTTOMRIGHT", 0, -3)
        row.k = k
        row:SetScript("OnEnter", function(self)
            local mode = self.k + (M.Mode() > 4 and 4 or 0)
            GameTooltip:SetOwner(self, "ANCHOR_LEFT")
            GameTooltip:SetText("Best " .. modeName(mode) .. (mode > 4 and " (endgame)" or ""), 1, 0.82, 0.1)
            for _, m in ipairs(M.Bests(mode)) do GameTooltip:AddDoubleLine(m.name, m.best, 1, 1, 1, 1, 0.9, 0.6) end
            GameTooltip:AddLine("Depth cleared in a run of this size.", 0.6, 0.6, 0.6, true)
            GameTooltip:Show()
        end)
        row:SetScript("OnLeave", function() GameTooltip:Hide() end)
        M.bestRows[k] = row
    end
    -- the group's own bests in the current mode, small, under the bests
    M.group = ES.Text(frame, 10, ES.MUTED)
    M.group:SetPoint("TOPRIGHT", M.bestRows[4], "BOTTOMRIGHT", 0, -2)
    M.group:SetJustifyH("RIGHT")
    M.group:SetWidth(RIGHT_W)
end

local function levelText(depth) return "Level " .. depth end

function M.ToggleLevels()
    local l = M.levelList
    if l:IsShown() then dropOut(l); return end
    closeAll(l)
    PlaySound("igMainMenuOptionCheckBoxOn")
    M.levelOffset = 0
    M.PaintLevels()
    dropIn(l, function(f, dy) f:SetPoint("TOPRIGHT", M.dropdown, "BOTTOMRIGHT", 0, -2 + dy) end, 6)
end

function M.PaintLevels()
    local list = M.levels or {}
    local max = math.max(0, #list - LEVEL_ROWS)
    M.levelOffset = math.max(0, math.min(M.levelOffset or 0, max))
    for i, row in ipairs(M.levelRows) do
        local depth = list[M.levelOffset + i]
        row.depth = depth
        shown(row, depth ~= nil)
        if depth then
            row.text:SetText(levelText(depth))
            row.check:SetAlpha(depth == M.selected and 1 or 0)
        end
    end
    M.levelList:SetHeight(8 + math.min(LEVEL_ROWS, math.max(1, #list)) * LEVEL_ROW_H)
    M.levelMore:SetText(max > 0 and ((M.levelOffset > 0 and "^ " or "") .. "scroll" .. (M.levelOffset < max and " v" or "")) or "")
end

local function buildEnter()
    local dd = CreateFrame("Button", nil, frame)
    dd:SetSize(RIGHT_W, 28)
    dd:SetPoint("BOTTOMRIGHT", -24, 62)
    dd.bg = ES.Solid(dd, "BACKGROUND", { 0.03, 0.035, 0.05 }, 0.88)
    dd.bg:SetAllPoints()
    dd.glow = wash(dd, ES.GOLD)
    dd.lines = ES.Border(dd, ES.GOLD_DIM, 0.7)
    dd.text = ES.Text(dd, 12, ES.TEXT)
    dd.text:SetPoint("LEFT", 10, 0)
    dd.arrow = ES.Text(dd, 12, ES.GOLD)
    dd.arrow:SetPoint("RIGHT", -10, 0)
    dd.arrow:SetText("v")
    local function paint(v) dd.glow:SetAlpha(0.1 * v); ES.Tint(dd.lines, v > 0.01 and ES.GOLD or ES.GOLD_DIM, 0.7 + 0.3 * v) end
    dd:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then ease(self, 1, 0.14, paint) end end)
    dd:SetScript("OnLeave", function(self) ease(self, 0, 0.2, paint) end)
    dd:SetScript("OnClick", function() M.ToggleLevels() end)
    M.dropdown = dd

    local l = CreateFrame("Frame", nil, frame)
    l:SetFrameStrata("DIALOG")
    l:SetWidth(RIGHT_W)
    l:EnableMouse(true)
    l:EnableMouseWheel(true)
    ES.Solid(l, "BACKGROUND", { 0.03, 0.035, 0.05 }, 0.97):SetAllPoints()
    ES.Border(l, ES.GOLD, 0.8)
    l:SetScript("OnMouseWheel", function(_, delta) M.levelOffset = (M.levelOffset or 0) - delta; M.PaintLevels() end)
    for i = 1, LEVEL_ROWS do
        local row = CreateFrame("Button", nil, l)
        row:SetSize(RIGHT_W - 8, LEVEL_ROW_H)
        row:SetPoint("TOPLEFT", 4, -4 - (i - 1) * LEVEL_ROW_H)
        row.wash = wash(row, ES.GOLD, "BACKGROUND")
        row.check = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
        row.check:SetPoint("LEFT", 2, 0); row.check:SetSize(2, LEVEL_ROW_H - 8)
        row.text = ES.Text(row, 12, ES.TEXT)
        row.text:SetPoint("LEFT", 10, 0)
        row:SetScript("OnEnter", function(self) ease(self, 1, 0.1, function(v) self.wash:SetAlpha(0.12 * v) end) end)
        row:SetScript("OnLeave", function(self) ease(self, 0, 0.16, function(v) self.wash:SetAlpha(0.12 * v) end) end)
        row:SetScript("OnClick", function(self)
            PlaySound("igMainMenuOptionCheckBoxOn")
            M.selected = self.depth
            dropOut(M.levelList)
            refresh()
        end)
        M.levelRows[i] = row
    end
    M.levelMore = ES.Text(l, 9, ES.MUTED)
    M.levelMore:SetPoint("BOTTOMRIGHT", -6, 3)
    l:Hide()
    M.levelList = l

    M.enter = flatButton(frame, RIGHT_W, 34, "", true)
    M.enter:SetPoint("BOTTOMRIGHT", -24, 20)
    perimeter(M.enter, RIGHT_W, 34)
    M.enter:SetScript("OnClick", function()
        if EsteriaManastorm_GetActive() then
            StaticPopup_Show("ESTERIA_MANASTORM_LEAVE", M.Checkpoint())
        elseif M.selected and EsteriaManastorm_Enter(M.selected) then
            PlaySound("igMainMenuOpen"); M.enter:SetActive(false)
        end
    end)
    M.leave = M.enter   -- in a run the same button leaves
    M.depthHint = ES.Text(frame, 10, ES.MUTED)
    M.depthHint:SetPoint("BOTTOMRIGHT", M.dropdown, "TOPRIGHT", 0, 6)
    M.depthHint:SetJustifyH("RIGHT")
end

local function build()
    frame = CreateFrame("Frame", "EsteriaManastormFrame", UIParent)
    frame:SetSize(W, H)
    frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH")
    frame:SetToplevel(true)
    frame:EnableMouse(true)
    frame:SetMovable(true)
    frame:SetClampedToScreen(true)
    frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:SetScript("OnMouseDown", function() closeAll() end)
    frame:Hide()
    tinsert(UISpecialFrames, "EsteriaManastormFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 1):SetAllPoints()
    buildBackground()
    ES.Border(frame, ES.GOLD, 0.7)
    closeButton()

    local aboutRule = buildAbout()
    local rewards = buildRewards(M.about)
    buildLoadout(rewards)
    buildCurrencies()
    buildBests()
    buildEnter()

    frame:SetScript("OnShow", function(self)
        PlaySound("igCharacterInfoOpen")
        M.selected, M.rewardKey = nil, nil
        refresh()
        ES.Tween("manastorm-open", 0.22, 0, function(p) self:SetAlpha(p) end)
        -- the rules draw across, then the right column's numbers settle in one by one
        for i, rule in ipairs(M.rules) do
            ES.Tween(rule, 0.45, 0.05 * i, function(p) rule:SetWidth(math.max(1, LEFT_W * p)) end)
        end
        for i, row in ipairs(M.bestRows) do
            ES.Tween(row, 0.3, 0.06 * i, function(p) row:SetAlpha(p) end)
        end
    end)
    frame:SetScript("OnHide", function()
        PlaySound("igCharacterInfoClose")
        for _, f in ipairs({ M.grid, M.levelList, M.picker }) do if f then f:Hide() end end
    end)
    M.aboutRule = aboutRule
end

function M.Checkpoint()
    local depth = EsteriaManastorm_GetActive() or 1
    return depth - (depth - 1) % 5
end

-- Paint ---------------------------------------------------------------------------------------------------------------
function M.PaintReward(icon, r)
    icon.item, icon.earned, icon.max = r.item, r.earned, r.max
    icon.icon:SetTexture(GetItemIcon(r.item) or "Interface\\Icons\\INV_Misc_QuestionMark")
    local locked = r.action == 2 and r.secondsLeft - (GetTime() - (M.rewardsAt or GetTime())) or nil
    icon.locked = locked and locked > 0 and locked or nil
    icon.icon:SetDesaturated(icon.locked and 1 or nil)
    icon.lock:SetText(icon.locked and timeText(icon.locked) or "")
    icon.count:SetText(r.max > 0 and (r.earned .. "/" .. r.max) or "")
    shown(icon.strip, r.max > 0)
end

local function paintRewards()
    local list = {}
    for _, r in ipairs(EsteriaManastorm_GetRewards()) do if r.action ~= 1 then list[#list + 1] = r end end
    M.rewardList = list
    local overflow = #list > REWARD_ROW
    local count = overflow and REWARD_ROW - 1 or #list
    local fresh = M.rewardKey ~= #list
    M.rewardKey = #list
    for i, icon in ipairs(M.rewardIcons) do
        local r = i <= count and list[i]
        shown(icon, r and true or false)
        if r then
            M.PaintReward(icon, r)
            if fresh then ES.Tween(icon, 0.25, 0.04 * i, function(p) icon:SetAlpha(p) end) end
        end
    end
    shown(M.rewardMore, overflow)
    M.rewardMore.label:SetText("+" .. (#list - count))
    M.rewardCount:SetText(#list > 0 and (#list .. " available") or "")
    shown(M.rewardHint, #list == 0)
    if M.grid and M.grid:IsShown() then M.ToggleRewardGrid(); M.ToggleRewardGrid() end
end

local function paintLoadout(active)
    local enabled = EsteriaManastorm_GetConfig("CONFIG_MANASTORM_LOADOUTS_ENABLED") ~= false
    for _, r in ipairs({ M.loadoutTitle, M.loadoutAbout, M.slotNames }) do shown(r, enabled) end
    for _, s in ipairs(M.slots) do shown(s, enabled) end
    if not enabled then return end
    local any = #M.Gadgets() > 0
    M.loadoutAbout:SetText(active and "Locked while you are in the Manastorm."
        or any and "Choose the gadgets you can use inside the Manastorm."
        or "Choose the gadgets you can use inside the Manastorm. Cogsley sells them during a run.")
    local names = {}
    for i, s in ipairs(M.slots) do
        local spell = select(i, EsteriaManastorm_GetLoadout())
        s.spell = spell
        local name, _, icon = GetSpellInfo(spell ~= 0 and spell or 0)
        s.icon:SetTexture(spell ~= 0 and icon or nil)
        s.icon:SetDesaturated(active and 1 or nil)
        shown(s.plus, spell == 0)
        s.plus:SetAlpha(active and 0.35 or 1)
        if spell ~= 0 then names[#names + 1] = name or ("Gadget " .. spell) end
    end
    M.slotNames:SetText(#names > 0 and table.concat(names, "  ·  ") or "")
end

local function paintBests(mode)
    local base = mode > 4 and 4 or 0
    for k, row in ipairs(M.bestRows) do
        row.value:SetText(bestOf(EsteriaManastorm_GetCompletedDepths(base + k)))
        local on = base + k == mode
        ES.Tween(row.accent, 0.25, 0, function(p) row.accent:SetAlpha((on and p or (1 - p) * (row.accent._on and 1 or 0)) * 0.9) end)
        row.accent._on = on
        row.label:SetTextColor(unpack(on and ES.GOLD or ES.GOLD_DIM))
    end
    M.best = M.bestRows[mode - base].value
    local others = {}
    for i, m in ipairs(M.Bests(mode)) do if i > 1 then others[#others + 1] = m.name .. " |cffe8c46b" .. m.best .. "|r" end end
    M.group:SetText(#others > 0 and ("Group: " .. table.concat(others, ",  ")) or "")
end

local function paintEnter(mode, depth)
    local leader = isLeader()
    if depth then
        M.dropdown:Disable()
        M.dropdown.text:SetText("In a run: level " .. depth)
        M.dropdown.text:SetAlpha(0.6); M.dropdown.arrow:SetAlpha(0)
        M.enter.label:SetText("Leave the Manastorm")
        M.enter:SetActive(true)
        M.depthHint:SetText("Leaving returns you to level " .. M.Checkpoint() .. " next time")
        return
    end
    M.levels = {}
    local list = M.Checkpoints(mode)
    for i = #list, 1, -1 do M.levels[#M.levels + 1] = list[i] end   -- deepest first, as in the original
    if not M.selected or M.selected > list[#list] then M.selected = list[#list] end
    M.dropdown:Enable()
    M.dropdown.text:SetText("Level " .. M.selected); M.dropdown.text:SetAlpha(1); M.dropdown.arrow:SetAlpha(1)
    M.enter.label:SetText("Enter " .. modeName(mode) .. " Manastorm")
    M.enter:SetActive(leader and true or false)
    M.enter.tip = not leader and { "Cannot enter the Manastorm", "Only the group leader can start a run." } or nil
    M.depthHint:SetText(leader and (#list .. (#list == 1 and " checkpoint" or " checkpoints") .. " open")
        or "Only the group leader can start a run")
    if M.levelList:IsShown() then M.PaintLevels() end
end

local function paintRun(depth, segment, caches, chance, item)
    local map, boss = scene(segment)
    M.runDepth:SetText(depth)
    M.runScene:SetText(map)
    M.runBoss:SetText(boss ~= "" and ("Objective: " .. boss) or "Clear the way forward")
    local link = EsteriaManastorm_GetChaoticLink()
    M.runLink:SetText(link > 0 and ("Chaotic Link x" .. link .. "  |cff999999enemies grow stronger|r") or "")
    M.runCache.item = item ~= 0 and item or nil
    M.runCache.icon:SetTexture(item ~= 0 and GetItemIcon(item) or "Interface\\Icons\\INV_Misc_QuestionMark")
    shown(M.runCache, item ~= 0)
    local text = caches .. (caches == 1 and " guaranteed cache" or " guaranteed caches")
    if chance > 0 then text = text .. string.format("  +%d%% chance of another", math.floor(chance + 0.5)) end
    M.runCacheText:SetText(text)
end

local function paintCurrencies()
    for i, b in ipairs(M.currency) do
        local id, name = CURRENCIES[i][1], CURRENCIES[i][2]
        local icon = GetItemIcon(id)
        b.text:SetText(name .. ": |cffffffff" .. (GetItemCount(id, true) or 0) .. "|r" .. (icon and ("  |T" .. icon .. ":14:14|t") or ""))
    end
end

refresh = function()
    if not frame or not frame:IsShown() then return end
    local mode = M.Mode()
    local depth, segment, _, caches, chance, item = EsteriaManastorm_GetActive()
    M.mode:SetText(string.upper(modeName(mode) .. (mode > 4 and "  ·  ENDGAME" or "")))
    paintBests(mode)
    shown(M.run, depth ~= nil)
    shown(M.about, depth == nil)
    if depth then paintRun(depth, segment, caches, chance, item) end
    paintEnter(mode, depth)
    paintLoadout(depth ~= nil)
    paintRewards()
    paintCurrencies()
end
M.Refresh = function() refresh() end

-- -------------------------------------------------------------------------------------------------------------------
-- tracker (while in a run) and toasts
-- -------------------------------------------------------------------------------------------------------------------
local function buildTracker()
    tracker = CreateFrame("Button", "EsteriaManastormTracker", UIParent)
    tracker:SetSize(230, 74)
    tracker:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -20, -230)
    ES.Solid(tracker, "BACKGROUND", ES.INK, 0.85):SetAllPoints()
    ES.Border(tracker, ES.GOLD_DIM)
    tracker.title = ES.Text(tracker, 10, ES.GOLD_DIM)
    tracker.title:SetPoint("TOPLEFT", 10, -8)
    tracker.depth = ES.Text(tracker, 16, { 1, 0.95, 0.8 })
    tracker.depth:SetPoint("TOPRIGHT", -10, -6)
    tracker.boss = ES.Text(tracker, 11, ES.TEXT)
    tracker.boss:SetPoint("TOPLEFT", 10, -28)
    tracker.boss:SetPoint("RIGHT", -10, 0)
    tracker.boss:SetJustifyH("LEFT")
    tracker.extra = ES.Text(tracker, 10, ES.MUTED)
    tracker.extra:SetPoint("TOPLEFT", tracker.boss, "BOTTOMLEFT", 0, -6)
    tracker:SetScript("OnClick", function() EsteriaManastorm_Toggle() end)
    tracker:Hide()
end

local function paintTracker()
    if not tracker then buildTracker() end
    local depth, segment, _, caches = EsteriaManastorm_GetActive()
    if depth and not tracker:IsShown() then ES.Tween(tracker, 0.3, 0, function(p) tracker:SetAlpha(p) end) end
    shown(tracker, depth ~= nil)
    if not depth then return end
    local map, boss = scene(segment)
    tracker.title:SetText("MANASTORM  ·  " .. string.upper(map))
    tracker.depth:SetText(depth)
    tracker.boss:SetText(boss ~= "" and ("Defeat " .. boss) or "Clear the way forward")
    local link = EsteriaManastorm_GetChaoticLink()
    tracker.extra:SetText((link > 0 and ("|cfff26b5cChaotic Link x" .. link .. "|r   ") or "") .. caches
        .. (caches == 1 and " cache" or " caches"))
end

local queue, showing = {}, false
local function nextToast()
    local item = table.remove(queue, 1)
    if not item then showing = false; return end
    showing = true
    if not toast then
        toast = CreateFrame("Frame", "EsteriaManastormToast", UIParent)
        toast:SetSize(420, 72)
        toast:SetPoint("TOP", UIParent, "TOP", 0, -150)
        toast:SetFrameStrata("HIGH")
        ES.Solid(toast, "BACKGROUND", ES.INK, 0.94):SetAllPoints()
        ES.Border(toast, ES.GOLD)
        toast.icon = iconFrame(toast, 46)
        toast.icon:SetPoint("LEFT", 12, 0)
        toast.title = ES.Text(toast, 15, ES.GOLD)
        toast.title:SetPoint("TOPLEFT", toast.icon, "TOPRIGHT", 12, -4)
        toast.text = ES.Text(toast, 11, ES.TEXT)
        toast.text:SetPoint("TOPLEFT", toast.title, "BOTTOMLEFT", 0, -5)
        toast.text:SetPoint("RIGHT", -12, 0)
        toast.text:SetJustifyH("LEFT")
        toast:SetScript("OnUpdate", function(self, elapsed)
            self.left = self.left - elapsed
            if self.left <= 0 then
                self.left = math.huge
                ES.Tween("manastorm-toast", 0.4, 0, function(p) self:SetAlpha(1 - p); if p >= 1 then self:Hide(); nextToast() end end)
            end
        end)
    end
    toast.icon.icon:SetTexture(item[1])
    toast.title:SetText(item[2])
    toast.text:SetText(item[3])
    toast.left = 5
    toast:Show()
    -- drops in from just above its place
    ES.Tween("manastorm-toast", 0.35, 0, function(p)
        toast:SetAlpha(p)
        toast:ClearAllPoints(); toast:SetPoint("TOP", UIParent, "TOP", 0, -130 - 20 * p)
    end)
end

function M.Toast(icon, title, text)
    queue[#queue + 1] = { icon, title, text }
    if not showing then nextToast() end
end

local function cleared(depth)
    PlaySound("LevelUp")
    M.Toast("Interface\\Icons\\Spell_Arcane_PortalDalaran", "Depth " .. depth .. " cleared",
        (depth % 5 == 0) and ("Checkpoint reached: next time you can start at depth " .. (depth + 1) .. ".")
        or "Step through the portal to go deeper.")
    local unlocks = DATA.unlocks[track()] and DATA.unlocks[track()][depth]
    for _, id in ipairs(unlocks or {}) do
        local m = DATA.messages[id]
        if m then M.Toast("Interface\\Icons\\" .. m[1], m[2], m[3]) end
    end
end

-- -------------------------------------------------------------------------------------------------------------------
-- events, slash command, micro menu
-- -------------------------------------------------------------------------------------------------------------------
StaticPopupDialogs.ESTERIA_MANASTORM_LEAVE = {
    text = "Leave the Manastorm? Your next run can start from depth %d.",
    button1 = YES, button2 = NO, timeout = 0, whileDead = 1, hideOnEscape = 1,
    OnAccept = function() EsteriaManastorm_Leave() end,
}

local events = CreateFrame("Frame")
for _, e in ipairs({ "MANASTORM_CONFIG_UPDATED", "MANASTORM_PROGRESS_UPDATED", "MANASTORM_ACTIVE_UPDATED",
    "MANASTORM_ENTER_RESULT", "MANASTORM_LEAVE_RESULT", "MANASTORM_LEVEL_COMPLETED", "MANASTORM_FAILED",
    "MANASTORM_CHAOTIC_LINK_UPDATED", "MANASTORM_LOADOUT_UPDATED", "MANASTORM_LOADOUT_RESULT", "MANASTORM_REWARDS_UPDATED",
    "PARTY_MEMBERS_CHANGED", "RAID_ROSTER_UPDATE", "PLAYER_LEVEL_UP", "LEARNED_SPELL_IN_TAB", "PLAYER_ENTERING_WORLD",
    "BAG_UPDATE" }) do
    events:RegisterEvent(e)
end
events:SetScript("OnEvent", function(_, event, a, b)
    if not available() then return end
    if event == "BAG_UPDATE" then
        if frame and frame:IsShown() then paintCurrencies() end
        return
    elseif event == "MANASTORM_REWARDS_UPDATED" then
        M.rewardsAt = GetTime()
    elseif event == "MANASTORM_ENTER_RESULT" then
        if a == "ENTER_MANASTORM_OK" then
            if frame then frame:Hide() end
        else
            UIErrorsFrame:AddMessage(RESULTS[a] or RESULTS.ENTER_MANASTORM_UNKNOWN, 1, 0.1, 0.1)
        end
    elseif event == "MANASTORM_LEAVE_RESULT" and a ~= "LEAVE_MANASTORM_OK" then
        UIErrorsFrame:AddMessage(RESULTS[a] or RESULTS.LEAVE_MANASTORM_UNKNOWN, 1, 0.1, 0.1)
    elseif event == "MANASTORM_LOADOUT_RESULT" and b ~= "SET_MANASTORM_LOADOUT_OK" then
        UIErrorsFrame:AddMessage(RESULTS[b] or "That gadget cannot be set right now.", 1, 0.1, 0.1)
    elseif event == "MANASTORM_LEVEL_COMPLETED" then
        cleared(a)
    elseif event == "MANASTORM_FAILED" then
        PlaySound("igQuestFailed")
        M.Toast("Interface\\Icons\\Spell_Shadow_Teleport", "The Manastorm collapsed",
            "Your run is over. Start again from your last checkpoint.")
    end
    paintTracker()
    refresh()
end)

function EsteriaManastorm_Toggle()
    if not available() then
        DEFAULT_CHAT_FRAME:AddMessage("|cff9b7bff[Manastorm]|r The Manastorm needs the wowdecomp client.")
        return
    end
    if not frame then build() end
    shown(frame, not frame:IsShown())
end

BINDING_HEADER_ESTERIAMANASTORM = "Esteria Manastorm"
BINDING_NAME_ESTERIAMANASTORM_TOGGLE = "Open the Manastorm"
SLASH_ESTERIAMANASTORM1 = "/manastorm"
SlashCmdList.ESTERIAMANASTORM = EsteriaManastorm_Toggle
