-- Esteria's own buttons on DragonUI's micro strip (Transmog, Speedflying talents, Radio), and the Talents
-- button turned into Character Advancement. Loaded from modules.xml before micromenu.lua, which captures its
-- button list in a file-scope local, so the buttons have to exist by the time it runs (as Pets & Mounts does).
-- micromenu.lua reads addon.EsteriaMicro to place, skin and size the strip around them.

local addon = select(2, ...)

local SHEETS = 'Interface\\AddOns\\DragonUI\\Textures\\Micromenu\\'

-- Each sheet is four 64x82 cells on a 256x128 texture: up, mouseover, down, disabled.
-- click and tip name global functions so an add-on loaded later (or in a different order) can supply them.
local SPECS = {
    {
        stem = 'Transmog', sheet = SHEETS .. 'esteria-transmog',
        title = 'Transmogrification', text = 'Change the look of your gear.',
        click = 'ManastormTransmog_Toggle',
    },
    {
        stem = 'Flight', sheet = SHEETS .. 'esteria-flight',
        title = 'Speedflying Talents', text = 'Spend points on your speedflying talent tree.',
        click = 'EsteriaShell_ToggleFlight',
    },
    {
        stem = 'Radio', sheet = 'Interface\\AddOns\\EverlookBroadcasting\\Textures\\radio-micro.blp',
        title = 'Everlook Broadcasting Co.', text = 'Internet radio.',
        click = 'EverlookBroadcasting_MicroClick', tip = 'EverlookBroadcasting_MicroTooltip',
    },
}

addon.EsteriaMicro = {}

function EsteriaShell_ToggleFlight()
    local f = _G.Manastorm_TalentFrame
    if not f then return end
    if f:IsShown() then f:Hide() else f:Show() end
end

local function call(name, ...)
    local fn = _G[name]
    if type(fn) == 'function' then return fn(...) end
end

for _, spec in ipairs(SPECS) do
    local name = spec.stem .. 'MicroButton'
    if not _G[name] then
        local btn = CreateFrame('Button', name, UIParent)
        btn:SetSize(32, 40)
        btn:SetPoint('CENTER')
        btn:Hide()
        -- micromenu.lua re-skins all four states from the atlas rows below; they only have to exist for its getters.
        for _, set in ipairs({ 'SetNormalTexture', 'SetPushedTexture', 'SetDisabledTexture', 'SetHighlightTexture' }) do
            btn[set](btn, spec.sheet)
        end
        btn:GetHighlightTexture():SetBlendMode('ADD')
        btn:RegisterForClicks('LeftButtonUp', 'RightButtonUp')
        btn:SetScript('OnClick', function(self, button) call(spec.click, self, button) end)
        btn:SetScript('OnEnter', function(self)
            if spec.tip and type(_G[spec.tip]) == 'function' then return _G[spec.tip](self) end
            GameTooltip:SetOwner(self, 'ANCHOR_LEFT')
            GameTooltip:SetText(spec.title, 1, 0.82, 0.1)
            GameTooltip:AddLine(spec.text, 1, 1, 1, true)
            GameTooltip:Show()
        end)
        btn:SetScript('OnLeave', function() GameTooltip:Hide() end)
    end
    addon.EsteriaMicro[string.lower(spec.stem)] = { atlas = 'Esteria-' .. spec.stem, sheet = spec.sheet }
end

-- Talents -> Character Advancement (the classless panel; the talent window itself is switched off) ------

local function advancementOnClick()
    if type(_G.ClasslessWildcard_TogglePanel) == 'function' then
        ClasslessWildcard_TogglePanel()
    end
end

local function relabelTalentButton()
    local btn = _G.TalentMicroButton
    if not btn then return end
    btn:SetScript('OnClick', advancementOnClick)
    btn:Enable() -- the stock button is disabled below level 10; the advancement panel starts at level 1
    local text = 'Character Advancement'
    if MicroButtonTooltipText then text = MicroButtonTooltipText(text, 'CLASSLESSWILDCARD_TOGGLE') end
    btn.tooltipText = text
    btn.newbieText = 'Opens the Character Advancement panel.'
end

relabelTalentButton()
hooksecurefunc('UpdateMicroButtons', relabelTalentButton)

-- The classless addon turns the "?" Help button into a second Hero Advancement button; the strip drops it
-- (DragonUI skips suppressed buttons) now that Talents is Character Advancement.
local help = _G.HelpMicroButton
if help then
    help.dragonUISuppressed = true
    help:SetScript('OnShow', help.Hide)
    help:Hide()
end
