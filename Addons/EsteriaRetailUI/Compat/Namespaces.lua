-- The retail files call RetailC_<Name> (renamed by Tools/RetailUI/isolate.py): private tables the providers in
-- Compat\Providers fill, so other addons' C_ backports (ClassicAPI) and ours never overwrite each other. Functions no
-- provider defines answer nil and are logged once, so the APIs the windows really call come from the running client.
local log = DevHarness_Log or function() end
local stubbed = {}

local lenient = EsteriaRetailUI.lenient
lenient(Enum, "Enum")
lenient(Constants, "Constants")
if not rawget(Enum, "GameMode") then rawset(Enum, "GameMode", { Standard = 0, Plunderstorm = 1, WoWHack = 2 }) end

for _, name in ipairs(EsteriaRetailUI.namespaces) do
	-- The docs also describe Lua's own libraries (table, string); only the engine's C_ namespaces are made.
	if name:sub(1, 2) == "C_" then
		_G["Retail" .. name] = setmetatable({}, { __index = function(t, fn)
			local stub = function()
				local key = name .. "." .. tostring(fn)
				if not stubbed[key] then stubbed[key] = true; log("RetailUI: stub " .. key) end
			end
			rawset(t, fn, stub)
			return stub
		end })
	end
end
EsteriaRetailUI.stubbed = stubbed
