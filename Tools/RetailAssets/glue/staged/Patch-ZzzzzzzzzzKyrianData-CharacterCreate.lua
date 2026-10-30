-- ============================================================================
-- Autora: Noa
-- ============================================================================
CHARACTER_FACING_INCREMENT = 2;
MAX_RACES = 129;
MAX_CLASSES_PER_RACE = 10;

function CharacterCreate_IsMaleOnlyRace(race)
    return race == 58 or race == 59 or race == 63 or race == 64 or race == 69 or race == 70 or race == 71 or race == 72;
end
CHARACTER_CREATE_DEBUG_RACE_ENUMERATION = false;
NUM_CHAR_CUSTOMIZATIONS = 5;
MIN_CHAR_NAME_LENGTH = 2;
CHARACTER_CREATE_ROTATION_START_X = nil;
CHARACTER_ROTATION_INCREMENT = 90;
CHARACTER_CREATE_INITIAL_FACING = nil;

PAID_CHARACTER_CUSTOMIZATION = 1;
PAID_RACE_CHANGE = 2;
PAID_FACTION_CHANGE = 3;
PAID_SERVICE_CHARACTER_ID = nil;
PAID_SERVICE_TYPE = nil;

FACTION_BACKDROP_COLOR_TABLE = {
    ["Alliance"] = {0.5, 0.5, 0.5, 0.09, 0.09, 0.19},
    ["Horde"] = {0.5, 0.2, 0.2, 0.19, 0.05, 0.05},
};
FRAMES_TO_BACKDROP_COLOR = {
    "CharacterCreateCharacterRace",
    "CharacterCreateCharacterClass",
--	"CharacterCreateCharacterFaction",
    "CharacterCreateNameEdit",
    "CharacterCreateLastNameEdit",
};
RACE_ICON_TCOORDS = {
    ["HUMAN_MALE"] = {0, 0.125, 0, 0.25},
    ["DWARF_MALE"] = {0.125, 0.25, 0, 0.25},
    ["GNOME_MALE"] = {0.25, 0.375, 0, 0.25},
    ["NIGHTELF_MALE"] = {0.375, 0.5, 0, 0.25},
    ["DRAENEI_MALE"] = {0.5, 0.625, 0, 0.25},
    ["WORGEN_MALE"] = {0.625, 0.750, 0, 0.25},
    ["HIGHELF_MALE"] = {0.750, 0.875, 0, 0.25},
    ["BROKEN_MALE"] = {0.750, 0.875, 0.5, 0.625},
    ["TAUREN_MALE"] = {0, 0.125, 0.25, 0.5},
    ["SCOURGE_MALE"] = {0.125, 0.25, 0.25, 0.5},
    ["TROLL_MALE"] = {0.25, 0.375, 0.25, 0.5},
    ["ORC_MALE"] = {0.375, 0.5, 0.25, 0.5},
    ["BLOODELF_MALE"] = {0.5, 0.625, 0.25, 0.5},
    ["GOBLIN_MALE"] = {0.625, 0.750, 0.25, 0.5},
    ["MAGHAR_MALE"] = {0.750, 0.875, 0.25, 0.5},
    ["SETHRAK_MALE"] = {0.875, 1.0, 0.25, 0.5},
    ["HUMAN_FEMALE"] = {0, 0.125, 0.5, 0.75},
    ["DWARF_FEMALE"] = {0.125, 0.25, 0.5, 0.75},
    ["GNOME_FEMALE"] = {0.25, 0.375, 0.5, 0.75},
    ["NIGHTELF_FEMALE"] = {0.375, 0.5, 0.5, 0.75},
    ["DRAENEI_FEMALE"] = {0.5, 0.625, 0.5, 0.75},
    ["WORGEN_FEMALE"] = {0.625, 0.750, 0.5, 0.75},
    ["HIGHELF_FEMALE"] = {0.750, 0.875, 0.5, 0.75},
    ["BROKEN_FEMALE"] = {0.750, 0.875, 0.625, 0.75},
    ["TAUREN_FEMALE"] = {0, 0.125, 0.75, 1.0},
    ["SCOURGE_FEMALE"] = {0.125, 0.25, 0.75, 1.0},
    ["TROLL_FEMALE"] = {0.25, 0.375, 0.75, 1.0},
    ["ORC_FEMALE"] = {0.375, 0.5, 0.75, 1.0},
    ["BLOODELF_FEMALE"] = {0.5, 0.625, 0.75, 1.0},
    ["GOBLIN_FEMALE"] = {0.625, 0.750, 0.75, 1.0},
    ["MAGHAR_FEMALE"] = {0.750, 0.875, 0.75, 1.0},
    ["SETHRAK_FEMALE"] = {0.875, 1.0, 0.75, 1.0},
};
RACE_ICON_TEXTURES = {
    ["VENTHYRHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VenthyrHordeFemale.blp",

    ["VENTHYRHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VenthyrHordeMale.blp",

    ["KYRIANALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-KyrianAllianceFemale.blp",

    ["KYRIANALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-KyrianAllianceMale.blp",

    ["CENTAURHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-CentaurHordeFemale",
    ["CENTAURHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-CentaurHordeMale",
    ["CENTAURALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-CentaurAllianceFemale",
    ["CENTAURALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-CentaurAllianceMale",
    ["SLIMEGIANTHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SlimeGiantHordeMale",
    ["SLIMEGIANTHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SlimeGiantHordeMale",
    ["SLIMEGIANTALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SlimeGiantAllianceMale",
    ["SLIMEGIANTALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SlimeGiantAllianceMale",
    ["DRAKONIDHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakonidHordeMale",
    ["DRAKONIDHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakonidHordeMale",
    ["DRAKONIDALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakonidAllianceMale",
    ["DRAKONIDALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakonidAllianceMale",
    ["ARAKKOAHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ArakkoaHordeFemale",
    ["ARAKKOAHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ArakkoaHordeMale",
    ["SABERONHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SaberonHordeMale",
    ["GNOLLHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-GnollHordeMale",
    ["DRAKKINHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakkinHordeMale",
    ["DRAKKINALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakkinAllianceMale",
    ["DRAKKINHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakkinHordeFemale",
    ["DRAKKINALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DrakkinAllianceFemale",
    ["JINYUALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-JinyuAllianceFemale",
    ["JINYUALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-JinyuAllianceMale",
    ["SABERONHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SaberonHordeMale",
    ["OGREALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OgreAllianceFemale",
    ["OGREALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OgreAllianceMale",
    ["OGREHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OgreHordeMale",
    ["OGREHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OgreHordeFemale",
    ["GNOLLHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-GnollHordeMale",
    ["SETHRAKALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SethrakAllianceMale",
    ["SETHRAKALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SethrakAllianceFemale",
    ["THINHUMANHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ThinHumanHordeMale",
    ["VRYKULHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VrykulHordeMale",
    ["VRYKUL_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VrykulFemale",
    ["TUSKARR_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-TuskarrFemale",
    ["THINHUMAN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ThinHumanMale",
    ["NAGAHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-NagaHordeMale",
    ["NAGAHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-NagaHordeFemale",
    ["TUSKARR_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-TuskarrMale",
    ["VRYKUL_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VrykulMale",
    ["VRYKULHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VrykulHordeMale",
    ["THINHUMAN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ThinHumanMale",
    ["THINHUMANHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ThinHumanHordeMale",
    ["HARANIR_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HaranirMale",
    ["HARANIR_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HaranirFemale",
    ["HARANIRHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HaranirHordeMale",
    ["HARANIRHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HaranirHordeFemale",
    ["EARTHEN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EarthenMale",
    ["EARTHEN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EarthenFemale",
    ["EARTHENHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EarthenHordeMale",
    ["EARTHENHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EarthenHordeFemale",
    ["HIGHMOUNTAINTAUREN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HighmountainTaurenMale",
    ["HIGHMOUNTAINTAUREN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HighmountainTaurenFemale",
    ["MECHAGNOME_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-MechagnomeMale",
    ["MECHAGNOME_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-MechagnomeFemale",
    ["MAGHAR_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-MagharFemale",
    ["MAGHAR_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-MagharMale",
    ["SKYBORNE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SkyborneMale",
    ["SKYBORNE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SkyborneFemale",
    ["SKYBORNEHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SkyborneHordeMale",
    ["SKYBORNEHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-SkyborneHordeFemale",
	["EREDAR_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EredarMale",
	["EREDAR_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EredarFemale",
	["NIGHTBORNE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-NightborneMale",
	["NIGHTBORNE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-NightborneFemale",
	["VOIDELF_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VoidElfMale",
	["VOIDELF_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VoidElfFemale",
	["LIGHTFORGEDDRAENEI_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-LightforgedMale",
	["LIGHTFORGEDDRAENEI_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-LightforgedFemale",
	["ZANDALARITROLL_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ZandalariMale",
	["ZANDALARITROLL_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ZandalariFemale",
	["DARKIRONDWARF_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DarkIronMale",
	["DARKIRONDWARF_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DarkIronFemale",
	["DRACTHYR_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DracthyrMale",
	["DRACTHYR_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DracthyrFemale",
	["KULTIRAN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-KulTiranMale",
	["KULTIRAN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-KulTiranFemale",
	["ILLIDARI_ALLIANCE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DemonHunterAllianceMale",
	["ILLIDARI_ALLIANCE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DemonHunterAllianceFemale",
	["ILLIDARI_HORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DemonHunterHordeMale",
	["ILLIDARI_HORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DemonHunterHordeFemale",
    ["BLOODELF_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-BloodElfFemale",
    ["BLOODELF_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-BloodElfMale",
    ["DARKFALLENHORDE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DarkfallenHordeMale",
    ["DARKFALLENHORDE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DarkfallenHordeFemale",
    ["DARKFALLEN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DarkfallenMale",
    ["DARKFALLEN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DarkfallenFemale",
    ["BROKEN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-BrokenFemale",
    ["BROKEN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-BrokenMale",
    ["DRAENEI_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DraeneiFemale",
    ["DRAENEI_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DraeneiMale",
    ["DWARF_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DwarfFemale",
    ["DWARF_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-DwarfMale",
    ["GNOME_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-GnomeFemale",
    ["GNOME_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-GnomeMale",
    ["GOBLIN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-GoblinFemale",
    ["GOBLIN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-GoblinMale",
    ["HIGHELF_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HighElfFemale",
    ["HIGHELF_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HighElfMale",
    ["HUMAN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HumanFemale",
    ["HUMAN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-HumanMale",
    ["NIGHTELF_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-NightElfFemale",
    ["NIGHTELF_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-NightElfMale",
    ["ORC_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OrcFemale",
    ["ORC_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-OrcMale",
    ["PANDAREN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-PandarenFemale",
    ["PANDAREN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-PandarenMale",
    ["SCOURGE_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ScourgeFemale",
    ["SCOURGE_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-ScourgeMale",
    ["TAUREN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-TaurenFemale",
    ["TAUREN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-TaurenMale",
    ["TROLL_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-TrollFemale",
    ["TROLL_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-TrollMale",
    ["VULPERA_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VulperaFemale",
    ["VULPERA_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-VulperaMale",
    ["WORGEN_FEMALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-WorgenFemale",
    ["WORGEN_MALE"] = "Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-WorgenMale",
};
local function GetRaceIconTextureKey(fileString, gender, faction)
    fileString = strupper(fileString or "");
    if fileString == "ILLIDARI" and faction then
        return fileString.."_"..strupper(faction).."_"..gender;
    end
    return fileString.."_"..gender;
end
CLASS_ICON_TCOORDS = {
    ["WARRIOR"]	    = {0.000000000, 0.063000000, 0.872000000, 1.000000000},
    ["MAGE"]	    = {0.127441406, 0.189941406, 0.872070313, 1.000000000},
    ["ROGUE"]	    = {0.191406250, 0.253906250, 0.872070313, 1.000000000},
    ["DRUID"]	    = {0.447753906, 0.510253906, 0.872070313, 1.000000000},
    ["HUNTER"]	    = {0.063476563, 0.126464844, 0.872070313, 1.000000000},
    ["SHAMAN"]	    = {0.512695313, 0.575195313, 0.872070313, 1.000000000},
    ["PRIEST"]	    = {0.255859375, 0.318359375, 0.872070313, 1.000000000},
    ["WARLOCK"]	    = {0.319824219, 0.382324219, 0.872070313, 1.000000000},
    ["PALADIN"]	    = {0.383300781, 0.445800781, 0.872070313, 1.000000000},
    ["DEATHKNIGHT"]	= {0.576660156, 0.639160156, 0.872070313, 1.000000000},
};

if not _G.ALLIANCE_RACES then
    _G.ALLIANCE_RACES = {1, 2, 3, 4, 5, 6, 7}
end

if not _G.HORDE_RACES then
    _G.HORDE_RACES = {8, 9, 10, 11, 12, 13, 14, 15}
end

local HideNameEditFrame = CreateFrame("Frame")
local hideScheduled = false

local AllianceTooltip
local HordeTooltip
local raceTooltips = {};
local classTooltips = {};
local detailedRaceTooltips = {};
local detailedClassTooltips = {};
local brokenRaceInfo = {
    Name = "Broken",
    Description = "Broken draenei survive through ingenuity, stealth, and an unbroken connection to the Naaru.",
    Spell_1 = {name = "Salvager", icon = "trade_engineering", description = "Engineering and Mining skill increased by 10, and repair costs reduced by 10%."},
    Spell_2 = {name = "Krokul Cunning", icon = "ability_hunter_misdirection", description = "Reduces the radius at which enemies detect you by 5 yards."},
    Spell_3 = {name = "Fel-Scarred", icon = "spell_shadow_antimagicshell", description = "Magic effects that would drain your mana are 10% less effective, and Shadow resistance increased by 10."},
    Spell_4 = {name = "Echo of the Naaru", icon = "spell_holy_holyprotection", description = "Restore 15% maximum health over 10 seconds. 3 minute cooldown."},
}

local function HideAllTooltips()
    for button, tooltip in pairs(raceTooltips) do
        if tooltip then
            tooltip:Hide()
        end
    end

    for button, tooltip in pairs(classTooltips) do
        if tooltip then
            tooltip:Hide()
        end
    end

    if AllianceTooltip then
        AllianceTooltip:Hide()
    end

    if HordeTooltip then
        HordeTooltip:Hide()
    end
end

local backdrop = {
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
};

local Backdrop2 = {
    bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
    edgeFile = "Interface\\Tooltips\\ui-tooltip-border-maw",
    tile = true, tileSize = 16, edgeSize = 16,
    insets = { left = 4, right = 4, top = 4, bottom = 4 }
};

local function GetOrCreateRaceTooltip(button)
    if not raceTooltips[button] then
        local tooltip = CreateFrame("Frame", nil, CharacterCreateFrame)
        tooltip:SetBackdrop(Backdrop2)
        tooltip:SetSize(280, 150)
        tooltip:SetFrameStrata("TOOLTIP")
        tooltip:SetBackdropColor(0, 0, 0, 1)

        local raceID = button.raceID or button:GetID()
        local faction = _G.GetFactionForRaceID(raceID)

        if faction == "Alliance" then
            tooltip:SetPoint("LEFT", CharacterCreateFrame, "LEFT", 160, 50)
        elseif faction == "Horde" then
            tooltip:SetPoint("RIGHT", CharacterCreateFrame, "RIGHT", -160, 50)
        else
            tooltip:SetPoint("CENTER", CharacterCreateFrame, "CENTER", 0, 0)
        end

        tooltip.text = tooltip:CreateFontString(nil, "OVERLAY", "GlueFontNormalSmall")
        tooltip.text:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
        tooltip.text:SetPoint("TOPLEFT", tooltip, "TOPLEFT", 10, -10)
        tooltip.text:SetTextColor(1, 0.82, 0)
        tooltip.text:SetJustifyH("LEFT")

        tooltip.detailsText = tooltip:CreateFontString(nil, "OVERLAY")
        tooltip.detailsText:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
        tooltip.detailsText:SetPoint("TOPLEFT", tooltip.text, "BOTTOMLEFT", 0, -5)
        tooltip.detailsText:SetWidth(260)
        tooltip.detailsText:SetWordWrap(true)
        tooltip.detailsText:SetJustifyH("LEFT")

        tooltip.rightClickText = tooltip:CreateFontString(nil, "OVERLAY")
        tooltip.rightClickText:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
        tooltip.rightClickText:SetPoint("TOP", tooltip.detailsText, "BOTTOM", 0, -10)
        tooltip.rightClickText:SetTextColor(0.8, 0.8, 0.8)
        tooltip.rightClickText:SetJustifyH("CENTER")

        raceTooltips[button] = tooltip
    end
    return raceTooltips[button]
end

local function UpdateRaceTooltip(button, toggleDetails)
    local tooltip = GetOrCreateRaceTooltip(button)
    local raceID = button:GetID()
    local info = button.raceInfo
    if button.name == "Broken" or button.raceFileString and strupper(button.raceFileString) == "BROKEN" or info and info.Name == "Broken" then
        info = brokenRaceInfo
    end
    if not info then return end

    for i = 1, 8 do
        if tooltip["spellContainer"..i] then
            tooltip["spellContainer"..i]:Hide()
        end
    end

    tooltip.text:SetText("|cFFFFFFFF"..info.Name)
    tooltip.detailsText:SetText("|cffffd100"..info.Description.."|r")

    tooltip.text:ClearAllPoints()
    tooltip.text:SetPoint("TOPLEFT", tooltip, "TOPLEFT", 10, -5)

    tooltip.detailsText:ClearAllPoints()
    tooltip.detailsText:SetPoint("TOPLEFT", tooltip.text, "BOTTOMLEFT", 0, -5)

    if toggleDetails == nil then
        toggleDetails = not detailedRaceTooltips[button]
    end

    tooltip.rightClickText:SetText(toggleDetails and RACIALS_HIDE_HINT or RACIALS_SHOW_HINT)

    local lastElement = tooltip.detailsText
    local heightToAdd = 0

    tooltip.rightClickText:ClearAllPoints()
    tooltip.rightClickText:SetPoint("TOP", tooltip.detailsText, "BOTTOM", 0, -10)
    lastElement = tooltip.rightClickText

    if toggleDetails then
        for i = 1, 8 do
            if info["Spell_"..i] and info["Spell_"..i].name ~= "" then
                if not tooltip["spellContainer"..i] then
                    tooltip["spellContainer"..i] = CreateFrame("Frame", nil, tooltip)
                    tooltip["spellContainer"..i]:SetSize(260, 60)

                    tooltip["spellIcon"..i] = tooltip["spellContainer"..i]:CreateTexture(nil, "OVERLAY")
                    tooltip["spellIcon"..i]:SetTexture("Interface\\Icons\\"..info["Spell_"..i].icon)
                    tooltip["spellIcon"..i]:SetSize(25, 25)
                    tooltip["spellIcon"..i]:SetPoint("TOPLEFT", -35, -20)

                    tooltip["spellName"..i] = tooltip["spellContainer"..i]:CreateFontString(nil, "OVERLAY")
                    tooltip["spellName"..i]:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
                    tooltip["spellName"..i]:SetPoint("LEFT", tooltip["spellIcon"..i], "RIGHT", 5, 8)
                    tooltip["spellName"..i]:SetTextColor(1, 0.82, 0)
                    tooltip["spellName"..i]:SetJustifyH("LEFT")

                    tooltip["spellDesc"..i] = tooltip["spellContainer"..i]:CreateFontString(nil, "OVERLAY")
                    tooltip["spellDesc"..i]:SetFont("Fonts\\FRIZQT__.TTF", 9, "OUTLINE")
                    tooltip["spellDesc"..i]:SetWordWrap(true)
                    tooltip["spellDesc"..i]:SetJustifyH("LEFT")
                    tooltip["spellDesc"..i]:SetPoint("TOPLEFT", tooltip["spellName"..i], "BOTTOMLEFT", 0, -2)
                end

                tooltip["spellName"..i]:SetText(info["Spell_"..i].name)
                tooltip["spellDesc"..i]:SetText(info["Spell_"..i].description)

                tooltip["spellContainer"..i]:Show()
                tooltip["spellContainer"..i]:ClearAllPoints()
                if lastElement == tooltip.rightClickText then
                    tooltip["spellContainer"..i]:SetPoint("TOPLEFT", tooltip.detailsText, "BOTTOMLEFT", 35, -(tooltip.rightClickText:GetHeight() + 16))
                else
                    tooltip["spellContainer"..i]:SetPoint("TOPLEFT", lastElement, "BOTTOMLEFT", 0, 3)
                end

                local containerHeight = tooltip["spellIcon"..i]:GetHeight() + tooltip["spellDesc"..i]:GetHeight() + 3
                tooltip["spellContainer"..i]:SetHeight(containerHeight)

                lastElement = tooltip["spellContainer"..i]
            end
        end
    end

    local maxWidth = math.max(tooltip.text:GetWidth(), tooltip.detailsText:GetWidth())
    if toggleDetails then
        for i = 1, 8 do
            if tooltip["spellContainer"..i] and tooltip["spellContainer"..i]:IsShown() then
                local nameWidth = tooltip["spellName"..i]:GetStringWidth()
                maxWidth = math.max(maxWidth, nameWidth + 70)
            end
        end
    end
    maxWidth = math.max(280, math.min(maxWidth + 40, 600))
    tooltip:SetWidth(300)

    if toggleDetails then
        for i = 1, 8 do
            if tooltip["spellContainer"..i] and tooltip["spellContainer"..i]:IsShown() then
                tooltip["spellDesc"..i]:SetWidth(205)
                local containerHeight = tooltip["spellIcon"..i]:GetHeight() + tooltip["spellDesc"..i]:GetHeight() + 3
                tooltip["spellContainer"..i]:SetHeight(containerHeight)
            end
        end
    end

    heightToAdd = 0
    if toggleDetails then
        for i = 1, 8 do
            if tooltip["spellContainer"..i] and tooltip["spellContainer"..i]:IsShown() then
                heightToAdd = heightToAdd + tooltip["spellContainer"..i]:GetHeight() + 3
            end
        end
    end

    local baseHeight = tooltip.text:GetHeight() +
                      tooltip.detailsText:GetHeight() +
                      tooltip.rightClickText:GetHeight() + 25

    tooltip:SetHeight(baseHeight + heightToAdd)
    detailedRaceTooltips[button] = toggleDetails
    tooltip:ClearAllPoints()

    local pos = _G.GetRaceTooltipPosition(raceID, button)
    tooltip:SetPoint(pos.point, button, pos.relPoint, pos.x, pos.y)
end

local function GetOrCreateClassTooltip(button)
    if not classTooltips[button] then
        local tooltip = CreateFrame("Frame", nil, CharacterCreateFrame)
        tooltip:SetBackdrop(Backdrop2)
        tooltip:SetSize(300, 200)
        tooltip:SetFrameStrata("TOOLTIP")
        tooltip:SetBackdropColor(0, 0, 0, 1)

        tooltip.text = tooltip:CreateFontString(nil, "OVERLAY", "GlueFontNormalSmall")
        tooltip.text:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
        tooltip.text:SetPoint("TOPLEFT", tooltip, "TOPLEFT", 8, -8)
        tooltip.text:SetTextColor(1, 0.82, 0)
        tooltip.text:SetJustifyH("LEFT")

        tooltip.detailsText = tooltip:CreateFontString(nil, "OVERLAY")
        tooltip.detailsText:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
        tooltip.detailsText:SetPoint("TOPLEFT", tooltip.text, "BOTTOMLEFT", 0, -5)
        tooltip.detailsText:SetWidth(284)
        tooltip.detailsText:SetWordWrap(true)
        tooltip.detailsText:SetJustifyH("LEFT")

        tooltip.Roles = tooltip:CreateFontString(nil, "OVERLAY", "GlueFontNormalSmall")
        tooltip.Roles:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE")
        tooltip.Roles:SetWidth(284)
        tooltip.Roles:SetWordWrap(true)
        tooltip.Roles:SetJustifyH("LEFT")

        classTooltips[button] = tooltip
    end

    return classTooltips[button]
end

local function GetValidRacesForClass(classID)
    local allianceRaces = {}
    local hordeRaces = {}

    for _, raceID in ipairs(_G.ALLIANCE_RACES) do
        if IsRaceClassValid(raceID, classID) then
            local raceName = _G["RACE_" .. raceID] or RACE .. raceID
            table.insert(allianceRaces, raceName)
        end
    end

    for _, raceID in ipairs(_G.HORDE_RACES) do
        if IsRaceClassValid(raceID, classID) then
            local raceName = _G["RACE_" .. raceID] or RACE .. raceID
            table.insert(hordeRaces, raceName)
        end
    end

    return allianceRaces, hordeRaces
end
local function UpdateClassTooltip(button)
    local tooltip = GetOrCreateClassTooltip(button)
    local classID = button:GetID()
    local info = _G.Class_Informations[classID]

    if not info then return end

    local buttonX, buttonY = button:GetCenter()
    local parentX, parentY = CharacterCreateFrame:GetCenter()

    if buttonX > parentX then
        tooltip:SetPoint("BOTTOMRIGHT", button, "TOPRIGHT", 0, 10)
    else
        tooltip:SetPoint("BOTTOMLEFT", button, "TOPLEFT", 0, 10)
    end

    tooltip.text:SetText("|cFFFFFFFF"..info.Name)
    tooltip.detailsText:SetText("|cffffd100"..info.Description.."|r")

    tooltip.text:ClearAllPoints()
    tooltip.text:SetPoint("TOPLEFT", tooltip, "TOPLEFT", 8, -8)

    tooltip.detailsText:ClearAllPoints()
    tooltip.detailsText:SetPoint("TOPLEFT", tooltip.text, "BOTTOMLEFT", 0, -5)

    local currentRaceID = GetSelectedRace()
    local isAllowed = IsRaceClassValid(currentRaceID, classID)
    local showRestriction = not isAllowed

    tooltip.RestrictionText = tooltip.RestrictionText or tooltip:CreateFontString(nil, "OVERLAY")
    tooltip.RestrictionText:SetFont("Fonts\\FRIZQT__.TTF", 12, "OUTLINE")
    tooltip.RestrictionText:SetJustifyH("LEFT")

    tooltip.FactionText = tooltip.FactionText or tooltip:CreateFontString(nil, "OVERLAY")
    tooltip.FactionText:SetFont("Fonts\\FRIZQT__.TTF", 11)
    tooltip.FactionText:SetWordWrap(true)
    tooltip.FactionText:SetJustifyH("LEFT")

    local coloredRoles = info.Roles
    coloredRoles = string.gsub(coloredRoles, "Daño cuerpo a cuerpo", "|cffff2020Daño cuerpo a cuerpo|r")
    coloredRoles = string.gsub(coloredRoles, "Daño a distancia", "|cffff2020Daño a distancia|r")
    coloredRoles = string.gsub(coloredRoles, "Tanque", "|cff0070ddTanque|r")
    coloredRoles = string.gsub(coloredRoles, "Sanador", "|cff20c000Sanador|r")

    if not tooltip.Roles then
        tooltip.Roles = tooltip:CreateFontString(nil, "OVERLAY")
        tooltip.Roles:SetFont("Fonts\\FRIZQT__.TTF", 11)
    end

    tooltip.Roles:SetText("|cFFFFFFFF"..FUNTION_INF.."|r\n\n "..coloredRoles)

    if showRestriction then
        tooltip.Roles:ClearAllPoints()
        tooltip.Roles:SetPoint("TOPLEFT", tooltip.detailsText, "BOTTOMLEFT", 0, -20)
        tooltip.Roles:Show()

        tooltip.RestrictionText:SetPoint("TOPLEFT", tooltip.Roles, "BOTTOMLEFT", 0, -20)
        tooltip.RestrictionText:SetTextColor(1, 0, 0)
        tooltip.RestrictionText:SetText(WARNING_RACE)
        tooltip.RestrictionText:Show()

        local allianceRaces, hordeRaces = GetValidRacesForClass(classID)

        local factionText = ""

        if #allianceRaces > 0 then
            factionText = ALLIANCE_RACE .. " |cFFFFFFFF" .. table.concat(allianceRaces, ", ") .. "|r"
        end

        if #hordeRaces > 0 then
            if factionText ~= "" then
                factionText = factionText .. "\n\n"
            end
            factionText = factionText .. "\n" .. HORDE_RACE .. " |cFFFFFFFF" .. table.concat(hordeRaces, ", ") .. "|r"
        end

        tooltip.FactionText:SetPoint("TOPLEFT", tooltip.RestrictionText, "BOTTOMLEFT", 0, -15)
        tooltip.FactionText:SetTextColor(1, 0.82, 0)
        tooltip.FactionText:SetText(factionText)
        tooltip.FactionText:Show()

        local maxWidth = math.max(tooltip.text:GetWidth(), tooltip.detailsText:GetWidth())
        maxWidth = math.max(maxWidth, tooltip.Roles:GetWidth())
        maxWidth = math.max(maxWidth, tooltip.RestrictionText:GetWidth())
        maxWidth = math.max(maxWidth, tooltip.FactionText:GetWidth())
        maxWidth = math.max(280, math.min(maxWidth + 16, 400))

        tooltip:SetWidth(maxWidth)
        tooltip.FactionText:SetWidth(maxWidth - 16)

        local restrictionHeight = tooltip.text:GetHeight() +
                                tooltip.detailsText:GetHeight() +
                                tooltip.Roles:GetHeight() +
                                tooltip.RestrictionText:GetHeight() +
                                tooltip.FactionText:GetHeight() + 80

        tooltip:SetHeight(restrictionHeight)

    else
        tooltip.RestrictionText:Hide()
        tooltip.FactionText:Hide()

        tooltip.Roles:Show()
        tooltip.Roles:ClearAllPoints()
        tooltip.Roles:SetPoint("TOPLEFT", tooltip.detailsText, "BOTTOMLEFT", 0, -10)

        local maxWidth = math.max(tooltip.text:GetWidth(), tooltip.detailsText:GetWidth())
        maxWidth = math.max(maxWidth, tooltip.Roles:GetWidth())
        maxWidth = math.max(280, math.min(maxWidth + 16, 400))
        tooltip:SetWidth(maxWidth)

        local baseHeight = tooltip.text:GetHeight() +
                          tooltip.detailsText:GetHeight() +
                          tooltip.Roles:GetHeight() + 30

        tooltip:SetHeight(baseHeight)
    end
end

function CharacterCreate_MoveTexturesToBackground()
    for i = 1, MAX_RACES do
        local button = _G["CharacterCreateRaceButton"..i]
        if button then
            local normalTex = _G[button:GetName().."NormalTexture"]
            local pushedTex = _G[button:GetName().."PushedTexture"]

            if normalTex then
                normalTex:SetDrawLayer("BACKGROUND")
            end
            if pushedTex then
                pushedTex:SetDrawLayer("BACKGROUND")
            end
        end
    end

    for i = 1, MAX_CLASSES_PER_RACE do
        local button = _G["CharacterCreateClassButton"..i]
        if button then
            local normalTex = _G[button:GetName().."NormalTexture"]
            local pushedTex = _G[button:GetName().."PushedTexture"]

            if normalTex then
                normalTex:SetDrawLayer("BACKGROUND")
            end
            if pushedTex then
                pushedTex:SetDrawLayer("BACKGROUND")
            end
        end
    end

    local maleButton = CharacterCreateGenderButtonMale
    local femaleButton = CharacterCreateGenderButtonFemale

    if maleButton then
        _G[maleButton:GetName().."NormalTexture"]:SetDrawLayer("BACKGROUND")
        _G[maleButton:GetName().."PushedTexture"]:SetDrawLayer("BACKGROUND")
    end

    if femaleButton then
        _G[femaleButton:GetName().."NormalTexture"]:SetDrawLayer("BACKGROUND")
        _G[femaleButton:GetName().."PushedTexture"]:SetDrawLayer("BACKGROUND")
    end
end

local function CreateFactionTooltip(parent, factionName)
    local tooltip = CreateFrame("Frame", nil, parent)
    tooltip:SetBackdrop(backdrop)
    tooltip:SetFrameStrata("TOOLTIP")
    tooltip:SetBackdropColor(0, 0, 0, 0.9)
    tooltip:SetBackdropBorderColor(1, 1, 1, 1)
    tooltip:Hide()

    tooltip.title = tooltip:CreateFontString(nil, "OVERLAY")
    tooltip.title:SetFont("Fonts\\FRIZQT__.TTF", 12, "OUTLINE")
    tooltip.title:SetPoint("TOPLEFT", tooltip, "TOPLEFT", 10, -10)
    tooltip.title:SetTextColor(1, 0.82, 0)
    tooltip.title:SetJustifyH("LEFT")

    tooltip.description = tooltip:CreateFontString(nil, "OVERLAY")
    tooltip.description:SetFont("Fonts\\FRIZQT__.TTF", 12, "OUTLINE")
    tooltip.description:SetPoint("TOPLEFT", tooltip.title, "BOTTOMLEFT", 0, -8)
    tooltip.description:SetWidth(280)
    tooltip.description:SetWordWrap(true)
    tooltip.description:SetJustifyH("LEFT")
    tooltip.description:SetTextColor(0.9, 0.9, 0.9)

    return tooltip
end

local function UpdateTooltipSize(tooltip)
    local titleHeight = tooltip.title:GetHeight()
    local descHeight = tooltip.description:GetHeight()
    local totalHeight = titleHeight + descHeight + 30

    tooltip:SetHeight(totalHeight)
    tooltip:SetWidth(300)
end

function CharacterCreate_PositionRaceButtons()
    -- Esteria compact faction grid: preserve enumeration order and exact RaceIDs.
    local width = CharacterCreateFrame:GetWidth();
    if width <= 0 then return; end
    local columns = 7;
    local spacing = math.min(72, (width * 0.36 - 32) / columns);
    local size = math.max(24, spacing - 8);
    local gridWidth = (columns - 1) * spacing + size;
    local function positionFaction(indices, side)
        local left = side == "Alliance" and 24 or width - 24 - gridWidth;
        for slot,index in ipairs(indices or {}) do
            local button = _G["CharacterCreateRaceButton"..index];
            if button then
                local column = (slot - 1) % columns;
                local row = math.floor((slot - 1) / columns);
                button:ClearAllPoints();
                button:SetSize(size, size);
                button:SetPoint("CENTER", CharacterCreateFrame, "TOPLEFT",
                    left + size / 2 + column * spacing, -100 - row * spacing);
                button:GetNormalTexture():SetSize(size, size);
                button:GetPushedTexture():SetSize(size, size);
                for _,name in ipairs({"staticTexture","highlightTexture","checkedTexture"}) do
                    if button[name] then button[name]:SetSize(size * 112 / 50, size * 112 / 50); end
                end
            end
        end
        local logo = side == "Alliance" and CustomizationLogoAlliance or CustomizationLogoHorde;
        local label = side == "Alliance" and CustomizationTextAlliance or CustomizationTextHorde;
        local hitbox = side == "Alliance" and AllianceLogoFrame or HordeLogoFrame;
        if logo and label then
            local centre = left + gridWidth / 2;
            logo:ClearAllPoints();
            logo:SetSize(56,56);
            logo:SetPoint("CENTER",CharacterCreateFrame,"TOPLEFT",
                centre + (side == "Alliance" and -44 or 44),-46);
            label:ClearAllPoints();
            if side == "Alliance" then label:SetPoint("LEFT",logo,"RIGHT",-4,0);
            else label:SetPoint("RIGHT",logo,"LEFT",4,0); end
            if hitbox then
                hitbox:ClearAllPoints(); hitbox:SetSize(56,56); hitbox:SetPoint("CENTER",logo,"CENTER",0,0);
            end
        end
    end
    positionFaction(CharacterCreate.raceIndicesAlliance,"Alliance");
    positionFaction(CharacterCreate.raceIndicesHorde,"Horde");
    if not CharacterCreateFrame.esteriaRaceLayoutResize then
        CharacterCreateFrame:HookScript("OnSizeChanged",CharacterCreate_PositionRaceButtons);
        CharacterCreateFrame.esteriaRaceLayoutResize = true;
    end
end

function CharacterCreate_PositionClassButtons()
    local buttonSpacing = 80

    for i = 1, MAX_CLASSES_PER_RACE do
        local button = _G["CharacterCreateClassButton"..i]
        if button then
            button:ClearAllPoints()

            if i == 1 then
                button:SetPoint("CENTER", CharacterCreateFrame, "BOTTOM", -360, 80)
            else
                local prevButton = _G["CharacterCreateClassButton"..(i-1)]
                button:SetPoint("LEFT", prevButton, "RIGHT", buttonSpacing - 38, 0)
            end
        end
    end
end

function CharacterCreate_PositionGenderButtons()
    local genderSpacing = 310

    local maleButton = CharacterCreateGenderButtonMale
    local femaleButton = CharacterCreateGenderButtonFemale

    if maleButton then
        maleButton:ClearAllPoints()
        maleButton:SetPoint("CENTER", CharacterCreateFrame, "CENTER", -(genderSpacing/2), -250)
    end

    if femaleButton then
        femaleButton:ClearAllPoints()
        femaleButton:SetPoint("CENTER", CharacterCreateFrame, "CENTER", (genderSpacing/2), -250)
    end
end

function CharacterCreate_PositionAllButtons()
    CharacterCreate_PositionRaceButtons()
    CharacterCreate_PositionClassButtons()
    CharacterCreate_PositionGenderButtons()
end

-- Creator mousewheel face zoom (wowdecomp)
function CharacterCreate_OnLoad(self)
    -- The viewport owns zoom; choice pickers keep their own wheel scrolling.
    CharacterCreateFrame:EnableMouseWheel(true);
    CharacterCreateFrame:SetScript("OnMouseWheel", function(_, delta)
        CycleCharCustomization("EA_ZOOM", delta);
    end);
    self:SetSequence(0)
    self:SetCamera(0)
    SetCharCustomizeFrame("CharacterCreate")

    CharacterCreate.numRaces = 0
    CharacterCreate.selectedRace = 0
    CharacterCreate.numClasses = 0
    CharacterCreate.selectedClass = 0
    CharacterCreate.selectedGender = 0
    CharacterCreate.raceIndicesAlliance = {}
    CharacterCreate.raceIndicesHorde = {}
    CharacterCreateRaceButtonsContainer:SetParent(CharacterCreateFrame)
    CharacterCreate.personalizationMode = false
    CharacterCreate_MoveTexturesToBackground()
    CharacterCreate_SetupCustomButtons()

    for i=1, NUM_CHAR_CUSTOMIZATIONS, 1 do
        _G["CharacterCustomizationButtonFrame"..i.."Text"]:SetText(_G["CHAR_CUSTOMIZATION"..i.."_DESC"])
    end

    local backdropColor = FACTION_BACKDROP_COLOR_TABLE["Alliance"]
    CharacterCreateNameEdit:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3])
    CharacterCreateNameEdit:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6])
    CharacterCreateLastNameEdit:SetBackdropBorderColor(backdropColor[1], backdropColor[2], backdropColor[3])
    CharacterCreateLastNameEdit:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6])

    CustomizationBG2 = CharacterCreateFrame:CreateTexture("CustomizationBG2", "BACKGROUND")
    CustomizationBG2:SetSize(GlueParent:GetWidth() + 2, GlueParent:GetHeight() + 6)
    CustomizationBG2:SetTexture("Interface\\Glues\\CharacterCreate\\MainShadow")
    CustomizationBG2:SetPoint("CENTER")
    CustomizationBG2:SetAlpha(0.8)

    CustomizationLogoAlliance = CharacterCreateFrame:CreateTexture("CustomizationLogoAlliance", "ARTWORK")
    CustomizationLogoAlliance:SetSize(100, 100)
    CustomizationLogoAlliance:SetTexture("Interface\\Glues\\CharacterCreate\\AllianceLogo")
    CustomizationLogoAlliance:SetPoint("TOPLEFT", -16, 16)

    CustomizationTextAlliance = CharacterCreateFrame:CreateFontString("CustomizationTextAlliance", "OVERLAY")
    CustomizationTextAlliance:SetFontObject(GlueFontNormal)
    CustomizationTextAlliance:SetText(string.upper(ALLIANCE))
    CustomizationTextAlliance:SetPoint("LEFT", CustomizationLogoAlliance, "RIGHT", -24, 0)

    AllianceTooltip = CreateFactionTooltip(CharacterCreateFrame, "Alliance")

    AllianceLogoFrame = CreateFrame("Frame", "AllianceLogoFrame", CharacterCreateFrame)
    AllianceLogoFrame:SetSize(100, 100)
    AllianceLogoFrame:SetPoint("TOPLEFT", -16, 16)
    AllianceLogoFrame:EnableMouse(true)

    AllianceLogoFrame:SetScript("OnEnter", function(self)
        AllianceTooltip.title:SetText(ALLIANCE)
        AllianceTooltip.description:SetText(FACTION_ALLIANCE_DESCRIPTION)
        UpdateTooltipSize(AllianceTooltip)
        AllianceTooltip:ClearAllPoints()
        AllianceTooltip:SetPoint("TOPLEFT", self, "BOTTOMLEFT", 90, 60)
        AllianceTooltip:Show()
    end)

    AllianceLogoFrame:SetScript("OnLeave", function(self)
        AllianceTooltip:Hide()
    end)

    CustomizationLogoHorde = CharacterCreateFrame:CreateTexture("CustomizationLogoHorde", "ARTWORK")
    CustomizationLogoHorde:SetSize(100, 100)
    CustomizationLogoHorde:SetTexture("Interface\\Glues\\CharacterCreate\\HordeLogo")
    CustomizationLogoHorde:SetPoint("TOPRIGHT", 16, 16)

    CustomizationTextHorde = CharacterCreateFrame:CreateFontString("CustomizationTextHorde", "OVERLAY")
    CustomizationTextHorde:SetFontObject(GlueFontNormal)
    CustomizationTextHorde:SetText(string.upper(HORDE))
    CustomizationTextHorde:SetPoint("RIGHT", CustomizationLogoHorde, "LEFT", 24, 0)

    HordeTooltip = CreateFactionTooltip(CharacterCreateFrame, "Horde")

    HordeLogoFrame = CreateFrame("Frame", "HordeLogoFrame", CharacterCreateFrame)
    HordeLogoFrame:SetSize(100, 100)
    HordeLogoFrame:SetPoint("TOPRIGHT", 16, 16)
    HordeLogoFrame:EnableMouse(true)

    HordeLogoFrame:SetScript("OnEnter", function(self)
        HordeTooltip.title:SetText(HORDE)
        HordeTooltip.description:SetText(FACTION_HORDE_DESCRIPTION)
        UpdateTooltipSize(HordeTooltip)
        HordeTooltip:ClearAllPoints()
        HordeTooltip:SetPoint("TOPRIGHT", self, "BOTTOMRIGHT", -90, 60)
        HordeTooltip:Show()
    end)

    HordeLogoFrame:SetScript("OnLeave", function(self)
        HordeTooltip:Hide()
    end)

    CharacterCreate_CreateGenderButtonTextures()
   CharacterCreate_PositionAllButtons()
end

-- Esteria creator dice and smooth rotation: reuse the client atlas and native name generator.
function CharacterCreate_SetupCustomButtons()
    local buttons = {
        {button = CharCreateRandomizeButton, width = 36, height = 36},
        {button = CharacterCreateRandomName, width = 30, height = 30},
        {button = CharacterCreateRandomLastName, width = 30, height = 30}
    }

    local texturePath = "Interface\\Glues\\CharacterCreate\\charactercreate"

    for _, btnInfo in ipairs(buttons) do
        local button = btnInfo.button
        if button then
            button:SetNormalTexture(texturePath)
            button:GetNormalTexture():SetTexCoord(0.261230469, 0.292480469, 0.890625000, 0.920410156)

            button:SetPushedTexture(texturePath)
            button:GetPushedTexture():SetTexCoord(0.223144531, 0.253906250, 0.890625000, 0.920410156)

            button:SetHighlightTexture("Interface\\Buttons\\UI-Common-MouseHilight")
            button:GetHighlightTexture():SetTexCoord(0.1, 0.9, 0.1, 0.9)
            button:GetHighlightTexture():SetBlendMode("ADD")

            button:SetSize(btnInfo.width, btnInfo.height)

            if button:GetFontString() then
                button:GetFontString():Hide()
            else
                button:SetText("")
            end
        end
    end
end

function CharacterCreate_TogglePersonalization()
    if (CharacterCreate.personalizationMode == false) then
        CharacterCreate.personalizationMode = true;
        PlaySound("gsCharacterSelectionCreateNew");
        CharacterCreateRaceButtonsContainer:Hide();
        CharacterCreateClassButtonsContainer:Hide();
        CharacterCreateGenderButtonsContainer:Hide();
        CustomizationLogoAlliance:Hide();
        CustomizationTextAlliance:Hide();
        CustomizationLogoHorde:Hide();
        CustomizationTextHorde:Hide();
        CharacterCreateRotateLeft:Show();
        CharacterCreateRotateRight:Show();
        CharCreatePersonalizeButton:Hide();
        CharCreateOkayButton:Show();
        CharacterCreate_UpdateHairCustomization();
        CharCreateRandomizeButton:Show();
        CharacterCreateNameEdit:Show();
        CharacterCreateLastNameEdit:Show();
        CharacterCreateRandomName:Show();
        CharacterCreateRandomLastName:Show();

        for i=1, NUM_CHAR_CUSTOMIZATIONS do
            _G["CharacterCustomizationButtonFrame"..i]:Show();
        end
    else
        CharacterCreate.personalizationMode = false;
        PlaySound("gsCharacterCreationCancel");
        CharacterCreateRaceButtonsContainer:Show();
        CharacterCreateClassButtonsContainer:Show();
        CharacterCreateGenderButtonsContainer:Show();
        CustomizationLogoAlliance:Show();
        CustomizationTextAlliance:Show();
        CustomizationLogoHorde:Show();
        CustomizationTextHorde:Show();
        CharacterCreateRotateLeft:Hide();
        CharacterCreateRotateRight:Hide();
        CharCreateRandomizeButton:Hide();
        CharacterCreateNameEdit:Hide();
        CharacterCreateLastNameEdit:Hide();
        CharacterCreateRandomName:Hide();
        CharacterCreateRandomLastName:Hide();
        CharCreatePersonalizeButton:Show();
        CharCreateOkayButton:Hide();

        for i=1, NUM_CHAR_CUSTOMIZATIONS do
            _G["CharacterCustomizationButtonFrame"..i]:Hide();
        end
    end
end

HideNameEditFrame:SetScript("OnUpdate", function(self, elapsed)
    if hideScheduled then
        CharacterCreateNameEdit:Hide()
        CharacterCreateLastNameEdit:Hide();
        CharacterCreateRandomName:Hide()
        CharacterCreateRandomLastName:Hide();
        hideScheduled = false
        self:Hide()
    end
end)

function CharacterCreate_SetNameFields(fullName)
    local firstName = "";
    local lastName = "";
    if ( fullName and fullName ~= "" ) then
        local splitFirst, splitLast = string.match(fullName, "^([^ ]+)%s*(.*)$");
        firstName = splitFirst or fullName;
        lastName = splitLast or "";
    end
    CharacterCreateNameEdit:SetText(firstName);
    CharacterCreateLastNameEdit:SetText(lastName);
end

function CharacterCreate_GetFullName()
    local firstName = CharacterCreateNameEdit:GetText() or "";
    local lastName = CharacterCreateLastNameEdit:GetText() or "";
    if ( firstName == "" ) then
        return lastName;
    elseif ( lastName == "" ) then
        return firstName;
    end
    return firstName.." "..lastName;
end

function CharacterCreate_RandomizeName(editBox)
    local field = editBox or CharacterCreateNameEdit;
    field:SetText(GetRandomName());
end

function CharacterCreate_OnShow()
    CharacterCreate.personalizationMode = false;

    for i=1, MAX_CLASSES_PER_RACE, 1 do
        local button = _G["CharacterCreateClassButton"..i];
        button:Enable();
        SetButtonDesaturated(button, false)
    end
    for i=1, MAX_RACES, 1 do
        local button = _G["CharacterCreateRaceButton"..i];
        button:Enable();
        SetButtonDesaturated(button, false)
    end

    if ( PAID_SERVICE_TYPE ) then
        CustomizeExistingCharacter( PAID_SERVICE_CHARACTER_ID );
        CharacterCreate_SetNameFields(PaidChange_GetName());
    else
        ResetCharCustomize();
        CharacterCreate_SetNameFields("");
        CharCreateRandomizeButton:Hide();
    end

    CharacterCreate.personalizationMode = false;
    CharCreateOkayButton:Hide();

    for i=1, NUM_CHAR_CUSTOMIZATIONS do
        _G["CharacterCustomizationButtonFrame"..i]:Hide();
    end

    CharacterCreateEnumerateRaces(GetAvailableRaces());
    SetCharacterRace(GetSelectedRace());
    CharacterCreateEnumerateClasses(GetAvailableClasses());
    local_,_,index = GetSelectedClass();
    SetCharacterClass(index);
    SetCharacterGender(GetSelectedSex())
    CharacterCreate_UpdateHairCustomization();
    SetCharacterCreateFacing(-15);
    CharacterChangeFixup();
    CharacterCreate_CreateGenderButtonTextures();
    CharacterCreate_UpdateButtonCheckedStates();
    CharacterCreate_ResetState();

    hideScheduled = true
    HideNameEditFrame:Show()
   CharacterCreate_PositionAllButtons()
end

function CharacterCreate_OnHide()
    PAID_SERVICE_CHARACTER_ID = nil;
    PAID_SERVICE_TYPE = nil;
    CharacterCreate_ResetState();

    for button, tooltip in pairs(raceTooltips) do
        if tooltip then tooltip:Hide() end
    end
    for button, tooltip in pairs(classTooltips) do
        if tooltip then tooltip:Hide() end
    end
    if AllianceTooltip then AllianceTooltip:Hide() end
    if HordeTooltip then HordeTooltip:Hide() end
end

-- Creator right-drag camera pan (wowdecomp)
function CharacterCreateFrame_OnMouseDown(button)
    if ( button == "RightButton" ) then
        CHARACTER_CREATE_PAN_X, CHARACTER_CREATE_PAN_Y = GetCursorPosition();
        CHARACTER_CREATE_PAN_MOVED = false;
    end
    if ( button == "LeftButton" ) then
        CHARACTER_CREATE_ROTATION_START_X = GetCursorPosition();
        CHARACTER_CREATE_INITIAL_FACING = GetCharacterCreateFacing();
    end
end

function CharacterCreateFrame_OnMouseUp(button)
    if ( button == "RightButton" and CHARACTER_CREATE_PAN_X ) then
        if ( not CHARACTER_CREATE_PAN_MOVED ) then CycleCharCustomization("EA_PAN_RESET", 1); end
        CHARACTER_CREATE_PAN_X = nil;
    end
    if ( button == "LeftButton" ) then
        CHARACTER_CREATE_ROTATION_START_X = nil
    end
end

function CharacterCreateFrame_OnUpdate()
    if ( CHARACTER_CREATE_PAN_X ) then
        -- Glue GetCursorPosition and GetScreenHeight share units: pass screen-height fractions.
        local x, y = GetCursorPosition();
        local dx, dy = x - CHARACTER_CREATE_PAN_X, y - CHARACTER_CREATE_PAN_Y;
        if ( dx ~= 0 or dy ~= 0 ) then
            CHARACTER_CREATE_PAN_MOVED = true;
            CycleCharCustomization("EA_PAN", dx / GetScreenHeight(), dy / GetScreenHeight());
            CHARACTER_CREATE_PAN_X, CHARACTER_CREATE_PAN_Y = x, y;
        end
    end
    if ( CHARACTER_CREATE_ROTATION_START_X ) then
        local x = GetCursorPosition();
        local diff = (x - CHARACTER_CREATE_ROTATION_START_X) * CHARACTER_ROTATION_CONSTANT;
        CHARACTER_CREATE_ROTATION_START_X = GetCursorPosition();
        SetCharacterCreateFacing(GetCharacterCreateFacing() + diff);
    end
end

function CharacterCreateEnumerateRaces(...)
    CharacterCreate.numRaces = select("#", ...)/3;
    CharacterCreate.raceIndicesAlliance = {};
    CharacterCreate.raceIndicesHorde = {};
    if ( CharacterCreate.numRaces > MAX_RACES ) then
        message("Too many races!  Update MAX_RACES");
        return;
    end
    local coords;
    local index = 1;
    local button;
    local gender;
    local hiddenSelection = false;
    local selectedSex = GetSelectedSex();
    local availableRaceIDs = nil;
    if ( type(GetAvailableRaceIDs) == "function" ) then
        availableRaceIDs = {GetAvailableRaceIDs()};
    end
    if ( selectedSex == SEX_MALE ) then
        gender = "MALE";
    elseif ( selectedSex == SEX_FEMALE ) then
        gender = "FEMALE";
    end
    for i=1, select("#", ...), 3 do
        local fileString = select(i+1, ...)
        local hiddenRace = strupper(fileString) == "DRAKKINHORDE" or strupper(fileString) == "SETHRAK" or strupper(fileString) == "VRYKULHORDE"
        local raceID = availableRaceIDs and availableRaceIDs[index] or index;
        if ( _G.GetExactRaceIDForFileString ) then
            local exactRaceID = _G.GetExactRaceIDForFileString(fileString);
            if ( exactRaceID ) then
                raceID = exactRaceID;
            end
        end
        local _, faction = GetFactionForRace(index);
        if ( _G.GetFactionForRaceID ) then
            faction = _G.GetFactionForRaceID(raceID);
        end
        local raceKey = GetRaceIconTextureKey(fileString, gender, faction);
        local targetTexture = RACE_ICON_TEXTURES[raceKey];
        local normalTexture = _G["CharacterCreateRaceButton"..index.."NormalTexture"];
        local pushedTexture = _G["CharacterCreateRaceButton"..index.."PushedTexture"];
        if targetTexture then
            normalTexture:SetTexture(targetTexture);
            pushedTexture:SetTexture(targetTexture);
            normalTexture:SetTexCoord(0, 1, 0, 1);
            pushedTexture:SetTexCoord(0, 1, 0, 1);
        else
            coords = RACE_ICON_TCOORDS[raceKey] or RACE_ICON_TCOORDS["HUMAN_"..(gender or "MALE")];
            normalTexture:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Races");
            pushedTexture:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-Races");
            normalTexture:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
            pushedTexture:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
        end
        button = _G["CharacterCreateRaceButton"..index];
        button.uiSlot = index;
        button.raceID = raceID;
        button.raceFileString = fileString;
        button.raceInfo = _G.RaceInfoByFileString and _G.RaceInfoByFileString[strupper(fileString)] or nil;

        if not hiddenRace then
            if faction == "Alliance" then
                table.insert(CharacterCreate.raceIndicesAlliance, index)
            else
                table.insert(CharacterCreate.raceIndicesHorde, index)
            end
        end
        local borderColor

        if faction == "Alliance" then
            borderColor = {0.0, 0.4, 1.0}
        else
            borderColor = {1.0, 0.0, 0.0}
        end

        if not button.staticTexture then
            button.staticTexture = button:CreateTexture(button:GetName().."StaticTexture", "ARTWORK");
            button.staticTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F");
            button.staticTexture:SetSize(112, 112);
            button.staticTexture:SetPoint("CENTER", 0, 0);
            button.staticTexture:SetVertexColor(borderColor[1], borderColor[2], borderColor[3]);
        else
            button.staticTexture:SetVertexColor(borderColor[1], borderColor[2], borderColor[3]);
        end
        button.staticTexture:Hide();

        if not button.highlightTexture then
            button.highlightTexture = button:CreateTexture(button:GetName().."HighlightTexture", "HIGHLIGHT");
            button.highlightTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F");
            button.highlightTexture:SetAlpha(0.5);
            button.highlightTexture:SetBlendMode("ADD");
            button.highlightTexture:SetSize(112, 112);
            button.highlightTexture:SetPoint("CENTER", 0, 0);
            button.highlightTexture:SetVertexColor(borderColor[1], borderColor[2], borderColor[3]);
        else
            button.highlightTexture:SetVertexColor(borderColor[1], borderColor[2], borderColor[3]);
        end

        if not button.checkedTexture then
            button.checkedTexture = button:CreateTexture(button:GetName().."CheckedTexture", "OVERLAY");
            button.checkedTexture:SetDrawLayer("OVERLAY", 7);
            button.checkedTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorderRace_H");
            button.checkedTexture:SetBlendMode("ADD");
            button.checkedTexture:SetSize(112, 112);
            button.checkedTexture:SetPoint("CENTER", 0, 0);
            button.checkedTexture:Hide();
        end

        if not button.texturesMoved then
            button:SetScript("OnMouseDown", nil);
            button:SetScript("OnMouseUp", nil);

            button.texturesMoved = true;
        end

        if hiddenRace then
            hiddenSelection = hiddenSelection or GetSelectedRace() == index;
            button.enable = false
            button:SetChecked(nil)
            button:Disable()
            button:Hide()
        else
            button:Show();
        end
        if not hiddenRace and ( select(i+2, ...) == 1 ) then
            button.enable = true;
            SetButtonDesaturated(button);
            button.name = select(i, ...)
            button.tooltip = select(i, ...);

            button:SetScript("OnEnter", function(self)
                if self:IsEnabled() then
                    local tooltip = GetOrCreateRaceTooltip(self)
                    tooltip:Show()
                    UpdateRaceTooltip(self, false)
                end
            end)

            button:SetScript("OnLeave", function(self)
                if self:IsEnabled() then
                    local tooltip = GetOrCreateRaceTooltip(self)
                    tooltip:Hide()
                end
            end)

            button:SetScript("OnMouseDown", function(self, clickedButton)
                if self:IsEnabled() and clickedButton == "RightButton" then
                    local tooltip = GetOrCreateRaceTooltip(self)
                    local isDetailed = detailedRaceTooltips[self]
                    UpdateRaceTooltip(self, not isDetailed)
                end
            end)
		elseif not hiddenRace then
            button.enable = false;
            SetButtonDesaturated(button, 1);
            button.name = select(i, ...)
            button.tooltip = _G[strupper(select(i+1, ...).."_".."DISABLED")];
        end
        index = index + 1;
    end
    for i=CharacterCreate.numRaces + 1, MAX_RACES, 1 do
        _G["CharacterCreateRaceButton"..i]:Hide();
    end
    if hiddenSelection and CharacterCreate.raceIndicesAlliance[1] then
        SetSelectedRace(CharacterCreate.raceIndicesAlliance[1])
    end
    CharacterCreate_PositionRaceButtons()
end

function CharacterCreateEnumerateClasses(...)
    CharacterCreate.numClasses = select("#", ...)/3;
    if ( CharacterCreate.numClasses > MAX_CLASSES_PER_RACE ) then
        message("Too many classes!  Update MAX_CLASSES_PER_RACE");
        return;
    end
    local coords;
    local index = 1;
    local button;
    for i=1, select("#", ...), 3 do
        coords = CLASS_ICON_TCOORDS[strupper(select(i+1, ...))];
        _G["CharacterCreateClassButton"..index.."NormalTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
        _G["CharacterCreateClassButton"..index.."PushedTexture"]:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
        button = _G["CharacterCreateClassButton"..index];

        if not button.staticTexture then
            button.staticTexture = button:CreateTexture(button:GetName().."StaticTexture", "ARTWORK");
            button.staticTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
            button.staticTexture:SetSize(112, 112);
            button.staticTexture:SetPoint("CENTER", 0, 0);
        end

        if not button.highlightTexture then
            button.highlightTexture = button:CreateTexture(button:GetName().."HighlightTexture", "HIGHLIGHT");
            button.highlightTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
            button.highlightTexture:SetAlpha(0.5);
            button.highlightTexture:SetBlendMode("ADD");
            button.highlightTexture:SetSize(112, 112);
            button.highlightTexture:SetPoint("CENTER", 0, 0);
        end

        if not button.checkedTexture then
            button.checkedTexture = button:CreateTexture(button:GetName().."CheckedTexture", "OVERLAY");
            button.checkedTexture:SetDrawLayer("OVERLAY", 7);
            button.checkedTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorderRace_H");
            button.checkedTexture:SetBlendMode("ADD");
            button.checkedTexture:SetSize(112, 112);
            button.checkedTexture:SetPoint("CENTER", 0, 0);
            button.checkedTexture:Hide();
        end

        if not button.nameFrame then
            button.nameFrame = CreateFrame("Frame", nil, button);
            button.nameFrame:SetSize(112, 40);
            button.nameFrame:SetPoint("TOP", button, "BOTTOM", 0, -5);

            button.nameFrame.text = button.nameFrame:CreateFontString(nil, "OVERLAY");
            button.nameFrame.text:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE");
            button.nameFrame.text:SetPoint("CENTER", 0, 0);
            button.nameFrame.text:SetTextColor(1, 1, 1);
            button.nameFrame.text:SetJustifyH("CENTER");
            button.nameFrame.text:SetJustifyV("CENTER");
            button.nameFrame.text:SetWidth(112);
            button.nameFrame.text:SetWordWrap(true);
            button.nameFrame.text:SetHeight(40);
        end

        if not button.texturesMoved then
            button:SetScript("OnMouseDown", nil);
            button:SetScript("OnMouseUp", nil);

            button.texturesMoved = true;
        end

        button:Show();

        local className = select(i, ...);

        if className == "Caballero de la Muerte" then
            button.nameFrame.text:SetText("Caballero\nde la Muerte");
        elseif string.len(className) > 12 then
            local spacePos = string.find(className, " ", 6)
            if spacePos then
                local firstPart = string.sub(className, 1, spacePos - 1);
                local secondPart = string.sub(className, spacePos + 1);
                button.nameFrame.text:SetText(firstPart .. "\n" .. secondPart);
            else
                local midPoint = math.floor(string.len(className) / 2);
                local firstPart = string.sub(className, 1, midPoint);
                local secondPart = string.sub(className, midPoint + 1);
                button.nameFrame.text:SetText(firstPart .. "-\n" .. secondPart);
            end
        else
            button.nameFrame.text:SetText(className);
        end

        if ( (select(i+2, ...) == 1) and (IsRaceClassValid(CharacterCreate.selectedRace, index)) ) then
            button.enable = true;
            button:Enable();
            SetButtonDesaturated(button);
            button.name = select(i, ...)
            button.tooltip = select(i, ...);
            _G["CharacterCreateClassButton"..index.."DisableTexture"]:Hide();

            button.nameFrame.text:SetTextColor(1, 1, 1);

            button:SetScript("OnEnter", function(self)
                if self:IsEnabled() then
                    local tooltip = GetOrCreateClassTooltip(self)
                    tooltip:Show()
                    UpdateClassTooltip(self, false)
                end
            end)

            button:SetScript("OnLeave", function(self)
                if self:IsEnabled() then
                    local tooltip = GetOrCreateClassTooltip(self)
                    tooltip:Hide()
                end
            end)

            button:SetScript("OnMouseDown", function(self, clickedButton)
                if self:IsEnabled() and clickedButton == "RightButton" then
                    local tooltip = GetOrCreateClassTooltip(self)
                    local isDetailed = detailedClassTooltips[self]
                    UpdateClassTooltip(self, not isDetailed)
                end
            end)
        else
            button.enable = false;
            button:Disable();
            SetButtonDesaturated(button, 1);
            button.name = select(i, ...)
            button.tooltip = _G[strupper(select(i+1, ...).."_".."DISABLED")];
            _G["CharacterCreateClassButton"..index.."DisableTexture"]:Show();

            button.nameFrame.text:SetTextColor(0.5, 0.5, 0.5);

            button:SetScript("OnEnter", function(self)
                local tooltip = GetOrCreateClassTooltip(self)
                tooltip:Show()
                UpdateClassTooltip(self, false)
            end)

            button:SetScript("OnLeave", function(self)
                local tooltip = GetOrCreateClassTooltip(self)
                tooltip:Hide()
            end)
        end

        index = index + 1;
    end

    for i=CharacterCreate.numClasses + 1, MAX_CLASSES_PER_RACE, 1 do
        _G["CharacterCreateClassButton"..i]:Hide();
    end
end

function CharacterCreate_CreateGenderButtonTextures()
    local maleButton = CharacterCreateGenderButtonMale;
    if maleButton and not maleButton.highlightTexture then
        maleButton.staticTexture = maleButton:CreateTexture(maleButton:GetName().."StaticTexture", "ARTWORK");
        maleButton.staticTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
        maleButton.staticTexture:SetSize(86, 86);
        maleButton.staticTexture:SetPoint("CENTER", 0, 0);

        maleButton.highlightTexture = maleButton:CreateTexture(maleButton:GetName().."HighlightTexture", "HIGHLIGHT");
        maleButton.highlightTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
        maleButton.highlightTexture:SetAlpha(0.5);
        maleButton.highlightTexture:SetBlendMode("ADD");
        maleButton.highlightTexture:SetSize(86, 86);
        maleButton.highlightTexture:SetPoint("CENTER", 0, 0);

        maleButton.checkedTexture = maleButton:CreateTexture(maleButton:GetName().."CheckedTexture", "OVERLAY");
        maleButton.checkedTexture:SetDrawLayer("OVERLAY", 7);
        maleButton.checkedTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorderRace_H");
        maleButton.checkedTexture:SetBlendMode("ADD");
        maleButton.checkedTexture:SetSize(86, 86);
        maleButton.checkedTexture:SetPoint("CENTER", 0, 0);
        maleButton.checkedTexture:Hide();

        maleButton:SetScript("OnMouseDown", nil);
        maleButton:SetScript("OnMouseUp", nil);
    end

    local femaleButton = CharacterCreateGenderButtonFemale;
    if femaleButton and not femaleButton.highlightTexture then
        femaleButton.staticTexture = femaleButton:CreateTexture(femaleButton:GetName().."StaticTexture", "ARTWORK");
        femaleButton.staticTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
        femaleButton.staticTexture:SetSize(86, 86);
        femaleButton.staticTexture:SetPoint("CENTER", 0, 0);

        femaleButton.highlightTexture = femaleButton:CreateTexture(femaleButton:GetName().."HighlightTexture", "HIGHLIGHT");
        femaleButton.highlightTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
        femaleButton.highlightTexture:SetAlpha(0.5);
        femaleButton.highlightTexture:SetBlendMode("ADD");
        femaleButton.highlightTexture:SetSize(86, 86);
        femaleButton.highlightTexture:SetPoint("CENTER", 0, 0);

        femaleButton.checkedTexture = femaleButton:CreateTexture(femaleButton:GetName().."CheckedTexture", "OVERLAY");
        femaleButton.checkedTexture:SetDrawLayer("OVERLAY", 7);
        femaleButton.checkedTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorderRace_H");
        femaleButton.checkedTexture:SetBlendMode("ADD");
        femaleButton.checkedTexture:SetSize(86, 86);
        femaleButton.checkedTexture:SetPoint("CENTER", 0, 0);
        femaleButton.checkedTexture:Hide();

        femaleButton:SetScript("OnMouseDown", nil);
        femaleButton:SetScript("OnMouseUp", nil);
    end
end

function SetCharacterRace(id)
    CharacterCreate.selectedRace = id;
    local selectedButton;
    for i=1, CharacterCreate.numRaces, 1 do
        local button = _G["CharacterCreateRaceButton"..i];
        if ( i == id ) then
            if button.nameFrame and button.nameFrame.text then
                button.nameFrame.text:SetText(button.name);
            end
            button:SetChecked(1);
            selectedButton = button;
        else
            if button.nameFrame and button.nameFrame.text then
                button.nameFrame.text:SetText("");
            end
            button:SetChecked(0);
        end
    end

    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate.selectedRaceID = selectedButton and (selectedButton.raceID or id) or id;
    CharacterCreate_UpdateButtonCheckedStates();

    local name, faction = GetFactionForRace(CharacterCreate.selectedRace);
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end
    if ( _G.GetFactionForRaceID ) then
        faction = _G.GetFactionForRaceID(CharacterCreate.selectedRaceID);
    end

    local race, fileString = GetNameForRace();

    CharacterCreateRaceLabel:SetText(race);
    fileString = strupper(fileString);
    if ( GetSelectedSex() == SEX_MALE ) then
        gender = "MALE";
    else
        gender = "FEMALE";
    end
    local raceKey = GetRaceIconTextureKey(fileString, gender, faction);
    local targetTexture = RACE_ICON_TEXTURES[raceKey];
    if targetTexture then
        CharacterCreateRaceIcon:SetTexture(targetTexture);
        CharacterCreateRaceIcon:SetTexCoord(0, 1, 0, 1);
    else
        local coords = RACE_ICON_TCOORDS[raceKey] or RACE_ICON_TCOORDS["HUMAN_"..(gender or "MALE")];
        CharacterCreateRaceIcon:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-RacesRound");
        CharacterCreateRaceIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
    end
    local raceText = _G["RACE_INFO_"..fileString];
    local abilityIndex = 1;
    local tempText = _G["ABILITY_INFO_"..fileString..abilityIndex];
    abilityText = "";
    while ( tempText ) do
        abilityText = abilityText..tempText.."\n\n";
        abilityIndex = abilityIndex + 1;
        tempText = _G["ABILITY_INFO_"..fileString..abilityIndex];
    end

    CharacterCreateRaceScrollFrameScrollBar:SetValue(0);
    CharacterCreateRaceText:SetText((GetFlavorText("RACE_INFO_"..strupper(fileString), GetSelectedSex()) or "").."|n|n");
    if ( abilityText and abilityText ~= "" ) then
        CharacterCreateRaceAbilityText:SetText(abilityText);
    else
        CharacterCreateRaceAbilityText:SetText("");
    end

    local backdropColor = FACTION_BACKDROP_COLOR_TABLE[faction] or FACTION_BACKDROP_COLOR_TABLE["Horde"] or FACTION_BACKDROP_COLOR_TABLE["Alliance"];
    local frame;
    for index, value in pairs(FRAMES_TO_BACKDROP_COLOR) do
        frame = _G[value];
        frame:SetBackdropColor(backdropColor[4], backdropColor[5], backdropColor[6]);
    end
    CharacterCreateConfigurationBackground:SetVertexColor(backdropColor[4], backdropColor[5], backdropColor[6]);

    local backgroundFilename = GetCreateBackgroundModel();
    local selectedRaceButton = _G["CharacterCreateRaceButton"..(CharacterCreate.selectedRace or 0)];
    local selectedRaceKey = selectedRaceButton and selectedRaceButton.raceFileString;
    local RACE_BACKGROUND_KEYS = {
        ["ETHEREALHORDE"]="ORC",
        ["ETHEREALALLIANCE"]="HUMAN",
    ["VENTHYRHORDE"] = "SCOURGE",
    ["KYRIANALLIANCE"] = "DRAENEI",
        ["CENTAURHORDE"] = "ORC",
        ["CENTAURALLIANCE"] = "HUMAN",
        ["SLIMEGIANTHORDE"] = "ORC",
        ["SLIMEGIANTALLIANCE"] = "HUMAN",
        ["DRAKONIDHORDE"] = "ORC",
        ["DRAKONIDALLIANCE"] = "HUMAN",
        ["ARAKKOAHORDE"] = "ORC",
        ["DRAKKINHORDE"] = "HUMAN",
        ["DRAKKINALLIANCE"] = "HUMAN",
        ["JINYUALLIANCE"] = "HUMAN",
        ["SABERONHORDE"] = "ORC",
        ["GNOLLHORDE"] = "ORC",
        ["SETHRAKALLIANCE"] = "HUMAN",
        ["BROKEN"] = "ORC",
        ["BROKEN_ALLIANCE"] = "HUMAN",
        ["BROKEN_HORDE"] = "ORC",
        ["PANDAREN"] = "HUMAN",
        ["PANDAREN_ALLIANCE"] = "HUMAN",
        ["PANDAREN_HORDE"] = "ORC",
        ["VULPERA"] = "ORC",
        ["SETHRAK"] = "ORC",
        ["HIGHELF"] = "HUMAN",
        ["MAGHAR"] = "ORC",
        ["HARANIR"] = "NIGHTELF",
        ["HARANIRHORDE"] = "NIGHTELF",
        ["EARTHEN"] = "DWARF",
        ["EARTHENHORDE"] = "DWARF",
        ["HIGHMOUNTAINTAUREN"] = "TAUREN",
        ["MECHAGNOME"] = "GNOME",
        ["SKYBORNE"] = "HUMAN",
        ["SKYBORNEHORDE"] = "ORC",
        ["DARKFALLEN"] = "HUMAN",
        ["DARKFALLEN_ALLIANCE"] = "HUMAN",
        ["DARKFALLEN_HORDE"] = "ORC",
        ["DARKFALLENHORDE"] = "ORC",
        ["EREDAR"] = "ORC",
        ["NIGHTBORNE"] = "ORC",
        ["ZANDALARITROLL"] = "ORC",
        ["DRACTHYR"] = "ORC",
        ["ILLIDARI"] = "ORC",
        ["VOIDELF"] = "HUMAN",
        ["LIGHTFORGEDDRAENEI"] = "HUMAN",
        ["DARKIRONDWARF"] = "HUMAN",
        ["KULTIRAN"] = "HUMAN",
    };
    local mappedBackground = selectedRaceKey and RACE_BACKGROUND_KEYS[strupper(selectedRaceKey)];
    if ( mappedBackground ) then
        backgroundFilename = mappedBackground;
    end
    if ( not backgroundFilename or backgroundFilename == "" ) then
        if ( faction == "Alliance" ) then
            backgroundFilename = "HUMAN";
        else
            backgroundFilename = "ORC";
        end
    end
    SetBackgroundModel(CharacterCreate, backgroundFilename);
end

function SetCharacterClass(id)
    CharacterCreate.selectedClass = id;
    for i=1, CharacterCreate.numClasses, 1 do
        local button = _G["CharacterCreateClassButton"..i];
        if ( i == id ) then
            CharacterCreateClassName:SetText(button.name);
            button:SetChecked(1);
        else
            button:SetChecked(0);
        end
    end

    CharacterCreate_UpdateButtonCheckedStates();

    local className, classFileName, _, tank, healer, damage = GetSelectedClass();
    local abilityIndex = 0;
    local tempText = _G["CLASS_INFO_"..classFileName..abilityIndex];
    abilityText = "";
    while ( tempText ) do
        abilityText = abilityText..tempText.."\n\n";
        abilityIndex = abilityIndex + 1;
        tempText = _G["CLASS_INFO_"..classFileName..abilityIndex];
    end
    local coords = CLASS_ICON_TCOORDS[classFileName];
    CharacterCreateClassIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
    CharacterCreateClassLabel:SetText(className);
    CharacterCreateClassRolesText:SetText(abilityText);
    CharacterCreateClassText:SetText(GetFlavorText("CLASS_"..strupper(classFileName), GetSelectedSex()).."|n|n");
    CharacterCreateClassScrollFrameScrollBar:SetValue(0);
end

function CharacterCreate_OnChar()
end

function CharacterCreate_UpdateButtonCheckedStates()
    for i=1, MAX_RACES, 1 do
        local button = _G["CharacterCreateRaceButton"..i];
        if button and button:IsShown() and button.checkedTexture then
            if button:GetChecked() then
                button.checkedTexture:Show();
            else
                button.checkedTexture:Hide();
            end
        end
    end

    for i=1, MAX_CLASSES_PER_RACE, 1 do
        local button = _G["CharacterCreateClassButton"..i];
        if button and button:IsShown() and button.checkedTexture then
            if button:GetChecked() then
                button.checkedTexture:Show();
            else
                button.checkedTexture:Hide();
            end
        end
    end

    local maleButton = CharacterCreateGenderButtonMale;
    local femaleButton = CharacterCreateGenderButtonFemale;

    if maleButton and maleButton.checkedTexture then
        if maleButton:GetChecked() then
            maleButton.checkedTexture:Show();
        else
            maleButton.checkedTexture:Hide();
        end
    end

    if femaleButton and femaleButton.checkedTexture then
        if femaleButton:GetChecked() then
            femaleButton.checkedTexture:Show();
        else
            femaleButton.checkedTexture:Hide();
        end
    end
end

function CharacterCreate_OnKeyDown(key)
    if ( key == "ESCAPE" ) then
        if (CharacterCreate.personalizationMode == true) then
            CharacterCreate_TogglePersonalization();
        else
            CharacterCreate_Back();
        end
    elseif ( key == "ENTER" ) then
        CharacterCreate_Okay();
    elseif ( key == "PRINTSCREEN" ) then
        Screenshot();
    end
end

function CharacterCreate_UpdateModel(self)
    UpdateCustomizationScene();
    self:AdvanceTime();
end

function CharacterCreate_Okay()
    if ( PAID_SERVICE_TYPE ) then
        GlueDialog_Show("CONFIRM_PAID_SERVICE");
    else
        CreateCharacter(CharacterCreate_GetFullName());
    end

    CharacterCreate.personalizationMode = false;
    PlaySound("gsCharacterCreationCreateChar");
end

function CharacterCreate_Back()
    if (CharacterCreate.personalizationMode == true) then
        CharacterCreate_TogglePersonalization();
        CharacterCreate.personalizationMode = false;
        return;
    end

    CharacterCreate.personalizationMode = false;
    PlaySound("gsCharacterCreationCancel");
    SetGlueScreen("charselect");
end

function CharacterClass_OnClick(id)
    PlaySound("gsCharacterCreationClass");
    local _,_,currClass = GetSelectedClass();
    if ( currClass ~= id and IsRaceClassValid(GetSelectedRace(), id) ) then
        SetSelectedClass(id);
        SetCharacterClass(id);
        SetCharacterRace(GetSelectedRace());
        CharacterChangeFixup();

        CharacterCreate_UpdateButtonCheckedStates();
    end
end

function CharacterRace_OnClick(self, id)
    PlaySound("gsCharacterCreationClass");
    if ( not self:GetChecked() ) then
        self:SetChecked(1);
        return;
    end
    if ( GetSelectedRace() ~= id ) then
        local target = _G["CharacterCreateRaceButton"..id];
        if target and CharacterCreate_IsMaleOnlyRace(target.raceID) then
            SetSelectedSex(SEX_MALE);
        end
        SetSelectedRace(id);
        SetCharacterRace(id);
        SetSelectedSex(GetSelectedSex());
        SetCharacterCreateFacing(-15);
        CharacterCreateEnumerateClasses(GetAvailableClasses());
        local _,_,classIndex = GetSelectedClass();
        if ( PAID_SERVICE_TYPE ) then
            classIndex = PaidChange_GetCurrentClassIndex();
        end
        SetCharacterClass(classIndex);

        CharacterCreate_UpdateHairCustomization();

        CharacterChangeFixup();

        CharacterCreate_UpdateButtonCheckedStates();
    end
end

function SetCharacterGender(sex)
    if CharacterCreate_IsMaleOnlyRace(CharacterCreate.selectedRaceID) then sex = SEX_MALE; end
    local gender;
    SetSelectedSex(sex);
    if ( sex == SEX_MALE ) then
        gender = "MALE";
        CharacterCreateGender:SetText(MALE);
        CharacterCreateGenderButtonMale:SetChecked(1);
        CharacterCreateGenderButtonFemale:SetChecked(nil);
    elseif ( sex == SEX_FEMALE ) then
        gender = "FEMALE";
        CharacterCreateGender:SetText(FEMALE);
        CharacterCreateGenderButtonMale:SetChecked(nil);
        CharacterCreateGenderButtonFemale:SetChecked(1);
    end

    CharacterCreateEnumerateRaces(GetAvailableRaces());
    CharacterCreateEnumerateClasses(GetAvailableClasses());
     SetCharacterRace(GetSelectedRace());

    local _,_,classIndex = GetSelectedClass();
    if ( PAID_SERVICE_TYPE ) then
        classIndex = PaidChange_GetCurrentClassIndex();
    end
    SetCharacterClass(classIndex);

    CharacterCreate_UpdateHairCustomization();

    local race, fileString = GetNameForRace();
    CharacterCreateRaceLabel:SetText(race);
    fileString = strupper(fileString);
    local _, faction = GetFactionForRace(CharacterCreate.selectedRace);
    local raceKey = GetRaceIconTextureKey(fileString, gender, faction);
    local targetTexture = RACE_ICON_TEXTURES[raceKey];
    if targetTexture then
        CharacterCreateRaceIcon:SetTexture(targetTexture);
        CharacterCreateRaceIcon:SetTexCoord(0, 1, 0, 1);
    else
        local coords = RACE_ICON_TCOORDS[raceKey] or RACE_ICON_TCOORDS["HUMAN_"..(gender or "MALE")];
        CharacterCreateRaceIcon:SetTexture("Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-RacesRound");
        CharacterCreateRaceIcon:SetTexCoord(coords[1], coords[2], coords[3], coords[4]);
    end

    CharacterChangeFixup();

    CharacterCreate_UpdateButtonCheckedStates();
end

function CharacterCreate_ResetState()
    CharacterCreate.personalizationMode = false;
    CharacterCreateRaceButtonsContainer:Show();
    CharacterCreateClassButtonsContainer:Show();
    CharacterCreateGenderButtonsContainer:Show();
    CustomizationLogoAlliance:Show();
    CustomizationTextAlliance:Show();
    CustomizationLogoHorde:Show();
    CustomizationTextHorde:Show();
    CharacterCreateRotateLeft:Hide();
    CharacterCreateRotateRight:Hide();
    CharCreateRandomizeButton:Hide();
    CharacterCreateNameEdit:Hide();
    CharacterCreateLastNameEdit:Hide();
    CharacterCreateRandomName:Hide();
    CharacterCreateRandomLastName:Hide();
    CharCreatePersonalizeButton:Show();
    CharCreateOkayButton:Hide();

    for i=1, NUM_CHAR_CUSTOMIZATIONS do
        _G["CharacterCustomizationButtonFrame"..i]:Hide();
    end
end

function CharacterCustomization_Left(id)
    PlaySound("gsCharacterCreationLook");
    CycleCharCustomization(id, -1);
end

function CharacterCustomization_Right(id)
    PlaySound("gsCharacterCreationLook");
    CycleCharCustomization(id, 1);
end

function CharacterCreate_Randomize()
    PlaySound("gsCharacterCreationLook");
    if CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 46 or CharacterCreate.selectedRaceID == 48 or CharacterCreate.selectedRaceID == 49 or CharacterCreate.selectedRaceID == 50 or CharacterCreate.selectedRaceID == 51 or (CharacterCreate.selectedRaceID == 62 or CharacterCreate.selectedRaceID == 63 or CharacterCreate.selectedRaceID == 64) then CycleCharCustomization("EA_RANDOM",1);
    else RandomizeCharCustomization(); end
end

function CharacterCreateRotateRight_OnUpdate(self)
    if ( self:GetButtonState() == "PUSHED" ) then
        SetCharacterCreateFacing(GetCharacterCreateFacing() + CHARACTER_FACING_INCREMENT);
    end
end

function CharacterCreateRotateLeft_OnUpdate(self)
    if ( self:GetButtonState() == "PUSHED" ) then
        SetCharacterCreateFacing(GetCharacterCreateFacing() - CHARACTER_FACING_INCREMENT);
    end
end

function CharacterCreate_UpdateHairCustomization()
    CharacterCustomizationButtonFrame3Text:SetText(_G["HAIR_"..GetHairCustomization().."_STYLE"]);
    CharacterCustomizationButtonFrame4Text:SetText(_G["HAIR_"..GetHairCustomization().."_COLOR"]);
    CharacterCustomizationButtonFrame5Text:SetText(_G["FACIAL_HAIR_"..GetFacialHairCustomization()]);
    -- Skyborne legacy labels
    if CharacterCreate.selectedRaceID == 52 or CharacterCreate.selectedRaceID == 53 then
        CharacterCustomizationButtonFrame3Text:SetText("Hair Style");
        CharacterCustomizationButtonFrame4Text:SetText("Hair Color");
        CharacterCustomizationButtonFrame5Text:SetText("Features");
    end
end

function SetButtonDesaturated(button, desaturated, r, g, b)
    if ( not button ) then
        return;
    end
    local icon = button:GetNormalTexture();
    if ( not icon ) then
        return;
    end
    local shaderSupported = icon:SetDesaturated(desaturated);

    if ( not desaturated ) then
        r = 1.0;
        g = 1.0;
        b = 1.0;
    elseif ( not r or not shaderSupported ) then
        r = 0.5;
        g = 0.5;
        b = 0.5;
    end

    icon:SetVertexColor(r, g, b);
end

function GetFlavorText(tagname, sex)
    local primary, secondary;
    if ( sex == SEX_MALE ) then
        primary = "";
        secondary = "_FEMALE";
    else
        primary = "_FEMALE";
        secondary = "";
    end
    local text = _G[tagname..primary];
    if ( (text == nil) or (text == "") ) then
        text = _G[tagname..secondary];
    end
    return text;
end

function CharacterCreate_DeathKnightSwap(self)
    local _, classFilename = GetSelectedClass();
    if ( classFilename == "DEATHKNIGHT" ) then
        if (self.currentModel ~= "DEATHKNIGHT") then
            self.currentModel = "DEATHKNIGHT";
            self:SetNormalTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Up-Blue");
            self:SetPushedTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Down-Blue");
            self:SetHighlightTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight-Blue");
        end
    else
        if (self.currentModel == "DEATHKNIGHT") then
            self.currentModel = nil;
            self:SetNormalTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Up");
            self:SetPushedTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Down");
            self:SetHighlightTexture("Interface\\Glues\\Common\\Glue-Panel-Button-Highlight");
        end
    end
end

function CharacterChangeFixup()
    if ( PAID_SERVICE_TYPE ) then
        for i=1, MAX_CLASSES_PER_RACE, 1 do
            if (CharacterCreate.selectedClass ~= i) then
                local button = _G["CharacterCreateClassButton"..i];
                button:Disable();
                SetButtonDesaturated(button, true)
            end
        end

        for i=1, MAX_RACES, 1 do
            local allow = false;
            if ( PAID_SERVICE_TYPE == PAID_FACTION_CHANGE ) then
                local faction = GetFactionForRace(PaidChange_GetCurrentRaceIndex());
                if ( (i == PaidChange_GetCurrentRaceIndex()) or ((GetFactionForRace(i) ~= faction) and (IsRaceClassValid(i,CharacterCreate.selectedClass))) ) then
                    allow = true;
                end
            elseif ( PAID_SERVICE_TYPE == PAID_RACE_CHANGE ) then
                local faction = GetFactionForRace(PaidChange_GetCurrentRaceIndex());
                if ( (i == PaidChange_GetCurrentRaceIndex()) or ((GetFactionForRace(i) == faction) and (IsRaceClassValid(i,CharacterCreate.selectedClass))) ) then
                    allow = true
                end
            elseif ( PAID_SERVICE_TYPE == PAID_CHARACTER_CUSTOMIZATION ) then
                if ( i == CharacterCreate.selectedRace ) then
                    allow = true
                end
            end
            if (not allow) then
                local button = _G["CharacterCreateRaceButton"..i];
                button:Disable();
                SetButtonDesaturated(button, true)
            end
        end
    end
end

-- custom race names
if not _G["RACE_14"] then _G["RACE_14"] = "Broken" end
if not _G["RACE_15"] then _G["RACE_15"] = "Sethrak" end
if not _G["RACE_16"] then _G["RACE_16"] = "Eredar" end
if not _G["RACE_17"] then _G["RACE_17"] = "Nightborne" end
if not _G["RACE_18"] then _G["RACE_18"] = "Pandaren" end
if not _G["RACE_19"] then _G["RACE_19"] = "Void Elf" end
if not _G["RACE_20"] then _G["RACE_20"] = "Vulpera" end
if not _G["RACE_21"] then _G["RACE_21"] = "Lightforged Draenei" end
if not _G["RACE_22"] then _G["RACE_22"] = "Zandalari Troll" end
if not _G["RACE_23"] then _G["RACE_23"] = "Dark Iron Dwarf" end
if not _G["RACE_28"] then _G["RACE_28"] = "Dracthyr" end
if not _G["RACE_29"] then _G["RACE_29"] = "Kul Tiran" end
if not _G["RACE_30"] then _G["RACE_30"] = "Illidari" end
if not _G["RACE_31"] then _G["RACE_31"] = "Illidari" end


-- ============================================================
-- mod-classless-wildcard: single-class ("Hero") creation screen
-- Hides the leftover class-selection button. There is only one
-- class, so the picker is noise; the Hero name and description
-- stay. Selection state is untouched (SetCharacterClass still
-- runs via GetSelectedClass), so Accept works normally.
-- ============================================================
if not ClasslessWildcard_HideClass then
    ClasslessWildcard_HideClass = true;
    local _cw_orig_enumerate = CharacterCreateEnumerateClasses;
    function CharacterCreateEnumerateClasses(...)
        _cw_orig_enumerate(...);
        local maxc = MAX_CLASSES_PER_RACE or 10;
        for i = 1, maxc do
            local b = _G["CharacterCreateClassButton"..i];
            if b then b:Hide(); end
        end
    end
end

-- >>> freeborn-third-team (managed block, do not edit) >>>
-- ============================================================
-- Freeborn third player team: character-creation selection.
--
-- This block also paints the Freeborn emblem beside a Freeborn
-- character's name on the character-select screen.
--
-- The Freeborn choice is NOT carried by the create packet. The only
-- create field Lua can influence is the name, and a Freeborn name has
-- to follow exactly the same rules as an Alliance or Horde one, so the
-- name is sent untouched and the selection is recorded for the in-world
-- addon `Interface/AddOns/FreebornClaim`, which claims it once the
-- character exists. characters.teamId stays the only source of truth.
--
-- LOAD ORDER: the XML creates the CharacterCreate frame AFTER this
-- file has been executed, so nothing here may touch a frame at load
-- time. A top-level `CharacterCreate.selectedFreeborn = ...` is a nil
-- index, and because it aborts the whole chunk it also prevents every
-- function below from being defined -- leaving the button
-- unpositioned and untextured. Every frame access lives inside a
-- function; the selection state is created on first use.
--
-- GlueXML.toc loads CharacterSelect.xml before CharacterCreate.xml, so by
-- the time this block runs the select screen's globals already exist and
-- can be wrapped here without touching Interface/GlueXML/CharacterSelect.lua.
--
-- THREE RULES THIS SCREEN FORCES, all learned the hard way:
--
--   1. `GetCVar`/`SetCVar` raise "Couldn't find CVar named '<x>'" for any
--      name the client does not already know. Never call either outside
--      pcall -- an uncaught glue error here takes the create screen down
--      and can crash the client.
--   2. An argument is evaluated BEFORE pcall is entered, so
--      `pcall(f, builder())` does NOT protect the builder. Do the work
--      inside the pcall, or pcall the builder separately.
--   3. Only names the engine registers at the login screen are writable.
--      Being present in Config.wtf does not imply SetCVar accepts it, and
--      a cvar is free to store a value as 0/1 -- which is why the record
--      is judged live rather than assumed.
-- ============================================================
if not CharacterFreeborn_Init then
    CharacterFreeborn_Init = true;

    -- Cvar names this client accepts, used to carry the hash of the chosen name from here to the
    -- world. This only has to survive within one session -- the create screen and the login that
    -- claims it are the same client run -- so a cvar the client rewrites at startup is fine here.
    CharacterFreeborn_CarrierCVars = { "readTOS" };

    -- Where the emblem records live. This must survive a client restart, because the emblem is read
    -- on the character-select screen at the START of the next session, and every integer cvar tried
    -- was normalised when the client loaded it: readTOS and readEULA became "1", and gameTip had a
    -- record clipped to a valid tip index (the value was on disk, but not what the client loaded).
    -- So the store has to be a STRING cvar the client never parses. These two are voice-chat device
    -- names, and voice chat does not exist in 3.3.5, so arbitrary text there is inert. Because they
    -- are strings, one of them holds every Freeborn record at once.
    -- Dedicated restart-persistent badge slot. ECS deliberately does not use
    -- Sound_VoiceChatInputDriverName anymore; sharing it caused recursive
    -- fb:/ecsN: wrapping and eventual Config.wtf corruption.
    CharacterFreeborn_BadgeCVars = { "Sound_VoiceChatInputDriverName" };
    CharacterFreeborn_BadgeMarker = "fb:";

    -- The stock slot is 60x60. The Alliance/Horde crests sit inside their artwork, while the
    -- Freeborn plate fills its image edge to edge, so it reads larger at the same size. Draw it
    -- smaller inside the very same 60x60 box (centred on it) so the three match.
    CharacterFreeborn_BadgeSize = 44;

    -- A record is 1000000000 + (hash % 1000000000): always exactly ten digits, always below
    -- INT_MAX, and never confusable with a cvar's own resting value ("1", "76", "0"). This client
    -- rejects values of other shapes -- a "fb:"-prefixed string was silently refused -- so a plain
    -- number is the only shape proven to survive. Must match the addon's copy.
    CharacterFreeborn_RecordBase = 1000000000;
    CharacterFreeborn_RecordMod = 1000000000;

    CharacterFreeborn_BadgeTexture = "Interface\\Glues\\CharacterSelect\\FreebornLogo";

    function CharacterFreeborn_IsSelected()
        return CharacterCreate ~= nil and CharacterCreate.selectedFreeborn == true;
    end

    -- Every CVar access goes through these two. On this client an unknown name is a hard error.
    function CharacterFreeborn_SafeGet(name)
        local ok, value = pcall(GetCVar, name);
        if ( not ok ) then
            return nil;
        end
        return value;
    end

    function CharacterFreeborn_SafeSet(name, value)
        local ok = pcall(SetCVar, name, value);
        return ok and true or false;
    end

    -- Small non-cryptographic hash, so the choice can ride cvars the client parses as numbers.
    -- Must stay identical to the addon's copy.
    function CharacterFreeborn_NameHash(name)
        local hash = 0;
        local lowered = strlower(name or "");
        for i = 1, strlen(lowered) do
            hash = (hash * 31 + strbyte(lowered, i)) % 2147483647;
        end
        return hash;
    end

    function CharacterFreeborn_UpdateButton()
        local button = CharacterCreateFreebornButton;
        if ( not button ) then
            return;
        end

        if ( CharacterFreeborn_IsSelected() ) then
            button:SetChecked(1);
        else
            button:SetChecked(nil);
        end

        if ( button.checkedTexture ) then
            if ( CharacterFreeborn_IsSelected() ) then
                button.checkedTexture:Show();
            else
                button.checkedTexture:Hide();
            end
        end
    end

    function CharacterFreeborn_SetSelected(selected)
        if ( CharacterCreate == nil ) then
            return;
        end

        CharacterCreate.selectedFreeborn = selected and true or false;
        CharacterFreeborn_UpdateButton();
    end

    function CharacterFreeborn_OnClick(self)
        PlaySound("gsCharacterCreationClass");
        CharacterFreeborn_SetSelected(not CharacterFreeborn_IsSelected());
    end

    -- Hover tooltip for the Freeborn pick. It explains what the pick does, and it wears the same
    -- border as the race tooltips -- same background, same edge texture, same 16px tile and 4px
    -- insets -- so the two read as the same widget. The backdrop is spelled out here rather than
    -- borrowed from the stock `Backdrop2` local: this block is appended to the stock file, and an
    -- unresolved index on the create screen takes the whole screen down. The contract test pins
    -- these values against the stock race-tooltip backdrop so they cannot drift.
    function CharacterFreeborn_GetTooltip()
        local tooltip = CharacterFreebornTooltip;
        if ( tooltip ) then
            return tooltip;
        end

        tooltip = CreateFrame("Frame", "CharacterFreebornTooltip", CharacterCreateFrame);
        tooltip:SetFrameStrata("TOOLTIP");
        tooltip:SetBackdrop({
            bgFile = "Interface\\Tooltips\\UI-Tooltip-Background",
            edgeFile = "Interface\\Tooltips\\ui-tooltip-border-maw",
            tile = true, tileSize = 16, edgeSize = 16,
            insets = { left = 4, right = 4, top = 4, bottom = 4 } });
        tooltip:SetBackdropColor(0, 0, 0, 1);
        tooltip:SetWidth(300);

        tooltip.title = tooltip:CreateFontString(nil, "OVERLAY");
        tooltip.title:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE");
        tooltip.title:SetPoint("TOPLEFT", tooltip, "TOPLEFT", 10, -10);
        tooltip.title:SetTextColor(1, 0.82, 0);
        tooltip.title:SetJustifyH("LEFT");
        tooltip.title:SetText("|cFFFFFFFFFreeborn|r");

        tooltip.body = tooltip:CreateFontString(nil, "OVERLAY");
        tooltip.body:SetFont("Fonts\\FRIZQT__.TTF", 10, "OUTLINE");
        tooltip.body:SetPoint("TOPLEFT", tooltip.title, "BOTTOMLEFT", 0, -6);
        tooltip.body:SetWidth(260);
        tooltip.body:SetWordWrap(true);
        tooltip.body:SetJustifyH("LEFT");
        tooltip.body:SetText("|cffffd100Freeborn are free of both the Alliance and the Horde. They swear no "
            .. "allegiance to either, though both have granted them access to their cities.\n\n"
            .. "They may take quests from either faction, and they can group and guild freely with "
            .. "other Freeborn. They can never group or guild with Alliance or Horde characters.\n\n"
            .. "Freeborn and the two factions are completely hostile to one another.|r");

        CharacterFreebornTooltip = tooltip;
        return tooltip;
    end

    -- The stock race tooltip sizes itself from its wrapped text, so this does the same: the body
    -- has to be laid out before its height means anything.
    function CharacterFreeborn_UpdateTooltipSize(tooltip)
        tooltip:SetHeight(tooltip.title:GetHeight() + tooltip.body:GetHeight() + 26);
    end

    function CharacterFreeborn_HideTooltip()
        if ( CharacterFreebornTooltip ) then
            CharacterFreebornTooltip:Hide();
        end
    end

    -- Sit the tooltip above the plate, clear of the button's own "Freeborn" label.
    function CharacterFreeborn_OnEnter(self)
        local tooltip = CharacterFreeborn_GetTooltip();
        CharacterFreeborn_UpdateTooltipSize(tooltip);
        tooltip:ClearAllPoints();
        tooltip:SetPoint("BOTTOM", self, "TOP", 0, 24);
        tooltip:Show();
    end

    function CharacterFreeborn_OnLeave(self)
        CharacterFreeborn_HideTooltip();
    end

    -- The Freeborn pick is a plain plate: no round border ring, unlike the gender picks. Only the
    -- checked ring is kept, and only while Freeborn is actually selected, as click feedback.
    local freeborn_originalGenderTextures = CharacterCreate_CreateGenderButtonTextures;
    function CharacterCreate_CreateGenderButtonTextures(...)
        freeborn_originalGenderTextures(...);

        local button = CharacterCreateFreebornButton;
        if ( button and not button.highlightTexture ) then
            button.staticTexture = button:CreateTexture(button:GetName().."StaticTexture", "ARTWORK");
            button.staticTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
            button.staticTexture:SetAlpha(0);
            button.staticTexture:SetSize(86, 86);
            button.staticTexture:SetPoint("CENTER", 0, 0);

            button.highlightTexture = button:CreateTexture(button:GetName().."HighlightTexture", "HIGHLIGHT");
            button.highlightTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorder_F1");
            button.highlightTexture:SetAlpha(0);
            button.highlightTexture:SetBlendMode("ADD");
            button.highlightTexture:SetSize(86, 86);
            button.highlightTexture:SetPoint("CENTER", 0, 0);

            button.checkedTexture = button:CreateTexture(button:GetName().."CheckedTexture", "OVERLAY");
            button.checkedTexture:SetDrawLayer("OVERLAY", 7);
            button.checkedTexture:SetTexture("Interface\\Glues\\CharacterCreate\\IconBorderRace_H");
            button.checkedTexture:SetBlendMode("ADD");
            button.checkedTexture:SetSize(86, 86);
            button.checkedTexture:SetPoint("CENTER", 0, 0);
            button.checkedTexture:Hide();

            button:SetScript("OnMouseDown", nil);
            button:SetScript("OnMouseUp", nil);
        end

        CharacterFreeborn_UpdateButton();
    end

    -- Sit the Freeborn pick between the two gender picks. It chooses a team,
    -- never a gender, so it never calls SetSelectedSex. The XML anchors it in the
    -- same place, so a wrapper that never ran would still leave it visible.
    local freeborn_originalPositionGenderButtons = CharacterCreate_PositionGenderButtons;
    function CharacterCreate_PositionGenderButtons(...)
        freeborn_originalPositionGenderButtons(...);

        local button = CharacterCreateFreebornButton;
        if ( button ) then
            button:ClearAllPoints();
            button:SetPoint("CENTER", CharacterCreateFrame, "CENTER", 0, -250);
            if ( PAID_SERVICE_TYPE ) then
                CharacterFreeborn_HideTooltip();
                button:Hide();
            else
                button:Show();
            end
        end
    end

    -- Race/gender clicks re-run this, so an explicit Freeborn selection is kept
    -- across race and gender changes until the player deselects it.
    local freeborn_originalUpdateButtonCheckedStates = CharacterCreate_UpdateButtonCheckedStates;
    function CharacterCreate_UpdateButtonCheckedStates(...)
        freeborn_originalUpdateButtonCheckedStates(...);
        CharacterFreeborn_UpdateButton();
    end

    -- A fresh visit to the create screen starts on the race's native team.
    local freeborn_originalOnShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        CharacterFreeborn_SetSelected(false);
        CharacterFreeborn_HideTooltip();
        freeborn_originalOnShow(...);
        CharacterFreeborn_UpdateButton();
    end

    -- Record the choice for the addon, then run the stock accept path so the name reaches the
    -- server exactly as typed. Nothing here may raise. The record is written ONLY when Freeborn
    -- was chosen: an intervening native creation must not wipe an unclaimed choice, and a stale
    -- record cannot claim a later character because the addon also matches the name's hash.
    local freeborn_originalOkay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        local pending = "";
        if ( CharacterFreeborn_IsSelected() and not PAID_SERVICE_TYPE ) then
            if ( CharacterCreate_GetFullName ) then
                pending = CharacterCreate_GetFullName();
            else
                pending = CharacterCreateNameEdit:GetText();
            end
        end

        if ( pending ~= "" ) then
            local record = CharacterFreeborn_RecordFor(pending);
            for _, name in ipairs(CharacterFreeborn_CarrierCVars) do
                CharacterFreeborn_SafeSet(name, tostring(record));
            end
            -- Record the emblem here as well as from the addon on claiming. This screen can write
            -- cvars the world state refuses (readEULA is refused there), so the badge does not
            -- depend on that, and a record always claims a name, never a character that is not it.
            CharacterFreeborn_StoreBadge(record);
        end

        return freeborn_originalOkay(...);
    end

    -- The value the addon parks for a character, and the value this screen looks for.
    function CharacterFreeborn_RecordFor(name)
        return CharacterFreeborn_RecordBase + CharacterFreeborn_NameHash(name) % CharacterFreeborn_RecordMod;
    end

    -- The record format is "fb:<record>,<record>,...|<whatever the cvar held before>", so the
    -- client's own value is preserved behind the bar and all Freeborn records share one cvar.

    -- The hashes of every character the addon has claimed as Freeborn.
    function CharacterFreeborn_BadgeHashes()
        local hashes = {};
        local mark = strlen(CharacterFreeborn_BadgeMarker);

        for _, cvar in ipairs(CharacterFreeborn_BadgeCVars) do
            local stored = CharacterFreeborn_SafeGet(cvar);
            if ( type(stored) == "string" and strsub(stored, 1, mark) == CharacterFreeborn_BadgeMarker ) then
                local body = strsub(stored, mark + 1);
                local bar = strfind(body, "|", 1, true);
                if ( bar ) then
                    body = strsub(body, 1, bar - 1);
                end
                for digits in string.gmatch(body, "%d+") do
                    hashes[digits] = true;
                end
            end
        end

        return hashes;
    end

    -- Add this character's record to the first cvar that will take it, keeping whatever that cvar
    -- already held. Returns the cvar, or nil when no cvar would accept the write.
    function CharacterFreeborn_StoreBadge(record)
        local wanted = tostring(record - CharacterFreeborn_RecordBase);
        local mark = strlen(CharacterFreeborn_BadgeMarker);

        for _, cvar in ipairs(CharacterFreeborn_BadgeCVars) do
            local stored = CharacterFreeborn_SafeGet(cvar);
            if ( type(stored) ~= "string" ) then
                stored = "";
            end

            local original = stored;
            local body = "";
            if ( strsub(stored, 1, mark) == CharacterFreeborn_BadgeMarker ) then
                body = strsub(stored, mark + 1);
                local bar = strfind(body, "|", 1, true);
                if ( bar ) then
                    original = strsub(body, bar + 1);
                    body = strsub(body, 1, bar - 1);
                else
                    original = "";
                end
            end

            local list = {};
            for digits in string.gmatch(body, "%d+") do
                if ( digits == wanted ) then
                    return cvar;
                end
                list[#list + 1] = digits;
            end
            list[#list + 1] = wanted;

            CharacterFreeborn_SafeSet(cvar,
                CharacterFreeborn_BadgeMarker .. table.concat(list, ",") .. "|" .. original);

            local check = CharacterFreeborn_SafeGet(cvar);
            if ( type(check) == "string" and strsub(check, 1, mark) == CharacterFreeborn_BadgeMarker
                and strfind(check, wanted, 1, true) ) then
                return cvar;
            end
        end

        return nil;
    end

    -- Paint the Freeborn emblem over the Alliance/Horde logo for those characters. The stock list
    -- creates one FactionIcon texture per visible button and tags its button with the character's
    -- index, so the match is name-hash against the badge record.
    function CharacterFreeborn_ApplyBadges()
        if ( CharacterSelect == nil ) then
            return;
        end

        local hashes = CharacterFreeborn_BadgeHashes();
        if ( not next(hashes) ) then
            return;
        end

        for index = 1, ( MAX_CHARACTERS_DISPLAYED or 8 ) do
            local button = _G["CharSelectCharacterButton"..index];
            local icon = _G["CharSelectCharacterButton"..index.."FactionIcon"];
            if ( button and icon ) then
                local id = button:GetID() or 0;
                local name = nil;
                if ( id > 0 ) then
                    local ok, value = pcall(GetCharacterInfo, id);
                    if ( ok and type(value) == "string" ) then
                        name = value;
                    end
                end

                if ( name and hashes[tostring(CharacterFreeborn_RecordFor(name) - CharacterFreeborn_RecordBase)] ) then
                    -- The stock slot is anchored TOPLEFT (170,-5) at 60x60; keep the same centre.
                    icon:ClearAllPoints();
                    icon:SetPoint("CENTER", button, "TOPLEFT", 200, -35);
                    icon:SetSize(CharacterFreeborn_BadgeSize, CharacterFreeborn_BadgeSize);
                    icon:SetTexture(CharacterFreeborn_BadgeTexture);
                    icon:Show();
                end
            end
        end
    end

    local freeborn_originalUpdateCharacterList = UpdateCharacterList;
    function UpdateCharacterList(...)
        freeborn_originalUpdateCharacterList(...);
        pcall(CharacterFreeborn_ApplyBadges);
    end

    local freeborn_originalSelectOnEvent = CharacterSelect_OnEvent;
    function CharacterSelect_OnEvent(self, event, ...)
        freeborn_originalSelectOnEvent(self, event, ...);
        pcall(CharacterFreeborn_ApplyBadges);
    end
end
-- <<< freeborn-third-team (managed block) <<<

-- Esteria native appearance controls: logical options use the permanent executable callback.
local EA_LABELS = {"Skin Color", "Face", "Hair Style", "Hair Color", "Eye Color", "Eyebrow Style",
    "Feathers", "Feather Color", "Facial Hair", "Ears"};
local EA_MECH_LABELS = {"Skin Color","Face","Hair Style","Hair Color","Facial Hair","Arm Upgrade","Leg Upgrade","Modification","Eye Color","Paint","Eyesight","Eye Style"};
local EA_LEFT = CharacterCustomization_Left;
local EA_RIGHT = CharacterCustomization_Right;
local EA_UPDATE = CharacterCreate_UpdateHairCustomization;
-- Retail race customization (wowdecomp)
local function EA_IS_RETAIL()
    return type(CycleCharCustomization) == "function" and CycleCharCustomization("EA_RETAIL", 1) == 1;
end
local function EA_ACTIVE()
    return EA_IS_RETAIL() or CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 50 or CharacterCreate.selectedRaceID == 51 or CharacterCreate.selectedRaceID == 48 or CharacterCreate.selectedRaceID == 49 or CharacterCreate.selectedRaceID == 46 or CharacterCreate.selectedRaceID == 47 or CharacterCreate.selectedRaceID == 52 or CharacterCreate.selectedRaceID == 53 or (CharacterCreate.selectedRaceID == 75 or CharacterCreate.selectedRaceID == 76 or CharacterCreate.selectedRaceID == 54 or (CharacterCreate.selectedRaceID == 62 or CharacterCreate.selectedRaceID == 63 or CharacterCreate.selectedRaceID == 64));
end
-- Retail option capacity v2 (45-option Dracthyr)
local function EA_LIMIT()
    if EA_IS_RETAIL() then return math.min(64, CycleCharCustomization("EA_COUNT", 1) or 29); end
    return 29;
end
local function EA_REFRESH()
    if not CharacterCustomizationButtonFrame1 then return; end
    if BodyShapePage_Active and BodyShapePage_Active() then
        for i=1,64 do
            local row = _G["CharacterCustomizationButtonFrame"..i];
            if row then row:Hide(); end
        end
        return;
    end
    local parent = CharacterCustomizationButtonFrame1:GetParent();
    for i=6,64 do
        if not _G["CharacterCustomizationButtonFrame"..i] then
            local frame = CreateFrame("Frame", "CharacterCustomizationButtonFrame"..i,
                parent, "CharacterCustomizationFrameTemplate");
            frame:SetID(i);
        end
    end
    if EA_ACTIVE() then
        -- Esteria split customization columns: balance only selectable options.
        local optionCount=0;
        for i=1,64 do
            local _,count = CycleCharCustomization("EA_GET",i);
            if count and count>1 then optionCount=optionCount+1; end
        end
        local rowsPerColumn=math.max(1,math.ceil(optionCount/2));
        local rowSpacing=math.min(optionCount>22 and 28 or 36, math.max(18, math.floor((GetScreenHeight()-210)/rowsPerColumn))); 
        local visibleRow=0;
        for i=1,64 do
            local frame = _G["CharacterCustomizationButtonFrame"..i];
            frame:ClearAllPoints();
            local value,count,label = CycleCharCustomization("EA_GET",i);
            local side=visibleRow>=rowsPerColumn and "TOPRIGHT" or "TOPLEFT";
            frame:SetPoint(side, CharacterCreateFrame, side, side=="TOPRIGHT" and -55 or 55,
                -110-(visibleRow%rowsPerColumn)*rowSpacing);
            local earthenChoiceText = nil;
            if (CharacterCreate.selectedRaceID == 48 or CharacterCreate.selectedRaceID == 49) and label == "Belt" then
                earthenChoiceText = value == 0 and "  None" or "  Gem";
            end
            _G["CharacterCustomizationButtonFrame"..i.."Text"]:SetText(
                ((EA_IS_RETAIL() or CharacterCreate.selectedRaceID == 20 or CharacterCreate.selectedRaceID == 46 or CharacterCreate.selectedRaceID == 48 or CharacterCreate.selectedRaceID == 49 or CharacterCreate.selectedRaceID == 50 or CharacterCreate.selectedRaceID == 51 or (CharacterCreate.selectedRaceID == 75 or CharacterCreate.selectedRaceID == 76 or CharacterCreate.selectedRaceID == 54 or (CharacterCreate.selectedRaceID == 62 or CharacterCreate.selectedRaceID == 63 or CharacterCreate.selectedRaceID == 64))) and (label or "") or CharacterCreate.selectedRaceID == 47 and EA_MECH_LABELS[i] or EA_LABELS[i] or "")..(earthenChoiceText or (count and "  "..(value+1).."/"..count or "")));
            if count and count>1 then
                visibleRow=visibleRow+1;
                if CharCreateOkayButton:IsShown() then frame:Show(); else frame:Hide(); end
            else frame:Hide(); end
        end
    else
        CharacterCustomizationButtonFrame1Text:SetText(CHAR_CUSTOMIZATION1_DESC);
        CharacterCustomizationButtonFrame2Text:SetText(CHAR_CUSTOMIZATION2_DESC);
        for i=6,64 do _G["CharacterCustomizationButtonFrame"..i]:Hide(); end
        if CharacterCreate.selectedRaceID == 68 then
            _G["CharacterCustomizationButtonFrame5"]:Hide();
        end
        if CharacterCreate.selectedRaceID == 66 and GetSelectedSex() == SEX_MALE then
            for _,i in ipairs({2,5}) do _G["CharacterCustomizationButtonFrame"..i]:Hide(); end
            if CharCreateOkayButton:IsShown() then CharacterCustomizationButtonFrame3:Show();
            else CharacterCustomizationButtonFrame3:Hide(); end
            CharacterCustomizationButtonFrame3Text:SetText("Horns");
            if CharCreateOkayButton:IsShown() then CharacterCustomizationButtonFrame4:Show();
            else CharacterCustomizationButtonFrame4:Hide(); end
            CharacterCustomizationButtonFrame4Text:SetText("Eye Color");
        elseif CharacterCreate.selectedRaceID == 66 or CharacterCreate.selectedRaceID == 67 then
            for _,i in ipairs({2,3,5}) do _G["CharacterCustomizationButtonFrame"..i]:Hide(); end
            CharacterCustomizationButtonFrame4Text:SetText("Eye Color");
        end
        if CharacterCreate.selectedRaceID == 65 then
            CharacterCustomizationButtonFrame4Text:SetText("Fin Color");
            for _, index in ipairs({2,3,5}) do
                _G["CharacterCustomizationButtonFrame"..index]:Hide();
            end
        end
        if CharacterCreate.selectedRaceID == 69 or CharacterCreate.selectedRaceID == 70 then
            CharacterCustomizationButtonFrame2Text:SetText("Head & Tail");
            CharacterCustomizationButtonFrame3Text:SetText("Horns");
            CharacterCustomizationButtonFrame4Text:SetText("Eye Color");
            _G["CharacterCustomizationButtonFrame5"]:Hide();
        end
        for i=1,5 do
            local frame = _G["CharacterCustomizationButtonFrame"..i];
            frame:ClearAllPoints();
            frame:SetPoint("RIGHT", CharacterCreateFrame, "RIGHT", -55, 200-i*50);
        end
    end
end
function CharacterCustomization_Left(id)
    if EA_ACTIVE() then CycleCharCustomization("EA_CYCLE",id,-1); EA_REFRESH();
    else EA_LEFT(id); end
end
function CharacterCustomization_Right(id)
    if EA_ACTIVE() then CycleCharCustomization("EA_CYCLE",id,1); EA_REFRESH();
    else EA_RIGHT(id); end
end
function CharacterCreate_UpdateHairCustomization()
    EA_UPDATE(); EA_REFRESH();
end
local eaFrame = CreateFrame("Frame");
eaFrame:SetScript("OnUpdate",function(self,elapsed)
    self.elapsed=(self.elapsed or 0)+elapsed;
    if self.elapsed>.15 then self.elapsed=0; EA_REFRESH(); end
end);
-- Esteria customization dropdown: reuse the native selectors and Glue widgets.
local picker;

local function PickerState(id)
    if EA_ACTIVE() then
        return CycleCharCustomization("EA_GET", id);
    end
    return CycleCharCustomization("EA_STOCK_GET", id);
end

local VULPERA_CHOICE_NAMES = {
    [1] = {
        [1] = {"","","","","",""},
        [2] = {"","","","","","","",""},
        [3] = {""},
        [4] = {"Wanderer","Compact","Sharp","Rakish","Scruffy","Raised"},
        [5] = {"Long","Blunt","Flat","Short","Strong","Sharp"},
        [6] = {"","","","","","","","","","","","","","","Death Knight","","","","","","","","","","","","","","","Primalist - Earth","Primalist - Fire","Primalist - Air","Primalist - Water"},
        [7] = {"Unpierced","Pierced"},
        [8] = {"Desert","Forest","Plains"},
        [9] = {"Both","Right","Left","Neither"},
        [10] = {"Slit","Star","Glow"},
    },
    [2] = {
        [1] = {"","","","","",""},
        [2] = {"","","","","","","",""},
        [3] = {""},
        [4] = {"Desert","Sharp","Vigilant","Relaxed","Curled","Scruffy","Scarred","Calm"},
        [5] = {"Desert","Forest","Plains"},
        [6] = {"Long","Flat","Blunt","Round","Sharp","Short"},
        [7] = {"","","","","","","","","","","","","","","Death Knight","","","","","","","","","","","","","","","Primalist - Earth","Primalist - Fire","Primalist - Air","Primalist - Water"},
        [8] = {"Unpierced","Pierced"},
        [9] = {"Both","Right","Left","Neither"},
        [10] = {"Slit","Star","Glow"},
    },
};
local function ChoiceText(id, value, label)
    if EA_IS_RETAIL() then
        local retailName = CycleCharCustomization("EA_CHOICE_NAME", id, value);
        if retailName and retailName ~= "" then return retailName; end
    end
    if CharacterCreate.selectedRaceID == 66
        and GetSelectedSex() == SEX_MALE and id == 4 then
        return ({"Original", "Red", "Amber", "Green", "Blue", "Violet", "Silver"})[value + 1]
            or label.." "..(value + 1);
    end
    if CharacterCreate.selectedRaceID == 20 then
        local names = VULPERA_CHOICE_NAMES[GetSelectedSex()];
        local name = names and names[id] and names[id][value + 1];
        if name and name ~= "" then return name; end
    end
    if (CharacterCreate.selectedRaceID == 48 or CharacterCreate.selectedRaceID == 49) and label == "Belt" then
        return value == 0 and "None" or "Gem";
    end
    return label.." "..(value + 1);
end

local function PickerSignature()
    local parts = {CharacterCreate.selectedRaceID or 0, GetSelectedSex(), select(3, GetSelectedClass()) or 0};
    for i = 1, EA_ACTIVE() and EA_LIMIT() or 5 do
        local value = PickerState(i);
        parts[#parts + 1] = value or -1;
    end
    return table.concat(parts, ":");
end

function CharacterCustomization_CloseChoices()
    if picker then picker:Hide(); end
end

local function ApplyChoice(id, target)
    local value, count = PickerState(id);
    if not value or not count or value == target then return; end
    if EA_ACTIVE() then
        CycleCharCustomization("EA_SET", id, target);
    else
        -- Stock cycling may skip unavailable DBC rows; read the actual value after each step.
        for i = 1, count do
            local previous = value;
            CycleCharCustomization(id, 1);
            value = PickerState(id);
            if value == target or value == previous then break; end
        end
    end
    CharacterCreate_UpdateHairCustomization();
end

local function RestorePreview()
    if not picker or not picker.previewRow then return; end
    if PickerSignature() == picker.signature then
        CycleCharCustomization("EA_PREVIEW_RESTORE", 1);
        CharacterCreate_UpdateHairCustomization();
    end
    picker.previewRow = nil;
    picker.signature = PickerSignature();
end

local function PreviewChoice(self)
    if not picker:IsShown() or not picker.saved then return; end
    if PickerSignature() ~= picker.signature then picker:Hide(); return; end
    RestorePreview();
    ApplyChoice(picker.optionID, self.choice);
    picker.previewRow = self;
    picker.signature = PickerSignature();
end

local function SelectChoice(self)
    local id, target = picker.optionID, self.choice;
    picker:Hide();
    PlaySound("gsCharacterCreationLook");
    ApplyChoice(id, target);
end

local function ScrollChoices(_, delta)
    picker.scrollbar:SetValue(picker.offset - delta);
end

local function RefreshChoices()
    RestorePreview();
    local hovered;
    for i, row in ipairs(picker.rows) do
        local value = picker.choices[picker.offset + i];
        if value then
            row.choice = value;
            row:SetText(ChoiceText(picker.optionID, value, picker.label));
            if value == picker.selected then row:LockHighlight(); else row:UnlockHighlight(); end
            row:Show();
            if row.hovered then hovered = row; end
        else
            row:Hide();
        end
    end
    if hovered then PreviewChoice(hovered); end
end

function CharacterCustomization_OpenChoices(self)
    if picker and picker:IsShown() then
        local sameOwner = picker.owner == self;
        picker:Hide();
        if sameOwner then return; end
    end
    local id = self:GetParent():GetID();
    local value, count, label = PickerState(id);
    if not count or count < 2 then return; end
    label = label or _G[self:GetParent():GetName().."Text"]:GetText();
    label = label:gsub("%s+%d+/%d+$", ""):gsub("%s+None$", ""):gsub("%s+Gem$", "");
    local choices = {};
    local encoded = CycleCharCustomization(EA_ACTIVE() and "EA_CHOICES" or "EA_STOCK_CHOICES", id);
    for choice in string.gmatch(encoded or "", "%d+") do
        choices[#choices + 1] = tonumber(choice);
    end
    if #choices < 2 then return; end
    if not picker then
        picker = CreateFrame("Frame", "CharacterCustomizationChoiceMenu", CharacterCreateFrame);
        picker:SetFrameStrata("TOOLTIP");
        local dismiss = CreateFrame("Button", "CharacterCustomizationChoiceDismiss", CharacterCreateFrame);
        dismiss:SetAllPoints(CharacterCreateFrame);
        dismiss:SetFrameStrata("TOOLTIP");
        picker:SetFrameLevel(dismiss:GetFrameLevel() + 1);
        dismiss:SetScript("OnClick", CharacterCustomization_CloseChoices);
        picker:SetScript("OnShow", function()
            CycleCharCustomization("EA_WHEEL_BLOCK", 1);
            dismiss:Show();
        end);
        picker:SetScript("OnHide", function()
            RestorePreview();
            CycleCharCustomization("EA_PREVIEW_END", 1);
            CycleCharCustomization("EA_WHEEL_BLOCK", 0);
            picker.saved = nil;
            for _, row in ipairs(picker.rows or {}) do row.hovered = false; end
            dismiss:Hide();
        end);
        dismiss:Hide();
        picker:Hide();
        picker:SetClampedToScreen(true);
        picker:EnableMouse(true);
        picker:SetBackdrop({bgFile="Interface\\Glues\\Common\\Glue-Tooltip-Background",
            edgeFile="Interface\\Glues\\Common\\Glue-Tooltip-Border", tile=true, tileSize=16, edgeSize=16,
            insets={left=5, right=5, top=5, bottom=5}});
        picker.rows = {};
        for i = 1, 10 do
            local row = CreateFrame("Button", nil, picker);
            row:SetSize(260, 24);
            row:SetPoint("TOPLEFT", 10, -8 - (i - 1) * 24);
            row:SetNormalFontObject(GlueFontNormalSmall);
            row:SetHighlightFontObject(GlueFontHighlightSmall);
            row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight");
            row:SetScript("OnClick", SelectChoice);
            row:SetScript("OnEnter", function(self) self.hovered = true; PreviewChoice(self); end);
            row:SetScript("OnLeave", function(self)
                self.hovered = false;
                if picker.previewRow == self then RestorePreview(); end
            end);
            row:EnableMouseWheel(true);
            row:SetScript("OnMouseWheel", ScrollChoices);
            picker.rows[i] = row;
        end
        picker.scrollbar = CreateFrame("Slider", "CharacterCustomizationChoiceMenuScrollBar",
            picker, "GlueScrollBarTemplate");
        picker.scrollbar:SetPoint("TOPRIGHT", -8, -24);
        picker.scrollbar:SetPoint("BOTTOMRIGHT", -8, 24);
        picker.scrollbar:SetValueStep(1);
        picker.scrollbar:EnableMouseWheel(true);
        picker.scrollbar:SetScript("OnMouseWheel", ScrollChoices);
        picker.scrollbar:SetScript("OnValueChanged", function(_, offset)
            picker.offset = math.floor(offset + .5);
            RefreshChoices();
        end);
        for _, direction in ipairs({"Up", "Down"}) do
            local button = _G["CharacterCustomizationChoiceMenuScrollBarScroll"..direction.."Button"];
            button:EnableMouseWheel(true);
            button:SetScript("OnMouseWheel", ScrollChoices);
            button:SetScript("OnClick",
                function() picker.scrollbar:SetValue(picker.offset + (direction == "Up" and -1 or 1)); end);
        end
        picker:EnableMouseWheel(true);
        picker:SetScript("OnMouseWheel", ScrollChoices);
        picker:SetScript("OnUpdate", function(self, elapsed)
            self.elapsed = (self.elapsed or 0) + elapsed;
            if self.elapsed > .15 then
                self.elapsed = 0;
                if not self.owner:IsVisible() or PickerSignature() ~= self.signature then self:Hide(); end
            end
        end);
    end
    picker.owner, picker.optionID, picker.label = self, id, label;
    picker.choices, picker.selected, picker.offset = choices, value, 0;
    picker.signature = PickerSignature();
    picker.saved = CycleCharCustomization("EA_PREVIEW_SAVE", 1) == 1;
    picker:ClearAllPoints();
    picker:SetPoint("TOPLEFT", self, "BOTTOMLEFT", -8, -2);
    picker:SetSize(300, 16 + math.min(10, #choices) * 24);
    picker.scrollbar:SetMinMaxValues(0, math.max(0, #choices - 10));
    local selectedRow = 1;
    for i, choice in ipairs(choices) do if choice == value then selectedRow = i; break; end end
    picker.offset = math.min(math.max(0, selectedRow - 5), math.max(0, #choices - 10));
    picker.scrollbar:SetValue(picker.offset);
    if #choices > 10 then picker.scrollbar:Show(); else picker.scrollbar:Hide(); end
    RefreshChoices();
    picker:Show();
end

local randomize = CharacterCreate_Randomize;
function CharacterCreate_Randomize()
    CharacterCustomization_CloseChoices();
    if EA_IS_RETAIL() then
        PlaySound("gsCharacterCreationLook");
        CycleCharCustomization("EA_RANDOM", 1);
        CharacterCreate_UpdateHairCustomization();
        return;
    end
    randomize();
    if EA_ACTIVE() and CharacterCreate.selectedRaceID ~= 46 and CharacterCreate.selectedRaceID ~= 48
        and CharacterCreate.selectedRaceID ~= 49 and CharacterCreate.selectedRaceID ~= 50
        and CharacterCreate.selectedRaceID ~= 51 then
        for i = 1, EA_LIMIT() do
            local _, count = PickerState(i);
            if count and count > 1 then CycleCharCustomization("EA_SET", i, math.random(0, count - 1)); end
        end
    end
    CharacterCreate_UpdateHairCustomization();
end

local keyDown = CharacterCreate_OnKeyDown;
function CharacterCreate_OnKeyDown(key)
    if key == "ESCAPE" and picker and picker:IsShown() then picker:Hide(); return; end
    if key == "ENTER" then CharacterCustomization_CloseChoices(); end
    keyDown(key);
end

-- Only Naga has an authored second playable body type in this NPC port.
local creatureOriginalSetRace = SetCharacterRace;
function SetCharacterRace(id)
    creatureOriginalSetRace(id);
    local race = CharacterCreate.selectedRaceID;
    local maleOnly = CharacterCreate_IsMaleOnlyRace(race);
    if maleOnly then
        CharacterCreateGenderButtonMale:Hide();
        CharacterCreateGenderButtonFemale:Hide();
        if GetSelectedSex() ~= SEX_MALE then SetCharacterGender(SEX_MALE); end
    else
        CharacterCreateGenderButtonMale:Show();
        CharacterCreateGenderButtonFemale:Show();
    end
end

-- Esteria Vrykul preview sizing.
local creaturePreviewUpdate = CharacterCreate_UpdateModel;
function CharacterCreate_UpdateModel(self)
    creaturePreviewUpdate(self);
    local index = 0;
    if index == 0 and type(CycleCharCustomization) == "function" then
        CycleCharCustomization("EA_PREVIEW_SCALE", index);
    end
end

-- Gnoll has one authored playable body; refreshes must not restore the female button.
local function GnollRefreshGender()
    local _, fileString = GetNameForRace();
    local gnoll = fileString and (strupper(fileString) == "GNOLLHORDE" or strupper(fileString) == "SABERONHORDE");
    if gnoll then
        CharacterCreateGenderButtonFemale:Hide();
        CharacterCreateGenderButtonFemale:Disable();
        if GetSelectedSex() ~= SEX_MALE then SetCharacterGender(SEX_MALE); end
    else
        CharacterCreateGenderButtonFemale:Enable();
    end
end
local gnollOriginalCheckedStates = CharacterCreate_UpdateButtonCheckedStates;
function CharacterCreate_UpdateButtonCheckedStates(...)
    gnollOriginalCheckedStates(...);
    GnollRefreshGender();
end
local gnollOriginalResetState = CharacterCreate_ResetState;
function CharacterCreate_ResetState(...)
    gnollOriginalResetState(...);
    GnollRefreshGender();
end


-- Drakkin: Tarasek male and the existing female share race 66.
local tarasekSetRace = SetCharacterRace;
function SetCharacterRace(id)
    tarasekSetRace(id);
    if CharacterCreate.selectedRaceID == 66 then
        CharacterCreateGenderButtonMale:Enable();
        CharacterCreateGenderButtonFemale:Enable();
        CharacterCreateGenderButtonMale:Show();
        CharacterCreateGenderButtonFemale:Show();
    end
end

RACE_ICON_TCOORDS["SLIMEGIANTALLIANCE_MALE"] = RACE_ICON_TCOORDS["HUMAN_MALE"];

RACE_ICON_TCOORDS["SLIMEGIANTHORDE_MALE"] = RACE_ICON_TCOORDS["ORC_MALE"];

RACE_ICON_TCOORDS["CENTAURALLIANCE_MALE"]=RACE_ICON_TCOORDS["HUMAN_MALE"];

RACE_ICON_TCOORDS["CENTAURALLIANCE_FEMALE"]=RACE_ICON_TCOORDS["HUMAN_FEMALE"];

RACE_ICON_TCOORDS["CENTAURHORDE_MALE"]=RACE_ICON_TCOORDS["ORC_MALE"];

RACE_ICON_TCOORDS["CENTAURHORDE_FEMALE"]=RACE_ICON_TCOORDS["ORC_FEMALE"];

-- Body sliders page (wowdecomp)
-- A second creator page after Appearance: native sliders for every body/face slider the chosen race and sex
-- supports. Flow: Appearance -> Next -> Body -> Accept. Values live in the client (SetBodyShape / GetBodyShape)
-- and show on the creator preview model.
-- Everything here is guarded: if anything fails the creator behaves exactly as before and a small note says why.
local BS = { page = false, rows = {}, built = false, broken = nil, okayText = nil };

local function BS_Log(text)
    if BodyShapeLog then BodyShapeLog("lua: " .. tostring(text)); end
end
BS_Log("page chunk reached; natives=" .. tostring(type(GetBodyShapeNames) == "function"));

local function BS_Retail()
    return type(GetBodyShapeNames) == "function" and EA_IS_RETAIL();
end

local function BS_InPersonalization()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

-- True when Accept should lead to the Body page first.
local function BS_FlowAvailable()
    return not BS.broken and BS_InPersonalization() and BS_Retail();
end

function BodyShapePage_Active()
    return BS.page and BS_FlowAvailable();
end

local function BS_Pretty(name)
    return (name:gsub("(%l)(%u)", "%1 %2"));
end

local function BS_Status()
    -- Other executables sharing this archive have no body-slider commands: say nothing there.
    if type(GetBodyShapeNames) ~= "function" then return nil; end
    if BS.broken then return "Body sliders unavailable: " .. tostring(BS.broken); end
    if not EA_IS_RETAIL() then return "Body sliders: not available for this race (not a Retail model)"; end
    return nil;
end

local function BS_Install()
    local BACKDROP = {
        bgFile = "Interface\\Glues\\Common\\Glue-Tooltip-Background",
        edgeFile = "Interface\\Glues\\Common\\Glue-Tooltip-Border",
        tile = true, tileSize = 16, edgeSize = 16,
        insets = { left = 5, right = 5, top = 5, bottom = 5 },
    };

    local panel = CreateFrame("Frame", "BodyShapePanel", CharacterCreateFrame);
    panel:SetAllPoints(CharacterCreateFrame);
    panel:Hide();

    local note = CharacterCreateFrame:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
    note:SetPoint("BOTTOMLEFT", CharacterCreateFrame, "BOTTOMLEFT", 20, 8);
    note:SetTextColor(1, 0.6, 0.4);
    note:Hide();

    local function MakeButton(name, text, width, x)
        local button = CreateFrame("Button", name, panel);
        button:SetSize(width, 26);
        button:SetPoint("TOP", CharacterCreateFrame, "TOP", x, -64);
        button:SetNormalFontObject(GlueFontNormalSmall);
        button:SetHighlightFontObject(GlueFontHighlightSmall);
        button:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight");
        button:SetBackdrop(BACKDROP);
        button:SetText(text);
        return button;
    end

    local buttonRandom = MakeButton("BodyShapeRandomButton", "Randomize Body", 140, -80);
    local buttonReset = MakeButton("BodyShapeResetButton", "Reset Body", 110, 60);
    buttonRandom:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationLook");
        BodyShapeRandomize();
        if BS.ResetTriangle then BS.ResetTriangle(); end
    end);
    buttonReset:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationCancel");
        ResetBodyShape();
        if BS.ResetTriangle then BS.ResetTriangle(); end
    end);

    local columnTitle = {};
    for _, side in ipairs({ "Body", "Face" }) do
        local title = panel:CreateFontString(nil, "ARTWORK", "GlueFontNormal");
        title:SetText(side);
        columnTitle[side] = title;
    end

    -- Body-type triangle: Muscular (top), Large (bottom left), Thin (bottom right). The knob's
    -- barycentric weights blend three presets over a few body sliders. Its contribution is added on
    -- top of the sliders, so manual fine-tuning survives moving the knob.
    local TRI_W, TRI_H, TRI_SPACE = 180, 156, 200;
    local TRI_PRESETS = {
        { ShoulderWidth = 0.8, ArmGirth = 0.8, ChestSize = 0.7, WaistWidth = -0.2, HipWidth = -0.1, LegGirth = 0.4 },
        { ShoulderWidth = 0.2, ArmGirth = 0.4, ChestSize = 0.5, WaistWidth = 0.9, HipWidth = 0.7, LegGirth = 0.7 },
        { ShoulderWidth = -0.2, ArmGirth = -0.4, ChestSize = -0.3, WaistWidth = -0.35, HipWidth = -0.25, LegGirth = -0.35 },
    };
    local CORNERS = { { TRI_W / 2, TRI_H }, { 0, 0 }, { TRI_W, 0 } };
    BS.tri = { w = { 1 / 3, 1 / 3, 1 / 3 }, applied = {} };

    local tri = CreateFrame("Frame", "BodyShapeTriangle", panel);
    tri:SetSize(TRI_W, TRI_H);
    tri:EnableMouse(true);
    local STRIP = 3;
    for y = 0, TRI_H - STRIP, STRIP do
        local width = TRI_W * (1 - (y + STRIP / 2) / TRI_H);
        local edge = tri:CreateTexture(nil, "BACKGROUND");
        edge:SetTexture(0.75, 0.85, 0.8, 0.55);
        edge:SetSize(width + 4, STRIP);
        edge:SetPoint("BOTTOM", tri, "BOTTOMLEFT", TRI_W / 2, y);
        if width > 6 and y > 1 then
            local fill = tri:CreateTexture(nil, "BORDER");
            fill:SetTexture(0.04, 0.07, 0.09, 0.85);
            fill:SetSize(width - 4, STRIP);
            fill:SetPoint("BOTTOM", tri, "BOTTOMLEFT", TRI_W / 2, y);
        end
    end
    local function CornerLabel(text, point, x, y)
        local label = tri:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall");
        label:SetText(text);
        label:SetPoint(point, tri, "BOTTOMLEFT", x, y);
    end
    CornerLabel("Muscular", "BOTTOM", TRI_W / 2, TRI_H + 4);
    CornerLabel("Large", "TOPLEFT", -6, -4);
    CornerLabel("Thin", "TOPRIGHT", TRI_W + 6, -4);

    local knob = tri:CreateTexture(nil, "OVERLAY");
    knob:SetTexture("Interface\\Buttons\\UI-RadioButton");
    knob:SetTexCoord(0.25, 0.5, 0, 1);
    knob:SetSize(18, 18);

    local function PlaceKnob()
        local w = BS.tri.w;
        local x = w[1] * CORNERS[1][1] + w[2] * CORNERS[2][1] + w[3] * CORNERS[3][1];
        local y = w[1] * CORNERS[1][2] + w[2] * CORNERS[2][2] + w[3] * CORNERS[3][2];
        knob:ClearAllPoints();
        knob:SetPoint("CENTER", tri, "BOTTOMLEFT", x, y);
    end

    -- Weights for a local point, clamped onto the triangle.
    local function Weights(x, y)
        local ax, ay = CORNERS[1][1], CORNERS[1][2];
        local bx, by = CORNERS[2][1], CORNERS[2][2];
        local cx, cy = CORNERS[3][1], CORNERS[3][2];
        local d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
        local a = ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / d;
        local b = ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / d;
        local w = { math.max(a, 0), math.max(b, 0), math.max(1 - a - b, 0) };
        local sum = w[1] + w[2] + w[3];
        return { w[1] / sum, w[2] / sum, w[3] / sum };
    end

    local function ApplyTriangle()
        local w = BS.tri.w;
        local wanted = {};
        -- Each corner's preset fades in from the centre (0) to its corner (1), so a corner shows
        -- exactly its own preset rather than also subtracting the other two.
        for k = 1, 3 do
            local share = math.max(0, (w[k] - 1 / 3) * 1.5);
            for name, amount in pairs(TRI_PRESETS[k]) do
                wanted[name] = (wanted[name] or 0) + share * amount;
            end
        end
        for name, value in pairs(wanted) do
            if GetBodyShapeSupport(name) ~= false then
                local lo, hi = GetBodyShapeRange(name);
                local base = GetBodyShape(name) - (BS.tri.applied[name] or 0);
                local target = math.max(lo or -1, math.min(hi or 1, base + value));
                SetBodyShape(name, target);
                BS.tri.applied[name] = target - base;
            end
        end
        PlaceKnob();
    end

    -- Back to the centre without touching the sliders (used after Reset/Randomize/new character).
    function BS.ResetTriangle()
        BS.tri.w = { 1 / 3, 1 / 3, 1 / 3 };
        BS.tri.applied = {};
        PlaceKnob();
    end

    local function TrackCursor()
        local scale = tri:GetEffectiveScale();
        local cx, cy = GetCursorPosition();
        BS.tri.w = Weights(cx / scale - tri:GetLeft(), cy / scale - tri:GetBottom());
        ApplyTriangle();
    end
    tri:SetScript("OnMouseDown", function(self, button)
        if button == "RightButton" then
            BS.tri.w = { 1 / 3, 1 / 3, 1 / 3 };
            ApplyTriangle();
            return;
        end
        self.dragging = true;
        TrackCursor();
    end);
    tri:SetScript("OnMouseUp", function(self) self.dragging = false; end);
    tri:SetScript("OnHide", function(self) self.dragging = false; end);
    tri:SetScript("OnUpdate", function(self)
        if self.dragging then TrackCursor(); end
    end);
    BS.triangle = tri;
    PlaceKnob();

    local function BuildRows()
        local index = 0;
        for _, group in ipairs({ "body", "face" }) do
            for _, name in ipairs({ GetBodyShapeNames(group) }) do
                index = index + 1;
                local row = CreateFrame("Frame", nil, panel);
                row:SetSize(290, 22);
                row.name, row.group = name, group;

                local label = row:CreateFontString(nil, "ARTWORK", "GlueFontHighlightSmall");
                label:SetPoint("LEFT", 0, 0);
                label:SetWidth(100);
                label:SetJustifyH("LEFT");
                label:SetText(BS_Pretty(name));

                local value = row:CreateFontString(nil, "ARTWORK", "GlueFontNormalSmall");
                value:SetPoint("RIGHT", 0, 0);
                value:SetWidth(52);
                value:SetJustifyH("RIGHT");

                local slider = CreateFrame("Slider", "BodyShapeSlider" .. index, row);
                slider:SetOrientation("HORIZONTAL");
                slider:SetSize(135, 14);
                slider:SetPoint("LEFT", 104, 0);
                slider:SetBackdrop({
                    bgFile = "Interface\\Buttons\\UI-SliderBar-Background",
                    edgeFile = "Interface\\Buttons\\UI-SliderBar-Border",
                    tile = true, tileSize = 8, edgeSize = 8,
                    insets = { left = 3, right = 3, top = 6, bottom = 6 },
                });
                slider:SetThumbTexture("Interface\\Buttons\\UI-SliderBar-Button-Horizontal");
                local lo, hi = GetBodyShapeRange(name);
                slider:SetMinMaxValues(lo or -1, hi or 1);
                slider:SetValueStep(0.05);
                -- The client only places the knob once a value has been set; Sync skips unchanged values.
                slider:SetValue(GetBodyShape(name) or 0);
                slider:EnableMouse(true);
                slider:SetScript("OnValueChanged", function(self, v)
                    v = math.floor(v * 20 + 0.5) / 20;
                    value:SetText(string.format("%d%%", math.floor(v * 100 + 0.5)));
                    if not self.syncing then SetBodyShape(name, v); end
                end);
                -- Right click puts a slider back to its default.
                slider:SetScript("OnMouseUp", function(self, button)
                    if button == "RightButton" then self:SetValue(0); end
                end);

                row.slider, row.value = slider, value;
                BS.rows[#BS.rows + 1] = row;
            end
        end
    end

    local function Sync(row)
        local v = GetBodyShape(row.name);
        if math.abs(row.slider:GetValue() - v) > 0.001 then
            row.slider.syncing = true;
            row.slider:SetValue(v);
            row.slider.syncing = false;
        end
        row.value:SetText(string.format("%d%%", math.floor(v * 100 + 0.5)));
    end

    local function Layout()
        local counts = { body = 0, face = 0 };
        local shown = {};
        for _, row in ipairs(BS.rows) do
            if GetBodyShapeSupport(row.name) == false then
                row:Hide();
            else
                counts[row.group] = counts[row.group] + 1;
                shown[#shown + 1] = row;
            end
        end
        local triShown = false;
        for name in pairs({ ShoulderWidth = 1, ArmGirth = 1, ChestSize = 1, WaistWidth = 1, HipWidth = 1, LegGirth = 1 }) do
            if GetBodyShapeSupport(name) ~= false then triShown = true; end
        end
        BS.triangle:ClearAllPoints();
        BS.triangle:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPLEFT", 105, -160);
        if triShown then BS.triangle:Show(); else BS.triangle:Hide(); end
        local bodyTop = triShown and 200 or 0;
        local tallest = math.max(counts.body, counts.face, 1);
        local spacing = math.min(34, math.max(22, math.floor((GetScreenHeight() - 240) / tallest)));
        local bodySpacing = math.min(spacing, math.max(20, math.floor((GetScreenHeight() - 240 - bodyTop) / math.max(counts.body, 1))));
        local placed = { body = 0, face = 0 };
        for _, row in ipairs(shown) do
            local right = row.group == "face";
            local n = placed[row.group];
            placed[row.group] = n + 1;
            row:ClearAllPoints();
            row:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -140 - (right and 0 or bodyTop) - n * (right and spacing or bodySpacing));
            Sync(row);
            row:Show();
        end
        for side, group in pairs({ Body = "body", Face = "face" }) do
            local title = columnTitle[side];
            local right = group == "face";
            title:ClearAllPoints();
            title:SetPoint(right and "TOPRIGHT" or "TOPLEFT", CharacterCreateFrame, right and "TOPRIGHT" or "TOPLEFT",
                right and -55 or 55, -116);
            if counts[group] > 0 then title:Show(); else title:Hide(); end
        end
    end

    local function SetPage(body)
        if BS.page == body then return; end
        BS.page = body;
        PlaySound("gsCharacterCreationLook");
        if CharacterCustomization_CloseChoices then CharacterCustomization_CloseChoices(); end
    end

    -- Accept becomes Next on the Appearance page; Back returns from the Body page before leaving customization.
    local okay = CharacterCreate_Okay;
    function CharacterCreate_Okay(...)
        if BS_FlowAvailable() and not BS.page then
            SetPage(true);
            return;
        end
        return okay(...);
    end
    local back = CharacterCreate_Back;
    function CharacterCreate_Back(...)
        if BS.page then
            SetPage(false);
            return;
        end
        return back(...);
    end
    local toggle = CharacterCreate_TogglePersonalization;
    function CharacterCreate_TogglePersonalization(...)
        if BS.page then
            SetPage(false);
            return;
        end
        return toggle(...);
    end
    -- A new character starts with default sliders.
    local onShow = CharacterCreate_OnShow;
    function CharacterCreate_OnShow(...)
        if ResetBodyShape then ResetBodyShape(); end
        if BS.ResetTriangle then BS.ResetTriangle(); end
        BS.page = false;
        local a, b = onShow(...);
        -- Customizing an existing character: start from its saved sliders (the stock handler just selected it).
        if PAID_SERVICE_TYPE and BodyShapeLoadExisting then BodyShapeLoadExisting(); end
        return a, b;
    end

    local function Tick(self)

        if CharCreateOkayButton and not BS.okayText then BS.okayText = CharCreateOkayButton:GetText(); end

        local status = BS_InPersonalization() and BS_Status() or nil;
        if status then note:SetText(status); note:Show(); else note:Hide(); end

        if not BS_FlowAvailable() then
            BS.page = false;
            panel:Hide();
            if BS.okayText and CharCreateOkayButton:GetText() ~= BS.okayText then CharCreateOkayButton:SetText(BS.okayText); end
            return;
        end

        if not BS.built then
            local ok, err = pcall(BuildRows);
            if not ok then
                BS.broken = tostring(err);
                BS.page = false;
                return;
            end
            BS.built = true;
        end

        if BS.page then
            local ok, err = pcall(Layout);
            if not ok then BS.broken = tostring(err); BS.page = false; return; end
            panel:Show();
            if BS.okayText and CharCreateOkayButton:GetText() ~= BS.okayText then CharCreateOkayButton:SetText(BS.okayText); end
        else
            panel:Hide();
            if CharCreateOkayButton:GetText() ~= "Next" then CharCreateOkayButton:SetText("Next"); end
        end
    end

    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        self.logAt = (self.logAt or 0) + 1;
        if self.logAt >= 30 then
            self.logAt = 0;
            if BS_InPersonalization() then
                BS_Log("state natives=" .. tostring(type(GetBodyShapeNames) == "function")
                    .. " retail=" .. tostring(EA_IS_RETAIL()) .. " flow=" .. tostring(BS_FlowAvailable())
                    .. " page=" .. tostring(BS.page) .. " built=" .. tostring(BS.built)
                    .. " broken=" .. tostring(BS.broken) .. " okayText=" .. tostring(CharCreateOkayButton:GetText()));
            end
        end
        local ok, err = pcall(Tick, self);
        if not ok then
            BS.broken = tostring(err);
            BS.page = false;
            BS_Log("tick error: " .. BS.broken);
        end
    end);
    BS_Log("page installed");
end

-- The creator's frames do not exist yet while this file loads, so build the page on the first frame that has them.
local installWaiter = CreateFrame("Frame");
installWaiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton) then return; end
    self:SetScript("OnUpdate", nil);
    local installed, installError = pcall(BS_Install);
    if not installed then
        BS.broken = tostring(installError);
        BS_Log("install failed: " .. BS.broken);
        pcall(function()
            local note = CharacterCreateFrame:CreateFontString(nil, "OVERLAY", "GlueFontHighlightSmall");
            note:SetPoint("BOTTOMLEFT", CharacterCreateFrame, "BOTTOMLEFT", 20, 8);
            note:SetTextColor(1, 0.4, 0.4);
            note:SetText("Body page failed to load: " .. BS.broken);
        end);
    end
end);

-- Creator skin (wowdecomp)
-- Restyles the two customization pages (Appearance and Body) without changing how they work: dark edge panels with
-- a gold accent, slim option rows, a step indicator, underlined name fields and flat buttons. It only restyles the
-- existing frames while customization is open and restores the shared ones (Back) when it closes. Any error turns
-- the skin off for the session and leaves the stock look.
-- Its own function: the creator chunk is near Lua's 200-locals-per-function limit.
(function()
local SK ={ on = false, broken = nil, rows = {}, sliders = {}, buttons = {} };

local GOLD = { 0.91, 0.77, 0.42 };
local GOLD_DIM = { 0.54, 0.43, 0.18 };
local INK = { 0.03, 0.04, 0.055 };
local TEXT = { 0.88, 0.86, 0.80 };
local MUTED = { 0.60, 0.59, 0.55 };
-- Everything uses the game font; body text runs a size below headers.
local HEAD_FONT, BODY_FONT = "Fonts\\FRIZQT__.TTF", "Fonts\\FRIZQT__.TTF";
local WHITE = "Interface\\Buttons\\WHITE8X8";
local PANEL_W, EDGE_X = 440, 55; -- rows sit EDGE_X in from each screen edge and are 340 wide

local function SK_Log(text)
    if BodyShapeLog then BodyShapeLog("skin: " .. tostring(text)); end
end

local function Solid(parent, layer, r, g, b, a)
    local tex = parent:CreateTexture(nil, layer or "ARTWORK");
    tex:SetTexture(WHITE);
    tex:SetVertexColor(r, g, b, a or 1);
    return tex;
end

local function Text(parent, font, size, color, layer)
    local fs = parent:CreateFontString(nil, layer or "OVERLAY", "GlueFontHighlightSmall");
    fs:SetFont(font, size);
    fs:SetTextColor(color[1], color[2], color[3]);
    fs:SetShadowOffset(1, -1);
    fs:SetShadowColor(0, 0, 0, 0.8);
    return fs;
end

local function Personalizing()
    return CharacterCreate.personalizationMode and CharCreateOkayButton:IsShown();
end

local function BodyPage()
    return BodyShapePage_Active and BodyShapePage_Active() and BodyShapePanel and BodyShapePanel:IsShown();
end

-- ---- static chrome -------------------------------------------------------------------------------------------

local function BuildChrome()
    local chrome = CreateFrame("Frame", "CreatorSkinChrome", CharacterCreateFrame);
    chrome:SetAllPoints(CharacterCreateFrame);
    chrome:SetFrameLevel(CharacterCreateFrame:GetFrameLevel());
    chrome:Hide();
    SK.chrome = chrome;

    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local left = side == "LEFT";
        local panel = Solid(chrome, "BACKGROUND", INK[1], INK[2], INK[3], 0.84);
        panel:SetPoint("TOP" .. side, chrome, "TOP" .. side, 0, 0);
        panel:SetPoint("BOTTOM" .. side, chrome, "BOTTOM" .. side, 0, 0);
        panel:SetWidth(PANEL_W);
        -- A soft fade past the panel so it melts into the scene instead of ending in a hard block.
        local fade = chrome:CreateTexture(nil, "BACKGROUND");
        fade:SetTexture(WHITE);
        fade:SetWidth(70);
        fade:SetPoint("TOP" .. (left and "LEFT" or "RIGHT"), panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        fade:SetPoint("BOTTOM" .. (left and "LEFT" or "RIGHT"), panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);
        if left then
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0.5, INK[1], INK[2], INK[3], 0);
        else
            fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0, INK[1], INK[2], INK[3], 0.5);
        end
        local edge = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        edge:SetWidth(1);
        edge:SetPoint("TOP", panel, "TOP" .. (left and "RIGHT" or "LEFT"), 0, 0);
        edge:SetPoint("BOTTOM", panel, "BOTTOM" .. (left and "RIGHT" or "LEFT"), 0, 0);

        local title = Text(chrome, HEAD_FONT, 16, GOLD);
        local subtitle = Text(chrome, BODY_FONT, 12, MUTED);
        subtitle:SetPoint("TOP" .. side, title, "BOTTOM" .. side, 0, -4);
        local rule = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.6);
        rule:SetSize(340, 1);
        SK[side] = { title = title, subtitle = subtitle, rule = rule, panel = panel, fade = fade };
    end

    -- Step indicator: Race, Appearance, Body (the last only for races with the Body page).
    local steps, lines = {}, {};
    for index, name in ipairs({ "RACE", "APPEARANCE", "BODY" }) do
        steps[index] = Text(chrome, HEAD_FONT, 13, MUTED);
        steps[index]:SetText(index .. "   " .. name);
    end
    for index = 1, 2 do
        lines[index] = Solid(chrome, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
        lines[index]:SetSize(40, 1);
    end
    steps[2]:SetPoint("TOP", chrome, "TOP", 0, -24);
    lines[1]:SetPoint("RIGHT", steps[2], "LEFT", -12, 0);
    steps[1]:SetPoint("RIGHT", lines[1], "LEFT", -12, 0);
    lines[2]:SetPoint("LEFT", steps[2], "RIGHT", 12, 0);
    steps[3]:SetPoint("LEFT", lines[2], "RIGHT", 12, 0);
    SK.steps, SK.stepLines = steps, lines;

    local hint = Text(chrome, BODY_FONT, 12, MUTED);
    hint:SetPoint("BOTTOM", chrome, "BOTTOM", 0, 14);
    hint:SetText("Left-drag rotate   \194\183   right-drag pan   \194\183   wheel zoom");
    SK.hint = hint;
end

-- Header text sits flush with the rows. On the race page `slide` (0 to -PANEL_W) moves both side panels out, the
-- left one to the left and the right one to the right, with their headers and rules; the fades, edge lines and the
-- race card follow their panel by their anchors.
local function PlaceHeaders(slide)
    slide = slide or 0;
    -- Fade the shadows out with the slide. SetAlpha would drop the gradient and leave a white strip; re-set it instead.
    local shadow = 0.5 * (1 + slide / PANEL_W);
    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local panel = SK[side].panel;
        local x = side == "LEFT" and slide or -slide;
        panel:ClearAllPoints();
        panel:SetPoint("TOP" .. side, SK.chrome, "TOP" .. side, x, 0);
        panel:SetPoint("BOTTOM" .. side, SK.chrome, "BOTTOM" .. side, x, 0);
        if side == "LEFT" then
            SK.LEFT.fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], shadow, INK[1], INK[2], INK[3], 0);
        else
            SK.RIGHT.fade:SetGradientAlpha("HORIZONTAL", INK[1], INK[2], INK[3], 0, INK[1], INK[2], INK[3], shadow);
        end
    end
    for _, side in ipairs({ "LEFT", "RIGHT" }) do
        local inset = EDGE_X + 12;
        local header = SK[side];
        local x = side == "LEFT" and slide or -slide;
        header.rule:ClearAllPoints();
        header.rule:SetPoint("TOP" .. side, SK.chrome, "TOP" .. side, side == "LEFT" and EDGE_X + x or -EDGE_X + x, -100);
        header.title:ClearAllPoints();
        header.title:SetPoint("TOP" .. side, SK.chrome, "TOP" .. side, side == "LEFT" and inset + x or -inset + x, -62);
        header.subtitle:ClearAllPoints();
        header.subtitle:SetPoint("TOP" .. side, header.title, "BOTTOM" .. side, 0, -4);
    end
end

-- ---- appearance rows -----------------------------------------------------------------------------------------

local function SkinArrow(button, direction)
    local path = "Interface\\MoneyFrame\\Arrow-" .. direction;
    button:SetNormalTexture(path .. "-Up");
    button:SetPushedTexture(path .. "-Down");
    button:GetNormalTexture():SetTexCoord(0, 1, 0, 1);
    button:GetPushedTexture():SetTexCoord(0, 1, 0, 1);
    button:GetNormalTexture():SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
    button:SetSize(14, 14);
    local highlight = button:GetHighlightTexture();
    if highlight then highlight:SetAlpha(0); end
end

local function SkinRow(row)
    local name = row:GetName();
    local background = _G[name .. "Background"];
    if background then background:SetAlpha(0); end
    _G[name .. "Text"]:SetAlpha(0);

    local hairline = Solid(row, "BACKGROUND", 1, 1, 1, 0.07);
    hairline:SetHeight(1);
    hairline:SetPoint("TOPLEFT", row, "TOPLEFT", 0, 0);
    hairline:SetPoint("TOPRIGHT", row, "TOPRIGHT", 0, 0);
    local accent = Solid(row, "ARTWORK", GOLD[1], GOLD[2], GOLD[3], 1);
    accent:SetWidth(2);
    accent:SetPoint("TOPLEFT", row, "TOPLEFT", 0, -1);
    accent:SetPoint("BOTTOMLEFT", row, "BOTTOMLEFT", 0, 1);
    accent:Hide();

    local right, left = _G[name .. "RightButton"], _G[name .. "LeftButton"];
    SkinArrow(right, "Right");
    SkinArrow(left, "Left");
    right:ClearAllPoints();
    right:SetPoint("RIGHT", row, "RIGHT", -10, 0);
    local value = Text(row, BODY_FONT, 13, GOLD);
    value:SetWidth(128);
    value:SetJustifyH("CENTER");
    value:SetPoint("RIGHT", right, "LEFT", -4, 0);
    left:ClearAllPoints();
    left:SetPoint("RIGHT", value, "LEFT", -4, 0);
    local label = Text(row, BODY_FONT, 13, TEXT);
    label:SetJustifyH("LEFT");
    label:SetPoint("LEFT", row, "LEFT", 14, 0);
    label:SetPoint("RIGHT", left, "LEFT", -6, 0);

    -- The whole row opens the choice list; the arrows stay on top of it.
    local choice = _G[name .. "ChoiceButton"];
    choice:ClearAllPoints();
    choice:SetAllPoints(row);
    local highlight = choice:GetHighlightTexture();
    if highlight then
        highlight:SetTexture(WHITE);
        highlight:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 0.10);
        highlight:SetBlendMode("BLEND");
        highlight:SetAllPoints(row);
    end
    choice:HookScript("OnEnter", function() accent:Show(); end);
    choice:HookScript("OnLeave", function() accent:Hide(); end);
    right:SetFrameLevel(choice:GetFrameLevel() + 2);
    left:SetFrameLevel(choice:GetFrameLevel() + 2);

    SK.rows[row] = { label = label, value = value };
end

-- The stock refresh writes "Label  3/36" into the hidden row text; split it, and prefer the choice's own name.
local function UpdateRow(row, skin)
    local text = _G[row:GetName() .. "Text"]:GetText() or "";
    local label, rest = string.match(text, "^(.-)%s%s+(.+)$");
    if not label then label, rest = text, ""; end
    local value, count = string.match(rest, "^(%d+)/(%d+)$");
    local shown = rest;
    if value then
        shown = value .. " / " .. count;
        if EA_IS_RETAIL() then
            local ok, choiceName = pcall(CycleCharCustomization, "EA_CHOICE_NAME", row:GetID(), tonumber(value) - 1);
            if ok and type(choiceName) == "string" and choiceName ~= "" and not string.match(choiceName, "^%d+$") then
                shown = choiceName;
            end
        end
    end
    -- Races on the stock option layout write only the label into the row; read the value from the native selector so
    -- they show the same "n / total" (or the choice's name) as the other races.
    if shown == "" then
        local ok, current, total = pcall(CycleCharCustomization, EA_ACTIVE() and "EA_GET" or "EA_STOCK_GET", row:GetID());
        if ok and type(current) == "number" and type(total) == "number" and total > 0 then
            shown = (current + 1) .. " / " .. total;
            local named, choiceName = pcall(ChoiceText, row:GetID(), current, label);
            if named and type(choiceName) == "string" and choiceName ~= "" and choiceName ~= label .. " " .. (current + 1) then
                shown = choiceName;
            end
        end
    end
    skin.label:SetText(label);
    skin.value:SetText(shown);
end

-- ---- buttons and name fields ---------------------------------------------------------------------------------

-- Back / Next / Accept / Customize already use the stock GlueButtonTemplate that character select uses; the
-- skin only hides their arrow icon so they read exactly like Enter World.
local function SkinButton(button)
    SK.buttons[button] = { icon = _G[button:GetName() .. "Icon"] };
end

local function ApplyButton(button, on)
    local skin = SK.buttons[button];
    if skin and skin.icon then skin.icon:SetAlpha(on and 0 or 1); end
end

-- The same glue button art (GlueButtonTemplate) on buttons that were not built from it.
local GLUE = "Interface\\Glues\\Common\\Glue-Panel-Button-";
local function GlueLook(button)
    for _, state in ipairs({ { "SetNormalTexture", "GetNormalTexture", "Up", 0.578125, 0.75 },
                             { "SetPushedTexture", "GetPushedTexture", "Down", 0.578125, 0.75 },
                             { "SetHighlightTexture", "GetHighlightTexture", "Highlight", 0.625, 0.6875 },
                             { "SetCheckedTexture", "GetCheckedTexture", "Highlight", 0.625, 0.6875 } }) do
        if button[state[1]] then
            button[state[1]](button, GLUE .. state[3]);
            local texture = button[state[2]](button);
            if texture then
                texture:SetTexCoord(0, state[4], 0, state[5]);
                texture:ClearAllPoints();
                texture:SetAllPoints(button);
                texture:SetAlpha(1);
                if state[3] == "Highlight" then texture:SetBlendMode("ADD"); end
            end
        end
    end
end

local function SkinNameEdit(edit)
    for _, region in ipairs({ edit:GetRegions() }) do
        local kind = region:GetObjectType();
        if kind == "Texture" then
            region:SetAlpha(0);
        elseif kind == "FontString" and region:GetText() then -- the "First Name" / "Last Name" caption
            -- Explicit flags and shadow: the template's outline and big drop shadow read as a doubled caption.
            region:SetFont(BODY_FONT, 12, "");
            region:SetShadowOffset(1, -1);
            region:SetShadowColor(0, 0, 0, 0.8);
            region:SetTextColor(MUTED[1], MUTED[2], MUTED[3]);
            region:ClearAllPoints();
            region:SetPoint("BOTTOMLEFT", edit, "TOPLEFT", 15, -8);
            region:SetJustifyH("LEFT");
            region:SetHeight(16);
        end
    end
    edit:SetFont(HEAD_FONT, 15);
    edit:SetTextColor(TEXT[1], TEXT[2], TEXT[3]);
    local underline = Solid(edit, "BORDER", GOLD[1], GOLD[2], GOLD[3], 0.85);
    underline:SetHeight(1);
    underline:SetPoint("BOTTOMLEFT", edit, "BOTTOMLEFT", 12, 6);
    underline:SetPoint("BOTTOMRIGHT", edit, "BOTTOMRIGHT", -10, 6);
    -- Soft plate behind the field and its caption, so the name reads against any scene.
    local plate = Solid(edit, "BACKGROUND", INK[1], INK[2], INK[3], 0.72);
    plate:SetPoint("TOPLEFT", edit, "TOPLEFT", 4, 22);
    plate:SetPoint("BOTTOMRIGHT", edit, "BOTTOMRIGHT", -2, -2);
    for _, side in ipairs({ { "TOPLEFT", "TOPRIGHT" }, { "BOTTOMLEFT", "BOTTOMRIGHT" }, { "TOPLEFT", "BOTTOMLEFT" },
                            { "TOPRIGHT", "BOTTOMRIGHT" } }) do
        local line = Solid(edit, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.55);
        line:SetPoint(side[1], plate, side[1], 0, 0);
        line:SetPoint(side[2], plate, side[2], 0, 0);
        if side[1] == "TOPLEFT" and side[2] == "BOTTOMLEFT" or side[1] == "TOPRIGHT" then line:SetWidth(1); else line:SetHeight(1); end
    end
end

-- ---- body page -----------------------------------------------------------------------------------------------

local function SkinSlider(slider)
    slider:SetBackdrop({ bgFile = WHITE, tile = false, insets = { left = 0, right = 0, top = 6, bottom = 6 } });
    slider:SetBackdropColor(1, 1, 1, 0.16);
    slider:SetThumbTexture(WHITE);
    local thumb = slider:GetThumbTexture();
    thumb:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 1);
    thumb:SetSize(3, 12);
    local fill = Solid(slider, "ARTWORK", GOLD[1], GOLD[2], GOLD[3], 0.9);
    fill:SetHeight(2);
    local function Paint()
        local lo, hi = slider:GetMinMaxValues();
        local v = slider:GetValue();
        local width = slider:GetWidth();
        local zero = (0 - lo) / math.max(hi - lo, 0.0001) * width;
        local at = (v - lo) / math.max(hi - lo, 0.0001) * width;
        fill:ClearAllPoints();
        if math.abs(at - zero) < 1 then fill:Hide(); return; end
        fill:SetPoint("LEFT", slider, "LEFT", math.min(zero, at), 0);
        fill:SetWidth(math.abs(at - zero));
        fill:Show();
    end
    slider:HookScript("OnValueChanged", Paint);
    Paint();
    local row = slider:GetParent();
    for _, region in ipairs({ row:GetRegions() }) do
        if region:GetObjectType() == "FontString" then
            local isValue = (region:GetText() or ""):find("%%$");
            region:SetFont(BODY_FONT, 13);
            if isValue then region:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); else region:SetTextColor(TEXT[1], TEXT[2], TEXT[3]); end
        end
    end
    SK.sliders[slider] = Paint;
end

local function SkinBodyPage()
    if SK.bodySkinned or not BodyShapePanel then return; end
    -- The page's own column titles are replaced by the skin's panel headers.
    for _, region in ipairs({ BodyShapePanel:GetRegions() }) do
        if region:GetObjectType() == "FontString" and (region:GetText() == "Body" or region:GetText() == "Face") then
            region:SetAlpha(0);
        end
    end
    if BodyShapeTriangle then
        -- The painted triangle art replaces the strip-drawn triangle and its corner labels; the knob stays.
        for _, region in ipairs({ BodyShapeTriangle:GetRegions() }) do
            local kind = region:GetObjectType();
            if kind == "Texture" then
                if region:GetDrawLayer() == "OVERLAY" then region:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
                else region:SetAlpha(0); end
            elseif kind == "FontString" then
                region:SetAlpha(0);
            end
        end
        -- Placement from body_triangle.py placement(): the art's stroke corners land on the knob's
        -- 180 x 156 triangle (CORNERS in body_page.lua), so dragging still lines up with the drawing.
        local art = BodyShapeTriangle:CreateTexture(nil, "ARTWORK");
        art:SetTexture("Interface\\Glues\\CharacterCreate\\EsteriaBodyTriangle");
        art:SetTexCoord(0, 1, 0, 0.888672);
        art:SetSize(217.92, 201.08);
        art:SetPoint("TOPLEFT", BodyShapeTriangle, "BOTTOMLEFT", -17.36, 177.17);
    end
    for _, button in ipairs({ BodyShapeResetButton, BodyShapeRandomButton }) do
        if button then
            button:SetBackdrop(nil);
            GlueLook(button);
            button:SetSize(150, 36);
            button:ClearAllPoints();
        end
    end
    -- Level with Back and Accept, each just inside its neighbour.
    if BodyShapeResetButton then BodyShapeResetButton:SetPoint("LEFT", CharCreateBackButton, "RIGHT", 16, 0); end
    if BodyShapeRandomButton then BodyShapeRandomButton:SetPoint("RIGHT", CharCreateOkayButton, "LEFT", -16, 0); end
    SK.bodySkinned = true;
end

-- ---- per-tick state ------------------------------------------------------------------------------------------

local function RaceLine()
    local race = (GetNameForRace and GetNameForRace()) or "";
    local sex = GetSelectedSex and GetSelectedSex() == SEX_MALE and "male" or "female";
    if race == "" then return ""; end
    return race .. ", " .. sex;
end

-- ---- race page -----------------------------------------------------------------------------------------------

-- Portrait centres start here: the tallest portrait (60) clears the header rule at -100.
local GRID_TOP = -150;
SK.races, SK.toggles = {}, {};

-- The three playable factions, in tab order. Membership comes from CharacterCreate.raceIndices<key>: the stock
-- enumeration fills Alliance/Horde from GetFactionForRaceID, and CharacterCreateEnumerateRaces below moves the
-- Freeborn races out of them.
local FACTIONS = {
    { key = "Alliance", name = ALLIANCE or "Alliance", crest = "Interface\\Glues\\CharacterCreate\\AllianceLogo",
      crop = { 0.2, 0.8, 0.2, 0.8 }, color = { 0.36, 0.60, 1.00 }, dark = { 0.03, 0.09, 0.26 }, hex = "|cff5c99ff" },
    { key = "Horde", name = HORDE or "Horde", crest = "Interface\\Glues\\CharacterCreate\\HordeLogo",
      crop = { 0.21, 0.79, 0.2, 0.8 }, color = { 0.92, 0.27, 0.22 }, dark = { 0.24, 0.02, 0.03 }, hex = "|cffeb4538" },
    { key = "Freeborn", name = "Freeborn", crest = "Interface\\Glues\\CharacterSelect\\FreebornLogo",
      crop = { 0, 1, 0, 1 }, color = { 0.74, 0.56, 0.98 }, dark = { 0.14, 0.05, 0.26 }, hex = "|cffbd8ffa" },
};

local function FactionRaces(key)
    return CharacterCreate["raceIndices" .. key] or {};
end

local function FactionOfRace(index)
    for _, faction in ipairs(FACTIONS) do
        for _, raceIndex in ipairs(FactionRaces(faction.key)) do
            if raceIndex == index then return faction; end
        end
    end
end

-- Freeborn is a faction of its own: these races leave the Alliance/Horde grids for a Freeborn grid. The server
-- puts every character of these races on the Freeborn team (both twins of a race; only the Alliance twin is offered).
local FREEBORN_FILES = { VRYKUL = true, THINHUMAN = true, OGREALLIANCE = true, SETHRAKALLIANCE = true,
    DRAKONIDALLIANCE = true, SLIMEGIANTALLIANCE = true, CENTAURALLIANCE = true, ETHEREALALLIANCE = true,
    NAGAHORDE = true, EREDAR = true, GNOLLHORDE = true, SABERONHORDE = true, ARAKKOAHORDE = true };
local FREEBORN_TWINS = { THINHUMANHORDE = true, OGREHORDE = true, DRAKONIDHORDE = true, SLIMEGIANTHORDE = true,
    CENTAURHORDE = true, ETHEREALHORDE = true };
-- Character select only has the localized race name.
local FREEBORN_NAMES = { ["Sethrak"] = true, ["Vrykul"] = true, ["Forgotten"] = true, ["Ogre"] = true,
    ["Slime Giant"] = true, ["Centaur"] = true, ["Drakonid"] = true, ["Ethereal"] = true, ["Naga"] = true,
    ["Eredar"] = true, ["Gnoll"] = true, ["Saberon"] = true, ["Arakkoa"] = true };

local function RaceFile(index)
    local button = _G["CharacterCreateRaceButton" .. (index or 0)];
    return strupper(button and button.raceFileString or "");
end

local function WithoutFreeborn(indices)
    local kept = {};
    for _, index in ipairs(indices or {}) do
        local file = RaceFile(index);
        if not (FREEBORN_FILES[file] or FREEBORN_TWINS[file]) then kept[#kept + 1] = index; end
    end
    return kept;
end

-- The stock race tooltips are anchored to fixed screen spots by faction and grow when right-clicked, so they can run
-- off screen. After the stock hover/right-click handler, put the shown one beside the left pane, level with the
-- portrait, and clamp it to the screen.
local function PlaceRaceTooltip(button)
    local frameLeft = CharacterCreateFrame:GetLeft() or 0;
    for _, child in ipairs({ CharacterCreateFrame:GetChildren() }) do
        if child.text and child:IsShown() and child:GetFrameStrata() == "TOOLTIP" then
            child:SetClampedToScreen(true);
            child:ClearAllPoints();
            child:SetPoint("TOPLEFT", button, "TOPLEFT", PANEL_W + 16 - ((button:GetLeft() or frameLeft) - frameLeft), 0);
        end
    end
end

-- Wraps a stock race button handler once; the stock enumeration sets fresh handlers each time, which drops the wrap.
local function AfterRaceHandler(button, event)
    local handler = button:GetScript(event);
    if not handler or handler == button["skin" .. event] then return; end
    button["skin" .. event] = function(self, ...)
        handler(self, ...);
        pcall(PlaceRaceTooltip, self);
    end;
    button:SetScript(event, button["skin" .. event]);
end

local enumerateRaces = CharacterCreateEnumerateRaces;
function CharacterCreateEnumerateRaces(...)
    enumerateRaces(...);
    for index = 1, CharacterCreate.numRaces or 0 do
        local button = _G["CharacterCreateRaceButton" .. index];
        if button then
            AfterRaceHandler(button, "OnEnter");
            AfterRaceHandler(button, "OnMouseDown");
        end
    end
    for _, index in ipairs(CharacterCreate.raceIndicesHorde or {}) do
        for _, texture in ipairs({ _G["CharacterCreateRaceButton" .. index .. "NormalTexture"],
                                   _G["CharacterCreateRaceButton" .. index .. "PushedTexture"] }) do
            if texture and texture.GetTexCoord then
                local ulx, uly, llx, lly, urx, ury, lrx, lry = texture:GetTexCoord();
                texture:SetTexCoord(urx, ury, lrx, lry, ulx, uly, llx, lly);
            end
        end
    end
    local freeborn = {};
    for index = 1, CharacterCreate.numRaces or 0 do
        local button = _G["CharacterCreateRaceButton" .. index];
        local file = RaceFile(index);
        if FREEBORN_FILES[file] then
            freeborn[#freeborn + 1] = index;
        elseif FREEBORN_TWINS[file] then
            button.enable = false;
            button:SetChecked(nil);
            button:Disable();
            button:Hide();
        end
    end
    CharacterCreate.raceIndicesAlliance = WithoutFreeborn(CharacterCreate.raceIndicesAlliance);
    CharacterCreate.raceIndicesHorde = WithoutFreeborn(CharacterCreate.raceIndicesHorde);
    CharacterCreate.raceIndicesFreeborn = freeborn;
    if FREEBORN_TWINS[RaceFile(GetSelectedRace())] and freeborn[1] then SetSelectedRace(freeborn[1]); end
    -- The team comes from the race now, so the old Freeborn pick is gone for good.
    local toggle = CharacterCreateFreebornButton;
    if toggle then toggle:Hide(); toggle.Show = toggle.Hide; end
    CharacterCreate_PositionRaceButtons();
end
CharacterFreeborn_IsSelected = function() return false; end

-- The character-select emblem (and ECS) read Freeborn membership from these hashes; derive them from race.
function CharacterFreeborn_BadgeHashes()
    local hashes = {};
    for index = 1, (GetNumCharacters and GetNumCharacters()) or 0 do
        local name, race = GetCharacterInfo(index);
        if name and FREEBORN_NAMES[race] then
            hashes[tostring(CharacterFreeborn_RecordFor(name) - CharacterFreeborn_RecordBase)] = true;
        end
    end
    return hashes;
end

local function RacePage()
    return CharacterCreateRaceButtonsContainer and CharacterCreateRaceButtonsContainer:IsShown()
        and CharacterCreateFrame:IsShown() and not Personalizing();
end

-- Portraits keep their own round rim; the selection ring is the stock round border, tinted gold.
local function SkinRaceButton(button)
    if button.checkedTexture then button.checkedTexture:SetAlpha(0); end
    if button.highlightTexture then button.highlightTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
    if button.staticTexture then button.staticTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
    SK.races[button] = true;
end

-- False once the left pane has slid all the way out.
local function PaneShown()
    return (SK.slide or 0) > -PANEL_W;
end

-- The browsed faction's portraits as one grid in the left panel; the other factions' portraits are hidden. Five
-- large portraits a row, seven smaller ones once a faction has more than 30 races, so the grid stays on screen.
local function LayoutRaces()
    if not (SK.on and SK.mode == "race") then return; end
    SK.factionOf = {};
    for _, faction in ipairs(FACTIONS) do
        local indices = FactionRaces(faction.key);
        local columns = #indices > 30 and 7 or 5;
        local spacing = 340 / columns;
        local size = spacing - 8;
        local rows = 0;
        for slot, index in ipairs(indices) do
            local button = _G["CharacterCreateRaceButton" .. index];
            if button then
                if not SK.races[button] then SkinRaceButton(button); end
                SK.factionOf[button] = faction.key;
                local column, row = (slot - 1) % columns, math.floor((slot - 1) / columns);
                rows = math.max(rows, row + 1);
                button:ClearAllPoints();
                button:SetSize(size, size);
                button:SetPoint("CENTER", CharacterCreateFrame, "TOPLEFT",
                    (SK.slide or 0) + EDGE_X + spacing / 2 + column * spacing, GRID_TOP - row * spacing);
                button:GetNormalTexture():SetSize(size, size);
                button:GetPushedTexture():SetSize(size, size);
                for _, name in ipairs({ "staticTexture", "highlightTexture", "checkedTexture" }) do
                    if button[name] then button[name]:SetSize(size * 112 / 50, size * 112 / 50); end
                end
                if faction.key == SK.faction and PaneShown() then button:Show(); else button:Hide(); end
            end
        end
    end
    -- The faction rail carries the crests; the stock corner crests and captions stay out of the way.
    for _, region in ipairs({ CustomizationLogoAlliance, CustomizationLogoHorde, CustomizationTextAlliance,
                              CustomizationTextHorde }) do
        if region then region:SetAlpha(0); end
    end
    for _, hitbox in ipairs({ AllianceLogoFrame, HordeLogoFrame }) do
        if hitbox then hitbox:EnableMouse(false); end
    end
end

-- ---- faction rail ------------------------------------------------------------------------------------------
-- One crest per faction in a slim column on the screen's left edge. A crest shows that faction's races in the left
-- pane (sliding both panes in if hidden); the browsed faction's crest slides the race and details panes out, leaving
-- the scene clear.

local RAIL_TOP, RAIL_ICON, RAIL_STEP, SLIDE_TIME = -150, 40, 50, 0.18;
local UpdateFactionRail;

local function Edge(parent, point, w, h)
    local edge = Solid(parent, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 1);
    edge:SetPoint(point, parent, point, 0, 0);
    if w then edge:SetWidth(w); end
    if h then edge:SetHeight(h); end
    return edge;
end

-- Open/closed is remembered per page kind: the race page and the customization pages.
local function PanesOpen()
    if SK.mode == "custom" then return SK.customOpen ~= false; end
    return SK.paneOpen ~= false;
end

local function SlidePane(open)
    if SK.mode == "custom" then SK.customOpen = open; else SK.paneOpen = open; end
    SK.mover:Show();
end

-- Customization rows, the body page and the randomize button are pinned to the screen, not to the panels, so they
-- fade out with the slide; once the panels are gone the rows are hidden and the body page stops taking the mouse
-- (its own driver keeps it shown).
local function SetMouseTree(frame, on, saved)
    for _, child in ipairs({ frame:GetChildren() }) do
        if on then
            if saved[child] then child:EnableMouse(true); saved[child] = nil; end
        elseif child:IsMouseEnabled() then
            saved[child] = true;
            child:EnableMouse(false);
        end
        SetMouseTree(child, on, saved);
    end
end

local function FadeCustom(slide)
    local alpha = 1 + slide / PANEL_W;
    SK.hiddenRows = SK.hiddenRows or {};
    local restored = false;
    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row then
            if alpha <= 0 then
                if row:IsShown() then SK.hiddenRows[row] = true; row:Hide(); end
            else
                if SK.hiddenRows[row] then SK.hiddenRows[row] = nil; row:Show(); restored = true; end
                row:SetAlpha(alpha);
            end
        end
    end
    -- Option counts can change while hidden (a gender switch); let the stock refresh settle which rows belong.
    if restored and CharacterCreate_UpdateHairCustomization then CharacterCreate_UpdateHairCustomization(); end
    if BodyShapePanel then
        BodyShapePanel:SetAlpha(alpha);
        SK.bodyMouse = SK.bodyMouse or {};
        if alpha <= 0 and not SK.bodyMouseOff then SK.bodyMouseOff = true; SetMouseTree(BodyShapePanel, false, SK.bodyMouse);
        elseif alpha > 0 and SK.bodyMouseOff then SK.bodyMouseOff = nil; SetMouseTree(BodyShapePanel, true, SK.bodyMouse); end
    end
    if alpha <= 0 and CharacterCustomization_CloseChoices then CharacterCustomization_CloseChoices(); end
end

-- Leaving the customization pages: give back alpha and mouse, forget hidden rows (the stock page switch owns them now).
local function RestoreCustom()
    SK.hiddenRows = {};
    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row then row:SetAlpha(1); end
    end
    if BodyShapePanel then
        BodyShapePanel:SetAlpha(1);
        if SK.bodyMouseOff then SK.bodyMouseOff = nil; SetMouseTree(BodyShapePanel, true, SK.bodyMouse or {}); end
    end
end

local function BuildRailIcon(faction, slot)
    local icon = CreateFrame("Button", "CreatorSkinFaction" .. faction.key, SK.rail);
    icon:SetSize(RAIL_ICON, RAIL_ICON);
    icon:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", 6, RAIL_TOP + RAIL_ICON / 2 - (slot - 1) * RAIL_STEP);
    icon.faction = faction;
    icon.wash = Solid(icon, "BACKGROUND", faction.color[1], faction.color[2], faction.color[3], 0.16);
    icon.wash:SetAllPoints(icon);
    icon.edges = { Edge(icon, "TOPLEFT", RAIL_ICON, 1), Edge(icon, "BOTTOMLEFT", RAIL_ICON, 1),
                   Edge(icon, "TOPLEFT", 1, RAIL_ICON), Edge(icon, "TOPRIGHT", 1, RAIL_ICON) };
    -- Faction-coloured marker on the outer edge of the browsed crest.
    icon.bar = Solid(icon, "OVERLAY", faction.color[1], faction.color[2], faction.color[3], 1);
    icon.bar:SetPoint("TOPLEFT", icon, "TOPLEFT", 1, -1);
    icon.bar:SetPoint("BOTTOMLEFT", icon, "BOTTOMLEFT", 1, 1);
    icon.bar:SetWidth(2);
    local highlight = Solid(icon, "HIGHLIGHT", GOLD[1], GOLD[2], GOLD[3], 0.08);
    highlight:SetAllPoints(icon);
    icon.crest = icon:CreateTexture(nil, "ARTWORK");
    icon.crest:SetSize(RAIL_ICON - 6, RAIL_ICON - 6);
    icon.crest:SetTexture(faction.crest);
    icon.crest:SetTexCoord(unpack(faction.crop));
    icon.crest:SetPoint("CENTER", icon, "CENTER", 0, 0);
    icon:SetScript("OnClick", function(self)
        PlaySound("gsCharacterCreationClass");
        if SK.faction == self.faction.key then
            SlidePane(not PanesOpen());
        else
            SK.faction = self.faction.key;
            pcall(LayoutRaces);
            if not PanesOpen() then SlidePane(true); end
        end
        pcall(UpdateFactionRail);
        self:GetScript("OnEnter")(self);
    end);
    -- Hover names the faction; the crest is all the rail shows otherwise.
    icon:SetScript("OnEnter", function(self)
        local tip = SK.rail.tip;
        local n = #FactionRaces(self.faction.key);
        local action = SK.faction ~= self.faction.key and "Show races" or PanesOpen() and "Hide races" or "Show races";
        tip.text:SetText(self.faction.hex .. string.upper(self.faction.name) .. "|r  " .. n .. " races  \194\183  " .. action);
        tip:SetWidth(tip.text:GetStringWidth() + 20);
        tip:ClearAllPoints();
        tip:SetPoint("LEFT", self, "RIGHT", 8, 0);
        tip:Show();
    end);
    icon:SetScript("OnLeave", function() SK.rail.tip:Hide(); end);
    return icon;
end

local function BuildFactionRail()
    local rail = CreateFrame("Frame", "CreatorSkinFactionRail", SK.chrome);
    rail:SetAllPoints(SK.chrome);
    rail:SetFrameLevel(SK.chrome:GetFrameLevel() + 4);
    SK.rail = rail;
    -- A slim ink plate behind the crests, so they read the same with the pane in or out.
    local plate = Solid(rail, "BACKGROUND", INK[1], INK[2], INK[3], 0.9);
    plate:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", 0, RAIL_TOP + RAIL_ICON / 2 + 6);
    plate:SetSize(52, (#FACTIONS - 1) * RAIL_STEP + RAIL_ICON + 12);
    local seam = Solid(rail, "BORDER", GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.9);
    seam:SetPoint("TOPLEFT", plate, "TOPRIGHT", 0, 0);
    seam:SetPoint("BOTTOMLEFT", plate, "BOTTOMRIGHT", 0, 0);
    seam:SetWidth(1);
    for slot, faction in ipairs(FACTIONS) do BuildRailIcon(faction, slot); end

    local tip = CreateFrame("Frame", nil, rail);
    tip:SetHeight(26);
    tip:SetFrameLevel(rail:GetFrameLevel() + 2);
    local back = Solid(tip, "BACKGROUND", INK[1], INK[2], INK[3], 0.95);
    back:SetAllPoints(tip);
    Edge(tip, "TOPLEFT", nil, 1):SetPoint("TOPRIGHT", tip, "TOPRIGHT", 0, 0);
    Edge(tip, "BOTTOMLEFT", nil, 1):SetPoint("BOTTOMRIGHT", tip, "BOTTOMRIGHT", 0, 0);
    tip:SetClampedToScreen(true);
    tip.text = Text(tip, HEAD_FONT, 12, GOLD);
    tip.text:SetPoint("LEFT", tip, "LEFT", 10, 0);
    tip:Hide();
    rail.tip = tip;

    -- Eases the panels between open (slide 0) and hidden (slide -PANEL_W) over SLIDE_TIME. On the chrome, not the
    -- rail, so it also runs on the customization pages.
    local mover = CreateFrame("Frame", nil, SK.chrome);
    mover:Hide();
    mover:SetScript("OnUpdate", function(self, elapsed)
        if SK.mode ~= "race" and SK.mode ~= "custom" then self:Hide(); return; end
        local target = PanesOpen() and 0 or -PANEL_W;
        local step = PANEL_W * elapsed / SLIDE_TIME;
        local slide = SK.slide or 0;
        slide = slide < target and math.min(slide + step, target) or math.max(slide - step, target);
        SK.slide = slide;
        pcall(PlaceHeaders, slide);
        pcall(LayoutRaces);
        if SK.mode == "custom" then pcall(FadeCustom, slide); end
        if slide == target then self:Hide(); end
    end);
    SK.mover = mover;
    rail:Hide();
end

-- Customization pages: one tab on the left edge that slides both panels out and back.
local function BuildPanelTab()
    local tab = CreateFrame("Button", "CreatorSkinPanelTab", SK.chrome);
    tab:SetSize(22, 72);
    tab:SetFrameLevel(SK.chrome:GetFrameLevel() + 4);
    tab:SetPoint("TOPLEFT", SK.chrome, "TOPLEFT", 0, RAIL_TOP + RAIL_ICON / 2 + 6);
    local fill = Solid(tab, "BACKGROUND", INK[1], INK[2], INK[3], 0.9);
    fill:SetAllPoints(tab);
    Edge(tab, "TOPLEFT", 22, 1);
    Edge(tab, "BOTTOMLEFT", 22, 1);
    Edge(tab, "TOPRIGHT", 1, 72);
    local highlight = Solid(tab, "HIGHLIGHT", GOLD[1], GOLD[2], GOLD[3], 0.08);
    highlight:SetAllPoints(tab);
    tab.arrow = tab:CreateTexture(nil, "ARTWORK");
    tab.arrow:SetSize(14, 14);
    tab.arrow:SetPoint("CENTER", tab, "CENTER", 0, 0);
    tab:SetScript("OnClick", function()
        PlaySound("gsCharacterCreationClass");
        SlidePane(not PanesOpen());
    end);
    tab:Hide();
    SK.panelTab = tab;
end

function UpdateFactionRail()
    for _, faction in ipairs(FACTIONS) do
        local icon = _G["CreatorSkinFaction" .. faction.key];
        local on = faction.key == SK.faction;
        icon.wash:SetAlpha(on and SK.paneOpen ~= false and 1 or 0);
        icon.bar:SetAlpha(on and 1 or 0);
        icon.crest:SetAlpha(on and 1 or 0.6);
        for _, edge in ipairs(icon.edges) do
            if on then edge:SetVertexColor(GOLD[1], GOLD[2], GOLD[3], 1);
            else edge:SetVertexColor(GOLD_DIM[1], GOLD_DIM[2], GOLD_DIM[3], 0.5); end
        end
    end
end

-- Male / Female: glue buttons whose highlight stays lit when picked. Their clicks are the stock ones.
local function SkinToggle(button, text)
    local normal, pushed = button:GetNormalTexture(), button:GetPushedTexture();
    local skin = { extras = {}, size = { button:GetWidth(), button:GetHeight() },
                   normal = normal and normal:GetTexture(), pushed = pushed and pushed:GetTexture() };
    local own = {};
    for _, getter in ipairs({ "GetNormalTexture", "GetPushedTexture", "GetHighlightTexture", "GetCheckedTexture" }) do
        local texture = button[getter] and button[getter](button);
        if texture then own[texture] = true; end
    end
    -- Everything else drawn on the button: ring borders.
    for _, region in ipairs({ button:GetRegions() }) do
        local kind = region:GetObjectType();
        if (kind == "Texture" or kind == "FontString") and not own[region] then skin.extras[#skin.extras + 1] = region; end
    end
    for _, name in ipairs({ "staticTexture", "highlightTexture", "checkedTexture" }) do
        if button[name] and not own[button[name]] then skin.extras[#skin.extras + 1] = button[name]; end
    end
    skin.label = Text(button, HEAD_FONT, 12, GOLD);
    skin.label:SetPoint("CENTER", button, "CENTER", 0, 2);
    skin.label:SetText(text);
    SK.toggles[button] = skin;
end

local function ApplyToggle(button, on)
    local skin = SK.toggles[button];
    if not skin then return; end
    for _, texture in ipairs(skin.extras) do texture:SetAlpha(on and 0 or 1); end
    if skin.state ~= on then
        skin.state = on;
        if on then
            GlueLook(button);
        elseif skin.normal then
            button:SetNormalTexture(skin.normal);
            button:SetPushedTexture(skin.pushed or skin.normal);
            button:GetNormalTexture():SetTexCoord(0, 1, 0, 1);
            button:GetPushedTexture():SetTexCoord(0, 1, 0, 1);
        end
    end
    if not on then skin.label:Hide(); button:SetSize(skin.size[1], skin.size[2]); return; end
    skin.label:Show();
    -- Glue fonts: gold normally, white when picked.
    if button:GetChecked() then skin.label:SetTextColor(1, 1, 1); else skin.label:SetTextColor(1, 0.82, 0); end
end

-- Race page: the picks sit at the bottom. Customization pages: smaller, just above the name fields.
local function LayoutToggles()
    if not (SK.on and (SK.mode == "race" or SK.mode == "custom")) then return; end
    local custom = SK.mode == "custom";
    local male, female = CharacterCreateGenderButtonMale, CharacterCreateGenderButtonFemale;
    for _, button in ipairs({ male, female }) do
        if button then
            button:SetSize(custom and 112 or 130, custom and 32 or 38);
            button:ClearAllPoints();
            button:SetPoint("BOTTOM", CharacterCreateFrame, "BOTTOM", (button == male and -1 or 1) * (custom and 60 or 66),
                custom and 134 or 64);
        end
    end
end

-- The selected race: name, faction, description and racials, in the right panel whichever faction is browsed.
local function BuildCard()
    local card = CreateFrame("Frame", "CreatorSkinRaceCard", SK.chrome);
    card:SetSize(340, 220);
    card.rule = Solid(card, "BORDER", 1, 1, 1, 0.08);
    card.rule:SetHeight(1);
    card.rule:SetPoint("TOPLEFT", card, "TOPLEFT", 0, 0);
    card.rule:SetPoint("TOPRIGHT", card, "TOPRIGHT", 0, 0);
    card.name = Text(card, HEAD_FONT, 15, GOLD);
    card.name:SetPoint("TOPLEFT", card, "TOPLEFT", 0, -12);
    card.crest = card:CreateTexture(nil, "ARTWORK");
    card.crest:SetSize(18, 18);
    card.faction = Text(card, HEAD_FONT, 12, GOLD);
    card.crest:SetPoint("TOPLEFT", card.name, "BOTTOMLEFT", 0, -6);
    card.faction:SetPoint("LEFT", card.crest, "RIGHT", 6, 0);
    card.description = Text(card, BODY_FONT, 12, TEXT);
    card.description:SetPoint("TOPLEFT", card.crest, "BOTTOMLEFT", 0, -8);
    card.description:SetWidth(340);
    card.description:SetJustifyH("LEFT");
    card.traits = {};
    for i = 1, 4 do
        local icon = card:CreateTexture(nil, "ARTWORK");
        icon:SetSize(18, 18);
        local name = Text(card, BODY_FONT, 12, TEXT);
        name:SetPoint("LEFT", icon, "RIGHT", 8, 0);
        card.traits[i] = { icon = icon, name = name };
    end
    card.hint = Text(card, BODY_FONT, 11, MUTED);
    card.hint:SetText("Hover a portrait for details  \194\183  right-click it for every racial");
    SK.card = card;
end

local function UpdateCard()
    local card = SK.card;
    local index = GetSelectedRace and GetSelectedRace();
    local button = index and _G["CharacterCreateRaceButton" .. index];
    local faction = button and FactionOfRace(index);
    if not faction then card:Hide(); return; end
    card:ClearAllPoints();
    card:SetPoint("TOPRIGHT", SK.RIGHT.panel, "TOPRIGHT", -EDGE_X, -104);
    local info = button.raceInfo;
    card.name:SetText(string.upper((info and info.Name) or button.name or ""));
    card.crest:SetTexture(faction.crest);
    card.crest:SetTexCoord(unpack(faction.crop));
    card.faction:SetText(faction.hex .. string.upper(faction.name) .. "|r");
    card.description:SetText(info and info.Description or "");
    local last = card.description;
    for i, trait in ipairs(card.traits) do
        local spell = info and info["Spell_" .. i];
        if spell and spell.name and spell.name ~= "" then
            trait.icon:SetTexture("Interface\\Icons\\" .. (spell.icon or "INV_Misc_QuestionMark"));
            trait.icon:ClearAllPoints();
            trait.icon:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, last == card.description and -10 or -6);
            trait.name:SetText(spell.name);
            trait.icon:Show();
            trait.name:Show();
            last = trait.icon;
        else
            trait.icon:Hide();
            trait.name:Hide();
        end
    end
    card.hint:ClearAllPoints();
    card.hint:SetPoint("TOPLEFT", last, "BOTTOMLEFT", 0, -12);
    card:Show();
end

-- After an error: the race page goes back to the stock layout and selection glow.
local function RestoreRace()
    for button in pairs(SK.races) do
        if button.checkedTexture then button.checkedTexture:SetAlpha(1); end
        if button.staticTexture then button.staticTexture:Hide(); end
    end
    for button in pairs(SK.toggles) do ApplyToggle(button, false); end
    for _, region in ipairs({ CustomizationLogoAlliance, CustomizationLogoHorde, CustomizationTextAlliance,
                              CustomizationTextHorde }) do
        if region then region:SetAlpha(1); end
    end
    for _, hitbox in ipairs({ AllianceLogoFrame, HordeLogoFrame }) do
        if hitbox then hitbox:EnableMouse(true); end
    end
    for button in pairs(SK.races) do if SK.factionOf and SK.factionOf[button] then button:Show(); end end
    if SK.positionRaces then SK.positionRaces(); end
    if SK.positionGender then SK.positionGender(); end
end

-- After an error: give the rows their stock text back (their arrows keep working either way).
local function RestoreRows()
    for row, skin in pairs(SK.rows) do
        local background = _G[row:GetName() .. "Background"];
        if background then background:SetAlpha(1); end
        _G[row:GetName() .. "Text"]:SetAlpha(1);
        skin.label:Hide();
        skin.value:Hide();
    end
end

local function SetSkin(on)
    if SK.on == on then return; end
    SK.on = on;
    if SK.chrome then if on then SK.chrome:Show(); else SK.chrome:Hide(); end end
    for button in pairs(SK.buttons) do ApplyButton(button, on); end
    if not on and CharCreateRandomizeButton then
        CharCreateRandomizeButton:SetAlpha(1);
        CharCreateRandomizeButton:EnableMouse(true);
    end
    if not on then
        CharacterCreateRotateLeft:SetAlpha(1);
        CharacterCreateRotateRight:SetAlpha(1);
    end
end

local function SetSteps(active)
    local body = BodyShapePage_Active and type(GetBodyShapeNames) == "function" and EA_IS_RETAIL();
    for index, step in ipairs(SK.steps) do
        if index == active then step:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); else step:SetTextColor(MUTED[1], MUTED[2], MUTED[3]); end
    end
    -- Only show the Body step when the race has that page.
    if body then SK.steps[3]:Show(); SK.stepLines[2]:Show(); else SK.steps[3]:Hide(); SK.stepLines[2]:Hide(); end
end

local function RaceTick()
    if SK.mode ~= "race" then
        SK.mode = "race";
        -- Coming onto the race page, browse the selected race's faction; tab clicks change it after that.
        local selected = FactionOfRace(GetSelectedRace());
        SK.faction = selected and selected.key or SK.faction or FACTIONS[1].key;
        -- The pane keeps whatever the player last left it as (open by default).
        if SK.paneOpen == nil then SK.paneOpen = true; end
        SK.slide = SK.paneOpen and 0 or -PANEL_W;
        LayoutRaces();
        LayoutToggles();
    end
    for button in pairs(SK.buttons) do ApplyButton(button, true); end
    for button in pairs(SK.toggles) do ApplyToggle(button, true); end
    for button in pairs(SK.races) do
        -- Race enumeration re-tints these in faction colours every time; keep them gold.
        if button.highlightTexture then button.highlightTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]); end
        if button.checkedTexture then button.checkedTexture:SetAlpha(0); end
        if button.staticTexture then
            button.staticTexture:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
            if button:GetChecked() and button:IsShown() then button.staticTexture:Show(); else button.staticTexture:Hide(); end
        end
        -- The stock code re-shows portraits on enumeration; only the browsed faction's stay up.
        local browsed = SK.factionOf and SK.factionOf[button] == SK.faction and PaneShown();
        if browsed and not button:IsShown() then button:Show();
        elseif not browsed and button:IsShown() then button:Hide(); end
    end
    if not SK.mover:IsShown() then PlaceHeaders(SK.slide); end
    SK.rail:Show();
    UpdateFactionRail();
    local browsing;
    for _, faction in ipairs(FACTIONS) do if faction.key == SK.faction then browsing = faction; end end
    SK.LEFT.title:SetText("CHOOSE A RACE");
    SK.LEFT.subtitle:SetText(browsing and (browsing.hex .. string.upper(browsing.name) .. "|r  \194\183  "
        .. #FactionRaces(browsing.key) .. " races") or "");
    SK.RIGHT.title:SetText("RACE DETAILS");
    SK.RIGHT.subtitle:SetText("Your current pick");
    SetSteps(1);
    UpdateCard();
end

-- Two Forms (Worgen Wolf/Human, Dracthyr Dragon/Visage): a tab per form, each with its own option set.
local function BuildFormTabs()
    SK.formTabs = {};
    for index = 0, 1 do
        local tab = CreateFrame("CheckButton", "CreatorSkinFormTab" .. index, CharacterCreateFrame);
        tab:SetSize(110, 30);
        tab:SetFrameLevel(CharacterCreateFrame:GetFrameLevel() + 5);
        tab:SetPoint("TOP", CharacterCreateFrame, "TOP", index == 0 and -120 or 0, -56);
        GlueLook(tab);
        tab.label = Text(tab, HEAD_FONT, 12, GOLD);
        tab.label:SetPoint("CENTER", tab, "CENTER", 0, 2);
        tab:SetScript("OnClick", function()
            CycleCharCustomization("EA_FORM_SET", index);
            CharacterCreate_UpdateHairCustomization();
        end);
        SK.formTabs[index] = tab;
    end
    -- Shows both forms in the viewport at once; the tabs still pick which one the rows edit.
    local both = CreateFrame("CheckButton", "CreatorSkinFormBoth", CharacterCreateFrame);
    both:SetSize(110, 30);
    both:SetFrameLevel(CharacterCreateFrame:GetFrameLevel() + 5);
    both:SetPoint("TOP", CharacterCreateFrame, "TOP", 120, -56);
    GlueLook(both);
    both.label = Text(both, HEAD_FONT, 12, GOLD);
    both.label:SetPoint("CENTER", both, "CENTER", 0, 2);
    both.label:SetText("BOTH");
    both:SetScript("OnClick", function()
        local on = CycleCharCustomization("EA_FORM_DUAL", 2) == 1;
        CycleCharCustomization("EA_FORM_DUAL", on and 0 or 1);
    end);
    SK.formBoth = both;
end

local function UpdateFormTabs(show)
    local ok, count = pcall(CycleCharCustomization, "EA_FORM_COUNT", 1);
    show = show and EA_IS_RETAIL() and ok and count == 2;
    if show and not SK.formTabs then BuildFormTabs(); end
    if not SK.formTabs then return; end
    local current = show and CycleCharCustomization("EA_FORM_GET", 1);
    for index = 0, 1 do
        local tab = SK.formTabs[index];
        if show then
            tab:Show();
            tab.label:SetText(string.upper(CycleCharCustomization("EA_FORM_NAME", index) or ""));
            tab:SetChecked(current == index and 1 or nil);
            if current == index then tab.label:SetTextColor(1, 1, 1); else tab.label:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); end
        else
            tab:Hide();
        end
    end
    local both = SK.formBoth;
    if show then
        both:Show();
        local on = CycleCharCustomization("EA_FORM_DUAL", 2) == 1;
        both:SetChecked(on and 1 or nil);
        if on then both.label:SetTextColor(1, 1, 1); else both.label:SetTextColor(GOLD[1], GOLD[2], GOLD[3]); end
    else
        both:Hide();
    end
end

local function Tick()
    if SK.mode == "custom" and not (Personalizing() and not RacePage()) then RestoreCustom(); end
    if SK.panelTab and not (Personalizing() and not RacePage()) then SK.panelTab:Hide(); end
    if RacePage() then
        UpdateFormTabs(false);
        SetSkin(true);
        RaceTick();
        return;
    end
    if SK.card then SK.card:Hide(); end
    if SK.rail then SK.rail:Hide(); end
    if not Personalizing() then UpdateFormTabs(false); SK.mode = nil; SetSkin(false); return; end
    SetSkin(true);
    if SK.mode ~= "custom" then
        SK.mode = "custom";
        SK.slide = PanesOpen() and 0 or -PANEL_W;
        LayoutToggles();
    end
    if not SK.mover:IsShown() then
        PlaceHeaders(SK.slide);
        FadeCustom(SK.slide);
    end
    SK.panelTab:Show();
    local arrow = PanesOpen() and "Left" or "Right";
    SK.panelTab.arrow:SetTexture("Interface" .. "\\" .. "MoneyFrame" .. "\\" .. "Arrow-" .. arrow .. "-Up");
    SK.panelTab.arrow:SetVertexColor(GOLD[1], GOLD[2], GOLD[3]);
    -- The stock page switch hides the gender picks here; this page offers them too.
    if CharacterCreateGenderButtonsContainer and not CharacterCreateGenderButtonsContainer:IsShown() then
        CharacterCreateGenderButtonsContainer:Show();
    end
    for button in pairs(SK.toggles) do ApplyToggle(button, true); end

    for i = 1, 64 do
        local row = _G["CharacterCustomizationButtonFrame" .. i];
        if row then
            if not SK.rows[row] then SkinRow(row); end
            if row:IsShown() then UpdateRow(row, SK.rows[row]); end
        end
    end
    for button in pairs(SK.buttons) do ApplyButton(button, true); end

    local body = BodyPage();
    UpdateFormTabs(not body);
    if body then
        SkinBodyPage();
        for i = 1, 128 do
            local slider = _G["BodyShapeSlider" .. i];
            if not slider then break; end
            if not SK.sliders[slider] then SkinSlider(slider); else SK.sliders[slider](); end
        end
    end

    SK.LEFT.title:SetText(body and "BODY" or "APPEARANCE");
    SK.LEFT.subtitle:SetText(body and "Drag the triangle to blend body types" or RaceLine());
    SK.RIGHT.title:SetText(body and "FACE" or "DETAILS");
    SK.RIGHT.subtitle:SetText(body and "Right-click a slider to reset it" or "Click a value for the full list");
    SetSteps(body and 3 or 2);

    -- The hint replaces the rotate arrows; the whole-character randomize sits in the details header.
    CharacterCreateRotateLeft:SetAlpha(0);
    CharacterCreateRotateRight:SetAlpha(0);
    if CharCreateRandomizeButton then
        CharCreateRandomizeButton:ClearAllPoints();
        -- Inner end of the Details header, clear of its title.
        CharCreateRandomizeButton:SetPoint("TOPLEFT", CharacterCreateFrame, "TOPRIGHT", -(EDGE_X + 340), -58);
        local shown = 1 + (SK.slide or 0) / PANEL_W;
        if body or shown <= 0.5 then CharCreateRandomizeButton:SetAlpha(body and 0 or shown); CharCreateRandomizeButton:EnableMouse(false);
        else CharCreateRandomizeButton:SetAlpha(shown); CharCreateRandomizeButton:EnableMouse(true); end
    end
end

local function SK_Install()
    BuildChrome();
    SkinButton(CharCreateOkayButton);
    SkinButton(CharCreateBackButton);
    SkinNameEdit(CharacterCreateNameEdit);
    SkinNameEdit(CharacterCreateLastNameEdit);
    if CharCreatePersonalizeButton then SkinButton(CharCreatePersonalizeButton); end
    BuildCard();
    BuildFactionRail();
    BuildPanelTab();
    for button, text in pairs({ [CharacterCreateGenderButtonMale or 0] = "MALE", [CharacterCreateGenderButtonFemale or 0] = "FEMALE" }) do
        if button ~= 0 then SkinToggle(button, text); end
    end
    -- The stock code re-lays the portraits and gender picks on enumeration and resize; keep the skin's layout.
    SK.positionRaces, SK.positionGender = CharacterCreate_PositionRaceButtons, CharacterCreate_PositionGenderButtons;
    function CharacterCreate_PositionRaceButtons(...)
        local a, b = SK.positionRaces(...);
        if not SK.broken then pcall(LayoutRaces); end
        return a, b;
    end
    function CharacterCreate_PositionGenderButtons(...)
        local a, b = SK.positionGender(...);
        if not SK.broken then pcall(LayoutToggles); end
        return a, b;
    end
    local driver = CreateFrame("Frame");
    driver:SetScript("OnUpdate", function(self, elapsed)
        self.elapsed = (self.elapsed or 0) + elapsed;
        if self.elapsed < 0.1 then return; end
        self.elapsed = 0;
        local ok, err = pcall(Tick);
        if not ok then
            SK.broken = tostring(err);
            SK_Log("tick error: " .. SK.broken);
            pcall(SetSkin, false);
            pcall(RestoreRows);
            pcall(RestoreRace);
            self:SetScript("OnUpdate", nil);
        end
    end);
    SK_Log("installed");
end

local skinWaiter = CreateFrame("Frame");
skinWaiter:SetScript("OnUpdate", function(self)
    if not (CharacterCreate and CharacterCreateFrame and CharCreateOkayButton and CharacterCreateNameEdit) then return; end
    self:SetScript("OnUpdate", nil);
    local ok, err = pcall(SK_Install);
    if not ok then
        SK.broken = tostring(err);
        SK_Log("install failed: " .. SK.broken);
        SK.on = true;
        pcall(SetSkin, false);
    end
end);
end)();

RACE_ICON_TCOORDS["ETHEREALALLIANCE_MALE"]={0,1,0,1};
RACE_ICON_TEXTURES["ETHEREALALLIANCE_MALE"]="Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EtherealAllianceMale";

RACE_ICON_TCOORDS["ETHEREALALLIANCE_FEMALE"]={0,1,0,1};
RACE_ICON_TEXTURES["ETHEREALALLIANCE_FEMALE"]="Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EtherealAllianceFemale";

RACE_ICON_TCOORDS["ETHEREALHORDE_MALE"]={0,1,0,1};
RACE_ICON_TEXTURES["ETHEREALHORDE_MALE"]="Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EtherealHordeMale";

RACE_ICON_TCOORDS["ETHEREALHORDE_FEMALE"]={0,1,0,1};
RACE_ICON_TEXTURES["ETHEREALHORDE_FEMALE"]="Interface\\Glues\\CharacterCreate\\UI-CharacterCreate-EtherealHordeFemale";

-- Shotboom Kyrian additions; existing Draenei racials remain granted.
for index = 1, 20 do _G["ABILITY_INFO_KYRIANALLIANCE"..index] = nil; end
_G["ABILITY_INFO_KYRIANALLIANCE1"] = "Ascended Hover: Spread your wings and take flight. Use normal flight controls to ascend and descend. Flight lasts indefinitely outside combat and grants up to 15 total seconds in combat per activation. Activating Hover from the ground while in combat grants immunity to Stun effects for 3 seconds. A successful stun ends Hover. When Hover ends, descend safely to the ground and begin a 2-minute cooldown. Activate again to land.";
_G["ABILITY_INFO_KYRIANALLIANCE2"] = "Ascendant Charge: Draw your weapon and leap to an enemy within 25 yards, dealing Holy impact damage and slowing nearby enemies by 30% for 3 seconds. While hovering, dive instead for 20% more impact damage; Hover ends after the dive. Interrupted dives end Hover safely, and its cooldown starts on landing. The impact creates a 5-yard Consecration for 3 seconds, dealing Holy damage each second. Damage scales with your higher attack power or spell power. 2-minute cooldown.";
_G["ABILITY_INFO_KYRIANALLIANCE3"] = "Ascended Wings: Reduces falling damage by 50%.";
_G["ABILITY_INFO_KYRIANALLIANCE4"] = "Purity of Bastion: Disease, Poison, and Curse effects last 10% less time.";
_G["ABILITY_INFO_KYRIANALLIANCE5"] = "Virtue of Courage: Increases critical strike chance by 1%.";
_G["ABILITY_INFO_KYRIANALLIANCE6"] = "Soul Guide: Move 40% faster as a released spirit.";
_G["ABILITY_INFO_KYRIANALLIANCE7"] = "Steward's Craft: Increases Jewelcrafting skill by 5.";
_G["ABILITY_INFO_KYRIANALLIANCE8"] = "Also retains Esteria's Draenei racial and language grants.";
