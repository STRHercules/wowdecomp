-- Animated top-of-screen banners for crime events and the Wilderness zone. One OnUpdate per banner drives every
-- layer from the elapsed time, so a banner can be replayed or held indefinitely without leftover tweens.
local ES = EsteriaStyle
EsteriaBanner = {}

local STAR = "Interface\\Cooldown\\star4"
local BURST = "Interface\\Cooldown\\starburst"

local function ease(t) return 1 - (1 - t) ^ 3 end
local function seg(t, a, b) if t <= a then return 0 elseif t >= b then return 1 end return (t - a) / (b - a) end

local function spaced(text)
    return (text:upper():gsub("(.)", "%1 "):gsub(" $", ""))
end

-- A horizontal strip that fades out toward both ends.
local function strip(parent, layer, height)
    local s = { }
    for i, dir in ipairs({ -1, 1 }) do
        local tex = parent:CreateTexture(nil, layer)
        tex:SetTexture(ES.WHITE)
        tex:SetHeight(height)
        tex:SetPoint(dir < 0 and "RIGHT" or "LEFT", parent, "CENTER", 0, 0)
        s[i] = tex
    end
    s.Set = function(self, width, y, r, g, b, a)
        for i, tex in ipairs(self) do
            tex:SetWidth(math.max(width / 2, 0.01))
            tex:SetPoint(i == 1 and "RIGHT" or "LEFT", parent, "CENTER", 0, y)
            if i == 1 then tex:SetGradientAlpha("HORIZONTAL", r, g, b, 0, r, g, b, a)
            else tex:SetGradientAlpha("HORIZONTAL", r, g, b, a, r, g, b, 0) end
        end
    end
    return s
end

local function diamond(parent, size, layer)
    local d = parent:CreateTexture(nil, layer or "OVERLAY")
    d:SetTexture(ES.WHITE); d:SetSize(size, size); d:SetRotation(math.pi / 4)
    return d
end

-- An icon set inside a rotated gold diamond plate.
local function emblem(parent, size)
    local e = CreateFrame("Frame", nil, parent)
    e:SetSize(size, size)
    e.glow = e:CreateTexture(nil, "BACKGROUND"); e.glow:SetTexture(STAR); e.glow:SetBlendMode("ADD")
    e.glow:SetPoint("CENTER"); e.glow:SetSize(size * 2.4, size * 2.4)
    e.plate = diamond(e, size * 0.98, "BORDER")
    e.plate:SetPoint("CENTER")
    e.inner = diamond(e, size * 0.86, "ARTWORK"); e.inner:SetPoint("CENTER"); e.inner:SetVertexColor(0.02, 0.02, 0.03, 1)
    e.icon = e:CreateTexture(nil, "OVERLAY"); e.icon:SetSize(size * 0.56, size * 0.56); e.icon:SetPoint("CENTER")
    e.icon:SetTexCoord(0.08, 0.92, 0.08, 0.92)
    return e
end

local function build(width, height, titleSize)
    local f = CreateFrame("Frame", nil, UIParent)
    f:SetSize(width, height); f:SetFrameStrata("HIGH"); f:Hide()
    f.titleSize = titleSize
    f.band = strip(f, "BACKGROUND", height * 0.62)
    f.bandEdge = strip(f, "BACKGROUND", height * 0.62 + 10)
    f.lineTop = strip(f, "BORDER", 2)
    f.lineBottom = strip(f, "BORDER", 2)
    f.burst = f:CreateTexture(nil, "ARTWORK"); f.burst:SetTexture(BURST); f.burst:SetBlendMode("ADD")
    f.burst:SetPoint("CENTER", 0, 4); f.burst:SetSize(height * 2.2, height * 2.2)
    f.title = f:CreateFontString(nil, "OVERLAY")
    f.title:SetFont(ES.FONT, titleSize, "OUTLINE"); f.title:SetPoint("CENTER", 0, 6)
    f.title:SetShadowOffset(2, -2); f.title:SetShadowColor(0, 0, 0, 1)
    f.sub = ES.Text(f, math.max(11, math.floor(titleSize * 0.42)), ES.TEXT)
    f.sub:SetPoint("TOP", f.title, "BOTTOM", 0, -6)
    f.left, f.right = emblem(f, height * 0.6), emblem(f, height * 0.6)
    f.tips = { diamond(f, 7), diamond(f, 7), diamond(f, 7), diamond(f, 7) }
    f.sparks = {}
    for i = 1, 8 do
        local s = f:CreateTexture(nil, "OVERLAY"); s:SetTexture(STAR); s:SetBlendMode("ADD")
        s.phase = i * 0.37; s.x = (i % 2 == 0 and 1 or -1) * (40 + (i * 53) % (width / 2 - 40))
        s.y = ((i * 29) % 34) - 17
        f.sparks[i] = s
    end
    return f
end

local function render(f, t)
    local spec = f.spec
    local r, g, b = unpack(spec.color)
    local outro
    if t < 0 then return end
    local hold = spec.hold
    local outStart = hold and math.huge or (spec.duration - 0.8)
    if f.leaving then outStart = f.leaving end
    outro = 1 - ease(seg(t, outStart, outStart + 0.8))
    if outro <= 0 then f:Hide(); f.spec = nil; if f.onDone then f.onDone() end; return end
    local w = f:GetWidth()
    local open = ease(seg(t, 0, 0.45))
    local pulse = 0.5 + 0.5 * math.sin(t * 2.4)
    local a = outro
    local rise = (1 - outro) * 10
    f:SetPoint("TOP", UIParent, "TOP", 0, f.baseY + rise)

    f.band:Set(w * (0.35 + 0.65 * open), 0, 0, 0, 0, 0.78 * a)
    f.bandEdge:Set(w * (0.3 + 0.7 * open), 0, r * 0.35, g * 0.35, b * 0.35, 0.35 * a)
    local lineW = (w - 40) * open
    local lineY = f:GetHeight() * 0.31
    f.lineTop:Set(lineW, lineY, r, g, b, 0.95 * a)
    f.lineBottom:Set(lineW * 0.82, -lineY, r, g, b, 0.75 * a)
    for i, tip in ipairs(f.tips) do
        local side = i % 2 == 0 and 1 or -1
        local y = i <= 2 and lineY or -lineY
        local len = i <= 2 and lineW or lineW * 0.82
        tip:SetPoint("CENTER", f, "CENTER", side * len / 2, y)
        tip:SetVertexColor(r, g, b, a * open)
    end

    -- The title slams in from oversize; the flare peaks with it and settles into a slow pulse.
    local slam = ease(seg(t, 0.05, 0.4))
    f.title:SetTextHeight(f.titleSize * (1.7 - 0.7 * slam))
    f.title:SetAlpha(slam * a)
    f.title:SetTextColor(1, 0.97 - 0.25 * (1 - g), 0.9 - 0.4 * (1 - b))
    local flash = seg(t, 0.05, 0.25) * (1 - seg(t, 0.25, 0.9))
    f.burst:SetVertexColor(r, g, b, (0.25 + 0.75 * flash + 0.12 * pulse) * a * open)
    f.burst:SetRotation(t * 0.35)
    f.sub:SetAlpha(ease(seg(t, 0.5, 1.0)) * a)

    local titleHalf = math.min(f.title:GetStringWidth(), w - 200) / 2
    local slide = ease(seg(t, 0.2, 0.7))
    for i, e in ipairs({ f.left, f.right }) do
        local side = i == 1 and -1 or 1
        e:SetPoint("CENTER", f, "CENTER", side * (titleHalf + 40 + 70 * (1 - slide)), 4)
        e:SetAlpha(slide * a)
        e.plate:SetVertexColor(r, g, b, 1)
        e.glow:SetVertexColor(r, g, b, 0.35 + 0.35 * pulse)
    end
    for _, s in ipairs(f.sparks) do
        local tw = math.max(0, math.sin(t * 3 + s.phase * 6))
        s:SetPoint("CENTER", f, "CENTER", s.x * open, s.y)
        local size = 6 + 14 * tw
        s:SetSize(size, size)
        s:SetVertexColor(r, g, b, tw * a * open)
    end
end

local function play(f, spec, y)
    f.spec, f.leaving, f.t, f.baseY = spec, nil, -(spec.delay or 0), y
    f.title:SetText(spaced(spec.title))
    f.sub:SetText(spec.subtitle or "")
    f.left.icon:SetTexture(spec.icon); f.right.icon:SetTexture(spec.icon)
    f:ClearAllPoints(); f:SetPoint("TOP", UIParent, "TOP", 0, y)
    f:SetScript("OnUpdate", function(self, elapsed) self.t = self.t + elapsed; render(self, self.t) end)
    render(f, 0)
    f:Show()
end

-- Crime events queue so two triggers in one moment play one after the other.
local event = build(620, 96, 34)
local queue = {}
local function nextEvent()
    local spec = table.remove(queue, 1)
    if spec then
        if spec.sound then PlaySound(spec.sound) end
        play(event, spec, -118)
    end
end
event.onDone = nextEvent

function EsteriaBanner.Play(spec)
    spec.duration = spec.duration or 4.6
    queue[#queue + 1] = spec
    if not event.spec then nextEvent() end
end

-- A held banner that stays until Leave() is called; used for the Wilderness zone.
function EsteriaBanner.Hold(width, height, titleSize, y)
    local f = build(width, height, titleSize)
    return {
        frame = f,
        Enter = function(_, spec)
            if f.spec and not f.leaving then return end
            spec.hold = true
            play(f, spec, y)
        end,
        Leave = function()
            if f.spec and not f.leaving then f.leaving = math.max(f.t, 0.6) end
        end,
        Clear = function() f:Hide(); f.spec = nil end,
        Active = function() return f.spec ~= nil and not f.leaving end,
    }
end

-- Crime triggers: the server keeps a debuff on the player for each state, and whispers the bounty total.
local WANTED, THIEF = 955080, 955083
local THEMES = {
    wanted = { title = "Wanted", color = { 0.95, 0.2, 0.16 }, icon = "Interface\\Icons\\INV_Misc_Bone_Skull_01",
               subtitle = "You committed murder. Killing you is now lawful.", sound = "RaidWarning" },
    thief = { title = "Thief", color = { 0.68, 0.42, 1 }, icon = "Interface\\Icons\\INV_Misc_Bag_11",
              subtitle = "You were seen stealing. Your PvP flag is locked on.", sound = "RaidWarning" },
    bounty = { title = "Bounty", color = { 1, 0.78, 0.25 }, icon = "Interface\\Icons\\INV_Misc_Coin_02",
               sound = "RaidWarning" },
}

local function theme(name, extra)
    local spec = {}
    for k, v in pairs(THEMES[name]) do spec[k] = v end
    for k, v in pairs(extra or {}) do spec[k] = v end
    return spec
end

local function auras()
    local found = {}
    for i = 1, 40 do
        local name, _, _, _, _, _, _, _, _, _, spell = UnitDebuff("player", i)
        if not name then break end
        found[spell] = true
    end
    return found
end

local function copper(text)
    local g, s, c = text:match("^(%d+)g (%d+)s (%d+)c$")
    return g and (tonumber(g) * 10000 + tonumber(s) * 100 + tonumber(c))
end

local state, bounty, quietUntil = {}, 0, 0
local watcher = CreateFrame("Frame")
watcher:RegisterEvent("PLAYER_ENTERING_WORLD")
watcher:RegisterEvent("UNIT_AURA")
watcher:RegisterEvent("CHAT_MSG_ADDON")
watcher:SetScript("OnEvent", function(_, ev, a1, a2, a3, a4)
    if ev == "PLAYER_ENTERING_WORLD" then
        -- States already present after a login or loading screen are not news.
        state, bounty, quietUntil = auras(), 0, GetTime() + 3
    elseif ev == "UNIT_AURA" and a1 == "player" then
        local now = auras()
        if GetTime() > quietUntil then
            if now[WANTED] and not state[WANTED] then EsteriaBanner.Play(theme("wanted")) end
            if now[THIEF] and not state[THIEF] then EsteriaBanner.Play(theme("thief")) end
        end
        if not now[955081] then bounty = 0 end
        state = now
    elseif ev == "CHAT_MSG_ADDON" and a1 == "EsteriaBounty" and a3 == "WHISPER" and a4 == UnitName("player") then
        local value = copper(a2)
        if not value then return end
        if value > bounty and GetTime() > quietUntil then
            local text = GetCoinTextureString(value)
            EsteriaBanner.Play(theme("bounty", bounty == 0 and
                { title = "Bounty Placed", subtitle = "A price of " .. text .. " is on your head." } or
                { title = "Bounty Raised", subtitle = "The price on your head is now " .. text .. "." }))
        end
        bounty = value
    end
end)
