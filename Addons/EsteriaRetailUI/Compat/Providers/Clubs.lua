-- RetailC_Club / RetailC_GuildInfo: the Guild & Communities window's clubs.
--   Club 1     the character's guild, from the 3.3.5a guild API; guild and officer chat are its two streams.
--   Club 1000+ Esteria communities from the social service (EsteriaChat transport); channels are streams. They are
--              account-wide, so they appear as Battle.net communities.
-- Messages are { epoch = 0, position = n }: the service's message id, or a per-session counter for guild chat.
local Fire = EsteriaRetailUI.Fire
local CT, ROLE, PRESENCE, STREAM = Enum.ClubType, Enum.ClubRoleIdentifier, Enum.ClubMemberPresence, Enum.ClubStreamType
local GUILD_ID, COMMUNITY_BASE = 1, 1000
local clubs, order = {}, {} -- clubId -> club; subscribed order
local invitations = {}       -- community invitations for this account
EsteriaRetailUI.clubs = clubs

local function request(payload, callback)
	if not (EsteriaChat and EsteriaChat.request) then return false end
	EsteriaChat.request(payload, callback or function() end)
	return true
end

local function newClub(id, clubType, source, remoteId)
	local club = clubs[id] or { streams = {}, streamOrder = {}, members = {}, memberIds = {}, nextMember = 1,
		messages = {}, complete = {}, loaded = {} }
	club.id, club.source, club.remoteId = id, source, remoteId
	club.info = club.info or { clubId = id, clubType = clubType, avatarId = 0, favoriteTimeStamp = 0, joinTime = 0,
		socialQueueingEnabled = false, crossFaction = true }
	clubs[id] = club
	return club
end

local function memberId(club, key)
	if not club.memberIds[key] then club.memberIds[key] = club.nextMember; club.nextMember = club.nextMember + 1 end
	return club.memberIds[key]
end

local function addMessage(club, streamId, position, content, author)
	local list = club.messages[streamId] or {}
	club.messages[streamId] = list
	for _, m in ipairs(list) do if m.messageId.position == position then return m end end
	local message = { messageId = { epoch = 0, position = position }, content = content, author = author,
		destroyed = false, edited = false }
	local at = #list + 1
	while at > 1 and list[at - 1].messageId.position > position do at = at - 1 end
	table.insert(list, at, message)
	return message
end

local function rebuildOrder()
	wipe(order)
	if clubs[GUILD_ID] then order[#order + 1] = GUILD_ID end
	local ids = {}
	for id in pairs(clubs) do if id >= COMMUNITY_BASE then ids[#ids + 1] = id end end
	table.sort(ids)
	for _, id in ipairs(ids) do order[#order + 1] = id end
end

-- Guild ------------------------------------------------------------------------------------------------------------
local CLASS_IDS = { WARRIOR = 1, PALADIN = 2, HUNTER = 3, ROGUE = 4, PRIEST = 5, DEATHKNIGHT = 6, SHAMAN = 7, MAGE = 8,
	WARLOCK = 9, MONK = 10, DRUID = 11, DEMONHUNTER = 12, EVOKER = 13 }
local guildChat = 0

local function guildMember(club, index)
	local name, rank, rankIndex, level, _, zone, note, officerNote, online, status, classFile = GetGuildRosterInfo(index)
	if not name then return end
	local id = memberId(club, name)
	local presence = not online and PRESENCE.Offline or status == 1 and PRESENCE.Away or status == 2 and PRESENCE.Busy or PRESENCE.Online
	local years, months, days, hours = GetGuildRosterLastOnline(index)
	return {
		isSelf = name == UnitName("player"), memberId = id, name = name, clubType = CT.Guild, presence = presence,
		role = rankIndex == 0 and ROLE.Owner or rankIndex == 1 and ROLE.Leader or ROLE.Member,
		memberNote = note, officerNote = officerNote, classID = CLASS_IDS[classFile or ""], level = level, zone = zone,
		guildRank = rank, guildRankOrder = rankIndex + 1, achievementPoints = 0, isRemoteChat = false,
		lastOnlineYear = years, lastOnlineMonth = months, lastOnlineDay = days, lastOnlineHour = hours,
		faction = UnitFactionGroup("player") == "Horde" and Enum.PvPFaction.Horde or Enum.PvPFaction.Alliance,
	}
end

local function refreshGuild()
	if not IsInGuild() then
		if clubs[GUILD_ID] then clubs[GUILD_ID] = nil; rebuildOrder(); Fire("CLUB_REMOVED", GUILD_ID) end
		return
	end
	local isNew = clubs[GUILD_ID] == nil
	local club = newClub(GUILD_ID, CT.Guild, "guild")
	local name = GetGuildInfo("player")
	club.info.name, club.info.shortName = name or GUILD, name or GUILD
	club.info.description = GetGuildInfoText() or ""
	club.info.broadcast = GetGuildRosterMOTD() or ""
	club.streams[1] = { streamId = 1, name = GUILD, subject = "", leadersAndModeratorsOnly = false, streamType = STREAM.Guild, creationTime = 0 }
	club.streamOrder = { 1 }
	if CanEditOfficerNote() or CanViewOfficerNote() then
		club.streams[2] = { streamId = 2, name = OFFICER, subject = "", leadersAndModeratorsOnly = true, streamType = STREAM.Officer, creationTime = 0 }
		club.streamOrder[2] = 2
	end
	wipe(club.members)
	local count = 0
	for i = 1, GetNumGuildMembers(true) do
		local m = guildMember(club, i)
		if m then club.members[m.memberId] = m; count = count + 1 end
	end
	club.info.memberCount = count
	club.complete[1], club.complete[2] = true, true -- guild chat has no stored history to page through
	if isNew then rebuildOrder(); Fire("CLUB_ADDED", GUILD_ID); Fire("CLUB_STREAMS_LOADED", GUILD_ID) end
	Fire("CLUB_MEMBERS_UPDATED", GUILD_ID)
	Fire("CLUB_UPDATED", GUILD_ID)
end

local function guildMessage(streamId, text, sender)
	local club = clubs[GUILD_ID]
	if not club then return end
	guildChat = guildChat + 1
	local author
	for _, m in pairs(club.members) do if m.name == sender then author = m break end end
	author = author or { name = sender, memberId = memberId(club, sender), clubType = CT.Guild, isSelf = sender == UnitName("player") }
	addMessage(club, streamId, guildChat, text, author)
	Fire("CLUB_MESSAGE_ADDED", GUILD_ID, streamId, { epoch = 0, position = guildChat })
end

do
	local frame = CreateFrame("Frame")
	for _, event in ipairs({ "PLAYER_ENTERING_WORLD", "PLAYER_GUILD_UPDATE", "GUILD_ROSTER_UPDATE", "GUILD_MOTD",
			"CHAT_MSG_GUILD", "CHAT_MSG_OFFICER" }) do frame:RegisterEvent(event) end
	frame:SetScript("OnEvent", function(_, event, text, sender)
		if event == "CHAT_MSG_GUILD" then guildMessage(1, text, sender)
		elseif event == "CHAT_MSG_OFFICER" then guildMessage(2, text, sender)
		else
			if event == "PLAYER_ENTERING_WORLD" and IsInGuild() then GuildRoster() end
			refreshGuild()
		end
	end)
end

-- Communities ------------------------------------------------------------------------------------------------------
local ROLES = { owner = ROLE.Owner, admin = ROLE.Leader, moderator = ROLE.Moderator, member = ROLE.Member }
local ROLE_NAMES = { [ROLE.Owner] = "owner", [ROLE.Leader] = "admin", [ROLE.Moderator] = "moderator", [ROLE.Member] = "member" }
local selfTag

local function presenceOf(tag)
	if tag == selfTag then return PRESENCE.Online end
	for _, f in ipairs(EsteriaRetailUI.bnet and EsteriaRetailUI.bnet.friends or {}) do
		if f.tag == tag then
			return f.presence == "online" and PRESENCE.Online or f.presence == "away" and PRESENCE.Away
				or f.presence == "dnd" and PRESENCE.Busy or PRESENCE.Offline
		end
	end
	return PRESENCE.Unknown
end

local function communityMember(club, m)
	return { isSelf = m.tag == selfTag, memberId = memberId(club, m.tag), name = m.displayName, role = ROLES[m.role] or ROLE.Member,
		presence = presenceOf(m.tag), clubType = CT.BattleNet, tag = m.tag, memberNote = "", achievementPoints = 0, isRemoteChat = false }
end

local function applySummary(c)
	local id = COMMUNITY_BASE + c.id
	local isNew = clubs[id] == nil
	local club = newClub(id, CT.BattleNet, "community", c.id)
	club.info.name, club.info.shortName = c.name, c.name
	club.info.description, club.info.broadcast = c.description or "", ""
	club.info.memberCount = c.memberCount or 0
	club.role, club.notify = ROLES[c.role] or ROLE.Member, c.notify
	wipe(club.streamOrder)
	for i, ch in ipairs(c.channels or {}) do
		club.streams[ch.id] = { streamId = ch.id, name = ch.name, subject = ch.topic or "", creationTime = 0,
			leadersAndModeratorsOnly = ch.postRole ~= "member", streamType = i == 1 and STREAM.General or STREAM.Other, unread = ch.unread or 0 }
		club.streamOrder[i] = ch.id
	end
	return club, isNew
end

local function loadMembers(club)
	request({ op = "cGet", id = club.remoteId }, function(result)
		if result.error then return end
		applySummary(result)
		wipe(club.members)
		for _, m in ipairs(result.members or {}) do
			local member = communityMember(club, m)
			club.members[member.memberId] = member
		end
		Fire("CLUB_MEMBERS_UPDATED", club.id)
	end)
end

function EsteriaRetailUI.RefreshCommunities()
	selfTag = EsteriaRetailUI.bnet and EsteriaRetailUI.bnet.me and EsteriaRetailUI.bnet.me.tag
	request({ op = "cList" }, function(result)
		if result.error then return end
		local seen = {}
		for _, c in ipairs(result.communities or {}) do
			local club, isNew = applySummary(c)
			seen[club.id] = true
			if isNew then
				rebuildOrder()
				Fire("CLUB_ADDED", club.id)
				Fire("CLUB_STREAMS_LOADED", club.id)
				loadMembers(club)
			end
			Fire("CLUB_UPDATED", club.id)
		end
		for id in pairs(clubs) do
			if id >= COMMUNITY_BASE and not seen[id] then clubs[id] = nil; rebuildOrder(); Fire("CLUB_REMOVED", id) end
		end
		local before = #invitations
		wipe(invitations)
		for _, invite in ipairs(result.invites or {}) do
			invitations[#invitations + 1] = { invitationId = invite.communityId, inviter = { name = invite.from },
				club = { clubId = COMMUNITY_BASE + invite.communityId, name = invite.name, shortName = invite.name,
					clubType = CT.BattleNet, avatarId = 0, memberCount = 0, description = "", broadcast = "" }, leaders = {} }
			if #invitations > before then Fire("CLUB_INVITATION_ADDED_FOR_SELF", invitations[#invitations]) end
		end
		Fire("INITIAL_CLUBS_LOADED")
	end)
end

local function fetchHistory(club, streamId, before)
	request({ op = "cHistory", id = club.remoteId, channelId = streamId, before = before, limit = 50 }, function(result)
		if result.error then return end
		local wasLoaded = club.loaded[streamId]
		local known = {}
		local newest = 0
		for _, existing in ipairs(club.messages[streamId] or {}) do known[existing.messageId.position] = true; newest = math.max(newest, existing.messageId.position) end
		local added = {}
		for _, m in ipairs(result.messages or {}) do
			local author = club.members[club.memberIds[m.from] or 0]
				or { name = m.displayName, memberId = memberId(club, m.from), clubType = CT.BattleNet, isSelf = m.from == selfTag }
			addMessage(club, streamId, m.id, m.body, author)
			if wasLoaded and not known[m.id] and m.id > newest then added[#added + 1] = m.id end
		end
		if not result.hasMore then club.complete[streamId] = true end
		club.loaded[streamId] = true
		local list = club.messages[streamId] or {}
		local range = list[1] and { oldestMessageId = list[1].messageId, newestMessageId = list[#list].messageId }
		if not wasLoaded then Fire("CLUB_MESSAGE_HISTORY_RECEIVED", club.id, streamId, range, range) end
		-- Messages that arrived since the last read (the other surface of this account, or a missed event) are new to the window.
		for _, id in ipairs(added) do Fire("CLUB_MESSAGE_ADDED", club.id, streamId, { epoch = 0, position = id }) end
	end)
end

do -- communities load with the Battle.net data, and follow community events
	local frame, waited = CreateFrame("Frame"), 0
	frame:SetScript("OnUpdate", function(self, delta)
		waited = waited + delta
		if EsteriaRetailUI.bnet and EsteriaRetailUI.bnet.connected then
			self:SetScript("OnUpdate", nil)
			EsteriaRetailUI.RefreshCommunities()
		elseif waited > 60 then
			self:SetScript("OnUpdate", nil)
		end
	end)
	if EsteriaChat and EsteriaChat.listen then
		EsteriaChat.listen(function(event)
			local data = event.data or {}
			if event.type == "community_message" and data.communityId then
				local club = clubs[COMMUNITY_BASE + data.communityId]
				if club and data.channelId then fetchHistory(club, data.channelId) end
			elseif event.type == "community_invite" or event.type == "community_removed" then
				EsteriaRetailUI.RefreshCommunities()
			end
		end)
	end
end

-- The service announces a message to everyone but its sender, so a second surface of the same account (the launcher) is
-- never told: while the window is open, reread the shown channel every few seconds.
do
	local frame, waited = CreateFrame("Frame"), 0
	frame:SetScript("OnUpdate", function(_, delta)
		waited = waited + delta
		if waited < 8 then return end
		waited = 0
		local window = RetailCommunitiesFrame
		if not (window and window:IsShown()) then return end
		local id = window:GetSelectedClubId()
		local club, streamId = id and clubs[id], window.GetSelectedStreamId and window:GetSelectedStreamId()
		if club and club.remoteId and streamId then fetchHistory(club, streamId) end
	end)
end

local function after(callback) return function(result) if not result.error then EsteriaRetailUI.RefreshCommunities() end if callback then callback(result) end end end

-- RetailC_Club -----------------------------------------------------------------------------------------------------
local C = RetailC_Club
local function club(id) return clubs[id] end

C.GetSubscribedClubs = function()
	local list = {}
	for _, id in ipairs(order) do list[#list + 1] = clubs[id].info end
	return list
end
C.GetClubInfo = function(id) local c = club(id) return c and c.info end
C.GetGuildClubId = function() return clubs[GUILD_ID] and GUILD_ID or nil end
C.GetStreams = function(id)
	local c, list = club(id), {}
	if c then for _, streamId in ipairs(c.streamOrder) do list[#list + 1] = c.streams[streamId] end end
	return list
end
C.GetStreamInfo = function(id, streamId) local c = club(id) return c and c.streams[streamId] end
C.GetMemberIds = function(id)
	local c, ids = club(id), {}
	if c then for memberIdValue in pairs(c.members) do ids[#ids + 1] = memberIdValue end end
	return ids
end
C.GetClubMembers = C.GetMemberIds
C.GetMemberInfo = function(id, memberIdValue) local c = club(id) return c and c.members[memberIdValue] end
C.GetMemberInfoForSelf = function(id)
	local c = club(id)
	if not c then return end
	for _, m in pairs(c.members) do if m.isSelf then return m end end
	return { isSelf = true, memberId = 0, name = UnitName("player"), role = c.role or ROLE.Member, clubType = c.info.clubType, presence = PRESENCE.Online }
end
C.AreMembersReady = function() return true end
C.FocusMembers = function() end
C.ClearClubPresenceSubscription = function() end

C.GetClubPrivileges = function(id)
	local c = club(id)
	local role = c and (c.source == "guild" and (C.GetMemberInfoForSelf(id) or {}).role or c.role) or ROLE.Member
	local leader, mod = role <= ROLE.Leader, role <= ROLE.Moderator
	local guild = c and c.source == "guild"
	return {
		canDestroy = role == ROLE.Owner and not guild, canSetAttribute = leader, canSetName = leader and not guild,
		canSetDescription = leader or (guild and CanEditGuildInfo()), canSetAvatar = false,
		canSetBroadcast = guild and CanEditMOTD() or leader, canSetPrivacyLevel = false,
		canSetOwnMemberAttribute = true, canSetOtherMemberAttribute = leader, canSetOwnMemberNote = guild,
		canSetOtherMemberNote = guild and CanEditPublicNote(), canSetOwnVoiceState = false, canSetOwnPresenceLevel = false,
		canUseVoice = false, canVoiceMuteMemberForAll = false, canGetInvitation = mod, canSendInvitation = guild and CanGuildInvite() or mod,
		canSendGuestInvitation = false, canRevokeOwnInvitation = mod, canRevokeOtherInvitation = leader, canGetBan = mod,
		canGetSuggestion = false, canSuggestMember = false, canGetTicket = mod and not guild, canCreateTicket = mod and not guild,
		canDestroyTicket = mod and not guild, canAddBan = mod and not guild, canRemoveBan = mod and not guild,
		canCreateStream = leader and not guild, canDestroyStream = leader and not guild, canSetStreamPosition = false,
		canSetStreamAttribute = leader and not guild, canSetStreamName = leader and not guild, canSetStreamSubject = mod and not guild,
		canSetStreamAccess = leader and not guild, canSetStreamVoiceLevel = false, canCreateMessage = true,
		canDestroyOwnMessage = not guild, canDestroyOtherMessage = mod and not guild, canEditOwnMessage = false,
		canPinMessage = false, kickableRoleIds = mod and { ROLE.Member } or {},
	}
end
C.GetAssignableRoles = function(id)
	local c = club(id)
	if not c or c.source == "guild" or (c.role or ROLE.Member) > ROLE.Leader then return {} end
	return c.role == ROLE.Owner and { ROLE.Leader, ROLE.Moderator, ROLE.Member } or { ROLE.Moderator, ROLE.Member }
end

-- Messages
C.IsSubscribedToStream = function() return true end
C.FocusStream = function(id, streamId)
	local c = club(id)
	if c and c.source == "community" and not c.loaded[streamId] then fetchHistory(c, streamId) end
	return true
end
C.UnfocusStream = function() end
C.GetMessageRanges = function(id, streamId)
	local c = club(id)
	local list = c and c.messages[streamId]
	if not list or not list[1] then return {} end
	return { { oldestMessageId = list[1].messageId, newestMessageId = list[#list].messageId } }
end
C.GetMessagesInRange = function(id, streamId, oldest, newest)
	local c, out = club(id), {}
	for _, m in ipairs(c and c.messages[streamId] or {}) do
		local p = m.messageId.position
		if p >= oldest.position and p <= newest.position then out[#out + 1] = m end
	end
	return out
end
C.GetMessagesBefore = function(id, streamId, newest, count)
	local c, out = club(id), {}
	local list = c and c.messages[streamId] or {}
	for i = #list, 1, -1 do
		if list[i].messageId.position <= newest.position then table.insert(out, 1, list[i]) end
		if #out >= count then break end
	end
	return out
end
C.GetMessageInfo = function(id, streamId, messageId)
	local c = club(id)
	for _, m in ipairs(c and c.messages[streamId] or {}) do
		if m.messageId.position == messageId.position then return m end
	end
end
C.IsBeginningOfStream = function(id, streamId, messageId)
	local c = club(id)
	local list = c and c.messages[streamId]
	return not c or c.complete[streamId] and (not list or not list[1] or list[1].messageId.position == messageId.position)
end
C.RequestMoreMessagesBefore = function(id, streamId, messageId)
	local c = club(id)
	if not c or c.complete[streamId] then return true end
	fetchHistory(c, streamId, messageId and messageId.position)
	return false
end
C.SendMessage = function(id, streamId, text)
	local c = club(id)
	if not c then return end
	if c.source == "guild" then
		SendChatMessage(text, streamId == 2 and "OFFICER" or "GUILD")
	else
		request({ op = "cSend", id = c.remoteId, channelId = streamId, body = text }, function(result)
			if result.error or not result.message then return end
			local m = result.message
			addMessage(c, streamId, m.id, m.body, C.GetMemberInfoForSelf(id))
			Fire("CLUB_MESSAGE_ADDED", id, streamId, { epoch = 0, position = m.id })
		end)
	end
end
C.DestroyMessage = function(id, streamId, messageId)
	local c = club(id)
	if c and c.source == "community" then request({ op = "cMessageDelete", id = c.remoteId, messageId = messageId.position }, after()) end
end
C.GetStreamViewMarker = function() return 0 end
C.AdvanceStreamViewMarker = function(id, streamId)
	local c = club(id)
	local list = c and c.messages[streamId]
	if c and c.source == "community" and list and list[#list] then
		request({ op = "cRead", id = c.remoteId, channelId = streamId, cursor = list[#list].messageId.position })
		if c.streams[streamId] then c.streams[streamId].unread = 0 end
	end
end
C.SetAutoAdvanceStreamViewMarker = function() end
C.ClearAutoAdvanceStreamViewMarker = function() end
C.DoesAnyCommunityHaveUnreadMessages = function()
	for id, c in pairs(clubs) do
		if id >= COMMUNITY_BASE then for _, s in pairs(c.streams) do if (s.unread or 0) > 0 then return true end end end
	end
	return false
end
C.GetClubStreamNotificationSettings = function(id)
	local c = club(id)
	local settings = {}
	for _, streamId in ipairs(c and c.streamOrder or {}) do
		settings[#settings + 1] = { streamId = streamId, filter = Enum.ClubStreamNotificationFilter.All }
	end
	return settings
end
C.SetClubStreamNotificationSettings = function() end

-- Management (communities)
C.CreateClub = function(name, shortName, description) request({ op = "cCreate", name = name, description = description }, after()) end
C.EditClub = function(id, name, shortName, description)
	local c = club(id)
	if c and c.source == "community" then request({ op = "cUpdate", id = c.remoteId, name = name, description = description }, after()) end
	if c and c.source == "guild" and description then SetGuildInfoText(description) end
end
C.DestroyClub = function(id) local c = club(id) if c and c.source == "community" then request({ op = "cDelete", id = c.remoteId }, after()) end end
C.LeaveClub = function(id)
	local c = club(id)
	if c and c.source == "guild" then GuildLeave() elseif c then request({ op = "cLeave", id = c.remoteId }, after()) end
end
C.CreateStream = function(id, name, subject, leadersAndModeratorsOnly)
	local c = club(id)
	if c then request({ op = "cChannelCreate", id = c.remoteId, name = name, topic = subject, postRole = leadersAndModeratorsOnly and "moderator" or "member" }, after()) end
end
C.EditStream = function(id, streamId, name, subject, leadersAndModeratorsOnly)
	local c = club(id)
	if c then request({ op = "cChannelUpdate", id = c.remoteId, channelId = streamId, name = name, topic = subject,
		postRole = leadersAndModeratorsOnly and "moderator" or "member" }, after()) end
end
C.DestroyStream = function(id, streamId) local c = club(id) if c then request({ op = "cChannelDelete", id = c.remoteId, channelId = streamId }, after()) end end
local function tagOf(c, memberIdValue) local m = c and c.members[memberIdValue] return m and m.tag end
C.KickMember = function(id, memberIdValue)
	local c = club(id)
	if c and c.source == "guild" then local m = c.members[memberIdValue] if m then GuildUninvite(m.name) end
	elseif tagOf(c, memberIdValue) then request({ op = "cKick", id = c.remoteId, tag = tagOf(c, memberIdValue) }, function() loadMembers(c) end) end
end
C.AssignMemberRole = function(id, memberIdValue, role)
	local c = club(id)
	if tagOf(c, memberIdValue) then request({ op = "cRole", id = c.remoteId, tag = tagOf(c, memberIdValue), role = ROLE_NAMES[role] }, function() loadMembers(c) end) end
end
C.SendInvitation = function(id, memberIdOrTag)
	local c = club(id)
	if c and c.source == "community" and type(memberIdOrTag) == "string" then request({ op = "cInviteTag", id = c.remoteId, tag = memberIdOrTag }, after()) end
end
C.GetInvitationsForSelf = function() return invitations end
C.GetInvitationInfo = function(id) for _, i in ipairs(invitations) do if i.club.clubId == id then return i end end end
C.AcceptInvitation = function(id) request({ op = "cInviteRespond", id = id - COMMUNITY_BASE, accept = true }, after()) end
C.DeclineInvitation = function(id) request({ op = "cInviteRespond", id = id - COMMUNITY_BASE, accept = false }, after()) end
C.RequestInvitationsForClub = function() end
C.GetInvitationsForClub = function() return {} end
C.RevokeInvitation = function() end
C.RequestTickets = function() end
C.GetTickets = function() return {} end
C.CreateTicket = function(id)
	local c = club(id)
	if c then request({ op = "cInviteCreate", id = c.remoteId }, function(result)
		if result.code then Fire("CLUB_TICKET_CREATED", id, { ticketId = result.code, uses = 0, allowedRedeemCount = result.maxUses or 0, creator = {}, exactExpirationTime = 0 }) end
	end) end
end
C.DestroyTicket = function() end
C.RequestTicket = function() end
C.RedeemTicket = function(code) request({ op = "cJoin", code = code }, after()) end
C.GetLastTicketResponse = function() return Enum.ClubErrorType and Enum.ClubErrorType.ErrorCommunitiesNone or 0 end
C.GetClubCapacity = function() return 1000 end
C.GetClubLimits = function() return { maximumNumberOfStreams = 20 } end
C.ShouldAllowClubType = function(clubType) return clubType == CT.Guild or clubType == CT.BattleNet end
C.IsEnabled = function() return true end
C.IsRestricted = function() return Enum.ClubRestrictionReason and Enum.ClubRestrictionReason.None or 0 end
C.ValidateText = function() return Enum.ValidateNameResult and Enum.ValidateNameResult.Success or 0 end
C.GetCommunityNameResultText = function() return nil end
C.GetAvatarIdList = function() return {} end
C.SetAvatarTexture = function(texture, avatarId, clubType)
	-- Guild: the guild tabard icon; communities: the retail community emblem.
	if clubType == CT.Guild then texture:SetTexture("Interface\\Icons\\INV_Shirt_GuildTabard_01")
	else texture:SetTexture("Interface\\Icons\\INV_Misc_GroupNeedMore") end
end
C.SetFavorite = function() end
C.SetSocialQueueingEnabled = function() end
C.IsAccountMuted = function() return false end
C.CompareBattleNetDisplayName = function(id, a, b) return (a or "") < (b or "") end
C.DoesCommunityHaveMembersOfTheOppositeFaction = function() return false end
C.CanResolvePlayerLocationFromClubMessageData = function() return false end
C.SendBattleTagFriendRequest = function(id, memberIdValue)
	local tag = tagOf(club(id), memberIdValue)
	if tag then RetailBNSendFriendInvite(tag) end
end
C.SendTitleFriendRequest = C.SendBattleTagFriendRequest
C.Flush = function() end
C.GetClubSessionUserIDs = function() return {} end

-- RetailC_GuildInfo ------------------------------------------------------------------------------------------------
local G = RetailC_GuildInfo
G.GuildRoster = function() GuildRoster() end
G.GetMOTD = function() return GetGuildRosterMOTD() end
G.SetMOTD = function(text) GuildSetMOTD(text) end
G.GetInfoText = function() return GetGuildInfoText() end
G.SetInfoText = function(text) SetGuildInfoText(text) end
G.CanEditOfficerNote = function() return CanEditOfficerNote() and true or false end
G.CanViewOfficerNote = function() return CanViewOfficerNote() and true or false end
G.CanSpeakInGuildChat = function() return IsInGuild() and true or false end
G.IsGuildOfficer = function() return CanGuildPromote() and true or false end
G.IsGuildRankAssignmentAllowed = function() return CanGuildPromote() and true or false end
G.GuildControlGetRankFlags = function(rankOrder) return { GuildControlGetRankFlags and GuildControlGetRankFlags(rankOrder) } end
G.AreGuildEventsEnabled = function() return false end
G.IsEncounterGuildNewsEnabled = function() return false end
G.IsGuildReputationEnabled = function() return false end
G.QueryGuildMemberRecipes = function() end
G.SetGuildRankOrder = function() end
