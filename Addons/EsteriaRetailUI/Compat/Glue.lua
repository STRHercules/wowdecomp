-- Routes the stock entry points to the retail windows: O and the Socials micro button (ToggleFriendsFrame), J and
-- guild links (ToggleGuildFrame), /who results, and anything else that opens the stock FriendsFrame. Loaded after
-- the retail files.
local friends, communities = RetailFriendsFrame, RetailCommunitiesFrame

-- Two templates each give the window a "$parentTitleText"; the global must be the one in the title bar, which is what
-- retail sets for the Who/Raid/Quick Join titles.
if friends and friends.TitleContainer then RetailFriendsFrameTitleText = friends.TitleContainer.TitleText end

UIPanelWindows["RetailFriendsFrame"] = UIPanelWindows["RetailFriendsFrame"] or { area = "left", pushable = 1, whileDead = 1 }
UIPanelWindows["RetailCommunitiesFrame"] = UIPanelWindows["RetailCommunitiesFrame"] or { area = "left", pushable = 0, whileDead = 1 }

-- Stock FriendsFrame tabs: 1 Friends, 2 Who, 3 Guild, 4 Chat, 5 Raid. Retail: 1 Contacts, 2 Who, 3 Raid, 4 Quick Join.
local RETAIL_TAB = { [1] = 1, [2] = 2, [5] = 3 }

function EsteriaRetailUI.ToggleCommunities(show)
	if not communities then return end
	if communities:IsShown() and show == nil then HideUIPanel(communities) else ShowUIPanel(communities) end
end

function ToggleFriendsFrame(tab)
	if tab == 3 then return EsteriaRetailUI.ToggleCommunities() end
	if tab == 4 then return end -- stock chat-channel tab: channels live in the chat frame here
	RetailToggleFriendsFrame(tab and RETAIL_TAB[tab])
end

function ToggleGuildFrame() EsteriaRetailUI.ToggleCommunities() end

-- Anything that still shows the stock window (who results, /friends, other addons) gets the retail one.
if FriendsFrame then
	FriendsFrame:HookScript("OnShow", function(self)
		local tab = PanelTemplates_GetSelectedTab(self) or 1
		HideUIPanel(self)
		if tab == 3 then
			EsteriaRetailUI.ToggleCommunities(true)
		else
			RetailPanelTemplates_SetTab(friends, RETAIL_TAB[tab] or 1)
			if friends:IsShown() then RetailFriendsFrame_OnShow(friends) else ShowUIPanel(friends) end
		end
	end)
end

-- The secure add-dialog files publish CommunitiesOutbound into the "secure environment"; retail references are renamed.
RetailCommunitiesOutbound = RetailCommunitiesOutbound or rawget(_G, "CommunitiesOutbound")

-- Retail's useParentLevel (dialog borders share their parent's level) is unknown to the 3.3.5a loader, which leaves the
-- nine-slice panel at its template level of 500, over the dialog's own text and buttons.
do
	local frame = EnumerateFrames()
	while frame do
		local parent = frame.layoutType == "Dialog" and frame:GetParent()
		if parent and frame:GetFrameLevel() >= 500 then frame:SetFrameLevel(parent:GetFrameLevel()) end
		frame = EnumerateFrames(frame)
	end
end

-- <KeyValue type="global"> values naming a renamed retail table ("COMMUNITIES_FRAME_DISPLAY_MODES.ROSTER") resolve to
-- nil, so the Chat/Roster/Info tabs would select no display mode and hide the whole window.
do
	local modes, frame = RetailCOMMUNITIES_FRAME_DISPLAY_MODES, RetailCommunitiesFrame
	if modes and frame then
		frame.ChatTab.displayMode = modes.CHAT
		frame.RosterTab.displayMode = modes.ROSTER
		frame.GuildBenefitsTab.displayMode = modes.GUILD_BENEFITS
		frame.GuildInfoTab.displayMode = modes.GUILD_INFO
	end
end

SLASH_ESTERIAMOUSE1 = "/ruimouse" -- prints what each click lands on (for debugging menus)
SlashCmdList["ESTERIAMOUSE"] = function()
	EsteriaRetailUI.debugMouse = not EsteriaRetailUI.debugMouse
	DEFAULT_CHAT_FRAME:AddMessage("mouse debug " .. (EsteriaRetailUI.debugMouse and "on" or "off"))
end

-- A script an intrinsic frame's template redefines replaces the intrinsic's own in the 3.3.5a loader, but retail runs the
-- intrinsic's first. For dropdowns that loses the handler that opens the menu (OnMouseDown_Intrinsic) and the tooltip one.
do
	local frame = EnumerateFrames()
	while frame do
		if type(frame.intrinsic) == "string" and frame.intrinsic:find("DropdownButton", 1, true) then
			for _, script in ipairs({ "OnMouseDown", "OnEnter" }) do
				local intrinsic = frame[script .. "_Intrinsic"]
				local own = frame[script] and frame:GetScript(script)
				if intrinsic and own then
					frame:SetScript(script, function(self, ...)
						intrinsic(self, ...)
						return own(self, ...)
					end)
				end
			end
		end
		frame = EnumerateFrames(frame)
	end
end

-- Dialog text that is wider than its dialog runs over the border: the 3.3.5a fonts are wider than retail's. Wrap it.
do
	local function fit(frame, width, depth)
		for _, region in ipairs({ frame:GetRegions() }) do
			if region:GetObjectType() == "FontString" and region:GetText() and region:GetStringWidth() > width then
				region:SetWidth(width)
				region:SetHeight(0)
				region:SetWordWrap(true)
			end
		end
		if depth > 0 then for _, child in ipairs({ frame:GetChildren() }) do fit(child, width, depth - 1) end end
	end
	for _, name in ipairs({ "RetailAddFriendEntryFrame", "RetailRecruitAFriendRecruitmentFrame" }) do
		local dialog = _G[name]
		if dialog then
			dialog:HookScript("OnShow", function(self) fit(self, self:GetWidth() - 56, 2) end)
		end
	end
end

-- The Add Friend dialog's Battle.net badge draws as a black square and a brown block (its logo pieces are not in the sheet
-- this client reads): leave the left badge out; the World of Warcraft icons on the right stay.
do
	local dialog = RetailAddFriendFrame
	if dialog then
		local function hideBadges()
			for _, part in ipairs({ dialog.EntryFrame, dialog.InfoFrame }) do
				local container = part and (part.OptionsContainer or part.InfoContainer)
				local holder = container and container.LeftTextContainer and container.LeftTextContainer.IconHolder
				if holder then for _, icon in ipairs({ holder.FriendIcon, holder.SecondaryIcon }) do if icon then icon:SetAlpha(0) end end end
			end
		end
		dialog:HookScript("OnShow", hideBadges)
		hideBadges()
	end
end
