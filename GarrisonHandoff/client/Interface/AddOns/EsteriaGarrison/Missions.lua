-- Garrison Missions window. Data comes from the server state packet (real GarrMission / follower rows).
-- Click a follower to seat them. Click a filled seat to stand them up. /garrisonmission opens the table.

local FRAME_W, FRAME_H = 980, 662
local RESULT = {
	[8] = "Not enough garrison resources.",
	[12] = "That mission is not available.",
	[13] = "That follower cannot go.",
}

local CLASS_ICON = {
	["Death Knight"] = "GarrMission_ClassIcon-DeathKnight",
	Druid = "GarrMission_ClassIcon-Druid",
	Hunter = "GarrMission_ClassIcon-Hunter",
	Mage = "GarrMission_ClassIcon-Mage",
	Monk = "GarrMission_ClassIcon-Monk",
	Paladin = "GarrMission_ClassIcon-Paladin",
	Priest = "GarrMission_ClassIcon-Priest",
	Rogue = "GarrMission_ClassIcon-Rogue",
	Shaman = "GarrMission_ClassIcon-Shaman",
	Warlock = "GarrMission_ClassIcon-Warlock",
	Warrior = "GarrMission_ClassIcon-Warrior",
}

local QUALITY = {
	[0] = { 0.62, 0.62, 0.62 },
	[1] = { 1, 1, 1 },
	[2] = { 0.12, 1, 0 },
	[3] = { 0.00, 0.44, 0.87 },
	[4] = { 0.64, 0.21, 0.93 },
	[5] = { 1.00, 0.50, 0.00 },
}

local state = { followers = {}, missions = {}, abilities = {}, enemies = {}, party = {}, resources = 0 }
local followerScroll, missionScroll, picked, tab = 0, 0, 0, 1
local frame

local function split(text, sep)
	local out, start = {}, 1
	while true do
		local found = string.find(text, sep, start, true)
		if not found then out[#out + 1] = string.sub(text, start) return out end
		out[#out + 1] = string.sub(text, start, found - 1)
		start = found + string.len(sep)
	end
end

-- retail/1000173.blp is the 512 sheet of mission-type icons. Rects are the atlas members.
local TYPE_RECT = {
	[3] = { 133, 197, 1, 65 }, [4] = { 1, 65, 133, 197 }, [5] = { 265, 329, 67, 131 },
	[6] = { 1, 65, 397, 461 }, [7] = { 67, 131, 265, 329 }, [8] = { 67, 131, 133, 197 },
	[9] = { 199, 263, 1, 65 }, [10] = { 133, 197, 67, 131 }, [11] = { 199, 263, 67, 131 },
	[12] = { 67, 131, 199, 263 }, [13] = { 67, 131, 67, 131 }, [14] = { 1, 65, 133, 197 },
	[15] = { 265, 329, 1, 65 }, [16] = { 1, 65, 67, 131 }, [17] = { 331, 395, 67, 131 },
	[18] = { 1, 65, 1, 65 }, [19] = { 67, 131, 1, 65 }, [20] = { 331, 395, 1, 65 },
	[21] = { 397, 461, 1, 65 }, [22] = { 1, 65, 199, 263 }, [23] = { 1, 65, 265, 329 },
	[24] = { 1, 65, 331, 395 }, [25] = { 397, 461, 67, 131 }, [35] = { 1, 65, 133, 197 },
}

local function typeIcon(texture, recId)
	local rect = TYPE_RECT[MISSION_TYPE and MISSION_TYPE[recId] or 0]
	if not rect then texture:Hide() return end
	texture:SetTexture("retail/1000173.blp")
	texture:SetTexCoord(rect[1] / 512, rect[2] / 512, rect[3] / 512, rect[4] / 512)
	texture:Show()
end

local function portrait(texture, fileId, size)
	if fileId and fileId > 0 then
		texture:SetTexture("retail/" .. fileId .. ".blp")
		texture:SetTexCoord(0, 1, 0, 1)
		texture:SetSize(size, size)
		texture:Show()
	else
		texture:Hide()
	end
end

local function atlas(texture, name, w, h)
	if texture.SetAtlas and EsteriaArchitectAtlas and EsteriaArchitectAtlas[name] then
		texture:SetAtlas(name, false)
		if w then texture:SetSize(w, h or w) end
		texture:Show()
		return true
	end
	texture:Hide()
	return false
end

local function placeKey(location)
	return (location or ""):gsub("[^%a]", "")
end

-- retail/1048254.blp is one 1024 sheet. Each Draenor list banner is 792x78, stacked every 80 pixels.
-- Showing the whole sheet is what turned the rows into stripes.
local LIST_ROW = {
	FrostfireRidge = 0,
	Gorgrond = 80,
	SpiresofArak = 160,
	ShadowmoonValley = 240,
	Nagrand = 320,
	Talador = 400,
	TannanJungle = 480,
	BlackrockMountain = 560,
	Shazgul = 240,
}

local GARRISON_ART = "Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\"

local function ring(texture, kind)
	local name = "GarrMission_PortraitRing"
	if kind == "empty" then name = name .. "_Empty"
	elseif kind == "enemy" then name = name .. "_Enemy" end
	texture:SetTexture(GARRISON_ART .. name .. ".blp")
	texture:SetTexCoord(0, 1, 0, 1)
	texture:Show()
end

local function artCrop(texture, file, w, h, fileW, fileH)
	texture:SetTexture(GARRISON_ART .. file)
	texture:SetTexCoord(0, w / fileW, 0, h / fileH)
	texture:SetSize(w, h)
	texture:Show()
end

local function listBanner(texture, location)
	local y = LIST_ROW[placeKey(location)]
	if y == nil then return false end
	texture:SetTexture("retail/1048254.blp")
	texture:SetTexCoord(0, 792 / 1024, y / 1024, (y + 78) / 1024)
	texture:Show()
	return true
end

local function clock(seconds)
	seconds = tonumber(seconds) or 0
	if seconds <= 0 then return "0 min" end
	local hours = math.floor(seconds / 3600)
	local minutes = math.floor((seconds % 3600) / 60)
	if hours > 0 and minutes > 0 then return string.format("%d hr %d min", hours, minutes) end
	if hours > 0 then return string.format("%d hr", hours) end
	return string.format("%d min", minutes)
end

local CLASS_RECT = {
	["GarrMission_ClassIcon-DeathKnight"] = { 58, 113, 1, 50 },
	["GarrMission_ClassIcon-Druid"] = { 115, 170, 1, 50 },
	["GarrMission_ClassIcon-Hunter"] = { 1, 56, 52, 101 },
	["GarrMission_ClassIcon-Mage"] = { 1, 56, 103, 152 },
	["GarrMission_ClassIcon-Monk"] = { 1, 56, 154, 203 },
	["GarrMission_ClassIcon-Paladin"] = { 1, 56, 205, 254 },
	["GarrMission_ClassIcon-Priest"] = { 58, 113, 52, 101 },
	["GarrMission_ClassIcon-Rogue"] = { 115, 170, 52, 101 },
	["GarrMission_ClassIcon-Shaman"] = { 172, 227, 52, 101 },
	["GarrMission_ClassIcon-Warlock"] = { 58, 113, 154, 203 },
	["GarrMission_ClassIcon-Warrior"] = { 58, 113, 205, 254 },
}

local function classAtlas(spec)
	spec = spec or ""
	for name, icon in pairs(CLASS_ICON) do
		if string.find(spec, name, 1, true) then return icon end
	end
end

local function classMark(texture, spec)
	local rect = CLASS_RECT[classAtlas(spec) or ""]
	if not rect then texture:Hide() return end
	texture:SetTexture("retail/1000176.blp")
	texture:SetTexCoord(rect[1] / 256, rect[2] / 256, rect[3] / 256, rect[4] / 256)
	texture:SetAlpha(0.25)
	texture:Show()
end

-- retail/952656.blp is 512x1024. These two plates are 232x56.
local function followerPlate(texture, selected)
	texture:SetTexture("retail/952656.blp")
	if selected then
		texture:SetTexCoord(1 / 512, 233 / 512, 469 / 1024, 525 / 1024)
	else
		texture:SetTexCoord(218 / 512, 450 / 512, 806 / 1024, 862 / 1024)
	end
	texture:Show()
end

local function qualityColor(quality)
	return unpack(QUALITY[quality] or QUALITY[1])
end

local function parse(text)
	state = { followers = {}, missions = {}, abilities = {}, enemies = {}, party = {},
		resources = 0, result = 0, owned = false }
	local followerByDb = {}
	for line in string.gmatch(text or "", "[^\n]+") do
		local row = split(line, "\t")
		local kind = row[1]
		if kind == "R" then
			state.result = tonumber(row[2]) or 0
		elseif kind == "G" then
			state.owned = (tonumber(row[2]) or 0) == 1
			state.resources = tonumber(row[5]) or 0
		elseif kind == "F" then
			local follower = {
				db = tonumber(row[2]) or 0, id = tonumber(row[3]) or 0, level = tonumber(row[4]) or 0,
				quality = tonumber(row[5]) or 1, mission = tonumber(row[6]) or 0, name = row[7] or "",
				portrait = tonumber(row[8]) or 0, spec = row[9] or "", building = tonumber(row[10]) or 0,
				ilvl = tonumber(row[11]) or 0, weapon = tonumber(row[12]) or 0, armor = tonumber(row[13]) or 0,
				xp = tonumber(row[14]) or 0, nextXp = tonumber(row[15]) or 0, abilities = {},
			}
			state.followers[#state.followers + 1] = follower
			followerByDb[follower.db] = follower
		elseif kind == "A" then
			local db = tonumber(row[2]) or 0
			local follower = followerByDb[db]
			if follower then
				follower.abilities[#follower.abilities + 1] = {
					id = tonumber(row[3]) or 0, icon = tonumber(row[4]) or 0, name = row[5] or "",
					trait = (tonumber(row[6]) or 0) == 1, text = row[7] or "",
				}
			end
		elseif kind == "M" then
			state.missions[#state.missions + 1] = {
				db = tonumber(row[2]) or 0, id = tonumber(row[3]) or 0, st = tonumber(row[4]) or 0,
				left = tonumber(row[5]) or 0, name = row[7] or "", location = row[8] or "",
				description = row[9] or "", seconds = tonumber(row[10]) or 0, slots = tonumber(row[11]) or 0,
				cost = tonumber(row[12]) or 0, xp = tonumber(row[13]) or 0, level = tonumber(row[14]) or 0,
				kind = row[15] or "", kindIcon = tonumber(row[16]) or 0, chance = tonumber(row[17]) or 0,
			}
		elseif kind == "Y" then
			local db = tonumber(row[2]) or 0
			state.party[db] = state.party[db] or {}
			state.party[db][#state.party[db] + 1] = tonumber(row[3]) or 0
		elseif kind == "E" then
			local rec = tonumber(row[2]) or 0
			state.enemies[rec] = state.enemies[rec] or {}
			local name = row[3] or ""
			local enemy
			for _, have in ipairs(state.enemies[rec]) do
				if have.name == name then enemy = have end
			end
			if not enemy then
				enemy = { name = name, portrait = tonumber(row[4]) or 0, mechanics = {} }
				state.enemies[rec][#state.enemies[rec] + 1] = enemy
			end
			local mechanic = row[5] or ""
			if mechanic ~= "" then
				enemy.mechanics[#enemy.mechanics + 1] = {
					name = mechanic, icon = tonumber(row[6]) or 0, ability = tonumber(row[7]) or 0,
				}
			end
		end
	end
	table.sort(state.followers, function(a, b)
		if a.mission ~= b.mission then return a.mission == 0 end
		if a.level ~= b.level then return a.level > b.level end
		return a.name < b.name
	end)
end

local function followerByDb(db)
	for _, follower in ipairs(state.followers) do
		if follower.db == db then return follower end
	end
end

local function missionByDb(db)
	for _, mission in ipairs(state.missions) do
		if mission.db == db then return mission end
	end
end

local function partyOf(mission)
	return state.party[mission.db] or {}
end

local function seated(mission, db)
	for _, id in ipairs(partyOf(mission)) do
		if id == db then return true end
	end
end

local function busy(follower)
	return follower.mission ~= 0 or follower.building ~= 0
end

local function statusText(follower)
	if follower.mission ~= 0 then return "On Mission" end
	if follower.building ~= 0 then return "Working" end
	return ""
end

-- db2_garr_follower_level_xp / quality, follower type 1. Level 100 spends this on the next quality.
local LEVEL_XP = {
	[90] = 400, [91] = 800, [92] = 1200, [93] = 1600, [94] = 2000,
	[95] = 3000, [96] = 3500, [97] = 4000, [98] = 5400, [99] = 6000,
}
local QUALITY_XP = { [1] = 2000, [2] = 60000, [3] = 120000 }

local function nextFollowerXp(follower)
	if follower.nextXp and follower.nextXp > 0 then return follower.nextXp end
	if follower.level >= 100 then return QUALITY_XP[follower.quality] or 0 end
	return LEVEL_XP[follower.level] or 0
end

local function chanceOf(mission)
	local chance = mission.chance or 0
	local party = partyOf(mission)
	if #party == 0 then return chance end
	local levels = 0
	local countered = {}
	for _, db in ipairs(party) do
		local follower = followerByDb(db)
		if follower then
			levels = levels + follower.level
			for _, ability in ipairs(follower.abilities) do
				countered[ability.id] = true
			end
		end
	end
	local average = levels / #party
	if mission.level > average then
		chance = chance - math.floor(mission.level - average) * 10
	end
	local enemies = state.enemies[mission.id] or {}
	for _, enemy in ipairs(enemies) do
		for _, mechanic in ipairs(enemy.mechanics) do
			if mechanic.ability ~= 0 and countered[mechanic.ability] then
				chance = chance + 15
			end
		end
	end
	if chance < 0 then chance = 0 end
	if chance > 100 then chance = 100 end
	return chance
end

local function act(op, a, b)
	if EsteriaGarrison_Action then EsteriaGarrison_Action(op, a or 0, b or 0) end
end

local function take(parent, maker)
	parent.used = (parent.used or 0) + 1
	parent.pool = parent.pool or {}
	local child = parent.pool[parent.used]
	if not child then
		child = maker()
		parent.pool[parent.used] = child
	end
	child:Show()
	return child
end

local function hideRest(parent)
	for i = (parent.used or 0) + 1, #(parent.pool or {}) do parent.pool[i]:Hide() end
end

local function build()
	-- Same carved wood frame as the architect. A hand-built copy stretched the
	-- background tile into planks and put the corners on the wrong texels.
	frame = CreateFrame("Frame", "EsteriaGarrisonMissions", UIParent, "GarrisonUITemplate")
	frame:SetSize(FRAME_W, FRAME_H)
	frame:SetPoint("CENTER")
	frame:SetToplevel(true)
	frame:EnableMouse(true)
	frame:SetMovable(true)
	frame:SetClampedToScreen(true)
	frame:RegisterForDrag("LeftButton")
	frame:SetScript("OnDragStart", frame.StartMoving)
	frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
	frame:Hide()
	if frame.TitleText then
		frame.TitleText:ClearAllPoints()
		frame.TitleText:SetPoint("TOP", 0, -16)
		frame.TitleText:SetText("Garrison Missions")
	end

	local plaque = frame:CreateTexture(nil, "BORDER")
	plaque:SetTexture("Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\Garr_Building_MaterialFrame.blp")
	plaque:SetTexCoord(0, 0.570312, 0, 0.687500)
	plaque:SetSize(300, 43)
	plaque:SetPoint("BOTTOMLEFT", 28, 18)
	frame.resources = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	frame.resources:SetPoint("LEFT", plaque, "LEFT", 16, 2)
	frame.resourceIcon = frame:CreateTexture(nil, "ARTWORK")
	frame.resourceIcon:SetSize(18, 18)
	frame.resourceIcon:SetPoint("LEFT", frame.resources, "RIGHT", 4, 0)
	frame.resourceIcon:SetTexture("Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\GarrMission_CurrencyIcon-Material.blp")
	frame.resourceIcon:SetTexCoord(0, 0.9375, 0, 0.9375)
	frame.status = frame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	frame.status:SetPoint("BOTTOM", 0, 22)
	frame.status:SetTextColor(1, 0.25, 0.25)

	local function tabButton(text, x, id)
		local button = CreateFrame("Button", "EsteriaGarrisonMissionsTab" .. id, frame, "CharacterFrameTabButtonTemplate")
		button:SetPoint("BOTTOMLEFT", x, -28)
		button:SetText(text)
		button:SetID(id)
		button:SetScript("OnClick", function()
			tab = id
			if id == 1 then picked = 0 end
			frame:refresh()
		end)
		return button
	end
	frame.tab1 = tabButton("Missions", 36, 1)
	frame.tab2 = tabButton("Followers", 130, 2)
	PanelTemplates_SetNumTabs(frame, 2)
	frame.selectedTab = 1

	local list = CreateFrame("Frame", nil, frame)
	list:SetPoint("TOPLEFT", 28, -48)
	list:SetSize(300, 560)
	frame.list = list
	-- Retail draws the follower list as its own parchment window, with a vertical
	-- edge against the mission pane and a scrollbar on that edge.
	list.papers = {}
	local function layoutListPapers()
		local w, h = list:GetWidth(), list:GetHeight()
		if w < 40 or h < 40 then return end
		local i, y = 0, 0
		while y < h - 1 do
			local x, th = 0, math.min(256, h - y)
			while x < w - 1 do
				i = i + 1
				local tile = list.papers[i]
				if not tile then
					tile = list:CreateTexture(nil, "BACKGROUND")
					tile:SetTexture("retail/953549.blp")
					list.papers[i] = tile
				end
				local tw = math.min(256, w - x)
				tile:ClearAllPoints()
				tile:SetPoint("TOPLEFT", list, "TOPLEFT", x, -y)
				tile:SetSize(tw, th)
				tile:SetTexCoord(0, tw / 256, 0, th / 256)
				tile:Show()
				x = x + 256
			end
			y = y + 256
		end
		for j = i + 1, #list.papers do list.papers[j]:Hide() end
	end
	layoutListPapers()
	local function tileEdge(parent, point, xOff, flip)
		local tiles, h, y, i = {}, parent:GetHeight(), 0, 0
		if h < 40 then h = 540 end
		while y < h - 1 do
			i = i + 1
			local tile = tiles[i]
			if not tile then
				tile = parent:CreateTexture(nil, "BORDER")
				tile:SetTexture("Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\_GarrMission_Bg-Edge.blp")
				tiles[i] = tile
			end
			local th = math.min(256, h - y)
			tile:ClearAllPoints()
			tile:SetPoint(point, parent, point, xOff, -y)
			tile:SetSize(16, th)
			if flip then
				tile:SetTexCoord(20 / 64, 0, 0, th / 256)
			else
				tile:SetTexCoord(0, 20 / 64, 0, th / 256)
			end
			tile:Show()
			y = y + 256
		end
	end
	tileEdge(list, "TOPRIGHT", 6, true)
	list.track = list:CreateTexture(nil, "ARTWORK")
	list.track:SetWidth(8)
	list.track:SetPoint("TOPRIGHT", -4, -32)
	list.track:SetPoint("BOTTOMRIGHT", -4, 8)
	list.track:SetTexture(0, 0, 0, 0.55)
	list.knob = list:CreateTexture(nil, "OVERLAY")
	list.knob:SetSize(8, 48)
	list.knob:SetPoint("TOPRIGHT", list.track, "TOPRIGHT", 0, 0)
	list.knob:SetTexture(0.62, 0.5, 0.3, 0.95)
	local search = CreateFrame("EditBox", nil, list)
	search:SetSize(280, 22)
	search:SetPoint("TOPLEFT", 8, -4)
	search:SetAutoFocus(false)
	search:SetFontObject(GameFontHighlightSmall)
	search:SetTextInsets(22, 6, 0, 0)
	search:SetBackdrop({
		bgFile = "Interface\\ChatFrame\\ChatFrameBackground",
		edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border",
		edgeSize = 12, insets = { left = 3, right = 3, top = 3, bottom = 3 },
	})
	search:SetBackdropColor(0, 0, 0, 0.55)
	search:SetScript("OnEscapePressed", search.ClearFocus)
	search:SetScript("OnEnterPressed", search.ClearFocus)
	search:SetScript("OnTextChanged", function() frame:refresh() end)
	search.hint = search:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
	search.hint:SetPoint("LEFT", 22, 0)
	search.hint:SetText("Search")
	search:SetScript("OnEditFocusGained", function() search.hint:Hide() end)
	search:SetScript("OnEditFocusLost", function()
		if search:GetText() == "" then search.hint:Show() end
	end)
	frame.search = search

	list.rows = CreateFrame("Frame", nil, list)
	list.rows:SetPoint("TOPLEFT", 0, -32)
	list.rows:SetSize(300, 520)
	list.rows:EnableMouseWheel(true)
	list.rows:SetScript("OnMouseWheel", function(_, delta)
		followerScroll = math.max(0, followerScroll - delta)
		frame:refresh()
	end)

	local stage = CreateFrame("Frame", nil, frame)
	-- Stay inside the 56px wood corners. The corner art is drawn by the frame template.
	stage:SetPoint("TOPLEFT", 336, -58)
	stage:SetPoint("BOTTOMRIGHT", -44, 64)
	frame.stage = stage
	-- Back, mid and fore are three bands of one 1024 sheet. They share one rectangle; the
	-- transparent top of each lower band lets the sky show through.
	stage.art = stage:CreateTexture(nil, "BORDER")
	stage.art:SetPoint("TOPLEFT", 6, -6)
	stage.art:SetPoint("TOPRIGHT", -6, -6)
	stage.art:SetHeight(250)
	stage.mid = stage:CreateTexture(nil, "ARTWORK")
	stage.mid:SetPoint("TOPLEFT", stage.art, "TOPLEFT")
	stage.mid:SetPoint("BOTTOMRIGHT", stage.art, "BOTTOMRIGHT")
	stage.mid:Hide()
	stage.ground = stage:CreateTexture(nil, "ARTWORK")
	stage.ground:SetPoint("TOPLEFT", stage.art, "TOPLEFT")
	stage.ground:SetPoint("BOTTOMRIGHT", stage.art, "BOTTOMRIGHT")
	stage.ground:Hide()
	-- Parchment tiles stay inside the stage. A tile past the edge draws outside the wood frame.
	stage.papers = {}
	local function layoutPapers()
		local w, h = stage:GetWidth(), stage:GetHeight()
		if w < 40 or h < 40 then return end
		local i, y = 0, 0
		while y < h - 1 do
			local x, th = 0, math.min(256, h - y)
			while x < w - 1 do
				i = i + 1
				local tile = stage.papers[i]
				if not tile then
					tile = stage:CreateTexture(nil, "BACKGROUND")
					tile:SetTexture("retail/953549.blp")
					stage.papers[i] = tile
				end
				local tw = math.min(256, w - x)
				tile:ClearAllPoints()
				tile:SetPoint("TOPLEFT", stage, "TOPLEFT", x, -y)
				tile:SetSize(tw, th)
				tile:SetTexCoord(0, tw / 256, 0, th / 256)
				tile:Show()
				x = x + 256
			end
			y = y + 256
		end
		for j = i + 1, #stage.papers do stage.papers[j]:Hide() end
		-- Left edge of the mission window, facing the follower list.
		if not stage.edge then stage.edge = {} end
		local ey, ei = 0, 0
		while ey < h - 1 do
			ei = ei + 1
			local edge = stage.edge[ei]
			if not edge then
				edge = stage:CreateTexture(nil, "BORDER")
				edge:SetTexture("Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\_GarrMission_Bg-Edge.blp")
				stage.edge[ei] = edge
			end
			local eth = math.min(256, h - ey)
			edge:ClearAllPoints()
			edge:SetPoint("TOPLEFT", stage, "TOPLEFT", -6, -ey)
			edge:SetSize(16, eth)
			edge:SetTexCoord(0, 20 / 64, 0, eth / 256)
			edge:Show()
			ey = ey + 256
		end
		for j = ei + 1, #stage.edge do stage.edge[j]:Hide() end
	end
	stage:SetScript("OnSizeChanged", layoutPapers)
	stage.layoutPapers = layoutPapers
	-- Retail GarrisonMissionPageStageTemplate: a 66px black wash, the level centered
	-- on the left, the type icon just right of it, title and location left-aligned after that.
	stage.header = stage:CreateTexture(nil, "ARTWORK")
	stage.header:SetTexture(0, 0, 0, 0.35)
	stage.header:SetPoint("TOPLEFT", stage.art, "TOPLEFT")
	stage.header:SetPoint("TOPRIGHT", stage.art, "TOPRIGHT")
	stage.header:SetHeight(66)
	stage.iconBg = stage:CreateTexture(nil, "ARTWORK")
	stage.iconBg:SetTexture("retail/952656.blp")
	stage.iconBg:SetTexCoord(81 / 512, 201 / 512, 943 / 1024, 1008 / 1024)
	stage.iconBg:SetSize(120, 65)
	stage.iconBg:SetPoint("TOPLEFT", stage.header, "TOPLEFT", 0, -1)
	stage.iconBg:SetVertexColor(0, 0, 0, 0.4)
	local headerFont = GameFontNormal:GetFont()
	stage.level = stage:CreateFontString(nil, "OVERLAY")
	stage.level:SetFont(headerFont, 24)
	stage.level:SetShadowOffset(1, -1)
	stage.level:SetPoint("CENTER", stage.header, "TOPLEFT", 30, -36)
	stage.level:SetTextColor(0.84, 0.72, 0.57)
	stage.typeMark = stage:CreateTexture(nil, "OVERLAY")
	stage.typeMark:SetSize(64, 64)
	stage.typeMark:SetPoint("TOPLEFT", stage.header, "TOPLEFT", 48, -4)
	stage.title = stage:CreateFontString(nil, "OVERLAY")
	stage.title:SetFont("Fonts\\MORPHEUS.TTF", 24)
	stage.title:SetShadowOffset(1, -1)
	stage.title:SetJustifyH("LEFT")
	stage.title:SetPoint("LEFT", stage.header, "LEFT", 125, 10)
	stage.title:SetTextColor(1, 1, 1)
	stage.location = stage:CreateFontString(nil, "OVERLAY")
	stage.location:SetFont("Fonts\\MORPHEUS.TTF", 18)
	stage.location:SetShadowOffset(1, -1)
	stage.location:SetJustifyH("LEFT")
	stage.location:SetPoint("TOPLEFT", stage.title, "BOTTOMLEFT", 0, -6)
	stage.location:SetTextColor(1, 1, 1)
	stage.time = stage:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	stage.time:SetPoint("TOPLEFT", stage.header, "BOTTOMLEFT", 7, -7)
	stage.kind = stage:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	stage.kind:SetPoint("TOPLEFT", stage.time, "BOTTOMLEFT", 0, -3)
	stage.kindIcon = stage:CreateTexture(nil, "ARTWORK")
	stage.kindIcon:SetSize(16, 16)
	stage.kindIcon:SetPoint("LEFT", stage.kind, "RIGHT", 4, 0)
	stage.description = stage:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	stage.description:SetPoint("TOPRIGHT", stage.header, "BOTTOMRIGHT", -15, -7)
	stage.description:SetWidth(260)
	stage.description:SetJustifyH("LEFT")
	stage.description:SetJustifyV("TOP")
	stage.enemies = {}
	for i = 1, 3 do
		local enemy = CreateFrame("Frame", nil, stage)
		enemy:SetSize(53, 53)
		enemy.name = enemy:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
		enemy.name:SetWidth(161)
		enemy.name:SetJustifyH("CENTER")
		enemy.name:SetPoint("BOTTOM", enemy, "TOP", 0, 4)
		enemy.face = enemy:CreateTexture(nil, "BACKGROUND")
		enemy.face:SetSize(50, 50)
		enemy.face:SetPoint("CENTER")
		enemy.ring = enemy:CreateTexture(nil, "BORDER")
		enemy.ring:SetPoint("CENTER")
		artCrop(enemy.ring, "GarrMission_EncounterBar-PortraitRing.blp", 64, 61, 64, 64)
		enemy.elite = enemy:CreateTexture(nil, "OVERLAY")
		enemy.elite:SetPoint("CENTER")
		artCrop(enemy.elite, "GarrMission_EncounterBar-Elite.blp", 75, 67, 128, 128)
		enemy.elite:Hide()
		enemy.mechanics = {}
		for m = 1, 4 do
			local mechanic = CreateFrame("Frame", nil, enemy)
			mechanic:SetSize(28, 28)
			mechanic.icon = mechanic:CreateTexture(nil, "ARTWORK")
			mechanic.icon:SetPoint("CENTER")
			mechanic.border = mechanic:CreateTexture(nil, "OVERLAY")
			mechanic.border:SetPoint("CENTER")
			artCrop(mechanic.border, "GarrMission_EncounterAbilityBorder-Lg.blp", 46, 46, 64, 64)
			mechanic:Hide()
			enemy.mechanics[m] = mechanic
		end
		enemy:Hide()
		stage.enemies[i] = enemy
	end
	stage.hint = stage:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	stage.hint:SetText("Drag Followers here to form a Party")
	stage.slots = {}
	for i = 1, 3 do
		local slot = CreateFrame("Button", nil, stage)
		slot:SetSize(161, 58)
		slot.box = slot:CreateTexture(nil, "BACKGROUND")
		slot.box:SetPoint("RIGHT", 13, 0)
		artCrop(slot.box, "GarrMission_FollowerPartyBox_Sm.blp", 169, 72, 256, 128)
		slot.ring = slot:CreateTexture(nil, "BORDER")
		slot.ring:SetSize(59, 62)
		slot.ring:SetPoint("LEFT")
		ring(slot.ring, nil)
		slot.empty = slot:CreateTexture(nil, "ARTWORK")
		slot.empty:SetSize(59, 62)
		slot.empty:SetPoint("CENTER", slot.ring, "CENTER")
		ring(slot.empty, "empty")
		slot.face = slot:CreateTexture(nil, "BACKGROUND")
		slot.face:SetSize(44, 44)
		slot.face:SetPoint("CENTER", slot.ring, "CENTER", 0, 5)
		slot.glow = slot:CreateTexture(nil, "OVERLAY")
		slot.glow:SetSize(59, 62)
		slot.glow:SetPoint("CENTER", slot.ring, "CENTER", 0, -1)
		slot.glow:SetTexture(GARRISON_ART .. "GarrMission_PortraitRing_Glow.blp")
		slot.glow:SetBlendMode("ADD")
		slot.glow:SetAlpha(0.85)
		slot.glow:Hide()
		slot:SetScript("OnClick", function(self)
			local mission = missionByDb(picked)
			if not mission or not self.db or mission.st ~= 0 then return end
			act(13, mission.db, self.db)
		end)
		stage.slots[i] = slot
	end
	stage.costBg = stage:CreateTexture(nil, "ARTWORK")
	stage.costBg:SetTexture("Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\GarrMission_MissionCostStartButtonBg.blp")
	stage.costBg:SetTexCoord(0, 376 / 512, 0, 51 / 64)
	stage.costBg:SetSize(376, 51)
	stage.costBg:SetPoint("BOTTOM", 0, 4)
	local gart = "Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\"
	local rewards = CreateFrame("Frame", nil, stage)
	rewards:SetSize(360, 52)
	rewards:SetPoint("BOTTOM", stage.costBg, "TOP", 28, 18)
	rewards.bg = rewards:CreateTexture(nil, "BACKGROUND")
	rewards.bg:SetTexture(gart .. "GarrMission_RewardsBG-Desaturate.blp")
	rewards.bg:SetTexCoord(0, 502 / 512, 0, 154 / 256)
	rewards.bg:SetPoint("TOPLEFT", 8, -6)
	rewards.bg:SetPoint("BOTTOMRIGHT", -8, 6)
	local function rewardEdge(file, w, h, point, x, y, a, b, c, d)
		local tex = rewards:CreateTexture(nil, "BORDER")
		tex:SetTexture(gart .. file)
		tex:SetTexCoord(a, b, c, d)
		tex:SetSize(w, h)
		tex:SetPoint(point, x, y)
	end
	rewardEdge("GarrMission_RewardsBorder-Corner-Desaturate.blp", 42, 42, "TOPLEFT", -10, 12, 0, 1, 0, 1)
	rewardEdge("GarrMission_RewardsBorder-Corner-Desaturate.blp", 42, 42, "TOPRIGHT", 10, 12, 1, 0, 0, 1)
	rewardEdge("GarrMission_RewardsBorder-Corner-Desaturate.blp", 42, 42, "BOTTOMLEFT", -10, -12, 0, 1, 1, 0)
	rewardEdge("GarrMission_RewardsBorder-Corner-Desaturate.blp", 42, 42, "BOTTOMRIGHT", 10, -12, 1, 0, 1, 0)
	local topEdge = rewards:CreateTexture(nil, "BORDER")
	topEdge:SetTexture(gart .. "_GarrMission_RewardsBorder-Top-Desaturate.blp")
	topEdge:SetTexCoord(0, 1, 8 / 32, 18 / 32)
	topEdge:SetPoint("TOPLEFT", 30, 8)
	topEdge:SetPoint("TOPRIGHT", -30, 8)
	topEdge:SetHeight(12)
	local bottomEdge = rewards:CreateTexture(nil, "BORDER")
	bottomEdge:SetTexture(gart .. "_GarrMission_RewardsBorder-Top-Desaturate.blp")
	bottomEdge:SetTexCoord(0, 1, 18 / 32, 8 / 32)
	bottomEdge:SetPoint("BOTTOMLEFT", 30, -8)
	bottomEdge:SetPoint("BOTTOMRIGHT", -30, -8)
	bottomEdge:SetHeight(12)
	local leftEdge = rewards:CreateTexture(nil, "BORDER")
	leftEdge:SetTexture(gart .. "_GarrMission_RewardsBorder-Left-Desaturate.blp")
	leftEdge:SetTexCoord(8 / 32, 18 / 32, 0, 1)
	leftEdge:SetPoint("TOPLEFT", -6, -22)
	leftEdge:SetPoint("BOTTOMLEFT", -6, 22)
	leftEdge:SetWidth(12)
	local rightEdge = rewards:CreateTexture(nil, "BORDER")
	rightEdge:SetTexture(gart .. "_GarrMission_RewardsBorder-Left-Desaturate.blp")
	rightEdge:SetTexCoord(18 / 32, 8 / 32, 0, 1)
	rightEdge:SetPoint("TOPRIGHT", 6, -22)
	rightEdge:SetPoint("BOTTOMRIGHT", 6, 22)
	rightEdge:SetWidth(12)
	local chestFrame = CreateFrame("Frame", nil, stage)
	chestFrame:SetSize(156, 116)
	chestFrame:SetPoint("RIGHT", rewards, "LEFT", 78, 10)
	chestFrame:SetFrameLevel(rewards:GetFrameLevel() + 5)
	stage.chest = chestFrame:CreateTexture(nil, "ARTWORK")
	stage.chest:SetTexture(gart .. "GarrMission-AllianceChest.blp")
	stage.chest:SetTexCoord(0, 209 / 256, 0, 155 / 256)
	stage.chest:SetAllPoints()
	stage.rewardIcon = rewards:CreateTexture(nil, "ARTWORK")
	stage.rewardIcon:SetTexture(gart .. "GarrMission_CurrencyIcon-Xp.blp")
	stage.rewardIcon:SetTexCoord(0, 60 / 64, 0, 60 / 64)
	stage.rewardIcon:SetSize(32, 32)
	stage.rewardIcon:SetPoint("CENTER", rewards, "CENTER", 8, 0)
	stage.rewardBorder = rewards:CreateTexture(nil, "OVERLAY")
	stage.rewardBorder:SetTexture("Interface\\Buttons\\UI-Quickslot2")
	stage.rewardBorder:SetSize(46, 46)
	stage.rewardBorder:SetPoint("CENTER", stage.rewardIcon, "CENTER", 0, 0)
	stage.rewardText = rewards:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	stage.rewardText:SetPoint("LEFT", stage.rewardIcon, "RIGHT", 10, 0)
	stage.xp = stage:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	stage.xp:SetPoint("BOTTOM", rewards, "TOP", -8, 14)
	stage.chance = chestFrame:CreateFontString(nil, "OVERLAY", "ZoneTextFont")
	stage.chance:SetPoint("CENTER", 0, 6)
	stage.chanceLabel = chestFrame:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	stage.chanceLabel:SetPoint("TOP", stage.chance, "BOTTOM", 0, -8)
	stage.chanceLabel:SetText("Chance")
	stage.costLabel = stage:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	stage.costLabel:SetPoint("LEFT", stage.costBg, "LEFT", 64, 1)
	stage.costLabel:SetText("Cost:")
	stage.cost = stage:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	stage.cost:SetPoint("LEFT", stage.costLabel, "RIGHT", 8, 0)
	stage.costIcon = stage:CreateTexture(nil, "OVERLAY")
	stage.costIcon:SetSize(28, 28)
	stage.costIcon:SetPoint("LEFT", stage.cost, "RIGHT", 2, 1)
	stage.costIcon:SetTexture("Interface\\AddOns\\Blizzard_GarrisonUI\\Art\\GarrMission_CurrencyIcon-Material.blp")
	stage.costIcon:SetTexCoord(0, 0.9375, 0, 0.9375)
	stage.start = CreateFrame("Button", nil, stage, "UIPanelButtonTemplate")
	stage.start:SetSize(120, 22)
	stage.start:SetPoint("LEFT", stage.costIcon, "RIGHT", 16, -1)
	stage.start:SetText("Start Mission")
	stage.back = CreateFrame("Button", nil, stage, "UIPanelCloseButton")
	stage.back:SetPoint("TOPRIGHT", stage.header, "TOPRIGHT", 4, 4)
	stage.back:SetScript("OnClick", function() picked = 0 frame:refresh() end)

	local board = CreateFrame("Frame", nil, frame)
	board:SetPoint("TOPLEFT", stage, "TOPLEFT")
	board:SetPoint("BOTTOMRIGHT", stage, "BOTTOMRIGHT")
	board:EnableMouseWheel(true)
	board:SetScript("OnMouseWheel", function(_, delta)
		missionScroll = math.max(0, missionScroll - delta)
		frame:refresh()
	end)
	frame.board = board

	local detail = CreateFrame("Frame", nil, frame)
	detail:SetPoint("TOPLEFT", stage, "TOPLEFT", 8, -28)
	detail:SetPoint("BOTTOMRIGHT", stage, "BOTTOMRIGHT", -8, 8)
	detail.count = detail:CreateFontString(nil, "OVERLAY", "GameFontNormal")
	detail.count:SetPoint("BOTTOMLEFT", detail, "TOPLEFT", 8, 6)
	detail.ring = detail:CreateTexture(nil, "ARTWORK")
	detail.ring:SetSize(59, 62)
	detail.ring:SetPoint("TOPLEFT", 8, -8)
	ring(detail.ring)
	detail.face = detail:CreateTexture(nil, "BORDER")
	detail.face:SetSize(44, 44)
	detail.face:SetPoint("CENTER", detail.ring, "CENTER", 0, 5)
	detail.quality = detail:CreateTexture(nil, "OVERLAY")
	detail.quality:SetSize(59, 62)
	detail.quality:SetPoint("TOPLEFT", detail.ring, "TOPLEFT")
	detail.quality:SetTexture(GARRISON_ART .. "GarrMission_PortraitRing_Quality.blp")
	detail.quality:SetTexCoord(0, 1, 0, 1)
	detail.levelBorder = detail:CreateTexture(nil, "OVERLAY")
	detail.levelBorder:SetSize(58, 24)
	detail.levelBorder:SetPoint("BOTTOM", detail.ring, "BOTTOM", 0, -3)
	detail.levelBorder:SetTexture(GARRISON_ART .. "GarrMission_PortraitRing_LevelBorder.blp")
	detail.levelBorder:SetTexCoord(0, 1, 0, 1)
	detail.level = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	detail.level:SetPoint("CENTER", detail.levelBorder, "CENTER", 0, 1)
	local nameFont = GameFontNormal:GetFont()
	detail.name = detail:CreateFontString(nil, "OVERLAY")
	detail.name:SetFont(nameFont, 18)
	detail.name:SetTextColor(1, 1, 1)
	detail.name:SetPoint("TOPLEFT", 78, -14)
	detail.name:SetJustifyH("LEFT")
	detail.spec = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	detail.spec:SetPoint("TOPLEFT", detail.name, "BOTTOMLEFT", 0, -4)
	detail.class = detail:CreateTexture(nil, "ARTWORK")
	detail.class:SetSize(55, 49)
	detail.class:SetPoint("TOPRIGHT", -8, -8)
	detail.xpText = detail:CreateFontString(nil, "OVERLAY")
	detail.xpText:SetFont(nameFont, 16)
	detail.xpText:SetTextColor(1, 1, 1)
	detail.xpText:SetPoint("TOPRIGHT", detail.class, "TOPLEFT", -6, -2)
	detail.xpLabel = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
	detail.xpLabel:SetPoint("TOPRIGHT", detail.xpText, "BOTTOMRIGHT", 0, -2)
	detail.xpBg = detail:CreateTexture(nil, "ARTWORK")
	detail.xpBg:SetHeight(9)
	detail.xpBg:SetPoint("TOPLEFT", detail.spec, "BOTTOMLEFT", 0, -8)
	detail.xpBg:SetPoint("RIGHT", detail, "RIGHT", -16, 0)
	detail.xpBg:SetTexture(0.12, 0.05, 0.16, 1)
	detail.xpFill = detail:CreateTexture(nil, "OVERLAY")
	detail.xpFill:SetHeight(7)
	detail.xpFill:SetPoint("LEFT", detail.xpBg, "LEFT", 1, 0)
	detail.xpFill:SetTexture(0.482, 0.098, 0.706, 1)
	detail.xpLine = detail:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
	detail.xpLine:SetPoint("CENTER", detail.xpBg, "CENTER")
	detail.abilityLabel = detail:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	detail.abilityLabel:SetJustifyH("LEFT")
	detail.traitLabel = detail:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	detail.traitLabel:SetJustifyH("LEFT")
	detail.rows = {}
	for i = 1, 8 do
		local row = CreateFrame("Frame", nil, detail)
		row:SetSize(250, 46)
		row.icon = row:CreateTexture(nil, "ARTWORK")
		row.icon:SetSize(35, 35)
		row.icon:SetPoint("LEFT")
		row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
		row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, -1)
		row.name:SetWidth(200)
		row.name:SetJustifyH("LEFT")
		row.text = row:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		row.text:SetPoint("TOPLEFT", row.name, "BOTTOMLEFT", 0, -2)
		row.text:SetWidth(200)
		row.text:SetJustifyH("LEFT")
		row:Hide()
		detail.rows[i] = row
	end
	local function gearRow()
		local row = CreateFrame("Frame", nil, detail)
		row:SetSize(225, 42)
		row.plate = row:CreateTexture(nil, "BORDER")
		row.plate:SetSize(181, 41)
		row.plate:SetPoint("LEFT", 44, 0)
		row.plate:SetTexture("retail/952656.blp")
		row.plate:SetTexCoord(303 / 512, 484 / 512, 282 / 1024, 323 / 1024)
		row.name = row:CreateFontString(nil, "OVERLAY", "GameFontNormal")
		row.name:SetPoint("TOPLEFT", row.plate, "TOPLEFT", 8, -6)
		row.name:SetJustifyH("LEFT")
		row.level = row:CreateFontString(nil, "OVERLAY", "GameFontHighlight")
		row.level:SetPoint("BOTTOMLEFT", row.plate, "BOTTOMLEFT", 10, 5)
		row.level:SetJustifyH("LEFT")
		row:Hide()
		return row
	end
	detail.weapon = gearRow()
	detail.weapon:SetPoint("BOTTOMLEFT", 16, 56)
	detail.armor = gearRow()
	detail.armor:SetPoint("TOPLEFT", detail.weapon, "BOTTOMLEFT", 0, -6)
	detail.ilvl = detail:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
	detail.ilvl:SetPoint("BOTTOM", detail.weapon, "TOP", 22, 4)
	frame.detail = detail

	local function followerButton()
		local button = CreateFrame("Button", nil, list.rows)
		button:SetSize(260, 56)
		button.bg = button:CreateTexture(nil, "BACKGROUND")
		button.bg:SetSize(232, 56)
		button.bg:SetPoint("TOPRIGHT")
		button.select = button:CreateTexture(nil, "BORDER")
		button.select:SetSize(232, 56)
		button.select:SetPoint("TOPRIGHT")
		button.select:Hide()
		button.ring = button:CreateTexture(nil, "ARTWORK")
		button.ring:SetSize(59, 62)
		button.ring:SetPoint("TOPLEFT", 4, 2)
		ring(button.ring)
		button.face = button:CreateTexture(nil, "BORDER")
		button.face:SetSize(44, 44)
		button.face:SetPoint("CENTER", button.ring, "CENTER", 0, 5)
		button.quality = button:CreateTexture(nil, "OVERLAY")
		button.quality:SetSize(59, 62)
		button.quality:SetPoint("TOPLEFT", button.ring, "TOPLEFT")
		button.quality:SetTexture(GARRISON_ART .. "GarrMission_PortraitRing_Quality.blp")
		button.quality:SetTexCoord(0, 1, 0, 1)
		button.levelBorder = button:CreateTexture(nil, "OVERLAY")
		button.levelBorder:SetSize(58, 24)
		button.levelBorder:SetPoint("BOTTOM", button.ring, "BOTTOM", 0, -3)
		button.levelBorder:SetTexture(GARRISON_ART .. "GarrMission_PortraitRing_LevelBorder.blp")
		button.levelBorder:SetTexCoord(0, 1, 0, 1)
		button.level = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		button.level:SetPoint("CENTER", button.levelBorder, "CENTER", 0, 1)
		button.name = button:CreateFontString(nil, "OVERLAY", "GameFontNormal")
		button.name:SetPoint("LEFT", 66, 8)
		button.name:SetWidth(140)
		button.name:SetJustifyH("LEFT")
		button.info = button:CreateFontString(nil, "OVERLAY", "GameFontDisableSmall")
		button.info:SetPoint("TOPLEFT", button.name, "BOTTOMLEFT", 0, -2)
		button.info:SetJustifyH("LEFT")
		button.status = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		button.status:SetPoint("LEFT", button.info, "RIGHT", 6, 0)
		button.status:SetTextColor(0.698, 0.941, 1)
		button.xpTrack = button:CreateTexture(nil, "ARTWORK")
		button.xpTrack:SetSize(205, 4)
		button.xpTrack:SetPoint("TOPLEFT", button.bg, "BOTTOMLEFT", 24, 6)
		button.xpTrack:SetTexture(0, 0, 0, 0.65)
		button.xp = button:CreateTexture(nil, "OVERLAY")
		button.xp:SetHeight(4)
		button.xp:SetPoint("LEFT", button.xpTrack, "LEFT")
		button.xp:SetTexture(0.212, 0, 0.337, 1)
		button.xp:Hide()
		button.class = button:CreateTexture(nil, "ARTWORK")
		button.class:SetSize(48, 42)
		button.class:SetPoint("TOPRIGHT", -2, -4)
		button.counters = {}
		for i = 1, 4 do
			local counter = CreateFrame("Frame", nil, button)
			counter:SetSize(22, 22)
			counter:EnableMouse(true)
			counter.icon = counter:CreateTexture(nil, "ARTWORK")
			counter.icon:SetSize(18, 18)
			counter.icon:SetPoint("CENTER")
			counter.border = counter:CreateTexture(nil, "OVERLAY")
			counter.border:SetAllPoints()
			counter.border:SetTexture(GARRISON_ART .. "GarrMission_EncounterAbilityBorder.blp")
			counter:SetScript("OnEnter", function(self)
				if not self.tip or self.tip == "" then return end
				GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
				GameTooltip:SetText(self.tip, 1, 1, 1)
				GameTooltip:Show()
			end)
			counter:SetScript("OnLeave", function() GameTooltip:Hide() end)
			counter:Hide()
			button.counters[i] = counter
		end
		button:RegisterForDrag("LeftButton")
		button:SetScript("OnClick", function(self)
			if tab ~= 1 then
				frame.shownFollower = self.follower and self.follower.db or 0
				frame:refresh()
				return
			end
			local mission = missionByDb(picked)
			if not mission or mission.st ~= 0 or not self.follower or busy(self.follower) then return end
			if seated(mission, self.follower.db) then
				act(13, mission.db, self.follower.db)
			else
				act(12, mission.db, self.follower.db)
			end
		end)
		button:SetScript("OnDragStart", function(self)
			if self.follower and not busy(self.follower) then
				frame.drag = self.follower.db
			end
		end)
		button:SetScript("OnDragStop", function() frame.drag = nil end)
		return button
	end

	for i = 1, 3 do
		local slot = stage.slots[i]
		slot:SetScript("OnReceiveDrag", function(self)
			local mission = missionByDb(picked)
			if not mission or mission.st ~= 0 or not frame.drag then return end
			if not self.db then act(12, mission.db, frame.drag) end
			frame.drag = nil
		end)
	end
	local function missionButton()
		local button = CreateFrame("Button", nil, board)
		button:SetSize(600, 76)
		button.banner = button:CreateTexture(nil, "BACKGROUND")
		button.banner:SetAllPoints()
		button.iconBg = button:CreateTexture(nil, "BORDER")
		button.iconBg:SetTexture("retail/952656.blp")
		button.iconBg:SetTexCoord(324 / 512, 474 / 512, 200 / 1024, 276 / 1024)
		button.iconBg:SetSize(150, 76)
		button.iconBg:SetPoint("TOPLEFT")
		button.iconBg:SetVertexColor(0, 0, 0, 0.4)
		local levelFont = GameFontNormal:GetFont()
		button.level = button:CreateFontString(nil, "OVERLAY")
		button.level:SetFont(levelFont, 22)
		button.level:SetTextColor(0.84, 0.72, 0.57)
		button.level:SetPoint("CENTER", button, "TOPLEFT", 34, -38)
		button.duration = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		button.duration:SetPoint("TOPLEFT", 66, -4)
		button.typeMark = button:CreateTexture(nil, "OVERLAY")
		button.typeMark:SetSize(36, 36)
		button.typeMark:SetPoint("TOPLEFT", 66, -18)
		button.pips = {}
		for i = 1, 3 do
			local pip = button:CreateTexture(nil, "OVERLAY")
			pip:SetSize(14, 14)
			pip:SetPoint("TOPLEFT", 66 + (i - 1) * 16, -56)
			ring(pip, "empty")
			pip:Hide()
			button.pips[i] = pip
		end
		button.name = button:CreateFontString(nil, "OVERLAY", "GameFontNormalLarge")
		button.name:SetPoint("LEFT", 158, 8)
		button.name:SetJustifyH("LEFT")
		button.where = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		button.where:SetPoint("TOPLEFT", button.name, "BOTTOMLEFT", 0, -2)
		button.meta = button:CreateFontString(nil, "OVERLAY", "GameFontHighlightSmall")
		button.meta:SetPoint("RIGHT", -16, 0)
		button.reward = CreateFrame("Frame", nil, button)
		button.reward:SetSize(64, 64)
		button.reward:SetPoint("RIGHT", -4, 0)
		button.reward:EnableMouse(true)
		button.reward.icon = button.reward:CreateTexture(nil, "ARTWORK")
		button.reward.icon:SetAllPoints()
		button.reward.qty = button.reward:CreateFontString(nil, "OVERLAY")
		button.reward.qty:SetFont(GameFontHighlight:GetFont(), 15, "OUTLINE")
		button.reward.qty:SetTextColor(1, 1, 1)
		button.reward.qty:SetJustifyH("RIGHT")
		button.reward.qty:SetPoint("BOTTOMRIGHT", -2, 3)
		button.reward:SetScript("OnEnter", function(self)
			if not self.tip or self.tip == "" then return end
			GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
			GameTooltip:SetText(self.tip, 1, 1, 1)
			GameTooltip:Show()
		end)
		button.reward:SetScript("OnLeave", function() GameTooltip:Hide() end)
		button.reward:Hide()
		button.threats = {}
		for i = 1, 6 do
			local threat = CreateFrame("Frame", nil, button)
			threat:SetSize(22, 22)
			threat:EnableMouse(true)
			threat.icon = threat:CreateTexture(nil, "ARTWORK")
			threat.icon:SetSize(16, 16)
			threat.icon:SetPoint("CENTER")
			threat.border = threat:CreateTexture(nil, "OVERLAY")
			threat.border:SetAllPoints()
			threat.border:SetTexture(GARRISON_ART .. "GarrMission_EncounterAbilityBorder.blp")
			threat:SetScript("OnEnter", function(self)
				if not self.tip or self.tip == "" then return end
				GameTooltip:SetOwner(self, "ANCHOR_RIGHT")
				GameTooltip:SetText(self.tip, 1, 1, 1)
				GameTooltip:Show()
			end)
			threat:SetScript("OnLeave", function() GameTooltip:Hide() end)
			threat:Hide()
			button.threats[i] = threat
		end
		button:SetScript("OnClick", function(self)
			picked = self.mission and self.mission.db or 0
			frame:refresh()
		end)
		return button
	end

	frame.followerButton = followerButton
	frame.missionButton = missionButton

	local function showListMark(button, row)
		if not row then
			button.iconBg:Hide()
			button.duration:SetText("")
			button.typeMark:Hide()
			for i = 1, 3 do button.pips[i]:Hide() end
			return
		end
		button.iconBg:Show()
		local text = clock(row.seconds)
		if row.st == 1 then
			text = row.left > 0 and clock(row.left) or "Complete"
			button.duration:SetTextColor(1, 0.2, 0.2)
		elseif row.seconds >= 8 * 3600 then
			text = "|cffff7d1a" .. text .. "|r"
			button.duration:SetTextColor(1, 1, 1)
		else
			button.duration:SetTextColor(1, 1, 1)
		end
		button.duration:SetText(text)
		typeIcon(button.typeMark, row.id)
		local slots = row.slots
		if slots < 0 then slots = 0 end
		if slots > 3 then slots = 3 end
		for i = 1, 3 do
			if i <= slots then button.pips[i]:Show() else button.pips[i]:Hide() end
		end
	end

	local function showListReward(button, recId)
		local rewards = MISSION_REWARD and MISSION_REWARD[recId]
		local reward = rewards and rewards[1]
		if not reward then
			button.reward:Hide()
			button.reward.tip = nil
			return
		end
		local kind, icon, amount, name = reward[1], reward[2], reward[3], reward[4]
		if kind == "gold" then
			portrait(button.reward.icon, 133784, 64)
		else
			portrait(button.reward.icon, icon, 64)
		end
		if kind == "item" and amount <= 1 then
			button.reward.qty:SetText("")
		else
			button.reward.qty:SetText(amount)
		end
		button.reward.tip = name
		button.reward:Show()
	end

	local function showListThreats(button, recId)
		local list = {}
		for _, enemy in ipairs(state.enemies[recId] or {}) do
			for _, mechanic in ipairs(enemy.mechanics) do
				if mechanic.icon > 0 then list[#list + 1] = mechanic end
			end
		end
		local count = #list
		if count > 6 then count = 6 end
		local rows = math.ceil(count / 2)
		for i = 1, 6 do
			local threat = button.threats[i]
			local mechanic = list[i]
			if i <= count and mechanic then
				local col = (i - 1) % 2
				local row = math.floor((i - 1) / 2)
				portrait(threat.icon, mechanic.icon, 16)
				threat.tip = mechanic.name
				threat:ClearAllPoints()
				local shift = button.reward:IsShown() and 76 or 8
				threat:SetPoint("RIGHT", button, "RIGHT", -shift - col * 24, (rows - 1) * 12 - row * 24)
				threat:Show()
			else
				threat.tip = nil
				threat:Hide()
			end
		end
	end

	function frame:refresh()
		if not EsteriaGarrison_GetState then return end
		parse(EsteriaGarrison_GetState())
		self.resources:SetText("Your Resources:  " .. (BreakUpLargeNumbers and BreakUpLargeNumbers(state.resources) or state.resources))
		self.status:SetText(RESULT[state.result] or "")
		PanelTemplates_SetTab(self, tab)

		local query = string.lower(self.search:GetText() or "")
		local shown = {}
		for _, follower in ipairs(state.followers) do
			if query == "" or string.find(string.lower(follower.name), query, 1, true) then
				shown[#shown + 1] = follower
			end
		end
		local visible = 8
		local maxScroll = math.max(#shown - visible, 0)
		if followerScroll > maxScroll then followerScroll = maxScroll end
		if maxScroll > 0 then
			local span = (self.list.track:GetHeight() or 0) - 48
			if span < 0 then span = 0 end
			self.list.knob:ClearAllPoints()
			self.list.knob:SetPoint("TOPRIGHT", self.list.track, "TOPRIGHT", 0, -span * followerScroll / maxScroll)
			self.list.knob:Show()
		else
			self.list.knob:Hide()
		end
		if tab == 2 and not followerByDb(self.shownFollower or 0) then
			self.shownFollower = shown[1] and shown[1].db or 0
		end
		self.list.rows.used = 0
		for i = 1, visible do
			local follower = shown[i + followerScroll]
			if follower then
				local button = take(self.list.rows, self.followerButton)
				button:ClearAllPoints()
				button:SetPoint("TOPLEFT", 4, -((i - 1) * 56))
				button.follower = follower
				local here = (tab == 1 and missionByDb(picked) and seated(missionByDb(picked), follower.db))
					or (tab == 2 and follower.db == self.shownFollower)
				followerPlate(button.bg, false)
				if here then followerPlate(button.select, true) else button.select:Hide() end
				portrait(button.face, follower.portrait, 44)
				local r, g, b = qualityColor(follower.quality)
				button.quality:SetVertexColor(r, g, b)
				button.levelBorder:SetVertexColor(r, g, b)
				button.level:SetText(follower.level)
				button.name:SetText(follower.name)
				if busy(follower) then button.name:SetTextColor(0.5, 0.5, 0.5) else button.name:SetTextColor(1, 0.82, 0) end
				if follower.level >= 100 and follower.ilvl > 0 then
					button.info:SetText("iLvl " .. follower.ilvl)
				else
					button.info:SetText("")
				end
				button.status:SetText(statusText(follower))
				local nextXp = nextFollowerXp(follower)
				if nextXp > 0 then
					button.xpTrack:Show()
					local width = math.floor(205 * follower.xp / nextXp)
					if width > 205 then width = 205 end
					if width > 0 then
						button.xp:SetWidth(width)
						button.xp:Show()
					else
						button.xp:Hide()
					end
				else
					button.xpTrack:Hide()
					button.xp:Hide()
				end
				local counters = {}
				for _, ability in ipairs(follower.abilities) do
					if not ability.trait and ability.icon > 0 then counters[#counters + 1] = ability end
				end
				if #counters == 0 then
					for _, ability in ipairs(follower.abilities) do
						if ability.icon > 0 then counters[#counters + 1] = ability end
					end
				end
				local n = #counters
				if n > 4 then n = 4 end
				for c = 1, 4 do
					local counter = button.counters[c]
					if c <= n then
						portrait(counter.icon, counters[c].icon, 18)
						counter.tip = counters[c].name
						counter:ClearAllPoints()
						counter:SetPoint("RIGHT", button, "RIGHT", -6 - (c - 1) * 24, 2)
						counter:Show()
					else
						counter.tip = nil
						counter:Hide()
					end
				end
				if n > 0 then button.class:Hide() else classMark(button.class, follower.spec) end
			end
		end
		hideRest(self.list.rows)

		local mission = missionByDb(picked)
		local showPage = tab == 1 and mission
		local showBoard = tab == 1 and not mission
		local showDetail = tab == 2
		if showPage then self.stage:Show() else self.stage:Hide() end
		if showBoard then self.board:Show() else self.board:Hide() end
		if showDetail then self.detail:Show() else self.detail:Hide() end

		if showBoard then
			local count = #state.missions
			local rows = 6
			if missionScroll > math.max(count - rows, 0) then missionScroll = math.max(count - rows, 0) end
			self.board.used = 0
			if count == 0 then
				local button = take(self.board, self.missionButton)
				button:ClearAllPoints()
				button:SetPoint("TOPLEFT", 8, -8)
				button.mission = nil
				button.banner:SetTexture(0, 0, 0, 0.35)
				button.level:SetText("")
				showListMark(button, nil)
				button.name:SetText("No missions on the table")
				button.where:SetText("The command table offers missions from the garrison list.")
				button.meta:SetText("")
				showListReward(button, 0)
				showListThreats(button, 0)
			end
			for i = 1, rows do
				local row = state.missions[i + missionScroll]
				if row then
					local button = take(self.board, self.missionButton)
					button:ClearAllPoints()
					button:SetPoint("TOPLEFT", 8, -((self.board.used - 1) * 80))
					button.mission = row
					if not listBanner(button.banner, row.location) then
						button.banner:SetTexture("retail/953549.blp")
						button.banner:SetTexCoord(0, 1, 0, 1)
						button.banner:Show()
					end
					button.level:SetText(row.level > 0 and row.level or "")
					showListMark(button, row)
					button.name:SetText(row.name)
					local where = row.location
					if row.st == 1 then
						where = row.left > 0 and (where .. "  ·  " .. clock(row.left) .. " left") or (where .. "  ·  Complete")
					elseif row.st == 2 then
						where = where .. "  ·  Bonus roll"
					end
					button.where:SetText(where)
					button.meta:SetText("")
					showListReward(button, row.id)
					showListThreats(button, row.id)
				end
			end
			hideRest(self.board)
		end

		if showPage then
			if self.stage.layoutPapers then self.stage.layoutPapers() end
			local key = placeKey(mission.location)
			if key == "Shazgul" then key = "ShadowmoonValley" end
			local entry = EsteriaArchitectAtlas and EsteriaArchitectAtlas["_GarrMissionLocation-" .. key .. "-Back"]
			local width = self.stage:GetWidth() - 16
			if width < 200 then width = 600 end
			if entry then
				local sceneW = self.stage:GetWidth() - 12
				if sceneW < 200 then sceneW = 600 end
				local sceneH = math.floor(sceneW * 287 / 1024 * 1.5)
				if sceneH > 280 then sceneH = 280 end
				local function band(texture, top, bottom)
					texture:SetTexture(entry[1])
					texture:SetTexCoord(0, 1, top / 1024, bottom / 1024)
					texture:Show()
				end
				self.stage.art:SetHeight(sceneH)
				band(self.stage.art, 0, 287)
				band(self.stage.mid, 287, 574)
				band(self.stage.ground, 574, 861)
			else
				self.stage.art:SetTexture("retail/953549.blp")
				self.stage.art:SetTexCoord(0, 1, 0, 1)
				self.stage.art:SetHeight(160)
				self.stage.art:Show()
				self.stage.mid:Hide()
				self.stage.ground:Hide()
			end
			self.stage.level:SetText(mission.level > 0 and mission.level or "")
			self.stage.title:SetText(mission.name)
			self.stage.location:SetText(mission.location)
			local seconds = mission.seconds
			if mission.st == 1 then seconds = mission.left end
			local shown = clock(seconds)
			if seconds >= 8 * 3600 then shown = "|cffff7d1a" .. shown .. "|r" end
			self.stage.time:SetText("Time:  " .. shown)
			self.stage.kind:SetText(mission.kind ~= "" and ("Type: " .. mission.kind) or "")
			portrait(self.stage.kindIcon, mission.kindIcon, 16)
			typeIcon(self.stage.typeMark, mission.id)
			self.stage.description:SetText(mission.description)
			local slots = mission.slots
			if slots < 1 then slots = 1 end
			if slots > 3 then slots = 3 end
			local listed = state.enemies[mission.id] or {}
			local enemies = #listed
			if enemies > 3 then enemies = 3 end
			local width = self.stage:GetWidth()
			if width < 200 then width = 556 end
			local enemySpan = enemies > 0 and (53 + (enemies - 1) * 173) or 0
			local enemyX = (width - enemySpan) / 2
			for i = 1, 3 do
				local frame = self.stage.enemies[i]
				local enemy = listed[i]
				if i <= enemies and enemy then
					frame:Show()
					frame:ClearAllPoints()
					frame:SetPoint("TOPLEFT", self.stage, "TOPLEFT", enemyX + (i - 1) * 173, -164)
					frame.name:SetText(enemy.name)
					portrait(frame.face, enemy.portrait, 50)
					local mechs = #enemy.mechanics
					if mechs > 4 then mechs = 4 end
					if mechs > 1 then frame.elite:Show() else frame.elite:Hide() end
					for m = 1, 4 do
						local widget = frame.mechanics[m]
						local info = enemy.mechanics[m]
						if m <= mechs and info then
							widget:Show()
							portrait(widget.icon, info.icon, 28)
							widget:ClearAllPoints()
							if m == 1 then
								widget:SetPoint("BOTTOM", frame, "BOTTOM", (mechs - 1) * -22, -16)
							else
								widget:SetPoint("LEFT", frame.mechanics[m - 1], "RIGHT", 16, 0)
							end
						else
							widget:Hide()
						end
					end
				else
					frame:Hide()
				end
			end
			local party = partyOf(mission)
			self.stage.hint:ClearAllPoints()
			if #party == 0 and mission.st == 0 then
				self.stage.hint:SetText("Drag Followers here to form a Party")
				self.stage.hint:SetPoint("BOTTOM", self.stage, "TOP", 0, -264)
			else
				self.stage.hint:SetText("")
			end
			local slotSpan = 161 + (slots - 1) * 178
			local slotX = (width - slotSpan) / 2
			for i = 1, 3 do
				local slot = self.stage.slots[i]
				if i <= slots then
					slot:Show()
					slot:ClearAllPoints()
					if i == 1 then
						slot:SetPoint("TOPLEFT", self.stage, "TOPLEFT", slotX, -274)
					else
						slot:SetPoint("TOPLEFT", self.stage.slots[i - 1], "TOPRIGHT", 17, 0)
					end
					local db = party[i]
					local follower = db and followerByDb(db)
					slot.db = follower and follower.db or nil
					ring(slot.ring, nil)
					if follower then
						portrait(slot.face, follower.portrait, 44)
						slot.empty:Hide()
						slot.glow:Hide()
					else
						slot.face:Hide()
						slot.empty:Show()
						if i == 1 and mission.st == 0 then slot.glow:Show() else slot.glow:Hide() end
					end
				else
					slot:Hide()
					slot.db = nil
				end
			end
			self.stage.xp:SetText(mission.xp > 0 and ("Base XP:  " .. mission.xp) or "")
			if mission.xp > 0 then
				self.stage.rewardIcon:Show()
				self.stage.rewardBorder:Show()
				self.stage.rewardText:SetText(mission.xp .. " XP")
			else
				self.stage.rewardIcon:Hide()
				self.stage.rewardBorder:Hide()
				self.stage.rewardText:SetText("")
			end
			local chance = chanceOf(mission)
			self.stage.chance:SetText(chance .. "%")
			local cr, cg, cb = 1, 0.2, 0.2
			if chance >= 50 then cr, cg, cb = 0.1, 1, 0.1 end
			self.stage.chance:SetTextColor(cr, cg, cb)
			self.stage.chanceLabel:SetTextColor(cr, cg, cb)
			if mission.st == 0 then
				self.stage.costLabel:SetText("Cost:")
				self.stage.cost:SetText(mission.cost)
				self.stage.costIcon:Show()
			else
				self.stage.costLabel:SetText("")
				self.stage.cost:SetText("")
				self.stage.costIcon:Hide()
			end
			if mission.st == 0 then
				self.stage.start:SetText("Start Mission")
				local full = #party == mission.slots and mission.slots > 0 and state.resources >= mission.cost
				if full then self.stage.start:Enable() else self.stage.start:Disable() end
				self.stage.start:SetScript("OnClick", function() act(7, mission.db, 0) end)
			elseif mission.st == 1 and mission.left <= 0 or mission.st == 2 then
				self.stage.start:SetText(mission.st == 2 and "Bonus Roll" or "Complete")
				self.stage.start:Enable()
				self.stage.start:SetScript("OnClick", function() act(8, mission.db, 0) end)
			else
				self.stage.start:SetText("In Progress")
				self.stage.start:Disable()
			end
		end

		if self.TitleText then
			self.TitleText:SetText(tab == 2 and "Garrison Followers" or "Garrison Missions")
		end

		if showDetail then
			local page = self.detail
			local follower = followerByDb(self.shownFollower or 0)
			page.count:SetText("Followers: " .. #state.followers)
			for i = 1, 8 do page.rows[i]:Hide() end
			page.abilityLabel:Hide()
			page.traitLabel:Hide()
			if follower then
				portrait(page.face, follower.portrait, 44)
				local r, g, b = qualityColor(follower.quality)
				page.quality:SetVertexColor(r, g, b)
				page.levelBorder:SetVertexColor(r, g, b)
				page.level:SetText(follower.level)
				page.name:SetText(follower.name)
				page.spec:SetText(follower.spec)
				classMark(page.class, follower.spec)
				if page.class:IsShown() then page.class:SetAlpha(0.8) end
				local nextXp = nextFollowerXp(follower)
				local remain = nextXp > follower.xp and (nextXp - follower.xp) or 0
				if remain > 0 then
					page.xpText:SetText(remain .. " XP")
					page.xpLabel:SetText(follower.level >= 100 and "to next upgrade" or "to next level")
				else
					page.xpText:SetText("")
					page.xpLabel:SetText("")
				end
				if nextXp > 0 then
					local width = math.floor(420 * follower.xp / nextXp)
					if width > 420 then width = 420 end
					if width > 0 then
						page.xpFill:SetWidth(width)
						page.xpFill:Show()
					else
						page.xpFill:Hide()
					end
					page.xpBg:Show()
					page.xpLine:SetText("XP " .. follower.xp .. " / " .. nextXp)
				else
					page.xpFill:Hide()
					page.xpBg:Hide()
					page.xpLine:SetText("")
				end
				local abilities, traits = {}, {}
				for _, ability in ipairs(follower.abilities) do
					if ability.trait then traits[#traits + 1] = ability else abilities[#abilities + 1] = ability end
				end
				local slot = 0
				local y = -108
				local function section(title, label, list)
					if #list == 0 then return end
					label:ClearAllPoints()
					label:SetPoint("TOPLEFT", 290, y)
					label:SetText(title)
					label:Show()
					y = y - 22
					for n = 1, #list do
						if slot >= 8 then return end
						slot = slot + 1
						local row = page.rows[slot]
						local ability = list[n]
						portrait(row.icon, ability.icon, 35)
						row.icon:ClearAllPoints()
						row.icon:SetPoint("TOPLEFT")
						row.name:SetText(ability.name)
						row.text:SetText(ability.text or "")
						local textH = 0
						if ability.text and ability.text ~= "" then
							row.name:ClearAllPoints()
							row.name:SetPoint("TOPLEFT", row.icon, "TOPRIGHT", 8, -1)
							row.text:Show()
							textH = row.text:GetStringHeight() or 0
							if textH < 12 then
								textH = 14 * math.max(1, math.ceil(string.len(ability.text) / 42))
							end
						else
							row.name:ClearAllPoints()
							row.name:SetPoint("LEFT", row.icon, "RIGHT", 8, 0)
							row.text:Hide()
						end
						local rowH = textH > 0 and (22 + textH) or 40
						if rowH < 40 then rowH = 40 end
						row:SetHeight(rowH)
						row:ClearAllPoints()
						row:SetPoint("TOPLEFT", 290, y)
						row:Show()
						y = y - rowH - 6
					end
				end
				section("Abilities", page.abilityLabel, abilities)
				section("Traits", page.traitLabel, traits)
				if follower.weapon > 0 or follower.armor > 0 then
					page.ilvl:SetText("iLvl " .. follower.ilvl)
					page.weapon.name:SetText("Weapon")
					page.weapon.level:SetText("iLvl " .. follower.weapon)
					page.armor.name:SetText("Armor")
					page.armor.level:SetText("iLvl " .. follower.armor)
					page.weapon:Show()
					page.armor:Show()
				else
					page.ilvl:SetText("")
					page.weapon:Hide()
					page.armor:Hide()
				end
			else
				page.face:Hide()
				page.name:SetText("No followers")
				page.level:SetText("")
				page.spec:SetText("")
				page.xpText:SetText("")
				page.xpLabel:SetText("")
				page.xpLine:SetText("")
				page.xpFill:Hide()
				page.xpBg:Hide()
				page.class:Hide()
				page.ilvl:SetText("")
				page.weapon:Hide()
				page.armor:Hide()
			end
		end
	end
end

build()

local function show()
	frame:Show()
	if EsteriaGarrison_Hello then EsteriaGarrison_Hello() end
	frame:refresh()
end

local watcher = CreateFrame("Frame")
watcher:RegisterEvent("ESTERIA_GARRISON_STATE")
watcher:SetScript("OnEvent", function()
	local text = EsteriaGarrison_GetState and EsteriaGarrison_GetState() or ""
	local mode = string.match(text, "^OPEN\t(%w+)")
	if mode == "missions" then
		frame:Show()
	end
	if frame:IsShown() then frame:refresh() end
end)

local listed = false
for _, name in ipairs(UISpecialFrames) do
	if name == "EsteriaGarrisonMissions" then listed = true end
end
if not listed then table.insert(UISpecialFrames, "EsteriaGarrisonMissions") end

SLASH_ESTERIA_GARRISON_MISSION1 = "/garrisonmission"
SlashCmdList["ESTERIA_GARRISON_MISSION"] = function()
	if frame:IsShown() then
		frame:Hide()
	else
		show()
		act(14)
	end
end
