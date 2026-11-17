-- Esteria Adventure Finder: one window with the Dungeon Finder (a bespoke front end on the client's own LFD queue)
-- and the Manastorm (Addons/EsteriaManastorm, embedded). The Dungeon Finder micro button and its key binding call
-- ToggleLFDParentFrame, which opens this window; the stock LFD popups (role check, ready check, boot votes, the
-- minimap eye) still run themselves. Styled like the Collections window.
local ES = EsteriaStyle
local W, H, HEADER = 1000, 610, 42
local CONTENT_H = H - HEADER
local RAIL_W, SIDE_W, ROW_H = 236, 280, 24
local TABS = { "Dungeon", "Manastorm" }
local ROLES = { { "TANK", "Tank" }, { "HEALER", "Healer" }, { "DAMAGER", "Damage" } }
-- Retail role icons and the group finder background (make_art.py turns wowdecomp/Assets into these BLPs).
local ART = "Interface\\AddOns\\EsteriaFinder\\Art\\"
local ROLE_ART = { TANK = ART .. "tank", HEALER = ART .. "healer", DAMAGER = ART .. "damager" }
-- Lock reasons that mean "not for you any more": hide those dungeons from the specific list.
local HIDE_LOCKS = { [1] = true, [3] = true, [1002] = true }

local frame, content, pane, tab = nil, nil, nil, "Dungeon"
local tabs = {}
local selection                -- a random dungeon id, or "specific"
local order, enabled, locks = {}, {}, {}
local listOffset = 0
local refresh

local function shown(region, on) if on then region:Show() else region:Hide() end end

-- ----------------------------------------------------------------------------------------------- widgets

local function flat(parent, width, height, text, primary)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, height)
    b.primary = primary
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, primary and 0.95 or 0.9); b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0); b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, primary and 13 or 12, primary and { 1, 0.92, 0.7 } or ES.TEXT); b.label:SetPoint("CENTER", 0, 1)
    b.label:SetText(text or "")
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12); ES.Tint(self.lines, ES.GOLD, 1) end end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0); ES.Tint(self.lines, self.primary and ES.GOLD or ES.GOLD_DIM, 1) end)
    function b:Set(text, on)
        self.label:SetText(text)
        if on then self:Enable() else self:Disable(); self.glow:SetAlpha(0) end
        self.label:SetAlpha(on and 1 or 0.35); self.bg:SetAlpha(on and (self.primary and 0.95 or 0.9) or 0.5)
    end
    return b
end

local function tabButton(parent, width, text)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, 32)
    b.wash = ES.Solid(b, "BACKGROUND", ES.GOLD, 0); b.wash:SetAllPoints()
    b.accent = ES.Solid(b, "ARTWORK", ES.GOLD, 0)
    b.accent:SetPoint("BOTTOMLEFT", 6, 0); b.accent:SetPoint("BOTTOMRIGHT", -6, 0); b.accent:SetHeight(2)
    b.label = ES.Text(b, 12, ES.MUTED); b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    function b:SetSelected(on)
        self.selected = on
        self.wash:SetAlpha(on and 0.10 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.MUTED))
    end
    b:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.05) end end)
    b:SetScript("OnLeave", function(self) self:SetSelected(self.selected) end)
    return b
end

local function band(parent, height)
    local t = parent:CreateTexture(nil, "BORDER")
    t:SetTexture(ES.WHITE)
    t:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    t:SetPoint("TOPLEFT", 1, -1); t:SetPoint("TOPRIGHT", -1, -1); t:SetHeight(height)
    return t
end

local function panel(parent, gold, art)
    local p = CreateFrame("Frame", nil, parent)
    if art then
        local bg = p:CreateTexture(nil, "BACKGROUND"); bg:SetTexture(art); bg:SetAllPoints()
        local shade = p:CreateTexture(nil, "BORDER"); shade:SetTexture(ES.WHITE)
        shade:SetGradientAlpha("VERTICAL", 0.02, 0.025, 0.04, 0, 0.02, 0.025, 0.04, 0.75)
        shade:SetPoint("TOPLEFT"); shade:SetPoint("TOPRIGHT"); shade:SetHeight(170)
    else
        ES.Solid(p, "BACKGROUND", gold and { 0.05, 0.05, 0.07 } or { 0.05, 0.06, 0.08 }, gold and 1 or 0.9):SetAllPoints()
    end
    if gold then
        local glow = p:CreateTexture(nil, "BORDER")
        glow:SetTexture(ES.WHITE)
        glow:SetGradientAlpha("VERTICAL", 0.62, 0.46, 0.15, 0.22, 0.62, 0.46, 0.15, 0)
        glow:SetPoint("BOTTOMLEFT"); glow:SetPoint("BOTTOMRIGHT"); glow:SetHeight(160)
    end
    ES.Border(p, gold and ES.GOLD or ES.GOLD_DIM, gold and 0.8 or 0.6)
    return p
end

local function head(parent, text, x, y)
    local t = ES.Text(parent, 10, ES.GOLD_DIM); t:SetPoint("TOPLEFT", x or 14, y); t:SetText(text)
    return t
end

local function railRow(parent, y)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(RAIL_W - 12, 36); row:SetPoint("TOPLEFT", 6, y)
    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
    row.label = ES.Text(row, 12, ES.TEXT); row.label:SetPoint("LEFT", 14, 0)
    row.label:SetWidth(RAIL_W - 60); row.label:SetJustifyH("LEFT")
    row.note = ES.Text(row, 10, ES.MUTED); row.note:SetPoint("RIGHT", -10, 0)
    row:SetScript("OnEnter", function(self)
        if not self.selected then self.wash:SetAlpha(0.06) end
        if self.tip then GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(self.label:GetText(), 1, 0.82, 0.1)
            GameTooltip:AddLine(self.tip, 1, 1, 1, true); GameTooltip:Show() end
    end)
    row:SetScript("OnLeave", function(self) if not self.selected then self.wash:SetAlpha(0) end; GameTooltip:Hide() end)
    function row:Paint(on, usable)
        self.selected = on
        self.wash:SetAlpha(on and 0.12 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or usable and ES.TEXT or ES.MUTED))
        self.label:SetAlpha(usable and 1 or 0.55)
    end
    return row
end

-- ----------------------------------------------------------------------------------------------- dungeon data

local function lockReason(id)
    if not GetLFDLockInfo or (GetLFDLockPlayerCount and GetLFDLockPlayerCount() or 0) < 1 then return nil end
    local _, reason = GetLFDLockInfo(id, 1)
    return reason ~= 0 and reason or nil
end

local function randomDungeons()
    local list = {}
    local level = UnitLevel("player")
    for i = 1, (GetNumRandomDungeons and GetNumRandomDungeons() or 0) do
        local id, name = GetLFGRandomDungeonInfo(i)
        local _, _, minLevel, maxLevel, _, _, _, expansion = GetLFGDungeonInfo(id)
        if level >= minLevel and level <= maxLevel and (EXPANSION_LEVEL or 2) >= expansion then
            list[#list + 1] = { id = id, name = name, joinable = IsLFGDungeonJoinable(id) }
        end
    end
    return list
end

-- The specific list: dungeons you could still run (others are hidden), under the headers that have any.
local function specificRows()
    order = GetLFDChoiceOrder and GetLFDChoiceOrder() or {}
    enabled = GetLFDChoiceEnabledState and GetLFDChoiceEnabledState() or {}
    locks = GetLFDChoiceLockedState and GetLFDChoiceLockedState() or {}
    local rows, header = {}, nil
    for _, id in ipairs(order) do
        if id < 0 then
            header = { id = id, header = true, name = GetLFGDungeonInfo(id) }
        else
            local reason = locks[id] and lockReason(id)
            if not (reason and HIDE_LOCKS[reason]) then
                if header then rows[#rows + 1] = header; header = nil end
                local name, _, minLevel, maxLevel = GetLFGDungeonInfo(id)
                rows[#rows + 1] = { id = id, name = name, minLevel = minLevel, maxLevel = maxLevel, locked = locks[id] }
            end
        end
    end
    return rows
end

local function inQueue()
    local mode = GetLFGMode()
    return mode == "queued" or mode == "rolecheck" or mode == "proposal", mode
end

local function join()
    local leader, tank, healer, dps = GetLFGRoles()
    if not (tank or healer or dps) then
        UIErrorsFrame:AddMessage("Pick at least one role.", 1, 0.1, 0.1); return
    end
    ClearAllLFGDungeons()
    if selection == "specific" then
        local count = 0
        for _, id in ipairs(order) do
            if id > 0 and enabled[id] and not locks[id] then SetLFGDungeon(id); count = count + 1 end
        end
        if count == 0 then UIErrorsFrame:AddMessage("Tick at least one dungeon you can enter.", 1, 0.1, 0.1); return end
    else
        SetLFGDungeon(selection)
    end
    JoinLFG()
    PlaySound("igMainMenuOptionCheckBoxOn")
end

-- ----------------------------------------------------------------------------------------------- dungeon pane

local D = {}

local function buildDungeon()
    pane = CreateFrame("Frame", nil, content); pane:SetAllPoints()
    band(pane, 56)
    D.title = ES.Text(pane, 22, ES.GOLD); D.title:SetPoint("TOPLEFT", 20, -12); D.title:SetText("Dungeon Finder")
    D.subtitle = ES.Text(pane, 11, ES.MUTED); D.subtitle:SetPoint("TOPLEFT", D.title, "BOTTOMLEFT", 1, -4)
    D.subtitle:SetText("Queue for a random dungeon or pick the ones you want. Choose your roles on the right.")
    D.state = ES.Text(pane, 12, ES.TEXT); D.state:SetPoint("TOPRIGHT", -20, -20)

    D.rail = panel(pane); D.rail:SetPoint("TOPLEFT", 14, -64); D.rail:SetPoint("BOTTOMLEFT", 14, 16); D.rail:SetWidth(RAIL_W)
    head(D.rail, "RANDOM DUNGEONS", 14, -12)
    D.randomRows = {}
    D.specificHead = head(D.rail, "PICK YOUR OWN", 14, -200)
    D.specificRow = railRow(D.rail, -218)
    D.specificRow.label:SetText("Specific Dungeons")
    D.specificRow:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); selection = "specific"; listOffset = 0; refresh() end)

    D.side = panel(pane, true); D.side:SetPoint("TOPRIGHT", -14, -64); D.side:SetPoint("BOTTOMRIGHT", -14, 16); D.side:SetWidth(SIDE_W)
    D.center = panel(pane, false, ART .. "background"); D.center:SetPoint("TOPLEFT", D.rail, "TOPRIGHT", 18, 0); D.center:SetPoint("BOTTOMRIGHT", D.side, "BOTTOMLEFT", -18, 0)

    -- center: a random dungeon's details
    D.info = CreateFrame("Frame", nil, D.center); D.info:SetAllPoints()
    D.kind = head(D.info, "", 18, -16)
    D.name = ES.Text(D.info, 20, ES.GOLD); D.name:SetPoint("TOPLEFT", D.kind, "BOTTOMLEFT", 0, -6)
    D.name:SetPoint("RIGHT", -18, 0); D.name:SetJustifyH("LEFT")
    D.desc = ES.Text(D.info, 11, ES.MUTED); D.desc:SetPoint("TOPLEFT", D.name, "BOTTOMLEFT", 0, -10)
    D.desc:SetPoint("RIGHT", -18, 0); D.desc:SetJustifyH("LEFT"); D.desc:SetJustifyV("TOP")
    D.desc:SetHeight(64)   -- room to wrap instead of truncating to one line
    -- Rewards: a ruled section with a daily/repeat badge and a two-column grid of reward cards.
    D.rewardHead = ES.Text(D.info, 14, ES.GOLD); D.rewardHead:SetPoint("TOPLEFT", 18, -168); D.rewardHead:SetText("Rewards")
    local rule = D.info:CreateTexture(nil, "ARTWORK"); rule:SetTexture(ES.WHITE)
    rule:SetGradientAlpha("HORIZONTAL", ES.GOLD[1], ES.GOLD[2], ES.GOLD[3], 0.7, ES.GOLD[1], ES.GOLD[2], ES.GOLD[3], 0)
    rule:SetPoint("TOPLEFT", D.rewardHead, "BOTTOMLEFT", 0, -6); rule:SetPoint("RIGHT", -18, 0); rule:SetHeight(1)
    D.rewardBadge = CreateFrame("Frame", nil, D.info); D.rewardBadge:SetSize(110, 18)
    D.rewardBadge:SetPoint("BOTTOMRIGHT", rule, "TOPRIGHT", 0, 4)
    D.rewardBadge.bg = ES.Solid(D.rewardBadge, "BACKGROUND", { 0.16, 0.32, 0.12 }, 0.95); D.rewardBadge.bg:SetAllPoints()
    D.rewardBadge.lines = ES.Border(D.rewardBadge, ES.GOOD)
    D.rewardBadge.text = ES.Text(D.rewardBadge, 9, { 0.85, 1, 0.8 }); D.rewardBadge.text:SetPoint("CENTER", 0, 0)
    D.rewards = {}
    local tileW = math.floor((W - 14 - RAIL_W - 18 - 18 - SIDE_W - 14 - 36 - 10) / 2)
    for i = 1, 8 do
        local r = CreateFrame("Frame", nil, D.info); r:SetSize(tileW, 54)
        r:SetPoint("TOPLEFT", 18 + ((i - 1) % 2) * (tileW + 10), -186 - math.floor((i - 1) / 2) * 62)
        r.bg = r:CreateTexture(nil, "BACKGROUND"); r.bg:SetTexture(ES.WHITE); r.bg:SetAllPoints()
        r.lines = ES.Border(r, ES.GOLD_DIM, 0.7)
        r.box = CreateFrame("Frame", nil, r); r.box:SetSize(40, 40); r.box:SetPoint("LEFT", 8, 0)
        r.glow = r:CreateTexture(nil, "BORDER"); r.glow:SetTexture(ES.WHITE)
        r.glow:SetPoint("TOPLEFT", r.box, "TOPLEFT", -4, 4); r.glow:SetPoint("BOTTOMRIGHT", r.box, "BOTTOMRIGHT", 4, -4)
        r.icon = r.box:CreateTexture(nil, "ARTWORK"); r.icon:SetPoint("TOPLEFT", 1, -1); r.icon:SetPoint("BOTTOMRIGHT", -1, 1)
        r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        r.boxLines = ES.Border(r.box, ES.GOLD)
        r.count = ES.Text(r.box, 11, ES.TEXT); r.count:SetPoint("BOTTOMRIGHT", -2, 3)
        r.name = ES.Text(r, 12, ES.TEXT); r.name:SetPoint("TOPLEFT", r.box, "TOPRIGHT", 10, -3); r.name:SetPoint("RIGHT", -8, 0)
        r.name:SetJustifyH("LEFT"); r.name:SetHeight(16)
        r.sub = ES.Text(r, 10, ES.MUTED); r.sub:SetPoint("BOTTOMLEFT", r.box, "BOTTOMRIGHT", 10, 3)
        r:EnableMouse(true)
        r:SetScript("OnEnter", function(self)
            ES.Tint(self.lines, ES.GOLD, 1)
            if not self.data then return end
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            if self.data.reward and GameTooltip.SetLFGDungeonReward then
                GameTooltip:SetLFGDungeonReward(self.data.dungeon, self.data.reward)
            else
                GameTooltip:SetText(self.data.sub, 1, 0.82, 0.1); GameTooltip:AddLine(self.data.plain or "", 1, 1, 1)
            end
            GameTooltip:Show()
        end)
        r:SetScript("OnLeave", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 0.7); GameTooltip:Hide() end)
        D.rewards[i] = r
    end

    -- center: the specific dungeon checklist
    D.list = CreateFrame("Frame", nil, D.center); D.list:SetAllPoints()
    head(D.list, "TICK THE DUNGEONS YOU WANT", 18, -16)
    D.listRows = {}
    D.visibleRows = math.floor((CONTENT_H - 64 - 16 - 50) / ROW_H)
    for i = 1, D.visibleRows do
        local row = CreateFrame("Button", nil, D.list)
        row:SetHeight(ROW_H); row:SetPoint("TOPLEFT", 12, -36 - (i - 1) * ROW_H); row:SetPoint("RIGHT", -12, 0)
        row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
        row.box = CreateFrame("Frame", nil, row); row.box:SetSize(14, 14); row.box:SetPoint("LEFT", 6, 0)
        ES.Solid(row.box, "BACKGROUND", ES.INK, 1):SetAllPoints(); row.boxLines = ES.Border(row.box, ES.GOLD_DIM)
        row.tick = ES.Solid(row.box, "ARTWORK", ES.GOLD, 1); row.tick:SetPoint("TOPLEFT", 3, -3); row.tick:SetPoint("BOTTOMRIGHT", -3, 3)
        row.name = ES.Text(row, 12, ES.TEXT); row.name:SetPoint("LEFT", row.box, "RIGHT", 8, 0)
        row.level = ES.Text(row, 10, ES.MUTED); row.level:SetPoint("RIGHT", -6, 0)
        row:SetScript("OnClick", function(self)
            local data = self.data
            if not data or data.header or data.locked or inQueue() then return end
            local on = not enabled[data.id]
            SetLFGDungeonEnabled(data.id, on); enabled[data.id] = on
            if LFGEnabledList then LFGEnabledList[data.id] = on end
            PlaySound("igMainMenuOptionCheckBoxOn"); refresh()
        end)
        row:SetScript("OnEnter", function(self)
            local data = self.data
            if not data or data.header then return end
            self.wash:SetAlpha(0.06)
            if data.locked then
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(data.name, 1, 0.82, 0.1)
                GameTooltip:AddLine(LFDConstructDeclinedMessage and LFDConstructDeclinedMessage(data.id) or "Locked", 1, 0.3, 0.3, true)
                GameTooltip:Show()
            end
        end)
        row:SetScript("OnLeave", function(self) self.wash:SetAlpha(0); GameTooltip:Hide() end)
        D.listRows[i] = row
    end
    D.list:EnableMouseWheel(true)
    D.list:SetScript("OnMouseWheel", function(_, delta) listOffset = math.max(0, listOffset - delta * 3); refresh() end)
    D.scrollNote = ES.Text(D.list, 10, ES.MUTED); D.scrollNote:SetPoint("BOTTOMRIGHT", -14, 10)

    -- side: roles, status, queue button
    head(D.side, "YOUR ROLES", 16, -14)
    D.roles = {}
    for i, spec in ipairs(ROLES) do
        local b = CreateFrame("Button", nil, D.side); b:SetSize(76, 88)
        b:SetPoint("TOPLEFT", 16 + (i - 1) * 84, -34)
        b.bg = ES.Solid(b, "BACKGROUND", ES.INK, 0.9); b.bg:SetAllPoints()
        b.lines = ES.Border(b, ES.GOLD_DIM)
        b.icon = b:CreateTexture(nil, "ARTWORK"); b.icon:SetSize(46, 46); b.icon:SetPoint("TOP", 0, -10)
        b.icon:SetTexture(ROLE_ART[spec[1]])
        b.label = ES.Text(b, 11, ES.TEXT); b.label:SetPoint("BOTTOM", 0, 8); b.label:SetText(spec[2])
        b.index = i
        b:SetScript("OnClick", function(self)
            if inQueue() then return end
            local roles = { GetLFGRoles() }
            roles[self.index + 1] = not roles[self.index + 1]
            SetLFGRoles(roles[1], roles[2], roles[3], roles[4])
            PlaySound("igMainMenuOptionCheckBoxOn"); refresh()
        end)
        D.roles[i] = b
    end
    D.leader = CreateFrame("Button", nil, D.side); D.leader:SetSize(SIDE_W - 32, 22); D.leader:SetPoint("TOPLEFT", 16, -130)
    D.leader.box = CreateFrame("Frame", nil, D.leader); D.leader.box:SetSize(14, 14); D.leader.box:SetPoint("LEFT")
    ES.Solid(D.leader.box, "BACKGROUND", ES.INK, 1):SetAllPoints(); ES.Border(D.leader.box, ES.GOLD_DIM)
    D.leader.tick = ES.Solid(D.leader.box, "ARTWORK", ES.GOLD, 1)
    D.leader.tick:SetPoint("TOPLEFT", 3, -3); D.leader.tick:SetPoint("BOTTOMRIGHT", -3, 3)
    D.leader.crown = D.leader:CreateTexture(nil, "ARTWORK"); D.leader.crown:SetSize(18, 18)
    D.leader.crown:SetPoint("LEFT", D.leader.box, "RIGHT", 8, 0); D.leader.crown:SetTexture(ART .. "guide")
    D.leader.label = ES.Text(D.leader, 11, ES.TEXT); D.leader.label:SetPoint("LEFT", D.leader.crown, "RIGHT", 6, 0)
    D.leader.label:SetText("I can lead the group")
    D.leader:SetScript("OnClick", function()
        if inQueue() then return end
        local leader, tank, healer, dps = GetLFGRoles()
        SetLFGRoles(not leader, tank, healer, dps); refresh()
    end)

    head(D.side, "STATUS", 16, -168)
    D.status = ES.Text(D.side, 13, ES.TEXT); D.status:SetPoint("TOPLEFT", 16, -186); D.status:SetWidth(SIDE_W - 32)
    D.status:SetJustifyH("LEFT")
    D.detail = ES.Text(D.side, 11, ES.MUTED); D.detail:SetPoint("TOPLEFT", D.status, "BOTTOMLEFT", 0, -8)
    D.detail:SetWidth(SIDE_W - 32); D.detail:SetJustifyH("LEFT")
    D.needs = {}
    for i, spec in ipairs({ "TANK", "HEALER", "DAMAGER", "DAMAGER", "DAMAGER" }) do
        local t = D.side:CreateTexture(nil, "ARTWORK"); t:SetSize(30, 30)
        t:SetPoint("TOPLEFT", 16 + (i - 1) * 36, -276)
        t:SetTexture(ROLE_ART[spec])
        D.needs[i] = t
    end
    D.queue = flat(D.side, SIDE_W - 32, 40, "Find Group", true); D.queue:SetPoint("BOTTOMLEFT", 16, 16)
    D.queue:SetScript("OnClick", function()
        if inQueue() then LeaveLFG() else join() end
    end)
    D.teleport = flat(D.side, SIDE_W - 32, 30, "Teleport"); D.teleport:SetPoint("BOTTOMLEFT", D.queue, "TOPLEFT", 0, 8)
    D.teleport:SetScript("OnClick", function() if LFGTeleport then LFGTeleport(IsInLFGDungeon and IsInLFGDungeon()) end end)
end

local function paintRandom(id)
    local name, typeID, _, _, _, _, _, _, _, _, difficulty, _, description = GetLFGDungeonInfo(id)
    D.kind:SetText((difficulty or 0) > 0 and "RANDOM HEROIC DUNGEON" or "RANDOM DUNGEON")
    D.name:SetText(name or "")
    D.desc:SetText(description and description ~= "" and description or "You'll be placed in a dungeon that suits your level, with a group the realm puts together.")
    local doneToday, moneyBase, moneyVar, xpBase, xpVar, numRewards = GetLFGDungeonRewards(id)
    local randoms = 4 - GetNumPartyMembers()
    local money, xp = (moneyBase or 0) + (moneyVar or 0) * randoms, (xpBase or 0) + (xpVar or 0) * randoms
    D.rewardBadge.text:SetText(doneToday and "REPEAT REWARDS" or "DAILY BONUS")
    D.rewardBadge.bg:SetVertexColor(doneToday and 0.12 or 0.16, doneToday and 0.12 or 0.32, doneToday and 0.14 or 0.12)
    ES.Tint(D.rewardBadge.lines, doneToday and ES.MUTED or ES.GOOD, 1)
    D.rewardBadge.text:SetTextColor(unpack(doneToday and ES.MUTED or { 0.85, 1, 0.8 }))
    -- gold and experience first, then each item; items take their quality colour
    local list = {}
    if money > 0 then
        list[#list + 1] = { icon = "Interface\\Icons\\INV_Misc_Coin_02", name = GetCoinTextureString(money), sub = "Gold",
            plain = "Paid out when the dungeon is done.", color = ES.GOLD }
    end
    if xp > 0 then
        list[#list + 1] = { icon = "Interface\\Icons\\Spell_Holy_SurgeOfLight", name = xp .. " XP", sub = "Experience",
            plain = "Granted when the dungeon is done.", color = { 0.55, 0.75, 1 } }
    end
    for i = 1, (numRewards or 0) do
        local rewardName, texture, count = GetLFGDungeonRewardInfo(id, i)
        if rewardName then
            local quality = select(3, GetItemInfo(rewardName))
            local q = quality and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[quality]
            list[#list + 1] = { icon = texture, name = rewardName, count = count, dungeon = id, reward = i,
                sub = (count and count > 1) and ("x" .. count) or "Item", color = q and { q.r, q.g, q.b } or { 0.64, 0.21, 0.93 } }
        end
    end
    for i, r in ipairs(D.rewards) do
        local data = list[i]
        r.data = data
        if data then
            local c = data.color
            r.icon:SetTexture(data.icon)
            r.name:SetText(data.name); r.name:SetTextColor(c[1], c[2], c[3])
            r.sub:SetText(data.sub)
            r.count:SetText(data.count and data.count > 1 and data.count or "")
            r.bg:SetGradientAlpha("HORIZONTAL", c[1] * 0.35, c[2] * 0.35, c[3] * 0.35, 0.9, 0.05, 0.05, 0.07, 0.9)
            r.glow:SetGradientAlpha("VERTICAL", c[1], c[2], c[3], 0.15, c[1], c[2], c[3], 0.55)
            ES.Tint(r.boxLines, c, 1)
            r:Show()
        else
            r:Hide()
        end
    end
end

local function paintSpecific()
    local rows = specificRows()
    local maxOffset = math.max(0, #rows - #D.listRows)
    listOffset = math.min(listOffset, maxOffset)
    local queued = inQueue()
    for i, row in ipairs(D.listRows) do
        local data = rows[i + listOffset]
        row.data = data
        if not data then row:Hide()
        else
            row:Show()
            if data.header then
                row.box:Hide()
                row.name:SetText(string.upper(data.name or "")); row.name:SetTextColor(unpack(ES.GOLD_DIM))
                row.name:SetPoint("LEFT", row, "LEFT", 6, 0)
                row.level:SetText("")
            else
                row.box:Show()
                row.name:SetPoint("LEFT", row.box, "RIGHT", 8, 0)
                row.name:SetText(data.name or "")
                local on = enabled[data.id] and not data.locked
                shown(row.tick, on)
                row.name:SetTextColor(unpack(data.locked and ES.MUTED or on and ES.GOLD or ES.TEXT))
                row.name:SetAlpha((data.locked or queued) and 0.55 or 1)
                row.level:SetText(data.locked and "Locked" or (data.minLevel == data.maxLevel and ("Level " .. data.minLevel) or ("Levels " .. data.minLevel .. "-" .. data.maxLevel)))
            end
        end
    end
    D.scrollNote:SetText(maxOffset > 0 and "Scroll for more" or "")
end

local function paintSide()
    local queued, mode = inQueue()
    local canTank, canHeal, canDPS = GetAvailableRoles()
    local leader, tank, healer, dps = GetLFGRoles()
    local picked = { tank, healer, dps }
    local can = { canTank, canHeal, canDPS }
    for i, b in ipairs(D.roles) do
        local on = picked[i] and can[i]
        b.icon:SetDesaturated(not can[i])
        b.icon:SetAlpha(can[i] and (on and 1 or 0.45) or 0.2)
        ES.Tint(b.lines, on and ES.GOLD or ES.GOLD_DIM, on and 1 or 0.6)
        b.bg:SetVertexColor(on and 0.13 or ES.INK[1], on and 0.11 or ES.INK[2], on and 0.07 or ES.INK[3])
        b.label:SetTextColor(unpack(on and ES.GOLD or can[i] and ES.TEXT or ES.MUTED))
        if can[i] and not queued then b:Enable() else b:Disable() end
    end
    shown(D.leader.tick, leader)

    local status, detail, needsShown = "Not in a queue", "Pick a dungeon and your roles, then find a group.", false
    if mode == "rolecheck" then
        status, detail = "Role check", "Waiting for your group to confirm their roles."
    elseif mode == "queued" then
        local hasData, _, tankNeeds, healerNeeds, dpsNeeds, _, instanceName, averageWait, _, _, _, myWait, queuedTime = GetLFGQueueStats()
        status = "Looking for a group" .. (instanceName and instanceName ~= "" and (": " .. instanceName) or "")
        local elapsed = queuedTime and (GetTime() - queuedTime) or 0
        detail = "In queue " .. (elapsed >= 60 and SecondsToTime(elapsed) or "under a minute")
        if hasData and myWait and myWait > 0 then detail = detail .. "\nAverage wait " .. SecondsToTime(myWait, false, false, 1) end
        if hasData then
            needsShown = true
            local found = { tankNeeds == 0, healerNeeds == 0 }
            for i = 1, 3 do found[2 + i] = i <= 3 - (dpsNeeds or 3) end
            for i, t in ipairs(D.needs) do t:SetAlpha(found[i] and 1 or 0.25); t:SetDesaturated(not found[i]) end
        end
    elseif mode == "proposal" then
        status, detail = "Group found!", "Accept the ready check to enter the dungeon."
    elseif mode == "lfgparty" or mode == "abandonedInDungeon" then
        status, detail = "In a Dungeon Finder group", "Use Teleport to travel in or out of the dungeon."
    end
    D.status:SetText(status); D.detail:SetText(detail)
    for _, t in ipairs(D.needs) do shown(t, needsShown) end
    D.state:SetText(queued and status or "")

    local leaderOK = (GetNumPartyMembers() == 0 or IsPartyLeader()) and (GetNumRaidMembers() == 0 or IsRaidLeader())
    if queued then
        D.queue:Set("Leave Queue", mode ~= "proposal")
    else
        local text = (GetNumPartyMembers() > 0 or GetNumRaidMembers() > 0) and "Join as Party" or "Find Group"
        local canPick = selection == "specific" or (selection and IsLFGDungeonJoinable(selection))
        D.queue:Set(text, leaderOK and canPick and mode ~= "lfgparty")
    end
    shown(D.teleport, mode == "lfgparty" or mode == "abandonedInDungeon")
    D.teleport:Set(IsInLFGDungeon and IsInLFGDungeon() and "Teleport Out" or "Teleport In", true)
end

local function paintDungeon()
    local randoms = randomDungeons()
    -- default to the realm's best random for your level; a random that became unavailable falls back the same way
    if selection ~= "specific" and (not selection or not IsLFGDungeonJoinable(selection)) then
        selection = (GetRandomDungeonBestChoice and GetRandomDungeonBestChoice()) or (randoms[1] and randoms[1].id) or "specific"
    end
    for i, data in ipairs(randoms) do
        local row = D.randomRows[i]
        if not row then
            row = railRow(D.rail, -30 - (i - 1) * 38); D.randomRows[i] = row
            row:SetScript("OnClick", function(self)
                if not self.joinable then return end
                PlaySound("igMainMenuOptionCheckBoxOn"); selection = self.id; refresh()
            end)
        end
        row.id, row.joinable = data.id, data.joinable
        row.label:SetText(data.name)
        row.note:SetText(data.joinable and "" or "Locked")
        row.tip = not data.joinable and LFDConstructDeclinedMessage and LFDConstructDeclinedMessage(data.id) or nil
        row:Paint(selection == data.id, data.joinable)
        row:Show()
    end
    for i = #randoms + 1, #D.randomRows do D.randomRows[i]:Hide() end
    local specY = -30 - math.max(1, #randoms) * 38 - 14
    D.specificHead:ClearAllPoints(); D.specificHead:SetPoint("TOPLEFT", 14, specY)
    D.specificRow:ClearAllPoints(); D.specificRow:SetPoint("TOPLEFT", 6, specY - 18)
    D.specificRow:Paint(selection == "specific", true)

    shown(D.info, selection ~= "specific"); shown(D.list, selection == "specific")
    if selection == "specific" then paintSpecific() elseif selection then paintRandom(selection) end
    paintSide()
end

-- ----------------------------------------------------------------------------------------------- window

local function selectTab(name)
    tab = name
    for label, b in pairs(tabs) do b:SetSelected(label == tab) end
    shown(pane, tab == "Dungeon")
    if tab == "Manastorm" then
        local ok = EsteriaManastorm_Embed and EsteriaManastorm_Embed(content, function() frame:Hide() end)
        shown(frame.noManastorm, not ok)
    else
        if EsteriaManastormFrame then EsteriaManastormFrame:Hide() end
        frame.noManastorm:Hide()
    end
    refresh()
end

local function build()
    frame = CreateFrame("Frame", "EsteriaFinderFrame", UIParent)
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving); frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames, "EsteriaFinderFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); band(frame, HEADER); ES.Border(frame, ES.GOLD)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -(HEADER - 1)); divider:SetPoint("TOPRIGHT", -1, -(HEADER - 1)); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12); title:SetText("Adventure Finder")
    for i, name in ipairs(TABS) do
        local b = tabButton(frame, 120, name); b:SetPoint("TOPLEFT", 210 + (i - 1) * 126, -6)
        b:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); selectTab(name) end)
        tabs[name] = b
    end
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    content = CreateFrame("Frame", nil, frame); content:SetSize(W, CONTENT_H); content:SetPoint("BOTTOMLEFT")
    frame.noManastorm = ES.Text(content, 13, ES.MUTED); frame.noManastorm:SetPoint("CENTER")
    frame.noManastorm:SetText("The Manastorm needs the wowdecomp client.")
    buildDungeon()
    frame:SetScript("OnShow", function(self)
        PlaySound("igCharacterInfoOpen")
        if RequestLFDPlayerLockInfo then RequestLFDPlayerLockInfo() end
        if RequestLFDPartyLockInfo then RequestLFDPartyLockInfo() end
        selectTab(tab)
        ES.Tween("finder-open", 0.2, 0, function(p) self:SetAlpha(p) end)
    end)
    frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); GameTooltip:Hide() end)
    local tick = 0
    frame:SetScript("OnUpdate", function(_, elapsed)
        tick = tick + elapsed
        if tick > 1 and tab == "Dungeon" and inQueue() then tick = 0; paintSide() end
    end)
end

refresh = function()
    if not frame or not frame:IsShown() then return end
    if tab == "Dungeon" then paintDungeon() end
end

function EsteriaFinder_Show(name)
    if not frame then build() end
    if name and tabs[name] then tab = name end
    if frame:IsShown() then selectTab(tab) else frame:Show() end
end

function EsteriaFinder_Toggle(name)
    if not frame then build() end
    if frame:IsShown() and (not name or name == tab) then frame:Hide() else EsteriaFinder_Show(name) end
end

local events = CreateFrame("Frame")
for _, e in ipairs({ "PLAYER_LOGIN", "LFG_UPDATE", "LFG_QUEUE_STATUS_UPDATE", "LFG_ROLE_UPDATE", "LFG_UPDATE_RANDOM_INFO",
    "LFG_LOCK_INFO_RECEIVED", "LFG_PROPOSAL_SHOW", "LFG_PROPOSAL_FAILED", "LFG_PROPOSAL_SUCCEEDED", "LFG_ROLE_CHECK_SHOW",
    "LFG_ROLE_CHECK_HIDE", "PARTY_MEMBERS_CHANGED", "PLAYER_LEVEL_UP" }) do events:RegisterEvent(e) end
events:SetScript("OnEvent", function(_, event)
    if event == "PLAYER_LOGIN" then
        -- The Dungeon Finder micro button and key binding call this global: they open the Adventure Finder now.
        ToggleLFDParentFrame = function() EsteriaFinder_Toggle("Dungeon") end
        return
    end
    refresh()
end)

SLASH_ESTERIAFINDER1, SLASH_ESTERIAFINDER2 = "/finder", "/lfd"
SlashCmdList.ESTERIAFINDER = function() EsteriaFinder_Toggle("Dungeon") end
