"""Runs creator_skin.lua against a fake glue widget layer and checks the three-faction race page."""
import sys
from lupa.lua51 import LuaRuntime

SKIN = sys.argv[1] if len(sys.argv) > 1 else str(__import__("pathlib").Path(__file__).with_name("creator_skin.lua"))
HARNESS = r'''
created = {}
local W = {}
W.__index = function(self, k)
    local m = rawget(W, k)
    if m or not string.find(k, "^%u") then return m end
    return function() end   -- any other widget method is a no-op
end
function W.new(name, parent, kind)
    local o = setmetatable({ _name = name, _parent = parent, _kind = kind or "Frame", _shown = true, _scripts = {},
        _w = 10, _h = 10, _points = {} }, W)
    if name then _G[name] = o end
    created[#created + 1] = o
    return o
end
function W:Show() self._shown = true end
function W:Hide() self._shown = false end
function W:IsShown() return self._shown end
function W:IsVisible()
    local o = self
    while o do if not o._shown then return false end o = o._parent end
    return true
end
function W:SetSize(w, h) self._w, self._h = w, h end
function W:SetWidth(w) self._w = w end
function W:SetHeight(h) self._h = h end
function W:GetWidth() return self._w end
function W:GetHeight() return self._h end
function W:ClearAllPoints() self._points = {} end
function W:SetPoint(p, rel, rp, x, y) self._points[#self._points + 1] = { p, rel, rp, x or 0, y or 0 } end
function W:SetScript(k, f) self._scripts[k] = f end
function W:GetScript(k) return self._scripts[k] end
function W:HookScript(k, f) self._scripts[k] = f end
function W:GetName() return self._name end
function W:GetObjectType() return self._kind end
function W:GetRegions() return end
function W:GetChecked() return self._checked end
function W:SetChecked(v) self._checked = v end
function W:SetText(t) self._text = t end
function W:GetText() return self._text end
function W:CreateTexture(name) return W.new(name, self, "Texture") end
function W:CreateFontString(name) return W.new(name, self, "FontString") end
function W:GetNormalTexture() self._nt = self._nt or W.new(nil, self, "Texture") return self._nt end
function W:GetPushedTexture() self._pt = self._pt or W.new(nil, self, "Texture") return self._pt end
function W:GetHighlightTexture() return nil end
function W:GetCheckedTexture() return nil end
function W:GetFrameLevel() return 1 end
function W:GetStringWidth() return 100 end
function W:SetTexCoord(a, b, c, d, e, f, g, h)
    if e == nil then self._tc = { a, c, a, d, b, c, b, d } else self._tc = { a, b, c, d, e, f, g, h } end
end
function W:GetTexCoord() local c = self._tc or { 0, 0, 0, 1, 1, 0, 1, 1 } return unpack(c) end
function W:IsEnabled() return true end
function CreateFrame(kind, name, parent) return W.new(name, parent, kind) end
function PlaySound() end
function BodyShapeLog(t) print(t) end
strupper = string.upper
SEX_MALE, SEX_FEMALE = 2, 3
function GetSelectedSex() return SEX_FEMALE end
function EA_IS_RETAIL() return false end
function CycleCharCustomization() return 0 end
ALLIANCE, HORDE = "Alliance", "Horde"

CharacterCreateFrame = W.new("CharacterCreateFrame", nil)
CharacterCreateFrame:SetSize(1024, 768)
CharacterCreate = CharacterCreateFrame
CharacterCreate.personalizationMode = false
CharacterCreateRaceButtonsContainer = W.new("CharacterCreateRaceButtonsContainer", CharacterCreateFrame)
for _, n in ipairs({ "CharCreateOkayButton", "CharCreateBackButton", "CharacterCreateNameEdit", "CharacterCreateLastNameEdit",
                     "CharacterCreateGenderButtonMale", "CharacterCreateGenderButtonFemale", "CharacterCreateFreebornButton",
                     "CharacterCreateRotateLeft", "CharacterCreateRotateRight", "CharCreateRandomizeButton",
                     "CustomizationLogoAlliance", "CustomizationLogoHorde", "CustomizationTextAlliance",
                     "CustomizationTextHorde", "AllianceLogoFrame", "HordeLogoFrame" }) do
    W.new(n, CharacterCreateFrame, "Button")
end
CharCreateOkayButton:Hide()  -- not personalizing

-- 22 Alliance, 19 Horde, 13 Freeborn picks + 6 hidden twins, roughly as the live client enumerates them.
files = {}
for i = 1, 22 do files[#files + 1] = { "A" .. i, "Alliance" } end
for i = 1, 19 do files[#files + 1] = { "H" .. i, "Horde" } end
for _, f in ipairs({ "Vrykul", "ThinHuman", "OgreAlliance", "SethrakAlliance", "DrakonidAlliance", "SlimeGiantAlliance",
                     "CentaurAlliance", "EtherealAlliance" }) do files[#files + 1] = { f, "Alliance" } end
for _, f in ipairs({ "NagaHorde", "Eredar", "GnollHorde", "SaberonHorde", "ArakkoaHorde", "ThinHumanHorde", "OgreHorde",
                     "DrakonidHorde", "SlimeGiantHorde", "CentaurHorde", "EtherealHorde" }) do files[#files + 1] = { f, "Horde" } end
for i = 1, #files do
    local b = W.new("CharacterCreateRaceButton" .. i, CharacterCreateRaceButtonsContainer, "CheckButton")
    b._nt = W.new("CharacterCreateRaceButton" .. i .. "NormalTexture", b, "Texture")
    b._pt = W.new("CharacterCreateRaceButton" .. i .. "PushedTexture", b, "Texture")
    b.staticTexture = W.new(nil, b, "Texture"); b.highlightTexture = W.new(nil, b, "Texture"); b.checkedTexture = W.new(nil, b, "Texture")
end
selected = 3   -- an Alliance race
function GetSelectedRace() return selected end
function SetSelectedRace(i) selected = i end
function CharacterCreate_PositionRaceButtons() end
function CharacterCreate_PositionGenderButtons() end
function CharacterCreateEnumerateRaces()
    CharacterCreate.numRaces = #files
    CharacterCreate.raceIndicesAlliance, CharacterCreate.raceIndicesHorde = {}, {}
    for i, f in ipairs(files) do
        local b = _G["CharacterCreateRaceButton" .. i]
        b.raceFileString = f[1]; b.enable = true; b:Show(); b:SetChecked(i == selected and 1 or nil)
        b._nt:SetTexCoord(0, 1, 0, 1); b._pt:SetTexCoord(0, 1, 0, 1)   -- stock re-sets coords every enumeration
        table.insert(f[2] == "Alliance" and CharacterCreate.raceIndicesAlliance or CharacterCreate.raceIndicesHorde, i)
    end
end
'''

L = LuaRuntime()
L.execute(HARNESS)
L.execute(open(SKIN, encoding='utf-8').read())
results = L.execute(r'''
local out = {}
local function check(cond, msg) if not cond then error("FAIL: " .. msg, 2) end out[#out + 1] = "ok  " .. msg end
local function fire(kind)  -- run every OnUpdate (waiter, then the skin driver with enough elapsed time)
    for _, o in ipairs(created) do local f = o._scripts.OnUpdate if f then f(o, 1) end end
end
CharacterCreateEnumerateRaces()
fire(); fire()   -- install, then tick
check(SK_ok == nil, "loaded")
local function shown(lo, hi) local n = 0 for i = lo, hi do if _G["CharacterCreateRaceButton" .. i]:IsShown() then n = n + 1 end end return n end
local function anyX(lo, hi, fn) for i = lo, hi do local b = _G["CharacterCreateRaceButton" .. i]
    if b:IsShown() then local p = b._points[1] if not fn(p[4], p[5], b._w) then return false end end end return true end
local tabs = { CreatorSkinFactionAlliance, CreatorSkinFactionHorde, CreatorSkinFactionFreeborn }
local function tip(i) tabs[i]._scripts.OnEnter(tabs[i]) return CreatorSkinFactionRail.tip.text._text end
check(tabs[1] and tabs[2] and tabs[3], "three faction crests built")
check(CreatorSkinFactionRail:IsShown(), "faction rail shown on race page")
check(shown(1, 22) == 22 and shown(23, #files) == 0, "browsing Alliance (selected race's faction): only Alliance portraits")
check(tip(1):find("22 races") and tip(2):find("19 races") and tip(3):find("13 races"), "hover counts 22/19/13")
check(not CharacterCreateFreebornButton:IsShown(), "Freeborn toggle hidden")
-- every visible portrait is inside the left panel, under the headers, above the gender buttons
check(anyX(1, #files, function(x, y, w) return x - w / 2 >= 55 and x + w / 2 <= 395 and y <= -150 + 0.1 and y >= -768 + 140 end),
      "Alliance grid inside left panel at 1024 wide")
check(anyX(1, #files, function(x, y, w) return y + w / 2 <= -100 - 4 end), "portraits clear of the header rule")
local function mirrored(i) local c = { _G["CharacterCreateRaceButton" .. i .. "NormalTexture"]:GetTexCoord() } return c[1] == 1 and c[5] == 0 end
check(mirrored(23) and mirrored(42 + 8) and not mirrored(1) and not mirrored(42), "Horde-born portraits (incl. Freeborn Naga) mirrored, Alliance not")
CharacterCreateEnumerateRaces()
check(mirrored(23) and not mirrored(1), "mirroring does not stack across enumerations")
check(CreatorSkinRaceCard:IsShown() and CreatorSkinRaceCard._points[1][1] == "TOPRIGHT", "race card in right panel")

tabs[3]._scripts.OnClick(tabs[3])
check(shown(1, 41) == 0 and shown(42, 54) == 13 and shown(55, #files) == 0, "Freeborn tab: 13 Freeborn portraits, no twins")
check(selected == 3, "browsing does not change the selected race")
check(tip(3):find("FREEBORN") and tip(3):find("Hide races"), "hover names the faction; browsed crest offers to hide")
fire()
check(shown(42, 54) == 13 and shown(1, 41) == 0, "tick keeps only browsed portraits shown")
check(anyX(42, 54, function(x, y, w) return x + w / 2 <= 395 end), "Freeborn grid inside left panel")

-- selecting a Freeborn race keeps that faction browsed; re-enumeration (gender change) keeps the layout
selected = 45
CharacterCreateEnumerateRaces(); fire()
check(shown(42, 54) == 13 and shown(1, 41) == 0, "re-enumeration keeps Freeborn browsed")
check(CreatorSkinRaceCard.faction._text:find("FREEBORN"), "card shows FREEBORN for a Freeborn pick")

tabs[2]._scripts.OnClick(tabs[2])
check(shown(23, 41) == 19 and shown(1, 22) == 0 and shown(42, #files) == 0, "Horde tab: 19 Horde portraits")
check(CreatorSkinRaceCard.faction._text:find("FREEBORN"), "card still describes the selected (Freeborn) race")

-- widescreen: same panel-relative layout
CharacterCreateFrame:SetSize(1365, 768)
tabs[1]._scripts.OnClick(tabs[1])
check(anyX(1, 22, function(x, y, w) return x + w / 2 <= 395 end), "Alliance grid inside left panel at 1365 wide")
check(tabs[1]._points[1][4] + tabs[1]._w <= 55, "rail fits in the left margin")

-- the browsed crest slides the pane out and back
tabs[1]._scripts.OnClick(tabs[1]); fire()
check(shown(1, #files) == 0 and SK_slide_probe == nil, "browsed crest hides the pane and its portraits")
check(CreatorSkinFactionRail:IsShown(), "rail stays visible with the pane hidden")
tabs[3]._scripts.OnClick(tabs[3]); fire()
check(shown(42, 54) == 13, "another crest slides the pane back in with that faction")
tabs[3]._scripts.OnClick(tabs[3]); fire(); tabs[3]._scripts.OnClick(tabs[3]); fire()
check(shown(42, 54) == 13 and shown(1, 41) == 0, "hide then show again restores the grid")
tabs[3]._scripts.OnClick(tabs[3]); fire()

-- customization page hides the tabs
CharacterCreate.personalizationMode = true; CharCreateOkayButton:Show(); CharacterCreateRaceButtonsContainer:Hide()
fire()
check(not CreatorSkinFactionRail:IsShown(), "rail hidden on Appearance page")
return table.concat(out, "\n")
''')
print(results)
print("PASS")
