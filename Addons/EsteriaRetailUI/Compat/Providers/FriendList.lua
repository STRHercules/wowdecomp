-- RetailC_FriendList: character friends, ignores and /who on the 3.3.5a functions.
local FL = RetailC_FriendList

FL.ShowFriends = ShowFriends
FL.GetNumFriends = GetNumFriends
FL.GetNumOnlineFriends = function()
	local online = 0
	for i = 1, GetNumFriends() do if select(5, GetFriendInfo(i)) then online = online + 1 end end
	return online
end

local function friendInfo(index)
	local name, level, className, area, connected, status, notes = GetFriendInfo(index)
	if not name then return end
	return { name = name, level = level, className = className, area = area, connected = connected and true or false,
		notes = notes, afk = status == CHAT_FLAG_AFK, dnd = status == CHAT_FLAG_DND, rafLinkType = 0, mobile = false }
end
FL.GetFriendInfoByIndex = friendInfo
FL.GetFriendInfo = function(name)
	for i = 1, GetNumFriends() do
		if (GetFriendInfo(i)) == name then return friendInfo(i) end
	end
end
FL.IsFriend = function() return false end -- by GUID; 3.3.5a friends are keyed by name
FL.AddFriend = function(name, notes) AddFriend(name) if notes and notes ~= "" then SetFriendNotes(name, notes) end end
FL.AddOrRemoveFriend = function(name, notes) if FL.GetFriendInfo(name) then RemoveFriend(name) else FL.AddFriend(name, notes) end end
FL.RemoveFriend = function(name) RemoveFriend(name) return true end
FL.RemoveFriendByIndex = function(index) RemoveFriend(index) end
FL.SetFriendNotes = function(name, notes) SetFriendNotes(name, notes) return true end
FL.SetFriendNotesByIndex = function(index, notes) SetFriendNotes(index, notes) end
FL.GetSelectedFriend = GetSelectedFriend
FL.SetSelectedFriend = SetSelectedFriend

FL.GetNumIgnores = GetNumIgnores
FL.GetIgnoreName = GetIgnoreName
FL.AddIgnore = function(name) AddIgnore(name) return true end
FL.DelIgnore = function(name) DelIgnore(name) return true end
FL.AddOrDelIgnore = AddOrDelIgnore
FL.DelIgnoreByIndex = function(index) local name = GetIgnoreName(index) if name then DelIgnore(name) end end
FL.GetSelectedIgnore = GetSelectedIgnore
FL.SetSelectedIgnore = SetSelectedIgnore
FL.IsIgnored = function(token)
	for i = 1, GetNumIgnores() do if GetIgnoreName(i) == token then return true end end
	return false
end
FL.IsOnIgnoredList = FL.IsIgnored
FL.IsIgnoredByGuid = function() return false end

FL.SendWho = function(filter) SendWho(filter or "") end
FL.GetNumWhoResults = GetNumWhoResults
FL.GetWhoInfo = function(index)
	local name, guild, level, race, class, zone, classFile = GetWhoInfo(index)
	if not name then return end
	return { fullName = name, fullGuildName = guild, level = level, raceStr = race, classStr = class, area = zone,
		filename = classFile, gender = 0 }
end
FL.SortWho = function(sort) SortWho(sort) end
FL.SetWhoToUi = function(toUi) SetWhoToUI(toUi and 1 or 0) end
