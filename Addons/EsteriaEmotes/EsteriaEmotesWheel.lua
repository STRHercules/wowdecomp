-- The emote wheels: hold the bound key, point at an entry and release; release on the hub to cancel. Pages of eight
-- (mouse wheel flips). The dance wheel (four pages, filled from the Dances window) dances; the spell emote wheel
-- (EsteriaSpellEmotes.lua) casts. Art and aiming are the character swap wheel's (Addons/EsteriaSwap, EasyController
-- art, MIT): always the 8-section background, empty sections veiled.
local EE = EsteriaEmotes
local TEX = "Interface\\AddOns\\EsteriaSwap\\Textures\\"
local SIZE = 440
local S = SIZE / 512
local N = 8
local ICON_RADIUS, SLOT, HUB = 144 * S, 64 * S, 124 * S
local INNER, OUTER = 58 * S, 250 * S
local LABEL_GAP = 52 * S
local OVERLAY = SIZE * math.sqrt(2)   -- this client's SetRotation shows a texture at 1/sqrt(2) of its size

-- source: Get(absolute slot) -> { name, icon } or nil; Pages() -> page count; Commit(entry) on a committed release
-- (optional); empty and prompt banner texts. Returns { frame, Open(), Close(commit) -> committed entry, Layout() }.
function EE.NewWheel(frameName, source)
    local wheel = CreateFrame("Frame", frameName, UIParent)
    wheel:SetSize(SIZE, SIZE)
    wheel:SetPoint("CENTER")
    wheel:SetFrameStrata("DIALOG")
    wheel:EnableMouse(true)
    wheel:EnableMouseWheel(true)
    wheel:Hide()
    tinsert(UISpecialFrames, frameName)

    local background = wheel:CreateTexture(nil, "BACKGROUND")
    background:SetAllPoints()
    background:SetTexture(TEX .. "wheel_bg_" .. N)

    local function Angle(i) return (i - 1) * 2 * math.pi / N end
    local function Overlay(sublevel)
        local texture = wheel:CreateTexture(nil, "ARTWORK", nil, sublevel)
        texture:SetSize(OVERLAY, OVERLAY)
        texture:SetPoint("CENTER")
        texture:Hide()
        return texture
    end
    local function Place(texture, kind, i)
        texture:SetTexture(TEX .. "wheel_" .. kind .. "_" .. N)
        texture:SetRotation(-Angle(i))
        texture:Show()
    end

    local veils, slots = {}, {}
    for i = 1, N do veils[i] = Overlay(1) end
    local highlight = Overlay(2)

    for i = 1, N do
        local slot = CreateFrame("Frame", nil, wheel)
        slot:SetSize(SLOT, SLOT)
        local dx, dy = math.sin(Angle(i)), math.cos(Angle(i))
        slot:SetPoint("CENTER", wheel, "CENTER", ICON_RADIUS * dx, ICON_RADIUS * dy)
        slot.disc = slot:CreateTexture(nil, "BACKGROUND")
        slot.disc:SetAllPoints()
        slot.disc:SetTexture(TEX .. "ck_slot")
        slot.icon = slot:CreateTexture(nil, "ARTWORK")
        slot.icon:SetPoint("TOPLEFT", SLOT * 0.18, -SLOT * 0.18)
        slot.icon:SetPoint("BOTTOMRIGHT", -SLOT * 0.18, SLOT * 0.18)
        slot.glow = slot:CreateTexture(nil, "OVERLAY")
        slot.glow:SetPoint("CENTER")
        slot.glow:SetSize(SLOT * 1.2, SLOT * 1.2)
        slot.glow:SetTexture(TEX .. "ck_slot_glow")
        slot.glow:Hide()
        -- The name on the outer side of the slot, as the swap wheel places it.
        slot.label = slot:CreateFontString(nil, "OVERLAY", "GameFontNormal")
        slot.label:SetWidth(110 * S)
        local ox, oy
        if math.abs(dx) > 0.8 then ox, oy = (dx > 0 and 1 or -1) * (LABEL_GAP + 14 * S), 0
        elseif math.abs(dx) < 0.3 then ox, oy = 0, (dy > 0 and 1 or -1) * LABEL_GAP
        else ox, oy = (dx > 0 and 1 or -1) * 30 * S, (dy > 0 and 1 or -1) * 45 * S end
        slot.label:SetPoint("CENTER", slot, "CENTER", ox, oy)
        slots[i] = slot
    end

    local hub = CreateFrame("Frame", nil, wheel)
    hub:SetSize(HUB, HUB)
    hub:SetPoint("CENTER")
    hub:SetFrameLevel(wheel:GetFrameLevel() + 10)
    local hubArt = hub:CreateTexture(nil, "ARTWORK")
    hubArt:SetAllPoints()
    hubArt:SetTexture(TEX .. "ck_hub")
    local hubText = hub:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    hubText:SetPoint("CENTER", 0, 4)
    local hubHint = hub:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
    hubHint:SetPoint("TOP", hubText, "BOTTOM", 0, -2)

    local banner = wheel:CreateTexture(nil, "BACKGROUND")
    banner:SetPoint("TOP", wheel, "BOTTOM", 0, 4)
    banner:SetSize(340, 44)
    banner:SetTexture(0, 0, 0, 0.55)
    local bannerText = wheel:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    bannerText:SetPoint("CENTER", banner, "CENTER", 0, 0)

    local page, aimed = 1, nil

    local function Entry(i) return source.Get((page - 1) * N + i) end

    local function Describe()
        local entry = aimed and Entry(aimed)
        hubText:SetText(entry and "" or (page .. "/" .. source.Pages()))
        hubHint:SetText(entry and "" or "page")
        local empty = true
        for i = 1, N do if Entry(i) then empty = false end end
        if entry then bannerText:SetText(entry.name)
        elseif empty then bannerText:SetText(source.empty)
        else bannerText:SetText(source.prompt) end
    end

    local function Aim(i)
        if i == aimed then return end
        aimed = i
        for k, slot in ipairs(slots) do if k == i then slot.glow:Show() else slot.glow:Hide() end end
        if i then Place(highlight, "sel", i) else highlight:Hide() end
        Describe()
    end

    local function Layout()
        page = math.min(page, source.Pages())
        for i, slot in ipairs(slots) do
            local entry = Entry(i)
            slot.icon:SetTexture(entry and entry.icon or nil)
            slot.label:SetText(entry and entry.name or "")
            if entry then veils[i]:Hide() else Place(veils[i], "off", i) end
        end
        aimed = false     -- not nil: Aim redraws even when nothing is aimed
        Aim(nil)
    end

    -- The section under the cursor (between the hub and just past the rim), if it holds an entry.
    local function Aimed()
        local x, y = GetCursorPosition()
        local scale = wheel:GetEffectiveScale()
        local cx, cy = wheel:GetCenter()
        local dx, dy = x / scale - cx, y / scale - cy
        local distance = math.sqrt(dx * dx + dy * dy)
        if distance < INNER or distance > OUTER then return nil end
        local fromNorth = (90 - math.deg(math.atan2(dy, dx))) % 360
        local i = math.floor((fromNorth + 180 / N) / (360 / N)) % N + 1
        return Entry(i) and i or nil
    end

    local w = { frame = wheel, Layout = Layout }
    function w.Close(commit)
        local entry = commit and aimed and Entry(aimed)
        aimed = nil
        wheel:Hide()
        if entry and source.Commit then source.Commit(entry) end
        return entry or nil
    end
    function w.Open()
        wheel:Show()
        Layout()
    end

    wheel:SetScript("OnUpdate", function() Aim(Aimed()) end)
    wheel:SetScript("OnMouseUp", function(_, button) w.Close(button == "LeftButton") end)
    wheel:SetScript("OnMouseWheel", function(_, delta) page = (page - 1 - delta) % source.Pages() + 1; Layout() end)
    return w
end

local danceWheel = EE.NewWheel("EsteriaEmotesWheelFrame", {
    Get = function(k) return EE.byId[EE.wheel[k]] end,
    Pages = function() return 4 end,
    Commit = function(emote) EE.Play(emote.id) end,
    empty = "This page is empty. Fill it in /emotes.",
    prompt = "Point at a dance and release. Mouse wheel: next page.",
})
EE.listeners[#EE.listeners + 1] = function() if danceWheel.frame:IsShown() then danceWheel.Layout() end end

function EsteriaEmotes_Wheel(keystate)
    if keystate == "down" then danceWheel.Open() else danceWheel.Close(true) end
end

SLASH_ESTERIAEMOTEWHEEL1 = "/emotewheel"
SlashCmdList.ESTERIAEMOTEWHEEL = function()
    if danceWheel.frame:IsShown() then danceWheel.Close(false) else danceWheel.Open() end
end
