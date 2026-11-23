-- Which retail social systems exist on Esteria. The windows ask these before showing tabs and buttons.

if not rawget(Enum, "GameRule") then -- engine-only enum, absent from the API docs
	rawset(Enum, "GameRule", { IngameFriendsListDisabled = 1, DisableQuickJoin = 2, DisableRaidGroups = 3 })
end
-- Engine constants absent from the API docs.
local inventory = rawget(Constants, "InventoryConstants") or {}
inventory.NumBagSlots = inventory.NumBagSlots or NUM_BAG_SLOTS or 4
inventory.NumReagentBagSlots = inventory.NumReagentBagSlots or 0
rawset(Constants, "InventoryConstants", inventory)

RetailC_GameRules.IsGameRuleActive = function() return false end
RetailC_GameRules.GetActiveGameMode = function() return Enum.GameMode.Standard end

-- On: 12.x replaces the Contacts/Raid/Quick Join tabs with its own social window. The classic four-tab layout is wanted.
RetailC_SocialUI.IsSystemEnabled = function() return false end
RetailC_FriendList.IsLegacyFriendSystemEnabled = function() return false end

-- Recruit-a-Friend is on (stock WotLK rewards, served by the social service).
RetailC_RecruitAFriend.IsSystemEnabled = function() return true end
RetailC_RecruitAFriend.IsRecruitingEnabled = function() return true end

-- Guild Finder / Find a Community are shown disabled until they are built on the server.
RetailC_ClubFinder.IsEnabled = function() return false end
RetailC_ClubFinder.IsCommunityFinderEnabled = function() return false end
RetailC_ClubFinder.GetClubRecruitmentSettings = function() return {} end
RetailC_ClubFinder.PlayerRequestPendingClubsList = function() end
-- No finder service: every list is empty.
for _, name in ipairs({ "ReturnClubApplicantList", "ReturnPendingClubApplicantList", "ReturnMatchingCommunityList",
	"ReturnMatchingGuildList", "PlayerReturnPendingCommunitiesList", "PlayerReturnPendingGuildsList", "PlayerGetClubInvitationList", "GetStatusOfPostingFromClubId" }) do
	RetailC_ClubFinder[name] = function() return {} end
end
RetailC_ClubFinder.GetTotalMatchingCommunityListSize = function() return 0 end
RetailC_ClubFinder.GetTotalMatchingGuildListSize = function() return 0 end

-- Quick Join lists nothing until groups are shared through the social service.
RetailC_SocialQueue.GetAllGroups = function() return {} end
RetailC_SocialQueue.GetConfig = function()
	return { TOASTS_DISABLED = true, TOAST_DURATION = 5, DELAY_DURATION = 1, QUEUE_MULTIPLIER = 1, PLAYER_MULTIPLIER = 1,
		PLAYER_FRIEND_VALUE = 1, PLAYER_GUILD_VALUE = 1, THROTTLE_INITIAL_THRESHOLD = 1, THROTTLE_DECAY = 1,
		THROTTLE_PRIORITY_SPIKE = 1, THROTTLE_MIN_THRESHOLD = 1, THROTTLE_PVP_PRIORITY_NORMAL = 1,
		THROTTLE_PVP_PRIORITY_LOW = 1, THROTTLE_PVP_HONOR_THRESHOLD = 1, THROTTLE_LFGLIST_PRIORITY_DEFAULT = 1,
		THROTTLE_LFGLIST_PRIORITY_ABOVE = 1, THROTTLE_LFGLIST_PRIORITY_BELOW = 1, THROTTLE_LFGLIST_ILVL_SCALING_ABOVE = 1,
		THROTTLE_LFGLIST_ILVL_SCALING_BELOW = 1, THROTTLE_RF_PRIORITY_ABOVE = 1, THROTTLE_RF_ILVL_SCALING_ABOVE = 1,
		THROTTLE_DF_MAX_ITEM_LEVEL = 1, THROTTLE_DF_BEST_PRIORITY = 1 }
end
RetailC_LFGList.GetAvailableRoles = function() return true, true, true end

-- Recent Allies (people you recently grouped with) is shown, empty until the server records groups.
RetailC_RecentAllies.IsSystemEnabled = function() return true end
RetailC_RecentAllies.GetRecentAllies = function() return {} end
RetailC_RecentAllies.IsRecentAllyDataReady = function() return true end
RetailC_SocialRestrictions.IsFriendsDisabled = function() return false end
RetailC_SocialRestrictions.IsChatDisabled = function() return false end
RetailC_SocialRestrictions.IsMuted = function() return false end
RetailC_SocialRestrictions.IsSquelched = function() return false end

-- Recruit-a-Friend summoning is not on the realm yet: never ready, never on cooldown.
RetailC_RecruitAFriend.GetSummonFriendCooldown = function() return 0, 0 end
RetailC_RecruitAFriend.CanSummonFriend = function() return false end
RetailC_RecruitAFriend.SummonFriend = function() end
RetailC_ClubFinder.GetPlayerApplicantSettings = function() return { playStyle = 0, maxLevelOnly = false, checkedRoles = 0 } end
RetailC_ClubFinder.GetClubFinderDisableReason = function() return Enum.ClubFinderDisableReason and Enum.ClubFinderDisableReason.Muted or 0 end

-- Retail's Raid tab hosts the raid roster frame (Blizzard_RaidUI's ClaimRaidFrame); the stock 3.3.5 RaidFrame is reused.
function ClaimRaidFrame(parent)
	if not RaidFrame then return end
	RaidFrame:SetParent(parent)
	RaidFrame:ClearAllPoints()
	RaidFrame:SetPoint("TOPLEFT", parent, "TOPLEFT", 4, -60)
	RaidFrame:SetPoint("BOTTOMRIGHT", parent, "BOTTOMRIGHT", -4, 26)
end

-- There is no guild calendar.
RetailC_Calendar.GetNumGuildEvents = function() return 0 end
RetailC_Calendar.GetGuildEventInfo = function() end

-- C_DateAndTime: calendar tables from os time. Community chat calls these for every message and every refresh; with the
-- stubs returning nil it errored half way and incoming messages never showed.
do
	local function calendar(epoch)
		local d = date("*t", epoch)
		return { year = d.year, month = d.month, monthDay = d.day, weekday = d.wday, hour = d.hour, minute = d.min }
	end
	local function epochOf(t) return time({ year = t.year, month = t.month, day = t.monthDay, hour = t.hour, min = t.minute }) end
	local D = RetailC_DateAndTime
	D.GetCurrentCalendarTime = function() return calendar(time()) end
	D.GetCalendarTimeFromEpoch = function(epoch)
		epoch = tonumber(epoch) or 0
		if epoch > 1e11 then epoch = epoch / 1e6 end -- retail counts microseconds
		return calendar(epoch)
	end
	D.AdjustTimeByDays = function(t, days) return calendar(epochOf(t) + days * 86400) end
	D.AdjustTimeByMinutes = function(t, minutes) return calendar(epochOf(t) + minutes * 60) end
	D.CompareCalendarTime = function(a, b) local x, y = epochOf(a), epochOf(b) return x < y and -1 or x > y and 1 or 0 end
	D.GetSecondsUntil = function(t) return epochOf(t) - time() end
end
RetailC_Calendar.GetClubCalendarEvents = function() return {} end

-- Recruit-a-Friend data: stock WotLK limits and an empty account until the social service tracks recruitment (plan stage 4).
-- The window reads these directly; with nothing here its recruitment dialog and reward header never fill in.
do
	local R = RetailC_RecruitAFriend
	local system = { maxRecruits = 5, maxRecruitMonths = 3, maxRecruitmentUses = 1, daysInCycle = 90 }
	local info = {
		versions = { { rafVersion = 1, monthCount = { lifetimeMonths = 0 }, numRecruits = 0, numAffordableRewards = 0, rewards = {} } },
		recruits = {}, claimInProgress = false,
	}
	R.GetRAFSystemInfo = function() return system end
	R.GetRAFInfo = function() return info end
	R.RequestUpdatedRecruitmentInfo = function() EsteriaRetailUI.Fire("RAF_INFO_UPDATED", info) end
	R.GetRecruitActivityRequirementsText = function() return "" end
	R.ClaimNextReward = function() return false end
	R.ClaimActivityReward = function() return false end
	R.GenerateRecruitmentLink = function()
		DEFAULT_CHAT_FRAME:AddMessage("|cffffd100Recruit a Friend:|r recruitment links are not available on this realm yet.")
		return false
	end
end

-- The reward list previews items in a side dress-up window that this client lacks: a hidden one keeps the window working.
if not rawget(_G, "SideDressUpFrame") then
	SideDressUpFrame = CreateFrame("Frame", "SideDressUpFrame", UIParent)
	SideDressUpFrame:Hide()
	CloseSideDressUpFrame = CloseSideDressUpFrame or function() end
	SetUpSideDressUpFrame = SetUpSideDressUpFrame or function() end
	SetUIPanelShown = SetUIPanelShown or function(frame, shown) if shown then ShowUIPanel(frame) else HideUIPanel(frame) end end
end

-- Class lookups by retail class ID (the roster places the name after the class icon; with no class it drew over the level).
do
	local FILES = { "WARRIOR", "PALADIN", "HUNTER", "ROGUE", "PRIEST", "DEATHKNIGHT", "SHAMAN", "MAGE", "WARLOCK", "MONK", "DRUID", "DEMONHUNTER", "EVOKER" }
	RetailC_CreatureInfo.GetClassInfo = function(classID)
		local file = FILES[tonumber(classID) or 0]
		if not file then return end
		local name = (LOCALIZED_CLASS_NAMES_MALE and LOCALIZED_CLASS_NAMES_MALE[file]) or file:sub(1, 1) .. file:sub(2):lower()
		return { className = name, classFile = file, classID = classID }
	end
end
