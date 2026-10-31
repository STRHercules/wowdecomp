#include "Cursor.h"
#include "Engine/Source/FrameScript/FrameScript.h"
#include "Engine/Source/Lumenveil/OptionsPages.h"

// Runs as built-in FrameXML after the signed UI loads, on login and /reload.
// Adds an "Esteria" category to the Video options with the horizon and fog controls. It uses the stock
// panel lifecycle, so Okay, Cancel, Defaults and the category list treat it like any other video panel.
void DistanceInitializeOptions() {
    static const char script[] = R"lua(
if not VideoOptionsFrame or WowDecompEsteriaVideoPanel then return end

local panel = CreateFrame("Frame", "WowDecompEsteriaVideoPanel", VideoOptionsFramePanelContainer)
panel:Hide()
panel.name = "Esteria"
panel.options = {}

local title = panel:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
title:SetPoint("TOPLEFT", 16, -16)
title:SetText("Esteria")

local sub = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
sub:SetPoint("TOPLEFT", title, "BOTTOMLEFT", 0, -8)
sub:SetPoint("RIGHT", -32, 0)
sub:SetJustifyH("LEFT")
sub:SetText("Draw distance, horizon, fog and field of view.Drag a slider, use the mouse wheel for small steps, or type an exact value.")

local stock = VideoOptionsEffectsPanelViewDistance

local function MakeSlider(name, cvar, y, entry, low, high, format, wheel, owner)
    owner = owner or panel
    local slider = CreateFrame("Slider", name, owner, "OptionsSliderTemplate")
    slider:SetSize(300, 17)
    slider:SetPoint("TOPLEFT", 24, y)
    slider.type = CONTROLTYPE_SLIDER
    slider.cvar = cvar
    slider.SetDisplayValue = slider.SetValue
    slider.SetValue = function(self, value)
        self:SetDisplayValue(value)
        BlizzardOptionsPanel_SetCVarSafe(self.cvar, value)
    end
    _G[name .. "Low"]:SetText(low)
    _G[name .. "High"]:SetText(high)

    -- The readout is an edit box, so an exact value can be typed.
    local readout = CreateFrame("EditBox", name .. "Edit", slider, "InputBoxTemplate")
    readout:SetSize(64, 20)
    readout:SetPoint("LEFT", slider, "RIGHT", 16, 0)
    readout:SetAutoFocus(false)
    readout:SetJustifyH("CENTER")
    readout:SetScript("OnEnterPressed", function(self)
        local value = tonumber(self:GetText())
        if value then slider:SetDisplayValue(value) end
        self:ClearFocus()
        self:SetText(format(slider:GetValue()))
    end)
    readout:SetScript("OnEscapePressed", function(self)
        self:ClearFocus()
        self:SetText(format(slider:GetValue()))
    end)

    slider:EnableMouseWheel(true)
    slider:SetScript("OnMouseWheel", function(self, delta)
        self:SetDisplayValue(self:GetValue() + delta * wheel)
    end)
    slider:SetScript("OnValueChanged", function(self, value)
        self.newValue = value
        if not readout:HasFocus() then readout:SetText(format(value)) end
        -- FoV previews live; Cancel still restores the old value through SetValue.
        if cvar == "cameraFov" then SetCVar(cvar, value) end
        if cvar == "farclip" and stock and stock.SetDisplayValue then
            stock:SetValueStep(1) -- the stock slider re-snaps itself to tenths of its range on login
            stock:SetDisplayValue(value)
        end
    end)
    owner.options[cvar] = entry
    BlizzardOptionsPanel_RegisterControl(slider, owner)
    return slider
end

local distance = MakeSlider("WowDecompDistanceSlider", "farclip", -96,
    { text = "View Distance", minValue = 177, maxValue = 10000, valueStep = 1,
      tooltip = "How far terrain and objects are drawn, in yards. Mouse wheel nudges by 25; type an exact value in the box." },
    "177", "10000", function(v) return string.format("%d", v + 0.5) end, 25)

MakeSlider("WowDecompHorizonSlider", "horizonFarclipScale", -160,
    { text = "Horizon distance", minValue = 3, maxValue = 12, valueStep = 0.1,
      tooltip = "How far the low detail horizon terrain reaches, as a multiple of view distance. Mouse wheel nudges by 0.5." },
    "3x", "12x", function(v) return string.format("%.1fx", v) end, 0.5)

MakeSlider("WowDecompFogSlider", "fogDistanceScale", -224,
    { text = "Distance fog", minValue = 0, maxValue = 1.5, valueStep = 0.01,
      tooltip = "Where distance fog ends relative to view distance. 0 turns distance fog off; above 100% shows the edge of the world. Mouse wheel nudges by 5%." },
    "Off", "150%", function(v)
        if v <= 0.004 then return "Off" end
        return string.format("%d%%", v * 100 + 0.5)
    end, 0.05)

-- The Effects page keeps its slider (the quality presets drive it) but it is hidden; close the gap it leaves.
if stock then
    stock:Hide()
    VideoOptionsEffectsPanelTerrainDetail:ClearAllPoints()
    VideoOptionsEffectsPanelTerrainDetail:SetPoint("TOPLEFT", VideoOptionsEffectsPanelQuality, "BOTTOMLEFT", 2, -20)
end

panel:SetScript("OnShow", function() distance:SetDisplayValue(tonumber(GetCVar("farclip")) or 350) end)

MakeSlider("WowDecompFovSlider", "cameraFov", -288,
    { text = "Field of view", minValue = 50, maxValue = 120, valueStep = 1,
      tooltip = "Camera field of view in degrees. Stock is 90. Mouse wheel nudges by 1." },
    "50", "120", function(v) return string.format("%d", v + 0.5) end, 1)

local note = panel:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
note:SetPoint("TOPLEFT", 24, -338)
note:SetWidth(380)
note:SetJustifyH("LEFT")
note:SetText("Large view distances load much more terrain: expect higher memory use and longer streaming. Fog above 100% shows the far clip edge of the world.")

VideoOptionsPanel_OnLoad(panel)
)lua";

    // A second chunk: one literal cannot hold both.
    static const char pagesScript[] = R"lua(
if not VideoOptionsFrame or WowDecompOutlinePanel then return end

-- Esteria option pages: the unit outline, the loot beam and the performance overlay. Every control is a CVar, so Okay,
-- Cancel and Defaults treat the pages like any other video panel. Colours are RRGGBB boxes that apply at once.
function WowDecompBuildPage(name, title, subtitle)
    local page = CreateFrame("Frame", name, VideoOptionsFramePanelContainer)
    page:Hide()
    page.name = title
    page.options = {}
    page.checks = {}
    page.boxes = {}
    page.y = -4

    local heading = page:CreateFontString(nil, "ARTWORK", "GameFontNormalLarge")
    heading:SetPoint("TOPLEFT", 16, -16)
    heading:SetText(title)

    local sub = page:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    sub:SetPoint("TOPLEFT", heading, "BOTTOMLEFT", 0, -8)
    sub:SetPoint("RIGHT", -32, 0)
    sub:SetJustifyH("LEFT")
    sub:SetText(subtitle)

    local scroll = CreateFrame("ScrollFrame", name .. "Scroll", page, "UIPanelScrollFrameTemplate")
    scroll:SetPoint("TOPLEFT", 8, -60)
    scroll:SetPoint("BOTTOMRIGHT", -30, 8)
    local child = CreateFrame("Frame", name .. "Child", scroll)
    child:SetSize(500, 10)
    scroll:SetScrollChild(child)
    page.child = child

    page:SetScript("OnShow", function(self)
        for _, check in ipairs(self.checks) do
            check:SetChecked(GetCVar(check.cvar) == "1")
        end
        for _, box in ipairs(self.boxes) do
            box:SetText(GetCVar(box.cvar) or "")
            box:UpdateSwatch()
        end
    end)

    function page:Header(text)
        self.y = self.y - 10
        local fs = self.child:CreateFontString(nil, "ARTWORK", "GameFontNormal")
        fs:SetPoint("TOPLEFT", 8, self.y)
        fs:SetText(text)
        self.y = self.y - 24
    end

    function page:Check(cvar, text, tooltip)
        local check = CreateFrame("CheckButton", name .. "Check" .. cvar, self.child, "OptionsCheckButtonTemplate")
        check:SetPoint("TOPLEFT", 8, self.y)
        _G[check:GetName() .. "Text"]:SetText(text)
        check.type = CONTROLTYPE_CHECKBOX
        check.cvar = cvar
        check.tooltipText = tooltip
        check:SetScript("OnClick", function(self) BlizzardOptionsPanel_CheckButton_OnClick(self) end)
        self.options[cvar] = { text = text, tooltip = tooltip }
        BlizzardOptionsPanel_RegisterControl(check, self)
        self.checks[#self.checks + 1] = check
        self.y = self.y - 28
        return check
    end

    function page:Slider(cvar, text, tooltip, low, high, step, format, wheel)
        local sliderName = name .. "Slider" .. cvar
        local slider = CreateFrame("Slider", sliderName, self.child, "OptionsSliderTemplate")
        slider:SetSize(280, 17)
        slider:SetPoint("TOPLEFT", 12, self.y - 14)
        slider.type = CONTROLTYPE_SLIDER
        slider.cvar = cvar
        slider.SetDisplayValue = slider.SetValue
        slider.SetValue = function(self, value)
            self:SetDisplayValue(value)
            BlizzardOptionsPanel_SetCVarSafe(self.cvar, value)
        end
        _G[sliderName .. "Text"]:SetText(text)
        _G[sliderName .. "Low"]:SetText("")
        _G[sliderName .. "High"]:SetText("")

        local readout = CreateFrame("EditBox", sliderName .. "Edit", slider, "InputBoxTemplate")
        readout:SetSize(70, 20)
        readout:SetPoint("LEFT", slider, "RIGHT", 16, 0)
        readout:SetAutoFocus(false)
        readout:SetJustifyH("CENTER")
        readout:SetScript("OnEnterPressed", function(box)
            local value = tonumber(box:GetText())
            if value then slider:SetDisplayValue(value) end
            box:ClearFocus()
            box:SetText(format(slider:GetValue()))
        end)
        readout:SetScript("OnEscapePressed", function(box)
            box:ClearFocus()
            box:SetText(format(slider:GetValue()))
        end)

        slider:EnableMouseWheel(true)
        slider:SetScript("OnMouseWheel", function(self, delta)
            self:SetDisplayValue(self:GetValue() + delta * (wheel or step))
        end)
        slider:SetScript("OnValueChanged", function(self, value)
            self.newValue = value
            if not readout:HasFocus() then readout:SetText(format(value)) end
        end)
        slider:SetScript("OnShow", function(self)
            self:SetDisplayValue(tonumber(GetCVar(self.cvar)) or low)
        end)
        slider:SetMinMaxValues(low, high)
        slider:SetValueStep(step)
        self.options[cvar] = { text = text, minValue = low, maxValue = high, valueStep = step, tooltip = tooltip }
        BlizzardOptionsPanel_RegisterControl(slider, self)
        self.y = self.y - 46
        return slider
    end

    function page:Color(cvar, text, tooltip)
        local label = self.child:CreateFontString(nil, "ARTWORK", "GameFontHighlight")
        label:SetPoint("TOPLEFT", 12, self.y - 4)
        label:SetText(text)

        local box = CreateFrame("EditBox", name .. "Color" .. cvar, self.child, "InputBoxTemplate")
        box:SetSize(70, 20)
        box:SetPoint("TOPLEFT", 230, self.y)
        box:SetAutoFocus(false)
        box:SetMaxLetters(6)
        box.cvar = cvar

        local swatch = self.child:CreateTexture(nil, "ARTWORK")
        swatch:SetSize(18, 18)
        swatch:SetPoint("LEFT", box, "RIGHT", 10, 0)
        swatch:SetTexture(1, 1, 1)

        function box:UpdateSwatch()
            local hex = self:GetText()
            if string.match(hex, "^%x%x%x%x%x%x$") then
                swatch:SetTexture(tonumber(string.sub(hex, 1, 2), 16) / 255, tonumber(string.sub(hex, 3, 4), 16) / 255, tonumber(string.sub(hex, 5, 6), 16) / 255)
            end
        end
        box:SetScript("OnTextChanged", function(self) self:UpdateSwatch() end)
        box:SetScript("OnEnterPressed", function(self)
            local hex = string.upper(string.gsub(self:GetText(), "#", ""))
            if string.match(hex, "^%x%x%x%x%x%x$") then
                SetCVar(self.cvar, hex)
            else
                self:SetText(GetCVar(self.cvar) or "")
            end
            self:ClearFocus()
        end)
        box:SetScript("OnEscapePressed", function(self)
            self:SetText(GetCVar(self.cvar) or "")
            self:ClearFocus()
        end)
        self.boxes[#self.boxes + 1] = box
        self.y = self.y - 28
        return box
    end

    function page:Done()
        self.child:SetHeight(-self.y + 16)
        VideoOptionsPanel_OnLoad(self)
    end

    return page
end

)lua";

    static const char pagesScript2[] = R"lua(
if not VideoOptionsFrame or WowDecompOutlinePanel then return end
local BuildPage = WowDecompBuildPage

local function Pixels(v) return string.format("%d px", v + 0.5) end
local function Yards(v) return string.format("%.1f yd", v) end
local function Percent(v) return string.format("%d%%", v * 100 + 0.5) end
local function Seconds(v) return string.format("%.2f s", v) end
local function Plain(v) return string.format("%.2f", v) end
local function Count(v) return string.format("%d", v + 0.5) end

local TierNames = { [0] = "Money only", "Poor", "Common", "Uncommon", "Rare", "Epic", "Legendary", "Artifact", "Heirloom" }

-- Unit outline
do
    local p = BuildPage("WowDecompOutlinePanel", "Esteria Outline", "Outlines your target and the unit under the mouse in its reaction colour.")
    p:Header("Unit outline")
    p:Check("unitOutline", "Enable unit outline", "Master switch for the unit outline.")
    p:Check("unitOutlineTarget", "Outline your target", "Outlines the unit you have targeted.")
    p:Check("unitOutlinePetTarget", "Outline your pet's target", "Outlines whatever your pet is attacking.")
    p:Check("unitOutlineMouseover", "Outline the unit under the mouse", "Outlines the unit the cursor is over.")
    p:Check("unitOutlineObjects", "Outline doors, chests and other objects", "Outlines interactive objects the cursor is over.")
    p:Check("unitOutlineQuestGivers", "Outline quest givers and flight masters", "Keeps quest givers with a quest for you, and flight masters, outlined without selecting them.")
    p:Header("Look")
    p:Slider("unitOutlineWidth", "Width", "Width of the line in pixels.", 1, 6, 1, Pixels, 1)
    p:Slider("unitOutlineOpacity", "Opacity", "Strength of the line.", 0.1, 1, 0.05, Percent, 0.05)
    p:Check("unitOutlineThroughWalls", "Show through terrain and buildings", "Off hides the line where something stands between you and the unit.")
    p:Check("unitOutlineSettleFade", "Dim the line after a moment", "A selected unit's line starts at full strength and settles to a dimmer one.")
    p:Slider("unitOutlineSettleOpacity", "Settled opacity", "How strong the line is once it has settled.", 0.1, 1, 0.05, Percent, 0.05)
    p:Header("Colours (RRGGBB, press Enter to apply)")
    p:Color("unitOutlineHostile", "Hostile")
    p:Color("unitOutlineNeutral", "Neutral")
    p:Color("unitOutlineFriendly", "Friendly")
    p:Color("unitOutlineFriendlyPlayer", "Friendly player")
    p:Color("unitOutlineQuestGiver", "Quest giver / flight master")
    p:Color("unitOutlineObject", "Object")
    p:Done()
end

-- Loot beam
do
    local p = BuildPage("WowDecompLootBeamPanel", "Esteria Loot Beam", "A beam of light over lootable corpses, tinted by the best item in the loot. A marker only; it sends nothing to the server.")
    p:Header("Loot beam")
    p:Check("lootBeam", "Enable loot beam", "Master switch for the loot beam.")
    p:Check("lootBeamShowBeam", "Draw the light shaft", "Off leaves only the sparkles.")
    p:Check("lootBeamSparkles", "Draw sparkles", "Drifting motes of light around the beam.")
    p:Check("lootBeamThroughWalls", "Show through terrain and buildings", "Off hides the beam behind terrain and buildings.")
    p:Slider("lootBeamMinQuality", "Lowest loot tier with a beam", "Corpses whose best loot is below this tier get no beam. 0 shows money-only corpses too.", 0, 8, 1, function(v) return TierNames[math.floor(v + 0.5)] or "?" end, 1)
    p:Slider("lootBeamMaxDistance", "Maximum distance", "Corpses further away than this get no beam. 0 is unlimited.", 0, 300, 5, function(v) if v < 1 then return "Unlimited" end return string.format("%d yd", v + 0.5) end, 5)
    p:Header("Shaft")
    p:Slider("lootBeamHeight", "Height", "How high the beam rises.", 2, 60, 1, Yards, 1)
    p:Slider("lootBeamBaseOffset", "Base height", "How far above the corpse the beam starts.", 0, 5, 0.1, Yards, 0.1)
    p:Slider("lootBeamWidth", "Width", "Half-width of the beam at its base.", 0.1, 3, 0.05, Yards, 0.05)
    p:Slider("lootBeamMinWidth", "Width kept at distance", "Minimum width per yard of camera distance, so far beams stay visible.", 0, 0.05, 0.002, Plain, 0.002)
    p:Slider("lootBeamAlpha", "Opacity", "Strength of the beam.", 0.05, 1, 0.05, Percent, 0.05)
    p:Slider("lootBeamPulse", "Pulse depth", "How much the beam pulses. 0 is steady.", 0, 1, 0.05, Percent, 0.05)
    p:Slider("lootBeamPulseSpeed", "Pulse speed", "How fast the beam pulses.", 0, 6, 0.1, Plain, 0.1)
    p:Slider("lootBeamFadeIn", "Fade-in time", "How long a new beam takes to appear.", 0, 3, 0.05, Seconds, 0.05)
    p:Slider("lootBeamFadeOut", "Fade-out time", "How long a beam takes to disappear once the corpse is looted.", 0, 5, 0.05, Seconds, 0.05)
    p:Header("Sparkles")
    p:Slider("lootBeamSparkleCount", "Count", "Sparkles per beam.", 0, 32, 1, Count, 1)
    p:Slider("lootBeamSparkleSize", "Size", "Size of each sparkle.", 0.01, 0.25, 0.01, Plain, 0.01)
    p:Slider("lootBeamSparkleAlpha", "Opacity", "Strength of the sparkles.", 0.05, 1, 0.05, Percent, 0.05)
    p:Slider("lootBeamSparkleRise", "Rise speed", "How fast sparkles climb.", 0, 5, 0.1, Plain, 0.1)
    p:Slider("lootBeamSparkleDrift", "Sideways drift", "How far sparkles wander sideways.", 0, 1, 0.01, Plain, 0.01)
    p:Slider("lootBeamSparkleLife", "Lifetime", "How long a sparkle lasts.", 0.3, 8, 0.1, Seconds, 0.1)
    p:Slider("lootBeamSparkleTwinkle", "Twinkle speed", "How fast sparkles flicker.", 0, 20, 0.5, Plain, 0.5)
    p:Header("Colours (RRGGBB, press Enter to apply)")
    p:Color("lootBeamColor", "Unknown loot")
    for tier = 0, 8 do
        p:Color("lootBeamTier" .. tier, TierNames[tier])
    end
    p:Done()
end

-- Performance overlay
do
    local p = BuildPage("WowDecompOverlayPanel", "Esteria Overlay", "A small movable overlay with frame rate, frame time, draw calls, triangles and CPU use.")
    p:Header("Performance overlay")
    p:Check("showPerformance", "Show the overlay", "Drag the overlay with the left mouse button to move it.")
    p:Check("perfOverlayFps", "Frame rate", "Frames per second, averaged over 30 frames.")
    p:Check("perfOverlayFrameTime", "Frame time", "Milliseconds per frame, taken from the averaged frame rate.")
    p:Check("perfOverlayDrawCalls", "Draw calls", "Draw calls submitted last frame, including the outline's extra passes.")
    p:Check("perfOverlayTriangles", "Triangles", "Triangles submitted last frame.")
    p:Check("perfOverlayCpu", "CPU use", "This process's share of the whole machine's CPU.")
    p:Header("Look")
    p:Slider("perfOverlayScale", "Size", "Scale of the overlay.", 0.5, 2, 0.05, Percent, 0.05)
    p:Slider("perfOverlayAlpha", "Opacity", "Opacity of the overlay.", 0.2, 1, 0.05, Percent, 0.05)
    p:Slider("perfOverlayInterval", "Update interval", "How often the numbers refresh.", 0.1, 2, 0.05, Seconds, 0.05)
    p:Check("perfOverlayBackground", "Dark background", "Draws a dark panel behind the numbers.")
    p:Check("perfOverlayLocked", "Lock position", "Stops the overlay from being dragged.")
    p:Done()
end

)lua";

    // Tree and bush wind (Engine/Source/Model2/M2TreeWind.cpp).
    static const char treeWindScript[] = R"lua(
if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompTreeWindPanel then return end

local function Degrees(v) return string.format("%.2f deg", v) end
local function Heading(v) return string.format("%d deg", v + 0.5) end
local function Speed(v) return string.format("%.1f yd/s", v) end
local function Yards(v) return string.format("%.1f yd", v) end
local function Plain(v) return string.format("%.2f", v) end
local function Limit(v) if v < 1 then return "Unlimited" end return string.format("%d yd", v + 0.5) end

local function WindRows(p, prefix, what, one)
    p:Slider(prefix .. "Direction", "Wind direction", "Heading the wind blows toward, in the world.", 0, 360, 1, Heading, 5)
    p:Slider(prefix .. "Speed", "Wave speed", "How fast the gusts travel across the " .. what .. ".", 0, 12, 0.1, Speed, 0.1)
    p:Slider(prefix .. "Amplitude", "Sway", "How far the top sways. Keep it under about 2 degrees for a breeze.", 0, 6, 0.05, Degrees, 0.05)
    p:Slider(prefix .. "Wavelength", "Wave length", "Longer waves make broader, slower gusts.", 2, 80, 0.5, Yards, 1)
    p:Slider(prefix .. "Lean", "Downwind lean", "A constant lean with the wind on top of the sway.", 0, 4, 0.05, Degrees, 0.05)
    p:Slider(prefix .. "CrossAmplitude", "Cross-swell", "A second, smaller wave across the wind, so the motion is not a single rhythm.", 0, 3, 0.01, Degrees, 0.05)
    p:Slider(prefix .. "CrossWavelength", "Cross-swell length", "Length of the second wave.", 2, 40, 0.5, Yards, 1)
    p:Slider(prefix .. "CrossAngle", "Cross-swell angle", "Heading of the second wave from the wind.", 0, 180, 1, Heading, 5)
    p:Slider(prefix .. "Variance", "Variety", "How much the sway differs from one " .. one .. " to the next. 0 makes them all alike.", 0, 1, 0.05, Plain, 0.05)
    p:Slider(prefix .. "Gust", "Gusts", "Depth of the slow rise and fall of the wind. 0 is steady.", 0, 1, 0.05, Plain, 0.05)
    p:Slider(prefix .. "DistanceFade", "Fade with distance", "Sway lost per yard from the camera. 0 sways far ones as much as near ones.", 0, 0.05, 0.001, function(v) return string.format("%.3f", v) end, 0.001)
end

local p = WowDecompBuildPage("WowDecompTreeWindPanel", "Esteria Tree Wind", "Placed trees and bushes sway gently in the wind. Purely visual: collision and selection do not move.")
p:Header("Trees")
p:Check("treeWind", "Trees sway", "Master switch for the trees.")
p:Check("treeWindFollowWorld", "Follow the world wind", "Where WindProfile.cdbc gives a place wind, its heading, strength and passing gusts drive the trees and bushes; calm places stand still. Elsewhere the sliders below apply as they are.")
p:Slider("treeWindWorldScale", "World wind response", "Sway per unit of world wind. At 2, a half-strength wind gives the sway set below.", 0, 10, 0.1, Plain, 0.1)
WindRows(p, "treeWind", "trees", "tree")
p:Header("What counts as a tree")
p:Check("treeWindDoodadsOnly", "Placed objects only", "Only trees placed in the map sway, never units, weapons or spell effects.")
p:Check("treeWindMatchNames", "Match tree names", "The model's name must name a tree (tree, pine, oak, willow and so on).")
p:Check("treeWindMatchTallThin", "Match tall models", "The model must also be tall and narrow. Off by default: names alone decide.")
p:Check("treeWindExcludeDead", "Skip dead and bare trees", "Dead, burnt, bare and felled trees, stumps and logs stay still.")
p:Slider("treeWindMinHeight", "Least height", "Tall-model test: the model must be at least this tall.", 0, 40, 0.5, Yards, 1)
p:Slider("treeWindMinAspect", "Least height to width", "Tall-model test: height divided by the widest side.", 0.5, 6, 0.05, Plain, 0.1)
p:Slider("treeWindMaxHeight", "Greatest height", "Trees taller than this keep still, so giant world trees do not lean across the zone. 0 is unlimited.", 0, 300, 5, Limit, 5)
p:Slider("treeWindMaxDistance", "Greatest distance", "Trees further away than this keep still. 0 is unlimited.", 0, 500, 5, Limit, 5)
p:Header("Bushes")
p:Check("bushWind", "Bushes sway", "Bushes, shrubs, ferns and hedges, matched by name only: the tree size tests never apply.")
WindRows(p, "bushWind", "bushes", "bush")
p:Check("bushWindDoodadsOnly", "Placed objects only", "Only bushes placed in the map sway.")
p:Check("bushWindExcludeDead", "Skip dead and bare bushes", "Dead, burnt and bare bushes stay still.")
p:Slider("bushWindMaxDistance", "Greatest distance", "Bushes further away than this keep still. 0 is unlimited.", 0, 500, 5, Limit, 5)
p:Done()
)lua";

    // Snow, sand and ash lying on the ground (WorldClient/SurfaceCover.cpp).
    static const char surfaceCoverScript[] = R"lua(
if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompSurfaceCoverPanel then return end

local function Times(v) return string.format("%.2fx", v) end
local function Plain(v) return string.format("%.2f", v) end
local function Yards(v) return string.format("%.2f yd", v) end

local p = WowDecompBuildPage("WowDecompSurfaceCoverPanel", "Esteria Snow Cover", "Painted snow lies on the ground as a layer of its own, from SurfaceCover.cdbc: a few inches deep on flat ground, thinning on slopes, burying the grass, lit and shadowed like the ground. Near you it is lumpy and takes trenches.")
p:Header("Cover")
p:Check("surfaceCover", "Snow lies on the ground", "Master switch. Needs its shaders translated once on Vulkan (surfaceCoverInfo says so).")
p:Check("surfaceCoverOthers", "Sand and ash too", "The table's other covers: a thin layer of sand in deserts, ash in burnt lands. Off: only snow.")
p:Slider("surfaceCoverDepth", "Depth", "Multiplier on how deep the table says the cover lies. 1 is as the table says.", 0, 3, 0.05, Times, 0.05)
p:Slider("surfaceCoverBreakup", "Ragged edges", "Multiplier on how patchy the cover's edges are. 0 gives smooth, rounded edges.", 0, 3, 0.05, Times, 0.05)
p:Slider("surfaceCoverBrightness", "Brightness", "How bright the cover is under the same light as the ground.", 0.2, 1.5, 0.01, Plain, 0.02)
p:Slider("surfaceCoverDistanceLift", "Lift far away", "Far away the cover is lifted this much so it never flickers with the ground under it. Keep it small: more makes distant snow float.", 0, 3, 0.05, Yards, 0.05)
p:Header("Near you")
p:Check("surfaceCoverNear", "Fine snow around you", "A detailed, lumpy patch of cover 48 yards across follows you, lit by its own bumps. Off: the coarser cover everywhere, and no trenches.")
p:Check("surfaceCoverTrenches", "Trenches", "You and the creatures and players near you press trenches into the cover, with pushed-up rims. They fill back in over time.")
p:Slider("surfaceCoverTrenchScale", "Trench width", "Width of a trench as a multiple of each one's size. Mounts make wider ones.", 0.2, 4, 0.05, Times, 0.05)
p:Done()
)lua";

    // The world wind and the grass (WorldClient/LivingWind.cpp, WorldClient/HD/HdGrassWind.cpp).
    static const char worldWindScript[] = R"lua(
if not VideoOptionsFrame or not WowDecompBuildPage or WowDecompWorldWindPanel then return end

local function Plain(v) return string.format("%.2f", v) end
local function Times(v) return string.format("%.2fx", v) end
local function Heading(v) return string.format("%d deg", v + 0.5) end
local function Speed(v) return string.format("%.1f yd/s", v) end
local function Yards(v) return string.format("%.1f yd", v) end
local function Distance(v) return string.format("%d yd", v + 0.5) end

local p = WowDecompBuildPage("WowDecompWorldWindPanel", "Esteria World Wind", "Wind per zone from WindProfile.cdbc: stronger in storms, with gusts that travel across the land, calm under roofs and behind walls. It moves the grass and the trees.")
p:Header("World wind")
p:Check("worldWind", "Enable the world wind", "Off: the trees and the grass use their own fixed wind (the sliders on their pages).")
p:Slider("worldWindStrength", "Strength", "Your multiplier on every place's wind. 1 is as the table says.", 0, 3, 0.05, Times, 0.05)
p:Check("worldWindShelter", "Shelter under roofs", "Roofs and overhangs keep the ground below calm.")
p:Check("worldWindLee", "Wind shadow behind walls", "Walls, cliffs and steep rock calm the air on their downwind side.")
p:Header("Grass")
p:Check("hdGrassWind", "Grass sways", "Grass moves in the wind, in every visuals mode. Outside Azeroth Remastered the grass keeps the game's own lighting.")
p:Check("hdGrassWindFollowWorld", "Follow the world wind", "Where the world wind blows, it sets the grass's heading and strength, and gusts pass over it.")
p:Slider("hdGrassWindStrength", "Sway", "Multiplier on the grass sway.", 0, 5, 0.05, Times, 0.05)
p:Slider("hdGrassWindSpeed", "Wave speed", "How fast the waves cross the grass.", 0, 12, 0.1, Speed, 0.1)
p:Slider("hdGrassWindDirection", "Fixed wind direction", "Heading of the grass wind where the world wind does not blow.", 0, 360, 1, Heading, 5)
p:Check("hdGrassReact", "Grass parts around you", "Grass bends away from your character.")
p:Slider("hdGrassReactRadius", "Parting radius", "How far around you the grass bends away.", 0.1, 20, 0.1, Yards, 0.5)
p:Check("hdGrassReactUnits", "Grass parts around others", "Creatures and other players near you part the grass too, each by its own size.")
p:Slider("hdGrassReactUnitScale", "Their parting size", "Parting radius around a creature or player, as a multiple of its size.", 0.5, 5, 0.1, Times, 0.1)
p:Slider("hdGrassReactMountedScale", "Wider when mounted", "How much wider the parting is around anyone mounted, you included.", 1, 5, 0.1, Times, 0.1)
p:Slider("groundEffectDist", "Grass distance", "How far grass is drawn. The game's own limit of 140 yards is lifted. Grass in range is baked once and kept on the GPU, up to the memory below.", 0, 1024, 5, Distance, 10)
p:Slider("grassBufferMemory", "Grass memory", "Most memory the kept grass may use. Grass past it is baked again every frame, which costs CPU time. Takes effect when the grass distance changes or the world loads.", 16, 4096, 16, function(v) return string.format("%d MB", v + 0.5) end, 64)
p:Header("Grass density")
p:Check("grassDensity", "Denser grass near you", "Extra plants on each plant's own patch of ground, as GroundEffectDoodadDensity.cdbc asks for each place and plant. They share the grass memory, so the grass reaches less far.")
p:Slider("grassDensityMaxMultiplier", "Most plants per plant", "Your limit on how dense grass gets. 1 is never denser.", 1, 8, 0.5, Times, 0.5)
p:Slider("grassDensityMaxRadius", "Dense radius", "Your limit on how far around you grass is made denser.", 0, 500, 5, Distance, 10)
p:Slider("grassDensityMaxSpread", "Spread", "Your limit on how far an extra plant may sit from its plant.", 0, 10, 0.1, Yards, 0.5)
p:Header("Far grass")
p:Check("grassThinning", "Thin far grass", "Far away, grass draws an even sample of its plants: much less GPU time at long grass distances, hardly visible.")
p:Slider("grassThinStart", "Full grass within", "Every plant is drawn this close to you.", 0, 1000, 5, Distance, 10)
p:Slider("grassThinEnd", "Thinnest from", "Where the thinning reaches its lowest share.", 0, 2000, 10, Distance, 25)
p:Slider("grassThinMinShare", "Thinnest share", "Share of plants drawn at that distance and beyond.", 0.05, 1, 0.05, function(v) return string.format("%d%%", v * 100 + 0.5) end, 0.05)
p:Done()
)lua";

    static const char overlayScript[] = R"lua(
-- The overlay itself. Created hidden and shown by the showPerformance CVar; a hidden frame runs no OnUpdate, so it
-- costs nothing while it is off. The position is saved in perfOverlayPos when it is dropped.
if not WowDecompPerfOverlay then
    local f = CreateFrame("Frame", "WowDecompPerfOverlay", UIParent)
    f:SetFrameStrata("LOW")
    f:SetSize(190, 78)
    f:SetMovable(true)
    f:SetClampedToScreen(true)
    f:EnableMouse(true)
    f:RegisterForDrag("LeftButton")
    f:SetBackdrop({
        bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
        edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 4, right = 4, top = 4, bottom = 4 },
    })
    local text = f:CreateFontString(nil, "ARTWORK", "GameFontHighlightSmall")
    text:SetPoint("TOPLEFT", 8, -8)
    text:SetJustifyH("LEFT")

    f:SetScript("OnDragStart", function(self)
        if GetCVar("perfOverlayLocked") ~= "1" then self:StartMoving() end
    end)
    f:SetScript("OnDragStop", function(self)
        self:StopMovingOrSizing()
        local x, y = self:GetCenter()
        local scale = self:GetEffectiveScale() / UIParent:GetEffectiveScale()
        SetCVar("perfOverlayPos", string.format("%.0f,%.0f", x * scale, y * scale))
    end)

    local function Number(v)
        if v >= 1000000 then return string.format("%.2fM", v / 1000000) end
        if v >= 1000 then return string.format("%.1fk", v / 1000) end
        return string.format("%d", v)
    end

    local function Look()
        local scale = tonumber(GetCVar("perfOverlayScale")) or 1
        f:SetScale(scale)
        f:SetAlpha(tonumber(GetCVar("perfOverlayAlpha")) or 0.8)
        local shown = GetCVar("perfOverlayBackground") ~= "0"
        f:SetBackdropColor(0, 0, 0, shown and 0.6 or 0)
        f:SetBackdropBorderColor(1, 1, 1, shown and 0.4 or 0)
        f:ClearAllPoints()
        local x, y = string.match(GetCVar("perfOverlayPos") or "", "^(-?%d+),(-?%d+)$")
        if x then
            f:SetPoint("CENTER", UIParent, "BOTTOMLEFT", tonumber(x) / scale, tonumber(y) / scale)
        else
            f:SetPoint("TOPRIGHT", UIParent, "TOPRIGHT", -8, -8)
        end
    end

    local elapsed = 1
    f:SetScript("OnUpdate", function(self, dt)
        elapsed = elapsed + dt
        if elapsed < (tonumber(GetCVar("perfOverlayInterval")) or 0.25) then return end
        elapsed = 0
        local fps, ms, draws, tris, cpu = GetPerformanceStats()
        local lines = {}
        if GetCVar("perfOverlayFps") ~= "0" then lines[#lines + 1] = string.format("FPS %.1f", fps) end
        if GetCVar("perfOverlayFrameTime") ~= "0" then lines[#lines + 1] = string.format("Frame %.2f ms", ms) end
        if GetCVar("perfOverlayDrawCalls") ~= "0" then lines[#lines + 1] = string.format("Draw %d", draws) end
        if GetCVar("perfOverlayTriangles") ~= "0" then lines[#lines + 1] = "Tris " .. Number(tris) end
        if GetCVar("perfOverlayCpu") ~= "0" then lines[#lines + 1] = string.format("CPU %.1f%%", cpu) end
        if #lines == 0 then lines[1] = "(nothing selected)" end
        text:SetText(table.concat(lines, "\n"))
        f:SetHeight(#lines * 12 + 18)
    end)

    local function Apply()
        if GetCVar("showPerformance") == "1" then
            Look()
            elapsed = 1
            f:Show()
        else
            f:Hide()
        end
    end
    hooksecurefunc("SetCVar", function(name)
        if name and string.find(string.lower(name), "^perfoverlay") then Look() end
        if name and string.lower(name) == "showperformance" then Apply() end
    end)
    f:RegisterEvent("PLAYER_ENTERING_WORLD")
    f:SetScript("OnEvent", Apply)
    Apply()
end
)lua";
    // The Effects page's "Visuals" dropdown: which visual pack draws the world (the `visuals` cvar, see Ui/Visuals.h).
    // It is a stock-style control, so Okay, Cancel and Defaults treat it like the sliders beside it. It sits in the
    // empty cells left of Player Textures.
    static const char visualsScript[] = R"lua(
if not VideoOptionsFrame or not VideoOptionsEffectsPanelPlayerTexture or WowDecompVisualsDropDown then return end

local panel = VideoOptionsEffectsPanel
local choices = {
    { value = "0", text = "Stock (Vulkan)" },
    { value = "1", text = "Lumenveil" },
    { value = "2", text = "Azeroth Remastered" },
}

local dd = CreateFrame("Frame", "WowDecompVisualsDropDown", panel, "UIDropDownMenuTemplate")
dd:SetPoint("TOPLEFT", VideoOptionsEffectsPanelPlayerTexture, "TOPLEFT", -284, 8)
UIDropDownMenu_SetWidth(dd, 190)

local function NameOf(value)
    for _, choice in ipairs(choices) do
        if choice.value == value then return choice.text end
    end
    return choices[2].text
end

local function Show(self, value)
    self.shown = value
    UIDropDownMenu_SetSelectedValue(self, value)
    UIDropDownMenu_SetText(self, "Visuals: " .. NameOf(value))
end

UIDropDownMenu_Initialize(dd, function()
    for _, choice in ipairs(choices) do
        local info = {}
        info.text = choice.text
        info.value = choice.value
        info.checked = dd.shown == choice.value
        info.func = function(button)
            dd.newValue = button.value
            Show(dd, button.value)
            if VideoOptionsFrameApply then VideoOptionsFrameApply:Enable() end
        end
        UIDropDownMenu_AddButton(info)
    end
end)

dd.type = CONTROLTYPE_DROPDOWN
dd.defaultValue = "1"
dd.GetValue = function() return GetCVar("visuals") end
dd.SetValue = function(self, value)
    self.value = value
    if GetCVar("visuals") ~= value then SetCVar("visuals", value) end
    Show(self, value)
end
dd.RefreshValue = function(self)
    self.value = GetCVar("visuals") or "1"
    self.newValue = nil
    Show(self, self.value)
end
BlizzardOptionsPanel_RegisterControl(dd, panel)
dd:RefreshValue()
)lua";

    FrameScript_Execute(script, "NativeDistanceOptions", nullptr);
    FrameScript_Execute(visualsScript, "NativeVisualsDropDown", nullptr);
    FrameScript_Execute(pagesScript, "NativeEsteriaPageHelpers", nullptr);
    FrameScript_Execute(pagesScript2, "NativeEsteriaPages", nullptr);
    FrameScript_Execute(treeWindScript, "NativeTreeWindPage", nullptr);
    FrameScript_Execute(worldWindScript, "NativeWorldWindPage", nullptr);
    FrameScript_Execute(surfaceCoverScript, "NativeSurfaceCoverPage", nullptr);
    FrameScript_Execute(overlayScript, "NativeEsteriaOverlay", nullptr);
    // Lumenveil's pages, built from its settings window (Engine/Source/Lumenveil/OptionsPages.cpp).
    const std::string lumenveilScript = LumenveilOptions::BuildScript();
    FrameScript_Execute(lumenveilScript.c_str(), "NativeLumenveilPages", nullptr);
}
