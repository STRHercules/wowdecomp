-- Manastorm_Transmog: the client half of mod-transmog's transmog window.
--
-- A 3D preview of your character with the armor slots laid over it, and the appearances your account owns
-- for the selected slot, each shown on a model card. Everything is previewed locally; "Apply" sends one
-- request per changed slot and the server charges for it exactly as the gossip menu would.
--
-- Every piece of art is drawn at the size it has in uitransmogrify2x.blp, one texel to one screen pixel:
-- nothing is scaled. P() turns a pixel count into UI units for this screen.
--
-- Protocol (addon whispers to yourself, see mod-transmog/src/transmog_client.cpp):
--   client -> server  TMOGC<TAB>HI | ST | L:slot:page:search | A:slot:entry | XA | OL
--                                  | OS:id:slot=entry,...:name | OU:id | OD:id | OR:id:name
--   server -> client  TMOGS<TAB>OPEN | I:... | E:slot,equip,fake,cost;... | L:slot:page:total:ids
--                                  | R:slot:code | OB:max | O:id:slot=entry,...:name

local PFX_OUT, PFX_IN = "TMOGC", "TMOGS"
local SERVER_PAGE = 20 -- items per L: reply, ITEMS_PER_PAGE on the server
local HIDDEN = 1

------------------------------------------------------------------------------------------------------
-- art
------------------------------------------------------------------------------------------------------

local ATLAS = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\uitransmogrify2x"
local ATLAS_SIZE = 2048
local ATLAS_SET = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\uitransmogrifysetb2x"
local ROW = { -- rectangles of the set atlas (2048x1024) the outfit list is drawn from
    panel = { 1260, 340, 626, 206 }, hover = { 1558, 592, 418, 88 }, selected = { 0, 930, 418, 88 },
}
local RECT = { -- x, y, w, h in atlas pixels
    -- cards, all cut to the same 222x286 so they swap in place
    cardBronze  = { 1498, 298, 222, 286 }, cardHover = { 1268, 298, 222, 286 },
    cardPreview = { 1728, 4, 222, 286 },   cardApplied = { 1498, 4, 222, 286 },
    -- slot frames
    slotBronze  = { 625, 1923, 110, 110 }, slotDark = { 855, 1923, 110, 110 },
    slotPurple  = { 1621, 1129, 110, 110 }, slotSelected = { 1490, 1129, 120, 119 },
    slotPending = { 1360, 1131, 116, 117 },
    -- marks
    eye         = { 1770, 1332, 37, 29 }, check = { 1577, 1334, 40, 40 }, no = { 1640, 1332, 37, 35 },
    warn        = { 1704, 1322, 41, 48 },
    -- empty-slot icons
    icoBoots    = { 1568, 1262, 72, 68 }, icoGloves = { 1634, 1262, 72, 64 }, icoHelm = { 1698, 1262, 72, 68 },
    icoPants    = { 1760, 1262, 72, 64 }, icoSword = { 1826, 1262, 72, 64 }, icoShield = { 1890, 1262, 72, 64 },
    icoShirt    = { 1954, 1262, 66, 64 }, icoChest = { 1968, 1124, 70, 70 }, icoCloak = { 1346, 1370, 70, 70 },
    icoTabard   = { 1346, 1436, 64, 70 }, icoBelt = { 1346, 1496, 64, 70 }, icoBracer = { 1346, 1560, 64, 70 },
    stageFrame  = { 11, 1636, 322, 322 },
}

local LIST_ATLAS = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\listbuttons"
local LIST = { -- rectangles of listbuttons.blp (512x512)
    dark = { 0, 0, 395, 48 }, light = { 2, 50, 391, 44 }, selected = { 0, 144, 396, 47 },
}
local GLUE_DIR = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\glue-panel-button-"
local GLUE = { 5, 3, 138, 40 } -- the button art inside the 256x64 glue textures

local function SetCoords(tex, x, y, w, h)
    tex:SetTexCoord(x / ATLAS_SIZE, (x + w) / ATLAS_SIZE, y / ATLAS_SIZE, (y + h) / ATLAS_SIZE)
end

-- the set atlas is 2048x1024
local function SetSetCoords(tex, r)
    tex:SetTexCoord(r[1] / 2048, (r[1] + r[3]) / 2048, r[2] / 1024, (r[2] + r[4]) / 1024)
end

-- Server equipment slot index (0-based) -> window slot.
local SLOTS = {
    { slot = 0,  name = "Head",      inv = "HeadSlot",          empty = "icoHelm" },
    { slot = 2,  name = "Shoulders", inv = "ShoulderSlot" },
    { slot = 14, name = "Back",      inv = "BackSlot",          empty = "icoCloak" },
    { slot = 4,  name = "Chest",     inv = "ChestSlot",         empty = "icoChest" },
    { slot = 3,  name = "Shirt",     inv = "ShirtSlot",         empty = "icoShirt" },
    { slot = 18, name = "Tabard",    inv = "TabardSlot",        empty = "icoTabard" },
    { slot = 8,  name = "Wrists",    inv = "WristSlot",         empty = "icoBracer" },
    { slot = 9,  name = "Hands",     inv = "HandsSlot",         empty = "icoGloves" },
    { slot = 5,  name = "Waist",     inv = "WaistSlot",         empty = "icoBelt" },
    { slot = 6,  name = "Legs",      inv = "LegsSlot",          empty = "icoPants" },
    { slot = 7,  name = "Feet",      inv = "FeetSlot",          empty = "icoBoots" },
    { slot = 15, name = "Main Hand", inv = "MainHandSlot",      empty = "icoSword" },
    { slot = 16, name = "Off Hand",  inv = "SecondaryHandSlot", empty = "icoShield" },
    { slot = 17, name = "Ranged",    inv = "RangedSlot" },
}
local SLOT_BY_ID = {}
for _, def in ipairs(SLOTS) do SLOT_BY_ID[def.slot] = def end

-- How each card frames its slot: `h` is the height of the part on show in model units (the model stands
-- ~1.8 tall, see PIVOT), `zoom` how far in, `rot` the turn. First guesses; tune by eye.
-- Nudge of each slot's card, as a share of the preview frame: up (negative = down) and left. The frame spans
-- about FRAME_SPAN model units from top to bottom (measured by eye, not exact).
local FRAME_SPAN = 2.0
-- The order the pad's copy text is written in, and the order the tables come back out.
local SLOT_NUDGE_ORDER = { 0, 2, 14, 3, 4, 18, 8, 9, 5, 6, 7, 15, 16, 17 }
-- Seed values, tuned by eye with the pad to frame each slot's part on the plain human preview model. The pad
-- writes the live values and the copy button prints them. There are two tables because the preview is a man or
-- a woman to match the player and the same framing does not sit right on both; each was tuned separately.
local CARD_NUDGE_SEED_F = {
    [0]  = { up = 0.854, left = 0.011, zoom = 1.262 },
    [2]  = { up = 0.719, left = -0.055, zoom = 1.262 },
    [14] = { up = 0.442, left = 0.107, zoom = 1.594, rot = 0.698 },
    [3]  = { up = 0.100, left = 0.000 },
    [4]  = { up = 0.591, left = 0.000 },
    [18] = { up = 0.200, left = 0.000 },
    [8]  = { up = 0.591, left = -0.469, zoom = 1.791 },
    [9]  = { up = 0.660, left = -0.126, zoom = 1.124 },
    [5]  = { up = 0.409, left = 0.000, zoom = 1.419 },
    [6]  = { up = 0.206, left = 0.000, zoom = 1.594 },
    [7]  = { up = 0.060, left = 0.066, zoom = 1.262 },
    [15] = { up = 0.431, left = 0.089, zoom = 1.124 },
    [16] = { up = 0.100, left = 0.100 },
    [17] = { up = 0.611, left = 0.192, zoom = 1.419 },
    -- weapon kinds framed apart from their slot, key "slot:class.subclass" (see SHOT.TypeKey)
    ["15:1.1"]  = { up = 0.823, left = 0.065, zoom = 1.480, rot = -1.466 },
    ["15:1.13"] = { up = 1.197, left = -0.048, zoom = 2.376, rot = -1.222 },
    ["15:1.2"]  = { up = 0.394, left = 0.175, zoom = 1.124 },
    ["15:1.5"]  = { up = 0.922, left = 0.100, zoom = 1.791, rot = -1.396 },
    ["15:1.6"]  = { up = 0.431, left = 0.187, zoom = 1.124 },
    ["15:1.7"]  = { up = 0.180, left = 0.386, zoom = 1.124 },
    ["15:1.8"]  = { up = 0.835, left = -0.033, zoom = 1.350, rot = -1.518 },
    ["15:1.9"]  = { up = 0.504, left = 0.248, zoom = 1.124 },
}
local CARD_NUDGE_SEED_M = {
    [0]  = { up = 0.826, left = -0.103 },
    [2]  = { up = 0.700, left = -0.134, zoom = 0.792 },
    [14] = { up = 0.656, left = 0.008, zoom = 1.262, rot = 0.698 },
    [3]  = { up = 0.100, left = 0.000 },
    [4]  = { up = 0.816, left = -0.141, zoom = 0.890 },
    [18] = { up = 0.200, left = 0.000 },
    [8]  = { up = 0.736, left = -0.537, zoom = 1.124 },
    [9]  = { up = 0.850, left = -0.312, zoom = 0.890 },
    [5]  = { up = 0.671, left = -0.103, zoom = 1.262 },
    [6]  = { up = 0.583, left = -0.114, zoom = 1.262 },
    [7]  = { up = 0.392, left = -0.023, zoom = 1.000 },
    [15] = { up = 0.100, left = 0.100 },
    [16] = { up = 0.100, left = 0.100 },
    [17] = { up = 0.150, left = 0.150 },
    -- weapon kinds framed apart from their slot, key "slot:class.subclass" (see SHOT.TypeKey)
    ["15:1.1"]  = { up = 1.049, left = -0.140, zoom = 1.124, rot = -1.396 },
    ["15:1.10"] = { up = 0.774, left = -0.003 },
    ["15:1.13"] = { up = 0.134, left = 0.066 },
    ["15:1.2"]  = { up = 0.637, left = 0.100 },
    ["15:1.5"]  = { up = 1.106, left = -0.049, zoom = 1.124, rot = -1.396 },
    ["15:1.6"]  = { up = 0.706, left = 0.066 },
    ["15:1.7"]  = { up = 0.557, left = 0.226, zoom = 0.792 },
    ["15:1.8"]  = { up = 1.083, left = -0.460, rot = -0.873 },
    ["15:1.9"]  = { up = 0.809, left = 0.043 },
    ["16:2.6"]  = { up = 0.843, left = 0.043, zoom = 1.262 },
}

-- The live values the pad and the cards read; a copy of the seed of the sex the preview is showing.
local SET_NUDGE_SEED_F = { up = -0.294, left = 0.0, zoom = 2.261 }
local SET_NUDGE_SEED_M = { up = 0.049, left = -0.126, zoom = 1.689 }
local CARD_NUDGE = {}
local SET_NUDGE = {}
local nudgeFemale
-- A framing: up, left, zoom, and the turns about the three axes: rot = Z (the model turning round), pitch = Y, roll = X.
local function CopyNudge(n)
    n = n or {}
    return { up = n.up or 0, left = n.left or 0, zoom = n.zoom, rot = n.rot, pitch = n.pitch, roll = n.roll }
end
local function SeedNudges()
    nudgeFemale = UnitSex("player") == 3
    local seed = nudgeFemale and CARD_NUDGE_SEED_F or CARD_NUDGE_SEED_M
    CARD_NUDGE = {}
    for s, n in pairs(seed) do CARD_NUDGE[s] = CopyNudge(n) end
    SET_NUDGE = CopyNudge(nudgeFemale and SET_NUDGE_SEED_F or SET_NUDGE_SEED_M)
end
-- The preview follows the player's sex, so the seeds do too; only re-seeded when that is not the sex in use.
local function SyncNudges()
    if nudgeFemale == nil or nudgeFemale ~= (UnitSex("player") == 3) then SeedNudges() end
end
SyncNudges()

local CARD_VIEW = {
    [0]  = { h = 1.80, zoom = 4.4, rot = 0.35 },
    [2]  = { h = 1.62, zoom = 3.8, rot = 0.5, x = 0.8 },
    [3]  = { h = 1.40, zoom = 3.2, rot = 0.2 },
    [4]  = { h = 1.40, zoom = 3.2, rot = 0.2 },
    [5]  = { h = 1.08, zoom = 3.5, rot = 0.3 },
    [6]  = { h = 0.65, zoom = 1.8, rot = 0.3 },
    [7]  = { h = 0.22, zoom = 3.0, rot = 0.5 },
    [8]  = { h = 1.05, zoom = 3.8, rot = 0.7, x = 0.5 },
    [9]  = { h = 1.01, zoom = 4.4, rot = 0.7, x = 0.8 },
    [14] = { h = 1.20, zoom = 2.0, rot = 3.14 },
    [15] = { h = 1.55, zoom = 1.6, rot = 3.14, x = 0.25 },
    [16] = { h = 1.55, zoom = 1.6, rot = 3.14, x = 0.25 },
    [17] = { h = 1.55, zoom = 1.6, rot = 3.14, x = 0.25 },
    [18] = { h = 1.20, zoom = 2.0, rot = 0.0 },
}

local MESSAGES = {
    [1] = "Transmogrification applied.",
    [2] = "Invalid slot.",
    [3] = "Invalid appearance.",
    [4] = "You do not own that appearance.",
    [5] = "Nothing is equipped in that slot.",
    [6] = "That appearance cannot be used on this item.",
    [7] = "Not enough gold.",
    [8] = "Not enough tokens.",
    [9] = "Transmogrification removed.",
    [10] = "There is no transmogrification to remove.",
    [101] = "You must be next to a transmogrifier.",
    [102] = "Outfit saved.",
    [103] = "No free outfit slot.",
    [104] = "Invalid outfit name.",
    [105] = "Nothing to save in this outfit.",
    [106] = "Outfits are disabled.",
    [107] = "Outfit applied.",
    [108] = "Outfit deleted.",
    [109] = "Outfit slot purchased.",
    [110] = "You already have every outfit slot.",
    [111] = "Outfit renamed.",
}
local SUCCESS = { [1] = true, [9] = true, [102] = true, [107] = true, [108] = true, [109] = true, [111] = true }

-- View of the main model. A scale is anchored on the feet, so zooming also shifts the model down by
-- PIVOT (half its height, measured in-client) to keep the middle where it was. SetPosition is
-- (depth, horizontal, vertical).
local PIVOT = 0.9
local DEFAULT_ZOOM, DEFAULT_PAN_V, DEFAULT_ROT = 0.9, 0, 0.3
local ZOOM_MIN, ZOOM_MAX, ZOOM_STEP = 0.5, 3, 1.15
local PAN_SPEED, ROTATE_SPEED = 0.011, 0.012

local S = {
    equip = {}, fake = {}, cost = {},   -- per slot, from the server
    pending = {},                        -- slot -> 0 (revert) | HIDDEN | item entry; nil = unchanged
    info = { hidden = false, sets = false, maxSets = 0, setMod = 0, setCopper = 0, hiddenFree = false },
    sheathe = false, outfits = {}, maxSets = 0, outfitSlots = 0, slotCost = 2500000, outfitId = nil,
    slot = 0, page = 0, search = "", ready = false,
    items = {}, loaded = {}, total = nil,   -- the owned list for the slot, filled a server page at a time
    tab = "items",                          -- "items" (the selected slot's appearances) or "sets"
    sets = {}, setAsked = {}, setTotal = nil, nonce = 0, -- the server's item sets, asked for a grid page at a time
}
local V = { zoom = DEFAULT_ZOOM, panH = 0, panV = DEFAULT_PAN_V, rot = DEFAULT_ROT }

local win, model, stage
local slotButtons, cardButtons, outfitButtons = {}, {}, {}
local cardQueue = {}
local sendQueue, sendClock = {}, 0
local wantedIcons = {}
local L = { cols = 4, rows = 3, perPage = 12 } -- grid shape, set when the window is built
local U = 1 -- UI units per screen pixel

local function P(n) return n * U end

-- The window is drawn at 70% of the art's own pixel size. Slots are drawn at 50% of the size they had at
-- native, so they are smaller than the window's scale by SLOT_K; their icon fills 82% of the frame.
local WIN_K = 0.7
local SLOT_SCALE = 0.53 / WIN_K
local ICON_FILL = 0.82
local function SP(n) return n * U * SLOT_SCALE end
-- the icon and the animated square reach two screen pixels further up and to the left
local function PX() return 2 * U / WIN_K end
local function IconSize() return SP(110) * ICON_FILL + PX() end

------------------------------------------------------------------------------------------------------
-- helpers
------------------------------------------------------------------------------------------------------

local function Split(str, sep, limit)
    local out, pos = {}, 1
    while true do
        if limit and #out + 1 == limit then
            out[#out + 1] = str:sub(pos)
            break
        end
        local a, b = str:find(sep, pos, true)
        if not a then
            out[#out + 1] = str:sub(pos)
            break
        end
        out[#out + 1] = str:sub(pos, a - 1)
        pos = b + 1
    end
    return out
end

-- 3.3.5 regions have no show-by-boolean setter.
local function Toggle(region, show)
    if show then region:Show() else region:Hide() end
end

local function Clamp(v, lo, hi)
    if v < lo then return lo elseif v > hi then return hi end
    return v
end

-- Draws an atlas piece at its own size: the texture is sized to the rectangle, never the other way round.
local function Art(tex, key, k)
    local r = RECT[key]
    tex:SetTexture(ATLAS)
    SetCoords(tex, r[1], r[2], r[3], r[4])
    tex:SetSize(P(r[3]) * (k or 1), P(r[4]) * (k or 1))
end

-- The flipbook of an unsaved slot: 24 frames of a square of light, 6 to a row, in cells of 170 pixels that start
-- at (1021, 1045) of the 2048 sheet. The square fills about 145 of a cell; FLIP_SIZE is the cell's size in layout
-- units, picked so the square lies on the slot's frame.
local FLIP_PATH = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\uitransmogrifyflipbookseta2x"
local FLIP_FRAMES, FLIP_FPS, FLIP_SIZE = 24, 24, 138
local function FlipFrame(tex, n)
    local c, r = n % 6, floor(n / 6)
    local x0, y0 = 1021 + 170 * c, 1045 + 170 * r
    tex:SetTexCoord(x0 / 2048, (x0 + 170) / 2048, y0 / 2048, (y0 + 170) / 2048)
end

-- A piece of slot art: slots are drawn smaller than the rest of the window.
local function SlotArt(tex, key)
    Art(tex, key, SLOT_SCALE)
end

local function Money(copper)
    copper = copper or 0
    if GetCoinTextureString then return GetCoinTextureString(copper) end
    return format("%dg %ds %dc", floor(copper / 10000), floor(copper / 100) % 100, copper % 100)
end

local function Say(text, ok)
    if win and win.status then
        win.status:SetText(text or "")
        if ok then win.status:SetTextColor(0.4, 1, 0.4) else win.status:SetTextColor(1, 0.45, 0.35) end
    end
end

local function Send(body)
    sendQueue[#sendQueue + 1] = body
end

local scan
local function Prefetch(entry)
    if not entry or entry <= HIDDEN or GetItemInfo(entry) then return end
    if not wantedIcons[entry] then
        wantedIcons[entry] = GetTime()
        if not scan then
            scan = CreateFrame("GameTooltip", "ManastormTmogScan", UIParent, "GameTooltipTemplate")
        end
        scan:SetOwner(UIParent, "ANCHOR_NONE")
        scan:SetHyperlink("item:" .. entry .. ":0:0:0:0:0:0:0")
    end
end

local UNKNOWN_ICON = "Interface\\Icons\\INV_Misc_QuestionMark"
local function IconOf(entry)
    local texture = select(10, GetItemInfo(entry))
    if not texture then
        Prefetch(entry)
        return UNKNOWN_ICON
    end
    return texture
end

-- What a slot looks like right now, previews included. 0 means nothing is drawn.
local function ShownEntry(slot)
    local p = S.pending[slot]
    if p ~= nil then
        if p == 0 then return S.equip[slot] or 0 end
        return p
    end
    local fake = S.fake[slot] or 0
    if fake > 0 then return fake end
    return S.equip[slot] or 0
end

local function IsChanged(slot)
    local p = S.pending[slot]
    if p == nil then return false end
    local fake = S.fake[slot] or 0
    if p == 0 then return fake ~= 0 end
    return p ~= fake
end

local function PendingCost()
    local total, count = 0, 0
    for slot in pairs(S.pending) do
        if IsChanged(slot) then
            count = count + 1
            local p = S.pending[slot]
            local cosmetic = false
            if slot == 14 then
                for _, appearance in ipairs(EsteriaCosmeticsData or {}) do
                    if appearance.id == p then cosmetic = true; break end
                end
            end
            if p ~= 0 and not cosmetic and not (p == HIDDEN and S.info.hiddenFree) then
                total = total + (S.cost[slot] or 0)
            end
        end
    end
    return total, count
end

-- The screen's real pixel size, for the window layout and for U.
local function ScreenPixels()
    local w, h = (GetCVar("gxResolution") or ""):match("(%d+)x(%d+)")
    w, h = tonumber(w), tonumber(h)
    if not w or not h then return 1920, 1080 end
    return w, h
end

------------------------------------------------------------------------------------------------------
-- the character model: dressing and the view
------------------------------------------------------------------------------------------------------

-- A directional light on the character, a little stronger than the model frame's own: direction (x, y, z) in
-- model space, then the ambient and the direct intensity. /tmog light x y z ambient direct retunes it live.
local LIGHT = { x = -1, y = 0, z = 0, amb = 0.5, dir = 1.0 }
-- the card pictures are lit flat: the directional light leaves gear black from the card's angle
local CARD_LIGHT = { x = -1, y = 0, z = 0, amb = 1, dir = 0 }
local function ApplyLight(m)
    local l = m and CARD_LIGHT or LIGHT
    ;(m or model):SetLight(1, 0, l.x, l.y, l.z, l.amb, 1, 1, 1, l.dir, 1, 0.97, 0.88)
end

-- How tall this model is next to the one PIVOT and the card heights were measured on (a night elf), so
-- a shorter race (blood elf) or a taller one (tauren) lands in the same place. 1 until it has loaded.
-- REF_HEIGHT is GetModelHeight() on a night elf (read it with /tmog height). A reading far from it is a
-- bad bounding box (the human female reads 1.7), so it is ignored rather than trusted.
local REF_HEIGHT = 4.8836
local RATIO_OVERRIDE   -- /tmog ratio <n>: forced for this session, to find the right value by eye
local function HeightRatio(m)
    if RATIO_OVERRIDE then return RATIO_OVERRIDE end
    local height = m.GetModelHeight and m:GetModelHeight() or 0
    local ratio = height / REF_HEIGHT
    return ratio > 0.7 and ratio < 1.3 and ratio or 1
end

local function ApplyView()
    if not model then return end
    ApplyLight()
    model:SetModelScale(V.zoom)
    model:SetPosition(0, V.panH, V.panV - PIVOT * HeightRatio(model) * (V.zoom - 1))
    model:SetRotation(V.rot)
end

-- SetUnit re-fits the model and folds any scale it finds into the fit, so the view is flattened first
-- and re-asserted for a moment afterwards (the swap lands a few frames after the call returns).
local function ResetModelUnit()
    model:SetModelScale(1)
    model:SetPosition(0, 0, 0)
    model:SetUnit("player")
    win.settle = 1
end

local function ResetView()
    V.zoom, V.panH, V.panV, V.rot = DEFAULT_ZOOM, 0, DEFAULT_PAN_V, DEFAULT_ROT
    ApplyView()
end

local function ClampPan()
    local limit = 1.2 * V.zoom
    V.panH = Clamp(V.panH, -limit, limit)
    V.panV = Clamp(V.panV, -limit, limit)
end

local function ZoomBy(notches)
    V.zoom = Clamp(V.zoom * (ZOOM_STEP ^ notches), ZOOM_MIN, ZOOM_MAX)
    ClampPan()
    ApplyView()
end

-- Dress the preview from scratch: bare, then every slot's shown appearance.
local DRESS_ORDER = { 0, 2, 14, 4, 3, 18, 8, 9, 5, 6, 7, 15, 16, 17 }
local function RebuildModel()
    if not model or not win or not win:IsShown() then return end
    model:Undress()
    -- sheathed weapons hang on the back or hip (a client method; an older client just shows them in hand)
    if model.SetSheathed then model:SetSheathed(S.sheathe) end
    for _, slot in ipairs(DRESS_ORDER) do
        local entry = ShownEntry(slot)
        if entry and entry > HIDDEN then model:TryOn(entry) end
    end
end

------------------------------------------------------------------------------------------------------
-- the owned-appearance list: the server sends 20 at a time, the grid shows what fits
------------------------------------------------------------------------------------------------------

local function InSets()
    return S.tab == "sets"
end

local function HasHideCard()
    return not InSets() and S.info.hidden and (S.equip[S.slot] or 0) > 0
end

local function VirtualTotal()
    if InSets() then return S.setTotal or 0 end
    return (S.total or 0) + (HasHideCard() and 1 or 0)
end

-- What card number v (1-based, across all pages) shows: HIDDEN for the hide card, an entry, or nil.
local function VirtualItem(v)
    if HasHideCard() then
        if v == 1 then return HIDDEN end
        v = v - 1
    end
    return S.items[v]
end

local function RequestServerPage(sp)
    if S.loaded[sp] then return end
    S.loaded[sp] = "asked"
    Send(format("L:%d:%d:%s", S.slot, sp, S.search))
end

-- The sets tab: the grid page [first, first + perPage) of the sets list, asked for once per page.
local function EnsureSets()
    if S.setAsked[S.page] then return end
    S.setAsked[S.page] = true
    Send(format("SL:%d:%d:%d:%s", S.nonce, S.page * L.perPage, L.perPage, S.search))
end

-- Asks for whichever server pages the current page of the grid still lacks.
local function EnsureItems()
    if InSets() then
        EnsureSets()
        return
    end
    if not S.total then
        RequestServerPage(0)
        return
    end
    local first = S.page * L.perPage + 1
    local last = math.min(first + L.perPage - 1, VirtualTotal())
    if HasHideCard() then first, last = first - 1, last - 1 end
    first = math.max(first, 1)
    for r = first, last do
        RequestServerPage(math.floor((r - 1) / SERVER_PAGE))
    end
end

local function ResetList()
    S.items, S.loaded, S.total, S.page = {}, {}, nil, 0
    -- replies to an older sets request carry an older nonce and are ignored
    S.sets, S.setAsked, S.setTotal, S.nonce = {}, {}, nil, S.nonce + 1
end

-- Where the pieces of a set go: { { slot, entry, owned }, ... } from the server's "slot=entry,slot/entry" list.
local function ParsePieces(text)
    local out = {}
    for _, pair in ipairs(Split(text or "", ",")) do
        local slot, sep, entry = pair:match("^(%d+)([=/])(%d+)$")
        if slot then out[#out + 1] = { slot = tonumber(slot), entry = tonumber(entry), owned = sep == "=" } end
    end
    return out
end

------------------------------------------------------------------------------------------------------
-- item cards: each one is the player wearing only that appearance, framed on the slot
------------------------------------------------------------------------------------------------------

-- a set card shows the whole character, a little turned
local SET_VIEW = { h = 0.9, zoom = 1.21, rot = 0.3 }

-- The cards do not hold a model each. One hidden model, always a plain human (the card only shows a dimmed
-- silhouette wearing the item), is dressed for one card at a time and photographed by the client into a
-- picture named "Snapshot:<key>"; a card is a texture showing that picture. The client keeps the pictures, so
-- a page that was seen before, another slot, or a /reload shows at once.
-- Several hidden models work at once: a picture is mostly waiting (the model builds, textures load, a few
-- frames are drawn), and those waits overlap. Drawing itself stays on the render thread: the device and the
-- model loader are not thread safe, so this is the parallelism available.
-- A worker is { m = hidden model, shot = job in progress: { key, set, entry, slot, t, wait, state },
-- warm = the model has finished loading once, so later jobs need not wait for it }.
local workers = {}
-- The shot settings, state and helpers live in one table: the file is close to Lua 5.1's limit of 200 locals in a chunk.
-- tries: jobs that ended without a picture, per key; gaveUp: when a key ran out of its tries; rest: seconds such a key
-- is left alone; sig: the view signature of SHOT.ViewSig; watch: time since the cards were last checked.
local SHOT = { workers = 12, timeout = 6, drawTimeout = 25, tries = 8, rest = 10, version = 6, tried = {}, gaveUp = {}, watch = 0 }
local scratch   -- the first worker's model; the picture cache is shared, so any one answers HasSnapshot
-- once drawing, a helm, shoulder or weapon whose attached model never reports waits up to 3x its frames (270),
-- which is over 6 s when the frame rate is low
-- hair style numbers of the human preview: short hair for the man, a ponytail for the woman (6).
-- /tmog hair <m|f> <n> tries another.
local HAIR = { m = 0, f = 6 }
-- /tmog hairlit draws the preview in full colour so hair styles can be told apart (the silhouette hides them)
local hairLit = false

function SHOT.NewWorker()
    local m = CreateFrame("DressUpModel", nil, win)
    m:SetSize(P(198), P(262))
    m:SetPoint("CENTER", win, "CENTER")
    m:SetSnapshotOnly(true)
    local w = { m = m }
    workers[#workers + 1] = w
    scratch = scratch or m
    return w
end

function SHOT.Scratch()
    if not scratch then SHOT.NewWorker() end
    return scratch
end

-- every worker's model must be built again (hair changed, window closed)
function SHOT.ColdWorkers()
    for _, w in ipairs(workers) do
        w.shot, w.warm = nil, nil
        w.m:Snapshot("")
    end
end

-- The picture's name carries the framing, not just the item: a picture cannot be shown under a name a region has
-- already been given (a texture region ignores SetTexture when the name it holds has not changed), so moving the
-- framing must ask for a differently named picture rather than the same one drawn again.
-- A weapon card is framed per kind of weapon (a sword, a staff and a bow differ in size), not just per slot. The kind is
-- the item's class and subclass, found by their place in the auction house's lists: those are in the game's fixed
-- order whatever the language, unlike the names GetItemInfo gives. Key "15:1.8" = slot 15, class 1, subclass 8; the
-- tables hold such a key only for a kind that was framed apart, any other weapon uses its slot's entry.
-- Returns the key and the kind's name; nil for a slot that is not a weapon's or an item the client does not know yet.
function SHOT.TypeKey(slot, entry)
    if not entry or slot < 15 or slot > 17 then return nil end
    local _, _, _, _, _, class, sub = GetItemInfo(entry)
    if not class or not sub then return nil end
    if not SHOT.kinds then
        SHOT.kinds = {}
        for ci, cname in ipairs({ GetAuctionItemClasses() }) do
            for si, sname in ipairs({ GetAuctionItemSubClasses(ci) }) do SHOT.kinds[cname .. "/" .. sname] = { ci .. "." .. si, sname } end
        end
    end
    local kind = SHOT.kinds[class .. "/" .. sub]
    if kind then return slot .. ":" .. kind[1], kind[2] end
end

function SHOT.Nudge(slot, entry)
    local key = SHOT.TypeKey(slot, entry)
    return key and CARD_NUDGE[key] or CARD_NUDGE[slot]
end

function SHOT.NudgeTag(slot, entry)
    local n = slot and SHOT.Nudge(slot, entry) or SET_NUDGE
    -- letters and digits only (thousandths, n for minus): a '.', '+' or '-' in a picture's name is not worth the risk
    local function part(v)
        v = math.floor((v or 0) * 1000 + (v and v < 0 and -0.5 or 0.5))
        return (v < 0 and "n" or "p") .. math.abs(v)
    end
    -- the zoom share only when it is set, so the names of pictures framed before it existed stay as they were
    local z = n.zoom and math.abs(n.zoom - 1) > 0.0005 and ("z" .. part(n.zoom)) or ""
    -- and the turn likewise
    local r = n.rot and math.abs(n.rot) > 0.0005 and ("r" .. part(n.rot)) or ""
    -- and the turns about Y and X
    local p = n.pitch and math.abs(n.pitch) > 0.0005 and ("y" .. part(n.pitch)) or ""
    local o = n.roll and math.abs(n.roll) > 0.0005 and ("x" .. part(n.roll)) or ""
    return "u" .. part(n.up) .. "l" .. part(n.left) .. z .. r .. p .. o
end

-- Pictures are kept on disk (Cache/Snapshots) between sessions, so the name must also change when anything that shapes
-- a picture does: the framing and light tables, the ratio. Bump SHOT.version when the drawing code itself changes.
function SHOT.ViewSig()
    if SHOT.sig then return SHOT.sig end
    local parts = { SHOT.version, PIVOT, FRAME_SPAN, RATIO_OVERRIDE or 0, CARD_LIGHT.x, CARD_LIGHT.y, CARD_LIGHT.z, CARD_LIGHT.amb, CARD_LIGHT.dir, SET_VIEW.h, SET_VIEW.zoom, SET_VIEW.rot }
    local slots = {}
    for slot in pairs(CARD_VIEW) do slots[#slots + 1] = slot end
    table.sort(slots)
    for _, slot in ipairs(slots) do
        local v = CARD_VIEW[slot]
        parts[#parts + 1] = slot .. "," .. (v.zoom or 0) .. "," .. (v.h or 0) .. "," .. (v.x or 0) .. "," .. (v.rot or 0)
    end
    local text = table.concat(parts, ";")
    local h = 5381
    for i = 1, #text do h = (h * 33 + text:byte(i)) % 2147483647 end
    SHOT.sig = format("%x", h)
    return SHOT.sig
end

-- picture name of what a card shows; the sex is in it because the silhouette follows the player's
function SHOT.ShotKey(set, entry, slot)
    local female = UnitSex("player") == 3
    -- the hair is in it too: a new hair style must not show pictures kept on disk with the old one
    local sex = (female and "f" .. HAIR.f or "m" .. HAIR.m) .. (hairLit and "t" or "")
    sex = sex .. SHOT.ViewSig()
    if set then return "s" .. set.id .. sex .. SHOT.NudgeTag(nil) end
    if entry and entry > HIDDEN then return entry .. ":" .. slot .. sex .. SHOT.NudgeTag(slot, entry) end
end

-- The bare character in the framing of a slot (or of the sets). A picture of an item has to differ from it: one
-- taken before the item has loaded is the bare character, a dark figure on black.
function SHOT.BareKey(set, slot)
    local female = UnitSex("player") == 3
    local sex = (female and "f" .. HAIR.f or "m" .. HAIR.m) .. (hairLit and "t" or "") .. SHOT.ViewSig()
    if set then return "bares" .. sex .. SHOT.NudgeTag(nil) end
    return "bare" .. slot .. sex .. SHOT.NudgeTag(slot)
end

function SHOT.CardApplyView(m, set, slot, entry)
    local v = set and SET_VIEW or CARD_VIEW[slot] or CARD_VIEW[4]
    -- the human preview model is always the same size, so no height ratio is needed (found by eye: 1);
    -- /tmog ratio <n> still overrides it
    local ratio = RATIO_OVERRIDE or 1
    local n = set and SET_NUDGE or SHOT.Nudge(slot, entry) or {}
    -- the pad's zoom scales the slot's own zoom; the model's height stays put, so it zooms about the part shown
    local zoom = v.zoom * (n.zoom or 1)
    m:SetModelScale(zoom / ratio)
    m:SetPosition(0, (v.x or 0) - (n.left or 0) * FRAME_SPAN * 0.75, PIVOT - zoom * v.h + (n.up or 0) * FRAME_SPAN)
    -- the pad's turn adds to the slot's own
    m:SetRotation(v.rot + (n.rot or 0))
    m:SetTilt(n.pitch or 0, n.roll or 0)
end

function SHOT.CardDress(m, set, entry)
    m:Undress()
    if set then
        for _, piece in ipairs(set.pieces) do m:TryOn(piece.entry) end
    elseif entry and entry > HIDDEN then
        m:TryOn(entry)
    end
end

-- the model dresses an item only once the client knows it; until then it would be photographed bare
function SHOT.ShotItemsKnown(job)
    if job.bare then return true end
    if job.set then
        for _, piece in ipairs(job.set.pieces) do
            if not GetItemInfo(piece.entry) then Prefetch(piece.entry) return false end
        end
        return true
    end
    if GetItemInfo(job.entry) then return true end
    Prefetch(job.entry)
    return false
end


function SHOT.StartShot(w, b)
    local m = w.m
    local shot = { key = b.shotKey, set = b.set, entry = b.entry, slot = S.slot, t = 0, state = "items", bare = b.bare }
    w.shot = shot
    m:Snapshot("") -- drops a request an earlier job left waiting, which would photograph this job's dressing
    local female = UnitSex("player") == 3
    m:SetHumanPreview(female, female and HAIR.f or HAIR.m)
    ApplyLight(m) -- the same light as the main preview, so the item is lit the same in both
    m:SetModelScale(1)
    m:SetPosition(0, 0, 0)
    -- the bare body is dimmed (the client dims its skin, not the gear on it)
    m:SetSilhouette(not hairLit)
    -- a weapon card shows the weapon alone, not the character holding it, sheathed so a staff stands upright
    local weapon = not shot.set and shot.slot >= 15 and shot.slot <= 17
    m:SetBodyHidden(weapon)
    m:SetSheathed(weapon)
    -- shortcut: attached models (weapons, shoulders) and enchant glows load apart from the character and the client
    -- cannot tell when they have, so the picture is drawn for a fixed number of frames first (a weapon has more to
    -- load); upgrade to a loaded check on the attachments if glows still come out missing
    -- a job that failed before waits longer each time
    local tries = SHOT.tried[shot.key] or 0
    -- the frames alone are not enough: at a high frame rate 36 of them pass before the item's textures have loaded
    shot.wait = (w.warm and 0.3 or 0.6) + 0.4 * tries
    shot.frames = weapon and 90 or 36
    -- the picture of an item is checked against the bare character (a weapon is drawn without one)
    shot.ref = not shot.bare and not weapon and SHOT.BareKey(shot.set, shot.slot) or nil
    -- helm, shoulders and weapons are models of their own
    shot.attach = not shot.bare and not shot.set and (shot.slot == 0 or shot.slot == 2 or weapon)
end

-- The pad, declared here because RefreshAll (below) asks it to restate the slot the window has selected.
local framePad

-- A card shows the picture named "Snapshot:<key>"
-- False when the client could not load the stored picture (it removes one it cannot read, so it is made again).
function SHOT.ShowShot(b, key)
    if not b.model:SetTexture("Snapshot:" .. key) then
        SHOT.HideShot(b)
        return false
    end
    b.model:Show()
    b.loading:Hide()
    return true
end

-- A card with no picture lets go of the one it had: the client keeps a texture only for what is on screen.
function SHOT.HideShot(b)
    b.model:SetTexture(nil)
    b.model:Hide()
    -- a card that has a picture coming shows the loading mark meanwhile
    if b.shotKey then b.loading:Show() else b.loading:Hide() end
end

-- Tells the client which pictures are on screen; the textures of all the others are freed.
function SHOT.KeepVisibleShots()
    local keys = {}
    for _, c in ipairs(cardButtons) do
        if c:IsShown() and c.shotKey and c.model:IsShown() then keys[#keys + 1] = c.shotKey end
    end
    SHOT.Scratch():KeepSnapshots(unpack(keys))
end

-- Every frame: the job goes items known -> dressed and framed -> photographed, one card at a time.
function SHOT.ShotRunning(key)
    for _, w in ipairs(workers) do
        if w.shot and w.shot.key == key then return true end
    end
end

-- A job ended without a picture (timed out, or the client could not draw it): the visible cards that wanted it
-- go back in the queue, so a card is never left blank for good.
function SHOT.RetryShot(w, key)
    w.shot = nil
    w.m:Snapshot("")
    local tries = (SHOT.tried[key] or 0) + 1
    SHOT.tried[key] = tries
    if tries >= SHOT.tries then
        -- SHOT.CardShot tries it afresh once SHOT.rest has passed
        SHOT.gaveUp[key] = GetTime()
        print("Transmog: no picture for " .. key .. " after " .. tries .. " tries (/tmog shot for the details)")
        return
    end
    w.warm = nil
    for _, c in ipairs(cardButtons) do
        if c.shotKey == key and c:IsShown() and not c.queued then
            c.queued = true
            cardQueue[#cardQueue + 1] = c
        end
    end
end

function SHOT.PumpWorker(w, dt)
    local m = w.m
    local shot = w.shot
    if not shot then
        local i = 1
        while i <= #cardQueue do
            local b = cardQueue[i]
            if not b:IsShown() or not b.shotKey then
                b.queued = false
                table.remove(cardQueue, i)
            elseif m:HasSnapshot(b.shotKey) then
                -- the picture exists (made by another job, or stored all along): show it, do not just drop the card
                b.queued = false
                table.remove(cardQueue, i)
                SHOT.ShowShot(b, b.shotKey)
            elseif SHOT.ShotRunning(b.shotKey) then
                i = i + 1 -- another worker is on it; its cards are shown when it finishes
            else
                -- the bare character of this framing comes first (not for a weapon, which is drawn alone)
                local weapon = not b.set and S.slot >= 15 and S.slot <= 17
                local bareKey = not weapon and SHOT.BareKey(b.set, S.slot) or nil
                -- a bare character that will not come out is given up on: the card is made without the check
                if bareKey and not m:HasSnapshot(bareKey) and (SHOT.tried[bareKey] or 0) < 4 then
                    if SHOT.ShotRunning(bareKey) then
                        i = i + 1 -- another worker is making it; this card waits in the queue
                    else
                        SHOT.StartShot(w, { bare = true, shotKey = bareKey, set = b.set and { pieces = {} } or nil })
                        break
                    end
                else
                    b.queued = false
                    table.remove(cardQueue, i)
                    SHOT.StartShot(w, b)
                    break
                end
            end
        end
        if not w.shot then return end
        shot = w.shot
    end

    shot.t = shot.t + dt
    if shot.t > (shot.state == "shoot" and SHOT.drawTimeout or SHOT.timeout) then
        SHOT.RetryShot(w, shot.key)
        return
    end

    if shot.state == "items" then
        if not SHOT.ShotItemsKnown(shot) then return end
        shot.state, shot.t = "dress", 0
        SHOT.CardDress(m, shot.set, shot.entry)
    end

    SHOT.CardApplyView(m, shot.set, shot.slot, shot.entry)
    m:SetSequence(0)
    m:SetSequenceTime(0, 0)

    if shot.state == "dress" then
        -- a fresh model is not built yet when it is first dressed, so it is dressed again once it is
        if not w.warm and not shot.redressed and shot.t > 0.4 then
            shot.redressed = true
            SHOT.CardDress(m, shot.set, shot.entry)
        elseif shot.t >= shot.wait then
            -- from the fourth try on the picture is stored even if it looks empty (an item that really is that dark)
            -- the bare character is stored whatever it looks like: it is what the others are checked against
            m:Snapshot(shot.key, shot.frames, shot.attach, shot.bare and 2 or ((SHOT.tried[shot.key] or 0) >= 3 and 1 or 0), shot.ref)
            shot.state = "shoot"
        end
    elseif not m:IsSnapshotPending() then
        if m:HasSnapshot(shot.key) then
            w.warm = true
            SHOT.tried[shot.key] = nil
            for _, c in ipairs(cardButtons) do
                if c.shotKey == shot.key and c:IsShown() then SHOT.ShowShot(c, shot.key) end
            end
            w.shot = nil
        else
            SHOT.RetryShot(w, shot.key)
        end
    end
end

-- Every frame: each worker runs its own job; a worker is added while cards wait and a worker is free.
function SHOT.PumpShots(dt)
    if #cardQueue > 0 and #workers < SHOT.workers then
        local busy = true
        for _, w in ipairs(workers) do busy = busy and w.shot ~= nil end
        if busy then SHOT.NewWorker() end
    end
    for _, w in ipairs(workers) do SHOT.PumpWorker(w, dt) end
end

-- Show a card's picture if the client has it, else ask for it.
function SHOT.CardShot(b)
    b.shotKey = SHOT.ShotKey(b.set, b.entry, S.slot)
    if not b.shotKey then
        SHOT.HideShot(b)
    elseif SHOT.Scratch():HasSnapshot(b.shotKey) and SHOT.ShowShot(b, b.shotKey) then
        return
    else
        SHOT.HideShot(b)
        if (SHOT.tried[b.shotKey] or 0) >= SHOT.tries then
            if GetTime() - (SHOT.gaveUp[b.shotKey] or 0) < SHOT.rest then return end
            SHOT.tried[b.shotKey] = nil
        end
        if not b.queued then
            b.queued = true
            cardQueue[#cardQueue + 1] = b
        end
    end
end

-- Turns the loading marks, 25 turns a minute. 3.3.5 textures have no SetRotation, so the texture coordinates of the
-- square are turned about its centre (the corners that leave the image sample its transparent edge).
SHOT.loadingRpm = 25 -- a field, not a local: the file is close to Lua's 200-locals limit
function SHOT.SpinLoading()
    local a = GetTime() * SHOT.loadingRpm / 60 * 2 * math.pi
    local c, s = math.cos(a) * 0.5, math.sin(a) * 0.5
    local ulx, uly = 0.5 - c + s, 0.5 - s - c
    local llx, lly = 0.5 - c - s, 0.5 - s + c
    local urx, ury = 0.5 + c + s, 0.5 + s - c
    local lrx, lry = 0.5 + c - s, 0.5 + s + c
    for _, b in ipairs(cardButtons) do
        if b.loading:IsShown() then b.loading:SetTexCoord(ulx, uly, llx, lly, urx, ury, lrx, lry) end
    end
end

-- Safety net: a visible card that wants a picture and shows none, with no job on it, is asked for again. Whatever
-- left it that way (a texture that could not be made at that moment, a job that gave up) is retried, not kept
-- until the window is reopened.
function SHOT.WatchCards(dt)
    SHOT.watch = SHOT.watch + dt
    if SHOT.watch < 1 then return end
    SHOT.watch = 0
    for _, c in ipairs(cardButtons) do
        if c:IsShown() and c.shotKey and not c.model:IsShown() and not c.queued and not SHOT.ShotRunning(c.shotKey) then
            SHOT.CardShot(c)
        end
    end
end

-- Whether the preview already wears every piece of the set the account owns (and there is one).
local function SetIsOn(set)
    local any = false
    for _, piece in ipairs(set.pieces) do
        if piece.owned and (S.equip[piece.slot] or 0) > 0 then
            any = true
            if ShownEntry(piece.slot) ~= piece.entry then return false end
        end
    end
    return any
end

local EPIC = { 0.64, 0.21, 0.93 }
local cardVignettes = {} -- card -> its four vignette gradients

local function SetCardBorder(b)
    local entry, slot = b.entry, S.slot
    local key = "cardBronze"
    local selected = false
    if b.set then
        if SetIsOn(b.set) then
            key = "cardPreview"
            selected = true
        end
        entry = b.set.id
    elseif entry then
        local pending = S.pending[slot]
        if pending == entry then
            key = "cardPreview"
            selected = true
        elseif (S.fake[slot] or 0) == entry and pending == nil then
            key = "cardApplied"
        end
    end
    if b.hoverFrame then Toggle(b.hoverFrame, b.over and entry and key == "cardBronze") end
    local r = RECT[key]
    SetCoords(b.bg, r[1], r[2], r[3], r[4])
    -- the frame of a set with nothing collected is grey
    local empty = b.set and b.set.owned == 0
    b.bg:SetDesaturated(empty and true or false)
    if empty then b.bg:SetVertexColor(0.75, 0.75, 0.75) else b.bg:SetVertexColor(1, 1, 1) end
    -- the vignette of the selected item turns epic purple
    if cardVignettes[b] then
        local c = selected and EPIC or { 0, 0, 0 }
        local k = selected and 0.5 or 1 -- the purple is kept subtle
        for _, v in ipairs(cardVignettes[b]) do
            v.tex:SetGradientAlpha(v.orient, c[1], c[2], c[3], v.minA * k, c[1], c[2], c[3], v.maxA * k)
        end
    end
end

------------------------------------------------------------------------------------------------------
-- refreshing the widgets
------------------------------------------------------------------------------------------------------

local RefreshAll
local SaveHasChanges -- defined with the actions below

local function RefreshSlotButton(b)
    local slot = b.slot
    local def = SLOT_BY_ID[slot]
    local equip = S.equip[slot] or 0
    local entry = ShownEntry(slot)
    local changed = IsChanged(slot)
    local transmogged = (S.fake[slot] or 0) > 0 and not changed

    b.eye:Hide()
    b.icon:SetDesaturated(false)
    b.icon:SetAlpha(1)
    b.icon:SetSize(IconSize(), IconSize())
    b.icon:SetTexCoord(0, 1, 0, 1)

    if equip == 0 then
        if def.empty then
            SlotArt(b.icon, def.empty)
            b.icon:SetSize(IconSize(), IconSize())
        else
            b.icon:SetTexture(b.emptyTexture)
        end
        b.icon:SetDesaturated(true)
        b.icon:SetAlpha(0.45)
    elseif entry == HIDDEN then
        b.icon:SetTexture(0.02, 0.02, 0.03, 1)
        b.eye:Show()
    else
        b.icon:SetTexture(IconOf(entry))
    end

    -- frame: dark when empty, purple once transmogrified, bronze otherwise
    local frame = "slotBronze"
    if equip == 0 then frame = "slotDark" elseif transmogged or changed then frame = "slotPurple" end
    SlotArt(b.frame, frame)
    Toggle(b.no, equip == 0)
    if equip == 0 then b:Disable() else b:Enable() end
    Toggle(b.selected, S.slot == slot)
    Toggle(b.pending, changed)
end

local function RefreshGrid()
    local hasHide = HasHideCard()
    local vtotal = VirtualTotal()

    for j, b in ipairs(cardButtons) do
        local v = S.page * L.perPage + j
        local set = InSets() and v <= vtotal and S.sets[v] or nil
        local id = not InSets() and v <= vtotal and VirtualItem(v) or nil
        -- a card shows a set or an appearance, never both
        b.set, b.entry = set, id
        Toggle(b.setName, set)
        Toggle(b.setCount, set)
        Toggle(b.grey, set and set.owned == 0)
        if set then
            for _, piece in ipairs(set.pieces) do Prefetch(piece.entry) end
            b.setName:SetText(set.name)
            b.setCount:SetText(format("%d/%d", set.owned, set.total))
            if set.owned == 0 then
                b.setName:SetTextColor(0.6, 0.6, 0.6)
                b.setCount:SetTextColor(0.5, 0.5, 0.5)
            else
                b.setName:SetTextColor(1, 0.82, 0)
                if set.owned == set.total then b.setCount:SetTextColor(0.4, 1, 0.4) else b.setCount:SetTextColor(1, 0.82, 0) end
            end
            SetCardBorder(b)
            Toggle(b.applied, false)
            Toggle(b.eye, false)
            Toggle(b.dim, false)
            b:Show()
            SHOT.CardShot(b)
        elseif id then
            Prefetch(id)
            SetCardBorder(b)
            local applied = id == (S.fake[S.slot] or 0)
            Toggle(b.applied, applied)
            Toggle(b.eye, id == HIDDEN)
            Toggle(b.dim, id == HIDDEN)
            b:Show()
            SHOT.CardShot(b)
        else
            b.shotKey = nil
            SHOT.HideShot(b)
            b:Hide()
        end
    end

    SHOT.KeepVisibleShots()

    local pages = math.max(1, math.ceil(vtotal / L.perPage))
    win.pageText:SetText(format("Page %d / %d", S.page + 1, pages))
    if S.page > 0 then win.prev:Enable() else win.prev:Disable() end
    if S.page + 1 < pages then win.next:Enable() else win.next:Disable() end

    local def = SLOT_BY_ID[S.slot]
    win.slotTitle:SetText(InSets() and "Sets" or def and def.name or "")
    win.tabItems:SetActive(not InSets())
    win.tabSets:SetActive(InSets())
    Toggle(win.showEquipped, not InSets())
    if InSets() then
        if S.setTotal == 0 then
            win.empty:SetText("No sets found.")
            win.empty:Show()
        else
            win.empty:Hide()
        end
    elseif (S.equip[S.slot] or 0) == 0 then
        win.empty:SetText("Nothing is equipped in this slot.")
        win.empty:Show()
    elseif S.total == 0 and not hasHide then
        win.empty:SetText(S.ready and "No unlocked appearances for this slot." or "Waiting for the server...")
        win.empty:Show()
    else
        win.empty:Hide()
    end
end

local function OutfitIsWorn(o)
    if not o then return false end
    for slot, entry in pairs(o.items) do
        if (S.fake[slot] or 0) ~= entry then return false end
    end
    for slot, fake in pairs(S.fake) do
        if fake > 0 and o.items[slot] ~= fake then return false end
    end
    return true
end

local function RefreshButtons()
    local cost, count = PendingCost()
    win.cost:SetText(count > 0 and Money(cost) or "")
    local anyTransmog = false
    for _, fake in pairs(S.fake) do
        if fake > 0 then anyTransmog = true break end
    end
    local canApply = count > 0 and GetMoney() >= cost
    -- only a previewed change gives them something to do: a fresh window, or an outfit that is already worn, has none
    if canApply then win.apply:Enable() else win.apply:Disable() end
    Toggle(win.save, S.info.sets)
    if S.info.sets and SaveHasChanges() then win.save:Enable() else win.save:Disable() end
    if count > 0 then win.reset:Enable() else win.reset:Disable() end

    local hasEquip = (S.equip[S.slot] or 0) > 0
    local shownFake = (S.pending[S.slot] == nil and (S.fake[S.slot] or 0) > 0) or
        (S.pending[S.slot] ~= nil and S.pending[S.slot] ~= 0)
    if hasEquip and shownFake then win.showEquipped:Enable() else win.showEquipped:Disable() end
    if S.info.sets and S.outfitSlots < S.maxSets then win.buySlot:Enable() else win.buySlot:Disable() end
    win.buySlot:SetLabel("Buy Slot  " .. Money(S.slotCost))
end

-- Whether a saved outfit is exactly what is on the character right now: every slot it names carries that
-- transmog, and no other slot carries one.

-- One row per outfit slot the player owns, empty ones included. The worn outfit stays lit, with its check mark, while
-- the preview changes.
local function RefreshOutfits()
    for i, b in ipairs(outfitButtons) do
        local id = i - 1
        if S.info.sets and id < S.outfitSlots then
            local o = S.outfits[id]
            b.id = id
            b.label:SetText(o and o.name or "|cff7d7468Empty outfit slot|r")
            Toggle(b.check, OutfitIsWorn(o))
            -- lit like the check mark: for as long as the outfit is on the character, whatever is previewed on top
            b:SetActive(S.outfitId == id or OutfitIsWorn(o))
            b:Show()
        else
            b.id = nil
            b:Hide()
        end
    end
    Toggle(win.noOutfits, not S.info.sets)
end

RefreshAll = function()
    if not win then return end
    for _, b in ipairs(slotButtons) do RefreshSlotButton(b) end
    RefreshGrid()
    RefreshButtons()
    RefreshOutfits()
    if framePad then framePad:Refresh() end
end

------------------------------------------------------------------------------------------------------
-- actions
------------------------------------------------------------------------------------------------------

local function RequestList()
    ResetList()
    EnsureItems()
    RefreshGrid()
end

local function SelectSlot(slot)
    S.slot = slot
    S.tab = "items" -- picking a slot shows its appearances
    RequestList()
    RefreshAll()
end

local function SelectTab(tab)
    if S.tab == tab then return end
    S.tab = tab
    RequestList()
    RefreshAll()
end

-- Previews the pieces of a set the account owns on the slots that hold an item; the rest of the preview stays.
local function PreviewSet(set)
    for _, piece in ipairs(set.pieces) do
        if piece.owned and (S.equip[piece.slot] or 0) > 0 then
            if piece.entry == (S.fake[piece.slot] or 0) then
                S.pending[piece.slot] = nil
            else
                S.pending[piece.slot] = piece.entry
            end
        end
    end
    RebuildModel()
    RefreshAll()
end

local function SetPending(slot, entry)
    local fake = S.fake[slot] or 0
    if entry == fake or (entry == 0 and fake == 0) then
        S.pending[slot] = nil
    else
        S.pending[slot] = entry
    end
    RebuildModel()
    RefreshAll()
end

local function GoToPage(page)
    S.page = page
    EnsureItems()
    RefreshGrid()
end

-- the wheel turns the pages of the grid: up for the previous one
local function WheelPage(delta)
    if delta > 0 and S.page > 0 then
        GoToPage(S.page - 1)
    elseif delta < 0 and (S.page + 1) * L.perPage < VirtualTotal() then
        GoToPage(S.page + 1)
    end
end

local function ResetPreview()
    wipe(S.pending)
    RebuildModel()
    RefreshAll()
end

-- Apply Outfit: puts the previewed changes on the character (the server charges for each slot).
local function Apply()
    local cost, count = PendingCost()
    -- nothing previewed: the button is greyed out and does nothing
    if count == 0 then return end
    if GetMoney() < cost then
        Say(MESSAGES[7], false)
        return
    end
    for slot in pairs(S.pending) do
        if IsChanged(slot) then Send(format("A:%d:%d", slot, S.pending[slot])) end
    end
end

-- What the preview shows on every slot that holds an item, as an outfit would store it.
local function PreviewLook()
    local look = {}
    for _, def in ipairs(SLOTS) do
        local slot = def.slot
        if (S.equip[slot] or 0) > 0 then
            local p = S.pending[slot]
            local entry
            if p ~= nil then entry = p else entry = S.fake[slot] or 0 end
            if entry > 0 then look[slot] = entry end
        end
    end
    return look
end

-- Whether an outfit holds exactly this look (only slots that carry an item count).
local function OutfitMatches(o, look)
    for slot, entry in pairs(look) do
        if o.items[slot] ~= entry then return false end
    end
    for slot, entry in pairs(o.items) do
        if (S.equip[slot] or 0) > 0 and look[slot] ~= entry then return false end
    end
    return true
end

-- Whether saving would change anything: with an outfit selected, the preview against it; otherwise the look
-- must be a new one, with something in it, that no stored outfit already holds. (So a reset preview, which shows
-- what the character wears, can be saved as a new outfit.)
SaveHasChanges = function()
    local look = PreviewLook()
    local o = S.outfitId ~= nil and S.outfits[S.outfitId] or nil
    if o then return not OutfitMatches(o, look) end
    if next(look) == nil then return false end
    for _, other in pairs(S.outfits) do
        if OutfitMatches(other, look) then return false end
    end
    return true
end

-- Save Outfit: stores the preview as an outfit without touching the character -- into the selected outfit,
-- or, when none is selected and a slot is free, under a name asked for.
local function SaveOutfit()
    if not S.info.sets or not SaveHasChanges() then return end
    if S.outfitId ~= nil then
        local o = S.outfits[S.outfitId]
        if o then
            ManastormTransmog_SaveOutfitTo(S.outfitId, o.name)
        else
            StaticPopup_Show("MANASTORM_TMOG_SAVE") -- an empty slot: its outfit needs a name
        end
        return
    end
    for i = 0, S.outfitSlots - 1 do
        if not S.outfits[i] then
            StaticPopup_Show("MANASTORM_TMOG_SAVE")
            return
        end
    end
end

local function PreviewOutfit(id)
    local o = S.outfits[id]
    if not o then return end
    wipe(S.pending)
    for slot, entry in pairs(o.items) do
        if (S.equip[slot] or 0) > 0 and entry ~= (S.fake[slot] or 0) then S.pending[slot] = entry end
    end
    -- what the outfit does not cover is bare, so the preview is the outfit and nothing else
    for slot, fake in pairs(S.fake) do
        if fake > 0 and o.items[slot] == nil then S.pending[slot] = 0 end
    end
    S.outfitId = id
    RebuildModel()
    RefreshAll()
end

-- The outfit is whatever the preview currently shows for every slot that holds a transmogrifiable item.
function ManastormTransmog_SaveOutfitTo(id, name)
    local parts = {}
    for _, def in ipairs(SLOTS) do
        local slot = def.slot
        if (S.equip[slot] or 0) > 0 then
            local p = S.pending[slot]
            local entry
            if p ~= nil then entry = p else entry = S.fake[slot] or 0 end
            if entry > 0 then parts[#parts + 1] = slot .. "=" .. entry end
        end
    end
    if #parts == 0 then
        Say(MESSAGES[105], false)
        return
    end
    Send(format("OS:%d:%s:%s", id, table.concat(parts, ","), name))
end

-- Save Outfit: a selected outfit is overwritten; otherwise the first free slot takes it.
function ManastormTransmog_SaveOutfit(name)
    local id = S.outfitId
    if id == nil then
        for i = 0, S.outfitSlots - 1 do
            if not S.outfits[i] then id = i break end
        end
    end
    if id == nil then
        Say(MESSAGES[103], false)
        return
    end
    ManastormTransmog_SaveOutfitTo(id, name)
end

StaticPopupDialogs["MANASTORM_TMOG_SAVE"] = {
    text = "Name this outfit:",
    button1 = ACCEPT,
    button2 = CANCEL,
    hasEditBox = 1,
    maxLetters = 24,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    OnShow = function(self)
        self.editBox:SetText(S.outfitId and S.outfits[S.outfitId] and S.outfits[S.outfitId].name or "")
        self.editBox:HighlightText()
        self.editBox:SetFocus()
    end,
    OnAccept = function(self)
        local name = self.editBox:GetText()
        if name and name ~= "" then ManastormTransmog_SaveOutfit(name) end
    end,
    EditBoxOnEnterPressed = function(self)
        local parent = self:GetParent()
        local name = self:GetText()
        if name and name ~= "" then ManastormTransmog_SaveOutfit(name) end
        parent:Hide()
    end,
    EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
}

-- Changes not yet on the character (Apply Outfit would send them).
local function HasUnappliedChanges()
    for slot in pairs(S.pending) do
        if IsChanged(slot) then return true end
    end
    return false
end

-- Whether the preview holds work that would be lost: an edit of the selected outfit, or, with none selected,
-- changes that are not on the character and that no stored outfit holds. A plain reset (the look the character
-- wears) loses nothing.
local function HasEdits()
    local look = PreviewLook()
    local o = S.outfitId ~= nil and S.outfits[S.outfitId] or nil
    if o then return not OutfitMatches(o, look) end
    if not HasUnappliedChanges() then return false end
    for _, other in pairs(S.outfits) do
        if OutfitMatches(other, look) then return false end
    end
    return true
end

local function HasUnsavedWork()
    return HasEdits() or (not S.info.sets and HasUnappliedChanges())
end

StaticPopupDialogs["MANASTORM_TMOG_RENAME"] = {
    text = "Rename this outfit:",
    button1 = ACCEPT,
    button2 = CANCEL,
    hasEditBox = 1,
    maxLetters = 24,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    OnShow = function(self, data)
        data = data or self.data
        local o = data and S.outfits[tonumber(data)]
        self.editBox:SetText(o and o.name or "")
        self.editBox:HighlightText()
        self.editBox:SetFocus()
    end,
    OnAccept = function(self, data)
        data = data or self.data
        local name = self.editBox:GetText()
        if data and name and name ~= "" then Send(format("OR:%d:%s", tonumber(data), name)) end
    end,
    EditBoxOnEnterPressed = function(self)
        local parent = self:GetParent()
        local name = self:GetText()
        if parent.data and name and name ~= "" then Send(format("OR:%d:%s", tonumber(parent.data), name)) end
        parent:Hide()
    end,
    EditBoxOnEscapePressed = function(self) self:GetParent():Hide() end,
}

StaticPopupDialogs["MANASTORM_TMOG_CLOSE"] = {
    text = "You have changes that are not applied or saved. Closing the window will discard them.",
    button1 = "Close anyway",
    button2 = CANCEL,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    OnAccept = function()
        S.closing = true
        if win then win:Hide() end
        S.closing = false
    end,
}

-- picking an outfit while the preview holds a look that is not saved to an outfit: the choice is to drop it,
-- or to save it first (into the selected outfit, or a new one asked for by name) and then move on
StaticPopupDialogs["MANASTORM_TMOG_SWITCH"] = {
    text = "Your current look is not saved to an outfit. Changing outfit will discard it.",
    button1 = "Change outfit",
    button2 = CANCEL,
    button3 = "Save outfit",
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    OnAccept = function(self, data) PreviewOutfit(data) end,
    OnAlt = function(self, data)
        -- an outfit already selected is overwritten at once, so the move can follow; a new one needs a name first,
        -- and the window stays where it is until that is given
        data = data or (type(self) == "table" and self.data) or nil
        local overwrite = S.outfitId ~= nil and S.outfits[S.outfitId] ~= nil
        SaveOutfit()
        if overwrite and data then PreviewOutfit(data) end
    end,
}

StaticPopupDialogs["MANASTORM_TMOG_CLEAR"] = {
    text = "Remove every transmogrification from your equipment?",
    button1 = ACCEPT,
    button2 = CANCEL,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    OnAccept = function() Send("XA") end,
}

StaticPopupDialogs["MANASTORM_TMOG_DELETE"] = {
    text = "Delete the outfit \"%s\"?",
    button1 = ACCEPT,
    button2 = CANCEL,
    timeout = 0,
    whileDead = 1,
    hideOnEscape = 1,
    OnAccept = function(self, data)
        if S.outfitId == tonumber(data) then S.outfitId = nil end
        Send("OD:" .. data)
    end,
}

------------------------------------------------------------------------------------------------------
-- server messages
------------------------------------------------------------------------------------------------------

local function OnState(body)
    for _, chunk in ipairs(Split(body, ";")) do
        local f = Split(chunk, ",")
        local slot = tonumber(f[1])
        if slot then
            S.equip[slot] = tonumber(f[2]) or 0
            S.fake[slot] = tonumber(f[3]) or 0
            Prefetch(S.fake[slot]) -- so the character panel can name the appearance
            S.cost[slot] = tonumber(f[4]) or 0
            if S.pending[slot] ~= nil and not IsChanged(slot) then S.pending[slot] = nil end
        end
    end
    S.ready = true
end

local function Handle(msg)
    local kind, rest = msg:match("^(%u+)(.*)$")
    if not kind then return end

    if kind == "OPEN" then
        S.ready = true
        ManastormTransmog_Show()
        return
    end

    local body = rest:sub(2)
    if kind == "I" then
        local f = Split(body, ",")
        S.info.hidden = f[1] == "1"
        S.info.sets = f[2] == "1"
        S.info.maxSets = tonumber(f[3]) or 0
        S.maxSets = S.info.maxSets
        S.info.setMod = tonumber(f[4]) or 0
        S.info.setCopper = tonumber(f[5]) or 0
        S.info.hiddenFree = f[9] == "1"
    elseif kind == "E" then
        OnState(body)
        if win and win:IsShown() then
            RebuildModel()
            RefreshAll()
        end
    elseif kind == "L" then
        local f = Split(body, ":", 4)
        local slot, sp = tonumber(f[1]), tonumber(f[2])
        if slot == S.slot and sp then
            S.total = tonumber(f[3]) or 0
            S.loaded[sp] = true
            local n = 0
            for _, id in ipairs(Split(f[4] or "", ",")) do
                local entry = tonumber(id)
                if entry then
                    n = n + 1
                    S.items[sp * SERVER_PAGE + n] = entry
                end
            end
            if win then
                EnsureItems()
                RefreshGrid()
            end
        end
    elseif kind == "SB" then
        local f = Split(body, ":")
        if tonumber(f[1]) == S.nonce then
            S.setTotal = tonumber(f[2]) or 0
            if win then RefreshGrid() end
        end
    elseif kind == "T" then
        local f = Split(body, ":", 3)
        local index = tonumber(f[2])
        if tonumber(f[1]) == S.nonce and index then
            local g = Split(f[3] or "", "|")
            local id = tonumber(g[1])
            if id then
                S.sets[index + 1] = {
                    id = id, owned = tonumber(g[2]) or 0, total = tonumber(g[3]) or 0,
                    name = g[4] or "", pieces = ParsePieces(g[5]),
                }
                if win then RefreshGrid() end
            end
        end
    elseif kind == "R" then
        local f = Split(body, ":")
        local code = tonumber(f[2]) or 0
        local text = MESSAGES[code] or ("Transmog result " .. code)
        -- only failures are worth a line; a success shows in the window itself
        Say(SUCCESS[code] and "" or text, false)
        if not SUCCESS[code] and UIErrorsFrame then UIErrorsFrame:AddMessage(text, 1, 0.3, 0.3, 1) end
    elseif kind == "OB" then
        local f = Split(body, ":")
        S.maxSets = tonumber(f[1]) or S.maxSets
        S.outfitSlots = tonumber(f[2]) or S.maxSets
        S.slotCost = tonumber(f[3]) or S.slotCost
        wipe(S.outfits) -- the outfits follow one by one; the selected one stays selected meanwhile
        if win then RefreshAll() end
    elseif kind == "O" then
        local f = Split(body, ":", 3)
        local id = tonumber(f[1])
        if id then
            local items = {}
            for _, pair in ipairs(Split(f[2] or "", ",")) do
                local a, b = pair:match("^(%d+)=(%d+)$")
                if a then items[tonumber(a)] = tonumber(b) end
            end
            S.outfits[id] = { name = f[3] or ("Outfit " .. (id + 1)), items = items }
            if win then RefreshOutfits() RefreshButtons() end
        end
    end
end

------------------------------------------------------------------------------------------------------
-- building the window
------------------------------------------------------------------------------------------------------

local BACKDROP = {
    bgFile = "Interface\\Buttons\\WHITE8X8",
    edgeFile = "Interface\\Buttons\\WHITE8X8",
    edgeSize = 1,
    insets = { left = 1, right = 1, top = 1, bottom = 1 },
}

local function Panel(parent, x, y, w, h)
    local f = CreateFrame("Frame", nil, parent)
    f:SetPoint("TOPLEFT", parent, "TOPLEFT", P(x), -P(y))
    f:SetSize(P(w), P(h))
    f:SetBackdrop(BACKDROP)
    f:SetBackdropColor(0.07, 0.057, 0.047, 0.97)
    f:SetBackdropBorderColor(0.3, 0.23, 0.13, 1)
    return f
end

-- Just a name: the appearance shown in the slot (previewed or applied), else the item itself; "Hidden" when the
-- slot is hidden.
local function ShowSlotTooltip(b)
    GameTooltip:SetOwner(b, "ANCHOR_RIGHT")
    local slot = b.slot
    local entry = ShownEntry(slot)
    if (S.equip[slot] or 0) == 0 then
        GameTooltip:SetText(SLOT_BY_ID[slot].name, 0.6, 0.6, 0.6)
    elseif entry == HIDDEN then
        GameTooltip:SetText("Hidden", 0.8, 0.4, 1)
    else
        local name, _, quality = GetItemInfo(entry)
        if name then
            local r, g, bl = GetItemQualityColor(quality or 1)
            GameTooltip:SetText(name, r, g, bl)
        else
            Prefetch(entry)
            GameTooltip:SetText("...", 0.7, 0.7, 0.7)
        end
    end
    GameTooltip:Show()
end

-- Slot buttons are the size of the frame art (110 px at native, drawn at SLOT_SCALE); the icon sits in it.
local function BuildSlotButton(parent, def, x, y)
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(SP(110), SP(110))
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", P(x), -P(y))
    b.slot = def.slot
    local _, bg = GetInventorySlotInfo(def.inv)
    b.emptyTexture = bg or UNKNOWN_ICON

    b.icon = b:CreateTexture(nil, "ARTWORK")
    b.icon:SetPoint("CENTER", b, "CENTER", -PX() / 2, PX() / 2)
    b.frame = b:CreateTexture(nil, "OVERLAY")
    b.frame:SetPoint("CENTER")
    SlotArt(b.frame, "slotBronze")
    b.eye = b:CreateTexture(nil, "OVERLAY", nil, 1)
    b.eye:SetPoint("CENTER")
    SlotArt(b.eye, "eye")
    -- an empty slot is marked with the forbidden sign and cannot be picked
    b.no = b:CreateTexture(nil, "OVERLAY", nil, 1)
    b.no:SetPoint("CENTER", b, "CENTER", 0, -U / WIN_K) -- a screen pixel low
    Art(b.no, "no", SLOT_SCALE * 1.5)
    b.no:Hide()
    b.selected = b:CreateTexture(nil, "OVERLAY", nil, 2)
    b.selected:SetPoint("CENTER")
    SlotArt(b.selected, "slotSelected")
    b.pending = b:CreateTexture(nil, "OVERLAY", nil, 3)
    b.pending:SetPoint("CENTER", b, "CENTER", -PX() / 2, PX() / 2)
    b.pending:SetTexture(FLIP_PATH)
    FlipFrame(b.pending, 0)
    b.pending:SetSize(P(FLIP_SIZE) * SLOT_SCALE + PX(), P(FLIP_SIZE) * SLOT_SCALE + PX())
    b.pending:SetBlendMode("ADD")
    b.hl = b:CreateTexture(nil, "HIGHLIGHT")
    b.hl:SetPoint("CENTER")
    -- the purple corner frame of the atlas, tinted gold, lights the slot on mouse over
    SlotArt(b.hl, "slotPurple")
    b.hl:SetBlendMode("ADD")
    b.hl:SetVertexColor(1, 0.8, 0.3)

    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    b:SetScript("OnClick", function(self, button)
        if button == "RightButton" then
            if S.pending[self.slot] ~= nil then
                S.pending[self.slot] = nil
                RebuildModel()
                RefreshAll()
            end
        else
            SelectSlot(self.slot)
        end
    end)
    b:SetScript("OnEnter", ShowSlotTooltip)
    b:SetScript("OnLeave", function() GameTooltip:Hide() end)
    slotButtons[#slotButtons + 1] = b
    return b
end

-- A card cell is 212x278 px; the art is 222x286 and overlaps its neighbours by their empty glow margin.
local CELL_W = 212
-- the card is drawn smaller than its cell and centred in it, which leaves a gap between neighbours; the rows sit
-- so the gap between them is the one between the columns
local CARD_SCALE = 0.9
local CELL_H = math.floor(286 * CARD_SCALE + (CELL_W - 222 * CARD_SCALE) + 0.5)

local function BuildCard(index)
    local b = CreateFrame("Button", nil, win.grid)
    b:SetSize(P(CELL_W), P(CELL_H))
    local col, row = (index - 1) % L.cols, math.floor((index - 1) / L.cols)
    b:SetScale(CARD_SCALE)
    b:SetPoint("CENTER", win.grid, "TOPLEFT", P((col + 0.5) * CELL_W) / CARD_SCALE, -P((row + 0.5) * CELL_H) / CARD_SCALE)

    b.bg = b:CreateTexture(nil, "BACKGROUND")
    b.bg:SetPoint("CENTER")
    Art(b.bg, "cardBronze")
    -- on mouse over the base frame is lit again in a yellowish tone, over itself
    b.hoverFrame = b:CreateTexture(nil, "BACKGROUND", nil, 1)
    b.hoverFrame:SetPoint("CENTER")
    Art(b.hoverFrame, "cardBronze")
    b.hoverFrame:SetBlendMode("ADD")
    b.hoverFrame:SetVertexColor(1, 0.82, 0.35)
    b.hoverFrame:Hide()

    -- the preview area of the card: opaque black behind the model. Its own layer: textures of one layer are drawn in
    -- no fixed order (sorted by texture), so in ARTWORK it could land over the picture and leave the card black.
    b.field = b:CreateTexture(nil, "BORDER")
    b.field:SetSize(P(198), P(262))
    b.field:SetPoint("CENTER")
    b.field:SetTexture(0, 0, 0, 1)

    -- the picture of the item on the character; ModelSnapshot in the client makes it, see SHOT.PumpShots
    b.model = b:CreateTexture(nil, "ARTWORK", nil, 1)
    b.model:SetSize(P(198), P(262))
    b.model:SetPoint("CENTER")
    -- the picture is stored fully opaque, so normal blending is exact; DISABLE draws nothing at all in this client
    -- (a solid colour with it stayed invisible, /tmog probe), which left every card empty
    b.model:SetBlendMode("BLEND")
    b.model:Hide()

    -- shown while the picture is being made
    b.loading = b:CreateTexture(nil, "ARTWORK")
    b.loading:SetSize(P(40), P(40))
    b.loading:SetPoint("CENTER")
    b.loading:SetTexture("Interface\\AddOns\\Manastorm_Transmog\\Textures\\loading")
    b.loading:Hide()

    -- the hide card: a dark field with the eye, no model
    b.dim = b:CreateTexture(nil, "ARTWORK")
    b.dim:SetSize(P(198), P(262))
    b.dim:SetPoint("CENTER")
    b.dim:SetTexture(0.02, 0.018, 0.02, 0.9)
    b.eye = b:CreateTexture(nil, "OVERLAY")
    b.eye:SetPoint("CENTER")
    Art(b.eye, "eye")

    -- marks sit on their own frame so they draw over the model
    b.top = CreateFrame("Frame", nil, b)
    b.top:SetAllPoints()
    b.top:SetFrameLevel(b:GetFrameLevel() + 2)
    -- a set with nothing collected yet is greyed out under this veil
    b.grey = b.top:CreateTexture(nil, "BACKGROUND")
    b.grey:SetAllPoints(b.field)
    b.grey:SetTexture(0.05, 0.05, 0.05, 0.7)
    b.grey:Hide()
    -- a soft vignette: four edge gradients, darkest at the frame
    local VIG, VIG_A = 30, 0.85
    local function Vignette(orient, minA, maxA, p1, p2, w, h)
        local t = b.top:CreateTexture(nil, "ARTWORK")
        t:SetTexture(1, 1, 1, 1)
        t:SetGradientAlpha(orient, 0, 0, 0, minA, 0, 0, 0, maxA)
        -- remembered so SetCardBorder can tint it
        cardVignettes[b] = cardVignettes[b] or {}
        table.insert(cardVignettes[b], { tex = t, orient = orient, minA = minA, maxA = maxA })
        -- starts one pixel outside the field, over the frame's own inner edge
        local function out(p)
            return (p:find("LEFT") and -1 or p:find("RIGHT") and 1 or 0) * P(1),
                   (p:find("TOP") and 1 or p:find("BOTTOM") and -1 or 0) * P(1)
        end
        t:SetPoint(p1, b.field, p1, out(p1))
        t:SetPoint(p2, b.field, p2, out(p2))
        if w then t:SetWidth(P(w)) end
        if h then t:SetHeight(P(h)) end
    end
    Vignette("VERTICAL", 0, VIG_A, "TOPLEFT", "TOPRIGHT", nil, VIG)
    Vignette("VERTICAL", VIG_A, 0, "BOTTOMLEFT", "BOTTOMRIGHT", nil, VIG)
    Vignette("HORIZONTAL", VIG_A, 0, "TOPLEFT", "BOTTOMLEFT", VIG, nil)
    Vignette("HORIZONTAL", 0, VIG_A, "TOPRIGHT", "BOTTOMRIGHT", VIG, nil)

    b.applied = b.top:CreateTexture(nil, "OVERLAY")
    b.applied:SetPoint("TOPRIGHT", -P(14), -P(14))
    Art(b.applied, "check")

    -- set cards: the set's name and how many of its pieces the account owns, over the foot of the card
    b.setName = b.top:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    b.setName:SetPoint("BOTTOM", b, "BOTTOM", 0, P(40))
    b.setName:SetWidth(P(180))
    b.setName:Hide()
    b.setCount = b.top:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
    b.setCount:SetPoint("TOP", b.setName, "BOTTOM", 0, -P(2))
    b.setCount:Hide()

    b:EnableMouseWheel(true)
    b:SetScript("OnMouseWheel", function(_, delta) WheelPage(delta) end)
    b:SetScript("OnClick", function(self)
        if self.set then
            PreviewSet(self.set)
        elseif self.entry then
            SetPending(S.slot, self.entry)
        end
    end)
    b:SetScript("OnEnter", function(self)
        self.over = true
        SetCardBorder(self)
        if self.set then
            GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
            GameTooltip:SetText(self.set.name, 1, 0.82, 0)
            GameTooltip:AddLine(format("%d/%d pieces collected", self.set.owned, self.set.total), 0.8, 0.8, 0.8)
            for _, piece in ipairs(self.set.pieces) do
                local name = GetItemInfo(piece.entry)
                if not name then Prefetch(piece.entry) end
                if piece.owned then
                    GameTooltip:AddLine(name or "...", 1, 1, 1)
                else
                    GameTooltip:AddLine(name or "...", 0.5, 0.5, 0.5)
                end
            end
            GameTooltip:Show()
            return
        end
        if not self.entry then return end
        GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
        if self.entry == HIDDEN then
            GameTooltip:SetText("Hide slot", 1, 1, 1)
            GameTooltip:AddLine("Nothing is drawn in this slot.", 0.8, 0.8, 0.8)
        else
            -- only the item's name, in its quality colour
            local name, _, quality = GetItemInfo(self.entry)
            if name then
                local r, g, bl = GetItemQualityColor(quality or 1)
                GameTooltip:SetText(name, r, g, bl)
            else
                Prefetch(self.entry)
                GameTooltip:SetText("...", 0.7, 0.7, 0.7)
            end
        end
        GameTooltip:Show()
    end)
    b:SetScript("OnLeave", function(self)
        self.over = false
        SetCardBorder(self)
        GameTooltip:Hide()
    end)
    -- a hidden model loses its character, so the card is dressed again when it comes back
    cardButtons[index] = b
end

-- Nine-slice: `rect` is a piece of the texture `path` (aw x ah texels). Corners keep their shape and are
-- drawn `draw` pixels square; the edges and the middle stretch. Returns an object whose Set(path) swaps art.
local function NineSlice(frame, layer, path, aw, ah, rect, corner, draw)
    local x, y, w, h, c = rect[1], rect[2], rect[3], rect[4], corner
    local d = P(draw)
    local parts = {}

    local function new(px, py, pw, ph)
        local t = frame:CreateTexture(nil, layer)
        t:SetTexture(path)
        t:SetTexCoord(px / aw, (px + pw) / aw, py / ah, (py + ph) / ah)
        parts[#parts + 1] = { tex = t, coords = { px / aw, (px + pw) / aw, py / ah, (py + ph) / ah } }
        return t
    end

    local tl = new(x, y, c, c)
    tl:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, 0)
    tl:SetSize(d, d)
    local tr = new(x + w - c, y, c, c)
    tr:SetPoint("TOPRIGHT", frame, "TOPRIGHT", 0, 0)
    tr:SetSize(d, d)
    local bl = new(x, y + h - c, c, c)
    bl:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 0, 0)
    bl:SetSize(d, d)
    local br = new(x + w - c, y + h - c, c, c)
    br:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", 0, 0)
    br:SetSize(d, d)

    local top = new(x + c, y, w - 2 * c, c)
    top:SetPoint("TOPLEFT", frame, "TOPLEFT", d, 0)
    top:SetPoint("TOPRIGHT", frame, "TOPRIGHT", -d, 0)
    top:SetHeight(d)
    local bottom = new(x + c, y + h - c, w - 2 * c, c)
    bottom:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", d, 0)
    bottom:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -d, 0)
    bottom:SetHeight(d)
    local left = new(x, y + c, c, h - 2 * c)
    left:SetPoint("TOPLEFT", frame, "TOPLEFT", 0, -d)
    left:SetPoint("BOTTOMLEFT", frame, "BOTTOMLEFT", 0, d)
    left:SetWidth(d)
    local right = new(x + w - c, y + c, c, h - 2 * c)
    right:SetPoint("TOPRIGHT", frame, "TOPRIGHT", 0, -d)
    right:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", 0, d)
    right:SetWidth(d)
    local middle = new(x + c, y + c, w - 2 * c, h - 2 * c)
    middle:SetPoint("TOPLEFT", frame, "TOPLEFT", d, -d)
    middle:SetPoint("BOTTOMRIGHT", frame, "BOTTOMRIGHT", -d, d)

    local slice = {}
    function slice.SetShown(show)
        for _, part in ipairs(parts) do
            if show then part.tex:Show() else part.tex:Hide() end
        end
    end
    function slice.SetAlpha(alpha)
        for _, part in ipairs(parts) do part.tex:SetAlpha(alpha) end
    end
    function slice.SetBlend(mode)
        for _, part in ipairs(parts) do part.tex:SetBlendMode(mode) end
    end
    function slice.Set(newPath)
        for _, part in ipairs(parts) do
            part.tex:SetTexture(newPath)
            part.tex:SetTexCoord(unpack(part.coords))
        end
    end
    return slice
end

-- Every ordinary button and every outfit row is the list-button art: a dark bar, a lighter bar on hover, the
-- yellow-outlined bar when latched or pressed. Nine-sliced, so it stretches along its length at will.
-- The label lives on its own frame above the art, or the hover bar (added on top) would wash it out.
local BUTTON_EXTRA = 4 -- every button is a touch taller than the height it was asked for

local function MakeButton(parent, text, w, h, x, y, onClick)
    h = h + BUTTON_EXTRA
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(P(w), P(h))
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", P(x), -P(y))
    local corner = math.min(8, h * 0.3)
    b.skin = NineSlice(b, "BACKGROUND", LIST_ATLAS, 512, 512, LIST.dark, 12, corner)
    b.lit = NineSlice(b, "ARTWORK", LIST_ATLAS, 512, 512, LIST.selected, 12, corner)
    b.lit.SetShown(false)
    local hover = NineSlice(b, "HIGHLIGHT", LIST_ATLAS, 512, 512, LIST.light, 12, corner)
    hover.SetBlend("ADD")
    hover.SetAlpha(0.55)

    b.textFrame = CreateFrame("Frame", nil, b)
    b.textFrame:SetAllPoints()
    b.textFrame:SetFrameLevel(b:GetFrameLevel() + 3)
    b.label = b.textFrame:CreateFontString(nil, "OVERLAY", "GameFontNormal")
    b.label:SetPoint("CENTER", 0, 0)
    b.label:SetTextColor(1, 0.82, 0)
    b:SetScript("OnClick", onClick)

    -- the button grows to fit its text (never below the width it was asked for); the text keeps its size
    local minWidth = P(w)
    function b:SetLabel(str)
        self.label:SetText(str)
        self:SetWidth(math.max(minWidth, self.label:GetStringWidth() + P(28)))
    end
    b:SetLabel(text)

    local enable, disable = b.Enable, b.Disable
    local function refresh(self, pressed)
        self.lit.SetShown(self.active or pressed)
    end
    b.Enable = function(self)
        enable(self)
        self.off = false
        self.label:SetTextColor(1, 0.82, 0)
        self.skin.SetAlpha(1)
    end
    b.Disable = function(self)
        disable(self)
        self.off = true
        self.label:SetTextColor(0.45, 0.42, 0.38)
        self.skin.SetAlpha(0.45)
        self.lit.SetShown(false)
    end
    -- a latched button (a selected row)
    b.SetActive = function(self, on)
        self.active = on and true or false
        refresh(self, false)
    end
    b:SetScript("OnMouseDown", function(self)
        if not self.off then
            refresh(self, true)
            self.label:SetPoint("CENTER", 0, -1)
        end
    end)
    b:SetScript("OnMouseUp", function(self)
        if self.off then return end
        refresh(self, false)
        self.label:SetPoint("CENTER", 0, 0)
    end)
    return b
end

-- Right click on an outfit: rename or delete it.
local outfitMenu
local function ShowOutfitMenu(id, o)
    if not outfitMenu then
        outfitMenu = CreateFrame("Frame", "ManastormTransmogOutfitMenu", UIParent, "UIDropDownMenuTemplate")
    end
    local menu = {
        { text = o.name, isTitle = true, notCheckable = true },
        { text = "Rename", notCheckable = true, func = function() StaticPopup_Show("MANASTORM_TMOG_RENAME", nil, nil, id) end },
        { text = "Delete", notCheckable = true, func = function() StaticPopup_Show("MANASTORM_TMOG_DELETE", o.name, nil, id) end },
        { text = CANCEL, notCheckable = true },
    }
    EasyMenu(menu, outfitMenu, "cursor", 0, 0, "MENU")
    -- the menu's panel is made fully opaque while it is ours, and put back when it closes
    local backdrops = { DropDownList1MenuBackdrop, DropDownList1Backdrop }
    for _, bd in ipairs(backdrops) do
        if bd and bd.GetBackdropColor and not bd.tmogColors then
            bd.tmogColors = { bd:GetBackdropColor() }
            bd.tmogBorder = { bd:GetBackdropBorderColor() }
            bd:SetBackdropColor(bd.tmogColors[1], bd.tmogColors[2], bd.tmogColors[3], 1)
            bd:SetBackdropBorderColor(bd.tmogBorder[1], bd.tmogBorder[2], bd.tmogBorder[3], 1)
        end
    end
    -- the stock panel texture is itself part clear: a solid fill goes under the entries
    if DropDownList1 then
        if not DropDownList1.tmogFill then
            local fill = DropDownList1:CreateTexture(nil, "BACKGROUND")
            fill:SetPoint("TOPLEFT", DropDownList1, "TOPLEFT", 4, -4)
            fill:SetPoint("BOTTOMRIGHT", DropDownList1, "BOTTOMRIGHT", -4, 4)
            fill:SetTexture(0.07, 0.057, 0.047, 1)
            DropDownList1.tmogFill = fill
        end
        DropDownList1.tmogFill:Show()
    end
    if DropDownList1 and not DropDownList1.tmogHooked then
        DropDownList1.tmogHooked = true
        DropDownList1:HookScript("OnHide", function()
            if DropDownList1.tmogFill then DropDownList1.tmogFill:Hide() end
            for _, bd in ipairs({ DropDownList1MenuBackdrop, DropDownList1Backdrop }) do
                if bd and bd.tmogColors then
                    bd:SetBackdropColor(unpack(bd.tmogColors))
                    bd:SetBackdropBorderColor(unpack(bd.tmogBorder))
                    bd.tmogColors, bd.tmogBorder = nil, nil
                end
            end
        end)
    end
end

-- Outfit rows are the same button with the label on the left.
local function BuildOutfitButton(index, w, y)
    local b = MakeButton(win.outfitPanel, "", w, 60, 20, y, nil)
    b.label:ClearAllPoints()
    b.label:SetPoint("LEFT", P(16), 0)
    b.label:SetPoint("RIGHT", -P(44), 0)
    b.label:SetJustifyH("LEFT")
    b.label:SetTextColor(1, 1, 1)
    -- the active outfit (the one on the character now, selected or not) carries a check mark at its right end (the check of minimalcheckbox.blp, 64x64)
    b.check = b.textFrame:CreateTexture(nil, "OVERLAY")
    b.check:SetTexture("Interface\\AddOns\\Manastorm_Transmog\\Textures\\minimalcheckbox")
    -- the sheet has no mipmaps, so it is drawn texel for texel (one texture pixel per screen pixel, the window's
    -- own scale undone) and kept a texel inside its cell
    local px = U / WIN_K
    b.check:SetTexCoord(8.5 / 64, 29.5 / 64, 34.5 / 64, 52.5 / 64)
    b.check:SetSize(21 * px, 18 * px)
    b.check:SetPoint("RIGHT", b, "RIGHT", -12 * px, 0)
    b.check:Hide()
    local setActive = b.SetActive
    b.SetActive = function(self, on)
        setActive(self, on)
        if on then self.label:SetTextColor(1, 0.82, 0) else self.label:SetTextColor(1, 1, 1) end
    end
    b:RegisterForClicks("LeftButtonUp", "RightButtonUp")
    b:SetScript("OnClick", function(self, button)
        if not self.id then return end
        local o = S.outfits[self.id]
        if not o then
            -- an empty outfit is a blank sheet: the preview drops every transmog (nothing is applied)
            wipe(S.pending)
            for slot, fake in pairs(S.fake) do
                if fake > 0 then S.pending[slot] = 0 end
            end
            S.outfitId = self.id
            RebuildModel()
            RefreshAll()
            return
        end
        if button == "RightButton" then
            ShowOutfitMenu(self.id, o)
        elseif S.info.sets and HasEdits() then
            StaticPopup_Show("MANASTORM_TMOG_SWITCH", nil, nil, self.id)
        else
            PreviewOutfit(self.id)
        end
    end)
    outfitButtons[index] = b
end

-- The big call-to-action: the glue panel button art, nine-sliced.
local function GlueButton(parent, text, w, h, x, y, onClick)
    h = h + BUTTON_EXTRA
    local b = CreateFrame("Button", nil, parent)
    b:SetSize(P(w), P(h))
    b:SetPoint("TOPLEFT", parent, "TOPLEFT", P(x), -P(y))
    b.skin = NineSlice(b, "BACKGROUND", GLUE_DIR .. "up", 256, 64, GLUE, 14, 10)
    local rim = NineSlice(b, "HIGHLIGHT", GLUE_DIR .. "highlight", 256, 64, GLUE, 14, 10)
    rim.SetBlend("ADD")
    b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
    b.label:SetPoint("CENTER", 0, P(2)) -- the art's shadow is at its foot, so the text sits above centre
    b.label:SetText(text)
    -- a greyed-out button never reacts, whatever delivers the click
    b:SetScript("OnClick", function(self, ...)
        if not self.off and onClick then onClick(self, ...) end
    end)

    local enable, disable = b.Enable, b.Disable
    -- our own flag: a greyed-out button must not press, whatever the client reports for IsEnabled
    b.Enable = function(self)
        enable(self)
        self.off = false
        self.label:SetTextColor(1, 0.82, 0)
        self.skin.Set(GLUE_DIR .. "up")
    end
    b.Disable = function(self)
        disable(self)
        self.off = true
        self.label:SetTextColor(0.5, 0.5, 0.5)
        self.skin.Set(GLUE_DIR .. "disabled")
    end
    b:SetScript("OnMouseDown", function(self)
        if not self.off then
            self.skin.Set(GLUE_DIR .. "down")
            self.label:SetPoint("CENTER", 0, P(2) - 1)
        end
    end)
    b:SetScript("OnMouseUp", function(self)
        if self.off then return end
        self.skin.Set(GLUE_DIR .. "up")
        self.label:SetPoint("CENTER", 0, P(2))
    end)
    b.label:SetTextColor(1, 0.82, 0)
    return b
end

local function BuildStage(x, y, w, h)
    stage = CreateFrame("Frame", nil, win)
    stage:SetPoint("TOPLEFT", win, "TOPLEFT", P(x), -P(y))
    stage:SetSize(P(w), P(h))

    -- the big dark backdrop of the atlas (0,0 to 1264x1620), cut to the stage's shape so it is never stretched
    local bgW, bgH = 1264, 1620
    local cw, ch = bgW, bgW * h / w
    if ch > bgH then cw, ch = bgH * w / h, bgH end
    local fallback = stage:CreateTexture(nil, "BACKGROUND")
    fallback:SetAllPoints()
    fallback:SetTexture(0.03, 0.024, 0.02, 1)
    local backdrop = stage:CreateTexture(nil, "BORDER")
    backdrop:SetPoint("TOPLEFT", 0, 0)
    backdrop:SetPoint("BOTTOMRIGHT", 0, 0)
    backdrop:SetTexture(ATLAS)
    SetCoords(backdrop, 0, 0, cw, ch)

    model = CreateFrame("DressUpModel", "ManastormTransmogModel", stage)
    -- out to the stage's corners, a screen pixel short of its top edge
    model:SetPoint("TOPLEFT", stage, "TOPLEFT", 0, -U / WIN_K)
    model:SetPoint("BOTTOMRIGHT", stage, "BOTTOMRIGHT", 0, 0)
    model:EnableMouse(true)
    model:EnableMouseWheel(true)

    -- left drag turns the model, right drag moves it, the wheel zooms
    model:SetScript("OnMouseDown", function(self, button)
        self.dragX, self.dragY = GetCursorPosition()
        self.dragButton = button
    end)
    model:SetScript("OnMouseUp", function(self) self.dragX, self.dragY = nil, nil end)
    model:SetScript("OnHide", function(self) self.dragX, self.dragY = nil, nil end)
    model:SetScript("OnMouseWheel", function(_, delta) ZoomBy(delta) end)
    model:SetScript("OnUpdate", function(self)
        if not self.dragX then return end
        local cx, cy = GetCursorPosition()
        local dx, dy = cx - self.dragX, cy - self.dragY
        self.dragX, self.dragY = cx, cy
        if self.dragButton == "RightButton" then
            V.panH = V.panH + dx * PAN_SPEED
            V.panV = V.panV + dy * PAN_SPEED
            ClampPan()
        else
            V.rot = V.rot + dx * ROTATE_SPEED
        end
        ApplyView()
    end)

    -- everything that sits over the model lives on one frame above it
    stage.over = CreateFrame("Frame", nil, stage)
    stage.over:SetAllPoints()
    stage.over:SetFrameLevel(stage:GetFrameLevel() + 12)

    -- the 1px outline is drawn over the texture, on the stage's own edge: the texture fills the whole frame
    local function Edge(p1, p2, ew, eh)
        local t = stage.over:CreateTexture(nil, "BORDER")
        t:SetTexture(0.1, 0.08, 0.06, 1)
        t:SetPoint(p1, stage, p1, 0, 0)
        t:SetPoint(p2, stage, p2, 0, 0)
        if ew then t:SetWidth(ew) end
        if eh then t:SetHeight(eh) end
    end
    Edge("BOTTOMLEFT", "BOTTOMRIGHT", nil, 1)
    Edge("TOPLEFT", "BOTTOMLEFT", 1, nil)
    Edge("TOPRIGHT", "BOTTOMRIGHT", 1, nil)

    -- a subtle vignette: four edge gradients over the model, under the slots and controls
    local VIG, VIG_A = 60, 0.7
    local function Vignette(orient, minA, maxA, p1, p2, vw, vh)
        local t = stage.over:CreateTexture(nil, "BACKGROUND")
        t:SetTexture(0, 0, 0, 1)
        t:SetGradientAlpha(orient, 0, 0, 0, minA, 0, 0, 0, maxA)
        t:SetPoint(p1, stage, p1, 0, 0)
        t:SetPoint(p2, stage, p2, 0, 0)
        if vw then t:SetWidth(P(vw)) end
        if vh then t:SetHeight(P(vh)) end
    end
    Vignette("VERTICAL", 0, VIG_A, "TOPLEFT", "TOPRIGHT", nil, VIG)
    Vignette("VERTICAL", VIG_A, 0, "BOTTOMLEFT", "BOTTOMRIGHT", nil, VIG)
    Vignette("HORIZONTAL", VIG_A, 0, "TOPLEFT", "BOTTOMLEFT", VIG, nil)
    Vignette("HORIZONTAL", 0, VIG_A, "TOPRIGHT", "BOTTOMRIGHT", VIG, nil)

    -- zoom controls: only while the cursor is over the stage
    stage.zoom = CreateFrame("Frame", nil, stage.over)
    stage.zoom:SetSize(P(84), P(30 + BUTTON_EXTRA))
    stage.zoom:SetPoint("TOP", stage.over, "TOP", 0, -P(26))
    -- a button is as wide as its label needs (never below 26), so they are chained, not placed on a fixed pitch
    local prev, total, gap = nil, 0, P(4)
    for _, def in ipairs({ { "-", function() ZoomBy(-1) end }, { "+", function() ZoomBy(1) end }, { "R", ResetView } }) do
        local btn = MakeButton(stage.zoom, def[1], 26, 30, 0, 0, def[2])
        btn.label:SetFontObject(GameFontNormalSmall)
        btn:SetLabel(def[1]) -- sized again for the smaller font
        btn:ClearAllPoints()
        if prev then
            btn:SetPoint("LEFT", prev, "RIGHT", gap, 0)
            total = total + gap
        else
            btn:SetPoint("LEFT", stage.zoom, "LEFT", 0, 0)
        end
        total = total + btn:GetWidth()
        prev = btn
    end
    stage.zoom:SetWidth(total)
    stage.zoom:SetAlpha(0)
    stage.zoom:EnableMouse(false)
    stage:SetScript("OnUpdate", function(self, dt)
        local target = MouseIsOver(self) and 1 or 0
        local a = self.zoom:GetAlpha()
        if a ~= target then
            a = a + (target - a) * math.min(1, dt * 12)
            if math.abs(a - target) < 0.02 then a = target end
            self.zoom:SetAlpha(a)
        end
    end)
end

local function BuildOutfits(x, y, w, h)
    -- Clear Transmogs: the big button on top of the list
    win.clear = MakeButton(win, "Clear Transmogs", w - 28, 48, x + 14, y, function()
        StaticPopup_Show("MANASTORM_TMOG_CLEAR")
    end)
    local listH = h - 128
    win.outfitPanel = CreateFrame("Frame", nil, win)
    win.outfitPanel:SetPoint("TOPLEFT", win, "TOPLEFT", P(x), -P(y + 74))
    win.outfitPanel:SetSize(P(w), P(listH))
    NineSlice(win.outfitPanel, "OVERLAY", ATLAS, ATLAS_SIZE, ATLAS_SIZE, RECT.stageFrame, 48, 28)

    -- rows are 64 px tall, so only as many as fit the column are made
    local rowCount = math.min(10, math.floor((listH - 40) / 70))
    for i = 1, rowCount do BuildOutfitButton(i, w - 40, 22 + (i - 1) * 70) end
    win.noOutfits = win.outfitPanel:CreateFontString(nil, "ARTWORK", "GameFontDisable")
    win.noOutfits:SetPoint("TOP", 0, -P(30))
    win.noOutfits:SetWidth(P(w - 50))
    win.noOutfits:SetText("Outfits are disabled.")

    local by = y + 74 + listH + 10
    win.buySlot = MakeButton(win, "Buy Slot", w - 28, 36, x + 14, by, function()
        Send("OP")
    end)
end

local GRID_Y = 88 -- the cards start under the header row

-- Tabs from uiframetabs.blp (64x256): a left cap, a stretching middle and a right cap, in a lit (selected) and a
-- dark version. The art has its rounded corners at the foot, so it is drawn upside down to stand on the panel.
local TABS_PATH = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\uiframetabs"
local TAB_ART = {
    on  = { left = { 2, 126, 35, 41 }, mid = { 8, 0, 48, 41 },  right = { 0, 82, 35, 41 } },
    off = { left = { 3, 208, 34, 35 }, mid = { 8, 44, 48, 35 }, right = { 0, 170, 36, 35 } },
}
local TAB_W, TAB_GAP, TAB_LEFT = 96, 4, 21 -- in screen pixels: the tabs are drawn texel for texel
local TAB_H = 48 -- the room above the panel: the lit tab (41 screen pixels, drawn texel for texel) less what dips into the frame

local function TabPiece(tab, state, part)
    local r = TAB_ART[state][part]
    local t = tab:CreateTexture(nil, "BACKGROUND")
    t:SetTexture(TABS_PATH)
    -- flipped top to bottom
    t:SetTexCoord(r[1] / 64, (r[1] + r[3]) / 64, (r[2] + r[4]) / 256, r[2] / 256)
    t.rect = r
    return t
end

local function TabButton(panel, text, index, onClick)
    local b = CreateFrame("Button", nil, win)
    local tx = U / WIN_K -- one screen pixel: the art is drawn at its own size
    b:SetSize(TAB_W * tx, 41 * tx)
    -- the frame's lit edge lies 11 px inside the panel's border box (its art has a soft shadow outside the line)
    b:SetPoint("BOTTOMLEFT", panel, "TOPLEFT", (TAB_LEFT + index * (TAB_W + TAB_GAP)) * tx, -P(11))
    b.pieces = { on = {}, off = {}, glow = {} } -- glow: the dark art again, added on top, while hovered
    for state, set in pairs(b.pieces) do
        local art = state == "glow" and "off" or state
        local h = TAB_ART[art].mid[4]
        local lift = art == "off" and -1 * tx or 0 -- the dark tab stands a pixel lower than the lit one
        for _, part in ipairs({ "left", "mid", "right" }) do
            local t = TabPiece(b, art, part)
            if state == "glow" then
                t:SetDrawLayer("BORDER")
                t:SetBlendMode("ADD")
                t:SetAlpha(0.3)
                t:Hide()
            end
            local r = t.rect
            t:SetHeight(h * tx)
            if part == "left" then
                t:SetPoint("BOTTOMLEFT", b, "BOTTOMLEFT", 0, lift)
                t:SetWidth(r[3] * tx)
            elseif part == "right" then
                t:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", 0, lift)
                t:SetWidth(r[3] * tx)
            else
                t:SetPoint("BOTTOMLEFT", b, "BOTTOMLEFT", TAB_ART[art].left[3] * tx, lift)
                t:SetPoint("BOTTOMRIGHT", b, "BOTTOMRIGHT", -TAB_ART[art].right[3] * tx, lift)
            end
            set[#set + 1] = t
        end
    end
    b.label = b:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    b.label:SetText(text)
    -- the label sits on a whole pixel, centred on the lit body of the art (its left edge is 2 texels in, its
    -- right edge has a 3 texel shadow); the lit tab's text is a pixel higher
    local function PlaceLabel(on)
        local textPx = floor(b.label:GetStringWidth() / tx + 0.5)
        local x = 2 + floor((TAB_W - 5 - textPx) / 2)
        b.label:ClearAllPoints()
        b.label:SetPoint("LEFT", b, "LEFT", x * tx, -P(2) + (on and 0 or -3 * tx))
    end
    b:SetScript("OnClick", onClick)
    -- an inactive tab brightens a little under the cursor
    local function Glow(on)
        for _, t in ipairs(b.pieces.glow) do Toggle(t, on) end
    end
    b:SetScript("OnEnter", function(self)
        if not self.active then
            Glow(true)
            self.label:SetTextColor(1, 0.92, 0.55)
        end
    end)
    b:SetScript("OnLeave", function(self)
        Glow(false)
        self:SetActive(self.active)
    end)
    function b:SetActive(on)
        self.active = on and true or false
        PlaceLabel(self.active)
        if self.active then Glow(false) end -- clicked while hovered: the lit art takes over
        for state, set in pairs(self.pieces) do
            if state ~= "glow" then
                for _, t in ipairs(set) do Toggle(t, (state == "on") == self.active) end
            end
        end
        if self.active then self.label:SetTextColor(1, 0.95, 0.6) else self.label:SetTextColor(1, 0.82, 0) end
    end
    b:SetActive(false)
    return b
end

local function BuildCollection(x, y, w, h)
    local panel = Panel(win, x, y, w, h)
    -- the same framed panel as the outfit column: the frame only, no background texture
    panel:SetBackdrop(nil)
    local brown = panel:CreateTexture(nil, "BACKGROUND")
    brown:SetPoint("TOPLEFT", P(8), -P(8))
    brown:SetPoint("BOTTOMRIGHT", -P(8), P(8))
    brown:SetTexture(0.1, 0.065, 0.04, 1)
    NineSlice(panel, "OVERLAY", ATLAS, ATLAS_SIZE, ATLAS_SIZE, RECT.stageFrame, 48, 28)

    win.slotTitle = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    win.slotTitle:SetPoint("TOPLEFT", P(44), -P(38))

    -- the two views of the collection: tabs standing on the panel's top edge
    win.tabItems = TabButton(panel, "Items", 0, function() SelectTab("items") end)
    win.tabSets = TabButton(panel, "Sets", 1, function() SelectTab("sets") end)

    win.showEquipped = MakeButton(panel, "Show Equipped Gear", 110, 26, w - 198, 34, function()
        SetPending(S.slot, 0)
    end)
    local face, size = win.showEquipped.label:GetFont()
    win.showEquipped.label:SetFont(face, size - 2)
    win.showEquipped:SetLabel("Show Equipped Gear")

    local search = CreateFrame("EditBox", "ManastormTransmogSearch", panel, "InputBoxTemplate")
    -- on the header row at the right end, as tall as Show Equipped Gear; that button sits to its left
    search:SetSize(P(190), win.showEquipped:GetHeight())
    search:SetPoint("TOPRIGHT", panel, "TOPRIGHT", -P(48), -P(32))
    win.showEquipped:ClearAllPoints()
    win.showEquipped:SetPoint("RIGHT", search, "LEFT", -P(20), 0)
    win.search = search
    search:SetAutoFocus(false)
    -- a dim "Search" while the box is empty and unfocused
    local hint = search:CreateFontString(nil, "OVERLAY", "GameFontDisable")
    hint:SetPoint("LEFT", search, "LEFT", P(12), 0)
    hint:SetText("Search")
    local function UpdateHint(self)
        Toggle(hint, (self:GetText() or "") == "" and not self:HasFocus())
    end
    search:SetScript("OnEditFocusGained", function(self) self:HighlightText() UpdateHint(self) end)
    search:SetScript("OnEditFocusLost", function(self) self:HighlightText(0, 0) UpdateHint(self) end)
    UpdateHint(search)
    search:SetMaxLetters(40)
    search:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    search:SetScript("OnEnterPressed", function(self) self:ClearFocus() end)
    search:SetScript("OnTextChanged", function(self, user)
        UpdateHint(self)
        if user then self.elapsed = 0 end
    end)
    search:SetScript("OnUpdate", function(self, dt)
        if not self.elapsed then return end
        self.elapsed = self.elapsed + dt
        if self.elapsed > 0.35 then
            self.elapsed = nil
            local text = (self:GetText() or ""):gsub("[:,|]", "")
            if text ~= S.search then
                S.search = text
                RequestList()
            end
        end
    end)

    -- the wheel turns the pages: up for the previous one
    panel:EnableMouseWheel(true)
    panel:SetScript("OnMouseWheel", function(_, delta) WheelPage(delta) end)

    win.grid = CreateFrame("Frame", nil, panel)
    win.grid:SetPoint("TOPLEFT", P(36), -P(GRID_Y))
    win.grid:SetSize(P(L.cols * CELL_W), P(L.rows * CELL_H))
    for i = 1, L.perPage do BuildCard(i) end

    win.empty = panel:CreateFontString(nil, "ARTWORK", "GameFontDisable")
    win.empty:SetPoint("CENTER", win.grid, "CENTER", 0, 0)
    win.empty:SetWidth(P(400))

    win.pageText = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
    win.pageText:SetPoint("CENTER", panel, "BOTTOM", -P(40), P(48))
    win.prev = CreateFrame("Button", nil, panel)
    win.prev:SetSize(P(44), P(44))
    win.prev:SetPoint("LEFT", win.pageText, "RIGHT", P(14), 0)
    win.prev:SetNormalTexture("Interface\\Buttons\\UI-SpellbookIcon-PrevPage-Up")
    win.prev:SetPushedTexture("Interface\\Buttons\\UI-SpellbookIcon-PrevPage-Down")
    win.prev:SetDisabledTexture("Interface\\Buttons\\UI-SpellbookIcon-PrevPage-Disabled")
    win.prev:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
    win.prev:SetScript("OnClick", function()
        if S.page > 0 then GoToPage(S.page - 1) end
    end)
    win.next = CreateFrame("Button", nil, panel)
    win.next:SetSize(P(44), P(44))
    win.next:SetPoint("LEFT", win.prev, "RIGHT", P(2), 0)
    win.next:SetNormalTexture("Interface\\Buttons\\UI-SpellbookIcon-NextPage-Up")
    win.next:SetPushedTexture("Interface\\Buttons\\UI-SpellbookIcon-NextPage-Down")
    win.next:SetDisabledTexture("Interface\\Buttons\\UI-SpellbookIcon-NextPage-Disabled")
    win.next:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight", "ADD")
    win.next:SetScript("OnClick", function()
        if (S.page + 1) * L.perPage < VirtualTotal() then GoToPage(S.page + 1) end
    end)
end

-- minimalcheckbox.blp (64x64) and ui-gearmanager-undo.blp (64x64), at their own size
local CB_PATH = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\minimalcheckbox"
local UNDO_PATH = "Interface\\AddOns\\Manastorm_Transmog\\Textures\\ui-gearmanager-undo"
local CB = { box = { 4, 4, 24, 24 }, check = { 8, 34, 22, 19 }, hover = { 40, 3, 22, 19 } }

local CB_SCALE = 1.5 -- the checkbox is drawn larger than its 24 px art

local function CbArt(tex, r)
    tex:SetTexture(CB_PATH)
    tex:SetTexCoord(r[1] / 64, (r[1] + r[3]) / 64, r[2] / 64, (r[2] + r[4]) / 64)
    tex:SetSize(P(r[3] * CB_SCALE), P(r[4] * CB_SCALE))
end

-- Sheathe Weapon at the top left of the preview, Reset (the undo arrow) at the top right.
local function BuildStageControls(over, stageW)
    local box = CreateFrame("Button", nil, over)
    box:SetSize(P(190), P(36))
    box:SetPoint("TOPLEFT", over, "TOPLEFT", P(30), -P(26))
    box.box = box:CreateTexture(nil, "ARTWORK")
    box.box:SetPoint("LEFT", 0, 0)
    CbArt(box.box, CB.box)
    box.check = box:CreateTexture(nil, "OVERLAY")
    box.check:SetPoint("CENTER", box.box, "CENTER", 0, 0)
    CbArt(box.check, CB.check)
    box.check:Hide()
    box.hover = box:CreateTexture(nil, "HIGHLIGHT")
    box.hover:SetPoint("CENTER", box.box, "CENTER", 0, 0)
    CbArt(box.hover, CB.hover)
    box.hover:SetAlpha(0.7)
    box.text = box:CreateFontString(nil, "OVERLAY", "GameFontNormalSmall")
    box.text:SetPoint("LEFT", box.box, "RIGHT", P(10), 0)
    box.text:SetText("Sheathe Weapon")
    box:SetScript("OnClick", function(self)
        S.sheathe = not S.sheathe
        Toggle(self.check, S.sheathe)
        RebuildModel()
    end)
    win.sheathe = box

    local undo = CreateFrame("Button", nil, over)
    undo:SetSize(P(44), P(44))
    undo:SetPoint("TOPRIGHT", over, "TOPRIGHT", -P(30), -P(22))
    undo.icon = undo:CreateTexture(nil, "ARTWORK")
    undo.icon:SetAllPoints()
    undo.icon:SetTexture(UNDO_PATH)
    undo.hl = undo:CreateTexture(nil, "HIGHLIGHT")
    undo.hl:SetAllPoints()
    undo.hl:SetTexture("Interface\\Buttons\\ButtonHilight-Square")
    undo.hl:SetBlendMode("ADD")
    undo:SetScript("OnClick", ResetPreview)
    undo:SetScript("OnEnter", function(self)
        GameTooltip:SetOwner(self, "ANCHOR_LEFT")
        GameTooltip:SetText("Reset", 1, 1, 1)
        GameTooltip:AddLine("Drop every previewed change.", 0.8, 0.8, 0.8)
        GameTooltip:Show()
    end)
    undo:SetScript("OnLeave", function() GameTooltip:Hide() end)
    local enable, disable = undo.Enable, undo.Disable
    undo.Enable = function(self) enable(self) self.icon:SetAlpha(1) end
    undo.Disable = function(self) disable(self) self.icon:SetAlpha(0.35) end
    win.reset = undo
end

-- Slots lie over the stage: the long column down the left, the short one down the right, weapons along the bottom.
local function BuildSlots()
    local over, gap = stage.over, 3
    local size = 110 * SLOT_SCALE
    local step = size + gap
    local left = { 0, 2, 14, 4, 3, 18, 8 }
    local right = { 9, 5, 6, 7 }
    local stageW = stage:GetWidth() / U
    local stageH = stage:GetHeight() / U
    local leftTop = (stageH - (#left * step - gap)) / 2
    local rightTop = (stageH - (#right * step - gap)) / 2
    for i, slot in ipairs(left) do BuildSlotButton(over, SLOT_BY_ID[slot], 26, leftTop + (i - 1) * step) end
    for i, slot in ipairs(right) do BuildSlotButton(over, SLOT_BY_ID[slot], stageW - 26 - size, rightTop + (i - 1) * step) end
    -- the weapons sit along the foot of the stage
    local weapons = { 15, 16, 17 }
    local startX = (stageW - (3 * size + 2 * gap)) / 2
    for i, slot in ipairs(weapons) do
        BuildSlotButton(over, SLOT_BY_ID[slot], startX + (i - 1) * step, stageH - 28 - size)
    end
    BuildStageControls(over, stageW)
end

------------------------------------------------------------------------------------------------------
-- the frame pad: drag where the model sits in a card, one slot at a time
------------------------------------------------------------------------------------------------------

-- The framing after its up and left: zoom, and the turns about Z (rot), Y (pitch) and X (roll), where they are set.
local function NudgeTail(n)
    return (n.zoom and format(", zoom = %.3f", n.zoom) or "") .. (n.rot and format(", rot = %.3f", n.rot) or "") ..
        (n.pitch and format(", pitch = %.3f", n.pitch) or "") .. (n.roll and format(", roll = %.3f", n.roll) or "")
end

-- The live placement values, written as the tables to paste over CARD_NUDGE_SEED and SET_NUDGE.
local function NudgeText()
    local name = nudgeFemale and "CARD_NUDGE_SEED_F" or "CARD_NUDGE_SEED_M"
    local out = { "local " .. name .. " = {" }
    for _, s in ipairs(SLOT_NUDGE_ORDER) do
        local n = CARD_NUDGE[s] or {}
        out[#out + 1] = format("    [%d] = { up = %+.3f, left = %+.3f%s },", s, n.up or 0, n.left or 0, NudgeTail(n))
    end
    -- weapon kinds framed apart from their slot ("slot:class.subclass", see SHOT.TypeKey), left out while they equal it
    local kinds = {}
    for key, n in pairs(CARD_NUDGE) do
        local base = type(key) == "string" and CARD_NUDGE[tonumber(key:match("^(%d+):"))]
        if base and (n.up ~= base.up or n.left ~= base.left or n.zoom ~= base.zoom or n.rot ~= base.rot or n.pitch ~= base.pitch or n.roll ~= base.roll) then
            kinds[#kinds + 1] = key
        end
    end
    table.sort(kinds)
    for _, key in ipairs(kinds) do
        local n = CARD_NUDGE[key]
        out[#out + 1] = format("    [\"%s\"] = { up = %+.3f, left = %+.3f%s },", key, n.up or 0, n.left or 0, NudgeTail(n))
    end
    out[#out + 1] = "}"
    out[#out + 1] = format("local SET_NUDGE_SEED%s = { up = %+.3f, left = %+.3f%s }", nudgeFemale and "_F" or "_M", SET_NUDGE.up or 0, SET_NUDGE.left or 0, NudgeTail(SET_NUDGE))
    return table.concat(out, "\n")
end

-- The pad's colours: the window's own browns and golds.
local PAD_GOLD = { 1, 0.82, 0 }
local PAD_DIM = { 0.72, 0.66, 0.54 }

-- A scrollable text box in a frame, with a scroll bar that follows it. `parent` gets the frame; returns the frame, whose
-- .edit is the EditBox (select-all on click, so the text can be copied with Ctrl+C; 3.3.5a has no clipboard call).
local function TextWell(parent, x, y, w, h)
    local well = Panel(parent, x, y, w, h)
    well:SetBackdropColor(0.03, 0.026, 0.022, 1)
    local BAR = 12
    local scroll = CreateFrame("ScrollFrame", nil, well)
    scroll:SetPoint("TOPLEFT", well, "TOPLEFT", P(8), -P(8))
    scroll:SetPoint("BOTTOMRIGHT", well, "BOTTOMRIGHT", -P(10 + BAR), P(8))
    local edit = CreateFrame("EditBox", nil, scroll)
    edit:SetMultiLine(true)
    edit:SetAutoFocus(false)
    edit:SetFont(STANDARD_TEXT_FONT, 12)
    edit:SetTextColor(0.95, 0.9, 0.8)
    edit:SetWidth(P(w - 28 - BAR))
    edit:SetHeight(P(h))
    scroll:SetScrollChild(edit)
    well.edit, well.scroll = edit, scroll

    local bar = CreateFrame("Slider", nil, well)
    bar:SetOrientation("VERTICAL")
    bar:SetPoint("TOPRIGHT", well, "TOPRIGHT", -P(6), -P(8))
    bar:SetPoint("BOTTOMRIGHT", well, "BOTTOMRIGHT", -P(6), P(8))
    bar:SetWidth(P(BAR))
    bar:SetMinMaxValues(0, 1)
    bar:SetValueStep(1)
    local rail = bar:CreateTexture(nil, "BACKGROUND")
    rail:SetAllPoints()
    rail:SetTexture(0.12, 0.1, 0.08, 1)
    local thumb = bar:CreateTexture(nil, "OVERLAY")
    thumb:SetTexture(0.62, 0.5, 0.28, 1)
    thumb:SetSize(P(BAR), P(40))
    bar:SetThumbTexture(thumb)
    well.bar = bar

    -- the bar and the text follow each other; `quiet` stops one echoing the other's change back
    function well.Sync()
        local range = scroll:GetVerticalScrollRange()
        if range < 1 then
            bar:Hide()
            return
        end
        local view = scroll:GetHeight()
        thumb:SetHeight(Clamp(bar:GetHeight() * view / (view + range), 24, bar:GetHeight()))
        bar.quiet = true
        bar:SetMinMaxValues(0, range)
        bar:SetValue(scroll:GetVerticalScroll())
        bar.quiet = nil
        bar:Show()
    end
    scroll:SetScript("OnScrollRangeChanged", function() well.Sync() end)
    scroll:SetScript("OnVerticalScroll", function(_, offset)
        bar.quiet = true
        bar:SetValue(offset)
        bar.quiet = nil
    end)
    bar:SetScript("OnValueChanged", function(self, v)
        if not self.quiet then scroll:SetVerticalScroll(v) end
    end)
    local function Wheel(_, delta)
        scroll:SetVerticalScroll(Clamp(scroll:GetVerticalScroll() - delta * 42, 0, scroll:GetVerticalScrollRange()))
    end
    scroll:EnableMouseWheel(true)
    scroll:SetScript("OnMouseWheel", Wheel)
    bar:EnableMouseWheel(true)
    bar:SetScript("OnMouseWheel", Wheel)

    edit:SetScript("OnEditFocusGained", function(self) self:HighlightText() end)
    edit:SetScript("OnEscapePressed", function(self) self:ClearFocus() end)
    -- the whole box is a click target, not just the lines of text
    scroll:EnableMouse(true)
    scroll:SetScript("OnMouseDown", function() edit:SetFocus() end)

    function well.SetText(self, text)
        edit:SetText(text)
        -- a line longer than the box wraps, so each one counts for the rows it fills
        local count = 0
        for line in (text .. "\n"):gmatch("([^\n]*)\n") do count = count + 1 + math.floor(#line / 66) end
        edit:SetHeight(math.max(scroll:GetHeight(), P((count + 2) * 15)))
        scroll:SetVerticalScroll(0)
        self.Sync()
    end
    return well
end

local function BuildFramePad()
    if framePad then return end
    -- everything below is in layout pixels, from the pad's top left
    local PAD_W, PAD_H = 478, 392
    local GX, GY, GW, GH = 16, 68, 224, 224 -- the grid: what a card's frame covers
    local CX, CW = 262, 200                 -- the column of sliders
    -- the grid's middle is nudge 0 and each edge is half of this (1.5 either way), in shares of the preview frame
    local SPAN = 3.0

    local pad = CreateFrame("Frame", "ManastormTransmogFramePad", win)
    framePad = pad
    pad:SetSize(P(PAD_W), P(PAD_H))
    pad:SetPoint("BOTTOMRIGHT", win, "BOTTOMRIGHT", -P(12), P(86)) -- clear of the window's bottom band
    pad:SetFrameStrata("DIALOG")
    pad:SetFrameLevel(win:GetFrameLevel() + 20)
    pad:SetBackdrop(BACKDROP)
    pad:SetBackdropColor(0.07, 0.057, 0.047, 0.98)
    pad:SetBackdropBorderColor(0.42, 0.32, 0.17, 1)
    pad:SetClampedToScreen(true)
    pad:SetMovable(true)
    pad:EnableMouse(true)
    pad:RegisterForDrag("LeftButton")
    pad:SetScript("OnDragStart", pad.StartMoving)
    pad:SetScript("OnDragStop", pad.StopMovingOrSizing)

    -- tags are pinned to the game's text font rather than a themed template's
    local function Text(x, y, size, color, justify, width)
        local t = pad:CreateFontString(nil, "OVERLAY")
        t:SetPoint("TOPLEFT", pad, "TOPLEFT", P(x), -P(y))
        if width then t:SetWidth(P(width)) end
        t:SetJustifyH(justify or "LEFT")
        t:SetFont(STANDARD_TEXT_FONT, size)
        t:SetTextColor(color[1], color[2], color[3])
        return t
    end
    local function Rule(y)
        local t = pad:CreateTexture(nil, "ARTWORK")
        t:SetTexture(0.42, 0.32, 0.17, 0.5)
        t:SetPoint("TOPLEFT", pad, "TOPLEFT", P(GX), -P(y))
        t:SetSize(P(PAD_W - 2 * GX), P(1))
    end

    Text(GX, 12, 15, PAD_GOLD):SetText("Card placement")
    local slotTag = Text(GX, 38, 13, PAD_DIM, "LEFT", 330)
    Rule(60)

    -- the grid: what a card's frame covers, a cross at its middle, and the dot for the model's place
    local grid = CreateFrame("Frame", nil, pad)
    grid:SetPoint("TOPLEFT", pad, "TOPLEFT", P(GX), -P(GY))
    grid:SetSize(P(GW), P(GH))
    grid:EnableMouse(true)
    grid:SetBackdrop(BACKDROP)
    grid:SetBackdropColor(0.02, 0.02, 0.025, 0.9)
    grid:SetBackdropBorderColor(0.3, 0.23, 0.13, 1)

    local function Line(vertical, pos)
        local t = grid:CreateTexture(nil, "ARTWORK")
        t:SetTexture(0.6, 0.5, 0.3, 0.35)
        if vertical then
            t:SetPoint("BOTTOM", grid, "BOTTOM", P(pos), 0)
            t:SetSize(P(1), P(GH))
        else
            t:SetPoint("LEFT", grid, "LEFT", 0, P(pos))
            t:SetSize(P(GW), P(1))
        end
    end
    Line(true, GW / 2)
    Line(false, GH / 2)

    local dot = grid:CreateTexture(nil, "OVERLAY")
    dot:SetSize(P(12), P(12))
    dot:SetTexture(1, 0.82, 0.2, 0.95)
    pad.dot = dot

    local upTag = Text(GX, GY + GH + 8, 13, PAD_GOLD, "LEFT", GW / 2)
    local leftTag = Text(GX + GW / 2, GY + GH + 8, 13, PAD_GOLD, "LEFT", GW / 2)
    Text(GX, GY + GH + 28, 11, PAD_DIM, "LEFT", GW):SetText("Drag in the grid to place. Wheel zooms.")

    local kindPrev, kindNext -- the buttons that choose the weapon kind, made below

    -- the kinds of weapon on the cards showing now: the pad frames one kind at a time, so each step lands on cards you see
    local function VisibleKinds()
        local list, seen = {}, {}
        for _, c in ipairs(cardButtons) do
            if c:IsShown() and not c.set and c.entry then
                local key, name = SHOT.TypeKey(S.slot, c.entry)
                if key and not seen[key] then
                    seen[key] = true
                    list[#list + 1] = { key = key, name = name }
                end
            end
        end
        table.sort(list, function(a, b) return a.key < b.key end)
        return list
    end

    -- the kind being framed (nil off the weapon slots): the one chosen last while its cards still show, else the first
    local function PickKind()
        if S.tab == "sets" or S.slot < 15 or S.slot > 17 then return nil end
        local list = VisibleKinds()
        for _, k in ipairs(list) do
            if k.key == pad.kind then return k end
        end
        pad.kind = list[1] and list[1].key
        return list[1]
    end

    -- the slot the window has selected owns the values the pad writes (a weapon kind its own); on the Sets tab it is the set cards'
    local function SlotNudge()
        if S.tab == "sets" then return SET_NUDGE end
        local slot = S.slot
        local kind = PickKind()
        local key = kind and kind.key or slot
        local n = CARD_NUDGE[key]
        if not n then
            n = CopyNudge(CARD_NUDGE[slot])
            CARD_NUDGE[key] = n
        end
        return n
    end

    function pad.SetDot(self)
        self.dot:SetPoint("BOTTOMLEFT", grid, "BOTTOMLEFT", P(self.dotX * GW - 6), P(self.dotY * GH - 6))
    end

    -- A slider with its name and value above it. get/set read and write the framing; the new pictures are asked for once
    -- the value has held still (pad.zoomWait), not at every step.
    local rows = {}
    local function Slider(id, y, title, lo, hi, step, wheel, show, read, write)
        local name = Text(CX, y, 12, PAD_DIM, "LEFT", CW - 70)
        name:SetText(title)
        local value = Text(CX + CW - 70, y, 12, PAD_GOLD, "RIGHT", 70)
        local s = CreateFrame("Slider", nil, pad)
        s:SetOrientation("HORIZONTAL")
        s:SetSize(P(CW), P(18))
        s:SetPoint("TOPLEFT", pad, "TOPLEFT", P(CX), -P(y + 18))
        s:SetMinMaxValues(lo, hi)
        s:SetValueStep(step)
        local rail = s:CreateTexture(nil, "BACKGROUND")
        rail:SetPoint("LEFT", s, "LEFT", 0, 0)
        rail:SetPoint("RIGHT", s, "RIGHT", 0, 0)
        rail:SetHeight(P(4))
        rail:SetTexture(0.14, 0.115, 0.09, 1)
        local thumb = s:CreateTexture(nil, "OVERLAY")
        thumb:SetTexture(1, 0.82, 0.2, 0.95)
        thumb:SetSize(P(8), P(16))
        s:SetThumbTexture(thumb)
        s:SetScript("OnValueChanged", function(self, v)
            if self.quiet then return end
            write(SlotNudge(), v)
            value:SetText(show(v))
            pad.zoomWait = 0.4
        end)
        s:EnableMouseWheel(true)
        s:SetScript("OnMouseWheel", function(self, delta) self:SetValue(self:GetValue() + delta * wheel) end)
        -- shows the framing's value without writing it back
        rows[id] = function(n)
            local v = read(n)
            s.quiet = true
            s:SetValue(v)
            s.quiet = nil
            value:SetText(show(v))
        end
    end
    local function Deg(v) return format("%+.0f deg", v) end
    Slider("zoom", GY, "Zoom", 0.4, 3, 0.01, 0.06, function(v) return format("x%.2f", v) end,
        function(n) return n.zoom or 1 end, function(n, v) n.zoom = v end)
    Slider("rot", GY + 54, "Turn  (Z axis)", -180, 180, 1, 5, Deg,
        function(n) return math.deg(n.rot or 0) end, function(n, v) n.rot = math.rad(v) end)
    Slider("pitch", GY + 108, "Pitch  (Y axis)", -180, 180, 1, 5, Deg,
        function(n) return math.deg(n.pitch or 0) end, function(n, v) n.pitch = math.rad(v) end)
    Slider("roll", GY + 162, "Roll  (X axis)", -180, 180, 1, 5, Deg,
        function(n) return math.deg(n.roll or 0) end, function(n, v) n.roll = math.rad(v) end)

    -- up and left, from the dot
    local function WriteReadout(n)
        upTag:SetText(format("up    %+.3f", n.up or 0))
        leftTag:SetText(format("left  %+.3f", n.left or 0))
    end

    -- everything the pad shows, from the values the picture is framed with, so the pad and the cards cannot drift apart
    local function WriteTags(n)
        local kind = PickKind()
        local name = S.tab == "sets" and "Sets" or SLOT_BY_ID[S.slot] and SLOT_BY_ID[S.slot].name or ("slot " .. S.slot)
        slotTag:SetText(kind and (name .. "  -  " .. kind.name) or name)
        for _, b in ipairs({ kindPrev, kindNext }) do
            if kind then b:Show() else b:Hide() end
        end
        WriteReadout(n)
        for _, set in pairs(rows) do set(n) end
    end

    -- Zoom from the wheel over the grid steps by a share of the current value.
    local function Zoom(steps)
        local n = SlotNudge()
        n.zoom = Clamp((n.zoom or 1) * 1.06 ^ steps, 0.4, 3)
        rows.zoom(n)
        pad.zoomWait = 0.4
    end
    pad:SetScript("OnUpdate", function(self, dt)
        -- the cards fill in after a page or slot change, so the weapon kinds on show are looked at again now and then
        self.kindWait = (self.kindWait or 0) - dt
        if self.kindWait <= 0 then
            self.kindWait = 0.5
            if not self.grabX and not self.zoomWait then self:Refresh() end
        end
        if not self.zoomWait then return end
        self.zoomWait = self.zoomWait - dt
        if self.zoomWait <= 0 then
            self.zoomWait = nil
            self:Shoot()
        end
    end)
    grid:EnableMouseWheel(true)
    grid:SetScript("OnMouseWheel", function(_, delta) Zoom(delta) end)

    -- steps through the weapon kinds on show; the dot and the sliders then belong to that kind
    local function StepKind(d)
        local kind, list = PickKind(), VisibleKinds()
        for i, k in ipairs(list) do
            if kind and k.key == kind.key then
                pad.kind = list[(i - 1 + d) % #list + 1].key
                break
            end
        end
        pad:Refresh()
    end
    kindPrev = MakeButton(pad, "<", 28, 22, PAD_W - GX - 64, 32, function() StepKind(-1) end)
    kindNext = MakeButton(pad, ">", 28, 22, PAD_W - GX - 30, 32, function() StepKind(1) end)

    -- the dot moved: write its values and lay the readout out from them
    function pad.Place(self)
        local n = SlotNudge()
        n.up = (self.dotY - 0.5) * SPAN
        n.left = (0.5 - self.dotX) * SPAN
        WriteReadout(n)
    end

    -- A card's picture cannot be reframed in place: the framing is part of the picture's name, so a moved dot
    -- asks for new pictures of the visible cards.
    -- shortcut: every framing keeps its pictures on disk (one per drag), no old one is dropped until the cache is
    -- cleared; upgrade if dragging long sessions on many slots fills Cache/Snapshots
    function pad.Shoot()
        RefreshGrid()
    end

    -- a value changed elsewhere (another slot, /tmog nudge, Reset): move the dot and the sliders to it
    function pad.Refresh(self)
        local n = SlotNudge()
        self.dotX = Clamp(0.5 - (n.left or 0) / SPAN, 0, 1)
        self.dotY = Clamp(0.5 + (n.up or 0) / SPAN, 0, 1)
        self:SetDot()
        WriteTags(n)
    end

    -- left drag inside the grid moves the dot; the drag is measured from where the dot started, so it
    -- cannot run away from the cursor
    grid:SetScript("OnMouseDown", function()
        pad.grabX, pad.grabY = GetCursorPosition()
        pad.grabDotX, pad.grabDotY = pad.dotX, pad.dotY
        pad.grabW = grid:GetWidth() / grid:GetEffectiveScale()
    end)
    grid:SetScript("OnMouseUp", function()
        if not pad.grabX then return end
        pad.grabX = nil
        pad:Shoot()
    end)
    grid:SetScript("OnUpdate", function()
        if not pad.grabX then return end
        local cx, cy = GetCursorPosition()
        pad.dotX = Clamp(pad.grabDotX + (cx - pad.grabX) / pad.grabW, 0, 1)
        pad.dotY = Clamp(pad.grabDotY + (cy - pad.grabY) / pad.grabW, 0, 1)
        pad:SetDot()
        pad:Place()
    end)

    -- the copy window: the same text in a framed, scrolling box the user can select from
    local function Popup()
        if not win.nudgeBox then
            local f = Panel(win, 0, 0, 540, 440)
            f:ClearAllPoints()
            f:SetPoint("CENTER", win, "CENTER")
            f:SetFrameStrata("DIALOG")
            f:SetFrameLevel(win:GetFrameLevel() + 30)
            f:EnableMouse(true)
            f:SetBackdropBorderColor(0.42, 0.32, 0.17, 1)
            local title = f:CreateFontString(nil, "OVERLAY")
            title:SetPoint("TOPLEFT", f, "TOPLEFT", P(16), -P(14))
            title:SetFont(STANDARD_TEXT_FONT, 15)
            title:SetTextColor(PAD_GOLD[1], PAD_GOLD[2], PAD_GOLD[3])
            title:SetText("Placement values")
            local hint = f:CreateFontString(nil, "OVERLAY")
            hint:SetPoint("TOPLEFT", f, "TOPLEFT", P(16), -P(38))
            hint:SetFont(STANDARD_TEXT_FONT, 12)
            hint:SetTextColor(PAD_DIM[1], PAD_DIM[2], PAD_DIM[3])
            hint:SetText("Click the text to select all, then Ctrl+C. Paste it over the matching tables in the addon.")
            f.well = TextWell(f, 16, 62, 508, 316)
            f.edit = f.well.edit
            f:SetScript("OnHide", function() f.edit:ClearFocus() end)
            MakeButton(f, "Close", 100, 22, 424, 392, function() f:Hide() end)
            win.nudgeBox = f
        end
        win.nudgeBox:Show()
        win.nudgeBox.well:SetText(NudgeText())
        win.nudgeBox.edit:SetFocus()
        win.nudgeBox.edit:HighlightText()
    end

    local BY = PAD_H - 44
    MakeButton(pad, "Copy", 100, 22, GX, BY, function()
        print("Transmog card placement:")
        for line in NudgeText():gmatch("[^\n]+") do print(line) end
        Popup()
    end)
    MakeButton(pad, "Reset", 100, 22, GX + 110, BY, function()
        if S.tab == "sets" then
            SET_NUDGE = CopyNudge(nudgeFemale and SET_NUDGE_SEED_F or SET_NUDGE_SEED_M)
        else
            local seeds = nudgeFemale and CARD_NUDGE_SEED_F or CARD_NUDGE_SEED_M
            local kind = PickKind()
            -- a weapon kind goes back to its own seed, or to its slot's when it has none
            CARD_NUDGE[kind and kind.key or S.slot] = CopyNudge(kind and seeds[kind.key] or seeds[S.slot])
        end
        pad:Refresh()
        pad:Shoot()
    end)
    MakeButton(pad, "Hide", 100, 22, PAD_W - GX - 100, BY, function() pad:Hide() end)

    pad:Refresh()
end

local function BuildWindow()
    -- pixel metrics of this screen
    local sw, sh = ScreenPixels()
    U = UIParent:GetHeight() / sh * WIN_K

    local M, TITLE, LEFT_W, GAP = 14, 62, 300, 14
    local winH = math.min(sh - 48, 1060)
    local innerH = winH - TITLE - 2 * M

    -- card rows from the height that is left, columns from the width
    local fixedV = GRID_Y + 72 -- panel header and pager
    -- up to three rows; the window is drawn at WIN_K, so the screen holds 1/WIN_K times more layout pixels
    local roomH = (sh - 48) / WIN_K - TITLE - 2 * M - 68
    L.rows = Clamp(math.floor((roomH - TAB_H - fixedV - 14) / CELL_H), 2, 3)
    local maxW = sw - 40
    local minStage = 600
    local availRight = maxW - 2 * M - LEFT_W - 2 * GAP - minStage
    L.cols = 4 -- four cards to a row
    L.perPage = L.cols * L.rows
    local rightW = L.cols * CELL_W + 72
    local rightH = fixedV + L.rows * CELL_H + 14
    -- the preview is wider than the room the other columns leave it; the window is drawn at WIN_K, so the
    -- screen holds 1/WIN_K times more layout pixels than its own pixel count
    local roomW = (sw - 40) / WIN_K - 2 * M - LEFT_W - 2 * GAP - rightW
    local stageW = Clamp(math.min(920, roomW), 520, 1100)
    local winW = 2 * M + LEFT_W + 2 * GAP + stageW + rightW
    innerH = rightH + TAB_H -- the tabs stand above the collection panel
    local BAND = 68 -- the bottom band that holds Apply
    winH = innerH + TITLE + 2 * M + BAND

    win = CreateFrame("Frame", "ManastormTransmogFrame", UIParent)
    win:SetSize(P(winW), P(winH))
    win:SetPoint("CENTER", UIParent, "CENTER", 0, 0)
    win:SetFrameStrata("HIGH")
    win:SetToplevel(true)
    win:SetMovable(true)
    win:EnableMouse(true)
    win:SetClampedToScreen(true)
    win:RegisterForDrag("LeftButton")
    win:SetScript("OnDragStart", win.StartMoving)
    win:SetScript("OnDragStop", win.StopMovingOrSizing)
    win:SetBackdrop({
        bgFile = "Interface\\Buttons\\WHITE8X8",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        edgeSize = 14,
        insets = { left = 3, right = 3, top = 3, bottom = 3 },
    })
    win:SetBackdropColor(0.045, 0.037, 0.033, 0.98)
    win:SetBackdropBorderColor(0.42, 0.32, 0.17, 1)
    win:Hide()
    tinsert(UISpecialFrames, "ManastormTransmogFrame")

    -- closing with unapplied changes asks first (the close button and Esc both go through Hide)
    local realHide = win.Hide
    win.Hide = function(self)
        if self:IsShown() and not S.closing and HasUnsavedWork() then
            StaticPopup_Show("MANASTORM_TMOG_CLOSE")
            return
        end
        realHide(self)
    end

    local strip = win:CreateTexture(nil, "BACKGROUND")
    strip:SetPoint("TOPLEFT", 4, -4)
    strip:SetPoint("TOPRIGHT", -4, -4)
    strip:SetHeight(P(TITLE - 6))
    strip:SetTexture(0.02, 0.017, 0.015, 1)
    local titleRule = win:CreateTexture(nil, "BORDER")
    titleRule:SetPoint("TOPLEFT", strip, "BOTTOMLEFT", 0, 0)
    titleRule:SetPoint("TOPRIGHT", strip, "BOTTOMRIGHT", 0, 0)
    titleRule:SetHeight(U / WIN_K) -- one screen pixel
    titleRule:SetTexture(0.3, 0.23, 0.13, 0.3)
    local title = win:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    title:SetPoint("TOP", 0, -P(18))
    title:SetText("Transmogrify")

    local close = CreateFrame("Button", nil, win, "UIPanelCloseButton")
    close:SetPoint("TOPRIGHT", 0, 0)

    win.status = win:CreateFontString(nil, "ARTWORK", "GameFontNormal")
    win.status:SetPoint("BOTTOMLEFT", P(M + 12), P(BAND / 2 - 7))
    win.status:SetJustifyH("LEFT")
    win.status:SetWidth(P(700))

    -- the bottom band: a darker strip along the foot of the window, Apply at its right end
    local band = win:CreateTexture(nil, "BACKGROUND")
    band:SetPoint("BOTTOMLEFT", 4, 4)
    band:SetPoint("BOTTOMRIGHT", -4, 4)
    band:SetHeight(P(BAND))
    band:SetTexture(0.02, 0.017, 0.015, 1)
    -- the price of what is being previewed, centred in the band
    win.cost = win:CreateFontString(nil, "ARTWORK", "GameFontHighlightLarge")
    win.cost:SetPoint("CENTER", band, "CENTER", 0, 0)
    local bandRule = win:CreateTexture(nil, "BORDER")
    bandRule:SetPoint("BOTTOMLEFT", band, "TOPLEFT", 0, 0)
    bandRule:SetPoint("BOTTOMRIGHT", band, "TOPRIGHT", 0, 0)
    bandRule:SetHeight(U / WIN_K) -- one screen pixel
    bandRule:SetTexture(0.3, 0.23, 0.13, 0.3)

    local top = TITLE + M
    BuildOutfits(M, top + 10, LEFT_W, innerH - 10)
    -- the stage starts under the title bar's rule, not over it (the strip is in screen units, the layout in pixels)
    local stageTop = TITLE - 6 + 4 / U + 1 / WIN_K
    -- and runs down over the band's rule by one pixel: the band sits 4 units above the window's foot, its rule one
    -- pixel above that, so ending at the band's top covers the rule's pixel
    BuildStage(M + LEFT_W + GAP, stageTop, stageW, winH - BAND - 4 / U - stageTop)
    BuildSlots()

    -- Apply in the bottom band; the price is centred beside it
    local by = winH - BAND + (BAND - 54) / 2 - 2 - 2 / WIN_K -- two screen pixels above where it sat
    win.apply = GlueButton(win, "Apply Outfit", 240, 50, winW - M - 240 - 6, by, Apply)
    -- Save Outfit stands to its left: it stores the preview without changing the character
    win.save = GlueButton(win, "Save Outfit", 240, 50, winW - M - 2 * 240 - 6 - 12, by, SaveOutfit)
    for _, btn in ipairs({ win.apply, win.save }) do
        local face, size = btn.label:GetFont()
        btn.label:SetFont(face, size - 2)
    end

    BuildCollection(M + LEFT_W + 2 * GAP + stageW, top + TAB_H, rightW, innerH - TAB_H)

    -- show / hide ---------------------------------------------------------------------------------
    win:SetScript("OnShow", function()
        ResetModelUnit()
        ApplyView()
        SyncNudges()   -- the player's sex is only known once in the world, and the seeds follow it
        Send("ST")
        Send("OL")
        RequestList()
        RebuildModel()
        win.rebuildIn = 0.3
        RefreshAll()
        Say("", true)
    end)
    -- whatever was being previewed is dropped when the window closes
    win:SetScript("OnHide", function()
        GameTooltip:Hide()
        StaticPopup_Hide("MANASTORM_TMOG_CLOSE")
        StaticPopup_Hide("MANASTORM_TMOG_SWITCH")
        wipe(S.pending)
        -- hiding drops the client's hidden model, so the next job loads it afresh and must wait for it again
        SHOT.ColdWorkers()
        wipe(SHOT.tried)
        wipe(SHOT.gaveUp)
        -- nothing stays in video memory for a window nobody sees
        for _, c in ipairs(cardButtons) do c.model:SetTexture(nil) end
        if scratch then
            scratch:KeepSnapshots()
            scratch:ReleaseSnapshotTargets()
        end
        S.outfitId = nil
        S.sheathe = false
        if win.sheathe then Toggle(win.sheathe.check, false) end
        ResetView()
    end)
    win:RegisterEvent("PLAYER_MONEY")
    win:SetScript("OnEvent", function() if win:IsShown() then RefreshButtons() end end)

    win.poll = 0
    win:SetScript("OnUpdate", function(self, dt)
        -- the main model: re-dress once it has settled, and hold the view while it does
        if self.rebuildIn then
            self.rebuildIn = self.rebuildIn - dt
            if self.rebuildIn <= 0 then
                self.rebuildIn = nil
                RebuildModel()
            end
        end
        if self.settle then
            self.settle = self.settle - dt
            ApplyView()
            if self.settle <= 0 then self.settle = nil end
        end

        -- slots with an unsaved change breathe
        self.pulse = (self.pulse or 0) + dt
        -- a changed slot plays the whole flipbook once, backwards, then holds its first frame and breathes: the square swells
        -- and fades
        for _, b in ipairs(slotButtons) do
            if b.pending:IsShown() then
                b.flipT = (b.flipT or 0) + dt
                local n = floor(b.flipT * FLIP_FPS)
                if n < FLIP_FRAMES then
                    if n ~= b.flipN then
                        b.flipN = n
                        FlipFrame(b.pending, FLIP_FRAMES - 1 - n) -- backwards: frame 24 first, frame 1 last
                    end
                    b.pending:SetAlpha(1)
                else
                    if b.flipN ~= 0 then
                        b.flipN = 0
                        FlipFrame(b.pending, 0)
                    end
                    -- the breath starts at full opacity when the flipbook hands over
                    local held = b.flipT - FLIP_FRAMES / FLIP_FPS
                    b.pending:SetAlpha(0.6 + 0.4 * math.cos(held * 5.5))
                end
            else
                b.flipT, b.flipN = nil, nil
            end
        end

        SHOT.PumpShots(dt)
        SHOT.WatchCards(dt)
        SHOT.SpinLoading()

        -- icons that were not cached when first wanted
        self.poll = self.poll + dt
        if self.poll > 0.5 then
            self.poll = 0
            local ready = false
            for id, since in pairs(wantedIcons) do
                if GetItemInfo(id) then
                    wantedIcons[id] = nil
                    ready = true
                elseif GetTime() - since > 15 then
                    wantedIcons[id] = nil
                end
            end
            if ready then RefreshAll() end
        end
    end)
end

function ManastormTransmog_Show()
    if not win then BuildWindow() end
    if not win:IsShown() then win:Show() end
end

function ManastormTransmog_Toggle()
    if not win then BuildWindow() end
    if win:IsShown() then win:Hide() else win:Show() end
end

------------------------------------------------------------------------------------------------------
-- character panel: a transmogrified piece says which appearance it wears
------------------------------------------------------------------------------------------------------

if GameTooltip then
    hooksecurefunc(GameTooltip, "SetInventoryItem", function(self, unit, invSlot)
        if unit ~= "player" or not invSlot then return end
        local fake = S.fake[invSlot - 1] or 0
        if fake == 0 or fake == S.equip[invSlot - 1] then return end
        local name
        if fake == HIDDEN then
            name = "Hidden"
        else
            name = GetItemInfo(fake)
            if not name then
                Prefetch(fake)
                return
            end
        end
        -- the line goes in right under the item's name: add it at the foot, then move every line below the
        -- name down by one (text, right text and colour; the name keeps its own larger font)
        self:AddLine(" ")
        local tn = self:GetName()
        for i = self:NumLines(), 3, -1 do
            local a, b = _G[tn .. "TextLeft" .. i], _G[tn .. "TextLeft" .. (i - 1)]
            local ra, rb = _G[tn .. "TextRight" .. i], _G[tn .. "TextRight" .. (i - 1)]
            a:SetText(b:GetText())
            a:SetTextColor(b:GetTextColor())
            if ra and rb then
                ra:SetText(rb:GetText())
                ra:SetTextColor(rb:GetTextColor())
                if rb:IsShown() then ra:Show() else ra:Hide() end
            end
        end
        -- the sell price is a money frame hung on one of the lines: it must follow its line down, or it ends up
        -- over the text that moved into that line's place
        for i = 1, self.shownMoneyFrames or 0 do
            local money = _G[tn .. "MoneyFrame" .. i]
            local point, rel, relPoint, x, y = money and money:GetPoint()
            local n = rel and rel.GetName and rel:GetName() and rel:GetName():match("^" .. tn .. "TextLeft(%d+)$")
            if n and _G[tn .. "TextLeft" .. (tonumber(n) + 1)] then
                money:ClearAllPoints()
                money:SetPoint(point, _G[tn .. "TextLeft" .. (tonumber(n) + 1)], relPoint, x, y)
            end
        end
        local line = _G[tn .. "TextLeft2"]
        line:SetText("Appearance: " .. name)
        line:SetTextColor(1, 0.5, 1)
        _G[tn .. "TextRight2"]:Hide()
        self:Show()
    end)
end

------------------------------------------------------------------------------------------------------
-- plumbing
------------------------------------------------------------------------------------------------------

local driver = CreateFrame("Frame")
driver:RegisterEvent("PLAYER_ENTERING_WORLD")
driver:RegisterEvent("CHAT_MSG_ADDON")
driver:SetScript("OnEvent", function(self, event, prefix, message)
    if event == "PLAYER_ENTERING_WORLD" then
        self.hello = 3 -- let the chat layer settle before the first whisper
    elseif event == "CHAT_MSG_ADDON" and prefix == PFX_IN and message then
        Handle(message)
    end
end)
driver:SetScript("OnUpdate", function(self, dt)
    if self.hello then
        self.hello = self.hello - dt
        if self.hello <= 0 then
            self.hello = nil
            Send("HI")
        end
    end
    if #sendQueue > 0 then
        sendClock = sendClock + dt
        if sendClock >= 0.06 then
            sendClock = 0
            local name = UnitName("player")
            if name then SendAddonMessage(PFX_OUT, table.remove(sendQueue, 1), "WHISPER", name) end
        end
    end
end)

SLASH_MANASTORMTRANSMOG1 = "/tmog"
SLASH_MANASTORMTRANSMOG2 = "/transmogrify"
SlashCmdList["MANASTORMTRANSMOG"] = function(msg)
    local x, y, z, amb, dir = (msg or ""):match("^light%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)%s+(%S+)")
    if x then
        local L = CARD_LIGHT   -- tunes the card pictures; the main preview keeps LIGHT
        L.x, L.y, L.z = tonumber(x) or L.x, tonumber(y) or L.y, tonumber(z) or L.z
        L.amb, L.dir = tonumber(amb) or L.amb, tonumber(dir) or L.dir
        SHOT.sig = nil
        for _, w in ipairs(workers) do ApplyLight(w.m) end
        if win then
            -- the cards' pictures were lit with the old light
            SHOT.Scratch():ClearSnapshots()
            RefreshGrid()
        end
        print(format("Transmog light: direction %.2f %.2f %.2f, ambient %.2f, direct %.2f", L.x, L.y, L.z, L.amb, L.dir))
        return
    end
    local nWorkers = (msg or ""):match("^workers%s+(%d+)")
    if nWorkers then
        -- /tmog workers <n>: how many hidden models take pictures at once (1 to compare against the old single worker)
        SHOT.workers = math.max(1, tonumber(nWorkers))
        print("Transmog workers: " .. SHOT.workers)
        return
    end
    local why = (msg or ""):match("^why%s+(%d+)")
    if why then
        Send(format("Y:%d:%s", S.slot, why))   -- the server answers in chat
        return
    end
    local mode = (msg or ""):match("^blend%s+(%a+)")
    if mode then
        -- /tmog blend blend|disable|alphakey|add|mod: how the cards draw their picture, to find what hides the item
        mode = mode:upper()
        if mode == "ALPHA" then mode = "BLEND" end
        for _, c in ipairs(cardButtons) do c.model:SetBlendMode(mode) end
        print("Transmog card blend: " .. mode)
        return
    end
    if (msg or ""):match("^probe") then
        -- /tmog probe: card 2 gets a game icon and card 3 a solid red, to tell a region that does not draw from a
        -- picture that does not load; /tmog probe off puts the pictures back
        if (msg or ""):match("^probe%s+off") then
            RefreshGrid()
            return
        end
        -- card 2: a game icon; 3: solid red, BLEND; 4: solid green, DISABLE; 5: its own picture, taken fresh from the
        -- client with BLEND; 6: its own picture as it already is
        local a, c, d, e = cardButtons[2], cardButtons[3], cardButtons[4], cardButtons[5]
        if a then
            a.model:SetBlendMode("BLEND")
            print("probe icon:", a.model:SetTexture("Interface\\Icons\\INV_Misc_QuestionMark"))
            a.model:Show()
        end
        if c then
            c.model:SetBlendMode("BLEND")
            c.model:SetTexture(1, 0, 0, 1)
            c.model:Show()
        end
        if d then
            d.model:SetBlendMode("DISABLE")
            d.model:SetTexture(0, 1, 0, 1)
            d.model:Show()
        end
        if e and e.shotKey then
            e.model:SetBlendMode("BLEND")
            e.model:SetTexture(nil)
            print("probe picture:", e.model:SetTexture("Snapshot:" .. e.shotKey), e.shotKey)
            e.model:Show()
        end
        return
    end
    if (msg or ""):match("^shot") then
        local m = scratch
        print(format("workers %d | queue %d", #workers, #cardQueue))
        for i, w in ipairs(workers) do
            local shot = w.shot
            print(format("worker %d: %s | warm %s | %s", i, shot and (shot.state .. " t=" .. format("%.1f", shot.t) .. " key=" .. shot.key) or "idle", tostring(w.warm), w.m:GetSnapshotDebug()))
        end
        if not m then print("no scratch model yet (open the window on a page)") end
        for i, c in ipairs(cardButtons) do
            -- the first three, and every card that wants a picture and does not show one
            if c:IsShown() and (i <= 3 or (c.shotKey and not c.model:IsShown())) then
                print(format("card %d: key=%s has=%s shown=%s visible=%s size=%.0fx%.0f alpha=%.2f tex=%s", i, tostring(c.shotKey), tostring(m and c.shotKey and m:HasSnapshot(c.shotKey)), tostring(c.model:IsShown()), tostring(c.model:IsVisible()), c.model:GetWidth(), c.model:GetHeight(), c.model:GetAlpha(), tostring(c.model:GetTexture())))
            end
        end
        return
    end
    if (msg or ""):match("^hairlit") then
        hairLit = not hairLit
        SHOT.ColdWorkers()   -- the hidden models are built again for the new style, so a shot must wait for them
        if win then
            SHOT.Scratch():ClearSnapshots()
            RefreshGrid()
        end
        print("Transmog hairlit: " .. tostring(hairLit))
        return
    end
    local sx, hair = (msg or ""):match("^hair%s+([mf])%s+(%d+)")
    if sx then
        HAIR[sx] = tonumber(hair)
        SHOT.ColdWorkers()   -- the hidden models are built again for the new style, so a shot must wait for them
        if win then
            SHOT.Scratch():ClearSnapshots()
            RefreshGrid()
        end
        print("Transmog hair " .. sx .. ": " .. hair)
        return
    end
    local nSlot, nUp, nLeft = (msg or ""):match("^nudge%s+(%d+)%s+(-?%d+%.?%d*)%s*(-?%d+%.?%d*)")
    if nSlot then
        -- /tmog nudge <slot> <up> [left]: card framing by eye, in shares of the preview frame
        nSlot = tonumber(nSlot)
        local n = CARD_NUDGE[nSlot] or {}
        CARD_NUDGE[nSlot] = n
        n.up = Clamp(tonumber(nUp), -1.5, 1.5)
        if nLeft ~= "" then n.left = Clamp(tonumber(nLeft), -1.5, 1.5) end
        if win then RefreshGrid() end   -- the framing is part of the picture's name, so this asks for new ones
        print(format("Transmog nudge %d: up %s left %s", nSlot, tostring(n.up), tostring(n.left)))
        if framePad then framePad:Refresh() end
        return
    end
    local forced = (msg or ""):match("^ratio%s*(%S*)")
    if forced then
        RATIO_OVERRIDE = tonumber(forced)   -- no number clears it
        SHOT.sig = nil
        print("Transmog height ratio: " .. tostring(RATIO_OVERRIDE or "automatic"))
        if model then ApplyView() end
        if win then
            -- the cards' pictures were framed with the old ratio
            SHOT.Scratch():ClearSnapshots()
            RefreshGrid()
        end
        return
    end
    local ref =(msg or ""):match("^height%s*(%S*)")
    if ref then
        REF_HEIGHT = tonumber(ref) or REF_HEIGHT   -- /tmog height <n> sets the reference for this session
        print(format("Transmog model height: %.4f, reference: %s", model and model:GetModelHeight() or 0, tostring(REF_HEIGHT)))
        return
    end
    local drop = (msg or ""):match("^pad%s*(%S*)")
    if drop then
        -- /tmog pad: the frame pad, a bare command opening it if the window is not up yet
        if not win then BuildWindow() elseif not win:IsShown() then win:Show() end
        BuildFramePad()
        Toggle(framePad, drop ~= "0")   -- 3.3.5 regions have no show-by-boolean setter
        framePad:Refresh()
        return
    end
    ManastormTransmog_Toggle()
end

-- The pad is a tuning tool, so it gets a short command of its own; it is not built until asked for.
SLASH_MANASTORMTRANSMOGPAD1 = "/tmogpad"
SlashCmdList["MANASTORMTRANSMOGPAD"] = function()
    if not win then BuildWindow() elseif not win:IsShown() then win:Show() end
    BuildFramePad()
    framePad:Show()
    framePad:Refresh()
end
