-- Retail events the 3.3.5a engine never sends (BN_*, CLUB_*, SOCIAL_QUEUE_* ...): frames that register for them are
-- remembered here, and the providers deliver them with EsteriaRetailUI.Fire(event, ...).
local registered = {} -- event -> { [frame] = true } (weak keys: frames are never released, but stay tidy)

local function track(methods)
	local register, unregister, unregisterAll = methods.RegisterEvent, methods.UnregisterEvent, methods.UnregisterAllEvents
	if not register or methods.retailEventsTracked then return end
	methods.retailEventsTracked = true
	methods.RegisterEvent = function(self, event, ...)
		if type(event) == "string" then
			registered[event] = registered[event] or setmetatable({}, { __mode = "k" })
			registered[event][self] = true
		end
		return register(self, event, ...)
	end
	methods.UnregisterEvent = function(self, event, ...)
		if registered[event] then registered[event][self] = nil end
		return unregister(self, event, ...)
	end
	methods.UnregisterAllEvents = function(self, ...)
		for _, frames in pairs(registered) do frames[self] = nil end
		return unregisterAll(self, ...)
	end
end
for _, methods in pairs(EsteriaRetailUI.widgetTypes) do track(methods) end

function EsteriaRetailUI.Fire(event, ...)
	for frame in pairs(registered[event] or {}) do
		local handler = frame:GetScript("OnEvent")
		if handler then
			local ok, err = pcall(handler, frame, event, ...)
			if not ok then geterrorhandler()(err) end
		end
	end
end

-- 3.3.5a has no GLOBAL_MOUSE_DOWN / GLOBAL_MOUSE_UP: menus and popups rely on them to close, so send them by polling.
do
	local pressed, watcher = {}, CreateFrame("Frame")
	watcher:SetScript("OnUpdate", function()
		for _, button in ipairs({ "LeftButton", "RightButton" }) do
			local down = IsMouseButtonDown(button) and true or false
			if down ~= (pressed[button] or false) then
				pressed[button] = down
				if EsteriaRetailUI.debugMouse and down then
					local focus = GetMouseFocus()
					DEFAULT_CHAT_FRAME:AddMessage("mouse " .. button .. " focus=" .. tostring(focus and (focus:GetName() or focus:GetObjectType())) .. " menu=" .. tostring(RetailMenu and RetailMenu.GetManager and RetailMenu.GetManager():GetOpenMenu() ~= nil))
				end
				EsteriaRetailUI.Fire(down and "GLOBAL_MOUSE_DOWN" or "GLOBAL_MOUSE_UP", button)
			end
		end
	end)
end
