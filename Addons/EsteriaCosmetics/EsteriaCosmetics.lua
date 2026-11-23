-- Cosmetics shares the collection layout with Incarnations and embeds the existing Emotes window intact.
local ES = EsteriaStyle
local PREFIX = "ESTCOS"
local W, H, CONTENT_H = 1180, 722, 680
local frame, content, pane, grid, stage, search, totalText, countText, pageText, apply, remove, favorite
local tabs, cards, rail = {}, {}, {}
local tab, filter, page, focus = "Backpacks", "all", 1, nil
local known, selected, syncing, db = {}, 0, nil, nil
local filtered = {}
local weaponKnown,mountKnown,hearthItems,weaponSelected={},{},{},{}
local refresh, selectTab
local mountType, typeButton, randomButton, customize = "All types"
local filters = {{"all", "All appearances"}, {"known", "Collected"}, {"favorites", "Favorites"}, {"locked", "Not collected"}}

local function send(body) SendAddonMessage(PREFIX, body, "WHISPER", UnitName("player")) end
local function shown(region, value) if value then region:Show() else region:Hide() end end
local function list()
    if tab=="Wings" then return EsteriaCosmeticsWings or {} end
    if tab=="Weapons" then return EsteriaCollectionsWeapons or {} end
    if tab=="Mounts" then return EsteriaCollectionsMounts or {} end
    if tab=="Pets" then return EsteriaCollections_Pets() end
    if tab=="Hearthstones" then return EsteriaCollectionsHearths or {} end
    return EsteriaCosmeticsData or {}
end
local function kind(row) return row and (row.kind or (row.spell and "wing" or "backpack")) end
local function id(row) return row and (row.spell or row.id) end
local function favoriteKey(row) return kind(row) .. ":" .. id(row) end
local function isFavorite(row)
    if not db then return false end
    if kind(row)=="mount" or kind(row)=="pet" then return EsteriaCollections_IsFavorite(row,db.favorites) end
    return db.favorites[favoriteKey(row)]
end
local function spellbook()
    local result = {}
    for t = 1, GetNumSpellTabs() do
        local _, _, offset, count = GetSpellTabInfo(t)
        for slot = offset + 1, offset + count do
            local link = GetSpellLink(slot, BOOKTYPE_SPELL or "spell")
            local value = link and tonumber(link:match("spell:(%d+)"))
            if value then result[value] = true end
        end
    end
    return result
end
local learnedSpells = {}
local function owned(row)
    if not row then return false end
    local k=kind(row)
    if k=="weapon" then return weaponKnown[row.id] end
    if k=="mount" then return mountKnown[row.spell] or learnedSpells[row.spell] or EsteriaCollections_Companion(row)~=nil end
    if k=="pet" then return EsteriaCollections_Companion(row)~=nil end
    if k=="hearth" then return hearthItems[row.item]~=nil end
    return k=="wing" and learnedSpells[row.spell] or k=="backpack" and known[row.id]
end
local function wingActive(row)
    if not row or not row.spell then return false end
    if kind(row)=="pet" then local entry=EsteriaCollections_Companion(row);return entry and entry.active end
    for i = 1, 40 do
        local name, _, _, _, _, _, _, _, _, _, spell = UnitBuff("player", i)
        if not name then break end
        if spell == row.spell then return true end
    end
    return false
end
-- Flat Esteria button (the Incarnations/Emotes look). primary = gold call to action. secure buttons keep their template.
local function button(parent, width, text, secure, primary, height)
    local b = CreateFrame("Button", nil, parent, secure and "SecureActionButtonTemplate" or nil)
    b:SetSize(width, height or 28)
    b.primary = primary
    b.bg = ES.Solid(b, "BACKGROUND", primary and { 0.36, 0.25, 0.06 } or ES.INK, primary and 0.95 or 0.9); b.bg:SetAllPoints()
    b.glow = ES.Solid(b, "ARTWORK", ES.GOLD, 0); b.glow:SetAllPoints()
    b.lines = ES.Border(b, primary and ES.GOLD or ES.GOLD_DIM)
    b.label = ES.Text(b, primary and 13 or 12, primary and { 1, 0.92, 0.7 } or ES.TEXT); b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b:SetScript("OnEnter", function(self) if self:IsEnabled() == 1 then self.glow:SetAlpha(0.12); ES.Tint(self.lines, ES.GOLD, 1) end end)
    b:SetScript("OnLeave", function(self) self.glow:SetAlpha(0); ES.Tint(self.lines, self.primary and ES.GOLD or ES.GOLD_DIM, 1) end)
    b.Active = function(self, on)
        if on then self:Enable() else self:Disable(); self.glow:SetAlpha(0) end
        self.label:SetAlpha(on and 1 or 0.35); self.bg:SetAlpha(on and (self.primary and 0.95 or 0.9) or 0.5)
    end
    return b
end

-- Window tab: text with a gold underline and wash when selected.
local function tabButton(parent, width, text)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(width, 32)
    b.wash = ES.Solid(b, "BACKGROUND", ES.GOLD, 0); b.wash:SetAllPoints()
    b.accent = ES.Solid(b, "ARTWORK", ES.GOLD, 0)
    b.accent:SetPoint("BOTTOMLEFT", 6, 0); b.accent:SetPoint("BOTTOMRIGHT", -6, 0); b.accent:SetHeight(2)
    b.label = ES.Text(b, 12, ES.MUTED); b.label:SetPoint("CENTER", 0, 1); b.label:SetText(text)
    b.SetSelected = function(self, on)
        self.selected = on
        self.wash:SetAlpha(on and 0.10 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.MUTED))
    end
    b:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.05); self.label:SetTextColor(unpack(ES.TEXT)) end end)
    b:SetScript("OnLeave", function(self) self:SetSelected(self.selected) end)
    return b
end

-- Gold gradient band across the top of a panel (the header of the Incarnations/Emotes windows).
local function headerBand(parent, height)
    local band = parent:CreateTexture(nil, "BORDER")
    band:SetTexture(ES.WHITE)
    band:SetGradientAlpha("VERTICAL", 0.55, 0.40, 0.12, 0, 0.55, 0.40, 0.12, 0.22)
    band:SetPoint("TOPLEFT", 1, -1); band:SetPoint("TOPRIGHT", -1, -1); band:SetHeight(height)
    return band
end

local function iconFrame(parent, size)
    local holder = CreateFrame("Frame", nil, parent)
    holder:SetSize(size + 2, size + 2)
    holder.icon = holder:CreateTexture(nil, "ARTWORK")
    holder.icon:SetPoint("TOPLEFT", 1, -1); holder.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    holder.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    holder.lines = ES.Border(holder, ES.GOLD_DIM)
    return holder
end

local function preview(parent)
    local model = CreateFrame("PlayerModel", nil, parent)
    model.facing = 3.14
    model:SetScript("OnUpdate", function(self, elapsed)
        if self.waiting and self.row and self.row.creature and not self.row.model and type(self:GetModel())~="string" then self:SetCreature(self.row.creature) end
        local hearthReady
        if self.playerPreview and kind(self.row)=="hearth" then
            self.castClock = self.castClock + elapsed * 1000
            hearthReady = EsteriaCosmeticsHearthPreview(self,self.row.spell,self.castClock)
        end
        if self.waiting then
            local ready
            if self.playerPreview then
                if kind(self.row)=="wing" then ready = EsteriaCosmeticsWingPreview(self, self.row.spell)
                elseif kind(self.row)=="hearth" then ready = hearthReady
                else ready = EsteriaCosmeticsPreview(self, self.row.display) end
                if ready and EsteriaCosmeticsFit then ready = EsteriaCosmeticsFit(self,self.facing,0.9) end
            else
                if EsteriaCosmeticsFit then ready = EsteriaCosmeticsFit(self,self.facing,0.9)
                elseif RetailModelFit then ready = RetailModelFit(self,0.65)
                elseif RetailModelReportLoaded then ready = RetailModelReportLoaded(self) end
            end
            if ready then self.waiting = nil; self:SetFacing(self.facing); self:SetAlpha(1); if self.onReady then self.onReady(self) end end
        end
        if self.dragX then
            local x = GetCursorPosition()
            self.facing = self.facing + (x - self.dragX) * 0.012
            self.dragX = x; self:SetFacing(self.facing)
            if EsteriaCosmeticsFit then EsteriaCosmeticsFit(self,self.facing,0.9) end
        elseif self.spin and not self.waiting then
            self.facing = self.facing + elapsed * self.spin; self:SetFacing(self.facing)
            if EsteriaCosmeticsFit then EsteriaCosmeticsFit(self,self.facing,0.9) end
        end
    end)
    model.ShowRow = function(self, row, player)
        if self.row == row and self.playerPreview == player and type(self:GetModel())=="string" then return end
        self.row, self.playerPreview, self.waiting = row, player, nil
        self.castClock = 0
        self:SetAlpha(0)
        self:ClearModel()
        if not row then self:Hide(); return end
        if player then self:SetUnit("player")
        elseif row.model then
            self:SetModel(row.model)
            if row.creatureDisplay and RetailModelBindDisplay then RetailModelBindDisplay(self,row.creatureDisplay) end
        elseif row.creature then self:SetCreature(row.creature)
        else self:Hide(); return end
        self.waiting = true; self:Show()
    end
    return model
end

local function choose(row)
    if InCombatLockdown() then UIErrorsFrame:AddMessage("Leave combat to change cosmetic selection.",1,0.1,0.1); return end
    focus = row; refresh()
end

function EsteriaCosmetics_MountCustomized()
    if stage and stage.model then stage.model.row = nil; refresh() end
end
function EsteriaCosmetics_RefitPreview()
    if stage and stage.model and stage.model.row and stage.model.playerPreview then stage.model.waiting=true end
end
-- The shared pane for Wings, Backpacks, Hearthstones, Weapons, Mounts and Pets, laid out like Incarnations:
-- header band, sectioned rail on the left, 3x2 card grid, stage with an info panel on the right.
local RAIL_W, CARD_W, CARD_H, GAP = 236, 170, 248, 12
local GRID_W, GRID_H = 3 * CARD_W + 2 * GAP, 2 * CARD_H + GAP
local STAGE_W = W - 14 - (14 + RAIL_W + 18 + GRID_W + 18)
local MOUNT_TYPES = { { "All types", "All mounts" }, { "Ground", "Ground" }, { "Flying", "Flying" }, { "Aquatic", "Aquatic" } }
local SUBTITLES = {
    Wings = "Wings you know from your spellbook. Select a pair to wear it.",
    Backpacks = "Learn backpack appearances with their collection items, then apply them to your cloak.",
    Weapons = "Fortnite weapon appearances. Apply to a compatible equipped weapon; normal transmog costs apply.",
    Mounts = "The realm's mount collection, shared across your account. Select a collected mount to summon it.",
    Pets = "Collect companions, mark favorites and summon them here. Drag a pet onto your action bar.",
    Hearthstones = "Choose a hearthstone you carry to return home with its custom visual. Item cooldowns apply.",
}
local KINDS = { wing = "WINGS", backpack = "BACKPACK", weapon = "WEAPON", mount = "MOUNT", pet = "COMPANION", hearth = "HEARTHSTONE" }
local railRows, typeRows, typeHead = {}, {}, nil

-- A rail row in the Emotes style: label, count on the right, gold accent and wash when selected.
local function railRow(parent, y, label, onClick)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(RAIL_W - 12, 36); row:SetPoint("TOPLEFT", 6, y)
    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
    row.label = ES.Text(row, 12, ES.TEXT); row.label:SetPoint("LEFT", 14, 0); row.label:SetText(label)
    row.count = ES.Text(row, 11, ES.MUTED); row.count:SetPoint("RIGHT", -12, 0)
    row:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); onClick() end)
    row:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.06) end end)
    row:SetScript("OnLeave", function(self) if not self.selected then self.wash:SetAlpha(0) end end)
    row.Paint = function(self, on, count)
        self.selected = on
        self.wash:SetAlpha(on and 0.12 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        self.count:SetText(count or "")
    end
    return row
end

local function railHead(parent, y, text)
    local head = ES.Text(parent, 10, ES.GOLD_DIM); head:SetPoint("TOPLEFT", 14, y); head:SetText(text)
    return head
end

local function buildCard(i)
    local card = CreateFrame("Button", nil, grid)
    card:SetSize(CARD_W, CARD_H)
    card:SetPoint("TOPLEFT", ((i - 1) % 3) * (CARD_W + GAP), -math.floor((i - 1) / 3) * (CARD_H + GAP))
    card.bg = ES.Solid(card, "BACKGROUND", { 0.06, 0.07, 0.10 }, 1); card.bg:SetAllPoints()
    local floor = card:CreateTexture(nil, "BORDER")
    floor:SetTexture(ES.WHITE)
    floor:SetGradientAlpha("VERTICAL", 0.55, 0.42, 0.16, 0.22, 0.55, 0.42, 0.16, 0)
    floor:SetPoint("BOTTOMLEFT", 0, 46); floor:SetPoint("BOTTOMRIGHT", 0, 46); floor:SetHeight(90)
    card.lines = ES.Border(card, ES.GOLD_DIM, 0.7)
    card.model = preview(card); card.model:SetPoint("TOPLEFT", 2, -2); card.model:SetPoint("BOTTOMRIGHT", -2, 46)
    card.icon = iconFrame(card, 64); card.icon:SetPoint("CENTER", 0, 24)
    card.plate = ES.Solid(card, "ARTWORK", ES.INK, 0.92)
    card.plate:SetPoint("BOTTOMLEFT", 1, 1); card.plate:SetPoint("BOTTOMRIGHT", -1, 1); card.plate:SetHeight(45)
    card.name = ES.Text(card, 11, ES.TEXT)
    card.name:SetPoint("TOPLEFT", card.plate, "TOPLEFT", 8, -6); card.name:SetPoint("TOPRIGHT", card.plate, "TOPRIGHT", -8, -6)
    card.name:SetJustifyH("CENTER"); card.name:SetHeight(18)
    card.state = ES.Text(card, 10, ES.MUTED)
    card.state:SetPoint("TOP", card.name, "BOTTOM", 0, -1)
    -- Badges, the favorite mark and the locked veil sit above the model frame.
    card.top = CreateFrame("Frame", nil, card); card.top:SetAllPoints(); card.top:SetFrameLevel(card.model:GetFrameLevel() + 2)
    card.veil = ES.Solid(card.top, "BACKGROUND", { 0, 0, 0 }, 0.45)
    card.veil:SetPoint("TOPLEFT", 1, -1); card.veil:SetPoint("BOTTOMRIGHT", -1, 46)
    card.badge = CreateFrame("Frame", nil, card.top); card.badge:SetSize(66, 16); card.badge:SetPoint("TOPRIGHT", -6, -6)
    card.badge.bg = ES.Solid(card.badge, "BACKGROUND", { 0.16, 0.32, 0.12 }, 0.95); card.badge.bg:SetAllPoints()
    card.badge.lines = ES.Border(card.badge, ES.GOOD)
    card.badge.text = ES.Text(card.badge, 9, { 0.85, 1, 0.8 }); card.badge.text:SetPoint("CENTER", 0, 0)
    card.star = ES.Solid(card.top, "OVERLAY", ES.GOLD, 1); card.star:SetSize(7, 7); card.star:SetPoint("TOPLEFT", 9, -10)
    card.sweep = ES.Solid(card.top, "OVERLAY", ES.GOLD, 0); card.sweep:SetAllPoints()
    card:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); choose(self.row) end)
    card:RegisterForDrag("LeftButton")
    card:SetScript("OnDragStart", function(self)
        if InCombatLockdown() or not self.row or not owned(self.row) then return end
        local entry = EsteriaCollections_Companion(self.row)
        if entry then PickupCompanion(kind(self.row) == "pet" and "CRITTER" or "MOUNT", entry.index)
        elseif kind(self.row) == "mount" then PickupSpell(self.row.spell) end
    end)
    card:SetScript("OnEnter", function(self)
        if not self.row then return end
        self.model.spin = 0.9
        if self.row ~= focus then ES.Tint(self.lines, ES.GOLD, 0.9) end
        ES.Tween("cosmetics-card" .. i, 0.18, 0, function(p) self.sweep:SetAlpha(0.07 * p) end)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(self.row.name, 1, 0.82, 0.1)
        GameTooltip:AddLine(owned(self.row) and "Collected" or "Not collected", 1, 1, 1)
        if self.row.sourceName then GameTooltip:AddLine("Source: " .. self.row.sourceName, 0.6, 0.59, 0.55, true)
        elseif self.row.item then GameTooltip:AddLine("Requires " .. self.row.name, 0.6, 0.59, 0.55, true) end
        if self.row.weaponType then GameTooltip:AddLine(self.row.weaponType, 0.6, 0.59, 0.55) end
        GameTooltip:Show()
    end)
    card:SetScript("OnLeave", function(self)
        GameTooltip:Hide(); self.model.spin = nil; self.sweep:SetAlpha(0)
        if self.row ~= focus then ES.Tint(self.lines, ES.GOLD_DIM, 0.7) end
    end)
    card:EnableMouseWheel(true)
    card:SetScript("OnMouseWheel", function(_, delta) page = page - delta; refresh() end)
    return card
end

local function buildPane()
    pane = CreateFrame("Frame", nil, content); pane:SetAllPoints()
    ES.Solid(pane, "BACKGROUND", ES.INK, 0.97):SetAllPoints()
    headerBand(pane, 56)
    pane.title = ES.Text(pane, 22, ES.GOLD); pane.title:SetPoint("TOPLEFT", 20, -12)
    pane.note = ES.Text(pane, 11, ES.MUTED); pane.note:SetPoint("TOPLEFT", pane.title, "BOTTOMLEFT", 1, -4)
    pane.note:SetWidth(860); pane.note:SetJustifyH("LEFT")
    totalText = ES.Text(pane, 12, ES.TEXT); totalText:SetPoint("TOPRIGHT", -20, -20)

    local sidebar = CreateFrame("Frame", nil, pane)
    sidebar:SetPoint("TOPLEFT", 14, -64); sidebar:SetPoint("BOTTOMLEFT", 14, 16); sidebar:SetWidth(RAIL_W)
    ES.Solid(sidebar, "BACKGROUND", { 0.05, 0.06, 0.08 }, 0.9):SetAllPoints(); ES.Border(sidebar, ES.GOLD_DIM, 0.6)
    railHead(sidebar, -12, "COLLECTION")
    for i, option in ipairs(filters) do
        railRows[i] = railRow(sidebar, -30 - (i - 1) * 38, option[2], function() filter = option[1]; page = 1; refresh() end)
    end
    local typeY = -30 - #filters * 38 - 12
    typeHead = railHead(sidebar, typeY, "MOUNT TYPE")
    for i, option in ipairs(MOUNT_TYPES) do
        typeRows[i] = railRow(sidebar, typeY - 18 - (i - 1) * 38, option[2], function() mountType = option[1]; page = 1; refresh() end)
        typeRows[i].value = option[1]
    end
    randomButton = button(sidebar, RAIL_W - 16, "Random Favorite Mount", false, true, 30)
    randomButton:SetPoint("BOTTOMLEFT", 8, 10)
    randomButton:RegisterForDrag("LeftButton")
    randomButton:SetScript("OnClick", function() EsteriaCollections_Random(tab == "Pets" and "CRITTER" or "MOUNT") end)
    randomButton:SetScript("OnDragStart", function() EsteriaCollections_Random(tab == "Pets" and "CRITTER" or "MOUNT", true) end)
    local randomEnter = randomButton:GetScript("OnEnter")
    randomButton:SetScript("OnEnter", function(self)
        randomEnter(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(self.label:GetText(), 1, .82, .1)
        GameTooltip:AddLine("Click to summon or dismiss. Drag to an action bar.", 1, 1, 1, true)
        GameTooltip:AddLine("Uses favorites, or your collected companions when none are favorited.", .6, .59, .55, true); GameTooltip:Show()
    end)
    local randomLeave = randomButton:GetScript("OnLeave")
    randomButton:SetScript("OnLeave", function(self) randomLeave(self); GameTooltip:Hide() end)

    grid = CreateFrame("Frame", nil, pane)
    grid:SetPoint("TOPLEFT", sidebar, "TOPRIGHT", 18, -40); grid:SetSize(GRID_W, GRID_H)
    for i = 1, 6 do cards[i] = buildCard(i) end
    search = CreateFrame("EditBox", "EsteriaCosmeticsSearch", pane, "InputBoxTemplate")
    search:SetSize(220, 20); search:SetPoint("BOTTOMLEFT", grid, "TOPLEFT", 6, 12); search:SetAutoFocus(false)
    search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    local placeholder = ES.Text(search, 11, ES.MUTED); placeholder:SetPoint("LEFT", 2, 0); placeholder:SetText("Search appearances")
    search:SetScript("OnTextChanged", function(self) shown(placeholder, self:GetText() == ""); page = 1; refresh() end)
    countText = ES.Text(pane, 11, ES.MUTED); countText:SetPoint("BOTTOMRIGHT", grid, "TOPRIGHT", 0, 16)
    local prev = button(pane, 36, "<", false, false, 26); prev:SetPoint("TOPLEFT", grid, "BOTTOMLEFT", 0, -14)
    prev:SetScript("OnClick", function() page = page - 1; refresh() end)
    local next = button(pane, 36, ">", false, false, 26); next:SetPoint("TOPRIGHT", grid, "BOTTOMRIGHT", 0, -14)
    next:SetScript("OnClick", function() page = page + 1; refresh() end)
    pageText = ES.Text(pane, 12, ES.TEXT); pageText:SetPoint("TOP", grid, "BOTTOM", 0, -20)
    pane.prev, pane.next = prev, next

    stage = CreateFrame("Frame", nil, pane)
    stage:SetPoint("TOPRIGHT", -14, -64); stage:SetPoint("BOTTOMRIGHT", -14, 16); stage:SetWidth(STAGE_W)
    ES.Solid(stage, "BACKGROUND", { 0.05, 0.05, 0.07 }, 1):SetAllPoints()
    local glow = stage:CreateTexture(nil, "BORDER")
    glow:SetTexture(ES.WHITE)
    glow:SetGradientAlpha("VERTICAL", 0.62, 0.46, 0.15, 0.3, 0.62, 0.46, 0.15, 0)
    glow:SetPoint("BOTTOMLEFT", 0, 170); glow:SetPoint("BOTTOMRIGHT", 0, 170); glow:SetHeight(170)
    ES.Border(stage, ES.GOLD, 0.8)
    stage.model = preview(stage); stage.model:SetPoint("TOPLEFT", 4, -4); stage.model:SetPoint("BOTTOMRIGHT", -4, 170)
    stage.model:EnableMouse(true)
    stage.model:SetScript("OnMouseDown", function(self) self.dragX = GetCursorPosition() end)
    stage.model:SetScript("OnMouseUp", function(self) self.dragX = nil end)
    stage.icon = iconFrame(stage, 96); stage.icon:SetPoint("CENTER", stage.model, "CENTER")
    stage.hint = ES.Text(stage, 9, ES.MUTED); stage.hint:SetPoint("BOTTOM", stage.model, "BOTTOM", 0, 6)
    stage.hint:SetText("Drag to rotate")

    local info = CreateFrame("Frame", nil, stage)
    info:SetPoint("BOTTOMLEFT", 1, 1); info:SetPoint("BOTTOMRIGHT", -1, 1); info:SetHeight(168)
    ES.Solid(info, "BACKGROUND", ES.INK, 0.96):SetAllPoints()
    local rule = ES.Solid(info, "ARTWORK", ES.GOLD_DIM, 0.8); rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    stage.kind = ES.Text(info, 10, ES.GOLD_DIM); stage.kind:SetPoint("TOPLEFT", 16, -14)
    stage.name = ES.Text(info, 17, ES.GOLD); stage.name:SetPoint("TOPLEFT", stage.kind, "BOTTOMLEFT", 0, -6)
    stage.name:SetPoint("RIGHT", -16, 0); stage.name:SetJustifyH("LEFT")
    stage.state = ES.Text(info, 11, ES.MUTED); stage.state:SetPoint("TOPLEFT", stage.name, "BOTTOMLEFT", 0, -6)
    stage.note = ES.Text(info, 10, ES.MUTED); stage.note:SetPoint("TOPLEFT", stage.state, "BOTTOMLEFT", 0, -6)
    stage.note:SetPoint("RIGHT", -16, 0); stage.note:SetJustifyH("LEFT")

    apply = button(info, 112, "Apply", true, true, 30); apply:SetPoint("BOTTOMRIGHT", -14, 14)
    -- Mounts: opens the retail-style customizer (Addons/EsteriaMountCustomize) for an owned customizable mount.
    customize = button(info, 112, "Customize", false, false, 30); customize:SetPoint("BOTTOMRIGHT", apply, "TOPRIGHT", 0, 8)
    customize:SetScript("OnClick", function()
        local entry = focus and EsteriaCollections_Companion(focus)
        if entry and EsteriaMountCustomize_Open then EsteriaMountCustomize_Open(entry.creatureID, focus.name) end
    end)
    local customizeEnter = customize:GetScript("OnEnter")
    customize:SetScript("OnEnter", function(self)
        customizeEnter(self)
        GameTooltip:SetOwner(self, "ANCHOR_TOP"); GameTooltip:SetText("Customize", 1, 0.82, 0.1)
        GameTooltip:AddLine(self:IsEnabled() == 1 and "Change this mount's colors and parts." or self.why or "", 1, 1, 1, true)
        GameTooltip:Show()
    end)
    local customizeLeave = customize:GetScript("OnLeave")
    customize:SetScript("OnLeave", function(self) customizeLeave(self); GameTooltip:Hide() end)
    apply:SetScript("PostClick", function()
        if not focus or not owned(focus) or InCombatLockdown() then return end
        if tab == "Backpacks" then send("apply\t" .. focus.id)
        elseif tab == "Weapons" then SendAddonMessage("TMOGC", "A:" .. (focus.slot or 15) .. ":" .. focus.id, "WHISPER", UnitName("player"))
        elseif tab == "Hearthstones" then
            local item = hearthItems[focus.item]
            if item and item.digital and EsteriaInventory then EsteriaInventory:Request(9, item, nil, nil, nil, 0) end
        end
    end)
    remove = button(info, 100, "Remove", true, false, 30); remove:SetPoint("RIGHT", apply, "LEFT", -8, 0)
    remove:SetScript("PostClick", function()
        if InCombatLockdown() then return end
        if tab == "Backpacks" then send("apply\t0")
        elseif tab == "Weapons" and focus then SendAddonMessage("TMOGC", "A:" .. (focus.slot or 15) .. ":0", "WHISPER", UnitName("player")) end
    end)
    favorite = button(info, 96, "Favorite", false, false, 30); favorite:SetPoint("BOTTOMLEFT", 14, 14)
    favorite:SetScript("OnClick", function()
        if not focus or not db then return end
        if kind(focus) == "mount" or kind(focus) == "pet" then
            EsteriaCollections_SetFavorite(focus, not isFavorite(focus), db.favorites)
        else db.favorites[favoriteKey(focus)] = not isFavorite(focus) or nil end
        refresh()
    end)
end

-- Whether a row is the one in use: wings worn, backpack on the cloak, weapon applied, mount ridden, pet out.
local function isActive(row)
    local k = kind(row)
    return owned(row) and (k == "wing" and wingActive(row) or k == "backpack" and selected == row.id
        or k == "weapon" and weaponSelected[row.slot or 15] == row.id or (k == "mount" or k == "pet") and wingActive(row)) or false
end

local function detail(row)
    if not row then return "" end
    if row.weaponType then return row.weaponType end
    if kind(row) == "mount" then return row.mountType or "Mount" end
    if row.sourceName then return row.sourceName end
    return ""
end

refresh = function()
    if not frame or not frame:IsShown() or tab == "Dances" or tab == "Emotes" or tab == "Incarnations" or tab == "Transmog" or tab == "Crates" then return end
    filtered = {}
    local query = search:GetText():lower()
    local collected, all = 0, list()
    local counts = { all = 0, known = 0, favorites = 0, locked = 0 }
    local typeCounts = {}
    local function matches(row)
        return query == "" or row.name:lower():find(query, 1, true) or (row.sourceName or ""):lower():find(query, 1, true)
    end
    local function inFilter(row, has)
        return filter == "all" or filter == "known" and has or filter == "locked" and not has or filter == "favorites" and isFavorite(row)
    end
    for _, row in ipairs(all) do
        local has = owned(row)
        if has then collected = collected + 1 end
        local typed = tab ~= "Mounts" or EsteriaCollections_MountType(row, mountType)
        if matches(row) then
            if typed then
                counts.all = counts.all + 1
                if has then counts.known = counts.known + 1 else counts.locked = counts.locked + 1 end
                if isFavorite(row) then counts.favorites = counts.favorites + 1 end
            end
            if tab == "Mounts" and inFilter(row, has) then
                for _, option in ipairs(MOUNT_TYPES) do
                    if EsteriaCollections_MountType(row, option[1]) then typeCounts[option[1]] = (typeCounts[option[1]] or 0) + 1 end
                end
            end
            if typed and inFilter(row, has) then filtered[#filtered + 1] = row end
        end
    end
    table.sort(filtered, function(a, b) local ka, kb = owned(a) and 1 or 0, owned(b) and 1 or 0; if ka ~= kb then return ka > kb end; return a.name < b.name end)
    local pages = math.max(1, math.ceil(#filtered / 6)); page = math.max(1, math.min(page, pages))
    if not focus then focus = filtered[1] end
    pane.title:SetText(tab)
    pane.note:SetText(SUBTITLES[tab] or "")
    totalText:SetText(("%d of %d collected"):format(collected, #all))
    countText:SetText(("%d shown"):format(#filtered)); pageText:SetText(("Page %d / %d"):format(page, pages))
    pane.prev:Active(page > 1); pane.next:Active(page < pages)

    for i, row in ipairs(railRows) do row:Paint(filters[i][1] == filter, counts[filters[i][1]]) end
    shown(typeHead, tab == "Mounts")
    for _, row in ipairs(typeRows) do
        shown(row, tab == "Mounts")
        row:Paint(row.value == mountType, typeCounts[row.value] or 0)
    end
    shown(randomButton, tab == "Mounts" or tab == "Pets")
    randomButton.label:SetText(tab == "Pets" and "Random Favorite Pet" or "Random Favorite Mount")

    for i, card in ipairs(cards) do
        local row = filtered[(page - 1) * 6 + i]; card.row = row
        if row then
            local has, active = owned(row), isActive(row)
            card.name:SetText(row.name)
            card.state:SetText(not has and "Not collected" or detail(row))
            local playerPreview = kind(row) == "hearth" and type(EsteriaCosmeticsHearthPreview) == "function"
            card.model:ShowRow(row, playerPreview)
            card.icon.icon:SetTexture(row.icon or "Interface\\Icons\\INV_Misc_Bag_10")
            shown(card.icon, not row.model and not row.creature and not playerPreview)
            shown(card.veil, not has)
            shown(card.star, isFavorite(row))
            shown(card.badge, active or not has)
            if active then
                card.badge.text:SetText((kind(row) == "mount" or kind(row) == "pet") and "SUMMONED" or "APPLIED")
                card.badge.bg:SetVertexColor(0.16, 0.32, 0.12); ES.Tint(card.badge.lines, ES.GOOD, 1)
                card.badge.text:SetTextColor(0.85, 1, 0.8)
            else
                card.badge.text:SetText("LOCKED")
                card.badge.bg:SetVertexColor(0.12, 0.12, 0.14); ES.Tint(card.badge.lines, ES.MUTED, 0.8)
                card.badge.text:SetTextColor(unpack(ES.MUTED))
            end
            local on = row == focus
            ES.Tint(card.lines, on and ES.GOLD or ES.GOLD_DIM, on and 1 or 0.7)
            card.bg:SetVertexColor(on and 0.13 or 0.06, on and 0.11 or 0.07, on and 0.07 or 0.10)
            card.name:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
            card:Show()
        else card.model:ShowRow(nil, false); card:Hide() end
    end

    local playerPreview = focus and (kind(focus) == "wing" and type(EsteriaCosmeticsWingPreview) == "function"
        or kind(focus) == "hearth" and type(EsteriaCosmeticsHearthPreview) == "function"
        or kind(focus) == "backpack" and type(EsteriaCosmeticsPreview) == "function") or false
    stage.model:ShowRow(focus, playerPreview)
    stage.icon.icon:SetTexture(focus and focus.icon or nil)
    local iconOnly = focus and not focus.model and not focus.creature and not playerPreview
    shown(stage.icon, iconOnly); shown(stage.hint, focus and not iconOnly)
    stage.kind:SetText(focus and ((KINDS[kind(focus)] or tab:upper()) .. (detail(focus) ~= "" and ("  ·  " .. detail(focus):upper()) or "")) or "")
    stage.name:SetText(focus and focus.name or "Nothing here yet")
    local state = not focus and "" or not owned(focus) and "Not collected" or tab == "Wings" and wingActive(focus) and "Applied" or tab == "Backpacks" and selected == focus.id and "Applied to cloak" or tab == "Mounts" and wingActive(focus) and "Summoned" or tab == "Weapons" and weaponSelected[focus.slot or 15] == focus.id and "Applied" or "Collected"
    if focus and tab == "Pets" and wingActive(focus) then state = "Summoned" end
    if focus and tab == "Hearthstones" and GetBindLocation then state = state .. "  ·  " .. GetBindLocation() end
    stage.state:SetText(focus and owned(focus) and ("|cff8cd97f" .. state .. "|r") or state)
    stage.note:SetText(focus and (focus.sourceName and ("Source: " .. focus.sourceName) or focus.item and not owned(focus) and ("Requires " .. focus.name)) or "")
    favorite:Active(focus ~= nil); favorite.label:SetText(focus and isFavorite(focus) and "Unfavorite" or "Favorite")
    -- Customize: owned mounts the client's MountCustomize extension has options for; the stage wears the saved look.
    local entry = tab == "Mounts" and focus and EsteriaCollections_Companion(focus)
    local look = entry and entry.creatureID and EsteriaMountCustomize_GetDisplay and EsteriaMountCustomize_GetDisplay(entry.creatureID)
    local customizable = look and EsteriaMountCustomize_GetOptions(look) ~= nil
    shown(customize, tab == "Mounts")
    customize:Active(customizable and EsteriaMountCustomize_Open ~= nil)
    customize.why = not (focus and owned(focus)) and "Collect this mount to customize it." or "This mount has no customization options."
    stage.model.onReady = customizable and function(model)
        EsteriaMountCustomize_Preview(model, look, EsteriaMountCustomize_GetLook(look))
    end or nil
    -- Secure action attributes cannot change in combat; the server also validates ownership and combat state.
    if not InCombatLockdown() then
        apply:SetAttribute("type",nil); remove:SetAttribute("type",nil)
        apply:SetAttribute("spell",nil);apply:SetAttribute("macrotext",nil);remove:SetAttribute("macrotext",nil)
        apply.label:SetText((tab=="Mounts" or tab=="Pets") and "Summon" or tab=="Hearthstones" and "Use" or "Apply")
        remove.label:SetText(tab=="Mounts" and "Dismount" or tab=="Pets" and "Dismiss" or "Remove")
        shown(remove,tab~="Hearthstones")
        if (tab=="Mounts" or tab=="Pets") and focus and owned(focus) then
            local entry=EsteriaCollections_Companion(focus)
            if entry then
                apply:SetAttribute("type","macro")
                apply:SetAttribute("macrotext","/stopmacro [combat]\n/run CallCompanion(\""..(tab=="Pets" and "CRITTER" or "MOUNT").."\","..entry.index..")")
            else
                apply:SetAttribute("type","macro")
                apply:SetAttribute("macrotext","/stopmacro [combat]\n/run CastSpellByID("..focus.spell..")")
            end
        elseif tab=="Hearthstones" and focus and owned(focus) and not hearthItems[focus.item].digital then
            apply:SetAttribute("type","macro");apply:SetAttribute("macrotext","/stopmacro [combat]\n/use item:"..focus.item)
        end
        if tab=="Mounts" then remove:SetAttribute("type","macro");remove:SetAttribute("macrotext","/stopmacro [combat]\n/dismount") end
        if tab=="Pets" then remove:SetAttribute("type","macro");remove:SetAttribute("macrotext","/stopmacro [combat]\n/run DismissCompanion(\"CRITTER\")") end
        if tab=="Wings" and focus and owned(focus) then
            local name=GetSpellInfo(focus.spell)
            if name then
                apply:SetAttribute("type","macro"); apply:SetAttribute("macrotext","/stopmacro [combat]\n/cast [@player] "..name)
                remove:SetAttribute("type","macro"); remove:SetAttribute("macrotext","/stopmacro [combat]\n/cancelaura "..name)
            end
        end
        apply:Active(focus and owned(focus)); remove:Active(tab=="Wings" and focus and wingActive(focus) or tab=="Backpacks" and selected~=0 or tab=="Mounts" and IsMounted and IsMounted() or tab=="Pets" and focus and wingActive(focus) or tab=="Weapons" and focus and weaponSelected[focus.slot or 15]~=nil)
    end
end

local function build()
    frame=CreateFrame("Frame","EsteriaCosmeticsFrame",UIParent); frame:SetSize(W,H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true); frame:SetClampedToScreen(true)
    frame:RegisterForDrag("LeftButton"); frame:SetScript("OnDragStart",frame.StartMoving); frame:SetScript("OnDragStop",frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames,"EsteriaCosmeticsFrame")
    ES.Solid(frame,"BACKGROUND",ES.INK,0.98):SetAllPoints(); headerBand(frame,42); ES.Border(frame,ES.GOLD)
    local divider=ES.Solid(frame,"ARTWORK",ES.GOLD_DIM,0.6); divider:SetPoint("TOPLEFT",1,-41); divider:SetPoint("TOPRIGHT",-1,-41); divider:SetHeight(1)
    local title=ES.Text(frame,18,ES.GOLD); title:SetPoint("TOPLEFT",20,-12); title:SetText("Collections")
    -- Crates is the EsteriaCrates store, embedded like Dances (EsteriaEmotes) and Emotes (its spell emotes).
    for i,name in ipairs({"Wings","Backpacks","Dances","Emotes","Hearthstones","Weapons","Mounts","Pets","Incarnations","Transmog","Crates"}) do
        local b=tabButton(frame,86,name); b:SetPoint("TOPLEFT",160+(i-1)*88,-6); b:SetScript("OnClick",function() PlaySound("igMainMenuOptionCheckBoxOn"); selectTab(name) end); tabs[name]=b
    end
    local close=CreateFrame("Button",nil,frame,"UIPanelCloseButton"); close:SetPoint("TOPRIGHT",2,2)
    -- Corner grip: scales the whole window (every tab and embedded window keeps its layout), top-left stays put.
    local MIN_SCALE,MAX_SCALE=0.55,1.4
    if db and db.scale then frame:SetScale(db.scale) end
    local grip=CreateFrame("Button",nil,frame); grip:SetSize(18,18); grip:SetPoint("BOTTOMRIGHT",-3,3)
    grip:SetFrameLevel(frame:GetFrameLevel()+50)
    grip:SetNormalTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Up")
    grip:SetHighlightTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Highlight")
    grip:SetPushedTexture("Interface\\ChatFrame\\UI-ChatIM-SizeGrabber-Down")
    grip:SetScript("OnMouseDown",function(self,mouse)
        if mouse~="LeftButton" then return end
        local eff=frame:GetEffectiveScale()
        self.left,self.top=frame:GetLeft()*eff,frame:GetTop()*eff   -- screen pixels
        self:SetScript("OnUpdate",function()
            local x,y=GetCursorPosition()
            -- the larger of the width/height drag decides, so the window keeps its proportions
            local wanted=math.max((x-self.left)/W,(self.top-y)/H)/UIParent:GetEffectiveScale()
            frame:SetScale(math.max(MIN_SCALE,math.min(MAX_SCALE,wanted)))
            local now=frame:GetEffectiveScale()
            frame:ClearAllPoints(); frame:SetPoint("TOPLEFT",UIParent,"BOTTOMLEFT",self.left/now,self.top/now)
        end)
    end)
    grip:SetScript("OnMouseUp",function(self)
        self:SetScript("OnUpdate",nil)
        if db then db.scale=frame:GetScale() end
    end)
    grip:SetScript("OnEnter",function(self)
        GameTooltip:SetOwner(self,"ANCHOR_RIGHT"); GameTooltip:SetText("Drag to resize",1,0.82,0.1)
        GameTooltip:AddLine("Right-click to reset the size.",0.6,0.59,0.55); GameTooltip:Show()
    end)
    grip:SetScript("OnLeave",function() GameTooltip:Hide() end)
    grip:RegisterForClicks("RightButtonUp")
    grip:SetScript("OnClick",function() frame:SetScale(1); if db then db.scale=nil end end)
    content=CreateFrame("Frame",nil,frame); content:SetSize(W,CONTENT_H); content:SetPoint("BOTTOMLEFT")
    buildPane()
    local hide=frame.Hide
    frame.Hide=function(self)
        if ManastormTransmog_Hide and not ManastormTransmog_Hide() then return end
        hide(self)
    end
    frame:SetScript("OnShow",function() learnedSpells=spellbook();EsteriaCollections_RefreshCompanions(); send("sync"); selectTab(tab) end)
    frame:SetScript("OnHide",function()
        if EsteriaEmotesFrame then EsteriaEmotesFrame:Hide() end
        if EsteriaSpellEmotesFrame then EsteriaSpellEmotesFrame:Hide() end
        if EsteriaIncarnationsFrame then EsteriaIncarnationsFrame:Hide() end
        if EsteriaCratesStore then EsteriaCratesStore:Hide() end
        GameTooltip:Hide()
    end)
end

selectTab=function(name)
    if InCombatLockdown() then UIErrorsFrame:AddMessage("Leave combat to change cosmetic selection.",1,0.1,0.1); return end
    if tab=="Transmog" and name~=tab and ManastormTransmog_Hide and not ManastormTransmog_Hide() then return end
    tab=name; page=1; focus=nil
    for label,b in pairs(tabs) do b:SetSelected(label==tab) end
    if EsteriaEmotesFrame then EsteriaEmotesFrame:Hide() end
    if EsteriaIncarnationsFrame then EsteriaIncarnationsFrame:Hide() end
    if EsteriaCratesStore then EsteriaCratesStore:Hide() end
    if EsteriaSpellEmotesFrame then EsteriaSpellEmotesFrame:Hide() end
    shown(pane,tab~="Dances" and tab~="Emotes" and tab~="Incarnations" and tab~="Transmog" and tab~="Crates")
    if tab=="Dances" then
        if EsteriaEmotes_Embed then EsteriaEmotes_Embed(content) end
    elseif tab=="Emotes" then
        if EsteriaSpellEmotes_Embed then EsteriaSpellEmotes_Embed(content) end
    elseif tab=="Incarnations" then
        if EsteriaIncarnations_Embed then EsteriaIncarnations_Embed(content) end
    elseif tab=="Transmog" then
        if ManastormTransmog_Embed then ManastormTransmog_Embed(content) end
    elseif tab=="Crates" then
        if EsteriaCrates_Embed then EsteriaCrates_Embed(content) end
    else search:SetText(""); refresh() end
end
function EsteriaCosmetics_Show(requested)
    if frame and frame:IsShown() and tab==requested then return end
    EsteriaCosmetics_Toggle(requested)
end

function EsteriaCosmetics_Toggle(requested)
    if type(requested)~="string" then requested=nil end
    if InCombatLockdown() then UIErrorsFrame:AddMessage("Leave combat to open Cosmetics.",1,0.1,0.1); return end
    if not frame then build() end
    if requested and not tabs[requested] then return end
    if requested then
        if frame:IsShown() and tab==requested then frame:Hide(); return end
        if not frame:IsShown() then tab=requested;frame:Show() else selectTab(requested) end
    elseif frame:IsShown() then frame:Hide() else frame:Show() end
end
SLASH_ESTERIACOSMETICS1="/cosmetics"
SLASH_ESTERIACOSMETICS2="/collections"
SlashCmdList.ESTERIACOSMETICS=function() EsteriaCosmetics_Toggle() end

-- DragonUI supplies its own strip button. Keep the same entry point on the stock micro bar.
if not _G.CosmeticsMicroButton and MainMenuBarArtFrame then
    local anchor=_G.NeighborhoodMicroButton or HelpMicroButton
    local b=CreateFrame("Button","CosmeticsMicroButton",MainMenuBarArtFrame,"MainMenuBarMicroButton")
    b:SetPoint("BOTTOMLEFT",anchor,"BOTTOMRIGHT",-3,0)
    b:SetNormalTexture("Interface\\Buttons\\UI-MicroButton-Spellbook-Up")
    b:SetPushedTexture("Interface\\Buttons\\UI-MicroButton-Spellbook-Down")
    b:SetDisabledTexture("Interface\\Buttons\\UI-MicroButton-Spellbook-Disabled")
    b:SetHighlightTexture("Interface\\Buttons\\UI-MicroButton-Hilight")
    b:SetScript("OnClick",function() EsteriaCosmetics_Toggle() end)
    b:SetScript("OnEnter",function(self)
        GameTooltip:SetOwner(self,"ANCHOR_LEFT"); GameTooltip:SetText("Collections",1,0.82,0.1)
        GameTooltip:AddLine("Your mounts, hearthstones, weapons, wings, backpacks and emotes.",1,1,1,true); GameTooltip:Show()
    end)
    b:SetScript("OnLeave",function() GameTooltip:Hide() end)
    hooksecurefunc("VehicleMenuBar_MoveMicroButtons",function() b:SetParent(anchor:GetParent()) end)
end

local events=CreateFrame("Frame")
for _,event in ipairs({"PLAYER_LOGIN","CHAT_MSG_ADDON","SPELLS_CHANGED","UNIT_AURA","PLAYER_REGEN_ENABLED","PLAYER_EQUIPMENT_CHANGED","BAG_UPDATE","COMPANION_LEARNED","COMPANION_UNLEARNED","COMPANION_UPDATE"}) do events:RegisterEvent(event) end
events:SetScript("OnEvent",function(_,event,prefix,message,_,sender)
    if event=="PLAYER_LOGIN" then
        EsteriaCosmeticsDB=EsteriaCosmeticsDB or {}; db=EsteriaCosmeticsDB; db.favorites=db.favorites or {}
        EsteriaCollections_RefreshCompanions();EsteriaCollections_MigrateFavorites(db.favorites)
        learnedSpells=spellbook(); send("sync"); return
    elseif event=="SPELLS_CHANGED" or event=="COMPANION_LEARNED" or event=="COMPANION_UNLEARNED" then
        learnedSpells=spellbook();EsteriaCollections_RefreshCompanions()
        if db then EsteriaCollections_MigrateFavorites(db.favorites) end
        send("sync");refresh();return
    elseif event=="COMPANION_UPDATE" then
        EsteriaCollections_RefreshCompanions()
        if db then EsteriaCollections_MigrateFavorites(db.favorites) end
        refresh();return
    elseif event=="BAG_UPDATE" then if frame and frame:IsShown() and tab=="Hearthstones" then send("sync") end;return
    elseif event=="UNIT_AURA" then if prefix=="player" then refresh() end; return
    elseif event=="PLAYER_REGEN_ENABLED" then refresh(); return end
    if event=="PLAYER_EQUIPMENT_CHANGED" then
        if frame and frame:IsShown() and (tab=="Backpacks" or tab=="Weapons") then
            stage.model.row=nil; refresh(); send("sync")
        end
        return
    end
    if prefix=="TMOGS" and sender==UnitName("player") then
        if message:sub(1,2)=="E:" then
            for slot,_,fake in message:sub(3):gmatch("(%d+),(%d+),(%d+),[^;]*") do
                local equipmentSlot=tonumber(slot);local appearance=tonumber(fake)
                if equipmentSlot==15 or equipmentSlot==16 then weaponSelected[equipmentSlot]=appearance~=0 and appearance or nil end
            end
            refresh()
        end
        return
    end
    if prefix~=PREFIX or sender~=UnitName("player") then return end
    local kind,a=strsplit("\t",message)
    if kind=="reset" then syncing={known={},weapons={},mounts={},hearths={},selected=selected,weaponSelected={}}
    elseif kind=="known" and syncing then for value in (a or ""):gmatch("%d+") do syncing.known[tonumber(value)]=true end
    elseif kind=="weapons" and syncing then for value in (a or ""):gmatch("%d+") do syncing.weapons[tonumber(value)]=true end
    elseif kind=="mounts" and syncing then for value in (a or ""):gmatch("%d+") do syncing.mounts[tonumber(value)]=true end
    elseif kind=="hearth" and syncing then
        local item,guid,digital=(message.."\t"):match("^hearth\t(%d+)\t(%d+)\t(%d+)\t")
        if item then syncing.hearths[tonumber(item)]={entry=tonumber(item),guid=tonumber(guid),digital=digital=="1"} end
    elseif kind=="weaponselected" and syncing then
        local slot,entry=(a or ""):match("^(%d+),(%d+)$")
        if slot and tonumber(entry)~=0 then syncing.weaponSelected[tonumber(slot)]=tonumber(entry) end
    elseif kind=="selected" then if syncing then syncing.selected=tonumber(a) or 0 else selected=tonumber(a) or 0 end
    elseif kind=="done" and syncing then known,selected=syncing.known,syncing.selected;weaponKnown,mountKnown,hearthItems,weaponSelected=syncing.weapons,syncing.mounts,syncing.hearths,syncing.weaponSelected; syncing=nil
    elseif kind=="err" then UIErrorsFrame:AddMessage(a or "That cosmetic is unavailable.",1,0.1,0.1) end
    refresh()
end)
