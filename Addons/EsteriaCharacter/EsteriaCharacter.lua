-- Esteria's character sheet, in the PvP/Collections/Quest Log look. Four tabs share one shell:
--   Character   gear around a turnable model (item level, gems, enchant, durability), and a side panel that switches
--               between Stats, Titles and Gear sets
--   Reputation  faction groups, a searchable list with standing bars, watch / at war / inactive
--   Skills      skill categories with rank bars, descriptions, abandon
--   Currency    gold and every currency, with the backpack display toggle
-- On a classless realm the stats come from ClasslessWildcard_API where Blizzard's sheet would guess from the class.
local ES = EsteriaStyle
local W, H = 720, 520
local RAIL_W = 160
local LIST_W = W - 14 - RAIL_W - 10 - 14
local PANE_H = H - 42 - 20
local ROW_H, STRIP_H = 36, 84
local SLOT, STEP = 36, 44

local frame, tabs, panes = nil, {}, {}
local current = "Character"
local db
local dirty = true
local selected = { faction = nil, skill = nil, currency = nil, side = "Stats" }

-- Player settings (the settings popout); every default is the layout as designed.
local DEFAULTS = {
    room = "auto", dim = 0.28, bottomFade = true,
    ilvlNumbers = true, ilvlQuality = true, qualityBorders = true, enchantDots = true, durability = true,
    avgIlvl = true, modelHint = true,
    autoRotate = false, rotateSpeed = 0.4, rememberModel = false,
    hideZero = false, scale = 1, animate = true, lock = false,
}
local function opt(key)
    local v = db and db.settings and db.settings[key]
    if v == nil then return DEFAULTS[key] end
    return v
end
local function sectionShown(title) return not (db and db.settings and db.settings.hiddenSections and db.settings.hiddenSections[title]) end

-- Dressing rooms: Blizzard's 3.3.5 race rooms (four tiles each) and the retail class rooms shipped in Rooms\.
local RACE_ROOMS = { { "Human", "Human" }, { "Dwarf", "Dwarf & Gnome" }, { "NightElf", "Night Elf" }, { "Orc", "Orc & Troll" },
                     { "Scourge", "Undead" }, { "Tauren", "Tauren" }, { "BloodElf", "Blood Elf" }, { "Draenei", "Draenei" },
                     { "DeathKnight", "Acherus" } }
local CLASS_ROOMS = { { "warrior", "Warrior" }, { "paladin", "Paladin" }, { "hunter", "Hunter" }, { "rogue", "Rogue" },
                      { "priest", "Priest" }, { "deathknight", "Death Knight" }, { "shaman", "Shaman" }, { "mage", "Mage" },
                      { "warlock", "Warlock" }, { "monk", "Monk" }, { "druid", "Druid" }, { "demonhunter", "Demon Hunter" },
                      { "evoker", "Evoker" } }
local ROOM_ART = "Interface\\AddOns\\EsteriaCharacter\\Rooms\\"

-- Fill a w x h box with a retail room's painted area (480 x 502 of its 512 square), cropping the excess like "cover".
local function coverCrop(tex, w, h)
    local aw, ah, artRatio = 480 / 512, 502 / 512, 480 / 502
    local l, r, t, b = 0, aw, 0, ah
    if w / h < artRatio then
        local keep = (w / h) / artRatio * aw
        l = (aw - keep) / 2; r = l + keep
    else
        local keep = artRatio / (w / h) * ah
        t = (ah - keep) / 2; b = t + keep
    end
    tex:SetTexCoord(l, r, t, b)
end

-- DragonUI's character panel would restyle the stock window this one replaces; keep it out (re-enable in DragonUI).
do
    local dui = _G.DragonUI
    local modules = dui and dui.db and dui.db.profile and dui.db.profile.modules
    if modules and modules.characterpanel then modules.characterpanel.enabled = false end
end

local function shown(region, on) if on then region:Show() else region:Hide() end end
local function pct(v) return ("%.2f%%"):format(tonumber(v) or 0) end
local function CW() return _G.ClasslessWildcard_API end
local function classless() local cw = CW(); return cw and cw.state and cw.state.universalResources == 1 end

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

-- Asks once more before doing something that can't be undone.
local function confirmButton(parent, width, text, action)
    local b = button(parent, width, text)
    b.label:SetTextColor(unpack(ES.BAD))
    b.text = text
    b:SetScript("OnClick", function(self)
        if self.armed then self.armed = nil; self.label:SetText(self.text); action()
        else self.armed = GetTime(); self.label:SetText("Sure?"); PlaySound("igMainMenuOptionCheckBoxOn") end
    end)
    b:SetScript("OnUpdate", function(self)
        if self.armed and GetTime() - self.armed > 3 then self.armed = nil; self.label:SetText(self.text) end
    end)
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

local function railRow(parent, onClick)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(RAIL_W - 12, 24)
    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
    row.label = ES.Text(row, 11, ES.TEXT); row.label:SetPoint("LEFT", 12, 0); row.label:SetPoint("RIGHT", -34, 0)
    row.label:SetJustifyH("LEFT")
    row.count = ES.Text(row, 10, ES.MUTED); row.count:SetPoint("RIGHT", -8, 0)
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

local function editBox(parent, width, hint, onEnter, maxLetters)
    local e = CreateFrame("EditBox", nil, parent)
    e:SetSize(width, 26); e:SetAutoFocus(false); e:SetFontObject(ChatFontNormal); e:SetTextInsets(10, 8, 0, 0)
    e:SetMaxLetters(maxLetters or 255)
    ES.Solid(e, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    e.lines = ES.Border(e, ES.GOLD_DIM)
    e.hint = ES.Text(e, 11, ES.MUTED); e.hint:SetPoint("LEFT", 10, 0); e.hint:SetText(hint)
    e:SetScript("OnEscapePressed", function(self) if self:GetText() ~= "" then self:SetText("") else self:ClearFocus() end end)
    e:SetScript("OnEnterPressed", function(self) if onEnter then onEnter(self, self:GetText()) else self:ClearFocus() end end)
    e:SetScript("OnEditFocusGained", function(self) ES.Tint(self.lines, ES.GOLD, 1) end)
    e:SetScript("OnEditFocusLost", function(self) ES.Tint(self.lines, ES.GOLD_DIM, 1) end)
    e:HookScript("OnTextChanged", function(self) shown(self.hint, self:GetText() == "") end)
    return e
end

local function pill(parent)
    local t = CreateFrame("Frame", nil, parent); t:SetHeight(18)
    t.bg = ES.Solid(t, "BACKGROUND", ES.GOLD, 0.16); t.bg:SetAllPoints()
    t.lines = ES.Border(t, ES.GOLD, 0.8)
    t.text = ES.Text(t, 9, ES.GOLD); t.text:SetPoint("CENTER", 0, 1)
    t.Set = function(self, text, color)
        if not text then self:Hide(); return end
        self.text:SetText(text:upper()); self.text:SetTextColor(unpack(color))
        self.bg:SetVertexColor(color[1], color[2], color[3], 0.14); ES.Tint(self.lines, color, 0.8)
        self:SetWidth(self.text:GetStringWidth() + 14); self:Show()
    end
    return t
end

local function bar(parent, height)
    local bg = ES.Solid(parent, "ARTWORK", { 0, 0, 0 }, 0.6); bg:SetHeight(height)
    local fill = ES.Solid(parent, "OVERLAY", ES.GOLD, 1); fill:SetPoint("TOPLEFT", bg, "TOPLEFT"); fill:SetHeight(height)
    return bg, fill
end

local function setBar(bg, fill, value, color, width)
    fill:SetWidth(math.max(1, (width or bg:GetWidth() or 0) * math.max(0, math.min(1, value or 0))))
    local c = color or ES.GOLD
    fill:SetVertexColor(c[1], c[2], c[3], 1)
end

-- Fixed rows over `data` with a slim gold scrollbar; fill(row, item) paints one row.
local function vlist(parent, width, rowH, count, make, fill)
    local l = CreateFrame("Frame", nil, parent)
    l:SetSize(width, rowH * count)
    l.rows, l.data, l.offset = {}, {}, 0
    for i = 1, count do
        local row = make(l, width - 12, rowH)
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

-- A list row with a progress bar under its name (reputation, skills) or an icon (currency).
local function barRow(parent, w, h, onClick)
    local r = CreateFrame("Button", nil, parent); r:SetSize(w, h - 2)
    r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
    r.edge = ES.Solid(r, "ARTWORK", ES.GOLD, 0); r.edge:SetPoint("TOPLEFT"); r.edge:SetPoint("BOTTOMLEFT"); r.edge:SetWidth(3)
    r.icon = r:CreateTexture(nil, "ARTWORK"); r.icon:SetSize(24, 24); r.icon:SetPoint("LEFT", 8, 0)
    r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    r.name = ES.Text(r, 11, ES.TEXT); r.name:SetPoint("TOPLEFT", 12, -5); r.name:SetPoint("RIGHT", -150, 0); r.name:SetJustifyH("LEFT")
    r.right = ES.Text(r, 10, ES.MUTED); r.right:SetPoint("TOPRIGHT", -10, -6); r.right:SetJustifyH("RIGHT")
    r.barBg, r.barFill = bar(r, 4); r.barBg:SetPoint("BOTTOMLEFT", 12, 7); r.barBg:SetPoint("BOTTOMRIGHT", -10, 7)
    r.header = ES.Text(r, 10, ES.GOLD_DIM); r.header:SetPoint("BOTTOMLEFT", 12, 6)
    r.rule = ES.Solid(r, "ARTWORK", ES.GOLD_DIM, 0.5); r.rule:SetHeight(1)
    r.rule:SetPoint("BOTTOMLEFT", 6, 2); r.rule:SetPoint("BOTTOMRIGHT", -6, 2)
    r:SetScript("OnEnter", function(self) if not self.on and not self.isHeader then self.wash:SetAlpha(0.06) end end)
    r:SetScript("OnLeave", function(self) if not self.on then self.wash:SetAlpha(0) end end)
    r:SetScript("OnClick", function(self) if not self.isHeader then PlaySound("igMainMenuOptionCheckBoxOn"); onClick(self.item) end end)
    -- kind: "header" | "bar" | "icon"
    r.Paint = function(self, kind, on, name, nameColor, right, value, color, icon)
        self.isHeader, self.on = kind == "header", on
        self.wash:SetAlpha(on and 0.12 or 0); self.edge:SetAlpha(on and 1 or 0)
        shown(self.header, kind == "header"); shown(self.rule, kind == "header")
        shown(self.name, kind ~= "header"); shown(self.right, kind ~= "header")
        shown(self.barBg, kind == "bar"); shown(self.barFill, kind == "bar"); shown(self.icon, kind == "icon")
        if kind == "header" then self.header:SetText(name:upper()); return end
        self.name:ClearAllPoints()
        if kind == "icon" then
            self.icon:SetTexture(icon); self.name:SetPoint("LEFT", 40, 0)
        else
            self.name:SetPoint("TOPLEFT", 12, -5)
        end
        self.name:SetPoint("RIGHT", -150, 0)
        self.name:SetText(name); self.name:SetTextColor(unpack(on and ES.GOLD or nameColor or ES.TEXT))
        self.right:SetText(right or "")
        self.right:ClearAllPoints()
        if kind == "icon" then self.right:SetPoint("RIGHT", -12, 0) else self.right:SetPoint("TOPRIGHT", -10, -6) end
        if kind == "bar" then setBar(self.barBg, self.barFill, value, color, self:GetWidth() - 22) end
    end
    return r
end

-- The selected entry's strip along the bottom of a list panel: icon, name, standing pill, bar, description, buttons.
local function strip(parent)
    local d = CreateFrame("Frame", nil, parent)
    d:SetPoint("BOTTOMLEFT", 1, 1); d:SetPoint("BOTTOMRIGHT", -1, 1); d:SetHeight(STRIP_H)
    ES.Solid(d, "BACKGROUND", { 0.09, 0.10, 0.14 }, 1):SetAllPoints()
    local rule = ES.Solid(d, "ARTWORK", ES.GOLD_DIM, 0.5); rule:SetPoint("TOPLEFT"); rule:SetPoint("TOPRIGHT"); rule:SetHeight(1)
    d.icon = CreateFrame("Frame", nil, d); d.icon:SetSize(36, 36); d.icon:SetPoint("TOPLEFT", 10, -10)
    d.icon.tex = d.icon:CreateTexture(nil, "ARTWORK")
    d.icon.tex:SetPoint("TOPLEFT", 1, -1); d.icon.tex:SetPoint("BOTTOMRIGHT", -1, 1); d.icon.tex:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    d.icon.lines = ES.Border(d.icon, ES.GOLD)
    d.title = ES.Text(d, 13, ES.GOLD); d.title:SetPoint("TOPLEFT", 56, -10); d.title:SetJustifyH("LEFT")
    d.pill = pill(d); d.pill:SetPoint("LEFT", d.title, "RIGHT", 8, 0)
    d.sub = ES.Text(d, 10, ES.MUTED); d.sub:SetPoint("TOPLEFT", d.title, "BOTTOMLEFT", 0, -3)
    d.barBg, d.barFill = bar(d, 5); d.barBg:SetPoint("TOPLEFT", 56, -44); d.barBg:SetWidth(250)
    d.barText = ES.Text(d, 10, ES.MUTED); d.barText:SetPoint("LEFT", d.barBg, "RIGHT", 8, 0)
    d.desc = ES.Text(d, 10, ES.TEXT); d.desc:SetPoint("TOPLEFT", 56, -54); d.desc:SetWidth(LIST_W - 56 - 230)
    d.desc:SetJustifyH("LEFT"); d.desc:SetJustifyV("TOP"); d.desc:SetHeight(26)
    d.none = ES.Text(d, 11, ES.MUTED); d.none:SetPoint("CENTER")
    d.lines = {}
    d.Begin = function(self, empty)
        self.lines = {}
        self.desc:SetText(""); self.pill:Hide(); self.barBg:Hide(); self.barFill:Hide(); self.barText:Hide()
        shown(self.none, empty ~= nil); if empty then self.none:SetText(empty) end
        for _, region in ipairs({ self.icon, self.title, self.sub, self.desc }) do shown(region, empty == nil) end
        self.title:SetTextColor(unpack(ES.GOLD))
    end
    d.Bar = function(self, value, color, text)
        setBar(self.barBg, self.barFill, value, color, 250)
        self.barText:SetText(text or "")
        self.barBg:Show(); self.barFill:Show(); self.barText:Show()
    end
    d.Line = function(self, label, value, color)
        if not value or value == "" then return end
        local c = color or ES.TEXT
        self.lines[#self.lines + 1] = ("|cff%02x%02x%02x%s|r"):format(c[1] * 255, c[2] * 255, c[3] * 255, value)
        self.desc:SetText(table.concat(self.lines, "  ·  "))
    end
    -- Buttons on the right, two rows of two.
    local bw = 104
    d.buttons = {}
    for i = 1, 4 do
        d.buttons[i] = button(d, bw, "", i == 1, 22)
        d.buttons[i]:SetPoint("TOPRIGHT", -10 - ((i - 1) % 2 == 0 and bw + 6 or 0), -12 - (i <= 2 and 0 or 28))
    end
    d.confirm = confirmButton(d, bw, "", function() if d.onConfirm then d.onConfirm() end end)
    d.confirm:SetHeight(22)
    d.Actions = function(self, list, danger)
        for i, b in ipairs(self.buttons) do
            local a = list[i]
            if a then b.label:SetText(a[1]); b:SetScript("OnClick", a[2]); b:Active(a[3] ~= false); b:Show() else b:Hide() end
        end
        if danger and #list < 4 then
            self.confirm:ClearAllPoints(); self.confirm:SetAllPoints(self.buttons[#list + 1])
            self.confirm.text = danger[1]; self.confirm.label:SetText(danger[1]); self.confirm.armed = nil
            self.onConfirm = danger[2]; self.confirm:Show()
        else
            self.confirm:Hide()
        end
    end
    return d
end

-- Character: equipment ------------------------------------------------------------------------------------------------
local LEFT = { "HeadSlot", "NeckSlot", "ShoulderSlot", "BackSlot", "ChestSlot", "ShirtSlot", "TabardSlot", "WristSlot" }
local RIGHT = { "HandsSlot", "WaistSlot", "LegsSlot", "FeetSlot", "Finger0Slot", "Finger1Slot", "Trinket0Slot", "Trinket1Slot" }
local WEAPONS = { "MainHandSlot", "SecondaryHandSlot", "RangedSlot" }
local NO_ILVL = { ShirtSlot = true, TabardSlot = true }
local slots = {}

local function itemDetails(link)
    if not link then return end
    local _, _, quality, ilvl, _, _, _, _, equipLoc = GetItemInfo(link)
    local enchant, gem1, gem2, gem3 = link:match("item:%d+:(%d*):(%d*):(%d*):(%d*)")
    local gems = 0
    for _, g in ipairs({ gem1, gem2, gem3 }) do if tonumber(g) and tonumber(g) > 0 then gems = gems + 1 end end
    return { quality = quality, ilvl = ilvl, equipLoc = equipLoc, enchanted = tonumber(enchant) and tonumber(enchant) > 0,
             gems = gems }
end

-- Average equipped item level; a two-hander counts for both hands, shirt and tabard don't count.
local function averageItemLevel()
    local total, count = 0, 0
    local twoHand
    local all = {}
    for _, list in ipairs({ LEFT, RIGHT, WEAPONS }) do for _, name in ipairs(list) do all[#all + 1] = name end end
    for _, name in ipairs(all) do
        if not NO_ILVL[name] then
            local id = GetInventorySlotInfo(name)
            local d = itemDetails(GetInventoryItemLink("player", id))
            if d and d.ilvl then
                total, count = total + d.ilvl, count + 1
                if name == "MainHandSlot" and d.equipLoc == "INVTYPE_2HWEAPON" then twoHand = d.ilvl end
            elseif name == "SecondaryHandSlot" and twoHand then
                total, count = total + twoHand, count + 1
            elseif name ~= "RangedSlot" then
                count = count + 1 -- an empty slot drags the average down, as an inspector would see it
            end
        end
    end
    return count > 0 and total / count or 0
end

local function makeSlot(parent, name)
    local id, empty = GetInventorySlotInfo(name)
    local s = CreateFrame("Button", nil, parent)
    s:SetSize(SLOT, SLOT)
    s.id, s.slotName, s.emptyTexture = id, name, empty
    s:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    s:RegisterForDrag("LeftButton")
    ES.Solid(s, "BACKGROUND", ES.INK, 1):SetAllPoints()
    s.icon = s:CreateTexture(nil, "ARTWORK"); s.icon:SetPoint("TOPLEFT", 1, -1); s.icon:SetPoint("BOTTOMRIGHT", -1, 1)
    s.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    s.lines = ES.Border(s, ES.GOLD_DIM)
    s.broken = ES.Solid(s, "OVERLAY", ES.BAD, 0); s.broken:SetAllPoints(s.icon)
    s.ilvl = ES.Text(s, 10, ES.TEXT); s.ilvl:SetFont(ES.FONT, 10, "OUTLINE"); s.ilvl:SetPoint("BOTTOMRIGHT", -2, 2)
    s.enchant = ES.Solid(s, "OVERLAY", ES.GOOD, 0); s.enchant:SetSize(5, 5); s.enchant:SetPoint("TOPLEFT", 3, -3)
    s:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if not GameTooltip:SetInventoryItem("player", self.id) then
            GameTooltip:SetText(_G[strupper(self.slotName)] or self.slotName, 1, 1, 1)
        end
        GameTooltip:Show()
    end)
    s:SetScript("OnLeave", function() GameTooltip:Hide() end)
    s:SetScript("OnClick", function(self, mouse)
        local link = GetInventoryItemLink("player", self.id)
        if link and IsModifiedClick() and HandleModifiedItemClick(link) then return end
        if mouse == "RightButton" then
            if link and SocketInventoryItem then SocketInventoryItem(self.id) end
            return
        end
        PickupInventoryItem(self.id)
    end)
    s:SetScript("OnDragStart", function(self) PickupInventoryItem(self.id) end)
    s:SetScript("OnReceiveDrag", function(self) PickupInventoryItem(self.id) end)
    slots[name] = s
    return s
end

local function paintSlot(s)
    local link = GetInventoryItemLink("player", s.id)
    local d = itemDetails(link)
    s.icon:SetTexture(GetInventoryItemTexture("player", s.id) or s.emptyTexture)
    s.icon:SetDesaturated(link == nil)
    s.icon:SetAlpha(link and 1 or 0.35)
    local q = d and d.quality and ITEM_QUALITY_COLORS and ITEM_QUALITY_COLORS[d.quality]
    if q and opt("qualityBorders") then ES.Tint(s.lines, { q.r, q.g, q.b }, 1) else ES.Tint(s.lines, ES.GOLD_DIM, link and 1 or 0.6) end
    local cur, max = GetInventoryItemDurability(s.id)
    local low = opt("durability") and cur and max and max > 0 and cur / max <= 0.2
    s.broken:SetAlpha(low and (cur == 0 and 0.45 or 0.22) or 0)
    s.ilvl:SetText(opt("ilvlNumbers") and d and d.ilvl and not NO_ILVL[s.slotName] and d.ilvl or "")
    if q and opt("ilvlQuality") then s.ilvl:SetTextColor(q.r, q.g, q.b) else s.ilvl:SetTextColor(unpack(ES.TEXT)) end
    s.enchant:SetAlpha(opt("enchantDots") and d and d.enchanted and 1 or 0)
end

-- Character: stats --------------------------------------------------------------------------------------------------
local function cr(name, fallback) return _G[name] or fallback end

local function statSections()
    local sections = {}
    local function add(title, rows) sections[#sections + 1] = { title = title, rows = rows } end
    local attributes = {}
    for i = 1, 5 do
        local base, stat, pos, neg = UnitStat("player", i)
        local buffed = (pos or 0) + (neg or 0)
        attributes[#attributes + 1] = { _G["SPELL_STAT" .. i .. "_NAME"] or ("Stat " .. i), stat or 0,
            color = buffed > 0 and ES.GOOD or buffed < 0 and ES.BAD or nil, stat = i,
            tip = ("%d base%s"):format(base or 0, buffed ~= 0 and ("  %+d from effects"):format(buffed) or "") }
    end
    attributes[#attributes + 1] = { HEALTH or "Health", UnitHealthMax("player") }
    if classless() or UnitPowerMax("player", 0) > 0 then attributes[#attributes + 1] = { MANA or "Mana", UnitPowerMax("player", 0) } end
    add("Attributes", attributes)

    local base, pos, neg = UnitAttackPower("player")
    local low, high = UnitDamage("player")
    local speed = UnitAttackSpeed("player")
    add("Melee", {
        { "Attack power", (base or 0) + (pos or 0) + (neg or 0) },
        { "Damage", ("%d - %d"):format(low or 0, high or 0) },
        { "Speed", ("%.2f"):format(speed or 0) },
        { "Hit", pct(GetCombatRatingBonus(cr("CR_HIT_MELEE", 6))) },
        { "Crit", pct(GetCritChance()) },
        { "Haste", pct(GetCombatRatingBonus(cr("CR_HASTE_MELEE", 18))) },
        { "Expertise", (GetExpertise()) or 0 },
        { "Armor penetration", pct(GetArmorPenetration and GetArmorPenetration() or 0) },
    })
    if GetInventoryItemLink("player", (GetInventorySlotInfo("RangedSlot"))) then
        local rb, rp, rn = UnitRangedAttackPower("player")
        add("Ranged", {
            { "Attack power", (rb or 0) + (rp or 0) + (rn or 0) },
            { "Hit", pct(GetCombatRatingBonus(cr("CR_HIT_RANGED", 7))) },
            { "Crit", pct(GetRangedCritChance()) },
            { "Haste", pct(GetCombatRatingBonus(cr("CR_HASTE_RANGED", 19))) },
        })
    end
    local power, crit = 0, 0
    for school = 2, 7 do
        power = math.max(power, GetSpellBonusDamage(school) or 0)
        crit = math.max(crit, GetSpellCritChance(school) or 0)
    end
    -- Mana regen: on a classless Hero the client reads the shown pool, so take the server's figure when it has one.
    local regen, casting
    local cw = CW()
    if classless() and cw.stats and cw.stats.mp5 then
        regen, casting = cw.stats.mp5, cw.stats.mp5Casting or 0
    else
        regen, casting = GetManaRegen()
        regen, casting = (regen or 0) * 5, (casting or 0) * 5
    end
    add("Spell", {
        { "Spell power", power },
        { "Healing", GetSpellBonusHealing() or 0 },
        { "Hit", pct(GetCombatRatingBonus(cr("CR_HIT_SPELL", 8))) },
        { "Crit", pct(crit) },
        { "Haste", pct(GetCombatRatingBonus(cr("CR_HASTE_SPELL", 20))) },
        { "Mana regen", ("%d  (%d casting)"):format(math.floor(regen), math.floor(casting)) },
        { "Penetration", GetSpellPenetration and GetSpellPenetration() or 0 },
    })
    local _, armor = UnitArmor("player")
    local defense, mod = UnitDefense("player")
    add("Defense", {
        { "Armor", armor or 0 },
        { "Defense", (defense or 0) + (mod or 0) },
        { "Dodge", pct(GetDodgeChance()) },
        { "Parry", pct(GetParryChance()) },
        { "Block", pct(GetBlockChance()) },
        { "Resilience", GetCombatRating(cr("CR_CRIT_TAKEN_MELEE", 15)) or 0 },
    })
    local resist = {}
    for school, label in ipairs({ "Holy", "Fire", "Nature", "Frost", "Shadow", "Arcane" }) do
        local _, total = UnitResistance("player", school)
        if school > 1 then resist[#resist + 1] = { label, total or 0 } end
    end
    add("Resistances", resist)
    return sections
end

local function buildStats(side)
    local scroll = CreateFrame("ScrollFrame", nil, side)
    scroll:SetPoint("TOPLEFT", 0, -38); scroll:SetPoint("BOTTOMRIGHT", 0, 6)
    local child = CreateFrame("Frame", nil, scroll); child:SetSize(side.width, 10)
    scroll:SetScrollChild(child)
    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", function(self, delta)
        local range = math.max(0, child:GetHeight() - self:GetHeight())
        self:SetVerticalScroll(math.max(0, math.min(range, self:GetVerticalScroll() - delta * 40)))
    end)
    side.stats = scroll
    side.statChild = child
    side.statRows, side.statHeads = {}, {}
end

local function paintStats(side)
    local child, y = side.statChild, 4
    local rows, heads = side.statRows, side.statHeads
    local r, hIndex = 0, 0
    local visible = {}
    for _, section in ipairs(statSections()) do
        if sectionShown(section.title) then
            local rows = {}
            for _, row in ipairs(section.rows) do
                local v = tostring(row[2])
                local zero = v == "0" or v == "0.00%" or v == "0 - 0" or v:match("^0  %(")
                if not (opt("hideZero") and zero) then rows[#rows + 1] = row end
            end
            if #rows > 0 then visible[#visible + 1] = { title = section.title, rows = rows } end
        end
    end
    for _, section in ipairs(visible) do
        hIndex = hIndex + 1
        local h = heads[hIndex]
        if not h then
            h = { text = ES.Text(child, 10, ES.GOLD_DIM), rule = ES.Solid(child, "ARTWORK", ES.GOLD_DIM, 0.4) }
            h.rule:SetHeight(1)
            heads[hIndex] = h
        end
        h.text:ClearAllPoints(); h.text:SetPoint("TOPLEFT", 14, -y); h.text:SetText(section.title:upper()); h.text:Show()
        h.rule:ClearAllPoints(); h.rule:SetPoint("TOPLEFT", 14, -(y + 13)); h.rule:SetPoint("TOPRIGHT", child, "TOPRIGHT", -14, -(y + 13))
        h.rule:Show()
        y = y + 18
        for _, row in ipairs(section.rows) do
            r = r + 1
            local f = rows[r]
            if not f then
                f = CreateFrame("Frame", nil, child); f:SetSize(side.width - 20, 17); f:EnableMouse(true)
                f.wash = ES.Solid(f, "BACKGROUND", ES.GOLD, 0); f.wash:SetAllPoints()
                f.label = ES.Text(f, 10, ES.MUTED); f.label:SetPoint("LEFT", 4, 0)
                f.value = ES.Text(f, 11, ES.TEXT); f.value:SetPoint("RIGHT", -4, 0)
                f:SetScript("OnEnter", function(self)
                    self.wash:SetAlpha(0.06)
                    local cw = CW()
                    if self.stat and classless() and cw.stats and cw.stats.have and cw.ShowStatTooltip then
                        cw.ShowStatTooltip(self, self.stat)
                    elseif self.tip then
                        GameTooltip:SetOwner(self, "ANCHOR_LEFT"); GameTooltip:SetText(self.label:GetText(), 1, 1, 1)
                        GameTooltip:AddLine(self.tip, 0.88, 0.86, 0.8, true); GameTooltip:Show()
                    end
                end)
                f:SetScript("OnLeave", function(self) self.wash:SetAlpha(0); GameTooltip:Hide() end)
                rows[r] = f
            end
            f:ClearAllPoints(); f:SetPoint("TOPLEFT", 10, -y)
            f.label:SetText(row[1]); f.value:SetText(row[2]); f.value:SetTextColor(unpack(row.color or ES.TEXT))
            f.stat, f.tip = row.stat, row.tip
            f:Show()
            y = y + 17
        end
        y = y + 6
    end
    for i = r + 1, #rows do rows[i]:Hide() end
    for i = hIndex + 1, #heads do heads[i].text:Hide(); heads[i].rule:Hide() end
    child:SetHeight(y)
    side.stats:UpdateScrollChildRect()
end

-- Character: titles and gear sets ---------------------------------------------------------------------------------
local function titleRows(query)
    local out = { { id = -1, name = NONE or "No title" } }
    for i = 1, (GetNumTitles()) or 0 do
        if IsTitleKnown(i) == 1 or IsTitleKnown(i) == true then
            local name = strtrim(((GetTitleName(i) or ""):gsub("%%s", "")))
            if name ~= "" and (query == "" or name:lower():find(query, 1, true)) then out[#out + 1] = { id = i, name = name } end
        end
    end
    table.sort(out, function(a, b) if a.id == -1 then return true elseif b.id == -1 then return false end return a.name < b.name end)
    return out
end

local function setRows()
    local out = {}
    for i = 1, (GetNumEquipmentSets and GetNumEquipmentSets() or 0) do
        local name, icon = GetEquipmentSetInfo(i)
        if name then out[#out + 1] = { name = name, icon = icon } end
    end
    return out
end

local function buildCharacter(p)
    local GEAR_W = 10 + SLOT + 8 + 248 + 8 + SLOT + 10
    local gear = panel(p); gear:SetPoint("TOPLEFT", 14, -10); gear:SetSize(GEAR_W, PANE_H)
    gear:SetFrameLevel(p:GetFrameLevel() + 6) -- the side panel slides away underneath it
    -- The dressing room behind the gear: Blizzard's four race tiles (256+64 wide, 256+128 tall), inset to keep the border.
    local roomW, roomH = GEAR_W - 2, PANE_H - 2
    p.room = {}
    p.roomSpecs = { { 0, 0, 0.8, 2 / 3 }, { 0.8, 0, 0.2, 2 / 3 }, { 0, 2 / 3, 0.8, 1 / 3 }, { 0.8, 2 / 3, 0.2, 1 / 3 } }
    for i = 1, 4 do p.room[i] = gear:CreateTexture(nil, "BORDER") end
    p.roomW, p.roomH = roomW, roomH
    p.wash = ES.Solid(gear, "ARTWORK", ES.INK, 0.28)
    p.wash:SetPoint("TOPLEFT", 1, -1); p.wash:SetPoint("BOTTOMRIGHT", -1, 1)
    p.fade = gear:CreateTexture(nil, "ARTWORK"); p.fade:SetTexture(ES.WHITE)
    p.fade:SetGradientAlpha("VERTICAL", ES.INK[1], ES.INK[2], ES.INK[3], 0.75, ES.INK[1], ES.INK[2], ES.INK[3], 0)
    p.fade:SetPoint("BOTTOMLEFT", 1, 1); p.fade:SetPoint("BOTTOMRIGHT", -1, 1); p.fade:SetHeight(130)
    gradient(gear, 50, 0.18)
    p.gear = gear
    -- The collapse handle on the gear panel's right edge.
    local chevron = CreateFrame("Button", nil, gear)
    chevron:SetSize(14, 46); chevron:SetPoint("RIGHT", gear, "RIGHT", 7, 0); chevron:SetFrameLevel(gear:GetFrameLevel() + 6)
    ES.Solid(chevron, "BACKGROUND", ES.INK, 1):SetAllPoints()
    chevron.glow = ES.Solid(chevron, "ARTWORK", ES.GOLD, 0); chevron.glow:SetAllPoints()
    chevron.lines = ES.Border(chevron, ES.GOLD_DIM)
    chevron.label = ES.Text(chevron, 12, ES.GOLD); chevron.label:SetPoint("CENTER", 1, 1); chevron.label:SetText("<")
    chevron:SetScript("OnEnter", function(self)
        self.glow:SetAlpha(0.15); ES.Tint(self.lines, ES.GOLD, 1)
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        GameTooltip:SetText(db.collapsed and "Show stats, titles and gear sets" or "Collapse to the gear", 1, 0.82, 0.1)
        GameTooltip:Show()
    end)
    chevron:SetScript("OnLeave", function(self) self.glow:SetAlpha(0); ES.Tint(self.lines, ES.GOLD_DIM, 1); GameTooltip:Hide() end)
    chevron:SetScript("OnClick", function() EsteriaCharacter_SetCollapsed(not db.collapsed) end)
    p.chevron = chevron
    p.ilvl = ES.Text(gear, 12, ES.GOLD); p.ilvl:SetPoint("TOP", 0, -8)
    p.ilvlLabel = ES.Text(gear, 8, ES.GOLD_DIM); p.ilvlLabel:SetPoint("TOP", p.ilvl, "BOTTOM", 0, -1)
    p.ilvlLabel:SetText("AVERAGE ITEM LEVEL")
    for i, name in ipairs(LEFT) do makeSlot(gear, name):SetPoint("TOPLEFT", 10, -36 - (i - 1) * STEP) end
    for i, name in ipairs(RIGHT) do makeSlot(gear, name):SetPoint("TOPRIGHT", -10, -36 - (i - 1) * STEP) end
    for i, name in ipairs(WEAPONS) do
        makeSlot(gear, name):SetPoint("TOP", gear, "TOPLEFT", GEAR_W / 2 + (i - 2) * STEP, -36 - 8 * STEP + 2)
    end

    local model = CreateFrame("PlayerModel", nil, gear)
    model:SetPoint("TOPLEFT", 10 + SLOT + 8, -36); model:SetPoint("BOTTOMRIGHT", gear, "TOPRIGHT", -(10 + SLOT + 8), -36 - 8 * STEP + 8)
    model.rotation = 0
    model:EnableMouse(true); model:EnableMouseWheel(true)
    model:SetScript("OnMouseDown", function(self, mouse) if mouse == "LeftButton" then self.dragX = GetCursorPosition() end end)
    local function remember(self)
        if opt("rememberModel") and db then db.model = { rotation = self.rotation, zoom = self.zoom } end
    end
    model:SetScript("OnMouseUp", function(self) self.dragX = nil; remember(self) end)
    model:SetScript("OnUpdate", function(self, elapsed)
        if self.dragX then
            local x = GetCursorPosition()
            self.rotation = self.rotation + (x - self.dragX) * 0.02
            self.dragX = x
            self:SetRotation(self.rotation)
        elseif opt("autoRotate") then
            self.rotation = self.rotation + (elapsed or 0) * opt("rotateSpeed")
            self:SetRotation(self.rotation)
        end
    end)
    model.zoom = 0
    model:SetScript("OnMouseWheel", function(self, delta)
        self.zoom = math.max(0, math.min(2.5, self.zoom + delta * 0.25))
        self:SetPosition(self.zoom, 0, 0)
        remember(self)
    end)
    -- Back to the remembered angle and zoom, or straight on.
    model.Restore = function(self)
        local saved = opt("rememberModel") and db and db.model
        self.rotation, self.zoom = saved and saved.rotation or 0, saved and saved.zoom or 0
        self:SetRotation(self.rotation); self:SetPosition(self.zoom, 0, 0)
    end
    p.model = model
    p.hint = ES.Text(gear, 8, ES.MUTED); p.hint:SetPoint("BOTTOM", model, "BOTTOM", 0, 2)
    p.hint:SetText("Drag to turn  ·  wheel to zoom")

    local side = panel(p)
    side.width = W - 14 - GEAR_W - 10 - 14
    -- Fixed size, so it slides as one piece instead of squashing while the window narrows.
    side:SetPoint("TOPLEFT", gear, "TOPRIGHT", 10, 0); side:SetSize(side.width, PANE_H)
    side:SetFrameLevel(p:GetFrameLevel() + 1)
    p.side = side
    side.tabs = {}
    for i, name in ipairs({ "Stats", "Titles", "Gear sets" }) do
        local t = tabButton(side, math.floor((side.width - 12) / 3), name, 24)
        t:SetPoint("TOPLEFT", 6 + (i - 1) * math.floor((side.width - 12) / 3), -6)
        t:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); selected.side = name; dirty = true end)
        side.tabs[name] = t
    end
    buildStats(side)

    side.titles = CreateFrame("Frame", nil, side); side.titles:SetPoint("TOPLEFT", 0, -38); side.titles:SetPoint("BOTTOMRIGHT")
    side.titleSearch = editBox(side.titles, side.width - 20, "Search your titles", nil, 40)
    side.titleSearch:SetPoint("TOPLEFT", 10, 0)
    side.titleSearch:HookScript("OnTextChanged", function() side.titleList.offset = 0; dirty = true end)
    side.titleList = vlist(side.titles, side.width - 12, 24, math.floor((PANE_H - 38 - 34 - 8) / 24), function(parent, w, h)
        local r = CreateFrame("Button", nil, parent); r:SetSize(w, h - 2)
        r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
        r.mark = ES.Solid(r, "ARTWORK", ES.GOLD, 0); r.mark:SetSize(6, 6); r.mark:SetPoint("LEFT", 10, 0)
        r.label = ES.Text(r, 11, ES.TEXT); r.label:SetPoint("LEFT", 22, 0); r.label:SetPoint("RIGHT", -8, 0); r.label:SetJustifyH("LEFT")
        r:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); SetCurrentTitle(self.item.id); dirty = true end)
        r:SetScript("OnEnter", function(self) if not self.on then self.wash:SetAlpha(0.06) end end)
        r:SetScript("OnLeave", function(self) if not self.on then self.wash:SetAlpha(0) end end)
        return r
    end, function(r, t)
        local on = (GetCurrentTitle() or -1) == t.id or (t.id == -1 and (GetCurrentTitle() or 0) <= 0)
        r.on = on
        r.wash:SetAlpha(on and 0.12 or 0); r.mark:SetAlpha(on and 1 or 0)
        r.label:SetText(t.name); r.label:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
    end)
    side.titleList:SetPoint("TOPLEFT", 6, -32)

    side.sets = CreateFrame("Frame", nil, side); side.sets:SetPoint("TOPLEFT", 0, -38); side.sets:SetPoint("BOTTOMRIGHT")
    side.setList = vlist(side.sets, side.width - 12, 34, math.floor((PANE_H - 38 - 110) / 34), function(parent, w, h)
        local r = CreateFrame("Button", nil, parent); r:SetSize(w, h - 2)
        r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
        r.icon = r:CreateTexture(nil, "ARTWORK"); r.icon:SetSize(26, 26); r.icon:SetPoint("LEFT", 6, 0)
        r.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        r.label = ES.Text(r, 11, ES.TEXT); r.label:SetPoint("LEFT", 40, 0); r.label:SetPoint("RIGHT", -8, 0); r.label:SetJustifyH("LEFT")
        r:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); side.selectedSet = self.item.name; dirty = true end)
        r:SetScript("OnEnter", function(self) if not self.on then self.wash:SetAlpha(0.06) end end)
        r:SetScript("OnLeave", function(self) if not self.on then self.wash:SetAlpha(0) end end)
        return r
    end, function(r, s)
        r.on = side.selectedSet == s.name
        r.wash:SetAlpha(r.on and 0.12 or 0)
        r.icon:SetTexture(s.icon); r.label:SetText(s.name); r.label:SetTextColor(unpack(r.on and ES.GOLD or ES.TEXT))
    end)
    side.setList:SetPoint("TOPLEFT", 6, 0)
    side.setList.empty:SetText("No gear sets yet. Name one below to save what you're wearing.")
    local bw = math.floor((side.width - 26) / 2)
    side.equip = button(side.sets, bw, "Equip", true, 24); side.equip:SetPoint("BOTTOMLEFT", 10, 72)
    side.equip:SetScript("OnClick", function()
        if side.selectedSet then
            if InCombatLockdown() then UIErrorsFrame:AddMessage(ERR_NOT_IN_COMBAT or "Not in combat.", 1, 0.1, 0.1)
            else UseEquipmentSet(side.selectedSet) end
        end
    end)
    side.delete = confirmButton(side.sets, bw, "Delete", function()
        if side.selectedSet then DeleteEquipmentSet(side.selectedSet); side.selectedSet = nil; dirty = true end
    end)
    side.delete:SetHeight(24); side.delete:SetPoint("BOTTOMLEFT", 16 + bw, 72)
    side.newSet = editBox(side.sets, side.width - 20, "Name, then Save to store current gear", nil, 16)
    side.newSet:SetPoint("BOTTOMLEFT", 10, 40)
    side.save = button(side.sets, side.width - 20, "Save current gear", false, 24); side.save:SetPoint("BOTTOMLEFT", 10, 10)
    side.save:SetScript("OnClick", function()
        local name = strtrim(side.newSet:GetText())
        if name == "" then name = side.selectedSet end
        if not name then return end
        local head = GetInventoryItemTexture("player", (GetInventorySlotInfo("HeadSlot")))
        SaveEquipmentSet(name, head or "Interface\\Icons\\INV_Misc_QuestionMark")
        side.newSet:SetText(""); side.newSet:ClearFocus(); side.selectedSet = name; dirty = true
    end)
end

-- Blizzard's dressing room mapping: gnomes share the dwarf room, trolls the orc room, death knights have their own.
local ROOM_ALIAS = { Gnome = "Dwarf", Troll = "Orc" }

-- The room the setting asks for: "auto" (own race), "none", "race:<File>" or "class:<key>".
local function ownRoomRace()
    local _, raceFile = UnitRace("player")
    local _, classFile = UnitClass("player")
    return classFile == "DEATHKNIGHT" and "DeathKnight" or ROOM_ALIAS[raceFile or ""] or raceFile or "Human"
end

local function resolveRoom()
    local room = opt("room")
    return room == "auto" and ("race:" .. ownRoomRace()) or room
end

local function paintDressingRoom(p)
    local room = resolveRoom()
    if p.drawnRoom == room then return end
    p.drawnRoom = room
    local kind, key = room:match("^(%a+):(.+)$")
    for i, tex in ipairs(p.room) do
        local spec = p.roomSpecs[i]
        tex:ClearAllPoints()
        if kind == "race" then
            tex:SetPoint("TOPLEFT", 1 + spec[1] * p.roomW, -1 - spec[2] * p.roomH)
            tex:SetSize(spec[3] * p.roomW, spec[4] * p.roomH)
            tex:SetTexCoord(0, 1, 0, 1)
            -- Esteria's custom races have no room of their own; they get the human one.
            if not tex:SetTexture("Interface\\DressUpFrame\\DressUpBackground-" .. key .. i) then
                tex:SetTexture("Interface\\DressUpFrame\\DressUpBackground-Human" .. i)
            end
            tex:Show()
        elseif kind == "class" and i == 1 then
            -- A retail room is one picture: the first tile takes the whole panel.
            tex:SetPoint("TOPLEFT", 1, -1); tex:SetSize(p.roomW, p.roomH)
            tex:SetTexture(ROOM_ART .. key)
            coverCrop(tex, p.roomW, p.roomH)
            tex:Show()
        else
            tex:Hide()
        end
    end
end

local function paintCharacter(p)
    paintDressingRoom(p)
    p.wash:SetVertexColor(ES.INK[1], ES.INK[2], ES.INK[3], opt("dim"))
    shown(p.fade, opt("bottomFade") and opt("room") ~= "none")
    shown(p.ilvl, opt("avgIlvl")); shown(p.ilvlLabel, opt("avgIlvl"))
    shown(p.hint, opt("modelHint"))
    for _, s in pairs(slots) do paintSlot(s) end
    p.ilvl:SetText(("%.1f"):format(averageItemLevel()))
    local side = p.side
    for name, t in pairs(side.tabs) do t:SetSelected(name == selected.side) end
    shown(side.stats, selected.side == "Stats"); shown(side.titles, selected.side == "Titles"); shown(side.sets, selected.side == "Gear sets")
    if selected.side == "Stats" then paintStats(side)
    elseif selected.side == "Titles" then
        side.titleList.data = titleRows(side.titleSearch:GetText():lower()); side.titleList:Refresh()
    else
        side.setList.data = setRows(); side.setList:Refresh()
        side.equip:Active(side.selectedSet ~= nil); side.delete:Active(side.selectedSet ~= nil)
        side.save.label:SetText(side.newSet:GetText() ~= "" and "Save as new set" or
            (side.selectedSet and ("Update " .. side.selectedSet) or "Save current gear"))
    end
end

-- Reputation --------------------------------------------------------------------------------------------------------
local function factions()
    -- Collapsed headers hide their factions from the API; open them, last first so earlier indices stay put.
    for i = (GetNumFactions()) or 0, 1, -1 do
        local _, _, _, _, _, _, _, _, isHeader, isCollapsed = GetFactionInfo(i)
        if isHeader and isCollapsed then ExpandFactionHeader(i) end
    end
    local out, groups, group = {}, {}, nil
    for i = 1, (GetNumFactions()) or 0 do
        local name, description, standing, low, high, value, atWar, canToggle, isHeader, _, hasRep, watched, isChild =
            GetFactionInfo(i)
        if name then
            if isHeader and not isChild then
                group = { name = name, count = 0 }
                groups[#groups + 1] = group
            else
                local e = { index = i, name = name, description = description, standing = standing, low = low, high = high,
                            value = value, atWar = atWar, canToggle = canToggle, watched = watched,
                            subHeader = isHeader and isChild, hasRep = hasRep, group = group and group.name or "" }
                out[#out + 1] = e
                if group and (not e.subHeader or hasRep) then group.count = group.count + 1 end
            end
        end
    end
    return out, groups
end

local function standingColor(standing)
    local c = FACTION_BAR_COLORS and FACTION_BAR_COLORS[standing or 4]
    return c and { c.r, c.g, c.b } or ES.GOLD
end

local function buildList(p, hint)
    local rail = panel(p); rail:SetPoint("TOPLEFT", 14, -10); rail:SetPoint("BOTTOMLEFT", 14, 10); rail:SetWidth(RAIL_W)
    p.railHead = heading(rail, ""); p.railHead:SetPoint("TOPLEFT", 12, -10)
    p.rail = {}
    for i = 1, math.floor((PANE_H - 26) / 26) do
        local row = railRow(rail, function(self) p.group = self.group; p.list.offset = 0; dirty = true end)
        row:SetPoint("TOPLEFT", 6, -24 - (i - 1) * 26)
        p.rail[i] = row
    end
    local middle = panel(p); middle:SetPoint("TOPLEFT", rail, "TOPRIGHT", 10, 0); middle:SetPoint("BOTTOMRIGHT", -14, 10)
    p.search = editBox(middle, LIST_W - 20, hint, nil, 40); p.search:SetPoint("TOPLEFT", 10, -10)
    p.search:HookScript("OnTextChanged", function() p.list.offset = 0; dirty = true end)
    p.detail = strip(middle)
    return middle
end

local function listRows(top) return math.floor((PANE_H - top - STRIP_H - 6) / ROW_H) end

local function paintRail(p, groups, total, allLabel)
    p.rail[1].group = nil
    p.rail[1]:Paint(allLabel, p.group == nil, total)
    for i = 2, #p.rail do
        local g = groups[i - 1]
        local row = p.rail[i]
        row.group = g and g.name
        if g then row:Paint(g.name, p.group == g.name, g.count); row:Show() else row:Hide() end
    end
end

local function buildReputation(p)
    local middle = buildList(p, "Search factions")
    p.railHead:SetText("GROUPS")
    p.list = vlist(middle, LIST_W - 12, ROW_H, listRows(42), function(parent, w, h)
        return barRow(parent, w, h, function(item) selected.faction = item.name; dirty = true end)
    end, function(r, e)
        if e.subHeader and not e.hasRep then r:Paint("header", false, e.name); return end
        local label = _G["FACTION_STANDING_LABEL" .. (e.standing or 4)] or ""
        local span = math.max(1, (e.high or 1) - (e.low or 0))
        r:Paint("bar", selected.faction == e.name, e.name, e.atWar and ES.BAD or nil,
            ("%s  %d / %d"):format(label, (e.value or 0) - (e.low or 0), span), ((e.value or 0) - (e.low or 0)) / span,
            standingColor(e.standing))
    end)
    p.list:SetPoint("TOPLEFT", 6, -42)
    p.list.empty:SetText("No factions match.")
end

local function paintReputation(p)
    local list, groups = factions()
    local query = p.search:GetText():lower()
    local rows = {}
    for _, e in ipairs(list) do
        local ok = query ~= "" and e.name:lower():find(query, 1, true) or query == "" and (p.group == nil or e.group == p.group)
        if ok then rows[#rows + 1] = e end
    end
    paintRail(p, groups, #list, "All factions")
    p.list.data = rows; p.list:Refresh()
    local d, e = p.detail, nil
    for _, f in ipairs(list) do if f.name == selected.faction then e = f end end
    if not e then d:Begin("Select a faction."); d:Actions({}); return end
    d:Begin()
    d.icon.tex:SetTexture("Interface\\Icons\\INV_Misc_Tournaments_Banner_Human")
    ES.Tint(d.icon.lines, standingColor(e.standing), 1)
    d.title:SetText(e.name); d.sub:SetText(e.group)
    d.pill:Set(_G["FACTION_STANDING_LABEL" .. (e.standing or 4)] or "", standingColor(e.standing))
    local span = math.max(1, (e.high or 1) - (e.low or 0))
    d:Bar(((e.value or 0) - (e.low or 0)) / span, standingColor(e.standing),
        ("%d / %d"):format((e.value or 0) - (e.low or 0), span))
    d:Line("About", e.description)
    if e.atWar then d:Line("Status", "At war", ES.BAD) end
    d:Actions({
        { e.watched and "Stop watching" or "Watch as bar", function() SetWatchedFactionIndex(e.watched and 0 or e.index); dirty = true end },
        { e.atWar and "Make peace" or "Declare war", function() FactionToggleAtWar(e.index); dirty = true end,
          e.canToggle and true or false },
        { IsFactionInactive and IsFactionInactive(e.index) and "Make active" or "Make inactive", function()
            if IsFactionInactive(e.index) then SetFactionActive(e.index) else SetFactionInactive(e.index) end
            dirty = true
        end, IsFactionInactive ~= nil },
    })
end

-- Skills ------------------------------------------------------------------------------------------------------------
local function skills()
    if ExpandSkillHeader then ExpandSkillHeader(0) end
    local out, groups, group = {}, {}, nil
    for i = 1, (GetNumSkillLines()) or 0 do
        local name, isHeader, _, rank, temp, modifier, maxRank, abandonable, _, _, _, _, description = GetSkillLineInfo(i)
        if name then
            if isHeader then
                group = { name = name, count = 0 }
                groups[#groups + 1] = group
            else
                out[#out + 1] = { index = i, name = name, rank = rank or 0, temp = temp or 0, modifier = modifier or 0,
                                  maxRank = maxRank or 0, abandonable = abandonable, description = description,
                                  group = group and group.name or "" }
                if group then group.count = group.count + 1 end
            end
        end
    end
    return out, groups
end

local function buildSkills(p)
    local middle = buildList(p, "Search skills")
    p.railHead:SetText("CATEGORIES")
    p.list = vlist(middle, LIST_W - 12, ROW_H, listRows(42), function(parent, w, h)
        return barRow(parent, w, h, function(item) selected.skill = item.name; dirty = true end)
    end, function(r, e)
        local max = e.maxRank > 0 and e.maxRank or 1
        local bonus = e.modifier ~= 0 and ("  (%+d)"):format(e.modifier) or ""
        r:Paint("bar", selected.skill == e.name, e.name, nil,
            e.maxRank > 1 and (("%d / %d"):format(e.rank, e.maxRank) .. bonus) or "",
            e.maxRank > 1 and e.rank / max or 1, e.rank >= e.maxRank and ES.GOOD or ES.GOLD)
    end)
    p.list:SetPoint("TOPLEFT", 6, -42)
    p.list.empty:SetText("No skills match.")
end

local function paintSkills(p)
    local list, groups = skills()
    local query = p.search:GetText():lower()
    local rows = {}
    for _, e in ipairs(list) do
        local ok = query ~= "" and e.name:lower():find(query, 1, true) or query == "" and (p.group == nil or e.group == p.group)
        if ok then rows[#rows + 1] = e end
    end
    paintRail(p, groups, #list, "All skills")
    p.list.data = rows; p.list:Refresh()
    local d, e = p.detail, nil
    for _, s in ipairs(list) do if s.name == selected.skill then e = s end end
    if not e then d:Begin("Select a skill."); d:Actions({}); return end
    d:Begin()
    d.icon.tex:SetTexture("Interface\\Icons\\INV_Misc_Book_11"); ES.Tint(d.icon.lines, ES.GOLD, 1)
    d.title:SetText(e.name); d.sub:SetText(e.group)
    if e.maxRank > 1 then
        d:Bar(e.rank / e.maxRank, e.rank >= e.maxRank and ES.GOOD or ES.GOLD, ("%d / %d"):format(e.rank, e.maxRank))
        if e.modifier ~= 0 then d:Line("Bonus", ("%+d from gear and effects"):format(e.modifier), ES.GOOD) end
    end
    d:Line("About", e.description)
    d:Actions({}, e.abandonable and { "Unlearn", function() AbandonSkill(e.index); selected.skill = nil; dirty = true end } or nil)
end

-- Currency ----------------------------------------------------------------------------------------------------------
local function currencies()
    local out, groups, group = {}, {}, nil
    local size = GetCurrencyListSize and GetCurrencyListSize() or 0
    for i = size, 1, -1 do
        local _, isHeader, isExpanded = GetCurrencyListInfo(i)
        if isHeader and not isExpanded and ExpandCurrencyList then ExpandCurrencyList(i, 1) end
    end
    size = GetCurrencyListSize and GetCurrencyListSize() or 0
    for i = 1, size do
        local name, isHeader, _, isUnused, isWatched, count, extra, icon, itemID = GetCurrencyListInfo(i)
        if name then
            if isHeader then
                group = { name = name, count = 0 }
                groups[#groups + 1] = group
            else
                if extra == 1 then icon = "Interface\\PVPFrame\\PVP-ArenaPoints-Icon"
                elseif extra == 2 then icon = "Interface\\PVPFrame\\PVP-Currency-" .. (UnitFactionGroup("player") or "Alliance")
                elseif not icon and itemID then icon = select(10, GetItemInfo(itemID)) end
                out[#out + 1] = { index = i, name = name, count = count or 0, icon = icon, watched = isWatched, unused = isUnused,
                                  itemID = itemID, group = group and group.name or "" }
                if group then group.count = group.count + 1 end
            end
        end
    end
    return out, groups
end

local function buildCurrency(p)
    local middle = buildList(p, "Search currencies")
    p.railHead:SetText("CATEGORIES")
    p.money = ES.Text(middle, 12, ES.GOLD); p.money:SetPoint("TOPRIGHT", -14, -44)
    p.moneyLabel = ES.Text(middle, 10, ES.GOLD_DIM); p.moneyLabel:SetPoint("TOPLEFT", 14, -46); p.moneyLabel:SetText("GOLD")
    p.list = vlist(middle, LIST_W - 12, ROW_H, listRows(66), function(parent, w, h)
        return barRow(parent, w, h, function(item) selected.currency = item.name; dirty = true end)
    end, function(r, e)
        r:Paint("icon", selected.currency == e.name, e.name, e.unused and ES.MUTED or nil, tostring(e.count), nil, nil, e.icon)
    end)
    p.list:SetPoint("TOPLEFT", 6, -66)
    p.list.empty:SetText("No currencies yet.")
end

local function paintCurrency(p)
    p.money:SetText(GetCoinTextureString(GetMoney()))
    local list, groups = currencies()
    local query = p.search:GetText():lower()
    local rows = {}
    for _, e in ipairs(list) do
        local ok = query ~= "" and e.name:lower():find(query, 1, true) or query == "" and (p.group == nil or e.group == p.group)
        if ok then rows[#rows + 1] = e end
    end
    paintRail(p, groups, #list, "All currencies")
    p.list.data = rows; p.list:Refresh()
    local d, e = p.detail, nil
    for _, c in ipairs(list) do if c.name == selected.currency then e = c end end
    if not e then d:Begin("Select a currency."); d:Actions({}); return end
    d:Begin()
    d.icon.tex:SetTexture(e.icon); ES.Tint(d.icon.lines, ES.GOLD, 1)
    d.title:SetText(e.name); d.sub:SetText(e.group)
    d.pill:Set(tostring(e.count), ES.GOLD)
    if e.itemID and e.itemID > 0 then
        local _, link = GetItemInfo(e.itemID)
        if link then d:Line("Item", link) end
    end
    d:Actions({ { e.watched and "Hide from backpack" or "Show on backpack", function()
        SetCurrencyBackpack(e.index, e.watched and 0 or 1); dirty = true
    end } })
end

-- Window ------------------------------------------------------------------------------------------------------------
local PANES = {
    { "Character", buildCharacter, paintCharacter },
    { "Reputation", buildReputation, paintReputation },
    { "Skills", buildSkills, paintSkills },
    { "Currency", buildCurrency, paintCurrency },
}

local title, subtitle
local function refresh()
    if not frame or not frame:IsShown() then return end
    title:SetText(UnitPVPName("player") or UnitName("player"))
    local race = UnitRace("player")
    local className = UnitClass("player")
    local guild = GetGuildInfo("player")
    subtitle:SetText(("Level %d %s %s%s"):format(UnitLevel("player"), race or "", classless() and "Hero" or (className or ""),
        guild and ("   |cffe8c46b<" .. guild .. ">|r") or ""))
    for _, spec in ipairs(PANES) do if spec[1] == current then spec[3](panes[current]) end end
end

-- Collapsing: the side panel slides left under the gear panel as it fades, the header tabs fade, and the window
-- narrows from the right in step with the panel, its left edge staying where the player put it. 0 open, 1 collapsed.
local collapse = 0
local settingsButton, popout

-- The header pieces that fade away with a collapse: the tabs and the settings button.
local function headerChrome()
    local list = {}
    for _, t in pairs(tabs) do list[#list + 1] = t end
    if settingsButton then list[#list + 1] = settingsButton end
    return list
end

local function applyCollapse(amount)
    collapse = amount
    local cp = panes.Character
    local travel = cp.side.width + 10
    frame:SetWidth(W - travel * amount)
    cp.side:ClearAllPoints(); cp.side:SetPoint("TOPLEFT", cp.gear, "TOPRIGHT", 10 - travel * amount, 0)
    cp.side:SetAlpha(1 - amount)
    for _, t in ipairs(headerChrome()) do t:SetAlpha(1 - amount) end
    cp.chevron.label:SetText(amount > 0.5 and ">" or "<")
end

local function animateCollapse(target, instant)
    local from = collapse
    if from == target then return end
    local left, top = frame:GetLeft(), frame:GetTop()
    if left and top then frame:ClearAllPoints(); frame:SetPoint("TOPLEFT", UIParent, "BOTTOMLEFT", left, top) end
    local cp = panes.Character
    if target == 0 then cp.side:Show(); for _, t in ipairs(headerChrome()) do t:Show() end
    elseif popout and popout:IsShown() then EsteriaCharacter_ToggleSettings(false) end
    local function apply(t)
        applyCollapse(from + (target - from) * t)
        -- Once shut, the panel and header pieces go away so nothing hidden answers the mouse.
        if t >= 1 and target == 1 then cp.side:Hide(); for _, piece in ipairs(headerChrome()) do piece:Hide() end end
    end
    if instant or not opt("animate") then apply(1) else ES.Tween("esteria-character-collapse", 0.35, 0, apply) end
end

function EsteriaCharacter_SetCollapsed(on)
    db.collapsed = on or nil
    if current == "Character" then
        PlaySound(on and "igCharacterInfoClose" or "igCharacterInfoOpen")
        animateCollapse(on and 1 or 0)
    end
end

local function selectTab(name, instant)
    current = name
    for _, spec in ipairs(PANES) do
        tabs[spec[1]]:SetSelected(spec[1] == name)
        shown(panes[spec[1]], spec[1] == name)
    end
    if name == "Character" then panes.Character.model:SetUnit("player"); panes.Character.model:Restore() end
    -- The collapsed size belongs to the Character tab; the other tabs always open the window up.
    animateCollapse(name == "Character" and db.collapsed and 1 or 0, instant)
    refresh()
end

-- Settings popout ---------------------------------------------------------------------------------------------------
local function applySettings()
    frame:SetScale(opt("scale"))
    if panes.Character then panes.Character.drawnRoom = nil end
    dirty = true
end

local function setOpt(key, value)
    db.settings = db.settings or {}
    db.settings[key] = value
    applySettings()
    if popout then popout.Refresh() end
end

local function buildSettings()
    local PW = 300
    popout = CreateFrame("Frame", nil, frame)
    popout:SetSize(PW, H); popout:SetPoint("TOPLEFT", frame, "TOPRIGHT", 8, 0)
    popout:EnableMouse(true); popout:Hide()
    ES.Solid(popout, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(popout, ES.GOLD)
    gradient(popout, 42, 0.28)
    local divider = ES.Solid(popout, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local t = ES.Text(popout, 15, ES.GOLD); t:SetPoint("TOPLEFT", 16, -6); t:SetText("Character settings")
    local s = ES.Text(popout, 10, ES.MUTED); s:SetPoint("TOPLEFT", 16, -25); s:SetText("Saved for all your characters")
    local close = CreateFrame("Button", nil, popout, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    close:SetScript("OnClick", function() EsteriaCharacter_ToggleSettings(false) end)

    local scroll = CreateFrame("ScrollFrame", nil, popout)
    scroll:SetPoint("TOPLEFT", 0, -44); scroll:SetPoint("BOTTOMRIGHT", 0, 8)
    local child = CreateFrame("Frame", nil, scroll); child:SetSize(PW, 10); scroll:SetScrollChild(child)
    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", function(self, delta)
        local range = math.max(0, child:GetHeight() - self:GetHeight())
        self:SetVerticalScroll(math.max(0, math.min(range, self:GetVerticalScroll() - delta * 48)))
    end)
    local y, refreshers = 8, {}

    local function head(text)
        if y > 8 then y = y + 6 end
        local h = heading(child, text); h:SetPoint("TOPLEFT", 16, -y)
        local rule = ES.Solid(child, "ARTWORK", ES.GOLD_DIM, 0.4); rule:SetHeight(1)
        rule:SetPoint("TOPLEFT", 16, -(y + 13)); rule:SetPoint("TOPRIGHT", child, "TOPRIGHT", -16, -(y + 13))
        y = y + 20
    end

    local function toggle(label, get, set)
        local b = CreateFrame("Button", nil, child); b:SetSize(PW - 24, 22); b:SetPoint("TOPLEFT", 12, -y)
        b.wash = ES.Solid(b, "BACKGROUND", ES.GOLD, 0); b.wash:SetAllPoints()
        b.box = CreateFrame("Frame", nil, b); b.box:SetSize(12, 12); b.box:SetPoint("LEFT", 6, 0)
        ES.Solid(b.box, "BACKGROUND", ES.INK, 1):SetAllPoints()
        b.box.lines = ES.Border(b.box, ES.GOLD_DIM)
        b.fill = ES.Solid(b.box, "ARTWORK", ES.GOLD, 1); b.fill:SetPoint("TOPLEFT", 3, -3); b.fill:SetPoint("BOTTOMRIGHT", -3, 3)
        b.label = ES.Text(b, 11, ES.TEXT); b.label:SetPoint("LEFT", 26, 0); b.label:SetText(label)
        b:SetScript("OnEnter", function(self) self.wash:SetAlpha(0.06) end)
        b:SetScript("OnLeave", function(self) self.wash:SetAlpha(0) end)
        b:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); set(not get()) end)
        refreshers[#refreshers + 1] = function()
            local on = get() and true or false
            shown(b.fill, on); ES.Tint(b.box.lines, on and ES.GOLD or ES.GOLD_DIM, 1)
        end
        y = y + 23
    end
    local function optToggle(label, key) toggle(label, function() return opt(key) end, function(v) setOpt(key, v) end) end

    local function slider(label, key, low, high, step, fmt)
        local l = ES.Text(child, 11, ES.TEXT); l:SetPoint("TOPLEFT", 18, -y); l:SetText(label)
        local v = ES.Text(child, 11, ES.GOLD); v:SetPoint("TOPRIGHT", child, "TOPRIGHT", -18, -y)
        local sl = CreateFrame("Slider", nil, child); sl:SetOrientation("HORIZONTAL")
        sl:SetSize(PW - 36, 14); sl:SetPoint("TOPLEFT", 18, -(y + 17))
        local track = ES.Solid(sl, "BACKGROUND", { 0, 0, 0 }, 0.6); track:SetHeight(4)
        track:SetPoint("LEFT"); track:SetPoint("RIGHT")
        local thumb = sl:CreateTexture(nil, "OVERLAY"); thumb:SetTexture(ES.WHITE); thumb:SetSize(8, 14)
        thumb:SetVertexColor(unpack(ES.GOLD)); sl:SetThumbTexture(thumb)
        sl:SetMinMaxValues(low, high); sl:SetValueStep(step)
        sl:SetScript("OnValueChanged", function(self, value)
            if self.painting then return end
            setOpt(key, math.floor(value / step + 0.5) * step)
        end)
        refreshers[#refreshers + 1] = function()
            sl.painting = true; sl:SetValue(opt(key)); sl.painting = false
            v:SetText(fmt(opt(key)))
        end
        y = y + 38
    end

    -- Dressing room picker: a grid of thumbnails, the chosen one edged in gold.
    local function roomGrid()
        local entries = { { "auto", "Automatic" }, { "none", "None" } }
        for _, r in ipairs(RACE_ROOMS) do entries[#entries + 1] = { "race:" .. r[1], r[2] } end
        for _, r in ipairs(CLASS_ROOMS) do entries[#entries + 1] = { "class:" .. r[1], r[2] } end
        local cols, gap = 3, 6
        local cw = math.floor((PW - 32 - (cols - 1) * gap) / cols)
        local th = 52
        for i, e in ipairs(entries) do
            local col, row = (i - 1) % cols, math.floor((i - 1) / cols)
            local c = CreateFrame("Button", nil, child); c:SetSize(cw, th + 16)
            c:SetPoint("TOPLEFT", 16 + col * (cw + gap), -(y + row * (th + 16 + gap)))
            local holder = CreateFrame("Frame", nil, c); holder:SetSize(cw, th); holder:SetPoint("TOP")
            ES.Solid(holder, "BACKGROUND", ES.INK, 1):SetAllPoints()
            local tex = holder:CreateTexture(nil, "ARTWORK"); tex:SetPoint("TOPLEFT", 1, -1); tex:SetPoint("BOTTOMRIGHT", -1, 1)
            local kind, key = e[1]:match("^(%a+):(.+)$")
            if kind == "class" then tex:SetTexture(ROOM_ART .. key); coverCrop(tex, cw - 2, th - 2)
            elseif kind == "race" then tex:SetTexture("Interface\\DressUpFrame\\DressUpBackground-" .. key .. "1")
            elseif e[1] == "auto" then c.auto = tex
            else tex:SetTexture(nil) end
            local mark = ES.Text(holder, 10, ES.MUTED); mark:SetPoint("CENTER")
            mark:SetText(e[1] == "none" and "Plain" or "")
            c.lines = ES.Border(holder, ES.GOLD_DIM)
            c.label = ES.Text(c, 9, ES.MUTED); c.label:SetPoint("BOTTOM", 0, 2); c.label:SetWidth(cw); c.label:SetText(e[2])
            c:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); setOpt("room", e[1]) end)
            c:SetScript("OnEnter", function(self)
                if opt("room") ~= e[1] then ES.Tint(self.lines, ES.GOLD, 0.7) end
                GameTooltip:SetOwner(self, "ANCHOR_RIGHT"); GameTooltip:SetText(e[2], 1, 0.82, 0.1)
                GameTooltip:AddLine(e[1] == "auto" and "Your own race's room." or kind == "class" and "Retail class room." or
                    e[1] == "none" and "No picture, just the panel." or "Classic race room.", 0.88, 0.86, 0.8)
                GameTooltip:Show()
            end)
            c:SetScript("OnLeave", function() GameTooltip:Hide(); popout.Refresh() end)
            refreshers[#refreshers + 1] = function()
                local on = opt("room") == e[1]
                ES.Tint(c.lines, on and ES.GOLD or ES.GOLD_DIM, on and 1 or 0.6)
                c.label:SetTextColor(unpack(on and ES.GOLD or ES.MUTED))
                if c.auto then
                    if not c.auto:SetTexture("Interface\\DressUpFrame\\DressUpBackground-" .. ownRoomRace() .. "1") then
                        c.auto:SetTexture("Interface\\DressUpFrame\\DressUpBackground-Human1")
                    end
                end
            end
        end
        y = y + math.ceil(#entries / cols) * (th + 16 + gap)
    end

    head("Dressing room")
    roomGrid()
    slider("Background dimming", "dim", 0, 0.8, 0.05, function(v) return ("%d%%"):format(v * 100 + 0.5) end)
    optToggle("Darker fade behind the weapons", "bottomFade")
    head("Gear icons")
    optToggle("Item level on each icon", "ilvlNumbers")
    optToggle("Color item levels by quality", "ilvlQuality")
    optToggle("Quality-colored borders", "qualityBorders")
    optToggle("Green dot on enchanted items", "enchantDots")
    optToggle("Red tint on low durability", "durability")
    optToggle("Average item level above the model", "avgIlvl")
    head("Model")
    optToggle("Turn slowly on its own", "autoRotate")
    slider("Turning speed", "rotateSpeed", 0.1, 1.5, 0.1, function(v) return ("%.1fx"):format(v / 0.4) end)
    optToggle("Remember angle and zoom", "rememberModel")
    optToggle("Show the drag and zoom hint", "modelHint")
    head("Stats")
    for _, section in ipairs({ "Attributes", "Melee", "Ranged", "Spell", "Defense", "Resistances" }) do
        toggle(section, function() return sectionShown(section) end, function(v)
            db.settings = db.settings or {}
            db.settings.hiddenSections = db.settings.hiddenSections or {}
            db.settings.hiddenSections[section] = not v or nil
            applySettings(); popout.Refresh()
        end)
    end
    optToggle("Hide stats that are zero", "hideZero")
    head("Window")
    slider("Window scale", "scale", 0.7, 1.3, 0.05, function(v) return ("%d%%"):format(v * 100 + 0.5) end)
    optToggle("Animate collapsing", "animate")
    optToggle("Lock window position", "lock")
    y = y + 6
    local bw = math.floor((PW - 38) / 2)
    local resetPos = button(child, bw, "Reset position", false, 24); resetPos:SetPoint("TOPLEFT", 16, -y)
    resetPos:SetScript("OnClick", function() frame:ClearAllPoints(); frame:SetPoint("CENTER") end)
    local resetAll = confirmButton(child, bw, "Reset everything", function()
        db.settings, db.model = nil, nil
        applySettings(); popout.Refresh()
        if panes.Character then panes.Character.model:Restore() end
    end)
    resetAll:SetHeight(24); resetAll:SetPoint("TOPLEFT", 22 + bw, -y)
    y = y + 34
    child:SetHeight(y)
    popout.Refresh = function() for _, r in ipairs(refreshers) do r() end end
end

-- Slides out from behind the window's right edge as it fades in; slides back in to close.
function EsteriaCharacter_ToggleSettings(show)
    if not popout then buildSettings() end
    if show == nil then show = not popout:IsShown() or popout.closing end
    local function place(offset)
        popout:ClearAllPoints(); popout:SetPoint("TOPLEFT", frame, "TOPRIGHT", 8 + offset, 0)
    end
    if show then
        popout.closing = nil
        popout:Show(); popout.Refresh()
        ES.Tween("esteria-character-settings", 0.25, 0, function(t) popout:SetAlpha(t); place(-(1 - t) * 28) end)
        if settingsButton then ES.Tint(settingsButton.lines, ES.GOLD, 1) end
    elseif popout:IsShown() then
        popout.closing = true
        ES.Tween("esteria-character-settings", 0.18, 0, function(t)
            popout:SetAlpha(1 - t); place(-t * 28)
            if t >= 1 then popout:Hide(); popout.closing = nil end
        end)
        if settingsButton then ES.Tint(settingsButton.lines, ES.GOLD_DIM, 1) end
    end
end

local function build()
    EsteriaCharacterDB = EsteriaCharacterDB or {}
    db = EsteriaCharacterDB
    frame = CreateFrame("Frame", "EsteriaCharacterFrame", UIParent)
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", function(self) if not opt("lock") then self:StartMoving() end end)
    frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:SetScale(opt("scale"))
    frame:Hide(); tinsert(UISpecialFrames, "EsteriaCharacterFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(frame, ES.GOLD)
    gradient(frame, 42, 0.28)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    title = ES.Text(frame, 15, ES.GOLD); title:SetPoint("TOPLEFT", 16, -6); title:SetWidth(296); title:SetJustifyH("LEFT")
    subtitle = ES.Text(frame, 10, ES.MUTED); subtitle:SetPoint("TOPLEFT", 16, -25); subtitle:SetWidth(296)
    subtitle:SetJustifyH("LEFT")
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    -- Settings: a small gear left of the close button, shown while the whole window is open.
    settingsButton = CreateFrame("Button", nil, frame); settingsButton:SetSize(22, 22); settingsButton:SetPoint("TOPRIGHT", -32, -10)
    ES.Solid(settingsButton, "BACKGROUND", ES.INK, 1):SetAllPoints()
    settingsButton.icon = settingsButton:CreateTexture(nil, "ARTWORK")
    settingsButton.icon:SetPoint("TOPLEFT", 2, -2); settingsButton.icon:SetPoint("BOTTOMRIGHT", -2, 2)
    settingsButton.icon:SetTexture("Interface\\Icons\\Trade_Engineering"); settingsButton.icon:SetTexCoord(0.1, 0.9, 0.1, 0.9)
    settingsButton.icon:SetDesaturated(true); settingsButton.icon:SetVertexColor(unpack(ES.GOLD))
    settingsButton.lines = ES.Border(settingsButton, ES.GOLD_DIM)
    settingsButton:SetScript("OnEnter", function(self)
        self.icon:SetDesaturated(false); self.icon:SetVertexColor(1, 1, 1)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT"); GameTooltip:SetText("Character settings", 1, 0.82, 0.1); GameTooltip:Show()
    end)
    settingsButton:SetScript("OnLeave", function(self)
        self.icon:SetDesaturated(true); self.icon:SetVertexColor(unpack(ES.GOLD)); GameTooltip:Hide()
    end)
    settingsButton:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); EsteriaCharacter_ToggleSettings() end)
    for i, spec in ipairs(PANES) do
        local b = tabButton(frame, 84, spec[1], 30); b:SetPoint("TOPRIGHT", -58 - (#PANES - i) * 86, -6)
        b:SetScript("OnClick", function() PlaySound("igCharacterInfoTab"); selectTab(spec[1]) end)
        tabs[spec[1]] = b
        local pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")
        spec[2](pane); pane:Hide()
        panes[spec[1]] = pane
    end
    frame:SetScript("OnUpdate", function() if dirty then dirty = false; refresh() end end)
    frame:SetScript("OnShow", function() PlaySound("igCharacterInfoOpen"); selectTab(current, true) end)
    frame:SetScript("OnHide", function()
        PlaySound("igCharacterInfoClose"); GameTooltip:Hide()
        if popout then popout:Hide(); popout.closing = nil; ES.Tint(settingsButton.lines, ES.GOLD_DIM, 1) end
    end)
    for _, ev in ipairs({ "PLAYER_EQUIPMENT_CHANGED", "UNIT_INVENTORY_CHANGED", "UNIT_STATS", "UNIT_RESISTANCES",
                          "UNIT_ATTACK_POWER", "UNIT_RANGED_ATTACK_POWER", "UNIT_ATTACK", "UNIT_ATTACK_SPEED", "UNIT_DAMAGE",
                          "COMBAT_RATING_UPDATE", "PLAYER_DAMAGE_DONE_MODS", "UPDATE_INVENTORY_DURABILITY", "UNIT_MODEL_CHANGED",
                          "SKILL_LINES_CHANGED", "UPDATE_FACTION", "CURRENCY_DISPLAY_UPDATE", "KNOWN_TITLES_UPDATE",
                          "EQUIPMENT_SETS_CHANGED", "PLAYER_LEVEL_UP", "PLAYER_MONEY", "UNIT_NAME_UPDATE", "GET_ITEM_INFO_RECEIVED" }) do
        frame:RegisterEvent(ev)
    end
    frame:SetScript("OnEvent", function(_, event, unit)
        if event == "UNIT_MODEL_CHANGED" and unit == "player" and panes.Character then panes.Character.model:SetUnit("player") end
        dirty = true
    end)
end

function EsteriaCharacter_Toggle(tab)
    if not frame then build() end
    if frame:IsShown() and (not tab or tab == current) then frame:Hide(); return end
    if tab then current = tab end
    if frame:IsShown() then selectTab(current) else frame:Show() end
end

-- Entry points: C / U / K / currency bindings and the micro button call ToggleCharacter; anything else showing the stock
-- window is redirected. The pet tab is left to the stock window.
local STOCK = { PaperDollFrame = "Character", ReputationFrame = "Reputation", SkillFrame = "Skills", TokenFrame = "Currency" }
local stockToggle = ToggleCharacter
ToggleCharacter = function(frameName)
    local tab = STOCK[frameName or "PaperDollFrame"]
    if not tab then return stockToggle and stockToggle(frameName) end
    EsteriaCharacter_Toggle(tab)
end
if CharacterFrame then
    CharacterFrame:HookScript("OnShow", function(self)
        -- Stock tabs: 1 Character, 2 Pet, 3 Reputation, 4 Skills, 5 Currency.
        local tab = ({ "Character", nil, "Reputation", "Skills", "Currency" })[PanelTemplates_GetSelectedTab(self) or 1]
        if not tab then return end
        HideUIPanel(self)
        if not frame then build() end
        current = tab
        if frame:IsShown() then selectTab(tab) else frame:Show() end
    end)
end

EsteriaCharacter_Test = { statSections = statSections, averageItemLevel = averageItemLevel, itemDetails = itemDetails,
                          factions = factions, skills = skills, currencies = currencies, titleRows = titleRows,
                          selected = selected, panes = panes, setOpt = function(k, v) setOpt(k, v) end,
                          popout = function() return popout end,
                          refresh = function() dirty = true; if frame then frame:GetScript("OnUpdate")(frame) end end }

SLASH_ESTERIACHARACTER1 = "/character"
SlashCmdList.ESTERIACHARACTER = function() EsteriaCharacter_Toggle() end
