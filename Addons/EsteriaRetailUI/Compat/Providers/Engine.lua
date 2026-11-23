-- Small engine queries the retail framework makes at load or show, answered from what 3.3.5a has.

-- RegisterEventCallback(event, callback[, owner]): one shared event frame.
do
	local frame, callbacks = CreateFrame("Frame"), {}
	frame:SetScript("OnEvent", function(_, event, ...)
		for _, entry in ipairs(callbacks[event] or {}) do entry.callback(entry.owner or event, ...) end
	end)
	function RegisterEventCallback(event, callback, owner)
		if not callbacks[event] then callbacks[event] = {} pcall(frame.RegisterEvent, frame, event) end
		table.insert(callbacks[event], { callback = callback, owner = owner })
	end
	function UnregisterEventCallback(event, callback)
		for i, entry in ipairs(callbacks[event] or {}) do
			if entry.callback == callback then table.remove(callbacks[event], i) break end
		end
	end
end

-- Font objects: retail lists them for text scaling and reads their settings.
function GetFonts() return {} end
function GetFontInfo(font)
	font = type(font) == "string" and _G[font] or font
	if not font or not font.GetFont then return end
	local path, height, flags = font:GetFont()
	local r, g, b, a = font:GetTextColor()
	local sx, sy = font:GetShadowOffset()
	local sr, sg, sb, sa = font:GetShadowColor()
	return { fontObject = font, font = path, height = height, outline = flags or "",
		color = { r = r, g = g, b = b, a = a },
		shadow = { x = sx, y = sy, color = { r = sr, g = sg, b = sb, a = sa } } }
end

function LocalizedClassList(female)
	local classes = {}
	FillLocalizedClassList(classes, female)
	return classes
end
function GetSpecializationInfoForClassID() end -- no specializations in 3.3.5a
function AutoCompleteEditBox_SetAutoCompleteSource() end -- retail chat autocomplete; plain edit boxes here
RetailC_EventUtils.IsEventValid = function() return true end

-- Retail panels describe their UIParent layout in attributes; 3.3.5a keeps the same data in UIPanelWindows.
function SetUIPanelAttribute(frame, name, value)
	local name_ = frame:GetName()
	if not name_ then return end
	UIPanelWindows[name_] = UIPanelWindows[name_] or { area = "left", pushable = 1 }
	UIPanelWindows[name_][name] = value
end
function GetUIPanelAttribute(frame, name)
	local info = frame:GetName() and UIPanelWindows[frame:GetName()]
	return info and info[name]
end

-- One realm: its id matches the realmID the Battle.net provider reports for friends in game.
GetRealmID = GetRealmID or function() return 1 end
GetNormalizedRealmName = GetNormalizedRealmName or function() return ((GetRealmName() or ""):gsub("[%s%-]", "")) end

function GetAvailableLocaleInfo() return { { localeId = 0, localeName = GetLocale() } } end
RegisterCVar("userFontScale", "1")

-- Profession skill lines only key icon tables here; any distinct value per profession will do.
RetailC_TradeSkillUI.GetProfessionSkillLineID = function(profession) return 100000 + (profession or 0) end

-- Guild systems from Cataclysm onward have no 3.3.5a data: perks, rewards, guild reputation, renames. Their tabs are
-- shown disabled; these answer "none".
GetNumGuildPerks = GetNumGuildPerks or function() return 0 end
GetNumGuildRewards = GetNumGuildRewards or function() return 0 end
RequestGuildRewards = RequestGuildRewards or function() end
GetGuildRenameRequired = GetGuildRenameRequired or function() return false end
GetGuildFactionGroup = GetGuildFactionGroup or function() return UnitFactionGroup("player") == "Horde" and 0 or 1 end
GetGuildNewsSort = GetGuildNewsSort or function() return 0 end
GetNumGuildNews = GetNumGuildNews or function() return 0 end
QueryGuildNews = QueryGuildNews or function() end
ClassicExpansionAtLeast = ClassicExpansionAtLeast or function() return false end
AbbreviateNumbers = AbbreviateNumbers or function(value) return tostring(value or 0) end
RegisterCVar("miniCommunitiesFrame", "0")
RetailC_Reputation.GetGuildFactionData = function() return nil end
RetailC_TradeSkillUI.IsGuildTradeSkillsEnabled = function() return false end

-- Account CVars the retail files read (Tools: grep CVar("name") over retail/); 3.3.5a errors on unknown names, so the
-- ones the client lacks are registered off. Existing stock CVars keep their values.
for _, name in ipairs({ "RetailQuestTextContrast", "ShakeStrengthUI", "accessibilityScreenNarrationEnabled",
	"accessibilityScreenNarrationSpeechRate", "accessibilityScreenNarrationSpeechVolume", "accessibilityScreenNarrationVoice",
	"addFriendInfoShown", "allowCompareWithToggle", "closedInfoFrames", "colorblindMode", "communitiesShowOffline",
	"findYourselfAnywhere", "findYourselfModeCircle", "findYourselfModeIcon", "friendInvitesCollapsed", "hideHelptips",
	"lastSelectedClubId", "miniDressUpFrame", "nameplateShowCastBars", "partyInvitesCollapsed_Glue", "pendingInviteInfoShown",
	"showCastableBuffs", "showCustomSetDetails", "showDispelDebuffs", "showTargetCastbar", "useClassicGuildUI" }) do
	if not pcall(GetCVar, name) or GetCVar(name) == nil then RegisterCVar(name, "0") end
end

-- Retail's per-character "seen applicants" table (saved, see the TOC) is read before anything creates it.
Retailg_clubIdToSeenApplicants = Retailg_clubIdToSeenApplicants or {}

-- Globals the Guild & Communities files call that 3.3.5a lacks (from a sweep of their bare calls against _G).
-- Later-expansion guild systems (recipes, perks, rewards, news, rank shifting, GM replacement) answer "none".
local function none() end
for name, func in pairs({
	QueryGuildRecipes = none, CanViewGuildRecipes = function() return false end,
	CanReplaceGuildMaster = function() return false end, GetGuildPerkInfo = none, GetGuildRewardInfo = none,
	GetGuildNewsFilters = function() return {} end, SetGuildNewsFilter = none, GuildNewsSort = none, GuildNewsSetSticky = none,
	GuildControlGetAllowedShifts = function() return false, false end, GuildControlShiftRankUp = none, GuildControlShiftRankDown = none,
	GetGuildBankWithdrawGoldLimit = function() return 0 end,
	SetGuildBankTabItemWithdraw = SetGuildBankTabWithdraw or none,
	MainMenuMicroButton_HideAlert = none, SetMoneyFrameColorByFrame = none,
	FCF_CanOpenNewWindow = function() return false end, FCF_IsChatWindowIndexReserved = function() return false end,
	GetCurrentRegionName = function() return "US" end,
	-- Community/ticket hyperlinks have no 3.3.5a handler: the visible text stands alone.
	GetClubTicketLink = function(_, clubName) return clubName end,
	GetPlayerCommunityLink = function(_, linkText) return linkText end,
	GetBNPlayerCommunityLink = function(_, linkText) return linkText end,
	GetDiscordUserCommunityLink = function(_, linkText) return linkText end,
	GetSpecializationInfoByID = none, GetSpecializationInfoForSpecID = none, GetSpecializationRoleEnumByID = none,
	ClubFinderGetTotalNumSpecializations = function() return 0 end,
	ShowAchievementFrameForAchievement = function() if ToggleAchievementFrame then ToggleAchievementFrame() end end,
	AchievementFrame_ToggleAchievementFrame = function() if ToggleAchievementFrame then ToggleAchievementFrame() end end,
	CalendarFrame_OpenToGuildEventIndex = function() if ToggleCalendar then ToggleCalendar() end end,
}) do
	if _G[name] == nil then _G[name] = func end
end

function GetNumMembersInRank(rankIndex)
	local count = 0
	for i = 1, GetNumGuildMembers(true) do
		if select(3, GetGuildRosterInfo(i)) == rankIndex then count = count + 1 end
	end
	return count
end

function FCF_IterateActiveChatWindows(callback)
	for i = 1, NUM_CHAT_WINDOWS do
		local frame = _G["ChatFrame" .. i]
		if frame and (frame:IsShown() or frame.isDocked) then callback(frame, i) end
	end
end

GameTooltip_SetBasicTooltip = GameTooltip_SetBasicTooltip or function(tooltip, text, x, y, wrap)
	tooltip:SetOwner(UIParent, "ANCHOR_NONE")
	tooltip:SetPoint("BOTTOMLEFT", x, y)
	tooltip:SetText(text, 1, 1, 1, 1, wrap)
	tooltip:Show()
end

-- 3.3.5a exposes a guild tabard only as character-model texture names (GetGuildTabardFileNames), with no emblem index
-- or colours to pick a cell of the Cataclysm emblem sheet: draw the plain banner (background and border as the XML set
-- them) and no emblem, rather than the whole sheet.
-- shortcut: no emblem art; parse GetGuildTabardFileNames' Emblem_<n>_<colour> if the banner should show it.
function SetLargeGuildTabardTextures(unit, emblem, background, border)
	-- The window portrait names Cataclysm's own GuildFrame sheet, which 3.3.5a's file of that name does not match.
	local overlay = RetailCommunitiesFrame and RetailCommunitiesFrame.PortraitOverlay
	if overlay and emblem and emblem:GetParent() == overlay then
		if background then background:SetTexture(nil) end
		if border then border:SetTexture(nil) end
		emblem:ClearAllPoints()
		emblem:SetPoint("CENTER", overlay.Portrait, "CENTER", 0, 0)
		emblem:SetSize(34, 34)
		emblem:SetTexture("Interface\\Icons\\INV_Shirt_GuildTabard_01")
		emblem:SetTexCoord(0.08, 0.92, 0.08, 0.92)
		return
	end
	if emblem and emblem:GetWidth() <= 64 then -- a plain tabard icon stands in for a small emblem (portrait, list banner)
		emblem:SetTexture("Interface\\Icons\\INV_Shirt_GuildTabard_01")
		emblem:SetTexCoord(0.08, 0.92, 0.08, 0.92)
	elseif emblem then
		emblem:SetTexture(nil)
	end
	-- The Cataclysm banner art is white, tinted per guild; without colour data it gets a neutral one.
	if background then background:SetVertexColor(0.3, 0.3, 0.34) end
end

-- Retail lists every frame under the cursor; 3.3.5a has only the topmost one.
function GetMouseFoci()
	local focus = GetMouseFocus()
	-- A click on part of a dropdown counts for the dropdown: menus stay open when the owner handles the mouse event.
	local walk = focus
	while walk do
		if walk.HandlesGlobalMouseEvent then focus = walk break end
		walk = walk:GetParent()
	end
	return focus and { focus } or {}
end
