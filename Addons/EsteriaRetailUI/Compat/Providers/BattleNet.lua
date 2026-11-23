-- Retail Battle.net friends (RetailBN*, RetailC_BattleNet) answered from the Esteria social service. The service is
-- reached through the EsteriaChat addon's game-bridge transport; without it (or before the account is linked) the
-- window shows Battle.net as unavailable, as retail does when disconnected.
local Fire = EsteriaRetailUI.Fire
local state = { connected = false, me = nil, friends = {}, incoming = {}, ids = {}, nextId = 1 }
EsteriaRetailUI.bnet = state

local function request(payload, callback)
	if not (EsteriaChat and EsteriaChat.request) then
		DEFAULT_CHAT_FRAME:AddMessage("|cffff5555Esteria social:|r the EsteriaChat add-on is not loaded")
		return false
	end
	EsteriaChat.request(payload, callback or function() end)
	return true
end

-- Session-stable numeric ids for tags (retail identifies accounts by number).
local function idFor(tag)
	if not state.ids[tag] then state.ids[tag] = state.nextId; state.nextId = state.nextId + 1 end
	return state.ids[tag]
end

local function epoch(iso)
	local y, m, d, H, M, S = (iso or ""):match("^(%d+)-(%d+)-(%d+)T(%d+):(%d+):(%d+)")
	if not y then return nil end
	return time({ year = tonumber(y), month = tonumber(m), day = tonumber(d), hour = tonumber(H), min = tonumber(M), sec = tonumber(S) })
end

local function gameAccount(friend, id)
	local online = friend.presence and friend.presence ~= "offline"
	local game = friend.gamePresence
	local info = {
		gameAccountID = id * 10 + 1, isOnline = online and true or false, clientProgram = "",
		isGameAFK = friend.presence == "away", isGameBusy = friend.presence == "dnd", isAppearOffline = false,
		canSummon = false, hasFocus = false, isInCurrentRegion = true, regionID = 1, richPresence = "",
	}
	if online and game and game.characterName then
		info.clientProgram = BNET_CLIENT_WOW
		info.wowProjectID = WOW_PROJECT_ID
		info.characterName = game.characterName
		info.realmName = GetRealmName()
		info.realmDisplayName = info.realmName
		info.realmID = tonumber(game.realmId) or 1
		info.characterLevel = tonumber(game.level)
		info.className = game.class
		info.raceName = game.race
		info.factionName = game.faction
		info.areaName = game.zone or game.area
		info.richPresence = info.areaName or ""
	elseif online then
		info.clientProgram = BNET_CLIENT_APP -- signed in to the launcher, not in game
	end
	return info
end

local function accountInfo(friend)
	local id = idFor(friend.tag)
	return {
		bnetAccountID = id, accountName = friend.displayName or friend.tag, battleTag = friend.tag,
		isFriend = true, isBattleTagFriend = true, friendLevel = 0,
		lastOnlineTime = epoch(friend.lastSeen) or 0, -- 0: never seen, shown as "Offline" isAFK = friend.presence == "away", isDND = friend.presence == "dnd",
		isFavorite = friend.favorite and true or false, appearOffline = false, friendTags = {},
		customMessage = friend.statusMessage or "", customMessageTime = 0, note = friend.note or "", rafLinkType = 0,
		gameAccountInfo = gameAccount(friend, id),
	}
end

-- Retail lists favourites, then online friends, then offline ones, each by name.
local function sortFriends(list)
	table.sort(list, function(a, b)
		local ao, bo = a.presence ~= "offline", b.presence ~= "offline"
		if (a.favorite and true or false) ~= (b.favorite and true or false) then return a.favorite and true or false end
		if ao ~= bo then return ao end
		return (a.displayName or a.tag):lower() < (b.displayName or b.tag):lower()
	end)
end

local refreshing, pending = false, false
function EsteriaRetailUI.RefreshBattleNet()
	if refreshing then pending = true return end
	refreshing = true
	local acc = { friends = {}, incoming = {} }
	local function page(n)
		local sent = request({ op = "feed", page = n }, function(result)
			if result.error then
				refreshing = false
				if DevHarness_Log then pcall(DevHarness_Log, "BN feed error: " .. tostring(result.error) .. " " .. tostring(result.message)) end
				local now = GetTime()
				if not state.lastReport or now - state.lastReport > 20 then
					state.lastReport = now
					DEFAULT_CHAT_FRAME:AddMessage("|cffff5555Esteria social:|r " .. tostring(result.error) .. " - " .. tostring(result.message))
				end
				-- Timeouts and throttling are transient: only a rejected account (or three failures in a row) is "unavailable".
				state.failures = (state.failures or 0) + 1
				if (result.error == "not_linked" or state.failures >= 3) and state.connected then
					state.connected = false; Fire("BN_DISCONNECTED")
				end
				return
			end
			state.failures = 0
			for _, f in ipairs(result.friends or {}) do acc.friends[#acc.friends + 1] = f end
			for _, i in ipairs(result.incoming or {}) do acc.incoming[#acc.incoming + 1] = i end
			if result.hasMore then return page(n + 1) end
			sortFriends(acc.friends)
			local wasConnected = state.connected
			state.me, state.friends, state.incoming, state.connected = result.me, acc.friends, acc.incoming, true
			refreshing = false
			if not wasConnected then Fire("BN_CONNECTED") end
			Fire("BN_INFO_CHANGED")
			Fire("BN_FRIEND_LIST_SIZE_CHANGED")
			Fire("BN_FRIEND_INFO_CHANGED")
			-- The stock toast says "You have N friend requests": announce only a changed, non-zero count.
			if #state.incoming > 0 and #state.incoming ~= state.announced then Fire("BN_FRIEND_INVITE_LIST_INITIALIZED", #state.incoming) end
			state.announced = #state.incoming
			if pending then pending = false EsteriaRetailUI.RefreshBattleNet() end
		end)
		if not sent then refreshing = false end
	end
	page(0)
end

do -- refresh at login, every 30 s, and on any social event
	local frame, elapsed = CreateFrame("Frame"), 0
	frame:RegisterEvent("PLAYER_ENTERING_WORLD")
	frame:SetScript("OnEvent", function() EsteriaRetailUI.RefreshBattleNet() end)
	frame:SetScript("OnUpdate", function(_, delta)
		elapsed = elapsed + delta
		if elapsed >= 30 then elapsed = 0 EsteriaRetailUI.RefreshBattleNet() end
	end)
	if EsteriaChat and EsteriaChat.listen then
		EsteriaChat.listen(function(event)
			if event.type ~= "community_message" then EsteriaRetailUI.RefreshBattleNet() end
		end)
	end
end

local function friendAt(index) return state.friends[index] end
local function indexOf(id)
	for i, f in ipairs(state.friends) do if idFor(f.tag) == id then return i end end
end
local function inviteTag(inviteID)
	for _, invite in ipairs(state.incoming) do if idFor(invite.tag) == inviteID then return invite.tag end end
end
local function presence(value, message)
	request({ op = "presence", presence = value, statusMessage = message }, function() EsteriaRetailUI.RefreshBattleNet() end)
end

-- Globals (renamed RetailBN* in the retail files) ----------------------------------------------------------------------
function RetailBNConnected() return state.connected end
function RetailBNFeaturesEnabled() return true end
function RetailBNFeaturesEnabledAndConnected() return state.connected end
function RetailBNGetInfo()
	local me = state.me
	if not me then return end
	local chosen = me.explicitPresence or me.presence -- the status picked in the launcher or here, not the aggregate of live sessions
	return idFor(me.tag), me.tag, nil, me.statusMessage or "", chosen == "away", chosen == "dnd", true
end
function RetailBNGetNumFriends()
	local online, favorites, favoritesOnline = 0, 0, 0
	for _, f in ipairs(state.friends) do
		local isOnline = f.presence ~= "offline"
		if isOnline then online = online + 1 end
		if f.favorite then favorites = favorites + 1 if isOnline then favoritesOnline = favoritesOnline + 1 end end
	end
	return #state.friends, online, favorites, favoritesOnline
end
function RetailBNGetFriendIndex(id) return indexOf(id) end
function RetailBNGetNumFriendInvites() return #state.incoming end
function RetailBNGetNumBlocked() return 0 end
function RetailBNGetBlockedInfo() end
function RetailBNGetSelectedBlock() return 0 end
function RetailBNSetSelectedBlock() end
function RetailBNSetBlocked() end
function RetailBNSetSelectedFriend(index) state.selected = index end
function RetailBNGetSelectedFriend() return state.selected or 0 end
function RetailBNSendWhisper(id, text) RetailC_BattleNet.SendWhisper(id, text) end
function RetailBNSendFriendInvite(tag) request({ op = "friend", username = tag }, function() EsteriaRetailUI.RefreshBattleNet() end) end
function RetailBNSendFriendInviteByID() end
function RetailBNAcceptFriendInvite(inviteID)
	local tag = inviteTag(inviteID)
	if tag then request({ op = "accept", username = tag }, function() EsteriaRetailUI.RefreshBattleNet() end) end
end
function RetailBNDeclineFriendInvite(inviteID)
	local tag = inviteTag(inviteID)
	if tag then request({ op = "removeFriend", username = tag }, function() EsteriaRetailUI.RefreshBattleNet() end) end
end
function RetailBNRemoveFriend(id)
	local f = state.friends[indexOf(id) or 0]
	if f then request({ op = "removeFriend", username = f.tag }, function() EsteriaRetailUI.RefreshBattleNet() end) end
end
function RetailBNSetFriendNote() end -- notes are kept per viewer in the launcher; not exposed to the game bridge yet
function RetailBNSetFriendFavoriteFlag() end
function RetailBNSummonFriendByIndex() end
function RetailBNRequestInviteFriend() end
function RetailBNGetNumFOF() return 0 end
function RetailBNGetFOFInfo() end
function RetailBNInviteFriend() end
function RetailBNCheckBattleTagInviteToUnit() end
function RetailBNCheckBattleTagInviteToGuildMember() end
function RetailBNIsSelf(id) return state.me and idFor(state.me.tag) == id end
function RetailBNIsFriend(id) return indexOf(id) ~= nil end
function RetailBNGetDisplayName(id) local f = state.friends[indexOf(id) or 0] return f and (f.displayName or f.tag) end

-- RetailC_BattleNet ----------------------------------------------------------------------------------------------------
local BN = RetailC_BattleNet
BN.GetFriendAccountInfo = function(index) local f = friendAt(index) return f and accountInfo(f) end
BN.GetAccountInfoByID = function(id) local f = state.friends[indexOf(id) or 0] return f and accountInfo(f) end
BN.GetAccountInfoByGUID = function() end
BN.GetFriendNumGameAccounts = function(index)
	local f = friendAt(index)
	return (f and f.presence ~= "offline") and 1 or 0
end
BN.GetFriendGameAccountInfo = function(index) local f = friendAt(index) return f and gameAccount(f, idFor(f.tag)) end
BN.GetGameAccountInfoByID = function(gameAccountID)
	local f = state.friends[indexOf(math.floor((gameAccountID or 0) / 10)) or 0]
	return f and gameAccount(f, idFor(f.tag))
end
BN.GetGameAccountInfoByGUID = function() end
BN.GetFriendInviteInfo = function(index)
	local invite = state.incoming[index]
	if not invite then return end
	return { inviteID = idFor(invite.tag), accountName = invite.displayName or invite.tag, isBattleTag = true, battleTag = invite.tag }
end
BN.SendWhisper = function(id, text)
	local f = state.friends[indexOf(id) or 0]
	if f and EsteriaChat and EsteriaChat.sendAccountWhisper then EsteriaChat.sendAccountWhisper(f.tag, text) return true end
	return false
end
BN.SetAFK = function(afk) presence(afk and "away" or "online") end
BN.SetDND = function(dnd) presence(dnd and "dnd" or "online") end
BN.SetAppearOffline = function(off) presence(off and "offline" or "online") end
BN.SetCustomMessage = function(text) presence(nil, text or "") return true end
BN.InviteFriend = function() end
BN.AreFriendTagsEnabled = function() return false end
BN.AreTitleFriendsEnabled = function() return false end
BN.IsBattleNetFriendsListEnabled = function() return true end
BN.IsBattleNetFriendsListSupported = function() return true end
BN.GetCustomTitleFriendName = function() end
