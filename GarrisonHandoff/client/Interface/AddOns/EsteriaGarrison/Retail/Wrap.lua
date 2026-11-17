-- Keeps Blizzard's architect OnLoad from stopping on pieces this client does not have.
local idle = { Play = function() end, Stop = function() end, IsPlaying = function() return false end }

local function arm(frame)
	if frame and not frame.UpgradeAnim then frame.UpgradeAnim = idle end
	if frame and not frame.BuildingGlowPulseAnim then frame.BuildingGlowPulseAnim = idle end
end

local originalLoad = GarrisonBuildingFrame_OnLoad
function GarrisonBuildingFrame_OnLoad(self)
	if self.FollowerList then
		self.FollowerList.Initialize = self.FollowerList.Initialize or function() end
		self.FollowerList.SetSortFuncs = self.FollowerList.SetSortFuncs or function() end
		self.FollowerList.ScrollBox = self.FollowerList.ScrollBox or { SetDataProvider = function() end }
	end
	if self.BuildingList then
		self.BuildingList.Buttons = self.BuildingList.Buttons or {}
		if self.BuildingList.Button1 and not self.BuildingList.Buttons[1] then
			self.BuildingList.Buttons[1] = self.BuildingList.Button1
		end
	end
	arm(self.InfoBox)
	arm(self.TownHallBox)
	if self.MapFrame and self.MapFrame.TownHall then arm(self.MapFrame.TownHall) end
	originalLoad(self)
end

local function adapt(name)
	local dialog = StaticPopupDialogs[name]
	if dialog and dialog.OnAccept then
		local accept = dialog.OnAccept
		dialog.OnAccept = function(self, data) accept(self, data or self.data) end
	end
end
adapt("GARRISON_CANCEL_BUILD_BUILDING")
adapt("GARRISON_CANCEL_UPGRADE_BUILDING")
