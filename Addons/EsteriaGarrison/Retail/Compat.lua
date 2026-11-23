-- Feeds Blizzard_GarrisonBuildingUI from the garrison the server already sent.
-- Strings are the ruRU architect window. Building names stay whatever the server DB has.

-- The running client is enUS, so Friz Quadrata cannot draw Cyrillic. These are the retail enUS strings.
GARRISON_ARCHITECT = "Garrison Architect"
GARRISON_TOWN_HALL_ALLIANCE = "Town Hall"
GARRISON_TOWN_HALL_HORDE = "Great Hall"
GARRISON_TOWN_HALL_ALLIANCE_UPGRADE_TIER1_TOOLTIP = "Upgrade your garrison to Tier 2."
GARRISON_TOWN_HALL_ALLIANCE_UPGRADE_TIER2_TOOLTIP = "Upgrade your garrison to Tier 3."
GARRISON_TOWN_HALL_HORDE_UPGRADE_TIER1_TOOLTIP = GARRISON_TOWN_HALL_ALLIANCE_UPGRADE_TIER1_TOOLTIP
GARRISON_TOWN_HALL_HORDE_UPGRADE_TIER2_TOOLTIP = GARRISON_TOWN_HALL_ALLIANCE_UPGRADE_TIER2_TOOLTIP
GARRISON_EMPTY_PLOT_SMALL = "Small Empty Plot"
GARRISON_EMPTY_PLOT_MEDIUM = "Medium Empty Plot"
GARRISON_EMPTY_PLOT_LARGE = "Large Empty Plot"
GARRISON_EMPTY_PLOT_EXPLANATION = "Select a building from the list, then drop it on a matching plot."
GARRISON_YOUR_MATERIAL = "Your Resources"
GARRISON_BUILDING_IN_PROGRESS = "Building in progress"
GARRISON_BUILDING_COMPLETE = "Building complete"
GARRISON_UPGRADE_IN_PROGRESS = "Upgrade in progress"
GARRISON_UPGRADE_COMPLETE = "Upgrade complete"
GARRISON_FINALIZE_BUILDING_TOOLTIP = "Click to finalize construction."
GARRISON_CANCEL_BUILD_BUILDING = "Cancel this building?"
GARRISON_CANCEL_UPGRADE_BUILDING = "Cancel this upgrade?"
GARRISON_BUILDING_LEVEL_TOOLTIP_TEXT = "Level %d"
GARRISON_BUILDING_LEVEL_UPGRADE = "Upgrade to level %d"
GARRISON_BUILDING_LEVEL_MAX = "Maximum level"
GARRISON_BUILDING_LEVEL_ONE = "Level 1"
GARRISON_PLAN_REQUIRED = "Plan required"
GARRISON_UPGRADE_NEED_PLAN = "You need a plan to upgrade this building."
GARRISON_UPGRADE_ERROR = "This building cannot be upgraded yet."
ERR_GARRISON_NOT_ENOUGH_CURRENCY = "You don't have enough Garrison Resources."
ERR_GARRISON_NOT_ENOUGH_GOLD = "You don't have enough gold."
GARRISON_LOCKED_PLOT_TOOLTIP = "Complete the quest to gain access to this building."
GARRISON_BUILDING_LOCKED = "Building Locked"
GARRISON_BUILDING_PLANS_REQUIRED = "Plans required"
GARRISON_BUILDING_SELECT_FOLLOWER_TITLE = "Select a Follower"
GARRISON_BUILDING_NO_FOLLOWERS = "No followers"
GARRISON_SWITCH_SPECIALIZATIONS = "Switch Specializations"
GARRISON_TOWN_HALL_LEVEL1_DESCRIPTION = "A tier 1 garrison."
GARRISON_TOWN_HALL_LEVEL2_DESCRIPTION = "A tier 2 garrison."
GARRISON_TOWN_HALL_LEVEL3_DESCRIPTION = "A tier 3 garrison."
GARRISON_CURRENT_LEVEL = "Level %d"
GARRISON_BUILDING_FOLLOWER_WORKING = "%s is working here."
GARRISON_BUILDING_FOLLOWER_EMPTY = "A follower can be assigned here."
TIME_LABEL = TIME_LABEL or "Time:"
UPGRADE = UPGRADE or "Upgrade"
GARRISON_BUILD = GARRISON_BUILD or "Build"
GARRISON_TIER = GARRISON_TIER or "Tier"

if not BreakUpLargeNumbers then
	function BreakUpLargeNumbers(value) return tostring(value or 0) end
end
if not GetCVarBitfield then
	function GetCVarBitfield() return true end
end
if not SetCVarBitfield then
	function SetCVarBitfield() end
end

Enum = Enum or {}
Enum.GarrisonType = Enum.GarrisonType or { Type_6_0_Garrison = 2 }
Enum.GarrisonFollowerType = Enum.GarrisonFollowerType or { FollowerType_6_0_GarrisonFollower = 4 }

SOUNDKIT = SOUNDKIT or setmetatable({}, { __index = function() return "igMainMenuOpen" end })
HelpPlate = HelpPlate or {
	Hide = function() end,
	IsShowingHelpInfo = function() return true end,
	ShowTutorialTooltip = function() end,
}

C_CurrencyInfo = C_CurrencyInfo or {}
function C_CurrencyInfo.GetCurrencyInfo()
	return {
		quantity = EsteriaArchitect and EsteriaArchitect.resources or 0,
		iconFileID = "Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\GarrMission_CurrencyIcon-Material.blp",
	}
end

local texture = CreateFrame("Frame"):CreateTexture()
local textureMethods = getmetatable(texture).__index
texture:Hide()
-- The garrison's own atlas entries first; every other atlas goes to the client's native SetAtlas (the full retail
-- table), so this override does not blank atlases for the rest of the UI.
local nativeSetAtlas = textureMethods.SetAtlas
function textureMethods:SetAtlas(name, useAtlasSize, ...)
	local entry = EsteriaArchitectAtlas and EsteriaArchitectAtlas[name]
	if not entry then
		if nativeSetAtlas then return nativeSetAtlas(self, name, useAtlasSize, ...) end
		return
	end
	self:SetTexture(entry[1])
	local tw, th = entry[4] or entry[2], entry[5] or entry[3]
	if tw and th and tw > 0 and th > 0 then
		self:SetTexCoord(0, entry[2] / tw, 0, entry[3] / th)
	else
		self:SetTexCoord(0, 1, 0, 1)
	end
	if useAtlasSize then self:SetSize(entry[2], entry[3]) end
end

local cooldown = CreateFrame("Cooldown", nil, UIParent)
cooldown:Hide()
local cooldownMethods = getmetatable(cooldown).__index
cooldownMethods.SetCooldownUNIX = cooldownMethods.SetCooldownUNIX or function(self, start, duration)
	if self.SetReverse then self:SetReverse(true) end
	self:SetCooldown(start or 0, duration or 0)
	self:Show()
end
cooldownMethods.SetCooldownDuration = cooldownMethods.SetCooldownDuration or function(self, duration)
	if not duration or duration == 0 then self:SetCooldown(0, 0) end
end
cooldownMethods.SetSwipeTexture = cooldownMethods.SetSwipeTexture or function() end

local tooltipMethods = getmetatable(GameTooltip).__index
tooltipMethods.SetCurrencyByID = tooltipMethods.SetCurrencyByID or function() end

local KIT = {
	[1] = "GarrBuilding_Mine", [2] = "GarrBuilding_Farm", [3] = "GarrBuilding_Barn",
	[4] = "GarrBuilding_LumberMill", [5] = "GarrBuilding_Inn", [6] = "GarrBuilding_TradingPost",
	[7] = "GarrBuilding_PetStable", [8] = "GarrBuilding_Barracks", [10] = "GarrBuilding_Armory",
	[11] = "GarrBuilding_Stables", [13] = "GarrBuilding_MageTower", [14] = "GarrBuilding_SalvageYard",
	[15] = "GarrBuilding_Storehouse", [16] = "GarrBuilding_Alchemy", [17] = "GarrBuilding_Blacksmith",
	[18] = "GarrBuilding_Enchanting", [19] = "GarrBuilding_Engineering", [20] = "GarrBuilding_Inscription",
	[21] = "GarrBuilding_Jewelcrafting", [22] = "GarrBuilding_Leatherworking", [23] = "GarrBuilding_Tailoring",
	[24] = "GarrBuilding_Fishing", [25] = "GarrBuilding_SparringArena", [26] = "GarrBuilding_Workshop",
}
local PLOT_SIZE = { [12] = 1, [13] = 2, [14] = 3 }
-- One building each. Retail draws that building on the plot and locks it
-- until the unlock quest is turned in. They are not small/medium/large tabs.
local QUEST_PLOT = { [28] = true, [29] = true, [31] = true }
local MAP_ATLAS = {
	[1158] = "Garr_Map_Shadowmoon1_A", [1331] = "Garr_Map_Shadowmoon2_A", [1159] = "Garr_Map_Shadowmoon3_A",
	[1152] = "Garr_Map_Frostfire1_H", [1330] = "Garr_Map_Frostfire2_H", [1153] = "Garr_Map_Frostfire3_H",
}

EsteriaArchitect = { plots = {}, buildings = {}, resources = 0, level = 1, horde = false, map = 1158 }

local function split(text, sep)
	local out, start = {}, 1
	while true do
		local found = string.find(text, sep, start, true)
		if not found then out[#out + 1] = string.sub(text, start) return out end
		out[#out + 1] = string.sub(text, start, found - 1)
		start = found + string.len(sep)
	end
end

local function buildingOf(id)
	for _, building in ipairs(EsteriaArchitect.buildings) do
		if building.id == id then return building end
	end
end

local function plotOf(id)
	for _, plot in ipairs(EsteriaArchitect.plots) do
		if plot.id == id then return plot end
	end
end

local function side()
	return EsteriaArchitect.horde and "H" or "A"
end

local function kitOf(building)
	local prefix = KIT[building.type]
	if not prefix then return nil end
	return string.format("%s_%d_%s", prefix, building.level or 1, side())
end

local function iconOf(building)
	local kit = kitOf(building)
	local entry = kit and EsteriaArchitectAtlas and EsteriaArchitectAtlas[kit .. "_Map"]
	return entry and entry[1] or "Interface\\Icons\\INV_Misc_QuestionMark"
end

local function sizeOf(plot)
	-- Tier-2 Lunarfall stores 0 in every ui marker, so the marker cannot tell a
	-- medium plot from a small one. The plot type is the size.
	local fromPlot = PLOT_SIZE[plot.garr]
	if fromPlot then return fromPlot end
	if plot.marker and plot.marker >= 0 and plot.marker <= 2 then return plot.marker + 1 end
	return 1
end

-- Lunarfall art is north-up, while the DB2 fractions are measured from the
-- bottom. Without the flip the town hall sits in the north garden and the
-- yard plots miss the round pads. Frostwall stays raw until its art is checked.
local function placeOnMap(x, y)
	if not x or not y then return x, y end
	local map = EsteriaArchitect.map
	if map == 1158 or map == 1331 or map == 1159 then return x, 1 - y end
	return x, y
end

local function positionOf(plot)
	if plot.x and plot.y and (plot.x > 0 or plot.y > 0) then
		local x, y = placeOnMap(plot.x, plot.y)
		return x, y
	end
	local page = EsteriaArchitectPlots and EsteriaArchitectPlots[EsteriaArchitect.map]
	local spot = page and page[plot.id]
	if spot then
		local x, y = placeOnMap(spot[1], spot[2])
		return x, y, spot[3]
	end
	return 0.5, 0.5
end

function EsteriaArchitect_Apply(text)
	local state = { plots = {}, buildings = {}, resources = 0, level = 1, horde = false, map = 1158, upgradeRes = 0, upgradeGold = 0 }
	for line in string.gmatch(text or "", "[^\n]+") do
		local row = split(line, "\t")
		local kind = row[1]
		if kind == "G" then
			state.horde = (tonumber(row[3]) or 0) == 1
			state.level = tonumber(row[4]) or 1
			state.resources = tonumber(row[5]) or 0
			state.map = tonumber(row[8]) or state.map
			state.upgradeRes = tonumber(row[9]) or 0
			state.upgradeGold = tonumber(row[10]) or 0
			state.townX = tonumber(row[11])
			state.townY = tonumber(row[12])
		elseif kind == "P" then
			local left = tonumber(row[6]) or 0
			state.plots[#state.plots + 1] = {
				id = tonumber(row[2]) or 0, garr = tonumber(row[3]) or 0, building = tonumber(row[4]) or 0,
				active = (tonumber(row[5]) or 0) == 1, left = left, readyAt = GetTime() + left, name = row[7] or "",
				x = tonumber(row[8]), y = tonumber(row[9]), marker = tonumber(row[10]),
			}
		elseif kind == "B" then
			state.buildings[#state.buildings + 1] = {
				id = tonumber(row[2]) or 0, type = tonumber(row[3]) or 0, level = tonumber(row[4]) or 1,
				garr = tonumber(row[5]) or 0, resources = tonumber(row[6]) or 0, gold = tonumber(row[7]) or 0,
				seconds = tonumber(row[8]) or 0, known = (tonumber(row[9]) or 0) == 1, name = row[10] or "",
				tooltip = row[11] or "",
			}
		end
	end
	EsteriaArchitect = state
end

local function formatTime(seconds)
	seconds = tonumber(seconds) or 0
	if seconds <= 0 then return "" end
	local hours = math.floor(seconds / 3600)
	local minutes = math.floor((seconds % 3600) / 60)
	if hours > 0 and minutes > 0 then return hours .. " hr " .. minutes .. " min" end
	if hours > 0 then return hours .. " hr" end
	if minutes > 0 then return minutes .. " min" end
	return seconds .. " sec"
end

local function buildClock(plot, building)
	local total = building and building.seconds or 0
	local readyAt = plot and plot.readyAt or GetTime()
	local remain = math.max(0, readyAt - GetTime())
	if total < remain then total = remain end
	if total <= 0 then total = remain > 0 and remain or 1 end
	return readyAt - total, total, remain
end

local function upgradeChain(building)
	local chain = {}
	for _, other in ipairs(EsteriaArchitect.buildings) do
		if other.type == building.type then chain[#chain + 1] = other end
	end
	table.sort(chain, function(a, b) return (a.level or 1) < (b.level or 1) end)
	local ids = {}
	for _, other in ipairs(chain) do ids[#ids + 1] = other.id end
	return ids
end

local function info(building, ownedPlot)
	if not building then return nil end
	local kit = kitOf(building)
	local startTime, duration, remain = buildClock(ownedPlot, building)
	local isBuilding = ownedPlot and not ownedPlot.active and remain > 0 or false
	local canActivate = ownedPlot and not ownedPlot.active and remain <= 0 or false
	return building.id, building.name, kit, iconOf(building), building.tooltip or "", building.level,
		824, building.resources, building.gold or 0, formatTime(building.seconds or duration),
		not building.known, false, nil, upgradeChain(building), false, building.level >= 3, false,
		nil, nil, nil, isBuilding, startTime, duration, formatTime(remain), canActivate, false
end

C_Garrison = C_Garrison or {}

function C_Garrison.GetBuildingSizes()
	return {
		{ id = 1, name = "Small" },
		{ id = 2, name = "Medium" },
		{ id = 3, name = "Large" },
	}
end

function C_Garrison.GetBuildingsForSize(_, category)
	local list = {}
	for _, building in ipairs(EsteriaArchitect.buildings) do
		if building.level == 1 and (PLOT_SIZE[building.garr] or 0) == category then
			local owned
			for _, plot in ipairs(EsteriaArchitect.plots) do
				if plot.building == building.id then owned = plot.id break end
			end
			list[#list + 1] = {
				buildingID = building.id, name = building.name, textureKit = kitOf(building),
				icon = iconOf(building), needsPlan = not building.known, plotID = owned, uiTab = category,
			}
		end
	end
	return list
end

function C_Garrison.GetBuildings()
	local list = {}
	for _, plot in ipairs(EsteriaArchitect.plots) do
		local building = plot.building ~= 0 and buildingOf(plot.building)
		if building then
			list[#list + 1] = { buildingID = building.id, plotID = plot.id, uiTab = PLOT_SIZE[building.garr] or sizeOf(plot) }
		end
	end
	return list
end

local function lockedQuestBuilding(plot)
	if plot.building ~= 0 or not QUEST_PLOT[plot.garr] then return nil end
	for _, building in ipairs(EsteriaArchitect.buildings) do
		if building.garr == plot.garr and building.level == 1 then return building end
	end
end

function C_Garrison.GetPlots()
	local list = {}
	for _, plot in ipairs(EsteriaArchitect.plots) do
		local x, y, markerSize = positionOf(plot)
		local row = { id = plot.id, x = x, y = y, size = markerSize or sizeOf(plot) }
		local locked = lockedQuestBuilding(plot)
		if locked then
			row.buildingID = locked.id
			row.building = kitOf(locked)
		end
		list[#list + 1] = row
	end
	return list
end

function C_Garrison.GetTabForPlot(plotID)
	local plot = plotOf(plotID)
	if not plot or QUEST_PLOT[plot.garr] then return nil end
	return PLOT_SIZE[plot.garr] or sizeOf(plot)
end

function C_Garrison.GetBuildingsForPlot(plotID)
	local plot = plotOf(plotID)
	local size = plot and (PLOT_SIZE[plot.garr] or sizeOf(plot))
	local list = {}
	if not size then return list end
	for _, building in ipairs(EsteriaArchitect.buildings) do
		if building.level == 1 and (PLOT_SIZE[building.garr] or 0) == size then
			list[#list + 1] = building.id
		end
	end
	return list
end

function C_Garrison.GetPlotsForBuilding(buildingID)
	local list = {}
	local building = buildingOf(buildingID)
	for _, plot in ipairs(EsteriaArchitect.plots) do
		if building and plot.garr == building.garr and plot.building == 0 and plot.active then
			list[#list + 1] = plot.id
		end
	end
	return list
end

function C_Garrison.GetGarrisonInfo()
	local town = EsteriaArchitectTown and EsteriaArchitectTown[EsteriaArchitect.map]
	local x = EsteriaArchitect.townX or (town and town[1]) or 0.36
	local y = EsteriaArchitect.townY or (town and town[2]) or 0.63
	x, y = placeOnMap(x, y)
	if EsteriaArchitect.map == 1158 then y = y + 0.06 end
	local atlas = MAP_ATLAS[EsteriaArchitect.map] or MAP_ATLAS[1158]
	return EsteriaArchitect.level or 1, atlas, x, y
end

function C_Garrison.GetBuildingTimeRemaining(plotID)
	local plot = plotOf(plotID)
	if not plot or plot.building == 0 or plot.active then return 0, "" end
	local _, total, remain = buildClock(plot, buildingOf(plot.building))
	return remain, formatTime(remain), total
end

function C_Garrison.GetOwnedBuildingInfoAbbrev(plotID)
	local plot = plotOf(plotID)
	local building = plot and plot.building ~= 0 and buildingOf(plot.building)
	if not building then return nil end
	local id, name, kit, icon, _, rank, _, _, _, _, _, _, _, _, _, _, _, _, _, _, isBuilding, startTime, duration, _, canActivate =
		info(building, plot)
	return id, name, kit, icon, rank, isBuilding, startTime, duration, canActivate, false, false
end

function C_Garrison.GetOwnedBuildingInfo(plotID)
	local plot = plotOf(plotID)
	local building = plot and buildingOf(plot.building)
	if not building then return nil end
	return info(building, plot)
end

function C_Garrison.GetBuildingInfo(buildingID)
	local building = buildingOf(buildingID)
	if not building then return nil end
	return info(building, nil)
end

function C_Garrison.GetBuildingUpgradeInfo(buildingID)
	local building = buildingOf(buildingID)
	if not building then return nil end
	local nextId
	for _, other in ipairs(EsteriaArchitect.buildings) do
		if other.type == building.type and other.level == building.level + 1 then nextId = other break end
	end
	if not nextId or nextId.level > (EsteriaArchitect.level or 1) then return nil end
	return nextId.id, nextId.name, kitOf(nextId), iconOf(nextId), nextId.level, 824, nextId.resources,
		nextId.gold or 0, nextId.seconds
end

function C_Garrison.GetBuildingTooltip(buildingID)
	local building = buildingOf(buildingID)
	return building and building.tooltip or ""
end

function C_Garrison.GetGarrisonUpgradeCost()
	return EsteriaArchitect.upgradeRes or 0, EsteriaArchitect.upgradeGold or 0
end

function C_Garrison.CanUpgradeGarrison()
	return (EsteriaArchitect.level or 1) < 3 and (EsteriaArchitect.upgradeRes or 0) > 0
		and (EsteriaArchitect.resources or 0) >= EsteriaArchitect.upgradeRes
end

function C_Garrison.RequestGarrisonUpgradeable() end
function C_Garrison.CloseArchitect() end
function C_Garrison.GetSpecChangeCost() return 0, 0 end
function C_Garrison.GetFollowerInfoForBuilding() return nil end

function C_Garrison.PlaceBuilding(plotID, buildingID)
	if EsteriaGarrison_Action then EsteriaGarrison_Action(3, plotID or 0, buildingID or 0) end
end

function C_Garrison.CancelConstruction(plotID)
	if EsteriaGarrison_Action then EsteriaGarrison_Action(4, plotID or 0, 0) end
end

function C_Garrison.UpgradeBuilding() end

function C_Garrison.UpgradeGarrison()
	if EsteriaGarrison_Action then EsteriaGarrison_Action(5, 0, 0) end
end

local function refresh()
	if GarrisonBuildingFrame and GarrisonBuildingFrame:IsShown() and GarrisonBuildingFrame_UpdateGarrisonInfo then
		GarrisonBuildingFrame_UpdateGarrisonInfo(GarrisonBuildingFrame)
		if GarrisonBuildingFrame_UpdateCurrency then GarrisonBuildingFrame_UpdateCurrency() end
		if GarrisonBuildingFrame_UpdateBuildingList then GarrisonBuildingFrame_UpdateBuildingList() end
	end
end

local watcher = CreateFrame("Frame")
watcher:RegisterEvent("ESTERIA_GARRISON_STATE")
watcher:SetScript("OnEvent", function()
	local text = EsteriaGarrison_GetState and EsteriaGarrison_GetState() or ""
	EsteriaArchitect_Apply(text)
	local mode = string.match(text, "^OPEN\t(%w+)")
	if mode == "buildings" and GarrisonBuildingUI_ToggleFrame then
		if not GarrisonBuildingFrame:IsShown() then
			GarrisonBuildingUI_ToggleFrame()
		else
			refresh()
		end
	else
		refresh()
	end
end)
