-- Esteria's social window, in the PvP/Collections/Quest Log look. Four tabs share one shell:
--   Friends      character friends and Esteria account friends in one list, requests, ignore list, your status
--   Who          searches with quick filters and a result list
--   Guild        roster (search, sort, offline toggle), guild info and MOTD, member card with notes and officer tools
--   Communities  Esteria communities: channels, live chat, members, invite codes, join/create, invitations
-- Account friends and communities come from the Esteria social service through the EsteriaChat addon's transport
-- (EsteriaChat.request / listen / sendAccountWhisper). Without it, everything character-side still works.
local ES = EsteriaStyle
local W, H = 960, 640
local RAIL_W, MID_W = 210, 400
local DETAIL_W = W - (14 + RAIL_W + 14 + MID_W + 14) - 14
local ROW_H = 44
local AWAY = { 1, 0.78, 0.25 }

local frame, tabs, panes = nil, {}, {}
local current = "Friends"
local db
local social = { connected = false, me = nil, friends = {}, incoming = {}, communities = {}, invites = {},
                 members = {}, messages = {} }
local view = { friends = "online", guild = "roster", whoQuery = nil }
local selected = { friend = nil, who = nil, member = nil, community = nil, channel = nil }

local function shown(region, on) if on then region:Show() else region:Hide() end end
local function service() return EsteriaChat and EsteriaChat.request and true or false end
local function request(payload, callback)
    if not service() then return false end
    EsteriaChat.request(payload, callback or function() end)
    return true
end

local CLASS_FILE = {}
for file, name in pairs(LOCALIZED_CLASS_NAMES_MALE or {}) do CLASS_FILE[name] = file end
for file, name in pairs(LOCALIZED_CLASS_NAMES_FEMALE or {}) do CLASS_FILE[name] = file end
local function classColor(class)
    local c = RAID_CLASS_COLORS and RAID_CLASS_COLORS[CLASS_FILE[class or ""] or class or ""]
    return c and { c.r, c.g, c.b } or ES.TEXT
end
local function presenceColor(p)
    return p == "online" and ES.GOOD or p == "away" and AWAY or p == "dnd" and ES.BAD or ES.MUTED
end
local PRESENCE_LABEL = { online = "Online", away = "Away", dnd = "Busy", offline = "Offline" }

local function ago(seconds)
    if not seconds or seconds <= 0 then return nil end
    if seconds < 3600 then return "less than an hour ago" end
    return SecondsToTime(seconds, true) .. " ago"
end
local function epoch(iso)
    local y, m, d, hh, mm, ss = (iso or ""):match("^(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)")
    if not y then return nil end
    return time({ year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = tonumber(hh), min = tonumber(mm),
                  sec = tonumber(ss) })
end

-- Model: friends --------------------------------------------------------------------------------------------------
local function characterFriends()
    local out = {}
    for i = 1, (GetNumFriends()) or 0 do
        local name, level, class, area, connected, status, note = GetFriendInfo(i)
        if name then
            local presence = not connected and "offline" or (status or ""):find("AFK") and "away" or
                (status or ""):find("DND") and "dnd" or "online"
            out[#out + 1] = { kind = "char", key = "c:" .. name, name = name, charName = name, level = level,
                              class = class, zone = area, presence = presence, note = note or "" }
        end
    end
    return out
end

local function accountFriend(f)
    local game = f.gamePresence
    local inGame = f.presence ~= "offline" and game and game.characterName
    return { kind = "account", key = "a:" .. f.tag, name = f.displayName or f.tag, tag = f.tag,
             presence = f.presence or "offline", status = f.statusMessage or "", favorite = f.favorite,
             charName = inGame and game.characterName or nil, level = inGame and tonumber(game.level) or nil,
             class = inGame and game.class or nil, race = inGame and game.race or nil,
             zone = inGame and (game.zone or game.area) or nil, lastSeen = epoch(f.lastSeen), note = f.note or "" }
end

local function friendRows()
    local rows = {}
    local query = panes.Friends and panes.Friends.search:GetText():lower() or ""
    local function keep(e)
        if query == "" then return true end
        for _, field in ipairs({ e.name, e.tag, e.charName, e.zone, e.class }) do
            if field and field:lower():find(query, 1, true) then return true end
        end
    end
    if view.friends == "requests" then
        for _, i in ipairs(social.incoming) do
            local e = { kind = "request", key = "r:" .. i.tag, name = i.displayName or i.tag, tag = i.tag, presence = "offline" }
            if keep(e) then rows[#rows + 1] = e end
        end
    elseif view.friends == "ignored" then
        for i = 1, (GetNumIgnores()) or 0 do
            local name = GetIgnoreName(i)
            local e = name and { kind = "ignore", key = "i:" .. name, name = name, presence = "offline" }
            if e and keep(e) then rows[#rows + 1] = e end
        end
    else
        local all = characterFriends()
        for _, f in ipairs(social.friends) do all[#all + 1] = accountFriend(f) end
        for _, e in ipairs(all) do
            if (view.friends == "all" or e.presence ~= "offline") and keep(e) then rows[#rows + 1] = e end
        end
        -- Favourites, then online, then by name.
        table.sort(rows, function(a, b)
            if (a.favorite and 1 or 0) ~= (b.favorite and 1 or 0) then return a.favorite and true or false end
            local ao, bo = a.presence ~= "offline", b.presence ~= "offline"
            if ao ~= bo then return ao end
            return a.name:lower() < b.name:lower()
        end)
    end
    return rows
end

local function friendCounts()
    local online, total = 0, 0
    for _, e in ipairs(characterFriends()) do total = total + 1; if e.presence ~= "offline" then online = online + 1 end end
    for _, f in ipairs(social.friends) do total = total + 1; if f.presence ~= "offline" then online = online + 1 end end
    return online, total, #social.incoming, (GetNumIgnores()) or 0
end

local refresh

local function refreshAccount()
    if not service() then social.connected = false; return end
    local acc = { friends = {}, incoming = {} }
    local function page(n)
        request({ op = "feed", page = n }, function(result)
            if result.error then social.connected = false; social.error = result.message or result.error; refresh(); return end
            for _, f in ipairs(result.friends or {}) do acc.friends[#acc.friends + 1] = f end
            for _, i in ipairs(result.incoming or {}) do acc.incoming[#acc.incoming + 1] = i end
            if result.hasMore then return page(n + 1) end
            social.connected, social.error = true, nil
            social.me, social.friends, social.incoming = result.me, acc.friends, acc.incoming
            refresh()
        end)
    end
    page(0)
end

-- Model: communities ----------------------------------------------------------------------------------------------
local function communityById(id) for _, c in ipairs(social.communities) do if c.id == id then return c end end end

local function messageKey(communityId, channelId) return communityId .. ":" .. channelId end

local function fetchHistory(communityId, channelId)
    request({ op = "cHistory", id = communityId, channelId = channelId, limit = 50 }, function(result)
        if result.error then return end
        local key = messageKey(communityId, channelId)
        local store = social.messages[key] or { byId = {}, list = {} }
        social.messages[key] = store
        for _, m in ipairs(result.messages or {}) do
            if not store.byId[m.id] then
                store.byId[m.id] = m
                store.list[#store.list + 1] = m
            end
        end
        table.sort(store.list, function(a, b) return a.id < b.id end)
        if store.list[#store.list] then
            request({ op = "cRead", id = communityId, channelId = channelId, cursor = store.list[#store.list].id })
        end
        if selected.community == communityId and selected.channel == channelId then refresh() end
    end)
end

local function loadMembers(communityId)
    request({ op = "cGet", id = communityId }, function(result)
        if result.error then return end
        social.members[communityId] = result.members or {}
        refresh()
    end)
end

local function refreshCommunities()
    request({ op = "cList" }, function(result)
        if result.error then return end
        social.communities = result.communities or {}
        social.invites = result.invites or {}
        refresh()
    end)
end

EsteriaSocial_Test = { social = social, view = view, selected = selected, friendRows = friendRows,
                       friendCounts = friendCounts, accountFriend = accountFriend }

-- Widgets -----------------------------------------------------------------------------------------------------------
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

-- A button that asks once more before doing something that can't be undone.
local function confirmButton(parent, width, text, action)
    local b = button(parent, width, text)
    b.label:SetTextColor(unpack(ES.BAD))
    b.text = text
    b:SetScript("OnClick", function(self)
        if self.armed then
            self.armed = nil; self.label:SetText(self.text); action()
        else
            self.armed = GetTime(); self.label:SetText("Sure?")
            PlaySound("igMainMenuOptionCheckBoxOn")
        end
    end)
    b:SetScript("OnUpdate", function(self)
        if self.armed and GetTime() - self.armed > 3 then self.armed = nil; self.label:SetText(self.text) end
    end)
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

local function railRow(parent, onClick)
    local row = CreateFrame("Button", nil, parent)
    row:SetSize(RAIL_W - 12, 28)
    row.wash = ES.Solid(row, "BACKGROUND", ES.GOLD, 0); row.wash:SetAllPoints()
    row.accent = ES.Solid(row, "ARTWORK", ES.GOLD, 0)
    row.accent:SetPoint("TOPLEFT"); row.accent:SetPoint("BOTTOMLEFT"); row.accent:SetWidth(3)
    row.dot = ES.Solid(row, "ARTWORK", ES.GOOD, 0); row.dot:SetSize(6, 6); row.dot:SetPoint("LEFT", 12, 0)
    row.label = ES.Text(row, 12, ES.TEXT); row.label:SetPoint("LEFT", 14, 0); row.label:SetPoint("RIGHT", -40, 0)
    row.label:SetJustifyH("LEFT")
    row.count = ES.Text(row, 11, ES.MUTED); row.count:SetPoint("RIGHT", -10, 0)
    row:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); onClick(self) end)
    row:SetScript("OnEnter", function(self) if not self.selected then self.wash:SetAlpha(0.06) end end)
    row:SetScript("OnLeave", function(self) if not self.selected then self.wash:SetAlpha(0) end end)
    row.Paint = function(self, label, on, count, dot, countColor)
        self.selected = on
        self.label:SetText(label)
        self.label:ClearAllPoints(); self.label:SetPoint("LEFT", dot and 24 or 14, 0); self.label:SetPoint("RIGHT", -40, 0)
        self.dot:SetAlpha(dot and 1 or 0)
        if dot then self.dot:SetVertexColor(dot[1], dot[2], dot[3], 1) end
        self.wash:SetAlpha(on and 0.12 or 0); self.accent:SetAlpha(on and 1 or 0)
        self.label:SetTextColor(unpack(on and ES.GOLD or ES.TEXT))
        self.count:SetText(count or "")
        self.count:SetTextColor(unpack(countColor or ES.MUTED))
    end
    return row
end

local function editBox(parent, width, hint, onEnter, maxLetters)
    local e = CreateFrame("EditBox", nil, parent)
    e:SetSize(width, 26); e:SetAutoFocus(false); e:SetFontObject(ChatFontNormal); e:SetTextInsets(10, 8, 0, 0)
    e:SetMaxLetters(maxLetters or 255)
    ES.Solid(e, "BACKGROUND", ES.INK, 0.95):SetAllPoints()
    e.lines = ES.Border(e, ES.GOLD_DIM)
    e.hint = ES.Text(e, 11, ES.MUTED); e.hint:SetPoint("LEFT", 10, 0); e.hint:SetPoint("RIGHT", -8, 0)
    e.hint:SetJustifyH("LEFT"); e.hint:SetText(hint)
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

-- A person row: presence dot (or class stripe), name, a line under it, something on the right.
local function personRow(parent, w, h, onClick)
    local r = CreateFrame("Button", nil, parent); r:SetSize(w, h - 2)
    r.wash = ES.Solid(r, "BACKGROUND", ES.GOLD, 0); r.wash:SetAllPoints()
    r.edge = ES.Solid(r, "ARTWORK", ES.GOLD, 0); r.edge:SetPoint("TOPLEFT"); r.edge:SetPoint("BOTTOMLEFT"); r.edge:SetWidth(3)
    r.dot = ES.Solid(r, "ARTWORK", ES.GOOD, 1); r.dot:SetSize(8, 8); r.dot:SetPoint("LEFT", 12, 0)
    r.name = ES.Text(r, 12, ES.TEXT); r.name:SetPoint("TOPLEFT", 30, -6); r.name:SetPoint("RIGHT", -96, 0)
    r.name:SetJustifyH("LEFT")
    r.sub = ES.Text(r, 10, ES.MUTED); r.sub:SetPoint("BOTTOMLEFT", 30, 6); r.sub:SetPoint("RIGHT", -12, 0)
    r.sub:SetJustifyH("LEFT")
    r.right = ES.Text(r, 10, ES.MUTED); r.right:SetPoint("TOPRIGHT", -10, -7); r.right:SetJustifyH("RIGHT")
    r:SetScript("OnEnter", function(self) if not self.on then self.wash:SetAlpha(0.06) end end)
    r:SetScript("OnLeave", function(self) if not self.on then self.wash:SetAlpha(0) end end)
    r:SetScript("OnClick", function(self) PlaySound("igMainMenuOptionCheckBoxOn"); onClick(self.item) end)
    r.Paint = function(self, on, dot, name, nameColor, sub, right)
        self.on = on
        self.wash:SetAlpha(on and 0.12 or 0); self.edge:SetAlpha(on and 1 or 0)
        self.dot:SetVertexColor(dot[1], dot[2], dot[3], 1)
        self.name:SetText(name); self.name:SetTextColor(unpack(on and ES.GOLD or nameColor or ES.TEXT))
        self.sub:SetText(sub or ""); self.right:SetText(right or "")
    end
    return r
end

-- A profile card on the right: header (icon, name, subtitle, pills), labelled lines, then buttons.
local function card(parent)
    local d = panel(parent)
    d.tint = gradient(d, 100, 0.22)
    d.icon = CreateFrame("Frame", nil, d); d.icon:SetSize(50, 50); d.icon:SetPoint("TOPLEFT", 16, -16)
    d.icon.tex = d.icon:CreateTexture(nil, "ARTWORK")
    d.icon.tex:SetPoint("TOPLEFT", 1, -1); d.icon.tex:SetPoint("BOTTOMRIGHT", -1, 1)
    d.icon.lines = ES.Border(d.icon, ES.GOLD)
    d.title = ES.Text(d, 15, ES.GOLD); d.title:SetPoint("TOPLEFT", 78, -18); d.title:SetPoint("RIGHT", -12, 0)
    d.title:SetJustifyH("LEFT")
    d.sub = ES.Text(d, 11, ES.MUTED); d.sub:SetPoint("TOPLEFT", d.title, "BOTTOMLEFT", 0, -4); d.sub:SetPoint("RIGHT", -12, 0)
    d.sub:SetJustifyH("LEFT")
    d.pills = { pill(d), pill(d) }
    d.pills[1]:SetPoint("TOPLEFT", d.sub, "BOTTOMLEFT", 0, -6)
    d.pills[2]:SetPoint("LEFT", d.pills[1], "RIGHT", 6, 0)
    d.none = ES.Text(d, 12, ES.MUTED); d.none:SetPoint("CENTER"); d.none:SetWidth(DETAIL_W - 40)
    d.lines, d.used = {}, 0
    d.Line = function(self, label, value, color)
        if not value or value == "" then return end
        self.used = self.used + 1
        local l = self.lines[self.used]
        if not l then
            l = { label = ES.Text(self, 10, ES.GOLD_DIM), value = ES.Text(self, 12, ES.TEXT) }
            l.value:SetJustifyH("LEFT"); l.value:SetWidth(DETAIL_W - 32)
            self.lines[self.used] = l
        end
        local y = self.y
        l.label:ClearAllPoints(); l.label:SetPoint("TOPLEFT", 16, -y); l.label:SetText(label:upper()); l.label:Show()
        l.value:ClearAllPoints(); l.value:SetPoint("TOPLEFT", 16, -(y + 14)); l.value:SetText(value)
        l.value:SetTextColor(unpack(color or ES.TEXT)); l.value:Show()
        self.y = y + 14 + l.value:GetStringHeight() + 10
    end
    d.Begin = function(self, empty)
        for i = 1, self.used do self.lines[i].label:Hide(); self.lines[i].value:Hide() end
        self.used, self.y = 0, 112
        for _, p in ipairs(self.pills) do p:Hide() end
        shown(self.none, empty ~= nil)
        if empty then self.none:SetText(empty) end
        for _, region in ipairs({ self.icon, self.title, self.sub }) do shown(region, empty == nil) end
    end
    d.Portrait = function(self, classFile, fallback)
        local coords = classFile and CLASS_ICON_TCOORDS and CLASS_ICON_TCOORDS[classFile]
        if coords then
            self.icon.tex:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Classes")
            self.icon.tex:SetTexCoord(unpack(coords))
        else
            self.icon.tex:SetTexture(fallback or "Interface\\Icons\\INV_Misc_GroupNeedMore")
            self.icon.tex:SetTexCoord(0.08, 0.92, 0.08, 0.92)
        end
    end
    -- Bottom action buttons, two per row (up to five plus one that asks before acting), first action top-left.
    local bw = math.floor((DETAIL_W - 30) / 2)
    d.buttons = {}
    for i = 1, 6 do d.buttons[i] = button(d, bw, "", i == 1) end
    d.confirm = confirmButton(d, bw, "", function() if d.onConfirm then d.onConfirm() end end)
    d.Actions = function(self, list, danger)
        local slots = #list + (danger and 1 or 0)
        local rows = math.ceil(slots / 2)
        local function place(region, i)
            region:ClearAllPoints()
            region:SetPoint("BOTTOMLEFT", 12 + ((i - 1) % 2) * (bw + 6), 12 + (rows - math.ceil(i / 2)) * 32)
        end
        for i, b in ipairs(self.buttons) do
            local a = list[i]
            if a then
                place(b, i); b.label:SetText(a[1]); b:SetScript("OnClick", a[2]); b:Active(a[3] ~= false); b:Show()
            else
                b:Hide()
            end
        end
        if danger then
            place(self.confirm, #list + 1)
            self.confirm.text = danger[1]; self.confirm.label:SetText(danger[1]); self.confirm.armed = nil
            self.onConfirm = danger[2]
            self.confirm:Show()
        else
            self.confirm:Hide()
        end
        self.actionRows = rows
    end
    return d
end

local function whisper(name) if name and ChatFrame_SendTell then ChatFrame_SendTell(name) end end
local function invite(name) if name then InviteUnit(name) end end

-- Friends -----------------------------------------------------------------------------------------------------------
local FRIEND_VIEWS = { { "online", "Online" }, { "all", "All friends" }, { "requests", "Requests" }, { "ignored", "Ignored" } }

local function setPresence(value, message)
    request({ op = "presence", presence = value, statusMessage = message }, function() refreshAccount() end)
end

local function addFriend(box, text)
    text = strtrim(text or "")
    if text == "" then return end
    if text:find("#", 1, true) then
        request({ op = "friend", username = text }, function(result)
            UIErrorsFrame:AddMessage(result.error and ("Friend request failed: " .. tostring(result.message or result.error))
                or ("Friend request sent to " .. text), 1, result.error and 0.1 or 0.82, 0.1)
            refreshAccount()
        end)
    else
        AddFriend(text)
    end
    box:SetText(""); box:ClearFocus()
end

local function buildFriends(p)
    local rail = panel(p); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    heading(rail, "My status"):SetPoint("TOPLEFT", 14, -12)
    p.me = ES.Text(rail, 12, ES.TEXT); p.me:SetPoint("TOPLEFT", 14, -30); p.me:SetPoint("RIGHT", -12, 0); p.me:SetJustifyH("LEFT")
    p.presence = {}
    local seg = math.floor((RAIL_W - 28 - 8) / 3)
    for i, spec in ipairs({ { "online", "Online" }, { "away", "Away" }, { "dnd", "Busy" } }) do
        local b = button(rail, seg, spec[2], false, 22); b:SetPoint("TOPLEFT", 14 + (i - 1) * (seg + 4), -50)
        b:SetScript("OnClick", function() PlaySound("igMainMenuOptionCheckBoxOn"); setPresence(spec[1]) end)
        b.key = spec[1]
        p.presence[i] = b
    end
    p.status = editBox(rail, RAIL_W - 28, "Set a status message", function(self, text)
        setPresence(nil, text); self:ClearFocus()
    end, 120)
    p.status:SetPoint("TOPLEFT", 14, -80)
    heading(rail, "Friends"):SetPoint("TOPLEFT", 14, -122)
    p.views = {}
    for i, spec in ipairs(FRIEND_VIEWS) do
        local row = railRow(rail, function() view.friends = spec[1]; selected.friend = nil; p.list.offset = 0; refresh() end)
        row:SetPoint("TOPLEFT", 6, -138 - (i - 1) * 30)
        row.key, row.text = spec[1], spec[2]
        p.views[i] = row
    end
    p.offline = ES.Text(rail, 10, ES.MUTED); p.offline:SetPoint("TOPLEFT", 14, -268); p.offline:SetPoint("RIGHT", -12, 0)
    p.offline:SetJustifyH("LEFT")
    heading(rail, "Add a friend"):SetPoint("BOTTOMLEFT", 14, 74)
    p.add = editBox(rail, RAIL_W - 28, "Character, or Name#1234", addFriend, 64); p.add:SetPoint("BOTTOMLEFT", 14, 44)
    local addButton = button(rail, RAIL_W - 28, "Add friend", true); addButton:SetPoint("BOTTOMLEFT", 14, 12)
    addButton:SetScript("OnClick", function() addFriend(p.add, p.add:GetText()) end)

    local middle = panel(p); middle:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); middle:SetPoint("BOTTOM", 0, 14)
    middle:SetWidth(MID_W)
    p.search = editBox(middle, MID_W - 24, "Search friends by name, tag, zone or class", nil)
    p.search:SetPoint("TOPLEFT", 12, -12)
    p.search:HookScript("OnTextChanged", function() p.list.offset = 0; refresh() end)
    p.list = vlist(middle, MID_W - 16, ROW_H, 11, function(parent, w, h)
        return personRow(parent, w, h, function(item) selected.friend = item.key; refresh() end)
    end, function(r, e)
        local sub, right
        if e.kind == "request" then sub, right = "Wants to be your friend", e.tag
        elseif e.kind == "ignore" then sub = "Ignored"
        elseif e.charName and e.presence ~= "offline" then
            sub = ("%s%s%s"):format(e.level and ("Level " .. e.level .. " ") or "", e.class or "",
                e.zone and e.zone ~= "" and ("  ·  " .. e.zone) or "")
            if e.kind == "account" and e.charName ~= e.name then sub = e.charName .. "  ·  " .. sub end
        elseif e.kind == "account" and e.presence ~= "offline" then sub = "In the Esteria launcher"
        else sub = e.lastSeen and ("Last seen " .. (ago(time() - e.lastSeen) or "recently")) or "Offline" end
        if e.kind == "account" then right = e.status ~= "" and e.status or e.tag end
        if e.kind == "char" and e.note ~= "" then right = e.note end
        local nameColor = e.kind == "char" and e.presence ~= "offline" and classColor(e.class) or
            (e.presence == "offline" and ES.MUTED or ES.TEXT)
        r:Paint(selected.friend == e.key, e.kind == "request" and ES.GOLD or presenceColor(e.presence),
            (e.favorite and "|cffe8c46b*|r " or "") .. e.name, nameColor, sub, right)
    end)
    p.list:SetPoint("TOPLEFT", 8, -46)

    p.detail = card(p); p.detail:SetPoint("TOPLEFT", middle, "TOPRIGHT", 14, 0); p.detail:SetPoint("BOTTOMRIGHT", -14, 14)
    -- Account whispers (friend not in game) are written right on the card.
    p.compose = editBox(p.detail, DETAIL_W - 24, "Whisper over Esteria, Enter to send", function(self, text)
        local e = p.composeTo
        if e and text ~= "" and EsteriaChat and EsteriaChat.sendAccountWhisper then
            EsteriaChat.sendAccountWhisper(e.tag, text)
            self:SetText("")
        end
        self:ClearFocus()
    end, 255)
    p.compose:SetPoint("BOTTOMLEFT", 12, 80)
    p.note = editBox(p.detail, DETAIL_W - 24, "Add a note, Enter to save", function(self, text)
        if p.noteFor then SetFriendNotes(p.noteFor, text) end
        self:ClearFocus()
    end, 48)
    p.note:SetPoint("BOTTOMLEFT", 12, 112)
end

local function paintFriends(p)
    local online, total, requests, ignored = friendCounts()
    local counts = { online = online, all = total, requests = requests, ignored = ignored }
    for _, row in ipairs(p.views) do
        row:Paint(row.text, view.friends == row.key, counts[row.key],
            row.key == "online" and ES.GOOD or nil, row.key == "requests" and requests > 0 and ES.BAD or nil)
    end
    local me = social.me
    if social.connected and me then
        local chosen = me.explicitPresence or me.presence or "online"
        p.me:SetText(("|cff%02x%02x%02x•|r %s"):format(presenceColor(chosen)[1] * 255, presenceColor(chosen)[2] * 255,
            presenceColor(chosen)[3] * 255, me.displayName or me.tag))
        for _, b in ipairs(p.presence) do
            b:Active(true); ES.Tint(b.lines, b.key == chosen and ES.GOLD or ES.GOLD_DIM, 1)
        end
        if not p.status:HasFocus() then p.status:SetText(me.statusMessage or "") end
        p.status:Show()
        p.offline:SetText("")
    else
        p.me:SetText(UnitName("player"))
        for _, b in ipairs(p.presence) do b:Active(false) end
        p.status:Hide()
        p.offline:SetText(service() and ("Esteria account friends are offline" .. (social.error and (": " .. social.error) or "."))
            or "Account friends need the EsteriaChat add-on.")
    end

    p.list.data = friendRows()
    p.list.empty:SetText(view.friends == "requests" and "No friend requests." or view.friends == "ignored" and
        "You aren't ignoring anyone." or "No friends here yet. Add one at the bottom left.")
    p.list:Refresh()

    local d, e = p.detail, nil
    for _, row in ipairs(p.list.data) do if row.key == selected.friend then e = row end end
    p.compose:Hide(); p.note:Hide(); p.composeTo, p.noteFor = nil, nil
    if not e then d:Begin("Select a friend to see what they're up to."); d:Actions({}); return end
    d:Begin()
    d:Portrait(CLASS_FILE[e.class or ""])
    d.title:SetText(e.name)
    d.sub:SetText(e.tag or (e.kind == "char" and "Character friend") or "")
    if e.kind == "request" then d.pills[1]:Set("Friend request", ES.GOLD)
    elseif e.kind == "ignore" then d.pills[1]:Set("Ignored", ES.BAD)
    else
        d.pills[1]:Set(PRESENCE_LABEL[e.presence] or "Offline", presenceColor(e.presence))
        d.pills[2]:Set(e.kind == "account" and "Esteria account" or "Character", ES.GOLD_DIM)
    end
    if e.status and e.status ~= "" then d:Line("Status", e.status, ES.GOLD) end
    if e.charName and e.presence ~= "offline" then
        d:Line("Playing", e.charName)
        d:Line("Character", ("%s%s %s"):format(e.level and ("Level " .. e.level .. " ") or "", e.race or "", e.class or ""),
            classColor(e.class))
        d:Line("Zone", e.zone)
    elseif e.lastSeen then
        d:Line("Last seen", ago(time() - e.lastSeen))
    end
    if e.kind == "char" and e.note ~= "" then d:Line("Note", e.note) end

    local inGame = e.charName and e.presence ~= "offline"
    if e.kind == "request" then
        d:Actions({
            { "Accept", function() request({ op = "accept", username = e.tag }, function() refreshAccount() end) end },
            { "Decline", function() request({ op = "removeFriend", username = e.tag }, function() refreshAccount() end) end },
        })
    elseif e.kind == "ignore" then
        d:Actions({ { "Stop ignoring", function() DelIgnore(e.name) end } })
    elseif e.kind == "char" then
        p.note:Show(); p.noteFor = e.name
        if not p.note:HasFocus() then p.note:SetText(e.note) end
        d:Actions({ { "Whisper", function() whisper(e.name) end, inGame },
                    { "Invite", function() invite(e.name) end, inGame },
                    { "Ignore", function() AddIgnore(e.name) end } },
            { "Remove", function() RemoveFriend(e.name); selected.friend = nil end })
    else
        if not inGame then p.compose:Show(); p.composeTo = e end
        d:Actions({ { "Whisper", function()
                        if inGame then whisper(e.charName) else p.compose:SetFocus() end
                    end, e.presence ~= "offline" },
                    { "Invite", function() invite(e.charName) end, inGame and true or false } },
            { "Remove", function() request({ op = "removeFriend", username = e.tag }, function() refreshAccount() end)
                selected.friend = nil end })
    end
end

-- Who ---------------------------------------------------------------------------------------------------------------
local function who(query)
    view.whoQuery = query
    if SetWhoToUI then SetWhoToUI(1) end
    SendWho(query)
end

local function buildWho(p)
    local rail = panel(p); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    heading(rail, "Quick searches"):SetPoint("TOPLEFT", 14, -12)
    local quick = {
        { "In my zone", function() return 'z-"' .. (GetRealZoneText() or "") .. '"' end },
        { "Near my level", function()
            local level = UnitLevel("player")
            return ("%d-%d"):format(math.max(1, level - 3), level + 3)
        end },
        { "My guild", function() local g = GetGuildInfo("player"); return g and ('g-"' .. g .. '"') end },
        { "My class", function() return 'c-"' .. (UnitClass("player")) .. '"' end },
        { "Max level", function() return tostring(GetMaxPlayerLevel and GetMaxPlayerLevel() or 80) end },
    }
    p.quick = {}
    for i, q in ipairs(quick) do
        local row = railRow(rail, function()
            local text = q[2]()
            if text then p.search:SetText(text); who(text) end
        end)
        row:SetPoint("TOPLEFT", 6, -28 - (i - 1) * 30)
        row:Paint(q[1], false)
        p.quick[i] = row
    end
    heading(rail, "Search tips"):SetPoint("TOPLEFT", 14, -(28 + #quick * 30 + 14))
    local tips = ES.Text(rail, 10, ES.MUTED); tips:SetPoint("TOPLEFT", 14, -(28 + #quick * 30 + 32))
    tips:SetWidth(RAIL_W - 28); tips:SetJustifyH("LEFT")
    tips:SetText('Type a name, level range (70-80), or prefix: z-"zone", g-"guild", c-"class", r-"race". ' ..
        "/who results open here too.")

    local middle = panel(p); middle:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); middle:SetPoint("BOTTOM", 0, 14)
    middle:SetWidth(MID_W)
    p.search = editBox(middle, MID_W - 112, "Who are you looking for?", function(self, text) who(text); self:ClearFocus() end)
    p.search:SetPoint("TOPLEFT", 12, -12)
    local go = button(middle, 82, "Search", true); go:SetPoint("TOPRIGHT", -12, -12)
    go:SetScript("OnClick", function() who(p.search:GetText()) end)
    p.total = ES.Text(middle, 10, ES.MUTED); p.total:SetPoint("TOPLEFT", 14, -46)
    p.list = vlist(middle, MID_W - 16, ROW_H, 11, function(parent, w, h)
        return personRow(parent, w, h, function(item) selected.who = item.name; refresh() end)
    end, function(r, e)
        r:Paint(selected.who == e.name, classColor(e.classFile or e.class), e.name, classColor(e.classFile or e.class),
            ("Level %d %s %s"):format(e.level or 0, e.race or "", e.class or ""), e.zone)
        r.sub:SetText(r.sub:GetText() .. (e.guild ~= "" and ("  ·  <" .. e.guild .. ">") or ""))
    end)
    p.list:SetPoint("TOPLEFT", 8, -60)
    p.list.empty:SetText("Search for players on the left or above.")
    p.detail = card(p); p.detail:SetPoint("TOPLEFT", middle, "TOPRIGHT", 14, 0); p.detail:SetPoint("BOTTOMRIGHT", -14, 14)
end

local function paintWho(p)
    local rows = {}
    local n, total = GetNumWhoResults()
    for i = 1, n or 0 do
        local name, guild, level, race, class, zone, classFile = GetWhoInfo(i)
        if name then rows[#rows + 1] = { name = name, guild = guild or "", level = level, race = race, class = class,
                                         zone = zone, classFile = classFile } end
    end
    p.list.data = rows; p.list:Refresh()
    p.total:SetText(view.whoQuery and (("%d shown%s"):format(#rows, (total or 0) > #rows and
        (" of " .. total .. " (narrow the search to see the rest)") or "")) or "")
    local d, e = p.detail, nil
    for _, row in ipairs(rows) do if row.name == selected.who then e = row end end
    if not e then d:Begin("Select a player."); d:Actions({}); return end
    d:Begin(); d:Portrait(e.classFile)
    d.title:SetText(e.name); d.sub:SetText(e.guild ~= "" and ("<" .. e.guild .. ">") or "No guild")
    d.pills[1]:Set("Level " .. (e.level or 0), ES.GOLD)
    d:Line("Character", ("%s %s"):format(e.race or "", e.class or ""), classColor(e.classFile))
    d:Line("Zone", e.zone)
    d:Actions({ { "Whisper", function() whisper(e.name) end }, { "Invite", function() invite(e.name) end },
                { "Add friend", function() AddFriend(e.name) end }, { "Ignore", function() AddIgnore(e.name) end } })
end

-- Guild -------------------------------------------------------------------------------------------------------------
local GUILD_SORTS = { { "rank", "Rank" }, { "name", "Name" }, { "level", "Level" }, { "zone", "Zone" }, { "online", "Last online" } }

local function guildMembers()
    local out = {}
    for i = 1, (GetNumGuildMembers(true)) or 0 do
        local name, rank, rankIndex, level, class, zone, note, officerNote, online, status, classFile = GetGuildRosterInfo(i)
        if name then
            local y, m, d, h = GetGuildRosterLastOnline(i)
            local hours = online and 0 or ((y or 0) * 8760 + (m or 0) * 730 + (d or 0) * 24 + (h or 0))
            out[#out + 1] = { index = i, name = name, rank = rank, rankIndex = rankIndex, level = level, class = class,
                              zone = zone, note = note or "", officerNote = officerNote or "", online = online and true or false,
                              status = status, classFile = classFile, hours = hours }
        end
    end
    return out
end

local function lastOnline(e)
    if e.online then return "Online" end
    if e.hours < 1 then return "Less than an hour ago" end
    if e.hours < 24 then return e.hours .. " hours ago" end
    local days = math.floor(e.hours / 24)
    if days < 60 then return days .. (days == 1 and " day ago" or " days ago") end
    return math.floor(days / 30) .. " months ago"
end

local function buildGuild(p)
    p.none = CreateFrame("Frame", nil, p); p.none:SetAllPoints()
    local lonely = panel(p.none); lonely:SetSize(460, 200); lonely:SetPoint("CENTER")
    gradient(lonely, 90, 0.2)
    local t = ES.Text(lonely, 20, ES.GOLD); t:SetPoint("TOP", 0, -40); t:SetText("You are not in a guild")
    local s = ES.Text(lonely, 12, ES.MUTED); s:SetPoint("TOP", t, "BOTTOM", 0, -14); s:SetWidth(400)
    s:SetText("Ask a guild officer for an invite, or start one with a guild master at any capital's charter registrar.")

    p.body = CreateFrame("Frame", nil, p); p.body:SetAllPoints()
    local rail = panel(p.body); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    gradient(rail, 90, 0.18)
    p.guildName = ES.Text(rail, 16, ES.GOLD); p.guildName:SetPoint("TOPLEFT", 14, -14); p.guildName:SetWidth(RAIL_W - 28)
    p.guildName:SetJustifyH("LEFT")
    p.myRank = ES.Text(rail, 11, ES.MUTED); p.myRank:SetPoint("TOPLEFT", p.guildName, "BOTTOMLEFT", 0, -4)
    p.counts = ES.Text(rail, 11, ES.TEXT); p.counts:SetPoint("TOPLEFT", p.myRank, "BOTTOMLEFT", 0, -6)
    heading(rail, "View"):SetPoint("TOPLEFT", 14, -104)
    p.views = {}
    for i, spec in ipairs({ { "roster", "Roster" }, { "info", "Guild info" } }) do
        local row = railRow(rail, function() view.guild = spec[1]; refresh() end)
        row:SetPoint("TOPLEFT", 6, -120 - (i - 1) * 30); row.key, row.text = spec[1], spec[2]
        p.views[i] = row
    end
    heading(rail, "Roster"):SetPoint("TOPLEFT", 14, -190)
    p.offlineToggle = railRow(rail, function() db.guildOffline = not db.guildOffline or nil; p.list.offset = 0; refresh() end)
    p.offlineToggle:SetPoint("TOPLEFT", 6, -206)
    p.sort = button(rail, RAIL_W - 28, "", false, 24); p.sort:SetPoint("TOPLEFT", 14, -244)
    p.sort:SetScript("OnClick", function()
        local idx = 1
        for i, s2 in ipairs(GUILD_SORTS) do if s2[1] == (db.guildSort or "rank") then idx = i end end
        db.guildSort = GUILD_SORTS[idx % #GUILD_SORTS + 1][1]; p.list.offset = 0; refresh()
    end)
    heading(rail, "Invite"):SetPoint("BOTTOMLEFT", 14, 108)
    p.invite = editBox(rail, RAIL_W - 28, "Character name", function(self, text)
        if text ~= "" then GuildInvite(text) end
        self:SetText(""); self:ClearFocus()
    end, 24)
    p.invite:SetPoint("BOTTOMLEFT", 14, 78)
    p.inviteButton = button(rail, RAIL_W - 28, "Invite to guild", true); p.inviteButton:SetPoint("BOTTOMLEFT", 14, 46)
    p.inviteButton:SetScript("OnClick", function()
        if p.invite:GetText() ~= "" then GuildInvite(p.invite:GetText()); p.invite:SetText("") end
    end)
    p.leave = confirmButton(rail, RAIL_W - 28, "Leave guild", function() GuildLeave() end)
    p.leave:SetPoint("BOTTOMLEFT", 14, 14)

    local middle = panel(p.body); middle:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); middle:SetPoint("BOTTOM", 0, 14)
    middle:SetWidth(MID_W)
    p.roster = CreateFrame("Frame", nil, middle); p.roster:SetAllPoints()
    p.search = editBox(p.roster, MID_W - 24, "Search members, ranks, zones or notes", nil)
    p.search:SetPoint("TOPLEFT", 12, -12)
    p.search:HookScript("OnTextChanged", function() p.list.offset = 0; refresh() end)
    p.list = vlist(p.roster, MID_W - 16, ROW_H, 11, function(parent, w, h)
        return personRow(parent, w, h, function(item) selected.member = item.name; refresh() end)
    end, function(r, e)
        local color = e.online and classColor(e.classFile) or ES.MUTED
        local status = e.status == 1 and " |cffffc740<Away>|r" or e.status == 2 and " |cfff26b5c<Busy>|r" or ""
        r:Paint(selected.member == e.name, e.online and ES.GOOD or ES.MUTED, e.name .. status, color,
            ("Level %d %s  ·  %s"):format(e.level or 0, e.class or "", e.online and (e.zone or "") or lastOnline(e)),
            e.rank)
    end)
    p.list:SetPoint("TOPLEFT", 8, -46)

    p.info = CreateFrame("Frame", nil, middle); p.info:SetAllPoints()
    heading(p.info, "Message of the day"):SetPoint("TOPLEFT", 16, -16)
    p.motd = editBox(p.info, MID_W - 32, "No message of the day", function(self, text) GuildSetMOTD(text); self:ClearFocus() end, 128)
    p.motd:SetPoint("TOPLEFT", 16, -32)
    heading(p.info, "Guild information"):SetPoint("TOPLEFT", 16, -76)
    local holder = CreateFrame("ScrollFrame", nil, p.info); holder:SetPoint("TOPLEFT", 16, -92); holder:SetPoint("BOTTOMRIGHT", -16, 52)
    ES.Solid(holder, "BACKGROUND", ES.INK, 0.95):SetAllPoints(); ES.Border(holder, ES.GOLD_DIM)
    p.infoText = CreateFrame("EditBox", nil, holder); p.infoText:SetMultiLine(true); p.infoText:SetAutoFocus(false)
    p.infoText:SetFontObject(ChatFontNormal); p.infoText:SetWidth(MID_W - 52); p.infoText:SetTextInsets(8, 8, 8, 8)
    p.infoText:SetMaxLetters(500)
    p.infoText:SetScript("OnEscapePressed", p.infoText.ClearFocus)
    p.infoText:SetScript("OnEditFocusGained", function(self) if not p.canEditInfo then self:ClearFocus() end end)
    holder:SetScrollChild(p.infoText)
    holder:EnableMouse(true); holder:SetScript("OnMouseDown", function() if p.canEditInfo then p.infoText:SetFocus() end end)
    p.saveInfo = button(p.info, 140, "Save guild info", true); p.saveInfo:SetPoint("BOTTOMRIGHT", -16, 16)
    p.saveInfo:SetScript("OnClick", function() SetGuildInfoText(p.infoText:GetText()); p.infoText:ClearFocus() end)

    p.detail = card(p.body); p.detail:SetPoint("TOPLEFT", middle, "TOPRIGHT", 14, 0); p.detail:SetPoint("BOTTOMRIGHT", -14, 14)
    p.publicNote = editBox(p.detail, DETAIL_W - 24, "Public note, Enter to save", function(self, text)
        if p.noteIndex then GuildRosterSetPublicNote(p.noteIndex, text) end
        self:ClearFocus()
    end, 31)
    p.publicNote:SetPoint("BOTTOMLEFT", 12, 112)
    p.officerNote = editBox(p.detail, DETAIL_W - 24, "Officer note, Enter to save", function(self, text)
        if p.noteIndex then GuildRosterSetOfficerNote(p.noteIndex, text) end
        self:ClearFocus()
    end, 31)
    p.officerNote:SetPoint("BOTTOMLEFT", 12, 80)
end

local function sortMembers(list)
    local key = db.guildSort or "rank"
    table.sort(list, function(a, b)
        if key == "name" then return a.name < b.name end
        if key == "level" and a.level ~= b.level then return a.level > b.level end
        if key == "zone" and (a.zone or "") ~= (b.zone or "") then return (a.zone or "") < (b.zone or "") end
        if key == "online" and a.hours ~= b.hours then return a.hours < b.hours end
        if key == "rank" and a.rankIndex ~= b.rankIndex then return a.rankIndex < b.rankIndex end
        if a.online ~= b.online then return a.online end
        return a.name < b.name
    end)
end

local function paintGuild(p)
    local inGuild = IsInGuild()
    shown(p.none, not inGuild); shown(p.body, inGuild)
    if not inGuild then return end
    local guildName, rankName = GetGuildInfo("player")
    local members = guildMembers()
    local online = 0
    for _, m in ipairs(members) do if m.online then online = online + 1 end end
    p.guildName:SetText(guildName or GUILD or "Guild")
    p.myRank:SetText(rankName or "")
    p.counts:SetText(("|cff8cd97f%d|r online  ·  %d members"):format(online, #members))
    for _, row in ipairs(p.views) do row:Paint(row.text, view.guild == row.key) end
    p.offlineToggle:Paint(db.guildOffline and "Showing offline members" or "Hiding offline members", false,
        db.guildOffline and "on" or "off")
    for _, s in ipairs(GUILD_SORTS) do if s[1] == (db.guildSort or "rank") then p.sort.label:SetText("Sort: " .. s[2]) end end
    p.inviteButton:Active(CanGuildInvite() and true or false); p.invite:EnableMouse(CanGuildInvite() and true or false)

    shown(p.roster, view.guild == "roster"); shown(p.info, view.guild == "info")
    if view.guild == "info" then
        if not p.motd:HasFocus() then p.motd:SetText(GetGuildRosterMOTD() or "") end
        p.motd:EnableMouse(CanEditMOTD() and true or false)
        local canEdit = CanEditGuildInfo and CanEditGuildInfo() and true or false
        p.canEditInfo = canEdit
        if not p.infoText:HasFocus() then p.infoText:SetText(GetGuildInfoText() or "") end
        p.infoText:EnableMouse(canEdit)
        shown(p.saveInfo, canEdit)
    end

    local query = p.search:GetText():lower()
    local rows = {}
    for _, m in ipairs(members) do
        local ok = db.guildOffline or m.online
        if ok and query ~= "" then
            ok = false
            for _, field in ipairs({ m.name, m.rank, m.zone, m.note, m.class }) do
                if field and field:lower():find(query, 1, true) then ok = true end
            end
        end
        if ok then rows[#rows + 1] = m end
    end
    sortMembers(rows)
    p.list.data = rows
    p.list.empty:SetText("No members match.")
    p.list:Refresh()

    local d, e = p.detail, nil
    for _, m in ipairs(members) do if m.name == selected.member then e = m end end
    p.publicNote:Hide(); p.officerNote:Hide(); p.noteIndex = nil
    if not e then d:Begin("Select a member."); d:Actions({}); return end
    d:Begin(); d:Portrait(e.classFile)
    d.title:SetText(e.name); d.title:SetTextColor(unpack(classColor(e.classFile)))
    d.sub:SetText(("Level %d %s"):format(e.level or 0, e.class or ""))
    d.pills[1]:Set(e.rank, ES.GOLD)
    d.pills[2]:Set(e.online and "Online" or "Offline", e.online and ES.GOOD or ES.MUTED)
    d:Line(e.online and "Zone" or "Last online", e.online and e.zone or lastOnline(e))
    if not CanEditPublicNote() then d:Line("Note", e.note) end
    if CanViewOfficerNote() and not CanEditOfficerNote() then d:Line("Officer note", e.officerNote) end
    p.noteIndex = e.index
    if CanEditPublicNote() then p.publicNote:Show(); if not p.publicNote:HasFocus() then p.publicNote:SetText(e.note) end end
    if CanEditOfficerNote() then p.officerNote:Show(); if not p.officerNote:HasFocus() then p.officerNote:SetText(e.officerNote) end end
    local isMe = e.name == UnitName("player")
    local actions = { { "Whisper", function() whisper(e.name) end, e.online and not isMe },
                      { "Invite", function() invite(e.name) end, e.online and not isMe } }
    if CanGuildPromote() or CanGuildDemote() then
        actions[3] = { "Promote", function() GuildPromote(e.name) end, CanGuildPromote() and not isMe and true or false }
        actions[4] = { "Demote", function() GuildDemote(e.name) end, CanGuildDemote() and not isMe and true or false }
    end
    d:Actions(actions, CanGuildRemove() and not isMe and { "Remove", function() GuildUninvite(e.name); selected.member = nil end } or nil)
    -- Notes sit just above the buttons.
    local top = 12 + (d.actionRows or 1) * 32 + 4
    p.officerNote:ClearAllPoints(); p.officerNote:SetPoint("BOTTOMLEFT", 12, top)
    p.publicNote:ClearAllPoints(); p.publicNote:SetPoint("BOTTOMLEFT", 12, top + (p.officerNote:IsShown() and 32 or 0))
end

-- Communities -------------------------------------------------------------------------------------------------------
local function buildCommunities(p)
    local rail = panel(p); rail:SetPoint("TOPLEFT", 14, -14); rail:SetPoint("BOTTOMLEFT", 14, 14); rail:SetWidth(RAIL_W)
    heading(rail, "Communities"):SetPoint("TOPLEFT", 14, -12)
    p.rows = {}
    for i = 1, 10 do
        local row = railRow(rail, function(self)
            if self.invite then selected.community, selected.invite = nil, self.invite
            else selected.community, selected.invite, selected.channel = self.community, nil, nil end
            refresh()
        end)
        row:SetPoint("TOPLEFT", 6, -28 - (i - 1) * 30)
        p.rows[i] = row
    end
    heading(rail, "Join or create"):SetPoint("BOTTOMLEFT", 14, 142)
    p.code = editBox(rail, RAIL_W - 28, "Invite code", function(self, text)
        if text ~= "" then request({ op = "cJoin", code = strtrim(text) }, function() refreshCommunities() end) end
        self:SetText(""); self:ClearFocus()
    end, 64)
    p.code:SetPoint("BOTTOMLEFT", 14, 112)
    local join = button(rail, RAIL_W - 28, "Join with code"); join:SetPoint("BOTTOMLEFT", 14, 82)
    join:SetScript("OnClick", function()
        local code = strtrim(p.code:GetText())
        if code ~= "" then request({ op = "cJoin", code = code }, function() refreshCommunities() end); p.code:SetText("") end
    end)
    p.newName = editBox(rail, RAIL_W - 28, "New community name", nil, 48); p.newName:SetPoint("BOTTOMLEFT", 14, 44)
    local create = button(rail, RAIL_W - 28, "Create community", true); create:SetPoint("BOTTOMLEFT", 14, 12)
    create:SetScript("OnClick", function()
        local name = strtrim(p.newName:GetText())
        if name ~= "" then
            request({ op = "cCreate", name = name, description = "" }, function() refreshCommunities() end)
            p.newName:SetText(""); p.newName:ClearFocus()
        end
    end)

    local chat = panel(p); chat:SetPoint("TOPLEFT", rail, "TOPRIGHT", 14, 0); chat:SetPoint("BOTTOM", 0, 14); chat:SetWidth(MID_W)
    gradient(chat, 70, 0.16)
    p.chat = chat
    p.title = ES.Text(chat, 15, ES.GOLD); p.title:SetPoint("TOPLEFT", 14, -12); p.title:SetPoint("RIGHT", -14, 0)
    p.title:SetJustifyH("LEFT")
    p.channels = {}
    for i = 1, 5 do
        local b = tabButton(chat, 74, "")
        b:SetHeight(24); b:SetPoint("TOPLEFT", 8 + (i - 1) * 76, -34)
        b:SetScript("OnClick", function(self)
            PlaySound("igMainMenuOptionCheckBoxOn"); selected.channel = self.channel; refresh()
            fetchHistory(selected.community, self.channel)
        end)
        p.channels[i] = b
    end
    p.topic = ES.Text(chat, 10, ES.MUTED); p.topic:SetPoint("TOPLEFT", 14, -64); p.topic:SetPoint("RIGHT", -14, 0)
    p.topic:SetJustifyH("LEFT")
    p.log = CreateFrame("ScrollingMessageFrame", nil, chat)
    p.log:SetPoint("TOPLEFT", 14, -82); p.log:SetPoint("BOTTOMRIGHT", -14, 50)
    p.log:SetFontObject(ChatFontNormal); p.log:SetJustifyH("LEFT"); p.log:SetFading(false); p.log:SetMaxLines(500)
    p.log:SetInsertMode("BOTTOM")
    p.log:EnableMouseWheel(true)
    p.log:SetScript("OnMouseWheel", function(self, delta) if delta > 0 then self:ScrollUp() else self:ScrollDown() end end)
    p.input = editBox(chat, MID_W - 28, "Message the channel, Enter to send", function(self, text)
        text = strtrim(text or "")
        if text ~= "" and selected.community and selected.channel then
            local community, channel = selected.community, selected.channel
            request({ op = "cSend", id = community, channelId = channel, body = text }, function(result)
                if result.error then UIErrorsFrame:AddMessage(tostring(result.message or result.error), 1, 0.1, 0.1) end
                fetchHistory(community, channel)
            end)
            self:SetText("")
        else
            self:ClearFocus()
        end
    end, 2000)
    p.input:SetPoint("BOTTOMLEFT", 14, 14)
    p.empty = ES.Text(chat, 12, ES.MUTED); p.empty:SetPoint("CENTER"); p.empty:SetWidth(MID_W - 60)
    p.accept = button(chat, 120, "Accept", true); p.accept:SetPoint("CENTER", -66, -60)
    p.decline = button(chat, 120, "Decline"); p.decline:SetPoint("CENTER", 66, -60)

    local side = panel(p); side:SetPoint("TOPLEFT", chat, "TOPRIGHT", 14, 0); side:SetPoint("BOTTOMRIGHT", -14, 14)
    p.side = side
    heading(side, "About"):SetPoint("TOPLEFT", 14, -12)
    p.about = ES.Text(side, 11, ES.TEXT); p.about:SetPoint("TOPLEFT", 14, -28); p.about:SetWidth(DETAIL_W - 28)
    p.about:SetJustifyH("LEFT")
    p.membersHead = heading(side, "Members"); p.membersHead:SetPoint("TOPLEFT", 14, -92)
    p.members = vlist(side, DETAIL_W - 16, 28, 12, function(parent, w, h)
        local r = CreateFrame("Frame", nil, parent); r:SetSize(w, h)
        r.dot = ES.Solid(r, "ARTWORK", ES.GOOD, 1); r.dot:SetSize(7, 7); r.dot:SetPoint("LEFT", 8, 0)
        r.name = ES.Text(r, 11, ES.TEXT); r.name:SetPoint("LEFT", 22, 0); r.name:SetPoint("RIGHT", -80, 0); r.name:SetJustifyH("LEFT")
        r.role = ES.Text(r, 9, ES.GOLD_DIM); r.role:SetPoint("RIGHT", -6, 0)
        return r
    end, function(r, m)
        local presence = "offline"
        for _, f in ipairs(social.friends) do if f.tag == m.tag then presence = f.presence or "offline" end end
        if social.me and social.me.tag == m.tag then presence = "online" end
        r.dot:SetVertexColor(unpack(presenceColor(presence)))
        r.name:SetText(m.displayName or m.tag)
        r.role:SetText(m.role and m.role ~= "member" and m.role:upper() or "")
    end)
    p.members:SetPoint("TOPLEFT", 8, -108)
    p.codeOut = editBox(side, DETAIL_W - 24, "Invite code appears here", nil, 64); p.codeOut:SetPoint("BOTTOMLEFT", 12, 78)
    p.share = button(side, math.floor((DETAIL_W - 30) / 2), "Invite code", true); p.share:SetPoint("BOTTOMLEFT", 12, 46)
    p.share:SetScript("OnClick", function()
        local id = selected.community
        if id then request({ op = "cInviteCreate", id = id }, function(result)
            if result.code then p.codeOut:SetText(result.code); p.codeOut:HighlightText(); p.codeOut:SetFocus() end
        end) end
    end)
    p.leave = confirmButton(side, math.floor((DETAIL_W - 30) / 2), "Leave", function()
        local id = selected.community
        if id then request({ op = "cLeave", id = id }, function() selected.community = nil; refreshCommunities() end) end
    end)
    p.leave:SetPoint("BOTTOMLEFT", 18 + math.floor((DETAIL_W - 30) / 2), 46)
end

local function paintCommunities(p)
    local entries = {}
    for _, c in ipairs(social.communities) do entries[#entries + 1] = { community = c } end
    for _, i in ipairs(social.invites) do entries[#entries + 1] = { invite = i } end
    if not selected.community and not selected.invite and social.communities[1] then selected.community = social.communities[1].id end
    for i, row in ipairs(p.rows) do
        local e = entries[i]
        row.community, row.invite = e and e.community and e.community.id, e and e.invite
        if e and e.community then
            local unread = 0
            for _, ch in ipairs(e.community.channels or {}) do unread = unread + (ch.unread or 0) end
            row:Paint(e.community.name, selected.community == e.community.id,
                unread > 0 and unread or (e.community.memberCount or ""), unread > 0 and ES.GOLD or nil,
                unread > 0 and ES.GOLD or nil)
            row:Show()
        elseif e and e.invite then
            row:Paint("Invite: " .. (e.invite.name or "?"), selected.invite == e.invite, "new", ES.GOLD, ES.GOLD)
            row:Show()
        else
            row:Hide()
        end
    end

    local c = selected.community and communityById(selected.community)
    local hasChat = c ~= nil
    for _, b in ipairs(p.channels) do b:Hide() end
    shown(p.log, hasChat); shown(p.input, hasChat); shown(p.topic, hasChat); shown(p.side, hasChat)
    shown(p.accept, selected.invite ~= nil); shown(p.decline, selected.invite ~= nil)
    if selected.invite then
        local inv = selected.invite
        p.title:SetText(inv.name or "Community invite")
        p.empty:SetText(("%s invited you to join %s."):format(inv.from or "Someone", inv.name or "a community")); p.empty:Show()
        p.accept:SetScript("OnClick", function()
            request({ op = "cInviteRespond", id = inv.communityId, accept = true }, function() selected.invite = nil; refreshCommunities() end)
        end)
        p.decline:SetScript("OnClick", function()
            request({ op = "cInviteRespond", id = inv.communityId, accept = false }, function() selected.invite = nil; refreshCommunities() end)
        end)
        return
    end
    if not hasChat then
        p.title:SetText("Communities")
        p.empty:SetText(not service() and "Communities need the EsteriaChat add-on." or not social.connected and
            "The Esteria social service isn't connected right now." or
            "You're not in any community yet. Join one with an invite code, or create your own.")
        p.empty:Show()
        return
    end
    p.empty:Hide()
    p.title:SetText(c.name)
    local channels = c.channels or {}
    if not selected.channel and channels[1] then selected.channel = channels[1].id; fetchHistory(c.id, channels[1].id) end
    local topic
    for i, ch in ipairs(channels) do
        local b = p.channels[i]
        if b then
            b.channel = ch.id
            b.label:SetText((ch.unread or 0) > 0 and (ch.name .. " (" .. ch.unread .. ")") or ch.name)
            b:SetSelected(ch.id == selected.channel); b:Show()
        end
        if ch.id == selected.channel then topic = ch.topic end
    end
    p.topic:SetText(topic or "")
    local store = social.messages[messageKey(c.id, selected.channel or 0)]
    local signature = c.id .. ":" .. tostring(selected.channel) .. ":" .. (store and #store.list or 0)
    if signature ~= p.drawn then
        p.drawn = signature
        p.log:Clear()
        for _, m in ipairs(store and store.list or {}) do
            local me = social.me and social.me.tag == m.from
            p.log:AddMessage(("|cff%s%s|r  %s"):format(me and "8cd97f" or "e8c46b", m.displayName or m.from or "?", m.body or ""),
                0.88, 0.86, 0.80)
        end
        if not store or #store.list == 0 then p.log:AddMessage("No messages yet. Say hello!", 0.6, 0.59, 0.55) end
    end
    p.about:SetText((c.description and c.description ~= "" and c.description or "No description.") ..
        ("\n|cff9a9790%d members  ·  you are %s|r"):format(c.memberCount or 0, c.role or "a member"))
    if not social.members[c.id] then social.members[c.id] = {}; loadMembers(c.id) end
    p.members.data = social.members[c.id]
    p.members:Refresh()
end

-- Window ------------------------------------------------------------------------------------------------------------
local PANES = {
    { "Friends", buildFriends, paintFriends },
    { "Who", buildWho, paintWho },
    { "Guild", buildGuild, paintGuild },
    { "Communities", buildCommunities, paintCommunities },
}

local header
refresh = function()
    if not frame or not frame:IsShown() then return end
    local online = friendCounts()
    header:SetText(("|cffe8c46b%d|r friends online%s"):format(online,
        IsInGuild() and ("     |cffe8c46b%s|r"):format(GetGuildInfo("player") or "") or ""))
    for _, spec in ipairs(PANES) do if spec[1] == current then spec[3](panes[current]) end end
end

local function selectTab(name)
    current = name
    for _, spec in ipairs(PANES) do
        tabs[spec[1]]:SetSelected(spec[1] == name)
        shown(panes[spec[1]], spec[1] == name)
    end
    if name == "Friends" then ShowFriends(); refreshAccount()
    elseif name == "Guild" and IsInGuild() then
        if SetGuildRosterShowOffline then SetGuildRosterShowOffline(true) end -- scan everyone; the toggle filters here
        GuildRoster()
    elseif name == "Communities" then refreshAccount(); refreshCommunities() end
    refresh()
end

local function build()
    EsteriaSocialDB = EsteriaSocialDB or {}
    db = EsteriaSocialDB
    frame = CreateFrame("Frame", "EsteriaSocialFrame", UIParent)
    frame:SetSize(W, H); frame:SetPoint("CENTER")
    frame:SetFrameStrata("HIGH"); frame:SetToplevel(true); frame:EnableMouse(true); frame:SetMovable(true)
    frame:SetClampedToScreen(true); frame:RegisterForDrag("LeftButton")
    frame:SetScript("OnDragStart", frame.StartMoving); frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
    frame:Hide(); tinsert(UISpecialFrames, "EsteriaSocialFrame")
    ES.Solid(frame, "BACKGROUND", ES.INK, 0.98):SetAllPoints(); ES.Border(frame, ES.GOLD)
    gradient(frame, 42, 0.28)
    local divider = ES.Solid(frame, "ARTWORK", ES.GOLD_DIM, 0.6)
    divider:SetPoint("TOPLEFT", 1, -41); divider:SetPoint("TOPRIGHT", -1, -41); divider:SetHeight(1)
    local title = ES.Text(frame, 18, ES.GOLD); title:SetPoint("TOPLEFT", 20, -12); title:SetText("Social")
    header = ES.Text(frame, 11, ES.TEXT); header:SetPoint("TOPRIGHT", -40, -15)
    local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton"); close:SetPoint("TOPRIGHT", 2, 2)
    for i, spec in ipairs(PANES) do
        local b = tabButton(frame, 110, spec[1]); b:SetPoint("TOPLEFT", 110 + (i - 1) * 112, -6)
        b:SetScript("OnClick", function() PlaySound("igCharacterInfoTab"); selectTab(spec[1]) end)
        tabs[spec[1]] = b
        local pane = CreateFrame("Frame", nil, frame); pane:SetPoint("TOPLEFT", 0, -42); pane:SetPoint("BOTTOMRIGHT")
        spec[2](pane); pane:Hide()
        panes[spec[1]] = pane
    end
    local clock = 0
    frame:SetScript("OnUpdate", function(_, elapsed)
        clock = clock + elapsed
        if clock >= 30 then clock = 0; refreshAccount(); if current == "Communities" then refreshCommunities() end end
    end)
    frame:SetScript("OnShow", function() PlaySound("igCharacterInfoOpen"); selectTab(current) end)
    frame:SetScript("OnHide", function() PlaySound("igCharacterInfoClose"); GameTooltip:Hide() end)
    for _, ev in ipairs({ "FRIENDLIST_UPDATE", "IGNORELIST_UPDATE", "WHO_LIST_UPDATE", "GUILD_ROSTER_UPDATE",
                          "PLAYER_GUILD_UPDATE", "GUILD_MOTD" }) do
        frame:RegisterEvent(ev)
    end
    frame:SetScript("OnEvent", function() refresh() end)
    if EsteriaChat and EsteriaChat.listen then
        EsteriaChat.listen(function(event)
            local data = event.data or {}
            if event.type == "community_message" and data.communityId and data.channelId then
                if frame:IsShown() and selected.community == data.communityId then fetchHistory(data.communityId, data.channelId) end
                if frame:IsShown() then refreshCommunities() end
            elseif event.type == "community_invite" or event.type == "community_removed" then
                if frame:IsShown() then refreshCommunities() end
            elseif frame:IsShown() then
                refreshAccount()
            end
        end)
    end
end

function EsteriaSocial_Open(tab)
    if not frame then build() end
    if tab then current = tab end
    if frame:IsShown() then selectTab(current) else frame:Show() end
end

function EsteriaSocial_Toggle(tab)
    if not frame then build() end
    if frame:IsShown() and (not tab or tab == current) then frame:Hide() else EsteriaSocial_Open(tab) end
end

-- Entry points: O / Socials micro button (ToggleFriendsFrame), J and guild links (ToggleGuildFrame), /who results and
-- anything else that shows the stock or the retail windows. The retail raid tab is left as it is.
local STOCK_TAB = { [1] = "Friends", [2] = "Who", [3] = "Guild" }
local previousToggle = ToggleFriendsFrame
ToggleFriendsFrame = function(tab)
    if tab == 5 then return previousToggle and previousToggle(tab) end -- raid
    if tab == 4 then return end -- chat channels live in the chat frame
    EsteriaSocial_Toggle(tab and STOCK_TAB[tab] or nil)
end
ToggleGuildFrame = function() EsteriaSocial_Toggle("Guild") end

local function redirect(frameToHide, tab)
    HideUIPanel(frameToHide)
    EsteriaSocial_Open(tab)
end
if FriendsFrame then
    FriendsFrame:HookScript("OnShow", function(self)
        local tab = PanelTemplates_GetSelectedTab and PanelTemplates_GetSelectedTab(self) or 1
        if tab == 5 then return end
        redirect(self, STOCK_TAB[tab] or "Friends")
    end)
end
if RetailFriendsFrame then
    RetailFriendsFrame:HookScript("OnShow", function(self)
        local tab = RetailPanelTemplates_GetSelectedTab and RetailPanelTemplates_GetSelectedTab(self) or self.selectedTab or 1
        if tab == 3 then return end -- retail raid tab
        redirect(self, tab == 2 and "Who" or "Friends")
    end)
end
if RetailCommunitiesFrame then
    RetailCommunitiesFrame:HookScript("OnShow", function(self) redirect(self, IsInGuild() and "Guild" or "Communities") end)
end

SLASH_ESTERIASOCIAL1 = "/social"
SlashCmdList.ESTERIASOCIAL = function(msg)
    local tab = ({ friends = "Friends", who = "Who", guild = "Guild", communities = "Communities" })[(msg or ""):lower()]
    EsteriaSocial_Toggle(tab)
end
