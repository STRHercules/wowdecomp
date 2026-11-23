-- Engine globals the retail framework expects, in their 3.3.5a form. Only names the stock client lacks are set.
local function provide(name, value) if _G[name] == nil then _G[name] = value end end
local log = DevHarness_Log or function() end

-- Secure execution: 3.3.5a has securecall; retail's variants call through it.
provide("securecallfunction", function(func, ...) return securecall(func, ...) end)
provide("secureexecuterange", function(tbl, func, ...)
	for key, value in pairs(tbl) do securecall(func, key, value, ...) end
end)
provide("forceinsecure", function() end)
provide("securecallmethod", function(object, method, ...) return securecall(object[method], object, ...) end)

-- 12.x secret values do not exist here: every value is ordinary and accessible.
provide("issecretvalue", function() return false end)
provide("issecrettable", function() return false end)
provide("canaccessvalue", function() return true end)
provide("canaccessallvalues", function() return true end)
provide("canaccesstable", function() return true end)
provide("hasanysecretvalues", function() return false end)
provide("secretwrap", function(...) return ... end)
provide("scrubsecretvalues", function(...) return ... end)

provide("tostringall", function(...)
	local n = select("#", ...)
	if n == 0 then return end
	local t = { ... }
	for i = 1, n do t[i] = tostring(t[i]) end
	return unpack(t, 1, n)
end)

-- Secure delegates run their function in a secure context on retail; here the function itself is enough.
provide("CreateSecureDelegate", function(func) return func end)
provide("GetCallstackHeight", function() return 0 end)

provide("SetErrorCallstackHeight", function() end)

-- Battle.net client programs and the game flavour the friends list compares against.
provide("BNET_CLIENT_WOW", "WoW")
provide("BNET_CLIENT_APP", "App")
provide("BNET_CLIENT_CLNT", "CLNT")
provide("WOW_PROJECT_MAINLINE", 1)
provide("WOW_PROJECT_ID", 1)

-- Retail systems the social windows only touch in passing (tutorial tips, toast anchoring, text scaling); each call
-- is a no-op here and returns nothing.
local function inert(name)
	return setmetatable({}, { __index = function(t, k) local f = function() end rawset(t, k, f) return f end })
end
provide("HelpTip", inert("HelpTip"))
provide("ChatAlertFrame", inert("ChatAlertFrame"))
provide("GuildMicroButton", inert("GuildMicroButton")) -- retail's guild micro button; the window updates its badge
provide("GUILD_EVENT_TEXTURES", {})
-- Text scale is always 1: UI-scaled sizes are the authored ones.
provide("TextSizeManager", {
	OnTextScaleUpdated = "TextSizeManager.OnTextScaleUpdated",
	RegisterObject = function() end, UpdateObject = function() end, Init = function() end,
	GetScale = function() return 1 end, GetWeightedScale = function() return 1 end,
	GetScaledValue = function(_, value) return value end, GetScaledValueWeighted = function(_, value) return value end,
	GetSettingValue = function() return 1 end, GetSettingDefaultValue = function() return 1 end,
	GetInitialUpdateEvents = function() return {} end,
})

-- 3.3.5a has no class or guild challenge queries; the windows that use them (Guild Finder, challenges) are shown
-- disabled, so they see none.
provide("GetNumClasses", function() return 0 end)
provide("RequestGuildChallengeInfo", function() end)
provide("GetGuildChallengeInfo", function() end)
provide("GetNumGuildChallenges", function() return 0 end)

-- 12.x additions to the math and table libraries.
local function lib(t, name, func) if t[name] == nil then t[name] = func end end
lib(math, "round", function(x) return x >= 0 and math.floor(x + 0.5) or math.ceil(x - 0.5) end)
lib(math, "clamp", function(x, lo, hi) return math.min(math.max(x, lo), hi) end)
lib(math, "saturate", function(x) return math.min(math.max(x, 0), 1) end)
lib(math, "lerp", function(a, b, t) return a + (b - a) * t end)
lib(math, "sign", function(x) return x > 0 and 1 or (x < 0 and -1 or 0) end)
lib(math, "normalize", function(x, lo, hi) return hi == lo and 0 or (x - lo) / (hi - lo) end)
lib(math, "wrap", function(x, lo, hi) local r = hi - lo return r == 0 and lo or lo + ((x - lo) % r) end)
lib(table, "count", function(t) local n = 0 for _ in pairs(t) do n = n + 1 end return n end)
lib(table, "contains", function(t, value) for _, v in pairs(t) do if v == value then return true end end return false end)
lib(table, "indexof", function(t, value) for i, v in ipairs(t) do if v == value then return i end end end)
lib(table, "isempty", function(t) return next(t) == nil end)
lib(table, "create", function() return {} end)
lib(table, "keys", function(t) local k = {} for key in pairs(t) do k[#k + 1] = key end return k end)
lib(table, "values", function(t) local v = {} for _, value in pairs(t) do v[#v + 1] = value end return v end)
lib(table, "removevalue", function(t, value)
	for i, v in ipairs(t) do if v == value then table.remove(t, i) return i end end
end)
lib(table, "removeunordered", function(t, index)
	local n = #t
	local value = t[index]
	t[index] = t[n]
	t[n] = nil
	return value
end)

-- Debug/source tracking hooks of the retail engine.
provide("AddSourceLocationExclude", function() end)
provide("GetCurrentEnvironment", function() return _G end)
provide("SwapToGlobalEnvironment", function() end)
provide("IsOnGlueScreen", function() return false end)
provide("IsPublicBuild", function() return true end)
provide("IsGMClient", function() return false end)
provide("Kiosk", { IsEnabled = function() return false end, IsCompetitiveModeEnabled = function() return false end })

-- Unknown Enum/Constants members read as 0 (logged once) instead of erroring at load.
local reported = {}
function EsteriaRetailUI.lenient(root, rootName)
	return setmetatable(root, { __index = function(t, k)
		local key = rootName .. "." .. tostring(k)
		if not reported[key] then reported[key] = true; log("RetailUI: missing " .. key) end
		local inner = setmetatable({}, { __index = function(_, field)
			local fieldKey = key .. "." .. tostring(field)
			if not reported[fieldKey] then reported[fieldKey] = true; log("RetailUI: missing " .. fieldKey) end
			return 0
		end })
		rawset(t, k, inner)
		return inner
	end })
end
