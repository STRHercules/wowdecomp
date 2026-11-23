-- Strings the retail XML names that the GlobalStrings export lacks (English).
local strings = {
	BATTLENET_FRIEND_LABEL = "Battle.net Friend",
	COMMUNITIES_ADD_DIALOG_LABEL = "Join or Create a Community",
	COMMUNITIES_ADD_DIALOG_BATTLE_NET_LABEL = "Community",
	COMMUNITIES_ADD_DIALOG_CREATE_WOW_LABEL = "Create a WoW Community",
	COMMUNITIES_ADD_DIALOG_CREATE_WOW_DESCRIPTION = "A community for the characters you play. Members chat in text channels.",
	COMMUNITIES_ADD_DIALOG_CREATE_BNET_LABEL = "Create an Account Community",
	COMMUNITIES_ADD_DIALOG_CREATE_BNET_DESCRIPTION = "A community for your friends' accounts, in the launcher and in game.",
	COMMUNITIES_ADD_DIALOG_INVITE_LINK_LABEL = "Join with an Invite Code",
	COMMUNITIES_ADD_DIALOG_INVITE_LINK_DESCRIPTION = "Enter an invite code to join a community.",
	COMMUNITIES_ADD_DIALOG_INVITE_LINK_JOIN = "Join",
	COMMUNITIES_CREATE_COMMUNITY = "Create Community",
	COMMUNITIES_CREATE_DIALOG_DESCRIPTION_LABEL = "Description (optional)",
	COMMUNITIES_CREATE_DIALOG_NAME_INSTRUCTIONS = "Community name",
	COMMUNITIES_CREATE_DIALOG_SHORT_NAME_INSTRUCTIONS = "Short name",
	COMMUNITIES_CREATE_DIALOG_SHORT_NAME_LABEL = "Short Name",
	ENCOUNTER = "Encounter",
}
for key, text in pairs(strings) do if rawget(_G, key) == nil then _G[key] = text end end
