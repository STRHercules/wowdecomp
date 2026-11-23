-- ChatFrameUtil (retail Blizzard_ChatFrameBase, not extracted): the Communities window's calls into the chat frame,
-- mapped onto the 3.3.5a ChatFrame/ChatEdit functions. Community chat channels live in the Communities window itself,
-- so the "add as chat channel" calls answer no.
local focusOverride

ChatFrameUtil = ChatFrameUtil or {}
local U = ChatFrameUtil

function U.DisplaySystemMessageInPrimary(message)
	local info = ChatTypeInfo["SYSTEM"]
	DEFAULT_CHAT_FRAME:AddMessage(message, info.r, info.g, info.b)
end
U.AddSystemMessage = U.DisplaySystemMessageInPrimary

function U.SendTell(name, chatFrame) ChatFrame_SendTell(name, chatFrame) end
U.SendBNetTell = U.SendTell -- the Battle.net provider whispers through the character name
function U.OpenChat(text, chatFrame) ChatFrame_OpenChat(text or "", chatFrame) end
function U.InsertLink(link) return link and ChatEdit_InsertLink(link) or false end
function U.LinkItem(_, link) return U.InsertLink(link) end
function U.TruncateToMaxLength(text, maxLength) return text and text:sub(1, maxLength) end
function U.GetTimestampFormat() return nil end -- 3.3.5a chat has no timestamp option

function U.SetChatFocusOverride(editBox) focusOverride = editBox end
function U.GetChatFocusOverride() return focusOverride end
function U.ClearChatFocusOverride() focusOverride = nil end
function U.RegisterForStickyFocus() end
function U.UnregisterForStickyFocus() end

function U.CanAddChannel() return false end
function U.AddNewCommunitiesChannel() end
function U.RemoveCommunitiesChannel() end
function U.GetCommunitiesChannelLocalID() return nil end
function U.GetCommunitiesChannelName() return nil end
-- Guild and officer streams take those chat colours; community streams the generic channel colour.
function U.GetCommunitiesChannelColor(clubId, streamId)
	local stream = RetailC_Club.GetStreamInfo(clubId, streamId)
	local types = Enum.ClubStreamType or {}
	local chatType = stream and (stream.streamType == types.Guild and "GUILD" or stream.streamType == types.Officer and "OFFICER") or "CHANNEL"
	local info = ChatTypeInfo[chatType]
	return info.r, info.g, info.b
end

function U.GetNameForDiscordMessage(info) return info and info.name or "" end
function U.FormatDiscordMessage(_, content) return content end
